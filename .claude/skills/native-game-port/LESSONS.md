# What the Excitebike run learned

Ordered the way a run meets them. Numbers are from the run that made PR #206.

## Process

1. **Ask the art/match policy before scouting** (section 0 of SKILL.md; the user later also asked that levels and look MATCH the cartridge, which is why the policy now separates *matching* from *depending*). The scouts recommended a CHR
   importer; the architect planned it; the user vetoed it after launch. Stopping
   cost nothing only because the scout reports were reusable - relaunch with a
   binding `ART AND ASSET POLICY` paragraph in the shared brief that says it
   *overrides scout reports and earlier plan drafts*, skip the scout phase
   (`[].map`), and tell the architect to REVISE the existing plan, not restart.
   Changing the brief text invalidates Workflow's prompt cache for every agent
   after it - edit only what you must.
   **And "match" meant the cartridge's own pixels.** The run shipped
   all-original art (PR #206); the user then asked for the original graphics,
   levels and sound, no dependency on the source, the marks replaced with
   generated art, no copyright line and a new name (8BitBike). That is why
   `extract` is now the default: ask the question with that as the
   recommended answer, and ask for the display name at the same time.
2. **Reports go to files.** Every agent writes its full report under
   `<reports>/` and returns a short summary. Messages truncate; a "verify" that
   only lives in a message is lost.
3. **The script's return is truncated too.** Read `close-*.md` and the wave
   `*-impl.md` files, not just the notification.
4. **One writer per worktree.** A sibling process wrote a test and a PERFORMANCE
   Set mid-close and the critic's first full run overlapped it. Quiet the tree
   before `test-full`.
5. **Independent verify finds what the implementer will not**: it re-ran from
   scratch, read PNGs (crop + zoom), and caught that a CGA test decoded VRAM
   with the start address taken from the game's own variable (so it could not
   see a wrong CRTC value) - it then checked the real card output by hand.
   Give the verifier the acceptance *command*, tell it to trust no log, and to
   record numbers.
6. **One repair round, then re-verify, then stop.** Four waves needed repair
   (2, 3, 5, 7); none needed two.
7. **Fixers must re-check each finding and may skip** with a reason. About a
   third of review findings were wrong or unmeasured ("skip covered bands via
   CLIP_TEST": CLIP_TEST cannot tell partly from fully covered). Skips are
   logged in `wave-N-fix.md`; do not force them.

## Design that worked (and its arithmetic lives in the plan)

- **Do the cycle budget before code.** The plan's per-frame table (scroll,
  erase, column write, sprites, HUD) became the gate: each measured component
  had to land inside +25% of its line, and a frame in 25 tile rows / 200 lines.
  Result: VGA 87,378 clk/frame (54.6 fps), CGA 79,969 (59.7), Hercules 94,380
  (50.6); the work is ~25k clk of an 80-87k-clk CRT frame. Nothing exceeded the
  table by surprise because the table was the spec.
- **Horizontal scrolling = CRTC start-address panning** (VGA mode X: byte/4px
  steps + the attribute-controller pixel pan; CGA: start address, byte =
  4px, modulo the 8192-byte bank), with a ring of world columns and compiled
  sprites, not a redraw. Latch copies and write modes carry the rest. See
  SPEC §101 for the vertical twin and §102 for this one.
- **A governor, not a fixed rate**: fixed-timestep simulation (60.1 steps/s)
  with a pacing governor that drops to n frames per step after a burst and
  returns after 64 quiet frames. The worst frame (Selection B, several bikes)
  was 20.5-22.2 fps and is stated as such.
- **A host reference model** (`tools/<name>sim.py`) that the guest must equal
  step for step (29,476 steps) is what lets the simulation be optimised freely.
  A pixel-exact reference renderer does the same for video (20 random
  positions, every course, ring wrap), each with a *negative control that must
  fail*.
- **Budgets asserted by `--selfcheck`**: every art/sound size against its cap,
  seven negative controls refused, two independent compiles `diff -r` empty
  (determinism).
- **Hercules was a go/no-go wave**, EGA refused with a stated sentence,
  Hercules/EGA verified by `excitebikefront` rows. Decide adapters in the
  plan, not in wave 6.

## Traps

- CLAUDE.md's rules bite immediately in a game: SS != DS (`[ds:bp+..]`), no
  `shl reg,imm>1`, sections back to `.text`, `font_run` not `font_str`,
  512-byte alignment, and **`%error` on a layout past 64KB** (derive band/dict
  sizes from the column count so a longer course fails the build, not the
  screen).
- An `OSAPI_WM_CLIP_SET`/clip hole: every paint path must arm and check it;
  review lens 1 found a click path that painted unclipped.
- Double-writing a region (whole-rect fill then art) is the canonical visible
  flash - `xb_frontpaint` fixed it by filling only margins.
- vsync waits can time out: report via a flag, commit nothing, roll back the
  per-bike snapshot - but do NOT skip a full write or an already-past boundary
  (that froze the picture and stalled the governor; a test found it).
- A 16-bit accumulator that saturates at ~5 s silently stops a race; clamp
  (`0x20:FFFF` span, `xb_cs` at 59999).
- Flaky rows on a loaded host (governor "n back after exactly 64 frames",
  a par off by one) - re-run once before believing, record as flake only if it
  passes; say so in the PR rather than hiding it.
- A new tests/*.py file that is not registered in `tests/suite.py` fails the
  full tier's `registry` row. A `local` package needs its line in
  `apps/RETIRED.txt`/`t_retired.py`; new Makefile targets need `.PHONY`.
- Launch time is CPU, not disk (sprite-blob build 1.5-1.9 s, art decode 1.4 s,
  splash reveal 1.7-3.7 s of 3.8-5.9 s). Pre-building blobs trades CPU for
  floppy reads; the run documented it and did not take it. Decide on purpose.

## Deliverable checklist (what the critic verified present)

package + 4 floppy geometries (`make <name>disk`, `os88disk --verify`), SPEC
section with contracts updated per wave, `docs/INDEX.md` regenerated
(`--check` clean), `README` make lines, `vm/xt-<name>/` 86Box dir, PERFORMANCE
Sets, suite rows for every `tests/<name>_*.py`, provenance gate in the fast
tier, plan doc under `docs/plans/`, disk class (`local` until the maintainer
promotes it), no gitignore needed because no user-supplied asset.
