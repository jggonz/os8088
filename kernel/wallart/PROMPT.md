You are drawing DESKTOP WALLPAPER TILES for os8088, a Macintosh System 1 / Windows 3.1 style GUI operating system for the IBM PC/XT, written in 8086 assembly. Use your built-in image generation tool. Save every image as a PNG in the current working directory with the exact file names given below. Do not write any other files.

A wallpaper is a small picture REPEATED edge to edge across the whole desktop, like Windows 3.1's wallpaper (Tile mode) or OS/2's background bitmap. Each tile must therefore be SEAMLESS: its right edge continues its left edge and its bottom edge continues its top edge, so a grid of copies shows no seams.

Hard constraints, because the OS converts each image to its real form automatically:
- Every tile is 64 x 64 logical pixels. Draw it ENLARGED 8 times, as a 512 x 512 image in which every logical pixel is an exact 8 x 8 block of one flat colour, aligned to the 8 px grid. No anti-aliasing, no gradients, no blur, no noise, no texture inside a block.
- COLOUR tiles use ONLY the 16-colour EGA/VGA palette: black #000000, blue #0000AA, green #00AA00, cyan #00AAAA, red #AA0000, magenta #AA00AA, brown #AA5500, light grey #AAAAAA, dark grey #555555, light blue #5555FF, light green #55FF55, light cyan #55FFFF, light red #FF5555, light magenta #FF55FF, yellow #FFFF55, white #FFFFFF. A single tile should use about 4 to 8 of them. Hand-placed ordered dithering (checkerboards of two palette colours) is welcome for shading - that is how 1990 artists shaded.
- MONO tiles are pure black and white only (two colours, #000000 and #FFFFFF), drawn by hand for a monochrome screen as a 1-bit artist would: crisp line work and 50% checkerboard greys, not a thresholded photo. They are 64 x 32 logical pixels (the mono screens have tall pixels), so draw them as 512 x 256 images with exact 8 x 8 blocks.
- Wallpaper sits BEHIND windows and white desktop icons with black labels, so keep the contrast calm: no large areas of pure white, nothing that competes with text. Low-contrast, mid-tone designs read best.
- Period-plausible, late 1980s / early 1990s. Not modern flat design, no photographs, no text or letters, no logos of real companies.

Draw these six designs, each as a colour tile AND a mono tile (twelve images):

1. `bricks-c.png` / `bricks-m.png` - a running-bond brick wall: brown/red bricks with dark grey mortar lines, a light-red highlight on the top edge of each brick.
2. `weave-c.png` / `weave-m.png` - an over-under basket weave of cyan and blue ribbons, the kind of pattern Windows 3.1 shipped as "Weave".
3. `circuit-c.png` / `circuit-m.png` - a green printed circuit board: dark green ground, lighter green traces turning at 45 degrees, round light-grey solder pads and a few small chips.
4. `floppy-c.png` / `floppy-m.png` - a scattered pattern of small 5.25-inch floppy disks (each about 16 x 16 logical pixels: black jacket, a light-grey label, the hub hole) on a dark blue ground, offset so the repeat is not obvious.
5. `waves-c.png` / `waves-m.png` - a calm Japanese-style seigaiha wave pattern of overlapping arcs in blue, light blue and cyan.
6. `stone-c.png` / `stone-m.png` - a flagstone / cobblestone floor of irregular grey stones (light grey, dark grey) with black gaps.

Check each image before saving: exactly the stated pixel size, every 8 x 8 block one flat colour from the allowed palette, and the tile repeats seamlessly.
