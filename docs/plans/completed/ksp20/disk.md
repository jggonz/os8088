# Kernel size pass 20: disk (the read-ahead, the directory writer, FIND_SEQ, the loader)

The **disk** agent's record (BRIEF.md section 2). Owned: `disk.inc`,
`diskw.inc`, `drvvol.inc`, `loader.inc`, `lz.inc`, `lzfilt.inc`,
`compress.inc`, `fsck.inc`, `diskops.inc`. Branch `ksp20-disk`, cut from the
brief commit `9c785cf3`. TAKEN outside those files: nothing. Every byte
taken is `.cold` (kern_mega: `.cold` above 1MB, so its conventional figure
does not move).

## BASE AND TIP

Assembled, `tools/kernsize.py --json` on all four kernels, the base built from
`9c785cf3`'s sources against this tree's generated includes. Resident =
`.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga` (+ `.mega`).

| kernel | `.text` | `.bss` | `.cold` base -> tip | resident base -> tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---|
| **kern_big** | 39,345 | 4,892 | 37,933 -> **37,840** | 90,086 -> **89,993** | **-93** | 95,232, unchanged |
| **kern_small** | 29,112 | 2,843 | 21,153 -> **21,044** | 55,976 -> **55,867** | **-109** | 57,856, unchanged |
| kern_emu | 39,591 | 4,892 | 37,933 -> **37,840** | 90,332 -> **90,239** | **-93** | 95,744, unchanged |
| kern_mega | 44,929 | 4,930 | 48,412 -> **48,331** | 128,895 -> **128,814** (`.mega` 24,688) | **-81** | conventional 61,440 of 68,096, unchanged |

