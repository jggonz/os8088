# DOS-PARTS-PLAN - what in DOS.O88 a program that does not use it should not pay for

**Status: a design study. Section 4 is BUILT (the flash-free variant, 4.6);
section 1.1's shared part reader, with HELP as its first consumer, is built;
section 3's NETWORK PART is BUILT (SPEC.md 96.23.11, measured below in 6) and
the Memory page's `Network` box starts cleared (SPEC.md 96.36.7.4); the
SETUP PAGE is a part too (SPEC.md 96.32.2.3, the owner's decision against
section 3's "no, unless D" - measured in 6), with a `Run in a window` box on
it (96.32.2.4); the windowed layer (2) is not.** Written on branch
`dos-windowed` at 2026-11-01. Every figure is marked **M** (measured on this
tree, method below) or **E** (estimated, with what it was estimated from). An
**E** is a promise to measure, not a fact.

The owner's question: *what would make sense to split into parts, so that a
DOS program not using a component does not need it loaded? Can most of the
windowed work go into a part of its own, loaded only when the user asks for
windowed mode? Are there other components this would make sense for?* - and
a follow-up: *a Windows program should be recognised by DOS's initial loader
and handed off before any window or the 50 KB host is loaded* (section 4).

## 0. The instrument, and the two quantities

Spans come from a NASM symbol map, never from a padded artefact: a wrapper
`[map all ...]` + `%include "dos.asm"` assembled into `/tmp` with the
Makefile's own flags (`-DDOSKPART -DDOS_EXTCORE -DDOS_WIN16`, Makefile:7488)
gives a `dosp.bin` **byte-identical** to `build/dosp.bin` (M), and each label's
span (to the next label) is charged to the file that defines it. The same for
`doscore.asm` (byte-identical to `build/doscore.bin`, M) and `dosload.asm`
(byte-identical, M). Packed sizes are `tools/os88lz.py`'s `lz4_compress` over
the same bytes, the codec the parts ship in. `DVW` was A/B'd by commenting
dos.asm:108's `%define DVW` in a copy under `/tmp`: image 48,668 -> 44,048,
bss 7,284 -> 6,631 (M) - **4,620 image + 653 bss**, as the brief said.

Two quantities, and they do not trade:

- **RAM per DOS instance** - the host part's image + bss, 55,952 bytes (M),
  sitting in the parts carve for as long as the window is open. During a
  `Run` (the bracket, SPEC.md 96.3) every byte of it is a byte the program's
  arena does not get, because that arena is `OSAPI_MEM_AVAIL`; during a
  windowed run the arena is capped at 128 KB (SPEC.md 96.54.2), so there it is
  a byte off the NEXT DOS window.
- **Disk** - DOS.O88 is 52,302 bytes (M). Moving code into a part saves no
  disk; it costs the row (8 bytes, `OP_ROW`, apps/os88parts.inc:124) and the
  compression context a separate stream loses.

## 1. What the mechanism actually allows a package at run time

Read against SPEC.md 20.12.4 and apps/os88partsbody.inc:

- A lazy part is fetched by `op_fetch` into **its own claim, its own
  segment** (one of `MEM_OWNER_MAX` = 8), and dropped by `op_drop`. There is
  no verb that loads a part *into* an existing segment; DOS's core reaches its
  slot only because the loader `op_fetch`es it and `rep movsw`s it into a hole
  the host reserves (apps/dos/dosload.asm `dsl_core`; dos.asm:143).
- **The host carries no fetch code.** `-DDOSKPART` emits the 18-byte part
  table and not the standard's body (Makefile:7484-7486); `op_load`/`op_fetch`
  live in the LOADER, whose region is freed at the re-home (SPEC.md 20.12.10).
  The loader's fetch surface is `op_fetch` 359 + `op_drop` 74 + `op_unpack` 180
  + helpers (M, loader map) - so giving the host `op_fetch` would cost it
  **~750 resident bytes** (E, sum of those spans). The cheap form is
  `OP_SELF` (SPEC.md 20.12.9.1, Clear Skies' precedent): the host reads the
  row's bytes off its own file with `OSAPI_FILE_READ_AT` and expands them with
  `OSAPI_DECOMP` in place (no margin is needed, SPEC.md 20.13.7). It already
  knows its own file (`dos_pkgwhere`, dos.asm:7637) and already holds rows the
  loader copied into its bss (`DSLH_*`, dosload.asm). **~100 resident bytes,
  shared by every lazy part below** (E). Call it `dos_pfetch`. *(Measured in
  1.1: 191 bytes, built as the shared `op_self`, plus ~50 of DOS's own.)*
- **Kernel bill: zero.** Every mechanism above is package code; SPEC.md
  20.12.4 measured `kernsize` byte-identical across all of them.

### 1.1 The shared part reader, measured

**Built far enough to measure, on this branch, as evidence; not merged.**
The owner's ask was *"cost the small shared part reader, then we will decide
how to part out."* Every figure below is **M** (built and measured on this
tree with `kern_big`'s shipped flags, spans out of a `[map all]` of an
assembly byte-identical to `build/dosp.bin`) or **E** (estimated, with what
it was estimated from).

**What was built.** `op_self`, in a new `apps/os88pself.inc`: ONE `OP_LAZY |
OP_COMP` row, read off the package's own file and expanded into a claim of
its own. The row, the file name and the stream format are ARGUMENTS
(`DS:SI`, `DS:DI`, `AL`). It touches no `op_*` bss word, and there are two
ways in: `%define OP_WANT_SELF` before `os88parts.inc` emits it from the
standard's body, for a package with a table; a carrier with no table (DOS's
re-homed host) `%include`s the file. The arithmetic is `op_fetch`'s
(SPEC.md 20.12.7.4) with ONE simplification, which is what makes it small:
everything lives in ONE segment. The read lands `R = roundup512(len -
packed)` up the claim, and the stream is the head slack past that and
expands down onto offset 0. So `ES:BX`, `DS:SI` and `ES:DI` are all the
claim plus an offset: there are no paragraph sums and no `op_at` windows,
and so nothing to do on kern_mega, where a selector plus an offset is still
an address. The price is a ceiling: `R` plus the read must fit 64KB, and a
row that does not is refused.

What it checks on hostile disk data (the row came off the disk, inside our
own image):

- a cluster of 1-64 sectors, from `OSAPI_FILE_DFREE` asked on every call,
  because kern_small's `READ_AT` still takes whole clusters (SPEC.md
  18.4.4.2);
- `len > packed`. `os88pkg.py` never writes anything else, so a row that
  says otherwise is refused rather than left to wrap `R`;
- every sum, carry-checked;
- the read must DELIVER the slack plus the whole stream, so a truncated file
  refuses;
- `OSAPI_DECOMP` is told the exact output and the exact input, so it
  refuses rather than writing past either.

