# LENS: bytes - duplication and resident-size leverage across modules

Reviewer: the `bytes` lens of the 2026-11-08 kernel review (branch
`the-big-review`, cut from `elendilon`). Read-only: nothing in the tree was
edited but this file. All figures are kern_big unless they say otherwise.
**MEASURED** means read off `build/kernel.map` symbol spans, a whole-kernel
listing, or `tools/kernsize.py`; everything else is **ESTIMATED** and says so.

---

## Scope read

**Mechanically, all of it.** The size passes worked routine by routine, so
this lens worked the other way: two whole-kernel listings were assembled into
the scratchpad (never `build/`) with the build's own defines -
kern_big (`-DKERN_BIG` + `kernel.kz.json`'s KZ numbers; 151 bytes differ from
`build/kernel-full.bin`, all of them the `cfx_tab` stamp `os88cfx.py` writes
after assembly) and kern_small (byte-identical to
`build/smallk/kernel-full.bin`) - and every emitted instruction (213,915
listing lines on kern_big) was attributed to its routine and to its section
through `build/kernel.map`, `build/smallk/kernel.map` and
`build/megak/kernel.map`. Over that data: an instruction n-gram duplicate
search across `.text`/`.cold`/`.vga`, a whole-routine duplicate search, a
macro-expansion byte census (284 kernel macros), a pre-call register-setup
census for every call target with 4+ sites, a crossing census
(`.text`<->`.cold`, both directions, plus the `ct_` trampolines), a
push/pop/`cli`/`sti` census, a resident data/string census, and a
decoded walk of the API table (the `t_api_abi.py` shape decoder, imported,
not run as a row).

**By hand, line by line:** `kernel/kernel.asm` 498-610 (KSEG/CSDO), 1085-1170
(the build-shape defines), 1640-1800 (the ladder), 2380-2640 (cell macros,
CLC_OR_STC), 4300-4460 (table tail), 4535-4560 (debug registry), 4590-4690
(the rare bodies, `api_far`), 5740-5765 (the slide call), 7119-7335 (EXTCALL,
MODFP), 7760-8200 (every `cw_` shim, every `ct_` trampoline), 8380-8560
(`kent_*`/`kret*` ladders), 8691-8693, 8757-8860 (`cfx_slide`, `cfx_grp`);
`kernel/mod.inc` 170-262, 395-475; `kernel/driver.inc` 880-1000, 1100-1150,
1179-1230, 1379-1400, 3470-3545, 4625-4660, 5380-5410; `kernel/drvvol.inc`
185-300; `kernel/assoc.inc` 60-160; `kernel/wm.inc` 14-180, 1229-1260;
`kernel/instance.inc` 20-90, 150-185; `kernel/mouse.inc` 1636-1900,
2189-2280; `kernel/xmem.inc` (every label); `kernel/font.inc` (outline);
`kernel/vga12.inc` 491-533, 5082-5130.

**Docs:** docs/plans/KERNEL-BYTE-QUEUE.md (all), docs/plans/ABI-BREAK-LIST.md
(all), docs/plans/LAST-DROP-BYTES.md (headings, 7.6, 7.7, 7.10),
docs/plans/completed/HANDOFF-KERNEL-SIZE-P19.md (all - the last size pass),
docs/plans/completed/ONDEMAND-PLAN.md 1.1-1.3 and 7.1-7.2, docs/KERNEL-MEMORY.md
1-80, 455-530, 1095-1135; SPEC.md 2.6-2.6.2, 2.8-2.8.5, 2.10.5, 2.10.7,
2.10.9, 20.3's opening, 22.3.0; CLAUDE.md and AGENTS.md (the hard rules);
and the other areas' reports in this directory, so this one does not repeat
them (cross-references are named where a finding meets one).

---

## Verdict

Eighteen size passes have taken the kernel close to the floor of what
routine-by-routine work can reach. The evidence is that the structural
searches came back nearly empty. Across 79KB of resident code there is **no
duplicated routine** except `kent_bp`/`kent_di` and their `.cold` twins,
which are duplicated because `.cold` has a separate CS. There is **no n-gram
worth a helper** that is not a register-save run or a graphics inner loop,
and both of those were already priced and decided. The cross-segment
plumbing, about 700 bytes, is **spread over 74 and 81 distinct targets at
three sites or fewer each**, so no hub is left to move. What is left is
structural, and it takes one of four shapes:

