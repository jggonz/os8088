# Kernel size pass 16: heap

The heap agent's record: `kernel/memory.inc`, `kernel/mod.inc`, the
heap-claim lines of `kernel/driver.inc` (`drv_release`) and `dsk_fatw_*` in
`kernel/disk.inc`. Branch `ksp16-heap`, cut from `714e9d64` (the brief).
Every figure below is ASSEMBLED, from `tools/os88rdiff.py kernel 714e9d64 .`
(with `-D KERN_SMALL`, `-D KERN_EMU`, `-D KERN_MEGA`) and `tools/kernsize.py
--json`; no estimate is quoted as a saving.

## BASE AND TIP

Resident is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: `.mega` beside it, unchanged).

| kernel | base | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 92,136 | **92,032** | **-104** | `.cold` 37,703 -> 37,599; every other section the same |
| kern_small | `.cold` 21,464 | 21,406 | **-58** | `.cold` only |
| kern_emu | 92,392 | 92,288 | **-104** | `.cold` only |
| kern_mega | `.cold` 45,997 | 45,888 | **-109** | `.cold` only; `.mega` 21,648 unchanged |

kern_mega's `os88megaseg` figure is **3,309 at the base and at the tip** -
nothing here touches the segment (`make mega` still fails that gate exactly
as at the base; the ops agent has it). `KERN_SIZE` crossed no rung on any
kernel - and per the banner that is not the point. No `.boot2`, `.ovl`,
`.cfx` or module byte moved; `BOOT2_SECS` is unchanged.

## TAKEN

Bytes are per kernel: big = kern_big and kern_emu (identical), small, mega.
Nothing below is on the hot-path list (the API cell, IRQ0, `sch_switch`,
`dsk_xfer`'s per-sector path, the mouse ISRs, `font_run`, the blits): every
item is per claim, per compaction step or per unload.

1. **The claim doors' far entries are their bodies** (big -13, small -13,
   mega -9). `mmf_osapi_mem_claim` was `push cx / xor cx, cx / call
   osapi_mem_claim_dma_x / pop cx / retf`, `_hi` the same, and the two DMA
   cells were `call <body> / retf` over two near bodies. Now
   `mmf_osapi_mem_claim_dma` and `mmf_osapi_mem_claim_dma_hi` ARE the bodies
   (they bank CX themselves), the plain claim steps over the DMA door's
   `push cx` with `SKIPF1`, `_hi` jumps past its `push cx`, and
   `OSAPI_MEM_HUGE` pushes its own `CX = 1000h` as the CX the door's frame
   pops, so the door's `retf` is the cell's. kern_mega's two near callers
   (`mmf_osapi_mem_huge`'s bridge arm and `osapi_mem_regrow_x`'s move-down)
   far-call the entry through `COLD_SEG:` - `push cs` is refused in `.cold`
   by `os88ovlchk` - +2 each there. `tools/dsegaudit.py`'s CLAIMERS set names
   the two new entries in place of the deleted near bodies.
2. **`mem_avail_lvl_p`'s private `.run` is `mem_cp_gap` / `mem_cp_tail`**
   (big -16, small n/a, mega -16). The walk is the ascending pass
   (`[mem_cp_msk]` = 0, which its own comment states and `.nested` enforces),
   and going up `mem_cp_gap` is `SEG + (PARA AND 0) - fill` and
   `mem_cp_tail` is `[mem_top] - fill` - exactly `.run`'s two calls. The
   barrier sets BX = the base itself (`.run`'s `pop bx`). +1 call/ret per
   barrier in an avail walk (~40 clocks).
3. **`mem_claim_1`'s `.down` is `.hi`'s tail** (-10 all). Both were "DX = an
   edge less AX, refused under the floor, then `.retry`": `.down` loads its
   edge and jumps to `.below`. `.fail0` still pays: `.fail` is 202 and 140
   bytes from its first refusals, out of short reach.
4. **Two dead stores in `mem_regrowp`** (-12 all). `mov word [mem_skip], 0`
   after `mem_runfree` and `mov word [mem_dma], 0` after `mem_hifit`: every
   reader of each sets it first (`mem_hifit` zeroes `[mem_skip]`, `mem_claim_1`
   stores `[mem_dma]` from SI, and `mem_hifit` is reached from `.move` alone).
   The `.bss` comments now say so.
5. **`mem_reloc_call` far-calls the kernel's half direct** (big -8, mega
   more). It staged `cw_mem_disp` and the segment in `[ss:mem_fptr]` and
   called through them; it is `call KERNEL_SEG:cw_mem_disp` now, and the
   `.kernel` arm sets the offset the owner's call needs (CX, stored in the one
   `.go` tail with the segment). The distance dependency the old comment
   warned about is gone with it.
