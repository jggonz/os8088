# Weave & Loom — web-style apps, compiled to a bundle, interpreted natively (`apps/weave/`)

**The binding contract for the Weave family.** It stands to `apps/weave` as
`docs/C64-SPEC.md` stands to `apps/c64`: every byte offset, opcode number,
atom id, refusal sentence and layout the family depends on is pinned here,
and the change goes in here *before* it goes in the code. The design record —
why each fork was decided, the judged alternatives, the deferred items — is
`docs/WEAVE-PLAN.md`.

It lives outside SPEC.md on the C64 precedent. **A bare `§N.M` in this file
is a section of THIS file**; SPEC.md's sections are always cited in full as
`SPEC.md §N`, and other documents by name (`C64-SPEC §1.4`,
`WEAVE-SPEC §2.2` from outside). `tools/checkdocs.py` resolves all three.

Three implementations are written from this document and from nothing else:
`tools/weavesim.py` (the host reference — parser, compiler, packer, both
VMs, layout and cost model), `tests/unit/t_wab.py` (an independent reader of
the bundle format, sharing no code with the packer — the
`tools/wordfmt.py`/`tools/os88doc.py` pattern), and later the 8086 runtime
itself. **If an implementer has to guess a byte, this document has a bug**;
fix the document first.

Every number in this file is either measured (and names its source), taken
from the platform's own constants (and cites the section that owns them), or
pinned here as this contract's own decision. All multi-byte integers in every
structure this file defines are **little-endian** — the 8086's own order.

---

## 1. The family and its shape

### 1.1 What Weave is

