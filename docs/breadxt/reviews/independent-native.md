# Breadboard XT independent installed-package verification

Source/model and UI host regressions are in /tmp/breadxt-verification-repair-report.md.
This stage runs the installed .O88 package under MartyPC, reads it through live
compactor-safe Pkg views, drives native controls with os88ui, and gates on guest
state/clock. No product sources or common target build artifacts were edited by
this verifier. The map is regenerated into a temporary directory, byte-compared
with the current primary binary, and the installed FAT .O88 compared with the
current built .O88 before boot.

Passed commands (host wall elapsed includes package-map check):

* `python3 tests/breadxt.py --only vga --editor-only` —35.64s total/32.31s leg.
  Mouse resistor placement and orthogonal bent-wire preview/commit; wire Undo/
  Redo; native SaveAs/New/Open; independent flushed FAT/BBX readback; LED330→680
  (4348uA)→Undo330 (8824)→Redo680→Undo330; actual A/B stable-pin probe clicks;
  SaveAs RUNLED.BBX; a cold boot with bootcache disabled; native .BBX association
  load preserves meaningful source/probes/view; Step solves saved circuit with
  source-8824/R+8824/LED+8824uA. PLACED.BBX remains separate placement evidence.
* `python3 tests/breadxt.py --only cga` —17.53s total/14.29s leg.
* `python3 tests/breadxt.py --only herc` —17.97s total/14.69s leg.
* `python3 tests/breadxt.py --only 256` —19.91s total/16.52s leg on the final
  spatial-dash build; BIOS reports256KB.
* `python3 tests/breadxt.py --only bundle` —19.02s total/15.51s leg.
  The real BREADXT.WPK is arc_verified/arc_read on the host, all decoded entries
  staged into a freshly constructed temporary360KB FAT disk via os88disk, every
  installed file compared byte-for-byte, then booted. Native Open navigates the
  BREADXT folder and actual installed LED source/load currents solve correctly.

CGA/Herc/256 additionally exercise actual inspector/scope, missing-limiter
failure and diagnostic Locate from Help. Every adapter has an actual grow drag
to the declared minimum: native content/frame alignment yields322x128, compact
Inspector/value/Help remain within the window, value cancellation preserves
source, and a second native drag restores its large layout. VGA minimum is the
fresh-bundle leg. Native screen coordinates are derived from live package/window
geometry. Screenshots are in build/breadxt-final/; mono captures use exact banked
VRAM rather than the rendered-card offset coordinates.

Cleanup: every passed leg proves the window/instance/owner are gone and all
115,712bytes of owned resident+overlay claims are released. The overlay code
segment lies inside its claim after its part header, so containment is checked.
The kernel intentionally retains dead timer/handler words until slot reuse;
its timer dispatcher skips the dead W_FLAGS record. A zero inactive timer word
is not the contract and is not asserted.

Free/largest heap bytes before package launch and after close:

* VGA/bundle640KB:515584/515072 before,515584/515072 after.
* CGA/Herc640KB:518144/517632 before and after.
* 256KB:124928/124416 before, **3072/2560 loaded**,124928/124928 after.
  Loaded free/largest are measured; subtracting only the application claims
  from baseline would miss other loaded/cache claims. Final script prints all
  three snapshots for every leg.

Native negative control: `python3 tests/breadxt.py --only cga --negative-led`
corrupts the actual guest-published LED branch current to0 after a real solve.
The current assertion fails with (-8824,-32768,8824,0). The positive control
passes without the corruption. /tmp/breadxt-native-negative.log retains proof.

The long command `python3 tests/breadxt.py --only vga --flasher-only` PASSED:
908.46s total/905.31s leg. This run loaded the image before the final spatial-
dash-only assembly change; its C/solver behavior is the frozen final behavior.
The final registered union must recapture the whole native scope on the final
package. The lead's direct assembly raster assertions separately cover that
last display-only repair. This report does not pass off the old B-dash picture
as final-head display evidence.

The flasher uses4ms with RB68k and1ms with RB10k, waits for three settled measured
periods in each, compares independent continuous ln(2) period/duty formulas
within2%, reads both timestamped traces, and exercises Pause/Step/Reset, native
RB editing, actual timing-wire Delete/Undo and recovery. The native source
mutation stopped the oscillation and Undo restored the exact saved clean source.
Every observation completed without an unexpected numeric diagnosis.

| observation | simulated seconds | guest cycles | XT seconds | simulated ms/XT sec |
|---|---:|---:|---:|---:|
|68k three periods|4.360|8474639001|1775.639|2.455|
|10k three periods|0.911|6197978767|1298.624|0.702|
|broken timing wire|1.001|6470063754|1355.632|0.738|
|restored timing wire|0.497|3209006536|672.363|0.739|

Those cycle intervals include native Run/Pause controls and UI drawing. They
are calculation throughput, not host wall speed and not a promise of real-time
flashing. Actual68k period1.008000s vs1.011995s oracle (-0.395%); duty0.535714
vs0.534247 (+0.275%relative). Actual10k period0.207000s vs0.207944s oracle
(-0.454%); duty0.666667 equals the independent ratio. Pause held time/capture;
Step advanced exactly1ms. The paused image visibly has square A, charging/
discharging B and the PAUSE header; final spatial dash capture remains the union.

The final script shortens broken-wire observation to at least three fast
periods (~0.621s), as requested by the lead. The measured908.46s is therefore a
conservative ceiling from the stronger1.001s observation. The final union owns
the updated exact wall timing on the final artifact.

Harness debugging exposed driver assumptions, not product defects: direct letter
keys need ui.type; explicit bend legs must be orthogonal; Step after reset/edit
may end at the same timestamp as the old result; same-count Open must gate on
committed filename plus stale/recompile state; association loading is deferred
past initial menu publication; grow width snaps to322 and pointer destinations
must stay inside the screen; overlay segment is interior to its owned claim.
A healthy long flasher calculation was once interrupted because buffered menu
prints hid its progress. Logs are now line-buffered and periodic progress is
flushed; guest progress, not a180s host threshold, determines whether it froze.

Target numeric bridge, strict stack/performance capacity and strict mega runtime
remain the lead's ownership. This report does not claim physical hardware, Bochs
or automation86Box coverage. No strict mega instrument is available locally.

All owned native Python drivers and their active context-managed sessions have
finished. No further native run will start before the lead's clean rebuild and
registered scoped union. tests/breadxt.py is frozen, including line-buffered
progress, same-count filename/stale load gates and loaded-heap output.
