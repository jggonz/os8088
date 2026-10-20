# ksp10 agent "new" - fdlg.inc, desk.inc (and #230's review fixes)

Base `833f13e4`. `kernsize` deltas against the blessed baseline.

| | kern_big | kern_small |
|---|---:|---:|
| resident (all `.cold`) | **-107** | **-10** |
| FDLG.DRV image (kern_small, not resident) | - | 1,256 -> **1,246** (-10) |

## #230's review fixes - what came back

* **e890eb7, desk half (`desk_zones_r_x`): ~117 bytes added, 74 back**
  with the defect still fixed (same predicate, same bound, same zone order,
  same mask out). See TAKEN 1.
* **7cd330e (fdlg): +73 on kern_big.** Of its hunks: `fdlg_grab`'s answered
  test (7 bytes) is gone into the gate, its tail is 1 shorter than the
  squash's (`neg ax / jnz / cmc`), and `fdlg_h_sel`'s edge test is 2
  shorter. The gate grew 6 to carry the answered case, and that FIXES a
  defect the review fix left (BUGS below). `fdlg_btn`'s act test (7),
  `fdlg_drawbtn`'s record (kern_small, FDLG.DRV only) and `fdlg_gdis`
  (2 bss) stand - each is the fix itself. Net on the fdlg hunks: about
  -4 of +73, plus the fix; the rest of fdlg's -33 is elsewhere in it.
* **The squash's 2 bytes in `fdlg_grab`:** the `cmc` shape is back, but
  with main's new branches it is 1 byte (the beep path's CF=1 comes from
  `neg ax`, AX being the chooser and never 0) - plus the 7 of the act test
  the gate now answers.

## TAKEN

