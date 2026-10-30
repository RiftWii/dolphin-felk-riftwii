// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/USB/Emulated/WUP028.h"

#include <algorithm>
#include <mutex>

#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/Memmap.h"
#include "Core/System.h"
#include "InputCommon/GCPadStatus.h"

namespace IOS::HLE::USB
{
// The adapter's endpoints, report ID and commands.
constexpr u8 ENDPOINT_IN = 0x81;
constexpr u8 ENDPOINT_OUT = 0x02;
constexpr u8 REPORT_ID = 0x21;
constexpr u8 CMD_START = 0x13;
constexpr u8 CMD_RUMBLE = 0x11;
constexpr u32 RUMBLE_SIZE = 5;
// Each port's part of the report: status, two button bytes, the sticks, the triggers.
constexpr u32 PORT_BYTES = 9;
// The status byte: the controller's kind in the high nibble, and the grey plug (rumble power).
constexpr u8 STATUS_WIRED = 0x10;
constexpr u8 STATUS_WIRELESS = 0x20;
constexpr u8 STATUS_RUMBLE_POWER = 0x04;
// bInterval 8 at full speed: a report every 8 ms, as the adapter sends them.
constexpr u32 REPORT_INTERVAL_US = 8000;
// A report cut short on purpose (for testing) holds the report ID and port 1 only.
constexpr u32 SHORT_REPORT_SIZE = 1 + PORT_BYTES;

static std::mutex s_stats_mutex;
static WUP028Stats s_stats;
static u32 s_reports_since_short = 0;

WUP028Stats GetWUP028Stats()
{
  std::lock_guard lk(s_stats_mutex);
  return s_stats;
}

WUP028USB::WUP028USB(EmulationKernel& ios, const std::string& device_name) : m_ios(ios)
{
  m_id = (u64(m_vid) << 32 | u64(m_pid) << 16 | u64(9) << 8 | u64(3));
}

WUP028USB::~WUP028USB()
{
  // Every scan makes a new copy and throws it away if one is plugged in already: only the copy
  // the HID module opened is the adapter, and only its removal ends the run.
  if (!m_device_attached)
    return;
  // Unplugged: whatever it was rumbling stops.
  m_rumble = {};
  UpdateMotors();
  std::lock_guard lk(s_stats_mutex);
  s_stats.attached = false;
  s_stats.started = false;
  s_stats.rumble = {};
}

void WUP028USB::UpdateMotors()
{
  const int ports = Config::Get(Config::MAIN_EMULATE_WUP028_PORTS);
  const int wireless = Config::Get(Config::MAIN_EMULATE_WUP028_WIRELESS);
  const bool rumble_power = Config::Get(Config::MAIN_EMULATE_WUP028_RUMBLE_POWER);
  for (int i = 0; i < 4; i++)
  {
    // As on a real adapter: only a wired controller rumbles (a WaveBird has no motor), and only
    // with the grey plug in. The command is 1 to run, 0 to stop, 2 to stop hard.
    const bool wired_here = (ports & (1 << i)) && !(wireless & (1 << i));
    const bool on = m_rumble[i] == 1 && wired_here && rumble_power;
    if (on == m_motor_on[i])
      continue;
    Pad::Rumble(i, on ? 1.0 : 0.0);
    m_motor_on[i] = on;
  }
  std::lock_guard lk(s_stats_mutex);
  s_stats.motors = m_motor_on;
}

DeviceDescriptor WUP028USB::GetDeviceDescriptor() const
{
  return DeviceDescriptor{0x12, 0x1, 0x200, 0x0, 0x0, 0x0, 0x40, WUP028_VID, WUP028_PID,
                          0x100, 0x1, 0x2, 0x3, 0x1};
}

std::vector<ConfigDescriptor> WUP028USB::GetConfigurations() const
{
  return {ConfigDescriptor{0x9, 0x2, 0x29, 0x1, 0x1, 0x0, 0xE0, 0xFA}};
}

std::vector<InterfaceDescriptor> WUP028USB::GetInterfaces(u8 config) const
{
  return {InterfaceDescriptor{0x9, 0x4, 0x0, 0x0, 0x2, 0x3, 0x0, 0x0, 0x0}};
}

std::vector<EndpointDescriptor> WUP028USB::GetEndpoints(u8 config, u8 interface, u8 alt) const
{
  return {EndpointDescriptor{0x7, 0x5, ENDPOINT_IN, 0x3, WUP028_REPORT_SIZE, 0x8},
          EndpointDescriptor{0x7, 0x5, ENDPOINT_OUT, 0x3, RUMBLE_SIZE, 0x8}};
}

bool WUP028USB::Attach()
{
  if (m_device_attached)
    return true;
  DEBUG_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Opening device", m_vid, m_pid);
  m_device_attached = true;
  std::lock_guard lk(s_stats_mutex);
  s_stats = {};
  s_stats.attached = true;
  s_reports_since_short = 0;
  return true;
}

bool WUP028USB::AttachAndChangeInterface(const u8 interface)
{
  if (!Attach())
    return false;
  if (interface != m_active_interface)
    return ChangeInterface(interface) == 0;
  return true;
}

int WUP028USB::CancelTransfer(const u8 endpoint)
{
  INFO_LOG_FMT(IOS_USB, "[{:04x}:{:04x} {}] Cancelling transfers (endpoint {:#x})", m_vid, m_pid,
               m_active_interface, endpoint);
  // Reads still waiting for the start command end as a real device's cancelled transfers do.
  if (endpoint == ENDPOINT_IN)
  {
    while (!m_waiting_reads.empty())
    {
      m_waiting_reads.front()->ScheduleTransferCompletion(-5, 0);
      m_waiting_reads.pop();
    }
  }
  return IPC_SUCCESS;
}

int WUP028USB::ChangeInterface(const u8 interface)
{
  m_active_interface = interface;
  return 0;
}

int WUP028USB::GetNumberOfAltSettings(u8 interface)
{
  return 0;
}

int WUP028USB::SetAltSetting(u8 alt_setting)
{
  return 0;
}

int WUP028USB::SubmitTransfer(std::unique_ptr<CtrlMessage> cmd)
{
  DEBUG_LOG_FMT(IOS_USB,
                "[{:04x}:{:04x} {}] Control: bRequestType={:02x} bRequest={:02x} wValue={:04x}"
                " wIndex={:04x} wLength={:04x}",
                m_vid, m_pid, m_active_interface, cmd->request_type, cmd->request, cmd->value,
                cmd->index, cmd->length);
  cmd->ScheduleTransferCompletion(0, 100);
  return IPC_SUCCESS;
}

int WUP028USB::SubmitTransfer(std::unique_ptr<BulkMessage> cmd)
{
  ERROR_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] The adapter has no bulk endpoints", m_vid, m_pid);
  return IPC_EINVAL;
}

