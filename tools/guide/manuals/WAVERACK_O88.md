# WaveRack

WaveRack records, plays and edits WAV sound files. Its window looks like a
digital sound recorder from the 1980s: a display with the file's name and the
time, a level meter, the transport keys, and below them the waveform of the
whole sound, which you can zoom into, select, cut and paste. A take you
record goes straight onto the disk as a WAV file, and opens as the document
when you stop.

WaveRack never loads a whole file into memory. It reads the sound from the
disk as it needs it, so a WAV of many megabytes opens on a small machine,
and nothing you edit changes the original file until you save.

![WaveRack with a long WAV open, its waveform filling the window.](shot:waverack-window){full}

## Starting WaveRack

WaveRack is in the `APPS` folder of the 1.44MB and 1.2MB software disks.
Double-click **WAVERACK.O88**; several can be open at once. Open a WAV with
**File > Open...** or the **OPEN** key. Double-clicking a `.WAV` file opens
the Audio Player, not WaveRack.

To play and record, WaveRack needs a Sound Blaster and [[SOUND.DRV]]
loaded. Without one, **Play** and **Record** are greyed and say **Needs a
Sound Blaster** or **No capture device**, and everything else works.
Playing through the PC speaker or a Covox is not offered.

WaveRack is happiest on a hard disk. A long WAV plays from a hard disk
while you work in other windows, and a take is recorded onto it at full
rate. On a floppy, a take records at 8,000 Hz only.

## Opening a WAV

WaveRack opens PCM WAV files of 8 or 16 bits, mono or stereo, at any rate
from 1,000 Hz, up to 2 GB. The status bar shows **Scanning 45%** while it
reads the file once to draw the waveform; **Escape** or **Stop** cancels.
A file it cannot open says why: **Not a RIFF/WAVE file**, **File is
compressed**, **24-bit is unsupported** and so on.

The display shows the name, the cursor's time, the length and the format.
A take that was interrupted before it was finished opens with
**Recovered; save to fix**: save it to repair its header.

## Looking at the waveform

Stereo files show two lanes, **L** and **R**. The strip under the waveform
is the overview of the whole sound, with the part on screen marked.

- **Zoom In** (`+`) and **Zoom Out** (`-`), or the **+** and **-** keys,
  zoom around the cursor, down to one sample a pixel. **Fit** (`F`) shows
  the whole sound.
- `Left` and `Right` move the cursor a pixel; `Home` and `End` go to the
  start and the end; `PgUp` and `PgDn` move the view by most of a screen.
  The mouse wheel over the waveform or the overview scrolls the view.
- Click in the waveform to put the cursor there.

## Selecting

Drag across the waveform to select a part; drag a handle at either end to
change it, or Shift+click to extend it. `Shift+Left` and `Shift+Right` extend
it a pixel at a time. The Selection field shows its start and end.
**Select All** (`A`) and **Select None** are on the Edit menu.

![A selection in the waveform, shown in the Selection field.](shot:waverack-selection){right}

## Playing

**Play** (the **PLAY** key or `Space`) plays the selection, or from the
cursor to the end when nothing is selected. `Space` pauses and plays on;
**Stop** (`S` or `Escape`) stops and puts the playhead back where it
started. **Loop** (`L`, or the **Loop selection** box) repeats the
selection, or the whole sound with none.

The meter shows **LEVEL  PLAY** and the level of what is playing; **PEAK**
lights when a sample reaches full scale. **VOLUME** sets how loud it plays,
with the slider, `[` and `]`, or the wheel elsewhere in the window. It
never changes the file.

The card plays 8-bit mono. A stereo or 16-bit file is mixed down as it
plays, and the status bar says the rate it plays at. A file a slow machine
cannot read or convert fast enough is first loaded into memory
(**Loading 45%**, `Escape` cancels) when it fits; when it does not, it plays
with gaps and says **Disk too slow: gaps** or **CPU too slow: gaps**.

## Recording

Choose the rate for the next take with the **Rate** list in the format row
or **Sound > Rate**: 8,000, 11,025 or 15,000 Hz. Takes are 8-bit mono from
the card's microphone input.

Press **Record** (the **RECORD** key or `R`). WaveRack makes a file named
`VOICE001.WAV` (the next free number) in the folder it is working in, and
the status bar says how much the disk holds, for example
**C: 312 KB free - 28 s**. The time runs, the meter shows **LEVEL  REC**,
and the waveform grows as you record.

- **Record** or **Pause** again pauses the take; press either to go on.
  Nothing is lost or doubled across a pause.
