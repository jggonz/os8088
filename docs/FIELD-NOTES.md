# Field notes — what real hardware found that the harness did not

Bugs reported off period machines (docs/FIELD-MACHINES.md says which) and off
period-accurate emulators, one entry a report. An **OPEN** entry carries the
symptom, the machine, what has been ruled out and on what evidence, and what
to try next. A **CLOSED** entry is a paragraph naming the fix and the SPEC.md
section that owns it, kept because other files cite the number; numbers
missing from the sequence were closed with nothing left to keep, and `git log`
has them.

Two rules the entries exist to serve:

- **QEMU is exact about how much work the guest does and useless about how
  long it takes** (PERFORMANCE.md) — and it runs SeaBIOS, which hides a whole
  class of bug: anything a real ROM does differently (notes 5, 7, 10, 31, 35,
  36). MartyPC with a period ROM is the instrument for an 8088
  (docs/TESTING.md).
- **Ask which machine a report came from** (docs/FIELD-MACHINES.md). A Tracker
  audio report sat here for months as a 5150 report and had come off PCem;
  the 5150 has no sound card.

**Still open:** 3 (mechanism D), 10, 14, 19, 24.2, 28, 32, and one residual
each in 33 and 37.

---

## 2. Heap fragmentation: a second Tracker load says "Out of memory" (CLOSED — two bugs, both fixed)

On a 384KB machine, load `BEVERLY.MOD` (116KB), play, close Tracker, open it
again, load again: refused, with the Task Manager showing ~104KB of heap free.
The total said there was room; the **largest run** said otherwise, because two
long-lived claims had been left in the middle of the heap:

1. `DSV_RELINST` released only the OPL half of the sound driver, so a package
   that had streamed left the driver's 20KB staging pool (`SBL_POOLKB`) claimed
   for the rest of the session. It is `snd_release_both` now, published by
   either half attaching.
2. Tracker held its ring grant for its whole lifetime, so even a stopped
   Tracker kept the pool claimed. It frees the grant in `trk_stream_close`.

What stands from it: §50.3's rule that a long-lived data claim mid-heap splits
the heap, which is why `OSAPI_MEM_AVAIL` reports the largest run and why
Tracker sizes its request from that figure. §66's compactor came later and
moves only data claims whose holders opted in — a region's base is its CS and
never moves — which is exactly the class the two offenders here were in.

---

## 3. Disk access is horribly slow (OPEN — mechanism D only)

**Symptom.** Navigating the file manager on a 5150 feels far slower than the
work justifies. Four mechanisms, all *work* rather than timing, so QEMU can
count them (`DISKCNT=1` compiles the counters; §18.94 is the published
instrument).

| | what | state |
|---|---|---|
| A | `dsk_chdir` is a full `disk_mount` (`dsk_chdir_x` sets `[dsk_keepcwd]` and calls `disk_mount_x`), so a folder change re-validates the BPB and re-reads the FAT | **declined** |
| B | the FAT window (`DSK_FAT_SECS` = 9 sectors on kern_big) is re-read on every one of those | **declined** |
| C | one `int 13h` per sector | **fixed** — §18.91 batches a run, §18.91.1 bounds it at the cylinder |
| D | the icon harvest reads the first sector of every package in the folder, on every mount — `APPS/` costs its package count in extra revolutions every time it is opened | **open** |

**Why A and B are declined** (docs/plans/completed/DISK-PERF-PLAN.md §4): the
only honest media-change test is `int 13h AH=16h`, and a 5150 with a Tandon
TM100 has no change line — a FAT window reused across a swapped disk gives a
file manager that lists correctly and reads garbage. `dsk_chdir_q`, the
file-I/O path, gets §18.9.1's "already standing here" shortcut; the listing
path does not.

**D is designed and not built**: docs/plans/completed/DISK-PERF-PLAN.md §5.5
(hit skips, miss harvests, one buffer), and
docs/plans/completed/DISK-PERF-PLAN.md §5.5.3 lists what has to be decided.
Count first — `DISKCNT=1` through two folder opens — so the harvest is priced
against the mount around it before anything is written.

---

## 4. "Bad package" on a file that is perfectly good, until the Disk window is refreshed (CLOSED — SPEC.md §22.8)

On the 5150: run a package from an open Disk window, have it write a new file
into that folder, close it, double-click another package in the same window →
**Bad package**, every time, until Refresh. Nothing was corrupt. The window's
per-window listing cache (§22.1) was stale: `dskw_write` remounts, the global
snapshot gains the new name sorted into place (§19.4), the window's cache does
not, and a double-click hands `loader_run` a directory INDEX that now names the
file *after* the one the row shows. It needs a new name that sorts before the
one launched, which is why it was rare and read as damage.

**Fixed** by §22.8: `dskw_sync` marks `FS_DIRTY` on every window showing the
folder that changed, and `fm_focus` re-lists when that window next comes to
the front. Reproduced under QEMU before and after with Note Pad's Ctrl-S; it
never needed the iron. The general rule the citing sites lean on: an index
resolved against a snapshot that can have moved is this bug, whatever the
route.

---

## 5. Multi-sector floppy reads returned the wrong sectors (CLOSED — SPEC.md §18.92, §18.93)

With §18.91's batching, every package froze the machine as its window drew, on
PCem and never under QEMU. The IBM PC/XT ROM's diskette parameter table
(`int 1Eh`) ships **EOT = 8**, a DOS 1.x number every DOS overwrites at boot
and os8088 never did. A single-sector transfer never consults it; a
multi-sector run reaching sector 9 flipped to the other head and returned head
1's sector 1 with `CF = 0` and the full count — correct opening sectors, wrong
bytes in the middle, a header that validated, a window that drew, a machine
that died on the substituted code.

**Fixed**: `dsk_dpt_init` copies the ROM's table, patches EOT to the mounted
volume's SPT before every transfer and installs the vector; the boot sector
does the same into `0000:0580` (§18.93). `make FLOPPY1=1` forces `AL = 1` in
both loops for an A/B. SeaBIOS never reads the table, so no emulator here can
show this class of bug at any speed.

---

## 6. The cursor washes out to white while the mouse is moving (Hercules) (CLOSED — SPEC.md §7.1.2)

