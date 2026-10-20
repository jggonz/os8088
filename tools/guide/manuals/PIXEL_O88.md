# PiXEL

PiXEL is a picture viewer and editor. It opens photographs and drawings in
twelve file formats, from JPEG and PNG to MacPaint, and shows them in the
desktop's colours in a window, or full screen in up to 256 colours. You can
zoom and pan, step through every picture in a folder or run a slideshow, play
an animated GIF, and edit: turn, flip, crop and resize, change brightness and
contrast, and apply effects such as blur, sharpen and sepia. Edits can be
undone, and the result saved as PNG, GIF, BMP, PCX or PIX.

![PiXEL showing a photograph, with the Navigator, Histogram and Image Info panels on the right.](shot:pixel-window){full}

## Starting PiXEL

PiXEL has a disk of its own, with the program at the top and sample
pictures, such as [[FLOWER.JPG]] and [[CITY.PCX]], in the `PICTURES` folder. It is also at the top of the Office disk
and in the `APPS` folder of the larger apps disks. Double-click
**PIXEL.O88** to start it with an empty window, then open a picture.

Double-clicking a `.JPG`, `.PNG`, `.PCX`, `.TIF` or `.PIX` file starts PiXEL
with that picture. Each picture opened this way gets its own PiXEL window.
BMP and GIF files open in Paint ([[PAINT.O88]]) when double-clicked; use PiXEL's **File >
Open...** for those.

PiXEL works on VGA, EGA, CGA and Hercules screens. It needs a machine with
plenty of memory: a picture is held at one byte a pixel, and a large photo
is read at a half, a quarter or an eighth of its size when memory is short.

## The window

From top to bottom the window has:

- the **toolbar**: Open, Save, Prev, Next, Zoom In, Zoom Out, Fit, 1:1
  (Actual), Rotate and Slideshow
- the **tool column** on the left, with six tools
- the **canvas** in the middle, where the picture is
- the **panels** on the right: the Navigator, the Histogram and Image Info
- the **status bar** along the bottom

The status bar shows the file's name, its size in pixels, the format, the
zoom, the number of colours, the file's size in bytes, the free memory, and
where the picture is in its folder, such as `5 of 5`. Its `<` and `>`
buttons go to the previous and next picture.

On a CGA screen the window is shorter. The toolbar loses its captions and
only one panel shows at a time; press `Tab`, or click the panel, to turn to
the next one.

## Opening a picture

Choose **File > Open...** (`Ctrl-O`) or click **Open** on the toolbar, and
pick a file in the file dialog. The picture fills in as it is read, and the
name field says how far it has got, such as `Opening 42%`. While a picture
is opening the Open button reads **Stop**; click it or press `Esc` to stop.

A picture opens at **Fit**, the largest size at which all of it shows.

If PiXEL cannot read a file it says why, naming the format and the problem:
for example `JPEG: packing not read`, `PNG: cut short` or `BMP: not enough
memory`. A file that is no picture at all gets **Not a picture**.

## Looking at a picture

| To do this | Use |
|---|---|
| Zoom in or out a step | **Zoom In** / **Zoom Out** on the toolbar or the Navigator's `+` and `-`, or `+` and `-` on the keyboard |
| Show the whole picture | **Fit**, or `0` |
| Show it at its real size | **1:1**, **View > Actual Size**, or `1` |
| Move around a large picture | the arrow keys, `PgUp` and `PgDn`, the Hand tool, or a click or drag in the Navigator |

The zoom steps run from 1/8 to 8 times. The Navigator shows the whole
picture small, with a frame round the part the canvas shows.

The **Histogram** panel graphs how light the picture is. Its drop-down
switches the graph between **Luma** (brightness), **Red**, **Green** and
**Blue**, and under it are the Mean, Std Dev, Min and Max.

**Image Info** lists the File, Folder, Size, Format, Pixels, Depth, Packing
and Palette. `Pixels 640x480 at 1/2` means the picture was read at half size
to save memory. **File > Image Info...** shows the same lines in a card,
which is useful when the panels are hidden.

## Full screen

Choose **View > Full Screen**, or press `F` or `Alt-Enter`. PiXEL takes the
whole screen and shows the picture in the best mode the display has: 256
colours at 320x240 on a VGA, sixteen colours on an EGA, sixteen colours at
160x100 on a CGA, and black and green at 720x348 on a Hercules.

![The same photograph full screen in 256 colours.](shot:pixel-full){full}

**View > Screen** names the mode full screen will use, for example
`Screen: 320x240, 256`. Choose it to turn to the next mode the display
offers. On a VGA these are 320x240 and 320x200 in 256 colours, and
640x480 in sixteen colours chosen for the picture. On a CGA they are 160x100
in sixteen colours, 320x200 in four colours chosen for the picture, and
640x200 in black and white. On a Hercules there is only one mode, so the
item is grey.