1. **desk_zones_r (e890eb7): kern_big .cold -74** (commit 939f212d)
   * The mask is a 33-bit RING with CF: `rcl [m]; rcl [m+2]` rotates the
     last verdict in and the next zone's bit out in one step, and 33 steps
     put every bit back - no register copy, no rebuild, no up-shift loop.
     Bits past DESK_NZ (nobody's) go back 0. Checked against the old code in
     a model, 20,000 random masks and verdicts.
   * SI -> wm_cl_ox1 (compares `[si+d8]`), DI the ring counter.
   * The bound walk ALSO answers "is any of it revealed?" (DH = fragments
     overlapped) - the same four compares as wm_clip_walk's swapped-corner
     overlap - so the `ct_cw_mem_disp` far call to it is gone.
   * One X/Y body run twice (BX, SI two bytes on, BP bit 15 the pass).
   * `cmp bp, 0x100 / adc dx, bp`, BP = 0xFF + pieces: DH + 1 and
     DL + max(0, pieces - 1) in two instructions.
   * The subtraction's CF is not read: the count bounds wm_clip_subr's peak
     (each overlapped fragment split once, into at most the pieces counted;
     nothing appended overlaps the zone), so it cannot overflow.
2. **fdlg: the gate counts an ANSWERED chooser as gone: kern_big -1**
   (2da1ad07, a bug fix - BUGS below). `fdlg_reap` takes the answer before
   asking (AH; the gate clobbers AL), `dec ah / jz .commit`.
3. **fdlg: kern_big -11, FDLG.DRV -5** (055c583b): grab's tail
   (`neg ax`/`jnz`/`cmc`); h_sel compares DI with [fdlg_gdis] in memory;
   kern_big's `fdlg_hook` is `fdlg_hook_x`'s own head (one caller: a call
   and a ret); `cbw` for `mov ah, 0` in the dispatch (FDH_* < 128);
   fdlg_open stores mode and act=0 as one word.
4. **desk: kern_big -16, kern_small -10** (0d3057eb): desk_draw_zone ends in
   `kretc_es` (both arms; kern_small banks DI for it); desk_zone_whole
   draws first and ends in `kretc_dx`; desk_owner's zone = DESK_NZ - CL;
   desk_cell_xy `cwd` + two `xchg`; desk_caprect one `lea`; desk_rowcalc
   `div bx`; desk_zones_r banks SI instead of reloading it.
5. **kern_big -5, FDLG.DRV -3** (8f068f61): desk_reflow's two 0xFF stores
   are one each pass; `fdlg_live` (the live-record test, twice).
6. **kern_big -1, FDLG.DRV -1** (6736d487): the gate's both-bits test is
   `inc ax / and al, 3` ((flags + 1) & 3 = 0 iff both set); the gate
   clobbers AX, so fdlg_reap holds the answer in DL.

## RUN

* `make`, `make small`, `make emu` (kern_emu .cold -108, assembles), the
  fast tier 61/61 after every batch.
* `stkbalance` over kernel.asm + desk.inc + fdlg.inc, base vs tip: no path in
  either file in either report (10 = 10, all kernel.asm's own ladder).
* Soak, one at a time: `deskzoom` (desk_zones_r's refusal), `deskflash`,
  `fdlgchoose` (resident chooser end to end), `fdlgchsmall` (FDLG.DRV) -
  4/4 green.
* No row exercises the answered-chooser KEY fix; `fdlgchoose`'s drain test
  is a press. A row typing Backspace after a double-click commit in the same
  drain would be its gate.

## REFUSED

* **desk_cbit with `rol ah, cl` and no `and cl, 7`: -3 bytes, refused.**
  A rotate by CL is 8 + 4n cycles with no masking on the 8086, and
  desk_zones_paint walks all 63 cells through desk_cbit on every desktop
  click on kern_big (desk_select) - ~8,000 cycles (~1.7 ms) a pass for 3
  bytes.
* **Opcode-swallow tricks** (`db 0xB4` to skip a `stc`, -1 in
  osapi_desk_item_x and similar): nothing in the kernel does it, and the
  source-reading gates (stkbalance, ovlchk) would misparse.
* **desk_col's row loop toggling AL instead of testing DX's parity**: +1.
* **A conservative bound in desk_zones_r (n + 3 per overlapped fragment):**
  ~25 bytes, but it refuses zones the exact count fits, each a double-draw
  over the dither - a behaviour (cost) change.
* **Skipping every hook in an answered chooser at the dispatcher** (to drop
  fdlg_btn's act test): FDH_RECT and FDH_ARM still have to answer for a
  repaint before the reap.
* **cwd in desk_rowcalc**: the room can be negative on a screen too short,
  and DX = FFFFh would turn the unsigned divide into a #DE.

## BUGS FOUND

* **FIXED (2da1ad07): a key reached an ANSWERED chooser.** SPEC.md 38.2 is
  binding: "From the moment [fdlg_act] is non-zero ... fdlg_top sends a key
  to no window at all." #230's review fix taught fdlg_grab the answered case
  with a test of its own and left fdlg_top asking only the gate, so a key
  later in the same drain still reached it - Backspace/Enter on a folder
  moved where a posted commit is made, arrows + Enter re-staged the name,
  Escape overwrote a commit with a cancel. Now the gate says "gone" for it.

## CROSS-FILE

* `kernel/wm.inc` wm_dmg_gray: the `jc .whole` straight after
  `call COLD_SEG:desk_zones_r_x` (~6233) is dead - desk_zones_r_x never
  answers CF=1 now. **2 bytes of `.text`**, kern_big. Its comment (and
  `.whole`'s "a zone that would overflow it is refused by desk_zones_r's
  own count") want the same edit.
* `kernel/apps.inc:766` is `ui_krect4`'s ONLY caller (ui_krect4 is in
  fdlg.inc, `.cold`, 16 bytes): write its four stores at the call site and
  delete it - **-4 bytes** (call + ret), both kernels. Needs both files.
* `docs/INDEX.md`: regenerated on this branch for this notes file; it will
  conflict with every other agent's - regenerate it at the merge
  (`tools/os88index.py`).
