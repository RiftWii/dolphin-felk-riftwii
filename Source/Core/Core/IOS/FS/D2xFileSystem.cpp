// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/IOS/FS/D2xFileSystem.h"

#include <algorithm>
#include <string_view>
#include <utility>

// Does not compile if diskio.h is included first.
// clang-format off
#include "ff.h"
#include "diskio.h"
// clang-format on

#include "Common/ChunkFile.h"
#include "Common/FatFsUtil.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Logging/Log.h"
#include "Core/Config/MainSettings.h"
#include "Core/IOS/Device.h"
#include "Core/IOS/USB/USBStorage.h"

namespace IOS::HLE::FS
{
namespace
{
constexpr std::array<std::pair<char, std::string_view>, 7> FAT_ESCAPES{{
    {'"', "&qt;"},
    {'*', "&st;"},
    {':', "&cl;"},
    {'<', "&lt;"},
    {'>', "&gt;"},
    {'?', "&qm;"},
    {'|', "&vb;"},
}};

std::string EscapePath(std::string_view path)
{
  std::string escaped;
  for (const char character : path)
  {
    const auto escape =
        std::ranges::find(FAT_ESCAPES, character, &std::pair<char, std::string_view>::first);
    if (escape == FAT_ESCAPES.end())
      escaped += character;
    else
      escaped += escape->second;
  }
  return escaped;
}

std::string UnescapeName(std::string_view name)
{
  std::string unescaped;
  while (!name.empty())
  {
    const auto escape = std::ranges::find_if(
        FAT_ESCAPES, [&](const auto& entry) { return name.starts_with(entry.second); });
    if (escape == FAT_ESCAPES.end())
    {
      unescaped += name.front();
      name.remove_prefix(1);
    }
    else
    {
      unescaped += escape->first;
      name.remove_prefix(escape->second.size());
    }
  }
  return unescaped;
}

// FatFs interprets dot components and backslashes itself. Do not allow them to
// escape the selected NAND folder, even though the host backend is permissive.
bool IsSafePath(std::string_view path)
{
  if (!path.starts_with('/') || path.find('\\') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos)
    return false;
  while (!path.empty())
  {
    path.remove_prefix(1);
    const auto separator = path.find('/');
    const auto component = path.substr(0, separator);
    if (component == "." || component == "..")
      return false;
    if (separator == std::string_view::npos)
      break;
    path.remove_prefix(separator);
  }
  return true;
}

std::string GetImagePath(Drive drive)
{
  return drive == Drive::USB ? USB::StorageImage::GetConfiguredPath() :
                               File::GetUserPath(F_WIISDCARDIMAGE_IDX);
}

bool IsImageAvailable(Drive drive)
{
  return drive == Drive::USB ? USB::StorageImage(GetImagePath(drive)).GetSectorCount() != 0 :
                               Config::Get(Config::MAIN_WII_SD_CARD);
}

class CardCallbacks final : public Common::FatFsCallbacks
{
public:
  explicit CardCallbacks(u32 partition_offset, Drive drive)
      : image(GetImagePath(drive),
              drive == Drive::USB || Config::Get(Config::MAIN_ALLOW_SD_WRITES) ? "r+b" : "rb",
              File::SharedAccess::ReadWrite),
        m_partition_offset(partition_offset), m_drive(drive)
  {
  }

  int DiskRead(u8, u8* buffer, u32 sector, unsigned int count) override
  {
    return Seek(sector, count) && image.ReadBytes(buffer, static_cast<size_t>(count) * 512) ?
               RES_OK :
               RES_ERROR;
  }

  int DiskWrite(u8, const u8* buffer, u32 sector, unsigned int count) override
  {
    if (m_drive == Drive::SD && !Config::Get(Config::MAIN_ALLOW_SD_WRITES))
      return RES_WRPRT;
    return Seek(sector, count) && image.WriteBytes(buffer, static_cast<size_t>(count) * 512) ?
               RES_OK :
               RES_ERROR;
  }

  int DiskIOCtl(u8, u8 command, void* buffer) override
  {
    switch (command)
    {
    case CTRL_SYNC:
      return image.Flush() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
      if (m_partition_offset > image.GetSize() / 512)
        return RES_PARERR;
      *static_cast<LBA_t*>(buffer) = static_cast<LBA_t>(image.GetSize() / 512 - m_partition_offset);
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD*>(buffer) = 512;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*>(buffer) = 1;
      return RES_OK;
    default:
      return RES_PARERR;
    }
  }

