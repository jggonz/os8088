# DOS-HEADLESS-PLAN - a DOS program with no DOS window behind it

**Status: a design study. Nothing here is built.** Written on branch
`dos-headless-study`, cut from `dos-windowed` at `80e2d393`, on 2026-11-02.
Every figure is **M** (measured on this tree, method in section 0) or **E**
(estimated, with what it was estimated from). An **E** is a promise to
measure, not a fact. No prototype was booted: no figure here is a MartyPC
reading of new code, and section 7 names the measurement each wave owes.

The owner's item:

> a previous session estimated removing the entire window at ~13KB. This is
> attractive as an option - drop it for things that will never run in a
> window, at the expense of getting any closing error output, in exchange for
> more RAM at launch - "DOS headless mode". I don't think a part is the best
> option here, but rather something more like SOUND.DRV's "drop it after
> loading if we don't need it" mode. The reason to forgo a part is the
> additional launch time and the speed loss having it FAR would cost - if we
> can moot those two, then it could be a part.

and, after it, *study the console (and anything that can go with it)*.

## 0. The answer in one screen

**The window is 19.4 KB of every DOS instance, not 13** (M, section 1):
14,232 bytes of code and 5,142 of bss, of which the console alone - its
renderer, its CP437 face, its 4,000-byte screen and the prompt that types
into it - is 11.4 KB. The ~13 KB figure was close to the CODE alone and left
out the console's buffers.

**A headless DOS host is 25.2 KB** (M by span, section 3) against today's
44,898 bytes - and 18,951 of the 25.2 KB is the core's slot, which kern_dos
shares and which this study does not touch. The host's own share of a
headless run is **6.1 KB** (M).

**Both shapes the owner named are buildable, and they are ONE LAYOUT with two
cut points.** Put everything that only shows the window LAST in the instance's
region - its code and its bss - so the region is `[header][core slot][the
resident host][the window]`. Then:

