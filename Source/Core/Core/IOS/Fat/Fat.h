// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <optional>
#include <string>

#include "Common/CommonTypes.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/IOS.h"

class PointerWrap;

namespace IOS::HLE
{
class FatDevice final : public EmulationDevice
{
public:
  FatDevice(EmulationKernel& ios, const std::string& device_name);
  ~FatDevice() override;

  void DoState(PointerWrap& p) override;

  std::optional<IPCReply> Open(const OpenRequest& request) override;
  std::optional<IPCReply> IOCtl(const IOCtlRequest& request) override;
  std::optional<IPCReply> IOCtlV(const IOCtlVRequest& request) override;

private:
  enum : u32
  {
    IOCTLV_FAT_MOUNT_SD = 0xF0,
    IOCTLV_FAT_UNMOUNT_SD = 0xF1,
  };

  // d2x's SD driver runs: started by 0xF0, stopped by 0xF1.
  bool m_card_started = false;
};
}  // namespace IOS::HLE
