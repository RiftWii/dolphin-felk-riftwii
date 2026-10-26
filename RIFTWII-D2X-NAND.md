# Step 2: d2x's `fat` module and NAND emulation

First draft by Muse, reviewed and corrected against d2x's source.

Branch `d2x-fat`, from `d2x-emulation`. d2x source: github.com/davebaol/d2x-cios
(GPL-2). No d2x code pasted; behavior re-implemented in Dolphin's style.
RiftWii side: `wii/storage/d2xnandemu.cpp` (worktree `wiiware-flawless`, read only).

## 1. Exact d2x semantics

### 1.1 `fat` module: `IOS_Open("fat", 0)`, ioctlv 0xF0 / 0xF1

d2x files: `source/fat-module/main.c` (`__FAT_Ioctlv`, `main` loop),
`source/fat-module/module.h` (command numbers), `source/fat-module/fat_wrapper.c`
(`FAT_Mount`), `source/fat-module/fs.h` (error codes).

- Device names (`main.c`, `IOS_OPEN` case): `"fat"` opens the module itself and
  answers 0; `"fat" + absolute-path` opens a file on the FAT volume;
  `"$" + relative-path` opens a file inside the emu-NAND folder; anything else
  answers `IPC_EINVAL` (-4). RiftWii only opens `"fat"`. Closing the module fd
  answers 0 (`IOS_CLOSE`: fds that are 32-byte-aligned addresses are files and
  get `FAT_Close`; anything else, i.e. the module, returns 0).
- ioctlv numbers (`module.h`): `IOCTL_FAT_MOUNT_SD 0xF0`, `IOCTL_FAT_UMOUNT_SD
  0xF1` (also `MOUNT_USB 0xF2`, `UMOUNT_USB 0xF3`, `GETPARTITION 0xF4`,
  ioctl `SETNANDPATH 0xF5`, ioctl `FILESTATS 11`; unlisted commands answer
  `IPC_EINVAL`). RiftWii passes no vectors to 0xF0/0xF1; the handler tolerates
  an optional first in-vector selecting the partition (default -1 = any).
