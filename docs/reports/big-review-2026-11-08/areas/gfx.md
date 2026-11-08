# Drawing primitives: planar VGA and the software renderer

Area key: `gfx`. Reviewer pass of 2026-11-08 on branch `the-big-review`
(tip `5e4740b`). Read-only: nothing was built, booted or measured. Every cycle
figure below is PREDICTED from PERFORMANCE.md Part 2's table unless it quotes a
measured number from the tree.

## Scope read

| file | lines | read |
|---|---:|---|
| `kernel/vga12.inc` | 7,202 | all of it, every proc and every `%ifdef` arm |
| `kernel/softgfx.inc` | 1,486 | all of it |
| `kernel/kernel.asm` | 225-300, 1340-1400, 2790-2810, 7890 | the KHP knob block, the GFX_VGA/GFX_PLANE/COLFAST/BANKWALK gates, the BLIT4 slot, `cw_gfx_clip_query` |
| `kernel/viddet.inc` | 421, 440-475, 1589-1625 | `gfx_inktab`, `VID_ROWTAB`, `gfx_rowbase`/`_calc` |
| `kernel/vidext.inc` | 71-105, 333-349 | `VXMONO`/`VIDEXTM`/`VIDEXTF` both arms |
| `kernel/dockmod.inc`, `dock.inc` | 1500-1620, 330-370 | `gfx_hole_arm` and its contract |
| `kernel/mega.inc`, `extmod.inc`, `vidsel.inc`, `mouse.inc` | spot reads | lock writers, selector limits, VX/VY placement, the cursor's restore path |
| `SPEC.md` | §5 opening + table (4141-4237), 5.4 (4238-4330), 5.4.1.3 (4560-4590), 5.4.3.2/6 (5836-5850, 6008-6020), 5.7 (7172-7260), 5.10.2/4 (7998-8095), 5.13 (8558-8600), §32 (70516-71010), 39.31-39.34 (81383-81490), §4 table rows 4096-4097 | |
| `PERFORMANCE.md` | Part 5 table (1209-1300) | |
| `apps/os88api.inc` | BLIT4, BLITP, SPANS, POINTS contracts | |
| `docs/plans/ABI-BREAK-LIST.md` §9-10, `KERNEL-BYTE-QUEUE.md`, `LAST-DROP-PERF.md` §7 | | to avoid re-proposing |

## Verdict

The drawing core is in better shape than its comments: the inner loops are
written against the 8088's real prices (fetch floor, `xchg` latch idiom, one
read-modify-write store, table masks, `rep movsw` with the `adc cx,cx` tail),
clipping and display translation are solved once at the public entry and
re-enter a single raw body, and almost every unusual case refuses with CF = 1
and a documented fallback instead of approximating. The real defects found are
at the edges of trust and of arms: **`gfx_blit4` trusts the sign of its width,
and a negative one sails through both decoders' "on screen" proof** - on VGA
the planar decoder then writes kilobytes past `vga_pbuf` into whatever sits
above `.vgabuf` (gfx-1, a zero-byte fix that `gfx_blitp` already learned from
PaccMan); the planar decoder's deliberate over-read is safe in real mode and
not on kern_mega's tight selectors (gfx-2). The systemic weaknesses are three:
the whole-shape primitives each hand-order "hide, enter display, dispatch" and
the tree's comments record five field bugs that were that order got wrong
(gfx-13); a shelf of built-and-off knob bodies doubles the reading surface of
the hottest code without shipping its measured wins to the calibration
machine (gfx-6); and the comments narrate history so densely that at least
two dozen of them now describe code that no longer exists (gfx-12 and the
mismatch list). The cheap wins are a `CLIPQF` far call per `gfx_blit1` band
that can simply be deleted (gfx-3), a planar arm for `gfx_points` (gfx-4), and
a path-dependent `[gfx_color]` side effect that should be pinned (gfx-5).

## Strengths

1. **One store per masked byte: `dest ^= (pat ^ dest) & mask`.** `sw_col`
   (softgfx.inc:788-792), `sw_spans` (341-350), `sw_patcol` (1313-1316 on the
   knob arm), `gfx_blit1_x`'s tail (vga12.inc:6331-6333). Three bus accesses
   instead of four, and the bits outside the mask are never written with a
   stale value even if an ISR touches the byte mid-sequence. This killed a
   real one-frame flash (`tests/deskflash.py`). Copy it everywhere a 1bpp RMW
   remains.
2. **Clipping and display translation live ABOVE the renderer dispatch.**
   `GFXCLIP` -> `GFXDISP` -> raw body (vga12.inc:491-589), with
   `gfx_clip_run` (954-1003) and `gfx_disp_run` (647-689) re-entering the
   same raw body per fragment / per display. One hook serves VGA, both 1bpp
   adapters and the packed driver, and the ordering argument (virtual clip
   before translation, 544-550) is written down where it binds.
