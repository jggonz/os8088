# Kernel size pass 19 - `kbpara`: paragraphs, the one unit

Branch `ksp19-kbpara`, cut from `kernel-size-19` at `f49e64a3`. The owner's
question: *"Why do packages need to call for KB at all? Can they be reworked
to call in paragraphs?"* Yes: every package, driver, test and C SDK site now
calls `OSAPI_MEM_P` in paragraphs, and the KB bridge C1 left behind is gone -
`OS88KB_BODIES`, the `MEMKB_*` macros, the kernel's `MEMP_KB`/`MEMP_KBH` arm
(`mem_p_kb`) and the SPEC.md subsection that described them.

## The contract (SPEC.md 50.3.7, `apps/os88api.inc`)

* `MEMP_CLAIM` / `MEMP_CLAIM_HI` (| `MEMP_CPU`): AX = paragraphs. **CX is not
  read.** A plain claim's site is `mov bl, verb` and the call - nothing else.
* **`MEMP_CONV` is now a VERB bit (80h)**, not a CX value (was `CX = 8000h`):
  with it CX is the page-safe head in paragraphs (0 = none) and the claim
  stays conventional on kern_mega. The kernel tests it by the verb byte's sign
  (`or bl, bl`), and the AVAIL dispatch is a signed compare (`jge`).
* `MEMP_AVAIL` **keeps CX** (it no longer answers the granule; the only reader
  was the C SDK's `os88_mem_gran`, which no C package called - withdrawn).
* `MEMP_REGROW` unchanged. Every claim keeps BX except BL, which is the verb:
  a site whose BX is live banks it (45 sites).

## TAKEN - bytes

`tools/kernsize.py --json`, resident = text+bss+cold+lowbss+vgabuf+vga(+mega),
against `f49e64a3` built in this tree:

| kernel | base | tip | delta |
|---|---:|---:|---:|
| kern_big | 90,081 | 90,031 | **-50** (`.cold`) |
| kern_small | 56,257 | 56,207 | **-50** |
| kern_emu | 90,327 | 90,277 | **-50** |
| kern_mega | 129,422 | 129,371 | **-51** (`.cold`, above 1MB) |

The arm (`mem_p_kb`, 50/51), the 3-byte `mov cx, MEM_GRAN`, kern_mega's
`jcxz` folded into the sign test, the head's `and si, 7FFFh` gone for an
`xor si,si / or bl,bl / jns / mov si,cx` (+2) and one parity pad `nop`
(t_hotpar: dsk_xfer, dsk_runcap, dsk_next_clus_x keep their parity on both
kernels). `mmf_osapi_mem_avail` stays (DISKOPS, compress, kern_mega).

**Packages** (scratchpad `pkgsum.py`, every `.o88`/`.drv` image unwrapped,
parted packages image + parts): **+99 bytes over the 98 top-level images**
(4,052,384 -> 4,052,483), +157 over all 170 images (the small/mega copies).
PRE-T5: a direct site is `mov bl` (2) + a 5-byte far call where a `MEMKB_*`
site was a 3-byte near call over a per-package body. Under T5 every
`call OSAPI_MEM_P` is a 3-byte door site: the source adds 252
`call OSAPI_MEM_P` lines and drops ~76 bodies' (one a package), so T5 takes
about 2 x 176 = **~350 more bytes off this branch than off the base**, which
puts the package side at **about -200 to -250** once T5 is in, with the
kernel's -50 x 4. (Measured pre-T5; the T5 figure is arithmetic - re-measure
at the merge.) Largest growths pre-T5: RealDMG +62, Arcade +40, WaveRack +33,
SysCheck +26, PiXEL +25; largest savings: Apple ][ -23, C64 -21, Paint -20
(`pt_kb_of` deleted), Tracker -14, Browser -12, RunCPM -12.

### Sites converted

~298 `MEMKB_*` sites in 122 files, plus every existing paragraph site:

* **Constant KB** -> `KB * 64` (free), or the exact byte count `(n + 15) / 16`
  where a constant was a byte count rounded to KB (RealDMG's frame, Video's
  ring, PiXEL's view, Recorder's buffer, Tank's template).
* **From bytes**: Paint's whole claim set (it already counted paragraphs and
  converted to KB for the door: `pt_kb_of` and six `shl 6`s gone), Artful's
  undo arena, Browser's copy, Arcade's image, WIN16's GlobalDOSAlloc,
  ExciteBike's part fetch, VBattle's band/cover/profile, the parts' `op_self`
  and `op_hilift`, XTMP3's head buffer, DOS's cluster buffers and DVW regrow,
  RealDMG's bounce, WaveRack's units and output.
* **Packages that PLAN in KB keep KB at one helper of their own** (shift
  in, or `>> 6` out): DOS `dos_avkb` (arena cap, `MEMC_WHATIF` is KB), RealDMG
  `rw_avkb` (cartridge plan), VBattle `vb_avkb`/`vb_kbconv` (rings vs
  `MEMC_WHATIF`), Browser `br_kbp` + `br_avail` (MEMH_AVAIL is KB), Video
  `vp_mkb`/`vp_mavail`, PiXEL `px_avail`/`pe_avail` (MEMH_AVAIL), XTMP3
  `x3_avkb` (X3S_PEAK). Display-only KB: isoTron, RedLine, Sheet's
  `GET.SYSTEM`, XTMP3's report, the tests' report fields.
* **C SDK**: `os88_mem_claim(kb)`/`os88_mem_regrow(kb)` shift in the thunk;
  the `_kb` avail answers shift MEMP_AVAIL's fallback; `os88_mem_claim_p`
  lost its `xor cx, cx`; `os88_mem_gran` withdrawn. CWORD, LOOM, Apple ][,
  C64 and RunCPM call `os88_mem_claim_p(KB * 64)` (their KB thunk gone where
  that was their last KB call); Lemmings and Weave keep the KB thunk (runtime
  KB sizes). RunCPM keeps `os88_mem_regrow(kb)` for its file claims: they
  grow toward a megabyte and `kb << 6` overflows 16 bits (see WANTS THE
  OWNER). Host stubs taught: a2uitest, c64uitest, rcuitest, cwuitest.
* **Drivers**: RAMDISK's avail (KB for the store's gate), HDD.DRV's bounce
  (`mov bl, MEMP_CONV` for `mov bl, ah`, same size; the probe's `mov cx, 1`
  is no longer a head - unneeded, one sector cannot split), RTL8139's ring.
  HDD.DRV stays inside its 4,096.