On the 5150's Hercules the whole arrow flashed white for an instant while
moving. Not the two passes coming apart — `cur_put_mono` writes halo and body
in one store — and not a wrong cell: a checker rebuilt `(saved | white) &
~black` from the kernel's own tables at sixteen positions and matched the
framebuffer every time. It was the **erase-then-draw gap**: between `cur_get`
restoring the old cell and `cur_put` drawing the new one the cell holds the
background, `ffff` inside a window, and the ~1.3 ms pair against a 20 ms frame
is a beam crossing a white blob a couple of times a second.

**Fixed** by `cur_move_mono` writing every byte once: pass 1 skips the bytes
pass 2 will write, pass 2 takes its background from the save buffer. A dense
walk parks and compares at 0 differing pixels of 237,600, and breaking pass
2's source back to the screen leaves 98. **VGA is still erase-then-draw**
(`cur_move`, `mouse.inc`): its save is four planes through Read Map Select and
cannot take a background from a buffer; its draw is one store (§7.1).

---

## 7. The floppy is 6x slow because int 13h answers AL = 1 (CLOSED — measured 3.9x on the 5150)

§18.91's batching measured **slower** than one sector a call on the 5150
(PERFORMANCE.md Sets 11–13), and DOS copied the same disk on the same drive at
~12,700 B/s against our 2,161. `sysbench`'s raw `int 13h` block (Set 14) cleared
the drive, controller and BIOS — a whole track in one call streams — and the
§18.94 call counter then showed a 32-sector file costing **148 sectors of
traffic in 34 calls**. The (LBA, run) trace (Set 16) named it: `dsk_xfer` asked
for nine, the BIOS moved nine and answered **`AL = 1`**, the short-count
handling believed it, and every sector cost its own revolution.

**Fixed**: `CF = 0` means the whole request completed; `dsk_xfer` and the boot
sector's `read_run` both advance by the request now, and `make DISKAL=1` puts
the old behaviour back. A 16KB read went 8.29 → 2.09 s (Set 17) and the boot
726 → 181 ticks (Set 18); the data check (`BENCH.DAT` holds its own sector
numbers) passes on the 5150 and a Compaq Portable III. The last 2.2 s of the
gap was the track bound, which §18.91.1 took (note 31). Two corrections worth
keeping: the media is 1:1 and not 2:1 — the second revolution a track read
costs is the IBM ROM's own head-settle loop, once per call (Set 37) — and
`sb_verify` was first written inside the `DISKCNT=1` block, so the check that
licensed the change did not run on the build that shipped it.

---

## 8. `GFX_UNLOCK+LOCK` was 9x dearer on the 5150 (CLOSED — it was neither the mouse nor the machine)

The one `gfxbench` row where the 5150 and MartyPC disagreed (2,241 µs against
246) measured **290 µs** on the next field build with the pointer provably
parked, and 369 with it moved continuously — so the mouse is worth +27% there,
never 9x. What changed between the two builds is the kernel and which commit
did it is not established. `tests/benchlib.inc` now samples the pointer
outside every timed span and prints whether it moved, so the operator half of
any such reading is answered in the report.

---

## 10. A package cannot safely call int 13h (OPEN — structural, not fixable from a package)

`sysbench`'s raw `int 13h` block hard-froze the 5150 on the first run after a
cold boot and ran normally after a reboot. A real BIOS runs its disk handler,
and the IRQ6 nesting inside it, on the **current task stack** (`SCH_STACK` =
384 today, §8), on top of the benchmark's own frames; and the kernel's
`dsk_xfer` holds `sch_lock` across every `int 13h`, which a package has no slot
to ask for. Whether it dies depends on where the tick lands inside the BIOS's
wait loop, so it is intermittent. SeaBIOS services interrupts on a stack of
its own, so QEMU can never show it; `tests/stackprobe` exists for that reason.

Kept with the hazard written at the top of the block: it is the only
instrument that could answer note 7. **Nothing shipped may copy it.** If a
BIOS-direct number is ever wanted routinely it belongs in the kernel behind a
knob, holding `sch_lock` like every other transfer.

---

## 11. Freehand circles in Paint come out as long straight chords (CLOSED — SPEC.md §42.8)

On the 5150's Hercules, a fast freehand circle collapsed into chords. Not the
mouse: `pt_seg` issued one `gfx_fill` per pixel, ~933 µs each, so the pencil's
ceiling was ~1,000 px/s and a hand passes that on the fast part of an arc.
**Fixed** by §42.8: a width-1 segment is one `OSAPI_GFX_LINE` (576 calls → 66
over one stroke), and the fullscreen bracket's per-sample floor is gone on
1bpp. The sample rate and the wide nib were separate defects (§42.8.1, note
37).

**Gotcha that survives it:** `gfx_line` does not rasterize the same on every
adapter — `gfx_line_raw` sends mono to `gfx_line_mono` and VGA to
`gfx_line_runs`, and a canvas walked with the mono arithmetic differs by 663
bytes on VGA. Nothing depends on the two agreeing, so Paint's fast path is
gated to 1bpp rather than the kernel changed.

---

## 13. The mouse is detected, moves exactly once, then freezes (Compaq Portable III) (CLOSED — SPEC.md §9.5.2.1)

§9.5.2's machine, not §9.5.2's symptom. The mouse is at 0x2F8 and pulls
**IRQ4**; `mou_lockon` retired the losing 3F8 row with the 8259 mask its base
implies, which is the mouse's own line. Eight clean packets, one movement,
silence. **Fixed** by masking the line the winning packets arrived on
(`[mou_line]`) instead of the base; `make test MOUSEPORT=com2irq4` is this
machine and reproduces it. Confirmed on the Compaq: `winning row 2` beside
`winning IRQ hex 10`, and `packets needed COM1 8 / COM2 1`, the first
real-hardware confirmation of §9.4.1's identify burst on a two-port machine.

---

## 14. The 5150's clock was not detected once, and has not failed since (OPEN — instrumented, one observation)

