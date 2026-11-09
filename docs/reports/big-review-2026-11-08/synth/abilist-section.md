## 13. Rows from the big kernel review (2026-11-08)

Folded in from the kernel review of 2026-11-08
(`docs/reports/big-review-2026-11-08/`): 25 area reviews and 12 gap reviews,
each verified, then three adversarial lenses (truth, cost, fit) over the
flag-day rows. The reconciliation against this list is
`areas/gap-flagday-reconcile-abilist.md`; the final plan, with every lens
correction spelled out, is `synth/flagday.md`. Nothing was built or booted for
it. **Every byte figure is ESTIMATED unless it says MEASURED** - the measured
ones come off `build/kernel.map`, `build/smallk/kernel.map` and
`build/megak/kernel.map` symbol spans and section summaries, and off a decode
of each kernel's API table in its `kernel-full.bin`. Cycle figures are
PREDICTED from the 8088's timing tables and fetch floor, never MartyPC
readings. "conv" is kern_mega conventional memory (`.text`, `.bss`,
`.lowbss`); kern_mega's `.cold` and `.mega` run above 1MB. kern_emu carries
kern_big's figure.

The rows add a **bump** column: what the row needs on its own - `PKG_FMT`,
`DRV_VER`, both, or **none** (an `abilock.py --update` without a bump, or a
private version). Ids continue this list's sequences.

### 13.1 What the review changes in the rows above

