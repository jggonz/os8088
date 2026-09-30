---
name: native-game-port
description: Build a native 8086 assembly remake of a console/arcade game (reference = a disassembly or source tree) as an os8088 package, the way apps/drmario (DrMarco), apps/1942 and apps/excitebike were made - levels and look matched to the cartridge, committed assets made once (codex/imagegen art), NO build-time dependency on the reference, XT-4.77MHz-first speed. Drives one Workflow - five scouts, an architect plan, sequential waves (implement, three review lenses, fix, independent verify with one repair round), then a completeness critic and a performance audit - and ends in a PR. Use when the user asks to port, remake or bring a NES/arcade/console game to os8088 as a fast native game. For porting a desktop PROGRAM written in C/other languages as a C package use port-to-os8088 instead.
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
`apps/drmario/` (SPEC §100: splash, animation, embedded-art streams; its generated-art prompts are the model for section 5a),
`apps/1942/` (SPEC §101: FSX pages, CRTC scrolling, compiled sprites, latch
copies, dirty rectangles), `apps/excitebike/` (SPEC §102: *horizontal* scroll).

`LESSONS.md` beside this file is what the Excitebike run learned the hard way.
Read it before step 1.

## 0. Match the cartridge, depend on nothing - ask it first

Two requirements, both the user's, and they pull against each other only if
you confuse them:

1. **The game matches the cartridge**: the levels, tables and rules, and the
   look and sound of the graphics and audio.
2. **Nothing depends on the reference being present.** A plain `make` on a
   machine without it builds the whole game; no test requires it (comparisons
   SKIP cleanly when it is absent); no ROM image, CHR dump, sample or note
   stream is read at build or run time. Anything taken from the reference is
   taken ONCE, at authoring time, by a one-off tool that `make` never runs
   (`tools/<name>_transcribe_once.py`), and only the result is committed, in
   our own format, with a provenance line in `apps/<name>/art/README.md`. A
   fast-tier provenance gate (`tests/unit/t_<name>_clean.py`) fails the build
   if a reference path or importer creeps into `make`.

The first Excitebike launch planned DrMarco's shape (a CHR importer read at
build time) and the user stopped it: *"It shouldn't be that way."* So at
intake ask, with `AskUserQuestion`, what "match" means for **graphics and
audio** (levels/tables are always transcribed unless the user says otherwise):

- *Recreate the look (Recommended)* - `artMode: recreate`. Same poses, sprite
  roles, palette feel, layout, scale and animation timing; every committed
  pixel and note is newly made with the image generator (section 5a) and
  procedural tools. Reference frames are studied, never attached as inputs or
  traced, and their bytes are never committed.
- *Extract exactly, once* - `artMode: extract`. A one-off tool converts the
  cartridge's tiles/palettes/sound data into committed assets: pixel-exact,
  and the licensing call is the user's. Say so in the PR.
- *Original designs* - `artMode: original`. In the spirit of the game, not
  copies.

Levels, piece/obstacle grammar, speed and timing tables, par times and rules
are transcribed once into committed text sources (Excitebike's `.trk` files)
so the game plays the original's courses; deliberate deviations go in
`tests/<name>_ref_deviations.txt` with a reason each. Pass `levels:
"original"` to skip that.

## 1. Intake

1. Reference path (default: a sibling checkout such as
   `../NES-Games-Disassembly/<Game>`), the game's name, a package stem that is
   8.3-safe (`EXCITEBIKE` became `EXCBIKE.O88`).
2. The art question in section 0. 3. Scope: which modes must ship; say what is
   expected to be cut (Excitebike cut design mode, two-player, PCM - and said
   so in the PR).
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
                   scope: "<what must ship / may be cut>", artMode: "recreate", levels: "transcribe" } })
```

`args` can arrive as a STRING in some harnesses; the script parses it and has
defaults, so a bare launch with edited DEFAULTS also works. Phases (`skipScout: true` reuses existing reports):

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

## 5a. Art generation

Masters come from an image generator; production assets are derived from them
deterministically. Probe in this order and use the first that exists:

1. **codex CLI** (tested on codex-cli 0.157.1, `image_generation` feature
   stable): from a scratch directory,
   ```
   codex features list | grep image_generation     # must say true
   codex exec --skip-git-repo-check --sandbox workspace-write -C <scratch> \
     "Use your image generation tool to create <prompt>. Save the PNG into the current directory as <name>.png and print its path."
   ```
   ~100 s and ~12k tokens for one small image; it also leaves a copy under
   `~/.codex/generated_images/`. Run it in the BACKGROUND (`run_in_background`)
   and read the file when done. Run from outside the repo (it can hang in a
   repo checkout), never `--dangerously-bypass-approvals-and-sandbox`, and do not
   pass `--image` with cartridge frames in `recreate` mode.
2. The session's own image tool (`mcp__mcp-image__generate_image`, or the
   built-in imagegen DrMarco and 1942 used).
3. No generator: procedural host drawing only (DrMarco's germs and checkerboard
   are drawn that way) and say so in the PR.

Write prompts like `apps/drmario/art/PROMPT.md`: use case, asset type, the
logical grid (e.g. 320x240), exact pixel regions that must stay black for the
engine, palette list, "hard edges, no gradients or anti-aliasing", "no text,
logos or watermarks", and in `recreate` mode the *measurements and poses* of
the original in words. Commit the master PNG plus the exact prompt
(`art/PROMPT.md`) and the derive tool; look at every generated image (Read the
PNG) before building on it, and check it survives 4-colour CGA and 1bpp
Hercules. Generators drift: pin assets by committing the master, never by
regenerating in `make`.

## 6. Ship

Commit with explicit paths (never `-A`): package dir, tools, tests, SPEC
section, plan, PERFORMANCE Sets, INDEX (regenerated), Makefile/suite/retired
edits, `vm/xt-<name>/`. Push to `origin`, `gh pr create --base main`, put the
measured table and the *known gaps* in the body. Attribution lines per the
session's commit/PR reminder. Do not merge; merges need `--admin` and are the
maintainer's call.
