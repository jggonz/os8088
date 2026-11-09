# Reconcile the review's flag-day candidates into one bundle, row by row, against docs/plans/ABI-BREAK-LIST.md

Gap review, 2026-11-08. Tree as checked out; `build/kernel.map`,
`build/smallk/kernel.map` and `build/megak/kernel.map` read for spans. Nothing
was built or booted. **Every byte figure is ESTIMATED unless it says MEASURED.**
Cycle figures are 8088 clocks from the Intel timing tables plus the 8088's
four-cycles-per-word bus penalty. They are PREDICTED, not MartyPC readings.

## Scope read

- `docs/plans/ABI-BREAK-LIST.md`, all of it: section 0, 1, T, C, R, D, F, L,
  P, A, X, S, V, 10, 10.1 and 11. Also `docs/plans/KERNEL-BYTE-QUEUE.md` item 1.
- Every finding in `data/*.json` with `phase == "flagday"` (24) and every
  non-empty `listed_as` (about 50 more). The area text behind them:
  `bytes.md` (bytes-1, bytes-6, bytes-7), `abi.md` (abi-4, -6, -8, -9, -11,
  -12, -13, -16, -17 and the Rethinks), `histsys.md` (histsys-7, -8, -9,
  -10, -12, -13), `histmem.md` (hm-10), `memory.md` (mem-13, mem-18),
  `drivers.md` (drivers-6, -9, -15, -16 and the Rethinks), `loader.md`
  (loader-6), `megacore.md` (mc-19), `isr.md` (isr-13), `diskwrite.md`
  (dw-10, dw-22), `wm.md` (wm-R3).
- Code:
  - `kernel/kernel.asm`: 8-30, 2336-2390 (the fixed words), 2490-2606 (the
    cell macros), 2675-2705 and 4348-4459 (the table), 4590-4770 (`api_rx`,
    `api_rxc`, `api_rsc`, `api_rs`, `api_far`, `api_rn`), 4985-5020
    (`api_file_append_sys`), 1110-1170, 1640-1660 and 1770-1800 (the ladder).
  - `kernel/driver.inc`: 336-372 (the driver header), 1325-1350 (`drv_svcix`),
    2525-2700 (the three dispatchers), 2976-2996 and 4072-4086 (N1's two
    fallbacks), 1175-1260 (names and titles).
  - `kernel/memory.inc`: 34-160 (`MC_*`), 4399-4425 (`mem_find_own`).
  - `kernel/instance.inc`: 1239-1248 (`inst_set_name_x`), 2095-2120 (the class
    byte at +15). `kernel/sched.inc:2865-2881`.
  - `kernel/mega.inc` (whether a code selector is readable).
  - `drivers/os88drv.inc:760-801` (`OS88_DRIVER`). `drivers/hdd/inst.inc`:
    125-135, 980-990, 2040-2096, 2236-2250.
  - `apps/dos/dos.asm`: 2885-2961 and 18125-18140 (the IVT exchange).
  - `tools/os88wire.py:120-140`, `tools/os88pkg.py:115-123`,
    `tools/abi.lock.json`.
  - SPEC.md 20.13.9.
- Census, MEASURED by grep over `apps/` and `drivers/` (`.asm`/`.inc`): **6,998
  `call OSAPI_*` sites. 2,767 of them call one of the 117 rare-cell names.**
  `tests/` adds 1,303 sites. Per-cell counts are quoted below where they decide
  something.

## Verdict

The list and the review do fit together, but four of the review's proposals
**replace** rows instead of adding to them, and two rows were overtaken by the
tree without anyone noticing. Read together:

