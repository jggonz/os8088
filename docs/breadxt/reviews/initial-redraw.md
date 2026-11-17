# Breadboard XT implementation review — redraw and native UI

Lens: redraw/UI. Verdict: **fix**. Read-only source and screenshot review; no
product edits, builds, emulator sessions or performance measurements. Read
AGENTS, docs/INDEX, PERFORMANCE Part 5, the port lessons/review lens, SPEC 130,
CONTRACT, plan, ui.c/draw.inc and the UI worker report. Inspected all three PNGs:
first VGA, checkpoint A, and initial actual CGA. Those images predate the latest
UI fixes; they establish a recognizable native workbench and restored resistor,
not the final menu/layout/electrical claims.

## Major findings

1. **Modal/compact views never enable their delta cache.**
   `apps/breadxt/ui.c:688–706`: full render sets `bu_cache_ok=0`, then returns
   early for VALUE, compact INSPECT/SCOPE, HELP, EXAMPLES and PALETTE. Only the
   ordinary board path reaches `bu_cache_ok=1` at line 702. Every subsequent
   `bu_text(...force=0)` in these views therefore redraws its entire field.
   A value-entry character sets VALUE_DIRTY and INFO_DIRTY: at 638px width,
   it writes 40 value cells +47 status cells +24 clock cells =111 cells,
   approximately100ms of glyph work before C/primitive overhead. A compact
   inspector update writes nine37-cell fields +47+24 status/clock =404 cells,
   approximately364ms, although presentation is scheduled every two OS ticks.
   Fix: mark the cache valid after each completed full view render; preserve
   explicit invalidation on layout/view/covered-panel changes. Add a glass/call
   assertion that an ordinary appended value character draws only its changed
   run (roughly four cells including moving caret/bracket), and a steady
   inspector draws no unchanged cells.

2. **Ordinary selection and value commits repaint the entire board.**
   `ui.c:827–830, 891, 907, 994, 1071–1074`: `bu_dirty_board` invalidates every
   canvas row on each click/preview, and `bu_edited` does the same even for a
   scalar resistor value change. `bu_canvas:368–406` first white-fills the
   dirty band, then row BLIT1 writes those white pixels again before restoring
   holes and finally components. A blank 30-column,14-row VGA board selection
   alone causes about46 drawing primitives (fill,14 row blits,14 row labels,
   seven column labels,eight split-rail hlines,trench,selected-hole frame) and
   26 board glyph cells, before inspector/status or any objects. The repository
   estimate is about58ms fixed/glyph work; row composition and C walks are
   additional, and every existing component/wire is visited/redrawn. This is
   the documented full-redraw/double-draw defect, not a measurement.
   Fix: damage old/new selection strips/object boxes and preview extents, not
   the whole canvas; value-only edits need inspector plus any LED states
   invalidated by the edit. Avoid clearing the row-bitmap area before an opaque
   row band that already paints its ground. Keep a full paint for genuine
   initial layout/pan/reveal. Price these exact actions in the final host/glass
   harness and XT pass.

3. **Queued edit payloads remain mutable before execution.**
   `ui.c:723–726, 869–907, 950–968, 985–1004, 1148–1151`: the queue banks only
   op/arg, while execution later reads shared `bu_part`, `bu_wire` and
   `bu_edit_index`. A later callback can overwrite these before `bu_queue`
   notices `bu_op` is already pending. Rejecting that later queue request does
   not restore the first payload. This can commit a different edit/object from
   the first accepted action. Fix: guard mutating callbacks before they touch
   pending records, or bank immutable command payload/index together. Esc
   cancellation should explicitly cancel the queued command or preserve it
   according to a documented rule; it must not leave an accepted payload
   pointing at a reset edit index. Exercise back-to-back input before a wake.

4. **About is overwritten by the next timer wake.**
   `ui.c:1175–1178`: About sets BU_HELP and BU_ALL, paints the native About
   card, and leaves pending full render. `ontimer:1193–1195` wakes for those
   flags; `bu_render` then paints Help over the card without user dismissal.
   W_PAINT after damage also paints Help rather than the card. Fix: retain a
   dedicated About state/flag and call the native `_d` card from paint; clear
   it only on a defined dismiss input. The SDK's About helper deliberately
   does not own this state. Verify card persistence across timer ticks and
   uncovering, then dismissal.

5. **Panning drops whole parts and can draw wire-selection frames in panels.**
   `ui.c:294–299`: any off-canvas component pin returns from `bu_part_draw`,
   hiding the entire part even when other leads/body are visibly in view.
   `ui.c:284–286` draws selected-wire endpoint frames through raw gfx_frame,
   without the canvas clipping used for wire segments. An off-screen endpoint
   can therefore mark the palette/inspector after horizontal/vertical pan.
   Fix: cull only when the whole object bounds miss the canvas; clip visible
   leads/body/labels/selection to the canvas rectangle. Apply the same local
   bounds to wire selection frames. Check long parts spanning the viewport
   and a selected wire with one endpoint panned out on CGA/Hercules.