**kern_small's accrual: 168/512 -> 59/512** (`kernsize[small]: accrued
kernel 59/512 (11%)`): 109 of the 169 bytes the rung wants. `.lowbss`,
`.vgabuf`, `.vga`, `.boot2`, `.ovl` and every module and driver image are
unchanged. No ABI moved; no package, driver or C SDK file is touched, so
`abilock` has nothing to update.

## TAKEN

Bytes per routine are `tools/os88rdiff.py kernel 9c785cf3 .` (and `-D
KERN_SMALL --build build/smallk`), measured, not estimated. "all" = kern_big,
kern_small, kern_emu and kern_mega; kern_mega's read-ahead pick and scan are
the macros' inline homes in `dsk_rah_fill`/`dsk_rah_serve` (no `DSK_RAHINT`),
so it takes those two at the same bytes and has no `dsk_rah_pre`. Every item
is H4 (per transfer, per mount, per launch, per directory write) unless said.

**THE int 13h BAR.** No item changes an I/O decision, so no operation gains or
loses a call - by construction, read off the code rather than counted on
MartyPC's FDC: the pick visits the same slots in the same order and spares the
same chunks (a HIT byte is 0 or 1, `.bss` being zero at boot - SPEC.md 2.5);
the scan's hit/miss answer is the same predicate (an empty slot is refused by
the extent compare instead of a test of its own); the fill's length, the
prefetch's run (`9 x (runs - slot)` is what the cursor arithmetic computed)
and the solve's width (the same quotient) are unchanged; `dskw_mkclus` writes
the same `spc` sectors in the same order with the same contents. The disk-3 /
disk-5 / disk-4 counts (open a root 2, a folder 2, Up 1, a cached launch none;
launches 2 and 3 calls on 360KB) stand as measured at khp1.

### Batch 1 - the read-ahead and the directory writer (`c4416da2`)

1. **`RAH_PICK`: a visited chunk's mark spent by one shift, the page test off
   slot x 9** (-10 all). `cmp`/`je`/`mov byte 0`/`jmp` became `shr byte
   [si+RAH_HIT], 1` / `jc` (the byte is 0 or 1: the shift is the test and the
   clear, -7), and the page test multiplies the slot x 9 that the record needs
   anyway by 32 with a byte `mul`, where it multiplied the slot by 288 under a
   push/pop (-3); `lea si` names the record. Faster: two byte multiplies for
   two word ones, a push/pop gone.
2. **`RAH_SCAN` and `dsk_rah_wr` drop the empty-slot test** (-6 each, all).
   `RAH_N = 0` is refused by the extent's `cmp ax, [si+RAH_N] / jae`
   whatever the delta, and in `dsk_rah_wr` the first overlap test cannot pass a
   0 length while the second only re-zeroes a zero. Cycles: a FILLED slot - the
   cache's steady state, every slot after the first few fills - is ~27 clocks
   cheaper; an empty one (only before the cache has filled) pays its VOL/SIG
   compares, ~40 more.
3. **`dsk_rah_pre` counts the slots from the record pointer** (-8 big, small,
   emu). `9 x runs + dsk_rah_tab - SI` is `9 x (runs - slot)`, the cursor
   arithmetic's answer, wrap case included.
4. **`dsk_rah_want` solves with a byte divide** (-4 all). The dividend is at
   most BX, half the arena in KB (<= 511), so the quotient is under 57: `div
   cl` + `cbw`, byte compares, `mul cl`. Faster (~70 clocks a solve).
5. **`dskw_mkclus` writes the cluster in one loop** (-9 all). Sector 0 as BP
   built it, then the buffer zeroed for the rest, counted in SI (which the
   write and the zeroing keep). The last pass zeroes and steps the cursor once
   more than it uses; nothing reads either (dskw_xclus sets the cursor, the
   buffer is scratch). One 512-byte `rep stosw` a sector more, ~1 ms against
   the ~400 ms write beside it, on a mkdir or a folder's growth.
6. **`dskw_wbody`'s two tails** (-3 all): FERR_IO is CF = 1 out of `cmp ax,
   FERR_NOENT` (2 < 4), so `jne .out` replaces `je .create / stc / ret`, and
   the grow's refusal is `jc .out` - `dskw_mkbody`'s own shape.
7. **`dsk_find_x` banks FIND_SEQ's token with `pop`/`push`** (-6 big, emu,
   mega - the `DSK_DSCAN` arm): the frame word was addressed through SP twice.

### Batch 2 - the free count, the mount (`7f47e1e4`)

8. **`dsk_free_clus_x`'s FAT12 walk tests the entries' words in place** (-9
   all): `test word [es:bx], 0x0FFF` and `test word [es:bx+1], 0xFFF0` - the
   even entry is the word's low 12 bits, the odd one `b1 | b2<<8`'s top 12 -
   where each was loaded and masked. Faster per pair (fewer bytes fetched, no
   register work).
9. **...and kern_small's relays at `.c12`** (-6 small): with no redirected arm
   falling into them they sit where nothing jumps over them, `.jcounted` being
   `.c12`'s own `jmp short .counted` and `.slowj` short.
10. **`dsk_bpb_check`'s `.fail` and `.jfail2` are one `stc`/`ret`** (-2 all),
    placed after `jmp short .capcmp` where every jump from rule 13 on reaches
    it short on both kernels.
11. **`disk_mount_x`'s ES reload before `asc_use_x` is kern_big's alone** (-2
    small): kern_small has no call there and the pair after it is the same two
    instructions.
12. **`dsk_fatw_pick`'s `jmp short .out` to a `ret` is a `ret`** (-1 all) -
    held for parity since pass 14; this pass's window wanted the odd byte.

### Batch 3 - what the callers read (`12af9b13`)

13. **`dsk_rah_serve` banks SI and DI only** (-3 all): dsk_xfer, its one
    caller, loads CX at `.count` and CX and DX at `.nocache` before reading
    either, and serve never writes BX. Its own `pop di / pop si / ret` replaces
    the jump to dskw_find's epilogue. ~-96 clocks a served run.
14. **`dsk_rah_fill` banks BX, DX and ES only** (-4 all): serve reads those
    three after it and loads AX, CX and DI. ~-96 clocks a fill.
15. **`dsk_runcap` banks AX on kern_mega alone** (-2 big, small, emu; kern_mega
    keeps it - its bounce reads the run back out of AX). dsk_xfer pops AX over
    it on both ways out. Pinned hot (t_hotpar): its entry does not move, the
    routine is ~27 clocks a run faster, and `dsk_next_clus_x` keeps its even
    shift.
16. **`dsk_xfer`'s `.unbounce` ended in a jump of zero bytes to `.sector`**
    (-2 all, both arms). Pinned hot: entry unmoved, the run loop after it at an
    even shift, the unbounce path 15 clocks faster.
17. **`dsk_find_x`'s `DSK_DSCAN` tail** (-6 big, emu, mega): dsk_dscan answers
    the FERR_* itself, so `.end`'s pair and `.fail`'s `stc` are dead there and
    success falls into `.out` (the `jc` is the one way to `.fail`, CF already
    1).
18. **kern_small: `dsk_find_x`'s `.stage`** (-13 small): both ways into it -
    the FSV_ENUM arm and DSK_DSCAN's redirected test - are OS88_REDIR's, so on
    kern_small it was eleven unreachable bytes and the `jmp short .staged` over
    them.
19. **kern_small: `dsk_fatw_demote` falls into `dsk_fatw_home`** (-4 small):
    without a compactor the `call` + `ret` was the routine above the label, and
    its two refusals take dsk_fatw_scan's `ret` just above it (`DFD_OUT`).
20. **kern_small: `dsk_byclus_x` banks ES alone** (-6 small): dsk_path_x is not
    assembled there and dskw_rt_byclus banks AX..DI under its own KENT frame.
21. **`ld_run_body_x`'s tails sit where the peek falls into them** (-2 all):
    `jnc .hdr` over `mov al, LD_EDISK / ret / jmp ld_unreserve`, no `jmp
    short .read` over them.

### Batch 4 (`69ca1979`) and 5 (`18792784`)

22. **`dsk_rah_redo` banks AX on kern_mega alone** (-2 big, small, emu): only
    dsk_rah_dropf reads AX after it; osapi_dsk_cache_x loads it.
23. **`dsk_vol_drop_drv_x` does not bank AX** (-1 big, emu, mega): its one
    caller, drv_free, zeroes AX next. (khp1 size.md section 6's lead.)
24. **`dsk_fatw_pick` tail-jumps to `dws_hret`** (-1 big, emu, mega: CF = 0
    either way). With item 23 it keeps `dsk_next_clus_x`'s parity.
25. **`ld_start`'s success** (-3 all): `xor ax, ax / mov [ld_base], ax` clears
    the region word and answers LD_OK (0, asserted) - every caller reads AL and
    none a flag.
26. **kern_small: `dsk_goto`'s refusals as one SKIPF2 tail** (-2 small): with
    nothing falling into `.bad` it follows `.full`, and `cbw` serves both.

Per routine, kern_big: dsk_xfer -2, dsk_runcap -2, dsk_vol_drop_drv_x -1,
dsk_bpb_check -2, dsk_fatw_pick -2, dsk_free_clus_x -9, dsk_rah_find -6,
dsk_rah_pick -10, dsk_rah_serve -3, dsk_rah_fill -4, dsk_rah_pre -8,
dsk_rah_wr -6, dsk_rah_redo -2, dsk_rah_want -4, dsk_find_x -15 (two items and
a jump that came into short reach), dskw_mkclus -9, dskw_wbody -3,
ld_run_body_x -2, ld_start -3 = **-93**. kern_small: the same less the
DSK_DSCAN items, plus disk_mount_x -2, dsk_bpb_check -2, dsk_fatw_pick -1,
dsk_fatw_demote -4, dsk_free_clus_x -15, dsk_find_x -13, dsk_goto -2,
dsk_byclus_x -6 = **-109**.

**Parity** (`hotpar`, fast tier, green at every batch): dsk_xfer and
dsk_runcap keep their entries; `dsk_next_clus_x` moved -16 on kern_big and -28
on kern_small (both even) - items 12, 23 and 24 are the odd bytes that pair
the window up. No pad byte was added.

## REFUSED / NOT TAKEN

* **`dsk_rah_cap` stored as `min(AL, DSK_RAH_RUNS)`**, which would fold the
  solve's ceiling into the cap's compare (about -8): the sentinel 0xFF is read
  by `tests/dirwshed.py` and the API's "ceiling already in force" test would
  stop re-solving between an explicit 7 and AUTO - a behaviour change.
* **`dsk_bpb_bank_zap`'s AX on kern_small** (-2): the body's `pop ax` is
  shared with `dsk_bpb_flopzap`, whose callers are promised every register.
* **`dsk_free_clus_x`'s CX on kern_small**, **`dsk_find_x`'s SI** (khp1
  leads): the first leaves through dskw_find's shared epilogue (no saving), the
  second is OSAPI_FILE_FIND's register contract to packages.
* **`dskw_read_at_x`'s answers above its entry on kern_small** (-2): reachable
  only by defining `dskw_read_at_x.*` labels ahead of the routine's own; not
  worth what it does to the label scheme and the tools that read it.
* **The fallback 9/2 geometry as one helper** (`disk_mount_x` entry and
  `.fail`): -1 net.
* `fsck.inc`'s `call x / ret` tails (FSCK's image, not resident) - module
  bytes, left for a module pass.

## WANTS THE OWNER

None.

## CROSS-FILE

None found.

## ROWS NAMED (none run - the coordinator's union)

* The read-ahead (items 1-4, 13-16, 22): `dirwsize`, `dirwshed`, `ldcost`,
  `dosbar`, `dosmedia`, `xtmp3eng`, `small128`, `smalllaunch`, `lzship`,
  `kdcylrun`, `megashed`, `megarsq`, `hdbounce` (kern_mega's runcap and fill),
  `drvcut`, `dskwstage`, `hotpar` (fast: green).
* The directory writer (5, 6): `stiknote`, `stiknotevga`, `stiknotesmall`
  (BR-60's growth), `win16file`, `fcpcopy`.
* FIND_SEQ / dsk_find_x (7, 17, 18): `stiknote`, `thewire`, `rdmount`.
* The free count (8, 9): `sysfree`, `fcproom`, `dosdirsw`.
* The mount, FAT window, BPB (10-12, 19, 24): `fatwpin`, `imgmount`,
  `rdmount`, `small128`.
* `dsk_byclus_x` (20): `pathcost`; the folder-delete rows on kern_small.
* `dsk_goto` (26): `dosmedia`, `assocsweep`, `imgmount`, `czto`.
* `dsk_vol_drop_drv_x` (23): `hdauto`, `megahddtick`, `wirezone`.
* The loader (21, 25): `pkgrun`, `rehome`, `rehomeabort`, `smalllaunch`,
  `ldcost`.

Host checks at every batch: `make -j2` (fast tier, 44 passed), kern_small via
its sub-make, `tools/stkbalance.py` base against tip over the touched files
(one unbalanced path FEWER: dsk_rah_fill's tail into `kretc_dx` at +4 is
gone), `checkdocs`. At the tip: `make emu`, `make mega` green. **One boot to
look**: kern_small's `small360.img` with `apps360.img` on MartyPC
(`os88ui.boot`, `OS88_BUILD=build/smallk`) mounted B:, listed APPS and
launched Calculator - the mount, the listing, the read-ahead's fill/serve, the
prefetch, the loader and `ld_start` on the item-by-item changed code.

## DEFECTS FOUND

None. One note for kern_dos (not one of the four kernels): its `.bss` is not
zero at load (kerndos/kdosgate.inc), so a never-filled slot's RAH_HIT byte can
be garbage on the first turn of the pick, where the old test spared such a
slot once and the shift spares it while bit 0 is set (at most eight times, and
the pick still ends). Which slot a first fill spends is a heuristic over
uninitialised memory either way; what the cache holds is correct either way.
