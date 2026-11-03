# Kernel size pass 17: sys

The sys agent's record: `kernel/memory.inc`, `mod.inc`, `driver.inc`,
`xmem.inc`, `hiber.inc`'s resident half, `clock.inc`, `serial.inc`,
`cpudet.inc`, and the placement lever (`boot/boot2.asm`'s kernel blocks,
`.cfx`, `.ovl`/`.ovlw`). Branch `ksp17-sys`, cut from `00348bdc` (the brief).
Every figure is ASSEMBLED: `tools/kernsize.py --json` on each kernel's own
build and `tools/os88rdiff.py kernel 00348bdc .` per routine. No estimate is
quoted as a saving.

## BASE AND TIP

Resident is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: plus `.mega`, unchanged at 21,712).

| kernel | base | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,879 | **91,801** | **-78** | `.text` 40,925 -> 40,902 (-23), `.cold` 37,793 -> 37,738 (-55) |
| kern_small | 57,718 | **57,689** | **-29** | `.text` 30,494 -> 30,476 (-18), `.cold` 21,393 -> 21,382 (-11) |
| kern_emu | 92,133 | **92,055** | **-78** | as kern_big; `KERN_SIZE` 97,792 -> **97,280** (one rung down) |
| kern_mega | 125,183 | **125,107** | **-76** | `.text` 46,167 -> 46,146 (-21), `.cold` 45,925 -> 45,870 (-55) |

`KERN_SIZE` kern_big 97,280, kern_small 59,904 unchanged. No `.bss`,
`.lowbss`, `.boot2`, `.ovl` or `.cfx` byte moved on any kernel; `BOOT2_SECS`
is unchanged.

