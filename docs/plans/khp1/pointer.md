# Kernel hot-paths pass 1: pointer

The pointer agent's record (`docs/plans/khp1/BRIEF.md` is the contract).
Owned: `kernel/mouse.inc`, `kernel/mouproto.inc`, `kernel/vmmouse.inc` - the
cursor's move, show, hide and save-under on every adapter, the serial, PS/2
and keyboard ISRs, and the producer side of the input events. Branch
`khp1-pointer`, base `6d6c23ae` (the same kernel as `a7ee7fab`).

Every cycle figure below is MartyPC guest cycles (209.6 ns each at
4.77 MHz) unless it says otherwise. Two instruments made them: os88hot
(`tools/os88hot.py`, the pass's) and a pair of scratch scripts of this
agent's that are not in the tree - one LOGS breakpoints at `mou_isr`,
`mou_isr3`, the serial ISR's `iret`, and `cur_move`'s entry and return
across a 40-packet sweep paced at one packet per 125,000 cycles (a
1200-baud mouse's own rate), and one SINGLE-STEPS a packet's three ISRs
and prices every instruction by routine and label off `kernel.map`. The
step trace is what the ranking in section 2 is made of.

## 1. BASE AND TIP

From `make`'s own `kernsize` lines. Resident = `.text` + `.bss` + `.cold` +
`.vga` + `.lowbss` + `.vgabuf`.

| kernel | `.text` | `.vga` | resident | `KERN_SIZE` / budget |
|---|---:|---:|---:|---|
| kern_big base | 40,631 | 2,032 | 91,530 | 96,768 / 100,864 |
| kern_big tip | 40,597 (-34) | 2,113 (+81) | 91,579 (+49; +47 mine, +2 is the shared `mod_cfix` fix in `.cold`) | 96,768 / 100,864 |
| kern_small base | 30,220 | 0 | 57,258 | 59,392 / 64,000 |
| kern_small tip | 30,184 (-36) | 0 | 57,222 (-36) | 59,392 / 64,000 |
| kern_emu tip | 40,853 (-31: kern_big's -34, +3 for VMware's absolute arm calling the wheel decode) | 2,113 (+81) | +50 | 97,792 / 100,864 (base 97,280) |
| kern_mega tip | 45,903 | 0 | footprint `resident 62,976` of 68,096, **unchanged** (`[+0]`) | - |

The rows above are this branch alone, before it merged `kernel-hotpaths` at
`87104a50`. **Against `87104a50`** (kern_big: `.text` 40,769, `.vga` 2,032,
`KERN_SIZE` 97,280) **the merged tip is `.text` 40,735 (-34), `.vga` 2,113
(+81), `KERN_SIZE` 97,792 (+512)** - the same deltas, so the branch's
bytes are the branch's whatever it is measured against.

**Growth before `.cold` (the coordinator's COLD_HOME limit): +47 bytes on
kern_big**, all of it the `[+81]` item's `.vga` less the free items' `.text`.
Without the `[+81]` commit the branch is -34 bytes on kern_big and -36 on kern_small.

**`KERN_SIZE` moved on kern_big and kern_emu: +512 each (96,768 ->
97,280, 97,280 -> 97,792), and only at the `[+81]` commit.** The base had
46 bytes left in the top rung and the planar save/restore's 81 bytes of
`.vga` crossed it (`kernsize`: "the top rung CROSSED"). Priced by the byte
per the brief: the item is 81 bytes, and dropping `49f6681c` takes the
branch back to 96,768 with 98 bytes left in the image rung. kern_small's
`KERN_SIZE` (59,392) and kern_mega's resident footprint did not move.
kern_emu, kern_mega and kern_small all build at the tip (`make emu`,
`make mega` with `os88megaseg: overhead 3231 within the 3283`,
`make small`).

## 2. WHERE THE TIME GOES

### 2.1 One mouse packet, ranked (base, single-stepped, CGA 5150)

A packet is three serial bytes, so three IRQ4s. The first two only stage a
byte; the third decodes, applies and moves the arrow. Per packet, base:

| part | cycles | ms | share |
|---|---:|---:|---:|
| `cur_move_mono` and its three per-row helpers (`cur_mvrow` x12, `cur_mvbg` x24) | 19,400 | 4.07 | 77% |
| `cur_geom` + `gfx_rowbase` + `cur_mvcols` | 1,280 | 0.27 | 5% |
| ISR fixed cost, three bytes (doors, stack swap, 9 push/9 pop, the port scan, EOI, `iret`) | 3,450 | 0.72 | 14% |
| `mou_byte`, `mou_apply` (the decode, the clamp, the gates, two `sch_wake_ui`, the zero-wheel add) | 2,200 | 0.46 | 9% |
| **total** (the bench's logged ISR spans: 1,363 + 1,363 + 22,378) | **25,100** | **5.27** | |

(The first three rows overlap `cur_move` and the rest; the bench's
`cur_move` span, entry to return, was 19,404 on CGA, 19,303 on Hercules
and 21,451 on VGA.) Inside the base move the per-label ranking was
`cur_move_mono.p2row` 5,687, `cur_mvrow` 2,140 (+572 `.out`), `cur_mvbg`
2,074 (+445 `.screen`), `.p2adv` 1,668, `.p1next` 1,231, `.p1row` 1,132:
pass 2's row - a call to `cur_mvrow`, two calls to `cur_mvbg` with a
push/pop and a memory flag around each, and memory compares - was 900
cycles a row, twelve rows a move.

On VGA the move was `vga_restore_vram_v` 7,600 + `vga_save_vram_v` 5,800 +
`cur_draw` 4,200 + `gfx_rect_setup` x2 1,200 + `cur_rect` 400.

### 2.2 The pointer sweep, whole machine (os88hot `pointer`, 24 packets)

MEASURED, `os88hot.py ab ... time pointer --all -n 3`, medians of three; the
window is ~632 ms either way because the serial line paces it. Twice: once
against the pass's base, and again against `kernel-hotpaths` at `87104a50`
(sched's idle-path and scan items merged), which is the base the
coordinator asked these items to be reported against.

| adapter | busy, `6d6c23ae` -> this branch before the merge | busy, `87104a50` -> tip (merged) | per packet, vs `87104a50` |
|---|---|---|---|
| CGA | 236.8 -> 202.0 ms (-14.7%) | **176.2 -> 137.7 ms (-21.9%)** | -1.60 ms |
| Hercules | 235.1 -> 202.8 ms (-13.7%) | **175.1 -> 138.2 ms (-21.1%)** | -1.54 ms |
| VGA | 245.3 -> 215.2 ms (-12.3%) | **184.4 -> 154.3 ms (-16.3%)** | -1.25 ms |

(The first column is the branch before pointer-5 and the fix; neither
touches the move.)

**The tip's per-packet CPU, split** (os88hot `profile pointer`, CGA, the
merged tip: 141.6 ms busy over 24 packets in a 649 ms window, 78% halted):

| part | ms / packet | routines (busy ms over the sweep) |
|---|---:|---|
| **cursor** | **2.28** | `cur_move_mono` 54.7 (39% of busy; `cur_geom` folds into it at this sample rate) |
| ISR, the rest of IRQ4 | 1.31 | `mou_eoi` 12.1, `mou_isr` 7.3, `mou_apply` 6.2, `mou_isr_body` 4.9, `mou_clamp` 1.0 |
| scheduling | 1.25 gross | `sch_switch` 18.1, `sch_idle_body` 4.2, `sch_account` 3.5, `task_sleep` 3.0, `task_yield` 1.1 - some of it the idle desktop's own |
| UI task | 0.78 | `ui_task` 17.6, `wm_hit` 1.1 |
| the clock, not the mouse | | `clk_tick` 2.9, `sch_isr` 1.9, `BIOS` 3.0 |

The cursor is again the largest part of a packet, now that sched-1 and
sched-4 have taken the scheduler's share from ~2.5 ms to ~1.25. At the
pass's base the same packet was cursor 4.3, ISR 1.5, scheduling ~2.5,
UI ~0.6 ms.

### 2.3 Showing and hiding the arrow

`cur_put_mono` and `cur_get_mono` run once each for every lazy hide a
paint spends (SPEC.md 7.1.4). os88hot's 31-key `type` scenario on CGA: 30
shows at 6,309 cycles and 31 hides at 2,916, **6.4% + 3.1% of typing's
busy time** (the echoed text is under the pointer). On a `raise` they are
1.3 calls each and 0.4%. On VGA the same pair is `cur_saveu` +
`cur_draw` / `cur_restoreu`, the baseline report's 13.5% of typing on VGA.

### 2.4 The keyboard ISR

`kbm_isr` over the same 31 keys: 62 calls (make and break) at a median of
1,829 cycles, the ROM's int 09h it chains to included, 3.9% of typing's
busy time. Nothing here was worth a change: the body is the chain, the
IMR bracket and two `sch_wake_ui`-class stores (section 4).

## 3. TAKEN

| id | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---:|---:|---|---|---|---|---|---|
| pointer-1 | `cur_move_mono` (+ `cur_mvcols`/`cur_mvrow`/`cur_mvbg` gone) | H2 (per packet) | the overlap decided once as a column delta and ONE row run; the new background banked off the screen, the run patched from the old save inside pass 1, the draw composed from the bank - no per-byte test, no helper calls | -26 | -26 | `cur_move` 19,404 -> 11,973 CGA, 19,303 -> 11,694 Herc (40-packet sweep) | serial ISR's CPU share while sweeping: CGA 20.0% -> 14.1%, Herc 19.9% -> 13.9%; a packet's third byte 4.69 -> 3.13 ms (the latency from the byte to the arrow on the glass) | M | same instructions, all register or word ops; no worse on a 16-bit bus (even shift, `hotpar` holds) | `2b493c0f` | FREE |
| pointer-2 | `cur_saveu`/`cur_restoreu` (VGA, EGA), `cur_rect` gone, `cur_vspan` new | H2 (per packet on a planar display, and every planar show/hide) | the cell's own four-plane walks instead of `vga_save_vram`/`vga_restore_vram` + `gfx_rect_setup` + the one-plane arm banking | **+81** (`.vga`) | 0 | `cur_move` 21,451 -> 15,103 VGA (save 5,800 -> 4,650, restore 7,600 -> 4,600) | VGA: serial ISR share while sweeping 21.5% -> 16.4%; packet byte 5.12 -> 3.79 ms. Also every planar lazy hide/show (typing under the pointer on VGA, a raise) | M | `movsb` loops; the 286 gains the rect setup outright | `49f6681c` | **[+81] OWNER'S PICK** |
| pointer-3 | `mou_apply` (`mou_whl_cl`, `mou_apply_w` new) | H2 (per packet) | the driver/VMware wheel decode (two `sar`, a zero add and a SECOND `sch_wake_ui`) moved out of the path every serial and PS/2 packet runs | 0 (+3 kern_emu) | 0 | ~210 cycles off the packet's third byte | 0.04 ms a packet, ~0.2% of the CPU while sweeping | M | same | `226fb2f8`, and its defect's fix `7e8e3220` (section 9) | FREE |
| pointer-4 | `cur_move_mono` head and background walk | H2 | the save-buffer flip as `(a + b) - live` (no compare, no branch, -4 B); the background walk holds the row step's wrap bit and fix in DX/SI (+4 B) | 0 | 0 | `cur_move` 11,973 -> 11,536 CGA, 11,694 -> 11,413 Herc | packet byte 3.13 -> 3.00 ms CGA | M | same | `226fb2f8` | FREE |
| pointer-5 | `cur_put_mono`, `cur_get_mono` | H2 (per lazy hide / show) | a word a row: one read of both bytes, one word banked, masks rotated into the word's byte order; `lodsw` and the row step in registers for the hide | -8 | -8 | put 6,309 -> 5,234, get 2,916 -> 2,215 (31 typed keys, CGA) | typing 31 keys under the pointer: 11.3 ms less busy, ~0.36 ms a key (CGA). os88hot `ab` of `type` vs `87104a50`, the whole branch: busy CGA 516.9 -> 505.9 ms (-2.1%), Hercules 458.0 -> 447.5 (-2.3%), VGA 525.6 -> 486.3 (-7.5%, pointer-2's planar show/hide) | M | same | `8780a506` | FREE |

All five keep the pixels and every result identical: proved under unicorn
against the base kernel's own bytes - **36,000 random moves on kern_big and
27,000 on kern_small** (CGA two-bank, Hercules four-bank and linear 1bpp
geometry, all three shapes with their hot spots, the right and bottom
edges, deltas small and large, hide/show cycles mixed in) with the whole
framebuffer, the live save buffer's valid bytes and the banked geometry
compared after every step, every framebuffer write counted (never twice
in a move) and the background compared whole after the final hide; and
**1,500 planar cells** (640x480 and 640x350) under a four-plane VGA model
(Read Map Select, Map Mask) comparing the buffer, the planes, the GC and
Sequencer state left behind and the write set. Each harness was shown to
go red on a deliberate break (a patch byte dropped, the parity test
inverted, a kind bit wrong, the aligned-span test removed, a show's second
byte dropped).

## 4. TRIED AND REFUSED

- **A row-address table beside each save buffer** (the DI of every row,
  stored once by the walk that computes it, so pass 1 and the draw do not
  re-run `gfx_nextrow`): about 1,400 cycles off a 1bpp move (12%). Free on
  kern_big (the mono saves use 48 of `cur_save`'s 96 bytes) but +48 bytes
  of `.bss` on kern_small, the machine most likely to be a CGA 5150, and it
  needs `cur_put_mono` to write the table too. Not built; an owner's pick
  if the move is wanted faster still.
- **Saving only AX, BX, DX and DS for the two staging bytes** and the rest
  around the decode: ~130 cycles a staging byte, ~260 a packet, 0.2% of the
  CPU while sweeping, for ~10 bytes and a second frame shape at `mou_eoi`
  (which `mou_p2_isr` joins with the full frame). Refused.
- **Skipping the loser port's LSR read once the contest is over**: ~76
  cycles a byte net (the loser row's scan is ~196), ~0.3% of the CPU, +11
  bytes - and it may only be done after `mou_lockon` has set the loser's IER
  to 0 (`[mou_hpst]` = 2), because between the settle and the lockon a
  cross-wired loser left unread holds a shared line high and the mouse is
  dead for the session (SPEC.md 9.5.2's own failure). Refused at that
  price.
- **Skipping `mou_byte`'s drain test once settled**: hiber.inc's resume arms
  `[mou_drain]` with `[mou_seen]` already 1, so the test is load-bearing.
- **The draw's `cur_b1ok` test hoisted** by writing byte 1 back unchanged
  at the right edge: invisible, but it breaks SPEC.md 7.1.2's "every byte
  written once" and a byte the background walk read before pass 1 restored
  it could be stale after a jump. Kept the test (~30 cycles a row).
- **`cur_draw` (planar)**: 4,200 cycles a move with an OUT and a latch read
  per byte, which the Bit Mask needs; the remaining fat (the `cur_b1ok`
  compare) is ~5%. Left alone.

## 5. WANTS AN ABI BREAK

None found. Checked: `OSAPI_MOUSE`, `OSAPI_MOUSE_FEED` (its register
contract already clobbers what `mou_apply` spends), the event record
(`mou_evfill`), VIDEXT's cursor hooks (`VX_CURPUT`/`VX_CURGET`/`VX_CURMOVE`,
whose packed displays are the driver's own cost) and VMMOUSE.DRV's private
ABI. Nothing in the pointer path is slowed by a frozen interface.

## 6. WANTS THE OWNER

- **The UI wake on pure motion** (SPEC.md 13.13, 8.1.2.2). Every packet
  wakes the UI task (`sch_wake_ui` from `mou_apply`) so that the shape pass
  (SPEC.md 7.2.1.1) and hover can follow the pointer. On the merged tip that
  wake and the pass it buys are ~0.8 ms of `ui_task` plus most of the
  ~1.25 ms of scheduling a packet on CGA, **~1.5-2 ms a packet, 6-8% of the
  CPU while the pointer moves**. ui_task's tick sweep already runs every
  55 ms, so waking only on a button edge, a wheel notch, or at most once a
  tick for motion would keep the arrow itself exactly as live (the ISR
  draws it) and cost the shape and hover up to 55 ms of lag. It changes
  behaviour (latency), so it is not taken.

## 7. CROSS-FILE

- **sched (`kernel/sched.inc`)**: `sch_switch` is 32% of a CGA pointer
  sweep's busy time at the tip (66.6 ms of 208 for 24 packets) - the
  largest routine there. A packet's three interrupts are three idle-task
  yields plus the UI wake; the baseline report's ranking item 1 already
  names the task-table scan. Anything that shortens a switch is worth
  ~1 ms a packet here.
- **vga (`kernel/vga12.inc`)**: after pointer-2 the cursor no longer
  reaches `vga_save_vram`/`vga_restore_vram`, so three comments there are
  stale: the one at the one-plane arm ("cur_saveu and cur_restoreu reach
  vga_save_vram and vga_restore_vram DIRECTLY out of IRQ4"), the `%error`
  text beside `vga_pn` ("cur_saveu/cur_restoreu bank it"), and the note at
  `gfx_1bpp_on` that `cur_rect` stores `VGA_PN_FOUR` inline. The arms
  themselves are unaffected. Not edited here (the file is vga's).
- **vga / gfxcall (`GFX_U_FLOOR` in `kernel/vga12.inc`)**: kern_small's
  floor of 20 names `cur_move_mono`'s working set, which is 12 bytes now;
  in the size inbox.

## 8. ROWS RUN

One scoped pass after the last code commit but the fix, `--marty-jobs 1`
(`os88test.py soak -k ...`, 18 rows, 762 s): **17 passed, 1 failed** -
`megapmmouse`, which was this branch's own defect (section 9). After the
fix (`7e8e3220`) the failing row and `vmmouse` (the other `mou_apply_w`
caller) were re-run: **both green**.

| row | result | | row | result |
|---|---|---|---|---|
| `mouseup` | ok 19.0 s | | `ps2mouse` | ok 11.4 s |
| `curshape` | ok 8.5 s | | `wheelps2` | ok 8.4 s |
| `curbusy` | ok 10.6 s | | `vmmouse` | ok 12.4 s, again 13.0 s |
| `curdisk` | ok 173.9 s | | `megacurdisk` | ok 40.5 s |
| `mouresume` | ok 27.2 s | | `megapmmouse` | FAIL 42.7 s, then **ok 40.0 s** |
| `mouerrq` | ok 17.4 s | | `dispcheck` | ok 49.2 s |
| `mouwheel` | ok 8.6 s | | `dispcorner` | ok 84.3 s |
| `wheel` | ok 22.3 s | | `dispseam` | ok 221.3 s |
| `mousess` | ok 21.2 s | | `dispherc1` | ok 24.2 s |

The gates at the tip: `make -j2` (the fast tier, 73 passed, `hotpar`
green), `make -j2 small`, `make emu`, `make mega` all clean;
`tools/stkbalance.py` reports the same 4 unbalanced paths on `mouse.inc`
(5 with `vmmouse.inc`) at the base and the tip. The `kernel-hotpaths`
merge at `87104a50` came after the rows and was not re-run here (no code
of this branch moved in it).

Rows that cover these items: `mouseup`, `curshape`, `curbusy`, `curdisk`,
`mouresume`, `mouerrq`, `mouwheel`, `wheel`, `mousess` (the serial path,
the shapes, the busy cursor, SPEC.md 7.4's disk freeze), `wheelps2`,
`ps2mouse` (the PS/2 path and its wheel), `vmmouse` (kern_emu, the
VMware pointer through `mou_apply_w`), `megacurdisk`, `megapmmouse`
(kern_mega; the PicoMEM mouse reaches `OSAPI_MOUSE_FEED`), and
`dispcheck`, `dispcorner`, `dispseam`, `dispherc1` (the two-display
`cur_move_multi` and the seam).

## 9. DEFECTS FOUND

- **The pointer could consume an armed sub-rect restore** (latent, base).
  `vga_restore_vram` applies `[gfx_sub_*]` (SPEC.md 5.8) and clears it on
  the way out, and the cursor's restore called it from IRQ4. An arm left
  up when the ISR drew would have put the cursor's 96-byte buffer back at
  a sub-rect's offsets and then cancelled the caller's arm. The lock rules
  make the window narrow (the ISR draws only with the lock free or held by
  the task inside int 13h), and nothing was seen to hit it. pointer-2
  removes it: the cursor's walks read neither the sub-rect words nor
  `[vga_pn]`.
- **pointer-3 itself shipped a defect, and `megapmmouse` caught it.**
  `mou_apply_w` called the wheel decode with the report's dx still in AX,
  and the decode spends AX: every relative report from a DRIVER (the
  PicoMEM's, the CH375's) and from VMware's relative arm lost its x. The
  serial and PS/2 paths, which every MartyPC row drives, never enter
  `mou_apply_w`, so only the kern_mega PicoMEM row could see it ("the
  pointer went (160,139) -> (160,159), wanted (200,159)"). Fixed in its own
  commit (`7e8e3220`): AX banked around the call (+2), paid for by letting the
  move's d = +1 arm fall into d = 0's OR (-2), so pointer-3 is still 0 bytes
  on kern_big and kern_small is -2.
- No defect was found in the 1bpp move: the equivalence harness agreed
  with the base on every one of 63,000 steps.
