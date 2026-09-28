# Gorillas for os8088

A native 8086 adaptation of the supplied Microsoft QBasic `gorilla.bas`
(1990). One player faces the computer, or two people share the keyboard, throwing
exploding bananas across a random city skyline. Wind bends the trajectory,
buildings retain craters, and a banana can hit its thrower. Skylines contain 8–12
buildings with varied widths, random facade colors and rising, falling, valley
or hill profiles. Gorillas occupy the second or third rooftop from either end.
Wind normally ranges from −4 to +5; one skyline in three gets a stronger gust,
extending the range to −14 through +15.

Build with `make gorillas`. Open `GAMES/GORILLAS.O88` on the applications
disk, or `GORILLAS.O88` at the root of the games disk. `make build/games360.img` builds the 360 KB games disk.
No BASIC interpreter or external assets are needed.

Launch automatically plays the original opening tune and musical gorilla
intro, with a moving sparkle marquee and alternating gorilla poses. The music
starts after the first screen is visible. It advances to setup when finished;
press any key to skip directly to setup:

1. Choose **1 or 2 players** (default 2). In solo mode the second gorilla is
   controlled by the computer.
2. Enter both **names**, up to 10 characters. Empty entries use Player 1 and
   Player 2, or Computer in solo mode. The computer can also be renamed.
3. Choose the **winning score**, from 1 to 99 (default 3). The first player
   to reach that many points wins the match.
4. Enter **gravity**, from 0.001 to 9999.999 m/s², with up to three decimal places
   (default 9.8). Smaller values give longer, higher arcs.
5. Press **V** to view the musical gorilla dance, or **P / Enter** to play.
   Any key skips the dance; it also ends automatically.

Blank entries accept defaults; Backspace edits. A blinking underscore marks
where the next character will appear. Alt+Enter and Escape work
throughout setup. The Game menu can restart setup or change fullscreen mode.

- Type an **angle**, press **Enter**, type a **velocity**, then press
  **Enter** to throw. Angles run from 0 to 360 degrees, measured from the
  horizontal toward the opponent. Velocity runs from 0 to 360. Both fields
  accept whole numbers only and display without zero padding.
  Zero and one cause a self-hit. Angles above 180 aim downward.
  Each player remembers her own last angle and velocity across turns and
  skylines. A new match resets both players to angle 45 and velocity 70.
- **Tab** switches fields; the first digit replaces the previous value.
  **Backspace** deletes a digit; **arrow keys** adjust the selected value.
- The HUD shows **only the active player's name and both running scores**. **Angle** and **Velocity** appear below. Signed wind
  appears beside the angle; the arrow below the city points with the wind
  and grows with its strength. The arrow remains visible during throws.
  Gravity is fixed by setup for the entire match.
- **P** pauses/resumes. **N** returns to match setup. After a hit, the next
  skyline starts automatically following the celebration and a short pause;
  **Enter** can skip that pause. Throwers alternate across skylines.
- A sparkling **final scorecard** lists both scores and declares the overall
  winner. Press any key to set up another match.
- **F** or **Alt+Enter** enters/leaves fullscreen; **Escape** returns to
  the window. The Game menu also offers fullscreen, pause and new match.
- The system **About** card supplies credits. Dismiss it with a click or
  key. Windowed simulation waits while the window is covered or unfocused.

VGA uses the BASIC game's blue sky, gray/red/cyan buildings, yellow windows
and sun, and orange gorillas. Fullscreen VGA loads the original EGA RGB
colors into its DAC. Windowed VGA uses the shared desktop palette, blending
red/yellow pixels for orange so other windows keep their colors. Gorillas
have facial and chest detail, buildings have roof edges and mixed lit/unlit
windows, and the sun has rays and a smile. Hercules uses monochrome. CGA uses the OS's
monochrome desktop when windowed and **320×200 four-color graphics in
fullscreen**; the OS does not provide color CGA desktop windows.
Fullscreen CGA selects a blue background and the bright green/red/yellow
hardware palette. CGA can change its background, foreground palette group
and brightness, but cannot independently redefine all four colors like VGA. Exiting
fullscreen restores the desktop and preserves the current shot and city.

