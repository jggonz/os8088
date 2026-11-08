# LENS: the public API surface as a whole

Reviewer area key: `abi`. Tree: `the-big-review` (cut from `elendilon`), 2026-11-08.
Nothing was assembled; every byte figure is ESTIMATED from the encodings unless
it says otherwise. "kern_big" below means the default build `t_api_abi` reads;
arms are named where a claim is true of one and not another.

## Scope read

| file | lines | read |
|---|---|---|
| `apps/os88api.inc` | 7,104 | lines 1-760 (preamble, environment, sound, FSX_SPK), 1365-1380, 2055-2260 (CPU_F, MEM_SUBSEG, OSAPI_SEG), 2530-2611 (FS_PROG/FS_OP/VOL_CHECK), 3160-3180, 3441-3460 (DRV_CALL), 4000-4260 (FERR, window record, template, header flags), plus a census of all 197 `%define OSAPI_*` |
| `kernel/kernel.asm` | 9,886 | 2300-2660 (fixed words, cell macros, ICELL/XREFUSE, skip bytes), 2652-4500 (the whole table, every `%if` arm walked: 2820, 3512, 3546, 3645, 3757, 4246, 4271, 4318, 4347, 4358, 4381, 4430, 4440), 4380-4560 (tail, length guard, debug registry), 4590-5240 (every shared body: `api_rx`/`api_x`, `api_rxc`, `api_rsc`/`api_rs`, `api_far`, `api_rn`, `api_fdlg_open`, `api_gfx_save/rest`, `api_file_find(_raw)`, `api_file_path`, `api_file_*_sys`, `api_file_rename`, `api_cpname`/`api_copyname`), 6330-6470 (`osapi_seg`, `osapi_img_name`), 6598-6612 (`osapi_rand`) |
| `drivers/os88drv.inc` | 1,059 | 50-92, 359-410, 680-775, every `equ` |
| `apps/cc/os88thunk.asm` | 2,781 | 1-140 (the thunk contract); `os88.h` by grep (error model, prototypes) |
| `apps/os88mega.inc` | 63 | all |
| `tools/abi.lock.json` | 1,819 | analysed whole: 258 history entries, 5 bumps |
| `tools/abilock.py` | 207 | 1-80 |
| `tests/unit/t_api_abi.py` | 583 | all |
| context | - | `kernel/loader.inc` 265-330 (`ld_hdr_ok`); `kernel/driver.inc` 3830-3870 (KEEP), 6539-6770 (`drv_pkg_call_x`, `drv_pkg_disp`, kern_small arm); `kernel/wm.inc` 16-132 (record), 396-470 (`wm_pkgcall`), 4277-4320; `kernel/fprog.inc` 1148-1185 (`osapi_fs_op`); `kernel/diskw.inc` 3380-3470 (`dwsf_dskw_write_seq`); `kernel/sched.inc` 2855-2885; `kernel/fsx.inc` 2330-2380, 2574-2590 (the real-mode table mask); `kernel/memory.inc` 4537-4580; `tools/os88wire.py` 130-140; `tests/unit/t_mirror.py` header; `tools/stkdepth.py` header |
| SPEC.md | - | §20.2, 20.2.0, 20.2.0.1, §20.3 whole (incl. the slot-contract block), 20.3.1, 20.3.2, 20.4, 20.5, 20.8 rules 1-4, §53.8 |
| plans/reports | - | `ABI-BREAK-LIST.md` (all), `KERNEL-BYTE-QUEUE.md` (grep), `CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (the API-touching rows) |

## Verdict

The surface's *mechanics* are the best part of the kernel and better than every
period peer on an 8088: a fixed far-call table with no relocation and no
loader fixups, three cell shapes chosen by heat and measured (the inline cell
is 52-57 cycles against the SLOT's 159-162), callbacks that are plain near
procs because the kernel sets DS from `W_SEG` (Win16 needed
`MakeProcInstance` and an exported prolog for the same thing), a binary-level
gate (`t_api_abi`) and a fingerprint lock (`abilock`) that make collisions and
silent re-contracts fail `make`. What lags is the *model around* the
mechanics: there is no runtime API level, so a new package on an old kernel at
the same `PKG_FMT` still far-calls past the table's end (SPEC.md 20.2.0.1
admits it); the refusal convention is "CF=1 and AX means whatever this cell's
author chose"; execution-context rules (UI task only, worker-safe, hook-safe)
are prose with one partial enforcement; and the surface has grown by
accretion - 44 `WM_*` cells, ten handler setters, seven flag setters, three
verb registers, three meanings of `DESK`. Two contract bugs are live
(`OSAPI_RAND`'s low bits are periodic and six games test them; kern_small's
`OSAPI_FS_OP` clobbers AX against "every register kept"), and the gate that
holds the table honest reads one kernel of four. The flag-day list already
carries the byte rows; the new rows here are mostly about *model*: a
major/minor version pair (GEOS protocols, AmigaOS `OpenLibrary`), one refusal
code, and a window CLASS record in the package (Win16 `WNDCLASS`) in place of
per-window handler words plus seventeen setter cells.

## Strengths

1. **Direct far calls into a fixed table, no fixups.** `call OSAPI_X` is
   `9A off 0060`; the loader relocates nothing (`os88pkg.py` validates, it does
   not generate). DOS paid an `int 21h` dispatch per call, Mac paid an A-line
   trap exception, Win16 paid a fixup chain per import segment at load (and a
   thunk through the entry table for every moveable target). On a 4.77 MHz
   8088 that is the right answer and should never be traded away.
2. **Cell shape follows heat, and the heat claim was measured.** Hot 8-byte
   SLOT / 7-byte X, rare 6-byte `push bp / call api_r<kind> / dw target`, and
   the inline cell that IS the routine (`kernel.asm:2652-4449`). `OSAPI_ICELL`
   / `OSAPI_IEND` (`kernel.asm:2574-2599`) make a length mismatch a
   *negative `times`*, i.e. an assembly error, on whichever build is wrong.
3. **The ABI is checked in the binary, not the source.** `t_api_abi.py`
   decodes `kernel.bin` by cell length, resolves each target in the section
   its shape names (a `.cold` offset resolved against `.text` is a plausible
   wrong name - the trap it avoids), and keeps ALIAS / INLINE / FIXED ledgers
   that cannot rot in either direction (`t_api_abi.py:432-439`).
4. **The lock makes the bump a decision.** 258 recorded decisions, 5 bumps
   (`abi.lock.json`), and the contract comments are part of the fingerprint
   (`abilock.py` docstring). `LD_EVER` turns the one refusal a user meets into
   words they can act on (`loader.inc:292-300`).
5. **The dispatcher at +12** (`call bp / retf`) plus `wm_pkgcall`'s single
   `call far [es:si+W_DISP]` (`wm.inc:396-470`) makes every callback an
   ordinary near proc, re-entrant across packages, with ES = KERNEL_SEG. A
   package author cannot forget a `retf` because they never write one.
6. **Marshalling at one layer.** X cells hand the caller's DS in ES; N cells
   stage a bounded name (`api_copyname`, 13 bytes, NUL-forced -
   `kernel.asm:5192-5212`) so a hostile or unterminated name cannot run on.
7. **One SDK, one table, refusals rather than holes** on all four kernels: a
   kern_small cell answers CF=1 instead of vanishing (SPEC.md 20.8 rule 4).
   This is OS/2's Family API idea done better - one binary per package, the
   kernel rather than a bind step absorbing the difference.
8. **"Branch on the caps, never on the tier"** (`os88api.inc:2061-2063`):
   `OSAPI_XMEM_CAPS`, `SND_CAPS`, `FSX_CAPS` answer what is THERE, not what
   the CPU could do. This is Gestalt's lesson learned without Gestalt.
9. **Tables versioned by their own length byte.** A driver's `DRV_H_DSV`
   (+15) and `DRV_H_FSV` (+10) say how long its service tables were when it
   was built, and `drv_publish` copies exactly that and zeroes the rest
   (`os88drv.inc:379-390`, `driver.inc:340-360`) - RegisterClassEx's `cbSize`
   in two bytes. It is the pattern R2 (snapshots) and the template rethink
   below should copy.
10. **Cells as module doors** (`apic_*`, `kernel.asm:2475-2494`): a module
    far-calls the public cell rather than growing a private shim, and the
    label follows the cell through any renumber.
11. **`t_mirror` maintains itself** - every `NAME equ` defined in more than
    one file is compared, so a newly mirrored constant is covered the day it
    appears.

## Findings

Ranked most severe first. "now" = no ABI break; "flagday" = batch with
`docs/plans/ABI-BREAK-LIST.md`; "record" = write down, not recommended.

### abi-1 - `OSAPI_RAND`'s low bits are periodic, and six games read them (bug, medium, now, +2 `.text`)

`osapi_rand` (`kernel.asm:6599-6607`) is `seed = seed*25173 + 13849 mod 2^16`
and returns the seed. 25173 = 1 mod 4 and 13849 is odd, so bit 0 has period 2
(it alternates every call), bits 0-1 period 4, bits 0-2 period 8. Checked:
from any seed `x & 3` cycles 3,0,1,2,3,0,1,2... Callers that take the low
bits: `apps/tank/tkgame.inc:1271` (`and ax,3`), `:1275` (`test al,1`),
`apps/dotdel/ddgame.inc:1002` (`test al,1`), `:1014`, `:1177` (`and ax,3`),
`apps/tamegram/tamegram.asm:1200/1209/1215`, `apps/wireball/wbgame.inc:34`,
`apps/pixelstein/pxact.inc:243`. With no other caller interleaving, Tank's
`test al,1` is a deterministic alternation, not a coin. The seed is also ONE
system-wide word (`[osapi_seed]`, inline `OSAPI_SRAND` at 0x00C0), so a
package's `SRAND` re-seeds every other package and two games interleave each
other's streams - not documented anywhere in the SDK (`os88api.inc:348-349`).
**Fix:** `xchg ah, al` after the store, so AL carries bits 8-15 (period >= 512)
and the stored seed is unchanged: +2 bytes resident, no ABI change (the SDK
promises "the next pseudo-random word"; SPEC.md 20.4's "out AX = seed" moves).
Say in the SDK that the generator is shared and that a package needing a
reproducible stream carries its own (~14 bytes). Any test that asserts a
seeded sequence must be re-baselined.

### abi-2 - kern_small's `OSAPI_FS_OP` clobbers AX against "Every register kept" (bug, low-medium, now, 0)

On kern_small `FPG_PARK` is undefined (`kernel.asm:1303-1307`), so 0x04EF is
`OSAPI_XREFUSE` (`kernel.asm:4437-4439`): `mov ax, 1 / cmp al, 2 / retf`. The
SDK contract (`os88api.inc:2543-2565`) lists kern_small among the refusals and
ends "Every register kept"; kern_big's body banks AX (`fprog.inc:1149`,
`1184`). Callers state the contract they rely on - `apps/dos/dosload.asm:611`
("every register kept"), `apps/os88partsbody.inc:1702`. I found none that
reads AX after the call today, so it is latent. **Fix:** the SDK line
"kern_small: CF=1 and AX = 1, the OSAPI_XREFUSE answer" (an `abilock --update`
without a bump), or make the cell `stc / retf` and four NOPs (still six bytes,
plus a FIXED row in `t_api_abi`). `OSAPI_VOL_CHECK`'s contract already says
"(AX = 1)" for the same cell shape - copy its wording.

### abi-3 - `t_api_abi` reads one kernel of four (hazard, medium, now, 0 kernel bytes)

`main()` opens `build/kernel.bin` only (`t_api_abi.py:443`). The table has
eleven `%if` arms (`kernel.asm:2820, 3512, 3546, 3645, 3757, 4246, 4271, 4318,
4347, 4358, 4381, 4430, 4440`) whose kern_small, kern_emu and kern_mega
shapes are never decoded. What holds them is the per-cell ICELL length check
and the single total-length assert (`kernel.asm:4493`), which cannot see two
compensating length changes in different arms. The FIXED ledger has no rows
for kern_small's two XREFUSE cells (FS_OP, VOL_CHECK), so the gate would fail
there today - i.e. it has never been run there. The invariant that actually
matters - "every kernel's cells START at the same offsets" - is checkable
from the four maps without decoding anything. **Fix:** a `t_api_abi
--kernel` mode over `build/smallk`, `emuk`, `megak` with per-kernel FIXED
rows (kern_mega's SLOT shape `1E E8 .. .. 1F CB 90 90` and its ICW jumps
need decoders), registered where those kernels are built (`full`), plus a
cheap cross-kernel start-offset comparison.

### abi-4 - an N-cell body reads the caller's DS out of `api_rn`'s frame by a literal offset (hazard, medium, now, 0)

`dwsf_dskw_write_seq` (`diskw.inc:3397-3414`) takes `WSQF_SYS`'s driver fence
on `mov bx, [bp+10]` - the caller's DS that `api_rn` (`kernel.asm:4747-4767`)
pushed, under `api_far`'s far frame, counted by hand in a comment. Nothing
asserts it. T1 (five-byte rare cells, ABI-BREAK-LIST) moves exactly this
frame ("the stack shape of the X/N bodies must be re-proved"); if the offset
shifts, the fence reads the caller's SI or BP as its segment and grants or
refuses SYS appends on garbage. **Fix:** `API_RN_DSOFF equ 10` defined beside
`api_rn` with the push sequence, used in `diskw.inc`, and a line in T1's row
naming this reader.

### abi-5 - `OSAPI_DRV_CALL` promises DI back and nobody keeps it (hazard, low-medium, now, 0)

The SDK says DI "reaches the driver as you set it ... and comes back yours"
(`os88api.inc:3456-3460`). `drv_pkg_call_x` pops the caller's DI *before* the
far call and restores only BP and DS after it (`driver.inc:6614-6651`), and
the driver-side contract asks nothing about DI ("out whatever you define; CF
and AX reach the package unchanged", `os88drv.inc:377`). So "comes back
yours" holds only if every verb of every driver preserves DI. Also: "CF=1
AX=0 = no driver" (`driver.inc:6544`, `6655-6659`) is indistinguishable from
a driver verb that refuses with AX=0. **Fix:** the driver contract says
"preserve DI (and BP is spent)"; a `push di / pop di` around
`call KERNEL_SEG:drv_pkg_disp` is +2 bytes but also +2 on ETHER's 272/384
service-worker chain, so the doc fix is the right one.

### abi-6 - a package cannot ask which cells this kernel has (weakness, high value; now 0 bytes, flagday ~+8 `.cold`)

`PKG_FMT` is an equality test (`loader.inc:295-298`): a MAJOR number. Since
format 10 the table has only grown, so the table's end IS a minor number -
and The Wire already reasons in it (`tools/os88wire.py:132-138`,
`WIRE_CELL_LEVELS`, `WIRE_TABLE_END`). The guest cannot: SPEC.md 20.2.0.1's
"What it does not cover" is a new package far-calling past an old table.
0060:0006-0007 are padding today (`kernel.asm:2346-2360`, "0x06..0x07 stay
free"). **Now (0 resident bytes):** `dw osapi_table_end` there; an SDK macro
`OS88_NEED cell` for the entry proc (`cmp word [es:6], cell+1 / jb refuse` -
an old kernel reads 0 and refuses, which is the right answer); `os88pkg.py`
already can compute a package's highest far-called cell the way the Wire
does, so it can warn when a package calls past the last *released* level
without the macro. **Flag day:** carry the needed level in the header and let
`ld_hdr_ok` answer `LD_EVER` when it exceeds `osapi_table_end` - load-time
refusal, as Win16 refused an unresolved import ("Call to Undefined Dynalink")
and AmigaOS refused `OpenLibrary(name, version)`. The header has no free word
(see abi-16), so this rides the flag day's header change.

### abi-7 - packages write the window record's geometry, which the SDK says they may not (weakness, medium, now, 0 / ~+10)

The SDK: "the fields are readable by contract, not writable"
(`os88api.inc:3170-3174`). In the tree: `apps/paint/paint.asm:12841-12862`
(`pt_wfix` writes W_W, W_H, W_X, W_Y from inside a size-changed callback),
`apps/os88stik.inc:1023, 1712, 2376-2407` (hide, poke W_X/W_Y/W_W/W_H,
show). The kernel keeps derived state the pokes bypass - the natural rect
`W_NATR`, the zoom bank `W_ZOOMR`, the raise cache's `W_SUEXT`
(`wm.inc:120-122`). The pokes exist because the API has no "set geometry,
don't repaint" door: `OSAPI_WM_RESIZE` always repaints and cannot be called
from the callback Paint is in. Prior art: Win16 `SetWindowPos` with
`SWP_NOREDRAW`/`SWP_NOMOVE`; Mac `SizeWindow(w, h, fUpdate)`. **Fix:** a
no-repaint/hidden mode on `OSAPI_WM_RESIZE` (a flag bit in CX's top - widths
never reach 32,768; ~+10 bytes) or a stated hide-poke-show rule for HIDDEN
windows only; and a host lint over `apps/` for stores to `[es:..+W_*]` with an
allow-list, so the next one is a decision.

### abi-8 - execution-context rules are prose; one is a stated "author rule with no enforcement" (weakness, medium, now, 0 shipped)

Every cell's context lives in its comment: "UI task only", "Any context",
"worker-safe", "Bracket-only", "lock held", "Legal in an FSXF_RATE hook".
SPEC.md 20.3: the file slots' UI-task rule "is an author rule with no
enforcement behind it, and violating it corrupts the mounted volume". The
mechanism it protects is single global state: `api_name`/`api_name2`
(`kernel.asm:5213-5214`) and `[api_sysfp]` written through CS
(`kernel.asm:5007-5011`). There is no `[sch_cur]` test on any file cell
(grep). **Fix:** a machine-readable tag in each contract comment
(`; @ctx ui`, `any`, `worker`, `hook`, `lock`) - inside the fingerprinted
contract, so `abilock` already covers it - and a host gate built on
`tools/stkdepth.py`'s call graph: no worker root (a `OSAPI_TASK_SPAWN`
target, `OSAPI_DRV_TASK`) and no `FSXF_RATE` hook may reach a cell whose tag
forbids it. `docs/INDEX.md` can then print the tag. Prior art: Inside
Macintosh's per-routine "may move or purge memory" and "interrupt-safe"
tables, which were the Mac's version of the same rule.

### abi-9 - "refused" has no common shape beyond CF (weakness, medium, now 0 / flagday ~0)

What a caller sees on CF=1 from an absent capability: AX=0 (`OSAPI_DRV_CALL`
with no driver, `driver.inc:6655`), AX=1 (`OSAPI_XREFUSE`,
`kernel.asm:2601-2605`; FERR_NODISK=1; FSKE_NOTHERE=1), AX=`FERR_PROT`
(kern_small's sys stubs, `kernel.asm:5000-5004`), AX untouched (every
`stc / ret` refusing stub; kern_mega's real-mode bracket floods the whole
table with `stc` and `retf`, `fsx.inc:2341-2351`), or a family code (7 for an
unknown sound verb, `DRVE_*`). CF is consistent; nothing else is. **Now:**
say in the SDK preamble that CF=1 is the only universal refusal and AX means
something only where a cell names a code family. **Flag day:** adopt DOS's
convention - error 1 is "invalid function" - as "no such service on this
kernel / no such verb" across every family: XREFUSE, FERR_NODISK and
FSKE_NOTHERE already answer 1, so the moves are `DRV_CALL`'s 0 and the
untouched-AX stubs (each `stc` stub becomes `mov ax,1 / stc`, +3 bytes per
stub that keeps its own; most can share XREFUSE).

### abi-10 - contracts are written in up to four places, and the copies have rotted (weakness, medium, now, 0)

The SDK's `%define` comments are the fingerprinted authority. SPEC.md 20.3
also carries a "slot-specific contracts" block, which is stale in at least
four entries (SND_PLAY still the blocking clip; SND_CAPS's BL/DX; FSX_RUN
missing `FSXF_RATE`/`FSXF_REAL`; FSX_WAIT missing `FSXW_FRAME` - see the
mismatch list). SPEC.md 53.8 is a third copy of the FSX four with the same
omissions. `kernel.asm`'s table carries a fourth, often a dozen lines a cell
(0x0334's at `kernel.asm:3524-3537`). SPEC.md 20.3 already refuses to keep a
second copy of the ADDRESS map for exactly this reason; the same argument
covers contracts. **Fix:** delete SPEC.md 20.3's block and 53.8's, pointing
at the SDK names; trim each kernel cell comment to one line naming the SDK
symbol and the § - the reader goes to the SDK, where `abilock` fingerprints
the text. Also drop SPEC.md 20.3's running cell-count paragraph (it says
itself "Those counts are the tree it landed on, not a contract", and it is
stale - mismatch 14).

### abi-11 - the window record carries ten handler words a CLASS should carry (rethink, medium; flagday, ~-200 to -260 resident kern_big, ~-1.5KB unpacked across packages)

Each window record holds W_ONSIZE, W_ONMOUSEUP, W_ONDRAG, W_ONTIMER
(`wm.inc:45-94`) and W_ONSZ, W_ONWK, W_ONCL, W_ONRC, W_ABOUT, W_ONWH
(`wm.inc:112-126`): ten near pointers *into the package's own segment*,
stored per window in kernel `.bss` (WIN_SIZE 74 x MAX_WIN 12 on kern_big),
each installed after `wm_create` by its own cell - 219 setter call sites in
`apps/` and `drivers/` (ONTIMER 41, ONWAKE 36, ONCLOSE 31, ONMOUSEUP 30, ONDRAG
25, ONRESIZE 20, ONWHEEL 18, ONCLICK 10, ONRCLICK 7, ONSIZE 1), ~8 bytes
each. Plus seven flag setters with 128 sites (SNAP 51, OWNBG 27, SAVEU 19,
KEEPH 14, SIZABLE 12, NOANIM 4, QUIET 1). The template is a fixed 8 words
(`os88api.inc:4198-4206`), which is why every later handler became a cell.
**Rethink:** one record word `W_CLASS` -> a class table *in the package
segment*: a length byte (the DSV pattern, strength 9), a style word (the
seven flags - Win16's `WS_*` at `CreateWindow`), then the handler offsets.
The kernel reads `[ds:class+n]` with DS already the package's in
`wm_pkgcall`; kernel windows (W_SEG = 0) share const tables in `.text`; the
table moves with the package under compaction for free (near offsets). Cost:
~20 cycles a callback for the indirection. Bytes: record -18 x 12 = -216
`.bss`; ~17 cells and their kern_mega ICW stubs withdrawn (~-60 to -110 table,
more on kern_mega); +~40 dispatch, +~40 kernel class tables. Package side:
~2.6KB of setter code (before LZ) becomes ~2 bytes per handler of `dw`.
**Listed as** K17-2 and K17-3 (one setter cell by index); this is the
alternative that removes the setters *and* the record words, and should be
priced against them at the flag day.

### abi-12 - multiplexed doors take their verb in three different registers (consistency, low; now 0, flagday ~0)

AL: `SND_FM`, `SND_STREAM`, `SND_PLAY`, `FSX_SPK`, `FILE_COPY`, `VOL_CHECK`,
`WM_DESK`, `DSK_CACHE`, `SERIAL`, `THM_SET`. AH: `MEM_COMPACT` (AH = MEMC_*),
`DRV_USE` (AL = class, AH = verb), `VOL_CHECK`'s mode. BL: `OSAPI_SEG`,
`OSAPI_MEM_P`, `OSAPI_DRV_CALL` (BH = class). Each choice had a local reason
(a register that was free), but a reader cannot guess. DOS was AH = function,
AL = subfunction from 1981 to the end. **Now:** a rule for the next door
("the verb is AL unless AL is an argument, then BL"); **flag day:** move the
AH outliers to AL where they are rewritten anyway (K17-2/K17-3 touch none of
them, so only if a row already reopens the cell).

### abi-13 - names collide across subsystems (consistency, low, now 0 / flagday source-only)

`CLIP` is both the clipboard (`OSAPI_CLIP_PUT/GET/SIZE`) and the clip region
(`OSAPI_WM_CLIP_SET/CLEAR/TEST`). `FS` is both a DRVC_FILE driver's listing
(`OSAPI_FS_ENT`) and the progress widget (`OSAPI_FS_PROG`, `OSAPI_FS_OP`).
`DESK` is a driver's desktop item (`OSAPI_DESK_ITEM`), the desk layer
(`OSAPI_WM_DESK`) and the theme's ground (`OSAPI_GFX_DESK`). Singletons break
the `SUBSYS_VERB` shape: `GET_TICKS`, `SET_COLOR`, `FULLSCREEN` (a WM verb),
`DECOMP`, `TOAST`, `REBOOT`, `SERIAL`, `SEG`, `BATCH_*` (a disk verb),
`ABOUT_SET` (a menu verb). Renames are source-only (the cell does not move),
so they cost a fingerprint update and no bump; do them at the flag day with
one cycle of `%define` aliases, and adopt the rule for every new cell now.

### abi-14 - "ES:SI" in an X cell's contract means DS:SI (consistency, low, now, 0)

`OSAPI_GFX_SPANS` documents "ES:SI = CX records ... ES is yours - the stub
puts your DS there" (`os88api.inc:1951-1957`): the caller's ES is ignored and
overwritten by `api_x` (`kernel.asm:4596-4612`). Meanwhile SLOT cells such as
`OSAPI_GFX_BLIT4`, `OSAPI_FILE_READ` (ES:BX) and `OSAPI_XMEM_COPY` read a real
caller-chosen ES. Three conventions under two spellings. **Fix:** write X-cell
pointer arguments as "SI (in your DS; ES is not read)" so the register named
is the register read.

### abi-15 - no parameter validation anywhere, and no knob that adds it (prior-art, low, now, 0 shipped)

A window argument is trusted everywhere: the inline setters store through
`[cs:bx+W_*]` (`kernel.asm:2824`, 3517, 3551, ...), `wm_sizable`
(`wm.inc:4277-4284`) ORs into `[bx+W_FLAGS]`. A wrong BX writes anywhere in
KERNEL_SEG. Under SPEC.md 20.8's trust model that is acceptable in the
shipped kernel - Windows 3.0 was the same - but Windows 3.1 added parameter
validation in retail and a debug KERNEL that RIPped on misuse, and that is
what found most Win16 bugs. **Fix:** an `APICHK=1` knob (zero shipped bytes):
the window-taking cells become same-length jump cells into a validating stub
(BX inside `wm_tab`, `W_SEG` equal to the caller's DS), file cells check
`[sch_cur] = 0`, and one soak row runs the package sweep on it. Rule 3 (cell
length is ABI on every build) holds because the jump cell is padded.

### abi-16 - the header has no room left to grow (weakness, low, record / flagday)

Header flags (+3) are all eight bits defined (SPEC.md 20.2); +4 is the link
word, already overloaded as the packed length with parts
(`loader.inc:306-317`); +15's class has six bits and only five classes, but
an index past `SCH_NCLASS` is refused as hostile (`sched.inc:2878-2881`), so
those bits cannot be repurposed without old kernels refusing. Every optional
block after the header (icon, assoc, glyph, sticky card) costs a flag bit.
**Flag day:** reserve one bit for "an extension list follows the clear
prefix" (type byte, length byte, data) so the next block - abi-6's needed
level is the first candidate - takes no bit. NE and PE grew the same way
(tables reached from a fixed header). ~+20 `.cold` in `ld_check_hdr`.

### abi-17 - every cell preserves every register, and C packages throw the work away (prior-art, low, record)

The contract is "every register except documented outputs"
(`os88api.inc:47-50`), so each body banks what it touches. The C SDK declares
everything volatile - "SmallerC has no callee-saved register ... AX, BX, CX,
DX, SI, DI, ES and FLAGS are all fair game" (`os88thunk.asm:58-66`) - so for
every C package the kernel's pushes are pure cost. Win16 and OS/2 1.x
defined AX, BX, CX, DX, ES volatile and SI, DI, BP, DS preserved. **Listed
as** V1 (rect primitives clobbering AX..DX); the new argument is that the C
SDK is already a volatile-everything caller. A surface-wide switch would
break hand-written asm in silent ways, so this stays "record" beyond V1.

## Rethinks

**What a cleaner ABI looks like with one flag day.** Taken together, and in
the order they pay:

1. **Version as major/minor.** `PKG_FMT` stays the major (equality, it fences
   the kinds); the table end is the minor, published at 0060:0006 (abi-6) and
   carried in the header for a load-time `LD_EVER`. Zero resident bytes now,
   ~8 `.cold` then. This is the GEOS geode protocol (major must match, minor
   must be >=) and AmigaOS `lib_Version`.
2. **One refusal shape:** CF=1, AX=1 for "not here / no such verb", family
   codes from 2 (abi-9).
3. **A window class in the package** with a length byte, a style word and the
   handler table (abi-11), superseding K17-2/K17-3 and the template's fixed
   eight words.
4. **Five-byte rare cells** (T1) - priced already; add abi-4's frame reader to
   what must be re-proved.
5. **Names and verb registers normalised** (abi-12, abi-13) in the same
   rebuild, because a flag day is the only time renames are free of churn
   cost.
6. **Context tags** made machine-readable before the flag day (abi-8), so the
   flag day's rebuilt tree is checked by them.

**What should NOT change:** the far-call table, the dispatcher at +12, the
X/N marshalling layer, the inline cell, and "one SDK, refusals not holes".
Each is better than the period alternative on this CPU.

**Trap patching** (Mac `SetTrapAddress`, Amiga `SetFunction`, DOS vector
hooking): os8088 has none by design - extension is by driver class and
service table (`DSV_*`), which the kernel calls out to - and that is right for
a machine where a bad patch is a dead desktop. The table is writable RAM on
every real-mode kernel (fsx's real-mode bracket rewrites it,
`fsx.inc:2337-2351`), so the SDK should say in one line that a package never
writes KERNEL_SEG's first 0x500 bytes.

## Prior art

- **DOS int 21h**: one entry, AH = function, AL = subfunction, CF + AX error,
  error 1 = invalid function, AH=30h version, AH=59h extended error. Never
  renumbered. os8088 is ~5x cheaper per call and has a richer surface; it lacks
  the version query (abi-6) and the uniform error 1 (abi-9). The C SDK's
  `os88_ferr()` is DOS's 59h, already.
- **Windows 3.0/3.1**: exports by ordinal resolved at load, so a missing
  import fails the LOAD, not the call; `GetVersion` plus `GetProcAddress` for
  optional entries; `WNDCLASS` holds the window procedure once per class;
  window styles are given at `CreateWindow`; 3.1 added retail parameter
  validation and the debug kernel. os8088 wins on callback DS setup (no
  `MakeProcInstance`) and per-call cost; it should take load-time refusal
  (abi-6), classes and styles (abi-11), and a validating debug build (abi-15).
- **OS/2 1.x DOSCALLS**: ordinals again, a consistent `DosVerbNoun` naming
  scheme, Pascal stack calling, AX = error; the Family API ran one binary on
  DOS and OS/2 - the same problem as one SDK on four kernels, solved by
  binding where os8088 solves it by refusing cells. Take the naming
  discipline (abi-13).
- **Mac System 5-7**: A-line traps through a RAM trap table (patchable),
  `_Unimplemented` for absent traps, Gestalt (6.0.4) for capabilities, one
  OSErr space for the whole system, and Inside Macintosh's per-routine
  "may move memory" / interrupt-safety tables. os8088 already has Gestalt's
  discipline (strength 8); take the context table (abi-8). The Mac's lasting
  regret was exposing `WindowRecord` fields to applications, which Carbon had
  to undo with accessors; os8088 exposes W_X..W_TIMER and two packages write
  them (abi-7).
- **GEOS** (the closest peer: a GUI on an 8088): library entry points by
  number, fixed up by the geode loader, and **protocol numbers** major/minor
  checked at load - the model abi-6 proposes. GEOS handles also made memory
  movable without the per-claim relocation procs os8088 needs (SPEC.md 66);
  that belongs to the memory lens.
- **AmigaOS**: negative-offset jump vectors of six bytes per entry (the rare
  cell's size, by coincidence), `OpenLibrary(name, version)` refusing a too-old
  library, `SetFunction` for patches. The version argument is the cheap thing
  to copy.

## Doc/code mismatches

(Each with the exact current text; the structured result carries the
proposed replacements.)

1. `apps/os88api.inc:28` - "Packages run in the kernel's near model: CS = DS
   = KERNEL_SEG, but" - packages own a segment (the same file, 54-56;
   `wm_pkgcall` loads DS from `W_SEG`, `wm.inc:446`).
2. `apps/os88api.inc:34` - "whose 384-byte stack is likewise in LOW_SEG" -
   workers get a class, 128-384 bytes (`sched.inc:2872-2877`).
3. `apps/os88api.inc:148` - the paragraph "It is baked into every OSAPI_* far
   call, so a" has drifted under `VT_FILE`; it describes `KERNEL_SEG`
   (line 70).
4. `apps/os88api.inc:210` - "Each slot is an 8-byte cell at 0x0010 + 8n" -
   cells are 3, 5, 6, 7 and 8 bytes (`kernel.asm:4493`).
5. `apps/os88api.inc:215` - "preserved, flags included." - most cells answer
   in CF; `CLC_OR_STC` tails also write ZF/SF/PF (`kernel.asm:2608-2623`).
6. `apps/os88api.inc:4130` - "window record layout (SPEC.md 11, 20 bytes)" -
   the published words run to `W_TIMER` at +32; the record is 74 bytes on
   kern_big, 65 on kern_small (`wm.inc:127,132`).
7. `apps/os88api.inc:4180-4181` - "offset 28 is the NEXT window's W_FLAGS and
   offset 32 is its W_W" - kern_small's `W_SIDE` is 28 (`wm.inc:102`), so 28
   is this window's own `W_ONSZ` and 32 its `W_ONCL`.
8. `apps/os88api.inc:4185` - "the stride is 72 on one shipping kernel" - it is
   74 (`wm.inc:127`).
9. `kernel/kernel.asm:2424` - "Three cells need more than that and jump to a
   stub below instead:" - lists two families, and the table has seven shapes.
10. `kernel/kernel.asm:2555` - "Fifteen routines were ONE memory access" -
    twenty memory-access inline cells and three register-only ones on
    kern_big since pass 14 (`t_api_abi.py` INLINE and FIXED).
11. `kernel/kernel.asm:2777` - "every published slot keeps its NUMBER (SPEC.md
    20.8)" and "everything ADDED since starts at 0x0200" - the opposite of
    SPEC.md 20.3.1/20.8 rule 4; the cells below it were deleted by pass 4.
    (Noted in ABI-BREAK-LIST section 11, still unfixed.)
12. `kernel/kernel.asm:4769` and `:5037` - "cannot be an OSAPI_NSTUB",
    "OSAPI_NSTUB avoids it" - no such macro; the N body is `api_rn`.
13. `kernel/loader.inc:295` - "6 = package, 7 = driver" - `PKG_FMT` 10,
    `DRV_VER` 12.
14. `SPEC.md:44378` - "and `osapi_table_end` is `0x04F5`." -
    `OSAPI_VOL_CHECK` sits at 0x04F5 and the end is 0x04FB, 1,259 bytes
    (`kernel.asm:4440-4449`, `:4493`).
15. `SPEC.md:44322` - "`kernel.asm` asserts the table's start; its span is
    whatever" - it asserts the span too (`kernel.asm:4493`).
16. `SPEC.md:44572` - "`api_rxc` into `api_xc`, `api_rn` into `api_n`" - no
    `api_xc` or `api_n` label exists; both bodies are inline and build the far
    frame with `push cs / call api_far` (`kernel.asm:4618-4627`,
    `4747-4767`), not `call KERNEL_SEG:api_far` as the snippet shows.
17. `SPEC.md:44674` - "are still defined in `kernel.asm`" - none of the
    5 macros is defined anywhere (grep).
18. `SPEC.md:44813` - "BLOCKS for the clip; a mouse" - the door is GRANT /
    RELEASE (`os88api.inc:539-553`). (In ABI-BREAK-LIST 11, unfixed.)
19. `SPEC.md:44805` - "(always 0 = speaker), DX = 1 (the speaker is" - BL = 1
    for a driver's sink, DX bit 1 = a sound driver loaded
    (`os88api.inc:529-534`).
20. `SPEC.md:44911` and `SPEC.md:107743` - "bit 1 FSXF_FASTTICK)." - CX bits
    2 and 3 are `FSXF_RATE` and `FSXF_REAL` (`os88api.inc:6878, 6895`).
21. `SPEC.md:44921` and `SPEC.md:107750` - "AL = 0 next tick / 1 vertical
    retrace." - AL = 2 is `FSXW_FRAME` (`os88api.inc:6974`).
22. `tests/unit/t_api_abi.py:6` - "SPEC.md 20.3's table is 8-byte cells at
    0060:0010 + 8n" - variable-length since pass 4.
23. `tests/unit/t_api_abi.py:101` - "(`drivers/os88drv.inc` owns 0x0248)" -
    `OSAPI_DRV_TASK` is 0x01D9 (`os88drv.inc:624`).
24. `tests/unit/t_api_abi.py:147` - "kept live so a main-era package still
    runs" - deleted by pass 4 (SPEC.md 20.3.1); `COMPAT_OLD` is read by
    nothing.
25. `drivers/os88drv.inc:74` - "rounded up to a KB" - 512 bytes, the heap
    granule (`driver.inc:3838-3846`).
26. `drivers/os88drv.inc:377` - "out whatever you define; CF and AX reach" -
    the package side promises DI back (`os88api.inc:3456-3460`) and the
    kernel does not restore it (abi-5).

## Open questions

- abi-1: may the RAND stream change under existing tests and demos (any row
  that seeds and asserts a sequence), or does the owner want the fix behind a
  new cell? The contract text permits the change.
- abi-6: is 0060:0006 truly free on kern_mega's protected-mode image too
  (KERNEL_SEG is a selector there; the read `[es:6]` with ES = the callback
  ES is legal), and should the ROM build stamp it? I read only real-mode
  arms.
- abi-7: does the owner want hide-poke-show legalised for hidden windows (the
  Sticky library's pattern) or a door? Paint's visible-window poke needs the
  door either way.
- abi-11: kernel windows' handler words (Disk windows, the Control Panel)
  were not counted; the `+~40` for their const tables is a guess.
- abi-3: kern_mega's hot SLOT and ICW shapes need decoder rows; whether that
  belongs in `t_api_abi` or a mega-only sibling is a tier question
  (docs/WRITING-TESTS.md 2.1).

## Verification

The adversarial pass re-read the cited code, plus the KERN_SMALL, KERN_BIG and KERN_MEGA arms where they differ. Nothing was refuted. Thirteen findings are confirmed, three are corrected and one is unchecked.

- abi-1 - confirmed. The LCG's low k bits have period 2^k. Tank's second draw is fixed by the first draw's parity. `xchg ah, al` is 2 bytes, so +2 holds.
- abi-2 - confirmed. FPG_PARK is defined only under KERN_BIG, so only kern_small gets the XREFUSE cell. kern_emu and kern_mega define KERN_BIG.
- abi-3 - confirmed. The gate opens build/kernel.bin at t_api_abi.py:458 (the review said :443). FIXED has no FS_OP or VOL_CHECK row, so the gate would fail on kern_small.
- abi-4 - corrected: severity medium -> low. `[bp+10]` is the caller's DS today. Each side's comment names the other, and ABI-BREAK-LIST T1 already requires the X/N frame to be re-proved.
- abi-5 - confirmed. drv_pkg_call_x restores only BP and DS after the driver returns.
- abi-6 - confirmed. 0060:0006-0007 are padding today. Caveat: before SPEC.md 57, a mouse-instrument word lived at 0060:0006. Check that no PKG_FMT-10 kernel is that old before relying on "an old kernel reads 0".
- abi-7 - confirmed. Paint's pt_wfix and os88stik.inc both write W_W/W_H/W_X/W_Y.
- abi-8 - confirmed. SPEC.md:44973 says the file slots' rule is "an author rule with no enforcement behind it".
- abi-9 - confirmed. The refusal AX values are 0, 1, FERR_PROT, or untouched (kern_mega's stc fill).
- abi-10 - confirmed. ABI-BREAK-LIST 11 already lists SND_PLAY and kernel.asm's "keeps its NUMBER" block. The 20.3 entries are fixed below. The video area owns the fix to 53.8's copy.
- abi-11 - corrected: the setter census depends on how it is counted. Asm `call` sites alone give ONTIMER 29, ONWAKE 31 and SNAP 30; every mention gives 48, 59 and 72. The design and the -216 .bss figure stand.
- abi-12 - confirmed (spot-checked MEM_COMPACT and DRV_USE).
- abi-13 - unchecked.
- abi-14 - corrected: milder than stated. GFX_SPANS' contract itself says "the stub puts your DS there". Only the register named is wrong.
- abi-15 - confirmed (spot-checked wm_sizable and the inline setters).
- abi-16 - confirmed. Flags bits 0-7 are all defined, and the class field holds 5 values in 6 bits.
- abi-17 - confirmed (os88thunk.asm's contract).

Doc mismatches:

- 23 of the review's 26 are confirmed and carried into data/abi.json as doc_fixes.
- Three are confirmed but left to the areas that already carry the same fix:
  - os88api.inc's "384-byte stack" (sched),
  - os88drv.inc's "rounded up to a KB" (drivers),
  - loader.inc's "6 = package, 7 = driver" (loader).
- Some proposals were reworded:
  - kernel.asm's "keeps its NUMBER" fix replaces the whole stale block, not just its header line.
  - The os88api.inc "offset 28 is the NEXT" fix also corrects "record STOPS at 28 bytes".
  - The t_api_abi COMPAT_OLD deletion would change code. It became a comment-only fix that says COMPAT_OLD is a record nothing reads.
- Three fixes are new:
  - kernel.asm:5037's second stale OSAPI_NSTUB.
  - SPEC.md 20.3's api_rxc snippet (`api_xc:` label, `call KERNEL_SEG:api_far`).
  - SPEC.md 20.3's FSX_CAPS entry, which omits BX = the window.
- Abilock fingerprints contract comments, so applying the os88api.inc/os88drv.inc fixes may need `tools/abilock.py --update`, with no bump.
