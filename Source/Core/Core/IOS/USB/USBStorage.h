// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace IOS::HLE::USB
{
// Each operation opens a shared handle and flushes before returning. No image
// cache survives a request from USB, d2x EHCI, FatFs or the fragment reader.
class StorageImage
{
public:
  static constexpr u32 SECTOR_SIZE = 512;
  explicit StorageImage(std::string path);
  static std::string GetConfiguredPath();
  u32 GetSectorCount() const;
  bool Read(u32 sector, std::span<u8> data) const;
  bool Write(u32 sector, std::span<const u8> data) const;

private:
  std::string m_path;
};

// Bulk-only transport, independent of PPC memory so the wire protocol can be
// tested directly. Transfer returns the byte count or a negative stall result.
class StorageTransport
{
public:
  explicit StorageTransport(StorageImage image);
  void DoState(PointerWrap& p);
  void Reset();
  int Transfer(bool input, std::span<u8> data);

private:
  bool BeginCommand(std::span<const u8> cbw);
  void ExecuteCommand(std::span<const u8> command, u8 lun);
  void Fail(u8 sense_key, u8 additional_sense);

  enum class Phase { Command, DataIn, DataOut, Status, Stalled };
  StorageImage m_image;
  Phase m_phase = Phase::Command;
  std::vector<u8> m_data;
  size_t m_offset = 0;
  u32 m_tag = 0;
  u32 m_expected = 0;
  u32 m_residue = 0;
  u32 m_write_sector = 0;
  u8 m_status = 0;
  u8 m_sense_key = 0;
  u8 m_additional_sense = 0;
};
}  // namespace IOS::HLE::USB
