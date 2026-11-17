# Breadboard XT local verification record

The implementation is on the local `feature/breadboard-xt` worktree. Both
milestones are present: native editor/DC/HC logic and calculated RC/NE555/scope.
No branch, PR, release, website or software-library catalog was published.

## Reproduction

```sh
make deps
make breadxtdisk breadxtbundle breadxtcheckdisk
python3 tools/os88test.py soak -k 'breadxt*'
make test-full
make budget-check
```

The native rows boot the installed 360 KB package through `tools/os88ui.py`
and MartyPC, using guest state/clock and fresh private floppy copies. `breadxteditor`
does real placement, bent-wire editing, save/flush, cold boot and BBX association.
`breadxtflasher` performs actual RB editing and wire Delete/Undo; it is a long
calculation on the emulated 4.77 MHz machine. `breadxtbundle` uses the canonical
WPK reader to create a fresh FAT installation, compares every file and boots it.
It does not claim a network download or the guest library's installation UI.

## Independent results

Host: 3,956 core assertions; 160 resistor networks and 3,920 independent
voltage/current/KCL comparisons; six expectation-fault controls plus three
corrupted-result controls rejected. ASan/UBSan run the same portable C.
UI: 245 assertions and eight injected defects, including queue ownership,
modal/partial damage, diagnostic location/staleness, sample holds and Pause labels.

Actual 8088: 13 software-number ABI cases plus two raster cases pass; the N
fault produces 14/one and restoration returns 15/zero. Installed HC input
thresholds, grounded reset with floating comparators, isolated LED, B-only clock,
signed LED/source current and RC charging match independent answers. The 24-node
limit solves every voltage/current and accepts Help while elimination is pending.
Measured stack high-water is 338 of 512 bytes. See [developer notes](DEVELOPER.md)
for exact cycles, model limits and the full memory table.

Native editor/installation passes cover VGA 640×480, CGA 640×200, Hercules
720×348, 256 KB CGA and a fresh WPK folder. Native minimum frames reach 322×128,
reflecting the 320-pixel content minimum. Inspector/value/Help stay clipped;
all owned region/parts claims are released. The measured 256 KB loaded session
has only 3,072 bytes free, largest block 2,560 bytes; this is a tight solo use case.

The 555's three settled 68k periods at 4 ms steps measured 1.008000 s/53.571%,
within 0.4% of the continuous reference. A native RB edit to 10k at 1 ms measured
0.207000 s/66.667%, within 0.5%. The actual timing wire was deleted: one simulated
second produced no oscillation and left the output HIGH. Undo/Reset recovered
a measured period. These are calculated node results, not a stored waveform.
The last drawing-only repair makes B's dash phase survive tiny sample segments;
its actual assembly is separately checked by the 8088 raster cases.

## Build and gate status

Linked package: image 32,710 + BSS 32,180 = 64,890/65,024 bytes; 134 bytes spare.
Eager overlay 49,637 bytes; complete O88 45,726 bytes. Installed 360 KB owned
claims total 115,712 bytes, including parts-carve slack. No budget was raised.

The first full integration run passed 79 rows, with one on-demand PageDesk skip,
and failed only `docindex`: the plan-review document was filed as reference.
It was moved to `docs/plans/BREADXT-PLAN-REVIEWS.md`, the index regenerated, and
that failed row passed independently. The broad green rows were not repeated.
All four kernel budgets pass: 5,632 bytes spare on big/emu, 6,656 on small/mega.

The clean rebuild and all four budget builds passed. The initial registered
union passed all ten rows in 947.4 host seconds. The new empty-board manual row
then exposed a real repaint defect after New/Open: the current was correct but
the LED cache retained its old ON key after a full stale/off paint. Five calls
to the existing result-change helper now synchronize replacement/failure state.
The host regression was red before this repair and passes afterward.

All affected short rows were rerun: host UI, manual construction/pixels, native
editor, WPK install, CGA, Hercules, 256 KB and target arithmetic/stack. The target
driver also needed a filename/stale-state wait for consecutive same-count HC
files; that one failed row passed after correcting the harness. All **eleven
registered Breadboard rows** now have passing results. Model, solver, numeric
bridge and drawing source remained byte-identical through the cache repair;
the long timing computation was preserved rather than repeated. The scoped
full gates also pass, including registry, host waits, relocation, segment and
mega-package rules. One unrelated on-demand PageDesk row was the full-run skip.

The full flasher captures below precede only that indicator-cache repair and
show the same model/drawing source as the final package. Short adapter/manual
captures and the latest arithmetic/stack run use the rebuilt final package.
Exact files, sizes, source/screenshot hashes and logs are retained in
[the manifest](evidence/manifest.json), [initial soak](evidence/soak-initial.txt),
[cache follow-up](evidence/ui-cache-retest.txt),
[target follow-up](evidence/target-retest.txt) and
[manual proof](evidence/manual-report.md).

| On-glass evidence | Claim |
|---|---|
| [Built from an empty board](evidence/vga-manual-built-led.png) | Four native parts/two wires, calculated LED, save/reload with the same lit pixels |
| [68k flasher](evidence/vga-flasher-68k-paused.png) | Calculated square output and dashed capacitor trace, 0.99 Hz / 53% |
| [Edited 10k flasher](evidence/vga-flasher-10k-stepped.png) | Native RB change, 4.83 Hz / 66%, paused single-step |
| [Timing wire removed](evidence/vga-flasher-timing-wire-removed.png) | Constant HIGH output and no measured oscillation |
| [CGA inspector](evidence/cga-inspector.png) | Readable 640×200 alternate layout and actual voltage |
| [Hercules board](evidence/herc-led-board.png) | Native 720×348 mono workbench |
| [Minimum value editor](evidence/cga-minimum-value.png) | Native 322×128 frame and clipped controls |
| [Fresh WPK installation](evidence/bundle-fresh-install-led.png) | Package/resources opened from the installed BREADXT folder |
| [8088 ABI fixture](evidence/breadxt-numeric-vga.png) | 13 number cases and two actual raster cases pass |

## Specific unverified targets

Physical hardware was not used. Strict kern_mega runtime is unverified because
Bochs and automated 86Box are unavailable on this host. `make mega` and all
budget/package gates pass; QEMU is not substituted for strict protected-mode
runtime evidence. The common C SDK path is implemented without a mega refusal.

The tool is educational and substantially slower than real time on an XT:
the reference 4 ms flasher run advanced 2.485 simulated milliseconds per XT
second. Model ranges, one-supply restriction, LED ambiguity policy and control-
pin limitations are explicit in Help and [the contract](CONTRACT.md).
