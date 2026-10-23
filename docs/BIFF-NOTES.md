# BIFF, as Sheet writes and reads it

What `apps/sheet/sheet.asm`'s BIFF writer (`sh_dowrite_biff`,
`sh_biff_workbook`, `sh_biff_fontsxfs`, `sh_biff_cells`, `sh_biff_formula`)
and reader (`sh_doread_biff`) emit and accept, record by record. It exists
because the same record has a different opcode in BIFF2, BIFF3/4 and BIFF5+,
and the wrong one produces a file that looks plausible and no program opens.
§81.7, §81.10.2, §81.10.5-§81.10.7, §81.11.4 and §81.20.1 carry the design
decisions; this file is the byte-level table to check a change against.

The extension is **`.BIF`** (`sh_s_ext_biff`, `ct_s_ext_biff`), not `.XLS`.
Sheet and Chart both dispatch on it.

## Two stream shapes

A save of **one** sheet is a **BIFF3** worksheet stream:

```
BOF 0209H   FONT 0231H x4   XF 0243H x64   COLWIDTH 0024H*   ROW 0208H*
  cell records   EOF 000AH
```

`*` one per column not the standard width (§81.56), one per row not the
standard height (§81.60) - none at all for a sheet that changes neither.

More than one sheet with data (`sh_sheets_used` >= 2) is a **BIFF4 workbook**,
because BIFF3 has no multi-sheet form at all (§81.10.5):

```
BOF 0409H (dt 0100H)   FONT x4   XF 0443H x64   SHEETSOFFSET 008EH
  SHEETHDR 008FH   BOF 0409H (dt 0010H)   COLWIDTH*   ROW*   cells   EOF
                                                             one per used sheet
EOF
```

BIFF3 is emitted whenever it can be because a reader is backward compatible
and not forward compatible: everything from Excel 4 on reads BIFF3, while a
BIFF3-only reader does not recognise a BIFF4 `BOF`. `[sh_wb_xf4]` is the
writer's version switch, set only inside `sh_biff_workbook`.

## Opcodes by version

| record | BIFF2 | **BIFF3** | BIFF4 | BIFF5+ | Sheet |
|---|---|---|---|---|---|
| `BOF`          | 0009H | **0209H** | 0409H | 0809H | writes 0209H (workbook: 0409H); reads neither, skips by length |
| `FONT`         | 0031H | **0231H** | 0231H | 0031H | writes and reads 0231H |
| `XF`           | 0043H | **0243H** | 0443H | 00E0H | writes 0243H (workbook: 0443H); reads 0243H **and** 0443H |
| `RK`           | -     | **027EH** | 027EH | 027EH | writes and reads 027EH |
| `NUMBER`       | 0003H | **0203H** | 0203H | 0203H | writes and reads 0203H |
| `LABEL`        | 0004H | **0204H** | 0204H | 0204H | writes and reads 0204H |
| `BOOLERR`      | 0005H | **0205H** | 0205H | 0205H | writes and reads 0205H |
| `FORMULA`      | 0006H | **0206H** | 0406H | 0006H | writes 0206H (workbook: 0406H); reads both |
| `SHEETSOFFSET` | -     | -     | 008EH | -     | writes in the workbook; reader ignores it |
| `SHEETHDR`     | -     | -     | 008FH | -     | writes in the workbook; reads 008FH |
| `DEFINEDNAME`  | 0018H | **0218H** | 0218H | 0018H | writes and reads 0218H |
| `COLWIDTH`     | 0024H | **0024H** | 0024H | -     | writes and reads 0024H; reads BIFF3+'s `COLINFO` 007DH too (§81.56) |
| `ROW`          | 0008H | **0208H** | 0208H | 0208H | writes 0208H; reads both - 0008H only after a BIFF2 `BOF` (§81.60) |
| `EOF`          | 000AH | 000AH | 000AH | 000AH | writes and reads 000AH |

Three traps in that table:

- **`FONT` is 0031H in BIFF2 and again in BIFF5/7/8**, and 0231H only in
  BIFF3 and BIFF4. A number copied from a BIFF8 reference is wrong here.
- **`LABEL` changed body, not just opcode.** BIFF2's 0004H carries a
  one-byte length and a three-byte cell attribute; BIFF3's 0204H is row(2),
  col(2), ixfe(2), **cch(2)**, bytes. A reader that takes the length as one
  byte reads the high half of the count as its first character and never
  resynchronises.
