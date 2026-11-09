# Window manager and damage/redraw

Big review, 2026-11-08, branch `the-big-review` (cut from `elendilon`).
Reviewer key: `wm`. Read-only: nothing was built, booted or measured; every
cycle figure below is 8088 instruction-table arithmetic and says so.

## Scope read

| file | lines | how |
|---|---:|---|
| `kernel/wm.inc` | 12,041 (5,806 non-comment) | every proc's code, comment-stripped, 1-11,807; comment blocks read in full for the record layout, `wm_pkgcall`, `wm_fit`, the natural/zoom banks, `wm_create`, the sizing cells, the close/wake machinery, `wm_resize`/`wm_rz_*`, `wm_zoom`, `wm_destroy`, the `wm_lk` doors, `wm_show_b`/`wm_raise`/`wm_front_b`/`wm_hide_b`, the snap family, the whole damage engine (`wm_dmg_*`, `wm_paint_dmg`, `wm_dmg_wins`, `wm_dock_under`, `wm_paint_all`), the whole clip engine (`wm_clip_*`, `wm_covered`, `wm_dmg_gray`, the occlusion walk), `wm_ptr2idx`/`wm_idx2ptr`, `wm_draw_win`, `wm_su_owed`/`wm_chrome_clip`/`wm_cov_rect`/`wm_su_take`/`wm_su_drop`, the zoom animation. The raise cache's inner fragment code (`wm_su_flay` .. `wm_su_try`, ~1,500 lines) was read for interfaces and invariants, not instruction by instruction |
| `kernel/band.inc` | 840 | all of it (a knob: `BAND=1`, no shipped kernel assembles it) |
| `SPEC.md` §11 | 18,311-18,560, 18,560-18,700, 18,920-19,062 (11.3), 24,923-24,990 (11.97) | read; the rest of §11 (to 26,000) grepped per claim |
| `REDRAW-SPEC.md` | 628 | outline + the present-tense passages |
| `PERFORMANCE.md` Part 5 | 1,209-1,378 | the window rows |
| cross-reads | `kernel/ui.inc` (timer scan, `ui_grow` around `wm_ask_size`, drag's `npro`), `kernel/instance.inc` (`inst_wchk`), `kernel/memory.inc` (`mem_free_x`), `kernel/dockmod.inc` (`dkx_band`), `kernel/vga12.inc` (gfx lock words), `kernel/kernel.asm` (build arms, KHP knobs, section layout), `apps/os88api.inc` (wm cells), `apps/paint` (`pt_onsize`) | |
| plans | `ABI-BREAK-LIST.md`, `KERNEL-BYTE-QUEUE.md`, `LAST-DROP-PERF.md` 7.1, `khp1/LOG.md` wm rows, ksp10/17/18 wm notes | for `listed_as` |

Build arms, as the code has them: `DOCK_OPT` is defined exactly when
`KERN_BIG` is (kernel.asm:1263), so every `%ifdef DOCK_OPT` is a kern_big arm;
kern_emu and kern_mega are kern_big plus; `KHP_DMGSTALE`, `KHP_RAISEOL` and
the other wm KHP knobs are **kern_mega only** (kernel.asm:283-294);
`BANDCOMP` is no shipped kernel.

## Verdict

The window manager is the most carefully *measured* subsystem in the tree, and
the model under it - synchronous painter's-algorithm repaint, one bounding-box
damage rect, a 16-rect clip list, record pointers as the ABI - has been pushed
remarkably far by making each operation repaint only what it provably changed
(show/raise reveal nothing, hide/destroy/drag repaint a rect, the dither skips
ground a window will cover, vacated-rect and shadow-only owing, a raise cache
with explicit promises). Part 5's budget rows are real and the code honours
them. The weaknesses are structural rather than local: the damage pass still
needs a transitive "redraw everything above a marked window" closure because
lower windows are not clipped while they draw, so the exactness won at the
dither is spent again at the windows; one-shot arguments to `wm_paint_dmg`
are consumed only on the non-empty path; package-supplied window pointers are
validated at some doors and not others, and the one routine everything funnels
through (`wm_ptr2idx`) can raise INT 0 on a bad pointer, which on an IBM ROM
masks every interrupt. The source itself has become a hazard: 52% comment,
much of it history ("kernel size pass 15, -2"), and at least a dozen comments
describe the pre-pass-4 side tables or a retired `wm_fast_ok` as current.
Nothing here wants a big rewrite; the highest-value work is small and
mostly ABI-free.

## Strengths

1. **`wm_pkgcall` is the one dispatch site for every window callback**
   (wm.inc:396-505). Re-entrant because the far pointer is read from the
   record, segment-agnostic (W_SEG = 0 is a kernel window through `wm_cbd`),
   and the package-side `call bp / retf` dispatcher means no package author
   writes `retf`. Per-callback pen (`gfx_b1ink`) and depth claim (`gfx_mono1`)
   are banked on the stack, so nested paints inherit nothing. Copy this shape
   for any future callback kind.
2. **Per-slot state lives in the record and is zeroed when the slot is TAKEN**
   (wm.inc:93-127, 1418-1431). One `rep stosb` from W_MENUS to the end of the
   record kills the whole "stale handler in a reused slot" class by
   construction; `wm_destroy` owes only `W_WKQ`. The `%error` guards on the
   layout (wm.inc:124-166) make the invariants assembler-checked rather than
   prose.
3. **The damage engine's exactness tricks are sound and cheap**:
   `wm_dmg_gray` dithers the damage MINUS every visible frame
   (wm.inc:6242-6375), phase-correct because the dither is absolute-x+y; the
   vacated-rect test (`wm_dmg_stale`, 4937) stops a drag across a window
   repainting it; `wm_dmg_shad` owes a drop shadow as two lines, not a repaint
   (5366-5380, 5444-5465); per-display desktop bands are clip SEEDS so one fill
   serves both displays (814-962).
4. **"Derived, never tracked"** for the zoom state (wm.inc:2862-2873) and the
   self-healing `wm_tarm` flag (wm_timer, 2049-2059): state that can only be
   stale in the safe direction.
5. **One-shot arguments cleared by the CONSUMER** (`wm_dmg_dk`, `wm_dmg_stwin`,
   `wm_su_son`, wm.inc:5468-5481): a caller that forgets inherits the safe
   default. (Finding wm-3 is the one gap.)
6. **The lock doors** (`wm_lk`/`wm_lk_go`, 3268-3352): a recursive-aware take
   shared by show/hide/front/resize, so the SDK's "call it with or without the
   lock" is true for those four.
7. **Preference -> clamp -> floor is one sequence** (`wm_apply_wh`,
   `WM_REFIT`, `wm_fit`'s floor-last), so the three sizing voices cannot be
   composed differently in two places.
8. **The raise cache's promise bits are explicit** (`WF_SAVEU`, `WF_1BPP`,
   `WF_STALE`, `WF_OWNBG`): the application states the fact only it knows, and
   the kernel never guesses. `wm_clip_set` drops a drawer's cache only after
   the occlusion walk (6100-6123), so a covered window keeps the save-under it
   exists for.

## Findings

Ranked by value. Bytes are signed estimates from instruction encodings, not
assembled spans; "resident" is `.text`/`.bss`/`.cold`.

### wm-1 (hazard, high) `wm_ptr2idx` raises INT 0 on a wild window pointer, and its comment says it is bounded
`wm_ptr2idx` (wm.inc:7278-7301) does `sub ax, wm_wins / div cl` with CL =
WIN_SIZE. The quotient overflows AL - INT 0 - for any BX below `wm_wins` or
more than 255 x WIN_SIZE (18,870 bytes) above it. instance.inc:1976 and
wm.inc:8280-8292 already record what INT 0 does on a 5150/5160 ROM: the BIOS
stub writes 0FFh to the 8259 mask and IRETs, every interrupt is gone, the
floppy motor stays on. The comment at 7295-7299 claims the clamp makes a
non-window pointer "name the LAST window rather than a byte of .text, which is
wrong and bounded" - true only inside that 18,870-byte window. Package-facing
paths reach it with an unvalidated BX: `OSAPI_WM_SHOW/HIDE/FRONT` ->
`wm_show_b`/`wm_su_precover`/`wm_zabove`, `OSAPI_WM_DESTROY`,
`OSAPI_WM_CLIP_SET` -> `wm_clip_occl` -> `wm_zabove`, the close request.
Only `inst_wchk` (two slots) and `wm_isrec` (saveu, noanim, quiet) validate.
**Recommendation (now):** bound before dividing - `cmp ax, MAX_WIN*WIN_SIZE /
jae .clamp` (+5 bytes, ~+8 clocks a call), or take wm-2, which removes the
divide. Fix the comment either way.
Bytes: +5 `.text` (or see wm-2).

### wm-2 (size/perf, medium) store the slot index in the record; `wm_ptr2idx` becomes a load
Every damage pass calls `wm_ptr2idx` per marked window (`wm_dmg_bit`, 4871)
and `wm_idx2ptr` per window per walk, plus O(n^2) in the marking's `.mbelow`
loop (5390-5420): `div r8` is 80-90 clocks on the 8088, `mul r8` ~70. A byte
`W_IDX` at the record's end, outside the create-time zero run, written once by
`wm_init` (in `.boot2`/`.ovl`, free), makes `wm_ptr2idx` `mov al,[bx+W_IDX] /
cmp al,MAX_WIN / jb / mov al,MAX_WIN-1 / cbw / ret` - 11 bytes against 19,
no divide, no INT 0 (a wild BX reads a byte and is clamped). WIN_SIZE is not
published (SPEC.md 11), so this is kernel-internal; `tools/os88geom.py`'s
mirror moves with it.
**Recommendation (now):** take it with wm-1. Optional second step (record):
a 12-word constant table for `wm_idx2ptr` trades +20 bytes for ~40 clocks a
call - worth it only if a profile shows the walks.
Bytes: -8 `.text`, +12 `.bss` kern_big (+6 kern_small): net +4 / -2 resident;
~-80 clocks a call.

### wm-3 (bug, medium) `wm_paint_dmg`'s empty-rect arm leaks every one-shot argument to the next pass
`wm_paint_dmg` consumes `wm_dmg_npro` at `.promote` (5285) and
`wm_dmg_stwin`/`wm_su_son`/`wm_dmg_dk` in `wm_dmg_wins.wdone` (5468-5481).
The `.chrome` arm (5307-5313), taken when the clamped rect is empty, reaches
neither. Callers arm before calling: `wm_hide_b` sets `npro` for a desk
window (3735), `ui_drag` sets `npro` (ui.inc:2025) and `stwin`, desk.inc:986
sets `stwin = 1` and `npro`, deskmod.inc:330 sets `npro`. If such a rect
clamps to nothing (a window parked wholly off the live screen, e.g. across a
display removal), the next unrelated pass inherits them: a leaked `npro`
suppresses the next real promotion (the new front window keeps a grey title
bar and no grow box - 11.91.2 makes that permanent), and a leaked
`stwin = 1` makes `wm_dmg_stale` read `[1+W_X]` as a window and wrongly
excuse windows from the marking.
**Recommendation (now):** clear the one-shots on `.chrome` too - route it
through a shared tail that zeroes `stwin`, `su_son` and `npro` (~+8 bytes),
or arm them inside `wm_paint_dmg` from a flags argument instead of globals.
Bytes: +8 `.text`.

### wm-4 (hazard, medium) `wm_ask_size` trusts a negotiator to keep BX, which no contract says
`wm_ask_size` (2465-2481) banks AX, SI, BP around `wm_pkgcall`; its callers
(`ui_grow` ui.inc:2243, `wm_zoom` 2934/2977, `EXT_APPLY` extmod.inc:713)
write `[bx+W_W]` straight after. `wm_pkgcall` restores DS/ES only, and the
general callback rule is "a window callback may clobber AX-DI and ES"
(SPEC.md 98,747, §12.2/§13); neither SPEC.md 11.1 nor `OSAPI_WM_ONSIZE`'s SDK
prose (os88api.inc:3178) says a negotiator owes BX or DI. The one shipped
negotiator (`pt_onsize`, paint.asm:11758) happens to preserve everything.
**Recommendation (now):** `push bx / pop bx` in `wm_ask_size` (+2 bytes), and
say in the SDK that a negotiator answers in CX/DX and may clobber what any
callback may.
Bytes: +2 `.text`.

### wm-5 (bug, medium, kern_big) `wm_snap_ax` moves a window at a LEFT dock's band edge onto the strip
With a left dock (`DOCK_SW` = 32, dock.inc:73), `dkx_band` sets
`vid_band_x0` = 32 (dockmod.inc:409-433). `wm_fit` floors x at that edge and
then calls `wm_snap_win` (657-661), and `ui_drag` snaps a drop the same way
(ui.inc:1803). `wm_snap_ax` (4102-4136) rounds the CONTENT origin down: x =
32 -> content 33 -> 32 -> frame x = 31, which is the strip's rule column
(dockmod.inc's `.left`: rule at across2). It only special-cases x < 7
("jns .fits"). The window's border then overlaps the live strip, so
`wm_dock_clear` (`dock_live_hit`) answers "dirty" and every later show,
raise and dock update pays `wm_dock_under`'s window repaint (SPEC.md 11.90) -
the cost `wm_fit`'s one-pixel rule (619-627) exists to avoid on the bottom
dock. PLAUSIBLE: argued from the geometry, not observed.
**Recommendation (now):** in `wm_snap_ax`, compare the rounded x against the
band's left edge rather than 0 on `DOCK_OPT`, and take the next aligned x up
when it is below (+~6 bytes kern_big).
Bytes: +6 `.text` kern_big, 0 kern_small.

### wm-6 (consistency, medium) `wm_resize` clamps against the union and ignores the side-dock band
`wm_resize_nb` (2498-2560) clamps width and x against `[vid_w]` - the virtual
desktop's width, the union on an extended desktop (viddet.inc:299-307) - and
never reads `vid_band_x0`/`vid_band_xe`, while `wm_fit` (581-669) and
`wm_zoom` (2898-2905) use the band, and `wm_apply_wh` routes multi-display
through `EXT_APPLY`. So `OSAPI_WM_RESIZE` from an app can (a) grow a primary
window to straddle the seam, and (b) push a window over a right-hand or
left-hand dock strip. PLAUSIBLE (no app shown to do it).
**Recommendation (now):** clamp with the same band words `wm_fit` uses
(DOCK_OPT arm, +~10 bytes), and decide whether the union width is intended.
Bytes: +10 `.text` kern_big.

### wm-7 (hazard, medium) `OSAPI_WM_DESTROY` and `OSAPI_WM_FULLSCREEN` still draw without taking the lock
SPEC.md 11.101.2 gave show/hide/front/resize the recursive-aware door
because an entry proc runs with the lock free, and a repaint with no hold
banks the arrow into a save-under (FIELD-NOTES 34.1). `OSAPI_WM_DESTROY`
(kernel.asm:3228, `OSAPI_RSLOT wm_destroy`) repaints through `wm_paint_dmg`,
and `OSAPI_WM_FULLSCREEN` (kernel.asm:2724) through `wm_raise`/`wm_paint_all`,
with no door. The SDK text justifies destroy's requirement by "as
OSAPI_WM_HIDE wants it" (os88api.inc:308), which stopped being true when hide
got its door. `wm_fullscreen` also draws a HIDDEN window whole if called
before `OSAPI_WM_SHOW` (4316-4345: no visible test before `wm_raise`).
**Recommendation (now):** give destroy the `wm_lk_go` door (+6 bytes).
Fullscreen answers in CF, which `wm_lk_go`'s `or al,al` destroys - give it the
door with a `pushf`/`popf` in `wm_lk_go` (+2) or keep the contract and say so;
and refuse (CF = 1) or defer the raise for a hidden window (+~5).
Bytes: +6..+13 `.text`.

### wm-8 (hazard, low) `OSAPI_WM_TIMER` with AX >= 32768 fires at once
`wm_timer` stores `[ticks] + AX` (2061-2079); the scan fires on
`[ticks] - W_TIMER` non-negative (ui.inc:806-809). For AX >= 32768 the
difference is positive on the next pass, so a timer asked for 30-60 minutes
fires within a tick. The SDK contract (os88api.inc:3231) gives no range.
**Recommendation (now):** document "AX <= 32767 ticks (30 minutes)" in the
SDK and SPEC.md 13.9 (0 bytes), or clamp (+6).
Bytes: 0 / +6.

### wm-9 (weakness, low) `wm_su_drop` ignores `mem_free`'s refusal and loses the claim
`wm_su_drop` (9866-9888) zeroes the slot word, then calls `mem_free`;
`mem_free_x` refuses (CF = 1) while a compaction's relocation walk is busy
(memory.inc:4451-4453). The cache claim is then owned and unreachable until
its owner tag is swept. Reachable only if a drop happens inside a relocation
proc (a package proc that calls `OSAPI_WM_CLIP_SET`/`SAVEU`). Low.
**Recommendation (now):** clear the slot only on CF = 0 (+3 bytes), or state
that relocation procs may not call clip/saveu.
Bytes: +3.

### wm-10 (size, low) `wm_lk`'s flag test is redundant
`gfx_unlock` stamps `gfx_lock_own` = 0xFF before it drops the flag
(mouse.inc:5381-5386, vga12.inc:310-312), so "owner == me" already implies
"held". `wm_lk` (3268-3307) tests the flag first and both of its outcomes for
"free" and "other" are the same `wm_lk_take`, so the `cmp byte
[gfx_lock_flag],0 / je` pair can go. (`fpg_arm`, fprog.inc:617-621, partitions
the states differently and needs its test, as `wm_lk`'s comment says.)
**Recommendation (now):** delete the two instructions.
Bytes: -7 `.text`.

### wm-11 (weakness, medium) the XT does not get the redraw knobs that were built for it
`KHP_DMGSTALE` (+38, drags over hidden windows -12 to -32%), `KHP_RAISEOL`
(+90, CGA raise -5 to -14 ms), `KHP_RELIT` (+32, fullscreen exit 781 -> 629
ms CGA) and `KHP_CAPTION` (+34) are defined only for kern_mega
(kernel.asm:283-294), which runs on 286/386 VGA machines where these
milliseconds matter least. They are flicker and visible-redraw wins measured
on the 4.77 MHz XT (khp1/LOG.md:359, 376, 411-412). Meanwhile wm.inc carries
both arms of `wm_dmg_stale`, `wm_raise`, `wm_draw_win` and `wm_cov_rect`
indefinitely.
**Recommendation (now):** decide each for kern_big against its bytes - the
four total +194 resident - and delete the arm that loses; a knob nobody ships
on the target is a second source tree.
Listed as: LAST-DROP-PERF.md 7.1 rows 2, 4, 5, 7. New argument: the target
is the machine that does not get them, and the dual arms are a standing
maintenance cost in the hottest file.
Bytes: +194 kern_big if all four (+38/+90/+32/+34 each).

### wm-12 (weakness, medium) the transitive marking redraws windows that were never damaged
`wm_dmg_wins` marks a window that merely OVERLAPS a marked window below it
(5384-5420), because the lower one is redrawn "WHOLE" - unclipped except by
the cull - and would paint over it. The 11.91.2 vacated-rect and 11.91.8
(`KHP_DMGSTALE`) work exist to keep the first link of that chain short, but
one marked window at the bottom of a cascade still repaints the cascade.
PERFORMANCE.md Part 5's widget-drop row is one instance ("every ordinary
window over its old or new rect redrawn WHOLE"). The marking loop itself is
O(n^2) with a `mul` per inner step (above). See Rethink R1 for the
structural answer; the cheap intermediate is to clip each marked window's
whole draw to its VISIBLE region (the 11.97 chrome clip already does this
for the outline, and the 11.3.3 cull for W_PAINT), and mark an upper window
only when its rect meets the lower window's VISIBLE region intersected with
the damage - which the clip list already computes.
Bytes: unmeasured; kernel-internal; ~+40..+80 `.text` for the predicate.

### wm-13 (weakness, medium) window-pointer validation is inconsistent across the doors
`wm_saveu`, `wm_noanim`, `wm_quiet` (`wm_isrec`), `wm_ownseg`, and two
`inst_wchk` slots validate BX; `OSAPI_WM_SHOW/HIDE/FRONT/DESTROY/CLIP_SET/
BAND/OWNBG/KEEPH/SNAP/SIZABLE/...` do not, and the inline setter cells
(`mov [cs:bx+W_ONCLICK], ax / retf`) cannot. A package passing BX = 0 to an
inline setter writes the kernel's first 74 bytes of `.text`. This is a policy
question rather than one defect: the tree trusts packages (ABI-BREAK-LIST T3's
note) but spends bytes validating at some doors.
**Recommendation (now):** write the rule down (SPEC.md 11): either "BX is
trusted at every wm door; `wm_isrec` exists for worker-reachable ones" (and
remove the inconsistent ones, ~-12 bytes), or validate at the RSLOT doors via
one shared prologue. wm-1/wm-2 make the funnel itself safe, which is the part
that matters for an XT.
Bytes: -12..+30 depending on the rule.

### wm-14 (rethink, low) duplicated DOCK_OPT source with dead inner arms
`wm_dmg_bands` and the 60-line natural-bank banner exist twice (813-962 and
963-1104). Since `DOCK_OPT` == `KERN_BIG`, the `%ifdef KERN_BIG` inside the
`DOCK_OPT` copy has a dead `%else` and the one inside the non-`DOCK_OPT` copy
a dead `%if`. The copies differ by six lines (the side-band clamp).
**Recommendation (now):** one body with a `%ifdef DOCK_OPT` around the six
lines. 0 bytes on every kernel, -~140 source lines, and one banner to keep true.
Bytes: 0.

### wm-15 (weakness, medium) the file narrates its own history, and the narration goes stale
wm.inc is 12,041 lines of which 6,235 are pure comment, and much of that is
the *history* of each instruction ("kernel size pass 15, -2", "was a jump
over the clc", commented-out stores, wm.inc:3113-3150). The section below
lists 21 places where a comment or doc describes a retired design as current
- the pass-4 side tables alone account for eight. The design rationale is
valuable; the per-pass byte ledger belongs to `git log` and the KSP reports,
which already carry it.
**Recommendation (now):** a comment pass: keep contracts, invariants and the
"why", drop pass ledgers and commented-out code; fix the mismatches below.
Bytes: 0.

### wm-16 (weakness, low) the content white-fill is still the default double-draw
`wm_draw_win` fills the content white before W_PAINT unless `WF_OWNBG`
(11017-11030) - exactly the erase-then-draw pattern CLAUDE.md's rule 2 calls
the canonical violation. 32 of 66 assembly packages and 9 C packages opt out.
This is Windows' `WM_ERASEBKGND` with a default brush, and the opt-out is the
right shape; the remaining packages are the queue.
**Recommendation (now):** treat `OSAPI_WM_OWNBG` as the default expectation in
docs/INDEX.md and the porting skills, and list the non-callers as a ratchet
the way `tests/movable.txt` does. No kernel change.
Bytes: 0.

### wm-17 (consistency, low) one-shot arguments are globals with many lifetimes
`wm_dmg_mine` is cleared by `wm_raise` (3588), `wm_dmg_rzwin` by
`wm_resize_nb` itself (2592-2595), `wm_dmg_dk`/`stwin`/`su_son` by
`wm_dmg_wins`, `wm_dmg_npro` by `.promote`, `wm_dmg_cullq` by `wm_dmg_wins`'s
own bracket, `wm_cvf_on` by `wm_draw_win`. Each was right when written; as a
set they are an implicit calling convention that wm-3 shows can be broken by a
new early-out.
**Recommendation (now):** a single `wm_dmg_args` byte of flag
bits plus the two rect words, cleared in ONE place at `wm_paint_dmg`'s exit.
Bytes: ~-10 (fewer separate clears).

### wm-18 (prior-art/record) the region is a 16-rect list with no coalescing
`wm_clip_split` (6740-6798) replaces a rect with up to four fragments and
never merges neighbours; at 16 it overflows and each caller degrades (skip,
draw-anyway, dither-all). With 12 windows the overflow is reachable on a
busy desktop, and every degraded answer is either a lost repaint (clip_set:
skip the frame) or a double draw (gray: `.whole`). The Y-X banded form
QuickDraw and X11 use keeps the above/below strips full width and merges
equal bands, so a subtraction costs at most two new rects in the common case.
**Recommendation (record):** measure overflow frequency with a counter in
`wm_clip_add`'s `.full` (one rebuild, PERFORMANCE.md rule 4) before changing
anything.
Bytes: 0 to measure.

### wm-19 (size, low, knob) band.inc: the composer costs 1,634 bytes for a bar that is slower on VGA
band.inc:46-63 records 36.5 vs 40.8 ms (Hercules), 37.1 vs 42.0 (CGA) and
38.3 vs 30.3 (VGA) against the primitive bar, for 1,634 bytes. The code read
clean (clipping, polarity, the replay loop). Since `KHP_CAPTION`/wm-3's
one-`font_run_x` caption took most of the double-write out of the default bar,
the composer's remaining argument is the stripes.
**Recommendation (record):** either retire `BAND=1` and its `t_buildmatrix`
row, or keep it explicitly as the reference for a future 1bpp composer; it is
not on any path to shipping as it stands.
Bytes: 0 shipped.

### wm-20 (hazard, low) `wm_anim` yields while holding the gfx lock
`wm_anim` (11433-11488) runs `task_yield` in a loop for every frame of the
zoom with the lock held by its caller (`wm_show_b`). Workers block on the
lock for the whole animation (~WM_ANIM_N x 27.5 ms). It is deliberate (the
XOR outline must not be overdrawn), but it is the one place the WM holds the
screen across a scheduling point, and it is not in SPEC.md 11.99's text.
**Recommendation (now):** state it in 11.99 (0 bytes).
Bytes: 0.

## Rethinks

**R1. Clip the damage pass exactly and drop the painter's-algorithm closure.**
Mac QuickDraw and Windows never needed "redraw everything above a marked
window": every window draws only into its visible region intersected with
the update region, so windows can be drawn in any order and an undamaged
window above is never touched. os8088 is now most of the way there - the
outline is clipped (11.97), W_PAINT runs under the cull region (11.3.3),
glyphs clip by rows on both kernels and by columns on kern_big (11.3.2,
11.3.4), icons by fragment (11.3.5). What still forces the closure is that
the cull rounds a cell OUTWARD and `gfx_blit4`/`gfx_scroll` take no clip
(SPEC.md 11.3 names them as holes). Cost: kernel-internal, no ABI; the bytes
are in making the cull exact (an inward-rounding mode that is safe once the
window above is not going to be redrawn) and in the two blit holes. Benefit:
on a cascade, a drag or hide repaints only the windows whose visible pixels
were damaged - the dominant remaining waste in the damage pass. Phase: now,
behind a knob, measured on MartyPC first.

**R2. Deferred invalidation (Windows `InvalidateRect` / `WM_PAINT`, OS/2
`WinInvalidateRect`, Mac update regions).** Every wm operation repaints
synchronously. A per-window "owed" rect accumulated by damage and drained once
per UI pass (lowest priority, as WM_PAINT is) would coalesce sequences that
today repaint twice - the hide-then-destroy of a task-owned close (which the
empty-rect special case exists for), several `OSAPI_WM_RESIZE` calls from one
handler, a drop followed by an `ONRESIZE`-driven relayout. It also gives
11.98 handlers a natural place to run before the paint. Cost: ~+60-100
resident, and every caller that today relies on pixels being on the glass when
the call returns (the raise cache's take-after-draw) has to be audited. Phase:
record - worth a plan, not a patch.

**R3. Window handles in the ABI (Windows `HWND`, GEOS optr).** Raw record
pointers are why validation is inconsistent (wm-13) and why `wm_ptr2idx` is
dangerous (wm-1). A handle (slot index) validated by one `cmp` at every door
would end the class, but packages read W_X..W_H through `[es:bx+...]` in
hundreds of places, so the break is every package. Phase: record; wm-1/wm-2
get the safety for 5 bytes without it. If R6 (content coordinates, design B)
is ever taken, it is the moment to take this with it.

**R4. One window-field setter** (K17-2/K17-3, listed): the ten handler cells
and seven flag setters as one indexed cell would also give one place to
validate BX. Listed; the new argument is wm-13.

## Prior art

| system | model | what it did better | what os8088 does better | cheap to adopt |
|---|---|---|---|---|
| Mac System 5-7 (QuickDraw, Window Manager) | arbitrary-shape regions; `visRgn` per port, `updateRgn` per window, `BeginUpdate` clips drawing to visRgn ∩ updateRgn; update events coalesced | exact clipping everywhere, so no painter's-algorithm closure and no "erase then draw" for windows above; the update region accumulates across operations | measured per-operation budgets; save-under (Mac had none for windows, only menus); the dither-minus-frames trick is cheaper than region erase | the update-region idea per window (R2); QuickDraw's banded region representation (wm-18) |
| Windows 3.0/3.1 (USER/GDI) | per-window update region, `WM_PAINT` synthesized when the queue is empty, `WM_ERASEBKGND` with `hbrBackground`, `CS_SAVEBITS`, DC clipping by visible region | painting is deferred and coalesced; every GDI call is clipped by the DC so an app cannot draw outside; `IsWindow` validates every HWND | `WF_OWNBG` is exactly `WM_ERASEBKGND`'s opt-out with less ceremony; the raise cache's explicit promise bits (`WF_SAVEU`, `WF_1BPP`) are more honest than `CS_SAVEBITS`' discard-on-any-draw; no message queue cost on an 8088 | handle validation at the doors (wm-13); deferred paint (R2) |
| OS/2 1.x Presentation Manager | as Windows, plus `CS_SYNCPAINT` (paint immediately) vs default deferred, `CS_SIZEREDRAW` (invalidate whole window on size) | lets a window class choose synchronous or deferred painting | the shrink-owes-nothing rule (11.90.3.1) is finer than `CS_SIZEREDRAW` | a per-window "sync paint" flag is how R2 could be introduced without breaking anyone: default sync, opt into deferred |
| GEOS (8088) | window system in the kernel clips every GState to the window's mask region; apps get `MSG_META_EXPOSED` with the exposed region and redraw into it | the closest peer on the same CPU: apps draw through a GState in window coordinates and literally cannot draw outside their window, and exposure is a region, not a bounding box | os8088's primitives are faster per call (no GState indirection) and the save-under is something GEOS did not have | R6 (content coordinates) is GEOS's model; the "exposed region" handed to the app is what `OSAPI_WM_DAMAGE` already approximates for `WF_OWNBG` windows |
| AmigaOS layers.library | simple-refresh (damage list + refresh event), smart-refresh (obscured parts kept in off-screen bitmaps automatically), super-bitmap | the system chooses backing store per layer and keeps it coherent by construction (draws to obscured parts go to the backing bitmap) | os8088's cache is opt-in and memory-aware on a 128KB machine, where smart refresh would not fit | nothing cheap; the coherent-by-construction backing store needs a draw redirect os8088 does not have |
| DOS | none | - | everything | - |

## Doc/code mismatches

Twenty-two, each with the current text quoted exactly.

1. **wm.inc:14** - `window record, 20 bytes, fixed offsets` - the record is
   WIN_SIZE = 74 bytes on kern_big, 65 on kern_small (wm.inc:126, 132).
   Proposed: `window record, WIN_SIZE bytes (74 kern_big, 65 kern_small), fixed offsets`.
2. **wm.inc:18** - `bit4 = content-origin snapped (SPEC.md 11.94)` - bit 4 is
   `WF_NOSNAP`, an OPT-OUT; alignment is the default (wm.inc:205-215,
   `wm_snap_want` 3904-3912). Proposed: `bit4 = WF_NOSNAP, the content origin is NOT kept aligned (SPEC.md 11.94; aligned is the default)`.
3. **SPEC.md:18313** - `Window record, 26 bytes, fixed offsets:` - the block
   below it ends at WIN_SIZE 74/65. Proposed: `Window record, WIN_SIZE bytes (74 on kern_big, 65 on kern_small), fixed offsets:`.
4. **SPEC.md:18445-18446** - `is the single dispatch point for all six near pointers` - it dispatches every callback word in the record: paint, onkey, onclick, onsize, onmouseup, ondrag, ontimer, the size-changed, wake, close, right-click, About and wheel handlers. Proposed: `is the single dispatch point for every callback word in the record`.
5. **wm.inc:501** - `caller, kmain:4291, and zero outbound: it is four `rep stos` runs` - the call is `OVSCALL wm_init` at kernel.asm:5834, and the body is two `rep stosb` runs and one store (wm.inc:520-536). Proposed: `caller, kmain's OVSCALL wm_init, and zero outbound: two rep stosb runs and one store`.
6. **wm.inc:529-536** - `.bss is never zeroed at boot and int 19h keeps the LAST kernel's` / `.bss is never zeroed at boot, so this is not redundant` - SPEC.md 10 (18,051: ".bss arrives zeroed ... stage 2's one contiguous read lands it over the whole section") and kernel.asm:5896 (".bss is zero at boot (SPEC.md 2.5)") say otherwise, and the layout agrees (`.bss nobits vfollows=.text`, `.cold start=COLD_START` past text+bss, kernel.asm:2157-2159, 8691-8693). Proposed: `.bss arrives zeroed (SPEC.md 10); this run is belt and braces and costs nothing, being in the boot blob`.
7. **wm.inc:5997, 8729, 8811, 11685; SPEC.md:18952-18954** - `-f bin zeroes nothing` / `because .bss is not zeroed (-f bin)` - same evidence as 6. The `.text` placement is harmless (zero resident difference), but the reason given is false. Proposed: `in .text by choice; .bss would arrive zeroed as well (SPEC.md 10)` (or, if a real reason exists - a read before the kernel's own load completes - state that one).
8. **wm.inc:930 and 1071** - `the tree disagrees with itself about whether .bss arrives zeroed` - resolved by 6; the sentence becomes untrue once the comments agree. Proposed: delete the sentence (keep "IT IS A BACKSTOP").
9. **wm.inc:919-923 and 1060-1064** - `One entry per window SLOT, a side table rather than four more words in the record` - the bank is `W_NATR` in the record since kernel size pass 4 (wm.inc:120, 1105-1112). Also `wm_destroy clears it`: `wm_create`'s zero run does (1418-1431). Proposed: `Eight bytes in the record (W_NATR, NR_*), zeroed by wm_create's run when the slot is taken`.
10. **wm.inc:2823-2830** - `One entry per window SLOT, a side table rather than four more words in the record` and `.bss arrives zeroed (the overlay note in kernel.asm), so a slot starts empty; wm_destroy clears it again` - the zoom bank is `W_ZOOMR` in the record, zeroed at `wm_create`. Proposed: `Eight bytes in the record (W_ZOOMR, ZR_*), zeroed by wm_create's run when the slot is taken`.
11. **wm.inc:1545-1549** (OSAPI_WM_ONRESIZE) - `A SIDE TABLE, not a word in the record` - it is `W_ONSZ` in the record; the same banner's last paragraph (1551-1554) says so. Proposed: delete the side-table paragraph.
12. **wm.inc:2089-2093** (OSAPI_WM_ONWAKE) - `a SIDE TABLE, not a word in the record ... Cleared by wm_destroy` and (2100) `wm_wkh_slot's `div` writes them` - `W_ONWK` in the record, zeroed at create, and the cell is a flag-free inline store. Proposed: `W_ONWK, in the record, zeroed when the slot is taken; the inline cell writes no flag`.
13. **wm.inc:2112-2116** (wm_onclose) - `a SIDE TABLE, not a word in the record ... Cleared by wm_destroy ... and by wm_init` - `W_ONCL` in the record, zeroed at create. Proposed: as 12, `W_ONCL`.
14. **wm.inc:2139-2152** (OSAPI_WM_ONRCLICK) - `a SIDE TABLE, not a word in the record ... It is in kern_small too: a side table is 24 bytes of .bss` and `wm_ptr2idx's div writes them` - `W_ONRC` in the record. Proposed: as 12, `W_ONRC`.
15. **wm.inc:1457-1469** (wm_create) - `(no size-changed handler either: W_ONSZ was in the zero run. Formerly:) lines up AX is 0 only for a KERNEL window ...` - a garbled fragment of a retired comment. Proposed: `(no size-changed handler either: W_ONSZ is in the zero run above)` and delete the rest.
16. **wm.inc:3066-3069** (wm_destroy) - `then repaints all` - it repaints the vacated rect through `wm_paint_dmg` (3151-3157), as SPEC.md 11's table says. Also delete the commented-out `;   mov word [wm_onwk+si], 0` block (3113-3150), which describes clears that no longer exist. Proposed: `then wm_paint_dmg over the rect it vacated (SPEC.md 11.91)`.
17. **wm.inc:3205-3210** (wm_show) - `wm_front on its own still does the full pass, because a raise there is reached from a click that may also have to UNCOVER something` - `wm_front_b` (4391-4448) draws the title bar only, or a partial raise (AL = 2), and falls to `wm_paint_all` only for a HIDDEN window. Proposed: `wm_front asks wm_obscured first and draws only the title bar, or only what was covered (11.96.10); it takes the full pass only for a window that is not visible`.
18. **wm.inc:3461** (wm_raise) - `has already passed wm_fast_ok` - there is no `wm_fast_ok` (it is named only in retirement notes at 4333, 5540, 7712). Proposed: delete the clause.
19. **wm.inc:7271 and 7295-7299** (wm_ptr2idx) - `see the comment on the `xor ah, ah` below` (it is a `cbw`), `wm_su_segs, wm_su_ext, the size preferences` (only `wm_su_segs` is still a side table), and `A pointer that is not a window then names the LAST window ... which is wrong and bounded` (true only within 255 x WIN_SIZE above `wm_wins`; else INT 0 - wm-1). Proposed: `see the cbw below`; `every per-window table still outside the record (wm_su_segs)`; and either fix the code (wm-1) or say `bounded for an offset under 255*WIN_SIZE; any other pointer raises INT 0`.
20. **apps/os88api.inc:307-308 and kernel.asm:3228-3229** - `the gfx lock is held, as OSAPI_WM_HIDE wants it` / `is held, exactly as OSAPI_WM_HIDE wants it` - `OSAPI_WM_HIDE` takes the lock itself since SPEC.md 11.101.2 (`wm_hide` -> `wm_lk_go`, wm.inc:3321-3324); `OSAPI_WM_DESTROY` does not (wm-7). Proposed (if wm-7 is not taken): `HOLD the gfx lock - unlike OSAPI_WM_HIDE, which takes it itself, this cell does not`.
21. **SPEC.md:19049-19052** (11.3 rule 3) - `Clipping is for background tasks only. ... the repaint path must stay unclipped.` - the damage pass now arms regions inside the repaint: the 11.3.3 cull around W_PAINT (`wm_draw_win` 11036-11043), the 11.97 chrome clip (`wm_chrome_clip` 9034, called at 10899 and from `wm_dmg_wins` at 5472), and the title veto (10959-10964). Proposed: `3. Background tasks clip; the repaint path clips only where §11.3.3 (the cull) and §11.97 (the chrome) say it may, each inside its own bracket. A painter that arms a region must not call wm_paint_all inside the same lock hold.`
22. **REDRAW-SPEC.md:38-40 and 399-401** - `there is no "do not erase me" flag. This is still true — Part 1 did not change it.` and `Part 1's point 1, still true, no opt-out flag` - `WF_OWNBG` (`OSAPI_WM_OWNBG`, SPEC.md 11.90.1, wm.inc:11018) is that flag, Paint sets it (paint.asm:630), and `OSAPI_WM_DAMAGE` (os88api.inc:5826) is "here is the rect you owe" (REDRAW-SPEC.md:148-150 says no such API exists). Proposed for 38-40: `This was true when Part 1 landed; SPEC.md §11.90.1's WF_OWNBG made the fill opt-out since, and OSAPI_WM_DAMAGE (§11.96.6) hands an OWNBG window the rect it owes.` Similar one-line notes at 145-150 and 399-401, and the table's Part 3 row should say what remains.

(SPEC.md 11's "Callback billing (binding). All three dispatch sites" paragraph
also predates `ui_bill`'s wider use - ONMOUSEUP, ONDRAG, ONTIMER, ONWAKE go
through it - and is worth re-reading in the same pass; not counted above
because I did not trace every site.)

## Open questions

1. Is `.bss` really zeroed on every boot path, including the `kernel.sys`
   LZ unpack (§2.9.13), the ROM build and a hibernate resume? kernel.asm and
   SPEC.md 10 say yes; four wm.inc comments and SPEC.md 11.3 say no. The
   answer decides wording only (the `.text` placements cost no resident byte),
   but the tree should say one thing.
2. wm-6: is `OSAPI_WM_RESIZE`'s use of the union width (`[vid_w]`) on an
   extended desktop deliberate?
3. wm-13: which validation policy does the owner want at wm doors - trusted
   everywhere, or one shared prologue?
4. R1: how often does the cull's outward rounding actually force a redraw of
   an upper window? A counter in `wm_dmg_wins`' `.mbelow` hit (one rebuild,
   MartyPC) would size the rethink before anyone builds it.
5. wm-18: how often does the 16-rect list overflow on a 12-window desktop?
   Same instrument, in `wm_clip_add`'s `.full`.

## Verification

Adversarial pass, 2026-11-08: every bug/hazard and every high/medium finding
re-read against the code (both arms where they differ); low non-bug items
spot-checked. Corrections are applied in `data/wm.json`.

- **wm-1** corrected - real; the 8-bit quotient overflows for `BX - wm_wins >= 256*WIN_SIZE` (18,944 / 16,640), not 255x; OSAPI_WM_DESTROY reaches the div twice unvalidated (`wm_su_drop`, then its own call).
- **wm-2** corrected - sizes check (19 -> 11 bytes); `.mbelow` calls `wm_idx2ptr` only per MARKED lower window, not per pair.
- **wm-3** corrected - real for `wm_dmg_npro` and `wm_dmg_stwin`; `wm_dmg_dk` is cleared at entry with `wm_dmg_fs`, `wm_su_son` is armed inside the pass. A leaked stwin does not read `[1+W_X]`: desk's armed rect is empty, so every window is excused from marking in the next pass (a missed repaint).
- **wm-4** confirmed - the clobber rule is SPEC.md:26382, not 98747.
- **wm-5** confirmed - also hits `wm_zoom`'s standard rect via `wm_resize`'s snap; argued from code.
- **wm-6** confirmed.
- **wm-7** corrected - severity low, kind consistency: both cells REQUIRE the lock in the SDK; the defect is destroy's stale "as OSAPI_WM_HIDE wants it" (fixed in the doc fixes). Hidden-window fullscreen draw plausible by reading.
- **wm-8** confirmed.
- **wm-9** corrected - SPEC.md 66.3 author rule 3 already forbids a relocation proc to draw or free; robustness only.
- **wm-10** confirmed - -7 bytes.
- **wm-11** confirmed - already in LAST-DROP-PERF.md 7.1.
- **wm-12** corrected - the mul and overlap test run per marked lower window only.
- **wm-13** confirmed.
- **wm-14** confirmed.
- **wm-15** confirmed (spot-checked).
- **wm-16** corrected - the package counts did not reproduce (~29 package directories name OWNBG).
- **wm-17** confirmed (spot-checked).
- **wm-18** unchecked.
- **wm-19** confirmed.
- **wm-20** REFUTED - SPEC.md 11.99 (line 25441) already says the lock is held across the wait and the wait yields.
- **wm-R1** confirmed (the named holes exist; a proposal, unmeasured).
- **wm-R2** unchecked.
- **wm-R3** unchecked.

Doc/code mismatches (numbering as above):

- 1-9, 12-22 confirmed and written as 28 exact edits in `data/wm.json` (several items split into two edits; item 22's "145-150" is about §11.3 rule 3, not OWNBG, and is fixed to match the new rule 3; item 22's `OSAPI_WM_DAMAGE` cross-reference is §11.90.2, not §11.96.6). `.bss` really is zeroed: `COLD_START` follows `KTEXT_SIZE + KBSS_SIZE` so the gap is file padding (kernel.asm:8691), `boot/osrom.asm` zeroes it on the ROM build, and SPEC.md 10 and 30 rely on it.
- 10 (wm.inc:930/1071, "the tree disagrees with itself about whether .bss arrives zeroed") REFUTED as a fix - the sentence is still true: dozens of "-f bin zeroes nothing" reasons remain in SPEC.md and other kernel files.
- 11 (wm.inc:919/1060, the natural-bank "side table" banner) confirmed stale, but NOT emitted as an edit: the two copies are byte-identical for ~80 lines either side, so no unique anchor exists. Apply it with wm-14's merge of the two `wm_dmg_bands` copies.
