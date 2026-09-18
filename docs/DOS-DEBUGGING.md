# Debugging a DOS program under os8088

A DOS program that misbehaves in the box (SPEC.md §96) is **not debuggable
from one side**. Our own trace says what we were asked and what we answered,
and both look right — they looked right through four separate defects in one
session, every one of which was found only by putting the same program in
front of a real IBM DOS and diffing.

This is the method and the tools. Read *The method* first; the rest is
reference.

---

## The method

1. **Log what the program asks us**, with the *call site* of every call.
2. **Log what it asks a real DOS**, in the same format.
3. **Align the two on (function, call site)** — never on line number.
4. Read two things off the alignment: where the code paths **part**, and the
   first place the same instruction was **answered differently**.

### Why the call site is the whole instrument

Two runs of one program differ in every address — the load segment, the heap,
the stack — so a line-for-line diff is noise from call 0. What does *not*
differ is the sequence of instructions that made calls. The ring records
`CS:IP` off the frame the `int` pushed, and the reader subtracts the PSP, so
`+0C93:A4DD` means the same instruction on both machines however they were
loaded.

That turns *"they diverge somewhere"* into *"they diverge at this
instruction"* — and **three of the four defects were cases where the
instruction was the same on both sides** and only a computed value differed.
No amount of reading our own trace would ever have shown those; our trace was
correct, right up to the call where the two sequences stopped agreeing.

### What each finding looks like

| the alignment says | it means |
|---|---|
| runs align, one side has extra calls | the program **branched** — read the last agreed call |
| same site, same arguments, different answer | **we answered wrong** — this is the common one |
| same site, same answer, program diverges later | the program read something with **no call in it** — PSP, BDA, IVT, its own memory |
| nothing aligns at all | different program, one trace wrapped, **or the program runs from a dynamically-placed OVERLAY** — see below |

**A program that executes out of overlays breaks the alignment key, and the
failure looks like "nothing aligns at all".** The key is `CS - PSP : IP`, on
the reasoning that it does not move between two machines that loaded the
program at different addresses — which is true of the base image and false of
an overlay, whose segment depends on how much memory was free. Microsoft
Works is that program: aligning two of its runs reported **1 call of 130
aligned**, with the IPs matching exactly (`0398`, `0404`, `063C`, `0610`) and
only the segment bases differing (`636F` against `8D7E`).

**The tell is in the gap listing itself**: `os8088 alone` and `dos alone`
print the same functions at the same `:IP` with a different `+segment`. When
you see that, re-align on `(function, IP)` alone and the divergence falls out
in one pass — docs/FIELD-NOTES.md 54 is the worked example, and it is how
§96.10.6 was found after two rounds of looking at the wrong subject.

The third row of the table is the one to be ready for. `--state` dumps the PSP, the IVT,
the BDA and the box's handle table for exactly that case; §96.21.4 is eight
PSP fields that were zero here and are not zero under DOS, found that way.

---

## The tools

| | |
|---|---|
| `tools/os88dosdbg.py` | the driver: trace, reference-trace, align, dump state |
| `tools/os88fat.py` | edit a floppy image in place; **say what a drive can reach** |
| `tests/dostrap/trap.asm` | the TSR that logs a real DOS's `INT 21h` traffic |
| `tests/dostrap/dosref.asm` | one binary that answers the same questions on both |
| `tests/dostrap/regs.asm` | ...and the same shape for the REGISTERS: which ones does `INT 21h` give back? (SPEC.md 96.7.1.2) |
| `tests/dostrap/vecs.asm` | ...and for the VECTORS: which of DOS's own block are installed, and do the four calls that go through them come back? (SPEC.md 96.5.2) |
| `tests/dostrap/mcb.asm` | ...and for the ALLOCATOR: can a block grow back into what it gave up? (SPEC.md 96.9.2) |
| `tests/dostrap/rdsum.asm` | did the program get the bytes the disk holds? |
| `tests/dostrap/twoopen.asm` | is it the file, or is it the *second handle*? |
| `tests/dostrap/diskcost.asm` | what one open and one read cost the DRIVE — the only SPEED probe |
| `tests/dostrap/dfree.asm` | what `AH=36h` answers, for every drive letter |
| `tests/dostrap/cx0.asm` | what `AH=40h` with `CX=0` does to a file's LENGTH (SPEC.md 96.11.6.2) |
| `apps/dos/dos.asm`, `%ifdef DOSTRACE` | the box's own ring — **not in any shipped build** |

Both Python tools carry `--selfcheck`, which needs no emulator and no network.
Run them before trusting an answer:

```sh
python3 tools/os88fat.py    --selfcheck
python3 tools/os88dosdbg.py --selfcheck
```

### A session, end to end

