# SPEAKER-PCM-HANDOFF - PC speaker PCM for Tracker and Audio

**Status: HANDOFF, nothing built for either package.** Written 2026-09-27 at
the end of the wave that built the mechanism for the Video Player
(`video-player` branch, commit 63c9018c). Everything this document calls
BUILT is in that tree; everything it calls a question is the next
session's, and several are the owner's to answer before any code.

The owner's brief for the mechanism (2026-09-27): *"PC Speaker PCM playback.
This should be generic, not just for video player. If it is going to cost
more than ~400b in the kernel then it can be a library rather than a call,
used per app like our UI libraries. Audio, Tracker and Video Player would be
the current consumers."* ModPlug was in that list and is RETIRED (SPEC.md
56.15), so it needs nothing.

Read these first, in this order: **SPEC.md 34.11** (the contract and every
measured cost), **SPEC.md 98.3.15** (the one consumer, as a worked
example), **SPEC.md 98.1.1.3** (samples stored as counts), and
`apps/os88spk.inc`'s header.

---

## 1. What exists

A sampled sound on a machine with no card is §34.4's pulse-width trick:
channel 2 in mode 0, one count written per sample, so the cone's average
position follows the wave. That needs an interrupt EVERY SAMPLE, which is
the machine's IRQ0, which only a package's own `FSXF_RATE` bracket may have
(SPEC.md 53.2.2). So it is two halves:

| piece | where | what it does |
|---|---|---|
| **the door**, `OSAPI_FSX_SPK` (slot `0x045B`) | `kernel/snd.inc` `osapi_fsx_spk`, `spk_off` | inside the CALLER'S OWN `FSXF_RATE` bracket: channel 0 at the sample rate, channel 2 as the PWM, IRQ0's vector pointed straight at the package's ISR; K (samples a rate period) and the chain written into a 6-byte block in the caller's image before the vector is armed. AL=1 closes; `fsx_restore` closes it on every way out of a bracket. **kern_big +314 bytes, kern_small +11** (a refusing stub) |
| **the library**, `apps/os88spk.inc` | the including package | `os88spk_init` (ring segment and size code, rate: builds the count table), `os88spk_go` (open the door, play from CONS), `os88spk_stop` (close where it is, CONS exact). The ISR: one count out and one counter a sample; a grant runs to the soonest of period end / data end / ring end; nothing queued plays the table's middle (silence) uncounted. ~480 bytes of the package |
| **the ring** | the package's claim | §34.5.3's EXTERNAL RING layout - RL bytes at seg:0 (RL = 4096 << size code), the producer's TOTAL at RL, the player's CONS at RL+2 - so a package that already feeds a Sound Blaster this way feeds the speaker the same way. **It holds PWM COUNTS, not samples**: the table is at RL + `SND_EXT_TAB` (16), 256 bytes, t[s] = 1 + s(N-2)/255, N = 1,193,182 / rate. The claim is RL + 272 |
| **the kernel's behaviour under a sample ISR** | `kernel/sched.inc` `.rate`/`.rtk`, `kernel/fsx.inc` `fsx_wait` | the rate hook is entered with IF = 1; a rate period makes NO task switch (switches ride the 18.2 Hz tick); `fsx_wait`'s frame wait `hlt`s instead of yielding (SPEC.md 34.11.3) |
| **the consumer** | `apps/video/video.asm` | `vp_sstart .spk` (the decision), `vp_aput` (translate or copy into the ring), `vp_sclose` / `vp_upaus` / `vp_fseek` (stop and go across pauses, swaps, seeks), `vp_poll .spk` (S in the full screen), `vp_spkinfo` (the info line) |
| **the gate** | `tests/vidspk.py`, rows `vidspk*` | traces 800 writes to port 42h mid-play and demands they be the clip's samples through the table, IN ORDER; the lost share; the play's time; the kernel left clean. Legs `--full`, `--fs-off`, `--silent`, `--counts`, `--rate`, `--route-spk`, `--cp-spk`. Instruments: `VIDSPK_ISR=1` (the fast path's cycles), `VIDSPK_APUT=1` (the producer's cycles a byte), `VIDSPK_ATTR=1` (every lost pulse named by the code the late one interrupted) |

