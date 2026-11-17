# Breadboard XT implementation review — rules, fit and safety

Lens: RULES / FIT / SAFETY. Verdict: **fix**.

Read-only review of the current source, shared contracts, generated C assembly,
package shim and Makefile integration. No product file was edited; no build,
emulator, test row or child agent was started. The independently passing host
checks and earlier target build are context, not checks performed by this review.

## Major findings

1. **Queued editor commands do not own an immutable transaction.**
   `apps/breadxt/ui.c:723` stores only `bu_op` and `bu_arg`. Part/wire commands
   subsequently read the global `bu_part`, `bu_wire`, `bu_edit_index` and selected
   object state in `ovl_bu_execute`. Callbacks mutate those globals *before*
   `bu_queue` notices that another operation is pending: examples are Rotate
   (`ui.c:964`), value acceptance (`ui.c:985`), switch/input toggle (`ui.c:998`),
   reroute/color (`ui.c:1148`) and tool reset (`ui.c:740`). Two already queued
   input events may therefore replace the first command's record/index before
   its wake executes. Esc can also reset `bu_edit_index` to -1 while an edit is
   pending, changing a replace into an add. This is a source-level race on one
   serialized UI task, not a worker-thread race.

   Fix: either bank the entire pending record, stable target and index at queue
   time, or reject/ignore every state-changing parent callback while `bu_op` is
   pending. If Esc is allowed, explicitly cancel the pending operation first;
   do not merely reset its staging fields. Apply the same rule to file-result
   latches (`ui.c:1184`) so a busy rejection cannot replace pending filename or
   I/O metadata. Add a host UI queue fixture with two callbacks before a wake,
   including Enter/edit then Esc and two different part/wire commands.

2. **Persisted probe/view edits are not marked dirty, so Close can discard them
   without the native save question.**
   Probe binding changes `bb_doc.probe`, `probe_kind`, `probe_id` and `probe_pin`
   at `ui.c:937–946`; pan changes persisted fields at `ui.c:1010`; scope division
   and trigger are changed in the key handler; zoom changes at `ui.c:1164`.
   These paths set drawing/capture flags but never `bb_dirty`. The only dirty
   recalculation for these fields lives in model operations, so a clean loaded
   project can receive a new stable pin/branch probe, remain clean, and close
   through `ui.c:1182` with no opportunity to save that binding. This also
   contradicts the model's deliberate saved-view snapshot in `model.c:294`.

   Fix: centralize persisted view/probe changes in a small helper that marks the
   source dirty (without unnecessarily recompiling topology), updates the title
   and schedules drawing. If view-only fields intentionally have a different
   save policy, implement and document that policy; stable probe edits still
   need recoverable source semantics. Verify clean load → change probe → close
   invokes Save/Discard/Cancel and Save/reopen retains the new binding; verify
   Undo cannot incorrectly clear unrelated metadata dirtiness.

## Minor finding

3. **New's clean baseline is not registered with the model's saved revision.**
   In `ui.c:836–843`, New clears history, invalidates, then directly writes
   `bb_dirty=0`, but does not call `bb_mark_saved`. `bb_history_clear` makes
   `bb_msaved_known=0`. Place one part and Undo it after New: the source returns
   to its clean empty state, but `ovl_bb_mdirty` must still report dirty because
   no baseline was registered. Initial launch and successful file load already
   call the proper API.

   Fix: after completing a genuine New, call `bb_mark_saved`; keep an opened
   built-in example dirty as intended. Test New → add → Undo returns clean.

## Cross-review finding

The redraw reviewer independently confirmed that a partial W_PAINT can update
whole text shadows and clear pending outside-damage flags (`ui.c:684–716`,
`1016–1018`). That reviewer owns the detailed finding and fix; do not treat a
successful static C gate as proof of glass/shadow correctness.

## Safety and fit evidence assessed

- The reported image 32,690 + BSS 32,110 = 64,800 is below the authoritative
  65,024-byte package cap, with only 224 bytes spare. The eager embedded overlay
  is 41,081 bytes. Required fixes must be followed by a measured package build;
  use existing overlay boundaries rather than raising the cap.
- The shared header uses 16-bit word storage under both host and target builds.
  Codec record sizes/count arithmetic are bounded before indexing, and decode
  rejects the live document as a target. Structural validation remains separate
  from electrical solvability. I found no unguarded field-derived write beyond
  the declared model/codec arrays in the reviewed paths.
- Current numerical and drawing cdecl helpers correctly fetch stack arguments
  through SS/BP and data through DS, preserve ES where it is borrowed, and leave
  the assembler in .text. Window records are accessed through SDK functions.
  No new C struct-by-value, addressable automatic, floating type or wide-integer
  dependency was found. Numeric bridges force the software path and preserve
  untouched output on an arithmetic failure.
- The shared 3,120-byte file/row/scope workspace is sound under the current
  single UI task: file operations finish before drawing, model/solver state
  does not retain a pointer into it, and no worker shares it. Keep that explicit
  invariant if future code introduces nested painting or workers.
- The largest generated C frame is 54 bytes in `bu_scope_draw`; I inspected
  its direct call chain, wake frames and numerical bridges. No concrete stack
  overflow was established by this read-only pass. A 96-byte frame gate alone
  is not a complete 512-byte UI-stack proof: final target high-water evidence
  must include far overlay entry/shims, the numeric engine and native alerts.
- The shim flags match the implemented callback set; ordinary callbacks defer
  model/codec/solver work to lock-free wakes. Initialization remains resident;
  no worker calls overlays. The native association/icon and eager overlay are
  present, and Make lists the included C/header/assembly dependencies. The app
  reaches the everything/live payload while remaining an on-demand C build.

Pending evidence: actual target numeric fixture and HC analog threshold cases,
installed save/reboot/association, all adapters/minimum geometry, close cleanup,
256KB memory arithmetic, protected-mode compatibility and guest-cycle/stack
measurements. Those are not passing claims from this review.

Rows: breadxtcore and the forthcoming scoped Breadboard XT target/UI rows;
ordinary make/doc/ABI gates and test-full after the final repair batch.
