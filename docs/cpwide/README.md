# Wide Control Panel mockups (SPEC.md 135)

The design reference for the wide face, generated on 2026-11-21 with Codex
CLI 0.170.0's image generation from `MOCKUP-PROMPT.md` (verbatim), with
screenshots of the compact panel attached. Each is at its screen's real
resolution, 1:1.

| file | page | screen |
|---|---|---|
| `cp-drivers-vga-640x480.png` | Drivers (SPEC.md 135.8) | VGA 640x480 |
| `cp-display-vesa-1024x768.png` | Display, the mode changer (135.9) | VESA 1024x768 |
| `cp-theme-vga-640x480.png` | Theme (135.11) | VGA 640x480 |

They are a design, not a screenshot: where the built panel differs, SPEC.md
135 and the plan say why.

## Screenshots of the built panel (`shots/`)

Taken on QEMU from the merged head, 2026-11-23, for PR #402. These are the
glass, not a design. `vga-*` and `vesa-*` are the wide face (CPWIDE.DRV);
`ega-*`, `cga-*` and `herc-*` are the compact face, which those adapters
still open. The EGA, CGA and Hercules builds are QEMU knob builds
(`VIDEO=ega|cga|herc`), and Hercules was read out of guest RAM with
`tools/hercshot.py`. `vga-640x480-side-dock-compact.png` is QA-1's fix: with
the Dock on the left, a 640x480 desktop opens the compact face.
