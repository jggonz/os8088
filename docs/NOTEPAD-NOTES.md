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
anything.** Between them they carry ten wrong diagnoses that each survived a
casual look, and eight of the ten were failures of the *harness* rather than
of the code — including one, §6.7, in the tool written to prevent exactly that
class of mistake; one, §6.8, that reported a 25% win as no change at all; one,
§6.9, that invented a defect out of nothing and kept it on the books for three
rounds; and one, §6.10, that condemned a correct change on evidence the
*unchanged* build produced just as loudly.

| | |
|---|---|
| §1 | the row index — designed, not built, and the trigger list |
| §2 | open numbers: what has been measured, on what, and what is owed |
| §3 | deliberately rejected, with the reason |
| §4 | what shipped, and where each piece lives |
| §5 | open field reports, with the diagnosis so far |
| §6 | measuring Note Pad, and the ten ways the apparatus lied |
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

**The middle of a note has now been measured, and it was a different bug —
FIXED (SPEC.md §27.4.2).** See §5.6.

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

### 5.2.1 The 66 stale cells did not exist — RESOLVED, it was the harness

**`tools/notepad/pixcheck.py` was decoding the framebuffer wrongly, and had
been since it was written.** The debug server sends the rendered framebuffer
as **hex**; the tool called `base64.b64decode` on it. Every hex character is
also a valid base64 character, so the decode never failed — it silently
returned **4.5 bytes per pixel of nonsense instead of 3**, deterministically.

Deterministic is why it survived: identical screens still decoded identically,
so "0 differing bytes" was honest and every A/B that compared two builds was
honest. What was never meaningful was any *magnitude* — the byte counts, the
bounding boxes, the "66 glyph cells", the per-row attribution, the "50%
dither" this entry described. All of it was structure in the garbage.

With `m.fbuf()` — the one decoder in the tree, and what `shot --rendered`
already used — the check reads **0 differing bytes: incremental == full
repaint**. The defect this entry has described for three rounds is not there.

**What IS there is 227 pixels on row 0, at columns 168..299** — exactly the
rectangle of the `Loaded README.TXT` toast — in a capture taken shortly after
opening and compared against a repaint taken later. `[np_msg]` reads **0**
throughout, and the strip does not change over **720 frames of guest time with
no input at all**, so the pixels outlive the state that names them.

That is one of two things and this round did not settle which: a toast that is
*meant* to stay until the user does something (in which case the residue is
the measurement comparing "toast up" with "toast gone", and there is no defect
at all), or a retired toast whose pixels nothing erases. `np_onkey`'s
`.edited` clearing `[np_msg]` suggests the first. Either way it is 34 cells in
one strip, not a redraw-path defect, and nothing else in the content differs.

**SETTLED, and it was the second — see §8.** `np_toast` cleared `[np_msg]`
*as it drew*, so the toast was on the glass with nothing left to put it back
and the repaint legitimately did not reproduce it. The toast is the kernel's
now (SPEC.md §60) and is in the menu bar rather than in anybody's content;
this check reads **0**.

**The keys were innocent and so was the height count**, both established
before the decode bug was found and both still true: 6 `ArrowDown`s from a
repaired screen give **0**, and stopping `np_hchunk` outright changes nothing.

**And the trap that stopped this check being able to FAIL is fixed rather than
merely known — `pixcheck.py --self-test` is how you check it stayed fixed.**
The decoder above made every magnitude meaningless; this one made the
comparison itself a tautology, and the two are independent. SPEC.md §11.96's
raise cache can serve the "forced full repaint" *without calling `W_PAINT` at
all*, so the reference state becomes a byte copy of the capture it is compared
against and agrees with itself on any build. The dependency is gone at both
ends:
SPEC.md §11.96.2 makes `wm_su_ck` test `WF_SAVEU`, so clearing the flag
defeats a cache that has *already* been banked and not only one that has not
been taken yet; and the self-test proves the two properties the check rests on
rather than assuming them — leg 1 leaves the cache armed and requires the run
to come back `INVALID` (so a tautological pass is *reachable and caught*), leg
2 arms the cache and clears the flag afterwards and requires `np_paint` to run
anyway. Verified by breaking it: with `wm_su_ck`'s `jz` NOPped in the running
guest, leg 2 fails with exit 5 and names the missing kernel test.

