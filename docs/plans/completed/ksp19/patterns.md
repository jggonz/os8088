# Kernel size pass 19 - `patterns`: did the flag day break a memory pattern?

Branch `ksp19-patterns`, cut from `kernel-size-19` at `11015e90`. The owner's
ask: *"make sure we didn't break any patterns; things still need to compact,
move, live in their proper ram areas, etc."* This pass changed how memory is
claimed (C1, kbpara: every claim on `OSAPI_MEM_P` in paragraphs; megacmp /
mcmerge: `MEMP_DMAPOOL`, `MEMH_REGROW`), where driver service tables are read
(drvtab: in place), where window handlers live (R14: a class table inside the
package's region, `{W_CLASS, W_SEG}`; wmrec: kernel classes biased to
segment 0), the `int 81h` door, and the image ladder's rounding (l1). The
final soak was the union of rows the agents named. This note is a systematic
BASE `4beca096` vs TIP audit: what the claims look like on a running machine,
which compaction/move rows never ran, and which gates still mean anything.

**Verdict: no placement, movability, direction or ownership pattern changed.**
Every census difference is a smaller image or an exact-paragraph claim (both
deliberate). One GATE had gone blind (t_pins, fixed below) and one document
named withdrawn cells (HEAP-CLAIMS.md, fixed). No product byte changed.

## 1. The census - where every claim lives

