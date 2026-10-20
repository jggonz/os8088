# Handoff: the call sites that needed stop detection and did not have it

**Nothing in this list has been changed, and this document changes nothing.**
It is a survey taken immediately after the fix below landed, for the session
rebuilding the harness UI so that it can drive the interface *and* hold a
breakpoint — because §3 is that session's own subject, and §1 and §2 are in
the same files. Two sessions editing them from opposite ends is the thing to
avoid, so this says what is where and what each site is exposed to, and leaves
the editing to whoever gets there.

Taken at `e6e1b94`, against the whole of `tests/` and `tools/`.

## 0. What landed, in one paragraph

`state` could not tell *"my resume has not landed"* from *"it landed, went
round, and stopped again"* — both read `"breakpoint"` — so every client
invented its own test, and the one that reached for the **instruction
pointer** cannot work at all: a breakpoint that fires repeatedly fires at the
same address every time, and an `int 08h` breakpoint on a plain desktop was
measured at 59 consecutive stops carrying 59 identical `flat_ip`s. The debug
server now carries `stops`, a sequence number counting every entry into a
stopped state, and `run` answers `resumed_from`, that count as the resume
found the machine. `Marty.go()` hands back the mark; `wait_stop(since=…)`
defaults to the last `run`/`go`/`advance`/`step` on that object. A status is a
new stop exactly when it is stopped and `stops` is above the mark.
`tests/martyresume.py` is the row, docs/MARTYPC-DEBUG.md the manual.

**`make marty` is required** — the client falls back to `cycles` against an
older binary, which is correct for the ordinary case but is not the fix.

## 1. Class A — hand-rolled resume-and-poll. DONE

**All thirteen are converted to `bp_trace` and every row is green.** What
follows is the survey as it was taken, with the outcome of each row beneath
it: three of the eight predictions were wrong and the two biggest findings
were not the ones ranked first, which is the usual shape and the reason the
verdict column is worth keeping rather than deleting with the work.


Thirteen sites in eight files poll `status()["state"]` or `stopped()` in a
loop of their own and resume by hand. They do not go through `wait_stop`, so
nothing above reaches them; each one is its own copy of the bug. Ordered by
what a wrong count does to the row's verdict, because that is what decides
whether it is worth touching.

| site | shape | what a spare stop does |
|---|---|---|
| `tests/dispfreeze.py:60-66` (`ui_passes`) | bare counter, no address filter | **inflates a LIVENESS number.** The count *is* the claim that the UI task is running, so a duplicate reads as a machine that is alive when it is frozen — a false pass in the direction the row exists to catch |
| `tests/paintrate.py:127-137` | bare counter, mem breakpoint on `gfx_lock_own` | **inflates a RATE** that is then compared against `PAINTRATE_MIN`; a false pass |
| `tests/paintwalk.py:141-160` | counter, discriminates two addresses via `regs()` | double-counts a chord or a rect, both published |
| `tests/paintblank.py:150-172` | two counters (`n` decoded, `nf` bands), address-filtered | inflates whichever bucket the stale IP names; the row asserts a floor and a zero |
| `tests/clipkeep.py:66-82` (`drops`) | `stopped()` + `advance()` interleaved | `stopped()` is TRUE at the advance's own pause (the `bp_count` defect one), and the `here != at` filter turns that into an early `break` — so this one **under**-counts |
| `tests/tmrepair.py:245-255` | the same shape, near enough line for line | as clipkeep |
| `tools/os88span.py:94-108` | `run()` then an inner poll that `break`s on the first `"breakpoint"` | takes the SAME stop again as the next sample: same cycles, same IP, a duplicate row in the span trace. Its `budget` is measured from a `t0` set once outside the loop, so it is cumulative rather than per-stop — a second bug in the same six lines |
| `tools/winmove.py:76-98` | as os88span | as os88span, but it dedupes on a `cycles` delta by hand (`QUIET`), which is the right clock arrived at the hard way. **Its docstring already documents the inverse defect** — a loop that opens with `run` resumes past the stop it was handed and never sees it, which "cost a whole diagnosis" |

