# Fonts

The `FONTS` folder holds the typefaces installed on the system: ten faces,
one file each, and `LICENSE.TXT`, the licence and credits for the eight that
were made from other people's fonts. Programs that let you choose a font
find their faces here, so every program on the machine offers the same set.

![The FONTS folder on the system disk.](shot:fonts-folder)

## What is in it

| File | Face | What it is like |
|---|---|---|
| ARCHIVO.F88 | Archivo Narrow | bold, narrow sans-serif; fits more on a line |
| CHARTER.F88 | Charter | sturdy serif drawn for os8088 |
| COURIER.F88 | Courier | typewriter face, every character the same width |
| HELV.F88 | Helvetica | clean sans-serif |
| INCONSOL.F88 | Inconsolata | compact fixed-width, 106 characters to a line |
| JETBRAIN.F88 | JetBrains Mono | bold fixed-width face for code |
| NOTO.F88 | Noto Serif | bold, condensed serif for 1-colour screens |
| ROBOMONO.F88 | Roboto Mono | heavy fixed-width face with short lines |
| TALLX.F88 | Tallx | os8088's own 8 by 8 screen face |
| TIMES.F88 | Times | light book serif |

Each face has the 95 letters, figures and punctuation marks of plain ASCII,
in one style. They are drawn for the screen, at a single size.

## Where it is

`FONTS` is inside the `SYSTEM` folder of the system disk, in every size, and
of the emulator system disk and the live USB image and CD. Programs always
read faces from the disk the machine started from, so the faces work for a
program on any other disk too: Word on its own floppy in drive B still finds
them on drive A.

## Using it

- Double-click any `.F88` file to see that face in Font Viewer, and type
  your own words to compare faces.
- In Microsoft Word, the C edition of Word and Scribe, the **Font** box on
  the ribbon lists **Pica** (the built-in letters) and these faces. Picking
  one shows the whole document in it.
- Double-click `LICENSE.TXT` to read the licence in Note Pad.

## Good to know

- Leave the files where they are. A face moved out of `SYSTEM/FONTS` is no
  longer offered anywhere, and with the folder gone the font lists offer
  only Pica.
- The programs list the first ten faces they find in the folder. It already
  holds ten, so a face you copy in only shows up if you take another out.
- A program reads the list once, the first time it needs it. After
  changing the folder, close the program and open it again.