1. **bytes-1's software-interrupt door replaces T1 and K17-1 as the table
   row.** T1 stays as the fallback if the door fails its proof. The door costs
   more cycles than bytes-1 says: about **+150 to +215 clocks a rare call on
   the 8088 (30 to 45 us), not +80 to 100.** That is still cheap for the cells
   it serves, but four of them (`OSAPI_GFX_FILL_GRAY`, `OSAPI_SEG`,
   `OSAPI_FS_OP`'s step `fpg_stepb`, `OSAPI_WM_CURSOR`) need a frequency
   reading before the renumber. V3's XMEM trio does not: it has 7 call sites in
   the whole tree.
2. **abi-11 and histsys-9's class table replace K17-3.** K17-2 survives in
   reduced form, as one runtime `OSAPI_WM_FLAG` door. The creation-time flags
   become the class's style word. Six of T2-2's ten kern_mega ICW stubs are
   handler setters, so they go with K17-3 for free. The remaining four
   (`SET_COLOR` among them) are XT cycles worth keeping.
3. **histsys-7, a now row needing no bump, makes D1 and drivers-9 moot.** If
   every class but SOUND reads its table in place, there is one copy left. Then
   `drv_svcix` has nothing to index and no renumber is needed.
4. **Memory handles: no shared handle model.** Windows already have a stable
   handle (the record pointer, which never moves), and packages read it by
   contract, so wm-R3 is refused. Memory handles (hm-10, R2-6) stay recorded.
   On kern_mega their saving is `.cold`, which runs above 1 MB, so it is not
   conventional. On kern_big it is -30 to -60, against +2 to 4 bytes at each of
   367 FREE/MOVABLE sites. hm-2's now fix (+6 to 8) buys the correctness half
   alone.
5. **abi-6's flag-day half is not needed.** loader-6 found package-header byte
   31 free, because `os88pkg.py:122` already refuses 16-character names. The
   class byte at +15 also has free bits 3-5: an out-of-range class launches
   with the largest stack (`instance.inc:2107-2110`), which refutes abi-16's
   "refused as hostile". So the API level and mc-19's protected-mode-clean bit
   both go in **now**, with no `PKG_FMT` bump.
6. **F2-1 is already BUILT** (SPEC.md 20.13.9, `LZ_ESC`). **C4 is half
   dead:** `OSAPI_FILE_APPEND_SYS` has zero callers, because HDD.DRV moved to
   `WRITE_SEQ` + `WSEQF_SYS`. **K17-5 and C7 are the same cell** but carry
   opposite statuses.

The two batches are written out at the end of Findings (**Phase 1** and
**Phase 2**). Phase 2 is `PKG_FMT` 10 -> 13 and `DRV_VER` 12 -> 14, per
section 0 item 2. **Net: about -1,070 kern_big, -690 kern_small and -930
kern_mega conventional (plus -223 `.mega`).** These are ESTIMATED, and about
a third of each is L1's pad, which is MEASURED today but varies 0-511 from
build to build.

## Strengths

- **The list's section 0 is the right shape for a batch.** It names every
  surface a flag day touches: the lock, the kind fence, the C SDK, The Wire,
  hand-named addresses, files on disk, four kernels and the soak. Every
  reconciliation below could be checked against it rather than re-derived.
- **The version fence really is cheap to extend without a bump.** The
  equality test on `PKG_FMT` (loader.inc:295) is the major number. The tree
  already has three unread spots that can carry a minor without old kernels
  misbehaving, all checked in code:
  - `0060:0006-0007` (kernel.asm:2357-2359, "stay free");
  - package-header byte 31 (`os88pkg.py:122` refuses a 16-character name);
  - +15 bits 3-5 (`instance.inc:2107`, out of range gets the largest class
    and launches).
  The driver header already used the byte-31 trick (`DRV_H_BSSP`,
  driver.inc:359).
- **The rare-cell bodies are already the door's back half.** `api_rs`,
  `api_rsc`, `api_rx`, `api_rxc` and `api_rn` (kernel.asm:4596-4767) take the
  target in BP and touch nothing else. A door only has to produce BP and the
  same frame, so bytes-1 does not rewrite the X/N marshalling. This is why the
  door is a realistic row and not a redesign.
- **Package code never names a handler word through the record** (bytes-6's
  grep, re-run in spirit: the dynamic setters in `os88stik.inc:987-1041` and
  `deskclok/dkset.inc:230-232` go through cells). So moving handler storage
  breaks templates and setter calls but no package's data access.

## Findings

### gfr-1 - bytes-1's door replaces T1 and K17-1, at about twice the cycle cost bytes-1 quoted (rethink, high, flagday)

**What exists.** Today a rare cell is `push bp / call api_r<kind> / dw target`
(kernel.asm:2501-2506). Its path over a hot SLOT costs one `push bp`, one near
call, `pop bp` and `mov bp,[cs:bp]`: about **+78 clocks**. That agrees with
V3's "~85 cycles". bytes-1 replaces the 6-byte cell with `int nn / db index`
at the site (3 bytes against 5) and a 2-byte table word, for **~-400 on
kern_big and kern_mega, ~-250 on kern_small**. MEASURED census: **2,767 rare
call sites** in `apps/` + `drivers/`, so about **-5.5 KB unpacked across
packages**. The packed gain will be smaller, because the far form's absolute
operand already repeats per cell under LZ.

**The door's real cost on the 8088.** `int` (71) is about the price of today's
`call far` + `push bp` + near call (36 + 15 + 23 = 74), so entry is a wash.
What bytes-1 left unpriced is the work after it:

| step | clocks |
|---|---:|
| `mov bp,sp` and banking DS plus one scratch register | ~31 |
| `lds` of the return CS:IP | ~33 |
| fetch the index | ~16 |
| bump the stacked IP | ~32 |
| index the table | ~30 |
| choose the kind by range (2-4 compares) | ~30 |
| restore IF only if the caller's stacked IF was 1 | ~30 |
| unbank, then dispatch to the kind body | ~40 |

Less today's `pop bp / mov bp,[cs:bp]` (31) and plus `retf 2`'s extra (~3),
that is **about +150 to +215 clocks a rare call, 30-45 us**. bytes-1 said
+80-100 (~20 us), but it costed `int`/`retf 2` on 8086 timings and left the
middle out. PREDICTED. The rule-5 check belongs in the final pass on MartyPC.

**Is it still worth it?** Yes, for the cells that are rare by SPEC.md 20.3's
own definition. A window create, a claim, or a file call (~400 ms) does not
notice 40 us. But the renumber must sort cells by **measured frequency, not by
today's shape**:

- `OSAPI_GFX_FILL_GRAY` (30 sites) is a drawing call. +40 us is ~5% of a
  756 us primitive.
- `OSAPI_SEG` (108 sites) is the kern_mega derived-segment door that packages
  may call per access.
- `fpg_stepb` (`OSAPI_FS_OP`, 45 sites) is a per-step progress call.

Each of these should become a hot SLOT at the renumber (+6 bytes each under the
door) or have its frequency read first.

**V3 (XMEM_RUN/WALK/SEL to hot SLOTs):** under the door the rare penalty
rises from ~78 to ~255 clocks over a SLOT. But the trio has **7 call sites in
the whole tree** (RUN 2, WALK 2, SEL 3), and V3's own figure was ~0.1 ms a
second. Even tripled that is 0.03%. **V3 stays recorded.** The door makes
promotion cost the same +6 bytes it costs today, so the decision is
unchanged.

**K17-4 (13 not-hot SLOTs to rare):** under the door each one saves 6, not 2:
**-78, not -26**. But each also pays ~+170-215 clocks more than a SLOT per
call, against ~+78 today. The bytes triple and so does the cycle price, so
K17-4's existing prerequisite (a call-frequency reading per cell) binds harder.
It does not go away.

**T3 (kern_small's thirteen refusal cells):** seven of the thirteen are rare
cells:

- `wm_band`, `xm_alloc`, `xm_free`, `osapi_vol_stat` (RSLOT);
- `drv_cfg_x`, `drv_dlg_x`, `pkg_rehome_x` (RCXCELL).

Under the door each of these refuses with a 2-byte table word pointing at one
shared refusal body, against today's 6-byte `OSAPI_XREFUSE` inline cell
(kernel.asm:2601-2606). That gives **about -28 of T3's ~78 on kern_small**
automatically, and SPEC.md 20.8 rule 4 still holds: the index exists and
refuses. Past the table's end the door answers CF=1 instead of a wild far call,
which is the run-time half of loader-6. **T3's refusal stands for the six hot
cells** (`gfx_spans`, `gfx_blitp`, `snd_fm_x`, `drv_pkg_call_x`,
`mouse_feed`, and `desk_svc`, whose cell kind was not checked).

**T4 is refuted harder:** a rare cell becomes 2 bytes, and no multiplexed verb
is cheaper than that.

**Costs the door adds that bytes-1 did not list:**

1. **The SDK spelling has to change at every site, hot or rare.**
   `call OSAPI_WM_SHOW` expands to `call KERNEL_SEG:0x007C` and cannot be made
   to emit `int`. Grep found no `dw`/`dd`/`jmp` use of an `OSAPI_*` cell in
   `apps/` or `drivers/`, so the 6,998 sites are all `call`. They should all
   become one macro (`OSCALL OSAPI_X`) that emits the far call or the `int`
   per the cell's kind. Otherwise promoting a cell (V3, K17-4, the four above)
   becomes a source edit at every call site, and a cell's KIND becomes source
   ABI. The rewrite is a mechanical sed. abi-13's renames should ride the same
   pass, because it is the only time renames cost no extra churn.
2. **The tools that read call sites must learn `CD nn idx`.** `tools/os88wire.py`
   and `os88pkg.py` compute a package's highest cell from far-call immediates
   (`os88wire.py:120-140`). The same goes for `tests/unit/t_api_abi.py`'s
   decoder. This is section 0 item 6's list, plus a decoder each.
3. **DOS exchanges vectors while other tasks run.** Windowed DOS swaps "the
   program's vectors and BDA, and the desktop's" (`apps/dos/dos.asm:18125-18133`,
   `dvw_in`), and full-screen DOS banks the whole IVT (`:2956-2961`). Under
   pre-emption, a package that makes a rare call while a DOS program's world is
   in the IVT vectors into the DOS program. So the door vector must be
   excluded from the exchange set and refused or virtualized for a DOS program
   that tries to hook it. That is bytes-1's first hazard, made concrete.
4. **abi-4's frame reader moves by two more bytes.** The `int` frame has FLAGS
   under CS:IP, so `dwsf_dskw_write_seq`'s `[bp+10]` (`api_rn` comment,
   kernel.asm:4743-4746) must be re-proved. That is the reason to take abi-4's
   `API_RN_DSOFF` equate in Phase 1.
