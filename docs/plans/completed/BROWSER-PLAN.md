# A text-and-table HTTP browser for os8088

**Research document, not a contract.** SPEC.md is the binding contract for
what the kernel *is*; this is the study of what it would take to build a
**text-and-table-only HTTP browser as an os8088 package** that is usable on a
4.77 MHz 8088. Every interface named lands in SPEC.md before its code.

docs/plans/completed/NET-STACK-PLAN.md is where the bytes come from. This document assumes
they arrive and is about **what happens after that**, which is the harder
half and the one with no precedent in this tree.

---

## 0. The verdict, up front

**The network is not the problem. Drawing is.**

PERFORMANCE.md Part 2, measured on the field 5150:

| | Hercules | CGA |
|---|---|---|
| One 8×8 glyph cell | **901 µs** | 909 µs |
| One 78-cell row of text | **71.4 ms** | 72.7 ms |
| **A full text page** | **2.50 s** | **1.24 s** |

> **A full window of text costs between 1.2 and 2.6 seconds to draw, on every
> adapter this OS supports.** Everything below is a consequence of that
> sentence.

Set that against the transports: the cable is **3,741 bytes/second** measured
(PERFORMANCE.md Set 39), so a 20 KB page is 5.3 seconds of fetch — the same
order as three repaints. An Ethernet card is modelled at 85–330 KB/s
(docs/plans/completed/NET-STACK-PLAN.md §3.3), so the same page is under a second and **the
browser becomes render-bound, not fetch-bound**. The design has to be right
for the second case, because the second case is where this ends up.

**So the whole engineering problem is: never draw a page twice, and never draw
a row that did not change.** Three mechanisms do nearly all of it, and this
tree has already built and measured every one of them for something else:

| | what it is | precedent | worth |
|---|---|---|---|
| **the blit tier** | scroll the pixels you already have; letter only the rows that were exposed | SPEC.md §27.7.2 (Note Pad), §22.11 (the Disk window) | **1.24 s → ~0.2 s** per line |
| **input coalescing** | while events are still queued, accumulate the delta and draw **once** | `OSAPI_EVQ_PENDING` | the difference between a held arrow key working and **input overrun** |
| **bounded layout** | record where each display line starts; stop every walk at the bottom of the view | SPEC.md §27.5, §27.7.1 | Note Pad measured **72%** of its walk below the window |

None of that is speculative. The Disk window's one-row scroll went from **262
ms to 83 ms** by exactly this route (PERFORMANCE.md, §22.11), and Note Pad's
keystroke went from 404 walk iterations to 35.

### The three things to decide before any code

1. **Monospace 8×8 grid for v1** (§6). It makes tables column arithmetic, it
   makes scrolling whole rows, and it earns `font_run`'s single-store path for
   free. Proportional type is real here (SPEC.md §6.3) and **measurably
   competitive**, and it is v2.
2. **HTTPS is out of reach, so a proxy is a first-class configuration** (§8),
   not a workaround. Over the cable the DOS box is already in the room.
3. **Time-to-first-line, not time-to-page** (§3.3). The first screenful is
   ~71 ms of drawing; the rest of the document need not be laid out at all
   until somebody scrolls.

---

## 1. What "performant on a 5150" has to mean

Concretely, per adapter, for a maximised window:

| | screen | content | cols × rows | one full repaint |
|---|---|---|---|---|
| CGA | 640×200 | ~632×136 | **79 × 17** | **~1.2 s** |
| Hercules | 720×348 | ~712×290 | **89 × 36** | **~2.6 s** |
| VGA 12h | 640×480 | ~632×420 | **79 × 52** | **~2.5 s** |

Two things fall out of that table that are worth having in mind early.

**The better adapter is the slower one.** Hercules and VGA fit two to three
times as many rows, and a row costs what a row costs — so the *good* display
is where a full repaint hurts most. Any design that leans on repainting is
worst exactly where the user is most likely to be reading.

**And the CGA number is the one that flatters.** A 17-row window is a small
window; the reason its page is cheap is that it shows less.

So the targets this document holds itself to:

| operation | budget | why that number |
|---|---|---|
| scroll one line | **< 250 ms** | four lines a second is readable-by-tapping |
| scroll a page | **< 1 repaint** | never worse than the thing it replaces |
| hold the arrow key | **never falls behind** | input overrun is unrecoverable, §4.2 |
| first text on screen after a fetch | **< 200 ms** | §3.3 |
| resize / adapter switch | one repaint, **no re-parse** | §3.5 |

### 1.1 The three pages this has to reach

Named by the owner, and **none of them is allowed to become the design** —
a browser that renders three sites is not a browser. They are here because
each one demands exactly one capability the generic case does not, and
between them they set the feature floor:

| the page | what it demands | where it lands |
|---|---|---|
| **frogfind.com** — search for vintage machines | **basic forms**: one text input, one submit button, `GET` | §7 |
| **old.reddit.com**, signed in, through a reformatter we write | **nothing in the browser at all** — the session and the TLS live in the proxy (§8.3). Not v1 | §8.3 |
| **a demo site we write and host** | nothing. It is the one page whose markup we control | §1.1.1 |

**The middle one is the important finding and it is a relief rather than a
problem: signing in to Reddit is not a browser feature here.** Reddit is
HTTPS, its login is a modern auth flow, and §8.2 says why none of that is
reachable. So the session belongs to the reformatting proxy, which
authenticates once, holds the cookie, and serves plain HTTP to the 5150. The
browser stays **stateless** — no cookie jar, no credential store, no TLS —
and that is a smaller browser, not a compromised one.

#### 1.1.1 The demo site should be this project's test fixture

It is the only one of the three whose markup we control, which makes it the
natural **conformance target**, and this tree already builds fixtures for
exactly this reason — `tests/linetest` is a deterministic fan of lines that
exists so two kernels can be diffed byte for byte.

So: **write the demo site's HTML into the repo, not just onto the host.** It
does double duty — the page the owner serves, and the fixture stage 1 opens
off a floppy with no network in the machine at all. It should carry, on
purpose:

- one of every tag §2.2 claims to support, so "supported" is checkable;
- the **pathological cases**, because they are the ones that will not be
  encountered until a real page does it: a 40-character unbroken word, a
  table wider than any screen, a table with one cell, nesting 30 deep,
  `&amp;`-heavy text, high-bit bytes, an unclosed tag, a `<pre>` block;
- **a page longer than any window**, since every scroll tier in §4 is
  unreachable on a page that fits.

That fixture is worth having before the renderer, not after. It is also what
lets a browser change be verified the way everything else here is — an
identical scripted session against a reference build, diffed for **0
differing pixels**.

**The double duty came apart, and it is this section that was right.** For as
long as `demo.htm` was both the fixture and the file in `MEDIA/`, the page a
new user opened first was one written to torture a parser — every
pathological case above is deliberately there, and none of them says what the
toolbar does. SPEC.md §71.12 splits the two: `apps/browser/browser.htm` ships
and is the program's manual, `tests/htm/demo.htm` stays exactly what this
section asked for and stops being anything else. Both are on `make
browsertest`'s disk, so the shipped page is renderable under test too — which
it was not while it was the fixture.

#### 1.1.2 What FrogFind's front page settled, and what it did not

A copy of `http://frogfind.de/` is checked in at `tests/htm/frogfind-de-ie5.htm`
(the `.de` host of the same service; `.com` was down, and its search API quota
was exhausted, so **there is no form and no results page yet**). 5,289 bytes.
It answers more than it looks like it should.

**Settled — and it is the one the whole goal rested on: plain HTTP.** The
saved URL is `http://frogfind.de/` and every internal link on the page is
written `http://frogfind.de/...` — the dark-mode link, the leaderboards, the
about/imprint/privacy row, the language switch. A service that redirected to
HTTPS would not self-link that way. Goal one does not need a proxy.

**Settled — the page is small and ordinary.** 5,289 bytes is ~1.4 s over the
cable and nothing on a card, and it lays out to roughly 40–50 display lines —
two or three CGA screens. It is a comfortable first target rather than a
stress case.

**Settled — the tag set is exactly §2.2's, plus `<CENTER>`.** `H3`, `B`, `I`,
`SMALL`, `FONT`, `A`, `IMG`, `BR`, `TABLE`/`TR`/`TD`, `HR`-free, and eleven
`<CENTER>` elements. Entities appear as predicted: `&nbsp;`, `&lt;`, `&gt;`,
and `&#9749;` — a numeric entity for U+2615, well outside anything the cell
font has, which is the case §2.2's fold rule exists for.

**Settled — the encoding is `ISO-8859-1`, not UTF-8**, declared in a `<META
http-equiv=Content-Type>`. §2.2 assumed UTF-8 and now has to read the
declaration and handle both.

**NOT settled — the form.** The quota page replaced it. §7 is written from the
shape of a search box and not from this site's markup, and the search URL is
still unknown.

**NOT settled — and this is the methodological one — these are not the wire
bytes.** The file is Internet Explorer 5's *save*, and says so in its own
header: `<!-- saved from url=... -->` and a `MSHTML 5.00.2614.3500` generator
tag. What is in it is MSHTML's re-serialisation of its own DOM — tags
upper-cased, attributes normalised and **unquoted**, `<TBODY>` **inserted**
where the server almost certainly emitted none, `src` rewritten to a local
directory. Some of that is harder than the real thing and some is tidier, so
it cannot stand in for a capture.

**That is this tree's own recurring trap wearing new clothes**: a harness that
is kinder than the thing it stands in for hides exactly the bugs it exists to
find — `tests/lptlink/partner.py` read `NC_BYE` as "carry on" and a protocol
bug survived a whole scripted session looking perfect (see docs/plans/completed/NET-PLAN.md
about `tests/lptlink`). A parser developed against a browser's tidied output
will meet the server's real output on the 5150.

`tests/htm/README.md` carries the three `curl` lines that fix it. **The one to
run first is `opensearch.xml`**, because the page advertises a `<LINK
rel=search>` and an OpenSearch descriptor contains the search URL *template* —
so it names the query parameter and the method **without a search having to
succeed**, which is precisely what the quota is blocking.

---

## 2. The pipeline

Four stages, each with a bounded cost and each with exactly one thing it is
allowed to invalidate:

```
   bytes            document            line table          pixels
  (source)          (parsed)            (laid out)
     |                  |                    |                 |
     |--- parse ------->|--- layout -------->|--- paint ------->|
     |    ONCE, on      |    lazily, per     |    per row,      |
     |    arrival       |    view width      |    on demand     |
     |                  |                    |                 |
  invalidated by     invalidated by      invalidated by     invalidated by
  a new fetch        a new fetch         a WIDTH change     a scroll, and
                                                            only the rows
                                                            that moved
```

**The rule that makes it work is that invalidation only ever flows to the
right.** A scroll may not reach layout. A resize may not reach the parse. A
new fetch is the only thing that touches the source. Getting that wrong is how
a browser ends up re-parsing HTML to move one line, and it is the single
easiest way to lose all of §0's gains.

### 2.1 Source

The raw response body in a heap claim (SPEC.md §50.3), owned for the life of
the page. It is kept rather than discarded because §3.5's reflow and any
"view source" both want it, and because the parsed form points into it.

### 2.2 Document — parsed once

A compact intermediate: a sequence of **styled text runs** and **structural
markers**. Not a DOM — there is no scripting and no CSS to resolve, so a tree
buys nothing a flat stream does not.

```
  RUN     len, style, -> offset into source
  PARA    the paragraph break
  HEAD    n = 1..6
  LI      list item, depth
  PRE     preformatted: no wrap, no whitespace collapse
  TABLE   ncols, -> the first row
  ROW     / CELL  colspan
  HR      a rule
  LINK    -> href in the link table, and the run(s) it covers
  CENTRE  the alignment of the block that follows
```

`CENTRE` is there because §1.1.2 found **eleven `<CENTER>` elements on
FrogFind's front page** — it is how a 1990s page does layout, and on a
character grid it is padding arithmetic, so it is the cheapest thing on this
list. `<TD align=>` uses the same marker.

Everything else in the tag soup is **dropped at the parse**: `<script>`,
`<style>`, `<head>`, `<!DOCTYPE>`, comments, forms' unsupported controls, and
every attribute but `href` — `style=` in particular, which on that page
carries a `BORDER-BOTTOM: #ffffff 2px solid` on nearly every element and is
CSS the browser has no business reading. An `<img>` renders its `alt` text if
it has one and nothing at all if it does not. That is the "text and table
only" in the ask, done in the one place where doing it is free.

#### 2.2.1 Colour is dropped, and BOTH halves must go together

This is the one place where doing half the job produces **invisible text**,
and it has already happened twice in this tree.

SPEC.md §39.4 reduces sixteen colours to three at 1bpp — `0..6` black,
`7,8,9,10,11,13` a 50% dither, `12,14,15` white — **and glyphs round to black
rather than dithering**, because a dithered glyph loses half its strokes.
ModPlug's green LCD came out black-on-black that way, and Missile Command's
`CLGREEN` wave counter was not faint but *absent*.

FrogFind's front page is the trap in the wild: a `bgColor=#000080` table
cell containing `<FONT color=#ffffff>` text. Honour the background and not the
foreground and the text is black on black; honour the foreground and not the
background and it is white on white. **Both are a page that renders as an
empty box.**

So: **ignore `color`, `bgColor`, `text`, `link` and `vLink` entirely, and
render every glyph as ink on paper.** It is the only option that cannot
produce invisible text, it is smaller than the alternatives, and on the two
adapters this OS is calibrated for it loses nothing that was ever going to
survive the reduction.

That is not the same as SPEC.md §47's greying, which is a deliberate dither
behind `[gfx_dis]` and stays exactly as it is — a disabled control still says
so.

Three things the parse owes, and each is a real defect if skipped:

- **Whitespace collapse.** Runs of space, tab, CR and LF become one space
  outside `PRE`. Done here it costs one pass; done at layout it costs one pass
  per reflow.
- **Entities and character set.** The kernel's cell font is **ASCII 0x20–0x7E
  and nothing else** — `font_char` indexes past the glyphs it has above that
  (SPEC.md §6.1), so this is a correctness requirement and not a nicety.
  `&amp;` `&lt;` `&nbsp;` and the numeric forms fold to ASCII; UTF-8
  sequences fold to their nearest ASCII (curly quotes to straight, en/em dash
  to `-`/`--`, non-breaking space to space) and anything left becomes `?`.
  **The same rule SPEC.md §19.1 applies to a FAT name applies here and for the
  same reason** — this text is hostile input and it is about to be handed to a
  glyph renderer.
- **The fold applies to EVERY character, not only to entity expansions.**
  This is §2.5's third bug and it is worth stating as a rule because the wrong
  version is the intuitive one: an entity is the case an author thinks about,
  so it is the case that gets folded, while a page whose charset carries the
  accent as a **raw byte** — most of the German web, and `torture.htm`
  section 7 — hands `0xF6` straight to a glyph table with 95 entries in it.
- **An inline tag boundary is not a word break.** `below</a>.` is one word.
  Collapsing whitespace per text run and then rejoining the runs with a space
  puts one in — §2.5's first bug, which rendered `see below .` and
  `Intel 8088 , and`. The word accumulator carries across the boundary, and
  what decides a break is whether the *source* had whitespace there.
- **Bounding.** A `Content-Length` of 0xFFFFFFFF, a table claiming 4,000
  columns, a nesting depth of 500 — refused at the parse, where refusing is a
  compare, rather than at the buffer. SPEC.md §69.7 is the precedent: TeXPad
  bounds every buffer a document can reach **at the copy**.

**Cost, and this is the estimate least grounded in a measurement**: the parse
is a byte-at-a-time state machine over the source. At an 8088's ~4.34 clocks
per instruction byte and a plausible 15–25 clocks a character, 20 KB is
**~70–110 ms**. That is one row of text and is not where the time goes — but
it must be measured, not assumed, and it is the reason §3.4 parses in chunks
rather than all at once.

### 2.3 Line table — the load-bearing structure

**One entry per display line**, and it is Note Pad's `np_rows` (SPEC.md §27.5)
with more in it:

```
  LN_DOC    word   where in the document this line starts
  LN_LEN    byte   how many cells it occupies
  LN_X      byte   the cell column it starts at (indent, list, table cell)
  LN_FLAGS  byte   heading / rule / table row / continuation / has-link
```

Six bytes. A 20 KB page at ~60 cells a line is **~350 lines ≈ 2 KB**.

Everything the browser does at speed is a lookup in this table. *Which line is
at the top of the view* is an index. *What must I letter after a blit* is a
range of indices. *Where did the user click* is an index plus a column. **The
pixels and the table are the same question asked twice**, which is SPEC.md
§22's `fm_hit` discipline: one place owns the geometry, so the drawn thing and
the clickable thing cannot drift.

It is built **lazily and bounded** (SPEC.md §27.7.1): the walk runs from a
known line to the bottom of the view plus a small margin, and stops. Note
Pad's measurement is the argument — 72% of its layout work was below the
window, 6 walks and 10,079 iterations became 2 walks and 1,015.

**Two traps carried over from Note Pad, both of which will happen again:**

- **A word wrap's seed must go back further than the row it wants.** SPEC.md
  §27.4: the break in *front* of a row is decided by the length of the word
  *behind* it, so resuming exactly at the target row gets a different break
  than a full walk would. Note Pad seeds one row earlier and walks back
  through a long word. A browser has the same problem with the extra
  complication of indents changing mid-document.
- **A line index above the view is negative, and unsigned tests read that as
  past every limit.** SPEC.md §27.7's whole scrolling mechanism turns on that
  one word being signed. It broke Note Pad twice.

### 2.4 Paint — one opaque run per line

**A display line is drawn as `OSAPI_FONT_RUN`, space-padded to the full
content width, and there is no fill anywhere in the path.**

That is SPEC.md §27.2's rule and it is not about speed — it is about the
flash. The pair it replaces is `gfx_fill` of the band then `font_str` over it,
which leaves the line **blank between the fill and the last glyph**, and on a
4.77 MHz machine that is tens of milliseconds of visible white per line. A
space paints background on `font_run`'s fast path, so **the padding is the
erase** and the line is never momentarily empty.

It also removes SPEC.md §11.3's granularity trap by construction: a fill clips
per pixel and a glyph per whole cell, so anything that erases a rect and then
letters into it goes *blank* rather than stale where a clip edge crosses it.
One opaque run has no pair to disagree.

A line with mixed styles is several runs, each opaque and each padded to its
own span — never one fill and then the pieces.

### 2.4.1 A pending space belongs OUTSIDE the link that follows it

`231 comments  r/vintagecomputing` came back from the field as one link with
an underscore in the middle of it, and every character of it was right.