| row | now | why |
|---|---|---|
| **T1**, **K17-1** | one row, **superseded by T5**, kept as T5's fallback | the same five-byte design priced twice; T5 buys about three times as much |
| **T3** | **refused, unchanged** | under T5 seven of kern_small's thirteen refusal cells are rare and become 2-byte words to one shared refusal (-28), with rule 4 intact |
| **T4** | **refused harder** | a rare cell is 2 bytes under T5 |
| **T2-2** | **superseded in part** | six of the ten `MEGA_ICW` cells are handler setters, which R14 deletes; the four left (`SET_COLOR` among them) are **refused** - kern_big's inline cycles against ~32 conv bytes |
| **K17-2** | **proposed, reduced** to one runtime `OSAPI_WM_FLAG` beside R14 | creation-time flags become the class's style word |
| **K17-3** | **superseded by R14** | its ten setters are INLINE cells (decoded), and R14 removes them and the record words together |
| **K17-4** | **proposed, low** - part of V7 | -78 under T5 rather than -26, but each demoted cell then pays +150 to +300 clocks a call over a SLOT |
| **K17-5**, **C7** | one row, **recorded** (C7's status) | the same cell, `OSAPI_DRV_CALL_AT`; two sites, both FTPD |
| **C1** | **proposed** - a bytes-and-one-unit row, no longer a correctness row | three package doors carried the KB carry defect, two already guarded; the live one is a Phase 1 fix (`synth/bugs.md` BR-105, 0..+3), and the shifts sit in helpers the kernel's own ~20 callers keep. MEASURED: -69 kern_big and kern_small under T5 (36 of cells, 12 under T5, plus 57 `.cold` of door bodies), -12 conv kern_mega. Packages: +0 to +150 with a shared helper (+0.9 to +1.7KB inlined) |
| **C3** | **refused** | -6 kernel against +~300 across 154 call sites that would each need `mov al, mode` |
| **C4** | **proposed as `OSAPI_FILE_APPEND_SYS` alone** | it has no caller (HDD.DRV moved to `WRITE_SEQ` + `WSEQF_SYS`); WRITE_SYS keeps its two sites, so nobody zeroes AL. **-17 kern_big MEASURED**, ~-3 kern_small, -12 conv kern_mega |
| **R1** | **proposed, strengthened** | both forms still there, one KEEP caller; ~-6 conv on kern_mega (the `ld_start` share is `.cold`) |
| **R2-6** | **recorded, weakened** | kern_mega's forks are `.cold` (above 1MB); packages would pay +2..4 at 367 FREE/MOVABLE sites C1 does not convert. hm-10 folds in |
| **N1** | **proposed - Phase 1** | only the FSV fallback (7, `drv_fs_call`) is resident; the DSV one is in `DRV_ATTACH`, expanded in `.boot2`, CTRL.DRV and HIBER.DRV |
| **D1** | **superseded once histsys-7 and D4 land** (both no bump) | one copied table left, so `drv_svcix` has nothing to index; drivers-9 is moot with it |
| **F2-1** | **superseded - BUILT** | SPEC.md 20.13.9 (`LZ_ESC`), no container change, no bump |
| **L1** | **proposed for kern_big; kern_small and kern_mega need more** | MEASURED pads: kern_big -384 bytes paragraph-exact. kern_small: one `KERNEL_SEG` on every kernel, but kern_small's FAT is 2 sectors against 9, so the ten-sector region is ~3,584 bytes too big for it: +924 to +3,280 as written. kern_mega: no `LADDER_PACK`, 512-byte rungs, so 0 conv unless its ladder is packed (then up to -128, its 128-byte selector granule). Packages: ~150 `mov ax, KERNEL_SEG` sites rebuild |
| **L3** | **proposed - still blocked on The Wire**; figures restated | MEASURED: -1,271 `.mega` (`mega_gpfix` 602, `mega_gpw` 526, `mega_null0` 74, `mega_db` 69), -1,627 with `mega_strcal_gp`/`mega_strcal`/`mega_strfix`, plus -17 conv (`mega_tramp`) to ~-60. Its staged road is L7, D8 and mc-10's counter |
| **R2-1** | **proposed**, restated | `mega_hibnc` is 235 `.mega` today (MEASURED) |
| **P2** | **strengthened** | the VX/XM/HD versions should be fingerprinted by `abilock.py` (drivers-16) |
| **V1** | **proposed, refined** | a new entry beside the raw body (kernel callers rely on it preserving everything), +~6; C SDK half first, asm half behind a lint |
| **V2** | **proposed** | free inside T5's renumber |
| **section 11** | bullets 1 and 3 **done** | neither text greps any more |

### 13.2 New rows

**T - the API table's shape**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **T5** | **The rare cells behind one software-interrupt door.** A rare call becomes `int OS88_RARE / db index` (3 bytes against 5); the kernel keeps a 2-byte word per rare cell. The door fetches the index through the caller's CS:IP, bumps the stacked IP, picks the kind by index range (cells sorted by kind), and enters today's `api_rs`/`_rsc`/`_rx`/`_rxc`/`_rn` past their `pop bp / mov bp,[cs:bp]` heads; it returns `retf 2`, so CF/ZF survive. Rare X gets its own `retf 2` copy of `api_x` (shared with every hot XCELL) and `api_rs.tail` becomes `retf 2`. IF comes back from the stacked FLAGS by `popf`; on kern_mega a trap gate leaves it alone. **The vector is named in 81h-FFh** (40h-5Fh BIOS, 60h-80h os8088 DOS's packet driver, 67h EMS, 70h-77h the slave PIC) and DOS's AH=25h/35h virtualise it in windowed and full-screen DOS; a DOS program writing the IVT directly is the residual risk. kern_mega: an IDT gate, and the real-mode FSX bracket points the door at a `retf 2` refusal. An index past this kernel's end answers CF=1, AX=1 - **rare cells only**: a newer hot cell on an older kernel is still a wild far call, which A4 guards | every package, driver and C package (every rare site, and the rare set renumbers); the C SDK's thunks; `t_api_abi`; `tools/os88wire.py` and `os88pkg.py` (far-call scans); tests naming cells; DOS.O88 and `kern_dos` (the vector); abi-4's frame reader | MEASURED counts: 112 rare cells on kern_big AND kern_small, 119 on kern_mega. **-325 to -345 kern_big and kern_small** (-448 of cells, +75..95 door from an instruction sketch, +15 tails, +12 two promotions; -385 only with a leaner door), **-345 to -365 conv kern_mega** (-410 lean). Packages **-2 a rare site, ~2,640-2,770 sites: -5.3 to -5.5KB unpacked**. Cost **+150 to +300 clocks a rare call (31-63 us), PREDICTED** | both | bytes-1, gfr-1, abi-4, loader-6; `synth/challenge-*.json` (T5) | **proposed - the table row**; re-proposes K17-1's declined renumber with three new arguments (bytes, a run-time refusal, a rehearsal first in P4). T1 is the fallback |
| **T6** | **One call spelling that can carry T5's flip, and the names normalised.** Either every `call OSAPI_X` (~7,000 sites) becomes `OSCALL OSAPI_X`, or - evaluate first - `os88api.inc` overloads the mnemonic once (`%imacro call 1` on a per-cell kind `%define`), touching no package source; check that NASM does not warn under `-w+error`. Byte-identical until T5, so `cmp` of every image proves the binaries. The source gates that parse `call OSAPI_X` (`t_textrules`, `t_movable`, `t_stkclass`, `t_megahigh`, `t_pins`, `t_drvclaim`, `t_toast`, `t_kdapi`) learn the spelling in the same commit and are broken on purpose; macro-wrapped sites (`os88ui.inc`, `w16pm.inc`, `stiklib.asm`) are caught by a gate refusing any surviving far call to a rare offset before the flip. Renames ride the flag day with one cycle of `%define` aliases (CLIP vs `WM_CLIP`, FS vs `FS_PROG`/`FS_OP`, DESK, the SUBSYS_VERB outliers) | no binary; package source (the rewrite form only); out-of-tree source gets the aliases | 0 bytes. It buys only T5's flip: every kind is already the same far call today | none | gfr-1, abi-13, abi-12; `synth/challenge-*.json` (T6) | **proposed - the mechanism now (Phase 1), the renames at the flag day** |

