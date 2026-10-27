// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/MassStorage.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "Common/ChunkFile.h"
#include "Common/Logging/Log.h"
#include "Core/HW/Memmap.h"
#include "Core/IOS/IOS.h"
#include "Core/System.h"

namespace IOS::HLE::USB
{
MassStorage::MassStorage(EmulationKernel& ios)
    : m_ios(ios), m_transport(StorageImage(StorageImage::GetConfiguredPath()))
{
  m_id = DEVICE_ID;
}

void MassStorage::DoState(PointerWrap& p, bool check_image)
{
  m_transport.DoState(p, check_image);
  p.Do(m_configuration);
}

void MassStorage::Reset()
{
  m_transport.Reset();
  m_configuration = 0;
}

DeviceDescriptor MassStorage::GetDeviceDescriptor() const
{
  return {18, 1, 0x0200, 0, 0, 0, 64, 0x1209, 0xd01f, 0x0100, 0, 0, 0, 1};
}

std::vector<ConfigDescriptor> MassStorage::GetConfigurations() const
{
  return {{9, 2, 32, 1, 1, 0, 0x80, 50}};
}

std::vector<InterfaceDescriptor> MassStorage::GetInterfaces(u8 config) const
{
  if (config != 0)
    return {};
  return {{9, 4, 0, 0, 2, 8, 6, 0x50, 0}};
}

std::vector<EndpointDescriptor> MassStorage::GetEndpoints(u8 config, u8 interface, u8 alt) const
{
  if (config != 0 || interface != 0 || alt != 0)
    return {};
  return {{7, 5, 0x81, 2, 512, 0}, {7, 5, 0x02, 2, 512, 0}};
}

bool MassStorage::Attach()
{
  return true;
}

bool MassStorage::AttachAndChangeInterface(u8 interface)
{
  return interface == 0;
}

int MassStorage::CancelTransfer(u8 endpoint)
{
  return IPC_SUCCESS;
}

int MassStorage::ChangeInterface(u8 interface)
{
  return interface == 0 ? IPC_SUCCESS : IPC_EINVAL;
}

int MassStorage::GetNumberOfAltSettings(u8 interface)
{
  return interface == 0 ? 1 : 0;
}

int MassStorage::SetAltSetting(u8 alt_setting)
{
  return alt_setting == 0 ? IPC_SUCCESS : IPC_EINVAL;
}

int MassStorage::SubmitTransfer(std::unique_ptr<CtrlMessage> message)
{
  std::vector<u8> response;
  if (message->request_type == 0xa1 && message->request == 0xfe && message->value == 0 &&
      message->index == 0 && message->length == 1)
  {
    response = {0};  // GET MAX LUN: exactly one disk.
  }
  else if (message->request_type == 0x80 && message->request == 8 && message->value == 0 &&
           message->index == 0 && message->length == 1)
  {
    response = {m_configuration};  // GET_CONFIGURATION: the current configuration.
  }
  else if (message->request_type == 0x21 && message->request == 0xff && message->value == 0 &&
           message->index == 0 && message->length == 0)
  {
    m_transport.Reset();
  }
  else if (message->request_type == 0x02 && message->request == 1 && message->value == 0)
  {
    // CLEAR_FEATURE(ENDPOINT_HALT); BOT reset is a separate class request.
  }
  else if (message->request_type == 0 && message->request == REQUEST_SET_CONFIGURATION &&
            message->value == 1)
  {
    m_transport.Reset();
    m_configuration = 1;
  }
  else if (message->request_type == 0x80 && message->request == REQUEST_GET_DESCRIPTOR)
  {
    // USBv5 normally gets these through GETDEVPARAMS, but USBv0 requests the
    // packed little-endian wire descriptors through control transfers.
    if ((message->value >> 8) == 1)
    {
      response = {18, 1, 0, 2, 0, 0, 0, 64, 9, 0x12, 0x1f, 0xd0, 0, 1, 0, 0, 0, 1};
    }
    else if ((message->value >> 8) == 2)
    {
      response = {9, 2, 32, 0, 1, 1, 0, 0x80, 50,
                  9, 4, 0, 0, 2, 8, 6, 0x50, 0,
                  7, 5, 0x81, 2, 0, 2, 0,
                  7, 5, 2, 2, 0, 2, 0};
    }
    else
      return IPC_EINVAL;
  }
  else
  {
    WARN_LOG_FMT(IOS_USB,
                 "USB drive: unhandled control request type {:#04x} request {:#04x} value {:#06x} "
                 "index {:#06x} length {}",
                 message->request_type, message->request, message->value, message->index,
                 message->length);
    return IPC_EINVAL;
  }
  const size_t size = std::min<size_t>(response.size(), message->length);
  auto& memory = m_ios.GetSystem().GetMemory();
  if (size != 0)
  {
    auto* output = memory.GetPointerForRange(message->data_address, size);
    if (!output)
      return IPC_EINVAL;
    std::copy_n(response.data(), size, output);
  }
  message->ScheduleTransferCompletion(static_cast<s32>(size), 100);
  return IPC_SUCCESS;
}

int MassStorage::SubmitTransfer(std::unique_ptr<BulkMessage> message)
{
  if (message->endpoint != 0x81 && message->endpoint != 0x02)
    return IPC_EINVAL;
  if (message->length == 0 || message->length > 65535 * StorageImage::SECTOR_SIZE)
    return IPC_EINVAL;
  auto* data = m_ios.GetSystem().GetMemory().GetPointerForRange(message->data_address,
                                                            message->length);
  if (!data)
    return IPC_EINVAL;
  const int result = m_transport.Transfer(message->endpoint == 0x81, {data, message->length});
  if (result < 0)
    return result;
  message->ScheduleTransferCompletion(result, 100);
  return IPC_SUCCESS;
}

int MassStorage::SubmitTransfer(std::unique_ptr<IntrMessage> message)
{
  return IPC_EINVAL;
}

int MassStorage::SubmitTransfer(std::unique_ptr<IsoMessage> message)
{
  return IPC_EINVAL;
}
}  // namespace IOS::HLE::USB
