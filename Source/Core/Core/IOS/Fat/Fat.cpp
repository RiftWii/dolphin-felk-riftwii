// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/Fat/Fat.h"

#include "Common/ChunkFile.h"
#include "Common/EnumUtils.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"

namespace IOS::HLE
{
FatDevice::FatDevice(EmulationKernel& ios, const std::string& device_name)
    : EmulationDevice(ios, device_name)
{
}

FatDevice::~FatDevice() = default;

void FatDevice::DoState(PointerWrap& p)
{
  Device::DoState(p);
  p.Do(m_card_started);
}

std::optional<IPCReply> FatDevice::Open(const OpenRequest& request)
{
  INFO_LOG_FMT(IOS, "fat: opening module (mode {})", Common::ToUnderlying(request.flags));
  m_is_active = true;
  return IPCReply(IPC_SUCCESS);
}

std::optional<IPCReply> FatDevice::IOCtl(const IOCtlRequest& request)
{
  INFO_LOG_FMT(IOS, "fat: IOCtl {:#x} (not supported)", request.request);
  return IPCReply(IPC_EINVAL);
}

std::optional<IPCReply> FatDevice::IOCtlV(const IOCtlVRequest& request)
{
  switch (request.request)
  {
  case IOCTLV_FAT_MOUNT_SD:
  {
    if (!Config::Get(Config::MAIN_WII_SD_CARD))
    {
      INFO_LOG_FMT(IOS, "fat: mount SD card failed: no SD card inserted");
      return IPCReply(IPC_EINVAL);
    }
    m_card_started = true;
    INFO_LOG_FMT(IOS, "fat: mounted SD card");
    return IPCReply(IPC_SUCCESS);
  }
  case IOCTLV_FAT_UNMOUNT_SD:
  {
    // d2x answers its driver shutdown's result: 1 (true) when it stopped the
    // driver, IPC_EINVAL when the driver was not running.
    if (!m_card_started)
    {
      INFO_LOG_FMT(IOS, "fat: unmount SD card failed: the card was not started");
      return IPCReply(IPC_EINVAL);
    }
    m_card_started = false;
    INFO_LOG_FMT(IOS, "fat: unmounted SD card");
    return IPCReply(1);
  }
  default:
    INFO_LOG_FMT(IOS, "fat: IOCtlV {:#x} (not supported)", request.request);
    return IPCReply(IPC_EINVAL);
  }
}
}  // namespace IOS::HLE
