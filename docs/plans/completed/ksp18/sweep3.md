# Kernel size pass 18: sweep3

The `sweep3` agent's record: a second-wave fresh reader over the `sys` row's
files as they stood after `sys`, `main`, `dir` and `wmui` merged
(`kernel/disk.inc`, `diskw.inc`, `drvvol.inc`, `loader.inc`, `lz.inc`,
`lzfilt.inc`, `compress.inc`, `memory.inc`, `mod.inc`, `driver.inc`,
`xmem.inc`, `hiber.inc`'s resident half, `clock.inc`, `clockw.inc`,
`serial.inc`, `cpudet.inc`, `sched.inc`, and placement: `boot/boot2.asm`,
`.cfx`, `.ovl`), routines `fresh-routines.md` gives `hot`, `dir` or `main`
excepted. Branch `ksp18-sweep3`, cut from `4ff13976`; mid-way the coordinator
asked for kern_big's boot blob to be freed for `draw`'s `vid_tab` move, and
`kernel-size-18` was merged in at `28d2a265` (a fast-forward: no edits of mine
had landed yet) before that work started. Every figure is ASSEMBLED
(`tools/kernsize.py --json` per kernel, the maps, `tools/os88rdiff.py`).

## BASE AND TIP

Resident is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: plus `.mega`, 23,056 throughout).

| kernel | cut (`4ff13976`) | merged base (`28d2a265`) | tip | **this branch's work** | where |
|---|---:|---:|---:|---:|---|
| **kern_big** | 91,522 | 91,420 | **91,338** | **-82** | `.text` 40,498 -> 40,416 (-72 `vid_tab`, -10 sched/clock) |
| kern_small | 57,187 | 57,091 | **57,081** | **-10** | `.text` 30,100 -> 30,090 |
| kern_emu | 91,766 | 91,594 | **91,584** | **-10** | `.text` 40,672 -> 40,662 |
| kern_mega | 127,017 | 126,926 | **126,916** | **-10** | `.text` 46,172 -> 46,162; conventional 62,976 unchanged (5,120 spare of 68,096) |

`KERN_SIZE` is unchanged on every kernel from the merged base (kern_big
96,768, kern_small 59,392, kern_emu 96,768). No `.bss`, `.lowbss`, `.vgabuf`,
`.vga`, `.cold`, `.ovl` or `.cfx` byte moved. `.text`'s total delta is even on
every kernel, so `.bss` keeps its alignment; `disk.inc`'s `align 2` after
`dsk_img16` absorbed one byte mid-way (see TAKEN 6) and the totals above are
the sections' own lengths.

**The boot blob** (`boot2_end`, pass 2's map - `kernsize`'s `boot2` figure
does not count the whole loader, as `draw.md` found):

| kernel | `BOOT2_SECS` | base `boot2_end` | tip | |
|---|---:|---:|---:|---|
| **kern_big** | 13, unchanged | 0x1A00 = 6,656 (0 spare) | **0x19FC = 6,652** (4 spare) | 0x19A9 = 6,569 before the rows went in: **87 freed**, 83 spent on `vid_tab` |
| kern_small | 11, unchanged | 0x1381 | 0x1342 | -63 |
| kern_emu | 14, unchanged | 0x1AC8 | 0x1A71 | -87 |
| kern_mega | unchanged | 0x1B3D | 0x1AE3 | -90 |