3. **The VGA idioms are the right ones.** Set/Reset solid fills with a word
   `out` of the pre-packed `8,mask` pair (`vga_m8l`/`gfx_lmsk`, 259-293);
   `xchg [es:di],al` as latch-load-plus-masked-write in one instruction;
   the XOR interior as `rep movsb` over itself to reload latches per byte
   (3483-3495); write mode 1 for the scroll (5276-5350). These are what the
   Windows 3.x VGA driver did, at a fraction of the code.
4. **Derived tables are built from the single source of truth.** The 1bpp
   pair tables (`sw_pairbuild`, softgfx.inc:879-922) and the planar transpose
   (`vga_p4build`, vga12.inc:2489-2526) are computed at run time from
   `gfx_inktab` and arithmetic, unioned over the same 512 bytes of `.lowbss`,
   each builder clearing the other's flag in one word store. The reduction
   is written once.
5. **Refusal is a contract, and probes make it cheap to ask.** `gfx_blitp`'s
   DI bit 15 probe (2927-2940), `gfx_spans` and `gfx_blit1`'s CF = 1 with a
   named fallback. The caller always has a correct slower path one branch
   away, which is §47's rule applied to primitives.
6. **The run-density hybrid in `gfx_blit4` prices each row for free.** The
   first run's length is the probe and the run count is the budget
   (2172-2249); flat rows keep the ~16x cheaper span writer, dithered rows go
   to the transpose. Measured, not assumed (Set 107).
7. **Layout invariants are assembler errors, not prose.** Every word load of
   a byte pair has an `%error` beside the declaration (85-87, 256-258,
   290-292, 316-318, 1039-1041, 5679-5681, 5791-5793, 7197-7199,
   softgfx.inc:1482-1484). This is the single best habit in the file.
8. **`gfx_unlock` is the end of a drawing transaction and resets everything
   a forgetful caller could leak** - region, pen, mono1, disabled pen,
   display context, progress widget, busy shape (5149-5269). The "safe value
   is the one a forgetful caller inherits" rule is EndPaint's, done better
   because nothing has to be released explicitly.
9. **The `.vga` rung slide** gives GEOS's "only the adapter's driver is
   resident" with near calls: a 1bpp machine overwrites the planar bodies
   with `.cold` at boot (2304-2312), and the doors are registered in
   `tests/vgarefs.txt`.

## Findings

Ranked most severe first. Phase: **now** = no ABI break, **flagday** = needs
one, **record** = write it down, not recommended now.

### gfx-1 - `gfx_blit4` trusts the sign of its width; a negative one overruns `vga_pbuf` (bug, high, now, 0 bytes)

`gfx_blit4` refuses an empty block with `or cx,cx / jz vga_b4out` and the same
for DX (vga12.inc:1886-1889) - it never tests the sign. Both decoders then
"prove the block is on screen in x" with an unsigned add that wraps:

- 1bpp: `add ax, cx / cmp ax, [vid_cw] / ja .slow` (2011-2013);
- planar: `mov dx, ax / add dx, cx / cmp dx, [vid_cw] / ja .pdone` (2050-2053),
  `cmp cx, 16 / jb` passes any CX >= 16 including 0x8000-0xFFFF (2054).

So x = 20, w = -10 (a package's `x2 - x1 + 1` on an empty rect) gives
x + w = 10: "on screen". On VGA the first non-flat row goes `.eplanar`
(2191-2194, 2237-2249) into `vga_blit_prow` with CX ~ 0xFFF6; the byte span
there is computed from the wrapped last-pixel x (2551-2570), giving
`[vga_pr_n]` ~ 8,190 groups, and the `.g` loop (2608-2661) stores that many
bytes at `[di]`, `[di+84]`, `[di+168]`, `[di+252]` from `vga_pbuf` - a
**336-byte** buffer (`VGA_PB*4`, 7176) at the top of the kernel image, under
the heap floor on a VGA machine (7166-7175). That is ~8 KB of kernel/heap
corruption from one bad package argument. On 1bpp `sw_blit_row` writes ~8 KB
of framebuffer (garbage, and on a dual-card Hercules into the CGA's
aperture). DX negative is a 65,535-row loop.

`gfx_blitp` already fixed exactly this (2856-2859: `jle`, citing PaccMan's
`rep movsb` across the framebuffer). **Fix: `jz vga_b4out` -> `jle vga_b4out`
at both sites** (same encoding, backward short). `gfx_blit1_x`'s `jcxz
.refuse` (5902) has the same shape with only framebuffer garbage as the
consequence; `or cx,cx / jle .refuse` there is +2 bytes in `.cold`.

### gfx-2 - the planar decoder's over-read is a #GP on kern_mega (hazard, medium, now)

`vga_blit_prow` starts the decode `p >> 1` bytes EARLY (2574-2576) and reads
whole 4-byte groups (2612-2632), so it touches up to 3 bytes before the
row's first source byte and up to 3 after its last. The banner and SPEC.md
4573-4574 call this safe because "the bytes read before the row are inside
the caller's segment". In real mode that is true (offsets wrap). On kern_mega
- which defines KERN_BIG and so GFX_PLANE - a claim's selector limit is the
claim's own end (mega.inc:5968-5970; SPEC.md 99294-99296), and the over-read
of a canvas's last row, or of a first row at offset < 3 with an odd-phase x,
is outside the limit: `#GP` inside the decoder. Not run; PLAUSIBLE from the
code. **Recommend**: under `%ifdef KERN_MEGA`, one `lsl` of ES where the
planar path is armed (2046-2068) and leave `[gfx_blit_pok]` 0 when
SI - 3 underflows or the last row's end + 3 passes the limit (~+20 bytes,
kern_mega `.text` only); and in any case state the slack in
`OSAPI_GFX_BLIT4`'s contract.

