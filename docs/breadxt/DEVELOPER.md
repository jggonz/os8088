# Breadboard XT developer notes

SPEC.md section 130 is binding. The [shared contract](CONTRACT.md) defines the
interfaces, [format reference](FORMAT.md) defines BBX1, and
[source record](SOURCES.md) names the inspected upstream commit and manufacturer
datasheets. The [delivery plan](../plans/BREADXT-PLAN.md) tracks US01–US16;
[the diary](../HOW-BREADXT-WAS-BUILT.md) separates observations from intentions.

Breadboard XT is an original native implementation. BreadboardSim inspired its
physical workflow; none of its GPL code, model expressions or artwork is copied
or required by the build. The guest needs the os8088 package SDK and 8086 software
arithmetic, with no network, host service, DOS, floating-point coprocessor or
modern runtime.

## Build and inspect

From a checkout with the normal host dependencies and pinned C toolchain:

```
make breadxtdisk breadxtbundle breadxtcheckdisk
python3 tools/os88wire.py --verify build/BREADXT.WPK
python3 tools/breadxtbbx.py inspect build/breadxt-data/FLASHER.BBX
```

That build command completed on 2026-11-16 before the final Help prose update.
The update changes a shipped resource, so repeat it to repack the disk/WPK.
`breadxtdisk` creates 1.44 MB, 720 KB, 1.2 MB and 360 KB FAT12 images, verifying
each. `breadxtbundle` uses the existing WPK convention. `breadxtcheckdisk` adds
BBNUM and independent target regression projects to a 360 KB test disk. Ordinary
`make` remains independent of the optional C compiler. The everything/live-media
payload includes a BREADXT folder; these local builds publish nothing.

The verified artifact names are:

| Artifact | Purpose |
|---|---|
| `build/breadxt.o88` | Native package, icon, BBX association and embedded eager overlay |
| `build/BREADXT.OVL` | Plain overlay emitted for inspection; not an installed sidecar |
| `build/breadxt-data/*.BBX` | Eight ordinary editable example documents |
| `apps/breadxt/HELP.TXT` | Installed tutorial and model notes |
| `build/breadxt*.img` | Four software-disk geometries |
| `build/BREADXT.WPK` | Local installer archive |
| `build/breadxt-check360.img` | Installed test package, numeric fixture and regression projects |

The source example exporter compiles the shipping model, codec and example
builder. It checks structural validity, byte-exact encode/decode roundtrips and
an independently stated logical graph. There is no second guest document dialect.
The Python inspector/converter is an independent bounded reader of the same
wire format; it does not calculate a circuit.

## Composition and execution

`breadxt.c` defines shared storage and includes `model.c`, `sim.c`, `file.c`,
`examples.c` and `ui.c` once. `bb.h` is their shared interface. The assembly shim
combines the C runtime, native UI helpers, procedural drawing helpers and numeric
bridge. Make lists the included C/header/assembly dependencies explicitly.

The model and codec have no OS dependencies beyond memory helpers. The solver
passes opaque eight-byte records to shared `apps/os88fp.inc`; target C uses no
floating type or wide integer. Every numeric bridge entry forces the software
path. Host numeric adapters exercise the same portable C, while BBNUM separately
exercises the actual compiled C calling convention and 8088 arithmetic bridge.

File access, model transactions, topology compilation and simulation execute on
the UI task in lock-free wake callbacks. Input callbacks latch or reject a
request before changing its queued payload. Paint reads committed results.
An edit pauses, cancels pending numerical work and invalidates results; a failed
edit/load preserves the live document. Save/load structural validation does not
require electrical success, so incomplete circuits and broken exercises remain
savable. No worker shares numeric scratch. An eager embedded overlay carries
command-time code without sidecar dependencies or different data pointers.

Simulation is a resumable state machine. A slice spends at most 16 scalar work
units, including numeric operations, comparisons and bounded graph visits; it
does not hide a whole elimination row in one unit. A complete step publishes
voltages, currents, capacitor/latch/clock state and capture together. Stable LU
factorization is reused only while unknown-node identities and conductances
remain compatible. Presentation cadence is separate from simulated time.

The canvas uses small monochrome row/plot bands, native clipping, changed-text
shadows and unions of affected object/strip bands. The 3,120-byte file/row/plot
workspace is serialized: a file operation finishes before drawing and no bitmap
pointer survives the wake. It is not a full-screen framebuffer. Physical strips
have solid marks; other strips on the selected electrical net have dotted marks.

## Units and model boundaries

| Quantity | Source/published unit and supported range |
|---|---|
| Resistance | Source value in 10-ohm units; 100–655,350 ohms |
| Capacitance | 1–60,000 nF |
| Supply | Exactly one ground-referenced source, 4,500–6,000 mV |
| Capacitor initial voltage | 0–6,000 mV |
| Pot wiper | 0–1,000 permille |
| Simulation time | Unsigned word pair in 100 us units; default step 1 ms |
| Time step | 100 us–100 ms; 0.5 ms is used for convergence fixtures |
| Node readings | 0–6,000 mV; negative values are explicit invalid-state sentinels |
| Branch current | Signed uA, at most ±30,000 uA; -32,768 means invalid |

