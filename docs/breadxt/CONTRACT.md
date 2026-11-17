# Breadboard XT shared implementation contract

SPEC.md section130 is binding. This file is written before product code.
Observed SDK facts: 16-bit int/pointer, SS differs from DS, no addressable
automatics or struct copies, frame gate96bytes, UI stack512bytes, package
image+bss<=65024bytes, five native menus. Wake is lock-free; other callbacks
hold the graphics lock. Whole-file write already performs safe chain replacement.

The lead owns `bb.h`, breadxt.c/.asm, integration includes and Makefile.
Model owns model.c; simulation owns sim.c and numeric.inc; UI owns ui.c and
draw.inc; persistence owns file.c, examples.c, project assets and help;
verification owns hosttest and tests/breadxt*.py. Request shared interface
changes from lead. Never change another owner's files.

## Source and records

`bb.h` pins the exact prototypes and data. One translation unit includes
model, simulation, persistence, examples and UI in that order. Common static
storage is in breadxt.c. No OS calls in model/topology/file format core.
Parts16bytes: id,type,three physical hole fields,value,param,flags (all words).
For DIP, pin[0] is a row-f anchor; pin destinations derive from type and
orientation. For two-lead parts pin[0/1], for pot pin[2] is wiper.
Wire16bytes: id,a,b,four bend coordinates (two (col,row) pairs),color/count
bytes. Bend coordinates use the board's hole grid, no electrical significance.
IDs0reserved, holes0..419; absent hole65535 expressed as BB_NONE.
Flags bit0 orientation180, bit1 closed switch; logic input param0/1/2/3
means0/1/X/Z. Clock value period in100us units and param duty permille.
Pot param0..1000, capacitor param initialmV. Supplyvalue mV;Rvalue10ohms;
Cvalue nF. Other values ignored and must be zero except model defaults.

`bb_doc` is live source; `bb_stage` validates new source; counts are packed
arrays. Object selection is by stable id; indexes are not persistent IDs.
`bb_edit_part/wire` add when index=-1 and replace otherwise; deletes retain
independent wires. Each success calls bb_invalidate, pauses, sets dirty.
Invalid/canceled changes do not mutate or dirty. `bb_compile` canonicalizes
active strips to ascending least physical strip IDs, publishes hole_net and
net count; it does not solve. Ideal closed switches and zero pot segments
are connectivity. Component bodies never union other terminals.

## Simulation

`bb_sim_start(reset)` compiles if dirty, resets only if requested/invalid.
`bb_sim_slice(budget)` advances at most budget numerical work units;0 means
still calculating,1 means one full step published,-1 diagnostic. The UI
continues by future wakes and can Pause/cancel between slices. `bb_result`
is immutable until step commit. `bb_valid` distinguishes stale from legitimate
zero; diagnostics carry stable part ID and net if available.
Eight-byte BBNum is raw IEEE software format via os88fp, no C numeric type.
Host shim provides the same wrapper contract; actual 8088 numeric fixtures
validate the shipping arithmetic independently. No hardware path initialized.
`bb_nset` signed, `bb_nuint` unsigned, `bb_nop` add/sub/mul/div returns1
success0numeric error; `bb_ncmp` returns-1/0/1; `bb_nround` reports rounded
signed integer with an explicit diagnostic on range failure. All out pointers
are statics. Matrix unknowns<=24. Compute residual via circuit stamps/KCL
instead of a second matrix.

## Persistence

BBX1 header48bytes: magic ASCII BBX1; version word1; header-length word48;
part count,wire count,next id,probe0,probe1,pan column,pan row,zoom,dt,scope
division,trigger,board-profile1 (12words), then probe_kind[2], probe_id[2],
probe_pin[2], and two reserved-zero words (8words). Each part record follows above word order.
Each wire record is7words+color/countbytes. Total48+16*(np+nw)<=3120.
Bytewise encode/decode; never native-struct serialization. Future versions,
reserved values, duplicates, invalid footprints/parameters/occupancy, malformed
length/counts are rejected before committing. Decoder never mutates current.
`bb_decode` fills supplied staged document and returns diagnostic; caller
commits only on success. `bb_encode` returns byte length or0. No derived nets
or numerical state saved. A host inspect tool decodes this exact table.

