# How the debug disks were built

*Plan: [docs/plans/DEBUG-DISKS-PLAN.md](plans/DEBUG-DISKS-PLAN.md). Contract: SPEC.md 127.*

## It started with an ESS card that went quiet

A user with an ESS ES1868F reported that Sound Blaster mode is detected, the
mixer and the volume control work, and a MIDI or MOD plays for two or three
seconds before the sound stops while the player's indicators carry on. AdLib
mode is fine.

We read the ES1868 datasheet and Linux's `es18xx` and `sb_ess` drivers, built
86Box in a container and gave it an ES1868 on a Pentium (`vm/pentium-ess`,
with a saved CMOS so drive B exists) and an ES1688 on an XT. MIDIRack's Sound
Blaster arm played ten seconds through at 8,000, 22,050 and 44,100 Hz, on
kern_big and on kern_mega: no underrun, the pitch right. Several things were
ruled out on paper: mixer writes during play, the volume icon reading the
mixer, a `D3h` mid-stream, an MPU acknowledgement left unread, the chip's
power-down timer, the ring wrapping at the wrong length. What was left needs
the real chip: its voice path turned off while the DMA carries on (86Box
treats the ESS `D1h`/`D3h` as no-ops, so it cannot show this), or another
source on the shared audio IRQ line.

That is a question for the reporter's machine, and they have no debugger.
So the owner asked for a disk that shows what the driver found.

## The shape

The owner chose to boot the real OS with a diagnostic program rather than
write a standalone prober. That choice is what makes the disk worth having:
the probes on the screen are the shipped driver's own, because the driver is
the shipped source with one define. `snddiag.inc` is a set of macros that
expand to nothing in a normal build, and a fast row assembles the driver with
every hook deleted and requires the shipped binary byte for byte. One `nop`
in the empty arm turns it red.

The record says how each base answered the reset, how the IRQ was found,
what the stream did and the last 32 bytes sent to the DSP. One more package
verb hands it over. The same verb reads a mixer register several times after
one index write, which is how an ESS gives its part number. It also asks the
DSP's voice switch (`D8h`) while a tone plays, and sends an idle DSP an
identify command (`E7h`), resetting it afterwards.

## Seen on the screen

On 86Box's ES1868 the Detect page reads `mixer 40h x4 = 18 68 08 00 =
ES1868 cfg 0800h  DSP E7h: 68 8B`. The play test shows ten rows of about
21,970 bytes and 11 block IRQs a second, `D8h` = FF throughout, and the ESS
master-volume registers steady. On QEMU's SB16 the same pages show DSP 4.05
and the IRQ read from mixer 80h. That emulator does not answer `D8h`, and
the column says `na`.

## What it costs

Nothing on a shipped machine. The diag `SOUND.DRV` is about 850 bytes
larger. The package is 8 KB. Each disk carries the boot set, the system
packages and Note Pad: 562 of 2,847 clusters at 1.44MB and 273 of 354 at
360KB.
