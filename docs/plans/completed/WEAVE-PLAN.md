# Weave & Loom — the design record

**Design record, not a contract.** `docs/WEAVE-SPEC.md` is the binding
contract for what the family *is* — every byte offset, opcode and refusal
sentence is pinned there and changes there first. This file is the account of
**why**: the design panel that produced the architecture, every fork that was
decided and the arithmetic that decided it, the platform facts that shaped
the whole shape, and what was deliberately deferred. It exists so the next
person does not re-fight a settled argument without the numbers that settled
it — and so the one argument that *looks* attractive and is broken (§1.2)
comes with its two verified flaws attached.

Citation convention follows WEAVE-SPEC: `SPEC.md §N` is the kernel contract,
`WEAVE-SPEC §N` is the family's, and this file's own sections are cited from
outside as `WEAVE-PLAN §N`. Every load-bearing number below carries its
source; two figures live in the tree in stale form (SPEC.md §61.4's heap
numbers, `apps/cc/os88.h`'s slot count) and are quoted here only alongside
the correction.

---

## 1. The design panel, and why Weave & Loom won

The family was designed by panel: three architectures written independently
to one brief (web-style apps developed and run in the OS, a spreadsheet and
a game demonstrably buildable, usable at 4.77 MHz), each grounded in the
same seven verified subsystem studies, then judged three times through three
different lenses — performance and memory, shippability and platform fit,
user and product. The ranking came back **unanimous: Weave & Loom first,
WAP second, LOOM third.**

### 1.1 The three candidates and what each staked

**LOOM — the platform-hosting design.** One resident runtime hosting
*several* apps: one VM core round-robining among bundles, one copy of the
runtime on the machine, the IDE itself a privileged bundle (`STUDIO.WAB`)
running *on* the runtime, CSS-like styles, the richest component set, and a
pack-time layout that emitted final rects plus 2-bit anchor springs per
widget. It staked everything on hosting: the saved runtime copies were its
XT story, and the IDE-on-runtime coupling was its proof the components were
good enough to build real software with.

**WAP — the minimal design.** Two packages, one VM, layout resolved to
absolute cell rectangles at pack time, spreadsheet recalc in 16-bit JS, games
paced by JS timer handlers at ~9 fps, and an on-machine compiler written in
8086 assembly. It staked everything on shippability: the smallest resident
(40–46KB), the fewest mechanisms, and the only XT develop-and-run arithmetic
that closed by its own figures.

**Weave & Loom — the components-first design.** The library IS the product:
native components (os88ui-wrapped forms, a band-composed grid, a
worker-driven sprite canvas), markup and script compiled at pack time, two
VMs (a 16-bit JS bytecode machine and a separate 16.16 fixed-point formula
engine), an instance per app, and a separate native IDE on the Note Pad
engine transplant. It staked everything on delivering the ask — real
formulas with decimals, a real 18 fps game — with every mechanism mapped to
something already shipped and measured in this tree.

### 1.2 The hosting design's two fatal flaws — verified, do not re-propose unwarned

Hosting is the seductive architecture: on paper it is the *better* multi-app
XT arithmetic (runtime 60KB + 2×32KB apps = 124KB, against 2×~85KB for two
full instances). It lost anyway, on two flaws every judge verified
independently, and anyone re-proposing it must answer both:

1. **Its XT story is broken by its own claim ledger.** The design's
   flagship "designed edge" — runtime 60KB + STUDIO arena 48KB + a 32KB app
   = 140KB of the 256KB XT's ~140.5KB heap (`tools/kernsize.py`) — omits
   two claims its own memory plan requires: the ~24KB `LOOM.OVL` transient
   claim that holds the on-machine compiler (Pack cannot run without it)
   and STUDIO's 8–16KB source-buffer claim. With them, the edit–pack–run
   peak is ~172KB — **~23% over the heap on exactly the machine class the
   OS targets**, so the advertised "loop in under five seconds on the XT"
   requires closing the running app first, which is not a loop. The
   arithmetic was re-derived from the design's own `memory_plan` figures by
   all three judges.
2. **Its warm-handoff mechanism is out of contract.** Passing a bundle into
   the already-running runtime rode a cross-instance `OSAPI_WM_WAKE` — and
   the SDK scopes that slot's argument explicitly: `apps/os88api.inc:2841`
   reads *"BX = win ptr (a window of YOURS)"*. Waking another instance's
   window is outside the documented surface, not merely untested. The
   design knew it (a timer-peek fallback was specified), but the headline
   mechanism was contract-breaking on the day it was written.

Two secondary findings sharpened the verdict: its 56KB resident estimate
priced the Note Pad engine transplant at ~11KB when the engine assembles at
17,819 bytes measured (the editors study), against the hard 61,440-byte
package ceiling with the one overlay (SPEC.md §73.14) already spent on the
compiler; and its v1 game was paced by JS timer handlers, contradicting its
own "no per-frame JS" exclusion.

If hosting is ever revisited — and on a 640KB machine the copy-saving is
real — the re-proposal starts by doing the XT ledger **with the OVL claim
and every transient claim on the table**, and by finding an in-contract
handoff. Neither existed in the judged design.

### 1.3 Why WAP placed second, not first

WAP had **no verified fatal flaw** — the judges called it "the fallback that
cannot fail". It lost on delivery: a spreadsheet without decimals (16-bit JS
recalc, ±32,767), no formula engine, games at ~9 fps, no preview, and
pack-time absolute layout whose own risk list admitted it shrinks the
audience per adapter — a VGA-sized app *refuses to open* on CGA's ~17
content rows. Its one novel risk (an on-machine compiler in raw 8086
assembly) was also its least necessary divergence, since both rivals put the
compiler in gated C inside the overlay. Everything WAP was right about was
graftable, and was grafted (§1.4).

### 1.4 The grafts

The adopted design is design candidate 2 plus five judge-verified grafts,
recorded here because each replaced something in the original draft:

| graft | from | replaced |
|---|---|---|
| per-class event-ring overflow policy (WEAVE-SPEC §4.9) | LOOM | a policy that could drop typed keys (§2.7) |
| slice-cap correction to 1,536 ops (WEAVE-SPEC §4.10) | verdict review | a 4,096-op cap with wrong arithmetic (§2.8) |
| pack-time exclusion **rejection** with sentences naming the platform fact (WEAVE-SPEC §10.5) | LOOM | pack-time warnings |
| the Deck launcher (WEAVE-SPEC §1.6) | LOOM | an empty window on launch-with-no-file |
| the committed component cost table, generated by the model (WEAVE-SPEC §14) | LOOM | costs scattered through prose |

Already in the winning design and kept exactly: WAP's File→Reload edit–run
loop and pack-on-save discipline, the handle-table GC, the optional SOURCE
section, and the pure-Python packer running inside `make all` so a fast-tier
suite row exists without the C toolchain.

---

## 2. The decided forks, each with its arithmetic

### 2.1 Instance-per-app

**Decided: every open Weave app is a full WEAVE instance** — its own copy of
image+bss in its own segment, the platform's grain (SPEC.md §20.1) — plus
its claims. WEAVE-SPEC §1.4 carries the binding table; the reasons:

- Hosting needs document-passing into a running instance, and
  `OSAPI_ARG_FILE` is read-and-clear (SPEC.md §54.5): each double-click
  opens a new instance *by construction*. The in-contract alternatives all
  reduce to polling or to the out-of-contract wake of §1.2.
- One VM and one ring serializing every open app means one runaway script
  takes down all of them; instance-per-app makes the runaway alert
  (WEAVE-SPEC §4.11) a per-app affair.
