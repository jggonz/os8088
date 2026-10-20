# PicoMEM 1.x firmware for os8088

os8088 needs this firmware to run on a **PicoMEM 1.x** (the RP2040 board with a
Pico W). Released 1.x firmwares up to `PM_W_11_16_25` lack the fixes below,
and without them the card's Sound Blaster crackles or stays silent under
os8088, and a disk read starves it (docs/FIELD-NOTES.md 66). They also
drop disk sectors with no error whenever the PC is busy for more than
120 ms during a copy, which an os8088 video does to a 286. The fixes are
not upstream yet, so the image is kept here. Once FreddyVRetro/ISA-PicoMEM
ships a release that carries them, this directory can go.

It is a **binary in a tree that otherwise commits none**, and **GPL-2 in a
tree that is otherwise MIT**. Both are the owner's decision (2026-10-15), on
the same footing as `apps/c64/rom/` and `apps/apple2/`. `COPYING` is the
firmware's licence.

| | |
|---|---|
| file | `PM_W_10_16_26_OS8088.uf2` (`SHA256SUMS`). The name follows upstream's `PM_x_M_D_Y_x` (`firmware/README.md`): `W` for the Pico W, the firmware's date with a two-digit year (Oct 16 2026, the source commit and the `__DATE__` built into it), and `OS8088` in the variant slot that `FR` and `MD` use upstream |
| source | Elendilon/ISA-PicoMEM, branch `pm1x-pr`, commit `5e507be2cd88e7dd1e4349e5e00d828b2837694c`: one commit on FreddyVRetro/ISA-PicoMEM `main` at `be7b2e6`. It is `pm1x-sb-fixes` (`338a2ae`, the Oct 12 image this replaced) with the disk wait below squashed in |
| board | `PICO_BOARD pico_w`, `PICOMEM_BOARD "PICOMEM"`. This is `v1board.patch`, the only change to the tree. The branch's own `CMakeLists.txt` selects a PicoMEM 2 |
| size | FLASH 542,912 B, RAM 210,132 of 256 KB |
| `picomem.bin` | `81fe08b5b74cb944c947c1b5d87c605deabf40c76058d2cfb4c939c4f3117da8` (the same image as raw bytes; not shipped) |

## What it fixes

The commit message in the source has the details. In short:

- **The 1.x boots at all.** `CYW43_PIO_CLOCK_DIV_INT=4` and the RP2040's
  1.20 V are restored; the PicoMEM 2 source had dropped both, and without
  them the LED never lights and the PC hangs after the BIOS banner.
- **A DMA cycle is no longer decoded as an 8237 register write.** The AEN
  address now has A10 set.
- **The Sound Blaster keeps playing through disk access.** The SB worker
  runs in the disk loop, in the PC-command waits of an int 13h and in the
  SD driver's waits.
- **SB DMA buffering:** copies are 512 bytes, the FIFO is 2,048 samples, and
  playback starts at 1,024. A sound shorter than that plays when its DMA
  ends.
- **The block IRQ fires once the block has been read**, not when a timer
  runs out. When the PC programs the DMA again, the leftover end of the last
  copy is dropped. Getting either of these wrong was the crackles.
- **Multi-sector disk read:** sectors are read in runs of 1, 2, then 4 while
  the PC copies the previous run. 685 → 755 KB/s on a 16 MHz 286
  (docs/reports/VIDDISK-PICOMEM1-286-MULTI-2026-10-12.md).
- **The disk waits for the PC's copy however long it takes.** The PC does
  the copy with interrupts enabled. The card used to give it 20 + 100 ms,
  then carry on without it, and the int 13h returned wrong bytes with CF
  clear. A video drawing from IRQ0 holds a 286 that long. This was the
  Video Player's freezes and "damaged frame" stops on the Victory 286, at
  points that moved with the wait states. The Oct 12 image read wrong at a
  99% hook in VIDDISK's V test and right at every share after the change,
  at the same rates, and the plays that broke run to the end
  (2026-10-16).

## Flashing

1. Hold **BOOTSEL** on the Pico W and connect it over USB. It mounts as
   `RPI-RP2`.
2. Copy the `.uf2` onto that drive. The Pico reboots when the copy finishes.
3. Keep a **1.1 board** (not 1.11) in a powered PC while you do this
   (docs/FIELD-NOTES.md 66).
4. If the card is coming **from the November 2025 firmware or older**, delete
   `PICOMEM.CFG` on its SD card and enter the settings again. The old file is
   not read correctly, and the first thing that breaks is the NE2000 (no card
   at its port).

## Rebuilding it

