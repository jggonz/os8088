# Microsoft Word

Microsoft Word is a word processor modelled on Word for Windows 1.1a. It has
the same nine menus, ribbon, ruler, status line and dialogs, and it reads and
writes real Word `.DOC` files and RTF files. You can set text in bold,
italic, underline, word underline, double underline and small caps, hide
text, and give each paragraph its own alignment, line spacing and indents.
Search and Replace, Sort, Renumber and Table of Contents work on the whole
document or on a selection.

![Word with its welcome document open, showing bold, italic, a centred heading, a hanging indent and a flush-right line.](shot:word-window){full}

Word shows your document in Draft view: the text is set in the system's own
screen face, with bold, italic and the underlines drawn over it. You can also
switch to Page view, or set the document in one of the typefaces on the
system disk.

## Starting Word

Word comes on its own floppy. Put that disk in a drive, open it and
double-click **WORD.O88**, or double-click [[WELCOME.DOC]] beside it to open
Word with the welcome document already loaded. Double-clicking any `.DOC`
file opens it in Word.

The disk also has an empty **DOCS** folder for your own documents. Word needs
no special hardware. Each Word window holds one document; you can start Word
more than once if your machine has the memory.

The title bar shows **Microsoft Word -** and the name of the document. A new
document is called **DOCUMENT.DOC** until you save it under another name.

## The window

From the top, the window has:

- the **menu bar**, with File, Edit, View, Insert, Format, Utilities, Macro,
  Window and Help;
- the **ribbon**, for the typeface and character formats;
- the **ruler**, for paragraph formats and indents;
- the text, with a scroll bar on the right;
- the **status line**.

Open a menu by clicking its name, then click an item; or press the mouse
button on the name, drag to the item and let go. From the keyboard, hold
`Alt` and press the underlined letter of a menu (`Alt+F` for File), then use
the arrow keys and `Enter`, or press an item's underlined letter. `Esc`
closes a menu.

Items drawn in grey are features Word 1.1a had that this machine cannot
offer, such as printing, spelling, macros, styles, tables and footnotes.
They stay on the menus so the menus look as they did.

The status line shows `Pg` (the page), `Sec 1`, the page out of the total,
`At` and `Ln` (the line, counting 54 lines to a page) and `Col` (the column).
At the right end four lamps light up: **EXT** (extend mode), **CAPS**,
**NUM** and **OVR** (overtype).

## Typing and moving around

Click to put the caret where you want to type. The arrow keys, `Home`,
`End`, `PgUp` and `PgDn` move around. `Backspace` deletes to the left and
`Del` deletes to the right. `Enter` ends a paragraph. A `Tab` moves the text
to the next tab stop; stops are every half inch.

Press `Ins` to switch to overtype: what you type replaces the characters
under the caret, and the **OVR** lamp lights. Press `Ins` again to go back.

## Selecting text

Drag across text with the mouse to select it. You can also press `F8`: the
**EXT** lamp lights and the arrow keys then stretch the selection from where
the caret was. `Esc` or any edit ends extend mode. `Ctrl+A` selects the whole
document.

Typing replaces the selection. `Esc` removes the selection.

## Character formats

Select text and apply a format, or apply it with nothing selected and then
type. When part of a selection has a format and part does not, applying it
gives it to all; when all of it has it, applying it takes it off.

| Format | Key | Ribbon button |
|---|---|---|
| Bold | `Ctrl+B` | **B** |
| Italic | Format Character | **I** |
| Small caps | `Ctrl+K` | **K** |
| Underline | `Ctrl+U` | **U** |
| Word underline (words only, not the spaces) | `Ctrl+W` | **W** |
| Double underline | `Ctrl+D` | **D** |
| Hidden | Format Character | |
| Back to plain text | `Ctrl+Space` | |

The ribbon buttons appear pressed when the text at the caret has that
format. Hidden text takes up no room on the screen; it stays in the file.

**Format > Character...** opens a dialog with a check box for each format.
Tick the ones you want and click **OK**, or press `Enter`. `Esc` cancels.
Pressing an underlined letter ticks its box. The Font, Points, Color,
Position and Character Spacing parts are shown but cannot be changed.

![The Character dialog, with its seven check boxes.](shot:word-character){full}

## Paragraph formats

A paragraph format covers the paragraph the caret is in, or every paragraph
the selection touches.