* **`xor cx, cx` dropped** before 32 plain claims whose CX the liveness pass
  proves dead (-64 source bytes; HDD.DRV's left alone).
* **Tests** (`tests/`): heapfrag, megaown and megargw keep their KB plans
  through local helpers (`hf_kb`/`hf_kav`, `mo_*`, `mr_kb`); filler counts in
  paragraphs (`tests/midirack.py` reads `fl_run // 64`); ExciteBike's
  `xb_sprkb` is paragraphs (`tests/excitebike_video.py` taught); megarmbf's
  register check keeps its `FF01` answer (`mov al, 1`: AX is MEM_P's output).

### Method

A heuristic interprocedural liveness pass (scratchpad `kbpara/live2.py`)
classified BL, AX and CX after every site (the first version skipped a
fallthrough behind a `jcc` to a `ret` - fixed, and every site it had passed
re-checked); then every flagged site was read by hand. Banks only where BX is
live or a routine's contract promises BX.

## REFUSED / RECORDED

* **The verb in another register** (the coordinator's prompt: a register that
  needs no BX bank). DX is an output of every claim and of AVAIL, so the verb
  could ride in DL there for free - but REGROW takes the claim in DX, and on
  kern_mega a claim is a selector that can be numerically small, so the
  kernel could not tell a verb from a claim. Moving REGROW's claim to CX costs
  CX at its 36 sites. The 45 banks are 90 bytes; recorded, not taken.
* **Per-package KB helpers everywhere** (smaller than direct sites pre-T5 for a
  package with 5+ claim sites) - refused: under T5 the direct site is 5
  bytes, and the owner's question was to call in paragraphs.

## WANTS THE OWNER / `megacmp`

* **kern_mega placement.** Every site lands where it landed at `f49e64a3`
  (C1's form), NOT always where it landed at `4beca096`: a converted
  `MEMKB_DMA`/`_DMA_HI` site with a NON-ZERO head is `MEMP_CONV` with that
  head (conventional), where the base's KB DMA door gave an OS88_HIGHMEM
  package a pool block. `megacmp`'s `MEMP_DMAPOOL` (a CX bit beside the head)
  fits this contract as: verb `MEMP_CONV` (+ `_HI`), CX = head |
  `MEMP_DMAPOOL`; the kernel must mask the bit off SI (`osapi_mem_p`'s head is
  `mov si, cx` now). Sites to OR it into (all head = the whole claim unless
  noted): 1942 `pcm.inc` (13KB), Arcade `ar_sb_open`, Audio
  `ap_ensure_bufs` (32KB), MIDIRack `mrb_start`, `os88type.inc` `ty_open`,
  PiXEL `px_dmaclaim` (both callers), Recorder `rc_do_rec`, WaveRack
  `wc_claims` and `wt_claims`, WIN16 `w16mm_wimake`, XTMP3's external ring,
  and the tests heapfrag (`hf_kb` with a head), megarsq, sbtest, sndcap,
  sndfmt, viddisk (`VK_BUFKB`), vidsnd (two). NOT Video's `.p` (the real-mode
  bracket reads that ring). Head-0 DMA sites (VBattle, ExciteBike, RealDMG,
  WIN16 `w16sel`/`win16.asm`, `os88xgap`, megarmb*) are `MEMP_CONV` with CX
  = 0: conventional, as at the base. Plain claims (`MEMP_CLAIM`, no CONV) are
  pool-eligible, as the KB CLAIM door was.
* **A claim of 1,024KB or more cannot be said in paragraphs** (AX is 16
  bits). C1's KB arm shifted it unchecked too. RunCPM's file regrow
  (`rcfs.c`) can grow toward a megabyte through `os88_mem_regrow(kb)`, whose
  thunk now shifts: at 1,024KB it wraps. A huge claim's regrow wants a KB (or
  a 32-bit) form of its own - `OSAPI_MEM_HUGE`'s, not this cell's.

## ROWS (for the final union)

memdoors' and nogrow's KB-arm rows: memp regrowshed heapgran drvmem pins
heapcheck heapmap heapdrv msegnomem clipgrow dosmem os88huge megahigh
megahuge megapxhuge megarmbref heaphi dirwshed ddsmall megamod mseglzslack
trkcompact trkbigmod cmemmove isotronsmall pxanimemu w16cens w16xsec
stiknote stiknotevga stiknotesmall skies waverackrec waverackmega rdpreserve
vidrmbems pxsmega recorder fontview ctoolchain; and the packages whose
arithmetic moved: paint* (pt_*, `paintgrow`, `paintload`), artful undo,
browser (brcapacity*, brmega), dos* (dosram, doswgrow, doslnk, dosopen,
dosnet), realdmg* (rdglend), vbattle*, video* (vidxdisp, viddisk, vidbench),
pixel* (pxdecode, pxsave), pxstein, tank*, tracker, texpad, xtmp3eng,
x3xtunit, midirack (sbmem), syscheck, redline*, sheet macro, scribe/word
save, arcade*, excitebike_video, fractal (dispfrac), frotz, isotron*,
win16 (w16mm, GlobalDOSAlloc rows), stiknote, skies, wirefleet, hdd*
(hddgeom, megahddtick), rtl8139/pciether, filler-driven rows, megaown,
megargw, megaflt, megajw, heapfrag, megarmb*, megacp, megahg, megabrdg,
sbtest, sndcap, sndfmt, the C packages' rows (c64*, apple2*, runcpm*,
cword*, loom*, lemmings*, weave*), and api-abi mirror hotpar.

## Checks

`make` (fast tier: 78 passed, 1 skipped), `make small emu mega`, the C
packages (`make cword runcpm c64 apple2 paccman lemmings weave loom`) - green;
`abilock --update` without a bump; `os88cells` matching. Emulator: `memp`
**ok** (its check 1 now asserts MEMP_AVAIL keeps CX instead of the
withdrawn granule answer), and one boot to look: Paint, Artful, Fractal and
the Audio Player opened together off `apps360.img` on MartyPC.