int WUP028USB::SubmitTransfer(std::unique_ptr<IntrMessage> cmd)
{
  if (cmd->endpoint == ENDPOINT_IN)
  {
    if (!m_started)
      m_waiting_reads.push(std::move(cmd));
    else
      SendReport(std::move(cmd));
    return IPC_SUCCESS;
  }
  if (cmd->endpoint != ENDPOINT_OUT)
  {
    ERROR_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Interrupt transfer on endpoint {:#04x}", m_vid,
                  m_pid, cmd->endpoint);
    return IPC_EINVAL;
  }

  auto& memory = m_ios.GetSystem().GetMemory();
  const u8* buf = memory.GetPointerForRange(cmd->data_address, cmd->length);
  if (buf == nullptr || cmd->length == 0)
  {
    ERROR_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Bad output buffer", m_vid, m_pid);
    return IPC_EINVAL;
  }

  if (buf[0] == CMD_START)
  {
    INFO_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Start command: reports begin", m_vid, m_pid);
    m_started = true;
    {
      std::lock_guard lk(s_stats_mutex);
      s_stats.started = true;
    }
    while (!m_waiting_reads.empty())
    {
      SendReport(std::move(m_waiting_reads.front()));
      m_waiting_reads.pop();
    }
  }
  else if (buf[0] == CMD_RUMBLE && cmd->length >= RUMBLE_SIZE)
  {
    std::copy_n(buf + 1, m_rumble.size(), m_rumble.begin());
    INFO_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Rumble {} {} {} {}", m_vid, m_pid, m_rumble[0],
                 m_rumble[1], m_rumble[2], m_rumble[3]);
    {
      std::lock_guard lk(s_stats_mutex);
      s_stats.rumble = m_rumble;
      s_stats.rumble_messages++;
    }
    UpdateMotors();
  }
  else
  {
    WARN_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] Unknown command {:#04x} ({} bytes)", m_vid, m_pid,
                 buf[0], cmd->length);
  }

  const u32 length = cmd->length;
  cmd->ScheduleTransferCompletion(length, 1000);
  return IPC_SUCCESS;
}

