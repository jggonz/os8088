# Breadboard XT face art - generation prompt

The master sheet `apps/breadxt/art/master.png` was made ONCE with the codex
CLI's built-in image generation (codex-cli 0.169.0, 2026-11-16), from the
prompt below, run from a scratch directory outside the checkout:

    codex exec --skip-git-repo-check --sandbox workspace-write -C <scratch> < PROMPT.txt

It is a GUIDE, not the asset. `tools/os88bbart.py draft` quantised each cell
to the 16 EGA colours at its logical size and wrote
`apps/breadxt/art/sprites.txt`; that file was then finished by hand and is
THE SOURCE from then on (the way `apps/win16/fonts/*.txt` are). `make` reads
only `sprites.txt` and the WIN16 font art, never the PNG and never a service.

## Prompt (verbatim)

```
Use your image generation tool to create ONE pixel-art sprite sheet PNG, 1024x1024,
and save it into the current directory as master.png, then print its path.

Use case: icons and component sprites for a 1987-style Macintosh-System-1-inspired
breadboard circuit simulator running in 16-colour EGA/VGA (640x480). The art will be
downsampled to the stated LOGICAL sizes, so draw each sprite as chunky pixel art on a
logical pixel grid where one logical pixel is a solid square block, with hard edges,
no anti-aliasing, no gradients, no soft shadows, no blur, no text, no labels,
no watermark, no border around the sheet.

Palette: ONLY these 16 exact colours (the standard EGA palette):
#000000 black, #0000AA blue, #00AA00 green, #00AAAA cyan, #AA0000 red,
#AA00AA magenta, #AA5500 brown, #AAAAAA light grey, #555555 dark grey,
#5555FF light blue, #55FF55 light green, #55FFFF light cyan, #FF5555 light red,
#FF55FF light magenta, #FFFF55 yellow, #FFFFFF white.
The BACKGROUND of every cell must be solid #FF55FF light magenta (it is the
transparency key); never use light magenta inside a sprite.

Layout: an exact 8 x 8 grid of 128x128 cells, row-major, row 1 at the top.
Each sprite is centred in its cell and drawn at the logical size given, scaled up by
the factor given (each logical pixel = a factor x factor block). Leave unused cells
plain light magenta. Black 1-pixel outlines, flat fills, Macintosh-icon clarity.

Row 1 - toolbar tool icons, logical 16x16, scale 6:
 1 select tool: classic black arrow mouse pointer with a white inner fill, pointing up-left
 2 wire tool: a short red jumper wire bent in a gentle S, with a small black square end at each end
 3 probe tool: a multimeter test probe pen lying diagonally, red handle bottom-left, grey metal tip top-right
 4 rotate tool: a black circular arrow (about 300 degrees of a circle) with an arrowhead
 5 undo tool: a black curved arrow sweeping left, like a U-turn arrow pointing left
 6 run: a solid green (#00AA00 with #55FF55 highlight) right-pointing play triangle with black outline
 7 pause: two upright yellow bars with black outlines
 8 step: a black right-pointing triangle followed by a black vertical bar
Row 2, logical 16x16, scale 6:
 1 reset: a black vertical bar followed by a black left-pointing triangle (rewind to start)
 2 document: a small white page with a folded top-right corner, black outline, three grey text lines
 3 status waveform: black rectangle containing a bright green (#55FF55) square wave, 16x10
 4 ground symbol: three horizontal black lines getting shorter downward under a short vertical stem
 5 power symbol: a red plus sign, a black battery-cell symbol (long and short bar), a black minus sign, side by side, 16x8
 6 clock source: a white box with black outline containing a black square-wave pulse train
 7 logic input: a small black toggle switch body with a lever and a white "1" mark-free dot (no text)
 8 logic lamp: a round indicator lamp with a black outline and green glass and a white glint
Row 3 - parts palette icons, logical 24x12, scale 5:
 1 wire: a straight horizontal red wire with a small black square terminal at each end
 2 resistor: a horizontal axial resistor, tan/beige body with rounded ends, four colour bands (brown, black, orange, gold-ish yellow), thin grey leads to both edges
 3 capacitor: a horizontal axial film capacitor, blue body with lighter blue highlight, grey leads to both edges
 4 LED: a red LED seen from the side, red dome top with a white glint, two grey legs going down
 5 switch: a small slide switch: grey base plate with a black lever tilted up-right
 6 555 timer: a black 8-pin DIP chip seen from above, four light-grey pins top and bottom, a notch on the left end
 7 logic IC: a black 14-pin DIP chip seen from above, seven light-grey pins top and bottom, notch on the left end
 8 potentiometer: a small blue square trimmer potentiometer with a white round adjuster with a slot, grey legs
Row 4 - board components, scale 5:
 1 resistor body, horizontal, logical 20x7: tan/beige rounded body (use yellow #FFFF55 and brown #AA5500 for shading), four vertical colour bands left to right drawn in PURE black, PURE dark grey, PURE light grey and PURE cyan (placeholders the program recolours), no leads
 2 resistor body, vertical, logical 7x20: the same body turned upright, bands top to bottom in black, dark grey, light grey, cyan placeholders
 3 electrolytic capacitor, logical 12x16: a standing blue (#0000AA with #5555FF highlight) cylindrical can seen from the front with a light-grey top rim, a white minus-stripe band on its right side, no legs
 4 ceramic disc capacitor, logical 10x10: an orange-brown (#AA5500 with #FFFF55 highlight) round disc, no legs
 5 LED unlit, logical 10x12: dark red (#AA0000) dome with a black outline and a small grey flange at the bottom, no legs
 6 LED lit, logical 10x12: bright red (#FF5555) dome with a white glint, black outline, grey flange, no legs
 7 slide switch OFF, logical 14x8: grey body with a black outline, the black slider knob at the LEFT end
 8 slide switch ON, logical 14x8: the same body with the slider knob at the RIGHT end and a small green dot
Row 5 - inspector picture and small parts, scale 4:
 1 big resistor, logical 48x14: a horizontal axial resistor with grey leads at both ends, tan body (yellow/brown shading) with four colour-band placeholders in PURE black, dark grey, light grey, cyan
 2 trimmer potentiometer, logical 12x12: blue square body, white round knob with a black slot
 3 logic input toggle, logical 10x10: black square body with a white lever
 4 indicator lamp off, logical 10x10: round lamp, black outline, dark grey glass
 5 indicator lamp on, logical 10x10: round lamp, black outline, bright green glass, white glint
 6 small red LED glow rays, logical 16x14: only five short bright red (#FF5555) dashes radiating up and outwards like light rays from a point at the bottom centre, nothing else
```

## What was kept

See `sprites.txt`'s header for which cells were drafted from the master and
which were drawn by hand afterwards.
