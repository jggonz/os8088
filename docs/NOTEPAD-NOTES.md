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
anything.** Between them they carry eight wrong diagnoses that each survived a
casual look, and six of the eight were failures of the *harness* rather than
of the code — including one, §6.7, in the tool written to prevent exactly that
class of mistake, and one, §6.8, that reported a 25% win as no change at all.

| | |
|---|---|
| §1 | the row index — designed, not built, and the trigger list |
| §2 | open numbers: what has been measured, on what, and what is owed |
| §3 | deliberately rejected, with the reason |
| §4 | what shipped, and where each piece lives |
| §5 | open field reports, with the diagnosis so far |
| §6 | measuring Note Pad, and the eight ways the apparatus lied |
| §7 | the latency budget: one typematic repeat, and what is still over it |

---

## 1. The row index — BUILT (SPEC.md §27.13)

**Status: built, measured and shipped.** What follows is the design as it was
argued before it existed, kept because every claim in it was tested and two
were wrong in ways worth knowing:

- **§1.2's sizing was too frugal.** 64 entries decimates a 781-row note to a
  stride of 16, and one keystroke runs FOUR walks that each pay that stride —
  68 rows walked for one row of new text. `NP_XN` is 256.
- **§1.3's "a page click costs the same at every depth" is still true and
  still not the point.** The trigger that mattered was not a jump the UI
  offers; it was `Up` out of the top of the view, which §1.4 half-identified
  and filed under the *net* rather than under the four separate walks that
  each needed it.

SPEC.md §27.13 is the implementation; §7 below is what it did and did not
buy.

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

**And §5.1.1 is now a second caller, on an arrow key.** `Up` out of the top of
the view asks for a row above everything `np_rows` describes, so §27.7.9's
`np_seedtail` cannot help it and the net walks unbounded: measured at 5.2 s a
press. That is not a corner — it is the other half of the most-used pair of
keys in the editor, and unlike §1.4's case it happens without the user doing
anything unusual first. The index answers it in the same inverse direction:
binary-search the table for the checkpoint at or before the row wanted, and
walk forward at most *K* rows.

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
| §27.7.9 | a walk that cannot be seeded is still BOUNDED, and the deepest row the table holds is the seed when there is one | `np_seedtail`, `np_vmove`, `np_move` |
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

### 5.1 Down on the bottom visible row — FIXED (SPEC.md §27.7.9)

**It was not in the redraw path**, which is where this entry used to send the
next reader, and the correction is worth keeping because the reasoning that
pointed there was sound and wrong.

The walk is in the KEY HANDLER, two routines before `np_redraw` ever runs.
`np_vmove` seeds its walk with `np_seedck` and sets the bound *inside the
branch that seeded* — so when `np_seedck` refused, `[np_lastrow]` kept the
`0x7FFF` `np_walk` resets it to and the walk laid out the whole note. And
`np_seedck` refuses on exactly the press that scrolls: it asks for the row
before the caret's, and a caret on the last visible row asks for a row past
what `np_rows` describes.

Measured on MartyPC's cycle-accurate 8088 (README.TXT, 781 rows, 16-row
view): **4,866 ms, of which 4,664 ms is that one walk**. Three consecutive
presses agreed to 0.02%. Fixed, a scrolling `Down` is **250–407 ms against
4,802–5,088 — 12–19x** — and a `Down` that does not scroll is unchanged to
within 1%, which is the control that says the band path was not disturbed.

The two things this entry had wrong, and how:

- **"It is in the redraw path."** Inferred from the cost falling with depth.
  It does not: re-measured across eleven presses at `top` = 0..10 it is flat
  at ~4.8 s, because an unbounded walk from index 0 does not care where the
  view is.
- **"Ruled out: not a walk from index 0."** It is exactly that. What made the
  old A/B honest and the conclusion wrong is that §27.7.7 fixed a *different*
  unbounded walk, on a path a scrolling `Down` does not take — so the A/B
  really did move nothing, and the inference from it went one step too far.
  §6's coda is the general form.

