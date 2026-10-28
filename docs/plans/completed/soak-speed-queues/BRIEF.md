# Soak-speed agent brief (read fully before starting)

You are one of FOUR agents working IN PARALLEL in ONE shared checkout:
/home/user/os8088, branch `soak-speed` (cut from origin/elendilon). Read
CLAUDE.md's Testing section and docs/WRITING-TESTS.md §1 once. The goal: make
each soak-tier emulator row MUCH cheaper (the precedent: tests/tmowner.py,
commit b787057, ~450 s -> 41 s, by reading the guest state the page is drawn
from instead of pumping/settling the screen) WITHOUT making it less able to
fail. Owner's words: "Every time we optimize an individual test, it can often
drop 60-90% of its time ... focus, in depth, on each test."

## Your queue
Your rows are in the queue file named in your prompt (round 2: see the end of this brief) (family, row, declared
secs, script). Work top-down (largest first; when you open a family, do its
siblings while the context is hot - they usually share a library). Mark each
line you finish by prefixing it `DONE ` / `SKIP <reason> ` in that file so a
successor can pick up. Declared secs are often wrong in both directions -
measure.

## Per row
1. Read the row in tests/suite.py and its script (targeted reads, not dumps).
2. Measure BEFORE: `MAKELEVEL=1 OS88_WAITLOG=<scratch file> python3 tools/os88test.py soak -k <row>`.
   The waitlog names every wait with its call site and guest cost - that is
   the work list. If the row FAILS at base: record it (ledger, "fails at
   base: <why>"), fix only if it is plainly a test bug, else skip.
3. Find where the time goes and cut it. Typical wins: settle -> quiesce on the
   bytes about to be read / an os88ui verb that confirms; one boot instead of
   several (MartyPC snapshot()/restore(), or sharing a boot across legs);
   fixed sleeps -> conditions; pumping font_run/breakpoints -> reading the
   final guest structure; fewer screenshots; guest-time waits sized to what
   the guest needs. Read docs/plans/SOAK-PARALLEL.md §11 for prior findings.
4. Re-measure AFTER. It must pass, and the assertions must be no weaker:
   never delete an assertion, never shorten a window that IS the measurement
   (rate/flicker rows), never add time.sleep. If you changed HOW something is
   read or asserted, break the subject on purpose (poke the byte, invert the
   expectation, a knob tree) and watch the row go red, then restore.
5. Re-declare the row's `secs` in tests/suite.py at ~1.25x measured (10/30/60s
   steps), and keep `timeout` sane.
6. Docs: re-read docs/plans/completed/SOAK-SPEED.md BEFORE each row (others add
   techniques as they go - use them). Add a ledger row (row | before | after |
   technique numbers | one-line note; quote guest seconds as well as wall).
   Add/extend a numbered Technique ONLY when it generalises, in 1-3 lines -
   keep the doc succinct; edit an existing technique rather than adding a
   near-duplicate. Use Edit with a small unique anchor; if the Edit fails,
   re-read and retry (others edit the same file).
7. Commit just your files: `git commit -m "..." -- <paths>` (end the message
   with the two attribution lines below). On index.lock contention, wait a
   few seconds and retry. Do NOT push; do not commit build/.

## Shared-tree rules (break these and you break five other agents)
- NEVER run `make` or anything that writes build/. Everything is prebuilt,
  including every `wants=` artefact. If you need a knob or private build,
  use tools/os88build.py (build/trees/<knob>-<hash>/).
- Never kill or reap MartyPC instances you did not start (no kill_all, no
  pkill martypc). os88marty.launch() isolates instances already.
- Never run the whole soak tier; only `-k <row>` scoped runs.
- Box: 4 cores, four agents => wall clock is noisy and slower. Never wait for
  load to drop. Compare guest seconds / cycles; wall as secondary.
- Harness (tools/os88marty.py, os88ui.py, os88mouse.py, os88mouserel.py,
  tests/dispcp.py and other shared libs): missing GENERIC features SHOULD be
  added, and existing routines MAY be made faster where still reliable. Others
  are running rows on these files right now, so: re-read the file just before
  editing; keep it importable at every moment (no half-edits); keep defaults
  backward-compatible unless the change is provably safe for all callers;
  validate with a couple of rows that use the routine (e.g. tests/uilayer.py,
  tests/martyconc.py, and your own row); commit promptly; add a one-line entry
  to SOAK-SPEED.md's "Harness changes" section so others can use it.
