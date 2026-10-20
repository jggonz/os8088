# APPLE2 port plan - an Apple II Plus emulator as an os8088 C package

The design record for **`docs/APPLE2-SPEC.md`** - the contract, which by the
user's instruction lives in its own file and not in SPEC.md - produced by
`.claude/skills/port-to-os8088`'s scouting workflow off two scout reports and
reconciled against an adversarial fit review.
`workflows/implement.js` reads this file one wave at a time.

**The SPEC is the authority.** This file says *why* it reads that way: the
intake, the decisions with their reasoning, the waves with their `done_when`,
the verification list and the risks. Where the two disagree, the SPEC wins and
this file is wrong.

---

## The intake, and the reference set

Three reference trees, none of them vendored (`CONTRIBUTING.md` §6), and **one
authority rule**:

| tree | licence | authority on |
|---|---|---|
| **AppleWin** | GPL-2-or-later | **everything II+-specific** - because MII has no II+ mode at all and apple2emu gates the //e switches unconditionally |
| **MII** (`mii_emu`) | MIT | the Macintosh-shaped menu bar, the About panel, the soft-switch enumeration, the per-write dirty-line map, the flash phase, the hi-res artifact-colour index |
| **apple2emu** | MIT | the 24-entry row-base tables, the Edit menu, the Disk II controller, the paddle one-shot, the paste handshake |

And one **fourth** tree that is not a reference but a **source**:
`apps/c64` in this repo. `a2cpu.inc` is a **derived copy** of
`apps/c64/c64cpu.inc` (measured 93% generic NMOS 6502), which is
GPL-2-or-later by way of VICE 3.10 - so **`apps/apple2` is GPL-2-or-later**
and VICE is named in every derived file header, in `apps/apple2/COPYING` and
in the About panel.

**The two scout reports this plan was built on:** the C64 precedent (what the
shipping port already solves and what an Apple II would have to invent) and
The Wire (what a catalog record costs, byte for byte).

**Four things the C64 tree does NOT have are new work and are sized in
`APPLE2-SPEC` section 15:**

1. **the three composers** - the C64's bitmap wave was never written, so there
   is no bitmap composer to copy, **and** its one text composer is 8-px-cell,
   which the Apple's 280-pixel raster forbids;
2. **the non-linear page-to-scan-line map** the interleaved address arithmetic
   forces;
3. **the four `OSAPI_FSX_*` C thunks**;
4. **the read-only Disk II.**

**THE APPLE TEXT CELL IS SEVEN PIXELS.** 280 / 40 = 7, so all three composers
are shift-accumulator composers of the same class, no cell after the first
lands on a byte boundary, and a decoded glyph is never a pixel byte. That
correction changed wave 1's only deliverable, `a2band.inc`'s budget line and
every per-cell figure in this plan.

---

## Decisions

### The user's, taken at intake

1. **The machine is a 48K Apple II Plus with Applesoft**, not a //e. The whole
   //e machine is `absent` and not greyed: AppleWin's `IS_APPLE2` /
   `IsAppleIIeOrAbove` tests are the checklist of exactly where the fence is,
   and greying a //e card on a II+ hands the reader a //e checklist.
   `APPLE2-SPEC` states the fence once, in section 10.4.
2. **The ROMs are FETCHED at a pin and NEVER committed.** A stricter posture
   than the C64's committed Commodore ROMs, and stricter than AppleWin and MII
   take themselves. `tools/getapple2rom.py` pins AppleWin commit `3e8054b4`
   and checks each of the three files by SHA-256.
   **Consequence, accepted:** a build with no network and no cache cannot
   build this package, exactly as `make zdisk` and `make runcpm` cannot.
3. **The 6502 core is a DERIVED COPY of `apps/c64/c64cpu.inc`**, not a
   `%include` of a GPL file from another package's directory and not a rewrite
   of seven of twelve rows of a working gate. The stated trade is that a bug
   in the shared 93% must be fixed twice; the mitigation is that both cores
   run the same fetched oracles.
4. **The windowed 1bpp path first, then a foreign video mode.** The window is
   monochrome and says so as a fact; colour lives in `FSXM_VGA13` at full
   screen. No kernel ink/paper band slot is added.
5. **The speaker is a toggle-interval estimator**, not PCM. It exists in no
   reference - all three synthesize - so it is this port's own design and is
   labelled as such on the status row and in the SPEC.
6. **The keyboard is AppleWin's II+ byte map**, rejections and folds included,
   with the reset chords on Ctrl+F2 / Ctrl+F3 and full screen on **Alt+Enter**
   - AppleWin's own chord, because the precedent the C64 sets is *keep the
   ORIGINAL's chord* (its Alt+D is VICE's own hotkey), not *keep the other
   port's key*.
7. **Load Program and Save Program read and write a TOKENISED Applesoft
   program** through the documented zero-page pointers. No reference
   implements either half, so `APPLE2-SPEC` section 12 records them as this
   port's own rows with the reason: there is no Disk II in this PR and a
   listing has to get in somehow.
8. **Names:** package `APPLE2`, directory `apps/apple2`, machines
   `vm/386-apple2` / `vm/xt-apple2` / `vm/286-apple2`, images
   `build/apple2*.img`. Two product strings with a stated rule -
   `Apple II Plus Emulator` long, `Apple II+` short - and not three that
   drift.
9. **Its own SPEC file.** `docs/APPLE2-SPEC.md`, on the C64 precedent.
   **SPEC.md gets no new section.**
10. **The Wire gets a listing** in wave 7, prepared on a branch and **not
    committed in the web repo until the OS side is done and the user has said
    go**.
11. **Honest speed posture.** Correct first; the measured percentage of a
    1.02 MHz Apple II is on the status row, and the Wire page says what an XT
    does and does not do. No faster core promised later.
12. **The whole harness kit exists from wave 1**, with every step failing
    rather than passing when its subject is absent.

### Taken by the maintainer on the two questions this plan asked

13. **Disk II is DEFERRED to a follow-up PR.** Waves 1-5 and 7 ship in this
    PR; Machine > Configure Slots... greys with
    `No Disk II in this build. Load Program reads an Applesoft program, and Paste types a listing in.`
    **Nothing is renumbered: wave 6 stays wave 6 and is marked "follow-up
    PR",** and the follow-up's first job is a measured size line.

    *Why.* Three things moved after the draft, which had recommended shipping
    it. **(a) The budget is contested** - this plan reads ~43,500 image and
    the fit review read 60,500-63,500, and the disagreement is not settled
    until wave 2's first measured line; deferring the largest wave is what
    makes that argument survivable rather than a mid-feature ceiling hit,
    which is exactly how CWORD lost time twice. **(b) Wave 6 costs more than
    the draft booked**: ~280KB of heap for two drive images, which the draft
    did not account for at all, plus the P5 ROM's authentic no-disk hang,
    which is a new user-visible surface with its own status-row wording.
    **(c) It is the wave with the least new invention and the most expensive
    failure mode** - DOS 3.3 in a silent wait loop - so it is the cleanest
    thing to lift out whole. Against it: a II+ with no drive is a machine you
    can only type into, and every piece of Apple II software that exists is a
    disk image.

14. **If the end-of-wave-5 size line breaks 54,000 resident, Disk II gives way
    and the foreign video mode ships WHOLE** - **unless** wave 5's own bench
    shows `FSXM_CGA640` and `FSXM_HERC` losing to the windowed path, in which
    case **those two writers are cut on their own merits**, ~1,200 bytes come
    back, and colour survives on VGA.

    *Why.* The four size levers in `APPLE2-SPEC` section 15.4 total ~2,400
    bytes and are all internal - they do not decide between features. This
    does. The foreign video mode is the feature the user named a reason for
    (colour, and XT speed if it measures), and Disk II lifts out cleanly as a
    whole wave with no half-state, whereas cutting the foreign mode leaves the
    port monochrome everywhere with nothing to point at. And wave 5's own
    measurement can make the choice free.

### Falling out of those, and recorded so they are not re-opened

15. **Integer BASIC as a second ROM part: flat no**, with the arithmetic. A
    part costs zero image and zero bss, so the segment cap does not bind - but
    the package **file** would go from ~57,400 to **69,688** against The
    Wire's `WIRE_FILEMAX` of **64,512**, forcing the record to an archive
    shape or to `floppy_only` with both buttons greyed.
16. **Six items the draft greyed are `absent` instead**: the Language Card,
    Integer BASIC, the 80-column card, RAMWorks, Double hi-res and the
    Mockingboard. None is a control in any reference's UI, so greying them is
    inventing surface.
17. **The CHARGEN decode and the 7-bit reverse table are built in `os88_main`,
    RESIDENT** - never in `ovl_a2_init`. Both are on the display path, and a
    disk with no `APPLE2.OVL` must be a program whose menus refuse, not a
    window that draws nothing. This has its own negative-control test row.
18. **The nibbliser is hand-written 8086 and resident**, not C. It is a
    per-nibble loop reached from inside an emulated slice, which is the one
    place C is forbidden, and an `ovl_` there would be a 400 ms floppy seek
    with the machine stopped behind it. The draft's "ports as-is to strict
    16-bit C" reading is deleted.
19. **Every foreign frame is driven off the SAME dirty-line set the windowed
    flush computes**, against a foreign-frame shadow in a heap claim. The
    draft's "called once per frame" would have reintroduced the whole-raster
    write that the dirty-page bitmap, the write window, the scan-line map and
    the span compare exist to avoid.
20. **`COPYING` and `README.TXT` ship in wave 1**, not wave 7: a GPL-derived
    binary is on a floppy from wave 1, and the C64 disk recipe names both as
    prerequisites **and** payload, so a recipe modelled on it fails with *No
    rule to make target* without them.
21. **`docs/INDEX.md` is regenerated in wave 1, in the SAME commit** as the
    `CC_PACKAGE` line and the two new `docs/*.md`. `all` depends on
    `checkdocs`, and `os88index.py --check` fails the build if a byte would
    change - and wave 1 gives it three reasons to change, which would
    otherwise turn every `make` and the whole fast tier red for six waves.
22. **`vm/386-apple2` in wave 1; `vm/xt-apple2` and `vm/286-apple2` in wave
    7**, with the measurement that justifies them. An XT target before anyone
    has measured the port there is a claim, not a machine.
23. **No wave-1 size line.** The first honest one is at the end of wave 2 with
    the whole core in, and five figures are reported at every wave from then
    on: resident image, bss, `APPLE2.OVL`, resident shims, largest C frame.

---

## Waves

Every `done_when` below is **automated evidence** (Decision 12 and the
verification list): the host harnesses, `make a2cputest`, `make a2bandbench`,
and QEMU driven over QMP with screendumps. **No `done_when` rests on 86Box.**

**Waves 1-5 and 7 are this PR. Wave 6 is the follow-up PR** (Decision 13) and
keeps its number.

| wave | title | in this PR |
|---|---|---|
| 1 | The window, the chrome, the screen machinery, the 7-pixel text composer, the licence and the harness | yes |
| 2 | The 6502 core, the Apple II memory model and the II+ keyboard map | yes |
| 3 | The graphics modes, and the rest of the keyboard | yes |
| 4 | The commands, the clipboard, and Applesoft program load and save | yes |
| 5 | The speaker, and the foreign video mode | yes |
| 6 | Disk II, read-only | **follow-up PR** |
| 7 | Polish, the documents, the disks and The Wire | yes |

### Wave 1 - The window, the chrome, the screen machinery, the 7-pixel text composer, the licence and the harness: the redraw path built before there is anything to draw

**Features**

- The window on the C64's exact geometry: content **336 px wide** (the Apple's
  280 letterboxed 16 px left and 24 px right so the composer's OUTPUT stays
  byte-aligned even though its cells are not), **192 + 16 border + 10 status =
  218** content high, **frame = content + 2**, `os88_wm_snap` so the content
  origin is a multiple of 8, and the height asked against
  `os88_video().dock_top` on `apps/c64/c64.c:1684`'s shape - **with the BOTTOM
  ANCHOR**: on a 640x200 desktop `dock_top` is 176 and only ~114 of the
  Apple's 192 scan lines fit, so the visible band anchors to the BOTTOM of the
  Apple frame, because the `]` cursor line and MIXED's four text rows are both
  there.
- **FOUR menus registered** - File, Edit, Machine, CPU, with Video's and
  Audio's rows inside Machine under separators - every string, mnemonic and
  caption from the authority table, all inert or greyed with their fact,
  `AM_NAME` `Apple II+`; the status row with its field layout and its delta
  drawing.
- **The ROM as part 0**: `tools/getapple2rom.py` wired into the Makefile as
  the one owner of `build/apple2-rom/APPLE2.ROM`, `CC_HAS_PARTS` +
  `OP_ASSET` in the shim, `os88_part_seg(0)` in `os88_main`, the 64KB RAM
  claim with its refusal quoting `os88_mem_largest_kb()`, and the fetch-bias
  underflow guard said out loud.
- **THE CHARGEN DECODE AND THE 128-ENTRY 7-BIT REVERSE TABLE BUILT IN
  `os88_main`, RESIDENT** - decided here once and never moved: `crt0` loads
  the `OP_ASSET` part before any C runs, both tables are on the display path,
  and an overlay that can be absent may not own what the flush cannot run
  without.
- **THE DAMAGE MODEL, whole**: the 32-byte dirty-page bitmap ORed on every RAM
  write from a `cs:` mask table, the write window taken over a WATCH RANGE the
  C sets to the live display page, the interleaved page-by-window to SCAN LINE
  mapping with both screen-hole tests and the MIXED split, the 7,680-byte
  frame shadow, the flush's order, the span compare, the k-row scroll test on
  a source signature, **AND THE FLASH PHASE** - a counter in host ticks (~5
  ticks for 16 frames at 60 Hz) whose flip force-dirties every scan line whose
  text row holds a byte in `$40-$7F`, because flashing changes the glass with
  no memory write and nothing write-driven can see it.
