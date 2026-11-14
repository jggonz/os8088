# ksp19 `drvtab` - the driver tables, the config record, the dirent mark

Kernel size pass 19 (the flag day), agent `drvtab`, branch `ksp19-drvtab`
cut from `kernel-size-19` at `174b80bf`. Rows: N1, D4, histsys-7, R3, F1,
F3, R2-1, R2-3 (BRIEF.md section 1), plus what the files they live in
offered. Every figure below is MEASURED: `tools/kernsize.py --json` on all
four kernels after `make`, `make small`, `make emu`, `make mega`, against
the brief's section 0.2 table, and routine spans off `tools/os88rdiff.py
kernel 174b80bf` (kern_big). Packages and drivers: their images assembled
at the base and here.

## Totals

| kernel | `.text` | `.bss` | `.cold` | resident | notes |
|---|---:|---:|---:|---:|---|
| kern_big | -17 | -199 | -13 | **-229** | `.boot3` -32 (transient) |
| kern_emu | -17 | -199 | -13 | **-229** | as kern_big |
| kern_small | 0 | 0 | 0 | **0** | no driver layer (SPEC.md 51.0); F3 is a constant |
| kern_mega | -19 | -199 | +4 | **-218 conventional** (`.text`+`.bss`) | `.cold` is above 1MB there; R2-3 adds 128 bytes to the tables' block above 1MB |

Modules (not resident): CTRL.DRV -33 `.modu`, +1 `.modc`, -5 `.modcb`;
HIBER.DRV -25. Drivers: SOUND.DRV -3, EMS.DRV +34 image (same 1KB
claim, zeros so a few file bytes), HDD.DRV 4,096 either way (two attach-only
routines traded places with its table, below).

## TAKEN

**histsys-7 - every class's table read IN PLACE but SOUND's.** `drv_svc`
was `DSV_SIZE * 7` = 252 bytes of `.bss` (seven copies, mostly zeros); it is
one 36-byte copy (SOUND's, which `snd_tick` reads inside IRQ0 and `snd.inc`
reads in twenty places) and `drv_svct`, the table OFFSET `DRVV_ATTACH`
answered, a word a class from class 2 (18 bytes). `drv_cls_svc_x` now
answers **ES:DI** (the class's published segment and that offset; for class
1, KERNEL_SEG:`drv_svc`) and refuses an unpublished class by `drv_owner`.
`drv_svcix` (10 bytes of `.text`) is gone, so **D1 and drivers-9 are moot**.
Spans: `drv_cls_svc_x` 23 -> 38, `drv_fs_seg` 38 -> 32, `drv_fs_has` 60 ->
27 (the FSV length test went with N1/D4), `drv_cp_closed` 43 -> 46,
`drv_cp_class_x` 36 -> 39, `drv_release` 91 -> 93, `drv_pkg_call_x` 98 -> 95,
`assoc_sweep` 92 -> 90, `dsk_xfer` 980 -> 982, `disk_mount_x` 798 -> 800,
`desk_pic` 150 -> 151; `.bss` -199. Callers bank ES where it is live
(`dsk_xfer`'s two, `drv_cp_closed`, `drv_cp_class_x`, `drv_pkg_call_x` -
the caller's ES the driver reads its buffer through - HIBER.DRV's geometry,
CTRL.DRV's `cp_drv_ev`); `fsx_rdrvf` (kern_mega) reads through DS.
Cycles: `OSAPI_DRV_CALL` +~25 (a push/pop of ES and a segment prefix,
PREDICTED), the file verbs' `drv_fs_has` FASTER (no header read, no length
compare), a block transfer +~20 per driver call (PREDICTED). No hot path.
**A driver's table must now stay where it said until detach**: HDD.DRV's
was in `hd_mbr`'s attach-only bytes (the first partition read overwrites
them) and moved out; to keep the image at 4,096 the attach-only
`hd_at_new`/`hd_at_geom` (38 bytes) moved into the run in its place
(`hd_mbrok` at DFBh, 4 bytes of slack before the next 512).

**N1 + D4 - the floors.** `DSV_FLOOR` 36 and `FSV_FLOOR` 30 in
`drivers/os88drv.inc`; `OS88_DRV_END` refuses a fourth `OS88_DRIVER`
argument that is not `DSV_FLOOR` (every driver in the tree passes);
`kernel/driver.inc` refuses to assemble if `DSV_SIZE`/`FSV_SIZE` pass the
floor without the length test coming back. `DSV_TAB0`/`FSV_TAB0` and both
fallbacks are gone, and so is more than N1 priced: `drv_fs_has`'s whole
`DRV_H_FSV` read and compare (with its HMA arm), and in `drv_publish` (the
`DRV_ATTACH` macro: `.boot3`, CTRL.DRV, HIBER.DRV, -30 each) the zero-fill,
the +15 read, the 3Fh mask, the 28 fallback and the clamp - the copy is
`DSV_SIZE` words, whole. +10 and +15's length bits are written and unread
(+15's bits 7/6 are still kern_mega's).

**EMS.DRV gets a real table (found here).** Its one cell rode
`drv_fptr7`'s offset half because the copy was 36 bytes for one word; with
nothing copied, EMS.DRV publishes a whole table (34 zero bytes + the door)
and the special cases leave `drv_pkg_call_x` (-9 `.text`), `drv_publish`
and kern_mega's `fsx_rdrvf` (-9). A clear win: every kernel's bytes against
34 of a driver's image inside the same 1KB claim.

**R3 - the volume bridge.** The kernel hands `SNDV_MIXPOP` DI = the bridge's
offset, far-called at `KERNEL_SEG`:DI; the 4-byte `ui_volume_bridge` record
is gone (-4 `.text`), SOUND.DRV -3. Private verb, `DRV_VER` already bumped.

**F1 - Force Color folded.** It is bit 7 (`CFG_VGCB`) of the `VM` record's
mode byte (`CFG_VGC` = `CFG_VDM`); the `VC` key went (`CFG_NB` -1 `.bss`,
`CFG_NKEY` 15, `CFG_FBUF` 164 -> 159). The boot masks the bit before the
mode's range check, the pack writes the mode under it, so `VM`'s ver stands
(an old file's mode byte has the bit clear) and a user sets Force Color once
more. `tools/os88stik.py`'s `CFG_FBUF` mirror and `tests/vgacolor.py`
follow. Non-resident: key -5 and pack +6 in each of `.boot3` and CTRL.DRV,
the panel's toggle -0.

**F3 - the compression mark at A4h/A5h**, bits 3 and 4 clear, straight to
the new mark (no dual reader: every disk is rebuilt and nothing reads an old
one - the hint is a cache, and an old disk's file still opens through the
'CZ' header sniff). `DSK_CZ_MARK`, `tools/os88disk.py`'s `CZ_HINT`
(`os88hdd.py` imports it), four tests that typed 5Ah/5Bh. 0 bytes.

**R2-3 - `MEGA_WTN` 32 -> 64.** +128 bytes of the tables' block above 1MB,
0 conventional; SPEC.md 50.6.11 says 64.

**Parity**: `dsk_xfer`'s PARITY nop moved to `disk_mount_x`'s OS88_REDIR arm
(kern_small keeps its own), `assoc_sweep`'s site rewritten to an even -2
(the pass is the bit, `and al,1 / shl al,1`; `FSCAP_LOCAL` = 1 asserted).
`t_hotpar` green on kern_big and kern_small.

## REFUSED

- **R2-1 (`mega_hibnc`, 235 `.mega`)**: no longer a clean delete. Since
  KERN-MEGA-PLAN 14.156 the FILE LAYER hands a floppy run into a pool claim
  to `mega_fd13` natively, and the native path hands back to the BIOS on any
  failure but write-protect and on a set change line - the first read after
  every disk swap - which then reaches `mega_hibnc` for a buffer above 1MB.
  Refusing 09h there makes that read fail three strikes and "Disk error";
  `dsk_xfer` has no 09h-to-bounce path. Taking it needs either `dsk_xfer`
  bouncing on 09h (+~15 `.cold` on kern_mega, a second revolution per
  hand-back) or the 14.156 native-into-pool path reverted (the 22 kHz
  regression it fixed). Price recorded; not taken.
- **The table offset in the driver header** (+10, now unread) instead of
  `drv_svct`: -18 `.bss` but +~14 `.cold` for the HMA header arm in
  `drv_cls_svc_x`, every driver's `OS88_DRIVER` naming its table, SOUND's
  `DRVV_TIER` table still dynamic. ~-4: recorded, not taken.

## WANTS THE OWNER / WANTS AN ABI BREAK

None new beyond the above. Nothing taken needs a further bump (`PKG_FMT` 13,
`DRV_VER` 14 as the brief set them); `tools/abi.lock.json` re-run with
`--update` and no bump.

## ROWS (the final pass's union)

Fast (ran green here): `stik`, `drvmem`, `installabi`, `hotpar`, `clscf`,
`mirror`, `abilock`, `api-abi`, `bootfence`. Ran here (mechanism):
`rdup`, `cpnameshdd`, `ems` - all ok. For the final pass:
drivers and tables - `fsclass`, `cdrom`, `cdromsim`, `megacdrom`,
`httpmount`, `httpmountname`, `httpinstall`, `kdhdd`, `hddcp`, `hddgeom`,
`megahddtick`, `install`, `cpnames`, `rdpreserve`, `emspico`, `emscard`,
`emscard2`, `videms*`, `vbtems*`, `vidrmbems`, `vidrmbhdd`, `megarmb*`,
`ftpd*`, `telnet`, `ethernet`, `wirechat`, `wirezone`, `thewire`;
sound and the bridge - `mixervga`, `mixercga`, `mixerherc`, `mixersb16`,
`mixervesa`, `kdmix`, `sndkeep`, `covox*`, `vidsound` family; assoc -
`assocsweep`, `assocvol`, `assocfind`; config - `vgacolor`, `cfgtrip`,
`fddpage`, `dispmode`, `dispmodex`, `extdmod`, `megarmbext`; mark -
`lzmod*`, `lzship*`, `instdeep`, `instassoc`, `rdcz`, `megacz`; hibernate
- `hibernate*`, `hmahiber`, `megahiber`, `megahibfull`; windows -
`megapxhuge`, `megapxhugez`.

## For the next wave

- `drv_cls_svc_x` CLOBBERS ES now (its output). A new caller banks ES if it
  needs it; a new reader of a driver's table writes `[es:di+DSV_*]`.
- A driver may not keep its service table in attach-only or dropped memory
  (`drivers/os88drv.inc`'s `DSV_FLOOR` note, SPEC.md 51.2).
- HDD.DRV has 4 bytes before its image grows to 4.5KB (`hd_mbrok` at DFBh).
- The ABI-BREAK-LIST rows N1, D4, histsys-7, D1/drivers-9, R3, F1, F3 and
  R2-3 are TAKEN and R2-1 REFUSED as above; the list's status column is the
  coordinator's to update at the merge.
