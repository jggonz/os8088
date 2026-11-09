# How PageDesk was built

The build diary for PageDesk (SPEC.md 127), written for later reuse on the
website. CLAUDE.md, *Building an app or game in waves*, item 6, is the rule
that asks for it. It is written the day the last integration landed, from the
agents' own reports (`*-report.md`, `*-verify.md`), the lead's `DECISIONS.md`
and the logs, which live outside the repository in `~/Repos/pagedesk-logs/`.
The screenshots that are quoted are in that directory's `shots/<agent>/`; none
is committed. Where this diary says a thing was **seen**, a report says so and
names how; **intended** and **not verified** mark the rest. Numbers are quoted
from the reports, never rounded up.

Companion documents: `docs/PAGEDESK-SPEC.md` (the contract),
`docs/plans/PAGEDESK-PLAN.md` (the story list and platform record),
`docs/PAGEDESK-GUIDE.md` (the user guide) and
`docs/plans/PAGEDESK-PM-ADR.md` (the protected-mode decision record).

## 1. The assignment

The owner's words, from `PAGEDESK_PROMPT.md`: *"Build out PageDesk. There may
be existing PDF exporting functionality in Tex, but we may want to extend that
or create a better PDF engine using opensource code from online sources. The
attached images show the design."* The full assignment is thirty user stories
(PD-01 to PD-30) and three mockups: a Hercules monochrome workspace with
thumbnails and a notes panel, a VGA colour workspace showing the Text
properties inspector, and a CGA compact workspace with a note popover.

The constraints that shaped everything:

- the baseline is an **IBM PC/XT**: 8088 at 4.77 MHz, 640 KiB, no FPU, no EMS,
  no XMS, no network. A protected-mode version is "an optional later
  extension, never a substitute";
- an explicit **PageDesk XT PDF profile**, not "every PDF 1.x file". Header
  version never decides support;
- unsupported content must say why: "Incomplete rendering", never a blank page
  passed off as correct;
- honest labels: a drawn signature is not a certificate signature, Delete page
  is not redaction;
- a static mockup, a browser app, or a sidecar file does not count. "No
  required story is called done because it has a mockup, stub, TODO or success
  toast without a real result."

## 2. The stories and where they stand

Status as of the last integration (`feat/pagedesk` 8e00bef0d): **S** seen
working (evidence cited), **P** partial, **I** integrated and host-checked,
**-** not done. The authoritative table is in the plan.

| ID | story | state | evidence and the gap |
|---|---|---|---|
| PD-01 | launch as a native application | S | `.PDF` association, dock icon, empty view: shell |
| PD-02 | full VGA workspace | S | full layout, both panels, QEMU |
| PD-03 | CGA 200-line workspace | S | compact layout, QEMU, real renderer |
| PD-04 | Hercules monochrome | S, with a caveat | Hercules 720x348 under QEMU with the RAM framebuffer harness; unusable for PageDesk by wave 3 (heap reaches the fake framebuffer); real MDA/Herc model is the final pass |
| PD-05 | self-contained fonts and graphics | S | Liberation bitmap strikes, AFM widths, 29 icons, all committed with sources and licences: fonts |
| PD-06 | open real PDFs, bounded | S | host and target diff of the real module in QEMU: 234 cases, 0 mismatches: eng |
| PD-07 | render text, vectors, images | S | host renders vs `mutool draw`: 0.0 to 0.3% of cells differ on samples; on the glass: rend, rendfix, annotr |
| PD-08 | responsive, bounded rendering | P | banded, sliced, cancellable (seen); XT timings are the final pass |
| PD-09 | navigate and scale | S | nav |
| PD-10 | select, copy, find | S | VGA and CGA: nav-verify |
| PD-11 | thumbnails, outlines, links | S | VGA; Outline unreachable on compact |
| PD-12 | notes | S | popover, panel, Resolve/Reopen (demo store and a hand-made file) |
| PD-13 | highlight, area mark | S | edit-verify, nav-verify |
| PD-14 | text boxes | S | typing, properties |
| PD-15 | ink, shapes, signature | S | one-stroke signature; multi-stroke not built |
| PD-16 | changes, undo/redo | S | 64 steps, page ops included |
| PD-17 | save a real PDF | S | fresh and incremental; `qpdf`, `mutool`, `pdftoppm` judges |
| PD-18 | reorder, rotate, delete, insert pages | S | undo of Rotate seen |
| PD-19 | extract, insert from another PDF | S (VGA) | Extract and Insert, saved and read back; known limits in the guide |
| PD-20 | new document, recovery | S for the journal, host-checked for the interrupted-save offer | the close-window Save/Discard/Cancel exists; New/Open/Close document do not ask |
| PD-21 | reproducible samples | I | `tools/os88pdfgen.py`, eight samples, byte-identical on regeneration |
| PD-22 | host preparation | I | `tools/os88pdprep.py`, 61 checks |
| PD-23 | keyboard and file workflows | S | keys, menus, Preferences |
| PD-24 | package and disk images | I | `make pagedeskdisk`, four geometries, all `--verify` OK; only 1.44 MB booted |
| PD-25 | more RAM, Flate | P | Flate decodes and draws `FLATE.PDF` on VGA; extended-memory caches not built |
| PD-26 | AcroForm filling | P | not selected: forms are shown as printed with a said baseline (D24) |
| PD-27 | protected-mode decision | I | `docs/plans/PAGEDESK-PM-ADR.md` |
| PD-28 | malformed input, failing storage | P | engine fuzz and 51-file negative corpus (host); writer refusals (host and VGA); the final pass runs the rows |
| PD-29 | hardware, memory, responsiveness proof | in progress | the `rows` agent: `tests/pagedesk*.py`, MartyPC |
| PD-30 | documented working behaviour | this | `README.TXT`, the guide, this diary |

