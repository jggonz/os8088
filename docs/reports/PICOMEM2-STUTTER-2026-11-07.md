# Video Battle's sound stutters on the 5150 + PicoMEM 2: the card goes silent while it waits for the PC

**A measurement, not a description.** Taken 2026-11-07 in the container, on
MartyPC's PicoMEM 2.x twin (`os8088_5150_herc_pm2_gla`: a 4.77 MHz 5150, the
card's firmware compiled for the host as `libpmhost`, its card time the
8088's; docs/TESTING.md, *The PicoMEM, as its own firmware*), at branch
`video-battle-pico2`. Nothing here was measured on the iron yet: the field
half is `make pm2kit` (below). True of that emulator, that firmware source and
that build.

## The report from the iron

The owner moved the PicoMEM 2 into 5150 #1 (CGA; the ST-225 and the Hercules
out for now) to test Video Battle: **the audio stutters; the video, if at
all, not much.** The card runs FreddyVRetro's PM2-10-4-26, whose source is
not published. The newest public PicoMEM 2 source is June 21 2026
(`be7b2e6`, with the ADPCM pull request).

## What the firmware does

Read in `be7b2e6` (the source `libpmhost` builds from):

- **The Sound Blaster's samples are fine.** On a 2.x the DSP takes one byte
  per sample by real DMA off a timer interrupt (`DSP_DMA_EventHandler`,
  `USE_HWDMA`), into a 1,024-sample FIFO, and raises the block IRQ when the
  count runs out. Neither depends on what core 0 is doing.
- **The OUTPUT is not.** The mixer (`pm_audio_domix`) runs on core 0, in the
  main loop and once per sector inside an int 13h, and fills five buffers of
  64 frames at 49,716 Hz: with one playing, **5.1 ms mixed ahead**. When the
  I2S DMA finds no mixed buffer it plays silence.
- **An int 13h waits on the PC with nothing mixing.** Per sector the card
  reads the SD, mixes once, asks the PC's ROM to copy 512 bytes out of its
  window, and spins in `PC_Wait_CMD_End_Disk` until the copy ends (20 ms,
  then 100 ms, then it gives up). The PC copies **with interrupts enabled**
  (the card's int 13h `sti`s), so any interrupt handler on the PC lengthens
  that wait - and Video Battle and Video Player decode frames in the timer's
  hook (SPEC.md 126.3, 98.3).

`pm1x-pr` (Elendilon/ISA-PicoMEM, the 1.x firmware in `firmware/picomem1/`)
already changed exactly this for the 1.x: a wait hook mixes during every PC
command wait of an int 13h, and the SD driver mixes during its own waits.
That commit is board-independent code; `libpmhost`'s v2 build carries it.

## The A/B, in the emulator

VIDSND's new **T** key (tests/vidbench/vidsnd.asm) plays a 634.9 Hz sine
through the card (22,222 Hz, auto-init DMA, a 735-byte block: 30 IRQs a
second) in ten 5 s rows, while a holder burns N ms with interrupts off and
the foreground reads the card's hard disk through int 13h. `tools/vstone.py`
finds the rows by their gaps and counts HOLES (the tone under half its
amplitude) in a recording of them.

Two `libpmhost` v2 builds, identical but for one thing: **mix** is `pm1x-pr`
as shipped; **no mix** is the same with the wait hook's assignment and the
two SD-wait mixes taken out - the stock firmware's behaviour, everything else
equal. The disk is `build/pmhd.img` (16 heads, 63 sectors) as the card's
HDD 0. Holes in 5 s, and the milliseconds silent:

| row | | reads in 5 s | mix | no mix |
|---|---|---:|---|---|
| A | the tone alone | - | 0 | 0 |
| B | the card's ISR holds 16 ms, no disk | - | 0 | 0 |
| C | whole tracks, no hold | 53 tracks | 0 | **787 holes, 980 ms** (1.1 ms each, every ~6 ms) |
| D | tracks, ISR holds 4 ms | 46 | 0 | 696, 1,371 ms |
| E | tracks, holds 8 ms | 40 | 0 | 612, 1,765 ms |
| F | tracks, holds 16 ms | 26 | 0 | 212, 2,431 ms (16 ms each, every ~30 ms) |
| G | one sector a call, holds 16 ms | 1,067 sectors | 0 | 164, 1,778 ms |
| H | as G, the card's line masked per call | 1,996 | 0 | 0 (but 9 IRQs of 151: see below) |
| I | IRQ0 holds 16 ms, one sector a call | 1,527 | 0 | 82, 955 ms |
| J | **as I, IRQ0 masked per call** | 1,415 | 0 | **0** |

Every row's IRQ count was 151 (5 s at 30.2 a second) but H's.

### What it says

- **The stutter is the card's output starving, and the firmware fix cures
  it.** With the wait hook, every row is clean.
- **Without it, the card chops even when NOTHING holds the PC** (row C).
  On an 8088 one sector's round trip - SD, mix, the PC's copy - is about
  6 ms, longer than the 5.1 ms the card mixes ahead: a 1 ms hole every
  sector, for as long as the disk streams. Video Battle streams its music and
  its clips all the time, which matches "the audio stutters, the video
  hardly".
- **A hold that lands in a copy makes the hole the hold's length** (F, G, I):
  the timer hook's decode is such a hold.
- **os8088 can work round it by itself** (J): one sector a call, with IRQ0
  masked at the 8259 for each call. Between calls the card sits in its main
  loop, which mixes, and no hook can lengthen a copy. In this emulator it
  costs the disk rate: 1,996 sectors in 5 s one at a time (H, holds lost)
  against 53 x 63 = 3,339 as tracks - **about 40% less** - and the SD's own
  read time, which `libpmhost` does not model (its SD is instant), still
  passes unmixed on the card.
- **A masked line can LOSE the card's interrupt** (H). The twin's SB is on
  IRQ 5 with only IRQ 7 jumpered, so the SB's interrupt goes through the
  card's multiplexer on 7, and one raised while 5 was masked was dropped:
  9 of 151 arrived. On a jumpered line it is a level held until the DSP is
  read, as a real SB's. Masking IRQ0 (J) lost nothing. **Which lines are
  jumpered on the owner's card decides whether any such mask is safe** -
  ask before designing on it.

### Not done

Video Battle itself was played off the twin's card disk (`os8088_5150_pm2_gla`
and the Hercules twin, the demo bundles): 0 late frames, 0 music underruns,
the sound stream open (`vb_snd` 1, channel 1 not masked) - and **the card's
DAC silent on both firmwares**. VIDSND, programming the DSP itself, sounds on
the same twin. Not run down: it is the twin's or SOUND.DRV's stream on an
emulated 2.x, not the machine's - the owner hears the battle, stuttering.
Until it is, the battle's own A/B is the field's.

