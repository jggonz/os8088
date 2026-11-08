# Verify the unchecked and unmeasured claims that recommendations rest on, and name the rows that would measure them

Gap review, big review 2026-11-08. Tree at `4b9f1715`. kern_big unless a line
says otherwise. No emulator was run and `make` was not run, per CLAUDE.md's
Testing rules. One host-side assembly was done: `kernel/kernel.asm` with the
defines `build/kernel.kz.json` names, plus `-Le -l`, into the scratchpad. That
build matches `build/kernel-full.bin` in length (165,457 bytes). It differs in
151 bytes, which are the stamped `cfx_tab` and the build number. Every census
figure below is read off that listing, with `build/kernel.map` resolving
sections.

## Scope read

- Data: all 25 `data/*.json`. The 54 `unchecked` findings, the 100
  `corrected` ones, and every verify note that mentions an estimate.
- Areas: `bytes.md` (bytes-5), `diskread.md` (dr-6, dr-18, dr-19), `histsys.md`
  (histsys-1, 4, 5, 12, 13), `wm.md` (wm-R2, wm-R3), `loader.md` (loader-14),
  `histmem.md` (hm-1, hm-15), `megados.md` (md-21), `isr.md` (isr-7),
  `megacore.md` (mc-5).
- Code:
  - `kernel/kernel.asm`: 3700-3720, 5010-5200 (`api_copyname` and its
    fall-through entries), and 8420-8560 (`kent_*`/`kret_*`).
  - `kernel/disk.inc`: 1400-1500, 3954-4010, 4125-4150, 5761-6313,
    6725-6760 and 8340-8400.
  - `kernel/driver.inc`: 55-275, 1160-1260 and 4475-4500.
  - `kernel/memory.inc`: 1740-1790, 1988-2050 and 4630-4660.
  - `kernel/vga12.inc`: 1040-1068 and 1327-1425.
  - `kernel/mega.inc`: 10760-10835.
  - `kernel/megares.inc`: 459-520.
  - `kerndos/kdpark.inc`: 134-180.
  - `kerndos/kdhma.inc`: 185-230.
  - `drivers/xmem/xmem.asm`: 1356-1500.
  - `drivers/hdd/hdxms.inc`: 134-170.
  - `kernel/wm.inc`: 2260 and 5120-5150.
  - `kernel/loader.inc`: 1290-1310.
  - `kernel/diskw.inc`: 3350-3440.
  - `apps/os88api.inc`: 6095-6170.
  - `apps/bounce/bounce.asm`: 32 and 133-136.
- Docs:
  - SPEC.md: §1 rule 3, 18.4.8, 18.4.9, 18.8, 18.95 and 18.95.1.
  - `docs/plans/ABI-BREAK-LIST.md`: T, C, R, D, F and L.
  - `docs/plans/KERNEL-BYTE-QUEUE.md`: section 1.
- Tools: `tools/stkwater.py` and `tools/os88sym.py`. `tests/suite.py` rows
  `uilat`, `weavelat`, `stk0water`, `stkpanel`, `uiblock`, `schacct`,
  `regrowshed`, `serlease` and `curdisk`, and `tests/uilat.py`.

## Verdict

The load-bearing numbers mostly hold in mechanism and mostly shrink in
magnitude. That is the expected direction for estimates nobody priced against
code.

- **bytes-5's census is real.** Re-derived from a listing, the per-register
  push counts reproduce the reviewer's to within one: AX 514, CX 363, BX 348,
  SI 298, DX 276, DI 276, BP 222, ES 183, DS 176.
- **Its harvest is small.** About half of the 4,791 bytes are callee-saves at
  routine entry: 2,459 bytes in 612 routines. A prototype interprocedural
  liveness pass proves **79 of those saves (127 bytes, 93 of them in routines
  with their own pops) dead at every direct caller**. A further ~440 bytes are
  undetermined. The "AX is scratch" convention change is not supported:
  **247 of 512 bytes of AX entry-saves sit in routines where at least one
  caller reads AX after the call**.
