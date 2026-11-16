# Breadboard XT source provenance and manufacturer authorities

Inspected 2026-11-16. Upstream study was read-only; implementation is independent.

## Reference and license decision

Repository: https://github.com/gatecat/BreadboardSim
Inspected commit: `0ba38d35a9f7b9cd53a226865d20b7d94faff948`.
Study material is kept outside the product tree and is not a build input.
The root `LICENSE` is the complete GNU GPL version 3 text. `SimGUI/res/about.txt`
credits David Shah (2014–2016) and explicitly permits GPL version 3 or later.
`SimGUI/Properties/AssemblyInfo.cs` credits David Shah (2014–2015).
The inspected C#/C++ source headers mostly contain imports and explanatory
comments, rather than additional license grants. There is no permissive
exception identified for the solver, model XML, footprints, or artwork.
The os8088 root LICENSE is MIT.

Implementation decision: no upstream code, model expressions, footprints,
bitmaps, icons, font data, serialized XML or component tables are copied or
adapted. The existing os8088 license remains unchanged. BreadboardSim is credited
as workflow inspiration; original geometry and package maps follow manufacturer
datasheets. The application, examples and host generators do not depend on the
study checkout. Any future proposed reuse needs its own exact license review.

Author overview: https://ds0.me/csim/index.html (Breadboard Simulator v1.0).
It describes interactive breadboards, hover measurements, and plotted voltages;
its target is Windows/.NET 4.5, not a portable guest runtime.

## Actual inspected implementation authorities

| Concern | Inspected upstream files | Finding to inform an independent design |
|---|---|---|
| Physical breadboard | `SimGUI/Breadboard.cs`, `SimGUI/res/breadboard/breadboard-holes.csv` | Valid holes and internal bus names are data. `GetNetAtPoint` derives connectivity from snapped board coordinates, rather than rendered wire crossings. Mouse interaction starts temporary placements, then commits on a valid endpoint. |
| Wire objects | `SimGUI/Wire.cs` | Permanent/temporary distinction, explicit endpoint board buses, selection and delete. Its single horizontal/vertical length representation is narrower than Breadboard XT's requested bends. |
| Connectivity and document | `SimGUI/Circuit.cs` | `UpdateNetConnectivity` merges the bus identities joined by wires and assigns component pin nets; `GetNetlist` emits simulation input separately. Save/load are XML and undo is independent. |
| Component representation | `SimGUI/Component.cs`, `LeadedComponent.cs`, `VariableComponent.cs`, `ComponentData.cs` | Stable designator, position, orientation, pin-to-footprint map, pin names, parameters, and connected nets are distinct properties. |
| IC representation | `SimGUI/Components/IntegratedCircuit.cs`, `SimGUI/res/footprints.xml`, `SimGUI/res/models/ics.xml` | IC model and physical footprint are separate data. Upstream 555 is an expanded circuit of comparators, resistors, latch and transistor; it is not an economical behavioral 8088 model. Do not port its XML expressions. |
| Probe | `SimGUI/Components/MeasurementProbe.cs` | A probe is a physical one-pin object that adds no electrical netlist element. The UI binds its connected net to a result variable. |
| Plotting | `SimGUI/GraphView.xaml.cs`, `GraphSettings.xaml.cs`, `TraceDisplay.cs` | Simulation time and voltage axes are independent; traces reference simulator variable IDs. Rendering batches paths. Eight traces and retained result lists are unsuitable budgets here. |
| UI update and simulation boundary | `SimGUI/MainWindow.xaml`, `MainWindow.xaml.cs`, `SimGUI/Simulator.cs` | The GUI launches `res/simbe.exe`, sends a textual netlist, and reads results through redirected streams on another thread. `Simulator.Results` retains up to 50,000 vectors. Replace this process/thread protocol entirely with bounded native batches and immutable published results. |
| Undo | `SimGUI/UndoAction.cs` | Command objects provide Undo/Redo, and Circuit maintains a queue cursor; replace object references with bounded stable records. |
| DC core | `SimBackend/DCSolver.cpp`, `DCSolver.h`, `Math.cpp`, `Math.h` | General component-current plus unknown-voltage Newton/Jacobian system, dynamic double matrix per solve, Gaussian pivoting, vectors/maps, exceptions and lambda templates. None fits the checked C model as-is. |
| Transient core | `SimBackend/TransientSolver.cpp`, `TransientSolver.h`, `Capacitor.cpp`, `PassiveComponents.h` | Retains per-tick vectors and times, allocates a Jacobian per tick, uses Windows and host clock limits. Capacitor code mixes backward-Euler and trapezoid-related branches. Do not copy it as a numeric oracle. |
| Digital models | `SimBackend/LogicGates.cpp`, `LogicGates.h` | The original folds idealized logic into analog equations and uses bool state, default threshold 1.4 V and hysteresis. This is not a four-state HC truth-table implementation; obtain requested HC maps independently. |

