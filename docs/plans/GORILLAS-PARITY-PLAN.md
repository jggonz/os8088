# Gorillas feature parity

Implement the missing player-visible behavior from `reference/gorillas/gorilla.bas`
in the native 8086 app. Preserve shared OS drawing, nonblocking sound, pause/focus
handling, all three adapters, and the 60 KB instance ceiling.

- [x] Compact cached sparkle masks to make room; retain incremental drawing.
- [x] Active player's name, running scores, persistent proportional wind arrow; final
      scorecard, winner/tie, animated sparkle border, any-key restart.
- [x] Raised-arm release, four rotating banana sprites, hide shots behind sun.
- [x] Animated building blast and distinct expanding gorilla death; persistent
      holes; musical victory dance after the blast.
- [x] Variable building counts/colors/heights and rising/falling/valley/hill
      profiles; reference rooftop placement; reference wind gust distribution.
- [x] Alternate throws across rounds, automatic next round after celebration.
- [x] Full 0..360 shot range, whole-number angles, zero/low-speed self-hit;
      wider/more precise positive gravity; reference-scaled trajectory equations,
      shared AI prediction, pixel silhouette collision.
- [x] Detailed opening instructions; document numeric precision and remaining
      native display/timing adaptations explicitly.
- [x] Update guest regression expectations; exercise scoring (including a last
      scorer who loses the match and ties), animations, numeric boundaries,
      trajectory samples, random cities/gusts, pause and mode changes.
- [x] Build and validate on VGA, CGA and Hercules; record results here.

The user selected retaining the scalable 256x128 scene. Numeric arithmetic must remain bounded and checked; BASIC's floating-point
runtime is not a dependency. The slope selector's unreachable BASIC CASE 4 is
interpreted as the documented hill profile, rather than reproducing that bug.

## Implementation and validation notes

- The light/sprite cache now uses 6,400 bytes instead of 15,360. The current
  package is about 54.3 KB including BSS, below the unchanged 60 KB ceiling.
- `grphysics.inc` shares Q6 motion, Q14 trigonometry, midpoint
  acceleration and adaptive pixel sweeps between live shots and AI.
- Native numeric limits are explicit: 0..360 whole-number angles/velocity, gravity
  0.001..9999.999 with three fractional digits.
  Native resolution, numeric precision and tick-based animation timing remain
  adaptations; this is not a BASIC runtime or pixel-identical EGA reproduction.
- Full redraws reuse the font-band path for the HUD between shots. A seeded
  VGA repaint measured 1.16 s windowed / 1.36 s fullscreen after this change,
  versus 1.54 / 1.81 s through the general scene converter.
- `make -j4`: successful; all 49 fast gates pass after building the complete
  shipped media set. Running fast tests before building those media correctly
  reported missing/stale images; the full build resolved those failures.
- Guest results: reactions (sun, sprites, blasts, scores/ties, trajectories),
  frontend/solo, menus, input and city gates pass on VGA, CGA and Hercules,
  in both windowed and fullscreen modes. The keyboard-match gate also passes
  on all three adapters: five total points using both zero/one-velocity
  self-hits, automatic rounds, final scores, paused hit animation across
  repeated fullscreen transitions, and close. Its complete run took 205.2 s.
- Regression checks caught and fixed a wind-text alignment error, instruction
  text overlapping the marquee, and an explosion repaint that omitted its
  outer pixels at the worst byte alignment. The latter now has an explicit
  radius-18/x-modulo-8=7 pixel assertion.

Current seeded redraw and worst complete intro-frame times, in milliseconds
at an emulated 4.77 MHz (not physical hardware):

| Adapter | City window | City fullscreen | Intro window | Intro fullscreen |
|---|---:|---:|---:|---:|

| VGA | 1242.39 | 1448.98 | 71.70 | 62.35 |
| CGA | 367.64 | 329.55 | 33.52 | 22.40 |
| HERC | 468.08 | 517.08 | 44.15 | 40.98 |

The 20 ms input, 75 ms intro, 400 ms city-generation, 1,800 ms VGA redraw
and 600 ms monochrome/CGA redraw budgets all remain unchanged and pass.

Final validation commands (all passed):

```sh
make -j4                       # complete media + 49 fast checks
python3 tests/gorillas.py
python3 tests/gorillasreactions.py  # each --arm vga/cga/herc verified
python3 tests/gorillasfront.py
python3 tests/gorillasmenu.py --output build/gorillas-menu-parity.json
python3 tests/gorillascity.py --output build/gorillas-city-parity.json
python3 tests/gorillasinput.py --check-repaint  # each adapter verified
python3 tools/checkdocs.py
git diff --check
```

Built media include `build/games360.img` and the apps disks. Screenshots in
`build/gorillas-proof/` include final winners/ties on every adapter, windowed
and fullscreen. The reference BASIC and unrelated user files remain intact.

Follow-up: angle and velocity use whole-number inputs without zero padding;
gravity retains decimals and the 9.8 default. Only the active player is named
in the HUD, and intro names sit inside the border.

Follow-up validation: all 49 fast gates pass; input/repaint, menu/intro,
reactions and frontend/solo gates pass on VGA, CGA and Hercules. The intro
gate now checks ten-character names through all five border phases in both
display modes; gameplay checks both aiming fields and clearing a long name
when the next player has a short name. Initial concurrent guest runs hit host
pacing timeouts; unfinished runs passed with reduced concurrency. The rebuilt
package uses 24,774 image bytes plus 29,545 BSS bytes (54,319 total).
