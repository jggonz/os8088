---
name: story-fanout
description: Build out a user-story-based feature - a plan's stories, open items or waves (WIN16-NEXT-PLAN's Q-items, a *-PLAN.md story list) - by fanning the independent items out to parallel sub-agents, each in its own worktree on a cheaper model, with a Monitor armed on every long-running agent, then verifying and improving each branch with Opus before integrating it into the feature's PR and running the final pass once. Use when the user asks to work through, parallelize or "fan out" a plan's stories or items, to continue a story-based PR with sub-agents, or to build a feature as several independent pieces at once. For a C port use port-to-os8088; for a native game remake use native-game-port.
---

# Story fan-out

Before starting, read the [shared runtime guide](../../../docs/AGENT-SKILLS.md)
for Claude Code and Codex tool mappings and session state, and
[LESSONS.md](LESSONS.md): every trap in it fired on a real run.

A story-based feature arrives as a list: a plan's stories, a PR's open items, a
wave's checklist. Most items in that list touch different files and can be built
at the same time. This skill builds them that way, under four rules from the
owner:

1. **Parallelize as much as possible.** Fan the independent items out to
   sub-agents at once, not one after another.
2. **Monitor every long-running agent: what it says, and its silence.** Arm a
   Monitor on the logs when agents start and relay what they report as it
   happens. Arm a second Monitor, the stall watchdog (Step 3), so that an agent
   that goes quiet is caught and nudged by the lead without anyone asking. An
   idle notification is not a delivered result.
3. **Cheaper models build; Opus verifies.** Implementers run on Sonnet, or on
   Haiku for a mechanical chore. Opus reviews each finished branch and improves
   it where that is warranted.
4. **The final pass runs once, at the end** (CLAUDE.md, *Testing*). No agent
   runs a soak row or `test-full`. The lead runs the deduplicated union after
   every branch is in.

The worked example is the WIN16-NEXT round on PR #329: seven agents for Q1, Q2,
Q3, Q7, Q8, L1/Q4 and L2, all started from one shared brief.

## Step 0 - scope

Read the plan or PR the user named, from beginning to end: its stories, open
items, the owner decisions already taken, and what each item names as done.
Read `docs/UPSTREAM.md` and fetch. Find the feature branch and its PR, and
bring main into it if main has moved.

Sort every open item into one of three bins:

- **Fan out.** It can be built without an owner decision, and its "done" is
  something an agent can show: a commit, a look, a number.
- **Owner decision.** The plan marks it as one, or it would cross a resident
  rung, change an ABI, or pick between routes the owner has not chosen. Do not
  fan these out. Gather the numbers, if that is itself fan-out work, and put
  the decision to the user.
- **Defer.** It is large, it depends on another item's outcome, or the plan
  orders it later. Say so in the report; do not quietly drop it.

Merge small chores that touch disjoint files into one agent, so fewer agents
build at once. Keep measurement-only work (looks, arithmetic, reach figures)
apart from code work, because its output is docs.

## Step 1 - partition by file ownership

Two agents editing the same routine produce a merge nobody can review. For
each fan-out item, write down the files and routines it will touch. Where two
items share a file (for example two kernel fixes in `kernel/mega.inc`), keep
both, but tell each agent the other's routines and require edits local to its
own. Where two items share a routine, give them to one agent.

## Step 2 - worktrees, logs and the shared brief

For each agent `<key>`:

```
git worktree add -b <feature>/<key> ~/Repos/os8088-<prefix>-<key> origin/<feature-branch>
```

First run `make` on the base branch. If it fails, for example because a
generated file such as `docs/INDEX.md` is stale, fix and commit that before
creating any worktree. Otherwise every agent starts from a red tree.

Worktrees go under `~/Repos/os8088-<name>`, never `/tmp` (CLAUDE.local.md).
Git-ignored inputs the agents need, such as `win16ref/`, are symlinked in. A
symlink is NOT matched by a `dir/` ignore pattern, so add `/<name>` to
`$(git rev-parse --git-common-dir)/info/exclude` first and check it with
`git check-ignore`.

