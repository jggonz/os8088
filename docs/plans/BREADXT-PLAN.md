# Breadboard XT implementation plan

Status: both milestones are implemented. Host core/UI and native editor,
DC/digital/RC, VGA/CGA/Hercules, minimum-window, 256 KB and WPK installation
checks pass. Both the long native flasher pass and registered scoped verification succeeded;
final LED replacement-cache repair and pixel assertions also pass; the ledger below distinguishes these from completed evidence.
Strict kern_mega runtime remains unavailable locally; all four kernel build
budgets pass. There is no scope cut or remaining user decision.

SPEC.md section 130, [the contract](../breadxt/CONTRACT.md) and bb.h preceded
implementation. The earlier estimates below are the design record; measured
sizes and limitations belong in [developer notes](../breadxt/DEVELOPER.md).
See [independent plan reviews](BREADXT-PLAN-REVIEWS.md),
[implementation reviews](../breadxt/reviews/) and
[the build diary](../HOW-BREADXT-WAS-BUILT.md).

## Outcome and decisions

Build `BREADXT.O88` in `apps/breadxt/`: an original native breadboard editor,
DC/four-state digital simulator, backward-Euler RC simulator, pin-level ideal
NE555 and two-channel scope. Deliver both required milestones locally, with four
software-disk geometries, a verified WPK and installed-package evidence. Do not
push, open a PR, publish a release or change a website. The user's explicit
local-only instruction overrides the port skill's external-delivery steps.

The user supplied the product name, reference, concept, scope, hardware baseline
and complete required story list. There are no remaining intake questions.
The reference is inspiration rather than the UI authority; the requested native
workbench and concept control appearance. There is no scope cut and no proposed
kernel/API change. Name search found no existing Breadboard/BBX/BREADXT surface.

Use five native menus: File, Edit, Parts, Simulate, View. Native `MENU_APPMAX=5`
prevents six, and the user explicitly says “as supported by the shell.” Put Help
under View and use the standard application About entry. Do not paint a second
application menu bar. Native controls, opaque bitmap text, file picker,
save/discard/cancel alert, clipping and damage own their existing OS semantics.

Study upstream commit `0ba38d35a9f7b9cd53a226865d20b7d94faff948` without copying
code, XML models, expressions, footprints, artwork or font data. Its GPL-3.0 root
license and GPL-3-or-later About text provide no permissive backend exception.
Keep the repository's MIT license. Credit David Shah's Breadboard Simulator as
workflow inspiration in About and developer provenance; clearly state that the
implementation and project format are original.

## Authority table

| Surface or behavior | Authority and inspected reference |
|---|---|
| Product hierarchy, tools, palette, inspector, scope | User's build prompt and Breadboard XT concept image; adapted to real native geometry |
| Native interaction, file picker, About, close negotiation | SPEC 11, 12, 20, 38, 54, 75; apps/cc/ccsmoke, apps/loom/lmui.inc, apps/cword |
| Board physical workflow and separated electrical/document identities | Independently reimplemented after studying upstream SimGUI/Breadboard.cs, Circuit.cs, Component.cs, Wire.cs |
| Plot/probe separation | Upstream SimGUI/Components/MeasurementProbe.cs, GraphView.xaml.cs, GraphSettings.xaml.cs, TraceDisplay.cs; no copied implementation |
| Solver architecture study | Upstream SimBackend/DCSolver.cpp, Math.cpp, TransientSolver.cpp, Capacitor.cpp, LogicGates.cpp; dynamic C++ engine is not adapted |
| NE555P pin/function table | TI SLFS022K, March 2026, https://www.ti.com/lit/ds/symlink/ne555.pdf |
| SN74HC00N | TI SCLS181H, https://www.ti.com/lit/ds/symlink/sn74hc00.pdf |
| SN74HC04N | TI SCLS078H, https://www.ti.com/lit/ds/symlink/sn74hc04.pdf |
| SN74HC08N | TI SCLS081J, https://www.ti.com/lit/ds/symlink/sn74hc08.pdf |
| SN74HC32N | TI SCLS200F, https://www.ti.com/lit/ds/symlink/sn74hc32.pdf |
| SN74HC86N | TI SCLS100F, https://www.ti.com/lit/ds/symlink/sn74hc86.pdf |
| Arithmetic | Shared apps/os88fp.inc, SPEC 84, pure 8086 software path |
| Distribution | Existing CC_PACKAGE embedded overlay, os88disk.py and tools/os88wire.py --archive/--verify/--dump |

## Observed SDK facts, distinct from proposed application limits

Observed: SmallerC tiny mode has signed 16-bit int, signed char, 16-bit pointers
and no usable C long/float/double. All addressable objects and out parameters are
static. Struct copying/pass/return by value is forbidden. CS=DS is the package;
SS differs. ES belongs to the kernel. Use SDK window accessors, never C window
record dereferences. The actual image+BSS cap is 65,024 bytes (older skill prose
says 61,440). The UI stack is 512 bytes, and the 96-byte per-function frame gate
is not a proof of total call-chain safety. Keep scalar frames small and chains
shallow; inspect generated code and full static stack chains including assembly.