  File::IOFile image;

private:
  bool Seek(u32 sector, unsigned int count)
  {
    const u64 offset = (static_cast<u64>(sector) + m_partition_offset) * 512;
    const u64 end = offset + static_cast<u64>(count) * 512;
    return end <= image.GetSize() && image.Seek(offset, File::SeekOrigin::Begin);
  }

  u32 m_partition_offset;
  Drive m_drive;
};

ResultCode ConvertFatResult(int result)
{
  switch (result)
  {
  case FR_OK:
    return ResultCode::Success;
  case FR_NO_FILE:
  case FR_NO_PATH:
  case FR_INVALID_NAME:
    return ResultCode::NotFound;
  case FR_EXIST:
    return ResultCode::AlreadyExists;
  case FR_DENIED:
  case FR_WRITE_PROTECTED:
    return ResultCode::AccessDenied;
  default:
    // d2x's FS_EFATAL is -101, which is Dolphin's ResultCode::Invalid.
    return ResultCode::Invalid;
  }
}

int VisitDirectory(const std::string& path,
                   const std::function<int(const std::string&, const FILINFO&)>& visit,
                   bool filter_isfs_names = true)
{
  DIR directory{};
  int result = f_opendir(&directory, path.c_str());
  if (result != FR_OK)
    return result;

  while (true)
  {
    FILINFO info{};
    result = f_readdir(&directory, &info);
    if (result != FR_OK || info.fname[0] == '\0')
      break;
    // d2x skips dot entries and names that cannot be returned through ISFS.
    if (info.fname[0] == '.' ||
        (filter_isfs_names && UnescapeName(info.fname).size() > MaxFilenameLength))
      continue;
    result = visit(path + '/' + info.fname, info);
    if (result != FR_OK)
      break;
  }
  const int close_result = f_closedir(&directory);
  return result == FR_OK ? close_result : result;
}

int DeleteTree(const std::string& path)
{
  FILINFO info{};
  int result = f_stat(path.c_str(), &info);
  if (result != FR_OK)
    return result;
  if (info.fattrib & AM_DIR)
  {
    result = VisitDirectory(
        path, [](const std::string& child, const FILINFO&) { return DeleteTree(child); }, false);
    if (result != FR_OK)
      return result;
  }
  return f_unlink(path.c_str());
}
}  // namespace

D2xFileSystem::D2xFileSystem(std::shared_ptr<FileSystem> real_fs, std::function<bool()> flush_card,
                             std::function<Gid()> get_gid, std::function<bool()> is_disc_title)
    : m_real_fs(std::move(real_fs)), m_flush_card(std::move(flush_card)),
      m_get_gid(std::move(get_gid)), m_is_disc_title(std::move(is_disc_title))
{
}

D2xFileSystem::~D2xFileSystem()
{
  LogStop();
}

void D2xFileSystem::LogStop() const
{
  if (m_mode)
    INFO_LOG_FMT(IOS_FS, "d2x NAND redirect stopped: mode {:#x}, folder \"{}\"", m_mode,
                 m_nand_path);
}

ResultCode D2xFileSystem::Run(const std::string& operation, const std::string& path,
                              const std::function<int()>& request,
                              std::optional<Drive> requested_drive)
{
  const Drive drive = requested_drive.value_or(RedirectDrive());
  const Volume& volume = VolumeOf(drive);
  int result = FR_NOT_READY;
  if (volume.mounted && IsImageAvailable(drive) && (drive == Drive::USB || m_flush_card()))
  {
    CardCallbacks callbacks(volume.partition_offset, drive);
    if (callbacks.image)
    {
      Common::RunInFatFsContext(callbacks, [&] {
        FATFS filesystem{};
        result = f_mount(&filesystem, "0:", 1);
        if (result == FR_OK)
          result = request();
        f_mount(nullptr, "0:", 0);
      });
      // Close flushes the host stream before the SDIO device can read it.
      if (!callbacks.image.Close() && result == FR_OK)
        result = FR_DISK_ERR;
    }
  }
  const ResultCode code = ConvertFatResult(result);
  if (code != ResultCode::Success)
    WARN_LOG_FMT(IOS_FS, "d2x {}({}): FatFs {}, IOS {}", operation, path, result,
                 static_cast<s32>(ConvertResult(code)));
  return code;
}

Drive D2xFileSystem::RedirectDrive() const
{
  return (m_mode & 2) != 0 ? Drive::USB : Drive::SD;
}

// Mounting a drive never changes which drive the NAND emulation uses: that
// follows d2x's mode alone (a loader may mount both drives).
s32 D2xFileSystem::Mount(s32 partition, bool usb)
{
  const Drive drive = usb ? Drive::USB : Drive::SD;
  Volume& volume = VolumeOf(drive);
  if (!IsImageAvailable(drive) || (drive == Drive::SD && !m_flush_card()))
    return IPC_EINVAL;
  volume.mounted = false;
  volume.partition_offset = 0;
  if (partition >= 0)
  {
    if (partition > 3)
      return -101;
    File::IOFile image(GetImagePath(drive), "rb", File::SharedAccess::ReadWrite);
    std::array<u8, 512> mbr{};
    if (!image || !image.ReadBytes(mbr.data(), mbr.size()))
      return -101;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa)
      return -101;
    // Like d2x's check_fs, accept a FAT boot sector at LBA 0 even when a
    // partition was requested (Dolphin's generated cards are superfloppies).
    const auto has_fat_signature = [&](size_t offset) {
      return std::string_view(reinterpret_cast<const char*>(mbr.data() + offset), 3) == "FAT";
    };
    if (!has_fat_signature(54) && !has_fat_signature(82))
    {
      const size_t entry = 446 + partition * 16;
      if (mbr[entry + 4] == 0)
        return -101;
      for (size_t byte = 0; byte < 4; ++byte)
        volume.partition_offset |= static_cast<u32>(mbr[entry + 8 + byte]) << (byte * 8);
      if (volume.partition_offset == 0)
        return -101;
    }
  }
  volume.mounted = true;
  const auto result = Run(
      "Mount", "/",
      [] {
        DIR root{};
        const int status = f_opendir(&root, "0:/");
        return status == FR_OK ? f_closedir(&root) : status;
      },
      drive);
  volume.mounted = result == ResultCode::Success;
  return volume.mounted ? 0 : -101;
}