Keys in full screen:

| Key | What it does |
|---|---|
| `F`, `Esc`, `Alt-Enter` | Back to the window |
| `Space`, `N`, the left button | The next picture in the folder |
| `Backspace`, `P`, the right button | The previous picture |
| `Home`, `End` | The first and the last picture |
| `+` or `=`, `-` | Zoom in, zoom out |
| `0`, `1` | Fit, actual size |
| the arrow keys | Pan when the picture is bigger than the screen |
| `S` | Start the slideshow |
| `M` | The next screen mode |

**View > Dither** chooses how full screen makes the in-between colours in
its sixteen-colour and two-colour modes: **Diffusion** (the default) or
**Ordered**. The window always uses the ordered pattern.

## Going through a folder

PiXEL keeps a list of the pictures in the folder of the open picture, sorted
by name. **Prev** and **Next** on the toolbar, **File > Previous Image** and
**Next Image**, or `Backspace` and `Space`, open the neighbouring picture;
the list wraps round at either end. `Home` and `End` open the first and the
last. The list holds up to 64 pictures.

**File > Slideshow**, or the **Slideshow** button, shows each picture in the
folder in turn at Fit, five seconds after the last one appeared. Choose
**Stop Slideshow**, click the button again, or press any key to stop.

An animated GIF plays when you choose **File > Play Animation**, or press
`A`. Press `A` again to stop it.

## The tools

| Tool | Key | What it does |
|---|---|---|
| Hand | `H` | Drag the picture to move around it |
| Zoom | `Z` | Click to zoom in at that point. Shift-click or the right button zooms out. Drag a rectangle to zoom to it |
| Marquee | `M` | Drag a rectangle to select part of the picture |
| Crop | `C` | Drag a rectangle, then press `Enter` or double-click to crop to it |
| Eyedropper | `E` | Shows the position, colour and colour number of the pixel under the pointer in the status bar. Click to pin it as the **Picked** line of Image Info |
| Rotate | `R` | Click to turn the picture a quarter turn clockwise, right-click to turn it anticlockwise |

![A selection made with the Marquee tool.](shot:pixel-select){full}

With a selection made, the arrow keys move it one picture pixel at a time,
`Enter` crops to it, and `Esc` removes it. **Edit > Select All** (`Ctrl-A`)
selects the whole picture.

## Editing

Every editing command works on the whole picture, except **Crop to
Selection**. A command that takes a while shows its progress in the status
bar, and `Esc` stops it and leaves the picture as it was. A command that
needs more memory than is free is refused before it starts, with the
numbers, for example `Blur needs 302K; 120K free`.

### The Image menu

![The Image menu.](shot:pixel-menu-image){full}

| Item | What it does |
|---|---|
| Rotate 90 CW | A quarter turn clockwise |
| Rotate 90 CCW | A quarter turn anticlockwise |
| Rotate 180 | A half turn |
| Flip Horizontal | Mirrors the picture left to right |
| Flip Vertical | Turns the picture upside down as in a mirror |
| Resize... | Scales the picture by 1/4, 1/3, 1/2, 2/3, 3/4, 3/2, 2, 3 or 4 times |
| Auto Levels | Stretches the picture's tones to run from black to white |
| Brightness/Contrast... | Brightness from -100 to 100 and contrast from -90 to 90 |
| Greyscale | Turns the picture to shades of grey |
| Invert | Makes a negative |

### The Effects menu

![The Effects menu.](shot:pixel-menu-effects){full}

| Item | What it does |
|---|---|
| Blur | Softens the picture |
| Sharpen | Makes edges crisper |
| Edge Detect | Leaves only the outlines |
| Emboss | Makes the picture look pressed into metal |
| Pixelate | Turns the picture into blocks of 8 by 8 pixels |
| Sepia | Brown old-photograph tones |
| Posterize... | Cuts each colour to between 2 and 16 levels |
| Threshold... | Turns the picture into pure black and white, split at the level you choose |
| Gamma... | Lightens or darkens the middle tones, from 0.3 to 3.0 |

### Cards

The items with `...` open a card over the picture. Each row has a value with
`-` and `+` buttons. On the keyboard, the left and right arrows change the
first row and the down and up arrows change the second, `Enter` presses
**OK** and `Esc` presses **Cancel**. There is no preview: the change is made
when you press OK.

![The Brightness/Contrast card.](shot:pixel-card){full}

### Undo

**Edit > Undo** (`Ctrl-Z`) takes back the last edit. The item names it, such
as **Undo Blur**, and afterwards it becomes **Redo Blur**. PiXEL remembers
one step, and opening another picture forgets it. **File > Revert**
(`Ctrl-R`) reads the picture back from the disk, dropping every edit.

## Saving

**File > Save As...** (`Ctrl-S`), or the **Save** button, opens a card where
you choose the format from the drop-down (the up and down arrows also step
through it). **Save...** then opens the file dialog for the folder and
name.

