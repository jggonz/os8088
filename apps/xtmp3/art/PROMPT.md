# XTMP3 art masters

Made with the codex CLI's image generator (codex-cli 0.163.0, `image_generation` stable), one `codex exec --skip-git-repo-check --sandbox workspace-write` per picture, from a scratch directory outside the repo, on 2026-10-22. These PNGs are the MASTERS: `tools/os88x3art.py` derives the package's bitmaps from them deterministically, and `make` never asks a generator. To change a picture, regenerate the master by hand with the prompt below, look at it, and commit it.

## knob.png

```
Use your image generation tool to create a 1980s hi-fi stereo VOLUME KNOB as 
seen straight from the front, as low-resolution PIXEL ART for a 16-colour 
EGA/VGA palette (black, dark grey #555555, light grey #AAAAAA, white #FFFFFF 
only). Logical grid 64x64 pixels, rendered with each logical pixel as an 8x8 
block (512x512 PNG). A round machined aluminium knob, diameter 52 logical 
pixels, centred; radial brushed-metal look made ONLY with ordered (Bayer) 
dithering between the four greys - no gradients, no anti-aliasing, hard pixel 
edges. Light from the upper left: bright highlight upper-left, dark grey 
shading lower-right, a 2-pixel black outline and a 2-pixel dark grey drop 
shadow to the lower right. A short vertical white indicator groove from the 
centre toward 12 o'clock, outlined in black. The background outside the knob 
must be flat light grey #AAAAAA. No tick marks, no text, no logos, no 
watermark. Save the PNG into the current directory as knob.png and print its 
path.
```

## vu.png

```
Use your image generation tool to create the FACE of a vintage analog VU 
meter as low-resolution PIXEL ART for a 16-colour EGA/VGA palette. Logical 
grid 84x56 pixels, rendered with each logical pixel as a 6x6 block (504x336 
PNG). Flat cream/ivory dial face (use pale yellow #FFFF55 checker-dithered 
with white #FFFFFF), a thin black arc scale across the upper half from 
lower-left to lower-right, with short black tick marks, the rightmost quarter 
of the arc thickened and coloured red #AA0000 (the overload zone), a black 
needle pivot as a small dark semicircle at the bottom centre. NO needle, NO 
numbers, NO letters, NO text at all. Hard pixel edges, no gradients, no 
anti-aliasing, a 1-pixel black frame around the face. Save the PNG into the 
current directory as vu.png and print its path.
```

## logo.png

```
Use your image generation tool to create the wordmark "XTMP3" as 
low-resolution PIXEL ART: very bold, wide, extended, slightly squared 1980s 
hi-fi brand lettering, solid black on a flat white background, letters 
touching a common baseline, heavy strokes (about 4 logical pixels thick). 
Logical grid 96x20 pixels, each logical pixel rendered as an 8x8 block 
(768x160 PNG). Exactly the five characters X T M P 3, nothing else - no other 
text, no tagline, no border, no shading, no anti-aliasing, hard pixel edges. 
Save the PNG into the current directory as logo.png and print its path.
```

