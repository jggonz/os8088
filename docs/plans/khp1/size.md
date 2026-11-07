# Kernel hot-paths pass 1: size

The size agent of the speed pass (`docs/plans/khp1/BRIEF.md`). Nine speed
agents spent resident bytes to buy time; this note is what was given back
**off** the hot paths: the inbox the speed agents kept
(`/home/user/wt/khp1-size-inbox/*.md`), the cold sides of the pass's own new
code, and a finder over the files the pass touched. Branch `khp1-size`, cut
from `kernel-hotpaths` at `957aac1d`, with `kernel-hotpaths` merged again at
`0b98062b` once the disk agent had landed.

Every item is behaviour-identical, ABI-neutral, and either FASTER or, in two
places named below, a taken jump on a slow arm. Nothing here changes a
routine in `tests/unit/t_hotpar.py`'s HOT table, and every cut leaves each
pinned routine at its parity on both kernels (`hotpar` green at every commit).

## 1. BASE AND TIP

Resident = `.text` + `.bss` + `.cold` + `.vga` + `.lowbss` + `.vgabuf`
(kern_mega: and `.mega`, given after the slash), from `tools/kernsize.py
--json` on each kernel, assembled.

| kernel | pass base `a7ee7fab` | `957aac1d` (this branch's cut) | `kernel-hotpaths` `0b98062b` (after disk) | **tip** | tip vs `0b98062b` | `KERN_SIZE` at tip |
|---|---:|---:|---:|---:|---:|---|
| **kern_big** | 91,530 | 92,910 | 93,370 | **93,287** | **-83** | 99,328 / 100,864 |
| kern_small | 57,258 | 58,013 | 58,473 | **58,408** | **-65** | 60,416 / 64,000 |
| kern_emu | 91,784 | 93,154 | 93,614 | **93,531** | **-83** | 99,328 |
| kern_mega | 103,575 / 126,631 | 104,958 / 128,014 | 105,170 / 128,226 | **105,094 / 128,150** | **-76** | 135,168 |

The whole pass at the tip is therefore kern_big +1,757 over `a7ee7fab`
(+1,840 before this note), kern_small +1,150 (+1,215). No `KERN_SIZE` rung
moved at this branch's scale - which, per CLAUDE.md's banner, is not the
question: the bytes are.

By section, tip against `0b98062b`: kern_big `.text` -30 and `.cold` -53;
kern_small `.text` -28 and `.cold` -37; kern_emu as kern_big; kern_mega
`.text` -30 and `.cold` -46. `.bss`, `.vga`, `.lowbss`, `.ovl` (5,117 of
5,120) and `.boot2` are unchanged on every kernel. Before `.cold` the branch
REMOVES 30 bytes, so the COLD_HOME notice in BRIEF section 8 is not moved
toward its next hazard. `tests/megaseg.txt`'s overhead reads 3,233 of 3,283.

## 2. TAKEN

Every row is FREE: no resident byte added on any kernel. Bytes are kern_big
/ kern_small / kern_emu / kern_mega.

| id | file | routine | what | bytes | cycles | commit |
|---|---|---|---|---:|---|---|
| size-1 | `apps/os88ui.inc` (kernel copy) | button body, `os88ui_sbar`, `os88ui_sbmove`, `os88ui_sbpos`, `os88ui_sbthdraw` | the kernel's `.cold` copy ends on `kretc_bp` / `kretc_si` / `kretc_dx` (SPEC.md 15.1.2); a package keeps its pops. `UI_KRET <rung>` at the file-level sites, the rung as a second macro argument to `OS88UI_BTN_BODY` because kern_small expands it into CTRL.DRV and FDLG.DRV (`OS88UI_KFAR`) | -20 / -12 / -20 / -20 | +~20 clocks a button or scroll-bar draw / thumb move, each already several drawing calls (~2.3 ms) | `7d6beace`, `a9972c68` |
| size-2 | `kernel/blank.inc` | `blk_relit` | wm-4's bank of AX..DX leaves by `kret_dx`. **Depends on `233ccd04` (wm-4)** | -2 / -2 / -2 / -2 | +~20 clocks once a relight (a ~600 ms repaint) | `68f50f2a` |
| size-3 | `kernel/vidsel.inc` | `vid_pt_clamp` | CX not banked: the one caller pops its own next | -2 / 0 / -2 / -2 | faster (a push and a pop) | `968d8df2` |
| size-4 | `kernel/wm.inc` | `wm_rz_swap` | DI not banked: `mov di, bx` after one call, `pop di` after the other | -2 all | faster | `968d8df2` |
| size-5 | `kernel/wm.inc` | `wm_su_bank` | AX and BX not banked (the caller leaves by `kret_di`); both arms then TAIL-jump into `wm_su_take` / `wm_su_drop` | -6 all | faster: two pushes, two pops and a call/ret gone a window draw | `968d8df2` |
| size-6 | `kernel/clip.inc` | `clip_drop` | BX not banked: `mov bx, MEM_K_CLIP` next, or `kret_bp` | -2 all | faster | `968d8df2` |
| size-7 | `kernel/memory.inc` | `mem_bcopy` | SI not banked: both callers `pop si` next | -2 all | faster, once a block moved | `968d8df2` |
| size-8 | `kernel/files.inc` | `fm_clamp_scroll` | AX not banked: `kretc_di`, or reloaded from `[fm_lscr]` | -2 all | faster | `968d8df2` |
| size-9 | `kernel/wm.inc`, `kernel/ui.inc` | `wm_dg_precover` | its BX and flags bank and its `call wm_rz_precover / ret` were dead; it is now three instructions that FALL INTO `wm_rz_precover` (pass 17's CROSS-FILE ~-8) | -8 all | faster, once a drag's drop | `eabd9200` |
| size-10 | `kernel/vga12.inc` | `gfx_sub_off` | AX not banked: both restore bodies pop it next | -2 all | ~27 clocks faster a gfx_restore (every menu dismiss, every save-under put-back) | `eabd9200` |
| size-11 | `kernel/toast.inc` | `toast_now` | AX not banked: `toast_show` leaves by `kret_di` | -2 all | faster | `eabd9200` |
| size-12 | `kernel/lz.inc` | `lz_lz4` slow paths | `.mslow`, `.lslow`, `.litx`'s long run and `.end` `call .sync` (lz_lzb's shape) instead of LZ_FRAME + `call lz_sync` each; LZ_FRAME retired. **Depends on `f47761a8` (disk-1)**. The disk agent's inbox item | -8 all | the inline symbol path is byte-identical at the same addresses; each slow-path ENTRY +~35 clocks against the several-hundred-clock round it makes, a handful a file (<0.02% of Note Pad's 1.12M-cycle decode) | `e59c61ea` |
| size-13 | `kernel/assoc.inc` | `assoc_scan` | CX comes back unchanged without its bank: the one write is inside the row's own push/pop. kern_big only (OS88_ASSOC) | -2 / 0 / -2 / -2 | faster, ~31 calls an open of B: | `64d97ff3` |
| size-14 | `kernel/wm.inc` | `wm_su_merge` | SI and DI not banked: `wm_su_edge` reloads both before its second call and leaves by `kret_bp` | -4 all | faster, twice a clipped-edge save-under restore (per raise) | `64d97ff3` |
| size-15 | `kernel/disk.inc` | `dsk_ico_stage` | AX not banked: both callers pop it next | -2 all | faster | `64d97ff3` |
| size-16 | `kernel/disk.inc` | `dsk_fatw_demote` | AX and BX not banked (mem_pg_forget pops both); both copies of the pops go, the live exit is `.out`'s `ret` and the IF = 0 arm falls into `dsk_fatw_home` | -10 / -4 / -10 / -10 | faster, once a FAT window shed | `64d97ff3` |
| size-17 | `kernel/disk.inc` | `dsk_rah_fill` `.multi` | the record stamp as `stosw` x4 + `stosb` with ES:DI on the record (both banked in the prologue); an %if pins the layout. **Depends on `1217b318` (disk)**. The disk agent's inbox item | -7 / -7 / -7 / 0 | once a launch's header peek; equal | `6538be65` |
| - | `kernel/vga12.inc` | `GFX_U_FLOOR` | comment only: its kern_small byte is a .bss parity pad now (see 3) | 0 | - | `6320019c` |
| | | | **total** | **-83 / -65 / -83 / -76** | | |

