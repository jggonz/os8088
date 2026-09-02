# What the waves 3-7 functional pass cost to learn

Read before step 3 of [`SKILL.md`](SKILL.md). Every item happened during the
functional check and merge of PRs #124-#128 (2026-08-31), on a real screen,
and every one first produced a WRONG conclusion - the conclusion is listed,
because recognizing it is how the trap is caught the second time.

## Driving the UI

- **Two `mouse.py click` invocations are never a double-click.** Process
  startup puts the presses outside the kernel's 9-tick window every time.
  Wrong conclusion drawn: "the icon doesn't open". Fix: position with `to`,
  then both presses down one `qmp.py` connection with `sleep 0.08/0.12`
  between events.
- **A modal alert explains three mysteries at once.** After a menu command,
  `^R` did nothing, the menu bar showed one menu instead of three, and a
  crop looked mid-redraw. Wrong conclusions: "Reload is broken", "the bar
  repaint is broken". The truth, visible only in a FULL screenshot: the
  command's handler had raised `alert()`, and a modal alert owns the menu
  bar and swallows every event until OK. Take the full shot first.
- **Stillness can be the contract.** PONG's frames were byte-identical
  before Serve - correct, because nothing moves until the loop starts
  (WEAVE-SPEC §9.3: no per-frame work without motion). And after a goal the paddle
  ignored keys - correct, because `onGoal` calls `field.stop()` and the
  key poll is a step OF the running loop. Wrong conclusion both times:
  "canvas/input is dead". Read the demo's own handlers before judging.
- **Key-state pollers need held keys.** QEMU's default `sendkey` tap can
  fall between an 18.2Hz poll's frames; `sendkey z 800` holds it. And a
  fast backspace sequence lost one keystroke silently - the committed value
  was `35` where `5` was intended. Verify the on-screen text (the caret
  shows every arrival), never the send.
- **Click coordinates rot.** The B: window's rows shifted when the file
  count changed between waves; a reopened window sits somewhere new; a
  caret jump scrolls the pane under a remembered coordinate. Single-click,
  screenshot the selection, then act in place. A backspace sent after a
  caret JUMP deleted a newline at column 1 and joined two lines - wrong
  conclusion: "the editor corrupted the file".
- **Some contracts need reading before driving.** The grid's formula bar is
  LOADED by a cell click but ARMED only by a click in the bar itself
  (WEAVE-SPEC §6.9.4) - typing after a cell click goes nowhere. Wrong conclusion:
  "the bar is broken". When input seems ignored, check what the spec says
  arms it.
- **Mouse cursor photobombs.** The XOR cursor sits exactly where you were
  working; move it away before the evidence shot.

## The harness

- **A too-early screenshot photographs the previous world.** `make test`
  can rebuild for a minute before QEMU exists; connection-refused
  tracebacks or a stale desktop follow. Poll for `build/qmp.sock` answering
  `info status`, then give the desktop 5s to paint.
- **MartyPC's staged config is a COPY.** `tools/martypc/configs/*.toml` is
  appended into `build/martypc/run/configs/machines/ibm5150.toml` by
  `tools/martypc/build.sh` at build time. Editing the tracked file changes
  nothing until re-staged, and a row asking for a machine only the tracked
  file has dies with `martypc_headless exited at once (rc=1)`. Wrong
  conclusion: "the new test is broken". Re-stage: copy the pristine
  `build/martypc/src/install/.../ibm5150.toml` and append the tracked file
  (never `cp` it beside - duplicate machine names refuse the whole config,
  and so does a duplicate `conventional.size` key from a mangled merge).
- **The suite runner failing when the direct run passes** usually means the
  runner's preflight (the symbol map, a knob, the staged config), not the
  test. `python3 tests/<row>.py` directly, and read
  `build/martypc/run/martypc.log`'s last lines - the answer is there.
- **`kernresident`-shaped harness bugs hide behind adapter choice.** The
  debug server's nonblocking socket dropped any reply bigger than the send
  buffer: deterministic on VGA's 1.8MB `fbuf`, a once-a-week flake on CGA's
  768KB. A failure that is adapter-shaped may be SIZE-shaped (#129).

## Merging a stack

- **GitHub closes, it does not retarget.** Deleting a merged branch that is
  still the BASE of the next open PR closes that PR; a closed PR whose base
  is gone cannot be reopened until the branch is pushed back
  (`git push origin <sha>:refs/heads/<name>`, reopen, retarget, delete
  again). Retarget first, delete second - and never delete an unmerged
  PR's HEAD branch at all.
- **Mergeability is asynchronous.** `gh pr merge` seconds after the
  conflict-resolving push was refused with "merge conflicts" that no longer
  existed. Re-check `--json mergeable`, wait, retry once.
- **Squash merges make cousins of identical content.** After squashing PR
  N, branch N+1 shares no commits with main; the 3-way base falls back to
  the fork point and every file both touched "conflicts" - most resolve to
  one side wholesale. Classify per file first: if main's copy equals the
  previous branch tip's, the newer branch's side is the whole answer; only
  files main genuinely moved (another team's work landing, a set-number
  collision, a shared-SDK growth) need hand-merging. `git diff main
  <last-branch>` empty after the final merge is the proof the gated tree is
  what shipped.
- **A number in prose is part of the diff.** Main's drift changed the cell
  grid (79->80 columns), renumbered a PERFORMANCE.md Set, and grew every
  package by ~105 bytes through a shared include - each invalidated
  figures quoted in specs, tests' docstrings, Makefile comments and the
  suite registry. After any merge, grep the branch's own documents for the
  numbers the merge moved.

## The working tree is somebody's home

- **Capture `git diff` of any pre-existing dirty file before starting.**
  Three subagents swept the user's uncommitted `vm/386-weave/86box.cfg`
  into commits with `git add -A`/`-u`; a fourth destroyed it outright with
  `git reset --hard`. It was restored only because the exact diff had been
  captured at session start. Name every path you stage; never reset or
  checkout over paths you do not own.
- **A bare `git stash pop` in this repo is a trap** (the stash stack
  carries years of other work): a pop that "kept the entry" half-applied an
  old c64 stash into the index and left a UU conflict a later builder
  tripped over. If state must be parked, prefer a scratch clone or
  `git worktree`; if a stash was popped wrong, the entry is still in the
  stack - `git reset --hard HEAD` discards only the half-application (but
  see the previous item first: know what else is dirty).
