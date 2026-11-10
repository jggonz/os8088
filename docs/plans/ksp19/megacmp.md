# Kernel size pass 19 - `megacmp`: kern_mega's placement and compaction audit

Branch `ksp19-megacmp`, cut from `kernel-size-19` at `7ba85e9f`. The owner's
concern: *"This seems like a yoyo; we JUST did a bunch of work to get mega able
to fully compact. Make sure we didn't break that work."*

## THE FLAG (for `kbpara`, written first)

**`MEMP_DMAPOOL equ 0x4000`** - a bit of **CX** on an `OSAPI_MEM_P` claim
verb (`MEMP_CLAIM` / `MEMP_CLAIM_HI`, SPEC.md 50.3.7), beside the head:

    mov ax, <paragraphs>
    mov cx, <head paragraphs> | MEMP_DMAPOOL
    mov bl, MEMP_CLAIM          ; or MEMP_CLAIM_HI
    call OSAPI_MEM_P

* **On kern_mega in protected mode, for an OS88_HIGHMEM package** (never a
  driver), a claim of 64KB or less with a head of 64KB or less is a **pool
  block above 1MB, BORN PINNED, its head inside one 64KB physical page**
  (`mega_claimdf`, `MX_FDMA | MX_FPIN`, KERN-MEGA-PLAN 14.160) - exactly what
  the withdrawn KB DMA doors (`OSAPI_MEM_CLAIM_DMA` / `_DMA_HI`) gave such a
  package with CX != 0. `OSAPI_MEM_MOVABLE` on its selector unpins it (or
  pins it again), and both pool compactions keep the head page-safe
  (`mx_dmafit`); a regrow keeps the head (`mega_claimregrowf`). The pool
  counts in KB: the claim and the head are each rounded UP to whole KB there,
  and AX comes back as the paragraphs of the KB granted.
* **Anywhere else** - every other kernel, real mode, a driver, a package
  without OS88_HIGHMEM, a claim or head past 64KB, no pool room - the bit is
  ignored: the claim is the paragraph door's ordinary head claim,
  conventional and page-safe, as without it.
* **The head with the bit off is conventional** (the paragraph door's contract
  since it was made: a page-safe buffer a real-mode path names). Head 0 with
  the bit is no head and conventional, as `MEMP_CONV`. Never set both.
* The **KB arm** (`MEMP_KB + MEMP_KBH`, i.e. `MEMKB_DMA` / `MEMKB_DMA_HI`)
  sets it for you: a KB head is a pool head where it can be, as the doors.
  **Converting a `MEMKB_DMA*` site to paragraphs: OR `MEMP_DMAPOOL` into CX**
  - every one of them was a KB DMA door at the base. The one KB-arm site that
  must stay conventional is Video's PKG_MEGA `vp_rclaim` `.p` (the real-mode
  bracket reads the ring), and it is the paragraph door without the bit here.

## THE AUDIT - every claim site whose kern_mega placement or movability moved