- **Stop** ends the take. It opens as the document, already saved.
- **INPUT GAIN (DIGITAL)** boosts or cuts the take from -12 to +12 dB, with
  the slider or `<` and `>`. It is applied to the sound before it is
  written, and it works while you record. It cannot undo a signal that was
  already too loud at the card.
- While a take runs, only Stop, Pause, Record, the theme and the layout
  work; everything else says **Stop the take first**.

If the disk fills, WaveRack stops and keeps what it wrote: **Disk full:
take kept**. If the disk falls behind, it keeps the take up to that point:
**Overrun: kept to 1:23**. A card that delivers nothing ends with **No
input from the card** and leaves no file. On a floppy, **Record** is greyed
at rates above 8,000 Hz and says **Floppy: use 8,000 Hz**: a floppy cannot
write faster than that while it records. An original Sound Blaster 1.0 or
1.5 cannot record into WaveRack: the first try says **Needs a DSP 2.00+
card**, and **Record** stays greyed after that.

If the machine stops in the middle of a take, the file is still there.
The next time you start WaveRack, **File > Recover Take** opens it. A take
that another WaveRack window is still recording is never offered, and
saving over it says **Take still recording**.

## Editing

**Trim** (`T`) keeps only the selection; **Delete** (`Del`) removes it;
**Cut** (`X`), **Copy** (`C`) and **Paste** (`V`) work through WaveRack's
own clipboard, which keeps its contents when you open another file, so you
can paste a part of one WAV into another of the same format. **Undo** (`U`)
and **Redo** (`Y`) go back and forward through your last edits. Editing
needs the transport stopped.

Edits are kept as a list of pieces of the original files, so a cut is
instant at any length. A very fragmented document says **Too many edits;
save**: save it, and it becomes one piece again. An edited document shows
`*` after its name in the title bar.

## Saving

**Save** (the **SAVE** key) writes the document to its file; **Save As...**
(the **SAVE AS...** key) to a new name; **Save Selection As...** writes only
the selection. WaveRack first writes a whole new file under a temporary
name and only then replaces the old one, so a full disk or a cancelled save
(`Escape`) leaves your original untouched. Replacing another file asks
first. New, Open, Record and closing the window ask whether to save an
edited document.

## The File menu

| Item | Key | What it does |
|---|---|---|
| New | | An empty document |
| Open... | | Opens a WAV |
| Save | | Saves the document to its file |
| Save As... | | Saves it under a new name |
| Save Selection As... | | Saves only the selection |
| Recover Take | | Opens a take that was interrupted |

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo | U | Takes back the last edit |
| Redo | Y | Puts it back |
| Cut | X | Removes the selection to the clipboard |
| Copy | C | Copies the selection |
| Paste | V | Inserts the clipboard at the cursor, or over the selection |
| Delete | Del | Removes the selection |
| Trim | T | Keeps only the selection |
| Select All | A | Selects the whole sound |
| Select None | | Clears the selection |

## The View menu

| Item | Key | What it does |
|---|---|---|
| Zoom In | + | Closer |
| Zoom Out | - | Further |
| Fit | F | The whole sound |
| Compact Layout / Full Layout | | Switches the window's layout |
| Theme: Color / Theme: Black and White | | Switches the colours |

## The Sound menu

| Item | Key | What it does |
|---|---|---|
| Play/Pause | Space | Plays, pauses and plays on |
| Stop | S | Stops playing or recording |
| Record | R | Records, or pauses and resumes a take |
| Loop: Off / Loop: On | L | Repeats the selection |
| Rate | | Picks the next take's rate: 8,000, 11,025 or 15,000 Hz |

![The compact layout on a small screen.](shot:waverack-compact){left}

## Layouts and screens

WaveRack picks a full, a medium or a compact face to suit the screen: VGA
gets the full one, EGA and Hercules the medium one, and a CGA's 640 x 200
screen the compact one, with every control still there. **View > Compact Layout** chooses the compact face on any screen. On
a one-colour screen the theme is black and white.

## Files

- WaveRack opens and saves `.WAV` files. Takes are `VOICEnnn.WAV`, in the
  folder WaveRack is working in.
- It keeps a small note, `WAVERACK.TAK`, in the system disk's
  `SYSTEM\APPDATA` folder while a take is running. That is how it knows to
  offer **Recover Take**.

## Good to know

- WaveRack is on the 1.44MB and 1.2MB software disks, not on the 720KB or
  360KB ones.
- Recording at 11,025 Hz onto a hard disk leaves an XT more than half its
  time, so the desktop stays usable while you record.
- A take on a hard disk keeps going while you open menus and other windows;
  on a floppy, keep to 8,000 Hz.
- WaveRack does not convert formats: a document saves in its own format.