## 2. The numbers (MartyPC's 4.77 MHz 5150; SPEC.md 34.11.4)

- **~400 cycles a pulse**: the ISR's fast path is 341-344 cycles entry to
  `iret`, and the 8088 takes ~60 to acknowledge the interrupt.
- **At 5,512 Hz the speaker is ~48% of the machine** fed counts that were
  made on the host, ~52% translating samples in the producer (`xlatb`, ~50
  cycles a byte of its own against ~15 for a plain copy).
- **Lost pulses 1.8-2.3%**: the rate period's own entry costs ~one pulse
  (grant + jump + `sch_isr` up to its EOI, ~1,300 cycles against the 864 a
  sample lasts at N = 216). Nobody has listened on the 5150 yet.
- **The ceiling on an 8088 is 8,000 Hz** (`VP_SPKMAX`): at 11,025 Hz a pulse
  is due every 432 cycles, the ISR takes ~400, and a 4 s clip played for 20.
  The Video Player plays silent above it on `CPU_8086` and says so.

## 3. What the wave learned the hard way - read before writing a line

1. **Any stretch at IF = 0 longer than one sample is a LOST PULSE**, and at
   5,512 Hz a sample is 864 cycles. The 8259 latches one pending IRQ0 and no
   more. The first build lost 9.0%. The fixes were all "hold IF = 0 for less":
   the hook runs with IF = 1, rate periods do not switch tasks, the frame
   wait `hlt`s, and the Video Player's thumb mover went from a `cli` around
   the whole move to one around each byte. **A `pushf`/`cli` section in your
   package that is fine at 18.2 Hz is a defect at 5.5 kHz**: find them with
   `VIDSPK_ATTR` rather than by reading.
2. **The ISR is entered on WHATEVER STACK IRQ0 HIT**, with the interrupt
   frame alone. Keep it that way: the library pushes three registers on the
   fast path and a few more at an event.
3. **The bracket's end closes the door.** A session that crosses brackets (a
   pause, a swap between window and full screen, a seek) must call
   `os88spk_go` again in the next one. `os88spk_stop` then `os88spk_go`
   resumes exactly where CONS says.
4. **`OSAPI_*` cells are FAR and end in `retf`.** Reach one with `call`,
   never `jmp` - a tail `jmp OSAPI_SND_STREAM` in the Video Player's new close
   routine broke every Sound Blaster play (the card path's close ate a
   return address) while every speaker test passed. The soak's card rows
   found it, not the speaker gate.
5. **The PC speaker and a card are chosen by the user, not by the hardware.**
   The Control Panel's Sound page sets `[snd_route]`; picking PC Speaker
   makes `SOUND.DRV` drop its DSP tier, so `OSAPI_SND_CAPS` loses
   `SND_CAP_PCM_BG`. **Test `SND_CAP_PCM_BG`, never "is a driver loaded"** -
   that one test is what makes the route obeyed (rows `vidspkroute` and
   `vidspkcp`).
6. **Auto-EOI was measured and refused** (SPEC.md 34.11.6): 16 cycles a pulse
   saved, MORE pulses lost (pulses entered the ROM's tick handler, onto the
   128-byte chain stack), and the controller reprogrammed for everyone. Do
   not re-try it without reading that section.
7. **A clip can carry the counts** (SPEC.md 98.1.1.3): the producer then
   copies instead of translating, ~7% of the machine back. A Tracker mixer
   cannot use that - it makes its samples live - but it can build the
   translation into the mixer's final clamp, which it already has.

## 4. Tracker (SPEC.md 45)