A number from this file is worth what its control is worth. That is the whole
of what SPEC.md §11.96.2 cost to learn: the raise cache shipped a window
drawing a **solid black rectangle** past its own right edge, and the check that
was supposed to see it reported success, because it counted `W_PAINT` calls
and a raise that draws garbage makes exactly as many as one that does not.

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

**BUILT — SPEC.md §27.14, and it measured 8.8 ms.** The two things that were
not obvious from here: the row signature is a rolling `rol 1, add` with the
caret folded in at its own position, so with the caret at the end of the row it
is **invertible in four operations** and `np_sig` can be patched rather than
walked (without that the fast path wins once and pays for it on the next
keystroke); and the reconcile needed **no new mechanism at all** — `[np_sowed]`
already means "this window owes a `np_redraw`" and the worker already spends it
behind the `NP_IDLE` gate, so the deferred wrap rides the same settle as
§27.7.3's height count.

### 5.6 Typing in the middle of a long unbroken run — FIXED (SPEC.md §27.4.2)

Reported from the field as *"typing into the middle of a long string is
extremely slow"*, with the reporter's own diagnosis in the note they sent:
**"typing at the end of a run is not [fine] so we're looking back
somewhere."** That was right, and it named the routine.

The note is the case, so it is worth stating exactly: **709 bytes with no
newlines in it at all** — 249 `a`, a `;`, a sentence of prose, then ~400 more
`a`. At `np_rcols` = 29 that first run is one word spanning nine rows.

`np_seedck` backs up while a row begins mid-word (§27.4), because §27.11's
word wrap decides the break in FRONT of a row by the length of the word
behind it. Inside one 249-character word **every** row begins mid-word, so the
back-up ran from the caret's row to row 0 and seeded the walk at index 0 — and
pass 1 then laid out the whole view from the top of the note, twice per
keystroke. Measured with breakpoints: **nine** `np_seedck.back` iterations and
17 row transitions in pass 1.

The licence to stop was already in `np_wordfit`: its `.p2l` answers *"longer
than a row: the cell rule owns it"* and returns CF = 0 — **no break decision
is made in front of such a word at all**, so there is nothing behind the row
to redecide. `np_cellrun` is that test and §27.4.2 is the rule, including why
the margin is `np_rcols + 2` and not `+ 1`.

Measured on a cycle-accurate 4.77MHz 8088 (MartyPC, `os8088_5150_cga`, the
period 10/27/82 BIOS), caret just after the `;` on row 8 of a 16-row view,
keystrokes paced as a held key:

| | before | after |
|---|---|---|
| first keystroke (enters the break) | 306 ms | 117 ms |
| **steady, held key** | **325 ms** | **23 ms** |
| caret deep inside the run (index 150) | — | 190 ms then **16 ms** |
| at the front of the note, for comparison | 15.5 ms | 15.5 ms |

**The break was never engaging, and that is the whole 325 ms.** With the seed
at index 0 the caret sat at row 8 and pass 1 reported `dr0 = 8, dr1 = 9` — two
dirty rows, 29 cells, under `NP_BRK_CELLS` = 60 — so `np_brktry` refused and
every keystroke paid a full reflow. Correctly seeded, the break engages on the
second keystroke and *stays up*, which is what the reporter saw at the front
of the note and not in the middle: *"it displaces and then is fast."*

**No regression on ordinary prose, by construction rather than by timing.**
The rule needs `np_rcols + 2` consecutive non-space characters; README.TXT's
longest unbroken run is **28**, so at 29 columns `np_cellrun` can never accept
there and the path is identical. Confirmed anyway: `ArrowDown` pass 1
10.0–19.7 ms and totals 75–127 ms, against §7's documented 17–27 and 103–160.

Verified for pixels, not just for speed, because a bad seed draws the wrong
text and no timing can see it: `pixcheck` against a forced full repaint reads
**0 differing bytes of 93,696** on the reported note and on README.TXT, and
0 on an adversarial note of words 28/29/30/31/32/33/40/60/90 characters long
at four carets including backspaces inside threshold-length words. Read §6.10
first — three of those checks reported large mismatches on *both* builds
before the apparatus was fixed.

