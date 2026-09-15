# Desktop wallpaper

Open the chip menu → **Control Panel → Wallpaper**. Click a choice to apply
it immediately; use **Previous** and **Next** to browse all 18 choices.
Close the panel to save the setting. **Theme default** restores the background
supplied by the Bright, Dark or Color theme.

- Solids: Black, White, Navy, Teal, Plum, Blue, Silver and Green.
- Retro patterns: Confetti, Zigzag, Checker and Weave.
- Technical patterns: Grid, Dots, Circuit and Hatch.
- **8088 die (AMD)**: a centered monochrome photograph of the processor die.

Patterns and the die image work on VGA, EGA, CGA and Hercules. Solid colors
use the adapter's normal monochrome reduction on CGA and Hercules. Wallpaper
ships in the standard kernel; the small kernel keeps its classic desktop.

The Control Panel category list shows eight rows at a time. When additional
categories are available, **Up** and **Down** appear below the list. These
scroll the categories without changing the selected settings page.

## Die photograph credit

The bitmap in `kernel/wallpaper-die.inc` is derived from
[AMD 8088 die.JPG](https://commons.wikimedia.org/wiki/File:AMD_8088_die.JPG)
by **Pauli Rautakorpi**, licensed under
[Creative Commons Attribution 3.0 Unported](https://creativecommons.org/licenses/by/3.0/).
The photograph was resized to 128×128 pixels and converted to monochrome
with contrast normalization and ordered 8×8 dithering. This bitmap retains
that license; the repository's MIT license applies to the surrounding code. No endorsement is implied.

Source image:
`https://upload.wikimedia.org/wikipedia/commons/8/89/AMD_8088_die.JPG`

The committed assembly is the bitmap's source representation, so ordinary
builds need no image tools, network requests, or image generation. It stores
256 successive 8×8 tiles, row-major, eight bytes per tile, bit 7 leftmost and
set bits white. It occupies 2,048 bytes; the renderer uses the existing clipped
pattern fill to restore even a one-pixel fragment of the image after damage.

The image-format conversion used ImageMagick:

```sh
magick AMD_8088_die.JPG -resize '128x128!' -colorspace Gray -auto-level \
  -ordered-dither o8x8 -depth 1 gray:die.raw
```

For tile `(tx, ty)`, row `r`, the assembly byte is
`die.raw[(ty * 8 + r) * 16 + tx]`.