- **The gate is worth having as a correctness lint, not as a size pass.**
  Its target is the 502 headers whose `clobbers:` line names registers.
- **dr-19 overstates.** There are two caches, not three: §18.95 already
  folded the directory window into the sector cache. The FAT-window machinery
  is measured at 519 bytes of `.cold`, and one pool nets about -100 to -300,
  not -300.
- **histsys-12 overstates.** The titles are already not resident (pass 14),
  so the resident saving is about -100 to -180. That overlaps drivers-10's
  -95 rather than adding to it.
- **wm-R2 understates.** It omits a per-window `.bss` rect.
- **wm-R3 overstates the breakage.** "Hundreds of places" is 121 field reads
  in 33 asm files, plus one C thunk family.
- **loader-14's opt-in clone is unsafe as written.** A running instance's
  initialised data is not pristine.
- **md-21 undercounts.** There are eight A20 bodies, not six: XMEM.DRV and
  HDD.DRV carry their own.

None of the deferred measurements has a row today that produces its number.
§ Rows names an existing row to extend, or one new row per WRITING-TESTS, for
each.

## Strengths

- **The register convention is already compressed where it is cheapest to
  compress.** The kernel's shared prologues and pop ladder (`kernel.asm`
  8446-8481, 8507-8550) cover 147 prologue calls (`kent_bp` 21, `kent_di` 22,
  `kentc_bp` 23, `kentc_di` 81) and 197 ladder tails. Each prologue banks six
  or seven registers for three bytes. None of those saves is in the 4,791-byte
  census, so the census does not count the convention's whole cost, only its
  uncompressed part.
- **The data a liveness gate needs is already written.** 807 routine headers
  in `kernel/` carry a `clobbers:` line. Several state their liveness argument
  outright, for example `api_copyname`'s *"SI and DI are dead at all four
  - ... A FIFTH call site has to read this"* (`kernel.asm` 5080-5090).
- **The `-Le` listing is a usable substrate.** With macro arguments
  substituted it is the whole kern_big instruction stream, conditionals
  resolved. A gate over it needs no new assembler and no new build. The
  prototype read it in about five seconds.
- **The sector cache's design record is unusually honest.** §18.95 was
  simulated before it was built, with a table of chunk sizes, and §18.95.1
  says what the first build got wrong. The dr-19 question can therefore be
  answered from the record rather than re-derived.

## Findings

### ul-1 (weakness, low, now) - bytes-5 re-derived: ~127 provable bytes, the convention change refuted, the gate justified only as a lint

**Census (re-derived, `-Le` listing + map sections):** explicit `push`/`pop`
of a GP or segment register in `.text`+`.cold`+`.vga`:

- 4,926 bytes counting CS/SS;
- 4,633 to 4,791 excluding them, depending on how sections are attributed.

The pushes per register match bytes-5 to within one. **Confirmed.**

**Of those, how many are callee-saves whose caller never reads the register?**

| class (entry callee-saves only) | saves | bytes (push + own pops) |
|---|---:|---:|
| routine is an API cell target (`OSAPI_*` in `kernel.asm`) | 148 | 294 |
| address taken / table / tail-jumped from an unresolvable site | 421 | 716 |
| at least one caller reads the register after the call | 456 | 794 |
| undetermined (an `int`, an indirect call or a `db` in the path) | 261 | 444 |
| no direct caller found | 38 | 84 |
| **dead at every direct caller** | **79** | **127** |
| total | 1,403 | 2,459 |

The method is a path-following walk from every call site, with one set of
rules:

- **Kills:** `mov`, `xor r,r`, `pop`, `les`/`lds` and so on.
- **Reads:** the register as an explicit or implicit operand, string ops,
  `mul`/`div`, `xlat`, `in`/`out`.
- **Saves:** a `push r` that its own `pop r` undoes.
- **Calls:** a `call` takes the callee's summary.
- **Returns:** a `ret` continues at the enclosing routine's callers, through
  tail jumps and fall-through entries, to depth 3.