| image | base | tip |
|---|---:|---:|
| CTRL.DRV kern_big (`MODC_SIZE`, the image with its bss) | 15,336 | 15,338 (+2: item 16's CX bank; still 30 granules) |
| CTRL.DRV kern_small | 5,650 | 5,652 (+2, the same bank; still 12 granules) |

`tests/megaseg.txt` is **not** tightened: the overhead was 3,248 at the base
and is **3,250** at the tip, the file's own figure. Item 17 is kern_big's
alone (kern_mega's arm keeps `MEGA_SEG0`), so kern_big's `.text` fell two
bytes further than kern_mega's.

## TAKEN

Bytes are kern_big / kern_small / kern_mega (kern_emu = kern_big). Nothing
below is on the hot-path list: every item is per load, per unload, per claim,
per compaction step, per Control Panel page, per second of the clock or per
boot. `hotpar` is green at every commit with **no new parity pad** (see
*Parity* below).

### driver.inc

1. **`drv_load_at` banks neither DX nor AX round `mem_movable_x`** (-4 / - /
   -4). `mem_movable_x` writes neither - `mem_find_own` reads DX (and
   kern_mega's `mem_val` banks it), and both exits are `mem_free_x`'s
   pushf/SI frame - and AX is dead: the PARA store above was its last reader
   and `dskw_read_x` takes none. This was pass 16's CROSS-FILE "DX bank"
   (-2); the AX bank was dead too.
2. **`drv_check` reads the image it was handed** (-6 / - / -6). ES is
   already the claim - `drv_load_at` read the file into ES:0 and
   `dskw_read_x` keeps ES - so `mov es, [bx+DRVR_SEG]` goes (-3); the entry
   bound compares against BP, which the test above it has just made equal to
   `DRV_H_IMG` (-2); and with the operands swapped (`cmp bp, ax / jbe`) the
   success path leaves CF = 0 and needs no `clc` (-1).
3. **`drv_bss` takes BP for `DRV_H_IMG`** (-3 / - / -3), the same fact.
4. **`drv_release` on the KENT ladder** (-7 / - / -7). `kentc_di` banks CX
   and BX as well as the four it pushed, so the service-table clear's CX bank
   and the owner sweep's BX bank go and the tail is one rung. Unload-only.
5. **`drv_cls_idx` as one unsigned compare** (-3 / - / -3): `dec` wraps
   class 0 to FFFF, so `cmp di, DRVC_MAX / cmc` is both range tests; the
   refusal still answers DI = 0, which `drv_cls_fp_x`/`drv_cls_svc_x`
   publish.
6. **`drv_cp_class_x` counts in AL** (-9 / - / -9): `drv_cls_svc_x` banks
   AX, so the class is the callee's argument and the hit's answer at once -
   the CX counter, its bank, `mov al, cl` every pass and `.hit`'s copy back
   go.
7. **`drv_shutdown_x` counts in AL** (-5 / - / -5): `drv_unload_x` is a
   KENT routine and hands AX back.
8. **`drv_pub_seg`'s `or cx, cx / jz` is `jcxz`** (-2 / - / -2).
9. **`drv_task_x`'s dead BX bank round the TF_SERVICE stamp** (-2 / - / -2):
   `.out` pops the caller's BX and nothing between reads it.
10. **`drv_unload_x` tests the row's entry word and goes through
    `drv_call_ent`** (-1 / - / -1). `drv_call_ent`'s banner said the fifth
    site "reads BP back"; it tested it, and now tests the row instead.
11. **`drv_pkg_call_x`'s refusal shares `.done`** (-1 / - / -1, `.text`).
    The OSAPI_DRV_CALL package door; the refusal path only.

### mod.inc

12. **`mod_need`** (-5 / -7 / -5). No `.already` arm: `cmp word [x], 0`
    cannot borrow, so `jne .out` answers CF = 0 (-3). ES is already the image
    at the check - the read went to ES:0, and both `dskw_read_x` and the ROM
    door's COPY (`boot/osrom.asm`) keep ES (-2: `MODR_SEG` is offset 0). And
    on a kernel with no shortcuts (kern_small) the id's DI bank round the
    header compare is dead - only the settings core's two tests read DI
    after it (-2 there).
13. **`mod_row` builds the row in AX** (-2 / -2 / -2): `add ax, mod_tab` is
    the accumulator's 3-byte form and `xchg bx, ax` one byte. Every caller
    banks AX already (`mod_need` and `mod_drop` on the KENT ladder,
    `mod_live`'s two by push/pop); both banners say so now.

### clock.inc (and one bank in ctrl.inc's image)

14. **`clk_mlen` as a parity** (-9 / -9 / -9, `.text`). A month has 31 days
    when it is odd below August and even from August on, so `cmp al, 8 /
    adc al, 0 / and al, 1 / xor al, 31` replaces the 0x15AA mask and its
    BX/CX banks; February is the branch, first. AL only, so AH - live across
    both CTRL.DRV calls as the +1/-1 step - and BX - `clk_inc_sec`'s row
    pointer - are untouched. Months 0 and 13 still give 30.
    **`tests/unit/t_mlen.py` follows the shape**: it reads every immediate at
    a FIXED offset whose opcode it checks first (a scan for an opcode byte can
    find one inside `[clk_year]`'s address) and turns them back into twelve
    months.
15. **`clk_put_mon` counts in AH** (-5 / -5 / -5, `.text`): `mov ah, 3 / mul
    ah` is the month times three, `mov ah, 3` again the count, and the CX and
    AX banks go - neither caller reads AX after (`clk_fmt` reloads AL,
    `clk_fld_str` pops its own). Its banner says "clobbers AX".
16. **`clk_put4` leaves CX to its module caller** (-2 / -2 / -2, `.text`;
    CTRL.DRV +2). `clk_fmt`'s CX is an output it sets afterwards; CTRL.DRV's
    `clk_fld_str` "clobbers nothing else", so it banks CX round its one call
    - in the image, where a byte is not resident.

### memory.inc

17. **`mem_region_reloc`'s ES = 0 from `mem_rr_walk`'s DI** (-2 / - / 0,
    `.text`): the walk's only exit is the zero word that ends a group, so DI
    is 0. kern_big only; kern_mega's arm is `MEGA_SEG0`.
18. **`mem_bytes_kb`'s zero test is the shift's ZF** (-2 / -2 / -2, `.text`):
    a shift by a non-zero CL sets ZF from its result.
19. **`mem_claim_1`'s refusal and success share a tail** (-1 / -1 / -1). DX is
    the base on success - a segment above `[mem_base]`, never 0 - and 0 on
    every refusal, so `SKIPF2` over `.fail`'s `xor dx, dx`, the one `popf`,
    and `cmp dx, 1` is the answer (CF = 1 iff DX = 0). Its one caller,
    `mem_claim_x`, reads CF alone.

### xmem.inc, hiber.inc, drvvol.inc

20. **`xm_copy` on `kent_bp`** (-2 / - / -2, `.text`), the answer written over
    the banked AX (`osapi_drv_use_x`'s shape). ~+40 clocks a copy against a
    transfer of up to 32 KB.
21. **`hb_pick_x` counts in BL** (-4 / - / -4): `dsk_vol_fixed_x` takes the
    volume in BL and banks BX, so the counter is its argument; the CX bank
    and `hb_pick1`'s `mov bl, cl` go.
22. **`drv_find`'s refusal through `CLC_OR_STC`** (-1 / -1 / -1).
    `kernel/drvvol.inc` is in no agent's row - TAKEN HERE, SAID HERE.

**Parity.** `hotpar` binds three routines in `.cold` (`dsk_xfer`,
`dsk_runcap`, `dsk_next_clus_x`, which sit after `mod.inc`) and two in
`.text` (`wm_paint_all` after `clock.inc`, `ui_task` after `memory.inc`). So
`mod.inc`'s `.cold` delta, `clock.inc`'s `.text` delta and `memory.inc`'s
`.text` delta must each be EVEN. Two odd one-byte savings were built,
measured and **left out for that reason** - see CROSS-FILE 1 and 2; with
either one alone the gate goes red. Nothing in `driver.inc`, `drvvol.inc`,
`hiber.inc` or `xmem.inc`'s ranges binds a hot routine on any kernel.

**Equivalence.** Beyond the rows: a scratch unicorn harness over the TIP's
own `kernel.bin` ran `clk_put_mon` for months 1-12 (name, BX + 3, SI and CX
kept), `clk_mlen` for months 0-13 in a leap and a common year (AH, BX, CX
kept), `drv_cls_idx` for all 256 classes with AH both 0 and 5Ah (DI and CF,
AX kept), and `drv_cp_class_x` over 60 random publication patterns at
ordinals 0-11 and 0FFh (the class or the count, CF, AH/BX/CX/DI kept): 0
differences. `tools/stkbalance.py` over every touched file reports the same
findings at the base and the tip.

## THE PLACEMENT LEVER

Pass 16's place.md says the lever is spent without a function change, and
that holds for this scope: the only resident routines added since `920b839b`
are the ones the brief lists in its section 0.2 (`dsk_xfer`'s staging,
`lzfilt`, `osapi_vol_check` and the fsck doors, `ld_pkg_byname`), all the
fresh agent's. None of my files grew a routine since pass 16, so there is no
new ground to walk; place.md's section 3 items are owner decisions and are
not taken. `.boot2`, `.cfx` and `.ovl` are unchanged.

## REFUSED

1. **`hb_s_ptr` out of resident** (13, `.text`). Its readers are the blob's
   `hb_probe_x` (boot, before any module) and HIBER.DRV's `hbm_nset`, which
   STAMPS its extension and hands it to the file layer through DS. A copy
   needs room in the blob (one byte spare) and a staging buffer HIBER.DRV
   can own across a hibernate; neither is free.
2. **The hibernate state block into HIBER.DRV's nobits section**: it is the
   image state and crosses loads of the module (`hb_doscode`'s own comment:
   "those two are separate loads of the image").
3. **The `push ax / mov al, MOD_x / call mod_need (or mod_drop) / pop ax`
   banks** (~2 bytes at ~15 sites in eight files): NOT dead. The KENT ladder
   hands back the AX it was called with, which is `AL = the id`, not the
   caller's AL. Every site I read uses AL afterwards.
4. **`drv_task_x`'s success through `SKIPF1` into `.fail`** (-2): `.die`
   would then fall into the next `.cold` bytes if `cw_task_exit` ever came
   back, where today it falls into `.fail`; guarding that costs the saving.
5. **`drv_pub_seg` without the zero test**: `drv_task_x` refuses BX = 0
   first, but CTRL.DRV's `modc_e_dlg` passes the caller's ES unchecked, and a
   0 would then match every unpublished slot - a fence weakened.
6. **`drv_dlg_done_x`'s two zero tests as one**: with `[drv_dlg_seg]` = 0
   and an unloaded row the compare would pass and clear `[drv_dlg_row]`
   where it does not today.
7. **`mem_pg_forget` without its banks**: `mem_cp_walk` reads SI and BX
   after it.
8. **`mod_need`'s two BX banks round the claim as one, via CX**: +2.
9. **`clk_put4` by `aam 100`** (-4): a V20 ignores AAM's immediate.
10. **A `clk_sp2` entry (`' '` then `clk_put2`, over `clk_colon2`'s store by
    `test si, imm16`)**: one site, net 0.
11. **`clk_ampm` / `clk_h12h` / `str_len` rewrites beyond the one in
    CROSS-FILE 1**: no shorter form.
12. pass 16's REFUSED items in these files (heap.md 1-7, sweep.md's
    `mod_cfix` and `mem_hifit`/`mem_cp_dest`, fsys.md's `drv_owns_seg_x`
    ladder and `clk_put_mon` into `clk_fmt`) still hold and are not
    re-proposed; `mod_disarm`'s called loop is still pass 14's parity pad.

## WANTS THE OWNER

None. No behaviour changed; every refusal path answers as it did.

## WANTS AN ABI BREAK

None found in this scope. The one interface that keeps paying in these files
is internal: the `push ax / mov al, id / call mod_need / pop ax` shape at
~15 sites (REFUSED 3) would be ~3 bytes a site cheaper as `call mod_needi /
db id` with the id read off the return address - kernel-internal, not ABI,
and not built: two entries (need and drop) cost ~25 bytes against ~40, and
the sites are in five other agents' files.

## CROSS-FILE

1. **`str_len` (`clock.inc`, -1, `.text`), built and backed out for
   parity**: `mov cx, si / not cx / .l: cmp byte [si], 1 / inc si / jnc .l /
   add cx, si`, which leaves SI past the NUL. Its one caller is `toast.inc`'s
   `toast_place` (the ui agent's), whose `kent_di` ladder restores SI.
   Takeable with any other odd byte in `clock.inc`'s `.text` (between
   `clk_mlen` and `wm_paint_all`).
2. **`mem_region_reloc`'s `cbw / xchg si, ax`** (-1, `.text`): AX is spent
   (`sch_frame` takes none). Built and backed out for parity (`ui_task`).
3. **`dsk_fatw_pick`'s `jmp short .out` to a `ret`** (-1, `disk.inc`; pass
   16's CROSS-FILE, assigned to this row): NOT taken - it sits between
   `dsk_runcap` and `dsk_next_clus_x` and flips the latter. Take it with any
   other odd byte in that span, or at the close.
4. **`ctrl.inc`'s `clk_fld_str`** banks CX round `CPXK clk_put4` (item 16),
   CTRL.DRV's image half - in no agent's row; +2 there.
5. **`toast.inc`'s `toast_place`** could write `str_len` at its one call
   (~-7 with CROSS-FILE 1's body, since the ladder already banks SI) - the
   ui agent's routine, not edited.

## ROWS RUN

Once, after the last code commit (`b050dae2`), with the tree not edited
while they ran: `python3 tools/os88test.py soak --marty-jobs 1 -j 2` scoped
to 36 rows. The first invocation was stopped by the session's 30-minute
background limit after 27 rows had reported (all ok); the nine it had not
reached were run in a second invocation. **36 passed, 0 failed, 0 skipped.**

| rows | what of this branch they reach | result |
|---|---|---|
| `drvmove`, `drvup`, `drvcall`, `drvcut`, `heapdrv`, `sndmove` | `drv_load_at`/`drv_check`/`drv_bss` (items 1-3), `drv_release` (4), `drv_unload_x` (10), the class walks (5, 8), `drv_pkg_call_x` (11) | ok |
| `cpnames` | the Drivers page through `drv_cp_class_x` (6) | ok |
| `shutdown`, `dispreboot`, `fddpark`, `cfgtrip` | `drv_shutdown_x` on the reboot path (7), CTRL.DRV's load and drop (12, 13) | ok |
| `desksc`, `deskitem`, `stikboot`, `dockpos` | `mod_need`/`mod_row` for the settings core, DESK.DRV and DOCK.DRV (12, 13) | ok |
| `rombig`, `romsmall`, `romnodisk` | `mod_need`'s ROM door COPY with no ES reload (12) | ok |
| `small128`, `smallboot` | kern_small's `mod_need` without the DI bank (12) | ok |
| `dtfield`, `mlen` | `clk_mlen` (14) through CTRL.DRV, and the twelve months out of the binary | ok |
| `regwork`, `regmove`, `heapcheck`, `memp`, `fatwpin`, `pgrank` | `mem_claim_1` (19), `mem_region_reloc` (17), `mem_bytes_kb` (18) | ok (`fatwpin` 248 s against a declared 30, under load; it passed) |
| `xmcheck`, `dosxms` | `xm_copy` on the ladder (20) | ok |
| `hibernate`, `hibernatedrv` | `hb_pick_x` (21), `drv_release`/`drv_load_at` on the reload | ok |
| `mega-regmove`, `mega-drvmove`, `mega-heapcheck`, `mega-cfgtrip` (Bochs, installed for them) | kern_mega's arms of the same | ok |

The clock bar (items 15, 16) has no row of its own; the unicorn harness in
TAKEN covers `clk_put_mon`, and `clk_put4` differs only in the CX it no
longer banks, whose one module caller now banks it. `make`, `make small`,
`make emu` and `make mega` build at the tip; the fast tier is green.

## DEFECTS FOUND

1. **`docs/INDEX.md` was stale at the brief's commit** (`00348bdc`), which
   fails `make` on a fresh worktree; regenerated in the first commit (pass
   16's defect 6, again).
2. `drv_call_ent`'s banner said its fifth caller "reads BP back afterwards";
   `drv_unload_x` tested it before the call and read nothing after. Corrected
   with item 10.
