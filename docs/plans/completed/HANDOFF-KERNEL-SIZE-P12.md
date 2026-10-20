# Kernel size pass 11: the record, and what is left for a twelfth

**PASS 11 HAS LANDED** on `kernel-size-p11`, cut from `elendilon` at
`2a05e31f`. The file is named `-P12` for the reason the two records before
it are named one ahead: each pass's record is named for the pass it hands
to. Its companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P11.md`**: pass 10's record.
* **`docs/plans/completed/ksp11/`**: the seven agents' own notes, one file
  each, written as they decided: every item TAKEN with its bytes on both
  kernels, every item REFUSED with the bytes it would have been and why, and
  the cross-file list. **Read the refused lists before re-proposing
  anything.**
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand, blessed at the close
  of this pass.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: a RESIDENT kernel size pass **focused on the incoming work** -
what landed on `elendilon` since the last upstream squash (`95f7e971`), and
in practice what landed after pass 10 merged (`112f8f9d`). Some of that work
had size-passed itself (multi-select, twice), most had not. **Any ABI, API
or version added since the squash was free to change or break**, having not
gone live. Other optimisations spotted on the way were in scope. One agent
per concept.

**What the incoming work had cost**, measured per merge with
`tools/kernsize.py --json` (resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf`), against pass 10's close:

| merge | concept | kern_big | kern_small |
|---|---|---:|---:|
| `f8ba8eb3` | font-bug: captions beside a covering window's corner (SPEC.md 11.3.4.3) | +92 | 0 |
| `e002a0e9` | video-xms: OSAPI_VOL_TAKE, EMS.DRV's door, xmem (52.1.1, 107) | +209 | +84 |
| `323f0efb` | multi-select (22.27), already passed twice | +797 | 0 |
| `a3818217` | PicoMEM: B: from the card, Restart warm-resets | +40 (`.ovlw` +85) | +40 |
| `42e0db93` | disk-cpu: `dsk_xfer` asks once a run whether a bar is live (18.91.6) | +15 | +15 |
| `bc6b0d87` | ftp-speed: WSEQF_KEEP/CKPT (18.4.9.3), FAT16 free search (18.4.10) | +222 | +114 |
| | **total** | **+1,375** | **+253** |

kern_big's `.cold` had crossed two rungs since pass 10's bless (KERN_SIZE
97,280 -> 98,304).

**The outcome**, base `2a05e31f` against the close:

| | base | close | Δ |
|---|---:|---:|---:|
| **kern_big resident** | 92,495 | **92,219** | **-276** |
| kern_big `.text` / `.bss` / `.cold` | 43,428 / 5,190 / 37,943 | 43,369 / 5,154 / 37,762 | -59 / -36 / -181 |
| kern_big `.ovl` / `.ovlw` | 2,471 / 5,086 | 2,471 / 5,075 | 0 / -11 |
| kern_big `KERN_SIZE` | 98,304 | **97,792** | **-512** (the cold rung uncrossed) |
| **kern_small resident** | 61,442 | **60,314** | **-1,128** (-1.8%) |
| kern_small `.text` / `.bss` / `.cold` | 32,022 / 3,073 / 23,479 | 31,830 / 3,067 / 22,549 | -192 / -6 / -930 |
| kern_small `KERN_SIZE` | 63,488 | **62,976** | **-512** |
| kern_emu resident / `KERN_SIZE` | 92,872 / 98,816 | 92,596 / 98,304 | -276 / -512 |
| `kerndos.bin` | 33,243 | 32,831 | -412 (-444, then +32 for §3.2's fix) |

Loadable images (UNPACKED image, which is what lives in RAM; the shipped
file is LZ4-packed):

| image | base | close | Δ |
|---|---:|---:|---:|
| `EMS.DRV` | 1,032 | 670 | **-362**, and its claim **2 KB -> 1 KB** on every machine that loads it |
| `HDD.DRV` | 3,748 | 3,568 | -180 (at its floor: §5) |
| `SOUND.DRV` | 6,946 | 6,796 | -150 |
| `USBMOUSE.DRV` | 1,410 | 1,396 | -14 |
| `ETHER.DRV` | 16,400 | 16,392 | -8 |
| `DOCK.DRV` | 2,246 | 2,233 | -13 |
| kern_small `FILECP.DRV` | 1,936 | 1,770 | -166 |
| kern_small `CTRL.DRV` | 4,490 | 4,406 | -84 |
| kern_small `FDLG.DRV` | 1,246 | 1,232 | -14 |
| kern_small `CLONE.DRV` | 7,196 | 7,184 | -12 |
| `HDDTOOL.DRV`, `HIBER.DRV`, `CLONE.DRV` (big) | | | 0 (savings fell into `align 512` pads) |

**The merge reproduced the sum of the branches exactly** on both kernels at
every merge (read after each).

## 1. WHO TOOK WHAT

Six concept agents, each in a worktree cut at `2a05e31f` and owning one
merge's code rather than a set of files (shared files - `disk.inc` had three
owners - were split by hunk, and nothing collided but `docs/INDEX.md` and one
pair of identical deletions); then a seventh, `xfile`, on the merged tree in
two rounds for the cross-file lists.

