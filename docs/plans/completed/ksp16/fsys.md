# ksp16 agent "fsys" - a general finder over the file side

Kernel size pass 16 (`docs/plans/completed/ksp16/BRIEF.md`, row `fsys`): the
shapes of ksp14 BRIEF 0.2 over `disk.inc` (not `dsk_fatw_*`, not the counting
hunk), `diskw.inc`, `files.inc`, `fdlg.inc`, `filecp.inc`, `loader.inc`,
`driver.inc` (not heap's or place's lines), `lz.inc`, `hiber.inc`'s resident
half, `assoc.inc`, `icons.inc`, the clock and `serial.inc`, on what pass 15's
fsys.md did not read. Branch `ksp16-fsys`, base `714e9d64` (the brief).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega).

**TAKEN outside the row**: `kernel/apps.inc` (no row names it; one tail,
item 1.6), and two zero-byte macros in `kernel/kernel.asm` and
`kerndos/kdshim.inc` (item 6).

**The finding**: pass 15 found frames a single caller never reads; what is
left on the file side is mostly the same thing seen from the other end -
routines that bank registers BY HAND and return nothing in them, so the shared
ladder (`kentc_di`/`kentc_bp`, `kent_di` in `.text`) is the frame and the
pops are a `jmp`; and EXITS that are already written one routine along
(`stc / ret`, `ret`, `jmp kretc_di`, a three-instruction error tail) and can
be reached by `equ` or a retargeted branch instead of spelled again. Both are
mechanical to find over the listing and cheap to prove: the first needs the
header's `out:`/`clobbers:` and every exit read, the second needs only that
the deleted label is reached by jumps alone (the instruction before it is a
`jmp`/`ret` on EVERY build arm) and that every jumper reaches the new target
short. Two came back as relaxations or undefined symbols on another build and
are listed under REFUSED.

## BASE AND TIP

| | base `714e9d64` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 92,136 | **92,032** | **-104** |
| kern_big `.text` / `.bss` / `.cold` | 41,289 / 5,418 / 37,703 | 41,286 / 5,418 / 37,602 | -3 / 0 / -101 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,456 / 5,119 / 97,792 | unchanged | 0 |
| **kern_small resident** | 57,984 | **57,942** | **-42** (`.text` -3, `.cold` -39) |
| kern_small FILECP.DRV (`.modp`) | 1,897 | 1,891 | -6 (not resident) |
| **kern_emu resident** | 92,392 | **92,288** | **-104** (`.text` -3, `.cold` -101) |
| **kern_mega resident** | 125,598 | **125,500** | **-98** (`.text` -3, `.cold` -95; `.mega` 21,648 unchanged) |

No rung moved on any kernel (kern_big `KERN_SIZE` 97,792 both ends).
`.bss`, `.lowbss`, `.boot2` and `.ovl` are untouched on every kernel. The
`align 2` after `dsk_img16` swallowed nothing: every `.text` change (`-3`,
`icon_draw_x`) is in `icons.inc`, after it. **kern_mega's megaseg figure is
unchanged at +3,309** (the base's pre-existing failure, ops's item); `make
mega` still stops at that gate exactly as it does at the base, and the kernel
it builds before stopping is the one measured.

## TAKEN

Bytes are kern_big / kern_small resident; kern_emu moves as kern_big and
kern_mega as kern_big less the six items that are `%ifdef KERN_MEGA`-shadowed
there (its -98 against -104).

### 1. Hand-banked frames onto the ladder (-37 / -8)

Each routine below returns nothing in a register (its header's `out:` is
flags or memory, AX a declared clobber at most), so `kentc_di` (AX..DI),
`kentc_bp` + `push es` with `jmp kretc_es`, or `.text`'s `kent_di`/`kret_di`
is the frame. Cost ~170 clocks a call; every one is per operation, per icon
or per mount, none per pixel, glyph, sector or cluster.

| routine | was | bytes |
|---|---|---:|
| `assoc_locate` | 5 pushes / 5 pops (AX a documented clobber) | -5 / 0 |
| `assoc_compose`, `asc_take_x` | 6 pushes, `pop es/di/si` + `jmp kretc_cx` -> `kentc_bp`/`push es`/`jmp kretc_es` | -5, -5 / 0 |
| `assoc_glyph_take`, `asc_gly_pair` | 4 / 4 | -3, -3 / 0 |
| `asc_inkck` | 3 / 3 (ZF out survives the pops) | -1 / 0 |
| `ico_key_of` | 4 / 4 (kern_small arm 3 / 3) | -3 / -1 |
| `dskw_zerobuf`, `dskw_flushrun` | 4 with ES -> `kentc_bp` + `push es` / `jmp kretc_es` | -2, -2 / -2, -2 |
| `drv_pub_seg` | 3 / 3 | -1 / 0 |
| `drv_dlg_done_x` | AX, BX, SI, BP -> `kentc_bp` / `jmp kretc_bp` | -3 / 0 |
| `icon_draw_x` (`.text`) | 4 / 4 -> `kent_di` / `jmp kret_di`; `icon_draw` below already takes `kent_bp` | -3 / -3 |
| `hb_ok_x` | BX, CX, DX + `retf` -> `kentc_di` / `jmp kretfc_di` (AX is "AH is XCAPS's", a clobber) | -1 / 0 |

