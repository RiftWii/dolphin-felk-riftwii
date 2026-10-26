// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Swap.h"
#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/USBStorage.h"

using IOS::HLE::USB::StorageImage;
using IOS::HLE::USB::StorageTransport;

namespace
{
void PutLE32(u8* data, u32 value)
{
  for (size_t index = 0; index < 4; ++index)
    data[index] = static_cast<u8>(value >> (8 * index));
}

std::array<u8, 31> MakeCommand(u8 opcode, u32 bytes, bool input, u32 sector = 0, u16 count = 0)
{
  std::array<u8, 31> cbw{};
  PutLE32(cbw.data(), 0x43425355);
  PutLE32(cbw.data() + 4, 0x12345678);
  PutLE32(cbw.data() + 8, bytes);
  cbw[12] = input ? 0x80 : 0;
  cbw[14] = opcode >= 0x20 ? 10 : 6;
  cbw[15] = opcode;
  if (opcode == 0x28 || opcode == 0x2a)
  {
    for (size_t index = 0; index < 4; ++index)
      cbw[17 + index] = static_cast<u8>(sector >> (24 - index * 8));
    cbw[22] = static_cast<u8>(count >> 8);
    cbw[23] = static_cast<u8>(count);
  }
  else
  {
    cbw[19] = static_cast<u8>(bytes);
  }
  return cbw;
}

class USBStorageTest : public testing::Test
{
protected:
  void SetUp() override
  {
    m_directory = File::CreateTempDir();
    ASSERT_FALSE(m_directory.empty());
    m_path = m_directory + "/usb.raw";
    File::IOFile image(m_path, "wb");
    std::vector<u8> contents(64 * 512);
    for (size_t index = 0; index < contents.size(); ++index)
      contents[index] = static_cast<u8>(index * 3 + index / 512);
    ASSERT_TRUE(image.WriteBytes(contents.data(), contents.size()));
    ASSERT_TRUE(image.Close());
    m_transport = std::make_unique<StorageTransport>(StorageImage(m_path));
  }

  void TearDown() override
  {
    m_transport.reset();
    if (!m_directory.empty())
      File::DeleteDirRecursively(m_directory);
  }

  void Begin(std::array<u8, 31> cbw)
  {
    ASSERT_EQ(m_transport->Transfer(false, cbw), 31);
  }

  void CheckStatus(u8 status = 0, u32 residue = 0)
  {
    std::array<u8, 13> csw{};
    ASSERT_EQ(m_transport->Transfer(true, csw), 13);
    EXPECT_EQ(std::vector<u8>(csw.begin(), csw.begin() + 8),
              (std::vector<u8>{0x55, 0x53, 0x42, 0x53, 0x78, 0x56, 0x34, 0x12}));
    std::array<u8, 4> encoded{};
    PutLE32(encoded.data(), residue);
    EXPECT_TRUE(std::equal(encoded.begin(), encoded.end(), csw.begin() + 8));
    EXPECT_EQ(csw[12], status);
  }