Whitespace collapses to a **pending** space (`br_wsp`) spent at the next ink
character — and for `</a> <a>` that character arrives *after* `D_LNK1` has
been written. The space therefore lands inside the second anchor's span,
`br_lmark` says 1 for that cell, and `br_underline` — which rules **one fill
per run of marked cells** (§2.4's own economy) — draws straight through the
gap. Two links read as one, and the gap reads as an underscore.

`br_wflush` is the fix: spend the pending space *before* the marker. It is one
routine rather than an inline test in `br_atag` because `br_char` wants the
same three lines and the next inline marker will want them too.

**A text comparison cannot see this.** `tests/brtest.py` checks the rendered
text against `tools/htmsim.py`, and the text is identical either way — the
defect is entirely in the 1-pixel rule under it. `tests/brlink.py` is the
gate: it opens `LINKS.HTM` (`AAAA` and `BBBB` on one line, `CCCCCCCC` on
another as a control) and asserts the **pixels**, two runs with unlit space
between them and one control run as wide as its word. A/B'd on a
cycle-accurate 5150: with the fix **32px, 8px gap, 32px**; without it a single
**72px** run, which is the field's report exactly.

### 2.5 The model — `tools/htmsim.py`

**Every cost in §0 and §1 was arithmetic on a page nobody had parsed**, and
this tree's standing answer to that is to model first: `tests/lptlink/linksim.py`
found three defects in the cable's link layer before any hardware ran, SPEC.md
§18.95's sector cache was simulated against a real 316-call trace before it was
built, and §19.2.3.1 is the negative result from the one time that was skipped.

So `tools/htmsim.py` is the parse, the §3.2.1 heuristic, the line table and the
cost model, in host Python, run against the four fixtures in `tests/htm/`. It
is the reference the assembly gets checked against, and `--render` prints what
a page would look like on a given adapter.

**It is calibrated against the machine rather than asserted.** `--selfcheck`
derives PERFORMANCE.md's *measured* 78-cell row from the two constants it uses
and compares:

```
selfcheck ok   78-cell row: model 71.3 ms vs measured 71.4 ms  (0.1%) herc
selfcheck ok   78-cell row: model 72.4 ms vs measured 72.7 ms  (0.5%) cga
```

If that check ever fails, the cost model has stopped describing the machine.

**What it confirmed:**

| | |
|---|---|
| the table heuristic, on real data | FrogFind: **1 real table, 2 collapsed to blocks, 2 dropped as decoration** — §3.2.1 predicted this and now it is counted |
| a full window | **1,245.7 ms on CGA**, against Part 2's independently measured 1.24 s |
| scrolling one line | **90.1 ms** — blit plus one row — so a full repaint is **13.8×** it |
| the bounded walk | **87–92%** of the demo and torture pages lie below the window, against Note Pad's measured 72% |

**And what it found that the design had wrong** — three bugs, all of which
would have been the same bugs in assembly, and none of which is visible by
reading the markup:

1. **An inline tag boundary inserted a space** (§2.2): `see below .`
2. **The table column squeeze split words**, rendering `Storage` as
   `Stora`/`ge`. A column's floor is the longest **word** in it, not one
   character.
3. **Only entity expansions were folded**, so a raw `0xF6` reached the glyph
   table (§2.2). Both fixtures now fold identically whatever their charset,
   which is the property that actually matters: `Köln` is `Koln` from
   Latin-1 and from UTF-8 alike.

#### 2.5.1 A row could remember how far it was last drawn

The model prices a line at **the full band**, because §2.4 requires the
padding to be the erase. Measured across the fixtures, the *ink* is only
**52–64%** of that — so a page of short lines pays about 40% for erasing
blank space.

Missile Command's `mc_srun` (SPEC.md §48.17) is the answer where it applies:
keep what each row was last **drawn** with and emit from the first differing
cell to the last. `--verbose` reports the gap.

**It is a floor and not an alternative, and the distinction is the whole
note**: a row a blit has just exposed holds undefined pixels, so on the
scrolling path — the one that matters — the band must be erased whatever the
new line says. What it can pay for is the *other* paths: a resize, a redraw in
place, a status line. Worth having on the list and **not** worth building
before §4 is measured on iron.

---

## 3. Layout

### 3.1 The units are cells, not pixels

v1 lays out in an 79/89-column character grid. The consequences are all good
and they compound:

- A table column is a **column count**, so measuring a table is integer
  arithmetic over lengths and never a text-measurement pass.
- A scroll is a whole number of 8-pixel rows, so `OSAPI_GFX_SCROLL`'s
  requirement that `x1` and `x2+1` be multiples of 8 is met for free.
- Every pen is 8-aligned, which is what earns `font_run`'s **single-store**
  path — on a 1bpp adapter a cell row becomes one store with no shift, no
  read, no second byte and no separate fill pass (SPEC.md §6.1). Measured, the
  aligned run is **1.24×** the skewed hand-written pair on both mono adapters,
  and SPEC.md §11.94 measured alignment at **9.4% on VGA and 12.5%** on the
  primitive.
- `WF_SNAP` is the **default** for every window now (SPEC.md §11.94), so the
  content origin is already aligned and the browser opts out of nothing.

### 3.2 Tables — the one genuinely new problem

**A table's column widths depend on all of its rows, so a table cannot be laid
out incrementally top-down.** That is the only place a browser's layout is
harder than Note Pad's, and it needs an answer rather than a hope.

**The answer is that a table is the atomic unit of layout.** When the lazy
walk first reaches a `TABLE` marker it does one measuring pass over that
table's cells in the *parsed document* — integer max-per-column over run
lengths, no drawing, no pixels — stores the resulting column widths in the
line table entry for the table, and from then on the table's rows lay out and
paint like any other lines.

So the bounded-walk property survives with one amendment: **the walk's
granularity is a line, except at a table, where it is the table.** A 40-row
table is measured whole even if one row is visible. That is acceptable because
the measure is arithmetic and the paint is not, and it is why the measure must
never touch a glyph.

Three rules keep it bounded:

- **Cap the columns.** Eight is generous for a readable table on 79 cells.
  Beyond it, degrade — render the cells as consecutive paragraphs — rather
  than refuse. SPEC.md §47 rule 3: say why, and do the useful thing.
- **Cap the cells.** A layout table wrapping a whole page is the common case
  in real HTML, and measuring 5,000 cells to draw one row is the failure this
  cap exists for. Past it, the table degrades the same way.
- **A table wider than the view scrolls horizontally**, by 8-pixel columns, on
  the same blit. It does not shrink columns to fit; a squeezed table of
  one-character columns is unreadable and costs a re-measure.

**Nested tables flatten.** The inner table becomes paragraphs. This is a
text-and-table browser and the honest limit is one level.

#### 3.2.1 Most tables are not tables, and the front page proves it

§1.1.2's capture has **five `<TABLE>` elements and one of them is a table** —
counted by `tools/htmsim.py` (§2.5) rather than by eye, which is how the first
version of this table came to say six:

| | what it is | rows × cols | verdict |
|---|---|---|---|
| the alert box | a navy panel round ~200 words | 1 × 1 | **a box** |
| the Ko-fi button, **nested inside it** | one short string | 1 × 1 | **a box** |
| two quota bars | a coloured strip, **no text at all** | 1 × 1 | **decoration** |
| the quota legend | `None` / `<1 Day` / `>1 Day` | 1 × 3 | **a table** |

That is how HTML of this era does boxes, buttons and progress bars, and
rendering them as tables gives a reader **four empty rectangles and a nest**.
So two tests, both answerable at the parse from counts the parser already
has:

- **A 1 × 1 table is not a table.** Drop the table and emit its contents as an
  ordinary block. This alone handles four of the five, and it composes with
  the flattening rule above so the nested pair collapses to nothing.
- **A table with no text in any cell is decoration.** Drop it, or emit a
  `HR` if it was doing the job of a rule. This is what stops the quota bars
  becoming empty boxes.

Two things follow that are worth having in mind. **The table renderer will be
exercised far less on real pages than on the demo site**, so §1.1.1's fixture
has to carry real tables deliberately or they will not be tested at all. And
**the heuristic is where a table gets lost**, so a page that renders wrongly
should be checked here first — a genuine one-column table of data is
indistinguishable from a box by this rule, which is an accepted loss and the
right one, because the loss is a border and the alternative is a screenful of
boxes.

### 3.3 Progressive layout, and what the user sees

Layout is driven by the view, so the natural loop is:

1. parse a chunk of source into the document,
2. lay out lines until the view is full,
3. **paint** — the user has text on screen,
4. carry on parsing and laying out in the worker, unbounded but interruptible,
5. correct the scroll bar's proportions as the total line count grows.

**Time to first line, not time to page.** The first screenful is one parse
chunk plus one window of rows — ~71 ms of drawing on CGA plus the parse — and
the rest of a long document need never be laid out at all if nobody scrolls
there.

The scroll bar is the one thing that wants the total up front and cannot have
it. Note Pad has exactly this problem and its answer applies unchanged
(SPEC.md §27.7.1): `[np_drows]` is **exact at a natural end and a monotone
lower bound at a bounded stop** — never lowered, so the error is always in the
direction that keeps the document reachable — and recounted by the worker when
typing stops. Here: the thumb is approximate while loading and exact when the
document is complete, and it never jumps backwards.

### 3.4 Fetching without freezing the machine

The fetch is the worker task (SPEC.md §20.6), for the reason Frotz's VM is
(SPEC.md §61.6): tens of seconds of transfer inside a key callback would hold
the gfx lock for all of them.

**And that forces a rule that will otherwise be discovered the hard way: a
worker may not call the file slots, the file dialog, or `OSAPI_MEM_*`.** So
**every buffer is claimed on the UI task, before the worker starts** — source,
document, line table, and the socket's own staging — exactly as Frotz's
`zi_load` claims the story, the Z-stack, the save buffer and the scrollback up
front. A browser that discovers mid-fetch that it needs a bigger buffer has to
come back to the UI task to ask.

The practical shape: claim a source buffer sized from `Content-Length` when
the server sends one (refusing with §47's arithmetic if it will not fit —
Frotz's *"Anchorhead needs 508K and this machine has 149K free"* is the model),
and a bounded default when it does not, treating overflow as a truncation the
status line reports rather than an error.

**Cancel has to work at every point**, and it is a flag the worker checks on
each `NETV_RECV` — which is non-blocking by contract
(docs/plans/completed/NET-STACK-PLAN.md §1.2), so there is never a call to interrupt.

### 3.5 Resize, reflow, and the adapter changing underneath

`OSAPI_WM_ONRESIZE` (SPEC.md §11.98) covers the content box moving with nobody
asking — which is the Control Panel's Display page switching adapters, and a
window dragged across an extended desktop's seam. The Calculator is the
reference consumer and its lesson applies: **derive the layout from the live
content box on every paint**, never from a constant.

A width change invalidates the **line table and nothing else**. The document
is untouched, the source is untouched, and no byte is re-parsed.

**Anchor on the source offset, not the line number.** Bank `LN_DOC` of the top
visible line before the reflow and find the line containing that offset
afterwards. A browser that returns to the top of the document when its window
is resized is one nobody uses twice, and the line *number* is meaningless
across a reflow.

---

## 4. Scrolling

### 4.1 Three tiers, and they are not new

Exactly SPEC.md §27.7.2's and §22.11's, which are already measured:

| the scroll | what is drawn | cost |
|---|---|---|
| `\|d\| <` visible rows | one `OSAPI_GFX_SCROLL`, then letter the **d** rows it exposed | blit + d × 71 ms |
| `\|d\| >=` visible rows (page, Home, End, a link jump) | the row band only — **not** the header, the status line or the scroll bar's chrome | one repaint of the band |
| a clip region cuts the band | full repaint | SPEC.md §11.3 — a blit cannot be cut per pixel, and `gfx_scroll` refuses rather than corrupting |

**`OSAPI_GFX_SCROLL` answers CF=1 when it refuses and nothing moved**, so the
fallback is a branch and not a hazard. The rows it vacated are the caller's to
repaint — that is the contract, and forgetting it leaves the previous
content's last rows on screen.

**One line therefore costs the blit plus ~71 ms.** The blit itself is the
number this plan does not have: PERFORMANCE.md measures `GFX_SCROLL 256x128`
at **48.2 ms**, and a full-width content area is two to three times wider, so
**~60–150 ms is the modelled range and `gfxbench` is what settles it.** That
puts one line at **130–220 ms** against a 1,240 ms repaint — inside §1's
budget, and the single biggest win available.

Two things that look like refinements and are not:

- **Round the blit's x span OUTWARD to byte columns.** Note Pad rounds inward
  and the Disk window rounds outward, for opposite and specific reasons
  (SPEC.md §22.11) — here the content starts at the window's own margin, so
  rounding inward would leave a sliver nothing ever repaints.
- **The blit's y span must stop at the last WHOLE row.** Note Pad's §27.7.2
  trap: the sliver below the last full row is one no row painter will ever
  draw, so nothing erases what the blit pushed into it — which showed as a
  permanent 1-pixel band of descenders **on Hercules and not at all on VGA**.

### 4.1.1 The tier test must read the DELTA, and for a while it did not

Shipped and fixed (SPEC.md §71.10). `br_flush` compared `[br_ptop]` — the old
scroll position — against `[br_rows]` instead of the delta, so the blit was
abandoned as soon as the reader was one windowful into a page and every scroll
after that repainted the band. Measured on a cycle-accurate 5150/CGA, one Down
key in a 15-row band: 1 `font_run` and 1 `gfx_scroll` for the first 15 presses,
15 `font_run`s and no blit from the 16th; 19 displayed frames of redraw against
5.

Three things in it generalise past this app:

- **A symptom that correlates with CONTENT can be a statement about POSITION.**
  It was reported as tables scrolling slowly, and the painter has no table path
  — §3.2's rows are composed into the document and letter exactly like prose.
  The table was simply what was on screen by the time the view was deep enough
  for the tier to flip.
- **The defect has no wrong output.** The tier it wrongly picked is the correct
  one and merely dearer, so every pixel test in the tree passed — including
  `tests/brtest`'s own blit-against-repaint check, which runs at `top = 0`,
  where the broken build blits too.
- **A register that held a value two instructions ago is not a variable.** The
  line was `mov bx, ax` and §5's scroll bar later put the thumb's old position
  in AX between the write and the read. It reads `[br_dy]` now.

`tests/brscroll` is the gate and it names the property rather than a budget:
one Down key costs the same at the top of a page as it does deep in it.

### 4.2 Input coalescing, and the defect this container cannot show

**This is the part most likely to be got wrong, because getting it wrong is
invisible here.**

Typematic repeat is roughly 10 keys a second. At 180 ms a line, ten lines is
1.8 seconds of work per second of holding the key — so the event queue grows
without bound, the browser draws scroll positions the user left behind seconds
ago, and the machine never catches up. **That is input overrun, and
PERFORMANCE.md names it as one of the three defects that cannot be observed in
this container at all** — the emulator is exact about how much work the guest
does and useless about how long it takes, so on a fast host the queue always
drains.

The fix is cheap and must be in from the start: **`OSAPI_EVQ_PENDING` reports
how many events are still queued.** While it is non-zero, keep taking scroll
keys and accumulating the delta; draw once at the end. A held arrow then costs
**one blit per batch** rather than one per repeat, and if the accumulated delta
exceeds the window it degenerates to §4.1's second tier — one band repaint,
which is the worst case and is bounded.

The same argument covers the mouse wheel if one is ever present, and the scroll
bar's drag, which SPEC.md §7.1.3 already requires to pace itself to the tick.

### 4.3 What is deliberately not done

**Pixel-smooth scrolling.** Sub-row scrolling means the blit is not a whole
number of character rows, every line straddles two cell rows, and `font_run`'s
single-store path is lost for the entire document. It would cost roughly the
1.24× alignment win and buy smoothness the machine cannot sustain anyway.

**A back-buffer.** There is none — SPEC.md §32 removed it, and the reasoning
applies here: it could only ever be armed on VGA, and the machine this is for
has a Hercules and a CGA in it.

---

## 5. The window

Ordinary, resizable, one instance per document, following the Disk window's
furniture because that is what a user of this OS already knows:

- **the row band** — the document,
- **a vertical scroll bar** on the right, and a horizontal one only when a
  wide table needs it,
- **a status line** — the URL while loading, then the title, then a link's
  target when the pointer is over one,
- **menus**: File (Open Location, Save As, Close), Edit (Copy — SPEC.md §55's
  system clipboard is text and this is a text browser, so it fits exactly),
  Go (Back, Forward, Home, Reload), View (Wrap, Tables on/off).

**Back is a stack of URLs, not of pages.** Keeping parsed documents costs the
heap and a re-fetch from a proxy or a LAN server is cheap; keeping the
*scroll position* per entry is what actually matters and is two words.

Built as **eight fixed slots of `BR_UBUF`** in bss rather than a packed arena:
the arena is smaller and needs compaction, and eight URLs is what a Back
button is for. Full, the **oldest** goes — a browser that stopped remembering
after eight pages would be remembering the wrong eight. Navigating from the
middle of the stack **drops everything after the cursor**, which is what makes
Forward mean anything.

**The bug worth naming is Back pushing itself.** `br_go` is the one place a
URL is accepted, so it is where the push belongs — and Back, Forward and
Reload all come through it. `[br_nopush]` is what separates *going somewhere*
from *going back*; without it the stack becomes a two-entry loop that Forward
can never leave, and it still looks correct for exactly one press.

The scroll position is **not** kept yet, deliberately: this is URL-only.

#### 5.1.2 A control is one WORD, and the window is the adapter's

Two more off the same 5150 session, once search worked.

**`< Submit >` came out as `< Submit` with the `>` alone on the next line.**
The button is ordinary document text — that is what makes it wrap, centre and
scroll with no painter of its own — and ordinary text breaks at spaces.
`D_NBSP` (0x7F) is the answer, and it is **text rather than a marker**: it
occupies a cell and counts toward the wrap width, it simply is not the `' '`
`br_layout` breaks at, and `br_build` renders it as a space. The button is one
word now, so the wrap moves it whole to the next line. The input field gets
the same treatment — a 30-cell field breaking at its own spaces would put half
a box on each line — which means the caret's *unfocused* character and
backspace's filler must be no-break too, or focus and editing punch wrap
opportunities into the middle of the field as the user types.

**The default window is derived from the adapter.** 150 rows was a number for
the smallest screen that every larger one then inherited, and a browser is
short of rows and nothing else. VGA and Hercules get 90% of the desktop band,
centred so the window still reads as one and can be grabbed by an edge; CGA
gets the whole band **and the dock's strip**, because 640x200 leaves the band
155 rows and this app spends 33 of them on chrome. That is the kernel's
existing `WF_KEEPH` (SPEC.md §11.93) rather than a fight with `wm_fit`, and
the dock stays reachable — a window over it is `wm_dock_under`'s ordinary case.

**One trap, and it cost a run.** `OSAPI_VIDEO` answers the screen height in
**BX** and `OSAPI_WM_KEEPH` takes the window in **BX**, so asking the adapter
first hands the kernel the number 200 as a window pointer. The symptom is a
window at the band's 155 rows — which is exactly what a `KEEPH` that never
happened looks like.

### 5.2 The toolbar strip

Back / Forward / Reload on the left and the **state** on the right, in one
thin row above the location bar. The state used to have a line of its own
*under* the bar, so moving it up costs the page three rows rather than
thirteen — and rows of band are what a browser on a 200-line screen is short
of.

They are `os88ui_btn`s (SPEC.md §20.5.1), which is what makes the greying
right rather than a fourth copy of it: `OS88UI_DIS` carries `[gfx_dis]` as
well as `CDGRAY`, so a dead Back is *dithered* on a Hercules instead of
pixel-identical to a live one (SPEC.md §47 rule 1). Each is greyed on a
**fact** — is there anywhere to go — and the same three predicates are what
the click refuses on, so the drawing and the refusal cannot disagree (rule 5).

Two things about the strip's geometry are load-bearing. The state's pen is
**floored to a multiple of 8** for §6.1's single-store path, for the reason
`os88line`'s is: this field is rewritten on every state change and the
fallback blanks it. And its width **shrinks** rather than the strip
overflowing — `font_run` clips to the SCREEN, not to the window, so a state
that ran under the Reload button would be drawn straight over it.

### 5.3 Save As writes the SERVER'S bytes

The point of it is to carry a real page off the machine, so it writes the
source rather than a re-render of the document stream — anything this browser
reconstructed would be a report about its own parser rather than evidence
about the page.

That cost one line: the source claim used to be freed the moment the parse
finished, on the reasoning that the stream is self-contained. It is kept until
the next navigation now, which does **not** move the peak — `br_free_all` runs
before `br_claim`, so a load still holds one page's worth at a time — only the
idle footprint.

**No default name is offered**, because the URL's last component is usually a
script path with a query on it and a mangled 8.3 default looks like a
considered suggestion.

**Links are the line table's job.** `LN_FLAGS` says a line has one, the run
carries an index into a link table, and a click resolves (line, column) →
run → href. Keyboard navigation — Tab to the next link — is the same walk and
is the only way the app is usable on the mouseless 5150 that SPEC.md §9.6
exists for.

### 5.1 …and this is how they were BUILT, which differs in one place

Everything above holds bar the run table, and what replaced it is worth
writing down because it made the feature cost almost nothing.

**The link markers are INLINE in the document, and their index rides in the
document too.** `D_LNK1` is followed by two bytes carrying the index, each in
**16..31** — and that range is the whole trick. Every walker in this format
already skips a byte below `0x20` as *a marker, not a cell*, so the payload
cost the layout, the table composer, the wrap measurer and the painter not one
line of change; and no real marker is above 15, so a **backward** scan can tell
a payload byte from a marker. That is what `br_linkat` is: from the clicked
offset, walk back to the first byte below 16 — `D_LNK1` means we are inside
that link, anything else means we are not. No run table, no per-line list, and
nothing to keep in step with the layout.

**One thing did have to change and it is the sharp edge.** `br_layout`'s
`.marker` path calls `br_emitline` *first* — every marker in this format ends
the pending line — and these two must not, or the sentence breaks at every
anchor. They are tested ahead of that call and resume the walk without
emitting.

**`LN_FLAGS` earns its keep exactly as planned**, for the one thing the
backward scan is bad at: a line that *begins* inside a link holds no `D_LNK1`
of its own, so `LNF_LNK` records the layout's running state at the line's
start. It is set in `br_emitline` — the one routine every line comes through —
rather than at each of the four places `br_lstart` moves.

**The hit test is the painter's own walk, run again.** `br_build` fills a
column→offset map beside the line buffer, so a click positions a row and a
column, rebuilds that one line and reads the map. Markers, table cells, centred
lines, an indent and a form field's spaces are all handled *because none of
them is handled there* — SPEC.md §22's `fm_hit` discipline one level down,
where the shared thing is a walk rather than a rect. It is what makes clicking
a form field and clicking a link the same three instructions apart.

**Underlining is one `gfx_fill` per RUN**, not per cell, on the cell's last
pixel row — blank in every ROM glyph but the descenders, which touch it rather
than being cut by it.

Resolution handles four shapes: absolute `http://`, `https://` **refused out
loud** (§8.3 — a link that silently did nothing reads as a broken browser),
`#anchor` (this page, so nothing happens), and root- or directory-relative
against the host the current page came from. A page opened from a **floppy**
has no host at all, and that is refused by name rather than composed into
`http:///…` and left for `br_split` to reject as a bad *address*.

`tests/brclick.py` is the gate, and it aims every click from the app's own
line table rather than from a screenshot: a click that lands one cell off is
the failure it exists to catch.

#### 5.1.1 Two defects a REAL page found that a written one could not

Both came off a 5150 fetching the live FrogFind, and both are about a
**relative** URL — which is the thing a fixture written by the parser's own
author tends not to have. The page is `tests/htm/frogfind-home.htm` now,
saved through File > Save As and served by `tests/brnav.py`.

**A form's action is a URL like any other.** FrogFind's is
`<form action="/">`, so `br_submit` composed `/?q=…` and handed it to `br_go`,
which rightly refused it as not-http: the search box could be typed into and
never submitted anywhere. It goes through the same `br_resolve` a link does
now, so an action and an href cannot come to disagree about what `/` means.
`tests/brclick.py` had this in front of it and printed
`composed query: '/search.php?q=hi&v=1'` — it asserted only that `q=hi`
appeared, so the relative URL was in the output and unexamined.

**`brnet.inc` has its OWN `br_claim`.** A file load and a fetch claim the same
buffers at different sizes — a file's size is known and a server's
`Content-Length` is a claim by a stranger — and the link arena was added to the
file path's copy alone. So `[br_lnkseg]` was 0 on every page off the wire and
`br_anchor` rendered every anchor as plain text: links worked perfectly from a
floppy and never over the network, which is the half that matters and the half
no local test could see. `br_lnkclaim` is one routine both call, so a fifth
buffer cannot be added to one path and not the other.

**Fullscreen is worth having and its key is contested.** SPEC.md §53's
same-mode bracket is what Paint takes (SPEC.md §42.7) and it buys two things
here: more rows visible per scroll, and **the gfx lock held across the whole
session** rather than an unlock/yield/lock round trip per event — PERFORMANCE.md
Set 4 priced that pair at 21.8% of a session. But SPEC.md §11.2.1's rule is
that `F` toggles fullscreen both ways *except in an app taking typed text*,
and this app has a URL box. So it follows Paint: bare `F` only when the URL
box does not have focus, `Ctrl+F` unconditionally — and the menu item names
`Ctrl+F`, because a key hint has to be true in every state.

---

## 6. Type

**v1 is the kernel's own fixed 8×8 cell.** §3.1 is the argument and it is
mostly about tables and alignment rather than about speed.

**v2 is proportional, and the interesting part is that it is competitive.**
SPEC.md §6.3–§6.5 already ship: `.F88` faces in `SYSTEM/FONTS/` on every system disk,
`apps/os88type.inc` as the method, and `OSAPI_GFX_BLIT1` to put a composed
1bpp band on screen in one call. Measured (PERFORMANCE.md Set 64): the emit is
**4.04 ms for a 624×12 band** and the compose is **21–43 ms** depending on how
the inner loop is written.

So a proportional line is **25–47 ms against a monospace row's 71 ms** — and
proportional type fits *more* words per line, so it is fewer lines as well.
**Proportional is plausibly faster than the cell font**, which is not the
answer anybody expects.

**And the right first conclusion from that is not "use proportional" — it is
that the fixed path has room in it.** A composed band writes each cell once
into RAM and emits the row in one call; `font_run` pays a per-cell cost 
inside the renderer. That is SPEC.md §5.7's finding in a new place, and **the
owner has taken it as separate work** — so this document does not assume it,
and every figure here is against the cell font as it stands today. If that
work lands, §1's repaint budgets improve and nothing in §2–§4 changes:
the whole design is about not drawing rows, and a cheaper row does not make a
wasted one worth drawing.

What stops it being v1 is not cost, it is that it takes the grid away:
column-width arithmetic for tables becomes text measurement, the line table's
`LN_X` stops being a cell, and a horizontal scroll stops being 8-aligned. Word
(SPEC.md §68) is the reference consumer and the place to learn it from.

Four traps come with `OSAPI_GFX_BLIT1` and every one is silent (SPEC.md §6.3):
it takes framebuffer **bytes**, not pixels; the band arrives in **final screen
polarity**, so white paper is `0xFF` and ink is ANDed in — OR it against
`0xFF` and every line comes out invisible; it is the one primitive that is
**greying-blind**, so SPEC.md §47's dither is the caller's to apply; and
`kern_small` does not carry the body at all, so a package must read CF and
fall back to the cell font.

**Headings, bold and italic** are the reason to want it. In v1 a heading is
the cell font with a rule under it and bold is a double-strike, which costs a
second pass over those cells and is worth it for `<b>` and not for `<h1>`.

---

## 7. Forms — the smallest thing that makes a search engine work

§1.1 puts one text input and one submit button on the critical path, and that
is a smaller feature than it sounds **provided it is scoped to what a search
box actually is**. It is also the first thing in this document that makes the
browser an *input* surface rather than a viewer, so it touches the keyboard
model, the focus model and the mouseless machine.

### 7.1 What is implemented, and what is refused

| | v1 | why |
|---|---|---|
| `<form method=get>` | **yes** | it is what a search box is |
| `<input type=text\|search>` | **yes** | |
| `<input type=submit>`, `<button>` | **yes** | |
| `<input type=hidden>` | **yes** | free — it is a name/value pair with no widget, and real forms carry them |
| `<form method=post>` | **the model carries it, the code does not** | §7.5 |
| checkbox, radio, select, textarea, file | **no** | each is a widget, a hit-test and a keyboard model of its own |
| `<input type=password>` | **no, and deliberately** | §8.2 — there is no TLS, so a password box would be a plaintext credential prompt. §1.1's sign-in lives in the proxy |

**Refusing a control must be visible, not silent.** SPEC.md §47 rule 3: an
unsupported input renders as an inert `[unsupported]` cell rather than
vanishing, because a form that silently loses a field submits the wrong query
and blames the server.

### 7.2 Controls are drawn on the character grid

No new primitive and no new geometry. On §3.1's cell grid:

```
   a text field   [Search os8088_______________]
   a button       < Search >        ...and inverted while it has focus
```

A field is a run of N cells drawn by the same opaque `font_run` as everything
else (§2.4), so it costs one call and never blanks. Focus is **an inversion**,
which is `gfx_xor_fill` and is its own inverse — so moving focus costs two
inversions and no re-lettering, exactly as SPEC.md §22.2's file selection and
§27.8.2's Note Pad selection do. **Do not re-letter a field to show focus**;
that is 30 cells at 909 µs to move a highlight, and Note Pad measured that
mistake at 4,049 glyph cells becoming 929.

The field is a **line-table entry** like anything else (§2.3), so it scrolls
with the document for free and a click resolves through the same
(line, column) lookup the link hit-test already does — SPEC.md §22's `fm_hit`
discipline, one owner for the geometry.

### 7.3 The keyboard, and the machine with no mouse

This is where forms actually cost something, because the browser's keys are
currently all navigation.

**Focus is modal and it is one word**: `[br_focus]` names the focused control
or 0 for "the document". With focus in a field, printable keys go to the
field and the arrows move the caret; with focus on the document they scroll.
**Tab moves between links and controls, Enter submits, Esc leaves the
field** — and Tab is not a nicety here: SPEC.md §9.6's mouseless 5150 has no
other way to reach a control at all, and that machine is the reason the key
map has to be complete rather than mouse-assisted.

Two collisions to settle now rather than discover:

- **Fullscreen.** SPEC.md §11.2.1 makes `F` the fullscreen key both ways
  *except in an app taking typed text*. This app takes typed text only while
  a field has focus, so it follows Paint (SPEC.md §42.7) exactly: bare `F`
  when `[br_focus]` is 0, `Ctrl+F` unconditionally, and the menu item names
  `Ctrl+F` because a key hint has to be true in every state.
- **The single-key scroll keys** (space for page-down is conventional) are
  unavailable while a field has focus, and that is correct rather than a bug.

**A caret is not free.** Note Pad's blinks on its worker; here the simpler
answer is a non-blinking inverted cell, because the browser's worker is busy
fetching and a blink is a timer, a lock hold and a redraw for decoration.

### 7.4 Submission is string building, and it is where the injection bugs live

On Enter or a click on the submit control: walk the form's fields in document
order, percent-encode each name and value, join with `&`, append to the
action after `?`, and issue the `GET`.

Three rules, and the first two are the ones that will bite:

- **Percent-encode against an allowlist, never a denylist.** Everything
  outside `A-Za-z0-9-_.~` becomes `%XX`, space becomes `+`. A denylist misses
  a byte, and the byte it misses ends up splitting a header.
- **A field's contents are hostile even though the user typed them.** A
  literal CR or LF reaching the request line is **HTTP request splitting** —
  the browser would send two requests and attribute the second's answer to the
  first. Refuse control bytes at the field, where the check is one compare,
  and not at the socket.
- **Resolve the action against the current URL**, so `action="/"`,
  `action="search.php"` and an absolute URL all work. That is the same
  relative-URL resolver links already need, which is why forms are cheap:
  **the only genuinely new code is the encoder and the widgets.**

### 7.5 `POST` is designed in and not built

The form model carries a method from the start and `br_submit` branches on it;
what v1 omits is the request builder's body path — `Content-Length`, a
`Content-Type` of `application/x-www-form-urlencoded`, and sending the encoded
string as a body rather than a query. **That is perhaps 200 bytes**, and the
encoder is shared, so `POST` is a small addition rather than a redesign.

It is left out because nothing on §1.1's list needs it: FrogFind is a search
box, and Reddit's sign-in is the proxy's problem. It goes in the day a form
that matters uses it.

### 7.6 Cost

Parser: four tags. Widgets: two, on the existing grid. Encoder, focus word,
key routing, relative-action resolution. **~1–1.5 KB of package** and no
kernel change at all — it adds nothing to §2's pipeline but a marker type and
nothing to §4's scrolling.

---

## 8. HTTP, HTTPS, and the proxy

### 8.1 What the browser speaks

**HTTP/1.0, `GET` only, one request per connection.** No keep-alive, no
chunked encoding, no compression, no cookies, no redirect chains beyond a
small bounded count. Every one of those is a real feature and every one costs
code on a machine that has none to spare; `Content-Length` and a close is the
whole transport.

`gzip` deserves its own sentence, because it is the one that looks tempting: a
20 KB page is often 5 KB compressed, which over the cable is **four seconds
saved**. Inflate is ~1.5 KB of code and a 32 KB window. **On the cable that
trade is probably worth it and on a card it certainly is not**, and it should
be decided with a measurement rather than in advance.

### 8.2 HTTPS is not reachable, and saying so early is the honest thing

A TLS handshake is public-key arithmetic. An RSA-2048 private operation on a
4.77 MHz 8088 with no hardware multiply worth the name is **minutes**, and the
symmetric layer then costs per byte for the life of the connection. There is
no version of this that is usable.

**And the modern web is essentially all HTTPS.** So the sites this browser can
reach directly are: a LAN server, a machine on the bench, a period service, and
the small set of plain-HTTP hosts still standing.

**A plain-HTTP page will still contain `https://` links, so refusing one is an
ordinary path and not an error.** §1.1.2's capture has two of them — the Ko-fi
button, twice. Clicking one must say so, in the status line, naming the reason:
SPEC.md §47 rule 3 again, the same say-why-not that greys `Format` on a network
volume. A link that silently does nothing reads as a broken browser, and a link
that tries and hangs is worse. If a proxy is configured, the honest offer is to
route it through that instead — which is §8.3, and it is the whole argument for
a proxy in one click.

### 8.3 …so the proxy is a first-class configuration

A proxy that terminates TLS and hands back plain HTTP solves the problem, and
it solves a second one at the same time: **a modern page is 200 KB of markup
that renders to two screens of text**, and a proxy that strips to text and
tables turns that into a few KB. On the cable that is the difference between
55 seconds and 2.

**And one of §1.1's targets is already a proxy**, which changes the order of
the work: FrogFind exists to reformat the web for vintage machines, so
whatever it can fetch on our behalf is reachable without us writing anything.
That makes it both the first real user of the browser *and* the cheapest
route to arbitrary pages — a plain-HTTP front door to content this machine
could never fetch itself. **It also makes it a dependency on somebody else's
service**, which is a fine place to start and a poor place to stay, and it is
why the two placements below are still worth building.

Two placements, and the first is nearly free:

- **On the DOS box, over the cable.** It is already in the room, already
  running mTCP, already running `OS88NET.COM`. A fetch-and-transcode there
  costs the 5150 nothing and is the natural home for the `gzip` and TLS this
  machine cannot do.
- **Anywhere on the LAN**, for the card. An ordinary HTTP proxy works
  unchanged if the browser sends absolute-form request lines, which is four
  extra bytes of code.

#### 8.3.1 The reformatter, and where a session lives

§1.1's second target — old.reddit.com, signed in — is the case that decides
what a proxy of ours is *for*. Three jobs, and only the first is about
bandwidth:

1. **Strip and reformat.** A Reddit listing is a great deal of markup around a
   little text. Reformatting it to the tags §2.2 already supports means the
   browser needs no new feature for it at all.
2. **Terminate TLS**, which §8.2 puts out of reach.
3. **Hold the session.** The proxy logs in once and keeps the cookie; the 5150
   sends an ordinary unauthenticated `GET` and gets a signed-in page back.

**That third job is what keeps the browser stateless, and it is a design
decision worth stating rather than a convenience.** A cookie jar in the
browser would mean storing a credential on a floppy that anyone can read, on
a machine with no TLS to protect it in transit — so the honest architecture is
that **os8088 never holds a credential**, and the proxy is single-user and
lives on the owner's own network.

The thing not to design out: the request builder should be able to carry an
arbitrary header, so a `Cookie:` line is a data change rather than a code
change if a multi-user proxy ever wants one. That costs nothing today.

**The browser must still parse HTML itself**, and the proxy must not become
load-bearing — because on the Ethernet path there may not be one, and because
a browser that only renders one server's dialect is not a browser. The proxy
is an accelerator and a TLS bridge, never the renderer.

---

## 9. Memory

The ladder this build produces puts the heap at `0x1aa0` = **106.5 KB**, so:

| machine | heap | verdict |
|---|---|---|
| 640 KB (both field 5150s) | **~533 KB** | comfortable — a 64 KB page and everything derived from it is a tenth of it |
| 256 KB | ~149 KB | fine for ordinary pages |
| 128 KB (the floor) | **~21.5 KB** | **the browser is a big-machine app** |

For a 20 KB page: source 20 KB, document ~12 KB (text minus tags, plus run
headers), line table ~2 KB, link table under 1 KB — **~35 KB**, plus the
package's own region.

**So it degrades by refusing, with the arithmetic in the refusal** — SPEC.md
§47 rule 3 and Frotz's §61.4 wording. A 128 KB machine gets a browser that
opens small pages and says exactly why it will not open a large one, which is
a great deal better than one that opens nothing or crashes at 90%.

**Every buffer is a claim and every claim can be refused** (SPEC.md §50).
Paint is the model for tiering: give up features one at a time — the
scrollback of visited pages first, then Back's history, then the link table —
and put up a notice only when the page itself will not fit.

---

## 10. Staging

| # | build | proves |
|---|---|---|
| **0** | **the fixtures and `tools/htmsim.py`** | §1.1.1 and §2.5. `demo.htm` is owed to the owner as a page anyway; `torture.htm`, `utf8.htm` and the FrogFind capture are what the renderer is checked against; the model is the reference implementation and the cost model. **Costs no guest code and it has already found three bugs** |
| **1** | **a renderer with no network at all** — open a local `.HTM` off the floppy through SPEC.md §54's association | **BUILT — §13.** `apps/browser/browser.asm`, 4,097 bytes, rendering `demo.htm` and `torture.htm` on a cycle-accurate 5150 with the period IBM ROM |
| **2** | the scroll tiers + coalescing, measured | §4, against `os88marty.py flicker` and a counter in `font_char` — the same instruments SPEC.md §22.11 and §40.2.1 were measured with |
| **3** | tables | §3.2, against step 0's pathological page as well as a real one |
| **4** | **forms** | §7. Still local: rendering, focus, Tab and the encoder are all checkable by *printing the URL a submit would fetch* instead of fetching it |
| **5** | `GET` over `NETV_*` | docs/plans/completed/NET-STACK-PLAN.md's stage D. By this point the browser already works, and **FrogFind is the first real user** — which is why forms come before the network rather than after |
| **6** | the reformatting proxy | §8.3.1, and old.reddit.com with it |

**Steps 0 and 1 are the recommendation to act on.** They are the majority of
the work, they carry all of the performance risk, they are fully testable in a
container today, and they are worth having on their own — a local HTML reader
is a real thing for a machine whose documentation ships on its floppies.

The ordering's one real claim is that **forms come before the network**. They
are the last thing that can be developed against a file, they are what the
first real site needs, and building them after the transport means debugging
an encoder and a socket at the same time.

---

## 11. What will break, in the order it is likely to

1. **Input overrun on a held arrow key.** §4.2. It is invisible in this
   container by construction, it is one of the three defects PERFORMANCE.md
   names as unobservable here, and it will present on the 5150 as "scrolling
   is broken" rather than as a queueing bug.
2. **A blank line where a clip edge crosses it.** SPEC.md §11.3's granularity
   rule. It follows from any fill-then-letter pair, so §2.4's single opaque
   `font_run` is not a style preference — it is the fix, applied before the
   bug.
3. **The 1-pixel band of descenders below the last whole row.** §4.1, and it
   showed on Hercules and not on VGA the first time, so a VGA-only check will
   pass.
4. **Re-parsing on a scroll or a resize.** §2's invalidation rule. It will not
   look like a bug; it will look like the browser being slow, and the cause is
   four call layers away.
5. **A table measured with glyphs instead of arithmetic.** §3.2. Correct
   output, and it turns a bounded walk into an unbounded one.
6. **A worker calling `OSAPI_MEM_*` or a file slot.** §3.4, SPEC.md §20.6.
   It will work on the first page and fail on the one that needs a bigger
   buffer.
7. **Hostile markup reaching a renderer.** §2.2. A 40-character "word" with no
   break, a heading nested 200 deep, a byte above 0x7E — every one refused at
   the parse. SPEC.md §18.2's stance on a FAT BPB is the standard: **every
   byte read off the wire is treated as hostile.**
8. **`ES` on entry to a callback.** It is `KERNEL_SEG`, and a `rep stosb`
   through it writes into the kernel (SPEC.md §56). A parser full of buffer
   fills is exactly where this happens.
9. **A CR or LF out of a form field reaching the request line.** §7.4. It is
   HTTP request splitting, the user typed it so it does not *look* hostile,
   and it will pass every test written with a well-behaved query in it.
10. **Focus swallowing the navigation keys** — or not swallowing them. §7.3.
    Both directions are real: a browser whose space bar stops paging the
    moment a search box exists, and one where typing `f` in a search box
    throws the window fullscreen.
11. **Honouring one of foreground and background colour.** §2.2.1 — it
    produces text that is invisible rather than wrong, on the two adapters
    that matter, and it has happened twice in this tree already. The safe
    state is dropping both.
12. **Rendering a layout table as a table.** §3.2.1 — five tables on the one
    real page checked so far and four of them are boxes. It does not look like
    a bug; it looks like the page being ugly.
13. **A parser tuned to a browser's saved copy.** §1.1.2. The wire bytes have
    no `<TBODY>` in them.
14. **A form field losing its value on a scroll.** §7.2 puts the widget in the
    line table so it scrolls with the document — but the *value* lives in the
    form model, not in the pixels, and a redraw that rebuilds the widget from
    the markup instead of from the model silently clears what was typed.

---

## 12. Questions for the owner

1. **Local HTML reader first, or wait for the network?** §9 recommends the
   first: all the risk, none of the dependencies, useful on its own.
2. **Is a proxy acceptable as the intended configuration?** §8.3 says the
   honest answer to HTTPS is yes. If it is not, the browser's reach is a LAN
   server and nothing else, and that is worth agreeing before the work rather
   than after.
3. **How much does `gzip` matter?** §8.1. It is ~1.5 KB and a 32 KB window,
   and it is worth roughly four seconds a page on the cable and nothing on a
   card.
4. **Proportional type in v2, or never?** §6 says it is plausibly *faster*
   than the cell font, which inverts the obvious assumption — though the first
   thing to do about that is the owner's separate work on the fixed path, not
   this.
5. ~~**Does FrogFind answer on plain HTTP without redirecting?**~~
   **Answered: yes**, on `.de`, and §1.1.2 has the evidence. What is still
   owed is the **form** and a **results page**, and the cheapest route to the
   first is `opensearch.xml` rather than a search — see `tests/htm/README.md`.
6. **What should the demo site contain?** §1.1.1 argues it should be built as
   the conformance fixture rather than as a nice page, and that the two are
   the same artifact. Worth agreeing before it is written, since a pretty page
   full of tags the browser does not support is a worse test than an ugly one.


---

## 13. What step 1 built, and what it found

`apps/browser/browser.asm` — **4,097 bytes**, 456 of bss, no kernel change of
any kind. `make browsertest` builds its disk; `python3 tests/brtest.py` drives
it on a cycle-accurate 5150.

**It renders.** Double-clicking `DEMO.HTM` in a Disk window launches it
through SPEC.md §54's association and the page appears: headings, centred
blocks, rules, wrapped prose, entities folded, `<title>` and `<script>`
dropped. `TORTURE.HTM` renders too, including the hard-split of a
90-character unbroken word.

### 13.1 The document is one linear byte stream, and the fold is why

The design decision worth keeping: **every character is folded to ASCII
0x20..0x7E, so any byte below 0x20 is free to be a marker.** A paragraph
break, a rule, a heading and a bullet are one byte each, text is itself, and
layout is a byte scan with no records and no pointer chasing. It also makes
the stream **self-contained**, which is why the source claim is freed the
moment the parse ends — nothing above the parse ever reads the file again.

The other thing that fell out: **layout is not on the hot path.** Note Pad
needs §27.7.1's bounded walk because it re-walks on every keystroke; a
browser lays out once per load and once per width change, so the whole
document is laid out at load and the walk needed none of that machinery.

### 13.2 Five bugs, and what each is an instance of

All five assembled cleanly under `-w+error` and four of them *ran*.

1. **`rep movsb` leaves CX at zero**, so `add [br_doclen], cx` after the copy
   added nothing and every flush rewrote offset 0. The document looked
   perfect in memory — it was the last 250 bytes of the page, at 0.
2. **`mov al, ' '` clobbers the low byte of AX**, and AX was the line index.
   Every row of a 16-row band drew line `(AH<<8)|0x20` — line 32, sixteen
   times. The screen showed one table cell repeated down the window.
3. **`br_act` used SI as its table cursor**, and SI is the parse's position in
   the source. `br_skipelem` then ran from a garbage offset and its advance
   was discarded, which put the contents of `<title>` on the page.
4. **The tag-name reader tested `cmp al, 'A'` before digits.** Digits sort
   below `'A'`, so `<h1>` read as `h`: no headings and **no paragraph breaks
   anywhere on the page**, with everything running together into one block.
5. **`shl bx, 10` on a 68 KB limit wrapped**, because 69,632 is not a 16-bit
   number. `BR_DOCMAX` caps the document claim at 63 KB, which every offset
   into it being a word makes the real bound anyway.

Numbers 1, 2 and 5 are the same family — **an instruction with a side effect
the author did not price** — and this tree names it repeatedly (SPEC.md §65's
`mul` writing DX, §45.15's `js` against `jg`). Number 4 is the one no reading
would have caught and no fixture would have flagged: the page rendered, it
just rendered as one paragraph.

### 13.3 FIXED: the seven stale pixels were the scroll bar's frame

**A colour change was clobbering a coordinate.** `br_sbar` held the bar's
`x1` in `AX` and then did `mov al, CBLACK` before framing it — a write to the
bottom half of the register the x1 was in. 520 became `0x0200` = **512**, so
the frame was drawn **eight pixels inside the text band**, and its left edge
was the vertical line at x512 that no arithmetic in §13.3's first draft could
account for. The seven pixels at x 513..519 were what the blit then dragged
around.

It is the same family as §13.2's first three — an instruction with a side
effect the author did not price — and it is the fourth of them in this file.
The rule it earns: **`mov al, <colour>` is never safe while a coordinate is
live**, so every pen change in this app now goes through `br_pen_black` /
`br_pen_white` / `br_pen_gray`, which save AX.

It was found from the front of the machine and not from the harness — a
report that the bar "redraws on top of the title bar" and that "there is a
line that draws after you scroll". **The instrument said seven pixels at one
coordinate for three rounds and never said which routine**, which is worth
remembering about pixel diffs: they localise a symptom exactly and a cause not
at all. `tests/brtest.py` now allows nothing here.

### 13.7 The scroll bar, and the window resizes

The bar was a plain frame with a white thumb — no arrow cells, no dithered
track, and one pixel of it inside the band. It is now drawn the way the Disk
window's is (SPEC.md §22): frame, an arrow cell at each end with its rule and
glyph, a 50% dithered track, and a framed white thumb, with `br_thumb` shared
between the painter and the hit-tester so the drawn control and the clickable
one cannot drift — §22's own rule. Clicking an arrow steps a line and the
track pages; dragging the thumb is not in yet.

**Matching the kernel's furniture is the point**, and not only for looks: when
the shared scroll-bar control arrives this is the shape it will have, so
adopting it should be a deletion rather than a redesign.

**The bar stops above the grow box.** `wm_grow_rect` is a 13x13 square at the
window's bottom-right corner (SPEC.md §11.1.1) and it lands squarely on the
bar's down-arrow cell — the two were drawing in the same space. The space is
reserved unconditionally rather than only when the box is showing: the box is
drawn on the *frontmost* sizable window alone, and a bar that changed length
as the window gained and lost focus would be worse than a gap.

**And the window is sizable** (`OSAPI_WM_SIZABLE`, SPEC.md §11.1) — it always
should have been and simply was not asked for. Nothing else had to change,
which is §3.5 working: `br_measure` re-reads the live content box on every
paint and a **width** change is the one thing that invalidates the line table.
Measured, dragging the grow box 120px left: **cols 60 → 45 and the layout
followed, 147 → 175 lines**, with the text reflowed and the bar moved to the
new edge. On Hercules, 60 → 47 and 147 → 168.

Verified on both adapters at **0 differing pixels** between a blitted arrival
and a repainted one — 128,000 on CGA and 250,560 on Hercules.

### 13.5 Tables, built

`br_table` is §3.2 as written: one measuring pass over the parsed cells —
integer arithmetic over lengths, not one glyph — then the rows. Measured on
`demo.htm`'s machine table: **3 columns, 18 cells, wanted widths 7/22/24,
word floors 7/11/9, 53 cells of band to share.** Cells wrap inside their
column and the row rules are drawn.

**A row draws from several cells at once, which the line table cannot
describe** — an entry is one contiguous span of the document. So table rows
are **composed into bytes appended past `D_END` in the document claim**, and
the line entries point at those. No new painter path, no third claim, and
`br_layout` rebuilds the arena with the rest of the layout on a width change.

Three more bugs, and two are one shape:

1. **`br_twrap` pushed and popped SI — which is its output**, the advanced
   cell cursor. So every call rewound the cell, no cell ever emptied, each
   reported that it had contributed, and the row composed lines until the
   64 KB arena was full: 1,047 display lines for a six-row table.
2. **`br_tcell1` counted its PADDING as a contribution**, so a row of empty
   cells looked non-empty — the same infinite compose from the other end.
3. `jcxz` is short-only on an 8086 and both uses were out of range.

**And the harness had the same fault twice, which is the lesson worth
keeping.** Its "did it render" check counted **lit** pixels — but the content
is *white*, so a blank window is made almost entirely of lit pixels and
sailed through. It counts **ink inside the content box** now. Before that, the
blit-versus-repaint comparison passed at *zero* differing pixels on a window
that was drawing nothing at all: **two identical nothings agree perfectly.**
Every assertion here has now failed once by being true of a broken build.

### 13.6 Forms, built

§7 as written, and small: **`/search.php?q=os8088&v=1`** is what a submit on
`demo.htm`'s search box composes — action, the text field, and the hidden pair,
in order — and `q=os+8088` when the value has a space in it. Verified by
reading the composed URL out of the guest, which is what §10 step 4 asks for:
with no transport yet the URL is **built and reported** rather than fetched,
so the encoder is checkable without a network.

**A field is a fixed span of the document.** The parse emits `[`, W spaces and
`]` as ordinary text and typing rewrites those bytes in place, so a field wraps
with the prose, scrolls with it, and **the painter needs no new path at all** —
the same trick §13.5's composed table rows use, from the other direction. A
keystroke repaints the one display line the field sits on (found by walking
the line table for the entry whose span holds the field's offset), because a
band repaint per keystroke is §0's 1.24 s.

Two decisions worth recording:

- **Focus is the caret being present, not an inversion.** §7.2 wanted an XOR,
  and an XOR overlay has to come off before every blit and go back after —
  SPEC.md §48.11's crosshair machinery — which is a lot for a v1 with one
  field on the page.
- **The arrows still scroll while a field has focus**, where §7.3 says they
  should move the caret. A deliberate v1 simplification, and the one part of
  §7.3's key map that is not honoured.

`<input type=password>` is still refused on §7.1's grounds, and an
`image`/`checkbox` input renders `[unsupported]` rather than vanishing —
§47 rule 3, so a form that quietly lost a field cannot submit the wrong query
without saying so.

### 13.4 What is deliberately not in it

Each is a named step and none is a surprise:

- ~~Tables~~ — **built, §13.5.**
- ~~Forms~~ — **built, §13.6.**
- **Links are drawn but not followable.** There is nothing to follow to yet.
- **The thumb does not drag.** The arrows and the track work; a drag is the
  one scroll-bar gesture missing, and it belongs to the shared control.
- **`<img alt>` is dropped**, because the parser reads no attributes at all.
  `tools/htmsim.py` *does* render `alt`, so **the model and the
  implementation disagree on exactly one thing** — worth stating rather than
  discovering, and the cheapest of the four to close.

## 14. What the FIELD found: issue #137's three

Three defects reported together off `files.bs0dd.net` — an Apache directory
index, which is a table of links with an `<address>` footer, and so carries
all three at once. `tests/htm/pubzone.htm` is that page's shape and
`tests/brtable.py` is the gate; the reporter's own capture is in the issue.

**None of the three is visible to `tools/htmsim.py`**, and that is the finding
under the findings. The model renders the reporter's page correctly, so
§13's "the model and the 8086 must agree on the text" would have passed it:
two of the three live *below* the text — in which document offsets a line
covers, and in what a click resolves to — and the third is in a routine the
model does not have at all, because htmsim never follows a link. A reference
implementation is a check on the parts it implements and silent on the rest,
which is worth knowing before the next one is trusted.

### 14.1 The doubled last line: `D_END` was tested one instruction too late

*"The browser doubles the last string on some pages."*

`br_layout`'s `.marker` path emits the pending line and **then** asked whether
the marker was `D_END`, jumping to `.fin` — which emits the pending line
again. Nothing between the two resets `[br_nch]`, so `br_emitline`'s
empty-line guard saw a real count both times and wrote two line-table entries
over the same span. The second is one byte longer (`.marker` advanced `SI`
past the marker first), which is why it drew as a *copy* rather than as a
blank.

**"Some pages" is exactly the pages whose last text is not followed by a block
tag this parser knows.** `address`, `body` and `html` are in none of
`br_tagtab`'s rows, so `</address></body></html>` emits no break and the
final run is still pending when `D_END` arrives. Every fixture in `tests/htm/`
ended in `</p></body></html>` — the `</p>` flushes, `[br_nch]` is 0, and the
second emit was suppressed by the empty-line guard. **The bug was one
`</p>` away from the suite for the whole of step 1.**

The fix is the two instructions moved above the emit, and it assembles to the
same bytes it replaced.

### 14.2 An anchor inside a table cell had no bracket left

*"There is no support for links in tables."*

There was support for the anchor everywhere except the one routine that
copies a cell, and the loss happened twice on the way:

- `br_tcell1`'s leading-space skip tested `al <= ' '`, so it walked over the
  `D_LNK1` that is a cell's **first byte** in the common case
  (`<td><a href=…>`) before `br_twrap` ever saw it;
- `br_twrap`'s emit pass dropped every byte below `0x20` outright.

So a composed row reached the arena as plain text. The painter then had no
`D_LNK1` to set `br_lnkcell` from and drew no underline, and `br_linkat` —
which answers a click by scanning **backward** — found no marker and reported
"not a link", so the click did nothing. Both symptoms, one cause.

**The fix is to carry the markers rather than to teach anything new.** The
composed arena is more document bytes in the same segment, so a `D_LNK1` in a
composed row is read by the painter and by the hit test exactly as one in the
prose is — neither gains a line. `D_LNK1`, `D_LNK0` and `D_LNK1`'s two payload
bytes are the whole of 14..31 (SPEC.md's `D_LNK1` comment is why), so **one
compare separates what must be carried from every other marker**, which is
still dropped. A carried marker costs no width: `DX` is the character count
and a marker never touches it, so the columns `br_tmeas` measured are the
columns that get drawn.

#### 14.2.1 The character budget had to move to the text byte

`br_twrap`'s emit loop exited on `dx >= cx` at the top. A link's `D_LNK0` sits
just **past** the last character it covers, so that exit left it behind — the
anchor stayed open across the cell wall and, because a row is one arena line
carrying every column, the backward scan from the *next* column walked into
it. That is a click on plain text going to the previous cell's link, which is
worse than the dead link it replaced. Testing the budget at the text byte
instead lets a trailing marker ride out and costs nothing else.

#### 14.2.2 A wrapped link is closed at the cell wall and re-opened

A cell wider than its column wraps, and its second fragment is on the next
composed row with the anchor still open. `br_tcell1` therefore **closes an
open link at the end of every fragment and re-opens it at the head of the
next**, so each composed row is balanced on its own and no backward scan can
cross a row. `br_tlnk` holds the state — one word per column, the **offset of
the `D_LNK1`**, not the decoded index, so re-opening is a three-byte copy out
of the document and needs neither the payload's encoding nor a second walk.

### 14.3 A `..` in a relative link was sent to the server

*"Problems with relative links."*

`br_resolve` composed the base directory with the href and sent the result:
`http://h/a/b/../c.htm`. **Removing dot segments is the client's job** (RFC
3986, section 5.2.4) — `../` is not a thing a server is asked for, it is a thing the
browser resolves before asking — and what a server does with one sent anyway
is the server's choice: Apache 404s, others answer a different page than the
one that was clicked. `br_undot` is that pass.

It runs on the **finished buffer** rather than in the three composers above
it, because `/a/../b` is legal in an absolute href and a root-relative one as
well as in the directory-relative case: one call at `.done` covers all three
and cannot be forgotten by a fourth. The rewrite is in place and forward-only
— a segment is only ever kept or dropped, so the write cursor is never ahead
of the read cursor — and it stops at `?` or `#`, because a query may hold
slashes and dots of its own and not one of them is a path segment.

Two deliberate answers where the RFC leaves room:

- **Excess `..` are discarded, not escaped.** `/a/../../x` is `/x`; the write
  cursor may not climb past the path's own first `/`.
- **An empty segment is kept.** `/a//b` stays `/a//b`, which is what RFC 3986
  says and is *not* what `urllib.parse.urljoin` does — it collapses the pair.
  A server may distinguish them, so the RFC's answer is the safe one, and the
  difference is written down here because the obvious host-side oracle
  disagrees.

### 14.4 The Reload button lost its right edge, and the button was innocent

*"The right side of the Reload button does not seem to fully draw, at least on
herc."*

**It is an OVER-DRAW, not a missing draw.** `br_toolbar` frames all three
buttons correctly; `br_status` then paints the state field over the last one's
right stroke. The field is one **opaque** `font_run` — one pass for ground and
glyph, §6.1's rule — and its text is right-aligned *inside* it, so it is
padded in FRONT with spaces. Those spaces are ground, and ground is white, so
they erased the frame every time the state was rewritten.

The pen came from `br_srect`, which right-aligns the field and then **floors**
it to a multiple of 8 for SPEC.md §6.1's single-store path. That floor is
right — the field is redrawn on every state change and an unaligned pen blanks
— but a floor moves the pen **left**, by up to 7px, and `BR_BTNG`'s gap is
3px. So the floor could step back over the gap and land on the button:
measured at the shipped window size, Reload's right edge is x=184, the first x
the state may use is 188, and the floored pen came back **184** — four pixels
inside the button, blanking 8 of the frame's 13 rows.

**The fix aligns the BOUND up instead of only the pen down.** A value at or
above an 8-aligned floor is still above it once floored, so the floor can no
longer cross. It costs the field one cell (43 → 42) and nothing else.

**It was not adapter-specific**, though it was reported on Hercules. `br_srect`
reads `br_r3`, `br_cx` and `br_cw` and nothing else — there is no adapter term
in it at all, and the window template is 496 wide everywhere — so the
arithmetic produced the identical 184 on CGA. `tests/brtoolbar.py` fails on
both 1bpp adapters before the fix with the same 8 unlit rows of 13, which is
the reason it is worth stating: "at least on herc" was a report about where it
was looked at, not about where it happens.

The gate's other two assertions guard the two cheap ways to make the first one
pass: Back and Fwd must keep unbroken right edges too (so a change that
stopped framing buttons fails), and the state field must still have ink in it
(so setting its width to zero fails).

### 14.5 Reload asked one question and answered another

*"After coming out of screensaver with the browser as the top window, its
redraw did not redraw the address bar text, and doing backspace left cursor
copies afterwards. Additionally, the reload button had been greyed out."*

Three faults, **one desync, and the saver is not in it.** The repro is two
clicks: open the browser and press Reload before fetching anything.

`br_okrel` greys Reload on whether the **bar** holds anything. The button's
action was `br_hgo` — *"Reload is Back to where we already are"* — which goes
to history entry `[br_histi]`, and that is a different question with no answer
until something has been fetched. So `br_hgo` copied a **zeroed history slot
over `br_ubuf`**, which is the location bar's own buffer (`br_loc + LN_BUF`),
and `br_go` refused the empty URL at `br_split` — before ever reaching the
`os88line_set` that would have told the control its text had changed.

The bar was then claiming `LN_LEN` characters that were no longer there, and
every symptom falls out of that one state:

- **no text** — `os88line_draw`'s opaque `font_run` stops at the NUL now
  sitting at offset 0;
- **Reload greys itself** — `br_okrel` reads that same byte;
- **caret copies** — the cells between the NUL and `LN_LEN` are painted by
  neither the run nor the strip fill that starts past it, so every caret drawn
  in them stays. Measured: three clicks along the bar leave **24 ink pixels,
  which is three carets of eight**.

**The saver is what made it visible, not what caused it.** The stale "http://"
pixels sat on the glass until something forced a full repaint; `ss_stop_x`'s
`wm_paint_all` is that. This is the shape SPEC.md §47 rule 5 exists to
prevent — one predicate for the greying and the refusal — and the comment at
the button's own hit test claimed to be obeying it.

The fix is that both of Reload's doors call one routine, `br_reload`, which
re-fetches **what the bar says** — which is what the menu item always did.
Two things came with it. The menu item never set `[br_nopush]`, so a Reload
from that door **pushed a duplicate history entry every time**; `br_reload`
sets it. And `br_hgo` now calls `os88line_set` immediately after overwriting
the bar's buffer, so *"the buffer and the length agree"* holds by construction
rather than by every caller remembering — `br_go` only resyncs on the far side
of `br_split`, which is exactly the gap this fell through.

`tests/brreload.py` is the gate: five assertions, all red before the fix, on
both 1bpp adapters. It drives the **click**, not the saver, and runs a saver
session afterwards only to prove the repaint comes back identical.
