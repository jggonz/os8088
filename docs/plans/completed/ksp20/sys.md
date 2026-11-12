# Kernel size pass 20: sys

The sys agent's record. Files: `mod.inc`, `driver.inc`, `memory.inc`,
`instance.inc`, `sched.inc`, `hiber.inc` (resident half), `hbstub.inc`,
`hbstage.inc`, `hbmark.inc`, `shutdown.inc`, `serial.inc`, `clock.inc`,
`clockw.inc`, `cpudet.inc`, `xmem.inc`, `snd.inc`, `toast.inc`, `events.inc`,
`clip.inc`, `apps.inc`, `kernel.asm` (the API table, the door, `kmain`, the
shims) and `boot/`. Branch `ksp20-sys`, cut from `9c785cf3` (the brief).
TAKEN: no file outside that list was edited.

Every figure is ASSEMBLED: `tools/kernsize.py --json` on each kernel's own
build, and `tools/os88rdiff.py kernel 9c785cf3 .` (and `-D KERN_SMALL`) per
routine. No estimate is quoted as a saving.

## BASE AND TIP

Resident is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: plus `.mega`, unchanged at 24,688). The kern_big and kern_small
bases were measured in this worktree and reproduce BRIEF.md 0.2 exactly; the
kern_emu and kern_mega bases are BRIEF.md 0.2's.

| kernel | base | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 90,086 | **90,019** | **-67** | `.text` 39,345 -> 39,306 (-39), `.bss` 4,892 -> 4,888 (-4), `.cold` 37,933 -> 37,909 (-24) |
| **kern_small** | 55,976 | **55,902** | **-74** | `.text` 29,112 -> 29,059 (-53), `.bss` 2,843 -> 2,839 (-4), `.cold` 21,153 -> 21,136 (-17) |
| kern_emu | 90,332 | **90,265** | **-67** | `.text` 39,591 -> 39,552, `.bss` 4,892 -> 4,888, `.cold` 37,933 -> 37,909 |
| kern_mega | 128,895 | **128,844** | **-51** | `.text` 44,929 -> 44,897, `.bss` 4,930 -> 4,926, `.cold` 48,412 -> 48,397, `.mega` 24,688 unchanged; conventional (`.text`+`.bss`+`.lowbss`+`.vgabuf`) 55,795 -> 55,759 (-36), the rung still 61,440 of 68,096 |

**kern_small's `accrued` line at the tip: `kernel 94/512 (18%)`** (base
168/512): 74 of the 169 the rung wants. `KERN_SIZE` is unchanged on every
kernel (no rung crossed here alone). `.lowbss`, `.vgabuf`, `.vga`, `.boot2`
and `BOOT2_SECS` did not move. **No module image grew**: CTRL.DRV -2 (its
settings stage reads `[sch_coop]`, item 23), and the boot overlay's twin of
the same macro -2 (`ovc_pack`, not resident). No `int 13h` call was added or
removed anywhere: the one disk path touched (`drv_load_at`, item 17) only
keeps a register banked across the read it already made.

`t_hotpar` is green on both kernels at every commit. **Four parity pads went**,
each paired with an odd real byte on the same side of the pinned routine
(items 6, 10, 12, 16); the routines between a removed pad and its partner are
UNPINNED code now at an odd shift (an 8086/V20/286 cost only; the 5150's
8088 does not see parity).

## TAKEN

Bytes are kern_big / kern_small (kern_emu = kern_big; kern_mega said where it
differs). "Hot" is BRIEF.md 1's speed rule. `sch_isr`, `sch_switch` and the
door's dispatch are byte-identical and at their measured parity.

### kernel.asm

