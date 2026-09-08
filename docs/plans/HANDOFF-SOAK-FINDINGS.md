# Handoff: what the pass-2 soak found that is NOT pass 2's to fix

The kernel size pass ran the whole of `tests/` — 235 rows in three passes, plus
a 61-row `gfxbench`/`sysbench` comparison per adapter — against a kernel it had
just moved 4,661 bytes of. Every failure was classified against the pass's own
base commit `073d4e7` before anything was concluded from it, and **the size
work is clean**: not one failure is a regression in kernel behaviour.

What the soak *did* find is a list of defects that were already there, and it is
written down here because they cost that pass about a day of investigation each
and will cost the next session the same. `docs/plans/completed/HANDOFF-KERNEL-SIZE-P2.md` is
the pass's own record; this file is the queue that came out of it.

**Nothing in here is speculative.** Each item says what was observed, what was
ruled out, and what evidence exists — and where the mechanism is a hypothesis
it says so in those words.

**Pass 3's soak appended to this file rather than starting another one** —
section E. Its findings are the same shape and one of them is in this queue
already (`dskwstage`, which pass 2 parked on a missing ROM), so a second
document would have split the row from its answer.

---

## How to read the classification

**`tools/os88bisect.py` IS THIS PROTOCOL, and running it is cheaper than
following it by hand** (`docs/plans/SOAK-PARALLEL.md` §10). It samples every point N
times so a rate is never read as a side, parses a row's output into LEGS so a
six-legged row can be sampled at all, and checks ancestry before comparing
anything — which are E1's three errors, in order.

    python3 tools/os88bisect.py classify <row>

Every failing row got the same two-sided treatment, cheapest step first:

1. **re-run ALONE on HEAD** — settles contention, which is the commonest cause
   and the cheapest to test.
2. **run at the base commit `073d4e7`** — settles pre-existing vs regression.
3. **bisect** — only where 1 and 2 disagreed.

The order matters: step 1 is one row of emulator time and decides whether step 2
is even the right question. Two rows were resolved by step 1 alone.

---

# A. Product defects — genuinely open, in shipped code

## A1. `trkscrl`: one scroll case of seven does nothing, at BOTH ends

**FIXED — and it was never a product defect**, which this entry called it in
bold. It is a key the test's own include bound to something the application
had already claimed.

```
down +1 rows: view  0-> 1  scrolls 1  repaints 0  ok
j    +2 rows: view  0-> 2  scrolls 1  repaints 0  ok
v    -2 rows: view  2-> 2  scrolls 0  repaints 0  FAIL
k    +3 rows: view  2-> 5  scrolls 1  repaints 0  ok
b    -3 rows: view  5-> 2  scrolls 1  repaints 0  ok
```

`tests/trkscrl.inc`'s `trk_dbg_keys` table binds `'v'` to −2. `tracker.asm`
compares `al, 'v'` for the **fullscreen SURFACE** (SPEC.md 45.13) **three lines
above** the `%ifdef TRKDBG` block that calls that table, so a `v` is answered
there and never reaches `trk_dbg_key` at all. The key was dead.

The table's own comment enumerates what not to use — *"NOT
l/p/r/x/d/w/m/1-4/f/space/Esc/Home, which the bracket already answers"* — **and
`v` is missing from that list.** The entry is `'u'` now, free in both handlers,
and all three places say why. `trkscrl` passes in 84.5 s.

**The tell was in the output the whole time and this entry wrote it down
without following it**: *"its neighbours pass, including `b -3`, which is the
same direction from a higher view and further. Nothing about `v` at view 2 is
special. That is the shape of a lost keystroke, not of broken scroll logic."*
That reasoning was right and stopped one step short — a keystroke is lost
either in flight or at the handler, and nobody looked at the handler.

## A2. `dispcheck`: a 270-second timeout, and every one of its own checks passed

**FIXED — a stale record stride, for the THIRD time in the same file.**

It hung with `TIMEOUT after 270s` and no output through the runner, which is
why nobody had looked: run directly it prints eleven lines, passes all three of
its own assertions, and then hangs in step 4, the pointer crossing.

`vid_ctx`'s per-display record is `VID_CTX_W*2+5` bytes. The row computed
`NWORD * 2 + 6` — one byte too many — so it read display 1's record a byte
late and believed

```
ctx[1] seg=5AB0 stride=768 cw=23554 ch=1 rseg=01B0 origin=(5122,256)
```

then asked the pointer to walk to **x = 16899** on a 1360-wide desktop.
`mo.to` cannot converge on that, so the row sat there until its timeout with
everything it actually tests already green.

**The file's own comment describes this failure happening twice before** — the
adapter kind moved it, then `vid_tseg` moved it, *"and it asked the pointer to
walk to x = 16769"*. The response then was to DERIVE the figure instead of
writing it down, and that is why it drifted a third time: the run's ends are
symbols and cannot drift, but the `+ 6` is a literal. `tools/os88geom.py`'s
checker could not see it either — it compares written-down copies, and this was
an expression, so it reported "267 local copies, 0 stale" over a stale one.

It comes from the mirror now, with a cross-check that the record's own run
still matches `VID_CTX_W`. `dispcheck` passes in 71 s: both cards up, the
pointer crossing and its round trip, a window dragged onto the secondary, and
fullscreen on both. **The gate had never once run to completion.**
`tests/dispband.py` carried the same `+6` in prose and is corrected too.

## A3. `GFX_BLITP` is 2.7% slower — CLOSED, it is the epilogue ladder

```
GFX_BLITP 256x16   +2.64% (cga)  +2.76% (herc)   3413 -> 3503 / 3409 -> 3503
GFX_BLITP 64x64    +2.76% (cga)  +2.76% (herc)   3409 -> 3503 / 3409 -> 3503
```

**It is the ladder, and the arithmetic that dismissed it divided by the wrong
number.** Built both ways on one tree — `gfx_blitp`'s epilogue as `jmp
kret_bp` against the seven pops written out, nothing else touched — the
Hercules bench reads **3,502 counts against 3,408**, which lands on the base
tree's 3,409 to one count. `GFX_BLIT4` in the same pair of runs moved by one
count in 642,000, so nothing else in `.text` shifted under it.

94 counts over 12 calls is 7.83 counts at 838 ns each: **31 clocks for one
taken near `jmp`**, the instruction table's ~18-22 plus a prefetch queue flush
it does not price. That is now measured and written down in three places —
PERFORMANCE.md's *What one rung of the ladder costs*, SPEC.md §15.1.2 (whose
"~18-22 clocks" was the basis for keeping eleven per-run exits out of the
ladder, a decision the real figure only strengthens) and
`docs/plans/completed/HANDOFF-KERNEL-SIZE-P3.md`'s reuse table, because pass 3 will add
sites.

**Why it was called unexplained**: 6.5 us was divided by the 756 us in
PERFORMANCE.md Part 1's table — "the FIXED PART of any `gfx_*` drawing call" —
and came out under 1%. But this row is a REFUSAL. `gfx_blitp` on a 1bpp
adapter checks its guards and returns (SPEC.md §5.4.3), so the call costs
238 us, not 756, and 6.5 of 238 is the 2.7% that was observed. PERFORMANCE.md
already carries *"never quote 756 as a floor a design must beat"* (Set 89) and
this is the same error one step along: never quote it as a denominator either.

**It is not worth reverting, and reverting costs 5 bytes of `.text`** —
measured, `50,613 → 50,618`, kern_big only (`GFX_PLANE` is undefined in
kern_small, where `gfx_blitp` is `stc / ret` and these lines are never
assembled). A `jmp rel16` is 3 bytes against the seven pops and the `ret`'s 8.

**But not for the reason first written here, which was that the refusal lands
once per session.** It does not. `pt_wantpl` is armed unconditionally by
`pt_onresize` (SPEC.md §42.13.1.3), and a refused probe is DESIGNED to stay
armed — `jc .again` skips the `mov byte [pt_wantpl], 0` — so on a 1bpp
adapter, where the probe can never be accepted, **every canvas repaint after
the window has ever been moved or resized pays one refused `gfx_blitp`,
indefinitely.**

It is still a wash, and the corrected frequency makes the case better rather
than worse, because it moves the denominator off the probe and onto the thing
the probe sits inside:

| | cost of the call | the 6.5 us is |
|---|---|---|
| 1bpp, refusing | 244.65 us | 2.7% of the probe — and the probe rides a canvas repaint that deskbench prices at **354 ms** on Hercules, so **~0.002%** of it |
| VGA, drawing | 15,639 us (256x16) to 38,087 us (64x64) | **0.017% to 0.042%** |

**2.7% was never the number to decide on.** It is a percentage of a call that
does nothing, which is exactly the denominator error that hid this finding in
the first place — and the same mistake made twice, once to dismiss the cause
and once to price the fix.

## A5. `dispmine` asked whether a cell opened, and meant whether the press arrived

**FIXED**, and it is the most instructive item on this page: the entry above
had already classified it — wrongly — as a contention artefact, and the first
fix for it was wrong too.

It failed **1 run in 4 alone on an idle box**, always with the same sentence:

```
the bottom row still cannot be played: a press at (248,194) opened nothing,
so it went past the window (frame 20..198) to the dock (SPEC.md 11.93)
```

The row pressed a control cell first — to prove the click path works — and
then asked whether the count of open cells had gone UP. **Two things in
`mines.asm`'s own reveal path make that unanswerable, and both were happening:**

```
    cmp byte [mn_state+bx], MN_S_COVER
    jne .out                     ; an OPEN cell is done: nothing happens
    cmp byte [mn_mode], MN_M_FRESH
    jne .armed
    call mn_place                ; lazy placement: first click always safe
