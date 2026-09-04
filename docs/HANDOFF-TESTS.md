# Handoff: the twelve failing soak rows, in three batches

**These twelve rows fail on an UNTOUCHED tree.** They were found by a full `soak`
run taken as a *baseline* before a kernel size-optimisation pass began — 176 passed,
17 failed, 8 skipped, 14,301 s. **Nothing here was caused by that pass**; no kernel
byte had been edited when the run was taken.

Five of the original seventeen have already been cleared and are **not** in these
batches: `wireflick`, `wirefps`, `uilat`, `fdlggrey` and `blitp` were failing only for
want of prerequisite disks that the `soak` tier never builds. `make bench`,
`make wiredisk` and `build/muptest.img` fixed all five, verified by re-running them
(41–56 s each, all green). **Build those three before you start** or you will chase
ghosts.

Read this file, then your batch's file:

| batch | file | rows | the hypothesis |
|---|---|---|---|
| **A** | `HANDOFF-TESTS-A-STRADDLE.md` | `dispmode` `dispmodex` `dispbrow` `dispstrad` `dispnp` | snapping/placement on a straddle changed and the tests were never caught up |
| **B** | `HANDOFF-TESTS-B-LAUNCH.md` | `mouseup` `dispcalc` `dispprefer` `dispsize` | something fails to open or to click, and the row then hangs or reads nothing |
| **C** | `HANDOFF-TESTS-C-FRESH.md` | `paintsize` `tmselfsu` `tpdraw` | written on 2026-08-29 and failing on the very feature their own commit added |

**Eight of the twelve are dual-display rows.** That concentration is the single most
useful fact here: this is a small number of defects, not twelve.

---

## The rule that governs all three batches

**A test may be wrong, and a test may be right. Decide which, and say which.**

Two of these are already proven to be the *test* at fault (`dispmode`, `dispmodex` —
see batch A). Others assert a documented invariant and are correctly refusing a real
defect. **Never make a row pass by loosening its assertion, raising its budget, or
deleting a leg.** If the row is right, the fix is in `kernel/` and the row stays as it
is. If the row is wrong, fix the row *and say what changed underneath it*, because the
next reader needs to know the invariant moved.

A timeout is not a licence to raise a timeout. **A run past ~180 s has FROZEN, not
slowed** — MartyPC runs the guest ~4.8x faster than real time on this container, so a
generous limit is not insurance, it is how long you wait to be told something went
wrong.

---

## Environment — set this up first

```sh
apt-get install -y nasm libudev-dev pkg-config qemu-system-x86   # nasm is required
tools/martypc/build.sh          # the emulator; needs cargo + libudev-dev
tools/setup-cc.sh               # SmallerC, for the `cc`-gated rows
make                            # the shipping images
make bench wiredisk build/muptest.img    # the prerequisite disks, see above
```

`make marty` at the START of a session, not when you first need it.

## THE EMULATOR IS A SINGLE RESOURCE — this bites silently

There is ONE MartyPC debug port (127.0.0.1:9001) and one QEMU QMP socket. **A second
client does not error: it silently drives the FIRST one's guest.** Every screendump
then succeeds and shows the wrong machine, which reads exactly like "the change did
nothing". `tools/os88test.py` already serialises emulator rows onto one lane; if you
drive the emulator by hand alongside a suite run, you are the second client.

Kill a stale QEMU with `pkill -f "[q]emu-system"` **in a command that does not itself
mention QEMU** — `-f` matches the killing shell's own command line, so the bare
pattern kills that shell and so does the bracketed one if a relaunch shares the line:
exit 144, no error text, nothing dead, and every command after it silently skipped.

## THE COMMIT TRAP — this will cost you an hour if you meet it cold

The About box's build number is the **commit count** (SPEC.md §14.2), so **every commit
moves three bytes of `.text`**. `tools/os88sym.py` re-assembles `kernel.asm` and refuses
an address unless the result is byte-identical to `build/kernel.bin` — so after any
commit, every emulator row dies saying *"the map describes a DIFFERENT kernel"*, which
points at the kernel rather than at what you did.

**Run `make` after committing. Always.**

## Driving the machine

```sh
python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90      # NOT two clicks
python3 tools/os88marty.py 127.0.0.1:9001 shot out.png --rendered
python3 tools/os88test.py soak -k dispmode -v                  # one row
```

Use `os88mouse.py`, **never** `os88marty.py mouse` — it reads the cursor back instead
of dead-reckoning. And crop/zoom a screendump before concluding a click was lost; a
small change is easy to misread as "nothing happened" in a full-screen dump.

---

## Validation protocol — the same for all three batches

1. **Reproduce first.** Run your rows on an unmodified tree and confirm you see the
   failure described in your batch file. If you do not, say so — the container's speed
   or a missing prerequisite disk is then the story, not the row.
2. **Diagnose before touching anything.** Say whether the ROW or the KERNEL is wrong,
   with the evidence.
3. **Fix, and run your batch green.**
4. **Then run the FULL `soak` tier**, because a fix in `kernel/` can move a row nobody
   assigned you. `python3 tools/os88test.py soak -v` is ~4 hours; budget for it.
5. **Report against this baseline** (the numbers above), naming any row whose status
   changed in either direction.

**Then it gets run a second time.** A kernel size-optimisation pass is landing on
`claude/kernel-size-optimization-vx08di` separately. Once both are in, the whole soak
tier is re-run so that any interaction between your fixes and that pass is caught. So
**keep your changes reviewable and separate** — a small, well-explained diff per row is
worth far more here than a clever sweep.

## What is already known, so you do not re-derive it

* The five disk-prerequisite rows are cleared (above).
* `dispmode`/`dispmodex` are **proven** to be test-side, by grep, with no emulator —
  see batch A. Start there; it is the cheapest win in the set.
* `dispstrad` and `dispbrow` are very likely **one** defect: both are a 2-pixel cut on
  an axis where the window fits.
* `paintsize` is **not** marginal — 158,022 ms against a 20,000 ms budget is 8x.
* `mouseup` fails its FIRST check (`launched`), so nothing it is named for is under
  test yet.
* The 8 SKIPPED rows skipped only for missing capabilities and now run: `ctoolchain`,
  `weavesmoke` (needed SmallerC), `vgadirty`, `ps2mouse`, `heapmap`, `xmcheck`,
  `minesrc`, `trkscrl` (needed QEMU). **Re-run them; they are unmeasured, not passing.**