The build is **reproducible**: with these pins, the result matches the
`SHA256SUMS` byte for byte. `SOURCE_DATE_EPOCH` is the commit's own time,
because `pico_rtc.cpp` seeds the card's clock from `__DATE__`/`__TIME__`. The
image carries `Oct 16 2026 07:10:58`.

- arm-none-eabi-gcc **13.2.1 20231009** (Ubuntu 24.04
  `gcc-arm-none-eabi libnewlib-arm-none-eabi
  libstdc++-arm-none-eabi-newlib`), cmake 3.28.3, ninja.
- Pico SDK **2.1.1** (`bddd20f`), with `lib/lwip` and `lib/cyw43-driver`
  (`c1075d4`).
- `src/lib/rp2040-psram` at its gitlink, `7786c93`.
- TinyUSB **wbcbz7/tinyusb `rp2040-epx-only` at `a88dc2429`**, and
  tusb_xinput at **`cfd83ba`**. The repository records neither revision.
- `src/audio_devices/midi/4MBGMGSMT.h`, the four-byte stand-in from
  `os8088-pm114`. The published source does not include it, and nothing
  loads it below PicoMEM 1.5.
- `tools/build-linux.sh` from `os8088-pm114`. It works on a case-sensitive
  filesystem.

```sh
git clone https://github.com/Elendilon/ISA-PicoMEM && cd ISA-PicoMEM
git checkout 5e507be && git submodule update --init src/lib/rp2040-psram
git clone -b rp2040-epx-only https://github.com/wbcbz7/tinyusb src/lib/tinyusb
git -C src/lib/tinyusb checkout a88dc2429
git clone https://github.com/Ryzee119/tusb_xinput src/lib/tusb_xinput
git -C src/lib/tusb_xinput checkout cfd83ba
git apply /path/to/os8088/firmware/picomem1/v1board.patch
git show origin/os8088-pm114:src/audio_devices/midi/4MBGMGSMT.h \
    > src/audio_devices/midi/4MBGMGSMT.h
git show origin/os8088-pm114:tools/build-linux.sh > /tmp/build-linux.sh
sed -i 's|^REPO="$(cd.*|REPO="$PWD"|' /tmp/build-linux.sh
PICO_SDK_PATH=/path/to/pico-sdk-2.1.1 SOURCE_DATE_EPOCH=1792134658 \
    STAGE_DIR=/tmp/picomem-src bash /tmp/build-linux.sh
# build/picomem.uf2
```

This recipe was run as written in a fresh clone (2026-10-15) and produced
the Oct 12 image's hashes. On 2026-10-16 the same pins, with
`SOURCE_DATE_EPOCH=1791841834` and `338a2ae`, rebuilt that image byte for
byte, and then produced the hashes in `SHA256SUMS` and the table above from
`5e507be`. Those builds used other clone and stage paths, so neither path is
part of the result.

## The debug branch builds the same firmware

`os8088-pm114` (`8ab70ed`) is `338a2ae` with the 1.14 build setup, the field
diagnostics behind `OS88_DIAG` (default 0) and the disk wait on top. Built
with `-DOS88_DIAG=0` and the same pins, it produces **this image except for
five `__LINE__` constants** (measured 2026-10-16, five single bytes each 24
higher) in `sd_card_spi.c`'s debug strings, which move by 24
because of the diagnostic lines above them. The note that said four counted
one short. To check: pad the PR branch's
`sd_card_spi.c` with blank lines to match the debug file's line numbers.
The two `.bin` and `.uf2` files then hash identically. That was shown for
the pair before the disk wait, `056c4d5` against `338a2ae` (`58027b02…` /
`fdcf84b2…` at `SOURCE_DATE_EPOCH=1760400000`). The disk wait put the same
one-line body in both, and the five bytes above are the only difference
between the new pair.

Before 2026-10-15 there was a second difference.
`sd_card_spi_init` compiled 4 bytes shorter on the debug branch. That came
from an inline `sd_wait_mix()` wrapper, which did nothing when the knob was
off but still changed GCC's code generation. `os8088-pm114` `056c4d5`
replaced it with a macro when the knob is off.

## Status

**Flashed, boots and runs on the owner's PicoMEM 1.14 in the Victory 286
(2026-10-15).** The Sound Blaster and USB mouse both work on that card with
these fixes (docs/FIELD-NOTES.md 66). **The disk wait was flashed on the
same card on 2026-10-16**: the Video Player files that froze or stopped on a
damaged frame play to the end. That flash was the same source built with a
different `__TIME__`, which is the only difference from this image.