void D2xFileSystem::Unmount(bool usb)
{
  // Requests leave neither host-stream nor FatFs write caches behind.
  const Drive drive = usb ? Drive::USB : Drive::SD;
  if (drive == Drive::SD)
    m_flush_card();
  VolumeOf(drive).mounted = false;
}

bool D2xFileSystem::IsEmulated(const std::string& path) const
{
  if (!(m_mode & 3) || path == "/tmp/launch.sys")
    return false;
  if (m_mode & 0x100)
    return path.starts_with('/') && !path.starts_with("/dev");
  if (path == "/tmp/uid.sys")
    return false;
  // These intentionally use d2x's prefix tests, without component boundaries.
  constexpr std::array prefixes{"/ticket/00010001",
                                "/ticket/00010005",
                                "/title/00010000",
                                "/title/00010001",
                                "/title/00010004",
                                "/title/00010005",
                                "/tmp",
                                "/import"};
  return path == "/sys/disc.sys" || std::ranges::any_of(prefixes, [&](const char* prefix) {
           return path.starts_with(prefix);
         });
}

std::string D2xFileSystem::BuildPath(const std::string& path) const
{
  return "0:" + m_nand_path + EscapePath(path);
}

s32 D2xFileSystem::SetMode(u32 mode, const std::string& path)
{
  if (!(mode & 3))
  {
    LogStop();
    m_mode = 0;
    m_nand_path.clear();
    m_flush_card();
    return 0;
  }
  if ((mode & 3) == 3)
    return -101;
  // The new mode's drive; the active mode keeps its own until this succeeds.
  const Drive drive = (mode & 2) != 0 ? Drive::USB : Drive::SD;
  if ((!path.empty() && !IsSafePath(path)) || path.find(':') != std::string::npos ||
      path.size() > 255)
    return -101;
  if (!VolumeOf(drive).mounted)
  {
    const s32 result = Mount(-1, drive == Drive::USB);
    if (result < 0)
      return result;
  }
  const auto ready = Run("SetMode", path, [] { return FR_OK; }, drive);
  if (ready != ResultCode::Success)
    return ConvertResult(ready);

  LogStop();
  m_mode = mode | 0x80;
  m_nand_path = path;
  INFO_LOG_FMT(IOS_FS, "d2x NAND redirect started: mode {:#x}, folder \"{}\"", m_mode, m_nand_path);
  // __FS_PrepareFolders bypasses the partial-mode routing rules. d2x's own
  // version returns nothing: a folder it can't make or clear doesn't fail the
  // mode switch, and the request that later needs it reports the error.
  Run("PrepareFolders", path, [&] {
    constexpr std::array folders{
        "/import",          "/sys",           "/ticket",         "/ticket/00010001",
        "/ticket/00010005", "/title",         "/title/00010000", "/title/00010001",
        "/title/00010004",  "/title/00010005"};
    for (const char* folder : folders)
      f_mkdir(BuildPath(folder).c_str());
    DeleteTree(BuildPath("/tmp"));
    f_mkdir(BuildPath("/tmp").c_str());
    return FR_OK;
  });
  return 0;
}

