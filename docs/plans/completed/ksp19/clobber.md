# Kernel size pass 19 - `clobber`: every converted OSAPI_MEM_P site, read for clobbers

Branch `ksp19-clobber`, cut from `kernel-size-19` at `ac92ac14` (the final
soak's 102 failures fixed). The risk: ~300 package, driver, test and C SDK
sites moved off the withdrawn KB doors (`OSAPI_MEM_CLAIM`, `_CLAIM_HI`,
`_CLAIM_DMA`, `_CLAIM_DMA_HI`, `OSAPI_MEM_AVAIL`, `OSAPI_MEM_REGROW`), which
kept every register but DX (and AVAIL's AX/BX), onto `OSAPI_MEM_P`
(SPEC.md 50.3.7). The new sequence writes **BL** (the verb), often **CL**
(a KB shift) and **CX** (a head), and MEM_P answers **AX = the paragraphs
GRANTED** where the old claim and regrow kept AX = the KB asked for. The
final soak found two such clobbers (PiXEL's CL, ExciteBike's BX), both read
back ACROSS a call, which kbpara's liveness pass could not follow.

## Method

`audit.py` (session scratchpad, `clobber/`): for every `call OSAPI_MEM_P` on
a `+` line of `git diff 4beca096..HEAD` over `apps/ drivers/ tests/ kernel/
kerndos/` (269 sites), the registers the NEW sequence leaves different from
the OLD hunk - BX always (not after an old AVAIL, where BX was an output
anyway), AX for CLAIM/REGROW, anything the new pre-sequence writes that the
old did not, and anything a new unit shift writes AFTER the call - followed
forward through jumps, INTO near callees (a callee that pushes the register
and READS it is a read: the PiXEL shape, which kbpara's `live2.py` treated
as "preserved" - the one-line bug that hid it), past OSAPI cells by their
contract, through low/high-half writes, and on `ret` into every caller of the
routine (`jmp` callers as tail calls). **Validated on the tree before the two
fixes** (`dd1e6e5c^`): it reports PiXEL's `px_claims` CX LIVE at
`px_wkp`'s `mov [px_tscl], cl` and ExciteBike's `xb_partget` BX LIVE - both.

Every site the tool could not prove dead (89 BX/CX/other, 124 AX, 42 AVAIL
output sites) was then read by hand, following the callers. The table below
has one row per site; "tool" means the automatic proof, every other note is
the hand reading. Also read: the 63 hunks that replaced a door call with a
package helper (`br_kbp`, `dos_avkb`, `rw_avkb`, `vb_avkb`/`vb_kbconv`,
`vp_mkb`/`vp_mavail`, `x3_avkb`, the tests' `hf_*`/`mo_*`) - their MEM_P
call is a row and its liveness runs through RET into every helper caller;
the 34 pre-existing MEM_P sites whose `xor cx, cx` kbpara DROPPED (the
inverse: code after the claim reading a CX it used to see zeroed); the
`MEMP_CONV` sites' head units (every head is paragraphs, x64 or a byte
count >> 4); routine headers that promise preservation; and the kernel's own
converted callers.

## LIVE clobbers found, and fixed

| site | what | fix | bytes |
|---|---|---|---|
| `tests/megahi/megahi.asm:112`, `:165` | `OSAPI_MEM_HUGE` READS BX (8000h/4000h windows, and since `mcmerge` BX = 4 is `MEMH_REGROW`). The converted REGROW at :88 leaves BL = `MEMP_REGROW` = 4, so the 200KB claim was taken as a REGROW of DX - the claim just freed - and refused on kern_mega. **Seen on Bochs** (`tools/os88bochs.py sweep`): at `ac92ac14` MEGAHI prints `regrow 1084 10AC ok` then `MEGAHI: no 30`; fixed, `huge 10A4 step 0008 ok` and through to `MEGAHI: ok`; :165 takes whatever the first HUGE answered in BX (a bridge's windows) | `xor bx, bx` before each | +4 (a test package) |
| `tests/megahg/megahg.asm:177` | the same cell after `OSAPI_XMEM_CAPS`, whose BL is the free entry count: 4 entries and BH = 0 is `MEMH_REGROW`. Not a converted site - `mcmerge`'s new verb made the existing BX read meaningful - found by the HUGE-input sweep below | `xor bx, bx` | +2 (test package) |

**No live clobber in shipped code**, beyond the two `fix` already took
(PiXEL `dd1e6e5c`, ExciteBike `95f46e28`). MEGAHI and MEGAHG are manual
Bochs sweeps with no suite row (`grep megahi tests/suite.py` finds only
`megahigh`, a host gate). MEGAHI was swept on Bochs before and after (above);
MEGAHG was assembled, not swept (a 4MB pool run, and its fixed read takes
BX = 0, the base's meaning).

## Contracts the conversion made false (comments, 0 bytes)

Routines whose header promised registers the claim inside now clobbers. Each
is DEAD at every caller (rows below); the header is corrected so the next
caller does not trust it:

* `pt_stagefit` (paint.asm) - "all registers preserved": BX now
  (pt_load banks it).
* `pt_clip_need` (paint.asm) - "preserves all registers": BX when it claims.
* `sc_scget` (scribe/scutil.inc), `wd_scget` (word/wdutil.inc) - "Preserves
  all registers": BX (every caller rewrites BX; sc_redraw/wd_redraw read none).
* `tw_scmem` (tracker/trkwin.inc) - "every register but AL": and BX (tw_fire
  banks it round the action).
* `op_hilift` (os88partsbody.inc) - "clobbers: CX, DX, DI": and BX (its one
  caller, op_fetch, clobbers BX by contract already).
* `dos_hk_claim` (dos.asm) - "in: AX = KB": PARAGRAPHS since kbpara.

## OSAPI_MEM_HUGE's BX, swept

Every `call OSAPI_MEM_HUGE` in the tree was checked for a deliberate BX
(0, 8000h, 4000h, `MEMH_AVAIL`, `MEMH_REGROW`, `HUGE_NOBRIDGE`): all but the
three above set it within a dozen lines. Left as they are: MEGAHG's
`hg_take` callers pass BX = a sixth of the pool's KB (4 only for a 24-29KB
pool) and the stage codes `mov bl, 0x31/0x52` with BH from a step of 8 or
1000h - never 4, 4000h or 8000h on any machine the row targets.

## The kernel's converted callers

* `mmf_mem_claim_own` / `mmf_mem_claim_ownhi` - bank BX and CX; the bridge
  (`mega_hugebf`) and the regrow move (`osapi_mem_regrow_x .move`, which
  needs CX kept) read both after: correct. HUGE's `.conv` banks AX itself.
* FILECP `fcp_claim` - `mem_claimp_dma_x`/`mem_claimp_x` keep AX (the
  paragraphs, x16 = the chunk; 64KB wraps to 0 and is handled); CL's shift
  precedes `mov cx, ax`.
* `kdgate` - `mem_claimp_x` keeps AX; the deleted KB entry was the one that
  spent CL.
* DISKOPS `clo_plan`, compress, kern_mega's bridge - far-call
  `mmf_osapi_mem_avail`, the withdrawn cell's own body: KB, AX/BX out as
  before, CX/DX/DI banked by the caller.
* `osapi_mem_p` itself - BX/SI/DI pushed round the conventional door, CX
  pushed round every shift, AVAIL's walk banks CX/DX/SI/DI: the contract
  ("every register not named is kept, CX included") holds on every arm read.

## Inverse: outputs read with the old meaning

* **AX after CLAIM/REGROW** (old: the KB asked; new: the paragraphs granted,
  a multiple of the 32-paragraph granule, the KB that covers it from the
  kern_mega pool). Every read was traced: the ones that store it store
  paragraphs (`at_akb`, `pt_smaxp`, `pt_cbparas`, `dk_cpara`), the ones that
  want KB shift it (`tk_shkb`, RealDMG's replay rebuilds `mov al, 2`), and
  the refusal compares (WaveRack's `wc_claims`/`wt_claims`) compare against
  a request that is already a multiple of 64. Chart's staging `sub ax, 64`
  relies on grant = request, which a multiple of 64 guarantees on both heaps.
* **AVAIL's AX/BX** (KB -> paragraphs): all 42 converted readers shift >> 6
  where they plan or show KB, or compare against `x 64` constants.
* **Refusal AX** is the request rounded to the granule - read only by the
  two WaveRack retry ladders above.

## Dropped `xor cx, cx` (kbpara), re-proved

34 pre-existing MEM_P sites lost an `xor cx, cx`: after them CX is the
caller's value, not 0. All 34 re-run through the fixed tool, the five it
could not close (`dkwid`, `thewire`, `hfidx`, `x3eng:1390`, C's
`os88_mem_claim_p`) read by hand: CX is rewritten before any read, or is C
scratch. Dead.

## Checks

`make` (fast tier: 78 passed, 1 skipped), `make small emu mega`, the C
packages (`cword runcpm c64 apple2 paccman lemmings weave loom`), and the two
test packages (`build/megahi.o88`, `build/megahg.o88`). Shipped bytes: the
change to shipped sources is comments, and `md5sum` of all 259 top-level
`build/` artefacts (`.o88`, `.drv`, `.bin`, `.img`) with and without it, at
one commit, is identical. No soak row: no shipped artefact moved, and the two
fixed packages have none; MEGAHI's Bochs sweep is the evidence for its fix.

## Site table

Old door -> new verb; the registers the new sequence leaves different
(`postCX` = a shift after the call); the verdict; the evidence for every
register that was not proved dead by the tool.

| site | door -> verb | regs | verdict | evidence |
|---|---|---|---|---|
| apps/1942/1942.asm:42 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| apps/1942/front.inc:74 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/1942/pcm.inc:20 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | BX: n_pushall bracket; n_popall restores |
| apps/arcade/arcade.asm:505 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/arcade/arcface.inc:209 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/arcade/arcimp.inc:255 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: BX was 0 before (xor bx) and BL=MEMP_CLAIM=0: same value; callers do not read it; CX: ar_zread callers: ar_zswap banks CX, .count/.say write it |
| apps/arcade/arcimp.inc:658 | CLAIM -> CLAIM | AX, BX | dead | BX: .lset/.make reload BL each pass |
| apps/arcade/arcpm.inc:225 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: BD_ON dispatcher re-pops BX after the call |
| apps/arcade/arcpm.inc:274 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/arcade/arcqb.inc:67 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: BD_ON dispatcher re-pops BX after the call |
| apps/arcade/arcqb.inc:83 | CLAIM -> CLAIM | AX, BX | dead | BX: BD_ON dispatcher re-pops BX after the call |
| apps/arcade/arcqb.inc:95 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: BD_ON dispatcher re-pops BX after the call |
| apps/arcade/arcqb.inc:257 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/arcade/arcqb.inc:263 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/arcade/arcsnd.inc:135 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead (AVAIL outputs) | AX: by hand: AL/AX written first, or only pushed/printed; BX: BL was SND_CAPS output already; callers reload [ar_bd] |
| apps/artful/atedit.inc:1609 | CLAIM -> CLAIM | AX, BX | dead | AX: [at_akb] is paragraphs (artful.asm:1404), compared in paragraphs |
| apps/artful/atedit.inc:1663 | REGROW -> REGROW | AX, BX | dead | AX: [at_akb] is paragraphs |
| apps/audio/apengine.inc:125 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/audio/apengine.inc:128 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/audio/apspk.inc:261 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/browser/brcopyselection.inc:67 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/browser/browser.asm:5524 | new helper -> ? | AX, BX, CX | dead | written first on every path (tool) |
| apps/browser/browser.asm:6451 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/cc/os88thunk.asm:2134 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: C ABI: BX/CX scratch (73.3); CX: C ABI: BX/CX scratch (73.3) |
| apps/cc/os88thunk.asm:2194 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand; postCX: shift banked, or C scratch |
| apps/cc/os88thunk.asm:2332 | REGROW -> ? | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: C ABI: BX/CX scratch (73.3); CX: C ABI: BX/CX scratch (73.3) |
| apps/cc/os88thunk.asm:2413 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand; postCX: shift banked, or C scratch |
| apps/chart/chart.asm:175 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: WM_CREATE writes BX next; .fail is CF=1 |
| apps/chart/chart.asm:629 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: grant = request (a multiple of 64); sub 64 then x16 = bytes; CX: CL/CX rewritten (mov cl,4 / xchg) before use |
| apps/dos/dos.asm:1909 | CLAIM_HI -> CLAIM_HI | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; CX: dos_hpop_7 epilogue pops CX |
| apps/dos/dos.asm:7029 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: DHK_CLAIM callers: dsh_bufget banks BX, dsh_sbuild writes it |
| apps/dos/dos.asm:7037 | CLAIM -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/dos/dos.asm:15420 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/dos/dosload.asm:293 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/dos/dosw16.inc:86 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; CX: CL rewritten; dos_hpop_7 pops CX |
| apps/dos/doswr.inc:124 | REGROW -> REGROW | AX, BX | dead | written first on every path (tool) |
| apps/excitebike/cga.inc:59 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: SAVE/RESTORE bracket |
| apps/excitebike/flow.inc:1137 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: SAVE/RESTORE bracket; xor bx before READ |
| apps/excitebike/game.inc:537 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/excitebike/sprite.inc:32 | CLAIM_DMA -> CONV | AX, BX | dead | BX: SAVE/RESTORE bracket |
| apps/excitebike/sprite.inc:40 | CLAIM_DMA -> CONV | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: SAVE/RESTORE bracket |
| apps/excitebike/video.inc:477 | CLAIM_DMA -> CONV | AX, BX | dead | written first on every path (tool) |
| apps/fractal/fractal.asm:850 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/fractal/fractal.asm:859 | REGROW -> REGROW | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/fractal/fractal.asm:1201 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/frotz/zio.inc:871 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: xor bx before the READ; zm_pages banks BX |
| apps/frotz/zio.inc:937 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: zi_load rewrites BX (mov bl) before any read |
| apps/frotz/zio.inc:2569 | CLAIM -> CLAIM | AX, BX | dead | BX: caller zi_load: zw_setup banks BX, WM_TITLE loads it |
| apps/frotz/zpic.inc:888 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/frotz/zwin.inc:2848 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/frotz/zwin.inc:2852 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/ftpd/ftpd.asm:489 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/ftpd/ftpd.asm:504 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/imgtest/imgtest.asm:108 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| apps/imgtest/imgtest.asm:209 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/isotron/isotron.asm:289 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/isotron/isotron.asm:294 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/isotron/isotron.asm:298 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/isotron/isotron.asm:303 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/isotron/isotron.asm:392 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/midirack/midirack.asm:596 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/midirack/mrout.inc:903 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: mrb_start banks every register |
| apps/midirack/mrout.inc:1325 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/midirack/mrwt.inc:200 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/midirack/mrwt.inc:507 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/os88partsbody.inc:2245 | CLAIM -> CLAIM | AX, BX | dead | BX: op_hilift runs under op_fetch, which clobbers BX by contract (its regrow); comment now says BX |
| apps/os88pself.inc:111 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/os88type.inc:789 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/os88type.inc:1222 | CLAIM -> CLAIM | AX, BX, CX | dead | CX: CL/CH rewritten in the .g/.p loop |
| apps/os88xgap.inc:61 | CLAIM_DMA_HI -> CONV+CLAIM_HI | AX, BX | dead | BX: xchg bx,ax after SEG_LIN: BX written |
| apps/paint/paint.asm:1097 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:1110 | CLAIM -> CLAIM | AX, BX | dead | AX: the grant, paragraphs, by design |
| apps/paint/paint.asm:1168 | CLAIM -> CLAIM | AX, BX | dead | AX: [pt_smaxp] holds the grant, paragraphs |
| apps/paint/paint.asm:1370 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/paint/paint.asm:1468 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:1499 | CLAIM -> CLAIM | AX, BX | dead | AX: [pt_cbparas] holds the grant, paragraphs |
| apps/paint/paint.asm:1575 | CLAIM -> CLAIM | AX, BX | dead | BX: pt_gif_in banks BX; its callees write before reading |
| apps/paint/paint.asm:1639 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:11181 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:11274 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:12363 | CLAIM -> CLAIM | AX, BX | dead | AX: [pt_smaxp] holds the grant, paragraphs |
| apps/paint/paint.asm:12376 | REGROW -> REGROW | AX, BX | dead | AX: [pt_smaxp] holds the grant, paragraphs |
| apps/paint/paint.asm:13698 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:14030 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/paint/paint.asm:14039 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/paint/paint.asm:14095 | REGROW -> REGROW | AX, BX | dead | written first on every path (tool) |
| apps/paint/paint.asm:14933 | REGROW -> REGROW | AX, BX | dead | AX: [pt_smaxp] holds the grant, paragraphs |
| apps/paint/paint.asm:14952 | REGROW -> REGROW | AX, BX | dead | AX: [pt_smaxp] holds the grant, paragraphs |
| apps/pixel/pxedit.asm:3015 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/pixel/pxedit.asm:3571 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/pixel/pxgif.asm:2054 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: av_tick is a timer callback; PSV services bank (px_ringclaim pushes all); CX: rep stosw writes CX; PSV services bank |
| apps/pixel/pxmaster.inc:736 | CLAIM -> CLAIM | AX, BX, CX | dead | BX: px_claims banks BX; PX_AVAIL rewrites it on .again |
| apps/pixel/pxmaster.inc:865 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/pixel/pxpump.inc:50 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: CF=1 only (CF=0 BX=DX, the contract); callers jc away |
| apps/pixel/pxui.inc:3515 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/pixel/pxview.inc:420 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/pixel/pxwrite.asm:1866 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/pixelstein/pxgame.asm:357 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: entry: WM_CREATE after; CF=1 path BX dead |
| apps/pixelstein/pxstein.asm:240 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/pixelstein/pxstein.asm:283 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/pixelstein/pxstein.asm:288 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: OS88_REHOME rewrites BX, loader xor bx after |
| apps/realdmg/realdmg.asm:462 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: WM_CREATE after; .noroom CF=1 |
| apps/realdmg/realdmg.asm:947 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/realdmg/realdmg.asm:1867 | CLAIM -> CLAIM | AX, BX, CX | dead | CX: CH/CL rewritten; .room tail-calls rw_toast; setup chain reads no CX |
| apps/realdmg/realdmg.asm:2000 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/realdmg/realdmg.asm:2096 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/realdmg/realdmg.asm:2143 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: rw_jitxget push/pop of it only; its caller (setup chain) reads no BX |
| apps/realdmg/realdmg.asm:4384 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/realdmg/rwaot.inc:406 | CLAIM_HI -> CLAIM_HI | AX, BX | dead | written first on every path (tool) |
| apps/realdmg/rwaot.inc:816 | CLAIM_DMA -> CONV | AX, BX | dead | written first on every path (tool) |
| apps/realdmg/rwaot.inc:842 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/realdmg/rwbank.inc:181 | CLAIM_DMA -> CONV | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: PKG_MEGA arm unbanked: rw_load0 .say pops BX, setup chain reads none |
| apps/realdmg/rwbank.inc:185 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/realdmg/rwbank.inc:753 | new helper -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/realdmg/rwbank.inc:776 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/realdmg/rwreplay.inc:535 | CLAIM -> CLAIM | AX, BX | dead | AX: mov al,2 rebuilds the KB (grant 128 = request) |
| apps/recorder/recorder.asm:787 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: rc_cmdtab dispatch, then rc_repaint (clobbers BX by contract) |
| apps/recorder/recorder.asm:890 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: rc_cmdtab dispatch, then rc_repaint (clobbers BX by contract) |
| apps/redline/detect.inc:358 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/scribe/scribe.asm:770 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| apps/scribe/scribe.asm:4786 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/scribe/scribe.asm:7236 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/scribe/scribe.asm:7337 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/scribe/scribe.asm:16979 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/scribe/scutil.inc:402 | CLAIM -> CLAIM | AX, BX | dead | BX: sc_scget broke "preserves all": callers xor bx / pop bx, sc_redraw reads none; comment fixed |
| apps/sheet/sheet.asm:2657 | CLAIM -> CLAIM | AX, BX | dead | BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:2667 | CLAIM -> CLAIM | AX, BX | dead | BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:2674 | CLAIM -> CLAIM | AX, BX | dead | BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:2686 | CLAIM -> CLAIM | AX, BX | dead | BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:2717 | CLAIM -> CLAIM | AX, BX | dead | BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:2724 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: entry: WM_CREATE after; sh_note_arg banks BX |
| apps/sheet/sheet.asm:43710 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | BX: AVAIL output; units checked by hand |
| apps/skies/csraster.inc:290 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/stiknote/stiknote.asm:573 | CLAIM -> CLAIM | AX, BX | dead | AX: grant x16 = the capacity read into; BX: xor bx before the READ |
| apps/syscheck/disk.inc:212 | CLAIM -> CLAIM | AX, BX, CX | dead | CX: CL rewritten; .nomem pops CX |
| apps/syscheck/out.inc:37 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/syscheck/out.inc:553 | CLAIM -> CLAIM | AX, BX, CX | dead | AX: by hand: AL/AX written first, or only pushed/printed; CX: CX = KB set before the call on purpose (FSKD_REFUSED reads it) |
| apps/syscheck/scan.inc:497 | CLAIM -> CLAIM | AX, BX, CX | dead | BX: mov bx,ax next; sc_manaddf banks BX |
| apps/tank/tkraster.inc:129 | CLAIM -> CLAIM | AX, BX | dead | AX: grant >> 6 = KB rung (grant a multiple of 64) |
| apps/tank/tkraster.inc:133 | CLAIM -> CLAIM | AX, BX | dead | AX: grant >> 6 = KB rung |
| apps/tank/tkraster.inc:137 | CLAIM -> CLAIM | AX, BX | dead | AX: grant >> 6 = KB rung |
| apps/tank/tkraster.inc:146 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/tank/tkraster.inc:201 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/telnet/tezm.inc:1859 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/texpad/tpexport.inc:38 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/texpad/tpexport.inc:132 | REGROW -> REGROW | AX, BX | dead | written first on every path (tool) |
| apps/tracker/tracker.asm:1895 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/tracker/trkwin.inc:3241 | CLAIM -> CLAIM | AX, BX | dead | BX: tw_fire banks BX round the action; comment fixed |
| apps/vbattle/vbattle.asm:579 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/vbattle/vbattle.asm:2171 | CLAIM_DMA_HI -> CONV+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: vb_cardclaim/vb_prep/vb_lread write before read |
| apps/vbattle/vbattle.asm:2809 | new helper -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand; postCX: shift banked, or C scratch |
| apps/vbattle/vbattle.asm:2826 | new helper -> CONV | AX, BX, CX | dead | written first on every path (tool) |
| apps/vbattle/vbbm.inc:66 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/vbattle/vbhold.inc:283 | CLAIM_DMA -> CONV | AX, BX | dead | BX: vb_hon1 writes before read |
| apps/vbattle/vbprof.inc:102 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/vbattle/vbprof.inc:249 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| apps/vbattle/vbprof.inc:771 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/video/video.asm:12091 | CLAIM_DMA_HI -> CONV+CLAIM_HI | AX, BX | dead | written first on every path (tool) |
| apps/video/video.asm:24168 | new helper -> ? | AX, BX, CX | dead | written first on every path (tool) |
| apps/video/video.asm:24231 | new helper -> ? | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/video/video.asm:24240 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/waverack/wrdoc.inc:333 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: pop bx before we_open; .refuse sets BL |
| apps/waverack/wredit.inc:420 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/waverack/wredit.inc:623 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/waverack/wrrec.inc:1179 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: refusal AX = request rounded = request (x64), the compare holds |
| apps/waverack/wrrec.inc:1218 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/waverack/wrsave.inc:375 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/waverack/wrxport.inc:1023 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/waverack/wrxport.inc:1030 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/waverack/wrxport.inc:1052 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: refusal AX = request (x64), the compare holds |
| apps/win16/w16i31.inc:251 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| apps/win16/w16load.inc:141 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: w16l_fread banks BX; BX saved in [w16_clus] first |
| apps/win16/w16load.inc:2868 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/win16/w16mm.inc:2239 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | written first on every path (tool) |
| apps/win16/w16sel.inc:424 | CLAIM_DMA -> CONV | AX, BX | dead | written first on every path (tool) |
| apps/win16/win16.asm:1386 | CLAIM_DMA -> CONV | AX, BX | dead | written first on every path (tool) |
| apps/wirefleet/wfanim.inc:71 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/wirefleet/wfsound.inc:391 | CLAIM_DMA_HI -> CLAIM_HI+CONV+DMAPOOL | AX, BX | dead (AVAIL outputs) | AX: by hand: AL/AX written first, or only pushed/printed; BX: BX was MEMP_AVAIL output already; op_fetch clobbers BX |
| apps/word/wdutil.inc:388 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/word/word.asm:715 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| apps/word/word.asm:5765 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/word/word.asm:8879 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/word/word.asm:9036 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/xtmp3/x3eng.inc:1923 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| apps/xtmp3/x3eng.inc:4495 | REGROW -> REGROW | AX, BX | dead | written first on every path (tool) |
| apps/xtmp3/x3eng.inc:4525 | new helper -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| apps/xtmp3/x3pars.inc:1237 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| apps/xtmp3/x3rep.inc:341 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| drivers/hdd/hdd.asm:1087 | CLAIM_DMA -> ? | AX, BX | dead | BX: BX = 0 is the verb and the read offset, by design |
| drivers/hdd/inst.inc:1251 | CLAIM -> CLAIM | AX, BX, CX | dead | written first on every path (tool) |
| drivers/hdd/mount.inc:246 | CLAIM_DMA -> CLAIM+CONV | AX, BX | dead | written first on every path (tool) |
| drivers/hdd/mount.inc:524 | CLAIM -> CONV | AX, BX | dead | written first on every path (tool) |
| drivers/ramdisk/rdimg.inc:100 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| drivers/ramdisk/rdimg.inc:351 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| drivers/sound/sb.inc:2737 | CLAIM_DMA_HI -> CLAIM_HI+CONV | AX, BX | dead | written first on every path (tool) |
| drivers/xmem/xmem.asm:1631 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/covbench/covbench.asm:423 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: by hand: rewritten before the first read |
| tests/cpubench/cpubench.asm:397 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/filetest/filetest.asm:110 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/filler/filler.asm:79 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| tests/filler/filler.asm:89 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/filler/filler.asm:125 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | BX: AVAIL output; units checked by hand |
| tests/filler/filler.asm:134 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/filler/filler.asm:167 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| tests/filler/filler.asm:173 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/filler/filler.asm:223 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/filler/filler.asm:312 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand |
| tests/gfxbench/gfxbench.asm:369 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/heapfrag/heapfrag.asm:1519 | new helper -> ? | AX, BX, CX, SI | dead | BX: hf_kb: callers read BX only after hf_kav (AVAIL) |
| tests/heapfrag/heapfrag.asm:1529 | new helper -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/lzfence/lzfence.asm:41 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/lzfile/lzfile.asm:49 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/megabrdg/megabrdg.asm:122 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megacp/megacp.asm:48 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megacp/megacp.asm:79 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/megacp/megacp.asm:128 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megaflt/megaflt.asm:76 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/megaflt/megaflt.asm:97 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/megahg/megahg.asm:571 | REGROW -> REGROW | AX, BX, CX | dead | BX: mov bl,0x52 next; BH read only (kept) |
| tests/megahi/megahi.asm:54 | CLAIM -> CLAIM | AX, BX | LIVE -> fixed | AX: by hand: AL/AX written first, or only pushed/printed; BX: LIVE: HUGE at :112 reads BX; fixed with xor bx |
| tests/megahi/megahi.asm:73 | REGROW -> REGROW | AX, BX | LIVE -> fixed | AX: by hand: AL/AX written first, or only pushed/printed; BX: LIVE: HUGE at :112 reads BX (BL = 4 = MEMH_REGROW); fixed with xor bx |
| tests/megahi/megahi.asm:88 | REGROW -> REGROW | AX, BX | LIVE -> fixed | AX: by hand: AL/AX written first, or only pushed/printed; BX: LIVE: HUGE at :112 reads BX (BL = 4 = MEMH_REGROW); fixed with xor bx |
| tests/megahi/megahi.asm:175 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/megahuge/megahuge.asm:192 | AVAIL -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/megahuge/megahuge.asm:207 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/megai13/megai13.asm:28 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/megajw/megajw.asm:39 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand; postCX: shift banked, or C scratch |
| tests/megaown/megaown.asm:607 | new helper -> ? | AX, BX, CX, SI | dead | written first on every path (tool) |
| tests/megaown/megaown.asm:616 | new helper -> AVAIL | AX, BX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/megard/megard.asm:33 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry, worker loop reads no BX |
| tests/megargw/megargw.asm:118 | REGROW -> REGROW | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: mr_win writes BL before mr_fill reads it |
| tests/megargw/megargw.asm:154 | REGROW -> REGROW | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: mr_win writes BL before mr_fill reads it |
| tests/megargw/megargw.asm:176 | REGROW -> REGROW | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megarmb/megarmbf.asm:163 | CLAIM_DMA -> CONV | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megarmb/megarmbf.asm:357 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megarmb/megarmbf.asm:429 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megarmb/megarmbf.asm:451 | CLAIM_DMA -> CONV | AX, BX | dead | written first on every path (tool) |
| tests/megarmb/megarmbx.asm:105 | CLAIM_DMA -> CONV | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/megarsq/megarsq.asm:37 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/memp/memp.asm:62 | new helper -> AVAIL | AX, BX, CX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand; CX: by hand: rewritten before the first read |
| tests/memp/memp.asm:177 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/memp/memp.asm:189 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/msstk/msstk.asm:48 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand); BX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/pkgrun/pkgrun.asm:181 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/pxsbench/pxsbench.asm:229 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: by hand: rewritten before the first read |
| tests/rehome/rhprog.asm:76 | CLAIM -> CLAIM | AX, BX | dead | AX: entry: CF=1 exit, or WM_CREATE/pop writes it first (by hand) |
| tests/rehome/rhprog.asm:122 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/sbtest/sbtest.asm:436 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/sndcap/sndcap.asm:197 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/sndfmt/sndfmt.asm:323 | CLAIM_DMA_HI -> CONV+DMAPOOL+CLAIM_HI | AX, BX | dead | BX: xor bl / mov written before read |
| tests/spkbench/spkbench.asm:321 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: by hand: rewritten before the first read |
| tests/spkfx/spkfx.asm:34 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/spkfx/spkfx.asm:39 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/spkfx/spkfx.asm:44 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/sysbench/sysbench.asm:315 | AVAIL -> AVAIL | AX, BX, postCX | dead (AVAIL outputs) | AX: AVAIL output; units checked by hand; BX: AVAIL output; units checked by hand |
| tests/sysbench/sysbench.asm:1264 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: by hand: rewritten before the first read |
| tests/sysbench/sysbench.asm:3213 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/sysbench/sysbench.asm:3495 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/sysbench/sysbench.asm:4654 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed; BX: by hand: rewritten before the first read |
| tests/trklog.inc:171 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/trklog.inc:695 | CLAIM -> CLAIM | AX, BX | dead | AX: by hand: AL/AX written first, or only pushed/printed |
| tests/vidbench/vidbench.asm:413 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/vidbench/vidbench.asm:418 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/vidbench/viddisk.asm:890 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | written first on every path (tool) |
| tests/vidbench/viddisk.asm:1741 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |
| tests/vidbench/vidsnd.asm:870 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | BX: xor bl before read |
| tests/vidbench/vidsnd.asm:876 | CLAIM_DMA -> CONV+DMAPOOL | AX, BX | dead | BX: xor bl before read |
| tests/vidbench/vm3bench.asm:433 | CLAIM -> CLAIM | AX, BX | dead | BX: by hand: rewritten before the first read |
| tests/vidkern/vidkern.asm:851 | CLAIM -> CLAIM | AX, BX | dead | written first on every path (tool) |

### The 34 sites whose `xor cx, cx` was dropped

| site | verdict |
|---|---|
| apps/artful/atcmd.inc:478 | dead |
| apps/artful/atui.inc:472 | dead |
| apps/cc/os88thunk.asm:2356 | dead |
| apps/deskclok/dkset.inc:541 | dead |
| apps/deskclok/dkwid.inc:717 | dead |
| apps/midirack/mrlist.inc:496 | dead |
| apps/notepad/notepad.asm:4526 | dead |
| apps/notepad/notepad.asm:7717 | dead |
| apps/os88ui.inc:4996 | dead |
| apps/os88ui.inc:6238 | dead |
| apps/paint/paint.asm:1500 | dead |
| apps/realdmg/realdmg.asm:1508 | dead |
| apps/scribe/scdoc.inc:2685 | dead |
| apps/scribe/scribe.asm:11376 | dead |
| apps/scribe/scribe.asm:11379 | dead |
| apps/scribe/scrtf.inc:1293 | dead |
| apps/texpad/texpad.asm:292 | dead |
| apps/thewire/thewire.asm:2847 | dead |
| apps/tracker/tracker.asm:1453 | dead |
| apps/tracker/trkspk.inc:189 | dead |
| apps/video/video.asm:24109 | dead |
| apps/video/video.asm:24228 | dead |
| apps/waverack/waverack.asm:170 | dead |
| apps/win16/w16pm.inc:283 | dead |
| apps/win16/w16pm.inc:617 | dead |
| apps/win16/w16rgn.inc:1028 | dead |
| apps/word/word.asm:13457 | dead |
| apps/word/word.asm:13460 | dead |
| apps/xtmp3/x3eng.inc:1390 | dead |
| apps/xtmp3/x3eng.inc:4367 | dead |
| drivers/ether/tcp.inc:981 | dead |
| drivers/httpfs/hfidx.inc:124 | dead |
| drivers/ramdisk/rdstore.inc:192 | dead |
| drivers/sound/volume.inc:142 | dead |