## Delivery and evidence

A native window and minimal save/reopen; B full editor/DC/digital; C RC/555/
scope and package final checks. Both milestones mandatory. No push, external
PR, release, website publishing. Host fixtures and a boot-to-look during
implementation; scoped soak union and test-full once after final integration.


## Plan review resolutions before implementation

Accepted branch-current magnitude is at most30000uA, including sources;
reject larger circuits with numeric range diagnostic before signed conversion.
Published analog domain is0..6000mV. mv=-1 means floating, -2 unknown, -3
failed; ua=-32768 means invalid. Scope samples carry these same sentinels,
never plausible zero for invalid. Probes bind kind0hole,1stable partID/pin,
2stable partID/branch. Store hole, kind,ID,pin in48byte BBXheader; holes are
used only forkind0. Resolve IDs at compile and diagnose dangling references.
Board-profile1 persists the defined30column/split15 fixed board metadata.

Numeric working envelope nonzero2^-64..2^40, inputs created only by checked
integer conversion. Physical voltage0..6V and perbranch current30mA checked
before publication. LED2V+10ohm:5V/330ohm gives8.823529mA. Nonlinear active
sets max12; digital propagation max1024events. Wake budget16scalarops.
Default scope sample every200timeunits(20ms), full ring5.12s; divisor defaults
500ms/div for10divisions. Timers pace painting separately. Plan estimates
are revised against actual buildbytes before claims of fit or performance.

Terminal identities: POWER pin1positive/pin2return (return must share GND),
LED pin1anode/pin2cathode, CAP pin1positive/pin2negative, POT pin1end/pin2end/
pin3wiper, resistor/switch pins1/2. GND/input/indicator/clock have one pin.
For explicit two-lead records BB_ROT exchanges pin1/2 destinations without
changing terminal identity; POT exchanges ends1/2 and keeps wiper3; DIP
rotates180 across e/f. One-pin rotation is inert. bb_pin is the sole accessor.
Pure digital-only nets including unconnected HC inputs remain Z/X and do not
become Gaussian unknowns. Analog-participating floating networks diagnose;
undriven digital pins never silently acquire a0V stamp. Clock/input ideal
levels use the one validated ground-referenced circuit supply.
Ideal IC outputs are separate fixed rail drivers. Their load/quiescent current
is omitted from the power pins; source current covers the external stamped
network at that source node, not an IC power-consumption model.

Error ownership: editor/codec bb_error is operation-local presentation status;
rejected edits/loads preserve running/valid/result/dynamic state. Arithmetic
uses separate sticky bb_num_error, cleared only by solver at an explicit
batch boundary; it never consumes unrelated editor status. Solver publishes
diagnostic only in bb_result after success/failure commit.

Time/view validation: dt1..1000 (100us..100ms), clock period2..60000units,
duty1..999permille, scope_div10..60000units/div, panx0..29,pany0..13,
zoom1or2,trigger0free/1rising/2falling. Clock duty quantization explicitly
marks undersampling if a requested edge falls between simulation steps.
Time pair wraps modulo2^32; measurements require differences<2^31units and
reset/rearm across ambiguous wrap, never signed16 elapsed arithmetic.

Native SaveAs cancellation calls no completion. Retain oldfilename/place and
dirty revision until successful write. A pending destructive action awaiting
SaveAs is canceled by the next parent-window usercommand/key/click if no
completion occurred; paint/timer/wake do not complete it. The filecompletion
latches its pending action and write outcome; failure/cancel never executes
New/Open/Example/Close.

Saved revision is explicit: bb_mark_saved() after successful write; load/new
clear history and mark saved only when source actually represents that state.
Model deletes unbind target part probes to BB_NONE hole mode; Undo restores
bindings atomically. IDs1..65534, next_id65535 is valid exhausted sentinel;
new insertion refuses there. Structural validation never requires an
unfinished or intentionally broken circuit to solve electrically.