Observed: callbacks ordinarily hold the graphics lock; `os88_onwake` does not.
The file-picker completion copies its filename then posts wake. Files, parsing,
solver compilation and arithmetic run in wake, never paint/key/timer callbacks.
Main banks an association launch name; its first wake loads it. Whole-file native
writes allocate a replacement FAT chain and commit its directory entry before
freeing the old chain. Reuse this; never delete the destination first. Preserve
dirty state on reported failure and describe the post-commit failure caveat.

Observed: VGA is 640x480, CGA 640x200 and Hercules 720x348. Query the window's
actual display and content size; native clipping applies. kern_mega is supported
by the ordinary C runtime and must be tested. `onresize` invalidates layout but
does not draw. Timers are absent on kern_small; a bounded self-posted wake gives
a fallback while Run is active. All numerical/compile and file work is reachable only from lock-free wake,
including Run/Step command handoff. No worker is required, eliminating static-scratch
races and the worker prohibition on files/overlay calls.

Proposed final limits, subject to measured build and explicit published revision:
30 columns, 420 holes, 64 parts, 128 wires, two route bends, 64 active nets,
24 unfixed analog nodes, 16 complete undo commands, two 256-sample channels.
DIP count is bounded by part slots, valid nonoverlapping footprints and active
net capacity, not a promise that 64 fourteen-pin packages fit on the board.

## Shared document and topology contract

The lead owns `apps/breadxt/bb.h` and SPEC. Workers propose changes to the lead; they do not
race edits to the contract or Makefile. One C translation unit includes each
concern. Every included file and assembly include is an explicit prerequisite.

Use byte-stable compact records, not object graphs:

```
BBPart: 16 bytes
  unsigned id, type, pin[3], value, param, flags;
BBWire: 16 bytes
  unsigned id, a, b, bend[4];
  unsigned char color, nbend;
BBNum:8bytes, opaque IEEE binary64 record, four BBWord words;
BBDoc/BBResult/BBScope and exact globals/prototypes are pinned in bb.h;
Time: unsigned lo/hi fields in BBResult/BBScope, in100-microsecond units;
```

`id=0` is an unused slot. Stable nonzero document IDs survive movement, parameter
changes, undo, save and reload; exhaustion refuses rather than wraps or reuses an
ID belonging to a surviving object. Selection/probes bind IDs, physical holes
or (part ID,pin), never array addresses or current net indices. Net indices are
compiled derived data and must not be persisted as identities.

Hole `h=row*30+column`, zero-based column 0..29. Rows 0/1 are top +/- rails,
rows 2..6 are a..e, rows 7..11 are f..j, rows 12/13 bottom +/- rails. At each
column, a-e share one conductor and f-j another. Four rails each break between
columns 15 and 16, giving eight explicit rail segments. This is the BBX-v1 board
profile; persist its profile/geometry/break metadata, mark breaks on the glass,
and reject unsupported metadata instead of assuming another board layout.
Physical strip identities comprise 60 terminal strips plus eight rail segments.
Only touched compiled roots count toward the 64 active-net limit.

A DIP spans e/f. Its anchor and 0/180-degree orientation determine physical
positions; no chip-body drawing coordinate participates in electrical logic.
With notch left, low pins 1..N/2 advance left-to-right along f, high pins continue
right-to-left along e. Rotating 180 degrees moves the same numbered pins to the
opposite side and preserves their signal identities. Reject other rotations for
DIPs. Two-/three-lead parts use explicit pin holes. A single shared pin accessor
serves placement validation, occupancy, rendering, persistence and net extraction.

One inserted lead per physical hole, including wire endpoints; probes consume no
hole. Neighbor holes on the same conductor remain available. Preview/cancel is
not a document mutation. Closed ideal switches union conductors. Resistors,
LEDs, capacitors and ICs do not union pins. A potentiometer's exactly zero-length
end segment is an explicit ideal short in compilation, never division by zero.

Wire bends are orthogonal drawing-only routing metadata. Crossings are not
junctions. Endpoints are explicit holes. Reject duplicate endpoint pairs,
self-connections, invalid bends and one-more-than-capacity without dirtying.
Moving a part leaves independent physical-hole wire endpoints in place; this is
explained in help and undoable as one object edit. Deletion likewise leaves wire
objects in place, because wires attach to holes rather than object references.

Union-find chooses the least physical strip identity as representative, then
numbers active roots in ascending order. Electrical net extraction is thereby
deterministic across wire order, route bends and equivalent placement orders.
An unallocated hole-net entry is 255; 0..63 are valid compiled net indices.

All model edits go through validated transactions: add/change/remove part or
wire. An undo record stores complete before/after object state and revision
identity, at most 128 bytes. Ring size 16; discard oldest whole record. New edit
after Undo drops redo. Invalid/cancelled commands do not increment revision.
Save captures a revision identity so Undo back to the saved document can clear
the dirty indicator. Empty/new/load resets history deliberately.

