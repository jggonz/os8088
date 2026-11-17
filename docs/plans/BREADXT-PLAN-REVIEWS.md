# Pre-implementation independent reviews

# Breadboard XT plan review: fit and numeric correctness

Read-only review of SPEC130, docs/breadxt/CONTRACT.md, apps/breadxt/bb.h,
/tmp/breadxt-plan-draft.md and the SDK/numeric scouts. No product edits,
builds, emulators or subagents. Verdict: fix the remaining contract gaps
before independent implementation; architecture otherwise fits the SDK.

## Major findings

1. **Publish the numeric domain and all conversion guards in the binding
contract, rather than leave the worker to choose them.** SPEC130.3 currently
accepts R100ohm and6V while BBResult.ua is signed16 microamps. That circuit
produces60000uA; a100ohm+LED at6V produces36364uA. The draft now deliberately
limits accepted branch current to30mA, and the lead confirmed this decision.
Put that limit in SPEC/CONTRACT before implementation, require the solver to
compare the opaque current against the limit **before** calling bb_nround,
and give BB_ENUMERIC with an invalid current sentinel rather than allowing
signed narrowing. Pot positions near an endpoint can produce even larger
currents despite the total R range, so cover them explicitly. The source
parameter range is not a promise every resulting circuit is numerically
accepted. The limit must also say whether source-current sums are included.

2. **Pin the proposed status sentinels so invalid results cannot display as
zero.** BBResult has a global validity flag and per-net logic but no per-net
analog status. Four-state logic alone cannot say that an analog reading is
stale, disconnected or failed. The lead proposes mv=-1 floating,-2X,-3failed,
valid analog0..6000mV, plus ua=(-32767-1) invalid. Add these named constants,
semantics and scope handling to bb.h/CONTRACT; comparisons, plotting and LED
brightness must test status before doing arithmetic. Keep document-wide
stale/failed state separate from a physically valid0V. Test each sentinel
through probe and scope rendering, including pause and stale-after-edit.

3. **The plan still delegates binding numerical constants after interfaces
are being handed off.** Draft171–190 says to choose pivot and residual
thresholds and gives an illustrative nonlinear bound; CONTRACT43 defines
“numerical work units” without counting them. Pin initial constants now:
finite working envelope, absolute/relative pivot tests, absolute/relative
KCL residual, maximum LED iterations and digital queue/events. Also define
which single operations consume the16-operation wake budget, including
stamping/residual and digital propagation stages. A whole elimination row
cannot be one unit. This is necessary for the independent verifier to reject
a result for the same specified reason as production, rather than widening
tolerances or assuming a bounded C loop is an acceptable XT callback.

4. **Pin all time and persisted-view ranges before codec/model workers split.**
CONTRACT specifies100us units and default dt10, but does not state legal dt,
clock period/duty, scope_div, trigger packing, zoom, pan and color ranges.
The model and staged decoder must share the same bounds; dt0 must never
reach a capacitor division. Default1ms and convergence0.5ms are representable.
Define modular32-bit timestamp subtraction and the maximum comparable
interval (less than half the wrap), and preserve carry with unsigned words.
Otherwise host32-bit int tests can mask target wrap in frequency/duty logic.

## Minor findings

- Draft memory table uses3104bytes for each in-memory BBDoc, but current
header has3072 object bytes+22metadata=3094. Treat3104 as a conservative
allowance and label it, not sizeof. BBScope sample arrays are2048bytes plus
20bytes metadata; actual structure is2068. The table already has separate
512byte edge/validity allowance, so the total estimate has headroom, but final
numbers must come from target map, not host sizeof assumptions.
- Pin that `bb_sim_start`, bb_compile and codec functions are only called
from lock-free wake in the integrated application, even though the pure
model is OS-free. The draft says this; put it beside prototypes to prevent
Run's command callback from compiling inline under the graphics lock.
- bb_nop currently returns1success/0failure; the numeric scout proposed the
opposite. bb.h is the authority. Require untouched out-on-error, aliasing
support, sticky bb_error and software fp_hw=0 at wrapper entries explicitly.
- Stable-ID exhaustion, next_id validation and unused part pin words need
canonical rules. Draft describes exhaustion refusal; codec should reject
next_id0 or collisions and require unused pin fields BB_NONE, not arbitrary
padding, to keep round trips deterministic.