The trap in the apparatus that hid it for one more round is in §6.5: a
breakpoint trace stamps the time between two hits against the SECOND, so a
routine that is not itself traced has its cost billed to whatever is traced
after it. `np_move` wore `np_vmove`'s 4,664 ms until `np_move` was given a
breakpoint of its own.

### 5.1.1 Up out of the top of the view — 5.2 s → 380 ms (SPEC.md §27.13)

The row index was built for this and it is what fixed it. Four separate walks
per press each needed a row above the view — `np_vmove`'s, `np_move`'s, the
caret-follow net's and `np_scrollpaint`'s — and every one of them fell back to
laying out the note from index 0.

Still **3.8x over budget** at 380 ms; §7 is the accounting.

What §27.7.9 did reach here was the correctness half. Before it, `Up` was
**14.9–20.4 s** and the view *flew to the end of the note* — `[np_top]` going
5 → 769 → 3 → 767 → 1 → 765 on consecutive presses of a key that means "up
one line", confirmed on the glass as well as in the numbers. It is now
5.15–5.22 s and the view steps 5 → 4 → 3 → 2 → 1 → 0. An `Up` with nothing to
do — caret already at the top of the note — went **4,956 ms to 145 ms**.

### 5.2 Typing in the front of a long note — MEASURED, and half fixed (SPEC.md §27.12)

Measured at last, with `tools/notepad`. A keystroke at index 0 of README.TXT
(15,428 characters) was **414 ms**, and it is three things:

| | before | after |
|---|---|---|
| the buffer move | 220 ms | **59 ms** |
| `np_walk` pass 1 — the layout | 114 ms | 114 ms |
| `np_walk` pass 2 — the drawing | 73 ms | 73 ms |
| **a keystroke at the front** | **414 ms** | **257 ms** |

**The standing hypothesis was right about where and wrong about why**, and
the correction is the useful part. This entry used to estimate the flat
buffer at "roughly 30–55 ms for 15KB" — that is what `rep movsb` would have
cost, and the move was not `rep movsb`. All three of Note Pad's moves were
open-coded byte loops at **68 clocks a byte** against a string move's ~17. So
the buffer really was the single largest cost of typing, and the fix was one
instruction in three places rather than a new data structure.

**And a gap buffer is now the wrong answer, on this measurement.** It would
take the remaining 59 ms to nothing and make the other 187 ms worse: the
layout walk reads every character of every row, and ArtfulType's `at_getb`
(§46.9) is a `push es`/load/fetch/`pop es` on each of those reads. What is
left of a keystroke is the WALK. §1's row index and SPEC.md §27.7.9 are the
structure to change; the document is not.

Still open here: **the middle of a note is not the front and has not been
measured.** The move is shorter (only the bytes after the caret) but the
layout walk is seeded differently, and §27.3's visual break is in play. Drive
it with `tools/notepad/lab.py trace` after scrolling in, and read §6.6 before
believing the first number.

### 5.3 Bringing Note Pad to the front pauses ~half a second — MEASURED, and it is not a bug

**The hypothesis was right and the window manager is exonerated.** Both halves
were tested rather than one:

- **Obscured raise** (Note Pad almost entirely behind a Disk window, clicked
  on its title bar): **1,026 ms** — 447 ms of kernel-side compositing before
  the package is called at all, then **578 ms inside `np_paint`**.
- **Unobscured raise** (already frontmost, clicked again): **not one package
  callback**, twice in a row. §11.90's cheap path does exactly what it says.

So the pause is a legitimate full repaint of a window that really was covered,
and there is no cheap-path test to fix. What is left is the **glyph floor**:
`np_paint` fills the content and letters 16 rows of 29 cells, and 464 cells at
PERFORMANCE.md's ~1 ms a cell is ~464 ms of the 578. The layout walk is the
rest.