| | (a) drop after load, SOUND.DRV's shape | **(b) the window as a NEAR part, cut by the loader** |
|---|---:|---:|
| RAM a headless program gains | **~19.2 KB (E from M spans)**, after one posted compaction moves the region up | **~19.2 KB (E from M spans)**, with no compaction at all |
| launch time, headless | **+~0.1 s (E)**: today's read, then the compaction | **-~0.7 s (E)**: ~22 fewer sectors read, 14.5 KB less decoded |
| launch time, windowed | +0 | **+0 calls (E)**: the two parts are one coalesced read |
| speed while the window is up | **+0** | **+0** - the window is at the SAME CS, every call stays near |
| `DOS.O88` on disk | +0 | **+~330 bytes (M-ish: the split's lost LZ4 context, section 4.3)** |
| resident in a windowed instance | +~100 (E: the gates) | +~100 (E: the gates) + at most 15 of paragraph pad |
| loader (transient, ships raw) | 0 | **+~27 (E)** |
| kernel | 0 | **0** |
| what can ask for it | anything the host knows at its entry | what the loader can read: a byte the `.LNK`'s first cluster already holds |

**Recommendation: (b), and keep (a)'s shrink as a second door on the same
layout.** It is a part, and it moots both of the owner's objections to one:
it is not FAR (the window is assembled in the host's own assembly at the
host's own CS, contiguous with it in the carve), and it adds no launch time -
it REMOVES ~0.7 s from a headless launch and is one read either way for a
windowed one, because two adjacent eager parts of the carve are read as one
run (SPEC.md 20.12.7.5's walk, `OP_CHUNK` = 32 KB). The refusal that sent
DOS-PARTS-PLAN section 2 to far code was about GROWING a region into place;
**shrinking, or never claiming, the top of a region has no such problem**
(section 4.1). What headless costs the user is that the instance ends with
its program: the window cannot come back without a grow, so it does not.

## 1. What "the window" is in the DOS host today

### 1.0 The instrument

`build/dosp.bin` (part 0 of `DOS.O88`, the re-homed host) with the shipped
flags, and its `[map all]` (`-DDOSP_MAP`, which the Makefile already writes
for `tools/os88dvwexp.py` - so the map describes the shipped bytes exactly,
no wrapper needed). Each global label's span runs to the next label and is
charged to the file that defines it; locals belong to their proc. The bss is
in the same map: the package's bss cells are `equ`s past `os88_image_end`
and nasm lists them with the code. Every routine was then put in a bucket by
hand (section 1.2), and `ndisasm` over the resident bytes counts every branch,
memory operand and immediate that names a WINDOW address (section 1.3).
Packed sizes are `tools/os88lz.py`'s `lz4_compress` over the bucket's bytes in
address order. The scripts are throwaway and not committed.

Part 0 today (M): image **38,264**, bss **6,634**, so **44,898** a DOS
instance - of which 19,079 is the header, icon, association block and the
core's reserved slot (`CORE_ORG` + `CORE_MAX` 15,440 + `CORE_BSS_SIZE`
3,511, SPEC.md 96.44.5) and **19,185 bytes of the host's own code**.

### 1.1 By file

| file | host code (M) | what it is |
|---|---:|---|
| `apps/dos/dos.asm` | 7,197 | everything not below: entry, wake, run, the doors and hooks, memory arms, drivers, `.LNK` reader, handoff, the bar, the path box, the Setup glue, menus, strings |
| `apps/os88ui.inc` | 2,778 | buttons, the alert (`os88ui_ask`), the About card, check box, radio, drop-down |
| `apps/dos/dosc.inc` | 2,442 | the PROMPT: the console's verbs, `.O88` at the prompt, `OPEN`, `HELP`'s glue, the full-screen console, `dos_snap` |
| `apps/os88con.inc` | 2,222 | the console: compose, emit, scroll, the cell marks, the text-mode renderer `con_tx_*` |
| `apps/os88cp437.inc` | 2,048 | `con_glyf`, the console's face (768 of it a hole `con_font` fills off the ROM, SPEC.md 70.8.6) |
| `apps/os88line.inc` | 935 | the line editor: the path box, the arguments field, the Setup fields |
| `apps/dos/dosw16.inc` | 324 | `dos_w16_try`, an NE typed at the prompt or behind a `.LNK` |
| unattributed (macro-made) | 483 | the windowed layer's stubs and thunks (`hent_*`, `dvwh_*`), the menu block, the shared epilogue |
| `apps/dos/dosw.inc` + `doswr.inc` | 389 | the windowed layer's RESIDENT half (SPEC.md 96.54.9): the door table, the worker's loop, fetch, patch, pin |
| `apps/os88pself.inc` | 207 | `op_self` |
| `apps/os88rseq.inc`, `os88sock.inc`, `dospkt.inc`, `os88api.inc` | 160 | the back end's sequential reader, `net_try`, two packet-driver doors |

### 1.2 By what a HEADLESS run needs

A headless run is a program run in the bracket (SPEC.md 96.2) - or handed
the whole machine (SPEC.md 96.40) - by a DOS instance that never shows its
console, and that ends when the program does. The buckets:

| bucket | code (M) | bss (M) | total (M) | what is in it |
|---|---:|---:|---:|---|
| **R** resident: launch, run, end | 2,484 | 412 | **2,896** | entry, wake, `dos_run`, `dos_fsx_main`, the 25 back-end doors, `os88_rseq`, the hooks a bracket uses (`DHK_POLL`, `DHK_TXT`, `DHK_MOUSE`, `DHK_CLAIM`/`FREE`), the memory arms and the cache dial, the driver suspend and `BLASTER=`, `op_self` + `dos_pself` (the network part's fetch), the parts standard's own bss, every setting's record (`dos_mrad`, `dos_mwin`, ...), `dos_fsi`, the driver query buffer |
| **N** network glue | 322 | 5 | **327** | `dos_pkt_want`/`_bufs`/`_start`/`_shut`, `net_try` - the part itself is already lazy (SPEC.md 96.23.11) |
| **L** the `.LNK` reader | 771 | 514 | **1,285** | `dos_lnk_*` and the 512-byte `dos_lbuf` - needed BEFORE the run, dead during it |
| **K** the whole-machine handoff | 508 | 561 | **1,069** | `dos_handoff`, `dos_lbfill`, `dos_wholeask`, the 561-byte KDH record (`dos_wake` reads it on every wake, SPEC.md 96.41.4) |
| **X** the exit report | 544 | 0 | **544** | `dos_fmt_exit`, `dos_err_line`, the twelve `DER_*` sentences |
| **W16** | 324 | 0 | **324** | `dos_w16_try` (section 5.4 says why it can go) |
| **W** only to SHOW the window | **14,232** | **5,142** | **19,374** | below |

**The W bucket, which is the window** (M):

| | bytes |
|---|---:|
| the console: `os88con.inc` 2,222 + `con_glyf` 2,048 + its bss 4,817 (`con_scr` 4,000, `con_band` 640, `con_run` 81, cursor and marks 96) | **9,087** |
| the prompt, `dosc.inc` (the verbs, `.O88`, `OPEN`, the full-screen console, `dos_snap`) | **2,442** |
| controls, `os88ui.inc` | 2,778 |
| the line editor, `os88line.inc` | 935 |
| `dos.asm`'s window code: paint, About, the bar and path box, keys, clicks, focus, the menu commands, the arguments field, the Setup page's glue (`dos_sp_*`), menus and labels, `dos_hk_tty` | 2,947 |
| the windowed layer's resident half and its stubs (`dvw_betab`, the worker loop, `hent_*`, `dvwh_*`, fetch/patch/pin) | 860 |
| `dos.asm`'s window bss: the path box's line block, the arguments and environment field blocks, button records, scratch | 325 |
| **total** | **19,374** |

So the console and the prompt that types into it are **11.5 KB of the 19.4**
(M) - the owner's "the console (and anything that can go with it)" is most of
the answer, and it is section 6.

### 1.3 How tangled it is

`ndisasm` over the R, N, L, K and X code finds **81 references into W** (M):
30 branches, 33 memory operands and 18 immediates, in 25 routines. **53 of
them are in four routines** - `dos_run` 20, `dos_entry` 14, `dos_fsx_main`
11, `dos_wake` 8 - which are exactly the four a headless host needs its own
arm of anyway: the window's creation, the console's seeding of the bracket,
the exit line, the repaint and the windowed layer's branches. The other 28
are a handful each, and some are false positives (an immediate that happens
to equal a W address). Fourteen bytes of the windowed layer's cells
(`dvw_want`, `dvw_cap`, `dvw_run`, ...) are READ by `dos_run` and `dos_wake`
and must stay resident: they are counted in W above and move to R in the
design (section 3).

### 1.4 What a run actually uses, and when

| | before the run | during it | after it |
|---|---|---|---|
| bracket (`Run`, SPEC.md 96.2) | R, L; W draws the window, the "Starting" line | R (doors, hooks); INT 21h output is the ROM's teletype (`dos_hk_tty` returns CF = 1 under `[dos_inbr]`) | W: `dos_snap` puts the program's last screen in the console, `dos_con_ended` writes the exit line, the window repaints |
| whole machine (SPEC.md 96.40) | R, L, K | nothing of DOS.O88 at all - kern_dos owns the machine | R reads the KDH record; W prints the exit line |
| in a window (SPEC.md 96.54) | everything | everything: the console is the program's screen | everything |

**Nothing of W runs while a bracket program runs**, M by reading the hooks:
the core reaches the host only through `dos_bevec` and `dos_hkv`, and the
only W hook is `DHK_TTY`, which under the bracket answers "the ROM's" on its
first compare. W is BEFORE and AFTER. That is what makes the window
removable - and also what removing it costs: it is exactly the AFTER that
goes.

## 2. What headless loses, exactly

1. **The program's last screen.** Today `dos_snap` (`DHK_SNAP`) copies the
   text screen into the console as the bracket ends (SPEC.md 96.34). Headless:
   the screen is gone the moment the bracket returns.
2. **The exit line** - `ended, exit code N`, the arena it had (SPEC.md
   96.33.19, 96.33.20).
3. **A refusal's sentence** - `DER_*`, written into the console by
   `dos_err_line`: out of memory, not found, too big, not a program.
4. **The prompt, Run again, Setup, HELP, Save Shortcut** - there is no
   window to do them in.
5. **The confirmation before the whole machine** (SPEC.md 96.42): it is an
   `os88ui_ask` dialog, which is W.

What **survives**, for 544 resident bytes that are already counted in the
floor (the X bucket): a **toast** (SPEC.md 59) - one 24-character line on the
menu bar, COPIED by the kernel so the instance may close while it is up
(`OSAPI_TOAST`'s own note). `PRINCE.EXE: exit 3`, or a refusal's short form,
`NAME.EXE: no memory` - the user's name last, so what the truncation eats is
the name and not the words. A toast on EVERY exit is noise; on a non-zero
exit code or a refusal it is the one thing worth saying. **~+60 bytes (E)**:
the short forms (the `DER_*` sentences are 19 to 58 characters and the toast
shows 24) and the call.

**Optional, and recorded rather than proposed**: a `Wait for a key` at exit.
In the bracket the program's screen is still up when it returns to us; a
headless host can bind a 6-byte `DHK_TTY` that only sets a flag and answers
CF = 1, and if the program printed through INT 21h, print `Press a key` with
the ROM's teletype and wait on `int 16h` before leaving the bracket. ~+35
bytes (E). It catches a utility that prints and exits; it cannot catch one
that writes the screen itself.

## 3. The floor: what a running headless DOS program's host costs

| | bytes |
|---|---:|
| the header, the icon, the association block and the glyph (`CORE_ORG`) | 128 (M) |
| the core's slot: `CORE_MAX` 15,440 + `CORE_BSS_SIZE` 3,511 | **18,951** (M) - the core is 15,438 of its 15,440 |
| R + N + L + K + X | **6,121** (M spans) |
| the 14 windowed-layer cells `dos_run`/`dos_wake` read, moved from W | +14 (M) |
| the window's own pieces of `dos_entry`/`dos_run`/`dos_wake`/`dos_fsx_main` moved OUT | ~-250 (E: the 53 references of 1.3 and the instructions round them) |
| the headless arms: a minimal entry, a one-line paint, the toast, the close | ~+150 (E) |
| the gates (section 4.4) | ~+100 (E) |
| the entries a FENCED slot names, kept below `I_SIZE` (section 4.5) | ~+20 (E) |
| **the floor** | **~25.2 KB (E from M spans)** - 25,200 M before the glue |

**What that buys a program** (E): the arena is `OSAPI_MEM_AVAIL` after the
region, so every byte off the region is a byte on the arena -
KERN-DOS-PLAN 1.1's **449 KB** (Keep all) and **481 KB** (cache dumped)
become **~468 and ~500 KB**. Those two readings were taken with a 42 KB
region; today's is 43.8 KB, so today's arms read ~1.8 KB less than they did
and the headless figures are against today's.

**Against IBM DOS 3.30**, which KERN-DOS-PLAN 1 puts at ~39 KB: the
comparison is not like for like and should not be quoted as one. The headless
host is 25.2 KB of DOS on top of a 97,280-byte kern_big (`KERN_SIZE`, M,
`kernsize` on this tree) plus whatever drivers and caches the machine has;
IBM DOS is the whole of the machine's software. The comparisons that are
honest:

- **the DOS layer itself**: 25.2 KB headless against kern_dos's 31,868-byte
  image (M, `build/kerndos.bin`), which carries its own disk layer - the
  headless host borrows the kernel's;
- **what a program gets**: ~468 KB headless, against KERN-DOS-PLAN's ~603
  for the whole machine and IBM DOS 3.30's ~600 on the same 640 KB. Headless
  is a middle arm - about a seventh of the way from today's bracket to
  kern_dos - that keeps the OS alive and comes back in no time, where kern_dos
  is a hibernate and a resume.

**The floor's own composition says where the next KB is**, if one is ever
wanted: 75% of it is the core's slot, which is kern_dos's ABI (SPEC.md
96.44.5) and is out of scope; of the host's 6.1 KB, the `.LNK` reader's 1.3 KB
is dead from the moment the program starts (section 6.2).

## 4. The two shapes

### 4.1 Why the earlier refusal does not reach either

DOS-PARTS-PLAN section 2's shape B was refused because it GREW the region
at `Run in Window`: `OSAPI_MEM_REGROW` grows in place only when the
paragraphs above are free, the carve is claimed top-down (`MEMP_CLAIM_HI`,
SPEC.md 20.12.10.5) so they almost never are, and the move it falls back to
does not rebase the instance record, the window's owner, the worker's frame
or the IVT. **Both shapes here only ever SHRINK, or never claim**:

- a SHRINK of a claim is in place and always succeeds ("Shrinking always
  succeeds in place, and the tail is free for everyone else immediately",
  `OSAPI_MEM_REGROW`'s own contract), and a package may regrow its own
  region with `mov dx, cs` (SPEC.md 66.6.1.2);
- a claim the loader never asks for needs no API at all.

What neither can do is put the window BACK. That would be the refused grow,
so a headless instance ends with its program, and that is the semantics
(section 5).

### 4.2 (a) Drop after load - SOUND.DRV's shape

SOUND.DRV puts what a machine may never need LAST in its image, writes the
offset where its kept code ends into `DRV_H_KEEP`, and the kernel shrinks the
claim to it (SPEC.md 51.1.3, 34.15.1: 11 KB -> 6 KB with no mixer). The
package form is the same rule with the package doing the shrink:

1. the layout of section 4.4 - the window code and the window bss last;
2. the host decides at its ENTRY, where it already reads the `.LNK`
   (`dos_lnk_open`), before `OSAPI_WM_CREATE`, so no window is ever painted;
3. headless: `OSAPI_MEM_REGROW` with `DX = CS` down to the resident end (KB
   granules, or `OSAPI_MEM_P`'s `MEMP_REGROW` in paragraphs).

**What it costs is one compaction.** The region was claimed top-down, so it
sits at the TOP of the free space, and the arena is claimed top-down too
(`OSAPI_MEM_CLAIM_HI`) - just under the region. The tail the shrink frees is
ABOVE the region, a separate run: `[next claim up][freed tail][region][the
arena's run]`. The bytes reach the program only when the compactor's ceiling
pass moves the region up into the hole. `dos_run` already asks the what-if
and posts that pass when it would add anything (SPEC.md 66.4.3, its
`MEMC_POST` path) - so no new code, but a pass: **~72 ms (E: the ~25 KB
region at `rep movsw`'s 2.86 ms/KB, PERFORMANCE.md Set 117.2 via
REGION-SELF-COMPACT-PLAN 3.4)** and a wake. And the 11.3 KB of the window's
packed stream was read and decoded for nothing first.

| (a) | |
|---|---|
| RAM to a headless program | ~19.2 KB (E from M spans) - IF the pass runs; a region pinned by anything is the same number as no gain at all, silently |
| launch, headless | **+~0.1 s (E)**: the compaction; the window's bytes are read as today |
| launch, windowed | +0 |
| speed while the window is up | +0 |
| disk | +0: the host stays one stream |
| loader | 0 |
| decided by | anything the host knows at entry - its own `.LNK` boxes byte (`LNK_BX_*`, SPEC.md 96.25.2.1), the Setup box |

### 4.3 (b) The window as a NEAR part, cut by the loader

The same layout, split into TWO eager parts of the carve, adjacent in the
file and in the claim: part 0 the resident host (image + bss, as today), part
1 the window (its code at the offset part 0 ends, rounded to a paragraph, and
its bss as zeros under `OP_ZFILL`). Assembled as ONE assembly - the window
is not a separate build with a map, it is a section of the host's own source
placed last - and cut into two files at a label, the way
`tools/dossetchk.py` cuts the Setup part. So:

- **every call stays NEAR and every operand stays as written.** The window
  part lands at `CS:roundup16(part 0's length)`, because the standard lays
  compressed carve parts out at `roundup16(len)` each (`op_seg`'s walk,
  SPEC.md 20.12.7) - and that is the offset it was assembled at. No macros,
  no thunks, no second segment: C' (SPEC.md 96.54.9) needed four families of
  macro because its part had a CS of its own, and this one does not;
- **the loader makes the choice before a byte is read.** For a headless
  launch it marks part 1 `OP_LAZY` in its own table before `op_load`: the
  table is the loader's image, which is per-instance RAM, and `op_size` sizes
  the carve from the eager rows alone. The carve is then part 0's size, the
  re-home's KEEP form trims at part 0's end (SPEC.md 20.12.10.5), and the
  region is born 19 KB smaller. **No compaction**: the region simply sits 19
  KB higher and the run under it is 19 KB longer;
- **a windowed launch reads the same bytes in the same run** - the carve is
  "the run from the FIRST filed part's first granule to the LAST one's last"
  and is read `OP_CHUNK` (32 KB) a take, and part 0 + part 1 pack to ~15.5 KB
  (E, below). So the same `OSAPI_FILE_READ_AT` and the same `int 13h` calls;
  what is new is a second stream start in `op_unpack`, microseconds.

**The part has to be part 1, and the rest renumber.** os88pkg.py appends
parts in table order and the carve run spans every byte between its first
and last eager rows, so a window part placed after the lazy ones (the core,
kern_dos, HELP, network, Setup, the windowed layer: 34 KB packed) would put
all of them in the read. The rows the loader copies into the host's bss are
copied by number from `DOS_PART_*` constants and read by the host by offset,
so renumbering is assembly constants - and `tests/kdpart.py`, which mirrors
`DOS_PART_CORE = 1` and `DOS_PART_KD = 2` in Python.

**The decision has to be readable by the LOADER, and it already is.**
`dsl_w16` (SPEC.md 124.3.1) reads the first cluster of whatever document DOS
was handed, to see whether it is an NE - so for a `.LNK` the first 512+ bytes
are in a claim the loader holds, at no new I/O. The `.LNK` header is 76 fixed
bytes (SPEC.md 96.21), and at `+0x3C` it has **`ShowCommand`**, which
Microsoft's format defines as how the target's window is shown: os8088 writes
1 (`SW_SHOWNORMAL`, `apps/dos/dosset.inc`'s template) and `dos_lnk_parse`
never reads it. **`SW_SHOWMINNOACTIVE` (7) is "no window"**: one compare at a
fixed offset, after the `0x4C` magic the loader can test in the same buffer.
An older DOS.O88 ignores the field and runs the link windowed, which is the
right way for an old host to be wrong; Windows would start the target
minimised. The `ExtraData` boxes byte, where `LNK_BX_WIN` lives (SPEC.md
96.32.2.4), is at a variable offset the loader would have to walk to - so
the private byte is the wrong place for this one bit and the header's own
field is the right one.

| (b) | |
|---|---|
| RAM to a headless program | **~19.2 KB (E from M spans)**, unconditionally - no pass needed and none can fail |
| launch, headless | **-~0.7 s (E)**: part 1 is 11.3 KB packed (E: the split, below), ~22 sectors at 27.2 ms a sector in a coalesced run (1,278 ms for 47, SPEC.md 2.9.13, M on a 5150) = ~0.6 s, plus 14,556 bytes not decoded at 39.9 cycles a byte (the same section) = ~0.12 s. Probably one `int 13h` call fewer of the launch's 14 (DOS-PARTS-PLAN 4.6, M) |
| launch, windowed | **+0 calls (E)** |
| speed while the window is up | **+0** - the same instructions at the same CS; the gates (4.4) are on entry, wake and run, never per character or per tick |
| disk | **+~330 bytes (E)**: the host packed whole is 15,572 bytes on this tool and split is 4,298 + 11,579, **+305** (M on `os88lz`'s optimal parse, which packs the whole host ~2.5% looser than the shipped 15,197); + 8 of row; + at most 15 of pad. Parts are 16-aligned in the file, not sector-aligned (M: part 0 at file offset 2,688, part 1 at 17,888) |
| loader | **+~27 (E)**: the `ShowCommand` test (~14), the row flip (~5), the flag into the host's bss (~6); transient, freed at the re-home |
| resident, windowed | +~100 (E, the gates) + at most 15 of paragraph pad between the parts |
| kernel | **0** - every mechanism is the parts standard and the re-home as they are |

### 4.4 The layout both need, and the gate that keeps it honest

`[header + core slot][R code][R bss, as zeros in the image][W code][W bss]`.
The resident bss moves INSIDE part 0's image as zeros (they cost LZ4 nothing
and `OP_ZFILL` keeps them off the decoder), so that part 0's header still
says `image + bss` and the window's bss is the TAIL. The `HBSS` chain splits
into two: `HBSS` (resident, based at part 0's own bss) and a new `WBSS`
(based past the window code), one macro name per row - the split SPEC.md
96.44.2 already made between `DBSS` and `HBSS`, one level along.

The danger is the one SPEC.md 96.32.2.3 met: nasm resolves a near reference
across the boundary silently, and in a headless region the tail is FREE
memory - a stray `call con_write` would run whatever claim landed there, and
a stray `mov [con_cx], ax` would write into it. So every reference from
part 0 into part 1 is spelled inside a `WIN_ONLY` ... `WIN_END` bracket whose
opening is the test itself (`cmp byte [dos_head], 0` / `jne`, 7 bytes,
~16 cycles), and a build-time checker in `tools/dossetchk.py`'s shape
disassembles part 0 and FAILS the build on any branch, memory operand or
immediate naming a part-1 address outside a bracket. **Measured shape of the
work**: 81 such references in 25 routines today (section 1.3), 53 of them in
the four routines that get headless arms - so ~15 brackets, ~100 bytes (E).
The identity gate C' had is not available (the layout move changes every
address), so the checker IS the gate; break it on purpose (take one bracket
out) and it must go red.

### 4.5 The kernel's one fence

`inst_pkg_fence` (kernel/instance.inc) refuses an `OSAPI_FSX_RUN` proc, an
`FSXF_RATE` hook and an `OSAPI_TASK_SPAWN` entry above `I_SIZE`, and
`I_SIZE` is part 0's `image + bss` (SPEC.md 20.12.10.4) - not the region's
extent, which includes the carve's other parts. So the few entries those
three slots name must stay in part 0 even though only a window uses them:
`dos_fsx_main` (resident anyway), the windowed layer's worker entry and its
full-screen proc's stub (`hent_dvw_fsmain`). ~20 bytes (E), counted in the
floor. Window callbacks (`W_PAINT`, `OSAPI_WM_ONWAKE`, `OSAPI_WM_ONCLOSE`,
the menu) are not fenced by `I_SIZE` and may live in part 1.

### 4.6 What (b) is NOT

- **Not a far part.** Nothing in it is entered by a far call; C' (SPEC.md
  96.54.9) stays exactly as it is, a separate 5 KB claim for a windowed run.
  Its 860 resident bytes move into part 1 - a headless instance never runs a
  program in a window.
- **Not a re-read.** The window is read with the host or not at all; there
  is no `op_self` of it and no claim of its own.
- **Not on kern_mega or kern_small.** kern_mega's host is above 1 MB, where
  the bytes cost no conventional memory and every launch is the whole machine
  (DOS-PARTS-PLAN 6, item 2); kern_small's disks carry no `DOS.O88` (SPEC.md
  24.5) and its plain `dos.o88` has no loader to cut anything. Both keep the
  one-part layout from the same source, the way the network and Setup parts
  are inline there.

### 4.7 (a) on (b)'s layout - the second door

With part 1 in the carve, a WINDOWED instance can still go headless for one
run: the user ticks the box and presses `Run`, the host shrinks its region to
part 0's end (4.2's mechanism), closes its window, and runs. The bytes reach
the arena through 4.2's compaction (~72 ms, E). Worth having only if users
open a DOS window first and decide afterwards; wave 4, optional.

## 5. UX

**How a user asks.** Per program, in the shortcut, because a shortcut is
the one object that is per program (SPEC.md 96.21). A check box on the Setup
page beside `Run in a window` (SPEC.md 96.32.2.4): **`Close when done`** -
ticked, the shortcut is saved with `ShowCommand` = 7 and the program runs
with no DOS window. The two boxes are exclusive (a run in a window IS a
window): ticking one clears and greys the other. `Close when done` greys for
an NE (the Setup part knows the target; a Windows program is WIN16's,
SPEC.md 124.3.1, and a headless DOS would only run its MZ stub).

**Not automatically.** A double-clicked `.COM` or `.EXE` with no shortcut
stays windowed. The host cannot know at load time whether the program is a
game that owns the screen or a utility whose whole output is its last screen,
and the second is the common case for a bare `.COM`; making its answer vanish
is a regression for every non-game program on the disk. The shortcut is where
the user says *this one draws its own screen and I do not need what it
leaves*.

**What the user sees.** A small window (the template's frame and one line,
`Starting PRINCE.EXE`) while the program is read - that is seconds on a
floppy, and a window is what `OSAPI_FSX_RUN` and `OSAPI_WM_WAKE` both need
(SPEC.md 53.1: the bracket is a WINDOW's) - then the bracket, then the
desktop, with a toast if the exit code was not 0 or the run was refused.
The instance is gone: no dock tile, no window, no region.

**Exit codes and errors**: section 2 - the toast, and nothing else.

**Alt+Enter**: unchanged in the bracket - it is the program's key, as it is
today in a `Run` (SPEC.md 96.54.1 moves only a program started IN a window).
A headless program cannot be moved into a window, there being no layer.

**The whole machine** (SPEC.md 96.40): headless and arm 3 combine - kern_dos
is headless by nature - and the return is a toast and a close instead of
the exit line. The confirmation (SPEC.md 96.42) is a W dialog, so a headless
shortcut saved with arm 3 IS the confirmation: the user ticked both boxes and
saved. The record (K, 1,069 bytes) stays in the floor.

**Several DOS windows.** Each instance is its own region, so headless saves
per instance - but only the RUNNING program's arena is the question, and a
windowed DOS left open beside it still costs that program its whole 44.9 KB.
Headless does nothing about that and should not pretend to: closing the
other DOS window is worth more than headless is. Two headless launches cannot
overlap - the bracket is exclusive - so the second starts after the first has
closed.

## 6. The console, and what can go with it

**The console IS the window.** The DOS window's face is the console (SPEC.md
96.33) - the prompt types into it, a run's last screen lands in it, the exit
line is a line of it, and Full Screen is the same buffer on text VRAM. There
is no useful DOS window without it, so there is no third state between
"windowed" and "headless" to build: everything in W goes together, and W is
mostly console (11.5 KB of 19.4, M).

### 6.1 What rides with it into part 1

All of W, which is every item the owner listed: the console (`os88con`,
`con_glyf`, its 4.8 KB of bss), the prompt and its verbs (`dosc.inc`), the
controls (`os88ui.inc`), the line discipline (`os88line.inc`), the band paint,
the menus, the path box, the full-screen console, `dos_snap`, HELP's and
Setup's glue (their text and code are already parts, SPEC.md 96.32.2.3 and
DOS-PARTS-PLAN 1.1), and the windowed layer's resident half.

### 6.2 What does NOT, and why

| | bytes (M) | |
|---|---:|---|
| the shell, `dosh.inc` | 4,339, in the core | **`COMMAND.COM` is not a file** (SPEC.md 96.30): a program that shells out reaches it through the core's `AH=4Bh`, headless or not, and kern_dos's COMMAND door is the same code. It is the core and stays |
| the error strings | 544 | the toast needs them - and a refusal must not need a disk |
| the `.LNK` reader | 1,285 | needed at launch, in the host, before the window question is asked. **The lever after this one**: `dsl_w16` already holds the link's first cluster, so the LOADER could parse it and hand the host its results through the bss block it already writes (`DOS_B_DOC`'s shape). That takes 1,285 bytes out of EVERY instance, windowed too, for ~+800 bytes of a loader that ships raw (E: the reader plus the hand-on) - one or two more sectors of every DOS launch. A study of its own; not proposed here |
| the KDH record and the handoff | 1,069 | `dos_wake` reads the record on every wake (SPEC.md 96.41.4) |
| the core's slot | 18,951 | kern_dos's ABI (SPEC.md 96.44.5), out of scope |

## 7. Waves

**Build status** (updated as each wave actually works, branch `dos-headless`):

- [x] **W0, the checker** - `tools/doswtail.py`, the `WIN_CODE`/`WIN_ONLY`
  macros and `WBSS` in `apps/dos/dos.asm`, the classification marked in the
  source, and `make` running the checker on a `-DDOS_WTAIL` assembly of the
  host. The shipped `dosp.bin` is byte-identical. Taking any bracket's test
  out turns `make` red (`soak -k doswtail` does it to the first and a middle
  one). The checker does not disassemble: it assembles the host twice with
  the tail at two addresses and refuses every resident byte that moved,
  which counts data references the plan's `ndisasm` survey could not. On
  that survey's 81 references: 19 brackets cover them, 0 unbracketed.
  Recorded in SPEC.md 96.44.19. The red row is SOAK and not fast, being
  about one package (docs/WRITING-TESTS.md 2.1); the every-build half is
  `make`'s own run of the checker.
- [x] **W1, the layout** - the parted host on kern_big/kern_emu (`DVW_SPLIT`)
  now ships as `[header + core slot][resident code][resident bss][window code]
  [window bss]`, still one part. The resident host is **25,444 bytes** (image
  23,960 + bss 1,484, so `I_SIZE`) and the tail **19,611**: `dosp.bin` 44,900
  -> 45,067 (+167, M). 29 of `dos.asm`'s window rows moved to `WBSS` and the
  console's 4.8 KB with them. Kept resident for the fence (4.5): the worker
  loop and `dvw_stubs` (so `hent_dvw_fsmain`), and `dos_fsx_cone`, a
  bracketed jump to Full Screen's `dos_fsx_con`, which the slot is handed
  instead. `os88pkg.py` now refuses a program part SHORTER than its image +
  bss rather than any other length. kern_mega, kern_small, `build/dos.bin` and
  the core are byte-identical (`cmp`ed).
- [x] **W2, the split** - the window is part 1 (eager, `OP_COMP|OP_ZFILL`),
  the core, kern_dos, HELP, network, Setup and the windowed layer renumber to
  2-7 (kern_mega keeps its five), `tests/kdpart.py`'s mirrors updated.
  **The owner's condition holds** (M, 5150 CGA, 360KB LZB system disk,
  `DOSHELLO.COM` double-clicked on a fresh boot, five runs each): **14 `int
  13h` calls to the window and 18 to the program's READY, before and after**,
  113 sectors against 112, READY at 11.90-12.04 guest s against 11.89-11.96.
  Calls stay near. `DOS.O88` 52,667 -> 53,212 (lz4) and 46,662 -> 47,174 (LZB,
  46 -> 47 clusters): +545/+512, against the plan's +~330 (E). The second
  ZFILL row exposed a defect in `op_unpack` - the stream head re-read after
  stream 1 had overwritten it - fixed in the standard (SPEC.md 96.44.19).
- [x] **W3, headless** (SPEC.md 96.44.20) - the loader's `ShowCommand` test
  and the row flip, `[dos_head]`, the headless arms of entry, wake and run
  (`dos_htpl`, `dos_fld_def`, `dos_hpaint`, `dos_hend`), the toast and the
  close; `Close when done` on the Setup page (exclusive with `Run in a
  window`, greyed for an NE through `dos_w16_isne`) and ShowCommand 7 in the
  `.LNK` writer; `dos_lnk_parse` reads it back. **M** on `doshead`: claim
  **25,984 against 45,680**, arena **+20 KB** (487 against 467), READY
  **13.5 s against 15.9 (-2.4 s)**, **20 `int 13h` calls against 21**; red
  with the loader's flip removed (claim 45,680, arena +0). The plan's E was
  -0.7 s: the window's read and decode are ~1.8 s of it (the flip-removed
  run), the rest is the console and paint a headless host never does. The
  host's own arms are **+535 resident bytes** against the plan's ~+250 (E).