5. **kern_mega is fine.** Package code selectors are readable (`0x9A`,
   `mega.inc:24`, `:9906`), so the door can fetch the index through the
   caller's CS. An IDT interrupt gate at the same privilege is cheap on a 286.

**Recommendation.** At the flag day, take bytes-1 as row **T5** in place of T1
and K17-1 (merge K17-1 into T1, which is the same row). Sort the rare index by
kind and by measured frequency. Move the SDK to an all-cells `OSCALL` macro in
the same commit. Keep T1 on the list explicitly as the fallback if the DOS
vector or IF proof fails.

Bytes: **~-380 kern_big and kern_mega (conventional), ~-240 kern_small**.
That is -672 of cells, +224 of table, +~45 door, +~6 for the `retf 2` tails
and +~18 for three promotions. Packages: -2 a rare site.

### gfr-2 - the same door, opened to the kernel's own cw_ shims first, is a no-ABI rehearsal (rethink, medium, now)

KERNEL-BYTE-QUEUE item 1 is stuck on BP liveness: `cw_mem_disp` needs the body
in BP. Its "alternative nobody has priced" is an inline-operand trampoline,
which bytes-7 priced at about zero (7 bytes a site against 8).

The door is the better inline trampoline. It reads its operand from the
return address, so **it clobbers no register at all**, which deletes the
liveness question for every remaining site. And it is 3 bytes a site instead
of 5 + a 4-byte shim. `build/kernel.map` lists **70 `cw_*` symbols**. As a
sixth door kind ("near body, DS already the kernel's"), each shim becomes a
2-byte table word: **about -140 `.text` kern_big**, plus -2 at every site in
`.cold` (resident) and in module images (not resident). The cost is ~+170
clocks per call on module paths (fm, desk, ctrl), which are cold by
construction.

**Why now and not at the flag day:** modules are versioned with their kernel
(P2), so this needs no bump. And it builds and proves the door (the DOS vector
ownership, kern_mega's gate, the IF rule) on kernel-private traffic before
2,767 package sites depend on it. If the door then fails, nothing shipped
depends on it and T1 is still there.

Bytes: **~-140 `.text` kern_big** (ESTIMATED; the shim census should be
re-taken first, because the queue says 90 and the map lists 70). Listed as:
KERNEL-BYTE-QUEUE item 1 (supersedes its trampoline alternative).

### gfr-3 - abi-11's class table replaces K17-3; K17-2 survives as one runtime flag door (rethink, medium, flagday)

The two designs are **not compatible as written**. K17-3 keeps ten handler
words per record and folds their setters into one indexed cell. abi-11 and
histsys-9 remove the words and the setters together. Taking K17-3 first would
spend a renumber on a cell the class table then deletes. **Pick abi-11 for the
handlers.**

The flags are different. SIZABLE, SNAP, SAVEU, OWNBG, KEEPH, NOANIM and QUIET
take AL = 0 clear / non-zero set (os88api.inc:805, 810, 5247, 5844, 5862,
6776). They are per-window state a package may toggle, and a constant class
table cannot hold a toggle. So:

- **the class's style word is the creation-time default** (Win16's `WS_*` at
  `CreateWindow`), absorbing the init-time calls among the 128 flag-setter
  sites;
- **runtime changes go through K17-2's single `OSAPI_WM_FLAG` (AL = set/clear,
  AH = bit)**.

Runtime handler changes exist and fit the table: `os88stik.inc:987-1041`
installs handlers from variables per Sticky App, and `deskclok/dkset.inc:230`
from a table. Both can build a class table in their own data, one per window.
The setter cells kern_small answers CF=1 to (ONDRAG, ONTIMER; os88api.inc:4179-4186)
disappear outright, because kern_small simply never reads those slots of the
class.

**T2-2 interplay, MEASURED in source:** kern_mega's ten `MEGA_ICW*` inline
cells are:

- six handler setters: `W_ONSIZE` 2867, `W_ONSZ` 3349, `W_ONWK` 3619,
  `W_ONRC` 3715, `W_ONCLICK` 4188, `W_ONWH` 4319;
- four value setters: `gfx_color` 2696, `osapi_seed` 2701, `gfx_b1ink` 3762,
  `ico_c1` 3778.