int WUP028USB::SubmitTransfer(std::unique_ptr<IsoMessage> cmd)
{
  ERROR_LOG_FMT(IOS_USB, "[{:04x}:{:04x}] The adapter has no isochronous endpoints", m_vid,
                m_pid);
  return IPC_EINVAL;
}

void WUP028USB::SendReport(std::unique_ptr<IntrMessage> cmd)
{
  // A setting changed since the last report (a port emptied, the grey plug out) stops its motor.
  UpdateMotors();

  const int ports = Config::Get(Config::MAIN_EMULATE_WUP028_PORTS);
  const int wireless = Config::Get(Config::MAIN_EMULATE_WUP028_WIRELESS);
  const bool rumble_power = Config::Get(Config::MAIN_EMULATE_WUP028_RUMBLE_POWER);
  const int short_every = Config::Get(Config::MAIN_EMULATE_WUP028_SHORT_REPORT_EVERY);

  std::array<u8, WUP028_REPORT_SIZE> report{};
  report[0] = REPORT_ID;
  for (int i = 0; i < 4; i++)
  {
    if (!(ports & (1 << i)))
      continue;
    const GCPadStatus pad = Pad::GetStatus(i);
    u8* d = report.data() + 1 + i * PORT_BYTES;
    d[0] = (wireless & (1 << i) ? STATUS_WIRELESS : STATUS_WIRED) |
           (rumble_power ? STATUS_RUMBLE_POWER : 0);
    const u16 b = pad.button;
    d[1] = (b & PAD_BUTTON_A ? 0x01 : 0) | (b & PAD_BUTTON_B ? 0x02 : 0) |
           (b & PAD_BUTTON_X ? 0x04 : 0) | (b & PAD_BUTTON_Y ? 0x08 : 0) |
           (b & PAD_BUTTON_LEFT ? 0x10 : 0) | (b & PAD_BUTTON_RIGHT ? 0x20 : 0) |
           (b & PAD_BUTTON_DOWN ? 0x40 : 0) | (b & PAD_BUTTON_UP ? 0x80 : 0);
    d[2] = (b & PAD_BUTTON_START ? 0x01 : 0) | (b & PAD_TRIGGER_Z ? 0x02 : 0) |
           (b & PAD_TRIGGER_R ? 0x04 : 0) | (b & PAD_TRIGGER_L ? 0x08 : 0);
    d[3] = pad.stickX;
    d[4] = pad.stickY;
    d[5] = pad.substickX;
    d[6] = pad.substickY;
    d[7] = pad.triggerLeft;
    d[8] = pad.triggerRight;
  }

  u32 size = std::min(cmd->length, WUP028_REPORT_SIZE);
  bool cut_short = false;
  {
    std::lock_guard lk(s_stats_mutex);
    s_stats.reports++;
    if (short_every > 0 && ++s_reports_since_short >= u32(short_every))
    {
      s_reports_since_short = 0;
      cut_short = true;
      s_stats.short_reports++;
    }
  }
  if (cut_short)
    size = std::min(size, SHORT_REPORT_SIZE);

  cmd->FillBuffer(report.data(), size);
  cmd->ScheduleTransferCompletion(size, REPORT_INTERVAL_US);
}
}  // namespace IOS::HLE::USB