## Numeric and simulation contract

Choose existing software IEEE arithmetic rather than invent a private wide-
integer engine or use forbidden C float. Package cdecl adapters pass pointers to
static BBNum records to os88fp.inc. The host builds the same C topology/stamps/
state machine with arithmetic adapters; a native-double adapter alone is not
proof of the guest arithmetic. An actual guest numeric fixture and negative
control must independently check the adapters/shared soft arithmetic.

Document units: resistor/pot `value` is 10 ohms, accepted 10..65535 (100 ohms to
655350 ohms); cap `value` is nF, 1..60000; cap `param` is initial mV; pot `param`
is 0..1000 permille. Supply `value` is mV, 4500..6000; initial default 5000.
Clock period and duty have separately documented bounded units. Parse only exact
documented numeric grammar/suffixes and reject negatives/overflow/unsupported
precision before mutation. Do not multiply large encoded values in 16-bit C.

Numerical stamps use volts, siemens, amperes and seconds in opaque BBNum.
The bridge accepts only canonical zero or finite nonzero magnitudes in
[2^-64,2^40); it forces fp_hw=0 on every public entry and never calls fp_init.
Check finite exponent/range and division status before packing; bb_nop returns
1success/0failure, leaves destination untouched on failure, and supports either
input aliasing the output. bb_ncmp returns-1/0/1. Integer rounding is nearest,
ties away from zero; bb_nround sets separate sticky bb_num_error on overflow.
Unsigned conversion handles65535 without signed narrowing.

Use absolute pivot threshold1e-12S and row-relative threshold1e-12 times that
row's largest absolute coefficient: a pivot must exceed both. Partial pivoting
selects the largest absolute candidate. Validate unrounded-solution KCL at each
unknown node with tolerance1e-9A+1e-7*sum(abs(incident branch currents)); never
validate only rounded mV. Source/reference rows use voltage residual <=1uV.
Reject ill-conditioned/singular/range failures rather than widening tolerance,
adding GMIN or hidden resistors. These constants have substantial separation
from the minimum supported ordinary conductance1/655350S; pot endpoint ideal
segments are topology, and nonzero fractional segments use their exact scale.
The independent boundary/random fixtures challenge the chosen envelope.

Published analog voltage domain is0..6000mV. Permit at most1uV numerical
excursion outside0..6V before clamping solely that excursion and rounding to
nearestmV (<=0.5mV conversion error). Valid branch currents are[-30000,30000]uA,
including total source current; compare the opaque unrounded current against
0.03A before signed conversion. A source parameter accepted by the editor is
not a promise every resulting circuit is accepted (e.g.6V/100ohm exceeds the
supported current). mv=-1floating,-2unknown,-3failed; ua=-32768invalid. Test
status before plotting, formatting, brightness or arithmetic. A global stale or
failed result never acquires plausible readings from sentinel conversion.
Current is pin1-to-pin2 only for supported two-terminal branches/source;
pot/IC multi-terminal current is N/A (not an invented scalar).

Ground-referenced ideal sources and ideal driven digital outputs fix node
voltages. Use at most 24 unknown node voltages and partial-pivot Gaussian
elimination. Matrix is 24x25 BBNum, 4800 bytes. Preserve no second full matrix:
validate the solution by re-evaluating branch stamps and KCL residuals using
original circuit/state. Bounded nonlinear LED iteration (at most12 complete active-set passes) either
converges or returns an actionable nonconvergence diagnostic. The binding SPEC LED
model is 2V threshold plus explicitly documented10-ohm on resistance, reverse
blocked. A5V/330-ohm/LED circuit should therefore give3/340A=8.824mA; explain
the2.9% correction from ideal9.09mA. Detect an ideal-source LED path with
no external supported current limiting. Do not simulate avalanche or damage.

Compile reports distinct no-ground, disconnected/floating, fixed-voltage
conflict, hard supply short, singular, nonconvergent, numeric-range, bad-IC-power,
contention, unsupported-bridge and oscillation/event-budget diagnostics. Attach
stable offending IDs/holes when possible. Published results have explicit valid,
stale, paused, floating, X/Z, conflict or failed status; invalid never reads 0V.

Four-state logic is 0/1/X/Z. Inputs use LOW <=0.3VCC, HIGH >=0.7VCC, X between;
undriven Z is unknown when consumed. AND(0,X)=0, OR(1,X)=1, otherwise unknown
propagates conservatively; NAND complements AND, XOR requires known inputs.
Each package uses real GND pin7/VCC pin14. Quad maps are (1,2,3),(4,5,6),
(9,10,8),(12,13,11); HC04 is (1,2),(3,4),(5,6),(9,8),(11,10),(13,12).
Maintain explicit driver resolution, equal multiple drivers handled deliberately,
and conflicting outputs diagnosed. Use a deduplicated64-entry net-event queue, at most1024 gate/event evaluations
per completed time step. Exhaustion diagnoses oscillation and discards pending
state. Equal valid ideal drivers resolve idempotently; unequal valid drivers
produce contention. X/Z contributes no fixed voltage and cannot become0V.

