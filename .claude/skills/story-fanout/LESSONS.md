# Story fan-out: lessons

Each entry is a trap that fired on a real run, the wrong conclusion it
produced, and the rule that prevents it.

## Messages to a running agent arrive late, or not at all

**WIN16 Phase B/C (#327).** The lead's decisions D1-D3 were sent by SendMessage
four times and never reached the agent. Later answers arrived hours late, in
batches. The agent kept working on its own guesses.

**Rule:** every decision goes into a `DECISIONS.md` the agents are told to
re-read, and SendMessage is only the nudge. Ask for an ACK line in the agent's
log.

## Idle is not done

**WIN16 N3.** The agent went idle twice without noticing that its own
background run had finished: first the soak, then a bisect. The results sat
unread for more than ten minutes. The user's answer was the standing rule: arm
a Monitor on every long-running task.

**Rule:** a Monitor on every agent's log from launch, and another on any
background run an agent reports. A finished run is relayed when its line
appears, not when someone asks.

## A row run by hand is a soak run

**isoTron's six waves** spent about eight of nineteen hours on emulator rows,
mostly `python3 tests/<row>.py` run by hand, and polling. None of it was called
"soak" (CLAUDE.md, *Testing*).

**Rule:** the brief says it in words. Agents name rows; the lead runs the
union once.

## Sub-agents sweep the user's dirty files into commits

Three of five wave builders committed a dirty `vm/386-weave/86box.cfg` through
`git add -A` / `-u`.

**Rule:** "stage by name" is in COMMON.md, and the verifier checks
`git show --stat` for files outside the item.

## A symlink is not ignored by a `dir/` pattern

`win16ref/` is git-ignored as a directory. A symlink named `win16ref` in a
worktree is NOT matched by that pattern, so a `git add` of the tree would have
committed it.

**Rule:** add `/<name>` to the common `info/exclude` and confirm it with
`git check-ignore` before launching.

## A monitor script fails silently in the wrong shell

A log monitor that used bash's `declare -A` and word splitting ran under zsh,
which does neither, and emitted nothing.

**Rule:** keep monitor commands to `tail -F | grep --line-buffered`, or write
them in Python. Test the filter once against a line you append by hand.

## A first bisect blamed a docs commit

`--first-parent` walked into pre-squash history, and a missing row (exit 2) was
scored as bad, so the bisect named a docs-only commit.

**Rule:** a bisect script exits 125 for "cannot test", scores on the one
observable the regression is about, and runs over a range whose ends were both
checked by hand.

## Many builds at once trip the fast tier's wall clock

A concurrent worktree build failed `make` on the fast tier's 30-second ceiling,
with all 43 rows green.

**Rule:** COMMON.md tells agents that a wall-clock-only failure with every row
green is host load, and to re-run once rather than "fix" anything.