## 3. How the work was organised

PageDesk is far larger than one context can hold, so it was built by a lead
and a fan-out of agents in the `story-fanout` shape (CLAUDE.md, *Shared agent
skills*).

- **One worktree and one branch per agent**: `~/Repos/os8088-pd-<key>`, branch
  `pagedesk/<key>`, never the main checkout. Staging by name only (an earlier
  project lost user files to `git add -A`).
- **A brief every agent read first**: `COMMON.md`, extended by an addendum at
  each wave. It pinned the log protocol, the code rules, the budgets, who owns
  which files and which SPEC subsection.
- **A contract before code**: `docs/PAGEDESK-SPEC.md` and the ABI in
  `apps/pagedesk/pdabi.h` and `pdmath.h` were written by the lead first. Each
  agent wrote its own subsection (20.N) and nothing else.
- **A decisions file**: `DECISIONS.md`, D0 to D27, re-read at every milestone.
  An agent that needed something not in the ABI wrote `BLOCKED` or `PROGRESS
  ASK` and carried on.
- **One owner per branch (D7)**: when an implementer logged DONE, its Opus
  verifier owned the branch.
- **A log line per milestone**, `[key] HH:MM STATUS: ...`, and a report file
  per agent; a watchdog read the logs and nudged quiet agents.
- **The testing rule**: agents verified on the host (`make`, which runs the
  fast tier, and their own harnesses) and looked at a picture once per
  adapter; **no soak rows and no `test-full` until a single final pass**
  (CLAUDE.md, *Testing*). This is why the on-target timing is not in this
  document yet.

The work was delivered as 28,033 lines of source under `apps/pagedesk/`
(C, assembly, includes and headers, by `wc -l`; the host harnesses, the
fonts and the samples are extra), and a 4,500-line contract.

## 4. Wave 1 (2026-11-05 evening): the components

