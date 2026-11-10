# PageDesk user guide

PageDesk is a native PDF viewer and editor for os8088 (SPEC.md 128,
`docs/PAGEDESK-SPEC.md`, `docs/plans/PAGEDESK-PLAN.md`). It runs on an IBM
PC/XT (8088, 4.77 MHz, 640 KiB, no FPU, no extended memory) in VGA, CGA and
Hercules, offline. This is the reader's guide. The contract is
`PAGEDESK-SPEC.md`; why it is built the way it is lives in
`docs/HOW-PAGEDESK-WAS-BUILT.md`. The on-disk `README.TXT` is the short form
of this file and Help inside the program is the shortest.

## How to read the claims in this guide

Every statement says only what a report in the build log shows working.
Three words mark the rest:

| word | meaning |
|---|---|
| **seen** | observed on the glass (QEMU) or by an independent PDF tool (qpdf, MuPDF's `mutool`, Poppler's `pdftoppm`) |
| **host-checked** | proved by a host test harness against the same C and assembly, not run on an emulated machine |
| **not verified** | built, or written down in the spec, and not shown working; or refused outright |

What was seen was seen on **QEMU** (kern_big, 640 KB), on VGA and CGA, and for
Hercules on QEMU with the test harness's fake framebuffer (`HERCSEG=0x7000`),
which verifiers called unusable for PageDesk late in wave 3. **No agent in the
build ran PageDesk on a real XT, on 86Box, or on MartyPC.** Speed on a 4.77 MHz
8088 is therefore estimated from instruction counts, not measured, except where
the final pass (PD-29, `docs/HOW-PAGEDESK-WAS-BUILT.md` section 9) has added
a number. See section 17 for the list.

## 1. Starting PageDesk

PageDesk ships as `PAGEDESK.O88` with its fonts inside it, and as its own
disk in four geometries from `make pagedeskdisk` (`build/pagedesk.img` at
1.44 MB, `pagedesk720.img`, `pagedesk120.img` at 1.2 MB, `pagedesk360.img`).
The disk's root holds the program, `README.TXT`, `PDFONTS.TXT` (the font
licence), the eight sample PDFs and, on every geometry but 360 KB,
`PDFONTX.RES` (the other font sizes, optional). It is also in the everything
set (`make allapps`) and on the live media.

To open a document:

- double-click a `.PDF` (the package registers the association);
- start `PAGEDESK.O88`, then File > Open... or Ctrl+O. The empty window also has
  an **Open...** button and a **Sample guide** button, which opens
  `XTGUIDE.PDF` from beside the program;
- File > Open sample guide.

Opening is a resumable job (a few slices, then the first page is drawn). Esc
cancels it. A document stays open until you close it, open another, or close
the window. PageDesk reads the file where it lies and **never opens it for
writing**.

## 2. The screen

The layout is chosen from the window's content box, not from the video card.

| layout | when | what you get |
|---|---|---|
| full | content at least 600 wide and 260 high | file bar, mode bar, status line, a Pages/Outline panel on the left and a Notes & edits panel on the right, each can be hidden |
| mid | at least 240 high | the same bars; panels open as pop-ups |
| compact | shorter (CGA at 200 lines: the content box is 136 high) | one combined bar and the status line; pages and notes are pop-ups; the Open button is in the File menu |

Measured content boxes (shell report): VGA 640x416 (full), Hercules 720x284
and EGA 286 high (full), CGA 640x136 (compact). Hercules at 350 lines was not
booted; the layout code is host-checked from 320x100 to 720x460, about 15
million boxes (`pdlay`).