Make a log directory outside the repo (e.g. `~/Repos/<feature>-logs/`) holding
one empty `<key>.log` per agent and one `COMMON.md`, the brief every agent reads
first. `COMMON.md` carries:

- the goal, quoted in the owner's words, and the plan's path;
- the workspace rules: its own worktree only, never the main checkout, stage
  by name, the commit trailer, no push/PR/merge (the lead integrates), no
  Microsoft binaries or screenshots in the repo;
- **the log protocol**: one line per milestone,
  `[<key>] <HH:MM> STATUS: STARTED|PROGRESS|RUNNING|BLOCKED|DONE|FAILED <text>`,
  where `RUNNING` precedes any command over three minutes and `BLOCKED` names
  an owner decision with its options and byte costs, then stops. A `RUNNING`
  line carries an estimate as `~<N>m` (the watchdog waits that long plus five
  minutes before calling the agent stalled);
- **the completion rule**: an agent never goes idle waiting for its own
  command. A long command runs in the foreground with a timeout, or in the
  background as `(cmd > <logs>/<key>-<what>.out 2>&1; echo "[<key>] $(date
  +%H:%M) STATUS: PROGRESS <what> finished rc=$?" >> <logs>/<key>.log)`, so
  that the finish is itself a log line the agent's own Monitor (or the lead's)
  sees. "Waiting for the build" with no such line is a stall;
- **no `git stash`, ever**: the stash stack is shared by every worktree of the
  repo, so one agent's `pop` applies another's work. Take baselines by
  copying build outputs or by building into a private `BUILD=` tree, and
  commit work in progress instead of stashing it;
- the report path, `<logs>/<key>-report.md`. A final message can arrive
  truncated or late, so the report file is what the lead reads;
- the testing rules: host checks, `make`, kernsize and budget-check, and one
  boot to LOOK. No soak rows (a hand-run `tests/<row>.py` counts as one), no
  `test-full`. Name the covering rows instead. A diagnosis may reproduce a
  failure, and must write down each run;
- the shared-host rules: never `pkill`, use its own QMP socket, and a fast-tier
  wall-clock failure with every row green is host load;
- the kernel rules: budgets, no resident rung crossed, ABI locks. Give a budget
  per PART and per section, not only for the resident image: several agents
  each fitting in the same on-demand part can together overflow it;
- **a reserved table of section numbers**: each agent's SPEC and plan section
  numbers, assigned by the lead. Agents never take "the next free" number,
  because two agents in flight find the same one;
- the build rules: a baseline is built in a separate `BUILD=` tree, never in
  the same `build/` as the real build. A hard-disk rig is rebuilt from the
  tree being tested, because `TESTHD=` boots the disk's own `KERNEL.SYS`;
- a table of every agent's key, item and files, so each knows its neighbours.

The per-agent prompt is then short: the key, the worktree, the item quoted from
the plan, the order of work (SPEC first), what "done" means for it, and what
not to touch.

## Step 3 - launch and monitor

Arm ONE Monitor over all the logs **before** launching, so no early status line
is missed:

```
tail -n0 -F <logs>/*.log | grep -E --line-buffered \
  '\] [0-9:]+ (STATUS: *)?(STARTED|DONE|BLOCKED|FAILED|RUNNING|PROGRESS)|Error|Traceback'
```

Use the maximum timeout and re-arm it on every expiry while any agent is still
running. List the log files by name; a glob is expanded only once.