The lead inspected the platform first and recorded it in the plan (section 2
of the plan): the package ceiling (`APP_MAX_SIZE` 65,024 bytes), the C
toolchain rules (SmallerC, no `long`, no `&local`, frames under 96 bytes), the
UI task's 512-byte stack, `READ_SEQ` and its cluster rules, the heap (about 534
KiB free on a 640 KiB XT with `kern_big`), the missing atomic rename, the lack
of a print service, and that **TeXPad writes PDF but nothing in the tree reads
it** (the owner's "Tex" remark). Decisions L1 to L8 followed (section 7).

Seven agents, each owning one segment or tool and proving it on the host:

| agent | model | built | headline |
|---|---|---|---|
| math | Sonnet | `pdmath.inc`: 16.16 fixed-point, 32-bit, matrix and view transforms in 8086 assembly; `pdabi.inc` generated from `pdabi.h` | 41 functions, 120,000 differential cases against C under an x86 emulator, 0 failures; every deliberate mutation caught |
| fix | Sonnet | the generator and an independent checker, eight samples, stress set, 51-file negative corpus | deterministic: regenerate twice, `diff -r` identical |
| eng | Sonnet | PDENG: file layer, lexer, xref by subsection table, page tree, filters, outline, annotations | 35,952 bytes; 234 cases identical host vs QEMU target |
| rend | Sonnet | PDREND: interpreter, paths, band rasteriser, glyphs, images, text map, thumbnails | 48,638 bytes after an assembly row scan; fuzz found two real bugs |
| fonts | Sonnet | PDRES: Liberation strikes, widths, icons, art | core bank 77,268 bytes; extra sizes in `PDFONTX.RES` |
| wrt | Sonnet | PDWRT: serialiser, annotation appearances, fresh and incremental writer | 36,416 bytes; `qpdf --check` clean on every output |
| shell | Opus | the package: parts table, claims, job scheduler, three layouts, menus, keys, stubs, disk rules | the spine everyone integrates into |

Each had an **Opus verifier** after it logged DONE, whose reports are the
`*-verify.md` files. The verifiers found much of what mattered: line-end
arrows swapped in the writer (wrt-verify), a disagreement between nav and edit
on whether a count was in quads or in points (verify-nav), the structural
stack overrun behind the page-3 corruption (verify-annotr, section 8.1).

The first vertical slice (launch, open `HELLO.PDF`, render, add a note, save,
reopen, independent check) was integrated by the lead as the wave closed, with
stand-in modules where the real ones were not yet merged.

## 5. Wave 2 (2026-11-05 night): the stories on the slice

Seven feature agents plus two fixers, all on the integrated slice:

| agent | stories | built |
|---|---|---|
| edit | PD-13 to PD-16 | the edit store (128 records), undo, the annotation tools, hit-test, move and resize |
| notes | PD-12, PD-16 | the Notes & edits panel, the note popover, review state, Text properties inspector |
| nav | PD-09 to PD-11 | zoom, history, selection, Copy, Find as a job, links, thumbnails, outline |
| pages | PD-17 to PD-20 | PDWRT page-tree rewrite, Extract, byte-copy Save As, New, the page map |
| annotr | PD-07 on screen | PDREND draws source appearances and the edit store's marks |
| flate | PD-25 | FlateDecode, PNG/TIFF predictors, a 43 KiB slot, asm symbol loop |
| host | PD-22, PD-27 | `os88pdprep.py`, the protected-mode decision record |
| rendfix | launch hang, page-3 corruption | see section 8 |
| deepstk | the stack | D23's private module stack, see section 8 |

## 6. Wave 3 (2026-11-06 and after): the carried stories

Four agents on a nearly full program:

| agent | built |
|---|---|
| insert | PD-19 Insert from another PDF: a merge job running the Extract pipeline per source file, numbering from the previous round, the page tree written last |
| recov | PD-20: the recovery journal, the `PDSAVE.$$$` finish-rename offer, the `/NM` seed |
| prefs | PD-23 Preferences; PD-26's honest baseline |
| polish | the five carried defects: the VGA light-grey fill, the double paint, the stale status line, Esc in a gesture, XOR overlays |

Wave 3 was interrupted for about two and a half days by the weekly usage limit (section 10)
and integrated on 2026-11-09 at 08:40. The final pass was then split between
two agents: `rows` (the instruments and rows of PD-29) and `docs` (PD-30,
this).

## 7. The decisions, D1 to D27, and L1 to L8

The lead's platform decisions (plan section 2.1):

| # | decision | why |
|---|---|---|
| L1 | C for components, assembly for inner loops and arithmetic | every parser and writer is then host-compiled and tested against qpdf, MuPDF and Poppler; the OS keeps pixels in assembly |
| L2 | three far-called C modules, a primary and an overlay and an asset part, in one `.O88` | the code cannot fit 65,024 bytes; LOOM.WPV is the precedent |
| L3 | no worker task; jobs stepped from the wake handler | a parser needs file I/O and overlays mid-operation, both forbidden on a worker |
| L4 | xref by subsection table and 20-byte entry reads | an index in RAM is unbounded; the format's fixed width bounds the reads |
| L5 | no Print | no print service exists; an inert button is dishonest |
| L6 | Flate is Enhanced, reusing PiXEL's inflater | its 32 KiB history must meet the XT gates first (it did not end up shared: D17 note) |
| L7 | generate every sample deterministically | the owner's sample kit was not supplied |
| L8 | the disk stores samples RAW | `READ_SEQ` delivers a file as it lies; WaveRack's clips are the precedent |

The lead's running decisions in `DECISIONS.md`:

| ID | decision | why |
|---|---|---|
| D1 | `READ_SEQ`, `WRITE_SEQ`, `VOL_KIND`, `FERR_TEXT` thunks added once, by eng | one owner for SDK additions |
| D2 | module INIT may be called again, only with no document open; a field of 0 is "no claim now" | the primary claims per document and frees after; modules hold nothing across |
| D3 | PDRES is not loaded whole: the primary reads only its index (at most 8 KiB); PDREND reads strike bitmaps on demand | memory |
| D4 | when the display list overflows, re-scan the content stream for a narrower row range | bands are blitted opaque; the framebuffer cannot be read back |
| D5 | PDRES carries the core sizes only; the rest ship as optional `PDFONTX.RES`, not on the 360 KB disk | a 360 KB disk must hold the program and the samples |
| D6 | no shared-exit tails in assembly | the `stkapps` gate refuses a `ret` reached at different depths |
| D7 | one owner per branch | an implementer and a verifier editing one branch is how work is lost |
| D8 | object numbers above 65,535 are `PDE_LIMIT` | they are 16-bit in every ABI struct |
| D9 | the edit store layout is pinned; colours are the VGA palette indices; `PD_PENUP` ends an ink stroke | wrt and rend need one definition |
| D10 | read the PDRES offsets from its directory, never constants; a tree with no pages is `PDE_SYNTAX`; ship `PDFONTS.TXT` | the OFL wants its licence with every copy |
| D11 | `PDE_BUSY` is CF=0, not a failure | resumable verbs |
| D12 | FULL layout from height 260, not 300; the primary carves per-document claims from two large claims | measured Hercules and EGA content boxes of 284-286 rows; the kernel caps an owner at 8 claims |
| D13 | PDREND's gstates and font cache live in a 4 KiB workspace in the display-list claim; the claim is 29 KiB | bss |
| D14 | Save As across folders: the writer reads from the source's folder and writes to the target's; `PDE_CHANGED` by fingerprint | correctness of "Save As into another folder" |
| D15 | PDREND draws every annotation, source and edit store; the primary draws no mark pixel; wave-2 rows carved into claim B | one renderer, one look |
| D16 | font strike = min(horizontal em, ceil(vertical em x 5/4)) | CGA and Hercules have tall pixels |
| D17 | Flate is gated by free RAM, never by CPU; a slot is 43 KiB; a low-memory switch forces it off | the same binary shows the XT behaviour on any machine |
| D18 | PDWRT 46 KiB | later raised (D24) |
| D19 | the overlay's cap is 48 KiB; shares by agent; one shared text buffer | SmallerC UI code is bulkier than the plan priced |
| D20 | PDREND hard cap 60 KiB | annotations |
| D21 | no recursion anywhere | the UI task's stack is 512 bytes |
| D22 | the UI stack must stay at or under 384 bytes in measured use | measured overrun by PDREND |
| D23 | a private module stack | section 8 |
| D24 | primary 58 KiB, PDWRT 48 KiB, overlay 48 KiB, PDREND 60 KiB; PD-26 filling not selected | wave-3 budgets; no room in any segment for filling |
| D25 | recov's primary share +1,150, overlay +300 | a bounded exception |
| D26 | settings are read at the first open or first dialog paint, not at launch | an empty-view launch must not fetch the overlay from a floppy |
| D27 | insert's primary delta cut to +280 by moving code to the overlay | 17 bytes over the cap |

## 8. Discoveries

These are the lessons that cost real time. Observations are first; what was
decided because of them is second.

### 8.1 The UI task's stack overran, and D23's private module stack

**Observed** (first by nav, while looking). After XTGUIDE page 3 was drawn, bytes of the document's content
stream (file offset 11262-11290) appeared in the kernel's `.bss` at the
menu-bar line buffer. It was first reported as a read with the wrong
destination. rendfix established that it was not a read: PDREND's recursive
Bézier flattener (about 50 bytes a level, 7 levels) overran the UI task's
512-byte stack (`LOW_SEG`) into the menu bar's data. The kernel's own deepest
measured use of that stack is 246 bytes. verify-annotr then measured PDREND's
worst non-recursive chain at 388 bytes (470 through the dash code), PDENG
adding 188 to 236 when PDREND calls it, and the kernel's file call on top.
Not a bug in one place: a structural overrun.

**First decision (D21, D22).** No recursion; an upper bound of 384 bytes of
measured stack use, a static gate (`tools/os88pdstk.py`, the fast row `pdstk`)
that computes the worst chain from each callback to the deepest leaf from the
generated assembly, and a request to hoist engine calls out of deep chains
and make locals static.

**Second decision (D23), after the lead read the kernel.** Modules run on a
foreign stack segment, and the kernel already tolerates a foreign SS: SeaBIOS
forces one on every `int 13h` and `int 16h`; `sch_switch` refuses to switch
when SS is not `LOW_SEG`; the int 08h chain runs in place; both mouse ISRs
load their own stack. What the kernel does not tolerate is a *kernel API call*
on a foreign SS. So:

1. the primary owns one 2 KiB deep stack (a row of claim B), filled with 0xCC
   with a canary at its bottom;
2. `pdu_mcall` swaps to it for every module verb and back;
3. every kernel call a module makes (`pdkgate.inc`) swaps back to the UI
   stack first, so the kernel runs at the primary's depth;
4. an unlisted far call into the kernel fails the module's *assembly*
   (`pdk_UNWRAPPED_KERNEL_CALL_...` is an undefined symbol), so a new call
   cannot slip past;
5. the canary is checked at every return; an overrun closes the document with
   `PDE_STACK`, not a corrupted menu bar.

D22's items 2 and 3 were withdrawn as requirements. **Seen** (deepstk, QEMU
VGA, real renderer): XTGUIDE pages 1, 3 and 12 draw; the menu bar stays intact;
deepest module stack 448 of 2,036 bytes; UI task high-water 258 of 512 bytes
(CGA: 386 and 252); the refusal path driven by zeroing the canary through
QEMU's gdb stub (the document closed with the stack message and the next open
re-armed the stack). **Not run**: kern_mega, kern_small, Hercules, MartyPC.

