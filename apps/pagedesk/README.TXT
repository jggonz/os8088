PAGEDESK
========

PageDesk reads and marks up PDF files on os8088. It is built for an IBM PC/XT (8088, 4.77MHz, 640KB) and runs in VGA, CGA and Hercules. Double-click a .PDF, or start PAGEDESK.O88 and choose File > Open (Ctrl+O). XTGUIDE.PDF beside the program is the sample guide: the empty window's Sample guide button opens it.

WHAT IT DOES
------------

Read: open, page, zoom, scroll, Find, select text, Copy, follow links inside the file, Pages and Outline panels.

Annotate: Highlight, Note, Text box, Area mark, Ink, Line (with an arrow), Rectangle, Ellipse and a drawn Sign. Marks are real PDF annotations that other readers show.

Edit: move, resize, retype and delete the marks PageDesk added. Undo and Redo cover every mark and every page change.

Pages: rotate, delete, duplicate, insert a blank page, move a page, Extract pages to a new file, Insert pages from another PDF, and File > New for a blank document.

PageDesk does not change the original text of a page, does not print, and never runs a script or opens a link.

MODES AND KEYS
--------------

  F5-F8     Read, Annotate,
            Edit, Pages
  1-6       Mode's tools
  Ctrl+O    Open
  Ctrl+N    New
  Ctrl+S    Save As
  Ctrl+Z/Y  Undo, Redo
  Ctrl+F    Find
  F3        Find next
  Sh+F3     Find previous
  Ctrl+G    Go to page
  Ctrl+C    Copy
  Ctrl+A    Select all
  PgUp/PgDn Page back, on
  Home/End  First, last
  Alt+Left  Back (Alt+Right
            goes forward)
  + -       Zoom in, out
  Arrows    Scroll the page
  Del       Delete the mark
  Tab       Next control
  Esc       Close, cancel,
            then deselect

Every menu item shows its key, and Help > Keyboard lists them. A greyed button or item says why when you press it.

SCREENS
-------

The layout follows the window. A big window (VGA, Hercules) shows the Pages panel and the Notes panel beside the page. A short window, such as CGA at 200 lines, has one bar and opens pages and notes as pop-ups. View hides and shows the panels.

WHAT IT CAN OPEN
----------------

Any PDF, on the machine itself: cross-reference streams and damaged files, every common filter (Flate, LZW, ASCII85, JPEG, CCITT, JBIG2, JPEG 2000), embedded fonts, colour spaces, shadings, patterns and transparency, and files with a password (RC4 and AES). Help > The XT PDF profile lists it.

The decoders are modules PageDesk loads when a page needs them. Flate (zip) needs a 43KB decoder, which PageDesk takes by itself; Preferences can switch it off. A page it can only partly draw says Incomplete rendering and names what is missing. On a 4.77 MHz XT a photo or a JPEG 2000 page takes from seconds to a minute, and an AES-256 file asks before the long password check.

A signed file opens read-only. Form fields are shown as printed; filling them is not supported.

tools/os88pdprep.py in the source tree is optional: it can turn a slow page into a picture on a PC first.

SAVING
------

Save As writes a new file, never the one you opened. A file with marks is saved as an update added to the end of the original: every earlier byte stays in the file. That is not redaction. Delete page and covering with a rectangle do not remove the old content. Sign is a drawn mark, not a certificate signature.

If the name exists, Enter replaces it and Esc keeps it. The copy is read back and checked before it takes the name. Closing with unsaved marks asks: S saves, D discards, Esc keeps.

RECOVERY
--------

Your marks are written to SYSTEM/APPDATA/PAGEDESK.JRN a moment after each change. After a crash, open the same PDF again and the status line asks to recover them: Enter yes, Esc no. Page changes are not in it. If a Save died while it replaced a file, the next open offers to finish: Enter renames PDSAVE.$$$ to the saved name.

PREFERENCES
-----------

View > Preferences sets the default view, which panels show, how marks look on CGA and Hercules, and the Flate cache (Auto or Off). Up and Down pick a row, Left, Right and Enter change it, Esc closes and saves. The settings live in SYSTEM/APPDATA/PAGEDESK.CFG on the disk PageDesk started from. On a write-protected disk they hold for the session and the status line says so.

LIMITS
------

Up to 1,024 pages and 128 marks a document, 240 pages for page operations, 1,024 characters in a note, zoom 25% to 400%. A page can take many seconds to draw on an XT; the status line shows progress and Esc cancels. Help > Document info shows the free memory.

Full details are in docs/PAGEDESK-GUIDE.md in the source tree.