Like Dot Delirium, the port uses the OS's exclusive fullscreen bracket,
window clipping and graphics bands. Its 256×128 scene scales to the available
surface, with different vertical scales for the adapters' pixel shapes.
Only the banana's small saved patch changes during flight. Each instance
owns its state, scene, scratch space and a 256-byte-stack worker; the game
requires no kernel modifications.

All game text hides during a throw so the banana remains visible through
the top of the sky, and returns when the shot ends. Pausing a shot keeps
the text hidden; **P** or **Enter** still resumes it.

A raised arm accompanies each release. The banana rotates through four
orientations, disappears behind the sun and opens its mouth in shock until the
throw ends. Terrain impacts expand and contract into permanent craters; direct
hits produce a larger, alternating-color blast that removes the gorilla.
After every point, the scoring gorilla performs the original four left/right
arm pairs, each accompanied by `VictoryDance`'s seven-note tune and short rest.
This includes self-hits and the final point. **Enter** continues once the dance
finishes; pause, fullscreen and new match controls remain available during it.

Angle, velocity, setup edits and menu transitions redraw only changed character
cells. Invalid input draws only the error line. Like Dot
Delirium, the HUD composes opaque bands directly from the OS font. Windowed
VGA uses the OS's colored 1bpp blitter, monochrome adapters use 1bpp bands,
and fullscreen CGA/VGA use packed/planar text writes. The scene keeps an
identical copy for window exposure and mode changes. Buffered fullscreen
aiming keys are drained without a frame wait between characters.

Menu exposure and mode changes clear the background and draw font bands
directly. The intro uses precomputed light masks and native sprite pixels,
including VGA planes, instead of plotting and converting each pixel every
frame. Animation is a separate layer from the text scene. Scaled masks and
VGA planes are cached once per surface (6,400 bytes per instance); side strips
repaint only rows touched by old or new lights. Fullscreen setup also drains
buffered typing without a tick wait per character.

The original game's floating-point simulation is adapted to swept integer
fixed-point physics using the reference trajectory equations scaled from
640×350 to the native scene. Midpoint velocities retain the half-acceleration
term; remainder accumulators preserve fractional acceleration from gravity and wind. Adaptive
substeps move at most one logical pixel along either axis, and gorilla
collisions test the drawn silhouette. Angles use Q14 sine values.
The logical scene, integer angles and velocity, bounded decimal gravity and BIOS-tick
animation timing remain native adaptations rather than bit-exact BASIC emulation. The solo
opponent predicts candidate trajectories using the same wind and gravity,
then adds a random velocity error of up to eight points in either direction
before throwing. This makes it more forgiving; buildings can also intercept
its shots and retain craters.
Its aiming work is spread over worker ticks so pause and menus stay usable.

The intro, dance, throw and impact note sequences come from the reference
`PLAY` strings. `tools/gorillas_music.py` generates speaker frequency/duration
tables. Sound is nonblocking; durations round to the OS's 18.2 Hz ticks, with
a one-tick minimum for very short notes. Raised-arm artwork is generated by
`tools/gorillas_art.py`. The reference source is
credited to Microsoft Corporation, copyright 1990; the port is native
assembly rather than a bundled BASIC runtime.

Run the actual guest gameplay gate on all three adapters:

```sh
make gorillas
python3 tests/gorillas.py
python3 tests/gorillasreactions.py
python3 tests/gorillasfront.py
python3 tests/gorillasmenu.py --output build/gorillas-menu.json
python3 tests/gorillasinput.py --check-repaint --output build/gorillas-input.json
```

