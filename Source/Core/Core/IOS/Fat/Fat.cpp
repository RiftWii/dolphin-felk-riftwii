// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/Fat/Fat.h"

#include "Common/ChunkFile.h"
#include "Common/EnumUtils.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/Memmap.h"
#include "Core/IOS/FS/D2xFileSystem.h"
#include "Core/System.h"
#include "Core/IOS/USB/USBStorage.h"

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
  p.Do(m_usb_started);
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
  case IOCTLV_FAT_MOUNT_USB:
  {
    const bool usb = request.request == IOCTLV_FAT_MOUNT_USB;
    if (usb ? USB::StorageImage(USB::StorageImage::GetConfiguredPath()).GetSectorCount() == 0 :
              !Config::Get(Config::MAIN_WII_SD_CARD))
    {
      INFO_LOG_FMT(IOS, "fat: mount SD card failed: no SD card inserted");
      return IPCReply(IPC_EINVAL);
    }
    (usb ? m_usb_started : m_card_started) = true;
    s32 partition = -1;
    if (!request.in_vectors.empty() && request.in_vectors[0].size >= sizeof(u32))
      partition = GetSystem().GetMemory().Read_U32(request.in_vectors[0].address);
    const s32 result = GetEmulationKernel().GetD2xFS()->Mount(partition, usb);
    INFO_LOG_FMT(IOS, "fat: mount {}, partition {}: {}", usb ? "USB" : "SD", partition, result);
    return IPCReply(result);
  }
  case IOCTLV_FAT_UNMOUNT_SD:
  case IOCTLV_FAT_UNMOUNT_USB:
  {
    const bool usb = request.request == IOCTLV_FAT_UNMOUNT_USB;
    bool& started = usb ? m_usb_started : m_card_started;
    // d2x answers its driver shutdown's result: 1 (true) when it stopped the
    // driver, IPC_EINVAL when the driver was not running.
    // d2x's file system starts the driver itself when it mounts the volume (SetMode, or a
    // redirected reload carrying NAND emulation).
    if (!started && !GetEmulationKernel().GetD2xFS()->GetNandConfig().volume_mounted[usb ? 1 : 0])
    {
      INFO_LOG_FMT(IOS, "fat: unmount SD card failed: the card was not started");
      return IPCReply(IPC_EINVAL);
    }
    started = false;
    GetEmulationKernel().GetD2xFS()->Unmount(usb);
    INFO_LOG_FMT(IOS, "fat: unmounted {}", usb ? "USB" : "SD");
    return IPCReply(1);
  }
  default:
    INFO_LOG_FMT(IOS, "fat: IOCtlV {:#x} (not supported)", request.request);
    return IPCReply(IPC_EINVAL);
  }
}
}  // namespace IOS::HLE