The numerical matrix uses V, mS and mA. Resistor conductance is
`100 / encoded_resistance` mS; a backward-Euler capacitor uses
`capacitance_nF / (100 * step_100us)` mS. Checked pivot, finite-range and KCL
residual policies are in the contract. Accepting a source/component value does
not promise that every connection stays inside the numerical envelope.

Every supported two-terminal current uses pin 1 to pin 2. For a source with its
positive terminal at pin 1, current is **negative while supplying energy** and
positive while absorbing capacitor backdrive. Potentiometers and ICs have no
single two-terminal current and report N/A. Source current includes its total
load and is checked against the same 30 mA limit before signed conversion.
A second supply receives an explicit one-supply diagnostic; it is not silently
used to change the HC/clock rail or assigned an invented current sharing ratio.

The LED model blocks reverse conduction and uses a 2 V forward drop plus 10 ohms
on resistance. A 5 V source through 330 ohms therefore gives
`3 / 340 A = 8.823529 mA`. There is no hidden limiter. An active all-LED path
between unequal ideal rails is refused even when its calculated current happens
to be under 30 mA; a parallel resistor does not provide series limiting.

Unpowered/open-switch LED stubs can carry zero current while their abandoned
nodes remain FLOAT/Z. Genuine resistor/capacitor references and stored capacitor
state are retained. A near-zero-current LED-only cycle can otherwise maintain
mutually arbitrary voltage clamps: the bounded solver conservatively reports
FLOAT for that ambiguity. This is a declared model limit, not a claim that the
physical circuit is insoluble. Unrestricted diode complementarity, reverse
avalanche, heating and damage are outside the application. LED active-set work
is capped at 12 passes.

HC00/04/08/32/86 use manufacturer PDIP pin maps, including actual power pins.
Inputs use 0.3/0.7 VCC thresholds with explicit 0/1/X/Z. Unused digital inputs do
not become fictitious grounded analog nodes. Output contention, unsupported
analog/combinational feedback and a 1,024-event propagation limit diagnose
instead of hanging. Outputs are ideal educational rail drivers. Their output
load and quiescent consumption are omitted from power-pin current: the DC
source reading is the stamped external network at its node, not a complete
IC power budget. The same ideal-output limitation applies to the NE555.

The NE555 is a pin-level comparator/latch/discharge model. At default CONT,
RESET below 0.7 V dominates even with unwired comparator inputs; otherwise an
asserted TRIG below VCC/3 takes priority, then THRES above 2 VCC/3 resets, else
the latch holds. CONT may be open or normally bypassed to ground; arbitrary
external control drive is rejected. The actual OUT and DISCH nets drive the
connected circuit. No filename or frequency formula supplies its waveform.

## Capacity and measured memory

Limits: one 30-column board, 64 parts, 128 wires with two bends, 64 active nets,
24 unknown analog nodes, 16 complete undo commands and two 256-sample channels.
The physical board also bounds DIP count; workspace holds 32 gates. Time is a
pair of unsigned words in 100 us units, independent of host and OS elapsed time.

The final link has 219 C functions, maximum individual frame 52 bytes,
**32,670 bytes resident image + 32,180 BSS = 64,850 of 65,024 bytes**
(174 bytes spare). The eager embedded overlay is 49,625 bytes, compressed to
22,828 bytes; the complete .O88 is 45,676 bytes. No kernel budget was increased.

| Resident BSS component | Bytes |
|---|---:|
| Live and staged documents, 3,106 each | 6,212 |
| Sixteen undo records and one pending record | 2,176 |
| 24×25 opaque-number matrix | 4,800 |
| 128 branch records | 4,096 |
| 32 logic-gate records | 256 |
| Current/next capacitor state | 1,024 |
| Scope samples, timestamps and metadata | 2,068 |
| Published and working result, 330 each | 660 |
| Hole-to-net map | 420 |
| Shared file/board/plot workspace | 3,120 |
| UI text shadows | 768 |
| Numeric-library scratch | 76 |
| Other topology/vectors/queues/UI/SDK state and alignment | 6,504 |
| **Total BSS** | **32,180** |

The remainder is measured against linked BSS, not another allocation. Runtime
heap observations on the installed 360 KB disk give a 65,024-byte region and a
50,688-byte parts carve, **115,712 bytes / 113 KiB owned**. The carve includes
native cluster-read/alignment slack; rounding only the raw overlay would
undercount it. Both claims are movable and are released on close. No worker or
full-screen RAM framebuffer is allocated. The existing UI stack is 512 bytes;
a safe fill probe measured **338 bytes used, 174 spare**, including interrupts
and native launch/menu/file/solver drawing paths. This is measured coverage,
not a static proof of every possible call chain.

