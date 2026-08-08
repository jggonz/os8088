# Note Pad — the latency work: what landed, what is open, what was rejected

The counterpart to `docs/PAINT-NOTES.md`. Note Pad's *implemented* behaviour is
SPEC.md §27 and that is where it stays; this file is everything around it — the
designs thought through and deliberately not built, the numbers that are owed,
the field reports still open with what has already been ruled out for each, and
the apparatus that measures any of it.

Recorded so the next person reaches for the finished argument rather than
re-deriving it, and so a decision that was right in one round is re-opened for
the right reason in the next rather than for a hunch.

**Read §5 before picking up any of the open reports and §6 before measuring
anything.** Between them they carry four wrong diagnoses that each survived a
casual look, and three of the four were failures of the *harness* rather than
of the code.

| | |
|---|---|
| §1 | the row index — designed, not built, and the trigger list |
| §2 | open numbers: what has been measured, on what, and what is owed |
| §3 | deliberately rejected, with the reason |
| §4 | what shipped, and where each piece lives |
| §5 | open field reports, with the diagnosis so far |
| §6 | measuring Note Pad, and the four ways the apparatus lied |

---

## 1. The row index — random access to a row without walking to it

**Status: designed, not built. Build it when any of §1.3's triggers lands.**

### 1.1 What it is

A sparse table of `(row, character index)` recorded every *K* rows, so that
"where does row 400 start" is a lookup plus a walk of at most *K* rows instead
of a walk of 400.

**The data already exists and is already being computed.** SPEC.md §27.7.3's
chunked height count walks the whole note in order, `NP_HCHUNK` rows per
worker pass, and publishes exactly `(absolute row, index)` at every chunk
boundary — that is `[np_stoprow]`/`[np_stopi]`, which `np_hwalk` reads and
throws away after using it to resume. Recording them instead of discarding
them *is* the index. Nothing new has to be walked.

For README.TXT (781 rows) at `NP_HCHUNK` = 16 that is 49 entries, 196 bytes.

### 1.2 Keeping it bounded

A note of nothing but newlines is one row per character, so at `NP_MAXKB` = 16
the worst case is 16,384 rows — 1,024 entries, 4KB, which is too much to
reserve and awkward to claim.

**Decimate instead of growing.** Fix the table at *N* entries and a stride
*K*. When it fills, keep every second entry and double *K*. The table then
always spans the whole note, always costs *N* entries, and the walk from the
nearest checkpoint is at most *K* rows — which grows only logarithmically in
the note's length. `N` = 64 and `K` starting at `NP_HCHUNK` covers 16KB of
newlines with `K` = 256 at worst, and covers README exactly with `K` = 16.

This is the standard skip-table decimation and it composes with the chunked
count without changing it: the count already arrives in stride order, so a
decimation pass is "drop every other entry" and a stride double.

### 1.3 Why it is not worth building yet — and what changes that

**Measured, not assumed.** A page click costs the same at every depth:

| scroll depth | cycles | ms |
|---|---|---|
| 56 | 477,320 | 100 |
| 248 | 477,304 | 100 |
| 448 | 477,398 | 100 |

Identical to 0.02% (MartyPC, `os8088_5150_cga_gla`, README.TXT, 781 rows).

The reason is that **nothing in the scrolling UI can ask for a row you have
not already walked past**. The track pages by a windowful (`np_sbclick`'s
`.pageup`/`.pagedn` add or subtract `[np_vrows]`), the arrow cells step
`NP_SB_STEP` = 4, the thumb itself is inert — it falls through to `.yes` and
does nothing, there is no drag — there are no PgUp/PgDn scan codes anywhere in
the module, and `End` is `np_hmove` with DX = 0x7FFF, which is end of *line*.
So the new top row of any scroll was on screen a moment ago, `np_rows`
(SPEC.md §27.5) still holds its start index, and `np_scrollpaint` seeds the
walk from it.

**Walking to the middle is what builds the index.** That is the whole argument,
and it is why the table would be dead weight today.

It stops being dead weight the moment a *jump* exists. Build the index when
any of these lands:

- a proportional thumb drag, or a track click that goes to the click position
  rather than paging
- PgUp/PgDn, Ctrl+Home/Ctrl+End, or a "go to line" command
- anything that scrolls to a search hit outside the view

### 1.4 …and one caller wants it ALREADY

