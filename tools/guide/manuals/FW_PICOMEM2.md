# PicoMEM 2 firmware

The PicoMEM is an ISA card built around a Raspberry Pi Pico. It gives an old
PC extra memory, a Sound Blaster, a network card, a USB mouse and disks
stored on an SD card. This firmware is for the **PicoMEM 2.0**.

The PicoMEM 2's released firmware stops mixing the card's sound for a moment
every time the PC copies a sector off the card's disk. A video or a Video
Battle played off that disk keeps the PC busy during those copies, and the
sound breaks up into short, repeated gaps. This build keeps the sound
playing through every disk read. Use it until the PicoMEM project ships a
release with the same fix.

## What it fixes

- **Sound keeps playing while the disk works.** No gaps in the Sound
  Blaster's output during disk access, however busy the PC is.
- **Disk reads move several sectors at a time**, the next ones read while
  the PC copies the last.
- **Disk reads are never wrong.** When the PC is busy for a long time, the
  card waits for it instead of carrying on.
- **os8088 can tell it is there**, and reads the card's disk in longer runs
  without holding up the rest of the machine.

## What it does not have

It is built from the newest firmware source that is published, the June
2026 release. The October 2026 release added things this one lacks:

- **The card's interrupt is fixed on IRQ 7.** Put the card's IRQ jumper on
  **IRQ 7** before you flash it.
- The Sound Blaster uses its default IRQ and DMA, whatever its settings say.
- No CD-ROM or ROM emulation, and no disk images from USB.

## Installing it

1. Make a copy of the card's whole SD card first.
2. Check the card's IRQ jumper is on **IRQ 7**.
3. Hold the **BOOT** button on the card and connect its **USB-C** port to
   your computer. It appears as a drive called `RP2350`.
4. Copy `PM2_11_8_26_OS8088.uf2` onto that drive. The card restarts by
   itself when the copy finishes.
5. If anything behaves oddly afterwards, delete `PICOMEM.CFG` from the SD
   card and enter your settings again.

To go back, do the same with the PicoMEM project's own firmware file.

> To check the file is intact before you flash it, compare it with
> `SHA256SUMS`. Most systems can show a file's SHA-256 with a built-in
> command, such as `shasum -a 256` on a Mac or `certutil -hashfile` on
> Windows.

## Using the card with os8088

Once the firmware is on, turn on what you want on the Control Panel's
**Drivers** page: **Sound** ([[SOUND.DRV]]) for the Sound Blaster, **Ethernet**
([[ETHER.DRV]]) for the network card, **USB Mouse** ([[USBMOUSE.DRV]]) for a
mouse plugged into the card, and **EMS** ([[EMS.DRV]]) for its expanded
memory.

## The files

| File | What it is |
|---|---|
| `PM2_11_8_26_OS8088.uf2` | The firmware to copy onto the card |
| `README.md` | The full notes, including how to rebuild the firmware from source |
| `SHA256SUMS` | Checksums of the firmware |
| `COPYING` | The firmware's licence, the GNU General Public License version 2 |

## Good to know

- This is only for the **PicoMEM 2.0**. A **1.x** card with a Pico W takes
  the PicoMEM 1.x firmware beside this one.
- It has been flashed and used on a PicoMEM 2.0 in a 4.77 MHz IBM 5150,
  where Video Battle and a Sound Blaster tone test play off the card's disk
  with no gaps.
