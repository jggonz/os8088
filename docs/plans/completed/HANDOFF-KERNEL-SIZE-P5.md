# Kernel size pass 4: the record, and what is left for a fifth

**PASS 4 HAS LANDED.** Nineteen batches and a fix on `kernel-size-p4`, cut from
`elendilon` at `6519baaa`. The brief was *"reduce kernel size by 2KB or more"*,
with the owner's permission to look at the kernel as a WHOLE, to change the
ABI (every package in the tree is rebuilt), and to spawn agents on targets
worth the effort; hot drawing paths were not to lose speed.

The file name is `-P5` because `HANDOFF-KERNEL-SIZE-P4.md` is **pass 3's**
record (each pass's record was named for the pass it handed to). This is pass
4. Its companions, and this file repeats none of them:

* **`docs/plans/completed/HANDOFF-KERNEL-SIZE.md`** — pass 1's handoff, still
  the authority on **method**.
* **`docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md`** — pass 3's record. Its §2
  (what was left) and §3 (the refusals that are proofs) were this pass's
  starting brief.
* **`docs/KERNEL-MEMORY.md`** — where the budgets stand. Blessed at the close
  of this pass.

---

## 0. THE OUTCOME

**Both kernels cleared the 2KB target**, measured by `tools/kernsize.py` as
the sum of the RESIDENT sections (`.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf`) against the base tree — never against a rung count:

| | kern_big base | kern_big close | Δ | kern_small base | kern_small close | Δ |
|---|---:|---:|---:|---:|---:|---:|
| `.text` | 50,358 | 47,223 | −3,135 | 37,423 | 35,698 | −1,725 |
| `.bss` | 6,114 | 5,524 | −590 | 4,189 | 3,485 | −704 |
| `.cold` | 41,020 | 40,004 | −1,016 | 26,394 | 25,854 | −540 |
| `.lowbss` | 6,366 | 6,366 | 0 | 3,636 | 3,636 | 0 |
| `.vgabuf` | 848 | 336 | −512 | 0 | 0 | 0 |
| **resident** | **104,706** | **99,453** | **−5,253** | **71,642** | **68,673** | **−2,969** |
| `.text`+`.bss` (the binding segment) | 56,472 | 52,747 | **−3,725** | 41,612 | 39,183 | −2,429 |
| `KERN_SIZE` | 111,104 | **105,984** | −5,120 | 74,240 | **71,168** | −3,072 |

The pass ran in three rounds. Batches 1-10 took it past 2KB on both kernels
(−2,921 / −2,004); the owner then reviewed what was left and took five more
(batches 11-15, −1,740 / −642); and a third round (batches 16-19, −592 /
−323) took the inline cells, the near-duplicate blocks, the removal of the
module entry cap and the extended desktop's second wave. The table is the
pass's OWN work. The branch also carries the compression session's streamed
Compress and kernel truncate, merged in after batch 18, which cost `.cold`
+200 on kern_big and +94 on kern_small; `kernsize` on the branch therefore
reads 99,653 and 68,767, and that difference is theirs rather than a
regression of this pass.

`KERN_SIZE` moved further than the byte sum because rungs round separately —
the banner in CLAUDE.md is why that figure is quoted beside the byte sum and
never instead of it. On kern_big the segment (`KERN_CODE_MAX`) goes 9,064 →
12,448 left.

**What a VGA machine and a mono machine each get.** `.vgabuf` is the one rung
a mono machine never reserves (SPEC.md 39.22), so batch 8's −512 is a VGA
machine's alone. A Hercules or CGA kern_big machine gets the other −4,149,
and batch 15's −851 is every machine's that does not EXTEND its desktop - a
two-adapter machine that does pays it back as a 2KB heap claim while
extended.

## 1. WHERE THE BYTES CAME FROM

| batch | what | kern_big | kern_small |
|---|---|---:|---:|
| 1 | `.bss` sized to its use: the BPB bank 64 → 32 a volume, `inst_tab` from `INST_MAX`, a one-plane cursor save-under with no VGA | −288 | −424 |
| 2 | `font_run`'s glyph table and edge bytes are a union with the icon renderer's scratch cell | −196 | −180 |
| 3 | four dead `disk.inc` routines, dead `os88ui` button tracking in the kernel's copy, four dead far shims, a case fold in `fm_onkey_x`, shared failure tails in `drv_load_row` | −274 | −100 |
| 4 | **the API table's rare cells shrink to six bytes** (an ABI flag day, below) | −402 | −402 |
| 5 | two more dead shims; nine kern_big-only shims gated out of kern_small | −8 | −48 |
| 6 | a shared push prologue (`kentc_di`/`kentc_bp`) for `.cold` routines | −135 | −42 |
| 7 | the BPB bank and stage shrink again, to the 18 bytes rules 3–16 read | −125 | −69 |
| 8 | **the VGA decode table shares the mono pair tables** (a measured speed trade, below) | −505 | 0 |
| 9 | **the window record absorbs its eleven side tables**; drag and grow share one loop; the built-in dock icons are row runs | −600 | −533 |
| 10 | cold-side trampolines for multi-site `.cold` → `.text` far calls; kernel windows call their `.cold` callbacks directly | −388 | −206 |
| | *round one* | *−2,921* | *−2,004* |
| 11 | **a failed load says two things** (`Load failed` / `Out of memory`, `LDDIAG=1` for the four reasons), and the Task Manager's and the Wire zone's failure is a toast rather than a notice window | −216 | −182 |
| 12 | `rect_get`/`rect_put` at the 19 measured-cold rect sites (the other 15 held in LAST-DROP-BYTES 7.10) | −142 | −99 |
| 13 | **boot-only code into the stage-2 blob**: `kmain_o`, `BLOBCALL`, `vid_detect`/`vid_init`/`hb_probe_x`; knob builds get room of their own (SPEC.md 2.5.3.3) | −334 | −248 |
| 14 | a document's missing program is a toast, `Needs TRACKER.O88`, and `ui_note` is gone | −197 | −113 |
| 15 | **the extended desktop is an on-demand module, `EXTD.DRV`** (SPEC.md 39.19.6) | −851 | 0 |
| | *round two* | *−1,740* | *−642* |
| 16 | **the inline cell**: fifteen API cells whose routine is one memory access ARE that access (`2E <mov> [cbw] CB`), and are faster for it (SPEC.md 20.3) | −89 | −82 |
| 17 | near-duplicate blocks share one body: the file layer's seven by-name bodies, its data loops, its two directory scans, its shared release tail, `drv_fs_call`, `wm_covered` | −242 | −201 |
| 18 | **`MOD_NENT` is gone**: each module's slot block is its own entry count, so an entry costs 4 bytes of `.bss` plus a byte a call site and there is no ceiling (SPEC.md 2.8.1) | −36 | −38 |
| 19 | the extended desktop's second wave: nine more second-display pieces into `EXTD.DRV` (12 entries), and the Control Panel's key handler into `CTRL.DRV` | −225 | −2 |
| | **total** | **−5,253** | **−2,969** |

### 1.1 The two ideas that were new

**The API table has two cell sizes (batch 4, SPEC.md 20.3).** Every cell was
8 bytes because the plain SLOT is — `push ds / push cs / pop ds / call / pop
ds / retf`, the fastest segment switch there is. That is right for the 48
cells some package calls per frame, per draw or per event, and they are
byte-identical. The other 106 became `push bp / call api_r<kind> / dw
target`: the rare body pops its own return address, which IS the address of
the target word. ~18 µs a call on a 4.77 MHz 8088, estimated rather than
measured. Eight withdrawn cells were deleted rather than kept as `stc`/`ret`
stubs, which retires SPEC.md 20.3.1's free list: with a renumber-anything
ABI, a withdrawn cell's SDK name is deleted and the source that still names
it fails to assemble.

**The window record absorbs its side tables (batch 9).** Eleven per-slot
tables sat beside `wm_wins` on the argument that a field in the record costs
every reader the stride. On an 8088 that is false: there is no cache, and a
record field is the same `[bx+disp8]` as any other. What the side tables
DID cost was a `wm_ptr2idx` — a `div` — and a scale on every access, and
eight `*_slot` helpers existed only for that. So this is the one batch that
made hot paths faster while saving bytes. `WIN_SIZE` 72 / 65.

### 1.2 The one speed trade, and the owner took it

Batch 8 puts `vga_p4tab` (512 bytes) over `gfx_pairtab0`/`gfx_pairtab1` in
`.lowbss`: the two serve different adapters, each builder clears the other's
built flag, and both flags are tested at every `gfx_blit4` under the gfx
lock. The decoder then reaches the table through `SS`, which is an `ss:` on
its four table loads per eight pixels. **Measured** with `tests/blitplane.py`
on MartyPC `os8088_xt_vga`: even phase 5,405,546 → 5,543,959 cycles
(**+2.6%**), odd 7,341,227 → 7,481,582 (**+1.9%**), pixels identical, the
decoder still 6.2× / 4.6× the span writer. The owner's reading, and the
right one: 2–3% off an optimisation that is itself 5–6×, touching nothing
but `gfx_blit4`'s planar row decoder. SPEC.md 39.22.1.

### 1.3 A defect this pass introduced and fixed

**Batch 4 renumbered the table and three on-demand modules did not follow.**
`HIBER.DRV`, the cloner and its compressor carried their cell offsets as
literal equates, and six of ten moved: `hb_ok_x` far-called the middle of
another cell, the `hibernate` soak row failed (the window never appeared),
and after batch 10 moved the code underneath it the same press CRASHED.
`t_api_abi.py` compares the SDK with the table and could not see a module's
private constant. Fixed in `7e50a94f`: every one is an `apic_*` label on the
cell itself, so the assembler derives the address and a renumber cannot
strand it. **Whoever renumbers the table again: grep `kernel/` for
`KERNEL_SEG:` targets that are not labels** — there are none now, and a gate
that says so would be cheap.

### 1.4 Batch 10's commit message lost a fragment

The shell ate a backquoted `call COLD_SEG:wm_cbd`, so `0fc1c465`'s message
reads *"wm_pkgcall's kernel arm is (call bp / retf)"*. It is **`call
COLD_SEG:wm_cbd`**, where `wm_cbd` in `.cold` is `call bp / retf` — the old
thunk's four transfers in the other order, so the cost and the stack depth at
the callback are unchanged.

### 1.5 Batches 18 and 19: the cap, and what the cap was hiding

`MOD_NENT` = 7 was a uniform slot count per module. It left 13 slots
allocated and never armed on each kernel, made an eighth entry a hard NASM
error in whichever module grew, and could not be per-build because
`tools/os88mod.py` scraped it with a regex. It had bitten three times; the
last was a compression session in another branch the same day. Each
module's block is now its own `X_NENT` (kernel.asm's `MODFP` macro, which
refuses a block out of `MOD_*` order or a count below one), `mod_fpt` maps a
row to its block, `mod_check` demands the header's count EQUAL the kernel's,
and the `O8MM` map carries the kernel's count to the host tool. Removing the
cap SAVED bytes (−36 / −38) rather than costing them.