- 0xF0 (`fat_wrapper.c FAT_Mount`, device 0 = SD): `sdio_Startup()` fails (no
  card, card won't start) -> `IPC_EINVAL` (-4). Partition >= 0 mounts just it;
  else partitions 0..3 are tried in order, each `f_mount` + opening `"0:/"`;
  the first whose root opens wins with `FS_SUCCESS` (0). None mounts ->
  `FS_EFATAL` (-101). -101 therefore covers both "card unreadable" and "no
  usable FAT volume", matching RiftWii's `kFatNoVolume` comment.
- 0xF1 (`main.c`): `FAT_Unmount(0)` (flushes FatFs cache, clears the
  partition), then `sdio_Shutdown()` (stops its SD driver) and answers its
  bool: 1 when it stopped a running driver; `IPC_EINVAL` (-4) when the
  driver was not running (`cios-lib/sdio.c sdio_Shutdown`). 0xF0 starts the
  driver even when no partition then mounts, so 0xF1 after a failed 0xF0
  still answers 1.
- d2x error codes (`fat-module/fs.h`): `FS_SUCCESS 0`, `FS_EINVAL -4`,
  `FS_EFATAL -101`, `FS_EACCESS -102`, `FS_EEXIST -105`, `FS_ENOENT -106`.
  Note: d2x `FS_EFATAL` (-101) is numerically Dolphin's `FS_EINVAL` (-101);
  different names, same wire value.

### 1.2 `/dev/fs` ioctlv 100 (and ioctl 100 / ioctlv 101)

d2x files: `source/ffs-plugin/ioctl.h` (`IOCTL_ISFS_SETMODE 100`,
`IOCTL_ISFS_GETMODE 101`), `source/ffs-plugin/plugin.h` (modes, `fsConfig`),
`source/ffs-plugin/plugin.c` (`FS_Ioctlv`, `__FS_SetMode`), `source/ffs-plugin/
fs_tools.c` (`FS_CheckRealPath`, path building), `source/ffs-plugin/fat.h`
(`FAT_MAXPATH 256`).

- Vectors: in-vector[0] = u32 mode (missing/empty -> `IPC_ENOENT`, -6);
  in-vector[1] = path if present, else `""` (a>setmode with one vector only
  exists as ioctl 100, which passes `""`). A non-v ioctl 100 also exists and
  needs `inlen >= 4`.
- Modes (`plugin.h`): `MODE_SDHC 0x01`, `MODE_USB 0x02`, `MODE_REV17 0x80`,
  `MODE_FULL 0x100`. RiftWii uses 1 (SD, game paths), 0x101 (SD, whole NAND),
  0 (off). d2x builds with `FORCE_MODE_REV17`, so enabling always stores
  `mode | 0x80`.
- `__FS_SetMode(mode, path)`: if `mode & (SDHC|USB)`: title running
  (`Stealth_CheckRunningTitle`) -> -6; store `path` (strcpy, 256-byte
  `config.path`), `pathlen`, `mode`; `FAT_Init("<dev>:" + path)` (opens
  `"fat"` itself, sends ioctl `SETNANDPATH`); on success create the 12
  folders (`__FS_PrepareFolders`: `/import /sys /ticket… /title/0001000x…
  /tmp` recreated) and return 0 (>= 0). Failure returns the FAT error.
  Else (off, including unrecognized modes without SD/USB bits): title running
  and request not from ES -> -6; else clear mode/path and return 0.
- ioctlv 101 (`GETMODE`) returns stored mode + path; same title/ES refusal.
  RiftWii never calls 100-as-ioctl or 101.
- Path form: card-relative with leading slash, e.g. `/nands/01`; absolute FAT
  path is `"<dev>:" + config.path + escaped Wii path` (device `0:` = SD).
- What is redirected (`FS_CheckRealPath`): full mode emulates every `/…`
  path except `/dev*` and `/tmp/launch.sys`. Partial mode emulates only
  `/ticket/0001000{1,5}`, `/title/0001000{0,1,4,5}`, `/tmp` (except
  `/tmp/uid.sys`, kept real so ES renaming it to `/sys/uid.sys` stays on one
  volume), `/import`, `/sys/disc.sys`. Mixed real+emulated renames are
  refused; open/read/write/seek of emulated fds go to FAT (max 20, `MAX_FAT_FD`
  in `plugin.c`); `GetStats` returns faked values; `GetAttr` fakes owner/group
  perms (and `/tmp/*`, `/import/*` as all-RW for ES).

### 1.3 RiftWii's sequence (`d2xnandemu.cpp`)

`IOS_Open("fat")` (fd<0 = not d2x, abort); unmount PPC-side libfat and shut
down libogc's SD driver; `0xF0`, retry once via `0xF1` + 100 ms pause;
on success ioctlv 100 with 2 in-vectors (u32 mode, 256-byte path); on any
failure `0xF1` to hand the card back (a failed `0xF1` means "d2x keeps the
card, stay off it"). Release: ioctlv 100 with mode 0, then `0xF1`. While
emulating, RiftWii touches neither libfat nor the card.

## 2. What is implemented (this branch)

- `Source/Core/Core/IOS/Fat/Fat.h`, `Fat.cpp` (new): `FatDevice`, answers
  0xF0/0xF1 as in 1.1. Mounted state = Dolphin's SD-card setting
  (`MAIN_WII_SD_CARD` inserted or not) plus a `m_card_started` flag: 0xF0
  with no card -> -4, else flag set and 0; 0xF1 answers 1 and clears the
  flag, or -4 when the card was not started. Other ioctls/ioctlvs -> -4. Every call is
  `INFO_LOG_FMT(IOS, …)`. Savestated.
- `IOS.cpp`: `FatDevice("fat")` registered in `AddStaticDevices()` only when
  `IsD2x()`; `OpenDevice` routes the bare `"fat"` path to it (d2x registers a
  non-`/dev/` name; without this it fell into "Unknown device"). On non-d2x
  IOS the device is absent, so `IOS_Open("fat")` still fails (-6) exactly as
  RiftWii probes for.
- `FS/FileSystemProxy.{h,cpp}`: `/dev/fs` ioctlv 100 handled only when
  `IsD2x()` (else -101 as before). Validates vectors (>= 1 in-vector, first
  >= 4 bytes; -6 otherwise), stores mode (OR 0x80 when enabling, as d2x) and
  path (truncated to 255 + NUL in a 256-byte field), logs mode + path at
  INFO, answers 0. Mode without SD/USB bits clears the stored state and
  answers 0. Stored in `FSCore`, so it dies with the IOS instance like d2x's
  `config`, and is savestated.
- ioctlv 100 answers -6 while a title runs, as d2x does (d2x lets ES turn
  the emulation off then; Dolphin's ES never asks).
- d2x hands the folder to its FAT module only the first time the emulation
  is switched on in an IOS session (`FAT_Init` returns early once the module
  is open); a later ioctlv 100 with another folder changes only the FFS
  plugin's copy. RiftWii switches it on once per launch.
- Deliberately not implemented: the redirect itself (see 3), ioctlv 101,
  ioctl 100, USB modes, and d2x's `"$"` relative opens.

## 3. Redirect design

Option A is implemented in `Source/Core/Core/IOS/FS/D2xFileSystem.cpp`: a
file system wrapper installed for a d2x kernel, so `/dev/fs` and ES both
see the emulated paths. Every request flushes the SD device's handle,
mounts the card image with FatFs, does its work and unmounts, so no
cache outlives a request. Tested with RiftWii: Super Mario Bros. (NES VC)
started from `sd:/nands/test` and wrote its save to the card, which was
still there after Dolphin closed and synced the card back to its folder.
Not done: USB, ioctlv 101, ioctl 100, d2x's per-game fixes.

The options as they were weighed:

Dolphin facts used: the SD card is a raw image (`WiiSD-<card>.raw`), rebuilt
from the host sync folder at boot when folder sync is on and synced back on
a clean close (`Core.cpp`, `SyncSDFolderToSDImage` /
`SyncSDImageToSDFolder`); `/dev/sdio/slot0` holds the image file open for
raw-sector game access; the NAND backend is `FS::FileSystem`
(`HostBackend`), whose `SetNandRedirects()` maps Wii-path prefixes to host
paths (`HostBackend/FS.cpp BuildFilename`, prefix + boundary match); FatFs
is linkable with custom disk callbacks (`Common/FatFsUtil.h`,
`RunInFatFsContext`; precedent: `VFFUtil.cpp`).

- Option A — mount the SD image with FatFs inside the FS HLE and redirect
  emulated paths into it. Faithful: real FAT errors, visible to raw-sector
  readers, works with folder sync off. Costs: a second FAT stack next to
  `SDIOSlot0Device`'s open image handle (cache/flush coordination, mount
  lifetime tied to 0xF0/0xF1), FatFs global-state/threading care, and writes
  only persist via image flush. Biggest risk: two writers (game raw sectors
  vs. FAT layer) with no lock.
- Option B — redirect emulated paths to the host SD sync folder and call
  `SetNandRedirects()` on setmode (clear on mode 0). Simple, no contention
  (nothing else writes the folder at runtime), writes persist on the host
  immediately, boot-time folder->image sync carries them onto the card image.
  Partial mode maps to a redirect per emulated prefix (longest-prefix / first-
  match ordering must put `/tmp/uid.sys`… rather, the real paths, first);
  full mode is one `/` redirect plus special-casing `/tmp/launch.sys` (and
  `/dev*`, which never reaches FS) back to NAND — d2x's only two exceptions.
  Costs: needs folder sync enabled to mean anything on the card; raw-sector
  SD readers won't see the files (matches no real game behavior; RiftWii
  itself stays off the card); FAT-specific errors become host-FS errors.
- Review: B loses every save. On a clean close Dolphin renames the sync
  folder to a backup and unpacks the SD image into a new one
  (`Common/FatFsUtil.cpp`, `SyncSDImageToSDFolder`), so files written
  straight into the folder during the session are discarded. A is the way:
  the files live in the card image, where the game, RiftWii's later reads and
  the sync all see them. Its writer hazard is the same as on a Wii (d2x's
  module and anything else using the card), and RiftWii stays off the card
  while the emulation is on. Flush the image between the FatFs layer and
  `SDIOSlot0Device`'s handle at 0xF0 and 0xF1.

## 4. Gaps / follow-ups

- No ioctlv 101 (`GETMODE`), no ioctl 100, no `"$"` relative opens, no USB.
- Savestate stores mode/path and the fat mounted flag; restoring into a
  running session is untested.
- Real-hardware check still needed: 0xF0/0xF1 and setmode answers against
  RiftWii logs (`boot.log`/`autorun.log`, `docs/WIIWARE.md` games).

## 5. Files changed

- `Source/Core/Core/IOS/Fat/Fat.h` (new)
- `Source/Core/Core/IOS/Fat/Fat.cpp` (new)
- `Source/Core/Core/IOS/IOS.cpp` (fat include, d2x-only registration,
  `"fat"` open routing)
- `Source/Core/Core/IOS/FS/FileSystemProxy.h` (d2x mode/path state, ioctlv
  100 number, handler declaration)
- `Source/Core/Core/IOS/FS/FileSystemProxy.cpp` (ioctlv 100 dispatch +
  handler, savestate)
- `Source/Core/Core/CMakeLists.txt` (new files)
- `Source/Core/DolphinLib.props` (new files, MSBuild)
- `RIFTWII-D2X-NAND.md` (this file)
