---
name: functional-check
description: Functionally verify a change on the glass before it merges - boot the built OS in QEMU, drive the actual UI the change proposes (mouse, keys, menus) over QMP, screenshot the evidence for every user-visible claim, and report per-claim pass/fail. Use when the user asks to functionally check, exercise, or "click through" a branch, PR, or the working tree - or to merge PRs as they pass such a check. Runs the change's own registered gates first; the driving pass is what the gates cannot see.
---

# Functional check

A change's tests prove what the tests ask. This skill proves the thing the
user was promised: the button fires, the cell recalculates, the pane draws
the picture. It boots what `make` built, drives the real UI over QMP, and
keeps a screenshot for every claim - so the merge decision rests on what was
seen, not on what a description said.

It exists because the Weave waves 3-7 merge train (PRs #124-#128) was checked
exactly this way, and the checking found what no gate had: a modal alert
explaining a "dead" `^R`, a key poll that only reads during play, and a
half-broken machine config that killed every MartyPC row at launch. Every
trap below fired on a real screen during that work. **Read [LESSONS.md](LESSONS.md)
before driving anything** - it is the trap list, and each entry names the
wrong conclusion the trap produces.

## Step 0 - scope: what are the claims?

Establish what work is under check and what it CLAIMS, before booting
anything:

- **A PR number**: `gh pr view <N>` - the body's claims are the checklist.
  Check out the head branch. If main has moved since the branch forked,
  merge `origin/main` into it FIRST and re-run its gates - a functional
  check of a tree that will not exist after merge proves nothing
  (docs/UPSTREAM.md; the wave merges each collided with main's own drift).
- **A branch or the working tree**: the checklist is the diff -
  `git diff --stat main...` plus commit messages. Turn it into concrete,
  user-visible sentences ("clicking X does Y") before starting.

Write the checklist down. The report at the end is this list with evidence
attached, and anything that cannot be exercised is REPORTED as not
exercised, never silently skipped.

## Step 1 - the change's own gates first

Cheapest evidence first, and it catches a broken tree before a long
interactive session does:

```
make                       # fast tier rides it; checkdocs too
make test-full             # the pre-merge gate, inside its 600s budget
python3 tools/os88test.py soak -k '<subject>*'   # the rows the change owns
```

`make` **after every commit** and before any emulator row - the About box's
build number is the commit count, so a commit invalidates `build/kernel.bin`
for the symbol reader and every row dies saying the map describes a
different kernel (CLAUDE.md, Testing traps).

A soak row that fails on a loaded box with a navigation-shaped error (drive
B's window never opened; two presses 9 ticks apart) is retried STANDALONE
before it is believed - the flake is documented, and one retry
distinguishes it from a defect. Never loosen a shared retry to make a row
greener.

## Step 2 - boot what was built

```
pkill -f "[q]emu-system"        # a stale QEMU answers on the old kernel;
                                # the pattern must not appear in the killing
                                # command itself (CLAUDE.md, Testing)
make <the-disk-target>          # weavedisk, zdisk, worddisk, ... whatever
                                # the change ships on
make test TESTAPPS=build/<x>.img &
```

Then WAIT for the socket - `make test` may rebuild for a long time first,
and a screenshot taken too early photographs the previous world or nothing:

```
for i in $(seq 1 40); do
  [ -S build/qmp.sock ] && python3 tools/qmp.py build/qmp.sock 'info status' \
      >/dev/null 2>&1 && break
  sleep 2
done
sleep 5     # let the desktop finish its first paint
```

## Step 3 - drive it

The three drivers, and the idioms that actually work (each learned the hard
way - LESSONS.md has the failure that taught it):

- **Move / click**: `python3 tools/mouse.py build/qmp.sock to|click X Y`.
  `--screen WxH` must match the adapter (§39).
- **Double-click**: two `mouse.py click` invocations are two processes and
  ALWAYS miss the window. Position first, then both presses down ONE QMP
  connection:

  ```
  python3 tools/mouse.py build/qmp.sock to X Y
  python3 tools/qmp.py build/qmp.sock 'mouse_button 1' 'sleep 0.08' \
      'mouse_button 0' 'sleep 0.12' 'mouse_button 1' 'sleep 0.08' \
      'mouse_button 0'
  ```

- **Menus**: `down` on the title, `to` the item, `up` on it. Screenshot the
  open menu once per session to learn the real item coordinates before
  selecting blind.
- **Keys**: `python3 tools/qmp.py build/qmp.sock 'sendkey <key>'`. A
  key-STATE consumer (`OSAPI_KEY_DOWN` pollers - games) needs a held key:
  `sendkey z 800` holds for 800ms; the default tap can fall between polls.
  Rapid sequences lose keystrokes - pace them, and verify the on-screen
  text rather than trusting the send.
- **Screenshot**: `python3 tools/shot.py build/qmp.sock out.png
  [--crop X,Y,W,H] [--zoom N]`. **Crop and zoom before concluding a click
  was lost** - a small change is invisible in a full-screen dump. Move the
  mouse away first; the cursor sits exactly where you were working.
- **Verify the target before acting on it**: single-click, screenshot the
  selection, THEN double-click in place. Rows move when file counts change;
  windows move when reopened.
- **Read the whole screen when anything is odd.** A menu bar that "lost its
  menus", input that "stopped working" - take a FULL screenshot before
  theorizing: a modal alert owns the bar and swallows every event, and it
  may have been raised by the previous action's handler.
- **Prove animation with arithmetic, not eyeballs**: two shots N seconds
  apart, diff the pixels (PIL), report the changed-pixel count and bbox.
  Identical frames are CORRECT for an idle canvas - know what the contract
  says before calling stillness a bug.

Exercise every checklist claim, happy path AND one refusal per surface the
change added (the refusal sentence is part of the contract - §10-style
sentences are quoted in the report verbatim). When an oracle exists
(`weavesim --render`, `dfrotz`, a golden number in the PR body), check the
glass against it, not against the change's own opinion of itself.

## Step 4 - report

A table: claim | what was driven | what the glass showed | verdict, with
the screenshot paths. Then, plainly: anything NOT exercised and why, any
flake seen and how it was disambiguated, and any defect found - a defect in
the change blocks the merge; a defect found in code the change does not own
is reported separately (and fixed in its own commit/PR if small, the wave
precedent).

## Step 5 - merging on pass (only when asked)

Merging is not part of the check; do it only when the user asked for
check-then-merge. The house style is squash (`gh pr merge N --squash`;
`--admin` only when the user said to use admin privileges). For a STACK,
per PR, in order:

1. Merge the bottom PR. GitHub's mergeability answer is computed ASYNC - a
   refusal seconds after a push may be stale; re-check, sleep, retry once
   before believing it.
2. **Retarget the next PR's base to main BEFORE deleting the merged
   branch** - `gh pr edit <next> --base main`, then delete. Deleting a
   branch that is still the base of an open PR CLOSES that PR, and a closed
   PR whose base branch is gone cannot be reopened until the branch is
   pushed back.
3. Never delete an unmerged PR's head branch - that closes it too.
4. Merge `origin/main` into the next branch, resolve (squash merges mean
   the histories share no commits - identical content auto-resolves, and a
   conflict means one side is genuinely newer; classify per file against
   the previous branch's tip before touching anything), re-run step 1's
   gates, re-run the functional pass for THAT change, merge, repeat.
5. After the last merge: `git diff main <last-branch>` must be EMPTY - the
   proof that what merged is what was gated - then `make && make test-full`
   on main, and leave the checkout on main.

Throughout: never `git add -A`/`-u`/`.` (name the paths), never
`git reset --hard` or `git stash pop` in a tree with anyone else's dirty
files or stash stack (capture `git diff` of any pre-existing dirty file
before starting, so it can be restored byte-for-byte), and leave the user's
uncommitted files exactly as found.