### gfx-3 - `gfx_blit1` far-calls the clip question twice per band (perf, medium, now, -3 bytes)

`CLIPQF` on kern_big is `call KERNEL_SEG:cw_gfx_clip_query` (458), a far call
to a shim that near-calls `gfx_clip_query` (kernel.asm:7890) - ~170 clocks
round trip. `gfx_blit1_x` asks it at 6019 and again at 6067, and nothing
between can change the answer except the hole arming the first call itself
does. **Carry the first answer in ZF** across the nine pushes (pushes write
no flag): the unarmed path already arrives at `.clipok` with ZF = 1; give
the two armed fall-throughs (after `cmp byte [wm_clip_cm],0FFh` at 6050-6051,
and kern_small's after `jc .percol` at 6053) an `or sp, sp` (ZF = 0, 2 bytes)
and delete the second `CLIPQF` (5 bytes). Net -3 bytes `.cold`, ~-170 clocks
(~36 us) per band on kern_big; every text line composed through `band.inc`,
Word/CWord, the caption bands. Optionally inline the first one too (`.cold`
has DS = KERNEL_SEG): +~9 bytes for another ~-110 clocks.

### gfx-4 - `gfx_points` has no planar path: every VGA point is a whole `gfx_fill` (perf, medium, now)

The gate (1350-1365) sends `[vid_mono] = 0` straight to `.slow`, one
`gfx_pixel` per point (1401-1414) - the full rect machinery, SPEC.md 5.13's
640.87 us per `GFX_PIXEL` against 165.96 a point on the 1bpp loop. The
callers are exactly the ones that draw many points: Cyclone, Missile, Tank,
`os88gfx.inc`'s `GFXE_POINTS` sink, the C SDK's `os88_gfx_points`. On a VGA
XT they get none of the 3.9x. A fourth `GFXPT_LOOP` body for planar needs
nothing the loop does not already have: Set/Reset = `[gfx_color]` armed once
(GC0/GC1, as `vga_blit_span` does), per point `mov ah,[cs:si+gfx_bitset] /
mov al,8 / out dx,ax / xchg [es:di],al`, `vga_gc_reset` at `.done`; ES =
VGA_SEG, the row table and clip box as now. In the `.vga` rung behind the
existing depth test, so it costs only VGA/EGA machines. ~+60-70 bytes `.vga`;
PREDICTED ~5-10x per point on VGA. (Points straddling displays keep `.slow`.)

### gfx-5 - `[gfx_color]` after `gfx_blit4` depends on the path taken (consistency, medium, now)

The run paths overwrite it with the last run's colour (`vga_blit_span`
2343-2344, `gfx_blit_run` 3245-3250, `gfx_b4piece` 6938); the two decoders
(`sw_blit_row`, `vga_blit_prow`) never touch it. The header (1742-1743) and
SPEC.md 5.4 promise "left holding the last colour drawn"; `os88api.inc`'s
BLIT4 contract (1958-1970) is silent. So a package that does SET_COLOR,
BLIT4, FILL gets a fill colour that depends on the adapter, x alignment, run
density and whether a region was armed. No GDI or QuickDraw blit mutates the
pen. **Recommend** preserving it (bank AL in the prologue, restore before
`kret_bp`: ~+8 bytes `.text`, ~30 clocks a blit), or at minimum declaring it
UNSPECIFIED in SPEC 5.4 and the SDK (0 bytes).

### gfx-6 - the built-and-off knob arms (weakness, medium, now)

`KHP_VGASAVE`, `_VGAFILL`, `_VGAEDGE`, `_VGABLITP`, `_FRAME` are kern_mega's
only; `KHP_MONORECT`, `_MONOPAT`, `_MONOSCROLL` are defined by no kernel
(kernel.asm:243-297). That is two bodies at ~15 sites in these two files and
~240 lines of the 1bpp bank walk (softgfx.inc:442-680) that no shipped
kern_big executes, while its measured -51% `GFX_FILL 64x64` on CGA and
Hercules for +344 bytes is the largest win for the machine the project is
calibrated against. Listed as LAST-DROP-PERF.md 7.1 rows 3, 6, 8, 9, 11, 12,
14, 16. **New arguments**: (a) rows 3, 6, 8 and 9 (+214) are `.vga` bytes,
which a 1bpp machine slides away, so they are billed only to VGA/EGA
machines; (b) the shelf is already producing false documentation - the
softgfx banner says the bank walk is kern_big's (445), SPEC.md 5.7 lists
items 9 and 10 as taken off the floor - and t_khpknobs proves the arms
assemble, not that they draw. **Recommend** a dated decision per row: take
it or delete the arm; a shelf with no review date only accrues.