```sh
# 1. is the disk even readable on the machine you are about to use?
python3 tools/os88fat.py reach GAME.img               # 40-cylinder drive
python3 tools/os88fat.py reach GAME.img --cylinders 80

# 2. a system disk carrying the traced box (built, then VERIFIED)
python3 tools/os88dosdbg.py build

# 3. our side
python3 tools/os88dosdbg.py trace GAME.EXE --disk GAME.img --state \
        -o build/ours.json

# 4. the reference.  The DOS floppy is YOURS - it is copied, never edited.
python3 tools/os88dosdbg.py ref GAME.EXE --disk GAME.img \
        --dos-disk ~/dos330.img -o build/ref.json \
        --free XCOPY.EXE REPLACE.EXE FORMAT.COM FDISK.COM SYS.COM

# 5. the answer
python3 tools/os88dosdbg.py diff build/ours.json build/ref.json
```

Traces are JSON, so they keep: attach one to a bug, diff it against the same
program after a fix, or read it in Python.

---

## `tools/os88dosdbg.py`

### `syms [NAME...]`

Every constant and bss offset in `apps/dos/dos.asm`, **derived by assembling
it**, never transcribed. With no names it prints the set the tool itself uses.

This exists because nasm will not print a symbol's value and `-f bin` emits no
map. The trick is to make the assembler emit the numbers: append a signature
and a `dw` of each name to a copy of the source, assemble, read the words back.
Use it whenever you want to poke at the box's state from the host.

### `build [--system TARGET] [-o IMG]`

A system floppy carrying the `DOSTRACE` build of `apps/dos`.

**The order matters and getting it wrong is silent.** `make` rebuilds the
system disk's `DOS.O88` from `apps/dos/dos.asm` whenever the source is newer,
so a copy made *before* `make` is overwritten by it — and the disk then carries
the **shipped** package while every symptom points at the guest. That cost a
whole debugging round: the ring read as empty. So the tool runs `make`, *then*
copies, *then* extracts the package back off the finished image and compares it
byte for byte. `build/` is put back to the shipped package either way.

**And WHICH file it copies over is read out of the Makefile**, because
`$(SYSROOT)` has been two things: `build/dos.o88`, the plain compressed
package, and since SPEC.md 96.40.3 `build/kdos/DOS.O88`, the four-piece one
carrying `kern_dos`. Writing the traced package over the file the disk rule
does **not** read is the same silent failure by a second route, which is why
the verify above exists rather than being belt and braces.

**The third radio arm is greyed on a trace disk**, and that is correct rather
than a limitation: a `DOSTRACE` build is one image with no part table, so
`dos_mem_whole` reads zero (SPEC.md 96.36.1). The ring this tool reads lives in
the box's own bss, and past the handoff there is no box.

### `trace PROG.EXE --disk IMG [--state] [--until N]`

Boots the traced box under MartyPC, double-clicks the program, and reads the
ring **out of guest memory** — not off the screen. That matters: inside the
`fsx` bracket the program owns the adapter, so a program that sets Hercules
graphics has no text screen to read, and `m.screen()` returns the framebuffer
decoded as characters, which looks like garbage and is easy to misread as a
crash.

`--until N` stops once *N* calls are logged, for a program that never exits.
Without it the trace runs to the program's own exit or the ring's cap.

`--state` writes `<out>.psp.bin`, `.ivt.bin`, `.bda.bin` and `.fhtab.bin`
beside the trace, and prints the box's open handles. Take the same four under
DOS (`--state` on `ref` is not implemented; use `cmp` against a known-good run,
or read the PSP dump directly) whenever the program diverges with no call in
between.

### `ref PROG.EXE --disk IMG --dos-disk YOURS.img`

The same program under a real DOS, logged by `tests/dostrap/trap.asm`.

- **No DOS is in this repository and none can be.** You supply the floppy.
- It is **copied, never edited**. `--free NAME...` leaves files out of the
  copy to make room for the tracer; a DOS system floppy is usually full of
  utilities the program under test does not use.
- `--boot-keys` is the number of Enters for the date/time prompts — 2 for IBM
  DOS 3.30, 0 for a DOS that does not ask.
- The TSR **arms on the first `AH=4Bh` EXEC**, so the ring is the program's and
  not `COMMAND.COM`'s prompt, and it **keeps the first N and stops**, because
  divergence is early and a ring that wrapped would throw away the only part
  that matters.

### `diff A B`

Prints the aligned runs, then every gap (with what each side did alone), then
**two findings that mean opposite things**:

- **The first differing ANSWER** — same instruction, same arguments, different
  reply. *This is the box being wrong.* Go and read the handler.
- **The first differing QUESTION** — same instruction, different arguments.
  *The program computed something different*, from state with no call in it:
  the PSP, the BDA, the vectors, its own memory. Reach for `--state`.

Getting *"no call was answered differently"* is a result, not a blank: it says
nothing the box **says** is the difference, which is most of the search space
gone in one line.

Both checks are deliberately narrow, and the narrowness is load-bearing —
every widening below was tried and reported noise on the first or second call
of every trace, which buries the real finding:

