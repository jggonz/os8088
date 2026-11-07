# Kernel size pass 18: hot

The `hot` agent (BRIEF.md section 2): the routines `fresh-routines.md`
assigns `hot` - the speed pass's lean picks, `KHP_LZ`, disk-3/disk-5 and the
free items' routines - shrunk AT THEIR SPEED. Branch `ksp18-hot`, cut from
`c5a7b0f9`. BRIEF.md section 1 binds every item: no khp1 pick reverted, a hot
routine changed only cycle-neutral or faster with the count below, no
`int 13h` added, `hotpar` green.

## 1. BASE AND TIP

Assembled (`tools/kernsize.py --json` on each kernel; kern_mega from `make
mega`'s `kernsize[mega]` lines). Resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga`, plus `.mega` on kern_mega. The base was built
in a private worktree of `c5a7b0f9` (kern_big, kern_small, kern_emu and
kern_mega kernels), and agrees with BRIEF.md 0.1.

| kernel | | `.text` | `.bss` | `.cold` | `.lowbss` | `.vgabuf` | `.vga` | `.mega` | **resident** | `KERN_SIZE` |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| **kern_big** | base | 40,633 | 5,376 | 38,034 | 5,358 | 336 | 2,030 | - | 91,767 | 97,280 |
| | tip | 40,617 | 5,376 | 37,992 | 5,358 | 336 | 2,030 | - | **91,709 (-58)** | 97,280 |
| kern_small | base | 30,232 | 2,946 | 21,287 | 2,868 | 0 | 0 | - | 57,333 | 59,392 |
| | tip | 30,218 | 2,946 | 21,273 | 2,868 | 0 | 0 | - | **57,305 (-28)** | 59,392 |
| kern_emu | base | 40,877 | 5,376 | 38,034 | 5,358 | 336 | 2,030 | - | 92,011 | 97,792 |
| | tip | 40,863 | 5,376 | 37,992 | 5,358 | 336 | 2,030 | - | **91,955 (-56)** | 97,792 |
| kern_mega | base | 46,307 | 5,414 | 46,573 | 5,574 | 336 | - | 23,072 | 127,276 | conventional 63,488 of 68,096 |
| | tip | 46,291 | 5,414 | 46,544 | 5,574 | 336 | - | 23,072 | **127,231 (-45)** | conventional **62,976** of 68,096 |

`.boot2` (6,456 / 4,742 / 6,571 / 6,773) and `.ovl` (5,117 / 1,474 / 5,117 /
5,116) are unchanged on every kernel. kern_mega's conventional footprint came
down a 512-byte step (`.text` -16), which is the rung and not the point.
`tests/megaseg.txt` is unchanged: kern_mega still carries 3,682 bytes beyond
kern_big in the segment. No module or driver image moved.

## 2. TAKEN

Bytes are kern_big / kern_small / kern_emu / kern_mega. H-grades are khp1
BRIEF section 1's. Clocks are 8088 clocks, by hand count against the
instructions removed or replaced (PERFORMANCE.md Part 2's `max(clocks, 4 x
bytes)` fetch floor noted where it decides), and each equivalence is named.

| # | commit | routine | grade | what | bytes | speed |
|---|---|---|---|---|---:|---|
| 1 | `bfa9bd8f` | `lz_lz4`, `lz_lzb`, new `lz_fsync` (KHP_LZ) | H1 fast path untouched; slow paths once a segment | `lz_fsync` = `mov bp, sp / add bp, 4` falling into `lz_sync`, placed AFTER the arms. lz_lz4's four slow paths call it directly - its `.sync` stub and the `jmp` in it go; lz_lzb's `.sync` is `mov bl, dl` falling into it. Both arms now sit at an EVEN shift (lz_lz4 -34, lz_lzb -50) | -11 / 0 / -11 / -11 | one `jmp` (~15 clocks and a queue refill) fewer on every slow-path entry of both arms |
| 2 | `bfa9bd8f` | `lz_len` (KHP_LZ) | per extended length | under KHP_LZ both of lz_lz4's calls are a nibble of 15 with CX = 15 already, so lz_len is `cbw` and `.x`: its dead `mov cx, ax / cmp al, 15 / jne` goes and the two callers' `cbw`s are its | -8 / 0 / -8 / -8 | identical instructions on both paths (the `cbw` moved into the callee) |
| 3 | `bfa9bd8f` | `lz_lzb` `.lslow` (KHP_LZ) | once per 64KB of output | the one literal byte the budget refused goes through `lz_take` with CX = 1 and `lz_copy`, whose `lz_cross` is the `movsb` and the boundary's `lz_dbump` spelled out (SI cannot wrap at a literal: it is under the checkpoint) | -7 / 0 / -7 / -7 | ~+100 clocks, only when the budget is out - at a 64KB boundary of the output, so at most once a segment (0 times for a package under 64KB). < 0.01% of any decode |
| 4 | `63360142` | `dsk_rah_fill`, `dsk_rah_hint` -> `dsk_rah_lim` | H4, every fill | the listing cap IS the fill's ceiling: one word, DSK_RAH_SECS (9) at every fill and 8 across a listing mount's LBA 0 - `cmp ax, [dsk_rah_lim] / jbe / mov ax, [dsk_rah_lim]` where it was `mov cx, [hint] / jcxz / cmp / jbe / mov` and then `cmp ax, 9 / jbe / mov ax, 9`. min(rest, 8, 9) = min(rest, 8), so every fill asks for exactly what it did | -11 / -11 / -11 / 0 | faster on every fill: ~-21 clocks and 5 bytes of fetch on the common (uncapped) path |
| 5 | `63360142` | `dsk_rah_fill` `.take` | H4, every fill | `and word [si+RAH_N], 0` -> `mov [si+RAH_N], ch`: CH = 0 (CX = 9, the multiply's), and RAH_N is at most DSK_RAH_SECS with its high byte 0 from the claim's flush on (asserted) | -1 / -1 / -1 / -1 | ~-16 clocks (AND mem16,imm 34 -> MOV mem8,reg 18) |
| 6 | `63360142` | `disk_mount_x` (DSK_RAHINT) | H4, a mount | the disarm after the LBA 0 read is `inc byte [dsk_rah_lim]` (8 back to 9, CF kept), the re-tag walks from `dsk_rah_tab + RAH_SIG` with no displacement, the listing test one byte longer; the arm comes down an EVEN 2, so its never-executed parity `nop` goes and `dsk_next_clus_x` keeps its parity | -2 / -2 / -2 / 0 | ~-22 clocks a listing mount (the re-tag -4 x 7, the disarm +5, the test +1) |
| 7 | `13aa007f` | `cur_move_mono` (pointer-1/4) | **H2**, every packet on a 1bpp display | the new position stored FIRST, so AX and BX need no push/pop around the bank (nothing in it reads `[cur_drawn_x/y]`); `or bh, 1` -> `inc bh` (BL even); `xor ch, ch` before the row run dropped (CH is 0: the background walk's count ran out or never started) | -7 / -7 / -7 / -7 | **~-58 clocks a packet**: two pushes and two pops (-54), `inc` for `or` (-1, a byte less), the `xor` (-3) |
| 8 | `13aa007f` | `task_exit`'s parity pad (sched-1) | never executed | paired with item 7's odd 7: `sch_isr` keeps its measured parity | -1 / -1 / -1 / -1 | - |
| 9 | `13aa007f` | `menu_draw_bar`'s logo (kern_big only) | H3, a forced bar | `mov dx, MENU_LOGO_Y + 9` -> `mov dl` (DH = 0: the rule's DX, which gfx_hline keeps), and that pairs its `mov bx, MENU_SYS_TX + 8` that was a byte long for `ui_task`'s parity -> `mov bl`. kern_small keeps both as they were | -2 / 0 / -2 / -2 | same clocks, two bytes less fetch |
| 10 | `3cf2d1b0` | `sch_switch`'s scan (sched-1) | **H2**, every switch | `xor bh, bh` once, before the scan: a slot's record offset is under 256 (asserted), so every `shl` leaves BH 0, and the fallback's own `xor` goes. The fsx freeze test as `mov al, [fsx_task] / cmp al, 0xFF / ... cmp dl, al` (AX is free there) | -4 / -4 / -4 / -4 | **-3 clocks (and 2 bytes of fetch) every slot scanned**, -3 on the fallback; the fsx test -2 clocks on the common path and a register compare on the armed one |
| 11 | `1bc3aadd` | `task_spawn` (sched-1) | H4, a spawn | the answer `mov ax, cx` loaded first (CH = 0) and the raise off it: `cmp al, [sch_top] / jb / inc ax / store / dec ax` where it was `cmp cl / jb / mov al, cl / inc ax / store` and the answer after | -1 / -1 / -1 / -1 | equal (`dec ax` where `mov al, cl` was), when the bound moves |
| 12 | `1bc3aadd` | `sch_idle_body`'s parity pad (sched-4) | never executed | paired with item 11. kern_emu has neither the halt-again arm nor the pad | -1 / -1 / 0 / -1 | - |
| 13 | `9fdd8817` | `dsk_fatw_demote` (size-16), OS88_COMPACT | H4, a window shed | `mov word [dsk_fatseg], FAT_SEG` -> `mov [dsk_fatseg], bx`: BX still holds the FAT_SEG loaded for ES | -2 / 0 / -2 / -2 | faster (a register store, 2 bytes less fetch) |
| | | | | **total** | **-58 / -28 / -57 / -45** | |

kern_emu's routines come to -57 and its `.text` assembles -14, not -15:
`disk.inc`'s `align 2` after `dsk_img16` takes the odd byte back there
(BRIEF.md section 3's warning; `os88rdiff -D KERN_EMU` shows the run at -15
up to `dsk_img16` and -14 after it), so kern_emu's resident is **-56** as
measured. Every taken item is behaviour-identical: same pixels, same reads,
same refusals.

**The brief's question about size-12** (the four lz_lz4 slow paths on one
`.sync` stub): it is already in. `KHP_LZ` was built from disk-1, disk-2 AND
size-12 (khp1 LOG L.2), so on kern_big since `0445ea49` it shipped with the
knob. Item 1 is the next step on from it: the stub itself is gone, the
fall-through shared with lz_lzb's.

**`int 13h` counts**: unchanged by construction - items 4 and 6 produce the
same cap at the same read (8 at a listing mount's LBA 0, 9 everywhere else),
so every fill reads what it read. `navcalls` (the FDC's own counts: open a
root 2, enter a folder 2, Up 1 on 360KB and 720KB) and `fdswap` are in the
rows below.

**Parity**: `hotpar` green on kern_big and kern_small at every commit. Every
shift into a pinned routine is even; the four pads this pass removed are
items 6, 8, 12 (and item 9's long `mov bx`). Shifts of `.cold` after
`dsk_rah_fill` are even again on kern_big (-40) and kern_small. `lz_sync`
and `lz_arm` moved past the arms (an odd +457): they run at an arm's entry
and on its slow paths only, never in a symbol. kern_emu is
not pinned; its `sch_isr` sits at an odd shift (no halt-again arm to pair).

### 2.1 Equivalence

- **Items 1-3** (lz.inc): a scratch unicorn harness after khp1's
  `disk_lzeq.py` (whose `lzharn.asm` and `pkgstreams.py` were not parked -
  section 8) assembles base and tip `lz.inc` with `KHP_LZ` and runs both on
  40 of this build's package images packed LZ4 and LZB, filtered and not
  (160 streams), apart and in place, declared length +-1, three truncations,
  four mutations each; 24 synthesised outputs past 64KB (both formats, both
  doors, fuzzed); six long-literal streams (LZ4's 16KB pieces) fuzzed; and
  200 garbage streams, over a 384KB arena poisoned with CCh. It compares CF,
  DI, DS, ES, SP, BP, SI and every byte of the arena: **2,134 cases, 0
  different**; instructions executed base 350,757,428, tip 350,711,805
  (-0.013%). Broken on purpose (item 3's take at CX = 2) it differs on the
  one LZB stream whose literal lands on the 64KB boundary.
- **Item 7**: khp1's `pointer_moveq.py`, base against tip on each kernel's
  own bytes (CGA, Hercules and 640x480 1bpp geometry, arrow, cross and busy
  shapes, small and large deltas, the edges): **21,600 moves on kern_big
  and 21,600 on kern_small, 0 mismatches, 0 double writes**. With item 7's
  `inc bh` patched out of the tip's image it reports 4,557 mismatches.
- **Items 10-12**: khp1's `sched_uc_switch.py`, 5,000 random task tables
  (states, the idle slot, the fsx freeze and its worker, TF_SERVICE, a
  task_exit below the bound): **0 mismatches**. Item 11 by reading: AX = CX
  with CH = 0 (`mov cx, si / shr cx, 1`, SI = slot * 2), the same compare,
  the same store, the same AL.
- **Items 4-6, 9, 13** by reading, recorded at each site; asserted where the
  reading rests on a constant (`DSK_RAH_SECS` = 9 for the `inc`, a chunk
  under 256 sectors for the byte store, `MAX_TASKS * T_SIZE` <= 256).
- `tools/stkbalance.py` over `kernel/*.inc` and `kernel/kernel.asm`, knobs off
  and on: 0 unbalanced paths at the base and at the tip.

## 3. REFUSED

| item | bytes | why not |
|---|---:|---|
| lz_lz4's `.lslow` through `.litx`'s piece loop (one take-and-copy tail for both) | -9 | the piece loop calls `lz_at` after the copy. On a MALFORMED stream whose literal run passes its own tail with K = 0 that slides DS early, so the decode runs on (K = 255) and writes more garbage into the output before it refuses, where the base refuses at the next token. CF is the same; the bytes written are not, and the bar is byte-identical refusals |
| lz_lzb's three refills (`.refill`, `.gr1`, `.gr2`) as one called stub | ~-12 | a call and a return in the bit reader, one bit in eight: H1 |
| `lz_arm` shorter | 0 | every form of min(REM32, FFFFh - DI) found is 22 bytes or more without a scratch register, and the arm's contract is "DX only" |
| `cur_move_mono`'s save-buffer flip as one `add ax, cur_save + cur_savex` | -3 | NASM in `-f bin` assembles a SUM of two section-relative labels to a wrong constant with no diagnostic (section 8) - the two `add`s are the only correct spelling |
| `cur_move_mono`'s draw and `cur_put_mono`'s row loop as one body | ~-30 | they differ only in where the background comes from (VRAM vs the bank); one body needs a per-row test, H2 |
| `sch_isr`'s wake bound as `mov cx, [sch_top]` | -1 net at best | needs the byte after `sch_top` to be 0 always; there is none (`sch_wd_hits` follows, and the seed word pins `sch_idleslot` before it), and a new one is a `.bss` byte plus an init store |
| `[sch_top]` as the HIGHEST live slot rather than one past it | spawn -2, exit -1 | the wake loop and the scan each want +1 byte and a clock a tick / a switch: H2 pays for the setup |
| `task_exit`'s bound walk shorter | - | khp1 size.md's reading holds: every shape found is 25 bytes or more |
| `disk_mount_x`'s two LBA 0 arms on one `jc .jfail / call dsk_bpb_sig` tail | ~-3 | the listing-ness is spent by the disarm before the shared tail, and the re-tag must NOT run for a quiet mount (it would hand the old disk's chunks the new disk's key) - a flag to carry it costs what it saves |
| `disk_mount_x`'s arm as `dec byte [dsk_rah_lim]` | -1 | rests on the ceiling being 9 on entry; the absolute `mov byte [dsk_rah_lim], 8` stays (and the arm is already even) |
| `fm_c_up`'s `fmv_ishere / je / fmv_sync_x` as an entry into fmv_sync_x past its own `fmv_ishere` | 0 | the entry needs the KENT frame, so it is 5 bytes for the 5 it saves |
| `font_width_x`'s parity pad (text-8) | -1 | no odd byte in the routine at its speed (`mov cx, -1` and `mov cl, 3 / shl` have no shorter equals; its register contract is the API's); see CROSS-FILE |

## 4. WANTS THE OWNER

Nothing. No item changes what a user sees, a refusal, a limit or a disk read.

## 5. WANTS AN ABI BREAK

Nothing found in these routines.

## 6. CROSS-FILE

Parity pads in `hot`'s routines that a byte elsewhere would retire (the
coordinator's pairing at the close, BRIEF.md section 2):

- **`sch_resume`'s pad** (sched-1: the scan's bound is 3 bytes) pairs with
  any odd byte between `sch_switch` and `wm_paint_all` on kern_big - the
  rest of `sched.inc` (sys) and the files after it (wmui's) - and before
  kern_small's next pinned routine.
- **`sch_isr`'s `.tres` pad** pairs with an odd byte inside `sch_isr` (none
  found here on its live paths; KFZ and DRVDIAG arms are knob-only).
- **`font_width_x`'s pad** (text-8) pairs with an odd byte between
  `font_run_cell` and `mou_p2_isr` (font.inc / mouse.inc: draw's).
- **kern_small's `menu_draw_bar`**: `mov dx, MENU_LOGO_Y + 9` is kept a byte
  long there for `ui_task`'s parity; an odd kern_small byte between
  `wm_paint_all` and `ui_task` (wm.inc, menu.inc, ui.inc - wmui's) takes it
  to `mov dl` for -2.
- **`.vga`'s glue `nop`** (khp1 LOG L) still holds `gfx_fill_gray_raw_v` and
  the planar save and restore at their parity; nothing in `hot`'s `.vga`
  rows came down by an odd byte.

## 7. ROWS RUN

Once, after the last code commit (`9fdd8817`), `python3 tools/os88test.py
soak -k ... --marty-jobs 1` over the 40 rows below: **39 passed, 0 failed,
1 skipped** in 1,425.5 s. The skip is `megacurdisk` (needs 86box, not on
this box).

| covers | rows (all ok) |
|---|---|
| items 1-3, the decoders (KHP_LZ) | `lzload` 8.8 s, `lzload-lz4` 98.5, `lzdrv` 10.6, `lzfile` 14.2, `lzbig` 366.7 (Compress / Uncompress past 64KB: the segment-end slow paths), `lzmod` 9.0, `lzmod-lzb` 9.2, `lzmod-lz4big` 10.2, `lzship` 126.4 (declared 30), `lzship-lzb` 131.6, `mseglzslack` 7.1, `ldcost` 12.6, `tmload` 8.5 |
| items 4-6, 13, the read-ahead and the mount | `navcalls` 27.0 (the int 13h counts off the FDC), `fdswap` 14.3 (a swap under an open window), `dirwshed` 19.6, `dirwsize` 0.6, `fatwpin` 186.0 (declared 30), `heapcheck` 11.8, `pkgread` 10.6, `curdisk` 109.1, `imgswap` 11.3, `rdmount` 9.1, `stikswap` 48.7 |
| item 7, the pointer | `mouseup` 12.3, `curshape` 9.7, `curbusy` 9.8, `mouwheel` 4.9, `mousess` 11.1, `vmmouse` 8.6 (kern_emu) |
| items 8-12, the scheduler | `uiblock` 6.0, `schacct` 10.0, `stk0water` 17.6, `tickzero` 4.4, `cpup` 9.2, `dskwstage` 8.3, `deskcaltask` 24.1 |
| item 9, the bar's furniture | `saver` 14.4, `fsxclip` 13.3 |

At every commit: `make -j2` (the fast tier, 73 passed and 1 skipped, `hotpar`
green on kern_big) and `make -j2 small` (`hotpar` green on kern_small); the
two commits between full gates (`bfa9bd8f`, `63360142`) were each built in a
private worktree (kern_big and kern_small kernels, `hotpar` green). `make -j2
emu` and `make -j2 mega` at the tip (megaseg 3,682 within 3,682).

## 8. DEFECTS FOUND

1. **`docs/INDEX.md` was stale at the brief's commit** (`c5a7b0f9`), so the
   first `make` in the worktree failed at `os88index`; regenerated in
   `bfa9bd8f` (pass 17's lesson 2 again).
2. **NASM `-f bin` silently mis-assembles `label + label`.** `mov ax, a + b`
   with `a` and `b` both `.bss` labels assembled to `B8 04 00` where the
   true sum was 12 - no error, no warning. Found while trying `cur_save +
   cur_savex` in one `add` (section 3). Not a tree defect; a trap for every
   size pass that folds two address constants.
3. **khp1's parked `disk_lzeq.py` cannot run as parked**: its `lzharn.asm`
   and `pkgstreams.py` are not in `docs/plans/khp1/patches/tools/
   equivalence/`. This pass wrote a scratch stand-in (section 2.1); the
   README's "expect to adapt one before trusting it" is truer of this one
   than it says.
4. Not a defect, a note for the next reader: after `make build/kernel.bin`
   alone, `tests/unit/t_hotpar.py` falls back to `os88sym` and refuses the
   map; `make build/kernel.sys` (pass 2 too) leaves the map and the image in
   step.
