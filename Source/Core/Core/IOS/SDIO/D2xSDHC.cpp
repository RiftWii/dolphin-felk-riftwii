// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/SDIO/D2xSDHC.h"

#include "Common/ChunkFile.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/Memmap.h"
#include "Core/IOS/ES/ES.h"
#include "Core/IOS/IOS.h"
#include "Core/IOS/SDIO/SDIOSlot0.h"
#include "Core/System.h"

namespace IOS::HLE
{
D2xSDHCDevice::D2xSDHCDevice(EmulationKernel& ios, const std::string& device_name)
    : EmulationDevice(ios, device_name)
{
}

void D2xSDHCDevice::DoState(PointerWrap& p)
{
  Device::DoState(p);
  p.Do(m_started);
}

std::optional<IPCReply> D2xSDHCDevice::Open(const OpenRequest& request)
{
  if (GetEmulationKernel().GetESCore().IsTitleRunning())
    return IPCReply{IPC_ENOENT};
  return Device::Open(request);
}

std::optional<IPCReply> D2xSDHCDevice::Close(u32 fd)
{
  const bool was_started = m_started;
  m_started = false;
  Device::Close(fd);
  return IPCReply{was_started ? 0 : 1};
}

std::optional<IPCReply> D2xSDHCDevice::IOCtlV(const IOCtlVRequest& request)
{
  const auto refuse = [&](const char* reason) {
    WARN_LOG_FMT(IOS_SD, "d2x SDHC command {:#x} refused: {}", request.request, reason);
    return IPCReply{IPC_EINVAL};
  };
  const bool inserted = Config::Get(Config::MAIN_WII_SD_CARD);
  if (request.request == 4)
    return IPCReply{inserted ? 0 : 1};
  if (request.request != 1 && request.request != 2 && request.request != 3)
    return refuse("unsupported command");
  if (!inserted)
    return IPCReply{1};

  const auto sdio = std::static_pointer_cast<SDIOSlot0Device>(
      GetEmulationKernel().GetDeviceByName("/dev/sdio/slot0"));
  if (sdio && !sdio->FlushCardImage())
    return IPCReply{1};
  const bool write = request.request == 3;
  File::IOFile card(File::GetUserPath(F_WIISDCARDIMAGE_IDX), write ? "r+b" : "rb",
                    File::SharedAccess::ReadWrite);
  if (request.request == 1)
  {
    m_started = card.IsOpen();
    return IPCReply{m_started ? 0 : 1};
  }
  if (!m_started || !card)
    return IPCReply{1};
  if (!request.HasNumberOfValidVectors(write ? 3 : 2, write ? 0 : 1))
    return refuse("invalid vector count");

  auto& memory = GetSystem().GetMemory();
  const auto valid_range = [&](const IOCtlVRequest::IOVector& vector, u64 size) {
    if (size > vector.size)
      return false;
    const u32 address = vector.address & 0x3fffffff;
    if (address < memory.GetRamSizeReal())
      return size <= memory.GetRamSizeReal() - address;
    if (address >= 0x10000000 && address - 0x10000000 < memory.GetExRamSizeReal())
      return size <= memory.GetExRamSizeReal() - (address - 0x10000000);
    return false;
  };
  if (!valid_range(request.in_vectors[0], 4) || !valid_range(request.in_vectors[1], 4))
    return refuse("invalid sector/count buffers");

  const u64 sector = memory.Read_U32(request.in_vectors[0].address);
  const u64 count = memory.Read_U32(request.in_vectors[1].address);
  const u64 bytes = count * 512;
  const auto& buffer = write ? request.in_vectors[2] : request.io_vectors[0];
  if (!valid_range(buffer, bytes) || sector + count > card.GetSize() / 512)
    return refuse("transfer exceeds its PPC buffer or SD image");
  if (write && !Config::Get(Config::MAIN_ALLOW_SD_WRITES))
    return IPCReply{1};

  void* data = memory.GetPointerForRange(buffer.address, static_cast<size_t>(bytes));
  if (!card.Seek(sector * 512, File::SeekOrigin::Begin))
    return IPCReply{1};
  const bool success = write ? card.WriteBytes(data, static_cast<size_t>(bytes)) && card.Flush() :
                               card.ReadBytes(data, static_cast<size_t>(bytes));
  return IPCReply{success ? 0 : 1};
}
}  // namespace IOS::HLE