![The Save As card with its Format list.](shot:pixel-saveas){full}

| Format | What is written |
|---|---|
| PNG | 8-bit colour or grey, compressed |
| GIF | 256 colours, compressed |
| BMP | 8-bit, 256 colours |
| BMP 24-bit | full colour; offered for pictures read from a full-colour file |
| PCX | 8-bit, 256 colours |
| PIX | os8088's own picture format, in its sixteen colours. A picture larger than 64KB in this form is refused |

If the name you type ends in one of these formats, that format is used;
otherwise the card's format is added to the name. When a file of that name
already exists, PiXEL asks **Replace NAME?**.

PiXEL writes the new file under a temporary name and only replaces the old
one when the whole picture is down. A full disk, a write-protected disk or
`Esc` leaves the old file as it was, with a message such as **Disk full: not
saved**.

When you are about to leave a picture that has unsaved edits, by opening
another, going to the next one, starting a slideshow or closing the window,
PiXEL asks **Save changes to NAME?** with Save, Discard and Cancel.

## The menus

### File

| Item | Key | What it does |
|---|---|---|
| Open... | `Ctrl-O` | Opens a picture |
| Save As... | `Ctrl-S` | Saves the picture under a name and format you choose |
| Revert | `Ctrl-R` | Reads the picture again from the disk |
| Previous Image | `Backspace` | The previous picture in the folder |
| Next Image | `Space` | The next picture in the folder |
| Slideshow | | Starts or stops the slideshow |
| Play Animation | `A` | Plays or stops an animated GIF |
| Image Info... | | Shows the picture's details in a card |

### Edit

| Item | Key | What it does |
|---|---|---|
| Undo | `Ctrl-Z` | Takes back the last edit, or brings it back |
| Copy Info | `Ctrl-C` | Puts one line of text about the picture on the clipboard |
| Select All | `Ctrl-A` | Selects the whole picture |
| Deselect | `Esc` | Removes the selection |
| Crop to Selection | `Enter` | Keeps only the selected part |

**Copy Info** copies a line such as `CITY.PCX 320x240 PCX`, with the
selection and the picked pixel added when there are any, so you can paste
it into Note Pad. PiXEL does not copy the picture itself; use Save As.

### View

| Item | Key | What it does |
|---|---|---|
| Zoom In | `+` | Zooms in a step |
| Zoom Out | `-` | Zooms out a step |
| Fit | `0` | Shows the whole picture |
| Actual Size | `1` | One picture pixel to one screen pixel |
| Full Screen | `F` | Shows the picture on the whole screen |
| Screen: ... | | Chooses the full-screen mode |
| Dither: ... | | Diffusion or Ordered, for full screen |
| Hide Panels | | Hides the panels to give the canvas more room. It then reads Show Panels |
| Keyboard Help | `F1` | Shows the key card |

![The key card, from View > Keyboard Help.](shot:pixel-help){full}

## Files

PiXEL decides what a file is from what is inside it, so a picture with the
wrong ending still opens. It reads:

| Format | Endings | What it reads |
|---|---|---|
| JPEG | `.JPG`, `.JPE` | baseline and progressive, colour and grey |
| PNG | `.PNG` | every standard colour type and depth, including interlaced pictures |
| GIF | `.GIF` | still and animated |
| BMP | `.BMP` | 1, 4, 8, 16, 24 and 32 bits, plain or RLE-compressed |
| PCX | `.PCX` | black and white, 16 colours, 256 colours and full colour |
| TIFF | `.TIF` | grey, palette and RGB, uncompressed, LZW or PackBits; the first picture in the file |
| TGA | `.TGA` | colour-mapped, full colour and grey, plain or compressed |
| PNM | `.PBM`, `.PGM`, `.PPM`, `.PNM` | all six kinds |
| PIX | `.PIX` | os8088's own picture format |
| ICO | `.ICO`, `.CUR` | Windows icons and cursors |
| ILBM | `.LBM`, `.IFF` | Amiga and Deluxe Paint pictures |
| MacPaint | `.MAC` | black-and-white MacPaint pictures |

A picture can be up to 8,192 pixels each way.

## Good to know

- The window shows the picture in the desktop's sixteen colours, or in
  black and white on CGA and Hercules. Full screen shows more colours where
  the display has them.
- Editing and saving are slow on an XT: a turn or a flip takes about ten
  seconds on a 320 by 240 picture, a blur almost a minute, and a PNG save
  about two minutes. The status bar shows the progress, and `Esc` stops.
- In full screen, Next and Prev say **Save the edit first** when the picture
  has unsaved edits, because there is no room for the question there.
- Any key or click takes a card or the key card down.
- PiXEL's About card is **PiXEL > About PiXEL**. **PiXEL > Close**, or the
  window's close box, closes it.
