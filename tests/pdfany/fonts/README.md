# tests/pdfany/fonts - the font fixtures of PageDesk's any-PDF round

PAGEDESK-SPEC 20.22.8. Made by `python3 tools/os88pdfx_fonts.py make` (deterministic; every file here is
reproduced byte for byte by it). Nothing is a Microsoft or Adobe binary.

## Provenance of the outlines

All glyph outlines are **Bitstream Vera Sans** (`Vera.ttf`, copyright 2003 Bitstream, Inc.; the licence allows
redistribution of the font and of derivative works as long as the name is changed, and every embedded font
here is named `ABCDEF+Vera...` only as a PDF subset tag, which is the licence's own carve-out for a subset
of the unmodified shapes). The source file was the copy shipped with ReportLab (`reportlab/fonts/Vera.ttf`);
`--src DIR` points the generator at any other copy. The licence text is
`bitstream-vera-license.txt` in that directory; it is copied beside the fixtures as `LICENSE-Vera.txt`.

No fontTools is available on the build host, so `tools/os88pdfx_fonts.py` carries its own writers:

| fixture | what it is |
|---|---|
| `tt-simple.pdf`, `vera-sub.ttf` | a TrueType subset (glyphs renumbered, composites kept), simple font, WinAnsi |
| `cff-simple.pdf`, `vera.cff` | Type1C: the same outlines as Type 2 charstrings (hh/vv/hv/vh curves, h/v line runs, hstemhm + hintmask, widths from nominalWidthX, one local and one global subr), name-keyed, standard strings |
| `cff-2048.pdf`, `vera2048.cff` | the same at 2048 units with a real `FontMatrix [0.00048828125 ...]`, no hints or subrs |
| `cff-cid.pdf`, `vera-cid.cff` | a CID-keyed CFF (ROS, FDArray, FDSelect 3) in a Type0 font, Identity-H, with a ToUnicode and `/W` |
| `cff-cid-cmap.pdf` | the same font through an EMBEDDED Encoding CMap (cidrange and cidchar) |
| `tt-cid.pdf` | the TrueType subset as CIDFontType2, `CIDToGIDMap /Identity`, ToUnicode, `/W` |
| `tt-cidmap.pdf` | the same with a `CIDToGIDMap` STREAM (CID = the code point) |
| `t1-simple.pdf`, `t1-simple.pfa` | Type 1 (PDF raw form: clear text, binary eexec, 512 zeros), `lenIV 4`, flex (`callothersubr` 1/2/0), hint replacement (`callothersubr` 3), a `/Differences` entry |
| `t1-hex.pdf`, `t1-hex.pfa` | the same with a hexadecimal eexec section |
| `type3.pdf` | a Type 3 font: four glyph procedures (`d1` shapes, a `d0` coloured one), `/FontMatrix 0.001`, `/Differences`, `/Widths` |

Each fixture has a `.json` sidecar (the code to glyph-index table, the sizes, the page geometry,
the expected ToUnicode, `/W` and CMap values) that the host checks read. Each PDF has one page a size
(8, 10, 12, 14 and 16 px at 72 dpi) with one glyph a cell, so a glyph's pen position is known exactly.

## Independent reader

`mutool` (MuPDF 1.28.6, FreeType inside): `mutool draw -r 72 -c gray`. Every fixture draws in it with
no warning, which is the check that the generator's output is valid; `hosttest/pdx_check.py` then
compares PDXF's glyphs against it.