D2xFileSystem::NandConfig D2xFileSystem::GetNandConfig() const
{
  NandConfig config;
  config.mode = m_mode;
  config.nand_path = m_nand_path;
  for (size_t i = 0; i < m_volumes.size(); ++i)
  {
    config.volume_mounted[i] = m_volumes[i].mounted;
    config.volume_offsets[i] = m_volumes[i].partition_offset;
  }
  return config;
}

void D2xFileSystem::ApplyNandConfig(const NandConfig& config)
{
  m_mode = config.mode;
  m_nand_path = config.nand_path;
  for (size_t i = 0; i < m_volumes.size(); ++i)
  {
    m_volumes[i].mounted = config.volume_mounted[i];
    m_volumes[i].partition_offset = config.volume_offsets[i];
  }
}

namespace
{
D2xPendingNand s_pending_d2x_nand;
}  // namespace

void SetNandConfigForNextIOSBoot(const D2xFileSystem::NandConfig& config)
{
  s_pending_d2x_nand.has_value = true;
  s_pending_d2x_nand.config = config;
}

D2xPendingNand TakeNandConfigForNextIOSBoot()
{
  D2xPendingNand pending = s_pending_d2x_nand;
  s_pending_d2x_nand = {};
  return pending;
}

D2xPendingNand& GetPendingNandConfigForNextIOSBoot()
{
  return s_pending_d2x_nand;
}

void D2xFileSystem::DoState(PointerWrap& p)
{
  m_real_fs->DoState(p);
  for (auto& volume : m_volumes)
  {
    p.Do(volume.mounted);
    p.Do(volume.partition_offset);
  }
  p.Do(m_mode);
  p.Do(m_nand_path);
  for (auto& handle : m_handles)
  {
    p.Do(handle.opened);
    p.Do(handle.mode);
    p.Do(handle.path);
    p.Do(handle.fat_path);
    p.Do(handle.offset);
    p.Do(handle.drive);
  }
  // Reopening by path on the next request rebuilds all FatFs objects; no
  // process pointers, cached FAT sectors, or stale FIL objects are serialized.
}

ResultCode D2xFileSystem::Format(Uid uid)
{
  return m_mode ? ResultCode::Success : m_real_fs->Format(uid);
}

Result<FileHandle> D2xFileSystem::OpenFile(Uid uid, Gid gid, const std::string& path, Mode mode)
{
  if (!IsEmulated(path))
    return m_real_fs->OpenFile(uid, gid, path, mode);
  const auto handle = std::ranges::find(m_handles, false, &Handle::opened);
  if (handle == m_handles.end())
  {
    WARN_LOG_FMT(IOS_FS, "d2x Open({}): IOS {}", path,
                 static_cast<s32>(ConvertResult(ResultCode::NoFreeHandle)));
    return ResultCode::NoFreeHandle;
  }
  if (!IsSafePath(path) || !IsValidPath(path) || mode == Mode::None)
  {
    WARN_LOG_FMT(IOS_FS, "d2x Open({}): invalid path or mode, IOS -101", path);
    return ResultCode::Invalid;
  }
  const auto result = Run("Open", path, [&] {
    FIL file{};
    const int status = f_open(&file, BuildPath(path).c_str(), static_cast<BYTE>(mode));
    return status == FR_OK ? f_close(&file) : status;
  });
  if (result != ResultCode::Success)
    return result;
  *handle = {true, mode, path, BuildPath(path), 0, RedirectDrive()};
  return FileHandle(this, FIRST_FAT_FD + static_cast<Fd>(handle - m_handles.begin()));
}

