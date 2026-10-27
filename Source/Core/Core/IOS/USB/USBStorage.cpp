// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/USBStorage.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

#include "Common/ChunkFile.h"
#include "Common/Hash.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Common/Swap.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"

namespace IOS::HLE::USB
{
namespace
{
u32 ReadLE32(const u8* bytes)
{
  return u32{bytes[0]} | (u32{bytes[1]} << 8) | (u32{bytes[2]} << 16) | (u32{bytes[3]} << 24);
}

void WriteLE32(u8* bytes, u32 value)
{
  for (size_t index = 0; index < 4; ++index)
    bytes[index] = static_cast<u8>(value >> (8 * index));
}

bool IsValidTransfer(File::IOFile& image, u32 sector, size_t bytes)
{
  const u64 size = image.GetSize();
  const u64 offset = u64{sector} * StorageImage::SECTOR_SIZE;
  return bytes % StorageImage::SECTOR_SIZE == 0 && offset <= size && bytes <= size - offset &&
         image.Seek(offset, File::SeekOrigin::Begin);
}
}  // namespace

StorageImage::StorageImage(std::string path) : m_path(std::move(path))
{
}

std::string StorageImage::GetConfiguredPath()
{
  return Config::Get(Config::MAIN_WII_USB_IMAGE_PATH);
}

u32 StorageImage::GetSectorCount() const
{
  File::IOFile image(m_path, "rb", File::SharedAccess::ReadWrite);
  if (!image)
    return 0;
  const u64 size = image.GetSize();
  if (size % SECTOR_SIZE != 0 || size / SECTOR_SIZE > std::numeric_limits<u32>::max())
    return 0;
  return static_cast<u32>(size / SECTOR_SIZE);
}

bool StorageImage::Read(u32 sector, std::span<u8> data) const
{
  File::IOFile image(m_path, "rb", File::SharedAccess::ReadWrite);
  return image && IsValidTransfer(image, sector, data.size()) &&
         image.ReadBytes(data.data(), data.size());
}

bool StorageImage::Write(u32 sector, std::span<const u8> data) const
{
  File::IOFile image(m_path, "r+b", File::SharedAccess::ReadWrite);
  return image && IsValidTransfer(image, sector, data.size()) &&
         image.WriteBytes(data.data(), data.size()) && image.Flush();
}

StorageImage::Identity StorageImage::GetIdentity() const
{
  Identity identity;
  identity.path = m_path;
  File::IOFile image(m_path, "rb", File::SharedAccess::ReadWrite);
  if (!image)
    return identity;
  identity.size = image.GetSize();
  std::array<u8, SECTOR_SIZE> sector{};
  if (identity.size < sector.size() || !image.ReadBytes(sector.data(), sector.size()))
    return identity;
  // Fingerprint sector 0 and the first sector of each MBR partition, so an
  // image with the same size but different contents is still noticed.
  identity.fingerprint = Common::ComputeCRC32(sector.data(), sector.size());
  for (size_t partition = 0; partition < 4; ++partition)
  {
    const size_t entry = 446 + partition * 16;
    if (sector[entry + 4] == 0)
      continue;
    const u32 lba = ReadLE32(sector.data() + entry + 8);
    const u64 offset = u64{lba} * SECTOR_SIZE;
    if (lba == 0 || offset > identity.size - sector.size())
      continue;
    std::array<u8, SECTOR_SIZE> boot{};
    if (!image.Seek(offset, File::SeekOrigin::Begin) ||
        !image.ReadBytes(boot.data(), boot.size()))
    {
      continue;
    }
    identity.fingerprint = Common::UpdateCRC32(identity.fingerprint, boot.data(), boot.size());
  }
  identity.readable = true;
  return identity;
}

StorageTransport::StorageTransport(StorageImage image) : m_image(std::move(image))
{
}

void StorageTransport::DoState(PointerWrap& p, bool check_image)
{
  // The image stays on disk; only the in-flight command belongs in the state.
  // The identity records which image the command was started against.
  StorageImage::Identity saved;
  if (!p.IsReadMode())
    saved = m_image.GetIdentity();
  p.Do(saved.readable);
  p.Do(saved.path);
  p.Do(saved.size);
  p.Do(saved.fingerprint);
  p.Do(m_phase);
  p.Do(m_data);
  p.Do(m_offset);
  p.Do(m_tag);
  p.Do(m_expected);
  p.Do(m_residue);
  p.Do(m_write_sector);
  p.Do(m_status);
  p.Do(m_sense_key);
  p.Do(m_additional_sense);
  if (!p.IsReadMode() || !check_image)
    return;
  const StorageImage::Identity current = m_image.GetIdentity();
  if (saved == current)
    return;
  if (m_phase == Phase::Command)
  {
    WARN_LOG_FMT(IOS_USB, "USB image changed (saved {}); idle drive reset", saved.path);
    Reset();
    return;
  }
  ERROR_LOG_FMT(IOS_USB, "Cannot restore USB storage: the USB image has changed ({})",
                saved.path);
  Core::DisplayMessage(
      "This savestate cannot be loaded because the USB image has changed "
      "while a USB command was in progress",
      4000);
  Reset();
  p.SetMeasureMode();
}

void StorageTransport::Reset()
{
  m_phase = Phase::Command;
  m_data.clear();
  m_offset = 0;
}

void StorageTransport::Fail(u8 sense_key, u8 additional_sense)
{
  m_status = 1;
  m_sense_key = sense_key;
  m_additional_sense = additional_sense;
}

bool StorageTransport::BeginCommand(std::span<const u8> cbw)
{
  if (cbw.size() != 31 || ReadLE32(cbw.data()) != 0x43425355 ||
      (cbw[12] & 0x7f) != 0 || cbw[13] > 15 || cbw[14] == 0 || cbw[14] > 16)
    return false;
  m_tag = ReadLE32(cbw.data() + 4);
  m_expected = ReadLE32(cbw.data() + 8);
  // READ/WRITE(10) cannot describe more than 65535 sectors. Bound host-supplied
  // lengths before allocating, including commands with an invalid CDB.
  if (m_expected > 65535 * StorageImage::SECTOR_SIZE)
    return false;
  m_residue = m_expected;
  m_offset = 0;
  m_status = 0;
  m_data.clear();
  ExecuteCommand(cbw.subspan(15, cbw[14]), cbw[13]);
  const bool input = (cbw[12] & 0x80) != 0;
  const bool write = cbw[15] == 0x2a;
  if ((!m_data.empty() && input == write) || m_data.size() > m_expected)
  {
    m_status = 2;
    m_data.clear();
  }
  if (m_expected == 0)
    m_phase = Phase::Status;
  else
    m_phase = input ? Phase::DataIn : Phase::DataOut;
  return true;
}

void StorageTransport::ExecuteCommand(std::span<const u8> command, u8 lun)
{
  const u8 opcode = command[0];
  const bool ten_byte = opcode >= 0x20;
  if (command.size() < (ten_byte ? 10u : 6u) || lun != 0)
  {
    Fail(5, lun != 0 ? 0x25 : 0x24);
    return;
  }
  const u32 sectors = m_image.GetSectorCount();
  if (sectors == 0 && opcode != 0x03 && opcode != 0x12)
  {
    Fail(2, 0x3a);
    return;
  }
  switch (opcode)
  {
  case 0x00:  // TEST UNIT READY
  case 0x1b:  // START STOP UNIT
  case 0x1e:  // PREVENT/ALLOW MEDIUM REMOVAL
  case 0x35:  // SYNCHRONIZE CACHE (writes are already flushed)
    break;
  case 0x03:  // REQUEST SENSE
    m_data.assign(18, 0);
    m_data[0] = 0x70;
    m_data[2] = m_sense_key;
    m_data[7] = 10;
    m_data[12] = m_additional_sense;
    m_sense_key = 0;
    m_additional_sense = 0;
    m_data.resize(std::min<size_t>(m_data.size(), command[4]));
    break;
  case 0x12:  // INQUIRY
    if (command[1] & 1)
    {
      Fail(5, 0x24);
      break;
    }
    m_data.assign(36, 0);
    m_data[1] = 0x80;
    m_data[2] = 4;
    m_data[3] = 2;
    m_data[4] = 31;
    std::fill(m_data.begin() + 8, m_data.end(), ' ');
    std::memcpy(m_data.data() + 8, "Dolphin", 7);
    std::memcpy(m_data.data() + 16, "USB disk", 8);
    std::memcpy(m_data.data() + 32, "1.00", 4);
    m_data.resize(std::min<size_t>(m_data.size(), command[4]));
    break;
  case 0x1a:  // MODE SENSE(6), writable direct-access device
    m_data = {3, 0, 0, 0};
    m_data.resize(std::min<size_t>(m_data.size(), command[4]));
    break;
  case 0x5a:  // MODE SENSE(10)
    m_data = {0, 6, 0, 0, 0, 0, 0, 0};
    m_data.resize(std::min<size_t>(m_data.size(), Common::swap16(command.data() + 7)));
    break;
  case 0x25:  // READ CAPACITY(10): last LBA, followed by block length
    m_data.resize(8);
    for (size_t index = 0; index < 4; ++index)
    {
      m_data[index] = static_cast<u8>((sectors - 1) >> (24 - 8 * index));
      m_data[index + 4] = static_cast<u8>(StorageImage::SECTOR_SIZE >> (24 - 8 * index));
    }
    break;
  case 0x28:  // READ(10)
  case 0x2a:  // WRITE(10)
  {
    const u32 sector = Common::swap32(command.data() + 2);
    const u32 count = Common::swap16(command.data() + 7);
    if (sector > sectors || count > sectors - sector)
    {
      Fail(5, 0x21);
      break;
    }
    const size_t bytes = size_t{count} * StorageImage::SECTOR_SIZE;
    if (bytes != m_expected)
    {
      m_status = 2;
      break;
    }
    m_data.resize(bytes);
    m_write_sector = sector;
    if (opcode == 0x28 && !m_image.Read(sector, m_data))
    {
      m_data.clear();
      Fail(3, 0x11);
    }
    break;
  }
  default:
    Fail(5, 0x20);
    break;
  }
}

int StorageTransport::Transfer(bool input, std::span<u8> data)
{
  if (m_phase == Phase::Command && !input)
  {
    if (BeginCommand(data))
      return static_cast<int>(data.size());
    m_phase = Phase::Stalled;
  }
  else if ((m_phase == Phase::DataIn && input) || (m_phase == Phase::DataOut && !input))
  {
    const size_t bytes = std::min<size_t>(data.size(), m_expected - m_offset);
    const size_t available = m_offset < m_data.size() ?
                                 std::min(bytes, m_data.size() - m_offset) : 0;
    if (available != 0)
    {
      if (input)
        std::copy_n(m_data.data() + m_offset, available, data.data());
      else
        std::copy_n(data.data(), available, m_data.data() + m_offset);
      m_residue -= static_cast<u32>(available);
    }
    m_offset += bytes;
    if (m_offset == m_expected || (input && available < bytes))
    {
      if (!input && m_status == 0 && !m_image.Write(m_write_sector, m_data))
        Fail(3, 0x0c);
      m_phase = Phase::Status;
    }
    return static_cast<int>(input ? available : bytes);
  }
  else if (m_phase == Phase::Status && input && data.size() >= 13)
  {
    WriteLE32(data.data(), 0x53425355);
    WriteLE32(data.data() + 4, m_tag);
    WriteLE32(data.data() + 8, m_residue);
    data[12] = m_status;
    Reset();
    return 13;
  }
  return -4;
}
}  // namespace IOS::HLE::USB
