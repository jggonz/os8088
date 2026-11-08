# PicoMEM 2 firmware for os8088

os8088 plays its video and its Video Battle off a **PicoMEM 2.0**'s disk
with sound only on this firmware: the released ones stop mixing the card's
sound while the PC copies a disk sector, so anything that runs from the
timer interrupt during a read - a video's decode, a battle's sound feed -
cuts holes in the sound as long as itself (docs/reports/PICOMEM2-STUTTER-2026-11-07.md).
The fixes are not upstream yet, so the image is kept here, beside
`firmware/picomem1/` and on the same footing: a **binary in a tree that
otherwise commits none**, and **GPL-2** (`COPYING`), the owner's decision.
Once FreddyVRetro/ISA-PicoMEM ships a release that carries them, this
directory can go.

| | |
|---|---|
| file | `PM2_11_8_26_OS8088.uf2` (`SHA256SUMS`). Upstream's `PM2-M-D-YY` names with `OS8088` added, as `firmware/picomem1/`'s does |
| source | Elendilon/ISA-PicoMEM, branch **`pm-pr`**, commit `6204e0db3601fe0b1e2973d560577b90fbb205f2`: ONE commit on FreddyVRetro/ISA-PicoMEM `be7b2e6`, written to go upstream as it stands. `tools/picomem/patches/fw/01-pm-pr.patch` is the same commit |
| board | upstream's own CMake defaults, untouched: `PICO_BOARD pimoroni_pico_plus2_rp2350`, `PICOMEM_BOARD "PICOMEM_20"`, 360 MHz, core 1.30 V. No line of the clock, voltage or pin setup differs from upstream; the commit's one voltage line is fenced to `PICO_RP2040` |
| base | the card BIOS inside is dated 2026-06-16, as in **PM2-6-21-26**: the public source is the June release. October's (PM2-10-4-26) was never published |
| size | FLASH 4,723,180 B of 16 MB, RAM 377,888 of 512 KB. UF2 family RP2350-ARM-S (`e48bff59`) plus the SDK's one absolute block at `0x10FFFF00`, the layout of both upstream releases |
| field | 5150 #1 (4.77 MHz, CGA, the card's HDD 0), 2026-11-08: VIDSND's tone rows clean in every row; Video Battle's Bolt six times with no dropout. The image tested was built from the same tree with an earlier date: the two differ in 10 bytes, the build date |

## What it changes (SPEC.md 18.91.7.1)

- **The card mixes while it waits for the PC** - in every PC-command wait
  of an int 13h, and in the SD driver's waits.
- **One copy command a run**: an int 13h's sectors are read in runs of 1, 2,
  then 4 into the card, the next run read while the PC copies the last.
- **The disk waits for the PC's copy however long it takes** (the stock one
  gives up after 120 ms and returns wrong bytes with CF clear).
- **BIOS function 3 answers BX = 8088h**, so os8088 knows it may stop
  holding IRQ0 and capping a read at eight sectors.

## What it loses against PM2-10-4-26 (the October release)

From upstream's own release notes, October added, so this does not have:
CD-ROM and CD audio emulation; ROM emulation; HDD/floppy images from USB;
**the PicoMEM's interrupt on any IRQ** - June's notes say "PicoMEM IRQ is
still hard coded, **Jumper on IRQ7 is mandatory**"; **the Sound Blaster's
IRQ/DMA settings** (the defaults are used); SB ADPCM; ArdSynth MIDI; RTC
fixes for GlaTick; PMDFS fixes and file dates.

## Flashing

The RP2350's boot ROM cannot be overwritten by a UF2, so a bad image is
always recovered by flashing another. The steps are upstream's own:

1. **Back up the microSD card** (a whole-card image). The firmware does not
   touch the card's images; if anything is odd after the flash, delete
   `PICOMEM.CFG` and set the card up again.
2. **Check the IRQ jumper is on IRQ 7** (above).
3. Hold **BOOT** and connect the card's **USB-C** to a PC, as for any
   PicoMEM 2 update. It mounts as `RP2350`. Copy the `.uf2` onto it; it
   reboots when the copy ends.
4. **To go back**: the same, with `PM2-10-4-26.uf2` from upstream's
   `firmware/PicoMEM2/`.

## Rebuilding it

The build is **reproducible**: with these pins, two builds give the hash in
`SHA256SUMS`. `SOURCE_DATE_EPOCH` is the commit's own time (`1794139200`),
because the image carries `__DATE__`/`__TIME__`.

- arm-none-eabi-gcc **13.2.1 20231009** (Ubuntu 24.04
  `gcc-arm-none-eabi libnewlib-arm-none-eabi
  libstdc++-arm-none-eabi-newlib`), cmake, ninja.
- Pico SDK **2.1.1** (`bddd20f`) with its submodules.
- `src/lib/rp2040-psram` at its gitlink; TinyUSB **wbcbz7/tinyusb
  `rp2040-epx-only` at `a88dc2429`** and tusb_xinput at **`cfd83ba`**, as
  `firmware/picomem1/` pins them.
- `tools/build-linux.sh` from the fork's `os8088-pm114` branch.

```sh
git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk && git -C pico-sdk submodule update --init
git clone -b pm-pr https://github.com/Elendilon/ISA-PicoMEM && cd ISA-PicoMEM
git submodule update --init src/lib/rp2040-psram
git clone -b rp2040-epx-only https://github.com/wbcbz7/tinyusb src/lib/tinyusb
git -C src/lib/tinyusb checkout a88dc2429
git clone https://github.com/Ryzee119/tusb_xinput src/lib/tusb_xinput
git -C src/lib/tusb_xinput checkout cfd83ba
git show origin/os8088-pm114:tools/build-linux.sh > /tmp/build-linux.sh
sed -i 's|^REPO="$(cd.*|REPO="$PWD"|' /tmp/build-linux.sh
PICO_SDK_PATH=$PWD/../pico-sdk SOURCE_DATE_EPOCH=1794139200 bash /tmp/build-linux.sh
# build/picomem.uf2
```

No board patch: `pm-pr`'s `CMakeLists.txt` is upstream's, which selects a
PicoMEM 2. The GMGSx soundfont is in the published tree. The emulator twin
(`tools/picomem/build.sh`, libpmhost v2) is built from the same commit.

`firmware/picomem1/`'s image is `pm1x-pr` (`5e507be`), which is `pm-pr`
without its BIOS revision word: os8088 reads that word only on a 2.0.