**Symptom.** Once, the 5150 (AST SixPakPlus, MM58167) came up on the fallback
date. The next boot, with §37.92's block in the kernel, answered `tier that
answered 2`, `NS probe stop hex 00FF` — rung 4, every gate passed — and it has
not failed since.

**Ruled out** by the passing card: `NS 0D wr AA rd = 0A`, the strict test
`clk_ns_probe`'s own comment nominated as the first suspect, passes on this
card; `NS reg 00 = 90` is the largest value gate 2 accepts and is simply the
top BCD digit of a tens-of-milliseconds counter.

**Standing theory: gate 4, the RP5C01 veto, depends on the time of day.**
`clk_rp_fields` reads the low nibble of each port and refuses the MM58167 if
they spell a plausible RP5C01 page 0 — but on an MM58167 those ports are its
own counters:

| port | RP5C01 wants | MM58167 has there | passes when |
|---|---|---|---|
| 0x01 | 0..5 | hundredths of a second | units digit ≤ 5 — changes 100x/s |
| 0x03 | 0..5 | minutes | units digit ≤ 5 |
| 0x05 | 0..3 | day of week, 1..7 | ≤ 3 |
| 0x06 | 0..6 | day of month | units digit ≤ 6 |

A genuine SixPakPlus looks like an RP5C01 a few percent of the time, and the
first row makes it non-reproducible boot to boot. The veto stays — it is what
stops two writes landing on a TC8521's MODE register — and one observation is
not enough to trade it away.

**Next**: when it recurs, read `NS probe stop hex`. `04` → the veto fired and
the fix is to make gate 4 time-invariant; anything else → the named gate is
the lead; `00` → an earlier rung claimed, which on an XT is something stranger
than a clock fault.

---

## 16. The scroll runs about three rows ahead of the music (CLOSED — SPEC.md §45.15.3)

Off PCem with a Sound Blaster and `CLICK.MOD` (`make clicktest`): the row on
screen led the click by a consistent ~3 rows. `TRKLOG.TXT` showed `PLAY-CONS`
riding ~800 bytes high and peaking past the 2,048-byte ceiling: §45.15.1's
estimator had no downward correction, so a starve on a real machine displaced
it and nothing pulled it back for 20 s. §45.15.3 closes a loop on the report
edge (6 reports, 2.2 s). Confirmed on the field machine: +1.20 rows → −0.21,
the same −0.2 MartyPC reads at every rate.

Two things worth keeping: the `K` key on the `clicktest` build moves XT mode's
rate through 4,000/5,500/11,000 Hz without leaving the text screen, which is
the discriminator between a guest-side offset (fixed bytes, so rows scale with
the rate) and a host-side one (fixed milliseconds) — leaving XT mode with `X`
changes the surface too and destroys the measurement. And **a single capture
of an uncontrolled quantity is a sample, not a size**: "exactly three rows"
being almost exactly one DMA block at the XT rate was a coincidence that
survived a capture, a mechanism and a designed experiment.

---

## 17. Four reports off the first `combo.img` field run (CLOSED)

The 5150 run that confirmed §18.97's probe on iron (`ST3 0021` twice, `probe
stop 03`, `verdict 0`, drive B gone) brought back four formatter reports. Two
things about the probe's block first: with `drives int 11h claims 2` the
external-pair loop (§18.98) correctly never runs — to exercise units 2 and 3
the switches must claim three or four — and `fdd_dbg_*` (§57.5) is one row per
unit now with `probe ran` a bitmap, where it used to describe only the last
unit asked.

### 17.1 The format prompt does not clear on Escape (CLOSED)

§22.12's prompt became two lines at §26.4 and the cancel path repainted one:
line one stayed on screen and read as corruption. Mode 5's cancel takes the
full-repaint exit now. It never showed on the Enter path, which is the only
path every test here had pressed.

### 17.2 Format Disk stays greyed on a disk it just made (CLOSED)

§22.12's predicate was live only while the mount had failed, so os8088 could
format any disk except one of its own. `fm_fmt_ok` asks the different question
instead — `ERASE and format A: as 360K?` on a mountable volume. Three traps
that the greying had been hiding are closed with it: `dskw_fmt_probe` restores
the caller's live geometry rather than forcing 9/2 on a cancelled prompt;
`fm_fmt_home` sends sibling Disk windows on that drive back to the root with
§22.8's deferred re-list; and §18.96.2's 360K fallback after a failed 720K
reach test is kept on its own merits, with a toast.

17.3 (720K on the field machine came back 360K) is §18.96.2 working on a
40-cylinder Tandon: 354K free on a 360KB volume is correct. 17.4 (the size
toggle) is `fm_fmt_sizeable`: `Spc=size` appears for units 2 and 3 only.

---

## 18. The switches were flipped and no external drive appeared (CLOSED — not a bug, and a machine fact)

On the field 5150, `int 11h` claimed **two** drives with SW1 set for one and
again with it set for three: bits 7:6 of the equipment word were not tracking
the switches at all. That is why drive B appeared on a one-floppy machine
before §18.97's probe existed, and the probe removing it is the only thing on
that machine that can answer the question. `sysbench` prints the raw equipment
word and, on a 5150, SW1 read straight off the 8255, so "the count did not
move" and "the switches did not reach the chip" are separable in one run. The
arithmetic trap: J1's first external drive is physical #2, so one internal
plus one external is a claim of **three**.

---

## 19. The 765 cannot see the external drive that DOS reads fine (OPEN — routed around, SPEC.md §18.98.1)

**Symptom.** The field 5150's IBM 4865 on the 5.25" adapter's 37-pin connector
mounts, lists and reads through `int 13h DL=2`. `dsk_fdd_probe`, driving the
FDC directly, reports it absent:

```
  --- unit 2
  ST3 motor off hex     0022      bit 4 (TRK0) CLEAR
  ST3 after seek hex    0022      ...still clear after a RECALIBRATE
  ST0 drained hex       0072      IC 01, SE, EC - Equipment Check
  probe stop hex        0003      ABSENT