Pure digital-only undriven input/output nets do not enter Gaussian unknowns:
they remainZ, with conservativeX consumption (AND(0,Z)=0, OR(1,Z)=1). A floating
passive analog island diagnoses separately. Pin identities: POWER1positive/2
return, LED1anode/2cathode, CAP1positive/2negative, POT1/2ends/3wiper;
resistor/switch1/2; GND/input/clock/indicator onepin. BB_ROT exchanges two-lead
destinations, pot ends but not wiper, or rotates DIP180; it is inert for onepin.

Enforceable mixed-signal rule: support fixed HC/555 outputs driving passive
resistor/LED/capacitor loads, and thresholded HC inputs fed by a feedforward
analog region. Build dependency edges input-to-HCoutput and analog-path edges
through passive branches, excluding traversal through fixed supply/ground rails.
If a dependency strongly connected region contains both a HC combinational
edge and a nonideal analog-path edge, reject BB_EBRIDGE. Pure digital cycles
use the bounded event policy. The 555's internal latched dynamic feedback is
not a combinational HC edge, so its required timing RC/discharge path remains
supported. Conflicting ideal outputs/references are diagnosed by driver
resolution. An analog region fed by onlyX/Z drivers stays invalid/floating,
never receives an invented0V source. Include supported loaded/feedforward
HC and555 examples, and an analog HC-feedback negative fixture.

Operation errors are isolated: bb_error is the most recent editor/codec status;
failed structural edits/loads preserve all running/valid/compiled/dynamic state.
Numeric bridges use separate sticky bb_num_error cleared only at explicit solver
boundaries. Committed simulation diagnostics are bb_result.diag; unrelated
editor status cannot poison a pending solve. A failed step commits no capacitor,
latch, clock, scope, timestamp or timing measurement state.

Run compiles only when model generation changed. Pause preserves dynamic state
and captures; Step advances one quantum; Reset restores documented initial cap/
latch/clock state and clears capture/timing measurements. Editing first cancels
pending work, pauses, marks results stale and invalidates compiled generation.
A compiled snapshot consists of validated generation-bound connectivity/stamps;
drawing never reads in-progress numeric scratch. Publish one complete result
only at a successful step boundary.

Use simulation time in unsigned hi/lo pairs at 100us resolution. Default step is
10 units (1ms); fixture convergence uses 5 units (0.5ms). This avoids claiming a
convergence test while the timestamp representation cannot express a smaller
step. Time-pair helpers propagate carry explicitly without C long, modulo2^32.
Comparable elapsed intervals must be<2^31units; reset/rearm measurements when
ambiguous. Legal dt1..1000units, clock period2..60000units,duty1..999permille,
scope_div10..60000units/div, panx0..29,pany0..13,zoom1or2,trigger0free/1rise/2fall.
Publish quantized/undersampled status if a clock edge lies between steps.

Backward-Euler capacitor companion: G=C/dt, equivalent history current from the
previous pin1-minus-pin2 voltage. Signed orientation and initial conditions must
be covered by independent fixtures. Capacitor state advances only after a whole
successful step. Reuse stable stamps/factorization where valid; invalidate on
switch, LED/discharge state or topology change. Recompute small networks if
necessary for correctness, then measure real XT cost rather than claiming reuse.

NE555 pins: 1 GND,2 TRIG,3 OUT,4 RESET,5 CONT,6 THRES,7 DISCH,8 VCC. Model only
4500..6000mV power. Active-low RESET dominates (document chosen ideal 0.7V reset
threshold). With reset released, TRIG<CONT/2 sets high first; otherwise
THRES>CONT resets low; otherwise hold. Equality holds. OUT is an explicitly ideal
rail driver, not a claim of real output drive. DISCH is a modeled switched
connection to ground in the same analog system. CONT is nominal 2/3VCC and
supports unconnected or its normal ground bypass; reject arbitrary external
control drive explicitly. Removing power/reset/timing wiring affects actual
pins/stamps, never a named-file oscillator.

The flasher example's independently checked graph: pins8/4+RA.1 to VCC; pin1,
timing-C negative, bypass negative, LED return to ground; pin7+RA.2+RB.1;
pins2/6+RB.2+timing-C positive; pin5+bypass positive; pin3 through 330-ohm/LED to
ground. RA10k,RB68k,C10uF,bypass10nF,5V. Settled oracle: high0.5406548s,
low0.4713401s,f0.9881473Hz,duty53.4246575%; exclude startup. Require <=2%
frequency/duty error at justified timestep, RB-change direction and broken-path
failure. Formula is test-only, not production behavior.