The other ~2,300 bytes of the census are mid-body saves around calls and
loops. They are not this class.

Two qualifications cut the 127 further:

- **The ladder.** 34 of the 127 bytes are in routines that leave through a
  shared `kret_*`/`kretc_*` rung. A dead save in the MIDDLE of a rung (DX
  under SI, say) cannot be dropped without a different ladder. So **~93 bytes
  in 55 routines** is the realistic one-time harvest.
- **The arms.** The pass reads kern_big only. `mem_can_move` (`memory.inc`
  1988) has a third caller at `driver.inc` 4489 that only another arm
  assembles, so a gate must run per build.

**Sample of 30, checked by hand against the source.** The routines are
`sch_dphex2/4`, `wm_covered`, `wm_clip_split`, `wm_su_vset`, `wm_su_ck`,
`wm_su_merge`, `mem_pg_forget`, `mem_hifit`, `mem_can_move`, `mem_cp_step`,
`mem_pg_cheap`, `mem_claim_1`, `inst_park_req`, `inst_seg_parked`,
`menu_drop`, `assoc_stem_of`, `assoc_tryfold`, `dsk_runcap`, `dsk_rah_fill`,
`fm_setdown`, `fm_stage_name`, `fm_ultoa_x`, `fcp_lfree`, `os88ui_sbar`,
`db_draw_tile`, `cur_lazyend`, `cp_drv_gone_x`, `vga_blit_prow` and
`api_copyname`.

