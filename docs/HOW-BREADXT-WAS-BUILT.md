# How Breadboard XT was built

[The delivery plan](plans/BREADXT-PLAN.md) tracks both milestones. SPEC.md
section 130 and the [shared contract](breadxt/CONTRACT.md) define behavior.

## Baseline, 2026-11-16

Work began at commit `217ff4dc5` on a separate `feature/breadboard-xt` worktree,
leaving the user's existing output alone. `make deps` confirmed the macOS arm64
tools. Initial overlapping builds hit the compressed-kernel two-pass comparison;
serialized `tools/setup-cc.sh`, `make cc-smoke covl` and `make marty` succeeded.
That setup scheduling problem needed no source change and was not an application
test result.

MartyPC's `os8088_xt_vga` machine opened the existing CCSMOKE.O88 through
`os88ui`. The captured `build/breadxt-baseline.png` was inspected: the smoke
window, counter and native File menu were visible. This established the baseline
C package/loader/UI chain, not a Breadboard XT capability.

Separate scouts studied upstream/datasheets, native SDK and verification. The
inspected BreadboardSim commit `0ba38d35` is GPL; none of its material is copied.
Manufacturer pin maps replace the concept's illustrative wiring. The native
shell supports five menus, so Help belongs under View. The application reuses
shared software arithmetic rather than forbidden target C floating types.

## Foundation integration

Three independent plan reviews—fit, electrical/product fidelity and process—were
reconciled before implementation. They led to 48-byte BBX headers with stable
branch/pin probes, explicit invalid readings, a checked 30 mA current boundary,
separate numerical error state, and exclusion of undriven digital-only inputs
from analog unknowns.

The model worker delivered geometry, validated transactions, undo and canonical
nets. Its isolated SmallerC gate reported 34 functions and a largest 14-byte
frame; implementation-side ASan/UBSan fixtures passed. Those module checks did
not replace independent integrated electrical/interaction verification.

Package metadata, an original 16×16 icon, native BBX association, eager embedded
overlay and four-geometry disk/WPK rules followed. The completeness gate required
a BREADXT folder in the everything/live payload. A nested example stamp exposed
the existing sweep gate's basename assumption; moving it to a root stamp preserved
the gate. A subsequent normal `make` passed 56 fast rows with one explicit skip.
The ordinary build acquired no C compiler dependency.

The standalone BBNUM package builds the shipping numeric bridge behind the real
C calling convention: its initial image was 5,842 bytes with 184 BSS bytes. It
contains independent IEEE-word expectations, checked failures and an N-key
negative control. That initial build was not itself guest numerical evidence.

## First installed checkpoints and implementation review

Moving command-time model, solver and UI code into the SDK's eager overlay made
the package fit without reducing circuit capacities. The first fitted snapshot
was image 32,778 + BSS 32,076 = 64,854 bytes, with a 37,065-byte overlay. Its
installed 360 KB VGA boot displayed the native board, palette, inspector and
scope. Checkpoint A inserted a resistor with actual mouse input, saved it as
FIRST.BBX through the native picker, used New, then reopened the file. The clean
restored document was inspected and the changed floppy flushed.

A later checkpoint fit at image 32,690 + BSS 32,110 = 64,800, overlay 41,081.
Its global bar showed all five native menus after shortening only that bar's
product label to BreadXT. An actual 8088 Step on LEDTEST.BBX produced 8,824 uA
through resistor/LED and 2.088 V at the LED anode, matching the 2 V + 10-ohm
model. The VGA image was inspected. These are checkpoint observations, not
claims about the final repaired head.

Three fresh implementation reviewers independently examined rules/fit, redraw
and electrical fidelity. They found queued-transaction aliasing, omitted dirty
metadata, partial/modal cache errors, nonpersistent About, overdraw/clipping,
indistinguishable strip/net marks and absent diagnostic navigation. Electrical
findings included B-channel aliasing, reset priority with floating comparators,
a fabricated zero-current LED clamp, missing one-supply enforcement and source
current sign. Independent red regressions showed nine failures before repairs;
the earlier passing host tests had not covered every required behavior.

## Review repairs and independent host evidence

The solver repair made both scope channels force capture at resolved threshold
edges, while keeping trigger and period ownership on A. RESET now dominates
floating lower-priority comparator inputs. Exactly one source is enforced, and
its reported current follows the same passive pin-1-to-pin-2 convention as other
two-terminal parts: negative when supplying energy, positive during capacitor
backdrive. An all-LED path between unequal ideal rails cannot evade the real
series-limiter check merely because its on-resistance keeps current below 30 mA.

The LED review also exposed a model boundary that needs an explicit diagnostic.
Unpowered/open-switch stubs can have zero current and FLOAT/Z nodes. Genuine
R/C references and capacitor history must remain active. Near-zero-current
LED-only cycles can mutually maintain arbitrary voltage clamps, so the bounded
model now refuses that ambiguity with FLOAT rather than inventing a voltage.
This is not a claim that the physical circuit is insoluble. The repair retained
the 12-pass and 1,024-event caps and reused existing bounded graph scratch.

The UI repair fenced queued payloads before callbacks could overwrite them.
Enter then Esc now cancels a queued edit explicitly. Persisted probe/pan/zoom/
timebase/trigger changes participate in dirty state; New registers a clean
saved baseline. Native partial paint preserves outside-clip pending work and
full-display text shadows. Wide value editing continues to paint the exposed
board, toolbar and plot. About persists across timer/uncover events. Diagnostic
location reconstructs Board when leaving a modal view, pans actual locations
into view and gives a native toast when no physical location exists. Edits hide
stale diagnoses even if a standalone topology compile clears its dirty flag.

