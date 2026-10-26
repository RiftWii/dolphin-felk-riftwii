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
| 3 | Keeping the cIOS across a game's IOS reload; going back to RiftWii | ES ioctlv 0xA0 (reload block) and 0xA1 (return to) | Coded; RiftWii's 0xA0 calls are answered. A game's reload still to test |
| 4 | Game images on the SD card or a USB drive | The disc drive's fragment lists, USB storage, `/dev/sdio/sdhc` | Later, if worth it. An SD launch now stops at `/dev/di` ioctls 0xFA, 0xF9 and 0xF6, unknown to Dolphin |

Steps 1 to 3 cover every WiiWare and Virtual Console test that needs a
Wii today: channels from an emulated NAND, the retry ticket, skipping a
cIOS that couldn't mount the card, and d2x saves for disc games.

## How it's checked

- Each step comes with a RiftWii test run: the 14 test games of
  RiftWii's `docs/WIIWARE.md`, started through RiftWii, from the Wii's own
  NAND (as today) and from an emulated NAND on the SD card.
- RiftWii's logs (`boot.log`, `autorun.log`) must show the same steps a
  real Wii's do, where we have real logs to compare.

## Building

As the `es-identify` branch: Visual Studio 2022, `Source/dolphin-emu.sln`,
Release x64. RiftWii's `docs/HARNESS.md` says how to run it.
