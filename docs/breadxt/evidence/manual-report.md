# Independent complete manual LED construction

New owned file: tests/breadxt_manual.py. Existing frozen tests, product files,
Makefile and built package artifacts were not edited.

Final positive command:

    PYTHONUNBUFFERED=1 BREADXT_SHOTS=build/breadxt-final python3 tests/breadxt_manual.py

The earlier pre-fix run used BREADXT_SHOTS=/tmp/breadxt-manual-evidence.

Pre-fix electrical/source/persistence/cleanup passed in20.23host seconds,
while PNG inspection exposed the rendering failure described below. Final
manual verification on the repaired package PASSED in23.36host seconds,
including map identity and both native LED raster assertions. Exact native actions start from np0/nw0/clean:

* Parts>Power holes0/30; Ground31; Resistor63/70 (default330ohm); LED100/110.
* Wire endpoints1→93 and32→140; native automatic bends(3,0) and(20,1).
* Step yields exactly3nets and source-8824/R+8824/LED+8824uA.
* Actual probe clicks bind A to LED stableid4/pin1 and B to sourceid1/pin1.
* Native SaveAsBUILT.BBX; independent flushed FAT+BBX readers verify4parts,
  2wires, resistor value, both bends and stable probe metadata.
* File>New; native OpenBUILT.BBX; meaningful source equality; Step again yields
  the expected currents and3nets.
* Close restores free/largest515584/515072bytes and releases115712bytes of
  region+overlay. Loaded free/largest369152/368640bytes.

Negative command, as an expected failure:

    python3 tests/breadxt_manual.py --negative-current

It changes only the actual guest-published LED current to0 after the first real
solve. The assertion fails with(-8824,-32768,8824,0). Expected failure observed;
/tmp/breadxt-manual-negative.log preserves the proof. No source patch was used.

Pre-fix evidence PNG: /tmp/breadxt-manual-evidence/vga-manual-built-led.png.
Final repaired PNG: build/breadxt-final/vga-manual-built-led.png (inspected).

A concrete visual issue was discovered in the pre-fix PNG: the reloaded LED
was white/off despite+8824uA. The body interior at x297..303/y115..121 was pure
white. bu_led_shadow[3] remained3 from the first solve. Load/New performed an
invalid/stale full paint with the LED off but did not synchronize that cache;
the second solve returned the same3 key and scheduled no board damage.

The product owner repaired result-cache synchronization on document replacement
and failed simulation publication, without changing model/simulator/numeric
code. Independent pre-fix host red proof is /tmp/breadxt-led-replacement-red.log:
new replace-led family62checks/2failures, exactly incremental LED body after
actual codec Load and Example replacement. The first genuine ON baseline and
clean full paints passed, isolating the stale delta cache.

Final host command `python3 tests/breadxt_ui_host.py` passed245checks/0failures,
eight injected fault controls red,6.29s. The new replacement/failure family is
69checks, covering New+real codec Load, Example replacement and an injected
numerical-slice failure. Restoring the old cache key after an invalid full paint
reproduces the old missing-damage failure as an explicit negative control.

Final native manual test derives the LED center from live part-pin geometry,
checks25/25 interior pixels are red after each actual solve, and compares the
entire21x15 body/rays raster after reload with the first genuine solve. Both
native checks pass at the live center(300,119); coordinates are observed output,
not remembered test inputs. The final PNG visibly shows the powered red LED.

All owned context-managed native sessions have ended. The bounded host and
manual pixel files are frozen; no unrelated source or artifact file was edited. No additional build or
flasher run was started. The loaded package was checked against the current
built O88 by the shared map/artifact identity helper.
