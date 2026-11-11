# PageDesk

PageDesk reads PDF files and lets you mark them up. You can read a document, search it, copy its text, follow its links and jump through its outline. You can add highlights, notes, text boxes and drawings, rotate, move, extract and insert pages, and save the result as a new file. The original file is never changed.

PageDesk opens most PDFs, including ones with embedded fonts, photographs, scanned pages and passwords. The parts that read each kind of content are loaded when a page needs them, so a plain document opens fast and a complicated one takes longer.

## Starting PageDesk

Double-click **PAGEDESK.O88**, or double-click any `.PDF` file and PageDesk opens it. The disk with PageDesk also holds a few sample files: [[XTGUIDE.PDF]] is a twelve-page guide, and [[HELLO.PDF]] is a single page.

The empty window has an **Open...** button and a **Sample guide** button. File > Open... (Ctrl+O) chooses a file. Esc stops a document that is still opening.

PageDesk reads its decoders out of PAGEDESK.O88 while it draws, and its extra font sizes from [[PDFONTX.RES]] beside it. Keep that disk in its drive.

On a 360 KB floppy machine the program fills a disk of its own, and the samples are on a second disk. Keep the PageDesk disk in one drive, put the other disk in the second drive and choose the file with File > Open.... If the PageDesk disk is out when a page needs it, PageDesk says **Insert the PageDesk disk**. Put it back and do the same thing again.

## The window

The window adapts to its size. On VGA, EGA and Hercules it shows a file bar, a mode bar, a status line and two panels: Pages and Outline on the left, Notes & edits on the right. On a shorter window, such as CGA, there is one combined bar and the panels open as pop-ups.

The status line is where PageDesk talks. It shows progress, results such as "Saved X", what is unsaved, and the reason when you press a button that is greyed out.

## Reading

| Key | What it does |
|---|---|
| PgUp, PgDn | Previous and next page |
| Home, End | First and last page |
| Ctrl+G | Go to a page: type the number, then Enter |
| `+`, `-` | Zoom in and out, from 25% to 400% |
| Arrow keys | Scroll |
| Alt+Left, Alt+Right | Back and forward through the pages you jumped between |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match |

The mouse wheel scrolls. The View menu has Actual size, Fit width and Fit page.

The left panel has two tabs. **Pages** shows small pictures of the pages, drawn one at a time while the machine is idle. **Outline** shows the document's bookmarks; click a title to go there. A window too short for panels reaches pages through the Pg button.

A click on a link in the page follows it to its page. A link to a web address shows the address and does nothing else.

Find searches one page at a time and Esc stops it. If a page has not been drawn yet and its fonts cannot be read in the memory left, Find says which page to draw first.

To copy text, drag across it with the Select tool, or press Ctrl+A for the whole page, then Ctrl+C. The selection shows in inverse video.

## Passwords

A document with a password asks for it in a small box. Type it and press Enter. A wrong password says so and asks again. Esc cancels the open.

Some of the newest password protection takes a long time to check on a slow machine. PageDesk then asks first, with an estimate: "This file's encryption takes about N minutes on this machine. Open anyway?" Enter opens it; Esc leaves the file shut.

A document that was saved with a password is saved with the same password protection.

## Modes and tools

Four modes are chosen with F5 to F8 or the mode bar. Keys 1 to 6 choose the tools of the current mode.

| Mode | Key | Tools |
|---|---|---|
| Read | F5 | Select |
| Annotate | F6 | Select, Highlight, Note, Text, Mark, More (Ink, Line, Rectangle, Ellipse, Sign) |
| Edit | F7 | Select, Text, Delete |
| Pages | F8 | Rotate left, Rotate right, Delete, Blank, Extract |

| Tool | What you do |
|---|---|
| Select | Click a mark to select it, drag to move it, drag a corner to resize it. Double-click a text box to type in it. On page text, drag to select. |
| Highlight | Select text first, then press Highlight. |
| Note | Click where the note goes, type, and press Done or Enter. Esc throws it away. A note holds 1,024 characters. |
| Text | Drag a frame, or click for a default box, then type. Esc finishes. |
| Mark | Drag to shade an area in light red. |
| Ink | Draw with the button down. |
| Line | Drag from the tail to the head. The line ends in an arrow. |
| Rectangle, Ellipse | Drag. |
| Sign | Draw one stroke. This is a drawn picture, not a certificate signature. |

A selected text box can change family (Helvetica, Times, Courier), size, bold, italic, colour and alignment.