That makes it the one open report here with **no structural fix behind it** —
§1's row index does not help, because a full repaint has to visit every
visible row whatever it costs to find them. Drawing fewer glyphs is the only
lever, and the tree already has the tool for it: `np_rflush` skips a row whose
signature is unchanged (§27.2), which a repaint deliberately cannot use
because the content was just filled over. Anyone picking this up should start
by asking whether a raise must fill first — not by looking at the walk.

Measured with `python3 tools/notepad/lab.py click 80 30`, which terminates on
the package's own dispatcher `retf` (SPEC.md §20.2, offset 14). Without that
terminator the same click reads **445 ms**, because the trace ends at the last
label inside the callback and §6.5's rule bills `np_paint`'s own 578 ms to
nobody. The first number this measurement produced was that 445.

**SPEC.md §11.96's raise cache is the answer to this and it now actually
runs.** It shipped not working: §50.6.1 placed a purgeable claim under a
ceiling computed by reading any claim owned by an instance *slot* as a region,
and a Disk window's 3KB listing cache is exactly that — a data claim, placed
bottom-up, at the floor of the heap. So on the one desktop this report is
about, the ceiling was 5KB above the heap floor with the FAT window already
filling it, every claim was refused, and the cache was never taken. Verified
on the machine, before and after the fix: `[wm_su_seg]` 0 always → `0x9A40`,
and a raise of a covered Note Pad now runs `wm_su_try` and **no `np_paint` at
all**. The 578 ms is gone rather than reduced.

Two things about that are worth carrying forward. **A refusal was the correct
behaviour of every piece involved** — `mem_claim` refuses, `wm_su_take` treats
a refusal as "no cache, raise the ordinary way", and the raise then repaints
exactly as it did before the feature existed — so the feature being 100% dead
looked identical to the feature being absent, on every screen and in every
timing. And **the arithmetic was visible in the claim table the whole time**;
what stopped anyone reading it is §6.7 below.

### 5.2.1 A short run of ArrowDowns leaves 66 glyph cells stale

**Found while pixel-verifying §27.13, and it PREDATES all of this work** — the
A/B is what says so, and it is the reason this is a report rather than a
regression. Open README.TXT, press Down 24 times, then force a full repaint by
covering the window and raising it: **705 content pixels change**, about
eleven glyph cells. Every later state in the same session is pixel-identical.

Both builds show it, in the same state (`top` = 9, `cur` = 482), at the same
705 pixels — the pre-index build and the post-index one alike. The index build
is strictly better either side of it: the baseline diverges again on the next
check (3,422 pixels) where the index build is 0.

What is special about the first sequence is that §27.7.3's background height
count is still running through it, so the suspicion is an interleaving between
the worker's chunk and the redraw's signatures rather than anything about
seeding. **Ruled out:** it is not the row index (it predates it), and it is
not the caret or a toast (an idle screen sampled twice is 0 differing pixels,
so the instrument is not manufacturing it).

`tools/notepad` + the crop-to-content diff in this entry reproduces it in one
run; that harness is the thing to start from.

**It is bigger than eleven cells and it needs no scroll at all.** The
reproduction now is three commands from a cold boot and it is deterministic to
the byte:

```sh
python3 tools/notepad/lab.py boot
python3 tools/notepad/lab.py press ArrowDown 6      # top stays 0 throughout
python3 tools/notepad/pixcheck.py
```

**2,743 bytes = 686 pixels = 66 glyph cells**, bounding box x 56..299 (the
full content width) and y 51..121 — nine rows of the sixteen, and the caret
never left the view. Byte-for-byte identical on two different builds of the
module, which is what says it is the code under both and not either change.

