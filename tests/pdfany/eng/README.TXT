tests/pdfany/eng: the any-PDF reader's fixtures (PAGEDESK-SPEC 20.22.3)

Made by: python3 tools/os88pdfx_eng.py tests/pdfany/eng
with qpdf version 12.4.2, from tools/os88pdfgen.py's samples (this tree's own, generated).
Every file is the tool's own or qpdf's rewrite of one; nothing third-party.
The tool checks each with qpdf as it writes it, and pde_check.py holds the engine
to qpdf over all of them.

HELLO_OS.PDF   HELLO.PDF with uncompressed object and xref streams (qpdf --object-streams=generate)
HELLO_OZ.PDF   HELLO.PDF with Flate object streams and a /Predictor 12 xref stream (qpdf --object-streams=generate)
PAGES_OS.PDF   PAGES.PDF with uncompressed object and xref streams (qpdf --object-streams=generate)
PAGES_OZ.PDF   PAGES.PDF with Flate object streams and a /Predictor 12 xref stream (qpdf --object-streams=generate)
MARKU_OS.PDF   MARKUP.PDF with uncompressed object and xref streams (qpdf --object-streams=generate)
MARKU_OZ.PDF   MARKUP.PDF with Flate object streams and a /Predictor 12 xref stream (qpdf --object-streams=generate)
LATIN_OS.PDF   LATIN.PDF with uncompressed object and xref streams (qpdf --object-streams=generate)
LATIN_OZ.PDF   LATIN.PDF with Flate object streams and a /Predictor 12 xref stream (qpdf --object-streams=generate)
XTGUI_OS.PDF   XTGUIDE.PDF with uncompressed object and xref streams (qpdf --object-streams=generate)
XTGUI_OZ.PDF   XTGUIDE.PDF with Flate object streams and a /Predictor 12 xref stream (qpdf --object-streams=generate)
HELLO_HY.PDF   HELLO_OS.PDF made hybrid: a classic table of the direct objects, /XRefStm naming the stream
PAGES_HY.PDF   PAGES_OZ.PDF made hybrid (compressed object streams)
MARKU_UP.PDF   MARKUP.PDF plus an incremental update with a cross-reference STREAM (/Prev to the table): page 1 rotates
BIG_OS.PDF     160 annotations of ~300 bytes in object streams longer than the 32,512-byte ring, read backwards (uncompressed)
BIG_OZ.PDF     160 annotations of ~300 bytes in object streams longer than the 32,512-byte ring, read backwards (compressed)
PAGES_RS.PDF   PAGES_OS.PDF with startxref past the end of the file: rebuilt by the scan, members included
XTGUI_RZ.PDF   XTGUI_OZ.PDF with startxref past the end of the file (compressed object streams)
MARKU_RW.PDF   MARKU_OS.PDF with the xref-stream row of the /Root's object stream 3 bytes off: the scan
LZW.PDF        LZWDecode content: a 45 KB stream through every code width and table reset, and EarlyChange 0
A85.PDF        ASCII85Decode content (wrapped lines, 'z', a short last group, the /A85 abbreviation) and an [ASCII85 LZW] chain
A85_OS.PDF     A85.PDF in object streams, its filters kept
LENREF.PDF     a content stream whose /Length is a reference (RAW_COPY PLAIN makes it direct)
MARKU_CU.PDF   MARKU_OZ.PDF plus a classic-table update (/Prev to the xref stream): page 1 rotates
RESDIR.PDF     a page whose /ExtGState /GS1 is written inside it (RES_LOOKUP PDRL_DIRECT)
RESDI_OS.PDF   RESDIR.PDF in object streams: the direct value inside a member