6. **Selected physical strip and full net use exactly the same style.**
   `ui.c:383–388` folds both conditions into a single mark bit, and
   `draw.inc:44–80` has one dotted surround. With net highlight enabled the
   learner cannot distinguish the selected five-hole physical strip from
   distant wire-connected strips, contrary to US02's explicit different-style
   acceptance. Fix: retain separate physical-strip/net marks and draw distinct
   monochrome patterns (for example solid surround versus dotted surround),
   with color only as an optional accent. Verify a wire-connected pair of
   physically separate strips, including rail-break sides.

7. **No action locates an electrical diagnostic.**
   `ui.c:120,538–548,1063–1074`: diagnostic display is text only. Clicking the
   status row falls into ordinary selection and may clear selection; clicking
   the inspector opens the value editor. No handler consumes
   `bb_result.part/net` to highlight/scroll to the affected object or net.
   Fix: a status-error click or a real Locate Error command should select the
   stable offending ID/net when present, pan it into view, and retain explicit
   no-location behavior when the solver cannot name one. Add a short hint.
   This closes US09 without adding decorative enabled controls.

8. **Persisted probe/view changes bypass dirty tracking.**
   `ui.c:937–946,1010–1014,1106–1110,1163–1165` directly changes BBX probe,
   pan, timebase, trigger and zoom fields without setting dirty. Model
   `bb_mark_saved/bb_mdirty` explicitly compares these persisted fields, but
   bb_mdirty is not invoked by these UI changes. A clean document can thus
   change a stable probe and close without a save prompt; undo after another
   command may only discover it later. Fix: one view/probe-change helper must
   update the saved-source dirty policy immediately (at least stable probe
   bindings); if pan alone is deliberately non-dirty, document that choice
   while still preserving save semantics. Test probe change on a clean saved
   project followed immediately by Close/New.

9. **Partial native damage updates full shadows and drops unrelated pending damage.**
   `ui.c:1016–1018,134–149,688–716`: every W_PAINT calls full render, even
   when bu_damage is a small native clip. If that clip intersects the inspector,
   bu_text(force=1) updates all its shadow fields, although only the damaged
   pixels reach the glass. Full render then clears every bu_flags, including
   a pending outside-damage readout/scope update. A committed result awaiting
   the two-tick presentation throttle can thereby be marked as displayed when
   its digits are still old; after a Step/Pause there may be no later update
   to repair it. This was independently noticed by the rules reviewer too.
   Fix: update shadow validity only for pixels actually painted, retain pending
   regions outside the native damage, or explicitly invalidate those shadows
   so the next scheduled draw must repair them. The final glass harness should
   commit a new result, damage a small unrelated rectangle before flush, then
   pause and compare all readouts/scope to a clean full repaint.

## Minor findings / follow-up checks

- `ui.c:655–669` Help/Examples headings begin at relative28/27 while compact
  tabs occupy24..31, so compact headings overlap tabs. The fixed eight-line
  Help page at28+7*12=112 also loses its final navigation lines in the minimum
  128px outer window (109px content). Hide compact tabs for these modal views
  or lay them out below the strip; use a page height derived from content so
  every help line/navigation remains reachable at minimum size.
- `ui.c:538–558` resets/loads rewind simulation timestamps but leave the speed
  measurement's prior epoch. The next unsigned difference can show saturated
  throughput. Reset speed epoch/counter on Reset/New/load/compiled restart.
- `ui.c:972–978` opens Value/Parameter for unsupported types before rejecting
  a parse. Refuse explicitly before opening or disable the contextual command
  with the stated fact. Update the menu label to clarify value versus T param.
- `draw.inc:93–118` does a multiplication and five-register save/restore for
  each pixel in each3x3 hole. The band removes hundreds of OS calls, which is
  good, but the inner implementation still computes repeated scanline addresses
  thousands of times. Once damage is fixed, consider one row address and an
  end-mask/byte operation per small hole span, as the repository's 8088 renderer
  guidance recommends. Measure before claiming the compositor is cheap.

## What is sound and what remains unproved

- Numerical/file operations are deferred to lock-free wakes; drawings consume
  committed results. Ordinary hover has no application callback/drawing path.
- Row/scope buffers are bounded: scope max384x64/8=3072 bytes inside3120;
  the row band also fits. Scope coordinates are range-checked before assembly
  Bresenham. DS/ES handling in the byte/board helpers is deliberate, and includes
  return to .text. The shared IO/draw workspace is plausible because UI task
  callbacks are serialized, no bitmap pointer is retained and the synchronous
  file operation finishes before the wake's draw. No evidence of an independent
  worker reentering it was found.
- Native menu adaptation, physical pin drawing, opaque text API, actual graph
  samples/timestamps and distinct scope trace patterns are present. The old
  screenshots show why the initial native View title/inspector clipping needed
  work. The latest source changes need new actual screenshots.
- No target cycles, lock duration, final mono/minimum-window passes or glass
  equivalence were measured in this review. The final verifier must exercise
  those after fixes; do not treat this report as a passing functional gate.
