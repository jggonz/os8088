# The KODAK example — SHEET's hand-made workbook fixture

`apps/sheet/` carries four files that are not samples a user is shown. They
are **evidence**: things SHEET wrote, or was fed, in the emulator, kept so a
host-side reader has a real artifact to check against rather than only files
the host wrote itself. No floppy carries any of them. SPEC.md 81.10.9 and
81.47.7 cite this page; `tools/os88sheetfmt.py --selfcheck` (the `sheetfmt`
fast row) is what reads `KODAK.BIF`.

| file | what it is | what reads it |
|---|---|---|
| `KODAK.BIF` | a three-sheet BIFF4 workbook, saved by SHEET | `os88sheetfmt.py --selfcheck` |
| `KODAKEX1.BMP` | Sheet2's column B, exported by Chart > Export | nothing — a picture of the session |
| `KODAKEX2.BMP` | Sheet3's column B, exported by Chart > Export | nothing — a picture of the session |
| `XEROXQ3.SLK` | a one-sheet SYLK test sheet across the function families | nothing yet |

## `KODAK.BIF` — the workbook

Three sheets typed by hand in SHEET and saved in Normal format, which for a
document using more than one sheet is the BIFF4 workbook (SPEC.md 81.47). The
rows are labelled as three business segments over 1981 and 1982; the figures
are the contributor's, and nothing here depends on what they measure.

**Sheet1** — the table, with ten formulas:

|   | A | B | C | D | E |
|---|---|---|---|---|---|
| 1 | Segment | 1981 | 1982 | Change | Pct |
| 2 | Photograph | 1890 | 1955 | `=C2-B2` | `=ROUND(D2/B2*100,1)` |
| 3 | Commercial | 505 | 545 | `=C3-B3` | `=ROUND(D3/B3*100,1)` |
| 4 | Chemicals | 250 | 265 | `=C4-B4` | `=ROUND(D4/B4*100,1)` |
| 5 | Total | `=SUM(B2:B4)` | `=SUM(C2:C4)` | `=SUM(D2:D4)` | `=ROUND(D5/B5*100,1)` |

**Sheet2** — the 1982 column as values (Photograph 1955, Commercial 545,
Chemicals 265).

**Sheet3** — three monthly figures, July 1.42, August 1.86, September 2.15.

What each part is *for*, which is why the self-check asserts exactly these:

- **Three named sheets of 25, 6 and 6 cells.** The multi-sheet writer had no
  host-side check of any kind until this file, because `write_sylk` has no
  notion of a second sheet. A writer bug that put every sheet's borders on the
  active sheet survived for that reason (SPEC.md 81.47.6). The names matter as
  well as the counts: reading `SHEETHDR`'s name count one byte late names every
  sheet `heetN`, and nothing else fails.
- **Both number records in one stream.** Sheets 1 and 2 are whole numbers and
  go out as `RK`. Sheet 3's `1.42` cannot be an `RK`, so it is written as an
  IEEE-754 `NUMBER` and must come back verbatim.
- **Ten FORMULA records whose tokens SHEET wrote.** `decode_rpn` turns them
  back into the text above, which makes this file the reference for
  SPEC.md 81.10.10 (formulas come back as formulas).

## The two pictures

`KODAKEX1.BMP` and `KODAKEX2.BMP` are 240x160 exports of the chart window
(the canvas size, SPEC.md 82.1), and their data are the workbook's:

- `KODAKEX1.BMP` charts Sheet2's column B: three columns scaled to 1955, which
  is the Photograph segment dwarfing the other two.
- `KODAKEX2.BMP` charts Sheet3's column B: three rising columns scaled to 2.15,
  the fractional series the `NUMBER` record exists for.

They record what the chart looked like for those two series, as the
contributor saw it, so a later change to the chart can be compared against
them by eye. No row compares them, and nothing on a disk ships them.

## `XEROXQ3.SLK`

A one-sheet SYLK file: four Xerox divisions' July–September figures, row and
column totals in R1C1 form, and below them one formula from each of several
function families — `STDEV`, `VAR`, `MAX`, `VLOOKUP`, `UPPER`, `DATE`, `NPV`,
`PMT`, `LN`. Its `ID` record says `PWXL`, the program name Excel writes, so it
was written in Excel's dialect and not necessarily by SHEET; where it came from
is the contributor's to say. Two things about it are worth knowing before
anyone builds on it:

- **Nothing reads it.** No row, no self-check and no disk uses it.
- **One label is a row off.** `Sep share` is in A20 and its formula is in B21.

If it becomes a fixture, it should be the read direction's: a file SHEET did
not write, opened in SHEET, its cached values compared against a fresh
recalculation.