Make the numerical engine a resumable state machine. A wake spends at most a
fixed work budget (at most16 work units), then returns
to the UI loop. Each conversion, arithmetic operation or numeric comparison costs one unit;
each digital gate/event evaluation or one union/classification/validation object
visit costs one unit. Stamping, residual and elimination resume between those
operations; do not hide a whole branch stamp or matrix row inside one unit.
Constant-index/counter assignments and bounded tiny tuple dispatch are overhead,
not a license for a separate unbounded loop. Gaussian elimination resumes inside
row/column loops; a whole
24x24 elimination row is too much to assume responsive on an XT. Timer only
posts wake and re-arms. No graphics/file calls occur under solver loops. Render
at an independent capped OS-tick cadence, and publish simulated time plus honest
calculation speed/lag. Never claim real-time from a 1ms simulation quantum.

## Scope and rendering

Two probes bind holes or stable part pins; a branch-current probe additionally
shows pin1-to-pin2 sign and units. Capture uses one shared 256-entry 32-bit time
ring and two 256-entry signed-mV rings: 2048 bytes. Explicit validity/status must
travel with or be derivable for every sample. A bounded edge-time ring or
per-sample edge flags preserves/marks transitions between sample times. Do not
present undersampled digital activity as a precise continuous trace. Basic
rising/falling trigger uses channel0,threshold half the validated supply,
128samples of pretrigger history and128posttrigger samples, then holds that
capture while simulation continues. Run/rearm clears only the capture latch;
Pause holds it; Reset clears samples/measurements. Free mode always rings.
A transition with insufficient prehistory is labeled as such.
Measurements use edge timestamps, never display pixels, and say insufficient
cycles until enough transitions exist. Default555 capture cadence about20ms gives
a5.12s window; scale labels come from actual endpoints/divisions. Pause holds;
Reset clears. Channels use solid/dashed or named patterns in monochrome.

VGA: compact toolbar above palette/board/inspector and scope dock. Hercules uses
its actual width with readable labels. CGA: compact toolbar, board panning and
switchable palette/inspector/instrument views; do not scale the VGA screen into
illegible text. The smallest window clamps every widget/hitbox to content size.
A finite two-level zoom is optional; required panning and physical hole identity
remain independent of pixels. Tool previews show actual pin destinations and
reject invalid insertion before commit.

Use no full-screen software framebuffer. Compose repeated board holes/rails into
small1bpp bands in assembly, with one BLIT1 per row/band. Component artwork is
original deterministic drawing data. Cache status/inspector text, selected-net
state and LED brightness; invalidate old/new object rectangles. A numerical step
must not repaint the whole board. Scope samples at numerical cadence but draws
only at capped presentation cadence. Menus/dialogs/covered windows invalidate
appropriate cached pixels so the shadow cannot lie. Bound all paint loops and
inspect both monochrome modes, especially disabled states and clipping.

## Exact interface handoff

The implementation must use the published bb.h names, not draft private APIs:
`bb_clear`, `bb_validate`, `bb_pincount`, `bb_pin`, `bb_strip`,
`bb_edit_part`, `bb_edit_wire`, `bb_delete`, `bb_undo`, `bb_history_clear`,
`bb_invalidate`, `bb_compile`, `bb_part_name`, `bb_diag_text`;
`bb_nset`, `bb_nuint`, `bb_nop`, `bb_ncmp`, `bb_nround`;
`bb_sim_start`, `bb_sim_slice`, `bb_sim_reset`, `bb_scope_reset`,
`bb_scope_sample`; `bb_decode`, `bb_encode`, `bb_parse_value`, `bb_example`,
`bb_example_name`. `bb_sim_slice` returns0pending,1committed,-1failed;
model/codec success returns1 except encode length. bb_doc/bb_stage are packed
live/staged source; bb_result is committed publication; bb_hnet and bb_nnet are
compiled topology. Input/clock/pot/cap parameters and sentinel semantics are
exactly those in CONTRACT.md. Other modules propose changes to the lead.

## Persistence and distribution

BBX-v1 uses a48-byte little-endian header, followed by16-byte part records
then16-byte wire records. File length must equal48+16*(np+nw), maximum3120.
Serialize fields bytewise; never dump C structs. Header offsets:

| Offset | Bytes | Value |
|---|---:|---|
|0|4|ASCII BBX1|
|4|2|version1|
|6|2|header length48|
|8,10,12|2each|part count,wire count,next_id|
|14,16|2each|channel0/1 hole binding|
|18,20,22|2each|panx,pany,zoom|
|24,26,28|2each|dt,scope_div,trigger mode|
|30|2|board profile1 (30columns,split15 rails as defined)|
|32,34|2each|channel0/1 probe_kind|
|36,38|2each|channel0/1 stable partID|
|40,42|2each|channel0/1 pin number|
|44,46|2each|reserved zero|

