# Batch C — written yesterday, failing on their own feature

**Read `docs/history/HANDOFF-TESTS.md` first** — environment, the single-emulator rule, the
commit trap, and the validation protocol are there and are not repeated here.

**Your rows:** `paintsize` `tmselfsu` `tpdraw`

**What makes this batch different, and why it is the most interesting of the three:**
all three test files were last touched on **2026-08-29 — yesterday** — and each one
arrived in the same squash as the feature it tests.

```
paintsize  3d06301  Elendilon -> Main (TANK ATTACK, Paint Stroke + Undo + RESIZE, ...)
tmselfsu   3d06301  Elendilon -> Main (..., SAVE-UNDER RAISE CACHES, ...)
tpdraw     f3a41d1  Elendilon -> Main (VGA Optimization Pass, TEXT FLICKER PASS 2, ...)
```

A row written alongside its feature and failing one day later is **not** a stale test.
Either the feature regressed between being written and being merged, or the row was
green only on its author's machine, or the merge of two branches broke something
neither did alone. **Find out which — that answer is worth more than the three fixes.**

Start each one by checking whether it **ever** passed here: check out the commit that
introduced it, build, and run the row. If it was red on arrival, the question changes
from "what broke it" to "how did it land", and that is worth saying out loud.

---

## C1. `paintsize` — an 8x budget overrun, not a marginal one

Measures (SPEC.md §42.8.6.1): a maximize GROWS Paint's canvas and a restore shrinks it,
so the two clicks walk `pt_ucopy` over every row at two strides.

```
FAIL: the restore took 158022 ms, over the 20000 budget
```

**158 seconds against a 20-second budget.** That is not a slow container; it is roughly
the number the row was written to prevent.

The row's own description names the bug it exists to catch:

> A row has AT MOST eight blocks and the walk assumed exactly eight: 97 seconds and a
> band of garbage in the saved picture.

So the historical symptom was **97 seconds and visible corruption**. The current
measurement is 158 s. **Check the saved picture for the band of garbage** — if it is
there, this is that defect back, and the row is doing exactly its job. If the picture is
clean but the walk is slow, it is a different defect wearing the same clothes.

`docs/plans/completed/PAINT-STROKE-PLAN.md` is the design record and carries three refusals, including
block-granular undo being built and measured **twice** before the third placement
worked. Read §42.8.3–§42.8.8.2 before changing anything in `pt_ucopy`'s neighbourhood —
the amortisation base (calls, then calls again, then CHORDS) is the whole lesson, and
§7 explains why "756 µs" must not be quoted as a floor.

---

## C2. `tmselfsu` — fails silently

Measures (SPEC.md §28.8.1): the Task Manager stops repainting for ITS OWN raise cache
and so gets to keep one — while still seeing everybody else's. That is what makes the
cut "the self-reference" and not "the range". The row is also the one that would notice
`tm_quiet`'s key going unrecorded again.

It failed at **242.3 s** (budget 300) with **no assertion text in the log**, so nothing
can be concluded from the baseline run. Run it directly and capture output first.

The feature it tests — save-under raise caches — is in the same squash as C1. Two rows
from one squash failing is a stronger signal than either alone: look for a common cause
before treating them separately.

`SPEC.md` §11.96.18 and §28.8.1, and the sibling row `clipkeep`, are the neighbourhood.
`clipkeep` **passed** in the baseline run, which is a useful bisection: whatever is
wrong spares the clip-arm path.

---

## C3. `tpdraw` — fails silently

Measures (SPEC.md §69.8): does TeXPad's INCREMENTAL source redraw draw what a full
repaint draws?

Failed at **278.1 s** (budget 300) with no assertion text. Close to its budget, so
establish first whether it is failing or merely running out of time — the same
discriminator as batch B: watch a screendump on a timer and see whether the guest is
progressing or parked.

It arrived with **Text Flicker Pass 2**, which is squarely about what gets redrawn and
when — so an incremental-vs-full redraw mismatch is exactly the kind of thing that pass
could have moved. `docs/plans/completed/TEXT-PLAN.md` is the standing account; note that STAGE 4 is done
and the transparent-text registry is a **ratchet that can only go down**
(`tests/textsites.txt`), so any fix that adds a `font_str`/`font_char` call site will
fail `textrules` as well.

This is the row the owner was least sure about. If it turns out to be a real TeXPad
redraw defect, say so plainly; if it is a timing artefact, say that too, with the
measurement.

---

## Standing constraints for this batch

* **Three defects here are invisible in an emulator**: a visible redraw (seconds on real
  hardware), a double-draw flash (anything drawn twice), and input overrun. C1 and C3 are
  both redraw questions, so a green screendump is **not** evidence on its own — count
  primitive calls, or measure.
* **A redraw is priced by how many primitive calls it makes, not by how many pixels it
  covers.** PERFORMANCE.md Part 5 is the standing budget; if your path is in that table,
  that row is the bar, and a change that reintroduces a full repaint is a regression
  against a documented number rather than a neutral refactor.
* **Nothing writes a pixel twice**, and text is `font_run`, not `font_str` (§6.1, §6.6).
* `SPEC.md` is updated **before** the change, not after.