Arm the **stall watchdog** beside it, also before launching, with one
`--worktree` per log file (the log's stem, so a verifier logging to its
implementer's file is covered by the same entry):

```
python3 .claude/skills/story-fanout/watchdog.py --logs <logs> \
  --worktree <key>=~/Repos/os8088-<prefix>-<key> ... --stale 10
```

It prints `STALL <key> ...` once when an agent's last status is not DONE,
FAILED or BLOCKED and, for ten minutes (or a `RUNNING ~<N>m` estimate plus
five), its log has had no new line, nothing in its worktree was written or
committed, and no process is running there. Re-arm it on expiry as you would
the log Monitor. Run it once by hand with `--once --stale 1` against a scratch directory of
logs you wrote yourself (never append test lines to the live logs: an
unfinished `[test]` key is itself a stall), so you know it fires. Log the
lead's own long runs to a `lead.log` with no `--worktree`, so they are
watched too and never charged to an agent.

**On a `STALL` line, nudge at once; do not wait to be asked.** Look once at
the worktree (`git status --short`, `git log -1`, whether a make or emulator
is running there) and SendMessage the agent what you found and the exact next
step: "your build finished, read its result; commit the N files you have
written, staging them by name; then the checks; then the report and DONE".
Append the nudge to `DECISIONS.md`. A second `STALL` for the same agent after
a nudge goes to the user with what is uncommitted, so they can choose between
another nudge, a fresh agent over the same worktree, and the lead finishing it.

Launch every fan-out agent in a single message, in the background, with
`model: "sonnet"` (or `"haiku"` for a mechanical chore) and a `name` equal to
its key, so it can be messaged.

While they run:

- Relay `BLOCKED` and `FAILED` lines to the user when they arrive. A `BLOCKED`
  owner decision goes to the user as one concrete question, with the agent's
  numbers.
- When an agent reports a background run of its own (a Bochs set, a long
  reproduction), arm a Monitor on that run's log or PID too.
- An agent's idle notification while its last status is not terminal is a
  stall candidate: check the worktree then, rather than waiting for the
  watchdog's threshold.
- Answer an agent's question by SendMessage **and** by appending the answer to
  a `DECISIONS.md` in the log directory. Messages to a running agent have
  arrived late and in batches; the file is the record that survives.
- Do not do an agent's work yourself while it runs, and do not predict its
  result.
- An item has one owner. To move an item to another agent, tell the current
  owner first and wait for its ACK before the new owner starts. A redirect
  that arrives late means the item gets built twice.
- Each agent launched later gets its section numbers from the reserved table
  in its brief.

## Step 4 - verify and improve with Opus

As each agent reports `DONE`, start an Opus verifier (`model: "opus"`) on its
branch. Do not wait for the whole set. The verifier gets the item, the plan
section, the branch and the agent's report, and NOT your opinion of the work.
It:

1. reviews the diff for correctness, register and section discipline, SPEC
   agreement, regressions against main, and leaks on every exit path;
2. checks the report's claims against the branch. A "SEEN" claim needs its
   screenshot or log; a size claim needs the kernsize output;
3. fixes what it finds, in its own commits on the same branch, re-running
   only host checks;
4. writes `<logs>/<key>-verify.md`: confirmed, fixed, and still open.

Use Opus for implementation from the start only when the item is subtle enough
that a wrong build costs more than the saving: fault paths, re-entry, anything
whose failure is "the wrong package closed". Say so when you choose it.

## Step 5 - integrate

Merge each verified branch into the feature branch in the order that keeps
conflicts smallest: docs-only and host-tool branches first, then packages, then
kernel. Resolve each conflict by reading both sides; never take one side
wholesale without checking what the other carried. Integrate in a worktree
with a warm `build/`. After EACH merge, run a full `make` and the size checks
for every budget the agents were given (package parts included). If the kernel
changed, also run `tools/kernsize.py` and `make budget-check`. Two branches
can each pass alone and fail together.
Update the plan: tick what was SEEN, and leave open what was only built. Add
new findings as new items.

## Step 6 - the final pass, once

1. Refresh any artefact a plain `make` leaves stale (for WIN16,
   `make build/win16/DOS.O88`).
2. Run ONE `os88test.py soak -k … -k …` over the deduplicated union of every
   row the agents and verifiers named, with any Bochs rows in the container, in
   parallel, and a Monitor on each run.
3. Fix the failures and re-run only those rows.
4. Run `make test-full` ALONE, with nothing else building, then
   `make budget-check`.
5. Push, rewrite the PR body (what works, what was seen versus built, the
   rung record, the rows, what is left), and report to the user honestly,
   failures included. Merge only on the user's instruction (`--admin`).

Clean up: remove each agent's worktree once its branch is merged. Stop every
Monitor. Kill only the emulators this run started.
