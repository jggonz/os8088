# The icon store's shed repair, and the `dosglyph` red it carried

**RESOLVED. The kernel was never wrong.** `dosglyph` was reading a slot the
mount had not written yet, and the branch's kernel change moved the timing
across the edge of a race the row had carried all along. The fix is
`8bde5610` on `elendilon-next` — `os88ui.open` waits for the MOUNT rather than
for the listing — and with it merged the row is **5 runs out of 5 green**,
with no kernel change at all.

This file is kept for two things: the measurement that settles it, and the
**correction of its own elimination table**, which had reasoned itself into
"dead code flips it" and stopped one question short.

## What the feature is

SPEC.md 25.9.5. A DOS box claims the whole arena, which is a full compaction,
which correctly drops the purgeable machine-wide icon store — and nothing put
the pictures back, because **a repaint is not a mount**. Every Disk window on
screen went on drawing SPEC.md 25's generic icon until the user navigated
somewhere else. Reported from the field as *"the file manager redraws, but
does not reload its icon cache assoc.dat so things draw with no icons"*.

Two halves: `[ico_n]` surviving the shed (so SPEC.md 54.7.4's guard could not
fire and the next mount paid a sector per package), and the reference bytes
in every window's listing naming rows that had gone. `tests/icoshed.py` is the
gate and is VERIFIED RED two ways.

## The `dosglyph` red: what it actually was

`dosglyph` step 5 forces a store miss and expects the harvest to read
`DOS.O88`'s first sector and take the SHIPPED glyph. On this branch it took
the REDUCTION instead. Steps 4 and 6 passed.

**The measurement that settles it**, taken with the row's own navigation and
`m.advance(cycles=)` stepping afterwards:

| at | DOS's slot | FDC reads | sectors |
|---|---|---|---|
| the moment `open_named` returned | the POISON | 42 | 295 |
| 100,000 guest cycles (21 ms) later | the SHIPPED glyph | 42 | 295 |

**The read count does not move.** The harvest had already read `DOS.O88`'s
sector; the kernel simply had not yet reached the instruction that writes
`assoc_glyph`. The row read the slot inside that gap.

And the store is byte-identical either way — probed at every step, 11 rows,
`DOS` at row 1 with its size word broken to `0xFFFF` and its shipped glyph
intact, on a passing run and a failing one alike.

Why the verb returned early: `os88ui.open` waited for the listing NAMES to
change, and a mount writes entries into the store the window already holds
long before the harvest runs. `8bde5610` adds an `os88marty.quiesce` on the
FDC read count, which cannot return until the controller has been quiet for a
guest second — three orders of magnitude more than the 21 ms gap. `dosglyph`
reaches it because `dispcp.open_named` routes to `ui.open` on MartyPC. The
same fault had already taken `tanksmall`, `pathcost`, `uilat` and `wireflick`
red, where it read as four different product bugs.

## The elimination table, and where it went wrong

Every row below was measured and every row is still true:

| tree | result |
|---|---|
| branch point (`0ae926d8`) | **0/5 fail** |
| this branch, whole | **5/5 fail** |
| `mem_pg_forget`'s `.ico` arm disabled (so the new routines are unreachable) | fail |
| the mount's `call dsk_docpass` removed | fail |
| branch point **+ the two routine bodies only**, uncalled | **fail** |
| branch point + 8 / 170 / 340 inert bytes at the same spot | ok |
| branch point + 600 inert bytes before `ico_find` | ok |

The conclusion drawn was *"DEAD CODE flips it and the same number of inert
bytes does not"*, which is where it stopped. **The kernel is COMPRESSED**
(SPEC.md 2.9.13): `PKGZ`/`KZIP` is the default, so a run of zero padding costs
the packed `KERNEL.SYS` almost nothing and 172 bytes of real code costs it
real sectors. Every "inert bytes" arm was therefore a control that **held the
boot timing fixed**, and every "real code" arm moved it. The table was
measuring the race's phase, not the code.

The lesson is one line, and `docs/WRITING-TESTS.md` has it now: **when a
build-size experiment separates code from padding, ask what the packer does
with each before concluding anything about placement.** The cheaper check is
cheaper still — a row that flips on unrelated kernel bytes is a row with a
race in it, and the first question is what it waits for.

## What was ruled out, correctly

- **the cold rung / `KERN_SIZE` / the heap ladder.** The 600-byte padding
  build had the identical rung state to this branch and passed.
- **the repair replacing a re-list.** The store is never shed during
  `dosglyph`: `ico_seg` = 24E0 and `[ico_n]` = 11 throughout, `FS_DIRTY` = 0,
  and `fmv_icorefs` never runs. Confirmed again here by dumping every store
  row at every step.
- **the document pass.** Removing the mount's call to it does not fix it, and
  the branch point has it.