```

* the control click's flood opens **anywhere from 1 to 62 of the 81** cells,
  measured, and if it reached the bottom-left one the press under test had
  nothing left to do;
* and the bottom-left cell can be one of the **10 mines**, in which case the
  press ended the game — `mn_mode` 1 → 2, `MN_M_LIVE` → `MN_M_LOST` — and
  `mn_revealed`, the only thing the row looked at, did not move.

Both read as "it went past the window to the dock" and blamed SPEC.md 11.93.

**The first fix caught only the mine** — predicate widened to "a cell opened OR
the mode changed" — and still failed 2 runs in 8, because the flood case is a
press that genuinely does nothing at all and no predicate can tell it from a
press the dock swallowed. **The fix is the ORDER.** The press under test goes
first, on a FRESH board: every cell is `MN_S_COVER` and `mn_place` puts the
mines around it, so it must open at least itself and must take the mode
`FRESH` → `LIVE`. The control is asked only when that press did nothing, which
is the one case where it has something to distinguish. **10 of 10 runs pass,
every one opening a cell**, against a measured 1-in-4 baseline.

**What this cost, and it is the page's own lesson three times over.** The
symptom named the dock, so the investigation went to `wm_hit`/`dock_hit` —
verified byte-identical to the base, correctly, and that was read as supporting
a harness explanation rather than as evidence the kernel was not involved.
Then one passing re-run closed it. Then a predicate fix was measured at 6 runs
and looked done at the 7th. The numbers were in the failure output the whole
time and nobody read the second column.

## A4. Four orphaned local blocks outside the kernel

**FIXED, and all four were dead by design drift** — each one's own comment
explains why the reachable world moved on without it:

| | |
|---|---|
| `apps/texpad` `tp_bact.next` | a copy of the routine's own FALL-THROUGH, which already calls `tp_next_page` |
| `apps/ftpd` `fd_drawctl.no` | a `stc`/`ret` in a routine that never sets CF and whose callers never test it |
| `drivers/hdd` `hd_iw_click.refused` | the greyed-control sentence. `hd_ibhit` answers 0 for a greyed button, so nothing could reach it — and its own comment half concedes it: *"a greyed control explains itself (SPEC.md 47 rule 5), so this says nothing new"* |
| `drivers/hdd` `hd_inst_apps.nboot` | *"the files are all there and the disk simply will not boot"* — a real distinction that is **no longer this phase's to make**. SPEC.md 52.10.10 moved the boot commit into the SYSTEM phase, and the branch went with it: that phase has its own `.nboot` with two live `jc`s into it. This copy stayed behind |

`ftpd.o88` −3 bytes and `texpad.o88` −5. The HDD tool driver's image is
byte-different and the same LENGTH — its packaged layout absorbed the ~13
bytes — so that half is a reachability fix rather than a size one.

**`t_asmrules` check 4 now walks `drivers/` as well as the kernel** (45 → 99
files), and the mutation test still fires. It does **not** walk `apps/`, and
that is a limit of the detector rather than a judgement: it reads fall-through
and named jumps, so a routine dispatching through a TABLE looks to it like a
wall of unreachable arms — `apps/weave/wfx.inc`'s `_wfx_eval` has 22 and
`apps/modplug`'s glyph picker 9, every one of them live. Adding `apps/` would
mean 31 exceptions to find 0 more defects, and an exception list is where this
class comes back.

# B. Harness defects — every one produces a FALSE failure

These are the expensive ones. A row that fails because the box has no compiler,
or because another row has not run yet, is noise that buries the failures that
mean something — which is exactly what the owner asked to have watched.

## A6. `paintrow` cannot provoke a repaint — FIXED, and it was UPSTREAM's

**FIXED. The provoker was wrong, and it was wrong at upstream `main` too.**

The first A/B here was a worktree at `1a415e9` — the commit immediately before
the 1bpp canvas — and it cleared *that* change and nothing else, because
`1a415e9` already carried the whole of that session's other Paint work. **The
second A/B is the one that settles it**: a worktree at `origin/main`, built and
probed, reads **identically** — same window rects, every pointer move failing
to reach `pt_blit`, a title drag hitting it every time. So the step has been
broken for as long as the geometry has been this shape, in nobody's recent
work.

**What the probe measured**, on this tree and on `main`, three targets each:

| provoker | reaches `pt_blit`? |
|---|---|
| `mo.to(rx + 3, ry + 3)` — what the row used | **no** |
| `mo.to(rx, ry)` — its fallback | **no** |
| `mo.to(4, 4)` | **no** |
| a drag of Paint's own title bar | **HIT**, every time |

**And the reason is on the glass.** The file row is at (164,145); Paint opens
at (71,24) 522×152 *over it*. So the move lands on Paint's own palette strip —
and the arrow is a save-under (§7.1), so crossing a window never asks it to
draw. The comment said *"anything that repaints it"*, and a pointer move
repaints nothing.

The fix is a small title-bar drag, which is the right provoker rather than a
lucky one: `wm_drag` damages what the window vacates and W_PAINT is Paint's
first `pt_blit` caller. It does not matter that the drag never completes — the
breakpoint stops the machine inside it, which is exactly where `patch_caller`
wants to be. The row now reads **0 of 466 columns differing on all six sampled
rows**, so `pt_line_get`'s four-plane reader is under test again.

Worth writing down because the failure mode changed twice on the way and each
disguise pointed somewhere else:

| tree | message | what it really was |
|---|---|---|
| `origin/main` | never repainted | **this finding, and it starts here** |
| `1a415e9` (before 1bpp) | never repainted | this finding |
| `c207fef` (1bpp, old fixture) | no `gfx_blitp` | `build/OS8088.GIF` has a TWO-entry colour table, so §42.23.6 opened it one bit deep and there was no planar canvas at all |
| `ef3e555` (colour fixture) | never repainted | this finding again |

The middle row was real and is fixed — `dispapps.colour_gif` derives a colour
picture that changes not one pixel — and fixing it is what let the row reach
its *original* defect again.

**The lesson worth keeping is about the SECOND fault, not this one.** Fixing
the fixture is what let the row reach its original defect again — and while
the fixture was wrong, `paintplan` **kept passing** on the same picture while
its stated proof, *"the canvas went planar by construction"*, had silently
stopped being true. A row that passes for a reason that has gone away is
worse than one that fails, and only reading a docstring against the new
behaviour caught it. `paint1bpp-colour` exists because of that: it asserts the
**negative** — four planes, sixteen colours, the 4bpp arithmetic to the byte —
which nothing in `tests/` did before.

**Checked for the same assumption elsewhere, and it is not there.** Six rows
call `mo.to` shortly before a breakpoint — `dispblitp`, `paintbig`,
`paintblank`, `paintfill`, `paintsu`, `tmrepair` — and in every one the move
PARKS the arrow clear of what is about to be read rather than provoking
anything, which is legitimate and why they pass. `paintrow` was the only row
that used a pointer move as a repaint provoker, and the *"anything that
repaints it"* comment appears nowhere else in `tests/`.
`tests/paintundo.py`'s docstring already warns about the neighbouring case
for a window *drag* on a 1bpp adapter, which is the same class of assumption
pointing the other way — so the pair of them is now the written record that
neither gesture is a repaint by itself.

## A7. `build/OS8088.GIF` is TWO COLOURS, and six rows needed it not to be

**The tree has one picture fixture and SPEC.md 42.23.6 made it monochrome.**
That section opens a GIF whose colour table has two entries **one bit deep on
any adapter** — which is right for that file — and `build/OS8088.GIF` has
exactly two. So every row whose subject is the FOUR-PLANE canvas stopped being
able to get one, and each said so in the product's words rather than the
fixture's: *"no gfx_blitp — the canvas is not planar"*.

**Six rows, and they were found three at a time, which is the lesson.** They
were fixed as they surfaced — `paintplan`, `paintrow`, `paintback`, then
`paintdraw` and `paintfill`, then `paintbig`, `paintlzw`, `paintpack`,
`blitcut`, `blitplane`, `dispblitp` — and each round was reported as complete
before the next one appeared. **`grep -l OS8088.GIF tests/` is fourteen files
and takes a second**; doing it at the start would have found all of them at
once. The rule worth keeping: when a change moves what a shared fixture MEANS,
enumerate its users before fixing any of them.

`dispapps.colour_gif` is the answer — two unused colour-table entries appended,
**not one pixel changed** — so every oracle of the form "the canvas against the
file" is unaffected and the rows stay pinned to the same picture. Deriving it
also keeps the repo free of a second binary (CONTRIBUTING.md 6).

### A7.1 Two things the failures said that were true and misleading

**`paintplan` KEPT PASSING.** Its docstring claims it proves "the canvas went
planar, by construction: the geometry comes off a breakpoint on `gfx_blitp`,
and on a build that stayed packed that breakpoint never fires". Once the
fixture was monochrome that stopped being true and the row still went green,
because what it *asserts* is a pixel comparison the 1bpp canvas also satisfies.
**A row that passes for a reason that has gone away is worse than one that
fails**, and nothing catches it: only reading the docstring against the new
behaviour did. `paint1bpp-colour` exists because of this — it asserts the
NEGATIVE, four planes and sixteen colours, which nothing in `tests/` did.

**`paintpack` read "the canvas is 466x1".** That is `blitpair` inferring the
canvas height from a blit, and 42.23.4's one-bit path blits **one row per
call**. Harmless there once the fixture is colour, but any harness that infers
geometry from a single blit will read 1 on a one-bit canvas.

### A7.2 …and one number that is NOT a fixture artefact

`paintlzw` measured `pt_line_put` at **199.1 cycles a pixel against a ceiling
of 150** — because the monochrome fixture sent it down 42.23's new BIT arm
instead of 42.13.1.4's unrolled planar one. Pointing the row back at a colour
picture restores what it was written to measure, and **the number stands on its
own**: the one-bit load path is a per-pixel loop with two table shifts and is
~33% over the planar arm's budget. §42.13.1.4's finding applies to it unchanged
— the 8088 charges for instruction BYTES, so a straight-line byte column beat
the rolled loop there and would here. Open, and worth taking with a measurement
rather than on this note's word.

## B1. Install-then-boot is broken by per-instance disk isolation

**FIXED.** `os88marty.stage_run_dir()` is a run tree the CALLER owns —
`launch(run_dir=...)` leaves its media alone, so the VHD survives the install
instance closing, and `launch` re-clones the FLOPPY on every call, so the boot
gets its blank one in the same directory. `hdboot` and `knobhd` install and
boot over one disk again, in-process rather than through a subprocess.
`hdboot` 106.5 s, `knobhd` 203.9 s on both adapters, `instdeep` still 98.9 s.

**And the first attempt failed for a reason worth more than the fix.** The
install timed out after 600 s: `instdeep` watched for the MBR to differ from
**the shared master's** bytes, on its docstring's reasoning that the master
"is never written and is the pristine master by construction". It was written
— by this session's own base-side classification runs, which execute code that
predates the isolation work and installs straight onto it. Every clone then
starts with an installed MBR, the installer writes the same bytes, and the
commit is invisible. `run_install` compares against **this instance's own
disk** now, and `install` refuses an already-installed one up front with the
cure in the sentence, instead of ten minutes of nothing.


**`hdboot`, `knobhd` and probably `blitcut`.** Bisected on a clean master to
`79d34b2` — *"Take the MartyPC concurrency work into size pass 2 (host-side
only)"* — 16 files, **zero** kernel, boot, driver or app source.

`tests/hdboot.py` runs `tests/instdeep.py` as a **separate subprocess** to do
the install, then opens **its own machine** to boot it:

| | where the install lands | what the boot sees |
|---|---|---|
| before `79d34b2` | the shared master VHD | the same disk — the install |
| after | `instdeep`'s own clone, discarded with the subprocess | a fresh clone of the pristine master — nothing |

So the boot finds an empty disk and reports "no desktop from drive C:".
`knobhd` installs-then-boots the same way.

**The per-instance isolation that makes `--marty-jobs 3` safe broke the one
workflow that needs state to persist BETWEEN instances.** That is a real trade
the concurrency work made without noticing, not a mistake in either direction on
its own.

**The fix**: do both halves in ONE instance, which is also what the rows are
actually asserting. (The alternative — let `instdeep` take a target disk and
have `hdboot` pass its own instance's — keeps two processes for no gain.)

**The kernel is independently exonerated.** `make DISKCNT=1` at both commits,
same workload, counters read through SPEC.md §57's debug registry:

```
base(073d4e7)  mounts=1  sectors=9  int13=2  max=8  resets=0  -> 4.50 sec/call
HEAD           mounts=1  sectors=9  int13=2  max=8  resets=0  -> 4.50 sec/call
```

Stated with its limit: that is the boot-time mount, not an install's payload
streaming, so it corroborates rather than proves. The proof is that the guilty
commit contains no kernel code.

## B2. `blitcut` — established, and the bisect was WRONG

**FIXED, and the entry above it was the right instinct.** The mechanism is not
B1's and not the concurrency commit's at all: **it is the size pass's own
epilogue ladder.**

The row breakpointed the return of `gfx_blit4` at `S("gfx_blit4.pops") + 7` —
seven bytes of `pop` and then the `ret`, counted by hand. SPEC.md 15.1.2's
shared ladder replaced that run with a three-byte `jmp kret_bp`, so `+7` landed
on unrelated code and the breakpoint never fired. The row then waited out its
whole 240 s and reported *"no straddling canvas blit arrived"* — **a sentence
about the kernel for a fault in one host-side line**, which is why it read as a
harness or boot problem and why a bisect placed it on a host-side commit.

What found it was making the row say which of its three failure modes it was
in, which it could not do before: *"the drag finished; gfx_blit4 entered 3
time(s), 2 of them wide (cx > 200), 0 matched returns"*. The drag ran, the
blits happened, the return never fired. One line.

**The ladder's `ret` is labelled `kret_ret` now** — a label is zero bytes,
A/B'd rather than asserted — so no test needs arithmetic to point at it. A
sweep of every other computed address in `tests/` came back clean: the rest are
struct-field offsets into DATA (`drv_tab + 2*16 + 2`, `ss_row + 2`), which the
ladder cannot touch, and `tests/blitp.py` already reads its return address off
the stack, which is the idiom that cannot go stale.

**This is a regression the pass caused**, and the summary's "not one failure is
a regression in kernel behaviour" survives it only on a technicality worth
stating: the kernel is correct, the row's address went stale. It is still the
pass's doing and it was mis-attributed for a week.

## B3. `os88layout.cold_span` measures `.cold` to EOF, and `.ovlw` sits after it

**FIXED** (`e8e1110`). The span runs to `OVLW_START` now, and the two messages name what actually disagrees rather than telling the reader to rebuild a current build. Both rows pass, at 259.6s and 260.1s against a declared 60 — nobody knew what they cost because nobody had seen one finish, and the declared times are 300 now.

**`dispcold` and `dispreboot`**, both:

```
RuntimeError: .cold is 42591 bytes from file offset 60416, against a
37376-byte rung below FAT_SEG - the map and the binary disagree, so
rebuild before trusting this
```

The arithmetic settles it in one line:

```
  37,376   the .cold rung