### 8.2 The static stack gate

The gate (`pdstk`) is a fast row, so every `make` enforces it. `cc_onkey`
first measured 380 of 384 and, once popovers were added, 388. The popover rule
below brought it to 366. The gate caught a 414-byte chain on `cc_onclick`
(notes' popover) before any boot did. It reads *generated*
assembly, so a stale `build/pdrend.gen.asm` (the Makefile does not rebuild the
renderer under `all`) made it report recursion that had already been removed;
two agents lost time to it.

### 8.3 The popover-at-the-wake rule

**Observed.** A key or click handler that painted a popover or a dialog
through the full paint chain put the callback at 388 of 384 bytes. **Rule
adopted by the lead's fix (a9cc94d37) and every later agent:** a key, click or
command handler does not paint through a deep chain. It sets a flag and calls
`pdu_kick()`, and the *wake handler* (`pdu_popown`, `pdu_popdmg`,
`pdu_pg_wake`, `pdu_rend_sync`) does the paint, taking `os88_gfx_lock()` itself
(callbacks already hold it). New page changes, Preferences paints and saves,
the Find pop-up's close, the inspector's Done all follow it. The side effect
is a one-wake delay on each, invisible at human speed.

### 8.4 The launch hang

The real PDREND hung PageDesk's launch (QEMU VGA, CPU at 0000:0003 in a #UD
loop). rendfix reproduced it with a hand-driven gdb remote client (there is
no gdb on the host) with a breakpoint at linear address 0. Root cause:
`verb_init()` called `close_streams()` before assigning the engine's far vector,
and on the first INIT the vector was the zeroed bss, so it far-called 0000:0000.
The host harness could not see it: its engine call was a plain function that
ignored the vector. The fix assigns the vector first, and `_pdr_eng` now
refuses with `PDE_NOTFOUND` while the vector is zero. The remaining proposal,
to make the harness fail when the vector is zero, is in the report and not
done.