`t_blobruns` (2 calls a blob, 7 for the kernel, every geometry) and
`t_canary` (memory offset 4096, file sector 21, all 6 geometries) are green at
the tip; `KSIG_OFF` did not move. CTRL.DRV shrinks with `CFG_SHARED`'s `_pack`
and the shutdown spinner (`spinner.inc`'s `sd_` copy); not resident.

## TAKEN

Bytes are kern_big / kern_small / kern_emu / kern_mega.

### The blob: 87 bytes for `vid_tab` (the coordinator's item)

None of these is resident; the point of them is item 5.

1. **`boot/boot2.asm`'s `read_run` does the run's bookkeeping** (blob -21).
   Both loops (`.head_next`, `.load_next`) followed the call with the same 22
   bytes: `mov es, [b2_dest]` before it, then advance `[b2_lba]`, take the
   run off `[b2_left]`, `pushf`, advance `[b2_dest]` by 0x20 a sector,
   `popf`. `read_run` now loads ES and does it on the way out, returning ZF
   from the `sub`; `.load_next` re-banks the flags past the splash tick as
   before. **No int 13h call changed**: the read, its three bounds, the
   retries and the `.rerun` fallback are untouched (`t_blobruns` counts the
   same 2 + 7 calls on six geometries).
2. **`b2_drive` and `b2_heads` stored as one word** (blob -4, `%if`-guarded
   adjacency); **`kz_expand`'s two `xor ah, ah`** inside the extension loops
   went (blob -4): AH is the `cbw`'s 0 and `lodsb` writes AL alone - fewer
   instructions per extension byte of the kernel's own decode; **`kz_all`'s
   `dec cx / jnz` is `loop`** (blob -1).
3. **`kernel/spinner.inc`** (the boot spinner, a macro the splash and the
   shutdown both expand; no row owns it): the three bar rows are one `_mkput`
   entry that builds `_rowb` and falls into `_putrow`, and `_putplain` loads
   its own 19 (blob -14); and **from `_prow` down the composer runs with DS =
   CS** (`_spin` sets it before the first call and nothing else calls them),
   so its 19 `cs:` overrides went (blob -19). `tests/cswrites.txt` is lowered
   13 -> 5 with the reason: eight stores now go through that DS = CS rather
   than through CS, the same stores to the same segment, not a conversion.
4. **`driver.inc`'s `CFG_SHARED` / `CFG_BOOT`** (`ovc_*` in the blob, `cpc_pack`
   in CTRL.DRV): `_pack` and `_unpack` read their map rows `cs lodsb / cbw /
   xchg bx, ax / cs lodsw / xchg di, ax` (-4 each; an `%error` holds every
   `CFG_*` offset under 128 for the `cbw`); `_deser` compares the signature
   with one `repe cmpsb`, DS borrowing the image's ES that `_load` (its one
   caller) set (-4); `_key`'s hit, `_deser`'s end and `_load`'s end answer CF
   where it already stands - a `je` after an equal compare is CF = 0, the
   `ja`s are CF = 0, the `jc`s CF = 1 (-3 each); `cbw` for `xor ah, ah`
   (-1). Boot only (SYSTEM.CFG's read).
5. **`vid_tab` off kern_big's resident** (-72 / 0 / 0 / 0). `draw`'s arm
   (`kernel/viddet.inc`, `VID_TAB_ROWS` emitted by each `VID_APPLY`
   expansion) was already running on kern_small and kern_emu; kern_big is
   dropped from `VID_TAB_RES` and its blob, CTRL.DRV and EXTD.DRV each carry
   the four rows, as kern_emu's do. kern_mega stays `VID_TAB_RES` (its switch
   rewrites the segment column). Thirteen sectors, 4 bytes spare.
   `tests/megaseg.txt` 3,684 -> 3,754: kern_mega keeps the rows kern_big lost,
   so the gap grew with nothing on kern_mega growing (`make mega` failed on
   the old figure).

### Resident