+  5,215   .ovlw
= 42,591   exactly what it measured
```

`cold_span` computes `.cold` as `filesize − offset`, on its own stated
assumption that *".cold is the last thing in kernel.bin and ends at EOF"*.
`.ovlw` sits after it, so the check subtracts nothing for it. The layout is
identical at the base — same `OVLW_START` equate, same section order — and the
check fails identically there.

**The fix is one line**: bound `.cold` by `OVLW_START` rather than by EOF. It
belongs to whoever owns `.ovlw`.

**A warning about this one.** Its message says *"rebuild before trusting this"*,
and that sentence is wrong here and cost a wrong diagnosis: nothing was stale.
`build/kernel.bin` is smaller than a raw assembly because `make` cuts the
`.modc`/`.modf`/`.modl` module images out of it, and `bootsmoke` passed on that
very build minutes later. **A check whose failure message names the wrong cause
is worse than one with no message.** Fix the message with the arithmetic.

## B4. The suite models TOOLS, not ARTEFACTS

**FIXED** (`e8e1110`), including the half this entry did not see. `fdlggrey` and `sbar` ask for `build/muptest.img` now — verified by deleting it, after which both build it and pass. **And the helper that builds a fixture runs `make`**, which `t_registry` already requires a row to declare: THIRTEEN rows were calling it from the shareable emulator lane with `builds=False`, invisible because the detector looked for a quoted `make` at the head of an argv list and the literal is one level down. It keys on the import now.

Three rows failed where they should have skipped, for one structural reason.
`marty`, `qemu`, `nasm` and `cc` are capabilities a box either has or lacks, and
a row naming one skips cleanly. *"The wire disk has been built"* and
*"`WEAVE.WSM` exists"* are **not** capabilities, so a row needing one has no way
to say so and fails instead.

| row | wanted | did |
|---|---|---|
| `weavegame` | `build/WEAVE.WSM` (needs `cc`) | FAILED where nine siblings SKIPPED |
| `wireflick` | `build/wire360.img` (`make wiredisk`; `all` deliberately does not) | FAILED, 0.1 s, a traceback |
| `fdlggrey` | `build/muptest.img`, built by ANOTHER SUITE ROW | FAILED, 0.1 s, `FileNotFoundError` |

**Two are fixed** — `weavegame` gained `needs=("marty", "cc")`, and a `wiredisk`
capability was added beside `cc`, satisfied by the artefact's presence, which
`wireflick`/`wirefps`/`uilat` now name.

**`fdlggrey` is NOT fixed and is a different animal**: `build/muptest.img` is
built by another row of the same suite, so whether it fails depends on the
ORDER rows ran in — and with `--marty-jobs` that order is not fixed. A
capability cannot express "after that other row". Either the artefact gets its
own build step, or the dependency gets stated.

**Why this matters more than three rows.** A skip says *this box cannot answer
this question*; a failure says *the answer is wrong*. Three rows said the second
when they meant the first. `wireflick` and `uilat` are two of the five rate rows,
so a soak's pass B would have failed half its rows for a reason that has nothing
to do with rates.

## B5. `settle()` is host wall-clock, so emulator rows are box-speed dependent

`os88marty.settle(m, quiet=1.0, stable=2)` compares rendered frames one **host**
second apart. How much GUEST time a settle covers is therefore a property of the
box, and every row that settles before clicking inherits that. So do
`os88mouse`'s `time.sleep(GAP)` and `_edge`'s `time.sleep(0.02)`, which run with
the machine going.

This is the root of three separate observations:

* **`dispmine` and `tmowner` failed in the 3-wide lane and passed alone** —
  46.5 s and 203.5 s with the whole box. `dispmine`'s symptom, a press at
  (248,194) reaching the dock instead of the window, reads exactly like a real
  hit-test defect, and `wm_hit`/`dock_hit` were verified byte-identical to the
  base before the re-run was believed.

  **AND THE `dispmine` HALF OF THAT WAS WRONG — see A5.** It is not contention
  at all. It fails **one run in four ALONE on an idle box**, and the cause is
  in the test's own predicate. The classification was made on a single passing
  re-run and it should not have been: N=1 is not a rate, and "passes alone" is
  the answer contention predicts *and* the answer an intermittent gives three
  times in four.

  `tmowner`'s half stands as far as it goes — it passed at width 3 in an
  eight-row slice with three guests on four cores (205.4 s) — and stands with
  the same caveat, which is now a demonstrated one rather than a hedge: **one
  passing run is not a classification.** Neither row is marked `alone=True`.
* **`deskbench`'s scene is not reproducible to the pixel** — 78,821 / 78,825 /
  78,830 lit pixels across three runs of the same build, because
  `new_window` waits on `time.time()`.
* **`weavepack` flaked once in two runs** (D2).

**MartyPC is bit-exact deterministic in guest time.** Everything above is the
harness re-introducing host time on top of it. `m.advance(frames=N)` is the
deterministic instrument and `os88marty.until` is the bounded observable wait;
the fix in each case is to wait on something the guest publishes rather than on
stillness or on the clock.

**PARTLY TAKEN, AND THE MECHANISM IS NOW MEASURED — `docs/plans/SOAK-PARALLEL.md`
§1.** The property this entry describes is real and is worse than it reads
here, because it does NOT show up as a slow row. Twelve rows run at width 1
idle and at width 3 with two extra CPU hogs were **1.06x** slower in wall time
(worst 1.17x) and **12/12 passed in both arms** — so no timeout was ever going
to fire. What moves is the GUEST rate. `OS88_WAITLOG` recorded **118 waits in each
arm - the same waits in the same scripts - at an identical median HOST cost
of 2.2s and a GUEST cost of 7.3s against 5.9s**, and up to **-37%** per
script (`curshape` 12.0 -> 7.6, `dispdrag` 8.0 -> 5.1). The host column does
not move and the guest column does. *Contention does not make a row slow, it makes it less thorough* — which
is exactly why the wall times in every classification run showed nothing.

What shipped: `settle`, `until` and `wait_stop` now spend a budget denominated
in GUEST seconds (`os88marty.GUEST_HZ`), a guest that stops advancing fails the
wait in ~2 s instead of at the end of the deadline (measured 2.1 s against
120), `guest_sleep()` is the standard replacement for `time.sleep`, and
`OS88_WAITLOG` records what every wait actually costs. The 194-file sweep this
paragraph warns about is still NOT taken and is behind `OS88_GUEST_PACE`.

The original text stands:

**STILL OPEN, and deliberately not taken yet.** Rewriting `settle` onto guest
time reaches 194 files that import `os88marty`, changes how much guest work
every row gets per settle, and would want a full soak behind it. The evidence
that it is causing failures has just weakened rather than strengthened (see the
first bullet), so the honest order is: run the next full soak with B4 fixed,
and take this if anything still moves. What is NOT in doubt is the property
itself — `deskbench`'s scene really does differ by nine lit pixels between runs
of the same build, because `new_window` waits on `time.time()`.

## B6. Rows that must not share a box have no way to say so

**FIXED** — and the entry as first written was wrong about the mechanism,
which is worth keeping rather than editing away.

It said *"`serial=True` exists and `dispmine`/`tmowner` do not set it"*. Both
DO set it, and setting it does not make a row run alone: `serial=True` is what
puts a row in the SHAREABLE emulator lane, which `--marty-jobs N` then widens
to N. The only flag that made a row run alone was `builds=True`, which is a
claim about rewriting the tree. **So a row that needed the cores could only say
so by lying about building** — and the four rate rows did not even do that,
they were excluded from the wide run by hand and taken in a second one, a
workaround written into two handoffs and remembered by whoever read them.

`alone=True` says it now, and the two flags are different claims: `builds`
cannot share the TREE, `alone` cannot share the CORES. An `alone` row runs in
the one-at-a-time lane of the SAME run, so the soak is one command again.
`saverate`, `deskbench`, `uilat` and `wirefps` carry it.

**`dispmine` and `tmowner` deliberately do NOT**, and that is measured rather
than assumed: see B5.

## B7. `trkrate` never calls `no_saver()`

**FIXED** (`e8e1110`). `trkrate` passes in 106.2s. `blitcut` got the same call on `no_saver`'s own rule — it sits still for 240 HOST seconds, over thirteen guest minutes — and NOT on evidence that the saver is its current failure, which it is not. The other three are argued below and get nothing.

```
os88marty.MartyError: the screen was still changing after 120s because
SPEC.md 79's SCREEN SAVER IS RUNNING - [blk_on] is set.
```

The harness names its own cause, which is the standard to hold the others to.
The saver arms on **guest** idleness — which is exactly what a row produces when
it sets something up and then waits — so host load is not needed, only waiting.
23 test files call `no_saver`; **`dispmine`, `hdboot`, `trkrate`, `trkscrl` and
`blitcut` do not.**

**Do not conclude from this that the saver explains the other four.** It was
tried: `dispmine`'s own saved screen refutes it outright — desktop up, menu bar
reading "Mines / Game", the grid live, **no saver** — and the picture instead
shows the window overlapping the dock band. A hypothesis that explains five
failures at once is attractive precisely when it should be distrusted, and the
way to test it was to open the picture the failing row had already saved. It
cost one `Read`.

## B8. `tests/sbar.py` has three unguarded `live[-1]`

**FIXED** (`e8e1110`). One guarded reader, naming the step that lost the window.

Found while fixing `deskbench`'s crash of exactly that shape. `sbar` builds a
window list and indexes `[-1]` three times with no emptiness check, so a window
that fails to open dies with `IndexError` rather than a sentence. Not currently
failing; cheap to make legible.

## B9. A failing QEMU row leaks its emulator, and an unrelated row pays

**FIXED** (`a472b8e`). `tests/os88qemu.py` is the teardown written once; every one of the thirteen launchers registers it at its launch site, and `tests/unit/t_qemuown.py` in the fast tier keeps that true. Verified end to end: `trkscrl` FAILS, nothing survives it, and `ps2mouse` — the row the leak broke — passes straight afterwards.

Found by the PRE-MERGE GATE rather than by the soak, which is why it is here.
`make test-full` on the merge failed `ps2mouse`:

```
qemu-system-i386: Failed to get "write" lock
Is another process using the image [build/os8088.img]?
```

Two `qemu-system-i386` processes were still alive, **five hours old**, both
mounting `build/trkscrl.img` — left by the `trkscrl` classification runs of A1.
`ps2mouse` passed the moment they were killed. So a row that FAILS can leave its
emulator holding `build/os8088.img`, and the bill lands on an unrelated row much
later, wearing a message about the wrong subject.

CLAUDE.md documents the stale-QEMU trap from the other end — a previous
session's instance still answering on `build/qmp.sock` and serving the OLD
kernel, which reads exactly like a change that did nothing. Same leak, different
symptom.

**The fix belongs in the row, not the runner**: a QEMU row must tear its
instance down on the failure path as well as the success path. Worth one sweep
of every `qemu-system` launcher under `tests/` for a `finally`.

---

# C. Measurement and documentation

## C1. Hercules write cost — CLOSED, and this entry's own mechanism was wrong

The correction landed: PERFORMANCE.md's *The framebuffer is barely slower than
RAM* now carries **two figures per cell**, one per BIOS ROM, all four rows at
N=48, plus the controls. Every documented ratio was low — `rep stosw` 1.57
against 1.75/1.82, `rep stosb` 1.36 against 1.67/1.50, the read-modify-write
1.09 against 1.126/1.125 — and the section's conclusion (the mono renderer's
inner step is instruction-bound, not bus-bound) survives at 82.0 clocks per
byte end to end against RAM's 72.8.

**But the mechanism this entry named is not the mechanism.** It said the
quantity "needs N=48" and that eight samples of a bimodal cost do not average.
Two measurements say otherwise:

- **N is not the variable.** One kernel reads `rep stosb` at 8,218.0 us with
  N=8 and 8,218.6 with N=48 — 0.01% apart.
- **Nothing is aliasing against the 19.19 ms frame.** Rebuilding the block at
  33 rows instead of 32 — which moves the sample's duration, the only thing
  that moves a phase orbit — scales every row by 33/32 to within 0.02%, RAM
  and VRAM alike.

What actually moves it is the **machine**. The same tree booted on GLaBIOS and
on the IBM 10/27/82 ROM, all rows N=48, puts the four RAM rows inside 0.014%
and the VRAM rows up to **10.2%** apart, in both directions. The report's
`ISA status port in` row — an `in` from the card's own status register,
touching no memory — moves 5.5% in the same pair, which bounds it sharply:
everything that goes to the video card moves and nothing else does.

**How the wrong mechanism got written down** is worth more than the mechanism.
N was the one thing deliberately varied between two runs that gave different
answers, so it got the credit — and the table in this entry that appeared to
prove it (*"base, N=48"* against *"HEAD, N=48"*, agreeing to five figures) is
**two runs of the same kernel**: the run filed as `base` reports `kernel span
106 KB` and `boot ticks 170`, which are HEAD's, against the real base tree's
111 and 177. The five-figure agreement was real and meant nothing. A control
that cannot be told apart from its treatment is not a control, and the report
printed the field that would have said so.

**N=48 is still the right thing to run**, for a reason this entry had right:
N=8 is not biased, it is *arbitrary* — one iteration is 36% of a frame — and
it costs seconds to remove that as a question.

**One trap for whoever raises N next**: the report's derived `VRAM/RAM word
x100` row divides raw COUNTS, so it means what it says only while both rows
share an N. Patching one side alone leaves it reading 6x high.

## C2. `deskbench` had no recorded numbers — CLOSED, the table is taken

`docs/plans/LAST-DROP-PERF.md` named `deskbench` on VGA as the measurement that would
settle whether an XOR-rect change is felt, and recorded that it *"has not been
taken"*. It has now, on all three adapters, and the table is in PERFORMANCE.md
under *What a BUSY DESKTOP costs* with its two caveats attached.

It answers less than the entry that asked for it hoped, and the reason is
worth keeping: **there is no before.** A first reading is a baseline, not a
comparison, so it cannot say the flash is new. What it can say is that the
asymmetry the entry predicts is there in the row that exercises the path —
moving the bottom window reads transient÷changed of 0.96 / 0.97 / **3.15**
across CGA / Hercules / VGA, the only row of eleven where the adapters differ
in kind — and that a second thing, which nobody was looking for, is four times
larger: **the fullscreen exit writes every pixel seven to eight times**, on
all three adapters equally, because §11.2 repaints the desktop and then each
window over it in z-order.

`docs/plans/LAST-DROP-PERF.md`'s bullet now carries that, and says plainly that the
honest test it named — a flicker run over a held drag, built both ways — is
still the one that would settle it.

---

# D. Fixed during the pass, recorded so they are not re-derived

## D1. `deskbench` crashed on noise and three rows priced nothing

Fixed. `measure()` guarded `if not any(ch)` where the threshold has a floor of
64, so a capture holding sub-64-pixel noise passed the guard with an empty
`live` and indexed it. `release()` spent a FIXED 24 frames of the mouse packet's
flight before opening the capture and the commit was over by frame 22, so three
of eleven rows read NOTHING DREW; it waits on the guest's own `mouse_btn` now. A
press row's held XOR outline was counted as redraw where the redraw was small,
which truncated `raise Control Panel` at 4,005 ms for an operation taking nine
frames; the row calibrates its own noise floor from its capture's second half.
A `burst="last"` arm with a docstring prescribing it for release rows had **no
caller and never had one**.

## D2. `weavepack` flakes — classified, not fixed

> **SUPERSEDED BY E6.** Pass 3's soak ran this row twice on the tree it had
> then and got **5 checks passed and 18 FAILED** both times — nothing like the
> 17-of-18 below, and not a flake at all in that shape. Read E6 first; what
> stands here is the mechanism (B5) and the fix proposal at the end, which E6
> does not replace.

Failed once on `TSHEET.WAB` (17 of 18 checks passed; six of seven projects
matched the host packer byte for byte), then passed alone at the same commit in
1828.6 s. It cannot be a regression by construction: `tests/weavepack.py`,
`apps/loom/` and `apps/cc/` are byte-identical at both ends of the pass, and
`apps/weave/wvm.inc`'s only change is five lines of comment.

The mechanism is B5, and `pack_one` states the hazard itself: *"a package LOAD
draws nothing while it runs, and so does a PACK, so `settle` sees perfect
stillness partway through either and returns."* It waits on the window for the
load and then falls back to three settles and a hope for the pack.

**The fix, if it is wanted:** `m.disk(reset=True)` before `^P` and poll
`m.disk()` until the write count moves — the guest's own answer to "has the pack
happened", bounded, so a pack that never writes becomes a *reported* refusal.
And screenshot the sidebar when the write never comes: the row's own `why` says
*"the sidebar has the sentence saying why"*, and the row throws that sentence
away. `weavesmoke._shot` is the family's helper.


---

# E. What the pass-3 soak added

Pass 3 moved 745 bytes of `.text`+`.bss` and ran the same suite behind it. Same
result at the top: **not one failure is a regression in kernel behaviour.**
Four things are worth the next session's time.

## E1. `dispsize` leg C is INTERMITTENT — and the bisect off it was void

`C: 1 pixel(s) of the CGA disagree with a full repaint after the adoption`.
The differing pixel is **(0, 0) of the secondary display** — the card's own
corner, nowhere near the window under test (743,64 418x151) or where the
pointer was left (959,73).

**It fails on every tree with more than one sample**, so it is nobody's
regression:

| tree | leg C |
|---|---|
| `f8af49e` — elendilon BEFORE the pass merged | 1 fail, 1 pass |
| `61d92f7` — the pass merged | 3 fail, 1 pass |
| `8626120` — HEAD | 5 fail, 1 pass |

Ten failures and five passes over fifteen runs. A pre-pass tree cannot fail on
account of the pass, and that is the whole of what is needed to clear it.

**What this entry is really for is the mistake.** Six single runs across
`f8af49e`, `61d92f7`, `cab55a9`, `2ef5052`, `94a9e23` and `5ecc7b1` came back
ok/ok/ok/ok/fail/fail, which reads as a clean bisect, and it named a commit
whose entire diff to shipped code is **four comment lines** (`~32.5KB` →
`~34.0KB`). That should have stopped the bisect and instead nearly published
it. Two separate errors made it:

1. **The protocol at the top of this file was skipped.** Step 1 is *re-run
   ALONE on HEAD*, and step 3 is *bisect only where 1 and 2 disagree*. No point
   was sampled twice before conclusions were drawn from all six.
2. **The row's exit code was read as one verdict.** `dispsize` has six
   independent legs. The one run that showed **0 differing pixels** still
   exited 1 — on leg E — so it was filed as a failure and the flake stayed
   invisible for four more runs. `/home/user/os88-bisect/legc.sh` in that
   session reported leg C separately, which is what finally made the rate
   legible; a row with independent legs needs that or it cannot be sampled at
   all.

A third trap sits underneath both, and it is this branch's rather than the
row's: **the commits being bisected did not share a base.** `cb3efa6 d5b591e
6f1a9a1 94a9e23`, `be4f8df 5a6940d 2ef5052` and `3254f48 cab55a9 5ecc7b1` all
fork from `f8af49e`, *not* from `61d92f7`, so four of the six points carried the
**pre-pass kernel** and were never comparable with the two that did not.
`git log --graph --boundary` says so in one screen and was not run until after
the conclusion. Only points containing `61d92f7` can be compared against it.

**Not yet known:** whether the rate genuinely differs between trees (5/6
against 1/2). Separating those needs ~20 runs a point and was judged not worth
the soak's remaining time. Stated as unknown rather than guessed at.

## E2. `dskwstage` — ANSWERED, and it is not the size pass's

> **ROOT-CAUSED AND FIXED: 6 of 6.** Two harness defects, one on top of the
> other, both in `tests/dskwstage.py`'s `Caller` — the class that calls a
> kernel routine on a synthetic context. Nothing in the kernel changed.
>
> **1. It ran a BIOS `int 13h` on a 128-byte stack.** The Caller took the
> paused machine's own `SS:SP` and dropped 96 bytes, on the reasoning that the
> call *"goes as deep as the BIOS's own int 13h handler, on this task's
> stack"*. It does — and `sch_stacks` begins at `.lowbss:0x15D6`, its first
> slice is the **128** class, and SPEC.md 8.1.2 guarantees which task a pause
> lands on: an idle desktop is 96.9% halted, so it is always the idle task's.
> Traced, SP started at `0x15F2` and reached **`0x1562`** — 116 bytes below
> `sch_stacks`, through its canary word and into `sch_chstack`, the ROM
> int 08h chain's own stack. It now runs on `STK0_TOP`, which is task 0's,
> which is **the stack the real caller uses**: a file operation on this
> machine *is* the UI task calling this routine.
>
> **2. It ran with interrupts off, so the motor never came up.** `park` goes
> through the reset vector, so it clears FLAGS with every other register and
> this had IF = 0 for its whole life. The BIOS's floppy handler starts the
> motor and times the spin-up on the BIOS tick at `0040:006C` — a byte only
> IRQ0 advances. Sampled at the timeout, the guest was at **`F000:FF23` with
> IF=0 and a PIC read in front of it**, parked in the ROM for 540 guest
> seconds.
>
> **The motor is the proof, and it is the cleanest evidence in the file.**
> `launch` settles on the first desktop, a few hundred milliseconds after the
> last boot read, with the drive still turning — and a handler that finds the
> motor already up skips the wait. So the row passed exactly when it won that
> race, which is what *"2 runs in 3"* was. Adding five idle guest seconds
> before taking over, past the ROM's own motor-off timer, took it to **0 of
> 6, deterministically.** Interrupts on plus `[sch_lock]` raised — the byte
> `sch_isr` tests before it picks, and exactly what `dsk_xfer` raises around
> every `int 13h` in this system — then takes it to **6 of 6 with the motor
> off**. Interrupts alone are not enough and were tried: without the stack fix
> under them the machine walked off to `CS=C301` executing zeros.
>
> **What made it diagnosable was making the row say WHERE.** *"the machine is
> still running after 180s"* is the whole of what four runs and two ROM sets
> had to work with below. It now samples the program counter six times, names
> the nearest symbol, prints IF, the code bytes, the stack and the registers,
> and says when the address is an interrupt vector rather than a hang. Two
> traps found on the way: `wait_stop`'s budget had already become guest
> seconds during this pass, so *"180s"* was never the host claim this entry
> treats it as; and `os88sym.syms()` answers **section-relative** offsets
> while `os88sym.linear()` answers flat addresses, so a walker that sorts one
> and searches with the other names every address and every name is wrong —
> adjacent bytes came back `menu_drop.poll`, `fpg_begin.shr`, `wm_db_b`,
> `fm_saycl`, which is the tell.
>
> The row also takes the machine over at `sch_idle_body.loop` rather than
> wherever `pause` lands, and reads its own register setup back before running
> — every line of that setup is a debug-server round trip, and a synthetic
> call is exactly as good as the registers it starts with.

`docs/plans/completed/HANDOFF-KERNEL-SIZE-P4.md` parked this row on a question: its default
machine wants the IBM 5150 27 OCT 82 ROM, which cannot live in this tree
(CONTRIBUTING.md §6), so it said *"re-run it if you have the 5150 CGA ROM
set"*. The ROM was supplied. Four runs, all `dskw_write_x never returned - the
machine is still running after 180s`:

| tree | ROM | |
|---|---|---|
| `8626120` | IBM 5150 | FAIL — in its chunk, and again on its own |
| `8626120` | GLaBIOS | FAIL — so the ROM is not the variable |
| `f8af49e` | IBM 5150 | FAIL — pre-pass |

The 180 is a **host** wall-clock bound inside `m.wait_stop`, not a guest
budget, so contention was the first suspect and is ruled out: the solo failure
had two other emulators up, not four. The hang is still open and still covers
`dskw_wdata.stg`; what closed is the question the row was parked on, and it now
has a reproducer needing no ROM anyone lacks.

## E3. A machine naming an IBM romset SILENTLY becomes `glabios_pc`

`tests/int0sweep.py`'s own description says this in as many words, and it is the
most under-weighted sentence in `tests/`: *"a machine naming an IBM romset
SILENTLY RESOLVES to glabios_pc when the ROM file is absent, so the handful of
rows that ask for one were not testing it either."*

Nine rows name a non-GLaBIOS machine; **four are registered** — `int0sweep`,
`fillpat`, `icoclip`, `dskwstage` — and none had ever run on the ROM it asks
for. Why that matters is in the same description: on an IBM ROM the INT 0
vector masks the 8259 and IRETs, so **one divide overflow anywhere is a dead
machine**, where GLaBIOS's handler leaves the PIC alone and the same fault is a
wrong clip index the session survives. `wm_ttl_rect` spending BX under
`wm_clip_occl` locked an IBM machine hard and passed every GLaBIOS row.

With the ROM in place: `int0sweep` **ok, 199.0 s** — a broad UI session with
INT 0 armed, on the kernel 745 bytes lighter, and nothing fired. `fillpat` and
`icoclip` re-run and pass. `dskwstage` is E2.

**CLOSED — `docs/plans/SOAK-PARALLEL.md` §5.** The audit was taken row by row and
**no registered row made the case**: `fillpat` and `icoclip` call a primitive
through the debugger over memory they zeroed themselves, `int0sweep` traps INT
0 with a breakpoint that fires before any ROM handler runs, and E2 above
already measured `dskwstage`'s hang to be identical on both ROMs. All four are
on the GLaBIOS twins now and all four pass. `os88marty.machine()` resolves the
name, `assert_rom()` ends the silence, and `tests/unit/t_machines.py` in the
FAST tier refuses a row that names an IBM machine directly. Two of the nine
(`brclick`, `brscroll`) were testing the path `roms/ibm5150`, which is not the
ROM's filename, so their IBM arm could never be taken on any box at all.

The original diagnosis, which was right:

**The hazard is the silence**, and it survives this entry: nothing fails, and
nothing in the output says which ROM ran. A row that means the IBM ROM should
assert it got one.

## E4. `dispcheck` indexed word 11 of a record that is now 16 words long — FIXED

The pass's own, found by the soak and fixed in it (`662b429`). Pass 3's B2
batch took `[vid_strm1]`, `[vid_rpara]` and `[vid_rend]` out of the
per-display run — `VID_CTX_W` 19 → 16 — and `tests/dispcheck.py` still indexed
that run by hand, so `vid_rseg` (word 11 → word 10) became `vid_cwm1` and the
row asserted the two displays *"render into"* 027F and 02CF: 640−1 and 720−1,
printed as segments. On a two-card machine that reads as the software renderer
pointed at the wrong memory.

The kernel was right throughout — `vidsel.inc` asserts the run's length **and**
`vid_cw`/`vid_ch`'s place in it, and `os88geom` mirrors `VID_CTX_W`, so
`VID_CTX_SZ`, `VID_CTX_VX/VY/KIND` and the thirty scripts importing them
followed with no edit. This script imported them too, and then indexed *inside*
the run with literals, which nothing mirrors.

**Its own guard could not see it.** `run != NWORD` compares the symbols'
distance against the mirror; both moved together, so it passed. A literal index
inside a correctly-sized run is invisible to a check on the size. Derived now —
cw/ch off `VID_CTX_CW`/`VID_CTX_CH`, rseg off its own symbol the way
`tests/dispcold.py` always has — and the live block is `NWORD` words at both
ends. It was the only script indexing the run by hand; every other reader of
`vid_ctx` takes its offsets from `os88geom`, and `dispsize.py`'s bare 0/2/4/6
are a `wm_natr` **rect**, not this record.

## E5. `weavegame`: PONG runs ZERO frames, and it has never run here before

> **ANSWERED, and every one of the six was the ROW reading the wrong image.**
> `weavegame` now passes 11 of 11, at 17.7 fps and 1.00 gfx calls a frame, on
> the same machine and the same bundle. Nothing in `apps/weave` changed.
>
> `find_modules` scanned the heap for WEAVE.WSM's six-byte stamp and its last
> sixteen bytes, and the caller then kept every candidate whose `WSS_CVSEG`
> was non-zero — *"a real one has been WRITTEN, and a cached copy still carries
> the file's zeros"*. **Two images answered that**, and the one the scan
> reached first was not a module at all:
>
> | | RUN | SLEEP | CVSEG | WIN | NSPR | BLITS | FRAMES | OVF |
> |---|---|---|---|---|---|---|---|---|
> | seg 2b80 | 1 | 256 | 0202 | 0c01 | 8 | 770 | 7693 | 15 |
> | seg 3500 | 0 | 1 | 3640 | 0000 | 3 | 1 | 0 | 0 |
>
> Every number in the first row is impossible — a canvas claim below the heap,
> 770 blits against 7,693 frames, eight sprites in a bundle that declares
> three — and every number in the second is PONG's, down to a canvas claim at
> `3640`, which is `3500` plus the module's own 5,087 bytes. **So the table
> below is a reading of garbage, and its four PASSES are too.** `ovf = 15` was
> not an input overrun, `sleep = 256` was not a sleep, and `cpu.y` was a cell
> nothing ever wrote — which this entry already suspected in its last
> paragraph and drew the wrong conclusion from.
>
> **What separates them is the CODE, not the state.** A live module differs
> from the file only from `WSM_H_STATE` onward; that is where its writable data
> begins and a flat image has nothing else to write. Diffed whole:  seg 3500
> differs in 59 bytes and every one is at or above 3676, seg 2b80 in 707 with
> **589 of them BELOW it, from offset 3584** — a 512 boundary, so it is disk
> sectors, which is the floppy cache this entry's own docstring describes. It
> matched at the tail only because the sectors past it had not been reused.
> `find_modules` compares the whole code region now.
>
> **The lesson is not about Weave.** A scan that identifies a claim by a
> SIGNATURE has to identify it by something a stale copy cannot have, and
> "a field that is zero in the file" is not that — anything can write a byte.
> The intact-code test costs one 3.6KB read per candidate.

Six of its ten checks fail, **identically at `f8af49e` and at HEAD** — same
checks, same reasons, same numbers — so it is pre-existing and nobody's
regression. `tests/weavegame.py` is byte-identical at both ends and `apps/weave`
differs by one line of `wcanv.c`.

**Why it is new information anyway:** this row has never had a verdict in this
container. B4 above records it *failing where nine siblings skipped*, for want
of `build/WEAVE.WSM`, and what pass 2 fixed was its **registration** —
`needs=("marty", "cc")` — not the row. There was no C toolchain here until pass
3's last lane installed one, so B4's fix converted a false failure into a skip
and the skip is all anyone has seen since. The first real run is this one.

What it reports, on `os8088_5150_cga_gla`:

| check | why |
|---|---|
| exactly one BOUND module | found 2 bound of 2 images (WEAVE-SPEC 1.2.2 reads it once at open and keeps it) |
| 18 fps asks for one frame a tick | `sleep = 256` |
| frames ran | **0 frames** |
| the computer's paddle moved | `cpu.y` unchanged over 0 frames |
| the staging ring dropped nothing | **`ovf = 15`** — WEAVE-SPEC 6.10.6's input-overrun counter |
| the palette is on where the pen is read | `colored = 5` on a **cga** adapter, want 0 |

Three of those are worth separating out.

**`0 frames` and `sleep = 256` are one fact, not two.** 18 fps wants one frame a
tick; 256 is what a sleep argument looks like when nothing sensible reached it.
The paddle row follows from the frame row by construction — the test says so
itself — so six failures are really about four independent things.

**`ovf = 15` is an INPUT OVERRUN**, which is one of the three defects
PERFORMANCE.md says an emulator cannot show and this project has been bitten by
repeatedly. It is worth more than the frame count: a ring that drops fifteen
events is a class of bug no screenshot finds.

**`colored = 5` on CGA is both halves of WEAVE-SPEC 9.2.1** — the palette read
on a planar adapter, and not read at all on a 1bpp one. PONG declares
paper/ink/color, so the row is asking a fair question of a bundle that opted in.

**Not investigated further**, deliberately: this is the WEAVE runtime rather
than the kernel, pass 3's soak was validating a size change, and the row is
identical at both ends of it. It is written down at this length because the
NEXT person to install a C toolchain here will meet it cold, and because
`ovf = 15` should not wait for someone to notice it twice.

**One caution about the corroboration.** `cpu.y` reads 1782 at HEAD and −15352
at `f8af49e`, over 0 frames both times. That is not two different behaviours —
it is the same behaviour reading a cell nothing ever wrote, which is itself
evidence that `onTick` never fired. Do not bisect on that number.

## E6. `weavepack`: 18 of 23 checks, and it is NOT the flake D2 recorded

> **ANSWERED: the row built a 1.44MB floppy for a machine with 360KB drives.**
> `weavepack` now passes 19 of 19. One line of `tests/weavepack.py` changed —
> `--size 1440` to `--size 360`.
>
> `MACHINE` is `os8088_5150_cga_gla`, and every 5150 and 5160 profile in
> `tools/martypc/configs/os8088_machines.toml` carries the
> `pcxt_2_360k_floppies` overlay. **The failure looked nothing like a geometry
> error, and that is the whole of why this entry exists.** A 1.44MB layout is
> 18 sectors a track, so the boot sector, the FAT and the root directory all
> sit in the first two tracks and read PERFECTLY: the Disk window opens, the
> listing is complete, `entry("LOOM.O88")` finds it, `scroll_to` walks to it.
> The first thing that asks for sector 10 of a track is the package LOAD — and
> a 360KB drive cannot step there.
>
> So the double-click produced **no window, no toast, no instance record, and
> `ld_status` still 0**, nine times over. That reads exactly like a
> double-click that missed or an association that did not resolve, which is
> what the row's own message says and what this entry spent three runs on.
> It is neither: `ASSOC.DAT` was correct, and **`LOOM.O88` opened directly
> fails identically**, which is the control that settles it in one run and
> which nobody had taken.
>
> Two things below stand, re-read in that light. The cascade is real — three
> retries of a load that cannot succeed, then a Disk window that will not come
> to the front — which is why *"the first failing project differs between runs
> while the count does not"*. And the B5 host-speed theory was correctly
> rejected here; what this entry could not do was name the alternative, because
> the row reported a host statement (*"os88mouse refuses a double-click…"*)
> about a guest fact.
>
> **The general lesson is the disk, not the row.** Nothing in this tree checks
> that a scratch image's geometry is one the machine's drives have, and the
> failure it produces is silent and late. `weavesmoke` boots the same machine
> off `build/weave360.img`; `weavepack` was the odd one out for its whole life.
>
> It fits 360KB with room to spare: 131 of ~354 clusters for the inputs, plus
> seven `.WAB` outputs of about a cluster each.

**Three runs, three identical results — 5 checks passed, 18 FAILED:**

| tree | box | |
|---|---|---|
| `8626120` | loaded (I was running other work in its window) | 5 / 18 |
| `8626120` | verified idle — nothing else on the box | 5 / 18 |
| `f8af49e` — before the pass merged | idle | 5 / 18 |

989.7 s at the base against 976.1 and 999.3 at HEAD. **Pre-existing, and not
this pass's.** `tests/weavepack.py`, `apps/loom/` and `apps/cc/` are
byte-identical at both ends of the pass.

**D2 is superseded, and the difference matters.** D2 recorded this row as a
FLAKE — *"17 of 18 checks passed, then passed alone at the same commit"* — with
B5 as the mechanism. What it does now is fail nearly everything, repeatably, on
an idle box. A row that fails 18 of 23 every time is not the row D2 describes,
and treating it as "the known weavepack flake" would have filed a hard failure
as noise. That is the whole reason this entry exists.

Every failure is host-side by the row's OWN account, which is what makes it
confusing:

* `LOOM opens on the double-click` → *"os88mouse refuses a double-click whose
  two presses straddled the kernel's 9-tick window — **a statement about the
  HOST**, retried three times and then reported"*
* `scrolled PAST entry 12 — the list moved under us`
* `waited 25s for LOOM's window for TSHEET and it never happened. The guest is
  still running, so either it is slower than the limit or the condition is
  asking about the wrong thing`

So the row's diagnosis is B5, and B5 is a **host-speed** story — yet the result
does not move with host speed. It is identical loaded and idle, and the loaded
run was 23 seconds FASTER. Whatever this is, the row's own explanation for it
is not supported by the two runs that were designed to test it.

**The failures also cascade, so 18 is not 18 independent facts.** A project
whose double-click never opens LOOM is never packed and never closed; the next
project then meets an instance that should have gone away, and
*"each project is one instance and they cannot all be open at once — a 640KB
machine has room for about four (WEAVE-SPEC 1.4)"* fires for a reason created
one project earlier. Note that the first failing project **differs between
runs** (`FORM` idle, `SHEET` loaded) while the count does not — consistent with
one root cause plus a deterministic cascade, and inconsistent with 18 separate
defects.

**What was NOT done, and why.** Not root-caused: it is the Weave family rather
than the kernel, it is identical at both ends of a size pass, and the honest
next step is a single project driven by hand with the sidebar photographed —
D2's own fix proposal (`m.disk(reset=True)` before `^P`, then poll `m.disk()`)
is still the right instrument and is still unbuilt. Whoever takes it should
start from **one** project, not nine, and should not begin from B5.

**It is the second Weave row in this section with the same history**: E5's
`weavegame` and this one had never produced a verdict in this container,
because there was no C toolchain, and B4/D2 record what was seen instead.
Installing one did not break them. It made them legible.

### The pass-3 tally

| | |
|---|---|
| soak rows attempted | 200 — the whole tier as the suite then stood |
| ok | 196 |
| FAIL | 4 |
| SKIP | **0** |
| not run | **0** |
| **regressions in kernel behaviour** | **0** |
| pre-existing, established at `f8af49e` | 4 — `dispsize`, `dskwstage`, `weavegame`, `weavepack` |
| found and FIXED in the pass's own work | 1 — `dispcheck`'s stale in-run index (E4, `662b429`) |
| rows whose FIRST EVER verdict here this was | 2 — `weavegame`, `weavepack`, both waiting on a C toolchain |
| **classifications this queue got WRONG and corrected** | **+2** — E1's bisect (void: single samples, a conflated exit code, and commits with no shared base) and `weavepack`'s contention theory (mine, disproved by an idle re-run) |
| entries SUPERSEDED by pass 3 | 1 — D2, which called `weavepack` a flake |

**Pass 2's lesson held.** Not one of these four was a check that failed
honestly on first reading: two were rows that had never run, one was
intermittent and read as a bisectable regression, and one was a hard failure
sitting under a queue entry that called it a flake. The defence that worked was
the same one — run it again, on purpose, with one variable moved.

---

# The tally

| | |
|---|---|
| rows run | 235 (pass A) + 5 (rate rows, serial) + 13 (C toolchain) |
| failures investigated | 15 |
| **regressions in kernel behaviour** | **0** |
| host-timing artefacts (B5) | 3 — `dispmine`, `tmowner` (both pass alone), `weavepack` |
| harness regressions, from a host-side commit (B1) | 3 — `hdboot`, `knobhd`, `blitcut` |
| pre-existing, identical at both ends | 4 — `trkscrl`, `dispcheck`, `dispcold`, `dispreboot` |
| missing artefact or registration (B4) | 3 — `weavegame`, `wireflick`, `fdlggrey` |
| a missing `no_saver()` call (B7) | 1 — `trkrate` |
| found by the pre-merge gate, not the soak (B9) | 1 — a leaked QEMU breaking `ps2mouse` |
| fixed during the pass | 3 — `deskbench`, and `weavegame`/`wireflick`'s registrations |
| **closed since, in this queue** | **15** — A1, A2, A3, A4, A5, B1, B2, B3, B4, B6, B7, B8, B9, C1, C2 |
| **left open** | **1** — B5, deliberately (it reaches 194 files and wants a full soak behind it, and its evidence weakened rather than strengthened) |
| **classifications this queue got WRONG and corrected** | **6** — `trkscrl` (A1, called in bold "the one genuine product defect" and it is a shadowed key in the test's own include), `dispmine` (A5, called contention on one passing re-run), `blitcut` (B2, bisected to a host-side commit and it is the size pass's own ladder), B6's own mechanism, **C1's mechanism** (it blamed N, which two measurements clear, and the table that appeared to prove it is one kernel run twice — the report printed the field that says so), and **A3's arithmetic** (it divided 6.5 µs by 756 µs, which is not this call's cost) |
| **product defects found in shipped software** | **0** |

**The one lesson, and it is one lesson.** Not one of these was a check that
failed. Every expensive hour went to a check that **passed for the wrong
reason**, or failed while naming the wrong cause: a benchmark reporting 0.0 ms
for a window that visibly moved, a layout check saying "rebuild" about a
build that was current, a bisect that skipped hangs while hunting a hang, a
baseline script that would have read an argparse error as a result. The defence
that worked, every time, was to break the thing on purpose and confirm the gate
noticed.

---

# F. What the parallel-soak work found

`docs/plans/SOAK-PARALLEL.md` is the record. Its §1 is the finding that matters to
every entry above: **contention does not make a row slow, it makes it less
thorough** — 118 waits in each arm at the same median HOST cost of 2.2 s and a
GUEST cost of 7.3 s against 5.9 s, up to −37% per script. So the wall times
every classification run in this file went looking at could never have shown
anything, and "it passed alone" was never going to be a satisfying diagnosis.

## F1. `blitp` and `blitpair` fail identically at the base — NOT the harness change

> **BOTH FIXED, and they were two unrelated things that the shared verdict
> hid.** The table below is right about what it measured — each row fails at
> HEAD and at `af1f2e0` — and "pre-existing" turned out to mean two different
> stories.
>
> **`blitp` needed an artefact nobody built.** Its own description ends
> *"Needs `make bench`"*, `all` does not build `build/gfxbench.o88`, and the
> row said so only to the reader. A bisect worktree runs `make`, which is
> `all`, so the artefact was missing at both ends — which is exactly how a
> missing prerequisite acquires a "pre-existing regression" verdict. With it
> present the row passes: **256 plane-rows, 2,048 bytes, all as given.** It
> now declares `wants=("build/gfxbench.o88",)` so the runner builds it up
> front. B4's shape again — the suite modelling tools rather than artefacts.
>
> **`blitpair` was reading a fixture that means something else.** The quoted
> message is this row's, and A7's fallout is the cause: when
> `build/OS8088.GIF` turned out to be two colours, the row's `--gif` default
> was pointed at `dispapps.colour_gif()` on the reasoning that *"the pixels
> are identical either way (two unused colour-table entries)"*. **The pixel
> INDICES are identical; the colours are not.** `colour_gif` inserts its two
> entries at offset 13 — the start of the global colour table — so indices 0
> and 1, the only ones any pixel carries, become `0xAA0000` and `0x0000AA` and
> the picture's own black and white move to 2 and 3.
>
> That prepend is **deliberate and must stay**: appending instead leaves the
> file one bit deep, `pt_fmtpick` calls it colourless, Paint takes a 1bpp
> canvas, and `paintrow` and `paintplan` report *"no gfx_blitp - the canvas is
> not planar"* — those rows losing their subject. What it means is that
> `colour_gif` is **not a drop-in for OS8088.GIF for any row whose oracle is
> "the 1bpp canvas equals the file's own bitmap"**: SPEC.md 39.4 reduces both
> new colours to the same class, so the guest drew a solid block where the
> file alternates. `row 0 want 10101010 / got 11111111`, 20,327 pixels — and
> that is indistinguishable from `sw_blit_row` reading its tables through the
> wrong segment, which is the one defect this row exists to catch.
>
> It takes `build/OS8088.GIF` itself now and reads **0 pixels differ**, and
> `paintrow`/`paintplan` are untouched and still green. A second defect
> surfaced underneath: the row asserted `(bw, bh) == (iw, ih)` on the blit it
> caught, and the load path emits one 466x1 call per row through that same
> primitive — so it reported *"the canvas is 466x1 and the picture 466x110 -
> Paint cropped it"* about a Paint that had cropped nothing. It takes the
> ORIGIN from the blit and asks Paint for the SIZE (`[pt_cw]`/`[pt_ch]`),
> which is what actually answers whether SPEC.md 42.6.5 letterboxed it.

Found while checking that the guest-clock waits had broken nothing. Both fail
on a dithered ground read back solid:

```
canvas 466x110 on 640x200: 20327 pixels differ from the file
row 0 want 10101010101010101010101010101010
row 0 got  11111111111111111111111111111111
```

The protocol at the top of this file, both steps:

| | `blitp` / `blitpair` |
|---|---|
| in a 3-wide lane | FAIL |
| alone on HEAD | FAIL — so it is not contention |
| alone at `af1f2e0`, the commit before the work | **FAIL, 20327 pixels, byte for byte the same message** |

So it is **pre-existing and nobody's regression**, and it is open. It is
adjacent to A7 — the picture fixture — but it is NOT A7's symptom: A7's six
rows said *"no gfx_blitp, the canvas is not planar"*, and this one gets a
canvas and reads every pixel of a 50% dither as lit. One for the pass that
fixes tests.

**The base worktree is worth keeping the recipe for**, because it took four
minutes and settled the question outright:

```sh
git worktree add /tmp/base-soak <base>
mkdir -p /tmp/base-soak/build
ln -sfn <tree>/build/martypc /tmp/base-soak/build/martypc   # pinned, identical
ln -sfn <tree>/build/cc      /tmp/base-soak/build/cc        # by construction
cd /tmp/base-soak && make && python3 tools/os88test.py soak -k <row>
```

Only those two may be shared. A shared writable DISK is what contaminated
pass 2's first bisect.

## F2. `msegnomem`: its first verdict, and it is a failure

**The row had never run.** Its own usage line is `make mseg && python3
tests/msegnomem.py`, `mseg` is not in `all`, and the row never built it — so it
died in `os88marty.launch` on a missing `build/mseg.o88` on any tree where
nobody had typed that by hand. B4's shape: an ABSENT gate reading as a failing
one. Building `mseg` into its own tree (`docs/plans/SOAK-PARALLEL.md` §8.6) is what
finally got a verdict out of it, and the verdict is:

```
the toast says 'Not enough memory' and should be 'Load failed'
```

Its own comment is the argument for the assertion, and it is a good one:

> `op_load` does put up `Not enough memory` — but step 10 toasts `[ld_status]`
> over `fm_stattab` for EVERY outcome including `LD_EABORT`, and it runs after
> the entry proc returns. So a package that refuses from its entry is always
> overwritten by `Load failed`, and asserting the package's own string here
> would be asserting a race.

So one of three things is true and this entry does not pick between them:

1. SPEC.md 21 step 10 no longer toasts over `LD_EABORT`, and the kernel is
   wrong;
2. it does, and the row reads the toast **before** step 10 runs — in which case
   the row is asserting the very race its comment says it is avoiding;
3. the expectation was written from the spec rather than from a run, and has
   never been true.

**(3) is the one to rule out first**, because the row has produced no evidence
in its whole life. The other assertions around it pass — `MSEGBIG` does come
back `LD_EABORT`, which is the substance — so this is about the message a user
sees, not about the refusal.

Not this work's regression by construction: there is no earlier verdict to
have regressed from.

## F3. `gfxlk`'s control check is INTERMITTENT, and it is not contention

> **FIXED, both ways this entry proposes, and the diagnosis below is right.**
> `gfxlk` is 6/6. The control was measuring a coincidence, and the reason it
> had become one is **SPEC.md 7.4**: the mouse ISR now DRAWS the arrow through
> a disk transfer (`NOCURDISK=1` restores the old freeze), so an arrival with
> the gfx lock free is a draw and not a deferral. When this control was
> written, 12.8.4's compare made the draw unreachable for the whole of a file
> operation, so every arrival in the window WAS a deferral and counting them
> proved the session had armed the widget. 7.4 inverted that on purpose, and
> the check was never revisited.
>
> Instrumented, six runs read **15-18 ISR arrivals inside the window with 0, 1,
> 0, 0, 1, 0 of them deferred**: the sum never varies and the deferral count is
> a coin. The control is now `gfx_aud_mv + gfx_aud_def`, which is what the
> file's own docstring asks for (*"a zero cannot be a session that never armed
> the widget"*) and is unambiguous under either kernel. **It is not the
> "≥0 deferrals" widening this entry warns against**: the assertion below it —
> `gfx_aud_gate == 0`, an ISR draw with the widget up — is untouched.
>
> The row also PROVOKES rather than hopes, the entry's other proposal: it waits
> for `[fpg_on]` and moves the hand while it is set, instead of firing a fixed
> 40 packets at a package load that sometimes finished first.
>
> `msegnomem` (F2) is the same shape one device along — an expectation written
> against a kernel that a later § deliberately changed.

`gfxlk` asserts five things about SPEC.md 12.8.4's cursor gate. Four are
"this did not happen" and pass. The fifth is the **control**:

```
[FAIL] the gate fired at all   0 deferrals - the control for the row below
```

It needs the race to actually OCCUR at least once — an ISR cursor move landing
while the gfx lock is held — or the four zeroes below it prove nothing. The run
that failed had **37 ISR cursor moves and 0 of them deferred**.

It first failed in a 3-wide lane, which reads like contention. Sampled, it is
not:

| | gfxlk |
|---|---|
| width 3 + two CPU hogs | 1 FAIL / 3 |
| idle, alone | **2 FAIL / 3** |

**Worse idle than loaded**, so `alone=True` would not have fixed it — and that
is the whole reason to sample both arms rather than only re-running the failure
alone. `curdisk`, sampled the same way in the same session, came out the other
way (2 FAIL / 4 loaded, 0 / 4 idle) and did take the flag; the two look
identical from a single failing run in a wide lane.

**What to fix is the control, not the tolerance.** A check that depends on a
coincidence occurring is a check that reports on the coincidence. Either the
row has to PROVOKE the collision — move the pointer while something holds the
lock, rather than hoping a sample lands there — or the control has to be a
different observation. Widening it to "≥0 deferrals" would delete the only
thing standing between this row and four zeroes that mean nothing.

## F4. `schacct` and `hdmove` — the two that looked like KERNEL defects, and neither is

Both were carried for most of a session as genuine product findings, written
down as such, and left failing on purpose. Both were the row.

### F4.1 `schacct`: 8.7% of the machine unbilled, and the leak was the read

The balance check reported the books accounting for **91.3%** of elapsed time
with the Task Manager open, against a `95..102%` window — and its own failure
text says what that means: *"Under 95% means a slice is being dropped"*. It
looked like exactly the defect SPEC.md 8.1.1's change-compare could plausibly
introduce, and it had the right shape for one:

| | balance |
|---|---|
| bare desktop, six 2-second windows | **99.9%** every time |
| Task Manager open, six 2-second windows | 91.9, 91.6, 91.6, 89.7, 91.6, 91.9 |
| Task Manager open, cumulative 2→12 s | 89.9 → 91.3, flat |

A steady **rate** rather than an end effect, invariant in the window length,
and specific to one window. Everything about it says "a slice is being
dropped".

**`cycles()` read 32 bytes and unpacked 8 slots.** `MAX_TASKS` is **14** and
`sch_cycles` is **56 bytes** — docs/plans/completed/STACK-SLOTS-PLAN.md took `SCH_PARTITION`
to twelve slices and this literal did not move with it. With the table read
whole the row passes at **99.6%, tasks [0, 1, 10]** — and task 10 is billed
**8.3%**, which is the missing time to the tenth of a percent.

**The lesson is the mirrored constant, not the arithmetic.** A kernel constant
copied into a test is what `tests/unit/t_mirror.py` exists to catch; this copy
was a *slice length* rather than a named constant, so nothing compared it. It
now comes from `os88sym.equates()`.

It is also a caution about a good check: the balance row is well designed and
its failure message is accurate about what a shortfall means. It just cannot
distinguish "the kernel dropped a slice" from "you did not read all the
slices", and the second is indistinguishable from the first in every statistic
the row prints.

### F4.2 `hdmove`: `[dsk_dseg] still names the old base`, about a pointer that did not

The row's check 4 failed with *"every listing read goes to the wrong segment
until the next mount"* — a pointer left naming freed memory, which would be a
serious kernel defect. `[dsk_dseg]` read `1860`, the `.lowbss` floor, and the
claim had moved `5e80 → 3b40`. **`1860` is neither base**, and the message was
about a value it never held.

`dsk_dseg_reloc` follows the pointer if and only if it names the block being
moved, and its own comment says why: *"the second equals the first only while
that volume is the one mounted"*. The row's sequence puts a mount between the
two: the only way it has to force a compaction is to LAUNCH heapfrag out of
the B: window, and **the launch mounts B: on its way to the file**, before
heapfrag's big claim can fire anything. So the order is mount, then compact,
and the pointer correctly names the floppy's listing when the relocation runs.

The row already had a `NOT EXERCISED` branch for exactly this, and it was
keyed on the wrong sample — `[dsk_dseg]` as it stood when C: was opened,
minutes and one mount earlier. Sampling it immediately before the compaction
was **not enough either**, because the mount happens inside the launch: the
honest formulation is that the only failure this check can see is the OLD
base, and any other value is a mount having re-pointed it, which is correct
and none of this check's business. Check 4b — *no word anywhere still holds
the old base* — is the one that is never vacuous, and it passed throughout.

**What both have in common** is that the row's own diagnosis was confident,
specific, and pointed at the kernel. `schacct` named a dropped slice;
`hdmove` named a stale pointer. Neither existed. A failing row is evidence
that the row and the kernel disagree, and it names the suspect it was written
to catch — which is worth remembering before the next one is written up as a
product defect.

## F5. The soak that found five, and four of them were the same defect

The tree with F1–F4 in it ran the whole tier in **1:51:52 — 267 reported, 262
ok, 5 FAIL, 0 SKIP**, against the previous run's **23 failures in 5:22**.
Every one of those 23 came back green. The five are a different set, and four
of them are one idea.

| row | previous run | why |
|---|---|---|
| `paintpack` | ok | mine: F1 changed `blitpair`'s `--gif` default |
| `saver` | ok | a wait that could observe the PREVIOUS session |
| `dmgcull` | ok | a zero from one pass, read as "did not happen" |
| `tmrepair` | **FAIL** | the same, and it never passed here |
| `dispprefer` | FAIL | `[fm_vinst]` is allowed to be 0 |

### F5.1 The common defect: a budget that is not the guest's

`tests/dispcells.py`'s `serve` has warned about this since it was written —
*"how often it happens depends on how many breakpoints are armed and how busy
the guest is"*, and *"treat a zero from a single pass as `not seen`, never as
`did not happen`"*. Three of the five are that warning coming true, and the
mechanism under it is one line:

```python
for _ in range(rounds):
    if not serve(...):
        m.advance(frames=frames)