1. **The API table's rare half is still a per-entry code cell.** Moving it
   behind one software-interrupt door with an inline index byte (DOS's and
   the Mac's model) is about -400 resident on kern_big and kern_mega and
   -250 on kern_small. It also takes 2 bytes off every rare call site in
   every package, and a cell newer than the running kernel becomes a
   refusal instead of a wild far call. Flag day; it supersedes T1.
2. **Hardware the target cannot have is still resident there.** The VGA rung
   showed the pattern. An "AT rung" holding the PS/2 runtime and the
   XMS bodies is about -400 on every 8088. No ABI change.
3. **The two biggest bodies that are modules on kern_small are resident on
   kern_big**, refused there because they would need the system disk. If
   they are preloaded in the boot read into a purgeable claim
   (COMMAND.COM's transient model), that objection goes away, and
   about 3.6KB stops counting as resident. No ABI change. It needs the
   module pin that ONDEMAND-PLAN 7.1 designed and never built.
4. **The register convention is the largest remaining tax.** It accounts
   for 4,791 bytes of push/pop, and AX alone is pushed 514 times. "Dead
   callee-saves" were the bulk of passes 16, 17 and 18. That is a
   convention over-saving systematically, and it cannot be fixed safely
   without a liveness gate. Recorded.

The documentation has drifted in this lens's own territory. SPEC.md 2.6
still describes the pre-`ROM_COLD` ladder. KERNEL-MEMORY.md says its
ladder is generated when it is prose, and that prose is 9KB stale.
AGENTS.md has lost CLAUDE.md's byte rules. The full list is under
mismatches.

---

## Strengths - keep these, and copy them

- **The shared epilogue ladders and prologue helpers**
  (`kernel/kernel.asm:8449-8560`: `kent_bp`, `kent_di`, `kret_*`, `kentc_*`,
  `kretc_*`, `kretfc_*`). 3 bytes a site against 6-8, 101 `.cold` and 43
  `.text` prologue sites, 174 epilogue jumps. Two gates protect them:
  stkbalance walks a rung as a CONTINUATION, and `t_asmrules`'s
  `crossed_pops` reads the rung->pop map out of the ladder. So the trick
  fails closed. Every 8086 project that wants `pusha` should copy it.
- **"The cell is the door"** (`kernel.asm:7808-7822`). 20 `cw_X equ apic_X`
  aliases cost zero bytes, because a cell already publishes the routine,
  and the rule for when it may be taken (per operation, never per draw) is
  written at the place.
- **`ct_` trampolines priced per build**: `2N - 6` bytes, with the per-build
  `%define` fallback to the plain far call (`kernel.asm:6954-6956`) when a
  build's site count drops to 2. A byte trade that re-evaluates itself per
  build is rare and right.
- **The VGA rung** (`.vga`, SPEC.md 2.10.9). It makes code that only one
  class of hardware runs cost nothing on machines without it: a ~100-byte
  transient slide in `.cfx` (`kernel.asm:8771-8854`) and a run-time door
  registry (`tests/vgarefs.txt`, enforced by `os88ovlchk` rule 2f). bytes-2
  proposes the second use of this mechanism.
- **Modules assembled with the kernel and cut out afterwards** (SPEC.md 2.8).
  There is no second ABI between the kernel and its own modules, and
  `t_moddrop` makes "freed when it is done" a gate rather than a hope. This
  is better than DOS overlays and Windows' load-time fixups. Neither of
  those could name the loader's own symbols without an export table.
- **One source, several images** (`CFG_MAP`/`CFG_DATA`,
  `driver.inc:894-1000`). The parser's tables are emitted into the boot
  overlay and into CTRL.DRV from one macro, edited once and resident
  nowhere.
- **Inline cells** (SPEC.md 20.3). They are both the smallest and the
  fastest shape in the table: `OSAPI_SET_COLOR` takes 52-57 cycles against
  159-162.
- **Generated seeds where data is regular**: `assoc_pg_seed` (14 bytes for a
  64-byte icon, `assoc.inc:85-87`), `IRUN` icon runs (`icons.inc:878`), and
  the row, pair and planar tables built at boot into `.lowbss`.
- **The measurement culture**: `kernsize`'s `accrued` line, symbol-span
  pricing and the "bytes not rungs" banner. Every figure in this report
  came off those instruments in minutes.

---

## Findings (ranked)

### bytes-1 (rethink, MEDIUM-HIGH, flagday) - the rare cells behind one software-interrupt door with an inline index

**Evidence (MEASURED, decoded off `build/kernel.bin` with `t_api_abi`'s
decoder).** kern_big's table is 1,259 bytes (`osapi_table` 0x0010 ..
`osapi_table_end` 0x04FB, `build/kernel.map:3136,3229`). It holds 44 SLOTs
(352), 11 X cells (77), **112 rare cells (672)** (RSLOT 50, RCXCELL 24,
RCSLOT 19, RNCELL 12, RXCELL 7), 20 inline cells (108), 6 jump cells, the
far and far-jump cells, and 3 fixed refusals. Each rare cell is
`push bp / call api_r<kind> / dw target` (`kernel.asm:2501-2521`). The
kind bodies are `api_rs`/`api_rsc`/`api_rx`/`api_rxc`/`api_rn`
(`kernel.asm:4596-4660`). Packages and drivers make **~7,000 source-level
`call OSAPI_*` sites** (grep over `apps/` and `drivers/`), each 5 bytes.

**The model.** DOS's `int 21h`/AH and the Mac's A-line trap dispatcher put
the function number at the call site and keep one table WORD per function.
For os8088's rare half:

```
package:   int OS88_RARE_VEC       ; CD nn
           db  OSR_WM_CREATE       ; the index - 3 bytes against 5
kernel:    rare_tab: dw target, target, ...   ; 2 bytes a cell against 6
```

The door reads the return CS:IP, fetches the index, bumps the return IP
past it, picks the kind by RANGE (a flag day renumbers the rare cells
sorted by kind, so no kind byte is stored), loads BP with the target, and
runs today's `api_rs`/`api_rxc`/... bodies unchanged. It returns with
`retf 2` rather than `iret`, so the body's CF/ZF answers survive (DOS
TSRs return that way for the same reason). It re-enables interrupts only
if the caller's stacked FLAGS had IF=1.