- **`BOF` is six bytes in BIFF3 and BIFF4** (version, substream type, two
  unused), four in BIFF2, eight in BIFF5. Skip it by its length field, as
  the reader does; a fixed skip desynchronises on any other version.
- **`DEFINEDNAME` is the same trap as `FONT`, and the newer one to fall
  into**: 0018H in BIFF2, 0018H again in BIFF5 and BIFF8, 0218H only in
  BIFF3 and BIFF4. Nearly every reference says 0018H because BIFF8 is what
  people read, and writing that into a BIFF3 stream produces a file no
  reader complains about - an unrecognised opcode is skipped by its length,
  so the names are simply not there.

## Record bodies, as written

Every record is `opcode(2) length(2) body`; every cell record starts
`row(2) col(2) ixfe(2)`. Lengths below are the body's.

**`BOF`** (6): version 0300H or 0400H, dt 0010H worksheet or 0100H workbook
globals, 0000H.

**`FONT`** (11): height 200 (10pt in twips), options (bit 0 bold, bit 2
underline), colour index 0, then a length byte 4 and `Helv`. Four are
written, indices 0-3 = normal, bold, underline, bold+underline, which is
bits 0-1 of `SH_FMT_*` (§81.4).

**`XF`** (12), sixty-four of them so that a cell's `SH_FMT_*` byte **is** its
`ixfe` with no lookup on either side. Same length in both versions, different
field order in the middle:

```
BIFF3 (0243H)                       BIFF4 (0443H)
  +0  font index          (1)         +0  font index          (1)
  +1  format index        (1)         +1  format index        (1)
  +2  XF_TYPE_PROT        (1)         +2  type/prot + parent  (2)
  +3  XF_USED_ATTRIB      (1)         +4  align/vert/orient   (1)
  +4  align + parent      (2)         +5  XF_USED_ATTRIB      (1)
  +6  XF_AREA_34          (2)         +6  XF_AREA_34          (2)
  +8  XF_BORDER_34        (4)         +8  XF_BORDER_34        (4)
```

Values: font index = `ixfe & 3`; format index from `sh_biff_numfmt_tab`
(below); `XF_TYPE_PROT` = 0 (a cell XF, unlocked, not hidden);
`XF_USED_ATTRIB` = FCH (every attribute is this XF's own, since no style XF
is written); parent style = FFFH, the documented "none"; horizontal alignment
= bits 2-3 of the format byte, which are already `XF_HOR_ALIGN`'s 0-3
(General/Left/Center/Right); area and border zero in these 64. So BIFF3 writes the words
`FC00H`, `align|FFF0H` and BIFF4 writes `FFF0H`, `align|FC00H`. Writing one
body under the other opcode yields wrong formatting, not an error.
Horizontal alignment is at offset 4 bits 2-0 in **both**, and the font and
format indices at 0 and 1, which is why the reader has one XF body path for
both opcodes.

**Border `XF`s** (SPEC.md 81.117 — READ here, never written by this fork): after the 64, one more per distinct
(format byte, border byte) pair the border table holds, at most 64, numbered
from 64 in the order `sh_bxf_collect` finds them. The body is the format
byte's, plus `XF_AREA_34` = 4A02H for Shade (pattern 2, the 50% grey,
colour 8 on colour 9) and `XF_BORDER_34`'s four bytes - top, left, bottom,
right - each 41H for a set edge (line style 1 thin, colour 8). A bordered
cell's record carries its pair's `ixfe`.

**`BLANK`** (6, 0201H): the head alone, for a bordered cell with no value -
the only way its XF reaches the file.

**`NOTE`** (6 + cch, 001CH): row(2), col(2), cch(2), the text, a line break
being 0AH. After the sheet's cell and `BLANK` records; a note is at most
`SH_NOTEMAX` - 1 = 239 characters, far under the record's 2,048.

**`RK`** (10): head, then the packed value (see below).

**`NUMBER`** (14): head, then the IEEE-754 double verbatim - the same eight
bytes `apps/os88fp.inc`'s packed form uses, so there is no conversion.

