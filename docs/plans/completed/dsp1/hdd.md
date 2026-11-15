# Driver size pass 1: `drivers/hdd/` (agent hdd)

Branch `dsp1-hdd`, from `de93581a` (the brief's commit on `3b16b60b`).
Owns `drivers/hdd/`: HDD.DRV (`hdd.asm` and its includes) and HDDTOOL.DRV
(`hddtool.asm` and its includes). **FSCHECK.DRV is not in this directory**:
it is `kernel/fsck.inc`, assembled with the kernel and cut out by
`tools/os88mod.py` as module 10 (Makefile `KMODARGS`, and kern_mega's own in
`$(MEGADIR)`), so it is off limits here and was not touched. It is priced
under CROSS-FILE.

Nothing outside `drivers/hdd/` changed except one SPEC.md paragraph (52.11.8's
note on the tool's tuned pad, which named the layout this pass changed) and
`docs/INDEX.md` (regenerated). `hddabi.inc` is untouched, so `HD_ABI_VER` and
`tools/abi.lock.json` are as they were; no file outside this directory
includes any of the shared `hd*` headers.

## 1. BASE AND TIP

Measured from this worktree's own builds (`make -j2` at base and at tip) and
`os88diskuse.py --diff` against the base snapshot.

| driver | | base | tip | delta |
|---|---|---:|---:|---:|
| HDD.DRV | image | 4,096 | 4,096 | 0 |
| | resident span in front of `hd_mbr`'s `align 512` | 3,546 | 3,514 | **-32** |
| | bss | 16 | 16 | 0 |
| | file | 3,571 | 3,558 | **-13** |
| HDDTOOL.DRV | image | 16,508 | 16,153 | **-355** |
| | of which code/data (not the alignment pad) | 14,823 | 14,617 | **-206** |
| | pad in front of the sector buffers | 149 | 0 | -149 |
| | heap claim (`HDTOOL_PARA` paragraphs, 512-byte granule) | 16,896 | 16,384 | **-512** |
| | bss | - | - | |
| | file | 12,216 | 12,116 | **-100** |
| **total** | image | 20,604 | 20,249 | **-355** |
| | file | 15,787 | 15,674 | **-113** |

Clusters, base -> tip, on every disk that carries them (no change anywhere):

| disk | cluster | HDD.DRV | HDDTOOL.DRV |
|---|---:|---|---|
| `os8088-360.img` (37 free -> 37) | 512 | 7 -> 7 | 24 -> 24 |
| `os8088-720.img` | 1,024 | 4 -> 4 | 12 -> 12 |
| `os8088-120.img`, `os8088.img` | 512 | 7 -> 7 | 24 -> 24 |
| `mega.img`, `mega120.img` | 512 | 7 -> 7 | 24 -> 24 |

The bytes are the result; the clusters are only a consequence. For the record
of how far off the next one is: HDD.DRV at 3,558 needs 486 fewer file bytes
for 6 clusters, HDDTOOL.DRV at 12,116 needs 340 fewer for 23.

**HDD.DRV's image cannot move in this pass's units.** It is the resident
span padded to 512 by `hdsec.inc`, plus the 512-byte attach run laid over
`hd_mbr` (SPEC.md 52.13.6): 4,096 while the span is 3,073..3,584 bytes. The
32 bytes freed are headroom under that line (70 bytes of it now), and file
bytes. The pad is zeros, which lz4 packs to almost nothing, so on this
driver the file is the honest measurement of what a byte of code bought.

## 2. TAKEN

Every item is cold unless it says otherwise. **Each was measured for the file
as well as the image**, by a leave-one-out pass over the diff's hunks (each
hunk reverted alone, the pair re-assembled and re-packed, the delta read):
lz4 matches repeated byte runs, so a change that removes a SECOND copy of
something often saves no file at all, and four did not (REFUSED). The file
deltas below are those leave-one-out readings at the tip; they are not
additive (lz4's context shifts), the totals in section 1 are the measured ones.

### HDDTOOL.DRV (all cold: it runs while a person clicks, or between int 13h
transfers that cost milliseconds)

| item | code | file (leave-one-out) | equivalence |
|---|---:|---:|---|
| `hd_fmt_layout12` joins `hd_fmt_layout`'s ceiling/div/store/cluster tail (`.ceil`) from the same `os88_ent` frame; its dead `mov cx, bx` and `mov bx, ax` become an `xchg` | -34 | -19 | harness |
| `hd_tw_fmtok` and `hd_tw_delok` share one body (`hd_tw_okbody`): the state that refuses besides HTS_LIVE arrives in AH | -21 | -15 | harness |
| `hd_chs_pack`: two `ror` and the mask where six `shl` were (`hd_chs_regs`' idiom); no CX bank | -14 | -13 | harness: `(c & 3) << 6` for every byte |
| `hd_ibuf_get`: the refusal is `cwd`/`stc` falling into the success stores (AX and [si] are both the ladder's 0 there) | -15 | -17 | reading: AX = 0 off the `jz`, [si] the terminating 0 |
| `hd_cyl_up`: the LBA restored before the tests, `jcxz`, one exit | -9 | -15 | harness (registers; its flags are a documented clobber and both callers overwrite them) |
| `hd_cyl_trim`: `xchg`/`jcxz` | -3 | -10 | harness (registers and [hd_fsecs]; flags as above, its one caller overwrites them) |
| `hd_fmt_spc0`: one `mov al, 2` under the 4 | -5 | -13 | harness |
| `hd_isrc`/`hd_idst`/`hd_isrc_here`/`hd_idst_here`: one GOTO_Q tail | -14 | -13 (the `_here` pair; the other assembles only with it) | reading: four `os88_ent` frames of one shape |
| `hd_svc_res`/`hd_svc_resff`: `hd_svc_req`, then AL = HRQ_RES, the 0FFh preset folded in; `hd_vol_ask` is its head | -14 | -35 | reading |
| `hd_mnt_do`: the device goes through AH, which neither MOUNTDEV nor UNMOUNTDEV reads | -2 | -5 | reading: `hd_svc` dispatches on AL; both handlers read HRQ_DEV |
| the installer's refusals: `hd_ifail_err` for the disk-error caption (five sites), and `hd_kplan`, `hd_inst_keep`, `hd_inst_sys` each share one `call hd_ifail` | -21 | -29 (three hunks; the rest assemble only together) | reading |
| exits: `CLC_OR_STC` in `hd_kclus` and `hd_iksecs` (`hd_ivol_back` keeps the flags); `hd_iapps_scan`'s and `hd_isrc_ck`'s two exits, one tail each | -14 | -48 | harness (`hd_kclus`), reading |
| dead `clc` after a `jc` not taken (`hd_kverify`, `hd_inst_vbr`, `hd_inst_mbr`, `hd_inst_fmt`, `hd_inst_keep`) and after an equal compare (`hd_idst_child`) | -6 | -14 each | reading: CF is 0 on every way in |
| the copy into the caller's block (a slot's text, the caption, a log line) is one loop, `hd_pcopy`, where it was written out three times | -14 | -1, 0 (two sites alone) | reading: identical loops, DF = 0 from `hd_pkgv`'s `cld` |
| `hd_pv_go` ends through `hd_pv_apps`' `.step`/`.fail`; `hd_pv_stage` falls into its copy (a dead `jmp short`, and `hd_pv_out` is its local `.out`) | -21 | -8 | reading: the same instructions in the same frame |
| `hd_ilog_file` banks with `os88_ent` and runs `hd_ilog_f`'s body (`.body`) in its own frame | -7 | -4 | reading: `hd_ilog_f`'s ladder pops that frame; every register still kept |
| **the pad re-tuned**: `hd_tshut`, `hd_tbusy`, `hd_win_live` (78 bytes) and the install log's strings but one (241, `ipkg.inc` under `HD_IPKG_TEXT`) moved in front of the buffers, before `partw.inc` | 0 (image -319) | -9 | layout only: every routine byte-identical (`os88rdiff`), every string DS-relative |

The leave-one-out readings overlap and do not add (lz4's context moves with
every byte); section 1's totals are the measured ones.

The re-tune is the one item that is all image and no code: the pad in front
of `hd_mbr` had been LEFT at 199 bytes because the claim was whole KB and it
bought none (hddtool.asm's history note). The claim is paragraphs now, rounded
by the heap to 512 (SPEC.md 50.3.7), so pad is heap. The block went in front
of `partw.inc` because twenty-seven placements were tried (nine places,
the block whole or split) and that one packed
smallest (12,116; in front of the ladder it was 12,131, `inst.inc` 12,132).

### HDD.DRV (resident; nothing on a transfer path)

| item | resident | file | equivalence |
|---|---:|---:|---|
| `hd_s_page` (DSV_CPNAME) **is** the header's name field, `$$ + 16`: 'Hard Drive' is NUL-padded and ends at +26; the kernel has no `DRV_H_` name offset and writes only +4 - and, as part of the same item, `hd_xa_a20`'s `clc` after an equal compare, the byte that puts `hd_xa_read` back on a dword (below) | -12 | -1 (the pair; the `clc` alone reads +1) | reading; seen on the glass (LOOKED) |
| `hd_cp_thunk`: the five pushes around `hd_tool_need` were a second copy of that routine's own contract ("clobbers: flags": its load banks AX..BP and ES and has one way out), and the call/ret is a tail jump | -12 | -9 | reading; its comment, which said the load spends the page's registers, corrected |
| `hd_cp_key` leaves by `hd_tool_need`'s ladder (`.out`): the same eight registers in the same order | -6 | -1 | reading |
| `hd_vol_new`'s two exits, one tail (CF = 0 off the equal compare) | -2 | -2 | reading |

**Hot paths.** `os88rdiff` against the base listings, with `hd_blk`,
`hd_xfer`, `hd_xfer_ide`, `hd_bios_run`, `hd_chs`, `hd_buf_step`, `hd_ide_*`,
`hd_raw` and `hd_xa_read` as the hot set: all seven present byte-identical,
`hd_chs` unmoved, the run `hd_blk .. hd_ide_setparams` shifted -20 and
`hd_xa_read` -32 - both multiples of 4, so word and dword alignment are kept
on every CPU that runs them (the 1-byte form of the `hd_xa_a20` item was
chosen over its 2-byte form for exactly that: -33 would have moved the 386's
XMS fill by an odd amount). No int 13h, controller command or XMS move was
added or removed anywhere: no transfer path's instruction changed.

**Equivalence.** "harness" is a unicorn script (scratch, not committed) that
runs the BASE and TIP images' routine of the same name on 3,000 random inputs
each (20,000 for the first batch) - registers, CF, and the variables the
routine writes, read back by symbol in each image - for `hd_chs_pack`,
`hd_cyl_up`, `hd_cyl_trim`, `hd_fmt_spc0`, `hd_fmt_layout`,
`hd_fmt_layout12`, `hd_tw_fmtok`, `hd_tw_delok`, `hd_iact_ok`, `hd_kclus`:
equal. "reading" is recorded in the comment at each change. Every moved
entry and fall-through was read: `hd_pv_out` became a local of `hd_pv_stage`
because the stack walker (`stkapps`) starts an unreferenced global label at
depth 0 and failed it - the one gate that caught anything.

## 3. REFUSED

| item | image / code | file | why |
|---|---:|---:|---|
| `hd_idev_row`/`hd_tdev_row`/`hd_sel_row` for the 24 `mov al, [x]` / `call hd_dev_row` pairs | -59 | **+14** | the pair, with the near-call filter's absolute targets, is a 6-byte lz4 match at every site; three calls are not. Every subset tried also grew the file (+8 to +12) |
| `OS88_ENT` + `OS88_RET_LADDER` in the resident, for `hd_tool_need`, `hd_cp_key`, `hd_cp_paint`, `hd_tool_reap`, `hd_cfg_mark`, `hd_mount`, `hd_mount_one` | resident -41 (image 0) | **+16**, and 3,587 is an 8th cluster | the ladder is 27 bytes and the push/pop runs it replaces were lz4 matches |
| the Format/Delete confirm question composed from the button labels ('Erase slot ' N '? Click ' 'Format' ' again', the same text) | -17 | **+4** | the two tails were already one lz4 match |
| `CLC_OR_STC` in `hd_iact_ok` and `hd_iapps_cand` | -2 each | +1 each | leave-one-out |
| `hd_ivol_back` on `os88_ent` (with its `pushf`) | -7 | +2 | leave-one-out |
| `hd_tool_reap` leaving by `hd_tool_need`'s ladder at `pop bp` | resident -5 | +1 | leave-one-out |
| `hd_at_unpair`, the store triple `hd_at_dup` and `hd_twins` share | resident -4 | +5 (with the 2-byte `hd_xa_a20`) | and it put `hd_xa_read` at an odd shift |
| moving buffers to the END of HDDTOOL.DRV (image 16,384, the zeros stripped as bss) | as the re-tune | ~0 | the tool is read by `OSAPI_FILE_READ`, not `drv_load`: no `drv_bss` zeroes a stripped tail, and `hd_vbrtpl`/`hd_sec0` would start as whatever the heap held |

## 4. WANTS THE OWNER

Nothing changes wording or behaviour. Two trades are one-sided enough to
name, both refused under the rule above:

- the `hd_*dev_row` helpers: **HDDTOOL.DRV image -59, file +14** (no cluster
  either way, 24);
- the resident ladder: **HDD.DRV resident -41 (image 0), file +16** - and
  the 360KB disk's 8th cluster for HDD.DRV, so this one is not a trade.

## 5. WANTS AN ABI BREAK

None found. `hddabi.inc`'s layouts and verb numbers were not needed to move.

## 6. CROSS-FILE

- **FSCHECK.DRV** (`kernel/fsck.inc`, the kernel's module 10; image 6,298,
  file 5,280, util360 6 clusters; kern_mega's 6,283) - the brief lists it
  under this directory but its source is the kernel's. Not read for bytes.
- **Attach-only code, HDD.DRV**: 726 bytes run once. 506 of them
  (`hd_attach`, `hd_at_bios`, `hd_at_channel`, `hd_at_ident`, `hd_ready`,
  `hd_at_new`, `hd_at_geom` and their data) are already laid over `hd_mbr`'s
  512-byte buffer (SPEC.md 52.13.6) and cost nothing after attach. **220**
  (`hd_at_dup` 101, `hd_twins` 119) sit in the resident span in front of the
  align, for the life of the session. A loader shrink could not return them:
  they are not at the tail, and the tail is `hd_mbr`, which stays. Moving
  them into the run needs the run to have 220 bytes free (it has 6). So the
  price of the cross-file feature for this driver is **0 bytes** today; it
  would be 512 only if the resident span fell to 3,072.
- **HDDTOOL.DRV has no attach**: it is loaded on demand and freed at the
  Control Panel's close (SPEC.md 52.11.7).
- **`apps/os88ui.inc`** is 410 of the tool's bytes (`os88ui_btn` 256,
  `os88ui_glyph` 111); the tool uses arm/fire/armed and already opts out of
  the gesture half, `bfind` and radios. Not looked at further (apps/).
- **`drivers/os88drv.inc`'s `OS88_RET_LADDER`** labels `os88_r_si` whatever
  rung is named; nothing here needed it.
- **The tool's tuned pad is fragile by construction** (SPEC.md 52.11.8): a
  byte added in front costs 511 of pad. The `INSTBENCH` knob (DISKCNT=1)
  already does that - its tool is 17,689 where the base's was 17,532 - which
  binds no budget (a knob build) but is the kind of drift the next change in
  front will meet.

## 7. ROWS TO RUN

Fast tier, run by every `make` here: `stkapps`, `installabi`.

Soak (the tool's Format window, page, installer and mount verbs; the
resident's page name, thunk, mount table, XMS arm, HMA copy and kern_mega):

`hddcp hdsize hdmap cpnameshdd hdauto hdnoclaim hdtake hdtakeblank
hdtakeboot install instassoc instcross instdeep instimg instkeep inststate
httpinstall netinst netinstbad icohd hdboot knobhd kdhdd drvcut drvovl
vidxiphd vidxiphdnoharm hmadrv hmapool hmahiber hmadosret megainst
megahddtick hdbounce buildmatrix`

`buildmatrix` for the knob arms (`diskcnt`, `instchunk`, `instchunk-tool`,
`nohdcyl`, `hdcylprobe`); every one also assembled here by hand at the tip,
with `-DDRV_ORG=0xC000` for the HMA copy.

## 8. LOOKED

One boot at the tip on MartyPC, `os8088_xt_hdd`, the 360KB pair: HDD.DRV
loaded at boot (drv_tab row 1 at 9F00h) and mounted C:; the Control Panel
lists **Hard Drive** (the header's string, now), and its page - painted by
the re-tuned HDDTOOL.DRV - shows `BIOS0 615x 4x 26 31M`, the C/H/S editor
and Format / Install / Unmount; Format opens the tool's window with slot 1
`31M C: FAT16`, slots 2-4 `Empty, No Room`, the drive map, the size line and
its three buttons. Nothing was clicked inside it.

## 9. DEFECTS FOUND

- No functional defect. One documentation defect, fixed: `hd_cp_thunk`'s
  comment said `hd_tool_need` "spends every register the page was handed",
  and the thunk banked five of them; `hd_tool_need` gives every register back
  (its contract says flags only) and the banking was a second copy.
- Process note: `make` at the base worktree failed `checkdocs` (the brief's
  own commit left `docs/INDEX.md` one plan stale); regenerated, committed
  here.
- This pass's first commit (the resident) was assembled and packed alone
  but the full `make -j2` ran on the tree holding both commits' changes.