- **`a2_band_text`, the first composer AND A SHIFT-ACCUMULATOR ONE**: the
  Apple's cell is 280/40 = SEVEN pixels, so no cell after the first is
  byte-aligned and a decoded glyph is never a pixel byte. 64 glyphs decoded
  out of the ROM part (bit-reverse; XOR `$7F` for the low-1KB entries whose
  bit 7 is clear) plus a **PER-CELL XOR MASK** derived from the screen byte's
  range and the flash phase - one mechanism covering the ROM's 128 distinct
  bitmaps and flashing both. A span argument, stride 40.
- **The whole harness kit from day one, with every step failing rather than
  passing when its subject is absent**: `hosttest/os88.h`, `a2uitest.c` with
  the pixel model of the glass, `a2memtest.asm` + `.sh` with its four negative
  controls, `tools/a2ref.py` with `--check` (an explicit mode list),
  `--selftest`, `--lumcheck` and `--romshape`, `build.sh` with the stamp file
  and the `a2_say` extractor's expected minimum,
  `tests/a2band/a2bandbench.asm`.
- **`apps/apple2/COPYING` and `apps/apple2/README.TXT`**, and the per-file
  GPL-2+ + four-attribution headers - HERE, not in wave 7 (Decision 20).
- **`docs/INDEX.md` REGENERATED IN THE SAME COMMIT** as the `CC_PACKAGE` line
  and the two new `docs/*.md` (Decision 21).
- **The Makefile**: `CC_PACKAGE` with the `.OVL` and the ROM part, `APPLE2SRC`
  and `APPLE2INC` as WRITTEN prerequisites, the four disk geometries each
  `--verify`'d with `COPYING` and `README.TXT` in the payload, and **ONE**
  86Box machine - `vm/386-apple2`, a copy of `vm/386-c64` that has booted with
  only `fdd_02_fn` and the uuid changed (Decision 22).
- **Every file the plan names exists, stubs included** - `a2cmd.c`,
  `a2prog.c`, `a2disk.c`, `a2about.c`, `a2fsx.inc`, `a2nib.inc` - because a
  file a later wave adds is a build the tree does not know about.
  `CC_HAS_OVL` is on and `APPLE2.OVL` exists.

**Files**

`apps/apple2/apple2.c`, `apple2.asm`, `a2assoc.inc`, `icon.inc`, `a2scr.c`,
`a2menu.c`, `a2band.inc`, `a2mem.inc`, `a2cpu.inc` (the shell, the scratch,
the memory hooks and the dispatch table only), `a2io.c` (stub), `a2kbd.c`
(stub), `a2cmd.c`, `a2prog.c`, `a2disk.c`, `a2about.c`, `a2nib.inc` (stub),
`a2fsx.inc` (stub), `COPYING`, `README.TXT`, `build.sh`,
`hosttest/os88.h`, `hosttest/a2uitest.c`, `hosttest/a2memtest.asm`,
`hosttest/a2memtest.sh`, `tools/a2ref.py`, `tests/a2band/a2bandbench.asm`,
`Makefile`, `docs/APPLE2-SPEC.md`, `docs/APPLE2-PORT-PLAN.md`,
`docs/INDEX.md`, `vm/386-apple2/86box.cfg`.

**Done when**

`make` is GREEN (checkdocs and `os88index --check` included) and
`make apple2disk` builds four verified floppies each carrying `COPYING` and
`README.TXT`. A QMP screendump of `build/apple2.img` in QEMU shows the window
with its title, four menus on the kernel bar, an 8-px border, the 16/24
letterbox and a status row - and a text page poked by the harness composed
with the ROM's OWN character generator at SEVEN pixels a cell, cropped and
zoomed to prove the glyph shapes, including a run of INVERSE glyphs and a
FLASHING run photographed on both phases. `a2uitest` asserts pixel-for-pixel
against `tools/a2ref.py` over the whole 320x192 after every driven step,
including across a flash flip, and prints the cost table in ms;
`a2ref.py --selftest` injects a one-bit defect and the compare FAILS;
`--romshape` asserts the pinned ROM's 128 distinct bitmaps. `a2memtest` passes
with all four negative controls failing. `make a2bandbench` prints
microseconds per cell and per call for `a2_band_text`, `rowspan`, `rowcopy`,
`rowsig` and `band_x2`. **NO SIZE LINE IS QUOTED THIS WAVE and the SPEC says
why.**

### Wave 2 - The 6502 core, the Apple II memory model and the II+ keyboard map: the machine boots to `]` and you can type at it

**Features**

