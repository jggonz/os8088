# ksp17 agent "disk" - a general finder over the disk side

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`, row `disk`): the
shapes of ksp14 BRIEF 0.2 over `kernel/disk.inc` (not `dsk_xfer`, not
`disk_write_x`, not `dsk_fatw_*`), `kernel/diskw.inc`, `kernel/loader.inc`
(not `ld_pkg_byname`), `kernel/lz.inc` (nothing taken: the decoder loops are
hot and `lz_decomp_*` are fresh's) and `kernel/compress.inc` (a module -
nothing resident led there). Branch `ksp17-disk`, base `00348bdc` (the brief).
Figures are ASSEMBLED bytes, `tools/kernsize.py --json` absolutes; resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on
kern_mega).

**TAKEN outside the row**: none.

**The finding**: what is left on the disk side is the long tail pass 16 named
- **single-caller routines** (eight written at their one call: the call, the
`ret` and usually a second branch go, and twice an `add` folded into the
caller's own arithmetic) and **error stanzas**: a run of `mov ax, FERR_x /
stc / ret` (five bytes each) becomes `mov al, FERR_x / SKIPF2` per verdict
and ONE `cbw / stc / ret`, because `test ax, imm16` steps over the next
`mov al` and every FERR_* and LD_* is under 80h. Five sites took that shape.

## BASE AND TIP

| kernel | base | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 91,879 | **91,794** | **-85** (`.cold` 37,793 -> 37,708; `.text`/`.bss` unchanged) |
| kern_small resident | 57,718 | **57,656** | **-62** (`.cold`) |
| kern_emu resident | 92,133 | **92,048** | **-85** |
| kern_mega resident | 125,183 | **125,102** | **-81** (`.cold` 45,925 -> 45,844; `.mega` unchanged) |

kern_big `KERN_SIZE` 97,280 both ends, kern_small 59,904 both ends: no rung
moved. `.text`, `.bss`, `.lowbss`, `.boot2` and `.ovl` are untouched on every
kernel, so the `align 2` after `dsk_img16` swallowed nothing (every change is
`.cold`; checked by kernsize absolutes and by `os88rdiff` routine spans).
kern_mega's megaseg overhead is 3,248 of the 3,250 `tests/megaseg.txt` allows,
unchanged. kerndos (`kerndos.bin`, which assembles disk.inc and diskw.inc)
builds.

## TAKEN

Bytes are kern_big / kern_small per item as estimated from the routine
spans (`os88rdiff`); the section totals are kern_big's and sum to the
assembled -85. kern_small's assembled total is -62 (its per-item figures
include relaxation it does not share with kern_big). kern_emu moves as
kern_big, kern_mega -81 (no `dskw_runmax` item).

### 1. Single-caller routines written at their call (kern_big -37)

| routine | its one caller | bytes |
|---|---|---:|
| `dsk_here_ok` | `dsk_chdir_q_x` (its SKIPF2 tail fell into `dsk_media_ok`, which is now a `call`) | -3 / -3 |
| `dsk_img_icon` | `dsk_docpass` (the caller's `jne` was the routine's own ZF answer) | -6 / 0 |
| `ico_rowoff` | `ico_body` - `add ax, ICO_RBASE + ICO_R_BODY` folds the caller's `add di` | -7 / -4 |
| `dsk_write_chain_x` | `dskw_wabody`'s INSIDE arm | -4 / 0 |
| `dwr_stat` | `dskw_read_at_x`, whose `%else` arm's `call dskw_stat_x` becomes the shared `.dwname` | -2 / 0 |
| `dskw_rt_zap` | `dskw_rtbody.lfn` (`call dskw_rmbody_n83.delent`) | -3 / 0 |
| `dskw_czstamp` | `dskw_commit` | -4 / -4 |
| `dskw_runmax` | `dskw_runadd.newrun` - **not on kern_mega**, whose above-1MB arm puts runadd's entry `jcxz` out of reach; the routine stays there under `%ifdef KERN_MEGA` | -4 / -4 (mega 0) |
| `dsk_sortdir` | `disk_mount_x.sortall` | -4 / -4 |
| `dsk_bpb_flopzap` | REFUSED, below | |

### 2. Error tails as SKIPF2 chains (kern_big -14)

`dskw_mkbody` (`.exists`/`.dirfull`/`.nospace`, -4), `dskw_wbody` (`.big`
then `.prot`, with `.replace`'s `jz .keep` turned `jnz .prot`, -2),
`dskw_stat_x` (`.badname`/`.nodisk`, -2), `dsk_path_x` (`.corrupt`/
`.noname`/`.toobig`, -4, not kern_small), `dsk_find_x` (`.ioerr`/`.end`, -2 on
kern_small only - kern_big has no `.ioerr` and is the same 3 bytes), and
`ld_hdr_size` (`.toobig` steps over `.bad`'s `mov al`, -1),
`loader_run_x` (`.body`'s call steps over `.badix`, -1). AH = 0 by `cbw` on
every FERR arm, as `mov ax, imm` gave; the LD arms answer in AL alone.

### 3. Registers and flags that already answer (kern_big -14)

* `ldf_ld_pkg_start` (OSAPI_PKG_START's far entry): `or al, al / jnz / xor
  ax, ax / retf / stc / retf` is `cmp al, 1 / cmc / mov ah, 0 / retf` (-3):
  CF = 1 for every LD_* but LD_OK and AX = 0 on success, as before. The one
  difference is AH = 0 on the FAILURE arm, where the contract says "AL = an
  LD_* code" and AH was whatever ld_pkg_start_x left.
* `ld_run_body_x`: `mov al, LZ_LZB` is `inc ax` after `mov al, LZ_LZ4`, with
  an assertion that LZB = LZ4 + 1 (-1).
* `ld_pkg_start_x`: `call ld_start / ret` is `jmp ld_start` (-1).
* `dskw_dotents`: time and date held in AX/DX and traded with `xchg` instead
  of reloaded (-2); DX joins the banner's clobbers - `dskw_mkbody`, its one
  caller, holds nothing in DX across it (read: no DX read before a write to
  the end of the routine, inside `dskw_wrap7`'s frame).
* `dskw_dfree_x`: the shift loop's last `rcl` already leaves CF = 0 (free
  sectors < 65,536 x 128 = 2^23, so bit 31 of the bytes is clear); the `clc /
  jmp short .out` is `pop cx / ret` (-1).
* `dskw_char_x`: `.badsym: stc` falls into `.goodsym: pop si / ret`, whose CF
  = 0 is the equal `cmp`'s (-1).
* `dsk_vol_fixed_x`, `dsk_free_clus_x`, `dsk_rah_fill`: `clc` (or a CF = 0
  already standing) then `SKIPBL1` over the `stc`, BL being popped by the
  same tail (-1 each, -3; `dsk_rah_fill` -1 on kern_small too).
* `dsk_ico_stage`: the row arm falls into the copy, `.built` after it (-2).

### 4. Frames and shared bodies (kern_big -20)

* `ld_lname_cp` is `fm_ncpy` (files.inc's NUL-bounded copy, the same loop
  and terminator): -8 / -8. Its `cld` and `ES = DS` output are kept.
* `dwf_dskw_rmany` spells the two bodies (`mov ax, dskw_rmbody / jz .go /
  mov ax, dskw_rtbody / .go: call dskw_wrap7`); `dskw_rmdir`'s only other
  caller is kerndos's `kdback.inc`, so it is `%ifdef KD_BUILD` (-4 / -4).
* `dskw_copy11` banks CX and jumps into `dsk_ncopy` after its own `push cx`
  (new `.held` label), whose tail pops the caller's CX (-2 / -2).
* `dskw_imgro`: `kentc_bp` + `push es` / `jmp kretc_es` (-2 / 0).
* `osapi_vol_fence`: `kentc_di` / `jmp kretc_di` (-1, not kern_small) - pass
  16 refused it ALONE for the FAT walk's parity; here it pairs with:
* `dsk_dirw_cwd` (`mov ax, [dsk_cwd]` in front of `dsk_dirw_start_x`) for
  the two callers that loaded it first, `disk_mount_x` and `dskw_find` (-3).
  **kern_big/emu/mega only** (`%ifdef OS88_DRIVERS`): on kern_small the
  fence item does not exist and the -3 alone put `dsk_next_clus_x` odd.

## HOT PATHS AND PARITY

Nothing on the hot list changed content: `dsk_xfer`, `dsk_runcap`,
`dsk_next_clus_x`, the LZ loops, `dskw_find`'s and the other directory-scan
entry loops are byte-identical (`dskw_find` gains a `call dsk_dirw_cwd` at its
head, once per find: +~20 clocks, no loop change). `hotpar` is green on
kern_big and kern_small at the tip **with no new pad**: every change between
`dsk_runcap` and `dsk_next_clus_x` nets even (`dsk_vol_fixed_x` -1 +
`dsk_free_clus_x` -1; `dsk_sortdir` -4; `osapi_vol_fence` -1 + mount's
`dsk_dirw_cwd` -3), and nothing in `lz.inc` or in disk.inc above `dsk_xfer`
changed. Cycle costs elsewhere are per operation: `dskw_imgro` and
`osapi_vol_fence` on the ladder (~170 clocks a call, per write/mount), the
others faster (a call and ret gone each).

## EQUIVALENCE

By reading, item by item: every inlined routine's exits mapped to the
caller's continuation (its CF/ZF answer followed into the caller's branch),
its local labels renamed, and every short branch crossing it checked by the
assembler; every SKIP chain's labels reached by jumps or by the fall-through
the chain was designed for, every value under 80h. `stkbalance` (fast tier)
is green at the tip; it rejected a first spelling of `dsk_bpb_flopzap`
(REFUSED). The fast tier passed at every commit (70/70 with one skip,
`w16cens`).

## REFUSED

* **`dsk_bpb_flopzap` written at `dsk_batch_begin_x`** (-2): calling
  `dsk_bpb_zap` with a pushed frame is legal but `stkbalance` reads it as an
  unbalanced entry; not worth a declaration.
* **`dskw_czexp` written at `dskw_rbody`** (-6 by arithmetic): built twice,
  measured +1 and +6 - the inlined body puts `.fits`' `jz .empty` and `jae
  .czbad` past short reach, and re-homing those targets relaxes others.
* **`dsk_bpb_check` written at `disk_mount_x`** (-9 by arithmetic): its
  refusals then need a near relay back to mount's `.fail`, which sits 330
  bytes away; jump-in/jump-out prices at 0.
* **A shared FERR ladder across diskw.inc** (`.big`, `.prot`, `.corrupt`,
  `.badarg`, `.exists`...): 0 net - every non-local stanza needs a 3-byte
  relay. Only the local chains (section 2) pay.
* **`dsk_rah_redo` sharing `dsk_rah_want`'s frame**: redo falls into want
  with IF restored by its `popf`; sharing the frame would run want's claim
  with interrupts off.
* **`ico_need`, `dsk_ncopy` on the ladder** (-1 each): per icon on the
  repaint path; not taken for a byte each.
* **`dskw_commit`'s zero loop as `rep stosw`** (-1): needs ES = DS, and
  nothing proves ES dead across `dskw_relold`.

## WANTS THE OWNER

None. No item changes a message, a limit or an edge case. (The pass 15 item
"ship the LZ4 decoder alone", -100 measured then, still stands where it was
put and is not re-proposed.)

## WANTS AN ABI BREAK

None found in this row. The one frozen interface touched, OSAPI_PKG_START's
far entry, keeps its contract exactly (CF, AL; AX = 0 on success).

## CROSS-FILE

* `osapi_fs_ent_x`'s `pop di..bx / retf` tail is `osapi_desk_item_x`'s after
  its `pop es` (files agent's `desk.inc`): -3, left for them (pass 16 listed
  it too).
* `assoc.inc` (files) has the third `call dsk_dirw_start_x`, with `xor ax,
  ax` (the root) rather than `[dsk_cwd]` - no saving there.
* `dsk_geom_pick` has one caller, `dsk_xfer`'s prologue (fresh's routine):
  written at it, its `push`/`pop` pairs go if `dsk_xfer` holds nothing in AX
  there (~-6, not checked).

## ROWS RUN

Once, at the tip (`364c6a07`'s kernel), after the last code commit:
`python3 tools/os88test.py soak -j 1 --marty-jobs 1 -k ...` over **32 rows:
31 passed, 1 failed** (1,304 s): `small128 fmcommit rehome pkgfmt fcpapi
fcpcopy pkgrun icostore pathcost ldcost dospkg lzload lzcomp lzmod drvcall
fdlgsmall fdlgup msel fmthumb instdeep instassoc hddcp deskitem appdata
viddisk wseq czseq vidkern imgmount imgswap rdicon` ok; **`kernmods` FAILED
on a base defect, not this work**: *"every module has a theme in
tools/kernsize.py ... got: fsck.inc"* - `kernel/fsck.inc` arrived with
FSCHECK.DRV (`8bc149c3`, before the brief) and has no row in kernsize's
THEMES; no file of this row is involved (DEFECTS FOUND).

What covers what: the loader (`ld_lname_cp`, `ld_hdr_size`, `loader_run_x`,
`ld_run_body_x`'s format, the `PKG_START` far entry) - `pkgrun`, `dospkg`,
`pkgfmt`, `rehome`, `ldcost`, `lzload`, `lzmod`, every launch; the write
path (`dskw_mkbody`'s chain, `dskw_dotents`, `dskw_commit`'s inlined stamp,
`dskw_wbody`, `dskw_runadd`, the INSIDE arm's write chain, `dskw_dfree_x`,
`dwf_dskw_rmany`) - `fmcommit`, `fcpcopy`, `fcpapi`, `hddcp`, `wseq`,
`czseq`, `lzcomp`, `instdeep`; the read-at / READ_SEQ cursor (`dwr_stat`'s
inline) - `viddisk`, `vidkern`; icons (`ico_body`, `dsk_ico_stage`, the
docpass image icon, `dskw_imgro`) - `rdicon`, `fmthumb`, `icostore`,
`imgmount`, `imgswap`, `instassoc`; `dsk_chdir_q_x` and `dsk_path_x` -
`pathcost`, `appdata`, `ldcost`; the listing sort - `msel`, `fdlgup`;
`osapi_vol_fence`, `dsk_vol_fixed_x` - `deskitem`, `drvcall`; kern_small
(`dsk_find_x`'s chain, the whole small arm) - `small128`, `fdlgsmall`. Not
reached by a row run: `dsk_path_x`'s three refusals and `dskw_stat_x`'s
`.nodisk`, equivalent by reading.

## DEFECTS FOUND

* **`kernmods` is red at the base**: `kernel/fsck.inc` (System Check,
  `8bc149c3`) has no theme in `tools/kernsize.py`'s THEMES, so
  `t_kernmods` fails *"every module has a theme"*. Not fixed here (fresh's
  file and a tool none of this row's items touch) - for the coordinator.

## NOTE AT THE CLOSE (the coordinator's merge)

This branch edits nothing in `dsk_xfer` or `disk_write_x` (fresh's), and
nothing in disk.inc above `dsk_xfer` or in `lz.inc`; between `dsk_runcap`
and `dsk_next_clus_x` its changes net EVEN on both kernels, so it should not
move fresh's parity settlement (-52) - `hotpar` to be re-read on the merge.
Fresh's cross-file offer, **`dsk_unit` right after `dsk_op` in `.bss` so one
word load replaces two byte loads (-4)**, is NOT taken: both loads are inside
`dsk_xfer` (the `.drv` arm and `.dcall`), which is fresh's routine and merged
after this branch's rows ran; it belongs with whoever edits `dsk_xfer` next.