## Final numeric/bridge resolutions

Pivot magnitude must exceed both1e-12S and1e-12 times original row maximum.
Use unrounded KCL residual <=1nA+1e-7*sum(abs(incident branch currents));
fixed-node residual <=1uV. Accepted node0..6V allows1uV numerical enclosure
then clamps; branch current<=30mA checked before uA scaling/rounding.
Sixteen work units count each numerical conversion/op/compare, gate/event
evaluation and object visit; inner matrix loops are resumable, not one unit.

Supported mixed bridge graph uses passive-analog path edges without
traversal through fixed rail nodes and HC input-to-output dependency edges.
Reject a strongly connected component containing both an HC dependency and
a nonideal analog edge with BB_EBRIDGE. Pure digital cycles use bounded
event propagation; NE555 latch feedback is explicitly supported and is not
a combinational HC dependency. Unknown drivers impose no fixed0V constraint.

Probe canonical source: unbound=kindHOLE,holeBB_NONE,id0,pin0; holebinding
id0/pin0; partPIN uses stableid,pin1..pincount,holeBB_NONE; branch uses
stableid,pin1,holeBB_NONE. Unsupportedbranchcurrent is explicit N/A.
Undo restores saved content identity but does not rewind monotonically
allocated next_id, avoiding reuse after branched history.

Simulation's equivalent numerical units are V,mS,mA (project/published units
unchanged): GmS=100/Rencoded, capacitorGmS=CnF/(100*dt100us). Thus absolute
pivot1e-9mS, KCLabsolute1e-6mA; relative thresholds unchanged.

The live-media completeness gate requires a built shipping package in the
everything-disk/live payload. BREADXT has its own BREADXT folder in
ALLAPPSFILES/ALLAPPSARGS, containing package, examples and help. This local
build integration publishes nothing and adds no compiler dependency to all.

Persisted probe/view mutations call bb_view_changed(): recompute dirty from
the saved object revision and all saved metadata, without invalidating the
electrical solution. This is separate from topology-changing bb_invalidate().

The bounded source class is exactly one BB_POWER part. A second supply is
structurally savable but simulation returns BB_ESUPPLIES (Use one DC supply).
All supported branch currents use the same internal pin1-to-pin2 convention:
a supplying ideal voltage source therefore reads negative; an absorbing source
reads positive. Source total magnitude remains bounded at30mA.

LED active-set floating policy: ON candidates are re-armed each new numerical
step and after a genuine HC/555 drive change, so initially dark paths can
conduct as referenced capacitor voltages evolve. Positive-current paths retain
their ON stamps. A 1 uV equality band triggers a bounded connectivity check
with the candidate diode removed; it is not a global turn-off cutoff. If both
terminals still have genuine references, retain the boundary state to avoid
weak-forward ON/OFF chatter. If a terminal loses its only reference, release
the artificial clamp. Whole unpowered LED-only components release similarly.
They publish FLOAT/Z and zero current; genuinely unanchored resistor/capacitor
networks retain their floating diagnostic. Stored-capacitor companions are
genuine transient references. OFF LEDs never use floating endpoints as 0 V.
All changes remain within the existing12-pass nonlinear bound.

For exact undo damage, bb_history_object(redo,after,part,wire) reads the next
history command without mutation, returning0 if that requested side is absent
or BB_PART/BB_WIRE after copying its object into caller static storage.
'after' is relative to the requested Undo/Redo operation. UI may call it for
both sides before applying the command, using existing temporary records.

External LED limiting is checked independently of the 30 mA numeric ceiling:
an active LED-only path between different ideal fixed rails is unsupported and
returns BB_ELEDLIMIT, even if the sum of documented diode on-resistances makes
the computed current fall below that ceiling. A resistor in parallel does not
limit that path; a genuine series resistor breaks it. Overrange with a genuine
series limiter returns the numeric/range diagnostic rather than claiming that
no resistor exists.
