# Writing an os8088 package in C

os8088 is written in 8086 assembly by choice. This document is about the
second way to write a package: a C compiler, a gate that makes its output safe
here, and a runtime that bridges the two. A C package is allowed to be slower
and larger than an assembly one in exchange for being writable.

**SPEC.md §73 is the contract.** It says what is true and why. This file says
what to type, in what order, and what to do when the build refuses you. Where
the two disagree, §73 is right.

Read [`apps/cc/os88.h`](../apps/cc/os88.h) beside this — it is the API, and
its header comment is the short version of everything below.

---

## The one-paragraph summary

A C package is **one `.c` file** plus a **ten-line `.asm` shim**. SmallerC
compiles the C to NASM source; `tools/cc8086.py` rewrites the seven
instruction forms SmallerC emits that an 8086 cannot execute and **refuses four
constructs that would corrupt memory silently**; `nasm -f bin` assembles the
shim, the runtime and the compiled C into one flat image; and from there it is
the ordinary package pipeline, which cannot tell a C package from an assembly
one. There is **no linker** — that is why SmallerC, which emits NASM source
rather than an object file, was chosen.

The four rules the gate enforces:

1. **Never take the address of an automatic.** Every addressable object is
   `static`.
2. **No string instructions**, which means no struct assignment, no struct by
   value, no struct return by value.
3. **No `long`, `float`, `double`, bit-field or anonymous union.**
4. **Frames stay under 96 bytes.**

Each has a section below with the exact message and the fix.

---

## Installing the compiler

The compiler is **not in this tree**. `build/` is gitignored, so what is
committed is a pinned commit hash and `tools/cc8086.py`.

```
tools/setup-cc.sh
```

