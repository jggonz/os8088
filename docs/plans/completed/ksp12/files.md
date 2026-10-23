# ksp12 agent "files" - the file-system, loader and driver-row features

Kernel size pass 12. Concept: the file-system, loader and driver-row
features that landed with no size pass and are all on `main` (`b47de145`),
so every published interface they added is FROZEN this pass:

| commit | feature | unpassed cost (the brief's) |
|---|---|---|
| `5ebea5a` | two file classes: the RAM disk and the cable attached at once (SPEC.md 62.9.19) | +103 kern_big, `HIBER.DRV` +4 |
| `c04ffc4` | `OSAPI_PKG_REHOME`'s KEEP form (SPEC.md 20.12.10.5) | +41 kern_big |
| `4094670` | `OSAPI_FERR_TEXT` (SPEC.md 18.4.11) | +25 / +25 |
| `78298be` | a failed mount is not a place to go back to (SPEC.md 51.5.2.1) | +15 / +15 |
| `edd0efb` `8cead00` `a32dfc3` | the `DA` key (51.5.4), `HDD.DRV` loads itself (51.3.2), `CLONE.DRV` -> `DISKOPS.DRV` | +4 / +2, blob +86, window +30 |
| `c193155` `dde6b19` | `LD_EVER` (#244), the port pins' resident side (#248) | +46 / +40, blob +20 |
| | **total** | **+234 kern_big, +82 kern_small** |

Branch `ksp12-files`, cut from `df8603a1`. Figures are assembled bytes,
`tools/kernsize.py --json`, absolutes against this branch's base (NOT
kernsize's printed deltas, which are against `b6acc4f7`). Resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`.

## BASE AND TIP

| | base `f54e1a99` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.bss` / `.cold` | 43,738 / 5,212 / 38,690 | 43,742 / 5,210 / 38,591 | +4 / -2 / -99 |
| **kern_big resident** | **93,574** | **93,477** | **-97** |
| kern_big blob (`.boot2`) / window (`.ovl`) | 4,884 / 5,102 | 4,879 / 5,102 | -5 / 0 |
| kern_small `.text` / `.bss` / `.cold` | 31,967 / 3,063 / 22,515 | 31,967 / 3,063 / 22,492 | 0 / 0 / -23 |
| **kern_small resident** | **60,413** | **60,390** | **-23** |
| kern_small blob / window | 4,366 / 1,480 | same | 0 |
| kern_emu `.text` / resident | 43,997 / 93,833 | 44,001 / 93,736 | **-97** (the same code) |
| kern_emu blob | 4,984 | 4,979 | -5 |
| `KERN_SIZE` big / small / emu | 99,328 / 62,464 / 99,840 | same | no rung moved |
| `HIBER.DRV`, `CTRL.DRV`, `DISKOPS.DRV`, `FORMAT.DRV`, `DOCK.DRV` (unpacked image) | 5,529 / 12,346 / 10,352 / 1,136 / 2,233 | same | 0 |

`f54e1a99` is `df8603a1` plus the brief's own fix (below) and is
byte-identical to it in every image. **Against the concept's unpassed cost:
kern_big -97 of +234 (41%), kern_small -23 of +82 (28%).** On kern_small
the concept is the loader's 40, `OSAPI_FERR_TEXT`'s 25 and the bank's 15;
the 25 is a 19-byte body with no shorter spelling (REFUSED) and most of the
40 is a user-facing sentence, so the 28% is close to what the code holds.

## TAKEN

Five commits. Every kernel built (`make -j2`, `make -j2 small`, `make -j2
emu`), the fast tier 43/43 after each, `tools/stkbalance.py` over
`driver.inc`, `loader.inc`, `disk.inc`, `drvvol.inc`, `memory.inc` base
against tip: the same 14 unbalanced paths, none new.

### 1. `LD_EVER` folded into `ld_hdr_ok` (both kernels, `.cold` -16)

`ld_hdr_ver` re-read the magic and the version after `ld_hdr_ok` had just
refused, to decide `LD_EVER` against `LD_EBAD`. `ld_hdr_ok` now says which:
`mov al, LD_EVER` between the version compare and its `jne` (a `mov` writes
no flag), and every other refusal jumps to `ld_hdr_size.bad` - the body right
below it, which already was `mov al, LD_EBAD / stc / ret` - so `ld_hdr_ok`
lost its own `stc` too. `ld_check_hdr` takes the verdict as it comes (`jc
.out`). The other two callers (`drv_check`, `ld_start`'s re-home arm) read
nothing in AL after a refusal - `drv_check` loads `DRVR_CLASS` into AL on
success and `.bad` is its own; `.abort` writes `LD_EABORT` - so AL being
clobbered on the success path is harmless to all three. Measured as the
span `ld_hdr_ok`..`ld_say_status`: 197 -> 181 on both kernels.

### 2. `drv_fs_has` hands `drv_fs_call` the proc (kern_big `.cold` -33, `.bss` -2)

`drv_fs_call` asked `drv_fs_has` whether the verb was published - which
reads `[es:di]`, the cell - and then rebuilt ES:DI from `[drv_fssv]` and
`[drv_fscur]` and read the same cell again (20 bytes). `drv_fs_has` now
leaves BP = the proc on CF = 0 (`mov bp, [es:di] / cmp bp, 1 / jnc`: the
compare borrows exactly on 0), and `drv_fs_call` goes straight to the busy
hatch and the far call. Its only other caller, `fcp_mkroot` (`filecp.inc`),
already banks BP round it. And `drv_fs_seg` hands the drv_svc copy back in
DI instead of a `[drv_fssv]` word that both of its callers read straight
back into DI: the word is gone, `disk_mount_x`'s harvest site loses its
`mov di, [drv_fssv]`, and the body computes the segment first (`drv_cls_fp_x`
into BX) and the copy second, so the copy's refusal is the routine's.

### 3. `drv_cls_svc_x` maps a class through a table (kern_big `.text` +8, `.cold` -14)

The class -> drv_svc slot compaction stepped over 3 (retired), 6 and 7 (no
copy) with a special case for 8 and a byte multiply: 28 bytes and ~90
cycles. `drv_svcix` is a byte a class, `slot * DSV_SIZE/2 + 1` and 0 = no
copy, so the routine is `mov al, [di+drv_svcix] / cmp al, 1 / jc / cbw / add
ax, ax / add ax, drv_svc - 2 / xchg`: 15 bytes and no multiply, on the path
every redirected file verb takes (`drv_fs_seg`). Class 3 reads class 4's
slot, which is exactly what the compaction answered - nothing publishes
class 3 (SPEC.md 58) - so the map is the old routine's to the class.
kern_small keeps the compare chain (`%else`): its five classes do not pay
for a table, and nothing there is published at all.

### 4. `drv_release` asks the volume drop for every class (kern_big `.cold` -16)

`dsk_vol_drop_drv_x` drops the driver-kind rows whose `DV_CLASS` is AL, and
a row's class is the one `osapi_vol_fence` found its driver published
under - so the class IS the key. `drv_release` put a four-class ladder in
front of it (`DRVC_DISK`, `DRVC_NET`, `DRVC_FILE2`, `DRVC_FILE`), a list of
which classes serve volumes that had already gone stale twice: the
`DRVC_FILE` arm was missing once (unticking the RAM disk left a mounted
volume over a freed image), and 62.9.19 had to teach it `DRVC_FILE2`. Now it
is asked for every class on the owning path (the `[drv_owner]` test above
it stands); a class that registered no volume matches no row, and the walk
is once per unload. A mounted `.IMG` is `DRVC_IMG` = `0x41`, which no
driver class can equal. SPEC.md 51.8 says so (its "takes no argument"
sentence was already stale and is corrected with it).

### 5. The re-home bound in one `jae` (kern_big `.cold` -3)

Both forms of 20.12.10.5's third bound are asked of AX = image + bss - 1
(`ld_hdr_size`'s entry bound keeps image + bss above 32): bytes `dx > n`
is `dx - 1 >= n`, and the keep's `ceil(dx / 16) > p` is `(dx - 1) >> 4 >=
p`, so the round-up `add ax, 15` goes, the form is told by `jcxz` off
`[ld_rehsz]` loaded into CX, and one `jae .abort` serves both. CX still
reaches `mem_reown_x` as the keep (0xFFFF for the bytes form, stored by a
`mov` after the compare).

### 6. `drv_vol_bank` lets the flags go (both kernels, `.cold` -2)

78298be added `pushf`/`popf` round the bank so that its new compare would
not break "both preserve the flags". No caller of the BANK reads a flag after
it - every site (`desksc.inc` x2, `driver.inc` x3, `mod.inc`, `hiber.inc`'s
two direct calls and `hbm_vbn`'s five) writes or calls before it tests
anything - and the load-bearing half of that sentence was always the
BACK's, whose caller's CF and AX are the result. The contract now says bank
clobbers the flags; `drv_vol_back` is unchanged.

### 7. `drv_blob_of` indexes its map by row (kern_big `.text` -4, `.cold` -12)

51.9.2's two port pins filled rows 0 and 2, so every row 0..3 owns a blob and
`drv_blob_map`'s searched (row, size, offset) records - 4 bytes each, a
`loop` and a CX bank - become (size, address) records indexed by the row, 3
bytes each, with `drv_cfg`'s base folded into the address at assembly:
`osapi_drv_cfg_x`, the routine's one caller, added it at run time. kern_big
only (`OS88_DRIVERS`); `DRV_NBLOB` is derived from the table.

### 8. `ld_check_hdr` reads the flags byte once (both kernels, `.cold` -4)

Four `test byte [es:si+LD_H_FLAGS], n` (5 bytes each) become `test dl, n` (3)
off one `mov dl, [es:si+LD_H_FLAGS]` placed between the high-word `or dx, dx`
and its `jz` - `mov` writes no flag, and the high word is not needed after
the jump. DX is a documented clobber and `ld_hdr_size` rewrites it next.

### 9. `ld_hdr_size`'s entry bound asked image-first (both, `.cold` -1)

`cmp [ld_img], ax / jbe .bad` in place of `cmp ax, [ld_img] / jae .bad`: the
not-taken `jbe` leaves CF = 0, so the success path's `clc` goes.

### 10. The `DA`-less fill through AX (kern_big blob -5)

`CFG_BOOT`'s `_unpack` filled a missing `DA` record (an old SYSTEM.CFG,
51.5.4) with `push word [CFG_WANT] / pop word [CFG_ANS]`. AX is loaded with
`CFG_WANT` for `_want_set` right after, so the load moved up and the fill is
one `mov [CFG_ANS], ax`. Blob only (`ovc`); CTRL.DRV's instance is
`CFG_SHARED`/`CFG_SAVE`, which carry no fill.

### Hot paths

None of these is on a hot path. `drv_cls_svc_x` and `drv_fs_has` are on
every redirected file verb (a cable round trip or a RAM disk store walk),
and both got faster: the byte multiply (~70 cycles) is gone from the first
and the second read of the verb cell (~30) from `drv_fs_call`.

## REFUSED

* **`ld_check_hdr`'s image bounds** (`cmp ax, LD_HDR_SIZE / jb` and `cmp
  ax, APP_MAX_SIZE / ja`): 10 bytes on both kernels, and redundant as a
  refusal - `ld_hdr_size`, reached on every accepted path, requires
  `LD_HDR_SIZE <= entry < image` and `image + bss <= APP_MAX_SIZE`. Refused
  because the ladder between them can then refuse first with the OTHER code
  (a short image in a >64KB file reads `LD_EBIG`, an oversized image past a
  truncated file `LD_EBAD`), which `LDDIAG` builds show and `OSAPI_PKG_START`
  hands a launching package in AL. A behaviour change for 10 bytes.
* **`osapi_ferr_text`** (19 bytes of body): every spelling found is 19
  (default-first with `cmp ax, FM_NERR / jae`, or the index doubled first -
  which also lets a hostile `0x8005` read code 5's words, against the
  routine's unsigned rule). 0.
* **`drv_vol_bank` without the select's second branch** (bank `0xFF` as "no
  position" and teach `drv_vol_back` to skip it): +1 net; and a flag-free
  select (`xlat`/`lea`) is longer than the branches.
* **`assoc_sweep`'s `FSCAP_LOCAL` read through a shared row entry of
  `drv_fs_seg`**: -5 at the site, +7 in `drv_fs_seg` for the second entry
  (it needs a carry-in to keep `.none` shared). +2 net.
* **`dsk_vol_add`'s kind test as `cmp al, DRVC_FILE / jb .kindpub`** (-4):
  a mounted `.IMG` registers through the same `.live` with `DRVC_IMG` =
  `0x41`, which would become a `DVK_FILE` volume. Every two-compare form
  is the 8 bytes it is.
* **Banking the mounted volume's `DV_CLASS` at mount** so `drv_fs_seg`
  skips `dsk_vol_row_x` (~-3 net): 62.9.19 resolves the class per call on
  purpose, and a dropped volume leaves `[dsk_vkind]` stale - a banked class
  would then dispatch to whatever driver holds that class next.
* **The KEEP form converted to bytes in the cell** (saturating `CX * 16`
  into `[ld_rehsz]`, so `ld_start` keeps its one-compare bound): the cell
  grows by more than the bound shrinks, +3 net. And **`mem_reown_x`'s keep**
  (10 bytes) has no shorter spelling - the min written through AX is 16.
* **`HIBER.DRV`'s +4** (`cmp al, DRVC_FILE2 / je .asked`): `cmp al,
  DRVC_EMS / ja .asked` is the same 4, and a skip-list mask needs CL, which
  holds the row counter. Module bytes, 0 resident either way.
* **`CFG_SHARED`'s `_want_get` and `_ans_get` as one walk** answering both
  bitmaps: -1 per instance (the shift per row moves into the body, `_pack`
  then banks DX). One instance is CTRL.DRV's (agent ctrl's image).
* **`drv_boot_x`'s DA/`DRVE_NOVOL` ladder around `or [drv_nerr], al`**: -2
  of blob, resting on `DRVR_ANS` being exactly 0 or 1 everywhere it is
  written. Not worth the dependency for blob bytes.
* **`drv_sniff`'s hard-disk probe** (window +30): the `push es/di` pair is
  `drv_sniff`'s every-register contract, and nothing else is shorter.
* **`fm_s_ldver`** (`'Wrong os8088 version'`, 21) and its `fm_stattab` word:
  the user's words, and `tests/thewire.py`/`pkgfmt.py` read them.
* **Class 8's copied service table** (36 of `.bss`): `RAMDISK.DRV` publishes
  every `DSV_` cell, its Control Panel page and `DSV_PKGCALL` included, and
  the kernel reads them from the copy. Needed.

## WANTS AN ABI BREAK

* **`OSAPI_PKG_REHOME` with the KEEP form only.** Retire the bytes form (AX
  != 0) and the bound is `(dx - 1) >> 4 >= [ld_rehkp]` alone: ~-10 of
  `ld_start`'s `.cold`, the cell's AX store and `[ld_rehsz]` (-2 `.bss`, ~-4
  cell) - ~-16 kern_big, ESTIMATED. Breaks every `.o88` whose loader was
  built before 20.12.10.5 (`DOS.O88`, Clear Skies, Word, Pixelstein,
  `tests/rehome` all rebuilt with `OS88_REHOME`, but an old copy on a disk
  would be bounded wrongly), so it is a `PKG_FMT` bump.
* **`DRVC_FILE2` renumbered below the uncopied classes** (it 6, `DRVC_POINT`
  7, `DRVC_EMS` 8): the copied classes are then 1..6 less the retired 3, and
  `drv_cls_svc_x` is the old `>= DRVC_POINT` refusal again with no table -
  ~-8 kern_big (`drv_svcix`), ESTIMATED. Breaks `RD_CLASS`, `USBMOUSE.DRV`'s
  and `EMS.DRV`'s declared classes and every package naming them; `DRV_VER`
  bump. Probably not worth it alone; worth folding into the next bump.

## CROSS-FILE

* **`kernel/filecp.inc` `fcp_mkroot`** (agent: whoever owns filecp): its
  `push bp / mov bp, FSV_ENUM / FCPX drv_fs_has / pop bp` was a courtesy and
  is now LOAD-BEARING - `drv_fs_has` leaves BP = the proc. Nothing to change;
  do not remove the bank.
* **CTRL.DRV (agent ctrl)**: `CFG_SHARED`'s two bit walks (above) are one
  byte per instance if merged; and CTRL.DRV's own `cp_clssvc` thunk is
  unchanged (it far-calls `drvf_drv_cls_svc`, whose contract is unchanged).
* **`kernel/driver.inc` `drv_release`'s HMA lines** (agent hma) sit just
  above and below the ladder this removed; no HMA line was edited.
* **The brief itself**: `docs/plans/completed/ksp12/BRIEF.md` lines 14 and 147 cite
  "section 1.2" meaning the P12 handoff's 1.2, which `checkdocs` reads as SPEC.md
  1.2 and fails `make` on; and the new plan file made `docs/INDEX.md` stale.
  This branch's first commit fixes both (the section sign dropped, the index
  regenerated). Every agent hit it; the coordinator will see the same two
  lines from each branch, and the index will need one regeneration after the
  merge in any case (each branch adds its own `ksp12/*.md`).

## ROWS RUN

(MartyPC unless named, `--marty-jobs 1`, on this branch's tip build.)

* **Gates**, after every batch: `make -j2` (its fast tier 43/43), `make -j2
  small`, `make -j2 emu`, `tools/checkdocs.py` clean; `tools/stkbalance.py`
  base against tip over the five files touched, identical (14 and 14).
* **Soak, scoped, at the tip `888b78e6`** (one run, `--marty-jobs 1`): 23
  passed - `fsclass`, `rdup`, `rdmount`, `rdicon`, `rdmove`, `drvcall`,
  `hdauto`, `drvup`, `cpnames`, `pkgfmt`, `rehome`, `rehomemove`,
  `rehomemove360`, `rehomeabort`, `dosarena`, `regrowshed`, `saver`,
  `hibernate`, `assocsweep`, `assocvol`, `imgmount`, `drvmove`, `small128`.
  `netcoexist` (QEMU) and `mpupin` FAILED in that run on a missing host
  tool (`mtype` / `mcopy`: the container had no `mtools`) before testing
  anything; `apt-get install mtools`, and both re-run green (2/2) - so the
  port pins' read through the re-indexed `drv_blob_of` is covered on both
  rows, `EP` on QEMU's NE2000 and `SP` on MartyPC's MPU.
* **Broken on purpose**: `drv_svcix`'s class-8 byte set to 0 (the RAM disk's
  class refused) - `fsclass` goes red, `the RAM disk mounted a class-8
  volume (rows [])`. Reverted and rebuilt; the tip build is the measured one.
* **Equivalence where no row reaches**: `drv_svcix` against the compaction
  it replaced, class by class 1..8 (1->0, 2->1, 3->2, 4->2, 5->3, 6 and 7
  refused, 8->4, out-of-range refused by `drv_cls_idx` as before), and the
  re-home bound's two forms by the integer identities in item 5; the
  `rehome*` rows run the KEEP form, `dosarena` the DOS loader's.

## DEFECTS FOUND

None in behaviour. Two things worth knowing:

* `drv_cls_svc_x` maps the RETIRED class 3 onto class 4's slot (`DRVC_NET`),
  and always has: `drv_cls_idx` accepts 3 and the compaction only steps
  over it. Nothing can publish class 3 (no row declares it), so it is
  unreachable; `drv_svcix` keeps the mapping so the change is exact, and
  says so. A `0` there would make it a refusal, for 0 bytes, if anybody
  wants the belt.
* SPEC.md 51.8 still said `dsk_vol_drop_drv` "takes no argument and wants
  none" years after it took the class in AL; corrected with item 4.