- **`a2cpu.inc`: the derived copy, whole.** 256 opcodes with every illegal
  family, decimal ADC and SBC, the interrupt machinery, the `cs:` cycle table
  with page-cross, taken-branch and branch-page-cross charged by the
  addressing routines and the branch handlers, the signed-word countdown with
  `a2_cut`/`a2_expire` so `ran = asked - cnt` is exact, and the stack wrap
  written as `and di,0x00FF` / `or di,0x0100` (the one-line `and di,0x01FF` is
  wrong and Dormann's `$0D89` is what catches it).
- **The Apple II ladder**: read is
  `cmp bh,0xC0 / jae .high / mov dh,[bx] / ret` - two instructions and one
  compare shorter than the C64's, because there is no `$00`/`$01` processor
  port on a 6502; `.high` goes `$C000-$C0FF` to the io call-out,
  `$C100-$CFFF` to the slot-ROM answer, `$D000-$FFFF` to one ROM bias. Write
  is `cmp bh,0xC0 / jae .high / mov [bx],dh` plus the dirty bit and the
  window; `.high` calls out for `$C000-$C0FF` and DROPS everything above.
- **`a2_rebias`'s four boundaries** - `$C000`, `$C100`, `$D000`, `$FFFF` -
  with the two-compare guard and the BLO/BOUND pair unchanged, because a
  ceiling alone is wrong: a backward `JMP` from the Monitor to `$0800` leaves
  PC BELOW the region.
- **The core's 256-byte scratch at `$CF00-$CFFF` of the 64KB claim, with ZERO
  stated deviations.** The C64's two deviation paragraphs and both of their
  harness cases are deleted rather than transcribed.
- **The soft switches, both directions active**: the keyboard latch and strobe
  with AppleWin's II+ fence over the whole of `$C010-$C01F`, the four video
  switches guarded BY VALUE, `$C030`'s toggle counted (silent this wave), the
  paddles' one-shots and the two buttons, and `$C100-$CFFF` answering `$FF`.
- **THE II+ KEYBOARD BYTE MAP AND THE CTRL-RESET CHORD, MOVED HERE FROM WAVE
  3** so this wave's evidence can be produced with this wave's files:
  AppleWin's `asciicode` row 0 with its rejections and its unconditional
  uppercase fold, the Ctrl+H/I/M folds routed on SCAN, and Ctrl+F2 =
  Ctrl-Reset (also a Machine menu item, which is the guaranteed route).
  Copy/Paste, the F1/F2 buttons and the mouse-arrow rule stay in wave 3. The
  C64 put its level keyboard in the CPU wave for exactly this reason.
- **Reset, both kinds**: power-on lays AppleWin's `FF FF 00 00` pattern with
  its three pokes, sets SP = `$1FF` and writes `$3F2`/`$3F3` = `$55 $55` so
  the Autostart Monitor's power-up check fires; Ctrl-Reset pulls SP down by 3
  and jumps through `$FFFC`.
- **The wake-driven wall slice** - no worker, nothing blocking, an Apple II
  sitting at `]` costing nothing until a key or a tick arrives. With no timers
  and no interrupt source on a bare II+ **there is no alarm scheduler at
  all**: the loop is `r = a2_run(budget)` with no `min()` and no advance,
  which is a real simplification the SPEC states as such.

**Files**

`apps/apple2/a2cpu.inc`, `a2mem.inc`, `a2io.c`, `a2kbd.c`, `a2menu.c`,
`apple2.c`, `hosttest/a2cputest.asm`, `hosttest/a2cputest.sh`,
`docs/APPLE2-SPEC.md`.

**Done when**

`make` is green. `make a2cputest` passes all twelve rows with every negative
control failing - Dormann's functional test to its success trap, 262,144
decimal cases against `tools/c64dec.py`, and the seven Apple II memory-model
rows. In QEMU: a screendump of the window showing the Apple II+ banner, the
`]` prompt and the cursor FLASHING (two dumps, one per phase); a second after
typing `PRINT 2+2` showing `4`; a third after Machine > Ctrl-Reset showing the
prompt again. **THE FIRST HONEST SIZE LINE is quoted here** - resident image,
bss, `APPLE2.OVL`, resident shims, largest frame - and checked against the
55,000 trigger and the 54,000 end-of-wave-5 ceiling.

### Wave 3 - The graphics modes, and the rest of the keyboard

**Features**

- **A GREYING SENTENCE COMES BACK HERE.** Wave 1 shortened Machine >
  Joystick... to `The paddles answer centre and there are no buttons in this
  build.` because the second half named a control the build did not have. This
  is the wave that reads F1 and F2, so it restores
  `F1 and F2 are the game buttons PB0 and PB1 - a departure from AppleWin's
  Left-Alt / Right-Alt.` in `a2menu.c` and in APPLE2-SPEC section 10.3
  together (they are **game buttons**, not //e Apple keys - section 6.3).

- **`a2_band_lores` and `a2_band_hires`, sharing `a2_band_text`'s 7-bit shift
  accumulator and span argument** - the whole composer set is one class, which
  is the correction that makes wave 1's work reusable rather than special.
  Hi-res: 40 source bytes to 35 output bytes a scan line through the 128-entry
  reverse table, the high bit DROPPED (MII and apple2emu drop it in mono;
  AppleWin instead shifts by half a dot at signal level, which is a NAMED
  DEPARTURE - expressing it needs 560 pixels, which is wave 5's business).
  Lo-res: 7x4 blocks, two nibbles a byte selected by `(line/4)&1`, through a
  16-entry luminance ladder as `{and, xor}` pairs, because an equal-luminance
  cell is `{0x00, level}` and an XOR alone cannot express a uniform block.
- **MIXED** (the top 160 scan lines in the graphics mode, the bottom 32 scan
  lines of text), **PAGE2**, and the mode dispatch - a mode change that
  actually changes the renderer marks the whole frame dirty, and every switch
  is guarded BY VALUE (the C64 measured 25 forced full-width blits, ~234 ms,
  for setting both flags on a register write that changed nothing).
- **The tier table, written FROM `tests/a2band`'s measured numbers**: the
  flush rate (`CPU_8086` flushes every OTHER tick), the FLASH PHASE's refusal
  on `CPU_8086` with its measured cost in the greying, and fullscreen at 2x on
  VGA / 2x-horizontal on CGA and Hercules / 1:1 centred on the 8086 tier, with
  `a2_scw`/`a2_sch` decided in ONE place and the doubling at BLIT time.
- **The rest of the keyboard**: F1 and F2 as the Open-Apple and Solid-Apple
  BUTTONS read through `os88_key_down` (a departure from AppleWin's
  Left-Alt/Right-Alt, recorded with its reason: Alt+Enter is this port's
  fullscreen chord), Ctrl+F3 = Open-Apple-Ctrl-Reset, and **ALT+ENTER
  fullscreen in both directions in RESIDENT code** with Ctrl+F kept as
  SPEC.md 11.2.1's unconditional door.
- **The keyboard-mouse rule** (`C64-SPEC §7.6`): on a machine with no mouse
  the kernel eats the arrows, Space and Del as its pointer, and ScrollLock
  hands them back. Inferred from the observable - the down-map says an arrow
  is held and `os88_onkey` has never once delivered one - over three
  consecutive polls, with a one-way latch.

**Files**

`apps/apple2/a2band.inc`, `a2scr.c`, `a2kbd.c`, `a2io.c`,
`hosttest/a2uitest.c`, `tools/a2ref.py`, `tests/a2band/a2bandbench.asm`,
`docs/APPLE2-SPEC.md`.

**Done when**

`make` is green. Screendumps of `GR : COLOR=13 : PLOT 20,20`, of
`HGR : HPLOT 0,0 TO 279,159` and of a MIXED screen showing four text rows
under a hi-res picture, each cropped and zoomed. `a2uitest` asserts all three
composers pixel-for-pixel against `a2ref.py`. `make a2bandbench` prints
per-source-byte and per-call microseconds for all three, and the cost table in
`APPLE2-SPEC` is rewritten from them - a full 280x192 hi-res repaint in ms, a
one-line change in ms, a k-row scroll in ms, **ONE FLASH PHASE FLIP in ms**
(the number that decides the `CPU_8086` greying), entering fullscreen at 2x in
ms. **ON `VIDEO=cga`, with `mouse.py --screen 640x200`**: a greyed menu item
(grey rounds to black on 1bpp, so it must be a checkerboard), the status row,
**A MIXED SCREEN and A SCREEN THAT HAS SCROLLED ONCE** - the last two prove
the bottom anchor puts the text window and the `]` cursor on the glass on a
200-line adapter. The size line is quoted against the 55,000 trigger and the
54,000 ceiling; if it is past 54,000, lever 1 is pulled this wave.

**WHAT WAVE 3 MEASURED, and it is done**

| | measured |
|---|---|
| resident image | **31,426** (wave 2: 28,396; the wave's own first cut 31,124) |
| bss | **13,604** (wave 2: 12,240) |
| **resident total** | **45,030** of 61,440 - **9,970 under the 55,000 trigger**, 8,970 under the end-of-wave-5 ceiling |
| `APPLE2.OVL` | **996** (was 883) - the About panel's magnification arithmetic is overlay code |
| resident shims | **8**, unchanged |
| largest C frame | **54** of 96 |

**Lever 1 was NOT pulled**: `a2_x2b`, the 1,280-byte doubled band, stays in
bss where the flush cannot refuse it. The estimate this plan carried for the
two composers was ~+1,400 "with the shared accumulator" and the actual is
~+1,500 of composer, which is the shared accumulator being real: `a2_pack` is
one routine the three of them CALL, extracted from the text composer, so the
class is a fact about the image rather than a family resemblance.

`make a2bandbench`, per call and **per SOURCE byte**, on the icount harness
(one PIT count is 0.359 ms of a real 4.77 MHz XT):

| composer | counts/op, 5 groups | XT ms | per source byte |
|---|---|---|---|
| `a2_band_text` | 35.000 (40 bytes) | 12.57 | 0.73 us host |
| `a2_band_lores` | 28.625 (40 bytes) | 10.28 | 0.59 us host |
| `a2_band_hires` | 50.875 (320 bytes, 8 scan lines) | 18.26 | 0.13 us host |
| `a2_band_hires` x **1 line** | 7.125 (40 bytes) | 2.56 | 0.14 us host |
| `BAND_X2 8r x 40b` | 29.500 | 10.59 | - |
| `BAND_X2 1r x 7b` | 1.125 | 0.40 | - |
| `BLIT1 640x16 stride 80` | 8.875 | 3.19 | - |

**The text composer re-measured at wave 1's own 35.000 counts an operation**,
so every published row that rests on it stands unchanged, and the two new
composers are priced separately in the cost model rather than at the text
composer's figure. **Hi-res and the doubler are each TWO measurements now**,
because each takes a range: a group is not one unit of work for a composer
that can be asked for one scan line, and a two-point fit in groups alone
prices a one-line call 23 % too high. APPLE2-SPEC section 7.9.2 and 7.9.3
carry the whole of it, including the whole-operation rows: a full hi-res
repaint **621.9 ms**, one changed hi-res scan line **4.2**, a full lo-res
repaint **430.7**, a MIXED screen **91.2**, a mode switch that draws the same
picture **305.0** with **zero blits**, eight soft-switch reads that change
nothing **0.0**, a scroll in a graphics mode **396.6** (the shift test is a
TEXT-mode test and refuses), entering full screen at 2x **783.6**, a
fullscreen recompose that draws nothing **410.3** with **zero `a2_band_x2`
calls**, and one changed cell at 2x **12.9** with 56 source byte-rows doubled.

**Five of those rows are the review's** (sections 7.4, 7.8, 11). A MIXED flip
was **504.9** because `a2_video_set` called `a2_dirty_all` on it, and the split
moves four rows and not twenty-four - `a2_dirty_split` is the narrow mark, and
both directions are now measured at 20 groups. `a2_band_x2` sat on the COMPOSE
side, so a flush that recomposed the frame and blitted nothing still spent 253
ms doubling it (**663.5 against 410.3**); it is `a2_emit`'s now - and, after
the second review, **per RUN**, so a keystroke at 2x doubles the 56 source
byte-rows it draws rather than the 320 the row holds (**12.9 against 21.5**).
`a2_band_hires` composed all eight scan lines whatever the flush had marked,
so the per-scan-line damage model delivered the compare and not the compose
(**4.2 against 7.6** on a one-line `HPLOT`). The About panel's hold range was
never taught about the magnification and, at VGA fullscreen, held Apple lines
[131,191] while covering [66,125] - two DISJOINT ranges, so the picture ate the
card on every flush and the bottom third of the screen froze. And the lo-res
luminance ladder was transcribed from a block **AppleWin's own source
disclaims as a placeholder**: under it `GR : COLOR=3` and `COLOR=6` drew black
where every live reference palette draws them white. The harness asserts all
five.

**The tier table is written from those numbers** (section 7.8): 2x on both
axes on a VGA, 2x horizontal on CGA and Hercules, **1:1 on the `CPU_8086`
tier** because the doubler is 253 ms on a whole-frame repaint; the flush every
OTHER tick there; and the **FLASH PHASE REFUSED** there with the measured
43.1 ms a flip - 157 ms in every second - in the greying (section 10.3). Two
greying sentences came back whole: Joystick...'s `The game buttons PB0 and PB1
are F1 and F2 - a departure from AppleWin's Left-Alt / Right-Alt.`, and
Flashing text's measured cost.

**On the glass**, in `build/port-shots/`: `GR : COLOR=13 : PLOT 20,20` with
two more plots and an HLIN/VLIN pair, cropped at zoom 8 to show the 7x4 block;
`HGR : HPLOT 0,0 TO 279,159`; a MIXED screen with the text window scrolling
under the picture; full screen at 2x on a VGA, zoomed to show every Apple
pixel as a 2x2 block, and Ctrl+F back out of it; `PB0 SEEN AT F1` printed by
an Applesoft loop polling `$C061`; and **the whole 1bpp pass** - on
`VIDEO=cga` a greyed item as a checkerboard, the status row, a MIXED screen
and a screen that has SCROLLED once (the bottom anchor putting the `]` on a
200-line adapter), and on `VIDEO=herc` the same machine at 720x348 plus full
screen at 2x HORIZONTAL, which is that adapter's own row of the tier table.

### Wave 4 - The commands, the clipboard, and Applesoft program load and save

**Features**

- **A GREYING SENTENCE COMES BACK HERE.** Wave 1 shortened Machine > Configure
  Slots... to `No Disk II in this build.` because the second half pointed at
  two routes a build with both greyed does not have. This is the wave that
  makes them work, so it restores `Load Program reads an Applesoft program,
  and Paste types a listing in.` in `a2menu.c` and in APPLE2-SPEC section 10.3
  together.

- **The four menus go live**, each item doing what its authority file says it
  does, with MII's dynamic Stop/Stopped and Running/Continue retitling and the
  check items' labels; every greyed item's refusal toast names the fact.
- **Edit > Copy**: the 40x24 text page walked through the interleaved row
  bases, the Apple screen encoding folded to ASCII by one 128-entry table and
  a mask, through `a2_copy_row` in ASSEMBLY, **ONE call a row**, into a
  transient heap claim. **Edit > Paste**: the peek/consume handshake, `\n` to
  `\r`, the emulated program setting its own rate. **BOTH BODIES RUN FROM THE
  TOP OF THE WAKE with no lock held** - the C64's wave-3 lesson, where the
  same commands cost 2,000 bridge crossings and the call-counting cost model
  charged one.
- **File > Load Program... / Save Program...**: `os88_fdlg` for the picker,
  the read into `$0801` with the documented 2-byte-length-prefix sniff (if
  word 0 equals filesize - 2, skip it; otherwise headerless), then
  `TXTTAB` = `$0801` and `VARTAB` = `ARYTAB` = `STREND` = `PRGEND` = end. Save
  writes `$0801` to `VARTAB-1`, headerless. Written from `A2_BASIC.SYM`'s
  addresses because no reference implements either half.
- **`CC_ASSOC` declares `BAS`**, so a `.BAS` beside the program opens on the
  first double-click of a cold boot with no prior run.
- **CPU > Warp** (the wall slice's cap, and it silences the speaker), **CPU >
  Pause**, **Machine > Ctrl-Reset**, **Machine > Power On** with AppleWin's
  confirmation wording on TWO rows - `Are you sure you want to reboot?` and
  `(All data will be lost!)`. **File > Quit** through `OSAPI_WM_CLOSE` and
  answered in the RESIDENT half, because it is the one command that must work
  on a disk whose `APPLE2.OVL` is missing.
- **The overlay's first-wake probe**: `ovl_a2_init()`, called from the FIRST
  wake (the `.OVL` cannot be resolved from `os88_main` - there is no instance
  yet), printing `Unable to load APPLE2.OVL.` on the status row AND toasting
  when it refuses. **It carries COMMAND SHELLS ONLY** - the CHARGEN decode and
  the reverse table are in `os88_main` from wave 1, so a disk with no overlay
  is a working Apple II whose menu commands refuse politely, not a black
  window. `a2_ovl_ready(win)` is the fence that keeps a LOCKED callback from
  going to the floppy for 400 ms with the whole machine stopped behind it.

**Files**

`apps/apple2/a2menu.c`, `a2cmd.c`, `a2kbd.c`, `a2prog.c`, `a2mem.inc`,
`apple2.c`, `a2assoc.inc`, `hosttest/a2uitest.c`, `docs/APPLE2-SPEC.md`.

**Done when**

`make` is green. A driven QMP session, screendumped at each step: paste a
five-line Applesoft listing from the clipboard, LIST it, RUN it, Save Program
to `WORK.BAS`, close the window, double-click `WORK.BAS` on the Disk window
(cold - the association is in the header), and see the same listing come back.
Edit > Copy of the text page round-trips through the OS clipboard into another
package. **A run with `APPLE2.OVL` DELETED from a scratch disk shows a working
machine at `]` whose menu commands toast their refusal** - the negative
control for the CHARGEN decision. `a2uitest` drives the same script against
the model. Whole-screen Copy is measured in ms and stated with its
bridge-crossing count. Size line quoted.

### Wave 5 - The speaker, and the foreign video mode: colour on a VGA, and the measurement that decides whether the 1bpp foreign modes ship at all

**Features**

- **A GREYING SENTENCE COMES BACK HERE.** Wave 1 shortened Machine >
  `Color NTSC` to `The window is monochrome.` because the second half named a
  mode the build did not have. This is the wave that writes it, so it restores
  `Colour is in the foreign video mode - Machine > Toggle Fullscreen on a
  VGA.` in `a2menu.c` and in APPLE2-SPEC section 10.3 together - and if the
  measurement cuts the 1bpp foreign modes, the restored sentence says VGA and
  the row greys on the other two adapters with the measured fact.

- **The `$C030` estimator**: a last-toggle emulated-cycle stamp,
  `hz = 1,020,484 / (2 x delta)` through ONE 32-bit assembly muldiv, played
  with duration 0 when the last N intervals agree within a tolerance, taken
  down by the single `a2_sound_stop()` after a silent 1/18 s and by pause,
  reset, warp, Machine > Audio > Mute and the About panel. Capability
  established once in `os88_main`; a refused grant retried, bounded at eight
  wakes, then dropped with the fact said once.
- **The four `OSAPI_FSX_*` thunks** added to `apps/cc/os88thunk.asm`,
  `apps/cc/os88.h` and `apps/apple2/hosttest/os88.h` **in ONE edit**, with the
  before/after `os88pkg` lines for CWORD, RUNCPM, C64, WEAVE and LOOM recorded
  in `APPLE2-SPEC` section 17.
- **THE MEASUREMENT FIRST, THEN THE WRITERS**: `make a2bandbench` gains
  foreign-frame rows and prints ms/frame for each FSX writer against the
  windowed flush ON THE SAME CHANGE SET, under
  `make test QEMU="...-icount shift=3"` where one PIT count is 0.359 ms of a
  real 4.77 MHz XT - which is how every other number in this plan is taken and
  needs no 86Box machine. **`FSXM_VGA13` ships on its COLOUR, which is not in
  question. `FSXM_CGA640` and `FSXM_HERC` ship only if they beat the windowed
  path**: they are 560x192 = 13,440 bytes a frame against the windowed 7,680,
  on the slowest machine, so "an XT gets its speed back" is a claim to prove
  and not to assert. If they lose, they are cut, ~1,200 bytes come back, and
  Machine > Toggle Fullscreen greys on those adapters with the measured fact.
  (There is no `Video` submenu: the kernel bar has no submenu mechanism, so
  MII's Video rows are folded into Machine.)
- **Every foreign frame driven off the SAME dirty-line set the windowed flush
  computes**, against a foreign-frame shadow in a heap claim taken at
  fullscreen-LATCH time (where a refusal is legal).
- **The bracket's rules obeyed by hand** because none is checkable from C: the
  entry is a resident C function whose address is taken (never an `ovl_`);
  after the first `os88_fsx_mode` every drawing slot is off-limits until it
  returns; keys come from a polled `int 16h` and the mouse from `OSAPI_MOUSE`
  with the app edge-detecting buttons; frames are paced with `os88_fsx_wait`
  and never `task_sleep`; the exit is the proc returning, on F with Esc as the
  escape hatch (SPEC.md 53.7), and the ~200 ms `wm_paint_all` it costs is paid
  once a session.

**Files**

`apps/cc/os88thunk.asm`, `apps/cc/os88.h`, `apps/apple2/hosttest/os88.h`,
`apps/apple2/a2io.c`, `a2fsx.inc`, `a2cmd.c`, `a2scr.c`, `a2menu.c`,
`tests/a2band/a2bandbench.asm`, `docs/APPLE2-SPEC.md`.

**Done when**

`make` is green and `make test-full` proves the four new thunks broke no other
C package, with the `os88pkg` line for CWORD, RUNCPM, C64, WEAVE and LOOM
quoted before and after. `make test-snd` captures the Applesoft beep and
`tools/sndcheck.py` finds a tone at the expected frequency. `a2bandbench`
prints ms/frame windowed against foreign for all three writers and the SPEC
records which of them shipped and why. A screendump inside `FSXM_VGA13` shows
a hi-res picture in artifact colour and a lo-res screen in its 16 colours; if
CGA640/HERC shipped, one inside each (Hercules through `tools/hercshot.py`,
because screendump is black there). **THE END-OF-WAVE-5 SIZE LINE IS THE
GATE**: past 54,000 resident, wave 6 is a follow-up PR whatever Decision 13
said, and the SPEC says so with the number.

### Wave 6 - Disk II, read-only - **A FOLLOW-UP PR** (Decision 13)

Not in this PR. Its number is kept so the plan and the branch agree. **Its
first job is a measured size line.**

**Features**

- **The P5 boot ROM at `$C600`** out of the ROM part's offset `0x3800`, so
  `PR#6` and the Monitor's own boot path work - **AND ITS CONSEQUENCE
  STATED**: `DISK2.rom` carries the Autostart boot signature, so from this
  wave a power cycle with NO disk boots the drive and spins forever, exactly
  as a real II+ does. The status row says the drive is spinning and names
  Ctrl-Reset as the way to `]`, so authentic behaviour is not read as the port
  having frozen.
- **The controller as one switch on `(addr & 0x0F)`**: the four phase magnets
  with the step rule and the 0..79 half-track clamp, motor with its off timer,
  drive select, the Q6/Q7 mode decode and the read latch - **AND THE SAMESLOT
  TRAP**, a latch read within 6 emulated cycles of the previous one answering
  `data >> 4` rather than the next nibble, without which DOS 3.3 sits in a
  wait loop with no error.
- **The storage model, stated before a line is written**: a mounted image
  lives in a 143,360-byte HEAP CLAIM per drive, over 64KB and therefore
  reached by segment arithmetic in `a2mem.inc`; drive 2 refuses with
  `os88_mem_largest_kb()` in the sentence. The alternative - re-reading the
  track off the floppy at each seek - is ~400 ms an `int 13h` with the machine
  stopped, seconds for a boot and a CATALOG, and is refused with that number.
- **Per-track 6-and-2 nibblisation on demand** into a 6,656-byte heap claim,
  one call a track change, in `a2nib.inc`'s hand-written RESIDENT assembly
  (Decision 18). Priced in ms per track change in the cost table, not asserted
  as free.
- **Machine > Configure Slots...** - MII's own route to the disks, not four
  invented File rows - with its two drive rows, `Drive 1:`/`Drive 2:`, Select
  becoming Eject, the empty-field prompt, and the exactly-143,360 check whose
  alert names the size difference in MII's human-readable form. `.DSK` and
  `.DO` take the DOS 3.3 order table, `.PO` the ProDOS one, and
  `.NIB`/`.WOZ`/`.2MG`/`.HDV` are refused BY NAME with the reason.
  `CC_ASSOC` gains `DSK`, `DO` and `PO` **in this wave and not before**.
- The drive lamp and track/sector readout on the status row.
- **No `.DSK` ships.** `tools/a2dsk.py` builds the gate's fixture, and the
  README and the Wire page say where a reader gets images.

**Files**

`apps/apple2/a2io.c`, `a2nib.inc`, `a2mem.inc`, `a2disk.c`, `a2menu.c`,
`a2scr.c`, `a2assoc.inc`, `tools/a2dsk.py`, `tests/apple2.py`,
`tests/suite.py`, `docs/APPLE2-SPEC.md`.

**Done when**

`make` is green. A fixture `.DSK` built by `tools/a2dsk.py`, inserted through
Machine > Configure Slots... , boots to the DOS 3.3 prompt and CATALOG lists
the files the fixture put there - screendumped, cropped and zoomed. A power
cycle with NO disk shows the drive spinning and the status row saying so, and
Ctrl-Reset reaches `]`. A wrong-sized image raises the alert with the size
difference in it. Drive 2 on a constrained machine refuses with
`os88_mem_largest_kb()` in the sentence. The nibbliser's ms per track change
is in the cost table. `tests/apple2.py` drives the whole sequence over QMP and
is registered in `tests/suite.py`.

### Wave 7 - Polish, the documents, the disks and The Wire

**Features**

- **The About panel on MII's shape**: `The Apple II Plus Emulator` (spelled
  out - the kernel face is ASCII 32..126 and MII's `][+` glyph draws NOTHING
  here), the version, what this port is, VICE / AppleWin / MII / apple2emu
  named with their licences, and the ROM copyright line. **NOTHING about how
  the build renders** - the monochrome fact is on its Video grey and the
  speaker fact on the status row. **TWELVE ROWS MAXIMUM**, a 640x200
  compatibility constant carrying a comment saying so: a control's y is
  `6 + row*10` and nothing clamps it.
- **The product name checked across all four surfaces** - window title and
  About panel `Apple II Plus Emulator`, `AM_NAME` and Wire title
  `Apple II+`, package stem `APPLE2` - two strings with a stated rule, not
  three that drifted.
- **`WELCOME.BAS`** - an Applesoft listing written for this port that
  demonstrates what this build renders and claims nothing it does not.
- **The four disk geometries re-verified with the whole payload in ONE folder
  `APPLE2/`** (package, overlay, `README.TXT`, `COPYING`, `WELCOME.BAS` - the
  `.OVL` is resolved in the LAUNCHING instance's current directory, and a
  document double-click leaves that directory on the document's, so all five
  are one folder or every menu refuses politely). A folder of its own on
  `make allapps` and on `make live`.
- **`vm/xt-apple2` and `vm/286-apple2` CREATED HERE**, with the measurement
  that justifies them: each a copy of a `vm/*-c64` that has booted with only
  `fdd_02_fn` and the uuid changed. **The XT is where the measured percentage
  of a 1.02 MHz Apple II is taken** and written into the status row's units,
  and where the two chords (Ctrl+F2, Ctrl+F3) and Alt+Enter are confirmed on a
  real AT/XT BIOS rather than on QEMU's SeaBIOS. Manual evidence; no gate
  rests on them.
- **`docs/APPLE2-SPEC.md` and `docs/APPLE2-PORT-PLAN.md` reconciled against
  the LAST build** - every number re-measured, because the C64 re-edited one
  figure three times in one session; the `README.md` table row;
  `docs/INDEX.md` regenerated again if any doc was added; the port skill's
  stale `CC_ASSOC` line corrected; `make test-full` green.
- **The Wire**: a branch `apple2-wire` off os8088-web's `main`. One record in
  `data/wire.json` on the C64's exact shape - stem `APPLE2`, title
  `Apple II+`, kind 0, tier from the MEASURED speed, flags `[new]`, files
  `[APPLE2.O88, APPLE2.OVL, {README.TXT}, {COPYING}]`, a description
  pre-wrapped to 5 lines of 27 characters, an honest `page` that says what an
  XT does and does not do and that colour is a VGA feature - plus
  `public/wire/pkg/`, a generated `public/wire/pic/APPLE2.PIC` (128x64, 1,024
  bytes, a 1:1 crop of a real screendump, never scaled),
  `tools/scenes.apple2.json`, the regenerated `catalog.bin` and `index.html`,
  and `site/wire.html`'s stale program count. `WF_DISK` is set automatically
  because the ROM part sets the header's flags bit 2. Verified from the OS
  side with `python3 tools/os88wire.py --verify`. **NO COMMITS IN THE WEB REPO
  UNTIL THE OS SIDE IS DONE and the user has said go.**

**Files**

`apps/apple2/a2about.c`, `README.TXT`, `COPYING`, `WELCOME.BAS`, `icon.inc`,
`Makefile`, `README.md`, `docs/APPLE2-SPEC.md`, `docs/APPLE2-PORT-PLAN.md`,
`docs/INDEX.md`, `.claude/skills/port-to-os8088/LESSONS.md`,
`tests/apple2part.py`, `tests/suite.py`, `vm/xt-apple2/86box.cfg`,
`vm/286-apple2/86box.cfg`, and in os8088-web on branch `apple2-wire`:
`data/wire.json`, `public/wire/pkg/`, `public/wire/pic/APPLE2.PIC`,
`public/img/apple2/`, `tools/scenes.apple2.json`, `site/wire.html`.

**Done when**

`make test-full` green. `make apple2disk` builds and verifies all four
geometries and `make allapps` carries `APPLE2/` as a folder. A screendump of
the About panel on `VIDEO=cga` with its OK button inside the window.
`vm/xt-apple2` and `vm/286-apple2` boot and their screendumps are in the PR,
with the measured XT speed quoted and the three chords confirmed there.
`python3 tools/os88wire.py --verify` passes against the web repo's catalog,
`python3 tools/build.py --check` and `linkcheck.py` are clean, and the branch
is prepared but unpushed.

---

## Verification

**Automated - every gate in this plan rests only on these.**

- **THE HOST HARNESS, from wave 1**, run by `apps/apple2/build.sh` through a
  stamp file that is a prerequisite of `build/apple2.raw.asm` - a check that
  fails leaves no stamp and the compile does not run. **EVERY STEP FAILS
  RATHER THAN PASSING WHEN ITS SUBJECT IS ABSENT:** (1)
  `getapple2rom.py --check`; (2) `build/a2uitest`, the whole program compiled
  by clang against `apps/apple2/hosttest/os88.h` with
  `-I apps/apple2/hosttest` ahead of `-I apps/cc`, `-DA2_HOST`; (3)
  `tools/a2ref.py --check <explicit mode list>` - it errors on a mode it has
  no reference for, so wave 1 asks for text alone and wave 3 adds lores and
  hires, instead of a green pass over three modes that do not exist - plus
  `--selftest` (inject a one-bit defect, require the compare to FAIL),
  `--lumcheck` over all 256 ordered luminance pairs, and `--romshape` (the
  pinned ROM yields 128 distinct 8-byte bitmaps and block `$00` = block `$80`
  XOR `$7F`); (4) `hosttest/a2memtest.sh`; (5) the `a2_say()` literal walk
  with an explicit expected **MINIMUM**, because a corpus of zero literals
  otherwise prints green from wave 1 to wave 4.
- **`a2uitest`'s glass model is a PIXEL model, not a call log**: `blit1`
  writes real pixels into a 320x192 bitmap, `gfx_scroll` moves it and fills
  the vacated rows with GARBAGE, `font_run` clears the cell whole, the armed
  clip is ENFORCED (a pixel written outside it fails with its coordinates),
  and after every driven step it asserts pixel-for-pixel that the glass shows
  what the shadow says it shows. **IT DRIVES THE FLASH PHASE ACROSS A FLIP**
  and asserts that exactly the scan lines whose text rows hold `$40-$7F` bytes
  were forced. **Verify the stubs model what the machine does** - the C64's
  `blit1` stub REFUSED for a whole wave, so the cost table priced the fallback
  and the number looked fine.
- **THE COST TABLE, printed on every build in MILLISECONDS and not in calls**:
  PERFORMANCE.md's 756 us a `gfx_*` call and 900 us a glyph cell applied to
  counted calls, plus `tests/a2band`'s measured per-cell and per-source-byte
  figures, plus 38 us a scratch thunk, ~190 us `a2_dirty_take`, 58 us a bridge
  crossing, 26 us a near call. The whole-operation rows are listed in
  `APPLE2-SPEC` section 7.9. **The cost FIXTURE must not be a full screen of
  one character** - a fixture filled to the cap measures the refusal path.
- **`make a2cputest`** - the core's twelve-row gate on a real x86,
  `%include`ing the SHIPPING `a2cpu.inc` into a boot sector, booting raw QEMU
  with SS != DS and reading the serial port. Every perturbation reaches the
  environment or the tables AT RUNTIME, never the source. Every row has a
  negative control the harness requires to FAIL (row 1's is `ADC #` dispatched
  to `ORA #`; row 6's is the ES reload NOPped out of the shipping text at
  runtime). **ROW 8 IS NAMED AND IS NOT OPTIONAL**: reads with side effects -
  `$C010`, `$C030`, `$C050-$C057`, `$C061-$C063`, `$C064-$C067`, `$C070` are
  all read-active on an Apple II, and a C side that treats a read as pure
  gives a machine that boots to `]` and then never changes video mode. Klaus
  Dormann's `6502_functional_test` at the same pinned SHA-256 the C64 gate
  uses, never committed. Minutes, therefore NOT in `build.sh`.
- **`make a2memtest`** - `a2mem.inc`'s and `a2band.inc`'s string loops on a
  real x86 with SS != DS and an ES sentinel, four negative controls, one each
  for the ES, DF, BP and DS the discipline check claims to check. Seconds,
  therefore IN `build.sh`. From the Disk II wave it also covers the segment
  arithmetic that reaches a track inside a claim larger than 64KB.
- **`make a2bandbench`** then
  `make test TESTAPPS=build/a2band.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"`
  - one PIT count = 0.359 ms of a real 4.77 MHz XT. This is where the tier
  table, every millisecond in the cost table AND WAVE 5'S SHIP-OR-CUT DECISION
  for `FSXM_CGA640` and `FSXM_HERC` come from; it needs no 86Box machine,
  which is why the foreign-mode measurement is not blocked on `vm/xt-apple2`
  landing in wave 7. It arms the clip on its rerun callbacks, saves ES around
  every blit, and preflights `OSAPI_GFX_BLIT1`. **MEASURE THE BAND BEFORE
  BELIEVING A PER-CELL GUESS**: RUNCPM's first row composer was 306 us a cell
  against a model that had guessed 40, and this port's composers are all
  shift-accumulator composers, a class this tree has never measured.
- **`make` IS GREEN AT THE END OF EVERY WAVE**, not only at wave 7. `all`
  depends on `checkdocs` and runs `tools/os88index.py --check`, which fails
  the build if regenerating `docs/INDEX.md` would change a byte - and the
  `CC_PACKAGE` line, `apps/apple2/apple2.asm` and the two new `docs/*.md` are
  three separate reasons it would. `make test-fast` hangs off `make`, so a
  wave that forgets this turns the whole fast tier red.
- **QMP-DRIVEN SCREENDUMPS, one per user-visible claim**, cropped and zoomed
  before anything is concluded. The recipe:
  `tools/qmp.py build/qmp.sock quit; rm -f build/qmp.sock` first, then
  `make test TESTAPPS=build/apple2.img`, then one process doing `to X Y` plus
  two `mouse_button` 1/0 pairs at 80/100 ms for a double-click,
  `tools/qmp.py ... 'sendkey ...'` for keys, and
  `tools/shot.py build/qmp.sock out.png --crop X,Y,W,H --zoom 8`. `--screen`
  MUST match the adapter. Save into a scratch copy of the image so a test's
  writes do not dirty the build. **A FLASHING run needs TWO dumps ~5 ticks
  apart, or it proves nothing.**
- **THE 1BPP PASS IS BINDING AND IS NOT OPTIONAL**, and it is bigger than a
  C64 pass: `make test VIDEO=cga TESTAPPS=build/apple2.img` with
  `tools/mouse.py --screen 640x200`, and `VIDEO=herc HERCSEG=0x7000` read with
  `tools/hercshot.py build/qmp.sock 0x70000`. Look at a greyed menu item, at
  the About panel's OK button, at the disk dialog, at the status row - **AND
  AT A MIXED SCREEN AND A SCREEN THAT HAS SCROLLED ONCE**, because on CGA
  `dock_top` is 176 and only ~114 of the Apple's 192 scan lines fit, so the
  bottom anchor is the only thing putting MIXED's text window and the `]`
  cursor on the glass. QEMU double-scans CGA mode 6 to 640x400, so a dump
  cropped at 640x200 shows the top half only - take every second row.
- **`tests/apple2part.py`** on `tests/c64part.py`'s shape, registered in
  `tests/suite.py`'s soak tier: `APPLE2.ROM` is NOT a file on the disk; the
  package file is image + 14,848; `os88_part_seg(0)` is the segment the C put
  in the machine record; three windows of the ROM read out of the guest equal
  `build/apple2-rom/APPLE2.ROM` byte for byte; the RESET vector at `$FFFC`
  reads `$FA62`; **and the CHARGEN table and the reverse table EXIST AFTER
  `os88_main` AND BEFORE any wake** - the negative control for keeping them
  off the overlay. `tests/apple2.py` drives the wave-4 and Disk II sessions
  over QMP. **Both registered, or the fast tier's own row fails the build.**
- **A DELETED-OVERLAY RUN as a first-class row**, not an afterthought: boot a
  scratch disk with `APPLE2.OVL` removed and screendump a working Apple II at
  `]` whose menu commands toast their refusal. This is what proves the CHARGEN
  decode and the reverse table are resident; placing both in `ovl_a2_init`
  would have shown a black window and a status line, and no existing test
  would have caught it.
- **`make test-full` is the pre-merge gate** and it is what proves the four
  new fsx thunks did not break another C package. Re-measure and quote the
  `os88pkg` line for CWORD, RUNCPM, C64, WEAVE and LOOM before and after that
  edit - CWORD ships with 1,043 bytes spare and four thunks at ~18 bytes each
  is 72 of them.
- **`make test-snd TESTAPPS=build/apple2.img`** and
  `tools/sndcheck.py build/snd.wav` for the beep, with a script that types
  `PRINT CHR$(7)` and one that runs a two-line square-wave POKE loop, so both
  the estimator's steady case and its silence timeout are exercised.
- **The Wire, from the OS side and before any commit in the web repo**:
  `python3 tools/os88wire.py --verify ../os8088-web/public/wire/catalog.bin --pkgdir ../os8088-web/public/wire/pkg`
  and `--dump`, plus `python3 tools/build.py --check` and
  `python3 tools/linkcheck.py` in the web repo. Check `lsof -ti :8788` before
  trusting a local preview. `make && make thewiretest && python3
  tests/thewire.py` is unaffected but is run anyway.

**Manual - recorded, never a gate.**

The three 86Box machines. `vm/386-apple2` in wave 1; `vm/xt-apple2` and
`vm/286-apple2` in wave 7. Each cfg is a copy of the corresponding `vm/*-c64`
that HAS BOOTED, with `fdd_02_fn` and the uuid changed and nothing else.
`git checkout` the cfg before committing and never commit `nvr/`. **THE XT IS
WHERE THE SPEED FIGURE IS TAKEN** and where Ctrl+F2, Ctrl+F3 and Alt+Enter are
confirmed, because a chord table filled in from QEMU's SeaBIOS describes a
BIOS that passes enhanced codes a real AT BIOS drops. Each reading goes into
`docs/APPLE2-SPEC.md` with its date and machine.

---

## Risks

- **THE BUDGET HAS TWO READINGS AND ONLY ONE IS TESTED BY BUILDING.** This
  plan's is top-down from `C64-SPEC §13.0.1`'s MEASURED 39,384 + 13,106,
  signed term by term against the C64's measured files, and lands at ~41,700
  image carried at 43,500. The fit review's is bottom-up and lands at
  60,500-63,500 - at or over `APP_MAX_SIZE` - on the reading that APPLE2 is a
  strict superset of the C64. The disagreement is entirely about `a2io.c` and
  `a2kbd.c`: the C64 models a VIC-II with raster and sprites, a SID, two CIAs
  with timers and TOD, a bank ladder and an 8x8 key matrix with a two-way
  PETSCII chain, none of which exists on an Apple II+, and this plan books
  those as -2,300 while the review books them as zero. **UNRESOLVED UNTIL WAVE
  2'S FIRST HONEST SIZE LINE.** The mitigation is a gate rather than an
  argument: `CC_HAS_OVL` from commit 1, a size line at every wave from 2 on,
  four levers named in the budget in the order they get pulled, and a HARD
  54,000-RESIDENT CEILING AT THE END OF WAVE 5.
- **THE APPLE'S TEXT CELL IS SEVEN PIXELS AND THE DRAFT WAS BUILT ON EIGHT.**
  280/40 = 7, so no cell after the first lands on a byte boundary, a decoded
  glyph is never a pixel byte, and there is no per-cell store shape anywhere
  on this machine - text is the same shift-accumulator class as hi-res, not
  the cheap composer the draft costed. Rendering text at 8 px is not an escape
  either: MIXED puts 160 hi-res scan lines above 32 text ones in ONE
  40-byte-stride frame with one shadow. Corrected here, but it means wave 1's
  only deliverable, `a2band.inc`'s budget line, every per-cell figure and the
  tier table written from them are all NEW measurements with no precedent in
  this tree.
- **THERE IS NO BITMAP COMPOSER IN THIS TREE TO COPY AND NOW NO TEXT COMPOSER
  EITHER.** `apps/c64/c64band.inc` says in the shipping source that it
  composes standard text ONLY, and `tools/c64prg.py` does not exist. All three
  composers, MIXED and the whole page-to-scan-line map are new work, and every
  millisecond quoted for them before `tests/a2band` runs is an estimate. An
  Apple II frame is 40 source bytes x 192 lines against the C64's 40 cells x
  25 rows.
- **EMULATED SPEED ON THE XT IS UNKNOWN AND THERE IS NO C64 FIGURE TO
  BORROW.** The arithmetic: ~400 8088 clocks per emulated 6502 instruction at
  an average 3 cycles gives roughly 3-4% of a 1.02 MHz Apple II on a 4.77 MHz
  8088, ~20% on a 12.5 MHz 286 and ~85% on a 33 MHz 386DX. At 3-4% the `]`
  prompt, typing and short RUNs are usable and anything animated is not. If it
  lands under ~3% the honest answer is the Wire page and the status row saying
  so. Measure on `vm/xt-apple2` in wave 7 and with `a2bandbench`'s icount rows
  from wave 3.
- **THE FOREIGN VIDEO MODE MAY NOT PAY ON THE MACHINE THAT NEEDS IT, AND THE
  COLOUR HALF IS VGA-ONLY.** `FSXM_VGA13` is a VGA mode, so "this port is in
  colour" is a claim about a 386-class machine, which was never the speed
  problem; the XT profiles get `FSXM_CGA640` and `FSXM_HERC`, both MONO and
  both 560x192 = 13,440 bytes a frame against the windowed path's 7,680 -
  1.75x the raster work on the slowest machine, in the name of speed. Wave 5
  therefore MEASURES BEFORE IT WRITES and cuts both if they lose, which also
  returns ~1,200 bytes. Separately, VGA13 at 280x192 = 53,760 pixels with a
  per-pixel artifact index is ~300 ms a frame on a 4.77 MHz 8088 - which is
  why every foreign frame is driven off the windowed flush's dirty-line set
  against a foreign-frame shadow, and never written whole.
- **THE 6502 CORE IS A DERIVED COPY, AND A BUG FOUND IN THE SHARED 93% MUST BE
  FIXED TWICE** - the second fix will be forgotten. The stated trade for not
  making `apps/apple2` `%include` a GPL file from `apps/c64` and not rewriting
  seven of twelve rows of a C64 gate. Mitigation: both cores run the SAME
  fetched oracles (Dormann's functional test at the same pinned SHA-256,
  `tools/c64dec.py`'s 262,144 decimal cases). A factoring into
  `apps/6502/m6502.inc` with the seven hooks as `%macro`s is possible LATER
  and is not a first wave.
- **READS WITH SIDE EFFECTS ARE THE SHAPE THAT BOOTS AND IS WRONG.** `$C010`,
  `$C030`, `$C050-$C057`, `$C061-$C063`, `$C064-$C067` and `$C070` are all
  read-active. The C64's plumbing already calls out for both directions so it
  is a free fit - but a C side that treats a read as pure gives a machine that
  boots to `]` and then never changes video mode. Named gate case,
  `a2cputest` row 8, and a sentence in the SPEC.
- **FLASHING IS THE ONE THING ON THE GLASS THE DAMAGE MODEL CANNOT SEE.** It
  changes pixels with no memory write, so nothing write-driven will ever mark
  its lines dirty and the `]` cursor would simply never blink - which is
  precisely what wave 2's screendump claims to show. The phase counter and its
  force pass are the fix; the cost is up to 24 forced text lines twice a
  second, which on the target is a full text repaint 2x/s. If that measures
  badly the `CPU_8086` tier refuses to flash and greys with the number -
  refusing is legitimate, silently not flashing is not.
- **THE SCRATCH PAGE'S SAFETY IS CONDITIONAL.** `$CF00-$CFFF` of the 64KB
  claim is safe because on a 48K II+ nothing between `$C000` and `$FFFF` is
  ever RAM to the emulated machine. That gives ZERO stated deviations, strictly
  cleaner than the C64's `$FFC0` with its two. **IT STOPS BEING TRUE** the
  moment a Language Card is modelled or a card claims `$C800-$CFFF` expansion
  ROM - both refused here, and the SPEC says so at the scratch's definition.
  NASM gotcha carried over: `[bx + 0xCF00]` is refused under `-w+error`, so
  `A2_SCR_DISP equ A2_SCR_BASE - 0x10000` exists.
- **THE FOUR FSX THUNKS COST EVERY OTHER C PACKAGE ~18 BYTES EACH.** CWORD
  ships at 60,397 of 61,440 with 1,043 spare, so 72 fits - but it is measured
  on all five (CWORD, RUNCPM, C64, WEAVE, LOOM) before and after that single
  edit. A cross-package cost paid by packages that do not use the feature.
- **THE FSX BRACKET'S RULES ARE NOT CHECKABLE FROM C AND ARE THE WHOLE
  FEATURE.** After the first `os88_fsx_mode` every drawing slot renders
  DESKTOP geometry; nothing may touch PIT channel 0, the sound ports or
  `int 10h` mode sets; pacing is `os88_fsx_wait` and never `task_sleep`; no
  events are dispatched; and the entry proc's address must never be an `ovl_`.
  Nothing in the toolchain enforces any of it. The whole bracket lives in one
  function whose header is the rule list.
- **THE TWO RESET CHORDS AND ALT+ENTER ARE A TARGET-CLASS QUESTION AND MUST BE
  CONFIRMED ON IRON.** Ctrl+F2 (scan `0x5F`) and Ctrl+F3 (`0x60`) are in the
  classic non-enhanced set an 83-key XT BIOS delivers; Alt+Enter is AppleWin's
  own chord and an Apple II+ has no Alt key so nothing collides - but QEMU's
  SeaBIOS passes codes a real AT BIOS drops, so the table is filled in from
  the 86Box machines in wave 7. Ctrl-Reset also has a menu item, and Ctrl+F
  remains SPEC.md 11.2.1's unconditional fullscreen door.
- **THE DISK II WAVE'S MEMORY IS ~280KB OF HEAP FOR TWO DRIVES**, on top of
  the 64KB RAM claim, the ~6KB overlay claim and the 6,656-byte nibble track.
  On a 640KB XT that is plausible and not proven; drive 2's refusal path is
  therefore a shipped surface and not an edge case, and the claim larger than
  64KB is reached by segment arithmetic in `a2mem.inc` rather than by any near
  pointer. The alternative (re-read the track at each seek) is ~400 ms an
  `int 13h` with the machine stopped and is refused with that number.
- **THE DISK II WAVE'S NIBBLISER RUNS INSIDE A SLICE, ON THE EMULATED CPU'S
  OWN READ PATH** - resident hand-written assembly, one call a track, never an
  `ovl_`. And the SAMESLOT 6-cycle re-read is the difference between DOS 3.3
  booting and DOS 3.3 sitting in a wait loop WITH NO ERROR AND NO SYMPTOM.
  That wave is also where the port stops booting to `]` on a bare power cycle:
  the P5 ROM carries the Autostart boot signature, so with no disk the drive
  spins forever, authentically, and only the status row saying so keeps that
  from reading as a freeze.
- **THE ROMS ARE APPLE'S COPYRIGHT AND ARE FETCHED, NEVER COMMITTED** - a
  harder case than the C64's committed Commodore ROMs, and the port takes the
  stricter posture that AppleWin and MII do not. Consequence: a build with no
  network and no `build/apple2-rom/` cache CANNOT build the package, exactly
  as `make zdisk` and `make runcpm` cannot. Nothing in `all` reaches it and
  `make clean` must spare the cache.
- **THE THREE EMULATOR-INVISIBLE DEFECTS ALL LIVE HERE**: a visible redraw (a
  full 280x192 repaint is ~200 ms and five host ticks on the target), a
  double-draw flash (setting both the source-changed and glass-unknown flags
  on a soft-switch write that changed nothing cost the C64 25 forced
  full-width blits), and input overrun (a paste feeder that types faster than
  the emulated program consumes loses characters at random on the target and
  never under QEMU). None shows in a screendump; the harness's cost table and
  its shadow audit are how the first two are seen.
- **MAKE CANNOT SEE THROUGH `#include` OR `%include`, and a stale
  `APPLE2.O88` reads exactly like a change that did nothing.** `APPLE2SRC` and
  `APPLE2INC` are written prerequisites from wave 1, every file the plan names
  exists from wave 1 including the stubs, and `build/apple2-rom/APPLE2.ROM`
  has exactly ONE owner - the Makefile rule - because `build.sh` reads it and
  writing it from the stamp recipe would re-make a downstream prerequisite
  behind make's back.

---

## What shipped

*Filled in per wave, with the measured `os88pkg` line and the evidence. Empty
until wave 1 lands.*

### Wave 1

**The window, the chrome, the screen machinery, the 7-pixel text composer, the
licence and the harness.** Landed, `make` green, `make apple2disk` verifying
all four geometries.

**What is on the glass** (`build/port-shots/wave1-*.png`, QEMU over QMP): the
window at 338 x 237 with `Apple II Plus Emulator` in its title and `Apple II+`
on the kernel bar beside File, Edit, Machine and CPU; an 8-pixel border, the
16/24 letterbox and a status row reading the live video mode; and a text page
composed with the ROM's OWN character generator at SEVEN PIXELS a cell,
cropped and zoomed - a NORMAL run, an INVERSE run and a FLASHING run
photographed on **both** phases. The Machine pull-down shows all eleven of its
rows and the CPU pull-down all eight, with every row of a marked group on the
same column. The About panel opens from the kernel's name pull-down with the
four attributions and the ROM copyright line on it, and closes as DAMAGE.
**Fullscreen goes both ways** - in from the menu with the About panel up (it
comes down and its rows are redrawn), out with Ctrl+F and with Alt+Enter - and
`File > Quit` leaves a clean desktop with no dock tile. **The window was
covered by the Disk window for eighteen seconds of flash-timer wakes and not
one pixel of the Apple band or the About panel bled through it**, which is the
clip region on the glass; the same run on a 640x200 CGA desktop shows the
bottom anchor putting `NO 6502 IN THIS BUILD - WAVE 2` on the first visible
row and the greyed menu rows as the documented checkerboard.

**What the harnesses assert.** `a2uitest` drives the program against a pixel
model of the glass and asserts, after every step, that the glass shows what
the 7,680-byte shadow says it shows - and it asserts that the character
generator and the 7-bit reverse table are built in `os88_main`, that the
overlay is NOT resolved there, and that **every drawing call was made with
both the gfx lock held and a clip region armed**, the region modelled as the
kernel scopes it (armed by `clip_set`, dead at the next `gfx_unlock`).
`tools/a2ref.py` composes the same memory independently and the two agree BIT FOR BIT on both flash phases;
`--selftest` injects a one-bit defect and the compare fails; `--romshape`
asserts the pinned ROM's 128 distinct bitmaps. `a2memtest` runs the shipping
`a2mem.inc` and `a2band.inc` on a real x86 with SS != DS: 43 cases pass and
all FOUR negative controls are caught.

**THE COMPOSER IS MEASURED AND IT IS EXPENSIVE.** `make a2bandbench` under
`-icount shift=3`: a 40-cell row composes in **35.0 counts = 12.57 ms** of a
real 4.77 MHz XT and one eight-cell GROUP in 7.875 = 2.83 ms, which is
**2.434 ms a group, 304 us a CELL**, with a 0.393 ms call floor. That is eight
times what a naive model would have said and it is the third time this tree
has measured a row composer and found the model low. The whole-operation rows
are in `APPLE2-SPEC` section 7.9.1: one changed cell **11.9 ms**, one changed
row **22.0 ms**, ONE FLASH PHASE FLIP **65.6 ms**, a full 320 x 192 repaint
**505.4 ms** against this plan's PLANNED ~200. Recorded, not smoothed.

**Two defects were found on the glass and one by the harness**, and all three
are the kind this wave exists to find:

- **the About panel's close left two white strips** - band bytes 0-1 and 37-39,
  the 16 and 24 letterbox pixels - because a FORCED redraw was still drawing
  only the seven-byte groups the composer had been asked for. A group span is
  right for a source change and wrong for a glass somebody else painted on;
- **a scan line that compared EQUAL was left dirty**, so its row recomposed
  itself on every wake for the rest of the session. The only symptom was a
  cost row reading ten groups where the window had narrowed the work to five;
- **the wake never flushed at all** until `a2_wrote()` existed: the C's
  `a2_dirty_any` is set by `a2_dirty_scan`, which runs INSIDE the flush.

**And two corrections to the recipe**, both recorded in the SPEC: the bench is
driven under plain `-icount shift=3` and never `sleep=off` (under `sleep=off`
the guest stops answering input entirely - the screen saver comes up and
neither the mouse nor a key dismisses it), and `MENU_POPMAX` is eleven, so
Machine's sixteen rows are FOLDED on `c64menu.c`'s rule to exactly eleven.

**AND THE REVIEW ROUND FOUND SEVEN MORE, every one of them in the REDRAW path
and every one invisible in a screendump.** Each is a `hosttest/a2uitest.c` row
now, and each row was negative-controlled by reverting the fix in place:

- **the wake's flush armed no clip region.** `W_PAINT` is the one callback the
  kernel arms one for (SPEC.md 11.3) and the flush is a BACKGROUND painter, so
  its 24 blits, five fills and status row went over whatever window was
  covering us. `clip_set` also answers -1 when nothing shows, which now skips
  the whole ~500 ms - and a window in that state stops asking for wakes;
- **`a2_wants_wake`'s flash arm was a constant 1.** The Apple's cursor IS a
  flashing space, so `a2_flrow[]` is never all zero: the handler re-posted at
  ~1,400 round trips a second of the SHARED UI task to service a phase that
  flips 3.64 times. The phase is a `W_ONTIMER` now (SPEC.md 13.9), with the
  wake's poll kept as the tested `kern_small` path;
- **`a2_status`'s "nothing changed" return left the row dirty**, so the same
  message said twice spun the wake for its whole five-second life;
- **the About panel was repainted on its latch and not on the damage rect** -
  ~185 ms on every expose while it was up, which is MORE than the hold rows
  save;
- **the panel's rect was measured only AFTER the flush that reads it**, so a
  geometry change held the old lines and drew the new ones under it; and
  **Toggle Fullscreen was reachable with the panel up**, which is that defect
  at its worst because `OSAPI_FULLSCREEN` repaints NESTED;
- **a flash flip did not widen the compose span**, so a cursor blinking at one
  end of a row while the machine printed at the other stopped blinking;
- **the k-row scroll test was gated on all 192 lines being visible**, which a
  640x200 desktop never is: a scrolling Applesoft session took the span path
  for every line, ~210 ms A LINE. Dropping the gate was not enough on its own -
  the signature compare had to start at the first row the glass has ever
  shown, or it compares live memory against a permanent 0.

**And one defect the review did not name, found by DRIVING the emulator:
`Machine > Toggle Fullscreen` was a ONE-WAY DOOR.** The item is live from wave
1, `kernel/wm.inc` draws no chrome for a `WF_FULL` window, and `a2_key()`
drops every key - so a fullscreen APPLE2 ignored `f`, `F`, Esc, Ctrl+F and
Alt+Enter and there was no menu bar to use instead. SPEC.md 11.2.1 is binding
on it and `C64-SPEC §9.8` says the same thing one machine along. Section 6.3's
two chords moved from wave 3 into this one, resident, at the top of
`os88_onkey`; Ctrl+F is ASCII 6 on every BIOS and Alt+Enter is accepted on
BOTH its scan codes, the classic `0x1C` and the enhanced `0xA6`, because
neither is confirmed on iron until wave 7.

Two fidelity findings landed with them: **every row of a marked menu group now
owns the two-glyph column** (`Fast: 3.5MHz`, `Color NTSC` and `Mute` were two
cells out of line beside their partners), and **the scaffolding page no longer
draws the `]` prompt and its flashing cursor** - in all three references that
pair means "this machine will accept typing", and `a2_key()` drops every
keystroke this wave. It states `NO 6502 IN THIS BUILD - WAVE 2` instead.

**NO SIZE LINE IS QUOTED, and section 15 says why.** For the record the build
prints `image 19,300 + bss 10,312` (29,612 resident of 61,440), `APPLE2.OVL`
843 bytes, largest C frame 42 of 96 - which says the package builds, packs and
fits, and says nothing about a core that has no opcodes in it.

### Wave 2

**The machine boots to `]`, you can type at it, and `PRINT 2+2` says 4.**
`a2cpu.inc` is the derived copy whole - 256 opcodes with every illegal family,
decimal ADC and SBC, the interrupt machinery, the `cs:` cycle table with the
page-cross, taken-branch and branch-page-cross penalties, the signed-word
countdown and the stack wrap as two instructions - with the Apple II model in
place of the C64's seven per cent: a read fast path of two instructions and one
compare, a write fence at `$C000` that drops everything above `$C0FF`, four
rebias boundaries with the BLO/BOUND pair kept, one ROM bias, and a fetch in
`$C000-$CFFF` that takes the DATA ladder rather than a RAM bias - which is what
keeps the core's own scratch page unreachable with zero stated deviations.
`a2io.c` answers the whole of `$C000-$C0FF` in both directions on AppleWin's
nibble-decoded ranges, `a2kbd.c` is AppleWin's `asciicode` row 0 with its
rejections and its unconditional uppercase fold, and `a2_power_on` /
`a2_reset_cpu` are `CPU.cpp:769-812`'s reset line with I set, the jam cleared,
D untouched and SP pulled down three within page one.

**THE FIRST HONEST SIZE LINE (APPLE2-SPEC section 15.0):**

| resident image | bss | resident total | `APPLE2.OVL` | resident shims | largest C frame |
|---|---|---|---|---|---|
| **28,396** | **12,240** | **40,636** of 61,440 | **883** | **8** | **46** of 96 |

**20,804 spare; 14,364 under SPEC.md 73.9's 55,000 split trigger and 13,364
under the 54,000 end-of-wave-5 ceiling.** (The review pass that followed the
wave added 234 image bytes and 2 of bss - the scroll's phase guard, the shift
test's probe budget and the permanent JAM row - and gave 32 back by keeping
`a2_rowsig`, which nothing on a shipping path calls any more, out of the image
behind an `%ifdef`.) 1,920 of the bss is the SOURCE
SHADOW the k-row scroll test compares against (APPLE2-SPEC section 7.7 step 2
and 15.0): forty bytes a character row rather than a 16-bit signature, which
is what lets a verified shift mark the shifted rows CLEAN. A one-row scroll is
**41.9 ms instead of 406.2**, and **32.4 instead of 238.1** on the clipped CGA
band. `a2cpu.inc` alone assembles to 6,510
bytes, within 8 of the C64 core's measured 6,518, which is this plan's `-120`
term landing where it was estimated. **The contested budget of Decision 13 is
settled in this plan's favour** - the fit review's 60,500-63,500 reading does
not survive a measurement with the whole 6502 in it - so Disk II's deferral
now rests only on the two reasons that remain: the ~280KB of heap it wants for
two drive images, and the P5 ROM's authentic no-disk hang.

**`make a2cputest` passes all twelve rows with every negative control
failing.** Klaus Dormann's functional test settles at its success trap `$3469`
(the control, `ADC #` dispatched to `ORA #`, settles at `$056D`);
`tools/c64dec.py`'s 262,144 decimal cases match all four checksums; and the ten
Apple II rows cover reads and writes across every mapping boundary, the drop
above `$C0FF`, fetches across every boundary with an immediate straddling
`$FFFF/$0000` and a backward `JMP` out of ROM, ES and DS restoration, the
scratch page's inaccessibility in all three of read, write and EXECUTE, the
soft-switch stub's returns for reads as well as writes, the illegal opcodes,
table-driven cycle totals, the four unstable stores, and the interrupt stack
with RTI.

**On the glass** (`build/port-shots/wave2-*.png`, QEMU over QMP): the Apple
II+ banner and the `]` prompt at launch; the cursor photographed on BOTH flash
phases; `PRINT 2+2` showing `4`; Machine > Control-Reset abandoning a
half-typed line and printing a fresh prompt, with `PRINT 3*7` = `21` after it
to show the machine still runs; a `FOR I=1 TO 40: PRINT I: NEXT` scrolling the
page twenty times, on the 480-line desktop and on the clipped CGA band, with
the cursor photographed on both flash phases AFTER the scroll (`a2_flrow[]`
moves with the rows, or flashing text that has scrolled stops flashing); a
forced `JAM` showing `6502: JAM at $0300` and then, once the message expires,
a BLANK speed field - a number about a machine that is not running is what
SPEC.md 47 forbids - and Control-Reset recovering from it.

The status row reads **2,180 %** of a 1.02 MHz Apple under QEMU on the VGA
desktop and **2,775 %** on the CGA one, and it MOVES from second to second,
which is what a measurement does. **The first version of this field read
`383%` for every machine speed above about 380 %**: `a2_cyc_add` stopped
accumulating at 60,000 64-cycle units and the one-second denominator is 157,
so 383 was the saturation value and the `pct > 9999` clamp was unreachable
dead code. The unit doubles instead now (`64 << a2_csh`, capped at 4,096
cycles), and `hosttest/a2uitest.c` drives seven speeds from 3 % to 6,000 %
through the arithmetic and checks each, with a 12,000 % case that must clamp -
a field that reads the same for two very different machine speeds being the
whole of what is tested.

**Wave 1's `a2_selftext()` placeholder page is deleted**, as this plan said it
would be. `a2_have_cpu` revives Control-Reset and Open-Apple-Control-Reset -
the two rows whose bodies this wave wrote - and a second flag `a2_have_cmd`
carries the rows whose bodies wave 4 writes, `Power On` among them because
section 10.2 gives it a two-row confirmation that arrives with them.

**AND `CPU > Normal: 1MHz` IS MARKED FROM THE MEASUREMENT, WHICH IS MII'S OWN
RULE** (`mii_mui_menus.c:165-170`: the tick goes on only while the measured
speed is between 0.9 and 1.1, and comes off otherwise). It was ticked
unconditionally, which put `* Normal: 1MHz` in the pull-down directly above a
status row reading 383 % - a mode label asserting a speed the same screen
refuted. The two facts that went with it are restated: there is no speed
control in this build at all, so `Fast: 3.5MHz` is greyed on THAT and not on
"this machine runs at 1.02 MHz". `README.TXT`, which ships on all four
geometries, is rewritten for the same reason - it still described a build with
no processor, showing a placeholder page this wave deleted.

**`make a2cputest` is in CLAUDE.md's make-target map** beside `a2memtest` and
`a2bandbench`, on the C64 block's own precedent - a gate that takes minutes
and is deliberately outside `all` is the one a reader will not find by
grepping the Makefile.

### Wave 3

### Wave 4

**The four menus went live, a listing gets in and out through the clipboard,
and a program survives being saved and double-clicked.** Every item that has a
body has one: File > Load Program... / Save Program... through `os88_fdlg`,
Edit > Copy and Paste, Machine > Power On with AppleWin's two-row
confirmation, CPU > `Normal: 1MHz`, Stop/Continue and Warp; File > Quit,
Toggle Fullscreen and Flashing text stay in the RESIDENT half for the reasons
`a2menu.c` states beside each. `a2_have_cmd` is set, so `a2_menu_state`
rewrites every row it was holding and the two greyings that pointed at routes
this build did not have come back WHOLE - Machine > Configure Slots... reads
`No Disk II in this build. Load Program reads an Applesoft program, and Paste
types a listing in.` again.

**Every per-command body was written `ovl_` from the start** rather than moved
there when the ceiling arrived, which is LESSONS.md 5's own instruction and is
why the resident line moved 2,854 bytes for a wave that added seven commands, a
clipboard pair, a loader, a saver and a modal dialog while `APPLE2.OVL` more
than tripled. **The staging buffers are heap claims and not bss** - 1KB for
Copy, 2KB for Paste, 47KB for a load, each freed the moment it is spent -
which is the C64's 3,074-byte finding applied before the bytes were spent.

**THE SIZE LINE (APPLE2-SPEC section 15.0.2):**

| resident image | bss | resident total | `APPLE2.OVL` | resident shims | largest C frame |
|---|---|---|---|---|---|
| **34,220** | **13,852** | **48,072** of 61,440 | **4,266** | **37** | **54** of 96 |

**13,368 spare; 6,928 under SPEC.md 73.9's 55,000 split trigger and 5,928
under the 54,000 end-of-wave-5 ceiling**, so lever 1 is not pulled. The
package FILE is 49,664 bytes with the ROM part in it (49,152 before the CGA cursor-follow fix), against The Wire's
`WIRE_FILEMAX` of 64,512.

**Three things this wave learned that were not in the plan.**

1. **A `.BAS` double-click has to WAIT FOR `]`.** The first wake happens the
   moment the window is on the glass, and Applesoft's cold start ends in a
   `NEW`: a load spent there is wiped by the ROM and the glass shows a prompt
   whose `LIST` is empty with nothing saying why. The condition is the cold
   start's own output - `TXTTAB` = `$0801` and `VARTAB` >= `$0803`, measured
   on the glass at 2049 and 2051 - and not a wall clock, which would be wrong
   at both ends. APPLE2-SPEC section 12.1 is the contract.
2. **The association path has no SIZE**, where the Standard File dialog has
   one. The claim is `os88_mem_largest_kb()` capped at `A2_PRGMAX` and
   **stepped down until the heap takes it**, and the read's own answer is the
   size. The review found both halves of that wrong on the first cut: the cap
   was a flat 47 KB = 48,128, which is 1,025 bytes ABOVE the `$C000 - $0801`
   ceiling the dialog arm refuses on (the constant's comment said 47,615 when
   it is 47,103), so a 47,104-byte `.BAS` double-clicked passed the walk and
   wrote a byte at Apple `$C000` with `VARTAB` above `MEMSIZ`; and the claim
   was that size WHATEVER the file was, so this wave's own 45-byte done_when
   document asked a busy desktop for 46 KB pinned and was told `No heap for
   the program.` about one cluster.
3. **The reboot confirmation is the About panel with a KIND**, not a second
   modal panel: the hold range, the geometry, the damage close and the
   `os88_paint` arm are one piece of code with two texts, so a fix to one
   cannot leave the other broken.

**The `os88pkg` line, verbatim, and it now carries `assoc=1`:**

```
os88pkg: 'APPLE2' entry=+0x0070 image=34220 bss=13852 icon=yes assoc=1
```

which is 48,072 of 61,440 resident with `APPLE2.OVL` at 4,266 - **6,928 under
the 55,000 split trigger**, so lever 1 stays unpulled.

Two review passes moved the resident line **-60** against the wave's own first
cut, and both spent their share on the same rule: **a per-command body is
`ovl_` from the start.** The first pass moved `a2_wr16` and `a2_named` out;
the second moved `a2_clip_service` and `a2_copy_screen`, whose split
`a2kbd.c`'s own header asserted and the build did not have - about 170 x86
instructions of resident image for the claims, both clipboard calls and all
six refusals, none of which runs more than once per menu pick. The second pass
also added `ovl_a2_walk` (the chain walk lifted out so the length-prefix hint
can be second-guessed) and **+20 bytes of bss for the file command's latch**,
which is what takes the loader out from under the desktop's gfx lock.

**A whole-screen Edit > Copy is 31.1 ms and ONE bridge crossing** - 24
`a2_zcopy_out` + 24 `a2_copy_row` + one `os88_clip_put_seg`. The per-row read
is `tests/a2band`'s measured 0.314 ms; the 23.6 us per cell is DERIVED from
the 8088's instruction-fetch floor and is labelled as derived, because
`a2mem.inc` is not in that bench's `%include` list. The C64's first draft of
the same pair crossed the segment boundary 2,000 times.

**The second review's four majors**, and each has a gate that was proved to
BITE by reverting the fix under it: **os88_onfile ran the whole loader under
the DESKTOP'S GFX LOCK** - six claims, a 46 KB floppy read and a 48KB-capable
block move, with the pointer and dock frozen and every other painter blocked -
so it latches now and the wake spends it beside the launch document's arm;
**the host stub truncated a read the kernel refuses**, which made an
unreachable arm testable and the refusal it gates was the wrong sentence, so
the stub answers `FERR_BIG` the way `kernel/diskw.inc` does and the loader
reads `os88_ferr()`; **`a2_argp` sat above the running gate in
`a2_wants_wake`**, spinning the shared UI task for a minute on a machine that
could never reach `]`; and **CPU > Continue told a JAMMED machine it was
running**, printing `Running.` over the one row that says why it is dead.

**The evidence.** `make` green (34 fast-tier rows); `a2uitest` drives the whole
wave-4 script against the model - the clipboard round trip with all three
folds, a save and a load that round-trip byte for byte, a repaired chain from
another load address, a refused file that does not touch the machine, the
launch document's wait, the bounded give-up, the retitling and the
confirmation's Yes/No/Esc - and `a2memtest` gained a tenth row for
`a2_zzcopy_in`/`_out`, `a2_scan0` and `a2_copy_row` on a real x86 with
SS != DS. On the glass, a driven QMP session: a five-line lower-case listing
typed into Note Pad and copied, pasted into the Apple as upper case with one
RETURN a line, `LIST`, `RUN` printing 1 to 5, `Save Program...` to `WORK.BAS`
on a SCRATCH disk, Edit > Copy round-tripping the text page back into Note
Pad, the Power On confirmation answered No and then Yes, File > Quit, and then
a COLD reboot in which `WORK.BAS` wears the package's icon and a double-click
brings the same listing back to `LIST` and `RUN`. The negative control is a
scratch disk with `APPLE2.OVL` removed: a working Apple II at `]` whose menu
commands say `Unable to load APPLE2.OVL.` on the status row and toast it on
the bar - and toast it ONCE, naming `the menu commands that need it`, because
Quit, Toggle Fullscreen and Flashing text are answered in the resident half -
which is Decision 17's CHARGEN choice proved rather than asserted.
The 1bpp pass is on `VIDEO=cga` in `build-cga/`: the confirmation renders
inside a 200-line content box and `Configure Slots...` is legibly greyed.

### Wave 5

**The speaker, the four `OSAPI_FSX_*` thunks, and colour on a VGA.**

**THE SIZE LINE IS THE GATE AND IT PASSES**: `os88pkg: 'APPLE2' entry=+0x0070
image=38944 bss=14340 icon=yes assoc=1` - resident **53,284** of 61,440 against
the wave-5 ceiling of **54,000**, so **716 to spare and not one lever
pulled**, `APPLE2.OVL` **4,349**, **38** resident shims, largest C frame
**54** of the 96-byte cap. It is 1,716 under SPEC.md 73.9's 55,000 split
trigger, so Disk II's ~1,200 resident bytes still fit in the follow-up PR by
the arithmetic Decision 13 asked for - with 516 over, which is the number that
wave opens on rather than a comfortable one. **Two review passes are 978 of
it** (`APPLE2-SPEC` 15.0.4's last two rows): the first's six fixes at 808, one
of which REMOVES ~633 ms from every colour session rather than adding anything
to it, and the second's three at **170** - the key sentinel made `unsigned`
(the enhanced Alt+Enter, scan 0xA6, was dead code because a 16-bit `int`
makes it negative and the test was `k >= 0`), the frame GATED on `a2_wrote()`
the way `os88_onwake` has always been (17-29 ms a tick of an idle colour
session, 18.2 times a second), and the row early-out HOISTED above the
prologue as `a2_flush` has always had it (~5 ms a frame in text, ~10 in
hi-res). Two of those three take work OUT of every frame.

**AND THE SCROLL IS MEASURED AND STATED RATHER THAN FIXED.** `a2_fsx_frame`
has no counterpart to the windowed flush's shift block, so a RETURN at the
bottom line of an Applesoft session recomposes 184 scan lines: a new bench row
(`SCROLL FSXM_VGA13 (184 lines)`) reads **4,867 counts = 1,747.2 ms** against
the windowed path's **41.9**, **41.7x**. The fix is a second damage model -
`a2_shsrc`, a mode key, a phase, the probe budget, the `a2_lnf` refusal and a
mover for the framebuffer and the shadow - and the gate has 716 bytes in it,
so `APPLE2-SPEC` 13.2 records the number, the shape of the fix and the ~6x it
would buy, and 13.1 prices the whole mode on the `CPU_8086` tier in one table
rather than leaving the reader to infer it. `APPLE2-SPEC` section 15.0.4 has the
breakdown; the interesting half is that the foreign video mode - the wave's
headline - is the cheaper of the two features at ~2,346 bytes against the
speaker's ~1,034, and that the OVERLAY moved only +83, because a per-`$C030`
estimator and a bracket whose entry `tools/cc8086.py` refuses to let be an
`ovl_` are both resident by construction.

**The four thunks cost 92 bytes, not the 72 this plan booked** - 23 each - and
they are **charged to APPLE2 and to nothing else**, behind `%ifdef
CC_HAS_FSX`, which is `os88thunk.asm`'s own idiom (`CC_HAS_FDLG`,
`CC_HAS_PARTS`) and `apple2.asm`'s own (`A2_SHIP`). The first cut of the wave
charged all six other C packages for four thunks none of them will ever call,
and **it is LOOM and not CWORD that is the tight one**: LOOM builds with
**122** bytes of its 61,440 left, which ungated thunks cut to **30**, where the
SPEC had CWORD's 1,043 as the smallest margin in the tree. `APPLE2-SPEC`
section 17 has all seven rows, gated and ungated, and `make test-full` passes.

**THE MEASUREMENT CUT TWO OF THE THREE WRITERS, WHICH IS Decision 14 SPENDING
ITSELF EXACTLY AS IT WAS WRITTEN TO.** `a2bandbench` gained seven primitive
rows and six whole-path rows and `FSXM_CGA640` / `FSXM_HERC` lost on BOTH
change sets: **695.4 ms a frame against the windowed path's 632.6**, and
**34.8 ms a character row against 26.3**. They are cut, Machine > `Color NTSC`
greys on CGA and Hercules with that number, and colour is a claim about a
VGA-class machine. **The reason is not the raster this plan argued about**:
both paths compose and double with the identical routines, and what differs is
the emit - ONE `os88_gfx_blit1` of 640x16 at 8.875 counts against sixteen
`a2_fsx_put` compares at 2.0. A band going down whole needs no span compare.
**And the cut gave back nothing**, because section 15.4's lever 3 was taken at
the design: there is one `a2_fsx_put` and the three writers differed only in
the caller's offset, so the ~1,200 bytes were booked against a shape that was
never built.

**`FSXM_VGA13` is 3,376 ms a frame on a 4.77 MHz 8088** - an order of magnitude
over this plan's ~300 - which is why every foreign frame is driven off the same
dirty-line set the windowed flush computes, against a 53KB shadow claimed at
the fullscreen LATCH where a refusal is legal. The harness asserts the drive
rather than the pixels: the first frame writes 192 lines and three frames with
nothing changing write 192 in total, where a per-frame raster write would be
576. It also asserts the rule nothing in the toolchain can - **not one drawing
slot between the mode set and the return** - and, after the review, the two
things that rule was only DESCRIBED by: a 6502 driven into a JAM inside the
bracket must raise no toast, and a Ctrl-Reset typed inside it must be spent
inside it.

**AND THE FRAME IS DRIVEN BY COLUMN AS WELL AS BY LINE.** The first cut read
none of the `a2_wlo`/`a2_whi`/`a2_rowwide` state `a2_dirty_scan` had just
filled for it and composed all forty cells of every dirty row: **140.7 ms a
keystroke** against the windowed path's 26.3. `a2_fsx_frame` takes
`a2_flush`'s own group span now - padded by ONE CELL each side in hi-res,
because artifact colour is decided by a pixel's neighbours and the 1bpp band
has none - and `a2_fsx_row` takes a cell range: **31.2 ms**, measured as its
own bench row, and checked before it is timed by `ab_spanck`, which requires
eight cells composed as a RANGE to equal the same eight composed as part of
the whole row. **On the glass, a narrow-write frame and a full recompose of
the same memory are pixel-identical: 0 differing bytes of 768,000.**

**AND ONE DEFECT THE REVIEW DID NOT NAME AND THE GLASS DID.** Entering Machine
> `Color NTSC` a SECOND time on an unchanged hi-res screen drew three of its
six lines and a truncated pair of verticals. The 53KB shadow is freed at the
exit and re-claimed at the next latch, and a free followed by a same-size claim
lands on the same block - so it arrived holding the last session's frame, and
`a2_fsx_put`'s per-byte compare answered "nothing moved" against a framebuffer
SPEC.md 53.4 had just cleared. `a2_fsx_ok = 0` defeats the per-LINE skip and
says nothing to the compare one level down; the fix is to make the shadow TRUE
rather than unknown, which the mode set's own clear allows. `a2_fsx_zero`
zeroes it at entry, the harness's `h_claim_keep` row is the gate - its negative
control draws **0** lit pixels where the first session drew 27,344 - and on the
glass a second session is now pixel-identical to the first, 0 of 768,000.

**On the glass** (`build/port-shots/wave5-*.png`): a hi-res `HCOLOR` ladder in
artifact colour inside `FSXM_VGA13` - green, purple, white, orange, blue,
exactly Applesoft's own `HCOLOR` order through MII's ten-entry CLUT - a lo-res
`GR` ladder in all sixteen of MII's "Color NTSC" colours, the Machine menu with
`Color NTSC` and `Mute` live on a VGA and `Color NTSC` greyed on `VIDEO=cga`,
`Square-wave tones only.` on the status row, and a clean exit on Ctrl+F with
the desktop and the window repainted whole. **`make test-snd` + `sndcheck`**
read the Applesoft beep at **934 Hz** and a two-line `POKE -16336,0 / GOTO 10`
loop at **62 Hz**, which is Applesoft's own speed (8,230 emulated cycles a
toggle) and not a defect; the capture is 5.5 s of a 40-second session, which is
the silence timeout.

**TWO THINGS WENT WRONG AND BOTH ARE WRITTEN DOWN WHERE THEY HAPPENED.** The
bench's four composite bodies HUNG the first time they ran, because cdecl here
preserves BP, DS, SS:SP and DF and nothing else and `_a2_band_text` loads BL on
its second instruction - a loop counter in BX never came back, and under
`-icount` a bench that never finishes is indistinguishable from one that is
merely slow. And a `make test-snd` capture was read as five seconds of DC and a
whole paragraph of `apple2.c` was written about a defect that did not exist:
the scan started at 30 Hz in steps of 5 and answered `60`, and 62 Hz at
44.1 kHz is 355 identical samples a half-period. What settled it was putting the
estimator's own answer on the status row for one build and reading `62%` off
the glass. The frequency band the wrong diagnosis produced is KEPT, on its own
merits - a measured frequency wobbles, and every step of that walk is a far
call plus an `out 0x43` that restarts the PIT mid-note - but at a
sixty-fourth rather than an eighth, and `APPLE2-SPEC` section 8 says which
half of that story is the machine's and which was the instrument's.

### Wave 6 - the follow-up PR

### Wave 7

**The polish, the documents, the disks, the two 86Box machines and The Wire -
and the wave's one number is the one nobody had.**

**THE SIZE LINE**: `os88pkg: 'APPLE2' entry=+0x0070 image=39042 bss=14344
icon=yes assoc=1` - resident **53,386** of 61,440, **8,054 spare**,
`APPLE2.OVL` **4,349** unchanged, **38** resident shims, largest C frame **54**
of the 96-byte cap, the FILE on disk **54,272** (the ROM part starts at a
sector, so 448 bytes of slack absorb an image change whole). `APPLE2-SPEC`
15.0.5 has the arithmetic: **+78** for the one-time colour fact, **+80** for
the foreign frame's tier pacing that the review found missing, **-58** given
back to every C package in the tree and **-2** for a shorter status message.
The **54,000 resident gate has 614 bytes in it** and the line is **1,614**
under the 55,000 split trigger, so the deferred Disk II wave's ~1,200 leave
**414**.

**THE MEASURED XT SPEED, WHICH IS THE WAVE'S HEADLINE AND IS NOT WHAT THIS
PLAN PREDICTED.** On MartyPC's `os8088_5150_cga` - docs/FIELD-MACHINES.md's
calibration machine, an 8088 at 4.77 MHz with a CGA and two 360K drives -
booting `build/os8088-360.img` with `build/apple2360.img` in B:, the machine
runs the Apple at **0.41% of 1.02 MHz idle at the `]` prompt - the same mean
in all THREE runs - and 0.51% inside a RUNNING
`FOR I = 1 TO 1000 : NEXT`** (0.52 / 0.51 / 0.51 over three runs of a dozen
one-second windows each). **The loop rows are a review re-take**: the first
driver typed the line in one burst, the BIOS's 15-key buffer dropped
everything after `FOR I = 1 TO 10` including RETURN, and both the rows and
`wave7-xt-loop.png` were the machine at the prompt with a half-typed command
on it - which is why they had read the same as idle. The status row reads **`0%`**, honestly: the field is
whole per cent and 0.4 truncates. **The risk section above predicted 3-4% and
was out by eight**, and the reason is written down rather than explained away:
it divided 8088 clocks by 6502 instructions and left out the SLICE, which is
`A2_SLICE_MIN` = 256 cycles once a wake, 18.2 wakes a second, 4,659 emulated
cycles a second, 0.46%. Lifting that ceiling is a scheduler decision with a
UI-latency price and was not taken here. `APPLE2-SPEC` 16.4.1 is the record,
with its date and machine.

**AND IT WAS TAKEN IN A WAVE OF ITS OWN, AFTERWARDS: 0.54% IDLE AND 0.64%
UNDER BASIC** (`APPLE2-SPEC` 4.3.1 and 16.4.1, both re-measured on the same
machine with the before rows kept). What pinned the budget was not the tick
being near - a 256-cycle slice measures 22.5 ms of a 55 ms tick - but the
rule's own asymmetry: FOUR consecutive clean slices to double against ONE
crossing to halve has a fixed point at **14 %** of a tick whatever the
machine, so the budget could not leave its floor. The adaptation is a
**duty-cycle controller** now - the crossing RATE over a window of eight
slices IS the share of a tick a slice is taking - and it settles at 512 cycles
idle and 648 in a `FOR/NEXT`, the slice being ~40 ms of a 55 ms tick in both.
**+98 bytes resident.** The menu still comes down: pressed while the loop
runs, the pull-down is on the glass in a median of **2 host ticks** either
way, with the worst case one tick longer.

**AND THE INSTRUMENT FOUND THE REAL CEILING, WHICH IS NOT THE SLICE.** Built
once with tick counters around the slice, the flush and the whole wake, the
XT spent **39 %** of its second in the 6502 and **50 %** inside `a2_flush` at
110.9 ms a flush. The core's own throughput on this machine is ~11,500
emulated cycles a second - **1.13 %** of an Apple - so everything between
0.54 % and that is redraw, and the plan's "3-4 %" was out by six on the
INTERPRETER before the slice is reached at all: ~415 8088 clocks an emulated
6502 CYCLE, about 1,250 an instruction, against the plan's 400. The flush's
pacing is section 7.8's tier rule and was deliberately left alone.

**AND THE FIGURE IS WHAT THE TIER, THE README AND THE WIRE PAGE NOW SAY.**
Tier **3** (`486+`), the C64's tier for the C64's reason; `README.TXT` says an
XT is a machine to look at and a 386 is where the port is usable; the Wire
page says the same in the words a reader meets first.

**`WELCOME.BAS`, AND IT IS TOKENISED FROM THE PINNED ROM'S OWN TABLE.**
`tools/a2bas.py` reads Applesoft's TOKEN.NAME.TABLE at `$D0D0` out of
`APPLE2.ROM` itself - 107 names from `END` to `MID$`, asserted - and
implements the ROM's PARSE at `$D559`; `--selfcheck` LISTs the tokenised
program back through the same table and requires it to agree with the source.
42 lines, **884 bytes**. The proof that outranks the gate is the machine's
own `LIST`: `build/port-shots/wave7-05-welcome-list.png` is Applesoft printing
the program back, and `wave7-06-welcome-run.png`, `wave7-07-welcome-gr.png`
and `wave7-08-welcome-hgr.png` are it RUNNING - the text page with INVERSE,
the lo-res bars with MIXED's four text rows, and the hi-res fan. It claims
nothing this build does not draw: no `FLASH`, which the `CPU_8086` tier
refuses; where it names colour it names Machine > Color NTSC and a VGA; and
**the geometries it prints are MIXED's** - `40 X 40` and `280 X 160`, not the
full-screen `40 X 48` and `280 X 192` the first draft printed under a mixed
screen (`APPLE2-SPEC` 16.2 has the correction and why it counts as a false
claim). `wave7-13-colour-gr.png` and `wave7-14-colour-hgr.png` are the colour
sentence being true - the same program's bars and fan in `FSXM_VGA13`.

**IT ARRIVES BY DOUBLE-CLICK.** `wave7-04-welcome-launch.png` is a cold
machine opened straight from `WELCOME.BAS` in the Disk window, which is the
`CC_ASSOC` block of wave 4 doing what it was declared for.

**COLOUR'S PRICE IS ON THE GLASS ONCE**, on the `CPU_8086` tier and on the way
OUT of the first session, because there is no status row under the bracket to
read one on: `Colour: 1.7 s a scroll.`, 23 cells of 26, from
`APPLE2-SPEC` 7.9.4's own measurement of a SCROLL. The row stays LIVE on every
tier - that decision did not move. **The RECURRING cost and not the entry
one**: the first draft said the 3.4 seconds of a first frame, which is paid
once and is behind the reader by the time the row exists again. **And it
names the SCROLL and not `per RETURN`**, the review's second correction to
the same 26 cells: 1,747.2 ms is the whole TEXT page, where a RETURN in the
MIXED modes `GR` and `HGR` select scrolls four rows for ~304 ms.

**THE SDK'S THREE OVERLAY REFUSALS WERE ALL BEING CUT OFF THE GLASS**, in
every C package in the tree, and nothing was checking (`APPLE2-SPEC` 17.3).
`TOAST_MAX` is 24 and truncates rather than refusing, so
`APPLE2.OVL is not on this disk` reached a reader as
`APPLE2.OVL is not on thi`. They are `No <NAME>.OVL`, `No RAM: <NAME>` and
`Old <NAME>.OVL` now, **sized from the SDK's own 15-character name field** and
not from the longest name this tree happens to hold, with a gate in
`tests/unit/t_mirror.py` - a FAST-tier row, so every `make` runs it - that
reads the cap out of `kernel/toast.inc`, the literals out of `crt0.asm`, the
name cap out of `crt0.asm`'s `%fatal` and the names out of every shim in the
tree. It gives **-58 image** back to every C package with an overlay.
`build/port-shots/wave7-33-noovl-refusal.png` is the whole sentence on the
glass at last, on a disk with `APPLE2.OVL` deleted, beside a machine still
running at `]`.

**THE FOUR DISKS ARE FIVE FILES IN ONE FOLDER** and `make allapps` - and
therefore `make live` - carries `APPLE2\` as a folder of its own: 93 of a
360KB disk's 354 clusters, each geometry `--verify`'d in its recipe. The
1.44MB everything disk is **2,720 of 2,847** clusters and the 1.2MB one
**2,244 of 2,371**.

**`vm/xt-apple2` AND `vm/286-apple2`**, each a copy of the corresponding
`vm/*-c64` with `fdd_02_fn` and the uuid changed and nothing else, with
`make xt-apple2` / `make 286-apple2` beside `386-apple2`.

**AND THE REVIEW OF THIS WAVE FOUND ONE THING THAT IS NOT POLISH**, which is
recorded here rather than folded into the paragraphs above. `a2_fsx_main` -
the fullscreen colour bracket - had the "did anything change" gate the
windowed flush has and **not the tier pacing**, so on the `CPU_8086` tier this
wave had just measured, and had just printed a message about, it issued a
140 ms frame between every 256-cycle slice: `os88_fsx_wait` returned at once
because the frame had already overrun the tick, and the emulated machine ran
at roughly a third of the speed the WINDOW manages on the same box.
`APPLE2-SPEC` 13.2 has the arithmetic and the fix - `a2_fsx_tick` beside
`a2_fsx_ok`, and four ticks where the window takes two, which is the ratio a
colour row costs against a windowed one - and it cost **80 bytes of image and
2 of bss**. The review also caught three documentation defects worth naming:
`wave7-xt-loop.png` showed a HALF-TYPED command at the `]` prompt rather than
a running loop (and so did the loop measurement behind it - see the corrected
table in `APPLE2-SPEC` 16.4.1); `a2_pct` was named in two places as the
instrument the XT figure came off when the figure came off `a2_c64u`; and the
size argument in 13.2 that keeps the biggest redraw defect in this package
UNFIXED was quoting a headroom figure two waves stale.

**AND `WELCOME.BAS`'s OWN COST IS MEASURED NOW** (`APPLE2-SPEC` 16.2.1). Its
header priced the loops it did NOT ship and put no number on the ones it did.
On the 5150: the text screen is **6.0 s** from `RUN`, the lo-res screen
**91.2 s** and the hi-res screen **106.2 s**, off the BIOS tick counter, with
the package launched by DOUBLE-CLICK on the document. The loops keep their
size - halving a minute and a half is still a minute - and the header and
`README.TXT` carry the figure instead, which is the port's posture everywhere
else.

**`tests/apple2part.py`** is `c64part.py`'s shape in the soak tier: the ROM is
not a file on the disk, the part is one ASSET of 14,848, `os88_part_seg(0)` is
what the C put in `a2_m.romseg`, three windows of the ROM in the guest equal
`build/apple2-rom/APPLE2.ROM`, the RESET vector at `$FFFC` reads `$FA62`, and
**both display tables exist after `os88_main` and before any wake** - the
negative control for keeping the chargen decode and the reverse table off the
overlay. It passes on `os8088_5150_herc_gla_144`.

**AND ONE DEFECT IS RECORDED RATHER THAN TIDIED** (`APPLE2-SPEC` 9): on the
MartyPC XT the mode field reads `TEX` while the row's own shadow holds `TEXT`,
read out of the guest's CGA VRAM byte for byte. It does not reproduce under
QEMU's `VIDEO=cga`, nothing in this wave touched `a2_status`, and the shape
that fits is a first full-row draw cut short with the shadow recording it as
drawn. It is open, it is one field on one machine class, and the SPEC names
the first thing to try.