Three things that narrows: the view never scrolled, so `np_scrollpaint`,
`[np_ptop]` and §27.7.2's blit are all out; six `ArrowDown`s is fewer than the
24 in the paragraph above, so it is not a long sequence accumulating; and it
is the full width of nine rows rather than the tail of one, so it is whole
rows not being drawn rather than a run stopping short. §27.7.3's chunked
height count is still the standing suspicion — `drows` reads 532 of 781 while
this is happening — and the next step is to hold the worker off (breakpoint
`np_hchunk` and never resume it) and see whether the mismatch survives.

**The instrument had to be repaired before any of this meant anything** — see
the note at the top of `pixcheck.py`. Its "forced full repaint" stopped being
forced the moment SPEC.md §50.6.1's fix brought §11.96's raise cache to life,
because a raise then restores the banked pixels instead of calling `W_PAINT`
and the check compares a copy against its own original. It clears `WF_SAVEU`
for the round trip now, and it puts a breakpoint on `np_paint` and reports
INVALID rather than a comforting zero if the repaint did not happen.

### 5.3.1 `[np_rowsn]` is not capped to the array it indexes

Found while building §27.7.9 and **left alone deliberately** — it is latent,
not live, and the change that would fix it is bigger than the bug.

`np_rows` is `NP_MAXROWS` = 60 words. `[np_rowsn]` says how many of them are
good, and three of its four write sites clamp it; the fourth — the walk's
natural end, `.sigpad` — stores `[np_row] + 1` with only a sign test. A
781-row note walked to its end therefore leaves `[np_rowsn]` = 771 against 60
slots, and `np_seedrow` bounds its index against `[np_rowsn]` alone:

```
    cmp ax, [np_rowsn]
    jae .out
    shl ax, 1
    mov bx, ax
    mov ax, [bx+np_rows]        ; ...up to 1,540 bytes past the array
```

**Nothing reaches it today** because every caller passes a *visible* row and
`np_bounds` caps `[np_vrows]` at `NP_MAXROWS`, so the index is always < 60
whatever `[np_rowsn]` claims. `np_seedtail` is the first caller that takes its
row *from* `[np_rowsn]`, which is why it carries its own `NP_MAXROWS` clamp
rather than trusting the word.

The honest fix is at `.sigpad`, and it is not free: capping there would make
`[np_rowsn]` mean "rows in the table" everywhere, which is what
`np_seedrow`'s test wants — but §27.7.3's chunked count and `np_shiftrows`
read the same word, and the natural-end path is where `[np_drows]` gets its
truth. Establish what each reader wants the word to MEAN before changing it;
do not just add a clamp and rebuild.

### 5.4 `NP_HCHUNK` is set from an emulator number

4 is chosen from a MartyPC figure (§2). It sizes a gfx-lock hold and a duty
cycle, both of which the operator feels, and the field number is what should
set it. `make npbench` reports what it costs on the machine in front of you.

### 5.5 Typing at the END of a note gets slower as the page fills — REPRODUCED

Reported from the field, in the reporter's own words: *"with the cursor at the
end of the file it should only be drawing one character and doing no massive
operations on the rest of the text behind it. Currently it gets slower as I
fill the page."*

**It reproduces, it is real, and it plateaus.** Empty the note (`Ctrl-A`,
`Backspace`), then type, with the caret never leaving the end — a 16×29 window
on a cycle-accurate 4.77MHz 8088:

| note length | rows | `[np_top]` | keystroke | pass 1 | drawing |
|---|---|---|---|---|---|
| 26 | 1 | 0 | **35 ms** | 10 | 21 |
| 151 | 5 | 0 | 38 | 12 | 21 |
| 351 | 12 | 0 | 56 | 13 | 40 |
| 601 | 16 | 9 | **66 ms** | 15 | 45 |
| 776 | 16 | 17 | 50 | 11 | 35 |
| — the keystroke that scrolls — | | | **190 ms** | 18 | 168 |