## Sound parts of the design

-64x16part+128x16wire records, one staged source, a4800byte matrix and no
second matrix are plausible inside a65024byte image+BSS budget with the
embedded per-command overlay. Soft arithmetic adds roughly3.3KB code and75
bytes scratch according to the scout's assembler fit measurement.
- Reusing the shared8086 arithmetic with opaque records avoids forbidden C
floating types and a new general numeric engine. Host core tests plus actual
8088 numeric fixtures are correctly separated.
- Resumable lock-free wakes, immutable per-step publication, no worker,
actual physical strip mapping, canonical nets and per-object undo fit the
runtime. Exact scope cadence and numerical speed remain measured outcomes.


# Breadboard XT plan review: electrical/product fidelity

Verdict: **fix**, with no scope cut required. Reviewed the user's complete build
prompt, SPEC130, docs/breadxt/CONTRACT.md, apps/breadxt/bb.h, the complete draft
plan, source scout and exact TI pin/function tables. Read-only review; no builds,
product changes or electrical test claims. The concept is correctly treated as
visual direction, and independent physical netlist generation is essential.

## Major findings

1. **Undriven digital pins need a separate numerical participation rule.**
   Draft numerical contract promises floating-network errors and at most24 analog
   unknowns, while logic contract promises Z/X truth behavior. A board using one
   gate of an HC00 leaves unused inputs undriven; feeding all touched pin nets to
   nodal elimination would fail singularity before the used gate can operate.
   Define classification: passive/transient analog participation vs digital-only
   nets. Pure digital undriven inputs stay Z and are consumed conservatively asX;
   they are not forced0V and do not alone make the analog matrix singular. X/Z
   outputs must remove their ideal known-voltage constraint and publish invalid
   analog voltage, while actually floating passive analog islands diagnose.
   Required fixture: one powered gate with other gates unused, then AND(0,Z),
   OR(1,Z), XOR(1,Z), NOT(Z), with meaningful outputs and explicit invalid voltage.

2. **Record/source contract is short of required probe identities.**
   Original bb.h78 had only two hole words, whereas draft promises stable
   (partID,pin) and branch-current probes. It also omitted an explicit board
   profile while promising complete geometry. Lead has accepted an expanded48B
   header with profile1 and probe kind/id/pin fields, max3120B. Apply this to
   exact byte table, BB_FILEMAX, BBDoc, memory table and serializer before workers
   code. Define dangling binding policy: a deleted part probe becomes unbound or
   explicitly invalid, never silently retargets the compacted array index.
   Test move/rotate/delete/undo and save/load of a branch/pin probe, and profile
   mismatch rejection. Fixed-profileBBX1 may encode the board implicitly if that
   rule is explicit, but preserving an explicit profile is clearer.

3. **Published current and invalid states were under-specified.**
   Original BBResult82 has signed16 uA, but6V/100ohm is60000uA. Lead has accepted
   max30mA per branch with checked numerical range rejection; no silent wrap.
   Lead also accepted mv=-1floating,-2unknown,-3failed and ua=-32768invalid.
   Pin these in shared constants and help; scope samples must carry/derive the
   same invalid states and must not connect an invalid sample to a plausible
   waveform. Define signed-current valid range[-30000,30000] and explicit
   unsupported/N/A for IC/multi-terminal branch current. Potentiometer three
   terminals cannot all be described by one pin1-to-pin2 current scalar; report
   only a clearly named supported segment or N/A. Test each boundary and invalid
   probe state. The draft now states30mA, unlike the original binding SPEC.

