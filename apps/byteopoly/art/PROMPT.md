# Byteopoly art - generation prompts

The three master sheets `master1.png` (computers, 1254x1254), `master2.png`
(icons, 1024x1024) and `master3.png` (mascot and card backs, 1254x1254) were
made ONCE with the codex CLI's built-in image generation (codex-cli 0.169.0,
2026-11-17), one `codex exec --skip-git-repo-check --sandbox workspace-write -C <scratch> < pN.txt`
per sheet, run from a scratch directory outside the checkout. They are the one
sanctioned binary of the lane (as Breadboard XT's `master.png`).

They are a GUIDE, not the asset. The sheets did not honour the exact grid, so
`tools/os88bpart.py draft` finds each picture as a connected run of non-key
pixels, reads the pictures in reading order, quantises each to its logical
sizes (a computer is cut at 48x32, 32x24 and 16x12 from the same picture) and
writes `sprites.txt`. That file was then finished by hand and is THE SOURCE:
the dice (all six faces), the Workshop, coins, keycap, null-modem cable and the
two card frames were redrawn as text (the master's were mushy or misaligned), and
the `_M` monochrome twins were derived by `tools/os88bpart.py mono-draft`.
`make` reads only `sprites.txt`, `help.txt`, `art.h` and the WIN16 font art.

Selection: all three sheets were used as generated, first attempt, no
re-rolls. The concept boards under the owner's intake were the style target
(not traced).

## Shared preamble (every sheet)

```
Palette: ONLY these 16 exact colours (the standard EGA palette): #000000 black,
#0000AA blue, #00AA00 green, #00AAAA cyan, #AA0000 red, #AA00AA magenta, #AA5500
brown, #AAAAAA light grey, #555555 dark grey, #5555FF light blue, #55FF55 light
green, #55FFFF light cyan, #FF5555 light red, #FF55FF light magenta, #FFFF55
yellow, #FFFFFF white. The BACKGROUND of every cell must be solid #FF55FF light
magenta (the transparency key); never use light magenta inside a sprite. Style:
chunky pixel art on a logical pixel grid where one logical pixel is a solid
square block, hard edges, no anti-aliasing, no gradients, no soft shadows, no
blur, NO text, NO letters, NO numbers, NO labels, no watermark, no border or
grid lines. 1-pixel black outlines, flat fills, beige/cream plastic with grey
shading, Macintosh-System-1 icon clarity, broad recognisable silhouettes of
1980s computers (silhouette only: no trademark lettering, no logos). All art is
ORIGINAL and not copied from any commercial board game.
```

## Sheet 1 prompt (after the preamble; each began "Use your image generation tool to create ONE pixel-art sprite sheet PNG ... master1.png")

```

Layout: an exact grid of 4 columns x 5 rows of equal cells (each 256 wide x 204 tall), row-major, row 1 at the top. Each sprite is centred in its cell, drawn at LOGICAL size 64x40 pixels scaled up 4x (each logical pixel a 4x4 block), side view three-quarter or front, filling most of the cell. Cell contents in order:
1 Commodore PET: a chunky all-in-one box with a small green-screen monitor and a built-in keyboard slab below
2 VIC-20: a wedge-shaped beige keyboard computer with a few coloured function keys
3 C64: a wide beige breadbin keyboard computer with a rainbow-less light brown/dark brown key row
4 power: a black electric plug with two prongs and a cable, next to a yellow lightning bolt
5 TRS-80: a grey keyboard computer with a small monitor on top
6 Atari 800: a wedge-shaped silver/beige keyboard computer with a cartridge slot on top
7 ZX Spectrum: a small black rubber-keyed flat keyboard computer with a thin rainbow stripe at the corner
8 Apple II: a beige computer with a flat keyboard and a green-screen monitor sitting on top
9 Apple IIe: the same style as 8 but a platinum grey case, a separate monitor
10 Macintosh 128K: a small beige all-in-one box computer with a tiny black-and-white smiling screen and a floppy slot
11 BBS: a grey modem box with a row of four small green and red LEDs
12 IBM PC 5150: a beige desktop box with two floppy drives and a green CRT monitor on top, and a keyboard
13 PC XT 5160: like 12 but wider case with a hard disk, a monitor on top, a keyboard
14 PC AT 5170: wide desktop case with a key lock and a monitor on top, a keyboard
15 Amiga 500: a beige wide keyboard-computer with a slightly angled front and a separate small monitor
16 Atari ST: a wide cream keyboard computer with a separate small monitor
17 a 386 tower: an upright beige tower case with a 5.25 inch bay, a 3.5 inch drive and a turbo button, with a small CRT monitor next to it
18 a 486 tower: an upright taller beige tower with two bays and a few LEDs, with a CRT monitor next to it
19 and 20: leave plain light magenta.
```

## Sheet 2 prompt (after the preamble; each began "Use your image generation tool to create ONE pixel-art sprite sheet PNG ... master2.png")

```

Layout: an exact 8 x 8 grid of 128x128 cells, row-major, row 1 at the top. Each sprite is centred in its cell. Leave unused cells plain light magenta.
Row 1, logical 16x16 scale 8: 1 floppy disk (blue with a silver shutter, a white label), 2 a computer mouse (beige, one button, a tail wire), 3 a joystick (black base, red ball top), 4 a compact cassette tape (light grey body, two white reels), 5 a coin stack icon (yellow coins), 6 a padlock (grey, yellow body keyhole), 7 a blank beige keyboard keycap (rounded square, top lit white), 8 empty.
Row 2, logical 24x24 scale 5: six dice faces showing 1,2,3,4,5,6 pips (white die, black pips, black outline, a light grey shaded right and bottom edge), in cells 1-6; cells 7-8 empty.
Row 3: cell 1 a green RAM memory chip (logical 8x6 scale 14: black chip with 4 gold pins on each long side, a dark green body); cell 2 a workshop icon (logical 12x10 scale 10: a grey wrench lying on a brown workbench); cell 3 a mortgage mark (logical 12x10: a red banner with a white X); cells 4-8 empty.
Row 4, logical 32x24 scale 4: 1 BOOT corner: a thick red arrow pointing right with a short stem; 2 REPAIR SHOP: a grey wrench; 3 SWAP MEET: a stack of three floppy disks (blue, red, light grey) slightly fanned; 4 CRASH: a monitor whose screen is cracked with a green jagged crack; 5 Floppy Find: a blue floppy disk with a silver shutter; 6 System Error: a red warning triangle with a white exclamation mark (no letters, only the bar and dot); 7 Import Duty: a brown wooden crate with a stamped X; 8 E-waste Fee: a grey recycling bin with a green lid and a small broken monitor poking out.
Row 5: cell 1 a null-modem cable (logical 32x12, scale 4): a black cable in a loose curve with a grey 9-pin D-sub connector on each end. Cells 2-8 empty.
Rows 6-8 empty.
```

## Sheet 3 prompt (after the preamble; each began "Use your image generation tool to create ONE pixel-art sprite sheet PNG ... master3.png")

```

Layout: an exact 2 x 2 grid of 512x512 cells, row-major.
Cell 1: the mascot, logical 64x64 scale 8: a friendly beige all-in-one retro computer with a black screen showing a green smiling face (two dot eyes and a curved smile), a tiny keyboard below, stubby cartoon arms with white gloves, one hand raised waving. 
Cell 2: a card back for a "Floppy Find" deck, logical 48x64 scale 8 (centred, filling about 384x512): a portrait rectangle card, light blue background with a darker blue border, a big floppy disk silhouette in the middle, thin white inner frame.
Cell 3: a card back for a "System Error" deck, logical 48x64 scale 8: a portrait rectangle card, light red background with a dark red border, a big warning triangle silhouette with an exclamation bar in the middle, thin white inner frame.
Cell 4: empty.
```

Each sheet's "Use case:" line read "vintage-computer sprites" / "icons" /
"large illustrations for a 1987-style retro board game on a 16-colour EGA
screen." followed by the preamble; the full text is kept at
`~/Repos/byteopoly-logs/codex-art/p1.txt..p3.txt` and in this file above.
Measured: BPART.BIN 52,033 bytes.

Update (D14): the LARGE cut was re-made at 48x32 from master1 (the prompt above asked for 64x40 cells; the draft fits each picture to its bbox). BPART.BIN 52,033 bytes, lz4 16,180.

## Master 5: the uplift's pictures (art lane U6, 2026-11-17)

`art/sprites2.txt` (BPART2.BIN, SPEC.md 131.10.8) was drawn after the owner's
five concept boards (`1-board.png` .. `5-linklost.png` in the owner's intake:
1448x1086 renders of a 640x480 VGA screen, about 2.26 render pixels per VGA
pixel). The boards ARE the design, so no image generation was used this round
and there is no prompt: `master5.png` is a sheet of the crops of those boards
that each picture was drawn after, labelled with its art2.h name. `BP_A2_GAVEL`
has no picture on the boards and was drawn without one.

Method. A crop-and-quantise draft (each target pixel takes the EGA colour with
the most area, black favoured, the board's ground flood-filled to the key) came
out mushy at these sizes - doubled outlines, smeared letters, stray dither - so
every picture was instead CONSTRUCTED on its logical grid and then finished by
hand in the text: polygons, ellipses and stamped hand-drawn parts (the gloves,
the chip, the padlock, the rays, BOOT's small arrows), one 1-pixel black
outline, light from the top-left (a white top-left edge, a darker bottom-right
edge), and a drop shadow as a black checker on the key so it dithers over
whatever ground the picture lands on. Beige plastic, which EGA does not have, is
white for lit faces and a white/brown checker for faces in shade (it reads as
tan, close to the boards' (208,176,144)), solid brown only at the far edge. The
two cards were built upright at 8x with their lettering set in a bold sans
(the host's Tahoma Bold, used only to draw the bitmap), turned 10 degrees left
(Floppy Find) and 11 right (System Error) as the boards tilt them, and voted
down to 1x with ink favoured. The construction scripts were throwaway: from the
first commit on, the text is the source, as for sprites.txt.

Sizes that differ from art2.h's first plan, measured against the boards at the
FULL layout's 56x50 cells: the mascot is 96x84 (the board draws it about 105 VGA
pixels wide; 72x72 was half its area), the turn header's tokens are 32x32 (the
board's is 33x31), and the Repair Shop and Swap Meet corners are 40x28 so they
fit under a two-line corner label in a 50-pixel cell.

Mono: every colour picture was looked at through the core's 2x2 mapping
(SPEC.md 131.10.5) on white and on the centre's ground. Only BOOT's new arrow has
a twin (`BP_A2_M_C_BOOT`, black with a white lit edge), and the red status LED
(`BP_A2_M_LED_RED`, a black ball: green and red both map to white); the rest read
as they are, so the other reserved `_M` IDs have no record. Measured: 45
records, 29,171 bytes of packed 4bpp sprite records (the LEDs, IDs 311..314,
were added for the link status ball of 2-auction.png). BOOT's four arrows in `sprites.txt`
were redrawn pointing UP at their old sizes, so BPART.BIN stays 55,473 bytes.