So **abi-11 takes six of T2-2's ten for nothing**. The remaining four include
kern_big's 52-cycle inline `SET_COLOR`, which T2-2 itself calls "the XT's
cycles against kern_mega's conventional bytes". At ~-32 left, **refuse the
remainder.**

Bytes, under the door (cells at 2 not 6), ESTIMATED: kern_big **~-180 to
-250** (record words -216 `.bss`, ~16 cells withdrawn ~-32, +~40 dispatch,
+~40 kernel class tables, K17-2's one door replacing seven bodies of 129
MEASURED bytes). kern_small ~-80 to -110. kern_mega ~-200 to -280
conventional (the six ICW stubs, 11 bytes each, go too; kernel class tables
in `.cold` are above 1 MB there). Cost: ~20 clocks a callback for the
indirection.

### gfr-4 - one handle model: refuse it; windows already have one, memory keeps its record (rethink, medium, record)

The four proposals are hm-10, R2-6, wm-R3 and R6's pairing.

**Windows.** `BX` is a pointer to a record in `wm_wins` that never moves while
the window lives. It is already a stable handle. What an index would add is
validation and layout hiding. But the record is **readable by contract**
(os88api.inc:4188, "read and write it with an override", and abi-7's census of
packages that read it), so hiding it breaks every package that reads `W_X` and
friends. Validation is available without a handle: `wm_isrec` is three
instructions (histsys-11, +6 to 12). An index would also turn every inline
setter cell (`[cs:bx+W_*]`, kernel.asm:2824) into a translating stub, which is
bytes on every kernel. **wm-R3: refused while R6 is shelved; re-raise it only
with R6.**

**Memory.** `mem_find_own` (memory.inc:4399-4425) walks 16 or 32 records. On
kern_mega it forks per record (`MEM_VAL`). hm-10's handle = the record OFFSET
(`MC_SIZE` 10, rows never move) would be a zero-translation handle, the same
idea as the window pointer. But:

- **kern_mega's measured forks (+48/+97/+42/+88/+47) are `.cold`, and
  kern_mega's `.cold` runs above 1 MB** (CLAUDE.md, rule 4: `MEGA_RESIDENT`
  excludes `.cold` and `.mega`), so the "several hundred" are not conventional
  bytes;
- kern_big gains -30 to -60 `.cold`;
- packages pay +2 to 4 bytes at each of **367 FREE/MOVABLE sites** (247 + 120,
  MEASURED) plus a word per claim, and C1 does not convert those sites (C1
  touches the claim/avail/regrow doors, 221 sites).

So "one conversion, not two" is wrong: these are different sites. The
correctness half (hm-2's exact owner fence) has a now fix at +6 to 8.
**hm-10 folds into R2-6, which stays recorded.**

### gfr-5 - the API level goes in now, in two places that are free, and abi-6's flag-day half dissolves (weakness, high, now)

abi-6 puts the kernel's level at `0060:0006`: free padding, zero bytes
(kernel.asm:2357-2359), and no reader in `boot/`, `kernel/` or `tools/`
(`[es:6]` in `hiber.inc`/`megados.inc` is a different segment). abi-6 then
says the package's needed level must "ride the flag day's header change"
because "the header has no free word" (abi-16). **loader-6 shows byte 31 is
free:** `inst_set_name_x` copies 15 characters (instance.inc:1242), and
`os88pkg.py:122` already refuses a 16-character name. So every package ever
built has 0 there.

Reconciled design:

- **Publish a LEVEL, not `osapi_table_end`.** Under gfr-1 the rare cells
  leave the table and become indexes, so a table end would stop describing
  the API at the flag day. The Wire already numbers levels (`WIRE_LEVEL = 7`,
  `WIRE_CELL_LEVELS`, os88wire.py:120-138).
- **Kernel side:** `dw OS88_API_LEVEL` at 0060:0006, 0 resident.
- **Package side:** `os88pkg.py` computes the needed level (as the Wire
  already does) into byte 31. `ld_check_hdr` compares it `<=` and answers
  `LD_EVER`, ~+8 `.cold`.
- **SDK:** `OS88_NEED level` for a package that wants to test at run time
  instead.

Old packages carry 0 and pass. **No `PKG_FMT` bump**: a lock update with no
bump, because the SDK prose moves. Under gfr-1 the door adds the run-time half
for rare cells (an index past the end returns CF=1).

**abi-16 is refuted for now.** Byte 31 and +15's bits 3-5 are free.
`instance.inc:2106-2110` says an out-of-range class is **not** a refusal and
launches with the largest stack, so a bit there costs an old kernel nothing a
package with class 0 does not already get. mc-19's protected-mode-clean bit
fits there (+12 `.mega`, kern_mega only). That is L3's staged road, and it is
additive too.

### gfr-6 - the driver cluster: histsys-7 now; D1 and drivers-9 moot; histsys-8 scoped; drivers-15 and histsys-12 recorded (rethink, medium)

- **histsys-7 (now, no bump):** `drv_svc` is `DSV_SIZE*(DRVC_MAX-3)` =
  252 `.bss` (driver.inc:6173). Read non-SOUND tables in place through the
  published segment. Net **~-90 to -140 kern_big**, ~-36 kern_small. Once only
  class 1's copy is left, `drv_svcix` (10 bytes, driver.inc:1344-1345, plus its
  `%error`) is a compare against 1. **That is D1's whole purpose (-6 to -10),
  done without a renumber.** drivers-9 (reordering `DSV_*` to shrink six copies,
  -98) has nothing left to shrink. **D1: superseded if histsys-7 lands.
  drivers-9: refused, moot under histsys-7.** If histsys-7 is rejected, D1 +
  drivers-9 remain one Phase 2 row together.
- **N1 (now, no bump), re-checked:** the two fallbacks are still there
  (driver.inc:2989-2991: `or ax,ax / jnz / mov ax, FSV_TAB0`, one copy shared
  by both HMA loads; and 4079-4080: `jnz / mov cl, DSV_TAB0` after a mask that
  stays). That is 7 + 4 bytes. It fails closed: a zero length refuses every
  verb or copies nothing. **Phase 1.**