- `CF` is always comparable. The error code is comparable whenever both failed.
  `AX` on *success* only for the functions where it is a fact rather than a
  leftover or an address (`AX_IS_AN_ANSWER`): `AH=4Ah` leaves `AX` undefined
  and `AH=48h` answers a **segment**.
- An argument counts only where it is a **value**, not an address
  (`ARG_VALUES`): `DS:DX` is a filename on `AH=3Dh` and the two stacks sit two
  bytes apart, so comparing it reports a different question at every open.
- `AL` counts only where it is an argument (`AL_IS_AN_ARGUMENT`): `AH=19h`
  reads nothing, so its `AL` is whatever the program last had there.
- A finding in the first few calls of a run that **follows a gap** is annotated
  as such. When a loop runs a different number of times on the two sides, which
  iteration pairs with which is difflib's guess; the tool says so rather than
  hiding it with a heuristic.

---

## `tools/os88fat.py`

`tools/os88disk.py` *builds* an image and is the right tool for anything this
project ships. This one **edits an image somebody else built**, which has a
different constraint: a bootable DOS floppy keeps IBMBIO and IBMDOS exactly
where SYS put them, and a game disk keeps every file where its installer put
it. Nothing here rewrites, compacts or re-orders.

```sh
python3 tools/os88fat.py ls    IMG                  # cluster, LBA, cylinder
python3 tools/os88fat.py add   IMG FILE [NAME.EXT]
python3 tools/os88fat.py del   IMG NAME...
python3 tools/os88fat.py cat   IMG NAME -o OUT
python3 tools/os88fat.py head  IMG NAME -n 32       # hex, and the first 3 words
python3 tools/os88fat.py reach IMG [--cylinders N]
```

### `reach` is the one nobody expects to need

**Every MartyPC 5150 profile in this tree has 40-cylinder drives** — 9 sectors,
2 heads, 720 sectors — and a 720KB image has 1,440. The first half reads
perfectly; the second answers an `int 13h` error, which `apps/dos`'s read
window turns into **end of file** (SPEC.md §96.11.4). So the program is handed
a file that is *shorter than it is*, and blames the file.

Nine of Prince of Persia's files sit past cylinder 39, and nothing anywhere
reported it as a configuration problem. It cost most of a day, twice: once
chasing the game, once chasing a probe that would not load — **and then a
third time**, on a run that had a hard disk in it.

`os8088_5150_herc_sb_720_gla` is this tree's 720KB machine and **has no hard
disk**, which is the gap the third one fell into: an installer needs `C:`, so
the only machine that would host it was `os8088_xt_hdd`, whose drives are
40-cylinder. `os8088_xt_hdd_720` exists now and is the **only profile here
with both** — it is `os8088_xt_hdd` with one overlay line changed. The
symptom it removes is worth recognising on sight: the copy ran, reported
success on eleven files it had **silently truncated**, and then failed
outright on the first file whose *start* was out of reach — which reads
exactly like a bug in a copy engine and is a drive.

If you need another geometry, clone a profile in
`tools/martypc/configs/os8088_machines.toml` and change the floppy overlay
(`pcxt_2_720k_floppies` and friends are defined upstream, in
`build/martypc/run/configs/machines/config_overlays.toml`), then re-run
`tools/martypc/build.sh` so the run tree picks it up — **and that last step is
not optional**: the run tree is built by `cat`-ing that file into
`build/martypc/run/configs/machines/ibm5150.toml`, so a machine added to the
source and not rebuilt is refused with *"No machine configuration for
specified config name"*, listing every name but yours.

**THE TRACER COSTS THE DOS PROGRAM 42 KB OF ARENA, so no memory-shaped
conclusion survives a traced run.** Measured on `os8088_5150_herc_sb_720_gla`
with the same disk: the shipping build gives the program **451 KB** and the
DOSTRACE build **409 KB**. The ring is a package PART now (§96.29.1), and a
part is claimed out of the same heap the arena comes from — so the instrument
takes its 42 KB off the top of the quantity you are measuring.

It is worth naming because the failure is so plausible. Prince of Persia
sizes itself from `AH=48h AL=03 BX=FFFF` — *"how much is there?"* — and a
traced run answered **220 KB** against a real DOS's 393 KB, which reads
exactly like the box being short of memory by 173 KB. It is not: on the
shipping build the gap is about a third of that, and the rest was the
tracer. **Ask the arena question on the SHIPPING build** (`[dos_akb]`, and
`dos_syms(..., defines=())` — the default is `DOSTRACE`, whose bss offsets do
not describe a shipped package and which will hand you a confident zero), and
use the trace for what it is good at: which call was asked, and what was
answered.

The other 14 KB is real and is the sound driver. `OSAPI_DRV_SUSPEND` *used
to* free that memory *inside* the fsx bracket, after the arena had already
been sized — docs/plans/DISK-CPU-PLAN.md §5 in one number, and the reason a
carded machine reported 14 KB less than one without. SPEC.md §96.40.3's
parted `DOS.O88` gives it back (§96.35), by taking the driver before the
sizing rather than inside it, so the gap above is history rather than the
current number.