  std::string m_directory;
  std::string m_path;
  std::unique_ptr<StorageTransport> m_transport;
};

TEST_F(USBStorageTest, InquiryCapacityAndNoDataCommands)
{
  Begin(MakeCommand(0x12, 36, true));
  std::array<u8, 36> inquiry{};
  ASSERT_EQ(m_transport->Transfer(true, inquiry), 36);
  EXPECT_EQ(inquiry[0], 0);
  EXPECT_EQ(inquiry[4], 31);
  CheckStatus();
  Begin(MakeCommand(0x25, 8, true));
  std::array<u8, 8> capacity{};
  ASSERT_EQ(m_transport->Transfer(true, capacity), 8);
  EXPECT_EQ(Common::swap32(capacity.data()), 63u);
  EXPECT_EQ(Common::swap32(capacity.data() + 4), 512u);
  CheckStatus();
  for (u8 opcode : {0x00, 0x1b, 0x1e, 0x35})
  {
    Begin(MakeCommand(opcode, 0, false));
    CheckStatus();
  }
}

TEST_F(USBStorageTest, SplitReadsAndWritesAreVisibleThroughIndependentHandles)
{
  Begin(MakeCommand(0x2a, 20480, false, 10, 40));
  std::vector<u8> first(16384, 0x71);
  std::vector<u8> second(4096, 0x32);
  ASSERT_EQ(m_transport->Transfer(false, first), 16384);
  ASSERT_EQ(m_transport->Transfer(false, second), 4096);
  CheckStatus();
  StorageImage independent(m_path);
  std::vector<u8> actual(20480);
  ASSERT_TRUE(independent.Read(10, actual));
  EXPECT_TRUE(std::equal(first.begin(), first.end(), actual.begin()));
  EXPECT_TRUE(std::equal(second.begin(), second.end(), actual.begin() + first.size()));
  Begin(MakeCommand(0x28, 20480, true, 10, 40));
  std::vector<u8> read_first(16384);
  std::vector<u8> read_second(4096);
  ASSERT_EQ(m_transport->Transfer(true, read_first), 16384);
  ASSERT_EQ(m_transport->Transfer(true, read_second), 4096);
  EXPECT_EQ(read_first, first);
  EXPECT_EQ(read_second, second);
  CheckStatus();
}

TEST_F(USBStorageTest, OutOfRangeCommandReportsSenseAndResidue)
{
  Begin(MakeCommand(0x28, 512, true, 64, 1));
  std::array<u8, 512> buffer{};
  EXPECT_EQ(m_transport->Transfer(true, buffer), 0);
  CheckStatus(1, 512);
  Begin(MakeCommand(0x03, 18, true));
  std::array<u8, 18> sense{};
  ASSERT_EQ(m_transport->Transfer(true, sense), 18);
  EXPECT_EQ(sense[2], 5);
  EXPECT_EQ(sense[12], 0x21);
  CheckStatus();
}

TEST_F(USBStorageTest, MalformedWrappersStallUntilReset)
{
  for (size_t field : {size_t{0}, size_t{12}, size_t{14}})
  {
    auto cbw = MakeCommand(0, 0, false);
    cbw[field] = 0xff;
    EXPECT_LT(m_transport->Transfer(false, cbw), 0);
    auto valid = MakeCommand(0, 0, false);
    EXPECT_LT(m_transport->Transfer(false, valid), 0);
    m_transport->Reset();
    Begin(valid);
    CheckStatus();
  }
  auto oversized = MakeCommand(0x28, std::numeric_limits<u32>::max(), true);
  EXPECT_LT(m_transport->Transfer(false, oversized), 0);
}

TEST_F(USBStorageTest, WrongDirectionLengthAndUnsupportedCommands)
{
  Begin(MakeCommand(0x28, 512, false, 0, 1));
  std::array<u8, 512> data{};
  EXPECT_EQ(m_transport->Transfer(false, data), 512);
  CheckStatus(2, 512);
  Begin(MakeCommand(0x28, 0, true, 0, 1));
  CheckStatus(2);
  Begin(MakeCommand(0x7f, 0, false));
  CheckStatus(1);
  auto lun = MakeCommand(0, 0, false);
  lun[13] = 1;
  Begin(lun);
  CheckStatus(1);
}

TEST_F(USBStorageTest, ImageBoundsAndConfiguration)
{
  Config::Init();
  Config::SetCurrent(Config::MAIN_WII_USB_IMAGE_PATH, m_path);
  EXPECT_EQ(StorageImage::GetConfiguredPath(), m_path);
  Config::ClearCurrentRunLayer();
  StorageImage image(m_path);
  EXPECT_EQ(image.GetSectorCount(), 64u);
  std::array<u8, 512> data{};
  EXPECT_TRUE(image.Read(63, data));
  EXPECT_FALSE(image.Read(64, data));
  EXPECT_FALSE(image.Write(std::numeric_limits<u32>::max(), data));
  EXPECT_FALSE(image.Write(0, std::span<const u8>(data).first(511)));
  EXPECT_EQ(StorageImage("").GetSectorCount(), 0u);
  EXPECT_FALSE(StorageImage(m_directory + "/absent.raw").Write(0, data));
  EXPECT_FALSE(File::Exists(m_directory + "/absent.raw"));
}
}  // namespace