So it roughly **doubles as the page fills** and then flattens: it is bounded
by a *screenful*, not by the note. That is why it reads as "as I fill the
page" and stops there. On a bigger window it will be worse in proportion — a
25×76 window is 3.5× the cells.

**What one keystroke actually does**, counted by breakpoint at a full page
(`len` = 700, `top` = 13):

```
np_walk=2   np_rflush=6   np_rstart=6   gfx_fill=17     (and 38 + gfx_scroll on the scroll)
```

Two walks is by design — pass 1 finds which signatures moved, pass 2 draws.
The **six** `np_rflush` is three rows per pass, and that is the answer to "it
should only be drawing one character": `np_seedck` deliberately backs the seed
up a row, because §27.11's word wrap decides a row's break from the length of
the word *behind* it, so the row above the caret's has to be laid out again
too. Both passes pay it. **17 `gfx_fill`s is 12.8 ms of pure arrival** at
PERFORMANCE.md's ~756 µs a call, before a glyph is drawn.

**Ruled out.** Not the seed failing: `[np_ckok]` and `[np_rowsok]` read 1 at
every depth and the trace shows `np_seedck` then `np_seedrow` on every press,
so nothing walks from index 0. Not §27.13's row index being dropped by
`np_hmark` — that was the first hypothesis, it is wrong, and the trace is what
killed it. Not the note's total length: at a fixed `[np_top]` the cost does not
move as the note grows behind the view.

**Where to go next**, in the order the numbers justify: the 17 fills per
keystroke (what are they, and can the margins be folded into the run the way
§27.2 folded the band?); the 190 ms scroll keystroke, which is §7.2's
`np_scrollpaint` on the one press that also inserts; and only then pass 1,
which is already down to 11–18 ms here.

**One trap it sprang, and it is §6-shaped**: counting glyphs by breakpointing
`font_char` and `font_run` reports **zero**, on a keystroke that plainly
letters a row. `OSAPI_FONT_RUN` lands on **`font_run_x`**, which is the
implementation and not a stub — it takes SPEC.md §6.1's aligned single-store
path itself and only falls through to the slow one. Both symbols resolve, so
the zero looks like a measurement rather than a miss. Count `font_run_x`.

#### 5.5.1 …and the drawing was never the problem

The whole keystroke, as an ordered breakpoint trace — one printable typed at
the end of a 701-character note, `[np_top]` = 14, 16×29 window, **52.3 ms**.
A leg is billed to the label at its END (§6.5):

| | | |
|---|---|---|
| `np_ins` | 4.6 ms | the insert itself |
| pass 1's walk | 9.8 | 2 `np_rflush`, `[np_draw]` = 0, so this is pure layout |
| the two margin fills | 1.8 | `gfx_fill` ×2 |
| pass 2's walk | 15.2 | **32 `np_carets`** — one per character laid out |
| **the glyphs** | **1.2** | one `font_run_x`, `[np_flo]` = 6 `[np_fhi]` = 7 — **two cells** |
| the caret bar | 2.3 | one `gfx_vline` |
| ~~the grow box~~ | ~~**~12**~~ | **FIXED — SPEC.md §27.2.1**, it was erasing nothing |
| `np_sbcheck`, `np_toast` | ~1 | |

**`np_rflush` already draws only what changed.** It diffs the row buffer
against `[np_prow]` cell by cell, folds in the caret and the selection, and
runs exactly `[np_flo]..[np_fhi]`. Two cells for one typed character, in one
call, in 1.2 ms. Nobody needs to fix the drawing.

**~25 ms of the 52 is laying the caret's row out TWICE** — 32 characters per
pass at ~0.3 ms each — to discover that one character changed. That is the
real cost, and §27.4's checkpoint and §27.11.1's seed test have already taken
it from "the whole note" down to "one row": the row is the floor of the
current design, not of the problem.