Use `--arm vga`, `--arm cga` or `--arm herc` for one adapter. Screenshots
are saved in `build/gorillas-proof/`. The contract is [SPEC.md §98](../../SPEC.md#98-gorillas-appsgorillasgorillasasm).

The reference source and IBM hardware documents are preserved in
[`reference/`](../../reference/README.md). Reproduce/check the committed
art and color tables with `python3 tools/gorillas_art.py [--check]`.

The input gate measures `gr_key` through its return in MartyPC guest cycles
at 4.77 MHz, including interrupts and drawing, excluding keyboard delivery
and the fullscreen idle wait. It enforces a 20 ms edit budget, checks HUD
pixels against the OS font, and optionally compares incremental video memory
with a full repaint after every key. It also checks paused edits, numeric
limits, and six buffered fullscreen keys completing within one BIOS tick.
Use `--max-input-ms 0` when measuring an older, slower build.

The menu gate measures field changes, errors and edits, checks glyphs and
video memory, and compares every marquee phase and both gorilla poses with
the original scene renderer on each adapter. It includes windowed and
fullscreen modes, with a 150 ms menu-transition budget and a 75 ms animation
frame budget at 4.77 MHz.

Historical measurements before the feature-parity update: setup transition from player count to the first name, and the worst
of five complete marquee/gorilla frames (milliseconds, emulated XT):

| Adapter / mode | Setup before | Setup after | Animation frame |
|---|---:|---:|---:|
| VGA window | 1895.37 | 72.51 | 62.80 |
| VGA fullscreen | 6057.35 | 75.13 | 49.33 |
| CGA window | 3041.99 | 65.69 | 27.06 |
| CGA fullscreen | 2485.50 | 61.10 | 17.58 |
| Hercules window | 3141.71 | 73.90 | 35.20 |
| Hercules fullscreen | 3136.17 | 71.40 | 32.65 |

Historical measurements before the feature-parity update: initial angle replacement (`45` → `9`), milliseconds per handler:

| Adapter | Window before | Window after | Fullscreen before | Fullscreen after |
|---|---:|---:|---:|---:|
| VGA | 881.45 | 5.62 | 1418.09 | 4.77 |
| CGA | 806.03 | 5.08 | 702.50 | 3.91 |
| Hercules | 819.86 | 5.60 | 805.12 | 4.60 |

These are emulator cycle measurements, not hardware measurements. The faster
renderer adds 1,476 bytes of image and state per instance.

Full skyline redraws finish one 32-pixel strip at a time from left
to right. Packed facade/window fills, native ink-pair conversion, repeated-row
reuse and a fast empty-sky path reduce the work. Repaints overwrite the old
scene directly, so the city no longer flashes blank first. Craters and the
current banana remain part of the scene.

Historical measurements of the original eight-building seeded city, in
milliseconds at an emulated 4.77 MHz (excluding city generation):

| Adapter / mode | Before | After |
|---|---:|---:|
| VGA window | 3303 | 1393 |
| VGA fullscreen | 5917 | 1631 |
| CGA window | 2901 | 355 |
| CGA fullscreen | 2341 | 322 |
| Hercules window | 3001 | 461 |
| Hercules fullscreen | 2996 | 508 |

City generation separately drops from 1540–1564 ms to 300–306 ms. VGA still
has a visible reveal on an XT; it is now confined to successive buildings.
The change adds 3,344 bytes per instance and uses the existing scratch and
intro-cache allocations. No additional heap or kernel memory is needed.

Run `python3 tests/gorillascity.py --output build/gorillas-city.json` for the
cycle and pixel gate. It checks rooftop placement and spacing across 32 seeds,
variable widths, computer aiming error, reproducible cities across adapters,
every ink pair, scaling, partial terrain updates and clipping. VGA checks read
all four planes with guest memory copies; debugger peeks alone return only plane zero.

The expanded gameplay and final scorecard fit within the existing 60 KB instance
limit. Rendering uses a 5,376-byte scratch buffer, with up to 32 rows for strips
no wider than 56 pixels and seven rows for wider updates.

The feature-parity revision is recorded in
[the implementation plan](../../docs/plans/GORILLAS-PARITY-PLAN.md), including
current guest measurements. The package uses 24,787 image bytes and 29,553
BSS bytes (54,340 total), below the unchanged 60 KB instance ceiling. The
reactions gate now also checks winning scores, numeric limits, silhouette
collision, blast repaint bounds and reference-equation trajectory samples.