The **status line** is where PageDesk talks: progress ("Drawing page 4 - Esc
cancels"), outcomes ("Saved X"), and, whenever a control is greyed, the reason
if you press it anyway ("Open a document first.", "This is the last page.").
When a page has been drawn it says what is unsaved ("3 changes · Unsaved",
"No changes"); its middle says the page and its right end the two keys you
most need. Every button is either live or greyed with a stated reason.

On the full and mid layouts the bars are drawn with pictures beside their
captions, in the compact typewriter face PageDesk carries for its pages
(Liberation Mono), with the active mode and tool in teal (SPEC 20.21). The
compact layout keeps the system's 8x8 face and captions only: CGA's bar is
12 lines tall.

### Adapters

| adapter | mode | what is different |
|---|---|---|
| VGA | 640x480, 16 colours | full layout; the 16-colour desktop palette; light greys between two palette greys are dithered |
| CGA | 640x200, 2 colours | compact layout; marks are mono (below); selection and Find are XOR inversion |
| Hercules | 720x348, 2 colours | full layout, both panels, as in the mockup; mono marks |

On a 1-bit adapter a **Highlight is never a solid fill**: it is drawn as an
underline plus a dotted frame so the letters stay readable. Preferences >
Mono marks chooses Line+frame, Line, Frame or Hatch. This is only how the
mark is drawn on the screen; the saved PDF always holds a normal highlight.

## 3. Modes and tools

Four modes, F5 to F8 or the buttons (Document menu):

| mode | key | tools (keys 1-6 pick them in this order) |
|---|---|---|
| Read | F5 | Select |
| Annotate | F6 | Select, Highlight, Note, Text, Mark, More (Ink, Line, Rectangle, Ellipse, Sign) - on the full and mid bars these read Select, Highlight, Note, Text, Draw (Ink), Shape and Sign, and Mark, Line, Rectangle and Ellipse are in Shape's list; the keys keep this order, so 5 is Mark and 6 opens the list |
| Edit | F7 | Select, Text, Delete |
| Pages | F8 | Rotate L, Rotate R, Delete, Blank, Extract |

On compact layouts Mark folds into More. Annotate and Edit tools are greyed on
a **signed** document ("This document is signed: it opens read-only.").

| tool | what you do | what you get |
|---|---|---|
| Select | click a mark (a shape by its border, a note or text box by its box, ink and highlight by their bounding box); drag it; drag a corner handle to resize; double-click or Enter on a text box to type. On the page text: drag to select. | moving is exact and stays on the page |
| Highlight | select text first, then press Highlight. Up to 12 line boxes per mark. | a yellow `/Highlight` annotation. With nothing selected: "Select text first." |
| Note | click where the note goes | a popover opens; the note exists only once it has text and you press Done/Enter. 1,024 characters. Esc discards. |
| Text | drag a frame, or click for a 144 x 36 pt box; click an existing box to edit | a `/FreeText` box (blue Helvetica 12 by default), then you type; Esc finishes. A click in the margin: "Click on the page." |
| Mark | drag | a light-red area mark (a `/Square`) |
| Ink | draw with the button down | a red ink stroke, one mark per stroke |
| Line | drag tail to head | a red line **with an arrow at the end** (there is no plain-line gesture) |
| Rectangle, Ellipse | drag | red outline shapes (4 pt minimum) |
| Sign | draw one stroke | a black drawn signature, labelled "Drawn signature. This is not a certificate signature." One stroke only |

A tool gesture holds the screen until the button comes up. Esc does not cancel
a gesture with the button down (the OS delivers no key event there);
**Undo takes a finished gesture back in one step**. This is documented in
SPEC 20.19.5 and is deliberate.

Everything is placed in page space (1/16 point), so zoom, scroll and rotation
never change a stored mark.

## 4. Reading

### Navigation and zoom

- PgUp / PgDn, the `<` and `>` buttons: previous and next page. Home and End:
  first and last. (A pull-down holds 11 items, so First/Last are keys only.)
- Ctrl+G, or the page field `n/N`: type digits, Enter to go, Esc to cancel.
  On compact the `Pg` button opens a page chooser.
- `+`, `-`, or the zoom field: 25% to 400% in steps of 25, 33, 50, 67, 75,
  100, 125, 150, 200, 300 and 400. The zoom pop-up offers Fit width,
  Fit page, 50, 75, 100, 150, 200 and 400%. View > Actual size is 100%. The
  default for a new document is Fit width (Preferences).
- Arrow keys scroll by 16 pixels; the mouse wheel scrolls; over the Pages
  panel the wheel scrolls its list. The scroll bar's thumb drags; on an 8088
  the view moves at the release, on a 286 or better every second tick.
- Back / Forward (Alt+Left, Alt+Right; Document menu) retrace the last 8
  pages a **jump** left: a followed link, an outline entry, a Find hit, Go to,
  Home/End, a panel click. Next/previous page are not history.

### Pages and Outline panels

The left panel has two tabs. **Pages** shows 56x72 one-bit thumbnails, drawn
one at a time while the machine is idle (the current page first); until then a
cell shows its number. **Outline** reads the document's bookmarks a level at a
time (up to 97 entries); click a twisty to expand, click a title to go to it.
There is no sidebar on compact layouts, so Outline is not reachable on CGA.

### Find

Ctrl+F opens the Find pop-up: type, Enter. The search is a job: it reads one
page's text at a time, case-insensitive over Latin-1, needle up to 63
characters, and Esc stops it. The hit is selected, scrolled into view and the
status line says which page. F3 finds the next hit (wrapping past the last
page), Shift+F3 the previous. **Seen** on XTGUIDE: `care` goes to page 2, F3
to page 5, Shift+F3 back to page 2 (VGA); `small` on CGA.

A search of a long document is slow on an XT (every page is read and
interpreted for its text). Not timed on a 4.77 MHz machine.

### Select and Copy

Drag with the Select tool: a rubber band follows the pointer, and on release
the nearest glyphs become a selection, shown by **XOR inversion** that is
never saved. A double-click grows the pick to the word. Ctrl+A selects all of
the page's text. Ctrl+C (Edit > Copy) puts the text on the clipboard as
Windows-1252, a line break between lines and a space between gaps. Over 32 KiB
of text it is refused whole ("The selection is over 32 KB of text: it was not
copied."). **Seen** ("Copied 155 characters." on VGA, 39 on CGA); the
clipboard's bytes themselves were not read back.

### Links

A click on a link goes to its page (it is history). A `/URI` link shows its
address on the status line and **PageDesk never opens it**; Launch, JavaScript
and Named actions are named and refused. Links to named destinations work for
the older `/Dests` dictionary, not for the name tree of newer files.

## 5. Annotating and reviewing

### Notes & edits panel

The right panel lists the page's marks. Three filters: **This page**, **All
pages**, **Changes** (every record with its change word: Added, Edited, Moved,
Deleted, Resolved, Reopened). Click a card to select it and jump to its page;
click a selected note again to open it, a selected text box again to open its
properties. A card's button toggles **Resolve / Reopen**, which is saved as a
PDF review reply. Source annotations that came with the file are listed (the
page's first 32, 8 shown) and jumped to; their text is read-only. On compact
layouts the same list is a pop-up of one line per row.

Resolve and Reopen were seen on a look-only demo store and on a hand-made file
with source notes; `MARKUP.PDF` has no source annotations, so no sample
shows it.

### Text properties

For a selected text box: family (Helvetica, Times, Courier), size
(8 to 24), Bold, Italic, colour (black, blue, red, green, magenta, brown,
grey), alignment L/C/R, Delete, Done. Keys in the inspector: B I + - F C L M R,
Del, Esc/Enter for Done. Every change is one Undo step.

### Undo and Redo

Ctrl+Z, Ctrl+Y. Sixty-four steps; a drag of fifty updates is one step; typing
into a box coalesces. The title bar shows `*` when the document differs from
the last save, and undoing back to the save point makes it clean again. Page
operations are steps too. When the history is full the oldest step is dropped
and the status line says so once.

## 6. Page operations

Pages mode (F8) or the File, Edit and Document menus. The page acted on is the
current page, or the one you last clicked in the Pages panel.

| command | where | notes |
|---|---|---|
| Rotate page left, right | Document, and the Pages-mode buttons | adds to the page's own rotation |
| Delete page | File | asks first: "Delete page N? Earlier bytes stay in the file. Enter confirms". The last page cannot be deleted. |
| Duplicate page | File | a new page sharing content, with copies of its annotations |
| Insert blank page | File | after the current page |
| Move page earlier, later, to... | Edit | "Move page to position:" prompt |
| Extract pages... | File | "Extract pages (e.g. 1,3-5):" then a file dialog; writes a **new** document and leaves the source open |
| Insert from PDF... | File | see below |
| New | File, Ctrl+N | "New page: 1 Letter, 2 A4, 3 Letter wide, 4 A4 wide", then a file dialog; writes `UNTITLED.PDF` by default and opens it |

Page operations handle at most 240 pages. They are Undo steps; undo is **seen**
for Rotate. The operations take effect in the saved file only at **Save As**
(the original is never touched).

**Extract** copies the chosen pages and everything they reference into a fresh
PDF 1.5 file (a graph copy; a link to a page not extracted becomes empty).

**Insert from PDF...** (File row 9) chooses a file but does not open it, then
asks `Insert NAME pages (3, 2-4):`. Enter inserts the range after the current
page as **one** step (one Undo takes it back). Limits, all stated:

- the view cannot draw a page it has not read: an inserted page is **paper**
  (a blank sheet) until you Save As, and the saved file opens with the real
  pages;
- the page count of the other file is not known when you type the range: a range
  past its end is refused **at save** ("no such page."), and nothing is written;
- the foreign file's outlines, names and forms are not carried; a file with
  forms or scripts is refused ("forms or scripts."); links to pages not
  inserted become empty; copied `/NM` ids may repeat; the output's version is
  not raised for the foreign file's features;
- one range per command, the same foreign page twice is refused ("Already
  inserted."), at most three different files per save ("3 files at most.");
- Duplicate and Extract refuse a list containing an inserted page ("Save
  first.");
- marks placed on an inserted page are refused rather than lost.

**Seen** (VGA): inserting pages 1-2 of `MARKUP.PDF` into `PAGES.PDF`, Save As,
and reading the result back: `qpdf --check` clean, 10 pages, Poppler pixels of
pages 2 and 3 equal MARKUP's pages 1 and 2. On the host the round (12 pages,
fonts and the form XObject copied once per file) passes `pdwrt-real`.

A deleted or duplicated page's marks follow the page map, with two known
defects (section 15): a duplicate still draws the original's marks, and
thumbnails are chosen by view position.

## 7. Saving

PageDesk **only saves under a new name** (Save As, Ctrl+S). The source is never
opened for writing; saving with the source's own name writes a temporary file
and then asks.

1. The source's bytes are copied unchanged to `PDSAVE.$$$` in the target
   folder.
2. New and changed objects are appended, then a new cross-reference section
   for them only, a trailer pointing back to the source's (`/Prev`), and the
   end marker.
3. The result is **read back through the PDF engine** (the trailer and the
   first, middle and last object written) before anything is renamed.
4. If the target name exists, the status line asks and **Enter replaces it, Esc
   keeps it**. There is no atomic replace on this file system, so PageDesk
   deletes the old file and renames the new one; a failure before the delete
   leaves the target untouched.

With no edits, a Save As is a byte copy of the source.

Consequences, which PageDesk says and the guide repeats:

- **Earlier bytes stay in the file.** An updated file keeps the older versions
  of everything. That is not redaction. Delete page and covering text with a
  rectangle do **not** remove what was underneath.
- **Sign is a drawn mark**, not a certificate signature; PageDesk makes no
  cryptographic claims.
- After a successful save **the new file is the open document**, on the page
  you were on, and the title's `*` clears. After a failed or cancelled save
  or an Extract the source is reopened.
- Save As may write into the folder the document came from or into another
  folder (D14; seen: a Save As into `SYSTEM\` kept the source's bytes as a
  prefix). Another volume is host-checked only.
- A source that changed on disk since you opened it is refused: "The file
  changed on disk since it was opened." The check uses the file's size and its
  first and last kilobyte.
- Failures leave nothing behind: disk full ("The disk is full."), write
  protect ("The disk is write-protected."), a short write or a failed
  read-back delete the temporary file and the document stays dirty.

**Independently checked**: the saved edits of every wave were opened with
`qpdf --check` (clean), `mutool` and `pdftoppm` (rendered without
diagnostics), and `qpdf --json` assertions on annotations
(subtypes, `/QuadPoints`, `/NM`, `/IRT` replies, `/InkList`, `/LE`) in
`pdw_verify.py`. An unedited save's prefix equals the source (SHA-256 checked
by the harness) and a second save over a saved file preserves both earlier
revisions.

### Closing

Closing the **window** with unsaved changes asks on the status line: `S`
saves (opens Save As), `D` discards, Esc keeps. **New, Open and Close
document do not ask**: the marks are gone from memory but the recovery journal
(next section) still holds them until you open the same source again.

## 8. Recovery

Two things protect your work.

**The journal.** A moment (half a second of quiet) after each change,
PageDesk writes your marks to `SYSTEM/APPDATA/PAGEDESK.JRN` on the boot
volume. It is rewritten whole each time, so a crash in the instant of the
rewrite can leave none. After a crash or a power-off, open the same PDF again
and the status line asks `Recover N marks? Enter yes, Esc no.` Enter puts them
back as one Undo step and the document is dirty again; Esc deletes the journal.
The match is the file's fingerprint (size, first and last kilobyte), not its
name. If the source changed, the journal is ignored. It does **not** hold page
operations or the last half second. On a read-only volume it is off ("Recovery
off."). A store that outgrows 8 KiB of journal turns it off, said once. The
check happens at the first document open of a session, never at launch.
**Seen** (VGA): Mark drawn, QEMU killed, reboot, open, the offer, Enter, the
mark back.

**The interrupted save.** If a save died after deleting the old file but
before renaming the new one, the complete new file is `PDSAVE.$$$`. At the next
session's first open, the status line reads `Finish save of NAME? Enter yes,
Esc no.` and Enter renames it. PageDesk writes a note (`PDSAVE.TGT`) only at the
moment of the delete, after the temporary file has been verified, and starting
another save drops an unanswered offer, so a partial file is never offered.
The offer is **host-checked only**; it was not seen on the glass.

## 9. Preferences

View > Preferences... opens a pop-up on every layout (it fits compact's 136
pixels). Rows:

| row | values |
|---|---|
| Default view | Fit width, Fit page, 100% (applies at the next open) |
| Full panels | None, Pages, Notes, Both |
| Mid panels | None, Pages, Notes, Both |
| Mono marks | Line+frame, Line, Frame, Hatch |
| Flate cache | Auto, Off (applies at the next open) |

Up/Down or Tab select a row, Left/Right/Enter/Space change it, a click on a
row advances it, Esc or a click outside closes and saves. Settings live in ten
bytes at `SYSTEM/APPDATA/PAGEDESK.CFG` on the volume PageDesk was launched from
and are read at the first document open or the first time the dialog paints,
so an empty window shows the defaults. A damaged file is ignored whole. When
the disk cannot be written (no APPDATA folder, write-protected, full) the choices
hold for the session and the status line says `Not saved: <reason>. Session
only.` **Seen** (VGA and CGA, keyboard; a click on a row was not clicked).
The Flate Off path was not exercised on the glass.

## 10. Forms

PageDesk shows a form as printed and **does not fill it**. Opening a document
with an AcroForm says `Form fields are shown as printed; filling is not
supported.` and Help > Document info has a `Forms:` line. Pages still render,
annotate and save; no field value is read or written. Filling (PD-26) was not
selected for this build. A form with scripts is refused by Insert.

## 11. Flate and the low-memory setting

Many PDFs compress their streams with Flate. Flate needs a 32 KiB history
window; a slot is 43 KiB. The XT-required profile does **not** include it
(SPEC 1.3): it is an enhanced capability gated by **free memory, not by the
CPU**. On open, PageDesk carves one Flate slot out of its document claim only
if the free heap afterwards stays at least 32 KiB; otherwise, or with
Preferences > Flate cache = Off, a Flate stream is refused ("No Flate cache
(low RAM, or set Off)."). The Off setting lets any machine show the XT
behaviour.

**Seen**: `FLATE.PDF` opens and draws on QEMU VGA with 640 KB (the lead's
look after the memory carve). PNG-predicted images were host-checked, not
looked at. **Host-checked**: 938 engine runs, fuzzing (24,000 hostile
streams, no crash), and the real module on QEMU against Python's inflater (60
streams, asm and C twin identical). Speed on an XT is an estimate from
instruction counts (about 63 instructions per output byte of text, 190 for a
predicted image, plus 188,000 instructions to start a dynamic block): a few
KB a second, so a Flate page is slow. Not measured on a 4.77 MHz machine.

## 12. Complex PDFs: the host preparation tool

`tools/os88pdprep.py IN.pdf OUT.pdf` (PD-22) runs on a PC with `qpdf` and
`mutool` installed. It rewrites a file into the XT profile:

- object and cross-reference streams become classic tables; revisions and
  linearisation are flattened; filters are decoded (Flate is kept as Flate
  only with `--flate`);
- a per-page classifier finds what the XT cannot draw (shadings, patterns,
  transparency, ICC and Indexed colour, bad images, CID and Type 3 fonts, huge
  boxes); `--raster none|auto|all|list` replaces such pages with 1-bit
  dithered or 8-bit grey strips, plus an invisible text layer where MuPDF
  found text, so Find and Copy still work;
- an `/Info` stamp records the original's size and SHA-256; encrypted input is
  refused unless you give `--password`;
- a text report says what changed; `--deterministic-id` makes the output
  byte-reproducible. Exit codes: 0 ok, 1 refused, 2 usage, 3 tool missing, 4
  verification failed.

It verifies its output with `qpdf --check`, `tools/os88pdfcheck.py` and the
engine itself. `tests/unit/t_pdprep.py` (61 checks, row `pdprep`) tests it,
and **skips, never passes, without qpdf**. **Not done**: PageDesk does not yet
read the stamp (it does not say "Prepared copy"); rotated text gets no text
layer; there is no merge back onto the original.

## 13. Memory

PageDesk's segments (image plus bss, the build's final figures):

| segment | size | cap |
|---|---|---|
| primary (the program) | 59,235 | 59,392 |
| overlay (lazy: dialogs, menus, tools) | 48,318 | 49,152 |
| PDENG (reads the PDF) | 47,074 | 47,104 |
| PDREND (draws) | 60,304 | 61,440 |
| PDWRT (writes; lazy, freed after each save) | 47,724 | 49,152 |

Every segment is full: **no feature can be added without removing one**.

Per-document claims (SPEC 16.2, 20.6.2): the kernel allows a program only 8
claims on kern_big, so the primary carves its rows from two large claims. On
VGA and EGA the second claim is 63 KiB (60 KiB on CGA and Hercules), holding
the display list and its workspace (29 KiB), the band, the edit store (16 KiB),
the undo log (4 KiB), page map, thumbnails, scratch and the 2 KiB private
module stack. A save adds the writer's buffers (18 KiB, up to 40 KiB for an
Extract). Flate adds 43 KiB when it is used.

**Measured** (QEMU, 640 KB, kern_big, wave 1 and 2): Help > Document info
showed 437 KB free with HELLO.PDF open on VGA after the display list grew to 29
KiB (446-447 KB before it), 451 KB on CGA, 450 KB on Hercules. The figure was
equal before and after open/close/reopen/Save As/quit/relaunch, so no claim
leaked on those paths. These are readings taken before the edit store, undo,
thumbnails and the writer joined; the final pass (PD-29) re-measures.

**Stack.** The UI task's stack is 512 bytes; the modules run on a private 2
KiB stack (SPEC 20.15). Seen on VGA with the real renderer on XTGUIDE pages 1,
3 and 12: deepest module stack 448 of 2,036 bytes, UI task high-water 258 of
512 (CGA: 386 and 252). A static gate (`tools/os88pdstk.py`, fast row `pdstk`)
holds every callback chain at or under 384 bytes (366 today on the deepest).
An overrun of the module stack closes the document with `Out of module stack:
the document was closed.` rather than corrupting memory. While a module verb
runs on the private stack the UI task is not pre-empted; the longest such
window has **not been measured**.

## 14. Menus and keys

| menu | items |
|---|---|
| File | Open... Ctrl+O; Open sample guide; New Ctrl+N; Save As... Ctrl+S; Close document; Extract pages...; Insert blank page; Duplicate page; Delete page; Insert from PDF... |
| Edit | Undo Ctrl+Z; Redo Ctrl+Y; Delete Del; Find... Ctrl+F; Find next F3; Find previous Shift+F3; Copy Ctrl+C; Select all Ctrl+A; Move page earlier; Move page later; Move page to... |
| View | Zoom in +; Zoom out -; Actual size; Fit width; Fit page; Show/Hide Pages panel; Show/Hide Notes panel; Preferences... |
| Document | Read mode F5; Annotate mode F6; Edit mode F7; Pages mode F8; Previous page PgUp; Next page PgDn; Go to page... Ctrl+G; Back Alt+Left; Forward Alt+Right; Rotate page left; Rotate page right |
| Help | Keyboard...; The XT PDF profile...; Document info... |

Other keys: `1`-`6` pick the mode's tools; Home / End first and last page;
arrows scroll; Tab / Shift+Tab move the keyboard focus (Enter or Space presses
it); Del deletes the selected mark; Ctrl+minus also zooms out. Esc, in order, the first
that applies: undoes a held tool gesture, cancels a running job, closes a
pop-up, leaves the page field, finishes typing, deselects the mark, drops the
keyboard focus, clears the text selection, then returns to the Select tool. A text field (the page field, Find,
a note, a text box) owns every key except Esc and Enter.

Help > Document info shows: file, pages, PDF version and revisions, access
(annotations allowed or read-only), the forms line, whether the page is fully
drawn or incomplete, the font index size, the layout and free memory. It is
also how a leak shows itself on a machine without a debugger.

## 15. The PDF profile and the capability matrix

The XT profile (SPEC 1.3) reads classic cross-reference tables with `/Prev`
chains (up to 8 revisions), indirect lengths, page trees with inherited boxes
and resources (up to 1,024 pages), content as one stream or an array,
ASCIIHex and RunLength filters in pipelines of up to four, the graphics and
text operators of 1.3, the base-14 Latin fonts by name with `/Widths` and
WinAnsi, MacRoman and `/Differences` encodings, Form XObjects four deep,
1- and 8-bit grey and 8-bit RGB images (converted to grey), image masks, and
inline images (1, 2, 4 and 8 bits, grey and RGB, with the exact length). It draws
dash patterns of up to four elements. It displays any annotation with a usable
`/AP /N` appearance.

| capability | native XT | enhanced (RAM, any CPU) | host-prepared | not implemented |
|---|---|---|---|---|
| classic xref, incremental chains | yes | | | |
| xref/object streams | refused (`PDE_XREFSTM`) | | converted by `os88pdprep.py` | |
| ASCIIHex, RunLength | yes | | | |
| Flate (+ PNG/TIFF predictors) | refused (`PDE_FLATE`) | decoded with a 43 KiB slot | normalised | colours above 4, TIFF below 8 bits, inline-image `/DP` |
| LZW, ASCII85, JPEG, CCITT, JBIG2, JPX | refused (`PDE_FILTER`) | refused | rasterised | |
| base-14 Latin text | yes (bitmap faces, PDF widths) | | | |
| other fonts (TrueType, CFF, CID, Type 3, Symbol, ZapfDingbats) | drawn with a substitute and marked **Incomplete rendering** | same | rasterised | |
| shading, patterns, transparency, soft masks, ICC/Indexed colour, JPEG | page drawn **Incomplete**, naming the feature | same | rasterised | |
| annotations created: Text, Highlight, Square, Circle, Line, Ink, FreeText | yes | | | |
| encryption | refused (`PDE_ENCRYPT`) | refused | decrypted with `--password` | |
| signed documents | open read-only | | | no verification, no signing |
| AcroForm | shown as printed | | | **filling** (PD-26) |
| printing | | | | no print service on os8088 |
| JavaScript, launch, remote links | never run | | | |
| protected-mode edition (PD-27) | | | | decision record only: `docs/plans/PAGEDESK-PM-ADR.md` |
| extended-memory caches (PD-25 part) | | | | not built; a bigger cache bought almost nothing in the measurement |

**Incomplete rendering.** A page PageDesk cannot fully interpret is drawn as
far as it can be and the status line names the first thing it could not do
("Incomplete rendering - ..."). Help > Document info says `incomplete
rendering`. PageDesk does **not** currently disable editing or saving on an
incomplete page (see section 16); do not treat an incomplete page as proof of
what the file looks like elsewhere.

**Text.** Text is drawn in bundled bitmap faces, Liberation (Helvetica/Arial,
Times, Courier equivalents) under the SIL OFL, 8, 10 and 12 pixels in the
package and 6, 7, 9, 14, 16, 20 and 24 in the optional `PDFONTX.RES`. Layout
uses the PDF fonts' own widths (Adobe AFM data), so line breaks match the
original; the glyph shapes are substitutes. Without `PDFONTX.RES`, sizes
above 12 pixel-double the nearest strike and sizes below 8 compress the 8 px
strike; no glyph is ever missing and no message is shown. In outline titles, characters outside Latin-1 come out as `?`.

## 16. Limits and refusals

Every message below is in `apps/pagedesk/pduerr.c`. "Where" says when you see
it.

| code | message | what it means and what to do |
|---|---|---|
| `PDE_SYNTAX` | The file is damaged: it does not parse as PDF. | no `%PDF-` header at byte 0, a bad stream length, a document with no pages, or a packed copy of the file (a `.PDF` that the OS has compressed on disk and that PageDesk would read as its wrapper). Re-copy the file from the original. |
| `PDE_XREF` | The file's cross-reference table is damaged. | a damaged or looping xref chain, no `/Root`, an offset the file does not hold. PageDesk never repairs a file; repair it first with `qpdf` on a PC. |
| `PDE_TRUNC` | The file is truncated. | no `startxref`, or data past the end. Copy the file again. |
| `PDE_CYCLE` | The file's page or resource tree loops. | a page-tree node reached twice or a `/Parent` loop. |
| `PDE_DEPTH` | The file nests objects deeper than PageDesk reads. | more than 32 levels. |
| `PDE_FLATE` | No Flate cache (low RAM, or set Off). | free memory could not hold the 43 KiB slot with 32 KiB to spare, or Preferences has Flate Off. Close windows, set Flate cache to Auto, reopen; or prepare the file with `os88pdprep.py` on a PC. |
| `PDE_FILTER` | It uses a stream filter PageDesk cannot decode. | LZW, ASCII85, JPEG, CCITT, JBIG2, JPX, a crypt filter, or a fifth stage. Prepare on a PC. |
| `PDE_FONT` | It uses a font PageDesk cannot draw. | reserved for a font with nothing to substitute; normal unsupported fonts draw with a substitute and say Incomplete. |
| `PDE_GRAPHICS` | It uses graphics outside the XT profile. | shadings, patterns and similar; normally shown as Incomplete rendering. Prepare with `--raster`. |
| `PDE_ENCRYPT` | The file is encrypted; PageDesk cannot open it. | decrypt on a PC with `os88pdprep.py --password`. |
| `PDE_XREFSTM` | It uses cross-reference streams (PDF 1.5+ packing). | the commonest modern layout. Prepare with `os88pdprep.py`. |
| `PDE_SIGNED` | The file is signed: it opens read-only. | a signature or `/Perms`: you can read it, search it and copy from it; annotation tools are greyed. |
| `PDE_RANGE` | A page or number is outside PageDesk's range. | a page box beyond +/-2,047 points, a zero-area box, a rotation that is not a multiple of 90, or in Insert, a page past the other file's end ("no such page."). |
| `PDE_LIMIT` | The file exceeds one of PageDesk's limits. | more than 1,024 pages, more than 8 revisions, an object number above 65,535, a file of 2 GiB, more than 256 xref subsections across the chain, one decoded stream over 1 MiB, or an Insert of a file with forms or scripts ("forms or scripts."). |
| `PDE_NOMEM` | Not enough memory. Close a window and try again. | a claim was refused (the open says `Not enough memory: PageDesk needs N KB.`). Close other windows. |
| `PDE_STACK` | Out of module stack: the document was closed. | the safety check on the private stack fired; reopen. Report it with the file if you can. |
| `PDE_IO` | The disk could not be read. | also "the file is not in this folder". Check the disk. |
| `PDE_DISKFULL` | The disk is full. | choose another disk; the temporary file was removed. |
| `PDE_WPROT` | The disk is write-protected. | choose another disk. |
| `PDE_EXISTS` | A file of that name is already there. | the confirm: Enter replaces, Esc keeps. |
| `PDE_CHANGED` | The file changed on disk since it was opened. | another program or disk swap changed the source; reopen it. |
| `PDE_NOTFOUND` | Not found, or not in this build yet. | a missing item (no outline, no match, a name not in the folder). |
| `PDE_CANCEL` | Cancelled. | you pressed Esc. |
| `PDE_BUSY` | Still working. | not a failure; the job has more to do. |

Other messages you may see:

- `Could not open NAME`, `Could not save NAME: <reason>`, `Could not draw the
  page: <reason>`.
- `Page operations handle up to 240 pages.`, `The edit store is full: save,
  then continue.` (128 marks; the same text heap limit is 1,024 characters a
  note), `Stroke limit: the stroke is kept.` (126 points an ink stroke),
  `Too small: 4 points at least.`, `Too large to undo: the history was
  cleared.`
- `Unsaved: S saves, D discards, Esc keeps`, `Recover N marks? Enter yes, Esc
  no.`, `Finish save of NAME? Enter yes, Esc no.`, `Not recovered.`, `Recovery
  off.`, `Not saved: <reason>. Session only.`
- `Busy - Esc cancels.`, `Wait for the current job, or press Esc.`, `Still
  applying the last page change.`
- `Save As writes a new file: choose another name.`: raised when a Save As
  names the file you have open and you decline to replace.

### Known issues

These are specific and testable; none loses a saved file.

1. **An Incomplete page is not write-protected, by design.** No PageDesk
   operation rewrites page content: marks are drawn over the page, and Extract
   and Insert copy its objects byte for byte. So nothing depends on what the
   renderer skipped (PAGEDESK-SPEC 10.6). Selection, Find and Highlight use the
   text that was interpreted.
2. **New, Open and Close document do not ask Save/Discard** (the window close
   does). The journal covers all but the last half second and page operations.
3. **Forms are not filled** (D24).
4. **A duplicated page still draws the original's marks**, and a reordered or
   inserted map shows the wrong **thumbnail** (thumbnails index by view
   position, not page id).
5. **Inserted pages show as paper until Save As.** The Notes panel on an
   inserted page lists page 1's source annotations.
6. **`/JS` written directly inside an annotation's dictionary** is not caught by
   Insert's refusal (PageDesk never runs it; it is incomplete, not a hazard).
7. **Esc cannot cancel a tool gesture with the button down** (Undo instead).
8. **The recovery offer and a replace are by fingerprint**: a same-size
   change in the middle of a source is not seen (size, first and last KiB).
9. **A second Save As over `PDSAVE.$$$`** in the same folder replaces a
   temporary file left by an earlier failed replace.
10. **kern_mega**: an option-ROM interrupt (a PicoMEM) arriving while PageDesk
    is on its private stack is handed to the ROM with a protected-mode
    selector as a real-mode stack segment. This is a kernel-side exposure that
    WIN16 shares; PageDesk uses the private stack there anyway (the other
    stack overruns everywhere). It was checked in source, not run.
11. **A normal redraw can show the old pixels until the renderer's first
    band arrives** (the double paint was removed; a fallback exists for page
    turns and zooms). Whether this flickers on a real XT is a final-pass
    question.
12. **A file with thousands of xref subsections opens slowly**: the stress
    `SPARSE.PDF` needs about 106 disk calls to open (host report: an estimated
    43 seconds on a floppy), and a bigger cache did not help. Not measured on
    hardware.

## 17. What has and has not been verified

Seen on the glass (QEMU, kern_big, 640 KB): launch by the `.PDF` association;
open, page, zoom, Fit, scroll, Find (F3, Shift+F3), selection, Copy count,
links, Outline, thumbnails; Highlight, Note popover, Text box and properties,
Mark, Ink, Line, Rectangle, Ellipse, Sign; undo and redo; Rotate, Blank,
Delete, Duplicate, Extract, New, Insert, Save As and read back with
`qpdf`/`mutool`/`pdftoppm`; the recovery offer after a killed session;
Preferences on VGA and CGA; Flate on VGA; the page-3 stack overrun fixed. VGA
and CGA were looked at in every wave; Hercules (QEMU with the RAM framebuffer
harness) in waves 1 and 2 for the shell and the renderer.

**Measured in the final pass** (PAGEDESK-PLAN 7.1, MartyPC at 4.77 MHz with
640 KiB):

- **Speed.** A first page takes 38-41 guest seconds, a page turn 24-36 and
  Find across XTGUIDE's 12 pages 87.
- **Adapters.** The whole open, page, zoom, Find, mark, Save As and reopen
  session runs on VGA, CGA and Hercules on MartyPC's real Hercules model.
- **Memory.** 81-97 KiB is free after a session.

**Still not verified**, and left to hardware or later work:

- the no-pre-emption window of the private stack, keystroke latency of
  text-box typing, and the cost of Copy;
- EGA, kern_small
and kern_mega; the 360 KB, 720 KB and 1.2 MB disks booted (all four images pass
`os88disk.py --verify`); the interrupted-save offer on the glass; a mouse
click on a Preferences row; the Flate Off refusal on the glass; the clipboard's
bytes; and anything on 86Box or a physical machine. `docs/FIELD-NOTES.md` is
where a hardware result belongs.

## 18. Building and testing

```
make deps                     # nasm, qemu, python 3.11+ (once)
tools/setup-cc.sh             # the C toolchain (SmallerC), once
make pagedesk pagedeskdisk    # PAGEDESK.O88 and build/pagedesk*.img (4 geometries)
make pagedeskdisk BUILD=...   # the same into a private tree
make                          # everything, then the fast tier
make test TESTAPPS=build/pagedesk.img     # boot with PageDesk's disk in B:
```

For other adapters use a private tree (`make BUILD=build-cga VIDEO=cga ...`)
and the screen size in `tools/mouse.py --screen 640x200`, as CLAUDE.md
describes. PageDesk's packaging switches: `PD_REND_MOD`, `PD_ENG_MOD`,
`PD_WRT_MOD` default to the real modules (a stamp rebuilds the parts when you
change them).

Host checks (each prints its own pass/fail line; they need `cc`, `qpdf`,
`mutool` and `pdftoppm` as noted by each target, and skip loudly without them):

| target | what it checks |
|---|---|
| `make pdeng` / `pdefuzz` | the engine against qpdf and MuPDF, fault injection, fuzz |
| `make pdrend` / `pdrend-real` / `pdrend-annot` / `pdrend-fuzz` | the renderer against `mutool draw`, annotations, hostile streams |
| `make pdwrt` / `pdwrt-real` | the writer, with qpdf, mutool and Poppler as judges |
| `make pdflate` / `pdflatefuzz` / `pdflateboot` | Flate |
| `make pdnav` / `pdnotes` | search and links, the notes list |
| `make pdfonts` | the font and art builders |
| `make pdmathtest` | the arithmetic, 41 functions, differentially under an x86 emulator |
| `python3 tools/os88pdfgen.py DIR` | regenerates the eight samples, byte-identical |

Fast rows run by every `make`: `pdfix`, `pdlay`, `pded`, `pdnotes`, `pdjr`,
`pdprep`, `pdstk`, `pdkgate`. The soak and full rows and the target-machine
matrix (PD-29) belong to the lead's final pass and are listed in the
`HOW-PAGEDESK-WAS-BUILT.md` diary.

Samples (`apps/pagedesk/samples/`, listed in `MANIFEST.TXT`): `HELLO.PDF`
(first slice), `XTGUIDE.PDF` (12 pages: the release scenario, "same baud
rate" is on page 3), `MARKUP.PDF` (a worksheet), `PAGES.PDF` (page
operations), `MONOIMG.PDF`, `LATIN.PDF` (encodings), `FLATE.PDF` and
`ACROFORM.PDF`. Larger stress files and the 51-file negative corpus
(`tests/pdfneg/`) are generated, not committed.
