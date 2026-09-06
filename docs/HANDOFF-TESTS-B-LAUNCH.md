# Batch B — something fails to open or to click

**Read `docs/HANDOFF-TESTS.md` first** — environment, the single-emulator rule, the
commit trap, and the validation protocol are there and are not repeated here.

**Your rows:** `mouseup` `dispcalc` `dispprefer` `dispsize`

**The hypothesis you are testing:** these rows do not fail an assertion about the thing
they are named for. They fail *earlier* — a window does not open, or a click does not
land — and the row then either reports nothing useful or waits until it is killed.

That makes this batch a different kind of work from A and C. **Your first job is to see
the screen**, not to reason about the subject matter. A screendump at the moment of
failure will very likely answer all four.

---

## B1. `mouseup` — fails its FIRST check. The subject is never reached.

Measures: SPEC.md §13.7 — a package's mouse-up. `docs/MOUSEUP-PLAN.md` §4.2 is the
design record.

Reproduced by hand on `os8088_5150_cga_gla`:

```
== os8088_5150_cga_gla : SPEC 13.7 + os88ui.inc + MOUSEUP-PLAN 4.2 ==
  [FAIL] launched
nothing to test
```

`tests/mouseup.py:105` is `check("launched", w is not None)` and everything after it is
skipped. So **nothing about mouse-up is under test yet** — the row is not telling you
about §13.7 at all.

What it does before that point (`tests/mouseup.py:82-104`): boots
`build/os8088-360.img` with `apps="build/muptest.img"`, settles, opens the Disk window
if absent, **raises it** (deliberately — the comment says a double-click on a background
window spends its first click on `wm_front`, so the pair reads as raise-then-select and
the row never opens; that cost "one CGA-only pass to notice"), then double-clicks at
`d[0] + 40, d[1] + 18 + 30` to open the package, and looks for a window whose frame is
**240x100** (`MU_W`/`MU_H` in `tests/muptest/muptest.asm:43`).

Candidate causes, cheapest first:
1. **The double-click lands on the wrong row.** `d[1] + 18 + 30` is a hard-coded row
   pitch. If the Disk window's header height or row pitch moved, this hits the wrong
   entry or none. **A screendump right after the dblclick settles it in one look.**
2. **The package is not on the disk, or is not where the listing puts it.**
   `build/muptest.img` is freshly built and 368,640 bytes; check the file is listed.
3. **The window opened at a different size**, so `find(m, MU_W, MU_H)` misses it. Print
   every window's frame instead of searching for one.
4. The raise-click at `d[0] + d[2]//2, d[1] + 9` misses the title bar.

`build/muptest.img` must exist — `make build/muptest.img`. It is not built by `all`.

---

## B2. `dispcalc` `dispprefer` `dispsize` — three TIMEOUTs at 270 s

| row | measures |
|---|---|
| `dispcalc` | does the Calculator add up, fold cleanly, and redraw nothing spare? |
| `dispprefer` | does a package's PER-ADAPTER preference and floor survive a drag across the seam, and does a USER outrank it? (SPEC.md §11.100) |
| `dispsize` | what size is a window given when it lands on the other card? (SPEC.md §11.100.3 / §11.100.4) |

All three have a **60 s** declared budget and were killed at **270 s**.

**Two readings, and you must distinguish them rather than assume:**

* **A hang.** If a window never opens, a `settle()` that waits for a state which never
  arrives will sit until the harness kills it. That is the same failure as B1 and is why
  these three are in this batch rather than in batch A.
* **Container speed.** `dispcalcx` — a near neighbour — **passed at 181.9 s against a
  60 s budget** on this same container, so slow-but-correct is demonstrably possible here.

**Do not simply raise the budget.** Two things forbid it. First, a run past ~180 s has
**frozen, not slowed**: MartyPC runs the guest ~4.8x faster than real time, so a
generous limit is not insurance, it is how long you wait to be told something went
wrong. Second, all three are *also* dual-display rows sitting beside five that fail on
assertions — so a hang is at least as likely as slowness.

**The cheap discriminator:** run one of them directly with a screendump on a timer. A
frozen guest and a slow one look completely different after 60 s. If the guest is
progressing, the budget is the story and you should raise it **with the measurement
written down**. If it is parked, you have B1's bug again.

Note the overlap with batch A: `dispprefer` and `dispsize` are about placement across
the seam, so if batch A's width-cut defect (`dispstrad`/`dispbrow`, a 2-pixel narrowing
on an axis where the window fits) is fixed first, **re-run these two before diagnosing
them**. They may resolve for free. Coordinate — do not both fix the same thing.

---

## Technique for this batch

```sh
python3 tools/os88test.py soak -k mouseup -v
python3 tests/mouseup.py                       # direct, prints its own checks
python3 tools/os88marty.py 127.0.0.1:9001 shot out.png --rendered
python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90     # NOT two clicks
```

* Use `os88mouse.py`, **never** `os88marty.py mouse` — it reads the cursor back rather
  than dead-reckoning, and `dblclick` is a verb of its own with its own timing.
* **Crop and zoom** (`shot.py --crop --zoom`) before concluding a click was lost. A
  small change is easy to misread as "nothing happened" in a full-screen dump.
* `docs/TESTING.md` carries mouse pacing, double-click timing, the writable scratch
  images and when to reset them.

## The rule

If a row is hanging because the OS is wrong, **the fix is in `kernel/` and the row stays
as it is**. If a row is hanging because it clicks at a coordinate that stopped being
right, fix the row — and say what moved underneath it, because the next reader needs to
know. Raising a timeout is a fix only when you can show the guest was making progress
the whole time.
