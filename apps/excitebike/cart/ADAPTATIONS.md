# 8BitBike adaptations and rebuild status

Wave 0 establishes the pinned importer, independent build and recorder. The
running package remains the original-art native engine baseline; the imported
rules, pieces and courses in this directory are not yet its runtime inputs.
The cartridge-faithful visual and gameplay gates in `excitebike_plan.md` remain
open. This file is maintained by hand and excluded from importer drift checks.

Current baseline gameplay differences awaiting wave 3:

| difference | baseline behavior | cartridge behavior / removal gate |
|---|---|---|
| Mud | speed capped at 0.75 pixels per step, with 64/256 deceleration above it | `sub_CE5C`: decelerate by `$C0` with B or `$7F` without B, without a mud cap; Selection A recording lockstep |
| Landing | relative pitch window of 1, bounce at 2, crash at 3 | `sub_DC1A` / `sub_DC97`: bounds by slope class and speed from `tbl_D86C` / `tbl_D87C`; Selection A recording lockstep |
| Takeoff | launch velocity based on 1.5 times the terrain rise | `DCFA` / `DCFE` / `DD06`: per-element launch from integer speed, halved after bounce; Selection A recording lockstep |
| Pitch | relative terrain pitch, -3 through 4 | absolute pitch, level at 6, and the element-script VM; Selection A recording lockstep |

`tests/excitebike_ref_deviations.txt` remains the baseline comparator's register
until wave 3 replaces it. Its presence does not waive the rebuild's lockstep gate.
The four entries above must disappear when that gate passes.

Rebuild decisions still awaiting implementation and measurement:

- Generated G1/G2 artwork replaces the title mark and track plate; G3 removes
  the copyright row and G4 renames text. Imported graphics cannot ship before
  these edits are committed and reviewed. Wave 0 imports no graphics.
- The planned 200-line field keeps NES rows 3–28 except row 24. Wave 1 must
  measure the highest jump to decide whether top-edge sprite clipping occurs.
- CGA scrolls at 8-pixel granularity and Hercules at 16 card pixels; wave 2
  records the actual shipped scrolling and parallax measurements. VGA fine
  scrolling is still an open measurement.
- The baseline uses fewer opponents on CGA and Hercules. Wave 5 first restores
  all three; wave 8 measures whether the 20 fps floor permits retaining them.
- The cartridge RNG advances in an idle loop. Wave 5 will replace that with
  once-per-frame advances plus cartridge call sites, with injected-RNG lockstep
  and measured spawn/lane/gap distributions.
- PC speaker and OPL approximate the 2A03 channels. Wave 7 must record the
  frequency/timing checks and DAC ducking limitation for the imported audio.
- The desktop window, help, Esc, Alt+Enter, palette and sound controls are the
  platform shell (P8). Persisted bests and design courses use APPDATA instead
  of cartridge soft-reset RAM and the Data Recorder, when those waves land.

Baseline performance already measured in the engine design record, on emulators:
scrolling 54.6 fps VGA, 59.7 fps CGA, 50.6 fps Hercules; worst Selection B frame
20.5–22.2 fps. These are emulator results, not physical XT measurements, and
are not measurements of the future cartridge-art build. Wave 8 must replace
them with measurements of the shipped result and attach a reason and number
to every retained gameplay or presentation difference.

Wave 0 rename verification: all three gameplay GFX files, the sound blob,
course streams and element scripts equal the pre-rename build byte for byte.
The desktop/loading wordmark now says 8BitBike. Its VGA splash changes from
12,457 to 11,867 bytes, CGA from 3,498 to 3,440, Hercules from 4,866 to 4,784.
Longer splash filenames relocate addresses in the assembled package; therefore
the plan's strict packed-binary-only-name assertion is still an open gate.
This is an identity/build adaptation, not evidence of cartridge-rule fidelity.