### 2. Exits written once (-33 / -16)

* `fcp_step.fail` was fcp_run2's `.seterr` three instructions (`mov
  [fcp_err], al / mov al, FCPS_ERR / jmp fcp_stop`) again: it jumps there
  (-5 / FILECP.DRV -5).
* `app_about_center_x` ended in `mov ax, (CWHITE << 8) | CBLACK / jmp
  ct_cw_font_run`, which is `fm_brun` (-3 / -3; +15 clocks a centred line).
* `fm_mset.out` (`jmp kretc_di`) is `fm_sel_bar.out`'s, in reach (-3 / 0).
* `dskw_size32.over` and `drv_bss.nofit` were `stc / ret` beside one each
  (`dskw_nbody.out`, `drv_check.bad`): retargeted (-2, -2 / -2, 0).
* `hbf_open.no` was a relay to `hbf_perform.toast`, which its `jc` reaches
  (-2 / 0).
* Jump-only `ret`s made `equ` a neighbour's (each label is reached by jumps
  alone on every arm, read in source, and every jumper reaches short):
  `fm_docmd.out`, `fm_c_format.no`, `fm_cldrop.armed`, `fm_c_up.none`,
  `fm_c_dlg.no`, `dskw_fdir.out`, `dskw_rbody.out`, `dskw_rbody.fsfail`,
  `dskw_dbody_n83.out`, `dws_commit.none`, `ld_run_name_x.fail`,
  `dsk_docpass.dp_done` (-12 / -9; `dskw_rbody.fsfail`, `dws_commit` and
  `dsk_docpass` are kern_big's alone).
* `dskw_wdata`'s `jne .go / ret` is `je dskw_wbody.rbret`, the rollback's
  `ret` four lines up (-1 / -1); `fcp_mksub`'s `jnc .go / ret` is `jc
  fcp_mkroot.out` (-1 / module -1); `dskw_last_p`'s `jmp short .out` to a
  `ret` is the `ret` (-1 / -1, and 15 clocks faster twice a cluster).
* `fdlg_cpy` tail-jumps `fm_ncpy` on kern_big through a new `FDXJ` macro
  (`jmp %1` there, `FDX %1 / ret` in FDLG.DRV, so `os88ovlchk` reads no
  near call out of `.modd`) (-1 / 0).

### 3. A copy already written (-10 / 0)

`dsk_vol_add`'s label copy (`es:si` -> the row's inline 8 bytes, at most 7
and the NUL) was a hand loop; it is `fm_ncpye`, SPEC.md 4's counted copy with
its source at ES:SI, which leaves the same 8 bytes (on the early-NUL path
fm_ncpye stores the NUL itself; on the bound it stores the terminator at the
same offset the loop's `.named` did), and the no-label arm stores its NUL
before the test. AH and CX are dead after it (`.live` reloads both).

### 4. Registers the code already holds (-17 / -14)

* `ico_demote` stores a zeroed AX - the shed's `mem_pg_forget` has banked AX
  by `fmv_icostale`'s own contract (-4 / -3).
* `dsk_vol_del.free` stores AH, which the `jz` that got there proved zero
  (-2 / 0).
* `fmv_get_dir` reloaded `CX = DSK_DE_STRIDE` after a `mul cx` that cannot
  write it (-3 / -3; three clocks a row faster).
* `fm_dgdrop` banked BX round `fmv_gdtype`, which keeps it (-2 / -2).
* `fmv_copy_in`'s never-taken zero arm sets ES by `push ds / pop es` - AX is
  zeroed next anyway (-2 / -2; the copying arm is unchanged).
* `ld_check_hdr`'s `>> 3` is `mov cl, 3 / shr al, cl`: CX is now a declared
  clobber, read by none of the three callers after the call (ld_run_body_x's
  two, ld_pkg_start_x's; read, each reloads CX before any use) (-2 / -2).
* `dsk_vol_slot_x`'s `x 16` is `push cx / mov cl, 4 / shl bx, cl / pop cx`,
  with an assertion that `DV_SIZE` is 16 (-2 / -2).

### 5. Flags that already answer (-2 / -2)

`dsk_up_x` ended `cmp [dsk_maxclus], ax / jb .bad / ret / .bad: stc / ret`:
the compare's CF is exactly `.bad`'s answer, so the `jb` goes.

### 6. Skip bytes (-3 / -1) and a spent parity byte (-1 / 0)

`jmp short` over one instruction into a pop is a skip opcode whose register
the pop discards: `dsk_dirw_next_x` (`SKIPBL1` over the `stc`, BX popped -
and 11 clocks faster on the walk's success path), `pth_leaf` (`SKIPAX2` over
`xor si, si`, AX popped), `assoc_tryfold` (`SKIPDL1` over `stc`, DX popped).
`SKIPBL1` (`B3`) and `SKIPAX2` (`B8`) are new beside pass 15's set in
`kernel.asm`, and in `kerndos/kdshim.inc` because kern_dos assembles
`disk.inc` too. **`dsk_bpb_bank`'s `cmc / jc .no`**, kern_big's parity byte
from ksp12/xfile.md, is `jae .no` again (`.no` does its own `stc`): the odd
`SKIPDL1` above it does that byte's job.

### 7. `lz_decomp_x`'s `mov ah, 0` is `cbw` (-1 / -1)

`js .big` has just established AL < 80h. Two clocks faster; also keeps the
hot routines' parity on kern_small (section HOT PATHS).

## HOT PATHS AND PARITY

Nothing on the brief's hot list changed content: `dsk_xfer`, `dsk_runcap`,
`dsk_next_clus_x`, `dskw_find`'s entry loop, `dskw_alloc`'s scan, the LZ
inner loops (`lz_match`'s refusal was tried and refused, below) are
byte-identical. The routines touched that run per entry or per cluster got
cheaper or stayed put (`fmv_get_dir` -3 clocks a row, `dskw_last_p` -15
twice a cluster, `dsk_dirw_next_x` -11 a sector); the ones that got dearer
(~170 clocks for each ladder frame, +15 for each tail jump) run per
operation, per icon or per mount.

`hotpar` is green on kern_big and kern_small at every commit. It took
arranging: the cuts IN FRONT of `dsk_xfer`/`dsk_runcap` (apps.inc, assoc.inc,
lz.inc, disk.inc's head) and between `dsk_runcap` and `dsk_next_clus_x`
(`dsk_vol_*`) each have to come out EVEN on both kernels. **No new pad**; one
old one spent (`dsk_bpb_bank`'s, item 6). Two candidates were left out for
parity alone (REFUSED). The layout the coordinator merges onto will differ:
heap's and gui's cuts land in front of disk.inc too.

## EQUIVALENCE

By reading, routine by routine: for every ladder frame, the header's outputs
and every exit (`tools/stkbalance.py` over the twelve files touched, base
against tip: 19 unbalanced paths against 17, the two new being
`dskw_zerobuf`'s and `dskw_flushrun`'s `kentc_bp` + `push es` / `jmp
kretc_es` - the same known tail as `disk_mount_x`'s and `dsk_rah_fill`'s -
and `asc_take_x`/`assoc_compose` moving from `kretc_cx` at +3 to `kretc_es`
at +1); for every `equ`, that the label is reached by jumps alone on every
`%ifdef` arm and that the target exists on every build that assembles the
jumper (two did not - REFUSED); for every register reuse, the instructions
between the proof and the use. kern_small, kern_emu, kern_mega and kern_dos
(`kerndos.bin`, which assembles disk.inc and diskw.inc) all build.

`dsk_ent_zero` and its `.zero` entry (heap's `drv_release` caller) are
untouched.

## REFUSED

* **`lz_match.bad` as `lz_lz4.bad`** (-2): built, and `dsk_xfer` RELAXED +3
  - a branch in it sits on the edge of short reach across the shift. Net +1.
* **`fm_c_clone.no` as `fm_fmt_go.no`** (-1): `fm_cldrop.armed` is already an
  `equ` of it, and the chain put `fm_cldrop`'s `jne` out of reach (+3).
* **`dskw_wbody.out` as `dskw_fsstat.out`** and **`fm_c_pasteno` as
  `fm_armset.out`** (-1 each): the targets are `OS88_REDIR`/kern_big only, and
  kern_dos / kern_small assemble the jumpers.
* **`osapi_vol_fence` on the ladder** (-1) and **`dsk_sortdir.out` as
  `dsk_up_open.out`** (-1): both sit between `dsk_runcap` and
  `dsk_next_clus_x`, and either alone puts the FAT walk odd.
* **`osapi_dos_handoff_x.no` as `sc_rclick_x.out`** (-2): the target is a
  local label in place's `desksc.inc`; a cross-file `equ` into a file another
  agent is editing is a merge hazard for two bytes.
* **`drv_owns_seg_x` on the ladder** (-2): `mem_sum_kb` calls it per heap
  record, so ~170 clocks x 32 records on every Task Manager refresh.
* **`fm_draw_icon16`/`fm_stage_name`, `fmv_copy_in`, `dsk_get_dir_x`**:
  per-row on the scroll paint (their own banners say so), or SI is the output.
* **`.fs_st_fmt`/`.fs_st_clone` in `fm_draw_status` as one body through a
  register** (~-8): the line callees are `FORMAT.DRV`'s and `DISKOPS.DRV`'s
  through far cells, and nothing states they keep BX or CX.
* **The defensive `cld`s** (SPEC.md 1 rule 3 already promises DF = 0): the
  owner DECLINED this in pass 13 (P14 handoff 3).
* **`clk_put_mon` written in `clk_fmt`** (-11): CTRL.DRV calls it too.
* **`fm_stage_name` through `fm_ncpy`**: the routine depends on SI ending at
  +16, which a NUL-stopping copy does not give.
* **`drv_load_at`'s `push dx` round `mem_movable_x`**: heap's lines
  (CROSS-FILE).

## WANTS THE OWNER

None. No item changes behaviour, a message, a limit or an edge case.

## WANTS AN ABI BREAK

None. Every interface touched is kernel-internal (`ld_check_hdr`'s
clobber list gains CX; frames of internal routines; local labels).

## CROSS-FILE

* **`fm_ncpye` into CTRL.DRV** (place's `ctrl.inc`): its one caller is the
  panel's `osapi_drv_dlg_x` through `CPXC`; written in the module it is -13
  resident on kern_big for +13 of CTRL.DRV (15 bytes of body, a near `call`
  for the 5-byte `CPXC`), which has ~104 bytes of room. **But
  `dsk_vol_add` now calls it too (item 3)**, so this is no longer free: the
  module would need its own copy and the resident one stays. Not proposed.
* `osapi_fs_ent_x`'s `pop di..bx / retf` tail (disk.inc) is
  `osapi_desk_item_x`'s after its `pop es` (place's `desk.inc`): a label
  there buys -3.
* `dsk_fatw_pick`'s `jmp short .out` to a `ret` (heap's): -1.
* `drv_load_at`'s `push dx`/`pop dx` round `mem_movable_x` (heap's claim
  lines): -2 if `mem_movable_x` keeps DX.

## ROWS RUN

Once, at the tip (`0d1638d5`'s kernel), after the last code commit,
`python3 tools/os88test.py soak -j 1 --marty-jobs 1 -k ...` over **28 rows:
28 passed, 0 failed, 0 skipped** (686.8 s; `fatwpin` 114.5 s against 30
declared, on a box shared by five agents, and passed):
`instassoc assocflush assocvol ascabsorb rdicon fmthumb hddcp rdmount
imgmount imgswap bigvol drvmove fdlgup fmcommit fcpcopy lzmod lzdrv
fdlgchoose fdlgdrop hibernate shedrelist fatwpin small128 fdlgsmall msel
kernmods deskitem drvcall`.

What each subject is covered by: the association frames and `assoc_tryfold`
- `instassoc`, `assocflush`, `assocvol`, `ascabsorb`, `rdicon`, `fmthumb`
(document icons, `ico_key_of`, `assoc_compose`); `dsk_vol_add`'s label copy,
`dsk_vol_del`, `dsk_vol_slot_x` - `hddcp`, `bigvol`, `rdmount`, `imgmount`,
`imgswap`, `deskitem` (a driver's volume with a label on the desktop); the
write path (`dskw_wdata`, `dskw_flushrun`, `dskw_zerobuf`, `dskw_size32`,
`dskw_last_p`) - `fmcommit`, `fcpcopy`, `hddcp`; `ico_demote` -
`shedrelist`, `fatwpin`; the loader and LZ (`ld_check_hdr`, `cbw`) -
`lzmod`, `lzdrv`, `kernmods`, every launch; drivers (`drv_bss`,
`drv_pub_seg`) - `drvmove`, `drvcall`; the Disk window's rets, `fm_mset`,
`pth_leaf`, `dsk_up_x` - `msel`, `fdlgup`, `fmcommit`; the file dialog
(`fdlg_cpy`) - `fdlgchoose`, `fdlgdrop`, `fdlgsmall`; `hb_ok_x` and
`hbf_open` - `hibernate`; kern_small on its own machine - `small128`,
`fdlgsmall`. Not reached by any row run: `drv_dlg_done_x` (a driver's file
dialog completing) and `app_about_center_x` (the kernel's About box and
notes) - equivalent by reading.

The fast tier (`hotpar`, `asmrules`, `canary` among them) is green at every
commit; `make small` and `make emu` build at the tip, and `make mega` builds
its kernel and stops on the pre-existing `os88megaseg` gate exactly as at
the base.

## DEFECTS FOUND

None. `make mega` fails at the base and at the tip on `os88megaseg` (+3,309
against 3,231), unchanged by this work (the coordinator's note: ops's item).
