# ksp17 agent "files" - a general finder over the file manager

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`, row `files`): the
shapes of ksp14 BRIEF 0.2 over `files.inc`, `fdlg.inc`, `filecp.inc`,
`desk.inc`, `desksc.inc`'s resident half, `assoc.inc` (not fresh's hunks or
the `assoc_run_x` they sit in) and `icons.inc`. Branch `ksp17-files`, base
`00348bdc` (the brief). Figures are ASSEMBLED, `tools/kernsize.py --json`
absolutes on each kernel's own build; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (kern_mega: plus `.mega`, which did not move).

**TAKEN outside the row**: none. `tests/dispcold.py`'s witness list names two
routines this branch wrote into their caller, so it is edited with them.

**The finding**: what pays on the file side now is the CALLER'S end of a
contract. Pass 15 and 16 trimmed frames a routine keeps for nobody; this pass
found the mirror image - callers banking registers round a callee that already
keeps them (`fm_drag` is a ladder routine, `inst_win_owner` clobbers DI alone,
`fm_vp_set` and `dsk_dest_x` keep AX), a frame banked for a caller whose own
exit is the ladder (`fm_onkey_x .cmd`'s SI), and the last few single-caller
routines that could be written at their call without a new jump
(`fm_find_view`, `fm_front_win`, `fm_rows_only`). The first kind is cheap to
prove - the callee's header and body, read once - and was found by a script
over every `push / call / pop` triple in the seven files whose callee
documents "all registers preserved" or opens on `kentc_di`.

## BASE AND TIP

| kernel | base `00348bdc` | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,879 | **91,816** | **-63** | `.cold` 37,793 -> 37,730 |
| kern_small | 57,718 | **57,667** | **-51** | `.cold` 21,393 -> 21,342 |
| kern_emu | 92,133 | **92,070** | **-63** | as kern_big |
| kern_mega (with `.mega` 21,712) | 125,183 | **125,120** | **-63** | `.cold` 45,925 -> 45,862 |
| kern_mega segment overhead (`os88megaseg`) | 3,248 | 3,248 | 0 | gate green |
| kern_small FILECP.DRV (`MODP_SIZE`) | - | 1,886 | -5 | not resident |

`.text`, `.bss`, `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl` and `.cfx`
are unchanged on every kernel; `BOOT2_SECS` and `KERN_SIZE` (kern_big 97,280,
kern_small 59,904, kern_emu 97,792) unchanged. Every byte is `.cold`, which is
RESIDENT - "-63 resident, no rung crossed".

## TAKEN

Bytes are kern_big (= kern_emu = kern_mega) / kern_small.

### 1. A frame no caller reads (-9 / -9) - `75a8e158`

`fm_c_pastein` (the context menu's Paste Into) banked CX, SI and DI round its
two tests: the frame of `fm_selfold`, folded into it in pass 12. Its one
caller is `fm_docmd` through `fm_jmp`, and `fm_docmd` reloads SI from
`[fm_cmdwin]` after every command while its neighbours (`fm_c_paste`'s
`fm_vpdrv`, `fm_c_newwin`'s `fm_count`) already leave DI and CX changed - so
no caller of `fm_docmd` (`fm_onkey_x`, `fm_oncmd_x`, `ui_rcmd`) can be reading
them. With the pops gone the `.out` merge is two branches: `jnc fm_c_pasteno`
for `fm_sel_ok`'s refusal (its `cmc / jc` was CF inverted to reach the pops)
and `jb fm_c_pasteno` for a non-folder. `fcp_paste` banks BX..DI itself.

### 2. Written at the one call (-12 / -12, -7 / -7) - `75a8e158`, `2009a9bf`

* `fm_find_view` and `fm_front_win` are `fm_choose`'s two walks and had no
  other caller. Written there: the two calls and two `ret`s go, the found
  case's `jmp short .out` becomes a `mov di, si` ahead of the `je` (a `mov`
  keeps the compare's ZF and CF), and `fm_front_win`'s CX bank goes - CX is
  `fm_count`'s there and nothing below the walk reads it. AL, DX and SI stay
  banked: `fm_seed`, `fmv_load` and `fm_settitle` read them.
* `fm_rows_only`, the middle scroll tier, written into `fm_scroll_by` where
  its call stood: the call, the `jmp short .done` after it and its own
  `clc / ret` and `stc / ret` exits go - those exits are `fm_scroll_by`'s
  `.same` and `.no`, which already existed (`jmp short .same` closes the
  body). It banked nothing, `fm_scroll_by`'s ladder being the frame.

### 3. Banks round a callee that keeps the register (-4, -4, -2, -2 / same) - `c0266de9`

* `fm_onclick_x` banked AX and BX round `fm_drag`, which opens on `kentc_di`
  and leaves by `kretc_di` on every path (`fm_dgwait`'s refusal, `fcp_arm`'s,
  the drop) - so both come back untouched. Its header now says so.
* `fm_dgdrop` banked CX and DX round `ct_cw_inst_win_owner`: `inst_of_win`
  documents "clobbers: nothing else" beside DI and banks AX and SI itself.
* `fm_dgdrop .go` banked AX round `fm_vp_set`, which writes DI, `[fm_vp]`,
  `[fm_vseg]`, `[fm_vinst]` and banks BX - nothing else.
* `fmv_load` banked AX round `dsk_dest_b0`, whose body (`dsk_dest_x`) banks
  AX itself. DX and BX stay banked: the call site loads DX, and `dsk_dest_b0`
  zeroes BX.

### 4. Banks a callee keeps for no caller (-2, -2, -2 / -2, -2, 0 - desk_zone_whole is kern_big's) - `75a8e158`

* `fm_dgmove` banked AX. Its three calls - `fm_drag`'s two and `desksc.inc`'s
  `sc_m_move` through `wm_cbd` - each load AX before their next read of it
  (`fm_linger`, the tracking loop's `[mouse_x]`, `sc_m_cell`, which clobbers
  AX anyway). Header says so.
* `fm_hasdot` banked SI. Its two callers: `fm_editkey`'s `.text` leaves
  through the ladder (`.lineonly` -> `kretc_di`), and `fm_extkeep` reloads SI
  from `fm_onam` (SI is a declared clobber of it). AX stays banked:
  `fm_editkey` stores the '.' it was handed.
* `desk_zone_whole` (kern_big) is reached by `desk_pm_body`'s `call dx` alone,
  and that loop reads CX (the zone counter) and DX (this routine's address)
  after it; AX is reloaded from CL and BX is not read. So it banks CX and DX
  and ends `pop dx / pop cx / ret` (5) instead of four pushes and
  `jmp kretc_dx` (7) - and `tools/stkbalance.py` loses its ladder tail.

### 5. fm_count bounded by address (-2 / -2) - `75a8e158`

`fm_count` walked `inst_tab` with a down-counter in DX it banked; the bound is
now `cmp si, inst_tab + INST_MAX*I_RECSZ` (as `fmv_inext` does). DX is no
longer touched, so it is still preserved.

### 6. filecp (-10 / module -5) - `1de8266b`

* `fcp_file1`'s `.clash: mov ax, FERR_EXIST / .err: stc / ret` is
  `fcp_ensure`'s `.clash`/`.fail`, which ends just above it: its three
  branches go there, in short reach on both builds (FILECP.DRV's `FCPX` is five
  bytes a call and the farthest is ~70 back). kern_big -5, FILECP.DRV -5.
* `fcp_arm` on kern_big banks through the ladder: `FCPX kentc_di` and
  `FCPXJ kretc_di` (the macro spelling, because `os88ovlchk` reads the
  `%ifdef FCP_MOD` arm's `.modp` section and would call a bare `call
  kentc_di` a cross-segment near call). DX is banked too and kept; CF, the
  answer, survives the ladder's pops. FILECP.DRV keeps its five pushes - a
  module cannot reach the ladder near. -5 on kern_big, ~170 clocks per Cut,
  Copy or drag start.

### 7. The clipboard keys (-5 / -5) - `4f23aaab`

`fm_onkey_x`'s Ctrl+X/C/V were `mov bx, FMC_x / jmp short .cmd` twice and a
`mov bx` once; they load BL and skip each other's load with `SKIPF2` (`test
ax, imm16`, flags only), and `mov bh, 0` clears the high byte once (-3).
Delete, which loads BX whole after its `fm_delrep`, moves above them and
jumps to `.cmd`. And `.cmd` banked SI round `fm_docmd` "for .redraw" - but it
leaves by `jmp .out`, which is the ladder and restores SI anyway (-2).

## HOT PATHS AND PARITY

Nothing on the hot list is touched: no per-pixel, per-glyph, per-sector or
per-cluster loop, no ISR, no `font_run`/blit run. The per-row paths
(`fm_draw_lrow`, `fm_draw_icon16`, `fm_stage_name`, `fm_ultoa_x`) are
untouched by design - pass 16 refused ladder frames on them and the same
reason holds. The changed routines run per click, per key, per scroll or per
drag step; the scroll tier got 2 jumps and a call/ret cheaper per scroll, the
clipboard keys 4-15 clocks dearer per keystroke for the skip opcode, `fcp_arm`
~170 clocks dearer per arm.

`hotpar` is green at every commit on kern_big and kern_small. All of this
branch's bytes are in `.cold`, past every hot `.text` routine, so no parity
moved and **no new pad**.

## EQUIVALENCE

By reading, routine by routine, for every bank removed: the callee's header
AND body (every exit), and every instruction between the call and the next
write of the register on every path of every caller. `tools/stkbalance.py`
over `files.inc`, `filecp.inc` and `desk.inc`, base against tip: 6 unbalanced
paths at the base, 4 at the tip - the two gone are `desk_zone_whole`'s ladder
tail; none new. kern_small, kern_emu and kern_mega build at the tip;
`os88megaseg` is green (3,248 of 3,250).

## REFUSED

* **`fm_mdraw` written into `fm_lsel_bar`** (-1 at best): `fm_mdraw` opens on
  `kentc_di`, so it cannot follow `fm_lsel_bar`'s `push di` without its
  `kretc_di` returning into that word; the version that reloads DI inside it
  saves one byte for a new `mov di, [fm_vp]`.
* **`fm_editkey` / `fm_edit_commit` / `fm_dgdrop` / `fm_drag` written at their
  one call**: each leaves by a tail jump (`fm_edit_end`, `fm_paste_res`,
  `sc_modcall`) or a ladder whose restore its caller relies on (SI = the
  window, for the repaint); inlined, each tail becomes `call` + `jmp` and the
  sum is zero or worse.
* **`fcp_unbatch` at `fcp_step .ask`**: `fcp_stop` falls into it, so the
  routine stays and a second copy is +5.
* **`fcp_rdnext`'s DI/ES bank on kern_big**: its own comment says DI is
  `fcp_xfer`'s WRITE_SEQ token, and ES is read by `dsk_goto`'s callers in a
  way not proven here.
* **`assoc_stem_of`/`assoc_padcpy`'s frames moved between the two** (0): the
  AX bank only moves.
* **`assoc_app_get` before `assoc_app_new`** (-1): it would fall through
  instead of `jc`, but moving it splits the three `assoc_ext`/`app` helpers'
  layout for one byte, and `assoc.inc` has fresh's hunks in it.
* **`desk_learn_x` / `asc_learn`'s banks** (-4 / -4 at most): their one
  caller is `disk_mount_x`/the harvest in `disk.inc`, whose register life
  after the call runs into other agents' code; not proven.
* **`fm_ultoa_x`/`fm_utoa` on the ladder** (-3): per row on the list paint
  (the size column), pass 16's refusal of per-row ladder frames.
* **`fm_bsspad`**: a parity pad for every `.bss` word after `files.inc`
  (its own comment).
* **`lnk_open`'s two `add sp` exits**: `add` writes CF, so the shared tail
  needs a flags-free `lea sp, [bp+..]` that only one path has BP for (-1).
* **The two shareable string tails found** (`cmz_s_in` in `fm_s_swap1`,
  `assoc_s_apps` in `stm_s_dir`): both partners live in module images, not
  resident.

## WANTS THE OWNER

None. No item changes behaviour, a message, a limit or an edge case.

## WANTS AN ABI BREAK

None found on this row. Everything touched is kernel-internal: local labels,
the frames of internal routines, `fm_dgmove`'s and `fm_hasdot`'s clobber
lists (headers updated), `desk_zone_whole`'s. The file manager's surface that
IS frozen - `OSAPI_FILE_COPY` (`fcp_door`), `OSAPI_FERR_TEXT`, the Standard
File dialog's cells - was read and has nothing that pays to break: each is a
thin door onto a body the kernel needs anyway.

## CROSS-FILE

* None proposed. The two string tails above are both in module images.

## ROWS RUN

Once, at the tip (`4f23aaab`'s kernel), after the last code commit, with the
tree not edited while they ran: `python3 tools/os88test.py soak -j 1
--marty-jobs 1 -k ...` over **21 rows: 21 passed, 0 failed, 0 skipped**
(1,040.7 s): `fmrefuse dragui fmcommit clipkeep fcpapi fcpcopy fcpsmall
fcproom dispcold fdlgsmall fdlgdrop fmarrows autostart msel fdlgchoose
fmthumb fmbtn desksc zonedmg deskwhole deskdrag`.

What each item is covered by: `fm_choose` (both walks) - `dispcold`,
`fdlgchoose`, `autostart`, `desksc` (shortcut opens); `fm_drag`, `fm_dgdrop`,
`fm_dgmove` - `dragui`, `msel`, `desksc` and `autostart` (drops on the
desktop), `deskdrag` (`sc_m_move` through `fm_dgmove`); Paste and Paste Into,
`fcp_arm`, `fcp_file1` - `fcpcopy`, `fcpapi`, `fcproom`, `clipkeep`, `msel`,
`fcpsmall` (FILECP.DRV); the clipboard keys and `.cmd` - `clipkeep`, `msel`;
`fm_scroll_by`'s middle tier - `fmarrows`, `fmthumb`; `fm_hasdot`,
`fmv_load` - `fmcommit` (rename/new folder), every listing; `desk_zone_whole`
- `deskwhole`, `zonedmg`; kern_small's file manager - `fcpsmall`,
`fdlgsmall`. Paste Into from the context menu is reached by no row run here
and is equivalent by reading (item 1). The fast tier (`hotpar`, `canary`,
`asmrules` among them) is green at every commit.

## DEFECTS FOUND

None.
