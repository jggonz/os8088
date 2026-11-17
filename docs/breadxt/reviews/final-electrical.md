# Breadboard XT repaired source re-review — electrical/product fidelity

Verdict: **solver source review passes; one minor prior UI issue remains.**
No new blocking mathematical or source-current defect was found. This is a
read-only review of the frozen repaired source and repair reports. I did not
edit product files, build, execute tests, boot an emulator or spawn agents.
Reported 3956 host checks and four private corners are supporting evidence
from other owners, not tests independently run by this reviewer. Target numeric,
ABI/stack, adapter display and performance checks remain required.

## Original major findings

| Finding | Disposition | Source evidence |
| --- | --- | --- |
| B-only digital scope alias | Resolved in source | `sim.c:1289–1295` detects B threshold changes and forces a shared timestamped sample. A retains trigger/period ownership. |
| Reset blocked by floating comparators | Resolved in source | `sim.c:1067–1086` evaluates valid RESET first, then asserted TRIG before requiring lower-priority THRES. Grounded RESET can force LOW with both comparators floating. Actual power and supported control configuration remain validated. |
| Floating LED manufactures 2 V | Resolved within explicit bounded model | `sim.c:612–689` computes genuine R/C reference flags and releases unpowered LED groups. `sim.c:982–1041` excludes an OFF diode's floating scratch voltage, tests a near-zero diode with its own stamp removed, and explicitly refuses mutually anchoring LED-only ambiguity. Abandoned LED-only terminals become FLOAT/Z; genuinely unanchored R/C circuits still fail FLOAT. |
| Multiple supplies silently select HC rail | Resolved in source | `sim.c:356–360` rejects the second BB_POWER with BB_ESUPPLIES before replacing the sole global rail. This also avoids inventing independently determined currents for parallel sources. |
| Persisted probe changes remain clean | Resolved in source | `model.c:672` recomputes saved-state dirty metadata; `ui.c:831–832,1092` routes probe changes through that helper. Pan/zoom/timebase/trigger routes also call it without electrical invalidation. |
| Logic state and error location missing | Resolved in source, glass verification pending | `ui.c:471–483` appends explicit 0/1/X/Z; `ui.c:835–848,1186` provides a real status click that resolves stable part ID or named net and pans/selects a physical location. |
| Supply current contradicts I1→2 | Resolved in source | `sim.c:1208–1222` checks magnitude before scaling, negates positive-node outgoing KCL, then publishes signed uA. Supplying is negative; capacitor backdrive is positive. Existing passive branch direction is unchanged and remains pin1→pin2. Contract explicitly states this convention. |

None of the seven original findings was refuted. Their implementations now
address the source proofs rather than only changing the displayed examples.

## Solver checks and model limits

- The LED union forests and anchor flags are indexed by bounded net indices
  (at most64); reused search/seen scratch has128bytes. Union parents always
  move to a lower root, so root walks terminate. Part/net-plus-one metadata
  fits its byte storage. No new recursive or unbounded graph traversal appears.
- Genuine R/C reference discovery excludes LED stamps. Fixed-node boundaries
  do not become shortcuts when pruning an unpowered nonfixed component.
  Capacitor-containing components retain their history companions. Per-step ON
  candidates and re-arm after an actual HC/555 drive change preserve recovery
  after a clock LOW phase instead of freezing a formerly dark series path OFF.
- The near-zero cyclic LED refusal is a conservative, explicit model boundary,
  not proof that every refused circuit is physically insoluble. It is preferable
  to publishing an arbitrary clamp. It is bounded by the existing12-pass cap;
  unrestricted complementarity is not claimed. Document this refusal plainly
  alongside the other supported LED limits if expanding the public help.
- Current limiting checks use converged active LEDs and an LED-only union;
  a parallel resistor cannot hide the direct ideal-rail path. A true series R/C
  breaks that path. The numeric current ceiling is checked before uA conversion;
  excessive current with a genuine series resistor reports range rather than
  falsely asking for a resistor that is already present.
- Stamp signs, BE history direction, partial pivot selection and row/permutation
  swaps remain coherent. Reuse requires the same unknown-net identities and
  every LED active conductance; changing fixed drive values changes RHS, and
  removing an LED or changing unknown participation invalidates LU. No modified
  path bypasses residual checking or atomic publication of capacitor/latch/clock
  state. Numeric wrapper source is unchanged by this repair.
- Event counting remains global to a numerical step and bounded at1024, including
  driver/latch rebuilds. The new clock-primed NAND fixture described in the
  verification report exercises a known-state oscillator and is stronger than
  accepting an all-X inverter ring. Preserve that fixture in final verification.

Required independent current-direction assertions are: passive resistor/LED
positive for their actual pin1→2 load direction; source negative when delivering
the same current; initialized capacitor feeding the source positive into source
pin1. Do not use only equal-magnitude comparisons. The reported strengthened
host fixtures cover these cases; the installed target should show the corrected
negative source sign as well.

## Remaining prior minor issue

**P2 — a previous diagnosis remains presented as an active error after an edit.**
`model.c:273–279` invalidates electrical results but retains `bb_result.diag` and
its net location. `ui.c:599` still prioritizes that diagnosis as `Click error:`;
`ui.c:1186` permits its locator to run against the edited document. If topology
has been recompiled, an old numeric net index can also name a different net.
This was in the original review's secondary findings and is not introduced by
the solver repair. Clear the active result diagnosis/location on topology edits,
or retain an explicitly labelled previous diagnosis with revision-aware location.
Simply testing bb_valid is insufficient because a fresh failed solve also has
invalid numeric results and must still display its active diagnostic.

Required check: fail a solve, edit the offending circuit without running, and
verify the UI no longer presents the old diagnosis as current or locates an
unrelated newly numbered net; a fresh failure must still be locatable.

The physical-strip/full-net issue is resolved in source by separate masks and
solid/dotted compositor marks (`ui.c:416–425`, `draw.inc`). Its monochrome
distinction still needs actual adapter evidence. Both user milestones remain
present; no physics feature was silently removed to obtain the repairs.