```

**A serviced stop costs a round.** So the guest gets `rounds` minus however
many breakpoints fired — less time exactly when more is happening, and how
much less is a property of the box's load. That is not a guest-anchored budget
at all; it only looks like one.

`tmrepair` is the clearest case: it read **0 `wm_su_occl` calls with 0
subpixels differing** — the repair itself perfect, the call that performs it
never observed. Counting only the rounds that ADVANCE makes the window a fixed
amount of the machine's own time, and it then reads **1 call, three runs of
three, in a three-wide lane**. That row has never passed in this container
before.

`dmgcull` failed on `0 cells outside the mover's rect` in the lane and read 27
alone, in the same build — two readings of the same machine, so the zero was
the pump. It retries the gesture now, which is what `serve`'s docstring asks
for, and reads 27 three runs of three. The retry did not have to fire in any
of them.

`saver` is the same family one step out: every wait in it was `time.time()`
and `time.sleep`, and what the saver waits for is `ss_idle` TICKS. It also
waited for `blk_on == 1` **without first proving it was 0** — and the previous
iteration leaves it 1, so the wait could return on a session that was already
running. Measured, exactly one of its two fallbacks failed each time and
*which one* changed between the lane and a solo run, which is a race and not a
saver. Guest-clock waits plus "wake it and confirm it is off, then arm" is
3/3 three-wide.

