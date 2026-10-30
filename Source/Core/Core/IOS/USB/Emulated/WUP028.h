// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <queue>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/IOS/USB/Common.h"

namespace IOS::HLE::USB
{
// Nintendo's Wii U GameCube Controller Adapter (WUP-028), as Wii homebrew reaches it through
// /dev/usb/hid: one interrupt-in endpoint with a 37-byte report of all four ports, and one
// interrupt-out endpoint for the start command and rumble. Its ports are fed from the emulated
// GameCube controllers (port 1 from controller 1, and so on), so scripts and the DSU pad drive it.
constexpr u16 WUP028_VID = 0x057e;
constexpr u16 WUP028_PID = 0x0337;
constexpr u32 WUP028_REPORT_SIZE = 37;

// What a script or test can see of the adapter. Every copy of the device shares it.
struct WUP028Stats
{
  bool attached = false;  // the HID module opened it
  bool started = false;   // the start command (0x13) came
  std::array<u8, 4> rumble{};  // the last rumble byte for each port
  std::array<bool, 4> motors{};  // each controller's motor is running (rumble that reached it)
  u32 reports = 0;             // input reports sent
  u32 short_reports = 0;       // of those, cut short on purpose (EmulateWUP028ShortReportEvery)
  u32 rumble_messages = 0;
};
WUP028Stats GetWUP028Stats();

class WUP028USB final : public Device
{
public:
  WUP028USB(EmulationKernel& ios, const std::string& device_name);
  ~WUP028USB() override;
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
  void SendReport(std::unique_ptr<IntrMessage> message);
  // Starts or stops each controller's motor to match the last rumble command and the settings.
  void UpdateMotors();

  EmulationKernel& m_ios;
  u16 m_vid = WUP028_VID;
  u16 m_pid = WUP028_PID;
  u8 m_active_interface = 0;
  bool m_device_attached = false;
  bool m_started = false;
  std::array<u8, 4> m_rumble{};       // the last rumble command, per port
  std::array<bool, 4> m_motor_on{};   // what each controller was last told
  // Reads asked for before the start command: a real adapter sends nothing until then.
  std::queue<std::unique_ptr<IntrMessage>> m_waiting_reads;
};
}  // namespace IOS::HLE::USB