**Where it stands.** With no Sound Blaster Tracker is a VIEWER: the
`SND_CAP_PCM_BG` test in `trk_ring_probe` (and `trk_play`'s own gate) means
no ring and no sound. With a card, its mixer worker (`mp_gen`, unsigned 8-bit
mono, up to 2,048 bytes a call) feeds `SOUND.DRV`'s stream. Its full screen
(`trk_fs_enter`) is an fsx bracket with **`FSXF_KEEPWORKER |
FSXF_FASTTICK`** - the mixer keeps running through the freeze, the quantum is
18 ms.

**What adopting the speaker means**, and none of it is built:
- **Full screen only.** The windowed player stays silent without a card
  (channel 0 is the desktop's, SPEC.md 34.1).
- **A different bracket.** The door needs `FSXF_RATE` (a divisor in DX, a
  hook in DI - SPEC.md 53.2.2), and `fsx_run` REFUSES `FSXF_RATE` with
  `FSXF_FASTTICK` (one channel 0). So a speaker play is `FSXF_KEEPWORKER |
  FSXF_RATE`, and the FASTTICK quantum is gone in it - under a sample ISR
  task switches ride the tick anyway (34.11.3). Whether the text screen's
  scroll (SPEC.md 45.13, 45.16, `FSXW_FRAME`) still keeps its pace at an
  18.2 Hz switch is the first thing to measure.
- **A rate row the speaker can play.** Tracker's lowest is `TRK_RATE`
  11,000 Hz, past the 8088's 8,000 ceiling. A speaker row at ~5,512 Hz (N =
  216) is the obvious one; 8,000 is the ceiling.
- **The mixer writes counts.** The ring layout is the same as the card's
  external ring, but its bytes are table[sample]. The cheapest place is the
  mixer's own output stage (a `xlatb` there against the table
  `os88spk_init` built) rather than a second pass.
- **The worker runs on tick switches only** while the ISR plays. At 5,512
  Hz a tick is ~303 samples, so the ring must hold several ticks of
  lead - the library does not care how big the ring is (4 KB << code).
- **The budget is the question nobody has answered.** 48% of an 8088 is the
  speaker's; Tracker's mixer at 11 kHz on a card already costs a large share
  of an XT (PERFORMANCE.md's Tracker sets). Measure `mp_gen`'s cycles a
  byte at the speaker row BEFORE designing - if speaker plus mixer exceeds
  the machine, the answer is a refusal with a reason (SPEC.md 47), not a
  slow song.
- **Choosing silence**: the Video Player's S key (98.3.15) is the precedent
  for "the speaker costs too much here, give me the machine back".

## 5. Audio (SPEC.md 86)

**Its premise is the problem.** AUDIO.O88 is *"lightweight background music
for the IBM XT"* that *"keeps playing while Sheet, Note Pad or anything else
has the focus"* (86.1). The speaker needs a full-screen bracket that OWNS the
machine. So the speaker in Audio is not a port, it is a second mode with the
opposite premise - **the owner decides whether Audio gets one at all**:
- **(a) No.** Audio stays card-only; with no card it already says so -
  `ap_entry` banks `SND_CAP_PCM_BG` as `[ap_have_sb]` and `ap_open_track`
  refuses with `ap_s_nosb`. Zero work.
- **(b) A full-screen "listen" mode.** A bracket owning the machine while
  a WAV plays through the speaker, the desktop gone for the duration.
  Then: the bracket body is the UI task, so it may read the file itself (a
  worker may not touch a file, SPEC.md 20.6 rule 7, and Audio's disk refill
  is a UI-task service today); WAV rates above 8,000 Hz must be refused or
  resampled on an 8088; IMA ADPCM decoding costs CPU on top of the
  speaker's 48% - measure it.

## 6. Questions for the owner, before any code

1. Tracker: a speaker rate row in the full screen - worth it if the budget
   measurement (4) comes back tight?
2. Audio: (a) or (b)?
3. Either: is a speaker play something the user turns ON, or the default
   with no card and S/Space to turn it off, as the Video Player does?

## 7. How to test it

`tests/vidspk.py` is the shape to copy: a clip (or a module) whose samples
are known, the card-less `os8088_5150_herc_hdd_gla`, a `bp_trace` on
`osapi_fsx_spk`'s open that switches to an `io` breakpoint on port 0x42 for
800 writes and then to `spk_off`. The writes must be the expected counts IN
ORDER - that one check caught every defect the wave had. Break it on purpose
(no `out 0x42`; no translation) and watch it go red.
