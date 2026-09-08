# Handoff: the kernel size pass, and the next twelve files

**Pass 1 is done and merged to `elendilon` at `9c4c5e9`.** It took twelve kernel files,
found 229 candidate optimisations, and landed **−6,656 bytes off every machine** with two
512-byte rungs given back. This file is what the next pass should read first: what was
done, what the numbers actually are now, which twelve files come next, the method that
worked, the tooling that exists because of this pass, and — the part worth most — the
mistakes that cost time.

---

## 1. What pass 1 covered, and what it left behind

Twelve files, chosen as the union of "largest by source" and "largest by compiled bytes".
Those two orderings disagree, and **compiled bytes is the right one**: `ctrl.inc` is the
6th largest source file and produces 1,055 code bytes, because it is mostly comment and
its real weight is in `.modc`.

| file | code bytes before | after | Δ |
|---|---:|---:|---:|
| `wm.inc` | 12,617 | 11,916 | −701 |
| `files.inc` | 9,878 | 9,358 | −520 |
| `vga12.inc` | 8,693 | 8,120 | −573 |
| `disk.inc` | 6,681 | 6,194 | −487 |
| `fdlg.inc` | 5,597 | 5,279 | −318 |
| `diskw.inc` | 5,246 | 4,805 | −441 |
| `mouse.inc` | 4,806 | 3,701 | −1,105 |
| `kernel.asm` | 3,583 | 3,142 | −441 |
| `ui.inc` | 3,516 | 3,372 | −144 |
| `menu.inc` | 3,086 | 2,939 | −147 |
| `driver.inc` | 3,034 | 2,538 | −496 |
| `ctrl.inc` | 1,060 | 1,055 | −5 |

### Where the budgets stand now

```
.text 52,668 · .bss 5,813 · .cold 38,823 · .lowbss 9,096 · .vgabuf 848 · .ovl 6,688 · .boot2 2,457
KERN_SIZE  113,664 of KERN_BUDGET 129,536 ->  15,872 spare   (was 9,216)
.text+.bss  58,481 of KERN_CODE_MAX 65,536 ->   7,055 left   (was 2,432)
heap floor 119.0 -> 112.5 KB      BOOT2_SECS 13 -> 19
```

**The segment was the binding constraint and no longer is.** It had 2,432 bytes; it now
has 7,055. If the next pass is being justified, justify it on `KERN_BUDGET` or on a
specific feature that needs room — not on the 64KB window, which is no longer scarce.

---

## 2. The next twelve

By compiled code bytes, excluding what pass 1 covered:

| # | file | code | notes for whoever takes it |
|---|---|---:|---|
| 1 | `assoc.inc` (§54) | 2,914 | 523 `.text` / 2,391 `.cold`. File-type associations: a table plus a parser, and pass 1 never looked at either |
| 2 | `memory.inc` (§50) | 2,680 | 35 `.text` / 2,645 `.cold` + 324 `.lowbss`. The claim heap. **Read §66 and the compaction rules before touching a byte** |
| 3 | `instance.inc` (§29) | 2,425 | **694 bytes of `.bss`** — the largest `.bss` claim left, and `.bss` shares the 64KB segment |
| 4 | `filecp.inc` (§22.3–22.5) | 2,423 | all `.cold`. Cut/Copy/Paste. Pairs with `files.inc`, which pass 1 already thinned — check for newly-shared code |
| 5 | `font.inc` (§6) | 2,087 | +218 `.bss` +768 `.lowbss`. **HOT** — the glyph inner loop. Setup only |
| 6 | `apps.inc` (§14) | 1,813 | 251 `.text` / 1,562 `.cold` + 240 `.lowbss` |
| 7 | `icons.inc` (§10) | 1,666 | all `.text`. Icon renderer — likely shares geometry with `wm.inc`, now refactored |
| 8 | `softgfx.inc` (§32) | 1,243 | all `.text`. **The 1bpp twin of `vga12.inc`** — pass 1 reshaped vga12 and did not revisit this. Highest-probability duplicate in the list |
| 9 | `vidsel.inc` (§39.11) | 1,229 | pass 1 took its boot probe trio to `.ovl`; the rest is untouched |
| 10 | `snd.inc` (§34) | 1,178 | +300 `.bss` |
| 11 | `fsx.inc` (§53) | 1,122 | fullscreen exclusive |
| 12 | `sched.inc` (§7–8) | 1,059 | +124 `.bss` **+2,746 `.lowbss`** (task stacks). **HOT** — `sch_switch`'s pick. Read docs/plans/completed/SCHED-IDLE-PLAN.md |

**Two of these are worth more than their rank suggests.** `softgfx.inc` is the 1bpp
renderer and `vga12.inc` is its VGA counterpart; pass 1 merged four rect decompositions
inside vga12 and explicitly noted 61 bytes of the same shape sitting in softgfx that it did
not take. And `instance.inc`'s 694 `.bss` bytes are the biggest single `.bss` claim left.

**The long tail below these twelve is ~6,000 bytes** across 20 files of 12–1,051 bytes each.
Pass 1's sweep agent covering that tail produced the single largest finding set of the
twelve (`.text` −2,169 proposed), so do not assume small files are picked over.

---

## 3. The method that worked, in order

1. **Baseline everything before touching code.** A full `soak` run (~4 h) and a
   `sysbench`/`gfxbench`/`deskbench` performance baseline. Pass 1 did both and needed both:
   the soak baseline is the only reason 17 pre-existing failures were not blamed on the
   pass, and the bench baseline is the only reason three predicted slowdowns could be
   settled at the end.
2. **One finder agent per file**, briefed from one shared document. Ask for the byte count
   to be **assembled**, not estimated.