Base `4beca096` against `kernel-size-19` at `7ba85e9f` (before this branch).
Read off the source of every converted site (memdoors' ~300, nogrow's KB arm,
swdrv's deletion) and the kernel paths behind each door, then proven on the
rows below. "Pool" = an above-1MB `mx_tab` block under a selector.

| site(s) | base (KB doors) | `7ba85e9f` | now |
|---|---|---|---|
| **every KB DMA door with a HEAD in an OS88_HIGHMEM package, claim and head <= 64KB** - Audio's look-ahead (`apengine.inc`, 32/32), WIN16 `w16mm.inc`'s ring, WireFleet `wfsound.inc` (13/13, inline), 1942 `pcm.inc`, Arcade `arcsnd.inc`, MIDIRack `mrout.inc`, PiXEL `pxpump.inc`, Recorder's ring, WaveRack `wrrec`/`wrxport`, XT-MP3 `x3eng.inc`, `os88type.inc`'s read buffer (Word, CWORD, FontView, Scribe) | pool, `MX_FDMA\|MX_FPIN` (born pinned), floor-up, head page-safe | **conventional**, head page-safe, MC_RLOC pinned | pool, as base (`MEMKB_DMA*` -> `MEMP_KBH` -> `MEMP_DMAPOOL`; WireFleet through the KB arm inline, same bytes) |
| the same claims **declared movable** (Audio's `ap_rdecl` between plays) | pool block unpinned (`mega_dmamovf`), both pool compactions keep the head page-safe (`mx_dmafit`) | a conventional MC_DMA claim (the heap's mover) | as base |
| the same claims **regrown** in the pool | new block carries the head (`mega_claimregrowf`) | n/a (never in the pool) | as base |
| **KB regrow of a POOL or HUGE claim past 1,023KB** (`MEMKB_REGROW`, the C SDK's `os88_mem_regrow`; `megargw` L3's bridge) | `osapi_mem_regrow_x` in KB | **the KB arm's `shl ax, 6` wraps**: regrown to a wrong small size, then a #GP walking the run (`megargw` CLOSED 0D) | KB straight to `osapi_mem_regrow_x` on kern_mega, as base |
| `MEMKB_DMA*` with CX = 0 (VBattle x5, ExciteBike x3, RealDMG x2, WIN16 `w16sel`/`win16.asm`, `os88xgap`, Video, megarmb*) | conventional (the "no page" idiom) | conventional | conventional |
| plain `MEMKB_CLAIM`/`_HI`, the C SDK's `os88_mem_claim`, inline `MEMP_CLAIM` CX = 0 (PiXEL GIF, Telnet, WIN16 loader x2, WireFleet FX, RAMDISK, XMEM, HDD `inst`) | pool for OS88_HIGHMEM <= 64KB (`mega_claimhf`), else conventional | the same (`.mpool`) | the same |
| drivers' head claims (SOUND.DRV ring and staging, HDD x3, inline) | conventional (a driver never takes the pool arm) | conventional | conventional |
| Video's PKG_MEGA `vp_rclaim` ring (paragraph door at base) | conventional, by design | conventional (KB arm) | conventional (paragraph door, no bit) |
| kernel callers: FILECP, the bridge's piece (`mmf_mem_claim_ownhi`), HUGE's conventional arm and the regrow's move (`mmf_mem_claim_own`), DISKOPS's AVAIL | conventional, DISK class | identical paths (`mem_claim_x.low` = the DMA door's `.go`) | unchanged |
| CPU vs DISK class | every KB door DISK | every MEMKB/inline site DISK (no `MEMP_CPU` added) | unchanged |

## TAKEN (a correctness fix of the pass's own regression, smallest form)

* **swdrv's deletion reverted, hunk by hunk** (its idioms kept): `MX_FDMA`,
  `mx_dmah`, `mega_claimdf`, `mega_dmahset`, `mega_dmamovf` and its call in
  `osapi_mem_movable_x`, `mx_alloc`'s floor-up `.bot` arm (KERN-MEGA-PLAN
  14.166, the owner's "no blockade" rule), `mx_dmafit` in both compactions,
  the regrow's head carry, `snd_ringhif`'s pool walk (the router admits a
  pinned DMA pool ring). SPEC.md 34, 50.3 and 50.6.10's paragraphs restored.
* **`MEMP_DMAPOOL`** (above): osapi_mem_p's kern_mega arm, 29 bytes `.cold`;
  `and si, 0x7FFF` -> `0x3FFF` (same size, every kernel); the KB arm ORs
  `MEMP_DMAPOOL` where it ORed `MEMP_CONV` (same size).
* **the KB arm's pool regrow** on kern_mega: 13 bytes `.cold`.
* **Video's PKG_MEGA `.p`** back on the paragraph door: +13 bytes in the
  kern_mega arm's VIDEO.O88 only (the shipped build has no `.p`).

| kernel | conventional | above 1MB | total |
|---|---:|---:|---:|
| kern_big / kern_small / kern_emu | 0 | - | **0** (two immediates differ, sizes identical; kern_big `cmp`-ed) |
| kern_mega | **0** (`.text` 45,418 both) | `.cold` +137 (48,303 -> 48,440), `.mega` +288 (24,416 -> 24,704) | **+425** |
| packages | | | Video (PKG_MEGA arm) +13; every other image the same size |

Against swdrv's -404: the deletion was .cold -95/.mega -304 plus idioms; the
restore is .cold +95/.mega +288, the arm +42.

## REFUSED / RECORDED

* **The KB arm does not refuse a KB count of 1,024 or more** on any kernel
  (nogrow recorded it): a `MEMKB_CLAIM` or a conventional `MEMKB_REGROW` of
  >= 1,024KB wraps to a small paragraph count and is GRANTED - a regrow can
  SHRINK a claim it meant to grow. The base doors refused. `cmp ax, 1024` /
  `cmc` / `jc` before the shift is +6 `.cold` a kernel; recorded for the
  coordinator (no shipped site asks that much conventionally; kern_mega's
  pool regrow, which can, is fixed above).
* **For `kbpara`**: converting a `MEMKB_DMA*` site with a head to paragraphs,
  OR `MEMP_DMAPOOL` into CX. WireFleet's ring is the KB arm inline
  (`MEMP_KB + MEMP_KBH + MEMP_CLAIM_HI`); if the arm goes, it is
  `mov cx, 13*64 | MEMP_DMAPOOL` (+1 byte against an image at APP_MAX_SIZE).
  `MEMKB_REGROW` of a pool claim: the paragraph `MEMP_REGROW` cannot express
  >= 1MB (the pool regrow takes KB): keep a KB path for huge regrows.

## ROWS RUN

`make` (fast tier 78 passed, 1 skipped, twice), `make small`, `make emu`,
`make mega`, the C packages (`make cword runcpm c64 apple2 paccman lemmings
weave loom`), `abilock --update` (no bump). Emulator, at this tip:
**megajw ok** (86Box mr286: the Audio Player's look-ahead a pinned
`MX_FDMA` pool block again; RED at `7ba85e9f`: "pool []"), **megargw ok**
(Bochs; RED at `7ba85e9f` identically: CLOSED MEGARGW 0D in L3), megaxmove
ok, megahuge ok, megahigh ok, heaphi ok.

## ROWS (for the final union)

kern_mega's pool and the router: megajw megargw megaxmove megahuge megahigh
heaphi megabrdg megabrdg86 megashed megaown megarmb* megapxhuge megapxhugez
megadosdir megahddtick megacdrom megahiber megahibfull pxsmega vidrmbems
midirack/waverackrec on kern_mega, megasbhold megasbgap megasndup
megasndword megarsq; the KB arm: memp regrowshed heapgran ctoolchain; Video's
arm: megavidring megavidbig megavidtier megavidv2; WireFleet: wirefleetsystem.
