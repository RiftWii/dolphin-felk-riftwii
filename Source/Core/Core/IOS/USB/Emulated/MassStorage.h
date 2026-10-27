// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/IOS/USB/Common.h"
#include "Core/IOS/USB/USBStorage.h"

namespace IOS::HLE::USB
{
class MassStorage final : public Device
{
public:
  static constexpr u64 DEVICE_ID = 0xd01f120000000001;

  explicit MassStorage(EmulationKernel& ios);
  void DoState(PointerWrap& p, bool check_image = true);
  void Reset();
  DeviceDescriptor GetDeviceDescriptor() const override;
  std::vector<ConfigDescriptor> GetConfigurations() const override;
  std::vector<InterfaceDescriptor> GetInterfaces(u8 config) const override;
  std::vector<EndpointDescriptor> GetEndpoints(u8 config, u8 interface, u8 alt) const override;
  bool Attach() override;
  bool AttachAndChangeInterface(u8 interface) override;
  int CancelTransfer(u8 endpoint) override;
  int ChangeInterface(u8 interface) override;
  int GetNumberOfAltSettings(u8 interface) override;
  int SetAltSetting(u8 alt_setting) override;
  int SubmitTransfer(std::unique_ptr<CtrlMessage> message) override;
  int SubmitTransfer(std::unique_ptr<BulkMessage> message) override;
  int SubmitTransfer(std::unique_ptr<IntrMessage> message) override;
  int SubmitTransfer(std::unique_ptr<IsoMessage> message) override;

private:
  EmulationKernel& m_ios;
  StorageTransport m_transport;
  u8 m_configuration = 0;
};
}  // namespace IOS::HLE::USB