6. **`drv_release`'s service-table clear on `dsk_ent_zero.zero`** (big -8,
   mega -8). The 16-byte `push ax/cx/es, ES = DS, rep stosb` block is `push
   cx / mov cx, DSV_SIZE / 2 / call dsk_ent_zero.zero / pop cx` - disk.inc's
   word loop through DS:DI that touches DI and CX alone. Unload-time only
   (18 loop passes instead of a 36-byte `rep stosb`). CROSS-FILE 1.
7. **`dsk_fatw_claim`** (-8 all). The claim's word in SI (`lea`, kentc_di
   banks SI and `mem_claimp_x` keeps it) instead of a push/pop of BX, and the
   refusal's 0 as `jnc / cwd` into one store - AX is the paragraph count,
   positive, asserted - instead of a second arm storing an immediate 0.
   Returns CF = 0 on a refusal now where it returned CF = 1; its contract is
   "out: nothing" and both callers read the word, not CF.
8. **`mem_compact`'s release stores unconditional** (big -6). `[mem_parked]`
   and `[inst_parkreq]` are cleared without the test: `inst_park_req`,
   reached from `.nowt` alone, is the request's only writer and raises
   `[mem_parked]` beside it, so a pass that never asked finds both 0.
9. **`mem_cp_sub` without a branch** (big -4) - the two directions are one
   subtraction in the complemented space `mem_cp_next` already walks: ~a - ~b
   is b - a, borrow and zero alike. ~+30 clocks a call, per claim in a
   compaction walk. And **`mem_cp_fill0`'s `inc ax / jnz`** for `or ax, ax /
   jz` (big -1, the parity balance - see below).
10. **`mem_owner_of_x` is `mem_find_own` with DX = BX** (-4 all): that walk
    with DX = BX matches the first live record based there whatever owns it,
    MEM_VAL included on kern_mega.
11. **`mem_sum_kb`'s purgeable range off CL** (-4 all): one biased unsigned
    compare, and CL holds the byte for the 0xFF test (`.done` sets CL again).
12. **`mem_rank_top`** (-4 all, mega -6): a label at `mem_rank_bh`'s `mov al,
    MEM_LVL_TOP`, for the three callers that loaded BH = MEM_LVL_TOP to get
    it (`mmf_osapi_mem_avail`, `osapi_mem_p`'s avail, kern_mega's huge ask).
13. **`mem_shed_one` leaves through `kretc_di`** (-3 all): its three pops
    before `jmp kretc_cx` were kretc_di's own three.
14. **`mem_in_xfer` loads the base once** (big -3, mega -1).

**Hot-path parity**: `hotpar` is green on both gated kernels with NO pad
added. Items 1-14 left the run in front of `dsk_xfer` and `dsk_runcap` an
even shift by choosing item 9's `inc` (-1) and item 7's `cwd` form (-8 rather
than the -5 or -7 forms) - the alternatives each flipped `dsk_xfer`/
`dsk_runcap` on kern_big or `dsk_next_clus_x` on kern_small. **No new parity
pad.**

## REFUSED

1. **`mem_floor_ax` out of resident** (46 bytes): `mem_init_x` computes the
   same floor, so storing it there for `mem_unblob_x` looks free - but `.cold`
   can move into the HMA during `drv_boot`, between the two, and
   `mem_floor_ax` reads `[api_coldseg]`: the floor at `mem_unblob` can be a
   rung lower than at `mem_init`. It has to be asked twice.
2. **One KB->paragraph helper for `mem_claim_x.go` and `mem_regrowp`** (+3
   net: 13 bytes of helper against 10 at the two sites).
3. **`mem_claim_1`'s publish tail through a skip**: its refusal zeroes DX,
   which a caller can see; no skip form both keeps that and saves a byte.
4. **`dsk_fatw_demote`'s compaction-arm epilogue as `jmp short .out`** (-1):
   pass 15 kept its own epilogue ON PURPOSE for `dsk_next_clus_x`'s parity.
5. **`mod_disarm`'s called `.slot` loop inline** (-4): it is pass 14's parity
   pad for `mem_bytes_kb_x` (ksp15 REFUSED, still binding).
6. **A shared `push bx / call mem_own / jc / call X / pop bx / retf` tail** for
   `mmf_osapi_mem_free`, `osapi_mem_regrow_x` and `osapi_mem_movable_x`: no
   spelling is shorter than the 10 bytes each already is.
7. **`mod_need`'s `.fail` reset of `[dskw_trunc]`** (5 bytes): moving the set
   after the claim to make the reset unnecessary needs a second `cmp di,
   MOD_SETS` and costs more than it saves.

## WANTS THE OWNER

None. No behaviour changed; every refusal path answers as it did (the one
flag difference, item 7's CF, is outside its documented contract).

## WANTS AN ABI BREAK

None. No cell moved; `OSAPI_MEM_P`'s contract is untouched.

## CROSS-FILE

1. **`drv_release` now calls `dsk_ent_zero.zero`** in `disk.inc` (fsys's
   file, not edited): the loop's contract it relies on is "DS:DI, CX words,
   writes DI and CX and nothing else". A change to that loop changes
   `drv_release`.
2. `tests/megaport.txt`'s `vidsound` red check pokes `stc / retf` at
   `mmf_osapi_mem_claim_dma_hi`: still the cell's entry, still a refusal.
3. `tools/dsegaudit.py`: CLAIMERS updated for item 1.

## DEFECTS FOUND

1. **kern_mega, protected mode: `mem_regrowp`'s path 3 rebased the moved
   claim's selectors by the wrong amount** - since `c2e139eb`. The arm does
   `xchg ax, dx / MEM_MP mem_mpmove` with AX meant to be the OLD base, but
   `mem_bcopy` WRITES DX (0 forward, 1 backward - its chunk direction) while
   its banner said it preserved every register, so `mega_mpmovef` was handed
   0 or 1. FIXED: the arm banks DX across the copy (`%ifdef KERN_MEGA`, +2),
   and `mem_bcopy`'s banner now says DX is written. Found by reading; no row
   I know of drives a protected-mode path-3 regrow, so it is not verified on
   Bochs beyond the rows below.

## ROWS RUN

Once, after the last code commit (`12a2de92`), `python3 tools/os88test.py
soak --marty-jobs 1` scoped to the heap's rows: **25 passed, 0 failed, 0
skipped** in 656 s - `drvmove`, `sndmove`, `pgrank`, `dsegaudit`, `regwork`,
`regapp`, `regpin`, `regmove`, `regrowshed`, `pins`, `pinsrt`, `small128`,
`rehome`, `rehomemove`, `fatwpin` (146 s against a declared 30: load from five
agents, and it passed), `heapmap`, `memp`, `heapcheck`, `heapdrv`,
`heapscrl`, `hdnoclaim`, `heaphi`, `trkcompact`, and on kern_mega under Bochs
`mega-regmove` and `mega-heapcheck`. The fast tier (`make`, hotpar included)
is green at the tip, and `tools/stkbalance.py` over the four touched files
plus `kernel.asm` reports the same 14 findings at the base and the tip (none
new). `make small`, `make emu` build; `make mega` builds the kernel and stops
at the base's own `os88megaseg` failure, at the base's figure.