- **histsys-8 against drivers.md's "Not recommended: a DOS-style request
  packet".** They conflict only if histsys-8 reaches `DSV_*`. drivers.md is
  right for the sound and IRQ0 path. histsys-8 is scoped to `DSV_BLK` and
  `FSV_*`, where a transfer is ~400 ms and packet building is noise. The
  saving (two staged words, `drv_blkcls`, one dispatcher, kern_mega's
  `dfh_*`) is unmeasured. The work lands in five drivers' transfer paths (HDD,
  RAMDISK, CDROM, HTTPFS, the file side of NET), and the async payoff has no
  consumer. **Record as D6. It is not in Phase 2.**
- **histsys-12 (driver discovery) is half done already.** `OS88_DRIVER` writes
  a 15-character name at +16 (os88drv.inc:798-801), and the shipped names are
  the titles: `OS88_DRIVER 'Sound', ...` against `DRV_TITLES`' `'Sound'`
  (driver.inc:1237-1257). So the resident titles duplicate the files. But the
  unpriced half is on the target. A Drivers page that reads titles off the
  files reads one header per `.DRV`: ~12 `int 13h` calls at ~400 ms each,
  **about 5 s to open the page on an XT**, unless the titles are cached in
  `SYSTEM.CFG`. **Record as D7, with that price.**
- **drivers-15 (typed slots, not classes):** it buys "a new file driver needs
  no new constant" and lets ETHER and PCIETHER coexist, and a machine has one
  NIC. **Record as D5.**

### gfr-7 - C1 survives on correctness while the door halves its bytes; C3 is refuted; C4 is half dead (weakness, medium, flagday)

- **C1:** mem-18's three repeats of the carry bug (claim AX, regrow AX, DMA
  head CX) are the strongest argument, and they are correctness, not bytes.
  Under gfr-1 the six withdrawn cells save 12, not 36, so **C1 is ~-35 to -55
  per kernel**, and on kern_mega only the cell part is conventional. 221 asm
  sites convert. **Keep it in Phase 2.** mem-13 (the owner word's four
  meanings, -20 to -40 `.cold`) rides it only if the Task Manager is touched
  anyway.
- **C3** (GOTO/GOTO_Q/GOTO_QM as one cell with AL = mode) saves 6 kernel bytes,
  or 4 under the door. Every one of the **154 call sites** (8 + 20 + 126,
  MEASURED) then needs a `mov al, mode`: **+~300 across packages**. Net bytes
  go the wrong way. **Refused, by arithmetic**; a cleanliness argument alone
  does not carry a flag-day row.
- **C4, refreshed against the code:** `OSAPI_FILE_APPEND_SYS` has **zero
  callers** in `apps/`, `drivers/`, `tests/` or the C SDK. HDD.DRV's installer
  writes system files through `OSAPI_FILE_WRITE_SEQ` with `WSEQF_SYS`
  (inst.inc:2246-2250), and `WRITE_SYS` keeps its two calls (inst.inc:2240,
  3025). So the half worth taking is **withdraw `APPEND_SYS` alone**: JCELL 3
  + `api_file_append_sys` 9 + `dwf_dskw_append_sys` 5 = **-17 kern_big
  (MEASURED spans, `build/kernel.map`: 0x5D0..0x5D9, 0xAD10..0xAD15)**, ~-3
  kern_small. The "every caller zeroes AL" cost the row worried about
  disappears, because `WRITE_SYS` is not touched.
- **C2** stands (-15, or -11 under the door). **histsys-13 and dw-22** (one
  file-position shape, a caller-held write cursor) are recorded together as
  **C9**. Today WRITE_SEQ has no second concurrent stream to thrash.

### gfr-8 - L1 is worth more today than its row says, and it shares the flag day with gfr-1 at no extra cost (size, medium, flagday)

MEASURED from the three maps (`.text` + `.bss` against the 512 rounding of
`KIMG_PARA`, kernel.asm:1656):

| kernel | `.text` + `.bss` | rounded to | pad |
|---|---|---|---:|
| kern_big | 0xB27E | 0xB400 | **386** |
| kern_small | 0x80CB | 0x8200 | **309** |
| kern_mega | 0xC918 | 0xCA00 | **232** |

The row's "165 at build 477" is a dated reading. The pad varies 0-511, and the
realized saving depends on where KERN_END's own rounding falls, which is ~256
on average. `FAT_SEG` is still `KERNEL_SEG + KIMG_PARA` on every `ROM_COLD`
kernel (kernel.asm:1792), and `ROM_COLD` is every kernel (1121-1123), so the
row's layout argument holds unchanged.

**Interplay:** L1 moves `KERNEL_SEG`, which every far call names. After gfr-1,
rare calls name no segment at all, so L1's per-site churn falls to the hot
SLOTs and the callback-ES contract. Both rows need every package rebuilt and
both bump both numbers, so **together they cost one flag day, not two**.

### gfr-9 - the review's other flag-day findings, placed (consistency, low)

| finding | becomes | phase |
|---|---|---|
| abi-9 (CF=1 + AX=1 "not here") | **R9**, proposed. The doc half now. Most stubs share `OSAPI_XREFUSE`, which already answers 1 | doc now; contract Phase 2 (~0 to +10) |
| abi-13 (names) | **T6**, renames with `%define` aliases, ridden on gfr-1's `OSCALL` rewrite | rule now; renames Phase 2 (0) |
| abi-12 (verb register) | rule for new doors now; move outliers only where a Phase 2 row reopens the cell | now (0) |
| abi-8 (context tags) | not a row, a gate | now (0 shipped; lock update, no bump) |
| abi-4 (`API_RN_DSOFF`) | pre-positions gfr-1 and T1 | now (0) |
| abi-16 (header room) | refuted for now (gfr-5) | record |
| histsys-10 (deferred paint) | **R10**, recorded; histsys-1 is the no-ABI subset | record |
| sched-19 (32-bit ticks) | **R11**, recorded until a package asks | record |
| video-19 (one display index) | **R12**, proposed low, -6 | Phase 2 |
| ui-13 (clipboard format byte) | **A4**, proposed low, +8; AL becomes an input, so it is a re-contract | Phase 2 |
| files-18 (Open filter in AH) | **A5**, proposed low, +30 `.cold`/FDLG.DRV; a re-contract of AH | Phase 2, optional |
| dw-10 (dirent compression mark) | **F3**. A dual-mark reader now (+6) and `os88disk.py`'s new mark; drop the old mark at the flag day (-6) | now + Phase 2 |
| md-24 (DOS launch block) | **P3**, `KDL_VER`, private | any DOS release |
| gfx-16 / abi-17 (V1's entry) | evidence in V1: a second entry (+~6), the C SDK as the zero-risk half, and the asm half gated on a lint | Phase 2 gated |
| isr-13 / drivers-6 (kernel owns IRQs) | L3's IRQ half; `OSAPI_IRQ` appended now (~+100 resident, the owner's call), mandatory at L3 | now optional |
| mc-19 (PM-clean bit) | L3's staged road, +15 bit 5 (gfr-5) | now (+12 `.mega`) |
| drivers-16 (private ABIs in the lock) | P2 gets a check | now (0) |

