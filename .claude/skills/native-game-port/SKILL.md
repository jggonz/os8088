---
name: native-game-port
description: Build a native 8086 assembly remake of a console/arcade game (reference = a disassembly or source tree) as an os8088 package, the way apps/drmario (DrMarco), apps/1942 and apps/excitebike were made - ORIGINAL committed art and sound, no ROM dependency, XT-4.77MHz-first speed. Drives one Workflow - five scouts, an architect plan, sequential waves (implement, three review lenses, fix, independent verify with one repair round), then a completeness critic and a performance audit - and ends in a PR. Use when the user asks to port, remake or bring a NES/arcade/console game to os8088 as a fast native game. For porting a desktop PROGRAM written in C/other languages as a C package use port-to-os8088 instead.
---

# Native game port (assembly, XT-first)

This is the technique that produced `apps/excitebike/` (SPEC.md §102, PR #206)
in one orchestrated run on **Sonnet 5.5** (53 agents, 7 waves, ~83 minutes of
wall clock). Agents inherit the session model - **never pass a `model`
override** - so whatever model you are running is the one that builds the game.
It was sized for that model; a larger one just needs fewer repair rounds.

What it makes: a **native remake**, not an emulator. The simulation is the
package's own, guided by the reference's rules; the picture and sound are
original; the whole thing is priced against a 4.77MHz 8088 and verified on
MartyPC's cycle counter. Precedents to name in every agent brief:
`apps/drmario/` (SPEC §100: splash, animation, embedded-art streams),
`apps/1942/` (SPEC §101: FSX pages, CRTC scrolling, compiled sprites, latch
copies, dirty rectangles), `apps/excitebike/` (SPEC §102: *horizontal* scroll).

`LESSONS.md` beside this file is what the Excitebike run learned the hard way.
Read it before step 1.

## 0. Hard policy - ask it first, it is the user's decision

**Original art and sound, committed. No ROM, CHR, sample or note-stream
imported from the reference, at build time or run time.** The first Excitebike
launch planned an `EXCITEBIKE_SOURCE` CHR importer (DrMarco's shape) and the
user stopped it: *"It shouldn't be that way."* That cost a relaunch and a plan
rewrite. So at intake, say the policy in one sentence and let the user
override it with `AskUserQuestion` (default: original). Consequences that
follow and must be in every brief:

- all sprites, tiles, fonts, splash and sounds come from committed host-tool
  sources (`tools/<name>_art.py`, `_assets.py`, `_audio.py`; text/JSON/MML
  sources under `apps/<name>/art|audio`), deterministic and `--selfcheck`ed;
- a plain `make` on a machine without the reference builds the **whole game**;
- the reference may inform RULES (physics feel, piece kinds, timing) that are
  re-implemented in our code; a test that compares against it must **skip
  cleanly** when it is absent, and every deliberate deviation is listed with a
  reason (`tests/<name>_ref_deviations.txt`);
- a fast-tier provenance gate (`tests/unit/t_<name>_clean.py`) fails the build
  if an import path or reference dependency creeps back.

## 1. Intake

1. Reference path (default: a sibling checkout such as
   `../NES-Games-Disassembly/<Game>`), the game's name, a package stem that is
   8.3-safe (`EXCITEBIKE` became `EXCBIKE.O88`).
2. Policy above. 3. Scope: which modes must ship; say what is expected to be
   cut (Excitebike cut design mode, two-player, PCM - and said so in the PR).
4. Read `docs/INDEX.md`, SPEC.md §100-§102 headings, and `LESSONS.md`.

## 2. Preflight

```
git fetch origin
git worktree add -b game/<name> /tmp/<short> origin/main   # SHORT path: sockets die past ~100 chars
mkdir -p /tmp/<short>-reports
cp -R <main>/build/martypc /tmp/<short>/build/martypc       # a copy, not a symlink; `make marty` if stale
```

One worktree, one writer at a time. Other sessions dirty the main checkout and
run their own emulators: never `pkill -f`, never `git add -A/-u`, never touch
main.

## 3. Run the workflow

```
Workflow({ scriptPath: "<repo>/.claude/skills/native-game-port/workflows/port.js",
           args: { repo: "<abs main repo>", worktree: "/tmp/<short>", ref: "<abs reference dir>",
                   name: "<Game>", stem: "<pkgstem>", reports: "/tmp/<short>-reports",
                   scope: "<what must ship / may be cut>", precedent: "excitebike" } })
```

`args` can arrive as a STRING in some harnesses; the script parses it and has
defaults, so a bare launch with edited DEFAULTS also works. Phases:

| phase | agents | output |
|---|---|---|
| Scout | 5 parallel: game logic, graphics, audio, perf techniques, integration checklist | `<reports>/scout-*.md` |
| Plan | 1 architect | `docs/plans/<NAME>-PLAN.md` and a wave list (4-7 waves, acceptance = a command or a measurement) |
| Waves | per wave: implement -> 3 lenses (8086/memory, XT performance, fidelity) -> fix -> independent verify -> one repair + re-verify | `wave-N-impl/fix/repair.md`, screenshots |
| Close | completeness critic (fixes mechanical gaps, runs `make` + `test-full`), perf audit (writes a PERFORMANCE.md Set) | `close-*.md` |

Sequential waves are deliberate: each builds on the last and there is one
writer. Parallelism is inside a wave (the lenses) and in scouting.

While it runs, do not touch the worktree or start another `make` there (a
concurrent build fails `make`'s wall-clock gate and clobbers `build/`).

## 4. Decide and report

Read the workflow's return plus `close-critic.md` and `close-perf.md`. Report
to the user: per-wave pass/fail with the measured numbers, the gaps the critic
left open, and anything the plan cut. **Numbers come from MartyPC cycle
counters, never from wall clock or QEMU**, and say which adapter.

## 5. Ship

Commit with explicit paths (never `-A`): package dir, tools, tests, SPEC
section, plan, PERFORMANCE Sets, INDEX (regenerated), Makefile/suite/retired
edits, `vm/xt-<name>/`. Push to `origin`, `gh pr create --base main`, put the
measured table and the *known gaps* in the body. Attribution lines per the
session's commit/PR reminder. Do not merge; merges need `--admin` and are the
maintainer's call.
