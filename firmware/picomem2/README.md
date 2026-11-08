# PicoMEM 2 firmware for os8088

A PicoMEM 2.0 firmware built from the newest PUBLIC source, with the fixes
os8088 needs. **The image is not in the tree** (`PM2_11_8_26_OS8088.uf2`,
`SHA256SUMS`): the owner decides whether it joins `firmware/picomem1/`'s
sanctioned binary. Everything needed to rebuild it byte for byte is below.

| | |
|---|---|
| source | Elendilon/ISA-PicoMEM `pm1x-pr`, commit `5e507be` (FreddyVRetro `be7b2e6` + the 1.x fixes), plus `tools/picomem/patches/fw/02-os8088-rev.patch` |
| board | upstream's own CMake defaults, untouched: `PICO_BOARD pimoroni_pico_plus2_rp2350`, `PICOMEM_BOARD "PICOMEM_20"`, 360 MHz, core 1.30 V. **Not one line of the clock, voltage or pin setup differs from upstream** - the fixes' only voltage line is fenced to `PICO_RP2040` |
| base | the card BIOS inside is dated 2026-06-16, the same as **PM2-6-21-26**: the public source is the June release, not October's (whose source was never published) |
| size | FLASH 4,723,180 B of 16 MB, RAM 377,888 of 512 KB. UF2 family RP2350-ARM-S (`e48bff59`) plus the SDK's one absolute block at `0x10FFFF00`, the same layout as PM2-6-21-26 and PM2-10-4-26 |
| sha256 | `3196e4eae8ecee9ed19d94cd6bdf161b6cdf93ffb1a2a70b477c47a7a3ad6241` (two builds, identical) |

## What it changes (SPEC.md 18.91.7.1)

- **The card mixes while it waits for the PC** (`pm_pccmd_wait_hook`) and
  in the SD driver's waits. The stock firmware mixes nothing while the PC
  copies a sector, so an interrupt that lands in the copy is a hole in the
  sound as long as the interrupt.
- **One copy command a run**: an int 13h's sectors are read in runs of 1, 2,
  then 4 into the card, the next run read while the PC copies the last.
- **The disk waits for the PC's copy however long it takes** (the stock one
  gives up after 120 ms and returns wrong bytes with CF clear).
- **BIOS function 3 answers BX = 8088h**, so os8088 knows and stops holding
  IRQ0 and capping the run at eight sectors.

## What it loses against PM2-10-4-26 (the October release)

From upstream's own release notes, October added, so this does not have:
CD-ROM and CD audio emulation; ROM emulation; HDD/floppy images from USB;
**the PicoMEM's interrupt on any IRQ** - June's own notes say "PicoMEM IRQ
is still hard coded, **Jumper on IRQ7 is mandatory**"; **the Sound Blaster's IRQ/DMA settings**
(here the defaults are used); SB ADPCM; ArdSynth MIDI; RTC fixes for
GlaTick; PMDFS fixes and file dates.

## Flashing, carefully

The RP2350's boot ROM cannot be overwritten by a UF2, so a bad image is
always recoverable by flashing another. The steps are upstream's own:

1. **Back up the microSD card first** (a whole-card image). The firmware
   does not touch the card's images, but a config written by October's
   firmware may not read right under June's: if anything is odd after the
   flash, delete `PICOMEM.CFG` and set the card up again.
2. **Check the IRQ jumper is on IRQ 7** (see above).
3. Hold **BOOT** and connect the card's **USB-C** to a PC, as for any
   PicoMEM 2 update. It mounts as `RP2350`. Copy the `.uf2` onto it; it
   reboots when the copy ends.
4. **To go back**: the same, with `PM2-10-4-26.uf2` from upstream's
   `firmware/PicoMEM2/`.

## Rebuilding it

Ubuntu 24.04: `gcc-arm-none-eabi libnewlib-arm-none-eabi
libstdc++-arm-none-eabi-newlib` (13.2.1 20231009), cmake, ninja.

```sh
git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk && git -C pico-sdk submodule update --init
git clone https://github.com/Elendilon/ISA-PicoMEM && cd ISA-PicoMEM && git checkout 5e507be
git submodule update --init src/lib/rp2040-psram
git clone -b rp2040-epx-only https://github.com/wbcbz7/tinyusb src/lib/tinyusb
git -C src/lib/tinyusb checkout a88dc2429
git clone https://github.com/Ryzee119/tusb_xinput src/lib/tusb_xinput
git -C src/lib/tusb_xinput checkout cfd83ba
git apply /path/to/os8088/tools/picomem/patches/fw/02-os8088-rev.patch
git show origin/os8088-pm114:tools/build-linux.sh > /tmp/build-linux.sh
sed -i 's|^REPO="$(cd.*|REPO="$PWD"|' /tmp/build-linux.sh
PICO_SDK_PATH=$PWD/../pico-sdk SOURCE_DATE_EPOCH=1792134658 bash /tmp/build-linux.sh
# build/picomem.uf2
```

No `v1board.patch`: `pm1x-pr`'s own `CMakeLists.txt` selects a PicoMEM 2.
The GMGSx soundfont is in the published tree. The emulator twin
(`tools/picomem/build.sh`, libpmhost v2) is built from the same two patches.