6. **`sch_account`** (IRQ0's charge, H2)
   (-7 / -7 / -7 / -7). The slot index through `mov al, [sch_cur]` (the
   3-byte accumulator form) / `cbw` / `shl ax` x2 / `xchg si, ax` where BX
   took 12 bytes (-3; `[sch_cur]` < 128); and the stamp stored and the old
   one read in one `xchg` a word: `mov ax, cx / xchg cx, [last] / sub ax, cx
   / mov bx, dx / xchg dx, [last+2] / sbb bx, dx` for six moves (-4). **Every
   path faster** in 8088 terms (`max(clocks, 4 x bytes)`): the index ~4 clocks
   and 3 bytes of fetch; the two stamp words 4 clocks each (`xchg r,m` 25+EA
   plus `sub r,r` 3, against `sub r,m` and `mov m,r` at 13+EA each, EA 6) and
   4 bytes of fetch - ~12 clocks and 28 of fetch floor off every tick's charge
   and every `task_yield` / `task_cycles` / `inst_charge`. CX:DX now come
   back holding the PREVIOUS stamp and BX untouched on the fsx early-out -
   documented clobbers; every caller read (`sch_isr`'s `.full` through
   `snd_tick` and the wake scan, `sch_switch`'s `.pick` which banks DX/BX,
   `task_exit`, `task_cycles`, `inst_charge`) and none reads them.
7. **sys's PARITY `nop` after `sched_unhook` retired** (-1 / -1 / -1 / -1):
   item 6's odd 7 is the "next odd byte between `cur_move_1` and `sch_isr`"
   sys.md named; `[cur_move_1, sch_isr)` is -8.
8. **`clk_tick`'s `xchg cx, ax`** for `mov cx, ax` (-1): AX is zeroed next.
9. **`clk_fmt` loads its SI output at the end, so `clk_put_mon` drops its SI
   bank** (-1 net: `mov bx, clk_str` + `mov si, clk_str` against `mov si /
   mov bx, si`, and `push si / pop si` gone). `clk_put_mon`'s other caller,
   CTRL.DRV's `clk_fld_str`, reloads SI at `.done`. Items 8 and 9 make
   `[sch_switch, wm_paint_all)` -2.

kern_big resident: 72 + 7 + 1 + 1 + 1 = **82**; the rest 10.

**Parity.** `hotpar` green on kern_big and kern_small at every commit:
`[cur_move_1, sch_isr)` -8, `[sch_switch, wm_paint_all)` -2, nothing between
`gfx_blit1_x` and the end of `.cold` moved. One pad REMOVED (item 7), none
added. Mid-way, before item 7, item 6's odd 7 had put `sch_isr`, `sch_switch`,
`wm_paint_all` and `ui_task` odd and `dsk_img16`'s `align 2` had swallowed a
byte - the gate caught it, and the tip is even everywhere.

**Equivalence.** A unicorn harness ran the base and tip `sch_account` bodies
(assembled standalone against the same addresses) over 5,000 random states -
running slot, fsx flag, both latch bytes, the IRR (bit 0 set, clear, random),
both stamp words, all fourteen slots' counters, incoming SI: **memory
(stamps and every counter) and SI identical in all 5,000**; an `adc` put in
for the `sbb` on purpose differs in 3,776. The blob items are proved by the
boot rows below (`read_run` and `kz_expand` load and expand the whole kernel
on every boot; `cfgtrip` reads SYSTEM.CFG through `ovc_deser`); the rest by
reading, each caller named. `tools/stkbalance.py` over every touched file
reports the base's count at the tip (boot2/spinner/driver/splash: 2 and 2;
sched/clock: 1 and 1).

