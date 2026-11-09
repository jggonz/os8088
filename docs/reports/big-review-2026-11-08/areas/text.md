# Font, icons, menus, file-op progress

Area key: `text`. Reviewer pass of 2026-11-08 on branch `the-big-review`
(tip `4f0da91`). Read-only: nothing was built, booted or measured. Every
cycle figure is PREDICTED from PERFORMANCE.md Part 2's table unless it quotes
a number the tree already measured. Symbol spans quoted from
`build/kernel.map` are kern_big's default build as it sits in `build/`.

## Scope read

| file | lines | read |
|---|---:|---|
| `kernel/font.inc` | 2,793 | all of it: every proc, every `%ifdef` arm (SEAMCUT, BAKED_FONT, KHP_STRRUN, SNAPAUDIT, VID_EXT, KERN_MEGA, KERN_SMALL) |
| `kernel/icons.inc` | 1,348 | all of it, the pool and every IREC record |
| `kernel/menu.inc` | 2,721 | all of it |
| `kernel/fprog.inc` | 1,388 | all of it |
| `kernel/ascw.inc` | 1,085 | all of it (FORMAT.DRV's `.modf`) |
| `kernel/wm.inc` | 6820-7030, 7850-7866 | `wm_clip_test`, `wm_clip_rows`, the caption's two spellings |
| `kernel/vga12.inc` | 20-60, 440-480, 800-870, 2800-2825, 6630-6650 | `gfx_mono1`, `CLIPQ`, `GFXDENTERCD`/`GFXDLEAVEI`, the cull, `gfx_clip_run`'s hole bank |
| `kernel/dockmod.inc` | 520-540, 1520-1580 | `dkx_paint`'s bank, `dkx_gfx_hole_arm` |
| `kernel/hiber.inc` | 5058-5100 | resume step 4a (FONT_PICK) |
| `kernel/kernel.asm`, `viddet.inc`, `toast.inc`, `stikmenu.inc`, `band.inc`, `assoc.inc` | spot reads | KHP knobs, `osapi_font_glyphs`, `vid_popmax`, `VID_ROWTAB`, `toast_now`, `stm_text`, band header, ASC_* constants |
| `SPEC.md` | §6 (8598-8800), 6.0.1, 6.1.13, 6.6.2, 11.96.17.1 (24745-24800), 11.97.1, §12 (12.05, 12.2 26440-26460, 12.4 26705-26780, 12.8 26974-27000), §25 (57389-57462) | |
| `apps/os88api.inc` | 220-290, 2525-2560, 3920-3945, 5165-5200 | FONT_*, ICON_*, FS_PROG/FS_OP, FONT_GLYPHS, FONT_RUN |
| `docs/plans/ABI-BREAK-LIST.md`, `KERNEL-BYTE-QUEUE.md`, `MONO-RECLAIM-PLAN.md`, `MOVABLE-COLD-PLAN.md`, `LAST-DROP-PERF.md` §7 | | to avoid re-proposing (R3 and V6 are the only text/menu rows; LAST-DROP-PERF row 15 is KHP_ICONWORD) |

## Verdict

This is the most heavily optimised and the most heavily *argued* area of the
kernel, and the code is better than its prose. `font_run_x` is a genuinely
good 8088 text engine: one y, one pen and one row base per run, a pre-walked
glyph-pointer table so the drawing pass is row-major, a `rep stosb` trailing
span for the padding this system uses as its erase, a planar prologue that
gets mode 12h to one store per cell row, and an unaligned path that writes
n-1 bytes plain and two edge columns. The menu bar as a cell-diffed
composition (`menu_bcell` is the record of the glass) and the file-progress
widget driven from the disk choke points are both systemic ideas the period
systems did not have. The bugs found are latent rather than live: one real
structural hole (`fpg_bare` banks the clip count but not the dock's hole,
which every other bank site in the tree does), a 14-byte unreachable arm in
`font_ink` that SPEC 11.96.17.1 still describes as working, a
path-dependent `[gfx_color]` clobber the slot contract does not mention, and
unbounded width measures over package strings in the menu layout. The
largest weakness is not in any instruction: it is that ~45% of these five
files is comment, much of it history ("it used to"), and this pass found
twenty-odd statements in those headers, SPEC and the SDK that no longer
describe the code. Rethinks worth having are small: a kernel-side BOLD for
`font_run` (retires the last structural reason for transparent text), real
separator and check-mark item markers, and moving the once-per-run VGA
prologue into the `.vga` rung.

## Strengths

1. **`font_run_x`'s run model** (font.inc:1181-2020). Everything that is a
   property of the RUN - the y test, the two inks, the row base, the cell
   count, the trailing span, the plane grouping - is decided once; what is
   left per cell row is `lodsw / xchg / add / mov / and / xor / stosb`
   (`FONT_RN_PX`, font.inc:1152). The glyph-pointer table biased by +8 so the
   row counter's `inc bp` is the loop test (font.inc:1428, 1584) is the kind
   of trick that only matters on an 8088 and is exactly right there. Copy
   this shape for any other "many cells, one pen" primitive.
2. **The ROM font read in place, chosen by the PIT** (font.inc:213-420).
   Measure, then spend: the 760 bytes are copied into a heap claim only on the
   machine that timed its table as >25% slower than RAM, billed to the arena
   top-down and pinned, and the same `FONT_PICK` macro runs at a resume
   (hiber.inc:5073) so a different reading machine is asked the same question.
   No resident byte on the machines that do not need it.
3. **The bar is one composition** (menu.inc:1021-1290). `menu_bcell` is both
   the composition buffer and the record of what is on the glass, so
   composing IS the diff; padding is the erase; a minute rolling over is one
   glyph. `menu_bput` advances DI even on a dropped cell, which turned a
   livelock class (SPEC 59.7) into something no caller can write
   (menu.inc:1455-1490). This is curses' "virtual screen vs physical screen"
   at the granularity this hardware can afford.
4. **Save-under sized from the rect, purgeable, movable, with a correct
   fallback** (menu.inc:1822-2100). A refused or purged claim degrades to a
   damage-rect repaint, and the `.repaint` path's `menu_bdirty` disarm
   (menu.inc:2053-2080) shows the failure was understood, not patched.
5. **`MENU_DIS` as a string prefix and "two labels over one db"**
   (menu.inc:2496-2497). Greying costs one byte per item and no structure;
   the live label is the tail of the greyed one. Cheaper than the Mac's
   per-menu enable bitmask and needs no API.
6. **The progress widget owns the FREEZE, not the operation** (fprog.inc
   header, `fpg_busy`, `fpg_finish`). Two choke points (dsk_xfer,
   drv_fs_call) arm it, `gfx_unlock` is the only teardown, so no caller can
   forget a bracket and no early return can leave it up. FPG_WARM keeps a
   one-sector probe from flashing it. No period system had a system-owned
   disk progress indicator at all.
7. **Data checked at assembly time.** `IREC`/`IRUN`/`IHALF`/`IEND`
   (icons.inc:863-908) make a mis-counted run stream a build error; the
   `%if` adjacency guards (`font_rn_fg/bg`, `font_rn_xm/bm`, `ico_c1/c2`,
   `menu_bdirty/bovr`, `menu_seg/dseg`) cost nothing and fire on the edit that
   breaks the one-word store. This is the right answer to "fragile invariant
   held by prose".
8. **One-shot pens that a refusal spends too** (icons.inc:168, 213):
   `ico_c1/c2` go back to the icon pair on the refused path as well as the
   drawn one, which is the case nobody tests.
9. **`menu_hover` reads the pointer once under `cli`** (menu.inc:2254-2300),
   with the measured failure (4 in 320 wrong launches) written beside it.
10. **ascw.inc treats ASSOC.DAT as hostile** (ascw.inc:384-445,
    691-760): size under 64KB and under the buffer, magic, both counts under
    the caps, the counts fitting the bytes that arrived, and header fields
    taken only if the bytes reached them; an I/O error is not "no file". It
    also reads the volume it writes about, so a swapped floppy cannot get
    another disk's cache.

## Findings (ranked)

### text-1 - `fpg_bare` banks `[wm_clip_n]` but not `[gfx_hole]` (hazard, medium, now, PLAUSIBLE)

fprog.inc:161-170 zeroes `[wm_clip_n]`, runs the painter, and puts the count
back. On a DOCK_OPT kernel (kern_big) `CLIPQ` is `gfx_clip_query`
(vga12.inc:454-470): with `[wm_clip_n]` = 0 and `[gfx_hole]` non-zero (the
hidden dock's strip is OPEN) it tail-jumps to `gfx_hole_arm`, which
`dkx_gfx_hole_arm` (dockmod.inc:1542-1562) implements by **re-seeding
`wm_clip_tab`** with the desktop minus the strip. `fpg_bare`'s `pop word
[wm_clip_n]` then restores the CALLER's fragment count over the hole's
table. The caller's next primitive in the same hold clips against
"desktop minus strip" (plus any of its own fragments past index 3) instead
of its window minus occluders, i.e. it can paint over windows above it.

Every other bank site in the tree does both words for exactly this reason:
`menu_drop` (menu.inc:1929-1935 / 2038-2039), `gfx_clip_run`
(vga12.inc:6638-6644, whose comment is the bug report: *"a hidden dock's
hole would re-arm a region the moment wm_clip_n reads 0"*), `dkx_paint`
(dockmod.inc:528-534). `toast_now` (toast.inc:412) goes through
`fpg_bare` and inherits the hole.

Trigger: a UI-task handler that arms a region, does file I/O that arms or
steps the widget (or raises a toast), then draws again in the same hold,
while the strip is open. Narrow, but the defect class is "silent clip
corruption", and the fix is mechanical.

Recommendation: under `%ifdef DOCK_OPT`, `push word [gfx_hole]` / `and
word [gfx_hole], 0` before the call and `pop word [gfx_hole]` after, in
`fpg_bare` itself so all four callers (fpg_arm, fpg_busy, fpg_step via
fpg_gbare, fpg_finish, toast_now) are covered. Bytes: **+13 `.text`,
kern_big only**; kern_small has no DOCK_OPT.

### text-2 - `font_ink`'s VGA arm and its `[gfx_mono1]` test are unreachable (size, medium, now)

`font_ink` (font.inc:945-1018) has one caller, `font_char_bb`
(font.inc:789), and `font_char_bb` has one caller, `font_char`, which
reaches it only when the display is 1bpp: `cmp byte [vid_mono], 0 / je
.vram` (font.inc:607-608) or, under VID_EXT, `cmp byte [vid_planes], 4 /
jae .vram` (font.inc:603-604) - and `vid_depth_set` writes mono and planes
together. So on entry to `font_ink` `[vid_mono]` is always 1, the `mov al,
[vid_mono] / or al, [gfx_mono1] / jz .vga` (font.inc:949-951) never jumps,
and `.vga` (font.inc:1006-1008) is dead. `grep` confirms no other caller.

The bytes: 3 + 4 + 2 + 3 + 2 = **-14 `.text`** on kern_big and kern_mega
(GFX_VGA; kern_small already omits it), plus ~40 clocks (fetch floor) per
glyph cell on every Hercules/CGA string drawn by a kern_big - ~1.2% of a
~3,300-cycle mono cell. The two parity `nop`s after it (font.inc:1011-1017)
will need re-balancing (t_hotpar).

The behaviour SPEC 11.96.17.1 credits to this test - "font_ink takes its
mono path when [vid_mono] is set or the window being drawn has declared two
colours" - does not exist on any path; the same section's next paragraph
correctly says `font_str` never reaches it on a VGA. See the mismatches.

### text-3 - `font_run_x` leaves `[gfx_color]` path-dependent, and no published contract says so (hazard, low, now)

The fast paths (`.rm`, `.rmp`, `.rmu`, `.planar`) never write
`[gfx_color]`; `.whole` (font.inc:1951-1961) leaves it at the FOREGROUND;
`font_run_scell` (font.inc:2228-2236) leaves it at the BACKGROUND for a
refused cell and the foreground for a drawn one. The header
(font.inc:1090-1093) says "left at the run's foreground on a drawn run",
which is false for every fast path. `OSAPI_FONT_RUN`'s SDK prose
(os88api.inc:5170-5193) and SPEC 6.1 do not mention the pen at all.

So a package that sets a pen, calls FONT_RUN, then draws a line gets its own
colour on a mono aligned run and on any VGA subset pair, and the run's
foreground on the Color theme's disabled chrome (`.plno`), on an armed-region
cut run, or on a two-display straddle (`.cells_nx`). Same source, different
pixels by adapter and theme - the class of bug SPEC 39.4 exists to prevent.

Recommendation: state it in the slot contract ("[the pen] is undefined
after the call; set it before your next draw") - **0 bytes**. Restoring it
(bank AL at entry, store at `.out`) is +8 `.text` and ~25 clocks a run if the
owner prefers a clean contract.

### text-4 - `osapi_font_glyphs` can hand out the SEAM's table on an extended desktop (hazard, low, now)

`font_ch_seam` (font.inc:448-458, SEAMCUT = kern_big) points `[font_seg]`
at LOW_SEG and `[font_base]` at `font_seam_a - (FONT_SEAM_A-FONT_FIRST)*8`
for one `font_char` call, under the gfx lock. `osapi_font_glyphs`
(kernel.asm:6627-6632) takes no lock and reads the same pair. A worker that
asks for the table while the UI task is pre-empted inside a seam cut gets
DX:SI into `.lowbss` biased by 760 bytes - garbage glyphs, and for an app
that copies "the font" before an FSX bracket, a wrong font for the session.

Recommendation (cheapest correct): give the seam its own pair. `font_char`
and `font_char_bb` already load ES from `[font_seg]` once a cell; only the
SEAMCUT arm needs to differ. Or keep a canonical pair for the API that only
`font_init`, `hbm_wake` 4a and `mega`'s pool move write (+4 `.bss`, +~6 in
each writer, `.boot2`/HIBER.DRV/`.cold`). Bytes: **+4 resident `.bss`**
either way. Rare enough that writing the window into SPEC 6's slot row is
also defensible.

### text-5 - menu layout measures PACKAGE strings with an unbounded `font_width_x` (hazard, low, now)

`font_width_x` (font.inc:2542, `repne scasb` with CX = -1) returns 8 x
length mod 65,536. `menu_widest` (menu.inc:2427-2460) measures every item
with it and then clamps to MENU_MAXW; `menu_trunc` (menu.inc:2390) letters at
most MENU_MAXCH = 24 cells. An item of >= 8,192 non-NUL bytes wraps the
width to a small number, so the rect (and the save-under, sized from the
rect) is narrow while 24 cells are lettered past it: the "permanent garbage
on the desktop" that menu_trunc's own header exists to prevent. A package's
counts are already treated as untrusted (SPEC 12.2); its lengths are not.

The same unbounded measure sets the first app menu's left edge in
`menu_relayout` (menu.inc:570, the full AM_NAME) while the name cell
displays the 24-glyph copy measured at menu.inc:719 - so a 25..60-char
name leaves a dead gap, and a wrapping one overlaps the name cell.

Recommendation: measure with the bound the drawing uses - `repne scasb`
with CX = MENU_MAXCH + 1, which `menu_trunc` already spells - in
`menu_widest`, and take the name width from `menu_abstr + MENU_ABPFX_LEN`
(build the copy first). **~+4 to +8 `.text`**, both kernels.

### text-6 - fprog's one-word-store adjacencies are half-asserted (consistency, low, now)

`fpg_finish`'s `xchg ax, [fpg_on]` (fprog.inc:1228) and KHP_FPG's `cmp word
[fpg_on], 0` (fprog.inc:1199) need `fpg_warm` to be the byte after
`fpg_on` on EVERY kernel, but the `%if (fpg_warm - fpg_on) != 1` guard is
inside `%ifdef FPG_PARK` (fprog.inc:1340-1344), i.e. kern_big only.
`mov [fpg_pat], ax`/`cx` (three sites: fprog.inc:431, 669, 852) empties
`fpg_pat` and `fpg_px`'s low byte in one store and has no guard at all
(fprog.inc:1345-1352 say "KEEP [fpg_px] NEXT TO IT" in prose).

Recommendation: hoist the first `%if` out of the `%ifdef`, add `%if
fpg_px - fpg_pat != 1`. **0 bytes.**

### text-7 - `font_run_x`'s VGA prologue is resident on mono machines and runs once per RUN (size, low, now)

MOVABLE-COLD-PLAN kept `font_char`'s cell writer in `.text` because a `jmp`
there "would land on every call of the hottest paths". `font_run_x`'s
`.planar` prologue is not that case: it runs once per run, before a
~4,300-cycle prologue and a row pass. From `build/kernel.map`:
`.planar`/`.vxp0` 0x3E65 .. `.plno` 0x3EF5 = **144 bytes** (120 of planar
grouping, 24 of the VESA door) plus `fnt_rn_edge`'s VGA arm (~25), all dead
on CGA/Hercules/MDA. Moving them into `.vga` (with a `tests/vgarefs.txt` row
whose guard is the `[vid_planes]`/`[vid_mono]` test `font_run_x` already
makes) costs one near `jmp` in and turns two or three short exits into near
ones.

Bytes: **~-165 resident on a mono kern_big machine, ~+10 on VGA/EGA** - and
KERN_BUDGET is measured on VGA, so the budget machine pays the +10. Worth it
only if the owner values the mono heap over the VGA budget; recorded so the
trade is priced.

### text-8 - `[vid_popmax]` is a constant held in memory (size, low, now)

viddet.inc:362 `vid_popmax dw MENU_POPMAX`, never written since vid_apply
stopped computing it (viddet.inc:918). Its only reader is `menu_setup`
(menu.inc:1764-1766). `cmp ax, MENU_POPMAX` (imm8 form) and `mov ax,
MENU_POPMAX` replace the two memory operands. **-3 `.text`**, both kernels.

### text-9 - VGA `ico_pass` keeps per-word bookkeeping in memory (perf, low, now)

`ico_pass` (icons.inc:592-655, `.vga` rung) does, per source word, `add word
[ico_col], 2`, `dec word [ico_wleft]` and up to three `cmp word [ico_col],
imm16`: ~90 clocks a word. `ico_pass_bb` moved the same column and limit
into DL/DH; the VGA pass cannot (DX is the GC port) and was never given an
alternative. A 32x32 desktop icon is 64 rows x 2 words x 2 passes = 256 words
-> ~23k clocks, ~4.8 ms of bookkeeping alone on the owner's 5150 with a
PVGA1A. PREDICTED.

Recommendation: ask once per icon whether `col0 + 2*ww + 2 <= ROW_BYTES` (the
common case) and take a copy of `.word` with no column compares, as
KHP_ICONWORD does for the 1bpp aligned case. **~+20 bytes in `.vga`** (not
resident on mono). The same per-icon test also gives the VGA arm the
fully-off-right early-out `ico_core_bb` has (icons.inc:557-560).

### text-10 - `ico_pass_bb` pays a near call per framebuffer byte (perf, low, record; partly listed)

icons.inc:776-800: three `call ico_bbop_byte` per source word, each a call,
a memory compare and a return - ~55 clocks a byte, ~165 a word, on the
shifted path that KHP_ICONWORD (LAST-DROP-PERF row 15, off pending LOG O5)
does not cover. Hoisting the op (two loop copies, as `font_char_bb`'s
`.srow`/`.crow` already do for glyphs) is ~+30 `.text`. Recorded so the
shifted case is priced next to row 15 when O5 is decided.

### text-11 - an empty volume keeps a stale ASSOC.DAT (hazard, low, now, PLAUSIBLE)

`ascw_flush_x` (ascw.inc:153-154): `asw_close` returns CF = 1 when no package
was placed, and the flush takes `.same` - success, nothing written. A
volume whose packages were all deleted keeps the old file, which still
names rows (stem, size, folder cluster) for packages that are gone and
associations to them. A full-mode flush (the installer's path) is the one
that should not leave it.

Recommendation: when the old file exists (`asw_dclus` >= 2) and the build is
empty, write the 16-byte header with both counts 0 (a valid empty cache)
instead of succeeding silently. ~+10 bytes in FORMAT.DRV, 0 resident.

### text-12 - `menu_drop` and `fpg_paint` fill a ground and then letter over it (weakness, low, record)

`menu_drop` (menu.inc:1939-1985) fills the whole rect, frames it, then
`font_run_x`s each item over the filled ground: every text cell is written
twice (CLAUDE.md rule 2). `fpg_paint` (fprog.inc:811-855) fills the bed, then
draws the icon (whose mask pass re-whites it), the frame and the trough over
it. **Neither is worth changing by hand**: the exact-once spelling with the
primitives available is MORE calls (top/bottom pad strips per item), and a
redraw is priced by calls. The only clean answer is composing - the band
(SPEC 5.9) - which is a knob since 5.9.6. Recorded so the next person does
not "fix" it into more calls.

### text-13 - two `nop`s under complementary `%ifdef`s are one unconditional `nop` (consistency, low, now)

font.inc:1011-1017: `%ifdef KERN_SMALL nop %endif` then `%ifndef
KERN_SMALL nop %endif`, each with its own reason. Every build emits exactly
one byte. Collapse to one `nop` with both reasons, or let text-2's change
re-derive it. 0 bytes.

### text-14 - the routine headers carry history, and history goes stale (weakness, medium, now)

Comment-only lines are 1,065 of font.inc's 2,793, 1,288 of menu.inc's 2,721
and 811 of fprog.inc's 1,388, before counting the paragraph-long trailing
comments. The headers this pass found wrong (see Doc/code mismatches, ~20
of them) share one shape: a contract written at the time, then a later pass
changed the body and wrote a NEW paragraph about the change while the old
contract line stood (`font_char`'s "whole-char" clip, `font_str_x`'s "five
call sites", `font_run_cell`'s "a planar run takes .slow", `font_ink`'s "per
plane", menu.inc's "SAVE_SEG:0", `menu_drop`'s "MENU_SAVE_KB"). The
reasoning is valuable; its location is the problem - it is read as the
contract.

Recommendation: a convention, not a gate: a routine header states the
CURRENT in/out/clobbers/invariants only; "it used to" paragraphs move to the
SPEC section or the pass's report, leaving a one-line pointer. Do it file by
file as each is next touched. 0 bytes.

### text-15 - RETHINK: BOLD as a property of the run (rethink, medium, now - additive slot)

SPEC 6.6.2 case 6 (overstrike: Word, Texpad, Frotz x2, cword - five sites)
is now the only structural reason a text pixel is written twice: bold is
"the string struck again one pixel right", an opaque pass and a transparent
one. STIKMENU.DRV already synthesises bold in its own composer as `row |
row >> 1` (stikmenu.inc:1387-1400), which stays inside the cell on the ROM
8x8 set (column 7 is blank on every glyph that has strokes). Doing that in
`font_run_x`'s compose - a one-shot style like `OSAPI_ICON_PEN` (a new slot
at the end, or a bit beside `[gfx_dis]`), one extra `mov ah,al / shr ah,1 /
or al,ah` in a bold copy of the row loop - makes bold one pass and retires
case 6 and its registry rows. **~+35-45 `.text`** (kern_big; kern_small can
take the plain loop). Prior art: QuickDraw's bold is the same 1-pixel smear
done by the text engine, not the application; Windows GDI emboldens raster
fonts the same way.

### text-16 - RETHINK: real separator and check-mark item markers (rethink, low, now - additive)

Separators are `db MENU_DIS, '-----------------'` with the dash count chosen
by hand to match the widest item (menu.inc:2523-2534, and isotron/pixel/
realdmg in apps/); a later wider item makes the rule short. `MENU_DIS` is
also used as a RADIO mark (SPEC 12.2: "the dithered item is the one that is
currently selected"), which reads as "disabled" to anyone who knows the
convention. Two more prefix bytes, still one compare in `menu_item_dis` (`<
MENU_SEP+1`): MENU_SEP draws one `gfx_hline` across the menu's interior in
the cell's middle row (the ground is already filled, so no double write), and
MENU_CHK draws a mark in the 8px left margin every item already has and stays
selectable. **~+12 `.text` in menu_drop, -19 for kern_big's `menu_s_msep`**,
net about -5 and every package's dash string goes. Old kernels would draw a
MENU_SEP item as a selectable blank, so ship it with the SDK move it
belongs to. Prior art: Mac `(-` meta-character and `CheckItem`, Windows
MF_SEPARATOR / MF_CHECKED, Intuition's CHECKIT.

### text-17 - RECORD: the icon API is 16 px wide because the stage shares `ico_ibuf` with the desk band (record)

`ICO_STAGE_WW` = 1 (icons.inc:50-55) is not a size accident: `ico_ibuf`
(256 bytes) already holds a 32x32 body, but `ico_band` lives in its top 136
bytes while a desktop cell composes (icons.inc:1330-1345) and
`OSAPI_ICON_DRAW` is called inside that compose (the Wire's driver,
icons.inc:200-206). A 32-px plain kind would need the stage out of the band
(+136 `.bss`) - so a package draws a 32x32 picture as two calls. Recorded so
the limit is not "fixed" into a band overrun.

### text-18 - RECORD: `font_char`'s VGA writer multiplies y by 80 per glyph (perf, record, refused on numbers)

font.inc:632-633 `mov ax, ROW_BYTES / mul dx` is ~125 clocks a VGA glyph.
The row table does not reach VGA (VID_ROWTAB = 348 rows, viddet.inc:464, and
the 480-row table was built, measured and refused, MONO-RECLAIM 4.0); a
shift-and-add y*80 is ~+13 resident bytes for ~45 clocks of a ~2,000-cycle
VGA cell (~2%). Not recommended; priced so it is not re-derived.

### text-19 - RECORD: no keyboard path INTO a menu (prior art, record)

The keyboard mouse steps a DROPPED menu by item (`menu_kbnav`,
menu.inc:2153-2250), but nothing opens the bar from the keyboard (Windows
F10/Alt + mnemonic, Mac Cmd-key equivalents shown in the item's right
column). A real model - a key opens cell N, arrows walk cells and items, Enter
dispatches through the same (cell, item) pair `menu_track` returns - is
~150-250 `.cold` bytes and the `.cold` placement `menu_kbnav` already uses.
Recorded, not recommended inside "no huge byte growth".

## Rethinks

* **Bold in the run** (text-15) - the one that removes a whole class of
  double-written text.
* **Separator/check markers** (text-16) - small, removes hand-counted dash
  strings across the tree.
* **The `.vga` rung for once-per-run VGA code** (text-7) - a placement rule
  worth stating generally: per-CELL VGA arms stay in `.text`, per-RUN or
  per-OBJECT ones go to `.vga`.
* **Composition for the two remaining fill-then-letter chrome surfaces**
  (text-12) - only if the band knob comes back; not by hand.
* **Considered and refused**: a length-counted FONT_RUN (ABI V6, refused by
  its finder; still right - the scan is <1% of a run); a proportional font
  (it would cost the 8-px cell grid that `menu_bcell`, `font_run`'s single
  store and the bar diff all stand on); a glyph cache of pre-shifted rows
  (6.1.11's run shift is 12-36 clocks a cell row - cheaper than a cache
  lookup on this CPU).

## Prior art

* **Text.** Mac System 5-7 (QuickDraw + Font Manager), Windows 3.x (GDI +
  raster .FON) and GEOS (BSW proportional fonts on an 8088) all chose
  proportional faces and paid for metrics, kerning and per-glyph blits. os8088
  chose one 8x8 cell, and it is the better choice for a 4.77 MHz machine: it
  is what makes `font_run`'s one store per cell row, the cell-diffed bar and
  the row-major run possible. What the period systems did better is STYLE: the
  text engine synthesised bold/underline so applications never double-struck
  (adopt cheaply: text-15). GEOS also cached the active font in RAM; os8088's
  timed in-place ROM read with a measured copy is strictly smarter.
* **Menus.** Mac Menu Manager: MENU resources, per-menu 32-bit enable flags,
  check marks, Cmd-key equivalents, MDEF hook, SaveBits behind the drop.
  Windows 3.x: HMENU with MF_GRAYED/CHECKED/SEPARATOR, accelerator tables,
  CS_SAVEBITS. Intuition: NewMenu with CHECKIT/COMMSEQ. os8088 matches the
  save-behind and beats the Mac on greying cost (a prefix byte), lacks
  separators, checks and keyboard entry (text-16, text-19). Its menu bar as
  an incrementally diffed composition has no equivalent in any of them - they
  redrew the bar.
* **Icons.** Mac ICN# is exactly this record (mask + data, 32x32); os8088's
  one-shot pen pair is a small transfer mode and the indexed pool is a
  compression the Mac never needed. Nothing to adopt.
* **Progress.** None of DOS, Windows 3.x, OS/2 1.x or System 6 had a
  system-owned progress indicator; they had a busy cursor (the watch,
  the hourglass). os8088 has both, and its "teardown is gfx_unlock" rule
  (no bracket to forget) is better than any app-driven progress API of the
  period, including Windows' later common-control progress bar.

## Doc/code mismatches

Each row: file:line, the current text, what the code does, and proposed
replacement. Also in the structured result.

1. **kernel/font.inc:505-512** "Clipping is whole-char ... while a background
   task's clip region is armed, if any part of the cell would leave THAT" -
   the code asks `wm_clip_rows` (font.inc:542-560) and draws the rows on its
   side (and on kern_big masks columns, `[wm_clip_cm]`). Proposed: "Off a
   SCREEN edge the cell is skipped whole. Under an armed clip region it is cut
   to the rows `wm_clip_rows` allows (SPEC.md 11.3.2) and, on kern_big, the
   columns `[wm_clip_cm]` allows (11.3.4); under the damage cull it is drawn
   whole or not at all (11.3.3)."
2. **SPEC.md:8782-8786** "Clipping is **whole-cell** ... and so is one whose
   cell is not wholly inside a single fragment of an armed clip region" - same
   code. Proposed: "A glyph whose cell would cross a screen edge is skipped
   entirely; against an armed clip region the cell is cut by ROWS (§11.3.2)
   and on kern_big by columns (§11.3.4), so a cell an edge crosses keeps the
   part on its side."
3. **SPEC.md:8789-8790** "applies the same shifted row masks to every plane
   the adapter has" - `font_char_bb` has no plane loop (font.inc:770-785).
   Proposed: "applies the same shifted row masks to the one 1bpp plane".
4. **SPEC.md:8622-8624** "A hibernated image carries the pointer and resumes
   only on the machine that wrote it, whose ROM the image's own IVT already
   names (§87.3)." - `hbm_wake` step 4a re-runs FONT_PICK (hiber.inc:5073) and
   refills a heap copy or replaces the pointer. Proposed: "A hibernated image
   carries the pointer, and the resume asks the reading machine again
   (§6.0.1's resume half)."
5. **kernel/font.inc:7-9** "There is no RAM copy: the 768 bytes of .lowbss the
   table and its blank cell were are not resident on any machine." -
   font_init copies into a MEM_K_FONT heap claim when the ROM times slow
   (font.inc:370-390). Proposed: "There is no RESIDENT copy: the table is read
   in the ROM unless font_init times it more than a quarter slower than RAM,
   when it is copied into a 1KB MEM_K_FONT heap claim (SPEC.md 6.0.1)."
6. **kernel/font.inc:725-726** "The same shifted-row masks as the VRAM path,
   applied to all four planes in software" - one plane. Proposed: "The same
   shifted-row masks as the VRAM path, applied in software to the 1bpp
   framebuffer's one plane".
7. **kernel/font.inc:921-923** "The glyph loop tests bit 0 of CH once per
   plane and shifts, so on VGA the colour index goes in untouched." - one
   plane, and no VGA caller (text-2). Proposed: "Its one caller is
   font_char_bb, which runs only on a 1bpp display: the ink is reduced to
   gfx_ink's black or white (00h/FFh)". Also its `out:` line (font.inc:942)
   "CH = the colour index (VGA) or 00/FF (mono)" -> "CH = 00h or FFh".
8. **kernel/font.inc:1051-1054** "Five call sites in the tree pass a package
   segment - the window title (wm_draw_win), the app name and menu titles
   (menu_draw_bar), and the item strings (menu_drop, menu_widest); every other
   caller reaches this through font_str above with ES = DS." - callers are the
   0x0068 X cell (kernel.asm:2672), wm_draw_win's caption (wm.inc:7864) and
   font_run_x's `.slow` (font.inc:1961); font_str is BOOT_PROFILE-only.
   Proposed: "Three callers: OSAPI_FONT_STR_XPARENT's X cell (ES = the
   package's), wm_draw_win's caption, and font_run_x's .slow fallback.
   font_str (ES = DS) survives only in `make BOOTPROF=1`."
9. **kernel/font.inc:1090-1092** "the pen is SPENT: it is left at the run's
   foreground on a drawn run and at its background on one whose cells were
   all refused" - the fast paths leave it untouched (text-3). Proposed: "the
   pen is UNDEFINED after the call: untouched on the single-pass paths, the
   foreground after .slow, the background after a run whose cells the region
   refused. A caller that draws afterwards sets its own pen."
10. **kernel/font.inc:2085-2087** "[vid_mono] is tested three instructions
    above the .cell loop, and a 1bpp target has one plane. A planar run takes
    .slow (SPEC.md 6.1)" - a planar run takes `.planar` and its escapes go to
    `font_run_scell` via `.escape` (font.inc:1463-1475). Proposed: "the only
    ways in are .escape's, which sends a planar ([font_rn_pl]) or off-grid
    ([font_rn_k7]) run to font_run_scell instead, and kern_small, which is
    1bpp on every machine."
11. **SPEC.md:24756-24758** "So `font_ink` takes its mono path when
    `[vid_mono]` is set **or** the window being drawn has declared two
    colours." - unreachable (text-2). Proposed: "`font_ink` is reached only on
    a 1bpp display, so declaring two colours changes nothing there; the
    declaration is honoured by `font_run`'s planar prologue (below) and by
    `gfx_pen_dis`."
12. **SPEC.md:24766-24768** "§6.1's opaque renderer calls `font_ink`
    explicitly on **every** adapter" - font_run_x inlines the reduction in its
    MONO prologue only (font.inc:1338-1365) and the planar prologue computes
    its own (font.inc:1866-1900). Proposed: "§6.1's opaque renderer reduces
    the inks itself - inline in its mono prologue, and in its planar prologue
    from `[gfx_mono1]` and `[gfx_dis]` - and computes its own checkerboard".
13. **kernel/vga12.inc:31-35** "font_ink takes its mono path here as well -
    reducing the ink to 00h/FFh" - same. Proposed: "font_run's planar
    prologue reduces a disabled run to the checkerboard here as well (SPEC.md
    11.96.17.1); font_char's VGA writer does not read it".
14. **kernel/wm.inc:6864-6866** "This is the answer for the two primitives
    that cannot draw a partial shape: font_char's 8x8 cell and ico_core's
    icon body" - font_char uses `wm_clip_rows`. Proposed: "This is the answer
    for the primitives that cannot draw a partial shape - ico_core's icon
    body, and a whole font_run - and for the cull's overlap test;
    font_char cuts its cell by rows through wm_clip_rows".
15. **apps/os88api.inc:241** "there are five kinds" - SPEC 6.6.2 lists six.
    Proposed: "there are six kinds".
16. **apps/os88api.inc:5189-5193** "Off that path it falls back to
    fill+FONT_STR, so it is never slower than doing it by hand" - VGA
    (6.1.10) and unaligned (6.1.11) are single passes too; the pair is only
    `.slow`. Proposed: "VGA (when one colour's planes are a subset of the
    other's, SPEC.md 6.1.10.1) and an unaligned x (6.1.11) are single passes
    too; any other pair falls back to fill+FONT_STR, so it is never slower
    than doing it by hand. The pen ([gfx_color]) is undefined after the call."
17. **apps/os88api.inc:271-274** "out CF = 1 refused (a first byte that is
    neither 1 nor ICON_SOLID, or a record taller than 16)" - also rows = 0
    (icons.inc:100-101) and ICON_SOLID on kern_small (icons.inc:88-91).
    Proposed: "out CF = 1 refused (a first byte that is neither 1 nor
    ICON_SOLID - and on kern_small anything but 1 - or 0 rows, or more than
    16)".
18. **kernel/kernel.asm:6628-6629** "the ROM's own table, or a baked face's
    copy in LOW_SEG" - also the MEM_K_FONT heap copy. Proposed: "the ROM's
    own table, its MEM_K_FONT heap copy on a slow-ROM machine (SPEC.md
    6.0.1), or a baked face's copy in LOW_SEG".
19. **kernel/band.inc:30-31** "What is shared is the thing worth sharing:
    font_glyphs itself." - font_glyphs exists only under BAKED_FONT; the band
    reads `[font_seg]` (band.inc:579). Proposed: "What is shared is the thing
    worth sharing: the glyph table itself, through [font_seg]:[font_base]."
20. **kernel/icons.inc:25-27** "rows are clipped to the screen (0..479) ...
    byte columns past the right screen edge (>= ROW_BYTES)" - the code clips to
    `[vid_ch]` (icons.inc:455) and the 1bpp pass to `[vid_stride]`
    (icons.inc:558, 691). Proposed: "rows are clipped to the screen (0 ..
    [vid_ch]-1) ... byte columns past the right edge ([vid_stride] on the 1bpp
    pass, ROW_BYTES on the planar one) are skipped defensively".
21. **SPEC.md:57406** "`ico_disk32` | library record: 32×32 floppy disk" -
    it is an INDEXED record (IREC, icons.inc:979) drawn by `icon_draw_ix`;
    `icon_draw` would misread it. Proposed: "`ico_disk32` (and its five
    siblings) | INDEXED records over `ico_pool` (§25.7), drawn by
    `icon_draw_ix` only".
22. **SPEC.md:57426-57428** "ORs the shifted mask-row bits into all
    `[vid_planes]` planes at `[vid_rseg]`" - one plane (icons.inc:684-690).
    Proposed: "ORs the shifted mask-row bits into the one plane at
    `[vid_rseg]`".
23. **SPEC.md:57418-57419** "like the menu-bar logo, the one sanctioned place
    for hand-made bitmap data — icons are the second" and
    **kernel/menu.inc:5-6** "an 11x11 hand-authored chip silhouette - the
    os8088 logo, and the one place bitmap data is hand-made" - the logo is
    seven rects now (menu.inc:898-940, 2469). Proposed (SPEC): "Bitmaps are
    hand-authored `dw` rows - the one sanctioned place for hand-made bitmap
    data (the menu-bar logo, which was the other, is seven rects since
    §12.05.1)"; (menu.inc): "the System menu (the os8088 logo, an 11x11 chip
    silhouette drawn as seven rects, SPEC.md 12.05.1)".
24. **kernel/menu.inc:24-25** "The pixels under the dropped menu are saved to
    SAVE_SEG:0" - a per-drop heap claim, `[menu_sseg]` (menu.inc:1858-1892).
    Proposed: "The pixels under the dropped menu are saved to a heap claim
    sized from the menu's rect ([menu_sseg]) and put back on release".
25. **kernel/menu.inc:1732-1734** "MENU_SAVE_KB for exactly as long as a menu
    is on screen" - the claim is sized from the rect (menu.inc:1858-1889);
    MENU_SAVE_KB is the build-time ceiling. Proposed: "a claim sized from the
    rect (at most MENU_SAVE_KB, the build-time ceiling) for exactly as long as
    a menu is on screen". Same at **menu.inc:1894** "It is only 20KB" and
    **menu.inc:2085** "rather than hold 20KB" -> "up to MENU_SAVE_KB".
26. **kernel/menu.inc:1652** "16 at 480 or 348 rows, 11 at CGA's 200" -
    `[vid_popmax]` is the constant MENU_POPMAX = 11 on every adapter
    (viddet.inc:362). Proposed: "MENU_POPMAX (11) on every adapter".
27. **SPEC.md:26767-26777** "nothing bounds its width — `menu_widest` is taken
    as-is ... against the `MENU_SAVE_KB` claim ... A width clamp in
    `menu_popup` is the fix the day that stops being true" - `menu_widest`
    clamps to MENU_MAXW for both droppers (menu.inc:2452-2457) and the claim
    is sized per rect. Proposed: "`menu_widest` clamps every menu, popup or
    bar, to `MENU_MAXW` and `menu_trunc` cuts the text to fit, so a popup is
    bounded exactly as a bar menu is; the save-under is claimed per drop,
    sized from the rect, under the `MENU_SAVE_KB` ceiling."
28. **SPEC.md:26452-26453** "`menu_drop` draws the item through
    `gfx_pen_dis` ... and `font_ink` masks a flagged glyph" - menu_drop calls
    `thm_pen_dis` and letters through `font_run_x`, whose §6.1.12 mask is the
    checkerboard (menu.inc:1950-1985). Proposed: "`menu_drop` sets the
    disabled pen through `thm_pen_dis` rather than storing `CDGRAY`, and
    `font_run` folds §6.1.12's checkerboard into the run's own mask".
29. **kernel/menu.inc:325**, **kernel/fprog.inc:1101-1102**,
    **SPEC.md:26266** "an 88px widget" / "an 88px strip" / "changes an 88px
    span" - FPG_W is 80 since FPG_GAP went to 0 (fprog.inc:59-69). Proposed:
    "80px".
30. **REFUTED (see Verification).** **kernel/fprog.inc:1033** "AX = whole units (<= 256, so the quotient" -
    (65,535 + 511) / 512 is at most 128. Proposed: "(<= 128, so the
    quotient".
31. **kernel/fprog.inc:1145-1146** "locked or not (the file calls' own rule;
    fpg_arm banks the cursor debt lock-free) (fpg_stepb's rule)." - two
    parentheticals spliced. Proposed: "UI task, a callback or a wake, locked or
    not - the file calls' own rule (fpg_arm banks the cursor debt lock-free)."
32. **kernel/ascw.inc:420** "napp <= 32: the product fits AX" and
    **ascw.inc:963** "<= 31 * 88" - ASC_NAPP is 90 (assoc.inc:1844). Still
    correct (89 x 88 = 7,832) but the bound is wrong. Proposed: "napp <=
    ASC_NAPP (90): 90 x 88 fits AX" and "<= 89 * 88".

## Open questions

1. text-1: is there a shipped UI-task handler that draws through an armed
   region AFTER file I/O in the same hold? If none, the hole is latent only -
   still worth the 13 bytes because the next one will not know.
2. text-7: does the owner want mono-heap bytes at a +10 cost on the VGA
   budget? The answer generalises to every per-run VGA arm.
3. text-15/16: are new item markers and a run style "contract additions"
   (abilock without a bump) or flag-day material? They break nothing already
   built; only a new package on an old kernel mis-renders.
4. text-3: document the pen as undefined, or restore it? Kernel callers all
   set their pen first today; the question is the SDK's promise.

## Verification

An adversarial pass re-read the cited code on both kernels (the `KERN_SMALL`,
`GFX_VGA`, `VID_EXT`, `DOCK_OPT`, `SEAMCUT` and `FPG_PARK` arms), along with
`build/kernel.map`, `docs/plans/ABI-BREAK-LIST.md` and
`docs/plans/KERNEL-BYTE-QUEUE.md`. Neither plan lists any finding below.

- **text-1: confirmed.** `dkx_gfx_hole_arm` re-seeds `wm_clip_tab` under
  `fpg_bare`'s zeroed count, and the pop puts back a stale count. +13 is
  exact. Banking the hole is safe because the widget never sits under the
  bottom strip.
- **text-2: confirmed.** `vid_depth_set` writes mono/planes as one word
  (1/1, 0/4, 0/8-or-16), so `font_char_bb` only ever runs with
  `vid_mono` = 1. The 14 bytes are exact, and the deletion is even, so
  parity holds.
- **text-3: confirmed.** Only `.whole` and `font_run_scell` store the pen.
- **text-4: confirmed** (low). The pair can also be read torn between the
  two stores.
- **text-5: confirmed** (low). The name-width gap was read, not run.
- **text-6: confirmed.**
- **text-7: CORRECTED.** `fnt_rn_edge`'s VGA arm is about 43 bytes by the
  map, not about 25, so the mono saving is about 185 bytes.
- **text-8: confirmed** (-3).
- **text-9: confirmed** (the clocks are predicted).
- **text-10: confirmed.**
- **text-11: confirmed.** Its impact is still PLAUSIBLE: how `asc_use`
  treats rows for missing packages was not traced.
- **text-12: unchecked.**
- **text-13: confirmed.**
- **text-14: confirmed.** The counts reproduce.
- **text-15: confirmed** (premises). It needs an abilock `--update` for the
  new cell.
- **text-16: CORRECTED.** The `menu_s_msep` saving is -17, not -19.
  `files.inc`'s `fm_s_fsep` is a third kernel separator.
- **text-17: unchecked.**
- **text-18: unchecked.**
- **text-19: unchecked.**
- **Doc mismatches: 32 confirmed, 1 REFUTED.**
  - **No. 30 (fprog.inc:1033) is REFUTED.** "<= 256" is a true, loose
    bound, so it is not a mismatch. The routine's header already says 128.
  - **No. 23** was quoted inexactly. The real text is "(like the menu-bar
    logo, the one / sanctioned place ...)".
  - **No. 8's** fallback is `.whole`, not `.slow`.
  - **No. 10** was reworded. `.escape` sends planar and off-grid runs to
    `font_run_scell`, so they never reach `font_run_cell`.
  - **No. 28** became two edits, because the same paragraph's "A width
    clamp in `menu_popup` is the fix ..." is stale too.
  - **No. 29** also fixes SPEC.md:26266. fprog.inc:1102 is left alone,
    because it is history told in the past tense.
  - **No. 33** also fixes ascw.inc:963.
  - **Three new mismatches were added:**
    - font_ink's `out:` line.
    - SPEC 11.96.17.1's "`font_ink` clears `[font_dith]` on the VGA path".
    - icons.inc:549, `ico_core_bb`'s "all four planes".
  - The two OSAPI contract-comment edits in apps/os88api.inc (FONT_RUN and
    ICON_DRAW) change the abilock fingerprint. Applying them needs
    `python3 tools/abilock.py --update "<why>"`, with no `PKG_FMT` bump.
  - That makes 38 doc fixes in all, in data/text.json.