### 2.1 How the dead banks were found, and why they are safe

A finder over the assembled listing (scratch, not committed): every routine
whose prologue banks a register, reached ONLY by near `call`, where that
register is written, popped, or put back by a `kret*` rung before it is read
on every path out of every call site (jumps followed, both arms of a `jcc`).
Its hits were then READ, one by one, because it is a heuristic and it was
wrong four ways, each caught by reading:

- **a fall-through entry** - `wm_resize_nb` is entered by falling out of
  `wm_resize`, whose callers the finder never saw;
- **a reference that is not a call** - `osapi_file_here` is an API slot
  through the `OSAPI_RSLOT` macro, whose invocation has no listing bytes;
- **a shared tail** - `wm_create` (`jmp wm_create.out` from elsewhere),
  `cur_lazyck` (its pushes join `cur_lazyrect`'s), `mem_claimp_sd`;
- **its own bug** - it read `mov [bx+disp], ax` as a write of BX
  (`dsk_vol_drop_drv_x` came out of that). Fixed, re-run; no taken item
  depended on it.

Each taken item's header now says what it spends.

### 2.2 Equivalence

- **size-17**: a unicorn harness runs the base and tip stamp loops on 3,000
  random (LBA, run length 10..180, drive, signature) and compares the
  table, SI, DX and SP: 0 differ. Storing END = 1 instead fails 3,000 of
  3,000.
- **Everything else is a reading**, recorded per item above and in each
  commit: a removed bank is equivalent exactly when no path from the call
  site reads the register before it is rewritten - which is what was read -
  and a ladder rung is the same pops as the run it replaces (the rung -> pop
  map `tests/unit/t_asmrules.py` checks). Every package is byte-identical
  across size-1 (`build/*.o88` md5 before and after; only the kernel modules
  and `syscheck.o88`, whose manifest names the kernel, move).
- `tools/stkbalance.py` over `kernel/*.inc`, `kernel/kernel.asm` and
  `apps/os88ui.inc`: 0 unbalanced paths at the tip, as at `0b98062b`.

## 3. REFUSED, AND HELD TO PAIR

**From the inbox:**

- **gfxcall: `gfx_clip_run`'s epilogue as `jmp kret_di`** (-4, every
  kernel). MEASURED with `os88hot.py cycles gfx_clip_run`, CGA: a raise and
  PgDn/PgUp make NO clipped call; a drag makes one, 74,111 cycles. The jump
  is a taken near `jmp` and a queue refill, ~+19 clocks a clipped primitive -
  0.03% of that call. Refused anyway: it is H2 by the brief's scale (per
  primitive call), SPEC.md 15.1.2 names `gfx_clip_run` among the eleven
  exits that keep their pop runs for exactly this reason, and this agent's
  rule is no cycle on a hot path. Four bytes the owner can take with one
  line if a cycle a clipped call is acceptable.
