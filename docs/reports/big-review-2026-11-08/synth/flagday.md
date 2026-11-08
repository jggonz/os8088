# Phase 2, the flag day - the final plan from the kernel review of 2026-11-08

This is the final Phase 2 synthesis of the kernel review: every finding of the
25 area reviews and the 12 gap reviews that needs an ABI break, or that changes
a row already on `docs/plans/ABI-BREAK-LIST.md`, reconciled row by row and then
put through three adversarial lenses. It is a dated reading of one tree
(branch `the-big-review`), not a maintained document. Nothing in the
repository was edited, and nothing was built or booted.

It replaces the first version of this file. That version was written before
the three challenge lenses ran; where a lens corrected a figure, only the
corrected figure appears below.

It has three parts:

1. **The existing rows the review touches**, with a verdict each.
2. **The new rows**, in ABI-BREAK-LIST's own style and section letters. The
   text to append to the list is the companion file
   `synth/abilist-section.md` (it becomes the list's section 13).
3. **The recommended bundle**: what has to land first, the order, and the net
   bytes per kernel at `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14.

## How to read it

**Inputs.**

- `data/*.json`, all 37 files (25 areas, 12 gaps). Findings whose verifier
  verdict is *refuted* are dropped. The 24 tagged `flagday` are all read, as
  is every finding whose better form needs a break or that names a row.
- `areas/gap-flagday-reconcile-abilist.md` (`gfr-*`): the gap review that
  reconciled the review against the list row by row. It supersedes the first
  syntheses where they differ, except where a lens corrected it.
- `synth/challenge-truth.json`, `challenge-cost.json` and `challenge-fit.json`:
  three independent lenses over the flag-day items T5, L1, R14, A4, T6, C1,
  D4, D8, R9 and L3 (verdict stands / amend / refute). Where the lenses
  disagreed on a figure, it was re-derived from `build/kernel.map`,
  `build/smallk/kernel.map`, `build/megak/kernel.map`, the `kernel-full.bin`
  images and `git log`, or the range is stated with the reason.

**Source tags.**

| tag | meaning |
|---|---|
| **[c]** | confirmed by the area review's verifier |
| **[k]** | corrected by the verifier; the corrected figure is the one quoted |
| **[u]** | unchecked |
| **[g]** | from a gap review (its own verification pass is in its report) |
| **[Lt] [Lc] [Lf]** | amended by the truth, cost or fit lens; the amended figure is the one quoted |
| **[s]** | checked against the tree by this synthesis |

**Byte conventions.** Every byte figure is ESTIMATED unless it says MEASURED;
every cycle figure is PREDICTED (8088 timing tables and the fetch floor), not a
MartyPC reading. Figures are signed (minus is a saving) and name the section:

- **resident**: `.text`, `.cold`, `.bss`. `.cold` IS resident on kern_big and
  kern_small, billed by `KERN_BUDGET` exactly like `.text`;
- **transient**: `.boot2`, `.ovl`;
- **module**: inside a `.DRV` image (CTRL.DRV, HIBER.DRV, FDLG.DRV), not
  resident;
- **conv**: kern_mega conventional, i.e. its `.text`, `.bss` and `.lowbss`.
  kern_mega's `.cold` and `.mega` run above 1MB after the switch, so a saving
  there is not a conventional one (`MEGA_RESIDENT`).

kern_emu carries every kern_big figure, because `KERN_EMU` implies `KERN_BIG`.

**Measurements this synthesis took [s].**

- **The rare-cell count, by decoding each table.** A rare cell is
  `55 E8 rel16 dw target` (`push bp / call api_r<kind>`) inside
  `osapi_table`..`osapi_table_end` (0x10..0x4FB on all three kernels).
  MEASURED: **kern_big 112** (`api_rs` 50, `api_rsc` 19, `api_rxc` 24, `api_rx`
  7, `api_rn` 12), **kern_small 112** (51 / 18 / 24 / 7 / 12, 93 distinct
  targets because its refusal cells share one), **kern_mega 119** (57 / 19 /
  24 / 7 / 12). kern_small has the same number of rare cells as kern_big, so
  T5 is worth the same there.
- **The ten handler-setter cells are INLINE, not rare.** Decoded on kern_big:
  `2E 89 47 xx CB` (`mov [cs:bx+W_x],ax / retf`) or the 6-byte `disp16`/`clc`
  forms, at table offsets 0x196, 0x1BC, 0x2EB, 0x334, 0x342, 0x357, 0x362,
  0x380, 0x450 and 0x499; about 55 bytes. R14 withdraws them whether or not
  T5 lands.
- **The window record.** kern_big: 13 handler words (`W_PAINT`, `W_ONKEY`,
  `W_ONCLICK`, `W_ONSIZE`, `W_ONMOUSEUP`, `W_ONDRAG`, `W_ONTIMER` at 12-31,
  then `W_ONSZ`, `W_ONWK`, `W_ONCL`, `W_ONRC`, `W_ABOUT`, `W_ONWH` in the side
  block), `WIN_SIZE` 74, `MAX_WIN` 12 (`kernel/wm.inc:26-163`). kern_small:
  `W_SIDE` 28, `WIN_SIZE` 65, `MAX_WIN` 6, 10 handler words.
- **The kern_mega fixup spans** (top-level symbol to the next, MEASURED):
  `mega_gpfix` 602, `mega_gpw` 526, `mega_null0` 74, `mega_db` 69 (1,271),
  `mega_strcal_gp` 43, `mega_strcal` 138, `mega_strfix` 175 (1,627 with
  them), `mega_hibnc` 235, `drv_hi_ivt` 67.
- **The driver-table history.** `git log -G` over `drivers/os88drv.inc`: the
  last `DSV_SIZE` change is no later than `2ce1e37b` (2026-09-08), and
  `DRV_VER` has moved since at `b47de145` (2026-10-14, to 11) and `d323a9a6`
  (2026-11-02, to 12). Every driver the equality test admits today carries a
  full table.
- **The section sizes** (summary lines of the three maps): `.bss` ends at
  0xB27E / 0x80CB / 0xC918; kern_small's `.lowbss` is 0xB34 (2,868 bytes);
  `DSK_FAT_SECS` is 9 on kern_big and kern_mega and 2 on kern_small
  (`kernel/kernel.asm:1519`, `:1565`).

---

## Verdict

The list and the review fit together, but the review changes the list's three
biggest rows into better designs, and the lenses then cut each of those back to
what the code supports.

1. **The table row is the software-interrupt door (T5), not five-byte cells
   (T1).** About **-325 to -385 resident on kern_big and on kern_small**
   (the same 112 rare cells on both, MEASURED count) and **-345 to -410 conv
   on kern_mega** (119 cells), plus **-2 bytes at each of ~2,640-2,770 rare
   package call sites**. The price is **+150 to +300 clocks a rare call**,
   PREDICTED. The door must own its vector against DOS (a named vector in
   81h-FFh, virtualised in DOS's AH=25h/35h), and its run-time refusal covers
   rare cells only, so A4's load-time level is still the guard for hot cells.
   T1 stays the fallback.
2. **The window-handler row is a class table in the package (R14), not one
   indexed setter (K17-3).** About **-320 kern_big** (13 words a record, not
   10), **-95 to -120 kern_small**, **~-350 conv kern_mega**, and +40 to +90
   clocks a callback. It must keep kern_small's refusal signal for the
   handlers kern_small cannot dispatch.
3. **L1 is a kern_big row as written.** kern_big saves **-384 ladder bytes**
   (paragraph-exact, MEASURED from the map). kern_mega saves **0 conv** as
   written, because its rungs stay 512-rounded. **kern_small LOSES bytes**: a
   shared `KERNEL_SEG` of 0x01A0 leaves seven sectors below kern_small's image
   that its two-sector FAT window does not need. L1 needs a kern_small design
   before it can ride the bundle.
4. **D4 leaves the flag day.** No shipped driver declares a short table, so
   the service-table floor is a contract clarification that needs no bump,
   and the code it touches is not resident. It joins N1 and histsys-7 in
   Phase 1.
5. **R9 shrinks to its documentation half.** A universal AX=1 collides with
   live family codes on the same cell (`EMSE_ROOM` = 1 through
   `OSAPI_DRV_CALL`).
6. **C1 survives as a bytes-and-one-unit row, not a correctness row.**
   Withdrawing the cells removes none of the KB shifts that carried the bug;
   those are kernel-internal and are fixed in Phase 1.
7. **The bundle saves about -1,170 resident on kern_big (-1,100 to -1,260),
   -515 on kern_small (-490 to -570, L1 excluded) and -770 conv on kern_mega
   (-700 to -870), all ESTIMATED.** About a third of kern_big's figure is L1's
   pad, which varies 0-511 by build. Packages lose about **-5.3 to -5.5KB
   unpacked** at the rare sites and about **-1.5KB** where class tables
   replace setter code. The bump is **`PKG_FMT` 10 -> 13 and `DRV_VER`
   12 -> 14** (list section 0, item 2: 11 and 12 are drivers', 13 is free for
   a package, so drivers go straight to 14).

---

## 1. Existing rows the review touches

Verdicts: **S** strengthened, **W** weakened, **X** refuted, **SUP** superseded
(taken, moot or replaced), **R** refreshed (facts restated from the code).

Rows not listed are untouched and still apply where the bundle uses them: T2,
C5, C6, C8, R3, R4, R5, R7, R8, D2, F1, L2, L4, L5, P1, A1-A3, V4-V6, R2-2,
R2-3, R2-4, R2-5 and P2-1.

| row | verdict | reason | sources |
|---|---|---|---|
| **T1** | **SUP by T5**, kept as the fallback | The door at the site (3 bytes against 5) and a 2-byte table word buys about three times T1's saving. T1 survives only if T5's DOS-vector or IF proof fails. abi-4's literal `[bp+10]` frame read in `dwsf_dskw_write_seq` is a hidden reader of the frame either row moves, so `API_RN_DSOFF` goes in now | bytes-1 [k], gfr-1 [g], abi-4 [k] |
| **K17-1** | **merge into T1**, then **SUP by T5** | The same five-byte design, priced twice and declined once | gfr-10 [g] |
| **T3** | **refused, unchanged** | Under T5 seven of kern_small's thirteen refusal cells are rare (`wm_band`, `xm_alloc`, `xm_free`, `vol_stat`, `drv_cfg_x`, `drv_dlg_x`, `pkg_rehome_x`) and become 2-byte words pointing at one shared refusal: -28, already inside T5's kern_small figure. The index still exists and refuses, so SPEC.md 20.8 rule 4 holds. The six hot ones stay | gfr-1 [g], [s] |
| **T4** | **X harder** | A rare cell becomes 2 bytes under T5; no multiplexed verb is cheaper | gfr-1 [g] |
| **T2-2** | **SUP in part, X for the rest** | Of kern_mega's ten `MEGA_ICW` cells, six are handler setters (`W_ONSIZE`, `W_ONSZ`, `W_ONWK`, `W_ONRC`, `W_ONCLICK`, `W_ONWH`; stubs at `mega.inc:5643-5648`), and R14 deletes them. The four left (`gfx_color` = `SET_COLOR`, `osapi_seed`, `gfx_b1ink`, `ico_c1`) are kern_big's 52-cycle inline cells, worth about -32 conv. **Refuse the remainder**: XT cycles beat 32 conventional bytes | gfr-3 [g], R14 [Lt] |
| **K17-2** | **R**: survives, reduced | One runtime `OSAPI_WM_FLAG` (AL set/clear, AH bit) beside R14; the creation-time flags become the class's style word. 107 flag-setter asm call sites (SNAP 30, OWNBG 27, SAVEU 19, KEEPH 14, SIZABLE 12, NOANIM 4, QUIET 1) | gfr-3 [g], abi-11 [k] |
| **K17-3** | **SUP by R14** | Folding ten setters into one indexed cell keeps the handler words in every record; R14 removes the words and the setters together, so taking K17-3 first spends a renumber on a cell R14 deletes. The ten setters are INLINE cells (decoded [s]), about 55 bytes on kern_big. 199 handler-setter asm call sites, five of them in drivers | abi-11 [k], histsys-9 [k], gfr-3 [g], [Lc] |
| **K17-4** | **S in bytes, S in price** | Under T5 each demoted SLOT saves 6, not 2: -78 rather than -26. But a rare call then costs +150 to +300 clocks over a SLOT, not ~+78, so the per-cell frequency reading the row asked for binds harder. It becomes part of V7 | bytes-1 [k], gfr-1 [g], T5 [Lc] |
| **K17-5** and **C7** | **one row, recorded** | The same cell: `OSAPI_DRV_CALL_AT` is `drv_pkg_call_x`'s second door (`kernel.asm:3715` beside the XCELL at `:3558`). Two call sites, both in FTPD. Keep C7's "recorded, not proposed" and delete K17-5 | gfr-10 [g], [s] |
| **C1** | **R: a bytes-and-one-unit row; its correctness half moves to Phase 1** | Three package-reachable doors carried the KB carry defect, not four, and two are already guarded (claim AX, `memory.inc:939`; regrow, `:4584`); md-16 is kern_dos's private allocator (`kerndos/kdshim.inc:409-422`), which has no OSAPI door. The live one, the DMA head CX (`memory.inc:867-879`), is BR-105's 0..+3 fix now. Withdrawing the six cells removes none of the shifts, because each sits in a shared KB helper the kernel's own ~20 callers keep (`disk.inc:9674, 9922`, `files.inc:1306, 1821, 6071`, `hiber.inc:2094, 2605, 2930, 4047`, `mega.inc:415`, `font.inc:375`, `filecp.inc:801`, `desksc.inc:1183`, ...); converting those is kernel-private Phase 1 work. **Bytes, MEASURED spans:** six rare cells (36 `.text`, 12 under T5) plus 57 of `.cold` door bodies (`mmf_osapi_mem_claim` 4, `_claim_dma` 18, `_claim_hi` 5, `_claim_dma_hi` 12, `osapi_mem_regrow_x` 11, `mem_claim_dma_hi_x` 7; `mmf_osapi_mem_avail` stays, `compress.inc:1758` far-calls it): **-69 kern_big and kern_small under T5 (-93 without), -12 conv kern_mega (-36 without)**. **Packages pay**: a converted site needs a verb and a class register and may bank what `MEMP` returns in AX, so 217 sites cost about +0.9 to +1.7KB inlined, or about +0 to +150 with a shared near helper | mem-18 [u], mem-5 [c], hm-6 [k], md-16 [c], gfr-7 [g], [Lt] [Lc] [Lf] |
| **C2** | **unchanged; smaller under T5** | About -11 kern_big under T5. 11 call sites | gfr-7 [g] |
| **C3** | **X, by arithmetic** | It saves 6 kernel bytes (4 under T5), and every one of **154 call sites** (GOTO 8, GOTO_Q 20, GOTO_QM 126; MEASURED) then needs a `mov al, mode`: about +300 across packages. **Status: proposed -> refused** | gfr-7 [g], [s] |
| **C4** | **R: half dead; take the dead half** | `OSAPI_FILE_APPEND_SYS` has **zero callers** in `apps/`, `drivers/`, `tests/` and the C SDK; HDD.DRV's installer writes with `OSAPI_FILE_WRITE_SEQ` + `WSEQF_SYS` (`drivers/hdd/inst.inc:2246-2250`). Withdraw APPEND_SYS alone; WRITE_SYS keeps its two sites, so nobody has to zero AL. **-17 kern_big, MEASURED** (the 3-byte jump cell, `api_file_append_sys` 9 at `.text` 0x5D0, `dwf_dskw_append_sys` 5 at `.cold` 0xAD10), about -3 kern_small, **-12 conv kern_mega** (its `dwf_` thunk is `.cold`, above 1MB) | gfr-7 [g], [s] |
| **R1** | **S** | Both forms and `ld_rehsz` are still in `kernel/loader.inc`, with one call site (KEEP). hm-11's `OP_INITDROP` would be a second KEEP caller; the bytes form would still have none. kern_mega: the `ld_start` share is `.cold`, so about -6 conv | hm-11 [k], [s] |
| **R2** | **unchanged, keep** | Correctness | - |
| **R2-6** (handles) | **W** | kern_mega's measured forks are `.cold`, above 1MB, not conventional; packages would pay +2 to +4 at each of 367 FREE/MOVABLE sites, which C1 does not convert. hm-2's now-fix (+6 to +8) buys the correctness half alone. **Stays recorded; hm-10 folds in** | hm-10 [c], gfr-4 [g], hm-2 [k] |
| **R6** | **unchanged (shelved); two riders** | wm-R3 (window handles) is refused while R6 is shelved: the record is readable by contract. histsys-17 (now) groups the implicit display-context bytes as `gfx_dc`, which gives R6 a home | wm-R3 [u], gfr-4 [g], histsys-17 [c] |
| **N1** | **S; take now (Phase 1)** | Both fallbacks are still present. Only the FSV one is resident: 7 bytes in `drv_fs_call` (kern_big `.cold`). The DSV one (4 bytes) is in the `DRV_ATTACH` macro, which expands in `.boot2`, CTRL.DRV and HIBER.DRV, so it saves 4 per copy and no resident byte | gfr-6 [g], D4 [Lc], [s] |
| **D1** | **SUP once histsys-7 and the D4 floor land (both Phase 1)** | histsys-7 keeps class 1's copy and reads the others in place, after which `drv_svcix` has nothing to index. With D4's floor written down, the in-place reads need no per-read length test. drivers-9's common-prefix reorder has nothing left to shrink. If histsys-7 is rejected, D1 + drivers-9 stay one row | histsys-7 [k], drivers-9 [c], gfr-6 [g], D4 [Lc] [Lf] |
| **D3** | **S** | The header's +16 name already carries the driver's title, so the resident titles duplicate the files; that feeds D7 | histsys-12 [u], gfr-6 [g] |
| **F2** | **unchanged (recorded)** | bytes-8 drops the `.DRV` extension from the module-name strings (no ABI); F2 is the `WIN95` -> `WIN` file rename. Neighbours, not one row | bytes-8 [k] |
| **F2-1** | **SUP: BUILT** | SPEC.md 20.13.9 (`LZ_ESC`, `kernel/lz.inc:90, 252`), with no container change and no bump | gfr-10 [g], [s] |
| **L1** | **S on kern_big; W on kern_small and kern_mega** | The pads are MEASURED (`.bss` 0xB27E / 0x80CB / 0xC918 against their 512 roundings), and `FAT_SEG equ KERNEL_SEG + KIMG_PARA` (`kernel.asm:1804`) on every `ROM_COLD` kernel. In bytes, paragraph-exact: **kern_big -384**. **kern_small**: `KERNEL_SEG` is one constant on every kernel (packages carry it), but kern_small's FAT window is 2 sectors against 9, so the ten-sector region below the image is about **3,584 bytes too big for kern_small**; even with its whole `.lowbss` (2,868) moved down, about 1,228 stay empty, against a 304-byte pad: **+924 to +3,280** as written. **kern_mega**: no `LADDER_PACK` (`ROM_ADOPT` is not defined there, `kernel.asm:1142-1146`), rungs stay 512-rounded and `COLD_SEG` a multiple of 8 paragraphs, so **0 conv** as written; up to -128 (the 128-byte selector granule, `mega.inc:12-15`) only if kern_mega's ladder is packed too. The realized `KERN_SIZE` step on kern_big today is a whole rung, which is the wrong number to quote. Churn: about 150 explicit `mov ax, KERNEL_SEG` sites in packages (rebuilds), the published words at 0060:000C and 0060:000E, crt0, DOS/kerndos, both boot paths, hibernate images, the ROMs and `COLDHMA.SYS`. After T5 a rare site names no segment, and both rows bump both numbers, so they share one flag day | gfr-8 [g], [Lt] [Lc] [Lf], [s] |
| **L3** | **S: a staged road; still out of the bundle** | (a) L7's clean bit, (b) the driver half of D8, (c) mc-10's `[mega_kfix]` counter. Corrected bytes, MEASURED: **-1,271 `.mega`** for the narrow set (`mega_gpfix` 602, `mega_gpw` 526, `mega_null0` 74, `mega_db` 69), **-1,627 `.mega`** with pmcore's `mega_strcal_gp`/`mega_strcal`/`mega_strfix`, plus **-17 conv** (`mega_tramp`, MEASURED) to about -60 conv (pmcore's ESTIMATE of the `.text` share). The list's label "MEASURED" on its L3 figure was wrong and the span stale. A driver-only IRQ cell retires only the drivers' share of `mega_null0`/`mega_db`, which trap EVERY segment-0 access, BDA reads by packages included (WIN16, Video). The one-disk 8088/286 product (gkm-9) runs against the standing rule that no release carries kern_mega's disks, so it is an open question for the owner, not a property of L3. Out of the bundle until The Wire serves kern_mega builds (the owner's ranking) | mc-19 [u], mc-10 [c], isr-13 [c], drivers-6 [c], gkm-9 [g], [Lt] [Lc] [Lf], [s] |
| **P2** | **S** | drivers-16: `VX_ABI_VER`, `XM_ABI_VER` and `HD_ABI_VER` are versioned by hand and fingerprinted nowhere; add them to `abilock.py` (0 shipped bytes) | drivers-16 [c] |
| **V1** | **S / R** | A new package-facing entry beside the raw body, because kernel callers rely on the preserve-everything body (`gfx_hline` keeps BX from the CX `gfx_fill` kept, `vga12.inc:1070-1079`); +~6. The C SDK already treats every register as volatile (`os88thunk.asm:58-66`), so its half is zero-risk; the asm half is gated on a lint | gfx-16 [c], abi-17 [c], gfr-9 [g] |
| **V2** | **S** | Its renumber is free inside T5's | gfr-1 [g] |
| **V3** | **unchanged (recorded)** | 7 call sites in the tree (RUN 2, WALK 2, SEL 3). Promotion still costs +6 | gfr-1 [g], [s] |
| **R2-1** | **R** | `mega_hibnc` is **235 `.mega` today** (MEASURED span); the list's figure was taken at msp2's tip. Above 1MB, not conventional | [s] |
| **section 11** | **SUP in part** | Bullets 1 (SND_PLAY's blocking text) and 3 (the dead 8-byte cold-cell macros) no longer grep. abi-10 and engsys-5 widen the point: the SDK and SPEC.md 20.3 keep two contract copies that disagree, and the cure is to delete the SPEC copy | abi-10 [c], engsys-5 [c], [s] |

**Refused rows respected.** Only K17-1 is re-proposed, as T5, with new
arguments: about three times the bytes, a run-time refusal for unknown rare
cells, and a rehearsal on kernel-private traffic first (P4). T3 stays refused;
T5 recovers part of its bytes inside rule 4. ui.md's "per-event handler words
beat one window procedure" refuses a Win16 `WndProc`, not R14, which keeps one
vector per event.

**List hygiene to apply with section 13:** mark F2-1 BUILT; rewrite C4 as
APPEND_SYS alone; merge K17-1 into T1 and K17-5 into C7; mark C3 refused; mark
T2-2 superseded in part; restate L3's and R2-1's figures; mark section 11
bullets 1 and 3 done.

---

## 2. New rows

Ids continue the list's own sequences; the gap review's provisional ids are
kept where they did not collide. Its clipboard and Open-filter rows are
**R15** and **R16** here, not A-rows, because each re-contracts a register that
callers leave unset today. The **bump** column says what the row needs on its
own: `PKG_FMT`, `DRV_VER`, both, or **none** (an `abilock.py --update` with no
bump, or a private version). The append-ready text is `synth/abilist-section.md`;
the table below is the planning view with the lens corrections spelled out.

### 2.T The API table's shape

**T5 - the rare cells behind one software-interrupt door.** Bump: both.
Phase 2. Status: proposed, the table row.

- **The mechanism.** A rare call becomes `int OS88_RARE / db index` at the
  site (3 bytes against 5) and the kernel keeps a 2-byte word per rare cell.
  The door fetches the index through the caller's CS:IP, bumps the stacked IP,
  picks the kind by index RANGE (the renumber sorts cells by kind, so no kind
  byte is stored), loads BP and enters today's `api_rs`/`api_rsc`/`api_rx`/
  `api_rxc`/`api_rn` bodies **past their `pop bp / mov bp,[cs:bp]` heads**
  (`kernel.asm:4588-4650`, `:4739`). It returns with `retf 2`, so CF/ZF
  answers survive. Hot SLOTs and the X, inline, jump and far cells are
  untouched. [Lt]
- **The tails.** `api_x.done` is shared with every hot `OSAPI_XCELL`
  (`jmp strict near api_x`), so rare X gets its own copy ending in `retf 2`
  (~13 bytes), and `api_rs.tail` becomes `retf 2` (+2); no non-door path may
  enter it afterwards, which binds P4's shims too. [Lt] [Lc] [Lf]
- **IF.** The caller's IF is restored from the stacked FLAGS by
  `push [bp+..] / popf`, the form SPEC.md's critical-section rule allows,
  not by a conditional `sti`. On kern_mega the gate is a TRAP gate, which
  leaves IF alone. Open: whether any rare cell is legitimately called with
  IF=0 (abi-8's tags answer it); "IF=1 callers only" drops the restore. [Lf]
- **The vector.** It is shared with DOS whichever way it is banked: windowed
  DOS's exchange set covers 00h, 08h-0Fh and 10h-3Fh (`apps/dos/dosw.inc:75-80`,
  `:114-121`), AH=25h writes the live IVT (`apps/dos/dos.asm:4481-4490`), and
  full-screen DOS banks the whole IVT (`:2956-2961`). So **DOS's AH=25h and
  AH=35h virtualise the door vector in both modes**, and the DOS world's copy
  of it is always the kernel's door. A direct IVT write by a DOS program is the
  residual risk an 8088 cannot fence. **The vector is named in 81h-FFh**:
  40h-5Fh are BIOS (relocated floppy, disk tables, EGA font, RTC alarm), 60h-80h
  is os8088 DOS's own packet-driver range (`apps/dos/dospktabi.inc:22-23`,
  which takes the first free vector there), 67h is EMS and 70h-77h the AT's
  slave PIC (the kernel hooks 74h, `kernel/mouse.inc:201`; kern_mega remaps it,
  `megados.inc:1922-1927`). It goes into SPEC.md and the interrupts page's
  anchors. [Lt] [Lf]
- **kern_mega.** An IDT gate (the 256-entry IDT exists, `mega.inc:87`; the
  IDT is built from the IVT, `:654-672`, so the gate's construction is
  specified). Package code selectors are readable (`0x9A`), so the index is
  fetched through the caller's CS. The real-mode FSX bracket
  (`fsx.inc:2337-2360`) refuses non-kept cells by filling the table with
  `stc`/`retf`; under T5 it must also point IVT[door] at a refusal ending in
  `retf 2`. [Lt] [Lc]
- **Refusal.** An index past this kernel's end answers CF=1, AX=1 (what
  `OSAPI_XREFUSE` answers today). This covers **rare indices only**: a newer
  HOT cell on an older kernel is still a wild far call past `osapi_table_end`,
  so A4 remains the guard for hot cells. [Lt] [Lc]
- **Bytes, re-derived.** Cells: 112 x (6 - 2) = -448 on kern_big and kern_small,
  119 x 4 = -476 on kern_mega (MEASURED counts). The door: an instruction
  sketch (bank DS and SI, `lds` the return, bump the IP, fetch and bound the
  index, a 10-byte refusal tail, the FLAGS restore, the table load, four range
  compares and five exits that each pop and jump) comes to about +75 to +95;
  the truth lens accepted ~+45, which needs the simplifications above (no IF
  restore, kind carried by the index) to be reachable. Tails +15. Two
  promotions (V7) +12. **kern_big and kern_small: about -325 to -345, -385 at
  the lean-door end. kern_mega: about -345 to -365 conv (-410 at the lean
  end), the gate's few bytes in `.cold` above 1MB.** [Lc], [s]
- **Packages.** -2 a rare site. The three censuses differ by rule: 2,641
  (cost lens), 2,736 (first synthesis), 2,767 (gfr-1) rare sites in `apps/` +
  `drivers/`, so **about -5.3 to -5.5KB unpacked**, less once packed (the far
  form's absolute operand already repeats under LZ). The largest: WIN16,
  RealDMG, Pixel, DOS, XTMP3, Video, Browser, Arcade, HDD.DRV, Scribe, each
  -140 to -380. A C package saves up to about -130 through the 65 rare sites
  in the thunks.
- **Cycles.** **+150 to +300 clocks a rare call, 31-63 us, PREDICTED.** The
  low end is gfr-1's per-step table; the high end is the cost lens's reading
  of a ~50-byte door at the 8088's fetch floor. A rare call also holds two
  more stack bytes for the body's life, and IRQs stay off ~100 clocks until
  the restore. The MartyPC reading belongs in the final pass. [Lc]
- **Who it breaks.** Every package, driver and C package (every rare site
  re-assembles, the rare set renumbers); the C SDK's thunks; `t_api_abi`'s
  decoder (which must also decode the index table and `CD nn idx` sites);
  `tools/os88wire.py` and `os88pkg.py`, whose highest-cell scan reads
  far-call immediates (`os88wire.py:120-140`); every test that hard-codes a
  cell; `kern_dos` and DOS.O88 (the vector); abi-4's frame reader (FLAGS now
  sits under CS:IP).
- **Sources.** bytes-1 [k], gfr-1 [g], abi-4 [k], loader-6 [k]; replaces T1
  and K17-1; challenge-truth, -cost and -fit (T5).

**T6 - one call spelling that can carry T5's flip, and the names normalised.**
Bump: none for the rewrite, none for the renames (SDK text). Phase 1 for the
mechanism, Phase 2 for the renames. Status: proposed.

- **Two candidate mechanisms, evaluate the second first.** (a) Rewrite every
  `call OSAPI_X` (about 6,960-7,000 sites, MEASURED by two counts) as
  `OSCALL OSAPI_X`. (b) Keep `call OSAPI_X` at every site and overload the
  mnemonic once in `os88api.inc` (`%imacro call 1`, guarded by `%ifid` and a
  per-cell kind `%define`). (b) touches no package source, causes no merge
  churn on in-flight branches and keeps every source gate's regex valid; its
  risk is whether NASM warns for a macro shadowing an instruction under
  `-w+error`, which is the first thing to check. [Lf]
- **Byte-identical until T5.** `OSAPI_*` names are `%define`s of
  `KERNEL_SEG:offset` (`os88api.inc:309-310`) and the tree has no `jmp`, `dw`,
  `dd` or `push` use of one, so either mechanism emits the same bytes and a
  `cmp` of every image proves the binaries. [Lt] [Lc]
- **What `cmp` cannot see.** (a) Host gates that parse the literal
  `call OSAPI_X` and would silently match nothing after mechanism (a):
  `t_textrules.py:50` (the transparent-text ratchet), `t_movable.py:76-89`,
  `t_stkclass.py:67`, `t_megahigh.py:109`, `t_pins.py:144, 201, 210`,
  `t_drvclaim.py:58, 157`, `t_toast`, `t_kdapi`. Each learns the new spelling
  in the same commit and is broken on purpose to show it still goes red
  (docs/WRITING-TESTS.md). (b) Macro-wrapped sites (`call %1` in
  `apps/os88ui.inc:6632`, `apps/win16/w16pm.inc:87, 261`,
  `apps/stiklib.asm:51`) that a textual rewrite misses: before T5's flip, a
  gate (`os88pkg.py`'s far-call immediate scan, extended) refuses any
  surviving far call to a rare offset. (c) After the flip a rare name must
  stop being a far-call address, or out-of-tree source assembles to a wild far
  call instead of failing. [Lt] [Lc] [Lf]
- **What it buys.** Only T5's flip: today every kind is already spelled the
  same 5-byte far call, so promoting a cell is already a rebuild; after T5 the
  per-cell kind attribute keeps it one. [Lc] [Lf]
- **The renames** ride the flag day with one cycle of `%define` aliases: CLIP
  (clipboard) against `WM_CLIP` (clip region), FS (file listing) against
  `FS_PROG`/`FS_OP`, DESK's three meanings, and the outliers of the
  SUBSYS_VERB shape (GET_TICKS, SET_COLOR, FULLSCREEN, SEG). Verbs move to AL
  only where another row reopens the cell.
- **Sources.** gfr-1 [g], abi-13 [u], abi-12 [c]; challenge-truth, -cost and
  -fit (T6).

### 2.C Duplicate doors and consolidations

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **C9** | **One file-position shape.** `OSAPI_FILE_WRITE_SEQ` takes `READ_SEQ`'s 16-byte caller cursor and buffer pair; the token becomes a cursor field, the HOLD stays the kernel's | 23 WRITE_SEQ sites in 12 files (DOS, Browser, Pixel, FTPD, XTMP3, Telnet, WaveRack, `os88wseq.inc`, RAMDISK.DRV, HDD.DRV's installer); the C SDK | 0 to -30 kern_big; ends `dws_rec`'s O(n^2) thrash between two concurrent write streams | both | histsys-13 [u], dw-22 [c] | **recorded** - wants a census of concurrent writers first |
| **C10** | **`OSAPI_DESK_ITEM` remove by record only.** AH's zone argument retired; remove names the row by volume + path, as Phase 1's additive `AH = 0FEh` verb already does | WIN16 (`w16progm.inc`), `os88stik.inc`, `drivers/wirezone.inc` (6 sites) | -6 `.cold` kern_big; WIN16 -12 (one call for a 16-probe loop) | both | shell-3 [c] | **proposed, low - fold-in only** |

### 2.R Register contracts and records

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **R9** | **One refusal sign, documented.** CF=1 is the only universal sign; AX means something only inside a named family; SPEC.md 18.4 gets the DOS-3-style class/action table. **Not** a universal AX value: AX=1 collides on `OSAPI_DRV_CALL` with `EMSE_ROOM` = 1 (`ems.asm:344`, asserted by `tests/emstest`), and holds its own meaning in `DRVE_DISK`, `LD_EDISK` and `FERR_NODISK`; Recorder reads the stream stub's deliberate AX=4 (`recorder.asm:754`); the bare `stc` stubs keep AX today and FM callers may rely on AL surviving a refusal (`snd.inc:714-717`, 50 `OSAPI_SND_FM` sites). The alternative (AX=0 universal, unused as a refusal in every family) is recorded with the census it needs (readers of XREFUSE's AX=1). kern_small's `OSAPI_VOL_STAT` stub, which breaks its own documented `FERR_NODISK` answer, is a Phase 1 bug fix with its own tail | nothing (the doc half) | 0 bytes; one stated rule. T5's past-the-end index answers what XREFUSE answers | none | abi-9 [c], gfce-8 [g], histsys-14 [u]; [Lt] [Lc] [Lf] | **recorded - the doc half now; the universal value refused as written** |
| **R10** | **Deferred damage, opt-in.** A per-class style bit (under R14) for "paint later"; damage into a global four-rect list drained at the top of the UI pass. Only a GLOBAL deferral changes `OSAPI_WM_SHOW`'s contract | opt-in: nobody | +80 to +100 resident; coalescing and a shallower nest on the UI task's stack. The safety half is ctx-1/histsys-1, now, no ABI | none (opt-in) / PKG_FMT (global) | histsys-10 [c], ctx-9 [g], histsys-1 [c] | **recorded - build it opt-in if at all** |
| **R11** | **32-bit ticks** as an appended `OSAPI_GET_TICKS32` (re-contracting the 329-site `GET_TICKS` to answer DX would break callers that keep DX) | nobody | +2 `.bss`, +~6 `.text` and a cell; lifts the 30-minute deadline bound | none | sched-19 [c], sysmisc-11 [u] | **recorded until a package asks** |
| **R12** | **One display-index register.** CH = display (0 = the caller's window's) on `OSAPI_FSX_CAPS`, `_RUN`, `_SURF` and `OSAPI_VIDEO`; FSX_CAPS's BX = 1..15 overload retired | 39 + 32 + 10 FSX call sites; `EXTF_CAPS` | -6 kern_big | PKG_FMT | video-19 [c] | **proposed, low - fold-in** |
| **R13** | **A claim's owner word is the instance slot** for everything a package holds | the Task Manager (`CLS_OWN`) | -20 to -40 `.cold`, not conv on kern_mega | PKG_FMT | mem-13 [c] | **recorded - ride C1 only if the Task Manager is touched anyway** |
| **R14** | **The window class lives in the package.** See below | | | both | | **proposed - replaces K17-3** |
| **R15** | **A format byte on the clipboard.** AL = format (0 = TEXT) on `OSAPI_CLIP_PUT`, `_GET`, `_SIZE`; anything else CF=1 until SPEC.md 55.1's second format arrives as data | 39 call sites must zero AL (up to +78 across packages); the C SDK | +8 `.text`; a second format without three new cells | PKG_FMT | ui-13 [u] | **proposed, low - only inside a flag day** |
| **R16** | **An extension filter on the Open form.** AH = filter; non-matching rows greyed (SPEC.md 47) and refused on commit | the Open callers of `OSAPI_FILE_DLG` (36 sites with Save) must zero AH; the C SDK | +~30 `.cold` kern_big; FDLG.DRV (module) on kern_small; not conv on kern_mega | PKG_FMT | files-18 [c] | **proposed, low, optional - a feature** |

**R14 in full.** Bump: both. Phase 2.

- **The change.** The record's handler words (13 on kern_big and kern_mega,
  10 on kern_small) become one `W_CLASS` word: a near pointer into the package
  image to {length byte, style word, handler offsets in a fixed order}.
  `wm_pkgcall` reads `[W_SEG:W_CLASS+n]` with DS already the package's. A slot
  past the length byte is "no handler", so an event nobody asked for still
  costs a package nothing. The length byte must cover every slot the LARGEST
  kernel reads, or a short table silently drops `W_ONWH`, the last one. [Lt]
- **Flags.** The creation-time flags become the style word's defaults; runtime
  changes go through K17-2's one `OSAPI_WM_FLAG` (AL set/clear, AH bit).
- **Runtime handler changes** (`os88stik.inc:987-1041`,
  `deskclok/dkset.inc:230`) point `W_CLASS` at a table of their own.
- **Kernel windows** (W_SEG = 0, `.cold` procs, `wm.inc:375-385`) share const
  tables, read through the live `[api_coldseg]` word (COLD_SLIDE moves
  `.cold` on mono kern_big), not through W_SEG and not through `COLD_SEG`; or
  the tables sit in `.text`, as abi-11 proposed. That one helper is also where
  BR-28's liveness re-test belongs. [Lc] [Lf]
- **kern_small's refusal signal is kept.** Today `OSAPI_WM_ONTIMER` and
  `_ONDRAG` answer CF=1 there and the SDK tells packages to test it and keep a
  second path (`os88api.inc:3267-3280, 4187-4194`; 1942 and Sheet do). Under
  R14 the setters vanish, so `OSAPI_WM_CREATE` answers in AL how many class
  slots this kernel dispatches (or CF when the class names one it cannot), at
  a few bytes. Otherwise a package arms a timer that never fires: the silent
  failure SPEC.md 47 forbids. [Lf]
- **No package reads a handler word through the record** (grep of `apps/` and
  `drivers/` for memory operands on `W_PAINT`..`W_ONWH`, outside
  `os88api.inc`). The kernel has 29 handler-field references to route through
  the helper. [Lt]
- **Who it breaks.** Every windowed package: the fixed 8-word `WT_*`
  template, 199 handler-setter and 107 flag-setter asm call sites, five in
  drivers (`saver`, `thmpal`, `hdd` x2, `ether`); `os88stik.inc`, `deskclok`;
  the C SDK's setter prototypes and thunks (open question 3).
- **Bytes.** kern_big: -288 `.bss` (12 words x 2 x 12 windows), -55 of inline
  setter cells (independent of T5), +~80 dispatch helper and kernel class
  tables, K17-2's one door replacing seven bodies of 129 MEASURED bytes
  (about -70 net), +~6 for kern_small's answer: **about -320 (range -260 to
  -360)**. kern_small: -108 `.bss` (9 x 2 x 6) and its fewer cells: **-95 to
  -120**. kern_mega: the same -288 `.bss` (conventional there) plus six
  `MEGA_ICWB` cells and their 11-byte stubs, with kernel class tables in
  `.cold` above 1MB: **about -350 conv**. Packages: **about -1.5KB unpacked**
  net (+~1.3KB of tables replacing ~2.6KB of setter code; abi-11's figure, not
  re-measured). Every later callback (ui-19's `W_ONMENU`) costs a class slot
  and no `.bss`. [Lc]
- **Cycles.** **+40 to +90 clocks a callback, PREDICTED** (a W_CLASS load, a
  length compare and an indexed load over today's `mov bp,[si+W_ONx]`; +50
  more if the lookup is a called helper rather than folded into
  `wm_pkgcall`). Negligible against a callback's body. [Lt] [Lc]
- **Sources.** abi-11 [k], histsys-9 [k], bytes-6 [c], gfr-3 [g];
  challenge-truth, -cost and -fit (R14).

### 2.D Driver header, classes and the driver tables

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **D4** | **The service-table floor, written down.** Every driver the `DRV_VER`-12 test admits already carries a full 36-byte DSV and 30-byte FSV table (`DSV_SIZE` last moved no later than `2ce1e37b`, 2026-09-08; `DRV_VER` moved twice since; `hdd.asm:1542-1573` lists all 18 cells, `ems.asm:485-487` is `DSV_SIZE` by construction and EMS keeps no copy). So the floor is the SDK saying so, plus an `%error` in `OS88_DRIVER` when its fourth argument is below it. The zero-fill and the clamp **stay**: they keep a longer table from a newer SDK out of an older kernel's copy, and they govern cells added past the floor later. They are in the `DRV_ATTACH` macro, expanded in `.boot2`, CTRL.DRV and HIBER.DRV, so none of their bytes is resident | nothing; no driver pads | **0 resident**; ~-36 a copy only if a later decision drops the clamp. Its value is that histsys-7 reads in place below the floor with **no per-read length test** (saving histsys-7's +50 to +110 of tests) | none | histsys-7 [k], drivers-9 [c], N1; [Lt] [Lc] [Lf], [s] | **proposed - NEEDS NO FLAG DAY (Phase 1, with N1 and histsys-7).** It makes D1 and drivers-9 moot. It reverses Phase 1's watch-list note that called the floor a bump |
| **D5** | **Typed driver slots instead of one slot per class** | every driver header, `OSAPI_DRV_CALL`'s AL, the Drivers page | 0 to -40 resident; a new file driver needs no constant | DRV_VER (+PKG_FMT for AL) | drivers-15 [c], gfr-6 [g] | **recorded** |
| **D6** | **A request block for `DSV_BLK` and the `FSV_*` verbs** (one dispatcher for `drv_blk_call_x`, `drv_fs_call`, kern_mega's `drv_fs_hi`); scoped away from DSV sound and IRQ0, where drivers.md's refusal of a request packet stands | five drivers' transfer paths (HDD, RAMDISK, CDROM, HTTPFS, NET's file side) | -60 to -120 resident, unmeasured; one status channel | DRV_VER | histsys-8 [c], gfr-6 [g] | **recorded** |
| **D7** | **Driver discovery** off `SYSTEM\*.DRV` headers; `SYSTEM.CFG` keyed by name | every driver row, `SYSTEM.CFG`, the Drivers page | -200 to -350 resident, +~120 `.boot2`. **Price on the target:** ~12 header reads at ~400 ms, about 5 s to open the Drivers page on an XT, unless titles are cached in `SYSTEM.CFG` | DRV_VER + SYSTEM.CFG | histsys-12 [u], gfr-6 [g] | **recorded, with that price** |
| **D8** | **The kernel owns the driver IRQ lines.** See below | | | none / DRV_VER | | **proposed - Phase 1 halves now; the flag-day half optional** |

**D8 in full.**

- **Phase 1 (no bump), one wording across the syntheses:** the IMR shadow
  (+~25) and drivers-6's release-time IVT scan (+25 `.cold`) now. The
  appended `OSAPI_IRQ` cell (refuse a held line, IMR shadow, LIFO-checked
  restore) is the owner's call: **+100 to +150 resident kern_big** (isr-13's
  16-row table is 112 `.bss` at 7 bytes a row, plus code), +~25 kern_small
  for the shadow alone; SOUND.DRV about -60 to -100 (MEASURED spans in
  `build/sound.hma.map`: `sbl_unhook` 46, `sbl_hook` 46, `sbl_cell` 28,
  replaced by two cell calls). [Lc] [Lf]
- **DOS.O88 is an IMR owner too**: full-screen DOS snapshots and restores
  ports 21h/A1h wholesale (`apps/dos/dos.asm:2989-2992, 3015-3022`), windowed
  DOS saves and selectively restores 21h (`dosw.inc:848-850, 1043-1046`), and
  a DOS program writes the PIC directly. The shadow must re-sync from port 21h
  after each DOS bracket, or DOS's restore goes through the cell. [Lf]
- **Flag day (DRV_VER): the cell is mandatory for drivers.** It binds drivers
  only. The image-move walk `mem_rr_ivt` (`memory.inc:3495-3505`) also covers
  int 33h (USBMOUSE's PicoMEM vector, not an IRQ) and vectors held by moved
  package regions (DOS.O88's int 08h chain), and kern_mega's
  `mem_ivt_relocf` walks it, so the walk is **replaced for driver IRQ lines,
  not deleted**, unless the cell also registers int 33h and package hooks
  (os88spk's IRQ0 bracket, DOS's exchange), which would need `PKG_FMT`. [Lt]
- **Bytes at the flag day:** about **0 on kern_big and kern_small** against
  today (the scan it would delete is a Phase 1 addition); **0 conv on
  kern_mega**, and up to -67 `.cold` above 1MB (`drv_hi_ivt`) only if int 33h
  is in scope. `mega_null0`/`mega_db` retire with L3's package side, not with
  this. [Lc] [Lt]
- **Cycles.** Vector-install-only costs 0 an interrupt. If the cell includes
  isr-13's common EOI/spurious stub, every hooked interrupt pays a far hop,
  +60 to +90 clocks (13-19 us); the stub is optional and stays off IRQ0 and
  the 8250 paths. [Lc]
- **Who it breaks.** SOUND.DRV (SB), USBMOUSE.DRV, ETHER/PCIETHER, EMS.DRV
  (`ems.asm:97-100`), and DOS.O88 (the shadow re-sync).
- **Sources.** isr-13 [c], drivers-6 [c]; challenge-truth, -cost and -fit (D8).

### 2.F Files on disk and names

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **F3** | **The compression hint moves off NTRes's case bits** (+12 is 5Ah/5Bh today; bits 3 and 4 make NT, Linux vfat and macOS list every packed file in lower case). Now: a dual-mark reader (+6 `.cold`) and `os88disk.py` writes a mark with bits 3 and 4 clear. Flag day: drop 5Ah/5Bh (-6) | no binary; every shipped disk is rebuilt anyway | +6 then -6 `.cold`, net 0; interop with every modern host | none | dw-10 [c] | **proposed - the reader now, the old mark dropped at the flag day** |

### 2.L Memory layout, segments and kern_mega

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **L6** | **Interrupts-on compaction with an ISR-reachable flag** (`GlobalFix`, `HLock`): unflagged blocks move with IF on; flagged ones at IF=0, as driver images do today | packages whose ISR or FSX rate hook touches a claim (WireBall's queue, the serial lease ring) | kern_mega's 64KB IF-off piece (~16 ms on the Victory 286) and `mx_alloc`'s `sch_lock` across a full pack end; +20 `.mega`, +10 to +16 `.cold` kern_big | none (opt-in) / PKG_FMT (if the default flips) | mc-5 [k], hm-1 [k] | **proposed as an ADDITION first** |
| **L7** | **L3's staged road: a "protected-mode clean" class bit** (Windows 3.0's NE `PROTMODE`), bit 5 of a PACKAGE header's +15. See below | nothing: old binaries keep the fixups | +12 `.mega`, kern_mega only | none | mc-19 [u], gfr-5 [g], mc-10 [c]; [Lt] [Lc] [Lf] | **proposed - NEEDS NO FLAG DAY** |

**L7 in full.**

- **The kernel half.** An out-of-range class is not a refusal
  (`instance.inc:2106-2116` masks with `0x3F` and gives the largest stack), so
  bit 5 on an OLD kernel costs only more stack. New kernels mask with `0x1F`
  (0 bytes, the same encoding) on every kernel, because `instance.inc` is
  shared; `sched.inc:2891-2893`'s `%error`, which pins the 6-bit field, moves
  with it. `mega_gpw` then CLOSES a clean package on a CS store (a bug)
  rather than completing it; mc-10's `[mega_kfix]` counter (+8 `.mega`, +2
  `.bss`, not built yet) proves the kernel's own stores.
- **The tool half.** `tools/os88pkg.py:211` fails any package whose
  `a[15] & 0x3F` exceeds `STK_CLASS_MAX`, so it must mask `0x1F` in the same
  diff. `os88pkg.py` writes shipped bytes (fork rule 6). `os88api.inc`'s
  prose gains a third flag bit: `abilock.py --update`, no bump.
- **Packages only.** A DRIVER's +15 is `DRV_H_DSV`, whose low six bits are
  the table length, and 36 = 100100b already sets bit 5. The drivers L3 needs
  (HDD.DRV/HDDTOOL's code-alias store, SB's and USBMOUSE's segment-0 hooks)
  need another header bit, which is a `DRV_VER` contract and rides the flag
  day's driver bump.
- **What "clean" forbids must be stated.** CS stores, yes. Writes through
  callback ES = KERNEL_SEG (Paint x4, `os88stik.inc` x2) are legal under
  today's callback contract, so a clean package may not be closed for them
  until the kernel hands a data alias in ES. Package BDA and IVT access (WIN16,
  Video, DOS.O88) needs a `MEGA_SEG0`-style SDK path before a clean package
  can forbid `mega_null0`'s fixup.

### 2.P Package-private and kernel-private ABIs

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **P3** | **The DOS launch block reorganised at the next `KDL_VER` bump**: a header with 16 reserved bytes, the body, a per-kernel tail named by a flag | `kern_dos` and DOS.O88 together | 0 bytes; the next field costs no hunt | KDL_VER (private) | md-24 [u] | **recorded** |
| **P4** | **T5's door rehearsed on the kernel's own `cw_` shims**, as a sixth door kind ("near body, DS already the kernel's"). 45 shim bodies and 23 zero-byte aliases (KERNEL-BYTE-QUEUE's 2026-11-08 status line; the map lists 70 `cw_*` symbols); the aliases stay far calls. It uses its own `retf 2` tail, not `api_rs.tail` | nothing shipped: modules are versioned with their kernel | **-45 to -90 `.text` kern_big**, plus -2 a site in `.cold` and in module images; +~170 clocks a call on module paths, which are cold by construction; sidesteps KERNEL-BYTE-QUEUE item 1's BP-liveness gate | none (kernel-private) | gfr-2 [g], bytes-7 [c], [Lf] | **proposed - NEEDS NO FLAG DAY; take it BEFORE T5** |
| **P5** | **One boot handoff record**: a 16-byte structure at a fixed low address for drive, partition base, geometry and loader id | the loaders and the kernel together | about neutral; a contract a test can read | none (private) | boot.md Rethinks (boot-5, boot-13) | **recorded** |

### 2.A Additions priced (no bump needed)

| id | what | buys | bump | source | status |
|---|---|---|---|---|---|
| **A4** | **A run-time API LEVEL: `PKG_FMT`'s minor number.** See below | load-time refusal of a package that needs newer cells | none | abi-6 [c], loader-6 [k], gfr-5 [g], abi-16 [c]; [Lt] [Lc] [Lf] | **proposed - NEEDS NO FLAG DAY; take it first** |
| **A5** | **The appended cells the flag day makes cheaper, recorded so none waits for it:** sched-15's `task_wake`, dr-5's `FILE_FIND_SEQ`, hm-9/mem-15's purgeable claims, ui-11's double-click byte, ui-12's clipboard generation, ui-19's `W_ONMENU`, video-12's `FSIF_FLIP`, shell-3's `AH = 0FEh`, D8's `OSAPI_IRQ` | under T5 an appended rare cell is 2 bytes; under R14 a new callback is a class slot | none | sched-15 [c], dr-5 [c], hm-9 [k], ui-11 [c], ui-12 [u], ui-19 [u], video-12 [c], shell-3 [c] | **recorded** |

**A4 in full.**

- **Kernel.** `dw OS88_API_LEVEL` at **`KERNEL_SEG:0006`**: free padding
  today (`kernel.asm:2349-2352`), 0 resident. Never written as a literal
  0060, because L1 moves `KERNEL_SEG` (to 0x01A0, after which 0060 is the FAT
  window). [Lt] [Lc] [Lf]
- **Package.** `os88pkg.py` writes the needed level into header byte 31. Byte
  31 is the NUL terminator of a legal 15-character name, so **names are capped
  at 14** (`os88pkg.py:122` and SPEC.md 20.2's header contract) and the
  validator's "no byte after the terminator" check (`:124`) exempts byte 31.
  The longest name built today is 13 characters, so nothing changes. The
  kernel's load path reads at most `LD_NAMELEN` = 12 (`loader.inc:56`);
  `inst_set_name_x` copies at most 15; an OLD kernel's caption scan counts up
  to 16, so a 14-character name with a level falls back to its stem there,
  visible and harmless. [Lt] [Lc] [Lf]
- **Load.** `ld_check_hdr` (`.cold`, `loader.inc:428`) refuses a level above
  the kernel's with `LD_EVER`: **+~8 `.cold` resident on kern_big and
  kern_small, 0 conv on kern_mega** (its `.cold` runs above 1MB). [Lc]
- **One source of truth.** The level is The Wire's: `WIRE_CELL_LEVELS`
  (`tools/os88wire.py:114-136`) and the SDK share one table, and
  `t_abilock` fails an appended cell that does not raise `OS88_API_LEVEL`;
  otherwise the level drifts the way bumps did before the lock. After T5 the
  level is computed from `CD nn idx` sites as well as far-call immediates. [Lc]
  [Lf]
- **A level, not `osapi_table_end`.** T5 takes the rare cells out of the
  table, after which a table end stops describing the API. **Phase 1's WP9
  as written puts `osapi_table_end` there with `OS88_NEED cell`; it must be
  changed to the level before it lands**, or every `OS88_NEED` compiled into
  packages changes meaning at T5. [Lf]
- **Scope.** It protects kernels that carry it, from then on. T5's run-time
  refusal covers rare cells only, so A4 is the only guard for hot cells.

### 2.V Speed

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **V7** | **T5's renumber sorts by MEASURED call frequency.** A rare call costs +150 to +300 clocks over a SLOT under T5, and a promotion +6 bytes, so the hot/rare line is drawn with a counter in the door (one rebuild). Promotion candidates: `OSAPI_SEG` (108 sites; kern_mega's derived-segment door, possibly per access) and `OSAPI_FS_OP` (45; a per-step progress call). Demotions: K17-4's thirteen. V2 rides the same renumber; V3's trio stays rare (7 sites). `OSAPI_GFX_FILL_GRAY` is already a hot SLOT | inside T5's renumber | ~30-60 us a call avoided on any cell that turns out hot; +6 a promotion, -6 a demotion | inside T5's | gfr-1 [g], bytes-1 [k], K17-4, V2, V3, [Lc] | **proposed - the reading is a prerequisite of T5** |

---

## 3. The recommended bundle

### 3.1 What lands first (Phase 1; no `PKG_FMT` or `DRV_VER` bump)

Each item is a gate the flag day needs, or the half of a row that does not need
the break. Each SDK-text item is an `abilock.py --update` without a bump.

| item | why the flag day needs it | bytes |
|---|---|---|
| **abi-3 / gkm-2**: `t_api_abi` over all four kernels | A renumber verified on one kernel of four is not verified; under T5 it must also decode the index table and `CD nn idx` | 0 (host) |
| **engsys-11**: `os88.h` and `os88thunk.asm` into `PKG_FMT`'s fingerprint and `t_mirror` | Section 0 item 4 says the C SDK moves in the same commit; no gate fails today when it does not | 0 (host) |
| **drivers-16** (P2): VX/XM/HD versions fingerprinted | A flag day that moves a private ABI then notices | 0 (host) |
| **abi-8**: `@ctx` tags on every contract | Lets the rebuilt tree be checked; answers T5's IF question | 0 shipped |
| **abi-4**: `API_RN_DSOFF` | T5 moves the frame by FLAGS | 0 |
| **A4**: the API level, with WP9 changed to match | Every kernel from then on refuses a too-new package, across T5's reshape | +~8 `.cold` big/small, 0 conv mega |
| **T6**'s mechanism, byte-identical; the source gates taught it and broken on purpose; the surviving-far-call gate | T5 becomes a flip; `cmp` proves the images | 0 shipped |
| **V7**'s reading: a counter in the rare path | The renumber's sort | 0 shipped |
| **P4**: the door on the `cw_` shims | Proves the door, the IF rule and kern_mega's gate on kernel-private traffic | -45 to -90 `.text` kern_big |
| **N1**, **D4** (the floor written down), **histsys-7** | Make D1 and drivers-9 moot without a renumber | N1 -7 resident (+ -4 a `DRV_ATTACH` copy, not resident); D4 0; histsys-7 -90 to -140 kern_big, -36 kern_small |
| **C1's correctness half**: BR-105 (the DMA-head guard; kern_dos's twin) and the ~20 internal KB callers moved to the paragraph entries | The bug is fixed whatever happens to C1, and C1's KB shifts can then go with the cells | BR-105 0 to +3 `.cold`, +1 to +4 kern_dos; the conversion about neutral, unmeasured |
| **L7** (clean bit, with `os88pkg.py` and the `%error`), **mc-10** (`[mega_kfix]`) | L3's staged road | +12 and +8 `.mega`, +2 `.bss` |
| **D8 phase 1**: the IMR shadow (with the DOS re-sync) and drivers-6's scan; the `OSAPI_IRQ` cell if the owner takes it | Lets the flag day make the cell mandatory | +~50; +100 to +150 kern_big more with the cell |
| **F3 phase 1** (dual-mark reader) | Lets the flag day drop the old mark | +6 `.cold` |
| **shell-3** (`AH = 0FEh`), **R9**'s doc half and class table, kern_small's `VOL_STAT` tail | Additive halves of C10 and R9 | +12 CTRL.DRV; 0; +3 to +4 kern_small |
| **abi-15 / engsys-20 / gfce-9**: an `APICHK=1` knob | The flag day's soak can run a checked kernel once, so each contract violation in the rebuilt tree is reported against the package that made it | 0 shipped |
| **L1's kern_small design** (paper, not code) | L1 cannot ride the bundle on kern_small until the region below a shared `KERNEL_SEG` holds fixed-size kern_small content | 0 |

### 3.2 The bundle, in order

One `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14, on one branch, with the C SDK in
the same commit as the SDK. The order renumbers the table **once**.

1. **Withdrawals**, so nothing renumbers twice: C1 (the six KB memory doors;
   packages convert through a shared helper), C2 (APPEND), C4' (APPEND_SYS
   alone), R1's bytes form, R14's setters, and any K17-4 demotion V7's
   reading supports.
2. **R14**: the class table, the `WT_*` template reshape, K17-2's one
   `OSAPI_WM_FLAG`, kern_small's dispatch-count answer. This deletes six of
   T2-2's kern_mega ICW stubs.
3. **Re-contracts**: R2 (snapshots bounded by the caller), R3 (volume bridge
   in ES:DI), R12 (display index), R15 (clipboard format), C10 (desk item by
   record), R2-4 and R2-5 (FSX_SPK and FSXF_REAL), V1's new rect entries, and
   L7's driver header bit. Optional: R16 (Open filter) and D8 made mandatory
   for drivers.
4. **T5**: sort the rare set by kind and by V7's frequency, renumber once,
   flip the call spelling's rare arm to the door, name the vector, virtualise
   it in DOS. V2 rides it; T6's renames land here with one cycle of aliases.
5. **L1** on kern_big and kern_mega (`KERNEL_SEG` 0x0060 -> 0x01A0, the FAT
   window and `dsk_secbuf` below the image), after T5 so that T5's
   `t_api_abi` diff is not confounded by every address moving. kern_small
   takes it only with the design from 3.1; kern_mega's figure counts only if
   its ladder is packed as well. A4's word moves with it to `KERNEL_SEG:0006`.
6. **Files on disk**: F1 (`'VC'` folded, Force Color lost once) and F3 (the
   old dirent mark dropped). kern_mega only: R2-1 (`mega_hibnc`, -235 `.mega`)
   and R2-3 (`MEGA_WTN` 32 -> 64, ~+128 of pool above 1MB).
7. **The decision and the gates**: `abilock.py --update` with both bumps; The
   Wire's level generation reset at 13 and the catalogue republished with
   packages of the new format (SPEC.md 92.2); `t_api_abi` on four kernels;
   `make budget-check` on all four budgeted kernels; then the soak, which is
   the owner's to ask for, ideally on an `APICHK` kernel.

### 3.3 Net bytes per kernel

Resident bytes, signed. kern_mega is conventional only. ESTIMATED unless
marked; a dash means the row does not reach that kernel.

| row | kern_big | kern_small | kern_mega conv |
|---|---:|---:|---:|
| **T5** + two promotions (V7) | -340 (-325..-385) | -340 (-325..-385) | -365 (-345..-410) |
| **L1** (bytes, paragraph-exact; 0-511 by build) | **-384** MEASURED pad | **not taken** (+924..+3,280 as written) | **0** (up to -128 if packed) |
| **R14** + K17-2's door | -320 (-260..-360) | -110 (-95..-120) | -350 |
| **C1** KB doors withdrawn (under T5) | -69 MEASURED spans | -69 | -12 |
| **C2** APPEND withdrawn | -11 | - | -2 |
| **C4'** APPEND_SYS withdrawn | -17 MEASURED | -3 | -12 MEASURED |
| **R1** REHOME KEEP form | -16 | - | -6 |
| **R2** snapshots bounded by the caller | +6 | +6 | +6 |
| **R3** volume bridge | -4 | - | -4 |
| **R12** one display index | -6 | - | -6 |
| **R15** clipboard format | +8 | +8 | +8 |
| **C10** desk item by record (`.cold`) | -6 | - | - |
| **F1** `'VC'` folded (+ -5 `.boot2`, transient) | -1 | - | -1 |
| **F3** old dirent mark dropped (`.cold`) | -6 | -6 | - |
| **V1** clobbering rect entries | -2 | -2 | -2 |
| **V2** yield enters directly | 0 (-60 clocks a yield) | 0 | 0 |
| **R2-4 / R2-5** FSX_SPK and FSXF_REAL | -3 | - | -25 |
| **T6** renames | 0 | 0 | 0 |
| **net** | **~-1,170 (-1,100..-1,260)** | **~-515 (-490..-570)** | **~-770 (-700..-870)** |

How to read the total:

- **kern_big**: about a third is L1's pad. With L1 at its ~256 average the
  net is about -1,040. `KERN_SIZE` moves in 512-byte steps and will report
  something else; per CLAUDE.md the byte figure is the one that decides.
- **kern_small**: L1 is excluded. Taken as written it would turn the whole
  kern_small figure into a cost (+924 at best). With a design that fills the
  region, the -304 pad comes back.
- **kern_mega**: also -235 `.mega` (R2-1) above 1MB, and L3's -1,271 to
  -1,627 `.mega` when L3 is taken.
- **Not in the figure**: R16 (+~30 `.cold` kern_big; FDLG.DRV on kern_small),
  D8 mandatory (~0), and every Phase 1 item.
- **Packages and drivers** (unpacked; packing shrinks each): T5 -2 a rare site,
  **-5.3 to -5.5KB**; R14 **about -1.5KB** net; C1 **+0 to +150** with a
  shared helper (+0.9 to +1.7KB if inlined); R15 up to +78; R16 (if taken)
  about +2 an Open call; C10 WIN16 -12. C3 is not taken, which avoids its
  +300.

**What changed from the first synthesis, and why.** T5's kern_small figure
rose to kern_big's (the decoded tables carry the same 112 rare cells) and the
door was re-priced from an instruction sketch; R14 rose (13 handler words a
record on kern_big, and the setter cells are inline, independent of T5); L1
left kern_small (the shared `KERNEL_SEG` leaves a hole there) and went to 0 on
kern_mega (512-byte rungs); D4 left the flag day (no driver pads, nothing
resident) and R9's re-contract was dropped (family codes collide on 1); C1
gained its `.cold` door bodies (MEASURED) and a package cost.

### 3.4 What stays out, and why

| row | why it is out |
|---|---|
| **L3** | Blocked on The Wire serving kern_mega builds (the owner's ranking). L7 and D8 start it now |
| **T1 / K17-1** | The fallback for T5 |
| **K17-3** | Superseded by R14 |
| **K17-4** | Only where V7's reading supports a demotion |
| **K17-5 / C7** | Recorded |
| **T2-2** (the four left after R14) | Refused: XT cycles against about 32 conventional bytes |
| **C3** | Refused, by arithmetic |
| **D1 / drivers-9** | Moot under histsys-7 + D4 (Phase 1) |
| **D4, N1** | Phase 1: no bump needed |
| **R9** (the universal value) | Refused as written; the doc half is Phase 1 |
| **R6 and wm-R3** | Shelved together |
| **R2-6 / hm-10** | Recorded |
| **R10, R11, L6** | Each has an additive form, so it needs no flag day |
| **R13** | Only if C1 converts the Task Manager anyway |
| **C9, D5, D6, D7, P3, P5** | Recorded |
| **V3, V4, V5, A1** | Recorded, or an addition waiting for a consumer |
| **F2** | Recorded (a user-visible rename) |
| **P1 and P2-1** | Private: ride any DOS release or kernel pass, not this bump |

### 3.5 Open questions for the owner

1. **T5's door.** A software interrupt for the rare half, at +150 to +300
   clocks (31-63 us) a rare call, with a vector the kernel owns against
   windowed and full-screen DOS, and a residual risk from a DOS program that
   writes the IVT directly? If not, T1 is the table row. (K17-1, its
   five-byte cousin, was declined before.)
2. **The IF rule.** Is any rare cell legitimately called with IF=0? abi-8's
   tags answer it; "IF=1 callers only" drops the FLAGS restore.
3. **R14 and the C SDK.** Should the C SDK emit a class table from the setter
   calls it sees, or should C packages declare one?
4. **L1 on kern_small.** Which fixed-size kern_small content fills the region
   below a shared `KERNEL_SEG`, or does L1 stay a kern_big and kern_mega row
   with kern_small paying the hole? And does kern_mega pack its ladder so that
   L1 is worth anything there?
5. **D8.** The `OSAPI_IRQ` cell now (+100 to +150 resident kern_big) or only
   at the flag day? isr-13 leaves it to the owner.
6. **L3 as product.** gkm-9 asks whether a one-disk 8088/286 set is a goal.
   That runs against the standing rule that no release artefact carries
   kern_mega's disks, so it is the owner's question, not a property of L3.

## Sources

- **Area data:** `docs/reports/big-review-2026-11-08/data/*.json` (25 areas and
  12 gaps; refuted findings dropped).
- **Area reports:** `areas/{abi,bytes,histsys,histmem,drivers,megacore,megados,ui,boot,wm,shell,files,video,memory,isr,loader,diskwrite,sched,gfx,engsys}.md`, findings and Rethinks.
- **Gap reports:** `areas/gap-flagday-reconcile-abilist.md` (the
  reconciliation), `gap-fault-containment-error-model.md`,
  `gap-context-lock-model.md`, `gap-kernel-matrix-near-model.md`.
- **First syntheses:** `synth/phase1.md` (WP4, WP9, WP22 and the watch list),
  `synth/bugs.md` (BR-28, BR-105), `synth/systemic.md`.
- **Lenses:** `synth/challenge-truth.json`, `synth/challenge-cost.json`,
  `synth/challenge-fit.json`, the flag-day entries.
- **Plans:** `docs/plans/ABI-BREAK-LIST.md` (all), `docs/plans/KERNEL-BYTE-QUEUE.md`
  (status line, item 1), `docs/plans/LAST-DROP-BYTES.md` 7.13.
- **Tree checks [s]:** the three maps' section summaries and the symbol spans
  quoted above; the rare-cell decode of `build/kernel-full.bin`,
  `build/smallk/kernel-full.bin` and `build/megak/kernel-full.bin`; the
  handler-setter cell bytes; `kernel/wm.inc` (record layout), `kernel/kernel.asm`
  (1125-1180, 1510-1575, 1795-1910), `kernel/mega.inc` (8-20),
  `kernel/driver.inc` (2975-2995, `DRV_ATTACH` and its three expansions),
  `tools/os88pkg.py` (115-125, 205-215); `git log -G` over
  `drivers/os88drv.inc` for `DSV_SIZE` and `DRV_VER`.
