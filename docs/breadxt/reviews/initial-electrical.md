# Breadboard XT implementation review — electrical and product fidelity

Verdict: **fix**. Read-only review of the actual model, solver, codec, examples,
UI, contracts, manufacturer-source notes, full user brief and independent host
report. No builds, tests, emulators, product edits or subagents were run by this
reviewer. Findings below are source proofs/fixtures to add, not claimed guest
reproductions. Line numbers refer to the reviewed snapshot; the lead may already
be applying communicated fixes.

## Major findings

1. **Channel B can display aliased digital activity as flat DC without warning.**
   `apps/breadxt/sim.c:1044–1078` detects threshold transitions and forces scope
   samples only from channel A. Reproducible configuration: A probes steady VCC;
   B probes a clock with period200 (20ms), duty500 and dt10 (1ms). The normal
  200-unit sample cadence observes B at the same HIGH phase indefinitely.
   `bb_sclocked` stays false because the numerical step resolves each10ms high/
   low interval. The resulting B trace is flat HIGH with no UNDERSAMPLED label,
   violating US15's preserve-or-mark rule. Track B transitions as well and force
   samples or mark aliasing explicitly. Trigger/period measurement may remain A.
   Add a channel-B-only clock fixture; assert actual B transitions appear in the
   ring or the committed undersampling flag is set.

2. **NE555 Reset priority is defeated by a missing comparator input.**
   `sim.c:887–891` requires valid RESET, TRIG and THRES readings before testing
   RESET<0.7V at898–900. A powered timer with reset grounded and an unwired
   trigger/threshold fails DISCONNECTED instead of forcing OUT LOW/discharge ON.
   The manufacturer's dominant reset is independent of comparator input state.
   Validate/evaluate RESET first; only when it is released require meaningful
   TRIG/THRES. Add reset-low with either/both comparator inputs floating, in
   addition to existing priority tests with both comparator inputs wired.

3. **A zero-current LED can manufacture a plausible voltage for a floating node.**
   `sim.c:579–584` treats every active LED as an anchoring conductive branch;
   `sim.c:865–869` retains the on set at exactly2V. An isolated anode connected
   only through that LED to ground therefore receives a nonunique2V boundary
   solution and0uA instead of FLOAT. The implementation report acknowledges this
   case; it is contrary to US06/09's explicit floating-state requirement. Ensure
   zero-current diode-only paths do not anchor an otherwise unreferenced node
   (e.g. release the zero-current active boundary and reclassify, with a bounded
   tolerance/convergence rule). Preserve valid capacitor initial conditions and
   ordinary positive-current LED results. Test the open-switch LED anode voltage,
   an isolated LED, and a node with a genuine resistor/capacitor reference; a zero
   current assertion alone does not establish a meaningful voltage.

4. **The documented one-supply class is not enforced, permitting wrong HC rails.**
   `sim.c:349–350` overwrites the global supply level for every BB_POWER. HC
   power validation at493–496 accepts actual pin14 at any4.5–6V, but thresholds
   at184–185 and HIGH output stamps at564–566 use the last global supply value.
   Two independently grounded5V/6V sources can therefore make a5V-powered HC
   output silently drive6V and use6V thresholds. This is outside the documented
   one-supply class but is currently accepted without an unsupported diagnostic.
   Either enforce the class with a clear diagnostic before solving, or derive
   each package's thresholds/outputs from its actual VCC. Equal parallel ideal
   sources also have underdetermined per-source current sharing; do not report
   the full node current as an independently determined current for each source.
   Add a differently powered HC plus an otherwise unused second source fixture.

5. **Persisted probe changes are not marked dirty.**
   `apps/breadxt/ui.c:937–946` changes persisted hole/kind/ID/pin fields but does
   not recompute `bb_dirty`. A clean saved project can change a stable probe and
   close without the native save negotiation, losing the binding. Saved metadata
   comparison exists in model.c but is invoked only by object transactions.
   Recompute dirty on probe changes without invalidating the electrical result;
   apply the same explicit policy to other persisted view settings. The lead's
   new `bb_view_changed` contract appears intended to address this. Test clean
   saved project -> change probe -> dirty title/close Save/Discard/Cancel ->
   save/reopen binding, plus undo back to the saved object revision.