`np_redraw`'s caret-follow safety net is an **unbounded walk from index 0**,
and its own comment says so: when the bounded walk stopped short of the caret,
`[np_cury]` is meaningless, so the net re-walks "FROM INDEX 0 and UNBOUNDED,
which is the whole point of this net: the seed is what let the walk miss the
caret and the bound is what made it missable".

That is a genuine jump and it is reachable today — the comment names one way
(page the view away with the bar, then press a key) and Find reaching a match
far outside the view is another. On a 781-row note an unbounded walk is
seconds (SPEC.md §27.7.5 measured the same walk at 4,684 ms).

**The index serves it directly, in the inverse direction.** The net has the
caret's *index* and wants its *row*; the table is sorted by both, so a binary
search on index finds the nearest checkpoint below the caret and the walk
resumes there — at most *K* rows instead of the whole note. Same table, no
extra state.

This is the strongest single argument for building it, and it is why §1.3's
trigger list should be read as "any of these, **or** someone complaining that
Find is slow on a long file".

### 1.5 Traps, so they are not rediscovered

- **The stored row must be ABSOLUTE**, not visible. `[np_top]` moves under the
  table, and `[np_sdr]` is a visible row derived at seed time — SPEC.md
  §27.7.3 already makes that distinction for the chunk resume pair and the
  index inherits it.
- **The table describes ONE layout.** Anything that changes the wrap width
  invalidates every entry, exactly as it invalidates `np_rows` — so it is
  dropped by `np_hmark`, which already exists and already means "the layout
  moved, recount from the top".
- **An entry is only valid if the count actually reached it.** A partially
  built table covers a prefix of the note; a lookup past its high-water mark
  has to fall back to the walk it replaces. Do not let a half-built table
  answer for a row it never saw — that is docs/FIELD-NOTES.md 4's shape (an
  index resolved against a snapshot that had shifted).

---

## 2. Open numbers

- **`NP_HCHUNK` is 4, was 16, and the field run is what settles it properly.**
  16 was reasoned from ~2 ms per measure row (no framebuffer: `np_draw` and
  `np_sigup` are both clear), putting a chunk at ~32 ms and inside one 55 ms
  tick. `make npbench` says otherwise — on MartyPC, README.TXT loaded, **with
  the 16-row chunk**:

  | row | ticks | iters | µs/op |
  |---|---|---|---|
  | `hchunk` | 18 | 8 | 123,581 |
  | `measure_all` | 85 | 1 | 4,668,625 |
  | `hguess` | 18 | 5,667 | 174 |
  | `baseline` | 18 | 11,607 | 85 |

  So **a measure row is ~6 ms, not 2** (`measure_all` ÷ 781 rows = 5,978 µs;
  `hchunk` ÷ 17 rows = 7,270 µs, the difference being `np_hmark` and the walk
  setup amortised over far fewer rows), and one chunk holds the gfx lock for
  **124 ms — over two ticks**. A UI action waits behind that.

  So the count was taking 124/(124+165) = **43% of the machine** for as long as
  it ran, which is what the field reported as "eating the entire system's
  resources". At 4 rows it is ~25 ms and ~13%, and the wall time it costs
  instead is nearly free to give away: §27.7.4's estimate already puts the bar
  in the right place, so what the count adds is exactness and nothing needs
  that in a hurry.

  It was a 38x improvement on the 4.7 s freeze even at 16, so shipping it
  untuned was right; it is simply still untuned. **Pick the final value from
  the field number and not from this table**: `NP_HCHUNK` ≈
  (wanted hold in µs) ÷ (measured per-row µs), and a hold under one 55 ms tick
  is the target, which on these figures is 8 or below.

  The other half of the trade is settle time, and it pulls the other way: at
  16 a 781-row note is 49 chunks, and the worker sleeps `NP_WTICKS` = 3
  between them, so the thumb converges in ~8 s. Halving the chunk doubles
  that. **The lever to reach for first is the sleep, not the chunk** — a
  worker that slept 1 tick while `[np_hdirty]` is set would settle 3x faster
  at no cost to the hold, and the hold is the part a user can feel.

- **`measure_all` cross-validates the harness.** 4,668,625 µs against the
  4,684 ms SPEC.md §27.7.5 measured for the same walk through an entirely
  different method (framebuffer-watched wall time from the resize's white
  fill). Two methods, 0.3% apart.

- **`np_hguess` costs ~89 µs** (174 minus the 85 µs the timing loop spends on
  its own `call` and `OSAPI_GET_TICKS`). SPEC.md §27.7.4 claimed it was under
  2% of the `np_bounds` it rides on; against two kernel far calls at
  PERFORMANCE.md's ~756 µs that is ~6%, so the claim was optimistic by about
  3x and the conclusion — that it is not worth gating — is unchanged.

