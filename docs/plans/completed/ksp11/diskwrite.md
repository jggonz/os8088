# Kernel size pass 11 - agent "diskwrite"

Concept: two merges on the disk path - `bc6b0d87` ftp-speed (SPEC.md 18.4.9.3
`WSEQF_KEEP`/`WSEQF_CKPT`, 18.4.10 the FAT16 free-cluster search) and
`42e0db93` disk-cpu (SPEC.md 18.91.6, `dsk_xfer` asks once a run whether a
bar is live) - plus pass 10's leftover relaxed jumps in `disk.inc` /
`diskw.inc`. Branch `ksp11-diskwrite`, cut from `elendilon` at `2a05e31f`.
Figures are `tools/kernsize.py --json`'s sections, bytes, ASSEMBLED.

## Base and tip

| | base `2a05e31f` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.bss` / `.cold` | 43,428 / 5,190 / 37,943 | 43,419 / 5,190 / 37,864 | -9 / 0 / -79 |
| kern_big resident | 92,495 | **92,407** | **-88** |
| kern_big `KERN_SIZE` | 98,304 | **97,792** | **-512** (the cold rung, 55 bytes into its 75th step at the base, uncrossed) |
| kern_big `.ovl` / `.ovlw` | 2,471 / 5,086 | 2,471 / 5,086 | 0 |
| kern_small `.text` / `.bss` / `.cold` | 32,022 / 3,073 / 23,479 | 32,022 / 3,073 / 23,422 | 0 / 0 / -57 |
| kern_small resident | 61,442 | **61,385** | **-57** |
| kern_small `KERN_SIZE` | 63,488 | 63,488 | 0 |
| kern_emu (`make emu` at the tip) | | `.text` 43,678 `.bss` 5,190 `.cold` 37,982, `KERN_SIZE` 98,816 | assembles; every change here is kern_big's and reaches it unchanged |
| drivers, `FTPD.O88` | | | 0 (no driver or package source touched) |

`kern_dos` (which includes `disk.inc` and `diskw.inc`, and has `DSK_STREAM`)
assembles in every `make`.

## Why ftp-speed cost kern_big twice what it cost kern_small, and why it touched ui.inc and vga12.inc

The merge is two features of different reach. **18.4.10** (the FAT16 search,
`dskw_alloc16`, +114 `.cold`) is in `dskw_alloc`, which both kernels have:
that is kern_small's whole +114. **18.4.9.3** (`WSEQF_KEEP`/`WSEQF_CKPT`,
+108: `.text` +34, `.bss` +2, `.cold` +72) is WRITE_SEQ, which is
`DSK_STREAM` - kern_big's and kern_dos's only (kern_small's cell answers
FERR_NAME). It touches `ui.inc` and `vga12.inc` because a HELD stream's commit
points are where the user can reach the drive: `gfx_unlock` (every UI-task
unlock commits a pending hold; a KEPT hold inside a wake is the one exception,
so the unlock has to know "kept" and "inside a wake"), `ui_task`'s wake arm
(which marks "inside a wake", `[dws_inwk]`) and `ui_task` step 0 (a posted
restart or hibernate must commit a kept hold first). Those are `.text`, so
the `.text` +34 is the unlock's compares, the wake arm's stores and step 0's
far call.

## TAKEN

### 1. `dskw_alloc`: the FAT16 scan shares the loop's wrap (kern_big .cold -42, kern_small .cold -42)

`dskw_alloc16` was a routine of its own with a second copy of the rover wrap,
its own `push/pop` of BX, a window-end computed from `[dsk_fatw0] +
[dsk_fatwn]`, and BOTH bounds (the last cluster and the candidates left). It
is now `dskw_alloc`'s `.scan`, entered from the loop's `.check` after the
shared wrap; the window's entries are `[dsk_fatwn]*256 - BX/2` straight off
`dsk_fat_ofs_x`'s offset; and the scan is bounded by the window and the last
cluster only, the candidates being CHARGED with what each window asked
(`sub cx, bx / ja .try`). Past the last candidate lie only clusters the same
search has already seen used, so an overrun finds nothing a bounded scan
would not - checked in a Python model of both against the per-cluster loop
over 20,000 random FATs (sizes 3..65,524 clusters, densities 0 to full,
rovers out of range both ways, the padding tail zero): identical answers. A
window that will not load asks that ONE cluster the per-cluster way (which
retries the load) where the old code handed the whole remainder back; the
answer is the same, a failed window being asked again either way.

Cycles: the FAT16 per-window cost is the old one less a `call`/`ret` and
two pushes; a FAT12 candidate (every floppy) now pays the `cmp byte
[dsk_fattype], 0 / jne` it did not (~24 cycles against a `dsk_next_clus_x`
of several hundred, and only on the first allocation after a mount, which
walks the used run). Once per allocation, not hot by the brief's list.

### 2. `WSEQF_KEEP` / `WSEQF_CKPT` (kern_big .text -9, .cold -22; kern_small 0)

| item | bytes | note |
|---|---:|---|
| the checkpoint arm shares the door's hot test | -16 | `.close` was its own copy of `cmp di, [dsk_mgen] / dws_ours.vol`; now a CKPT close jumps into `.hot`, where `jcxz .ckpt` tells a checkpoint from a write once the token is known hot. Same answers on every path |
| the keep verdict | -4 | `mov al, [dws_flg] / and al, WSQF_KEEP` gives AL = 0 for "not asked" for free; `dsk_vol_fixed_x`'s CF=1 arm cannot happen on a volume just written, and were it ever taken AL = 4 is not 1, so the unlock commits |
| `dsk_vol_del` | -7 | `[dws_vol]` alone is asked: `dws_commit` tests `[dws_hold]` itself (a stale `[dws_vol]` costs a near call that returns) |
| `gfx_unlock`'s banked test moved to the bank | -7 text, +5 cold | `dws_hswap` clears `[dws_keep]` whichever way it swaps, so the unlock asks one word. A hold that hopped off its volume and BACK within one wake now commits at the next unlock where it used to stay - safe direction, rare case; SPEC.md 18.4.9.3 says so |
| the wake arm's stores | -2 text | `inc`/`dec byte [dws_inwk]` for `mov 1`/`mov 0`: `.bss` is zero at boot (SPEC.md 2.5) and a nested wake reads 2, which commits |

### 3. Relaxed jumps, pass 10's leftover (kern_big .cold -15, kern_small .cold -15)

| item | bytes | note |
|---|---:|---|
| `dsk_xfer`'s write-protect `je .fail` | -3 | lands on the `jbe .fail` after the retry loop, which ZF=1 takes. +16 cycles on the write-protect failure only; the per-sector and per-run paths are untouched |
| `dskw_rbody`'s `jne .czbad` | -3 | lands on the `jae .czbad` under `.fits`: a compare with 0 leaves CF=0 |
| `dskw_read_at_x` | -9 | the redirected arm and the answers moved ABOVE the FAT body (`jz .badarg`, `jc .err` reach backwards short, `jmp .fsat` gone); the short-chain and failed-read exits share one tail (`mov ax, [dwr_take] / mov dx, 0 / jnc / mov ax, FERR_IO`), so the skip loop's `jc` stays short too. Same answers on every path |

`dsk_fdd_probe`'s relaxed `jc .nordy` is in `.ovlw` (boot only): left.
`ui.inc`'s two (`jne .evloop`, `jz .posted_done`) and `vga12.inc`'s twelve (kern_big)
are other owners' and were not looked at.

### 4. A row for the DMA bounce (0 kernel bytes)

`tests/dskwstage.py` grew three legs after its five cases: B0 makes BNC.TST,
B1 rewrites its first cluster through `dskw_write_at_x` from a buffer 0xF0
short of a 64KB page, B2 reads it whole through `dskw_read_at_x` into a
straddling buffer with `[dsk_rah_busy]` = 1. `dsk_xfer.bounce` and
`.unbounce` must fire exactly once per leg, ES must come back the caller's,
and BNC.TST is checked in the guest and off the flushed floppy. Every
file-layer case asserts the bounce fired ZERO times (the control). **Broken on
purpose twice**: the write's copy into `dsk_secbuf` out (B2 and the host read
red), the read's copy out to the caller out (B2 red, poison at byte 0). The
row runs 13.8s and declared 120: `secs` is 15 now. The row breakpoints
`dskw_xclus.stg` by name, so `diskw.inc`'s two `equ` aliases are gone (0
bytes; ksp10/disk.md cross-file 2).

## REFUSED

| candidate | bytes | why refused |
|---|---:|---|
| drop `WSEQF_CKPT`: every close of a HOT token hands back a hot token | ~-6 | a semantic change for seven other WRITE_SEQ callers (filecp, clone, compress x3, ramdisk, the HDD installer, kern_dos) that would each have to be audited for reusing a closed token on a new file |
| make `WSEQF_KEEP` implied by `WSEQF_HELD` | ~-6 | every HELD writer that runs inside a wake on a fixed disk would keep its hold to the next non-wake unlock; `wsequnclosed`'s premise is that the unlock commits |
| fold `[dws_inwk]` into `[dws_keep]` (one byte, two bits) | +3 | the door must then preserve the wake bit (`and`/`or`, +6) to save one byte at the unlock |
| commit a kept hold in `ui_cmd_reboot` and `hbf_perform` instead of `ui_task` step 0 | 0 | 5 + 4 bytes against the 9 it replaces |
| `dsk_xfer`'s per-run bar test (disk-cpu, +15) | 0 | already minimal: `[spl_fseg]`'s dead value is `COLD_SEG` (imm16, 6 bytes, SPLCALL's own test) and `[fpg_total]` is a word; reordering the two moves no cycle in the common case (neither bar live: ~64 cycles a run against two far-ish calls a SECTOR) |

## Defects found

None. One observation that is not a defect: `tests/ftpkeep.py` failed ONCE
at its third leg's setup (`drive C: has no desktop zone`, `dispcp.open_drive`
straight after `make test`) with legs 1 and 2 green, and passed whole on the
re-run - its `launch` paces the boot with fixed host sleeps (`settle` is
`time.sleep(2.0)`), which a box running six agents shortens in guest terms
(docs/plans/SOAK-PARALLEL.md 1). Cross-file, below.

## Cross-file

* `tests/ftpkeep.py`'s `launch`/`settle` are host sleeps (docs/WRITING-TESTS.md's
  `time.sleep` failure); the flake above is that shape. Not registered in
  the suite (`t_registry.py` exempts it: QEMU with ETHFWD=1), so it is
  nobody's gate today.
* **Shared-file hunks, for the merge**: in `disk.inc` this branch touches
  only `dsk_xfer`'s write-protect `je` and a label on its `jbe .fail` (the
  retry block), and `dsk_vol_del`'s `%ifdef DSK_STREAM` head; in `ui.inc` the
  wake arm's two `dws_inwk` lines; in `vga12.inc` `gfx_unlock`'s
  `[dws_keep]` compare. voltake and picomem share `disk.inc`.
* `ui.inc`'s two relaxed jumps (`jne .evloop`, `jz .posted_done`) are in the
  file but not in this concept's hunks; not looked at.
* SPEC.md 18.4.9.3's cost paragraph and 18.4.10's now carry pass 11's
  figures; nothing else in the tree quotes `dskw_alloc16` (the
  `tests/unit/t_asmrules.py` and `tests/suite.py` prose naming
  `dskw_wdata.stg` is history and was left).

## Rows run

All on the branch tip's build (MartyPC unless named), one invocation:

| row | result |
|---|---|
| `wseq` (FAT16 allocation on an XT-IDE volume, 12.5 MB) | ok 257s |
| `wseqfull` (the volume FILLS: the scan's full-volume exit) | ok 125s |
| `wsequnclosed` (gfx_unlock's commit is the only commit) | ok 126s |
| `wseqdeleted` (`dws_gate`'s commit) | ok 71s |
| `czseq` (the bank across hops - `dws_hswap` now clears the verdict) | ok 163s |
| `czseqlose` (the poisoned hold) | ok 56s |
| `dskwstage` (staging, and now the transfer's bounce: B0-B2) | ok 12.5s, then 13.8s with the B legs |
| `rdcz` (`dskw_rbody`'s compressed arm, redirected) | ok 79s |
| `lzfile` (compressed reads, `.czjae`) | ok 25s |
| `fcpcopy` (FAT12 allocation and the write path) | ok 52s |
| `shedrelist` | ok 22s |
| `tests/ftpkeep.py` (QEMU, hard-disk boot: KEEP/CKPT) | legs 1-2 green then a setup flake at leg 3 (above); re-run **all four legs green**: KEPT 3,107 disk writes against PLAIN 3,665 for 192 chunks (2.91 a chunk, gate 1.5), the cut leaves 32 KB + 4 checkpoints exactly, fsck clean |

Gates: `make -j2` and the fast tier (61/61) after every batch, `make -j2
small`, `make emu`; `tools/stkbalance.py` over `disk.inc`, `diskw.inc`,
`ui.inc`, `vga12.inc`, base against tip: 15 unbalanced paths both sides (the
same 15), 654 -> 653 entries walked (`dskw_alloc16` gone). No relaxed `jcc`
left in `disk.inc`/`diskw.inc` on either kernel but `.ovlw`'s `jc .nordy`
(checked off a `nasm -l` listing for the `7x 03 E9` pair).