Two of these are instruments rather than rows (`os88span`, `winmove`), which
is worse and not better: an instrument's wrong answer is read as a finding.

### What the conversions actually found

Baselines and results are one run each on this container, same build, same
disks. Every row passed before and after; the numbers are what moved.

| row | before | after | what it means |
|---|---|---|---|
| `clipkeep` | PARTIAL **4** drops | PARTIAL **27** | The under-count was not marginal: `stopped()` is true at the advance's own PAUSE, the IP read there is the pause point, and the `!= at` arm broke the inner loop on the FIRST advance of every round — so each round sampled `frames // 8` instead of forty of them and **the row ran on 104 frames where it meant 880**. WHOLLY stays 0 across the wider window, so the claim it makes is now made over ~7x the evidence |
| `tmrepair` | KEPT **0** across 8 polls | KEPT **0** | Same defect, same 8x, and the answer does not move — which is the point: a zero from a window nobody looked at and a zero from a window that was watched read alike, and only one of them is a test |
| `paintrate` | **537** samples/s | **895, 901** over two runs | The biggest surprise, and it is not a dedupe: the row's own docstring records the fixed kernel measuring **882**, so the hand-rolled pump was reading a third low and the converted one lands on the documented figure. One thread cannot both pace a nudge schedule and pump a breakpoint that fires 900 times a guest second. It is also 28s where it was 44 |
| `paintblank` | 0 DECODED, **17** bands | 0 DECODED, **18** bands | One band hit per run that `advance()` swallowed: it stops AT a breakpoint and returns there, and the `m.run()` on the next line resumed it unclassified. The headline `0 DECODED` is unchanged, which is the assertion that matters — but it was being made by a counter that could drop a hit |
| `paintwalk` | 9 chords | 9 chords | No change, and the exposure was a false FAIL rather than a false pass: a stop reported twice re-reads the same `pt_wx`/`pt_tox`, appends a chord whose end is its own start, and that chord fails the landing test. An intermittent red on a row about ink going where the hand did not |
| `dispfreeze` | 180–191 passes | comparable | **The survey over-ranked this one.** The verdict is `n == 0` and a duplicate needs a real pass to duplicate, so it could never manufacture an ALIVE from a frozen machine. Converted for the count beside it and for the twelve lines |
| `tools/os88span.py` | 82.20 ms | **82.19 ms** | Measurement identical, so the duplicate row was latent rather than active on this machine. The cumulative budget was not latent: the old collector sat out its whole 200-second allowance after the last hit, and the converted one returns 2 seconds after the operation goes quiet |
| `tools/winmove.py` | 66 calls / 199.1 ms | **66 calls / 199.2 ms** | Identical, and the special case for "the first hit, which a loop opening with `run` resumes past" is gone rather than preserved — the trigger fires inside the block now, so the pump is watching before it is pulled |

The two that moved most were ranked fourth and sixth. What ranked first could
not have failed at all.

## 2. Class B — `run(); wait_stop()`. Fixed for free, and it mattered

**109 pairs in 46 files.** All of them are now measured from the resume rather
than from "any stop", so none can be answered by the stop that was already
there. No edit is needed anywhere in this class; it is listed because the
second session will read these files and should know they are done.

Where it was load-bearing, verified by reading:

* `tests/wdcaret.py:154-163` — asserts **an exact count of 1** (`a Right arrow
  makes ONE wd_walk`). This is the shape `tests/alertbtn.py` carried, which is
  what turned up the whole defect.
* `tests/uiblock.py:105-118` and `tests/schacct.py:158-170` — `hits_per_second`,
  twice, in two files. These feed the **published** SPEC.md 8.1.1 and 8.1.2
  numbers (1,134.6 passes a second against 17.7; 10.94% against 0.18%).
