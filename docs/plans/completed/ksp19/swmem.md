# Kernel size pass 19 - `swmem`: the wave-C sweep over the memory and disk files

Branch `ksp19-swmem`, cut from `kernel-size-19` at `f49e64a3`. Files:
`kernel/memory.inc`, `diskw.inc`, `loader.inc`, `mod.inc`, `xmem.inc`,
`lz.inc`, `disk.inc`. Scope narrowed mid-run by the coordinator: the
`MEMP_KB` arm and `apps/os88api.inc`'s `OS88KB_BODIES`/`MEMKB_*` are
`kbpara`'s (it converts every package KB site and deletes both), so neither
was touched. Nothing in the API table, `rd_tab` or any `OSAPI_*` define moved.

## BASE AND TIP

`tools/kernsize.py --json`, resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (+ `.mega`). Only `.cold` moved.

| kernel | base | tip | delta |
|---|---:|---:|---:|
| kern_big | 90,081 | 90,049 | **-32** (`.cold` 37,670 -> 37,638) |
| kern_small | 56,257 | 56,240 | **-17** (`.cold` 20,982 -> 20,965) |
| kern_emu | 90,327 | 90,295 | **-32** |
| kern_mega | 129,422 | 129,395 | **-27** (`.cold`) |

No package, driver, module or C package changed (no SDK edit).
`t_hotpar` green on both kernels; `stkbalance` green.

## TAKEN

Bytes kern_big / kern_small. None is on a hot path except item 5, which is
cycle-neutral (the compare moved into the routine; `pop es / jmp kretc_di`
unchanged).

1. **`OSAPI_MEM_HUGE` falls into its claim** (-4 / -4, not kern_mega):
   off kern_mega the cell is `mmf_mem_claim_own`'s only caller, so its
   `jmp short .pushed` went (the label too) and the `xor cx, cx` - a DMA head
   no door reads since C1 - with it. kern_mega unchanged.
2. **`osapi_mem_compact_x`'s what-if** (-2 / -2): `mem_avail_lvl_p` ends in
   `popf`, so the `or ah, ah` ZF survives the call; the KB answer is a call
   to the shift alone (`mem_avail_kbdn`, a label inside `mem_avail_lvl_x`).
   The `.nested` arm's `xor` sets ZF and answers 0 in either unit.
3. **READ_AT's division shared** (-8 / 0, DSK_STREAM): `.fatat` calls
   `dwr_cidx` instead of spelling its three instructions. ~+35 cycles per
   READ_AT call (each of which is an int 13h read).
4. **`ld_where` loads ES = `[ld_base]`** (-4 / -4): both callers did it next.
5. **`dsk_fat_window` answers ZF** (-5 / -5, then -1 / -1 for the pad):
   both callers compared `[dsk_fatw0]` with 0xFFFF after the call; the
   compare is at the routine's `.out`. Odd, so the two parity pads that kept
   `dsk_next_clus_x` at its measured shift (kern_big's in `disk_mount_x`,
   kern_small's at the end of the OS88_DRIVERS block) went with it.
6. **`dskw_pmask` answers the attribute test** (-4 / -4): both callers
   tested `[dskw_raw+DSK_R_ATTR]` against its AL next.
7. **`mod_need`'s header end off SI** (-1 / -1): `add dx, si` after `mov si,
   MOD_H_ENT` moved up.
8. **Four word zero-stores whose flags are dead become `and`** (-2 resident
   / -1; the two `DV_SECS` stores in `disk.inc` are boot-path bytes outside
   resident): `ld_fsz+2` (flags rewritten by the `not`/`test` after),
   `ld_rehome` in `ld_start` (the `shl ax, cl` with CL = 4 rewrites them),
   `DV_SECS` twice (`sub ax, dsk_vtab` after). +7 clocks each, none hot.

## REFUSED / RECORDED

* **The KB arm of `mem_claim_x` cannot go while `OSAPI_MEM_HUGE` counts in
  KB** (the coordinator's question; kbpara does not touch HUGE). HUGE's AX is
  1..65535 KB and needs the `< 1024` refusal and the shift somewhere: made
  paragraph-only, `mem_claim_x` loses 11 bytes and `mmf_mem_claim_own` gains
  ~13 for HUGE and the kern_mega regrow move. What would fold is
  `mem_claimp_x`'s 3-byte glue and, on kern_mega, its ~16-byte
  paragraph->KB arm - but the ~20 internal callers then need paragraphs: the
  constants convert free, the four `mem_bytes_kb` sites (wm, menu, clip,
  stikmenu) and the KB state they keep (`[clip_kb]`, `FS_VKB`, `[cmz_k]`,
  `[ico_kb]`) do not without growing or changing claim sizes (granule- not
  KB-rounded: a behaviour change). Net about -4 big/small, -20 kern_mega,
  over edits in ~12 files most of which are other owners'. Not taken.
  HUGE itself cannot be paragraphs (a >1MB request does not fit a word).
* **`mem_regrow`'s KB arm** (~14 bytes): its three callers (`ico_grow`,
  `fmv_fit`'s grow, `sc_m_mem`) hold KB state and would each pay a shift; ~0.
* **`MEMC_WHATIF` in KB** (ABI, now allowed): withdrawing the KB form saves
  ~7 `.cold` after item 2, against ten package sites that print KB and would
  each grow a shift. Larger in total; not taken.
* `dsk_fat_window`'s `.out` as `jmp kretc_es` (-1): wrong, `kretc_es` pops
  BP too (stkbalance caught it).
* `ld_unreserve`'s and the cell paths' `mov word [ld_base], 0` and
  `[ld_rehome]` at `.rhfreed`: CF/flags reach a caller; left as `mov`.
* `dskw_fatclean`/`dskw_refat` stores: "it leaves CF alone" is its contract.
* `lz.inc` not edited: its fast paths are KHP_LZ's instruction-identical
  bodies; nothing found that is smaller without touching them.

## ROWS (for the final union)

memory: `os88huge`, `chuge-big`, `chuge-mega`, `megahuge`, `x3huge`,
`audhuge`, `brhuge`, `waverackhuge`, `tmsmall`, `trkcompact`, `memp`,
`heapcheck`; READ_AT/READ_SEQ and the FAT window: `fatwpin`, `lzship`,
`lzship-lzb`, `dosfile`, `fcpcopy`, `ftpd`, `thewire`; the write tests
(`dskw_pmask`): `install`, `installabi`, `mainstall`, `httpinstall`,
`wseq`; loader: `rehome`, `rehomeabort`, `rehomemove`, `rehomemove360`,
`megarehome`; modules: `moddrop`, `drvmem`. Plus `hotpar`, `stkbalance`
(fast). None run here (no soak rows by brief).

## For the coordinator

kbpara: once `MEMP_KB` and the MEMKB bodies are gone, the only KB claim
consumers left in the kernel are `OSAPI_MEM_HUGE` and the internal callers
above; the shift in `mem_claim_x` stays for HUGE (see REFUSED).