| agent | concept | big | small |
|---|---|---:|---:|
| **diskwrite** | ftp-speed + disk-cpu, pass 10's relaxed jumps, the DMA-bounce row | -88 | -57 |
| **multisel** | multi-select's third pass (797 -> **755**), `fm_onkey_x`'s eleven relaxed jumps | -79 | -11 |
| **voltake** | video-xms's kernel side; kern_small's driver-volume slots | -55 | -440 |
| **caption** | the caption fix, `wm_clip_rows` | -28 | -8 |
| **picomem** | PicoMEM in the kernel and three drivers, `sbl_isr` | -5 | -5 |
| **drivers** | `EMS.DRV`, `HDD.DRV` | 0 | 0 |
| **xfile** | the cross-file lists: kern_small's redirector arms, relaxed jumps, `lea sp` | -21 | -607 |
| **total** | | **-276** | **-1,128** |

### 1.1 What the incoming work came to

* **font-bug** (+92): -28, and SPEC.md 11.3.4.3 records why kern_small does
  not carry the grow at all. Proved equivalent to the base under unicorn
  over 30,000 cases per kernel; every path runs fewer instructions.
* **video-xms** (+209 / +84): the VOL_TAKE half is +66 of `.cold` with
  **`OSAPI_VOL_TAKE` (0x0467) RETIRED** - a take is `OSAPI_VOL_ADD` with DX
  != 0 (DL = the index `OSAPI_VOL_AT` answered), which main's SDK had marked
  "RESERVED, pass 0" - and the take and give-back are each one word store,
  DV_KIND and DV_UNIT adjacent. `DRVC_EMS` keeps no 36-byte table copy: its
  one cell rides in `drv_fptr7`'s offset half, and `OSAPI_DRV_CALL` got
  ~16 cycles faster for every other class. `HDD.DRV` follows the new ABI,
  `HD_ABI_VER` 4 -> 5 (shared only with `HDDTOOL.DRV`).
* **multi-select** (+797): 755 now. `MSELOFF=1` is the feature's A/B and no
  longer byte-identical to the pre-feature kernel, since pass 11 re-laid
  code the feature shares; CLAUDE.md's knob row says so.
* **PicoMEM** (+40 / ovlw +85): +35 / +74; `dsk_fdd_park_x` out of
  `kern_dos`, where it was dead (-60 there).
* **disk-cpu** (+15): left as is, already minimal (`ksp11/diskwrite.md`).
* **ftp-speed** (+222 / +114): -42 on both kernels from the FAT16 search
  sharing `dskw_alloc`'s wrap (checked against the old loop in a model over
  20,000 FATs) and -31 from KEEP/CKPT. kern_big paid twice because KEEP/CKPT
  is part of WRITE_SEQ, which kern_small does not have.

### 1.2 The shapes that recurred

* **Dead on one kernel only.** The largest single result is kern_small's:
  no driver class is ever published there, so `osapi_vol_fence` could never
  let anyone through - and eleven redirector arms, four zero-filled driver
  tables, a thunk, two fenced file cells, the VOL slots and six FILECP.DRV
  arms were still assembled behind it (voltake -418, xfile -607). Each slot
  is now a label on an existing refusal stub; a redirected arm added there
  later fails to assemble instead of silently calling a refusal. SPEC.md
  51.0.2 and 62.9.2.3 say so.
