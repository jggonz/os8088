# Kernel hot-paths pass 1: the log

The consolidated record of every change this pass made to a kernel hot
path: what it bought, what it cost in bytes, and how hot the path is. The
brief is `docs/plans/khp1/BRIEF.md` (its section 1 defines hotness H1-H4, and
section 7 defines the columns). Each agent's note in `docs/plans/khp1/` is
the authority on its own rows. This file collects them so the owner can read
the byte-costing items side by side.

**Status: IN PROGRESS.** Rows are added as each agent's branch merges into
`kernel-hotpaths`.

## 1. Byte-costing items: the owner's picks

Each row is one commit whose subject starts `[+N]`, revertable on its own.
They are listed best value first: user-visible ms saved per resident byte,
weighted by how often the operation happens.

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|

## 2. Free items: faster and no resident byte added

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|

## 3. Where the time goes: the ranked profiles

One sub-section per agent, copied from each note's WHERE THE TIME GOES.

## 4. Totals

Resident bytes and `KERN_SIZE` on all four kernels at the base (`a7ee7fab`)
and the tip, and the headline user-visible operations before and after.