**~12 ms was the grow box, redrawn for nothing — FIXED (SPEC.md §27.2.1).**
The test was a *row* overlap and `np_bounds` reserves that corner in **both**
dimensions: the text and both margin fills stop two pixels short of its left
edge, and the scroll bar stops one row above its top. Nothing on the band path
could reach it. Keystroke **37–40 ms → 25.1 ms**, `gfx_fill` 21.2 → 3.0, and
the corner byte-identical over 740 keystrokes.

**What the problem actually requires**, for the commonest keystroke there is —
a printable appended at the end of the note, no wrap:

- the row's cell count is known (`[np_rcols]`, and the row buffer holds it)
- "does this character still fit" is one compare; "does the current word still
  fit" is one backward scan to the nearest space, which §27.11.1's `np_ckword`
  already does forward
- nothing above the caret's row can have changed, and nothing below it exists
- so: one glyph at column C, one caret bar, and the row buffer and signature
  updated in place — **no walk at all**

That is ~9 ms against today's 52, and it is a new fast path rather than a
tightening of the old one: every optimisation so far has made the *walk*
shorter, and this one is about not walking. The wrap question is the only
thing standing in front of it, and it is answerable from the row buffer.

---

## 6. Measuring Note Pad, and the eight ways the apparatus lied

Every one of these produced a confident wrong answer that looked right.
PERFORMANCE.md Part 4's rule — the apparatus is the thing most likely to be
wrong — has now earned its keep eight times, and §6.7 is the sharpest: it is
the same defect, in the tool built to stop it, one section over.

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

### 6.5 A breakpoint trace bills a leg to the routine at its END

The instrument that finally found §5.1 is a breakpoint trace: put an `exec`
breakpoint on every branch of interest, inject the key, and stamp each stop
with the guest cycle count. It is exact and it costs the guest nothing —
**and the delta printed beside a name is the time BEFORE that name, not
inside it**. A routine with no breakpoint of its own is invisible, and its
cost is billed to whichever traced label happens to follow it.

That produced a confident wrong answer within minutes: with `np_measure`
traced and `np_move` not, the 4,664 ms showed up as a gap between two
`np_measure` hits and read as "np_move is slow". With `np_move` traced too,
the same 4,664 ms moved to the leg *before* it — `np_vmove`'s own walk. One
fix was written against the wrong routine before the second breakpoint was
added.

**So trace the entries either side of anything you are about to blame**, and
treat a large delta as "somewhere in the gap" until a breakpoint splits it.

Three smaller things about the same harness, all of which produced a wrong
number first:

- **`run` does not resume from a breakpoint.** MartyPC's `ExecutionControl`
  stays latched in `BreakpointHit` through a Run and advances nothing,
  reporting success and zero cycles — `debug_server.rs` says so in a comment
  and the harness hit it anyway. Use `step(1)` then `advance(cycles=...)`.
- **The budget passed to `advance` is a silent truncation.** A budget shorter
  than the leg being measured ends the trace early and it reads as "the path
  stopped there". One unbounded walk is 22 million cycles; the first budget
  was 4 million.
- **A warmup of N keypresses does not put two builds in the same state** when
  the builds differ in how long a keypress takes: the same 20 Downs left the
  fixed build at `top` = 5 and the original at `top` = 3, because `advance`
  waited a fixed number of FRAMES and the slow build had presses still in
  flight. Read the state back and compare the runs where they agree — here
  presses 1..14 matched to 0.3%, which is what licensed comparing 15..19.

### 6.6 An injected key is QUEUED, not delivered — and not always dropped

`os88marty.py key` presses and releases in one call with **no guest time
between them**, and the emulator's keyboard queue then needs the guest to
actually run before int 09h sees anything. From a machine sitting in
MartyPC's latched `BreakpointHit` state — which is where every `lab.py`
command leaves it — a bare `key` followed by `advance(frames=...)` delivered
**nothing**, and the same `advance` after a `step(1)` delivered it.