Batch 19 then took the extended desktop's second wave at −225, **minus the
performance-sensitive calls**, as the owner asked. Moved: `vid_disp_desk`,
`wm_disp_span`, `wm_fs_setrect`'s arm, `wm_display`'s secondary arm (gated),
`wm_kind_now`'s arm, `ui_ylow` (gated), `fsx_caps`'s and `fsx_mode`'s arms
as `EXT_FSX` selectors, and `wm_zoom_xmax` shares `wm_disp_xw` rather than
copying it. `cp_onkey_x` went into `CTRL.DRV` on kern_big (kern_small's
panel has no key handler). Refused, and staying resident:
- `wm_disp_of` and `wm_disp_xw`'s arm are reached under every window's
  content rect, `OSAPI_WM_CONTENT` included, which Tracker calls per draw;
- `fsx_surf`'s arm is asked per frame inside a bracket;
- `ui_drag_dead`/`ui_drag_phase` are the drag loop;
- the two secondary-click tests in `ui_task` would put a far call on every
  press on a one-display machine.

A one-display machine pays ~+20 cycles (a far call to `mod_gone`) only on
create, fit, resize, zoom, the fullscreen toggle, a whole-desktop repaint and
`fsx_caps`/`fsx_mode`; nothing per frame, move, tick, primitive or glyph.