- [x] **Size pass** (SPEC.md 96.44.20's last paragraph): the resident host
  25,982 -> **25,216** (-766) and the tail 19,688 -> 20,247, so a windowed
  region is 207 bytes smaller and a headless one 766. 544 of it is the X
  bucket (section 1.2) moving into the tail - section 2 left it resident for
  a reuse W3 did not build, its toast having short forms of its own - and
  222 the headless arms' own (the arms now cost **+316** against W2's
  25,444 with X counted where it was, where W3 measured +538). Every
  bracket's compare is one `dos_hq`. Loader 3,101 -> 3,087; `DOS.O88`
  53,803 -> 53,675 (lz4) and 47,701 -> 47,604 (LZB, 47 clusters). The
  windowed launch reads fewer packed bytes than before (part 0 + part 1,
  16,188 -> 16,071 lz4) in the same one run, and pays ~15 near calls to
  `dos_hq` on its once-a-launch paths. kern_mega, `kern_dos` and the plain
  `build/dos.bin` are byte-identical (`cmp`ed).
- [ ] **W4** (drop after load on the same layout) - not built: it needs a
  path from a live window to the headless arms plus a posted compaction to
  hand the freed tail to the arena, which is not cheap on this layout.

Every wave is kern_big and kern_emu; kern_small and kern_mega assemble the
one-part layout from the same source and must stay byte-identical to their
shape before (their `dos.o88`/`DOS.O88` `cmp`ed in a private tree).

