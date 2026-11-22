# Wide Control Panel artwork (SPEC.md 134.6)

`cp-icons-16.png` (256x32, 16x16 cells, 16 x 2) and `cp-icons-32.png`
(384x32, twelve 32x32 cells) are the SOURCE of every icon in `CPWIDE.DAT`.
They were generated on 2026-11-21 by Codex CLI 0.170.0's built-in image
generation from `ICON-PROMPT.md` (verbatim), with the three mockups in
`docs/cpwide/` attached as style references:

    codex exec --skip-git-repo-check --sandbox workspace-write -C <dir> \
        -i cp-drivers-vga-640x480.png -i cp-display-vesa-1024x768.png \
        -i cp-theme-vga-640x480.png < ICON-PROMPT.md

Every pixel is one of the 16 EGA/VGA palette colours or the key #FF00FF
(transparent). `tools/cpwdat.py` converts them deterministically; nothing in
`make` calls a generation service. To change an icon, edit the PNG by hand
(or regenerate and keep the new prompt here), then rebuild.
