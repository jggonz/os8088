# PicoMEM 1.x firmware

The PicoMEM is an ISA card built around a Raspberry Pi Pico. It gives an old
PC extra memory, a Sound Blaster, a network card, a USB mouse and disks
stored on an SD card. This firmware is for the **PicoMEM 1.x**, the version
with a **Pico W**.

The firmware released for the 1.x up to November 2025 does not work well
with os8088. Its Sound Blaster crackles or stays silent, and a disk read
during a busy moment can hand back the wrong data with no error. This build
fixes both. Use it until the PicoMEM project ships a release with the same
fixes.

## What it fixes

- **The card starts.** Without these settings the Pico W's light never comes
  on and the PC stops after the BIOS banner.
- **Sound keeps playing while the disk works.** The Sound Blaster no longer
  goes quiet during disk access, and the crackles are gone.
- **Short sounds play.** A sound shorter than the card's buffer plays as soon
  as it ends.
- **Disk reads are faster**, about 10% on a 16 MHz 286.
- **Disk reads are never wrong.** When the PC is busy for a long time, the
  card now waits for it instead of carrying on. This was the cause of videos
  freezing or stopping on a "damaged frame".

## Installing it

1. Put the PicoMEM 1.x in a PC and switch the PC on. Keep it powered while
   you flash the card.
2. Hold the **BOOTSEL** button on the Pico W and connect the Pico to your
   computer over USB. It appears as a drive called `RPI-RP2`.
3. Copy `PM_W_10_16_26_OS8088.uf2` onto that drive. The Pico restarts by
   itself when the copy finishes.
4. If the card was running the **November 2025 firmware or older**, delete
   `PICOMEM.CFG` from the card's SD card and enter your settings again. The
   old settings file is not read correctly, and the network card is the
   first thing to stop working.

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
| `PM_W_10_16_26_OS8088.uf2` | The firmware to copy onto the Pico W |
| `README.md` | The full notes, including how to rebuild the firmware from source |
| `SHA256SUMS` | Checksums of the firmware |
| `COPYING` | The firmware's licence, the GNU General Public License version 2 |
| `v1board.patch` | The one change made to the source to build it for the 1.x board |

## Good to know

- This is only for the **1.x** card with a **Pico W**. A PicoMEM 2 needs
  its own firmware.
- It has been flashed and used on a PicoMEM 1.14 in a 16 MHz 286, where the
  Sound Blaster, the USB mouse and video playback from disk all work.