**A program in a SUBDIRECTORY needs two things of `ref` that `trace` does not,
and both were missing until Prince of Persia wanted them.** `trace` hands the
program to `os88ui.path()`, which wants forward slashes; `COMMAND.COM` reads
one as a **switch character** and answers *"Bad command or file name"*, so
`ref` translates to `\` — one invocation, one program name, two machines. And
a program may demand that its own directory be CURRENT rather than merely
named: Prince of Persia answers `B:\PRINCE\PRINCE` with *"Unable to find
necessary files. Please start program from the default drive and directory"*,
which is not a DOS refusal at all but the game's own. `--cd PRINCE` types the
`CD` our side gets for free from a double-click. **Without it the comparison is
two machines doing different things**, and the reference looks broken.

`reach` also walks the **root only** — a documented scope, but worth knowing
when a disk keeps everything in a folder: `PRINCE/`'s files run out to
cylinder 59 and `reach` reports only the three entries in the root.

`reach` reports the **last** cylinder a file touches, not the first: a file
that starts inside the drive and runs off the end truncates in the middle,
which is harder to spot than one that cannot be opened at all.

---

## The guest-side probes

They are `.COM` programs, so they run under our box *and* under a real DOS,
unchanged. Assemble one with `nasm -f bin -o NAME.COM tests/dostrap/NAME.asm`
and put it on the disk with `os88fat.py add`.

Each of them **waits for a key before exiting**. That is not a courtesy: the
box's `fsx` bracket ends when the program does and the desktop comes straight
back, so a probe that prints and exits leaves its answers on the screen for a
few milliseconds.

### `dosref.asm` — one binary, two DOSes

Puts the questions the box has to answer to *a* DOS and prints what it said.
The same binary runs on both sides, so the two columns diff with nothing to
interpret. It is how SPEC.md §96.12.1.2's table was measured, and it found four
wrong answers in one run.

**Add a row whenever you settle an argument about what DOS does.** A rule read
out of a reference book is an opinion; this is a measurement. Keep the output
one line per question, `AX/CF=` shaped, so the two columns stay diffable.

### `rdsum.asm` — right bytes, or right behaviour?

*"The program was given the wrong bytes"* and *"the program did the wrong thing
with the right bytes"* look identical from outside. This reads a named file
whole and prints its length and a **position-sensitive** checksum (rotate, then
add), which the host computes off the image with `os88fat.py cat`. A plain sum
would be blind to exactly the failures worth catching here — a cluster chain
walked wrongly, a window refilled from the wrong offset, two handles sharing
one buffer.

### `twoopen.asm` — the file, or the second handle?

The box has **one** read window; a program reading two files at once is
ordinary. Three cases in one run: X alone, Y alone, both open at once. A and B
passing with C failing is the window (SPEC.md §96.11.5 is that bug); all three
failing is the file; B alone failing is usually the disk — go back to `reach`.

### `diskcost.asm` — what ONE open and ONE read cost the DRIVE

The only one of the four that is about **speed** rather than correctness, and
the only one the host brackets from outside: it does `NOPEN` opens and `NREAD`
reads of `CHUNK` bytes and then **waits for a key**, so
`os88marty.Marty.disk()` — MartyPC's own floppy-controller counters — can be
read either side of it. Assemble one binary per point and take the
**difference** between two:

```
nasm -f bin -DNOPEN=1 -DNREAD=0               -o D1.COM tests/dostrap/diskcost.asm
nasm -f bin -DNOPEN=4 -DNREAD=0               -o D2.COM tests/dostrap/diskcost.asm
nasm -f bin -DNOPEN=1 -DNREAD=1 -DCHUNK=8192  -o D3.COM tests/dostrap/diskcost.asm
nasm -f bin -DNOPEN=1 -DNREAD=4 -DCHUNK=8192  -o D4.COM tests/dostrap/diskcost.asm
```

`(D2 − D1) / 3` is one open and `(D4 − D3) / 3` is one 8KB read. Every point
carries the identical fixed cost — the file manager loading it, the box
claiming its arena — so that cost cancels and never has to be known. The same
binaries run under a real DOS, which is the whole point: *"we make seven times
the `int 13h` calls"* is a ratio nobody can act on, and *"one open costs us N
calls and DOS M"* is a defect with an address. SPEC.md §96.24.1 is what it
produced.

Three traps, all paid for once:

- **The buffer is not emitted.** A `.COM` owns every byte after its image, so
  `times CHUNK db 0` would only make the *file* bigger — and a bigger file
  costs more to load, which is a confound in the one measurement this exists to
  take. Every point must cost the same to start.
- **Give it a disk of its own.** The file under test needs a known DIRECTORY
  POSITION, because that is exactly what an open costs here; and os8088's Disk
  window lists a bounded number of entries, so a busy root simply *hides* the
  later probes — they are on the floppy and cannot be double-clicked.
- **The key wait is not a courtesy.** Under os8088 the fsx bracket ends when
  the program does and the desktop comes straight back, taking its own disk
  traffic with it.

### `vecs.asm` — the vectors DOS OWNS, and the calls that go through them

The box installed seven of DOS's own vectors and left fourteen at
`0000:0000`, which is **not** *unimplemented* — an `int` through a null vector
executes the vector table, and there is no way to test for it beforehand
because *the probe is the call*. Nothing in our own `INT 21h` trace looks wrong
at any point; the last thing answered correctly and the first thing that
crashes are three instructions apart.

Five lines, and the first four are identical on both machines:

    NUL=NONE                     every vector of DOS's own block installed
    2A=00                        `int 2Ah` came back, and AH survived it
    29=[*]                       `int 29h` put a character up
    25=CF? AX=???? SPD=0000      INT 25h's STACK, which is the assertion
    KEY

`CF` and `AX` on the `25` line are **not** comparable and the probe judges
nothing: IBM DOS reads sector 0 of drive A and succeeds (`CF0 AX=0100`), this
box refuses (`CF1 AX=0C01`). `SPD` is the finding — `INT 25h` and `INT 26h`
are the only calls of the era that do not `iret`, DOS leaving the `FLAGS` word
the `INT` pushed on the stack for the caller to pop, so a handler that `iret`s
answers correctly and unbalances the caller by two bytes. That faults somewhere
else entirely and looks like anything but this.

**The commonest failure of this probe is a hang**, not a wrong line: a null
vector sends it into the IVT rather than printing something false. So read the
partial output — `NUL=2A` followed by silence names the vector *and* shows what
happened next.

It reads sector 0 of drive A, which is a read, so neither disk is at risk.

### `mcb.asm` — can a block grow back into what it gave up?

`AH=4Ah` grows a block only into the block immediately above it. That is DOS's
rule; what DOS *also* does is **coalesce adjacent free blocks during the
allocation walk**, and a box that implements the first half without the second
refuses a block smaller than one it has already granted.

The five steps are every DOS memory manager's own shape, in eighths of the
largest block there is: take the lot, give half back, take three quarters, give
half back, take seven eighths. **Step 5 asks for less than step 2 was given**,
so `cf=1` there is the finding with nothing to interpret.

    IBM DOS 3.30            this box
    MAX=93AE                MAX=6D9E
    ask=93AE cf=0 bx=93AE   ask=6D9E cf=0 bx=6D9E
    …                       …
    ask=8138 cf=0 bx=8138   ask=5FEA cf=0 bx=5FEA

**`MAX` is not comparable and nothing under it is** — the two arenas differ by
design. What has to hold on both machines is the *shape*: five grants, each
answering the size it granted.

### `dfree.asm` — the four registers, for every drive letter

`AH=36h` (SPEC.md §96.27) answers four things and a program usually reads one
of them, so *"the installer stopped complaining"* is not *"the numbers are
right"*: a wrong total-cluster count is invisible to a caller that only wants
free bytes. This prints `AX`, `BX`, `CX` and `DX` for drives 0..4 and the host
compares them with `tools/os88fat.py` reading the same image — one side the
guest's arithmetic, the other an independent FAT reader that shares no code
with it.

`AX=FFFFh` is the row to look at hardest. It is DOS's own *invalid drive*, and
it is the answer a program can act on **when it ignores the carry — which for
this call every program does**, DOS never setting `CF` here. That is the exact
shape of the defect §96.27 fixed: unimplemented, the call returned `AX=1` with
`CF`, and Prince's installer read it as one sector per cluster.

### `renref.asm` — `AH=56h` renames, and it also MOVES

`OSAPI_FILE_RENAME` rewrites a directory entry in the folder you are standing
in (SPEC.md §18.4). DOS's `AH=56h` does that *and* uses the same call to move
a file between directories of one volume by re-linking it. So the question a
probe has to answer is not *"does rename work"* — it is which shapes DOS
refuses, which it does silently, and with what codes.

Measured, IBM DOS 3.30 against this box, same binary, every drive letter
built at run time from `AH=19h` so that both columns mean the same thing:

```
                  DOS 3.30        os8088