Probe kind0 is a hole (BB_NONE means unbound); kind1 is stablepartID/pin;
kind2 is stablepartID/branch. Nonapplicable binding fields are canonical zero
except absent hole=BB_NONE. Structural validation rejects dangling IDs/pins.
Deleting a probed part clears its binding in the same undo transaction;
Undo restores both object and bindings. Selection never retargets a compacted
array index. Profile mismatch and future versions are rejected. No derived
netlist or solver state is authoritative source. Add a host inspector using
this exact byte table. Reject extra/truncated records, counts, duplicate IDs,
invalid enums/parameters/footprints/occupancy/bends and reserved bytes before
commit; failure leaves live document, dirty revision, history and simulation
state untouched. bb_validate is structural only: partial/broken circuits can
be saved. Electrical acceptance belongs to compile/simulation.

Stable IDs span1..65534 across both object kinds; next_id is greater than every
live ID and in1..65535. next_id65535 means exhausted and addition refuses.
Never wrap/reuse an ID. Unused part pin fields are BB_NONE; unused bend words
and type-inapplicable value/param/flags are zero. Wire color0..15,nbend0..2.
Consecutive repeated route points are canonicalized before mutation; endpoint
self-connections and duplicate unordered endpoint pairs are rejected. Every
route segment is orthogonal; bends remain electrically inert.

Use a dedicated 3120-byte file buffer and second staged document, not a second
full solver state. Save through the native whole-file write; only reported
success clears dirty. Bank file-picker result under lock, parse/read/write on
wake. New/Open/Example/Close share native save/discard/cancel negotiation, and a
failed/cancelled save must not execute the pending destructive action.
Use a request token consumed only by the initiating successful save. Since
native SaveAs Cancel calls no completion, the next parent user command/key/click
clears an outstanding uncompleted SaveAs action; paint/timer/wake cannot fire it.
Old filename/place/saved revision remain until successful write. A later ordinary
Save cannot inherit a canceled New/Open/Example/Close request.

Ship ordinary editable LEDTEST, DIVIDER, POT, SWITCH, LOGIC, BROKEN, RC and
FLASHER.BBX examples, notes and HELP.TXT. Generate physical layouts through the
same model/loader validation, and independently assert extracted logical graphs.
No special filename behavior. Examples/help resolve from installed package
location; exercise association launch from another document folder. Prefer
embedded resources or proven place-preserving lookup so Save As cannot strand
Help/examples in a previous folder. The overlay is an eager embedded part of
BREADXT.O88, so copying the executable cannot separate code from its module.

## Memory budget — estimates, not measurements

| Object | Proposed bytes |
|---|---:|
| Live BBDoc:34-byte metadata+64x16+128x16 |3106 |
| Staged load BBDoc |3106 |
| BBX file buffer |3120 |
| Undo16x128 |2048 |
| Analog augmented matrix24x25x8 |4800 |
| Numeric temporaries, state, stamps, published volts/currents |2048 allowance |
| Hole-net/union/occupancy and event queue |1536 allowance |
| Scope shared timestamps+two voltage rings |2048 |
| Edge/validity/trigger state |512 allowance |
| Small drawing bands |1536 allowance |
| UI/static strings/input/view/cache state |1536 allowance |
| Total planned BSS |25396 |
| Resident code/data/runtime/numeric shared source |~26000 estimate |
| Resident total |~51396, below55k trigger |
| Eager embedded overlay |~16000 estimate, separate heap claim |
| UI stack |existing512, full call-chain margin must be checked |

CWORD's reported ratio is an estimate basis, not a size guarantee. At resident
image+BSS>55000, next work moves infrequent per-command code into the existing
overlay before more features. Never raise APP_MAX_SIZE or kernel budgets. The
matrix may be kept in the region; if a buffer moves to a separate claim, declare
it movable, repair every cached segment and pin it for file transfer. Avoid
that complexity until measured size requires it. Final table replaces every
allowance with actual symbols/build bytes, adds rounding/overlay/claims, and
reports largest free block on640KB and256KB machines.

## Ownership and waves

Lead alone owns SPEC, bb.h, Makefile, shim configuration/package metadata and
integration decisions. Workers have exclusive files; no implementation begins
against an unpublished header. Implementer/fixer writes are sequential when
they touch one include/TU surface; independent module files can be prepared in
parallel under the user's requested ownership and integrated one writer at a
time. No recursive delegation. Record agent IDs, roles, reports and states in a
session journal so all five bounded responsibilities are actually staffed in
waves within available slots.

| Owner | Exclusive surface and deliverable |
|---|---|
| Model | model.c: pin access, strips, topology, validated commands, undo; fixture proposals only |
| Simulation | sim.c and numeric.inc: DC/digital/transients555/scope samples; no shared header edits |
| UI | ui.c and draw.inc, package-local assets: layouts, interactions, inspector, scope rendering; callback proposals to lead |
| Persistence/examples | file.c, examples.c, examples/, HELP.TXT and host inspector; staged reader/writer and independently documented layouts |
| Verification | hosttest/, tests/breadxt*.py and reports: independent oracle, UI scenarios, screenshots, release inspection; challenge implementation |