* `tests/schacct.py:173-186` (`resume_changes`) — appends a `sch_cur` sample
  per stop. A repeat stop appended a duplicate sample, which biases *"0 of 400
  switches changed `sch_cur`"* toward zero — the measurement the whole idle
  design rests on.
* `tests/skiespanel.py:241-247` — `wait_stop`'s answer is discarded and `SI` is
  read straight after it. A stale stop meant reading the **previous** cell's
  string and comparing it against this cell's expectation.
* `tests/paccman.py:180-192` — frame/GFX-call accounting, and the file that
  already documents this defect's third form in a comment: the timing loop
  left the guest stopped at `frame_addr`, the first `run` after `bp_exec`
  reported a call that was thrown away unclassified, and *"both published
  columns were a third of a call low"*. `bp_count` no longer counts the stop
  it was handed; this hand-rolled loop still anchors by hand, and does it
  correctly.
* `tests/skiesrwy.py:139-148`, `tests/dskwstage.py`, `tests/atkey.py` — counting
  and staging loops of the same shape.

## 3. Class C — the input half, which is the other session's subject

**41 files arm a breakpoint AND drive input rawly** (`m.mouse`, `m.key`,
`mo._pk`, `mo._edge`) instead of through `os88ui`/`os88mouse`'s confirming
verbs; 32 of them do not import `os88ui` at all. This is not laziness and the
files say so:

> `tests/paintblank.py:139-141` — *"os88mouse proves each edge by polling,
> which a guest stopped at a breakpoint never reaches — so the helper parks
> the pointer and the packets go in by hand."*

> `tests/paintcull.py:150-153` — *"The breakpoint is pumped from this thread
> while a daemon does the click: os88mouse proves each button edge by polling
> `mouse_btn`, and a guest stopped at a breakpoint never advances far enough
> to answer."*

So the confirming layer and the breakpoint are mutually exclusive today, and
the workaround is a daemon thread doing the gesture while the main loop pumps
the stops — which is `bp_count`'s shape, hand-rolled per file, and where its
`advance()` defect came from. The top of that list by how much raw input it
carries: `wdscroll` (22), `wdtype` (21), `wdenter` (20), `wdcaret` (16),
`skiesfleet` (13), `atblit` (10), `os88span` (9), `atkey` (8), then
`fmcommit`, `paintbig`, `skiesease` (6 each).

**A verb that can confirm itself against a stopped guest is what removes the
daemon thread**, and with it every hand-rolled loop in §1 that exists to pump
one. The two jobs meet here: §1's sites are pumping loops, and a UI layer that
can hold a breakpoint is what they would be rewritten against.

## 4. Overlap — the files both jobs would touch

`paintblank`, `paintcull`, `paintrate`, `paintwalk`, `clipkeep`, `tmrepair`,
`os88span`, `winmove`, `wdcaret`, `paccman`, `skiesfleet`. Eleven files carry
both a hand-rolled stop loop and hand-rolled input. **Nothing here has been
edited**; if the UI work lands first, §1 mostly dissolves into it, and what is
left is `os88span` and `winmove`, which are instruments with no UI in them at
all.

## 5. What is NOT on the list, having been checked

* `tests/gifdrag.py:140-142` compares `instructions` across a `sleep(2)` to
  detect a `cli`/`hlt`. That is the one honest use of that field — a halted
  CPU burns cycles and retires nothing, so `cycles` would be the wrong test
  here — and no breakpoint is armed at that point, which is the condition
  under which the count is exact. Leave it.
* The 51 `advance()`-near-a-breakpoint sites are mostly settle waits with no
  breakpoint armed at the time. Only `clipkeep` and `tmrepair` interleave the
  two, and both are already in §1.
* `tests/dtfield.py:229` and `tests/schacct.py:133` read guest state with the
  machine deliberately stopped, which is the point of those reads rather than
  a wait. Leave them.
