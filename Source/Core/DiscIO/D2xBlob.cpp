// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DiscIO/D2xBlob.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include "Common/Swap.h"

namespace DiscIO
{
D2xBlobReader::D2xBlobReader(File::IOFile card, u64 card_size, u64 disc_size,
                             std::vector<Fragment> fragments)
    : m_card(std::move(card)), m_card_size(card_size), m_disc_size(disc_size),
      m_fragments(std::move(fragments))
{
  // SDIO and the emulated NAND may write through other handles between reads.
  std::setvbuf(m_card.GetHandle(), nullptr, _IONBF, 0);
}

std::unique_ptr<D2xBlobReader> D2xBlobReader::Create(File::IOFile card, std::span<const u8> list,
                                                     std::string* error)
{
  const auto refuse = [error](const char* reason) -> std::unique_ptr<D2xBlobReader> {
    if (error)
      *error = reason;
    return nullptr;
  };
  if (list.size() < 12 || list.size() > MAX_LIST_SIZE || list.size() % 4 != 0)
    return refuse("invalid fragment list byte size");

  const u64 disc_sectors = Common::swap32(list.data());
  const u32 count = Common::swap32(list.data() + 4);
  const u32 capacity = Common::swap32(list.data() + 8);
  if (count > MAX_FRAGMENTS || capacity > MAX_FRAGMENTS || count > capacity ||
      12 + static_cast<u64>(count) * 12 > list.size())
    return refuse("fragment count/capacity exceeds the supplied list or d2x limit");
  // d2x's fragment index covers at most 10 GiB (640 * 16 MiB).
  if (disc_sectors == 0 || disc_sectors > 640 * 32768)
    return refuse("virtual disc size is zero or exceeds d2x's 10 GiB limit");
  if (!card)
    return refuse("SD card image could not be opened");

  const u64 card_size = card.GetSize();
  std::vector<Fragment> fragments;
  fragments.reserve(count);
  u64 previous_end = 0;
  for (u32 index = 0; index < count; ++index)
  {
    const u8* entry = list.data() + 12 + index * 12;
    const u64 offset = Common::swap32(entry);
    const u64 sector = Common::swap32(entry + 4);
    const u64 sectors = Common::swap32(entry + 8);
    if (sectors == 0 || offset < previous_end || offset + sectors > disc_sectors)
      return refuse("empty, overlapping, unsorted or out-of-disc fragment");
    if (sector + sectors > card_size / 512)
      return refuse("fragment extends past the SD card image");

    fragments.push_back({offset * 512, sector * 512, sectors * 512});
    previous_end = offset + sectors;
  }

  return std::unique_ptr<D2xBlobReader>(
      new D2xBlobReader(std::move(card), card_size, disc_sectors * 512, std::move(fragments)));
}

std::unique_ptr<BlobReader> D2xBlobReader::CopyReader() const
{
  File::IOFile card = m_card.Duplicate("rb");
  if (!card)
    return nullptr;
  return std::unique_ptr<D2xBlobReader>(
      new D2xBlobReader(std::move(card), m_card_size, m_disc_size, m_fragments));
}

bool D2xBlobReader::Read(u64 offset, u64 size, u8* out_ptr)
{
  if (offset > m_disc_size || size > m_disc_size - offset)
    return false;

  auto fragment = std::ranges::upper_bound(
      m_fragments, offset, {}, [](const Fragment& entry) { return entry.offset + entry.size; });
  while (size != 0)
  {
    if (fragment == m_fragments.end() || offset < fragment->offset)
    {
      // Unmapped sectors are sparse holes, including the tail of the disc.
      const u64 hole_end = fragment == m_fragments.end() ? m_disc_size : fragment->offset;
      const u64 bytes = std::min(size, hole_end - offset);
      std::memset(out_ptr, 0, static_cast<size_t>(bytes));
      offset += bytes;
      out_ptr += bytes;
      size -= bytes;
      continue;
    }

    const u64 within_fragment = offset - fragment->offset;
    const u64 bytes = std::min(size, fragment->size - within_fragment);
    if (!m_card.Seek(fragment->card_offset + within_fragment, File::SeekOrigin::Begin) ||
        !m_card.ReadBytes(out_ptr, static_cast<size_t>(bytes)))
    {
      m_card.ClearError();
      return false;
    }
    offset += bytes;
    out_ptr += bytes;
    size -= bytes;
    if (within_fragment + bytes == fragment->size)
      ++fragment;
  }
  return true;
}
}  // namespace DiscIO