**Bytes (ESTIMATED from the measured counts):** -672 cells, +224 table,
+~45 door, so **~-400 resident on kern_big, the same on kern_mega (in
CONVENTIONAL memory, where P19 says the image rung has about 1 byte
left)**, and ~-250 on kern_small. Packages lose **2 bytes per rare call
site**. T1 (`ABI-BREAK-LIST.md` row T1, 5-byte rare cells) is -117 on the
same renumber, so this row supersedes it.

**What else it buys.** An index past the end of this kernel's table is
ONE COMPARE AWAY from a clean `CF = 1`. Today a package built against a
newer SDK far-calls into whatever bytes sit at that address in an older
kernel. That is exactly the hole `loader.md` loader-6 describes, and this
row closes it for every rare cell at run time, the way DOS answers an
unknown AH and the Mac's `_Unimplemented` does. Hooking a cell (a driver
patching a door, like the Mac's `SetTrapAddress` or Amiga's `SetFunction`)
becomes a table-word store, if that is ever wanted.

**Costs, honestly.** About +80-100 cycles a rare call on the 8088 (`int`
51 + `retf 2` 33 against `call far` 28 + `retf` 32, plus the index fetch
and range test), so ~20 us. The calls are rare by SPEC.md 20.3's own
definition (window create, files, claims). Three hazards have to be
designed out:

- the vector has to be owned against a windowed DOS program (`kerndos`/
  `apps/dos` must refuse or virtualize it);
- kern_mega needs an IDT gate and a readable code alias to fetch the
  index through the caller's CS;
- a rare cell called with IF=0 must not come back with IF=1.

Hot SLOTs and inline cells stay as they are: they are where the cycles
are.

**Recommendation:** price it properly at the next flag day, against T1, as
the table row. **Listed as:** T1 / K17-1 (supersedes), K17-4 (makes moot).

### bytes-2 (size, MEDIUM, now) - an "AT rung": the PS/2 runtime and the XMS bodies, slid over on an 8088

**Evidence (MEASURED spans, `.text`):**

- The PS/2 runtime is `mou_p2_byte` 114, `mou_p2_isr` 51, `mou_p2_off` 32,
  `mou_p2wr` 29 and `mou_p2wcmd`/`mou_p2dw`/`mou_p2cw` 25, **~251 bytes**.
  The file's own banner says *"NOTHING HERE RUNS ON THE TARGET MACHINE ...
  The whole module is behind [cpu_tier]"* (`mouse.inc:1642-1646`), and
  `mou_p2_init` is `cmp byte [cpu_tier], CPU_8086 / je .none`
  (`mouse.inc:1839-1841`).
- The XMS kernel half is `xm_copy` 83, `xm_release_rec` 42, `xm_dsp` 27,
  `xm_caps` 23, `xm_free` 20, `xm_alloc` 16, `xm_ownbl` 8 and more,
  **~250**. Every body opens with `call xm_have` (`xmem.inc:485-730`), and
  XMS cannot exist below a 286.

On every 8088 running kern_big (the target), those ~500 bytes are resident
and dead.

**Shape.** It is the VGA rung's mechanism, used a second time:
`[image][FAT][LOW][AT][VGA][COLD][VGABUF]`. `cfx_slide`'s delta becomes
`VGA_PARA + AT_PARA` on an 8088 with no VGA/EGA (`kernel.asm:5757-5760`
decides it now on `[vid_avail]` alone). AT goes BELOW VGA so that every
common machine slides a contiguous top:

- 8088 + CGA/Hercules slides both;
- 286 + mono slides VGA only;
- 286 + VGA slides nothing.

The rare 8088 + VGA (MartyPC's `os8088_xt_vga`) keeps the AT bytes, which
is no worse than today.

The HMA image's two hashes (`RT_SLIDES`) are unaffected: an 8088 never
loads COLDHMA.SYS, so no third slide value can meet it. The doors that
stay resident:

- `mou_p2_off`'s first `cmp byte [mou_p2], 0 / je` (it is called
  unconditionally from `mouse.inc:1264,1437`);
- one `call xm_have / jz <refusal>` front per XMS entry;
- the ISR, which needs no door because only `mou_p2_init` on a 286
  installs it.

A `tests/atrefs.txt` registry would play `vgarefs.txt`'s part.

**Bytes (ESTIMATED):** **-400 to -450 resident on an 8088**, +30-40 `.text`
of fronts on every machine. The 64KB window has 6,946 bytes free
(`kernsize` window line). It does not move the KERN_BUDGET gauge, which is
taken on VGA. It does move the machine the project is for.

**Companions:** video-17 (the mono renderer resident on VGA machines) is the
same idea run the other way, and input-12 (the keyboard mouse as a module).

### bytes-3 (rethink, MEDIUM-HIGH, now) - FILECP and FDLG on kern_big as boot-preloaded PURGEABLE images

**Evidence (MEASURED spans):**

- kern_big keeps `filecp.inc` (1,657 `.cold` + 152 `.bss`) and `fdlg.inc`
  (35 `.text` + 2,080 `.cold` + 84 `.bss`) resident, about 4.0KB.
- kern_small builds the same bodies as `FILECP.DRV` (1,835 + 76 bss) and
  `FDLG.DRV` (1,191 + 16). Its resident halves are fcp 85 `.cold` + 68
  `.bss` and fdlg 91 `.cold` + 36 `.text` + 15 `.bss`.