### 8.5 The Flate carve

PD-25 asked for Flate "with an explicit XT fallback" and a budgeted 32 KiB
history. **Observed:** a decoder is 43 KiB with its predictor rows and tables
(slot k at `flate_seg + k x 2752` paragraphs), and PDENG had 7 KiB of share.
**Decided (D17):** gate by free RAM, not CPU; carve the slot out of claim B's
tail only when at least 32 KiB of heap stays free; one or two slots; a "low
memory" Preferences switch forces it off so the XT behaviour can be shown on
any machine. PDENG ended at 47,074 bytes (cap 47,104) with 30 bytes to spare.
The tables and saved state live in the slot, not in PDENG's bss. The first
decoder was C; the lead's go for an assembly symbol loop gave 1.4x fewer
instructions on text (90.2 to 63.4 per output byte) and 1.27x on a predicted
image (242.1 to 190.1), and **did not save a byte**. The inflater could not be
shared as source with PiXEL's `pxpng.asm` (WEAVE-SPEC 1.2's rule) without
changing PiXEL's only decode path with no PiXEL row allowed to run, so the five
tables are compared with `pxpng.asm`'s by a gate instead.

### 8.6 The size caps and how they were met

Every segment ended full. The brief's per-segment targets (primary 56 KiB,
PDENG and PDREND 40 KiB, PDWRT 40) were missed in the direction of C being 2
to 4 times the size of assembly, and each time the change went through the
lead as a numbered decision, not as a quiet overrun:

