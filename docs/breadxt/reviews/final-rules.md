# Breadboard XT repaired-source re-review — rules, fit and safety

Verdict: the three concrete findings in my first review are resolved. I found no remaining concrete defect in this lens that requires another source repair. This is a read-only source review; I did not build, run an emulator, or reproduce the reported host test results.

## Original findings

- **Resolved — queued payload alias.** `ui.c:1233`, `1281`, `1332`, `1336`, `1341`, `1345` fence parent mouse/key/menu/About/close/file completion before they can alter pending records, indices, selection or file-action latches. Escape explicitly cancels pending model edits before clearing their staging state. Resize/paint/timer paths do not change the queued payload. The wake consumes the operation and executes it on the UI task.
- **Resolved — persisted metadata dirty state.** `model.c:672` recomputes dirty state using the existing saved revision and all 28 persisted metadata bytes (`model.c:292–309`). Probe, pan, zoom, scope timebase and trigger paths call this helper immediately; timestep changes already invalidate the document. Reverting metadata to its saved value can become clean again without losing an outstanding object edit.
- **Resolved — New baseline.** `ui.c:967` calls `bb_mark_saved()` after clearing history for a new document, allowing edit followed by Undo to reach the clean baseline.

## New helper and lifecycle checks

`model.c:674–693` checks history position before indexing, maps Undo/Redo and before/after to the correct stored side, and copies only an existing record to caller-owned static staging. `ui.c:985–988` unions both sides before mutation; it does not retain a pointer into history. No new automatic-address or C structure-copy violation is introduced.

The cancelled-Save-As/deferred-place restoration concern does not produce a wrong-folder save with the current native picker: `kernel/fdlg.inc:614–624` remounts the chooser's own banked volume/folder before delivering completion. The chooser keeps its own FS_DRV/FS_CWD fields while open. Save identity/clean state still changes only after successful write; failed load stays in the staging document, and failed write preserves dirty state. No new close/use-after-close or movable-region pointer issue was found.

The new solver state remains bounded by the existing part/net limits. The supply range is 4500–6000 mV, so the repaired unsigned HC threshold products remain within 16-bit unsigned range. The limiter/reference graph forests use bounded net indices and monotonically joined roots. The added B edge state does not enlarge an array or change the file/result layout. No concrete new integer overflow or out-of-range access was found.

## Build evidence and limits

The shared repair build log reports 219 C functions, maximum frame 54 bytes, and successful package/disk generation. It records a 32704-byte resident image plus 32182-byte BSS = **64886/65024 bytes, 138 bytes spare**, and a 49372-byte eager overlay. `CC_OVL_PART` without `CC_OVL_LAZY` selects the existing eager SDK path, so callback-side overlay helpers do not introduce first-use file I/O. The new helpers have small frames and add no recursive call path. I found no new stack/segment rule violation; the compiler's per-frame gate is not an independent measurement of total native stack high-water.

Actual target stack high-water, adapter behavior, disk failures and final functional/performance evidence remain with the lead's validation pass. This report does not claim those tests passed.
