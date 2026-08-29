# BIFF, as Sheet writes and reads it

What `apps/sheet/sheet.asm`'s BIFF writer and reader actually emit and accept,
and where each fact was verified. This exists because the record numbers move
between BIFF versions in ways that are easy to get subtly wrong — the same
record has a different opcode in BIFF2, BIFF3/4 and BIFF5+, and picking the
wrong one produces a file that looks plausible and no program will open.

## Sheet writes BIFF3 for a single sheet, deliberately

**A reader is backward compatible and not forward compatible.** Excel 4 and
everything after it read a BIFF3 stream happily; a program that knows only
BIFF3 does not even recognise a BIFF4 `BOF` and rejects the file outright. So
emitting the older stream is strictly the wider audience, and it costs nothing
here — every record this writer needs exists in BIFF3.

That is the **single-sheet** save. A workbook of more than one sheet has no
BIFF3 form at all — the workbook stream arrived with BIFF4 — so a multi-sheet
save emits the BIFF4 workbook of §81.10.5 instead: `BOF` 0409H, and `XF` 0443H
with the BIFF4 body layout below. One sheet keeps the BIFF3 stream.

## The records, with their verified opcodes

| record | BIFF2 | **BIFF3** | BIFF4 | BIFF5+ | Sheet |
|---|---|---|---|---|---|
| `BOF`    | 0009H | **0209H** | 0409H | 0809H | writes 0209H; 0409H in the §81.10.5 workbook |
| `FONT`   | 0031H | **0231H** | 0231H | 0031H | writes 0231H |
| `XF`     | 0043H | **0243H** | 0443H | 00E0H | writes 0243H (0443H in the workbook), reads 0243H **and** 0443H |
| `RK`     | —     | **027EH** | 027EH | 027EH | writes 027EH |
| `NUMBER` | 0003H | **0203H** | 0203H | 0203H | writes 0203H |
| `LABEL`  | 0004H | **0204H** | 0204H | 0204H | writes 0204H |
| `EOF`    | 000AH | 000AH | 000AH | 000AH | writes 000AH |

`LABEL` is the second trap, for a different reason than `FONT`: the opcode is
easy, but the BODY changed shape. BIFF2's `LABEL` (0004H) carries a **one-byte**
length and a three-byte cell attribute where later versions put a two-byte XF
index; BIFF3's (0204H) is row(2), col(2), ixfe(2), **cch(2)**, then the bytes.
A reader that takes the length as one byte reads the high half of a 16-bit
count as its first character and desynchronises from there to the end of the
stream. Sheet writes and accepts only the BIFF3 form.

`FONT` is the trap in that table: it is `0031H` in BIFF2 **and again** in
BIFF5/7/8, but `0231H` in exactly BIFF3 and BIFF4. Getting it from a BIFF8
reference and using it in a BIFF3 file would be wrong.

## Record bodies that differ by version

**`BOF` is six bytes in BIFF3**, not four: version (2), substream type (2), and
two documented as unused. BIFF2's is the four-byte form. A reader that trusts
the length field desynchronises on the short one.

**`XF` is twelve bytes in both BIFF3 and BIFF4, with different field order:**

```
BIFF3 (0243H)                       BIFF4 (0443H)
  +0  font index          (1)         +0  font index          (1)
  +1  format index        (1)         +1  format index        (1)
  +2  XF_TYPE_PROT        (1)         +2  type/prot + parent   (2)
  +3  XF_USED_ATTRIB      (1)         +4  align/vert/orient    (1)
  +4  align + parent style(2)         +5  XF_USED_ATTRIB       (1)
  +6  XF_AREA_34          (2)         +6  XF_AREA_34           (2)
  +8  XF_BORDER_34        (4)         +8  XF_BORDER_34         (4)
```

Same length, so a length-driven walk survives either — but the fields land in
different places, and writing a BIFF4 body under a BIFF3 opcode yields
nonsense formatting rather than an error. Sheet writes `XF_TYPE_PROT` = 0 (a
cell XF, unlocked, not hidden), `XF_USED_ATTRIB` = FCH (override every
inherited attribute, since no style XFs are written), and a parent style index
of FFFH — the documented "none", which is the honest value when there is no
style XF to point at.

Horizontal alignment sits at offset 4 bits 2-0 in **both**, which is why the
reader can accept either opcode with one body path.

## Which record carries a value

**`RK` when the value is an exact in-range integer, `NUMBER` otherwise.**

That split keeps every integer file byte-identical to what Sheet wrote before
it had doubles, and means a reader that only understands the old RK integer
subtype still gets those cells. `NUMBER` carries the IEEE-754 double verbatim —
the same eight bytes `apps/os88fp.inc`'s packed form uses, so no conversion
happens in either direction.

On **read**, all four RK subtypes are accepted:

| bit 1 | bit 0 | meaning |
|---|---|---|
| 1 | 0 | signed 30-bit integer in the top 30 bits |
| 1 | 1 | ...that integer, divided by 100 |
| 0 | 0 | the top 32 bits of an IEEE-754 double, low 32 zero |
| 0 | 1 | ...that double, divided by 100 |

The ÷100 and float forms are exactly the ones a 16-bit integer model had to
refuse, and a real Excel file uses them freely — refusing them meant silently
dropping cells.

## Sources

- **OpenOffice.org, "Microsoft Excel File Format"** (Daniel Rentz) — the
  per-version opcode tables and record layouts above. Covers BIFF2 through
  BIFF8, which is what makes the version columns checkable.
- **Microsoft, "Excel97-2007 Binary File Format (xls) Specification"** —
  independently confirms the BIFF2-4 rule that *the version is the high byte of
  the record number*: `=00 BIFF2, =02 BIFF3, =04 BIFF4`, with `bof = 09h`.

Both are third-party documents and are **not tracked in this repository** (see
`.gitignore`): the tree is MIT under one licence file, and vendoring someone
else's specification would break that. Keep local copies alongside the other
reference material if you need them.

## How the output was checked

Not by inspection. A sheet holding two decimals and two integers was saved,
the image's FAT walked on the host, and the record stream decoded field by
field: every opcode, every length, `BOF` version and substream type, the first
`XF`'s bytes, and each cell's decoded value — confirming the stream consumes
cleanly with no length drift. That is the check worth repeating after any
change here, because a wrong length field is invisible until some other
program tries to open the file.