- **wm: `wm_dmg_wins`' two inline mask builds as a shared helper.** It is
  -2, not ~-6 (`mov cl, al / mov bx, 1 / shl bx, cl` is 7 bytes and the
  helper is 8 plus 3 a call, with `wm_dmg_bit` rewritten on it), and it puts
  a call/ret (~40 clocks) into the marking loop per window pair. Refused.
- **wm: `wm_draw_title`'s gap test before its rect arithmetic.** The wm
  agent's own estimate is 0-3 bytes either way; not re-derived.
- **pointer: `GFX_U_FLOOR` on kern_small 20 -> 16** (-1 `.bss`). Correct -
  `cur_move_mono`'s set is 12 now, gfx_scroll's 16 is the largest other one
  - but the byte is the first-but-one of kern_small's `.bss` (`gfx_u` is its
  first label), so cutting it moves `[ticks]` and every scheduler word after
  it to an ODD address: a second bus cycle a word on an 8086, V20 or 286.
  **Held to pair** with any odd cut of kern_small's `.text` tail, which moves
  the `.bss` start the other way (the comment now says so: `6320019c`).
- **files: `fm_draw_icon16`'s `.orw` all-zero test** (~-10 `.cold`, ~1.6k
  cycles an icon). `dsk_ico_stage` would hand back ZF for "no body"; the
  file is open now, but the contract change spans two routines and four
  paths and was not reached. Next pass.

**From the disk agent's inbox** (`disk.md`): `lz_lz4`'s `.sync` and the
`.multi` stamp are size-12 and size-17. The third, **`disk_mount_x`'s
listing-mount predicate computed once**, is refused: no register survives
from step 1 to step 2b (the BPB bank, the parse and the FAT window load
spend all of them), and a memory flag costs a store on each arm for the
compare it saves - ~0 net.

