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

**Still open:** 3 (mechanism D), 10, 14, 19, 24.2, 28, 32, 43, and one residual
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
moves only claims whose holders opted in, which is exactly the class the two
offenders here were in. (This used to add *"a region's base is its CS and never
moves"*; §66.6.1 refuted it — a region moves when it is frameless, and
`mem_region_reloc` is the kernel's half of it.)

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

---

## 40. A line of the PREVIOUS horizon survives in the view (CLOSED — SPEC.md §88.3.1.1.3)

**Reported from play, and the report was a diagnosis**: banking one way left
*"a blank line in the ground"*, banking the other *"a filled line in the sky"*,
both **carried from where the horizon was on the previous frame**, both
sporadic, and both **sticking around until some object drew where they were**.
Two photographs, four seconds apart at the same flight state, put it at **view
row ~56 — the CENTRE row of a 112-row view** — as a horizontal run of 8 to 13
bytes; in one it is sky-black stranded in the ground and in the other a dashed
run of ground dither floating in empty sky, detached from the terrain.

**It is the narrow span of §88.3.1.1 meeting a SIDE SWAP.** A band row is given
the crossing's byte and one either side, on the argument that the rest of the
row is what it was; when the roll changes SIGN the two sides exchange, the fill
lays the row's whole width mirrored about a crossing that has barely moved, and
the argument fails everywhere the span does not reach. Every other row is
repaired by `cs_hzrows`' kind arm as the band sweeps past it — the band always
contains the view's centre and at roll 0 it is that row alone, so the centre
row is the one row in the band on both sides of the crossing. That is why it is
exactly one line, and why the field's *"until something draws over it"* is the
same fact from the other end: nothing repairs a band row's outer bytes but a
mark. §88.3.1.1.3 keeps last frame's side pair in one word and forces
`cs_fullspan` for the band when it changes — 18 bytes of `.text`, 2 of `.bss`,
one compare a frame.

**`tests/skiesstale.py` is the gate and it needed no model**: after `cs_blit`
returns the card must equal the shadow over the whole view, which is the
blit's one job. `--clobber` restores the old behaviour and reads the artefact
being **born at roll +0.0** — the frame the wings pass level — and surviving
every frame after.

**One thing worth keeping from the hunt.** A first instrument reported "1 to 3
bytes a frame that differ and are not carried" by reconstructing `cs_blit`'s
union rule host-side. That looked like confirmation and was **the model
disagreeing with the machine at the edges**; the real bug is 3 bytes wide in
the same place, which is exactly how a wrong instrument survives scrutiny.
Assert against the thing itself — the card against the shadow — not against a
second implementation of the code under test.

## 41. Ink from a road survives on the GROUND after a bank (CLOSED — SPEC.md §88.3.2.3.4)

*"Stale pixels originate from roads or lines drawn on the ground, below the
horizon. A few pixels from the line stick around, pretty often, until the
line recrosses them. Happens after banking only. Again only on the right
side of the screen."*

**FIXED.** `tests/skiesink.py` is the instrument, and it reads **8 of 60
frames leaking on the build before and 0 of 60 on the build after**, over
`rollsweep`, `bank` and `slightbank`. It costs no per-row instruction at all
— the row body it lands in is two bytes SHORTER than the one it replaces —
and `tests/skiesspan.py` is the gate that came out of getting it wrong once
in between.

### What it is

**The erase IS the span.** For a row whose KIND is unchanged from last frame,
`cs_hzrows`'s `.r` arm refills exactly the bytes LAST frame's span covers —
*"what last frame drew on the row IS the erase"* (SPEC.md 88.3.1). So ink laid
outside its row's recorded span is never erased, and it sits there until
something marks that byte again. That is the report, exactly.

The under-mark is **SPEC.md 88.3.2.3's stepped mark**, and it is one clamp:

    mov cl, [cs_wb0]        ; the ENDS are pulled CS_MKD_SLOP inside the view
    add cx, CS_MKD_SLOP     ; so the byte arithmetic below cannot wrap

Pulling an end **moves the interpolated line**, by up to the clamp itself,
all the way along it — and the `±CS_MKD_SLOP` widening was sized for the
divide's truncation, not for the clamp's own displacement. A segment whose
end sits at the view's edge, which a bank puts there constantly, marks up to
**3 bytes short of its own ink**, always on the side that got clamped.

Modelled host-side over 40,000 segments against the true Bresenham range:
**3,558 rows leak, worst 3 bytes.** Example — segment (516,99)→(167,108),
row 101: ink in bytes 52..57, mark `[44,54]`.

### The evidence

`tests/skiesink.py` checks a **single-frame** invariant and so needs no A/B
and can run in FLIGHT: on a whole-ground row (`cs_rowkind` = 1), every byte
differing from the row's ground pattern must lie inside `cs_spcur`'s pair.

| profile | roll | result |
|---|---|---|
| `rollsweep` (2 deg a frame) | moving | **11 of 60 frames, 62 bytes, ALL right of span** |
| `bank` (decaying) | moving | **10 of 60 frames, 61 bytes, ALL right of span** |
| `slightbank` | held | clean |
| `turnhold` | held | clean |
| `rollsweep`, `cs_mknostep=1` | moving | **clean** |

The roll must be CHANGING, never a byte to the left, and the box mark is
clean — which is the diagnosis three ways.

### Three detectors that could NOT see it, and why

1. **`tests/skiesstale.py`** compares the CARD with the SHADOW. Here the
   shadow itself keeps the ink and the card faithfully matches it.
2. **Incremental shadow against a forced full repaint** measures
   `cs_rowkind`'s own refill rule, not the marking — it reports the FULL arm
   having MORE ink, the opposite sign to the bug.
3. **Consecutive frames** are not the same scene by right: 88.5.2's cull
   carries SKIP COUNTERS across them, so an object can be absent from one
   frame and present in the next with nothing wrong.

### The fix: the clamp is DELETED, and a second bug was under it

**Widening alone can never work**, which is the arithmetic worth keeping: the
widening must exceed the clamp to cover the displacement AND the divide's
truncation (`W > C`), while keeping the stored low byte at or above `wb0`
needs `C >= W`. Both cannot hold, so every value of the pair is either short
of the ink or below the view.

**`cs_seg` CLIPS to the view before it marks**, so both ends are already
inside it: the clamp was a no-op on geometry and a bug on placement. It is
DELETED, `CS_MKD_SLOP` goes 3 → 4, and the setup gets six instructions
SMALLER. Modelled over 40,000 segments against the true Bresenham range,
unclamped needs W = 4 for **zero** leaks (W = 3 leaves 64, worst 1 byte).

**And the 20x regression the first attempt hit was a real latent bug**, not a
consequence of the widening. `cs_hzrows` computes the refill's start as `sub
cx, [cs_wb0]` after `xor ch, ch`, and a span starting left of the view BORROWS
— leaving `CH` = 0xFF. The byte count below it is `mov cl, dh / sub cl, dl /
inc cx`, which never touches CH, so the `rep` ran ~65,000 bytes instead of
twenty: **`cs_skyground` 25.13 ms → 490.52**, and a write far past the row. One
`xor ch, ch` in the right place fixes it — DI moving backwards is correct, the
refill may legitimately start in the row's left margin. **It was unreachable
before**, because the old clamp guaranteed the low byte never fell below
`wb0`; deleting the clamp is what exposed it.

### …and a clamp of the STORED interval was needed after all

The first build of the fix carried a reading that was wrong twice over —
*"a pair reaching `wb0−4` and `wb0+wbn+3` is inside the row on every backend
and lands in a margin that is never blitted."* It is inside the row only
while the view HAS a margin, and `cs_wx0` is `((vw − ww) / 2) & 0xF0`, so at
`CSZ_FULL` — the DEFAULT on CGA and Mode X — the view is the whole box and
there is none. And the margin is not un-blitted: `cs_blit` copies whatever
byte range the span names.

`tests/skiesspan.py` is the row that says so, and it is one line of
invariant: a stored pair must name a byte of the view. On the build with the
clamp merely deleted it reads **17 frames of 30 storing a pair outside it**,
and on `slightbank` — 33 m up, so most of the view is ground — **1,428 bytes
of ground pattern laid into the box border**. `cs_mknostep=1` is clean, so it
is the stepped mark's widening and nothing else. It runs at `CSZ_FULL`, where
the view is the whole 80-byte row and there is no border to absorb an escape:
with the clamp taken back out it stores a span reaching byte **83**, which is
three bytes of the NEXT ROW.

**The two clamps are different quantities.** Clamping an ENDPOINT moves the
interpolated line, which is this entry's defect; clamping the widened OUTPUT
moves nothing, because `cs_seg` clips the ink to the view before it is
marked, so a span has nothing to cover out there. SPEC.md §88.3.2.3.6 is the
second one: it rides inside the widening as a pair of 256-entry tables built
once a bracket, which makes the row body **two bytes shorter** than the
`sub`/`add` it replaces — and on an 8088 that loop is fetch-bound, so the
correctness fix took it from 130 clocks a row to 121.

### What the first attempt got wrong

Widening alone is **REFUSED, measured**: at clamp 3 the model needs a
widening of 6 to reach zero leaks (4 leaves 273 rows, 5 leaves 13). Built,
it sends a row's low byte to `wb0 − 3`, and `cs_hzrows` computes the refill's
start as `dl − wb0` **unsigned** — so the fill runs backwards off the view
and `cs_skyground` goes **25.13 ms → 490.52 ms**, a 20x regression far worse
than the defect. Reverted; `skies.bin` is byte-identical (`12c7b9ec`).

The instrument needed one correction of its own: it took a row's ground byte
as the row's MODE, which is the ground only while ink is a minority — a solid
polygon covering more than half a row made the mode the INK, and every ground
byte then read as a leak (54 of them on one row of `rollsweep`). The ground
byte comes from the bytes OUTSIDE the span now, which were not written this
frame by construction, and a row with fewer than eight of them is not judged.

## 42. Cyclone overflows its task stack (FIXED — its class was 192 and it is a 256 program: SPEC.md 8.7.5)

Reported as *"Cyclone is overflowing its stack… performant, near as I can
tell, until it overflows."* Paint, Missile Command and Tank were exercised
alongside it and none of them does it.

**The mechanism is already known and already written down, in
`apps/cyclone/cyclone.asm`'s `cy_kbdrain` header.** A held key refills the
BIOS's 16-entry buffer at ~10/s; the UI task takes exactly ONE key per pass
(`kernel/ui.inc`); two keys held is ~20/s against a pass rate this game's own
lock holds put near 18. A full buffer makes the BIOS **beep**, and the beep is
`call F000:E8C0` — two `loop $` delays run with **interrupts enabled** — so
every IRQ1 arriving inside it nests another `int 09h` and another beep on
whichever task stack is current. Measured on a cycle-accurate 5150 when it was
first chased: the buffer went 0 → 9 → 15 pending inside one second of held
arrow-plus-fire, and the machine halted in `sch_stkdie` with **seven nested
copies of the same 26-byte beep frame** — 182 bytes. `cy_kbdrain` is the fix
and it takes every repeat of the three keys every time it looks, because
§`cy_kbdrain` establishes there is no safe threshold.

**…and the owner reports NO BEEPS — "just sudden death" — while holding exactly
the pair this game asks for (one arrow and space).** That is the datum that
matters most in this entry, because **it eliminates the mechanism above rather
than confirming it**: the beep is what a FULL buffer produces, so silence means
`cy_kbdrain` is doing its job and the buffer never fills. The nesting cannot be
happening, and 42.2's first question is answered before it is asked.

**So something else is running away, and there is no evidence yet for what.**
What is left in the frame: a real recursion or an unbounded loop reached only in
play; an ISR path that is not the keyboard's; or simply that 1.28× is not enough
margin for the ordinary deepest chain plus a normal interrupt on a bad tick —
which the numbers below make a live possibility rather than a fallback.

**PARKED at the owner's direction** ("one thing at a time"), with the three ways
out named as the owner named them: lower Cyclone's stack usage, move it to the
256 class, or catch whatever is running away.

### 42.1 …but this branch made the margin 32% thinner, and that is measured

`docs/plans/completed/GFX-EMBEDDABLE-PLAN.md`'s wave 5 (`94dd890`) moved
Cyclone's resumable walk into `apps/os88gfx.inc`. `cy_dsc_run` used to push two
registers and far-call `OSAPI_GFX_LSTEPV`; it now pushes five and calls
`gfxe_wstepv` → `gfxe_wstep` → `gfxe_padd` → `gfxe_pput` → `OSAPI_GFX_POINTS`,
four app-side frames where there was one far call.

`tools/stkdepth.py` on the package either side of that commit:

| | before | after | |
|---|---:|---:|---|
| `cy_worker` (the slice this runs on) | **66** | **86** | +20 |
| `cy_onkey` / `cy_onclick` (deepest roots) | 76 | 96 | +20 |
| `cy_fsx_main` | 70 | 88 | +18 |

`tests/unit/t_stkclass.py` reads the consequence in one line — **`thinnest
cyclone 1.28x (86 + 64 in 192)`**. Before the conversion the same slice was
`66 + 64 in 192`, which is **1.48×**. So Cyclone went from comfortable to the
thinnest margin in the tree, level with Frotz's 1.26× (docs/plans/completed/STACK-SLOTS-PLAN.md
§12), and it did so on this branch.

**Twenty bytes is not 182**, so the beep chain remains the mechanism that can
actually exhaust a slice. But 20 of a 62-byte margin is a third of it, and a
nesting failure is exactly the shape that turns "nearly enough headroom" into a
halt — so this is a contributing cause and must not be written off as
coincidence because the other number is bigger.

### 42.2.0 THE PANEL DECODED, and it says the SP is HEALTHY

Two reproductions photographed (2026-09-09 and 2026-09-10, Hercules 720x348)
both read **`STACK OVERFLOW  TASK 04  SP 1788  CYCLONE 88`** — the same slot and
the same SP to the digit, on builds a day and several kernel changes apart.

`sch_diepanel` prints `sch_dphex4`, so **SP is HEX: 0x1788 = 6024**, and it is
the PARKED SP out of the task record, not the live one. Against the slice
table, on `kern_big`:

| | |
|---|---:|
| `sch_stacks` | 5,590 |
| slots 1–3, 128 each | 5,590 – 5,974 |
| **slot 4, 192 — Cyclone's** | **5,974 – 6,166** |
| its canary, a word at the BASE | **5,974** |
| **the parked SP** | **6,024** |

**So the parked SP is 50 bytes ABOVE the base, comfortably inside the slice**,
with 142 of 192 used. Nothing about SP is wrong. What died is the canary word
at 5,974, and `sch_switch` tests that and not SP —
`cmp word [ss:bx], SCH_MAGIC` / `jne sch_stkdie`.

**That changes the shape of the bug.** It is not a runaway and not a slice that
is simply too small for its resting depth: it is a **transient excursion below
the base that had already unwound** by the time the switch looked. The SP in
the panel can never show it, and the same SP twice says the excursion happens
at a repeatable point rather than at random.

**And it is why the symptom keeps changing.** Below 5,974 is slot 3's slice.
What the excursion destroys is whatever lives there, which differs per build
and per session — so one build panics on a clean screen, the next panics on a
corrupted one, and the third does not panic at all but **reboots with a full
BIOS memory count**. That last one is not a third bug: an 8086 has no fault to
triple, so "hard reboot" means execution reached `F000:FFF0`, which is what a
`ret` into a corrupted return address eventually does.

### 42.2.1 What was ruled OUT here, and the term that is still missing

Measured on MartyPC (`os8088_5150_herc_gla`), Cyclone launched and **played
with the arrow key and the spacebar HELD** — the control docs/FIELD-NOTES.md 40
is about, `key(down=True, up=False)` so the BIOS repeats them:

| | |
|---|---:|
| slot 4 high water, 21 s of held keys | **114 of 192** |
| …and it is FLAT: it reaches 114 and stays | |
| slot 1 (the idle task) | 54 of 128 |
| slots 2, 3 and 5–13 | never spawned |

So **78 bytes were still free and nothing here can spend them.** The static
chain agrees: `tools/stkdepth.py --from cy_worker` is 86 bytes to the
`OSAPI_GFX_POINTS` far call, and the kernel below that call measures **26**
(`gfx_points`' cost to its caller, SPEC.md 5.6.9.3) — 112, which is the 114
observed.

Ruled out with it: **no indirect dispatch** for `stkdepth` to miss (Cyclone's
tables at `cy_sh_*`, `cy_pn_*` and the window template are data and callbacks,
not a state machine's jump table), and **`Z` and `J` in the status line are
inventory** — a held zapper and a held jump — not states with code behind them.

**The missing term is worth ~80 bytes and this box cannot produce it.** The
candidates, in the order they are worth spending a field run on:

1. **The interrupt floor on the machine that reproduces.** MartyPC's is ~32;
   docs/plans/completed/STACK-SLOTS-PLAN.md §9 measured **118 on a real 5150,
   100 with `MOUPRIV`**. `make stkdiag` answers it on ANY machine — boot it,
   touch nothing for 30 seconds, photograph the panel — and that one number
   either closes this or eliminates the whole line.
2. **A stack UNDERFLOW in slot 3's task**, which would write AT 5,974 rather
   than below it: slot 3's slice TOP *is* slot 4's canary address, so one `pop`
   too many next door kills this canary and leaves Cyclone's SP innocent —
   which is exactly the evidence. Slot 3 is unspawned on this box, so nothing
   here can test it.
3. Only then the ones docs/FIELD-NOTES.md 42.2 already lists.

### 42.2.2 The reproducing machine is now IN THE TREE, and it is not the one this was reasoned against

`vm/pc5150` (`make pc5150`, docs/FIELD-MACHINES.md) is the reporter's own
86Box config, and reading it moves both candidates above from *"spend a field
run"* to *"of course"*. It is an **IBM PC 5150** on the 10/27/82 ROM with, all
at once: a **Sound Blaster 2.0**, an **NE1000**, an **AST SixPakPlus** whose
MM58167 makes it a §37.90 **rung-2** machine, and an **ST-225 on an ST11M with
an option ROM at IRQ 5**. The container's MartyPC has **none** of the four and
boots an XT ROM.

That matters twice over, and in the two places the reasoning was weakest:

- **Candidate 1 is no longer a guess about a floor.** Four devices the model
  does not have is four chances for an ISR the floor was never measured with.
  The ST11M's ROM in particular puts a live IRQ 5 on a machine whose stkdiag
  reading — on the *iron* 5150, which has the same controller — was taken with
  the drive idle.
- **Candidate 2 stops needing slot 3 to be hypothetical.** SOUND.DRV and
  ETHER.DRV are both refused by default (§51.3), but this machine's A: floppy
  is **writable**, so a Control Panel tick from any earlier session persists in
  `SYSTEM.CFG` and the next boot mounts the driver — and a mounted driver is
  exactly the thing that puts a task in a slot the container never spawns.
  **So the first question to ask the reporter is what `SYSTEM.CFG` on their
  boot floppy says**, and it is cheaper than any run.

Neither is confirmed. What is confirmed is that the box this was diagnosed on
differs from the box that reproduces by four devices.

### 42.2.3 Candidate 1 is MEASURED, and it is a quarter of the term

**`make stkdiag` has been run on the reporting machine**
(`docs/reports/STKDIAG-PC5150-2026-09-10.md`, arm 1, Hercules 720). The floor
is **52 against the container MartyPC's 32** — and the BIOS is controlled, the
reporter having supplied the genuine `27 OCT 82` ROM so that MartyPC ran the
same 8,192 bytes: `ROM int08` reads **17 on both** and the floor did not move
with it.

**So the machine costs a slice twenty more bytes than the box this was
diagnosed on, and twenty is not eighty.** `cy_worker` read **114 of 192** on
MartyPC with both keys held, flat; +20 is ~134 and leaves 58 free. Even
against the **iron** 5150's 64 — the deepest floor any machine here has
recorded — it is ~146 and 46 free.

**Candidate 1 is therefore closed as the explanation and kept as a term.**
Which is what the run was for: it was the cheapest of the three and it was
going to either close this or eliminate the line, and it eliminated it.

Two things the same panel rules out on its own rows, both on a third machine
now: **the mouse** (`+mouse` is +2 here, +0 on MartyPC — SPEC.md 9.10 working,
and the ISR's own stack reads the 30 STACK-SLOTS-PLAN §9.9.1 predicts for a
1bpp adapter) and **the keyboard** (`+keys` +0, which is §9.4's finding again).

### 42.2.4 What is left, and it is the sound card

The reporter runs **fresh OS disks every time**, so no `SYSTEM.CFG` survives a
session and **no driver is ever ticked** — the hard disk is present and never
mounted, the NIC never brought up. The one exception is `SOUND.DRV`, which
**auto-mounts** because the Sound Blaster 2.0 is real on that machine and the
boot probe finds it.

That is the term nothing has measured, and the reason is structural rather
than an oversight: **`stkdiag` never plays a note.** The 52 above is the sound
driver *resident and idle*. Cyclone plays sound continuously. A driver frame
landing on `cy_worker`'s slice mid-walk is something that

- the panel cannot see, because the panel is silent;
- the container cannot produce, because **no machine in
  `os8088_machines.toml` pairs Hercules with a Sound Blaster** — all seven SB
  machines are CGA or VGA; and
- no arithmetic off these numbers can bound, because it is a nesting depth and
  not a constant.

It also fits the varying symptom better than the floor does: a sound IRQ is
**asynchronous to the walk**, so whether it lands inside the deepest chain is
a race — which is a clean account of why the same build panics cleanly one
time, corrupts the screen another, and reaches `F000:FFF0` a third.

**The next run is therefore a Hercules + Sound Blaster machine**, which has to
be written before it can be booted (four lines of TOML, plus its GLaBIOS twin
— `tools/martypc/configs/os8088_machines.toml`'s own rule), and then the
repro re-taken with Cyclone actually making noise.

**That run has been taken and 42.2.5 is it.** Two things in the paragraphs
above are wrong and are corrected there rather than here, because how they
were wrong is the useful part: this was written as an **IRQ 7** completion,
and it is IRQ **0**; and the *"114 of 192"* every one of these sections
reasons against is **the title screen**.

**A note on this branch's own contribution.** ~~SPEC.md 5.6.9.3's first
version made `gfx_points` cost its caller **34 bytes where the routine it
replaced cost 26** — a `push ds` and a wrapper. On a margin this thin that is
material, and the reboot symptom appeared on that build.~~ **MEASURED WRONG —
42.2.5's sweep reads `189c8c7` and HEAD at 164 alike**, so those 8 bytes never
reach the maximum and `gfx_points` is not the bottom of this chain. Struck
through rather than deleted because it is the obvious inference and the next
reader will draw it too. What is left standing is the branch's *other*
contribution, wave 5's +20 to `cy_worker` (42.1) — and the same sweep makes
that one **bigger** than it looked, since the inlining has since given 18
back.

### 42.2.5 REPRODUCED — and it needs the card

**`os8088_5150_herc_sb` now exists** (the first MartyPC machine here pairing a
1bpp adapter with a sound card) and the reporter's own procedure reproduces on
it. Full account and provenance:
`docs/reports/CYCLONE-STACK-2026-09-10.md`.

| MartyPC, IBM `27 OCT 82`, Hercules 720 | slot 4 (`cy_worker`, 192) | outcome |
|---|---|---|
| **no card** | **164 / 192** — three runs, identical to the byte | **survived 3/3** |
| **SB 2.0** | 184, 188 sampled before the panel | **PANIC 3/3** at 12 s, 10 s, 23 s |

`STACK OVERFLOW  TASK 04  SP 172C`.

#### Two harness faults had to be fixed first, and one invalidates this document's own arithmetic

**The repro was never in the game.** Cyclone's title screen says
`PRESS ENTER TO START`; the script pressed Space and sat on the title for the
whole of every run. **So "114 of 192, flat" in 42.2.1 is the TITLE SCREEN**,
and every piece of arithmetic in 42.2.1 and 42.2.3 built on it — *"+20 leaves
58 free"*, *"+32 leaves 46 free"* — was subtracting from the wrong number.
In play and with no card it is **164**, and the machine is **28 bytes** under
the canary before anything else happens. That is why this document kept
hunting for eighty bytes it did not need.

The reporter's procedure named the step (*"Enter game"*) and the script
skipped it. **A repro that does not perform every line of the report is not
the repro**, and it fails silently by producing plausible numbers.

**And the machine did not exist.** Seven MartyPC machines carry a Sound
Blaster and every one is CGA or VGA.

#### The mechanism, from the driver's source

An SB 2.0 carries an OPL2, so `drivers/sound/sound.asm`'s attach publishes
**both** halves — `DSV_TONE = opl_tone` and `DSV_TICK = sbl_tick` — and both
are entered **from inside IRQ 0 at IF = 0, on whichever slice the tick
interrupted**:

- **`DSV_TICK`** is called from `snd_tick` **every tick, playing or not**
  (`drivers/os88drv.inc` says so at its definition), so it is a constant
  addition to every slice. It is what the idle floor sees: 32 without a card
  and 52 with one.
- **`DSV_TONE`** turns `snd_tone_out`'s *near tail jump* to `spk_tone` into
  `drv_svc_call`, **a far call into the driver**, on a path its own comment
  says is *"reached from `snd_tick`'s expiry path, so this can run INSIDE IRQ0
  at IF=0."* Cyclone fires a tone every few frames.

**So it is IRQ 0 and not IRQ 7.** Cyclone plays no stream and the card's own
interrupt is not on this path at all; what changes is that two service
pointers which are null on a cardless machine are not null here.

The second one is **asynchronous to the walk**, which is the account of the
varying symptom this document has wanted since it opened: identical starts
panic at 12 s, 10 s and 23 s, and a clean panel, a corrupted screen and a hard
reboot are **three landing sites rather than three bugs**.

#### The two SPs are one event at two moments

Slot 4 runs **5,974 … 6,166**. The field panel's `0x1788` = 6,024 is **50
bytes above** the base and this repro's `0x172C` = 5,932 is **42 below** it —
`sch_diepanel` prints the *parked* SP, so whether it reads healthy is a matter
of when `sch_switch` looked. 42.2.0 decoded a healthy SP off a machine that
had just died, and that is why.

#### Both service calls are terms, and neither alone is enough

`snd_rt_card` tests `cmp byte [snd_route], SND_RT_SPK`, so writing **1** to
`snd_route` sends tones back to `spk_tone` while `snd_tick` keeps calling
`DSV_TICK` every tick. One byte at run time, no build:

| HEAD, Hercules 720 | slot 4 of 192 | free | outcome |
|---|---|---|---|
| no card at all | **164** ×3 | 28 | survived 3/3 |
| SB 2.0, `snd_route = SPK` — `DSV_TICK` only | **180** ×2, identical | 12 | survived 2/2 |
| SB 2.0, both | 184, 188 | — | **PANIC 3/3** |

**`DSV_TICK` costs 16 bytes of every slice, always** — 28 of margin becomes
12 — and **`DSV_TONE` costs more than the 12 that are left**, arriving
asynchronously. `snd_route = SPK` is reachable from Control Panel → Sound and
is a **diagnosis, not a fix**: 12 bytes is thinner than any declared class
margin in the tree, and it takes the FM tier from everything else.

#### The history sweep: the inlining took 18 bytes OFF, and the 34-byte build is invisible

`cy_worker`'s peak on the **no-card** machine — a deterministic number where
the SB machine is a coin toss. One worktree per point, each given the same
period ROM so the BIOS is held fixed.

| point | commit | slot 4 of 192 | free |
|---|---|---|---|
| wave 5 — the walk moves into the apps | `94dd890` | **182** | 10 |
| the commit before the `gfx_points` inlining | `0d43c61` | **182** | 10 |
| the inlining, 34-byte caller cost | `189c8c7` | **164** | 28 |
| HEAD, caller cost back to 26 | `e6f6fc0` | **164** ×3 | 28 |

Between `0d43c61` and HEAD **the only code change in the whole tree is
`kernel/vga12.inc`**, so the attribution is clean: the `gfx_points` inlining
took **18 bytes off Cyclone's deepest chain**, by removing the nested
`gfx_ls_addr` / `gfx_rowbase` / `gfx_ls_box` frames *below* it.

**And that corrects this document about its own branch.** 42.2.1 ends with a
worry that SPEC.md 5.6.9.3's first build cost its caller 34 bytes where the
old routine cost 26, *"and the reboot symptom appeared on that build"*.
Measured, `189c8c7` and HEAD are **both 164**: those 8 bytes never reach the
maximum, because **`gfx_points` is not the bottom of Cyclone's deepest
chain**. A frame added at its entry is not on the critical path. The worry was
reasonable and it was wrong.

What the sweep does confirm is the reporter's observation that pre-inlining
builds *"seemed to do it more often"* — **10 bytes of margin against 28**, and
a card asks for 16 before a tone is played.

#### THE FIX: the class was 192 and Cyclone is a 256 program

**Declared `OS88_STACK_256`** (SPEC.md 8.7.5). Measured on the machine that
reproduces — SB 2.0, Hercules, the same held keys:

| Cyclone's class | slot | peak | free | outcome |
|---|---|---|---|---|
| `OS88_STACK_192` | 4 | 184–192 of 192 | — | **PANIC 3/3** |
| **`OS88_STACK_256`** | 10 | **184, 188, 192 of 256** | 64–72 | **survived 3/3** |

**192 was short by a handful of bytes**, which is why the symptom needed a
sound card to appear at all and why it looked stochastic: the true peak sits
*on* the old canary.

The reporter's framing is what found it — *Cyclone is a worker-heavy app that
takes over the whole machine* — and the tree already said so twice. **SPEC.md
67.5.5's stack analysis is written against "a worker gets 384 bytes"**, from
before the classes existed, and **Missile Command, PacMan, Tank and TameGram
all declare 256 already.** Cyclone was the outlier among its own siblings, and
the declaration had disagreed with its own design record since the day the
classes landed.

How it got there is worth keeping, because no step is a mistake alone: the
class was set at `b9bb040` when `cy_worker`'s chain was **66** (66 + 64 = 130,
1.48×); wave 5 took the chain to **86** (150, 1.28×); `t_stkclass` printed
*thinnest in the tree* on every build after that and it was read as tight-but-
passing rather than as a class to revisit. And the gate could not have caught
it — it compares a static chain against a documented 64-byte floor, and both
terms were right. **The floor is a property of the machine's configuration,
not of the kernel**, and `DSV_TICK` alone moves it 16.

#### What it does not settle

- **The margin without a card.** 164 of 192 was the *good* case, and it is
  still 162 of 256 now — the class change buys headroom, it does not make the
  chain shorter. 42.2's item 2 (give the walk chain its 20 bytes back) stands
  on its own merits, it is simply no longer urgent.
- **The Wire is the new thinnest** at 1.30× (84 + 64 in 192), on the same
  optimistic floor. It is a network app rather than a game, so it is unlikely
  to meet a mounted `SOUND.DRV` under load — but that is an argument, not a
  measurement.
- **The old framing, kept for the record:**
  `cy_worker` runs at **85% of its class with no card in the machine**, and
  the card is a further 16 before anything is audible. That is a margin
  question first and a driver question second — which puts 42.2's item 2
  (give the walk chain its 20 bytes back) at the top rather than the bottom,
  and makes wave 5's +20 the single largest lever on the table.
- **What `t_stkclass` bills.** It reads `cyclone 1.28x (86 + 64 in 192)` — an
  86-byte app chain on an assumed **64-byte floor**. Measured in play with no
  card the slice reads 164, and slot 1 under load reads 70 rather than its
  idle 32. **The gate's floor term is optimistic before a card exists and
  models no driver at all**, so it was never going to catch this.
- **P0.** `b9bb040`, before wave 5, cannot be run with this harness at all:
  `tools/os88ui.py` did not exist yet, and hand-rolling clicks at remembered
  coordinates is what that layer exists to stop. Dropped rather than faked.

### 42.2 Where to look, in order

1. ~~**Does `cy_kbdrain` still run on every path?**~~ **ANSWERED NO by the
   absence of beeps** — see above. Left here struck through rather than deleted,
   because it is the obvious first guess and the next reader will have it too.
2. ~~**Give the walk chain its 20 bytes back.**~~ **DONE, and it was SIX, not
   twenty.** The chain is **82 -> 76 bytes** and `tools/stkdepth.py` now reads
   *"0 bytes of 76 are pushes the routine never needed to make"*. What was
   actually there, against what this line claimed:

   - **`gfxe_padd`'s flush frame, +10 -> +6.** It banked AX, CX, DX and SI
     across `gfxe_pput`; only CX and SI needed it. `gfxe_pput` clobbers those
     two - its own `mov`s - and `OSAPI_GFX_POINTS` beneath it **preserves
     every register** (SPEC.md 5.6.9), so AX and DX were dead saves. **This
     one is in the shared library**, so it is 4 bytes off the deepest frame of
     every walk in the tree - Cyclone, Missile, Tank, Paint and wire - and
     four instructions off the flush path.
   - **`cy_web_repair`'s `push si`, 2 bytes.** Dead exactly as named: the
     routine never writes SI and `cy_web_lane` restores it.
   - **The tail `jmp` does not work as written.** `.full` re-enters
     **`gfxe_padd` itself** after making room, so it cannot tail-jump to
     `gfxe_pput`; the saving had to come from the bank list instead.
   - **`cy_dsc_run` is REFUSED, and the claim was wrong twice.** It is
     **seven** pushes today rather than five, and all seven are required:
     `gfxe_wstepv` documents *"clobbers everything but the segments"*. Moving
     them to the callers would gain nothing either - the same bytes are on the
     stack when the bottom of the chain is reached, which is where the maximum
     is taken.

   **The lesson is that `stkdepth` cannot see a dead save whose callee is
   outside the image.** It flagged `cy_web_repair`'s 2 and not `gfxe_padd`'s
   4, because `OSAPI_GFX_POINTS` is *"not in this image - API/kernel"* and a
   tool must assume the worst there. The published contract is what finds the
   other one, and only a person reads that.
3. ~~**Only then consider the class.** Cyclone is on 192; the next class is 384
   and `SCH_STACK` is the ceiling. Moving it is the expensive answer and the
   one that hides both of the above.~~ **THIS WAS THE ANSWER, and the sentence
   is wrong twice.** The classes are 128/192/**256**/384 (SPEC.md 8.7), so the
   next one up is 256 and not 384 — *"the expensive answer"* was an artefact of
   a ladder with a rung missing from it. And it hides nothing: items 1 and 2
   are worth 22 bytes between them against a slice that needed ~28 more, so
   neither would have fixed this and the pair of them together would not
   either. **Reclassifying was the cheap answer and the correct one**
   (SPEC.md 8.7.5); items 1 and 2 stand on their own merits and are not
   urgent.

`tools/stkwater.py` measures what a slice actually reached, and
`tools/cyunwind` is Cyclone's own unwinder from the first investigation —
neither needs an emulator run to be set up specially.

---

## 43. Prince of Persia will not start under the WHOLE-MACHINE arm (CLOSED — three causes, and the third was `kern_dos` crossing a head on a ROM that cannot: SPEC.md §96.44.14)

Reported off an 86Box 286 with an OTI-067 VGA, three floppies and a 128MB VHD
(so the third floppy lands on D:, §18.7.1): *"Prince, when run from a
subdirectory, goes back to `Please Insert Disk in Drive D:`. The drive is
right — but I think the CWD being given to it probably doesn't have the
subdirectory."*  And separately: *"I'm also unable to launch it through our
console, it just prints `Bad command or file name`. I tried it from B: and
D:."*

**THE SUBDIRECTORY IS NOT THE VARIABLE, and that is measured four ways.** The
report's shape points straight at the folder and the folder is innocent:

| | windowed | whole machine |
|---|---|---|
| launched from the volume ROOT | runs | **"Unable to find necessary files"**, exit 1 |
| launched from a SUBDIRECTORY | runs | **"Please insert Prince of Persia Disk 1"** |

What made it look like the folder is the `.LNK`: a shortcut records the arm,
so double-clicking it takes the third one while opening `PRINCE.EXE` directly
fits windowed and takes the first. On the reporter's machine the game is in a
subdirectory AND has a shortcut, so the two moved together.

**TWO CAUSES FOUND AND FIXED.** Both are §96.44 seam defects and each is
reproduced by a registered row now:

1. **§96.44.2.1** — the core and the window laid the shared bss out four bytes
   apart, because `DBSS DOS_B_DVCWD, 2 * DVOL_MAX` sized a core table from a
   per-host constant. That is the console half of the report, entirely: every
   program typed at the parted box's prompt answered `Bad command or file
   name`, root or subdirectory. `tests/unit/t_dosbss.py` rule 4.
2. **§96.44.10** — `OSAPI_FILE_PATH` is an X cell, so `api_x` puts the
   caller's DS in ES; `kern_dos` bound the door with a far call straight at
   `dsk_path_x`, which writes to ES:DI and never reloads ES. The program's own
   path in the environment came out as `B:` — the drive and nothing after it.
   `tests/kdcwd.py`. With it fixed, Prince opens `B:\PRINCE\prince.dat`
   correctly and still fails.

**WHAT IS RULED OUT for the third**, each on a measurement rather than on
reasoning, all taken on one machine with one disk and the two arms as the only
variable (`os8088_5150_herc_sb_720_gla`, `build/os8088-720.img`):

- **the file's BYTES** — `RDSUM.COM` reads `PRINCE.DAT` whole in 512-byte
  chunks and answers `len=00000CCE sum=BB77` under both arms, which is what
  the host computes off the image;
- **the SMALL read Prince actually makes** — `tests/dostrap/rdsmall.asm` reads
  six bytes at offset 0, six more, six after a rewind and 512 after a rewind,
  and prints every one: `DC 0A 00 00 F2 01` under both arms, identical;
- **the current directory, the drive, a bare-name open and the program's own
  path** — `tests/kdcwd.py`, all four identical since fix 2;
- **the command tail, the environment and its count word** — `DOSARGS.COM`
  under both arms: `COUNT 0 / ARGS (none) / TERM 0 / SET BLASTER=… / MYPATH
  B:\DOSARGS.COM`;
- **the registers a program is handed** — `cs:ds`, `ss:sp` and `PSP:0002`
  printed at entry: a `.COM` gets `DS:FFFC` on both, and the only difference
  is the block top, which is memory it has more of;
- **the amount of memory** — capping the arena to 200 KB on the Memory page
  (`dos_memkb`, which rides the handover) changes nothing;
- **the drive's CYLINDERS** — `os88fat.py reach` says the machine reaches the
  whole image, and `PRINCE.DAT` is at cylinder 16 either way.

**WHERE IT DIVERGES, to the instruction.** Both arms make the same fourteen
calls and then part company on the fifteenth. Prince opens `prince.dat`, reads
six bytes — `DC 0A 00 00 F2 01`, which is the words `0x0ADC`, `0x0000`,
`0x01F2` — and then:

| | windowed | whole machine |
|---|---|---|
| `AH=48h` paragraphs | `0x0026` (38) | `0x0179` (377) |
| `AH=42h` seek to | `0x0ADC` = 2780 | `0x1733` = 5939 |
| `AH=3Fh` read | `0x01F2` = 498 | `0x1728` = 5928 |

2780 + 498 is **3278, the file's exact length** — the windowed arm takes the
two words straight out of the header and reads the index off the tail. The
whole-machine arm's two numbers are **nowhere in the file**, and 5939 is past
the end of it. So the six bytes are right and what is computed from them is
not.

**THE INSTRUMENT WAS WRONG, AND FIXING IT IS MOST OF WHAT THIS ROUND ADDED.**
`os88intmon`'s `--time` computed the return site as `CS:IP + 2` — but
MartyPC's INT breakpoint stops INSIDE the handler with the vector already
fetched, so `cs:ip` is the handler's entry (the same `05C9` for every call in
both arms, which is the tell) and `+2` is two bytes into the handler. Every
`--time` figure it ever printed was ~20 cycles, and the TD3 run that reported
`0.0 ms in-BIOS` was the same defect on `int 13h`. The return site is the
three words the CPU pushed at `SS:SP`. With that right, the stop is already
being made, so the ANSWER costs one `regs` call — and an entry-only trace
cannot see this family of failure at all, which is the point: the answers were
what needed comparing.

**AND THE ANSWERS ARE IDENTICAL THROUGH THE SIX-BYTE READ.** Fourteen calls,
both arms, every return register and flag:

    AH=30h 1E03 · 4Ah 4A5A · 30h 1E03 · 35h/25h · 44h A0C0 80C0 80D3 80D3 80D3
    19h 1901 (drive B) · 47h AX=0100 CX=0001 CF=0 · 3Dh AX=0005 (the handle)
    3Fh six bytes, AX=0006, CF=0

Then `AH=48h` asks for 38 paragraphs on one arm and 377 on the other, with no
call in between. **The ruled-out list above is now exhaustive over everything
`int 21h` can say**, and what is left is not a DOS answer.

**THE THREAD IS `SP`, AND IT IS NOW EXACT.** Single-stepping the program
itself — stop at an `INT 21h`'s return, which `do_time` already runs to, then
`step` — puts a number on what was a shape. At the open's return, before the
six-byte read:

    windowed   SP=756E  BP=75C0   and every buffer at 75B8, 7574, 75DA
    kern_dos   SP=757A  BP=75CC   ...and at 75C4, 7580, 75E6

**`SP` differs by exactly 12** — six words — and every "twelve-byte pointer
shift" in this entry is that one fact seen through `BP`. Prince's data
pointers are `[bp-n]` locals, so they move with it and nothing is corrupt:
the program is simply **six pushes deeper** under `kern_dos` than under the
window by the time it opens its data file, and the numbers it then computes
come out of different locals.

`dos_exe_setup` takes `SS` and `SP` straight from the MZ header with no clamp,
so the loader hands both arms the same stack. Six words is a CALL DEPTH, not a
loader difference: somewhere between the program's first instruction and its
`AH=3Dh`, one arm takes a branch the other does not — three nested calls, or a
retry.

**So the next step is bounded and mechanical**: step from the program's entry
(a breakpoint at the `CS:IP` in its own MZ header) and find the FIRST
instruction where `SP` parts. Every `INT 21h` answer before that point is
already known identical, so whatever the branch tests is something the program
read without a call — and §96.21.4 is the list of those.

**What it is NOT**, and this cost a run each to establish: not the PSP (all 256
bytes diffed, and the eight fields §96.21.4 fills are correct on both arms —
`[PSP:0002]` is `PSP + 0x2B13` on both after the program's own resize, and
`[PSP:0008]`'s far-call segment lands on `PSP:0050` on both, by 8086
wraparound on the low one); not the BDA (9 of 128 bytes differ and they are
the tick count, the floppy motor state, the cursor and `MEM KB`); not the
environment, the command tail or the program's own path; and not the file
layer — `tests/dostrap/rdsmall.asm` reads the same six bytes into a POISONED
buffer and reports the span actually written, which is 6 on both arms, from
the real disk at LBA 300.

**A THIRD instance of §96.44.10's CLASS was found looking for this and is
fixed** (§96.44.12), though it is not this bug: `dos_k_find` binds
`dsk_find_x` directly and so passed it **whatever `AL` the core was holding**,
where `AL` is §19.6.1's fence between a package and a driver — a stray 1 shows
a DOS program `SYSTEM.CFG` and the kernel's own files — and left
`[dsk_fdraw]` at whatever `api_file_find_raw` last set, which reports a
compressed file's PACKED size where the program will be handed its expanded
one. The Prince disk has neither a hidden file nor a compressed one, so it
changes nothing here; it is in the tree because the CLASS is what keeps
costing this program, and §96.44.12 is the class written down with the gate
that would catch the fourth.

**To reproduce**: `make kdostest`, then `build/os8088-720.img` in A: and a
720KB Prince disk in B: on `os8088_5150_herc_sb_720_gla`; open the box, Setup
→ Memory → the third arm, Return, type `B:\PRINCE.EXE` in the path box, Run,
Proceed. `tools/os88intmon.py` armed right after Proceed catches the whole
startup in 114 calls.

### 43.1 …and the cause on the reporter's own machine is the BIOS, not the disk (SPEC.md §96.44.14)

**The reporter narrowed it, and the narrowing is the finding.** A photograph
of the screen settled what the message was — `Please insert / Prince of Persia
Disk 1 / into Drive B: / and press <ENTER>`, which names **the right drive**
and is that program's *retryable* prompt rather than its `Unable to find
necessary files` bail-out. So it is an open or a read failing while Prince is
standing exactly where it should be. Then, one variable at a time on the same
286:

| changed | result |
|---|---|
| ENTER at the prompt | comes straight back — **consistent**, not a transient |
| `PRINCE.EXE stdsnd` (PC speaker instead of the Sound Blaster) | same prompt |
| the same disks and program on an **IBM 5150**, three floppies and the disk | **works** |
| 286, one 360KB + one 720KB drive, no third floppy | same prompt |
| 286, **both drives 720KB** — media and drive matched | same prompt |
| 286, hard disk removed | same prompt |

That leaves the CPU and the ROM, and this file already knew which: **note 31
measured MR BIOS 286 (86Box `mr286`) as a ROM that will not cross a head** —
it answers `CF = 0` for the whole request and transfers the first half only.

`kern_dos` was crossing one on every machine, with nothing behind it:
`boot_cylrun` is a WORD the loader writes after §18.93.1's canary, and in
`kerndos/kdshim.inc` it was declared **`resb 1`** with `kd_top` — the bump
allocator's ceiling — declared next, so `dsk_geom_check`'s `cmp word` read the
byte plus `kd_top`'s low byte. Measured inside a live `kern_dos`: `kd_top` =
`9DC0`, the word = `C000`, `[dsk_cylrun]` = **1**. Fixed as `resw 1`, with
§96.44.14.1 carrying the kernel's own verdict across in `KDL_CYLRUN` so the
machines that earned §18.91.1's cylinder run keep it; `soak -k kdcylrun` is
the gate and it goes red both ways.

**CONFIRMED ON THE MACHINE**: *"That was it - confirmed loading prince on the
286 mrbios machine works!"* No emulator here can show the symptom — GLaBIOS,
SeaBIOS and MartyPC all cross a head correctly — so the row reads the CELL
rather than looking for corruption, and the 286 is what closed it.

**What is worth keeping either way**: `PRINCE.EXE` carries a 25-entry table
mapping each data file to a disk number, so *"Disk 1"* is exactly
`PRINCE.DAT`, `DIGISND1.DAT`, `DIGISND3.DAT`, `IBM_SND1.DAT` and
`MIDISND1.DAT` and nothing else — every video set is Disk 2. And it takes
command-line switches, which is what made the sound arm above a one-word test:
video `vga mcga tga ega hga herc cga`, sound `stdsnd adlib covox gblast ibmg
sblast tandy`, plus `bypass` and `megahit`.

## 44. A WHEEL mouse can never win the packet contest — its fourth byte broke the run (FIXED: SPEC.md 9.5.4, and `MOU_IDMAX` on the way: SPEC.md 9.4.1.1)

Reported on a **100 MHz Pentium**. The mouse is a PS/2 part with a passive
PS/2-to-serial adapter on it — a "combo" or "hybrid" mouse, which chooses its
protocol at power-up. os8088 does not find it. A plain serial mouse on the
same machine is found and works.

**The reporter's own workaround is the finding, and it is worth more than the
symptom:**

> "If I plug in that [plain serial] mouse first and then plug in the hybrid
> mouse later, the hybrid mouse does work in that case."

So the part is not broken, the adapter is not miswired, and the port is fine.
What differs is the STATE OF THE PORT the mouse is plugged into. A port the
kernel has settled — `mou_lockon` has run, `[mou_seen]` = 1, `[mou_hpst]` = 2 —
differs from a port it is still hunting on in exactly three ways (SPEC.md
9.4.6.5 enumerates them and the round below does each in turn):

1. **DTR/RTS are up and STAY up.** Until a packet arrives, `mou_hotplug`
   power-cycles every port every `MOU_REPOLL` — §9.4.1's own arithmetic is
   **12 ticks of every 58 with the mouse dead**, for ever. A part that needs
   more than the ~2.85 s of stable power each cycle leaves it to finish
   powering up and choosing a protocol is reset before it can ever speak.
   **This is the candidate the report fits best**, because the workaround
   removes it completely.
2. **The low hold is `MOU_RSTLOW`, ~165 ms** — a constant sized for the period
   parts §9.4 names, not for a part that makes a mode decision at power-up.
3. **The contest is over.** A settled port needs no packets; an unsettled one
   on a two-port machine owes `MOU_LOCKN` = 8 clean ones in a row, and every
   reset edge calls `mou_newround` and throws the run away.

**Not yet ruled out, and only the machine can say**: that the part chooses
**PS/2 mode** and drives the serial RX line not at all. Nothing on a UART can
tell that from a dead mouse — SPEC.md 9.4.6.5's `msr` column is the only hint,
and it is a hint.

### 44.1 What was built for it, and why the old table could not answer

`make MOUDIAG=1` was the obvious instrument and **it would have said nothing
loudly**: every row of it is about the identify window, which is 1.2 s into a
boot and never runs again, and `idn 00 / b0 00 / ident 0` on both ports is
exactly what it prints for a machine with no mouse plugged in at all.

SPEC.md 9.4.6.5 adds the half that is about the WIRE and the rest of the
session — `rx`, `err`, `msr`/`mcr` read live, the last four bytes and `dt`,
the ticks from our own rising edge to the port's first byte — plus a **round**
that removes the three differences above one at a time, 15 s each, and freezes
the moment a packet arrives so the phase left on the glass is the verdict.

**`rx` is the fork the whole report turns on.** 0000 on both ports after a
full round says the mouse has never put a byte on the wire and the fault is
electrical or is the part's own mode choice; anything else says it talks and
this kernel does not believe it, which is a different investigation with a
different fix.

### 44.2 The trap this must not fall into

`mdb_pin` raises DTR/RTS through the poller's own state 3 and arms the drain
exactly as `.low` does, so **a phase change can never strand DTR low**. That is
§9.4's trap in the one shape that would leave the reporter worse off than the
bug they reported: a mouse unpowered for the session rather than merely
unfound.

### 44.3 DIAGNOSED — one photograph, and it is the documented degradation

`MOUROUND=1`'s panel came back off the reporter's machine and named the cause
outright. **SPEC.md 9.4.1.1 is the reading**; the short form is that the mouse
is on **COM2**, it answers our rising edge with **`'M'`**, and it then sends
**69 bytes** where `MOU_IDMAX` was **8** — so `mou_idjudge` threw out a mouse
that had already passed rule 2, `[mou_idany]` stayed 0, and `mou_hotplug`
power-cycled it every `MOU_REPOLL` for the whole session (`cyc 000A` — ten
edges by the time of the photograph).

**It was written down as acceptable before it was a bug.** SPEC.md 9.4.1 said
in as many words: *"a mouse whose burst is longer than `MOU_IDMAX` (a verbose
PnP ID) fails rule 3 and gets exactly today's behaviour — no stand-down, no
threshold drop."* The degradation had a name, a mechanism and a predicted
symptom, and none of that made it visible until a panel printed `idn 45`.

**What made the photograph conclusive was that its numbers check each other.**
69 bytes at 1200 7N1 is 9.42 ticks of line time, and the panel's own `last`
column independently put the final byte at tick 10 — so the count is real
bytes at the programmed rate, which `err 00` over all 667 then confirms from
the UART's own error bits. Neither figure alone would have carried it.

**And the instrument found a defect nobody was hunting**: `MOU_DRAINT` was 9
ticks against that 9.42-tick burst, so the drain ceiling expired before the ID
finished and its last ~3 bytes reached the packet decoder as fake motion.
Both constants are now cut from one quantity - the line time of `MOU_IDMAX`
bytes - so they cannot drift apart again.

**Still open**: whether this part streams at all once the resets stop. The
round pinned DTR/RTS for 138 seconds with `[mou_need]` at 1 and saw no byte
(`dt FFFF`, cursor still homed), but it is not known whether the mouse was
moved in that window. `rx` on row 2 answers it in one number.

### 44.4 …and the SECOND photograph, which is the actual defect

The `MOU_IDMAX` build went back and the reporter moved the mouse. **It still
did not work — and the panel said why in one column.**

```
row  base  idn  b0   last   idt  nd  run      row   rx   err msr mcr   b0 b1 b2 b3    dt
  0  03F8   00  00   FFFF    0    8   0         0  0000   00  00  0B   00 00 00 00  FFFF
  2  02F8   45  4D   000A    1    8   0         2  00A9   00  20  0B   00 00 3F 43  0000
idany 1  port 0  seen 0  hpst 0   cyc 0000      win open 0003  used 000E of 0013
```

**9.4.1.1's fix worked exactly as designed** — `idt 1`, `idany 1`, `hpst 0`,
`cyc 0000`: the port identified, the poller never fired once, and the window
closed early at 14 ticks against the ceiling's 19. **And the mouse was never
the problem**: `rx 00A9` is 169 bytes against the boot burst's 69, so **100
bytes arrived while the reporter moved it**. It streams perfectly.

**The last four bytes are the whole answer.** Newest first they read
`00 00 3F 43`, so in arrival order: `43 3F 00 00`.

| byte | | |
|---|---|---|
| `43` | `0100 0011` | bit 6 **set** — a packet header. Buttons up, Y high 00, X high 11 |
| `3F` | `0011 1111` | bit 6 clear — X low = 63. With the header, **dx = -1** |
| `00` | | bit 6 clear — Y low = 0, **dy = 0**. A complete, perfect Microsoft packet |
| `00` | | bit 6 clear — **A FOURTH BYTE.** Wheel delta 0, middle button up |

It is an **IntelliMouse-compatible wheel mouse**: four bytes to a packet, the
fourth with bit 6 clear like the two before it. `mou_byte` returned to phase 0
at the third byte, so the fourth fell through `.chk2` to *"a byte with bit 6
clear arriving between packets is a thing the protocol cannot produce"* and
**zeroed `[mou_run]`. Every packet.** The run could never exceed 1, `[mou_need]`
was `MOU_LOCKN` = 8 on this two-port machine, and **the contest was unwinnable
by construction** — 100 bytes of flawless mouse data discarded as fast as it
arrived. `run 0` is in *both* photographs and neither time did it mean
"nothing arrived".

**And it is exactly why the hot-plug workaround works.** With the port already
settled `mou_claim` returns at its first compare, the run is never read again,
and the fourth byte costs nothing. Nothing about the wheel mouse changes when
you swap it in — what changes is whether the run still matters.

**SPEC.md 9.5.4 is the fix**: a fourth phase, so the byte is recognised by
position and consumed without breaking the run. Verified by injecting the
reporter's own four bytes into `mou_byte` **in the guest** — five packets take
`[mou_run]` to 5 where the kernel before it capped at 1; a plain three-byte
mouse is unchanged at 5; and a genuine stray byte after the wheel byte still
zeroes the run, so the rule it relaxes survives.

**The near miss worth recording**: raising `MOU_IDSTRICT` was considered and
declined in 9.4.1.1 on the grounds that the reporter needed nothing from it.
That reasoning was wrong — it assumed the run could accumulate — but the
*decision* was right for a reason it did not know: dropping `[mou_need]` to 1
makes one packet enough, so it would have **masked this defect rather than
fixed it**, and left every one-port machine with a wheel mouse still broken.

## 45. A live DOS hibernate restore freezes for ever on an 8088 (FIXED — `kd_stageseg` could never answer B000: SPEC.md §96.49.2)

**Reported off the fork owner's own 86Box `pc5150` profile** — an IBM PC 5150,
4.77 MHz 8088, 256KB + a 384KB SixPakPlus, **Hercules**, serial mouse, an
ST-225 on a real ST11M. Boot clean, mount the hard disk, run Prince of Persia
off a 720KB floppy under §96's whole-machine arm, quit with Ctrl-Q. The
machine stops on

```
os8088: putting the session back...
```

and never moves again. It is not frozen in the ordinary sense on the way in —
the reporter could type in the DOS window throughout the session — and normal
hibernation, on the same machine, resumed fine.

**THE FIX IS ONE LINE MOVED AND IT COSTS NOTHING.** `kd_stageseg` reads the
BDA's video mode into AL and then loaded `AX` with 0xB800 *before* testing it,
so `cmp al, 7` compared the constant's own low byte, was never equal, and the
`mov ax, 0xB000` under it was unreachable code. Every mono machine staged
§87.5's resume stub into B800 — which on a Hercules primary is not decoded at
all — and then far-jumped into it. SPEC.md §96.49.2 is the entry.

### 45.1 The photograph was the diagnosis, and the blank rows were the finding

One screenshot came back: the message on row 2 of an otherwise **clean**
screen. That is the whole answer and it took a while to read. `kd_resume`
`rep movsb`'s ~440 bytes of stub to **offset 0** of the staging segment
immediately after printing that line, so rows 0 and 1 of the visible page
*must* be garbled if the copy landed where the machine can see it. They were
empty. The copy went somewhere that is not the screen.

The message itself renders because `kd_puts` is the ROM's teletype and the ROM
resolves the segment from the same BDA byte — correctly. So the two readers of
one byte disagreed, and only one of them was wrong.

### 45.2 Three things were suspected and none of them was it

The reporter's own framing was *"no idea if it was this change, or the size
changes, or a merge or something older"*, and a two-point bisect settled it
before any of it was read: **build 576** (both of §9.4.1.1/§9.5.4's mouse
fixes, no size pass) and **build 574** (neither, no size pass) **both froze**.
Prince of Persia is not in it either — `tests/kdreturn.py` reproduces the
freeze with `DOSHELLO.COM`, a 40-line `.COM` that prints and exits.

What made it look new is that it is not: the reporter had been testing the
286 and 386 profiles for a cycle, and **both are colour machines**, where
B800 is the right answer by accident.

### 45.3 It was a hole in the MACHINE LIST, not in the rows

A hibernation needs a fixed disk (`hb_pick` is the predicate on both sides)
and the adapter picks the staging segment — so the two have to be on **one**
machine before that line runs at all. Every MartyPC profile in this tree with
an `[machine.hdc]` was a CGA or a VGA. `kdreturn`, `kdreturnf`, `hibernate`
and `mouresume` all drive this exact path, all four were green, and all four
were staging at B800 where B800 is right.

`os8088_5150_herc_hdd_gla` (and its IBM twin `os8088_5150_herc_hdd`) is that
hole closed. It went red on its first run, with the reporter's screen.

### 45.4 `DOSRMARK=1` is what should have been reachable for

The resume is the one path here that nothing can watch — no kernel, no task,
no debugger hook — so every stage of it looks the same from outside and the
investigation was arithmetic against a still photograph. SPEC.md §96.49.3 is
the knob that ends that: an info line with every number the far jump depends
on, `stg` first, and then one character per stage of the stub onto row 7 of
whatever page it is standing in. On this defect the first field of the first
line is the answer.

### 45.5 The other half of the hole, measured rather than argued

The ORDINARY resume had never run on a mono machine either, for the same
reason: `tests/hibernate.py` was `os8088_xt_hdd`. That route is the one that
*cannot* have this defect — `hbm_stageseg` compares a byte in memory
(`[vid_kind]`) where `kd_stageseg` held its answer in AL — but that is a claim
about the source, and the point of the machine list is that claims about the
source were what everyone had. It is green: **29 checks on
`os8088_5150_herc_hdd_gla`, the same 29 its CGA twin passes**, and `hibernatem`
keeps it that way.

## 46. Microsoft Works: `Too many files open`, with one file open (FIXED — `CON` was resolved through the directory: SPEC.md §96.11.7)

**The first program anyone ran on this box from outside the project**, and the
first report from upstream. An IBM PC 5150 on the fork owner's `pc5150`
profile — 4.77 MHz 8088, Hercules, Sound Blaster — with Microsoft Works 1.00
(`WORKS.EXE`, 313,702 bytes) on a 360KB floppy in B:. Works **launches and
draws its splash screen**, then puts up

```
                          FILE ERROR
                         B:\WORKS.INI
                     Too many files open.
                            <  OK  >
```

The handle table had **one slot of eight in use** at that moment — `WORKS.EXE`
itself — and **the box never returns error 4 at all** on that path: `.fmany`
is reachable only from `dos_fh_new` refusing a full table. So the message was
about a condition nothing had reported, and `DOS_NFH` was never the question.

**Only a reference trace could have found it** (docs/DOS-DEBUGGING.md's whole
premise: our side looked right, and it was right). Traced under this box and
under a real IBM DOS 3.30 on the same disk, one call site diverges:

| | | |
|---|---|---|
| `os8088 43` | `AH=3D AL=02 → 0002 CF` | `@+0BD1:082A` |
| `dos 41` | `AH=3D AL=02 → 0007 ok` | the same site |
| `dos 42` | `AH=3D AL=02 → 0008 ok` | …and again |
| `dos 43` | `AH=3D AL=02 → 0009 ok` | …and again |
| `dos 44` | `AH=3D AL=02 → 0004 CF` | **DOS itself answers 4** |
| `dos 45` | `AH=3E close BX=0007` | and Works hands them all back |

The name is **`CON`**. Works opens the console over and over at one site until
DOS refuses, to count how many handles it has left, then closes them. Under
DOS it counts three. Under us the first open answered *file not found* —
because the box resolves every name through the directory and `CON` is not a
file — so it counted **zero**, and every open it wanted after that it refused
by itself. The error text is Works being right about what we told it.

**THE FIX IS THAT A DEVICE IS NOT A FILE NAME** (SPEC.md §96.11.7). `AH=3Dh`
tests `CON`/`NUL`/`PRN`/`AUX` before the directory and hands out a real slot
marked `FHF_DEV`; reads answer end of file, `CON` writes take the teletype,
and the other three accept their bytes and write none. **A real slot is the
requirement and not a detail**: answering with one of the five standard
handles would give the counting loop the same number for ever and it would
never end.

The same trace named two more, both refusals DOS does not make: **`AH=44h
AL=08h`** — is this drive removable — which is the *first* differing answer
of the run (§96.22.2), and **`AH=0Dh`**, disk reset (§96.11.8).

**What it cost was a kilobyte of the DOS core**, on the owner's call —
*"raise the image by a kb for now, and we will optimize afterwards. Working
at all is most important."* `CORE_MAX` had 30 bytes of slack and the device
path is 192; `KD_IMG_KB` goes with it, so it is also a kilobyte off the DOS
program on the shut-down arm. Task #27 is where both come back.
`tests/dostrap/condev.asm` and the `dosdev` row are the gate, and they assert
the property the loop rests on — **two opens, two different handles** — and
not merely that `CON` opens.

## 47. Microsoft Works: `Cannot write file`, on a floppy with 42 free clusters (FIXED — two defects, and the second one shipped the day before: SPEC.md §96.11.6.3, §96.11.10)

The same 5150, the same Works 1.00, one error behind the last. With §96.11.7's
`CON` fix in, Works reaches its New dialog and its word processor. Type
something, `Alt`, `File`, `Save As`, take the default name, and:

```
                          FILE ERROR
                          B:\WORD1.WPS
                      Cannot write file.
                            <  OK  >
```

Choosing another drive fails the same way. The disk has **42 free clusters**.

**IT IS TWO DEFECTS AND ONE MASKS THE OTHER.** Works's Save As is one shape,
and it is the shape of every format whose header depends on its body:

```
3C02 create WORD1.WPS      -> handle 6
4202 seek END              -> 0
4200 seek to 0180          -> 0180      the file is still EMPTY
40   write 011D bytes      -> ax=0005 CF=1     <-- the error the user saw
4200 seek to 0             -> 0
40   write 0180 bytes      -> 0180              the header it left room for
3E   close                 -> 0
```

**One**: `.fwrite`'s append-only guard was `jne .fhacc` twice, which is not an
ordering test at all — it refused a write *past* the end in exactly the same
breath as one *behind* it, and those are opposite cases. Behind is §96.11.2's
real refusal; past is a **gap**, which `dos_fh_wiloop`'s `.ihole` already lays.
Three answers now, on an unsigned 32-bit compare.

**Two**: with that fixed, the save *succeeded* — `write 011D -> 011D`,
`write 0180 -> 0180`, `close -> 0`, no dialog — and `WORD1.WPS` came off the
floppy **669 bytes with the right body and a header of 384 zeroes**. Works was
told it wrote 384 bytes and nothing was written. That is §96.11.10: `FHF_DEV`
had been given bit 5, which `FHF_WROTE` already owned, so the **first `AH=40h`
on any handle** made that handle read as a character device for ever after —
every later read answering end of file, every later write accepted and
discarded with its full count reported. It went in with entry 46's `CON` work
the day before, seven `equ` lines from the value it collided with, with four
unrelated `DOS_DEV_*` codes sitting in the gap.

**Three things are worth more than the fixes.**

`tests/unit/t_bits.py` (fast tier) now derives every flag family from the
**code that uses it** — a `test`/`or`/`and`/`xor` against a memory field
enrols its constant in that field — so nothing enumerates the 43 families in
this tree and a flag added tomorrow is covered tomorrow. Grouping by name
*prefix* was tried first and reports **81 false positives**.

`os88dosdbg trace --flush-disk` writes B: back before the machine closes. The
instance runs on a private clone and nothing persists it, so a successful save
and a silent no-op look identical on the host — which cost a wrong conclusion
about a fix that worked.

And `tests/dostrap/wrgap.asm`'s step **C2** is what placed the second defect:
it reads the header back on the same handle *before* the close. The source had
been read three times by then and said the write could not be lost.

## 48. Microsoft Works has a mouse and it does nothing (FIXED — it installs an EVENT HANDLER and never polls: SPEC.md §96.10.4)

Reported with entry 47 and in the same sentence — *"works is supposed to have
a mouse, and there is none"*. Functions 3, 5, 6 and `0Bh` were all exact
throughout, which is why reading the code found nothing: the box's `INT 33h`
answers the position and the buttons correctly and always did.

**The `DOSTRACE` histogram (§96.10.3) answered it in one line.** The ring
cannot carry `INT 33h` — every host-side decoder in `tools/os88dosdbg.py`
reads an entry as an `INT 21h` call — so a program that never calls the mouse
and one whose calls are answered wrongly both come back as a trace with no
mouse in it. Thirty-two saturating bytes, one per function, and Works reads:

```
INT 33h: 00h reset/installed? x1, 08h set y range x1,
         0Ah set text cursor x1, 0Ch SET EVENT HANDLER x1
```

Four calls, then nothing. **It never polls function 3.** A box that answers
`0Ch` with `not supported` and makes no callbacks has told a program a mouse
exists and then never mentions it again, which a program cannot tell from no
mouse at all.

§96.10.4 is what it takes to make that real on a machine whose kernel owns
both mouse ISRs: chain IRQ0, because every other moment the box gets control
is the program calling *us* and a program with a handler installed has stopped
calling. 18.2 Hz against a serial mouse's ~40, 398 bytes, and `dosmouevt`
reads `events 000F move 000D press 0001 release 0001`.

**The debugging cost four A/B builds and none of them was the bug.**
`os88mouserel.Rel` paces by FRAMES by default and `m.advance(frames=)` leaves
the emulator **paused**; a test that moves the mouse and does not resume stops
the guest, freezes the BIOS tick at `0040:006C`, and reads exactly like
*moving the mouse hangs the machine*. The callback was removed, then the host
read, then the IRQ0 hook — each one **keeping** the symptom, which is what
should have named the cause several builds sooner. The row uses `pace="wall"`
and its docstring carries the corpse.

## 49. Dual-screen Herc/CGA: leaving the DOS box's full screen turns the CGA GREEN and flickering (FIXED — the kernel was reading the ROM's ONE mode shadow for a machine with two cards: SPEC.md §39.18.1.1)

Reported off 86Box, an `ibmxt` with a CGA and a Hercules Plus in it and the
**Hercules made primary** in the Control Panel, the dock on bottom/auto and
only `SOUND.DRV` mounted: *"opening dos.o88, pressing alt-enter to go
fullscreen, then exiting fullscreen, with the dos window on the herc primary
display caused the second cga display to turn green and flicker and have
corrupted gfx."*

**Reproduced first try on `os8088_5150_both_gla_mono`**, which is that machine
— a Hercules primary with a CGA beside it — and the mechanism is one byte.
`vid_unblank_kind`'s CGA arm sourced 3D8h from **40:65h, the BIOS's shadow of
the CRT mode register**, and a BIOS keeps exactly one of those: it describes
whichever card the ROM last set a mode on. `fsx_mode(FSXM_TEXT80)` on a
Hercules display is `int 10h AX=0007h`, so the byte the CGA was handed on the
way out was **mode 7's `0x29`** — 80x25 text, video on, **blink on** — written
to a card whose 6845 still carried mode 6's timings.

The three symptoms are three bits of that one byte. The desktop ground is
§39.4's 50% dither, so decoded as character cells every other attribute byte
is `0xAA`: background green, foreground light green, blinking. **66.0% of the
card measured RGB (0,170,0)** and `video(card=1)` read `Mode3TextCo80` where it
had read `Mode6HiResGraphics`. Nothing was wrong with the pixels the kernel
wrote — §53.6's `wm_paint_all` repaints both displays correctly, and after the
fix all 128,000 of them are identical across the round trip.

**Three things it is NOT**, each of which was on the table before the trace:
not the dock (the report mentions it and `fsx_run` drops an open one anyway),
not `SOUND.DRV`, and **not Alt+Enter or the DOS box** — any BIOS mode set on
any other card of a two-card machine does it, and the blank direction had the
same defect with §64.3's idle blanker as its trigger. So the fix is at the
source of the byte: `[vid_cgamode]`, banked by `vid_setmode` right after the
CGA's own `int 10h AX=0006h`, which is the one instant 40:65h is known to
describe that card. It came out **12 bytes on kern_big and 5 on kern_small**,
because reading a kernel byte needs no `ES`.

`tests/dispfsxcga.py` is the gate and it was **verified to fail** against the
kernel before the fix — leg 3 reads `Mode3TextCo80`, leg 4 counts 115,010 of
128,000 pixels changed — while its leg 2 asserts that the ROM really does move
40:65h, so the row cannot pass vacuously on a BIOS that does not.

## 50. ...and the REVERSE: the DOS box full screen on the CGA corrupts the HERCULES (FIXED — `vid_text` never got §39.19.4's `vid_cga_equip`: SPEC.md §39.19.4.1)

Entry 49's mirror, same machine, reported the next morning: *"Move the dos
window over to cga (it won't fully fit because of wm_snap). Go fullscreen.
Return. The herc screen is corrupted, the CGA screen is fine."* The photograph
is the desktop sheared — the menu bar squeezed along the top, the dither
repeating — which is a framebuffer scanned on the wrong timings and not
anything drawn wrongly.

**It is the EQUIPMENT FLAG, where 49 was the mode shadow.** §39.19.4 already
knows that the PC/XT ROM's mode set is equipment-driven: with `40:10` bits 5:4
saying `11b` it forces mode 7 and the 3B4h CRTC *whatever mode was asked for*.
`vid_setmode` was fixed by moving `vid_cga_equip` above its `VID_CGA` test.
**`vid_text` has the identical arm and never got the call** — and an fsx
bracket is what reaches it, because `fsx_mode` on the second display sets
`[vid_kind] = VID_CGA` while the desktop's primary, and so `40:10`, is still
the Hercules. So `int 10h AX=0003h` retimed the **Hercules** for 80x25 MDA
text over its own graphics framebuffer, and the CGA was never touched.

Nothing puts it back, and every step on the way out is individually correct:
`fsx_restore`'s `vid_setmode` runs before `vid_fsx_leave`, so it sets the
*bracket's* display's mode; `vid_fsx_leave` republishes geometry and sets no
mode by design (§39.18.1); and `vid_unblank_kind` writes 3B8h = 0x0A, which
puts the graphics bit back over a 6845 still timed for text.

**THE BYTE EVERYONE WOULD HAVE WATCHED CANNOT SEE THIS.** IBM's mode-register
table gives **mode 3 and mode 7 the same value, `0x29`**, so 40:65h reads
identically whether the ROM honoured the request or forced it — and entry 49's
gate watches exactly that byte. The mono card's own RASTER is the
discriminator: 912 wide with its graphics timings, **882** once the ROM has
retimed it. Measured on `os8088_5150_both_gla_mono`, which is this machine.

**Three things were broken and the report names one.** The Hercules was 134,951
of 252,000 pixels wrong afterwards (1,322 now, and those are the clock, the
pointer and the straddling window's own console). **The FULL SCREEN was also
broken, on the monitor the app was on** — the CGA kept its 640x200 bitmap while
§96.33's teletype wrote character cells into `B8000`, so it showed 20,320
coloured pixels where an 80x25 text screen belongs; it is black-and-white text
now. And `40:10` was left claiming a colour primary for the rest of the
session, which is the flag §39.20's Restart reads.

**The flag must NOT be restored by `vid_text`**: the DOS box writes through the
ROM's own teletype while it is full screen and the ROM picks the card off that
same flag, so putting it back early would send the program's output to the
monitor it is not on. `vid_fsx_leave` is where it belongs — `vid_disp_init`'s
extend arm already writes that exact `vid_kind` / `vid_apply` / `vid_equip`
sequence.

**A THIRD SITE had the same missing line**: `fsx_setbios`, the `int 10h AH=00h`
behind `fsx_mode`'s plain-BIOS rows, so TANK's `FSXM_CGA320` (§85.3) and Mode
X carry this defect on the same machine. It is fixed by inspection against the
mechanism measured twice here — the ROM forces before it looks at which mode
was asked for, which is why §39.19.4 caught mode 12h and this caught mode 3 —
and what would exercise it is TANK launched from a Disk window already on the
second display.

**3 bytes each, 9 on kern_big and 6 on kern_small**, no rung crossed.
`tests/dispfsxherc.py` is the gate and goes red on four of its five legs
without the fix.

**And it cost two INSTRUMENT findings, both now in docs/MARTYPC-DEBUG.md.**
MartyPC's `fbuf` on a SECONDARY card in a graphics mode is not faithful — the
CGA's memory here is a perfect 50% dither, 8,000 bytes of `0xAA` and 8,000 of
`0x55`, and the rendered frame has black bands and a solid blue block in it —
so entry 49's gate reads the framebuffer BYTES and this one reads the
rasterisation, because this defect never touches a byte and that one leaves
every byte perfect. And a `settle` after a bracket returns **mid-repaint**:
`[fsx_cur]` is cleared before `wm_paint_all` runs and the cards are lit after
it, so the first capture differed from the next by ~4,500 pixels on an idle
box. Both rows converge now instead of trusting one settle.

## 51. CLEAR SKIES: San Francisco will not fly — the Fly button does nothing at all (FIXED — the LAST stream in the file can never fill a cluster-rounded read: SPEC.md §88.10.5.4.1)

*"I'm unable to fly in san fran - clicking the fly button does nothing (no
error, but also, no flying)."* Reported off a 286 with a VGA, and **the machine
is incidental**: San Francisco is the last world in the package file, and that
is the whole of it. Reproduced on the first attempt and on the first shot,
under MartyPC — nine locations poked one at a time, eight fly, SFO reports
`cs_wldnow = FF` with nothing loaded at all.

**A CAPACITY IS WHAT YOU ASK FOR AND A STREAM IS WHAT YOU NEED.** `cs_wldget`
asks `OSAPI_FILE_READ_AT` for the head slack plus the stream **rounded up to
whole clusters**, because §20.14.3 wants a cluster multiple for the capacity as
well as the offset — and then it checked the *delivered* count against that
same rounded number. Every stream but the last has more file behind it, so the
read fills the capacity and the check passes by accident. The last one ends at
EOF and never can.

Measured on the shipped package, 45,255 bytes, the ninth stream at sector 86
and 1,223 bytes long — `44,032 + 1,223` is **exactly** the file's length:

| stream | needs | capacity asked | file has after the base | |
|---|---:|---:|---:|---|
| `csw7` Rio | 1,247 | 1,536 | 2,759 | ok |
| **`csw8` San Francisco** | **1,223** | **1,536** | **1,223** | **refused** |

At a 1,024-byte cluster the capacity is 2,048 and the shortfall is larger, so
**every geometry this ships on fails identically** and no other location does.
`cs_wldpick` returns `CF=1`, `[cs_wldnow]` stays `0FFh`, and `cs_cmd_fly`'s
`jc .out` makes the button a no-op — which is §88.10.5.4's symptom exactly,
reached through the *other* check in the same routine. Both are one mistake in
one shape: **a size handed to a kernel call that verifies it, taken from the
room rather than from the thing.**

**`apps/os88partsbody.inc` already had it right.** `op_load`'s chunk loop
carries `[op_want]` — *"how much of what MUST arrive just did"* — and refuses
on that, so the shared parts reader was never wrong and this is what the
package's own hand-rolled copy of that read lost. Six bytes of the package
image, nothing resident, and `build/skies.o88` is the same 45,255 bytes.

**WHY IT SHIPPED IS THE PART WORTH KEEPING: no row ever flew it.**
`skieswater` visits LBG, LCY and JFK, `skiesgeom` both Paris runways, and every
other skies row takes the default location — so of nine places the suite flew
five, and the one it never picked is the one that was broken.
`tests/skiesworlds.py` flies **all nine** now, one independent full load each
(`[cs_wldnow]` forced to `0FFh` first, so nothing passes on its predecessor's
world), and asserts the world that ARRIVED rather than that the screen changed
— because a silent load failure takes no mode, so there are no pixels to ask
about. Verified to fail on SFO alone against the package before the fix.

## 52. Microsoft Works: `Directory not found` when you pick another drive in Save As (FIXED — `AH=43h` was a FILE lookup, and a root parses to no file name at all: SPEC.md §96.12.4)

Reported the same day as 47 and 48 and, like them, described from the glass:
*"switching to another drive (Directory not Found)"*, then, asked how:
*"I tabbed over to the directory browser and tried to select A: or D:. It
correctly lists the drives we gave it, but trying to switch to one is what
gives the error."*

**The drive switch works and always did.** The trace shows `AH=0Eh` select
A:, `AH=19h` answering `AL=00`, and `AH=0Eh` back to B: — all of it before
anything fails. The refusal is the call *after* them: `AH=43h AL=00h`, which
Works uses to ask *"is this directory there?"* before it writes, answering
`CF=1 AX=0002`.

`.att_get` resolved every name through `dos_fh_stat` — the **file** lookup
`AH=3Dh` opens through (§96.11) — so a name that is a folder found nothing.
Not just the drive's root: **every directory on every disk read as missing**,
which nothing had noticed because nothing else in the tree had asked.

**The root is the sharper half and is why the name recorder had to be
widened.** `A:\` parses to a drive and *no 8.3 name at all*, so the lookup
was for the empty string — and the twelve-slot recorder §96.11.7 had left in
place was full of `CON` long before the interesting name arrived (a program
opens `CON` eight times). At 48 slots the name came back **empty**, which is
the whole diagnosis in one field.

`dos_att_isdir` is `dos_cd_go`'s own `.named` scan with the walk taken out,
and the root needs no scan at all — an empty name *is* the directory we
stand in.

**IBM DOS 3.30 is the specification and `tests/dostrap/attrdir.asm` runs
under both machines unchanged** (docs/DOS-DEBUGGING.md): it answers `\` and
`A:\` with `CF=0 CX=0074`, a subdirectory `0010`, a file `0020`, and only a
missing name `CF=1 AX=0002`. We answer `0010` for the two roots deliberately
— `0074` is bits DOS never set, a root having no directory entry to read them
from, and what every caller tests is `CF` and bit 4. **The probe prints
`ATTRDIR PASS` on both machines**, which is what says the assertion is not
one only this box could satisfy.

**The gate's own first version was the near-miss worth keeping.** `ask`
pushed `AX` and `CX` and then did `or bl, bl` between the `int 21h` and the
`jc` — so the judgement read *its own* flag, not DOS's. It printed `FAIL`
beside five correct answers, and the same defect would later have printed
`PASS` beside five wrong ones. The carry is banked into a byte by a `mov`
now, `mov` being the one instruction there that writes no flags.

## 53. Microsoft Works has a mouse, it works, and there is nothing to see (FIXED — in DOS the DRIVER draws the pointer: SPEC.md §96.10.5)

The third of the Works reports and the only one where **the reporter brought
the diagnosis**: *"Apparently we are expected to draw the cursor — including
in text mode, which we never had to do before in our os — unless the program
tells us somehow that it is taking over drawing the cursor itself."* That is
exactly right, and it is the one part of `INT 33h` this box had answered with
a shrug: `01h` and `02h` were both `.none`, on the reasoning that the kernel
owns the pointer.

**The reasoning is right in the windowed host and wrong under `kern_dos`**,
where the program owns every pixel and the kernel is not running at all.
There is no compositor and no arrow the machine keeps: `01h` means *put a
cursor on the screen and keep it under the mouse*, and if the driver does not,
nothing does.

It is also the report that came with its own **correction**, and the
correction is the more useful half: *"I went fully into a document and it DOES
work — still no visible cursor of course cause we don't draw one, but if I
push it up to the top and click I can open menus with it."* §96.10.4's event
handler was working the whole time; what was missing was only the drawing.

**The reference is what specified it.** Works under IBM DOS 3.30 with CTMOUSE
loaded (docs/DOS-DEBUGGING.md), read off §96.10.3's histogram:

```
00 0A 0C 08 0A 0A 01 03 02 01 03 02 01 03 02 ...   (x25)
0Ah x3: kind=0000, first 77FF/7700, last 80FF/F000
```

Three things fall out and each decided something. `kind=0` is the **software**
cursor, so the whole drawing rule is `(cell AND screen_mask) XOR cursor_mask`
and there is no shape to draw. The masks are asked for **three times with two
different values**, so they are *state* — a hard-coded `77FF`/`7700` would
draw the wrong cursor for most of a session. And `01 03 02` is show / ask /
hide: **Works takes the cursor off before it draws its own screen**, which is
what a well-behaved DOS application does and is why §96.10.5.3's guard is a
guard rather than the main mechanism.

**`DHK_TXT` is how "`kern_dos` only" is spelled.** The core is assembled once
and joined to either host, so it cannot be an `%ifdef`: `kdentry.inc` fills
the hook and `dos_hk_bind` does not, and in the window the cell stays the zero
a `.bss` arrives as. `tests/kdmcur.py` asserts **both** arms, because a row
that only ran arm 3 would pass just as happily with a box that scribbled on
the desktop.

Measured: `doscore.bin` **15,475 → 15,759** (+284, 113 bytes of `CORE_MAX`
left), `kerndos.bin` +52, `DOS.O88` +254 packed. Nothing resident on a machine
that is not running a DOS program.

**The debugging cost one wasted arm and it was the harness's own.** The first
gate ran only `ui.path("B:/MCURSOR.COM")` — which is the *windowed* box — and
reported the cursor absent, correctly and uselessly. Reaching arm 3 is
`tests/kdmouse.py`'s sequence: open `DOS.O88` itself, pick the Memory arm,
then name the program. And the probe's own labels had no trailing space, so
`C remasked0741` split as one token and the harness read the label as the
cell — a parse that says *the cursor was drawn* about a machine where it was
not.

## 54. Microsoft Works has a mouse, the buttons work, and the cursor is invisible (FIXED — we answered `08h` with a zero and Works reads that as `no mouse`: SPEC.md §96.10.6)

The fourth Works report and the one that took three attempts, so the failures
are worth as much as the fix.

*"Mouse: Still no cursor. I can still open the menus with it, its just still
invisible."* — and then, asked which arm: *"I tested BOTH under kern dos, and
inside the OS. No cursor in either."*

**It is one instruction of Works's, and it is not a drawing bug at all.**
Disassembled out of `WORKS.EXE` (the mouse module is at file offset
`0x4BE90`, found by `mov ah,35h / mov al,33h` at `0x4C00C`):

```
0004C00C  mov ah,0x35 ; mov al,0x33 ; int 0x21   ; get the INT 33h vector
0004C012  mov ax,es ; or ax,bx ; jz 0xc051       ; 0000:0000 -> no mouse
0004C018  xor ax,ax ; int 0x33                   ; fn 00h reset
0004C01C  or ax,ax  ; jz 0xc051                  ; AX=0 -> no mouse
          ... fn 0Ah 77FF/7700, fn 0Ch handler 0E7:0CEA mask 1F ...
0004C04C  mov ax,0x8 ; int 0x33                  ; fn 08h SET Y RANGE
0004C051  mov [0x98ca],al                        ; *** mouse-present flag ***
```

**Works stores `AL` after function `08h`, which documents no return value.**
A real driver never writes `AX` there, so CTMOUSE comes back with `AX = 8`.
Our dispatcher had no `08h` arm, so the call fell through to an exit that did
`xor ax, ax` under a comment reading *"INT 33h's not supported"* — and `INT
33h` has no such convention. We handed Works a zero at the end of an init
sequence every call of which we had answered correctly.

**It presents as HALF a working mouse**, which is why two rounds of looking at
the drawing code found nothing: the handler installed at `0Ch` is still hooked
and still called, so the buttons work and the menus open, while the program
never asks for a cursor (`01h`) and never polls the position (`03h`).

**Three method failures, and each cost a round.**

1. **The gate was written to the mechanism and not to the application.**
   `MCURSOR.COM` calls `01h` and then holds still — the two things Works does
   not do — so it proved the drawing and could not see the ABI. A probe the
   author writes to exercise their own feature agrees with it by
   construction.
2. **A negative control can encode the wrong premise as a requirement.**
   §96.10.5.1 first refused `DHK_TXT` to the windowed host, and
   `tests/kdmcur.py`'s window arm *asserted* that nothing is drawn there. It
   passed for the same wrong reason the code was wrong. That refusal is
   corrected in the same commit: `dos_fsx_main` puts every DOS program inside
   an `FSXM_TEXT80` bracket, so the window has a text screen for the whole of
   a program's life — and the BDA would have been the wrong source for it,
   `OSAPI_FSX_CAPS` answering the display's own kind off the primary
   (§53.7.1).
3. **`os88dosdbg diff` could not align these two runs at all**, and its
   premise is why: Works executes from dynamically-placed overlays, so
   `CS − PSP` is not stable between machines the way docs/DOS-DEBUGGING.md
   assumes. The IPs matched exactly (`0398`, `0404`, `063C`, `0610`) while the
   segment bases differed (`636F` against `8D7E`). Aligning on `(function,
   IP)` alone found the divergence in one pass.

**And two red herrings, both plausible and both wrong**, recorded because the
next reader will find them too. The reference makes a `SET vector 0Ch` that we
do not — `IRQ4`, the serial mouse's line — but its `CS` is *below* the
program's PSP, so it is CTMOUSE re-hooking its own IRQ inside the `00h` reset,
not a decision of Works's. And the two sides disagree about how many file
handles a program may have (ours grants five more before error 4, DOS two),
which is real and is not this.

The vector's ADDRESS was the other suspect and is also not it: ours is at
575.0 KB where a TSR sits at 52.5 KB, below the program — a genuine
difference, and Works never looks at it beyond the `or ax,bx` test for
`0000:0000`.

## 55. Microsoft Works: the cursor is there, but not over the startup dialog, and not after a redraw (NOT OURS — CuteMouse answers identically: SPEC.md §96.10.5.4)

The fifth Works report and the one with no fix in it, which is the finding.

*"I have a cursor in works! Same issue ctmouse has — which means its probably
a works itself issue — the cursor does not invert or display at all over open
'dialogs' like the one that opens when you first open the program. Also, I
cannot replicate the stationary cursor issue, so likely works does not redraw
in this condition? You might have to make a custom program to test that."*

Two observations, and **the reporter had already done the hard half of both**
by running CTMOUSE beside our box on the same machine. That is the control
this whole section of the tree is built on (docs/DOS-DEBUGGING.md), and it is
worth saying plainly: *two independent drivers behaving identically is an
observation about the application, not about either driver.*

### The dialog

Works sets 77FF/7700 and then 80FF/F000 twice more (§96.10.5, measured off
`WORKS.EXE`). The second pair is `(cell AND 80FF) XOR F000`: it keeps the
character and the blink bit, clears every colour bit and forces background
`F`. Over ordinary grey-on-black text that is a bright block and obvious; over
a dialog already drawn black-on-white (`70`) it produces `F0` — the same
black on white, one intensity bit brighter. On a CGA that is close to
invisible, and it is Works's own choice of mask, applied faithfully. CTMOUSE
draws the same nothing for the same reason.

### The redraw, which was investigated as OURS and is not

The second half looked like a real defect of ours and was written up as one: a
software text cursor is an attribute flipped into a cell the driver does not
own, and it gets **no notification** when the application stores over that
cell. `dos_m33_paint` returns early whenever the pointer has not changed
cell, so a program that redraws under a hand holding still takes the cursor
with it and does not get it back until the pointer moves.

`tests/dostrap/mredraw.asm` was written to prove exactly that, and it did:

```
A drawn     7041 want 7041 ok
B redrawn   1E2A want 612A BAD          <- the predicted defect
C restored  1E2A want 1E2A ok
D unmoved   0000 want 0000 ok
```

**Then the same `.COM` was run under IBM DOS 3.30 with CuteMouse 1.9.1 on the
same machine, and it answered all four identically.** A serial mouse that is
not moving raises no interrupt, so a driver whose repaint hangs off its own
IRQ has nothing to repaint from, and neither driver hooks the tick for it.

So there is no fix and the early return stays. `tests/kdmredraw.py` asserts
the **parity** instead — a compatibility ratchet, red if this box ever starts
repainting where CuteMouse does not. It was verified to fail by building the
seventeen-byte guarded re-save that would have been the fix, which takes B to
`612A` and leaves C green: a correct cursor, and a worse DOS.

**The lesson is the order of the two runs.** Our own answer was wrong-looking,
reproducible and fully explained by our own code, and every one of those is
true of the reference too. Four earlier Works defects were found by putting
the same program in front of a real DOS and diffing; this is the first time
that method has said *stop, there is nothing here* — which is worth as much,
and cost about fifteen minutes against the day a "fix" would have taken.