**The failure mode is worse than losing the keystroke: it is kept.** Measured
directly — three characters typed with the bad recipe all arrived at once
during the *fourth* press's advance, so the run that inherits them reports a
keystroke nobody asked for and a length that does not match the edits. That
is a wrong number in the direction of looking plausible.

`tools/notepad/drive.py` has `tap()` and `chord()` for this; use them rather
than `m.key`. A modified keystroke needs guest time around *each* event as
well — press, key, release with nothing running in between puts four
scancodes into the controller in one instant, and an XT delivers one per
IRQ1, so Ctrl+Z did nothing at all until each event got its own advance.

`Tracer.collect` never showed any of this because it already does `step(1)`
before every `advance`, for the unrelated reason in §6.5's second bullet.

### 6.7 `os88sym.py` answered `KERNEL_SEG:offset` for three other segments

The tool that exists so nobody reads a `.bss` symbol out of a NASM listing had
the same defect one section over, and it cost this round a session. `linear()`
added `KERNEL_SEG * 16` to **every** symbol — right for `.text` and `.bss`,
wrong for `.cold` (COLD_SEG), `.ovl` (FAT_SEG) and `.lowbss` (LOW_SEG), which
between them hold 25KB of resident code and every task stack, disk buffer and
claim record.

The wrong answer is the bad kind: a small plausible address *inside the kernel
image*, which reads back without error. `mem_tab` resolved into `.text`, so
the claim map came out as 32 rows of noise — `0x0efe, 44,642 KB, owner
0x5859` — and a heap with 505KB free read as "581,598 KB claimed of 550, zero
free", which is a diagnosis of the wrong thing entirely. `mem_tab` is at
`0x15200`; the tool said `0x01400`.

Fixed: the map is cut with `[map all]`, symbols are attributed to their
section, and the four segment bases are read from the map's own equates so a
rung that moves cannot desync them. `os88sym.py --all` prints the section and
`seg:off` now. **A section not in `SECTION_SEG` is an error rather than a
guess at `KERNEL_SEG`** — guessing is what made this invisible.

Confirm a suspicious address against `SS` (which is `LOW_SEG` for every task)
before believing anything read through it.

### 6.8 A bench that types one character manufactures its own worst case

The apparatus for SPEC.md §27.11.1 typed `KeyA` for every measured keystroke,
so each five-press round grew the unbroken run at the caret. By the time it
measured, the caret sat inside a **14-character word** — which is precisely the
case the optimisation under test has to refuse. It reported **no change at
all**, twice, on a build that is worth 25–30%.

The tell was in the counters and not the milliseconds: `np_ckword=1.0` beside
`np_ckword.yes=0.0` says the test ran and answered no every time, which no
timing could have said. Reading the note out of guest memory then showed
`'aaaaaaaaaaaaaa'` between the row start and the caret.

**A text benchmark has to type text.** The fill loop already put a space every
eighth character and the *measured* presses did not, which is the same class of
mistake as §6.1's mismatched waits: the two halves of the run were not the same
experiment. One counter across every press, filled and measured alike, fixed
it — 53.5 ms → 37–40 ms, immediately.

And one that is not the apparatus but cost a whole attempt: **a fix must be
measured against the symptom it claims to fix.** §27.7.7 is correct, targets a
real unbounded walk, and does nothing at all for the report it was written for
(§5.1) — which was only discovered by A/B'ing it. The first attempt at
§27.7.3 was lost the same way, to a diagnosis (`[np_lastrow]` clobbered at
`notepad.asm:1050`) that named a line inside a routine which never runs on
that path.

---

## 7. The latency budget

**The target is one typematic repeat — ~100 ms (SPEC.md §2951).** It is the
right target and not an arbitrary one: below it, a held key cannot outrun the
editor, so nothing the user does by leaning on a key can build a backlog. It
applies to every user-triggered interaction *except* opening a document, which
is disk-bound.

