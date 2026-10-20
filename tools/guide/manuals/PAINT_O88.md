# Paint

Paint is a picture editor. You draw with the mouse on a canvas in sixteen
colours, using a pencil, an eraser, boxes, ovals, a flood fill and text. It
has one level of undo, a clipboard for cutting and pasting parts of the
picture, and a full-screen view. Pictures are saved as BMP or GIF files, and
Paint opens both.

![Paint with a box, a filled oval, a freehand line and some text.](shot:paint-window){full}

## Starting Paint

Paint is in the `APPS` folder of the apps disks and at the top of the Office
disk. Double-click **PAINT.O88** to start it with a blank canvas.
Double-clicking a `.BMP` or `.GIF` file, such as [[SAMPLE.BMP]] or
[[OS8088.GIF]], also starts Paint, with that picture open. You can have more
than one Paint open at a time; each has its own picture.

Paint works on every display. On a colour (VGA or EGA) screen you get all
sixteen colours. On a CGA or Hercules screen the palette offers three:
black, white, and a grey made of alternating black and white dots.

## The window

The **tool palette** runs down the left side: eight tool buttons in two
columns, and under them the canvas size. The **canvas** fills the rest of the
window. Along the bottom is the **strip**:

- four **width** buttons; the dark one is the width in use
- the **current colour**, in a well of its own
- the **colour swatches**; click one to make it the current colour
- the **Filled Shapes** button, a small square that is hollow or solid
- the **text size** button, which shows 1, 2 or 4

Click a tool button to pick that tool. It stays picked until you choose
another.

## The tools

| Tool | Left column / right column | What it does |
|---|---|---|
| Pencil | top left | Draws freehand in the current colour while you hold the button down |
| Eraser | top right | Rubs out to white while you hold the button down |
| Dropper | second row, left | Click the picture to make the colour under the pointer the current colour |
| Rectangle | second row, right | Drag from one corner to the other to draw a box |
| Oval | third row, left | Drag out a rectangle and Paint draws the oval that fits inside it |
| Selection | third row, right | Drag out a rectangle to select that part of the picture |
| Flood fill | bottom left | Click an area to fill it, and everything joined to it in the same colour, with the current colour |
| Text | bottom right | Click where the text should start, then type |

While you drag a box, an oval or a selection, an outline follows the pointer.
The shape is drawn when you let go.

### Line width

The four width buttons set how thick the pencil draws and how thick the edge
of a hollow box or oval is: 1, 2, 4 or 8 pixels. The eraser has a width of
its own, chosen with the same four buttons while the eraser is picked: 8,
16, 24 or 32 pixels. It starts at 16.

### Filled shapes

When **Filled Shapes** is on, the rectangle and oval tools draw solid shapes
in the current colour. When it is off they draw only the edge. Click the
square in the strip, or choose **Draw > Filled Shapes**, to switch.

### Text

![The Draw menu.](shot:paint-menu-draw){right}

Pick the text tool, click on the canvas and type. The letters go into the
picture in the current colour, at the text size shown in the strip. A thin
bar shows where the next letter will go.

- `Enter` starts a new line under the first one.
- `Backspace` takes the last letter away and puts back what was under it.
- A line that reaches the right edge carries on on the next line.
- `Esc`, or picking another tool, ends the text.

The text size is 1x, 2x or 4x. Click the size button in the strip to step
through them, or choose a size from the **Draw** menu.

## Selecting, cutting and pasting

Drag a rectangle with the selection tool. A dotted outline marks it.

![A selection drawn round the blue box.](shot:paint-select){full}

- **Edit > Copy** (`Ctrl-C`) copies the selected part of the picture.
- **Edit > Cut** (`Ctrl-X`) copies it and then fills it with white.
- **Edit > Clear** (`Delete`) fills it with white without copying it.
- **Edit > Paste** (`Ctrl-V`) puts the copied part back into the picture.
  Its top-left corner goes at the top-left corner of the current selection,
  or at the top-left corner of the canvas when nothing is selected.

Drawing with any other tool, or pressing `Esc`, removes the selection.
Paint's clipboard is its own. It holds a piece of picture, which other
programs cannot paste.

## Undo

**Edit > Undo / Redo** (`Ctrl-Z`) takes back the last thing you did. Choose
it again and the change comes back. Paint remembers one step. **File > New**
can be undone too.

## Changing the canvas size

A new canvas is 448 by 280 pixels, or smaller when the screen or the memory
cannot hold that much. There are two ways to change it:

- Drag the grow box in the bottom right corner of the window. The canvas
  follows the window. The picture stays in the top left corner, and new
  space is white.
- Type a width into the **W** box and a height into the **H** box under the
  tools, then click **Apply** or press `Enter`. `Esc` leaves the boxes
  without changing anything. Leave a box empty to keep that side as it is.

