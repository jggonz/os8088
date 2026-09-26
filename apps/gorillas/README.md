# Gorillas for os8088

A native 8086 adaptation of the supplied Microsoft QBasic `gorilla.bas`
(1990). One player faces the computer, or two people share the keyboard, throwing
exploding bananas across a random city skyline. Wind bends the trajectory,
buildings retain craters, and a banana can hit its thrower.

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
3. Choose **total points**, from 1 to 99 (default 3). As in the BASIC source,
   this counts points scored by both players together; the higher score at
   the end wins. An even total can produce a tie.
4. Enter **gravity**, from 0.1 to 99.9 m/s², with at most one decimal place
   (default 9.8). Smaller values give longer, higher arcs.
5. Press **V** to view the musical gorilla dance, or **P / Enter** to play.
   Any key skips the dance; it also ends automatically.

Blank entries accept defaults; Backspace edits. Alt+Enter and Escape work
throughout setup. The Game menu can restart setup or change fullscreen mode.

- Type an **angle**, press **Enter**, type a **velocity**, then press
  **Enter** to throw. Angles run from 0 to 180 degrees, measured from the
  horizontal toward the opponent; velocity runs from 1 to 150.
- **Tab** switches fields; the first digit replaces the previous value.
  **Backspace** deletes a digit; **arrow keys** adjust the selected value.
- The compact HUD shows names, scores, the active player and wind. Positive
  wind blows right. Gravity is fixed by setup for the entire match.
- **P** pauses/resumes. **N** returns to match setup. **Enter** continues after
  a hit or starts another match after a win.
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
VGA planes are cached once per surface (15 KB per instance); side strips
repaint only rows touched by old or new lights. Fullscreen setup also drains
buffered typing without a tick wait per character.

The original game's floating-point simulation is adapted to swept integer
fixed-point physics, including a fractional gravity accumulator. The solo
opponent predicts candidate trajectories using the same wind and gravity,
then throws normally; buildings can intercept its shots and retain craters.
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

Measured setup transition from player count to the first name, and the worst
of five complete marquee/gorilla frames (milliseconds, emulated XT):

| Adapter / mode | Setup before | Setup after | Animation frame |
|---|---:|---:|---:|
| VGA window | 1895.37 | 72.51 | 62.80 |
| VGA fullscreen | 6057.35 | 75.13 | 49.33 |
| CGA window | 3041.99 | 65.69 | 27.06 |
| CGA fullscreen | 2485.50 | 61.10 | 17.58 |
| Hercules window | 3141.71 | 73.90 | 35.20 |
| Hercules fullscreen | 3136.17 | 71.40 | 32.65 |

Measured initial angle replacement (`45` → `9`), milliseconds per handler:

| Adapter | Window before | Window after | Fullscreen before | Fullscreen after |
|---|---:|---:|---:|---:|
| VGA | 881.45 | 5.62 | 1418.09 | 4.77 |
| CGA | 806.03 | 5.08 | 702.50 | 3.91 |
| Hercules | 819.86 | 5.60 | 805.12 | 4.60 |

These are emulator cycle measurements, not hardware measurements. The faster
renderer adds 1,476 bytes of image and state per instance.