- **SPEC.md §27.7.5's resize numbers are emulator numbers.** 4,684 ms → 233 ms
  is a MartyPC measurement and PERFORMANCE.md Part 9 takes field figures only.
  The field number for that zoom is still owed.

---

## 3. Deliberately rejected

**Estimating the note's height ABOVE the fold** — guessing which character
index row *N* starts at, so a jump could go there immediately and be corrected
later. Rejected, and the asymmetry is the reason:

- *Below* the fold (SPEC.md §27.7.4) an error in `[np_drows]` changes the size
  of a **decoration**. The thumb is slightly wrong, the direction of the error
  is controlled (a row holds at most `[np_rcols]` cells, so `len/cols` can only
  be too small), and the walk corrects it upward.
- *Above* the fold an estimated start index changes **which characters are on
  screen**. Wrapping is a deterministic function of the entire prefix, so a
  guess cannot be right; the caret and selection indices key off exact row
  starts, so editing would land in the wrong place; and when the true walk
  finished, the text would shift under the reader.

The right answer to the same problem is §1's index, which is exact and whose
data is already being computed.

---

## 4. What shipped, and where each piece lives

One session's work, all of it on the `elendilon` branch. The problem it started
from: opening a 15,889-byte `README.TXT` on the 5150 loaded quickly, drew the
first screenful, and then **froze for seconds with nothing on the disk and
nothing on the glass**.

| SPEC.md | what it does | key symbols |
|---|---|---|
| §27.7.3 | the height count runs `NP_HCHUNK` rows per worker pass instead of walking the whole note in one gfx-lock hold | `np_hchunk`, `np_hwalk`, `np_hmark`, `np_hirechk`, `[np_stoprow]`/`[np_stopi]` |
| §27.7.4 | until the count lands, the note's LENGTH is the scroll bar's estimate | `np_hguess` |
| §27.7.5 | a resize walks to the bottom of the view, not to the end of the note | `np_paint`'s `[np_gchg]` block |
| §27.7.6 | only a scroll asking to go PAST the counted extent may finish the count | `np_sbclick`'s `.set` |
| §27.7.7 | the caret-follow net resumes forward instead of restarting at index 0 | `np_netseed` |
| §27.7.8 | a superseded scroll is not drawn at all | `[np_sowed]`, `np_worker` |
| §13.4 | `OSAPI_EVQ_PENDING` (slot **0x0338**): are more events queued behind me? | `evq_pending` |

Three things in there are worth knowing on their own terms:

- **`np_hmark` raises the height debt and forgets the resume pair as one act**,
  because they are one event. It stores only to memory, so it preserves the
  flags and drops in where each of its five callers had
  `mov byte [np_hdirty], 1`.
- **The worker had to EXIST**, and on the path that mattered it did not. The
  hire hung off `np_redraw`'s `.done`, `.fullpaint` falls past it, and a
  document opened by *double-clicking* it loads in the entry proc and is drawn
  by `W_PAINT` — which is not `np_redraw` at all. The note whose height most
  needed counting was the one note that never got a worker to count it.
  `np_hirechk` is the predicate in one place, called from both drawing ends.
- **`NP_HCHUNK` went 16 → 4** on the strength of §2's measurement: the 16-row
  chunk held the lock 124 ms and the worker sleeps 165 ms between passes, so
  the count was taking 43% of the machine for as long as it ran.

`tests/npbench.inc` + `make npbench` is the instrument for all of it — a
**bootable** 360KB disk (§6), Ctrl-B, report replaces the note.

---

## 5. Open field reports

All four are reproducible on the 5150 and none is fixed. What each entry is
for is the *ruled out* column: start from evidence.

### 5.1 Down on the bottom visible row — ~933 ms

The press that SCROLLS costs 933 ms; presses that only move the caret inside
the view cost 50 ms. On the most-used key in the editor.

**Ruled out.** It is *not* the caret-follow net: §27.7.7's `np_netseed` was
written for exactly this and the A/B says it changes nothing — 4,455,200
cycles against 4,455,272, five significant figures, with and without. The
reason is that the walk is bounded to `[np_vrows]`, which is one row *past*
the last visible, so it DOES find the caret and `[np_curseen]` is set.

