// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>

#include "Core/IOS/FS/FileSystem.h"

namespace IOS::HLE::FS
{
// One filesystem for both ES and /dev/fs. Real-NAND handles retain their original
// numbers; the separate d2x handle range remains valid across mode changes.
class D2xFileSystem final : public FileSystem
{
public:
  D2xFileSystem(std::shared_ptr<FileSystem> real_fs, std::function<bool()> flush_card,
                std::function<Gid()> get_gid, std::function<bool()> is_disc_title);
  ~D2xFileSystem() override;

  s32 Mount(s32 partition = -1);
  void Unmount();
  s32 SetMode(u32 mode, const std::string& path);
  bool IsEmulated(const std::string& path) const;
  // The console's own NAND, which this wraps.
  std::shared_ptr<FileSystem> GetRealFS() const { return m_real_fs; }

  void DoState(PointerWrap& p) override;
  ResultCode Format(Uid uid) override;
  Result<FileHandle> OpenFile(Uid uid, Gid gid, const std::string& path, Mode mode) override;
  ResultCode Close(Fd fd) override;
  Result<u32> ReadBytesFromFile(Fd fd, u8* ptr, u32 size) override;
  Result<u32> WriteBytesToFile(Fd fd, const u8* ptr, u32 size) override;
  Result<u32> SeekFile(Fd fd, u32 offset, SeekMode mode) override;
  Result<FileStatus> GetFileStatus(Fd fd) override;
  ResultCode CreateFile(Uid uid, Gid gid, const std::string& path, FileAttribute attribute,
                        Modes modes) override;
  ResultCode CreateDirectory(Uid uid, Gid gid, const std::string& path, FileAttribute attribute,
                             Modes modes) override;
  ResultCode Delete(Uid uid, Gid gid, const std::string& path) override;
  ResultCode Rename(Uid uid, Gid gid, const std::string& old_path,
                    const std::string& new_path) override;
  Result<std::vector<std::string>> ReadDirectory(Uid uid, Gid gid,
                                                 const std::string& path) override;
  Result<Metadata> GetMetadata(Uid uid, Gid gid, const std::string& path) override;
  ResultCode SetMetadata(Uid caller_uid, const std::string& path, Uid uid, Gid gid,
                         FileAttribute attribute, Modes modes) override;
  Result<NandStats> GetNandStats() override;
  Result<DirectoryStats> GetDirectoryStats(const std::string& path) override;
  Result<ExtendedDirectoryStats> GetExtendedDirectoryStats(const std::string& path) override;
  void SetNandRedirects(std::vector<NandRedirect> redirects) override;

private:
  struct Handle
  {
    bool opened = false;
    Mode mode = Mode::None;
    std::string path;
    std::string fat_path;
    u32 offset = 0;
  };

  // No FatFs objects survive a request: the SDIO sector interface and other
  // FatFs users must never see stale directory, FAT, or file-data caches.
  ResultCode Run(const std::string& operation, const std::string& path,
                 const std::function<int()>& request);
  std::string BuildPath(const std::string& path) const;
  Handle* GetHandle(Fd fd);
  Result<u32> Transfer(Fd fd, Mode access, u8* read_buffer, const u8* write_buffer, u32 size);
  void LogStop() const;

  static constexpr Fd FIRST_FAT_FD = 0x10000;
  std::shared_ptr<FileSystem> m_real_fs;
  std::function<bool()> m_flush_card;
  std::function<Gid()> m_get_gid;
  std::function<bool()> m_is_disc_title;
  std::array<Handle, 20> m_handles{};
  bool m_mounted = false;
  u32 m_partition_offset = 0;
  u32 m_mode = 0;
  std::string m_nand_path;
};
}  // namespace IOS::HLE::FS