That fetches [SmallerC](https://github.com/alexfru/SmallerC) at commit
`1865d79ce7a5ad3f8a9515a571437cee084b8b1d` (2-clause BSD) into `build/cc/`,
builds `smlrc`, `smlrcc` and `smlrpp` there (the front end, the driver and the
preprocessor — not the linker, not the runtime libraries), and finishes by
running a canary C file through the whole chain and assembling the result under
`cpu 8086`, both as a stub-linked file and as a package (runtime in front of
the compiled C), because "the files are present" is not the same claim as "the
toolchain works".

It needs `git`, `make`, `nasm`, `python3`, a host C compiler (`CC=` names one;
`xcode-select --install` on a Mac) and a `readlink -f` that works (GNU
coreutils on a Mac older than 12.3). `tools/setup-macos.sh` offers to run it,
and `--no-cc` declines.

| | |
|---|---|
| `tools/setup-cc.sh --check` | report what is there, change nothing |
| `tools/setup-cc.sh --force` | discard `build/cc` and start over |
| `tools/setup-cc.sh --no-verify` | skip the closing canary |
| `tools/setup-cc.sh --print-path` | print the compiler's directory |

It is idempotent: it re-fetches only when `build/cc` is missing or not at the
pin, and rebuilds only when a binary is missing or older than its source.
Re-pinning needs no clean — it compares HEAD against `PIN` on every run.

**It builds the compiler with four of its table limits raised** through
`CFLAGS` (`MAX_IDENT_TABLE_LEN`, `MAX_MACRO_TABLE_LEN`, `SYNTAX_STACK_MAX`,
`MAX_CASES` — all `#ifndef`-guarded in `smlrc.c`). smlrc keeps those tables in
fixed-size arrays and stops with `Identifier table exhausted`, naming no file
and no line, and a program the size of `cword` (one translation unit of ~9,700
lines) fills three of the four. Nothing about the compiler's output changes.
Two things to know if you touch them: they go through `CFLAGS` and not
`CPPFLAGS` because a command-line `CPPFLAGS` would replace the `-DPATH_PREFIX`
SmallerC's own makefile adds and build a compiler that cannot find its include
directory; and changing the numbers needs `make clean-cc` first, because the
freshness check compares source mtimes and cannot see a flag.

`make clean` **spares** `build/cc` — the compiler is a pinned instrument that
arrives over the network. `make clean-cc` removes it; `make distclean` calls
that.

**Nothing in `make` depends on any of this.** A checkout with no compiler
builds every shipping floppy exactly as before and prints one note naming
`tools/setup-cc.sh`. Asking for a C target automatically runs that setup
script when any compiler binary is missing, then continues the build. The
first run needs network access, Git and a host C compiler. An alternate
`BUILD` directory gets its own compiler under `$(BUILD)/cc`; the setup script
also accepts `--build-dir DIR` for explicit setup.

---

## What a C package is made of

Two files, and a `make` rule that already exists.

### The `.c` file

One translation unit. `nasm -f bin` refuses an external reference (`binary
output format does not support external references`), so **a C package cannot
be several `.c` files linked together**. To split it, `#include` the parts into
the one file; `apps/cword/cword.c` does that with eight of them.

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
void  os88_ontimer(void *win);              /* one-shot; re-arm to repeat */
int   os88_onclose(void *win);              /* non-zero = let it close */
void  os88_onwake(void *win);               /* runs WITHOUT the gfx lock */
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
real. **The two cannot drift silently**: a `%define` with no C function behind
it is an nasm error naming that function, and a C function with no `%define`
is code the kernel never calls.

The `%define`s: `CC_HAS_ONKEY`, `CC_HAS_ONCLICK`, `CC_HAS_ONMOUSEUP`,
`CC_HAS_ONRESIZE`, `CC_HAS_MENUS`, `CC_HAS_ABOUT`, `CC_HAS_FDLG`,
`CC_HAS_WORKER`, `CC_HAS_ONTIMER`, `CC_HAS_ONCLOSE` (§75.1), `CC_HAS_ONWAKE`
(§74.1: `os88_onwake()` is the one callback dispatched without the gfx lock —
install it with `os88_wm_onwake()`, post it with `os88_wm_wake()` from any
context, and it runs on the UI task where the file slots are legal, taking the
lock itself for whatever it draws; `apps/runcpm` is the worked example),
`CC_HAS_OVL` (the overlay, below), `CC_HAS_PARTS` (embedded parts, §20.12),
`CC_STACK_CLASS` (the worker's stack class, an `OS88_STACK_*` from the SDK,
§8.7), plus `CC_ICON` to name an embedded icon file and `CC_ASSOC` to name an
association file. A trampoline you do not ask for is not assembled at all.

`CC_ASSOC` is the C half of §54.6: the file it names holds a count byte and up
to five `OS88_ASSOC_EXT` lines, as an assembly package writes them
(`apps/frotz/frotz.asm` is the example), and `crt0.asm` brackets it with
`OS88_ASSOC16` / `OS88_ASSOC16_END` so the offset assertions and the pad to 16
bytes cannot be forgotten. Icon (flags bit 0) and declaration (bit 1) are
independent, so an iconless package can declare extensions. Declaring costs
nothing at run time: the mount's icon harvest already reads that sector, so a
document beside your program opens on the first double-click.

### The `make` rule

`apps/cc/Makefile.inc` defines `CC_PACKAGE`. For a package at
`apps/myapp/myapp.c` + `apps/myapp/myapp.asm`, one line in the Makefile:

```make
$(eval $(call CC_PACKAGE,myapp,myapp))
```

gives you `build/myapp.raw.asm`, `build/myapp.gen.asm`, `build/myapp.bin` and
`build/myapp.o88`. A third argument names an overlay file (`CWORD.OVL`, below)
and a fourth lists embedded part files (§20.12). Add a disk rule and a phony
target beside the `cword` ones.

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
make chello                            the capability gate   -> build/chello.img, chello360.img
make covl                              the overlay gate      -> build/covl.img, covl360.img
make cword                             the application       -> build/cword.o88 + CWORD.OVL
make cworddisk                         ...and its floppy in all four geometries
make test TESTAPPS=build/cword.img     boot it in QEMU, B: = that floppy
make 386-c-word                        boot it on a period 386 in 86Box
```

Then double-click **Disk B**, then the package's icon.

The suite's `ctoolchain` row (`python3 tools/os88test.py full`,
`tests/unit/t_ctoolchain.py`) builds `ccsmoke`, `chello`, `covl` and `cword`
from clean and checks that a call to an `os88_*` with no thunk still stops the
build naming the function. It skips where the compiler is not built. It is in
`full` and not `fast` because `fast` hangs off `make`, and `make` builds no C.

The chain, if you want to run a step by hand:

```
apps/myapp/myapp.c
     | smlrcc -tiny -S                       (1) C -> NASM, but 80386
build/myapp.raw.asm
     | python3 tools/cc8086.py               (2) -> 8086, or REFUSE
build/myapp.gen.asm
     | nasm -f bin, from apps/myapp/myapp.asm (3) one assembly, no linker
build/myapp.bin
     | python3 tools/os88pkg.py              (4) validate and stamp
build/myapp.o88
     | python3 tools/os88disk.py             (5) FAT12, four geometries
build/myapp.img
```

Steps 3 to 5 are the pipeline every assembly package goes through, unchanged
and unaware that the source was C. Only the first two are new. `setup-cc.sh`
prints the exact `smlrcc` command line at the end of a successful run.

**`.raw.asm` and `.gen.asm` are not interchangeable.** `.raw.asm` is what the
compiler wrote — ungated 80386. `.gen.asm` is what passed the gate, and it is
the only one that may be assembled.

---

## The four rules

These are not style advice. Each is a defect that **assembles cleanly, boots,
and misbehaves**, and each is refused by `tools/cc8086.py` with a message that
says what to write instead. §73.5, §73.5.1, §73.7 and §73.8 are the reasoning;
this is the practice.

### Rule 1 — never take the address of an automatic

**Why.** Every task runs with `SS = LOW_SEG` and `DS =` your package's
segment. On the 8086 `[bp+disp]` addresses SS, so *reading and writing* a
local through `[bp+N]` is correct. What is wrong is the **address**: `&x`
compiles to `lea ax, [bp-2]`, a bare 16-bit offset, and every later
dereference of a C pointer is DS-relative. So the program writes to the stack
and reads out of its own image, at whatever offset the frame happened to occupy.

**The message.**

```
build/myapp.raw.asm:412: error: `lea ax, [bp-80]` takes the address of an
automatic. os8088 runs every task with SS = LOW_SEG and DS = the package
segment, so a BP-relative offset is meaningless the moment anything
dereferences it through DS: ... Move the buffer to STATIC storage ...
```

**The fix: `static`.** A `static` declared *inside* a function is the idiom —
the scope is still the function, only the storage moved.

```c
static char line[80];                   /* fine: &line is a real address    */
void draw(void) { char tmp[80]; f(tmp); }        /* REFUSED                 */
void draw(void) { static char tmp[80]; f(tmp); } /* the fix                 */
```

**The four shapes that trip it, three of which do not look like an address:**

| C | why |
|---|---|
| `&x`, `f(buf)` on a local array | the obvious one |
| `buf[i]` on a **local** array with a variable index | `buf[0]` is a direct `mov [bp-10], al`; `buf[i]` forms the address. Invisible in the source |
| an **out-parameter**: `struct os88_pt p; os88_wm_content(w, &p);` | the one that surprises people, and half the API is out-parameters |
| a local struct passed, assigned or returned **by value** | the compiler implements it as an address and a copy. See rule 2 |

**Two things a `static` costs you:**

- **It is not re-entrant.** It is per package *instance*, not per call. A paint
  that triggers a paint of your second window shares it, and so do your UI
  task and your worker, which pre-empt each other. Own the aliasing
  deliberately or do not share the buffer between the two contexts.
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

**What this forbids in C.** `rep movsb` is how SmallerC copies a struct by
value, so:

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

**Rules 1 and 2 are one defect with two symptoms**: `struct point p = q;`
trips both, because forming the source address is the first half of the copy.
Bp-relative `lea` is not the only way SmallerC forms a stack address — its
struct-by-value argument helper does `mov di, sp` — but that helper always
carries the `rep movsb` beside it, so rule 2 catches what rule 1 misses. That
is a property of the pinned compiler; re-check it if the pin moves.

**If you genuinely mean it**, write the routine by hand, load ES on purpose,
restore it, and mark the line:

```nasm
    rep movsb                   ; cc8086:allow ES loaded above, restored below
```

Where the line lives decides what the marker does. **The gate reads only the
compiled C** — `build/<pkg>.raw.asm`, which includes any inline asm in the
`.c` — so there the marker is what lets the line through. A routine in an
`.inc` that the shim `%include`s never passes through the gate at all; the
marker there is the reviewer's convention and nothing enforces it.
`apps/runcpm/rcmem.inc` and `apps/loom/lmui.inc` are marked that way;
`apps/cword/cwmove.inc` (an editor's insert moves the tail of two arrays every
keystroke — a `memmove` the runtime does not have) is the same shape and says
in prose what it does with ES.

### Rule 3 — no `long`, no `float`, no `double`, no bit-fields

**`long` does not exist.** There is no 32-bit integer type in 16-bit mode and
the compiler will not parse the keyword: `Unexpected token long` (or, with
`os88.h` included, `Unexpected token
OS88_ERROR_no_32_bit_int_in_16_bit_mode__use_two_words`).
`sizeof(int) == sizeof(void *) == 2`.

**The fix.** A byte count that can exceed 65,535, a file offset or a tick
accumulator is **two words**, which is the shape the kernel already uses.
`os88.h` does this where the API answers in 32 bits: `os88_file_read()` hands
you `size_lo` and `size_hi`, and `os88_disk_free_kb()` does the arithmetic in
assembly and answers in KB. **A feature whose natural unit is a 32-bit
quantity is a feature to write in assembly** — `OSAPI_XMEM_*` is not wrapped
for C at all, for exactly this reason.

**`float` is worse than absent — it compiles.** A `float` global assembles to
`resb 2`, so `sizeof(float)` is **2**, and a program that declares one and
assigns to it builds and runs and is silently wrong about every value it ever
held. Arithmetic and comparison call `___addsf3`, `___gesf2` and friends,
which do not exist here.

Two defences, and you need both:

- `os88.h` `#define`s `long`, `float` and `double` to an identifier that cannot
  parse, so the compiler names the problem:
  `Unexpected token OS88_ERROR_float_is_only_2_bytes_here__use_scaled_int`.
- `cc8086.py` refuses any reference to a soft-float helper:
  ``error: `call ___addsf3` references the floating-point helper `__addsf3`.
  SmallerC compiles `float` as a SILENT 2-byte type and calls helpers that are
  not on this floppy. There is no floating point here: use scaled integers.``

**The second one fires on arithmetic and comparison.** A `float` that is only
declared, assigned and copied emits no helper call, and there is no
assembly-level signature for it. If you defeat the `#define`, nothing else
will catch you. **Use scaled integers**, which is what every assembly package
in this tree does.

**No bit-fields and no anonymous unions.** Both are parse errors (`Unexpected
token :` and `Identifier expected in declaration`). A packed hardware layout
or an on-disk header field is masks and shifts.

### Rule 4 — frames stay small

**Why.** There are two stacks, both small, neither growable, both measured
(§2.1, §8.7):

| | size | deepest measured |
|---|---|---|
| the UI task (`STK0_SIZE`), where every callback runs | 512 bytes | 246 |
| a worker (`SCH_STACK`, the largest slot class) | 384 bytes | 150 |

A worker is given the smallest free slice at least as big as the class it
asked for (`CC_STACK_CLASS`; 128, 192, 256 or 384), which can be bigger than
it asked for but never smaller. A worker overrun is not silent (a canary halts
the machine) but a halt is still the machine stopping in front of the user.

**The message.**

```
build/myapp.raw.asm:88: error: frame of `_render` is 122 bytes; --max-frame
is 96. A worker task's whole stack is 384 bytes (kernel/sched.inc, SCH_STACK)
... Move the big automatic to static storage.
```

96 bytes is a **smell threshold**, not a budget: a 96-byte frame in this OS is
a local buffer, and rule 1 already says a local buffer is either refused or
should have been static. Lowering it (`CC_MAXFRAME=` in the make rule,
`--max-frame` on the tool) for a package that runs on a worker is normal;
raising it is an argument to have in §73.8.

**The frame report is printed on every build and it is not noise.** The number
that actually matters is a *sum over a call chain*, the chain runs through
kernel dispatches no static analysis can see, and the only defence is an
author who notices the figures growing. Do not add `--quiet` to a build rule
to tidy the output. For scale: `cword` is 159 functions and its largest frame
is **26 bytes**.

---

## Debugging a refusal

Every finding is `<file>:<line>: error: …` against the **generated** assembly,
quoting the instruction and naming the fix. It does **not** name the C
function and it does not map back to a line of C — SmallerC emits no
source-line information.

**Finding the C is a two-step:**

1. Open `build/<pkg>.raw.asm` at the reported line.
2. Read **upwards** to the nearest `_name:` label. That is your C function,
   with SmallerC's leading underscore.

```
$ sed -n '405,412p' build/myapp.raw.asm      # look at the site
$ awk 'NR<=412 && /^_[A-Za-z_]/ {l=$0} END {print l}' build/myapp.raw.asm
_draw_status:
```

**The one case where that fails:** the struct helpers are emitted at the foot
of the file under bare `L<n>:` labels with no C function above them. A
`rep movsb` reported there names no caller, and the answer is: *you passed,
assigned or returned a struct by value somewhere.* Grep your source for
`struct` on the right of an `=`, in an argument list, and after a `return`.

**One run tells you everything.** The tool gates, lowers, sweeps and reports
frames before printing anything, so you get every finding in the file at once.
Gate findings — the ones whose fix is in the C — are printed first.

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
it.** `grep -n no-gate Makefile apps/cc/Makefile.inc` is the check: it finds
three comment lines in `apps/cc/Makefile.inc` and no recipe. A hit in a
recipe is the bug.

### The refusals that are not one of the four rules

The lowering pass can also refuse a site because it cannot prove what it would
clobber. These are rare, and each message says what to change:

| message | what to do |
|---|---|
| `push …: … none of AX/CX/DX/BX/SI/DI is provably dead here` | simplify the call — fewer or simpler arguments — or hoist the constant into a variable |
| `imul …: … FLAGS is live (or not provably dead) here` | split the expression so the multiply is not the last thing before a test |
| `imul …: multiplying by N needs a scratch register …` | make the multiplier a power of two (pad the struct or array element) or split the expression |
| `… is an 80186 instruction and cc8086 has no lowering for it` | an inline-asm `pusha`, `enter`, `bound` and the like — write the 8086 form |
| `internal: … survived lowering — this is a cc8086 bug, not a source bug` | a bug in the tool, not in your C |

Over SmallerC's own 48,466-line corpus AX is provably dead at 120 of 120
push-immediate sites, so the first row is not something to expect.

---

## What it costs: size

`APP_MAX_SIZE` is **0xF000 = 61,440 bytes** and it bounds the image, the bss
**and their sum**. `ld_check_hdr` enforces all three on every load and
`os88pkg.py` on every build.

**A C package meets that ceiling two to four times sooner than an assembly
one.** Read it as a design input: **a C package is a small package**, and a
feature that wants the whole 60KB is a feature to write in assembly.

What follows in practice:

- **No `printf` family.** `os88_utoa()` and `os88_itoa()` are in the runtime;
  build strings with `os88_strcpy()`, which always terminates and whose cap
  counts the NUL.
- **The runtime you carry is small on purpose**: the header, the trampolines,
  the far-call bridges for the slots you use, and a handful of string and
  memory helpers. **Everything else comes from the API table and costs you no
  bytes at all** — a package that spends its code on *decisions* and reaches
  every primitive through the API is one whose bulk is exactly the part C is
  good at.
- **Bss is the cheap half.** `.bss` occupies zero file bytes and the loader
  zeroes it, so a static buffer is the right way to spend the ceiling. A large
  **initialised** array in `.data` is paid for twice, once on the floppy and
  once in the region.

`os88pkg.py` prints the numbers on every build:

```
os88pkg: 'CWORD' entry=+0x0020 image=36326 bss=24533 icon=no assoc=0 -> build/cword.o88
```

`cword` is 60,859 of 61,440 — **99%, with 581 spare** — and another 18,565
bytes of its code are in `CWORD.OVL`. That is what the next section is about.

**Track this from your first commit**, not at the end.

---

## When it does not fit: the overlay

**A module has a segment of its own, so it does not spend the package's**
(§73.14). Name a function `ovl_*` and its CODE is emitted into a section
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
  because the module keeps `DS` = the package's segment. This is what makes
  the mechanism possible in C at all.
- **The calls are rewritten for you**, both ways, by `tools/cc8086.py`. A call
  to an `ovl_*` function becomes a far call preceded by a load-on-demand check;
  a call from one back to resident code becomes a far call through a shim.
- **A refused load is an ordinary path** (§47). No heap, a disk without the
  file, a stale module, or a **worker task** asking (§20.6 rule 7 forbids the
  file I/O there): the reason is toasted and the call returns 0 without
  happening. So write an overlay function that answers a status, and 0 means
  "it did not".
- **Do not take the address of one.** A function pointer here is a 16-bit
  offset with no segment, and a module offset means something else entirely in
  the package's segment. `cc8086.py` refuses it by name: ``error: the address
  of the overlay function `ovl_thing` is taken here (`mov ax, _ovl_thing`)
  ... Call it directly, or move it back out of the overlay by renaming it.``

Two lines turn it on: `%define CC_HAS_OVL` in the shim, and the module's file
name as the third argument to `CC_PACKAGE` in the Makefile
(`$(eval $(call CC_PACKAGE,cword,cword,CWORD.OVL))`). `tools/os88ovl.py` then
cuts the assembled image at the size its own header carries and writes the tail
as the `.OVL`. Put the split where the FREQUENCY divides rather than where the
size does: everything a keystroke touches stays resident, and everything a
menu command touches can go out. `tests/covl` (`make covl`) is the capability
gate and the smallest complete example.

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
  once. `apps/os88type.inc` (§6.3, §6.5) is the model: it composes a whole row
  of glyphs into a 1bpp band in the package's own RAM and emits it with **one**
  `gfx_blit1`, because lettering a 104-glyph line one glyph at a time is 79 ms
  of pure per-call floor.

Three further rules, restated where a C author will meet them:

1. **No C between `gfx_lock` and `gfx_unlock` that is not bounded by a count
   you can state.** The lock stops every other task from drawing, and C makes
   every computation longer.
2. **Measure the way this project measures** — a counter in a primitive, one
   rebuild, multiplied by the table — and note what that does *not* see: the
   counters live in the drawing primitives, and C changes the multiplier on
   the *non*-drawing part of the frame. A C path that keeps its call count and
   still feels slow on hardware is the expected shape of this failure.
3. **Refusal is a normal path** (§47). Grey the control and say which fact was
   tested.

The three defects invisible in an emulator — a visible redraw, a double-draw
flash, input overrun — are all *more* likely from C, because the language
makes the wasteful structure the natural one to write.

**`cword` is the worked example of taking this seriously.** A full repaint of
its 71×24 view is 132 calls and 1,899 cells — **1.8 seconds** on the target.
It shadows the glass, so a repaint compares each laid-out row against what is
already on the screen and draws only the columns that differ; and its relayout
stops the moment a row's new start equals its old start plus the insertion
delta. An ordinary keystroke costs **5 calls and 8 cells**. The numbers come
from `apps/cword/hosttest/cwuitest.c`, a host harness that stubs the API with
a model of the glass and counts; it found three real defects no screenshot
would have shown.

---

## The API in C

`apps/os88api.inc` is the assembly SDK: 155 jump-table slots, each with a
**register** contract. C has no register contracts, so every slot a C package
can reach has a hand-written bridge in `apps/cc/os88thunk.asm` and a prototype
in `apps/cc/os88.h`. **`os88api.inc` stays the authority on what each slot does
and when it may be called**; `os88.h` is the shape it takes in C, and every
prototype there carries the SPEC section that owns it.

**125 C entry points**: 97 of the slots, plus the window-record accessors and
the string and memory helpers, which are not slots at all.

**You never dereference the window record.** It lives in `KERNEL_SEG`, and
`os88_win_w()` and its siblings do the `es:` override for you — and are correct
in any context, because they load `KERNEL_SEG` themselves rather than trusting
the ES they were entered with.

### Letting the compactor move a claim

`os88_mem_claim()` was the whole of the C SDK's heap surface until §66.4.1's
work: there was no `os88_mem_movable`, so **every claim a C package made was
pinned by construction and no author could change it** — C64's 64KB of RAM,
RunCPM's 64KB Z80 space, Weave's bundle and canvas, Loom's project buffers,
each one a wall in the middle of the arena for as long as the program ran.

It is two lines now, and one of them goes in the shim:

```
%define CC_HAS_ONMOVE                       ; in your .asm

static unsigned my_seg;
void os88_onmove(unsigned was, unsigned now)
{   if (my_seg == was) my_seg = now;   }

my_seg = os88_mem_claim(32);
if (os88_mem_movable(my_seg, 1) != 0) { /* refused: it stays pinned */ }
```

**`os88_onmove` is not a window callback**, and the difference is the whole of
what can go wrong with it. It is dispatched from inside `mem_reloc_call`, in
the middle of the compaction, on whatever task asked for the memory that could
not be found — so §66.3 rule 3 binds the C on the other side of it: **no
claim, no free, no yield, no drawing and no file call**, because each of those
re-enters the walk that is calling you. Assignments and arithmetic only.

Three more things, each of which has cost this project a defect:

- **Fix every word that names the block**, not the first one you think of. A
  cached base, a "current" pointer, a scratch you handed a library — all stale
  the instant the handler returns. §66.1 is the record of a design that failed
  on exactly that.
- **Pin it around a file call.** If the block is the `ES:BX` of an
  `os88_file_read()`/`write()`, `os88_mem_movable(seg, 0)` first and declare it
  again after (§66.9 reason 4): a file call claims, so a compaction inside one
  moves the buffer out from under a transfer the kernel already has the address
  of.
- **Take the answer.** `mem_movable`'s fence is *yours, or not at all*, so a
  segment you do not hold matches nothing, writes no `MC_RLOC` and returns
  refused — and from inside the package that is indistinguishable from success.
  §66.5.6.2 is what that cost once.

`tests/chello` is the worked example and `tests/cmemmove.py` the gate: it puts
CHELLO above a `tests/heapfrag` that then frees the floor, and reads the move
back out of the kernel's own table and CHELLO's own statics. The round trip it
proves is longer than any other callback's — C, thunk, kernel,
`cc_onmove`, C — and the assertion that earns its keep is that `was` and `now`
did not arrive swapped, because a swapped pair counts the same moves and reads
just as plausibly.

What is deliberately **not** wrapped, with the reason for each, is the list in
`os88.h`'s header comment. The short version: the sound driver's multi-verb
protocols, `OSAPI_XMEM_*` (every argument is a 32-bit linear base — the one
part of the API C genuinely cannot hold), the fsx bracket, the resumable
Bresenham, the driver-fenced slots, and `OSAPI_WM_ONSIZE` (it must answer in
CX **and** DX, and a C function has one return value — write that negotiator
in your shim). Adding one of the rest is a dozen lines in `os88thunk.asm`.

---

## Worked examples to read

| | |
|---|---|
| **`apps/cc/ccsmoke.c`** + `.asm` | the SDK's template. Small and boring on purpose. `make cc-smoke` |
| **`tests/chello/chello.c`** | the capability gate — the first C program this OS ran. Written to make every part of the round trip visible in a screendump, including a crosshair at the click point, because a swapped x/y still counts up correctly and only the mark can tell you. It also gates `os88_mem_movable()` and draws the claim's live base beside where it came from, for the same reason. `make chello` |
| **`tests/covl/covl.c`** | the overlay gate — argument offsets across a far call, a call back out of the module, a call inside it. `make covl` |
| **`apps/cword/cword.c`** | the application: a word processor, ~9,700 lines across nine files, RTF in and out, in two segments. Read its header comment for the redraw model and the cost table. `make cworddisk` |

`apps/runcpm`, `apps/c64`, `apps/weave` and `apps/loom` are the other C
packages in the tree — larger, and each with its own SPEC section.

Read `chello.c` before writing any C. It is short, and its header comment
walks the four rules against the actual code that obeys them.

---

## Things that will confuse you once

- **`-nopp` must not be passed.** It disables the preprocessor, and if your
  source has an `#include` the failure is a parse error a long way from the
  cause. The invocation in `apps/cc/Makefile.inc` is correct.
- **A missing runtime shim is an nasm error, not a gate error.** SmallerC emits
  an `extern` for every symbol it did not define — every `os88_*` call you
  make — and `tools/cc8086.py` comments those lines out, so a symbol the SDK
  defines resolves in the ordinary way and a missing one becomes ``symbol
  `_os88_foo' not defined`` **with the name in it**. nasm does *not* tolerate a
  redundant `extern` for a symbol defined in the same assembly: it is a
  redefinition, and passes only when the symbol's value is 0 (§73.1).
- **`os88pkg.py` reporting `image size field N != actual file size M` does
  not mean a truncated file.** On a C package it much more likely means the
  section layout was disturbed. A C package must not open a section of its own,
  and must not use `OS88_HEADER` / `OS88_IMAGE_END` — those compute `$-$$`,
  and `$$` is the start of the *current* section, so in a four-section file the
  header's image-size word becomes the size of one section. `apps/cc/crt0.asm`'s
  pinned layout exists to make that structurally impossible. §73.2 has the
  account, including two further layout traps.
- **`char` is signed** and `int` is 16 bits, so `char c = getch(); if (c == 200)`
  is never true. Use `unsigned char` where you mean a byte.
- **An edit that appears to do nothing.** Two causes, in this order. First,
  `make` cannot see through a `#include` or a `%include`, so if you did not
  declare the prerequisite (see "The `make` rule" above) the chain did not
  re-run and you are booting the previous `.o88`. Second, the standing trap in
  CLAUDE.md: a previous session's QEMU may still be answering on
  `build/qmp.sock` with the old image. Compare its start time against the
  `.o88`'s mtime before concluding anything.