It is also *not* a walk from index 0, and this is the useful measurement: the
cost **falls** with depth — 1000 ms at `top` = 0 against 267 ms at `top` = 262
— where anything walking from the top would grow. So it is in the redraw path,
and the likely shape is `np_redraw` choosing a full repaint where a band or
`np_scrollpaint`'s blit would do. **Start by finding which of `.band`,
`.scrolled` and `.fullpaint` a scrolling Down actually takes**, and why the
two depths take different ones.

### 5.2 Typing in the front or middle of a long note — "impossible"

Reported as huge latency, with the observation that §27.3's visual break is
correctly engaging, so it is not the drawing.

**Not yet measured, and that is the first job.** The apparatus could not see it
(§6.2). The standing hypothesis is the document being a **flat buffer**: an
insert at the front `rep movsb`s everything after the caret, which for 15KB is
roughly 30–55 ms depending on bytes or words — real, but not obviously
"impossible", so something else is probably in there too and measuring first is
what stops the wrong half being optimised.

**The fix, when the measurement justifies it, is a gap buffer**, and
ArtfulType already has one to copy (SPEC.md §46.9): a 20KB gap buffer in a heap
claim, with `at_getb` showing the cost of the indirection — one
`push es` / load / fetch / `pop es` per character read. Two runs with a gap at
the caret makes insertion O(1) and pays a move only when the caret jumps, and
then only for the distance jumped.

**What was considered and argued against**: a separate small staging buffer
(256 bytes, flushed when full). That is a gap buffer with an extra copy and a
second place that has to agree where the text is — the gap gives the same O(1)
typing with neither.

### 5.3 Bringing Note Pad to the front pauses ~half a second

**Not investigated.** The hypothesis to test first is that it is simply a full
content repaint: ~18 rows × ~29 cells at PERFORMANCE.md's ~1 ms a glyph cell is
~400–500 ms, which matches the report. §11.90 means a window that was NOT
covered costs only its title bar, so **the first thing to establish is whether
the window was actually obscured** — if an unobscured raise is repainting the
content, that is a bug in the cheap-path test rather than a cost.

### 5.4 `NP_HCHUNK` is set from an emulator number

4 is chosen from a MartyPC figure (§2). It sizes a gfx-lock hold and a duty
cycle, both of which the operator feels, and the field number is what should
set it. `make npbench` reports what it costs on the machine in front of you.

---

## 6. Measuring Note Pad, and the four ways the apparatus lied

Every one of these produced a confident wrong answer that looked right.
PERFORMANCE.md Part 4's rule — the apparatus is the thing most likely to be
wrong — earned its keep four times in one session.

### 6.1 The A/B must wait the same length of time on both sides

A resize was "blank on the merged build, fine on the baseline", and it was a
**regression that did not exist**: the merged run's sampling loop broke early
on a condition that was already true and screenshotted after 0.7 s, where the
baseline had slept 3 s. A resize triggers a walk that takes seconds. Re-run
with matched waits, the two were **109,826 lit pixels against 109,824** — two
pixels, and those were the cursor.

### 6.2 The screen probe watches ONE row

The cheap probe reads 40 bytes of one CGA scan line to detect "did the screen
change". It cannot see a keystroke echoed anywhere else, which is why §5.2 is
unmeasured: three keystrokes reported 0 ms, 0 ms and 1217 ms. Timing an edit
needs either a wider capture or in-guest instrumentation.

### 6.3 The state probe's offsets are derived from ONE binary

`npstate()` computes its field addresses from the size of `build/notepad.bin`.
Point it at a machine running **`npbench`** — a different build, a different
size — and every field reads plausible garbage: `len=59587`, `vrows=10546`.
Always confirm `np_len` against the file you actually opened before believing
anything else the probe says.

### 6.4 MartyPC here runs unthrottled, so wall time means nothing

The container runs the guest ~3.6× real time, so **work the guest does** is
exact and **how long it took** is not. Use the cycle counter for everything and
convert at 4.772727 MHz. `npbench` sidesteps this by timing in guest ticks from
inside the guest.

And one that is not the apparatus but cost a whole attempt: **a fix must be
measured against the symptom it claims to fix.** §27.7.7 is correct, targets a
real unbounded walk, and does nothing at all for the report it was written for
(§5.1) — which was only discovered by A/B'ing it. The first attempt at
§27.7.3 was lost the same way, to a diagnosis (`[np_lastrow]` clobbered at
`notepad.asm:1050`) that named a line inside a routine which never runs on
that path.