Wave1 — CheckpointA, working native document vertical slice (US01/02/10 base).
Create shared contract, native window/toolbar/board/menus, minimal placement and
selection, dirty title/close negotiation, original icon and validated BBX
save/reopen through native dialogs. Include model/parser host harness from the
start. Launch installed app and save/reload a minimal project on MartyPC VGA;
inspect screenshot. Host suite must reject trench/rail-break/invalid-file cases.
Do not call milestone1 complete here.

Wave2 — CheckpointsB/C, complete editor/DC/digital (US01–12).
Finish all supported part footprints, wiring/bends/selection/movement/value
validation/undo-redo/probes/help/examples; bounded DC/LED/pot model and all five
HC packages plus clock. Run LED-through-real-UI placement/wiring/probe/save and
reload, edit330ohm value and undo/redo; independently assert electrical values.
Inspect actual CGA/Hercules layout, native menus/dialogs, net selection and errors.
At completion all milestone1 acceptance has explicit evidence or remains open.

Wave3 — CheckpointsD/E, RC/555/scope and final installation (US13–16).
Add/finish cap initial conditions, resumable transient engine, realpin555 model,
scope trigger/time measurements, editableRC/FLASHER resources, all geometry disks
and verifiedWPK. Final installed app run saves, reboots/reloads/runs555, changesRB
and breaks timing/reset/power; capture output and capacitor together. Check all
three adapters, minimum windows, 640KB/256KB and ordinary binary on kern_mega.
No enabled placeholder controls and no untested “done” story. Both milestones
are required; any open required story keeps the assignment incomplete.

Each wave: host build/core checks plus one boot to look; three independent
review lenses (rules/fit+stack, redraw cost, product/electrical fidelity), bounded
fix/review round, then fresh independent verifier rebuilds and looks. Preserve
review findings and actual sizes/screenshots. Record affected registered rows;
run their deduplicated soak union and test-full only after the final batch.

## Checked story evidence ledger

Checked from implementation and independent results, not from wave labels.
Host arithmetic and actual8088 evidence remain distinct. Each row names the
checks performed rather than implying every case ran on every adapter.

| Story | Outcome | Implemented | Host verified | Guest evidence |
|---|---|---|---|---|
|US01|Native workbench/files/close/help|[x]|[x]|[x] Native launch, menus, Help and staged-file workflows; host modal/close guards|
|US02|Strips/trench/split rails/net selection|[x]|[x]|[x] 420-hole/rail/topology oracle; native mono board and diagnostic selection|
|US03|Valid pin placement/polarity/rotation|[x]|[x]|[x] Native Power/Ground/R/LED placement from empty, two wires, solve/save/reload; all footprints and both DIP orientations in core|
|US04|Wire endpoints/bends/editing|[x]|[x]|[x] Native bent wire, Undo/Redo; topology/crossing/edit cases in core|
|US05|Inspector/value grammar/Undo/Redo|[x]|[x]|[x] Native LED330→680→Undo/Redo/Undo; minimum value dialog; grammar/overflow core|
|US06|Bounded real DC/LED/pot diagnostics|[x]|[x]|[x] Native8.824/4.348mA LED and24-node ladder; independent resistor/pot/LED oracle|
|US07|Five HC maps/four-state gates/clock|[x]|[x]|[x] Every gate/pin/orientation in core; actual8088 HC thresholds and B-clock edges|
|US08|Run/Pause/Step/Reset/bounded batches|[x]|[x]|[x] Native Pause/Step/Reset; Help during24-node elimination; slice≤16 operations|
|US09|Stable voltage/current probes|[x]|[x]|[x] Native A/B pin binding and signed current; host stable-ID/branch cases|
|US10|Staged BBX/save/reboot/association|[x]|[x]|[x] Guest-written RUNLED.BBX, independent FAT/BBX read, cold association and solve|
|US11|Editable examples/tutorial|[x]|[x]|[x] Eight generated/graph-checked ordinary BBX files; installed examples and Help|
|US12|VGA/CGA/Hercules/minimum layouts|[x]|[x]|[x] Actual640×480,640×200,720×348; native322×128 frame;256KB full-capacity run|
|US13|RC transient/convergence/initial state|[x]|[x]|[x] Actual8088 RC101ms3170mV vs BE3169.746; host convergence/discharge/reset|
|US14|Pin-level555/settled timing/rewiring|[x]|[x]|[x] Native68k1.008s/53.571%,10k.207s/66.667%; wire Delete stops edges; Undo/Reset recovers; registered native pass succeeded|
|US15|Two-channel timestamp scope/trigger|[x]|[x]|[x] Core wrap/trigger/edge oracle; target A/B traces; actual spatial-dash capture and15-case raster/numeric ABI fixture passed|
|US16|Models/help/limits/attribution|[x]|[x]|[x] Installed Help/minimum views and host About; one-supply/current/LED limits documented|

## Verification and release evidence