- The cost is known and stated, not discovered: a typical instance is
  ~75–120KB (package region ~52 + bundle 8–24 + VM 16 + grid 0–26 + canvas
  0–8). On the 640KB machine (~524KB heap, kernsize.py) that is four to
  five apps open at once, or two plus Loom. On the 256KB XT (~140.5KB heap)
  it is **exactly one app at a time** — a full spreadsheet instance
  (~110KB) with ~30KB slack, the second launch refused before any I/O with
  the sentence naming both figures (WEAVE-SPEC §10.1). On the XT you edit
  or you run; that is a decision this record owns, not a discovery waiting
  in wave 6.

### 2.2 The Reload loop, not a kernel launch slot

**Decided: the edit–run loop is Pack → click the open window → Cmd-R
Reload** (WEAVE-SPEC §1.7), and no kernel launch API is asked for.

The gap is real and was verified by enumeration: all **143** `%define
OSAPI_` slots in `apps/os88api.inc` were counted and none launches a
package; `loader_run`/`ld_run_name` (`kernel/loader.inc:230,316`) are
kernel-internal, reachable only from a Finder listing click, the dock, or
the association sweep. So an IDE's "Run" button has no kernel route today.

The fork had three exits: a new kernel slot, an IDE-embedded preview
interpreter, or a loop that never needs launch. The slot loses on the
budget: **kern_small has 0 bytes of `KERN_BUDGET` spare and kern_big 512**
(docs/KERNEL-MEMORY.md's two guards), and CLAUDE.md's hard rule makes
raising either an owner decision, not a build fix — a family that *depends*
on that conversation cannot ship until it is had. The embedded interpreter
loses on the resident ceiling (§1.2's cautionary arithmetic). The Reload
loop costs zero kernel bytes and two gestures: `File → Reload` re-reads the
banked bundle from disk into a fresh claim, re-flows, restarts the VM. Only
the first-ever run of a new bundle takes a Finder double-click, through the
`OS88_ASSOC16` association (SPEC.md §54.6, the Frotz shape) — and WEAVE
launched empty shows the Deck, so "launch" is an internal function of the
runtime.

The launch-by-name slot is **deferred, not rejected** — recorded with its
arithmetic in §4.1 as an upgrade path if the loop proves slow in practice.

### 2.3 Two VMs — WJS 16-bit, FX 16.16

**Decided: formulas run on their own interpreter**, not on the JS machine.
The WJS number type is 16-bit signed int because the C toolchain has no
`long` or `float` (docs/C-TOOLCHAIN.md) and the VM inherits that honestly
rather than emulating what the platform refused. But a spreadsheet without
decimals and capped at ±32,767 is a calculator-grade fiction — the judges
scored WAP down for exactly that — and the fix is free in assembly, where
the C rule does not bind: the FX VM (WEAVE-SPEC §5) is untyped 32-bit 16.16
fixed point, RPN, ~30–60 µs/op with no tag dispatch and no call frames.

The budget arithmetic that forces the split: recalc of 500 formula cells at
~10 ops each is 5,000 ops. On the FX VM that is ~150–300 ms across 3–6
ONWAKE slices with a status cell showing. Pushed through the WVM it would
spend seconds of the 10–30k ops/s contract (WEAVE-SPEC §4.12) *and* compete
with event handling for the same slice budget — recalc would make the app
deaf. Formulas must not spend the JS budget; two interpreters is the cheap
way to say it. Recalc ordering is VisiCalc's model — natural row-major, a
second pass, a circular marker — no dependency graph in v1.

### 2.4 Flow walk, not anchor springs

**Decided: layout is one flow walk, run at open and again at every resize**
(WEAVE-SPEC §7). LOOM's alternative — bake final rects at pack time plus
2-bit L/T/R/B springs per widget for native resize — was rejected as a
second mechanism: two layout truths (baked positions *and* a spring
adjuster) where one walk suffices, and baked positions are wrong on two
adapters of three anyway, because the three cell grids differ (CGA ~79×17
content cells, Hercules ~89×36, VGA ~79×52 — the browser study's table).
WAP's fixed rects had already demonstrated the failure mode: a VGA-sized
bundle that *refuses* on CGA. The walk is priced cheap enough to re-run —
it emits no gfx calls itself, and positions are never in the bundle at all,
which is also what keeps the bundle adapter-independent.

### 2.5 Compile at pack time, never parse at run time

**Decided: the machine never sees a character of WML, WJS or FX text.** The
browser parses at run because it has no choice — its input arrives over a
wire at run time, and streaming HTML through a fixed window was the hard
lesson of that project (docs/plans/completed/BROWSER-PLAN.md). A bundle's source exists in
full at pack time, so runtime parsing would buy nothing and cost a resident
parser plus parse-time on a 4.77 MHz critical path. Everything compiles in
the packer — host-side `weavesim --pack`, on-machine `LOOM.OVL` — and the
two packers are held **byte-identical** (WEAVE-SPEC §11), which is the
`tools/wordfmt.py` two-independent-implementations pattern doing double
duty: it is also what makes an on-machine compiler trustworthy at all. The
host reference is written first on the `tools/htmsim.py` precedent, which
found three real bugs before any 8086 code existed.

**Wave 4 carved out exactly one exception and it is worth saying why the
decision above survives it.** A spreadsheet whose cells cannot be typed into
is not one, and a cell's source IS text — so `apps/weave/wfxc.c` compiles FX
on the machine (WEAVE-SPEC §6.9.2, WEAVE-SPEC §9.4). What the paragraph above is about
is a PROGRAM: the display list and the bytecode still arrive compiled, the
pack step is still the only surface that compiles one, and no WML and no WJS
is ever parsed on this machine by WEAVE. What is carved out is one expression
language of nine productions and eight functions, entered a line at a time by
a person looking at the result — WJS is the app's language and FX is the
user's, which is the same line WEAVE-SPEC §5's own split already draws.

Two things kept it honest. It is **the whole of WEAVE-SPEC §5.1 and not a
subset**, because a subset would let a formula pack on the host, load on the
machine, and refuse the moment its author clicked its cell to look at it —
WEAVE-SPEC §11's byte-identity rule said about a language instead of about a
file. And it is **not resident**: WEAVE-SPEC §6.9.2's first draft argued
that "your formula did not
compile because a module would not load" is not an answer a spreadsheet may
give, which is true and was not decisive — tenant 5 already gives exactly
that answer about `saveState`, on the same three grounds, and the size line
made the choice arithmetic rather than taste (WEAVE-SPEC §1.2.1's tenant 7
carries the paragraph).

### 2.6 Handle-table GC, and the §66 adoption path

**Decided: v1 pins every claim, and every VM heap object is reached through
a handle table** (WEAVE-SPEC §4.8). Strings and arrays carry handles —
indices into one table in the VM claim — never pointers, so the compacting
collector fixes exactly one table and never hunts roots. That choice is
also the MOVABLE story: SPEC.md §66's default is PINNED, opting in takes a
relocation proc, and forgetting the proc is silent until a busy-heap
compaction. Handle indirection plus base-relative addressing throughout
keeps adoption a later, **per-claim, three-word decision** (the Frotz
ceiling precedent, SPEC.md §66.5.9) instead of a hunt through every
derived segment — v1 takes none of that risk and forecloses none of it.

### 2.7 The event-ring policy — a graft that fixed a real defect class

Design candidate 2 originally coalesced *keyboard* events and dropped
others on overflow — which can lose typed keys, and input overrun is one of
the three defect classes this tree has learned are **invisible in every
emulator** (CLAUDE.md's performance section; each cost a field bug).
LOOM's stricter per-class policy was grafted verbatim and is now binding as
WEAVE-SPEC §4.9: keys are never dropped by coalescing; `onchange` /
`onselect` / `onclick` / `onscore` coalesce per component, newest wins;
`ontimer`/`ontick` collapse to one; a full ring receiving a key drops the
newest queued non-key event; a ring genuinely full of keys answers the next
key with a BEL (the RunCPM precedent) rather than silence.

### 2.8 The slice-cap correction — arithmetic checked against its own contract

The judged design shipped a slice cap of 4,096 ops justified as "under
~50 ms". At the design's *own* contract number — 10–30k ops/s — 4,096 ops
is **136–410 ms**, up to seven ticks. The verdict caught it; the adopted
cap is **1,536 ops = 51–154 ms**, with the honest arithmetic stated in
WEAVE-SPEC §4.10 precisely because a wrong version of it once existed. The
episode is recorded here as method: every budget number in the family is
required to carry the multiplication that produced it, so a reviewer can
re-run it.

### 2.9 The canvas core is a SECOND SEGMENT, and the alternatives are priced

**The decision wave 5 turns on, recorded here so that whoever reverses it
starts from the arithmetic rather than from the shape of the code.** The
contract is WEAVE-SPEC §1.2.2; this is why.

**What wave 5 opened with.** `weave.o88` at 50,360 image + 10,502 bss =
**60,862 resident** against SPEC.md §20.1's `APP_MAX_SIZE` of 0xF000 =
61,440. **578 bytes.** §5.2 already said the pre-named overlay tenants were
spent for the second time and that the easy structural savings had gone with
wave 4.

**What wave 5 needs resident, and why none of it can be an overlay tenant.**
Mask composition, the dirty-band mark/compose/emit, the frame loop, AABB over
sixteen sprites, the 37-key poll, the staging ring — measured at ~3.4KB of
hand-written 8086 — run **on a worker task, per frame**. SPEC.md §73.14's
overlay is loaded by `cc_ovneed`, whose *first instruction* is `call cc_iswk`
and whose answer on a worker is a refusal: the load claims memory and reads a
floppy, both forbidden by SPEC.md §20.6 rule 7, and the return-stash LIFO is
correct for exactly one task. So the tenant list being spent is not what
decides this. The tenant list is **inapplicable**, and would be at 578 bytes
or at 5,780.

**The four alternatives, each with the number that lost it.**

| alternative | the arithmetic | verdict |
|---|---|---|
| **Raise `APP_MAX_SIZE`** | It is not a budget. A package links at `org 0` and addresses itself with 16-bit offsets (SPEC.md §33), so image + bss can never reach 64KB whatever the heap holds. 0xF000 leaves 4,096 bytes of headroom for the loader's own arithmetic; raising it to 0xF800 buys 2,048 bytes and spends a kernel constant every package in the tree is measured against, on one package's wave | **Refused, and it is not this project's to take** — CLAUDE.md: raising a budget is a decision taken with whoever asked for the feature. And 2,048 bytes buys one wave, not the family |
| **Move bss into claims** | The three big ones are `w_lay[250]` at 2,500 bytes, `w_probe` at 1,024 and `w_gband` at 720. `w_lay` is defended in the source (an eleventh field costs 500 bytes) and every access would become a `w_w(seg,…)` far call on the layout walk's hot path; `w_probe` must be a whole cluster or §10.1's refuse-before-read guarantee degrades on every 2-sector-cluster volume; `w_gband` is reachable but needs an ES-based composer, which re-opens PERFORMANCE.md Set 111's measured file | **Refused as the ANSWER, taken as a partial** — it buys ~1KB at the cost of speed on measured paths, and wave 5 needs 3.4KB. What it did buy is §2.9's honest savings below |
| **A second overlay** | Same mechanism, same `cc_iswk` refusal at the door. A second `.OVL` would need a second load path, a second stamp, a second claim — and would still refuse the worker | **Refused: it does not solve the problem at all** |
| **Cut the feature** | A `<canvas>` without a worker is a canvas that draws on `WM_TIMER` at 18.2 Hz on the UI task, holding the gfx lock for the compose as well as the blit, with every keystroke and every menu track behind it. SPEC.md §20.6 rule 3 is the rule it breaks | **Refused: the windowed game IS the worker** (§4.4) |

**What was taken instead**, and it is the tree's own doctrine rather than a
new mechanism: CLAUDE.md's hard rule *a C package that does not fit gets a
second segment, not a bigger one*, read through SPEC.md §68.10's `WORD.OVL`
(an assembly module beside the package, DS still the package's) and
`C64.ROM`'s lifecycle (a sidecar read at launch into a claim, never
re-read). `WEAVE.WSM` is both: a flat `nasm -f bin` binary at `org 0`, read
**once at open and only when the bundle declares a canvas**, resident until
close, far-called from the worker and from the UI task alike. A bundle with
no canvas pays nothing at all — not a claim, not a disk revolution, not a KB
of §10.1's ask.

**What it costs, and it is not free.** One more claim record: WEAVE-SPEC
§1.4's ladder goes from six at peak to **seven against SPEC.md §50.2's cap of
eight**, so the family has one spare rather than two, and §1.4 names the
candidate to take back if an eighth is ever wanted. One more file on the
disk, and one more refusal sentence at open. One more stamp to keep honest —
and the stamp is stronger than `.OVL`'s, because a shared `%include` gives
both assemblies an ABI number, which two size words cannot express.

**And the honest savings were taken first, because a wave that reaches for a
new mechanism without them has not earned it.** Two, both provable, both
free: `w_idseen[256]` (the comp_id uniqueness bitmap) is **subsumed** by the
sequential check three lines below it — a bundle whose ids are not exactly
1..n in order is refused by `id != w_ncomp + 1` with the same field name, so
the array can never be the thing that fires; and `w_msg[88]` was a
byte-for-byte duplicate of `w_status[88]`, written only by `w_say()`, which
copies the same sentence into both. **344 bytes**, which is not the wave and
is 60% of what wave 5 opened with.

---

### 2.10 Preview's painter is a SECOND SEGMENT too, and the alternatives are priced

**The decision wave 7 turns on, recorded here so that whoever reverses it
starts from the arithmetic rather than from the shape of the code.** The
contract is WEAVE-SPEC §1.2.4; this is why. It is §2.9's decision taken again
for a different reason, and the difference between the two reasons is the
whole of this section: §2.9's code could not be an overlay tenant because a
**worker** cannot enter one; wave 7's code could enter one perfectly well, and
the thing that does not fit is not code at all.

**What wave 7 opened with.** `loom.o88` at 54,752 image + 6,198 bss =
**60,950 resident** against SPEC.md §20.1's `APP_MAX_SIZE` of 0xF000 = 61,440.
**490 bytes** (594 before the merge with `main`; §5.2 prices the shared-SDK
growth).** And WEAVE-SPEC §1.7.1's row to pay: Preview's picture.

**What Preview's picture needs, and why an overlay does not move it.** The
picture is WEAVE-SPEC §7's flow walk and WEAVE-SPEC §6's component painter —
`apps/weave/wflow.c`, `apps/weave/wpaint.c` and the assembly cores in
`apps/weave/wdraw.inc`. SPEC.md §73.14 moves **code** into a module and leaves
*"every global, literal and bss byte it names resident and DS-relative"*, and
what those files name is:

| what | bytes |
|---|---|
| `w_lay[W_MAXLAY]` — the walk's output table, ten bytes a record | 2,500 |
| `w_lpos`, `w_lsel1`, `w_ctext`, `w_cflag` — four byte tables keyed by comp_id | 1,004 |
| `w_cval`, `w_cvold` — two word tables, ditto | 1,004 |
| `w_str` (the staged string), `w_sbblk`, the list-override pool, the walk's own statics | ~250 |
| the rest of the module's `.bss` as built | — |
| **measured, `LOOM.WPV`'s `.bss` as it shipped** | **5,268** |

against **594**. An overlay moves none of it. That is the paragraph, and every
alternative below is judged against it.

**The four alternatives, each with the number that lost it.**

| alternative | the arithmetic | verdict |
|---|---|---|
| **Make `wflow.c`/`wpaint.c` `ovl_*` tenants of `LOOM.OVL`** | The obvious answer, and the one WEAVE-SPEC §1.7.1 asks to be judged honestly. The CODE moves and 5,268 bytes of data do not, against 594. The overlay's own size was never the constraint — `LOOM.OVL` is 42,902 bytes against a segment's 64KB — and neither was the callback rule: a `W_PAINT` runs on the UI task, so it *may* enter an overlay and would simply have to draw WEAVE-SPEC §1.7.1's label when it could not. It is the data | **Refused, by 4,674 bytes.** The instrument does not cut in this direction |
| **Write a smaller painter inside LOOM** | It fits, and WEAVE-SPEC §1.2 forbids it by name: *"never a second copy… two layouts that must agree cell-for-cell is the failure §11's byte-identity rule exists to prevent, said about code instead of about bundles."* A Preview that drew a different picture from the runtime's is worse than one that draws none, because the whole point of the pane is to answer "what will this look like" before `^R` | **Refused, and it is the one alternative that is refused on principle rather than on arithmetic** |
| **Move the tables into a claim and keep the code in the overlay** | Reachable: `w_lay[]` and the six tables become `w_w(seg,…)` accessors against a claim. It costs a far call per field on the walk's and the painter's hot paths — and it is a rewrite of `wflow.c` and `wpaint.c`, which are **WEAVE's** files, so the cost lands on the RUNTIME's per-paint path to buy the IDE a pane. Wave 5's own measured refusal of the same idea (§2.9's second row) applies twice over here | **Refused: it spends the runtime's speed on the IDE's feature**, and WEAVE closed wave 5 with 32 bytes spare, so it could not have absorbed the change either |
| **Raise `APP_MAX_SIZE`** | §2.9's first row, unchanged and still not this project's to take: a package addresses itself with 16-bit offsets, 0xF000 leaves the loader its headroom, and raising it spends a kernel constant every package in the tree is measured against | **Refused, and it is the owner's** |

**What was taken instead** is §2.9's answer with one difference: `LOOM.WPV` is
a second, resident segment beside `LOOM.O88`, read once the first time Preview
is opened and held until the instance closes — and it is **compiled C** rather
than hand-written assembly, which is new in this tree. That costs exactly one
thing beyond WEAVE-SPEC §1.2.2's shape, stated in WEAVE-SPEC §1.2.4: a C module
has a `.bss` its file does not carry, so the header carries a bss word, LOOM
claims image + bss, and the module zeroes the tail itself on its first entry.
Everything else — the three-word stamp grown to four, the far-call seam, the
`OSAPI_*` far immediates that need no vector back into the package — is the
canvas core's, one wave later.

**What it costs, and it is not free.** One more claim record: LOOM's ladder
goes to **five at peak against SPEC.md §50.2's cap of eight** (WEAVE-SPEC
§1.4), which is comfortable where WEAVE's is not. 21KB of heap and one floppy
read, on the first Preview of an instance and never again — and nothing at all
for a LOOM the user never previews in. 228 resident bytes in `LOOM.O88` for
the seam, the loader and the three refusal sentences, leaving **366 under**
the ceiling. One more file that has to travel with the package, which is one
more `LOOM.WPV is not on this disk.` and one more reason `make weavedisk`,
`make loomdisk` and SPEC.md §19.10's `LOOM/` folder put the three files
together.

**And the honest savings were taken first**, because §2.9 set that rule and a
wave that reaches for a mechanism without them has not earned it. Wave 6's
Preview pane carried six rows of prose in it; the pane now carries a picture,
so the prose is three one-line refusals and the label moved to the status row.
That is where most of the 228 came back from: the first build of the seam
closed at 61,402 — **38 bytes under** — and trimming the sentences a pane no
longer needs took it to 61,074.

**What it does NOT buy, named rather than left to be found.** A `<grid>` and a
`<canvas>` draw as their frame in Preview (WEAVE-SPEC §1.7.1), because a
grid's body is the band composer over a cell store and a canvas's is the
compositor inside `WEAVE.WSM` on a worker — a second claim, a second module
and a recalculation apiece. And the pane does not ARM: WEAVE-SPEC §1.7's
*"widgets draw and arm/fire natively"* is drawing in wave 7 and not the
gesture, which needs `apps/weave/wact.c`'s press/release pair and the field
pool under it, and firing needs the event ring, which is the VM's. Both are in
WEAVE-SPEC §13.2.

### 2.11 The family disk is TWO whole folders, not one flat root and not a folder per project

**The ask was the obvious one** — the compiled programs apart from the source
they were built from — and wave 7 had already shown the obvious layout for it
refusing on the machine: a `PROJECTS/` folder per project opened LOOM without
`LOOM.OVL` by both routes (WEAVE-SPEC §11.2), because SPEC.md §73.14 resolves
a package's overlay and sidecars in the directory the instance stands in, and
a double-click on a document stands it in the DOCUMENT's. Wave 7's answer was
a flat root. Three shapes were judged after it:

| shape | what breaks | verdict |
|---|---|---|
| flat root (wave 7) | nothing; the listing mixes 22 files of five kinds | correct and confusing |
| `WEAVE/` bundles + runtime, `LOOM/` IDE + sources | the bundle Pack writes into `LOOM/` opens with WEAVE, whose modules are in `WEAVE/`: `WEAVE.OVL is missing or stale` on the first run of anything built on the machine — the edit-run loop (WEAVE-SPEC §1.7) dead on the disk that exists to show it | rejected |
| the same, **plus a second copy of the runtime in `LOOM/`** | 75 clusters a disk (51,124 + 20,983 + 4,593 bytes); 285 of 354 at 360KB | **taken** |
| kernel: resolve the overlay in the PACKAGE's directory instead | a kernel change with 512 bytes spare (docs/KERNEL-MEMORY.md), touching every overlay package's launch (CWORD, C64, RUNCPM, WORD) for a layout preference | not proposed |

The copy is a copy and never a move — SPEC.md §24.3's rule for the core
packages on the system disk — and it is the only cost: nothing changes at run
time, and a person who launches WEAVE from `LOOM/` gets the same program. A
folder per project stays what §11.2 describes for a project a person keeps
beside a LOOM of its own. `make allapps` had shipped the `LOOM/` folder
without the runtime since wave 7 — the same dead first run — and gained the
copy in the same change, along with the `--dir-slots` the folder needs on a
disk whose cluster is one sector (SPEC.md §18.5: the kernel does not grow a
directory, and Pack saves into this one).

---

### 2.12 The palette is a PEN, not a deeper buffer — and it is canvas-only

**The fork the colour wave turns on**, recorded here so that whoever reverses
it starts from the arithmetic. The contract is WEAVE-SPEC §6.10.7 and the
amendment it makes is WEAVE-SPEC §9.2.1; this is why each half went the way
it did.

**What was asked for**: colour in Weave, and colour in PONG. What the
platform offers: sixteen indices on VGA and **one plane** on CGA and
Hercules, where SPEC.md §39.4 rounds every colour to black, white or a
dither — and, decisively, where SPEC.md §5.4.2.2 says the `GFX_BLIT1` pen is
**not read at all**.

**Fork 1: where the colour lives.** Three candidates.

| | what it buys | what it costs |
|---|---|---|
| **the PEN over the existing 1bpp buffer** (taken) | per-sprite colour, per band span | one `GFX_BLIT1_PEN` per span, and a span split per colour change in a run |
| a 4bpp buffer + `GFX_BLIT4` | per-PIXEL colour | 4× the buffer (PONG 3,600 → 14,400 bytes, past §2.2's 8KB canvas byte), a nibble-shifting composer rewrite, 4× the per-frame compose bytes |
| a second 1bpp plane per colour class | 2 colours free | one more buffer, one more compose pass, and a hard cap of two |

The middle one is the one a reader expects and it is the one the arithmetic
refuses: it buys per-pixel colour, and **sprite art in this family has never
wanted per-pixel colour** — a `.WSP` is `#` and `.`, one bit, by
WEAVE-SPEC §3.6's own grammar. Paying a composer rewrite and four times the
frame's byte count for a capability the source language cannot express is
not a close call. The pen costs 1.3% of a PONG frame (below) and needs no
new byte in the buffer at all.

**And the pen is safe here for the precise reason wave 5 said it was NOT.**
WEAVE-SPEC §6.10.2 carries a shipped defect: wave 5 used the pen to fix the
buffer's POLARITY, and on CGA — where the pen is not read — PONG came up a
black field with white paddles. That paragraph is what makes this one
possible. The buffer is now stored the way all three adapters want it, so a
pen is a pure *addition* on the adapter that has colours and a *no-op* on the
two that do not: not a reduction, not a dither, ignored. The load path makes
that structural rather than incidental — it does not read the three colour
attributes at all when `OSAPI_VIDEO` says `bpp == 1`, so on CGA and Hercules
the composer runs the same instructions over the same runs and emits the same
calls as before the feature existed.

**Fork 2: how a run with two colours is emitted.** A dirty band run is full
width and can hold sprites of different colours, and `GFX_BLIT1` is opaque
and takes one pen. Three candidates:

- **base pass + a re-blit per coloured sprite** — writes the sprite's pixels
  twice a frame. That is PERFORMANCE.md's *double-draw flash*, one of the
  three defects no emulator shows, bought with a colour. Rejected outright.
- **one colour per run, chosen by whichever sprites are in it** — costs
  nothing and makes a sprite's colour depend on what shares its band, so the
  ball changes colour when it passes a paddle. That is a half-honoured
  colour, which is the exact failure WEAVE-SPEC §9.2 was written about.
- **disjoint column SPANS** (taken) — the run is cut at byte columns where
  the colour changes, and each span is one blit. No pixel is written twice,
  every sprite is always its own colour, and the split is deterministic.

Two rules make the spans cheap. **A column no sprite reaches is NEUTRAL and
merges into its neighbour** — every bit of it is paper, so the pen's ink half
cannot show, and merging is provably pixel-identical. And **a column two
sprites share takes the last one's colour in UISTREAM order**, which is
composition order — so the sprite whose pixels are on top is the one whose
colour shows. In PONG that is two frames of contact in which the ball's
overlapping byte column wears the paddle's colour; the alternative is the
second pass rejected above.

**The count, measured over a 400-frame rally** against the model's own
composer — driven by `PONG.WJS`'s own `onTick`, `onHit` and `onGoal`, so the
sprite motion is the game's and not a guess — and not modelled: **1.005 calls
a frame uncoloured, 2.040 coloured** — 1 call for 193 frames, 3 for 205, 4
for 2. The 3-call frames are the ball level with the paddles, which is most
of a rally because PONG serves at `vy = 4`, a quarter of a pixel a frame.
**+1.035 calls is 782 µs of a 55 ms frame, 1.4%**; the worst frame is
+2.3 ms, 4.1%.

**And the MACHINE counted it, on both adapters, which is the reading to
quote.** `tests/weavegame` reads WEAVE.WSM's own blits/frames counters over
the frames after Serve, and it runs on a VGA MartyPC as well as a CGA one:
**0.95-1.06 calls a frame with the palette off (CGA, 18-19 blits over 18-19
frames) against 2.84-3.17 with it on (VGA, 54-57 blits) — about +1.8 calls,
~1.35 ms, 2.4%**. The 1bpp figure is `origin/main`'s own, run for run.

**And the first version of this feature FAILED that comparison, which is why
it is now a gate.** `ink` and `paper` were skipped on a 1bpp adapter and
`color` was not — `w_pint(props, WA_COLOR, w_cink)` answers the PROP when the
bundle carries one, and PONG carries three — so the sprite nibbles were set
anyway, the module's `colored` flag came up, and the composer cut full-width
runs into colour spans on CGA. The picture was identical, because the pen is
not read there; the count was **2.84 against 1.06, a 2.7x regression on the
target adapter class**, and nothing in a screenshot or in `weavegame`'s
0.5-4.0 assertion could see it. PERFORMANCE.md's own sentence, arriving as
the thing it warns about: keeping the SHAPE of an optimisation is not keeping
the optimisation. The fix is one flag covering all three reads, and
`weavegame` now asserts the flag itself — `colored` is 1 on `vga` and 0
everywhere else — because a count has a band and a flag does not. WEAVE-SPEC §14 carries it as
a NEW row beside the two uncoloured ones rather than moving them, because the
uncoloured rows are still exactly what CGA and Hercules pay.

**What would remove even that, and why it is not in this wave**: every extra
call is a paddle re-blitted because a dirty BAND is full width. Bounding
a run to the columns the moved sprites actually touched would put PONG back at
~1 call a frame and cut bytes on both paths — which is why it is a separate
change: it moves wave 5's shipped behaviour, WEAVE-SPEC §12.1.3's whole corpus
and §14's two existing canvas rows. A dirty-rectangle change wearing a
palette's clothes; merging the two would leave neither reviewable.
WEAVE-SPEC §13.2 carries it.

**Fork 3: canvas-only, and this is the load-bearing restraint.** Colour on
`<label>`/`<button>` is CHEAPER than the canvas's — `OSAPI_FONT_RUN` already
takes an ink/paper pair, so it is **zero extra gfx calls** and roughly
100–150 resident bytes (one `w_pint` per painted component at ~20–40 µs,
against a card paint measured in hundreds of milliseconds; no eleventh field
in `w_lay`, so not the 500 bytes that would have cost). It was left out
anyway, for three reasons that are about the picture and not the price:

1. **The 1bpp degrade is only half-clean.** SPEC.md §39.4 rounds a *glyph* to
   black, so a coloured ink would degrade exactly — but *paper* goes through
   `gfx_ink` and can come back a dither. The vocabulary would have to be half
   of a pair, and a half-honoured fg/bg pair is what WEAVE-SPEC §9.2 names.
2. **It puts colour on the things a reader READS.** The line
   WEAVE-SPEC §9.2.1 draws — art takes a palette, text does not — is what
   keeps SPEC.md §39.4's "state never rides on colour" true by construction
   rather than by review. PONG's score is a `<label>` and is a `<label>` on
   VGA too.
3. **It is two images, not one.** `LOOM.WPV` compiles the same painter a
   second time (§2.10), so every painter change lands twice.

**Where the bytes went.** `weave.o88` moved 51,124 + 9,196 = 60,320 to
51,134 + 9,210 = **60,344 resident, 1,096 under** SPEC.md §20.1's 61,440 —
**24 bytes**, because the whole load path is inside overlay tenant 8
(`ovl_canvasload`) and only the `bpp` probe's static struct is resident.
`WEAVE.WSM` went 4,593 → 5,077, and it is a claim rather than the package's
segment, so that is the composer's own budget and not §20.1's.

**The refusal is at PACK, and it had to be.** SPEC.md §5.4.2.2 has a fourth
refusal: `GFX_BLIT1` answers `CF = 1` for a pair whose colours share no plane
in either direction. At run time that is a band that silently does not
arrive, on a path WEAVE-SPEC §6.10.2 states has no second answer. So both
packers compute it for every (paper, ink) and (paper, sprite colour) pair,
`tests/weave/packerr/canvas-pen-pair/` is the case, and `tests/unit/t_wab.py`
asserts it independently as a bundle invariant. It is also why **`color` is
not on the script surface** (WEAVE-SPEC §13.2): a running bundle that could
write a colour could invent a pair the packer never saw.

## 3. The platform facts that shaped everything

None of these was derived during design; each was verified in the tree by
the subsystem studies and the panel's critic, and each closed a door the
design then did not lean on. Sources binding:

| fact | consequence in the design | source |
|---|---|---|
| **No launch API.** 143 `OSAPI_` slots enumerated, none launches a package; the loader is kernel-internal | the Reload loop (§2.2), the Deck, the association-only first launch | `apps/os88api.inc` (count), `kernel/loader.inc:230,316` |
| **No hover.** No passive mouse-move event exists; `W_ONDRAG` fires only inside one press–release gesture, and polling `OSAPI_MOUSE` beside tracking is forbidden | WML has no hover vocabulary at all; the packer rejects it (WEAVE-SPEC §9.1/10.5) | SPEC.md §13.7 |
| **No masked blit.** `GFX_BLIT1`/`BLIT4` are opaque; XOR is the only reversible on-screen mark | sprite transparency is AND/OR masks built at pack time and resolved in RAM composition; selection marks are XOR | `apps/os88api.inc` blit slot text, verified by the critic |
| **kern_small refuses `GFX_BLIT1`, `WM_TIMER`, `WM_ONDRAG` by CF=1**, and its heap is ~22KB | the family's floor machine is 256KB; `NEEDS_*` header flags refuse at load with the sentence (WEAVE-SPEC §10.2) | `apps/os88api.inc` slot text; kernsize.py |
| **`SND_PLAY` freezes the desktop** — scheduler locked for the clip, a click aborts; TONE is the floor and the only worker-legal voice | `playSound` documented with the cost, refused where absent; the game worker emits tones only (channel 8 reserved) | `apps/os88api.inc` sound slot text, verified by the critic |
| **The ops/s budget.** ~10–30k bytecode ops/s with assembly dispatch is design arithmetic from the two shipped interpreter cores, **pending a field reading** | the contract number (WEAVE-SPEC §4.12), self-measured in About, converted by the wave-5 field run | SPEC.md §74's Z80 precedent; §4.2 below |
| **Heap truth is `tools/kernsize.py`**: ~524KB (640KB machine) / ~140.5KB (256KB XT). SPEC.md §61.4's 551/167 are stale | every instance-count claim in §2.1 | docs/KERNEL-MEMORY.md (kernsize is the authority) |
| **Drawing is priced**: 756 µs per gfx call, ~900 µs per glyph cell, ~71 ms per 78-cell row; band composer 860 µs/call + 173 µs/cell | the component cost appendix (WEAVE-SPEC §14), generated, never hand-edited | PERFORMANCE.md Part 2 and **Set 68** — this wave also fixes CLAUDE.md's stale `Set 65` citation for the composer bench |
| **A worker may not touch a file** (rule 7), a memory slot, or the layout | `saveState`/`loadState` run on the UI task inside the slice; the game worker enqueues events and draws, nothing else | SPEC.md §20.6 |
| **Package ceiling 61,440 bytes image+bss, ONE overlay, ≤8 claims per owner, every disk-visible base 512-aligned** | the ~52KB resident target with 55,000 as the split trigger; six named claims, two spare; KB-aligned bundle base | SPEC.md §20.1, §73.14, §50.2; CLAUDE.md hard rules |
| **A refusal must be computable before I/O**, so the directory size must stand for the resident ask | bundles are never compressed and cap at 62KB (WEAVE-SPEC §2.1/9.10) | SPEC.md §61.4 (the Frotz rule) |

---

## 4. Deferred and open — each with its number attached

### 4.1 A kernel launch-by-name slot

Deferred as an **owner conversation, never a dependency**. What it would
take: a slot resolving an 8.3 name through the association sweep to the
`ld_run_name` path, an answer for the ARG_FILE handoff, and kernel budget —
of which **kern_small has 0 bytes and kern_big 512** (docs/KERNEL-MEMORY.md;
raising `KERN_BUDGET` is a decision taken with whoever asks, per CLAUDE.md).
What it would buy: Loom's Pack could end in "and run it", one gesture
instead of two-and-a-click. The Reload loop (§2.2) dissolves the IDE's need
at zero kernel cost, so the conversation happens only if the loop proves
slow in real use — and whoever opens it starts from this paragraph's
arithmetic.

### 4.2 The pending XT field readings

The 10–30k ops/s contract is design arithmetic until iron says otherwise,
and the two shipped interpreter precedents are both explicit that their XT
numbers are pending: RunCPM self-measures and **banners its effective Z80
clock** with the field reading outstanding (SPEC.md §74), and C64's
percent-of-real-speed figure is in the same state (docs/C64-SPEC.md).
WEAVE inherits the discipline — About banners `WVM: <n> ops/s (measured)` —
and wave 5 **commissions the field run** (docs/FIELD-MACHINES.md binds how
it is asked for and read) that converts WEAVE-SPEC §4.12 from design figure
to measurement. If the reading lands below ~10k, handler budgets and the
`ontick` feature shrink; no performance claim ships on emulator evidence
alone (docs/TESTING.md).

**Commissioned, and it is request W1** in docs/FIELD-MACHINES.md's "Standing
requests, unanswered" — where a request lives until it is answered, at which
point it moves into PERFORMANCE.md Part 9 with its provenance lines. Wave 5
built the two things it needs, and neither existed before:

- **the banner itself.** WEAVE-SPEC §4.12 said About would carry
  `WVM: <n> ops/s (measured)` and nothing did. It does now, in About and
  again in Bundle Info — the second is not a duplicate, because About is a
  toast and retires itself in about three seconds, which is not long enough
  to copy a five-digit number off a 5150's screen by hand. Only EXHAUSTED
  slices are counted, which is WEAVE-SPEC §4.10's own rule and the only
  honest window, and WEAVE-SPEC §4.12 pins the arithmetic so a reader can
  re-run it.
- **the canvas's three counters** — frames, blits and the staging ring's
  dropped-record count — in `WEAVE.WSM`'s state block, printed by Bundle Info
  and read by `tests/weavegame`. `blits/frames` is WEAVE-SPEC §14's row, and
  `ovf` is input overrun, which is the only one of CLAUDE.md's three
  emulator-invisible defects this family can turn into a number at all.

**What is already settled and is NOT being asked.** MartyPC is exact about
how much work the guest does (PERFORMANCE.md Part 4), so the CALL count is
container evidence and needs no iron: three consecutive runs on a 5150/CGA
gave 1.00–1.06 gfx calls a frame at 17.7–18.7 fps, and Hercules 0.95 at 18.6.
What the 5150 is asked for is the ops/s reading, the milliseconds, and the
two things only a person watching can answer — does the ball move smoothly,
and does anything flicker.

### 4.3 Syntax highlighting in Loom

Deferred with the arithmetic: the only styled-text substrate in the tree is
Word's CHP model (SPEC.md §68.3), a byte of character properties per
character — **double the document memory** for every open source file — and
no editor in the tree has ever shipped a highlighter, so there is no
precedent to transplant and a real one to invent. Loom v1 edits plain text
with the Note Pad engine as-is; the fork reopens only if someone brings the
memory and the mechanism together.

### 4.4 The arkanoid-mode note

The panel's critic caught the memory study asserting a blanket "JS runs on
UI-task ONWAKE slices, no worker" — contradicted by a shipped precedent
nobody had read: arkanoid's own header says the game **is** the worker
task, sleeping one tick a frame for ~18 fps. The adopted design runs the
windowed `<canvas>` game exactly that way (WEAVE-SPEC §6.10). The *other*
shipped game precedent — missile command's `OSAPI_FSX_RUN` fullscreen
bracket with its frame clock (SPEC.md §53) — is deferred: v1's canvas is
windowed, arkanoid-class, and an FSX mode would be a new bracket tenant
with §53's whole obligation set. Both models are real; the design picked
per workload rather than per doctrine, which is the note's point.

### 4.4.1 `os88ui_sbar` has no minimum height, and nothing else can reach it

Found while wave 2 wrapped the shared scroll bar. `os88ui_sbar`
(`apps/os88ui.inc`) draws its two arrow-cell rules at `y1 + OS88UI_SBCELL`
and `y2 - OS88UI_SBCELL` — 10 px in from each end — **unconditionally**. A
bar shorter than 20 px crosses them; one 8 px tall draws an hline outside
its own rect, which is a package painting where it does not own the pixels.

It has never fired: both kernel callers and every shipped package give it a
bar the height of a window's content area. Weave is the first caller that
can be handed an arbitrary height, because `<list rows>` is an app author's
number — hence WEAVE-SPEC §6.8's pack-time refusal below 3 rows, which is
the fix at the level that can see the author.

**Not fixed in `os88ui.inc` here, deliberately.** The bar is shared with the
kernel (`%ifdef OS88UI_KERNEL`), a clamp changes what every existing caller
draws, and no current caller can reach the broken range — so the change
would be untestable against its own precedent and would edit a file this
wave has no other reason to touch. Whoever gives the bar a floor should do
it with the kernel's two callers in front of them, and can then relax
WEAVE-SPEC §6.8's refusal.

### 4.4.2 `os88line_draw` forces CBLACK, so a greyed field cannot exist

Found while wave 3 wrapped the shared one-line field. `os88line_draw`
(`apps/os88line.inc`) sets `CBLACK` for its frame and again for its text,
unconditionally — so a field drawn under SPEC.md §47's pen comes out with a
**solid** frame and **dithered** letters. That is rule 2's own failure: two
halves of one control disagreeing, which reads as a mislabelled live control
rather than a disabled one, and on the two 1bpp adapters that is the whole
difference.

It has never fired, for §4.4.1's reason exactly: the browser's location bar
and Telnet's host box are never greyed, so no existing caller can reach it.
Weave is the first, because `<input disabled>` is an app author's attribute
(WEAVE-SPEC §3.3).

**Not fixed in `os88line.inc` here, deliberately** — and this is the same
judgment as the scroll bar's, made twice now rather than once by habit. The
file is shared with the browser and Telnet, honouring the pen changes what
both draw, and neither can be handed a greyed field to test the change
against. So WEAVE paints a DISABLED field with its own `wd_input` (which
takes the pen the caller set) and a live one with the real editor; the split
is two lines in `w_infield` and it is written down at both ends. Whoever
gives `os88line.inc` a pen should do it with its two existing callers in
front of them, and can then delete WEAVE's second painter.

The pair of these — a shared control with no minimum height, and a shared
control with a hard-coded colour — is worth reading as one finding: **the
`apps/*.inc` library was written by and for callers who all wanted the same
thing**, and the first caller whose numbers come from an app author finds the
places where "the same thing" was assumed rather than parameterised.

### 4.4.3 A `.WAB` cannot show a bundle formula's source, and the format is why

WEAVE-SPEC §6.9.3 makes the formula bar load a bundle formula as `=?`, and
that is a format decision rather than a runtime shortfall: WEAVE-SPEC §2.9
carries compiled RPN and no formula text, so the only ways to show what an
author wrote are to decompile the RPN — a third implementation of
WEAVE-SPEC §5.1 to keep in step with the other two — or to carry the source
in the bundle.

The second is the real option and it is deferred with its arithmetic: a
SOURCE section already exists (WEAVE-SPEC §2.13) and is off by default, and a
per-cell source table would cost roughly the sheet's own formula text against
the 62KB cap (WEAVE-SPEC §2.1). SHEET's six formulas are 61 bytes; a 500-formula sheet at 20
characters is 10KB, which is a sixth of the cap for a feature that matters
only when somebody clicks a cell. The wave that wants it should measure a
real sheet first.

Committing over a `=?` replaces the formula, which is the operation the user
was reaching for — so the gap costs a look, never an edit.

### 4.6 The `ontick` budget at LOAD, and why the runtime does not re-check it

Wave 5 drafted this as a wave-5 deliverable — WEAVE-SPEC §13.1's row says
"ontick budget enforcement" — wrote the amendment, and then reversed it after
reading what WEAVE-SPEC §4.11 already covers. The record is here rather than in the spec
because it is a decision and not a contract.

**The argument for a load-time check** was WEAVE-SPEC §10.4's own opening: a `.WAB` on a
disk need never have been through a packer, so a runtime that trusts a
pack-time refusal is trusting a file. That is right for every other WEAVE-SPEC §10.5
rule the validator already re-checks.

**What killed it** was the second half of the same argument: that WEAVE-SPEC §4.11's
runaway alert could not reach an `ontick` handler, because the counter is
armed per dispatch and an `ontick` is dispatched every frame. `w_startt` is
armed at each `wvm_begin` — once per **handler invocation**, not once per
frame — so a handler that never finishes never re-arms it and the alert fires
at 90 ticks like any other. The hang is covered. What the pack-time bound
protects is the frame rate: a handler that *finishes* but costs 600 ops eats a
third of the VM's second at 18 fps, and WEAVE-SPEC §4.9 rule 3's collapse turns that into
an app that runs slowly rather than one that falls over.

**What it would cost.** An exact check needs an operand-length table for all
38 opcodes. The runtime does not carry one and has no other use for one:
`wvm.inc` decodes each operand inside its own op body, which is what makes the
dispatch a two-instruction jump. Generating a second table that both cores
have to agree about is the kind of duplication WEAVE-SPEC §12's whole differential
apparatus exists to avoid, for a bound whose failure mode is slowness.

**What was rejected along the way.** WEAVE-SPEC §2.8 packs functions contiguously, so a
function's BYTE length is `next offset - this offset` and needs no table at
all — and 64 ops is at most 192 bytes, so `len > 192` refuses the extreme
cases. It also passes a hundred single-byte ops, which is enforcement in name
only, and a check that is called enforcement and is not is worse than none.

Whoever reopens this starts here, and the cheapest honest version is probably
a **format** change rather than a runtime one: one byte of op count per
function in the CODE table, written by both packers, checked by the validator
in one compare. That is a `.WAB` version bump and belongs to whichever wave is
already making one.

### 4.5 The stale slot count in os88.h

`apps/cc/os88.h:141` says the C thunk layer covers "90 of the 134 slots";
the actual count is **143** `%define OSAPI_` slots today (verified by
enumeration; SPEC.md §20.3's own span figure has drifted the same way). The
fraction the C shell can reach is therefore *uncertain*, and WEAVE's shell
is C. **Before wave 2 assumes any slot is reachable from C, the thunk
coverage gets recounted against the real 143** and os88.h's comment
corrected — otherwise the family inherits a number that was wrong before it
started.

---

## 5. Wave status

### 5.1 Wave 1 — this PR

Wave 1 is the executable spec, host-side only — no 8086 code, no kernel
change, no C package:

- `docs/WEAVE-SPEC.md`, the binding contract, complete enough that
  `weavesim`, `t_wab` and later the 8086 runtime are written from it and
  from nothing else;
- this design record;
- `tools/weavesim.py` — parser, compiler, packer, both VMs, flow walk,
  cost model, `--selfcheck`;
- `apps/weave/demos/` — the FORM, SHEET and PONG sources, committed and
  deterministic;
- `tests/unit/t_wab.py` — the independent bundle reader, a fast-tier row;
- the Makefile packing the demo `.WAB`s in `all` (pure Python, no C
  toolchain needed — this is what lets a fast row exist) and the
  `build/.weave-hostchecks` stamp wave 2 will depend on;
- CLAUDE.md's docs-table rows, and the `Set 65` → `Set 68` citation fix
  (§3's table).

### 5.2 Waves 2–7

WEAVE-SPEC §13.1 carries the binding table; the shape in one line each,
every wave gated before the next begins:

| wave | ships | gated by |
|---|---|---|
| 2 | the WEAVE viewer — Frotz accept idiom, flow walk, static components | `weavesmoke` on both 1bpp adapters |
| 3 | interaction + the WVM — event ring, adaptive slices, Reload | the raw-QEMU differential corpus, then session replay and the SPEC.md §7.3 latency bar |
| 4 | `<grid>` — band composer, per-row damage, FX VM, sliced recalc | recalc-vs-model and the incremental-equals-full pixel identity |
| 5 | `<canvas>`/`<sprite>` — worker loop, masks, collision, **and `WEAVE.WSM`, a second resident segment** (§2.9) | `weavecanvas` FIRST, then `weavegame`; the field run is COMMISSIONED (§4.2) |
| 6 | ~~Loom~~ **SHIPPED** — editor transplant, overlay compilers, Pack; Preview's PLUMBING but not its picture (§5.3) | on-machine pack **byte-identical** to `weavesim --pack` |
| 7 | distribution — `make weavedisk` ×3 geometries, vm targets, allapps rows | the release checklist, and the 256KB one-app refusal exercised on the `xt` target |

The size line is watched every wave: `os88pkg.py`'s resident count against
the ~52KB target, 55,000 bytes the overlay-split trigger, the OVL
candidates pre-named (WEAVE-SPEC §1.2) so the split is a move, not a
scramble.

### 5.3 Wave 6's own size line, and the one thing it could not afford

LOOM is a SECOND package with a ceiling of its own, so wave 6 opens two size
lines rather than moving one. It closed at **54,648 image + 6,198 bss =
60,846 resident, 594 under** SPEC.md §20.1's 61,440 (54,752 and 490 under
after the merge with `main` — the same shared-SDK growth §5.2 prices for
WEAVE, paid here without a cut because the headroom absorbs it), with `LOOM.OVL` at
42,902 — and WEAVE did not move a byte: exactly wave 5's number,
verified by rebuilding `weave.bin` and comparing it whole after each of the
two changes LOOM needed there (`wfx_frac` extracted into a shared `.inc`, and
an `#ifndef` around `wfxc.c`'s output buffer).

**Where the 594 went, and it is the interesting half.** An overlay's CODE is
free — `LOOM.OVL` carries 42,902 bytes of compiler and costs the resident
image nothing — but SPEC.md §73.14's rule is that *"every global, literal and
bss byte it names stays resident and DS-relative"*, and a compiler is made of
sentences. WEAVE-SPEC §10.5 pins forty-odd of them and every one is a
`const char *` in `.rodata`. So the resident cost of the pack step is its
VOCABULARY, not its logic, which is why WEAVE-SPEC §11.4 puts every compiler
TABLE in a heap claim reached through accessors: a 5,000-byte component table
declared as a C array would have been 5,000 resident bytes for a body that
runs once per Pack.

**And it is why Preview ships without its picture** (WEAVE-SPEC §1.7.1).
Rendering the compiled stream needs `apps/weave/wflow.c` and
`apps/weave/wpaint.c` — 880 lines that name the component value arrays, the
field pool, the grid's cell store and the canvas seam — and ~500 bytes will not
host them. Writing a SMALLER painter is what WEAVE-SPEC §1.2 forbids by name,
and it would be the worse failure: a Preview that draws a different picture
from the runtime's is worse than one that draws none, because the whole point
of the pane is to answer "what will this look like" before `^R`. The price of
doing it properly is the one §2.9 already worked out for a different subject —
a second segment — and it is a wave-7 row rather than a corner cut here.

**As of wave 5 it is 60,320 resident against SPEC.md §20.1's 61,440 ceiling —
1,120 bytes — and the wave that got there did it by putting its code in a
SECOND SEGMENT** (§2.9, WEAVE-SPEC §1.2.2) rather than by finding room. Wave 5's
first build was 62,850, 1,410 over; five structural cuts brought it under, of
which two were the honest savings §2.9 asks a wave to take first and three
were the wave's own C compiling fatter than it read. `WEAVE.WSM` is 4,593
bytes and `WEAVE.OVL` 20,740.

**Then the merge with `main` took the 32 bytes and 42 more — 61,514, 74
over — and a sixth cut answered it: 51,124 + 9,196 = 60,320 resident, 1,120
under.** The overrun is worth naming because no Weave source caused it: the
Elendilon work `main` carried grew the SHARED SDK includes (`os88api.inc`,
`os88ui.inc` — the scroll bar's arrow buttons and arrow-drag, which `<list>`
and `<grid>` reach, so the feature cannot be `%define`d out), and a package
that shares a library pays for the library's growth. **The cut is duplicated
DATA, and it is the last cut of its kind this file knows of**: the validator
(WEAVE-SPEC §10.4) answers with the name of the field that refused, and
SmallerC emits a string literal once per site — `section table` eighteen
times, `prop block` twenty-one — while a literal an `ovl_` function names
stays RESIDENT even though its code does not (SPEC.md §73.14). Spelling the
twenty-four names once each in `apps/weave/wval.c` is **1,194 bytes** with
`.text` byte-identical, so it costs no call on any path. **Do not re-propose
it, and do not re-propose the wave 3–5 cuts below**: `w_idseen`, `w_msg`, the
per-component rect table, the layout record's `row` field, the dirty bitmap,
`w_ctname`'s switch, the sprite atom map, the ops/s arithmetic, `w_gband` and
`w_fxc_out` aliased into `w_probe`, the comp_id tables at 251, and the
word→byte arrays are all spent. What follows is wave 4's record, kept because
its arithmetic is what wave 5 planned against.

**As of wave 4 it was 60,862 resident against SPEC.md §20.1's 61,440 ceiling
and the pre-named list is spent for the second time.** Wave 3 spent tenants
1–5 and wave 4 added and spent 6 and 7, which is the mechanism working as
designed; what is left in WEAVE-SPEC §1.2.1 is two tenants that do not exist
yet (8 and 9) and one that is listed with its own disqualification (10, the
flow walk, which a card switch reaches mid-run and W_PAINT reaches on a path
that may not refuse). Wave 4 also spent the easy structural savings — the
per-component rect table, the layout record's `row` field, the dirty set's
byte-per-component — so **wave 5 opens with 578 bytes and no shortcut**, and
its canvas work has to name a tenant of its own before it writes one. That is
not a surprise this plan can absorb quietly, and it is written here rather
than discovered in the size line's error message.
