# Kernel hot-paths pass 1: vga

The VGA/EGA planar renderer: `kernel/vga12.inc` and its `.vga` rung, less
what gfxcall owns (the slot shims, the gfx lock, the clip and coordinate
set-up). The brief is `docs/plans/khp1/BRIEF.md`. Every timing figure below
is in GUEST time on MartyPC's cycle-exact 4.77 MHz 8088 (`os8088_xt_vga`,
an IBM 5160 with an 8-bit ISA VGA) unless it says otherwise. MartyPC's VGA
writes cost about 0.9 cycles more than RAM, where a real 8-bit VGA card adds
more (docs/FIELD-MACHINES.md). So every VGA figure here is a floor, and a
change that removes instructions rather than bus cycles is what it rewards.

Branch `khp1-vga`. Base `a7ee7fab` (os88hot's `6d6c23ae` is the same kernel).

## 1. BASE AND TIP

Assembled by `make`, `make small`, `make emu` and `make mega`, read off
`kernsize`. Two bases matter:

* **`a7ee7fab`**, the pass's base. Pre-merge tip `f483f74c` (this branch
  alone, plus the coordinator's +2 `.cold` mod_cfix fix).
* **`8f85f95c`**, kernel-hotpaths with pointer, gfxcall, sched, text and files
  merged. Tip `7390cd6d` (that, plus this branch).

| kernel | section | a7ee7fab | f483f74c | 8f85f95c | **7390cd6d** | this branch's own |
|---|---|---:|---:|---:|---:|---:|
| kern_big | `.vga` | 2,032 | 2,190 | 2,113 | **2,243** | **+130** |
| kern_big | `.text` | 40,631 | 40,631 | 41,129 | 41,129 | 0 |
| kern_small | everything | resident 57,258 | 57,258 | - | - | **0** (no VGA in the build) |
| kern_emu | `.vga` | 2,032 | 2,190 | - | 2,243 | +130 |
| kern_mega | `.text` (no VGA rung: the bodies are in `.text`) | 45,853 | 46,013 | - | 46,569 | +160 at `f483f74c` (the same bodies in `.text`; nasm relaxes a jump or two differently there), and `7390cd6d`'s -28 applies there too |

`KERN_SIZE`: kern_big 96,768 at the base. This branch alone made it 97,280,
one rung; the merged tip is 97,792 with everybody's bytes. kern_small 59,392
is untouched by this branch.

**Growth before `.cold` (BRIEF 8, the COLD_HOME limit): +130 bytes**, all of
it in the VGA rung. That is under the ~300-byte line the coordinator asked
each branch to flag.

## 2. WHERE THE TIME GOES

### 2.1 The VGA interactive time, by routine (base, from the shared baseline)

docs/reports/HOTPATH-BASE-2026-11-06.md section 2 is the sampled profile of
every interactive scenario together. On VGA, the busy time spent in this
file's routines at the base was:

| # | routine | ms | share | what it is |
|---:|---|---:|---:|---|
| 1 | `vga_restore_vram_v` | 348.1 | 16.7% | save-under return, menu dismissal, the pointer's restore |
| 2 | `vga_save_vram_v` | 318.0 | 15.2% | save-under take, menu drop, the pointer's save |
| 4 | `vga_solid_rect_v` | 165.8 | 7.9% | every solid fill, the XOR fills, every frame and line |
| 18 | `gfx_fill_gray_raw_v` | 18.6 | 0.9% | the desktop dither, scrollbar troughs |
| 19 | `gfx_scroll_v` | 17.7 | 0.8% | the planar scroll (write mode 1, already the floor) |
| 21 | `vga_gc_reset` | 14.4 | 0.7% | the GC teardown after each fill |

The save-under pair is 70% of a VGA raise (120 + 118 ms of 341) and the
reason VGA is the slowest adapter on `raise`: 366 ms against CGA's 180.

### 2.2 Exact cycles inside each routine, per operation (base)

`tools/os88hot.py` samples. To get exact totals I wrote a second instrument
(scratch, not committed: `rtime.py`). It puts MartyPC logging breakpoints on
each routine's entry and on every exit found by disassembling the guest's
own bytes, pairs them with a stack, and runs a scripted scene on
`os8088_xt_vga`: a Disk window on A:, Note Pad on README.TXT, and Paint on
MEDIA/OS8088.GIF. Time is INCLUSIVE of callees and of any interrupt that
lands. Base:

| operation | save | restore | solid fills | the pointer (cur_saveu + cur_restoreu + cur_draw) |
|---|---:|---:|---:|---:|
| raise Note Pad | 47.0 ms / 7 | 56.6 / 6 | 44.1 / 53 calls | 18.8 |
| raise Paint | 132.9 / 9 | 52.6 / 10 | 50.6 / 60 | 33.1 |
| raise Disk | 59.6 / 5 | 6.9 / 4 | 89.0 / 92 | 16.5 |
| drag Note Pad | 67.6 / 11 | 81.0 / 13 | 137.6 / 91 | 42.5 |
| menu drop | 39.4 / 1 (188,240 cycles) | 1.8 | 14.1 / 6 | - |
| menu dismiss | 1.4 | 41.8 / 1 (199,357 cycles) | 1.4 | - |
| 10 pointer moves | 15.1 | 20.2 | - | 47.5 (19,000 cycles a move) |

What this ranked:

1. **save and restore**: the biggest by a factor of two. The time is per BYTE,
   in `rep movsb` (17 clocks a byte) four planes deep, plus per-row
   arithmetic and a `cs:`-overridden memory add on every restore row.
2. **solid fills**: about 4,000 cycles a call, mean. Of that, ~770 is
   `gfx_rect_setup` (gfxcall's) and ~160 is `vga_gc_reset`. The rest is two
   masked edge columns, at ~60 cycles a row each, and the interior's
   `rep stosb`.
3. **the pointer**: 19,000 cycles (4 ms) a mouse packet on VGA. Save and
   restore are 13,500 of that; `cur_draw` (pointer's) is the rest.
4. **gfx_blit4's flat rows** (Paint's blank canvas, images, games): ~2,900
   cycles a row for one run (gfxbench `GFX_BLIT4 solid`, 64x64, 39.4 ms).
5. **gfx_blitp's fast path**: per byte, `rep movsb` four planes deep.

## 3. TAKEN

All are MEASURED on MartyPC `os8088_xt_vga` unless marked. "gfxbench"
means `GFX*` rows read off `GFXVGA.TXT`, base against the tree before the
item. Scenario figures are `os88hot.py ab --base 6d6c23ae time <scen>
--machine vga -n 3`, medians, for all the items together.

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| vga-1 | vga | `vga_save_vram_v`, `vga_restore_vram_v` row loops | H1 | words (`rep movsw`, 25 clk/word vs 17/byte). Two loops (even words; odd words + `movsb`) chosen once a plane. Row steps in registers. The restore's sub-rect step read into BP before DS moves. (`7e469c23` also had a one-word loop for the pointer's cell; `7390cd6d` took it out once pointer's own walks landed) | **+41** (+69 then -28) | 0 | menu drop save 188,240 -> 138,856. Menu dismiss restore 199,357 -> 145,101. (Before pointer's change, the pointer cell too: save 6,000 -> 5,100, restore 8,020 -> 6,250; pointer's own walks now replace that path) | 12-op scene: saves 536 -> 406 ms (-24%), restores 433 -> 316 (-27%). Raise Paint over Note Pad: save 132.9 -> 99.4 ms, restore 52.6 -> 36.3. Menu drop 39.4 -> 29.1, dismiss 41.8 -> 30.4 | M | words are one bus cycle on a 16-bit card; a straddling word is two (= the byte loop's traffic) | `7e469c23` + `7390cd6d` (revert the second first) | [+41] OWNER'S PICK |
| vga-2 | vga | `vga_solid_rect` / `gfx_fill_gray` interiors | H1 | `rep stosw` interior, odd byte by loop choice once a call | +39 | 0 | gfxbench `GFX_FILL 256x128` 13.37 -> 10.67 ms. `GFX_FILL 64x64` 3.56 -> 3.32. `GFX_FILL_GRAY 64x64` 4.08 -> 3.89. `GFX_FILL 8x8` unchanged | every window background, the desktop, troughs: part of raise 368.6 -> 292.4 ms and drag 610.4 -> 537.0 | M | as vga-1 | `71a90157` | [+39] OWNER'S PICK |
| vga-3 | vga | `vga_solid_rect` edge columns | H1 | two rows a pass, odd count enters mid-loop, step in CX | +21 | 0 | gfxbench `GFX_VLINE 128px` 2.08 -> 1.64 ms (-21%). `GFX_FILL 64x64` 3.32 -> 2.88 (-13%). `GFX_FRAME 64x64` -11%. **`GFX_HLINE 8px` 488.7 -> 498.2 us (+1.9%)**, `GFX_FILL 256x1` +2.6% | mean fill in the scene's raises -4% to -16% a call | M | neutral: fewer instruction bytes | `9e5511d1` | [+21] OWNER'S PICK |
| vga-4 | vga | `vga_solid_rect` XOR interior | H1 | `rep movsb` over itself (SI = DI, DS = ES = A000): the read is the latch load | +3 | 0 | gfxbench `GFX_XOR_FILL 64x64` 6.75 -> 4.35 ms (-36%). `GFX_XOR_RECT 256x1` 1.09 -> 0.88 (-19%). `256x128` 5.93 -> 5.52 | menu highlight and selection bars; every drag-outline step (four strips, erase and draw) | M | neutral | `8b1de03b` | [+3] OWNER'S PICK |
| vga-5 | vga | `gfx_blitp_v` fast path (`.frow`) | H1 | rows of 8 bytes or wider as words, loop picked once a plane | +32 | 0 | gfxbench `GFX_BLITP 256x16` 9.48 -> 7.97 ms (-16%). `GFX_BLITP 64x64` (8-byte rows) 12.38 -> 12.43 (break-even) | Paint's planar canvas (56-byte rows): a whole 448x280 canvas ~-53 ms, PREDICTED from the per-word saving. Games' bands (1942, Excitebike, DrMarco) | M (P for Paint) | words win more on a 16-bit card | `da0b1070` | [+32] OWNER'S PICK |
| vga-6 | vga | `vga_blit_span` | H2 (per run) | stop banking AX, CX, DI, which its one caller reloads | -6 | 0 | gfxbench `GFX_BLIT4 solid` 39.38 -> 38.18 ms (-3.0%), ~80 cycles a run | every coalesced blit4 run on VGA | M | neutral | `47639976` | FREE |
| vga-7 | vga | `gfx_blit4` `.row` prologue | H2 (per row) | the 1bpp decoder's pair-table choice moved after the fst test, so planar rows skip it; parity via `shr dx,1` | 0 | 0 | gfxbench `GFX_BLIT4 solid` 38.18 -> 36.97 ms (-3.2%), ~95 cycles a row | every direct-path blit4 row on VGA; 1bpp unchanged | M | neutral | `f483f74c` | FREE |
| vga-0 | vga | `tools/os88sym.py` | tool | `.vga` resolves at KERNEL_SEG (it raised for every rung label) | 0 | 0 | - | - | - | - | `bbf8d217` | FREE (host tool; merged to kernel-hotpaths already) |

**The scenarios, all items together** (`os88hot ab`, medians of 3,
MEASURED). First against the pass base, then re-priced against
kernel-hotpaths `8f85f95c` with pointer's cursor walks merged, which is the
figure the owner gets on merging this branch:

| scenario (os88hot), VGA | vs kernel-hotpaths `8f85f95c`: ms | busy |
|---|---|---|
| raise | 357.4 -> 286.4 (**-19.9%**) | 346.4 -> 275.4 (-20.5%) |
| drag | 659.9 -> 587.6 (**-11.0%**) | 645.2 -> 576.2 (-10.7%) |
| menu open File | 45.5 -> 40.6 (-10.8%) | 34.5 -> 30.9 |
| menu close File | 31.2 -> 26.2 (**-15.9%**) | 31.2 -> 26.2 |
| pointer sweep | 633.0 -> 631.6 | 154.7 -> 154.1 (-0.4%: the pointer no longer uses these bodies) |

Against the pass base `6d6c23ae`, this branch alone (`f483f74c`):

| scenario (os88hot) | VGA ms base -> tip | VGA busy base -> tip |
|---|---|---|
| raise (Disk over Disk) | 368.6 -> 292.4 (**-20.7%**) | 358.5 -> 284.7 (-20.6%) |
| drag (8 packets) | 610.4 -> 537.0 (**-12.0%**) | 600.3 -> 529.3 (-11.8%) |
| menu open File | 49.6 -> 43.7 (-11.9%) | 39.1 -> 35.2 |
| menu close File | 30.4 -> 26.9 (-11.6%) | 30.4 -> 26.9 |
| pointer sweep (24 packets) | 632.2 -> 635.6 (serial-bound) | 245.2 -> 232.5 (**-5.2%**, ~0.5 ms a packet) |
| scroll PgDn / PgUp | 104.1 / 96.8 -> 100.1 / 92.9 (-3.8% / -4.0%) | same |
| **86Box 286 VGA raise** (RELATIVE, `--emu 86box --cpu 286`) | 120.0 -> 105.0 (-12.5%) | 84.0 -> 74.0 (-11.9%) |
| CGA raise (no-regression check) | 179.1 -> 181.2 (+1.2%, serial flight) | 170.5 -> 170.9 (+0.2%) |
| Hercules drag (no-regression check) | 538.1 -> 538.2 | 526.2 -> 527.4 (+0.2%) |

The 1bpp adapters run none of this code except vga-7's reordered row
prologue, where they do the same instructions as before. Their figures move
only as much as an A/B of an unchanged path moves (ms within 1.2%, busy
+0.2%).

**Value per byte, for the owner's pick.** The raise and drag savings split by
routine in the exact-cycles scene: about three quarters come from vga-1
(save/restore) and a quarter from vga-2 and vga-3 (fills).
- vga-1: ~55 ms of the ~71 ms off a VGA raise, and ~5 ms of a menu's open
  and close, for 41 B. (Before pointer's walks it also took ~0.55 ms off
  every mouse packet. Pointer's [+81] now owns that path.)
- vga-2 + vga-3: ~15 ms a raise and ~40 ms a drag, for 60 B together.
- vga-4: small per operation (0.2-2 ms), but on every menu highlight and drag step, for 3 B.
- vga-5: pays where a package blits wide planar rows (Paint, games) and
  nowhere in the kernel's own scenarios, for 32 B.

## 4. TRIED AND REFUSED

* **Word copies in `vga_prow_emit`** (the masked-row emitter the planar
  decoder and `gfx_blitp`'s general rows share). Built with an 8-byte
  threshold per row: +22 bytes. No gfxbench row reaches a wide masked row
  apart from the `w`-flagged (tick-timed) `GFX_BLIT4 4px runs`, so the gain
  was unmeasured. It was taken out again rather than shipped on a guess.
* **Words for `gfx_blitp` below 8 bytes a row.** At 8 bytes it is exactly
  break-even (12.38 -> 12.43 ms). Below that it loses, which is the earlier
  Gorillas measurement (71.7 vs 68.3 ms) and the reason for the threshold.
* **Inlining `vga_gc_reset` into `vga_solid_rect`'s teardown.** Priced, not
  built: ~95 cycles a fill (the call, the ret and two push/pop pairs the
  caller has already banked) for +12 bytes. That is ~1.2 ms a VGA raise, far
  below vga-3's ~5 ms for 21 B. Not taken. A cheaper shape may exist: a
  bankless entry beside the shared one.
* **Words in the planar scroll.** Refused by the hardware, not by the
  numbers. Write mode 1 copies through the latches, and a word read loads
  them twice, so the first byte of every word would be written with the
  second byte's planes. The `rep movsb` there stays. Its per-row DS
  reload and memory counter are ~70 cycles a row, about 10% of a 256-wide
  scroll. Left for a later pass: the scroll is 0.8% of VGA time.

## 5. WANTS AN ABI BREAK

None found. Every change here sits behind an unchanged contract:
`gfx_save`/`gfx_restore`'s buffer layout, `gfx_blitp`'s and `gfx_blit4`'s
register contracts, and the fills'. vga-6 narrows an INTERNAL routine's
preserved set (`vga_blit_span`, one caller), which is not an ABI.

## 6. WANTS THE OWNER

* **A VRAM-resident save-under on VGA, copied through the latches.** The
  coordinator named it the classic lever, and it is the biggest one left in
  this file. Priced, not built.
  - **The mechanism.** Mode 12h uses 38,400 of each plane's 65,536 bytes,
    which leaves 27,136 bytes a plane offscreen (A000:9600). A write-mode-1
    `rep movsb` moves 8 pixels of all four planes per byte.
  - **The measured price** is `gfx_scroll`'s own: `GFX_SCROLL 256x128` copies
    4,096 such bytes in 19.5 ms, 22.7 cycles a byte all-in. The same rect
    through `gfx_save` + `gfx_restore` is 16,384 plane-bytes each way: about
    45 ms each with vga-1's words, about 60 ms each at the base.
  - **PREDICTED gain**: a VGA raise's save + restore (~180 ms at the tip)
    to ~70 ms, so a raise of ~292 -> ~180 ms, and a menu drop and dismissal
    to about a third.
  - **What it costs is design, not bytes.** The save-under is wm's (wm.inc
    `wm_su_take`/`wm_su_try`). It addresses into its buffer (`wm_su_edge`
    patches bytes in place, `gfx_sub_arm` restores sub-rects), keeps one per
    covered window, and holds them across operations. Offscreen VRAM
    is a shared, finite resource: a full-screen bracket (§53) or Mode X
    would overwrite it, an EGA with 128 KB has only 4,768 bytes a plane
    spare, and the extended desktop and packed displays need their own
    answer. It frees heap, too: a save-under claim is several KB of
    conventional memory today. It needs a VRAM allocator owned by one
    module, invalidation on every mode change, and wm's buffer arithmetic
    taught a second layout.
  - **The smaller cousin** is the POINTER's 8x12 cell in offscreen VRAM.
    (Priced against the BASE cursor, before pointer's [+81] gave it its own
    walks: pointer should re-price it.)
    `cur_saveu`/`cur_restoreu` and, since that change, their walks are
    pointer's. At the base it was
    save + restore 11,350 -> ~1,500 cycles a mouse packet, PREDICTED from the
    same 22.7 cycles a byte: ~2 ms a packet, ~8% of the CPU while the mouse
    moves at 40 packets a second. It needs one fixed 48-byte offscreen slot
    and the same invalidation on a mode change.
* No change in this branch alters a settled picture or a result. That is
  checked in section 8.

## 7. CROSS-FILE

* **`gfx_rect_setup` (softgfx.inc; gfxcall's set-up)** is ~770 cycles of
  every VGA fill, save and restore. That is 19% of a mean solid fill (4,000
  cycles) and 10-13% of the pointer's save and restore. Twice a mouse
  packet, the cursor pays the generic clip for a rect `cur_rect` has already
  bounded. A cursor-only entry that takes the clip as given would save
  ~1,500 cycles (0.3 ms) a packet. Route: gfxcall and pointer.
* **`cur_draw` (mouse.inc, pointer's, in the VGA rung)** is ~3,900 cycles a
  packet on VGA. Not read in depth here.
* **tests/deskbench.py on VGA** could not build its scene at the base: Paint's
  title bar was buried. wm fixed it on kernel-hotpaths (`80030976`); my
  profile used its own scene instead.

## 8. ROWS RUN, and the equivalence checks

**Equivalence, by plane** (scratch `planes.py` / `planecmp.py`). The same
scripted scene runs on the base and the tip, and after each of eleven steps
all four VGA planes (153,600 bytes) are read back with the debug server's
`planes` verb and compared. The steps are Note Pad open, Paint open, three
raises, a drag out and back, a menu down and dismissed, ten pointer moves,
and a last raise.
- VGA (`os8088_xt_vga`): identical except 48 plane-bytes in three captures,
  at rows 267-273, x 264-287. Those are the DIGITS of a number in the Disk
  window's status line, which differs because the tip kernel is one 512-byte
  rung larger, so less memory is free.
- EGA (the `VIDEO=ega` tree, mode 10h at 640x350, the same machine):
  identical except the same digits (28 plane-bytes).
- Every other capture, menus, drags and the pointer included, is
  byte-identical.

**Rows** (`python3 tools/os88test.py soak -k ... --marty-jobs 1`, once,
after the last code commit): see 8.1.

**Gates on every commit:** `make -j2` (the fast tier, hotpar included, green
every time), `make small`; at the tip `make emu` and `make mega` (both
green). `tools/stkbalance.py kernel/kernel.asm kernel/vga12.inc` reports
the same set at base and tip: no new unbalanced path. checkdocs after
`git add`.

### 8.1 Rows

Run once, after the last code commit before the kernel-hotpaths merge (`f483f74c`):
`python3 tools/os88test.py soak -k ... --marty-jobs 1`, **20 passed, 0 failed,
0 skipped** in 1,107 s. The rows and what each covers:

| row | covers |
|---|---|
| `xorrect`, `wdunsel`, `colorpresets` | vga-4 (the XOR interior: outlines, deselect XORs, a themed menu bar XORed and back) |
| `wdmenusu`, `atmenusu`, `vgadrop`, `vgadirty`, `savervga` | vga-1 (menu save-unders, the drop path, the dirty-rect save-under, the saver's bank) |
| `vidgrey` | vga-2's gray fill (the dither's parity, pixel for pixel) |
| `dispdrag`, `colorpresets` | vga-2, vga-3 (solid fills and frames through a drag) |
| `blitp`, `dispblitp`, `paintplan`, `paintdraw`, `gorillasmenu` | vga-5 (the planar copy; Gorillas' 2-byte rows and their 75 ms budget) |
| `blitplane`, `b4walk` | vga-6, vga-7 (blit4's run writer against the planar decoder; a region walk) |
| `isotronega`, `excitebikeega` | EGA: the same bodies at 640x350 |
| `tmhover` | the Task Manager's inverted row (XOR) |

After the merge with kernel-hotpaths (pointer's own cursor walks), the
fast tier ran again on `7390cd6d`: 73 passed, 0 failed, 1 skipped. The soak
rows were not re-run, because the merge moved no code of mine. The only
change after them is the one-word loop's removal, and `wdmenusu`,
`atmenusu`, `vgadrop` and `xorrect` are the rows to re-run on the integration
branch for it.

## 9. DEFECTS FOUND

1. **`mod_cfix`'s opcode test matched `8A` (and AA/BA/CA/DA/FA) as well as
   `9A`/`EA`** (kernel/mod.inc, disk's). Found when vga-1 alone moved the
   HMA build's COLD_HOME to 08B3h. CTRL.DRV holds
   `8A 0E 3D B3 08` (`mov cl,[0xB33D]` / `or cl,cl`), which passed the
   `(op | 70h) == FAh` predicate, so `build/coldhma.sys`'s proof failed and
   so did `make`. On a machine where `.cold` moved, the kernel would have
   rewritten that operand. It was routed to disk and fixed on
   kernel-hotpaths (`6209b524`). My intermediate commits were ordered to stay
   off the colliding homes until the fix landed. No workaround is left in
   the tree.
2. **`tools/os88sym.py` could not resolve any `.vga` label** (no
   SECTION_SEG row). Fixed in `bbf8d217`.
3. **`tests/deskbench.py --machine os8088_xt_vga` failed to build its scene**
   at the base (Paint placed under two windows). Reported. wm fixed it.
4. **`tools/stkbalance.py` treats a label named inside a STRING as a
   reference.** My `%error` text for the vga_pn/vga_pm adjacency named
   `gfx_sr_tail`. The checker then walked that continuation as an entry from
   depth 0 and failed the fast tier with "ret at depth -4". I reworded the
   text. Not fixed in the tool, whose owner is the suite: a `%error`'s string
   could be skipped there.
5. **The MartyPC debug server clears its logging-breakpoint record whenever
   the breakpoint set is REPLACED, including with an empty set**
   (tools/martypc/debug_server.rs `breakpoints`). Read `bplog` before
   disarming. Not a bug in the server, but nothing in os88marty's docs says
   it, and it cost one run here.