### gfr-10 - the row-by-row verdict table

Strengthens (S), refutes (X), supersedes (SUP), untouched (U), refreshed by the
code (R).

| row | verdict | by |
|---|---|---|
| T1 | **SUP** by T5 (gfr-1); kept as the fallback | bytes-1, gfr-1 |
| T2 | U (recorded) | - |
| T3 | **SUP in part**: ~-28 of ~78 under T5, rule 4 intact; refusal stands for the six hot cells | gfr-1 |
| T4 | **X harder**: a rare cell is 2 bytes under T5 | gfr-1 |
| C1 | **S** on correctness, bytes ~halved under T5 | mem-18, gfr-7 |
| C2 | U | - |
| C3 | **X**: +~300 package bytes for -6 | gfr-7 |
| C4 | **R**: APPEND_SYS has no callers; withdraw it alone, -17 MEASURED | gfr-7 |
| C5, C6, C8 | U (refused) | - |
| C7 | U; **K17-5 is the same cell** | gfr-10 |
| R1 | U (re-checked: `ld_rehsz` at loader.inc:1637, 1774, 2165) | - |
| R2 | U (correctness, keep) | - |
| R3 | U (fold-in) | - |
| R4, R5 | U (built) | - |
| R6 | U (shelved); wm-R3 rides it | gfr-4 |
| R7, R8 | U (refused) | - |
| N1 | **S, re-checked**, Phase 1 | gfr-6 |
| D1 | **SUP** by histsys-7 if taken | gfr-6 |
| D2 | U (built) | - |
| D3 | **S**: +16's name already carries the title (histsys-12) | gfr-6 |
| F1 | U | - |
| F2 | **SUP** by bytes-8 (module names lose `.DRV`) if taken | bytes-8 |
| L1 | **S**: 386/309/232 MEASURED today; shares T5's flag day | gfr-8 |
| L2 | U | - |
| L3 | **S**: a staged road (mc-19 bit, isr-13 cell) that does not wait on The Wire | gfr-5, gfr-9 |
| L4, L5 | U | - |
| P1 | U | - |
| P2 | **S**: drivers-16 adds a lock check | drivers-16 |
| A1 | U; A2, A3 U (refused) | - |
| V1 | **S/R**: separate entry, C SDK first, lint for asm | gfx-16, abi-17 |
| V2 | **S**: its renumber is free inside T5's | gfr-1 |
| V3 | U: the door triples the gap but there are 7 sites | gfr-1 |
| V4 | U | - |
| V5 | U (an addition; wants a consumer) | - |
| V6 | U (refused) | - |
| L3 (10, corroborated) | as L3 | - |
| T2 (10) | U | - |
| T2-2 | **SUP in part** by abi-11 (6 of 10); **X** for the 4 left | gfr-3 |
| R2-1 | U; its -223 is `.mega`, not conventional | - |
| R2-2 | U (wording, now) | - |
| R2-3, R2-4, R2-5 | U | - |
| R2-6 | U (recorded); hm-10 folds in | gfr-4 |
| P2-1 | U (kernel-private, any time) | - |
| F2-1 | **SUP: BUILT** as SPEC.md 20.13.9 | gfr-10 |
| K17-1 | **merge into T1**; SUP by T5 | gfr-1 |
| K17-2 | **R**: survives as one runtime flag door | gfr-3 |
| K17-3 | **SUP** by abi-11 | gfr-3 |
| K17-4 | **S** in bytes (-78), **S** in cycle price; still needs the frequency reading | gfr-1 |
| K17-5 | **duplicate of C7**: one row, recorded | gfr-10 |
| FSX_CAPS VESA | U (refused) | - |

New rows: **T5** (the door), **T6** (renames), **R9** (refusal shape), **R10**
(deferred paint, recorded), **R11** (ticks, recorded), **R12** (display index),
**C9** (file position, recorded), **D5** (typed slots, recorded), **D6**
(request block for BLK/FS, recorded), **D7** (driver discovery, recorded),
**F3** (dirent mark), **A4** (clipboard format), **A5** (Open filter), **P3**
(DOS launch block). The abi-11 class table goes in as the replacement text of
**K17-3**.

### Phase 1 - now, no bump

Pre-positioning at zero cost:

- **The API level** at 0060:0006 (0 resident) and in byte 31 with
  `ld_check_hdr`'s `<=` (~+8 `.cold`), gfr-5.
- **abi-8**'s context tags (0 shipped).
- **abi-4**'s `API_RN_DSOFF` (0).
- **abi-9**'s doc sentence (0).
- **abi-12/abi-13**'s rules for new cells (0).
- **drivers-16**'s lock check (0, host).
- **R2-2**'s wording (0).
- **mc-19**'s PM-clean bit in +15 bit 5 (+12 `.mega`, kern_mega only).

Each of these is a lock update with no bump where the SDK prose moves.

Rows taken now:

- **N1**: -11 every kernel.
- **histsys-7**: ~-90 to -140 kern_big, ~-36 kern_small; makes D1 and
  drivers-9 moot.
- **gfr-2's door for cw_ shims**: ~-140 `.text` kern_big; rehearses T5.
- **dw-10**'s dual mark: +6.
- **hm-2**'s fence: +6 to 8.

The list's own hygiene: F2-1 BUILT, C4 refreshed, K17-1 into T1, K17-5 into
C7, the new rows above.