D2xFileSystem::Handle* D2xFileSystem::GetHandle(Fd fd)
{
  if (fd < FIRST_FAT_FD || fd - FIRST_FAT_FD >= m_handles.size() ||
      !m_handles[fd - FIRST_FAT_FD].opened)
  {
    WARN_LOG_FMT(IOS_FS, "d2x file request(fd={}): no open emulated path, IOS -101", fd);
    return nullptr;
  }
  return &m_handles[fd - FIRST_FAT_FD];
}

ResultCode D2xFileSystem::Close(Fd fd)
{
  if (fd < FIRST_FAT_FD)
    return m_real_fs->Close(fd);
  auto* handle = GetHandle(fd);
  if (!handle)
    return ResultCode::Invalid;
  *handle = {};
  return ResultCode::Success;
}

Result<u32> D2xFileSystem::Transfer(Fd fd, Mode access, u8* read_buffer, const u8* write_buffer,
                                    u32 size)
{
  auto* handle = GetHandle(fd);
  if (!handle)
    return ResultCode::Invalid;
  const bool writing = access == Mode::Write;
  u32 transferred = 0;
  const auto result = Run(writing ? "Write" : "Read", handle->path, [&] {
    if (!(static_cast<u8>(handle->mode) & static_cast<u8>(access)))
      return static_cast<int>(FR_DENIED);
    FIL file{};
    int status = f_open(&file, handle->fat_path.c_str(), static_cast<BYTE>(handle->mode));
    if (status != FR_OK)
      return status;
    status = f_lseek(&file, handle->offset);
    if (status == FR_OK)
    {
      if (writing)
        status = f_write(&file, write_buffer, size, &transferred);
      else
        status = f_read(&file, read_buffer, size, &transferred);
    }
    const int close_status = f_close(&file);
    return status == FR_OK ? close_status : status;
  }, handle->drive);
  if (result != ResultCode::Success)
    return result;
  handle->offset += transferred;
  return transferred;
}

Result<u32> D2xFileSystem::ReadBytesFromFile(Fd fd, u8* ptr, u32 size)
{
  if (fd < FIRST_FAT_FD)
    return m_real_fs->ReadBytesFromFile(fd, ptr, size);
  return Transfer(fd, Mode::Read, ptr, nullptr, size);
}

Result<u32> D2xFileSystem::WriteBytesToFile(Fd fd, const u8* ptr, u32 size)
{
  if (fd < FIRST_FAT_FD)
    return m_real_fs->WriteBytesToFile(fd, ptr, size);
  return Transfer(fd, Mode::Write, nullptr, ptr, size);
}

Result<FileStatus> D2xFileSystem::GetFileStatus(Fd fd)
{
  if (fd < FIRST_FAT_FD)
    return m_real_fs->GetFileStatus(fd);
  const auto* handle = GetHandle(fd);
  if (!handle)
    return ResultCode::Invalid;
  FILINFO info{};
  const auto result =
      Run("FileStats", handle->path, [&] { return f_stat(handle->fat_path.c_str(), &info); },
          handle->drive);
  if (result != ResultCode::Success)
    return result;
  return FileStatus{handle->offset, static_cast<u32>(info.fsize)};
}

Result<u32> D2xFileSystem::SeekFile(Fd fd, u32 offset, SeekMode mode)
{
  if (fd < FIRST_FAT_FD)
    return m_real_fs->SeekFile(fd, offset, mode);
  const auto status = GetFileStatus(fd);
  if (!status)
    return status.Error();
  u32 position = offset;
  if (mode == SeekMode::Current)
    position += status->offset;
  else if (mode == SeekMode::End)
    position += status->size;
  if ((mode != SeekMode::Set && mode != SeekMode::Current && mode != SeekMode::End) ||
      position > status->size)
  {
    WARN_LOG_FMT(IOS_FS, "d2x Seek({}): IOS -101", GetHandle(fd)->path);
    return ResultCode::Invalid;
  }
  GetHandle(fd)->offset = position;
  return position;
}