On CGA and Hercules a highlight is drawn as an underline with a dotted frame so the letters stay readable. The saved file holds an ordinary highlight.

Ctrl+Z undoes and Ctrl+Y redoes, up to sixty-four steps. A `*` in the title means unsaved changes.

## The Notes & edits panel

The panel lists the marks on the page, on all pages, or every change. Click a card to select the mark and go to its page. A card's button marks a note Resolved or Reopened. Notes that were already in the file are listed too; their text can be read but not changed.

## Page operations

| Command | Where | What it does |
|---|---|---|
| Rotate page left, right | Document menu | Turns the page |
| Delete page | File menu | Asks first. The last page cannot be deleted. |
| Insert blank page | File menu | After the current page |
| Duplicate page | File menu | Copies the page and its marks |
| Move page earlier, later, to... | Edit menu | Reorders pages |
| Extract pages... | File menu | Type pages such as `1,3-5`, then choose a name. The result is a new file; the source stays open. |
| Insert from PDF... | File menu | Choose a file, then type the pages, such as `2-4`. They go after the current page. |
| New | File menu | Starts a blank document in Letter, A4 or a wide size |

Inserted pages show as blank paper until you save; the saved file holds the real pages. A file with forms or scripts cannot be inserted. Page operations handle up to 240 pages.

## Saving

PageDesk saves under a new name with File > Save As... (Ctrl+S). The file you opened is not changed. If you pick the same name, PageDesk asks whether to replace it: Enter replaces, Esc keeps it.

After a save the new file is the open document. If the disk is full or write-protected, PageDesk says so and writes nothing. Names must be in the 8.3 form: up to eight letters, a dot and three letters.

Earlier content stays inside the saved file. Deleting a page or covering text with a mark does not remove what was underneath.

When you close the window with unsaved changes, press S to save, D to discard or Esc to keep working. If the machine stops before you save, PageDesk offers to recover your marks the next time you open the same file.

## The menus

| Menu | Items |
|---|---|
| File | Open... Ctrl+O; Open sample guide; New Ctrl+N; Save As... Ctrl+S; Close document; Extract pages...; Insert blank page; Duplicate page; Delete page; Insert from PDF... |
| Edit | Undo Ctrl+Z; Redo Ctrl+Y; Delete Del; Find... Ctrl+F; Find next F3; Find previous Shift+F3; Copy Ctrl+C; Select all Ctrl+A; Move page earlier; Move page later; Move page to... |
| View | Zoom in; Zoom out; Actual size; Fit width; Fit page; Show or hide the Pages panel; Show or hide the Notes panel; Preferences... |
| Document | Read, Annotate, Edit and Pages modes F5 to F8; Previous page; Next page; Go to page... Ctrl+G; Back; Forward; Rotate page left; Rotate page right |
| Help | Keyboard...; The XT PDF profile...; Document info... |

Help > Document info names the file, its page count, whether the page is fully drawn and how much memory is free.

## Preferences

View > Preferences... has six rows: Default view, panels for large and medium windows, how marks look on black-and-white screens, Flate cache and Module cache. Up and Down choose a row, Left and Right change it, Esc closes and saves. The settings are kept in `SYSTEM/APPDATA/PAGEDESK.CFG`.

Flate cache set to Off keeps memory free on a small machine, at the cost of refusing documents whose pages are stored compressed in that way. Module cache set to Auto keeps the decoders in extended memory when the machine has some, so the second use is a copy and not a disk read.

## Good to know

- Photographs and scanned pages take time on an 8088: tens of seconds for a photograph, about a minute for a JPEG 2000 page. Esc cancels a page that is drawing.
- A font inside a document is drawn from its own shapes. When memory is short, the page is drawn in a plain substitute face and the status line says so.
- A page that cannot be fully drawn is drawn as far as possible. The status line says "Incomplete rendering" and names the first thing it could not do.
- Forms are shown as printed. They cannot be filled in.
- A signed document opens read-only. Reading, searching and copying still work.
- JavaScript, launch actions and web links are never run.
- Documents over 1,024 pages, or with a decoded stream over 1 MB, are refused with a message that names the limit.
- On a machine with little memory, PageDesk may say **Out of memory. Close a window, retry.**

## Files

PageDesk reads `.PDF` files and writes new `.PDF` files. [[PDFONTS.TXT]] is the licence of its fonts. [[PDFONTX.RES]] holds extra text sizes and is optional.
