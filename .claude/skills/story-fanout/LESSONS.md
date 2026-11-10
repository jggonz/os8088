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

## The log Monitor cannot see an agent that stops talking

**PCIETHER (RTL8139).** An Opus verifier logged `ACK D3`, wrote seven files
and went idle "waiting for both builds". The builds had already finished. It
wrote no further log line, so the log Monitor, which reports only lines, said
nothing for thirty-two minutes, and the stall surfaced only when the user
asked what the agent was doing. One SendMessage naming the finished builds and
the uncommitted files got a commit within a minute. Two other agents (the
`harness` implementer and the same verifier earlier) idled the same way on
their own `make`.

**Rule:** run `watchdog.py` as a second Monitor from launch, and nudge on its
`STALL` line without waiting to be asked. The brief's completion rule makes an
agent's own long command end in a log line, so "waiting for the build" cannot
be silent.

## One agent's `git stash pop` took another agent's work

**PCIETHER.** `kmega` stashed its `kernel/driver.inc` edits to build a
baseline. `harness`, in a different worktree, ran `git stash pop`, which takes
the top of the stack the worktrees share, and applied `kmega`'s kernel work
to its own tree. It noticed, re-stashed it, and `kmega` recovered it by name.
Nothing was lost only because both agents reported it in their logs.

**Rule:** COMMON.md forbids `git stash` outright. Baselines come from copied
build outputs or a private `BUILD=` tree; work in progress is committed.

## The watchdog's first version read only a log's last line

**PCIETHER.** A verifier logged `DONE`, then `PROGRESS ACK D4` for a rule
sent after it had finished, and the watchdog called the finished agent
stalled. A `[test]` line the lead had appended to check the log Monitor was
a second false stall.

**Rule:** the watchdog follows each `[key]` on its own; DONE and FAILED stick
until a STARTED or RUNNING, and BLOCKED reopens on any line. Test it against a
scratch directory, never by appending to the live logs.

## Two agents in flight both took "the next free" section number

**WIN16-NEXT (#329)**: four times in one round: KERN-MEGA-PLAN 14.139,
SPEC 124.25.13, 124.25.16 (part 11 and bigbmp) and 124.25.20 (kindmsg and
vbxlib). Each agent looked for the next free number on its own base, so each
was right about its own tree. `checkdocs` catches a duplicate heading only
after the merge, and then one side renumbers its section and every citation
of it.

**Rule:** the lead assigns every agent its SPEC and plan numbers in the brief
at launch, from a reserved table in COMMON.md. Agents take no other number,
and ask in their log if they need one more.

## Each branch passed alone; together they failed `make`

**WIN16-NEXT.** The lead merged five verified branches while checking only
`checkdocs`, in a worktree with no `build/`. Two branches each passed the
fast tier alone. Together they pushed WIN16's `.w16dlg` part past
`w16wrap`'s 4 KB band, so the feature branch failed `make` and every later
agent started from a red fast tier.

**Rule:** integrate in a worktree with a warm `build/`, and run a full `make`
(plus `make mega` and the size checks a kernel or package budget needs) after
EACH merge, not once at the end.

## Several agents filled the same on-demand part

**WIN16-NEXT.** Three agents put code in WIN16's part 9. Each measured the
resident image and passed. Together they made every program that calls the
part pay its whole size in heap.

**Rule:** give each agent a budget per PART (and per section), not only for
the resident image, and measure the parts after each merge.

## A late redirect built the same thing twice

**WIN16-NEXT.** The lead re-assigned the class table's growth from `lheap` to
`ctlproc` by SendMessage. The message reached `lheap` after it had built the
growth anyway, so two branches changed the same structure and conflicted in
six files. One side then had to drop its version.

**Rule:** an item has ONE owner. When the lead moves an item mid-flight, it
tells the old owner first and waits for its ACK before the new owner starts.
Better still, never put the same item in two briefs.

## A hard-disk rig carried its own kernel

**WIN16-NEXT, o8np.** A rig copied from another agent was booted with
`make test TESTHD=`, which boots the hard disk's OWN `KERNEL.SYS`. That
kernel was older than the tree's, so a kernel change "tested" on the rig had
not been tested at all.

**Rule:** a rig is rebuilt from the tree that is being tested, or the tree's
kernel is booted from A:. The brief says so.

## A baseline build and the real build shared one `build/`

**WIN16-NEXT, kindmsg.** The agent started a "baseline" `make` and then its
own `make` in the same `build/`. Two makes ran over one tree and their
outputs collided silently. The agent then went idle "waiting for the build".

**Rule:** a baseline is built in a separate worktree or `BUILD=` tree. Every
build runs in the foreground as one tool call that waits.

## The base branch failed `make` before any agent started

**WIN16-NEXT.** The feature branch's generated `docs/INDEX.md` was stale, so
every agent's first `make` stopped at `checkdocs`. Each agent then spent
turns on a problem that was not its own.

**Rule:** before creating the worktrees, run `make` on the base, regenerate
what is stale, and commit it.

## A monitor regex that depended on the `STATUS:` word

**WIN16-NEXT.** Two of seven agents wrote `[key] HH:MM DONE: ...` without the
`STATUS:` word. The monitor matched `STATUS: *(DONE|...)`, so it missed one
agent's DONE for about ten minutes.

**Rule:** match the word after the time stamp, whether or not `STATUS:` comes
before it: `\] [0-9:]+ (STATUS: *)?(STARTED|DONE|BLOCKED|FAILED)`.

## Per-branch verification was replaced by one QA run

**PageDesk any-PDF (#354), 2026-11-09.** Twelve agents were running and an
Opus verifier was planned for each finished branch. The owner asked instead:
*"Instead of doing code verification after each step, can you first do a QA
testing run in emulation after all the features are completed? Then look into
fixing the bugs that come up during testing."*

**Rule:** no reviewer between steps. Merge each branch as its agent reports
DONE, with `make` after every merge. When every feature is in, run one QA pass
in emulation over the whole feature, list the bugs, fix them, and only then run
the soak union and `test-full` (SKILL.md, Steps 4-6).
