# Kernel size pass 9: the record, and what is left for a tenth

**PASS 9 HAS LANDED** on `kernel-size-p9`, cut from `elendilon` at `e1c204c`.
The file is named `-P10` for the reason pass 8's record is named `-P9`: each
pass's record is named for the pass it hands to. Its companions, and this file
repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`**: pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P9.md`**: pass 8's record. Its
  §4 costed list still stands except where §5 below says otherwise.
* **`docs/KERNEL-MEMORY.md`**: where the budgets stand. Blessed at the close
  of this pass for all three kernels.

---

## 0. THE BRIEF AND THE OUTCOME

The brief: a general pass, including the small things that had not had a size
pass since pass 8, with **kern_big back under 100 KB** as the goal and **hot
paths staying fast**. Desktop shortcuts had had two passes already and were to
get a light look at most. The owner declined the one large lever left on the
table (the Standard File dialog and Cut/Copy/Paste as on-demand modules on
kern_big, ~-7,400): **bytes only**.

"Under 100 KB" was read as `KERN_SIZE` <= 102,400, the footprint that sets
the heap floor. Resident was already under 100 KB by either reading of a KB.

The un-passed work since pass 8 landed in #214, by `kernsize --json` at each
first-parent commit touching `kernel/`:

| commit | what | kern_big resident |
|---|---|---:|
| `6d1690c` (#215) | confirmed shutdown, animated splash (CTRL.DRV) | +75 |
| `03e8ef7` `1b372a1` `8aac7e7` | desktop cells drawn once, into the dither's region | +396 net |
| `7a48a57` `efaf762` `3dabb6a` | the Floppy page's Cylinder (CTRL.DRV, `.ovl`) | 0 resident, `.ovl` +5 |
| `f5e0c60` | LZ4 input past 64KB | +43 |

Resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`:

| | base `e1c204c` | close | Δ |
|---|---:|---:|---:|
| kern_big resident | 99,303 | **95,653** | **-3,650** (-3.7%) |
| kern_small resident | 65,686 | **63,324** | **-2,362** (-3.6%) |
| kern_big `KERN_SIZE` | 104,960 | **101,376** | -3,584 (**goal met**, 1,024 under) |
| kern_small `KERN_SIZE` | 67,584 | **65,536** | -2,048 |
| kern_emu `KERN_SIZE` | did not assemble | **101,888** | (§3.2) |
| kern_big `.ovl` | 2,494 of 2,496 | 2,470 | -24 |
| CTRL.DRV (big / small) | 12,114 / 5,673 | 12,218 / 5,735 | +104 / +62 (RTC helpers moved IN, §1) |
| FORMAT.DRV | 1,304 | 1,298 | -6 |
| FDLG.DRV (small) | 3,214 | 3,079 | -135 |
| FILECP.DRV (small) | 2,269 | 2,237 | -32 |
| CLONE, HIBER, DOCK, EXTD | | | 0 |

The figures include the two correctness fixes the pass made (§3.1, §3.3),
+140 resident on kern_big and +137 on kern_small.

## 1. WHO TOOK WHAT

Six agents, each in a worktree cut by the coordinator at `e1c204c` and owning a
set of kernel files, merged `--no-ff` in the order below. **The merge reproduced
the sum of the branches to within 4 bytes** (two agents removed the same
overlay shim, `ovw_desk_rowcalc`; the conflict was comments only).

| agent | files | big | small |
|---|---|---:|---:|
| **disk** | disk, diskw, fdlg, dskwin, drvvol | -700 | -497 |
| **files** | files, filecp, fprog, clone, mod, lz, compress | -672 | -605 |
| **core** | kernel.asm, memory, sched, events, instance, apps, loader, fsx, snd, xmem, cpudet, assoc, hiber, hb* | -665 | -383 |
| **wm** | wm, icons, font, clip, toast | -636 | -543 |
| **gfx** | vga12, softgfx, mouse, vidsel, viddet, splash, mouproto, band | -571 | -190 |
| **shell** | ui, menu, driver, ctrl, shutdown, spinner, desk, desksc, dock, dockmod, extmod, clock, clockw, blank | -550 | -285 |
| coordinator | the DMA bounce (§3.1), the WPROT relay (§3.3) | +140 | +137 |

Per file at the close, code (`.text` + `.cold`) and `.bss`:

| file | code | bss | | file | code | bss |
|---|---:|---:|---|---|---:|---:|
| files | -625 | +4 | | clock | -110 | |
| wm | -589 | | | kernel.asm | -85 | |
| vga12 | -281 | -70 | | driver | -82 | |
| memory | -240 | -2 | | assoc | -77 | |
| diskw | -204 | | | menu | -69 | |
| ui | -202 | -2 | | ctrl | -47 | |
| disk | -183 | +1 | | apps | -44 | |
| fdlg | -177 | | | filecp | -32 | |
| instance | -157 | -2 | | icons | -22 | |
| mouse | -147 | -44 | | desk | -21 | |

and under 20 each in fsx, sched, mod, softgfx, font, loader, dock, snd, toast,
viddet, hiber, lz and clip. `disk` is net of §3.1's +131 (and `diskw` of
§3.3's +3, which landed after this table was taken).

### 1.1 The largest items

The agents' own commit messages carry the instruction-level detail; these are
the shapes that recurred.

* **Single-caller routines written at their call sites** (core, wm, disk):
  memory, assoc, fsx, loader, instance, the WM's damage helpers. Each one is a
  `call`/`ret` and usually a register bank gone.
* **Thirteen `cw_` shims became their OSAPI cells** (core, `kernel.asm`): a
  module calling a routine that is already published goes through the public
  cell and the private shim is deleted. Where no cell existed, nothing was
  published to make this work (MODULE-SELFCONTAIN-PLAN's rule).
* **The clock's six RTC port helpers are a copy in each image** (shell,
  SPEC.md 37.94): the only callers are module code, so resident -N, CTRL.DRV
  +N. This is the CTRL.DRV growth in §0.
* **`drv_stamped`** (shell): `drv_call`, `drv_fs_call` and `drv_cp_call_x`
  were one body three times, differing in where they read the driver's
  segment. It takes the segment on the stack now.
* **The Disk window** (files, -625): layout, selection and context-menu code,
  all cold.
* **The primitive union, the cursor rect, the display dispatch** (gfx):
  re-cut so the common path is shorter AND faster (§2).
* **The FAT write and read bodies** (disk): one stage-and-transfer step,
  `dskw_wone`/`dskw_rone`, where four sites spelled it out.
* **`.lowbss` was not touched.** Its rung had 34 bytes of slack and 478 out
  of it would have dropped a rung; nobody found a dead buffer, and the task
  stacks are sized from measurement (STACK-SLOTS-PLAN), so they were left.

### 1.2 What desktop shortcuts got

`desksc.inc` is unchanged in resident bytes (CTRL.DRV's refusal ladder in it
went short, -30 of module). As the owner expected, there was little there.

## 2. HOT PATHS

Measured, not argued: `tests/gfxbench`, `tests/sysbench` and `tests/fontbench`
on MartyPC, base `e1c204c` against the merged tree, on the 5150 Hercules and
CGA (GLaBIOS twins) and the VGA XT, six runs per tree per machine (two boots
of three), the per-tree minimum compared. Each agent also wrote an
instruction-level count into its commit message for every hot routine it
touched.

**No kernel path got slower.** The ones that moved, moved down:

| row | Herc | CGA | VGA |
|---|---:|---:|---:|
| `GFX_POINTS` 8 / 24 pts | -3.2% / -0.5% | -3.9% / -1.7% | -3.1% / -3.2% |
| `WM_CONTENT` / `WM_GEOM` | -3.3% / -2.3% | -3.3% / -2.3% | -3.3% / -2.3% |
| `WM_CLIP_SET+CLEAR` | -1.2% | -1.2% | -1.2% |
| `GFX_PIXEL` | -1.1% | -1.1% | -1.6% |
| `GFX_HLINE` 8px | -1.3% | -1.3% | -2.0% |
| `GFX_FILL` 8x8 / 64x64 | -1.0% / -1.2% | -0.3% / -0.1% | -0.6% / -0.2% |
| `FONT_RUN` 10 aligned | -1.1% | 0.0% | 0.0% |
| `FONT_CHAR`, `FONT_STR` | 0.0% | 0.0% | 0.0% |
| `TASK_YIELD`, the API far-call cell | 0.0% | 0.0% | 0.0% |

Three things read as slower and are not the pass's code:

* **Hercules VRAM rows, +2% to +9%** (`GFX_VLINE` 128px +9.0%, `GFX_SCROLL`
  +7.0%, `GFX_FRAME` +6.1%, `GFX_BLIT1` +5.0%, `GFX_XOR_RECT` up to +4.5%).
  Per call the instruction counts are **identical or lower** (BLIT1 1,403 ->
  1,403, SCROLL 2,078 -> 2,074, VLINE 212 -> 211), the same primitives on CGA
  (same 1bpp renderer) are flat or faster, and the bench's own RAW VRAM rows,
  which are PACKAGE bytes identical in both trees, moved by the same amounts
  in both directions (write word -7.2%, read word +3.6%). It is MartyPC's
  Hercules wait-state phase, stable for a kernel and shifted by any layout
  change; what keys it was not found. A real card runs on its own crystal, so
  these rows do not predict iron, in either direction.
* **VGA floppy "read 16K, warm" +64%**: the smaller kernel moved the bench's
  16KB claim from `2D00` to `2C20`, so its read no longer crosses a 64KB page,
  so cylinder 10 no longer goes through the track cache that served the warm
  pass. With the buffer moved by opening a Disk window first, both trees give
  identical traffic and identical times; the 5150s read the same on both.
* **Noise rows**: `GFX_UNLOCK+LOCK` on VGA (+2.4% by minimum, -0.4% by
  median, 5.6% spread within the base), `SET_COLOR`, the tick-timed rows, a
  cold motor.

The one real cost: **`sw_pairbuild`**, the 1bpp pair table built on the first
`GFX_BLIT4` of a boot, is ~+14.5k cycles (~3 ms, once) after its rewrite; every
later `GFX_BLIT4` is 0.7% faster.

## 3. DEFECTS FOUND AND FIXED

### 3.1 A sector across a 64KB page was int 13h error 09h (SPEC.md 18.91.4)

**Latent at the base, exposed by the smaller kernel.** `tests/lzbig.py`'s
streamed legs (BIG3, BIG4) said `Disk error` on the merged tree, every time,
while **every agent's branch passed alone** and so did every merge before the
last. Bisecting the merge chain and then the last branch file by file found
no code collision. What differed was the layout: only the full merge put the
heap floor at `0x1920`, and the identical merged code passed with 512 bytes of
`.lowbss` padding (floor `0x1940`). A breakpoint on `dsk_xfer.fail` then
caught it: status **09h**, a one-sector write from **`2FE0:0008`**, which runs
across physical `0x30000`.

`dsk_runcap` caps a run at the 64KB DMA page, but for a buffer that is not
512-aligned and sits under a sector from the page end it answered **1**, with
a comment that the case was "only reachable from a base SPEC.md 2.4
forbids". SPEC.md 18.4.1 lets a caller hand the file layer a buffer at any
offset; `dskw_runadd` stages the case for an append and a read (18.4.2.1);
but `OSAPI_FILE_WRITE_AT`'s INSIDE arm (`dsk_write_chain_x`) and every
`dsk_read_chain_x` caller reach `dsk_xfer` with the caller's ES:BX. The
streamed Compress rewrites its header at `(ES-1):8` at the end, and the
claim had simply never landed on an edge before.

The fix stages that one sector through `dsk_secbuf` inside `dsk_xfer`'s own
loop: the caller's ES, BX and count banked (the count doubling as the flag),
a write copied in, the sector sent round `.sector` again, and `.success`
putting the caller's buffer back and copying a read out. It is not a
recursive `dsk_xfer`, whose head calls `fpg_busy`, which may draw, and a draw
inside `[sch_lock]` can wait on a gfx lock another task holds. **+137
resident** (`.cold` +131, `.bss` +6). `lzbig` is red without it at this
layout and green with it.

**It has no dedicated row.** `lzbig` caught it by luck of layout and will
stop exercising it the next time the heap floor moves. A row that places a
buffer within 512 bytes of a 64KB page on purpose and drives `WRITE_AT` and a
chain read through it is the follow-up (§5).

### 3.2 kern_emu did not assemble, and nor did five knob builds

Both at the base. kern_emu (`make emu`, `make vmmousetest`) was 2,500 bytes
of `.ovl` against the blob's 2,496, from the Floppy page's Cylinder work
taking kern_big's overlay to 2,494. `drv_boot_x`'s three walks over the driver
rows now ask one local question, so kern_big's overlay is 2,470 and kern_emu's
2,476. The 24 bytes then went to the knob overlay give: DISKAL=1, MOUDIAG=1
(both kernels), MOUROUND=1 and BOOTSTOP=1 had loaders past `OVL_AT - 144`.
`OVL_KNOBGIVE` is 128 now for a knob whose growth is in the loader and 136
for BOOTMARK=1, whose growth is all overlay; `soak -k buildmatrix` is green
(5 red at the base). Shipped kernels are byte-identical across that change.

### 3.3 Behaviour changes

None intended and none left. One was found at the coordinator's review and
put back: the disk agent's relay for `dskw_wabody`'s INSIDE arm sent a failed
`dsk_write_chain_x` to `dskw_rdata.corrupt` (`FERR_IO`) instead of
`dskw_ioerr`, so a WRITE-PROTECTED disk on `WRITE_AT` into an existing file
would have said `Disk error`. It has a `.jio` relay of its own again (+3).

## 4. WHAT WAS RUN

* The fast tier at every merge and every coordinator commit (54/54).
  `make small`, `make emu`, `make vmmousetest` at the close.
* **A 125-row integration soak** on the merged tree before §3.1, scoped to
  every subject the pass touched (boot, display, WM, desk, dock, files, disk,
  file dialog, LZ, heap and compaction, drivers, Control Panel, instances, the
  DOS return, kern_small's floor machine): 120 green. Of the five red, three
  were gate disks the run had not built (`ptsext`, `ptsmix`, `doscom`; green
  once built), `fddpage` was a scratch-image path inside the soak's private
  tree (green alone, twice), and `lzbig` was §3.1.
* After §3.1: `lzbig`, `lzload`, `lzfile`, `fcpcopy`, `dskwstage`, `wseq`,
  `czseq`, `hdboot`, `fmcommit`, `hddcp`, all green. After §3.2: `buildmatrix`,
  `vmmouse` (QEMU, kern_emu), `drvup`, `drvcall`, `cfgtrip`, `bootsmoke`,
  `smallboot`, `small128`, `fddpage`, all green.
* The whole soak tier was not run: it is the owner's to ask for.

## 5. WHAT IS LEFT

* **The agents' own "not taken" lists are lost.** The container restarted
  while all six were working; every agent had committed and left a clean
  tree, but none wrote its final report. What each costed and refused is in
  its commit messages where it was written down at all.
* **A row for §3.1**, as above.
* **The modules question stands answered**: FDLG and FILECP stay resident on
  kern_big by the owner's call. Pass 8's §4 list is otherwise unchanged.
* `.lowbss` (34 bytes of rung slack) and `.vgabuf` (176) are where the next
  rung is cheapest to find, and nothing in this pass looked hard at either.
* `tests/fddpage.py` writes a scratch image the soak's frozen tree cannot
  see; it passes run directly and fails under `os88soak.py`.

## 6. METHOD LESSONS

* **A layout-dependent failure looks exactly like an integration bug.** Every
  branch green and the merge red points at two agents' changes colliding.
  Here it was neither. The cheapest discriminator was a one-line padding
  build: same code, different heap floor. Try that before bisecting files.
* **Bisect the merge chain first.** Worktrees at each `--no-ff` merge commit,
  four rows at once, found the last merge in one round.
* **Agents commit as they go, and that is what survived the restart.** Six
  branches of committed, building work came through intact; six final
  reports did not. A pass that wants its "refused" lists should have agents
  append them to a file in the worktree as they decide, not hold them for
  the last message.
* **`make emu vmmousetest` under `-j` races** on `build/emuk/`; the symptom is
  os88kz's "the two passes packed DIFFERENT bytes". Build them one at a time.