- 26 hold as dead on kern_big.
- **1 was wrong in the first pass and is the lesson.** `api_copyname`
  (`kernel.asm` 5184) has ONE direct caller but is reached by fall-through
  from `api_cpname0`/`api_cpname`, and AX is an argument at those sites
  (`kernel.asm` 3713: *"The AL survives the N stub because api_copyname banks
  AX"*). The second pass models fall-through and classifies it correctly.
- 1 is arm-dependent (`mem_can_move`, above).
- 2 are ladder rungs: `fm_ultoa_x` and `fm_stage_name`, `pops = 0`.

The other ladder entries, and an unconfirmed `gfx_pixel` read, are covered by
the next paragraph.

**`gfx_pixel` is a warning about the first pass.** The first pass also marked
`gfx_pixel` dead (vga12.inc 1053). It is `OSAPI_SLOT gfx_pixel` (`kernel.asm`
2651): an API cell whose callers are packages. The plain listing hides the
macro's `call %1`, so a gate that reads `nasm -l` without `-Le` misses every
macro-generated call.

**The convention change.** "AX and flags are scratch unless the header says
`preserves`" would not delete AX saves. It would move them. 247 of the 512
bytes of AX entry-saves are in routines with 226 caller sites that read AX
after the call. Only 39 bytes are dead everywhere.

**Is that enough to justify the liveness gate?** As a bytes tool, no. It is
one harvest of ~90-130 bytes, and the next pass "finding nothing" is the same
outcome without the gate. As a correctness lint, yes:

- **What it would check:** every `clobbers:` line among the 807 against every
  caller. A caller reading a register the header says is clobbered is a bug.
  A header claiming `clobbers: nothing` over a body that does clobber is a
  stale header.
- **The KERNEL-BYTE-QUEUE 1 reads:** it would answer them mechanically. Run
  over that table's routines, the prototype finds BP live after `cp_promise`,
  `fm_dgdrop` and `fm_choose` (so those shims stay), and leaves the other 16
  undetermined. Those 16 sit behind `int` or table dispatch, which is exactly
  what a hand read must resolve.
- **Cost:** 0 shipped bytes, about 300 lines of host Python, a soak-tier row.

**Recommendation:**

- Record the convention change as **refused on evidence**.
- Build the gate as `tools/os88live.py` over a `-Le` listing, per build
  (kern_big, kern_small and kern_mega listings). It must model:
  - fall-through entries;
  - `kent_*`/`kret_*`;
  - the `OSAPI_*` table;
  - `jmp`/`jcc` tail calls.
- Wire it as a soak row that checks the 502 register-naming headers and
  prints the dead-save list for the next size pass.
- Take the ~93 bytes in that pass, not as a project of their own.

### ul-2 (weakness, low, record) - dr-19 corrected: two caches, ~519 bytes of machinery, one pool nets -100 to -300 and costs kern_small

- **Count.** §18.95 (SPEC.md 39042-39048) already *"replaces §19.2.3's
  directory-only window"*. The sector cache and the FAT window are the two
  caches. Nothing else in `disk.inc` is a third.
- **Size.** Measured from `build/kernel.map`:
  - kern_big: `dsk_fatw_*` 386 + `dsk_bpb_sig` 26 + `dsk_fat_window` 107 =
    **519 bytes of `.cold`, resident**;
  - kern_small (`build/smallk/kernel.map`): 289 + 26 + 107 = 422.

  The sector cache (`dsk_rah_*`) is 513 bytes of `.cold` plus 19 of
  `.text`/`.bss` on kern_big, and 507 plus 5 on kern_small. "About 500" is
  confirmed for the window.
- **What one pool must add back:**
  - a dirty bit per slot and write-back through `dskw_flush_x` (it exists);
  - a pin while `dsk_next_clus_x` reads through ES:BX;
  - the DX/DX+1 straddle guarantee. A chunk reads FORWARD from the missed
    sector to the track's end (§18.95 rule 5), so a FAT sector that is the
    last on its track does not bring DX+1, which needs a second pinned slot;
  - a NON-purgeable floor, because today's cache is a claim the heap may
    refuse (`DIRW1`, §18.95.4) and the FAT window may not be refused (§18.8.3,
    *"the kernel's own window is one of them, PINNED"*).

  That is about +200 to +300, so the net is **-100 to -300 resident**, not
  -300.
- **kern_small.** Its window is two sectors (1KB), while a cache slot is a
  9-sector track (4.5KB). On the 128KB machine one pool either raises the FAT
  floor four-fold or needs variable-size slots.
- **ABI-BREAK-LIST L1.** L1 moves the FAT window *below* the kernel image. A
  unified pool and L1 are alternatives, not companions.

**Recommendation:** keep it **record**, with the corrected figure, and note
in L1's row that the pool and L1 are exclusive.

### ul-3 (weakness, low, flagday) - histsys-12 corrected: about -100 to -180 resident, and it overlaps drivers-10

The cited cost items are smaller than stated:

- **Titles: no resident cost.** "Titles instantiated twice (DRV_TITLES)"
  costs nothing resident. Kernel size pass 14 moved them into the blob and
  CTRL.DRV (`driver.inc` 1166-1172, *"77 resident bytes on kern_big"* saved).
- **Names and table:** the resident cost is 102 bytes of names
  (`drv_f_sound`..`drv_f_eth`, map 0x939A-0x9406, less `fdlg_s_drive`'s 6)
  plus `drv_tab` at 11 rows × 16 = 176 bytes (map 0x9438-0x94E8).
- **What discovery would remove:**
  - the names (-102);
  - `DRVR_FILE` and `DRVR_TITLE` in each row (-44);
  - perhaps one row (-16).

  Rows for loaded drivers must keep DISP/SEG/PARA/ENT/CLASS/WANT/ERR/ANS.
- **What it would add:** class-keyed lookups where code names a row by index,
  for example `driver.inc` 5642 `drv_tab+DRVR_SIZE+DRVR_WANT` for the hard
  disk, plus a heap or blob home for the found names.
- **Net:** about **-100 to -180 resident**, and +120 boot overlay as stated.
- **Overlap:** drivers-10 (**now**, -~95) takes the same 102 name bytes
  without a flag day. Do not add the two. After drivers-10, histsys-12's own
  residual is about -10 to -80, and its value is extensibility, not bytes.

### ul-4 (consistency, low, flagday) - histsys-13 confirmed, but the cursor/token split is deliberate; only the buffer pair is a wart

`READ_SEQ` takes the buffer at DX:BX and a 16-byte cursor at ES:DI.
`WRITE_SEQ` takes the buffer at ES:BX and a token in DI (`os88api.inc`
6119-6160). Both confirmed. The cursor-versus-token difference is the design,
and the section titles say so:

- §18.4.8 is *"a streaming read, whose place the CALLER keeps"*;
- §18.4.9 is *"a streaming append, whose place the KERNEL keeps"*.

A held write owns dirty FAT and directory state that cannot live in a
package's memory. The token is `[dsk_mgen]`, a generation number for one
kernel record (`diskw.inc` 3365-3371, 3429, 3503). Folding it into a caller
cursor deletes no kernel state.

What is left is the buffer convention. At a flag day `READ_SEQ` could take
the buffer at ES:BX and the cursor at DS:DI (the caller's own segment, the C
SDK's rule anyway). That gives every file slot the same buffer pair. About 0
bytes. It breaks 7 READ_SEQ files and none of the 15 WRITE_SEQ files.

**listed_as:** ABI-BREAK-LIST C2-C4 (neighbour).

### ul-5 (consistency, low, now) - md-21 corrected: eight A20 bodies, three probe addresses

The kernel-tree bodies are:

- `mega_a20`/`mega_a20s`/`mega_a20t` (`mega.inc` 10760-10835, 512 probes,
  alias FFFF:0510);
- `mr_a20` (`megares.inc` 459, 512);
- `mega_wrprobe` (`megados.inc` 2089, a single test);
- `kdp_a20w`/`kdp_a20t` and `kd_a20shut` (`kdpark.inc` 136-180, 512, alias
  **FFFF:FFF0**, because 0500 is kern_dos's own code on the HMA arm, 145-160);
- `kdh_tchk_src` (`kdhma.inc` 194, `xor cx,cx` = 65,536).

**Two more were not counted:** XMEM.DRV's `xm_a20_*` (`xmem.asm` 1356-1500,
`XM_A20_SETTLE equ 512`, alias `XM_A20_ALIAS`) and HDD.DRV's `hd_xa_a20`
(`hdxms.inc` 134, alias 0510). `mega.inc` 10792 even says its sequence is
XMEM.DRV's.

A shared macro must take the alias and the budget as parameters. It can sit
in an include the drivers share (`drivers/os88drv.inc`'s neighbourhood). 0
bytes. Its value is one budget and one probe written once.

### ul-6 (hazard, low, record) - loader-14 / hm-15 corrected: a clone of a running instance is not a pristine image

- **The idea.** loader-14 proposes an opt-in "pure image" bit so a second
  launch copies the first instance's region. The entry half exists already:
  `ld_start` takes an image *"out of a caller's claim"* (`loader.inc`
  1290-1299, OSAPI_PKG_START), so ~+25 `.cold` is plausible.
- **The hazard.** The first instance's region holds its MUTATED initialised
  data. `ld_start` zeroes only `.bss` (1303-1308). A clone would start with
  the first instance's variables. "Pure" would have to mean "never writes its
  own initialised data", which almost no package satisfies, or the loader
  would keep a pristine decoded copy, which costs the memory the clone saves.
- **The second read.** The loader's file read is a streaming read, which
  §18.95.1 sends around the sector cache, so a second launch re-reads the
  disk in full today.
- **The estimate.** loader-14's "~0.2 s of rep movsw" is arithmetic (about
  25 clocks a word: 0.2 s is ~75KB), not measured.

hm-15's shared-part registry stands as record. Shared PARTS are code-only by
construction, which is what makes them the safe variant.

### ul-7 (weakness, low, record) - wm-R2 corrected: the estimate omits the rect's `.bss`, and two deferral queues already exist

- **Bytes.** A per-window owed rect is 8 bytes × `MAX_WIN`: 96 `.bss` on
  kern_big, 48 on kern_small, on top of the +60..+100 drain code. So the
  estimate is about **+150..+200 resident**. A single global owed rect costs
  +8 `.bss` but over-repaints the union of disjoint damage.
- **Shape.** The window manager already defers two things:
  - closes, through `wm_close_pass` (`wm.inc` 2260, SPEC.md 75.2);
  - wakes, through the `WKQ_*` ring (`wm.inc` 2313-2315).

  A third queue should reuse `wm_close_pass`'s per-pass drain point rather
  than add a new one.

### ul-8 (weakness, low, flagday) - wm-R3 corrected: the breakage is ~121 field reads in 33 files, not hundreds

`grep -E '\+ *W_(X|Y|W|H|FLAGS|TITLE)'` over `apps/` and `drivers/` finds 121
reads in 33 asm files: W_W 30, W_FLAGS 27, W_H 24, W_Y 21, W_X 18, W_TITLE 1.
C packages read geometry through one thunk family (`CC_T_WFIELD`,
`apps/cc/os88thunk.asm` 845).

A handle model can leave those reads alone if the handle IS the record
offset and is validated at every door, which is wm-2's bounded load. Every
package would still be rebuilt for the doors' new contract, so the phase
stays **flagday**. The cost is countable, not "unestimated".

**listed_as:** pairs with ABI-BREAK-LIST R6 and K17-2/K17-3.

### ul-9 (weakness, low, now) - dr-6 not re-derived; the saving is small either way

The sequence is confirmed (`disk.inc` 1408-1420, `dsk_media_ok` 8359).
`dsk_media_ok` is about 30 instructions plus `dsk_vol_row_dl` (and
`dsk_vol_rmv` on `OS88_IMG`), so 600-700 cycles a transfer is plausible.
§18.95.1 prices a cache hit's copy at 1.6 ms a sector, so the overhead is
about 4-8% of a hit and ~50 us saved. It is a micro-optimisation behind
dr-16's sweep. The synthesis should not quote 250 cycles as measured.

### ul-10 (consistency, medium, now) - the deferred measurements: which row produces each number

None of these is to be run per wave. Each is named for the final pass.

| number | today | the row that would produce it |
|---|---|---|
| histsys-1: a dying 128-byte worker's slice depth over a second window | no row. `stk0water` reads task 0 only, `stkpanel` forces the panel, and `t_stkclass` is static | **new soak row `stkdie`** (MartyPC, `os8088_xt_vga` and the 5150 CGA twin). Open a second package, then Bounce (`OS88_STACK_128`, worker at bounce.asm:133). Close Bounce from its close box with the second window and the dock under it. Read the dead slot's 0xCC water with `tools/stkwater.water()` before the slot is reused, and breakpoint `sch_stkdie`. Red on purpose: drop Bounce's class or move the second window under it so the full `wm_paint_dmg` chain runs |
| histsys-4: click latency with N busy workers | `uilat` measures ONE Wire worker (7.3) | **a `--workers N` leg of `tests/uilat.py`** (`uilatn`, soak): the same `evq_tail` → `menu_ent` cycle bracket with 2 and 3 drawing instances. The N × 55 ms ceiling is arithmetic until then |
| histsys-5: how long workers are frozen per `int 13h`, and frames lost | no row. `curdisk` is the cursor and `viddiskcpu` is QEMU | **new soak row `xferfreeze`** (MartyPC): a drawing worker running while Note Pad opens a 16KB file. Breakpoint the `inc`/`dec [sch_lock]` in `dsk_xfer` (disk.inc 1462) for the frozen span in cycles. Read the worker's slice count across it with `schacct`'s accounting. The "~400 ms" is PERFORMANCE.md's per-call figure, measured, but the freeze is by reading only |
| hm-1: IF-off span of `mem_regrow` path 3 | no row. `regrowshed` drives the regrow on the 128KB machine but reads verdicts, not spans | **new soak row `ifoff`, leg `--regrow`**: `regrowshed`'s scenario arranged so the grow MOVES. Exec breakpoints at memory.inc 4559 (`pushf/cli`) and 4664 (`popf`), cycle difference. Until then "86 ms for 32KB" (memory.inc 2046) is arithmetic: 16K words × ~25 clocks |
| isr-7: the leased-UART rate cap from the cursor drawn at IF=0 | `serlease` checks that the lease SURVIVES, not overruns. Whether MartyPC's 8250 models LSR OE is unconfirmed | **the same `ifoff` row, leg `--mouse`**: `mou_isr_body` entry → its EOI, with the pointer moving through `os88mouserel` (motion with no destination, the short list's case). Max cycles over 40 packets on `os8088_xt_vga` and `os8088_5150_cga_gla`. The cap is 10 bit-times / span: about 4,100 baud on CGA and 3,100 on VGA from PERFORMANCE.md 1356's cur_move cycles. That is **predicted**, and it says 9600 (the lease default) overruns and 2400 does not |
| mc-5: kern_mega's 64KB piece copy at IF=0, and `sch_lock` across a pack | the 0.257 ms/KB rate is a FIELD measurement on the Victory 286 (LOADALL-PLAN). Bochs counts instructions and 86Box is not cycle-accurate | **field only** (docs/FIELD-MACHINES.md, Victory 286). 16 ms a piece and ~0.5 s per 2MB pack are arithmetic from the field rate and must be quoted as predicted |

### ul-11 (consistency, medium, now) - numbers the synthesis must not quote as measured, and corrected estimates that moved 30% or more

**Quote as predicted or arithmetic, never as measured:**

- bytes-5 "a few hundred": an upper bound. Use ul-1's ~93-127 proven.
- dr-19's -300: use -100 to -300.
- histsys-12's -200 to -350: use -100 to -180, overlapping drivers-10.
- dr-6's 250 cycles.
- loader-14's 0.2 s.
- hm-1's 86-170 ms.
- isr-7's rate cap: the 2.4 ms and 3.2 ms cur_move spans ARE measured
  (PERFORMANCE.md 1356); the baud ceilings derived from them are not.
- mc-5's 16 ms and 0.5 s.
- histsys-4's N × 55 ms.
- histsys-1's overflow: plausible, unmeasured. SPEC 8.7.4's ~90-byte font_run
  chain is the only measured term.
- wm-R2's +60..+100: use +150..+200.
- sysmisc-15's -1.5 to -3KB, and video-17's -1.1 to -1.5KB: both ESTIMATED,
  unprototyped.

**Corrected findings whose estimate changed by 30% or more.** Use the
corrected figure, never the reviewer's original:

| id | original | corrected |
|---|---|---|
| bytes-2 | XMS ~250 of ~500 | XMS ~110-130; total -250 to -320 |
| hm-11 | op_* spans 1.1-2.0KB (dosp 1,998) | -0.8 to -1.5KB per instance (dosp ~0.77KB) |
| histsys-7 | -216 .bss | net ~-90 to -140 (in-place reads need a DSV length test) |
| histsys-9 | kern_small ~-180 | kern_small ~-108 (10 words × 6 windows) |
| files-2 | -3 | ~-12 |
| hm-6 | -2 | 0 to +2 (sign flip) |
| ui-17 | -3 | +4 to +6 (sign flip) |
| ui-16 | +4 | +6 to +10 |
| isr-2 | +8 | ~+13 |
| sched-7 | "seed in the overlay, free" | +9..+12 `.text` (`.ovl` is full to 5,119 of 5,120) |
| dw-1 | +11 kern_big / +4 kern_small | both guards on both kernels (kern_small +4 → ~+11) |
| dw-7 | +6 | +6 per re-arm site, 6-18 |
| drivers-5 | 0 resident | +2..+4 `.cold` resident |
| sysmisc-9 | ~22 | +32 (measured, LAST-DROP-PERF.md 589) |
| sysmisc-3 | ~35 | ~40-60 CTRL.DRV |
| shell-2 | MOD_SETS 2,206 | 3,180 bytes |
| gfx-3 | kern_small ~170 clocks a band | ~20 clocks (CLIPQF is inline there) |
| files-11 | ~800 cycles a row | ~1,500 |
| gfx-1 | ~8,190 iterations (VGA) | 65,536 iterations sweeping VGABUF_SEG |
| isr-7 | 1.94 ms (that was gfx_lock/unlock) | 2.4 ms CGA / 3.2 ms VGA cur_move |
| ui-15 | "550 instructions" | ~1,150 code lines against ~1,890 comment or blank |
| sysmisc-12 | eleven sites | ~31 lines |
| abi-11 | setter census 219 | order-of-magnitude (asm-only counts are 25-40% lower); bytes unchanged |
| mc-9 | +1 | 0 |
| ul-1 (bytes-5) | "a few hundred" | ~93-127 proven |
| ul-2 (dr-19) | -300 | -100 to -300 |
| ul-3 (histsys-12) | -200 to -350 | -100 to -180, overlapping drivers-10 |
| ul-7 (wm-R2) | +60..+100 | +150..+200 |

**Moved less than 30%, so either figure is safe:** dr-9 (4 → 5), md-6
(950 → 1,025), md-19 (30 → 26), mc-16 (-81 → -70 net), video-1 (+11 → +13),
text-16 (-19 → -17) and ui-10 (81 → 79).

## Rethinks

- **A lint over the listing is the general instrument this review kept
  wishing for.** The same `-Le` stream answers several questions:
  - register liveness (ul-1);
  - KERNEL-BYTE-QUEUE 1's BP reads;
  - "which routines are reachable from an ISR" (isr-16's missing list: walk
    `call` edges from every vector);
  - the IF-state at each call (isr-12/gfx-10).

  GEOS shipped per-routine `destroyed:` lists and a checker. OS/2's MASM
  `USES` made saves declarative. Here the declarations exist (`clobbers:`);
  what is missing is only the checker. One tool, several gates, 0 shipped
  bytes.

## Prior art

- **GEOS:** `destroyed:` headers, checked by Esp (the GEOS assembler's
  preprocessor), the model for ul-1's lint.
- **Microsoft C / Windows 3.x:** AX, BX, CX, DX and ES scratch, the rest
  preserved. The data in ul-1 says os8088's callers already depend on AX
  preservation at 226 sites, so the Microsoft split would move bytes rather
  than save them.
- **DOS BUFFERS=:** one pool. It was affordable because DOS had no read-ahead
  and no per-volume parked FAT, which are the two things os8088's FAT window
  and sector cache buy (ul-2).

## Doc/code mismatches

1. **SPEC.md §1 rule 3** says: *"Every public routine **preserves all
   registers** except its documented outputs. Callers rely on this."* The
   code contracts on the header's `clobbers:` line. 502 of 807 kernel headers
   name registers that are clobbered and are not outputs, for example
   `api_copyname`'s *"clobbers: SI and DI, which the copy spends and does NOT
   put back"* (`kernel.asm` 5083-5086) and *"clobbers: AX, CX (internal
   helper)"*. Proposed: *"Every routine preserves all registers except its
   documented outputs and the registers its header's `clobbers:` line names;
   callers rely on that line, and a routine with no `clobbers:` line
   preserves everything. Every API cell's target preserves all registers but
   its documented outputs. Flags are not preserved."*
2. **CLAUDE.md and AGENTS.md, Hard rules**, say: *"Public routines preserve
   every register but their documented outputs."* Same code evidence.
   Proposed: *"Public routines preserve every register but their documented
   outputs and their header's `clobbers:` line (an API cell's target: outputs
   only)."*

## Open questions

1. Does MartyPC's 8250 model the LSR overrun bit? If it does, the `ifoff`
   `--mouse` leg can count overruns directly. If not, the cycle span and
   arithmetic are all an emulator can give, and the overrun itself is a
   field question.
2. Should the liveness lint run in fast or soak? It reads only `make`'s own
   kernel, but needs three listings (one per budgeted kernel). Probably soak
   per WRITING-TESTS 2.1, with a fast `--check` against the headers alone.
3. ul-1's prototype treats `int` as unknown. Most `int 13h`/`int 10h` sites
   preserve everything but AX and flags. A BIOS summary table would resolve
   a large share of the 444 undetermined bytes.