4. **Physical lead identities must be pinned before parallel modules diverge.**
   Shared contract21–29 gives generic two-lead holes and DIP anchor, but no exact
   terminal table for non-DIPs. Define POWER1positive/2return, LED1anode/2cathode,
   capacitor1positive/2negative (or state nonpolar), pot1/2track ends/3wiper,
   resistor/switch1/2, one-pin GND/input/clock/indicator. Define whether stored
   flag BB_ROT applies only to DIP or swaps leaded footprints; render and solver
   must call the same accessor and preserve signal identity. Actual DIP maps in
   source scout are correct: quad upper tuples(9,10,8),(12,13,11), inverter upper
   pairs(9,8),(11,10),(13,12). Tests must exercise every gate, both orientations,
   each chip's power pins, boundary columns and polarity markings.

5. **Mixed-signal bridge policy needs an enforceable graph criterion.**
   “Reject unsupported analog feedback” alone lets workers disagree on what is
   supported. Declare ordinary forward bridges (fixed HC/555 output into
   resistor/LED load or thresholded next input) and the actual unsupported case
   (analog feedback into a driver dependency cycle, conflicting output/reference
   source, or whichever exact bounded class the implementation checks). Unknown
   drivers cannot silently become a fixed0V node. Include a supported loaded
   gate and555LED example, plus deliberately unsupported feedback/output bridge
   and two disagreeing drivers. Same-valued drivers require an explicit resolution
   rule. Keep the policy educational and visible in help.

## Minor and implementation-check findings

- Native five-menu decision is consistent with “as supported by shell”; retain
  Help in View plus About. There is no need for a kernel/API change or question.
- NE555 truth-table order is correct. Pin equality/RESET0.7V convention,
  defaultCONT2/3VCC, support for open/normal ground bypass, and rejected external
  control drive must all be executable checks. In particular bypass capacitor
  initial0V must not be mistaken for unsupported external control or turn the
  device into an invented second timing oscillator.
-20ms capture cadence is compatible with a roughly1Hz flasher, but shorter
  pulses/clocks need explicit edge preservation/undersampling. Trigger mode must
  specify channel, threshold, pretrigger window and hold/rearm; its serialized
  word currently has no enum/range table. Scope measurements must ignore startup
  and compute from solver-time edges, not ring/pixel spacing.
- Run/Pause/Step with partial numerical work must preserve the previous committed
  snapshot; Reset discards pending work. A failed solve may not commit capacitor,
  latch, clock, scope or measurement state. Tests should fail the final numerical
  iteration deliberately to prove atomic publication, not just test happy paths.
- Ordinary FLASHER graph is correctly specified independently:8/4VCC,1GND,
  RA VCC-to7,RB7-to(2,6),C(2,6)-toGND,5bypassGND,3through330ohmLED-toGND.
  Removing RB, moving C, reset-low and removing power must alter calculated state.
  Expected settled ideal timing0.5406548s high/0.4713401s low; first high pulse
  from0V is longer and cannot be the2% oracle interval.
-10ohm LED on-resistance explicitly corrects the ideal target:3/(330+10)A=
  8.823529mA. This is allowed by the prompt but must be visible in model help;
  do not compare to9.09mA within a tighter unexplained tolerance.
- One-lead-per-hole includes wires; valid examples need adjacent holes on a
  shared strip. Do not place a wire endpoint into the same hole as an IC pin.
  Orthogonal routing is drawing-only; crossings and rail breaks remain distinct.
- Keep compact/minimum-window controls readable and clipped at actual640x200.
  Inspect labels/polarity/pin1/disabled states and two trace styles in actualCGA
  and Hercules. Palette/inspector/instrument switching must expose every required
  command, not merely crop hidden VGA controls.