* **An unpublished cell folded into a published one's reserved register**
  (`OSAPI_VOL_TAKE` into `OSAPI_VOL_ADD`'s DX).
* **A table copy the kernel kept of a driver's table it reads one cell of**
  (`DRVC_EMS`), and on the driver's side a 256-byte page-owner table that a
  contiguous-run allocator never needed (`EMS.DRV` -506 of image+bss).
* **Relaxed jumps, again** - `fm_onkey_x` alone had eleven. The margins left
  are thin (Ctrl+A's jump is 126 bytes from its body): any growth there will
  relax one again without a word. The xfile sweep left 83 hits on kern_big
  and 30 on kern_small, each with its reason (`ksp11/xfile.md`): most are
  the software-renderer dispatch, `gfx_blit4`'s per-run loop and `font_run`,
  where the short form costs cycles.
* **A jump table for a compare chain** (`HDD.DRV`'s `hd_svc`, twelve
  compares, nine of them relaxed: -33).
* **Register banks a callee did not need**, single-caller routines written
  at their call site, and a `.bss` word dropped where its twin held the same
  value (`[pm_sbport]`) - pass 10's shapes, still paying.

## 2. HOT PATHS

None got slower; the ones that moved, moved down. `wm_clip_rows` (once per
clipped glyph cell) 2-5 instructions fewer on every path; `sbl_isr` 12-14
cycles faster per block IRQ; `OSAPI_DRV_CALL` ~16 cycles faster for every
class but EMS; `HDD.DRV`'s per-sector DRQ exit one instruction shorter, the
`rep insw` loop unchanged; `gfx_blit1_x`'s teardown `lea sp, [bp+18]` ~8
clocks once per call (`tools/stkbalance.py` reads the form now, with a
fixture). The costs paid on purpose, all once per operation: the FAT12
allocation scan one compare per candidate on the first allocation after a
mount (~24 cycles); `fm_mset`/`fm_mdraw` through `kentc_di` (~40 cycles a
gesture); `dsk_xfer`'s write-protect refusal +16 cycles on that error path.
`sch_switch`, `sch_isr`, the API cell, `font_run` and `dsk_xfer`'s
per-sector path are untouched.

## 3. DEFECTS FOUND

### 3.1 A driver move left two classes' published segments behind (FIXED)

`93287205`, 0 bytes. `memory.inc`'s relocation row `MEM_RR_ROW drv_fseg, 5,
4` covered five driver classes; `DRVC_POINT` (6, on main) and `DRVC_EMS` (7,
this round) were added without it. A compaction that moved `EMS.DRV` (it
hooks no vector, so it may move) left `drv_fseg7` naming freed memory, and
every `OSAPI_DRV_CALL` to it and the `EMSV_GONE` call at every instance
teardown far-called there; the USB mouse's packets would be refused. The row
counts `DRVC_MAX` now and `tests/drvmove.py` checks all seven. **Found by
reading; not reproduced** - no row moves `EMS.DRV`.

### 3.2 Leaving a DOS program on a PicoMEM machine (FIXED on the owner's word)

`kerndos/kdentry.inc`'s `kd_leave` ended in `int 19h` with no PicoMEM check,
which is the path SPEC.md 18.100.1 fixed for the desktop's Restart, so it
plausibly hung the same way. Found by reading in this pass and left for the
owner, who asked for it at the close: the ramp and the warm reset are now
`DSK_PMEM_WARM` in `kernel/disk.inc`, one source expanded by both
`dsk_fdd_park_x` and `kd_leave`. `kerndos.bin` +32 (32,799 -> 32,831), the
kernel's sections +0 (its image differs only in `mov dx, 0x2A3` moving after
`pushf`/`cli`). No emulator here has the card, so the success path is the
field's to confirm, as the desktop's was.

### 3.3 Behaviour changes taken on purpose

* A kept WRITE_SEQ hold that hops off its volume and back within one wake
  commits at the next unlock instead of staying (the safe direction; SPEC.md
  18.4.9.3).
* `drv_load_row` asks one-driver-per-class FIRST, so a row both duplicate and
  missing answers `DRVE_TWICE` where it answered `DRVE_NOENT` (SPEC.md
  51.2.1).
* `HSV_MOUNT`/`HSV_VOLOF` pointed at a partition the kernel already carries
  takes it over instead of adding a second volume; the installer refuses that
  target, so nothing reaches it today.

## 4. WHAT WAS RUN

* Every agent: `make`, `make small` and the fast tier (61/61) after every
  batch, `make emu` at its tip, `checkdocs`, and `tools/stkbalance.py` over
  the files it touched, base against tip (no new unbalanced path anywhere;
  the tool learned `jmp far` to a literal address and `lea sp, [bp+N]`, each
  with a QUIET and a LOUD fixture in `tests/unit/t_stkbalance.py`). The
  coordinator rebuilt and read `kernsize` after every merge.
* Per-agent soak rows - about 120 distinct, all green (each agent's note
  lists them). Two scratch unicorn harnesses proved equivalence where no
  emulator has the hardware or no row reaches the path: `wm_clip_rows`
  (30,000 cases per kernel) and `picomem.inc` (4,322 scenarios, port traces
  identical in order). `tests/dskwstage.py` gained **the DMA-bounce row
  passes 9 and 10 asked for**: WRITE_AT and READ_AT through a buffer 0xF0
  short of a 64 KB page, broken on purpose twice and red both times.
* **The integration soak**: §4.1.

### 4.1 The integration soak

On the merged tree at `c6ba7e8d`, `tools/os88soak.py start` scoped to every
subject the pass touched - 160 rows (the union of the agents' rows, each
subject's neighbours, every `small*`, `kd*`, `hd*`, `fdlg*`, `fm*`, `fcp*`,
`wseq*`, `heap*`, `reg*`, `rehome*`, `covox*`, `ems*`, `videms*` row): **158
ok, 1 FAIL, 1 SKIP** in 34 minutes at width 4. The skip is `instkeep` (the
box has no `mtools`). The failure is **`regrowshed`, red at the base
`2a05e31f` with the same signature** (`loaded` and `toobig`; re-run there
alone), as pass 10 recorded it at its own base: its staging no longer fills
the 128 KB machine's heap. The whole soak tier was not run: it is the
owner's to ask for.

The full tier on the same tree: **67/67** (it boots kern_small on its
128 KB floor machine and both 1bpp adapters, `kernresident`, `ctoolchain`,
`ps2mouse`). `soak -k buildmatrix` (every knob kernel and every knob module,
both arms): **ok** - no module crossed a KB boundary downward with its bss on
top, which is what broke `ANIMOFF=1` in pass 10.

## 5. WHAT IS LEFT

* **`HDD.DRV` is at its floor**: 3,072 + max(512, the attach-only code laid
  under `hd_mbr`). Smaller resident savings only widen the pad until ~280
  more bytes come out of the resident part, which would take it to a 3 KB
  claim.
* **The relaxed-jump remainder** (`ksp11/xfile.md`'s table): HIBER.DRV holds
  ~45 bytes of them, and `gfx_blit4`'s per-run loop wants a hoist that is a
  speed job first.
* **The 2A3h PicoMEM ramp exists in four places** (park, `pmemu`, SOUND,
  USBMOUSE) and cannot be shared without a published cell.
* **`xm_release_rec` on kern_small** (9 bytes), left by xmem.inc's own rule
  that teardown sites stay unconditional.
* Carried from pass 10 §5, untouched and still the owner's: `rect_get` /
  `rect_put` at the damage-repaint and save-under sites (-138), and the
  module bss assertion's general fix (no module crossed a KB boundary
  downward with bss on top in this pass; `buildmatrix` is the row that says
  so).

## 6. METHOD LESSONS

* **Agents by CONCEPT rather than by file worked**, with hunk ownership in
  shared files. Three agents edited `disk.inc` and the merges were clean;
  the one overlap (two agents deleting the same two `equ` aliases) resolved
  as either side.
* **The follow-up agent earned its round twice**: the cross-file lists were
  worth -607 on kern_small, more than any concept agent. Give it
  a second round once the files it was barred from are free.
* **A ratchet for one kernel arm is worth looking for**: the biggest bytes
  were code whose GUARD said "never on this kernel" while the code behind it
  was still assembled there. `grep` for a refusal stub whose callers all
  sit behind a test that cannot pass on that arm.
* **Do not run a row while editing kernel sources in the same tree**: two
  rows failed in `os88sym` ("the map describes a DIFFERENT kernel") for that
  reason alone and were green on a frozen re-run.