### F5.2 `dispprefer`: `[fm_vinst]` is allowed to be 0

`os88ui.open_drive` waited on the front window's own state and then returned
`disk_window()` — the ACTING window, resolved through `[fm_vinst]`. Those are
different questions, and the kernel is entitled to answer the second with
"nobody": `fm_vp_set` stores the window's OWNER instance and **0 for an
unowned one**, which kernel/files.inc says at the store. `dispprefer` reached
that state in its seventh section, with seven windows up and four of them file
windows, and the verb raised *"no Disk window is acting"* about a wait that
had just succeeded on a Disk window showing B: at its root.

The verb returns the window it confirmed now, and `disk_window()` falls back
to the front window when nothing is acting — the docstring's warning is that
front and acting are different, and with nothing acting there is no other
answer to disagree with.

### F5.3 `paintpack`: one default cannot serve both adapters

F1 pointed `blitpair`'s `--gif` default at `build/OS8088.GIF`, because
`colour_gif`'s derived picture reduces to one class on a 1bpp adapter.
`paintpack` runs the same row on a **VGA** machine, where the framebuffer
carries real colour and `blitpair`'s red channel separates `0xAA0000` from
`0x0000AA` — so there the derived picture is the right one, and it is what
`paintpack` puts on the disk it builds.

Both are true and neither is a default. `paintpack` passes `--gif` now, which
is what `blitpair`'s own comment asks for: it is the caller that builds the
disk.
