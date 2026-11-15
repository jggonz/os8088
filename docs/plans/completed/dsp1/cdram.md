# Driver size pass 1: cdram (`drivers/cdrom/`, `drivers/ramdisk/`)

Branch `dsp1-cdram`. Base `de93581a` (the brief's commit on `3b16b60b`).
The coordinator called time partway through, so **CDROM.DRV is done and
RAMDISK.DRV / RAMPAGE.DRV were read but not changed**. What was queued for
them is in section 10, NOT REACHED.

## 1. BASE AND TIP

| driver | | image | bss stripped | file | 512-byte granules (RAM) |
|---|---|---:|---:|---:|---:|
| CDROM.DRV | base | 9,038 | 320 | 5,233 | 18 |
| | tip | **8,203** | 0 | **4,644** | **17** |
| | delta | **-835** | | **-589** | -1 |
| RAMDISK.DRV | base = tip | 5,400 | 112 | 4,597 | 11 |
| RAMPAGE.DRV | base = tip | 2,438 | 80 | 2,171 | 5 |

**Clusters.** CDROM.DRV is on no 360KB disk (`SYS360OMITDRV`: an 8-bit bus
cannot run it), so the 360KB system disk's cluster count does not move: it
is still 37 of 706 free. On the 720KB, 1.44MB and 1.2MB system disks and
kern_mega's disks the file is 589 bytes smaller; whether that frees a
cluster depends on the geometry's cluster size (5,233 to 4,644 crosses 5,120
on 1,024-byte clusters, so it is 6 to 5 there, and 11 to 10 at 512).

RAM: the image is a heap claim rounded to 512, so it is **one granule (512
bytes) less** on every machine with the driver ticked. That is what
`t_drvmem` saw (section 6).

## 2. TAKEN (CDROM.DRV, one commit)

Image bytes per routine come from the `[map all]` maps of base and tip;
the total is -835 image and -589 file. Hot paths are `cd_packet`'s data
phase, `cd_bytes` and `cd_bnorm` (CDROM.DRV refuses below a 286, so the
machine is a 286 or better).

**Callee-saves no caller reads** (all cold unless marked):
- `cd_attach` (-11): its BX/CX/DX frame round `OSAPI_CPU_INFO` (keeps all
  but AX) and `cd_probe` (keeps all) protected nothing; exits rebuilt.
- `cd_packet` (-22, **hot**): only DX is banked now - `cd_cmd` banks BX, CX,
  SI, DI, ES round it and `cd_sense` is only reached from inside `cd_cmd`.
  Also `mov al,0` before the BCL write (AL is already 0), `lea dx,[STAT]` as
  `inc dx` x2, the redundant `.done: clc`, and `.empty` falling into `.tmo`.
  The data-phase loop (`.phase` .. `.dr`) is instruction-identical and at
  the base's parity (551/563 odd, `.drain` even, as before).
- `cd_wait` (-5), `cd_select`+`cd_delay` (-9), `cd_tmo_set` (-4),
  `cd_tmo_up` (-8; `sbb byte [cd_ihi],0` is the two-step borrow exactly),
  `cd_sense` (-12), `cd_dopen` (-10), `cd_bytes` (-16, **hot**: the five
  register pushes its two callers already bank, one `mov cl,11` by reordering
  the shift, and `les di,[cd_boff]` for two loads), `cd_mkcda` (frame),
  `cd_stage` (-19), `cd_parent` (-13, and a dead store to `cd_tmp2`),
  `cd_setcwd` (-11, now a tail into `cd_wcopy`), `cd_ptbyte`, `cd_dotdot`.
- `cd_ident` (-26): IDENTIFY's word 0 rides the stack (`cd_w0` gone), the
  model's NUL is never written (0 from the load, nothing writes byte 40),
  the packet size is `6 + 2*al`, the trim stops on `cd_pkw`'s high byte (a
  sentinel, asserted adjacent), the ES bank dropped (`drv_stamped` restores
  ES for DRVV_ATTACH), and `CLC_OR_STC`.

**Shared bodies**: `cd_wzero` (one zero fill, keeps AX) behind `cd_pclear`,
`cd_ezero` and `cd_forget`; `cd_pclear` clears `cd_xmax`/`cd_xout` in the
same fill (reordered behind the packet; `cd_xseg`/`cd_xoff` deliberately
not); `cd_mcore` is the one clamp-and-divide behind `cd_msf` and
`cd_msfle` (-62 between them); `cd_sopen` feeds `cd_cwdopen` and
`cd_enum`; `cd_mediak` is the `push ax / cd_media / cd_ferr` prologue four
FSV verbs each spelt out; `cd_readat` ends in `cd_read`'s tail; `cd_media`
answers the stale flag in ZF (three `cmp byte [cd_stale]` gone).

