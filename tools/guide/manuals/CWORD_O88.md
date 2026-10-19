# Microsoft Word (C edition)

This is a second Microsoft Word, written again in the C language. It looks
the same as Word on the Word disk: the same nine menus, ribbon, ruler, status
line and dialogs, modelled on Word for Windows 1.1a. Its title bar and the
menu bar also say **Microsoft Word**. The differences are in what it can do
and in its file format: this edition reads and writes **RTF** files only,
and holds smaller documents.

This manual describes the C edition completely. Where it behaves like the
other Word, the text says so briefly; the [[WORD.O88]] manual has more
detail on those parts.

![The C edition with its welcome document open.](shot:cword-window){full}

## Starting it

The C edition comes on its own floppy. Put that disk in a drive, open it and
double-click **CWORD.O88**, or double-click [[WELCOME.RTF]], the welcome
document beside it, to start the program with that document open. The disk
also has an empty **DOCS** folder for your own documents.

Double-clicking an `.RTF` file opens it in the C edition. Each window holds one document. A new document is called
**Document** in the title bar until you save it.

## What is the same as Word

- **The window**: menu bar, ribbon, ruler, text with a scroll bar, and the
  status line with `Pg`, `Sec 1`, the page out of the total, `At`, `Ln`,
  `Col`, and the EXT, CAPS, NUM and OVR lamps. View > Ribbon, Ruler and
  Status Bar hide and show them.
- **The menus**: open them with the mouse (click, or press and drag) or with
  `Alt` and the underlined letter, then the arrows, `Enter` or an item's
  letter. Grey items are features this machine cannot offer.
- **Typing**: click to place the caret; the arrows, `Home`, `End`, `PgUp`,
  `PgDn`, `Backspace` and `Del` work as usual. `Ins` switches overtype on and
  off.
- **Selecting**: drag with the mouse, or press `F8` and stretch the selection
  with the arrow keys. Typing replaces the selection.
- **Undo**: Edit > Undo, `Alt+BkSp` or `Ctrl+Z` takes back the last thing
  done to the document: a run of typing, a run of deletions, a format, a cut
  or paste, a Replace, Sort or Renumber. Undo again puts it back. New and
  Open start with nothing to undo.
- **Cut, Copy and Paste**: Edit > Cut, Copy and Paste, or `Shift+Del`,
  `Ctrl+Ins` and `Shift+Ins`. The clipboard is shared with the other
  programs.
- **Character formats**: bold, italic, small caps, underline, word underline,
  double underline and hidden, from the ribbon's B I K U W D buttons, from
  **Format > Character...**, and from `Ctrl+B`, `Ctrl+K`, `Ctrl+U`, `Ctrl+W`
  and `Ctrl+D`.
- **Paragraph formats**: left, centred, right and justified; single, one and
  a half and double spacing; a blank line before; and left, first-line and
  right indents. The keys are `Ctrl+L`, `Ctrl+C`, `Ctrl+R`, `Ctrl+J`,
  `Ctrl+O` (space before), `Ctrl+E` (no space before), `Ctrl+N` (indent half
  an inch more), `Ctrl+T` (hanging indent) and `Ctrl+G` (undo the hanging
  indent). The ruler's indent markers can be dragged.
- **The Font box** lists Pica and the typefaces in the system disk's
  `SYSTEM/FONTS` folder, and sets the whole document in the one you pick. The
  ¶ button at the right of the ribbon shows and hides paragraph marks.
- **Views**: View > Draft wraps to the window, View > Page to a six-inch
  column with page ticks.
- **Go To**: Edit > Go To... (`F5`) moves to a page, counting 54 lines to a
  page.
- **Saving changes**: New, Open, Close and Exit ask **Do you want to save
  changes to** the document, with Yes, No and Cancel - or `Y`, `N` and `C`.
- **Search and Replace**: Edit > Search... (`Ctrl+F`) and Edit > Replace...,
  described below.

## What is different

**Files.** The C edition opens only RTF files. Any other file is refused with
**That is not an RTF file.** Save and Save As always write RTF, whatever the
name. The first save of a new document suggests the name **DOC.RTF**.

**What a file keeps.** Saving keeps the text, all seven character formats
and every paragraph's alignment, line spacing, space before and indents.
Draft and Page view show the alignment and the indents; every paragraph is
shown single spaced, ragged right and with no blank line before, whatever
it is set to, and keeps its setting in the file.

**Size.** A document holds up to 4,000 characters; past that the program says
**This document is full.** A longer file opens cut short. A document can use
64 different paragraph formats.