| segment | at first measurement | final | cap |
|---|---|---|---|
| primary | 41,421 | 59,235 | 59,392 |
| overlay part | 2,259 | 48,318 | 49,152 |
| PDENG | 35,952 | 47,074 | 47,104 |
| PDREND | 48,638 | 60,304 | 61,440 |
| PDWRT | 36,416 | 47,724 | 49,152 |

The levers that worked: moving code to the overlay (a lazy part: dialogs,
menus and tools cost the primary only their string literals, which stay
resident); moving tables into the document claims; putting bss that is read
rarely into the display-list workspace; an assembly row scan; writing the
point loops with pointers; shortening messages (a literal named by `ovl_*` code
is resident). At one merge the primary was 17 bytes over its cap and D27
cut insert back; it ended 157 bytes under, PDENG 30 under. Every segment is full: **the next feature starts by removing code.**

### 8.7 Memory claims and the owner cap

The kernel allows 8 claims per owner on `kern_big`. The shell's first open
failed ("PageDesk needs 74 KB") because the carve, exchange block, bank and six
document claims were nine. The fix, kept for the whole build, was to carve
sub-segments from two claims with `os88_seg_sub`; every later row (edits,
undo, pages, thumbnails, scratch, the deep stack, the Flate slots) was carved
out of claim B rather than added. The claim table stands at 63 KiB on VGA and
EGA and 60 KiB on CGA and Hercules.

### 8.8 Smaller discoveries

- The writer's first page-op save died with a STACK OVERFLOW because PDWRT
  calls PDENG from the bottom of its chain; plain wave-1 saves had never run on
  a target. A target boot of the save path was a gap in wave 1's rule of
  "host only".
- A save has to be able to refuse an unedited document *less* cleverly: PDWRT
  first answered `PDE_RANGE` ("nothing to save") on `MARKUP.PDF`; an
  unedited Save As is a byte copy (SPEC 14.2).
- `PDE_BUSY` was returned with CF=1 by PDENG while the shell expected CF=0
  (D11). Two agents read the same sentence two ways.
- `Esc` in the file save's key handler deadlocked the UI task in `gfx_lock`:
  a key arrives with the lock held and the job's ending takes it again. Esc now
  starts the job with a cancel flag and the wake does the work.
- The kernel's keyboard map doubles E0 keys once any package arms
  `os88_key_down` (memory note in the project). That is why Esc during a
  held-button tool gesture is **not** delivered: the polish agent checked and
  documented it (SPEC 20.19.5) instead of arming the map.
- A QEMU Hercules look needs `HERCSEG=0x7000`, whose fake framebuffer lives in
  RAM at 0x70000, where PageDesk's heap reaches. verify-polish stopped using it;
  Hercules moves to MartyPC's real model in the final pass.
- Build artefacts caused repeated confusion: `build/pdrend.part` is not tracked
  against `PD_REND_MOD`; `pagedesk.raw.asm` did not depend on the other `pdu*.c`
  files, so edits rebuilt nothing; every commit rebuilds every kernel because
  the About box's build number is the commit count.

## 9. What was measured

All figures are the reports'. Nothing is from a real 8088; nothing is from
MartyPC yet.

**Correctness against independent tools (host).**

