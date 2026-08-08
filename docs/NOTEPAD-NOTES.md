# Note Pad — designs recorded but not built

The counterpart to `docs/PAINT-NOTES.md`: what has been thought through for
Note Pad and deliberately not implemented, with the reasoning that decided it
and the trigger that would change the answer. Recorded so the next person
reaches for the finished argument rather than re-deriving it — and so a
decision that was right in one round is re-opened for the right reason in the
next, not for a hunch.

The implemented behaviour is SPEC.md §27. This file is only the things that
are *not* in it.

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

- **`NP_HCHUNK` = 16 is about four times too big, and the field run is what
  settles by how much.** It was reasoned from ~2 ms per measure row (no
  framebuffer: `np_draw` and `np_sigup` are both clear), putting a chunk at
  ~32 ms and inside one 55 ms tick. `make npbench` says otherwise — on
  MartyPC, README.TXT loaded:

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

  It is still a 38x improvement on the 4.7 s freeze it replaced, so it is not
  a regression and was right to ship; it is simply not yet tuned. **Pick the
  new value from the field number and not from this table**: `NP_HCHUNK` ≈
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
