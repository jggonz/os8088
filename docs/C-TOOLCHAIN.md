# Writing an os8088 package in C

os8088 is written in 8086 assembly and that is a choice, not an accident
(CLAUDE.md, PERFORMANCE.md). This document is about the **second** way to
write a package: a C compiler, a gate that makes its output safe here, and a
runtime that bridges the two. It is allowed to be slower and larger than
assembly in exchange for being writable.

**SPEC.md §70 is the contract.** It says what is true and why. This file says
what to type, in what order, and what to do when the build refuses you. If the
two ever disagree, §70 is right and this file is stale.

Read this before writing a line of C, and read
[`apps/cc/os88.h`](../apps/cc/os88.h) beside it — it is the API, and its
header comment is the short version of everything below.

---

## The one-paragraph summary

A C package is **one `.c` file** plus a **ten-line `.asm` shim**. SmallerC
compiles the C to NASM source; `tools/cc8086.py` rewrites the seven
instructions SmallerC emits that an 8086 cannot execute and **refuses four
constructs that would corrupt memory silently**; `nasm -f bin` assembles the
shim, the runtime and the compiled C into one flat image; and from there it is
the ordinary package pipeline, which cannot tell a C package from an assembly
one. There is **no linker** and there never will be — that is why SmallerC was
chosen over every other 16-bit C compiler.

The four rules the gate enforces, in the order you will meet them:

1. **Never take the address of an automatic.** Every addressable object is
   `static`.
2. **No string instructions**, which means no struct assignment, no struct by
   value, no struct return by value.
3. **No `long`, `float`, `double`, bit-field or anonymous union.**
4. **Frames stay under 96 bytes.**

Each has a section below with the exact error message and the fix.

---

## Installing the compiler

The compiler is **not in this tree** and never will be. `build/` is gitignored
and nothing under it is committed, so what is committed is a pinned commit
hash and `tools/cc8086.py`.

```
tools/setup-cc.sh
```