6. **Probe presentation omits required logic state and diagnostic navigation.**
   `ui.c:445–508` prints voltage/current but never reads `bb_result.logic` for
   the active probe. A2.5V HC input therefore shows2.500V without its X state;
   an undriven input says floating without explicit Z. Add0/1/X/Z alongside a
   voltage probe, preserving stale/failed labels. Separately, no UI code reads
   `bb_result.part/net` or `bb_error_part/net`; the diagnostic string cannot be
   selected to locate its offending object/net as US09 requires. Provide a real
   diagnostic selection action, bind stable part IDs, and pan/highlight if needed.
   Test intermediate analog input, undriven digital input, contention and an
   error whose affected object is outside the current viewport.

7. **Supply-current sign contradicts the declared pin1-to-pin2 reference.**
   `sim.c:1001–1010` publishes `bb_skcl[positive_net]*1000`. That is current
   leaving the positive supply node into its loads. For BB_POWER pin1positive/
   pin2return, current through the source from pin1to pin2 is the opposite sign.
   The lead's actual target LED reading confirms POWER=+8824uA, matching the
   passive LED/resistor, while the UI and contract label all as I1->2. Prefer
   negating source KCL before publication so the existing uniform reference
   remains true: a supplying source reads-8824uA; positive means absorbing into
   its positive terminal. Alternatively retain positive delivered current only
   with an explicit source-specific "out of +pin1" label and matching contract/
   help/test convention. Do not leave the current generic I1->2 caption.
   Add a simple divider/source sign fixture and a capacitor-driven return-current
   case; assert passive directions and supply energy direction independently.

## Other required fidelity/verification items

- **Physical strip vs complete net styles are identical** (`ui.c:378–388`). Both
  reduce to the same mark bit passed to `bb_board_row`. Use distinct monochrome
  shapes/patterns, retaining a selected-hole marker, so US02's two meanings are
  visible when a wire joins several strips. This was independently noticed by
  the lead and belongs in the UI repair batch.
- **Event exhaustion is bounded but its error arm still needs a positive circuit
  fixture.** The existing inverter ring fixture permits either X or EOSCILLATE,
  so it does not establish the1024-event error path. A useful candidate is NAND
  with output fed back to input2 and a clock on input1, period20/duty500/dt10:
  first low-clock step forces a known HIGH; next high-clock step should oscillate
  under zero-delay semantics and terminate with EOSCILLATE. Preserve the correct
  all-unknown ring behavior; do not invent a physical oscillation from X.
- `ui.c:542` gives an old `bb_result.diag` priority even after an edit invalidates
  the result. Label it as a previous/stale error rather than an active diagnosis
  of the edited circuit. Inspector stale handling is already more conservative.
- Unsupported current range with a real series resistor currently maps any LED
  current>30mA to "LED needs a series resistor" (`sim.c:950–954`). Prefer a range/
  increase-resistance message when a resistor exists; distinguish this from an
  actual unbounded direct-source LED. This is diagnostic clarity, not a silent
  numerical result.

## Sound electrical/core aspects inspected

- Sole physical pin accessor, correct trench-spanning DIP order and rotations,
  explicit split rails, one lead per hole, canonical wire/ideal-switch unions,
  and no unions across passive/IC bodies. Source records and codec preserve
  geometry, stable IDs, parameters and probes; broken documents remain savable.
- Correct HC quad and inverter pin tables, supply pins, conservative X/Z truth
  rules, explicit driver contention, and no fake0V stamp for unknown outputs.
- Correct V/mS/mA BE conductance and history-source signs; nodal stamps and branch
  current direction agree. Partial pivoting, residual validation, working bounds,
  physical current checks and retained LU/permutation logic are coherent. LU
  reuse invalidates on unknown-map or nonlinear conductance changes.
- 555 latch/discharge acts through real nets; control policy is explicit; default
  flasher wiring matches the independently specified seven-net graph. No file-
  name/formula oscillator or stored trace is present. Settled timing is measured
  from transition timestamps, with startup excluded by the supplied host tests.
- Pending cap/latch/clock data commits only after completed validation; failed
  work leaves previous time/capture/numeric state with invalid status. Pause and
  simulation time are separate from wall time. Scope trigger/hold uses actual
  timestamps, and the two traces have monochrome distinction.
- User-facing model bounds disclose the10ohm LED correction and no SPICE,
  transistor, thermal or realistic-output-drive claim. No upstream GPL material
  is copied according to source/provenance inspection.

The independent host suite is valuable evidence but cannot settle target16-bit
ABI/arithmetic, actual display interaction, installed reboot behavior, stack or
XT performance. Those checks remain mandatory before a final completion claim.
