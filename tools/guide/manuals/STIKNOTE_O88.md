# Sticky Note

Sticky Note is a pad of notes that lives on the desktop itself, under every
window. It shows one note at a time on a sheet of paper, and you turn the
pages to see the others. A note can be a few lines to remember or a
checklist whose boxes you tick with a click.

Each note is an ordinary text file, so you write and change it in Note Pad.
The pad shows the note, and reads it again when Note Pad closes. Once Sticky
Note is open it comes back by itself every time the machine starts.

![Sticky Note on the desktop, with its checklist open in Note Pad beside it.](shot:stiknote-desktop){full}

## Starting Sticky Note

Sticky Note is on the system disk, in the `STIKAPPS` folder. Double-click
**STIKNOTE.O88** to open it. You can also pick **Sticky Note** from the
**Sticky Apps** menu on the desktop's menu bar (see below).

Only one Sticky Note can be open. If you try to open a second, the menu bar
says **Sticky Note is open**.

The pad opens on the desktop as a sheet on a small stack of sheets, with a
name tag, **Sticky Note**, under it. It has no title bar and no dock tile.
Windows always cover it, never the other way round.

## The pad

![The welcome note on its yellow sheet.](shot:stiknote-welcome){right}

The top line of the sheet is the note's title, which is the first line of
the note. Under the rule are the next lines of the note. A line that is too
long for the sheet wraps onto the next. The sheet has room for eight lines
on a VGA screen, seven on Hercules or EGA and six on CGA; a longer note shows
its top.

Along the bottom are `<` and `>`, which turn to the note before or after,
the count, such as `1 / 2`, and an arrow in a box, which opens the note for
editing.

The first time you open it, the pad shows a note called **Welcome** that
comes with the system.

When there are no notes, the sheet says **No notes.** and **Press + to add
one.**

## Writing a note

Click the `+` at the top right of the sheet, or press `Ctrl+N`. Sticky Note
makes a new note whose first line is `New note`, turns to it, and opens it
in Note Pad.

To change a note, turn to it and click the arrow at the bottom right of the
sheet, or press `Enter`. The note opens in Note Pad, or in whichever program
you have chosen to open text files.

Write the note in Note Pad and save it with **File > Save** (`Ctrl+S`).
When you close Note Pad, the pad reads the note again and shows what you
saved. The first line is the title.

## Checklists

![A checklist on a pink sheet, with two of its boxes ticked.](shot:stiknote-checklist){left}

A line that starts with `[ ]` is shown on the pad as an empty box, and a
line that starts with `[x]` as a ticked one:

| In Note Pad | On the pad |
|---|---|
| `[ ] Floppy disks` | an empty box, then `Floppy disks` |
| `[x] Printer ribbon` | a ticked box, then `Printer ribbon` |

Click a box on the pad to tick it, or click a ticked box to clear it. The
note's file is changed at once. In Note Pad the box shows as `[ ]` or `[x]`.

## Turning, deleting and colours

Click `<` or `>`, or press `Left` or `Right`, to turn to the note before or
after. The pad remembers which note it was showing.

To delete the note on the pad, press `Del` (or choose **Note > Delete
Note**). The menu bar says **Delete again to confirm**. Press `Del` again
straight away to delete the note's file. Anything else in between cancels.
There is no undo.

![The Color menu, with Pink chosen.](shot:stiknote-color){right}

On a colour screen each note can have its own paper. Choose **Yellow**,
**Pink**, **Green**, **Blue** or **White** from the **Color** menu; the
paper the note has is marked `*`. New notes are yellow. On a black-and-white
screen the paper is always white, and every item in the Color menu is greyed
with **(color screen)**.

## The menus and the keyboard

![The Note menu.](shot:stiknote-menu){right}

The pad's menus show while it has the menu bar. Click the pad to give it
the bar. Clicking a window gives the bar to that window.

| Item | Key | What it does |
|---|---|---|
| New Note | `Ctrl+N` | Makes a new note and opens it in Note Pad |
| Delete Note | `Del` | Deletes the note on the pad, after a second press |
| Previous Note | `Left` | Turns to the note before |
| Next Note | `Right` | Turns to the note after |
| Open Note | `Enter` | Opens the note in Note Pad |

Items that cannot be used right now are greyed with the reason, such as
**Next Note (last)** on the last note or **Delete Note (none)** when there
are no notes.

The **Sticky Note** menu at the left of the bar has **About Sticky Note**,
which shows a line about it on the menu bar, and **Close**.

## Moving and closing it

![The name tag turns into a handle, with a close box at its left.](shot:stiknote-handle){right}

Point at the pad for a moment and its name tag turns into a black handle
with a close box at the left and a grip at the right. Drag the tag to move
the pad anywhere on the desktop. It remembers the place.

To close it, click the close box on the handle, or choose **Close** from the
**Sticky Note** menu. Closing it is the only thing that stops it coming
back. If it is open when you restart or switch off, it opens again at the
next start, in the same place, on the same note. Closing the pad does not
delete any notes.

## The Sticky Apps menu

Sticky Note is a Sticky App: a small program that belongs to the desktop.
When no window is in front, the desktop's menu bar has a **Sticky Apps**
menu ([[STIKMENU.DRV]]), where Sticky Note is listed as **Notes &
checklists**. Pick it when it is marked `Add` to open it, or when it is
ticked `On` to close it.

## Files

Every note is a text file in the [[NOTES]] folder, inside `SYSTEM\APPDATA`
on the system disk. A new note is named `NOTE0001.TXT`,
`NOTE0002.TXT` and so on. Any `.TXT` file you copy into the folder, from a
Disk window or from a DOS PC, shows up on the pad as a note, and you can
copy notes out the same way.

The pad keeps which note it shows, its place on the screen and the notes'
paper colours in `SYSTEM\APPDATA\STIKNOTE.DAT`. A note that is renamed or
copied loses its colour.

## Good to know

- The folder holds up to 30 notes. When it is full, `+` says **Notes folder
  is full**.
- If the system disk is write-protected or out of the drive, a new note or a
  tick says **Note not saved**.
- A tick rewrites the whole note, so a note larger than 16 KB, more than
  Note Pad itself can hold, refuses with **Note is over 16 KB**.
- The pad shows what Note Pad last saved, not what you are typing. It reads
  the note again when Note Pad closes, or when you turn the page.
- Do not tick boxes on the pad while the same note is open in Note Pad: if
  Note Pad saves afterwards, its copy replaces the tick.
- Sticky Note needs the desktop layer, so it is not on the small 128KB
  system disks.