No isolated routine is a useful direct adaptation: even Math's small Gaussian
elimination carries double, heap allocation, templates/lambdas, and exception
assumptions. The bounded resistor-only nodal system and behavioral state machines
are simpler to independently implement than adapting this general backend.

## Manufacturer authority: NE555P, 8-pin PDIP

TI `SLFS022K`, revised March 2026: https://www.ti.com/lit/ds/symlink/ne555.pdf
Sections 4 (pin map), 5.3 (conditions), 5.5 (reset threshold), 6.4 (truth table).

Pins: 1 GND; 2 TRIG; 3 OUT; 4 RESET; 5 CONT; 6 THRES; 7 DISCH; 8 VCC.
Reset LOW overrides everything: OUT LOW, discharge conducts to ground.
With reset released: TRIG below CONT/2 sets HIGH and discharge opens;
otherwise THRES above CONT sets LOW and discharge conducts; otherwise retain.
Nominal CONT is two thirds VCC; trigger threshold is one third.
Trigger takes priority over threshold under the documented default-control case.
RESET's physical threshold is specified 0.3–1 V, nominal 0.7 V; choose and
document a deterministic educational threshold. Equality needs a stated model
convention. Recommended NE555 supply is 4.5–16 V; 5–15 V is the characterization
range in the description. Breadboard XT accepts 4.5–6 V, with exactly one source; this narrower educational
range also stays within the implemented HC devices' supply range.
Control may be open or bypassed; external drive changes thresholds physically.
Breadboard XT diagnoses external control drive; open/default CONT and its normal
ground bypass are the supported configurations.
The real output is not ideal rail-to-rail; document any idealized output level.

## Manufacturer authority: exact 14-pin PDIP HC logic

Use TI N-package models: SN74HC00N, SN74HC04N, SN74HC08N, SN74HC32N,
SN74HC86N. These documents explicitly include N (PDIP), unlike the latest
Nexperia HC documents, whose current ordering sections list surface-mount parts.

| Device | Datasheet/version | Boolean result |
|---|---|---|
| SN74HC00N | https://www.ti.com/lit/ds/symlink/sn74hc00.pdf ; SCLS181H, August 2021 | NAND |
| SN74HC04N | https://www.ti.com/lit/ds/symlink/sn74hc04.pdf ; SCLS078H, April 2021 | NOT |
| SN74HC08N | https://www.ti.com/lit/ds/symlink/sn74hc08.pdf ; SCLS081J, February 2025 | AND |
| SN74HC32N | https://www.ti.com/lit/ds/symlink/sn74hc32.pdf ; SCLS200F, April 2021 | OR |
| SN74HC86N | https://www.ti.com/lit/ds/symlink/sn74hc86.pdf ; SCLS100F, April 2021 | XOR |

