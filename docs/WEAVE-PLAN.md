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
lesson of that project (docs/BROWSER-PLAN.md). A bundle's source exists in
full at pack time, so runtime parsing would buy nothing and cost a resident
parser plus parse-time on a 4.77 MHz critical path. Everything compiles in
the packer — host-side `weavesim --pack`, on-machine `LOOM.OVL` — and the
two packers are held **byte-identical** (WEAVE-SPEC §11), which is the
`tools/wordfmt.py` two-independent-implementations pattern doing double
duty: it is also what makes an on-machine compiler trustworthy at all. The
host reference is written first on the `tools/htmsim.py` precedent, which
found three real bugs before any 8086 code existed.

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

---

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
| 5 | `<canvas>`/`<sprite>` — worker loop, masks, collision | wirefps/wireflick, and **the field run** (§4.2) |
| 6 | Loom — editor transplant, overlay compilers, Pack, Preview | on-machine pack **byte-identical** to `weavesim --pack` |
| 7 | distribution — `make weavedisk` ×3 geometries, vm targets, allapps rows | the release checklist, and the 256KB one-app refusal exercised on the `xt` target |

The size line is watched every wave: `os88pkg.py`'s resident count against
the ~52KB target, 55,000 bytes the overlay-split trigger, the OVL
candidates pre-named (WEAVE-SPEC §1.2) so the split is a move, not a
scramble.