It frees the claim on every refusal and leaves nothing held.

| | bytes |
|---|---:|
| `op_self`, the reader (span from `op_self` to the next label) | **191** (M) |
| ...in a DOS-PRIVATE form (row, name and format hard-coded) | ~180 (E: the pushes, the frame and two `[bp+]` reloads it would not need) |
| ...in a package that does NOT opt in | **0** (M: `midirack.bin` assembles byte-identical against the new body; the fast tier's every-includer rows are green) |
| DOS's wrapper: stand in DOS.O88's folder (`dos_pkgdir`/`dos_pkgvol`, which `dos_pkgwhere` already banks), call, stand back | ~50 of `dos_con_help`'s 92 (M span: 33 bytes before the call, 17 after) |

**Recommendation on placement: the shared library, as built.** A private
copy would save ~11 bytes in DOS, and would cost every other package the
chance to take the same 191. The tree already carries FOUR hand-written
`OP_SELF` readers, each with its own arithmetic: MIDIRack's `mra_load`,
Excitebike's `xb_partget`, RealDMG's `rw_aread`, and Clear Skies' stream
reads. None of the four is converted here, and converting them would save no
RAM. `mra_load` lives in an image tail that is laid over after entry and has
80 bytes spare (M: the tail assertion fails with `op_self` in it).
`xb_partget` and `rw_aread` read parts larger than one segment holds, through
the standard's windows. So the library form is for this package and for the
NEXT one.

**The first consumer: HELP's text.** `dos_h_text` (798 bytes with its NUL)
moved out of the host into `apps/dos/doshelp.asm`, together with its 16-line
assertion. That one file is two things:

- assembled on its own, it is part 3 of DOS.O88 (part 4 on kern_mega), a
  lazy `OP_ASSET | OP_COMP | OP_LAZY | OP_SELF` row;
- `%include`d, it is the inline text of the one build that has no loader
  (`dos.bin`, built without `-DDOSKPART`).

The loader copies the row into the host's bss, the same way it copies the
core's. The new row is `DSLH_HLPROW`, last of the block, so no existing
offset moves. The loader also writes the stream's FORMAT into the row's kind
byte, which is always `OP_ASSET`, so the format costs the host no byte of its
own.

When HELP is typed, `dos_con_help`:

1. stands in DOS.O88's folder;
2. calls `op_self`;
3. stands back where the prompt was;
4. prints the claim with `con_write`, bounded by the row's length rather than
   by a NUL, because the bytes came off a disk;
5. frees the claim.

A refusal prints `Cannot open HELP`, out of two strings the file already had.

HELP is window-side and NOT in the core (SPEC.md 96.33.23), and `dosc.inc`
is not in `kern_dos` at all (`%ifndef KD_BACKEND`). So the core slot that the
hosts share with kern_dos is untouched, and `doscore.bin` is byte-identical
(M).

| | before (6a2b777c) | after | delta |
|---|---:|---:|---:|
| host image (`dosp.bin`, part 0) | 48,047 | 47,520 | **-527** (M) |
| host bss | 7,301 | 7,309 | **+8** (M: the row) |
| **RAM per DOS instance** | 55,348 | 54,829 | **-519** (M) |
| ...of which the reader | | | +191 (M) |
| ...of which HELP's own glue (`dos_con_help` 12 -> 92 bytes) | | | +80 (M) |
| HELP's marginal saving once the reader is paid for | | | **-710** (M: -798 of text, +80 of glue, +8 of row) |
| loader (`dosload.bin`, transient, freed at the re-home) | 2,779 + 133 bss | 2,809 + 133 | **+30** (M) |
| `DOS.O88` on disk | 50,577 | 50,781 | **+204** (M: part 3 is 605 bytes packed, in a sector of its own) |
| `doscore.bin` | 15,388 | 15,388 | byte-identical (M) |

**What a HELP costs now** (M). Measured on MartyPC's `os8088_5150_cga_gla`,
with the 360KB system disk, DOS.O88 in `A:\APPS` and the prompt on `A:\`; a
breakpoint trace on `dos_con_help`'s labels, and `m.disk()` around it:

| step | cycles | time on a 4.77 MHz 8088 |
|---|---:|---:|
| the fetch: stand there, `op_self`'s claim, read and decode | 4,699,626 | **985 ms** |
| standing back | 2,159 | 0.5 ms |
| printing the sixteen lines (the same `con_write` per byte as before) | 2,104,731 | **441 ms** |

So HELP went from ~0.44 s to ~1.43 s: **two `int 13h` calls, 9 sectors, and
two seeks over 68 cylinders, 544 ms of it seek.** SPEC.md 96.33.23.2 expected
exactly this - *"an `int 13h` EVERY time and that is fine: HELP is typed once
by somebody who does not know what to type"* - and now it is priced: one
second on a floppy XT. That is the number that decides where else the reader
is acceptable. It is not acceptable on a HOT path: a part that a Run needs
should stay fetched once read, which is the network part's shape below and
not HELP's.

**Checked**:

- HELP prints all sixteen lines and the prompt in the window (M: the trace
  above, read out of `con_scr`).
- HELP works **under the bracket** (M: Alt+Enter, `[fsx_cur]` = 0, HELP
  typed, the sixteen lines read out of real text VRAM at B800).
- `kern_dos` has no HELP: `dosc.inc` is `%ifndef KD_BACKEND`.
- kern_small's and kern_mega's DOS.O88 assemble and pack (`make
  BUILD=<private tree> KERN_SMALL=1`, and the same with `KERN_MEGA=1`, of
  `kdos/DOS.O88`; the help part is 605 bytes packed on both). Neither was
  booted.
- `make`, with the fast tier: 68 passed, 0 failed.
  `tests/unit/t_stkapps.py` needed the reader's teardown written as `add sp`
  rather than `mov sp, bp`, which its walker does not model.

#### 1.1.1 The readers that already exist, and why neither is this one

*Does a part reader not already exist for reading out kern_dos?* Two exist,
and the running host can call neither.

- **HIBER.DRV's handoff path** (`kernel/hiber.inc`'s stub and `kds_expand`)
  is the one that reads kern_dos and, on the whole-machine arm, the core. It
  is ONE-WAY. The host writes the KDH record (`dos_handoff`: volume, folder,
  name, and each part's first sector and two lengths). The kernel walks the
  file into raw EXTENTS while the file layer is still alive. The stub then
  reads those extents with `int 13h` and expands them with an UNBOUNDED
  decoder, after the OS is gone. That is legitimate there, for the reason
  SPEC.md 2.9.13 allows the kernel's own unbounded decoder, and it is not
  allowed for disk data a package reads under a running OS. It is not a
  package entry point and cannot become one. Nor can `op_self` replace it:
  by the time it runs there is no file layer, no heap and no `OSAPI_*` cell
  to call.
- **The loader's `op_fetch`** reads the core on an ordinary launch, and it
  is FREED at the re-home (SPEC.md 20.12.10), so the host cannot call it.
  Giving the host its own copy is the ~750 resident bytes of section 1.

So the reader is still needed. It CAN, however, replace the loader's fetch.
Built as a scratch variant: `dsl_core` calls `op_self`, and the core and
kern_dos rows are marked `OP_SELF`, so `OP_HAS_FETCH` goes false and
`op_fetch`, `op_drop`, `op_lazyp`, the shadow and their strings leave the
image. The loader goes **2,809 -> 2,430 bytes** on kern_big and **2,577 ->
2,196** on kern_mega (M, both assembled; on kern_mega the low kern_dos row
has to be marked too).

What that buys is SMALL. The loader is transient, and on kern_big `DOS.O88`
stays **50,781 bytes** (M), because the compressed loader already starts the
parts at sector 5: no sector saved and no launch time. It is ~380 bytes of
headroom in the one image section 4.6 already had to grow. So it is worth
doing with the next loader change, and not on its own. It is NOT in this
branch's loader: the variant was not booted, and it changes the launch path
that every DOS row runs.

#### 1.1.2 The next consumers, priced against the measured reader

The reader's share is now FIXED: 191 bytes, plus ~50 of DOS's
stand-there-and-back, paid ONCE by whichever consumer arrives first. HELP,
as built, already pays it. Each later consumer adds only its own glue.

| consumer | saves per instance (sections 2 and 3) | adds beyond the reader | net | time |
|---|---:|---|---:|---|
| HELP (built) | 798 | 80 of glue + 8 of row (M) | **-519** (M); -710 once the reader is paid | +0.98 s per HELP (M) |
| network part (`dosnet.inc` + the packet driver) | ~4.3 KB (E) | ~150-250 (E): far doors at the ~9 host-to-part entry points (~6 bytes each); the vector and `dos_pkt_hook08` re-aimed by `dos_pkt_reloc` (~30); the row copy (+12 loader, +8 bss); and an `op_self` variant that expands INTO the buffers claim `dos_pkt_bufs` already makes (~15 bytes for an output-offset argument), so the code does not spend a second of the 8 owner claims | **~-4.0 KB** (E) on an instance with no wire; ~0 to +0.5 KB with one | ~1 s once per wired instance (E: HELP's two calls and seeks, plus ~6 coalesced sectors for ~3.5 KB packed) |
| windowed layer, option C' | ~5.0 KB (E, section 2) | C''s own ~+285 bytes of resident thunks and ~+310 of part (E, section 2). The reader adds nothing beyond the shared 191 | **~-4.7 KB** (E) on an instance never windowed | ~1 s at the first Run in Window (E) |

**Code run from the claim.** Both of those consumers, unlike HELP, EXECUTE
what was read. `op_self` already gives the shape that needs: the part is
assembled `org 0`, lands at the claim's offset 0, and is entered far at
`claim:entry` with CS = DS = the claim, so its near offsets are right as
assembled. Three things are the consumer's to solve, not the reader's:

- the claim must be PINNED while a vector or the IRQ0 hook points into it
  (`op_movable`'s shape, between runs);
- kern_mega's claims are DATA selectors, so code there needs an executable
  alias, which the reader does not make. The windowed layer is kern_big only
  and never meets this. The network part does: its kern_mega arm either
  stays resident or asks `OSAPI_SEG` for an alias (KERN-MEGA-PLAN's door);
- `op_self` does not shrink its claim, which stays `R` plus the read. A part
  that stays fetched wants a `MEMP_REGROW` like `op_fetch`'s `.keep`, ~14
  bytes (E).

**Recommendation.** Keep the reader in the shared library (`OP_WANT_SELF`,
`os88pself.inc`) and adopt it with HELP as built: **-519 bytes of every DOS
instance, +204 bytes of disk, one second per HELP on a 5150, and no kernel
byte.** The reader's real customer is the network part, at ~4.0 KB net, and
that part is now priced against a measured 191 bytes rather than an
estimated ~100. The windowed layer stays section 2's risk decision; the
reader does not move it. Fold the loader's `dsl_core` onto `op_self` at the
next loader change.

**Rows a real adoption must run.** None was run here except the one HELP
measurement.

- HELP: `dosopen` (it lists the verbs), `dosconcga` (the sixteen lines fit
  CGA's band), `dosaltenter` (the bracket).
- DOS's launch through the loader and the row hand-on: `dosexe`, `doscom`,
  `doslnk`, `dosargs`, `dosmedia`, `dospkg`, `dosext`, `doswlive`, and
  `win16nedos` (`DSLH_DOC` has not moved; the new row comes after it).
- The rows that read the loader's block: `kerndos`, `kdos`, `dosbss`.
- kern_mega: `megadosp`, `megadosroom`. kern_small: `small128`'s DOS leg.
- For the loader variant, all of those; and `dospkt` and `doscable` once the
  network part follows.

## 2. The windowed layer as a part - four shapes

What it is (M, host map): `dosw.inc` 3,987 + `doswm.inc` 382 + 251 of `%ifdef
DVW` arms in dos.asm = 4,620 image; 653 bss (`dvw_xblk`/`dvw_xdsk` 242 each,
`dvw_sums` 50, `dvw_kq` 32, flags). Its coupling (M, grep over the two files):

| | count |
|---|---:|
| `[cs:...]` operands | 119 (111 name `dvw_`/`dwm_` cells, 8 host) |
| plain DS-relative reads of HOST cells | ~85 (`dos_win` 13, `con_cx`/`con_cy` 18, `dos_hkv` 6, ...) |
| near calls OUT to host routines | ~45 sites, ~30 routines (`con_*`, `dos_snap`, `dos_be_bind`, `dos_build_psp`, `dn_shut`, `dos_pkt_shut`, ...) |
| ways IN | `dvw_start`, `dvw_wake`, `dvw_kput`, `dvw_onclose` (a WM close callback, dos.asm:1012), `dvw_fin` (the core's DHK_SNAP hook), `dvw_betab` (158 bytes: a `DBE_NENT`-word table and 22 five-byte posting stubs, which the CORE near-calls through `dos_bevec`, dosw.inc:495-524), the worker entry, IVT 08h/10h/13h/16h (dosw.inc:755-761) and INT 33h |

**A. A slot reserved in the host.** Costs the RAM whether or not it is used.
Zero saving unless something else shares it - which is D.

**B. Grow the instance's own region at Run in Window, load the part past
the bss, same CS.** This is the shape that would need no rewrite at all - near
calls and `[cs:]` stay valid - and it does not work:
`OSAPI_MEM_REGROW` (os88api.inc:3496) grows in place only when the paragraphs
ABOVE are free, and the carve is claimed **top-down** (`MEMP_CLAIM_HI`,
os88partsbody.inc:690; SPEC.md 20.12.10.8), so what is above it is the
ceiling or another top-down claim - almost never free. Otherwise it takes path
3 and MOVES the block (SPEC.md 50.3.1) from inside the region's own callback,
and `mem_regrow` does not rebase or notify (SPEC.md:118303): the instance
record, the window's owner segment, the worker's frame and the IVT the host
hooked all keep the old segment. There is no in-place-only verb, and making
one safe is the posted-compaction machinery of SPEC.md 66.4.3 aimed at a
regrow - a kernel feature. **Refused**: zero-kernel cannot reach it.

**C. A lazy part in its own claim, entered by far calls.** The only shape that
saves RAM with no kernel byte. What has to change:
- the part's own state moves into the part (its `[cs:]` operands then stay
  right as written - that is 111 of the 119);
- every `push cs / pop ds` that means "the host's DS" (~12 sites in the ISRs:
  `dvw_irq0`, `dvw_int10`, `dvw_int16`, INT 33h) becomes
  `mov ds, [cs:dvw_host]`, a word the part holds and `dos_reloc` re-aims when
  the movable host region moves (dos.asm:944);
- the ~45 near calls out go through ONE resident far-to-near bounce in the
  host (`call [cs:bx]` / `retf`, ~12 bytes) - the bytes grow in the PART;
- everything entered NEAR must have a resident stub: the WM close callback,
  the DHK_SNAP hook, `dvw_betab`'s doors (the core calls them near, on the
  worker, in the program's context), the worker's spawn entry. ~150 bytes of
  thunks (E: ~6 entries x 6 + `dvw_betab`'s table and stubs kept resident (158, M) or re-aimed);
- the part is **pinned while the world is in the IVT** (a program running),
  movable between runs (`op_movable`'s shape), or dropped at the run's end.

| | C, E unless marked |
|---|---|
| RAM saved per instance never windowed | 4,620 + 653 (M) - ~250 resident = **~5.0 KB** |
| RAM while windowed | part claim ~5,120 (granule-rounded) + ~250 resident against 5,273 today: **~+100 to +300 bytes**, and one more of the 8 owner claims |
| disk | +126 bytes of lost compression (M: host-without-DVW 20,062 + DVW code alone 3,824 against 23,760 packed as one) + 8 row + the far encodings, **~+300 bytes** |
| time | first Run in Window reads ~3.8 KB packed, ~8 sectors, ~1 `int 13h` call: **~0.4 s on a 5150** (PERFORMANCE.md's ~400 ms a call) |
| risk | **high**: 119 operands and ~45 call sites re-aimed by hand; `dvw_irq0` runs on a foreign SS inside `DVW_GAP` = 96 bytes (dosw.inc:58), so the bounce's extra frame must be counted against it; host relocation now has a second word to fix |
| work | ~4-5 days with the audit, plus `doswlive`, `doswtwo`, `doswfs`, `doswmouse`, `doswcap`, `doswcell`, `doswspin` re-run |

**BUILT, 2026-11-01, as option C' (SPEC.md 96.54.9 is the contract and
its table the measurement).** Against the estimates below: -3,923 bytes of
every DOS instance never run windowed (E ~4.1-5.0 KB), a 5 KB part claim
while a window has one, DOS.O88 +1,581 bytes (E ~+0.75 KB: the part's own
stream and its sectors), a glyph +4.2% and `AH=09h` 1.7% FASTER - the
second `con_celloff` crossing was taken out, inline too - and the first Run
in Window's fetch 1,075 ms on a 5150's floppy (E ~1 s). The macros are four
more than this paragraph names (`HICALL`, `PARTDS`/`PARTES`/`PARTCS`,
`HSEGTO`, `HENT`/`PADDR`); the checker found 211 sites on the sources before
them, where this paragraph counted 164.

**C', C done MECHANICALLY (costed, not built).** C's risk is that 164
sites are re-aimed by hand; every one of them is decidable from symbol
OWNERSHIP, which NASM already knows. So: a two-pass build - the host is
assembled first and its `[map]` becomes an `equ` file, so the part can name
`con_*`/`dos_*` - and the two files are written once against four macros
whose rule is the symbol's owner: `HCALL x` (a near call out), `HREF` (a
`[cs:]` operand naming a host cell - the 8), `HOSTDS`/`HOSTES` (every `push
cs / pop ds|es`, which after the move always meant the host - the ~12), and
plain DS operands naming a PART cell rewritten to `[cs:]` (a byte each; C
says most already are). A small host-side checker (`tools/`, at build time)
refuses what no macro covers - an operand or `dw` naming a host symbol in any
other form, `lea` of a host cell later used through another segment, a jump
table - so the classifier's blind spots fail the build instead of the run.
**The gate that makes it a translation rather than an audit**: with the
macros' IDENTITY arm (`HCALL` = `call`, `HOSTDS` = `push cs / pop ds`) the
host must assemble **byte-identical** to today's `dosp.bin`, which proves
every site was found before any of them is changed. The far arm: `HCALL x`
is `call far [cs:dvw_hx+4*n]` (5 bytes against 3) through a dword per host
routine in the part, landing on a 4-byte resident thunk `call x / retf`;
`HOSTDS` is `mov ds, [cs:dvw_host]` (5 against 2). Bytes, E from C's census:
part **+~310** (45 sites x 2, ~30 dwords 120, 12 x 3, 8 `HREF`s ~64);
resident **~+285** (30 thunks 120 + C's ~150 of entry stubs and `dvw_betab`
+ ~15 of `dos_reloc` re-aiming the ~30 segment words) against C's ~250 -
so the saving per never-windowed instance stays **~5.0 KB**. Cycles, E: a
far indirect call, the thunk's near call and `retf` are **~+110-130 a host
call** on an 8088 (~25 us); `dvw_irq0`'s tick path names one host cell
(`dos_dstk`) and calls no host routine, so it is **~+20-40 cycles a tick**,
nothing measurable; the virtual `int 10h` makes **2-3 host calls a
character** (`con_write`, `con_markcur`/`con_celloff`), **~+60-75 us
against ~1.2 ms**, a **~5-6%** slower teletype - the one real price, and
hand-written C could keep the same three routines as part-local copies
where this cannot without a census of its own. Stack: +4 bytes per host-call
depth against `DVW_GAP`'s 96 (the far frame and the thunk's near one). Risk:
**medium** rather than high - the classifier is wrong LOUDLY (identity gate,
checker) where a hand audit is wrong silently - with the residue being
computed addresses, which the checker flags for a hand decision. Work: ~2
days (macros, checker, gate) against C's 4-5, the same seven `dosw*` rows.

**D. One overlay slot, shared with something exclusive per instance.**
Same CS, so no `[cs:]` rewrite and no thunks - the doscore slot's own shape
(apps/dos/doscall.inc's jump table, `doscents.inc`), plus a host-symbol
export so the overlay can name `con_*`/`dos_*` (a two-pass build: the host
map feeds the overlay's `equ` file). The catch is the second tenant. During a
windowed run the fsx console is NOT exclusive (Alt+Enter moves a live
program, SPEC.md 96.54.1), nor is the packet driver; what is idle is the
Setup/Memory page (`dos_mem_*`, `dos_paint_mem`, `dos_click_mem`, the radios:
1,719 M - the Program menu greys during a run, dos.asm:9556, to be verified for
every page) and HELP's text (798 M). Saving = the smaller tenant, **~2.5 KB**
(E), paid for with a disk read at every Run in Window and again at the next
Setup open. Less saving than C, more disk traffic, no ISR risk.

**Recommendation for the windowed layer: C, but SECOND** - after section 3's
network part, which is the same pattern with a quarter of the coupling and
proves `dos_pfetch`, the bounce and the pin-while-installed rule on a small
target. Its value is bracket runs (+5 KB of arena, ~1% of a 640 KB machine's
~449 KB) and every DOS window left open idle. If the owner judges ~5 KB not
worth a high-risk rewrite of a layer that just shipped, D is the fallback and
B is not one.

### 2.1 The windowed layer, natively a part

**The owner's direction on this section: not C' - the layer reconceived as a
part from what it does at run time, best for speed and size.** The study is
its own file, docs/plans/DOS-WINDOWED-PART-PLAN.md; its answer in brief:

- the part owns all of the windowed run's state and is entered through a
  jump table; the host keeps 15 bytes of bss, ~554 bytes of thunks, door
  stubs, the worker's host half and a 31-byte register-transparent bouncer
  for the COLD calls out (E);
- the console stays in the host, and the part carries its own copy of the
  per-character teletype routines from the same source (391 bytes, M), so
  **no per-character or per-tick path crosses a segment**;
- RAM saved per instance never run windowed **~4.1 KB (E)**, the same as C'
  re-costed on this tree; a 5 KB claim while windowed; `DOS.O88` ~+1.25 KB;
- measured with two throwaway prototypes on MartyPC: a crossing on the
  virtual `int 10h` costs **~111 cycles** (C' is +4.3% on a glyph and
  +14.5% on `AH=09h`), and the native design's one change to that path,
  loading the host's DS from a part cell, costs **+0** (M).

## 3. Census of the rest (host image 48,668, core 15,430; all M)

| component | bytes | who needs it | could be lazy? trigger | verdict |
|---|---:|---|---|---|
| **network: `dosnet.inc` (cable translation, SPEC.md 96.26) + packet driver (`dos_pkt_*`, dos.asm:15442-16590, SPEC.md 96.23)** | 3,099 + 1,462 | a program that opens a packet driver, on a machine with a NIC or a NET.DRV cable; dosnet only on the CABLE route | yes - at `dos_pkt_bufs` (dos.asm:16167), the moment a Run finds a wire (`dos_pkt_want` CF=0). No wire, or the Memory page's Network box unticked: never read | **DO FIRST** (below) |
| windowed layer | 4,620 + 653 bss | `Run in Window` | yes, at the menu pick (dos.asm:9560) | section 2: C, second |
| HELP text `dos_h_text` (dosc.inc:1416) | 798 (609 packed) | the `HELP` command | yes, an `OP_ASSET` lazy row read by `dos_pfetch` into a transient claim, printed, freed | **do**: ~780 net once `dos_pfetch` exists, ~15 bytes of glue (E) |
| Win16 hand-off `dosw16.inc` | 324 | an NE double-clicked, typed or reached through a `.LNK` | moved to the loader for the desktop case - section 4 | section 4 |
| kern_dos hand-off (`dos_handoff`, `dos_wholeask`, ...) + `dos_kdh` bss | 367 + 561 bss | "Give DOS the whole machine" | the code yes; the record **no** - `dos_wake` reads `[dos_kdh+KDH_CODE]` on every wake to learn whether a handoff came home (dos.asm:1050-1060, 1182) | not worth it: the code alone is 367 |
| Setup / Memory page | 1,719 | the Setup pages | only as D's second tenant; `dos_pkt_want` and `dos_mem_arena` are read by `dos_run` | no, unless D - **OVERTAKEN: BUILT as part 5** (SPEC.md 96.32.2.3), as far code with the host's DS rather than a tenant of D's slot: the page's painters, placers and press handlers and the `.LNK` writer, -1,746 bytes M (6, item 5) |
| `.LNK` parse/save (`dos_lnk_*`) | 1,256 | parse: every launch of a shortcut (`dos_lnk_open`, dos.asm:1075); save: Save Shortcut | parse must stay; save (~400 E) is too small | no |
| driver suspend / BLASTER (`dos_drv_*`) | 266 | the bracket | too small | no |
| error strings, About | 356, 160 | refusals, About | too small; a refusal must not need a disk | no |
| console `os88con` + CP437 `os88cp437` + controls `os88ui` + `os88line` | 2,297 + 2,048 + 2,778 + 935 | the window itself, every instance | no - this IS the window | no |
| `os88sock.inc` (`net_try`) | 55 | `dos_pkt_want` | resident | no |
| trace dump (`DOSTRACE`) | 0 shipped | a knob build | already absent | nothing to do |
| core: shell `dosh.inc` | 4,339 | the prompt (the window's own face) and kern_dos's COMMAND door | the core is ONE asset in a fixed slot both hosts and kern_dos share (SPEC.md 96.44.5), 10 bytes under `CORE_MAX` | no: splitting the core is a second slot contract in three assemblies |
| core: INT 33h (`dos_int33`, `dos_m33_*`) / XMS (`dos_xms_*`) | 930 / 590 | a mouse program / an XMS client | same objection; a vector hooked at Run must already be there | no |

**The network part, which is the one to build first.** It is already almost a
part: `dosnet.inc` is entered through exactly four doors (`DN_DOOR init`,
`shut`, `tx`, `ready`, dosnet.inc:82-101), runs with **DS = its own claim**
(`dos_pkt_bseg`, SPEC.md 96.26.6), calls nothing in the host but `OSAPI_*`, and
its `[cs:]` operands are 21, of which 12 are `[cs:net_cls]` and the rest its
own constant strings and `dos_ebuf`. The packet driver's state is its own
`dos_pkt_*` cells (65 references, 3 `[cs:]`, 3 `push cs`), entered from the
host at five points (`dos_pkt_bufs`, `_start`, `_poll`, `_shut`, `_want`) and
from the program by the vector `dos_pkt_install` searches for. So: one
segment holding code AND state, CS = DS, read into the head of the claim
`dos_pkt_bufs` already makes (offsets of `PKB_*` rebased by the code's size,
an assembly constant), with `net_cls` and `dos_ebuf` copied in at `dn_init`.
`dos_pkt_want` and `dos_pkt_bufs` stay resident (they decide and claim).

| | E unless marked |
|---|---|
| RAM saved per instance on a machine with no wire, or with Network unticked | 3,099 + ~1,310 movable of the 1,462 (M spans) - ~40 of doors = **~4.3 KB** |
| with a wire | the code joins a claim that exists today: ~0 to +0.5 KB (granule) |
| disk | +126 bytes of lost compression (M: 21,290 + 2,596 against 23,760 for dosnet alone) + 8 row |
| time | ~3.5 KB packed read at the first wired Run of an instance, kept until the window closes: ~0.4 s once on a 5150 |
| risk | **medium**: the vector and IRQ0 hook (`dos_pkt_hook08`) point INTO the claim, so it is pinned while installed and the relocation proc (`dos_pkt_reloc`, today two instructions because one word names the block) must re-aim them when movable |
| rows | `dospkt`, `doscable`, `dosnetarena` (QEMU by name: MartyPC has no NIC) |

## 4. Windows programs: decide in the LOADER, before any part is read

**Today** (M): an NE double-clicked hands DOS.O88 its name; the loader
`op_load`s the carve (part 0, 55,952 bytes into a ~56 KB top-down claim),
`op_fetch`es the core (15,430, a second ~15.5 KB claim, transient) into its
slot, re-homes; the host creates and SHOWS its window, and only at the first
wake does `dos_w16_try` (dos.asm:1319-1322) read the header and call
`OSAPI_PKG_START` with WIN16.O88 and the document - **while DOS still holds
its 56 KB**, so WIN16 launches into a heap that much smaller - then
`OSAPI_WM_CLOSE`. The two parts are 23,796 + 13,691 packed (M), ~74 sectors
beyond the loader's 5, and ~71 KB of LZ4 decode at ~40 cycles a byte
(SPEC.md 2.9.13's 39.9): **~3.5-4 s of a 5150 launch** (E: the Makefile's
4,950 ms four-piece launch, Makefile:3577-3580, less the loader's own read).

**The owner's design is right and costs no kernel byte**, with one constraint
the kernel imposes. The loader's entry proc (`dsl_entry`) can make the test:
it runs on the UI task with the lock free, and file reads there are what
`op_load` already does. What it CANNOT do is call `OSAPI_PKG_START`: that slot
is UI-task-from-a-callback only (kernel/loader.inc:1776-1781), and from an
entry proc it would re-enter `ld_start` with the outer launch's `[ld_base]`,
`[ld_img]` and `[ld_status]` live. So the NE arm is:

1. `OSAPI_ARG_FILE` -> name, dir, vol, copied into the loader's bss as the
   16-byte locator WIN16 wants (SPEC.md 21.5.3). **It is READ-AND-CLEAR**
   (os88api.inc:5260), so on the NOT-NE path the loader must hand it to the
   host: one more `DSLH_DOC` record beside `DSLH_KDROW`/`DSLH_COREROW`, which
   `dos_entry` (dos.asm:1062) reads instead of asking the kernel - and a
   "checked, not NE" byte so the host's wake does not read the header twice.
2. `OSAPI_FILE_GOTO_QM`, `OSAPI_FILE_DFREE`, a one-cluster claim, one or two
   `READ_AT`s, the same `MZ` / `e_lfarlc >= 40h` / `e_lfanew` / `NE` test as
   dosw16.inc:44-116, free.
3. NE: create a minimal window (no menus, a frame), `OSAPI_WM_WAKE`, return
   it. The wake handler calls `OSAPI_PKG_START` (SI = `WIN16.O88`, ES:DI = the
   locator, DX:CX = 0) and `OSAPI_WM_CLOSE`s itself. A frame shows for one
   wake where today the whole DOS window paints. Not NE: `op_load` and
   re-home exactly as now.

A window-less exit (CF=1 with no window) would leave nobody to call
`PKG_START`; the flash-free version needs the kernel to POST a launch made
from an entry proc (a `[ld_pending]`-shaped queue serviced by `ui_task`,
~30-60 bytes, E) and is **not** recommended against the zero-kernel rule.
*(Superseded by 4.6: the owner asked for no flash, the queue already existed
as `[assoc_dapp]`, and the post measured 22 bytes.)*

| | |
|---|---|
| loader bytes added | **~+330** (E: dosw16.inc's test is 201 bytes of 324, M; the locator 80; a template, wake handler and close ~60; the doc hand-on ~15; less the `dos_be_goto` wrapper). The loader ships RAW (os88pkg.py refuses `--compress` beside parts), so this is +330 bytes of disk and one more sector of every DOS launch's read - coalesced, so no extra call; the RAM is transient, freed at the re-home |
| host bytes removed | **~0**. The host still needs `dos_w16_try` for an NE typed at the prompt (the `.con` arm, dosw16.inc:158-170), named in the Run box, or reached through a `.LNK` (the loader sees the link, not the program; `dos_lnk_open` resolves it in the host). It gains ~+20 (E) for the `DSLH_DOC` read and the skip byte. Dropping the in-host arm entirely saves 324 + 5 per instance and makes those three paths run the MZ stub's "This program requires Microsoft Windows" - the owner's call; this study recommends keeping it |
| RAM never claimed on the NE path | the ~56 KB carve and the ~15.5 KB core claim (M: 55,952 and 15,430 before granule rounding), the window, the console - and WIN16 starts with those 56 KB in the heap rather than held |
| time | **~3.5-4 s off every NE double-click on a 5150** (E); `tests/win16mines.py` already times double-click-to-window in ticks and is the instrument |
| builds | the loader arm goes under `DOS_WIN16`, and the dosload rule (Makefile:7540-7546) must be given `$(DOSW16DEF)` (Makefile:7482), which today it is not: kern_small and kern_mega keep no forward on either side; dosw16.inc's `%error` on `DOS_MEGA` stays |
| rows | `win16mines`, `win16fpu`, `win16calc`, `win16cpu`, `win16desk`, `win16hiber` all launch an NE through the DOS association and would run the loader arm (win16mines.py:162's window filter keys on `w16_name`, so the loader's window does not fool it). **No row types an NE at the prompt or opens a `.LNK` to one** - the arm that stays in the host is untested today, and the change should add that leg, and an assertion that no DOS carve was claimed on the NE path |

**Recommendation: build it**, early - it is independent of sections 2 and 3,
needs no fetch code in the host, and is the largest single saving in this
document for the case it covers.

### 4.6 BUILT, and measured against the kernel deciding instead

The owner's ask was **no visible flash**, which 4's window-for-one-wake
cannot give. Two designs were built far enough to measure; the first ships
(commit 5cf63d75, SPEC.md 21.5.4 and 124.3.1), the second was prototyped
(d3443ad1) and reverted (04c57525).

**Design 1 - the loader decides, the kernel POSTS the launch.** `dsl_w16`
in `apps/dos/dosload.asm` (under `-DDOS_WIN16`, which the dosload rule now
gets from `$(DOSW16DEF)`) does 4's steps 1-2, then on an NE calls
`OSAPI_PKG_START` with `WIN16.O88` and the document and REFUSES. The kernel
half is a contract on an existing cell, not a new one: the document arm,
called while `[ld_base]` is non-zero (from an entry proc), leaves the slot it
already writes in `[assoc_dapp]` and returns instead of calling `assoc_run` -
**`[assoc_dapp]` IS the queue**, the double-click's own, serviced by
`ui_task`'s `.chk_assoc` on the next pass. `assoc_run_x`, finding it standing
again after the load (it consumed it before), skips `ld_say_status` and
`files_refresh` - the refusal is silent and repaints nothing. A second post
replaces the first, which is the rule two queued double-clicks already
have. Not NE: the locator and a *read, no NE* byte go into the host's bss
after the core's row (`DOS_B_DOC`, `DOS_B_W16K`), `dos_entry` reads them
instead of the spent `OSAPI_ARG_FILE`, and the first wake skips
`dos_w16_try`; a `.LNK` clears the byte. The host's arm stays for the
prompt, Run and `.LNK`.

**Design 2 - the kernel decides which program opens the file.**
`assoc_nesniff` in `assoc_run_x` at `.known`: when the slot is DOS's, stand
in the document's folder (`assoc_back`), claim a cluster, `dskw_read_at_x`
the header, and re-point an NE at WIN16's row. Built with the two slots
HARD-CODED (rows 4 and 5); the version the brief asked for - the mapping
named nowhere - needs an `ASSOC.DAT` row kind keyed on a signature
(`ASC_VER` 3, a writer in `tools/os88disk.py`, the harvest and the merge
reading it, `t_*` readers updated), which was not built. That is
**~+80-150 more resident bytes (E**, from `asc_*`'s row-handling spans) and
a format change. kern_small has no associations, so neither design reaches it.

| | before (c714ec45) | design 1 (loader + post) | design 2 (kernel sniff, prototype) |
|---|---:|---:|---:|
| resident kernel bytes, kern_big / kern_emu | - | **+22** `.cold` (M, os88rdiff: 10 `ld_pkg_byname`, 12 `assoc_run_x`) | **+166** `.cold` (M) + ~80-150 for the signature row (E) |
| kern_mega / kern_small | - | +22 `.cold`, above 1MB / **byte-identical** (M) | the same shape (E) / none |
| loader (`dosload.bin`, ships RAW, transient) | 2,471 + 113 bss | 2,779 + 133 bss (**+308 / +20**, M) | unchanged |
| host per DOS instance (`dosp.bin`) | 48,032 + 7,284 bss | 48,047 + 7,301 (**+15 / +17**, M) | unchanged |
| `DOS.O88` on disk | 50,577 | **50,577** - the 512-byte padding took it (M) | 50,577; `KERNEL.SYS` +166 |
| NE double-click to WIN16's window, 5150 CGA, 360KB B: (MartyPC) | 10.59 s, 10.79 s (two runs) | **6.13 s, 6.24 s** | **5.59 s** |
| `int 13h` calls in that launch | 15 (19 in one run) | **10** | **9** |
| DOS's window on the glass | frames 257-569, ~5 s | **never** (every frame stepped) | never |
| DOS's 55,328-byte carve claimed | yes | **no** | no |
| plain MZ (`DOSHELLO.EXE`): calls / sectors | 14 / 110 | 14 / 110 | 14 / 104 |
| ...DOS's window up / program ended | 5.39 / 8.34 s | 5.84 / 8.58 s | 5.80 / 8.34 s |

One run each per arm except where two are given, all `os8088_5150_cga_gla`
with WIN16.O88, DOS.O88, TINY16.EXE and DOSHELLO.EXE on one 360KB B:; the
base's two NE runs agreed to 0.2 s and its MZ runs to the frame. TINY16 on an
8088 is WIN16's own refusal window, which is WIN16's window all the same.

**What the ordinary `.EXE` pays.** Neither design adds an `int 13h` call:
the header read moved, it did not multiply (design 1 moves the host's read
into the loader; design 2's second read, the host's, came back without a
call of its own). Both put that read IN FRONT of DOS's window, so the window
is ~0.45 s later in both. Design 1's whole launch was **+0.23 s** later on
this disk: one more seek (10 against 9, 68 cylinders against 60), because
the loader reads the program between DOS.O88's image and its parts and the
program's load then seeks back. It depends on where the two files sit, and
it is the one number here that favours design 2.

**Risks.** Design 1: a new contract on a published cell (SPEC.md 21.5.4,
recorded with `tools/abilock.py`, no bump - the context was not allowed
before); an entry proc that posts and then does NOT refuse starts both
programs and skips its own document wake; the loader moves the instance to
the document's folder and back (`OSAPI_FILE_GOTO_QM` twice) before
`op_load`. Design 2: 166-300 bytes resident on every `kern_big` machine for
ever, for a check one package's documents need; either a kernel that names
`WIN16`/`DOS` or a format change to say it; the UI task reads the header of
every `.COM` and `.EXE` the DOS row opens, kernel-side, and the host still
carries its own copy of the test for the prompt.

**Recommendation: design 1, as shipped.** It costs 22 resident bytes against
166+, says nothing about WIN16 in the kernel, and gives the owner what was
asked - no frame of DOS - with 4.5 s and five `int 13h` calls off every NE
double-click on a 5150. Design 2 is ~0.55 s faster on the NE (it never reads
DOS.O88's loader) and avoids the one extra seek; neither is worth 144+ bytes
resident on every machine.

**Rows.** New: **`win16nedos`** (soak, MartyPC, ~75 s; TINY16.EXE is
assembled from `tests/win16/tiny16.asm`, so it needs neither Microsoft's
samples nor Watcom): frame-stepped from the click, no window called DOS on
any frame, no carve-sized claim, no toast; a plain MZ still opens DOS; an NE
TYPED at its prompt still reaches WIN16 through the host's arm (the leg 4
said no row had). **Red on c714ec45** (DOS's window at frame 261, carve
55,328). To run in the final pass: `win16nedos`; `win16fpu`, `win16fpuxt`,
`win16desk`, `win16hiber` (need `make win16fixtures`, Open Watcom);
`win16mines`, `win16minesxt`, `win16calc`, `win16cpu`, `win16cpuxt` (need
the win16-reference skill's Microsoft samples, `win16samples`); `win16ne`
(fast); and DOS's launch rows that go through the loader and the hand-on:
`dosexe`, `doscom`, `doslnk`, `dosopen`, `dosargs`, `dosmedia`, `dospkg`,
`dosext`, `dosaltenter`, `doswlive`; `pkgrun` (the cell's called arm);
and the association rows `assoc*`.

### 4.7 Kept for later: a Windows program with NO DOS on the disk at all

**Owner's decision, 2026-11-01: design 1 ships, and design 2 is RECORDED, not
refused.** What design 2 buys that design 1 cannot is not the 0.55 s. Design
1 still needs DOS.O88 on the disk to RECOGNISE the program, because it is
DOS's loader that reads the header. Design 2 decides in the kernel, so a
Windows program can start with no DOS.O88 present at all.

That matters for a ONE-DISK 360KB machine. WIN16.O88 is 64,162 bytes and
does not fit on the system disk. So today a user puts in the disk WIN16 lives
on, and that disk must also hold the Windows program and DOS.O88 (50,577
bytes, ~50 of 354 clusters), only so that something can read an NE header and
hand it on. Taking DOS.O88 off that requirement is worth ~50 clusters on
exactly the geometry that runs out first.

**What it would take to be CLEAN** (the prototype was not): the mapping named
nowhere in the kernel. That means an `ASSOC.DAT` row kind keyed on a
SIGNATURE rather than an extension (a magic at an offset, or "MZ whose
e_lfanew points at 'NE'"):
- an `ASC_VER` bump;
- a writer in `tools/os88disk.py`;
- the harvest and the merge reading it, and the `t_*` readers updated.

Measured +166 resident `.cold` for the sniff with hard-coded slots
(prototype `d3443ad1` on this branch, reverted by `04c57525`), plus ~80-150
estimated for the signature row. kern_small has no associations and would
not carry it.

**Why it may come back GENERALISED.** A signature-keyed association is not
an NE feature. Any extension that names more than one program ("`.EXE`" is
DOS's MZ, Windows' NE, and later perhaps OS/2's LE or a self-extracting
archive's stub) has the same shape. The first time a second such type wants
a launcher, the cost is shared. At that point the chain
`DOS.O88 + WIN16.O88 + the program` becomes `WIN16.O88 + the program`.

Revisit when:
- a second content-sniffed type appears; or
- the 360KB one-disk Windows case is wanted; or
- design 1's +22 can be folded into it.

## 5. What it costs the loader and the kernel

- **Kernel: 22 bytes of `.cold`**, section 4.6's post (kern_small none), and
  zero for everything else recommended. `OP_SELF` rows, `OSAPI_FILE_READ_AT`,
  `OSAPI_DECOMP`, `OSAPI_MEM_P` and `OSAPI_MEM_MOVABLE` all exist; the only
  kernel options named (an in-place-only regrow, a posted launch from an entry
  proc) are refused above.
- **Loader**: +330 (E) for section 4; +~10 transient bytes per lazy row whose
  row it copies into the host's bss (the `dsl_row` pattern); an `OP_SELF` row
  adds no fetch body to it (SPEC.md 20.12.9.1).
- **Host, resident, shared**: `dos_pfetch` ~100 (E) + the far bounce ~12,
  once.
- **Disk**: each part ~+130 bytes of lost compression (M on slices) + 8 of row.

## 6. Order of work

1. **DONE** (4.6). Section 4, the loader's NE arm (~1 day, E). Gate: the six `win16*` rows
   plus a new prompt/`.LNK` leg.
2. **BUILT** (SPEC.md 96.23.11): the network part. **-4,363 bytes** of every
   DOS instance (M: host image 47,520 -> 43,227, bss 7,309 -> 7,239), the
   plan's ~4.3 KB E confirmed. Part 4, 4,480 bytes raw and 3,662 packed;
   `DOS.O88` +1,009 bytes on disk (M - the part's own sectors cost more than
   the +134 E, which priced compression alone). It took `op_self` with a KB
   FLOOR (`AH`, +8 bytes) rather than a second reader, so the code and the
   buffers are ONE claim, and a shared `dos_pself` that HELP now uses too.
   The pin is the bracket's (from `dos_pkt_start`, bucket C), not a
   relocation proc that re-aims vectors a client may have copied. kern_mega
   carries no network at all (every launch there is the whole machine, so it
   was unreachable): -4,714 bytes there. The first networked run's read is
   ~1 s on a 5150, ESTIMATED from HELP's measured 985 ms - no emulator here
   with a wire was used to time it. Gate: `dospkt`, `doscable`, `dosxlat`,
   `dosnetarena`, all four adapted to the cleared default.
3. HELP as a lazy asset: BUILT as evidence (1.1), -519 bytes M with the
   reader in it. Gate: `dosopen`, `dosconcga`, `dosaltenter`.
4. The windowed layer as a far part, option C (~4-5 days, E): ~5.0 KB a
   never-windowed instance. Gate: the seven `dosw*` rows. Or D, or neither -
   the owner's decision on the risk.

5. **BUILT** (SPEC.md 96.32.2.3), on the owner's word: the SETUP PAGE as
   part 5 (kern_big, kern_emu), read when Setup opens and freed at `Return`.
   Everything a launch reads stays resident - the `.LNK` reader, every
   setting's value, `dos_mem_fix`/`_whole`/`_take`/`_arena` - and the `.LNK`
   WRITER went with the page, being reachable only from its `Save Shortcut`.
   **-1,746 bytes** of every DOS instance (M: host image 43,292 -> 41,520,
   bss 7,239 -> 7,265), with section 2's D refused in passing: the part is
   far code with the HOST's DS, assembled inside the host's own assembly in a
   `vstart=0` section, so its data names are the host's and only its CALLS
   need help - a stub per host routine and a 5-byte bouncer, and
   `tools/dossetchk.py` failing the build on any near branch that leaves it.
   It is PINNED while held (its return addresses sit on the stack across
   kernel calls). Part 5 is 2,320 bytes raw and 1,914 packed; `DOS.O88`
   +812. (A size pass since moved `os88ui`'s check box, radio, drop-down
   and glyph, and the arena figure's own arithmetic, out of the window and
   the host into it: part 5 is 4,274 raw, and a windowed instance 1,898
   bytes smaller - SPEC.md 96.32.2.3.2.) Opening Setup costs **1,013-1,181 ms** on MartyPC's 4.77 MHz
   machines (M: two `int 13h`, 7 sectors, 544 ms of seek), every open. The
   same change added SPEC.md 96.32.2.4's `Run in a window` box (85 resident
   bytes of the figure above). kern_mega keeps the page inline - its host is
   above 1MB. Gate: `doslnk`, `dosram`, `dosmem`, `dosest`, `dosenvfold`,
   `kdpart`.

Total for 2-4: **~10 KB per DOS instance that neither runs windowed nor
touches a network** (E, from M spans), for ~+600 bytes of disk and no
kernel byte.