**Tables and data**: the 22 constant bytes of a TRACKnn.CDA are `cd_cda`'s
load-time value, not 66 bytes of immediates (-86 in `cd_mkcda`);
`cd_readvol` fills its five dwords from a 10-byte map (-46); the driver's
name and the volume label are one string (-7); `cd_bases` is walked
backwards so the end test is the borrow.

**Registers instead of memory**: `cd_mangle` (-78 and `cd_put`'s 14):
index in BX so the name byte is `[bx+si]` (the 6-byte `CD_NCH` macro x7
gone), L in BP, output by `stosb` straight into the caller's DI - `cd_stage`
mangles into `cd_ent` with no copy - and the hash on the stack. `cd_enum`'s
ordinal in DI; `cd_v_play`'s end LBA in BP:SI (`drv_pkg_call_x` saves BP);
`cd_opos`, `cd_dot`, `cd_hash`, `cd_w0`, `cd_ord`, `cd_tmp2`, `cd_vpage`
and the never-used `cd_nlen` are gone (15 bytes of state).

**Speed, taken for nothing**: `cd_sbuf` was at an ODD address (6741) and is
filled by `rep insw`; every word cell `cd_packet` and `cd_bytes` touch per
block (`cd_xgot`, `cd_xmax`, `cd_pos`, `cd_left`, `cd_boff`, ...) was odd
too. The state is reordered so every word is even; the one `align 2`
before `cd_stag` costs 0 while the code ends odd (`cd_vol` takes the odd
byte), and a second at `cd_sbuf` is a guard (0 now).

**Equivalence.** A unicorn harness (scratch, not committed) runs base and
tip images with the OSAPI cells answered in Python and a port-level fake
ATAPI drive: 10 discs (data, mixed-mode, CD-Extra, audio-only, no disc,
UNIT ATTENTION, becoming ready, no audio commands, 1,000-byte DRQ blocks,
short reads) x 3 states (real mode, `CPU_F_PM` set, a full listing) -
attach twice, READY, every CDV_* verb including PLAY/VOLUME/STATUS and the
icon, then a recursive walk doing LIST, ENUM by every ordinal, STAT of every
name, READ at three capacities and READAT at eleven offsets. Every
register at every return, every FS_ENT/FS_PROG/VOL_* call, every buffer
hash and **every ATAPI packet** compared. Against a harness-only tip that
re-creates defect 1's AX, **all 30 transcripts are identical**, so the only
behaviour difference is the fix; no command was added anywhere. Masked:
BP after a package verb (the dispatch's own jump register; the door
restores it), ES after attach (`drv_stamped` restores it), and SI after a
refused CDV_STATUS (the address of our own buffer, which moved). Plus 14
probe layouts (both units, all four channels, ATA disk, tape, bad packet
size, busy, empty bus, blank and full model) identical including every port
write, and a 20,010-name `cd_mangle` fuzz identical to base and to
`tools/os88cdiso.py`'s reference.

**Hot-path cycles.** Every change on the hot paths only removes
instructions (pushes/pops, one `mov cl`) or replaces two `mov` with one
`les` (286 real mode 7 clocks vs 10; protected mode 21 vs 24). Executed
instructions under the harness, base -> tip: READ of a 200,000-byte file
104,999 -> 104,563; READAT 8KB 4,777 -> 4,692; READ 4,096 bytes 4,234 ->
4,083; LIST of the root 13,937 -> 10,854. The `rep insw` destination is now
word-aligned. Two call targets moved to odd (`cd_read10`, `cd_bnorm`,
once per run of up to 16 sectors) and `cd_wait.poll` is odd (a status
poll, bounded by time).

## 3. REFUSED

- `xchg ax,cx` for `mov cl,al` in the DRQ-length read (-1): 3 clocks
  against 2 on a 286.
- `cd_bytes .whole` computed before the high-word test (-4): slower on the
  more-than-64KB-left path.
- `les bx,[cd_boff]` for the `cd_xseg`/`cd_xoff` copy in `cd_bytes .k`
  (-4): faster in real mode, ~10 clocks slower under kern_mega's protected
  mode (a segment load).
- `cd_enum` reading its buffer pointer off its own frame through BP (-4):
  needs a BP bank on a kernel-called verb.
- Mirroring the icon masks at run time (96 bytes of data): a buffer and code
  cost more image than the data.

## 4. WANTS THE OWNER

Nothing. No string, probe, limit or setting changed.

## 5. WANTS AN ABI BREAK

Nothing. `cdpkg.inc` (shared with apps/cdrack) is untouched.

## 6. CROSS-FILE

- **BLOCKING the fast tier: `kernel/driver.inc` `DRVM_IMG_CD equ 18` must
  become 17.** `tests/unit/t_drvmem.py` (row `drvmem`) holds the Drivers
  page's RAM figure to `ceil(cdrom.drv / 512)`, and the image is now 17
  granules. Off limits to me; a one-character edit, checked here (the row
  passes with it, then reverted). Until it lands, `make` stops at
  `test-fast` on `drvmem` alone - `make -k` shows nothing else failing.
- **Attach-only code (CDROM.DRV), priced**: `cd_attach`, `cd_probe`,
  `cd_ident`, `cd_bases`, `cd_select`'s probe use aside: about **200
  bytes** (31 + 53 + 111 + 8) of an 8,203-byte image, all needed only once.
  Not worth a shrink-claim mechanism on its own.
- **Guide stale**: `guidelock` warns that the CDROM_DRV manual page
  predates this change. Only the subfolder TRACKnn.CDA listing (defect 1)
  is user-visible; whoever owns the guide should check it says .CDA files
  are in the root, and restamp.

## 7. ROWS TO RUN

`cdrom`, `cdromsim`, `cdromempty`, `megacdrom` (86Box 286, kern_mega),
`win16mscdex`, `x3live386`; the fast tier's `drvmem` (after the
DRVM_IMG_CD edit), `drvverb`, `megapkg`. `cdrom --disc` lists every folder
against `tools/os88cdiso.py`, so it is where defect 1's fix is seen; the
QEMU disc has no audio tracks, so `cdromsim` (three fake ones) is the row
that shows the subfolders no longer list them.