```

The unit-select bits in both answers say the commands addressed unit 2.

**Ruled out**: the drive, power, cable and select jumper (DOS and `int 13h`
use it), and **media** — the capture was taken with a formatted disk in the
drive, in the boot that mounted it, so TRK0 is not gated on media here.

**What note 21 added**: the Packard Bell 286's present, working unit 1 answers
the identical `ST3 = 21` twice, but `ST0 = 21` — IC 00, SE, **EC clear** —
where the 5150's genuinely absent unit 1 answers `ST0 = 71`. ST3 cannot tell a
present drive from an absent one on these controllers; ST0 can, in one
direction (§18.97.3). On this 4865 ST0 says EC set, so it is the harder case:
the controller issued step pulses and never saw track 0 through a drive that
DOS reads.

**Routed around**: units 2 and 3 trust the equipment word (§18.98.1); the
probe still runs and publishes, which is the only reason any of this is
diagnosable. Unit 1 is still contested on tier 0.

**Next**: disassemble the 27 Oct 82 ROM's motor-on/select/seek sequence and
diff it against `dsk_fdd_probe`'s — the ROM image is in hand. Carry two
questions into the read: whether the IBM adapter decodes DOR bits 4–7 as motor
enables for units 2 and 3 the way a stock FDC does, and whether the drive
needs something asserted before it will drive TRK0.

---

## 20. A window dragged from BEHIND another lands at the covered rect's corner (CLOSED — SPEC.md §11.96.10.1)

Found by `tests/dispsave.py`: the dropped window landed 130 px from where the
pointer asked, at exactly `wm_cov_x2`/`wm_cov_y2`. `ui_dispatch` holds the
mousedown point in `CX`/`DX` across `wm_front`, `wm_front` saves
`AX`/`BX`/`BP`/`DI` — what `wm_raise`'s contract says it clobbers — and
§11.96.10's arming site loads a rect into all four. Two pushes. It hid because
it needs a window that is not already frontmost, and on a 16 px cascade the
error reads as drag imprecision.

---

## 21. No Drive B on the Packard Bell 286, and a 1.2MB drive sitting right there (CLOSED — SPEC.md §18.97.2, §18.97.3)

§18.97's probe read a present 1.2MB drive as absent (TRK0 clear before and
after a recalibrate — note 19's signature) and removed it. The fix is about
the **claim**: on a 5150 the drive count is the SW1 default and worth
disproving; on an AT it is CMOS setup, somebody's decision. The probe still
runs everywhere and the verdict is acted on **on tier 0 only** (`[cpu_tier]`).
No emulator here can produce a real absent verdict — MartyPC and QEMU both
synthesize TRK0 set — so `make FDDABSENT=1` forces it and the four cells
(knob × MartyPC 5150 / QEMU 386) are the gate. Confirmed on the machine with a
1.44MB `combo` disk: drive B back on the desktop. The `sysbench` run that
followed produced the ST0 finding recorded in note 19, and §18.97.3 consults
ST0 before anything is removed, only ever to change the answer to *keep*.

---

## 22. Tracker "hardlocks" at the end of a large module (CLOSED — the module says stop)

`banana split` ends order 48 with `F00`, ProTracker's stop; `mp_playing` goes
0 and the FT2 text screen legitimately has nothing to draw. Inside §53's
bracket there is no clock, cursor or chrome, so a finished song looks exactly
like a dead machine — the status line's `Stopped  ENTER play  F/ESC exits` is
the whole diagnostic surface. Reproduced on MartyPC: Esc exits. If it comes
back, ask whether Esc exits and whether the last pattern carries `F00`. The
locator worth remembering: `Pos 30/30` can only come from the XT-mode text
screen (`trktxt.inc` prints `songlen − 1`; the windowed splash would say
`30/31`), so a readout formatted differently in two places names the screen.
`BPM 125` on every module is correct — a MOD carries no tempo, and `TEMPO.MOD`
(`F96`/`F3C`) proves the field tracks `Fxx`.

---

## 23. A black dash on the desktop after mounting a hard drive (CLOSED — SPEC.md §51.2.4)

One 16-pixel black run on one scan line, PCem 286/VGA, deterministic within a
boot and different between boots — uninitialised memory, and an address that
lands harmlessly on the other two adapters. QEMU hands the guest zeroed RAM
and the bad value was 0; **`make DIRTYRAM=1`** fills the claim heap with 0xAA
before anything claims from it and reproduced it on the first try. A gdb
watchpoint on the framebuffer byte stopped on `pop word [snd_inst]` in
`drv_call` with `DS` = a heap segment: a restore 0xD5F9 bytes past the driver's
base, into VGA memory. The rule: anything banked in kernel memory across a
call that changes `DS` is pushed before `DS` and popped after it is back.

---

## 24. The VGA's colours corrupt after a few minutes (CLOSED — oxidised sockets on the card)

**5150 #2** (docs/FIELD-MACHINES.md): a PVGA1A-JK primary, Hercules beside it.
After minutes of use the whole screen recoloured — dither to lavender, white
frames to cyan — with every shape still exactly where it belonged. That
picture rules out the video RAM by itself: a RAM fault moves *pixels*, and
only the stages every pixel shares (attribute registers, DAC, output) recolour
a correct picture uniformly. §39.21 put the readback of the first two into
`sysbench`'s video block. os8088 never writes the attribute controller or the
DAC; the only palette write on the machine is the BIOS mode set.

### 24.1.2 The trigger is drawing volume, not time and not the disk

The first correlation was with disk activity (every action that corrupted it
mounted something) and the Picomem was suspected. A window **drag** — an XOR
outline and nothing else — corrupted it instantly; sitting still never did;
mode 6 at 640x200 never did. So the provocation is VGA memory traffic and the
damage lands in the DAC, which no sequence of Graphics Controller or Sequencer
writes can reach: hardware.

### 24.1.3 Fixed — and the instrument was flawed

Four socketed chips pulled, sockets cleaned, reseated: no corruption for a
whole session. And the `dac SHOWN 16 sum` row did not order with the screen
(057E less corrupt, 044B more, 0441 clean, against a known-good 05D3), so the
"DAC contents moved" reading of the earlier pairs is withdrawn.

### 24.1.4 The double read fired on its first outing

The row now reads the sixteen entries twice and prints both; on the repaired
machine they read `0433` and `03D2`, milliseconds apart. **DAC readback on
this card is unreliable outside vertical retrace**, which is why period
software programs the DAC during retrace. Fixing the instrument means reading
inside the retrace window, and it is worth doing only if the row is ever
needed again.

### 24.2 The Hercules destabilised too (OPEN — hardware, nobody here can see it)

With the desktop extended onto it the Hercules went wavy at the edges and
"out of phase". A Hercules has no palette, so it cannot be the same fault as a
palette fault: either two faults, or a common cause upstream of both cards.
5150 #2 carries 384KB of ISA RAM, a Picomem and two video cards on a 63.5W
supply. The tests are physical — pull cards, measure +5V under load, try other
slots — and no software change is the answer.

---

## 25. An XMS RAM disk "corrupted" what was copied onto it (CLOSED — two defects, neither the extended-memory store)

An 86Box 386 with 4MB: 1024 typed into the size box corrected itself to 264,
and a 297KB module dragged onto the drive arrived truncated and "not a mod".
**`rd_kb_max` gated the extended store on conventional room instead of
subtracting it** (§62.9.10.3), so a 4MB machine offered 264K. And **a copy that
ran out of room left a truncated file with its truncated length in the
directory and no error left on screen** — a defect of the copy engine on any
volume, not the RAM disk. §22.5.2 refuses a copy that will not fit before it
starts and `fcp_undo` deletes a partial destination when one fails after
creating it. Ruled out on the way, and fixed anyway: `drv_fs_call` not clearing
the dispatch stamp (§62.9.10.4), which broke a package's own save onto an XMS
volume and nothing else. A drag between Disk windows is a copy, not a move
(§22.3).

---

## 26. A window dragged onto the second monitor comes back smaller (CLOSED — SPEC.md §11.100, §39.16.3, gated by tests/dispsize.py)

Hercules beside a CGA: a Disk window dragged across the seam and back came
home 320x140 instead of 320x200, because `ui_drag`'s release ran the straddle
clamp `wm_strad_fit` **before** `wm_nat_bank`, so the bank recorded the cut and
nothing could put the size back. §11.100 gives a window a preferred size per
adapter kind and a minimum the kernel may not cut through, and the drag banks
its position and re-derives its size from the bank (§11.100.3).

### 26.2 …and a window dropped clear across the seam is not cut at all (NOT A BUG — SPEC.md §39.16.3.2)

Solitaire at 258x303 dropped wholly onto the 200-row CGA hangs 104 rows into
§39.2.1's dead zone, and a first fix clamped it. The second field report
reversed that: a window may hang off the bottom of the primary and is left
alone, so it may hang off the bottom of the secondary too — rows nobody has a
display for are not a hole in the desktop. The clamp is gated on the frame
actually reaching the other display. The finding that survives: a rule derived
from the union's bounding box treats regions no display owns as though the
desktop did.

---

## 27. A window that draws every frame starves the pointer (CLOSED — three defects, SPEC.md §7.3, §10.1, §10.2)

`apps/wire` on the 5150: "I could not even click to close the window, after
over a minute of trying." Reproduced on `os8088_5150_herc`, and it was three
defects at once: **`gfx_lock` had no fairness** — a worker that releases and
immediately re-takes beats a UI task that has to be scheduled first, so the UI
task ran once a tick (18 passes/s against 650 idle); **the UI task drained one
event a pass**; and **a full ring dropped the newest press**. The queue
arithmetic then decided which half of a click survived: with one record a
pass, refusing the newest delivered sixty `EVT_MDOWN`s and no `EVT_MUP`, and
refusing the oldest did the opposite — either way the close box was never both
armed and spent. §10.2 (drain) is what fixed the click; §10.1 (drop oldest)
is kept for bounded staleness. `tests/evqfull.py` measures the ring.

### 27.4 Defect 1 was nearly thrown away on a measurement taken at the wrong moment

Counters on `gfx_lock` sampled per guest second read **0 blocks** at every
draw order, so a fairness handover was built, found to fire zero times, and
reverted. They were sampled with no input pending, when the UI task never asks
for the lock. Counted across the click instead — a memory breakpoint on the
press being queued and one on the menu going up — the same counters read
1,382–14,722 ms and 26–268 blocks; §7.3's handover takes that to 37–70 ms and
1 block. **Contention is a property of the moment a click lands.** The
quantum (`make QUANTUM=2|3|4`, §53.2.1's sub-tick armed system-wide) is real
and measured — 54 passes/s against 18 — and with §7.3 in place it moves
nothing, so it stays a knob and stays off. Paint's version of the complaint
was a separate defect, `pt_stroke`'s own wait aliasing a 40 Hz mouse to the
tick (§42.8.1).

---

## 28. CURFIX still reads wrong to the eye (OPEN — second report)

**Symptom.** §7.1.4.4 left §7.1.4.2 + §7.1.4.3 behind `make CURFIX=1` because
the instruments and the eye disagreed. Judged twice by the same reader on an
`os8088_5150_herc`-class machine, both times a qualified no: *"slightly weird
— almost like the acceleration is wrong, and it flashes just as much with wire
running as no-curfix."*

**What the two claims mean.** Nothing in either section touches `mou_isr`'s
deltas — §7.1.4.3 stores `[ticks]` into `[cur_mvt]` and reads it in
`cur_lazyck` — so "acceleration" is about *when* the arrow is redrawn, not
where: an arrow hidden through a draw and put back at the new place reads as a
jump. The wire half cannot discriminate: the ISR does not move the arrow while
the lock is held (§7.1), and wire holds it for nearly the whole frame (note
27), on both builds.

**Ruled out**: the disks — both were built at one commit differing in the knob
alone, with a marker file in each root.

**Next**: an instrument that reads a MOVING pointer. Every pointer instrument
in `tools/` parks it (§7.1.4.4), which is why the eye and the instruments
disagree. The default does not move until one exists.

---

## 29. The 5150 hard-freezes on an FTP upload to the hard disk (CLOSED — two faults, and a stack margin that is now a design)

FTP server with its root on `C:/`, client connects, `CWD`, the machine stops.
Ruled out early: `rep movsb` (§72.16 — a build that predated it froze too),
the profiler, the report save, the client sequence (clean under QEMU), a
cross-linked chain (`DSK_DIRW_MAX` caps a walk). Root on the floppy: no
freeze; mount the hard disk: instant. **The disk was not corrupt** — an ST-11M
controller reserves the front of the drive, so raw sector 0 is its geometry
block and the real MBR is 68 sectors in; `tools/os88disk.py --verify-hdd` is
the check that reads it right.

**Fault 1** (§77.31): `fd_stage`, the server's 8KB `int 13h` buffer, sat 51
bytes into a sector; §72.13's 37KB claim moved every region above it onto a
64KB page boundary. A `%error` beside the offset holds it. `apps/cyclone` had
the same violation; `apps/cc/os88thunk.asm` has it structurally and is
recorded rather than patched. **At least one "freeze" was the machine busy**:
`bl_save` held the gfx lock through several 400 ms `int 13h` calls with nothing
on screen, and `bl_progress` now says so before the write.

### 29.6 The instrument answered, and there were two faults wearing one symptom

`KFZ=1` paints fifteen bytes of kernel state into the menu bar from IRQ0 twice
a tick (mono only), and `tools/kfzread.py` decodes a photograph of it, because
a watchdog printed by `sch_isr` cannot report a freeze that took the timer
with it. Two captures, both called "hard freeze":

- **The dots stopped**: `sch_stkdie`'s bar. A task overran its slice —
  the FTP server's worker at 196 of a 256-byte slice before the tick
  arrived, and the ROM's `int 08h` chain then ran through the canary.
  `tests/ftpd.py --kfz` reproduces it under QEMU at 232 of 256, and
  `tests/stackprobe` on the 5150 read **220 of 256** during a 300KB upload.
  This is what moved `SCH_STACK` to 384 (§8.5, §9.10, §8.7: the ROM chain and
  both mouse ISRs run on private stacks now, and slots are classed).
- **The dots kept going**: task 1 spinning in `menu_bpadc`'s pad loop holding
  the drawing mutex, because §12.8's progress widget lowered `[menu_bn]` from
  another task mid-loop. Both halves fixed (§59.7.1, §12.8.3).

The instrument had also broken the mouse — 10 ms per paint with `IF` clear
against a byte every 7.5 ms from a 1200-baud mouse — and it composes the row
once and blits it now (§9.6.5). An instrument that changes what it measures
cost a round.

---

## 30. A window drag during an FTP upload kills the transfer, permanently (CLOSED — SPEC.md §74.1.1)

A kernel bug that presented as a network one, and two rounds went into
`ETHER.DRV` before the control experiment — the unmodified build, one drag —
placed it. `ui_drag` drained every event that was not an `EVT_MUP`, `wm_wake`'s
per-slot coalescing flag stayed set with no record behind it, and the window
never received another wake: ftpd's commit never ran, its worker waited on a
handshake byte forever, and `NET_SOCKS`'s handles were gone. Any package built
on `OSAPI_WM_WAKE` across a worker boundary was one drag away from the same
silence. Confirmed on the 5150; the `ETHPUMP` experiment it had been blamed on
was removed (§72.19).

---

## 31. A 286 clone loads a scrambled kernel and freezes at 92% (CLOSED — SPEC.md §18.93.1, §18.93.2)

86Box `mr286`: `make rdiag`'s self-naming payload reported **92 of 206 sectors
never written, first at file sector 15, each holding 0000** — the BIOS stops at
the head boundary and answers `CF = 0` for the whole request, so §18.91.1's
cylinder-bounded run left the back half of every crossing run unwritten. Note
7's hazard (a short transfer taken as complete) through a different door than
note 5's. **Fixed** by §18.93.1's canary — the loader verifies the transfer
against a word read out of the image and reloads track-bounded — and
§18.93.2's XT gate. A second door was found and closed before shipping: the
loader published its final run bound as a number the kernel compared against
the **mounted** volume's SPT, so a 286 that booted a 1.44MB disk (18) and then
mounted a 360KB one (9) switched crossing back on; the word is a boolean now,
written only on the path where a crossing run passed the canary.

**The BIOS survey** (`build/rdiag360.img`, one boot each; the clone ROMs are in
`tools/martypc/roms/`, untracked):

| BIOS | class | result |
|---|---|---|
| IBM 5150 / 5160 | XT | crosses a head correctly |
| GLaBIOS 0.2.6 | XT | crosses a head correctly |
| Compaq Deskpro Rev H (11/10/86) | XT clone | crosses a head correctly |
| Eagle PC Spirit 1.9 | XT clone | crosses a head correctly |
| Columbia MPC 1600 3.02 REVB | XT clone | untestable — halts at 0000:0407 on MartyPC before the loader |
| MR BIOS 286 (86Box `mr286`) | 286 clone | **will not cross a head** |
| Packard Bell 286 (86Box `pb286`) | 286 clone | errors twice, then lies (note 36) |

Two 286 clones fail and every XT passes: that is the evidence §18.93.2's gate
rests on, and the canary runs underneath it for the XT that turns out to be
the exception.

**§18.91.1 measured on the 5150: 2,197 ms.** `boot + early init` 7,416 ms
cylinder-bounded against 9,613 ms `TRACKRUN=1`, the second figure identical
across two runs; MartyPC predicted 1,923. The other row that moved, mouse_init
at 591 against 1,195, is §9.4.5's identify window ending early or running to
its full length (`MOUIDSLOW=1`), not the disk. A first revision of this A/B
blamed `TRACKRUN=1` for not booting at all; the disk had been written by
os8088's own `Write Img...` and DOS refused it afterwards — note 32.

---

## 32. os8088's own `Write Img...` left a floppy whose low-level format was damaged (OPEN — reported once, not reproduced, not reproducible on any emulator here)

**Symptom.** One physical 360KB floppy: `Write Img...` (§18.99.8) wrote an
image to it and it did not boot; DOS's `dskimage` then **refused the same
disk** — *"unable to use sector 22"* — until a DOS `FORMAT` recovered it, after
which `dskimage` wrote it and it booted. A write tool refusing a sector number
is being refused by the ID address marks, which a format writes and a write
never does. Severity: media the user did not ask us to touch the format of,
recoverable only under another OS.

**What the source says.** Every floppy `int 13h` this system issues is
`AH=02h`/`03h` (`[dsk_op]` takes exactly those two values), `AH=00h` on a
retry and `AH=08h` in `clone.inc`. There is **no `AH=05h` format-track call
anywhere in the tree** and no `AH=17h`/`18h` set-media-type either. The
geometry comes from the image's size, which was right. The diskette parameter
table is the ROM's own with only EOT rewritten (§18.92).

**Candidates, ranked by the evidence:**

0. **The disk was already failing.** The front-runner: the owner had written
   this exact disk with this tool before, so only one write went wrong.
   *Test:* write a fresh, DOS-formatted, known-good disk and read it back with
   `dskimage`.
1. **`int 1Eh` frozen at the boot media's parameters.** §18.92 points
   `0000:0078` at `dsk_dpt` permanently; a BIOS that swaps tables per media
   (360KB media in a 1.2MB drive) is prevented from doing so, and a write under
   the wrong gap length can run the write gate over the next sector's ID mark
   — documented µPD765 behaviour and exactly the damage seen. *Test:* which
   drive, and is the BIOS AT-class? A genuine 360KB drive on an XT BIOS nearly
   rules it out.
2. **No set-media-type before writing** — candidate 1's twin (a 96-tpi head
   over 48-tpi tracks leaves old ID fields under the new ones;
   docs/FIELD-MACHINES.md warns of the same thing). The same test separates
   them.
3. **`clone.inc`'s window arithmetic writing off a track.** Weak: §18.91.3
   bounds every write run at the track. A `DISKCNT=1` clone with every write's
   CHS logged against the image's geometry is the cheap check, and the only
   one an emulator can run.

**No emulator here can reproduce it**: 86Box, QEMU and MartyPC present a
floppy as an array of sectors, so a write with the wrong gap, data rate or
track width lands in the right slot. A green `make test-full` says nothing
about this note.

**Next report needs**: which machine and which drive (a 360KB drive or a 1.2MB
one), whether the disk was freshly formatted, and whether it recurs on a
known-good disk — at the same sector number (arithmetic) or a wandering one
(media/timing). Until then treat `Write Img...` as unsafe on media anyone
minds losing.

---

## 33. A hard-disk install writes the whole volume to the wrong place, because SYSTEM.CFG carried another machine's geometry (CLOSED — drivers/hdd/cfg.inc, boot/boothd.asm)

Both 5150s, not reproducible in 86Box on the same images. Diagnosed from the
two dumped 20MB images alone: the newer install's structures were where a
**17/4** geometry puts them on a **63/16** drive — four runs of seventeen
sectors at a stride of 63, then a jump of 1,008 — and `SYSTEM.CFG`'s `HD` blob
carried `cyl=613 heads=4 spt=17`, the previous stop's ST-225. `hd_cfg_apply`
restored a saved geometry over the probe's on a key (`BIOS/80h/0`) that is the
same on every machine, and the file travelled on the install floppy from
86Box to the ST-225 to the Picomem, refreshed at every stop. **Fixed**: a saved
geometry is restored only when the probe could not determine one or the record
carries `HDC_F_TYPED`; the mount bit still travels.

**The `G` was a second fault, also fixed.** `boothd` asked `int 13h AH=08h`
for the geometry where the kernel's own `dsk_bpb_check` reads it from the BPB
— the geometry that *wrote* the volume is the only one that reads it back, and
a drive the card created minutes earlier has no ROM answer to give. `boothd`
takes spt and heads from the BPB now, eight bytes smaller, and both field
images boot with the new sector without reinstalling.

**Residual, open**: a *typed* record travelling the same route is still
restored. `hd_page_adjust`'s `+`/`-` are live on every drive; greying them on
a probed drive (§47) would close it and has not been taken.

**Worth keeping**: read the image first. Every prediction about where a
structure would be found came true off the platters, with no hardware.

---

## 34. The mouse cursor gets written into save-unders, most often out of the File Manager (CLOSED — SPEC.md §12.8.4, §11.101.2)

286/VGA under 86Box: an arrow-shaped hole in a Disk window's listing, carried
forward by the raise cache, worst when launching a package.

### 34.1 What it is

Two defects, one symptom. **The file-operation progress widget drew with the
gfx lock free, and a clear lock is the one state in which the mouse ISR
draws** — `fpg_arm` refused when another task owned the mutex and proceeded
when nobody did. `make GFXAUDIT=1` counts primitives entered unlocked and named
seven call sites, all this module's, twelve per unlocked file operation. Fixed
(§12.8.4, 38 bytes): `mou_apply` defers on `[fpg_on]` and `fpg_arm` takes the
arrow off the glass first. The more general fix — the widget taking the mutex
around each burst — is written down there and not shipped at 141 bytes.

The second report ("almost 100% now, and only when the window opens above the
cursor… Cyclone was causing it every time") was **`OSAPI_WM_SHOW` not taking
the lock the SDK said it took** (§11.101.2): `wm_su_precover` banked the
arrow off the glass as the window's content. The first fix made it
reproducible by freezing the pointer where the reader used to move it away.
`tests/gfxlk.py` (soak) counts the ISR reaching its draw while the widget is
up: 6 before, 0 after. A pixel test would have passed on the broken kernel
nearly every run.

What made the second one findable: the reader's exact recipe (park the mouse,
open with Enter), `GFXAUDIT=1`'s cursor event ring (six lines: `W` and `P` at
`lvl=0` with no `L` above them), and three comments that said `WM_SHOW` takes
the lock, written from the contract and not the code.

---

## 35. A boot that reached the desktop with a corrupt kernel — 86Box XT, 360KB (CLOSED — four defects, five gates)

86Box `ibmxt86` (the October 1982 IBM XT ROM), VGA, ST-11M, testing the
splash-evict branch (§2.9.4–§2.9.7). Four defects, each invisible to every
instrument here for a different reason:

| | what | why nothing saw it | gate |
|---|---|---|---|
| bar parked at 44% | `mov ds, KERNEL_SEG` went through the register holding `spl_tick`'s argument: 96/214, and 96 is 0x60 | nothing watched the bar MOVE | `tests/splashbar.py` |
| banded dashes behind the dialog (VGA only) | the ROM sets 12h without clearing plane 2, which holds the mode 3 font (§39.23) | every BIOS here clears mode 12h | `tests/vgadirty.py`, `VGADIRTY=1` |
| freeze at 92% | `SPLCALL` rewrote only the offset half of a retired far pointer; every disk access after the desktop called `cold_entry`'s padding | every boot row stops at the first frame | `tests/postboot.py` |
| `Loader checksum 589C` | stage 1's own multi-sector read lost sector 9 of the track: the IBM ROM's EOT is 8 and the patch ran in stage 2 (§2.9.8) | no other ROM says 8 | `tests/blobsum.py`, §2.9.7's checksum |
| silent | moving the text-mode set put `int 10h AH=01h` above the run bound: it takes the cursor shape in `CX`, which held SPT (§18.93.3); §18.93's reload rescued every boot and §18.91.1 was paid for and never collected | the boot works | `tests/cylrun.py` (`boot_cylrun != 0`) |

Two lessons. A bisect over a feature switch on a corrupt image is a bisect
over **code size** — every RTC configuration that reached `.found` failed and
every one that fell to `.none` booted, and none of it was a clock finding. And
a stamp-tracked knob is two lists: `VGADIRTY` went into `$(KNOBS)` and not
`$(VIDSTAMP)`, so its first two runs tested a kernel nobody built.

---

## 36. A handful of 86Box BIOSes have said `Disk error` since day one, and the BIOS never set DL (CLOSED — SPEC.md §2.9.11)

86Box `pb286` (Packard Bell 286, BIOS 09/17/86), 360KB disk, since the first
commit. `make bootdiag` (§2.9.10) answered it in one boot — `bootdiag360.img`
reached its report through its own paranoid loader and `bootdiagx360.img`
printed `Disk error` through the shipped one:

```
DL at boot 61  used 00  <== THE BIOS DID NOT SET DL. Fell back to 0.
```

`boot/boot.asm` believed the register. Fixed with a range check (a floppy VBR
cannot have come from a unit above 3) and the same clamp the other way in
`boot/mbr.asm`; `make test DLJUNK=0x61` is this machine and `tests/dljunk.py`
the A/B. The same report cleared the ROM's EOT patch, `0000:0580` and the
top-of-RAM relocation, and found this ROM answering a head-crossing run with
status `04` twice and then `CF = 0` **with two of four sectors wrong** — note
31's behaviour on a second 286 clone, already harmless because §18.93.2's gate
keeps a 286 track-bounded.

---

## 37. Paint's wide pen draws a snake (CLOSED — SPEC.md §42.8.3; a lag remains)

Third report of one complaint and the first with the right cause. With the
walk sampled at the live pointer on every turn (§42.8.1 had fixed the rate),
the stroke still wandered 9 px against a hand that wobbled 1: `pt_seg` kept the
Bresenham denominator in **CX, which `loop` decrements**, so the minor axis
stepped ever more often towards the end of each chord and the ink went
somewhere the hand never went. It scaled with chord length, so with speed; a
faster machine "fixed" it with shorter chords; a thin pen was not this code
(`pt_lineseg`). No harness had seen it because `os88mouse`'s injection costs
~0.5 guest seconds a report, so scripted chords are one or two pixels long;
pacing packets on the **guest** clock at 25 ms reproduced it first try, and
`tests/paintwalk.py` asserts the walk (each axis steps exactly `|d|` times).

**What is left**: an 8 px nib at 640 px/s finishes ~46 px behind the pointer
on a 4.77 MHz 8088; 63% of a step is `pt_rect`'s fixed preamble.
docs/plans/completed/PAINT-STROKE-PLAN.md prices the sweep primitive that would
take it.

---

## 38. A swimmer at the RIGHT edge lights a pixel at COLUMN 0 — 86Box 5150 + Hercules (CLOSED — the emulator's, SPEC.md §79.5.9; hidden by §79.5.10)

The mark is real in the recording and it is on **row y+1**: cross-correlating
the left-hand mark against the right-hand columns over every frame gives +1
and nothing else. On Hercules row *y+1* column 0 lives in another bank, 8,103
bytes from anything a row-*y* write touches; the byte after row *y*'s last is
row *y+4*. Scanlines are adjacent only in the display. MartyPC on both 1bpp
adapters is clean in the framebuffer and in its own rasterised field over
~2,500 forced straddles, the previous renderer measures identically, and
86Box's `hercules_plus` is clean where its plain `hercules` is not — a
horizontal filter reading one pixel before the scanline starts. The desktop
dither masks it (column 0 of an even row is already lit), which is why only
the black sea shows it. §79.5.10 reserves eight columns at the right on
Hercules so the copy has nothing to carry (`NOHEDGE=1` is the A/B,
`tests/fishedge.py` the gate).

Two traps: the capture's column 0 was image column 9, not 10 — calibrate an
origin against a feature the guest controls before reading a pixel — and
`os88marty.vram()` returns one entry per **pixel**, not packed bytes; read as
bytes the dither looks like a stipple and a wrong answer was published on it.

## 39. Every key types a different character on a 286 — 86Box `mr286`/`ami286`, AT 8042, serial mouse (CLOSED — SPEC.md §9.9.7)

`f` typed `\`. The PS/2 probe (§9.9) is gated on `[cpu_tier]`, so it never
runs on an 8088 and the same disk was clean there; `NOPS2=1` was clean on the
286 and `NOCHAINPRIV=1`/`NOMOUPRIV=1` were not, which put it on the probe in
three boots. `MOUDIAG=1` then gave the two bytes: **`cm1 55`** (the command
byte as read) against **`cmd 65`** (what `.fail` wrote back). The only bit
that moved besides 4 is **bit 5, 0 → 1**.

Bit 5 is the auxiliary clock on a PS/2 controller and **PC MODE** on an AT
one, and in PC mode an 8042 stops translating set 2 to set 1. The keyboard's
set-2 `f` is `0x2B`; set-1 `0x2B` is backslash. One bit, latched for the
session, and every key on the machine is wrong.

`.fail`'s own comment said it restored the byte "exactly as the BIOS had it"
and it did not — `[mou_p2cmd0]` is banked with bit 5 forced set for
`mou_p2_off`'s sake, which is right on a controller where a mouse answered
and wrong on one where nothing did. `[mou_p2cmdr]` is the raw byte and is
what the failure paths write now.

**Step 2's evidence test is ALSO wrong here and deliberately stays** (bit 5
clear reads as "the port is clocked" and on this controller means "AT mode").
Taking it out sends every machine whose BIOS left bit 1 clear down the `0xA8`
road — the road that stopped on the **Phoenix 8042** §9.9.1 step 3 records,
which the reporter confirms is their Packard Bell 386 (`vm/386-ps2`, the only
machine here with a PS/2 mouse) — and a differential
`0xA8` is worse still, because asking that question means writing bit 5 SET,
which is the PC-mode bit itself. Both were built and reverted. With the
restore fixed, being wrong at step 2 costs a probe that fails at step 5 and
puts the controller back, and nothing else.

**It could not be gated at runtime by anything in the tree**, which is why
`tests/unit/t_p2restore.py` asserts it over the source: the probe succeeds on
QEMU so no failure path runs there, and QEMU does not model the translate bit
either — measured, by clearing it on purpose and watching `ps2mouse` stay
green. Confirmed fixed on both machines: the 286 types correctly and the
Packard Bell's mouse is untouched.
