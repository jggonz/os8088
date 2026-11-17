# Breadboard XT host verification

Run from the repository root:

```
python3 tests/breadxt_core.py
```

The runner compiles `coretest.c` in a temporary directory with ASan/UBSan,
executes the shipping model, simulator, codec and example C, runs six expected-
answer negative controls, then compares 160 deterministic physical-board
resistor fixtures with an independent Python Cholesky solution and KCL check.
Returned rounded currents also satisfy independent KCL. Altered voltage,
branch-current and source-direction controls must all be rejected.
The host uses actual 16-bit record storage (`BBWord`/`BBInt`), while opaque
numerical adapters use native IEEE doubles. This verifies the portable C, not
16-bit local arithmetic, the assembly numeric bridge, SDK calls or the target
stack. The BBNUM package and actual emulator cases supply those other checks.

The C fixtures cover all holes and split rails, trench isolation, DIP orientation,
wire crossing/order, atomic failed edits, bounded undo/redo, capacities, divider/
parallel/LED/switch/pot behavior, floating/short/range diagnostics, every gate
and both orientations of all five HC packages with 0/1/X/Z, contention, missing
power, output loads and analog bridges. Transient fixtures compare RC charging
and discharge with the backward-Euler recurrence and a smaller timestep; test
555 reset/comparator priority, actual power/control/timing wires, settled timing
and changed RB; and check capture wrap, triggers, timestamps and failed-step
publication. Codec checks reject every truncated flasher length and malformed
source fields while preserving live state.

The independent host investigation exposed and retained regression fixtures for:

- Negligible residual roundoff being multiplied below the working numeric range
  after a timing wire was removed.
- A target-only signed overflow in the HC high-input threshold (`5000*7`).
- Feed-forward analog inputs being forgotten during logic re-seeding, causing
  valid low/high HC divider inputs to exhaust propagation.

`export.c` belongs to the source example generator and is a separate tool.
`coretest.c --network` consumes a host-only logical resistor fixture, not a guest
file format. `coretest.c --emit DIRECTORY` exports actual BBX documents through
the shipping encoder for target bridge tests:

- `HCMID.BBX`: HC04 input 1 at 2.500 V, output 2 must be X/unknown.
- `HCLOW.BBX`: input 1 at 1.250 V, output 2 must be HIGH/5 V.
- `HCHIGH.BBX`: input 1 at 3.750 V, output 2 must be LOW/0 V.

For these HC fixtures, probe A follows input 1 and B follows output 2. They must
be checked on the actual 8088 because the host's 32-bit int would mask a target threshold
multiply overflow. Generated test BBX files remain under build/, untracked.

Additional exported target fixtures are `BCLK.BBX` (A steady VCC, B a20ms
clock), `RESET.BBX` (powered555 with RESET grounded and floating comparator
pins), `FLOATLED.BBX` (isolated LED voltage/current), and `MAX24.BBX`
(25series10k resistors/24unknowns/20uA with A4800mV/B200mV). They use stable pin and
branch probes and the same shipping encoder.

The repair fixtures add passive source-current direction and capacitor backdrive,
B-only capture aliasing, dominant555 reset, zero-current floating LED nodes,
real R/C references, series-LED reactivation, weak near-threshold references,
missing real current limiters, multiple-source refusal, actual event exhaustion,
metadata dirty/restore and read-only undo/redo damage objects.

Independent core repair pass:3,956 C checks, six negative-control families,
160 random networks/3,920 oracle comparisons, maximum16 numeric ops per slice;
12.08seconds observed on this host. Target evidence remains separate.

## UI queue and glass regressions

```
python3 tests/breadxt_ui_host.py
```

This separate ASan/UBSan runner executes the actual UI callbacks with a clipped
opaque drawing model and SDK stubs. It generates host declarations from the real
SDK, removing only the three target-layout size assertions; it does not maintain
another API list. Host-only warning suppressions cover existing SDK comments and
source style, not interface or address errors. Each case is a fresh process,
matching loader-zeroed application state.

The queue case sends Enter then Esc before a wake, two object interactions while
an accepted operation is pending, and a busy file-dialog callback. The modal case
checks delta text cost, unchanged inspector, and About across timer/uncover then
dismissal. The damage case changes published input to the renderer, clips a tiny
native paint while readout/scope updates are pending, then compares delta glass
with a clean full repaint. Wide value-editor cases preserve the edit text while
checking uncovered LED/scope/bar changes, both before and after dismissal. Locate
cases exercise Help, compact Scope, an offscreen target and no-location feedback. This is rendering-input injection, not evidence that
those injected numerical values were calculated by the solver.

Seven families have explicit fault controls: bypass the busy callback guard,
invalidate modal cache, hide the partial-damage indication while preserving
its native clip, discard pending main-view damage in a wide editor, or revive an old diagnostic
after a standalone recompile, or draw an interpolated diagonal trace. Each corresponding positive check must turn red. The final UI pass records
176 checks/0 failures in7.11seconds. Compact text append uses4glyph cells/
1drawing call and a steady inspector0cells/0calls. These counts are
host primitive/glyph evidence, not a period-machine timing claim. Assembly band
helpers, font rasterization, kernel clipping, stack and native dialogs still
require actual target verification.

Source current uses the passive pin1-to-pin2 direction for every two-terminal
part. For a supply with pin1 positive, negative current means delivering energy;
positive current means absorption/back-drive. The independent Python oracle
therefore compares source_ua with minus the logical graph's delivered current.

## Installed-package checks

`tests/breadxt.py` drives the actual installed8088 package. Independent rows
can use `--only vga --editor-only`, `--only vga --flasher-only`, or `--only`
`cga`, `herc`, `256`, and `bundle`. Every map is linked into a temporary file and
byte-compared against the primary; the disk's package must equal the built O88.
The bundle case decodes the actual WPK and installs every entry into a fresh
temporary FAT disk before boot. `BREADXT_SHOTS` keeps real adapter screenshots.

The editor case covers native placement/bends/Undo/Redo, resistor changes, A/B
probe binding, independent FAT+BBX readback and cold association launch/solve of
a saved working LED circuit. The flasher case compares three settled68k4ms and
10k1ms periods/duty with independent ln(2) oracles, checks real two-channel
capture and Pause/Step, then deletes/restores an actual timing connection.
It prints guest cycles and simulated progress separately: host wall time cannot
claim XT calculation throughput. The flasher row takes minutes on this host.

Adapter cases grow the real window to its snapped minimum, exercise Inspector/
value/Help, restore, solve and locate a diagnostic, and prove owned region and
overlay claims are released. The final script reports free/largest heap before,
while loaded and after close. `--negative-led` corrupts a real guest-published
current after a positive solve and must fail the electrical assertion.
