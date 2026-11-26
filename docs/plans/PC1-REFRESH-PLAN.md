# PC1 refresh-rate setting

Contract: SPEC.md 31.10.5. Work branch: `feat/pc1-refresh-rate`.

## Stories

- [x] Recognize a PC1 before accessing its Yamaha video registers.
- [x] Offer 50 Hz / 60 Hz (PC1) only on that machine's Display page.
- [x] Apply immediately and remember on close, then restore at boot.
- [x] Build all system geometries and check other kernels refuse cleanly.
- [x] Check the actual compact CGA UI and configuration round trip.
- [x] On the owner's real PC1: verify 60 Hz activation, 50 Hz restoration
      and persistence at boot (composite output).
- [ ] Report fullscreen/DOS return and mouse coexistence separately.

## Build diary

- Inspected the local 100-byte CRT.COM: two V40 port checks followed by
  DDh=65h, DEh=81h; no TSR or interrupt hooks. The binary stays local.
- Owner uses composite and has already verified CRT.COM at 60 Hz under DOS.
- PERITEL.COM is unavailable. The 50 Hz value 89h comes from John Elliott's
  documented BIOS initialization, and awaits field confirmation.
- Keep both desktop-extension controls and adapter selection. Use the bottom
  caption's line for the two PC1 radios, retaining the caption elsewhere.
- The feature is shared source in the boot overlay and on-demand panel;
  no always-loaded driver or BIOS interrupt hook is needed.
- `make deps` could not install packages without administrative access.
  Extracted libudev's development/runtime packages under `/tmp/opencode` and
  built MartyPC successfully against that local library. Unicorn lives in a
  temporary venv. No host administration or tracked dependency changes.
- Host NASM 3.01 triggers unrelated DOS-tail and mega exception-stub gates.
  Extracted Ubuntu's NASM 2.16.01 locally for builds and symbol readers.
  Avoid concurrent makes targeting the same build subtree: the first attempt
  read another make's unfinished kernel; sequential builds completed.

## Verification scope

Final targeted soak union: `pc1video`, `pc1refresh`, `pc1mouse`, `cfgtrip`,
`cpwide`, `btncp`. Five passed. `pc1mouse` fails its existing keyboard-filter
source check (8 checks pass, 1 fails), reproduced unchanged on the original
`main` checkout at f2318e0f. No mouse code was changed by this branch.

`pc1video` executes the shared production 8086 routine under Unicorn with
SS != DS. Twenty cases check both frequencies, all three BIOS identities,
CPU/ROM/port refusal, invalid preferences, registers, IF preservation and
atomic indexed writes. A wrong-frequency mutation is detected.

`pc1refresh` boots MartyPC CGA and confirms no PC1 control by default. It
replaces ONLY the loaded panel's identity probe with CLC/RET to expose a
**synthetic PC1 UI fixture**; 60/50/60 clicks, deferred save and `P1` record
are checked. The saved disk reboots on an unmodified non-PC1 guest: the
choice survives but the control remains hidden. This is not a Yamaha timing
measurement. Its 9.6-second run is the registry's measured basis (10 s).
Screenshots in `docs/pc1-refresh/` show this actual CGA session and are labelled
as a synthetic identity fixture, not as physical PC1 evidence.

`make test-full`: **77 passed, 0 failed, 5 skipped** (C-toolchain/PageDesk
capabilities unavailable), 82.4 s elapsed. `make budget-check` passes for
big/small/emu/mega. All four system disk geometries built. The initial cold
fast pass exceeded its host-time budget; its only failing row, `bootfence`,
needed the boot artifacts for the manually built emu subtree and passed
after those were built. All fast rows are also covered by the green full
pass (apart from the explicitly skipped C/PageDesk rows).

Field image: `build/pc1mouse720-ref.img` from `make pc1mousetest`, the existing
720 KB PC1 disk with the mouse enabled and **no refresh override preselected**.
Alternatively use `build/os8088-720.img` and enable the mouse as usual.

## PC1 field check

The owner (`hufon/os8088`) tested the os8088 implementation on the **real
Olivetti Prodest PC1**, using the composite setup identified during intake,
and reported: “activation 60Hz et désactivation (50Hz) fonctionnent
parfaitement. Persistance au boot aussi.” Both switch directions and boot
persistence are therefore field-validated. No instrumented frequency
measurement or separate fullscreen/DOS/mouse result was supplied.

Original field recipe (steps 3–5's frequency/persistence behavior confirmed;
input coexistence and step 6 remain separately unreported):

1. Boot the 720 KB image using the same composite setup that ran CRT.COM.
2. Open Control Panel > Display. Confirm 50 Hz / 60 Hz (PC1) is present.
3. Select 60 Hz, check picture stability and ordinary pointer/keyboard input.
4. Select 50 Hz, check restoration, then select 60 Hz again.
5. Close the panel to save, restart, and confirm the 60 Hz selection and image.
6. Enter/leave a fullscreen application and DOS, confirming the same refresh
   and mouse behavior after returning. Record any picture or colour change.

The software screenshots remain synthetic-identity emulator captures. The
physical validation above is the owner's functional report, not those images.