**Phase 1 net, ESTIMATED: ~-260 kern_big** (~-120 without gfr-2), ~-35
kern_small, ~-125 kern_mega conventional (plus +12 `.mega`). isr-13's
`OSAPI_IRQ` cell (+~100) is the owner's call and is not in the figure.

### Phase 2 - the flag day: `PKG_FMT` 10 -> 13, `DRV_VER` 12 -> 14

Both numbers move, because L1 moves `KERNEL_SEG` for drivers too and R3 is
driver-side. Per section 0 item 2, the next pair is 13/14. The rows, with
signed bytes per kernel (big / small / mega conventional, ESTIMATED unless
marked):

| row | kern_big | kern_small | kern_mega |
|---|---:|---:|---:|
| **T5** rare door (+ `OSCALL` SDK, three promotions) | -380 | -240 | -380 |
| **L1** FAT window + sector buffer below the kernel (today's pad, MEASURED) | -386 | -309 | -232 |
| **K17-3 as abi-11** class table + **K17-2** one flag door | -215 | -95 | -270 |
| **C1** KB doors withdrawn | -45 | -45 | -12 |
| **C2** APPEND withdrawn | -11 | 0 | -2 |
| **C4** APPEND_SYS withdrawn (MEASURED spans) | -17 | -3 | -15 |
| **R1** REHOME keep form | -16 | 0 | -16 |
| **R2** snapshots bounded by caller | +6 | +6 | +6 |
| **R3** volume bridge in ES:DI | -4 | -4 | -4 |
| **R9** one refusal shape | +5 | +5 | +5 |
| **R12** one display index | -6 | 0 | -6 |
| **A4** clipboard format byte | +8 | +8 | +8 |
| **F1** `'VC'` folded | -6 | 0 | -6 |
| **F3** old dirent mark dropped | -6 | -6 | -6 |
| **V2** yield cell enters directly | 0 (-60 clocks a yield) | 0 | 0 |
| **V1** clobbering rect entries (lint-gated) | -2 (-120 clocks a rect call) | -2 | -2 |
| **R2-4 / R2-5** FSX_SPK and FSXF_REAL | -3 | 0 | -25 |
| **T6** renames | 0 | 0 | 0 |
| **net** | **~-1,078** | **~-685** | **~-957** |

With L1 at its ~256 average instead of today's pad: ~-950 / ~-630 / ~-980.
kern_mega also drops **R2-1's -223 `.mega`** if taken. In packages:
-2 a rare site (2,767 sites, ~-5.5 KB unpacked) and +~1.3 KB of class tables
replacing ~2.6 KB of setter code (abi-11's figure). **L3 stays out of Phase 2
until The Wire serves kern_mega builds** (the owner's ranking). Phase 1's
mc-19 bit is the staged start.

Not in Phase 2, and why:

- T1/K17-1: the fallback for T5.
- K17-4: no frequency reading yet.
- C3: refuted.
- D1/drivers-9: moot under histsys-7.
- D5/D6/D7, R6/wm-R3, R2-6/hm-10, R10, R11, C9: recorded.
- T2-2's remainder: XT cycles.

## Rethinks

- **The flag day as one source rewrite, not N.** T5 forces every call site
  through one macro. That rewrite is the moment abi-13's renames, abi-12's
  register moves and any future kind change cost nothing. After it, promoting
  or demoting a cell is a rebuild. Win16's ordinal imports and OS/2's
  `DosVerbNoun` set had this property, because the import table sat between
  the call site and the kind.
- **Rehearse an ABI mechanism on a private ABI first.** gfr-2 is the general
  pattern. The modules (P2), `VX_ABI_VER` and `HD_ABI_VER` are versioned with
  the kernel, so a mechanism proven there costs nothing to abandon. T1 was
  declined twice partly because it could only be tried by breaking every
  package.
- **A minor version that is a LEVEL, not an address.** The Wire already
  reasons in levels. The kernel publishing a level keeps the fence meaningful
  across a table reshape, which a table-end word would not survive. GEOS
  protocol numbers and AmigaOS `lib_Version` are both counters, not
  addresses.

## Prior art

- **Mac OS A-line trap dispatcher / DOS `int 21h`:** the function index at the
  site and one table word per function. This is gfr-1's door. The Mac
  dispatcher cost ~40-60 clocks on a 68000 for the same index fetch and
  table read, and it was the Mac's universal call, not its rare one. So the
  idea has carried far heavier traffic than os8088's rare cells. DOS returned
  through `retf 2` to keep CF, which T5's tails copy.
- **Win16 `WNDCLASS` + `CreateWindow` styles, with `SetWindowLong` for
  runtime changes:** handlers in the class, initial style in the class, a
  runtime door per field. That is gfr-3's split exactly: the class table, the
  style word, and K17-2's one flag door.
- **Windows 3.0 NE `PROTMODE` flag:** a converted binary declares itself, and
  the loader stops excusing it. That is mc-19 at +15 bit 5.
- **GEOS geode protocol (major must match, minor must be >=):** gfr-5, with
  `PKG_FMT` as the major and the level as the minor.

## Doc/code mismatches

1. **`kernel/kernel.asm:13-19`**: "Three fixed entry points, at KERNEL_SEG ...
   0060:0008 boot splash tick (SPEC.md 15): far-called by the boot sector
   after every sector it reads ... 0060:0010 os8088 API jump table (SPEC.md
   20.3): 8-byte DS-switching cells". The code disagrees on both counts:
   - kernel.asm:2360-2365 says 0x08..0x0B "ARE FREE AGAIN", because stage 2
     calls the splash tick NEAR (SPEC.md 2.9.4);
   - the table holds 6-byte rare cells and 5-byte inline cells as well as
     8-byte SLOTs (kernel.asm:2501-2506);
   - 0x04, 0x0C and 0x0E are fixed words.
2. **`kernel/mouse.inc:6310-6311`**: "one contiguous span that a test package
   reads through 0060:0006". kernel.asm:2357-2359 says the mouse instrument
   "used to have a fixed word at 0x0006 and is now an entry in the registry at
   0x000E", and the registry carries `DBG_TAG_MOUSE, mou_dbg_blk`
   (kernel.asm:4536).
3. **`kernel/kernel.asm:2376-2377`** (`boot_ticks`): "exactly like the two
   jumps above and the table below". There is one jump above (`jmp kmain` at
   0x0000); the splash jump is gone.
4. **`drivers/hdd/inst.inc:127-129`, `:984-986`, `:2052-2053`**: "OSAPI_FILE_APPEND_SYS
   is the door (SPEC.md 19.6.1)". The installer's chunk writer is
   `or al, WSEQF_SYS / call OSAPI_FILE_WRITE_SEQ` (inst.inc:2246-2250), and the
   close is `call OSAPI_FILE_WRITE_SEQ` (2092-2093). No call to
   `OSAPI_FILE_APPEND_SYS` exists anywhere in the tree.
5. **`docs/plans/ABI-BREAK-LIST.md` C4**: "(drivers only, one caller, `HDD.DRV`'s
   installer)". `OSAPI_FILE_WRITE_SYS` has two call sites (inst.inc:2240,
   3025), and `OSAPI_FILE_APPEND_SYS` has none.
