# Scribe

Scribe is a word processor built from Microsoft Word. It has Word's nine
menus, ribbon, ruler, status line and dialogs, and it reads and writes the
same Word `.DOC` and RTF files. On top of that, Scribe can put **pictures**
in a document: `.BMP`, `.PCX` and `.PIX` files, in up to 16 colours or in
black and white. Its own format is RTF, which keeps those pictures in full
colour.

![Scribe with its welcome document open.](shot:scribe-window){full}

You can set text in bold, italic, underline, word underline, double
underline and small caps, hide text, and give each paragraph its own
alignment, line spacing and indents. Search and Replace, Sort, Renumber and
Table of Contents work on the whole document or on a selection.

## Starting Scribe

Scribe comes on its own floppy. Put that disk in a drive, open it and
double-click **SCRIBE.O88**. The disk also holds two copies of the welcome
document, [[SCWELCOM.RTF]] and [[SCWELCOM.DOC]], and an empty **DOCS** folder
for your own work. Open a document with **File > Open...**.

Double-clicking a `.DOC` file opens Microsoft Word ([[WORD.O88]]), not Scribe, even when
both are on the same disk. Scribe never takes over a double-click; open your
documents from inside it.

Scribe needs no special hardware. The title bar shows **Scribe -** and the
document's name. A new document is called **DOCUMENT.RTF** until you save
it under another name.

## The window

From the top: the **menu bar** (File, Edit, View, Insert, Format, Utilities,
Macro, Window, Help), the **ribbon**, the **ruler**, the text with its scroll
bar, and the **status line**.

Click a menu name to open it and click an item, or press on the name, drag
to the item and let go. From the keyboard, hold `Alt` and press the menu's
underlined letter (`Alt+I` for Insert), then use the arrows and `Enter`, or
press the item's underlined letter. `Esc` closes a menu. Grey items are
features this machine cannot offer, such as spelling, macros, styles and
tables.

The status line shows `Pg` (the page), `Sec 1`, the page out of the total,
`At` and `Ln` (the line, counting 54 lines to a page) and `Col`. Its lamps
are **EXT** (extend mode), **CAPS**, **NUM** and **OVR** (overtype).

## Typing, moving and selecting

Click to place the caret. The arrows, `Home`, `End`, `PgUp` and `PgDn` move
around; `Backspace` and `Del` delete; `Enter` ends a paragraph; `Tab` moves
to the next stop, every half inch. `Ins` switches overtype on and off.

Drag with the mouse to select text, or press `F8` and stretch the selection
with the arrow keys; `Esc` or any edit ends that. `Ctrl+A` selects
everything. Typing replaces the selection, and `Esc` removes it.

## Character formats

Select text and apply a format, or apply it first and then type. If only
part of the selection has the format, applying it gives it to all of it;
otherwise it takes it off.

| Format | Key | Ribbon button |
|---|---|---|
| Bold | `Ctrl+B` | **B** |
| Italic | Format Character | **I** |
| Small caps | `Ctrl+K` | **K** |
| Underline | `Ctrl+U` | **U** |
| Word underline | `Ctrl+W` | **W** |
| Double underline | `Ctrl+D` | **D** |
| Hidden | Format Character | |
| Plain text again | `Ctrl+Space` | |

**Format > Character...** has a check box for each format. Tick them and
press **OK** (`Enter`), or **Cancel** (`Esc`). The Font, Points, Color,
Position and Character Spacing parts cannot be changed. Hidden text takes no
room on the screen and stays in the file.

## Paragraph formats

These apply to the paragraph at the caret, or to every paragraph the
selection touches.

| Format | Key |
|---|---|
| Left, centred, right, justified | `Ctrl+L`, `Ctrl+C`, `Ctrl+R`, `Ctrl+J` |
| Double spacing | `Ctrl+2` |
| A blank line before / none | `Ctrl+O` / `Ctrl+E` |
| Indent half an inch more | `Ctrl+N` |
| Hanging indent / undo it | `Ctrl+T` / `Ctrl+G` |
| Back to the normal paragraph | `Ctrl+X` |

The ruler's top row has the **Style** box (always Normal), the four
alignment buttons, the three line spacing buttons (1, 1.5, 2) and the two
space-before buttons; the four tab buttons are grey. Its lower row is an
inch scale lined up with the text, with markers for the left, first-line and
right indents. Drag a marker to move that indent.

**Format > Paragraph...** sets the **Alignment**, the **Indents** (From Left,
From Right, First Line, in inches) and the **Spacing** (Line 1, 1.5 or 2;
Before 0 li or 1 li). Justified text shows as left aligned on screen.

## The ribbon

**Font** lists **Pica**, the built-in screen face, and the typefaces in the
system disk's `SYSTEM/FONTS` folder, such as [[CHARTER.F88]] and [[TIMES.F88]]. Picking one shows the whole document in
that face; it is not saved in the file. **Pts** always reads 10. After the
B I K and U W D buttons come two grey superscript and subscript buttons, and
the **¶** button, which shows and hides paragraph marks.

## Pictures

**Insert > Picture...** opens the file dialog. Pick a picture and Scribe puts
it into the document at the caret and tells you its width and height. Press
`Enter` to start an empty paragraph first: a picture is drawn only when it
begins a line.

![The Insert menu. Picture and Table of Contents work.](shot:scribe-insert-menu){full}

![Choosing a picture file.](shot:scribe-picture-dialog){full}

Scribe reads:

