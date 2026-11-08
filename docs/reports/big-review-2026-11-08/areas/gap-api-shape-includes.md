# The kernel-adjacent package includes that exist because of an API's shape (rseq, wseq, data, link, pit, m83, pself, partsbody, crt0)

Gap review, 2026-11-08. These are the package-side halves of kernel conventions:
SDK files that every package carries a copy of, each one written because a kernel
cell has a particular shape. The question for each is the owner's: what kernel
limitation makes it necessary, what a kernel-side fix would cost in resident
bytes against what it saves across the copies, and whether it can land now or
has to wait for the flag day.

## Scope read

| file | lines | read |
|---|---|---|
| `apps/os88rseq.inc` | 91 | whole |
| `apps/os88wseq.inc` | 244 | whole |
| `apps/os88data.inc` | 350 | whole |
| `apps/os88link.inc` | 117 | whole |
| `apps/os88pit.inc` | 85 | whole |
| `apps/os88m83.inc` | 216 | whole |
| `apps/os88pself.inc` | 147 | whole |
| `apps/os88real.inc` | 134 | whole |
| `apps/os88partsbody.inc` | 2,647 | 1-120 (`op_load`), 860-925 (`op_at`), 1734-1880 (`op_clbget`, `op_lazyp`, `op_lazyok`), 2297-2560 (`op_drop`, `op_row`, `op_pin`/`op_movable`/`op_decl`, `op_lazymv`, `op_carvemv`, `op_zrpara`, `op_scrub` head) |
| `apps/cc/crt0.asm` | 1,931 | 1-232, 320-335, 405-1016 (entry, every trampoline, `cc_worker`), 1040-1430 (`cc_iswk`, `cc_ovneed`'s three arms, `cc_ovthunk`, `cc_ovmthunk`, `cc_ovbind`), the tail's labels |
| kernel | | `kernel/diskw.inc` 470-560 (`dskw_dfree_x`, `dwf_dskw_vstat`), 3080-3345 (`dskw_read_at_x`, `dwf_dskw_read_seq`), 6750-6831 (the cursor alias, the kern_small stubs); `kernel/disk.inc` 4195-4206, 5104-5118 (mount rule 4), 6330-6380 (`dsk_free_clus_x`), 11177-11192 (`dsk_mgen`); `kernel/sched.inc` 380-420, 460-560 (`sch_pit_now`, `sch_fast_on/off`), 660-700, 1262-1325 (`sch_account`), 1935-1975; `kernel/kernel.asm` 6640-6657; `kernel/snd.inc` 700-730; `kernel/mega.inc` 175-190 |
| SDK / SPEC | | `apps/os88api.inc` 1083-1140, 6010-6175; SPEC.md 18.4.7.4, 18.4.8.1, 18.4.4.2, 53.2.1, 53.2.2, 72.15.1, 73.4 |
| callers | | every includer, by grep across `apps/`, `drivers/`, `tests/`; spans from `build/ftpd.map` and `build/midirack.map`; contents of `build/util360.img` |

Checked against `docs/plans/ABI-BREAK-LIST.md` (T3, C2, C3) and
`docs/plans/KERNEL-BYTE-QUEUE.md`, and against `diskread.md` (dr-5) and
`diskwrite.md` (dw-22), so this report adds to them rather than repeating them.

**Who carries what** (shipped carriers; spans measured from the `.map` files where one exists):

| include | kernel shape it compensates for | shipped carriers | span |
|---|---|---|---|
| `os88rseq.inc` | `READ_SEQ` uses different registers from `READ_AT` (DX:BX buffer, ES:DI cursor, offset inside the cursor); kern_small has no `READ_SEQ` | DOS, PIXEL, Audio, MIDIRACK, FTPD, XTMP3, Video, SysCheck, WAVERACK, `RAMDISK.DRV`, HDDTOOL, every parts body with `OP_HAS_READ`, the C SDK | **76 B** (ftpd.map 0x18C-0x1D8; SPEC 18.4.8.1 agrees) |
| `os88wseq.inc` | `APPEND`/`WRITE_SEQ` take whole-cluster chunks only; the cluster size is learned through a FAT count | REDLINE | ~230 B (by eye; no map) |
| `os88data.inc` | every file cell takes a bare 8.3 name in the current folder; `GOTO` was a remount | ~20 (Cyclone, Tank, Dot Delirium, Skies, PIXELSTEIN, WireChat, RealDMG, StikNote, Browser, WIN16, MIDIRACK, FTPD, XTMP3, REDLINE, RLAPPLY, ARCADE, WAVERACK, WEAVE, `ETHER.DRV`, through `os88stik.inc`/`os88type.inc`) | **397 B** in FTPD (0x2549-0x26D6), state and the FIND record included |
| `os88link.inc` | (none: a file format the kernel follows) | REDLINE | ~100 B |
| `os88pit.inc` | no fine-clock cell; the kernel owns the PIT | PIXELSTEIN, RealDMG, WIN16, MIDIRACK, FTPD, XTMP3, ARCADE, isoTron, `ETHER.DRV` (and `x3stub`'s private copy, test-only) | **61 B** (midirack.map 0x769A-0x76D7) |
| `os88m83.inc` | (none: a wire convention; mirrors `dskw_syms`) | FTPD, Telnet | **264 B** (ftpd.map 0x26D6-0x27DE) |
| `os88pself.inc` | no kernel service to read a part of one's own image after a re-home | DOS (direct), parts bodies with `OP_WANT_SELF` | ~150 B |
| `os88real.inc` | kern_mega hands out selectors; `FSXF_REAL` needs paragraphs | Video, vBattle | ~68 B (header says ~60) |
| `os88partsbody.inc` | no kernel service for multi-segment images | every package with parts | 800 B minimum (its own header) |
| `apps/cc/crt0.asm` | register-argument callbacks against cdecl C | every C package | per package |

## Verdict

The includes work as os8088's libc. The kernel's file API is **stateless and
takes a name on every call**, NFSv2's model. Each efficiency is won back by a
token the caller holds: the `READ_SEQ` cursor, the `WRITE_SEQ` token,
`os88data`'s banked folder, `op_clb`. That keeps kernel state and resident
bytes near zero, and it is right for this machine. Each include states in its
header the limitation it compensates for, which is rarer than it should be in
any codebase.

The weaknesses fall into four kinds.

1. **Geometry leaks into every caller, and the only way to learn it is a full
   FAT count.** `APPEND`/`WRITE_SEQ`, and `READ_AT` on kern_small, take whole
   clusters. The cluster size comes only from `OSAPI_FILE_DFREE` or
   `OSAPI_VOL_STAT`, which count every FAT entry: ~105 ms on a 20 MB disk. Three
   includes pay that count to learn a byte the kernel holds in `[dsk_spc]`
   (gi-2). The parts standard rounds to clusters even on kern_big, where
   `READ_AT` takes sectors (18.4.4.2), so it brings back the 32 KB-cluster cost
   that 18.4.4.2 was written to remove (gi-3).
2. **The kern_small stubs answer inconsistently.** Some give `FERR_NAME`. Others
   give a bare `stc` with AX left at whatever the caller had. The shared
   fallbacks rely on telling these apart: `os88wseq.inc`'s documented kern_small
   path cannot run, because its open fails first (gi-1), and `os88data`'s bank
   is useless for writes on kern_small (gi-6).
3. **The `READ_SEQ` cursor does not name its file** (gi-4), and its register
   shape needs a 76-byte shim in every reader (gi-5).
4. **`pit_now` is wrong inside an armed `FASTTICK`/`RATE` bracket.** Its users
   know this, but the include does not say so and SPEC.md 53.2.1 says the
   opposite (gi-7).

None of these needs a large kernel change. The largest is a granule cell of
about 16 bytes per kernel, and two of the fixes cost 0 kernel bytes.

I looked for a better approach and rejected two. Absorbing `os88wseq` into the
kernel is wrong: user-space buffering is stdio's `fwrite`, and System V keeps it
in libc for the same reasons. Absorbing the parts body into a kernel module is
also wrong: a module is read from the system disk, and a part comes off the
package's own disk.

`crt0.asm` meets every rule I checked it against:
- SS ≠ DS: the gate in `cc8086.py` covers it, and `crt0` takes no address of a local.
- ES = KERNEL_SEG on entry: saved and restored in every trampoline, and `cc_onfile` reloads ES itself.
- Register preservation: BX/CX/DX/SI/DI/ES are saved in every trampoline, and AX is deliberately not saved.
- DF: every entry clears it.
- CF answers are set after the pops.
- Worker return: a parked worker dies inside `TASK_ALIVE`.
- Claims: the overlay and parts claims belong to the instance, and the stale `.OVL` arm frees its own.

Its faults are documentation and one dead arm (gi-9).

## Strengths

- **One body per convention, each with a guard.** `os88link`'s record is
  compared against `kernel/desksc.inc` by `t_mirror`. `os88m83` runs under
  Unicorn against `tools/os88bbs.py`'s `MANGLE83_CASES` (`tests/m83.py`).
  `pit_now` has `tests/pitnow.py`. The header of `os88m83.inc:6-12` records the
  two drifts that two copies caused, which is the reason for having one body.
- **Staleness takes a cold path, never an error.** `dsk_mgen` is "ODD FOR EVER,
  seeded 1" and steps by 2 (`kernel/disk.inc:11177-11188`). A zeroed cursor
  therefore never matches, and the add needs no wrap test. `os88_rseq` re-seeds
  whenever the offset asked for differs from the cursor (`os88rseq.inc:38-46`),
  so a seek, a retry and a first call all need nothing from the caller.
- **`os88data.inc` verifies its own bank.** A `WRITE_AT` refusal is taken as
  evidence that the banked folder is not the one remembered (`os88data.inc:313-336`).
  The in-place write is taken only for a file this package itself wrote in full
  since the last walk, because only then is the compression hint known to be
  clear (`os88data.inc:47-61`).
- **`pit_now` and `sch_account` use the same coherence rule.** Both read the IRR
  inside the latch window, and both increment the tick when IRQ0 is pending and
  less than half a tick has elapsed (`os88pit.inc:70-76`, `kernel/sched.inc:1305-1314`).
  The header records the measured failure: 40 in 60,000 reads, and Auto judging
  98.2 ms as 43.3.
- **`os88real.inc`'s contract.** The words are renamed at the door. A word that
  is 0 stays 0 in both directions. The pin requirement is stated, with the
  reason (a move rebases a selector and nothing else). The `2-*` macro fix is
  written down at the point where the trap is.
- **`crt0`'s design rules.**
  - It saves everything rather than the minimum per callback (`crt0.asm:105-111`).
  - CF is the last thing written (`cc_entry` `cmp bx, 1`, `cc_onclose` `cmp ax, 1`).
  - The worker gate is `cc_iswk`, a stack-range test with no kernel call.
  - The thunk's return stash refuses when it is full instead of writing past it.
  - On kern_mega the region is declared movable with DS, the data alias, not CS.

## Findings

### gi-1 - `os88wseq.inc`'s documented kern_small path cannot run: the open fails first (bug, medium, now)

The header says: "ON kern_small, whose WRITE_SEQ answers FERR_NAME, each chunk
goes through OSAPI_FILE_APPEND instead" (`apps/os88wseq.inc:37-40`). But
`os88_wseq_open` first calls `OSAPI_VOL_STAT` (`:66-89`), and on kern_small that
cell is `osapi_vol_stat equ osapi_snd_fm`: a bare `stc`/`ret`
(`kernel/kernel.asm:6653-6657`, `kernel/snd.inc:714-717`). It leaves AX
unchanged, and AX is 0 here, from the `xor ax, ax` at `:74`.

So the open takes `.stat` (`:100-104`): `xchg dx, ax` gives DX = 0,
`mov [di+WS_ERR], dx` banks **0**, and the open answers CF=1 with **AX = 0**,
which is no `FERR_*` code. The stream is not marked failed, because WS_ERR is
still 0, and no file is created. REDLINE's Inspect report (`apps/redline/inspect.inc:801-806`)
returns at `jc .done` with "Report not saved". REDLINE ships on
`build/util360.img`, and `REDLINE.O88` is not in `$(SMALLOMIT)`
(`Makefile:14896-14901`), so a 128 KB machine booted from `small360.img` can
open it from B:. Whether REDLINE otherwise runs on kern_small I did not check.
The code defect stands either way, and every future kern_small user of the
include would hit it.

**Fix:** ask `OSAPI_FILE_DFREE` instead (BX = sectors per cluster). It exists on
both kernels with the same cost, and the mask becomes `BX*512 - 1`. Bank a real
`FERR_*`, not the raw AX. **About ±2 package bytes. No kernel change.** gi-2's
cell, when it exists, is cheaper still. **Listed as:** T3 names `osapi_vol_stat`
among kern_small's bare refusals and keeps them (SPEC 20.8 rule 4); this finding
is that a shared include does not handle the refusal.

### gi-2 - The cluster size can only be learned by counting every free cluster (weakness, medium, now)

`OSAPI_FILE_DFREE` and `OSAPI_VOL_STAT` both run `dsk_free_clus_x`
(`kernel/diskw.inc:491-499`, `:546-560`). That is ~105 ms on a 20 MB disk,
measured (SPEC.md 18.4.5), and ~93 clocks a cluster modelled on a floppy
(`kernel/disk.inc:6336-6346`), about 55 ms on a 1.44 MB disk's 2,847 clusters. The
SDK sends callers there for a constant: "Ask OSAPI_FILE_DFREE for the cluster
size" (`apps/os88api.inc:6021-6022`). The callers that pay it only for the
cluster size are:

- `op_clbget` (`os88partsbody.inc:1734-1756`): once per instance, on the first lazy fetch.
- `op_self` (`os88pself.inc:59-60`): on **every** call. Its header justifies this with "nothing here is a loop" (`:41-45`).
- `os88_wseq_open`: once per stream.

The kernel already holds the answer in `[dsk_spc]`, which mount rule 4
validates (`kernel/disk.inc:5106-5118`).

**Fix:** add a cell at the end of the table, `OSAPI_FILE_GRAIN` (name to taste),
behind `inst_vol_enter`:
- AX = bytes per cluster (`[dsk_spc]` × 512, at most 32,768 by rule 4).
- CX = `READ_AT`'s granule on this kernel: 512 on kern_big (18.4.4.2), a cluster on kern_small.

No count and no I/O. **About +14-16 resident bytes per kernel** (a 3-byte cell
plus ~11-13 of `.cold`/`.text`). It retires the DFREE call in three includes,
and it is what gi-3 needs. Prior art: DOS kept `AH=32h` (get the DPB), which
gives sectors per cluster without counting; only `AH=36h` counts, and DOS 4+
caches that count in the DPB as well. **Listed as:** none. Not the same as dr-5
or dw-22.

### gi-3 - The parts standard rounds every read to CLUSTERS on kern_big, which brings back the cost 18.4.4.2 removed (weakness, medium, now)

Three places round to whole clusters "because kern_small's READ_AT still takes
clusters":
- `op_lazyp` prices a fetch as the head slack up to a cluster boundary, plus the
  read rounded up to whole clusters (`os88partsbody.inc:1834-1870`).
- `op_self` does the same (`os88pself.inc:62-80`, header `:41-45`).
- `op_claim` uses the same arithmetic for the carve.

On kern_big, `READ_AT` takes whole **sectors**. SPEC.md 18.4.4.2 made that change
because "a 1.8 KB sound lead cost 64 KB of disk and of RAM on a 2 GB volume".
The parts standard never took the change. On a 512 MB FAT16 partition (16 KB
clusters) a 4 KB lazy part claims and reads up to ~32 KB; at 32 KB clusters, up
to ~64 KB. On floppies (1-2 sectors a cluster) the difference is negligible, which
is why nothing has noticed.

**Fix:** with gi-2's cell, `op_clbget` stores CX (the `READ_AT` granule) and
everything downstream is unchanged, because all of it is mask arithmetic on
`[op_clb]`. `op_self` does the same with its `bx` mask. **About 0 package bytes;
the kernel cost is gi-2's.** It saves RAM and disk time per fetch on hard disks
with large clusters. **Listed as:** none.

### gi-4 - The `READ_SEQ` cursor names no file, and stays trusted across a quiet folder change (hazard, low, now)

A hot cursor is checked only against `[dsk_mgen]` and the volume
(`kernel/diskw.inc:3104-3115`). `dsk_mgen` moves on a committed write and on a
mount that reads a boot sector, and not otherwise. It does not move on a quiet
`GOTO_QM` inside a floppy whose banked BPB is trusted, or on a fixed disk's
directory change (`kernel/disk.inc:11177-11183`, `:4199-4204`). So a cursor
seeded on `A\X.DAT` and then used for `B\Y.DAT` at the same offset delivers
**A's clusters, with CF=0**.

The rule ("zero the cursor when the file changes") is written in SPEC.md
18.4.8.1 and `os88rseq.inc:21-24`, and all nine readers SPEC lists follow it.
The SDK's own cell comment says the opposite: "so a stale cursor is never an
error" (`apps/os88api.inc:6136`).

**Fix:**
- Kernel side: the cursor's bytes 3-5 are free (`RSQ_VOL` = 2, `RSQ_CUR` = 6,
  `kernel/diskw.inc:3258-3261`). Have `dwf_dskw_read_seq` stamp an 8-bit sum of
  the name plus the current folder's low cluster byte there, and treat a mismatch
  as cold. **About +20 `.cold` bytes on kern_big, 0 on kern_small.** No ABI change:
  those bytes are the kernel's, and a zeroed cursor is still cold.
- Documentation side: correct the `os88api.inc` sentence (mismatch 12).

Prior art: NFS made the same stateless choice, and its file handle is what names
the file in every request. This stamp is the cheap version of that handle.
**Listed as:** none.

### gi-5 - `os88_rseq` is 76 bytes in ~13 carriers to re-present `READ_AT`'s registers; that logic belongs in the cell (rethink, low, now then flagday)

`os88_rseq` does three things (`apps/os88rseq.inc:38-71`):
- puts READ_SEQ's registers back into READ_AT's shape (DX:BX buffer, ES:DI cursor, the offset taken from the cursor);
- re-seeds the cursor when the offset differs;
- retries as `READ_AT` on `FERR_NAME`.

**The smallest cell change that retires it:** a new cell with READ_AT's
registers plus a DS:DI cursor. It re-seeds itself when DX:AX ≠ `+12`, and on
kern_small it is an alias of `READ_AT` that ignores DI. The DS:DI cursor frees
ES:BX for the buffer, so a player's ring can still read each chunk into a
different segment. **About +14 `.cold` on kern_big (the compare and the zero)
and +3 on kern_small (one cell aliasing `READ_AT`).** It retires 76 bytes in
each of ~13 carriers, ~1 KB of package and driver bytes, plus the C SDK thunk.

Append the cell **now**; withdraw 0x0455 at the **flag day** (the ABI-BREAK-LIST
C2/C3 batch). **Listed as:** none. It is the read-side twin of dw-22, which makes
`WRITE_SEQ`'s record caller-held.

### gi-6 - The kern_small stubs leave AX undefined, so the shared fallbacks cannot tell "no door" from a real refusal (weakness, low, now)

On kern_small:
- `READ_SEQ` and `WRITE_SEQ` answer `FERR_NAME`.
- `WRITE_AT` is `stc`/`retf` with AX left as the caller had it (`kernel/diskw.inc:6813-6825`).
- `VOL_STAT` is the shared `stc`/`ret` (gi-1).

Consequences:
- gi-1: `os88wseq` hands REDLINE AX = 0.
- `od_write`: once a file is stamped, every later save on kern_small tries
  `WRITE_AT`, which refuses, and the refusal sends it down `.full`'s re-walk
  (`os88data.inc:313-336`). That is root, SYSTEM, APPDATA: three `GOTO_QM`s and
  two `FILE_FIND` scans, followed by the full write, on every save after the
  first. The bank buys nothing for writes on kern_small. The header (`:62-64`)
  says only that every save "takes the full arm".

**Fix:**
- Move `dwf_dskw_write_at:` up one line, onto the `mov ax, FERR_NAME`: **0 bytes**.
- Point `osapi_vol_stat` at a stub that answers `FERR_NODISK`: **about +4 bytes on kern_small**.
- Whether `od_write` may then skip the re-walk on `FERR_NAME` is the witness-rule
  question NAV-COST-PLAN 4.4 decided ("a create or a replace goes where a walk
  says"). See Open questions.

**Listed as:** T3, the same cells. T3 refused removing them; this keeps them and
fixes their answer.

### gi-7 - `pit_now` is wrong inside an armed `FSXF_FASTTICK`/`FSXF_RATE` bracket, and only its users say so (hazard, low, now)

`sch_fast_on` reloads channel 0 at 65536/N, or at the caller's divisor
(`kernel/sched.inc:509-534`). The high half of `pit_now` is still `0040:006C`,
at 18.2 Hz by construction (SPEC.md 53.2.1-53.2.2). So inside the bracket the
low half sweeps `[65536 - divisor, 65536)` N times a tick: it jumps backwards
mid-tick, and the IRR adjustment never fires because the sign bit is always set.

The users know:
- isoTron uses the tick instead: "FASTTICK runs PIT channel 0 at 3x" (`apps/isotron/itfs.inc:905-906`), and skips its hold budget in a bracket (`itsweep.inc:940`).
- RealDMG switches clocks inside its `RATE` bracket (`apps/realdmg/rwlog.inc:100-114`).
- MIDIRACK's SPK bracket uses divisor 0xFFFF, which is harmless.

But `os88pit.inc` and SPEC.md 72.15.1 never mention it, and SPEC.md 53.2.1 says
"nothing outside sched.inc can tell".

**Fix:** a paragraph in the include header and a sentence each in 72.15.1 and
53.2.1 (mismatches 7-9). **0 bytes.** The alternative, a kernel `OSAPI_PIT_NOW`
that knows `[sch_fast]` and `[sch_racc]`, would be correct everywhere for about
+20 resident bytes. But a far call costs 46.7 µs against the inline read's
~30 µs, which matters for a profiler whose unit is 838 ns, and the cell would
have to join §53.7's legal list. Record that alternative; do not do it now.
Prior art: Windows 3.x's VTD virtualised the PIT, so `GetTickCount` and
`timeGetTime` stayed right when `timeBeginPeriod` reprogrammed it. The component
that reprograms the clock is the one that owns reading it.

### gi-8 - Kernel conventions copied without a mirror gate (consistency, low, now)

- `m83_syms` (`apps/os88m83.inc:214`) copies `dskw_syms` (`kernel/diskw.inc:175-176`),
  and the two agree today. `tests/unit/t_mirror.py` does not compare them, so a
  kernel change to the legal-character set would silently let FTPD/Telnet store
  names the kernel refuses, or the reverse.
- `x3stub.asm`'s `sb_now` (`apps/xtmp3/x3stub.asm:347-378`) is a third copy of
  `pit_now` "without its cli". That breaks SPEC.md 34.1's binding rule: an IRQ0
  between the latch and the second `IN` makes `sch_account`'s latch a no-op and
  its byte pair swapped. It is test-only (`X3STUB=1`; no shipped disk carries it).
- `os88pit.inc` is the only include of the set without an `%ifndef` guard.

**Fix:** add a `t_mirror` row for the symbol set; give `sb_now` a one-line
comment that it trades SPEC 34.1 for test-hook determinism, or give it the
`cli`; add the guard. **0 shipped bytes.**

### gi-9 - `crt0`'s plain `.OVL` arm ships in no package and keeps the folder bug the lazy arm fixed (weakness, low, now)

Without `CC_OVL_PART`, `cc_ovneed` reads `<NAME>.OVL` "in THIS INSTANCE's
directory - the folder it was launched from ... so no visit is needed"
(`apps/cc/crt0.asm:1256-1261`). By SPEC.md 19.2.1 a name resolves where the
instance **stands**, and the file dialog moves that. The lazy arm's own comment
names the hole: "A <NAME>.OVL beside the package had the same hole, and it was a
refusal in every menu of a program whose document lived in another folder"
(`:1146-1156`). The header says this arm is "exactly what every unconverted
package ships" (`:194-195`). Every shipped C package with an overlay is
`CC_OVL_PART` (APPLE2, C64, CWORD, LEMMINGS, LOOM, RUNCPM, WEAVE); only
`tests/cfsx` uses the plain arm.

**Fix:** retire the arm (three toasts, the stamp and ~110 lines of source;
**0 shipped bytes**), or give it the lazy arm's go-home bracket. At minimum
correct the two comments (mismatches 10-11).

### gi-10 - `os88_wseq_put` copies byte by byte (perf, low, now)

`.c` (`apps/os88wseq.inc:146-151`) is `mov al,[es:si]` / `mov [di],al` / `inc` /
`inc` / `loop`: about 50+ clocks a byte on the 8088 (segment override, two
memory operands and `loop`'s 17), against `rep movsb`'s ~17 after a 4-byte
DS/ES swap. An 8 KB report costs ~0.4 M clocks, about 85 ms, where `rep movsb`
costs ~30 ms. **About +4 package bytes.** Low, because the stream's writes
(`int 13h` at ~400 ms a call) dominate.

### gi-11 - `os88data.inc` exists because no file cell takes a path (rethink, low, record)

397 bytes in FTPD across ~20 carriers is ~8 KB of package bytes, and almost all
of it is `od_walk`/`od_dive`: root, then SYSTEM, then APPDATA, by `FILE_FIND`
ordinal scans and quiet goto calls (`os88data.inc:169-252`). DOS 2.0 put paths
in every call. Here a "resolve a relative path to (drive, folder cluster)" cell
over the existing directory walker (`OSAPI_FILE_PATH` already has one, walking
upward) would shrink the walk to one call. But the bank already makes the
steady state one `GOTO_QM`, so the saving is the first visit of a session. That
does not justify ~+80 resident bytes now. **Record.** If ABI-BREAK-LIST C3 (one
`GOTO` cell with a mode) lands, `od_qm` and crt0's lazy home are its consumers
and change by a register.

## Rethinks

- **Where the kernel API should change shape, and where it should not.**
  - Change it for geometry (gi-2/gi-3): one appended cell that the parts
    standard, `op_self` and `wseq` all use.
  - Change it for the read cursor (gi-4 now, gi-5 for the register shape).
  - Do not change it for write buffering. `os88wseq` is stdio's `fwrite`, and
    a kernel that took any-size appends would need a partial-sector
    read-modify-write in the resident write path, while the include would still
    want its buffer to cut the `int 13h` calls.
  - Do not move the parts standard into a kernel module: the bytes it reads
    are on the package's own disk, and a module is read from the system disk.
- **Make the kern_small refusals a vocabulary, not a carry flag** (gi-6). The
  shared includes are where "this kernel has no such door" and "this call failed"
  have to be told apart. Today that works for `READ_SEQ`/`WRITE_SEQ`
  (`FERR_NAME`) and fails for `VOL_STAT`/`WRITE_AT`. One rule, "a missing door
  answers `FERR_NAME`", costs about 4 bytes and makes every fallback in the SDK
  testable.

## Prior art

- **DOS** keeps a handle and its position in the SFT and buffers sectors in
  `BUFFERS=`, so `AH=3Fh`/`40h` take any size at any offset. It also keeps the
  cluster size out of the counting path (`AH=32h`) and caches the free count in
  the DPB from 4.0. os8088 chose statelessness for its read, write, read copy
  loop (os88api.inc:6028-6032), which is a fair trade on this machine. The DPB
  idea is the part worth taking: the cluster size should not cost a count (gi-2).
- **OS/2** `DosRead`/`DosSetFilePtr` on a handle; `DosQueryFSInfo` level 1
  returns the allocation-unit geometry with the free count. The handle names
  the file, which the `READ_SEQ` cursor does not (gi-4).
- **System V** `read`/`lseek` take any size; stdio does the small-write
  buffering in user space. That is the layering `os88wseq` already has, and it
  should keep it.
- **NFSv2** is the closest model: stateless, the offset in every request, the
  server caching the file's position. Its file handle is the piece the cursor
  is missing (gi-4).
- **Windows 3.x** owned the PIT through the VTD, so the component that
  reprogrammed the clock kept the clock right (gi-7).

## Doc/code mismatches

1. **`apps/os88wseq.inc:37-40`**: "ON kern_small, whose WRITE_SEQ answers FERR_NAME, each chunk goes through OSAPI_FILE_APPEND instead". **Code:** the open's `OSAPI_VOL_STAT` is a bare `stc` on kern_small (`kernel/kernel.asm:6653-6657`), so the open answers CF=1, AX=0, and no chunk is reached. **Proposed** (until gi-1's fix): "ON kern_small THE OPEN IS REFUSED: OSAPI_VOL_STAT there is a bare refusal (kernel.asm, osapi_vol_stat), so os88_wseq_open answers CF=1 and nothing is written. The APPEND arm below serves a kern_big WRITE_SEQ that refuses a chunk." After gi-1's fix, keep the current text and replace "OSAPI_VOL_STAT" with "OSAPI_FILE_DFREE" in the "THE BUFFER IS FLUSHED IN WHOLE CLUSTERS" paragraph.
2. **`kernel/diskw.inc:524-525`**: "DOS.O88 is this slot's only caller and it forwards these four to the program unchanged". **Code:** `OSAPI_VOL_STAT` is called by DOS, PIXEL (`pxpump.inc`), XTMP3 (`x3conv.inc`), REDLINE (`inspect.inc:136`), SysCheck (`scan.inc`, `disk.inc`), `os88wseq.inc` and `HDD.DRV` (`inst.inc`). **Proposed:** "DOS AH=36h's own register shape, and that is not an accident: DOS.O88 forwards these four to the program unchanged, and the other callers (SysCheck, PIXEL, XTMP3, REDLINE's inspector, os88wseq.inc, HDD.DRV's installer) read the same four."
3. **`kernel/kernel.asm:6654-6657`**: "DOS-only, and DOS is not on the small disks (SPEC.md 18.4.7.4)". **Code:** as above; REDLINE, through `os88wseq`, reaches it from `util360.img`. **Proposed:** "a refusal on this kernel (SPEC.md 18.4.7.4): DOS is not on the small disks, and every other caller tests CF - os88wseq.inc's open included, which then refuses the stream".
4. **`kernel/diskw.inc:6819-6821`**: "DOS.O88 is the one caller (SPEC.md 18.4.7.4) and it does not ship on the small disks". **Code:** `OSAPI_FILE_WRITE_AT` is also called by `os88data.inc`'s `od_write` (~20 packages), Cyclone, FTPD, MIDIRACK, WAVERACK and XTMP3. **Proposed:** "every caller tests CF: DOS.O88, which does not ship on the small disks, and os88data.inc's od_write, which then takes its full arm (SPEC.md 19.9.1)".
5. **SPEC.md 18.4.7.4**: "DOS is the only caller of the offset door and of `OSAPI_VOL_STAT`, and it does not ship on the small disks ... legitimate here because the one caller tests `CF`". **Code:** the callers in items 2 and 4. **Proposed:** "DOS was the only caller of the offset door and of `OSAPI_VOL_STAT` when they were cut, and it does not ship on the small disks. Both cells refuse here (`stc`), which is legitimate because every caller tests `CF`: `os88data.inc` falls back to a full write, and `os88wseq.inc`'s open refuses its stream."
6. **`apps/os88data.inc:261`** (`od_back`): "a word: we never left the volume". **Code:** under `OD_BOOTVOL` or `OD_TVOL` the visit is on another volume (`:150-163`, `:195`), so the way back is a quiet mount. **Proposed:** "a word when the visit stayed on our volume; a quiet mount after an OD_BOOTVOL or OD_TVOL visit to another one. A refusal here has nobody to tell, as the old FILE_GOTO's had not either".
7. **`apps/os88pit.inc:39-40`**: "ONE BODY, TWO USERS: ETHER.DRV's stage profiler (SPEC.md 72.15) and the FTP server's own split (SPEC.md 77.32)". **Code:** included by PIXELSTEIN, RealDMG, WIN16, MIDIRACK, FTPD, XTMP3, ARCADE, isoTron and `ETHER.DRV`. **Proposed:** "ONE BODY, MANY USERS: ETHER.DRV's stage profiler (SPEC.md 72.15) and the FTP server's own split (SPEC.md 77.32) were the first two." Also add a paragraph: "**NOT INSIDE AN ARMED FSXF_FASTTICK OR FSXF_RATE BRACKET.** Channel 0 then reloads at 65536/N or at the caller's divisor while 006C keeps its 18.2 Hz (SPEC.md 53.2.1-53.2.2), so the low half laps several times a tick and a span read across a lap goes backwards. Time a bracket by its hook's periods or by OSAPI_GET_TICKS, as isoTron and RealDMG do."
8. **SPEC.md 72.15.1**: "a stage may run eighteen hours before it wraps". **Code:** the high half is the low WORD of 006C, which laps in 65,536 ticks, and 2^32 × 838 ns = 3,599 s (`apps/os88pit.inc:6-9`). **Proposed:** "a stage may run an hour before it wraps".
9. **SPEC.md 53.2.1**: "the BIOS clock at 40:6C are untouched **by construction rather than by adjustment**, and nothing outside sched.inc can tell". **Code:** any package that latches channel 0 can tell; `os88pit.inc`'s `pit_now` reads a counter that now reloads N times a tick (isoTron says so at `itfs.inc:905-906`). **Proposed:** "...and nothing outside sched.inc that keeps to the tick can tell. A reader of channel 0's COUNT can: `apps/os88pit.inc`'s `pit_now` is wrong inside an armed bracket, and its users time one by the tick or by their hook."
10. **`apps/cc/crt0.asm:705-706`**: 'SPEC.md 73.4 pins the shape - "pops, then `or ax, ax`, then stc/clc, then ret"'. **Code:** `cmp ax, 1` / `ret` (`:734-735`), and SPEC.md 73.4 says "pops, then `cmp ax, 1`, then `ret`". **Proposed:** 'SPEC.md 73.4 pins the shape - "pops, then `cmp ax, 1`, then `ret`", because a `pop` does not write FLAGS and AX is the one register a thunk deliberately does not save.'
11. **`apps/cc/crt0.asm:1258-1261`**: "the name resolves in THIS INSTANCE's directory - the folder it was launched from (SPEC.md 19.2.1), so no visit is needed and none is made". **Code and SPEC 19.2.1:** a name resolves where the instance stands, which the file dialog moves (the lazy arm's comment, `:1158-1166`). **Proposed:** "the name resolves where THIS INSTANCE STANDS (SPEC.md 19.2.1), which the file dialog moves - so this arm refuses when the document lives in another folder, the hole CC_OVL_LAZY's go-home bracket closes. No shipped package takes this arm." And `:194-195`, "exactly what every unconverted package ships", becomes "exactly what an unconverted package ships (tests/cfsx is the last)".
12. **`apps/os88api.inc:6124-6128` and `:6136`** (`OSAPI_FILE_READ_SEQ`): "CX = its capacity (a cluster multiple, READ_AT's rule)", "set FSEQ_OFF (a cluster multiple)", "so a stale cursor is never an error". **Code:** the cell exists only on kern_big, where `READ_AT` takes whole sectors (`kernel/diskw.inc:3140-3146`, SPEC.md 18.4.4.2); and a cursor reused for another file is trusted (gi-4). **Proposed:** "CX = its capacity (whole SECTORS - READ_AT's kern_big rule, SPEC.md 18.4.4.2) ... To start or seek: zero it and set FSEQ_OFF (a sector multiple) ... so a stale cursor is never an error - but the cursor names NO FILE: zero it whenever the name or the folder changes, or the first file's clusters are read under the second's name (SPEC.md 18.4.8.1)". At `READ_AT` (`:6108-6109`), "Offset AND capacity must each be a cluster multiple" becomes "...a cluster multiple (kern_big takes any whole number of SECTORS, SPEC.md 18.4.4.2)".
13. **`kernel/diskw.inc:479-480`** (`dskw_dfree`): "BX = SECTORS per cluster (spc*512 overflows 16 bits at the SPEC.md 18.2-legal spc = 128)". **Code:** mount rule 4 refuses spc > 64, for exactly that overflow (`kernel/disk.inc:5106-5118`), and the same routine's body says "spc <= 64" (`:500`). **Proposed:** "BX = SECTORS per cluster (1..64, mount rule 4 - so spc*512 fits a word, but the cell has answered sectors since before the rule and callers multiply)".

## Open questions

- **gi-6 / `od_write` on kern_small.** Once the stub answers `FERR_NAME`, may
  `od_write` treat `FERR_NAME` from `WRITE_AT` as "no in-place door" and do the
  full write in the banked folder without a walk? NAV-COST-PLAN 4.4's rule is
  that a create or replace goes where a walk says. `od_enter`'s `GOTO_QM` has
  passed the media test by then, but that is not the witness the rule names. It
  is the owner's call; the cost of saying no is three `GOTO_QM`s and two FIND
  scans a save on the 128 KB machine.
- **Does REDLINE run on kern_small at all** (memory, refusals)? That decides
  whether gi-1 is user-visible today or only a trap for the next user.
- **gi-5's DS:DI cursor.** Confirm the cell layer can read a caller-DS pointer
  in a second register as cheaply as it reads the name at DS:SI. If not, keep
  ES:DI for the cursor and pass the buffer in DX:BX as now. The saving shrinks
  to the re-seed and kern_small alias logic, about 40 of the 76 bytes.
