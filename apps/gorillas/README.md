# Gorillas for os8088

A native 8086 adaptation of the supplied Microsoft QBasic `gorilla.bas`
(1990). Two people share the keyboard and throw exploding bananas across a
random city skyline. Wind bends the trajectory, buildings retain craters,
and a banana can hit its thrower. The first player to three points wins.

Build with `make gorillas`. Open `GAMES/GORILLAS.O88` on the applications
disk, or `GORILLAS.O88` at the root of the games disk. `make build/games360.img` builds the 360 KB games disk.
No BASIC interpreter or external assets are needed.

- Type an **angle**, press **Enter**, type a **velocity**, then press
  **Enter** to throw. Angles run from 0 to 180 degrees, measured from the
  horizontal toward the opponent; velocity runs from 1 to 150.
- **Tab** switches fields; the first digit replaces the previous value.
  **Backspace** deletes a digit; **arrow keys** adjust the selected value.
- **G** cycles Earth, Moon and Jupiter gravity before a throw. The HUD
  identifies them as **E**, **M**, and **J**. Positive wind blows right.
- **P** pauses/resumes. **N** starts a new match. **Enter** continues after
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

Angle and velocity edits redraw only changed character cells. Like Dot
Delirium, the HUD composes opaque bands directly from the OS font. Windowed
VGA uses the OS's colored 1bpp blitter, monochrome adapters use 1bpp bands,
and fullscreen CGA/VGA use packed/planar text writes. The scene keeps an
identical copy for window exposure and mode changes. Buffered fullscreen
aiming keys are drained without a frame wait between characters.

The original game's floating-point simulation is adapted to swept integer
fixed-point physics. Players are labeled P1 and P2, matches are first to
three, and gravity has three presets. The original name-entry screens,
introductory dance and music are not reproduced. The reference source is
credited to Microsoft Corporation, copyright 1990; the port is native
assembly rather than a bundled BASIC runtime.

Run the actual guest gameplay gate on all three adapters:

```sh
make gorillas
python3 tests/gorillas.py
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

Measured initial angle replacement (`45` → `9`), milliseconds per handler:

| Adapter | Window before | Window after | Fullscreen before | Fullscreen after |
|---|---:|---:|---:|---:|
| VGA | 881.45 | 5.62 | 1418.09 | 4.77 |
| CGA | 806.03 | 5.08 | 702.50 | 3.91 |
| Hercules | 819.86 | 5.60 | 805.12 | 4.60 |

These are emulator cycle measurements, not hardware measurements. The faster
renderer adds 1,476 bytes of image and state per instance.