**From the finder and the pass's own code:**

| item | bytes | why not |
|---|---:|---|
| `vid_setmode`'s 8-pop epilogue as `jmp kret_es` | -6 | the loading screen calls it while `.text`'s end - the ladder - is not aboard (kernel.asm's own rule) |
| `osapi_file_here` AX | -2 | an API slot: a package may hold AX across it. See section 5 |
| `wm_resize_nb`, `wm_create`, `cur_lazyck`, `mem_claimp_sd`, `drv_blk_call_x` | - | fall-through entries or shared tails (2.1) |
| `fm_stage_name` SI | -2 | far-entered from FDLG.DRV and FILECP.DRV (`FDX`/`FCPX`), whose contract is "every register kept" |
| `os88ui_sbthdraw`'s `add ax, 2` / `sub cx, 2` as inc/dec pairs | -2 | shared package source: it would change every package with a scroll bar |
| `font_str_run`'s epilogue as `jmp kret_es` (text-6's code) | -6 | +~20 clocks a transparent STRING, H2 |
| `task_exit`'s bound walk shorter, to retire sched-1's pad | -2 | every shorter shape found is 26 bytes against 25 |
| removing pad PAIRS inside one pinned interval (nine pads between `gfx_blit4` and `font_run`, three before `ui_task`, three before `sch_isr`) | -2 a pair | the code between the two pads shifts ODD, and in every interval looked at it holds measured hot or per-event code (`font_str_x`, `task_sleep` - `ui_task`'s idle sleep -, `fpg_finish`, the `mou_bases` word table) |

**Held to pair** (each built and correct, each an odd byte count where it
sits, so each needs a partner in the same pinned interval or a pad removed
next to it):

| item | bytes | interval / note |
|---|---:|---|
| `mou_lockon`'s bank (its one caller, `mou_hotplug`, banks AX..DX and leaves by `kret_dx`): pushes gone and `jmp kret_cx` -> `ret` | -5 (or -9 written at its call) | between `font_run_cell` and `mou_p2_isr`, no pad after it; and its name is in a dozen comments and `tests/serlease.py` |
| `cur_lazyend`'s AX (pointer-6's code): the "not moved" path's push/pop go, the push moves to `.move` | -1, ~27 clocks off every unmoved unlock | between `mou_isr_body` and `cur_move` |
| `wm_su_rect`'s `sub dx, 2` -> `dec dx / dec dx` | -1 | +1 clock on a 286 |
| `osapi_mouse`'s `mov di, 2` -> `inc di / inc di` (the FSX re-hook arm) | -1 | +2 clocks on a 286 |
| `sw_rp` (mono-1's code) `resw` -> `resb` | -1 `.bss` (kern_big) | moves every later `.bss` word's parity |
| `ui_drain` `resw` -> `resb` (sched-2 left it a word "for ui_dragx's alignment") | -1 `.bss` (both) | as above - and see section 7, item 2 |

## 4. WANTS THE OWNER

Nothing. No item changes the settled picture, a transient, a refusal or a
result.

## 5. WANTS AN ABI BREAK

| the break | bytes | cycles | callers |
|---|---:|---|---|
| `OSAPI_FILE_HERE` documented to clobber AX (`osapi_file_here` banks it only for its contract: inst_where spends it) | -2 | ~27 clocks a call | every package that asks where it stands; not worth a flag day alone - fold into the next renumbering |

## 6. DISK-FILE FINDS

The disk agent merged during this work (`9e95521f`), so its files were opened
and four items TAKEN (size-12, 15, 16, 17). Left for the next pass, all from
the finder and NOT read to the end:

- `dsk_rah_serve` CX and `dsk_rah_fill` CX/DI - the finder calls them dead at
  their one call site each, but both are on `dsk_xfer`'s per-transfer path,
  which the coordinator names hot (`dsk_rah_fill`'s fill); a removed push is
  faster, but the `.count` path after the serve was not read.
- `dsk_rah_redo` AX (two callers; `.out` is shared with `dsk_rah_reloc`).
- `dsk_runcap` AX - pinned in t_hotpar's HOT table; a removal would be
  faster and must keep its parity.
- `dsk_vol_drop_drv_x` AX - leaves by `kretc_cx`, so only the push goes:
  -1, odd.
- kern_small only: `dsk_bpb_bank_zap` AX, `dsk_free_clus_x` CX, `dsk_find_x`
  SI, `dsk_byclus_x` DI, and outside the disk files `wm_show_b`, `fmv_get_icon`,
  `fmv_demote`, `fm_choose`, `desk_cmark`, `desk_paint_mask_x`, `desk_open`,
  `drv_vol_back`, `mem_pg_forget`, `mem_bigrun` - dead only on kern_small's
  call graph, so each wants an `%ifdef` or a kern_big caller fixed first.

## 7. ROWS RUN

Covering rows, run ONCE after the last code commit, scoped, `--marty-jobs 1`:

`python3 tools/os88test.py soak -k sbar -k sbardlg -k fmthumb -k fdlgchoose
-k saver -k zoomsave -k dragui -k dockmark -k dispdrag -k clipkeep -k
heapcheck -k fatwpin -k pkgread -k toastbar -k dispseam -k lzship -k
lzload-lz4 -k lzload -k assocfind -k icoclip --marty-jobs 1`

**20 passed, 0 failed, 0 skipped** in 598.8 s at `a9972c68` (the last code commit): `dragui` 10.5s, `assocfind` 11.4s, `clipkeep` 19.5s, `saver` 9.6s, `fatwpin` 130.6s, `dockmark` 20.9s, `pkgread` 8.5s, `heapcheck` 11.0s, `dispdrag` 11.0s, `icoclip` 8.0s, `dispseam` 109.1s, `lzload` 7.3s, `lzload-lz4` 52.4s, `lzship` 81.4s, `fdlgchoose` 24.5s, `fmthumb` 4.8s, `toastbar` 4.8s, `sbar` 8.3s, `sbardlg` 9.3s, `zoomsave` 55.9s. `lzship` ran 81.4 s against a declared 30 s; that is its own declaration, not a failure.

What each covers: `sbar`, `sbardlg`, `fmthumb`, `fdlgchoose` the os88ui
exits (size-1); `saver` blk_relit (size-2); `dispseam` vid_pt_clamp (size-3);
`zoomsave` wm_rz_swap and the save-under bank and merge (size-4, 5, 14);
`dragui`, `dockmark` wm_dg_precover (size-9); `dispdrag` the save-under
restore (size-10, 14); `clipkeep` clip_drop (size-6); `heapcheck`,
`pkgread`, `fatwpin` mem_bcopy and dsk_fatw_demote (size-7, 16); `toastbar`
toast_now (size-11); `lzship`, `lzload`, `lzload-lz4` the LZ4 decoder's slow
paths and the launch peek's fill (size-12, 17); `assocfind` assoc_scan
(size-13); `icoclip` dsk_ico_stage (size-15). fm_clamp_scroll (size-8) is
reached by every Disk-window scroll in `fmthumb`.

The fast tier (`make`, 73 rows) and `hotpar` passed at every commit; `make
small`, `make emu` and `make mega` build at the tip.

## 8. DEFECTS FOUND

1. **The suite's `stkbalance` row does not walk `apps/os88ui.inc`'s kernel
   half.** Its corpus is `kernel/*.inc` and `kernel/kernel.asm`
   (`_kernel_sources()`), and the kernel's `.cold` copy of os88ui is
   assembled from `apps/`. size-1's first form passed `kretc_bp` as a macro
   argument, which stkbalance reads as an address taken and walks from depth
   0 (`kretc_bp: ret at depth -7`); `make` stayed green and only a hand run
   with `apps/os88ui.inc` in the list showed it. Fixed in `a9972c68`. Adding
   `apps/os88ui.inc` to the row's kernel corpus is clean at the tip (0
   findings) and would have caught it: NOT done here (a harness change, for
   the coordinator).
2. **`ui_drain`'s comment says it is a word "for ui_dragx's alignment"**,
   and on kern_big `ui_dragx` is at an ODD address (`ADE9h` at `957aac1d`):
   kern_big's `.bss` starts at an odd offset (`.text` is 41,781 bytes there),
   so its word alignment is whatever `.text`'s length makes it and nothing
   holds it. Not fixed; it matters only on a 16-bit bus, and the pass has
   never pinned `.bss` parity. A deliberate `.bss` alignment would be a
   decision with bytes attached.
3. `tools/os88hot.py cycles gfx_clip_run raise` reports "no call ... in 1
   reps" correctly - a raise draws its front window unclipped - which is
   worth knowing before reading the gfxcall item as hot on a raise.
