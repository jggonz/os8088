# ksp13 agent "wkfs" - the file system and the Disk window, whole-kernel

Kernel size pass 13 (the brief on this branch still sits at
`docs/plans/completed/ksp14/BRIEF.md`, the pass's first name). Branch
`ksp13-wkfs`, cut at `aedadd25` (the brief on `4833dfef`). Area: `disk.inc`,
`diskw.inc`, `files.inc`, `filecp.inc`, `fdlg.inc`, `drvvol.inc`,
`dskwin.inc`, `fprog.inc`, `icons.inc`, `assoc.inc`, `clip.inc`, `lz.inc`,
bar the concept agents' hunks since `3bb038f3`. Figures are
`tools/kernsize.py --json` sections, ASSEMBLED bytes; resident = `.text` +
`.bss` + `.cold` + `.lowbss` + `.vgabuf`.

## BASE AND TIP

| | text | bss | cold | lowbss | vgabuf | **resident** | `.boot2` | `.ovl` | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big base | 43,756 | 5,223 | 38,084 | 5,358 | 336 | **92,757** | 5,129 | 5,106 | 98,816 |
| **kern_big tip** | 43,740 | 5,213 | 38,021 | 5,358 | 336 | **92,668 (-89)** | 5,129 | 5,106 | 98,816 |
| kern_small base | 31,847 | 3,063 | 22,482 | 2,868 | 0 | **60,260** | 4,364 | 1,480 | 62,464 |
| **kern_small tip** | 31,847 | 3,009 | 22,404 | 2,868 | 0 | **60,128 (-132)** | 4,364 | 1,480 | 62,464 |
| kern_emu base | 44,015 | 5,223 | 38,084 | 5,358 | 336 | **93,016** | 5,229 | 5,106 | 99,328 |
| **kern_emu tip** | 43,999 | 5,213 | 38,021 | 5,358 | 336 | **92,927 (-89)** | 5,229 | 5,106 | 99,328 |

`kern_dos` (`kerndos.bin`, which shares `diskw.inc`): 31,789 -> 31,763
(-26). No rung crossed or uncrossed; `.boot2` and `.ovl` untouched.

Module images, UNPACKED (`os88drv.image_unwrap`): **FILECP.DRV (kern_small)
1,770 -> 1,855 image and 36 -> 80 bss** - 1,936 of its 2 KB claim, so the
claim is unchanged; it carries the four doors and a `cs:` on 91 operands now
(TAKEN 1, 2). Every other image is the same size (FDLG 1,232, FORMAT 1,136,
DISKOPS 7,168 - still exactly its 7 KB, CTRL 4,383 on kern_small).

## TAKEN

### 1. FILECP.DRV's four doors into its image (kern_small -28, kern_big 0)

`fcp_gosrc`/`fcp_gofs`/`fcp_godst`/`fcp_gofd` (and their shared `fcp_gos`/
`fcp_god` tails) are the only arguments the copy engine ever hands
`fcp_goto`, and nothing outside `filecp.inc` names one - but they sat in the
trailing RESIDENT `.cold` block with `fcp_goto`, which has to stay resident
for DISKOPS.DRV's mid-clone call. On kern_small they are image code now: the
thirteen sites `call` them near (3 bytes each, where `FCPX` was 5), and the
one hop out is the door's own `FCPX fcp_goto` - the same number of hops, so
no slower. kern_big assembles **byte-identical**: the doors stay `.cold` and
fall into `fcp_goto` (`fcp_gos` jumps to `fcp_god.go`, which is `fcp_goto`'s
address there). One source, the arms `.modp`-last for `os88ovlchk`'s reason.

### 2. FILECP.DRV's operation scratch into its own bss (kern_small -44 .bss)

MODULE-SELFCONTAIN-PLAN 6.4.1 built this, measured -192 with the frame, went
red in `fcpsmall` and was reverted with "wants a bisect". It did not need
one: the question it never asked is which RESIDENT code reads the state,
because a resident reader's operand is `[x]` on both builds and so names no
module data - no check can see it. Two did:

* the four doors above, resident, reading `fcp_dcwd`, `fcp_fsclus`,
  `fcp_fdclus` (and `fcp_cwd`/`fcp_drv`/`fcp_ddrv`) through DS - a WRONG
  FOLDER on the first file, which is *"GUIDE.TEX is not in B:\ after the
  paste"*;
* `fm_paste_res` and `fmv_reload_all`, reading `[fcp_err]`, `[fcp_op]`,
  `[fcp_drv]`, `[fcp_ddrv]` and `fcp_fname` AFTER `fcp_fin` has dropped the
  image - the FERR off a freed claim.

So item 1 went first, and what moved is exactly what nothing resident reads,
through one accessor `FCV(x)` (`[cs:x]` in the image arm, `[x]` on kern_big,
`os88ovlchk`'s half 3 construction rule): `fcp_dcwd`, `seg`, `chunk`,
`rlclus`, `rclus`, `roff`, `rsz`, `rszh`, `rgot`, `depth`, `ctype`, `csrc`,
`fsclus`, `fdclus`, `ovwsz`, `need`, `flen`, `started`, `all`, `pend`, `ovw`
- 44 bytes, 91 operands. What stays: `fcp_busy`/`err`/`ddrv`, the operation
record (`fcp_op`..`fcp_name` is one `dsk_ncopy` target, and `fcp_name` is a
DS pointer), `fcp_cname` and `fcp_fname` (DS pointers) and `fcp_made`, kept
resident only so the move is an EVEN 44 - every `.bss` word after
`filecp.inc` (the icon renderer's among them) keeps its parity. Checked by
script on the kern_small listing: no moved label is referenced outside
`.modp`, and every `.modp` reference carries `cs:`. Lifetime: scratch by 3.4's
test - a suspended paste holds the image across the question (`fcp_fin`
drops only on a return that is not `FCPS_ASK`), a MEM_K_MOD claim is pinned
and never shed, and `mod_need` zeroes the bss only on a FRESH load (the
`.already` path skips it), which is what `.bss` was at the start of every
operation anyway (`fcp_seg` is freed by the operation that claimed it).
MODULE-SELFCONTAIN-PLAN 6.4.1 carries the resolution.

### 3. `dskw_fdir` - "a plain folder with a real cluster", asked once (-18 both)

`dskw_rmbody_n83` and `dskw_rtbody` each spelt out find / refuse a protected
entry or a FILE / load the cluster / refuse one outside the volume. One
helper now, both callers `call` it and keep their own continuation. Error
codes are identical on every arm: the find's AX, `FERR_PROT` through
`dskw_wbody.prot`, `FERR_IO` through `dskw_rdata.corrupt` (rmbody's own
`mov ax, FERR_IO / stc` was that body twice). The one ordering difference:
rmbody stored `[dskw_oldclus]` BEFORE the range check and stores it after
now, so a refused folder no longer leaves a bad cluster there - which nothing
reads, every body sets it before `dskw_relold`. rtbody's `.protj` trampoline
goes with it.

### 4. Shared pop runs in `diskw.inc` (-9 both; -12 less one relaxation, below)

`dskw_find`'s pop run already served five routines; `dws_commit`,
`dskw_wrp` and `dskw_isempty` jump into it now (a new `.psi` entry for the
last), and `dskw_ent_store`'s run gains a `.pdi` that `dskw_commit` and
`dskw_read_x` take. A `pop` writes no flag, so every CF rides through as
before. Off every hot path (once per file operation). **One side effect,
on kern_big only: nasm's grow-only relaxation lengthened `fm_dgdrop`'s
`jne .out` (+3)** - its final displacement is 126, so the short form fits,
but the optimiser never shrinks a branch it grew. `fm_dgdrop` is the misc
agent's (CROSS-FILE).

### 5. `assoc_dfold` computed (kern_big -20; kern_small has no assoc)

The assoc agent's hand-off. A 16-byte `.text` table nothing ever wrote: 1
(APPS) for the five built-ins, 0 for every declared slot. `assoc_tryvol`
asks `cmp di, ASSOC_NDEF / jb` instead: APPS first for everyone, and a
built-in looks nowhere else (CF = 1 and AX as the APPS miss left them - the
same answer the table's arm gave). `mov bl, 1` rather than `mov bx, 1`
because `assoc_tryfold` reads BL alone - and that byte is what put
`dsk_xfer`, `dsk_runcap` and `dsk_next_clus_x` back at an even shift. SPEC.md
54's layout and rung tables updated.

### 6. `ico_need`'s count store on kern_small (-5 small)

The assoc agent's hand-off. `[ico_n]` is already 0 whenever `[ico_seg]` is:
`.bss` starts zeroed and `ico_demote` - the only code that writes
`[ico_seg] = 0` - zeroes the count beside it. Only the `%else` arm's
instruction went; kern_big's word store (it also clears `[ico_capn]`) is the
assoc concept's and untouched.

### 7. `dsk_free_slow` written at its one call (-18 both)

`dsk_free_clus_x`'s `.slow` arm called a routine that banked SI/DI, set
SI = 2, DI = 0 and answered through its own `mov ax, di / clc` - all of which
the caller's frame already is (both ways to `.slow` leave SI = 2 and DI = 0
from `.fat`; `dsk_fat_ofs_x` spends BX and DX alone). The loop is the same
instructions, so the same cycles a cluster. SPEC.md's mention (the FAT12
free count's fallback) updated.

### 8. The Disk window's thumb-scroll tail shared (kern_big -14; kern_small has no SBDRAG)

`fm_ondrag_x`'s thumb move ended in `sub ax, [fm_lscr] / fm_scroll_by /
repaint on refusal / kretc_di` - `fm_onup_x`'s release tail byte for byte,
in the same `kentc_di` frame with SI = the window. One `jmp
fm_onup_x.sbscroll`.

### 9. `fm_numbuf` shares `fm_name` (-10 both)

`fm_numbuf`'s one writer is `fm_draw_lrow`'s size column, AFTER that row's
name has been lettered out of `fm_name`; every reader of `fm_name` calls
`fm_stage_name` first, and `fm_newwin` (the one that hands `fm_name` on in
SI) copies it into the seed before any row can repaint. 11 bytes, less a
one-byte pad (`fm_bsspad`) so the `.bss` after it keeps its parity on both
kernels - checked against the base map (`fcp_cbop`, `ico_ww`).

### Parity

`tools/os88rdiff.py kernel aedadd25 . --hot ...` over `gfx_blit4`,
`gfx_blit1_x`, `font_run`, both mouse ISRs, `kbm_isr`, `sch_isr`,
`sch_switch`, `dsk_xfer`, `dsk_next_clus_x`, `ui_task`, `dsk_runcap`: all
identical and at an even shift on kern_big and on kern_small (`-D
KERN_SMALL --build build/smallk`; `mou_p2_isr` does not exist there).

### Tools

`tools/os88rdiff.py` could not assemble a COMMIT side of the kernel:
`vga12.inc` includes `drivers/thmpal/thmrows.inc` and the export took
`kernel`, `boot` and `apps` only. It exports `drivers` too now.

## REFUSED

* **A shared directory-entry iterator** for the seven `dsk_dirw_get_x`
  loops (`dsk_find_x`, `dsk_byclus_x`, `dskw_find`, `dskw_scan1`, `fcp_scan`,
  `asc_*`, `disk_mount_x`): ~40-60 bytes, but a call an ENTRY on the scan
  DISK-CPU-PLAN names as the kernel's own gap (`OSAPI_FILE_FIND` re-walks per
  ordinal). Not on the brief's hot list, and not worth making that plan's
  number worse.
* **The 4-byte module-only doors** (`dskf_disk_read`/`_write`,
  `fmf_fm_utoa`, `fmf_fm_stage_onam`: 16 resident bytes): an image-side go
  device costs each module ~20, and DISKOPS.DRV is exactly 7,168 on
  kern_small - one byte and its claim is 8 KB.
* **FDLG.DRV's dialog state into a module bss** (kern_small ~-16:
  `fdlg_mode`/`act`/`gdis`/`rq*`/`sdrv`/`scwd`/`down`, image-only, and per
  dialog): needs `section .moddb`, `MODD_BSS` and its KB assertion in
  `kernel.asm`, which is wkcore's file. CROSS-FILE.
* **FDLG.DRV's strings into its image** (kern_small ~-26 gross): all four
  are read through DS by resident routines (the button labeller, the window
  caption the WM dereferences on repaint, `dskw_stat`), so it needs a staging
  buffer and a registered stager - MODULE-SELFCONTAIN-PLAN W3, more than it
  saves here.
* **`drv_vol_back` / `fdlg_reap_x` through `dsk_chdir_q_x`'s own test**:
  their `cmp dl,[disk_drive] / cmp ax,[dsk_cwd]` pre-test is COMPARE-ONLY,
  where `dsk_here_ok` also asks whether the media can have changed - dropping
  it would revalidate where it does not today. A behaviour change.
* **`fm_held`** (the "mouse-up queued or button up" test `fm_drag` and
  `fm_dgwait` both spell): -4, and a new contract for it. **`fmv_get_dir`
  into `dsk_get_dir_x`'s tail** (-3), **`dsk_bpb_zap` into `dsk_rah_want`'s**
  (-2), **`fm_menter`'s pops into `kretc_cx`** (-1): each moves `.cold`
  parity for under four bytes. Left.
* **`fm_front_win` through `wm_top`'s walk**: different predicates (any
  visible window with `W_MENUS = fm_menus`, desk windows included).
* **`fm_editkey`'s `je .swap`, `fm_draw_status`'s `je .fs_st_swap`,
  `fcp_xfer`'s `jc .jerr`**: pass 12's refusals stand (status ladders whose
  re-layout pushes another arm out of reach).

## WANTS THE OWNER

* **Cut/Copy/Paste and the Standard File dialog as on-demand modules on
  kern_big too** - FILECP.DRV and FDLG.DRV, exactly kern_small's shape
  (SPEC.md 22.3.0, 38.0). ESTIMATED **~3.2 KB resident**: `filecp.inc` is
  2,017 + 152 resident on kern_big against 151 + 68 on kern_small, and
  `fdlg.inc`'s own code 2,346 - 1,086 (the 1,086 is `os88ui.inc`, which
  `fdlg.inc` includes and which stays resident on both) plus 13 of `.bss`;
  less the slot blocks (~36 of `.bss`) and the resident stubs kern_small
  already counts. It was not assembled: the module ids on kern_big
  (`MOD_DOCK`..`MOD_DESK` are 4..7), `MODP_START`/`MODD_START`, `assoc_go`'s
  near call to `fcp_goto` and FDLG's kern_big-only hooks (`FDH_RECT`, the
  thumb drag) all need porting first - a scratch build stopped at those. **It
  is a functionality change**: a copy, a paste and every package's Open/Save
  dialog would need the system disk on a machine that has 100 KB and never
  needed it, which is why KERN-SMALL-MODULE-SPLIT left kern_big resident on
  purpose. The largest single figure this pass can buy, and the owner's.

## WANTS AN ABI BREAK

None.

## CROSS-FILE

* **misc - `fm_dgdrop`'s `jne .out`** (`files.inc`, a hunk of #252's
  `fm_dgdrop`): write it `jne short .out` (-3 kern_big). Its displacement is
  126; nasm lengthened it when this branch's `diskw.inc` bytes moved the
  layout under it and never shrank it back. Its two relaxed `jz .desk` /
  `jz .out` (136/132) are the other half of the same routine.
* **wkcore - FDLG.DRV's module bss** (`kernel.asm`): `section .moddb nobits
  vfollows=.modd`, `MODD_BSS` and the `MODD_SIZE + MODD_BSS` KB assertion
  beside `.modpb`'s; then `fdlg.inc` moves ~16 bytes of per-dialog state with
  an `FCV`-shaped accessor (kern_small ~-16).
* **wkcore - `fm_s_root`'s 'Disk' as the tail of `drv_t_ram`'s 'Ram Disk'**
  (`driver.inc`, the `fdlg_s_drive` trick already there): -5 `.text` on
  kern_big, if both are in the same build arm.

## ROWS RUN

One pass after the last code commit, `python3 tools/os88test.py
soak -k ... --marty-jobs 1`: **24 rows, 24 ok** (1,459 s) - `fcpcopy`,
`fcpsmall`, `fcpapi`, `fcproom` (FILECP on both kernels, the folder MOVE
that ends in `dskw_rmtree`/`dskw_fdir`, `dskw_dfree`), `dosdir` (the DOS
box's `rmdir`, i.e. `dskw_rmbody_n83`), `dskwstage`, `wseq`, `wseqheld`,
`wsequnclosed`, `wseqcut`, `wseqioerr`, `wseqfull` (`dws_commit`,
`dskw_wrp`, `dskw_read_x` tails), `assocstale`, `assocfind`, `assocvol`,
`assocsweep`, `instassoc` (`assoc_tryvol`'s rung 5 - `instassoc` is the row
whose program sits in `C:\APPS` under a declared slot), `fmthumb`, `fmbtn`,
`msel`, `fmcommit` (the Disk window's thumb drag, buttons, list rows),
`bigvol` (`dsk_free_clus` on a FAT16 volume), `sndmove`, `drvmove`. The
IBM-ROM rows were not needed; nothing SKIPped.

**Broken on purpose**: `fcpsmall` is the row new to me and the one 6.4.1's
attempt turned red, so with item 2 in place `fcp_godst`'s load was written
`mov bx, [fcp_dcwd]` (DS, the mistake that assembles and passes
`os88ovlchk`) - **FAIL in 24.9 s**: *"the folder paste reported FERR 2"*,
*"MEDIA/ is not in B:\SYSTEM after the paste"*. Restored, rebuilt, resident
back to 60,128.

The `.slow` arm of `dsk_free_clus` (item 7) runs only for a FAT12 whose FAT
outruns `DSK_FAT_SECS` - kern_small on a 720 KB or 1.44 MB disk not built
`--fatcap 2` - and no row above reaches it; it is the same loop instructions,
read rather than run (`small128`, the full tier's, boots kern_small).

## EQUIVALENCE

* Item 1: kern_big `cmp` byte-identical; kern_small by reading (the doors'
  bodies are unchanged, the hop count is the same) and `fcpcopy`/`fcpsmall`.
* Item 2: scripted check over the kern_small listing (above) + `fcpsmall`,
  which is the row that went red on 6.4.1's attempt.
* Items 3, 4, 7, 9: by reading, recorded per item; `tools/stkbalance.py` on
  `diskw.inc`, `disk.inc`, `files.inc`, `filecp.inc`, `assoc.inc`, base
  against tip - the same unbalanced paths (none new).
* Item 5: the two arms enumerated above; `assoc*`/`instassoc` rows.
* `make`, `make small`, `make emu` and the fast tier (47/47) after every
  batch; `os88ovlchk` clean.

## DEFECTS FOUND

* **MODULE-SELFCONTAIN-PLAN 6.4.1's failure explained** (not a defect in a
  shipped tree - it was reverted): resident readers of module-bss labels,
  which `os88ovlchk`'s half 1 is blind to by construction. A check for it
  would be: a label in a `.mod?b` section named by any line outside that
  module's image sections. Not written; the note above is the recipe.
* `tools/os88rdiff.py` could not build a commit side of the kernel (fixed,
  above).