Weave runs **web-style applications** — markup, script, formulas — on a
4.77 MHz 8088 by inverting the browser: the components are native (an
os88ui-wrapped form set, a band-composed spreadsheet grid, a worker-driven
sprite canvas), and the app's markup and script are **compiled at pack time**
into one `.WAB` bundle that the runtime interprets as a display list plus
event-handler bytecode. Nothing on the machine ever parses WML or WJS text;
the machine interprets bytecode, walks a compiled UI stream, and draws
through the same priced primitives every shipped app draws through
(CLAUDE.md's cost table; PERFORMANCE.md Part 2).

Loom closes the loop: an in-OS IDE that edits the sources and packs the
bundle on the machine, **byte-identical** to the host packer (§11), so the
whole develop–run cycle lives on the target with zero new kernel bytes.

### 1.2 The pieces

| piece | what it is |
|---|---|
| **`WEAVE.O88`** | the runtime. A C package (`CC_PACKAGE(weave,weave,WEAVE.OVL)`, SPEC.md §73) with hand-written 8086 cores for the hot loops, RUNCPM's shape (SPEC.md §74). Opens one `.WAB` per instance. Resident target ≤52KB image+bss; 55,000 bytes is the overlay-split trigger (SPEC.md §73.14) |
| **`WEAVE.OVL`** | the runtime's one overlay: refusable, UI-task-only paths — the tenant list is §1.2.1. Nothing an event handler needs mid-run lives here |
| **`LOOM.O88`** | the IDE. A separate native package (the WORD/CWORD precedent: two things may not answer to one name). Note Pad's editor engine transplanted with prefix `lm_` (the SPEC.md §68 precedent), a file-list sidebar, one editor pane |
| **`LOOM.OVL`** | Loom's one overlay: the WML compiler, the WJS compiler, the FX pre-compiler, the atom interner, the bundle writer. Pack is a menu command and menu commands may refuse — the canonical overlay tenant |
| **`apps/weave/*.inc`** | the shared component library: **paint and hit-test** cores as assembly source `%include`d by BOTH packages (the `apps/os88ui.inc` model, SPEC.md §20.5.1 — this platform's only code-sharing mechanism). They are assembly from wave 2 because they run under the gfx lock, once per callback, and are what LOOM's Preview (§1.7) paints with. **The flow walk (§7) is C in the runtime** and is not one of them: it emits no gfx call, runs over at most 250 records in microseconds (§7.2), and has exactly one caller until LOOM exists. When LOOM lands (wave 6) it takes the same walk — moved to a shared `.inc`, or called through one — and **never a second copy**: two layouts that must agree cell-for-cell (§12) is the failure §11's byte-identity rule exists to prevent, said about code instead of about bundles |
| **`tools/weavesim.py`** | the host reference implementation, written FIRST (the `tools/htmsim.py` precedent): parser, compiler, packer (`--pack`), WVM and FX interpreters, flow-walk layout, gfx-call cost model (`--costs`, §14), `--render`, `--emit-optab`, `--emit-foldtab`, `--selfcheck`. Deterministic, byte-for-byte |
| **`tests/unit/t_wab.py`** | the independent second reader of `.WAB`, written from THIS file, sharing no code with any packer |

What runs where at run time: kernel callbacks (`W_ONCLICK`, `W_ONKEY`, …,
all under the gfx lock) are handled **entirely natively** — hit-test, widget
arm/fire, caret, selection XOR, scroll — and only ENQUEUE an 8-byte event
record into the VM's ring (§4.9), post `OSAPI_WM_WAKE`, and return. The WVM
drains the ring in adaptive `OSAPI_WM_ONWAKE` slices (§4.10). One worker
task exists only while a `<canvas>` game runs (§6.10) and obeys
SPEC.md §20.6 to the letter — it never touches a file, a memory slot, or
anything a worker may not.

#### 1.2.1 The overlay's tenants, in the order they move

The split is by **FREQUENCY** and not by size (SPEC.md §73.14): a keystroke's
path stays resident, a once-per-open or once-per-command path can go out,
because a menu command may refuse and a keystroke may not. The list is
ordered, and a wave that crosses the 55,000-byte trigger moves the next
entries until it is under with room for the wave after it — the point of
naming them in advance is that the split is a move rather than a scramble.

| # | tenant | how often it runs | shipped in |
|---|---|---|---|
| 1 | verbose bundle diagnostics (Bundle Info) | a menu command | wave 2 |
| 2 | About | a menu command | wave 2 |
| 3 | **the bundle validator** (§10.4) | once per open; the runtime's largest single body of code | wave 3 |
| 4 | **the load path** — the size probe, the capability tests, the directory search, the claim and the read, the component birth state, the field pool's assignment, the menu build, the VM bind and §2.6.2's module-init call | once per open, and once per `^R` (§1.7), which is a COMMAND keystroke and not an editing one | wave 3 |
| 5 | **`saveState` / `loadState`** (§8.3) | a builtin, so MID-RUN — the one stated exception, below | wave 3 |
| 6 | **the grid's load path** — the grid claim, the CELLS section read into the cell store, the formula cells' pool slots and the first recalculation's arming (§5.6) | once per open, beside tenant 4 and for tenant 4's reason | wave 4 |
| 7 | **the formula bar's COMMIT** — §6.9.3's classification, §6.9.2's compiler and the cell write | once per Enter in the bar; a human's gesture, never a script's | wave 4 |
| 8 | state import/export dialogs | a menu command | — |
| 9 | formula-function help | a menu command | — |
| 10 | the flow walk's NATURAL SIZES (§7.3) | once per open and per resize — **not** movable while `app.go()` can reflow from a handler (§6.12) | — |

Entries 8 and 9 do not exist yet. **Entry 10 is listed with its own
disqualification**, because it is the obvious next thing to reach for and it
is wrong: a card switch runs the walk from inside a handler, so the walk is
mid-run by §6.12's own design and moving it would put a refusable call on the
path a running script takes.

**Entries 6 and 7 were added in wave 4 and the list they joined was spent** — 3, 4 and 5
had all moved, 7 and 8 did not exist and 9 is disqualified above — so it is
worth saying what qualified it rather than letting a wave extend the list by
reaching. It runs exactly once per bundle, at open, on the UI task, from
inside tenant 4's own body; it draws nothing and no handler can reach it; and
its refusal already has a meaning, because a grid claim that cannot be had is
§10.1's sentence and that path exists whether the overlay loads or not.
Everything ELSE the grid does — the band composer, the FX VM, the display
conversion, the selection, the sliced recalculation — is on a keystroke's or
a handler's path and stays resident, which is why wave 4 spends the size line
rather than saving it.

**Entry 7 was the compiler, and §6.9.2 said it was resident until the size
line said otherwise.** That draft's argument was that "your formula did not
compile because a module would not load" is not an answer a spreadsheet may
give — and it is a worse answer than "it compiled", but it is the SAME answer
tenant 5 already gives about `saveState`, on the same terms: the refusal
already exists (§6.9.2 has a `Formula:` line for a formula that will not
compile, and a module that will not load is one more reason it did not),
the path is UI-task-only by construction (a script cannot reach the compiler
— §8.5 gives WJS no way to write a formula, only a value), and it runs once
per Enter rather than once per keystroke. What decided it was arithmetic
rather than taste: the compiler and the commit are ~6,000 bytes, wave 4's
resident code without them is already at the ceiling, and the alternative
was a wave that does not fit on the machine. The paragraph §1.2 asks a
tenant to be able to write is this one.

**Entry 5 is the one exception to "nothing an event handler needs mid-run",
and it is stated rather than stretched.** `saveState()` and `loadState()` are
builtins: a script calls them from inside a slice, which is exactly the case
the rule exists to protect. Three things make them the exception and nothing
else is:

- **The refusal already exists and is already the answer.** §8.3 says the
  pair "returns false — never a crash — on refusal (no room, no file, no
  `SYSTEM/APPDATA`, write-protected disk)". A module that will not load is
  one more reason the state was not written, arriving on a path the app
  already has to handle. Every other mid-run body would have to invent a
  meaning for a refusal.
- **They are already the slowest thing a handler can do.** Both take a
  transient claim and touch a floppy — ~400 ms of `int 13h` on the target
  (CLAUDE.md's cost table) — so one more module read is a fraction of a cost
  the app author already chose to pay, where on any drawing or arithmetic path
  it would be the whole cost.
- **They cannot be called from a worker**, so the overlay's UI-task-only rule
  (SPEC.md §73.14) is met by construction: §8.3 puts them on the UI task
  inside the ONWAKE slice, and SPEC.md §20.6 rule 7 is why.

An implementation that moves anything else mid-run has to write a paragraph
like this one for it, and if it cannot, the body stays resident.

**What a refused overlay costs, per tenant, and it must be stated rather than
discovered.** A refused load returns 0 (`apps/cc/crt0.asm`), so every tenant
whose natural answer is a pointer or a count has to be written to answer
"did it run" separately from "what did it say" — the validator's `const char *`
where 0 means VALID is the worked example (§10.4). With tenants 3 and 4 out,
a missing or stale `WEAVE.OVL` means **no bundle opens at all**, refused with
the sentence naming the overlay rather than the bundle. That is the price of
the split and it is paid once, visibly, on a disk somebody has taken the
package off without its module — which is why `make weavedisk` puts both in
one folder and `weavesmoke` boots that folder.

### 1.3 The languages, and where compilation lives

| language | what it writes | compiled to |
|---|---|---|
| **WML** (§3) | the UI: elements, attributes, events, style | UISTREAM + PROPS + ATOMS sections |
| **WJS** (§4) | event handlers: a small, C-like statement subset of JS | CODE section bytecode |
| **FX** (§5) | spreadsheet formulas in `<grid>` cells | FXCODE section RPN |

All three compile at pack time — host-side in `weavesim --pack`, on-machine
in `LOOM.OVL` — and the two packers must produce **byte-identical** output
(§11). The runtime never sees a line of source; a bundle that carries its
source (the SOURCE section, §2.13) carries it for re-editing, not for
execution.

### 1.4 The instance model, with the arithmetic

**Instance-per-app.** Every open Weave app is a full WEAVE instance —
its own copy of image+bss in its own segment, which is the platform's grain
(SPEC.md §20.1) — plus its claims. The rejected alternative (one instance
hosting several apps) would need document-passing into a running instance,
which `OSAPI_ARG_FILE`'s read-and-clear contract cannot do (SPEC.md §54.5:
each double-click opens a new instance by construction), would serialize
every app through one VM and one event ring, and would let one runaway
script take down every open app.

The accepted cost, priced. One typical instance:

| piece | KB |
|---|---|
| WEAVE package region (image+bss, target) | ~52 |
| bundle claim (§2.1; typical 8–24, cap 62) | 8–24 |
| VM claim (§4.7; default 16, cap 32) | 16 |
| grid claim (§5.6; only with a `<grid>`) | 0–26 |
| canvas claim (§6.10; only with a `<canvas>`) | 0–8 |
| **typical instance total** | **~75–120** |

A sixth, transient claim (2–8KB) exists only during `saveState`/`loadState`
staging (§8.3). Six claim records at peak against the 8-per-owner cap
(SPEC.md §50.2) — two spare.

Against the machine ladder — heap figures are `tools/kernsize.py`'s, the
authority per docs/KERNEL-MEMORY.md (SPEC.md §61.4's 551/167 figures are
stale and are not quoted here):

- **640KB machine, ~524KB heap:** four to five typical Weave apps open at
  once, or two plus Loom. Loom + one WEAVE + Finder leaves ~250KB spare.
- **256KB XT, ~140.5KB heap:** exactly **ONE** Weave app at a time. A full
  spreadsheet instance (~110KB) fits with ~30KB slack; the second launch
  refuses **before any I/O** with §10.1's sentence naming both figures.
  This is stated here and tested on the `xt` target, not discovered.
- **128KB machine, kern_small, ~22KB heap:** WEAVE refuses at launch with
  the arithmetic. kern_small also refuses `GFX_BLIT1`, `WM_TIMER` and
  `WM_ONDRAG` by CF=1, so the floor machine for the family is **256KB**.

### 1.5 The launch story

**No launch API exists and none is invented.** None of the 143 `OSAPI_*`
slots launches a package; `loader_run`/`ld_run_name` are kernel-internal,
reachable only from a Finder click, the dock, or the association sweep. The
family therefore launches the Frotz way, unmodified:

1. **Double-click a `.WAB`.** WEAVE declares the extension in its 16-byte
   `OS88_ASSOC16` header block (SPEC.md §54.6), harvested on the mount's
   first-sector read, so the association works on first sight with no prior
   run. The entry proc banks `OSAPI_ARG_FILE`'s name + DX + BL (read-and-
   clear, SPEC.md §54.5 — and reloads BX, SPEC.md §54.8's documented trap),
   and the first `W_PAINT` spends the banked locator: `GOTO` → `FIND` (size
   from the snapshot) → refuse-or-`READ`, sharing one load path with the
   File → Open dialog route. `apps/frotz`'s `zi_openpend` is the template,
   copied line by line, because SPEC.md §54.8's three traps each shipped
   broken once.
2. **Loom registers `.WML` and `.WJS`** the same way, so double-clicking a
   source opens the IDE.
3. **WEAVE launched empty shows the Deck** (§1.6) — "launch" is an internal
   function of the runtime, not a kernel surface.

A kernel launch-by-name slot is recorded in docs/WEAVE-PLAN.md as an
explicitly **deferred owner conversation** with the budget arithmetic
attached (kern_small at 0 bytes spare, kern_big at 512) — an upgrade path,
never a dependency.

### 1.6 The Deck

WEAVE started with no `ARG_FILE` (dock click, Finder click on the package)
puts up **the Deck**: a launcher window listing the `.WAB` files in the
instance's current directory plus the recents kept in `SYSTEM/APPDATA/`
(SPEC.md §19.9's bank/GOTO/act/GOTO-back idiom, tolerating absence).
Selecting an entry loads it through the same one load path as §1.5's two
routes. Each row carries the bundle's 16×16 icon (the ICON section, §2.12)
and its name from the header. The Deck is **normative here from wave 1**;
wave 2 may ship File → Open only, but this is where launch-empty lands.

### 1.7 The edit–run loop

Pack (`^P` in Loom, §11) → click the open WEAVE window → **`^R` Reload**.
`File → Reload` re-reads the current bundle from disk into a fresh claim,
re-runs the flow walk, restarts the VM; the app's `.SAV` file (§8.3) is
untouched. Two keystrokes and a click per iteration, zero kernel bytes.
Only the first-ever run of a new bundle takes a Finder double-click.

**The keystroke is `Ctrl-R`, written `^R`, and it is not a menu
accelerator.** Earlier drafts of this section said "Cmd-R", which is the
Macintosh spelling the whole system borrows its *look* from and not
something this machine has: there is no command key, and no `OSAPI_*` slot
binds a key to a menu item — `OSAPI_MENU_SET` (SPEC.md §12.2) draws and
tracks a bar and nothing else. A package that wants a shortcut reads it in
its own `W_ONKEY`, and the item's label says so, which is Note Pad's
convention verbatim (`Open...  ^O`, `Replace...  ^R`). So WEAVE's File menu
carries `Reload  ^R` and `os88_onkey` acts on **ASCII 0x12** — a control
character, which `apps/os88line.inc` hands back to its caller rather than
inserting (`os88line_key`'s `.ctrl` arm), so the shortcut and a focused
`<input>` cannot fight over it. A bare `r`, which wave 2 shipped because
nothing could type yet, would have been eaten by the first field on the
card.

Loom additionally offers **Preview** — the compiled UI stream rendered in a
child area by the SAME shared component includes WEAVE paints with; widgets
draw and arm/fire natively but no bytecode runs, and the pane is labelled
`Preview: layout and controls only - Run runs the app`.

---

## 2. The .WAB bundle

### 2.1 The rules that shape the format

- **One packed 8.3 file** (`MYAPP.WAB`), read whole into a single pinned
  heap claim. A claim base is KB-aligned, so SPEC.md §2.1.1's 512-alignment
  rule for the read is met by construction.
- **Size-refused BEFORE any disk I/O**: the directory-entry size, plus the
  header-declared claim asks (§2.2), against `OSAPI_MEM_AVAIL` — the Frotz
  rule (SPEC.md §61.4). Which is exactly why the format is **never
  compressed**: the directory size must stand for the resident requirement,
  or the refusal cannot happen before the read.
- **Hard cap 62KB — total size ≤ 0xF800 (63,488 bytes)** — so every
  internal offset is a 16-bit word within one segment. The browser's own
  arithmetic ceiling (SPEC.md §71), inherited deliberately.
- **Every section 16-byte aligned** (file offset a multiple of 16), so a
  section can be addressed as `segment:0` from the claim's base paragraph —
  the `.PIX` precedent. Padding bytes between sections are `0x00`.
- **Little-endian words and dwords throughout.**
- The bundle is **read-only at run time**. Mutable state lives in the VM
  claim, the grid claim, and the `.SAV` file — never written back into the
  bundle.
- Every byte read off the disk is hostile (SPEC.md §19): the runtime
  validates §2.2–§2.3 before believing an offset, and refuses a malformed
  bundle with §10.4's sentence.

### 2.2 The 32-byte header

| offset | size | field | contents |
|---|---|---|---|
| +0 | 4 | magic | `'W','A','B',0x1A` |
| +4 | 2 | version | format version, **1**. Any other value refuses (§10.4) |
| +6 | 2 | total size | file size in bytes, ≤ 0xF800. Checked against the actual directory size and the actual read |
| +8 | 2 | flags | §2.2.1 |
| +10 | 1 | vm KB | VM claim ask, 16–32 (§4.7) |
| +11 | 1 | grid KB | grid claim ask, 0 or 8–26 (§5.6) |
| +12 | 1 | section count | **5–9**, one row per section present. Five is the floor and not one: §2.4 makes UISTREAM, PROPS, CODE and ATOMS mandatory in every bundle and ICON always present, so those five rows are named by the format itself and a bundle carrying fewer refuses (§10.4) |
| +13 | 1 | entry card | card index (1-based) shown at open |
| +14 | 1 | canvas KB | canvas claim ask, 0 or 2–8 (§6.10) |
| +15 | 1 | reserved | 0 |
| +16 | 16 | app name | 15 characters + NUL, space-padded before the NUL is **not** allowed: unused bytes after the NUL are 0x00 |

The header ends at +32 exactly; the fields above sum to 32 bytes, and
`weavesim --selfcheck` asserts that sum (a prior in-house design shipped a
"128-byte" header whose fields ended at +132 — the class of error this
table exists to make impossible).

The three claim-KB bytes exist so the **whole** memory refusal is
computable from the directory entry plus the first sector: ask =
`ceil(filesize/1024) + vmKB + gridKB + canvasKB`. §10.1 gives the sentence.

#### 2.2.1 The flags word

| bit | name | meaning |
|---|---|---|
| 0 | `WABF_GRID` | the app declares a `<grid>`; grid KB byte must be non-zero |
| 1 | `WABF_CANVAS` | the app declares a `<canvas>`; needs `GFX_BLIT1` — refused on kern_small (§10.2) |
| 2 | `WABF_TIMER` | exactly three causes, any one of which sets it: CODE calls `timer()`, the app declares an `<input>`, or the app declares a `<grid>` — a grid's formula bar is a library-wired `input` (§6.9), so a grid blinks a caret too. On kern_small the caret degrades to static and `timer()` refuses (§8.2) |
| 3 | `WABF_STATE` | the app calls `saveState`/`loadState` |
| 4 | `WABF_SOURCE` | a SOURCE section is present |
| 5–15 | — | 0. A set unknown bit refuses the bundle (§10.4) |

Flags are computed by the packer, never hand-set. At load they are checked
against machine capabilities **by testing the fact** — the slot's CF answer,
not a guess about machine size (SPEC.md §47: grey a fact).

### 2.3 The section table

Immediately after the header at +32: `section count` rows of **8 bytes**:

| offset | size | field |
|---|---|---|
| +0 | 1 | type (§2.4) |
| +1 | 1 | 0 |
| +2 | 2 | section offset from file start; multiple of 16 |
| +4 | 2 | section length in bytes (unpadded) |
| +6 | 2 | extra — per-type meaning (§2.4), else 0 |

Rows are sorted by ascending type, one row per type at most. The first
section begins at `align16(32 + 8×count)`; each subsequent section begins at
`align16(previous offset + previous length)`. The file **ends at the last
section's unpadded end**: §2.2's total size equals the last section's
offset + length exactly, and no tail padding follows it — the 16-byte
padding exists only *between* sections. The reader must bounds-check
every `offset + length ≤ total size`.

### 2.4 The nine section types

| type | name | contents | extra word |
|---|---|---|---|
| 1 | UISTREAM | the compiled display list: cards and component records (§2.5) | record count |
| 2 | PROPS | per-component property blocks (§2.6) | offset of the app block within PROPS |
| 3 | CODE | WVM bytecode + function table (§2.8) | 0 |
| 4 | ATOMS | the interned string pool (§2.7) | 0 |
| 5 | FXCODE | compiled formula RPN (§2.9) | formula count |
| 6 | CELLS | initial grid values (§2.10) | cell record count |
| 7 | SPRITES | pre-rendered 1bpp sprite images + AND masks (§2.11) | sprite count |
| 8 | ICON | 64 bytes, 16×16 1bpp (§2.12) | 0 |
| 9 | SOURCE | optional: the WML/WJS text for round-trip re-editing (§2.13) | WML length in bytes |

UISTREAM, PROPS, CODE and ATOMS are mandatory in every bundle (an app with
no script still gets an empty-function-table CODE section and its label
atoms). FXCODE and CELLS appear iff `WABF_GRID`; SPRITES iff any `<sprite>`;
ICON always (the packer supplies a default icon when the project has none);
SOURCE iff packed with source carriage on (off by default — §2.13).

### 2.5 UISTREAM — the compiled display list

A sequence of **10-byte records**; `length = 10 × record count`. Positions
are **never** in the stream — the three adapters have different cell grids,
so geometry is computed by the flow walk (§7) at open and at resize.

Record kinds, selected by the first byte:

**`REC_END` (0x00)** — the final record; bytes +1..+9 are 0. Exactly one,
last.

**`REC_CARD` (0x01)** — starts a card:

| offset | size | field |
|---|---|---|
| +0 | 1 | 0x01 |
| +1 | 1 | card index, 1-based, assigned in document order, ≤ 8 |
| +2 | 6 | 0 |
| +8 | 2 | prop block offset in PROPS, or 0xFFFF (none — v1 cards carry no props) |

**`REC_COMP` (0x02)** — a component:

| offset | size | field |
|---|---|---|
| +0 | 1 | 0x02 |
| +1 | 1 | comp_id: 1–250, unique across the bundle, assigned in document order. 0 names the app itself (§2.6) |
| +2 | 1 | ctype (§2.5.1) |
| +3 | 1 | w in cells (0 = natural, §7.3) |
| +4 | 1 | h in rows of 8 px (0 = natural) |
| +5 | 1 | style byte (§2.5.2) |
| +6 | 1 | cflags (§2.5.3) |
| +7 | 1 | 0 |
| +8 | 2 | prop block offset in PROPS, or 0xFFFF |

Components follow their card's `REC_CARD`. A `<sprite>` record follows its
`<canvas>` record directly (sprites are not flow components; the walk skips
them, §7.2). Menus and items never appear in UISTREAM — they compile into
the app block (§2.6.2).

A `<canvas>`'s record carries its pixel size already divided down to the
cell grid — `w/8` at +3 and `ceil(h/8)` at +4, never 0, since both
attributes are required (§3.3). A `<sprite>`'s record carries 0/0; its
geometry lives in SPRITES (§2.11).

#### 2.5.1 ctype — the component type codes

| code | element | | code | element |
|---|---|---|---|---|
| 0x01 | `label` | | 0x08 | `check` |
| 0x02 | `text` | | 0x09 | `radio` |
| 0x03 | `rule` | | 0x0A | `input` |
| 0x04 | `box` | | 0x0B | `list` |
| 0x05 | `spacer` | | 0x0C | `grid` |
| 0x06 | `meter` | | 0x0D | `canvas` |
| 0x07 | `button` | | 0x0E | `sprite` |

Codes 0x0F+ are unassigned; a reader refuses them (§10.4). `app`, `card`,
`menu`, `item` and `script` have no ctype — they compile to other
structures.

#### 2.5.2 The style byte

| bits | field | values |
|---|---|---|
| 0 | BOLD | glyphs doubled-struck one pixel right (§6.2) |
| 1 | INVERT | ink and paper swapped for the component's cells |
| 2–3 | ALIGN | 0 left, 1 center, 2 right; 3 refused by the packer |
| 4–7 | — | 0; a set bit refuses at load |

This byte is the **whole** style vocabulary. No colors — two of three
adapters are 1bpp and half-honoured color pairs made invisible text twice
in this tree (docs/BROWSER-PLAN.md §2.2.1). The packer rejects anything
else by name (§3.5, §10.5).

#### 2.5.3 The cflags byte

| bit | name | meaning |
|---|---|---|
| 0 | `CF_BREAK` | start a new layout row before this component (§7.2) |
| 1 | `CF_HIDDEN` | hidden at open (`hidden` attribute) |
| 2 | `CF_DISABLED` | disabled at open; painted through the SPEC.md §47 pen |
| 3–7 | — | 0 |

### 2.6 PROPS — property blocks

A heap of **4-byte property records**, grouped into blocks. A block is a
run of records terminated by a record of four zero bytes. UISTREAM records
and the section table's extra word point at block starts (byte offsets from
the PROPS section start; blocks need no alignment).

| offset | size | field |
|---|---|---|
| +0 | 1 | name: an atom id (§2.7) — a property or event name. 0 terminates the block |
| +1 | 1 | kind: 0 `PK_INT`, 1 `PK_ATOM`, 2 `PK_BLOB`, 3 `PK_FUNC`, 4 `PK_SPRITE` |
| +2 | 2 | value: a signed word (`PK_INT`); an atom id in the low byte (`PK_ATOM`); a byte offset into PROPS (`PK_BLOB`); a function index (`PK_FUNC`); a SPRITES index (`PK_SPRITE`) |

Records inside a block are sorted by ascending name atom id; a name appears
at most once per block. Which names are legal on which component is §3.3's
table; a reader treats an unknown pairing as a malformed bundle (§10.4).
Event bindings are ordinary records: name = the event atom, kind =
`PK_FUNC`, value = the handler's function index.

#### 2.6.1 Well-known blob: list items

A `<list>`'s items compile to a blob: `+0` count byte (≤ 64), then count
atom-id bytes, one per item, in document order. The list's prop block
carries `ITEMS` (§2.7.1) as `PK_BLOB` pointing at it.

#### 2.6.2 The app block

The PROPS section's extra word names one block for **comp_id 0, the app**.
It may carry `CARD` (`PK_INT`, mirror of the header's entry card),
`MENUS` (`PK_BLOB`), and `start` (`PK_FUNC`): when any WJS global carries
an initializer other than int 0, the packer synthesizes a module-init
function — appended as the **last** function-table entry, its body the
initializer stores in declaration order ending `PUSHN`/`RET` — and names
it here under atom 40; the runtime runs it once at VM start, before the
first event, and §4.7's "zeroed, then initializers applied" happens by
exactly this carriage. Nothing else may appear in the app block. The
MENUS blob:

```
+0  menu count (1..5 — MENU_APPMAX is the kernel's own bound, SPEC.md §12.2)
per menu:
  +0  title atom id
  +1  item count (1..8)
  per item, 2 bytes:
    +0  label atom id
    +1  oncommand function index, or 0xFF for none (item present, inert)
```

### 2.7 ATOMS — the interned string pool

Every name and string literal in the bundle is an **atom**: a byte-sized id,
so `GETP`/`SETP`/`CALLM` carry one byte and the runtime never compares a
string at event time.

- **Atom ids 1–63 are well-known** (§2.7.1): pinned here, known to every
  implementation, **not stored in the pool**.
- **Atom ids 64–250 are app atoms**: the bundle's own strings, stored in the
  pool. Pool row *i* (0-based) is atom id `64 + i`.
- Atom 0 means "none". Ids 251–255 are reserved. A bundle needing more than
  187 app atoms is refused at pack with §10.5's sentence.

Section layout:

```
+0  count word          (app atoms, 0..187)
+2  count offset words  (byte offsets from the ATOMS section start)
then, per atom, at its offset:
  +0  length byte L (1..255)
  +1  L bytes, folded to ASCII 0x20..0x7E (§3.1's fold)
  +1+L  NUL
```

Offsets ascend in id order and strings are packed without gaps: atom 64
begins at `2 + 2×count`, and each next atom at the previous NUL + 1. Two
identical strings intern to ONE atom (the packer's interning rule, §2.14).
A string literal that happens to spell a well-known name still interns as
an **app** atom: ids 1–63 have no string table in the runtime, so a pooled
copy is the only way the literal's bytes exist at run time — every `PUSHA`
operand is therefore 64–250, never a well-known id.

#### 2.7.1 The well-known atom table

Pinned. Property atoms:

| id | name | | id | name | | id | name |
|---|---|---|---|---|---|---|---|
| 1 | `text` | | 9 | `vx` | | 17 | `sel` |
| 2 | `value` | | 10 | `vy` | | 18 | `editing` |
| 3 | `label` | | 11 | `frame` | | 19 | `group` |
| 4 | `enabled` | | 12 | `shown` | | 20 | `card` |
| 5 | `checked` | | 13 | `min` | | 21 | `selrow` |
| 6 | `hidden` | | 14 | `max` | | 22 | `selcol` |
| 7 | `x` | | 15 | `rows` | | 23 | `walls` |
| 8 | `y` | | 16 | `cols` | | 24 | `tick` |

Method atoms:

| id | name | | id | name |
|---|---|---|---|---|
| 32 | `cell` | | 37 | `go` |
| 33 | `setCell` | | 38 | `set` |
| 34 | `recalc` | | 39 | `get` |
| 35 | `select` | | 40 | `start` |
| 36 | `stop` | | 41 | `clear` |

Event atoms:

| id | name | | id | name |
|---|---|---|---|---|
| 48 | `onclick` | | 54 | `oncollide` |
| 49 | `onchange` | | 55 | `onwall` |
| 50 | `onkey` | | 56 | `onscore` |
| 51 | `onselect` | | 57 | `ontick` |
| 52 | `onedit` | | 58 | `oncommand` |
| 53 | `oncalc` | | 59 | `ontimer` (internal: `timer()` firing, never written in WML) |
| | | | 60 | `onalert` (internal: alert button, never written in WML) |

Structural atoms: 61 `ITEMS` (the list-items blob name, §2.6.1), 62 `MENUS`
(§2.6.2), 63 reserved. Ids 25–31 and 42–47 are unassigned and reserved.

### 2.8 CODE — bytecode and the function table

```
+0  function count byte F (0..128)
+1  globals count byte  G (0..128)
+2  function table, F rows of 4 bytes:
      +0  code offset word (from the CODE section start)
      +2  nargs byte   (0..8)
      +3  nlocals byte (nargs..16; args are locals 0..nargs-1)
then bytecode, each function a contiguous run ending in RET or HALT
```

Function indices are assignment order = definition order in the `.WJS`
source (the synthesized module-init function, when one exists, is last —
§2.6.2). Global indices are declaration order. The bytecode itself is §4.5.

The bytecode is **packed**: function 0's code begins at `2 + 4F`, and
every next function begins where the previous ended — no gaps, no padding
(§2.14 rule 6 leaves a packer nothing to vary). A scriptless bundle
(F = 0) still carries the section: its body is **exactly one `HALT`
byte** — the guard §4.5 names — so the section length is exactly 3.

### 2.9 FXCODE — compiled formulas

```
+0  formula count word N
+2  N offset words (from the FXCODE section start)
then N RPN streams (§5.3), each terminated by FEND
```

CELLS records reference formulas by index into this table.

### 2.10 CELLS — initial grid values

Fixed **8-byte records**, count = length/8, sorted row-major
(row, then column), only non-empty cells present:

| offset | size | field |
|---|---|---|
| +0 | 1 | row, 0-based (0..255) |
| +1 | 1 | col, 0-based (0..25) |
| +2 | 1 | kind: 1 number, 2 atom string, 3 formula |
| +3 | 1 | 0 |
| +4 | 4 | payload: a 16.16 dword (kind 1); an atom id in the low word (kind 2); a formula index in the low word (kind 3) |

### 2.11 SPRITES — pre-rendered images and masks

Sprite transparency is resolved **in RAM** at composition time — no masked
gfx blit exists on this machine (`GFX_BLIT1`/`GFX_BLIT4` are opaque) — so
every image ships beside its pre-built AND mask, both rendered at pack time
in **final screen polarity** (1 = ink), widths multiples of 8.

```
+0  sprite count byte S (1..16)
+1  0
+2  S descriptors of 8 bytes:
      +0  w_bytes byte   (width in bytes; width px = 8 × w_bytes; 1..8)
      +1  h_px byte      (1..64)
      +2  frames byte    (1..8)
      +3  0
      +4  data offset word (from the SPRITES section start)
      +6  0
then image data, in descriptor order:
  per frame: h_px × w_bytes bytes of image (OR pattern),
             then h_px × w_bytes bytes of AND mask
```

Composition rule, normative: `screen_byte = (screen_byte AND mask_byte) OR
image_byte`, per band byte in the canvas's RAM buffer. A mask bit of 0
keeps the background; the packer derives the mask as NOT(coverage) from the
`.WSP` art (§3.6).

### 2.12 ICON — 64 bytes

A 16×16 1bpp icon, 4 bytes per row, top to bottom, MSB leftmost, 1 = ink —
the same format as the package header icon (SPEC.md §21). Shown in the Deck
(§1.6) and in the runtime's About.

### 2.13 SOURCE — optional round-trip text

Off by default; `weavesim --pack --with-source` (and Loom's matching
checkbox) carries it, at the visible cost of bundle bytes against the 62KB
cap. Layout: the WML text, then the WJS text, both folded and
LF-terminated; the section's extra word is the WML length, so the reader
splits without a scan. Loom's `File → Open` of a `.WAB` with `WABF_SOURCE`
re-creates the project files from it.

### 2.14 Determinism — how two packers stay byte-identical

The pack step is deterministic by rule, not by accident, because Loom's
on-machine pack must be **byte-identical** to `weavesim --pack` (§11, §12):

1. Sections are emitted in ascending type order; inter-section padding is
   0x00; no timestamps, no host paths, nothing environmental in the file.
2. `comp_id` assignment is WML document order, starting at 1. Card indices
   are document order, starting at 1.
3. Atom interning order is **first appearance** in this traversal: (a) the
   WML document in document order — element by element, attributes in
   §3.3's table order for that element, then text content; (b) the WJS
   source in token order; (c) FX formulas in CELLS order. Duplicate strings
   intern once, at their first appearance.
4. Function indices are WJS definition order; global indices declaration
   order.
5. Property records within a block sort by ascending name atom id; blocks
   are emitted in the order their owners appear (cards and components in
   UISTREAM order, then the app block last), packed back to back with no
   gaps; blobs (§2.6.1, §2.6.2) follow **all** blocks, in the order their
   referencing records were emitted. The PROPS length is exactly the sum
   of its blocks and blobs.
6. WJS compiles by §4.6's fixed templates: no optimisation, no constant
   folding, no peephole. FX compiles by §5.3's shunting-yard with the pinned
   precedence. Identical source ⇒ identical bytes, both compilers.
7. CELLS records sort row-major; SPRITES emit in `.WSP` definition order.

---

## 3. WML — the markup language

### 3.1 Syntax

WML is an XML-shaped subset, closed and small enough to parse with a
hand-written scanner:

```
document   := ws? element ws?
element    := "<" name attrs ws? ( "/>" | ">" content "</" name ">" )
attrs      := ( ws attr )*
attr       := name ( "=" '"' avalue '"' )?
content    := ( element | text | comment )*
comment    := "<!--" any-not-"-->" "-->"
name       := [A-Za-z][A-Za-z0-9]*
avalue     := any characters except '"' and control bytes
text       := any characters except "<"
ws         := one or more of space, tab, CR, LF
```

- Element and attribute names are **case-insensitive**, folded to lowercase
  by the parser. Attribute values and text content keep their case.
- A **boolean attribute** is true when present bare (`checked`) or as
  `="1"`, false when absent or `="0"`. Any other value refuses at pack.
- Entities: exactly `&lt;` `&gt;` `&amp;` `&quot;` — nothing else, and a
  bare `&` not forming one of these four is a pack error.
- All text content and attribute values are folded to **ASCII 0x20–0x7E**
  through the browser's Latin-1 fold (the table `weavesim --emit-foldtab`
  generates from `tools/htmsim.py`'s one definition, so the model and the
  8086 cannot drift). The cell font has 95 glyphs (SPEC.md §6.1); nothing
  outside them survives to a bundle.
- Whitespace in text content collapses to single spaces; leading and
  trailing whitespace of a content run is dropped.
- Numeric attribute values are unsigned decimal integers unless a row in
  §3.3 says signed.

The closing tag must match the open tag; overlap is a pack error, not a
recovery — WML is compiled, and a compiler that guesses ships a different
guess in two implementations.

### 3.2 The element inventory

**Closed.** Eighteen elements; anything else is refused at pack with a
sentence naming the element (§10.5). This is graft-level policy, not
convenience: every exclusion in §9 is enforced at pack time, so an author
discovers a platform fact at the pack step, never at run time on a slower
machine.

| element | ctype | children allowed |
|---|---|---|
| `app` | — | `card`, `menu`, `script` |
| `card` | — | any flow component |
| `label` | 0x01 | text |
| `text` | 0x02 | text |
| `rule` | 0x03 | — |
| `box` | 0x04 | — |
| `spacer` | 0x05 | — |
| `meter` | 0x06 | — |
| `button` | 0x07 | text (the label) |
| `check` | 0x08 | text (the label) |
| `radio` | 0x09 | text (the label) |
| `input` | 0x0A | — |
| `list` | 0x0B | `item` |
| `grid` | 0x0C | — |
| `canvas` | 0x0D | `sprite` |
| `sprite` | 0x0E | — |
| `menu` | — | `item` |
| `item` | — | text (the label) |
| `script` | — | — |

Exactly one `app` per document; 1–8 `card`s; at most one `grid` and one
`canvas` per app (each owns a dedicated claim and the claim cap is 8 per
owner, SPEC.md §50.2); at most one `script`; at most 5 `menu`s
(`MENU_APPMAX`, SPEC.md §12.2).

### 3.3 Attributes, element by element

Attributes common to every flow component (`label`…`canvas`):

| attr | type | default | compiled to |
|---|---|---|---|
| `id` | name | — | required on any component a script or event names |
| `w` | cells 0–160 | 0 = natural | REC_COMP w |
| `h` | rows 0–40 | 0 = natural | REC_COMP h |
| `style` | tokens | empty | style byte bits 0–1 (§3.5) |
| `align` | `left`/`center`/`right` | `left` | style byte bits 2–3 |
| `br` | bool | 0 | `CF_BREAK` |
| `hidden` | bool | 0 | `CF_HIDDEN` |
| `disabled` | bool | 0 | `CF_DISABLED` |

Per element:

| element | attribute | type | default | limit / note |
|---|---|---|---|---|
| `app` | `name` | string | required | ≤ 15 chars — the header name field |
| | `vm` | KB | 16 | 16–32; the VM claim ask |
| `card` | `id` | name | required | compiled to card index in document order |
| `label` | — | | | content → `text` prop (atom); one line, no wrap |
| `text` | — | | | content → `text` prop (atom); wraps in the flow walk |
| `rule` | — | | | `w`=0 spans the full row |
| `box` | — | | | `w`,`h` required, ≥ 2×1 |
| `spacer` | — | | | `w` required |
| `meter` | `value` | int | 0 | 0..max → `value` prop |
| | `max` | int | 100 | 1–32000 → `max` prop |
| `button` | `onclick` | fn name | — | content → `label` prop |
| `check` | `checked` | bool | 0 | → `checked` prop |
| | `onchange` | fn name | — | |
| `radio` | `group` | name | required | → `group` prop (atom); one checked per group |
| | `checked` | bool | 0 | |
| | `onchange` | fn name | — | |
| `input` | `cols` | int | 20 | 2–60 → `cols` prop |
| | `text` | string | empty | initial contents → `text` prop |
| | `onchange` | fn name | — | fires on Enter |
| | `onkey` | fn name | — | fires per keystroke |
| `list` | `rows` | int | 8 | 1–40 visible rows → `rows` prop |
| | `onselect` | fn name | — | |
| `grid` | `cols` | int | required | 1–26 |
| | `rows` | int | required | 1–256, and cols×rows ≤ 6,140 (§5.6) |
| | `onselect` | fn name | — | selection moved |
| | `onedit` | fn name | — | a cell committed from the formula bar |
| | `oncalc` | fn name | — | a recalculation finished |
| `canvas` | `w` | px | required | multiple of 8, 64–320 |
| | `h` | px | required | 32–160 |
| | `walls` | string | `TBLR` | subset of `TBLR`: bouncing edges; missing edges are open (§6.10) |
| | `tick` | frames | 0 | 0 = no ontick; 1–255 = every N frames |
| | `onkey` | fn name | — | key transitions while the game runs |
| | `oncollide` | fn name | — | sprite–sprite AABB overlap |
| | `onwall` | fn name | — | sprite bounced on a wall edge |
| | `onscore` | fn name | — | sprite left through an open edge |
| | `ontick` | fn name | — | requires `tick` ≥ 1; handler bound by §4.11.1 |
| `sprite` | `img` | name | required | a `.WSP` sprite name → a `PK_SPRITE` record **named by atom 11 `frame`** — no `img` atom exists; the record doubles as the `frame` property's initial value (§6.10) |
| | `x`,`y` | px, signed | 0 | initial position |
| | `shown` | bool | 1 | |
| `menu` | `title` | string | required | ≤ 8 chars |
| `item` | `oncommand` | fn name | — | menu items only; content = the label, ≤ 24 glyphs |
| `script` | `src` | 8.3 name | required | the `.WJS` file; **no inline script** — the runtime never parses text, and the pack step is the only compiler surface |

Any attribute not in these tables — on any element — refuses at pack,
naming the attribute and the element (§10.5). Cell values inside a `<grid>`
come from Loom's grid editor / the demo sources' `.WFX` sheet file (§11.2),
not from WML attributes.

### 3.4 Events

The complete event vocabulary, and what each carries in its ring record
(§4.9; `data1`/`data2` are the two words):

| event | fires when | data1 | data2 |
|---|---|---|---|
| `onclick` | a button fires (arm/fire complete, SPEC.md §13.8) | 0 | 0 |
| `onchange` | check/radio toggled; input committed with Enter | new value (0/1) or 0 | 0 |
| `onkey` | input: each keystroke. canvas: key transition | ASCII (0 if none) | input: scan code; canvas: 1 down / 0 up |
| `onselect` | list or grid selection moved | new index / row | 0 / col |
| `onedit` | grid cell committed | row | col |
| `oncalc` | grid recalculation completed | changed-cell count | 0 |
| `oncollide` | two sprites' AABBs overlap this frame | comp_id A | comp_id B |
| `onwall` | sprite bounced (velocity component negated) | sprite comp_id | edge: 0 T, 1 B, 2 L, 3 R |
| `onscore` | sprite fully crossed an open edge | sprite comp_id | edge code |
| `ontick` | every N canvas frames | frame counter low word | 0 |
| `oncommand` | a menu item chosen | menu index | item index |
| `ontimer` | a `timer()` expired (internal) | function index | 0 |
| `onalert` | an alert dismissed (internal) | function index | button: 1 OK/Yes, 0 Cancel/No |

There is **no hover event of any kind** — no passive mouse-move reaches a
package on this machine (`W_ONDRAG` tracks only between the press and the
release of one gesture, and polling `OSAPI_MOUSE` alongside tracking is
forbidden, SPEC.md §13.7). The packer rejects `onhover`, `onmouseover`,
`onmouseout` and any unknown `on*` attribute with the sentence in §10.5.

### 3.5 Style — the closed set

`style` accepts a space-separated subset of exactly two tokens: `bold`,
`invert`. `align` accepts `left`, `center`, `right`. `w` and `h` size in
cells. **That is the entire styling system.** There are no colors, no
fonts, no sizes, no margins, no hover states, no CSS of any kind; the
packer rejects unknown vocabulary with a message naming the platform fact
(§10.5), because the discovery must happen at pack, never at run.

### 3.6 Sprite art — the `.WSP` source

A project with sprites carries `SPRITES.WSP`, plain text:

```
sprite <name> <w_px> <h_px> [<frames>]
<h_px rows of exactly w_px characters, per frame, frames separated by a line "-">
```

`#` is ink, `.` is background; any other character in a row is a pack
error. `w_px` a multiple of 8 (8–64), `h_px` 1–64, frames 1–8. The packer
renders each frame to §2.11's image bytes (ink = 1, final screen polarity)
and derives the AND mask as the bitwise NOT of coverage — a background
pixel inside the sprite's rectangle is **transparent**, not white; a
sprite that needs opaque white paints `#` in a frame of its own design.

---

## 4. WJS — the script language

### 4.1 The language

WJS is a C-like statement subset of JavaScript, honest about the machine
under it: **16-bit signed integers are THE number type** (this toolchain
has no long and no float, SPEC.md §73.7, and the VM inherits that rather
than hiding it), strings are immutable, arrays are fixed-size, and there
are **no objects, no prototypes, no `this`, no `new`, no closures, no
nested functions, no try/catch, no regex, no eval**. The only structured
values are component handles, resolved at pack time from WML `id`s.

JS is **event handlers only, by construction**: handlers are named in
markup (`onclick="doAdd"` names a top-level function) and there is no other
entry point — no top-level statements execute (top level is declarations
only), no per-frame callback exists (§9.3), and the game loop never runs a
bytecode op.

### 4.2 Grammar

```ebnf
program     = { vardecl | fundecl } ;
vardecl     = "var" ident [ "=" initexpr ] ";" ;
initexpr    = number | string | "true" | "false" | "null"
            | "array" "(" number ")" ;
fundecl     = "function" ident "(" [ ident { "," ident } ] ")" block ;
block       = "{" { statement } "}" ;
statement   = block | vardecl | ifstmt | whilestmt | forstmt
            | "break" ";" | "continue" ";"
            | "return" [ expr ] ";"
            | ident ( "++" | "--" ) ";"
            | exprstmt ;
ifstmt      = "if" "(" expr ")" statement [ "else" statement ] ;
whilestmt   = "while" "(" expr ")" statement ;
forstmt     = "for" "(" [ forinit ] ";" [ expr ] ";" [ forstep ] ")"
              statement ;
forinit     = assign | vardecl-no-semi ;
forstep     = assign | ident ( "++" | "--" ) ;
exprstmt    = ( assign | expr ) ";" ;
assign      = lvalue "=" expr ;
lvalue      = ident | ident "[" expr "]" | ident "." ident ;
expr        = orexpr ;
orexpr      = andexpr { "||" andexpr } ;
andexpr     = eqexpr { "&&" eqexpr } ;
eqexpr      = relexpr { ( "==" | "!=" ) relexpr } ;
relexpr     = addexpr { ( "<" | "<=" | ">" | ">=" ) addexpr } ;
addexpr     = mulexpr { ( "+" | "-" ) mulexpr } ;
mulexpr     = unary  { ( "*" | "/" | "%" ) unary } ;
unary       = ( "-" | "!" ) unary | postfix ;
postfix     = primary { "(" [ args ] ")" | "[" expr "]" | "." ident
                        [ "(" [ args ] ")" ] } ;
args        = expr { "," expr } ;
primary     = number | string | "true" | "false" | "null"
            | ident | "(" expr ")" ;
number      = decimal 0..32767, or "-" applied by unary ;
string      = '"' chars '"'  (folded to 0x20..0x7E; \" \\ \n are the only
                              escapes; length after escapes ≤ 255) ;
ident       = [A-Za-z_][A-Za-z0-9_]* , ≤ 31 chars, case-sensitive ;
comment     = "//" to end of line, or "/*" ... "*/" ;
```

Limits, enforced at pack: ≤ 128 functions, ≤ 128 globals, ≤ 8 parameters,
≤ 16 locals per function (parameters included), ≤ 64 `var` initializers of
kind `array`, array size 1–2048 (and it must fit the arena, §4.7). `var`
inside a function declares a local; locals have function scope (no block
scoping). Redeclaration, use-before-declaration of a local, `break`/
`continue` outside a loop, and assignment to a parameterless function name
are pack errors.

An identifier in expression position resolves in this order, pinned:
local, global, component id (from WML), function name (legal only as a
callback argument, §4.6.6, or a call). Anything else is a pack error
naming the identifier.

### 4.3 The value model

Every WJS value is a **4-byte tagged cell**: tag word + payload word.

| tag | type | payload |
|---|---|---|
| 0 | int | signed 16-bit value |
| 1 | string | handle index (§4.8) |
| 2 | array | handle index |
| 3 | component | comp_id (0 = app) |
| 4 | null | 0 |
| 5 | bool | 0 or 1 |

Payloads of strings and arrays are **handles, never pointers** — indices
into the handle table in the VM claim — so the compacting collector moves
arena bytes without hunting roots, and the claims' own movability
(SPEC.md §66) stays a later, three-word adoption. Tags ≥ 6 do not exist;
an implementation that reads one has a corrupt claim and stops the script
with §10.6's sentence.

### 4.4 Semantics

- **Arithmetic** is 16-bit signed, two's complement, **wrapping on
  overflow** — defined behaviour, not an error. `/` truncates toward zero;
  `%` takes the dividend's sign; `/ 0` and `% 0` stop the handler with the
  script-error sentence (§10.6). `+` on two strings concatenates (result
  ≤ 255 bytes or script error); `+` on any other mixed pair, and every
  other arithmetic op on non-ints, is a script error.
- **`-32768 / -1` is `-32768`**, and `-32768 % -1` is `0`. It is named
  because it is the one arithmetic case where the machine and the model
  part company by construction rather than by anybody's mistake: the
  mathematical quotient is 32,768, wrapping is the rule above, and the
  8086's `idiv` does not wrap it — it raises **INT 0**, the same vector
  as divide-by-zero, with no handler installed in a package. So the
  implementation tests for the pair before dividing and answers the
  wrapped value; without that the app does not report a script error, the
  machine hangs. (The model's `wrap16(abs(a)//abs(b))` reaches the same
  answer by arithmetic and cannot see the trap.)
- **Comparisons**: `==`/`!=` compare tag and payload (strings by contents,
  byte-wise); `<` `<=` `>` `>=` require two ints or two strings (bytewise
  order) — anything else is a script error.
- **Truth**: `false`, `null`, int 0 and the empty string are false;
  everything else is true. `&&`/`||` are short-circuit and yield the
  deciding VALUE (JS semantics), compiled by §4.6.4's pattern; `!` yields
  a bool.
- **Strings are immutable**; all construction goes through `+`, `str()`,
  `substr()`. Max length 255.
- **Arrays are fixed-size int arrays**: `a[i]` reads and writes ints only;
  index out of range is a script error. Arrays cannot be resized, nested,
  or compared except by handle identity (`==` on two arrays compares
  handles).
- **Component properties and methods** (`c.text`, `g.cell(r,1)`) compile
  to `GETP`/`SETP`/`CALLM` with well-known atom ids — §6 defines, per
  component, which are legal, their types, and their price. A property
  read of the wrong type or an unknown atom on that ctype is a script
  error at run time in v1 (the packer checks what it can see statically:
  a literal `ident.prop` pairing against §6's tables).
- **Handlers run one at a time, to completion**, in event order — §4.9.
  There is no re-entrancy and no nesting; `alert()` and `timer()` return
  immediately and their callbacks arrive as later events.

### 4.5 The bytecode

Byte-oriented; one opcode byte, then operands. 38 opcodes, 0x00–0x25;
**the dispatch table is exactly 38 entries** and is generated by
`weavesim --emit-optab` so the model and the 8086 core cannot drift.
`rel16` operands are signed words relative to the byte after the operand.
Stack effects list pops → pushes.

| op | mnemonic | operands | stack | notes |
|---|---|---|---|---|
| 0x00 | `HALT` | — | any → 0 | end of handler; clears the eval stack |
| 0x01 | `PUSHI` | imm16 | → int | |
| 0x02 | `PUSHA` | atom8 | → str | pushes the atom's string (no copy: a static handle, §4.8) |
| 0x03 | `PUSHN` | — | → null | |
| 0x04 | `PUSHB` | imm8 | → bool | 0 or 1 |
| 0x05 | `PUSHC` | comp8 | → comp | |
| 0x06 | `LDG` | g8 | → v | |
| 0x07 | `STG` | g8 | v → | |
| 0x08 | `LDL` | l8 | → v | frame-relative |
| 0x09 | `STL` | l8 | v → | |
| 0x0A | `POP` | — | v → | |
| 0x0B | `DUP` | — | v → v v | |
| 0x0C | `ADD` | — | a b → r | int+int; str+str concat |
| 0x0D | `SUB` | — | a b → r | |
| 0x0E | `MUL` | — | a b → r | |
| 0x0F | `DIV` | — | a b → r | truncating; /0 = script error |
| 0x10 | `MOD` | — | a b → r | |
| 0x11 | `NEG` | — | a → r | |
| 0x12 | `EQ` | — | a b → bool | |
| 0x13 | `NE` | — | a b → bool | |
| 0x14 | `LT` | — | a b → bool | |
| 0x15 | `LE` | — | a b → bool | |
| 0x16 | `GT` | — | a b → bool | |
| 0x17 | `GE` | — | a b → bool | |
| 0x18 | `NOT` | — | a → bool | |
| 0x19 | `JMP` | rel16 | — | |
| 0x1A | `JZ` | rel16 | v → | jump when v is falsy |
| 0x1B | `JNZ` | rel16 | v → | jump when v is truthy |
| 0x1C | `CALL` | f8 | args → | pushes a frame; §4.7.1 |
| 0x1D | `RET` | — | v → v | pops the frame, leaves v for the caller |
| 0x1E | `GETP` | atom8 | comp → v | component property read |
| 0x1F | `SETP` | atom8 | comp v → | component property write |
| 0x20 | `CALLM` | atom8 argc8 | comp args → v | native component method |
| 0x21 | `BUILT` | b8 argc8 | args → v | builtin (§8.1) |
| 0x22 | `INCG` | g8 | — | global int += 1 (wrapping) |
| 0x23 | `DECG` | g8 | — | global int -= 1 |
| 0x24 | `AGET` | — | arr idx → int | |
| 0x25 | `ASET` | — | arr idx v → | v must be int |

A function with no `return` falls off its end: the compiler emits
`PUSHN` + `RET` there (handlers' return values are discarded). `HALT`
appears only as the compiled body of an empty function table's guard and
at the end of §4.11's stop path; handlers end in `RET`.

#### 4.5.1 Every indexed operand is bounds-checked at dispatch

**Binding, and it is a rule about hostile bytes rather than about
correctness.** §2.8's CODE section is validated at load only as far as its
*function table* — count, per-function offset, `nargs`, `nlocals` — because
that is what can be checked in one pass; the bodies are a byte stream whose
instruction boundaries are not knowable without decoding them, and a
decoder that walks them still cannot prove that a jump lands on one. A
`.WAB` on a disk need never have been through a packer (§10.4), so the
guarantees §4.6's compiler makes do not travel with the file, and an
operand believed on sight indexes a table with a number nobody wrote.

The dispatcher therefore checks, per op, before it is obeyed:

| operand | bound | else |
|---|---|---|
| the opcode byte | < 38 | `bad opcode.` |
| `LDG` `STG` `INCG` `DECG` g8 | < 128 (§2.8's cap) | `bad opcode.` |
| `LDL` `STL` l8 | < the current frame's `nlocals` | `bad opcode.` |
| `CALL` f8 | < the function count | `bad opcode.` |
| `BUILT` b8 | < 12 (§8.1) | `bad builtin.` |
| `PUSHA` atom8 | an atom this bundle can name (§2.7) | `bad opcode.` |
| `JMP` `JZ` `JNZ` target | inside the CODE section's body | `bad opcode.` |
| `PUSHC` comp8, `GETP`/`SETP`/`CALLM` atom8 | — | resolved natively, which already answers `no component %d.` / `no property "%s" on a %s.` |

Six compares and no table (`AGET`/`ASET` were already bounds-checked by
§4.4, and `PUSHI`/`PUSHB`/`PUSHN`/`POP`/`DUP`/`RET`/the arithmetic take no
index at all). The cost is under 3% of the §4.12 contract and the
alternative is a package that writes into its own claim at an address a
corrupt file chose. A jump that lands *inside* another instruction's
operand is not detectable this way and is not meant to be: what these
bounds guarantee is that such a stream can only run garbage **inside the
VM's own claim** and will meet one of the sentences above, never that it
runs the program its author wrote.

### 4.6 Code generation — normative templates

Two independent compilers must emit **identical bytes** from identical
source, so emission is pinned per production. No optimisation of any kind:
no constant folding, no dead-code elimination, no peephole, no
strength-reduction. Left operands compile before right operands; arguments
compile left to right.

#### 4.6.1 Statements

- `expr ;` → [expr] `POP`. A call statement whose value is void-shaped
  still pushes (all calls push) and still pops.
- `lvalue = expr ;` → ident global: [expr] `STG g`; local: [expr] `STL l`;
  `a[i] = e` → [a] [i] [e] `ASET`; `c.p = e` → [c] [e] `SETP atom`.
- `g++ ;` where g is a global → `INCG g`; `g-- ;` → `DECG g`. On a local:
  `LDL l` `PUSHI 1` `ADD` `STL l` (and `SUB` for `--`). `++`/`--` exist
  only as statements.
- `return ;` → `PUSHN` `RET`; `return e ;` → [e] `RET`.

#### 4.6.2 `if`

```
if (c) A          [c] JZ Lend  [A] Lend:
if (c) A else B   [c] JZ Lelse [A] JMP Lend  Lelse: [B]  Lend:
```

#### 4.6.3 Loops

```
while (c) A    Ltop: [c] JZ Lend [A] JMP Ltop Lend:
for (i;c;s) A  [i] Ltop: [c] JZ Lend [A] Lstep: [s] JMP Ltop Lend:
```

An omitted `for` condition compiles as `PUSHB 1`. `break` → `JMP Lend`;
`continue` → `JMP Lstep` (`Ltop` in a `while`). Jump displacements are
resolved in a single backpatch pass; forward references patch to the final
offsets — there is exactly one encoding, so no ambiguity survives.

#### 4.6.4 `&&`, `||`, `!`

```
a && b    [a] DUP JZ Lend  POP [b]  Lend:
a || b    [a] DUP JNZ Lend POP [b]  Lend:
!a        [a] NOT
```

#### 4.6.5 Calls, properties, methods, indexing

```
f(a1,a2)      [a1] [a2] CALL f      (argc must equal nargs — pack error else)
c.p           PUSHC c  GETP atom      (or [expr] GETP when c is a variable)
c.m(a1)       PUSHC c  [a1] CALLM atom,1
a[i]          [a] [i] AGET
builtin(...)  [args] BUILT b,argc
```

A component named directly by its WML id compiles to `PUSHC comp_id`; a
component held in a variable compiles to the variable load. Builtins are
recognised by name (§8.1) before globals — shadowing a builtin name with a
global is a pack error.

#### 4.6.6 Callback arguments

Where a builtin takes a callback (`alert`'s second argument, `timer`'s
second), the argument **must** be an identifier naming a top-level
function; the compiler emits `PUSHI function-index`. Functions are not
values anywhere else — passing, storing or comparing one is a pack error.

### 4.7 The VM claim layout

One claim per instance, pinned, default 16KB, header-declared 16–32KB.
`S` = claim size in bytes. All offsets from the claim base:

| offset | size | region |
|---|---|---|
| 0x0000 | 512 | globals: 128 × 4-byte cells, zeroed, then initializers applied |
| 0x0200 | 1,024 | handle table: 256 × 4 bytes (§4.8) |
| 0x0600 | 256 | eval stack: 64 × 4-byte cells |
| 0x0700 | 128 | frame stack: 16 × 6-byte frames + 32 pad |
| 0x0780 | 128 | event ring: 16 × 8-byte records (§4.9) |
| 0x0800 | 128 | reserved (§5.3's FX eval stack is **not** here — below) |
| 0x0880 | S−0x0880−2,064 | string arena |
| S−2,064 | 2,048 | array arena |
| S−16 | 16 | hot scratch: slice counter, budget, ring head/tail, GC request — parked in the claim's top 16 bytes, not package bss (the measured RunCPM TCG lesson: a bss word sharing a page with translated code cost 5×) |

Eval-stack overflow (64 cells, locals included) and frame overflow (call
depth 16) stop the handler with §10.6's sentence. The CPU stacks (1,024
UI / 384 worker) never carry VM state.

**The 128 bytes at 0x0800 were §5.3's FX eval stack and are now reserved**,
because wave 4 went to write the FX VM and found the stack in the wrong
segment. The FX VM's hot memory is the GRID claim — every `FCELL` and every
aggregate reads it — and an 8086 has two data segment registers, one of which
is the RPN stream's. A stack in the VM claim would need a third. So §5.3's
sixteen 6-byte slots are 96 bytes of the RUNTIME's own bss, which is where a
value that never outlives one `wfx_eval` call belongs, and the region here
stays reserved rather than reclaimed so that no offset in this table moves.
It is also what lets the FX VM run in §12.1.2's boot sector with **no VM
claim bound at all** — the two cores are independent, and the corpus proves
it by not providing one.

#### 4.7.2 The hot scratch, byte by byte

Pinned, because "slice counter, budget, ring head/tail, GC request" names
five things and reserves sixteen bytes, and an implementer who has to
choose the order has to guess (this document's own rule). All offsets from
`S−16`:

| offset | size | field |
|---|---|---|
| +0 | 2 | `HS_BUDGET` — ops allowed in the current slice (§4.10) |
| +2 | 2 | `HS_LEFT` — ops still owed in the current slice; 0 = exhausted |
| +4 | 1 | `HS_RHEAD` — the ring's oldest slot, 0–15 |
| +5 | 1 | `HS_RCOUNT` — records queued, 0–16 |
| +6 | 1 | `HS_GCREQ` — 1 = an allocation did not fit; collect between slices |
| +7 | 1 | `HS_STATE` — 0 idle, 1 a handler is part-run, 2 stopped by §4.11 |
| +8 | 2 | `HS_SEED` — `rand()`'s LCG state (§8.1) |
| +10 | 6 | 0. Reserved; a reader must not assume a meaning for them |

The reason they are here rather than in the package's `.bss` is the one the
table above gives, and it is measured rather than argued: RunCPM's own
translated-code page cost 5× when a hot bss word shared it. `HS_LEFT` is
the value that survives a slice; inside the dispatch loop it may be held in
a register and written back once on the way out, which is what makes the
budget cost one `dec`/`jnz` per op rather than a memory round trip.

#### 4.7.1 Frames and locals

A frame is 6 bytes: return offset word (bytecode offset of the next
instruction in the CALLER), frame base word (eval-stack cell index), fn
index byte, nlocals byte. `CALL f`: base = sp − nargs; cells
[base, base+nlocals) become the locals, non-argument locals zeroed to
null; sp = base + nlocals. `RET`: pops the value, sets sp = base, pushes
the value, pops the frame. A handler invocation starts with its event's
values as arguments per §4.9.1.

### 4.8 The handle table and the collector

Handle-table entry, 4 bytes: arena offset word (0xFFFF = free), type byte
(0 free, 1 string, 2 array, 3 static string), flags byte (bit 0 = mark).
Type 3 names an ATOMS-pool string in the bundle claim: `PUSHA` binds atoms
to static handles on first use — never copied, never collected, never
compacted (the bundle is pinned).

Arena objects carry a 4-byte header: total size word (header + payload,
rounded even), handle byte, flags byte. String payload: length byte + up
to 255 bytes. Array payload: count word + count words.

**The collector is compacting mark-sweep and runs only BETWEEN slices** —
never mid-slice, never under the gfx lock. Trigger: an allocation that
does not fit its arena sets the GC-request scratch byte and ends the slice
early; the wake handler collects, then resumes the handler and retries the
allocation; if it still does not fit, the script stops with §10.6's
out-of-space sentence. Mark roots: the 128 globals, the live eval-stack
cells, **and the runtime's own component-string slots** (§4.8.1). Event
records carry no handles and are not roots. Sweep frees unmarked
non-static handles; compaction slides each arena's live objects down in
address order and rewrites only the handle-table offsets — no other value
in the system holds an arena address, by construction.

**Retrying an allocation means re-executing the op, not resuming inside
it.** So an allocating op reads its operands from the eval stack *without
popping them*, and on a GC request rewinds the bytecode pointer to its own
opcode byte and ends the slice with the stack exactly as it found it. The
ops that can allocate are `ADD` (string concatenation), `GETP` on a
property whose value is not already a handle, `CALLM` returning a string,
and `BUILT` for `str`, `substr` and `array`. Nothing else touches an arena.

#### 4.8.1 The component-string slots are roots

A component's `text` or `label` starts as an ATOMS-pool atom and becomes,
the first time script writes one, an ordinary arena string — so the
runtime holds **one handle per comp_id** for it, and a `<list>` that has
had `set(i, s)` called on it holds one per overridden item. Those handles
are reachable from nothing the paragraph above listed, and the earlier
version of it ("the 128 globals, the live eval-stack cells, nothing else")
would have had the collector free the string a label is *currently
displaying*, the moment a later handler filled the arena. They are roots,
and they are named here rather than left to an implementer to notice,
because the failure is a label that goes to garbage on some unrelated
handler and never on the one that set it.

The list-item overrides come out of a **64-entry pool** shared by every
list in the bundle — `(comp_id, item index, handle)`, searched linearly and
skipped entirely while it is empty, which is every app that never calls
`set`. The 65th override refuses with §10.6's `out of string space.`; 64 is
one full `<list>` (§2.6.1's own item cap), and a second list that rewrites
all of its items is the case that pays. Recorded as a bound rather than
discovered: an app meets a sentence that says what ran out.

### 4.9 The event ring

16 slots × 8 bytes, in the VM claim (§4.7): comp_id byte, event-atom byte,
data1 word, data2 word, reserved word. Kernel callbacks — running under
the gfx lock — enqueue and post `OSAPI_WM_WAKE`; the wake handler drains.
Exactly **one handler runs at a time, to completion, in ring order**;
events arriving mid-handler queue behind it. That is the entire
re-entrancy rule — no locks, no nesting.

**The overflow policy** (binding, in this order):

1. **Keys are never dropped by coalescing.** Every `onkey` enqueues.
2. `onchange`, `onselect`, `onclick` and `onscore` **coalesce per
   component**: an incoming record replaces a queued record with the same
   comp_id and atom — newest wins — whether or not the ring is full.
3. `ontimer` and `ontick` **collapse to one**: at most one of each queued;
   a new one replaces it.
4. A full ring receiving a key event drops the **newest queued non-key
   event** to make room.
5. A ring genuinely full of key events answers the next key with a **BEL**
   (`OSAPI_SND_TONE` beep — the RunCPM precedent) rather than silence, and
   the key is refused.

The policy above is written about a QUEUE and the storage is a RING, and
the two differ in exactly one place, so it is pinned here. The ring is
`HS_RHEAD` (the oldest slot) plus `HS_RCOUNT` (§4.7.2); the *k*th oldest
record is slot `(head + k) mod 16`. Rules 2 and 3 both replace an existing
record, and they do not replace it in the same place:

- **Rule 2 (coalesce) overwrites the record where it stands** — the queue's
  order does not change and a click that lands twice on one button is
  answered once, at the position of the first.
- **Rule 3 (collapse) removes and re-appends** — the surviving `ontimer` or
  `ontick` is the NEWEST, at the BACK. Removing from the middle of a ring
  slides the records after it down one slot and decrements `HS_RCOUNT`;
  at most 15 8-byte moves, on a path that fires at most 18 times a second.
- **Rule 4** removes by the same slide, scanning from the newest end for
  the first non-key record, then appends the key.

An implementation that coalesced by remove-and-append would reorder events
that the model keeps in order, which is invisible in every single-event
test and is exactly the kind of difference the `weavevm` corpus (§12.3)
exists to catch.

#### 4.9.1 Handler invocation

Dequeuing a record whose (comp, atom) resolves to a bound function invokes
it with arguments = the record's meaningful words per §3.4's table, in
that order, as ints (comp events on components the handler already knows —
the component itself is not passed). A record with no binding is
discarded. `ontimer`/`onalert` records invoke the function named by data1
with data2 as the single argument (alert) or none (timer).

"The record's meaningful words" is pinned as a count, because §3.4's table
spells a word that is always zero as `0` and an implementer reading it
cannot tell "this word means nothing" from "this word is usually nought":

| event | args | event | args |
|---|---|---|---|
| `onclick` | 0 | `oncollide` | 2 |
| `onchange` | 1 | `onwall` | 2 |
| `onkey` | 2 | `onscore` | 2 |
| `onselect` | 2 | `ontick` | 1 |
| `onedit` | 2 | `oncommand` | 2 |
| `oncalc` | 1 | | |

and the list is then **padded with int 0 or truncated to the handler's own
`nargs`** (§2.8), so a `function onKey(ch)` written against a two-word
event is legal and gets the first word. Every argument arrives as an int
(tag 0), sign-extended from the record's word.

### 4.10 The slice model

RunCPM's adaptive ONWAKE discipline (SPEC.md §74.1), with the family's own
numbers:

- Budget unit: one dispatched op (WVM op or FX op, both count 1).
- **Start budget `256 << cpu tier`** (256 on an 8086), **floor 128, cap
  1,536 ops per slice.**
- The honest arithmetic, stated because a wrong version of it shipped in a
  draft: at the contracted 10–30k ops/s (§4.12), 1,536 ops is **51–154
  ms** — the cap alone can span one to three ticks on the slow end, and
  the halving arm below is what keeps the desktop responsive there.
- Doubled after four same-tick exhaustions; halved when a slice spans two
  ticks; only exhausted slices are timed.
- The wake re-posts **only while a handler is unfinished or the ring is
  non-empty** — an idle app costs zero CPU (a spinning wake is ~1,400
  task switches/s of dead UI task at 693 µs each).
- File-touching builtins (`saveState`/`loadState`) execute inside the
  slice, on the UI task, where file slots are legal.

### 4.11 The runaway script

A handler still unfinished after **90 ticks (~4.9 s at 18.2 Hz)** raises
the shared alert (`os88ui_ask`): **`Script is still running. Stop it?`**
with buttons **`Yes` / `No`**. Yes abandons the handler (eval and frame
stacks cleared, ring preserved, globals as they are); No re-arms the counter
for another 90 ticks. The runaway loop is a designed path with a designed
sentence, not a hang.

This section asked for buttons `Stop` / `Wait` until wave 3 went to raise
one. **The shared alert's button sets are fixed** — `OK`, `Yes`/`No`,
`Save`/`Discard`/`Cancel`, and no more (SPEC.md §75.3, `apps/os88ui.inc`) —
because that engine's whole argument is that it costs each package 607 bytes
of its own image rather than the kernel 1,067, and a per-caller label table
is the first thing that would undo it. So the choice was between inventing a
fourth set in a shared file for one caller, or asking the question as a
question. It is asked as a question, in 33 characters against
`OS88UI_AMAX`'s 34. The alternative was a runtime quietly using different
words from the ones this document pins, which is worse than either.

#### 4.11.1 The `ontick` budget

An `ontick` handler is bound at **≤ 64 emitted bytecode ops, no backward
jump, no CALL** — statically checkable, and the packer **rejects** a
handler over the bound with §10.5's sentence (at 18 fps, 64 ops is ~1,150
ops/s, under 10% of the VM contract). Per-frame JS beyond that is
arithmetically impossible on this machine (§9.3) and is refused at pack,
not discovered on an XT.

### 4.12 The contract number

**The WVM contract is 10,000–30,000 bytecode ops/s on the 4.77 MHz
target**, with assembly dispatch (the rcz80 shape: `xor bh,bh / mov
bl,[si] / inc si / shl bx,1 / jmp [cs:bx+wvm_tab]`, DS switched to the VM
claim for the whole slice) — ~500–1,600 ops per 55 ms tick. Stated here
the way SPEC.md §74 states the Z80's: a design figure derived from the two
shipped interpreter cores' slice arithmetic, **PENDING a field reading**,
and self-measured — WEAVE's About panel banners `WVM: <n> ops/s
(measured)` from the last second of exhausted slices, so the machine
reports its own number the way RunCPM banners its clock. The wave-5 field
run (§13) converts the figure from design arithmetic to measurement; no
performance claim ships on emulator evidence alone (docs/TESTING.md,
docs/FIELD-MACHINES.md).

---

## 5. FX — the formula language

### 5.1 Grammar

Formulas live in `<grid>` cells and begin with `=`. Case-insensitive
function names and column letters; whitespace free between tokens.

```ebnf
formula   = "=" expr ;
expr      = cmp ;
cmp       = sum [ ( "=" | "<>" | "<" | "<=" | ">" | ">=" ) sum ] ;
sum       = term { ( "+" | "-" ) term } ;
term      = factor { ( "*" | "/" ) factor } ;
factor    = [ "-" ] atom ;
atom      = number | cellref | funcall | "(" expr ")" ;
number    = digits [ "." digits ]  (16.16 range: |value| < 32768;
                                    at most FOUR fraction digits) ;
cellref   = letter digits          (column A..Z, row 1..256) ;
range     = cellref ":" cellref    (legal only as an aggregate argument) ;
funcall   = name "(" arg { "," arg } ")" ;
arg       = expr | range ;
```

A non-formula cell entry is a number (stored as 16.16) or a text label
(stored as a string). Cell references are absolute — there is no `$`
notation and no relative copy-adjust in v1.

**The fraction is bounded at four digits** and a fifth refuses at pack. It is
not an arbitrary cap: 16.16's own resolution is 1/65536 ≈ 0.0000153, so the
fifth decimal place is *below* what the format can store and could not change
the value — all it could do is make two parsers disagree about which way to
round it. Four digits is also what lets the conversion be one 16-bit divide on
the target (`(digits × 65536 + d/2) / d` with `d ≤ 10000`), which is what the
resident compiler (§6.9.2) needs to be able to do the same arithmetic as the
host packer rather than an approximation of it.

### 5.2 The number model

FX values are **32-bit 16.16 fixed point** — legal because the FX VM is
assembly, where 32 bits cost nothing the C subset forbids (SPEC.md §73.7
binds C, not cores). Range ±32,767.9999; overflow wraps (defined);
division by zero yields the error value, which displays as `#DIV0` and
propagates through arithmetic. Comparisons yield 1.0 or 0.0. `ROUND`
rounds half away from zero to an integer value (fraction bits cleared
after rounding).

Crossing into WJS (`cell()` reads, `setCell()` writes) **truncates to the
integer part** in the int's range; out of range is a script error. This
seam is stated, not hidden: WJS is 16-bit, FX is 16.16, and the grid is
FX's domain.

#### 5.2.1 The display form, pinned

A 16.16 value reaches the glass as characters, and `weavegrid` (§12.3)
compares the machine's picture with the model's cell for cell — so the
conversion is part of the contract and not the model's private business. It
was `fmt_16_16`'s "recorded decision" until wave 4 went to write the 8086's
half and found nothing to write it from.

Given a cell whose value is `v`:

| case | display |
|---|---|
| the error value (§5.2) | `#DIV0` |
| a formula marked CIRC (§5.5) | `#CIRC` |
| a label | its string |
| an empty cell | the empty string |
| otherwise | as below |

```
neg   = v < 0            ; the sign is emitted first and separately
a     = |v|              ; as an unsigned 32-bit magnitude
ip    = a >> 16
cents = ((a & 0xFFFF) * 100) >> 16      ; TRUNCATED, never rounded
if cents == 0:  ip in decimal
else:           ip, '.', cents as EXACTLY TWO digits, then ONE trailing
                '0' removed if the second digit is '0'
```

So `3.5` is `3.5`, `7.25` is `7.25`, `12` is `12`, `1/3` is `0.33`, and
`-0.001` is `-0` — the last is named because it looks like a defect and is
the arithmetic: a magnitude under 1/200 has no cents and an integer part of
zero, and inventing a rounding rule for it would be a second conversion to
keep in step. The truncation is likewise deliberate: rounding at two places
would make `0.999` display `1` while `=A1=1` is false, which is the class of
disagreement a spreadsheet must not have.

### 5.3 The RPN encoding

Formulas compile (shunting-yard, §5.1's precedence, left-associative) to a
byte-oriented RPN stream. FX eval slots are 6 bytes: type byte (0 number,
1 range, 2 error) + pad + 4-byte payload (16.16 value, or packed range
r1,c1,r2,c2 bytes). Stack depth cap 16; a formula deeper than that is
refused at pack.

23 FX opcodes, 0x00–0x16, pinned:

| op | mnemonic | operands | effect |
|---|---|---|---|
| 0x00 | `FEND` | — | end; the single remaining slot is the result |
| 0x01 | `FNUM` | dword | push 16.16 literal |
| 0x02 | `FCELL` | row8 col8 | push that cell's value (label/empty → 0) |
| 0x03 | `FRANGE` | r1 c1 r2 c2 | push a range descriptor |
| 0x04–0x07 | `FADD FSUB FMUL FDIV` | — | a b → r |
| 0x08 | `FNEG` | — | a → −a |
| 0x09–0x0E | `FEQ FNE FLT FLE FGT FGE` | — | a b → 1.0/0.0 |
| 0x0F | `FSUM` | — | range → sum |
| 0x10 | `FMIN` | — | range → min |
| 0x11 | `FMAX` | — | range → max |
| 0x12 | `FAVG` | — | range → mean (0 over an all-empty range) |
| 0x13 | `FCOUNT` | — | range → count of numeric cells |
| 0x14 | `FIF` | — | c a b → (c≠0 ? a : b); both arms already evaluated (eager — stated, and the reason `FIF` cannot guard a `#DIV0`) |
| 0x15 | `FABS` | — | a → |a| |
| 0x16 | `FROUND` | — | a → rounded |

Aggregates walk their range row-major, skipping empty and label cells
(except `FCOUNT`, which counts numeric cells only). A range anywhere but
directly under an aggregate is a pack error. An error value entering any
op yields the error value.

### 5.4 The functions

`SUM MIN MAX AVG COUNT` take exactly one range; `IF(c,a,b)` three
expressions; `ABS(a)` and `ROUND(a)` one. Anything else — any other name,
wrong arity — is a pack error naming the function. This is the whole set;
FX grows by amending this table first.

### 5.5 Recalculation

Row-major with a circular marker — VisiCalc's model, no dependency graph
in v1:

1. **Pass 1**, row-major over formula cells: evaluate each from current
   values (a formula reading a not-yet-recomputed cell sees its previous
   value).
2. **Pass 2**, row-major again. A formula whose pass-2 value differs from
   its pass-1 value is marked **CIRC** and displays `#CIRC`; its pass-2
   value stands.
3. Cells whose displayed value changed mark their rows dirty; paint
   follows §6.9's band discipline.

Recalc is **sliced**: FX ops count against §4.10's slice budget one for
one, and `oncalc` fires (once) when pass 2 completes. Arithmetic for
the stated worst case: 500 formula cells × ~10 ops × 2 passes = 10k FX
ops ≈ 300–600 ms across 7–14 capped slices at ~30–60 µs/FX-op.

Triggers: a cell commit from the formula bar, `setCell()`, `recalc()`.
Multiple triggers before the passes start collapse to one recalculation.

#### 5.5.1 The slice boundary falls INSIDE the passes, so the state is pinned

A recalculation that only ran whole would be the runaway §4.11 exists to
prevent, wearing a different hat: 10k FX ops is 300–600 ms with the desktop
stopped. So the walk is resumable and its state is named here rather than
left to an implementer, because two of the four fields are the ones an
implementation would keep on a stack a slice does not own.

| field | what |
|---|---|
| `pass` | 0 idle, 1 pass 1 running, 2 pass 2 running |
| `cursor` | the next cell index in row-major order, 0..rows×cols |
| `changed` | display strings that have changed so far, for `oncalc` |
| the **pre-walk value** | per formula cell, in its own pool slot (§5.6) |

That is ONE extra value per formula cell and not two, and the arithmetic is
worth writing down because the obvious reading of §5.5 asks for three. §5.5
needs the value a formula had *before the walk* (to count what changed, and to
mark its row) and the value it had *after pass 1* (to decide CIRC) — but the
pass-1 value needs no storage at all: pass 1 leaves it in the cell's own
cached slot, and pass 2 reads it there on its way past, one instruction before
overwriting it. So pass 1 saves the pre-walk value into the slot's second
dword and writes its own answer into the cached one; pass 2 reads the cached
one as its pass-1 comparand, writes its answer over it, and compares its
DISPLAY against the pre-walk value's.

It is per CELL and not a list because the two passes are separated by an
unbounded number of slices: a vector allocated for the duration would be a
second allocation on a heap the app has already been refused against (§10.1),
and a local would not survive the return. `CELLF_ERRWAS` and `CELLF_CIRCWAS`
carry the two bits of the pre-walk DISPLAY that its dword cannot — a value of
zero and `#DIV0` are the same four bytes.

**A trigger arriving mid-walk restarts at pass 1, cursor 0.** That is what
§5.5's "multiple triggers collapse to one" means once the passes can be
interrupted: a `setCell` landing between the two passes has changed an input
the first pass already read, and finishing the walk would publish values from
before it. Restarting is bounded — the trigger came from a handler, handlers
run one at a time (§4.9), and each restart is one more pass over the same
cells.

**`Calculating...` goes in the FORMULA BAR**, not in a status cell. §5.5 said
"the grid's status cell" until wave 4 went to draw one and found the family
has no status row while a card is up (§10.6.0's own reason, and §7.1.1 gives
the grid no chrome of its own to put a cell in). The bar is already the
grid's one line of text, it is already repainted per commit, and it is where
the eye is after an Enter. Pass 2's completion restores it to the selected
cell's source (§6.9.3).

**The damage rule.** A cell whose DISPLAY string (§5.2.1) differs from what
it displayed before the walk began marks its grid ROW dirty (the cell
record's flags bit 1, §5.6). At the end of pass 2 exactly the dirty visible
rows are re-composed and blitted — one `GFX_BLIT1` each (§6.9.1) — and
nothing else on the card is touched. A recalculation that changes one cell
costs one gfx call, which is §14's `edit one cell` row and the whole reason
the store carries a dirty bit rather than the painter carrying a compare.

### 5.6 The grid claim — the cell store

A dedicated pinned claim, header-declared (§2.2), one grid per app in v1
(the 8-claims-per-owner cap, SPEC.md §50.2, is why). Layout:

| offset | size | region |
|---|---|---|
| +0 | 16 | header: cols byte, 0, rows word, pool-next word, pool-end word, 8 reserved |
| +16 | rows×cols×4 | the dense cell array, row-major, 4 bytes/cell |
| then | to claim end | the pool: bump-allocated, never freed |

Cell record: kind byte, flags byte, payload word.

| bit | name | meaning |
|---|---|---|
| 0 | `CELLF_CIRC` | §5.5's circular marker; the cell displays `#CIRC` |
| 1 | `CELLF_DIRTY` | the row-dirty mirror (§5.5.1) |
| 2 | `CELLF_ERR` | **the cached value IS the error value** (§5.2); the cell displays `#DIV0` |
| 3 | `CELLF_ERRWAS` | the PRE-WALK value was the error value (§5.5.1) |
| 4 | `CELLF_CIRCWAS` | ...and the cell was marked CIRC before the walk began |
| 5–7 | — | 0 |

Bit 2 is named here because it cannot be inferred: FX's error is not a
number, every one of the 2³² bit patterns of a 16.16 slot is a legal value,
and a cached dword therefore cannot carry "this is `#DIV0`" in itself. An
implementation that reserved a sentinel value instead would make one real
number un-storable and would disagree with the model on the first sheet that
computed it.

| kind | meaning | payload |
|---|---|---|
| 0 | empty | 0 |
| 1 | inline int | signed 16-bit value (a whole number in int range) |
| 2 | pool number | pool offset of a 4-byte 16.16 value |
| 3 | atom label | atom id |
| 4 | bundle formula | pool offset of a **10-byte** slot: FXCODE index word, the 4-byte cached value, the 4-byte pre-walk value (§5.5.1) |
| 5 | pool string | pool offset: length byte + bytes (a runtime `setCell` string, copied in) |
| 6 | **runtime formula** | pool offset of a slot: RPN length word, the 4-byte cached value, the 4-byte pre-walk value, then `len` bytes of §5.3 RPN — the resident compiler's output (§6.9.2) |

Kinds 4 and 6 are one cell kind wearing two addresses. The RPN a bundle
carried is in the BUNDLE claim, which is pinned and read-only (§2.1); the RPN
a user typed into the formula bar has nowhere to be but the grid claim's own
pool, and a wave that made the formula bar work without saying so would have
had to write it into a read-only section. The FX VM therefore takes a
(segment, offset) pair for the stream rather than an index, and the only
difference between the two kinds is which segment it is handed.

Both slots carry the **pre-walk value** §5.5.1 requires, which is why kind 4's
slot is 10 bytes and not 6. A runtime formula's slot is `10 + len` bytes and
is bump-allocated like every other pool object: **re-typing a formula into
the same cell allocates a new slot and leaks the old one**, which is stated
rather than discovered — the pool is never freed (this section's own rule),
so a session that edits one formula five hundred times meets `grid pool
full.` (§10.6) and an app that edits a handful does not notice. The
alternative is a free list in a 2KB pool, which is more machinery than the
whole component.

A whole-number store in int range is kind 1 (no pool cost); a fractional
value allocates a pool slot once and overwrites it thereafter. Pool
exhaustion is a script error (§10.6's grid sentence). The packer sizes the
claim as `max(8, ceil((16 + rows×cols×4)/1024) + 2)` KB — the `+2` is a
2KB pool floor, and the 8 is §2.2's claim envelope (the grid byte is 0 or
8–26): a small grid's ask rounds up to 8 and the whole difference goes to
the pool, so its strings and fractional values ride free. The cap is 26,
which therefore **caps rows×cols at 6,140** — stated, because the naive
26×256 grid does NOT fit a 26KB claim once its pool exists, and the
refusal belongs at pack.

---

## 6. The component library

### 6.1 What the library is

The library IS the product: each component is declared in WML, painted by
shared native `.inc` code included by both packages, and **priced here**
in gfx calls from the measured table — 756 µs fixed per gfx call, ~900 µs
per glyph cell, ~71 ms per 78-cell row, and the band composer's 860
µs/call + 173 µs/cell (PERFORMANCE.md Set 68 — the composer's bench; not
Set 65, which is the tracker's 11 kHz question). Everything sits on the
8×8 cell grid, which is what earns `font_run`'s single-store path and
8-aligned blits. §14 is the same pricing as one regenerated table.

Two rules every component inherits so an app author cannot get them
wrong:

- **Disabled is the SPEC.md §47 pen**: `OSAPI_GFX_PEN` sets CDGRAY plus
  the dither flag, the whole control greys, and one predicate feeds the
  paint, the click refusal and the explanation. Verified on a 1bpp
  adapter before any drawing change is called done (SPEC.md §39.4).
- **Nothing repaints more than it changed** (PERFORMANCE.md Part 5): a
  press repaints the pressed control; a keystroke letters ~2 cells; a
  scroll is one `GFX_SCROLL` plus the exposed row; a full-card repaint
  happens at card switch and window resize, priced and shown as such.

### 6.2 `label` and `text`

Wrap nothing — one `font_run` per row, painted on damage or on a `.text`
write. A 20-cell label repaints in ONE call, ~18 ms. `label` is one line,
clipped at its width; `text` wraps at the walk's width — **its own settled
width, never `CW`**: a `<text w="…">` that wrapped at the full row on one
implementation and at its declared width on another would break §12's
determinism contract silently, since the two differ only in a bundle that
declares the attribute — natural height = its wrapped row count. BOLD is
drawn by the library double-striking one pixel right within the composed
band, which is **a second gfx call** (an opaque `font_run`, then a
transparent one a pixel right) — §14's `label` row prices the plain case at
1; INVERT swaps ink and paper for the run (padding is the erase — no
fill-then-letter pair, SPEC.md §27.2).

WJS surface: `text` (get/set string). Setting `.text` repaints only the
component's rows.

### 6.3 `rule`, `box`, `spacer`

One `gfx` call each (`rule` an hline, `box` a frame); `spacer` draws
nothing. Price ~0.8 ms each, paid at card paint only. No WJS surface
beyond `hidden`.

### 6.4 `meter`

A framed horizontal bar. Setting `.value` fills or clears only the
**delta** span: 1 call per change, ~0.8–1 ms. Full paint = 2 calls.
Surface: `value` (get/set int, clamped 0..max), `max` (get).

### 6.5 `button`

Wraps `os88ui_btn` with its arm/fire gesture verbatim (SPEC.md §13.8). A
press repaints only the pressed control: ~2 calls + label cells ≈ 1.5–8
ms. Fires `onclick` on release-inside. Surface: `label` (get/set),
`enabled` (get/set; set repaints through the pen).

### 6.6 `check` and `radio`

Wrap `os88ui_glyph` (12×12). A toggle repaints ONE glyph: 35–50 ms
field-measured (44–64 set bits) — one control, never the row. Radio
groups are exclusive by `group` atom; checking one unchecks and repaints
the previous holder (two glyphs). `onchange` fires after the state
settles. Surface: `checked` (get/set), `enabled`, `label` (get).

### 6.7 `input`

Wraps `apps/os88line.inc` — the caller-owned-block single-line editor
Telnet and the browser ship (SPEC.md §71.3). Caret blink via `WM_TIMER`
re-arm with the **static-caret fallback** where `WM_TIMER` answers CF=1
(kern_small). A keystroke letters ~2 cells ≈ 1.8 ms (the Note Pad
contract, SPEC.md §27.2). `onkey` per keystroke; `onchange` on Enter.
Surface: `text` (get/set ≤ 255, display window scrolls), `cols` (get),
`enabled`. Focus is the platform's: the frontmost window's keys go to the
armed input (click to arm; one armed input per card; Tab moves to the
next input in UISTREAM order).

**At most eight editable fields per bundle, over 512 bytes of text.**
`apps/os88line.inc` declares no storage at all — every routine takes a
block the CALLER owns, which is what lets one window have two — so the
runtime's blocks and their buffers come out of a fixed pool, assigned in
UISTREAM order at load. Eight blocks is eight full-width fields (`cols`
caps at 60) or twenty typical ones. A ninth is **painted normally and
refuses focus**: it is not a malformed bundle and not a pack error, because
the bound is the runtime's arithmetic and not the format's, and a form is
still readable when its last field cannot be typed in. It is stated here so
an app author meets a number rather than a mystery.

**A greyed field is drawn by the runtime's own painter and not by
`os88line_draw`**, which forces `CBLACK` for its frame and its text: a
disabled one would come out solid-framed with dithered letters, two halves
of one control disagreeing, which is SPEC.md §47 rule 2's own failure. That
is the shared file's defect and not this family's — it has no other caller
that can grey a field — and it is recorded in docs/WEAVE-PLAN.md §4.4.2
rather than fixed here, for the reason WEAVE-PLAN §4.4.1 gives about the
scroll bar's missing floor.

### 6.8 `list`

One `font_run` per visible row; selection is an **XOR bar** — reversible,
1 call to move each way, so moving the selection is 2 calls ≈ 1.6 ms.
Scroll = one `GFX_SCROLL` + one exposed row ≈ 83–90 ms/line (the Part 5
contract row; a repaint would be 1.24 s on CGA), coalesced through
`OSAPI_EVQ_PENDING` with the browser's MAXSKIP bound of 4. **Plus the
thumb, when it moves**: `os88ui_sbmove` is three further calls, and it
draws NOTHING when the step is too small to shift the thumb — which on a
long list is most steps. So a scroll is 2 calls in the common case and 5
when the thumb translates; §14's row prices the common one.

**`rows` must be at least 3 for the list to be scrollable.** `os88ui_sbar`
places its two arrow-cell rules at `y1 + 10` and `y2 - 10` with no test that
the bar is tall enough for both, so a bar under 20 px crosses them and one
at 8 px puts an hline **outside its own rect**. A `<list rows="1">` or
`rows="2"` is legal by §3.3's 1–40 range and cannot carry a bar, so the
library draws none — which leaves a list that cannot be scrolled and does
not say why, and §47's rule is that a refusal is stated, never silent. The
packer therefore refuses `rows` below 3 on a list whose items can exceed
it, with §10.5's voice: `list rows="2": a scroll bar needs 3 rows (24 px);
os88ui_sbar's arrow cells are 10 px each`. The shared bar's own missing
bound is recorded in docs/WEAVE-PLAN.md rather than fixed here — it has no
other caller that can reach it. Items are
fixed at pack (§2.6.1), text mutable. `onselect` on selection change
(click or arrow keys). Surface: `sel` (get/set index, −1 none),
`set(i, s)` / `get(i)` (CALLM; set repaints that row), `rows` (get).

### 6.9 `grid` — the spreadsheet component

**The flagship.** Dense cell store per §5.6; painter is the band composer
(`wband.inc`, the rcband shape): each visible row is composed as a 1bpp
band in RAM — column rules and the selection frame drawn INTO the band for
zero extra gfx calls — and emitted as **ONE `gfx_blit1` per CHANGED row**.

Prices, from Set 68's constants:

| interaction | cost |
|---|---|
| edit one cell | recompose + blit 1 row ≈ 3–5 ms |
| move the selection | 2 recomposed rows ≈ 5–10 ms, or 2 XOR rects ≈ 1.6 ms on the fast path |
| one 80-cell row | 14.7 ms (vs `font_run`'s ~60) |
| full visible page, 20 rows | ≈ 290 ms (vs ~1.4 s via font_run) |
| scroll one row | 1 `GFX_SCROLL` + 1 composed row ≈ 90–100 ms |

The formula bar is a library-wired `input` bound to the grid: clicking a
cell loads its source (formula text or value) into the bar; Enter commits,
recompiles the formula **with the resident in-grid compiler** (§6.9.2 —
resident because commit is a keystroke-path action and overlay calls are
refusable), and triggers §5.5's recalc. Column headers
A.. and row numbers compose into the same bands.

Events: `onselect(row, col)`, `onedit(row, col)`, `oncalc(changed)`.
Surface: `cell(r,c)` (CALLM → int, §5.2's truncation), `setCell(r,c,v)`
(int or string), `recalc()`, `select(r,c)`, `clear()` (empties every
non-formula cell), `selrow`/`selcol` (get), `rows`/`cols` (get).
Row/col arguments are 1-based in WJS and FX alike.

#### 6.9.1 What the grid looks like, cell by cell

Pinned, because `weavegrid` (§12.3) diffs the machine's picture against
`weavesim --render`'s and a picture that is not pinned is not a diff. The
component's rect from the walk is `w` cells by `h` rows of 8 px. Inside it:

| rows | band |
|---|---|
| 0..1 | the **formula bar** — an `os88line` field spanning all `w` cells, the same editor an `<input>` is (§6.7), out of the same eight-block pool |
| 2 | the **column header** band |
| 3..h−1 | the **data** bands, one grid row each |

```
WG_GUT  = 4 cells      the row-number gutter
WG_COLW = 8 cells      every data column, fixed
VC = max(1, min(cols, (w - WG_GUT) / WG_COLW))     visible columns
VR = max(0, min(rows, h - 3))                      visible rows
```

The column width is **fixed at 8 cells and not fitted to the content**. A
fitted width would have to be recomputed whenever any cell in the column
changed, which turns a one-cell edit — §14's one-blit row — into a re-compose
of every band; and the two implementations would have to fit identically or
the diff is noise. Eight cells is seven characters and a separator, which
holds `-32768` and `#DIV0` whole.

Each band is composed left to right as exactly `w` cells of text and emitted
as ONE `GFX_BLIT1`:

```
header band:  WG_GUT spaces, then per visible column c:
                3 spaces, the column letter 'A'+(left+c), 4 spaces
              ...and the WHOLE band is drawn INVERTED (ink and paper
              swapped). Inverting costs nothing here - the band is bytes in
              RAM and the composer complements them - and it is the only
              chrome the grid has
data band r:  the 1-based grid row number, right-justified in 3 cells, then
              one space; then per visible column c: the cell's display
              string (5.2.1) clipped to WG_COLW-1 characters and justified
              LEFT for a label, RIGHT for a number, an empty cell or an
              error value, then one space
```

Cells past `WG_GUT + VC × WG_COLW` are blank. Rows and columns scroll: `top`
and `left` are the first visible grid row and column, both 0-based, and both
move only far enough to keep the selection visible (§6.9.4).

**The selected cell is drawn INVERTED** — its `WG_COLW` cells within its data
band. A selection move whose two cells are both on screen is **2 XOR rects**
over exactly those cell spans (§14's row, ~1.5 ms); a move that scrolls
re-composes. The two agree by construction: XOR-ing an inverted cell restores
it and XOR-ing a plain one inverts it, so the incremental path lands on the
same pixels a full re-compose would — which is precisely what `weavegrid`'s
tpdraw identity asserts.

**A one-row scroll is one `GFX_SCROLL` plus one composed band**; any other
step re-composes every visible band. That is §6.8's list rule with the same
arithmetic behind it and the same end-stop refusal: a scroll that would move
nothing draws nothing at all.

#### 6.9.2 The resident formula compiler

§9.4 says the runtime never parses text. This is the one carve-out, it is
named there too, and it is **the whole of §5.1's grammar and not a subset**.

Two grammars for one language is the drift §11's byte-identity rule exists to
prevent, said about a language instead of about a file: a subset would let a
formula pack on the host, load on the machine, and refuse the moment its
author clicked its cell to look at it. So the resident compiler is §5.1's
recursive descent emitting §5.3's RPN, with §5.3's depth cap and §5.4's
function set, and its output for a given source is the same bytes
`weavesim --pack` would have written for it.

It lives in the **overlay** (§1.2.1's tenant 7) with the rest of the commit,
and the draft of this section that said "resident" is corrected rather than
quietly diverged from: the reason it gave — that "your formula did not
compile because a module would not load" is not an answer a spreadsheet may
give — is true and is not decisive, because tenant 5 already gives exactly
that answer about `saveState` and for the same three reasons. §1.2.1 carries
the full paragraph and the arithmetic that forced it.

What that costs, stated: with `WEAVE.OVL` missing or stale **no bundle opens
at all** (§1.2 already), so the case where a grid is on screen and its bar
cannot compile is the case where the module went away between the open and
the Enter — a disk pulled mid-session. It answers with the sentence naming
the overlay, the bar keeps the text, and nothing is committed.

Its refusals are §10.5's, reduced to one line: the runtime has no file and no
line number to name, so the toast reads `Formula: <message>` with §10.5's own
message text, and the bar keeps the text the user typed so it can be fixed
rather than retyped.

#### 6.9.3 What Enter commits

The bar's text is classified in this order, and the order is the contract:

1. **empty** (no non-space character) → the cell becomes empty (kind 0);
2. **begins with `=`** → §6.9.2 compiles the rest; on success the cell
   becomes a runtime formula (§5.6 kind 6), on a refusal **nothing is
   committed** and the message is shown;
3. **parses whole as a §5.1 `number`** (optional `-`, digits, optional `.`
   and digits, nothing else) → a numeric cell: kind 1 when the value is a
   whole number in signed 16-bit range, kind 2 otherwise;
4. **anything else** → a label (kind 5, copied into the pool).

Then `onedit(row, col)` is enqueued and §5.5's recalculation is triggered.
The classification is tried in that order and never re-tried: `=` first means
a formula is never mistaken for a label, and the number test before the label
means `12` is twelve and not the word.

Loading the bar is the same rule run backwards — a formula shows as `=` plus
its source, a number as its §5.2.1 display, a label as its text, an empty
cell as nothing. **The source of a BUNDLE formula (§5.6 kind 4) cannot be
shown**: the bundle carries compiled RPN and no formula text (§2.9), and
decompiling RPN to source would be a third implementation of §5.1 to keep in
step. It loads as `=?` — the cell's own answer to "what is in you" when the
honest answer is "a formula this machine cannot spell". Committing over it
replaces it, which is the operation the user was reaching for.

#### 6.9.4 Selection, keys and scrolling

- A click in a data band moves the selection to that cell, loads the bar
  (§6.9.3) and does **not** arm the bar; a click in the formula bar arms it;
  a click in the header band or the gutter does nothing.
- The four arrow keys move the selection by one cell when the grid was the
  last component clicked and the bar is not armed. They scroll `top`/`left`
  by the minimum needed to keep the new cell visible.
- Enter commits (§6.9.3). Escape reloads the bar from the selected cell,
  which is the cancel.
- Every selection move — click, arrow key or `select()` — enqueues
  `onselect(row, col)`, 1-based, exactly once.

### 6.10 `canvas` + `sprite` — the game component

The arkanoid precedent, componentized. The canvas owns an offscreen 1bpp
buffer in its claim (§2.2's canvas KB; a 240×120 buffer is 3,600 bytes).
**The worker task** is hired at `start()` and released at `stop()` (spawn
refusal is normal and degrades to a refused `start()` — script error with
the sentence). Per frame (~18 fps: the worker sleeps `max(1,
round(18.2/fps))` ticks):

1. Move sprites by native per-axis velocities with **sub-pixel
   remainders** — `vx`/`vy` are in 1/16-px per frame (16 = 1 px/frame),
   the arkanoid technique.
2. Bounce on `walls` edges (negate the component, emit `onwall`); a
   sprite fully out an open edge stops and emits `onscore`.
3. AABB collision over shown sprites (≤ 16 per canvas; pairs emit
   `oncollide` once per contact, re-armed on separation).
4. Compose background + sprites into the buffer via §2.11's AND/OR masks
   — transparency is legal HERE because it happens in RAM; the
   no-masked-blit constraint binds gfx calls only.
5. Emit one `GFX_BLIT1` per dirty 8-aligned band run: a two-sprite frame
   is 2–4 blits ≈ 2–5 ms, comfortably inside the 55 ms frame — honouring
   obscured/clip as a worker must (SPEC.md §20.6).
6. Poll `OSAPI_KEY_DOWN` (SPEC.md §9.7's held-key API) and enqueue
   `onkey` transitions; emit tones with `SND_TONE` (worker-legal, never
   channel 8).
7. Every `tick` frames, enqueue `ontick`.

**The game loop never executes a bytecode op.** JS sees discrete events
only. Where `GFX_BLIT1` is refused (kern_small), `WABF_CANVAS` already
refused the bundle at load (§10.2).

Surface — canvas: `start(fps)` (fps 1–18), `stop()`; sprite: `x`, `y`
(px, get/set), `vx`, `vy` (1/16-px per frame, get/set), `frame` (get/set,
0-based, < frame count), `shown` (get/set). Writes from JS land between
frames (the worker reads sprite state once per frame from the shared
records in the canvas claim; a single word each, no tearing on an 8086
word write).

### 6.11 Menus

`<menu>`/`<item>` compile to §2.6.2's blob; WEAVE registers them through
`OSAPI_MENU_SET` (5 menus max — the kernel's own bar bound, SPEC.md
§12.2), and the kernel draws and tracks them; `MENU_DIS` greying is free
and SPEC.md §47-correct. An item fires `oncommand`. The kernel's own Close item
is not the app's.

**The runtime keeps ONE of the five and the app gets four**, which is the
arithmetic §3.2's "at most 5 `<menu>`s" has to be read against.
`MENU_APPMAX` is the bar's own bound and the runtime needs somewhere to put
File → Open, Reload and Bundle Info; wave 2 spent two of the five on two
pull-downs, which would have left an app three. They are folded into one
named for the program. A bundle that declares a fifth menu gets its first
four and a toast saying so — a menu that is silently absent is a command the
user cannot find and cannot ask about (SPEC.md §47: refuse out loud).

### 6.12 Cards

`app.go(card)` switches cards with **one full-card repaint**, priced and
shown as such (a text-heavy card is ~71 ms per 78-cell row of it — a full
CGA card can be a second; the cost is the model's and the spec says so).
The HyperCard model substitutes for navigation, dialogs and dynamic
layout at once; `hidden`/`shown` covers the rest — there is **no dynamic
component creation** (§9.9), which is what keeps layout §7's single walk.

---

## 7. The flow-walk layout algorithm

### 7.1 The cell grid

Layout runs on the 8×8 cell grid of the window's **content area**, whose
truth is the live screen (`[vid_w]`/`[vid_h]`/`[vid_stride]` — never the
VGA reference constants; SPEC.md §39). Let `CW` = content width in cells
and `CH` = content height in rows of 8 px, both floored and both derived in
§7.1.2 — the floor is not the only thing that comes off `CW`. Positions are
cell-grid coordinates, converted to pixels only at paint.

**Cell (0,0) IS the content origin.** WEAVE adds no inset of its own; a
component that wants breathing room asks for a `spacer` (§6.3). That is a
decision, not an omission — §7.1.2 is what it buys.

#### 7.1.1 The three adapters' `CW × CH`, and the arithmetic behind them

**WEAVE's window chrome is the browser's** (SPEC.md §71): an ordinary
resizable window, no toolbar and no status strip of its own, opened at
SPEC.md §11.95's **standard rect** — the whole desktop band. Everything the
three adapters differ by falls out of that one rect, so the numbers below
are derived and checkable rather than measured:

```
frame:   x = 0                       y = MBAR_H
         w = [vid_w]                 h = [vid_dock_y0] - MBAR_H - 1
content: width  = w                  (a window spanning the screen has
                                      NEITHER side border - SPEC.md 11.95.2
                                      for the left and 11.95.3 for the
                                      right - so wm_geom answers w, not
                                      w-1 and not w-2)
         height = h - (TITLE_H + 1)   (wm_geom, SPEC.md 11)
```

`MBAR_H` is 20 and `TITLE_H` 18 (`apps/os88api.inc`), `DOCK_H` is 24
(`kernel/dock.inc`) and `[vid_dock_y0]` is `[vid_h] - DOCK_H`
(`kernel/viddet.inc`). The four subtractions on the height collapse to a
constant — `24 + 20 + 1 + 19 = 64` — so for every adapter:

```
CW = floor( [vid_w]       / 8)
CH = floor(([vid_h] - 64) / 8)
```

| adapter | screen | content px | `CW × CH` | wasted |
|---|---|---|---|---|
| CGA | 640×200 | 640 × 136 | **80 × 17** | 0 px × 0 px |
| Hercules | 720×348 | 720 × 284 | **90 × 35** | 0 px × 4 px |
| VGA mode 12h | 640×480 | 640 × 416 | **80 × 52** | 0 px × 0 px |

**`[vid_w]` and not `[vid_w] - 1`, and the whole column of waste went with
it.** The derivation above already says a window spanning the screen has
neither side border, so the content is `w` — SPEC.md §11.95.2 took the left
one and §11.95.3 the right. While only the left had gone, `wm_geom` answered
`w - 1` and the seven pixels the last cell could not fill were real; now there
is one more whole cell on every adapter instead. `tools/weavesim.py`'s
`ADAPTERS` and `tests/weavesmoke.py`'s frame model are the two other places
this number is written down, and all three say 80 / 90 / 80.

These are the grids `weavesim --render` prints and the 8086 must reproduce
exactly (§12). They are the **opening** grid and not a constant: a window
the user has resized re-runs the walk at whatever `CW × CH` it then has
(§7.4), and §7.4's 32×12 floor is what the family refuses to go below.
Nothing else in this document may hard-code 80, 90, 17, 35 or 52 — an
implementation that reads the numbers instead of the screen is wrong on two
adapters of three the moment either constant moves (SPEC.md §39).

#### 7.1.2 The content origin is rounded UP to a multiple of 8

**Binding, and it is a shipped defect class rather than a theory.**
`OSAPI_FONT_RUN`'s single-store fast path — the one that writes each cell
old-to-final in ONE store, so a run is never momentarily blank — requires
the pen on a multiple of 8 (SPEC.md §6.1). The walk emits cell coordinates
and converts to pixels only at paint, so cell column `c` is drawn at
`origin_x + 8c`: if `origin_x` is not ≡ 0 (mod 8) then **no component in
the family ever takes the fast path, at any window position, on any
adapter**, and every one of them draws the erase-then-letter pair instead
(PERFORMANCE.md Part 1).

That is exactly what happened to the line editor. `apps/os88line.inc` took
its pen from a content edge plus its caller's small inset — the browser's
is content+3, so the pen sat at content+6 — and `WF_SNAP` (SPEC.md §11.94)
makes the content origin 8-aligned, which pinned the pen at 6 mod 8 for
every window position on every adapter. Measured on a cycle-accurate
5150/CGA with PERFORMANCE.md Part 3.1's instrument, a 26-character URL:
**one keystroke flashed 246 transient pixels** over ~18 cells, and a
backspace 437 over ~21.

So, at **layout** time and not at paint:

```
origin_x = (content_left + 7) & ~7      ; UP. Down would put the pen inside
                                        ; the frame (os88line_pen's reason)
origin_y = content_top                  ; y is NOT aligned - font_run's
                                        ; requirement is on x alone
CW       = (content_left + content_w - origin_x) / 8
CH       = content_h / 8
```

Three instructions, and the whole family is on the fast path by
construction. The round costs up to 7 px of width, which `CW` has already
taken off — the `os88line_cols` pattern, so nothing downstream of the walk
learns about it. On §7.1.1's standard rect `content_left` is 0 and the
round is a no-op, which is the point: it is the **general** case it exists
for — a window the user resized, one that opted out with `WF_NOSNAP`, a
secondary display where x = 0 is not a screen edge (SPEC.md §39.17.2), and
the window too wide to snap that SPEC.md §11.94 leaves unsnapped rather
than illegal.

### 7.2 The walk — normative and deterministic

One pass over the current card's REC_COMP records in UISTREAM order.
Sprites are skipped (they live inside their canvas). State: cursor `x`
(cells), row list; each row collects components left to right.

```
for each component C (hidden components still take part — hiding does not
                      reflow; that is what keeps hide/show 1-2 calls):
  w = C.w if C.w > 0 else natural width (§7.3), clamped to CW
  if C has CF_BREAK, or x > 0 and x + w > CW:
      close the current row; x = 0
      (closing an EMPTY row is a NO-OP: no row is emitted. A CF_BREAK on a
       card's first component, and a second CF_BREAK straight after one, are
       both already at the start of a new row and must not produce a
       zero-component row - a row has no height without a component in it,
       and an implementation that emits one has no max() to take)
  place C at column x in the current row; x += w + 1   (one gutter cell)
close the last row (the same no-op rule: an empty last row emits nothing)
for each row:
  row height = max component natural/declared height in the row
  align: slack = CW - (sum of widths + (n - 1))
         (n = the row's component count. A row of n components carries n-1
          gutters, NOT n: the gutter `x += w + 1` left after the last
          component is the space before a component that went to the next
          row, and is not part of this one. Slack is never negative - every
          width is clamped to CW and a row closes before a component would
          overflow it)
         ALIGN of the row's FIRST component: left -> 0 shift,
         center -> shift floor(slack/2), right -> shift slack
row tops stack from row 0 downward, one blank pixel row (not cell) between
rows is NOT inserted - rows abut; components shorter than their row
top-align within it
```

The walk is integer arithmetic over at most 250 records — microseconds on
the target — and **re-runs at open and at every resize** (no anchor
springs, no constraint solver, no baked positions; the three adapters have
three different `CW×CH`). Same inputs, same layout, on every
implementation — `weavesim --render` prints the cell rectangles and the
8086 must match them exactly (§12).

### 7.3 Natural sizes

| component | natural w (cells) | natural h (rows) |
|---|---|---|
| `label` | max(1, text length) | 1 |
| `text` | CW (full row) | max(1, wrapped row count at its width) |
| `rule` | CW | 1 |
| `box` | declared (required) | declared |
| `spacer` | declared | 1 |
| `meter` | 10 | 1 |
| `button` | label length + 2 | 2 |
| `check`/`radio` | label length + 2 | 2 |
| `input` | cols + 2 | 2 |
| `list` | max(1, longest item) + 3 (scroll bar) | rows |
| `grid` | CW | CH − consumed rows above, min 6 |
| `canvas` | w px / 8 | h px / 8 |

The **floor of 1** in three of those rows is not defensive rounding. A
component with no content still occupies one cell, so that it can be seen,
hit, and given content later from script. Without the floor an empty
`label` is 0 cells wide and §7.2's `x += w + 1` collapses it into its
neighbour's gutter — a component present in the display list, invisible on
the glass, and reachable by `.text` from WJS with nowhere to draw the
answer. The same reasoning gives `text` a height of at least one row and a
`list` of empty items a width of `1 + 3`.

`text` wrapping is greedy word wrap at cell granularity: break at spaces; a
word longer than the width hard-breaks. **The algorithm is normative here
rather than by reference** — `tools/htmsim.py`'s `wrap()` is its one host
implementation and the 8086 implements from this text, never from that
source. In: the component's text folded to ASCII (§3.1) and split at
whitespace into `words` in document order, and `width` = the component's
width in cells, at least 1. Out: a list of lines, whose count is the
natural height in the table above.

```
pending = ""                                ; the line being collected
1. for each `word`, in document order:
   a. while len(word) > width:              ; too long for ANY line: break it
        i.   if pending is not empty: emit pending; pending = ""
        ii.  emit the first `width` characters of word
        iii. word = what is left of word
   b. if pending is empty:
          pending = word
      else if len(pending) + 1 + len(word) <= width:
          pending = pending + " " + word
      else:
          emit pending; pending = word
2. if pending is not empty: emit pending
```

Three things the sentence above does not pin and an implementer would
otherwise have to guess:

- **On a hard break the pending line is emitted FIRST** (1.a.i), before the
  over-long word's leading `width` characters. The word starts a line of
  its own; it never continues the line already in hand, however much room
  that line had left.
- **The remainder becomes the new pending line** (1.a.iii feeding 1.b), not
  a line of its own — so the tail of a hard-broken word goes on collecting
  the words after it.
- **The remainder may be EMPTY**, when the word's length was an exact
  multiple of `width`. An empty `pending` is overwritten by the next word
  at 1.b and emits nothing at step 2, so such a word adds no blank line.

The separator written between two words on a line is one space, one cell
wide, whatever whitespace stood there in the source (§3.1 collapses it) —
and `len(pending) + 1 + len(word)` is the whole of the fit test. Identical
rule in weavesim and the 8086: the wrap is part of the layout contract, and
`--render` diffs it.

### 7.4 Resize, minimums, clipping

The window's minimum content size per adapter is the family's floor:
**32×12 cells**, asked through the platform's resize negotiation. Below
what the card needs, components clip against the content box through the
platform clip (`WM_CLIP`) — the walk never produces overlaps, and
clipping is the degradation, not reflow-below-minimum. A resize re-runs
the walk and repaints the card once (the resize is the user's own action;
the price is visible and honest).

---

## 8. Builtins and the app API surface

### 8.1 The builtin table

`BUILT b,argc` — indices pinned. Wrong argc is a pack error.

| b | name | signature | notes |
|---|---|---|---|
| 0 | `alert` | `alert(msg [, fn])` | §8.2 |
| 1 | `timer` | `timer(ticks, fn)` | §8.2 |
| 2 | `saveState` | `saveState()` → bool | §8.3 |
| 3 | `loadState` | `loadState()` → bool | §8.3 |
| 4 | `playSound` | `playSound(name)` | §8.4 |
| 5 | `tone` | `tone(freq, ticks)` | §8.4 |
| 6 | `str` | `str(v)` → string | int/bool/null/string → its text |
| 7 | `len` | `len(s)` → int | string or array |
| 8 | `substr` | `substr(s, start, len)` → string | 0-based; clamped to the string |
| 9 | `find` | `find(s, needle)` → int | first index, −1 if absent |
| 10 | `rand` | `rand(n)` → int | 0..n−1 from §8.1.1's LCG; n ≥ 1 |
| 11 | `array` | `array(n)` → array | legal only as a `var` initializer (§4.2) |

`str`, `len`, `substr`, `find`, `rand` and `array` are **pure**: they read
and write nothing outside the VM claim, and they are therefore implemented
inside the bytecode core itself rather than reached through the runtime.
That is not a division of labour — it is what puts them inside the
`weavevm` differential corpus (§12.3), which runs the core in a raw boot
sector with no OS under it and so can call nothing that draws, sounds or
files. `alert`, `timer`, `saveState`, `loadState`, `playSound` and `tone`
go out to the runtime and are covered by `weavesession` instead.

#### 8.1.1 `rand` is a pinned LCG, seeded by `OSAPI_RAND`

`rand()` is `seed = (seed × 25173 + 13849) mod 65536`, then the answer is
`seed mod n` on the UNSIGNED seed. The state is `HS_SEED` (§4.7.2).

The generator is pinned rather than delegated because the model and the
machine have to agree op for op: `OSAPI_RAND` is the kernel's sequence and
`weavesim` cannot reproduce it, so a corpus case containing `rand` would be
untestable and — worse — would look like a VM defect the first time it
disagreed. What the platform's randomness is for is the SEED: the machine
takes `HS_SEED` from one `OSAPI_RAND` at VM start, so two runs of a game
differ; `weavesim --run` pins it at `0x1234`, so a scripted session is
reproducible; and the `weavevm` corpus pins it at `0x1234` too, which is
what makes `rand` a differential row rather than an excluded one.

(25173/13849 is the ZX Spectrum-era 16-bit LCG this tree already uses for
its own throwaway sequences; the low bits of any 16-bit LCG are poor, and
`seed mod n` therefore takes the whole word. It is a game's dice, not a
cryptographic anything, and §8.5 is the section that says why nothing here
is.)

### 8.2 `alert` and `timer`

`alert(msg[, fn])` raises the shared 3-button engine's one-button form
(`os88ui_ask`, OK only; message ≤ 34 chars — the alert window is the
kernel's 288×92 shape) and **returns immediately**; when a callback is
named, its `onalert` event arrives after dismissal. `timer(ticks, fn)` is
**one-shot** at 18.2 Hz granularity (55 ms floor — the platform's tick;
the faster FSX tick of SPEC.md §53.2.1 exists only inside the fullscreen
bracket, which v1 does not use); the `ontimer` event fires once; re-arm from the
handler for repetition. On kern_small `WM_TIMER` answers CF=1 and
`timer()` returns without arming — the app that needs it declared
`WABF_TIMER` and was told at load (§10.2).

### 8.3 `saveState` / `loadState` — the only file surface

The app's entire file access. State = the 128 globals, serialized:
`'WSV',0x1A`, version word (1), then 128 tagged cells with string/array
payloads flattened (strings length-prefixed; arrays count-prefixed;
component handles saved as comp_id; a handle that no longer resolves loads
as null). Written whole to **`SYSTEM/APPDATA/<bundle stem>.SAV`**, on
the UI task, inside the ONWAKE slice where file slots are legal, staged
through the transient claim (§1.4). Returns false — never a crash — on
refusal (no room, no file, no `SYSTEM/APPDATA`, write-protected disk), and
the status row says why. There is **no other file surface**: no open, no
read, no write, no directory listing, no path (§9.7).

**Not beside the bundle**, which is what this section said until wave 3
went to write one. SPEC.md §19.9 is the platform's rule and it is not
negotiable per family: *an application's own state goes in
`SYSTEM/APPDATA/` rather than beside the user's documents*, and a `.WAB`
is a user's document — wave 7 puts them in a writable `PROJECTS/` folder
(§13.1) precisely because that is where the user keeps things. A `.SAV`
dropped next to one would be the pattern SPEC.md §19.9 exists to prevent,
appearing in the same folder listing the user browses for apps, and on the
common arrangement — the bundle on a data floppy, the system on the boot
disk — it would also be the one write that lands on the disk more likely
to be write-protected. The path is reached with §19.9's own
bank/`GOTO`/act/`GOTO`-back idiom, tolerating absence.

The cost, named: **two bundles with the same 8.3 stem share one `.SAV`**,
wherever they came from. That is the flat-namespace consequence of §19.9
everywhere in this system, it is what the sentence in the status row will
be about when it surprises somebody, and the alternative (a per-volume or
per-directory qualifier) is a path vocabulary §9.7 does not have.

`weavesim` writes `<bundle stem>.SAV` beside the bundle it was handed,
because a host has no `SYSTEM/APPDATA` and `--run` is a single-shot
harness; the SERIALIZED BYTES are the contract the two share, and they are
what `weavevm` diffs (§12.3).

### 8.4 Sound

`tone(freq, ticks)` is the floor and always present (`OSAPI_SND_TONE`,
speaker; the one worker-legal voice — the canvas worker uses it natively;
channel 8 is reserved and never the app's). `playSound(name)` plays a
named clip via `SND_PLAY` **with the documented cost stated to the
author**: it runs with the scheduler locked and **freezes the desktop for
the length of the clip**; a mouse click aborts it. Refused (returns, no
sound) where the capability is absent. v1 ships no clip-carriage in the
bundle — `playSound` refuses politely until a wave adds a clip section,
and the packer warns on its use; it is specified now so the name is not
re-invented.

### 8.5 What does NOT exist — the privileged surface

No bytecode op and no builtin reaches: file I/O beyond §8.3, the network,
`OSAPI_*` slots, other windows or instances, the clipboard, the loader,
memory claims, or dynamic component creation. The tree is fixed at pack
time; `hidden`/`shown` and cards are the dynamic UI. A bundle is data; the
worst a hostile bundle can do is waste its own slices — and §4.11 bounds
even that.

---

## 9. The exclusions

Each exclusion names its platform fact and where the fact is pinned. These
are **enforced at pack time** (§10.5) wherever a source file could express
them — discovered at pack, never at run.

### 9.1 No hover

No `:hover`, no mouseover/mouseout, no tooltips, no hover cursors. **No
passive mouse-move event exists**: `W_ONDRAG` fires only between the press
and the release of one gesture, and polling `OSAPI_MOUSE` alongside
tracking is forbidden (SPEC.md §13.7). The markup has no hover vocabulary
at all, so the gap is closed at the spec, not met in the field.

### 9.2 No CSS and no colors

Style is §2.5.2's closed byte — bold, invert, align, cell w/h. Grey rounds
to black on 1bpp, two of three adapters are 1bpp, and half-honoured
fg/bg pairs produced invisible text twice in this tree
(docs/BROWSER-PLAN.md §2.2.1). State never rides on color (SPEC.md §39.4).

### 9.3 No per-frame JS

No requestAnimationFrame, no frame callbacks beyond §4.11.1's bounded
`ontick`. At 10–30k ops/s a frame budget is ~500–1,600 ops while ONE
native 78-cell text row already costs 71 ms; game loops are native worker
code and JS receives discrete events only.

### 9.4 No text parsing in the runtime

WEAVE interprets bytecode and display lists only. Parsing lives in
`weavesim` (host) and `LOOM.OVL` (on-machine), because nothing on the
machine compiles C (SPEC.md §73) and the pack step is the only compiler
surface.

**One carve-out, and it is FX in the formula bar** (§6.9.2). A spreadsheet
whose cells cannot be typed into is not one, and a cell's source is text: it
is compiled where it is typed, by a resident recursive-descent compiler over
the whole of §5.1's grammar, emitting §5.3's RPN. The exclusion still holds
where it matters — **no WML and no WJS is ever parsed on the machine by
WEAVE**, so the display list and the bytecode still arrive compiled and the
pack step is still the only surface that compiles a program. What is
carved out is one expression language of nine productions and eight
functions, entered one line at a time by a person who is looking at the
result. The line this draws is the same one §5's own split draws: WJS is the
app's, FX is the user's.

### 9.5 No floats, longs, closures, objects, `this`, `new`, try/catch, regex, eval

16-bit int is WJS's number type — the toolchain has no long/float
(SPEC.md §73.7) and the VM inherits that honestly. Formulas get 32-bit
16.16 fixed point in the assembly FX VM instead (§5.2). There are no
bitwise operators in WJS v1 either — pinned so the grammar stays closed.

### 9.6 No transparency in any gfx call

`GFX_BLIT1`/`GFX_BLIT4` are opaque and no masked blit exists; sprite
transparency is resolved by AND/OR masks during RAM composition, built at
pack time (§2.11); XOR is the only reversible on-screen mark.

### 9.7 No arbitrary file I/O and no network

`saveState`/`loadState` against the `.SAV` file on the UI task is the
entire file surface (§8.3); a worker may not touch a file (SPEC.md §20.6
rule 7) and the network is a driver (SPEC.md §72) the runtime does not
bind in v1.

### 9.8 No timers under 55 ms in a window

The tick is 18.2 Hz; `WM_TIMER` is one-shot at that granularity and
refused entirely on kern_small. Faster ticks exist only inside the FSX
fullscreen bracket (SPEC.md §53), which v1 does not use.

### 9.9 No dynamic component creation

The tree is fixed at pack time; `hidden`/`shown` and cards cover dynamic
UI. This is what keeps layout §7's single flow walk instead of an engine.

### 9.10 No compressed bundles and nothing over 62KB

The directory-entry size must stand for the resident requirement so the
refusal happens before any disk I/O (SPEC.md §61.4's Frotz rule), and
every internal offset is a 16-bit word (§2.1).

### 9.11 No silent SND_PLAY

`SND_PLAY` runs with the scheduler locked, freezes the desktop for the
clip, and a click aborts it — stated wherever `playSound` is documented
(§8.4). `TONE` is the floor and the only worker-legal voice.

### 9.12 No 128KB machines

kern_small refuses `GFX_BLIT1`/`WM_TIMER`/`WM_ONDRAG` and its ~22KB heap
cannot hold a bundle + VM. WEAVE refuses at launch with the arithmetic
(§10.1). The family's floor is 256KB, one app at a time (§1.4).

### 9.13 No tabs, no multi-window apps, no popups

One window per instance; cards for screens. No tabbed panes, popup menus
or context-menu API exists in the WM (SPEC.md §12.2 gives menus; nothing
gives popups) and the family does not invent them.

---

## 10. Refusals

Refusal is a normal, visible path (SPEC.md §47: grey a fact, never a
guess; refuse with the arithmetic, never silently). The sentences are
pinned so three implementations refuse identically.

### 10.1 Memory — before any I/O

Ask = `ceil(filesize/1024) + vmKB + gridKB + canvasKB` from the directory
entry and the header (§2.2). Refuse when `OSAPI_MEM_AVAIL`'s total is
short OR its largest free run cannot hold the largest single claim:

> `This app needs <N>KB; the largest free run is <M>KB.`

on the glass, C64-SPEC §1.4 style: the window comes up, the sentence is in
the content area, the status row keeps it, and the toast fires too —
never a bare failed launch.

### 10.2 Capability — kern_small and absent slots

Tested by the slot's CF answer, per flag bit (§2.2.1), at load:

> `This app draws on a canvas (GFX_BLIT1); this kernel does not carry it.`
> `This app uses timers (WM_TIMER); this kernel does not carry them.`

`WABF_CANVAS` refuses the load; `WABF_TIMER` loads with the sentence in
the status row once, `timer()` inert and the input caret static (§6.7,
§8.2).

### 10.3 Missing or unreadable bundle

The C64-SPEC §1.4 shape, copied: window up, content area names the file
(`FORM.WAB missing`), permanent status-row line, toast as well. A
double-click that reaches a deleted file and an empty Deck directory both
land here.

### 10.4 Malformed bundle

Bad magic, version ≠ 1, size word ≠ directory size, section count under
5 (§2.2), section out of bounds, unknown section type, unknown ctype,
reserved bits set, atom id out of range, **a required property absent**,
**a property outside §3.3's range**:

> `<NAME>.WAB is not a Weave bundle (<field>).`

with the field named. The runtime never guesses past a bad header — every
byte off a disk is hostile (SPEC.md §19).

The last two are the ones a reader is likeliest to skip, because the packer
already refuses them at pack (§10.5, §11.3) — and a `.WAB` on a disk need
never have been through a packer. **A required property absent is
malformed, never defaulted**: §3.3 requires `group` on a `radio`,
`cols`/`rows` on a `grid`, `w`/`h` on a `box`, `w` on a `spacer` and
`w`/`h` on a `canvas`, and §7.3 reads `box`'s, `spacer`'s and `canvas`'s
straight out of the record as their natural size — so a bundle missing one
lays the card out around a number nobody wrote. The field is
`required property`. **A property outside its range is malformed too**:
§3.3 bounds a `meter`'s `max` at 1–32000, an `input`'s `cols` at 2–60, a
`list`'s `rows` at 1–40, a `grid`'s `cols` at 1–26 and `rows` at 1–256
with cols×rows ≤ 6,140 (§5.6), a `canvas`'s `w` at 64–320 and a multiple
of 8 and its `h` at 32–160. The field is `property range`. Both are
checked as the PROPS blocks are read (§2.6), before any of them reaches
the walk.

### 10.5 Pack-time refusals — the sentences name the platform fact

Format: `<file>:<line>: <message>`. The binding examples (weavesim and
LOOM.OVL print the same text; the corpus in `tests/weave/packerr/` holds
one case per rule, §12):

| trigger | message |
|---|---|
| unknown element | `<zap>: not a Weave element; the inventory is closed (WEAVE-SPEC 3.2)` |
| unknown attribute | `button: no such attribute "color"; style is bold/invert/align only - two of three adapters are 1bpp` |
| hover vocabulary | `onhover: no hover exists; pointer movement reaches a package only between press and release (SPEC.md 13.7)` |
| color vocabulary | `color: there are no colors; grey rounds to black on 1bpp and state never rides on color (SPEC.md 39.4)` |
| oversize bundle | `bundle is 68112 bytes; the cap is 63488 - the directory size must stand for the resident ask` |
| ontick over budget | `ontick handler is 91 ops; the cap is 64 - per-frame JS does not fit 10-30k ops/s` |
| too many atoms | `188 app atoms; the cap is 187 - atom ids are one byte` |
| grid too big | `grid is 26x256 = 6656 cells; the cap is 6140 - the cell store plus its pool must fit a 26KB claim` |
| inline script | `script: inline script is not packed; name a .WJS file - the runtime never parses text` |

Unknown events, bad arity, undeclared identifiers, frame/stack overdepth
and every §3/§4/§5 limit refuse in the same voice: what was written, the
bound, the fact.

### 10.6 Script errors — at run time

A script error stops the current handler (stacks cleared, ring kept),
puts the sentence where the runtime has one to put (§10.6.0), and the app
lives on:

> `Script error in <fn>: divide by zero.`
> `Script error in <fn>: out of string space.`
> `Script error in <fn>: array index 12 of 10.`
> `Script error in <fn>: too deep.`
> `Script error in <fn>: grid pool full.`

The CODE section carries no name table (§2.8), so `<fn>` is the function
INDEX (`fn 3`) — unless the bundle carries SOURCE, in which case the
overlay's diagnostics resolve the index to its name. Pinned so nobody
invents a name table the format does not have.

#### 10.6.0 There is no status row while a card is up

"The status row" is §10.1's — the bottom row of the content area — and it is
available exactly when a card is NOT painted over it. §7.1.1 gives the family
no status strip of its own and §6.12 gives the card the whole content box, so
a script error raised while an app is running has nowhere to put a line that
is not on top of the app's own last row.

It therefore goes to the **toast** (SPEC.md §59 — the platform's transient
row, which costs this window no pixels and takes itself down) and is KEPT in
the runtime's status string, where the overlay's Bundle Info shows it to a
user who missed it. §8.3's refusals take the same route. The Deck and
§10.1–§10.4's refusal screens still use the status row, because on those
there is nothing else in the box.

#### 10.6.1 The complete list

Five examples were not a contract. This section opened by saying "the
sentences are pinned so three implementations refuse identically" and then
gave a sample — and `tools/weavesim.py` raises **eighteen** of them, of
which the commonest by a distance (`type mismatch.`) was not among the
five. An 8086 core written from the sample would have invented its own
wording for every error an app actually hits. So: this is the whole set,
and it is the set the `weavevm` corpus (§12.3) diffs.

| sentence | raised by |
|---|---|
| `type mismatch.` | arithmetic on a non-int pair (§4.4), `NEG`, an ordered comparison that is not int-int or str-str, `GETP`/`SETP`/`CALLM` whose receiver is not a component, `INCG`/`DECG` on a non-int global, `AGET`/`ASET` on the wrong types, `str`/`len`/`substr`/`find`/`tone`/`alert`/`timer` on the wrong argument type, a string written to a numeric property or a number to `text`/`label` |
| `divide by zero.` | `DIV` or `MOD` with a zero divisor |
| `out of string space.` | a concatenation over 255 bytes; an arena allocation that does not fit after a collection; the 65th list-item override (§4.8.1) |
| `too deep.` | the eval stack reaching 64 cells, or `CALL` at 16 frames |
| `array index %d of %d.` | `AGET`/`ASET` out of range |
| `bad opcode.` | §4.5.1's bounds, every one of them |
| `bad builtin.` | a `BUILT` index of 12 or more |
| `no component %d.` | a comp_id no card in this bundle declares |
| `no property "%s" on a %s.` | an atom outside §6's get/set surface for that ctype |
| `no method "%s" on a %s.` | ditto, for `CALLM` |
| `no method.` | a method atom legal for the ctype that this wave does not implement |
| `list index %d of %d.` | `list.get`/`list.set`/`.sel =` out of range |
| `grid cell %d,%d of %dx%d.` | a grid method's 1-based row/col out of range |
| `cell is #DIV0.` | `grid.cell()` on a cell holding the FX error value (§5.2) |
| `cell %s is out of int range.` | a 16.16 cell whose integer part is not a signed 16-bit int |
| `frame %d of %d.` | `sprite.frame =` past the sprite's frame count |
| `card %s of %d.` | `app.go()` outside 1..card count |
| `start(%s): fps is 1..18.` | `canvas.start()` |
| `rand of %s.` | `rand(n)` with n < 1 or a non-int |
| `grid pool full.` | the cell store cannot take another cell (§5.6) |

`%d` is decimal with a leading `-` where negative; `%s` renders an int the
way `str()` does. The sentence always ends in a full stop, and the whole
line always begins `Script error in fn <N>: `, where N is the index of the
function the error was raised INSIDE — the innermost frame, not the
handler the event named.

### 10.7 The runaway alert

§4.11's `Script is still running.` — Stop / Wait. The one modal the
runtime ever raises on its own.

---

## 11. The pack step

### 11.1 Two packers, one output

`weavesim --pack PROJECT/ -o MYAPP.WAB` on the host; `File → Pack Bundle`
(Cmd-P) in Loom, whose LOOM.OVL carries the compilers and the bundle
writer. **The gate is byte identity**: Loom's pack of every demo and
template must equal `weavesim --pack`'s output byte for byte (§12.4) —
two implementations written from this file, sharing no code, which is
what makes an on-machine compiler trustworthy at all. §2.14's determinism
rules exist for this gate.

### 11.2 A project

A folder: `MAIN.WML` (required), `MAIN.WJS` (iff a `<script>` names it),
`SPRITES.WSP` (iff sprites, §3.6), `SHEET.WFX` (iff a grid has initial
cells: lines `<cellref> = <formula|number|"label">`, one per cell, packed
in file order into CELLS row-major). All 8.3 names, plain files any
editor could touch.

### 11.3 What the packer validates

Everything §3, §4, §5 and §10.5 state, plus: every event names a defined
function; every `id` referenced from script exists; radio groups have ≥ 2
members; the entry card exists; section arithmetic (§2.2–§2.3) checks
before write; the finished bundle re-reads through the packer's own
independent reader before it is written to disk (a self-check, not the
t_wab gate — that one stays independent). Errors list with line numbers;
Loom shows them in the sidebar and jumps the caret to the first
(pack-on-save is Loom's default; caret-to-error is the loop's whole
speed).

### 11.4 On-machine specifics

Pack stages the image in a transient 63KB claim, writes the `.WAB` whole
on the UI task, and refuses politely (toast + sidebar) when the overlay
cannot load or the claim cannot be had — Pack is a menu command and menu
commands may refuse (SPEC.md §73.14).

---

## 12. The testing contract

### 12.1 weavesim — executable spec, oracle, table generator

`tools/weavesim.py` is written FIRST and is three instruments (the
htmsim precedent, which found 3 real bugs before any 8086 existed):

1. **The executable spec**: parses, compiles, packs, lays out, runs WVM
   and FX, and renders (`--render`) the cell-grid layout per adapter.
2. **The oracle**: every differential gate diffs the 8086 against it —
   end states, transcripts, layouts, recalc results.
3. **The generator**: `--emit-optab` (the WVM jump table), 
   `--emit-foldtab` (the Latin-1 fold, from htmsim's one definition),
   `--costs` (§14's table), `--emit-vmcorpus` (§12.1.1) — shared tables
   the model and the 8086 cannot drift apart on.

#### 12.1.1 `--emit-vmcorpus` — the differential corpus, generated

`python3 tools/weavesim.py --emit-vmcorpus <dir> -o <out.inc>` compiles
every `.wjs` in `<dir>` (sorted by file name — the harness has to visit the
cases in the model's order or a comparison is not one), runs each on the
model's own WVM, serializes the end state by §8.3's rules, and writes ONE
nasm `%include` carrying, per case: the CODE section bytes exactly as a
`.WAB` would carry them, the ATOMS section, the entry function index, and
the expected end state — plus, for a case that ends in a script error, the
expected §10.6 sentence.

It is a **generator and not a test**: the file it writes is assembled into
`apps/weave/hosttest/weavevm.asm` beside the SHIPPING `wvm.inc`, and the
comparison happens on an 8086 in raw QEMU (§12.3). The corpus lives in
`tests/weave/vmcorpus/`, one `.wjs` per subject, and each file's first
comment line is the case name the harness prints.

Two rules, both learned elsewhere in this tree and both load-bearing here:
**the model must visit the cases in the harness's order** (`tools/c64dec.py`
says so about a checksum and it is just as true of a listing), and the
corpus carries **negative controls** — cases whose expected state is
deliberately wrong, which the harness must FAIL. A differential that cannot
see a broken core has proved nothing.

#### 12.1.2 `--emit-fxcorpus` — the FX VM's half of the same gate

`apps/weave/wfx.inc` is a second interpreter and it gets the same treatment,
in the same boot sector, generated by
`python3 tools/weavesim.py --emit-fxcorpus <dir> -o <out.inc>`. The corpus
lives in `tests/weave/fxcorpus/`, one `.fx` file per subject, each carrying a
`grid <cols> <rows>` line, `<cellref> = <number|"label"|=formula>` lines in
`.WFX`'s own syntax (§11.2), and then `? <formula>` lines — the expressions to
evaluate. The generator compiles each with the model's `FxCompiler`, evaluates
it against the model's own cell store, and writes per case: the cell store as
§5.6 bytes, the compiled RPN, and the expected 16.16 result or `#DIV0`.

**It is generated from a cell store and not from a table of answers**, which
is the point: the machine's FX VM reads the same §5.6 image the runtime's does
— dense array, pool slots, kinds 1 through 6 — so a defect in how a cell is
READ shows here rather than in the app. The negative controls are the same
rule as §12.1.1's: cases whose expected answer is deliberately wrong, which
the harness must FAIL.

The FX rows run **before** the grid is wired to the VM, which is §13.1's own
ordering said again: an interpreter diffed after its component is built
reports its defects as widget defects.

`--selfcheck` runs its unit corpus and the pack/read round-trip;
`build/.weave-hostchecks` stamps it as a prerequisite of the future
`.raw.asm`, so a broken model stops the 8086 compile (the RunCPM stamp
pattern).

### 12.2 t_wab — the independence rule

`tests/unit/t_wab.py` reads the demo `.WAB`s that `all` packs host-side
and asserts header arithmetic, section alignment and bounds, atom-pool
integrity, UISTREAM record validity, CODE jump-target bounds. It is
written from THIS document and **shares no code with any packer** — the
`tools/wordfmt.py` pattern: two implementations agreeing by accident of
shared code is the failure the rule exists to prevent. It registers as a
fast-tier row (host-side, no build, inside every `make`).

### 12.3 The suite rows

Respecting the enforced tier budgets (fast 30 s host-only; full 600 s —
~8 emulator rows for the whole repo, nearly spent; soak unbudgeted):

| tier | row | what |
|---|---|---|
| fast | `t_wab` | §12.2 |
| fast | (checkdocs) | picks up WEAVE-SPEC/WEAVE-PLAN citations automatically once tracked |
| full | `weavesmoke` | MartyPC boots, opens FORM.WAB, asserts drawn-window STRUCTURE (never a golden screenshot) on both 1bpp GLaBIOS twins; needs=(marty,), serial — the family's ONE full row, forever |
| soak | `weavevm` | raw-QEMU SS≠DS boot-sector differential corpus vs weavesim (the rcz80test shape) — **both cores**: the WVM's end states (§12.1.1) and the FX VM's results and errors (§12.1.2) |
| soak | `weavesession` | MartyPC scripted replay of a real session, diffed against `weavesim --run`'s end state |
| soak | `weavegfx` | pixels-vs-model with no goldens — transcript diffing is structurally blind to drawing defects (zgfx's whole reason) |
| soak | `weavegrid` | recalc vs weavesim + incremental-equals-full-repaint (the tests/tpdraw.py identity gate) |
| soak | `weavegame` | wirefps/wireflick with PONG.WAB as the load |
| soak | `weavelat` | uilat's cycle-exact bar with a Weave form as the load |
| soak | `weavepack` | Loom's pack byte-identical to weavesim --pack, in the OS |

Every `tests/weave*.py` is registered in `tests/suite.py` or excused in
t_registry with the needs-make-weavedisk reason — never silently
unregistered. `os88test.py soak -k 'weave*'` is the family's command and
belongs in the pre-release ritual.

#### 12.3.1 What `weavesession` actually reads, and why not a transcript

The row was drafted as "COM4 0x3E8 `-DWVHARNESS` scripted replay, the
zharness shape" — a build of the runtime that prints a transcript on a
serial port. Wave 3 does not build that, and the reason is worth writing
down rather than quietly diverging: **a transcript is a claim the program
makes about itself.** Frotz needs one because an interpreter's whole
output is text and there is nothing else to compare; WEAVE's output is a
picture and a set of component states, and a build that speaks about them
on a wire is a second implementation of the thing under test — the
`-DWVHARNESS` build could be right about a machine the shipping build gets
wrong. It is also a permanent tax: every state a later wave adds has to
learn to serialize itself.

So `weavesession` drives the SHIPPING package under MartyPC and reads
back only facts that are on the glass or in the kernel's own window table:

- a `<meter>`'s fill, in pixels, which is an exact reading of the `value`
  the VM wrote (`doGreet` sets `count.value = greets`);
- a `<check>`'s glyph, before and after a click, which is `checked`;
- whether an ALERT WINDOW exists — `alert()` raising and its callback
  arriving after dismissal are two separate, structural facts;
- that a row of the card CHANGED, where the change is a string the model
  predicts but the reading cannot spell (§12.3.2).

and diffs each against `weavesim --run` given the same event script. Every
one of those is a value the bytecode computed, arriving through the whole
stack — ring, slice, `SETP`, painter, primitive — with nothing in the path
that exists only for the test.

#### 12.3.2 Reading text off the glass is by CONSISTENCY, never by faith

Where a session's assertion is about a string, the row reads the cells with
`tests/rczex_ocr.py`'s harvesting reader (SPEC.md §74's own instrument) —
the 8×8 face comes from the machine's BIOS at boot (`kernel/font.inc`) and
is not in this tree, so there is no font to compare against and glyphs are
LEARNED from rows whose text is already known: the card's static labels,
which are painted from ATOMS before a line of bytecode has run, and the
menu bar's. A character the learning rows never contained reads back as
`?`, and the assertion is: **every learned glyph must match the model's
string, and no learned glyph may appear where the model says another
one does.** That catches `HELLO, x!` against `Hello, x.` on the `e`
alone, and it never fails for a font this tree cannot see.

Stated because the alternative — asserting the string outright — would
report a machine with an unusual BIOS font as a broken VM.

### 12.4 The gates that bind

- **The SPEC.md §7.3 latency bar**: 37–70 ms click-to-action, measured by
  tests/uilat.py's cycle counting — the JS slice design must not push a
  Weave form past it.
- **wirefps / wireflick** (SPEC.md §78.9's instrument): frame rate and
  flicker with the game as load — the canvas's double-draw and pacing
  gate.
- **The tpdraw identity**: every incremental redraw pixel-identical to a
  forced full repaint — each rule it enforces was a shipped defect first.
- **The zgfx shape**: pixels against the model's bookkeeping, no goldens.
- **The pack identity**: §11.1, host vs machine, byte for byte.
- **The field-measurement rule**: no performance claim ships on emulator
  evidence alone (docs/FIELD-MACHINES.md binds whoever reads a result);
  §4.12's contract number is PENDING until the wave-5 field run, and the
  About banner is how the machine reports its own.

The three emulator-invisible defects — visible redraw, double-draw flash,
input overrun — are exactly a widget library's failure modes, and every
drawing change gets looked at on a 1bpp adapter before it is called done
(SPEC.md §39.4).

---

## 13. The waves, and what is deferred

### 13.1 The waves

Wave 1 shipped this document, docs/WEAVE-PLAN.md, `tools/weavesim.py`,
`tests/unit/t_wab.py`, the three demo sources, and the host-side pack of
the demos in `all`.

**Wave 2 shipped `apps/weave/`** — `WEAVE.O88` + `WEAVE.OVL`, the first C
package in this tree to declare a file association (which took a `CC_ASSOC`
path in `apps/cc/crt0.asm`, since no C package had ever needed one), the
accept idiom, the §10 refusals, the flow walk and the static components —
plus `make weavedisk` in three geometries and `tests/weavesmoke.py`. A
wave-1 fix pass preceded it: 21 defects in this document and in `weavesim`,
of which the load-bearing one was §7.1.1's Hercules grid, wrong at 89×36
because it had been inherited from the browser's viewport rather than
derived.

**`vm/xt-weave` and `vm/386-weave` came forward out of wave 7** — a 640KB
IBM XT at 4.77MHz with `build/weave360.img` in B: and a 386DX/25 with
`build/weave.img` — because a runtime whose whole subject is what fits and
what refuses on a period machine (§1.4) needs that machine available from
the wave it becomes bootable, not from the wave that packages it. Nothing
else in wave 7 moved with them: the `xt` target's 256KB one-app refusal is
still that wave's row, because it needs WEAVE on a disk the `xt` machine
sees, which is the ALLAPPSFILES work. Both machines are manual evidence —
`make xt-weave` launches 86Box and cannot assert that anything booted, so
no gate in this family rests on either.

**Wave 3 shipped the interaction and the VM** — `apps/weave/wvm.inc`
(section 4's machine in assembly, on the generated dispatch table), the event
ring with §4.9's whole policy, §4.10's adaptive slices, §6.5–§6.8's arm/fire,
the `os88line` field and its caret, §8's builtins, §10.6's script errors and
§4.11's runaway alert, and §1.7's `^R` Reload. It is gated FIRST by
`weavevm`, the raw-QEMU differential (§12.3), which found its own first defect
on its first green run: `wvm_alloc` banked an object's type in `DH` and then
loaded `DX` with the handle, so every dynamic string and array was filed as
free — reads worked and the next allocation quietly took the same handle,
which is invisible until a second string is alive at once.

It also **crossed §1.2's 55,000-byte split trigger**, and the body that moved
into `WEAVE.OVL` is the bundle validator: it runs exactly once per bundle, at
open, on the UI task, which is §1.2's own test for a tenant. The pre-named
candidates were spent. `weave.o88` is 42,704 image + 12,674 bss = 55,378
resident, `WEAVE.OVL` 9,518.

Fifteen amendments landed in this document before the code that needed them,
each listed in the wave's pull request; the load-bearing ones were §10.6.1
(five example sentences were not a contract — the model raises eighteen, and
the commonest was not among the five), §4.8.1 (the component-string slots are
GC roots, without which the collector frees the string a label is currently
displaying) and §4.5.1 (every indexed operand is bounds-checked, because a
`.WAB` on a disk need never have been through a packer).

**Wave 4 shipped the `<grid>`** — WEAVE-SPEC 5.6's cell store in a claim of
its own, `apps/weave/wfx.inc` (section 5's RPN machine and its 16.16
arithmetic in assembly, because this toolchain has no `long` at all),
`apps/weave/wband.inc` (§6.9.1's band composer, rcband's shape and Set 68's
constants, now re-measured as PERFORMANCE.md Set 113), §5.5's sliced two-pass
recalculation with §5.5.1's per-row damage, the formula bar over `os88line`,
and `apps/weave/wfxc.c` — §9.4's one carve-out, the whole of §5.1's grammar
compiled where it is typed.

It was gated FIRST by the FX half of `weavevm` (§12.1.2), which found nothing
only because the corpus was written before the core; `weavegrid` and
`weavegfx` found the rest, and the load-bearing one was that `g.cell()`
answered 0 for every cell — SHEET's `bump()` therefore computed 0+1 rather
than 12+1 and the sheet recalculated to a total that looked entirely
plausible. Nothing but the picture would have caught it, which is what
§12.3's pixel rows are for. `weavegrid`'s first green run also caught the
MODEL: `tools/weavesim.py` ran §5.5's passes at the end of a HANDLER, so a
cell committed from the formula bar never recalculated at all.

**It crossed §1.2's trigger by a long way and spent the tenant list a second
time.** Tenants 6 and 7 moved out (the grid's load path and the whole commit,
each with the paragraph §1.2 asks for), and the wave still did not fit
SPEC.md §20.1's 61,440-byte ceiling, so three structures changed shape: the
per-component SCREEN rect table went (2,000 bytes of bss to avoid four
multiplies, rebuilt on every edge anyway), the layout record lost its `row`
field and is ten bytes rather than twelve (a row is a contiguous run of the
table), and the dirty-component set became one bit a comp_id. `weave.o88` is
50,360 image + 10,502 bss = **60,862 resident**, `WEAVE.OVL` 19,475 — 578
bytes under the ceiling, which is the number wave 5 has to plan around.

The rest, each gated before the next begins:

| wave | ships | the gate |
|---|---|---|
| 2 | WEAVE viewer: CC_PACKAGE from day one, the Frotz accept idiom verbatim (ASSOC16, ARG_FILE banking, first-paint spend, §10.1–§10.4's refusals), flow walk, static components, list with scroll | `weavesmoke` on both 1bpp adapters |
| 3 | interaction + the VM: widget arm/fire, os88line input, event ring, `wvm.inc` (gated FIRST by the raw-QEMU differential corpus), adaptive slices, onclick/onchange/onkey, alert/timer/tone/state builtins, `^R` Reload | `weavevm`, `weavesession`, the §7.3 bar via `weavelat` |
| 4 | `<grid>`: cell store, `wband.inc` benched against Set 68's numbers (`make weavebandbench`), per-row damage, formula bar, `wfx.inc` + the formula compiler (§1.2.1's tenant 7, not resident — the size line decided it), sliced recalc | `weavegrid` (recalc vs model + tpdraw identity), `weavegfx`, and the FX half of `weavevm` FIRST |
| 5 | `<canvas>`/`<sprite>`: `wspr.inc` mask composition, dirty-band emit, worker loop, AABB, KEY_DOWN input, worker tones, ontick budget enforcement | `weavegame` (wirefps/wireflick); **commission the field run** — 5150 fps and the XT ops/s reading that converts §4.12 to measurement |
| 6 | Loom: `lm_` editor transplant, project folder + file switcher, LOOM.OVL compilers + packer, Pack, Preview, templates, APPDATA prefs, W_ONCLOSE/ASAVE close guard | `weavepack` byte-identity on all templates and demos, in the OS |
| 7 | distribution: `make weavedisk` in three geometries with cluster-fit refusal + `--verify`, writable `PROJECTS/` folder, CATALOG.TXT, `BUNDLES=` knob; ALLAPPSFILES rows (one `WEAVE/` folder — package + overlay + bundles share it, SPEC.md §19.10); the 256KB one-app refusal exercised on the `xt` target | the release checklist |

Wave order within a wave follows the size line: `os88pkg.py`'s resident
count is printed and recorded every wave, 55,000 is the overlay-split
trigger, and the pre-named OVL candidates (§1.2) move first.

### 13.2 Deferred, with the arithmetic attached

Recorded in docs/WEAVE-PLAN.md; listed here so the spec says what it is
NOT promising:

- **A kernel launch-by-name slot** — an owner conversation; kern_small has
  0 budget bytes spare, kern_big 512. The Reload loop (§1.7) dissolves the
  IDE's need at zero kernel cost; this is an upgrade path, never a
  dependency.
- **MOVABLE claims** (SPEC.md §66) — v1 pins every claim; handle
  indirection (§4.8) and base-relative addressing keep adoption a
  per-claim, three-word decision later.
- **Syntax highlighting in Loom** — zero precedent in the tree; the CHP
  substrate (SPEC.md §68.3) doubles document memory. Deferred with that
  arithmetic.
- **FSX fullscreen games** (SPEC.md §53) — the missile-command bracket;
  v1's canvas is windowed, arkanoid-class.
- **v2 cut list, written in advance**: multiline field, a second grid,
  clip carriage for `playSound`, SOURCE-section editing conveniences.

---

## 14. Appendix: the component cost table

**Regenerated by `weavesim --costs`** — do not edit the numbers by hand;
the model owns them (calibrated against the measured constants: 756 µs
per gfx call, ~900 µs per glyph cell, ~71 ms per 78-cell row, band
composer 860 µs/call + 173 µs/cell, PERFORMANCE.md Set 68). Regenerate
after any change to §6 or to the model. Field figures land on the 5150
and supersede modelled ones row by row (§12.4).

**The band composer's two constants were Set 68's and are now measured for
`wband.inc` itself**: PERFORMANCE.md **Set 113** ran the shipping file on Set
68's own harness and solved 915 µs a call and 162 µs a cell against 860 and
173 — six per cent apart in opposite directions, on a harness whose quantum is
one count of 0.359 ms. A 79-cell row measured **13.7 ms** against the model's
14.5 for those cells (the rows were 79 cells when Set 113 was taken; SPEC.md
§11.95.3 has since made a full CGA row 80, which the table below prices). Set 113 also settles the one claim §6.9.1 was making without
evidence: inverting the header band and the selected cell costs **0.4%**, one
count over eight iterations of 79 cells, where a second `gfx` call would have
been ~756 µs.

| component | interaction | gfx calls | modelled cost |
|---|---|---|---|
| label | .text set (20 cells) | 1 | ~19 ms |
| text | repaint (per wrapped row, 40 cells) | 1/row | ~37 ms/row |
| rule / box / spacer | card paint | 1 / 1 / 0 | ~0.8 ms |
| meter | .value delta | 1 | ~0.8-1 ms |
| button | press+release | ~2 + label | ~1.5-9 ms |
| check / radio | toggle (one glyph) | 1 | 35-50 ms (field) |
| input | keystroke | ~2 cells | ~1.8 ms |
| list | selection move | 2 (XOR) | ~1.6 ms |
| list | scroll one line | 2 | ~83-90 ms |
| grid | edit one cell (compose+blit 1 row) | 1 | ~3-5 ms |
| grid | selection move (2 XOR rects) | 2 | ~1.5 ms |
| grid | 80-cell row compose+blit | 1 | ~14.7 ms |
| grid | full 20-row page | 20 | ~294 ms |
| grid | scroll one row (GFX_SCROLL + 1 composed band) | 2 | ~83-90 ms |
| canvas | frame, 2 sprites (dirty bands) | 2-4 | ~2-5 ms |
| card | switch (full-card repaint, text-heavy CGA card) | ~1/row | ~0.3-1.2 s |
| card | first paint, fully lettered CGA 640x200 (17 rows x 80 cells) | 17 | ~1.26 s |
| card | first paint, fully lettered Hercules 720x348 (35 rows x 90 cells) | 35 | ~2.88 s |
| card | first paint, fully lettered VGA 640x480 (52 rows x 80 cells) | 52 | ~2.62 s |
| alert | raise + dismiss | ~8 | ~30-40 ms |

A change that moves a row of this table upward is a regression against a documented
number, not a neutral refactor - PERFORMANCE.md Part 5's discipline as a table.

A change that moves a row of this table upward is a regression against a
documented number, not a neutral refactor — PERFORMANCE.md Part 5's
discipline, applied to the family as a table rather than a hope.
