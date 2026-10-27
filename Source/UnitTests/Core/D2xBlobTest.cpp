// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Swap.h"
#include "DiscIO/D2xBlob.h"

namespace
{
std::vector<u8> EncodeList(std::initializer_list<u32> words)
{
  std::vector<u8> bytes(words.size() * sizeof(u32));
  size_t offset = 0;
  for (const u32 word : words)
  {
    const u32 big_endian = Common::swap32(word);
    std::memcpy(bytes.data() + offset, &big_endian, sizeof(word));
    offset += sizeof(word);
  }
  return bytes;
}

File::IOFile MakeCard()
{
  File::IOFile card(std::tmpfile());
  std::array<u8, 8 * 512> contents;
  for (size_t index = 0; index < contents.size(); ++index)
    contents[index] = static_cast<u8>(index * 7 + index / 512);
  if (!card.WriteArray(contents) || !card.Flush())
    return {};
  return card;
}
}  // namespace

TEST(D2xBlob, ReadsPartialSectorsAcrossFragmentsAndSparseHoles)
{
  // Disc sectors 0, 3 and 5 are holes. Physical sectors need not be sorted.
  const auto list = EncodeList({6, 2, 2, 1, 5, 2, 4, 1, 1});
  std::string error;
  auto reader = DiscIO::D2xBlobReader::Create(MakeCard(), list, &error);
  ASSERT_NE(reader, nullptr) << error;
  EXPECT_EQ(reader->GetDataSize(), 6 * 512);
  std::array<u8, 6 * 512> expected{};
  auto card = MakeCard();
  ASSERT_TRUE(card.Seek(5 * 512, File::SeekOrigin::Begin));
  ASSERT_TRUE(card.ReadBytes(expected.data() + 512, 2 * 512));
  ASSERT_TRUE(card.Seek(512, File::SeekOrigin::Begin));
  ASSERT_TRUE(card.ReadBytes(expected.data() + 4 * 512, 512));

  // Exercise every byte offset, not just sector-aligned reads.
  for (size_t offset = 0; offset < expected.size(); ++offset)
  {
    const size_t bytes = std::min<size_t>(777, expected.size() - offset);
    std::vector<u8> actual(bytes);
    ASSERT_TRUE(reader->Read(offset, bytes, actual.data()));
    ASSERT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin() + offset)) << offset;
  }
  auto copy = reader->CopyReader();
  ASSERT_NE(copy, nullptr);
  std::array<u8, 6 * 512> actual;
  ASSERT_TRUE(copy->Read(0, actual.size(), actual.data()));
  EXPECT_EQ(actual, expected);
}

TEST(D2xBlob, SavestateRoundtripPreservesDeviceAndFragments)
{
  u32 device = 2;
  auto list = EncodeList({6, 2, 2, 1, 5, 2, 4, 1, 1});
  std::string error;
  auto original = DiscIO::D2xBlobReader::Create(MakeCard(), list, &error);
  ASSERT_NE(original, nullptr) << error;

  std::vector<u8> state(2 * sizeof(u32) + list.size());
  u8* cursor = state.data();
  PointerWrap writer(&cursor, state.size(), PointerWrap::Mode::Write);
  writer.Do(device);
  writer.Do(list);
  ASSERT_TRUE(writer.IsWriteMode());

  u32 restored_device = 0;
  std::vector<u8> restored_list;
  cursor = state.data();
  PointerWrap reader(&cursor, state.size(), PointerWrap::Mode::Read);
  reader.Do(restored_device);
  reader.Do(restored_list);
  ASSERT_TRUE(reader.IsReadMode());
  EXPECT_EQ(restored_device, device);
  EXPECT_EQ(restored_list, list);

  auto restored = DiscIO::D2xBlobReader::Create(MakeCard(), restored_list, &error);
  ASSERT_NE(restored, nullptr) << error;
  EXPECT_EQ(restored->GetDataSize(), original->GetDataSize());
  std::array<u8, 6 * 512> original_bytes;
  std::array<u8, 6 * 512> restored_bytes;
  ASSERT_TRUE(original->Read(0, original_bytes.size(), original_bytes.data()));
  ASSERT_TRUE(restored->Read(0, restored_bytes.size(), restored_bytes.data()));
  EXPECT_EQ(restored_bytes, original_bytes);
}

