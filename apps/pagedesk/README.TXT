PAGEDESK
========

PageDesk reads PDF files on os8088, on an IBM PC/XT with 640KB and on VGA, CGA and Hercules. Double-click a .PDF, or start PAGEDESK.O88 and choose File > Open. XTGUIDE.PDF beside the program is the sample guide; the empty window's Sample guide button opens it.

THIS BUILD
----------

This is the first build of the program's shell: the window, the three layouts, the menus, the keys and the job that opens and draws a page. The PDF engine, renderer and writer are stand-ins until they land, so a page is drawn as grey bars and the status line says Incomplete rendering - stub renderer. Annotation tools and page operations are shown greyed, and pressing one says why.

KEYS
----

  Ctrl+O  Open
  Ctrl+S  Save As
  Ctrl+F  Find
  Ctrl+G  Go to page
  PgUp    Previous page
  PgDn    Next page
  Home    First page
  End     Last page
  + -     Zoom in, out
  F5-F8   Read, Annotate,
          Edit, Pages
  1-6     The mode's tools
  Tab     Next control
  Esc     Close, cancel

LAYOUTS
-------

The layout follows the window, not the display card. A big window shows the Pages panel and the Notes panel beside the page; a small one, or a CGA screen, keeps one bar and opens pages and notes as pop-ups. View hides and shows the panels.

LIMITS
------

PageDesk reads the XT PDF profile: classic cross-reference tables, ASCIIHex and RunLength streams, the Helvetica, Times and Courier families and simple grey images. Help > The XT PDF profile lists it. It never prints (there is no print service) and never runs a link or a script.