| wave | what | gate rows (the deduplicated union runs ONCE, after the last wave) |
|---|---|---|
| **0** | the checker: a `dos_wtail` boundary label, `WIN_ONLY`/`WIN_END` with the test built in, and a `tools/` checker run on part 0 at every build that fails on an unbracketed reference into the tail. Written against today's single part with the boundary at the end, so it reports today's 81 | host only; broken on purpose (one bracket removed) it goes red. A fast-tier unit row beside `t_mirror` |
| **1** | the LAYOUT: the window's code and bss last, resident bss as zeros inside part 0's image, `HBSS`/`WBSS`, the 14 `dvw_*` cells and the fenced entries kept resident, the four routines' window arms moved behind brackets. Still ONE part | every DOS row the layer touches: `dosopen`, `dosexe`, `doscom`, `doslnk`, `dosargs`, `dosmedia`, `dospkg`, `dosext`, `dosconcga`, `dosaltenter`, `doswlive`, `doswpart`, `doswfs`, `doswcell`, `dosram`, `dosmem`, `dosarena`, `kerndos`, `kdos`, `dosbss`; RAM per instance M (`dosarena`'s probe) within the pad of 44,898 |
| **2** | the SPLIT: part 1 eager, adjacent, `OP_COMP \| OP_ZFILL`; `DOS_PART_*` renumbered; `tests/kdpart.py`'s mirrors updated. Nothing is headless yet | wave 1's rows, `kdpart`, `rehome`, `rehomemove360`, `win16nedos` (the loader's NE arm reads the same table); **the measurement**: `m.disk()` calls over a double-clicked `.EXE` launch equal to today's 14 (DOS-PARTS-PLAN 4.6, M), and the region claim equal to wave 1's |
| **3** | HEADLESS: the loader's `ShowCommand` test, the row flip and `[dos_head]`; the headless arms of entry, wake and run; the minimal paint; the toast; the close; `Close when done` on the Setup page and in the `.LNK` writer | a new soak row, `doshead` (MartyPC, `os8088_5150_cga_gla`, 360 KB): a link saved with the box launches, the carve claim equals part 0's length (no tail), the arena is part 1's length larger than the same link windowed, a non-zero exit toasts and the instance is gone; red with the loader's flip removed. **The measurement**: the same link windowed and headless, frame-stepped from the double click to the program's first INT 21h - section 4.3's -0.7 s |
| 4 | optional: 4.7, a windowed instance going headless for one run | `doshead`'s second leg; `heapcheck`-style read of the region moving up |
| - | not here: the loader parsing the `.LNK` (6.2) | its own study |

## 8. Risks

- **The tail is freed memory in a headless region.** A missed reference is
  not a crash in the window but a WRITE INTO SOMEBODY ELSE'S CLAIM in the
  headless run, on the one path no windowed row exercises. The checker (wave
  0) is the whole defence, and it must refuse immediates and memory operands
  as well as branches, because `mov [con_cx], ax` is the dangerous one.
- **The fence (4.5)** is silent the other way: a window-only entry left in
  part 1 that a fenced slot names is REFUSED at run time - `Run in Window`
  falling back to the bracket for want of a worker, which is a fallback that
  exists today (SPEC.md 96.54.9) and so would not look like a bug.
- **Renumbering the parts** touches the loader, the host's row offsets, the
  handoff's rows (by copy, so not their numbers) and one Python mirror.
- **(a)'s gain is conditional** on a compaction that can move the region;
  (b)'s is not, which is a reason for (b) rather than a risk of it.
- **A headless refusal says less.** 24 characters on the menu bar against a
  sentence in a window. That is the trade the owner named.

## 9. What was measured, and how

- Spans: `build/dosp.bin` and `build/dosp.map` as `make` wrote them on this
  tree (the map is `-DDOSP_MAP`'s, so it describes the shipped bytes). A
  throwaway script read the map's `.text` symbols - code and bss alike, the
  bss being `equ`s past `os88_image_end` - took each global's span to the
  next, and charged it to the file whose source defines the label.
- Buckets: by hand, routine by routine, from each routine's callers and the
  hooks the core binds (`dos_hk_bind`).
- References: `ndisasm -b 16` over part 0's code; every hex operand of an
  instruction inside an R/N/L/K/X span that falls inside a W span, split by
  whether the instruction is a branch, has a memory operand, or neither.
- Packed sizes: `tools/os88lz.py`'s `lz4_compress` (optimal parse) over the
  header plus the bucket's code bytes in address order, against the shipped
  part 0's `zkb` (15,197) read out of `build/kdos/DOS.O88`'s part table.
- Not measured, and owed by the waves: every launch time (MartyPC), the
  compaction's cost for (a), and every byte of new code.