- Engine: 234 cases, host vs the real module running in QEMU, 0 mismatches at
  cluster sizes of 1 and 4 sectors; agreement with `qpdf` on page count, page
  objects, inherited boxes, content-stream CRCs and every object offset;
  fault injection at every `READ_SEQ` call: an I/O error, never a crash.
- Renderer: 75 renders (5 pages, 3 zooms, 5 adapter and depth configurations);
  against `mutool draw`: XTGUIDE p2 0.3% of cells differ, MONOIMG 0.0%, HELLO
  0.3%, LATIN 0.0%; an ASan/UBSan fuzz of 38 hostile streams plus 800 random
  ones, 0 failures; 94 annotation checks and 52 renders with the real engine.
- Writer: `qpdf --check` clean on every output (fresh at four cluster sizes,
  incremental, second incremental, large source, failure paths); `qpdf --json`
  assertions on subtypes, `/QuadPoints`, `/NM`, `/IRT`, `/InkList`, `/LE`; the
  source's bytes are the output's prefix (SHA-256 checked).
- Arithmetic: 41 functions, 120,000 cases, every listed mutation caught.
- Flate: 938 engine runs; 24,000 fuzz runs; 60 streams on the real module,
  asm and C twin identical, with a negative control.
- Layout: 14,997,906 checks, 0 failed.
- Edit store: 178 checks including 40 random walks, three mutations caught.
- Recovery journal: 310 checks under ASan.
- Host preparation: 61 checks, including a negative control.

**On the glass (QEMU, kern_big, 640 KB).**

- Free heap with HELLO.PDF open (Help > Document info): VGA 446-447 KB (22 KiB
  display list) then 437 KB (29 KiB); CGA 451 KB; Hercules 450 KB. Equal across
  open, close, reopen, Save As, quit and relaunch: no claim leaked.
- Stack: deepest module stack 448/2,036 (VGA), 386 (CGA); UI task high-water
  258/512 (VGA), 252 (CGA).
- Layout: content boxes VGA 640x416, Hercules 720x284, CGA 640x136.
- Static stack gate: deepest UI chain 366/384 (`cc_onkey`), 360 (`cc_onwake`),
  PDREND 656/1,776, PDWRT 460, PDENG 350.
- A saved Insert (10 pages) reads back with `qpdf --check` clean and Poppler
  pixels of the inserted pages equal to the source's.

**Estimates from instruction counts (QEMU `-icount`; not clocks).**

- Flate text 63.4 instructions per output byte, a predicted image 190.1, a
  dynamic block's start 187,712. SmallerC output costs about 14 to 18 clocks an
  instruction; the author's reading is a few KB a second on an XT, text. The
  8088-clock figures for the math routines (`fx_mul` 288 us, `view_fwd` 814 us)
  are computed from instruction counts and the 8088 fetch floor, not measured.

**Final pass (PD-29).** The `rows` agent's report (`rows-report.md`) is where
the MartyPC cycle counts, the 640 KiB matrix on VGA, CGA and Hercules, and the
final soak union will be; at the time this diary was written it had not been
delivered. Whoever adds it should add a section here and in section 17 of the
guide, not edit the figures above.

## 10. Incidents

- **Weekly usage limit.** At 00:30 on 2026-11-06 the limit stopped recov,
  verify-insert, verify-prefs and verify-polish. The lead quit the two orphaned
  verifier QEMUs through their sockets and resumed all four by `SendMessage`
  on 2026-11-08, each told to commit its work in progress first. Net cost: about
  two and a half days.
- **Host sleep.** The host sleeps; wall-clock readings jump by hours. A
  `make` after the prefs merge ran 9 hours because a sleep stranded the
  `multiprocessing` pool of four fast-tier rows, holding the runner's pipes.
  The lead killed the five orphaned workers by pid and re-ran the four rows
  alone (all passed). recov's reported 45-minute "hang" of the C compiler turned
  out not to be a hang: `cc8086.py` takes 22 to 29 seconds standalone, and `ps
  etime` counts sleep. The lead withdrew its earlier diagnosis. **Lesson:** a
  background command does not wake an idle agent; wait in the foreground
  (`while kill -0 pid; do sleep 30; done`, up to 10 minutes a call).