That fetches [SmallerC](https://github.com/alexfru/SmallerC) at commit
`1865d79ce7a5ad3f8a9515a571437cee084b8b1d` (2-clause BSD) into `build/cc/`,
builds three binaries there, and finishes by running a canary C file all the
way through the chain and assembling the result under `cpu 8086` — because
"the files are present" is not the same claim as "the toolchain works".

It needs `git` and a host C compiler (`xcode-select --install` on a Mac).
`tools/setup-macos.sh` offers to run it, and `--no-cc` declines.

Useful flags:

| | |
|---|---|
| `tools/setup-cc.sh --check` | report what is there, change nothing |
| `tools/setup-cc.sh --force` | discard `build/cc` and start over |
| `tools/setup-cc.sh --print-path` | print the compiler's directory |

It is idempotent — run it as often as you like. It re-fetches only when
`build/cc` is missing or is not at the pin, and rebuilds only when a binary is
missing or older than its source. **Re-pinning does not need a clean**: it
compares HEAD against `PIN` on every run.

**It builds the compiler with four of its limits raised**, through `CFLAGS`
and not a patch: `MAX_IDENT_TABLE_LEN`, `MAX_MACRO_TABLE_LEN`,
`SYNTAX_STACK_MAX` and `MAX_CASES` are all `#ifndef`-guarded in `smlrc.c`,
which is upstream saying they are meant to be set from outside. smlrc keeps
those tables in fixed-size arrays and stops when one fills — `Identifier table
exhausted`, naming no file, no line and no way forward — and a program the size
of `cword` (SPEC.md 70.12: about 5,000 lines in ONE translation unit, because
`nasm -f bin` has no notion of an external symbol) fills three of the four.
Nothing about the compiler's OUTPUT changes, so the pin stays a pin. They go
through `CFLAGS` because `CPPFLAGS` is `+=` in SmallerC's own makefile and
carries `-DPATH_PREFIX` and `-DHOST_MACOS`: a command-line `CPPFLAGS` would
REPLACE those and quietly build a compiler that cannot find its own include
directory. Changing the numbers needs `make clean-cc` first — the freshness
check compares source mtimes and cannot see a flag.

`make clean` deliberately **spares** `build/cc`. The compiler is a pinned
upstream instrument, not an output of this tree, and it arrives over the
network — a clean that threw it away would make the C targets need the network
to come back. `make clean-cc` removes it; `make distclean` calls that.

**Nothing in `make` depends on any of this.** A checkout with no compiler
builds every shipping floppy exactly as before and prints one note naming
`tools/setup-cc.sh`. Asking for a C target on such a tree gets that same one
line, not a failure inside a recipe.

---

## What a C package is made of

Two files, and a `make` rule that already exists.

### The `.c` file

One translation unit. `nasm -f bin` has no notion of an external symbol —
verified, it says `binary output format does not support external
references` — so **a C package cannot be several `.c` files linked
together**. If you need to split it, `#include` the parts into the one file;
`apps/cword/cword.c` does exactly that with three of them.

It starts with `#include "os88.h"` and defines `os88_main()` and
`os88_paint()`, plus one function per callback it wants:

```c
void *os88_main(void);                      /* always */
void  os88_paint(void *win);                /* always */
void  os88_onkey(int ascii, int scan, void *win);
void  os88_onclick(int x, int y, void *win);
void  os88_onmouseup(int x, int y, void *win);
void  os88_onresize(int w, int h, void *win);
void  os88_oncmd(int item, int menu, void *win);
void  os88_about(void *win);
void  os88_onfile(int mode, const char *name,
                  unsigned size_lo, unsigned size_hi, void *win);
void  os88_worker(void *win);               /* must never return */
```

`os88_main()` runs before your window exists and before your instance is
published. Create the window with `os88_wm_create()` and return it; return 0
to abort the launch. You may **not** draw there.

### The `.asm` shim

Ten lines, and `apps/cc/ccsmoke.asm` is the template. It does four things and
nothing else belongs in it:

```nasm
%define CC_PKG_NAME 'MYAPP'         ; <= 15 chars; the Disk window's label
%define CC_HAS_ONKEY                ; one line per callback the C defines
%define CC_HAS_ONCLICK
%include "cc/crt0.asm"              ; sections, header, trampolines, the API
%include "myapp.gen.asm"            ; the compiled C, found through -I build/
    CC_IMAGE_END                    ; the last thing in the file
```

The shim exists because the two halves have to agree about which callbacks are
real, and this is the cheapest place to say it. **The two cannot drift
silently**: a `%define` with no C function behind it is an nasm
external-reference error naming that function, and a C function with no
`%define` is code the kernel never calls.

The `%define`s available are `CC_HAS_ONKEY`, `CC_HAS_ONCLICK`,
`CC_HAS_ONMOUSEUP`, `CC_HAS_ONRESIZE`, `CC_HAS_MENUS`, `CC_HAS_ABOUT`,
`CC_HAS_FDLG`, `CC_HAS_WORKER` and `CC_HAS_ONWAKE` (`os88_onwake()`, the one
callback dispatched WITHOUT the gfx lock — SPEC.md 71.1: install it with
`os88_wm_onwake()`, post it with `os88_wm_wake()` from any context, and it
runs on the UI task where the file slots are legal, taking the lock itself for
whatever it draws; `apps/runcpm` is the worked example), plus `CC_ICON` to
name an embedded icon file. A trampoline you do not ask for is not assembled
at all.

### The `make` rule

`apps/cc/Makefile.inc` defines `CC_PACKAGE`. For a package at
`apps/myapp/myapp.c` + `apps/myapp/myapp.asm`, one line in the Makefile:

```make
$(eval $(call CC_PACKAGE,myapp,myapp))
```

gives you `build/myapp.raw.asm`, `build/myapp.gen.asm`, `build/myapp.bin` and
`build/myapp.o88`. Add a disk rule and a phony target beside the `cword` ones.

If your `.c` `#include`s other files, or your shim `%include`s an `.inc`,
**make cannot see through either** — say so explicitly, the way `cword` does:

```make
$(BUILD)/myapp.raw.asm: apps/myapp/mypart.c apps/myapp/mypart.h
$(BUILD)/myapp.bin:     apps/myapp/myasm.inc
```

Without those lines an edit to an included file leaves a stale `.o88` behind,
which reads exactly like a change that did nothing.

---

## Building and booting

```
make cc-smoke                          the SDK's smoke test  -> build/ccsmoke.img
make chello                            the capability gate   -> build/chello.img
make cword                             the application       -> build/cword.o88
make cworddisk                         ...and its three floppies
make test TESTAPPS=build/cword.img     boot it, B: = that floppy
make 386-c-word                        boot it on a period 386 in 86Box
```

Then double-click **Disk B**, then the package's icon.

The chain, if you want to run a step by hand:

```
apps/myapp/myapp.c
     | smlrcc -tiny -S                       (1) C -> NASM, but 80386
build/myapp.raw.asm
     | python3 tools/cc8086.py               (2) -> 8086, or REFUSE
build/myapp.gen.asm
     | nasm -f bin, from apps/myapp/myapp.asm
build/myapp.bin
     | python3 tools/os88pkg.py              (3) validate and stamp
build/myapp.o88
     | python3 tools/os88disk.py             (4) FAT12, three geometries
build/myapp.img
```

Steps 3 and 4 are the pipeline every assembly package already goes through,
unchanged and unaware that the source was C. Only the first two are new.

**`.raw.asm` and `.gen.asm` are not interchangeable.** `.raw.asm` is what the
compiler wrote — ungated 80386. `.gen.asm` is what passed the gate. The suffix
is the only thing distinguishing them, and only `.gen.asm` may be assembled.

---

## The four rules

These are not style advice. Each is a defect that **assembles cleanly, boots,
and misbehaves**, and each is refused by `tools/cc8086.py` with a message that
says what to write instead. §70.5, §70.5.1, §70.7 and §70.8 are the reasoning;
this is the practice.

### Rule 1 — never take the address of an automatic

**Why.** os8088 runs every task with `SS = LOW_SEG` and `DS =` your package's
segment. On the 8086 `[bp+disp]` addresses SS, so *reading and writing* a
local through `[bp+N]` is perfectly correct. What is wrong is the **address**:
`&x` compiles to `lea ax, [bp-2]`, a bare 16-bit offset with no segment
attached, and every later dereference of a C pointer is DS-relative. So the
program writes to the stack and reads out of its own image, at whatever offset
the frame happened to occupy — which is to say, depending on how deep the call
was, which is to say depending on what the user did.

**The message.**

```
build/myapp.raw.asm:412: error: `lea ax, [bp-8]` takes the address of an
automatic. os8088 runs every task with SS = LOW_SEG and DS = the package
segment, so a BP-relative offset is meaningless the moment anything
dereferences it through DS: ... Move the buffer to STATIC storage ...
```

**The fix: `static`.** A `static` declared *inside* a function is the idiom
and reads correctly — the scope is still the function, only the storage moved.

```c
static char line[80];                   /* fine: &line is a real address    */
void draw(void) { char tmp[80]; f(tmp); }        /* REFUSED                 */
void draw(void) { static char tmp[80]; f(tmp); } /* the fix                 */
```

**The four shapes that trip it, three of which do not look like an address:**

| C | why |
|---|---|
| `&x`, `f(buf)` on a local array | the obvious one |
| `buf[i]` on a **local** array with a variable index | `buf[0]` is a direct `mov [bp-10], al` and is safe; `buf[i]` is an address. Invisible in the source |
| an **out-parameter**: `struct os88_pt p; os88_wm_content(w, &p);` | this is the one that surprises people, and half the API is out-parameters |
| a local struct passed, assigned or returned **by value** | the compiler implements it as an address and a copy. See rule 2 |

**Two things a `static` costs you, because nothing will remind you:**

- **It is not re-entrant.** It is per package *instance*, not per call. A paint
  that triggers a paint of your second window shares it, and so do your UI task
  and your worker, which pre-empt each other. Where an assembly package would
  have used a stack buffer and been safe by construction, you must either own
  the aliasing deliberately or not share the buffer between the two contexts.
- **It is spent against the 60KB ceiling** rather than against the stack.

### Rule 2 — no string instructions; ES belongs to the kernel

**Why.** The 8086 string instructions address **ES:DI**, and every callback is
entered with `ES = KERNEL_SEG` because that is where the window record and the
file dialog's name live. Compiled C never loads ES. So a `rep stosb` clearing
what the C thought was a 200-byte buffer **overwrites 200 bytes of the
kernel**. It does not fault.

**The message.**

```
build/myapp.raw.asm:127: error: `rep movsb` addresses ES:DI, and a package
callback is entered with ES = KERNEL_SEG (SPEC.md 20). This does not fail, it
writes into the kernel. SmallerC emits it to copy a struct by value ...
```

**What this actually forbids in C.** `rep movsb` is how SmallerC copies a
struct by value, so:

```c
struct point p = q;         /* REFUSED - struct assignment          */
f(q);                       /* REFUSED - struct argument by value   */
return p;                   /* REFUSED - struct return by value     */
```

**The fix: pass pointers to statics.** Every prototype in `os88.h` that takes
a struct takes a pointer to one, for this reason. For arrays of structs, a
group stack or anything you would have copied element-wise, **parallel
arrays** are the idiom — `apps/cword/cwrtfio.c` does that and says so.

`lods` is **allowed**: it reads DS:SI, which is your own segment.

**Rules 1 and 2 are one defect with two symptoms**, and that is why they are
here together. A single line — `struct point p = q;` — trips both at once,
because forming the source address is the first half of the copy. A C author
who keeps every addressable buffer `static` and never copies a struct by value
meets neither.

**Note for anyone auditing the gate.** Bp-relative `lea` is *not* the only way
SmallerC forms a stack address: its struct-by-value argument helper does
`mov di, sp`. That helper always carries the `rep movsb` beside it, so this
rule catches what rule 1 misses. **The two rules are complete together and
neither is complete alone**, and that is a property of the pinned compiler
rather than a theorem — re-check it first if the pin ever moves.

**If you genuinely mean it**, write the assembly by hand in your shim, load ES
on purpose, restore it, and mark the line:

```nasm
    rep movsb                   ; cc8086:allow ES loaded above, restored below
```

Per-line, with a written reason, and greppable — which is a different kind of
thing from a build flag. `apps/cword/cwmove.inc` is the one place in the tree
that needs it: an editor's insert moves the tail of two arrays every keystroke,
which is a `memmove` the runtime does not have and a loop that has no business
being in C.

### Rule 3 — no `long`, no `float`, no `double`, no bit-fields

**`long` does not exist.** There is no 32-bit integer type in 16-bit mode and
the compiler will not parse the keyword: `Unexpected token long`.
`sizeof(int) == sizeof(void *) == 2`, full stop.

**The fix.** A byte count that can exceed 65,535, a file offset or a tick
accumulator is **two words**, which is the shape the kernel already uses
everywhere it needs 32 bits. `os88.h` does this for you where the API answers
in 32 bits: `os88_file_read()` hands you `size_lo` and `size_hi`, and
`os88_disk_free_kb()` does the arithmetic in assembly and answers in KB.

Read the constraint as a design input: **a feature whose natural unit is a
32-bit quantity is a feature to write in assembly.** `OSAPI_XMEM_*` is not
wrapped for C at all, for exactly this reason.

**`float` is worse than absent — it compiles.** A `float` global assembles to
`resb 2`, so `sizeof(float)` is **2**, and a program that declares one, assigns
to it and compares it will build and run and be silently wrong about every
value it ever held. Arithmetic on it calls `___addsf3`, which does not exist
here.

Two defences, and you need both:

- `os88.h` `#define`s `long`, `float` and `double` to an identifier that cannot
  parse, so the compiler names the problem:
  `Unexpected token OS88_ERROR_float_is_only_2_bytes_here__use_scaled_int`.
- `cc8086.py` refuses any reference to a soft-float helper.

**Know the limit of the second one: it fires on *arithmetic*.** A `float` that
is only declared, assigned and compared emits no helper call and there is no
assembly-level signature for it. If you defeat the `#define`, nothing else will
catch you. **Use scaled integers**, which is what every assembly package in
this tree already does.

**No bit-fields and no anonymous unions.** Both are parse errors. A packed
hardware layout or an on-disk header field is masks and shifts.

### Rule 4 — frames stay small

**Why.** There are two stacks, both small, neither growable, both measured
(§2.1):

| | size | deepest measured | headroom |
|---|---|---|---|
| the UI task, where every callback runs | 1,024 bytes | 246 | ~778 |
| a worker | 256 bytes | 150 | ~106 |

Those marks are from a 0xCC-fill probe under the hardest load the machine
takes, ISR frames included — the tick and mouse handlers run on whichever
stack they interrupt. A worker overrun is not silent (a canary halts the
machine) but a halt is still the machine stopping in front of the user.

**The message.**

```
build/myapp.raw.asm:88: error: frame of `_render` is 120 bytes; --max-frame
is 96. A worker task's whole stack is 256 bytes ... Move the big automatic to
static storage.
```

96 bytes is a **smell threshold**, not a budget: a 96-byte frame in this OS is
a local buffer, and rule 1 already says a local buffer is either refused or
should have been static. Lowering it for a package that runs on a worker is
normal; raising it is an argument to have in §70.8.

**The frame report is printed on every build and it is not noise.** The number
that actually matters is a *sum over a call chain*, the chain runs through
kernel dispatches no static analysis can see, and the only defence is an
author who notices the figures growing. Do not add `--quiet` to a build rule
to tidy the output. For scale: `cword` is 78 functions and its largest frame
is **24 bytes**.

---

## Debugging a refusal

Every finding is `<file>:<line>: error: …` against the **generated** assembly,
quoting the instruction and naming the fix. It does **not** name the C
function and it does not map back to a line of C — SmallerC emits no
source-line information, so there is nothing to map through.

**Finding the C is a two-step:**

1. Open `build/<pkg>.raw.asm` at the reported line.
2. Read **upwards** to the nearest `_name:` label. That is your C function,
   with SmallerC's leading underscore.

```
$ sed -n '405,412p' build/myapp.raw.asm      # look at the site
$ awk 'NR<=412 && /^_[A-Za-z_]/ {l=$0} END {print l}' build/myapp.raw.asm
_draw_status:
```

**The one case where that fails is the one that most needs explaining.** The
struct helpers are emitted at the foot of the file under bare `L<n>:` labels
with no C function above them. A `rep movsb` reported there names no caller at
all, and the answer is: *you passed, assigned or returned a struct by value
somewhere.* Grep your source for `struct` on the right of an `=`, in an
argument list, and after a `return`.

**One run tells you everything.** The tool gates, lowers, sweeps and reports
frames before printing anything, so you get every finding in the file at once
rather than the first. Gate findings — the ones whose fix is in the C — are
printed first.

**`OUT.asm` is not written at all if anything fails.** There is no partial
output for a later build step to pick up.

### Running the gate by hand

```
python3 tools/cc8086.py build/myapp.raw.asm -o /dev/null
python3 tools/cc8086.py build/myapp.raw.asm -o /dev/null --quiet   # offenders only
```

`--max-frame N` changes the cap. **`--no-gate` is not for a package that
ships**: it lowers the instruction set without enforcing any of the four
rules, and exists to measure a corpus that is never going to run here. It
prints the count of findings it suppressed so a run with it on can never be
mistaken for a clean one. **No build target that produces an `.o88` may pass
it.** `grep -n no-gate Makefile apps/cc/Makefile.inc` is the check: today it
finds three comment lines in `apps/cc/Makefile.inc` saying exactly that, and
no recipe. A hit in a recipe is the bug, not the package it produced.

### The refusals that are not one of the four rules

The lowering pass can also refuse a site because it cannot prove what it would
clobber. These are rare, they are honest, and each message says what to change:

| message | what to do |
|---|---|
| `push …: … none of AX/CX/DX/BX/SI/DI is provably dead here` | simplify the call — fewer or simpler arguments — or hoist the constant into a variable |
| `imul …: … FLAGS is live (or not provably dead) here` | split the expression so the multiply is not the last thing before a test |
| `imul …: multiplying by N needs a scratch register …` | make the multiplier a power of two (pad the struct or array element) or split the expression |
| `internal: … survived lowering — this is a cc8086 bug, not a source bug` | it says so: that is a bug in the tool, not in your C |

Over SmallerC's own 48,466-line corpus, AX is provably dead at **120 of 120**
push-immediate sites, so the first row is not something you should expect to
hit.

---

## What it costs: size

`APP_MAX_SIZE` is **0xF000 = 61,440 bytes** and it bounds the image, the bss
**and their sum**. `ld_check_hdr` enforces all three on every load and
`os88pkg.py` on every build.

**A C package meets that ceiling two to four times sooner than an assembly
one**, and that is not a fact to discover halfway through a program. Read it
as a design input: **a C package is a small package**, and a feature that wants
the whole 60KB is a feature to write in assembly.

What follows in practice:

- **No `printf` family.** SmallerC's own `printf` is thousands of bytes and
  drags in the whole formatting machinery for the one conversion you wanted.
  `os88_utoa()` and `os88_itoa()` are in the runtime; build strings with
  `os88_strcpy()`, which always terminates and whose cap counts the NUL.
- **The runtime you carry is small on purpose**: the header, the trampolines,
  the far-call bridges for the slots you use, and a handful of string and
  memory helpers. **Everything else comes from the API table and costs you no
  bytes at all** — which is the strongest argument for C here. A package that
  spends its code on *decisions* and reaches every primitive through the API
  is one whose bulk is exactly the part C is good at.
- **Bss is the cheap half.** `.bss` occupies zero file bytes and the loader
  zeroes it, so a static buffer is the right way to spend the ceiling. A large
  **initialised** array in `.data` is the wrong way — you pay for it twice,
  once on the floppy and once in the region.

`os88pkg.py` prints the numbers on every build:

```
os88pkg: 'CWORD' entry=+0x0020 image=37062 bss=22028 icon=no assoc=0
```

`cword` is 59,090 of 61,440 — **96%, with 2,350 spare** — and it is a
reimplementation of Microsoft Word 1.1a (SPEC.md 70.12), which does not fit in
one segment at all: another 9,430 bytes of its code are in `CWORD.OVL`. That is
what the next section is about.

**Track this from your first commit.** It is not a number to check at the end.

---

## When it does not fit: the overlay

**A module has a segment of its own, so it does not spend the package's**
(SPEC.md 70.14). Name a function `ovl_*` and its CODE is emitted into a section
that ships as `<NAME>.OVL` beside `<NAME>.O88`, read into a heap claim the
first time one of them is called:

```c
static int ovl_save_document(const char *name)   /* out there */
{
    return cw_write(name);                       /* back in here, by far call */
}
```

Four things follow, and only the last is a rule you have to remember:

- **Only code moves.** Every global, string literal and `.bss` byte the moved
  code names is still resident and still a plain `DS`-relative reference,
  because the module keeps `DS` = the package's segment. This is what makes the
  mechanism possible in C at all.
- **The calls are rewritten for you**, both ways, by `tools/cc8086.py`. A call
  to an `ovl_*` function becomes a far call preceded by a load-on-demand check;
  a call from one back to resident code becomes a far call through a shim.
- **A refused load is an ordinary path** (SPEC.md 47). No heap, a disk without
  the file, a stale module, or a **worker task** asking: the reason is toasted
  and the call returns 0 without happening. So write an overlay function that
  answers a status, and 0 means "it did not".
- **Do not take the address of one.** A function pointer here is a 16-bit
  offset with no segment, and a module offset means something else entirely in
  the package's segment. `cc8086.py` refuses it by name.

Two lines turn it on: `%define CC_HAS_OVL` in the shim, and the module's file
name as the third argument to `CC_PACKAGE` in the Makefile. Put the split where
the FREQUENCY divides rather than where the size does: everything a keystroke
touches stays resident, and everything a menu command touches can go out.
`tests/covl` (`make covl`) is the capability gate, and it is the smallest
complete example.

---

## What it costs: speed

**PERFORMANCE.md's numbers do not change because the source language did.**

| | cost on the target 4.77 MHz 8088 |
|---|---|
| any `gfx_*` drawing call, whatever it draws | **756 us** fixed |
| one 8×8 glyph cell | **~900 us** |
| one `OSAPI_*` far call (which is what one thunk adds) | 46.7 us + 11 us for the near call |

> **A redraw is priced by how many primitive calls it makes, not by how many
> pixels it covers.**

C neither reduces that count nor is charged differently for it. What C changes
is the loop *around* the calls, by a factor of **3–5×** against hand assembly.
Two things follow and both are binding:

- **Part 5 of PERFORMANCE.md applies to a C package unchanged.** A C package
  that reintroduces a full repaint is a regression against a documented
  number, and "it is written in C" is not a row in that table.
- **The inner loop is assembly.** C composes and decides; anything that touches
  pixels, cells or bytes *per iteration* is a hand-written proc that C calls
  once. `apps/os88type.inc` is the model: it composes a whole row of glyphs
  into a 1bpp band in the package's own RAM and emits it with **one**
  `gfx_blit1`, because lettering a 104-glyph line one glyph at a time is 79 ms
  of pure per-call floor.

Three further rules, restated where a C author will meet them:

1. **No C between `gfx_lock` and `gfx_unlock` that is not bounded by a count
   you can state.** The lock stops every other task from drawing; a worker
   holding it across a computation is a livelock the watchdog cannot break,
   and C makes every computation longer.
2. **Measure the way this project measures**: a counter in a primitive, one
   rebuild, multiplied by the table. Note what that does *not* see — the
   counters live in the drawing primitives, and C changes the multiplier on the
   *non*-drawing part of the frame, which is precisely the part no counter is
   watching. A C path that keeps its call count and still feels slow on
   hardware is the expected shape of this failure.
3. **Refusal is a normal path.** A C package that cannot do something in the
   time the target machine has greys the control and says which fact it tested.
   Being written in C is a reason to hit that path sooner, not a reason to ship
   the slow version.

And the three defects that are **invisible in an emulator** — a visible
redraw, a double-draw flash, and input overrun — are all *more* likely from C,
because the language makes the wasteful structure the natural one to write.
None will show in a screendump.

**`cword` is the worked example of taking this seriously.** A full repaint of
its 69×24 view is 29 calls and 1,682 cells — **1.5 seconds** on the target. It
shadows the glass, so a repaint compares each laid-out row against what is
already on the screen and draws only the columns that differ; and its relayout
stops the moment a row's new start equals its old start plus the insertion
delta, because from there down nothing changed. A keystroke at the end of the
document costs **3 calls and 1 cell**. The measurement comes from a host
harness that stubs the API with a model of the glass and counts, and it found
two real defects no screenshot would have shown.

---

## The API in C

`apps/os88api.inc` is the assembly SDK: 134 jump-table slots, each with a
**register** contract. C has no register contracts, so every slot a C package
can reach has a hand-written bridge in `apps/cc/os88thunk.asm` and a prototype
in `apps/cc/os88.h`. **`os88api.inc` stays the authority on what each slot does
and when it may be called**; `os88.h` is the shape it takes in C, and every
prototype there carries the SPEC section that owns it.

**109 C entry points**: most of the slots, plus the window-record accessors and
the string and memory helpers, which are not slots at all.

**You never dereference the window record.** It lives in `KERNEL_SEG`, and
`os88_win_w()` and its siblings do the `es:` override for you — and are correct
in any context, because they load `KERNEL_SEG` themselves rather than trusting
the ES they were entered with.

What is deliberately **not** wrapped, with the reason for each, is the full
list in `os88.h`'s header comment. The short version: the sound driver's
multi-verb protocols, `OSAPI_XMEM_*` (every argument is a 32-bit linear base —
the one part of the API C genuinely cannot hold), the fullscreen bracket, the
resumable Bresenham, the driver-fenced slots, and `OSAPI_WM_ONSIZE` (it must
answer in CX **and** DX, and a C function has one return value — write that
negotiator in your shim). Adding one of the rest is a dozen lines in
`os88thunk.asm`.

---

## Two worked examples to read

| | |
|---|---|
| **`apps/cc/ccsmoke.c`** + `.asm` | the SDK's template. Small and boring on purpose. `make cc-smoke` |
| **`tests/chello/chello.c`** | the capability gate — the first C program this OS ever ran. Written to make every part of the round trip visible in a screendump, including a crosshair at the click point, because a swapped x/y still counts up correctly and only the mark can tell you. `make chello` |
| **`apps/cword/cword.c`** | the application: a word processor, 1,876 lines, RTF in and out. Read its header comment for the redraw model and the cost table. `make cworddisk` |

Read `chello.c` before writing any C. It is short, and its header comment
walks the four rules against the actual code that obeys them.

---

## Things that will confuse you once

- **`-nopp` must not be passed.** It disables the preprocessor, and if your
  source has an `#include` the failure is a parse error a long way from the
  cause. The invocation in `apps/cc/Makefile.inc` is correct; do not "tidy" it.
- **A missing runtime shim is an nasm error, not a gate error.** SmallerC emits
  an `extern` for every symbol it did not define, and nasm accepts a redundant
  `extern` for a symbol defined in the same assembly. So the compiler's
  `extern` lines cost nothing — but a genuinely missing symbol becomes nasm's
  external-reference error **with the symbol's name in it**. This is the one
  failure in the C path that already names its own cause.
- **`os88pkg.py` reporting a size mismatch does not mean a truncated file.** On
  a C package it much more likely means the section layout was disturbed. A C
  package must not open a section of its own, and must not use `OS88_HEADER` /
  `OS88_IMAGE_END` — those compute `$-$$`, and `$$` is the start of the
  *current* section, so in a four-section file the header's image-size word
  becomes the size of one section. That is what `apps/cc/crt0.asm`'s pinned
  layout exists to make structurally impossible. §70.2 has the full account,
  including two further layout traps that reached a booting package.
- **`char` is signed** and `int` is 16 bits, so `char c = getch(); if (c == 200)`
  is never true. Use `unsigned char` where you mean a byte.
- **An edit that appears to do nothing.** Two causes, in this order. First,
  `make` cannot see through a `#include` or a `%include`, so if you did not
  declare the prerequisite (see "The `make` rule" above) the chain did not
  re-run and you are booting the previous `.o88`. Second, the standing trap in
  CLAUDE.md: a previous session's QEMU may still be answering on
  `build/qmp.sock` with the old image. Compare its start time against the
  `.o88`'s mtime before concluding anything.