## The cure, three ways

1. **Firmware, upstream.** The `pm1x-pr` wait hook is two small hunks in
   `pm_pccmd.cpp`/`pm_cmd.cpp` and two lines in `sd_card_spi.c`; offered to
   FreddyVRetro, it would land in a release that keeps PM2-10-4-26's
   features. The clean route.
2. **Firmware, ours.** `pm1x-pr` builds for a PicoMEM 2 (its CMakeLists
   selects one), but from the June source: it would LOSE what PM2-10-4-26
   added and published no source for - the CD-ROM and CD audio, ROM
   emulation, images off USB, the front panel's joystick and CD player,
   ArdSynth, the PicoMEM's interrupt on any line (`IRQ` in config.txt), the
   RTC fixes for GLaTICK, DFS and file-date fixes, and "SB DSP does not
   answer init end directly". The SB's IRQ/DMA command (77h) IS in the June
   source, which is what SOUND.DRV uses. Untried on a real PicoMEM 2.
3. **os8088, for itself.** J's shape in the kernel's PicoMEM disk path:
   one sector a call, IRQ0 masked for the call. It costs disk rate on every
   PicoMEM machine and cures only the stock firmware's half that we can
   reach; it is the fallback if 1 does not come.

## The field kit: `make pm2kit`

`build/pm2kit/` (and `build/pm2kit-360.img`): VIDSND (T and R), VIDBENCH and
its DAT, VIDDISK, VIDEOD and VBATTLED, with `tests/vidbench/FIELDPM2.TXT` as
the README. The recording of T, run through `tools/vstone.py`, answers on the
iron what the table above answers in the emulator; the rest is the first
measurement of the `5150-picomem2` profile, and VBATTLED's profile
(`tools/vbtprof.py`) is the battle's own account of a stuttering play.

## The ST-225 beside the card

The ST11M is the classic XT controller: **its ROM at C800h**, **ports
320h-323h**, **IRQ 5**, **DMA 3**. Each meets something the card had on 5150
#2:
- the ROM: the card's BIOS defaults to C800h too - move the card's to
  **D000h** in its config.txt (and its EMS frame off D000h, to E000h);
- the ports: the card's NE2000 was at **320h** on 5150 #2 - move it to
  300h or 340h (ETHER.DRV probes 300h, 320h, 340h ... itself);
- IRQ 5: the SB must not share it. SPEC.md 34.5.6.1: an ST11 on its BIOS
  v2.0 wipes a block interrupt pending on its line once per int 13h, which
  ends a play part way. Move the SB off 5, onto a JUMPERED line (a line
  that is not goes through the card's multiplexer, row H above): 3 with the
  NE2000 elsewhere, or 7 if the card's own interrupt can move (`IRQ` in
  config.txt, PM2-10-4-26 on);
- with the ST-225 as 80h, the card's own disk image is 81h.