Each has recommended VCC 2–6 V, with GND pin 7 and VCC pin 14.
The four two-input devices share these gate tuples `(A, B, Y)`:
`(1,2,3), (4,5,6), (9,10,8), (12,13,11)`.
The inverter's `(A,Y)` pairs are `(1,2), (3,4), (5,6), (9,8), (11,10), (13,12)`.
This reversal on the upper half is easy to get wrong if generated by an
unqualified sequential numbering loop.

Quad-device pin table:

| Pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Signal | 1A | 1B | 1Y | 2A | 2B | 2Y | GND | 3Y | 3A | 3B | 4Y | 4A | 4B | VCC |

Inverter pin table:

| Pin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Signal | 1A | 1Y | 2A | 2Y | 3A | 3Y | GND | 4Y | 4A | 5Y | 5A | 6Y | 6A | VCC |

Datasheets define binary behavior, not X/Z resolution or event queues. Those
are application model decisions. Breadboard XT defines HC LOW at <=0.3 VCC, HIGH at
>=0.7 VCC, X in between; undriven Z remains unknown at a gate input. The
conservative forced-result rules are AND(0,X)=0, OR(1,X)=1; NAND complements
AND, NOT(X)=X, XOR with either unknown=X. Conflicting ideal outputs diagnose contention; equal drivers are resolved
explicitly. Actual ground/supply pins must connect correctly: a power component
elsewhere is not enough. Four-state resolution, event limits and rejected analog
feedback are application policies rather than manufacturer analog behavior.

## Independent flasher logical reconstruction

Create editable physical placements whose extracted nets equal this graph:

* VCC: supply positive, U1.8, U1.4, RA.1.
* Ground: supply negative, U1.1, timing capacitor negative, bypass return,
  LED branch return.
* Discharge: U1.7, RA.2, RB.1.
* Timing: U1.2, U1.6, RB.2, timing capacitor positive.
* Control: U1.5, 10 nF bypass positive.
* Output: U1.3, series resistor 330 ohm input.
* LED anode: series resistor output, red LED anode; cathode to ground.

RA = 10 kohms, RB = 68 kohms, timing C = 10 uF, supply = 5 V. The continuous ideal reference is
independently computed, not used to drive the model:
high `ln(2)*78000*10e-6 = 0.5406548s`, low
`ln(2)*68000*10e-6 = 0.4713401s`, period `1.0119949s`, frequency
`0.9881473Hz`, duty `53.4246575%`. First charge from zero takes longer
than steady-state high time. Exclude startup from timing comparisons.
LED branch direction above is electrically equivalent if resistor and LED
are swapped in series, provided polarity and return stay correct.

## Risks requiring verification

* Visual placements in the supplied concept are not electrical authorities.
* A free-running 555 must depend on pin 2/6 results and the pin-7 switched
  conductance; using the frequency formula in production would violate scope.
* Break either timing connection, ground RESET, or remove pin-8 supply and
  observe a result change. An unchanged waveform is evidence of a model bug.
* Do not permit a footprint to use both sides of one physical five-hole strip
  for unrelated IC pins. DIP spans the trench and pin identities survive rotation.
* Keep a normal bypass capacitor at CONT supported while rejecting arbitrary
  control-drive circuitry; the bypass is not an extra timing capacitor.


## Original and shared material

The original component artwork and breadboard/scope bands are procedural source
in `apps/breadxt/draw.inc`; the icon is an original 16×16 source bitmap. Native
fonts, UI helpers and software arithmetic are existing os8088 shared source.
No generated image service or external download participates in the build.

The source record establishes provenance and model authority, not a target test
result. [Developer notes](DEVELOPER.md) and [the diary](../HOW-BREADXT-WAS-BUILT.md)
separately record the linked checkpoint, completed host checks, earlier guest
observations and pending final verification. The 555 is a bounded pin-level
behavioral implementation; it is not a transcription of the upstream expanded
transistor model or a claim of transistor-level accuracy.
