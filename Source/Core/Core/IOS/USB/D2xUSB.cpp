// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/D2xUSB.h"

#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Core/HW/Memmap.h"
#include "Core/IOS/USB/USBStorage.h"
#include "Core/System.h"

namespace IOS::HLE
{
D2xUSBDevice::D2xUSBDevice(EmulationKernel& ios, const std::string& device_name)
    : EmulationDevice(ios, device_name)
{
}

void D2xUSBDevice::DoState(PointerWrap& p)
{
  Device::DoState(p);
  p.Do(m_started);
  p.Do(m_port);
}

std::optional<IPCReply> D2xUSBDevice::Close(u32 fd)
{
  m_started = false;
  return Device::Close(fd);
}

std::optional<IPCReply> D2xUSBDevice::IOCtlV(const IOCtlVRequest& request)
{
  constexpr u32 base = 0x554d5300;  // 'UMS'
  auto& memory = GetSystem().GetMemory();
  const auto valid = [&](const IOCtlVRequest::IOVector& vector, u64 bytes) {
    return bytes <= vector.size && bytes <= 0x02000000 &&
           memory.GetPointerForRange(vector.address, static_cast<size_t>(bytes)) != nullptr;
  };
  if (request.request == base + 0x83)
  {
    if (!request.HasNumberOfValidVectors(1, 0) || !valid(request.in_vectors[0], 4))
      return IPCReply{IPC_EINVAL};
    const u32 port = memory.Read_U32(request.in_vectors[0].address);
    if (port > 1)
      return IPCReply{IPC_EINVAL};
    m_port = port;
    m_started = false;
    return IPCReply{0};
  }
  if (request.request == base + 0x10)
  {
    m_started = false;
    return IPCReply{0};
  }
  USB::StorageImage image(USB::StorageImage::GetConfiguredPath());
  const u32 sectors = m_port == 0 ? image.GetSectorCount() : 0;
  if (request.request == base + 1)
  {
    if (!request.HasNumberOfValidVectors(0, 0))
      return IPCReply{IPC_EINVAL};
    m_started = sectors != 0;
    INFO_LOG_FMT(IOS_USB, "d2x USB init: port {}, {} sectors", m_port, sectors);
    return IPCReply{m_started ? 0 : -100};
  }
  if (request.request == base + 2)
  {
    if (!request.HasNumberOfValidVectors(0, 1) || !valid(request.io_vectors[0], 4))
      return IPCReply{IPC_EINVAL};
    if (!m_started || sectors == 0)
      return IPCReply{0};
    memory.Write_U32(USB::StorageImage::SECTOR_SIZE, request.io_vectors[0].address);
    return IPCReply{static_cast<s32>(sectors)};
  }
  const bool write = request.request == base + 4;
  if (request.request != base + 3 && !write)
    return IPCReply{IPC_EINVAL};
  if (!request.HasNumberOfValidVectors(write ? 3 : 2, write ? 0 : 1) ||
      !valid(request.in_vectors[0], 4) || !valid(request.in_vectors[1], 4))
    return IPCReply{IPC_EINVAL};
  const u32 sector = memory.Read_U32(request.in_vectors[0].address);
  const u32 count = memory.Read_U32(request.in_vectors[1].address);
  const u64 bytes = u64{count} * USB::StorageImage::SECTOR_SIZE;
  const auto& buffer = write ? request.in_vectors[2] : request.io_vectors[0];
  if (!valid(buffer, bytes) || sector > sectors || count > sectors - sector)
    return IPCReply{IPC_EINVAL};
  if (!m_started || sectors == 0)
    return IPCReply{1};
  auto* data = memory.GetPointerForRange(buffer.address, static_cast<size_t>(bytes));
  const bool success = write ? image.Write(sector, {data, static_cast<size_t>(bytes)}) :
                               image.Read(sector, {data, static_cast<size_t>(bytes)});
  return IPCReply{success ? 0 : 1};
}
}  // namespace IOS::HLE