- Help/examples must remain reachable after SaveAs changes the instance folder;
  installed documents must use the same loader, not an internal demo-only path.
  New/Open/Example/Close share native save/discard/cancel. Canceled SaveAs must
  not allow the pending destructive action to execute.

## Positive assessment and remaining review boundary

The chosen separation, canonical net extraction, actual pin maps, ideal-vs-real
model disclosure, fixed split-rail board, native menus and local-only delivery
are faithful. Both milestones remain in the plan and no feature is claimed
implemented. This is a plan review, not passing host/emulator evidence. Apply
accepted shared-contract changes and let the independent verifier challenge
all behaviors through the shipping core and real UI before marking stories done.


# Breadboard XT plan review — process lens

Verdict: fix. No new user decision or scope cut is required. The A/B/C order is sound: A includes a native editable document and real save/reopen; B completes editor/DC/digital; C completes RC/555/scope and installed distribution. Both required milestones remain mandatory. The baseline compiler/overlay builds and inspected MartyPC C Smoke boot are recorded as baseline evidence only.

Reviewed the draft plan, SPEC130, docs/breadxt/CONTRACT.md, bb.h, the port skill's ownership/testing lessons, and repository test-writing/runtime instructions. No builds, product edits or agents were started by this reviewer.

## Major findings to resolve in the shared contract before implementation

1. **Shared error state can corrupt an unrelated in-progress operation.** bb.h uses bb_error for model/codec failure and for bb_nround's sticky numeric failure. A rejected placement/value edit or failed staged load must leave simulation running/paused/current state unchanged, but overwriting this shared status can cause a later solver slice to consume the editor's failure as a numerical failure. Define bb_error as last-operation scratch, clear/isolate it at each solver entry, and keep committed simulation failure exclusively in bb_result.diag (or use a separate internal numeric-error latch). Validators must not change bb_valid, bb_running, bb_recompile, history, source or published results on failure. Host fixture: pause a solve mid-slice, reject an occupied placement/bad file, resume it and compare the committed solution/time with an untouched control.

2. **Save As cancellation and replacement identity need an explicit state transition.** The SDK picker does not call onfile on Cancel; the plan says cancellation must not execute pending New/Open/Example/Close but does not pin how an old pending action is disarmed. Specify a request token/state that is consumed only by that request's successful save, cannot be inherited by a later unrelated Save, and is cleared when another command begins/cancels. Retain old document filename/place and saved revision until a successful write; a chosen but failed Save As must not cause later Save to overwrite a different document. Verify picker Cancel, failed write, subsequent ordinary Save and repeated New/Close requests through the actual UI. This uses existing native mechanisms and does not need an API addition.

3. **Implementation/test ownership overlaps.** The ownership table gives the model owner “model host fixtures” while the verification owner owns hosttest/. The common TU and exact shared globals make unspecific test writes a practical merge/race risk. Assign each worker exact exclusive source/test paths or reserve all hosttest/ writes for verification; implementers can return fixture proposals/reports. The lead must serialize shared-header/build/integration changes and compilation of the common TU. Parallel preparation of distinct module files is otherwise consistent with the user's requested worker ownership.

## Minor findings

4. Convert wave prose to a checked US01–US16 evidence table. Current wave descriptions correctly retain all work but are too broad to show which required acceptance remains open. Each story should carry planned/implemented/host-verified/guest-verified status and actual artifact references; “wave complete” must not automatically check every story.

5. Pin validation layers: bb_validate is structural document/footprint/value validation, not a demand that the circuit electrically solves. Otherwise the deliberately broken example and an ordinary partially built circuit may become impossible to save/reopen. bb_compile/net limits and simulation diagnostics are separate. Failed decode must not mutate live dirty/history/result state even if it updates operation diagnostics.

6. Timestamp, clock and timestep ranges still need final concrete limits and wrap behavior before the simulation worker uses them. Explicitly propagate word carry and choose modulo-wrap comparisons or refuse a simulation epoch that would wrap; tests should cross tlo=65535. The default100us unit,1ms step and0.5ms convergence step are coherent. This is a routine contract choice, not a user decision.