ResultCode D2xFileSystem::CreateFile(Uid uid, Gid gid, const std::string& path,
                                     FileAttribute attribute, Modes modes)
{
  if (!IsEmulated(path))
    return m_real_fs->CreateFile(uid, gid, path, attribute, modes);
  return Run("CreateFile", path, [&] {
    if (!IsValidNonRootPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    FIL file{};
    const int result = f_open(&file, BuildPath(path).c_str(), FA_CREATE_NEW | FA_WRITE);
    return result == FR_OK ? f_close(&file) : result;
  });
}

ResultCode D2xFileSystem::CreateDirectory(Uid uid, Gid gid, const std::string& path,
                                          FileAttribute attribute, Modes modes)
{
  if (!IsEmulated(path))
    return m_real_fs->CreateDirectory(uid, gid, path, attribute, modes);
  return Run("CreateDirectory", path, [&] {
    if (!IsValidNonRootPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    return static_cast<int>(f_mkdir(BuildPath(path).c_str()));
  });
}

ResultCode D2xFileSystem::Delete(Uid uid, Gid gid, const std::string& path)
{
  if (!IsEmulated(path))
    return m_real_fs->Delete(uid, gid, path);
  return Run("Delete", path, [&] {
    if (!IsValidNonRootPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    return static_cast<int>(f_unlink(BuildPath(path).c_str()));
  });
}

ResultCode D2xFileSystem::Rename(Uid uid, Gid gid, const std::string& old_path,
                                 const std::string& new_path)
{
  const bool old_emulated = IsEmulated(old_path);
  const bool new_emulated = IsEmulated(new_path);
  if (!old_emulated && !new_emulated)
    return m_real_fs->Rename(uid, gid, old_path, new_path);
  if (old_emulated != new_emulated)
  {
    WARN_LOG_FMT(IOS_FS, "d2x Rename({} -> {}): mixed real/emulated paths, IOS -101", old_path,
                 new_path);
    return ResultCode::Invalid;
  }
  return Run("Rename", old_path + " -> " + new_path, [&] {
    if (!IsValidNonRootPath(old_path) || !IsSafePath(old_path) || !IsValidNonRootPath(new_path) ||
        !IsSafePath(new_path))
      return static_cast<int>(FR_INVALID_NAME);
    const std::string source = BuildPath(old_path);
    const std::string destination = BuildPath(new_path);
    FILINFO info{};
    int result = f_stat(source.c_str(), &info);
    if (result != FR_OK || source == destination)
      return result;
    // d2x replaces existing files and recursively removes destination folders.
    result = f_stat(destination.c_str(), &info);
    if (result == FR_OK)
    {
      result = DeleteTree(destination);
      if (result != FR_OK)
        return result;
    }
    result = f_rename(source.c_str(), destination.c_str());
    if (result == FR_OK)
    {
      for (auto& handle : m_handles)
      {
        // Handles keep the physical location they were opened in across mode changes.
        if (handle.opened && handle.drive == RedirectDrive() &&
            (handle.fat_path == source || handle.fat_path.starts_with(source + '/')))
        {
          const std::string suffix = handle.fat_path.substr(source.size());
          handle.path = new_path + UnescapeName(suffix);
          handle.fat_path = destination + suffix;
        }
      }
    }
    return result;
  });
}

Result<std::vector<std::string>> D2xFileSystem::ReadDirectory(Uid uid, Gid gid,
                                                              const std::string& path)
{
  if (!IsEmulated(path))
    return m_real_fs->ReadDirectory(uid, gid, path);
  std::vector<std::string> entries;
  const auto result = Run("ReadDirectory", path, [&] {
    if (!IsValidPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    FILINFO info{};
    if (f_stat(BuildPath(path).c_str(), &info) == FR_OK && !(info.fattrib & AM_DIR))
      return static_cast<int>(FR_INVALID_OBJECT);
    return VisitDirectory(BuildPath(path), [&](const std::string&, const FILINFO& info) {
      entries.push_back(UnescapeName(info.fname));
      return FR_OK;
    });
  });
  if (result != ResultCode::Success)
    return result;
  return entries;
}

Result<Metadata> D2xFileSystem::GetMetadata(Uid uid, Gid gid, const std::string& path)
{
  if (!IsEmulated(path))
    return m_real_fs->GetMetadata(uid, gid, path);
  FILINFO info{};
  const auto result = Run("GetAttr", path, [&] {
    if (!IsValidPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    return static_cast<int>(f_stat(BuildPath(path).c_str(), &info));
  });
  if (result != ResultCode::Success)
    return result;

  const bool es_attributes = path.starts_with("/tmp/") || path.starts_with("/import");
  constexpr std::string_view nocopy_pattern = "/title/0001000#/########/data/nocopy";
  bool nocopy = path.size() >= nocopy_pattern.size();
  for (size_t index = 0; nocopy && index < nocopy_pattern.size(); ++index)
    nocopy = nocopy_pattern[index] == '#' || nocopy_pattern[index] == path[index];
  Metadata metadata{};
  metadata.uid = es_attributes ? 0 : 0x1001;
  metadata.gid = es_attributes ? 0 : m_get_gid();
  metadata.modes = {Mode::ReadWrite, Mode::ReadWrite,
                    es_attributes || nocopy ? Mode::None : Mode::ReadWrite};
  metadata.is_file = !(info.fattrib & AM_DIR);
  metadata.size = static_cast<u32>(info.fsize);
  return metadata;
}

ResultCode D2xFileSystem::SetMetadata(Uid caller_uid, const std::string& path, Uid uid, Gid gid,
                                      FileAttribute attribute, Modes modes)
{
  if (!IsEmulated(path))
    return m_real_fs->SetMetadata(caller_uid, path, uid, gid, attribute, modes);
  const auto metadata = GetMetadata(caller_uid, gid, path);
  return metadata ? ResultCode::Success : metadata.Error();
}

Result<NandStats> D2xFileSystem::GetNandStats()
{
  if (!m_mode)
    return m_real_fs->GetNandStats();
  return NandStats{0x4000, 0x5dec, 0x1dd4, 0x10, 0x2f0, 0x146b, 0x394};
}

Result<DirectoryStats> D2xFileSystem::GetDirectoryStats(const std::string& path)
{
  if (!IsEmulated(path))
    return m_real_fs->GetDirectoryStats(path);
  // FS_Ioctlv GETUSAGE deliberately returns these values for channels, even
  // when the path does not exist. Disc games use the table or the FAT totals.
  if (!m_is_disc_title())
    return DirectoryStats{1, 1};

  struct FakeUsage
  {
    std::string_view path;
    DirectoryStats stats;
  };
  constexpr std::array<FakeUsage, 6> fake_usage{{
      {"/meta", {3, 8}},
      {"/ticket", {71, 77}},
      {"/title/00010000", {75, 55}},
      {"/title/00010001", {1505, 74}},
      {"/title/00010004", {185, 9}},
      {"/title/00010005", {23, 42}},
  }};
  for (const auto& entry : fake_usage)
  {
    if (path != entry.path)
      continue;
    const auto metadata = GetMetadata(0, 0, path);
    if (!metadata)
      return metadata.Error();
    return entry.stats;
  }
  const auto stats = GetExtendedDirectoryStats(path);
  if (!stats)
    return stats.Error();
  u32 blocks = static_cast<u32>(stats->used_clusters);
  if (blocks > 0x2000)
    blocks = 0x1000 + (blocks & 0xfff);
  return DirectoryStats{blocks, static_cast<u32>(stats->used_inodes)};
}

Result<ExtendedDirectoryStats> D2xFileSystem::GetExtendedDirectoryStats(const std::string& path)
{
  if (!IsEmulated(path))
    return m_real_fs->GetExtendedDirectoryStats(path);
  u64 total_bytes = 0;
  ExtendedDirectoryStats stats{0, 1};
  const auto result = Run("GetUsage", path, [&] {
    if (!IsValidPath(path) || !IsSafePath(path))
      return static_cast<int>(FR_INVALID_NAME);
    std::function<int(const std::string&, const FILINFO&)> count_entry;
    count_entry = [&](const std::string& child, const FILINFO& info) {
      if (info.fattrib & AM_DIR)
        return VisitDirectory(child, count_entry);
      ++stats.used_inodes;
      total_bytes += info.fsize;
      return static_cast<int>(FR_OK);
    };
    return VisitDirectory(BuildPath(path), count_entry);
  });
  if (result != ResultCode::Success)
    return result;
  // d2x converts the sum of byte sizes to NAND blocks (rounding down).
  stats.used_clusters = total_bytes / CLUSTER_SIZE;
  return stats;
}

void D2xFileSystem::SetNandRedirects(std::vector<NandRedirect> redirects)
{
  m_real_fs->SetNandRedirects(std::move(redirects));
}
}  // namespace IOS::HLE::FS