| Format | Key |
|---|---|
| Left aligned | `Ctrl+L` |
| Centred | `Ctrl+C` |
| Right aligned | `Ctrl+R` |
| Justified (shown left aligned on screen) | `Ctrl+J` |
| Double spacing | `Ctrl+2` |
| Space before the paragraph | `Ctrl+O` |
| No space before | `Ctrl+E` |
| Indent half an inch more | `Ctrl+N` |
| Hanging indent | `Ctrl+T` |
| Undo a hanging indent | `Ctrl+G` |
| Back to the normal paragraph | `Ctrl+X` |

The ruler does the same with the mouse. Its top row has, from the left, the
**Style** box (always Normal), four alignment buttons (left, centre, right,
justified), three line spacing buttons (single, one and a half, double) and
two buttons for no space or a blank line before the paragraph. The four tab
buttons after them are grey. The lower row is a scale in inches that lines
up with the text. Its markers show the left, first-line and right indents of
the paragraph at the caret; drag a marker to move that indent.

**Format > Paragraph...** sets everything at once: **Alignment**, the
**Indents** (From Left, From Right and First Line, in inches), and
**Spacing** (Line 1, 1.5 or 2, and Before 0 li or 1 li). Click a box or press
`Tab` to move to it, and type a number such as `0.5`.

![The Paragraph dialog.](shot:word-paragraph){full}

## The ribbon

The ribbon has a **Font** box, a **Pts** box, the six format buttons (B I K
and U W D), two grey superscript and subscript buttons, and a ¶ button at the
right.

- **Font** lists **Pica**, the built-in screen face, and the typefaces in
  the system disk's `SYSTEM/FONTS` folder, such as [[CHARTER.F88]] and
  [[TIMES.F88]]. Pick one and the whole document is
  shown in it. Pick Pica to go back. The typeface is how Word shows the
  document; it is not saved in the file.
- **Pts** always reads 10.
- **¶** shows or hides the paragraph marks.

## Search, Replace and Go To

**Edit > Search...** (`Ctrl+F`) finds text. Type it in **Search For**. Tick
**Whole Word** to skip matches inside longer words, and **Match
Upper/Lowercase** to match capitals exactly. Choose a **Direction**, Up or
Down. The search goes round the document once. A match is selected; if there
is none, Word says **Search text not found**. `F4` finds the next match down
and `Shift+F4` the next one up.

A few codes find things you cannot type:

| Type | To find |
|---|---|
| `?` | any one character |
| `^?` | a real question mark |
| `^p` | a paragraph mark |
| `^t` | a tab |
| `^w` | any run of spaces and tabs |
| `^^` | a real `^` |
| `^` and a number | the character with that code |

**Edit > Replace...** adds a **Replace With** box and a **Confirm Changes**
check box. Without Confirm Changes, Word replaces every match and tells you
how many changes it made. With it, Word selects each match and asks
**Replace this occurrence?** with Yes, No and Cancel. In the replacement,
`^m` puts back the text that matched and `^c` puts in the clipboard.

![The Replace dialog.](shot:word-replace){full}

**Edit > Go To...** (`F5`) asks for a page number and moves to the top of
that page.

## Utilities

![The Utilities menu. Renumber and Sort work; the rest are grey.](shot:word-utilities){full}

These commands, and Table of Contents, work on the selected paragraphs, or
on the whole document when nothing is selected. Each one can be taken back
with a single Undo.

**Utilities > Sort...** sorts paragraphs. Choose **Ascending** or
**Descending**. The **Key Field** part says what to sort on: the **Key Type**
(Alphanumeric or Numeric), the **Separator** between fields (Comma or Tab) and
the **Field number**. Tick **Case Sensitive** to put capitals before small
letters. Paragraphs with the same key keep their order, so you can sort on
one field and then on another.

**Utilities > Renumber...** numbers paragraphs. Choose **All** paragraphs,
**Numbered Only** (renumber the ones that already have numbers) or
**Remove** (take the numbers off). **Automatic** starts at 1; **Manual**
starts at the number in **Start at**. **Format** gives the style: `1.`,
`(1)`, `I.`, `i.`, `A.` and `a.` give numbers, roman numerals or letters with
that punctuation. A tab follows each number.

## Insert > Table of Contents