1. **The door's out-of-range index refuses through XMEM_SEL's own row**
   (-7 / -7, kern_mega 0). `.ref` spelled out the refusal (`popf` / four pops
   / `mov ax, 1` / `stc` / `retf 2`, 11 bytes); on every kernel but kern_mega
   rd_tab already holds `api_refuse` (RS) at XMEM_SEL's index, so `.ref` is
   `mov bl, RD_P + rdp_XMEM_SEL` / `jmp short .in` and the RS kind answers
   CF = 1, AX = 1, every other register and DS kept - the same answer. **The
   hot path is untouched**: `cmp bl, rd_n / jae .ref` stays not-taken, the
   first 13 bytes (dispcold's +13) are identical. A `%error` holds the
   borrowed index inside the RS block. kern_mega keeps its own `.ref` (its
   XMEM_SEL is a real body, and its FSX bracket patches `.lim`'s immediate).
2. **The stack-overflow panel's pen and digit live in `api_name`** (-4 `.bss`
   / -4). The panel ends in `hlt` with nothing after it to stage a name, so
   `sch_dpcol` / `sch_dphc` are the first four bytes of the 13-byte name
   buffer (labels, so `tests/stkpanel.py`'s `m.sym()` reads them as before).
3. **`osapi_seg` on kern_small: `test bl, bl` and the one `clc` / `retf`**
   (0 / -2, kern_mega -1 `.text`). The SKIPF2 shape kern_big already had, and
   `SEG_LIN` = 0 as a test. kern_big and kern_emu keep `cmp bl, SEG_LIN`:
   `gfx_blit1_x` follows in `.cold` and that byte is its parity (REFUSED 3).

### sched.inc / instance.inc

4. **`task_spawn` on the KENT ladder, and the slice top is where the fill
   stops** (-14 / -14). `call kent_bp` / `push es` and `jmp kret_es`, the
   answer written over the banked AX (`mov bp, sp` / `mov [bp+14], ax`); and
   the fill loads the size straight into CX and pushes only the base, so
   `rep stosw` leaves DI = base + size = the top and `xchg` pairs replace the
   three-register bank and the `add` (the slot index is banked as slot x 2
   and halved after). Spawn only: ~+120 clocks for the ladder against a
   0.7 ms fill. +1 word of stack inside the spawn (the AX slot).
5. **`sch_clsbytes` has no class-0 row, and on kern_small it IS
   `sch_stksize + 2`** (-2 / -10 with the next sentence). Class 0 meant SCH_STACK, which is what
   task_spawn gives CX = 0, so `inst_pkg_spawn`'s `dec bx` wraps it onto the
   out-of-range arm (+1); kern_big's table is classes 1-4 (-2), and
   kern_small's partition is one slice of each class smallest first, so its
   table is the partition's own words (-10). `SCH_C1..3` are the one spelling
   both lists are written from; SPEC.md 8.7 says so. `inst_pkg_spawn` reads
   `[di+I_SPTR]` twice instead of banking it (-1 / -1), which with the `dec`
   leaves it at 67 bytes.
6. **`sch_resume`'s parity pad** (-1 / -1) - traded for item 8's byte, which
   sits between it and `ui_task`.
7. **`inst_task_die` lives under `inst_pkg_alive`'s `.die`, which falls into
   it** (-8 / -8): the `jmp inst_task_die` goes, and `.orphan`'s own
   `xor bx, bx` / `jmp task_exit` is `jae inst_task_die.x`. kern_mega's far
   jump names the same label.
8. **`inst_icon_ptr`: one compare, three answers** (-2 / -2): `cmp si, 1` /
   `jb .none` / `jne .runs` for `or` / `jz` / `cmp` / `jne` (I_ICO_PKG = 1,
   asserted).

### toast.inc / clip.inc / snd.inc / apps.inc

9. **`toast_show`'s default TTL** (-2 / -2): `jcxz .ttl` / SKIPF2 / `mov cl,
   TOAST_TICKS` (CH is 0 there; `%error` if the default outgrows a byte).
10. **`toast_pass`'s queued arm jumps to `.up` itself** (-1 / 0, TOAST_Q):
    the `or al, al` / `jnz .up` after the popf went - the `jz .nq` inside the
    window already asked. The pass with no queue runs 7 clocks fewer. Paired
    with **kern_big's `sched.inc` parity pad** (-1 / 0) before `sch_s_ui`,
    which held `wm_paint_all` even - a routine that left the pinned list.
11. **`clip_get`'s skip over `.empty`** (-1 / -1): SKIPF2 for the `jmp short
    .out`; CF = 0 either way.
12. **kern_small's `spk_tone` writes the gate itself** (0 / -6):
    `spk_gate_on` had one caller there (kern_big's PWM falls into it, so it
    stays on kern_big), and the two arms share one `out 0x61, al`.
13. **No `snd_str_busy` on kern_small** (0 / -2): its only askers are kern_big's
    clip paths; the kern_small stub was dead code.
14. **The About box reads `[sch_coop]`** (-2 / -2): `cmp byte [sch_coop], 0`
    for `call KERNEL_SEG:sched_mode_get` / `or al, al`.

### memory.inc / mod.inc / driver.inc / hiber.inc

15. **`mem_regrow` enters `mem_regrowp.e` through SKIPCY1** (-1 / -1): a new
    skip macro (kernel.asm), `db 0x73` - `jnc` over the `clc`, never taken
    because `stc` is the instruction before it. No register, no flag.
16. **Three `.cold` pads traded for real bytes**: kern_small's after
    `mem_floor_ax` (0 / -1) for `mem_fatw_dirty` reading the record's base
    once for both its compares (0 / -1); kern_big's after `osapi_sys_kb_x`
    (-1 / 0, kept on kern_small and kern_mega under `%ifndef MOD_CACHE`) for
    MOD_CACHE's `mod_live` answering through `sub` / `neg` (-1 / 0: `neg`
    sets CF iff the difference is not 0).
17. **`drv_load_at` keeps the row banked across the read** (-2 / 0): the
    `pop bx` / `push bx` between `mem_movable_x` and the read went - BX was
    pushed again at once. No int 13h change: the same one read.
18. **`mmf_mem_claim_own`'s refusal DX ahead of the test** (-2 / -2, kern_mega
    -1): `mov dx, 0` keeps mem_own's CF, and DX is no input of the claim.
19. **`mod_need`'s held row falls under the read** (-4 / 0, MOD_CACHE): the
    `.held` block moved up from below the answers, which brings the arming
    loop's two `.badarm` jumps back into short range (they were 5 bytes each)
    and makes the LIVE thunk path two taken jumps (~24 clocks) faster.
20. **`mod_disarm`'s loop is inside its IF = 0 window** (-4 / -4): it was a
    `call .slot` there and a second `ret`; the fresh BR fix's shape.
21. **`mod_free_row` tail-jumps `mem_free_x`** (-1 / -1); its held-nothing
    `ret` is `mod_disarm`'s, just above.
22. **Flag-dead word stores as `and`** (-4 / 0): `drv_dlg_done_x`,
    `drv_release` (two) and `hbf_perform`.
23. **CTRL.DRV's settings stage reads `[sch_coop]`** (module -2, not
    resident) - `drv_cfg`'s macro, which the boot overlay's `ovc_pack` shares
    (-2 there, not resident).
24. **`drv_task_x`'s success falls into its exit** (-2 / 0): CLC_OR_STC for
    `clc` / `jmp short .out`, the refusals below it.
25. **kern_small's driver refusal stubs share one `stc` / `retf`** (0 / -3):
    the Drivers page's five `drvf_*` join the API refusals, and the four bare
    `retf`s (`drv_notice_x`, `drv_cp_closed_x`, `drv_shutdown_x`,
    `drv_task_x`) enter past the `stc`.

Sum, items 1-25: kern_big 7+4+0+14+2+1+8+2+2+2+1+0+0+2+1+2+2+2+4+4+1+4+0+2+0
= **67**; kern_small 7+4+2+14+10+1+8+2+2+0+1+6+2+2+1+2+0+2+0+4+1+0+0+0+3 =
**74**.

## REFUSED

1. **`inst_icobuf` as `dsk_ico`** (-64 / -64, the largest item seen). Still
   unsafe after BR-4: a package worker's `wm_show` repaints the dock through
   `inst_icon_ptr` under the gfx lock, while task 0 may be inside a mount's
   `dsk_docpass` stage window with the lock FREE (its cli protects its own
   window, not the worker's) - a tile drawn with another file's icon. Not
   taken; see CROSS-FILE 3 for the stale comment.
2. **`snd_town_off` on the KENT ladder** (-3 / -3): it is reached inside
   IRQ0 (`snd_tick`'s tail call), and the ladder adds 4 bytes of depth on
   whatever task the tick interrupted.
3. **`osapi_seg`'s `test bl, bl` on kern_big / kern_emu** (-1): `gfx_blit1_x`
   follows it in `.cold`; it would need a pad back.
4. **`mod_need` without `cmp ax, MOD_H_ENT + 2`** (-5): the later checks
   refuse any short file, but a 0-byte one would reach a zero-paragraph claim
   first. Not proved.
5. **The settings core told apart by `bp == MODS_SIZE`** (-1, kern_big): a
   truncated module file of exactly MODS_SIZE bytes would pass where it is
   refused.
6. **`dskw_trunc` cleared once at `.back`** (-5, kern_big): it would stay set
   from the read to `.back`, and a pre-emption there truncates another task's
   read.
7. **HIBER.DRV-only `hb_*` words into the module's bss** (-18): they are the
   hibernation image's state block (hbm_wake reads them after the restore,
   from a module loaded somewhere else).
8. **`hbf_closed` always marks GONE and drops** (-7): the module's own close
   reaches it with a module frame on the stack - the `je .out` is the guard.
9. **`app_close_now` tests the kind through AL** (-1, kern_big):
   `app_close_win`'s contract keeps every register.
10. **`xm_release_rec`'s second `push cs / pop es` and AX bank** (-2 / -4):
    a driver's DSV_PKGCALL may clobber anything.
11. **`osapi_desk_item_x` on the KENT ladder** (-1): AL is its answer, so the
    frame store eats the gain; not worth the cycles.
12. The `ct_*` trampolines, recounted off the listing by target (not by the
    stale per-site comment): only `ct_cw_font_run` on kern_small pays for
    its removal, and its one site is files.inc's (CROSS-FILE 2).
13. Pass 18's and 19's REFUSED items in these files still hold and were not
    re-proposed (`mod_needi`, `sch_account` through `sch_currec`,
    `clk_snapshot` as `rep movsb`, the `hbf_*` dispatcher, the `.DRV` stem
    table).

## WANTS THE OWNER

None. No behaviour changed; every refusal answers as it did.

## WANTS AN ABI BREAK

None taken or needed. No SDK, `apps/os88api.inc` or `drivers/os88drv.inc`
byte moved, so `tools/abi.lock.json` is untouched.

## CROSS-FILE

1. **`ctrl.inc` (wmui) 3866 and 3894: `mov al, [sch_coop]` for `call
   KERNEL_SEG:sched_mode_get`** (CTRL.DRV -2 each). With those two gone,
   **`sched_mode_get` (sched.inc) has no caller and goes: -4 resident `.text`
   on every kernel**, before `sch_isr` and even, so parity-neutral.
2. **`files.inc` (files) `fm_brun`: on kern_small, `call
   KERNEL_SEG:cw_font_run` / `ret` for `jmp ct_cw_font_run`** (+3), and
   kern_small's `ct_cw_font_run` (kernel.asm) then has no site and goes (-6):
   **-3 kern_small**, post-hot in both places.
3. **`disk.inc` (disk) `dsk_docpass`'s comment** names `inst_icon_ptr` as
   `dsk_ico`'s fifth tenant; it has had `inst_icobuf` since BR-4's first cut
   (REFUSED 1 is why it keeps it). Documentation only.

## ROWS

Run here (fast tier, every commit, all green): `hotpar`, `stkbalance`,
`stkclass`, `stkapps`, `ovlchk` and the rest of `make`'s 44. `make small`,
`make emu` and `make mega` build at the tip (`os88megaseg` overhead 3,407 of
3,555). `tools/stkbalance.py` base against tip: two new findings, both the
known kret-ladder artefact every `jmp kret*_es` site already carries
(`task_spawn` and `mod_need`'s new LIVE exit - the tool cannot resolve the
ladder and reports the ES push as depth +1).

NAMED for the final pass (none run here):

- the door and its refusal: `t_api_abi` (fast) and any door-heavy package row;
  `dispcold` (the door's first 13 bytes, unchanged);
- `task_spawn`, the classes, the worker death: `stkclass`, `stk0water`,
  `stkpanel` (the panel's pen and digit, now in `api_name`), `regpin`,
  `instkeep`;
- the modules: `modcache`, `moddrop`, `dockmodule`, `hibernate`,
  `hibernatedrv`, `p2hiber`, `megahiber`;
- the drivers: `drvup`, `drvcall`, `drvclaim`, `drvmem`, `drvmove`,
  `heapdrv`;
- memory: `memp`, `regrowshed`, `pins`;
- toasts, clipboard, sound: `toast`, `toastbar`, `toastfpg`, `clipkeep`,
  `sndonce`, `sndkeep`, `spkfx`;
- the dock icons: `dockmark`, `deskdock`.

## DEFECTS FOUND

None in behaviour. kern_small carried a dead `snd_str_busy` stub (item 13).