### gfx-7 - the planar bodies hard-code an 80-byte stride (hazard, low, now)

Every VGA row step is `ROW_BYTES` (3398, 3414, 3439, 3450, 3817, 3833, 3845,
4019, 4052, 4407, 4435, 4748, 4782) while `gfx_blitp`'s plane-major path
reads `[vid_stride]` (2994) and `gfx_blit1_x` pushes `[vid_rowadd]` (6203).
Nothing enforces "every 4-plane surface is 80 bytes a row" except the
file's line 8-9, and that line is already stale (see mismatches). A future
planar mode (800x600x16 is a one-`int 10h` mode on most SVGA cards) would
draw every fill sheared. **Recommend** a boot check in `vid_apply` (`.ovl`):
4 planes implies stride = ROW_BYTES, else refuse the mode (+~8 bytes `.ovl`,
0 resident); and say so in SPEC §5's opening.

### gfx-8 - a right-edge clip forfeits both decoders (perf, low, now)

`x + w > [vid_cw]` sends the whole block to the per-run path (2011-2013,
2050-2053), ~0.5 ms a run. Clamping `[gfx_blit_w]` to `vid_cw - x` once
(when x >= 0) keeps both decoders and the run scanner, because the source
stride is BP and is untouched. Callers clip today ("Every caller clips its own
blit", 2013; softgfx.inc:962-966) so the gain is mostly that they could stop:
~+10 bytes `.text`, one clip in the kernel instead of one per package. An
EVEN left clip is a source advance of `-x/2` bytes (+~12); an odd one is
`gfx_b4piece`'s column already written (6913-6965).

### gfx-9 - the 1bpp XOR interior is a byte loop (perf, low, now)

The shipped column-major body complements the interior one byte at a time
(`not byte [es:di] / inc di / loop`, softgfx.inc:722-727) - every Disk-window
selection band and the menu highlight on CGA and Hercules. The word loop
already written for the knob arm (`sw_k_xrow`, 577-598) is ~40% cheaper per
byte. +~8 bytes `.text`; PREDICTED ~1 ms per file-row band.

### gfx-10 - `gfx_lock` is a cli/sti section (consistency, low, now)

5084-5127 uses `cli` ... `sti` against CLAUDE.md's "pushf/cli ... popf,
never cli/sti" rule. It is correct - task level only, and the contract says
"IF = 1 on return" (5079) - but the rule's exception should be written where
the rule is (§1 / CLAUDE.md hard rules), or the next reviewer "fixes" it.
0 bytes.

### gfx-11 - `vga12.inc` is not the module §4 describes (consistency, low, now)

It carries the theme (`thm_tab` and the live block 101-195, `thm_set` and
`api_thm_set_x` in `.cold` 6969-7133), all the pens, the lock, both display
hooks, `gfx_blit1`, `gfx_points`, `gfx_spans` and the shared rect helpers;
SPEC.md 4096 names the lock, save/restore, scroll and `vga_rect_setup` (which
is `gfx_rect_setup` and lives in softgfx.inc). **Recommend** moving the theme
to `kernel/theme.inc` (0 bytes, mind `t_hotpar`) and correcting §4.

### gfx-12 - comments as archaeology (weakness, medium, now)

~7,200 lines for roughly 2,000 of instructions, and much of the prose is "it
was X until size pass N". This review found 25+ statements that describe code
that is gone (`sw_rect_pl`, `vid_rpara`/`vid_rend`, `gfx_line`,
`gfx_pt_resolve`, `gfx_ls_addr`, the back buffer, `bb_*`, `vga_rect_setup`,
`.lowbss` plane rows, `gfx_blit1` "kern_big only"). The current contract is
buried under its own history. **Recommend** (0 bytes): history moves to SPEC
and `docs/plans/completed/`; a code comment states the current contract and
the invariant; and a cheap gate - comment identifiers that are not symbols in
the build's map and not on an allow-list - catches the next rename.

### gfx-13 - "asked the depth before entering the display" is a CLASS, fixed five times (rethink, medium, now)

The tree's own comments record it at `gfx_xor_rect` (4090-4105: the reboot),
the menu save-under (39.14.8), `gfx_blit1`'s pen (5.4.2.4, 6726-6734),
`gfx_blitp`'s depth test (2874-2898, "LATENT") and `gfx_points` (1376-1387).
Each whole-shape primitive hand-orders `cur_unlazy` -> display enter -> depth
dispatch, and each got the order wrong once. **Rethink**: one entry routine
for the whole-shape class (`xor_rect`, save/restore, scroll, blitp, blit4's
hook, points) that does hide, enter and only then dispatch, plus a GFXAUDIT
assertion that the depth byte is read only with `[gfx_dnest]` > 0 or one
display. ~0 bytes (code motion). Prior art: Windows GDI cannot call a DDI
entry before it has the DC's PDEVICE - the dispatch target IS the device.

### gfx-14 - a per-display renderer table (rethink, low, record)

Today each primitive does `cmp [vid_planes],4 / jb / je`, a `COLD_SLIDE` door
`jmp X_v`, and on kern_small a `NOVGA_ARM`. A table of ~8 near pointers in
`VID_CTX`, set by `vid_apply` and swapped by `vid_ctx_act`, makes every entry
`jmp [vid_fn_x]` (cycle-neutral against the compares), folds VID_EXT's
packed dispatch in, and makes the `.vga` slide's safety structural: a 1bpp
display's table cannot name `.vga`. ~+16 bytes `.bss` a display, ~-40
`.text`. Prior art: the Win 3.x DDI and GEOS's video-driver strategy routine.
Recorded rather than recommended: the current form is measured and works.

### gfx-15 - `gfx_clip_run` tests every fragment (rethink, low, now, cross-area: wm)

954-1003 intersects the primitive with all `[wm_clip_n]` rects (up to 16,
~150 clocks each: ~0.5 ms a primitive under a full region) and has no early
exit. If the producer kept the list y-sorted (Windows' and X11's y-x banded
regions), the walk could stop at the first fragment whose y1 > the
primitive's y2. ~+6 bytes here, the sort is wm.inc's. `gfx_ls_box` and the
`.frags` walk would gain the same exit.

### gfx-16 - V1 cannot be the cell alone (flagday, low)

Listed as ABI-BREAK-LIST V1. **New evidence** for whoever does it: the
kernel's internal callers depend on the preserve-everything contract being
the BODY's - `gfx_hline` returns BX from the CX `gfx_fill` kept (1070-1079),
`gfx_xor_strips` carries the fill in BP across four calls and relies on every
callee keeping it (4150-4158), `gfx_blit_run` and `gfx_pixel` the same. So V1
is a new package-facing entry beside the raw body, not a change to
`gfx_fill`; price it with that cell.

### gfx-17 - parity pads serve the non-target buses (record, low)

12 `PARITY` pads in these files, ~61 sites kernel-wide, keep hot entries at a
pinned address parity for 16-bit-bus fetch (t_hotpar's own header). The 8088
fetches a byte at a time and is indifferent to it. Each pad is a resident
byte spent for 8086/V30/286 machines. Worth stating in the gate's header so
the trade is visible; not a recommendation to remove.

## Rethinks

The three worth the owner's time, in order: **gfx-13** (one entry shape for
whole-shape primitives - the bug class the comments keep recording), **gfx-4**
(planar points - a 5-10x on the colour XT for the games that already use the
right API), and **gfx-6** (end the shelf: ship or delete each knob). gfx-14
and gfx-15 are model changes recorded for the next time either area is
opened anyway.

## Prior art

- **DOS / BIOS int 10h**: pixel write through the BIOS is hundreds of clocks
  per pixel; every serious DOS program bypassed it, which is what this whole
  layer is. Nothing to adopt.
- **Windows 3.0/3.1 GDI**: device dispatch through the DDI table of the DC's
  device, with brute (software) fallbacks; regions are y-x banded rect lists;
  BitBlt is one generalised ROP engine whose VGA driver compiled the inner
  loop onto the stack. os8088 deliberately keeps separate solid/gray/pattern/
  XOR bodies (the merge was measured at +33%, LAST-DROP-BYTES 7.6) - right
  for an 8088. Worth adopting: banded regions (gfx-15), "device before
  dispatch" as structure (gfx-13), a pen that a blit never mutates (gfx-5).
- **Mac System 5-7 QuickDraw**: per-port local coordinates, clipRgn ∩ visRgn
  intersected once per port change; the scanline region format. os8088's
  shelved content-coordinates design B (ABI-BREAK-LIST R6) is the GrafPort
  model; nothing new to add here.
- **GEOS (8088)**: kernel graphics with one loadable video driver resident,
  called through a strategy routine with function numbers. os8088's `.vga`
  slide achieves the same residency with near calls, which is better on an
  8088; GEOS's single dispatch point is what gfx-14 borrows.
- **OS/2 1.x PM GPI**: presentation spaces and correlation; far heavier
  than this target can carry. Nothing to adopt.
- **AmigaOS Layers**: primitives iterate a layer's ClipRect list, precomputed
  on damage - exactly `gfx_clip_run`'s per-fragment re-entry; Layers keeps
  the list per layer across draws where os8088 rebuilds it per `W_PAINT`,
  which is a wm question.

What os8088 does better than all of them on this hardware: CF = 1 refusal
with a probe, instead of a silent slow path; the interrupt-safe one-store
RMW; and assembler-enforced layout of every packed pair.

## Doc/code mismatches

Each: file:line, the exact current text, what the code does, proposed text.

1. **vga12.inc:9** - "only the two bottom-edge clips here read the live [vid_ch]." Code: the live `[vid_cw]`/`[vid_ch]` are read by `gfx_rect_setup` (softgfx.inc:101-116) and at vga12.inc:2012, 2052, 2077, 2353, 2925, 2982, 3068, 3590, 3598, 3717, 5395-5397, 6114-6146. Proposed: "The width and stride are 640/80 on both, and the planar row steps are ROW_BYTES; every clip reads the live [vid_cw]/[vid_ch]."
2. **vga12.inc:20** - "Every routine clips to 0..639 / 0..479" -> "Every routine clips to the live screen, 0..[vid_cwm1] / 0..[vid_chm1],".
3. **vga12.inc:301-304** - "Four writers in the tree and no fifth: :5303 (1), :5384 (0), sched.inc" Code: writers are vga12.inc:5124 (gfx_lock, flag+owner word), 5262 (gfx_unlock, 0xFF00), sched.inc:2098 (0) and 2114 (the banked value), and on kern_mega mega.inc:2491, 7569, 7582. Proposed: "Writers: gfx_lock and gfx_unlock (each one word store with the owner), sched.inc's KFZ strip (forced 0, then the banked value back), and on kern_mega mega.inc's fault paths - grep `gfx_lock_flag]` before adding one."
4. **vga12.inc:1746-1748** - "It coalesces runs of equal pixels and emits one gfx_fill per run" (and 1774-1793's cost and "worthwhile future optimisation" paragraphs). Code: three paths - the run writer (gfx_fill or `vga_blit_span`), the 1bpp pair decoder (`sw_blit_row`, 2089-2100) and the planar transpose (`vga_blit_prow`, 2237-2249) - chosen per call and per row. Proposed: replace with a three-line summary pointing at SPEC 5.4, 5.4.1.1 and 5.4.1.3.
5. **vga12.inc:1795-1797** - "Colour reduction on a 1bpp adapter happens in gfx_fill, per run" Code: an on-screen block on 1bpp is reduced by `sw_blit_row`'s pair tables built from `gfx_inktab`; gfx_fill only for blocks off-screen in x. Proposed: "On a 1bpp adapter the reduction is the pair tables' (sw_pairbuild, from gfx_inktab), or gfx_fill's per run for a block off the screen in x."
6. **vga12.inc:1841-1851** - "there is deliberately no GFXDENTER here: A RUN CARRIES NO SOURCE" Code: `gfx_disp_enter` is called at 1946 when the block fits one display (39.14.7.1), and `.cut` splits a straddle (2281-2294). Proposed: "the display hook is taken below, at .hkdone, when the block fits one display (39.14.7.1); a straddle is cut (39.14.7.2) and only an odd cut keeps the destination virtual."
7. **vga12.inc:2469-2472** - "the table and the four plane rows are .lowbss - so DS becomes LOW_SEG and nothing needs an override" Code: the plane rows are `.vgabuf` (7166-7176), DS = VGABUF_SEG / `[mega_vbsel]` / `[rom_vgabw]` (2585-2601), the table is read `ss:` (2615-2631). Proposed: "the table is .lowbss and read ss:, the four plane rows are .vgabuf and DS becomes VGABUF_SEG for the decode - so every .lowbss word in between needs ss:."
8. **SPEC.md:4585-4588** - "the table and the plane rows are `.lowbss`, so **`DS` becomes `LOW_SEG` and neither needs an override**" -> same correction as 7.
9. **vga12.inc:7141-7143** - "because the decoder runs with DS = LOW_SEG and reads both with no override at all" Code: `sw_blit_row` reads `ss xlat` with DS = the caller's source (softgfx.inc:1004-1022); `vga_blit_prow` reads `[ss:bx+vga_p4tab]` with DS = VGABUF_SEG. Proposed: "and it costs one `ss:` prefix to reach, SS being LOW_SEG for all kernel code, while DS is the caller's source or VGABUF_SEG."
10. **vga12.inc:5865-5868** - "kern_big only; kern_small's thunk is stc/retf" Code: the body assembles on both builds (KERN_SMALL arms at 5911-5914, 6053; 5858-5862 says so). Proposed: "kern_big and kern_small (SPEC.md 5.4.2.5.1); kern_small's arms leave out the pen, the second display and the VGA ports."
11. **SPEC.md:4178** - "`kern_big` only — the small build refuses with CF=1" -> "both kernels since §5.4.2.5.1; `kern_small` has no pen, no second display and no off-grid x".
12. **SPEC.md:4160** - "`gfx_xor_fill` and `gfx_xor_rect` are additionally cut to it" Code: also `gfx_fill_pat` (GFXCLIP, 3964), `gfx_frame` (per strip), `gfx_blit4` (walk, 1898-1908), `gfx_blit1` (rows/fragments), `gfx_points` (`gfx_ls_box`), `gfx_blitp` (DI bit 14 walk); `gfx_spans` and `gfx_scroll` refuse. Proposed: list them.
13. **SPEC.md:4228** - "its `bb_*` twin in softgfx.inc on a 1bpp adapter" Code: twins are `sw_*`, dispatched by `VIDEXTM`/`VXMONO` on `[vid_planes]` below GFXCLIP/GFXDISP. Proposed: "its `sw_*` twin, through the `[vid_planes]` dispatch at the raw body below the clip and display hooks".
14. **SPEC.md:4096** - "the coordinate core `vga_rect_setup` that both renderers share (§39.3)" Code: `gfx_rect_setup`, in softgfx.inc:96. Proposed: "`gfx_blit1`, `gfx_points`, `gfx_spans`, the pens and the theme; the shared rect geometry is `gfx_rect_setup` in softgfx.inc" (and the other 26 `vga_rect_setup` / `[vga_off]` / `vga_lmtab` mentions in SPEC, e.g. 7188, 7202-7205, 27593-27594, are the same rename).
15. **SPEC.md:70522** - "Module prefix `bb_`" -> "Module prefix `sw_` (the shared `gfx_rect_setup` and `gfx_pat_stage` keep `gfx_`)".
16. **SPEC.md:70938** - "`SWM_SOLID` — per plane, the `[gfx_color]` bit picks a 00/FF byte; edges" Code: `sw_ink` reduces `[gfx_color]` through `gfx_inktab` to 00/FF or the 50% dither (softgfx.inc:381-404); edges are one store `dest ^= (pat ^ dest) & mask` (788-792); interiors `rep stosw` with an odd tail. Proposed accordingly.
17. **SPEC.md:70967** - "`vid_apply` sets the plane stride to a single paragraph — purely so `sw_xfer`'s segment compare terminates" Code: there is no plane-stride cell (`vid_rpara`/`vid_rend` are gone, grep); `sw_xfer` makes one pass (softgfx.inc:1372-1382). Proposed: "`sw_xfer` makes one pass over `[vid_rseg]`; the plane stride and end it used to walk to are gone (§39.26)."
18. **softgfx.inc:1378-1382** - "vid_apply - the sole writer of all three cells" ... "sets [vid_rpara] = 1 and [vid_rend] = [vid_rseg] + 1" Present tense about deleted cells. Proposed: "vid_apply set [vid_rpara] = 1 and [vid_rend] = [vid_rseg] + 1 on every non-VGA arm, so the walk was always one pass; both cells have since gone."
19. **softgfx.inc:304, 321-324, 388, 417-420, 439-440** - "sw_rect_pl" (e.g. 417: "caller is sw_rect_pl (a tail `jmp`, so this routine's `ret` is its)") Code: `sw_rect_pl` was folded into `sw_rect` (245-249) and `sw_plane_op` is `call`ed from it (293). Proposed: "The one caller is sw_rect (a `call`, 293), which banks all eight registers in its prologue."
20. **softgfx.inc:445** - "SPEC.md 39.31 - THE RECT IS WALKED A BANK AT A TIME (kern_big)" Code: `KHP_MONORECT` is defined by no kernel (kernel.asm:274-282). Proposed: "(KHP_MONORECT: a knob, built and OFF on every kernel - SPEC.md 39.31's banner)".
21. **softgfx.inc:1348-1350** - "a buffer saved from the back buffer restores to it byte-identically" ... "restore additionally dirties the rect." Code: no back buffer, no dirty rect; restore disarms the sub-rect (1453-1459). Proposed: "same rounding and buffer layout (SPEC.md 5); restore honours and then disarms the sub-rect (SPEC.md 5.8)."
22. **softgfx.inc:938** - "(vga12.inc:2714," Code: the call is vga12.inc:2100. Proposed: "(gfx_blit4's `.row`, vga12.inc,".
23. **SPEC.md:8011-8013** - "It fills the same `vga_*` scratch ... 1bpp goes through `sw_rect_pl`" Code: `gfx_sp_next` answers in registers and writes no scratch (vga12.inc:3678-3685); 1bpp is `sw_spans`' own loop (softgfx.inc:328-362). Proposed: "It answers in registers (AH/AL masks, BX span, DI byte); 1bpp is `sw_spans`' own row loop (§5.10.4) and VGA a copy of `gfx_fill_raw`'s row body with the GC arm hoisted."
24. **SPEC.md:7239** - "Kernel hot-paths pass 1 (docs/plans/khp1/gfxcall.md) took three more off the" Code: item 9 is `KHP_FRAME` and item 10 `KHP_FPG`, defined on kern_mega only (kernel.asm:284-295). Proposed: add "(9 and 10 are knobs, built on kern_mega alone)"; and rule 3's `vga_lmtab`/`vga_rmtab` (7202) -> `gfx_lmtab`/`gfx_rmtab` (softgfx.inc:83-84).
25. **vga12.inc:1725-1729** - "gfx_ls_ink, gfx_ls_box, gfx_ls_addr and gfx_ls_lx/ly. gfx_points needs all four" Code: `gfx_ls_ink` is inlined (1453-1457), `gfx_ls_addr` gone (1580-1583), `gfx_ls_lx/ly` gone (1621-1625); `gfx_ls_box` alone remains. Proposed: "What stayed is gfx_ls_box; the ink class, the byte and bit and the translation are inline in gfx_points."
26. **vga12.inc:1124, 1597, 5726** - "gfx_pt_resolve rewrites all five at every call's .pass" Code: folded into `.pass` (1420-1424). Proposed: "gfx_points' .pass rewrites all five".
27. **vga12.inc:701-702, 709, 4150** - "font_char, font_run, ico_core, gfx_scroll, gfx_save/restore and gfx_line" / "gfx_line's axis-aligned cases are gfx_hline/vline" / "which is how gfx_line already uses it in this file" Code: `gfx_line` is a `stc/ret` stub (1097-1102). Proposed: drop `gfx_line`; 4150: "BP CARRIES THE FILL".
28. **vga12.inc:5373-5374** - "per-row rep movsb through gfx_rowbase, immune to the banked layouts by construction." Code: with dy a whole number of banks the base is computed once and the destination steps `gfx_nextrow` (5496-5602). Proposed: "a constant-delta walk when dy is a whole number of banks (SPEC.md 5.5.1), else two gfx_rowbase a row."
29. **SPEC.md:6014** - "**A plane step with bit 14 in it is refused as a request**" Code: bit 14 is silently stripped (`and di, 0xBFFF` at 2855, `and di, 0x3FFF` at 6876), nothing refuses. Proposed: "A plane step with bit 14 in it is READ as the request and stripped: a caller keeps its planes under 16 KB apart".
30. **vga12.inc:1742-1743 / SPEC.md 5.4** - "[gfx_color] is left holding the last colour drawn." See gfx-5: true only on run paths. Proposed (if not fixed): "[gfx_color] is UNSPECIFIED afterwards."

## Open questions

- vga12.inc:2466 says the odd-x `rcr` pass is "~39% of a row"; SPEC.md:4577
  says "~27% of a row". One is stale; which measurement is current?
- Is a negative width (gfx-1) reachable from any shipped package today? A
  grep of `OSAPI_GFX_BLIT4` callers for unclamped `x2 - x1 + 1` would say;
  the fix is free either way.
- kern_mega: does any shipped package hand `gfx_blit4` a source within 3
  bytes of its claim's limit (gfx-2)? Paint's canvas at full width is the
  first candidate.
- gfx-4's predicted gain wants one `gfxbench` row on MartyPC's
  `os8088_xt_vga` before the bytes are spent.

## Verification

The adversarial verifier re-read the cited code, the other build arms and the
plan lists on 2026-11-08. Nothing was built or run. The machine-readable
result, with every doc fix checked for an exact and unique match, is
`../data/gfx.json`.

- **gfx-1: CORRECTED** (still high). The bug is real. The VGA group count was
  wrong: with w < 0, the last pixel x+w-1 lies left of x, so `[vga_pr_n]` is
  <= 0 and BP wraps to about 65,536 groups. The decoder then sweeps the whole
  64KB of VGABUF_SEG, which is the bottom of the heap, not 8KB of it. The
  ~8KB per row figure belongs to sw_blit_row on 1bpp, kern_small included.
  `jle` and `jz` have the same 2-byte short encoding.
- **gfx-2: confirmed** (plausible, not run). kern_mega builds GFX_PLANE, and
  no limit guard exists.
- **gfx-3: CORRECTED.** On kern_small, CLIPQF is the inline `cmp word
  [wm_clip_n],0`, not a far call, so the saving there is -3 bytes and about 20
  clocks rather than -4 bytes. On kern_big only the `jne .okq` fall-through
  needs `or sp,sp`, because `ja .clipok` already arrives with ZF=0; net -3.
- **gfx-4: confirmed** as a perf proposal. The gain is PREDICTED. SPEC 5.6.9.2
  never measured or refused a planar arm.
- **gfx-5: confirmed.** The header and SPEC 5.4 are corrected below to say
  UNSPECIFIED.
- **gfx-6: confirmed.** It is already ranked in LAST-DROP-PERF.md 7.1.
- **gfx-7: confirmed.** The `.ovl` placement of vid_apply was not checked.
- **gfx-8: unchecked.**
- **gfx-9: confirmed.**
- **gfx-10: CORRECTED.** SPEC §1 rule 3 already scopes the IF rule to
  ISR-callable code. Only CLAUDE.md/AGENTS.md's short form needs the scope
  back.
- **gfx-11: confirmed.** The §4 row is fixed below.
- **gfx-12: confirmed.** All 31 listed mismatches held.
- **gfx-13: confirmed.**
- **gfx-14: unchecked.**
- **gfx-15: confirmed.** The sort is wm.inc's.
- **gfx-16: confirmed** (gfx_hline relies on gfx_fill keeping CX).
- **gfx-17: unchecked.**
- **Doc mismatches: 31 confirmed, 0 refuted.** Nos. 4, 13 and 30 were
  reworded. No. 30's "refused as a request" is ambiguous rather than flatly
  false, but no path refuses on bit 14. Nine more stale statements in the
  same blocks were added as fixes:
  - vga12.inc gfx_blit4 "no adapter-specific code"
  - softgfx.inc:207 SWM_SOLID
  - softgfx.inc:439 caller chain
  - softgfx.inc:942 and 951, the AX and BP notes
  - SPEC 4580 ".lowbss" plane rows
  - SPEC 4235 `bb_*`
  - SPEC 7203 `vga_lmtab`
  - SPEC 4253 `[gfx_color]`

  That makes 40 doc fixes in all.