**C - duplicate doors and consolidations**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **C9** | **One file-position shape**: `OSAPI_FILE_WRITE_SEQ` takes `READ_SEQ`'s 16-byte caller cursor | 23 WRITE_SEQ sites in 12 files, RAMDISK.DRV, HDD.DRV, the C SDK | 0 to -30 kern_big; ends the O(n^2) thrash between two concurrent write streams | both | histsys-13, dw-22 | **recorded** - wants a census of concurrent writers |
| **C10** | **`OSAPI_DESK_ITEM` remove by record only** (AH's zone retired; the additive `AH = 0FEh` verb comes first) | WIN16, `os88stik.inc`, `drivers/wirezone.inc` (6 sites) | -6 `.cold` kern_big; WIN16 -12 | both | shell-3 | **proposed, low - fold-in only** |

**R - register contracts and records**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **R9** | **One refusal sign, documented**: CF=1 is the only universal sign, AX means something only inside a named family, and SPEC.md 18.4 gets a class/action table. A universal AX value is **refused as written**: 1 is `EMSE_ROOM` on `OSAPI_DRV_CALL` itself (and `DRVE_DISK`, `LD_EDISK`, `FERR_NODISK`), Recorder reads the stream stub's AX=4, and FM callers may rely on AL surviving a bare `stc` refusal. AX=0 as the universal value is recorded with the census it needs | nothing | 0; a stated rule. kern_small's `OSAPI_VOL_STAT` stub, which breaks its documented answer, is a separate bug fix (+3..4) | none | abi-9, gfce-8, histsys-14; `synth/challenge-*.json` (R9) | **recorded - the doc half now** |
| **R10** | **Deferred damage, opt-in** (a class style bit; a global four-rect list drained at the UI pass's top) | opt-in: nobody | +80 to +100; coalescing, a shallower UI-task nest | none (opt-in) / PKG_FMT (global) | histsys-10, ctx-9 | **recorded** |
| **R11** | **32-bit ticks** as an appended `OSAPI_GET_TICKS32` | nobody | +2 `.bss`, +~6 and a cell | none | sched-19 | **recorded until a package asks** |
| **R12** | **One display-index register**: CH on `OSAPI_FSX_CAPS`, `_RUN`, `_SURF`, `OSAPI_VIDEO`; FSX_CAPS's BX overload retired | 81 FSX call sites, `EXTF_CAPS` | -6 kern_big | PKG_FMT | video-19 | **proposed, low - fold-in** |
| **R13** | **A claim's owner word is the instance slot** for package claims | the Task Manager | -20 to -40 `.cold` | PKG_FMT | mem-13 | **recorded - ride C1 only if the Task Manager is touched** |
| **R14** | **The window class lives in the package.** The record's handler words (13 on kern_big/kern_mega, 10 on kern_small) become one `W_CLASS` near pointer to {length, style word, handler offsets}; past the length is "no handler", and the length must cover every slot the largest kernel reads. Creation-time flags are the style word; runtime changes go through K17-2's one `OSAPI_WM_FLAG`; runtime handler changes point at a table of their own. Kernel windows' tables are read through `[api_coldseg]` (or sit in `.text`), in the one helper that also carries `synth/bugs.md` BR-28's liveness re-test. **kern_small keeps its refusal signal**: `OSAPI_WM_CREATE` answers how many class slots this kernel dispatches (ONTIMER and ONDRAG refuse there today and the SDK tells packages to test it) | every windowed package (`WT_*`, 199 handler-setter and 107 flag-setter asm sites, five in drivers); `os88stik.inc`, `deskclok`; the C SDK | **~-320 kern_big (-260..-360)**: -288 `.bss` (12 words x 12 windows), -55 inline setter cells, +~80 helper and tables, K17-2 ~-70, +~6. **-95 to -120 kern_small. ~-350 conv kern_mega** (the `.bss`, six `MEGA_ICWB` stubs). Packages ~-1.5KB unpacked. **+40 to +90 clocks a callback, PREDICTED** | both | abi-11, histsys-9, bytes-6, gfr-3; `synth/challenge-*.json` (R14) | **proposed - replaces K17-3** |
| **R15** | **A format byte on the clipboard**: AL = format (0 = TEXT) on `OSAPI_CLIP_PUT`/`_GET`/`_SIZE` | 39 sites zero AL (up to +78), the C SDK | +8; SPEC.md 55.1's second format with no new cells | PKG_FMT | ui-13 | **proposed, low - only inside a flag day** |
| **R16** | **An extension filter on the Open form** (AH; non-matching rows greyed, refused on commit) | the Open callers of `OSAPI_FILE_DLG`, the C SDK | +~30 `.cold` kern_big, FDLG.DRV on kern_small | PKG_FMT | files-18 | **proposed, low, optional - a feature** |

**D - driver header, classes and the driver tables**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **D4** | **The service-table floor, written down**: at `DRV_VER` 12 every driver carries full 36-byte DSV and 30-byte FSV tables (`DSV_SIZE` last moved no later than 2026-09-08 and `DRV_VER` has moved twice since; HDD.DRV and EMS.DRV are full too), so the SDK says so and `OS88_DRIVER` gets an `%error` below it. The zero-fill and clamp in `DRV_ATTACH` stay - they fence a newer, longer table and govern cells past the floor | nothing | 0 resident (`DRV_ATTACH` is in `.boot2`, CTRL.DRV, HIBER.DRV). histsys-7 then reads in place below the floor with no per-read length test | none | histsys-7, drivers-9, N1; `synth/challenge-*.json` (D4) | **proposed - NEEDS NO FLAG DAY**; with histsys-7 it makes D1 and drivers-9 moot |
| **D5** | **Typed driver slots** instead of one slot per class | every driver header, `OSAPI_DRV_CALL`'s AL, the Drivers page | 0 to -40; a new file driver needs no constant | DRV_VER (+PKG_FMT) | drivers-15 | **recorded** |
| **D6** | **A request block for `DSV_BLK` and the `FSV_*` verbs** (not for sound or IRQ0) | HDD, RAMDISK, CDROM, HTTPFS, NET's file side | -60 to -120, unmeasured | DRV_VER | histsys-8 | **recorded** |
| **D7** | **Driver discovery** off the `.DRV` headers | driver rows, `SYSTEM.CFG`, the Drivers page | -200 to -350 resident, +~120 `.boot2`; but ~5 s to open the Drivers page on an XT unless titles are cached | DRV_VER + SYSTEM.CFG | histsys-12 | **recorded, with that price** |
| **D8** | **The kernel owns driver IRQ lines.** Now: the IMR shadow (+~25, re-synced from port 21h after each DOS bracket) and drivers-6's release-time IVT scan (+25 `.cold`); the appended `OSAPI_IRQ` cell is the owner's call (+100 to +150 kern_big, SOUND.DRV -60 to -100). Flag day: the cell mandatory for DRIVERS. The image-move IVT walk is replaced for driver IRQ lines, not deleted - it also covers int 33h and package-held vectors (DOS.O88). Any common EOI stub stays optional and off IRQ0 and the 8250 paths (+60 to +90 clocks an interrupt) | SOUND.DRV, USBMOUSE.DRV, ETHER/PCIETHER, EMS.DRV; DOS.O88 (the shadow) | flag day ~0 on kern_big and kern_small, 0 conv kern_mega (up to -67 `.cold` above 1MB if int 33h is covered). Correctness: no vector left behind after detach, a kernel-computed `DRVE_BUSY` | none (append) / DRV_VER (mandatory) | isr-13, drivers-6; `synth/challenge-*.json` (D8) | **proposed - the Phase 1 half now; mandatory at a flag day, optional** |

**F - files on disk and names**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **F3** | **The compression hint moves off the directory entry's NTRes case bits** (5Ah/5Bh list every packed file in lower case on NT, Linux vfat and macOS). Now a dual-mark reader (+6 `.cold`) and `os88disk.py` writes a mark with bits 3 and 4 clear; at the flag day 5Ah/5Bh go (-6) | no binary; every disk is rebuilt anyway | net 0; interop with modern hosts | none | dw-10 | **proposed - the reader now, the old mark at the flag day** |

**L - memory layout, segments and kern_mega**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **L6** | **Interrupts-on compaction with an ISR-reachable flag** per claim (`GlobalFix`, `HLock`) | packages whose ISR or FSX rate hook touches a claim | kern_mega's 64KB IF-off piece (~16 ms on the Victory 286) ends; +20 `.mega`, +10 to +16 `.cold` kern_big | none (opt-in) / PKG_FMT (default flipped) | mc-5, hm-1 | **proposed as an ADDITION first** |
| **L7** | **L3's staged road: a "protected-mode clean" bit** in a PACKAGE header's +15 (bit 5). An old kernel answers it with the largest stack (`instance.inc:2106-2116`); new kernels mask `0x1F` on every kernel (0 bytes) and `mega_gpw` closes a clean package on a CS store. `tools/os88pkg.py:211` and `sched.inc`'s `%error` move with it. Packages only: a driver's +15 is its DSV length, whose bit 5 is already set by 36, so drivers need a header bit of their own at a `DRV_VER` bump. "Clean" forbids CS stores, not writes through callback ES = KERNEL_SEG, which stay legal until the kernel hands a data alias; package BDA/IVT access needs an SDK path first | nothing | +12 `.mega`, kern_mega only (+8 `.mega` and +2 `.bss` for mc-10's `[mega_kfix]`) | none (packages) / DRV_VER (drivers) | mc-19, gfr-5, mc-10; `synth/challenge-*.json` (L3) | **proposed - NEEDS NO FLAG DAY for packages** |

**P - package-private and kernel-private ABIs**

| id | what changes | who it breaks | what it buys | bump | source | status |
|---|---|---|---|---|---|---|
| **P3** | **The DOS launch block reorganised** at the next `KDL_VER` bump | `kern_dos` and DOS.O88 together | 0; the next field costs no hunt | KDL_VER | md-24 | **recorded** |
| **P4** | **T5's door rehearsed on the kernel's own `cw_` shims** (45 bodies; the 23 zero-byte aliases stay far calls), with its own `retf 2` tail | nothing shipped | -45 to -90 `.text` kern_big; +~170 clocks on cold module paths; proves the door before 2,700 package sites depend on it | none | gfr-2, bytes-7 | **proposed - NEEDS NO FLAG DAY; take it before T5** |
| **P5** | **One boot handoff record** (16 bytes: drive, partition base, geometry, loader id) | the loaders and the kernel together | about neutral; a readable contract | none | boot-5, boot-13 | **recorded** |

**A - additions priced (no bump needed)**

| id | what | buys | source | status |
|---|---|---|---|---|
| **A4** | **A run-time API LEVEL, `PKG_FMT`'s minor number.** `dw OS88_API_LEVEL` at `KERNEL_SEG:0006` (free padding today; never a literal 0060, which L1 moves). `os88pkg.py` writes the needed level into header byte 31; byte 31 is a 15-character name's NUL, so names are capped at 14 (the longest today is 13) and the validator exempts it. `ld_check_hdr` refuses a higher level with `LD_EVER`: +~8 `.cold` on kern_big and kern_small, 0 conv on kern_mega. One level table shared with The Wire's `WIRE_CELL_LEVELS`, and `t_abilock` fails an appended cell that does not raise the level. A level, not `osapi_table_end`, because T5 takes the rare cells out of the table: the review's Phase 1 plan (WP9) is changed to match before it lands | a load-time refusal of a package that needs newer cells; the only guard for hot cells after T5 | abi-6, loader-6, gfr-5, abi-16; `synth/challenge-*.json` (A4) | **proposed - take it first** |
| **A5** | **The appended cells the flag day makes cheaper**, recorded so none waits for it: `task_wake`, `FILE_FIND_SEQ`, purgeable claims, the double-click byte, the clipboard generation, `W_ONMENU`, `FSIF_FLIP`, `OSAPI_DESK_ITEM`'s `AH = 0FEh`, `OSAPI_IRQ` | under T5 an appended rare cell is 2 bytes; under R14 a callback is a class slot | sched-15, dr-5, hm-9, ui-11, ui-12, ui-19, video-12, shell-3 | **recorded** |

**V - speed**

| id | what changes | who it breaks | what it buys | source | status |
|---|---|---|---|---|---|
| **V7** | **T5's renumber sorts by MEASURED frequency**: a counter in the door (one rebuild) draws the hot/rare line. Promotion candidates `OSAPI_SEG` (108 sites) and `OSAPI_FS_OP` (45); K17-4's demotions; V2 in the same renumber; V3 stays rare (7 sites) | inside T5 | ~30-60 us a call avoided on any cell that turns out hot; +6 a promotion | gfr-1, bytes-1 | **proposed - a prerequisite of T5** |

### 13.3 The recommended flag day

**Before it (no bump):** `t_api_abi` on four kernels; the C SDK in `PKG_FMT`'s
fingerprint; the private ABI versions fingerprinted; abi-8's `@ctx` tags;
`API_RN_DSOFF`; **A4**; **T6**'s mechanism; **V7**'s reading; **P4**; **N1**,
**D4** and histsys-7; C1's correctness half (the DMA-head guard and the
kernel's own KB callers moved to paragraphs); **L7** and mc-10; D8's shadow and
scan (and the cell if the owner takes it); F3's reader; R9's doc half; an
`APICHK=1` knob for the soak; and a paper design for L1 on kern_small.

**The bump: `PKG_FMT` 10 -> 13, `DRV_VER` 12 -> 14** (section 0 item 2), one
branch, the C SDK in the same commit, the table renumbered once:

1. withdrawals - C1, C2, C4 (APPEND_SYS alone), R1's bytes form, R14's
   setters, V7-supported K17-4 demotions;
2. R14 with K17-2's one flag door;
3. re-contracts - R2, R3, R12, R15, C10, R2-4, R2-5, V1's new entries, L7's
   driver bit; optionally R16 and D8 made mandatory;
4. T5 with V2 and T6's renames;
5. L1 on kern_big and kern_mega (kern_small only with its design; kern_mega's
   figure only if its ladder is packed), A4's word moving with `KERNEL_SEG`;
6. files on disk - F1, F3; kern_mega only, R2-1 and R2-3;
7. the decision, The Wire's generation reset at 13 and the catalogue
   republished, `make budget-check` on all four budgeted kernels, and the
   soak when the owner asks for it.

**Net, resident, ESTIMATED** (L1's pad MEASURED today, 0-511 by build):

| | kern_big | kern_small | kern_mega conv |
|---|---:|---:|---:|
| T5 (+ V7's two promotions) | -340 | -340 | -365 |
| L1 | -384 | not taken | 0 |
| R14 + K17-2 | -320 | -110 | -350 |
| C1, C2, C4, R1 | -113 | -72 | -32 |
| R2, R3, R12, R15, C10, F1, F3, V1, R2-4/5 | -14 | +6 | -24 |
| **net** | **~-1,170 (-1,100..-1,260)** | **~-515 (-490..-570)** | **~-770 (-700..-870)** |

kern_mega also drops -235 `.mega` (R2-1). Packages: -5.3 to -5.5KB unpacked at
the rare sites (T5), ~-1.5KB from class tables (R14), +0 to +150 for C1 with a
shared helper, up to +78 for R15.

**Out of it:** L3 (until The Wire serves kern_mega builds); T1/K17-1 (the
fallback); K17-3; K17-5/C7; T2-2's remaining four; C3; D1/drivers-9; D4, N1,
L7 (no bump needed); R9's universal value; R6 with wm-R3; R2-6/hm-10; R10,
R11, L6 (additive forms); R13; C9, D5, D6, D7, P3, P5; V3-V5, A1; F2; P1, P2-1.

**For the owner:** T5's door and its DOS vector, or T1; whether any rare cell
is called with IF=0; how C packages declare a class; L1 on kern_small and
whether kern_mega packs its ladder; D8's cell now or at the flag day; and
whether a one-disk 8088/286 set is a goal (gkm-9), which bears on L3 and runs
against the rule that no release carries kern_mega's disks.

### 13.4 Sources

`docs/reports/big-review-2026-11-08/`: `areas/gap-flagday-reconcile-abilist.md`
(the reconciliation) and the other gap reports
(`gap-fault-containment-error-model.md`, `gap-context-lock-model.md`,
`gap-kernel-matrix-near-model.md`); the area reports `areas/abi.md`,
`bytes.md`, `histsys.md`, `histmem.md`, `drivers.md`, `megacore.md`, `ui.md`,
`wm.md`, `shell.md`, `files.md`, `video.md`, `memory.md`, `isr.md`,
`loader.md`, `diskwrite.md`, `sched.md`, `gfx.md`, `boot.md`; their verified
`data/*.json`; `synth/flagday.md` (the final plan), `synth/phase1.md`,
`synth/bugs.md`; and the three lenses `synth/challenge-truth.json`,
`challenge-cost.json`, `challenge-fit.json`.