A scratch script (`census.py`, the scratchpad; `tests/pinsrt.py`'s readers)
boots a tree's own disks on MartyPC (`os8088_5150_cga_gla`), opens packages
through `os88ui`, and reads `mem_tab`, `inst_tab`, `wm_wins` (and at the tip,
each window's class through `W_SEG:W_CLASS`) after each open. Same script,
same disks' recipe, each tree's own symbols. Claims are compared by ROLE
(region / data / kernel tag / purgeable) because bases shift with the kernel.

### kern_big - apps360 (Sticky Note, Tracker, Audio Player, Paint)

| claim | base `4beca096` | tip | why |
|---|---|---|---|
| `mem_base` (arena floor) | 1760h | **1700h** (-1.5KB) | the kernel's -1,281 + l1's ladder pad |
| kernel tags (FE02 parts, FF08 MOD, FF11 SCUT, FD21 FATW, FB1x views) | sizes, MOV/PIN, top/bottom | **identical** | - |
| Sticky Note region (slot 11) | 480 para, MOVABLE, top | identical | - |
| Tracker region | 2,720 para, MOVABLE, top | **2,688** | T5/R14 shrank the image |
| Audio Player region | 1,152, MOVABLE, top | **1,120** | image |
| Paint region | 2,048, MOVABLE, top | identical | - |
| Paint's two data claims | 448 + 448, MOVABLE, bottom | **416 + 416** | kbpara: Paint's exact paragraphs, no longer rounded up to whole KB (`pt_kb_of` deleted) |
| Disk windows' instance claims | 64, MOVABLE, bottom | identical | - |
| DMA heads / `MC_DISK` | none / none (not playing; `MEM_ALIGN_CLASS` is a knob build) | identical | - |

The two harness misses (TeXpad's document and DOS: "a Disk window showing B:
did not happen") happen identically at the base - a census-script artefact
of four open Disk windows, not a finding.

### kern_big - office360 (Word, PiXEL, Chart; WELCOME.DOC, SAMPLE.BMP, SALES.SLK)

| claim | base | tip | why |
|---|---|---|---|
| Word region (`900C` / `900A`) + three data claims | 3,913 MOVABLE top; 64 x3 MOVABLE bottom | identical sizes and flags | the region base is 2 para lower: the kernel |
| PiXEL region | 3,296, MOVABLE, top | **3,264** | image |
| Sheet region + eight data claims (2,048 ... 1,792) | MOVABLE; rlocs 0070 / D2xx | identical | - |
| Chart region + 1,216 data | MOVABLE | identical | - |
| Paint (SAMPLE.BMP's association) | as above | as above | - |

### kern_small - small360 + smallapps360 (Paint, TeXpad, WireFleet)

| claim | base | tip | why |
|---|---|---|---|
| `mem_base` | 0EA0h | **0E80h** | -712 + l1's -128 |
| Paint region | 1,312, **PINNED**, top | **1,280**, PINNED, top | image; kern_small pins regions at both |
| Paint data | 448 PINNED bottom | **416** PINNED bottom | kbpara's exact paragraphs |
| TeXpad, WireFleet regions and claims | | identical | - |

### kern_big with every driver attached (QEMU, `tests/heapmap.py`)

SOUND + HDD + NET + XMEM, the shipped disk with a SYSTEM.CFG: the same five
claims at both (purge HIGH/02 31.5K bottom-up movable; MOD 0.5K PINNED
top-down; DRV 5.5K / 17.0K / 2.0K movable top-down), the arena 580K -> 582K,
largest run 523.5K -> 525.0K, every modelled pass +1.5K.

### kern_mega (Bochs, `OS88_MEGA=1 tests/heapmap.py`)

Conventional: the same one claim (purge HIGH/03, 9.0K, PINNED, bottom-up,
DMA head 576 para) at the same base; arena 576K -> 578K. **The pool above
1MB is identical to the byte, addresses included**: purge HIGH/02 32K, FATW2
5K, kern FE 1K, kern FD 5/6/17/4/11K, all movable. megacmp's per-site table
(`mcmerge.md`) is the placement audit with packages open; its rows (megajw,
megargw, megaxmove, megahuge, megahigh, heaphi) ran green there, and this
pass ran kern_mega's every untested move and compaction row (section 3).

### kern_emu

Not separately censused (`tests/heapmap.py` does not boot `build/emuk`). It
is kern_big plus the VMware pointer: every memory, window and driver arm is
kern_big's and its delta is kern_big's (-1,281 resident).

### Windows: the class lives in W_SEG, and every one read back

At the tip every live package window's `{W_CLASS, W_SEG}` was read through
guest memory: a length word in 4..30, even, the style word, then the
handlers - Sticky Note (26), Tracker (28), Audio (28), Paint (28; its
run-time-written slots present), Word (30), Sheet (30), PiXEL (28), Chart
(12), and kern_small's Paint (28), TeXpad (30), WireFleet (28). Kernel
windows read `W_SEG` 0 with the class biased by `KERNEL_SEG * 16`
(`fm_cls`, length 30). Statically: the only reader of `W_CLASS` is `wm_hnd`
(the bias store in `wm_create` and kern_small's chooser excepted), and
`wm_create` reads the template, the class and the style through ES - the
caller's segment - and stores that same ES as `W_SEG`, so a class can only be
named in the segment that `mem_rr_tab`'s `wm_wins + W_SEG` row follows on a
move. Every kernel class table (`app_cls_about`, `fm_cls`, `fdlg_cls`,
`cp_cls`, `hb_cls`) is `.text`.

### Driver tables read in place

`drv_cls_svc_x` answers `ES` from `drv_fptr+2` (= `drv_fseg`, which
`mem_rr_tab` rows over all `DRVC_MAX` classes) and `DI` from `drv_svco`, the
offset the attach returned. A moved driver's segment follows; the offset is
inside the image. Every caller reads the table and lets ES go before
anything that can compact (assoc, desk, `dsk_xfer`'s BP fetch, `drv_fs_seg`,
the panel walks). `[drv_fscur]` caches a driver's SEGMENT across calls - as
it did at the base (unchanged by this pass). An HMA driver's table offset is
its `DRV_ORG`-relative label, the same space its dispatcher uses.

## 2. Gates

| gate | base | tip | verdict |
|---|---|---|---|
| `t_pins` | 382 sites / 317 keys, 157 movable, 160 pinned, X25 | **378 / 315**, 156, 159, X24 | **BLIND to one site - fixed.** RAMDISK's `rd_claimp`/`rd_claim` merged into one (its X line went with it: deliberate). But The Wire's `wr_catseg` vanished: the final pass taught `huge_regrow` that `mov bl, MEMP_REGROW` is a resize, and its 24-line look-back ran past `wr_refresh`'s nearer `mov bl, MEMP_CLAIM` (at `.new:`) to the REGROW above it. The look-back now stops at any nearer `mov bl,`/`mov bx,`: **379 / 316, 157 movable**. Broken on purpose: undeclaring `wr_catseg` (`OSAPI_MEM_MOVABLE` -> `nop`) is RED with the fix and GREEN without it |
| `t_movable` | 80 packages, 74 declare, 6 exempt | identical | meaningful |
| `t_megahigh` | 80; HIGH 74, HIGHMEM 76; 52 images | identical | meaningful |
| `t_drvclaim` | 2 drivers, 2 tasks, no cone | identical | meaningful |
| `kernresident`, `small128`, `pinsrt` | | **ok** (run below) | meaningful |
| `docs/HEAP-CLAIMS.md` | | **stale**: eight mentions of the withdrawn KB doors (`OSAPI_MEM_CLAIM`, `_HI`, `_DMA`, `_DMA_HI`) | re-pointed at `OSAPI_MEM_P`'s verbs, `MEMP_DMAPOOL` named where kern_mega's pool takes a head |

`osapi_mem_p` re-read for placement: a `MEMP_CONV` head is page-safe
everywhere; on kern_mega a head with `MEMP_CLAIM_HI` still goes BOTTOM-UP
(KERN-MEGA-PLAN 14.55, as `mem_claim_dma_hi_x` did); `MEMP_DMAPOOL` is the
only way to the pool head, and the verb bits cannot reach `.avail` (signed
compare) or `MEMP_CPU` (bit 1). The CPU/DISK class is a knob build
(`MEMGRAN=16`), as at the base.

## 3. Rows - the compaction / move / heap / hibernate rows the union never ran

`tests/suite.py`'s soak rows matching move, compact, heap, hiber, rehome,
pool, huge, hma, pin, resid, claim were diffed against the final pass's
union (`union.txt`) and `fix.md`'s re-runs: 88 had run in neither. All but the
three WIN16 rows (no fixtures here) ran once, `tools/os88soak.py` run
`20261110-142622` at `11015e90` (+ this branch's t_pins/doc, no shipped byte),
lane 2: **83 ok, 4 FAIL**.

ok: a2move arcimporthuge editmove editmove-frotz excitebikelowmem
excitebikesmallclaim hdnoclaim heapscrl hma286 hmapool kernresident lemmove
mpupin pinsrt pxsmove rcmove rdgmove rdmove regmove ringmove-audio
ringmove-audiospk ringmove-midirack ringmove-tracker ringmove-wirefleet
small128 trackmove tymove vidclmove vidclmovediag vidclmovely vidmegaclaims
vidmove vidpagepool4v vidpagepoolf vidresident vidresidentcga
vidresidentvga wcclaim wdheapmove wdheapmove-sc wdmove; kern_mega:
mega-a2move mega-btnmove mega-c64move mega-drvmove mega-editmove
mega-editmove-frotz mega-hdnoclaim mega-heapcheck mega-heapdrv mega-heaphi
mega-heapmap mega-heapscrl mega-hibsumbad mega-hibsumok mega-httpinstall
mega-ovlhigh mega-paintmove mega-pkgmove-browser mega-pkgmove-texpad
mega-rcmove mega-rdmove mega-regmove mega-rehome mega-rehomeabort
mega-rehomemove mega-ringmove-audio mega-ringmove-midirack
mega-ringmove-tracker mega-sheetmove mega-sndmove mega-stikhiber
mega-stikmove mega-tmowner mega-trackmove mega-trkcompact
mega-vidresidentvga mega-wdheapmove mega-weavemove megahma megahw86
megahwbig megap2hiber.

| FAIL | at the base | verdict |
|---|---|---|
| `megahw` | **FAIL the same way** (kern_mega FAULT 00 at 0060:6335; tip 0060:5FC8 - the kernel moved), after every data check passed; `megahw86` and `megahwbig` ok | pre-existing, Bochs-only |
| `mega-regpin`, `mega-regwork` | **FAIL the same way**: "the regions are not stacked in open order" (base paint1 10AC calc 10EC paint2 112C pinme 110C) | pre-existing: the row's precondition, not a move |
| `arcmove` | ok | **FLAKE**: Space Invaders' `find_view` (a scan of guest memory for `ARCV`) found nothing once under the soak's load; the same row at the tip, alone, **ok twice** (80.8s, 82.1s), and its Q*bert and Pac-Man move legs passed in the failing run |

Not run: `win16hiber`, `win16lheap`, `win16megahugega` (`win16fixtures`, as
the final pass). Earlier passes' coverage stands for the classes this audit
did not re-run: the union's pkgmove-*, heapcheck, heapdrv, heaphi, heapmap,
hibernate*, stik*, rehome*, megaxmove, megahuge, megapxhuge*, trkcompact,
sheetmove*, paintmove, ovlmove, loommove, c64move, cmemmove, dostrmove (and
fix.md's re-runs after its fixes).

Also run by hand: `tests/heapmap.py` (QEMU, kern_big) and
`OS88_MEGA=1 tests/heapmap.py` (Bochs) at base and tip (section 1);
`megahw`, `mega-regpin`, `mega-regwork`, `arcmove` at the base.

## 4. Hibernate, DOS, the door

The door's vector is installed in `sched_init` with IRQ0, hibernation images
carry the whole IVT (HS_IVT), DOS virtualises 81h (`dos_rarev`) in both
hosts and full-screen DOS banks the whole table (door.md 2, 4). Rows: the
union's hibernate, hibernatedrv, hibernatem, hibsumok, hibsumbad, stikhiber,
megahiber, megahibfull, dosseam, doswspin, dostrmove; here mega-stikhiber,
megap2hiber, mega-hibsumok, mega-hibsumbad - all ok. A window class needs
nothing of a hibernate: the image restores the region and the record, and a
kernel class's bias is a build constant.

## Commits

* `t_pins sees The Wire's catalogue claim again; HEAP-CLAIMS names the
  paragraph door` - `tests/unit/t_pins.py` (the look-back), `docs/HEAP-CLAIMS.md`.
  0 bytes anywhere shipped.