**Insert > Table of Contents...** builds a list of headings at the caret,
each followed by a tab and its page number.

- **Use Heading Paragraphs** takes every paragraph whose first character is
  bold. Its level comes from its left indent: each half inch is one level
  deeper.
- **Use Table Entry Fields** takes every paragraph that starts with hidden
  text beginning `.C.`. The rest of the hidden text is the entry. `.CC.`
  makes a level 2 entry, `.CCC.` level 3, and so on.

**From** and **To** keep only some levels. If nothing matches, Word says
**No table of contents entries**.

## Views

**View > Draft** wraps the text to the width of the window. **View > Page**
wraps it to a six-inch column drawn in the middle of the window, with a tick
in the margin where each page starts. The ruler then measures the column.

![Page view, with the text column's edges drawn.](shot:word-page){full}

**View > Ribbon**, **View > Ruler** and **View > Status Bar** show and hide
those parts of the window.

## The File menu

![The File menu. The printing items are grey.](shot:word-file-menu){full}

| Item | Key | What it does |
|---|---|---|
| New... | | Starts an empty document called DOCUMENT.DOC |
| Open... | `Ctrl+F12` | Opens a document |
| Close | | Closes the window |
| Save | `Shift+F12` or `Ctrl+S` | Saves under the current name |
| Save As... | `F12` | Saves under a new name |
| Exit | `Alt+F4` | Closes the window, like Close |

If the document has changes, New, Open, Close and Exit first ask **Do you
want to save changes to** the document. **Yes** saves first, **No** throws
the changes away and **Cancel** goes back. `Y` and `N` answer from the
keyboard.

Save All, Find, Print, Print Preview, Print Merge and Printer Setup are grey.

## The Edit menu

| Item | Key | What it does |
|---|---|---|
| Undo | `Alt+Backspace` or `Ctrl+Z` | Takes back the last change; press again to go further back |
| Cut | `Shift+Del` | Moves the selection to the clipboard |
| Copy | `Ctrl+Ins` | Copies the selection to the clipboard |
| Paste | `Shift+Ins` | Puts the clipboard in at the caret |
| Search... | `Ctrl+F` | See above |
| Replace... | | See above |
| Go To... | `F5` | See above |

The clipboard is shared with the other programs, so you can paste text from
Note Pad and into it. Repeat, Paste Link, Header/Footer, Summary Info,
Glossary and Table are grey.

## The other menus

| Menu | Items that work |
|---|---|
| View | Draft, Page, Ribbon, Ruler, Status Bar |
| Insert | Table of Contents... |
| Format | Character..., Paragraph... |
| Utilities | Renumber..., Sort... |
| Macro | none |
| Window | **1** and the document's name, ticked |
| Help | About... (also `F1`) |

**Help > About...** shows the version and the credits. The menu bar at the
top of the screen also reads **Microsoft Word**, and its About item opens the
same box.

## Files

Word reads three kinds of file:

- **Word documents** (`.DOC`), including ones saved with Word's fast save.
  Bold, italic, the underlines, small caps, hidden text and the paragraph
  formats all come back.
- **RTF files**. A file that starts like RTF is read as RTF whatever its
  name.
- **Plain text**. Each line becomes a paragraph; tabs are kept.

**Save** writes a Word document, unless the name ends in `.RTF`, in which
case it writes RTF. **Save As** adds `.DOC` to a name you type without an
extension. Saving a new document with Save writes **DOCUMENT.DOC**; use Save
As to give it a name, for example in the **DOCS** folder.

## Good to know

- A document can hold about 30 KB of text. A longer file opens cut short and
  Word says **Truncated**.
- A document can use up to 256 different paragraph formats. Past that Word
  says **Too many formats**.
- A very long document with a great deal of formatting may be refused with
  **Too complex to save**. It stays open, so nothing is lost.
- Closing the window with its close box does not ask about unsaved changes.
  Use File > Close or Exit to be asked.
- On screen, justified paragraphs look left aligned, and centred or right
  aligned lines may sit a few pixels off. The file keeps the real setting.
- Some keyboards have no `F11` and `F12`; use the File menu or `Ctrl+S`.
- Italic and hidden text have no Ctrl key, because `Ctrl+I` and `Ctrl+H` are
  the same keys as `Tab` and `Backspace`. Use the ribbon or Format Character.
