// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/CoreTiming.h"
#include "Core/HW/SystemTimers.h"
#include "Core/IOS/IOSC.h"

class PointerWrap;

namespace Core
{
class System;
}
namespace Memory
{
class MemoryManager;
}

namespace IOS::HLE
{
namespace FS
{
class FileSystem;
class D2xFileSystem;
}  // namespace FS

class Device;
class ESCore;
class ESDevice;
class FSCore;
class FSDevice;
class WiiSockMan;

struct Request;
struct OpenRequest;

struct IPCReply
{
  /// Constructs a reply with an average reply time.
  /// Please avoid using this function if more accurate timings are known.
  explicit IPCReply(s32 return_value_);
  explicit IPCReply(s32 return_value_, u64 reply_delay_ticks_);

  s32 return_value;
  u64 reply_delay_ticks;
};

constexpr SystemTimers::TimeBaseTick IPC_OVERHEAD_TICKS = 2700_tbticks;

// Used to make it more convenient for functions to return timing information
// without having to explicitly keep track of ticks in callers.
class Ticks
{
public:
  Ticks(u64* ticks = nullptr) : m_ticks(ticks) {}

  void Add(u64 ticks)
  {
    if (m_ticks != nullptr)
      *m_ticks += ticks;
  }

private:
  u64* m_ticks = nullptr;
};

template <typename ResultProducer>
IPCReply MakeIPCReply(u64 ticks, const ResultProducer& fn)
{
  const s32 result_value = fn(Ticks{&ticks});
  return IPCReply{result_value, ticks};
}

template <typename ResultProducer>
IPCReply MakeIPCReply(const ResultProducer& fn)
{
  return MakeIPCReply(0, fn);
}

enum IPCCommandType : u32
{
  IPC_CMD_OPEN = 1,
  IPC_CMD_CLOSE = 2,
  IPC_CMD_READ = 3,
  IPC_CMD_WRITE = 4,
  IPC_CMD_SEEK = 5,
  IPC_CMD_IOCTL = 6,
  IPC_CMD_IOCTLV = 7,
  // This is used for replies to commands.
  IPC_REPLY = 8,
};

enum class MemorySetupType
{
  IOSReload,
  Full,
};

enum class HangPPC : bool
{
  No = false,
  Yes = true,
};

void RAMOverrideForIOSMemoryValues(Memory::MemoryManager& memory, MemorySetupType setup_type);

void WriteReturnValue(Memory::MemoryManager& memory, s32 value, u32 address);

// RiftWii's tests (RIFTWII-D2X.md): whether this IOS title is a cIOS slot whose
// first content starts with d2x's version block, so it boots as d2x.
bool HasD2xSlot(FS::FileSystem& fs, u64 ios_title_id);

// HLE for the IOS kernel: IPC, device management, syscalls, and Dolphin-specific, IOS-wide calls.
class Kernel
{
public:
  explicit Kernel(IOSC::ConsoleType console_type = IOSC::ConsoleType::Retail);
  virtual ~Kernel();

  // These are *always* part of the IOS kernel and always available.
  // They are also the only available resource managers even before loading any module.
  std::shared_ptr<FS::FileSystem> GetFS();
  std::shared_ptr<FS::D2xFileSystem> GetD2xFS() const { return m_d2x_fs; }
  FSCore& GetFSCore();
  ESCore& GetESCore();

  u32 GetVersion() const;

  // RiftWii's tests (RIFTWII-D2X.md): a d2x cIOS runs in this slot. GetVersion()
  // then gives the base IOS d2x is built on, whose devices and features Dolphin
  // uses; the slot and d2x's revision are what the PPC sees at 0x80003140.
  bool IsD2x() const { return m_d2x_base != 0; }

  IOSC& GetIOSC();

protected:
  explicit Kernel(u64 title_id);

  std::unique_ptr<FSCore> m_fs_core;
  std::unique_ptr<ESCore> m_es_core;

  bool m_is_responsible_for_nand_root = false;
  u64 m_title_id = 0;
  u32 m_d2x_base = 0;  // the base IOS of a d2x slot; 0 when not d2x
  u16 m_d2x_revision = 0;  // d2x's revision (the slot's TMD version)

  IOSC m_iosc;
  std::shared_ptr<FS::FileSystem> m_fs;
  std::shared_ptr<FS::D2xFileSystem> m_d2x_fs;
  std::shared_ptr<WiiSockMan> m_socket_manager;
};

// HLE for an IOS tied to emulation: base kernel which may have additional modules loaded.
class EmulationKernel final : public Kernel
{
public:
  EmulationKernel(Core::System& system, u64 ios_title_id);
  ~EmulationKernel();

  // Get a resource manager by name.
  // This only works for devices which are part of the device map.
  std::shared_ptr<Device> GetDeviceByName(std::string_view device_name);
  std::shared_ptr<Device> GetDeviceByFileDescriptor(const u32 fd);

  std::shared_ptr<FSDevice> GetFSDevice();
  std::shared_ptr<ESDevice> GetESDevice();

  void DoState(PointerWrap& p);
  void UpdateDevices();
  void UpdateWantDeterminism(bool new_want_determinism);

  std::shared_ptr<WiiSockMan> GetSocketManager();

  void HandleIPCEvent(u64 userdata);
  void UpdateIPC();

  void EnqueueIPCRequest(u32 address);
  void EnqueueIPCReply(const Request& request, s32 return_value, s64 cycles_in_future = 0,
                       CoreTiming::FromThread from = CoreTiming::FromThread::CPU);

  void SetUidForPPC(u32 uid);
  u32 GetUidForPPC() const;
  void SetGidForPPC(u16 gid);
  u16 GetGidForPPC() const;

  bool BootstrapPPC(const std::string& boot_content_path);
  bool SetupMemoryForThisIOS(MemorySetupType setup_type);
  bool BootIOS(u64 ios_title_id, HangPPC hang_ppc = HangPPC::No,
               const std::string& boot_content_path = {});
  void InitIPC();

  Core::System& GetSystem() const { return m_system; }

private:
  void ExecuteIPCCommand(u32 address);
  std::optional<IPCReply> HandleIPCCommand(const Request& request);

  void AddDevice(std::unique_ptr<Device> device);

  void AddStaticDevices();
  s32 GetFreeDeviceID();
  std::optional<IPCReply> OpenDevice(OpenRequest& request);

  Core::System& m_system;

  static constexpr u8 IPC_MAX_FDS = 0x18;
  std::map<std::string, std::shared_ptr<Device>, std::less<>> m_device_map;
  // TODO: make this fdmap per process.
  std::array<std::shared_ptr<Device>, IPC_MAX_FDS> m_fdmap;

  u32 m_ppc_uid = 0;
  u16 m_ppc_gid = 0;

  using IPCMsgQueue = std::deque<u32>;
  IPCMsgQueue m_request_queue;  // ppc -> arm
  IPCMsgQueue m_reply_queue;    // arm -> ppc
  u64 m_last_reply_time = 0;
  bool m_ipc_paused = false;
};

// Used for controlling and accessing an IOS instance that is tied to emulation.
void Init(Core::System& system);
void Shutdown(Core::System& system);

}  // namespace IOS::HLE
