// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/IOS/Device.h"

namespace IOS::HLE
{
class D2xUSBDevice final : public EmulationDevice
{
public:
  D2xUSBDevice(EmulationKernel& ios, const std::string& device_name);
  void DoState(PointerWrap& p) override;
  std::optional<IPCReply> Close(u32 fd) override;
  std::optional<IPCReply> IOCtlV(const IOCtlVRequest& request) override;

private:
  bool m_started = false;
  u32 m_port = 0;
};
}  // namespace IOS::HLE