## Already recognized by the lead/other review lenses

The requested48-byte BBX header/3120-byte cap, board profile and stable branch/pin probes; signed voltage/current invalid sentinels; maximum30mA checked branch current; and10ohm LED on-resistance are accepted corrections in progress. Do not leave stale32-byte/3104-byte/1ohm statements in the final plan. These need reconciliation, not duplicate implementation.

## Process that is sound

- Native five-menu adaptation uses the user's “as supported by shell” allowance and preserves native Help/About access.
- No upstream GPL implementation material is copied, no kernel API/budget change is proposed, and external publication remains prohibited.
- Wave1 source/codec host harness plus actual native save/reopen is an executable checkpoint, not a decorative foundation.
- Numerical target fixtures complement host-double adapters; installed save/reboot/association scenarios and fresh WPK installation remain mandatory.
- Actual VGA/CGA/Hercules and256KB configurations are named, with guest cycles distinct from host wall time.
- Per-wave host/build/boot-to-look evidence and independent reviews precede one final deduplicated scoped soak union and test-full. New suite rows retain the correct soak tier and measured time budgets.

No user approval is needed for these fixes. Reconcile them into the lead-owned contract before worker implementation, and preserve the required independent implementation reviews/verifiers later.


# Breadboard XT plan reconciliation

Verdict: ready for implementation after lead mirrors the exact policies into
the shared contract. No scope cut, kernel/API change or user decision. Both
required milestones remain mandatory. This is plan review, not product proof.

Resolved:48-byteBBX/max3120, explicit boardprofile/stableID-pin-branch probes;
16byteobjectrecords; canonical unused fields and IDexhaustion; structural-only
validation; deletedprobe cleared/restored by undo;30mA currents including source
sums, invalidmv/ua sentinels and N/A multileadcurrent; exact terminal/rotation
semantics; puredigitalZ/X excluded fromGaussian unknowns; supported feedforward
bridges versus HC+analog dependencycycle rejection; separate bb_num_error;
SaveAs request tokens/cancel and retainoldidentity; exclusive hosttestownership.

Initial numeric constants: nonzero2^-64<=abs(x)<2^40; pivot greater than both
1e-12S and1e-12*rowmax; KCL residual<=1e-9A+1e-7*sumabsincidentI on unrounded
solution; fixednodevoltageerror<=1uV; physical0..6V with only±1uV numerical
excursion clamped beforemVround; current checked<=0.03A before signedround.
12LEDactive-setpasses;64entrydeduplicatedqueue,1024events/gateevaluations per
step;16workunits perwake, each numericconversion/op/compare, gate/event or
objectvisitcounts individually; elimination/stamping/residuals resumable inside.

Time100us unsignedwordpair modulo2^32; differences<2^31;dt1..1000,default10,
convergence5; clockperiod2..60000,duty1..999;scope_div10..60000;panx0..29,
pany0..13,zoom1/2;trigger0free/1rise/2fall onchannel0 atVCC/2,128pre+128post,
hold/rearm;undersampledexplicit. Defaultscope20ms cadence,5.12sfullring.

Mirror before coding: CONTRACT currently says header-lengthword32:fix48;
add precise numeric thresholds/workunit and mixedsignalgraph rule; pin unused
field/ID/probe-deletion rules and structural-onlybb_validate. Header matches
record/probe/current/sentinel/error changes already. Exact fileownership in
final plan. Storyledger hasUS01..16 separate implementation/host/guest boxes.

Planned in-memory documents are3106bytes each (3072object+34metadata), scope
2068bytes plus separately allowededge/statusscratch. With3120filebuffer,
estimatedBSS25396,residentimage26000,total51396 plus16000embeddedoverlay.
Replace estimates from actual target map and measured execution. No builds,
product edits or emulator runs were performed by this reconciler.