- **Stalls.** Quiet agents were nudged by the lead. verify-flate stalled twice
  (45 minutes with no output) and was replaced by verify-flate2 on the same
  worktree. pages ignored two stop requests and was stopped; verify-pages
  became the sole owner. nav stalled twice and was stopped at a deadline; the
  lead committed its work in progress and verify-nav took over. verify-insert
  stalled and the owner chose a second nudge. Watchdog false positives: a
  daemonised QEMU has cwd `/`, and agents LOOKing produce files in `shots/`,
  not in the worktree.
- **Fast-tier wall clock.** Many `make` runs reported the 30-second budget
  exceeded with every row green, on a host running ten worktrees' builds. The
  rule was to re-run once, not to fix.

## 11. What is still open

Specific and testable. The first four are final-pass items.

1. **PD-29**: MartyPC cycle counts for the XT scenario of the assignment's
   section 6 on VGA, CGA and real Hercules at 640 KiB; the UI-stack high-water
   during a session (`stk0water`); the longest no-pre-emption window of the
   private stack; keystroke latency of text-box typing; Find and Copy on a full
   page.
2. Hercules on a real model; EGA; the 360 KB, 720 KB and 1.2 MB disks booted.
3. kern_small and kern_mega: not run. Documented exposure on kern_mega
   (option-ROM interrupts while on the private stack).
4. 86Box and physical machines: nothing was run. `docs/FIELD-NOTES.md` takes the
   results.
5. PD-26 filling; PD-25's extended-memory caches (a bigger cache bought almost
   nothing in measurement: reads on XTGUIDE 4, 4, 4 and on the 580 KB stress file
   107, 106, 106 for 16, 32 and 64 KiB).
6. Disable editing and saving on an Incomplete page (the assignment's PD-07;
   no check in `pdu_why`).
7. A Save/Discard question at New, Open and Close document (the primary has 157
   bytes left).
8. `CC_HAS_ONDRAG` in `apps/cc/crt0.asm`, which would retire nav's one-tick
   poll during drags.
9. Thumbnails by page id, a duplicate page's marks, an inserted page drawn as
   paper, `/JS` direct in an annotation's dict, a PDENG `/State` bit so a
   discussion reply does not reopen a note, `/Popup` kept on a rewritten
   annotation.
10. PageDesk reading `/PageDeskPrep` ("Prepared copy"); a text layer for
    rotated text; a merge back.
11. The same-size-middle-change blind spot of the fingerprint (size, first and
    last KiB) used by `PDE_CHANGED` and the journal.
12. A host-harness check that fails when a module's vector is zero.

## 12. Lessons for the next build

1. **Run the target early.** Wave 1 ran only on the host and QEMU stubs; the
   first real save overflowed the stack, the first real renderer hung the
   launch, and page 3 corrupted the menu bar. Each was found the first day the
   pieces met. Schedule an early slice that boots the real modules.
2. **Budget the stack like memory.** A static gate on call depth, written
   early, would have caught the structural overrun before four agents built
   on it.
3. **Pin the ABI in a file with a generator and a checker** (`pdabi.h`,
   `pdabi.inc`, `os88pdabi.py --check`). It cost one agent and saved the
   others a month of arguments.
4. **Make every size cap a numbered decision.** Every overrun here was asked
   for in the log and answered in `DECISIONS.md`, so the final tally is
   knowable. The shape that scaled: shares by agent, a hard cap per segment,
   and "move it to the overlay" as the first answer.
5. **A verifier per branch found what the author could not**, but the owner of
   a branch must be one agent (D7).
6. **Foreground waits.** Background commands do not wake an agent. Tell agents
   to wait in the foreground with timeouts.
7. **Name the instruments.** A test harness that cannot see the failure (a
   plain function standing in for a far call through a vector) is silent where
   the target fails.

## 13. Reproducing the build

```
make deps
tools/setup-cc.sh                        # the C toolchain, once
make pagedesk pagedeskdisk               # PAGEDESK.O88, four disk images
make pdeng pdrend pdwrt pdflate pdnav pdnotes pdfonts   # host checks, each its own
python3 tools/os88pdfgen.py build/pdfix  # the eight samples (byte-identical)
python3 tools/os88pdstk.py --top 6       # the stack gate
make test TESTAPPS=build/pagedesk.img    # boot to look
```

`tests/suite.py` fast rows that cover PageDesk: `pdfix`, `pdlay`, `pded`,
`pdnotes`, `pdjr`, `pdprep`, `pdstk`, `pdkgate`; full: `pdfixstress`. The
remaining target and soak rows are listed in the `rows` agent's report.
