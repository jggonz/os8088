# Breadboard XT workbench face

The user's request (2026-11-16): update Breadboard XT's UI to match the
concept screenshot "of how the design should have been", using codex for
button assets and in-package custom fonts and images, as rich as the machine
allows. Contract: SPEC.md 130.6. Mockup: `docs/breadxt/design/`. Diary:
`docs/HOW-BREADXT-WAS-BUILT.md`, *The workbench face*.

Behaviour that the concept pictures but #372 lacked is part of the job: the
part categories, the examples list, the Net list tab, the value spinner, the
probe box, the circuit figures and the scope's collapse button all work.

## Decisions

- **No new kernel facility, no sixth menu.** Help stays View > Help / F1.
- **Resident segment is full** (32,710 image + 32,180 bss = 64,890 of 65,024
  at #372). The drawing core replaces the old one and layout code is overlay
  code; the help text moved into the art and the example builders to the
  host to pay for the faster core. 197 bytes spare at the end.
- **The band is a 16 KB heap claim, not the file buffer** (decided by a
  measurement, 2026-11-16): the file buffer held 9 rows, and 45 strips each
  re-walking every section made the first VGA paint 16.4 s on the XT. The
  file buffer stays the fallback when the claim is refused.
- **The art is a LAZY part.** The 256 KB machine had 3 KB of heap left with
  the package loaded; it gets the plain face rather than a refusal.
- **Fonts are WIN16's**, shared as source, not a new face.
- **The art is drafted from a codex master and finished by hand**; the text
  art is the source (`apps/breadxt/art/sprites.txt`).

## Stories

- [ ] F1 Contract, plan, mockup in the tree, draft PR.
- [x] F2 Art: codex master, `tools/os88bbart.py` (draft, build, selfcheck,
      png), `sprites.txt` finished by hand, `BBART.BIN` as part 1 (lazy).
- [x] F3 Drawing core `bx_*`: band, fill, dither, sprite + remap, text, trace,
      planar / packed / mono emit; plain fallback with the kernel glyphs.
- [x] F4 Chrome: tool bar with picture buttons and run-state field, palette
      with categories, examples list, tabs, inspector (picture, value box
      with spinner, Rotate/Toggle, probe box and readout, circuit figures),
      status bar with `F1 Help`.
- [x] F5 Board: cream board, rails and breaks, trench, labels, drawn parts
      (resistor bands from the value, capacitors, LED glow, DIPs with
      names, switch, pot, supply box, ground, logic parts), 2 px coloured
      wires, ref/value tags, dashed selection, strip/net highlight.
- [x] F6 Scope dock: black screen, dotted grid, two traces, legend, V and
      time labels, collapse; MID's Scope tab; COMPACT's Scope view.
- [x] F7 Net list view.
- [x] F8 MID and COMPACT layouts, 1bpp mono table, Hercules and CGA looked at.
- [ ] F9 Tests: suite rows updated for the new geometry, host UI harness,
      identity check (incremental vs full paint), 256 KB plain face.
      Done on the host (`breadxtuihost` green, with the new `sections`
      mode); the emulator rows are updated and run once in the final pass.
- [ ] F10 Docs: SPEC sizes, DEVELOPER.md, diary, screenshots per adapter,
      PR description with pictures.

## Verification

Host: `make breadxt breadxtdisk`, `make` (fast tier), `make KERN_SMALL=1`
only if it ships there (it does not), `tools/os88bbart.py --selfcheck`.
Looking: one boot per adapter (QEMU VGA and CGA, MartyPC Hercules), cropped
and zoomed. Rows named by each wave; ONE soak union and `test-full` after the
last batch: breadxtcore, breadxtuihost, breadxttarget, breadxtcapacity,
breadxtcga, breadxteditor, breadxtmanual, breadxt256, breadxtherc,
breadxtbundle, breadxtflasher.