| Machine/session | Free / largest before | Loaded free / largest | After close |
|---|---:|---:|---:|
| 640 KB VGA | 515,584 / 515,072 | final scoped run records this | 515,584 / 515,072 |
| 640 KB CGA/Hercules | 518,144 / 517,632 | final scoped run records this | 518,144 / 517,632 |
| 256 KB CGA | 124,928 / 124,416 | **3,072 / 2,560** | 124,928 / 124,928 |

All entries are bytes. The loaded snapshot follows native file/diagnostic work;
associated native caches/window resources account for another 6,144 bytes on
the 256 KB session beyond owned claims. Thus its observed total incremental
working set is 121,856 bytes / 119 KiB. This is a tight, otherwise idle desktop:
close other applications there. Full capacities and scope remain enabled; the
actual 256 KB run exercised launch, solve, files, compact/minimum views and
cleanup. It is not a promise of room for another program or every future SDK.

## Correctness and XT cost

The independent host pass has 3,956 C assertions, six expectation-fault families,
160 resistor networks and 3,920 independent Cholesky/current/KCL comparisons.
Three actual-output corruptions must also fail. ASan/UBSan cover the host core.
The host UI pass has 176 assertions and seven injected defects, including
queued-edit ownership, partial/modal/wide editor damage, diagnostic location
and stale results, sample holds and Pause labels. Appending a value character
changed four glyph cells in one call; an unchanged inspector drew zero cells.

The actual 8088 ABI fixture checks 13 software-number cases and two raster
cases: short dashed spans retain their global pattern while A stays solid.
Its N-key fault must produce 14 passes/one failure, then restore 15/zero. Host
doubles do not establish target promotions, segments or arithmetic; this guest
fixture and installed target HC/LED/RC cases do.

Actual 8088 observations include HC inputs 1.25/2.5/3.75 V producing HIGH/X/LOW,
555 reset with floating comparator pins, isolated LED FLOAT/zero current, a
B-only clock, and signed LED/source current. RC at 101 ms is 3,170 mV versus the
independent backward-Euler value 3,169.746 mV. Smaller-timestep convergence and
RC discharge/initial conditions are separate host checks.

| XT measurement | Guest cycles | Interpretation |
|---|---:|---|
| 24-unknown first step, with Help input | 157,526,406 | about 33.0 XT seconds; all 25 currents and node voltages checked |
| Help accepted during elimination | 3,977,940 | about 0.83 XT seconds including its native paint |
| Scope paint with one sample | 1,387,965 | about 0.29 XT seconds; not a full-ring frame-rate claim |
| RC Run/Pause to 101 ms | 381,335,966 | about 79.9 XT seconds, 1.26 simulated ms per XT second |
| 68k flasher, 4 ms steps, 4.360 simulated s | 8,474,639,001 | 1,775.639 XT seconds, 2.455 simulated ms per XT second |
| Edited 10k flasher, 1 ms steps, 0.911 simulated s | 6,197,978,767 | 1,298.624 XT seconds, 0.702 simulated ms per XT second |

The run measurements include control/display overhead and scheduling. Host
wall time is not substituted for 4.77 MHz guest cycles. This is an educational
calculator running substantially slower than real time on an XT, not a live
SPICE instrument. Larger steps make previews faster and reduce timing accuracy.
The engine yields at no more than 16 numeric work operations per slice and
reuses topology/LU where valid; it does not hold the graphics lock while solving.

The measured 68k flasher period is 1.008000 s and duty 53.571%, against independent
ln(2) estimates 1.011995 s/53.425%. Editing RB to 10k gives 0.207000 s/66.667%,
against 0.207944 s/66.667%. Each used three settled periods and met 2% bounds.
Both timestamped channels varied; real timing-wire removal stopped edges,
Undo/Reset restored them. No filename or canned waveform controls the solver.

## Reproduction and evidence

```sh
make breadxtdisk breadxtbundle breadxtcheckdisk
python3 tools/os88test.py soak -k 'breadxt*'
```

For interactive launch, insert build/breadxt360.img as B: beside the bootable
system disk (make run RUNAPPS=build/breadxt360.img is the QEMU launch command;
period validation uses the registered MartyPC rows). Open BREADXT.O88, then an
ordinary BBX file or File/Examples. Build products stay under build/.

The native checks include real placement/bent-wire editing, LED330→680 with
Undo/Redo, stable A/B probes, native saves, independent flushed FAT/BBX readback,
a cold boot and BBX association that runs the saved circuit, all three actual
adapters and 322×128 minimum frames. WPK verification uses the canonical reader,
fresh FAT installation, file-by-file comparison and boot from its BREADXT folder;
it does not claim a network download or guest library installation.

Final screenshots, command results, file hashes and suite status are collected
in [the evidence record](EVIDENCE.md). All four kernel budget builds pass.
**kern_mega runtime is unverified:** neither Bochs nor the automated 86Box
instrument is installed here. QEMU is not claimed as a strict protected-mode
substitute. The common C SDK path is implemented and passes the mega package
gate; physical hardware and strict mega runtime remain separate checks.
