# d2x in Dolphin, for RiftWii's tests

RiftWii starts games on a Wii through a d2x cIOS: a custom IOS that adds a
few commands to the Wii's own. Dolphin doesn't run IOS code; it answers
every IOS request itself (HLE). This branch teaches it to answer d2x's
extra commands too, so the parts of RiftWii that only run on d2x can be
tested here instead of only on real consoles.

It emulates d2x as RiftWii uses it, from d2x's public source (GPL) and
RiftWii's own logs. It is not d2x: real-console quirks (an SD card d2x
can't mount, timing) still need a Wii. Tests on hardware become a check
of what already works here, instead of the first test.

## What RiftWii uses, and the plan

| Step | What | d2x's interface | Status |
| --- | --- | --- | --- |
| 1 | A d2x cIOS in a slot (249, 250...) | The slot's IOS title, and d2x's version block in its NAND content, which RiftWii reads to recognise d2x | Works: RiftWii finds the slot, lists it as installed, reloads into it (Dolphin logs "IOS249 is d2x on base IOS56") |
| 2 | NAND emulation from the SD card: emulated NANDs, game saves on the card | `/dev/fs` ioctlv 100 sets the mode and the folder; the `fat` module mounts and unmounts the card (ioctlv 0xF0 / 0xF1) | Works: NAND requests go into the SD card image (`IOS/FS/D2xFileSystem.cpp`). RiftWii starts Super Mario Bros. (NES VC) from `sd:/nands/test` and its save is written to the card. Details in RIFTWII-D2X-NAND.md |
| 3 | Keeping the cIOS across a game's IOS reload; going back to RiftWii | ES ioctlv 0xA0 (reload block) and 0xA1 (return to) | Coded; RiftWii's 0xA0 calls are answered. NAND emulation now carries across the redirected reload, and so do the disc device's fragment mode and no-reset setting. A game's reload still to test |
| 4 | Game images on the SD card or a USB drive | The disc drive's fragment lists, USB storage, `/dev/sdio/sdhc`, `/dev/usb2` | Works from the SD card and from a USB drive: Deca Sports 2 (RVZ and WBFS on SD, WBFS on USB) starts through IOS249. The USB drive is a disk image set with `Dolphin.General.WiiUSBPath`: an emulated USB mass-storage device on `/dev/usb/ven` for RiftWii's menu, and d2x's `/dev/usb2` and fragment lists on the same image (`IOS/USB/Emulated/MassStorage.cpp`, `IOS/USB/USBStorage.cpp`, `IOS/USB/D2xUSB.cpp`, `DiscIO/D2xBlob.cpp`, `IOS/DI/DI.cpp`, `IOS/SDIO/D2xSDHC.cpp`) |

Steps 1 to 3 cover every WiiWare and Virtual Console test that needs a
Wii today: channels from an emulated NAND, the retry ticket, skipping a
cIOS that couldn't mount the card, and d2x saves for disc games.

## How it's checked

- Each step comes with a RiftWii test run: the 14 test games of
  RiftWii's `docs/WIIWARE.md`, started through RiftWii, from the Wii's own
  NAND (as today) and from an emulated NAND on the SD card.
- RiftWii's logs (`boot.log`, `autorun.log`) must show the same steps a
  real Wii's do, where we have real logs to compare.

## Savestates

A savestate keeps what brings a d2x game's disc back: which image
the game is read from (SD or USB), the fragment list and the game
ID. Loading opens that image as it is now and rebuilds the disc
before Dolphin checks for an inserted disc
(`Source/Core/Core/HW/DVD/DVDInterface.cpp`).

- A state from another game ID is refused first with `State belongs
  to a different game (ID ...)`, so RiftWii's menu cannot load a
  game's state.
- If the image is missing, or the rebuilt disc is another game, the
  load is refused with `The d2x game could not be restored`, and
  Dolphin undoes it.
- For each SD or USB card d2x's NAND emulation had mounted, the
  state also records the image's path, size and a checksum of its
  partition table and boot sector; loading when the image is
  missing, or its path, size or those sectors differ, is refused
  with `This savestate cannot be loaded because the d2x SD image is
  missing or has changed` (USB for the USB drive), before any d2x
  state is used (`Source/Core/Core/IOS/FS/D2xFileSystem.cpp`). The
  files on the card aren't compared, so a card whose files changed
  still loads.
- The emulated USB drive records its image's path, size and a
  fingerprint of its first sectors; a state saved mid-command
  against a changed image is refused with `This savestate cannot
  be loaded because the USB image has changed while a USB command
  was in progress`, while an idle drive is simply reset
  (`Source/Core/Core/IOS/USB/USBStorage.cpp`).
- A state saved when one side is d2x and the other isn't, or when
  both differ in IOS features while one of them is d2x, is refused
  with `This savestate was saved under a different IOS (d2x or IOS
  features differ)` because a load does not rebuild the IOS file
  system and devices (`Source/Core/Core/IOS/IOS.cpp`).
- Like any Dolphin savestate, one made with a different savestate
  version (`STATE_VERSION` in `Source/Core/Core/State.cpp`) can't
  be loaded.

With `Automatically Sync with Folder` on (`Config > Wii > SD
Card`), Dolphin builds a fresh SD image at every start. The game's
file can land elsewhere on the new image, and if it does, the saved
fragment list misses it and a d2x game's state from before the
restart is refused. A rebuilt card also gets a new boot sector,
because its serial number comes from the clock (except in
Dolphin's deterministic mode, used for netplay and movie
recording), so a state made while d2x had the SD card mounted is
refused after a restart with sync on. Turn sync off before the next
start of Dolphin to keep those states. Once the image has been
rebuilt, a state from before needs the old image back, or a new
state. Tested on a Mac with Mario Kart Wii, 27 October 2026.

## Building

As the `es-identify` branch: Visual Studio 2022, `Source/dolphin-emu.sln`,
Release x64. RiftWii's `docs/HARNESS.md` says how to run it.