**`LABEL`** (8 + cch): head, cch(2), the bytes. cch is capped at 255 on
write (the format's ceiling; `SH_EDITMAX` = 63 binds first) and truncated to
`SH_EDITMAX` on read while the record's own length still governs the skip.

**`BOOLERR`** (8): head, value byte, type byte (0 = boolean, 1 = error).
Written only for an error cell whose formula could not be tokenised.

**`FORMULA`** (18 + cce): head, result(8), flags 0000H, cce(2), the RPN
tokens from `sh_rpn_emit` - numbers, references, the operators, `tBool`,
and since SPEC.md 81.61 `tStr` (17H, a count byte then the characters),
`tConcat` (08H) and `tErr` (1CH, then the file's error code). A formula it
cannot express is written as its cached value alone. A numeric result is the double; an error result
is byte 0 = 2, byte 2 = the error code, bytes 6-7 = FFFFH. The token
array's function indexes are one byte under 0206H and a word under 0406H,
which is the one place the workbook changes a body beyond `XF` (§81.10.2).

**`COLWIDTH`** (4): first column, last column (a byte each), the width in
1/256ths of a character. One record per column, however many share a width.

**`ROW`** (16): the row, first and one-past-last cell column (0, 0 - no
cells are claimed), the height in twips with bit 15 clear (a custom height),
0000H, BIFF3/4's offset to the row's first cell (0000H), option flags
0140H (bit 8 is always set; bit 6 says the height was set by hand) and XF
index 000FH, unused while bit 7 is clear. Nothing depends on the two zeroed
offsets: the reader here walks the stream, and LibreOffice (checked on
tests/sheetrowh.py's save, converted to .fods) reads all four heights to the
point - 18, 8, 9.75 and 30.

**`SHEETSOFFSET`** (4): stream offset of the first `SHEETHDR`, dword.
**`SHEETHDR`** (11): byte length of the substream that follows, dword,
backpatched after the substream is built, then a length byte 6 and
`Sheet1`..`Sheet4`. A sheet with no cells gets no substream.

## Values

**Which record a number takes: `RK` when it is an exact integer a signed
word can hold, `NUMBER` otherwise** (`sh_biff_cells`: `fp_a2i`, then
`fp_i2a` and compare). Integers therefore go out as the RK integer subtype
`sh_rkenc` produces, `(value << 2) | 2`, sign-extended to 32 bits.

**On read, all four RK subtypes are decoded** (`sh_rkdec_d`):

| bit 1 | bit 0 | meaning |
|---|---|---|
| 1 | 0 | signed 30-bit integer in the top 30 bits |
| 1 | 1 | ...that integer, divided by 100 |
| 0 | 0 | the top 32 bits of an IEEE-754 double, low 32 zero |
| 0 | 1 | ...that double, divided by 100 |

A real Excel file uses the ÷100 and float forms freely; a reader that
accepted only the integer form dropped those cells in silence.

**Number formats** are BIFF built-in ids, no `FORMAT` record is written:
`sh_biff_numfmt_tab` maps General/Currency/Comma/Percent to 0/5/3/9
(`General`, `$#,##0`, `#,##0`, `0%`), and `sh_biff_numfmt_from_id` maps only
those four back - any other id, including a custom `FORMAT`'s, reads as
General.

**Error codes are translated at the file's edge.** Inside SHEET they are
`SH_ERR_*`, 1-7 in `ERROR.TYPE` order; BIFF's error byte in `BOOLERR` and in
a `FORMULA` result is 00H #NULL!, 07H #DIV/0!, 0FH #VALUE!, 17H #REF!, 1DH
#NAME?, 24H #NUM!, 2AH #N/A. `sh_biff_e2b` converts on the way out
(`sh_biff_cells`' `.aserr`, `.errresult`) and `sh_biff_b2e` on the way in
(`sh_doread_biff`'s `.isboolerr`, `.isformula`) - the four places this note
used to say a translation had to go (SPEC.md 81.38.1). Before it, SHEET's
own files round-tripped because the reader applied the same wrong numbers,
while a `#DIV/0!` written here was byte 2 where Excel expects 07H, and
Excel's 07H read back here as #N/A.

## What the reader accepts, and what it drops

- Any opcode not in the table is skipped by its length, `BOF` and
  BIFF5-8's `XF` 00E0H included - so a BIFF5+ file loads its cells with no
  formatting rather than misformatted ones.
- A record whose length runs past the file, or wraps 16 bits, ends the read
  (every byte off a disk is hostile). The read reports "Loaded" with what it
  had.
- `FONT` and `XF` are tracked up to `SH_BIFF_FONT_CAP` = 32 and
  `SH_BIFF_XF_CAP` = 128; a cell pointing past either reads back General.
  Each XF's border byte is read too: any line style in `XF_BORDER_34` is that
  edge and any fill pattern in `XF_AREA_34` is Shade.
- `BLANK` (0201H) applies its XF's border to the cell; `NOTE` (001CH) becomes
  the cell's note, cut at 239 characters or at a NUL. A continuation `NOTE`
  (row FFFFH) is skipped by the row check below.
- A row >= `SH_ROWS` or column >= `SH_COLS` skips the record: a row with
  bit 14 set would land on another sheet through the packed key.
- `FORMULA`: the tokens are decoded back to the formula's text
  (`sh_biff_dcrpn`, SPEC.md 81.10.10); only a token array the decoder
  refuses falls back to the cached result, as a value.
- `COLWIDTH` and `COLINFO` round to whole characters and are clamped to
  what the Column Width dialog allows (SPEC.md 81.56).
- `ROW`: bit 15 set means the default height and is skipped; a height of
  0 (hidden, which this grid cannot draw) is skipped too; the rest are
  clamped to the Row Height dialog's range, 146-874 twips (SPEC.md 81.60).
- `BOOLERR` with the flag clear reads as the logical (SPEC.md 81.51); it was
  0 or 1 while Sheet had no logical type, and is what Sheet writes for one.
- A read clears all four grids first. A plain stream loads onto the sheet
  the user was on, which stays current. In a workbook, `EOF` ends the read
  only before the first `SHEETHDR`; each `SHEETHDR` advances the target
  sheet, its name is ignored (sheets are positional), a fifth and later
  sheet piles onto sheet 3, and the read ends on sheet 0.

**Chart's reader** (`ct_read_biff` in `apps/chart/chart.asm`) is
independent and narrower: `RK` (all four subtypes) and `NUMBER` only, so a
column of `FORMULA` cells charts as empty.

## Sources

- **OpenOffice.org, "Microsoft Excel File Format"** (Daniel Rentz) - the
  per-version opcode tables and record layouts above. Revision 1.42 or later
  is the one to have: its section 3.11 carries the built-in function index table
  that `sh_rpn_fid`/`sh_rpn_fvar` are built from, and earlier revisions have
  that section reading only `2do` (§81.10.2).
- **Microsoft, "Excel97-2007 Binary File Format (xls) Specification"** -
  confirms the BIFF2-4 rule that the version is the high byte of the record
  number (`00` BIFF2, `02` BIFF3, `04` BIFF4, with `BOF` = 09H).

Neither is tracked here: the tree is MIT under one licence file and vendoring
someone else's specification would break that. `.gitignore` reserves
`docs/excelfileformat.pdf` for a local copy of the first.

## Checking a change

No gate decodes the stream. The check that catches a wrong length field -
which is invisible until another program opens the file - is to save a sheet
holding an integer, a decimal, a label, a formula and an error as `.BIF`
under MartyPC, read the file off the run directory's copy of the floppy with
`tools/os88disk.py`'s FAT12 reader, and walk it record by record: every
opcode and length in the table above, `BOF`'s version and dt, the first
`XF`'s twelve bytes, each cell's decoded value, and the walk landing exactly
on `EOF`. Then load it back and compare the grid.

**`DEFINEDNAME`** (BIFF3-4): option flags(2) - 0 is visible, user-defined,
standard, simple formula - shortcut(1), `ln`(1) the name's length in
characters, `sz`(2) the size of the formula data, the name (`ln` bytes,
**counted, not NUL-terminated**), then the RPN token array (`sz` bytes, with
no size field of its own).

Sheet's `sz` is always 7: one `tAreaR` (25H) plus a six-byte cell-range
address - row1(2), row2(2), col1(1), col2(1) - the same encoding the FORMULA
tokens use, with the two relative-flag bits in each row word **clear**.

`tAreaR` and not `tAreaN`: excelfileformat 3.9.11 says a defined name in
BIFF2-BIFF4 uses `tAreaN`, the relative form, but *"if all components of the
cell range address are absolute, a tArea token is used instead"* - and a name
in this app is always absolute, the same rule SYLK's `NN` record follows.
Reference class (25H), not the value class (45H) the cell formulas use: the
name **is** the range, it is not a value read out of one.

Where the record goes in the stream is **not documented for BIFF3/4** -
excelfileformat's 4.2.3 (BIFF4 worksheet stream order) and 4.10.1 (references
in BIFF2-BIFF4) both read `2do`. Sheet follows the BIFF5 worksheet stream the
same document *does* give: the link table sits after the globals and before
the cell records.