- **BMP** files that are not compressed, in 16 colours or black and white;
- **PCX** files in 16 colours or fewer;
- **PIX** picture archives, from which it takes the first picture.

A picture behaves like one character: put the caret after it and press
`Backspace` to remove it. It follows the paragraph's left indent.

These messages mean the picture could not go in:

| Message | Why |
|---|---|
| **Not a picture file** | The file is not one of the three kinds |
| **Convert to 4-bit first** | It has more than 16 colours |
| **Compressed BMP not read** | The BMP is compressed |
| **Picture too big** | The picture is too large; about 320 by 250 pixels is the most |
| **That file is too big** | The file itself is too large to read |
| **No room for a picture** | The document already has eight pictures, or memory is short |

## Search, Replace and Go To

**Edit > Search...** (`Ctrl+F`) finds the text in **Search For**, with
**Whole Word**, **Match Upper/Lowercase** and an Up or Down **Direction**. It
goes round the document once and selects the match, or says **Search text
not found**. `F4` repeats it downwards and `Shift+F4` upwards. `?` matches
any character, `^p` a paragraph mark, `^t` a tab, `^w` spaces and tabs,
`^?` a question mark and `^^` a caret.

**Edit > Replace...** adds **Replace With** and **Confirm Changes**. Without
Confirm Changes every match is replaced and Scribe says how many; with it,
Scribe asks **Replace this occurrence?** for each one. In the replacement
`^m` repeats the matched text and `^c` inserts the clipboard.

**Edit > Go To...** (`F5`) moves to the top of a page.

## Sort, Renumber and Table of Contents

Each works on the selected paragraphs, or the whole document when nothing is
selected, and each is undone in one step.

**Utilities > Sort...** sorts paragraphs **Ascending** or **Descending**, on
a **Key Field**: Alphanumeric or Numeric, fields split by Comma or Tab, and
the field number. **Case Sensitive** sorts capitals first.

**Utilities > Renumber...** numbers **All** paragraphs, renumbers the
**Numbered Only**, or **Remove**s numbers. **Automatic** starts at 1,
**Manual** at **Start at**. **Format** is the pattern, such as `1.`, `(1)`,
`I.` or `a.`.

**Insert > Table of Contents...** lists headings at the caret, each with a
tab and its page number. **Use Heading Paragraphs** takes paragraphs that
start in bold, one level per half inch of indent. **Use Table Entry Fields**
takes paragraphs that start with hidden text `.C.` (level 1), `.CC.` (level
2) and so on. **From** and **To** choose the levels.

## Views

**View > Draft** wraps text to the window. **View > Page** wraps it to a
six-inch column with a tick where each page starts. **View > Ribbon**,
**Ruler** and **Status Bar** show and hide those parts.

## The File menu

![The File menu. Scribe has no printing items.](shot:scribe-file-menu){full}

| Item | Key | What it does |
|---|---|---|
| New... | | Starts an empty document called DOCUMENT.RTF |
| Open... | `Ctrl+F12` | Opens a document |
| Close | | Closes the window |
| Save | `Shift+F12` or `Ctrl+S` | Saves under the current name |
| Save As... | `F12` | Saves under a new name |
| Exit | `Alt+F4` | Closes the window |

Save All and Find are grey. New, Open, Close and Exit ask **Do you want to
save changes to** the document when it has changes: **Yes**, **No** or
**Cancel** (`Y` and `N` answer from the keyboard).

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo | `Alt+Backspace` or `Ctrl+Z` | Takes back the last change; press again to go further back |
| Cut | `Shift+Del` | Moves the selection to the clipboard |
| Copy | `Ctrl+Ins` | Copies the selection to the clipboard |
| Paste | `Shift+Ins` | Puts the clipboard in at the caret |
| Search... | `Ctrl+F` | Finds text |
| Replace... | | Replaces text |
| Go To... | `F5` | Moves to a page |

The clipboard is shared with Note Pad, Word and the other programs.

## The other menus

| Menu | Items that work |
|---|---|
| View | Draft, Page, Ribbon, Ruler, Status Bar |
| Insert | Picture..., Table of Contents... |
| Format | Character..., Paragraph... |
| Utilities | Renumber..., Sort... |
| Macro | none |
| Window | **1** and the document's name |
| Help | About... (also `F1`) |

![The About box.](shot:scribe-about){full}

## Files

Scribe opens Word documents (`.DOC`), RTF files and plain text, the same as
Word.

**Save** writes RTF, unless the document's name ends in `.DOC`, in which case
it writes a Word document. So a new document is saved as RTF; to make a
`.DOC`, use **Save As** and type a name ending in `.DOC`.

- In **RTF**, pictures keep all 16 colours. Other programs that read RTF,
  Word among them, open the text and leave the pictures out.
- In a **Word document**, pictures are saved in black and white.

Saving a new document with Save writes **DOCUMENT.RTF**; use Save As to name
it, for example in the **DOCS** folder.

## Good to know

- Give each picture a paragraph of its own. A picture in the middle of a
  line is not drawn, text typed after a picture on the same line does not
  show until you press `Enter`, and a second picture straight after the
  first is not drawn.
- A centred or right aligned picture can run past the right margin. Clicking
  on a picture puts the caret at its left edge.
- A document holds about 30 KB of text and up to eight pictures. The memory
  of a deleted picture comes back at the next File > New or Open.
- Closing the window with its close box does not ask about unsaved changes.
  Use File > Close or Exit.
- Some keyboards have no `F11` and `F12`; use the File menu or `Ctrl+S`.
