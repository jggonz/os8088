# Kernel size pass 19 - `mcmerge`: megacmp merged onto kbpara's paragraphs

Branch `ksp19-mcmerge`, cut from `kernel-size-19` at `a0a91459` (T5's door,
kbpara's paragraph claims with the KB arm and `OS88KB_BODIES` deleted, wmrec
merged); merges `ksp19-megacmp` (`7f7ebc06`, based on `7ba85e9f`, before T5
and kbpara). The owner's words: *"we JUST did a bunch of work to get mega able
to fully compact. Make sure we didn't break that work."* The aim: kern_mega
places, pins, compacts and regrows every claim exactly as at the pre-pass base
`4beca096`.

## How it merged

* **Kept from megacmp, as it was**: the pool DMA-head machinery in
  `kernel/mega.inc` (`MX_FDMA`, `mx_dmah`, `mega_claimdf`, `mega_dmahset`,
  `mega_dmamovf`, `mx_alloc`'s floor-up `.bot` arm, `mx_dmafit` in both
  compactions and `mx_packupf`, the regrow's head carry), its call in
  `osapi_mem_movable_x`, `snd_ringhif`'s pool walk (`kernel/snd.inc`), and
  SPEC.md 34 / 50.3 / 50.6.10's restored paragraphs. These merged clean.
* **Dropped from megacmp**: everything about the KB arm (`mem_p_kb`, the
  `MEMKB_*` documentation and its SPEC.md subsection), which kbpara deleted, and its
  WireFleet and Video edits (both written against the KB arm).
* `docs/INDEX.md`, `tools/abi.lock.json`: regenerated (`os88index`,
  `abilock --update "Merge of megacmp (kernel size pass 19)"`, no bump: PKG_FMT
  13, DRV_VER 14). `os88cells --write`: unchanged (95 + 1 door cells).

## MEMP_DMAPOOL - a VERB bit, 40h (the smallest form)

megacmp made it bit 14 of CX beside the head (`CX = head | 4000h`), when the
head was read on every claim. kbpara then made `MEMP_CONV` a verb bit (80h)
with CX = the head read only under it. Three forms were possible:

| form | kern_big/small/emu | kern_mega | a site |
|---|---:|---:|---:|
| CX bit 14 (kbpara's suggestion), kernel masks it off the head | +4 each (`and si, 3FFFh` where it is `mov si, cx` now) | +4 and the arm | +0 constant head, **+3** a computed head (`or ch, 40h`: os88type, PiXEL, WaveRack x2, WireFleet's `mov cx, ax` +1 at APP_MAX_SIZE) |
| **verb bit 40h, with `MEMP_CONV`** (taken) | **0** (no path reads it: `cmp bl, MEMP_AVAIL` is signed, `and di, 1` and `test bl, MEMP_CPU` ignore it) | the arm only | **0** (the verb is an immediate every site writes anyway) |

So `MEMP_DMAPOOL equ 40h`: `mov bl, MEMP_CONV + MEMP_DMAPOOL [+ MEMP_CLAIM_HI]`,
CX = the head in paragraphs. On kern_mega `osapi_mem_p`'s arm sends a
`MEMP_CONV` verb with the bit and a non-zero head to `mega_claimdf` (claim and
head in KB, rounded up, as the pool counts); refused, or without the bit, or
head 0, it is the conventional head claim it always was. **WireFleet: 0
bytes** (`wfsound.inc`'s verb immediate), so it stays inside APP_MAX_SIZE.

## Huge regrow past 1,023KB - `OSAPI_MEM_HUGE` BX = `MEMH_REGROW` (4)

Paragraphs cannot say 1,024KB in sixteen bits, and megargw's L3 regrows a
bridge past that (at `7ba85e9f` the KB arm's `shl ax, 6` wrapped: #GP, CLOSED
0D; at `a0a91459` the test's own `mr_kb` shift wraps the same way - not run
there). Taken:
`OSAPI_MEM_HUGE` with **BX = `MEMH_REGROW`** (= `MEMP_REGROW`, 4, so BL is
either cell's verb), DX = the claim, AX = KB: on kern_mega a claim above 1MB
(`mem_ishidx`: a run or a bridge) goes straight to `osapi_mem_regrow_x`, the
pool's KB regrow - the base's `OSAPI_MEM_REGROW` route - and anything else is
CF = 1. **13 bytes `.cold`, kern_mega only** (`cmp bx, 4` / `jne` /
`call mem_ishidx` / `jc .bret` / `jmp osapi_mem_regrow_x`). Every other
kernel's body is the claim, which refuses AX >= 1,024 before its shift
(`mem_claim_x`), so the verb is only ever asked that size and is refused
there, where no claim can be that big. Below 1,024KB `MEMP_REGROW` serves every
claim on every kernel, as before.

* `tests/megargw`: L3's three regrows past 1,023KB call `OSAPI_MEM_HUGE`
  with `MEMH_REGROW`; `mr_kb` deleted (no other caller). The third, "below
  the cut" (windows 0..P-2), kbpara had converted to `(P-1) << 12`
  paragraphs, which wraps for any P past 16 - **it ran RED on the merge
  until converted** (a regrow to the wrapped size: "a window below 1MB"),
  the same defect in the test that the KB arm had in the kernel.
* **Browser** `br_regrow` (the source and document claims, a huge one-run
  claim on kern_mega that grows in 4KB steps from 1KB while `br_memroom`
  sees room, which `MEMH_AVAIL` counts in megabytes there): a target of
  1,024KB or more takes `MEMH_REGROW`. kbpara's `br_kbp` shift would wrap
  it: 1,021KB grown to 1,025KB asks for 64 paragraphs - a SHRINK to 1KB the
  Browser then writes 1,025KB into. At the base the KB regrow door took it.
  Off kern_mega the claim refuses AX >= 1,024, as the base door did (and
  `BR_DOCMAX` caps the document at 1,023KB anyway). +13 bytes in
  BROWSER.O88. Audited for the same shape: every other `MEMP_REGROW` site
  in a package that holds an `OSAPI_MEM_HUGE` claim (Tracker's `trk_trim`,
  960KB-guarded; XT-MP3's `x3_shrink`, a part; `tests/megahg`, 130-400KB) -
  none reaches 1,024KB.
* **C SDK** `os88_mem_regrow(seg, kb)`: kb >= 1,024 takes `MEMH_REGROW`, below
  it the paragraph regrow as before - RunCPM's file claims (`rcfs.c`), which
  grow toward a megabyte, regrow as at the base (kbpara's WANTS THE OWNER
  item, closed). +10 bytes in RUNCPM.O88, the one C package that calls it.
  `os88.h` says so.

## Bytes and a byte back

`osapi_mem_p`'s `.mreg` was restructured to share one `push ax` / `call .kb`
across its pool and conventional-run arms and to rejoin `.mpool`'s tail
(`.mpc`) - **-14 bytes `.cold`**, which pays for most of the DMAPOOL arm
(+25: `test bl, 40h` / `jz` / `jcxz` / two `.kb`s / `mega_claimdf`).

`tools/kernsize.py --json`, resident = text+bss+cold+lowbss+vgabuf+vga+mega,
`a0a91459` built in this tree against the merge:

| kernel | `a0a91459` | merged | delta |
|---|---:|---:|---:|
| kern_big | 89,618 | 89,618 | **0** |
| kern_small | 55,821 | 55,821 | **0** |
| kern_emu | 89,864 | 89,864 | **0** |
| kern_mega | 128,541 | 128,945 | **+404** (`.cold` 48,199 -> 48,315 +116, `.mega` 24,400 -> 24,688 +288) |
| kern_mega conventional | `.text` 45,079 `.bss` 4,927 `.lowbss` 5,600 `.vgabuf` 336; `mres` 61,440 | the same | **0** |

kern_mega's +404 is all above 1MB: megacmp's restoration of swdrv's deletion
(`.cold` +95, `.mega` +288, as megacmp measured), the DMAPOOL arm (+25) and
the huge regrow (+13), less the `.mreg` share (-14). megacmp's own figure was
+425; the verb-bit form is 21 bytes smaller than its CX-bit arm and the KB
arm's pool regrow together.

**Packages** (kbpara's pkgsum, the 170 images `make` and the C packages
build, parts included; the test images are not among them): 6,801,338 ->
**6,801,361, +23** - RUNCPM.O88 +10 (the thunk) and BROWSER.O88 +13
(`br_regrow`). Every other image is the same size: the twelve DMA sites and
WireFleet change a verb immediate. The
shipped VIDEO.O88 and its PKG_MEGA arm are unchanged (the arm's `.p` was
already the paragraph door without the bit at `a0a91459`).

This restores the base's behaviour, which the pass's own C1/swdrv had broken;
it is a correctness fix of the pass's regression in its smallest form, not a
row of new growth (BRIEF 0.0). `t_hotpar` green on both kernels, no re-pad (kern_big and kern_small's
`.cold` did not move a byte).

## Placement at the end state against `4beca096`

kern_mega in protected mode; "pool" = an above-1MB `mx_tab` block under a
selector. Base column: the KB doors' answer.

| site(s) | `4beca096` | `a0a91459` (before) | merged |
|---|---|---|---|
| **headed DMA claims** - Audio `ap_ensure_bufs` (32/32KB), WIN16 `w16mm_wimake`, WireFleet `wf_pcm_open` (13/13), 1942 `pcm.inc`, Arcade `ar_sb_open`, MIDIRack `mrb_start`, PiXEL `px_dmaclaim` (both callers), Recorder `rc_do_rec`, WaveRack `wc_claims` / `wt_claims`, XT-MP3's external ring, `os88type.inc`'s `ty_open` (Word, CWORD, FontView, Scribe) | pool, `MX_FDMA\|MX_FPIN`, floor-up, head page-safe (an OS88_HIGHMEM package's; else conventional) | **conventional** head claim | **pool, as base** (`MEMP_CONV + MEMP_DMAPOOL`) |
| the same, declared movable (Audio's `ap_rdecl`) | unpinned pool block, compactions keep the head page-safe | a conventional MC_DMA claim | as base (`mega_dmamovf`, `mx_dmafit`) |
| the same, regrown in the pool | new block carries the head | n/a | as base |
| the same tests: heapfrag (`hf_kb` with a head), megarsq, sbtest, sndcap, sndfmt, viddisk, vidsnd x2 | KB DMA door with a head | conventional | as base (the bit) |
| huge / pool / bridge regrow >= 1,024KB (megargw L3, RunCPM's files, the Browser's source) | `OSAPI_MEM_REGROW` in KB, pool route | wraps at the shift (wrong size, a shrink) | as base (`MEMH_REGROW`) |
| huge / pool regrow < 1,024KB (`MEMP_REGROW`) | pool KB regrow / `mega_hugecrgf` / heap | the same in paragraphs | the same |
| head-0 DMA claims (`MEMP_CONV`, CX = 0): VBattle x5, ExciteBike x3, RealDMG x2, WIN16 `w16sel`/`win16.asm`, `os88xgap`, Video's `vp_mclaim`/`vp_mclhi`/`vp_rbh`, megarmb* | conventional | conventional | conventional |
| Video's PKG_MEGA `vp_rclaim` `.p` ring (real-mode bracket reads it) | conventional (paragraph door) | conventional (`MEMP_CONV`, head) | conventional (no bit) |
| plain claims (`MEMP_CLAIM`/`_HI`, the C SDK's `os88_mem_claim`) | pool for OS88_HIGHMEM <= 64KB, else conventional | the same | the same |
| `MEMP_CONV` no-head claims (parts' carve, fetch, XMS staging; crt0's overlay; C64/Apple ][ code claims; SysCheck; WaveRack's I/O) | conventional | conventional | conventional |
| drivers' head claims (SOUND.DRV ring and staging, HDD, RTL8139) | conventional (never the pool) | conventional | conventional (`mega_claimhf.chk`: a head is never a driver's) |
| kernel callers (FILECP, the bridge piece, HUGE's conventional arm, DISKOPS) | conventional, DISK | unchanged | unchanged |
| the external-ring router (`snd_ringhif`) | admits a pinned `MX_FDMA` pool ring | refuses every pool ring | as base |

## Checks and rows run

`make` (fast tier 78 passed, 1 skipped), `make small emu mega`, the eight C
packages, `t_hotpar` (24 / 16 routines at parity), `t_api_abi --all` (3,341
checks), `os88cells --write` unchanged, `abilock` no bump, `checkdocs`.

Emulator, at this tip (`os88test.py soak -k ...`): **megajw ok** (86Box
mr286, megacmp's 86Box build symlinked: the Audio Player's look-ahead a
pinned `MX_FDMA` pool block - the row that was RED at `7ba85e9f`),
**megargw ok** (Bochs; RED on the first pass of this merge at L3's "below
the cut", fixed in the test as above), **megaxmove ok**, **megahuge ok**,
**megahigh ok**, **heaphi ok**, **memp ok**, **brmega ok** (the Browser's
regrow change). **brcapacity FAILS - not this merge's**: it fails
identically with `a0a91459`'s Browser image put back (garbage `source=`
figures read back off kern_big, whose sources this merge moved only under
`KERN_MEGA`); for the coordinator.

## ROWS (for the final union)

megacmp's list stands: megajw megargw megargw86 megaxmove megahuge megahigh
heaphi megabrdg megabrdg86 megashed megaown megarmb* megapxhuge megapxhugez
megadosdir megahddtick megacdrom megahiber megahibfull pxsmega vidrmbems
midirack waverackrec waverackmega megasbhold megasbgap megasndup
megasndword megarsq memp regrowshed heapgran ctoolchain megavidring
megavidbig megavidtier megavidv2 wirefleetsystem; and for the regrow thunk
the RunCPM rows (runcpm*), the Browser's (brmega brhuge brmem brcapacity),
and sbtest sndcap sndfmt heapfrag vidsnd viddisk.