**Undo** has one level, as in Word: it undoes the last action, and Repeat
is grey. If there is no memory to keep a copy of the document, Undo stays
grey.

**Keys.** These work differently from Word:

| Key | In the C edition |
|---|---|
| `Ctrl+P` | Opens Go To |
| `Ctrl+S` | Save |
| `Ctrl+Z` | Undo, as well as `Alt+BkSp` |
| `F1` | About |
| `Tab` | Types a space; there are no tab characters |

`F12` (Save As), `Shift+F12` (Save) and `Ctrl+F12` (Open) work on a keyboard
and BIOS that send them; most PC and XT machines do not, so use `Ctrl+S` or
the File menu. `Alt+F4` exits. On some machines the BIOS does not pass on
`Ctrl+Ins` or `Alt+BkSp`; Edit > Copy and `Ctrl+Z` always work. There is no
`Ctrl+A`, `Ctrl+X`, `Ctrl+2` or `Ctrl+Space`, and `Esc` does not end extend
mode; press `F8` again.

**Menus grey themselves.** Save is grey until the document has changes, Cut
and Copy are grey with nothing selected, and Paste is grey when the
clipboard is empty.

![The Edit menu.](shot:cword-edit-menu){full}

**The Character dialog** has the seven check boxes and OK and Cancel. Its
Position and Color line is a label. The underline boxes are named Word
Underline and Double Underline.

![The C edition's Character dialog.](shot:cword-character){full}

**The Paragraph dialog** has Alignment (Left, Centered, Right, Justified),
Line Spacing (Single, 1 1/2 Lines, Double), a **Space Before** check box, and
three Indents boxes, **From Left**, **First Line** and **From Right**, in
inches to a tenth (`0.5"`). First Line is measured from the left indent and
can be negative, for a hanging indent. When a box is reached with `Tab` or
a click its value is selected, and typing replaces it.

![The C edition's Paragraph dialog.](shot:cword-paragraph){full}

**The ruler** has the Style box, four alignment buttons (L C R J), three
spacing buttons (1, 5 for one and a half, 2) and two buttons for a blank
line before (O) or none (C). Click a button to apply it. On the scale below
them, the lower mark at the left is the left indent, the upper one the first
line, and the mark at the right the right indent. Drag a mark along the
scale to move that indent; dragging the left indent leaves the first line
where it is, which is how a hanging indent is made.

**Search and Replace.** Edit > Search... has Search For, Whole Word, Match
Upper/Lowercase and Down or Up. It searches from the caret to the end (or
the start) of the document and does not go round. `F4` and `Shift+F4` repeat
it. The `^` and `?` codes are not understood: every character is matched as
itself. Edit > Replace... has a **Replace All** button that replaces every
match in the document and says how many it replaced. With **Confirm
Changes** ticked it selects each match in turn and asks **Replace this
occurrence?**: Yes (`Y`) replaces it, No (`N`) leaves it, and Cancel (`Esc`)
stops. One Undo takes back the whole Replace.

**Sort, Renumber and Table of Contents** run at once, with no dialog, on the
selected paragraphs or the whole document.

- **Utilities > Sort...** puts the paragraphs in alphabetical order, ignoring
  capitals.
- **Utilities > Renumber...** puts `1.`, `2.`, `3.` and so on at the start of
  each paragraph, replacing a number that is already there.
- **Insert > Table of Contents...** collects every short paragraph (3 to 40
  characters) that does not end in a full stop, and puts the list at the top
  of the document under a bold **Contents** heading.

**About.** Help > About... (or `F1`) says the program is a reimplementation,
in C, of the interface and file format of Word for Windows 1.1a, with the
Computer History Museum credit.

## The menus

| Menu | Items that work |
|---|---|
| File | New..., Open..., Close, Save, Save As..., Exit |
| Edit | Cut, Copy, Paste, Search..., Replace..., Go To... |
| View | Draft, Page, Ribbon, Ruler, Status Bar |
| Insert | Table of Contents... |
| Format | Character..., Paragraph... |
| Utilities | Renumber..., Sort... |
| Macro | none |
| Window | **1** and the document's name |
| Help | About... |

## Files

RTF in and out. Use **Save As** to name a document, for example in the
**DOCS** folder. The RTF it writes can be opened by Word, [[SCRIBE.O88]] and other
programs that read RTF.

## Good to know

- If a file is longer than 4,000 characters, the program says **The document
  was truncated to fit.** Do not save over the original.
- Closing the window with its close box does not ask about unsaved changes.
  Use File > Close or Exit.
- If the Font box cannot use a typeface it says **That typeface would not
  open.**