- Rows needing hardware/emulators absent here (86box, bochs, ...) - skip.

## Token economy
Targeted reads (grep -n, sed -n ranges), tail command output, don't paste
large logs. Don't re-read files you have in context.

## Finishing
Work through your queue. When your remaining rows are all under ~60 s
declared, or your context is getting heavy, stop at a row boundary (all work
committed, queue file marked) and reply with a SHORT report: table of rows
(before -> after, guest s), techniques added, harness changes, rows skipped and
why, and anything the coordinator must know (e.g. product bugs found).

Commit trailer lines:
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_0128wd3x4TL6TkrT5R6Y6y6u

## Round 2 additions (read these too)
- Your queue is `docs/plans/completed/soak-speed-queues/r2-q<N>.txt`. Lines prefixed
  `VERIFY` are rows whose shared script was edited in round 1 and never
  re-run: run each once first (pass = mark DONE; fail = diagnose).
- Read docs/plans/completed/SOAK-SPEED.md FIRST: eight techniques and a dozen harness
  features exist already (quick boots, Forks, UI.still, kernel_up, logging
  breakpoints, ui_done-as-settle). Reuse before inventing.
- EVERY os88test run is `MAKELEVEL=1 python3 tools/os88test.py soak -k <row>`.
  Without it the runner makes in the shared build/ and breaks every agent.
- `git commit -- tests/suite.py` commits the WHOLE file, including other
  agents' uncommitted edits. Commit only your hunks: write your hunk(s) as a
  patch and `git apply --cached <patch>`, then `git commit` (no paths); or
  edit suite.py and commit it immediately so the window is seconds.
- Commit your queue file (with its DONE/SKIP marks) with each row's commit.
- A new file under docs/ needs `python3 tools/os88index.py`, or checkdocs
  fails the next make.
- Scratch files go in your scratchpad, never the repo root.

## Round 3: Bochs and 86Box (both newly installed in this container)
- These emulators have never been driven by an agent in this environment, and
  their harnesses (tools/os88bochs.py, tools/os88bochsui.py, tools/os88box.py)
  have had none of the work MartyPC's got. Read docs/BOCHS.md or
  docs/86BOX.md FIRST, then SOAK-SPEED.md's techniques and harness section.
- Step 1 is a BASELINE of every row in your queue as it stands: pass / fail
  / skip, wall time, and where the time goes (profile the host side too -
  Xvfb, screenshots, xdotool, debugger round trips). Rows that fail at base:
  find out whether the harness or the environment is the cause (fix those)
  or the product (record, don't chase).
- The harness work is expected and WANTED: the same kinds of features
  MartyPC got - a boot gate on kernel state instead of a fixed wait or a
  picture settle, waits on guest memory/state instead of sleeps, a
  closed-loop pointer, several instances at once without collisions (display
  numbers, ports, run dirs, pidfiles; reap only your own), and cheaper
  round trips. Keep it generic and opt-in-compatible; document each in
  SOAK-SPEED.md's "Harness changes" AND in the emulator's own doc
  (docs/BOCHS.md / docs/86BOX.md).
- Only you own your emulator's harness files this round; another agent owns
  the other emulator. Don't edit the other one's.
- Run rows with `MAKELEVEL=1 python3 tools/os88test.py soak -k <row>`. All
  declared artefacts are prebuilt; don't run make in build/ (private trees
  via tools/os88build.py if a row needs a knob kernel).

## Round 4: the rows that used to SKIP for a missing tool
- Installed today: `unicorn` (pip), `dosbox` (apt), NASM 3 (built by
  tools/setup-nasm3.sh into build/nasm3, found automatically), QEMU 9.2
  (built by tools/setup-qemu9.sh into build/qemu9/inst/bin). The qemu9 rows
  launch `qemu-system-i386` BY NAME, so they need
  `PATH=/home/user/os8088/build/qemu9/inst/bin:$PATH` in front of the run -
  only for those rows; every other QEMU row stays on the system QEMU 8.
  Whether the qemu9 rows should find build/qemu9 themselves (as nasm3 finds
  build/nasm3) is a fair harness question for whoever owns them.
- These rows have never run in this container: baseline first (pass /
  fail / where the time goes), then the usual speed pass. Many are small
  host-side rows; for an already-lean one, confirm and re-declare `secs`.