A second independent source review found no remaining concrete rules/fit or
solver defect. The remaining redraw/diagnostic findings were repaired and
challenged by fresh host regressions. These are source/host findings, not final
on-glass proof. The verifier's latest logs report:

- Core: 3,956 checks, zero failures, maximum 16 numeric operations per slice;
  six expected-answer negative-control families fail as intended.
- Independent random oracle: 160 deterministic resistor networks and 3,920
  voltage/current/KCL comparisons; deliberate voltage/current/source-sign
  corruptions are rejected.
- UI: queue, modal, partial damage, wide editing/dismissal, error location and
  stale-diagnostic families all pass. Injected defects turn the relevant
  families red. Appending one value character draws four glyph cells in one
  call; a steady inspector draws zero cells/calls.

The host runs the shipping C with 16-bit stored words but native numeric adapters
and wider automatic integers. Those checks do not prove the actual 8088 calling
bridge, integer promotion, SS/DS, stack, band helpers, native dialogs or XT speed.
The first source-sign checkpoint was deliberately retained as historical
observation; it precedes the repair and is not evidence for the final sign.

## Linked and packaged checkpoint, 2026-11-16 14:25

The combined package build reported 219 C functions and a largest individual
frame of 54 bytes. Resident image 32,800 plus BSS 32,182 is 64,982 of the 65,024
byte limit, leaving 42 bytes. The eager embedded overlay is 49,441 bytes and
packs to 22,731; the resulting O88 file is 45,675 bytes. These are measured
checkpoint values, not a final stack/performance or future-size guarantee.
No capacity or kernel/package budget was raised.

`make breadxtdisk breadxtbundle breadxtcheckdisk` completed at that checkpoint.
All four distribution floppy geometries passed structural verification, as did
the local WPK. The test disk includes the ordinary package/examples plus BBNUM
and six actual-codec target regression documents. Tutorial prose was polished
afterward, so disk/WPK resources need repacking; the executable source was not
changed by that documentation update.

Historical captures already inspected include the installed VGA placement/
save/open checkpoint and an LED step yielding 8,824 uA and 2.088 V. The first
CGA picture showed a readable compact toolbar, view tabs and panned board; it
also exposed the long native product label hiding View, fixed by using BreadXT
in the global bar. These old captures do not certify the final repaired head.

## Final evidence still to record

The lead's final pass owns actual 8088 numeric/HC/RC/555 behavior and stack/cycle
measurements, current VGA/CGA/Hercules screenshots and dialogs, save/reboot/
association installation, 256 KB memory arithmetic and the final scoped suite
results. kern_mega needs its strict runtime instrument; unavailable Bochs or
automated 86Box is not replaced with a passing QEMU claim. The checked story
list stays open until its required evidence exists. No branch, external PR,
release or website was published by this work.


## Final native evidence and the cache lesson

Both milestones reached installed native checks. The reference 555 ran through
its real pins and RC network: three settled periods measured1.008s/53.571% at
68k/4ms, then.207s/66.667% after a native10k/1ms edit, within2% of independent
continuous estimates. Deleting the real timing wire stopped edges; Undo/Reset
recovered them. The solid output and dashed capacitor trace are calculated
samples, and the drawing-only dash repair was tested by actual8088 bitmap cases.

A full empty-board LED build supplied the last useful challenge: all four parts
and both wires were placed with native controls, solved, probed, saved and
reopened. Its numeric checks passed while its reopened LED was white. The
logical ON cache survived a full stale/off repaint. The independent host
replacement case went red before a five-call synchronization repair. The final
native test now sees25/25 red interior pixels before and after reopening and an
exact315-pixel match of the entire body/rays. Correct numbers alone had not
proved the indicator was right.

The first final union passed ten rows; the added manual row and affected short
UI/adapter/ABI rows passed after the cache repair. Pure model, solver, numeric
bridge and drawing source did not change, so the long timing run was retained.
The full OS gate passed apart from a misfiled plan-review document, which was
moved and its failed index row rerun successfully. All four kernel budgets pass.

The actual256KB run keeps full capacities but has only3KB free while loaded.
The final package owns113KiB plus native window/cache resources. The software
arithmetic is deliberately honest about speed: the reference4ms run advanced
about2.5 simulated milliseconds per real XT second. The UI remains usable during
bounded calculation. No hardware or strict mega runtime is claimed; Bochs and
automated86Box were unavailable. See [the evidence record](breadxt/EVIDENCE.md),
[measured memory/cycles](breadxt/DEVELOPER.md) and retained captures for the exact
checks and practical limits. Nothing was published externally.

## Review handoff

After local delivery, a separate GUI 86Box session booted the existing
Micronics 386DX/25 VGA profile with private copies of the system and software
disks. A standard AUTOSTRT document link opened FLASHER.BBX in Breadboard XT;
the workbench was observed loaded and running. The session and writable disks
remain under build/, outside the tracked source and captured verification set.

On 2026-11-16 the user authorized pushing the feature branch and opening a PR.
This supersedes the original local-only delivery restriction for that review
handoff; it does not authorize merging or publishing a release or website.