No registered row types into a driver's Control Panel page; the agent drove
it with an ad-hoc MartyPC script (RAMDISK.DRV's size box, with a negative
control), which is a row worth registering if that path changes again.

## 2. WHAT IS LEFT, costed

| candidate | kern_big | kern_small | why not taken |
|---|---:|---:|---|
| `rect_get`/`rect_put` for 37 four-word load/store sites | −298 | −245 | **S1 TAKEN as batch 12** (19 sites, −142) after the sites were COUNTED on MartyPC; S2 (damage repaint, ~80) and S3 (save-under, ~70) held in LAST-DROP-BYTES 7.10 |
| boot-only code into the blob (`kmain`'s pre-mount half, `vid_detect`, `vid_init`, `hb_probe_x`; a `BLOBCALL` macro and three `os88ovlchk` rules) | −270 | −208 | **TAKEN AFTER THE CLOSE, as SPEC.md 2.5.3.3** — at −334 / −248 with the post-mount half, `dsk_ltrtab` and two `desk_init` thunks folded in. The owner took kern_small's blob at 42 bytes free, and the knob builds that overflowed it got knob-only room (2.5.3.3.1) rather than a second blob length |
| the extended desktop's WM code as a kern_big on-demand module | ~−850 | 0 | **TAKEN as batch 15**, `EXTD.DRV`, at −851, and the second wave as batch 19 at −225 once batch 18 removed the entry cap. What stays resident is §1.5's list |
| inline cells whose routine fits in 8 bytes (`get_ticks`, `set_color`, …) | −55 | −55 | **TAKEN as batch 16**, fifteen cells, −89 / −82, and faster |
| `ui_tm_errs` duplicates `fm_stattab` | −125 | | **TAKEN, and further, as batches 11 and 14** — the owner folded four verdicts into `Load failed`, made every failed launch a toast and retired `ui_note` |
| eleven near-identical block pairs (`dskw_mkbody`/`rmbody`/`dbody`, `dskw_wdata`/`rdata`, …) | ~−280 | | **TAKEN as batch 17** where it paid, −242 / −201; the refusals are in its commit message (per-move `W_ONDRAG`, IRQ0's `snd_tick` path, and doors whose code costs what it saves) |
| `mov word [wm_clip_n], 0` → `call wm_clip_clear` at ~18 non-hot sites | ~−54 | | |
| `inst_icobuf` onto `ico_ibuf` | −64 | | REFUSED: it is filled on a dying package's WORKER while task 0 may be staging |

**Two enablers, not savings.** Reordering `.lowbss` so the worker stack pool
(`sch_stacks`, 2,816 bytes, unused before the first spawn) follows
`dsk_secbuf` would raise the `.ovlw` ceiling by ~2.7KB at no resident cost —
which is what would let boot-only bodies move into `.ovlw` again, kern_big's
`.ovlw` being at 5,110 of 5,120. And the `jcc` residual pass 3 costed is
unchanged in kind.

## 3. THE REFUSALS

* **String compression**: resident strings are 2,118 bytes; every consumer
  hands `DS:SI` straight to `font_run`, a toast, a menu or the file layer, so
  each needs a decode buffer. Under ~200 net, with risk on every notice path.
* **Boot-only data**: ~26 bytes on either kernel, and already overlaid (the
  clock probe's scratch in `clk_str`). The owner's instinct was right in kind
  and previous passes had already spent it.
* **Moving FDLG/FILECP to modules on kern_big**: ~5–7KB and the machinery
  exists, but the owner's standing decision (KERN-SMALL-MODULE-SPLIT) is that
  kern_big keeps them resident for speed.
* **Unions refused on lifetime**: `snd_xlat` (a worker can call the sound API
  without the gfx lock), `wm_clip_tab` (live through every painter hold),
  `dsk_ico` (staged by the mount without the gfx lock), the UI text buffers
  (they nest), the file-copy/dialog/loader scratch (those operations suspend
  to the event loop).

## 4. METHOD LESSONS

* **Six agents in parallel, each with a topic and a private worktree**,
  produced patches measured on their own base; the main tree applied them
  with `git apply -3`. Every agent's figure reproduced on the branch.
* **A worktree made mid-session silently takes the newer base.** Two agents
  reported against a HEAD that had moved; both said so. Quote the base.
* **An ABI renumber needs a search for PRIVATE copies of the address**, not
  only the SDK. §1.3.
* **`stkbalance` learns entry depths from jumps.** Deleting the unused 8-byte
  cold macros left `api_xc`/`api_n` as labels reached only by fall-through,
  and the walker then started them at depth 0 and reported two false leaks.
  They are comments now.
* **A build that reads the source tree while it is being edited sees a
  half-written file.** One `small128` run failed on `kernel.asm:6207`,
  `parser: instruction expected`, mid-edit; the re-run was green.

## 5. WHAT WAS RUN

The fast tier on every batch; per batch, the MartyPC soak rows the batch
could reach (mount and copy: `deskfdd`, `fcpapi`, `fcpcopy`, `fcpsmall`,
`hddcp`, `volsig`; the API: `pkgrun`, `pkgbig`, `kernresident`; the window
system: 16 rows from `bootsmoke` to `wmartifact`; the decoder: `blitplane`,
`blitcut`, `dispseam`; the modules: `hibernate*`, `diskclone`, `xmcheck`,
`lzmod-lzb`; the callbacks: 17 rows from `fmcommit` to `dockmodule`), plus
`small128` and `bootsmoke` throughout. `soak -k buildmatrix` at the close.
The whole soak tier was not run: it is the owner's to ask for.

**Round two** (batches 11-15) ran the same way, per batch: the failure
toasts were driven on MartyPC by pointing the Task Manager at a missing file
and at a directory in SYSTEM, and by double-clicking `BEVERLY.MOD` off
`media360.img` with no Tracker anywhere; the rect sites were COUNTED with
execution breakpoints before any was converted; batch 13 ran the boot rows
and `buildmatrix` (four new kern_small knob rows); batch 15 ran 47 rows -
every `disp*` row but `dispfit`, which fails identically at the base (its
second adapter switch never takes), plus the new `extdmod`, `kernmods`,
`fsxdisp` and the Control Panel, dock and association rows - 47/47.

**Round three**: batch 16 ran `pkgrun`, `pkgbig`, `tank`, `evqfull`, `telpen`
and nine more (15/15); batch 17 ran 22 file-layer rows in the agent's tree
and 12 on the merged one plus an ad-hoc RAM-disk and rmtree run; batch 18 ran
the eleven module rows (`cpup`, `fcpsmall`, `fcpcopy`, `extdmod`,
`fdlgdrop`, `dockmodule`, `hibernate`, `diskclone`, `lzcomp`, `bootsmoke`,
`small128`) and `buildmatrix`; the merge with the compression work ran
`lzbig` and seven more; batch 19 ran 35 rows in the agent's tree (every
`disp*`/`fsx*` row; `dispfit` fails at base as always) and 14 on the merged
tree, plus `buildmatrix`.