3. **Consolidate and de-duplicate.** Finders report cross-file duplicates from both ends by
   design, so a naive sum is wrong. Pass 1's de-dup moved the total by 528 bytes and found
   12% of the `.cold` claims were double-counted.
4. **Adverse review, grouped by shared risk rather than by file.** Ten groups. This is where
   the value is: it killed the largest single finding (a 663-site "improvement" whose own
   arithmetic was wrong) and repaired dozens.
5. **Integration review** for what only appears in combination: ordering, double-counting,
   register pressure across merged routines, and the test blast radius.
6. **Implement in large sequential batches**, each gating itself with `make` +
   `os88test.py fast` + `os88ovlchk.py` before moving on.
7. **Re-run the soak and the benches, and compare.**

### What earns the effort

The three review phases removed roughly a fifth of the proposed bytes and **found four
real bugs that were not size findings at all**. If you skip them you will land the bytes
and the bugs.

---

## 4. Findings that will still be true next pass

* **Compiled bytes, not source bytes.** See §1.
* **Section matters more than size.** `.cold` is resident forever; **`.ovl` is released at
  `spl_finish`**, so a boot-only body moved there costs nothing after boot. That converts a
  *move* into a *true reduction*. `docs/plans/LAST-DROP-BYTES.md` is the live register of what is
  still eligible.
* **Report three numbers, never one**: true reductions, section moves, and the combination.
  A single "bytes saved" figure hides whether the machine got anything back.
* **A rung crossing is what the user feels.** Byte counts are the currency; the two 512-byte
  rungs are the payment.
* **Shorter is often faster** on the 8088's `max(clocks, 4.34 × bytes)` prefetch floor —
  but pass 1 measured three cases where it was not, and two shipped anyway. Measure.
* **"Hot" is narrower than it looks.** Per-pixel loops, the mouse ISR, IRQ0, `sch_switch`,
  `dsk_xfer`'s per-sector site. A loop that runs once per file operation is not hot.

---

## 5. Tooling that exists because of pass 1 — use it

* **`tools/os88ovlchk.py` now has 11 checks, up from 7.** Four pre-existing holes were
  closed and two rules added. It caught real defects three times during implementation.
  Two of the new rules exist because `.ovl` **fails silently**.
* **`tests/ovlrefs.txt`** — every reference into `.ovl` registered with the reason it is
  boot-only. A new one fails the build until somebody answers *what guarantees this runs
  before `spl_finish`?*
* **`tests/int0sweep.py`** — arms INT 0 across a UI session **on the IBM ROM**. Pass 1's
  worst bug was a divide overflow that hard-locks an IBM machine (the BIOS stub masks the
  8259) and is merely a wrong clip index on GLaBIOS. **Every other MartyPC row in the suite
  runs GLaBIOS**, so that entire bug class was invisible.
* **`tools/kernsize.py --modules`** — per-module attribution, byte-exact, and it proves it
  did not perturb the build.
* **`tools/os88geom.py`'s `_MIRROR`** — host tools should read kernel constants from here.
  Two independent teams added to it during pass 1 for the same reason.
* **`docs/plans/LAST-DROP-BYTES.md`** (what we could still do) and **`docs/plans/LAST-DROP-PERF.md`**
  (what we chose not to do, or took and priced). Both are live.

---

## 6. Mistakes pass 1 made. Read this section twice.

* **A wrong `grep` reported a clean soak for hours.** `FAIL` lines start at column 0 while
  ` ok` lines are indented; `grep -cE '^ FAIL'` returned 0 through 15 real failures.
  **Dump the distinct line prefixes before trusting any count**, and prefer the runner's own
  summary line.
* **Two "regressions" were unsound tests.** Both were attributed to the pass and neither was.
  Check whether a failing row fails at the branch point *before* bisecting.
* **A single-sample bisect named a docs-only commit.** A commit that cannot change behaviour
  cannot be first-bad. If a bisect lands somewhere impossible, the test is flaky.
* **The suite was a ROM monoculture and nobody knew.** MartyPC resolves a romset by what it
  `provides`, so machines naming an IBM romset silently came up as `glabios_pc` when the ROM
  file was absent. Check what the emulator actually loaded.
* **A stale QEMU held `build/qmp.sock`** and made a row fail in 0.2 s.
* **`checkdocs` only walks tracked files**, so a bad `§` citation passes a pre-commit run and
  fails on the commit that adds it.
* **The kernel has no push/pop balance gate.** `stkbalance` is scoped to SHEET and CHART
  deliberately (the kernel's ISR tails push and pop under different labels). An imbalance
  introduced during pass 1 went green through `make`, the fast tier and `stkbalance`.
  **Run it over `kernel/*.inc` yourself and diff before against after.**
* **Predicted costs were allowed to ship unmeasured.** Three did. One turned out to be
  double its prediction and was reverted at the end — after it had been merged, benchmarked
  and shipped in an image. Measure before landing, not after.

---

## 7. The standing rules that bit hardest

`SS ≠ DS` (so `[bp+disp]` is SS); ES belongs to nobody; a near call across a section
boundary assembles cleanly and runs wrong; a NASM local belongs to the last non-local label,
so **moving code re-parents every local inside it**; `.bss` cannot hold a non-zero resting
value; `[vid_w]`/`[vid_h]`/`[vid_stride]`, never `SCREEN_W`/`SCREEN_H`; text is `font_run`
and `tests/textsites.txt` only goes down; **a knob is bound by physics, never by a
documented limit** — it takes its own sector rather than shaping the shipped build.

And the one that is easy to forget: **the About box's build number is the commit count**,
so every commit moves three bytes of `.text` and `os88sym.py` rejects every address until
you `make` again.
