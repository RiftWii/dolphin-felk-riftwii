// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Common/IOFile.h"
#include "DiscIO/Blob.h"

namespace DiscIO
{
// A d2x fragment list describes a sparse disc in 512-byte SD card sectors.
// The list is copied and validated before the DVD thread can use it.
class D2xBlobReader final : public BlobReader
{
public:
  static constexpr u32 MAX_FRAGMENTS = 20000;
  static constexpr u32 MAX_LIST_SIZE = 12 + MAX_FRAGMENTS * 12;

  static std::unique_ptr<D2xBlobReader> Create(File::IOFile card, std::span<const u8> list,
                                               std::string* error, u32 sector_size = 512);

  BlobType GetBlobType() const override { return BlobType::PLAIN; }
  std::unique_ptr<BlobReader> CopyReader() const override;
  u64 GetRawSize() const override { return m_card_size; }
  u64 GetDataSize() const override { return m_disc_size; }
  DataSizeType GetDataSizeType() const override { return DataSizeType::Accurate; }
  u64 GetBlockSize() const override { return 0; }
  bool HasFastRandomAccessInBlock() const override { return true; }
  std::string GetCompressionMethod() const override { return {}; }
  std::optional<int> GetCompressionLevel() const override { return std::nullopt; }
  bool Read(u64 offset, u64 size, u8* out_ptr) override;

private:
  struct Fragment
  {
    u64 offset;
    u64 card_offset;
    u64 size;
  };

  D2xBlobReader(File::IOFile card, u64 card_size, u64 disc_size, std::vector<Fragment> fragments);

  File::IOFile m_card;
  u64 m_card_size;
  u64 m_disc_size;
  std::vector<Fragment> m_fragments;
};
}  // namespace DiscIO