6. **`docs/plans/ABI-BREAK-LIST.md` F2-1**: "**proposed, unmeasured** - wants a
   host-side experiment over the shipped files first". It is built: SPEC.md
   20.13.9 (`LZ_ESC`, `kernel/lz.inc:90`, `:252`), measured -114,457 bytes and
   225 clusters over seventeen images, with no container change and no bump.
7. **`docs/plans/ABI-BREAK-LIST.md` K17-5 and C7**: K17-5, "Retire the duplicate
   `drv_pkg_call_x` SLOT at 0x038B ... **proposed, low**", and C7 (the same
   cell, `OSAPI_DRV_CALL_AT`, os88api.inc:3303), "**recorded, not proposed**".
   One cell, two statuses. Its only caller is FTPD (two sites).

Proposed text for each is in the structured result.

## Open questions

1. The door's IF rule. Should rare cells be stated "IF=1 callers only" (abi-8's
   tag), which saves ~30 clocks by dropping the stacked-IF test? Or is any rare
   cell legitimately called with IF=0? A tag census would answer it.
2. KERNEL-BYTE-QUEUE item 1 says 90 `cw_` shims after its first cut, and
   `build/kernel.map` lists 70 `cw_*` symbols. Retake the census before gfr-2
   is priced for real.
3. Whether `OSAPI_SEG` is called per access in any kern_mega package arm.
   That decides whether it is promoted to a hot SLOT at T5's renumber.
4. Under gfr-2 and T5 the modules' far calls into `KERNEL_SEG`/`COLD_SEG` go
   away. Does that shrink `cfx_tab` (the slide's list of segment words) and
   `mod_cfix`'s work? Plausible, unchecked.
5. Which vectors the windowed DOS exchange set (`DVW_XB_SIZE`) covers, and
   whether one vector can be held out of it at no cost.

## Verification

Adversarial pass on 2026-11-08 against the working tree (build/ maps of Nov 8
03:08-03:11). Ten findings: **6 confirmed, 4 plausible, 0 refuted**. Nine doc
fixes confirmed, two of them narrowed to facts.

- **gfr-1 confirmed.** The counts were re-taken and match exactly: 6,998
  `call OSAPI_*` sites, XMEM 2/2/3, GFX_FILL_GRAY 30, OSAPI_SEG 108 and FS_OP
  45. `apps/dos/dos.asm` banks all 512 IVT words, so the door vector really
  must be held out of the exchange. The cycle price is a PREDICTION. One cost
  is missing from the finding: on kern_mega in protected mode an `int` goes
  through the IDT, so the door also needs a gate there.
- **gfr-2 plausible.** There are 70 distinct `cw_` symbols, but some of them
  are equates onto shared tails, so the shim census has to come first. The
  -140 is unmeasured.
- **gfr-3 confirmed.** There are ten MEGA_ICW* cells: six handler setters
  (ICWB/ICWBW) and four ICW (gfx_color, osapi_seed, gfx_b1ink, ico_c1). The
  cited line numbers have drifted by about 6-8.
- **gfr-4 confirmed.** `MC_SIZE` is 10 and the 247/120 counts are exact.
  R2-6 is already "recorded".
- **gfr-5 confirmed, with two caveats.**
  - abi-16 is refuted by `instance.inc`'s own "AN INDEX OUT OF RANGE IS NOT A
    REFUSAL", and `ld_check_hdr` does not validate +15.
  - Header byte 31 is always 0, but `os88pkg.py`'s `parse_name` must be
    changed to allow it.
  - An OLD kernel's `desksc.inc` caption scan counts up to 16 bytes. A
    15-character name with a non-zero byte 31 therefore falls back to its
    stem. That is visible but harmless, so "ignored" overstates it.
- **gfr-6 plausible.** The N1 fallbacks are still present (now at
  driver.inc:2981 and 4072). The D1 and drivers-9 verdicts depend on
  histsys-7, which is itself a proposal.
- **gfr-7 confirmed.** No caller of `OSAPI_FILE_APPEND_SYS` exists. The spans
  are 9 + 5 + the 3-byte cell = 17, and the cell's 3 bytes go only at a
  renumber. WSEQF_SYS keeps using the shared fence. C3 calls itself a
  cleanliness row, so it is refuted on bytes only.
- **gfr-8 confirmed.** Re-derived from the three maps: 386, 309 and 232.
- **gfr-9 and gfr-10 plausible.** These are bookkeeping and estimated nets.
  The new row ids do not collide with any existing row.

Doc fixes. Every one was checked: each quote occurs exactly once, and the code
differs from the text in the arm it describes.

- **The `kernel.asm` file header.** The doc pass has already touched it, and
  the reviewer's quote was elided. Two lines are still wrong: "Three fixed
  entry points" and "8-byte DS-switching cells".
- **The boot_ticks comment.** It still says "the two jumps above".
- **`mouse.inc`.** Its 0060:0006 comment is stale.
- **`hdd/inst.inc`.** The "APPEND_SYS is the door" sentence is stale at :2052
  and :985. The history line at :129 is left alone, because it is true as
  history.
- **ABI-BREAK-LIST C4 caller fact.**
- **F2-1 marked BUILT.**
- **K17-5 cross-referenced to C7.**

The reviewer's replacement texts for C4 and K17-5 also changed the status
verdicts. These fixes keep only the facts and leave the verdicts to the
list's owner.