## 8. LOOKED

No boot. The coordinator called time before it; the harness above stands in
for it, and the rows in section 7 are the check.

## 9. DEFECTS FOUND

1. **FIXED - subfolders listed the root's audio tracks.** `cd_resolve`'s
   copy of a record's extent and length went through AX, so it returned
   AX = the length's high word (almost always 0). `cd_chdir` stores AX as
   `[cd_cwd]` and `cd_enum` tests it as "the root", so on any disc with
   audio tracks every subfolder's FSV_LIST, FSV_ENUM and FSV_STAT also
   served TRACKnn.CDA - SPEC.md 119.3.5 puts them in the root only. The copy
   is now `cd_wcopy`, which keeps AX. Found by reading, confirmed by the
   harness (the only difference left once it is re-created).
2. **FIXED - a hostile TOC could make CDV_PLAY divide-fault.** `cd_msf`
   clamped the LBA's high word to 4,499 and then added the 150-frame
   lead-in, which can carry it to 4,500: the `div` overflows, and on the
   286+ this driver runs on INT 0 re-executes for ever. `cd_mcore` adds the
   lead-in between the negative and the high clamp; every input that did
   not fault gives the same MSF as before.

## 10. NOT REACHED (RAMDISK.DRV, RAMPAGE.DRV)

Read in full; no edit made. The brief's question first:

- **`ramdisk.asm`'s `incbin "mines.o88"` costs the shipped images
  nothing.** It is inside `%ifdef RDSEED`, set only by `make ramseed`
  (`RAMSEED=1`): the `make ramseed` harness seeds the RAM disk with two
  folders, three text files and a real package to click (SPEC.md 62.9.5.1).
  The shipped RAMDISK.DRV does not contain it.

Queued, with rough prices (unmeasured):
- `rd_extkb`/`rd_extb`/`rd_extpara`/`rd_sext` have non-zero load values that
  nothing reads before `rd_geom`/`rd_store_get` writes them: make them 0 and
  move them into the trailing zero run (file only, a few bytes).
- Error-tail triplets `rd_e_*`, `rd_e2_*`, `rd_e3_*` (three copies by
  design, for short jumps) - check which still need all three after other
  cuts (~10-20).
- `rd_meta_check`'s and `rd_meta_apply`'s bounds chains, `rd_cfg_load`/
  `rd_cfg_mark` shared block copy, `rd_chain_len`/`rd_chain_nth`/
  `rd_chain_free` walk loops (one walker with a mode, ~20-40).
- `rd_page_need`'s and `rd_page_check`'s header tests against
  `OSAPI`-side constants (~10).
- `rd_pkg .upcase` and `rd_settype` both call `rd_upper` in loops; small.
- RAMPAGE.DRV is already tight (it has had a redraw pass); `os88ui.inc`
  (~570 bytes) is `apps/`, cross-file. RAMPAGE at 2,171 is 124 bytes from
  four 512-byte clusters on the 360KB disk; RAMDISK at 4,597 is 86 from
  eight.
- `rd_io` (hot) left alone: nothing seen there that is both smaller and not
  slower.
