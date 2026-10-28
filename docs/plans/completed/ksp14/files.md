# ksp14 agent "files" - the Disk window's wheel and folder walk, image mounts, the CD-ROM's desk icon, links' argument text

Kernel size pass 14 (`docs/plans/completed/ksp14/BRIEF.md`). Concept (BRIEF 0.1,
row `files`): the file side added since pass 13 - `fm_onwheel_x` and
`mou_whl_add`, `dsk_dscan`/`dsk_goto` (which replaced FILECP's
`fcp_scan`/`fcp_rdnext`/`fcp_goto`) and `dsk_find_x`, the mounted-image
routines (`dskw_imgro`, `dsk_img_next`, `dsk_img_orphan`), `fm_fmt_reach`, the
CD-ROM's desk icon (`desk_drvico`, `desk_caprect`, `drv_f_cd`/`drv_t_cd`),
`assoc_run_x`'s missing-program retry, links' argument text (`lnk_open`,
`osapi_arg_text_x`, `lnk_argo`) - plus, per BRIEF 0.1, what was read on the way
in `disk.inc`, `diskw.inc`, `files.inc`, `filecp.inc`, `desk.inc`,
`desksc.inc` and `assoc.inc`. Branch `ksp14-files`, base `6c83b8ff` (the
menubar merge). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.