So the module machinery for exactly these two bodies already exists and is
gated (`FCP_MOD`, `FDLG_MOD`, `kernel.asm:1093-1103`).

**Why they are resident on kern_big:** ONDEMAND-PLAN 1.2 and 1.3. Loading
the module needs the system disk, and the dialog and the copy are exactly
when the user's disk is in the drive. That argument holds only for
loading **on demand**.

**The new argument.** DOS's answer to the same constraint was
COMMAND.COM's TRANSIENT portion: it is loaded with the resident part, it
lives in memory a program may take, and it is reloaded from `COMSPEC`
only if it was overwritten. Windows 3.x did the same with discardable
code segments, the Mac with purgeable `CODE` resources, and AmigaOS with
libraries expunged under memory pressure when their open count is zero.
For os8088 that means:

- Leave the two images in KERNEL.SYS's one contiguous boot read.
  `os88mod.py` already knows where they are, and they are ~6 sectors
  packed, so the boot costs about +120 ms on a 5150 (ESTIMATED off one
  more track of a sequential read), not two file lookups.
- Copy them into a `MEM_PG_LOW` claim at `mem_unblob`.
- Pin while entered: ONDEMAND-PLAN 7.1's per-module nesting count, held
  open-to-close for the dialog and across a copy.
- Re-read only when shed, through the refusal path kern_small already
  ships ("Needs Sys Disk A:", SPEC.md 2.8.4).

On every machine with heap to spare, nothing changes: zero disk reads.
Under pressure, the heap gets ~4KB back before it refuses a package.

**Bytes (ESTIMATED):** **~-3,600 resident on kern_big** (it becomes a
purgeable heap claim), and +60-100 `.cold` for the pin and the boot-time
copy. KERN_BUDGET counts "every non-purgeable byte", so the gauge moves by
the whole amount, honestly, because the bytes really are reclaimable.