Baseline before product edits: actual compiler smoke and overlay gate builds,
smallest native OS boot; record any preexisting failure. MartyPC is the default
for8088 behavior and all three actual adapters, per docs/TESTING. QEMU is only
for its documented faster-CPU/mega capabilities, not proof ofXT performance or
actual mono adapters. Drive via os88ui and guest conditions/ticks, not remembered
coordinates or host-clock sleeps. Stop only this worktree's own emulator.

Host core tests use actual model/parser/solver code and an independent double
oracle for expected electrical values. Cover strip equivalence/trench/rails,
bends/crossings/nojunctions, deterministic roots/IDs, occupancy/deletion/rotation,
undo overflow/branching/cancel, capacities+one, value grammar/overflow, malformed
and failed loads, byte-exact save/load view/probes. Electrical fixtures: divider
2.5V/.25mA; parallel1k5mA each; LED8.824mA and reverse/open/missinglimit;
pot endpoints; every gate on every package includingX/Z,power,contention,
feedback/event exhaustion; floating/singular/hardshort; boundary/random resistor
networks withKCL/residual. Include actual guest numeric wrapper fixtures and
negative controls; host double adapters cannot substitute for these.

Transient independent fixtures: RCtau=.1s, V(.1s)near.632Vfinal with declared
BE discretization and smallerdt comparison; discharge/pause/reset/edit; every555
pin/resetpriority/power/controlpolicy/brokenwire; settled period/duty<=2%,changed
RB; scope ring wrap/timestamps/edge marks/trigger and flatDC. A failing negative
control is required for registered numerical/topology rows.

Final installation scenarios from fresh disk/WPK: launch, open/run/probe; build
or alter actualmouse/keyboard circuit; edit/undo/redo/run; save/reboot/reopen/run;
555output+captrace,RBchange,timingbreak; hardshorterror with responsive desktop;
all adapters' menus/dialogs/inspector/scope/clipping. Retain actual inspected full
and cropped screenshots per adapter and cite what each demonstrates. Measure
MartyPC guest cycles for place/select/valueedit, DCsolve, one transient step,
scope draw and run input latency; separately report simulatedtime and walltime.
Record a256KB attempt's actual claim/free-block arithmetic or graceful refusal.

Build/verify four software-disks with existingos88disk conventions; boot360KB.
Create WPK through tools/os88wire.py --archive with --srcdir, --home BREADXT,
--program BREADXT.O88; --verify and --dump it, then test a fresh installed copy.
Never add a speculative catalog/downloadmanifest entry. Final local artifacts
live underbuild (untracked); source/examples/provenance/docs are reviewable.

Run make/budget/doc gates, exact scoped soakunion and test-full once after last
batch; only failing rows rerun after fixes. Keep preexisting failures separate.
Finish with executable/resource/overlay/totalRAM sizes, actual limits, proven
build/launch commands, inspected screenshot paths and honest remaining gaps.

## Principal risks and mandatory responses

1. Soft double Gaussian work can be slow onXT. Resume inside inner loops, cache
   where correct, cap repaint cadence, measure and show simulation speed.
2. TotalC+numeric+UI+BSS may exceed one segment. Plan embedded overlay now;
   size every build; split frequency rather than cut required stories.
3. An8byte numeric representation can hide errors in asm/host glue. Guest
   fixture plus independent expected values is necessary before claiming math.
4. SS!=DS and tiny stack make superficially ordinaryC unsafe. Gate every build,
   static outparameters, no copying structs, no deep recursive helpers.
5. Monochrome height/labels and per-hole primitives can ruin usability. Small
   bands, actualCGA/Hercules captures and measured damage costs are mandatory.
6. Example electrical geometry can look plausible while wrong. Generate/check
   through physicalpin/topology pipeline and assert a separate logicalnetlist.
7. Active digital outputs and555ideal state changes can invalidate factorization
   or create mixedfeedback. Explicitly recompile stamps and diagnose unsupported
   feedback; never publish halfsolved state.
8. Target soft arithmetic and16bit metadata must match host adapters exactly.
   The reconciled48-byte header preserves profile/view/stable probes; assert
   offsets/length and exercise actual target numeric fixtures before completion.


## Reconciliation and implementation handoff

The three independent lenses are recorded in the session plan-review reports.
All major issues are resolved above without cutting either milestone:48-byte
source format, stable probes, canonical ID rules,30mA range and invalid sentinels,
separate numeric error latch, structural-vs-electrical validation, exact terminal
identities, digital-only participation, executable analog-feedback rejection,
fixed thresholds/budgets and SaveAs request cancellation. Implementer reports
propose fixtures; only the verification owner writes hosttest/ and test rows.

Before implementation the lead must mirror the reconciled policies into SPEC130,
CONTRACT.md and bb.h where applicable. In particular fix the stale CONTRACT
header-length value32 to48. Product files use model.c,sim.c,numeric.inc,file.c,
examples.c,ui.c,draw.inc; lead owns breadxt.c,breadxt.asm,bb.h and buildfiles.
No unresolved user decision or required approval remains. Planned numerical
limits and fit estimates are contracts to verify, not evidence of passing tests.