Cost: **+43 bytes** of `notepad.bin`. No kernel file changed.

**Reproducing it needs a note with no newlines, and `make npbench` builds one
now.** README.TXT cannot show this and no disk in the tree could, which is
why the report sat unmeasured for a round. `build/nprun360.img` (and the
1.44MB `nprun.img`) carries **RUN.TXT** — 709 bytes, no newlines at all, one
run of 249 characters, a `;`, a sentence of prose, then a long tail. It
carries no README.TXT on purpose, so RUN.TXT sits at the same row ordinal and
`drive.open_readme`'s fixed coordinates drive both disks.

```sh
make npbench
python3 tools/notepad/lab.py --len 709 boot --image build/nprun360.img
```

**Two carets, and they answer different halves — take the first one.**

- **Inside the run** (`Down`×5, `Right`×5, then hold a key): 439 ms to enter,
  972 ms on the second keystroke as the break takes hold, then **17–18 ms**
  steady with `bmode = 1`. This is the whole report in one place — the seed,
  the break engaging, and the break *staying* engaged.
- **Just after the `;`** (`Down`×8, `Right`×18): **110–156 ms**, down from the
  field note's 325, but the break does *not* engage here and on the field's
  own note it did. That is not a difference in the fix but in the prose after
  the semicolon: word wrap absorbs a one-character shift inside the row when
  the following words still fit, so only the caret's row is dirty (`dr1 = 8`),
  29 cells, under `NP_BRK_CELLS` = 60. Whether the break engages at that caret
  is a property of the sentence, not of the seeding.

---

## 6. Measuring Note Pad, and the ten ways the apparatus lied

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

### 6.9 A decode that cannot fail is worse than one that can

`pixcheck.py` read the rendered framebuffer with `base64.b64decode`. The
payload is **hex**. Because every hex character is a legal base64 character
the decode raised nothing — it returned 4.5 bytes per pixel instead of 3, and
kept doing so for three rounds.

**It was deterministic, and that is what made it dangerous.** Identical
screens decoded identically, so every "0 differing bytes" was true and every
two-build A/B was true; the tool was right about *whether* two screens differ
and wrong about *everything else*. That is a much harder failure to notice
than one that throws, because the results it gives are internally consistent —
it produced stable byte counts, stable bounding boxes, a reproducible cell
count and even a plausible-looking "dither", all of it structure in noise.
docs/NOTEPAD-NOTES.md §5.2.1 was that noise, and it survived because nobody
asked the payload what format it was in.

The tell was available the whole time and cost one call to find:
`len(data) / (w*h)` is **3.000** decoded as hex and **4.500** decoded as
base64, and 4.5 bytes per pixel is not a format. **Use `m.fbuf()`** — the
tree has one decoder and `shot --rendered` was already using it.

And one that is not the apparatus but cost a whole attempt: **a fix must be
measured against the symptom it claims to fix.** §27.7.7 is correct, targets a
real unbounded walk, and does nothing at all for the report it was written for
(§5.1) — which was only discovered by A/B'ing it. The first attempt at
§27.7.3 was lost the same way, to a diagnosis (`[np_lastrow]` clobbered at
`notepad.asm:1050`) that named a line inside a routine which never runs on
that path.

### 6.10 `[np_bmode]` going to 0 is not the screen being finished

A settle that polls the flag and then captures gets a **half-drawn screen**.
`np_reconcile` clears `[np_bmode]` near its top and then fills a band and
letters it, so between the flag moving and the last glyph landing there are
several rows of content that are neither the break's fiction nor the note.
Captured there, a `pixcheck` round trip reports 13,000–19,000 differing bytes
and reads exactly like a seed landing in the wrong place.

It cost a wrong diagnosis of §27.4.2 before the control was run: **the
pre-change build failed the same four cases identically**, which is what said
the apparatus was at fault rather than the change. The fix is to wait for the
flag and *then* for **two identical consecutive frames** — the only statement
about the screen that a poll of a state byte cannot make. `drive.wait_until`
is a condition rather than a sleep for this reason; this is the same rule
applied to the pixels instead of to the state.