Paint will not make the canvas so small that part of your drawing would be
cut off. It keeps that side as it was and says **Would crop artwork**. If
there is not enough memory for a bigger canvas, it says **No memory to
resize**. On a CGA screen the canvas cannot be resized at all.

## Full screen

**View > Full Screen** (`Ctrl-F`, `F` or `Alt-Enter`) gives the whole screen
to Paint. The canvas grows to fill it, with the tools down the left and the
strip along the bottom, and every tool works as it does in the window.

![Paint full screen, with the canvas grown to 590 by 390.](shot:paint-full){full}

Press `Ctrl-F` or `F` again to go back to the window. `Esc` also goes back
when there is no text being typed and nothing selected. Open and Save are
not available in full screen; Paint says **Leave Full Screen first**.

## The File menu

| Item | What it does |
|---|---|
| New | Clears the canvas to white. You can undo it |
| Open... | Opens a BMP or GIF picture with the file dialog |
| Save Bmp | Saves the picture as a BMP file under its current name |
| Save as Bmp... | Asks for a name, then saves as BMP |
| Save Gif | Saves the picture as a GIF file under its current name |
| Save as Gif... | Asks for a name, then saves as GIF |

![The Save As dialog, with the name PICTURE.GIF filled in.](shot:paint-save){right}

The kind of file is set by the item you choose, and Paint changes the
name's ending to match. **Save Gif** on `PICTURE.BMP` writes
`PICTURE.GIF`. A picture that has never been saved asks for a name the
first time, starting from `PICTURE.BMP` or `PICTURE.GIF`. Paint shows **Saved**
when the file is written, and **Saving...** while it works.

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo / Redo | `Ctrl-Z` | Takes back the last change, or brings it back |
| Cut | `Ctrl-X` | Copies the selection and fills it with white |
| Copy | `Ctrl-C` | Copies the selection |
| Paste | `Ctrl-V` | Puts the copied part into the picture |
| Clear | `Delete` | Fills the selection with white |

When there is not enough memory for undo or the clipboard, the items say
**(NoRam)** and are greyed, and the keys answer with a short message such as
**No RAM for undo here**.

## The Draw menu

| Item | What it does |
|---|---|
| Filled Shapes | Switches between solid and hollow boxes and ovals |
| Text Size 1x | Normal size text |
| Text Size 2x | Text twice as big |
| Text Size 4x | Text four times as big |

## The View menu

| Item | Key | What it does |
|---|---|---|
| Full Screen | `Ctrl-F` | Gives the whole screen to Paint |

## Keys

| Key | What it does |
|---|---|
| `Ctrl-Z` | Undo or redo |
| `Ctrl-X`, `Ctrl-C`, `Ctrl-V` | Cut, copy, paste |
| `Delete` | Clear the selection to white. `Backspace` does the same when you are not typing text |
| `Esc` | End the text you are typing, or remove the selection |
| `Enter` | A new line of text |
| `Ctrl-F`, `F`, `Alt-Enter` | Full screen and back. `F` types a letter while you are using the text tool |

## Files

Paint opens and saves **BMP** and **GIF** pictures. It decides what a file
is from what is inside it, not from its name.

- It reads BMP files with 1, 4, 8 or 24 bits per pixel, as long as they are
  not compressed. Colours that are not among the sixteen are changed to the
  nearest of them.
- It reads GIF files, including interlaced ones. A GIF file larger than 64KB
  cannot be opened.
- Any other kind of file gets **Only BMP and GIF**. A compressed BMP gets
  **No compressed BMP**, and a JPEG is refused. PiXEL ([[PIXEL.O88]]) opens
  JPEG pictures and many other kinds.

While a picture loads Paint says **Decoding BMP** or **Decoding GIF**, then
**Opened**. A picture bigger than the screen or the memory allows is
opened cut down to fit. Paint then refuses to save it under its own name,
which would lose the rest of it, and says **Cropped - use Save As**. Save it
under a new name instead. The copy of Paint on the small system disks keeps
every picture in black and white; a colour picture opened there is changed
to black, white and grey, and saving it says **Reduced - use Save As** for
the same reason.

A very large picture may be too big to save as a GIF: Paint says **GIF too
big - try Bmp**.

## Good to know

- When you close a picture with unsaved changes, Paint asks **Save changes
  to PICTURE.BMP?** with Save, Discard and Cancel. It asks even if you undid
  your last change.
- On a CGA or Hercules screen a new canvas is black and white, and the grey
  is saved as the dot pattern you see. A colour picture opened there keeps
  all its colours in the file, even though the screen shows only three.
- The flood fill needs some memory of its own. When there is none, the fill
  button is grey and clicking it says **No RAM for flood fill**.
- **Paint > About Paint** shows who made it. **Paint > Close**, or the
  window's close box, closes it.
