// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/IOS/Device.h"

namespace IOS::HLE
{
// d2x's raw SD sector interface, used by loaders after handing the card to IOS.
class D2xSDHCDevice final : public EmulationDevice
{
public:
  D2xSDHCDevice(EmulationKernel& ios, const std::string& device_name);
  void DoState(PointerWrap& p) override;
  std::optional<IPCReply> Open(const OpenRequest& request) override;
  std::optional<IPCReply> Close(u32 fd) override;
  std::optional<IPCReply> IOCtlV(const IOCtlVRequest& request) override;

private:
  bool m_started = false;
};
}  // namespace IOS::HLE