Two more in the same family, both of which produced a confident wrong number
in this round and both of which the bbox identified in one line. **Both were
found independently on the branch this work was done on, and §8 has since
closed both at the source** — they are kept here because the *reasoning* is
what a future measurement needs, not because either symptom is still live:

- **A freshly loaded note had a toast drawn over it**, and a forced repaint
  then differed from the screen by a toast-sized rectangle — ~3,700 pixels,
  which is not a layout bug. §5.2.1 chased the same residue and §8 explains
  why it was real: `np_toast` cleared `[np_msg]` as it drew, so the pixels
  were on the glass with nothing left to put them back. The toast is the
  kernel's now (SPEC.md §60) and sits in the menu bar, in nobody's content,
  so this cannot recur — `np_toast` and `[np_msg]` no longer exist in this
  module. **The general rule survives the specific fix**: anything drawn over
  the content that a repaint will not reproduce is a difference the
  instrument will report and the code did not cause.
- **The mouse pointer sits inside the content box** — 108 bytes, 36 pixels, a
  7×11 bbox at `drive.README`'s (175,100), which is an 8×12 arrow. Arrived at
  twice, from two directions, in the same week: §8.1 is the other, and
  `pixcheck` parks the pointer outside the content box before its first
  capture now. Two people reading the same bbox and reaching the same answer
  is the argument for always printing it.

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

---

## 8. The toast moved to the kernel, and the 227 pixels went with it

§5.2.1's residue is closed. `pixcheck` reported **227 differing pixels on row
0 of the content**, at the toast's rectangle, with `[np_msg]` = 0 and
unchanged over 720 frames. It was the toast, and the divergence was real
rather than an artefact: `np_toast` **cleared `[np_msg]` as it drew** — the
fix for a drag re-showing `Loaded README.TXT` — so the toast was on the glass
with nothing left to put it back, and a forced full repaint legitimately did
not reproduce it.

What reconciled the two paths was `np_sigsame`'s fifth and sixth tests
(`[np_msg]` and its generation), which threw the fast path away and repainted
the **whole content** on the next keystroke. So every save and every load was
followed by one full `np_paint` — the exact operation the rest of §7's budget
work exists to avoid — and it was invisible because it happened one keystroke
after the thing that caused it.

The toast is SPEC.md §60's now: an inverse-video strip at the right end of the
menu bar, `OSAPI_TOAST` at slot 0x0380. What went with it from this module:
`np_toast` (91 lines), `[np_msg]`, `[np_msgn]`, `[np_smsg]`, `[np_smsgn]`,
the four toast-box words, `np_sigsame`'s two tests, `np_sigmark`'s two
stores, the `mov word [np_msg], 0` on the hot keystroke path, and **two
"a toast sits over the text" gates** — one in `np_scrollpaint`, which refused
the blit outright, and one in the band path. Both were correct and neither is
needed by a message that is in nobody's content. **−239 bytes** of package.

`np_saymsg` is the whole of what is left, and it is now the single point every
message goes through — `np_setmsg`, `np_errmsg` and the two literal writers
were all routed into it rather than storing a word each.

**Measured**: `pixcheck` 227 → **0**. The strip's own behaviour was verified
separately on a cycle-accurate 5150/CGA: it goes up, and when it expires the
menu bar comes back **0 differing bytes** against the capture taken before it
appeared; and a differing span that CROSSES the strip's left edge — the
two-run emit, which is the only genuinely new drawing logic — is **0
differing bytes** against a forced full bar redraw.

### 8.1 The instrument moved the mouse between its two captures

`pixcheck` reported 108 bytes in a 7x11 box at `(175, 100)` after the
migration, which is `drive.README` — the double-click that opens the note
leaves the pointer there, and the routine then moves it to the title bar to
force the repaint. **36 pixels of mouse arrow**, read as a real mismatch and
costing a diagnosis. It is exactly the misattribution `tools/notepad/README.md`
warns about and PERFORMANCE.md Part 3.1's third trap names, arriving in the
instrument that carries the warning. It parks the pointer outside the content
box before the first capture now.