**Do the old and new folder walks still overlap?** No. On kern_big
`fcp_scan` is a 62-byte caller of `dsk_dscan` and `dsk_find_x` the other; the
sector walk `fcp_scan` carried is gone. kern_small keeps its own walks
(`dsk_find_x`'s `%else` arm, resident, and `fcp_scan`'s, in `FILECP.DRV`) on
purpose: there the shared walk would be resident and the copy's walk is module
bytes (`disk.inc`'s `DSK_DSCAN` note), and that reasoning still holds.

## BASE AND TIP

| | base `6c83b8ff` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 93,135 | **92,885** | **-250** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 43,785 / 5,388 / 38,140 / 5,486 / 336 | 43,785 / 5,384 / 37,894 / 5,486 / 336 | 0 / -4 / -246 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 5,926 / 5,118 / 99,328 | 5,926 / 5,118 / 99,328 | 0 |
| **kern_small resident** | 59,050 | **58,909** | **-141** (`.bss` 2,991 -> 2,987, `.cold` 22,054 -> 21,917; `.text` 31,137 unchanged) |
| kern_small `.boot2` / `.ovl` / `KERN_SIZE` | 4,381 / 1,486 / 61,440 | unchanged | 0 |
| **kern_emu resident** | 93,406 | **93,156** | **-250** (`.bss` -4, `.cold` -246) |
| **kern_mega resident** | 102,952 | **102,702** | **-250** (`.bss` 5,386 -> 5,382, `.cold` 42,748 -> 42,502; `.mega` 14,698 unchanged) |

Module images (unpacked, `os88mod`'s figure):

| image | base | tip | claim |
|---|---:|---:|---|
| `FORMAT.DRV`, kern_big | 3,131 | 3,179 (+48) | 4 KB both, 917 to spare |
| `FORMAT.DRV`, kern_small | 1,231 | 1,279 (+48) | 2 KB both |
| `FILECP.DRV`, kern_small | 1,837 | 1,813 (-24) | 2 KB both |

Every other module and driver image is byte-identical. No rung moved on any
kernel: KERN_SIZE is the same step on all four, and the 250 bytes are banked
below the next one. Against the concept's ~+250 net, -250 is the whole of it;
about 125 of it is in the concept's own routines and the rest is what reading
them turned up in the seven files around them.

## TAKEN

Bytes are kern_big / kern_small resident. *(outside)* marks an item outside
the concept's own routines (BRIEF 0.1's "other opportunities").

### Structural

* **The format's reach test and its fallback are `FORMAT.DRV`'s** (-65 / -65:
  61 of `.cold` and 4 of `.bss`; image +48 with the next two items). `fm_fmt_reach` (resident, 45) and the `dskw_fmt_reach` stub (8)
  were a decision taken while the image is in RAM from the first instruction
  to the last: format, read the last sector back, re-format a 720K that did not
  reach it as 360K. It is `dskw_fmt_do_x` now, the `FME_FORMAT` entry, reaching
  `dskw_format_x` and `dskw_fmt_reach_x` as `push cs / call` (both still end in
  their own `retf`; `STKBALANCE-OK` on the caller says why the walk cannot pair
  them). `FME_REACH` goes, so `FMFP` is 4 bytes shorter (`.bss` -4), and the
  resident `.fmt` arm is one call. The only path that behaves differently is
  the image vanishing between the format and the test, which nothing between
  them can cause (`mod_need` is answered from RAM). `kerndos/kdshim.inc`'s
  `FM_NENT` mirror moves with it. SPEC.md 18.96's table is rewritten.
* **The probe stores its own answer** (-7 / -7, image +3): `dskw_fmt_probe_x`
  writes `[fm_fmtrow]`/`[fm_fmtalt]` - adjacent now, one word - itself;
  `fm_fmt_go`, its one caller, stored them with a 3- and a 4-byte `mov`.
* **Single-caller routines written at their calls** (pass 13's commonest
  shape). Where the caller had the banked registers dead the bank went too:

  | routine | where it is now | kern_big | kern_small |
  |---|---|---:|---:|
  | `desk_drvico` | `desk_pic` (DI is `desk_col`'s to clobber at both calls) | -8 | - |
  | `desk_zone_label` *(outside)* | `desk_caprect` (its AX/BX bank was the caller's own) | -8 | -8 |
  | `dsk_img_orphan` | `dsk_vol_del` | -4 | - |
  | `fm_isdown` *(outside)* | the button painter (BX is reloaded after) | -6 | - |
  | `fm_sb_thumb` *(outside)* | `fm_scrollpaint` (AX..DX dead: its header said so) | -8 | -8 |
  | `fm_nfiles` *(outside)* | the header composer (SI reloaded two lines on) | -6 | -6 |
  | `fm_title_flush` *(outside)* | `fm_repaint` (and the zero stored through AX) | -11 | -11 |
  | `fmv_movable` *(outside)* | `fmv_fit` (no caller of `fmv_fit` reads a flag) | -10 | - |
  | `fmv_viofs` *(outside)* | `fmv_get_dir` (SI is the output; its conditional tail call became `jnz` + `call`) | -3 | -3 |
  | `assoc_docicon_x` *(outside)* | `dsk_docpass` (the `.IMG` arm spends DI and CX anyway) | -12 | - |
  | `asc_slot_of` *(outside)* | `asc_merge_ext`'s loop | -10 | - |
  | `fcp_here` *(outside)* | `fcp_run2` (the `stc/pop/ret` tail becomes `jz .none`) | -9 | module |
  | `fcp_rlup` *(outside)* | `fcp_relink`, placed past `.no` | -2 | module |
  | `fm_samename` + `dsk_streq` *(outside)* | the rename's `.ren` (nothing banked) | -16 | -16 |

  Each header's reasoning moved with its body; SPEC.md's live mentions say
  where each is now (18.7.1, 18.96, 18.101.1, 18.101.3, 22.x, 26.4.2, the
  section 4 table's `disk.inc` row).

### Peepholes in the concept's routines

* **`desk_caprect`**: DESKPIC's picture offset is `4 - ((left + 4) AND 7)`
  (DESK_PICX is a multiple of 8, asserted), and the `+4` folds into the
  centring term, with BP - banked at entry for `font_width_x` - as scratch
  instead of a DX bank: -7 kern_big (kern_small has no shortcut arm). Checked
  against the old arithmetic in a model over every width 0..399 and every cell
  left 0..1099: 0 mismatches.
* **`dsk_goto`**: one range test (`cmp bx, 1 / je` then the unsigned
  `ja` against `[dsk_maxclus]` lets 0 through as it did), DL loaded once for
  both the media test and `.full`, and `.fsgo`'s refusal falls into `.bad`
  (kern_big's arm alone): -8 / -6.
* **`dsk_dscan`**: the read-error/end pair is `mov ax, FERR_IO / jnz .stc`
  falling into `.end`, `FSV_ENUM`'s refusal joining on ZF (-4, kern_big's
  routine alone); the
  hidden/system fence is `test dl, dl` (every caller passes 0 or 1 - the API
  fence's `sbb/inc`, `fcp_scan`'s `xor dx, dx`, the two module callers' `xor
  al, al`; -1, faster). The same `test` in kern_small's `dsk_find_x` (-1).
* **`dsk_find_x`** on kern_big reads `dsk_dscan`'s raw entry through SS
  instead of banking ES round it and copying SI to BX: -6.
* **`lnk_open`**'s second `push ss / pop es` (`dskw_read_x` keeps ES): -2.
* **`fm_onwheel_x`** leaves by `fm_onkey_x.outI`'s `jmp kretc_di`: -3.
* **`assoc_run_x`**: the `.rnone` relay goes (-2), and the volume is stored
  as a word whose high half clears `assoc_dead`, the byte after (`cbw`, -4 -
  and the even total is what keeps `dsk_xfer`, `dsk_runcap` and
  `dsk_next_clus_x` behind it on their measured parity).
* **`desk_pic`**'s service zone reads the record's adjacent class and verb as
  one word and swaps (-2).

### Adjacent bytes written as one word *(outside, except the first)*

Each pair asserted with `%if b - a != 1 / %error`:

* `fm_fmtrow`/`fm_fmtalt` (the concept's): Space swaps them as one word (-2 /
  -2), the probe stores them as one (above), the image compares them as one
  (`0x0302`, image -8).
* `fm_swapwho`/`fm_fmtstep`: Format and Clone arm the swap prompt with one
  store each (-8 / -8).
* `fcp_pend`/`fcp_ovw` in `fcp_file1` (-3), `fcp_op`/`fcp_drv` in `fcp_door`
  (-2), `fcp_busy`/`fcp_err` in `fcp_run2` (-3) - kern_big resident, kern_small
  module.

### Tails

* `fcp_stop` falls into `fcp_unbatch` (-4, both). `fcp_unbatch`, `fcp_n13`,
  `fcp_ensure` and `fcp_srcdel`'s `.tree` end in a tail jump on kern_big
  (-4): `FCPXJ`, a macro rather than a bare `jmp`, because `os88ovlchk` files
  every body in `filecp.inc` as `.modp` (it cannot evaluate `FCP_MOD`) and
  would read a near jump out of the image; kern_small keeps `FCPX` + `ret`.

### Hot paths

Nothing on BRIEF 1's hot list changed content. `dsk_dscan` (per entry of
`OSAPI_FILE_FIND` and of a copy's walk) and `dsk_find_x` got cheaper, not
dearer. **Parity**: `hotpar` green on kern_big and kern_small at every commit;
the `assoc_run_x` store was first written `mov ah, 0` (-3) and put
`dsk_xfer`, `dsk_runcap` and `dsk_next_clus_x` on an odd shift - `cbw` made it
-4 and even rather than padding.

## REFUSED

* **`fcp_rdnext` written in `fcp_xfer`'s chunk loop** (kern_big): built, -1
  net. The 26 bytes pushed `fcp_xfer`'s `jnc .fin` past short reach (+3).
* **`desk_cap` written in `desk_draw_zone`**: built, 0 net - `desk_draw_zone`'s
  early `jc .done` relaxes. A copy of the 12-byte epilogue near it costs more.
* **`dskw_dotents` in place**: pass 10's refusal stands (four `jcc`s in
  `dskw_mkbody` relax).
* **`dsk_geom_pick` in place** (-4 estimated): its caller is `dsk_xfer`, a hot
  routine whose content BRIEF 1 keeps unchanged; it would be faster, but that
  is the coordinator's call, not a concept agent's.
* **`dsk_write_chain_x` in place** (-4): SPEC.md 18.4.7 describes it in four
  places as the wrapper that sets the direction byte; not worth the rewrite.
* **`dskw_rt_byclus` in place** (-4): `dskw_rtbody`'s loop is laid out for
  short reach (`jmp short .loop` after it) and its CX/DX/SI/DI are the walk's.
* **`lnk_open` written in `assoc_run_x`** (~-7): the `LNK_SPMIN` guard is a
  stack-depth test, and a call fewer per level moves where a self-naming link
  chain stops - a refusal-path change. `sc_walk` into `lnk_open` the same.
* **`lnk_open`'s parameter scan before the volume read** (-1): `.pop` would
  then have to clear `lnk_argo`, which rests on it being 0 at every entry.
* **`dskw_imgro` without its AX bank** (-2): the rename body's AX contract
  after the test is not proven.
* **A "first image row" entry to `dsk_img_next`** for its two callers: 0.
* **`dsk_dscan` answering "redirected" in SI** so `dsk_find_x` drops its
  `dsk_vkind` compare: -1, a new contract for a byte.
* **`fm_swapwho`/`fm_fmtstep` compares as one word read** (-1 at each of two
  sites): needs AX at both, where it is not free at one.
* **`dsk_vkind`/`dsk_vflop` as one store** in the mount's reset: 0 (AX has to
  be 2 straight after for `disk_heads`).
* **`fm_s_folder` as the tail of `fm_s_up`** ('Up One Folder'): it is already
  the tail of 'Root Folder' (`fm_s_rootf`), which is the better share.
* The wheel's ui_task arm (the idle pass) and `mou_apply`'s per-packet wheel
  decode (ISR): hot, and the latter is the serial agent's hunk.

## WANTS THE OWNER

None. Every item above is byte-identical behaviour on every reachable path.

## WANTS AN ABI BREAK

None. The interfaces touched are kernel <-> module (`FME_*`, `FM_NENT`,
checked by `os88mod.py`) and kernel-internal.

## CROSS-FILE

* `kernel/hiber.inc` (~line 2800) says "the compare underneath is
  `dsk_streq`" of the `dskw_stat` name lookup; `dsk_streq` had one caller (the
  rename) at the base and is gone now - the comment was already describing
  something else. HIBER.DRV's file; one comment.
* The P14 handoff (6) says kern_small's `DISKOPS.DRV` "is exactly 7,168
  bytes": it is **7,196** at this base (and the tip), so its claim is already
  8 KB. Stale, not this pass's change.
* `dsk_chdir_q_x`/`dsk_chdir_x` bracket `[dsk_quiet]`/`[dsk_keepcwd]` with
  byte stores; one word store would need the two adjacent and `dsk_chdir_x`
  written into its quiet twin - larger as measured (38 against 33). Left.
* `fcp_god`'s `jmp dsk_goto` (kern_big, 3) could be a fall-through if the four
  doors were assembled in front of `dsk_goto` on kern_big - a second copy of
  their source under `%ifndef FCP_MOD`, against filecp.inc's one-source rule.

## ROWS RUN

Once, at the tip (`4ec671d7`), after the last code commit, `--marty-jobs 1`:
`python3 tools/os88test.py soak -k fmtreach -k fmtpair -k fmtlow -k wheel -k
mouwheel -k wheelps2 -k fcpcopy -k fcpapi -k fcproom -k assocsweep -k
assocstale -k assocfind -k assocopen -k imgmount -k imgswap -k desksc -k
deskzoom -k deskwhole -k fmbtn -k fmthumb -k drvscroll -k fmcommit -k
instassoc -k pathcost -k cdromsim -k doslnk -k deskhover` - **27 passed, 0
failed, 0 skipped** (619.6 s). The fast tier (61 rows, `hotpar` and
`stkbalance` among them) green at every commit, both kernels; `make emu` and
`make mega` build at the tip.

What each subject is covered by: the format move and the row pair - fmtreach
(arm B is the 720K-on-a-40-cylinder fallback), fmtpair, fmtlow; the wheel -
wheel, mouwheel, wheelps2; `dsk_goto`/`dsk_dscan`/`dsk_find_x` and FILECP -
fcpcopy, fcpapi, fcproom, pathcost, instassoc, assocsweep/stale/find/open;
images - imgmount, imgswap; the desk - desksc, deskzoom, deskwhole, deskhover;
the Disk window's painters - fmbtn, fmthumb, drvscroll, fmcommit; links -
desksc (its `.LNK` legs), doslnk; the CD-ROM - cdromsim.

**Broken on purpose** (a private worktree of the tip with five one-instruction
breaks, each the same size so no parity moved; rows run there once):

| break | row | result |
|---|---|---|
| `dskw_fmt_do_x` compares the row pair with `0x0303` (no 720K ever falls back) | fmtreach | **red** (arm B) |
| `fm_onwheel_x`'s edit test inverted | wheel | **red** |
| the written-in `fm_isdown`'s second test inverted | fmbtn | **red** ("a press draws the toggle DOWN") |
| `desk_caprect`'s `add bp, 4` -> `3` (every desk caption 1 px off its picture) | desksc, deskzoom, deskwhole, deskhover | **all green - NO ROW SEES IT** |
| the rename's compare treats every changed name as unchanged (`jne .renew` -> `.done`) | fmcommit | **green - NO ROW RENAMES A FILE** in the Disk window (fmcommit is Delete, New Folder and a typed character); only the fast tier's `asmrules` went red, and that was the break's own dead label, not the behaviour |

So two of this pass's changes have no row that would see them break: the desk
caption's x (the model check above is the evidence for that one) and the
Disk window's rename (equivalence by reading: the inline loop is
`dsk_streq`'s, instruction for instruction, with its operands fixed and
nothing banked because AX/SI/DI are reloaded by the rename that follows).
The CD-ROM's own picture (`desk_pic`'s written-in drvico test) has no
MartyPC machine and cdromsim checks the volume and CDRack, not the icon -
equivalence there is by reading too (the same instructions, BL banked as
before, DI dropped because both of `desk_pic`'s calls are inside `desk_col`,
which clobbers AX..DI).

## DEFECTS FOUND

None in the shipped behaviour. **Coverage gaps** (no row goes red - above): the
desk caption's horizontal placement under its picture, and the Disk window's
Rename (no row renames a file there at all). Worth a row each. **Gates**: `os88ovlchk` cannot evaluate
`FCP_MOD`, so a kern_big-only tail jump in `filecp.inc` has to be spelled as a
macro (above); `stkbalance` cannot pair a `push cs / call` with the callee's
`retf`, hence the one `STKBALANCE-OK`.