**Files in no row taken here**: `kernel/spinner.inc` (and the `tests/` and
`viddet.inc` lines items 3 and 5 needed; `viddet.inc` is draw's, the one-line
gate change draw's note names as the way to take it).

## REFUSED

1. **A shared push/pop ladder for the blob** (`spl_tick`, `spl_step`,
   `spl_finish`, `spl_reset`, `xm_boot_x` each bank eight or nine
   registers): the `.text` ladder is not aboard while the splash runs, and a
   blob-local one (entry 19 bytes, exit 9) pays ~0 because three of the five
   need a `pushf`/`popf` or `retf` tail of their own and so a call wrapper.
2. **Blob variables as nobits past `boot2_end`**: the heap sits directly
   above the blob during the boot (mem_init raises `[mem_base]` over it), so a
   zero-initialised tail would be claimed under the splash.
3. **`mod_need`'s `jmp short .check` by moving the answers below the check**:
   the routine's own comment says why not - every early refusal's `.fail`
   would go past a short jump.
4. **`xm_release_rec`'s second `KES` and `pop ax / push ax`** (-4): the EMS
   and ETHER drivers' GONE verbs promise to keep ES only by convention, and
   "the driver eats AX".
5. **`clk_inc_sec`'s `mov byte [bx], 0` as `sub [bx], al`** (-1): equal only
   while every field is in range; a field the RTC or the Control Panel left
   out of range would wrap differently.
6. **`task_cycles` without its BX/SI/CX banks**: `ui_bill` and `wm.inc`'s
   paint read BX/SI (the window) after it.
7. Held, not built: **`mod_free_row`'s `call mem_free_x / ret` as `jmp
   mem_free_x`** (-1; the `je .out` retargets to `mod_disarm.slot`'s `ret`
   just above): odd, between `gfx_blit1_x` and `dsk_xfer` - any other odd
   byte in memory.inc's/mod.inc's/lz.inc's `.cold` or the head of disk.inc
   takes it.
8. sys.md's REFUSED list, pass 16 and 17's (`mod_needi`, `sch_account`
   through `sch_currec`, `clk_put4` by `aam`, ...) still hold and were not
   re-proposed; `dsk_fatw_pick`'s -1 and `mem_region_reloc`'s -1 are still
   held for parity - no partner found in `[dsk_runcap, dsk_next_clus_x)` or
   `[wm_paint_all, ui_task)` in this row's files.

## WANTS THE OWNER

None. No behaviour changed; every refusal path answers as it did. (The
`vid_tab` move draw's note put to the owner as "a fourteenth sector" is taken
WITHOUT one, which was the alternative that note named.)

## WANTS AN ABI BREAK

None found.

## CROSS-FILE

1. **Parity partners for the coordinator**: `hot.md`'s `sch_resume` pad pairs
   with any odd byte in `[sch_switch, wm_paint_all)` - this branch's clock
   items there are even together, so it still stands; REFUSED 7's
   `mod_free_row` -1; sys's held `dsk_fatw_pick` and `mem_region_reloc` -1s.
2. **`tests/megaseg.txt`** moved with item 5; a merge with another branch that
   touches it should keep the larger figure's reasoning.
3. **Blob room left on kern_big: 4 bytes** at thirteen sectors. Whatever goes
   into `.boot2` on kern_big next needs a byte freed first; the cheapest
   remaining reads are in `splash.inc` (no row) and `bvs_*`/`font_init`/
   `vx_boot_x` (draw's).

## ROWS RUN

Once, after the last code commit (`8fb826e2`), with the tree not edited
while they ran: `python3 tools/os88test.py soak --marty-jobs 1` scoped to 22
rows: **22 passed, 0 failed, 0 skipped** in 443 s (MartyPC rows on the
GLaBIOS twins; `vesa*` on QEMU, its emulator).

| rows | what of this branch they reach |
|---|---|
| `bootsmoke`, `kzboot`, `cylrun`, `bootstatus`, `smallboot`, `hdboot` | the blob's loader: `read_run`'s bookkeeping, `kz_expand`, `kz_all`, the head-cross reload (items 1, 2), both kernels, the hard-disk `kz_hd` path |
| `splashbar`, `splashspin` | the spinner (item 3) and the bar ticks between runs (item 1) |
| `shutdown` | the shutdown's spinner copy (item 3) and `sched_stop` |
| `cfgtrip`, `dispreboot` | SYSTEM.CFG read back through `ovc_*` (item 4) |
| `dispmode`, `dispherc1`, `vesa`, `vesacp`, `extdmod`, `vidext` | `vid_apply`'s rows now in the blob, CTRL.DRV and EXTD.DRV on kern_big, the VESA row (item 5) |
| `tickzero`, `tmload`, `tmsmall` | `sch_account` (item 6), the Task Manager's cycle figures |
| `dtfield`, `toastbar` | `clk_fmt`, `clk_put_mon`, `clk_tick` (items 8, 9) |

`make`, `make small`, `make emu` and `make mega` build at the tip; the fast
tier is 73 passed, 1 skipped (`w16cens`, fixtures); `hotpar`, `t_blobruns`,
`t_canary` and `megaseg` green.

## DEFECTS FOUND

1. **`make mega` fails on `tests/megaseg.txt` the moment kern_big's `.text`
   shrinks by more than the allowance's slack** - by construction, since the
   figure is a gap against kern_big; fixed for this move (item 5). Not a
   kernel defect, but worth a line in the next brief: a size pass that takes
   resident bytes off kern_big alone will trip it.
2. The container restarted mid-pass (coordinator's notice); the two commits
   then on the branch survived and the tree was rebuilt clean before any
   figure here was taken.
