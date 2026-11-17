# Breadboard XT final closure review — redraw and native UI

Lens: REDRAW/UI. Verdict: **pass at source-review level**. No remaining major
finding from this lens. Read-only review of frozen ui.c/draw.inc and relevant
model interfaces, compared against both prior redraw reports and the repair
handoff. No product edits, builds, emulator runs or test execution.

## Closure of prior findings

| Finding | Final source disposition |
| --- | --- |
| Modal/compact text cache never enabled | Closed. Completed renders reach the common cache-valid path; partial native paints do not update whole text shadows (`ui.c:139–153,782–784`). |
| Ordinary selection/scalar edits clear/redraw whole board | Closed. Old/new object and physical/net strip spans drive canvas damage; unchanged scalar bodies are retained; row bands paint their own ground. Undo/Redo queries both operation sides through `bb_history_object`. |
| Queued part/wire/index/file payload remains mutable | Closed. Parent callbacks guard pending/asking before mutation, and Escape explicitly cancels accepted editor operations before resetting staging. Busy file callbacks guard their latches (`ui.c:1281–1285,1333–1349`). |
| About replaced by timer/paint | Closed. Dedicated BU_ABOUT persists; native paint uses the damage-aware card, and dismissal reconstructs Board (`ui.c:756,1287,1333–1338`). |
| Offcanvas pin hides whole part / wire frame escapes canvas | Closed. Full object bounds drive culling; canvas-local clipping bounds bodies/leads/captions/selection (`ui.c:250–329`). |
| Physical strip and whole-net marks indistinguishable | Closed. Separate masks select solid physical-strip sides and dotted distant-net sides (`ui.c:418–425`, `draw.inc:45–94`). |
| Missing diagnostic locator | Closed. Stable part/net lookup, pan, stale-result refusal and explicit no-location toast are present; successful modal-to-Board transition requests BU_ALL (`ui.c:835–848`). |
| Persisted metadata bypasses dirty state / New baseline missing | Closed. Persisted metadata routes through `bb_view_changed`; genuine New calls `bb_mark_saved`; history-object querying supports exact Undo damage without changing model semantics. |
| Partial native paint consumes outside pending damage / corrupts shadows | Closed. Partial paint avoids full-shadow mutation and restores pending flags, band and cache (`ui.c:152,1165–1172`). Idle partial uncover no longer schedules blanket INFO/BAR/SCOPE work. |
| Wide VALUE discards exposed board/plot/toolbar changes | Closed. Only compact VALUE exits early; wide VALUE services exposed toolbar/canvas/scope while retaining the editor in the inspector (`ui.c:772–780`). |
| Locate from modal leaves prior view beneath local damage | Closed. `ui.c:847` requests full reconstruction whenever successful locating changes mode. |

Prior minor source requests remain addressed: live-height Help pagination,
compact tab separation, speed epoch reset, explicit unsupported editor refusal,
cached board scanline address, and explicit probe `L:0/1/X/Z` (`L:--` for stale,
diagnostic or branch-current cases).

## Final scope repair

`ui.c:641–660` walks samples in stored timestamp order, maps only samples inside
the ten-division span, and resets continuity for invalid values or gaps. For
each connected pair it draws horizontally at the older sample value and then
vertically at the newer timestamp (`ui.c:657`). This removes diagonal
interpolation from sparse digital traces. Applying the same documented
sample-hold presentation to both channels is internally consistent.

The plot stays bounded: width is byte-aligned and capped at384, height at64;
time mapping yields x in0..w-1 for accepted samples, and accepted0..6000mV values
map to y in0..h-1. The maximum band is3072 bytes within the3120-byte shared
workspace. The two axis-aligned segments therefore satisfy draw.inc's
preclipped-coordinate requirement. Invalid samples do not gain a connecting
segment. No new buffer or native clip escape was found.

Run and its failure, each Pause route, Reset, edit/timestep invalidation,
single-Step completion and published solver failure now request scope damage,
so the cached RUN/PAUSE/STALE heading follows committed running/valid state
(`ui.c:954–978,1154,1195,1318,1387–1388`). Wide VALUE preserves those requests.
The existing font-run cache still avoids unchanged label glyphs.

## Scope of this verdict

This closes the identified source defects. The lead reports host regression
coverage for wide VALUE, deferred flags and Locate, actual target math success,
and final linked image32676+BSS32182=64858 against65024 (166 bytes spare).
Those are integration evidence supplied to this reviewer, not measurements
performed here. Moving pan/edit completion into the existing eager overlay
keeps the hot rendering/publication paths resident; no new memory scope or
workspace reentrancy defect was identified.

Fresh scope-curve fixture, final target screenshots, supported-adapter/minimum
window checks, XT cycles and stack high-water remain verification evidence
owned by the lead/verifier. This source verdict does not replace those gates.
