# Note Pad

Note Pad is a plain text editor. It wraps long lines at word breaks to fit
the window, scrolls through a note of up to 16KB, and finds and replaces text,
with simple patterns if you want them. It saves ordinary text files with DOS
line endings, so a PC running DOS can read what you write, and it reads text
files written on DOS or on Unix.

![A short note in Note Pad.](shot:notepad-window){full}

## Starting Note Pad

Note Pad is in the `APPS` folder of the apps disk, and a copy is in the
`APPS` folder of the system disk too. Double-click **NOTEPAD.O88**. You can
also double-click any `.TXT` file to open it in Note Pad. You can open
several Note Pads at once; each has its own note and its own file name.

The window can be moved and resized like any other. Making it wider or
narrower re-wraps the text to the new width.

## Typing and moving around

Type, and the text appears at the caret. Press `Enter` to start a new line.
Lines wrap at the last word that fits, so you only press `Enter` to end a
paragraph.

| Key | What it does |
|---|---|
| Arrow keys | Move the caret one character or one line |
| `Home` / `End` | Go to the start or end of the line |
| `Backspace` | Delete the character before the caret |
| `Delete` | Delete the character after the caret |
| `Esc` | Clear the selection, or close the Find panel |

Use the scroll bar on the right to move through a long note. The view also
follows the caret as you type and move.

## Selecting, cutting and pasting

Drag across text with the mouse to select it. Typing replaces the selection,
and `Backspace` or `Delete` removes it. **Edit > Select All** (`Ctrl-A`)
selects the whole note.

To move a selection, press inside it and drag. A bar shows where the text
will go, and it lands where you let go. A plain click inside a selection
just puts the caret there.

**Cut**, **Copy** and **Paste** use the system clipboard, so you can copy
text between Note Pad and other programs such as TeXPad ([[TEXPAD.O88]]) and
ArtfulType ([[ARTFUL.O88]]).

## Undo

**Edit > Undo** (`Ctrl-Z`) takes back your last edit. A burst of typing with
no pause in it counts as one edit. Note Pad remembers your last five edits.
There is no Redo. When there is nothing left to take back, Note Pad says
**Nothing to undo**.

## Finding and replacing

Press `Ctrl-F`, or choose **Find > Find...**. A panel opens at the top of
the window. Type what you are looking for and press `Enter`: Note Pad selects
the next match and scrolls to it. The counter beside the **Rx** box shows
which match you are on and how many there are, for example `2/10`.

![The Find panel, with the second of ten matches selected.](shot:notepad-find){full}

Press `Ctrl-R`, or choose **Find > Replace...**, to add a **Repl** box for
the replacement text. `Tab` moves between the Find box, the Repl box and
the note.

![The Replace panel.](shot:notepad-replace){full}

The buttons on the panel:

| Button | What it does |
|---|---|
| **Next** | Find the next match (the same as `Enter` in a box) |
| **Repl** | Replace the selected match and find the next one |
| **All** | Replace every match in the note, from the top |
| **X** | Close the panel |

**Repl** only replaces a match that Find selected. If you selected some text
yourself, it finds the next match instead of overwriting your selection.
After **All** the panel says how many were replaced. One **Undo** takes back
a whole **All**.

Searching wraps around: after the last match it goes back to the first.
`F3` finds the next match and `Shift-F3` the previous one, even when the
panel is closed. Matching is case sensitive, so `the` does not find `The`.

### Patterns

Tick the **Rx** box to search with a pattern instead of plain text:

| In a pattern | Matches |
|---|---|
| `.` | Any character except a line break |
| `[abc]` | Any one of a, b or c |
| `[a-z]` | Any character from a to z |
| `[^abc]` | Any character except a, b or c |
| `*` | The thing before it, any number of times, or none |
| `+` | The thing before it, one or more times |
| `?` | The thing before it, once or not at all |
| `^` | The start of a line |
| `$` | The end of a line |
| `\` | Makes the next character an ordinary character |

So `^Call` finds lines that start with "Call", and `col.*r` finds "colour"
and "color". A pattern can hold up to twelve `*`, `+` and `?` marks; one
with more shows **Bad pattern**.

## The File menu

| Item | Key | What it does |
|---|---|---|
| New | | Empties the note and calls it `NOTES.TXT` again |
| Open... | `Ctrl-O` | Opens a text file, using the file dialog |
| Save | `Ctrl-S` | Saves the note under its current name, without asking |
| Save As... | | Saves the note under a new name, using the file dialog |

A new note is called `NOTES.TXT`, so **Save** on a brand-new note writes
`NOTES.TXT`. Use **Save As...** to give it a name of your own. After a save or
an open, a short message such as **Saved LETTER.TXT** appears in the corner
of the window.

If the note has unsaved changes, **New** first asks **Save changes to** the
note, the same question closing the window asks.

## The Edit and Find menus

| Item | Key | What it does |
|---|---|---|
| Edit > Undo | `Ctrl-Z` | Takes back the last edit |
| Edit > Cut | `Ctrl-X` | Moves the selection to the clipboard |
| Edit > Copy | `Ctrl-C` | Copies the selection to the clipboard |
| Edit > Paste | `Ctrl-V` | Puts the clipboard's text at the caret |
| Edit > Select All | `Ctrl-A` | Selects the whole note |
| Find > Find... | `Ctrl-F` | Opens the Find panel |
| Find > Find Next | `F3` | Finds the next match |
| Find > Replace... | `Ctrl-R` | Opens the panel with the Repl box |

**Find Previous** has no menu item; it is `Shift-F3`.

## Closing

If you close a Note Pad whose note has changed since it was last saved or
opened, it asks first.

![Note Pad asking before it closes.](shot:notepad-close){full}

**Save** saves the note and closes the window. If the note has never been
saved, the Save As dialog comes up first. **Discard** closes without saving,
and **Cancel** leaves the window open. Note Pad compares the note with what
was last saved, so typing a letter and deleting it again does not count as a
change.

## Files

Note Pad reads and writes plain text. On disk each line ends the DOS way, and
it reads files with DOS or Unix line endings alike. Characters other than
ordinary letters, digits, spaces and punctuation are left out when a file is
loaded, so tab characters disappear. A file larger than 16KB is refused.

## Good to know

- The `Tab` key does not type a tab. In the Find panel it moves between the
  boxes.
- The clipboard has a size limit. If a selection is too large, Note Pad says
  **Too big to copy**.
- **Note Pad > About Note Pad** shows who made it. **Note Pad > Close**
  closes this Note Pad.