rename            0012 CF=0       0000 CF=0
gone              0002 CF=1       0002 CF=1
onto itself       0005 CF=1       0005 CF=1
old THIS drv      0012 CF=0       0000 CF=0
old OTHER drv     0011 CF=1       0011 CF=1
new OTHER drv     0011 CF=1       0011 CF=1
new is a path     0012 CF=0       0000 CF=0
old OTHER, real   0011 CF=1       0011 CF=1
```

**Three of those six are worth having in front of you before writing the
handler:**

- **`AX` IS JUNK ON SUCCESS.** The row that worked reports `0012h`, and so do
  both rows at the bottom. Only `CF` is the answer, which is a trap for
  anyone who reads `AX` the way `AH=4Eh` invites.
- **The two names must resolve to the SAME drive**, and an unqualified one
  means the **current** drive — not the other name's. A handler that
  resolves the new name against wherever the old one lives renames happily
  on the other drive, where DOS answers `11h`. It is `11h` whether or not
  the source is really there, which row 8 is for.
- **A path in the new name is a move**, and DOS does it. That is the shape an
  entry rewrite cannot make.

**This probe measured ITSELF twice before it measured DOS, and both times the
wrong answer looked like a finding about DOS.**

1. Rows 5 and 6 took their source from row 4's output, row 4 was always going
   to fail, and both reported *"file not found"* — an artefact of the fixture
   wearing the clothes of a result. Each row creates its own file now, which
   is the lesson `dosdir`'s find counts already carry.
2. Then the letters were computed as `'A' XOR 1` — which is **`@`**, not
   `B`. Three rows were naming a drive that does not exist, DOS answered
   **3, path not found**, and that read exactly like *"a cross-drive rename
   is 3 and not 11h"* — a plausible, quotable, entirely false finding about
   the call under test. The XOR belongs on the drive NUMBER, before `'A'` is
   added.

Both are the same shape: **a probe that is wrong produces a confident answer,
not an error.** The tell for the second one was that an earlier run with a
hard-coded `B:` had said `11h` for what looked like the same question, and
two measurements of "the same thing" disagreeing is worth more attention than
either of them.

It **writes**, so the disk it runs from must be writable, and it leaves its
files behind.

---

### `parsefcb.asm` — what `AH=29h` really answers

`AH=29h` (Parse Filename into FCB, SPEC.md §96.28) is a pure string-to-FCB
parse with no I/O, which makes it the easiest thing in §96 to get *nearly*
right and then ship wrong. This runs eleven inputs through it and prints, for
each, `AL`, `CF`, how far `SI` moved, the FCB's drive byte and the eleven name
bytes DOS wrote — spaces and all, so a field padded wrongly shows up as a
shifted column rather than as nothing.

**The carry is the column to look at first.** Unimplemented, the call fell to
the "invalid function" arm and answered `CF=1` with `AX=0001` — and DOS does
not use the carry for `29h` at all, on any row, the invalid drive included. A
program reading `AL`, which for this call every program does, was told its
plain name *had wildcards in it*.

**Four of the eleven rows decide how the handler is written, and none of them
is guessable from a reference:**

- `*.*` comes back as **eleven question marks**, not asterisks.
- `Z:` answers `FFh` **and still writes 26** into the drive byte.
- `B:Prince.exe` comes back **upper-cased** — an FCB matches a directory
  entry, and mixed case is what Prince's installer really passes.
- `A:\DIR\PRINCE.EXE` advances `SI` by **two** and leaves the name **blank**:
  `29h` parses a NAME, stops at the first separator, and the caller is
  expected to notice.

The last one is why the probe prints `SI+` at all. Everything else about that
row looks like success.

It runs under a real DOS unchanged. `tests/dosfcb.py` is the registered row
and its table is exactly what IBM DOS 3.30 answered here.

**One trap is in the probe's own source**, because it bit: `pad` walks a
string to its NUL decrementing a column counter, and a string LONGER than the
column wrapped that counter to 65,535 and printed that many spaces — which
scrolls the whole run off the screen and reads exactly like the program
crashing. Adding a row is what triggers it, which is the worst possible time.

---

### `drvname.asm` — does a drive letter in a NAME reach that drive?

A trace cannot answer this one, which is why it exists. `dos_fh_name` banks
the name it was given **after** stripping the prefix, so the DOSTRACE ring
records `*.*` whether the program wrote `*.*` or `B:\*.*` — and the question
here is precisely whether there was a letter to lose.

So it asks from the other side, by running the patterns. Stand on B:, with a
system disk in A: and no hard disk at all, and print for each shape:

```
*.*         AX=0000 CF=0 CUR=1 FOUND=DRVNAME.COM
A:*.*       AX=0000 CF=0 CUR=1 FOUND=COMMAND.COM      <- IBM DOS 3.30
A:*.*       AX=0000 CF=0 CUR=1 FOUND=DRVNAME.COM      <- this box, before
```

Three of those five columns matter and the obvious one matters least:

- **`FOUND`** is the name out of the DTA, so *"it found something"* and *"it
  found the right thing"* are different answers. This is the column that
  caught it: a search of A: came back with B:'s own directory, **reporting
  success**. Nothing else on the machine can see that — `AX=0000 CF=0` is
  what a correct search looks like too.
- **`CUR`** is `AH=19h` afterwards. A letter in a name must not move the
  program: under DOS it selects which drive's current directory the name is
  resolved against, and `AH=0Eh` alone changes drives. Getting the search
  right by *moving* is a second defect wearing the first one's fix, and only
  this column can tell them apart.
- **`AX`/`CF`** is the ordinary one, and it pinned an error code that had been
  guessed: a drive that is not there answers **3**, not 15, on `AH=4Eh`,
  `AH=3Dh` and `AH=3Bh` alike. SPEC.md §96.6 said `0Fh` and is corrected.

It runs under a real DOS unchanged, so the reference answer is a machine. See
SPEC.md §96.6.2 for the whole table and what it cost — Prince's `INSTALL.EXE`
stood on C:, asked for its source files by a name naming B:, was shown the
empty destination directory, and printed *"Please insert Prince of Persia Disk
in drive B:"*.

---

---

## Extending them

### Record another register in the ring

Both rings are 16 words an entry, and **three pieces of code must move
together**:

1. `apps/dos/dos.asm` — `dos_trace` (the way in) and `dos_tr_result` (the way
   out), and `DOS_TRACE_SZ` if the entry grows.
2. `tests/dostrap/trap.asm` — the same fields, in the same order, and `ENTSZ`.
3. `tools/os88dosdbg.py` — `FIELDS`, in that order.

The tool checks the first and third agree **on every use** and refuses rather
than decoding from inside the wrong entry. Growing the entry beyond 32 bytes
means keeping it a power of two: both rings mask rather than divide.

`dos_trace_dump` also writes `TRACE.LOG` on the guest's own disk, for a field
report from a machine with no debugger. Its line is built by hand; widen it too
or the file quietly loses the new column.

### Make the ring longer

`DOS_TRACEN` in `apps/dos/dos.asm` (a power of two) and `NENT` in
`trap.asm`. `DOS_TRDUMPN` is separate on purpose: the ring is read live off a
debugger, `TRACE.LOG` is what the field posts, and 512 entries of dump buffer
is 36KB of bss taken **out of the program's own arena** — which would change
the measurement the instrument exists to take.

A wrapped trace is reported as `WRAPPED` and the alignment is unreliable
against a reference that did not wrap; raise the ring rather than reason around
it.

### Watch another piece of state

`_dump_state` in `os88dosdbg.py` is four `m.read` calls. Anything the guest can
see, the host can: `dos_syms` gives you any bss offset by name, and
`os88geom.windows` finds the box's arena. Dump it as raw binary beside the
trace so `cmp` says everything.

### Add a probe

Copy `twoopen.asm`: build the name in (a double click passes no arguments),
print one line per case, and wait for a key. Keep it assembling with
`nasm -f bin` and nothing else — it has to run under a real DOS too.

---

## Traps, each of which cost real time

- **THE SITES REPEAT, SO INDEX ALIGNMENT LOCKS ONTO THE WRONG ITERATION.** The
  call site is the right key (see *Why the call site is the whole instrument*),
  and it is not enough on its own: a program that opens six sound files in a
  loop makes the same five calls from the same five addresses six times over,
  so two traces that are **out of step by a constant** still agree, site for
  site, for hundreds of calls before they visibly part. That is exactly what a
  reference trace is out of step by, because `ref` logs `COMMAND.COM`'s own
  calls before the program's and `trace` does not — 52 of them on IBM DOS 3.30.

  It cost a whole wrong conclusion this session: the two sides appeared to open
  **different files at the same site with the same registers**, which reads as
  a file-lookup defect, and it was two runs of one loop compared an iteration
  apart. The tell was that each side's next `LSEEK` was its own file's first
  header word — *both* were reading correctly.

  **The rule: align on a NAMED LANDMARK, not on an index.** Find the first call
  in each trace that opens the same file, take the difference of those two
  indices as the offset, and compare forward from there. Then check the
  segment bias is constant — `CS_real − CS_ours` is one number for the whole
  run once the offset is right, and drifts if it is not.

- **AND SET THE SAME ENVIRONMENT, OR THE TWO SIDES RUN DIFFERENT PROGRAMS.**
  `COMMAND.COM` hands out `COMSPEC=` and whatever the user set; the box hands
  out `BLASTER=` when it unloaded a sound card on the way in (SPEC.md 96.17),
  and since SPEC.md 96.44.13.1 a `PATH=` when it has nothing else to say. A
  program that picks a device off `BLASTER=` then opens **different files**,
  and a diff that does not control for it reports the program's own branch as a
  defect in the DOS underneath. `ref --set "BLASTER=A220 D1 T3"` is what makes
  the two comparable; `trace` gets the row from the machine.

- **THE HOST-SIDE MONITOR IS THE SAME INSTRUMENT ON BOTH SIDES, and it is the
  better one for a long run.** `tools/os88intmon.py` breakpoints `INT 21h` in
  MartyPC and does not care whose DOS is underneath, so it can be pointed at a
  real DOS booted in the emulator just as well as at ours. Against the
  guest-side ring it has no 512-call cap, takes no memory away from the program
  under test, and **records the NAME of every file opened**, which the ring
  cannot carry. What it costs is host wall-clock: ~100 calls a second, or ~28
  with `do_time` reading each answer back. Use the ring when the program must
  see an untouched machine; use the monitor when the question is *where do
  these two runs part*, which is most of the time.

- **THE TRACE DISK HAS A SMALLER ARENA THAN THE SHIPPED BOX, and a program that
  refuses under the tracer is refusing the TRACER.** This is first because it
  has cost **eight** separate wrong diagnoses, every one of them the same
  sentence — *"the program will not run, so the box is short of memory"* — about
  a program that launches perfectly on a plain build. A `DOSTRACE` package
  carries the ring and the rendered dump as an `OP_ASSET` part (SPEC.md
  §96.29.1), so **the part is claimed out of the heap before the arena is**, and
  the arena is `DOS_TRACE_KB` smaller than the figure any other disk would give.
  `python3 tools/os88dosdbg.py syms DOS_TRACE_KB` prints it — 21 KB today.

  **The rule: a refusal seen under the tracer is not a measurement until it has
  been reproduced without it.** Run the program off `build/os8088-360.img` (or
  whatever geometry you were on) with no `--build` and no `--kernel`, and only
  then say anything about memory. The box's own arena figure is on the Setup
  page and `[dos_akb]` is the word behind it, so the A/B takes one boot.

  The cost was written down in three places before this one — in `dos.asm` at
  the part, in SPEC.md §96.29.1, and in this file's own prose — and all three
  were read *after* the wrong conclusion had been reached, which is why
  `os88dosdbg.py` now **prints it on stderr at the top of every `trace` and
  `build`**. A line the run emits cannot be scrolled past the way a document
  can be left unopened.

- **A 720KB image in a 360KB drive.** Half the disk is unreachable, and the
  refusal reads as *end of file*. `os88fat.py reach` first, every time.
- **`make` overwriting the traced package.** `os88dosdbg.py build` orders it
  correctly and verifies; do not hand-roll the copy.
- **The image size is not a constant.** Every bss offset is measured from
  `os88_image_end`, so a `DOSTRACE` build's offsets differ from the shipped
  one's — and since SPEC.md 96.40.3 the shipped one is the parted package,
  whose `-DDOS_EXTCORE` moves them again by about 1,800 bytes. The tool reads
  the package **off the disk it is about to boot**; taking it from
  `$(SYSROOT)` gives a plausible number that is wrong by the difference, and
  the ring reads as empty.
- **`m.screen()` inside the bracket.** The program owns the adapter. Read the
  ring from memory.
- **A ring that wrapped.** 169 calls into a 64-entry ring threw away the first
  105 — which is where a divergence is, every time.
- **Comparing registers DOS leaves undefined.** See `AX_IS_AN_ANSWER`.
- **Believing our own trace.** It was correct four times in a row while the
  program was failing. The reference is the instrument.
- **Reading the PROGRAM's buffer from the host, after the fact.** A trace entry
  records where an output buffer *was*; the program has reused it by the time
  anything on the host looks. `AH=47h` was read back as answering an **empty**
  path, twice, and an afternoon went into why — the box was answering
  `\PRINCE` correctly all along, and a probe that copied the answer **in the
  guest, at the instant the slot returned**, said so in one run. Bank it where
  the program cannot reach, or do not quote it.
- **A probe that is big enough to be the experiment.** The ring above grew to
  8 KB of package bss to hold every parsed name, and the program then died
  after two INT 21h calls — a clean-looking result that was entirely the
  instrument. Keep a scratch ring to a few hundred bytes, and prefer recording
  only the **failures**: they are what you are looking for, they are rare, and
  they are not overwritten by whatever loop the program falls into afterwards.
- **A refusal whose cause is hundreds of calls upstream.** SPEC.md §96.4.1.1
  is the worked example: an `AH=3Dh open` answered "no such file" with every
  piece of state around it reading correct, because an earlier `AH=47h` had
  written a directory sector into the program's own memory. When the diff says
  the box is wrong at one instruction and the state at that instruction is
  right, the question is **who wrote to this program**, not what this call
  did.

---

## Where the contract lives

SPEC.md §96 is what the box promises; this file is how to find out where it
does not keep the promise. The sections most often reached from a trace:

| | |
|---|---|
| §96.7.1 | the gate banks `SI`, `DI`, `ES` — and what it cost not to |
| §96.9.1 | the allocator handed out the block it had just freed |
| §96.11.4 | an `int 13h` failure is **not** end of file — still open |
| §96.11.5 | the read window cannot outlive its file |
| §96.12.1.1 | `AH=4Eh`'s mask, and the volume label — still open |
| §96.12.1.2 | the measured table, and `dosref.asm` |
| §96.21.4 | the PSP fields a program reads without making a call |
| §96.22 | the calls a C runtime makes that a *program* never writes |