TEST(D2xBlob, RejectsReadsOutsideDiscWithoutChangingOutput)
{
  auto reader = DiscIO::D2xBlobReader::Create(MakeCard(), EncodeList({2, 0, 0}), nullptr);
  ASSERT_NE(reader, nullptr);
  std::array<u8, 8> output;
  output.fill(0xa5);
  EXPECT_FALSE(reader->Read(1020, 8, output.data()));
  EXPECT_FALSE(reader->Read(std::numeric_limits<u64>::max(), 8, output.data()));
  EXPECT_FALSE(reader->Read(1, std::numeric_limits<u64>::max(), output.data()));
  EXPECT_TRUE(reader->Read(1024, 0, output.data()));
  EXPECT_EQ(output.front(), 0xa5);
}

TEST(D2xBlob, RejectsMalformedLists)
{
  const std::vector<std::vector<u8>> invalid_lists{
      {},
      EncodeList({6, 2, 2, 0, 1, 1}),           // Truncated entries.
      EncodeList({6, 1, 0, 0, 1, 1}),           // Count exceeds capacity.
      EncodeList({6, 0, 20001}),                // d2x capacity limit.
      EncodeList({0, 0, 0}),                    // Empty virtual disc.
      EncodeList({0xffffffff, 0, 0}),           // Oversized virtual disc.
      EncodeList({6, 1, 1, 0, 1, 0}),           // Zero-length fragment.
      EncodeList({6, 2, 2, 1, 1, 2, 2, 3, 1}),  // Overlap.
      EncodeList({6, 2, 2, 4, 1, 1, 1, 3, 1}),  // Unsorted virtual offsets.
      EncodeList({6, 1, 1, 5, 1, 2}),           // Beyond virtual disc.
      EncodeList({6, 1, 1, 0, 7, 2}),           // Beyond card image.
      EncodeList({6, 1, 1, 0, 0xffffffff, 2}),  // Physical sector wraparound.
      EncodeList({6, 1, 1, 0xffffffff, 1, 2}),  // Logical sector wraparound.
      std::vector<u8>(DiscIO::D2xBlobReader::MAX_LIST_SIZE + 4),
  };
  for (const auto& list : invalid_lists)
  {
    std::string error;
    EXPECT_EQ(DiscIO::D2xBlobReader::Create(MakeCard(), list, &error), nullptr);
    EXPECT_FALSE(error.empty());
  }
}

TEST(D2xBlob, AcceptsPaddedListsAndTheExactEndOfTheCard)
{
  auto list = EncodeList({2, 1, 20000, 0, 6, 2});
  list.resize(64);
  auto reader = DiscIO::D2xBlobReader::Create(MakeCard(), list, nullptr);
  ASSERT_NE(reader, nullptr);
  std::array<u8, 512> bytes;
  EXPECT_TRUE(reader->Read(512, 512, bytes.data()));
}

TEST(D2xBlob, ReadOnlyDiscHandleAllowsSDIOWritesAndSeesThem)
{
  const std::string directory = File::CreateTempDir();
  ASSERT_FALSE(directory.empty());
  const std::string path = directory + "/card.raw";
  {
    File::IOFile initial(path, "wb");
    const std::array<u8, 512> zeroes{};
    ASSERT_TRUE(initial.WriteArray(zeroes));
  }
  {
    auto reader =
        DiscIO::D2xBlobReader::Create(File::IOFile(path, "rb", File::SharedAccess::ReadWrite),
                                      EncodeList({1, 1, 1, 0, 0, 1}), nullptr);
    ASSERT_NE(reader, nullptr);
    std::array<u8, 4> output{};
    ASSERT_TRUE(reader->Read(0, output.size(), output.data()));

    File::IOFile writer(path, "r+b", File::SharedAccess::ReadWrite);
    ASSERT_TRUE(writer.IsOpen());
    const std::array<u8, 4> replacement{1, 2, 3, 4};
    ASSERT_TRUE(writer.WriteArray(replacement));
    ASSERT_TRUE(writer.Flush());
    ASSERT_TRUE(reader->Read(0, output.size(), output.data()));
    EXPECT_EQ(output, replacement);
  }
  EXPECT_TRUE(File::DeleteDirRecursively(directory));
}
