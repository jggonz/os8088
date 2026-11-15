# Soak speed 2 - stream "frozen": a soak runs on a FULLY frozen copy

Goal 4 of the second soak-speed pass: a session must be able to fix things
while its own soak runs. The design record is
[SOAK-PARALLEL.md §14.4](../SOAK-PARALLEL.md); this is the ledger - what
changed, what it cost, and what was verified.

## What was exposed before

`os88soak.py start` (SOAK-PARALLEL §14.2) froze three things: the build tree
(`build/trees/plain-*`), a copy of `kernel/ boot/ drivers/ apps/*.inc` for
os88sym (`$OS88_SRC`), and the build number (`$OS88_BUILDNUM`). The run itself
was `python3 tools/os88test.py` with cwd = the LIVE checkout, so all of these
were read live, hours after `start`:

| read live | broken by |
|---|---|
| `tools/os88test.py`, `tests/suite.py` (imported per row decision) | editing a row's registration, a merge |
| `tests/<row>.py` - each read when its row STARTS | editing or checking out a test |
| every `tools/*.py` a row imports (os88marty, os88ui, os88build, ...) | any harness fix |
| the Makefile and package sources a row's `make` reads (knob trees, `need()`) | an edit under `apps/`, a merge |
| `build/martypc/run/martypc_headless` - one directory symlinked into every worktree on the box | `make marty` in ANY worktree |
| `build/fixcache`, `build/trees/*` knob trees | `make clean` |

## What changed

| change | file |
|---|---|
| `start` takes a SNAPSHOT (commit object via a throw-away index), checks it out into a reusable SLOT (detached git worktree under `build/soak/src/s<N>`), pins the instruments, builds the slot, and re-runs `start` from INSIDE it; records stay in the operator's `build/soak/` (`$OS88_SOAK_HOME`) | `tools/os88soak.py` (the block headed *THE SNAPSHOT*, `_start_frozen`, `start`) |
| slot reservation: a live run's slot, the latest run's while resumable (no `exit`), a claimed slot - never re-pointed or pruned; `$OS88_SOAK_SLOTS` (default 2) free slots kept | `tools/os88soak.py` `_reserved`, `pick_slot`, `prune_slots` |
| `--resume` reuses the run's own snapshot unrebuilt; refuses if it is gone, `--resume --resnapshot` to continue on a fresh one | `tools/os88soak.py` `_start_frozen` |
| MartyPC `run/` and 86Box's binary COPIED into the slot; MartyPC `inst/` (the instance registry) LINKED to the shared one; `cc watcom qemu9 nasm3 rgbds gbtest apple2-rom picomem` linked; `fixcache` (content-addressed) linked; ignored inputs `tools/martypc/roms`, `win16ref` linked | `tools/os88soak.py` `pin_instruments` |
| `status` prints the snapshot; the check-in protocol text says the checkout is free to edit | `tools/os88soak.py` |
| `_buildnum_current` asks `buildnum.build_number()` (shallow clones; folded from soak-speed-office 10b460b) - which also honours the pin the snapshot needs | `tools/os88soak.py` |
| `os88test.py --frozen`: the same freeze, foreground | `tools/os88test.py` (`_run_frozen`, one flag) |
| `make clean` spares `build/soak` (it held a live run's source tree) | `Makefile` |
| gate `tests/unit/t_soaksnap.py`, row `soaksnap` (soak tier, host, ~1 s) | `tests/unit/t_soaksnap.py`, `tests/suite.py` |
| §14.4 written; §8.3's "nothing may change the source while a soak runs" and §14.2's "a SOURCE edit is still not frozen" marked superseded | `docs/plans/SOAK-PARALLEL.md` |

`$OS88_SRC` is no longer set by `start`: inside the slot os88sym's own ROOT is
the frozen source. `$OS88_TREE`/`$OS88_BUILD` are still set (to the slot's
`build/`) so every row stays on the paths §14.2 proved out.

## Cost (measured, this box, load average 40-45 on 4 cores)

| step | time | disk |
|---|---|---|
| snapshot commit (`git add -A` into a copied index, `write-tree`, `commit-tree`) | 0.2-0.34 s | a few blobs |
| NEW slot: `git worktree add` | 11.7 s | 640 MB (638 MB of tracked files; 400 MB of it is `apps/video/demo/*.V88`) |
| reused slot: `checkout --detach -f` + `clean -fd` | 0.1-0.2 s | - |
| MartyPC `run/` copy | <0.1 s | 12 MB |
| COLD slot build (shipped set + selection's `wants=`) | 404 s wall, 269 s CPU | the slot's `build/` |
| warm slot build after edits to Makefile + tools | 40 s | |
| warm `freeze()` with nothing changed | **1.1 s total** | |

Compared with §14.2: its `build/trees/plain-<hash>` was warm for every run in
the checkout. A slot is warm for the runs that use it; with two slots the cold
build is paid at most twice per checkout, and consecutive runs that finish
normally reuse the same slot. Knob trees (`build/trees/*`) are per slot too,
so a slot's first `builds`-heavy run pays them once.

## Verification

See the end of this file (filled in after the 09:05 run).

## Left for the next pass

1. **Seed a NEW slot's build/ from the checkout's** when the snapshot equals
   HEAD's tree and the checkout's build is current: copy with mtimes and set
   the slot's checkout mtimes older, so the first `make` is a no-op. Saves the
   ~400 s cold build once per slot. Risky (make semantics on mtimes), so not
   done.
2. **Hardlink the 400 MB of demo videos** into new slots instead of writing
   them: git never rewrites a blob in place, but an editor could, so it would
   unfreeze those files. Saves ~10 s and 400 MB per slot.
3. `check` still reports on the checkout's `build/`, which a frozen `start` no
   longer reads; it could report on the slot it would use.