Measured on MartyPC's cycle-accurate 4.77MHz 8088, README.TXT (15,428
characters, 781 rows) in a 16-row × 29-column window — the smallest realistic
case, and a wider window is worse:

| interaction | at the start of this round | now | budget |
|---|---|---|---|
| `Up` that scrolls | 5,150 ms | **380 ms** | 100 |
| `Down` that scrolls | 4,802–5,088 ms | **250–410 ms** | 100 |
| `Down`/`Up` inside the view | 104–252 ms | **104–252 ms** | 100 |
| typing at the front | 414 ms | **257 ms** | 100 |
| raise of an obscured window | 1,026 ms | 1,026 ms | 100 |
| `Up`/`Down` with nothing to do | 4,956 ms | **145 ms** | 100 |

**Nothing is at budget yet.** Two costs remain and neither is a seeding
problem any more — §27.13 removed the last of those.

### 7.1 Pass 1 walks the view to find out what changed — ~155 ms

`np_redraw`'s first pass lays out from the caret's row to the bottom of the
view to discover which rows' signatures moved. That is ~6 ms a row (§2), so it
is `(vrows − caret row) × 6` and hits ~96 ms with the caret at the top of a
16-row view — the whole budget, before anything is drawn.

**For a CARET MOVE it is provably unnecessary.** No character moved, so the
only rows whose signatures can differ are the one the caret left and the one
it arrived on; `np_ask` folds the caret into a row's signature and nothing
else about those rows changed. `np_move` already knows both rows —
`[np_ckpr]` is the new one — so the walk could be bounded to `max(old, new)`
instead of `[np_vrows]`.

Not attempted here, and the reason is worth stating: pass 1 also sets
`[np_rowsn]`/`[np_rowsok]` from where it stopped, so a shorter walk shrinks
what `np_rows` describes and pushes more traffic onto §27.13's index. That is
probably fine — the index exists now — but it is a change to the invariant two
other sections rest on, and it wants its own round and its own A/B rather
than being bolted onto this one.

### 7.2 A one-row scroll draws ~186 ms

`np_scrollpaint` after §27.13's band bound is: one `OSAPI_GFX_SCROLL` over the
content, a strip fill, a band fill, ~4 rows of layout from the checkpoint, one
row of text (29 cells ≈ 29 ms at PERFORMANCE.md's ~1 ms a cell), two XOR
selection bands and `np_sbar`.

The lettering is near its floor. The blit is not obviously so — it moves the
whole content rect to shift it one row — and `np_sbar` redraws the whole
scroll bar for a thumb that moved a pixel. Those two are where the next
measurement should go, and `tools/notepad/lab.py trace` will attribute them
once they have breakpoints of their own (§6.5).

### 7.3 The raise is the glyph floor and needs a different idea

§5.3: 1,026 ms, of which 578 ms is `np_paint` lettering 464 cells that were
genuinely overwritten while the window was covered. No walk is involved any
more — §27.13 seeds it — so there is nothing left to make cheaper except
drawing fewer glyphs, and every one of them is a glyph the user needs to see.
Getting a raise under 100 ms means not repainting at all: a save-under, or a
window that keeps its own back buffer. Both cost RAM this machine does not
obviously have (§50), and both are a window-manager change rather than a Note
Pad one.

### 7.4 What the budget would need

Honest arithmetic, on the 16-row window and the ~1 ms glyph cell:

- a keystroke that changes one row: 1 row of layout (6 ms) + 1 row of glyphs
  (29 ms) ≈ **35 ms**. Reachable, and §7.1 is what stands in the way.
- a one-row scroll: the blit + 1 row of glyphs ≈ **50–60 ms** if the blit and
  the bar come down. Reachable.
- a raise: **464 ms of glyphs, or a repaint that does not happen.** Not
  reachable by making the existing path faster.

So two of the three are in range and the third needs a different mechanism.