**It generalizes.** The same tier would let CTRL.DRV and the others be
SHED rather than DROPPED (ONDEMAND-PLAN 7.2's "shed, do not free"). A
second open of the panel then costs nothing on a machine with room.

**Listed as:** ONDEMAND-PLAN 1.2/1.3 (refused; new argument: preload),
7.1/7.2 (designed, not built). It meets memory.md mem-10 (module pins)
and mem-15/hm-9 (purgeable classes): build the pin once for all three.

### bytes-4 (record, MEDIUM) - the two-segment tax, measured: ~700 bytes on kern_big, ~540 on kern_small, and spread too thin to cut

**Evidence (MEASURED, listing census):**

| | kern_big | kern_small |
|---|---:|---:|
| `.text` -> `.cold`: thunks (`call COLD_SEG:x / ret`, 6 bytes) | 27 | 12 |
| `.text` -> `.cold`: inline far calls/jumps | 63 | 34 |
| `.cold` -> `.text`: far calls (5 bytes) | 112 | 67 |
| `.cold` -> `.text`: near calls to `ct_` trampolines | 94 (16 trampolines) | 67 (16) |
| real `cw_` shims (`call x / retf`) | 46 bodies (+ 23 zero-byte `equ`s) | (same source) |
| distinct targets, either direction | 74 / 81 | - |

As near calls in one segment this would cost, ESTIMATED: 27x6 + 63x2 +
112x2 + 16x6 + the share of shims only the split needs (~60) + the
duplicated `kentc_*`/`kretc_*` ladder (~70) = **~700 bytes on kern_big**,
and by the same arithmetic **~540 on kern_small**. The busiest target has
13 sites. **No hub is left**: moving any single routine across saves 2-8
bytes, which is the routine-level floor the size passes reached.

**The reason has changed.** SPEC.md 2.6 says `.cold` buys `KERN_CODE_MAX`
and nothing else. Today `.text+.bss` has **19,842** bytes of KERN_CODE_MAX
left and the window 6,946 (`kernsize`). What the split buys now is
**movability**:

- the 5150 ROM (SPEC.md 2.10.5);
- the HMA on a 286 (2.10.7: 38KB of conventional back);
- the slide over the VGA rung (2.10.9).

**kern_small is the sharp case.** Its `.text+.bss+.cold` = 30,035 +
2,936 + 21,031 = **54,002**, inside 64KB with room. So every 128KB
machine without the ROM pays ~540 bytes for the ROM the owner's 5150 can
carry, and that ROM gives back 21KB.

**Recommendation:**

- Write the trade down (SPEC.md 2.6; mismatch M2).
- State the placement rule in terms of it: code goes `.cold` for
  MOVABILITY or cadence, and comes back to `.text` only where it removes
  crossings without costing the HMA machines their conventional bytes.
- Do not spend a pass on per-site migration: it is at its floor.

### bytes-5 (rethink, LOW-MEDIUM, record) - "preserve every register" is the largest systematic tax left

**Evidence (MEASURED census, resident sections):** push/pop of a GP or
segment register is **4,791 bytes** (`.text` 2,438, `.cold` 2,203, `.vga`
150), about 6% of resident code. Per register (pushes only): AX 514, CX
363, BX 347, SI 299, DX 276, DI 276, BP 222, ES 183, DS 176.
`HANDOFF-KERNEL-SIZE-P19.md` section 1: *"single-caller routines written at
their call, and callee-saves no caller reads, were the bulk of every
finder's bytes"*, for the third pass running.

When three independent sweeps keep harvesting the same class, the
convention generates the class. CLAUDE.md's rule (*"Public routines
preserve every register but their documented outputs"*) is right at the
API boundary. Inside the kernel it makes every new routine bank AX "to be
safe", and a later pass removes it.

**Prior art:** Windows 3.x and Microsoft C (AX, BX, CX, DX and ES are
scratch; SI, DI, BP and DS are preserved); the Mac Pascal convention (D0-D2
and A0-A1 scratch); GEOS's per-routine `destroyed:` lists, which are
os8088's `clobbers:` lines.

**Recommendation:** record it, not now. A kernel-internal convention of
"AX and flags are scratch unless the header says `preserves`" would stop
the class at the source. It is safe only behind a register-liveness gate
of the stkbalance kind, which checks every caller of a routine against its
header, and nothing in `tools/` does that today. The bytes cannot be
priced without that gate: plausibly a few hundred, mostly realized as
"the next pass finds nothing".

### bytes-6 (rethink, MEDIUM, flagday) - the handler words: new evidence for histsys-9 / K17-3

**Listed as:** histsys-9 (this review), ABI-BREAK-LIST K17-2 and K17-3.
Only the new evidence:

- **No package reads or writes a handler word through the record.**
  `grep -rnE "\+\s*W_(PAINT|ONKEY|ONCLICK|ONSIZE|ONMOUSEUP|ONDRAG|ONTIMER)"`
  over `apps/`, `drivers/` and `tests/` finds nothing. Packages reach them
  only through the template copy (`wm_create`'s 8 words) and the setter
  cells.
- **Inside the kernel, 28 read sites in six files** dispatch through them
  (`wm.inc` 17, `kernel.asm` 4, `ui.inc` 3, `ctrl.inc` 2, `files.inc` 1,
  `fdlg.inc` 1).

So moving the storage out of `wm_wins` (312 `.bss` on kern_big = 13 words
x 12 windows; 132 on kern_small = 11 x 6) breaks no package's CODE, only
its template and setter calls. The kernel edit is bounded to those 28
sites, which can go through one `wm_hget`-shaped helper (+2 bytes a site,
+~15 helper).

**Bytes:** histsys-9's ~-150 to -250 net stands. This evidence lowers its
risk, not its price.

### bytes-7 (record, LOW) - the queue's "alternative nobody has priced", priced: not worth it

KERNEL-BYTE-QUEUE.md 1 proposes `call far cw_call_imm / dw body` (7 bytes a
site) to delete the remaining `cw_` shims without a liveness read. Pricing
it on this tree:

- the trampoline must preserve every register and flag, and read its word
  through the CALLER's CS with an `lds` and a `lodsw`, which is ~25-30
  bytes;
- each single-site shim then nets -4 + 2 = -2, and each extra site costs
  +2;
- with 46 shim bodies left, of which perhaps 20 have one site (the queue's
  census was 50 of 90; ESTIMATED), the total is **~-15 to +10 bytes**, for
  ~+100 cycles a call.

**Recommendation:** close the row with this price. The shim sweep is done.

### bytes-8 (size, LOW, now) - the module names' `.DRV`

drivers.md drivers-10 covers the 22 driver-row names. The module table has
another eleven on kern_big (`mod_f_*`, `mod.inc:180-215`) that `mod_need`
hands to `drv_find` (`mod.inc:431-432`). That is 44 bytes of the same four
characters. If drivers-10's staging buffer is built, `mod_need` can stage
through it too and store the stems.

**Bytes:** ~-35 net kern_big, ~-15 kern_small, ESTIMATED. Fold it into
drivers-10 rather than doing it alone.

### bytes-9 (weakness, LOW, now) - KERNEL-MEMORY.md says its ladder is generated, and it is prose

The file's header (`docs/KERNEL-MEMORY.md:9-14`) says *"The section sizes,
the rungs, the ladder, the per-module table and the theme table are all
GENERATED; `tools/kernsize.py --bless` rewrites them"*. `--bless` writes
three blocks: the JSON baseline (`kernsize:begin`, 304-438), the theme
table and the module table (`tools/kernsize.py:196-198, 1135-1136`). The
ladder table (466-475) and the Task Manager table (504-509) are
hand-written. They are **~9KB stale** (`KERN_SIZE` 105,984 against 96,768)
and describe the pre-`ROM_COLD` order.

**Recommendation:**

- Have `--bless` write both tables between markers. `kernsize` already
  prints the ladder line it needs.
- Fix the prose until then (M5-M8).

0 bytes. It is the same class as memory.md mem-12 ("orphaned headers ...
are where the documentation rots").

### bytes-10 (weakness, LOW, now) - AGENTS.md has lost CLAUDE.md's byte rules

CLAUDE.md:5: *"`CLAUDE.md` and `AGENTS.md` carry the same repository
guidance. Update both"*. AGENTS.md is 786 lines against 1,370. Some of the
gap is the fork section, which is deliberately CLAUDE.md's. But it also
lacks:

- the **"RUNGS ARE TEMPORARY. BYTES ARE FOREVER."** banner;
- the **"Design for BYTES, never for rungs"** rule;
- the **"`.cold` IS RESIDENT, and the name is the trap"** paragraph;
- **"A PACKAGE'S OWN REGION ... RATCHET"**.

Each is present in CLAUDE.md and absent from AGENTS.md (grep counts 1/0).
A Codex agent reading AGENTS.md is therefore not told the rules this lens
exists to enforce.

**Recommendation:**

- Port them (M11).
- Add a `checkdocs` gate that diffs the shared sections, so "update both"
  is checked rather than asked.

0 bytes.

---

## Rethinks (what would be better done an entirely different way)

1. **The API surface as an index, not an address** (bytes-1). Every
   generation of 8086 system software that had to be both small and
   versioned converged on a call site that NAMES a function rather than
   JUMPS to it: DOS's AH, the Mac's trap word, Amiga's library vector
   offset, Windows' import ordinal resolved by the loader. os8088 chose
   fixed addresses because it refuses relocation, and that was right for
   speed. The rare half pays for it twice: 4 table bytes a cell, and no
   way to refuse a cell the kernel does not have.
2. **Machine-shaped residency** (bytes-2, video-17, input-12). The VGA rung
   proved that "resident" can mean "resident on machines that can run it".
   Taken as a rule rather than a one-off, the kernel would be laid out
   in rungs by hardware class, so the 8088 target carries only what an
   8088 can execute. That is the IBMBIO-per-OEM idea, done at boot by the
   kernel itself.
3. **A purgeable tier between resident and on-demand** (bytes-3). Today a
   byte is either resident (always paid) or a module (a disk read on
   every use, which needs the system disk). DOS, Windows, the Mac and
   AmigaOS all had the middle tier: present until memory is needed.
   With it, the kern_big/kern_small module split stops being a
   build-time decision about the user's drives and becomes a run-time
   one about the heap.
4. **A scratch-register convention behind a liveness gate** (bytes-5).

---

## Prior art (systemic)

- **MS-DOS.** It does three things better than os8088 here:
  - one table word per function, with a refusal for an unknown one;
  - the resident/transient COMMAND.COM split, which reloads from
    `COMSPEC` with a checksum;
  - per-OEM IBMBIO for hardware.

  os8088 does better on the module ABI (modules are assembled with the
  kernel, with no export table at all) and on accounting: DOS never knew
  what its own resident bytes bought.
- **Windows 3.0/3.1.** Discardable code segments, reloaded through
  `INT 3Fh` thunks, are bytes-3's model. Import by ordinal, resolved at
  load, put a direct far call at every site with no per-entry kernel
  cell; os8088 cannot copy that without relocation. One `lpfnWndProc` per
  window CLASS is bytes-6. os8088 does better: there is no fixup pass, so
  a 4.77MHz load stays a read.
- **OS/2 1.x.** Call gates and ordinal DLL imports. The useful lesson is
  that the gate (bytes-1's door) is also where a protection boundary goes
  later: L2's ring 3 on kern_mega would want exactly one door.
- **Mac System 5-7.** The A-line trap table is 2 bytes at the call site
  and patchable. The Segment Loader's jump table entries are 8 bytes
  each, "loaded" or `_LoadSeg`. That is os8088's hot SLOT almost exactly,
  and the Mac kept it only for intra-application calls. Purgeable
  resources are bytes-3. The ROM/RAM split (Toolbox in ROM, patches in
  the System file) matches os8088's `.cold`-in-ROM, which os8088 does
  more cleanly: the ROM adopts a kernel and checks that it is the same
  one.
- **GEOS** (the closest peer: a GUI on an 8088). All system code
  outside the fixed kernel lives in movable and discardable resources,
  swapped to disk or EMS, and the library entry points are resolved by
  number. GEOS's resident kernel was small because almost everything was
  purgeable. That is the direction of bytes-3, carried further. Its
  objects carry a class pointer, not per-instance method pointers
  (bytes-6).
- **AmigaOS.** Library vector tables are 6-byte `JMP` entries at
  negative offsets from a base: the same price as os8088's rare cell.
  `SetFunction` patches them. Libraries with an open count of zero are
  EXPUNGED on low memory, which is the pin plus purge that ONDEMAND-PLAN
  7.1/7.2 designed.

---

## Doc/code mismatches

(The ids M1-M12 are this file's; the structured result carries the same
entries.)

- **M1** - SPEC.md:1053, *"It sits between the image rung and the FAT window,
  so it"*. Every shipped kernel defines `ROM_COLD` (`kernel.asm:1121-1123`),
  so `COLD_RAM` is `VGA_RAM + VGA_PARA` on kern_big (`kernel.asm:1674`) and
  `LOW_SEG + LOW_PARA` on kern_small (`:1676`), with `FAT_SEG = KERNEL_SEG +
  KIMG_PARA` (`:1792`). The ladders are `KERNEL 0x0060 FAT 0x0ba0 LOW
  0x0cc0 VGA 0x0e2f COLD 0x0eae VGABUF 0x17da HEAP 0x1800` (big) and
  `KERNEL 0x0060 FAT 0x0880 LOW 0x08c0 COLD 0x0994 VGABUF 0x0eb7 HEAP
  0x0ec0` (small). Proposed: *"It sits at the TOP of the ladder - after the
  FAT window, `.lowbss` and (kern_big) the VGA rung, `ROM_COLD`'s layout on
  every shipped kernel (§2.10.9) - and rides the same contiguous boot read,
  after the zero gap those leave."*
- **M2** - SPEC.md:1056, *"What it buys is `KERN_CODE_MAX` and nothing
  else"*. A `.cold` with its own CS is what lets it run from a 5150 ROM
  (§2.10.5), from the HMA on a 286 (§2.10.7, `xm`/COLDHMA.SYS) and slid
  over the VGA rung (§2.10.9). KERN_CODE_MAX itself has 19,842 bytes free.
  Proposed: *"What it buys AGAINST THE BUDGETS is `KERN_CODE_MAX` and
  nothing else ... What it buys a MACHINE is movability - the ROM, the HMA
  and the slide (§2.10.5-2.10.9) - which no budget counts, because
  KERN_BUDGET is taken with no ROM and no XMS."*
- **M3** - SPEC.md:1406 and 1428-1429, *"`kernel.asm` carries eighteen,
  every target with at least four `.cold` sites"* and *"Five of the
  eighteen are break-even or worse on `kern_small` (`cw_snd_beep` has two"*.
  The code (`kernel.asm:8086-8126`) has seventeen names and sixteen per
  kernel: `ct_cw_snd_beep` is kern_big's and `ct_cw_wm_clip_test`
  kern_small's, and the other build takes the plain far call through the
  `%define` block (`kernel.asm:6954-6956`). `ct_cw_font_run` is at 2/3
  sites (mostly tails) and `ct_cw_wm_content` at 3/3. Proposed:
  *"`kernel.asm` carries sixteen on each kernel (seventeen names), taken
  per build: a target whose call count falls to two goes back to the plain
  far call through the `%define` block above the includes (three is a
  wash), and the counts beside each are kern_big's / kern_small's."* -
  deleting the "Five of the eighteen" sentence.
- **M4** - SPEC.md:44378, *"at `0x04EF` a 118th, **1,253 bytes**, and
  `osapi_table_end` is `0x04F5`."* `OSAPI_VOL_CHECK` is an `OSAPI_RCSLOT` at
  0x04F5 (`kernel.asm:4440-4447`), `osapi_table_end` is 0x04FB
  (`build/kernel.map:3229`) and the table is 1,259 bytes. The decoded kern_big
  table is 44 SLOT, 11 X, 112 rare, 20 inline, 6 jump, 1 far, 1 far-jump and
  3 fixed refusals, so the running ordinals ("118th") no longer match a
  count (cells became inline in pass 14). Proposed: *"... `OSAPI_FS_OP`
  (§12.8.10) at `0x04EF`, 1,253 bytes, and `OSAPI_VOL_CHECK` (§118.14.2) at
  `0x04F5`, **1,259 bytes**; `osapi_table_end` is `0x04FB`. The ordinals
  above are the order cells were appended in; `t_api_abi` prints today's
  counts by shape."*
- **M5** - docs/KERNEL-MEMORY.md:470-471, *"| `.cold` (40,432) | `COLD_SEG`
  0x0D60 | 40,448 |"* (and the image row, *"`.text` 47,333 + `.bss`
  5,525"*). The ladder is `[image][FAT][LOW][VGA][COLD][VGABUF]` at 0x0060 /
  0x0BA0 / 0x0CC0 / 0x0E2F / 0x0EAE / 0x17DA, heap 0x1800; `.text` 40,328,
  `.bss` 5,366, `.cold` 37,566, `.vga` 2,030, `.lowbss` 5,358, KERN_SIZE
  96,768 (`kernsize`). Proposed: rows in ladder order - image (`.text` 40,328
  + `.bss` 5,366) at 0x0060, 46,080; FAT window at 0x0BA0, 4,608; `.lowbss`
  (5,358) + task 0's stack at 0x0CC0, 5,872; `.vga` (2,030) at 0x0E2F,
  2,032; `.cold` (37,566) at 0x0EAE, 37,568; `.vgabuf` (336) at 0x17DA to the
  512 round; **KERN_SIZE 96,768**, heap at 0x1800 - preferably emitted by
  `kernsize --bless` (bytes-9).
- **M6** - docs/KERNEL-MEMORY.md:477, *"`kern_small`'s ladder is `KERNEL
  0x0060  COLD 0x0a00  FAT 0x1060  LOW 0x10a0"*. `kernsize -DKERN_SMALL`:
  `KERNEL 0x0060 FAT 0x0880 LOW 0x08c0 COLD 0x0994 VGABUF 0x0eb7 HEAP 0x0ec0
  = 59.0 KB`, KERN_SIZE 58,880. Proposed: *"`kern_small`'s ladder is `KERNEL
  0x0060  FAT 0x0880  LOW 0x08c0  COLD 0x0994  HEAP 0x0ec0` - `KERN_SIZE`
  **58,880 bytes**, the heap starting at 59.0 KB - so a 128KB machine has
  69.0 KB of heap by arithmetic"*, with `tests/small128.py`'s measured figure
  re-taken rather than kept (it describes 71,168).
- **M7** - docs/KERNEL-MEMORY.md:500 and 509, *"**104** on `kern_big`"* and
  *"| **`System`** | **`KERN_SIZE`** | **105,984** | **104** |"*. `KERN_KB =
  (KERN_SIZE + 1023) / 1024` (`kernel.asm:9239`) = 95 at 96,768; `SKB_IMG =
  KERN_SIZE - SKB_FAT - SKB_STK - SKB_STK0` (`:9275`) = 88,832. Proposed:
  *"**95** on `kern_big`"*; Code+data 88,832; System **96,768** / **95**
  (or generated, bytes-9).
- **M8** - docs/KERNEL-MEMORY.md:487-488, *"stage 2's blob (`.boot2`, 4,751
  bytes"* and *"(`.ovl`, 5,075 bytes"*. `kernsize`: `boot2 6,478` (the
  guard's `boot2_end` is 6,652 of 6,656, P19 section 6) and `ovl 5,119`.
  Proposed: *"stage 2's blob (`.boot2`, 6,652 of its 13 sectors' 6,656 on
  kern_big: the loader, the splash and the boot overlay's blob half) and the
  overlay's window half (`.ovl`, 5,119 bytes ...)"*.
- **M9** - kernel/kernel.asm:7810 and 7818, *"Thirteen shims below are
  `cw_X equ apic_X`"* and *"drawing shims a repaint calls by the dozen
  (gfx_fill/lock/unlock/frame,"*. There are 20 `equ apic_*` aliases
  (`kernel.asm:7823-8060`), and `cw_gfx_lock`, `cw_gfx_unlock` and
  `cw_task_yield` are among them since size pass 13 (`:7908`, `:7914`, `:7970`).
  Proposed: *"Twenty shims below are `cw_X equ apic_X`"* and *"NOT for the
  drawing shims a repaint calls by the dozen (gfx_fill/frame/fill_pat,
  font_run - the lock pair joined in size pass 13 because a lock is per
  OPERATION),"*.
- **M10** - docs/plans/KERNEL-BYTE-QUEUE.md:19, 99 and 178, *"carries **90
  `cw_` shims** after the first cut"*, *"`hb_probe_x` (`kernel/hiber.inc`) is
  ~60 of hibernate's `.cold` bytes"* and *"`.lowbss` is at **478/512 with 34
  bytes left**"*. There are 46 shim bodies plus 23 zero-byte aliases;
  `hb_probe_x` is a `BLOBCALL` into `.boot2` (`kernel.asm:5966`,
  `build/kernel.map:2949`), so item 2 is DONE; `LADDER_PACK` rounds LOW's
  top to the paragraph and `kernsize` reports no `low` rung, so item 4 is
  moot; the header figures (text 50,636, cold 37,998) are 18 passes old.
  Proposed: a status banner - *"**Status at 2026-11-08 (kernel size pass
  18):** item 1 is at 46 shim bodies and 23 zero-byte aliases and its
  inline-trampoline alternative is priced at ~0 net (big-review bytes-7);
  item 2 is TAKEN (`hb_probe_x` is in the blob); item 3 was superseded in
  pass 4; item 4 is moot under `LADDER_PACK`. The figures below are the tree
  this was written on."*
- **M11** - AGENTS.md:382, *"**Before spending a resident byte, ask whether
  the feature is an ON-DEMAND"* - the place the missing paragraphs belong.
  CLAUDE.md:307-327 (the "RUNGS ARE TEMPORARY" banner), CLAUDE.md:448 (the "Design for
  BYTES, never for rungs" bullet), CLAUDE.md:432-442 ("`.cold` IS RESIDENT")
  and CLAUDE.md:491 (the "A PACKAGE'S OWN REGION ... RATCHET" bullet) are absent from
  AGENTS.md (grep 1/0 each), against CLAUDE.md:3-5's "carry the same
  repository guidance" (CLAUDE.md:5). Proposed: insert CLAUDE.md's text verbatim at the
  same positions in AGENTS.md's Hard rules.
- **M12** - CLAUDE.md:433-436, *"Those bytes are in RAM from boot to
  power-off exactly like"*. On a 286 with XMEM.DRV kern_big copies `.cold`
  to the HMA and gives its RAM to the heap (SPEC.md 2.10.7: 38.0KB), and a
  5150 with the ROM never holds it in RAM (2.10.5). The rule's POINT - a
  `.cold` byte is billed by KERN_BUDGET like a `.text` byte - is right,
  because the budget is taken with no ROM and no XMS. Proposed: *"Those
  bytes are in RAM from boot to power-off exactly like `.text`'s on the
  machine the budget is measured on (no ROM, no XMS - a ROM or the HMA can
  take `.cold` out of RAM, SPEC.md 2.10.5-2.10.7, and no budget counts
  that) and are billed by `KERN_BUDGET` exactly like `.text`'s"*, and the
  same in AGENTS.md once M11 lands.

---

## Open questions (for the owner)

1. **bytes-1:** is a software-interrupt door acceptable at all for the
   rare half, given ~+20 us a rare call and a vector the kernel must own
   against windowed DOS programs? If yes, it should replace T1 on the
   flag-day list rather than sit beside it.
2. **bytes-3:** does ~+120 ms of boot read on a 5150 buy ~3.6KB of
   reclaimable kernel on kern_big? And should the same pin and purge tier
   then turn the existing modules' DROP into a SHED?
3. **bytes-4:** should kern_small's crossing tax (~540 bytes, paid on every
   128KB machine for the ROM option) be written into SPEC.md 2.6 as an
   accepted trade? That is the cheap answer; a ROM-less single-segment
   kern_small is the expensive one.
4. **bytes-5:** is a register-liveness gate worth building, so that the
   kernel-internal convention can change and the "dead callee-save" class
   stops being re-found every pass?
