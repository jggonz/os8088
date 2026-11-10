# PAGEDESK-SPEC - PageDesk, a native PDF viewer and editor

PageDesk is a PDF reader and annotator for os8088 that runs natively on the
4.77 MHz 8088 XT with 640 KiB and on VGA, CGA and Hercules. This document is
its binding contract, outside SPEC.md on the C64-SPEC/WEAVE-SPEC precedent:
the profile, the module ABI, the numeric and geometry formats and the on-disk
representations are pinned here **before** product code, and a change to any
of them is a change to this file first. SPEC.md 128 is the anchor that points
here. A bare section number in this file means a section of this file.
`docs/plans/PAGEDESK-PLAN.md` is the story list, the platform record and the
capability matrix; this file is the contract those stories build against.

Story IDs (PD-01 ... PD-30) are the owner's, from the assignment the plan
quotes. Capability labels are the assignment's: **XT required**, **Host
assistance**, **Enhanced**, **Exploratory**.

## 1. Scope

### 1.1 What PageDesk is

A package, `PAGEDESK.O88`, that opens PDF files from any os8088 volume,
renders their pages in a window, lets the user navigate, search, select and
copy text, add standard PDF annotations (notes, highlights, area marks, text
boxes, ink, shapes, a drawn signature), reorder/rotate/insert/delete pages,
extract and insert pages across documents, create a blank document, and save
the result as a real PDF that an independent reader renders. It does NOT edit
original page content (no paragraph reflow), does not print (there is no print
service, SPEC.md 68.11), and does not claim cryptographic signing, redaction or
sanitisation.

### 1.2 The baseline machine

An IBM PC/XT, 8088 at 4.77 MHz, 640 KiB conventional RAM, no FPU/EMS/XMS,
`kern_big`, one floppy or a hard disk, VGA or CGA or Hercules. Everything
labelled XT required in the plan works there, offline. Nothing on that path
may use an 80186+ instruction (`cpu 8086` and `tools/cc8086.py` gate every
assembly, including the compiled C of every module).

### 1.3 The PageDesk XT PDF profile

A file is **supported** when every structure the page being shown uses is in
this list. The header version is reported but never decides support.

| area | supported | refused with a reason (`PDE_*`, 3.6) |
|---|---|---|
| file structure | classic `xref` tables, `trailer`, `startxref`, incremental updates chained by `/Prev` (8 revisions, 4.6), free entries, generations | xref streams, object streams (`PDE_XREFSTM`), hybrid files read through their classic table only |
| objects | null, booleans, integers, reals, literal and hex strings with every escape, names with `#xx`, arrays, dictionaries, indirect references, comments | - |
| streams | direct and indirect `/Length`; filters `ASCIIHexDecode`, `RunLengthDecode`, and pipelines of them (at most 4 stages) | `FlateDecode` on the XT build (`PDE_FLATE`, Enhanced in PD-25), `LZWDecode`, `DCTDecode`, `CCITTFaxDecode`, `JBIG2Decode`, `JPXDecode`, `/DecodeParms` predictors (`PDE_FILTER`) |
| pages | page tree with inherited `/MediaBox` `/CropBox` `/Rotate` `/Resources`, cycle-checked; `/Contents` as one stream or an array | page boxes outside +/-2047 pt (`PDE_RANGE`) |
| graphics | `q Q cm w J j M d ri i gs`(ignored safely) `m l c v y h re S s f F f* B B* b b* n W W*`, `G g RG rg K k`, `CS cs SC sc SCN scn` for DeviceGray/RGB/CMYK, `BX EX BMC BDC EMC MP DP` | shading `sh`, patterns, ICC/Separation/DeviceN/Indexed colour, transparency, soft masks (`PDE_GRAPHICS`, page drawn as **Incomplete rendering**, 10.6) |
| text | `BT ET Tc Tw Tz TL Tf Tr Ts Td TD Tm T* Tj TJ ' "`; Type1 base-14 Latin fonts (Helvetica, Times, Courier families) by name, `/Widths` when present, `Standard`/`WinAnsi`/`MacRoman` encodings and `/Differences` with the glyph names of 11.4 | Symbol, ZapfDingbats, TrueType, CFF, Type0/CID, Type3 (drawn with the substitute face and flagged Incomplete rendering), render modes 4-7 (clip text) |
| XObjects | Form XObjects with `/Matrix` `/BBox` `/Resources`, nested 4 deep; Image XObjects 1 and 8 bits per component, DeviceGray (and DeviceRGB 8-bit, converted to grey), unfiltered/ASCIIHex/RunLength, `/ImageMask` stencils, `/Decode [1 0]` | inline images `BI ID EI` beyond 4 KiB, JPEG/CCITT/JBIG2, `/SMask`, colour-key masks |
| annotations | display of any annotation that has an `/AP /N` appearance this profile can draw; PageDesk creates `Text`, `Highlight`, `Square`, `Circle`, `Line`, `Ink`, `FreeText` (13) | `Widget`/AcroForm editing (PD-26, Enhanced), `Popup` windows are not drawn, `Link` actions are never run automatically |
| security | - | `/Encrypt` (`PDE_ENCRYPT`); a document carrying `/Perms` or a signature field with a `/V` is opened **read-only** (`PDE_SIGNED`) |

No JavaScript, launch action, embedded file, remote resource or external link
is ever executed.

### 1.4 What "editing" means

PageDesk edits annotations it can represent and content it added. "Edit text"
means a PageDesk-added FreeText. "Delete page" and "Cover with a rectangle" do
not remove earlier bytes from an incrementally saved file, and the UI says so
(13.9). "Sign" places drawn ink and is labelled **Drawn signature**.

## 2. The package and its segments

### 2.1 Files

All source lives under `apps/pagedesk/`. Prefixes: `pd` for shared, `pdu` UI,
`pde` engine, `pdr` renderer, `pdw` writer, `pdf` fonts and assets. Host tools
are `tools/os88pdf*.py`. Every C file obeys SPEC.md 73's four rules.

### 2.2 Segments

| part | file in build/ | kind | ships as | holds | image+bss cap |
|---|---|---|---|---|---|
| primary | `pagedesk.o88` | C package (`pagedesk.asm` shim + `pagedesk.c`) | `PAGEDESK.O88` | UI, layouts, menus, keyboard, the edit model, job scheduler | 56 KiB |
| overlay | `PAGEDESK.OVL` | `ovl_*` functions of the primary (`CC_OVL_PART`, lazy) | a part | Preferences, Help/About text, page-ops and recovery dialogs, rare commands | 24 KiB |
| `PDENG` | `PDENG.MOD` | C module (`pdeng.asm` + `pdeng.c`), eager `OP_SEG` | a part | file layer, lexer, objects, xref, page tree, filters, text extraction and search | 56 KiB |
| `PDREND` | `PDREND.MOD` | C module (`pdrend.asm` + `pdrend.c`), eager `OP_SEG` | a part | content interpreter, path flattening, band rasteriser, glyph and image drawing, thumbnails | 56 KiB |
| `PDWRT` | `PDWRT.MOD` | C module (`pdwrt.asm` + `pdwrt.c`), lazy `OP_SEG` | a part | serialiser, appearance streams, incremental save, fresh documents, page-graph copy | 48 KiB |
| `PDRES` | `PDRES.BIN` | `OP_ASSET`, lazy, read by offset | a part | document font bank (11.2), base-14 width tables (11.3), icons and cursors (17.6) | file only |

One file on disk: `PAGEDESK.O88` carries every part (SPEC.md 20.12). The
package is launched from any folder; parts are fetched from the folder it was
launched from.

### 2.3 Why C, and where assembly

C is chosen because every parser, writer and model component compiles a
second time on the host (2.5) and is tested there against the fixtures with
independent tools - the property that makes bounds and save correctness
reviewable. Per SPEC.md 73.11 nothing that touches pixels per iteration is C:
span filling, glyph composition into a band, image row scaling, 32-bit and
16.16 arithmetic (`pdmath.inc`, 4.2) and memory movement between segments are
assembly procs the C calls once per run.

### 2.4 Shared source

`apps/pagedesk/pdabi.h` (C) and `apps/pagedesk/pdabi.inc` (assembly) are the
ONE contract every segment includes; `tools/os88pdabi.py --check` (run by the
build) fails when they disagree. `pdmath.inc` and its host twin `pdmath.c` are
included as source by each module that needs them (WEAVE-SPEC 1.2's rule:
shared as source, never as a copy).

### 2.5 Host builds

Each module's C compiles on the host with `cc` against `apps/pagedesk/hosttest/`
(stub `os88.h`, segment arena, file layer on stdio, `pdmath.c`). Host
harnesses run in `make pagedesk` behind a stamp (the lemmings pattern), are
deterministic, and use only the committed fixtures and the independent tools
of 18.4.

## 3. The module ABI

### 3.1 The door

Every module image starts with an 8-byte header and its door at +8, exactly
`apps/loom/lmpvmod.asm`'s shape:

| offset | word |
|---|---|
| +0 | magic (`PDM_MAGIC_ENG` `'PE'`, `PDM_MAGIC_REND` `'PR'`, `PDM_MAGIC_WRT` `'PW'`) |
| +2 | `PDM_ABI` |
| +4 | image size |
| +6 | bss size (the module zeroes its own bss on first entry) |
| +8 | the door: `call far` with AL = verb, BX, CX, DX = arguments; answer AX (and DX where a verb says so), CF = 1 on failure with AX = a `PDE_*` code. A resumable verb that has more to do answers **CF = 0, AX = `PDE_BUSY`** - it is not a failure - and the caller calls again; CF = 0 with AX = `PDE_OK` (or the verb's answer) is done (DECISIONS D11) |

DS is the caller's on entry and on return; the door sets DS = CS and calls
the module's C dispatcher `pdX_verb(verb, bx, cx, dx)`.

### 3.2 Data crosses segments only through the exchange block

No verb ever takes a near pointer into the caller's DS. Arguments that do not
fit in three registers travel in the **exchange block** (XB): a 1 KiB claim
the primary makes at launch, whose segment every module receives at
`PDV_INIT`. Each verb's XB layout is a `struct pdxb_*` in `pdabi.h`; a module
copies it in and out with `pd_fcopy_in`/`pd_fcopy_out` (4.3). Bulk data lives
in claims whose segments are passed in registers.

### 3.3 Who calls whom

The primary calls every module. `PDREND` calls `PDENG` (object and stream
access) and `PDWRT` calls `PDENG` (reading the source). No module calls the
primary. `PDENG` calls nobody. A module calls another through the far vector
table the primary passes at `PDV_INIT` (`struct pdxb_init`). Calls are made on
the UI task only (15.1), so file I/O is legal inside every verb.

### 3.4 Memory ownership

Only the primary claims memory (SPEC.md 20.12.5: a part may not call the
ES-fenced claim slots). At `PDV_INIT` and at document open the primary hands
each module the claims 16.2 assigns it. A module never assumes a claim
survives a `PDV_DOC_CLOSE`. Every claim is pinned (no `OSAPI_MEM_MOVABLE`) in
the first release.

### 3.5 Verb ranges

`PDENG` 0x00-0x3F, `PDREND` 0x40-0x7F, `PDWRT` 0x80-0xBF; 0x00/0x40/0x80 are
each module's `INIT`. The complete list is `pdabi.h`; adding a verb is an edit
to `pdabi.h` and this table, by the lead or with the lead's assignment.

### 3.6 Errors

Every failure is a `PDE_*` code from `pdabi.h` and every code has one short
user-facing sentence (`pdu_errtext`). A refusal names the feature or the
limit, never "error". Codes split into parse (`PDE_SYNTAX`, `PDE_XREF`,
`PDE_TRUNC`, `PDE_CYCLE`, `PDE_DEPTH`), unsupported (`PDE_FLATE`, `PDE_FILTER`,
`PDE_FONT`, `PDE_GRAPHICS`, `PDE_ENCRYPT`, `PDE_XREFSTM`, `PDE_SIGNED`),
limit (`PDE_RANGE`, `PDE_LIMIT`, `PDE_NOMEM`), I/O (`PDE_IO`, `PDE_DISKFULL`,
`PDE_WPROT`, `PDE_EXISTS`, `PDE_CHANGED`) and `PDE_CANCEL`.

## 4. Numbers

### 4.1 Types

| name | C | meaning |
|---|---|---|
| `pd_u32` | `struct { unsigned lo, hi; }` | unsigned 32-bit: file offsets, lengths, byte counts |
| `pd_fx` | `struct { unsigned lo; int hi; }` | signed 16.16: every PDF number, every matrix element |
| `pd_pt` | `int` | page-space coordinate in **1/16 point** ("pt16"), range +/-32767 = +/-2047.94 pt |
| `pd_px` | `int` | device pixel |

There is no `long` (SPEC.md 73.7). A `pd_u32`/`pd_fx` is a static or a field,
always passed by pointer.

### 4.2 `pdmath`

The only arithmetic on `pd_u32`/`pd_fx`: `pdm_u32_set/add/sub/cmp/add16/
mul16/divmod16/shr`, `pdm_fx_from_int/to_int_floor/to_int_round/add/sub/mul/
div/neg/cmp`, `pdm_fx_parse` (PDF real to 16.16, rejects out of range with
`PDE_RANGE`), `pdm_fx_to_pt16`, `pdm_pt16_to_fx`, and `pdm_mat_mul`/
`pdm_mat_apply` over the 6-element `pd_mat`. Out-of-range results set a sticky
overflow flag the caller reads (`pdm_ovf`); a page whose transforms overflow
is drawn Incomplete, never wrapped. Assembly on the target, `pdmath.c` on the
host; the two are checked against each other by a host differential test.

### 4.3 Far memory

`pd_fcopy_in(seg, off, near_dst, n)`, `pd_fcopy_out(near_src, seg, off, n)`,
`pd_fcopy(dseg, doff, sseg, soff, n)`, `pd_fpeekb/w`, `pd_fpokeb/w`,
`pd_ffill`. They save and restore ES (SPEC.md 73.5.1). n is at most 0xFFF0 and
`off + n` must not wrap; the procs refuse rather than wrap.

## 5. Geometry

### 5.1 Canonical space

Every stored geometry - annotation `/Rect`, `/QuadPoints`, ink points, page
boxes, text-box frames, selection quads, search hits - is in the page's
**unrotated, bottom-left-origin user space**, in pt16. Screen coordinates are
never stored. The page's identity carries its own `/MediaBox` origin; nothing
normalises it to zero.

### 5.2 The effective box

`box = CropBox ∩ MediaBox` (MediaBox when CropBox is absent), all inherited,
and the effective rotation `R = (/Rotate + user rotation) mod 360`, in
{0, 90, 180, 270}; any other `/Rotate` is refused `PDE_RANGE`. `W = x1 - x0`,
`H = y1 - y0` in pt16. The displayed page is `W x H` for R in {0, 180} and
`H x W` otherwise.

### 5.3 The view transform

`struct pd_view` holds the box, R, the zoom in percent (25..400, steps in
9.1), the aspect ratio `asp_num/asp_den`, the page's top-left on screen
`(ox, oy)` (window content origin + margins - scroll). Scale: `sx = zoom/100`
pixels per point, `sy = sx * asp_num / asp_den`.

Forward, page (x, y) to displayed (u, v) in pt16, origin at the displayed
page's top-left:

| R | u | v |
|---|---|---|
| 0 | x - x0 | y1 - y |
| 90 | y - y0 | x - x0 |
| 180 | x1 - x | y - y0 |
| 270 | y1 - y | x1 - x |

then `px = ox + floor(u * sx / 16)`, `py = oy + floor(v * sy / 16)`. The
inverse undoes each step exactly; a pixel maps to the pt16 of its top-left
corner. `pdm_view_fwd`/`pdm_view_inv` in `pdmath` are the ONLY
implementation, used by the renderer, hit testing, selection, ink capture,
annotation placement and export. Rounding happens once, at the raster
boundary. A host test round-trips every pixel of a 200-pixel strip for every
R on a nonzero-origin cropped page.

### 5.4 Pixel aspect

The OS has no aspect slot, so the primary derives it from `OSAPI_VIDEO`'s
geometry assuming a 4:3 display: `asp = (h * 4) / (w * 3)` reduced - VGA
640x480 1/1, EGA 640x350 35/48, Hercules 720x348 29/45 (720x350: 35/54), CGA
640x200 5/12. The `Square pixels` preference forces 1/1. The derivation reads
the live geometry; nothing assumes 348 or 350.

### 5.5 Appearance streams

An appearance's `/BBox` and `/Matrix` are its own; the renderer maps them by
PDF 1.7 12.5.5 (the algorithm, not an approximation) and the writer emits
`/Matrix` absent (identity) with `/BBox` equal to the annotation's `/Rect`
moved to the origin.

## 6. The file layer (`PDENG`)

### 6.1 Reads

The source is read through a block cache of `PD_BLK` = the volume's cluster
size (from `os88_disk_cluster_sectors`, at most 4 KiB; a larger cluster is read
as 4 KiB windows through `READ_SEQ`'s cursor), `PD_NBLK` blocks in one claim
(16 KiB default, LRU). `OSAPI_FILE_READ_SEQ` with an `FSEQ_OFF` seek is the
read primitive; the whole file is never resident. Offsets are `pd_u32`; a
file over 2^31 is refused.

### 6.2 Identity

At open the engine records the file's size and a fingerprint: CRC-16 of the
first and last 1 KiB plus the size. Save and recovery (14.4, 14.7) refuse
`PDE_CHANGED` when the fingerprint moved.

## 7. Objects and the cross-reference

### 7.1 Lexer and parser

A real tokenizer over the block cache (no pattern search). Limits, all in
`pdabi.h`: token 1 KiB (`PD_MAXTOK`), string 4 KiB (longer strings are read in
pieces by the verbs that need them), name 127 bytes, nesting 32
(`PD_MAXDEPTH`), array/dict items 4096 per container, integer +/-2^31,
real per 4.2.

### 7.2 The object arena

Parsed objects are nodes in a bounded arena claim (8 KiB default) with
mark/release scopes; a verb that resolves an object holds it until its scope
is released. Exhausting the arena is `PDE_LIMIT`, recoverable.

### 7.3 The cross-reference without an index in RAM

A classic xref entry is exactly 20 bytes, so the engine keeps only a
**subsection table**: per revision, per subsection, (first object, count, file
offset of its first entry), at most `PD_MAXSUBSEC` = 256 subsections across
at most `PD_MAXREV` = 8 revisions, newest first. Looking up object n reads the
20-byte entry from the newest subsection containing n, through the block
cache, plus a small direct-mapped cache of resolved offsets (512 entries).
This answers "many objects exceeding the RAM index" by construction.

### 7.4 Rules

The newest definition wins. A free entry hides older ones. A generation
mismatch between the reference and the entry is treated as a free object
(null). An offset beyond the file, an entry whose bytes do not read
`n g obj` at that offset, or a `/Prev` cycle (offsets visited are recorded)
fails `PDE_XREF` with the revision named. A damaged file is never "repaired"
by scanning.

### 7.5 Streams

`/Length` is resolved (directly or indirectly) before the data is touched;
the stream ends where the length says, and the engine checks `endstream`
follows (whitespace allowed). A missing or wrong length is `PDE_SYNTAX`
naming the object.

## 8. Pages

The page tree is walked with an explicit stack (`PD_MAXDEPTH`), cycles caught
by a visited set of object numbers. The engine keeps a **page table** claim:
per page 8 bytes (page object number, generation, flags, inherited-attribute
source) up to `PD_MAXPAGES` = 1024; inherited `/MediaBox` `/CropBox`
`/Rotate` `/Resources` are resolved per page on demand, never stored for all
pages. Page identity in PageDesk is the page object's number and generation
in the source; a page PageDesk adds or duplicates has a PageDesk page id
(13.2).

## 9. Navigation, view and text

### 9.1 Zoom

Steps: 25, 33, 50, 67, 75, 100, 125, 150, 200, 300, 400 percent; Fit width and
Fit page compute an arbitrary percent clamped to 25..400. A view is never a
whole-page bitmap: the renderer draws only what the canvas shows (10.4).

### 9.2 Text geometry

The renderer's text pass can run without drawing (`PDR_TEXTMAP`), emitting
per glyph `(code, unicode-ish Latin byte, quad in pt16)` into a caller's
buffer in pieces. Selection and search consume these pieces and do not keep
a page's map after they are done with it. Reading order is content-stream
order; this is documented as the limit.

### 9.3 Search

Case-insensitive Basic Latin over the text map, page by page, resumable at
(page, glyph index) and cancellable between pages and between pieces; a match
spanning pieces is found (the matcher carries its state).

## 10. Rendering (`PDREND`)

### 10.1 Pipeline

Content -> interpreter (graphics state stack `PD_MAXGSTACK` = 16) ->
device-space display list (DL) in a claim (`PD_DLBYTES` default 20 KiB) ->
band rasteriser -> `OSAPI_GFX_BLIT1` (1bpp adapters) or `OSAPI_GFX_BLIT4`
(VGA/EGA) per band.

### 10.2 The display list overflows correctly

A band is put on the glass OPAQUE, and the framebuffer cannot be read back,
so a band must be composed from ALL of the page's content that touches it
before it is blitted. When the DL fills, the renderer therefore narrows the
vertical range rather than resuming: it rasterises the bands the DL covers
completely, then RE-SCANS the content stream from the start (graphics state
reset) keeping only items that intersect the next, smaller range of rows,
halving the range again on a further overflow down to one band. Every band is
drawn exactly once, from every item that touches it, in content order.
Nothing is dropped; a page whose single band overflows the DL is drawn with
what fits and marked Incomplete rendering (`PDF_INC_LIMIT`). (DECISIONS D4.)

### 10.3 Bands

A band is `PD_BANDROWS` = 16 rows of the canvas width at the adapter's depth:
1bpp `ceil(w/8)` bytes a row, 4bpp `ceil(w/2)`. One band claim, sized from
the live canvas width.

### 10.4 What is drawn

Only the intersection of the page rectangle and the canvas's visible part,
clipped to the window's clip region (the kernel's, armed per band draw). The
area outside the page is the desk pattern.

### 10.5 Colour

VGA/EGA: DeviceGray/RGB/CMYK to luminance and hue; a PDF black/white is the
palette's black/white, greys map to black, dark grey, light grey, white, and
saturated colours to the nearest of the 16 by the table in `pdr_colour`.
1bpp: luminance through a 4x4 ordered dither for fills and images; stroked
lines and text are black unless near white (luminance >= 240 of 255), so a
coloured or grey line, a coloured word and PageDesk's own red mark stay
visible on Hercules and CGA. No alpha.

### 10.6 Incomplete rendering

The interpreter records every unsupported operator, resource or feature it
skipped (a bitmask plus the first name). A page with any record shows
**Incomplete rendering - <feature>** in the status line and in Help >
Document info. No operation PageDesk offers depends on what was skipped, so
none is greyed for it (resolved by the lead, final pass, from 20.20.2 item 1):
- PageDesk never rewrites or removes page content. Marks, notes and text boxes
  are annotations drawn over the page.
- Rotate, move, duplicate and delete edit the page tree only.
- Extract and Insert copy the page's objects byte for byte (20.11, 20.16), so
  the skipped content travels with the page exactly as it was.
- There is no cover or redaction tool.
- What the interpretation does feed is the text map behind selection, Find and
  Highlight. On an Incomplete page these work on the text that was
  interpreted, and the status line already says what was not.

### 10.7 Slices

Interpretation and rasterisation run in slices of at most `PD_SLICE_OPS`
operators or one band, each ending at a resumable state (15.2).

### 10.8 Thumbnails

A thumbnail is the same pipeline at the zoom that fits the thumbnail cell,
drawn into a 1bpp cell buffer kept in an LRU of `PD_THUMBS` = 12 cells; a page
not yet drawn shows its number and "..." rather than a fake picture.

## 11. Fonts, encodings and assets

### 11.1 Substitution

Base-14 name -> family: `Helvetica*`/`Arial*` -> sans, `Times*` -> serif,
`Courier*` -> mono; `Bold`/`Oblique`/`Italic` in the name select the style.
Any other simple font draws with the sans family and marks the page
Incomplete rendering - font substituted. Placement always uses PDF widths
(`/Widths`, else 11.3), never the bitmap's width.

### 11.2 The font bank (`PDFB`)

Built by `tools/os88pdfont.py` from committed OFL fonts (Liberation or
Arimo/Tinos/Cousine sources plus notices under `apps/pagedesk/fonts/`) into
`PDRES`. Layout, all little-endian:

```
+0   'PDFB'  magic
+4   u16 version (1)
+6   u16 strike count n
+8   n x 16-byte strike records:
       +0 u8 family (0 sans, 1 serif, 2 mono)
       +1 u8 style  (bit 0 bold, bit 1 italic)
       +2 u8 pixel size (em height in device rows)
       +3 u8 ascent rows
       +4 u8 descent rows
       +5 u8 first code (32)
       +6 u8 last code (255)
       +7 u8 flags (bit 0: italic synthesised by the renderer)
       +8 u32 offset of the strike from the bank start
       +12 u32 strike length
strike: (last-first+1) x 4-byte glyph records
       +0 u8 bitmap width in pixels, +1 i8 left bearing,
       +2 u16 offset of the glyph's rows from the strike's bitmap base
     then the bitmap base: rows, MSB-first, ceil(width/8) bytes a row,
     ascent+descent rows a glyph
```

Pixel sizes 6, 7, 8, 9, 10, 12, 14, 16, 20, 24 for each family, regular and
bold; italic strikes where the budget allows, else synthesised. Codes are
Windows-1252 (11.4). The renderer picks the nearest size at or below the
device em height and loads strikes on demand into the glyph cache claim
(16 KiB, LRU by strike).

### 11.3 Width tables

Adobe's Core14 AFM advance widths for the 12 Latin faces, codes 32..255 in
Windows-1252 order, u16 per code in 1/1000 em, carried in `PDRES` with the
AFM licence notice. Fonts without `/Widths` use these.

### 11.4 Encodings

Internally every simple-font code becomes a Windows-1252 byte ("PD Latin").
`StandardEncoding`, `WinAnsiEncoding` and `MacRomanEncoding` map by table;
`/Differences` maps glyph names through a 256-entry Adobe glyph-name table for
the Windows-1252 repertoire. A code with no mapping draws the substitute box
and is searched as `?`.

### 11.5 UI type

The UI uses the OS's own system font through `font_run` and `os88type.inc`
for any proportional label; PageDesk bundles no UI font of its own, so a
missing font disk never matters (the system font is in the kernel).

### 11.6 Icons and cursors

16x16 1bpp masks with an optional 4bpp VGA variant, from pixel sources under
`apps/pagedesk/art/` converted by `tools/os88pdart.py`; the package icon and
the document glyph through `OS88_ICON16` and `OS88_DOCGLYPH8`.

## 12. Clipboard

Copy puts selected text as plain Windows-1252 bytes with CR LF between lines
through `OSAPI_CLIP_PUT` (32 KiB ceiling, refused whole above it, with the
reason).

## 13. Annotations and the edit model

### 13.1 The edit store

The primary keeps the document's changes in an **edit store** claim
(16 KiB): fixed 48-byte records (kind, flags, page id, rect in pt16, colour
and style, id, link to text) plus a text heap; a record's text is at most
1,024 bytes (refused beyond, with the typed text kept). Up to 256 records stay
in RAM; beyond that, records of pages not on screen spill to
`SYSTEM/APPDATA/PAGEDESK.SWP` and are reloaded by page. Existing annotations
of the source are NOT copied into the store; they are read per page from
`/Annots` and only enter the store when the user edits one (a copy-on-edit
record that names the source object).

### 13.1.1 The edit store's layout (DECISIONS D9)

The segment passed as `edits_seg` holds `edits_count` `struct pd_edit`
records at offset 0, then the text heap and the point heap. `text_off` and
`pts_off` are byte offsets from `edits_seg:0`. `pts_n` counts POINTS, each an
`(x, y)` pair of pt16 words in canonical page space (5.1):

| kind | points | other fields |
|---|---|---|
| Highlight | 4 per quad, in the order UL, UR, LL, LR (PDF 1.7 QuadPoints) | `rect` = the union |
| Ink, Sign | each stroke's points, strokes separated by a point whose x is `PD_PENUP` | `size` = stroke width x 2 (half points) |
| Line | 2 (start, end) | `align` = line ends: bit 0 arrow at start, bit 1 arrow at end; `style` = width in points |
| Rect, Ellipse, Area | 0 (`rect` is the frame) | `style` = border width in points |
| Note | 0 (`rect` is the icon box) | `text_*` = /Contents |
| Text | 0 (`rect` is the frame) | `style` = family<<4 \| bold \| italic<<1, `size` = points, `align` = /Q, `text_*` = /Contents |

`irt` is the parent note's `id` when the parent is a PageDesk record, or the
parent's SOURCE object number when `flags` has `PDEF_IRTSRC`. `colour` is a
`PDC_*` palette index (pdabi.h), mapped to RGB by the EGA 4-level triple.

### 13.2 Identity

Every PageDesk-created annotation has `/NM (PD-<8 hex>-<4 hex>)`: a document
nonce from the fingerprint and a counter. Page ids for added/duplicated pages
are `0x8000 | n`.

### 13.3 PDF representation

| tool | subtype | geometry | appearance |
|---|---|---|---|
| Note | `/Text` with `/Contents`, `/T` author when set, `/Name /Comment`, `/Open false` | 20 x 18 pt rect at the click | the speech-bubble icon as a Form XObject |
| Highlight | `/Highlight` with `/QuadPoints` per line in the PDF 1.7 order (x1 y1 x2 y2 x3 y3 x4 y4 = upper-left, upper-right, lower-left, lower-right) | the selection's quads | filled quads in `/C` with `/BM /Multiply` in an ExtGState (PDF 1.4) |
| Area mark | `/Square`, `/IT`-free, labelled "Area mark" in the UI | the dragged rect | dashed or solid border |
| Text box | `/FreeText` with `/DA (/Helv 12 Tf 0 0 1 rg)`, `/Q` 0/1/2, `/Contents` | the frame | the wrapped text in Helvetica/Times/Courier with a `/Font` resource of `/Type1 /BaseFont` and `/Encoding /WinAnsiEncoding` |
| Ink and Sign | `/Ink` with `/InkList`, `/BS /W` | simplified points | stroked paths |
| Line/Arrow | `/Line` with `/L` and `/LE [/None /OpenArrow]` | two points | stroked path with the head drawn |
| Rect, Ellipse | `/Square`, `/Circle` | the frame | stroked path (four Beziers for the ellipse) |

Every created annotation carries `/P`, `/F 4` (print), `/M` (date from the RTC
when present, else omitted) and `/AP /N`.

### 13.4 Review state

Resolved/reopened is a **reply annotation**: `/Text` with `/IRT` to the note,
`/State /Completed` or `/None`, `/StateModel /Review`, `/F 32` (no view) - PDF
1.5 (PDF 1.7 12.5.6.4). Other readers show the note's human text unchanged.

### 13.5 Versions

The writer computes the effective version from what it emits: highlight
blend modes need 1.4, review states 1.5. When the source's header version is
lower, an incremental save adds `/Version /1.5` to an updated catalog (PDF 1.4
7.7.2; a 1.3 reader ignores the key and still sees every annotation). A fresh
document is written `%PDF-1.5`.

### 13.6 Undo

A bounded command history of 64 commands (deltas with minimal old state,
never a PDF copy or a bitmap), typing and drags coalesced; page operations
are commands; truncation of the oldest is announced. Undo back to the save
point clears dirty.

### 13.7 Page operations

Reorder, rotate (`/Rotate` in the new page object), duplicate (a new page
object sharing `/Contents` and `/Resources` by reference, with every
annotation deep-copied to new objects with new `/NM` and `/P`, popup and
`/IRT` links remapped), insert blank (`/MediaBox` of the current page,
empty contents), delete (with confirmation; the last page cannot be deleted).
The new page order is a new `/Pages` tree written by the save (a single flat
`/Kids` array, inherited attributes materialised on each moved page).

### 13.8 Extract and insert

Extraction writes a **fresh** document (14.6) by graph-copy: for each chosen
page, every object reachable from it except `/Parent` is copied with new
numbers; resources are de-duplicated by source object number. Insert from
another PDF graph-copies into the current document's save with fresh numbers
above `/Size` and resource names unchanged (they are per-page scoped).

### 13.9 Honest labels

Delete page: "Removes the page from the saved document's page list. The
earlier bytes stay in the file." Cover: "Draws a box over the page. The text
under it is still in the file and can still be found." Sign: "Drawn signature.
This is not a certificate signature."

## 14. Saving

### 14.1 Save As only writes a new name

The source is never opened for writing. Save As with the source's own name
writes the temp file and then asks to replace it (14.3).

### 14.2 Incremental save

1. Stream-copy every source byte to `PDSAVE.$$$` in the target folder.
2. Append new and changed objects, each `n g obj ... endobj`, starting on a
   new line; new objects numbered from the source's newest `/Size`.
3. Append an `xref` section of the new objects only (subsections by run),
   20-byte entries, then `trailer` with `/Size` (max object + 1), `/Root`,
   `/Info` when present, `/ID` (the source's first element, a new second),
   `/Prev` (the source's newest `startxref`), then `startxref` and `%%EOF`.
4. Verify by re-reading the new trailer and three random new objects through
   `PDENG` before the rename.

With no new or changed objects (a Save As of an unedited document) the
output is a byte copy of the source: steps 2 and 3 are skipped and step 4
checks the length. The writer must not refuse it (lead's look, 2026-11-05:
PDWRT answered `PDE_RANGE`, its "nothing to save", on MARKUP.PDF - wave 2).

### 14.3 Finalisation

`PDSAVE.$$$` -> (target exists: confirm, delete target) -> rename. The
filesystem has no atomic replace (SPEC.md 18.4), so the documented guarantee
is: a failure before the delete leaves the target untouched; a failure
between delete and rename leaves the complete new file as `PDSAVE.$$$`, which
PageDesk offers to finish renaming at the first document open of the next
session (20.17.6). Disk full, write
protect and short writes are caught on every write; the temp file is deleted
on failure and the document stays dirty.

### 14.4 Source identity

Before step 1 the fingerprint (6.2) is re-read; a moved source refuses
`PDE_CHANGED`.

### 14.5 What the output keeps

Every untouched byte. Earlier revisions remain readable; the UI states that
incremental files keep earlier bytes and are not redacted.

### 14.6 Fresh documents

New and Extract write a complete PDF 1.5 file: catalog, flat page tree,
pages, contents, `/Helv` `/TiRo` `/Cour` WinAnsi font resources as needed,
annotations, classic xref, trailer with a new `/ID`.

### 14.7 Recovery journal

`SYSTEM/APPDATA/PAGEDESK.JRN` on the boot volume (when it is writable): a
header with the source's name and fingerprint, then the edit store's records
as they stand after the last burst of commands, CRC-16 each. The file is
rewritten whole half a second after the last command, not appended to
(20.17.2 says why and what it costs). Recovery is offered at open only when
the open source's fingerprint matches; replay stops at the first bad record.
Page operations are not journaled (20.17.1). A read-only volume disables the
journal with a status-line note. Closing a window with unsaved changes asks
Save / Discard / Cancel (20.17.8).

## 15. Responsiveness

### 15.1 No worker

PageDesk runs no worker task. Every long operation (open, render, search,
thumbnail, save, extract) is a **job**: a resumable state machine stepped by
the primary's `OSAPI_WM_ONWAKE` handler, one bounded slice per wake, each
slice re-posting `OSAPI_WM_WAKE` until done. Mouse and keyboard events
interleave with the slices in the event queue, so Escape and clicks are seen
between slices. File I/O is legal in every slice (UI task), and overlay and
module calls are legal (no worker restriction applies).

### 15.2 Slice budget

A slice does at most one disk transfer call or `PD_SLICE_OPS` content
operators or one band, aiming at <= 100 ms of CPU on the 8088. The draw lock
is taken per band, never across a slice.

### 15.3 Progress and cancel

A job longer than one second shows progress in the status line and, for file
transfers, through `OSAPI_FS_OP`. Escape cancels at the next slice boundary;
a cancelled job releases its claims and leaves the document as it was.

## 16. Memory

### 16.1 Ceiling

The assignment's planning ceiling is 176 KiB total. This contract budgets
**176 KiB idle** (document open, page shown, writer and overlay not resident)
and **224 KiB peak** (during a save, extract or dialog). On a 640 KiB XT with
kern_big about 534 KiB of heap is free (docs/KERNEL-MEMORY.md), so the peak
leaves well over the required 32 KiB unclaimed. PD-29 measures it; a change
that exceeds a row is a change to this table first.

**Measured (PD-29, `pagedeskxt` on MartyPC, 640 KiB, 2026-11-09 at
fef1fcbd0): the 176 KiB planning ceiling is NOT met.** PageDesk idles at
317.5 KiB of its own claims on VGA, and 357.5 KiB on CGA and Hercules, plus its
58 KiB instance:

- the two parts PDENG and PDREND, 107 KiB top-down;
- the overlay, 47.5 KiB;
- the writer's segment, 47 KiB;
- 9 KiB of workspace;
- the document block of 106 or 146 KiB. This holds the 16 KiB block cache,
  the arena and tables, the display list, the edit store and the one or two
  Flate slots D17 carves from what RAM is left.

After the session the desktop has 97.0 KiB free on VGA, 83.0 on CGA and 81.0
on Hercules. The 32 KiB reserve is held after every step on all three. During
Save As the lowest unclaimed figure falls below it on two of them:

| adapter | lowest unclaimed during Save As | with the purgeable caches shed |
|---|---|---|
| VGA | 32.0 KiB | 103.0 KiB |
| CGA | 18.0 KiB | 65.0 KiB |
| Hercules | 16.0 KiB | 65.0 KiB |

The kernel sheds those purgeable caches before it refuses a claim, so the row
asserts the reserve after each step, and during a step only with the caches
shed. D28 raised VGA's in-save trough from 5.5 KiB. The rows below are the
design budget; the claims as measured are the row's printout
(`python3 tools/os88test.py soak -v -k pagedeskxt`).

### 16.2 Claims

| claim | owner | size | when |
|---|---|---|---|
| primary image + bss | loader | <= 56 KiB | launch |
| `PDENG` image + bss | parts | <= 40 KiB measured target | launch |
| `PDREND` image + bss | parts | <= 40 KiB measured target | launch |
| exchange block | primary | 1 KiB | launch |
| block cache | PDENG | 16 KiB | open |
| object arena + xref tables + page table | PDENG | 8 + 3 + 8 KiB | open |
| display list + PDREND workspace | PDREND | 22 + 7 KiB (D13) | open |
| band | PDREND | 1.25-5 KiB by adapter | open |
| glyph cache | PDREND | 12 KiB | open |
| thumbnails | primary | 4 KiB | panel shown |
| edit store + undo | primary | 16 KiB | open |
| text/search scratch | primary | 2 KiB | search |
| private module stack | primary (every module verb runs on it, 20.15) | 2 KiB | open |
| `PDWRT` image + bss | parts, lazy | <= 46 KiB (D18; `PDW_CAP`) | save/extract/new, freed after |
| writer buffers | PDWRT | 18 KiB (4 KiB output + 4.7 KiB of page-op/edit tables at the tail + a ~1,100-object offset table); an extract 16 + /Size/128 + /Size/2048 KiB, at most 40: the bit maps sized from the source's `/Size` go between the output and the object table (D18, 20.11.4) | save/extract, freed after |
| overlay | parts, lazy | <= 48 KiB (D19; was 24) | dialog, inspector, menu command; freed when idle |

Failure of an optional claim (thumbnails, a larger cache) degrades quality;
failure of a required one refuses the open with the KB it wanted.

## 17. User interface

### 17.1 Layouts

The layout is chosen from the live content box (`OSAPI_WM_GEOM`), never from
the adapter kind:

| layout | when | rows of chrome | panels |
|---|---|---|---|
| FULL | width >= 600 and height >= 260 (Hercules and EGA content boxes measure 284-286) | toolbar + mode/tool bar + status | Pages/Outline sidebar and Notes & edits panel, each collapsible |
| MID | height >= 240 | same | panels default hidden, open as overlays |
| COMPACT | otherwise (CGA 640x200) | one combined bar + status | page chooser and notes as popovers/dialogs that fit the client area |

### 17.2 Bars

File/navigation bar: Open, Save As, Undo, Redo, Prev, page field `n/N`, Next,
zoom -, zoom field, zoom +, Fit menu, Find. Mode bar: Read, Annotate, Edit,
Pages; then the mode's tools (Annotate: Select, Highlight, Note, Text, Mark,
More -> Ink, Line, Rect, Ellipse, Sign). COMPACT folds the file bar into the
menu bar's commands and shows the mode bar only. Print is not shown (1.1).

### 17.3 Notes & edits panel

A virtual list over the edit store and the page's source annotations (no card
per note in memory): filter This page / All pages, a card per visible item,
Resolve/Reopen on notes, jump-to on click, and Text properties (family, size,
bold, italic, colour, align, Delete, Done) for a selected text box.

### 17.4 Monochrome marks

On 1bpp adapters a highlight is drawn as an underline plus a dotted frame
(preference: underline only, frame only, hatched), never a solid fill, so
letters stay legible; selection is XOR inversion and is never saved.

### 17.5 Keyboard

Ctrl+O Open, Ctrl+N New, Ctrl+S Save As, Ctrl+Z Undo, Ctrl+Y Redo, Ctrl+F
Find, F3 / Shift+F3 next/previous, PgUp/PgDn page, Home/End first/last page,
Ctrl++ / Ctrl+- zoom, Del delete selection, Esc close popover then deselect,
Tab/Shift+Tab focus. Menus show the bindings. A text field owns every key
while it has focus except Esc and Enter.

### 17.6 Assets

Toolbar icons 16x16 (open, save, undo, redo, prev, next, zoom out/in, find,
select, highlight, note, text, mark, ink, line, rect, ellipse, sign, more),
the app icon, the `.PDF` document glyph, cursors (hand, crosshair, I-beam via
`OSAPI_WM_CURSOR` where the OS has the shape).

## 18. Fixtures and independent checks

### 18.1 Samples

`tools/os88pdfgen.py` writes, deterministically, the shipping samples into
`build/pdfix/` and the committed copies under `apps/pagedesk/samples/`:
`HELLO.PDF`, `XTGUIDE.PDF` (12 pages; page 3 is the mockups' "Connecting your
XT"), `MARKUP.PDF`, `PAGES.PDF`, `MONOIMG.PDF`, `LATIN.PDF`, `FLATE.PDF`,
`ACROFORM.PDF`, and `MANIFEST.TXT` (pages, size, SHA-256, fonts, filters,
expected capability, named tasks).

### 18.2 Stress and structure

Generated, not shipped: 64 pages, > 640 KiB, a > 64 KiB content stream,
10,000 sparse objects, indirect lengths, content arrays, inherited resources,
3-revision incremental chains, rotated and cropped nonzero-origin pages.

### 18.3 Negative corpus

`tests/pdfneg/` (generated into `build/pdfneg/`), labelled invalid: truncated
header/xref/stream, wrong `/Length`, cyclic page and resource trees, deep
arrays, huge numbers, oversized filter output, broken `/Prev` chains, invalid
annotations. Never mixed with the samples.

### 18.4 Independent tools

`qpdf --check`, `mutool draw` (MuPDF) and `pdftoppm` (Poppler) on the host,
wrapped by `tools/os88pdfcheck.py`; a missing tool skips its check with a
message and never passes it.

## 19. Tests

Host checks run in `make pagedesk` behind stamps. Emulator rows are named in
the plan per story and run once, in the final pass. Rows land in
`tests/suite.py` as `pagedesk*`.

## 20. Reserved subsections

Agents add detail under the subsection numbers the lead assigns in the
plan's agent table and nowhere else.

### 20.1 Fixtures and independent checks (`fix`)

**20.1.1 Regenerating.** Everything here is produced by
`tools/os88pdfgen.py` (Python 3 standard library only, deterministic: no clock,
no randomness, a fixed `/ID`, and even the one FlateDecode stream is written by
the generator's own fixed-Huffman deflater, so the bytes do not depend on the
host's zlib).

| command | writes |
|---|---|
| `python3 tools/os88pdfgen.py apps/pagedesk/samples` | the eight shipping samples and `MANIFEST.TXT` (committed; 64,364 bytes in all) |
| `python3 tools/os88pdfgen.py build/pdfix` | the same, for the build tree (18.1) |
| `python3 tools/os88pdfgen.py --stress build/pdfstress` | ten stress files and `STRESS.TXT` (never committed, never shipped) |
| `python3 tools/os88pdfgen.py --negative build/pdfneg` | 53 deliberately invalid or unsupported files and `README.TXT` (only the README is committed, as `tests/pdfneg/README.TXT`) |

`tests/unit/t_pdfix.py` regenerates the samples and requires byte equality with
the committed copies, the manifest to match each file's size and SHA-256, the
negative README to be what the generator writes, `qpdf --check` to accept
every sample, and (`--stress`, about 0.45 s more, so not in the fast tier) the
stress set to keep 20.1.3's size and structure claims
(over 640 KiB, a stream over 64 KiB, 10,000 sparse objects, every stress file
within `PD_MAXSUBSEC` xref subsections, three revisions, 256 annotations, 64
pages, rotated nonzero-origin boxes).

Font widths: `/Widths` are Adobe's Core 14 AFM advance widths (the files
`tools/os88pdfont.py` pins; notice in `apps/pagedesk/fonts/AFM-NOTICE.txt`),
every code an encoding names, not only the ones a sample draws; a code whose
glyph the AFM lacks (MacRoman's Omega, pi and the like) is 0.

**20.1.2 The samples (18.1).** Every sample except `FLATE.PDF` is asserted, by
the generator about its own bytes, to stay inside the 1.3 profile: classic
xref, base-14 Type1 fonts with `/Widths`, no filter beyond ASCIIHex and
RunLength, 1- and 8-bit DeviceGray images, no object or xref streams, no
encryption; every `/BaseFont` is checked against the base-14 list and every
xref offset against its `N 0 obj`. Each file states its own version (`%PDF-1.3`;
`FLATE.PDF` is 1.2, `ACROFORM.PDF` 1.4).

| file | pages | what | XT capability |
|---|---|---|---|
| `HELLO.PDF` | 1 | text, a frame, a triangle | supported |
| `XTGUIDE.PDF` | 12 | page 3 is the mockups' "03 Connecting your XT" (two IBM XT drawings as vector paths, "Null-modem serial cable" arrow, "Before you connect" steps, "Recommended settings" table, footer `GETTING STARTED  3`); cover, contents, pinout, maintenance, diagrams, troubleshooting and more; `/Outlines` with 6 sections and 5 children; `/Link` annotations (`/Dest` and `/A /GoTo`) on the contents page and page 4; `same baud rate` occurs once, on page 3 | supported |
| `MARKUP.PDF` | 2 | drawn fields, checkboxes, plan and shape areas, no AcroForm | supported |
| `PAGES.PDF` | 8 | portrait, landscape, CropBox, `/Rotate` 90/180/270, A4, nonzero-origin MediaBox | supported |
| `MONOIMG.PDF` | 1 | 1-bit raw image, its `/Decode [1 0]` twin, 8-bit ASCIIHex gradient, RunLength `/ImageMask` | supported |
| `LATIN.PDF` | 1 | literal-string escapes, hex strings, WinAnsi, MacRoman, Standard (no `/Encoding`), `/Differences`, `TJ Tc Tw Tz Ts Tr TL T* ' "` | supported |
| `FLATE.PDF` | 1 | content compressed with FlateDecode | refused `PDE_FLATE` (Enhanced PD-25 draws it) |
| `ACROFORM.PDF` | 1 | AcroForm: two text fields under one parent, two checkboxes, `/AP` on all, `/NeedAppearances false` | display supported; editing is Enhanced (PD-26) and must be refused, never silently applied |

`MANIFEST.TXT` lists per file the page count, byte size, SHA-256, version,
fonts, filters, expected capability, and `task <name> <json>` lines: the named
editing exercises as machine-readable operations (`op` of `find`, `highlight`,
`note`, `text`, `line`, `ink`, `move-page`, `rotate`, `duplicate`,
`insert-blank`, `delete`, `extract`, `insert-from`, `save-as`, ...).

**20.1.3 Stress (18.2).** `STRESS64.PDF` (64 pages under an 8x8 nested tree),
`BIGFILE.PDF` (721,926 bytes), `LONGSTRM.PDF` (one 85 KB content stream),
`SPARSE.PDF` (10,000 filler objects in 200 runs of 50 numbers at 1000+320k,
the highest 64729, the page, content and font at 61000-61002: 203 xref
subsections, inside 7.3's `PD_MAXSUBSEC` = 256, every number within the
1..65535 of DECISIONS D8, so it must open; one subsection per object is the
negative `xref_subsec_300.pdf`, a number above 65535 the negative
`objnum_70000.pdf`),
`INDIRLEN.PDF` (`/Length N 0 R`, the length object after the stream),
`CONTARR.PDF` (`/Contents` as three streams, `q` and `Q` in different ones),
`INHERIT.PDF` (resources, boxes and `/Rotate` through three `/Pages` levels),
`ROTCROP.PDF` (90/180/270, nonzero-origin MediaBox and CropBox),
`REVCHAIN.PDF` (three revisions; revisions 2 and 3 redefine a content stream
and an annotation, revision 3 adds a page and redefines `/Pages`: the final
state is 3 pages, page 1 reading `REVISION 3`, one Text note `note from
revision 3`), `ANNOTS256.PDF` (256 annotations, 7 subtypes, each with `/AP`).

**20.1.4 Negative corpus (18.3).** `tests/pdfneg/README.TXT` is the table: file,
layer (`open` or `render`), expected `PDE_*`, reason. The files are named for
their defect, and each carries ONLY that defect: every other byte, offset,
`/Length` and xref entry is right (the unsupported-filter cases carry real
Flate, LZW, five-stage and JPEG data), so a reader that refuses it for any
other reason is wrong. Layers are `open`, `stream` (`PDV_ENG_STRM_OPEN` of
the page content), `pageinfo` (`PDV_ENG_PAGEINFO`) and `render`. The
expectations are the fixture author's reading of 1.3 and 3.6
and give way to the engine's 20.2 where they differ. A case is not finished
when the code is right: the source file must be byte-identical afterwards, no
claim left open, and the next valid file must open. Independent readers
repair some of these (`qpdf --check` rebuilds the table of `trunc_xref.pdf`
and `xref_wrong_offset.pdf` and answers with warnings, not an error): that is
the point of them, PageDesk refuses.

**20.1.5 The checker (18.4).** `tools/os88pdfcheck.py`, over `qpdf`, `mutool`
and `pdftoppm`, sharing no code with PageDesk or the generator. Tools are
found on `PATH` only, so hiding one proves the skip path; a missing tool
prints `SKIP` and exits 3: it never passes. Exit codes: 0 pass, 1 fail, 2
usage, 3 SKIP.

| subcommand | does |
|---|---|
| `validate FILE...` | `qpdf --check`; PASS only when clean (`--warn-ok` tolerates warnings) |
| `render FILE PAGE OUT [--dpi N] [--gray]` | `mutool draw`, else `pdftoppm`; OUT's extension picks PNG, PGM or PBM |
| `annots FILE PAGE` | JSON list for the page: `subtype`, `rect`, `quadpoints`, `contents`, `nm`, `irt`, `state`, `statemodel`, `flags`, `author`, `has_ap` (true only when `/AP /N` is a stream, or a state dictionary of streams), `inklist`, `line` (from `qpdf --json=2`, so the latest revision wins; when qpdf had to repair the file its warnings go to stderr as `WARN`, because the JSON then describes the repaired file) |
| `compare A.pgm B.pgm [--tol N] [--frac F]` | PASS when at most F of the pixels differ by more than N (defaults 16 and 0.01); a size mismatch fails |
| `text FILE [PAGE]` | `mutool draw -F txt`, to confirm text is findable |

### 20.2 The engine (`eng`)

`PDENG.MOD` (apps/pagedesk/pdeng.asm + pdeng.c and the pde*.c files it includes,
one translation unit) is the PDF reader segment. This section is its contract
beyond 3, 6, 7 and 8: the numbers it enforces, the exact behaviour of every
verb, the error each failure gives and the places it differs from those
sections. Sizes are measured, not estimated (20.2.1).

#### 20.2.1 Build, size and checks

`make pdeng` compiles `pdeng.c` with smlrcc, lowers and peepholes it with
`tools/cc8086.py` (96-byte frame gate, `&local` refusal), assembles
`pdeng.asm` with `nasm -f bin -w+error`, `cpu 8086`, and prints the size. It
then builds the host harness (`hosttest/pde_test.c`, `-DPD_HOST`, pdmath.c,
pdhost.c) and runs `hosttest/pde_check.py`. `make pdefuzz` runs that and then
the sanitized mutation pass below.

| item | bytes |
|---|---|
| `PDENG.MOD` image (code + data, the file) | 27,466 |
| bss (zeroed by the door on first entry) | 8,972 |
| image + bss | 36,438 (target 40,960; cap 57,344) |

(27,010 + 8,942 at the implementer's DONE; verify-eng's fixes, D11 and
feat/pagedesk's faster `pdmath.inc` took it to the figures above.)

The bss is: the tail/fingerprint buffer 1,024; the subsection table 2,560
(256 x 10); six stream contexts 6 x 424; the object lexer's 256-byte buffer;
the token (270), value (270) and verb structs; the block-cache tables; the
verbs' static exchange copies. Nothing is on the stack but scalars (largest
frame 20 bytes), because SS != DS.

Checks, none of them an emulator row:

* `hosttest/pde_check.py` - the engine's own fixtures
  (`hosttest/pde_fixtures.py`: filter pipelines, three-revision update with a
  freed object and a generation mismatch, escapes and numbers through
  DICT_GET, outline, annotations, resources, boxes, 33, 190 and 240
  one-object subsections, and about 50 negatives whose xref is correct so the
  engine reaches the defect under test) with explicit expectations; and the
  shared samples, stress set and negative corpus (20.1) compared with qpdf:
  page count, page object numbers, inherited MediaBox/CropBox/Rotate, the CRC
  of every content stream's decoded bytes, every object's offset against
  `qpdf --show-xref`, at cluster sizes of 1, 2 and 8 sectors; a token
  count/operator hash against an independent content lexer written in Python;
  failure injection at every READ_SEQ call (`pde_test faults`: an I/O error,
  never a crash, and the file opens afterwards); re-INIT and reuse after
  each failure (`pde_test multi`).
* `hosttest/pde_fuzz.c` (`make pdefuzz`, verify-eng) - the hostile-input
  pass: the engine built with AddressSanitizer and UBSan, every verb driven
  over mutations of that corpus (a truncation at every 997th byte; bit flips,
  syntax bytes, `/Length` +/-1, 0, huge, negative, real and indirect, xref
  entry offsets and `startxref` shifted, subsection headers, splices,
  nesting and filter-chain bombs, half of the moved cases with the newest
  xref repaired so they reach the parser), one READ_SEQ failure injected in
  one case of eight, all on ONE engine re-INITed between cases. A case fails
  on a crash, a run over 20 s (a hang), a canary moved after any claim or
  around a destination (the claims live in one host array ASan cannot
  police), an unterminated text field, `ARENA_MARK` not 0 after `CLOSE`, or
  the pristine file opening differently after the mutations (every 64th
  case). At `PDEFUZZ_N=100` it is 16,486 cases in about 20 s, clean.
* `hosttest/pde_boot.py` - the module itself, on the TARGET build: a boot
  floppy carries `PDENG.MOD`, a kernel stub (READ_SEQ with its contract
  enforced: capacity and offset cluster multiples, else it prints
  `!CONTRACT` and stops), and a PDF; QEMU boots it; a harness compiled by the
  same toolchain drives the verbs through the exchange block and prints, over
  COM1, the same lines as `pde_test dump` (pages, trailer, fingerprint,
  content CRCs, token counts, every object's extent and DICT_GET, the whole
  outline, every annotation); the script diffs the two. It proves the 16-bit
  `int`, SmallerC's code and `pdmath.inc` compute what the host build
  computed. QEMU is a 386-class CPU: it says nothing about speed or about an
  8088's behaviour beyond what `cpu 8086` and the cc8086 gate already hold.
  Files over 345,600 bytes do not fit the harness's 640 KiB map and run on
  the host only.

#### 20.2.2 Claims and the limits the engine adds

The engine uses only the segments `PDV_ENG_INIT` gave it (D2: INIT may be
called again, only with no document open; a claim of 0 is "none"). `OPEN`
refuses with `PDE_NOMEM` unless `cache_kb >= 8`, `arena_kb >= 8`,
`tables_kb >= 11` and the three segments are nonzero.

| claim | layout |
|---|---|
| exchange block | offset 0: the verb's struct (3.2); every verb copies it in and out whole |
| cache | `nblk` blocks of `blk` bytes (20.2.3) |
| arena | 8 KiB bitmap, one bit per object number, live only during `OPEN`'s page-tree walk (the visited set of 8); released when the walk ends. `ARENA_MARK` returns the claim's current top (0 when idle), `ARENA_REL` sets it back; no other verb allocates from it |
| tables | bytes 0-8191: the page table, 8 per page (object number, generation, flags 0, reserved 0), 1,024 pages; 8192-11263: the resolved-offset cache, 512 direct-mapped entries of 6 (tag = object number, 0xFFFF empty, offset lo, hi; hi 0xFFFF = free) |

Limits not in `pdabi.h`: 32 cache blocks; 6 open streams (a page, 4 nested
forms, an image); a decoded stream is cut at 1 MiB (`PDE_LIMIT`), and the
bytes EVERY stage of its filter chain produces at 4 MiB (`PDE_LIMIT`, 20.2.5);
16 page-tree
nodes per `OPEN` slice; 8 indirect hops before `PDE_CYCLE`; 32 exact xref
subsections per revision (20.2.3); object numbers above 65,535 are
`PDE_LIMIT` (the ABI carries them in 16 bits); a file of 2 GiB or more is
`PDE_LIMIT`; a page tree visits at most 2,080 nodes (`PDE_LIMIT`).

#### 20.2.3 The file layer and the cross-reference

`blk` = `max(2048, cluster bytes)`, a cluster multiple always (READ_SEQ's
rule), so a cluster over 32 KiB or one that leaves fewer than two blocks in the
cache is `PDE_NOMEM` (6.1's "4 KiB windows of a bigger cluster" cannot be
expressed: the capacity must be a cluster multiple). `nblk = cache_kb /
(blk / 1024)`, at most 32, LRU, filled by one READ_SEQ call per block from the
engine's own 16-byte cursor, re-seeded only when the next block is not the one
the cursor stands at. The directory is read at `OPEN` (`os88_file_find`
by name) for the size and the compressed bit, and again by `FINGERPRINT`; a CZ-packed file, or one
whose first two bytes are `CZ`, is refused `PDE_SYNTAX` with
`err_detail = 1` (a packed PDF would be read as its wrapper). The name is
matched case-insensitively and READ_SEQ is then handed the directory's own
spelling of it; a name not in the current folder is `PDE_IO`.

The cross-reference follows 7.3 with one extension: each revision keeps at most
32 subsection records; the 33rd keeps every other record as a CHECKPOINT (the
subsections between two checkpoints are found again by reading their headers
off the file). A lookup in a thinned revision takes the last record whose
first object is at most the one asked for and reads the headers after it
until one starts past that object or the table ends (20.22.3.7: the walk is
bounded by the table, so a record needs no count of what it stands for). The whole chain may name at most `PD_MAXSUBSEC`
(256) subsections: the 257th is `PDE_LIMIT` (D10c). A thinned revision must be in ascending order of
first object throughout: a revision of 33 or more subsections with one out of
order, before the thinning or after it, is `PDE_LIMIT`. A revision with 32 or
fewer subsections may be in any order. A subsection that reaches past object
65,535 (`first + count > 65,536`) is `PDE_LIMIT` (D8).

Entries are checked byte by byte (ten digits, space, five digits, space, `n`
or `f`, a two-byte end of line: space+CR, space+LF or CR+LF); an in-use
offset under 9 or at or past the end of the file, a generation over 65,535, a
bad type or an object whose header does not read `n g obj` is `PDE_XREF` with
`err_obj` the object and `err_detail` the revision whose entry it is (0 =
newest; 0 also when the entry came from the resolved-offset cache, which does
not keep the revision). The generation a reference is matched against is the
one in the object's `n g obj` header; the entry's own generation field is
checked for its range only (20.2.8). A reference
whose generation is not the object's, a free entry and an object in no
subsection are all a null (`ob_null`; OBJ_FIND answers kind `PDK_NULL`,
offset and length 0).

`startxref` is searched in the last 1,024 bytes (the last occurrence wins);
none is `PDE_TRUNC` with `err_detail = 5`; a number over ten digits, zero, or
at or past the end of the file is `PDE_XREF`. The header must be at offset 0
(`%PDF-d.d`; anything else `PDE_SYNTAX` with `err_detail = 2`, a file under 9
bytes `PDE_TRUNC`). A `/Prev` that is a revision already read is `PDE_XREF`
(the revision named in `err_detail`), the ninth revision is `PDE_LIMIT`. At the
offset the engine expects `xref`; `n g obj` there whose dictionary says
`/Type /XRef` is `PDE_XREFSTM`, any other thing `PDE_XREF`. A trailer with
`/Encrypt` is `PDE_ENCRYPT` in any revision; no `/Root` is `PDE_XREF`
(`err_detail = 4`); a hybrid file's `/XRefStm` is ignored and the classic table
read (1.3). The newest revision's `/Size`, `/Root`, `/Info` and first `/ID`
element win, an older revision filling only what is missing.

#### 20.2.4 Objects

There is no object tree. The parser works in place on the file: it tokenises,
skips what it does not need, and answers a query by walking the one dictionary
or array concerned, so the arena is not where objects live and `OPEN`'s
memory is the 8 KiB bitmap above plus the tables. Limits are those of
`pdabi.h`: a token over 1,023 bytes `PDE_LIMIT`; a name over 127 `PDE_LIMIT`;
nesting over 32 `PDE_DEPTH`; an array or dictionary of over 4,096 items
`PDE_LIMIT` where the engine counts them; integers of 2^31 or more are flagged
and a number that does not fit 16.16 is saturated and answers `PDE_RANGE`.
Strings are lexed in pieces of at most 255 bytes (literal escapes, octal,
line continuation, balanced parentheses, EOL normalised to LF; hex with an
odd last digit padded with 0); a value's text is the first 255 bytes and its
`len` the true length (to 65,535).

#### 20.2.5 Streams and TOKEN

`STRM_OPEN` pushes onto a stack of 6 open streams; `STRM_READ`, `TOKEN` and
`STRM_CLOSE` address the top, so a content interpreter opens a Form XObject
while its caller's stream is still being read. `/Length` is read first
(direct, or a reference to an integer object; a real, a negative number, a
missing key or a non-integer is `PDE_SYNTAX`); data starting past the end of
the file is `PDE_TRUNC`; `endstream` must follow the data after optional white
space, else `PDE_SYNTAX` (so a /Length too short or too long is caught here,
and at `OBJ_FIND`/`RAW_COPY`/`STRM_OPEN`, never at `OPEN`: the engine does not
read page content while opening). The end-of-line after `stream` is LF or
CR+LF; a lone CR is `PDE_SYNTAX`. `/F` (an external file) is `PDE_FILTER`.
Filters: `ASCIIHexDecode`/`AHx` and `RunLengthDecode`/`RL`, alone or in
a chain of at most 4 (`/Filter` a name, an array, or a reference to either);
`/DecodeParms` is ignored; `FlateDecode`/`Fl` is `PDE_FLATE`; every other
name (`LZW`, `ASCII85`, `DCT`, `CCITT`, `JBIG2`, `JPX`, `Crypt`, anything
unknown) is `PDE_FILTER`; a fifth stage is `PDE_LIMIT`. A hex stage ends at
`>` or the end of data; a run-length stage at 128 or the end of data; a bad
hex digit is `PDE_SYNTAX` at the read. Two bounds hold a stream's decoding
(PD-28): the decoded output is cut at 1 MiB, and every byte ANY stage of the
chain produces counts against 4 MiB - a lower stage that feeds only white
space to an `ASCIIHex` above it (`[/RL /AHx]`, 128 spaces a raw pair) produces
no output at all and would otherwise be unbounded work inside one `TOKEN` or
`STRM_READ`. Either is `PDE_LIMIT` at the read, and the stream stays at its
end afterwards. `declen` is the length for an
unfiltered stream, 0xFFFFFFFF for a filtered one, and `filters` the `PDFL_`
bits.

`TOKEN` returns the next token of the top stream: `kind`, `num` (16.16) for a
number, `text`/`len` for a name (without the slash), operator, keyword
(`true`, `false` and `null` have their own kinds) or a string PIECE (at most
255 bytes, `more = 1` while the string continues; the next call returns the
next piece with kind `PDT_STR`). There are no references in a content stream.
A number that does not fit 16.16 is delivered saturated with the call
answering `PDE_RANGE` (CF = 1, the token valid; the renderer marks
`PDF_INC_RANGE` and goes on). Array/dictionary nesting beyond 32 is `PDE_DEPTH`.
After the operator `ID` the engine consumes exactly one white-space byte; the
inline image's bytes are then read with `STRM_READ`, which first drains the
lexer's lookahead, and the renderer scans for `EI` itself. End of data is
kind `PDT_EOF` with CF = 0, unless the decoder stopped on an error, which is
returned instead. `STRM_READ(BX = dest seg, CX = dest off, DX = max)` answers
the bytes copied (0 at the end), up to 0xFFF0 and never across the end of the
destination segment (`PDE_RANGE`).

#### 20.2.6 The verbs

* `INIT` - as 20.2.2. Always succeeds (a re-INIT closes any document).
* `OPEN` - 4 slices: (1) directory, header, `startxref`; (2) one revision per
  slice; (3) the catalog and the first page-tree node; (4) up to 16 page-tree
  nodes per slice. Each returns **CF = 0, AX = `PDE_BUSY`** (D11: more to do
  is not a failure) with `state` set (2, 3 or 4) until the last, which returns
  CF = 0, AX = `PDE_OK`. A call whose `state` is not the one the
  last BUSY returned is `PDE_RANGE`. On any failure `err_obj` (the object or
  0) and `err_detail` are written, the document is closed, the file layer
  forgets the file and the engine is ready for the next `OPEN`; calling
  `CLOSE` instead of finishing is the cancel. Outputs: `npages` (1-1,024; none
  is `PDE_SYNTAX`, `err_detail = 3`, D10b), `nrev`, `version` (header x10),
  `flags`: `PDO_OUTLINE` (catalog has `/Outlines`), `PDO_ACROFORM`,
  `PDO_TAGGED` (`/MarkInfo` present), `PDO_READONLY` (catalog `/Perms`, or
  the AcroForm's `/SigFlags` bit 0; a signature field's own `/V` is not
  searched), `size`. A node is a page when it has no `/Kids`; a node with
  `/Type` other than `/Page`/`/Pages` is `PDE_SYNTAX`; a node reached twice
  (a cycle, or a page shared by two parents) is `PDE_CYCLE`; a `/Parent` that
  is not the node it hangs from is tolerated unless following it loops
  (`PDE_CYCLE`); depth over 32 is `PDE_DEPTH`; a kid that is a free or
  absent object, or has the wrong generation, is skipped.
* `CLOSE` - closes the document and the streams; always OK; idempotent.
* `PAGEINFO(BX = index)` - `objnum`/`gen`, `media`, `crop` (= media when
  absent), `rotate` (normalised to 0/90/180/270; a value that is not a
  multiple of 90 is `PDE_RANGE`), `nannots` (the elements of `/Annots`, of any
  kind), `flags`: `PDP_NOCONTENT` when `/Contents` is absent or null;
  `PDP_HASLINKS` is never set (finding out costs one object read per
  annotation: use `ANNOTS`). Boxes are inherited through `/Parent` (a loop or
  32 hops is `PDE_DEPTH`), reversed corners are swapped, an absent MediaBox is
  Letter, a zero-area box or any coordinate outside +/-2,047.9 pt is
  `PDE_RANGE`. Index past the end: `PDE_RANGE`.
* `OBJ_FIND` - a free, absent or wrong-generation object is `PDK_NULL`,
  offset/length 0, CF = 0. Otherwise `kind` (`PDK_STRM` when a dictionary is
  followed by `stream`), `offset` of the digits of `n g obj`, `length` through
  `endobj`; a missing `endobj` or a stream whose length is wrong is
  `PDE_SYNTAX`/`PDE_TRUNC`.
* `DICT_GET` - `objnum`/`gen` 0/0 is the newest trailer. The path is up to
  three names; `"[n]"` indexes an array (so `Widths`, `"[17]"`); an indirect
  value met MID-path is followed; the FINAL value is not (a `PDK_REF` comes back
  as `refnum`/`refgen`); no names at all describe the object's own value
  (`DICT_GET` of the referenced object is how a reference is dereferenced).
  `num` carries INT, REAL (rounded to 1/65536) and BOOL (0/1); a number
  that does not fit is saturated and answers `PDE_RANGE`. `text` holds a
  name/string's first 127 bytes, `len` its true length; `count` an
  array's elements or a dictionary's pairs. A missing key is `PDE_NOTFOUND`.
* `STRM_OPEN`/`STRM_READ`/`STRM_CLOSE`/`TOKEN` - 20.2.5. With `objnum` 0, `page`
  and `index` name a page's content stream: `/Contents` a stream is element 0,
  an array (direct or indirect) its elements; past the last is `PDE_RANGE`
  (the way to count them), as is a page with no `/Contents`.
* `RES_LOOKUP` - for a page, `/Resources` is inherited; for `page = 0xFFFF`,
  `resobj`/`resgen` name a Form XObject (its own `/Resources`) or a resource
  dictionary itself; no fallback to the page is made when a form has its own
  and lacks the name (that is the renderer's decision). The result is the
  referenced object (`objnum`, `gen`, `kind` of the target, `PDK_STRM` for a
  stream), or `objnum` = 0 with `kind` = `PDK_DICT` for a dictionary written
  inline in `/Resources` (it has no object number, so `DICT_GET` cannot reach
  it: the renderer treats it as a missing resource). Not found:
  `PDE_NOTFOUND`.
* `RAW_COPY` - the object's bytes from `n g obj` through `endobj` from
  `done`, at most `dmax` (itself at most 0xFFF0) to `dseg:doff`; `done`
  advances; the copy is complete when `done` equals `OBJ_FIND`'s `length`. A
  short read is `PDE_TRUNC`. A copy that would cross the end of `dseg`
  (`doff` + the bytes it would move > 0x10000) is `PDE_RANGE` and copies
  nothing - it used to wrap into `dseg:0`.
* `TRAILER` - the newest revision's `/Size`, `/Root`, `/Info`, `startxref`, the
  first 16 bytes of the first `/ID` element (`id0_len` its true length) and the
  header version.
* `OUTLINE` - item `objnum`/`gen` (0/0 is the first item under the catalog's
  `/Outlines`; the identity of the item is written back); `next_*`, `first_*`
  (children), `count` (the magnitude of `/Count`), `dest_page` (the index of
  the page `/Dest [page ...]` or `/A << /D [page ...] >>` names; 0xFFFF for a
  name, a string, a remote destination or a missing page), `title` (PDFDoc
  bytes pass through with controls as spaces; UTF-16BE `FEFF` keeps code
  points under 256 and turns the rest into `?`; 63 characters). No outline is
  `PDE_NOTFOUND`.
* `ANNOTS(BX = page, CX = index)` - element `index` of `/Annots`. A hole
  (a string, an integer, a dangling reference, a non-dictionary) is a record
  with `objnum` 0 or the number and an EMPTY `subtype`, rectangle 0, so the
  caller skips it and keeps counting; a missing or short `/Rect` leaves it 0;
  `/Rect` is normalised; `nm`, `flags`, `has_ap` (an `/AP` with `/N`), `irt_num`,
  `state` (`PDS_COMPLETED` when `/State` is `Completed`), `contents_len` (the
  true length) and `contents` (159 PD Latin characters). Past the end:
  `PDE_RANGE`.
* `SEARCH` - NOT IMPLEMENTED in wave 1: answers `PDE_NOTFOUND`. Search needs the
  renderer's text map (`PDV_REND_TEXTMAP`, 9.2); the engine has nothing to
  match until that exists.
* `FINGERPRINT` - flushes the cache and re-reads the first and last 1,024
  bytes from the disk (CRC-16/CCITT, initial value 0xFFFF; a file under 1 KiB
  uses the same bytes for both) with the size the directory gives NOW, and the
  last KiB read at that size: a file another program appended an update to
  keeps its first KiB and its old last KiB, so only the current size and the
  current tail can see it. The open document keeps OPEN's size and the cache
  is flushed again afterwards. The primary stores the answer at open; the
  writer calls it again before it writes and refuses `PDE_CHANGED` when it
  moved. A file no longer in the folder is `PDE_IO`. Before an `OPEN` it is
  `PDE_RANGE`.
* `ARENA_MARK`/`ARENA_REL` - 20.2.2.

A verb that needs a document before `OPEN` finished answers `PDE_RANGE`. An
I/O error in any read is `PDE_IO` whatever the parser made of the gap, the
block it was reading is not cached, and the cursor is unmoved (so the same
call may be retried).

#### 20.2.7 Error map

| code | when |
|---|---|
| `PDE_SYNTAX` | no `%PDF-d.d` at offset 0 (detail 2); packed file (1); catalog or tree malformed; a stream's `/Length` or `endstream` wrong; a stray `>` or `)`; a bad hex digit |
| `PDE_XREF` | table damaged, `/Prev` loop or out of range, entry/header mismatch, no `/Root` (4), `startxref` out of range, an offset the file does not hold |
| `PDE_TRUNC` | under 9 bytes; no `startxref` (5); data past the end of the file; end of file inside a token |
| `PDE_CYCLE` | a page-tree node twice; a `/Parent` loop; more than 8 indirect hops |
| `PDE_DEPTH` | nesting over 32 in an object, a content stream or the page tree |
| `PDE_FLATE`, `PDE_FILTER` | 20.2.5, at `STRM_OPEN` |
| `PDE_ENCRYPT` / `PDE_XREFSTM` | `/Encrypt` in a trailer / `/Type /XRef` where `xref` was expected |
| `PDE_RANGE` | a page box outside +/-2,047.9 pt or of zero area; `/Rotate` not a multiple of 90; a number that does not fit 16.16; an index past the end; a bad `state`; no document; a `RAW_COPY` or `STRM_READ` destination that would cross the end of its segment |
| `PDE_LIMIT` | token/name/array limits; more than 8 revisions or 1,024 pages; an object number over 65,535; a subsection past object 65,535; a revision of 33+ subsections out of order; an object stream numbered over 32,766 or a member past 65,535 decoded bytes (20.22.3); a file of 2 GiB; 5 filters; 7 open streams; 1 MiB decoded; 4 MiB of filter work |
| `PDE_NOMEM` | a claim too small (20.2.2); a cluster over 32 KiB |
| `PDE_IO` | any failed read; a name not in the folder |
| `PDE_NOTFOUND` | a missing dictionary key or resource; no outline; `SEARCH` |
| `PDE_BUSY` | `OPEN` has more to do - with CF = 0 (D11), not a failure |

`PDE_SIGNED` is not returned: a signed or `/Perms` document opens with
`PDO_READONLY`. `PDE_CANCEL`, `PDE_FONT`, `PDE_GRAPHICS`, `PDE_DISKFULL`,
`PDE_WPROT`, `PDE_EXISTS` and `PDE_CHANGED` are not the engine's.

#### 20.2.8 Where the engine departs from the rest of the contract

* 6.1: a block is never smaller than 2 KiB and a cluster over 32 KiB is refused.
* 7.2: no object arena of parsed nodes; see 20.2.4. The arena claim is the page
  tree's visited bitmap.
* 7.3: 32 exact subsections a revision and checkpoints beyond; 256 in total across the chain.
* A page tree with no pages is `PDE_SYNTAX` (`err_detail = 3`, D10b). An object number above 65535 is `PDE_LIMIT` (D8).
* 7.4: a `/Prev` loop is `PDE_XREF`; the 1,024-byte tail search for `startxref`
  and header-at-zero are the whole of "a damaged file is never repaired".
* 8: no page-table "inherited source" word - inheritance is found by walking
  `/Parent` and the word is reserved 0.
* 1.3: a signature field's own `/V` is not looked for; `/SigFlags` bit 0 and
  `/Perms` decide `PDO_READONLY`.
* 7.4: the generation a reference must match is the object header's; an xref
  entry whose generation disagrees with the header it points at is not
  detected (the resolved-offset cache keeps no generation).
* The wave-1 stub: `SEARCH`. `PDP_HASLINKS` is never set.

### 20.3 The renderer (`rend`)

`PDREND.MOD` (`apps/pagedesk/pdrend.asm` + `pdrend.c`, which `#include`s `pdrg.c`,
`pdrd.c`, `pdrf.c`, `pdri.c`, `pdrv.c`; the primitives are `pdrprim.inc`) is the
rendering segment: content interpreter, display list, band rasteriser, glyph and
image drawing, the text map and thumbnails. This section is its contract beyond
3, 5, 10 and 11: numbers, formats, the exact behaviour of its verbs, and the
places it departs from those sections (20.3.8).

#### 20.3.1 Build, size, checks, claims

`make pdrend` compiles `pdrend.c` with smlrcc, lowers it with `tools/cc8086.py`
(`&local` refusal, 96-byte frames - the largest is 58), assembles `pdrend.asm`
with `nasm -f bin -w+error` (`cpu 8086`) and prints the size; it then builds the
host harness and runs `hosttest/pdr_check.py` and, where a python has Unicorn,
`hosttest/pdr_primtest.py`. `make pdrend-fuzz` is the sanitizer run (20.3.6);
`make pdrend-real` draws every committed sample through the REAL engine
(20.3.6).

| item | bytes |
|---|---|
| `PDREND.MOD` image (code + data) | 43,914 (42,256 before verify-rend, which added the scaled strikes, the resumable inline-image reader, the clip pool, the index checks and the real `READ_SEQ` thunk) |
| bss (zeroed by the door on first entry) | 6,588 |
| image + bss | 50,502 (target 40,960 and the lead's 44 KiB - NOT met; cap 57,344) |
| of which `pdmath.inc`'s share (`PDM_SKIP_PARSE`, `PDM_SKIP_CRC`) | about 3,900 |

The target is missed by a third and the reason is stated, not hidden: SmallerC
emits about 3.4 bytes of 8086 a source token, so the 4,000 lines of C that do the
work are 33 KB; the near arrays the algorithms need are 6 KB of bss (13 KB before the saved graphics states, the font cache and the edge buffers moved to the workspace, below). What was
cut to stay under the cap: dash patterns (20.3.8), a one-row clip mask instead of
a band-sized one, four fonts in the cache instead of six, and the
saved graphics states and the font cache moved into the workspace tail of `dl_seg`
(3.7 KB of bss), and the staged edges and clip edges followed them once the row
scan was written in assembly (`pdr_act`, `pdr_scan`, `pdr_fcopy` in `pdrprim.inc`;
they read 8-byte edge records through ES, and a FILL record is scanned in the list
itself, with no copy). What is left in bss is hot: the active-edge list and crossing
arrays (`g_act`, `g_cx`, `g_cd`, 0.9 KB), the flattened path (1 KB), the operand and
run staging the engine and `dl_put` take by near pointer (2 KB). What
would reach 40 KB is a different division of labour (the shell doing thumbnails,
the text map moving into PDENG), not further trimming.

Claims it expects from `PDV_REND_INIT` (16.2 revised by this section):

| `pdxb_init` field | size | holds |
|---|---|---|
| `dl_seg`, `dl_kb` | **29 KiB** (<= 63): 22 KiB of list + the WORKSPACE (`PDR_WSB` = 6,912 bytes) at the tail of the claim | the display list (20.3.2); also the text-map ring (20.3.4). The workspace holds, by offset: saved graphics states 0 (16 x 78), the four-entry font cache 1,280 (4 x 684; one font is resident in bss at a time), staged edges 4,096 (256 x 8) and clip edges 6,144 (96 x 8). A claim with less than `PDR_WSB` + 256 bytes (`dl_kb` < 7) is treated as NO claim: `BEGIN`, `TEXTMAP` and `THUMB` refuse `PDE_NOMEM` and nothing is written to it (before verify-rend a 1-6 KiB claim had the workspace written past its end) |
| `band_seg`, `band_bytes` | `16 x stride`, stride = `ceil(w/8)` (1bpp) or `ceil(w/2)` (4bpp) of the canvas width rounded out to 8 pixels: 1.5 KiB at 720 px 1bpp, 5.1 KiB at 640 px 4bpp | one band |
| `glyph_seg`, `glyph_kb` | **24 KiB** (16.2 said 12; 20.3.4 carves it; more than 63 counts as 63) | image row buffer, PDFONTX index, transformed glyphs, the strike cache |
| `bank_seg`, `bank_kb` | ignored | (D3: the bank arrives through `PDV_REND_FONTBANK`) |

A claim of 0 is "none now" (D2). `BEGIN`, `THUMB` and `TEXTMAP` refuse with
`PDE_NOMEM` when `dl_seg` or (for drawing) `band_seg` is 0 or the band cannot hold
16 rows of the canvas. `INIT` resets the font state: **the primary sends
`PDV_REND_FONTBANK` after every `INIT`.**

#### 20.3.2 Geometry, the display list and the passes

**Device space** inside the renderer is the 1/4-pixel grid ("qpx"): pixel p covers
`[4p, 4p+4)` and its sample point is `4p+2`; a coordinate is an `int` clamped to
+/-16,000 (+/-4,000 px). The base matrix (page pt -> qpx) is built from the
`pd_view` by the same four rotation cases as 5.3 and is equal to `pdm_view_fwd`
within **one pixel** on every point (`pdr_check`: 1,008 points over every rotation,
zoom 25-400 and the four adapter aspects, on a box with a nonzero origin). A pixel
is painted when its sample point is inside the shape (nonzero or even-odd);
axis-aligned rectangles round the same way and a rectangle that would cover no
sample point but has area is **widened to one pixel**, so a 0.5 pt table rule never
vanishes. A **rectangular clip** (`W`/`W*` of an axis-aligned rectangle, and a
form's `/BBox`) keeps every pixel it TOUCHES, which is PDF's own rule for a clip,
not its sample points. A widened shape's pixel can have its centre outside a clip
that its centre line is inside. The case is a 1 pt stroke half a point inside a
`/BBox`, which is PageDesk's own saved mark (20.5.5): with sample points two of its
edges were lost at 50%. The price is that a shape LARGER than the clip can gain the
touched pixel, up to one along each clipped side. A form's `/BBox` also gives back
the rounding to qpx on its low sides (1 qpx), because there the stroke and the edge
can round to the same pixel boundary at small scales (25-33% on VGA, Hercules' and
CGA's y axis below about 80% and 120%). A rectangle with no area clips everything.
A polygon clip stays on sample points. (square, verify-square; `pdr_annot`'s "an
appearance stroking at its BBox edge".) Beziers are flattened by de Casteljau subdivision until both second
differences of the control polygon sum to <= 1 qpx (chord within 0.19 px of the
curve), depth 6 (64 pieces) - the operation limit. Arithmetic is `pdmath`
(16.16) for everything that is user space; a transform that overflows sets
`PDF_INC_RANGE` and the coordinate clamps, never wraps.

**The display list** is a byte stream of records (all fields little-endian,
coordinates in device pixels unless marked qpx), appended in content order:

| record | layout after the 8-byte header `type, aux, len(u16), y0(i16), y1(i16)` |
|---|---|
| `DLR_STATE` (1) | clip rect `cx0 cy0 cx1 cy1`, `clid` (the innermost polygon clip in the pass's pool, 0xFF none), `c4` (VGA colour), `pat[4]` (the 4 rows of the 8-bit dither/solid pattern) - emitted only when it changed |
| `DLR_RECT` (2) | `x0, x1` - an axis-aligned rectangle, rows `y0..y1` (header) |
| `DLR_FILL` (3) | `aux` = 0 nonzero / 1 even-odd; `xmin, xmax, nedges`, then edges `x0 y0 x1 y1` (qpx, original direction) |
| `DLR_GLYPHS` (4) | `family, style, strike size, flags (rot | (scale-1)<<2, scale 1..12), nglyphs(u16), strike id(u16)`, then `code(u8) x(i16) y(i16)` per glyph - up to 64 a record |
| `DLR_IMAGE` (5) | dest rect `xl yt xr yb` (qpx), `W H` (u16), object `num gen`, `bpc, ncomp, flags` (1 stencil, 2 inverted, 4 mirrored) |

`y0..y1` is the record's row range already intersected with the clip and the pass;
edges wholly outside the pass's rows, and horizontal edges, are never stored, and a
stored edge is clamped to +/-16,000 qpx (`pdr_scan`'s in-line divide relies on it). A
record is appended WHOLE or not at all (a FILL reserves its header, body and edges
before writing any), so a list that fills up holds only complete records and the
walker stops at the first that does not lie inside the list. A scan line with more
than 128 crossings is not painted (`PDF_INC_LIMIT` "crossings per row"): a dropped
crossing would pair the rest wrongly and paint a false span. A
**non-rectangular clip** (W/W* of anything but an axis-aligned rectangle) goes in
a pool of up to 8 polygons / 96 edges that lives for the PASS: an entry is never
rewritten while the pass's list can name it (the list is walked after the whole
pass is interpreted, so a slot reused after a `Q` would cut EARLIER records by the
later polygon - before verify-rend the first of two clipped fills vanished). A state
names its innermost entry (`clid`) and each entry its parent, so nesting is a chain
that the walker ANDs. A polygon the pool cannot hold cuts by its bounding box
(`PDF_INC_LIMIT` "clip depth" / "clip path size"). A fill or rectangle under one is
painted one scan line at a time through two 96-byte masks. Text and images under a
polygon clip are cut to its bounding box only (20.3.8).

**Strokes** become fills of nonzero polygons: a quad per segment, a wedge per
join (miter inside the limit, else bevel; round = an octagon), square caps by
extension, round caps by octagon, all wound the same way so overlaps cannot cancel.
A width under one device pixel is one pixel. Polylines whose segments are all
horizontal or vertical (the table-rule case) become `DLR_RECT`s directly.
`dash patterns are not drawn` (20.3.8).

**Passes (rewrites 10.2; DECISIONS D4).** The framebuffer cannot be read back and a
band is blitted opaque, so a second pass could not paint over a band already
shown. Instead one **pass** interprets the whole content stream for the rows of a
*group of bands*, culling at emit time everything outside them. The first group is
every band of the canvas. When the list fills (`dl_kb`), the group is **halved**
(bands `[b0, b0+n/2)`), the content is **rescanned from its first byte**, the group
is drawn and the next group follows; the group width stays halved. When one band
still overflows, that band is composed in **sub-passes of halving row ranges**
onto the same band buffer and blitted once. Only when even one *row* overflows is
the rest of that row dropped, with `PDF_INC_LIMIT`. The result is the identical image
whatever the list size (`pdr_check`: 4, 8, 12 and 16 KiB lists (claims 4 KiB larger) against 22 KiB,
byte for byte, 11 to 63 passes). `pdxb_rstatus.passes` counts them.

**A band** is 16 canvas rows, `ceil` over `clip_y0..clip_y1`, as wide as the clip
rounded out to multiples of 8 pixels (the kernel blit's rule; `OSAPI_WM_CLIP_SET`
cuts the overhang). It starts as the desk pattern (1bpp: alternating `AA`/`55`
rows, 4bpp: colour 7), the page rectangle is filled white, the list is walked, and
the band is presented: `gfx_lock`, `os88_wm_clip_set(win_off)`, one `OSAPI_GFX_BLIT1`
or `BLIT4`, `gfx_unlock`. **The lock is held for the blit only.** 1bpp: bit 1 =
lit = white. 4bpp: VGA palette index in a nibble, high nibble leftmost.

**Colour (10.5).** Gray, RGB and CMYK operands (and `sc/scn` by operand count) become a
luminance `(77r + 150g + 29b) >> 8` and a VGA index: greys -> 0 / 8 / 7 / 15 at
luminance 48 / 128 / 208; saturated colours -> the nearest of the other twelve by
channel threshold (`rgb_c4`). A 1bpp **fill** and every image is the 4x4 ordered
dither `lit when (17 * lum >> 8) > Bayer[y&3][x&3]`; a 1bpp **stroke, text and
stencil** is solid, white at luminance 240 and above and black below it. Until
the final pass the cut was 128, which drew PageDesk's own saved /Square
(`1 0.333 0.333 RG`, luminance 136) and every light rule WHITE, so they
vanished on reopening; `pdr_annot`'s mono checks hold the line. 4bpp fills use the
index (a grey fill between two greys is their checker, 20.19.2); 4bpp greys in images are dithered between the four greys.

**Images** are drawn at raster time from the engine's stream, which stays OPEN from
band to band while the same image is drawn further down (each band reads on from
the row the last stopped at; a different image, an earlier row or the next pass's
interpretation closes it) - before verify-rend it was reopened and re-read from its
first row for every band (the source
row for a destination row is `(2y+1) * H / 2dh` at the pixel centre), each row
through the assembly row composer. 1-bit, 8-bit grey, 8-bit RGB (to grey), `/ImageMask`
stencils (painted in the fill colour; `/Decode [1 0]` swaps the sense), mirrored
left-right. Rows longer than 1,024 bytes are refused (`PDF_INC_LIMIT`), as are
images drawn rotated by a quarter turn or upside down (`PDF_INC_IMAGE`, 20.3.8).

#### 20.3.3 What PDREND asks of PDENG (verified against `pde_verb`)

PDREND's host build links the real `pdeng.c` (`hosttest/pdr_hosteng.c`) and draws
the same pixels as against its own mini-engine; these are the calls, as 20.2
specifies them:

* `STRM_OPEN {objnum=0, page, index}` for `/Contents` element `index` until
  `PDE_RANGE` (the end); `{objnum, gen}` for a Form or an image; `STRM_CLOSE`
  pops. Up to 6 streams (a page, 4 forms and one image) are open at once.
* `TOKEN`: `PDE_RANGE` with CF = 1 means "number saturated, token valid" and sets
  `PDF_INC_RANGE`; any other error ends the page (`PDF_INC_LIMIT`, "content stream
  error"). Names and operators are NUL-terminated after `len` here.
* `DICT_GET` with paths of names and `"[n]"`; a **final reference is followed by
  PDREND** (a second call with no names). `/Widths` is read element by element, on
  first use of a code, never as a whole.
* `RES_LOOKUP {page}` for the page, `{page=0xFFFF, resobj, resgen}` for a Form that
  has its own `/Resources` (a Form without them keeps its parent's); an inline
  dictionary (`objnum` 0) is "missing". `PAGEINFO` for thumbnails.
* **Inline images** (`BI ... ID ... EI`) are consumed and **not drawn**. PDENG
  cannot unread, so the bytes after `ID` are read EXACTLY when the dictionary
  allows: unfiltered, `/W` `/H`, `/BPC` 1, 2, 4 or 8 and a device space (`/G`,
  `/RGB`, `/CMYK` and the long names) or `/IM true` - `W x components x BPC`
  bits a row, rows padded to the byte; a key the reader does not know takes its
  value (a number, a name, an array) without swallowing the next key. Anything
  else (`/F`, a named space) is scanned for white space, `E`, `I` one byte at a
  time. Either is RESUMABLE - at most 4 KiB a slice - so a large image spans
  slices. The page is Incomplete (image). A dictionary that reaches the end of
  the stream ends it (before verify-rend that looped without end).
* **A slice is bounded in tokens** as well as operators (16 x `PD_SLICE_OPS`): a
  stream of numbers, or an array or dictionary that never closes, has no operator
  to count.

#### 20.3.4 Fonts, text and the text map

**The bank (D3, D5).** `PDV_REND_FONTBANK` takes `pdxb_fontbank`: the PDRES INDEX
claim (directory `PDRS`, the `PDFB` bank at the directory's kind-1 offset `D`,
widths `PDWD`, names), the package name, the launch folder's place and the PDRES
part's file offset. PDREND keeps **no strike bitmap resident**. The glyph claim
(`glyph_kb`, 24 KiB recommended) is carved:

| offset | size | holds |
|---|---|---|
| 0 | 1,024 | the image row buffer |
| 1,024 | 2,048 | the INDEX of `PDFONTX.RES` (read at `FONTBANK` when the file is beside the package) |
| 3,072 | 2,016 | 14 slots of 144 bytes: glyph bitmaps turned or doubled (below) |
| 5,120 | the rest | the **strike cache** |

A strike is read on first use: `file_enter` stands in the launch folder
(`os88_file_goto_q_mark`, the working folder restored afterwards so PDENG's
reads by name still find the document), the cluster size of that volume is asked,
the offset and capacity are rounded OUT to cluster multiples, `READ_SEQ`
delivers them into the cache and the strike is slid down to the paragraph. The
cache holds up to 8 strikes, 512-aligned, bump-allocated; when it is full it is
emptied and the wanted strike read again (so a working set larger than the cache
thrashes: **24 KiB holds every strike of one page of mixed Helvetica, Times and
Courier at 8-16 px; 16 KiB cannot hold a 20 px bold (9.9 KiB with its cluster
slack) beside others** - measured on XTGUIDE). A strike larger than the cache, or
a failed read, makes the run use the next smaller strike (`PDF_INC_FONT`
"font strike unavailable" only when none is left).

**Selection.** Family and style from the BaseFont (11.1; subset prefixes ignored;
a name outside the base-14 families is sans and `PDF_INC_FONT`, "font substituted").
The device em is `fontsize x |text-space y axis|`; the strike is the **largest size
at or below it** over both banks, else the smallest. With only the core sizes
(8, 10, 12), a request of 16 px or more is a core strike of at most half the size
**pixel-doubled** (D5: 16 from 8, 20 from 10, 24 from 12); a missing
`PDFONTX.RES` is not an error and not Incomplete. The same rule carries on above
the largest strike (verify-rend; before it a 120 pt title drew 24 px tall): a
strike of `em / n` or less **scaled n times** (n = 2..12) is used when `n x size`
is at least a quarter larger than the crisp strike, the largest product `<= em`
winning and the smaller `n` on a tie (36 px is a tripled 12 with or without
`PDFONTX.RES`; 28 px with it stays the crisp 24, a doubled 14 being only 4 px
larger). A scaled or turned glyph that fits a cache
slot (32 x 32) is made there; a larger one is drawn block by block through the
clipped span path (`glyph_big`). An italic record flagged
"synthesise" is **sheared** in the compositor, `floor((ascent - row) / 4)` pixels a
row; sheared text is not turned or doubled. Text under 5 device pixels is
**greeked**: one hairline per show operator at 0.3 em above the baseline in half
the ink (thumbnails are made of these).

**Orientation.** The text direction (`Tm x CTM`) picks a quadrant; a quarter-turn
(a rotated page, a rotated text matrix) draws every glyph's bitmap turned by the
same quarter, from the 14-slot cache, so a 90-degree page reads correctly. An
arbitrary angle draws upright bitmaps along the true baseline (a font
simplification). Horizontal scaling (`Tz`) moves glyphs but does not narrow them.

**Widths and encodings.** A code's advance is `/Widths[code - FirstChar]` (read
on first use), else the base-14 table of 20.4.3 for the glyph the code maps to
(`Courier` is 600). Encoding: `WinAnsi` identity; `MacRoman` and `Standard` by the
tables in `pdrenc.h` (generated by `hosttest/pdr_gen.py`); no `/Encoding` is
Standard (as the PDF reference says for the base-14); `/Differences` maps glyph names through the
sorted names table (and `uniXXXX` below 127). A code with no mapping advances
and draws nothing. Type0 (CID) fonts draw nothing and set `PDF_INC_FONT`; Type3 and
Symbol/Dingbats draw as the substitute and set it.

**The text map** (`PDV_REND_TEXTMAP`, 9.2) runs the same interpreter with no
drawing and a base matrix of 16 (user space -> pt16, no flip, MediaBox origin kept),
writing `pdtm_glyph` records (12 bytes: code, flags bit 0 = first glyph of a show
operator, the axis-aligned box of the glyph's quad - advance wide, 3/4 em above the
baseline, 1/4 below - and the running index) into the ring that the list claim is
when no page is being drawn. Each call interprets one slice (256 operators)
**only when the ring is empty**, then copies up to `dmax / 12` records to the
caller; `done` is set when the content is exhausted and the ring drained. Invisible
text (`Tr 3`) is mapped. `PDF_INC_LIMIT` "text map" if a single operator wrote more
than the ring holds. Reading order is content order.

**Thumbnails** (`PDV_REND_THUMB`, 10.8): `PAGEINFO` for the box and rotation, the
zoom (percent) at which the page fits the cell, the page centred in a white cell,
the same pipeline at 1bpp into `dseg:doff` (stride `ceil(w/8)`), driven by repeated
calls (`state` 0 -> 1 -> 2; `PDE_BUSY` while stepping). A new `BEGIN`, `THUMB` or
`TEXTMAP` abandons whatever was running.

#### 20.3.5 Verbs, answers and slices

| verb | answer |
|---|---|
| `INIT` | `PDE_OK`; replaces every claim; clears the font state |
| `FONTBANK` | `PDE_OK`, or `PDE_FONT` when the index is not a PDRES index |
| `BEGIN` | `PDE_OK` (the first pass is set up; nothing is drawn yet), `PDE_NOMEM`, or the engine's error on the first content stream |
| `STEP` | `PDE_BUSY` after a slice, `PDE_OK` after the last band; CF is **clear** on `PDE_BUSY` (it is not a failure) |
| `ABORT` | `PDE_OK`; closes every stream. With **CX = 1** it also writes, as XB word 0, the first view row the running render did not blit (the clip's top when none was); CX = 0, which every other caller sends, is the old verb exactly (20.19.1, verify-polish) |
| `STATUS` | `pdxb_rstatus`: the `PDF_INC_*` bits, the first skipped feature's name, operators interpreted, passes |
| `TEXTMAP`, `THUMB` | as above |

A `STEP` slice is one of: up to `PD_SLICE_OPS` (256) operators of interpretation, or
**one band** composed and blitted (one gfx lock). On the 8088 a band of dense text
is the long one: see 20.3.7. Cancel is `ABORT` between slices.

#### 20.3.6 Assembly primitives and host checks

`pdrprim.inc`: `pdr_muldiv` (`a*b/c` with a 32-bit intermediate, saturating - an
`idiv` overflow would be a #DE), `pdr_fill1`/`pdr_fill4` (a span in a band row),
`pdr_glyph` (one bitmap into 1bpp - clear or set - or 4bpp, with the shear),
`pdr_imgrow` (one image row: nearest sample, the dither, a nibble or a bit),
`pdr_eng` (the far call into PDENG) and `pdr_blit`. Their cdecl convention is
`pdmath.inc`'s; their **C twins** in `hosttest/pdr_host.c` are the specification.
`pdr_glyph`'s precondition: the glyph and its shear margin lie inside the band row,
at most 4 bitmap bytes a row, and the bitmap's padding bits are zero (strike rows
are); the C computes the shear's exact extent - `floor((1 - descent) / 4)` to
`floor(ascent / 4)` - and sends anything else down the per-pixel path. The shear
shift is SIGNED (`sar`): below the descent's eighth row it is negative, and a
logical shift there wrote ~2 KiB to the right of the glyph (verify-rend; the
twin always had the floor). `pdr_act`/`pdr_scan`/`pdr_fcopy` are the edge scan:
`pdr_scan` divides in line (`imul`/`idiv`) on the precondition that an edge's
`|x1 - x0| < 32768`, which the +/-16,000 clamp of every stored edge guarantees,
so the quotient always fits and cannot #DE.

* `hosttest/pdr_primtest.py` runs the SHIPPING assembly in Unicorn against the
  twins (muldiv, both fills, every glyph mode with and without shear - rows below
  the descent's eighth included, which the logical-shift version FAILS - every
  image mode with flip/invert/stencil, the edge scan, random inputs and edges):
  every byte of every arena equal, the calling convention kept; two mutated images
  prove it can fail.
* `hosttest/pdr_check.py`: the view transform; 75 renders (5 pages x 3 zooms x
  VGA, EGA, Hercules, CGA and 1bpp VGA) with the expected Incomplete bits; all
  four rotations; multi-pass == single pass; against **mutool** at the same scale
  (<= 1.4% of 8x8 cells differ by more than 35 points, no ink cell missing beyond
  the font and four-grey simplification); strikes read identically at 512 B, 1 KiB and
  4 KiB clusters (the READ_SEQ stub refuses a seek or capacity that is not a cluster
  multiple); no `PDFONTX.RES`; a small glyph claim; the text map; thumbnails.
* `hosttest/pdr_fuzz.py` (`make pdrend-fuzz`): 30 hostile streams (inline images
  of every shape, a q/Q overflow, a token flood, numbers-only TJ, image rows that
  wrap 16 bits, large text in every quarter turn, control codes, extreme miters)
  and random operator streams, on two address+undefined-behaviour sanitized builds
  - the mini-engine (400 streams) and the REAL `pdeng.c` (200) - with 8 KiB of
  canary behind every claim (a few bytes cannot see a workspace overrun that starts
  6 KiB in), no crash, no hang, no write past a claim.
* `make pdrend-real` (`hosttest/pdr_hosteng.c` + `hosttest/pdr_real.py`): every page
  (up to 4) of every committed sample through the REAL `pdeng.c`, as VGA (640, 1/1,
  4bpp), Hercules (720, 29/45, 1bpp) and CGA (640, 5/12, 1bpp) draw it, against
  mutool resampled to the same aspect: <= 6% of 8x8 cells differ and <= 60 ink
  cells missing on a `supported` sample (measured 0.0-1.8%, 4.2% on one CGA page),
  Incomplete bits only font (and dash, wave 2), a `refused` sample refused cleanly
  at BEGIN (FLATE.PDF: `PDE_FLATE`); then five contract renders (an inline image
  whose data is content, 40 q/Q pairs past the stack, a numbers-only TJ, control
  codes with widths, a 120 pt title), each of which FAILS on the pre-verify
  renderer. `PDR_SHOTS=<dir>` writes every page beside mutool's.
* Not done on the glass: no QEMU/MartyPC row exists yet for PDREND; the module is
  built for the target and its primitives are run on an 8086 model, but the whole
  module has not been executed on an emulated XT (rows proposed in the report).

#### 20.3.7 Cost (ESTIMATES - no emulated XT has timed the module)

Instruction counts of the primitives, from Unicorn (`hosttest/pdr_primtest.py`
harness, counted not timed): `pdr_fill1` of an 80-pixel span 57; `pdr_glyph` 1bpp
6x12 724, 10x16 1,052, 14x24 sheared 2,336; 4bpp 6x12 1,156, 10x16 2,428;
`pdr_imgrow` 64 pixels 3,000-4,900 (47-76 a pixel). At ~14 cycles an instruction
(the 8088's mix with memory operands) a 1bpp 10x16 glyph is ~15k cycles (3 ms) plus
~0.8 ms of C setup; a 4bpp one 7 ms. A band of one dense text line (80 glyphs) is
therefore ~0.3 s on a 1bpp adapter and ~0.6 s at 4bpp, a band of images a few
hundred milliseconds a 64-pixel row-pair - **longer than the 100 ms slice goal for
text-heavy bands; the cause is the per-glyph path (the 4bpp nibble loop is the
obvious next optimisation, 20.3.9)**. The list walk is ~65 us a record a band
(`pd_fcopy_in` of 8 bytes). Disk time is separate: a strike read is one `READ_SEQ`
call of 1-2 clusters (~0.4 s on a floppy), once per strike per `FONTBANK`.

verify-rend's own counts (a whole `pdr_glyph` / `pdr_scan` call in Unicorn,
entry to `ret`, so not comparable digit for digit with the figures above): an
upright glyph keeps its row position, shift and byte count after the first row
(`gl_pok`), 1bpp 10x16 **1,404 -> 1,228 instructions** (3,748 -> 3,228 bytes
fetched, the 8088's floor), 6x12 1,024 -> 896; 4bpp and sheared glyphs are
unchanged (2,380 -> 2,381, 1,978 -> 2,027: the mode test moved ahead of the
hoist). The row scan multiplies and divides inline instead of calling
`_pdr_muldiv` (its precondition, |xb - xa| < 32,768, is what the qpx clamp
guarantees): 40 edges **6,888 -> 5,727 instructions**.

#### 20.3.8 Where PDREND departs from the contract, and what it does not do

* 10.2: rescan with narrower groups instead of "resume from the stream position"
  (D4); a band that still overflows is composed in sub-passes.
* 10.4: the whole canvas clip is drawn (desk pattern outside the page), not only
  the page rectangle.
* 11.2: strike offsets are from the file's first byte (20.4.1); strikes are read
  on demand (D3), PDFONTX.RES's index is read by PDREND.
* 16.2: the glyph claim is 24 KiB, not 12 (20.3.4); the list claim is 29 KiB
  (22 of list + the 6,912-byte workspace, 20.3.1); the current graphics state is in bss.
* Not drawn, and said so (`PDF_INC_*` + the first name): **dash patterns** (`d` is
  parsed, strokes are solid, `PDF_INC_OPER` "dash pattern" when a non-zero
  pattern is set); **inline images**; images turned a quarter or upside down;
  rows over 1,024 bytes; shadings, patterns, non-device colour spaces (a tint of
  1 component draws as grey), transparency (`/SMask`, `/Mask` noted, the image drawn
  opaque), text render modes 4-7 (drawn as 0-3, noted), Type0/Type3/Symbol fonts.
* Text and images under a **polygonal** clip are cut to its bounding box.
* Glyphs above the largest strike are a smaller strike scaled by a whole factor
  (blocky, at the right size; positions follow the PDF widths either way).
* Codes below 32 are not drawn (no PD Latin for them) but advance by their `/Widths`
  entry (read uncached); before verify-rend they advanced by 0.
* Fonts at most 4 distinct per page in the cache (round robin), 14 distinct
  rotated/doubled glyphs hot; paths of 200 vertices, 256 staged edges, 24
  operands, 512 bytes of a string per operator (the rest is cut, `PDF_INC_LIMIT`).
* The page's `q` depth beyond 16 and Form nesting beyond 4 are refused
  (`PDF_INC_LIMIT`); the `Q` that matches a refused `q` restores nothing, so every
  pair below stays balanced (before verify-rend it popped a real level). A Form
  is refused when the stack has no room for its own save. A Form invoking itself
  ends at the depth limit. A `TJ` array of numbers only still moves the pen.

#### 20.3.8.1 Strike choice and the working folder (DECISIONS D16, D14b)

The strike is picked by `min(h_em, ceil(5/4 v_em))` pixels: the em along text
space's x axis on the device, capped so a glyph cannot reach into the next line.
On VGA the two ems are equal and nothing changes; on CGA and Hercules a pixel is
tall, and the old pick by the vertical em drew a narrow glyph on true-width
advances (letter-spaced, greeked below 6 px). Before/after crops of XTGUIDE at
fit-width and 200% on both grids are in the lead's evidence
(`shots/verify-rend/d16/`); the after reads better on every one. Greeking now
uses the picked em, and a greeked run's length the horizontal em.

Every strike read is bracketed inside the verb: `file_enter` records the working
folder (`os88_file_here`), stands in the package's, and `file_leave` puts the
recorded one back - on the failure paths too - before PDENG's next read by name
in the same slice (the shell restores the document's folder only between wakes).
`hosttest/pdr_host.c` models the two folders on different volumes and counts any
engine read made in the wrong one (`pdrh_wrongdir`, 0 required).

#### 20.3.9 Open items

The 4bpp glyph compositor writes a nibble a set bit (a byte-table version would be
~3x faster); no `PDREND` row on an emulated XT yet; the clip pool holds 8
polygon clips a pass (a ninth is its bounding box, `PDF_INC_LIMIT`); text and
images under a polygon clip are cut to its bounding box; device space is clamped to +-16,000 qpx, so a giant page at 400% loses the far
part of a path rather than wrapping; (polish, 20.19.2: 4bpp grey fills are now a 7-level checker, not rounded to white); a
`kern_mega` build would need the glyph claim addressed through selectors (the cache
uses segment arithmetic, as every claim here does).

### 20.4 Fonts, widths and art (`fonts`)

Two resource files in one format (DECISIONS D3, D5), built by `tools/os88pdfont.py`
from the committed sources under `apps/pagedesk/fonts/` and `apps/pagedesk/art/`
(pure Python, no network, no Pillow, run by `make`; `make pdfonts` builds and
checks them alone). Nothing is looked up at run time. All little-endian.

| file | where it lives | carries |
|---|---|---|
| `PDRES.BIN` | a part of `PAGEDESK.O88`, stored UNPACKED so file offsets are real (D3) | the CORE strikes: 8, 10, 12 px, all three families, all four styles (36 records, the italics synthesised); the width tables; the glyph-name table; the icon atlas |
| `PDFONTX.RES` | optional, beside the package in the launch folder; on the 720KB, 1.2MB and 1.44MB PageDesk disks and the everything set, not the 360KB disk (D5) | the other sizes: 6, 7, 9, 14, 16, 20, 24 px, all three families, all four styles (84 records; TRUE italics at 14 and 16, the rest synthesised). No widths, names or icons |
| `PDFONTS.TXT` | beside the package on EVERY disk that carries it | the font licence (SIL OFL 1.1 section 2 wants it with each copy) and the AFM notice: `LICENSES.txt`, `OFL-Liberation.txt`, `AUTHORS-Liberation.txt`, `AFM-NOTICE.txt` concatenated, CRLF, ASCII |

A size is in exactly one of the two resource files. A missing `PDFONTX.RES` is not an
error and not Incomplete rendering: the renderer scales a core strike (D5).

#### 20.4.1 Layout and the directory (D3)

Each file is laid out INDEX first. Every offset in a file - the directory's, and the
`u32` strike offsets in the strike records - is from THAT file's first byte (for
`PDRES.BIN` the file offset of the part inside `PAGEDESK.O88` is added, D3's
`part_off`). This is the one change to 11.2, whose strike offsets were from the bank
start.

```
0          directory: 'PDRS', u16 version (1), u16 entry count n (7),
           n x 12-byte entries {u16 kind, u16 flags (0), u32 offset, u32 length}
D          the PDFB bank: 'PDFB', u16 version (1), u16 strike count m,
           m x 16-byte strike records (11.2's layout),
           'PDFX', u16 widths_off, u16 names_off   (from D; 0 = absent),
           the PDWD width tables (20.4.3), the glyph-name table (below)
INDEX end  = D + the bank's length; zero padding to a multiple of 16
data_off   strike data: every distinct strike's glyph records + bitmaps, each
           starting 16-bit aligned (the records point here)
icons_off  the PDIC icon atlas (20.4.4), 16-byte aligned
notice     a short ASCII copyright notice naming PDFONTS.TXT; the file's last bytes
```

`D` is a multiple of 16 (96 today: the directory padded to a paragraph), so with
the INDEX in a claim at `claim_seg`, `bank_seg = claim_seg + D/16`, and
`widths_off`/`names_off` are offsets from `bank_seg:0`. Read `D` from kind 1's offset,
never assume it.

| kind | part | `PDRES.BIN` (offset, length) | `PDFONTX.RES` |
|---|---|---|---|
| 1 | the PDFB bank: header, strike records, PDFX, widths, names | 96, 7,696 | 96, 1,360 |
| 3 | the PDWD width tables (inside the bank) | 688, 5,384 | 0, 0 |
| 4 | the PDIC icon atlas | 74,576, 2,708 | 0, 0 |
| 5 | the glyph-name table (inside the bank) | 6,072, 1,720 | 0, 0 |
| 6 | the strike data region | 7,792, 66,784 | 1,456, 309,730 |
| 7 | the INDEX: offset 0, length = the bytes the primary loads | 0, **7,792** | 0, **1,456** |
| 8 | the notice | 77,296, 532 | 311,200, 532 |

File sizes: `PDRES.BIN` 77,828 bytes, `PDFONTX.RES` 311,732. Both INDEXes are inside
D3's 8 KiB, and `os88pdfont.py check` refuses either file when its INDEX is not. A
kind whose length is 0 is absent (its offset is 0). The primary reads the INDEX of
`PDRES.BIN` (directory first, then `[0, kind 7 length)`) into one claim and the
icon atlas (kind 4) itself; PDREND reads a strike's `length` bytes at its record's
offset into the glyph cache on first use. The largest strike is 4,692 bytes in
`PDRES.BIN` and 11,790 in `PDFONTX.RES` (sans bold 24), so every strike fits 11.2's
16 KiB glyph cache; `check` refuses a file with one that does not.

The glyph-name table (rend's request): Adobe glyph name to Windows-1252 code,
`{u8 len, name bytes, u8 code}` repeated, sorted bytewise by name, ended by a
`len` of 0; 216 names, 1,720 bytes. The names are 11.4's Windows-1252 repertoire;
`space`, `hyphen` and `bullet` map to 32, 45 and 149 (not 160, 173 or the undefined
codes that draw a bullet). Names outside the repertoire (`minus`, `fi`, ...) are
not in it: a `/Differences` name that is not found draws the substitute box.

#### 20.4.2 Strikes

`PDFB` as 11.2, with strike offsets from the file's first byte (20.4.1). Family 0/1/2 = sans/serif/mono (Liberation Sans/Serif/Mono
2.1.5, OFL), style byte bit 0 bold, bit 1 italic. Per family and size there are four
records (regular, bold, italic, bold italic). TRUE italic strikes exist at 14 and 16
px only; at every other size the italic records carry flag bit 0 (`synthesise`) and
share the upright strike's data (same offset and length), so the renderer slants it.
Each strike has ascent/descent rows measured from the actual ink (a strike's cell
height is `ascent+descent`, per strike), codes 32..255 in Windows-1252 (11.4); codes
127, 129, 141, 143, 144 and 157 draw a bullet, code 160 is blank, 173 a hyphen. A
glyph's bitmap is ink-trimmed horizontally (`width` = ink width, `left bearing` the
signed distance from the pen origin; width 0 = no ink, no rows) and has the full
`ascent+descent` rows; its glyph-record offset is from the bitmap base
(`4*(last-first+1)` bytes into the strike). PDF placement uses the 20.4.3 widths or the
PDF's own, never the bitmap.

Strike sources: `apps/pagedesk/fonts/<family>-<style>-<size>.t88`
(`sans|serif|mono`, `r|b|i|bi`), one readable text file each: header lines
(`family`, `style`, `size`, `ascent`, `descent`), then per code
`glyph <code> w <width> lb <left bearing> top <rows above the baseline of the first
ink row>` and the ink rows (`#` ink, `.` paper). Comment lines start with `# `.
The files ARE the source: hand-fix a glyph and `build` packs it. Legibility: 8 px and
up read well; 6 and 7 px are for dense captions only (a 6 px lower-case is
recognisable but not comfortable) and the renderer should prefer 8 when it can.

#### 20.4.3 Width tables

`PDWD`: `'PDWD'`, u16 version (1), u16 face count (12), then 12 x 224 u16 advance
widths in 1/1000 em, codes 32..255. Face index = `family*4 + style`, style 0
regular, 1 bold, 2 italic/oblique, 3 bold italic - sans = Helvetica, serif =
Times-Roman, mono = Courier. Source `apps/pagedesk/fonts/widths.txt`, extracted from the
Adobe Core 14 AFMs (11.3) by name through the Windows-1252 glyph-name list in
`os88pdfont.py`; every name must exist in every AFM or `draw` refuses. The notice
`AFM-NOTICE.txt` travels with it.

#### 20.4.4 The icon atlas

`PDIC`: `'PDIC'`, u16 version (1), u16 count n, then n x 20-byte entries:
`name[12]` NUL padded, `u8 w, u8 h, u8 hot x, u8 hot y, u16 mono offset, u16 vga offset`
(0 = none; offsets from the atlas start). Mono data of a 16-wide asset is `h` mask
words then `h` data words (the order `OS88_ICON16` uses: mask = white underlay,
data = black, bit 15 leftmost), so a CGA/Hercules asset has exactly two pixel values.
An 8-wide asset (the document glyph) is `h` bytes. Optional VGA data is `h` words
of opacity mask then `h` rows of `w/2` bytes (4bpp, high nibble left, VGA palette
0..15).

Assets (29, 16x16 unless noted): `open save undo redo prev next zoomout zoomin find
select highlight note text mark ink line rect ellipse sign more close collapse
resolve pages` (toolbar; `highlight` has a VGA variant), `appicon` (with VGA
variant), `docglyph` (8x8), cursors `hand` (hot 6,0) `cross` (hot 7,7) `ibeam`
(hot 7,7). Sources: `apps/pagedesk/art/{toolbar,app,cursors}.art`, hand-drawn text
grids (`#` black, `o` white, `.` transparent; `vga` block with hex digits);
`tools/os88pdart.py build` also writes `$(BUILD)/pdart_appicon.inc` (32 `dw` rows,
between `OS88_ICON16` and `OS88_ICON16_END`) and `$(BUILD)/pdart_docglyph.inc` (8 `db`
rows, between `OS88_DOCGLYPH8` and `OS88_DOCGLYPH8_END`). No generated artwork was
used.

#### 20.4.5 Licences and regeneration

`apps/pagedesk/fonts/LICENSES.txt` is the attribution file; `OFL-Liberation.txt`,
`AUTHORS-Liberation.txt` and `AFM-NOTICE.txt` sit beside it. The strikes are
Modified Versions under the OFL and avoid the Reserved Font Name (called PD Sans,
PD Serif, PD Mono). `os88pdfont.py fetch` downloads the pinned Liberation 2.1.5
archive and the twelve AFMs (Apache PDFBox commit a0e38de6 raw URLs) into
`build/pdfetch/`, SHA-256 verified; `draw` re-rasterises (and OVERWRITES hand fixes);
`proof` writes PNG proofs. None of the three runs at make time. What `make` runs:
`build` and `build --extra` (the two files), `notice` (`$(BUILD)/PDFONTS.TXT`) and
`check` / `check --extra`, which decodes every glyph back against its `.t88`, compares
the widths, names and icon atlas with their sources, and holds the INDEX to 8 KiB and
every strike to the 16 KiB glyph cache. `PDFONTS.TXT` must go on every disk that
carries `PAGEDESK.O88` or `PDFONTX.RES` (20.6's disk rules), and the notice part in
both resource files names it.

### 20.5 The writer (`wrt`)

Source: `apps/pagedesk/pdwrt.asm` (door), `pdwrt.c` (verbs) and the `pdw*.c` it
`#include`s (one translation unit, SPEC.md 73.1): `pdwio.c` the file layer,
`pdwser.c` the serialiser, `pdwap.c` annotation dictionaries and appearance
streams, `pdwscan.c` the object patcher, `pdwsave.c` the jobs, `pdwwid.h` the
TEMPORARY width tables (generated by `hosttest/pdw_genwidths.py`). Host
harness: `hosttest/pdw_test.c` (`#include`s `pdwrt.c`) and the independent
check `hosttest/pdw_verify.py` (qpdf, mutool, Poppler); `make pdwrt` runs both.

#### 20.5.1 The door

The shape of 3.1. `AL` = verb, `BX/CX/DX` = arguments (the four verbs below use
only the exchange block). Out: `AX`; `CF` = 1 for every failure, **`CF` = 0 and
`AX` = `PDE_BUSY` while a job must be called again**. `BX`, `CX`, `DX` are
clobbered; `BP SI DI DS ES SS:SP DF` are kept. `PDV_WRT_EXTRACT` answers
`PDE_LIMIT` in wave 1 (graph copy, 20.5.12). `PDV_WRT_INIT` follows D2: any
number of times while no job is active, each call replaces the whole
`pdxb_init` (`wbuf_seg`, `wbuf_kb`, the PDENG far vector); an INIT during a job
drops it and deletes its temp file.

#### 20.5.2 The writer buffer, flushing, numbering, identity

`wbuf_seg`/`wbuf_kb` (claimed by the primary, <= 63 KiB used) is two regions.
The **output region** is `max(4096, 2 x cluster)` bytes at offset 0 and is
where every byte is staged; the **object table** follows it, 8-byte entries
(object, generation, 32-bit offset) = `(wbuf_kb*1024 - output)/8` objects. A
buffer too small for both is refused `PDE_NOMEM` with the KB it wanted in
`err_detail`. **Recommended `wbuf_kb` = 12** (1,024 objects: 250 annotations of
two objects each plus revised pages); the 8 KiB of 16.2 holds 512 objects and a
save that needs more answers `PDE_LIMIT`. Extract (wave 2) sizes it from the
page count: 4,096 objects = 36 KiB.

*Flush policy.* The cluster is `os88_disk_cluster_sectors() * 512`. The output
region is written to the temp file in WHOLE CLUSTERS only (OSAPI_FILE_APPEND's
rule, SPEC.md 18.4.7); the tail below one cluster stays at the head of the
region and moves down. The first flush creates the file (`OSAPI_FILE_WRITE`);
later ones are `OSAPI_FILE_WRITE_SEQ` with `WSEQF_HELD`, so the FAT and the
size are committed once, at the close (a call with count 0). Source bytes are
read with `OSAPI_FILE_READ_SEQ` into the output region at offset 0 (512-aligned
by construction). Nothing but `pw_flush` and the source reader touches a file.

*Two passes.* A stream's `/Length` precedes its bytes, so a generator runs
twice, once with `g_mode` = COUNT (bytes counted, nothing staged) and once
EMIT; no stream is buffered whole and no `/Length` is an indirect object.

*Numbering.* New objects are numbered from the source's newest `/Size` (an
incremental save) or from 1 (a fresh document), **in emission order**, so a new
object's xref entries are one ascending run. A record that replaces a source
annotation (`src_num` != 0) is written under THAT number and generation
(newest revision wins; its appearance stream is a new object); two live records
naming one source object are refused `PDE_LIMIT`, `err_detail` 9. A review reply
is numbered in edit order like any record; its `irt` is the `id` of a live note
record, or with `PDEF_IRTSRC` a source object number (generation 0 - `pd_edit`
has no field for the parent's generation). A reply whose PageDesk parent is not
a live record (deleted, or never there) is DROPPED, never written with an `/IRT`
that names whatever object carries that number; `PDEF_IRTSRC` in a fresh
document is refused `PDE_RANGE`, `err_detail` 8.

*Identity (13.2).* `/NM (PD-<nonce 8 hex>-<id 4 hex>)`; the nonce is
`crc_head << 16 | (crc_tail ^ size.lo)` of PDENG's `FINGERPRINT` of the source
(a fresh document: a CRC of the date, the edit and page counts). A replaced
annotation keeps its own `/NM` verbatim when it has one (read through
`RAW_COPY`), so the identifier survives re-saves even though the fingerprint
moves with every save. The new `/ID` second element is eight CRC-16 words mixed
from the head of what was written, the nonce, the date, the object count and the
file length; a source with no `/ID` gets the new value in both slots.

#### 20.5.3 Serialising

Numbers: `pb_pt` writes a pt16 as `n/16` with at most four fraction digits and
no trailing zeros, exactly (`0.0625`, `-3.5`) in 16-bit arithmetic only; byte
offsets and lengths are `pd_u32` through `pdm_u32_todec` (xref offsets 10
digits), so no 64 KiB limit exists anywhere. Text strings (`/Contents`, `/T`):
plain ASCII as a literal with `\( \) \\` and `\r \n \t` escaped, anything with a
byte >= 0x80 as `<FEFF...>` UTF-16BE (Windows-1252 0x80-0x9F mapped to their
Unicode). Content-stream strings (WinAnsi): literal, bytes >= 0x80 and controls
as `\ooo`. Names: `#xx` for anything outside the regular characters.

#### 20.5.4 The edit store as the writer reads it

`edits_seg` holds `edits_count` 48-byte `pd_edit` records from offset 0.
`text_off` and `pts_off` are **byte offsets from `edits_seg`:0** (the text heap
and the point heap live in the same claim past the records), `text_len` bytes
(<= 1,024, 8-bit Windows-1252), `pts_n` **points**, each `x y` as two pt16
words. 13.1.1 pins these conventions (D9); the writer's reading of them:

| kind | points | fields |
|---|---|---|
| `PDA_HIGHLIGHT` | 4 per quad in PDF 1.7 order UL, UR, LL, LR | `colour` |
| `PDA_INK`, `PDA_SIGN` | strokes separated by a pen-up point (`x` = -32768) | `size` = width x 2 (half points), `colour` |
| `PDA_LINE` | 2 (start, end) | `style` = width (pt), `align` bit 0 = arrow at the START, bit 1 = at the END (13.1.1; `/LE [start end]`) |
| `PDA_RECT`, `PDA_ELLIPSE`, `PDA_AREA` | none; `rect` | `style` = width (pt, min 1); area: `align` bit 0 = dashed |
| `PDA_TEXT` | none; `rect` | `style` = family<<4 \| bold \| italic<<1, `size` pt, `align` 0/1/2, `colour` |
| `PDA_NOTE` | none; `rect` origin (size forced to 20 x 18 pt) | `irt` != 0 makes it a review reply, `state` |

`colour` is a `PDC_*` index (black 0 ... white 15; the PDF rgb is the 4-level
EGA triple, pdabi.h). Default for a highlight is `PDC_YELLOW`. Any other kind -
`PDA_PAGEOP` (13.7, wave 2: `PDE_LIMIT`, `err_detail` 7) or a number outside
`PDA_NOTE..PDA_SIGN` (`PDE_RANGE`, `err_detail` 10) - is REFUSED, never
silently dropped; so is an incremental save whose `pages_seg`/`pages_count`
is not the source's own order (a move, or a page left out: `PDE_LIMIT`, 7).
`pages_count` = 0 means "the source's order".

#### 20.5.5 Annotation dictionaries and appearance recipes

Every kind of 13.3 is written, each with `/Type /Annot /F 4 /NM /P /M` (when a
date is set) `/C /AP << /N n 0 R >>`. `/Rect` is the record's rect, except
highlight, ink, sign and line, where it is the bounding box of the points
(grown by half the stroke plus 1 pt, or by the arrowhead) so the appearance is
never clipped. The appearance is a Form XObject with `/BBox [0 0 W H]`, no
`/Matrix`, and everything drawn relative to the rect's lower left (5.5).

* **Note**: `/Text /Name /Comment /Open false`, the speech bubble: a Bezier
  outline filled with `/C`, three text strokes.
* **Highlight**: `/QuadPoints` as stored; the appearance fills each quad UL UR
  LR LL under `/GS gs` with `/ExtGState << /GS << /BM /Multiply >> >>` (the
  catalog gains `/Version` >= 1.4, 20.5.7).
* **Area mark / Rect**: `/Square`, `/BS` solid or `[3 3]` dashed, a stroked
  rectangle inset by half the width. **Ellipse**: `/Circle`, four Beziers with
  kappa 0.5523. An area mark carries `/Subj (Area mark)`.
* **FreeText**: `/DA (/Helv 12 Tf r g b rg)`, `/Q`, `/Border [0 0 0]`; the
  appearance carries `/Font << /Helv << /Type /Font /Subtype /Type1 /BaseFont
  /Helvetica /Encoding /WinAnsiEncoding >> >>` (resource names `Helv HeBo HeOb
  HeBO TiRo TiBo TiIt TiBI Cour CoBo CoOb CoBO`) and `BT ... Tm (line) Tj ... ET`.
* **Ink / Sign**: `/InkList` per stroke, `/BS /W` = size/2 pt, round caps and
  joins, a one-point stroke a dot; Sign adds `/Subj (Drawn signature)` and no
  other claim (13.9).
* **Line**: `/L`, `/LE [/None /OpenArrow]` (either end or both), the arrowhead
  an open V of two barbs 25 degrees off the line (a 16.16 unit vector), length
  8 pt + 2 widths.
* **Review reply (13.4)**: `/Text /IRT n g R /StateModel /Review /State
  /Completed|/None /F 32`, no `/AP` (it is never displayed). No `/RT`: it is a
  PDF 1.6 key and `/R` is its default.

*Versions (13.5), by key* (PDF 1.7 tables 164-182): `/NM` is PDF 1.4 and every
annotation carries one (13.2), so ANY annotation makes the output 1.4 (as do
`/LE`, FreeText `/Q` and the highlight's `/BM`); `/Subj` (area mark, drawn
signature) and the reply's `/IRT /State /StateModel` make it 1.5.

A replaced source annotation is rewritten from the record, but keeps the keys
that are its identity and history rather than the editor's, verbatim out of the
source object: `/NM`, `/T` (the current author is never written over another
person's note), `/Popup` and `/CreationDate` (200 bytes of them at most; one
that does not fit falls back to the generated `/NM` / the current `/T`, or is
dropped). Other keys the record cannot express are not carried over.

#### 20.5.6 FreeText layout and the TEMPORARY width tables

The frame is padded 2 pt; the text width limit is `(W - 4 pt) * 1000 /
(16 * size)` in 1/1000 em, accumulated in `pd_u32` per glyph. Lines break at
the last space that fits, else inside the word; `\n`, `\r`, `\r\n` start a
paragraph; leading is 1.2 x size; lines below the frame are not emitted
(clipped by the BBox in any reader). Widths come from `pdwwid.h`: Core-14 AFM
advances for Helvetica, Helvetica-Bold, Times-Roman/Bold/Italic/BoldItalic
(Oblique faces share Helvetica's, Courier is 600), codes 32..255, generated by
`hosttest/pdw_genwidths.py` from reportlab's copy of Adobe's AFM data.
**TEMPORARY**: replaced by the PDRES width tables (11.3) at integration; the
interface is `ft_cw(code)`.

#### 20.5.7 What an incremental save writes (14.2 steps 1-3)

In order, after the source bytes and one newline: (1) for each page that gains
or loses an annotation, the page object again with `/Annots` patched (inline
array: refs appended, deleted refs dropped; indirect array: that array object
revised; absent: `/Annots [ ... ]` added); (2) the catalog again with `/Version
/1.4|/1.5` when the output needs more than the source's version (13.5; the
source's `/Version` value is replaced in place); (3) per live record, the
annotation object then its appearance stream; (4) `xref` with one subsection
per run of consecutive numbers, 20-byte entries (`oooooooooo ggggg n \n`);
(5) `trailer << /Size /Root /Info /ID [<source id0><new>] /Prev >>`, `startxref`,
`%%EOF`. Objects are patched from `PDV_ENG_RAW_COPY` text by the scanner in
`pdwscan.c`, which walks dictionary keys at depth 1 skipping each value by
syntax only (never deeper than 16, never past the buffer); an object longer than
3,072 bytes is refused `PDE_LIMIT`. Before the rename the writer re-reads the
temp file: the tail must be `startxref N %%EOF` with `N` the offset it wrote
`xref` at, and the bytes at `N` must spell `xref` (`PDE_TRUNC` / `PDE_XREF`
otherwise, temp deleted); when the last cluster holds under 190 bytes the read
starts a cluster earlier, because the trailer can straddle the boundary (it
refused about one file length in sixty until verify-wrt's fix). The catalog is
revised only when its own `/Version` is lower than the output needs: PDENG's
TRAILER `version` is the HEADER's, so a `/1.6` catalog over a 1.3 header is left
alone, never lowered. Verifying through PDENG itself (14.2 step 4) is wave 2.

#### 20.5.8 PDENG verbs used

`TRAILER` (size, root, info, id0, startxref, version), `FINGERPRINT` (nonce),
`PAGEINFO` (page index -> object, generation), `RAW_COPY` (an object's bytes,
from its `n g obj` through `endobj`, into a static buffer in the module's DS;
asked twice - the second call into an 8-byte slack so an over-long object is
detected, not truncated). `pdxb_rawcopy.done` is IN/OUT: PDENG advances it, so
the second call sets it to the bytes received rather than adding them again
(adding answered `PDE_RANGE` on every page patch against the real engine; the
host stub now advances it the same way). `DICT_GET` is
not used: `/Annots`, `/NM` and `/Version` are never inherited, and the patcher
needs the raw text anyway.

#### 20.5.9 The job protocol (`PDV_WRT_SAVE`, `PDV_WRT_NEWDOC`)

One job at a time. `pdxb_save.state` = 0 starts a job (an active one is aborted
first); the module answers `PDE_BUSY` with `state` = phase + 1 and `written`
(bytes so far) / `total` (an estimate for `OSAPI_FS_OP`: the source's last
xref + 1 KiB + 600 bytes an edit) updated, until it ends. **A slice makes at
most one disk transfer** (a source read, a write of the buffered whole clusters,
the close, a verify read), at most ~1.5 KiB of generated text (point loops run
96 points a call, `bl_*`), at most four CPU units. A finished, verified temp file
`PDSAVE.$$$` is renamed to `target`: if the name is taken the answer is
`PDE_EXISTS` with `state` = `PDW_ST_CONFIRM` (0x0100) and the temp file waits;
the primary asks, then calls again with `state` = `PDW_ST_REPLACE` (0x0200) -
the target is deleted and the temp renamed (14.3: a failure after the delete
leaves the complete file as `PDSAVE.$$$`, `err_detail` = 1; a failure of the
DELETE itself leaves the target untouched and is an ordinary failure, temp
deleted) - or calls `PDV_WRT_ABORT`, which deletes the temp file. Every other failure deletes the
temp file before answering. Disk full / write protected / short write map to
`PDE_DISKFULL` / `PDE_WPROT` / `PDE_IO`. Success: `state` = `PDW_ST_DONE`.

`PDV_WRT_NEWDOC`: `pages_seg`/`pages_count` is an array of `struct pdw_newpage`
(`pdw.h`, 10 bytes: `w h` MediaBox pt16, `rotate`, `cseg clen` contents bytes at
`cseg:0`, `clen` 0 = blank); edits' `page` is the page index. Objects: 1
Catalog, 2 Pages, 3 Info (`/Producer`), 4-6 the shared `Helv TiRo Cour` fonts,
then page `7+2k` and its contents `8+2k`, then annotations. `%PDF-1.5`, flat
page tree, classic xref, `/ID [<n><n>]`. The contents are streamed half a
buffer a slice.

#### 20.5.10 The date and the author

`/M` is written only when `pdxb_save.date` is set (`"D:YYYYMMDDHHmmSS"`) and
`/T` only when `pdxb_save.author` is (D9); the writer copies both into its own
`pdw_date`/`pdw_author` at the start of a job. The source an incremental save
copies is `pdxb_save.source`, an 8.3 name in the CURRENT directory, as the target
and `PDSAVE.$$$` are (READ_SEQ and WRITE_SEQ take nothing else).

#### 20.5.11 Sizes and limits

PDWRT.MOD measures image 27,952 + bss 9,258 = 37,210 bytes, 36.3 KiB (target
40, cap 48; verify-wrt's fixes cost 642 bytes net of the ~490 that skipping
pdmath's unused PARSE group saved); the widest C frame is 28 bytes.
Limits: 256 records, 1,024 text bytes, 8,000 points an annotation, 1,024 pages,
one source object patched <= 3,072 bytes, objects per save = table capacity
(20.5.2), a cluster of at most 32 sectors (16 KiB; `os88_disk_cluster_sectors`
above 32 would wrap the 16-bit byte count and is refused `PDE_NOMEM`, a volume
that does not answer `PDE_IO`).

#### 20.5.12 What remains for wave 2

The asks of wave 1 are answered (D9: `pdxb_save.source/date/author`, the edit
store pinned in 13.1.1, `PDC_*`, `wbuf_kb` 12). `make pdwrt-real` runs
`hosttest/pdw_real.c`: PDWRT's incremental save through `pdw_verb` against
PDENG's own host build, twice in a chain, each output re-opened by PDENG (it
SKIPs until `pdeng.c` is on the branch; verify-wrt ran it green against
`pagedesk/eng` 965a179db). Not done:

1. **The target, run.** PDWRT.MOD has been assembled, never executed: the door,
   `_pdw_engcall`, the READ_SEQ/WRITE_SEQ/rename/delete thunks and every 16-bit
   product are covered by review and the host build only (whose `int` is 32
   bits). The first emulator boot that saves is the first test of them.
2. **One folder.** Source, target and `PDSAVE.$$$` are 8.3 names in the CURRENT
   directory (READ_SEQ and WRITE_SEQ take nothing else). A Save As into another
   folder needs the primary to copy across folders (or the module to hop the
   current directory between the source read and the write), which is not built.
3. **14.4 `PDE_CHANGED`**: the primary holds the open-time fingerprint and the
   writer compares it with `FINGERPRINT` at `job_begin`.
4. **14.2 step 4 through PDENG**: re-reading the trailer and three new objects
   needs a second open document in PDENG (or a re-open after the rename);
   `pdw_real.c` does the re-open on the host.
5. **The slice budget, measured.** A slice makes at most one transfer of the
   WRITER's, but `an_prep`/`pg_patch` also call PAGEINFO and two RAW_COPYs, which
   may each read through PDENG's cache; one 1,024-byte FreeText's lines are not
   split across slices (at most ~9 KB, two or three of the writer's own
   transfers); a held WRITE_SEQ is committed at every `gfx_unlock`, so between
   slices the token goes cold and the next append pays a lookup and a walk. A
   MartyPC row must time a save of a sample with a note on a 360 KB disk.
6. **Page operations, Extract, graph copy** (13.7, 13.8; refused today), and
   xref-stream sources (PDENG's `PDE_XREFSTM`).
7. **The shell's half**: `struct pd_newpage` (10 bytes, PDV_WRT_NEWDOC's page
   list) is in `pdabi.h` now, and every module answers `PDE_BUSY` with `CF` = 0
   (3.1, DECISIONS D11); after a Save As over the source's
   own name (REPLACE) PDENG's open file is gone and the document must be
   re-opened; the edit store must not change between a job's slices.
8. **Fidelity**: a replaced annotation keeps `/NM /T /Popup /CreationDate`
   only (20.5.5); a reply to a SOURCE note is written with generation 0;
   FreeText's `/DA` font name has no AcroForm `/DR` behind it (every reader
   tested uses the `/AP`); widths are still the TEMPORARY `pdwwid.h` (PDRES's
   tables are on `feat/pagedesk` now, 20.4.3).

### 20.6 The package and its interface (`shell`)

The primary is `apps/pagedesk/pagedesk.asm` (the shim: header, parts table,
`os88ui.inc`, the seam `pdui.inc`, `weave/wfetch.inc`, `pdmath.inc`) and one C
translation unit, `pagedesk.c`, which `#include`s `pdulay.c` (layout, pure),
`pduerr.c` (a sentence per `PDE_*`), `pdumod.c` (modules and claims),
`pdudraw.c`, `pduview.c`, `pdujob.c` (the scheduler), `pducmd.c` (menus,
keys, clicks, popovers) and `pdovl.c` (the overlay's tenants: the help and
info cards). `pdulay.c` also compiles on the host:
`apps/pagedesk/hosttest/pdu_lay.c` sweeps every content box from 320x100 to
720x460 in every mode, and the five displays by name (20.6.4).

#### 20.6.1 Parts table

| row | name | kind, flags | what it is |
|---|---|---|---|
| 0 | `PDENG` | `OP_SEG`, `OP_COMP` | the module image followed by its bss as zeros (the Makefile's `pd*.part` rule), so the carve covers the bss; plain `OP_COMP`, because `OP_ZFILL` refuses a part whose only zeros are its tail |
| 1 | `PDREND` | `OP_SEG`, `OP_COMP` | the same |
| 2 | `PDWRT` | `OP_SEG`, `OP_LAZY` `OP_COMP` | fetched by `w_partfetch` (from the launch folder) at the first save, dropped with `os88_part_drop` after it |
| 3 | `PDRES` [0, 65,024) | `OP_ASSET`, `OP_LAZY` `OP_SELF` | never fetched by the standard: the primary reads the INDEX by file offset (20.6.2) |
| 4 | `PDRES` [65,024, end) | `OP_ASSET`, `OP_LAZY` `OP_SELF` | `PDRES.BIN` is 77,828 bytes and a part's ceiling is 65,024 (`os88pkg.py`, SPEC.md 20.12.11), so it rides as two rows cut at a multiple of 16: `os88pkg` lays parts on paragraphs in row order, so row 4's bytes follow row 3's in the file and every D3 offset is still `part_off` + offset. `pdu_bank_load` refuses the bank unless `off(4) == off(3) + len(3)/16` |
| 5 | the overlay | `CC_OVL_ROW` (lazy, `OP_COMP`) | `pdovl.c`'s `ovl_*`, last (SPEC.md 73.14.2) |

`pagedesk.asm` holds `pdu.h`'s `PDU_PART_*` to these rows with `%if`.
`PD_ENG_MOD`/`PD_REND_MOD`/`PD_WRT_MOD` (Makefile) name the module images and
default to the stand-ins in `apps/pagedesk/stubs/` (`pdstub.asm`, one source
three images) until the lead flips them; `PD_MATH_STUB=1` builds against the
math stand-in.

#### 20.6.2 Load, claims and teardown

**MEM_OWNER_MAX.** kern_big allows **8 claims per owner** (`kernel/memory.inc`,
12 on kern_mega), and the carve, the exchange block and every lazy part are
claims of the instance. 16.2's rows one claim each would be 13 at a save's
peak, and the first build failed its first open on exactly that. So the
document set is **two claims cut with `os88_seg_sub`**, every cut on a KiB
(each sub-segment 512-aligned for a transfer; on kern_mega each is a selector):

| claim | sub-segments, in order | size |
|---|---|---|
| A | block cache 16, object arena 8, xref + page tables 11, glyph cache 12 | 47 KiB |
| B | display list 22 + PDREND's gstate/font-cache workspace 7 (D13: `dl_kb` = 29), band (`((w+1)/2)*16` bytes at 4bpp, `((w+7)/8)*16` at 1bpp, to a whole KiB), then wave 2's rows (D15): edit store 16, undo 4, page order 1, thumbnails 4, text/search scratch 2, and the private module stack 2 (D23, 20.15) | 60-63 KiB (2 KiB band on CGA and Hercules, 5 KiB at 640 px 4bpp) |

and the claims at the peak are: the carve (primary + `PDENG` + `PDREND`), the
exchange block, the `PDRES` INDEX, A, B, the overlay, `PDWRT`, the writer's
buffers - **8**. Wave 2's edit store, thumbnails and search scratch (16.2) must
be carved into A or B (or a third claim that replaces one of these), never added
beside them.

**Launch** (`pdu_mod_launch`): check each eager module's header against its
row (magic, `PDM_ABI`, image + bss == the row's length) and bind its far vector
(`pdu_mbind`, `OS88_CODESEL`); claim the 1 KiB XB; INIT `PDENG` and `PDREND`
with `pdxb_init` carrying the XB, the vectors, adapter and bpp and **no**
document claims (D2). A refused header toasts `<MODULE>: wrong version` (21 characters at most, TOAST_MAX)
and the launch is refused; nothing is left claimed.

**Open** (first slice of `J_OPEN`): read the `PDRES` INDEX once per instance
(D3): the part's file offset comes from row 3 (paragraphs), the read is
`OSAPI_FILE_READ_AT` from the launch folder, cluster-aligned (the claim covers
the clusters the INDEX spans and the bank is a `seg_sub` at the part's first
paragraph), the directory must say `PDRS` version 1 and kind 7's length - read,
never assumed (D10) - must lie inside the 8 KiB read. A bank that does not load
is not fatal; Help > Document info says `Fonts: none (system face)`. Then claim
A and B, re-INIT `PDENG` and `PDREND` with them (D2), hand `PDREND` the
`pdxb_fontbank` (package name, launch place, `part_off`, INDEX segment and
length) with `PDV_REND_FONTBANK`, and step `PDV_ENG_OPEN`. A refused claim
fails the open with `Not enough memory: PageDesk needs N KB.` (N = A + B) and
frees whatever it had taken.

**Close** (`pdu_doc_release`): `PDV_REND_ABORT`, `PDV_ENG_CLOSE`, free A and B;
the INDEX stays for the next open. **Save**: fetch `PDWRT`, check and bind it,
claim the writer's buffers (18 KiB, D9/D18), INIT it, step `PDV_WRT_SAVE`; on any
outcome free the buffers, unbind and `os88_part_drop` the module.
**Quit** (`os88_onclose` and every refused launch): close, then free the INDEX
and the XB and unbind every vector. A freed segment never stays in
`pdxb_init`: the document fields are zeroed with A and B (on a failed open
too) and `wbuf_seg`/`wbuf_kb` with the writer, so a later INIT (D2) hands a
module 0, never a claim that has been given back.

**The document's folder.** `pdxb_open.name` and `pdxb_save.source` are 8.3
names resolved in the instance's CURRENT folder, and a Standard File dialog
moves that folder (SPEC.md 19.2.1). The open banks its folder
(`pdu_docplace`); every wake and every Find stand back in it first
(`os88_file_goto_q_mark`: a word, no I/O, inside one volume). Save As writes
beside the document only (20.5.12 item 2): a dialog answered in another
folder is refused, `Save As writes beside the document in this build.`, and
the instance is stood back beside the document. `pdxb_save` carries
`source` = the open document's name and `date` = `D:YYYYMMDDHHmmSS` from
`OSAPI_CLK_GET`, or "" when no hardware RTC was found (a PIT clock since
boot may never have been set); `author` is "".

Measured on QEMU (640 KB, kern_big, stub modules, the real `PDRES`), free heap
with a document open, from Help > Document info: **446-447 KB on VGA, 451 KB on
CGA, 450 KB on Hercules** (the band is 5 KiB at 4bpp and 2 KiB at 1bpp). On the
stub-`PDRES` build the figure read the same after open, close, reopen, a Save
As, quit and relaunch (458, 458, 459 KB), and on the final build a close
through the close box (`os88_onclose`) and relaunch read 447 KB both times: no
claim is leaked on any of those paths. The verifier's pass (22 KiB display
list, before D13 grew it to 29) read 427 KB on VGA with the Task Manager and
two Disk windows up, the same after close + Sample guide and after both Save
As refusals, and the Task Manager's HEAP read 21/580K before PageDesk's
launch and again after its close box; CGA read 448 KB before and after a
close and reopen, Hercules 448 KB.

#### 20.6.3 The job scheduler (15.1-15.3)

One job at a time, `pdu_job`: `J_NONE`, `J_OPEN`, `J_RENDER`, `J_SAVE`. The
document is `D_EMPTY`, `D_OPENING`, `D_OPEN` or `D_FAILED` (the empty card then
says `Could not open <name>` and the `PDE_*` sentence).

- `os88_onwake` does, in order: a pending cancel; the launch document
  (`OSAPI_ARG_FILE`, reached with `os88_file_goto_q_mark`); **one** slice of the
  running job; a pending page change (`PDV_ENG_PAGEINFO`, then the canvas); a
  pending full relayout and repaint; then, if no job runs and a render is owed and
  no popover or card is up, `PDV_REND_BEGIN`. It re-posts `os88_wm_wake` while a
  job, a page change or the launch document is outstanding.
- A slice is one verb call. `PDE_BUSY` means "call again next wake"; anything else
  ends the job.
- **Render is owed, not run**: damage inside the canvas is unioned into one
  owed rect (`pdu_render_want`) and rendered after the damage handler returns,
  in bands, under `PDREND`'s own lock (3.1, 10.2). A popover opening aborts the
  render (`PDV_REND_ABORT`) and the canvas under it is re-owed when it closes.
- **Esc** cancels the running job at the next slice boundary. A cancelled open
  closes and frees; a cancelled render leaves the page as drawn and says
  `Drawing cancelled - the page is incomplete.`
- **Progress**: after 18 ticks a job writes `Opening <name> - Esc cancels`
  (or `Drawing page`, `Saving <name>`) to the status line, once a second. A
  save opens `OSAPI_FS_OP` with the total `PDWRT` reports and closes it with 0.
- **Incomplete rendering** is said once a page (`PDV_REND_STATUS`'s first
  reason): a repair of a damaged strip draws the same page again and must not
  overwrite what the user just did - a Save As's outcome was lost exactly that
  way in the first build.

#### 20.6.4 Layouts and metrics

The layout is a pure function of the content box (`pdu_layout`), recomputed on
every `OSAPI_WM_ONRESIZE` and display change. Metrics (`pdu.h`): bar rows 16
(FULL/MID) and 12 (COMPACT); status line 12 / 10; Pages sidebar 136, Notes
panel 160; scroll bar 16; desk margin 8; the canvas's left edge and right
edge + 1 are on the 8-pixel grid (the `gfx_scroll` contract); a panel folds
away (Notes first) when the canvas would be narrower than 160. The status
line stops 16 short of the content's right edge, where the grow box is.

**`PDU_FULL_H` is 260, not 17.1's 300.** A full-desktop window's content box
is the screen's width by its height less 64 (menu bar, title bar, dock), so on
Hercules (720x348 or 720x350) and EGA (640x350) it is 284-286 rows; at 300
those machines would get MID, whose panels open as overlays, while the mockups
show Hercules with both panels beside the page. 260 still sends CGA (136 rows)
to COMPACT and leaves MID for a small window. The lead may move 17.1's figure
to match.

| display | content box | layout | canvas | panels | source |
|---|---|---|---|---|---|
| VGA 640x480 | 640x416 | FULL | 328x372 | Pages + Notes | MEASURED (QEMU, Document info) |
| EGA 640x350 | 640x286 | FULL | 328x242 | Pages + Notes | the rule; host test |
| Hercules 720x348 | 720x284 | FULL | 408x240 | Pages + Notes | MEASURED (QEMU `VIDEO=herc`, Document info) |
| Hercules 720x350 | 720x286 | FULL | 408x242 | Pages + Notes | the rule; host test |
| CGA 640x200 | 640x136 | COMPACT | 624x114 | popovers | MEASURED (QEMU `VIDEO=cga`, Document info) |

`tests/unit/t_pdlay.py` (fast row `pdlay`) compiles `pdulay.c` on the host and
sweeps every content box 320x100..720x460 in every mode plus these five rows.

COMPACT has one combined bar: the page field with Prev/Next, zoom, the four
mode tabs, the mode's tools (as many as fit) and the Pg and Notes popover
buttons; Open, Save As, Find, Undo/Redo and the panels are in the menus and
the popovers (Pages: a grid; Notes; Zoom list; More). Monochrome marks the
active control by inversion and focus by a double frame; colour by cyan.
Every shown panel has its own put-away control (Pages: `<` at the foot,
Notes: `x`) - `t_pdlay` checks it at every box.

**What a redraw costs.** Each laid-out control keeps the state and label it
last showed (keyed on id and position): unchanged is no call; a new label is
the slivers beside it and one `font_run`; a new state the four slabs round
the label, the frame and the label; on a ground its painter just filled,
frame and label. No pixel is written twice (the first build filled each
control and ran the label over the fill, so the page field blanked on every
PgDn). A page change moves the sidebar's heavy frame between two cells - or
pages the list by screenfuls when the page is off it - and redraws only the
Notes panel's per-page line, padded to the list's width.

#### 20.6.5 Menus and keys

Menus (`os88_menuset`, five at most - the application menu carries About):
File (Open... ^O, Open sample guide, New ^N, Save As... ^S, Close document),
Edit (Undo ^Z, Redo ^Y, Delete Del, Find... ^F, Find next F3, Find previous
Shift+F3), View (Zoom in +, Zoom out -, Actual size, Fit width, Fit page,
Hide/Show Pages panel, Hide/Show Notes panel, Preferences), Document (Read F5,
Annotate F6, Edit F7, Pages F8, the active one starred; Previous/Next/First/Last
page PgUp/PgDn/Home/End; Go to page... ^G), Help (Keyboard..., The XT PDF
profile..., Document info...). No Print (1.1). A disabled item is greyed by
fact (no document, nothing to undo, a wave-2 tool), and the control it mirrors
says why on the status line when pressed. Keys beyond 17.5: Tab/Shift+Tab move
focus through the bars, Enter/Space press the focused control, 1-6 pick the
mode's tools, the arrows scroll (and pan when zoomed past the canvas), Esc
cancels a job, then closes a popover, then leaves a field, then drops focus,
then returns to Select. Help > Document info shows the file, pages, version and
revisions, access, whether the page is fully drawn, the font INDEX, the layout
and content box, and the free heap.

#### 20.6.6 The disk

`make pagedeskdisk` writes `build/pagedesk{,120,720,360}.img`: `PAGEDESK.O88`,
`README.TXT`, `PDFONTS.TXT`, `PDFONTX.RES` (not at 360KB, D5) and the samples
(`apps/pagedesk/samples/*.PDF`, unpacked) at the root, `SYSTEM/APPDATA/`, each
verified with `os88disk.py --verify`. The everything set and the live media
carry the same files in `PAGEDESK\` (`tests/unit/t_livefull.py` has no
exemption for it). At 360KB the disk is 184 of 354 clusters.

### 20.7 Arithmetic and the assembly contract (`math`)

`apps/pagedesk/pdmath.inc` is the 8086 body of every function in `pdmath.h`;
`pdmath.c` is its host reference and `tests/pdmath/pdmath_diff.py` holds the
two to each other bit for bit. `apps/pagedesk/pdabi.inc` is GENERATED from
`pdabi.h` by `tools/os88pdabi.py --write` and `--check` (run by every `make`)
fails when they disagree.

#### 20.7.1 Calling convention

SmallerC cdecl (SPEC.md 73.3). Arguments are pushed right to left and the
caller removes them; the result is in AX; `void` functions leave AX undefined.
A function's label is its C name with a leading underscore (`_pdm_fx_mul`,
`_pd_fcopy_in`). BP, DS, ES, SS:SP and DF come back as they went in; AX BX CX
DX SI DI are the caller's to lose. Every struct argument is a NEAR pointer into
DS; a module's door has loaded DS = CS (3.1), so `pdmath.inc` reads its own
`.bss` flag and its CRC table through CS/DS with no further setup. A result
pointer may equal an operand pointer (`mat_mul`, `mat_apply` and the u32/fx
ops build the answer first). Two RESULT pointers of one call - `mat_apply`'s `x`
and `y`, the view's `px`/`py`, `x`/`y` and `w`/`h` - must be distinct: the
assembly stores `view_fwd`'s and `mat_apply`'s in the opposite order from
`pdmath.c`, so an aliased pair keeps a different one of the two. The far movers are the only code that loads DS
or ES; they restore both and leave DF clear. Nothing touches SS.

`pdmath.inc` is `%include`d by each assembly in its `section .text` after
`cpu 8086`; it needs `section .bss` to exist (the shim's bss is zeroed on
first entry, 3.1). The sticky overflow word `pdm_ovfw` is in THAT assembly's
bss: every segment keeps its own flag (a PDREND overflow is read through
PDREND's own `pdm_ovf`, or reported to the primary in `pdxb_rstatus`). The
include is guarded, so a second `%include` in one assembly is harmless; every
internal label is `pdmi_*`, the flag is `pdm_ovfw`, every constant is
`PDMI_*`, and the C-visible names are exactly `pdmath.h`'s. A segment that
needs only part of it defines before the include any of `PDM_SKIP_U32`,
`PDM_SKIP_PARSE` (fx_parse, fx_todec), `PDM_SKIP_MAT`, `PDM_SKIP_VIEW`,
`PDM_SKIP_FAR` and `PDM_SKIP_CRC`. Measured by nasm (`cpu 8086`, `-w+error`):

| configuration | bytes of `.text` |
|---|---|
| everything | 3,633 |
| core only (all six skipped: overflow flag, fx add/sub/mul/div/neg/cmp/int/floor/round/to_pt16/pt16_to_fx, fx_div's 48/32 divider) | 848 |
| the groups, by what each adds to the core | U32 403, PARSE 515, MAT 525, VIEW 806, FAR 371, CRC 165 (the 21-byte span check both use is in the core) |

Largest stack use of one call (return address included): `mat_mul` 50 bytes,
`mat_apply` 30, `fx_mul`/`fx_div`/`view_*`/`fx_parse` 18-22, the rest 4-6.
`tools/stkbalance.py` is clean on the file (no callee pops arguments; frames
are torn down with `add sp, N`; the two digit routines carry
`STKBALANCE-LOOP`).

#### 20.7.2 Semantics settled here (beyond 4.2 and `pdmath.c`'s header)

* **Overflow.** The flag is a word, 0 or 1, cleared only by `pdm_ovf_clear`.
  u32 add/sub/add16 saturate to 0xFFFFFFFF / 0 and set it; `u32_shl` sets it
  when any bit is shifted out (saturating to 0xFFFFFFFF; `0 << n` is 0, no
  flag); `u32_shr` with n >= 32 is 0; `u32_divmod16` by 0 sets it, stores
  0xFFFFFFFF and returns 0. fx results saturate to +-0x7FFFFFFF (the pattern
  0x80000000 is accepted as input and means -2^31; `fx_neg` of it, `fx_int` of
  -32768 and every sum/difference/product/quotient out of range set the flag).
  `fx_div` by 0 sets it and returns +0x7FFFFFFF, or -0x7FFFFFFF when the
  dividend is negative. `pdm_fx_parse`'s `PDE_RANGE` saturates and does NOT set
  the flag (the return code is its report).
* **Rounding.** `fx_mul`/`fx_div` truncate toward zero (magnitude, then sign).
  `fx_floor` is the high word. `fx_round` and `fx_to_pt16` round half away from
  zero; `fx_round` of a value >= 32767.5 saturates to 32767 and sets the flag
  (the low end cannot overflow: -32768 fits).
  `fx_to_pt16` saturates to +-32767 and sets the flag. `fx_parse` rounds half up
  in magnitude: at most 8 fraction digits count, the integer part saturates at
  40000, no spaces or exponents are allowed, a lone sign or point is
  `PDE_SYNTAX`, and a syntax error stores 0. `fx_todec` rounds to 4 fraction
  digits, drops trailing zeros and a bare point, prints a negative that rounds
  to zero as "0", and writes exactly the characters and the NUL (at most 12
  bytes); the u32 form writes at most 10 digits + NUL.
* **The view transform.** Any `rot` other than 90, 180 or 270 behaves as 0.
  Valid for every value its types hold: coordinates int16 (so each of u, v is
  at most 17 bits), zoom and `asp_num` 0..65535, `asp_den` 1..65535. The
  scaled offset is computed exactly (48-bit numerator) and floored; `inv`
  takes the ceiling, so a pixel maps to the smallest pt16 that lands on it.
  Anything with a magnitude of 65536 or more is clamped before the origin or box
  edge is added, which changes no result (it lands outside int16 either way);
  the final clamp is to +-32767 and sets the flag. `pdm_view_inv` is an exact
  inverse of `pdm_view_fwd` for every pixel a view of at most one pixel per
  pt16 unit can reach (`zoom <= 1600` and `zoom * asp_num <= 1600 * asp_den`),
  provided nothing clamps; a coarser view cannot reach every pixel, so no
  inverse exists (the differential test checks this property on the assembly).
  Degenerate views are DEFINED on the target and undefined on the host (a
  division by zero): `asp_den = 0` in `fwd`/`size` and `zoom = 0` or
  `asp_num = 0` in `inv` set the flag and give 0 for the affected coordinate.
* **Far memory.** A span is refused (the function returns 0 and touches
  nothing) when `n > 0xFFF0` or `off + n > 0x10000`; `off + n == 0x10000` is
  legal. A successful call returns `n`, so `n = 0` is indistinguishable from a
  refusal: do not branch on the result of a zero-length call. `pd_fcopy` is a
  `memmove` in LINEAR address space (`seg*16 + off`): overlapping or aliased
  source and destination, from different segment:offset pairs, copy correctly.
  `pd_fpeekw`/`pd_fpokew` at offset 0xFFFF are outside the contract (a word
  access across the segment end). `pdm_crc16_far` answers its `crc` argument
  unchanged for a refused span.
* **CRC.** CRC-16/CCITT, polynomial 0x1021, MSB first, initial value the
  caller's; a 16-entry nibble table in `.text` (32 bytes).

#### 20.7.3 Cost on an 8088 (ESTIMATED)

`tests/pdmath/pdcycles.py` runs each routine in the x86 engine and costs every
executed instruction as `max(4.34 x bytes, clocks)` (PERFORMANCE.md Part 2's
fetch floor), with MUL r16 at 125 and DIV r16 at 155 clocks and +9 for an
effective address. It is an instruction-count estimate, not a measurement on a
5150; at 4.77 MHz one clock is 0.2096 us. The call and its return are included.

| routine | instructions | clocks | microseconds |
|---|---|---|---|
| `fx_add` | 19 | 321 | 67 |
| `fx_mul` | 63 | 1,374 | 288 |
| `fx_div`, divisor below 1.0 (three DIVs) | 65 | 1,361 | 285 |
| `fx_div`, an integral divisor of 1.0 or more (two DIVs) | 46 | 892 | 187 |
| `fx_div`, any other divisor of 1.0 or more (32 shift-subtract steps in registers) | 395 | 4,727 | 991 |
| `view_fwd` (any aspect) | 166 | 3,884 | 814 |
| `view_inv` | 185 | 4,155 | 871 |
| `view_size` | 169 | 3,894 | 816 |
| `mat_mul` | 823 | 17,371 | 3,641 |
| `mat_apply` | 273 | 5,849 | 1,226 |
| `fx_parse("-123.456")` | 450 | 6,234 | 1,307 |
| `fx_todec(1.5)` | 87 | 2,054 | 430 |
| `crc16`, 64 bytes | 1,098 | 16,669 | 3,494 (~55 us a byte) |

`view_fwd` costs the same at every aspect: the y scale divides by `1600`, then
by `asp_den`, as two three-DIV chains (floor of a floor is the floor of the
product), so no aspect reaches the shift-subtract divider; only `fx_div` by a
NON-integral divisor of 1.0 or more does (an integral one, low word 0, is
`|x| / hi` in two DIVs: `fx_int(1000)` costs 187 us, not 991, so a constant
divisor should be integral - or, in a per-glyph loop, a multiplication by its
reciprocal computed once).

A second model (the verifier's: the Intel manual's clocks with the 8088's +4
per word memory transfer on every form, Jcc/LOOP priced taken or not taken,
shift-by-CL at its actual count) agrees with the table within 6% on every row
except the 32-step divider, which it puts at 855 us (the table's model prices
every conditional jump as taken). A hit test or highlight loop over N glyphs is N x 0.8 ms
per corner pair: the renderer should transform a page rect's two corners once
and step in device space, not call `view_fwd` per pixel.

#### 20.7.4 What holds it to its reference

`make pdmathtest` (or `python3 tests/pdmath/pdmath_diff.py [-n CASES]`):
assembles `tests/pdmath/pdmharness.asm` (the shipping include plus an entry
table) under `cpu 8086 -w+error`, runs it in Unicorn's real-mode x86 with SS
different from DS and sentinels in BP/ES, and runs `pdmath.c` natively through
ctypes on the same inputs: edge-value pools plus a seeded stream per function,
2,500 cases each by default, after a FIXED edge pass of about 26,000 cases run
every time (every pair of 24 boundary 16.16 values through add/sub/mul/div/cmp,
rounding and pt16 ties, the parser's edge strings - lone sign or point, 9+
fraction digits including ones where an ignored 9th digit would change the
rounding, leading zeros, the 32767.99999 and -32768 boundaries, junk - u32
shifts by 0/16/31/32/33/FFFFh, divmod by 0, PageDesk's five aspects (5.4) at
every rotation, zoom 25..400 edges and past 400, spans ending exactly at the
segment's end, and 64 KB moves overlapping in both directions within and across
segments), including the saturation edges of `fx_mul` and
`fx_div`, the clamp edges of the view, string fuzzing for `fx_parse`, and
far spans at, across and 64 KB past the segment end. It compares the return
value, every byte of argument memory (guard bytes included), the overflow flag,
the far arena, and the convention (BP/DS/ES/SS/SP/DF). Seven negative controls
prove those checks can fail, an include-twice / every-`PDM_SKIP_*` assembly
matrix runs, the `fwd(inv(p)) == p` property is checked on the assembly, 5.3's
own claim is checked on it literally (every pixel of a 200-pixel row and column
through the page's top-left, every R, every 5.4 aspect, zoom 25/33/100/400, two
nonzero-origin cropped boxes: 64,000 pixels), and `pdm_crc16` on both sides is
held to the published check values of "123456789" (CCITT-FALSE 29B1h, XMODEM
31C3h).
Mutating the include (a changed rounding constant, a dropped carry, a wrong
comparison) fails the run. It needs the `unicorn` Python package and exits 77
(skip) without it.


### 20.8 The edit store and undo (`edit`, wave 2)

Pinned by the lead before wave 2 so `edit`, `notes` and `pages` build against
one interface (DECISIONS D15). `edit` owns `apps/pagedesk/pduedit.c` and
`pduundo.c`; every other agent calls only these, never the claim directly.

| call | contract |
|---|---|
| `int pdu_ed_count(void)` | records in the store, deleted ones included |
| `int pdu_ed_get(int i, struct pd_edit *e)` | copy record `i` out; 0, or -1 for a bad index |
| `int pdu_ed_new(struct pd_edit *e, const char *text, unsigned tlen, const pd_pt *pts, unsigned npts)` | append; assigns `e->id`, sets `PDEF_DIRTY`, stores text and points; the new index, or -1 with `pdu_ed_err` = `PDE_NOMEM`/`PDE_LIMIT` (the typed text is kept by the caller, 13.1) |
| `int pdu_ed_set(int i, const struct pd_edit *e)` | replace the fixed fields (geometry, colour, style, state) |
| `int pdu_ed_text(int i, const char *text, unsigned tlen)` / `unsigned pdu_ed_text_get(int i, char *dst, unsigned cap)` | the record's text heap entry |
| `int pdu_ed_pts(int i, const pd_pt *pts, unsigned npts)` / `unsigned pdu_ed_pts_get(int i, pd_pt *dst, unsigned cap)` | the point heap entry (13.1.1's orders) |
| `int pdu_ed_del(int i)` | sets `PDEF_DELETED`; a record replacing a SOURCE annotation stays as the deletion |
| `int pdu_ed_copy_src(unsigned obj, unsigned gen, struct pd_edit *e)` | copy-on-edit of a source annotation (13.1): finds or creates the record naming it |
| `int pdu_ed_first(unsigned page)` / `int pdu_ed_next(int i, unsigned page)` | live records on a page id, in z-order; -1 at the end |
| `unsigned pdu_ed_seg(void)` | the segment for `pdxb_render.edits_seg` / `pdxb_save.edits_seg` (records at 0, heaps after, 13.1.1) |
| `void pdu_undo_begin(int label)` / `void pdu_undo_end(void)` | bracket one user command; nested calls coalesce |
| `int pdu_undo(void)` / `int pdu_redo(void)` | 0 = done, -1 = nothing to do |
| `int pdu_undo_can(void)` / `int pdu_redo_can(void)` | greying (17.3) |
| `void pdu_undo_pageop(const void *delta, unsigned len)` | a page operation as a command (13.6, 13.7): `pages` supplies the delta and its own apply/revert through `pdu_pg_apply(const void *, unsigned, int undo)` |
| `int pdu_dirty(void)` / `void pdu_saved(void)` | dirty since the save point; Save calls `pdu_saved` on success |

Every mutating call records its undo delta itself, so a caller brackets a
command with `pdu_undo_begin`/`end` and does nothing else for undo. Calls are
made on the UI task only, never inside a module slice.

#### 20.8.1 As built (`edit`, wave 2)

**Files.** `pduedit.c` (the store, the selection outline, typing), `pduundo.c`
(the history), `pdutool.c` (the annotation tools: every function an `ovl_*`);
`hosttest/pdu_ed_test.c` compiles the first two UNCHANGED on the host
(`tests/unit/t_pded.py`). Everything but the hot path rides the overlay
(SPEC.md 73.14): `pdu.h` renames the 20.8 calls with `#define pdu_ed_new
ovl_ed_new` and so on, so every caller keeps the pinned name and reaches the
overlay's resident shim. Resident: `pdu_ed_seg`, `pdu_ed_count`,
`pdu_ed_selected`, `pdu_ed_init`, `pdu_ed_gone`, the outline's off/on hooks,
`pdu_dirty`, `pdu_saved`, `pdu_undo_can`, `pdu_redo_can`.

**What it costs the primary** (measured at the cut, against the base before
`edit` of 40,650 image + 3,969 bss = 44,619): **43,146 + 5,515 = 48,661 B**,
+4,042 against a share of +4,096; the overlay 14,821. The near scratch is one
set shared by the three files - `ed_pts` (96 points: a Line's two, a
Highlight's quads, a page delta's copy in the history), `ed_xr` (a clip rect,
and the history's 8-byte swap) and the three 48-byte record buffers - and
everything bigger lives in the claims: an Ink or Sign stroke is captured
STRAIGHT into the EDITS segment's gap below the point top and converted there
in place - converted, then turned round so the first point drawn is the first stored (`pdu_ed_new` with `pts` NULL means the points are already in place),
and the history's command table is in `PDU_DC_UNDO` after its entries. Nothing
in `edit` recurses, and its widest frame is 16 bytes of locals (`ovl_ed_set`; D21).

**The segment** (`pdu_dcseg(PDU_DC_EDITS)`, 16 KiB, never zeroed; `pdu_ed_init`
sets the three tops when a document's claims exist):

| bytes | what |
|---|---|
| 0 .. 6,143 | 128 records of 48 bytes, in z-order (index = z; a later record is on top) |
| 6,144 .. `text top` | the text heap, grows up (one text is at most 1,024 bytes) |
| `point top` .. 16,383 | the point heap, grows down (one record at most 2,000 points) |

13.1's 256 records and swap file are NOT built: 128 records, and the 10 KiB the
two heaps share, are refused with `PDE_LIMIT` / `PDE_NOMEM` (`pdu_ed_err`) and
the caller keeps what was typed. A heap is a bump allocator. A replaced text or
point list is a new block and the old one is abandoned (no compaction); typing
extends the tail of the LAST text block in place, and a backspace gives that
block's last byte back to the heap **when the current typing run wrote it** (a
byte below the top the run began at is old text an undo restores, so it is
only dropped from the length) - so typos cost nothing and only retyping into
old text copies the block, which is the only reuse. **`rect` is the tight bounding box of the points** for every
point-based kind (Highlight, Ink, Sign, Line); the renderer pads it.

**Defaults** (13.3): Note 20 x 18 pt centred on the click, yellow; Highlight
yellow; Area mark light red, 1 pt border; Text blue, Helvetica 12, left;
Ink red, 2 pt (`size` 4); Sign black, 1.5 pt (`size` 3); Line red, 1 pt, **arrow
at the end** (`align` 2; there is no plain-line gesture yet - the inspector
clears the bit); Rect and Ellipse red, 1 pt.

**Undo** (`pduundo.c`). A command is one `pdu_undo_begin`/`end` bracket (a
mutating call outside one is its own command). 64 commands over a ring of 256
twelve-byte entries in `PDU_DC_UNDO` (bytes 0..3,071; the 64 commands'
first-entry and entry-count words follow at 3,072..3,327). Every entry is an involution (undo and
redo are the same swap, walked in opposite orders): a FIELD swaps up to 8
bytes of a record, TEXT and PTS swap a record's offset/length and a heap top,
CREATE swaps the store's count and both tops, MOVE stores the translation (its
undo is the negation, exact in pt16). Three things are not logged and why that
is right: a change to a record born in the same command (undo truncates the
store below it; redo restores the count and the record is back as it ended,
because **truncation never erases a byte**), the second edit of a field in one
command (the first old state is the one that matters, so a drag of fifty sets is
one step - and a MOVE of a record folds into that command's last move of it only
while no rectangle or points entry of the record came after that move, since a
move does not commute with them), and a keystroke into the record the previous command typed into
(coalescing, 13.6). A new command discards the redo branch. The oldest command
falls off at the 65th and the status line says so once until the next save
(`Undo history full: the oldest step is dropped.`); a command that cannot fit
at all clears the history and says `Too large to undo: the history was
cleared.`

**Dirty** is "not at the save point". The save point is an absolute command
number: undo back to it is clean, redo past it dirty, a new edit made from
below it forgets it (that state is on a dead branch), and a history that has
run past it never matches. Save calls `pdu_saved()` on success only, so a
failed save stays dirty. The title's `*` and the greying of Undo/Redo follow it.

**Page operations** (`pdu_undo_pageop`): the delta (at most 96 bytes) is chunked
into the log and handed back to `pages` through `pdu_pg_apply(delta, len, undo)`
- 1 on undo, 0 on redo - in order with the annotation commands. `pduundo.c`
carries a stub of `pdu_pg_apply` guarded by `#ifndef PDU_PAGES`; `pages`
defines `PDU_PAGES` (pdu.h's pages block) when it lands its own. A delta over
96 bytes clears the history (said, not hidden).

**What a mutation does to the screen.** Every mutating call hands the screen
rectangle of the record before and after (through `pdm_view_fwd`, padded 6 px,
cut to the canvas) to `pdu_render_want`, which keeps the union - nothing renders
before the next wake, so a command is still ONE repaint of the union of its
marks; the outermost `pdu_undo_end` refreshes Undo/Redo, the title,
`pdu_changes` (the live count) and the menus. So a caller of 20.8 never asks for a repaint. The primary
draws no annotation pixel (D15).

**The selection** is an XOR outline with a 4 x 4 handle just OUTSIDE each
corner on the shapes that resize (Text, Rect, Ellipse, Area mark) - outside, so
no pixel is flipped twice. The strips actually drawn (8 at most) are
remembered and taken off by replaying them; a render's BEGIN takes the outline
off first, but only when one of its strips meets the render's rectangle (the
page under it is still the page it was drawn on), and its END puts it back;
`pdu_canvas_paint` takes it off before it paints (a closed dialog's damage) and
puts it back itself when no render will; scrolling, zooming and a page change
take it off before they move anything. `Esc` deselects; `Delete` deletes the selection (one undoable
command); Enter on a selected text box opens it for typing. `pdu_ed_gen()` is bumped on every mutation, undo, redo and selection change (D19, for `notes`); `pdu_tbuf[1025]` is the one near text buffer shared with notes.

**The tools** (`pdutool.c`; Annotate and Edit modes, never Read or Pages, never
on a signed document). A gesture is modal: the OS delivers a press and a
release but no motion (13.7), so it polls `os88_mouse()` and yields, holding
the gfx lock until the button comes up (CWORD's menu and ruler do the same),
bounded by a MINUTE of ticks - not a pass count, which ended a drag in well
under a second on a fast machine. It draws an XOR band and takes it off again
(an Ink or Sign stroke is left for PDREND's opaque bands to paint over, D4, so
a stroke is not drawn twice; a refused stroke repaints its own strip); the
record is made at the release,
converted ONCE from screen to page space with `pdm_view_inv` (5.1), so zoom,
scroll and rotation never touch a stored byte.

| tool | gesture | result |
|---|---|---|
| Select | click a mark (a shape by its border, a note/text by its box, Ink/Sign/Line/Highlight by their bounding box); drag it; drag a corner handle; double-click or Enter a text box to type | moves by an exact pt16 translation, kept on the page; resize re-converts the corner |
| Note | click | `pdu_nt_new(page, &rect)`: notes' popover (20.9) makes the Note (20x18 pt centred there) on Done with text; double-click a note calls `pdu_nt_open` |
| Text | drag a frame (any direction), or click for 144 x 36 pt; click an existing box to edit it; a click in the margin is refused (`Click on the page.`) | Text record, then typing |
| Highlight | with a selection, press Highlight | one record, up to 12 quads (`pdu_sel_quads`, 4 points each), `rect` = their union |
| Mark / Rect / Ellipse | drag | Area mark / Rect / Ellipse over the dragged frame (4 pt minimum) |
| Line | drag tail to head | Line with an arrow at the end |
| Ink | draw with the button down | one Ink record per stroke: a point only when the pointer is 3 px from the last kept one, at most 126 per stroke (said when reached) |
| Sign | draw one stroke | one Sign record placed on release; the status line says 13.9's `Drawn signature. This is not a certificate signature.` A multi-stroke capture is not built |

**Typing** (`pdu_tx_*`): while a note or text box is open the printable keys,
Backspace and Enter (a new line, LF) go to it; every other key and every command
ends the session. Each keystroke is a command that coalesces into the one
before, so one Undo removes a freshly made box with all its typing, and one
Undo takes back a whole later typing run. 1,024 characters is the limit and is
said; the typed text is never dropped.

**Not built / ASKs.** (1) Source annotations are not hit-tested - `pdu_ed_copy_src`
exists and is tested, but a click on one needs `PDV_ENG_ANNOTS` per page, which
is `notes`' list; it calls `copy_src` then `pdu_ed_set`/`pdu_ed_move`/`pdu_ed_del`.
(2) The Highlight tool's text drag is `nav`'s: its selection gesture, when the
tool is Highlight, ends by calling `pdu_tool_hilite()`; until nav lands `pdu_sel_quads` answers 0 and the status
line says `Select text first.`. (3) Line-end handles, a plain (arrow-less) Line
gesture, text caret positioning and property editing are not here; properties
are `notes`' inspector through `pdu_ed_set`. (4) Page ids are view indices until
`pages` lands `pdu_pg_id` (`pdu_ed_pg()` is the one line to change). (5) Esc
does not cancel a gesture while the button is down: no key event is delivered
inside the held-button loop and `os88_key_down` would arm the kernel's key map
for the whole session; Undo takes a finished gesture back in one step.

**As measured (2026-11-05, QEMU VGA with the stand-in PDREND, `LATIN.PDF`;
verify-edit).** An Area mark, a Line dragged up-left, a Rect, an Ellipse, an
Ink stroke, a Sign stroke and a Text box typed `Hi there`, two backspaces,
`re`, Enter, `Ok`; the Rect selected, moved by its border, resized by its
lower-right handle, undone and redone; Save As into `LATIN2.PDF`: `qpdf
--check` passes and `mutool draw` shows all seven (/Square x2, /Line with
`/LE [/None /OpenArrow]` at the release end, /Circle, /Ink x2 - the Sign at
`/BS /W 1.5` with `/Subj (Drawn signature)` - and /FreeText `(Hi there\nOk)`)
where they were placed; the title's `*` goes at the save. Note and Highlight
wait for `notes` and `nav`. Sizes: primary 43,398 + 6,239 bss (+4,999 over the
44,619 base, of which 1,025 is `pdu_tbuf`, which `notes` drops from its own
bss: +3,974 net of it); overlay 14,248 bytes against D19's +12 KiB share
(14,547).


### 20.9 Notes & edits, the note popover and Text properties (`notes`, wave 2)

Stories PD-12 (notes), PD-14 (the inspector) and PD-16 (the changes list);
13.4's review state. Files: `pdunotes.c` (resident state, pure model and hooks),
`pdovlnotes.c` (the list, cards, Resolve, jump-to, the note popover - all
`ovl_nt_*`), `pduinsp.c` (Text properties - control table resident, functions
`ovl_nt_*`), and
`hosttest/pdu_nt.c` + `tests/unit/t_pdnotes.py` (the model, on the host).

#### 20.9.1 The list

A virtual list: no card is kept. `ovl_nt_step` walks the store with one int of
state and returns the next item - an edit-store index, or `PDU_NT_SBASE` + a
slot of the source cache - and `ovl_nt_seek(k)`/`ovl_nt_find(id)` are the
walk run to item k / to an id; the panel paints `vis` rows from `top`. Three
filters on `pdu_npall`, cycled by the filter button: **This page** (records on
the page id, then the page's source annotations), **All pages** (every record,
then the shown page's source annotations - the source of other pages is not
read, which would be a disk walk per page; so this filter follows the page
too), **Changes** (every record the store
holds, deleted ones and review replies included, each with its word: Added,
Edited, Moved, Deleted, Resolved, Reopened; no source annotations). Deleted
records and review replies are in no other filter.

**Source annotations** (`ovl_nt_srcload`, once a page, when the panel or the
popover shows it and no job runs - an `ANNOTS` call moves the engine's object
state): the first 32 `/Annots` elements through `PDV_ENG_ANNOTS`, at most 8
listed (Text, Highlight/Underline/Squiggly/StrikeOut, FreeText, Ink, Line,
Square, Circle - `Link`, `Popup`, `Stamp`, `Widget`, `Caret`,
`FileAttachment`, `TrapNet` and the rest are not), each with its
object number, rect and the first 17 characters of `/Contents`; a reply
(`/IRT`) is not listed and only sets its parent's state. The load runs from
the page change (`pdu_page_load`) or the wake, **never from a paint**: a module
verb at the bottom of a paint is the deepest stack PageDesk could build (D21),
so a paint that finds the cache of another page empties it (the list never
shows another page's annotations, and a selected source slot is dropped) and
asks the wake (`pdu_nt_srcwant`), which loads it and repaints the rows that
changed. The answer's head comes into `pdu_line` and the 17 characters straight
into the cache - not `pdu_tbuf`, which may be holding `edit`'s open text box. A source annotation
that a record replaces (`src_num`, 13.1's copy-on-edit) is listed once, as the
record.

**Review state** (13.4): resolved or reopened is a reply record: `PDA_NOTE`,
`irt` = the parent's `id` (or, with `PDEF_IRTSRC`, its source object number),
`state` `PDS_COMPLETED`/`PDS_NONE`. The newest reply wins, and a PageDesk reply
is newer than one the source carries; up to 12 replies are kept for the lookup
(`ovl_nt_sync`, one pass over the store when its count, the `pdu_ed_gen()` poll's
generation, the page or the filter changed). `irt == 0` means "no parent":
record ids are non-zero.

#### 20.9.2 The panel

Under the shell's header (title, close box, filter): cards 146 x 36 on a
38-pixel pitch (title "Note - Page 3", the first 17 characters, then a 12-row
band with the status word and, on a note, Resolve/Reopen), a strip
`<  1-4/9  >` that pages by a screenful, and the Text properties button (grey
until a text box is selected). A card's interior is 18 cells wide, so no sliver
is written beside a row. Selection inverts the card (`pdu_cact`; black on 1bpp).
**Click** a card: select it, jump to its page (`pdu_goto`; the wake loads it,
`pdu_notes_page` then reveals the rect) and tell `edit` (`pdu_ed_select`);
click the selected note again: its popover; the selected text box again: the
inspector; the card's button: Resolve/Reopen as one undo command.

**Row pictures.** Every visible row (up to 12), the strip and the Text
properties button keep one word saying what they last showed - for a row a
hash of its title, preview and status word with the selection, the review
state and the button folded in; 1 for a blank row - and any repaint of the
list compares before it draws: an unchanged row is no call at all. So a
selection change paints two cards, a resolve one, a page change only the rows
whose item moved, a `pdu_ed_gen()` change the rows it touched, and none of them
the panel's chrome. The words are forgotten when the ground under the list was
just filled (the panel's chrome, the popover) or its geometry moved. A button
is its frame, the four slabs round its caption and the caption in one
`font_run` (20.6.4's rule), so a toggled state does not flash the label.

COMPACT: the Notes popover is the same list, one line a row (`Note p3 ok: the
first words`), the same strip, a click picks, jumps and opens the note's
popover or the inspector; Up/Down scroll, Enter/Esc close. A scroll or a store
change repaints the list's rows over what they showed; only opening the popover
fills it.

#### 20.9.3 The note popover

The CGA mockup: a title and `n/1024`, the last four 32-column lines of the text
with a cursor, and Resolve/Reopen, Delete, Done. A bounded field: 13.1's 1,024
bytes, a refused key says so and keeps what is typed; Esc discards the typing;
Enter, Done, a click elsewhere or any other way of closing keeps it. A key
redraws the counter and the one or two lines it touched (the character and the
cursor); the four lines only when the text scrolls. **A new note
is not in the store until it has text** (`pdu_nt_new(page, &rect)`, the Note
tool's call): an empty or cancelled note is no change and does not dirty the
document. The record is created with `pdu_ed_new` in one undo bracket, colour
`PDC_YELLOW`; an existing note's text is `pdu_ed_text`. A note that came with the
file is listed, jumped to and resolved but its text is the source's. A full
store refuses Done with the popover still open and the text in it; a close by
a click elsewhere cannot be refused (`pdu_pop_close` has many callers), so
there the refused typing is lost with a toast - the one gap in 13.1's rule.

#### 20.9.4 Text properties

Family (Helvetica, Times, Courier), size (8 9 10 11 12 14 16 18 24, - and +),
Bold, Italic, colour (Black, Blue, Red, Green, Magenta, Brown, Gray; a record
in any other `PDC_*` shows its own colour labelled `Other`, and C moves it to
Blue), alignment
L/C/R, Delete and Done, for one `PDA_TEXT` record: in the panel's place (FULL,
MID) or a popover (COMPACT, a hidden panel). Each control is one command -
`pdu_undo_begin`, `pdu_ed_set`, `pdu_undo_end` - and the box is rendered again
by its own rect (`pdu_render_want`). Keys: Esc/Enter Done, Del delete, B I + - F C
L M R. `style` = family<<4 | bold | italic<<1 as 13.1.1. In the panel the
inspector's keys yield to the page field while it is being typed in (and, once
`edit` lands, must yield to its open text box: 20.9.5).

#### 20.9.5 What the other agents call

The panel polls `unsigned pdu_ed_gen(void)` (D19: `edit` bumps it on every
mutation, undo, redo and selection change) when it paints and on the wake, and
repaints only the rows that changed (20.9.2's row pictures); `edit` never
calls `notes`. The note text is `edit`'s `char pdu_tbuf[1025]`.
`edit` calls `void pdu_nt_new(unsigned page_id, const struct pd_rect *r)` (the
Note tool), `void pdu_nt_open(int i)` and `void pdu_nt_inspect(int i)`: real
resident functions in `pdunotes.c` with exactly the prototypes `edit`'s block of
`pdu.h` declares, so the two blocks may sit in either order, and notes' block
defines `PDU_NOTES`, which switches off `edit`'s stand-ins in `pduedit.c`.
`pdu_ed_select(i)`/`pdu_ed_selected()` are `edit`'s, as are non-zero record ids.
Notes reaches the store only through 20.8 (`edit`'s `pdu.h` block; the
stand-in store `pdunt.h` went at the merge with `edit`), brackets each command
with `edit`'s own undo labels (`UL_NOTE` for a new note and a review reply,
`UL_TYPE` for a note's text, `UL_DELETE`, `UL_PROPS` for an inspector control),
and its key hook in `os88_onkey` is `pdu_tx < 0 && pdu_nt_key(...)`: an open
text box of `edit`'s keeps B, I, C, L, M, R, + and -.

**Sizes (the lead's grant, 22:55):** notes' shares are 2 KiB of the primary
(image + bss) and 11.5 KiB of the overlay over the wave-1 base (40,650 + 3,973;
overlay 2,259). Measured at 983d3bc1b, less the stand-in store that went at the
merge: **+2,030 B resident (1,441 image + 589 bss) and +11,775 B of overlay**.
About 0.8 KB of the resident figure is string literals the overlay code names
(SPEC.md 73.14 keeps them resident).

### 20.10 Navigation, selection, Find, thumbnails, outline and links (`nav`, wave 2)

Files: the primary's `pdunav.c` (history, gestures, links, the idle ladder),
`pdusel.c` (selection, Copy), `pdufind.c` (Find), `pduside.c` (the sidebar and
its thumbnail job), `pdovlnav.c` (the Outline, in the overlay); PDENG's
`pdesrch.c` (SEARCH) and `pdelink.c` (links). Everything the primary does
that is not on a keystroke's path is an `ovl_*` function (2.2, 73.14): of
nav's compiled code the resident part is the glue (the press, the release and
the poll, the hooks the render job and the page load call, the greying
tests), the rest rides in the overlay. Host checks: `make pdnav`
(`hosttest/pde_srch.py` differential-tests SEARCH against a reference over
random pages and piece sizes; `pde_link` lists the sample guide's links).

#### 20.10.1 Find is a job; SEARCH is the engine's matcher

`J_FIND` is a resumable job stepped by the wake handler, one `PDV_REND_TEXTMAP`
slice a wake (a text pass, 20.3.4: 74 `pdtm_glyph` records at most) and the
`PDV_ENG_SEARCH` calls that consume it. PDREND does not know PDENG's matcher
and PDENG has no vector to PDREND, so the primary carries the one to the other,
and **the exchange block is the pipe**: the text map's `dseg:doff` is the XB at
offset `PDE_SRCH_PIECE` = 128 (`dmax` 896), so a piece is not copied twice.
`pdxb_search` is used as pinned, with these meanings:

| field | in | out |
|---|---|---|
| `needle` | PD Latin, read when `state` = 0 | |
| `page` | the page the piece belongs to | |
| `glyph` | skip records whose `run` is below it (they were matched already, or are before the point a Find-next resumes from; they still feed the blank rule, never the matcher) | the run of the hit's first glyph |
| `state` | 0 = a NEW page stream (the matcher is reset), 1 = the next piece of the same page | 1 |
| `found` | the NUMBER of records in the piece (<= 74) | 1 = a hit completed in this piece |
| `glyph_end` | | the run of the hit's last glyph |

A hit ends the call; the primary calls again with the SAME piece and `glyph` =
`glyph_end + 1` until `found` = 0 (the matcher resets on a hit, so hits never
overlap). A piece with no hit is consumed whole. The matcher is
case-insensitive over PD Latin (ASCII and the Latin-1 letters fold), keeps the
last 64 symbols with their runs (so a hit's FIRST glyph is known without a
table), and takes a needle of up to 63 symbols with leading and trailing blanks
dropped. **Blanks are made**: a justified line moves its words with TJ numbers
and a line ends with no space at all, so a glyph a line apart (`dy` > half the
previous em) or a fifth of an em after the previous one counts as preceded by a
blank, and runs of blanks collapse (the primary's `pdu_gapk` is the same rule,
and Copy writes CR LF for a line and a space for a gap by it). A hit is a page
and two glyph runs: it becomes the SELECTION (20.10.3), which is how it is
shown, scrolled to and copied.

Enter (from the top of this page), F3 / Find next (after the selection,
wrapping past the last page, and once more over the start page for what the
resume skipped) and Shift+F3 (the LAST hit that starts before the selection,
wrapping the other way: the page is scanned forward and the last candidate
kept) all start the job from the idle ladder; Esc cancels it at a slice. A
second after it starts the status line says which page it is on (15.3). A page
is searched from its start every time: a page's map is never kept (9.2).

#### 20.10.2 Links (`PDP_HASLINKS`)

`PDV_ENG_PAGEINFO` sets `PDP_HASLINKS` when one of the page's first 64
annotations is a `/Link`. A link is the `PDV_ENG_ANNOTS` record, with these
meanings for `subtype` "Link": `state` = 0 (no destination in this file) or
the destination page index + 1; `nm` = the action's `/S` name ("GoTo", "URI",
"Launch", ...) or "Named" for a `/Dest` the engine could not resolve;
`contents_len`/`contents` = a `/URI`'s length and first 159 characters. A
destination is `[page ...]` (direct, as `/Dest` or as `/A /D`) or a NAME looked
up in the catalog's `/Dests` dictionary (PDF 1.1); the name TREE of later files
is not read. **Nothing is ever run** (1.4): a click that lands on a link goes
to its page (and is history, 20.10.5), a `/URI` is shown on the status line
("Link: http://... - PageDesk never opens links."), and a `/Launch`,
`/JavaScript` or `/Named` action is named and refused. The primary fetches a
page's links when the page loads, into the SCRATCH claim (42 links of 12 bytes:
rect, destination, annotation index), and a press tests them with
`pdm_view_inv` and no disk read.

#### 20.10.3 Selection and Copy

A selection is a page and a range of glyph runs (`lo..hi`, inclusive); its
boxes are derived and its text is not kept. The press tests links first, else
starts a gesture (20.10.5): a rubber band is XORed while the button is down,
and on release ONE text pass (`J_TEXT`, mode PICK) takes the two points to their
nearest glyphs (a line weighs four times a column, so the nearest line wins
before the nearest letter) in content order, then QUAD computes one box a line
(UL UR LL LR, 13.1.1's order, page space, pt16) into the SCRATCH claim (96
quads of 16 bytes) - a double-click grows the pick to its word (WORD) first.
Select all (Ctrl+A) is `lo` 0 .. `hi` 0xFFFF. **On the glass** a selection is XOR
inversion over its boxes (17.4: never saved), drawn only when the page under it
is whole. A render of a region wipes the XOR there, so the render job calls
`pdu_sel_after(&rect)` with the rect it finished and the XOR is put back inside
it; the selection is taken off the glass by XOR when nothing is being drawn and
by repainting its boxes when something is. The gfx lock is NOT re-entrant: the
XOR is drawn under the caller's lock and the wake handler takes it around the
call.

**Copy** (Ctrl+C, Edit > Copy; 12) runs a COPY pass of its own: the selected
glyphs as Windows-1252 bytes, CR LF between lines and a space for a gap, into
a heap claim of three bytes a glyph (the THUMB claim's 4 KiB when there is no
claim to be had - its pictures are then drawn again lazily - and truncated,
saying so; an aborted Copy gives back whichever it held), then `OSAPI_CLIP_PUT_SEG` and
the claim is freed. Over 32 KiB it is refused whole with the reason. This is the
one transient claim nav makes (ASK: it is outside 16.2's table; the owner cap of
8 holds because a copy never runs during a save - Copy refuses while a job
runs).

Interfaces for `edit`: `int pdu_sel_quads(pd_pt *dst, unsigned cap)` copies the
CURRENT page's selection into `dst`, 8 `pd_pt` (4 points) a quad, **`cap` counted
in quads** (edit's Highlight passes 12 for its 96-value buffer), and returns the quads (0 = none or still being computed);
`int pdu_sel_text(char *dst, unsigned cap)` runs the COPY pass to its end on this
call (slow on a dense page: never from a callback that holds the lock) and
returns the characters. Esc clears the selection after the popover and the page
field (17.5).

#### 20.10.4 The Pages and Outline sidebar

A Pages cell is a page's picture once `PDV_REND_THUMB` has drawn it (10.8): one
56x72 1bpp cell per 512-byte slot of the THUMB claim (8 slots, so the panel
shows at most 4 rows of 2 cells; `PD_THUMBS` = 12 is not reached), filled one at
a time from the idle ladder - the current page first, then what is visible - and
drawn by `os88_gfx_blit1_seg` (a new thunk, `apps/cc/os88thunk.asm`: the band is
in a claim, not in the package). Until then a cell shows its number and "..."
and a page the renderer refused shows " - " and is not retried. A job waits for
the page: a render or a page owed aborts a running thumbnail. LRU by use.
`int pdu_side_sel(void)` is the page last clicked in the panel, -1 for none
(`pages`' interface); a click also goes to the page.

The Outline tab reads `PDV_ENG_OUTLINE` a level at a time into the same claim
as a flat array of 42-byte entries in display order (97 at most): the first
level when the tab is shown, a twisty's children (up to 24) when it is clicked,
and the collapse gives them back. A title goes to its page; an entry whose
destination is not a page in the file says so. Switching tabs empties whichever
the other had.

#### 20.10.5 Gestures, history and the idle ladder

The kernel has a tracking edge (`OSAPI_WM_ONDRAG`, SPEC.md 13.8.2) but the C
SDK carries no callback for it (`crt0.asm` has no `CC_HAS_ONDRAG`; ASK, an SDK
change outside nav's files), so a press that needs one arms the
window's one-shot timer (`os88_wm_timer`, 1 tick) and POLLS `os88_mouse()` until
the button is up (`os88_onmouseup` ends it too; a lost release is noticed by the
poll). **The scroll bar's thumb** drags through `os88ui.inc`'s thumb drag
(13.10.5; `pdu_sbgrab`/`pdu_sbtrack`/`pdu_sbdrop` in `pdui.inc`): the view
follows every 2 ticks on a 286 or better and only at the release on an
8086/8088, where one commit is a third of a second. While a selection's band is
on the glass no page render runs.

**History**: Back (Alt+Left, Document > Back) and Forward (Alt+Right) over the
pages a JUMP left - a followed link, an outline entry, a Find hit, Go to, Home
and End, a cell click; Prev/Next/PgUp/PgDn move by one and are not history. 8
pages, a new jump forgets Forward.

**The idle ladder** (`ovl_nav_work`): when no job runs, no page is owed and no
render is wanted, nav's next piece of work starts, in the order the user waits
for it: a text pass owed, a Find, the boxes of a selection, a selection owed to
the glass, a link's message, the Outline's reading, a thumbnail. The wake
handler keeps going while one is owed (`pdu_nav_owed`).

Keys added: Ctrl+C Copy, Ctrl+A Select all, Shift+F3 Find previous, Alt+Left /
Alt+Right Back / Forward. Menus: Edit > Copy, Select all (Find previous is
live), Document > Back, Forward.

#### 20.10.6 Sizes, deviations and what is open

Measured by verify-nav against feat/pagedesk a9cc94d37 (edit and notes in):
primary 47,800 + 6,207 = 54,007 bytes, **+3,160** of nav's +3,584 (feat:
44,728 + 6,119); overlay part 37,306, **+10,585** of +10,752 (feat 26,721);
PDENG.MOD +1,994 of +2.5 KiB. Every figure is an `os88pkg` line of `make
pagedesk` (nav-verify.md). Stack (`tools/os88pdstk.py`, t_pdstk green): nav's
deepest UI-task chain is the wake's text pass scrolling a Find hit into view,
360 of 384 (`cc_onwake > ovl_tx_step > ovl_tx_done > ovl_sel_view >
pdu_scroll_to > pdu_canvas_full`, the tail being the popover's paint); no
recursion in pdesrch.c, pdelink.c or the primary's nav files. Deviations: (1) the Copy claim above; (2) `pdxb_search`
and `pdxb_annot` carry the protocol above instead of new fields (no ABI
change); (3) `os88_gfx_blit1_seg`, an SDK thunk. Open: reading order is the
content stream's, so columns and tables copy in stream order; a name-tree
destination is "Named" and not followed; no hand cursor over a link (the OS
shows no hover - it has no move callback with no button down); no triple-click
line selection.

### 20.11 Page operations, Extract, New and the save flows (`pages`, wave 2)

Source: `apps/pagedesk/pdwmap.h` (the map's encoding, shared by both sides),
`pdupage.c` (resident: the lookups, the greying, the doors), `pdovlpage.c`
(the overlay: every command, prompt and job start/end), `pdwpg.c` (PDWRT: the
flat page tree, duplicates, blanks), `pdwgc.c` (PDWRT: the graph copy). Host
checks: `make pdwrt` (`hosttest/pdw_test.c`, byte copy, `PDE_CHANGED`, folders)
and `make pdwrt-real` (`hosttest/pdw_real.c` against PDENG's own host build,
`pdw_pgcheck.py` against qpdf and Poppler's pixels, `pdw_nest.py`'s nested tree).

#### 20.11.1 The page map

The map is the order of the pages the user SEES, one 16-bit word a view page
in claim row `PDU_DC_PAGES` and, byte for byte, the `pages_seg` of
`PDV_WRT_SAVE` / `PDV_WRT_EXTRACT`: bits 15-14 kind (`PDM_SRC` a page of the open
document, `PDM_BLANK` a new empty page of the size of source page `idx`,
`PDM_FOREIGN` reserved for Insert, 20.11.7), bits 13-12 quarter turns ADDED to
the page's own `/Rotate`, bits 11-0 `idx`. `pages_count` = 0 is the source's own
order. A map holds at most `PDM_MAXMAP` = 240 pages: the row is 1 KiB, the map
at byte 0, Extract's page list at `PDM_XLIST` = 480 and the prompt's names at
`PDM_NAMES` = 960 (a 400-page cap put the list inside the map). Page operations
on a longer document, and Duplicate or Insert blank page on a 240-page one, are
refused in words ("Page operations handle up to 240 pages."); so is an Extract
list of more than 240 pages, which is refused rather than cut short.
The second time `PDM_SRC` names a source page it is a **duplicate**; the first is
the original and owns the page id's edit records (`pdu_pg_isdup(view)` tells the
renderer's caller). `pdu_npages` is the VIEW's page count, so nothing else in the
primary changes meaning; `pdu_pg_id(view)` is the one lookup from a view index to
the page the engine knows (`pdu_pg_count()` = `pdu_npages`). The view shows the
new order and rotation before the save: `pdu_page_load` asks PAGEINFO for
`pdu_pg_id(n)` and turns `pdu_pinfo.rotate`; a blank page is paper (no render).
A page operation is a delta of four words `{op, a, b, entry}` - 1 ROT, 2 DEL, 3 INS,
4 MOVE - applied and reverted by `pdu_pg_apply(delta, len, undo)` (edit's undo
calls it; the commented `pdu_undo_pageop(pdu_pgd, 8)` in `ovl_pg_do` is the one
line edit enables when it lands, until then an operation applies without a history). When the
map returns to the identity it is dropped (nothing to write).

#### 20.11.2 The writer's page operations (13.7)

An incremental save whose map is not the identity writes the root `/Pages` node
again: its other keys kept, `/Kids` in the new order, `/Count`. A page that is
rotated, is under a node other than the root, or has annotation edits is written
again (one revision per page: `/Parent`, `/Rotate` and the annotation array
merged); a page moved to the root gets `/MediaBox`, `/CropBox` and `/Resources`
materialised from its nearest ancestor. The tree is treated as flat when the
root's `/Count` equals the number of its `/Kids` (a single-page intermediate node
is the one case this misjudges). A duplicate is a new page object sharing
`/Contents` and `/Resources` by reference whose annotations are new objects
(`/NM` new, `/P`, `/Popup`, `/Parent`, `/IRT` remapped); a blank is a new object
with the source page's `MediaBox`. Edit records of a deleted page are not
written. Limits: 16 annotations on a duplicated page (`err_detail` 12), a root
or page object over `PDW_PGBUF` = 4,096 bytes (13), a map entry that is not
`SRC`/`BLANK` (14). Numbers: new objects from the source's `/Size`, page-operation
objects first (`pm_prep`), annotations after; the xref is the incremental one.

#### 20.11.3 Save As beside nothing: byte copy, `PDE_CHANGED`, another folder (14.2, 14.4, D14)

An incremental save with no live edit and the identity map is a **byte copy** of
the source (no newline, no xref section; PDENG's check below still runs, and its
`startxref` must be the source's own). The writer copies to the end of the source
and compares the length with the fingerprint's size taken at the start: a source
that moved answers `PDE_CHANGED`. The primary compares the directory's size with
the one OPEN saw before it starts (`ovl_save_begin`, the 4-byte fingerprint read back from the exchange block). **`src_vol` is the source's
volume PLUS ONE** (0 = one folder; volume 0 is A:, a legal source) and `src_clus`
its folder: the writer stands in the source's folder for every PDENG call and
source read and in the target's for every write, rename and delete (`pdw_cross`,
`pdw_in_src`, `pdw_in_tgt`; the target's folder is "here" at the first call), so
the primary stands in the TARGET's folder for a job's slices (`pdu_tgtplace`).

#### 20.11.4 Extract (13.8)

`PDV_WRT_EXTRACT` with the map as the page list (SRC and BLANK entries, a source
page at most once, `err_detail` 15) writes a fresh document. The output region,
then three bit maps sized from the source's `/Size` (`gc_nw` = ((size >> 7) + 1)
x 8 words each for `seen` and `scan`, a quarter of that for the prefix count a
128 numbers; 1,048 bytes for a 1,000-object source, 17 KiB only at the 2^16
ceiling), then the object table and the tail tables; a claim too small answers
`PDE_NOMEM` with the KiB it needed in `err_detail`. The primary sizes the claim
from PDENG's trailer: `wbuf_kb` = 16 + size/128 + size/2048, at most 40 (40 when
the trailer cannot be read). A reference past `/Size` is not marked (it becomes
`null`), a root page whose object number is past `/Size` answers `PDE_XREF`, and
a root that cannot be read fails the job rather than being dropped. Phases: PAGEINFO
of every chosen page (its object is a root); a fixpoint that reads every reachable
object's references (an object whose `/Type` is Page, Pages or Catalog and is not
a root is NOT copied and every reference to it becomes `null`, as does a reference
to an object the source does not have); objects 1-3; every seen object in
ascending source order, numbered by rank from 4 (a root page rewritten from its
PAGEINFO: `/Parent` the new tree, `/Rotate` final, boxes and inherited
`/Resources` materialised), the dictionary with every `n g R` rewritten by the one
reference walker (`rs_walk`: strings, hex strings and names skipped by syntax) and
the stream bytes from `stream` to `endobj` copied verbatim in buffer-sized pieces;
then the blank pages. Over 2,432 objects `err_detail` 16, a dictionary over the
buffer 14. Marks not yet saved are not extracted.

#### 20.11.5 The jobs (primary)

`ovl_save_begin(name, kind)` (kind 0 Save As, 1 New, 2 Extract; an overlay
function, so a save needs the overlay part) claims the writer (18 KiB; an Extract
sized from `/Size`, 20.11.4), fills
`pdxb_save` and starts `J_SAVE`; `pdu_save_step` runs the verb of the kind.
`PDE_EXISTS` with `state` = `PDW_ST_CONFIRM` parks the job and asks in the status
line (Enter replaces, Esc keeps and aborts). **Esc does not say "Cancelled." from
the key handler**: a key arrives with the gfx lock held, and the job's ending
(`ovl_save_done` -> `pdu_wsay`) takes it again, which deadlocked the UI task in
`gfx_lock` on the first build. Esc starts `J_SAVE` with `pdu_jcancel` set, and the
onwake slice, lock free, takes the ordinary cancel path: "Cancelled." and the
source re-opened. Escape during the re-open itself is ignored (`pdu_reopening`),
since cancelling it would leave PDENG with no document. **The writer reads its output back
through PDENG** (14.2 step 4: `PH_VFY` closes the source's document, opens
`PDSAVE.$$$`, checks the file's size, the trailer's `startxref` and `/Size`, and
the first, middle and last object written) and says so by ORing
`PDW_OUT_REOPEN` (0x0100) into `pdxb_save.mode` (an ASK to the lead for a proper
field). After the job the primary always gives PDENG a document: a saved Save As
or New **opens the output** (the page, 1-based, and the "Saved X" line kept), a
failed or cancelled job and an Extract re-open the source with `OPEN` alone (CLOSE
keeps PDENG's claims; the edit store and the map are untouched). New writes
`PDV_WRT_NEWDOC` with one `pd_newpage` (Letter, A4, either way) 512 bytes into
the exchange block, written again before every slice. The prompts (`PK_*`: extract
list, destination, delete confirmation, New's size, replace) live in the status
line, so CGA does everything the other adapters do. Menu items: File New,
Extract pages..., Insert blank page, Duplicate page, Delete page; Edit Move
earlier, later, to... (after nav's Copy and Select all); Document Rotate left,
right (after nav's Back and Forward). A pull-down holds 11 items (12.4) and the
bar 5 menus, so Document has no First / Last page rows: Home and End are
those, and the Pages panel's ends. The
nine page rows are one table in `pdupage.c` (`pdu_pgmrow`/`pdu_pgmcmd`/
`pdu_pgmtxt`) that both the menu sync and the command lookup read, and every
command body is in the overlay (`ovl_pg_cmd`, `ovl_pg_key`, `ovl_pg_onfile`);
Insert from another PDF has no row until it is built (20.11.7). The Pages-mode
buttons are the same commands. A job's outcome line ("Saved X", "Extracted to
X", "Could not save ...") is the last thing the job says: the re-open after it
does not overwrite it with "Ready:".

#### 20.11.6 The UI task's stack (D21)

PDWRT calls PDENG from the bottom of its own call chain, on the UI task's stack
(512 bytes, the kernel using up to 246; D21): the first page-operation save on the
target died with `STACK OVERFLOW TASK 00` when the stack was believed to be 384.
Rules kept: no recursion anywhere (`sc_val`, the value scanner, was recursive in
wave 1 and is an explicit stack of 16 container kinds now; the graph copy, the
page-tree rewrite and `rs_walk` are loops over the buffer), `eng_get` is one
function (arguments in file-scope words), the page-operation and graph-copy
functions keep their locals in statics, `pdw_ferr` is one function. The writer's
tail tables are read through `tg`/`ts`, far peeks and pokes in the claim. A new
feature in PDWRT must be measured with `tools/stkwater.py` (and, when it lands,
`tools/os88pdstk.py`) before it is believed. **The output region is flushed at
the top of the slice loop** whenever less than `PDW_ROOM` = 2,048 bytes of it is
free (`job_slice`), so the file write happens one level below the door instead of
at the bottom of `pb_*` -> `pw_n` -> `pw_flush` inside an appearance stream, which
was 196 bytes of package frames plus the kernel's file code. Measured on the
verifier's pass (cc8086 frames + pushes over the generated asm): every kernel call
from PDWRT is within 122 bytes of the door, the PDENG door within 134, no
recursion; a unit that writes more than 2,048 bytes still flushes where it is.
D23's private module stack supersedes the byte budget; the flush stays because
it is also fewer, larger writes. Insert from another PDF opens a
second document in PDENG from inside the writer, which needs the engine's requests
made from the primary, which is why it is not built.

#### 20.11.7 Not built, and deviations

Items 1, 2, 4 and 5 recorded the state when pages landed; later waves
superseded them.

1. Insert from another PDF: built, see 20.16.
2. The recovery journal and the `PDSAVE.$$$` offer: built, see 20.17.
3. Undo/redo of a page operation needs edit (20.11.1); the dirty flag is
   `pdu_changes`.
4. `ovl_pg_tgt` takes the Pages sidebar's selection (`pdu_side_sel()`) since nav
   landed.
5. Sizes at pages' merge are no longer current; PAGEDESK-PLAN 7.1 carries the
   measured sizes.
6. 14.4's open-time reference is the file's SIZE (free at OPEN); the CRCs would
   cost two uncached reads at every open.
7. A foreign-folder Save As stands the primary in the target's folder, so
   `pdu_at_doc` is not called during a save.


### 20.12 Annotations on screen (`annotr`, wave 2)

PDREND draws what the user has marked up, in the same pass and the same bands as the page:
the source file's annotation appearances (`/AP /N`, PDRF_ANNOTS), the edit store's records
(PDRF_EDITS, 13.1.1), dash patterns and inline images. The code is `apps/pagedesk/pdra.c`
(`#include`d by `pdrend.c` after `pdrv.c`) and edits to `pdri.c` (the stream machine), `pdrg.c`
(dashes), `pdrd.c` and `pdrv.c` (inline-image records). It SUPERSEDES three statements of 20.3:
20.3.3's "inline images are consumed and not drawn" and 20.3.8's lists of what is not drawn
(dash patterns, inline images) - both are drawn now, with the limits below. The primary draws no
annotation pixels itself (D15): it sets `pdxb_render.rflags` and, for edits, `edits_seg`,
`edits_count` (and `hide`, `mono_mark`) in the BEGIN call.

#### 20.12.1 The phases of one pass

A pass (20.3.2) interprets the page's content once for the rows of its band group. With the
flags set it is five phases, in display-list order, so the later paints over the earlier:

| # | phase | why there |
|---|---|---|
| 1 | the edit store's **highlights** | a band is blitted OPAQUE and there is no Multiply, so a highlight drawn after the text would hide it: the fill goes FIRST and the page's text, drawn over it, is the multiply that the file would have |
| 2 | the **source's highlight** annotations (`/Subtype /Highlight` with an `/AP`) | the same reason; a real file's highlight appearance carries `/BM /Multiply`, and the `gs` operator is ignored, so what is left is an opaque fill drawn under the page |
| 3 | the page's own content | |
| 4 | every OTHER source annotation with an appearance | |
| 5 | the other edit records, in store order | |

Only `PDRF_ANNOTS` runs phases 2 and 4; only `PDRF_EDITS` runs 1 and 5; a thumbnail and the text
map run neither (their `rflags` is 0 inside PDREND). Every phase is IN BAND: its records are in
the list of the pass, culled by the group's rows like everything else, and the DL-overflow rescan
(10.2, D4) repeats all five phases from the start for each narrower group - `pdr_annot.py` draws
ANNOTS256 and MARKUP with an 11 KiB list in 19 and 24 passes and compares the images byte for
byte with the 29 KiB list's. The state machine is `g_aph` (`frame_end` calls `annot_next` when
the page's contents, or an annotation's form, end with nothing open): the page's stream is
OPENED once at the start (so a page with no readable content still answers the engine's error)
and closed again when phase 1 or 2 runs first. A page with no `/Contents` still shows its annotations.

**Every phase is sliced** (10.7, 15.1 - verify-annotr). The edit store is drawn from a record
index (`g_aedi`) a budget a turn - records until about 1,024 points and text bytes are spent, a
record is never split, a record not drawn costs 4 - and the source walk tries at most 8
`ANNOTS` (with their `DICT_GET`s) a turn; a turn that ends with no stream open sets `g_await`,
which ends the slice and resumes the walk at the next. So a page of 120 hidden annotations is
some 15 slices more than without them, not one unbounded slice, and 200 ink records are drawn
over several. **A stream error ends that stream, not the page**: an appearance that does not
tokenize (or the page's own content) is closed where it failed, `PDF_INC_LIMIT` "content stream
error" is noted, and the phases go on - the annotations after it and the user's own marks are
still drawn. Each annotation stream starts with the lexer's partial state cleared (an appearance
that ends inside `BI`'s dictionary or a `<< >>` cannot swallow the next one).
The clip pool of the pass is kept across phases (a state is reset with `gs_reset`, not
`gs_init`), so an earlier polygon clip is never rewritten under the list that names it.

#### 20.12.2 Source annotations

`ANNOTS(page, index)` (20.2.6) is walked in order; PDREND reads the answer straight out of the
exchange block (`objnum` +0, `gen` +2, `subtype` +4, `rect` +20, `flags` +52, `has_ap` +54 -
held to the struct by `static_assert`s in the host build) and calls nothing else on the
annotation until it has them. An annotation is SKIPPED when it is a hole (`objnum` 0), has no
`/AP /N`, has `/F` bit 2 (Hidden) or 6 (NoView, value 32), is not of this phase's kind (the
subtype is `Highlight` or it is not), or when `PDRF_EDITS` is set and a record of the store names
it in `src_num`/`src_gen` (a deletion, a move or a replacement: the record draws it, or nothing
does). `/N` is a form XObject by reference (an object with a `/BBox`), or a state dictionary of which
`/AS` names the entry - by reference, or DIRECT (`/N << /Off 5 0 R /Yes 6 0 R >>`, every check
box's shape: `DICT_GET` then walks `/AP /N /<AS>` in one path); anything else draws nothing. The
form is drawn as a Form XObject (the same `do_form`: its `/Matrix`, a clip to its `/BBox`, its
`/Resources`, forms nested to the limit of 20.3.8) after the **PDF 1.7 12.5.5** placement: the
form's `/BBox` through its `/Matrix` is the smallest upright rectangle holding its FOUR
transformed corners (exact for every matrix: a quarter turn, a skew, a 45-degree turn - the
first version took two opposite corners, which collapsed a 45-degree appearance to nothing), and
the matrix A that maps that rectangle onto the annotation's `/Rect` (a scale and a translation)
is applied BEFORE the form's own `/Matrix`. A degenerate box, rectangle or an
unreadable `/BBox` draws nothing and is not Incomplete. `Widget` is drawn like any other: a
field's appearance is a form. No annotation without an appearance gets a made-up icon.

#### 20.12.3 The edit store's marks (13.3, 17.4)

A record is drawn when `page` equals the render's `page`, `PDEF_DELETED` is clear, its kind is
1..9, it is not the record `hide` while `PDRF_NOEDITID` is set, and it is not a review reply (a
`PDA_NOTE` with `irt` set has no appearance: 13.4). The store is READ ONLY here (`pdr_test`
compares all 64 KiB of it after every render). Colours are the `PDC_*` palette indices; on 1bpp
every stroke and every text is BLACK (10.5) and fills are white. Widths are points: `style` for a
frame or a line (0 = 1), `size` x 2 for ink (0 = 2 pt). Everything is built from the page's own
path, stroke, fill, dash and text machinery, so a mark clips, turns and scales with the page.

| kind | on VGA/EGA | on Hercules and CGA |
|---|---|---|
| Note | PDWRT's speech bubble (`pdwap.c ap_note`) in the record's rect, scaled from its 20 x 18 pt design: filled in the colour, a 0.8 pt black outline and three text lines | the same, filled white |
| Highlight | each quad filled in a LIGHT form of the colour (black, light grey and white become yellow; 0..7 take bit 3), drawn FIRST (20.12.1) | by `pdxb_render.mono_mark`: **0** (default) an underline along each quad's lower edge AND a dotted frame (1 pt on, 2 pt off), **1** the underline only, **2** the dotted frame only, **3** a light hatch (a dither of grey 7, drawn first); never a solid fill (17.4) |
| Area mark | a stroked frame inset by half its width, dashed 3 pt on / 3 pt off when `align` bit 0 | the same, black |
| Rect | a solid frame as above | |
| Ellipse | four Beziers, kappa 0.5523, in the same inset | |
| Text box | the writer's layout (PDWRT 20.5.6): 2 pt of padding, the first baseline 0.8 size below the top, leading 1.2 size, `/Q` from `align`, lines broken at the last space that fits else inside the word, `\n`, `\r`, `\r\n` paragraphs, no line below the frame, at most 1,024 bytes; the PDRES face of the family (`style >> 4`: 0 sans, 1 serif, 2 mono) and `style & 3` (bold, italic), at `size` points (0 = 12). Widths are kept exactly (whole thousandths of an em) so the breaks are PDWRT's | black |
| Ink, Sign | each pen-up separated stroke as one polyline, round caps and joins; a one-point stroke is a dot; at most 4,096 points; a stroke longer than the path buffer (200 vertices) is stroked in pieces, each going on FROM the last point (the first version lost the segment across each cut) | black |
| Line, Arrow | a butt-capped shaft and, for each of `align` bit 0 (start) and bit 1 (end), an open head of two barbs 25 degrees off the line, 8 pt plus two widths long | black |

`mono_mark` is the shell's preference (17.4); the value 0 is what a zero-initialised block asks
for. The hatch and the frames are drawn IN BAND like the rest. A highlight of more than 64 quads
draws the first 64 (the record's `pts_n` is trusted for no more than 4,096 points and 1,024 text
bytes whatever it says: a hostile store cannot make PDREND read past a 64 KiB segment or loop
unbounded - `pdr_fuzz.py` draws a store of 60,000 points and wrapped offsets under ASan).

#### 20.12.4 Dash patterns

`[a b c d] phase d` is kept in the graphics state (so `q`/`Q` restore it): up to four lengths,
each in quarter points 0..255 (a longer array keeps four and sets `PDF_INC_LIMIT` "dash array";
`[] 0 d` and `0 d` are solid), and the phase in quarter points REDUCED MODULO THE PERIOD (the
sum, twice it for an odd count) when `d` runs, so any phase is exact to a quarter point - the
first version clamped it to 63.75 pt (`dph` took the state's spare pad byte, so the state did not
grow). A path is walked in device qpx,
cut where the pattern changes, and every "on" piece is stroked as an open polyline of its
own, so each carries the line's caps (a zero-length dash with round caps is a dot - at a hairline's width a one-pixel square, since an
octagon half a pixel across can miss every pixel centre and the first version drew no dotted
hairline at all; with butt caps it is nothing). An odd count cycles with on and off swapped, as PDF says; the phase is
consumed from the pattern's start; a closed subpath dashes round its closing segment; curves are
dashed after flattening. A pattern whose lengths sum to under 1.5 device pixels draws solid (it
cannot be told from it, and the piece count would be the cost), and one subpath cuts at most 3,000
pieces. The saved graphics states grew to 84 bytes (`WS_FN` 1,344; `pdr_ck_gs` holds 16 of them
under it in both compilers). The walker's state is static and its pieces reach `stroke_poly` in
one call (20.12.8).

#### 20.12.5 Inline images

`BI ... ID ... EI` is read EXACTLY (PDENG cannot un-read, 20.2.5): the dictionary's `W`, `H`,
`BPC` (1, 2, 4, 8), a device `CS` (`/G`, `/RGB`) or `/IM true`, `/D` (only its first number: 1 or
more inverts), unfiltered. Such an image whose raw row fits the 1,024-byte row buffer and whose
stored form fits 6,144 bytes is CAPTURED: PDREND copies it into its own display-list record
(flag 16 of `DLR_IMAGE`, the rows follow the 20-byte body) - a stencil or 1-bit gray as 1 bit, all
else as 8-bit gray (RGB by its luminance) - and the rasteriser draws it with the XObject images'
row composer: 1-bit dithered to the adapter (10.5), gray through the 4 levels on VGA, a stencil
in the fill colour, mirrored left to right. It is read in slices of at most 4 KiB, so a large
image spans several, and a stream that ends early leaves the rest of the rows blank. An image
the page does not show (outside the group's rows, or clipped away) is read and skipped. NOT drawn,
consumed by the same exact count, and `PDF_INC_IMAGE` "inline image": CMYK, a filter (`/F`, then
scanned for white space, `E`, `I` one byte at a time), an unnamed or named colour space, a width
over 4,096, a stored size over 6 KiB, a row over 1,024 bytes, images turned a quarter or flipped
(as XObjects, 20.3.8). A record that does not fit the list sets the list-full path (10.2).

#### 20.12.6 Size, memory, cost

| item | bytes |
|---|---|
| `PDREND.MOD` image + bss | 52,996 + 6,688 = **59,684** with rendfix's iterative `bez` merged (over D20's 58 KiB target by 292, under its 60 KiB cap); 52,822 + 6,562 = 59,384 after verify-annotr (52,210 + 6,518 = 58,728 as annotr left it, +18 of the lead's `verb_init`; 50,584 before this wave; D20: target 58 KiB = 59,392, hard cap 60). verify-annotr's +656: the sliced walk and store (+270), 12.5.5's four corners and direct state dictionaries (+110), the phase modulo (+80), errors that end a stream and not the page, the ink cut, the hairline dot and the dash walker's static state |
| new code (image, +8,230): the annotation phases and 12.5.5 about 1,100, the edit marks about 2,700, dashes about 500, inline images about 850, the image split and the other edits to the interpreter the rest | |
| bss (-86 net) | the glyph-run buffer is ONE (the walker and the interpreter never overlap: a group is interpreted before its bands are drawn; -320), the annotation is read out of the exchange block instead of copied (-238 avoided), the dash buffers and fields and the edit record (+470) |
| claims | none new. Inline images and records live in the display list (the 29 KiB `dl_kb`), the workspace table grew 64 bytes inside its 6,912 |

Cost is in list records: a mark is one `DLR_FILL` per shape plus glyph runs for a text box; a
note is two, a highlight one a quad. On the 8088 the new work at BEGIN is a few hundred bytes of
far copies per annotation and one `ANNOTS` plus up to four `DICT_GET` calls (cached by PDENG), once
per pass, twice for a source annotation file (highlights, then the rest). It is NOT measured on
an emulated XT; the figures to take are the passes of an annotated page.

#### 20.12.7 Checks

`make pdrend-annot` (`hosttest/pdr_annot.py`, 81 checks): a generated page of appearance cases
(a plain form, a stretched `/Matrix`, a quarter turn, a negative-origin `/BBox`, Hidden, NoView,
`/F 0`, a state dictionary with `/AS`, no `/AP`, a highlight over text, a page with no
`/Contents`, `/Rotate 90`) and ANNOTS256 against **mutool**; the edit store of every kind built
by `hosttest/pdr_mkstore.c`, saved by the REAL PDWRT over the real PDENG, and PDREND of the
ORIGINAL with that store compared with mutool's render of the saved file (0.0% of 8x8 cells
differ on MARKUP.PDF at VGA), then every kind probed at its pixels on VGA, on Hercules and CGA and
in each `mono_mark` style, `PDRF_NOEDITID`, flag combinations, a 0-record store, an 11 KiB list, a
turned page, a replaced and a deleted source annotation (nothing else on the page changes); dash
run lengths, phase, odd counts, caps, `Q`; inline images of every depth, space and mask, exact
lengths, refusals, a 5,000-byte image across slices, against mutool. `make pdrend-fuzz` adds
hostile appearances (zero, singular, huge and short `/BBox` and `/Matrix`, a form that includes
itself, a lying `/Length`, `/AP` cycles, 300 heavy annotations), arrays of holes, extreme dashes
and every inline-image size, and runs every case again with both flags and a hostile store.

verify-annotr added eleven checks to `pdr_annot.py` (92 in all), each of which FAILS against the
first version (2bae9f97d) and passes now: a 45-degree `/Matrix` appearance (12.5.5's four
corners, and mutool agreeing there), a DIRECT `/N` state dictionary, an appearance that errs half
way and one that ends inside `BI` (the annotations after both still drawn), a page whose content
errs (its annotations still drawn), 120 hidden annotations walked a batch a slice, 200 ink
records drawn over several turns, an ink stroke across the path buffer's cut, and a dash phase of
1,002 pt taken modulo the period. `pdr_fuzz.py` gained a case of direct state dictionaries with
hostile `/AS` names, turned, skewed and near-singular forms and erring appearances.

#### 20.12.8 Stack (verify-annotr)

PDREND runs on the UI task's stack, **512 bytes** (`STK0_SIZE`), under the primary's chain (about
70 bytes from `os88_onwake` to `pdu_mcall`) and the module door (22), and PDENG runs under it in
turn (its door 26, then up to 236: `DICT_GET` 188, `TOKEN` 158, `STRM_READ` 98, each ending in a
kernel file read). Measured by a static walk of the compiled C (SP through every push, pop and
`sub sp` of each function, the worst call chain composed): before this wave PDREND's deepest
non-recursive chain from `verb_step` was 388 bytes (`do_image > dict_num > dict_get > pdr_eng`)
and the recursive `bez` 518; the first version of the dash walker made it 470. The walker's state
is now static and it strokes in one call, so the deepest is **396** (a dashed stroke), the
deepest call into PDENG **350** (was 380: `image_place` split `do_image`'s frame) and into a file
read 374 (376). `ed_text`'s 54-byte frame stays on the stack: the annotation phases reach a
strike read 160 bytes shallower than the page's own text does. **The class is not closed by
this**: a PDREND chain into a PDENG cache miss is about 350 + 26 + 188 + the kernel's read, which
is past 512 before `bez`; that is the lead's (DECISIONS, the kernel-.bss corruption of 21:38).
### 20.13 FlateDecode and the predictors (`flate`, wave 2, PD-25)

PDENG's `FlateDecode` is the Enhanced tier's decoder. It is a stage of the
filter chain (20.2.5) like `ASCIIHexDecode` and `RunLengthDecode`: it hands the
stage above it one byte at a time and is resumable at any byte, so every bound
of 20.2.5 (1 MiB out, 4 MiB of every stage's work, PD-28) holds unchanged. A
machine that gives PDENG no flate claim (20.13.3) refuses Flate with `PDE_FLATE`
exactly as the XT build always has, and the page says Incomplete.

**20.13.1 What is decoded.**
* zlib (RFC 1950): the two header bytes are checked (method 8, window <= 32 KiB,
  FCHECK, no preset dictionary); anything else is `PDE_SYNTAX`. The Adler-32
  trailer is **not read and not verified**: a corrupt stream is caught by the
  structure of its deflate data, and the checksum would cost a pass over every
  output byte on a 4.77 MHz machine for a defect that real viewers also
  tolerate (a wrong or missing trailer is not an error here, as in MuPDF).
* deflate (RFC 1951): stored, fixed and dynamic blocks, a history of up to
  32,768 bytes. A code set that is over-subscribed, or incomplete other than
  zlib's single code of length 1, is `PDE_SYNTAX` (as zlib); so are a block
  type 3, a stored block whose LEN and NLEN disagree, a dynamic header with
  more than 286 literal/length or 30 distance codes or no end-of-block code,
  a length or distance symbol that does not exist, and a match that reaches
  before the first byte the stream produced.
* a stream that ends before its final block's end-of-block code is `PDE_TRUNC`
  (`PDE_IO` when the file read failed), reported after every byte decoded
  before that point has been delivered; a stream whose final block is whole
  and whose trailer is missing is complete.
* Predictors, from the stage's `/DecodeParms` (a dictionary, an array with one
  entry per filter of which the Flate stage's is used, either direct or by
  reference; `null`, no entry and no dictionary mean predictor 1). `/Predictor`
  10-15 is PNG: every row carries its own tag byte (None, Sub, Up, Average,
  Paeth) and the value 10-15 is not consulted, as in every viewer; a tag over 4 is
  `PDE_SYNTAX`. `/Predictor` 2 is TIFF with `/BitsPerComponent` 8 or 16.
  `/Colors` 1-4 (default 1), `/BitsPerComponent` 1, 2, 4, 8 or 16 (default 8;
  PNG at every depth, TIFF at 8 and 16 only), `/Columns` (default 1). A row
  of more than 4,096 bytes is `PDE_LIMIT`; `/Colors` over 4, a predictor
  other than 1, 2 and 10-15, a TIFF depth under 8 and a parameter that is not
  a positive integer are `PDE_FILTER`, all at `STRM_OPEN`. A last row the
  data stops inside is delivered as far as it goes. (`/DP` is an inline
  image's abbreviation and is not read in a stream dictionary.)
* At most one Flate stage in a chain (a second is `PDE_FILTER`), anywhere in
  it: `[/ASCIIHexDecode /FlateDecode]` and `[/RunLengthDecode /FlateDecode]`
  decode, and `/FlateDecode` and `/Fl` are the same filter.

**20.13.2 Why the decoder is C and not apps/pixel/pxpng.asm's, and what is
shared.** PiXEL's inflater is a 600-line assembly loop whose input is a *bit
position over a de-chunked buffer in its scratch segment*, whose output is a
*ring window it also extracts scanlines from*, and whose registers (SI, DI, DX,
BP, CL) are its calling convention; its tables are a 9-bit primary plus
sub-tables laid out in that same segment, and it runs to the end of a batch
rather than yielding a byte. None of that is a seam PDENG can use (a pull of
one byte at a time through the filter chain, a far history, every call
bounded), and cutting one into the file PiXEL assembles would change PiXEL's
only decode path with no way to run PiXEL's rows in this wave (CLAUDE.md,
Testing). The decoder is therefore a new one, written to PDENG's rules, and
**what is shared is the constants**: `fl_lbase`, `fl_lext`, `fl_dbase`,
`fl_dext` and `fl_clord` are checked element for element against
`pz_lbase`, `pz_lext`, `pz_dbase`, `pz_dext` and `pz_clord` by
`hosttest/pde_fl_check.py` (`tables`), which fails if either file drifts. It
reads the same way PiXEL's does: an 8-bit lookup for the first bytes of a
code, the canonical walk for a longer one.

**20.13.3 Memory: nothing is claimed by PDENG.** The primary hands PDENG a
**flate claim** in `pdxb_init` (`flate_seg`, `flate_kb`, valid when `flags &
PDXI_FLATE`; DECISIONS D17), 43 KiB a **slot**, one or two slots
(`flate_kb / 43`, capped at 2; under 43 KiB, or no flag, is no slot). Slot *k*
is at `flate_seg + k * 2752` paragraphs, so every slot is under 64 KiB and no
offset wraps:

| offset in the slot | bytes | holds |
|---|---|---|
| 0 | 32,768 | the deflate history (a ring) |
| 32,768 | 4,096 | the predictor's row A |
| 36,864 | 4,096 | the predictor's row B |
| 40,960 | 1,728 | the Huffman tables: counts, symbols and an 8-bit lookup for the literal/length code, counts, symbols and a lookup for the distance code (`FT_*` in `pdeflate.h`) |
| 42,688 | 448 | the decoder's saved state (`struct flst`: 56 bytes of state, the 256-byte output buffer, the 72-byte predictor chunk, the 64-byte input read-ahead) |

The primary carves the area only on the Enhanced tier, by RAM and not by CPU
(D17): from claim B's tail when the free heap after the whole document set
stays at or above 32 KiB (PD-29's reserve), two slots when two fit, and never
when the Control Panel's low-memory setting is on, so the XT's capability set
can be shown on any machine. Re-`INIT` is legal (D2): each `INIT` replaces the
claim and frees every slot. A stream opened while both slots are in use is
`PDE_LIMIT` at `STRM_OPEN` (so with one slot a Form XObject that is itself
Flate inside a Flate page is incomplete; with two it works).

PDENG's near cost is one *loaded* decoder (the bit buffer, the output buffer
`ob` of 256 bytes, the predictor's 72-byte chunk, the 64-byte input read-ahead
and its state, 448 bytes in all) and a 320-byte scratch for a dynamic header and the predictor's row
above. Reading a Flate stream other than the one loaded swaps the loaded
state out to its own slot and the reader's in (two block copies of 448 bytes;
a form opened inside a page's content and then closed costs two swaps); the
tables and the history never move. PDENG.MOD: **34,680 + 9,852 = 44,532 bytes**
against 27,466 + 8,972 = 36,438 before (+7.9 KiB: +7,214 image, +880 bss;
96 of them are the iterative chain below).

**20.13.4 Resumability and the work bound.** The decoder runs in *fills* of at
most 256 output bytes into `ob`: one literal, one stored byte or one symbol's
match at a time, a match that does not fit finishing in the next fill
(`mleft`, `mdist`). Input is the stage below (`sget_lo`), so the raw bytes and
every lower stage are still counted against the 4 MiB bound as before, and a
fill that finds the input ended fails with `PDE_TRUNC` rather than waiting.

*The chain has no recursion (D21).* `pdestrm.c` walks the filter stages with
one index instead of one call each: ASCIIHex and RunLength are push machines
(`st_step`: offered nothing, a stage gives a byte it holds or asks for one;
fed a byte of the stage below, it gives a byte, asks again or ends), and
`sg_run` goes down while a stage asks and up while one gives. The Flate stage
pulls bits in the middle of a header and cannot be a push machine, so it is
the bottom source of the stages above it (`sg_flate`) and reads the stages
below it through `sget_lo`, which holds no Flate stage: `sget -> sg_flate ->
fl_get -> ... -> fl_infill -> sget_lo` is a chain, not a cycle, and
`tools/os88pdstk.py` passes with no allowance for it. Every byte each stage
produces is counted as before, a stage that ends stays ended, and a stage
that asked for input is fed the end of the stage below it too (an odd final
hex digit still decodes). Behaviour is unchanged byte for byte (the eng and
flate host matrices, 1,170 chain/size/bomb cases, both target harnesses);
what changed is speed on a pathological ASCIIHex-over-RunLength whitespace
bomb, about 1.6x slower to reach `PDE_LIMIT` on the target, and 0.4
instructions a byte on Flate content.
The history is written once at the end of each fill and read only for a match
whose source lies before the fill's first byte (a source inside the fill is
read from `ob`). The top Flate stage's bytes are handed up in bulk
(`fl_take`) instead of a call each, and counted against the work bound as
`sget` would have. A block that decodes to nothing is still work, and nothing
the stages produce would count it, so a stream of empty blocks would run its
whole length inside one call: more than **64 block headers with no output
byte between them** is `PDE_LIMIT`, and each block header is charged to the
4 MiB work bound as if a stage had produced bytes - **1,024** for one that
builds tables (dynamic, or fixed after a dynamic one), 16 for any other. A
legitimate stream does not come near either (zlib writes a block per ~16 K
symbols; one sync-flushed every 300 bytes into dynamic blocks would stop at
about 950 KB instead of the 1 MiB output bound); a hostile one of one-literal dynamic blocks stops after about
4,000 of them. The most work between two returns of a byte is then a
block start: a dynamic block's header (up to 316 code lengths, two tables,
the lookups) plus the first fill, measured at **108,102 instructions** for a
2 KB stream and **188,550** for a 240 KB one on the target code (20.13.6);
the work after it is bounded by the caller's `STRM_READ` size as for any
stage, and the number of blocks by the charge above.

**20.13.5 A failure surfaces once.** It is parked when found and reported, as
`lx_err`, when the bytes decoded before it have been delivered: the `STRM_READ`
or `TOKEN` that reaches it returns the failure (and the bytes of *that* call,
as for a bad hex digit, are void), later reads see the end of the stream.
`PDE_FLATE` at `STRM_OPEN` is only "no flate claim".

**20.13.6 Verification.**
* `make pdflate` (host): `hosttest/pde_fl_check.py` drives the real engine with
  the flate claim over PDFs it writes, byte for byte against Python's `zlib`,
  a predictor written in the script from the specification, and `qpdf
  --filtered-stream-data` as the independent decoder. 938 engine runs in all: every
  block type (stored, fixed, dynamic, Z_HUFFMAN_ONLY, Z_RLE, flushed
  multi-block), eight data sets including distances to 32,768 and a 3 MiB
  bomb cut at 1 MiB with `PDE_LIMIT`, five read sizes from 1 to 4,096, three
  cluster sizes and cache sizes to 64 KiB; the filter chains; every PNG tag at
  gray/RGB/RGBA/16-bit and 1/2/4 bits, rows to the 4,096-byte limit and over
  it, TIFF at 8 and 16 bits; the parameter forms; nested reads of two streams
  with one and two slots; and 537 malformed streams (every truncation point,
  random bit flips, named malformations) whose output must equal an
  independent inflater's up to the failure and whose code must be
  `PDE_SYNTAX` or `PDE_TRUNC` exactly when it is.
* `make pdflatefuzz`: the same engine under AddressSanitizer and UBSan
  (`hosttest/pde_fl_fuzz.c`) over 24,000 mutations of six streams, with a
  guard pattern around the flate claim checked after every run: no crash, no
  write outside the claim, no stream over 1 MiB.
* `make pdflateboot` (`hosttest/pde_fl_boot.py`): the **module as the target
  toolchain built it** (SmallerC, `cc8086.py`, `nasm cpu 8086`) booted in QEMU
  with the flate claim, twice: the shipped module (hand-written symbol loop,
  20.13.7) and the same source built with `-DFL_NOASM` (its C twin, in
  `build/flc/`). Each runs 70 streams - three read sizes, nested reads, a
  corrupt stream, and a corpus of every block type (stored, fixed, dynamic at
  levels 1/6/9, Huffman-only, RLE, flushed multi-block, a 9-bit window), a
  history distance of exactly 32,768, 28 malformed streams (bit flips,
  truncations, a block type 3, a bad stored length) and 10 built by hand for
  the symbol loop's edges (length 258 as symbol 285 and as 284 + 31; symbols
  286 and distance 30; a distance before the first byte, and one exactly at
  and one past the history across a fill boundary; distance 32,768 with
  exactly 32,768 bytes of history and with one fewer; history copies that
  straddle the 32 KiB ring's end), the malformed and hand-built ones read a
  byte a call - and the result lines are compared with what Python's inflater
  yields **and with each other**: 0 of 70 differ. **The comparison is proved to bite** by a
  negative control in the same run: the shipped module's literal store is
  patched in the image (`mov [di],al` -> `mov [di],ah`) and the run must
  fail, which it does (63 results wrong against the expected values, 64
  against the C twin); a second control breaks the history copy's ring mask
  (`and si, 0x7FFF` -> `0x7FFE`) and must fail too (39 and 40), which proves
  the corpus reaches the history path. The harness refuses a PDF that would
  grow into the flate slots it places in memory. Boots use `-icount shift=0`, so the BIOS and the PIT
  count guest instructions (1 ns each). Instructions per output byte,
  240,000 bytes of Flate text: **90.6 as C, 63.4 as shipped**; the same kind
  of text unfiltered 20.7; a PNG-predicted 8-bit gray image 243.2 as C, 190.1
  as shipped. QEMU counts one `rep movsb` as one instruction, so the
  asm figure flatters it (an 8088 pays about 17 clocks a byte there); clock
  counts need MartyPC, the lead's final pass. `pde_fl_check.py` (`asmlayout`)
  checks `pdeflate.inc`'s FL_* offsets and table addresses against
  `struct flst` and `pdeflate.h` on the 16-bit layout.

**20.13.7 The symbol loop in assembly.** `_fl_codes` (`pdeflate.inc`,
included by `pdeng.asm`; defined only when `FL_ASM`, that is on the target and
not under `FL_NOASM`) decodes literal/length and distance symbols through the
slot's lookups (read directly through ES, no call per table word), their
extra bits, and the match copy - a source inside the output buffer by
`rep movsb`, one before it from the history - until the 256-byte buffer is
full or the block ends, then returns. A code longer than the lookup is
resolved by C's walk (`fl_slow`) and the read-ahead is refilled by C
(`fl_infill`); everything else (headers, tables, stored blocks, predictors,
the slot swap) stays C. Its C twin is the `FS_CODES` case and `fl_match()`
of `fl_fill()`, which the host harness runs and the target builds under
`-DFL_NOASM`. Input is read ahead 64 bytes at a time from the file's
lookahead when Flate is the first stage (one byte at a time through a lower
stage, so its errors and bounds are unchanged).

**20.13.8 Not done.** `/Predictor` with more than four colours or a TIFF
sub-byte depth (`PDE_FILTER`); a second Flate stage in one chain; the Adler-32
check; LZW and every other filter (still `PDE_FILTER`); inline-image Flate
(`BI ... /F /Fl`: PDREND's, `annotr`). The page's *rendering* of a Flate
stream was not looked at on screen: the package still ships the stand-in
PDREND (`PD_REND_MOD`, the lead's hold), which reads no stream, and the
primary carves the flate claim at claim B's tail (`pdumod.c`, `pdu_doc_claim`):
two slots when the largest free run holds B, both slots and the reserve (32 KiB plus a save's 48 KiB peak, D28),
one when one fits, none otherwise. On a kernel whose second slot's selector is
not `flate_seg + 2752` (kern_mega), one slot. The Control Panel low-memory
setting D17 names is not built; until it is, the rule is by RAM alone.

### 20.14 Host preparation of complex PDFs (`host`, wave 2, PD-22)

`tools/os88pdprep.py` is the optional host command for a PDF outside the XT
profile (1.3). It is a Python 3.11 script over three independent programs,
`qpdf` (the rewrite), `mutool` or `pdftoppm` (the raster) and nothing else:
no pip package, no Microsoft or Adobe binary, no network. Every other part of
PageDesk works without it; a prepared file is just a PDF.

#### 20.14.1 Command

```
tools/os88pdprep.py IN.pdf OUT.pdf [--flate] [--raster none|auto|all|2,5-7]
    [--fonts keep|raster] [--dpi N] [--bits 1|8] [--target vga|cga|herc]
    [--strip-bytes N] [--no-text-layer] [--pages RANGE]
    [--password PW | --password-file F] [--force] [--pde-test PATH] [--report FILE|-]
```

| exit | meaning |
|---|---|
| 0 | prepared and verified |
| 1 | refused, with the reason on stderr (below) |
| 2 | usage |
| 3 | a needed tool is missing (`qpdf`; `mutool` or `pdftoppm` for `--raster`): nothing was written, and it is never reported as a pass |
| 4 | the derivative was written and FAILED verification: it is left in place and the report says why |

Refusals: IN is not a file or has no `%PDF-` in its first KiB; OUT is IN
(`samefile`: not even with `--force`); OUT exists without `--force`; the file
is encrypted and no password was given (`PDE_ENCRYPT`), or the password is
wrong (a file with an EMPTY user password - owner restrictions only - is
refused too, saying that `--password ""` decrypts it); OUT is a directory; the
report would land on IN; no pages; more than 1,024 pages (`PDE_LIMIT`;
`--pages 1-1024` prepares a range). The original is only ever read, and OUT
appears only after qpdf finished: qpdf writes a fresh `mkstemp` name beside
OUT, which is renamed over it (never `OUT.tmp`, which an input could be
called). `--password-file` uses the file's first line.

`--flate` is the Enhanced tier: Flate streams are kept (or re-made from LZW and
ASCII85) for a build that has PD-25. The default is the XT tier: **no Flate
at all**, which a 4.77 MHz machine with 640 KiB reads without a decoder, at the
price of a larger file. `--raster` defaults to `none` so that nothing is
destroyed unasked; the report names every page that will show *Incomplete
rendering* and the command that would replace it.

#### 20.14.2 What is rewritten

One `qpdf` pass (`--object-streams=disable`, `--stream-data=uncompress` on the
XT tier, `--newline-before-endstream`, `--deterministic-id` - on BOTH passes:
an input with no `/ID` otherwise gets a random one from the first) gives, by
construction, everything PDENG's profile lists:

| input | derivative |
|---|---|
| xref stream, object streams, hybrid file, linearization | one classic `xref` of 20-byte entries in a single subsection, no `/ObjStm`, no `/XRef` |
| incremental revisions (`/Prev`) | flattened to one revision (the report says how many there were) |
| object numbers | renumbered 1..N (N is reported against `PD_MAX` 65,535, D8) |
| `FlateDecode`, `LZWDecode`, `ASCII85Decode`, `ASCIIHex`, `RunLength` and predictors | decoded (XT tier), or Flate again (`--flate`) |
| `/Encrypt` | decrypted only with a supplied password; the derivative has no encryption, no `/Perms`, no permissions |
| `DCTDecode`, `JPXDecode`, `CCITTFaxDecode`, `JBIG2Decode` images | left as they are: their page is *incomplete* and is the raster's business |
| `/Outlines` | kept; dropped with a warning when `--pages` selects a subset, because its targets are gone |
| `/Names /Dests` name tree | kept when its keys are in order; REBUILT as a sorted two-level tree (leaves of 128 pairs) when they are not (a real producer's file had them out of order, which fails `qpdf --check` and a binary search), and pruned to the pages kept under `--pages` (31 pages of a 4,234-page manual kept 58 of 9,066 destinations) |
| `/Perms`, signature byte ranges, XFA | kept as data, with a warning: rewriting invalidates a signature |

A stream decoded past the engine's 1 MiB cut (7.5, 20.2.2) is not fixed by
decoding; the classifier (below) names it.

#### 20.14.3 What the classifier calls incomplete

For each page the tool walks the page's content streams and the Form XObjects
they draw (4 deep), with the page's inherited resources, and lists what 1.3
refuses or draws with a stand-in. **Hard** (a reason to rasterise, named in
the report): `sh`; a pattern colour or `/Pattern`; an ExtGState `/SMask`, a
`/BM` other than Normal, `/ca` or `/CA` below 1; a colour space that is not
DeviceGray/RGB/CMYK (ICCBased, Indexed, Separation, DeviceN, Cal*); an image
with a filter PageDesk lacks, an `/SMask` or `/Mask`, a colour space other than
DeviceGray/RGB, a depth other than 1 or 8, or a decoded size over 1 MiB; an
inline image over 4,096 bytes or with such a filter or colour space; a Type 0
(CID), Type 3, Symbol or ZapfDingbats font; a content stream or form over
1 MiB; a page box outside +/-2,047 pt or empty. **Soft** (drawn with the
substitute face, reported, rasterised only with `--fonts raster`): TrueType and
non-base-14 Type 1 fonts; render modes 4-7.

`--raster auto` takes the hard pages; `all` every page; a list (`2,5-7`,
numbered after `--pages`) exactly those; `none` leaves every page vector.

#### 20.14.4 A rasterised page

The page is replaced in place - **its object number stays**, so outline and
link targets still resolve - by `/Type /Page`, the parent, a new `/MediaBox
[0 0 W H]`, `/CropBox` equal to it and `/Rotate 0` (both are INHERITABLE: left
out, a `/Pages` node's would crop and turn a picture that has them baked in),
with Annots, groups and the other boxes dropped, and new contents:
the page as MuPDF (or Poppler, `pdftoppm -gray`) draws it, with `/Rotate`, the
CropBox and the source's annotation appearances baked in, cut into horizontal
**strips**, each an Image XObject (`DeviceGray`, `--bits` 1 or 8) of at most
`--strip-bytes` decoded (default 60,000: inside one 64 KiB segment, far under the
1 MiB cut). A row is at most 1,024 bytes, PDREND's image row buffer (20.3.2
refuses a longer one), so a page wider than 1,024 px at 8 bits (8,192 px at 1)
is cut into COLUMNS as well; the report names them. 1-bit strips are Bayer 4x4
ordered-dithered; strips are RunLength-encoded when that is smaller (XT tier)
or Flate (`--flate`). Each is placed by its own `q ... cm /Sn Do Q`. A page
larger than 2,000 pt in either direction is shrunk so it fits +/-2,047 pt -
rendered ONCE at the reduced resolution, from its box (a 14,400 pt page is not
first drawn as a 20,000-pixel square) - and the report says by what factor.

**Searchable text exists only when it genuinely did.** Where the source page
had extractable text (MuPDF's `stext`), the page also carries an INVISIBLE
layer - `BT 3 Tr`, one `Tj` per word in `Courier` with `Tz` set so the word's
width equals its box in the picture (Courier's advance is 600/1000 em, so no
width table is needed) - which Find, selection and Copy use as for any text. A
word followed by another on the same line carries its space, stretched over the
gap (`(two ) Tj (words) Tj`), so a reader that joins glyphs in content order
gets the words apart.
A word with a character outside WinAnsi is skipped and counted; a font with no
widths gets half an em a letter. There is no layer on a scan, and the report
says "searchable text: none". `--no-text-layer` omits it. Rotated lines are
not given boxes.

New annotations PageDesk adds to such a page are ordinary vector records
(13.3); the source's annotations on it are part of the picture.

#### 20.14.5 The stamp and what PageDesk can show from it

The derivative's Info dictionary gains `/PageDeskPrep`, one text string:

```
os88pdprep 1.0; qpdf 12.4.2; src-size=N; src-sha256=<64 hex>; tier=xt|enhanced;
  [raster=2,3; dpi=100; bits=1;] pages=N
```

(one line; the line break is for print). It is the only thing binding the
derivative to its original: the original's size and SHA-256, the pages that
are pictures, the tier. A reader that wants to show **Prepared copy** looks
for the key through `PDV_ENG_TRAILER` and `DICT_GET`; a string starting
`os88pdprep` is the signal, `raster=` says which pages are pictures and
therefore whether the document has searchable text (a page in the list has it
only if the report said so; the list does not say, by design - the report file
does). PageDesk itself does not yet read it (open item 1).

There is no side cache and no private container. PD-22 allowed "a
documented versioned container with per-chunk offsets, sizes, checksums";
that was written for a derivative that was NOT a PDF. This one is: the strips
are image XObjects, the fingerprint is in the file, and a stale or wrong chunk
cannot exist because there is no chunk to go stale (a damaged strip is an
ordinary damaged stream, 7.5, and is refused by the engine like any other).
The original is never edited and "restore the edits onto the complex original"
is NOT offered (open item 2); a prepared copy is its own document.

#### 20.14.6 Verification

After writing, the tool re-reads the output with tools that share nothing
with it and prints each verdict in the report: `qpdf --check` (any warning
fails - except a warning the qpdf rewrite of the ORIGINAL gives word for word,
path, object numbers and offsets aside: that defect was inherited, not made
here, and is printed as such, e.g. a damaged JPEG the page still draws through),
`tools/os88pdfcheck.py validate`, its own profile check (classic xref
at `startxref`, every entry 20 bytes, no `/ObjStm` or `/XRef`, subsections at
most `PD_MAXSUBSEC`, object numbers under 65,536, no `/Encrypt`, page count) and,
when `build/pde_test` (or `--pde-test`, `$PDE_TEST`) exists, **PDENG itself**:
`pde_test dump` must open the file with the right page count and decode and
tokenize every content stream. A page that the report lists as still
incomplete may be refused by the engine (the page-box case is `PDE_RANGE`) and
is then expected; any other refusal fails the run (exit 4). The Enhanced tier
is not run through PDENG (it has no Flate until PD-25).

#### 20.14.7 Checks and numbers

`tests/unit/t_pdprep.py` (fast, about 1.1 s, proposed row `pdprep`): the
RunLength encoder round-trips; the dither's white, black and grey; the
tokenizer through strings, arrays, hex, comments and an inline image whose
data holds a false `EI`; page lists; then on a generated six-page file that
qpdf has turned into an object-stream + Flate PDF - a native page, a shading,
a transparent TrueType page, a 2,500 pt wide page, a JPEG page and a long inline
image - the report's every verdict, no `/ObjStm` or `/XRef` or Flate in the
output, the profile check, `qpdf --check`, strips under the cap, the Info
stamp (the original's SHA-256), MuPDF extracting the invisible words, the
output byte-identical on a second run, the refusals (in place, existing,
encrypted without and with a wrong password), a decrypt with the right one (the first line of a `--password-file`), and
a **negative control**: the profile check must fail the object-stream input.
Then what the verifier found, each check failing on the first version (nine
FAILs against it, 0cebd288e): the report never lands on the input; an input
NAMED `OUT.tmp` is untouched; an input with no `/ID` still prepares
byte-identically; a page under a `/Pages` node with `/Rotate 90` and a CropBox
comes out with `/Rotate 0` and its own CropBox; an 8-bit raster 1,650 px wide
is cut into columns of at most 1,024 bytes a row; an unsorted `/Dests` tree is
rebuilt sorted and `qpdf --check` is clean; and the invisible words carry their
spaces. It SKIPs, never passes, without `qpdf`.

Measured on this host (qpdf 12.4.2, MuPDF 1.28.6): XTGUIDE.PDF as an
object-stream/Flate file (9,555 bytes) becomes a 27,948-byte classic file
PDENG opens (PDENG: XREFSTM before); an HTML page converted by MuPDF with CID
fonts rasterises at 80 dpi, 1-bit, to one 3,861-byte strip with 22 invisible
words. verify-host ran it over 25 more: six macOS system PDFs (ICC colour,
transparency and blend modes, an 18-page Thai licence in CID fonts -> 36 strips,
920 KB), the same guide linearized, with object streams, with three incremental
revisions, and encrypted RC4-40/RC4-128/AES-128/AES-256/AES-256+object streams;
a generated file with a Type 3, a CID, an Indexed + pattern, a Separation and a
Symbol page and LZW+ASCII85 content and a PNG-predictor Flate image; posters of
5,000 and 14,400 pt (0.9 s, 46 MB, scaled to 14%); 1,024 pages (2.9 s) and
1,100 (refused); and pages 100-130 of a 209 MB, 4,234-page manual (17.7 s,
530 MB resident - the original is read whole) -> 1.56 MB, `qpdf --check`
clean. Every output passed `qpdf --check`, `os88pdfcheck validate`, the profile
check and PDENG, and every raster row is at most 1,024 bytes and every strip at
most 59,920.

#### 20.14.8 Open items

1. The shell/engine should read `/PageDeskPrep` and show **Prepared copy**
   and the raster list (20.14.5): not done, it is the lead's to assign.
2. No merge of a prepared copy's edits back onto the untouched original (PD-22
   allows leaving it out; if wanted it is a separate host command that lifts the
   `PDA_*` records out and re-applies them through qpdf).
3. One scale and one depth per run; no multi-scale pyramid and no per-tile
   checksum container (20.14.5 says why not).
4. Colour (RGB/palette) rasters and CCITT/JBIG2 1-bit passthrough are not
   produced; a 1-bit image whose filter is CCITT is a hard page like any other.
5. Rotated text gets no layer; vertical writing is not recognised.
6. Not run on a real XT: no host check can say how fast a strip draws (the
   lead's final pass measures a prepared copy with MartyPC).

### 20.15 The private module stack (`deepstk`, wave 2, DECISIONS D23)

**Why.** Every module verb ran on the CALLER's stack, which is the UI task's:
512 bytes (`kernel/kernel.asm`'s `STK0_SIZE`) in `LOW_SEG`, sitting directly
on top of `.lowbss`. A PDREND verb is 388-470 bytes of module frames, PDENG
adds 158-236 when PDREND calls it, and the kernel's FAT and `int 13h` go under
that: it ran into `.lowbss` (the menu bar's line buffer on XTGUIDE page 3, the
stack at 510 on page 1, the canary overrun on page 12). D22's cure - hoist
every engine call, make every frame static - was withdrawn for this one.

**The contract** (`apps/pagedesk/pdstk.inc`, `apps/pagedesk/pdkgate.inc`,
`pdabi.h`'s `PD_DSTK_*`):

1. **The stack.** Claim B's last row, `PDU_DC_DSTK`, `PD_DSTK_BYTES` = 2 KiB,
   carved like every row (paragraph offset, `os88_seg_sub`; a selector on
   kern_mega). At each carve the primary fills it with `0xCC` and writes the
   canary `PD_DSTK_CAN0`/`PD_DSTK_CAN1` at offsets 0 and 2, then arms it
   (`pdu_dstkseg`); `pdu_doc_free` disarms it with the claims.
2. **The door** (`pdu_mcall`, the primary's only call into `PDENG`, `PDREND`
   and `PDWRT`). With a stack armed and no verb already down there: bank the
   UI SS:SP in statics; `pushf` / `cli`; SS = the row, SP = `PD_DSTK_MAG`;
   write the **record** - `[2046]` = `PD_DSTK_MAGIC`, `[2044]` = the row's own
   SS, `[2042]` = the UI SS, `[2040]` = the UI SP - so SP = `PD_DSTK_UISP`;
   `popf`; far-call the door. A module that calls another (`PDREND` ->
   `PDENG`, `PDWRT` -> `PDENG`, through `pdxb_init.eng_*`) stays on this
   stack: only `pdu_mcall` swaps. On the way back the answer is banked, the
   stack is **checked** (SP back at `PD_DSTK_UISP`, both canary words whole)
   and the UI SS:SP restored the same way, IF as the caller had it.
3. **Flat.** With no stack armed (no document: the launch INITs) or with a
   verb already down there (`pdu_mcall` re-entered from inside a module's
   kernel call - a modal loop that dispatches this package), the door is
   called where it stands, as before this section.
4. **Every kernel call goes back up.** The kernel cannot run on the deep
   stack: its bodies reach `.lowbss` through SS (`gfx_rowbase`'s
   `vid_rowtab`, `evq_push`'s ring; `os88api.inc`: SS = `LOW_SEG` is a
   requirement of every OSAPI call). Each module includes `pdkgate.inc`
   (one line, after its compiled C and before `cc/os88thunk.asm`), which
   re-defines every slot a module calls from `KERNEL_SEG:0xNNNN` to a near
   **slot** - `call pdk_up` / the original far call / `jmp pdk_dn` - so
   `apps/cc/` is unchanged for everyone. `pdk_up` decides from SS:SP alone,
   with no register and no segment load: **SP below `PD_DSTK_BYTES`** (task
   0's stack is on top of `.lowbss`, so its SP is never below 2 KiB) **and
   the record's magic and self-SS match** -> with IF off, bank the deep SS:SP
   ON THE UI STACK under a nonzero word and stand on the record's UI SS:SP;
   otherwise push a zero word where it stands. `pdk_dn` takes the word back
   and, if nonzero, goes back down. Every register and flag crosses both
   ways (the API's CF/ZF/DF come back), BP is never touched (`pdr_blit`
   passes the stride in it), and what must survive the kernel call lives on
   the UI stack, so a re-entered module cannot overwrite it. A slot is
   assembled only when the C names a thunk that makes that call (the
   `CC_U_` gating `os88thunk.asm` uses), and **after the table `KERNEL_SEG`
   is re-defined to an undefined symbol**, so a module calling a slot the
   table does not list does not assemble. `tests/unit/t_pdkgate.py` holds
   the BUILT modules to it: every `9A off 60 00` is in a slot, one `pdk_up`
   and one `pdk_dn` per module.
5. **An overrun is loud.** A failed check sets `pdu_stkbad`, answers the verb
   CF = 1, AX = `PDE_STACK` (35), posts ONE wake, and from then on
   `pdu_mcall` refuses every verb `PDE_STACK` except the three that let go
   (`PDV_REND_ABORT`, `PDV_ENG_CLOSE`, `PDV_WRT_ABORT`). The wake's first act
   (`pdu_stk_wake`, `pdumod.c`) closes the document and puts *Out of module stack: the document was
   closed.* on the status line; giving the claims back re-arms the stack for the next open. The
   fill and the canary are left as they were for a debugger. A check after
   the fact cannot undo the overrun: in real mode a stack that passes offset
   0 wraps to the top of its 64 KiB, outside claim B (on kern_mega the row's
   limit faults instead) - which is why the static gate below exists.

**Why the kernel tolerates a foreign SS for the length of a verb.** SeaBIOS
already forces the case on every `int 13h` and `int 16h` it serves on a stack
of its own, so every ISR path has been made to stand it:

- `sch_switch` (`kernel/sched.inc`, `mov ax, ss` / `cmp ax, LOW_SEG` /
  `jne sch_isr.tres`) refuses to switch off a stack that is not `LOW_SEG`'s,
  so no task record ever holds a deep SP; the cost is "not switching this
  tick", the cooperative watchdog's forced switch included.
- `sch_isr` chains the ROM's `int 08h` in place on a foreign SS (the same
  `cmp ax, LOW_SEG` before `.chplain`), and the `FSXF_RATE` hook is refused
  there (`sch_rhook`), which PageDesk never arms.
- both mouse ISRs bank SS:SP and stand on `LOW_SEG:mou_pstack` whatever SS
  was (`MOUPRIV_ENTER`, `kernel/mouse.inc`; kern_mega banks through its data
  alias).
- the keyboard ISR (`kbm_isr`) writes nothing through SS: the keypad mouse's
  events are posted in task context (`kbm_p5spend`), not from the ISR.

**The no-preemption window.** While the UI task is on the deep stack it is
not pre-empted, so the window is one verb - one bounded slice (15.1). Disk
and drawing are NOT in it: they are kernel calls, made on the UI stack, where
pre-emption works as before. The final pass measures the longest window on
MartyPC.

**kern_mega** (checked in `kernel/mega.inc`, not run here). The row's
segment is a `SEG_SUB` selector: an LDT descriptor from `mega_ldt_alloc`,
TI = 1 and RPL 0, access byte `0x92` (present, DPL 0, data, writable,
expand-up), limit = the asked paragraphs x 16 - 1, clipped to claim B's end -
here 2,047. Packages run at CPL 0, so loading it into SS is legal and SP
stays inside the limit (pushes from 2,048 down; an overrun past 0 faults
`#SS` rather than wrapping). It is re-loaded from memory after every kernel
call (`pdk_dn`, `pdu_mcall`), so a compaction that moved claim B during a
call is followed. One residual hazard, not taken here because it is the
kernel's: `mega_svc` reflects an OPTION ROM's IRQ (`mega_irqrom`, the
PicoMEM's) that lands on a stack that is neither `LOW_SEG`'s nor a lent one
IN PLACE, giving the ROM the selector as a real-mode SS. A WIN16 program's
stack has the same exposure; `SEG_LEND` is the kernel's answer and has one
lender at a time, so PageDesk does not take it. The deep stack is used on
kern_mega: the flat alternative overruns `.lowbss` on every machine.

**Cost.** 2 KiB of claim B (16.2, 20.6.2). The gate: about 118 bytes of body
in each module plus 10-11 bytes a slot - `PDENG` 3 slots (+148 B), `PDREND`
9 (+224 B), `PDWRT` 6 (+170 B), the stand-in renderer 5 - and on an 8088
about 90 clocks of instructions a kernel call (~20 us at 4.77 MHz, the UI
stack's 8 bytes) against 756 us for the cheapest drawing call. The primary's
`pdu_mcall` grew by the swap and the check (~90 bytes).

**Budgets (the static gate, `tools/os88pdstk.py`, rendfix).** The door is
entered at SP 2,036 (the record, then the far return); offsets 4-2,035 are
the module chain's and the interrupts'. D23 reserves 256 bytes for the
interrupt floor (an IRQ frame, `sch_isr`'s, the ROM's `int 08h`/`09h`), so
the module chain from the door's entry - nested `PDENG` included, plus 6
bytes of gate at the leaf - is <= **1,776 bytes**. The UI side is the
primary's chain + `pdu_mcall`'s 10 + 8 a kernel call (the banked deep SS:SP
and the far return) + the API's own depth, <= 384 (D22 item 1).

#### 20.15.1 Measured

On QEMU (kern_big, 640 KB, the REAL `PDREND`, `XTGUIDE.PDF`), read by dumping
memory: the deep stack's 0xCC fill from its canary up, and task 0's stack
filled with 0xCC through QEMU's gdbstub below the UI SP the record banked
(`pdu_mcall` was 80 bytes into task 0's stack) and read back after paging:

| adapter | pages | deep stack, deepest use | task 0, high-water |
|---|---|---|---|
| VGA 640x480 | 1 | 332 of 2,036 | - (filled after page 1) |
| VGA 640x480 | 1, 2, 3, 12, then 11 back to 1 | **448 of 2,036** | **258 of 512** |
| CGA 640x200 | 1, 2, 1 | **386 of 2,036** | **252 of 512** |

The menu bar stayed whole on every page (the corruption was on page 3), the
View menu opened over page 1 afterwards, and the machine ran on. The refusal
was driven on CGA by zeroing the canary through the gdbstub and paging: the
document closed with the line above, and *Sample guide* reopened it on a
re-armed stack. Not run: kern_mega and kern_small, MartyPC (the no-preemption
window), Hercules and EGA. The final pass re-takes both water marks with
`tools/stkwater.py` / `tests/stk0water.py` on every adapter.

### 20.16 Insert pages from another PDF (`insert`, wave 3, PD-19)

Source: `apps/pagedesk/pdwin.c` (PDWRT: the foreign windows), the `gc_ins`
arms of `pdwgc.c`, `pm_prep`/`pm_step` in `pdwpg.c`, `pdovlpage.c` (the
command, the prompt, op 5) and the File row of `pdupage.c`. Host checks:
`make pdwrt-real` (`hosttest/pdw_real.c` round I, `pdw_foreign.py`'s
fixtures, `pdw_pgcheck.py`'s I1 checks against qpdf and Poppler).

#### 20.16.1 What the user does

File > **Insert from PDF...** (File row 9) raises the OS file dialog; the
chosen file is not opened. The status line then asks for the pages of that
file, `Insert NAME pages (3, 2-4): ` (built from the row on each key, no buffer), and Enter inserts
them after the page the command is for (the current page, or the Pages
panel's selection; Move page moves them afterwards). One run of consecutive
pages is one page operation, so one Undo takes it back. The inserted pages
are map entries (20.16.2); the view cannot draw a page it has not read, so
each shows as **paper** (as a blank does, 20.11.1) until Save As, whose
output opens with the real pages. The file's page count is therefore not
known when the range is typed: a range past its end is refused by the save
(`PDE_RANGE`, `err_detail` 17) and nothing is written. Up to three
different files per save (`PDM_NSLOT`), at most 240 pages in the view
(20.11.1). Extract refuses a list that holds an inserted page, and Duplicate
page refuses an inserted page (the writer takes a file page once, `err_detail`
15), both with "Save first." - the extract pipeline reads one
document. Insert blank page after an inserted page takes page 1's size.

#### 20.16.2 The map and the row

`PDM_FOREIGN` entries carry `idx` = `PDM_FORE(slot, page)` (slot 0-2 in bits
11-10, the file's 0-based page in bits 9-0, `PD_MAXPAGES` = 1,024). The row
`PDU_DC_PAGES` holds, from byte `PDM_NAMES` = 960, slot k's 8.3 name at
960 + 14k (NUL) and its folder at 1,002 + 4k: the cluster word then the
volume + 1 (never 0). A slot is *in use* while a map entry names it **or
the undo history may bring one back** (`pdu_insmask`, a bit per slot named
since `ud_init` - a new document - or since the history was last empty): an
Undo of Delete page or a Redo of an Insert restores map entries that name
the slot by number, so renaming it would save another file's pages in their
place. The command reuses the slot whose name and folder are equal, else the
lowest free one, else refuses ("3 files at most."). A foreign
page is a duplicate for edit's purposes (`pdu_pg_isdup`) and `pdu_pg_id` is 0
for it (the placeholder takes page 1's size). It owns no edit records, as a
blank or a duplicate owns none (13.7): a new mark on any of them is refused
in words ("Save first.", `pdutool.c`). The edit store keys a
mark by the page ID, `pdu_ed_pg()` = `pdu_pg_id(pdu_page)`, which the
renderer draws by and the writer saves by, and the notes panel maps through
`pdu_pg_id` and its inverse `ovl_pg_view` (verify-insert: until then a mark
was keyed by its VIEW index, so after any Insert, Move or Duplicate it was
drawn and saved on another page). The same file page twice is refused (the
writer: `err_detail` 15).

#### 20.16.3 The writer

`PDV_WRT_SAVE` with a map that holds `PDM_FOREIGN` entries (a map is then
always on). `pm_prep` gives every foreign page its **page object number up
front** (`T_PMNUM`, after the page-operation objects, before the annotations'),
so `pm_tree` writes `/Kids` in view order *before* any foreign byte is read:
the tree needs no document but the source's. The job then runs, per used
slot, a **foreign window** after `PH_TREE`:

1. `PH_FOPEN` - close PDENG's document (`PDW_OUT_REOPEN` is ORed into
   `mode`: from here the primary always re-opens the source, on success,
   failure and cancel alike), stand in the file's folder
   (`pdw_src_dir`), `PDV_ENG_OPEN` it (resumable), take its `PDV_ENG_TRAILER`
   (the source's own trailer is saved and put back after the last window),
   check every chosen page against `npages` (`err_detail` 17), carve the three
   bit maps of 20.11.4 from the **end of the object table** (sized from the
   file's `/Size`; the table gives up that much capacity for the window).
   OPEN's refusal is the job's: encrypted, `/Size` damage, Flate - the same
   words as opening the file.
2. `PH_XROOT`, `PH_XDISC` - Extract's discovery over the file's pages
   (`gc_ins`). Two Insert-only refusals: any reachable dictionary with
   `/Subtype /Widget` or a `/JS` key (a form field, a script) answers
   `PDE_LIMIT`, `err_detail` 18, naming no page (the primary's line says "form
   fields or scripts"). `/Type /Page`, `/Pages`, `/Catalog` objects that are not
   chosen pages are not copied and their references become `null`, as in
   Extract, so a link to another page of the file is dropped (13.8). At the
   fixpoint the chosen pages are **removed from the seen set** so the rank
   of every other object counts only copied objects.
3. `PH_XCOPY` - every other seen object in ascending file order, numbered
   from the window's base (the first free number after the annotations, or
   the previous window's end) by rank: dictionary rewritten by `rs_walk`, stream
   bytes `stream`..`endobj` verbatim. A reference to a chosen page maps to its
   pre-assigned number (`gc_newnum`'s fallback).
4. `PH_FROOTS` - the chosen pages, with their pre-assigned numbers: `/Parent`
   the document's root `/Pages`, `/Rotate` final (own + added quarter turns),
   `MediaBox`/`CropBox`/`/Resources` materialised from the inherited values -
   `/CropBox` and `/Resources` ALWAYS (an empty `<< >>` when the file's tree
   has none), because under the source's root the page would otherwise take
   up the source root's own inheritable ones.

A failure inside a window names the file: `err_detail` 20 + slot, unless the
detail is already set or the error is the target's (`PDE_DISKFULL`,
`PDE_WPROT`), which the primary words as for any save.

Within a window a resource used by several pages is copied **once**; across
windows (different files) nothing is shared, so the growth is bounded by the
three files' reachable objects, never by the page count. Object numbers and
resource names of the foreign file are irrelevant: numbers are new, names
are per-page scoped. The output is checked by 14.2 step 4 as ever. The source
document and the foreign files are only read; the output is `PDSAVE.$$$` until
verified, so a failure or Esc leaves everything as it was.

Not carried: the foreign catalog (outlines, names, AcroForm, page labels),
`/NM` of copied annotations is kept as it is (it may repeat one already in
the document), the document `/Version` is not raised for the foreign file's
features. Limits: 3 files, 1,024 pages a file, 2,432 reachable objects a
window less those already written (`err_detail` 16), a dictionary over
`PDW_PGBUF` (14).

#### 20.16.4 Sizes

Image + bss, measured after the verifier's fixes: PDWRT 47,724 (D24's cap
49,152, +2,342); the primary 50,490 + 6,319 = 56,809 (+274 against 56,535;
D27 holds insert to +280 of the primary, so the prompt is built in `pdu_line`
from the row rather than kept in a buffer, and the lines are short); the
overlay part 43,113 (+1,856 against 41,257, share +2,600); PDENG and PDREND unchanged. One resident literal, "Save first.", serves the
three refusals (a mark, Duplicate, Extract). The
save claim is 40 KiB whenever the map holds a foreign page (no new claim, D12);
the three bit maps live in the object table's tail for the window only.
`t_pdstk` stays at 366 of 384 on `cc_onkey`: the command, its prompt and op 5
run in the overlay from the key's chain no deeper than the Extract prompt did.

#### 20.16.5 Checks

`make pdwrt-real`: round I of `pdw_real.c` inserts three pages of a file next
to PAGES.PDF and one of a copy in another folder, byte-compares the source and
the foreign files afterwards, re-opens the output through PDENG, then drives the
refusals - a page past the end (17), a missing file (named, 20), a form-field
file (`ACROFORM.PDF`, 18), a full disk and Esc both in `PH_COPY` and INSIDE a
foreign window (PDENG holding the foreign file; the disk-full one must not
blame the file) - each of which leaves no `PDSAVE.$$$` and no target, and the
in-window ones `PDW_OUT_REOPEN` set. `pdw_pgcheck.py` I1: `qpdf
--check`, text, rotation and Poppler pixels of all twelve pages against the
source pages, every inserted page carrying its own `/MediaBox`, `/CropBox` and
`/Resources`, one copy of the shared font and form per inserted file, the link's
`/Dest` following its page, `/P` `/Popup` `/Parent` rewritten, and the source
file the output's prefix. On the glass (QEMU VGA): File > Insert from PDF...,
MARKUP.PDF pages 1-2 after page 1 of PAGES.PDF, Save As, the output opened with
ten pages and read back with `mcopy`: `qpdf --check` clean, pages 2 and 3 equal
MARKUP's pages 1 and 2 pixel for pixel.

### 20.17 The recovery journal and the interrupted-save offer (`recov`, wave 3, PD-20)

Files: `pdurecov.c` (the resident part: three flags, the hooks' bodies, the
prompt's key), `pdovlrecov.c` (every body that touches a file or the store, an
`ovl_*` set in the overlay), marked one-line hooks in `pduundo.c`,
`pdujob.c`, `pduedit.c`, `pducmd.c`; `hosttest/pdu_jr_test.c` is the host test
(`tests/unit/t_pdjr.py`). It refines 14.7 and 14.3; where it differs the
differences are listed in 20.17.7.

#### 20.17.1 What is journaled, and what is not

The journal is the **edit store's state after a command**, not a log of
commands: every record, whole (its 48 bytes, its text, its points), in index
order. Undo, redo, move, resize and typing therefore cost nothing extra to
journal - the record is now what it is - and replay never needs the undo
history, only 20.8's creation call. **Page operations are not replayed**
(20.11 keeps them in the page map, which needs the history to rebuild in
order). A record on a page a page operation created (id with bit 15) is
written as a deleted placeholder, so the others keep their indices. Recovery
restores marks on the source's own pages only.

#### 20.17.2 Where, and the file

`SYSTEM/APPDATA/PAGEDESK.JRN` on the **boot volume** (`os88_vol_sys()`, 14.7;
the instance stands there by `OSAPI_FILE_GOTO_QM` and goes back, 19.9). The
folder is **walked from the root on every visit and never banked**: every
visit may write, and SPEC.md 19.9.1's rule is that a banked visit never
creates - on a boot floppy swapped since the last visit the banked cluster is
a cluster of somebody else's disk, and a full `OSAPI_FILE_WRITE` there writes
a directory entry into that disk's data (verify-recov). One
file serves the whole machine: it belongs to the most recent session that
wrote it, and the offer is only ever for the journal that is there at open.

All words little-endian. Header, 36 bytes: `PD` `J1`, the source's 8.3 name
(14, informational), 2 pad, the 8 bytes of the engine's fingerprint (6.2:
size, CRC of the first and last KiB, `pdxb_fingerprint`), 6 reserved, CRC-16 of the first 34 bytes. Then one record per store
index: `len` (2: 48 + text + 4 x points), the 48-byte `pd_edit`, its text, its
points, `crc` (2: CRC-16 of the `len` bytes before it). Nothing else.

**Deviation from 14.7 ("append-only"):** the file is rewritten whole by one
`OSAPI_FILE_WRITE` after each burst of commands. The append the volume offers
refuses a file that is not a whole number of clusters (18.4.4), so an
append-only journal needs padding, a compaction pass, a record for a store
that shrank and a last-record-wins replay: about 1,050 bytes of overlay more
than the D25 share. The cost of the rewrite is a window of one call (the old
chain freed, the new one written) in which a crash leaves no journal, and a
write proportional to the store (typically one or two clusters). A journal of
more than 8 KiB (one staging claim) is not written: it turns itself off, said.

#### 20.17.3 When it is written

Never inside a callback, never per keystroke, never during a render, a job, a
popover or a typing session. `pdu_undo_end` (outermost) calls `pdu_rc_cmd`,
which records the tick; the wake handler (`pdu_rc_wake`) writes when the
document is open, no job runs and **half a second has passed with no further
command** - so a typing run, a drag or a burst of undo is one write. Cost on
the 4.77 MHz XT: the copy of the store into the staging claim (a few
milliseconds per record), then one file replace: on a floppy the data
sectors, both FAT copies and the directory entry, ~1.2-2 s of the UI task in
which input queues; a hard disk ~0.2-0.4 s. A command that changes nothing
the store holds (a selection, a scroll, a mode) never reaches the hook and
writes nothing. The staging buffer is a transient 8 KiB claim taken
and freed inside the write, so the claim count (16.2) is untouched; a refused
claim skips that write, once, and the next command retries. The header's
fingerprint is asked of PDENG **once per document**, at its first write:
`PDV_ENG_FINGERPRINT` re-reads the first and last KiB of the source and drops
PDENG's block cache, which per write was two more `int 13h` calls on the
source's disk and a cold cache for the next render.

An unrecoverable write (read-only, full, any `FERR`) turns the journal **off
for the session** and says `Recovery off.` once. The file already on the disk
stays: an older state of the same document, which the offer names by its
count.
A volume with no `SYSTEM/APPDATA` says it at the first write too. The
document is never affected.

#### 20.17.4 Retiring it

The journal is deleted when: a **Save As** completes (20.11.5: the output is
the document now and holds the marks); the offer is declined; the close
question is answered `D` (20.17.8); an interrupted-save offer is answered
(20.17.6: the marks are in the temporary file). It is **not** deleted by New
(which also reopens with `pdu_pgsaved`, so the first build deleted the last
document's unsaved marks at File > New), by opening another document, by
Close document, by Extract, by a failed save, or by a document that has no
edits: those leave it as that document's unsaved work, and **every later
open looks at it again**, so opening the source in the same session or the
next offers it. A window closed with nothing unsaved has nothing in the
journal that is not in a file. There is one journal per machine: the first
command in another document replaces it.

#### 20.17.5 The recovery offer

When an open completes, the wake looks for the journal (`pdu_rc_begin` sets
the look as the open starts; the wake waits for the job). It is offered only
if every one of these holds: the header and its CRC are valid; **its fingerprint equals the
fingerprint the engine just computed** (a source that changed since - PD-20's
"present and verified" - is silently ignored and left alone); at least one `R`
record is valid. The status line then reads
`Recover N marks? Enter yes, Esc no.` (N counts the records, deleted
placeholders included) and **every key is the prompt's** (the same rule as
20.11's prompts) **except while a job runs**: a render started under the
prompt says `Esc cancels`, and that Esc is the job's, not a discard of the
journal. It is a line of text and not a dialog, so it is the same on every
adapter. Enter: the file is read into a claim and its records are counted (replay stops at the
first bad one: a bad CRC, a length that does not fit, a torn tail); Enter makes
the records in index order through `pdu_ed_new` inside ONE `pdu_undo_begin/end`
(so Undo takes the recovery back as one step), then restores each record's
`id` and the id counter (a note's reply still points at its note), and the
document is dirty. The replay holds the gfx lock (the store's flush paints
Undo and Redo, and this is the wake). A record whose `pts_n` exceeds
`ED_PMAX` stops it: `pts_n << 2` wraps in 16 bits and would pass the room
test with a 64 KiB copy behind it. The journal stays and the session's next
write replaces it. Esc deletes it. If marks were made meanwhile, or the claim
is refused: `Not recovered.` and the journal is kept. The name in the header
is informational: the match is the fingerprint, so a renamed copy of the same
bytes is the same source; and the fingerprint is 6.2's (size, first and last
KiB), so a same-size change in the middle of the source is not seen - the
same limit as 14.4's `PDE_CHANGED`. The check runs at the **first document
open** of a session and, while a journal is known to be on the disk, at each
open - never at a bare launch, which would fetch the overlay every time (D26's
reason for prefs).

#### 20.17.6 The interrupted save (14.3)

Before the writer is told to replace an existing target (Enter at `... exists`)
the shell writes `PDSAVE.TGT` beside the journal: the target's volume, folder
cluster and name, 18 bytes. It is the only moment in a save at which a failure
can leave the complete new file as `PDSAVE.$$$` with the old target gone, and
the writer has verified the temp file by then (14.2 step 4). It does not
depend on there being any marks. **Each** save that reaches `REPLACE` writes
its own note (the first build wrote one per session once a replace had
failed, so a later replace's note named the earlier target). At the **first
document open of the next session** (20.17.5's look) the wake reads
the note, goes to that folder and: the target is there, or the temp file is
not - the note is stale and is deleted; the target is absent and `PDSAVE.$$$`
is there - the line reads `Finish save of NAME? Enter yes, Esc no.`; Enter
renames it and says `Saved NAME`, Esc leaves the file where it is. Either way
the note **and the journal** are deleted (the marks are in the temporary
file); a rename that fails keeps both and says `Not recovered.`. A note whose
folder cannot be reached (the target's floppy is not in the drive) is
deleted as stale, and `PDSAVE.$$$` stays for the user to rename. A completed
Save retires the note and the journal.

What it can touch: it deletes only `PAGEDESK.JRN` and `PDSAVE.TGT`, in
`SYSTEM/APPDATA` reached by a walk; it renames only `PDSAVE.$$$` in the noted
folder, only to the noted name, and only when that name is absent, so it
never overwrites or deletes a document. The note carries no volume serial: a
different floppy in the noted drive answers for the noted cluster, and the
rename happens there only if that disk holds a `PDSAVE.$$$` in a directory at
that cluster and lacks the name - a copy of the same disk, where it is the
same act. The temporary file is not re-verified (it was, before the note was
written). The offer is on the status line and the mouse is not its, so a
user can ignore it and save: that save's start (`ovl_save_begin` calling
`ovl_rc_drop`) deletes the unanswered offer's note, because the save is about
to write a `PDSAVE.$$$` of its own and, if it died mid-copy, the next session
would offer that PARTIAL file under the old target's name. The old temporary
file stays on the disk; only the offer goes. Each save's own note is written
only on the `PDW_ST_REPLACE` call, after the temp file has been verified, so
a save that dies earlier leaves no note and its partial file is never offered.

#### 20.17.7 `/NM`, deviations and limits

**`/NM`.** PDWRT writes `PD-<nonce hi><nonce lo>-<id>` (20.5.2), the nonce being
a CRC of the *source's* head and tail and size (an incremental save) or of the
save's date, so two sessions on **different** sources never repeat a pair, and a
saved file is a different source from the one it came from. The id alone
restarted at 1. `pdu_ed_init` now seeds the counter with `PDU_RC_SEED()`
(pdu.h): the tick's low ten bits times 16, a multiple of 16 **below 0x4000**,
so 128 records stay below 0x8000 - the top half is PDWRT's, whose duplicated
pages' annotations are `0x8000 + page + k` under the same nonce (pdwpg.c); the
first build's 0x7FF0 bound let a record id reach 0x8060. Two sessions on the
same source repeat an id only when their ticks fall within a few units of each
other on a 56-second wheel, and their outputs are different files whose later
merge (Insert, 20.16) gives copied annotations a new `/NM` anyway. A
recovered record keeps the id it had, and the counter continues above the
largest.

Deviations from 14.7, judged against PD-20's acceptance (verify-recov):

1. **A whole-store rewrite, not an append** (20.17.2). Accepted: PD-20 asks
   for incremental writes "without rendering snapshots", and a burst of
   commands is one write of the store's records, which is incremental in time
   and holds no picture; an append would cost about the same calls a write
   (a cluster, both FATs, the entry) plus the padding, compaction and
   last-wins replay 20.17.2 prices. What it gives up is one window of one
   call in which a crash leaves no journal, and a store over 8 KiB of journal
   (heavy ink) turns the journal off, said.
2. **Page operations are not journaled** (20.17.1). Accepted: PD-20 says
   "recover supported changes"; marks on the source's own pages come back on
   the pages they were on (their `page` is a source page id, not a view
   index), and marks on created pages come back as nothing. Rotations,
   moves, deletes, duplicates and blanks are lost with the session.
3. **The match is the fingerprint, not the name** (20.17.5). Accepted: "a
   stale journal is not applied to a different or externally modified PDF"
   holds to 6.2's resolution, and the same bytes under another name are the
   same source.
4. `PDSAVE.TGT` instead of an `F` record, read at the first open, not at
   launch (20.17.6).

The debounce means a crash inside the half second, or inside a typing run,
loses that run. Limits: 128 records (the store's), 8 KiB of journal (more and
the journal turns off, said), one journal per machine. Not built:
page-operation replay, a journal per document, recovery without opening the
source first.

#### 20.17.8 Closing with unsaved changes (PD-20: Save / Discard / Cancel)

`os88_onclose` asks `pdu_rc_close()` first. A document that is open and
dirty (20.8's save point, `pdu_dirty()`) **refuses the close** (SPEC.md 75.1)
and the status line reads `Unsaved: S saves, D discards, Esc keeps`; every
key is the question's until one of the three. `S` opens Save As; once the
save has completed the document is clean and the next close asks nothing.
`Esc` keeps the window and the edits. `D` sets a flag the wake spends: the
journal of this document is retired, then `os88_wm_close` (75.2), which
retires the negotiator and closes at the UI's next pass. No job runs then -
the key waits while one does - so there is no temporary file to remove, and
the instance's death frees every claim. It costs the primary about 200 bytes
of D25's share and no overlay byte.

Not asked: **New, Open and Close document** replace the document without a
question. What happens to the unsaved marks there, precisely: the store is
re-initialised and the marks are gone from memory, but the journal holds
them as of the last write - everything but the last half second of commands
and an open typing run, and none of the page operations - and it is kept
(20.17.4), so opening the same source again, in the same session or a later
one, offers `Recover N marks?`. They are lost only if a command is made in
another document first (one journal per machine), or if the journal was off.
A question at those three is the shell's (`pducmd.c`, `pdovlpage.c`) and
did not fit D25's primary share after the close's (verify-recov, still
open).

### 20.18 Preferences and the forms baseline (`prefs`, wave 3, PD-23, PD-26)

`pdupref.c` (resident: the settings, their load and their application),
`pdovlpref.c` (the dialog's body, an overlay tenant included from `pdovl.c`).

#### 20.18.1 The settings

One file, `SYSTEM/APPDATA/PAGEDESK.CFG` on the volume PageDesk was launched
from (`pdu_home.vol`, SPEC.md 19.9: found by walking, never assumed;
`os88_file_goto_q_mark` there and back, so the instance is left where it was).
Not the volume the instance stands on: a load or a save runs while it stands
in the open document's folder (`pdu_at_doc`), which may be another disk. Ten bytes:

| offset | meaning |
|---|---|
| 0-3 | `PDCF` |
| 4 | format version, 1 |
| 5 | **default view** of a newly opened document: 0 Fit width, 1 Fit page, 2 100% |
| 6 | **panels at the FULL layout**: bit 0 Pages panel, bit 1 Notes panel (0-3) |
| 7 | **panels at the MID layout**, same bits |
| 8 | **monochrome mark style** (17.4), passed as `pdxb_render.mono_mark`: 0 underline and dotted frame, 1 underline, 2 frame, 3 hatch |
| 9 | **Flate cache**: 0 Auto, 1 Off (D17's low-memory switch) |

COMPACT has no docked panels (17.1), so it has no setting. A file that is
missing, short, long, of another version, or holds a value out of range is
IGNORED AS A WHOLE and every default applies (view 0, FULL 3, MID 0, marks 0,
Flate 0): a bad file never half-applies and is never an error. The defaults
are what PageDesk did before the dialog existed.

**Read** once, by the first document open's first slice (before its claims,
which the Flate rule needs) or by the dialog's first paint, whichever comes
first: the reading is an overlay body, and a launch to the empty view does not
load the overlay for it, so the empty view shows the defaults (both panels at
FULL) until the first open. **Applied** at every document open (the view; the
panels, by the layout that follows; the Flate slot rule of 20.13.3, where Off
takes the same path as "too little RAM": no slots, `PDXI_FLATE` clear, Flate
streams refused with PDE_FLATE) and on every render (marks). A change in the dialog applies to the session at once where it
can: panels at the dialog's close, marks by repainting the page; the view and
the Flate slots wait for the next open, which the dialog says.

#### 20.18.2 The dialog

View > Preferences... opens a popover (P_PREFS, `pdu_pop_open`'s own mechanism:
placed in the canvas, one at a time, Esc closes it before the document sees
the key) of eight text rows: a title, the five settings, the line `View, Flate: next
open` and the key line (`Arrows  Enter  Esc`). The panel values are None,
Pages, Notes, Both. 28 columns, so it fits COMPACT's
640x200 canvas (92-104 px) as well as FULL.

| key | effect |
|---|---|
| Up / Down, Tab | select the previous / next setting |
| Right, Enter, Space | next value (wraps) |
| Left | previous value |
| Esc, a click outside | close; the changes are saved |
| a click on a setting row | select it and take its next value |

The dialog works with the keyboard alone. Its paints (a value changing, the
popover appearing) run at the wake like every popover paint (`pdu_popown`,
20.6.5), because the key chain has no room for them (t_pdstk).

#### 20.18.3 Saving, and read-only media

The save is OWED to the wake (`pdp_owe`, paid by `ovl_pf_save`), not done in the key chain: closing
the dialog with something changed sets the debt, and the wake writes the
file. It is a full replace of ten bytes. When it cannot - no `SYSTEM/APPDATA`
on the disk (a user's own volume), a write-protected or full disk, no disk -
the SESSION KEEPS THE CHOICES and the status line says why in the kernel's
words: `Not saved: <reason>. Session only.` Nothing else fails: a
run on a CD or a write-protected floppy launches, applies whatever it could
read and lets the dialog change the session.

#### 20.18.4 Forms (PD-26, the baseline)

PageDesk does not fill or change AcroForm fields (D24: filling is not selected
this round). When the open completes for a document whose
`pdu_xopen.flags & PDO_ACROFORM`, the status line says `Form fields are shown
as printed; filling is not supported.` (instead of "Opened X", and again when
the open's first render ends, whatever its length, since a long render's "Page
drawn." or a slow one's progress line would bury it; a signed file's read-only
line and a save's outcome keep precedence, and then the note is not said at
all), and Help > Document info gains a line `Forms:    as printed, not
fillable`. A form's pages still render, annotate and save: nothing is hidden,
and no field value is written or changed.

#### 20.18.5 Hooks and sizes

Marked `/* prefs */` lines: `pducmd.c` (the View > Preferences item, the
popover's rows, paint, key, click and its close), `pdumod.c` (the Flate slot
rule), `pdujob.c` (the view at open, `mono_mark`, the forms note, the wake's
`pdp_wake`), `pdovl.c` (the include and the info line), `pagedesk.c` (the
include). There is no launch load (D26). Sizes: see the wave-3 report.

### 20.19 Polish: the carried defects (`polish`, wave 3)

Five defects carried out of wave 2's verifications, fixed in the shell and in
PDREND. Each says what was wrong, what is true now and what was looked at.

#### 20.19.1 The page is painted once (`pdu_canvas_paint`)

`PDU_PREFILL` white-filled the page's strip and then PDREND composed the same
strip (ground, desk, ink) in a band and blitted it, so every repaint wrote the
page twice. PDREND paints the whole of its clip, so the shell now lays the
white only where no render covers the strip at once (`pdu_prefill()`): a page
that is not `PDM_SRC` (a blank is paper, no render is asked for), a popover or
the About card over the canvas (renders never run under one), a held-button
gesture (`pdu_gs == 2`), a job already running (the strip waits its turn), and
the strip a scroll exposes (`pdu_pfill`, set around `pdu_scroll_to`'s paint:
`OSAPI_GFX_SCROLL` leaves those rows undefined, SPEC.md 5.5, so without it a
held arrow key smeared the page until the render caught up). Everywhere else
the strip's ground is written once, by PDREND. The desk around the page is
the shell's, once - and PDREND's band, which reaches up to 7 pixels past its
clip to a byte column, now composes that desk in the shell's colour (8, not
7: a 3-pixel light-grey stripe stood beside the page at 4bpp).

What is NOT white any more, until PDREND reaches it, is the page part of a
repaint: the previous page, the previous zoom, or a dialog that has just
closed stays there for the render's interpretation phase (PDREND interprets
the whole clip before it blits its first band). That is the trade item 2
asked for; on an XT it is the render's own time, to be looked at in the final
MartyPC pass.

What is never left there is stale pixels a render will NOT draw:
`ovl_ground` lays white on the page part of a rect from a row down, and runs
when a render is cancelled (Esc) or fails (`ovl_render_rest`: `ABORT` with
CX = 1 says the first row it did not blit, 20.3.5, and the paper starts
there, so what was drawn stays), when `BEGIN` fails (the whole clip), and
when a popover or the About card opens over an owed render
(`pdu_pop_open`, `os88_about`: the owed rect, which renders again when it
closes).

#### 20.19.2 4bpp grey fills (10.5)

A grey fill (palette index 0, 8, 7 or 15, luminance 0 / 85 / 170 / 255 on the
VGA palette) between two of them is the 50% checkerboard of the pair that
brackets it: lum 21-63 is 0/8, 64-105 solid 8, 106-148 8/7, 149-190 solid 7,
191-233 7/15, else solid 15 (seven levels, nearest-of-seven; 10.5's four
thresholds 48/128/208 gave HELLO.PDF's 0.85 triangle, luminance 217, white).
Fills only: strokes, text and image rows are as before; 1bpp is unchanged (its
4x4 ordered dither). The checker is `emit_state`'s `grey_mix`: the state's
colour is the pair's first and `g_stl[10]` (the 1bpp pattern, unused at 4bpp)
carries the second + 1; `span_out` paints it through `fill4_mix` (parity from
absolute x and the PAGE's row - y less the page's top, so an odd scroll
leaves no seam where the moved pixels meet the rendered strip; the ends by `pdr_fill4`, the interior one byte fill). Text keeps
its own quantised colour (its state is `PK_TEXT`, never mixed). Looked at: HELLO.PDF
at 50% on VGA, the triangle is a 7/15 checker (shots/polish/hello50-zoom.png).

#### 20.19.3 The status line after a short render

A render of 8 slices or fewer that showed "Drawing page N - Esc cancels"
(a progress line is drawn once a job passes a second) left it on the status
line. `pdu_pshown` now means "the status line shows a progress line": set by
`pdu_progress` after it says it, cleared by every `pdu_say`/`pdu_say2`, and
NOT reset when a job starts - so a render that a popover or a scroll aborted
after its progress line, and whose restart is short, still ends with "Page
drawn.".

#### 20.19.4 XOR overlays against popovers and cancelled renders

Edit's selection outline is taken off (`pdu_ed_xoff_l()`) BEFORE a popover or
the About card opens, so a selection change while one is up no longer replays
the XOR over its pixels; `pdu_ed_xon_l` already refuses while one is up and
the close repaints the canvas, which lights it again at the render's end. A
render that is cancelled or fails left a clip partly fresh and partly old
pixels with nav's selection XORed into them. Since 20.19.1 lays paper below
the last band, every pixel of the clip is fresh afterwards, so
`ovl_render_rest` puts the selection back over the whole clip
(`pdu_sel_after`) and edit's outline with it: the selection is KEPT and the
XOR invariant holds. Looked at on VGA: a selection under the Find popover,
opened and closed with no residue (shots/verify-polish/09, 10).

#### 20.19.5 Esc during a held-button gesture

Two kinds of held gesture exist. Nav's (the Select tool's rubber band and the
scroll thumb) poll from `os88_ontimer` and return to the dispatcher, so a key
reaches `os88_onkey` meanwhile: Esc now ends it (`ovl_gcancel`: the band comes
off, nothing is selected, the release finds no gesture; a thumb drag's thumb
goes back to where the view is, since os88ui's rate may have moved it past
the view). Edit's annotation
tools (`ovl_loop` in `pdutool.c`) are modal loops that poll `os88_mouse()`
under the gfx lock; no key event is delivered inside one, and the only way to
see Esc there is `os88_key_down`, which arms the kernel key map for the whole
session and doubles E0 keys on QEMU (known, `os88_keymap_doubles_e0`). That
is not taken: an annotation gesture is cancelled by Undo after it ends, as
20.8 says. Making those gestures non-modal (the nav shape) would give them
Esc and is a design change for a later wave, not polish.

#### 20.19.6 Sizes

Primary 50,568 + 6,327 = 56,895 (was 56,535: +360, D24's polish share was
+300); overlay part 41,604 (was 41,257: +347, share +200 - `ovl_ground` and
`ovl_render_rest` live there, being a cancel's and a popover's, not a
keystroke's); PDREND 60,304 (was 59,878; D20's cap 61,440); `pdstk` unchanged
at 366 on `cc_onkey`.

### 20.20 Corrections found while documenting the behaviour (`docs`, final pass, PD-30)

Written from the agents' reports, the code at `feat/pagedesk` 8e00bef0d and
the PageDesk guide (`docs/PAGEDESK-GUIDE.md`). Each item says what an earlier
section states, what the code does, and which is right. Nothing here changes
behaviour: every segment is full, so a code defect found is listed for the
lead, not fixed. A section named below is not edited; this one overrides it.

**20.20.1 Text that describes a state of the build that has passed.**

| where | says | is |
|---|---|---|
| 20.6.1 | `PD_ENG_MOD`, `PD_REND_MOD` and `PD_WRT_MOD` "default to the stand-ins" until the lead flips them | the Makefile defaults to the real `PDENG.MOD`, `PDREND.MOD` and `PDWRT.MOD`; `PD_REND_MOD=$(PDSTUBDIR)/PDREND.MOD` selects the stand-in |
| 20.6.2 ("The document's folder") | Save As writes beside the document only; another folder is refused with `Save As writes beside the document in this build.` | D14 and 20.11.3 replaced it: Save As may name another folder (the writer reads from the source's and writes to the target's), and the string no longer exists. The one remaining refusal is naming the open file itself: `Save As writes a new file: choose another name.` |
| 20.11.5 | "Insert from another PDF has no row until it is built" | built (20.16): File row 9, `Insert from PDF...` |
| 20.2.5, 20.2.7 | `FlateDecode` is `PDE_FLATE` | only when PDENG holds no Flate claim (20.13): with a slot it is decoded. The sentence the user sees is `No Flate cache (low RAM, or set Off).` |
| 16.1, 16.2 | planning rows: primary 56 KiB, PDENG and PDREND 40 KiB, PDWRT 46 KiB, overlay 48, display list 22 + 7 KiB, glyph cache 12 KiB, a 176 KiB idle ceiling | the built figures (image + bss): primary 59,235 of 59,392 (D24), overlay 48,318 of 49,152, PDENG 47,074 of 47,104, PDREND 60,304 of 61,440 (D20), PDWRT 47,724 of 49,152 (D24); display list and workspace 29 KiB (D13), glyph cache 24 KiB (20.3); claim B 63 KiB on VGA and EGA, 60 KiB on CGA and Hercules; a Flate slot 43 KiB, one or two. The 176 KiB idle ceiling is exceeded by the module parts alone (D24); the PD-29 matrix is where the justified figure belongs |
| 1.3, streams row | `FlateDecode` "on the XT build" is refused | correct for a machine without room for a slot or with Preferences > Flate cache = Off; the gate is free RAM, not the CPU (D17) |
| 1.3, XObjects row | inline images "beyond 4 KiB" are refused | 20.12.5: an inline image is read by its exact length and drawn when its raw row fits 1,024 bytes and its stored form 6,144 bytes; the refused cases (CMYK, a filter, a colour-space name, a width over 4,096) are listed there |
| 17.5 | `Ctrl++` / `Ctrl+-` zoom | the zoom keys are the plain `+`, `=` and `-` (and `Ctrl+-`); `Ctrl++` is not a separate binding |
| 17.2 | Mode bar and file bar as drawn | COMPACT also lacks the Open button (File menu, Ctrl+O), recorded in 20.6 |
| 13.1 | 256 records and a swap file | 128 records, no swap file (20.8.1, edit-verify 1); the heaps share 10 KiB |

**20.20.2 Behaviour the contract promises that the build does not do.**

1. **10.6 and PD-07: greying operations that depend on skipped content.** The
   spec said that "cover" and extraction of an Incomplete page are greyed with
   a reason, and the build greys neither. RESOLVED by correcting 10.6 rather
   than the code. PageDesk has no cover tool, and Extract and Insert copy the
   page's objects byte for byte, so no operation depends on what the renderer
   skipped. PD-07's rule ("disable destructive operations that depend on the
   missing interpretation") is met with nothing to disable.
2. **14.7, PD-20: Save / Discard / Cancel at New, Open and Close document.**
   Only closing the window asks (20.17.8). The journal keeps the marks, so
   nothing is lost, but the contract's wording ("closing a document") is wider
   than the build.
3. **PD-11, links:** a name tree of a modern file is not read (20.10.2); only
   `/Dests` of PDF 1.1 and direct destinations. Links in such files do nothing
   and say so.
4. **PD-12:** source annotations are listed (8 of the first 32) and jumped to,
   not text-edited, and the Changes filter has none (20.9.1). A source
   `/Popup` is dropped when PageDesk rewrites that annotation (20.5).

**20.20.3 User-visible wording that is stale (a code change for the lead).**

- Help > *The XT PDF profile* (`pdovl.c`, `pdu_hprof`) reads `Refuses, saying
  why: Flate on the XT, xref streams, encryption, JPEG.` It predates 20.13.
  Flate is refused only when memory has no slot or the setting is Off. A line
  such as `Flate: needs a free 43KB slot.` would be true. The card strings are
  resident literals, so the edit costs the primary a few bytes it does not
  have to spare; the lead decides.
- The on-disk `README.TXT` described a first build with stand-in modules; it is
  rewritten for the shipped behaviour and checked by `tools/checkreadme.py`
  (27 columns, 4.5 KB on disk).

**20.20.4 Facts the documentation pinned that the contract did not state.**

- The Preferences defaults: view Fit width, Full panels Both, Mid panels None,
  marks Line+frame, Flate Auto (20.18.1 gives them in prose; the dialog row
  values are None / Pages / Notes / Both).
- Zoom steps by `+` and `-`: 25, 33, 50, 67, 75, 100, 125, 150, 200, 300, 400
  (9.1), the zoom pop-up: Fit width, Fit page, 50, 75, 100, 150, 200, 400.
- Esc has a fixed order and the first that applies wins: a held tool gesture
  (undone), a running job (cancelled), a popover, the page field, an open text
  box (typing ends), the selected mark, the keyboard focus, the text selection,
  and last the return to the Select tool (`os88_onkey`, `pducmd.c`).
- After a successful Save As the new file is the open document, on the same
  page; after a failed or cancelled save, or an Extract, the source is reopened
  (20.11.5).
- Which checks have **not** been run on anything but the host or QEMU, with
  what that does and does not show, is the guide's section 17; the contract's
  timing budgets (15.2) and memory ceilings (16.1) are PD-29's to state.

### 20.21 The face (`face`, after the merge)

The owner's word (2026-11-09): *"We just shipped PageDesk but the UI does not
look like the design ... give PageDesk the look of the attached screenshot."*
The screenshot is `docs/pagedesk/design/vga-workspace.png`; the face as first
shipped is `docs/pagedesk/design/before-vga-qemu.png`. This section is the
contract for the change and overrides 17.2, 17.6 and 20.6.4 where they differ.
It is the vga-face and retro-face skills' method applied to a C package:
fonts and icons generated on the host, every control's inside composed into
one 1bpp band and put down with one `OSAPI_GFX_BLIT1`, and the frame drawn
round it, so no pixel is written twice and an unchanged control is no call.

#### 20.21.1 What the design asks for, and what the OS answers

| the design shows | the build |
|---|---|
| bevelled buttons, a picture beside the caption | the bars of 20.21.4; the pictures are the kit's (20.21.2) |
| a narrow slab typewriter face in the bars and panels | Liberation Mono at 10 px (bold for captions, regular for text) and 12 px bold for panel titles, from the strikes PDRES already carries (20.4.2), packed by `tools/os88pdfont.py` into the kit at a fixed 6 or 7 pixel advance |
| the page field `3` and `of 12` beside it | two controls: the field and a label (`C_PGOF`) |
| `Fit width` with a drop-down mark | the Fit control, captioned with the current fit (`Fit width`, `Fit page`, or `Zoom`), opening the zoom list as before |
| Find and Print at the right end | Find at the right end. **No Print** (1.1) |
| tools Select, Highlight, Note, Text, Draw, Shape with a drop-down, Sign | Draw is the Ink tool on the bar; Shape is the More control, renamed, whose list is unchanged (Mark, Ink, Line, Rectangle, Ellipse, Sign). It shows as active while its list's tool is the current one |
| teal for the active mode, tool and tab | `OS88_CYAN` (the VGA palette's 00AAAAh: the package does not own the palette) |
| a dark desk, the page with a shadow | `OS88_DGRAY`, and a 2-pixel black shadow right and below the page |
| thumbnails with the number under each, the current one in a teal frame and a teal number box | 20.21.6 |
| Notes & edits: a title, a filter drop-down, a card per item with a picture, a bold title line and the text, Reply and Resolve | 20.21.7. **No Reply**: PageDesk writes no reply annotation (20.20.2 item 4) |
| the status line as three fields: changes, the page, the keys | 20.21.8 |
| `PageDesk - XT-GUIDE.PDF *` | the kernel's title bar, unchanged |

#### 20.21.2 The kit (`PDUK`, PDRES kind 9)

`tools/os88pdfont.py build` packs a ninth kind into `PDRES.BIN` **right after
the INDEX** (at `data_off`, before the strike data), so the read that loads
the INDEX loads the kit with it:

```
'PDUK', u16 version (1), u16 font count f (3), u16 icon count n
f x 8 bytes {u8 advance, u8 rows, u8 ascent, u8 glyph count g, u16 offset, u16 0}
n x 32 bytes: 16 rows of a 16x16 icon, two bytes a row, bit 7 of the first
               byte the leftmost pixel, 1 = ink
font data: g glyphs x rows bytes, bit 7 leftmost, 1 = ink, a fixed advance
```

Fonts 0, 1 and 2 are Liberation Mono bold 10 px (captions), regular 10 px
(text) and bold 12 px (titles). Glyph k is the code `32 + k` for k < 95; the
five after it are the drawing marks the bars need, at CP437's codes so the
kernel's 8x8 face draws the same mark when the kit is absent: `0x10` right
triangle, `0x11` left triangle, `0x1F` down triangle, `0xFA` middle dot, `0x9E`
a multiplication cross (the close box). A glyph whose ink is wider than the
advance is refused by `check`. Icons are hand drawn as text in
`apps/pagedesk/art/face.kit`; their order is the build's, and
`$(BUILD)/pdkit.h` names them (`PK_I_*`) for the C. `check` refuses a kit
whose icon count differs from the header the C was built with.

The read: `PDU_INDEX_MAX` becomes the INDEX's 8 KiB **plus** the kit's
(`PDU_KIT_MAX` = `PK_KIT_MAX`, 4.5 KiB, one constant in `tools/os88pdkit.py`), and D3's limit on the INDEX itself is unchanged.
The claim also holds the band (20.21.3) after what was read. It is taken at
LAUNCH, not at the first open, because the bars are drawn before any
document is: the claim count is unchanged (it is the INDEX claim), and a
claim refused at launch leaves the face on the kernel's 8x8 face with no
pictures - a complete face, the one 20.6.4 describes.

#### 20.21.3 The composer (`apps/pagedesk/pdkit.inc`, assembly, primary)

Seven cdecl routines (SPEC.md 73.3) over one band in the claim:

| routine | does |
|---|---|
| `pk_begin(w, h, x)` | a clear band `w` x `h` pixels (h <= 24) for a field whose left edge will be at screen `x`: the band starts on the byte column holding `x` and everything is composed `x & 7` pixels in, which is what SPEC.md 5.4.2.8's off-grid BLIT1 expects. Refuses (answers 0) a band wider than its 96-byte stride |
| `pk_text(x, y, s, f)` | font `f`'s glyphs ORed in at band position x, y; answers the x after the last |
| `pk_icon(x, y, k)` | icon `k`, ORed in, clipped to the band |
| `pk_hline(x1, x2, y)` | a run of ink (an underline, the drop-down's rule) |
| `pk_pen(y, ink, paper, grey)` | makes the band ready to go down at screen row y: on colour `OSAPI_GFX_BLIT1_PEN` (an inline cell); on a 1bpp display the pen is not read and a set bit is lit, so a greyed band is ANDed with the row's checkerboard and a band whose ink is not white is inverted (CDRack's rule; a greyed caption's `DGRAY` is black there). The C then puts it down with `os88_gfx_blit1_seg(pk_bseg, 0, pk_st, x, y, pk_w, pk_h)` - ONE drawing call, through the SDK's thunk as every other primary draw is (20.21.9) - and `pk_pen0()` puts the pen back to white on black, which the thumbnails' and the page's own BLIT1 bands are drawn with inside the same lock hold |
| `pk_adv(f)` | font `f`'s advance: 6, 6, 7 with the kit, 8 without |

Without the kit every font is the kernel's 8x8 table (`OSAPI_FONT_GLYPHS`,
read once) and `pk_icon` draws nothing.

#### 20.21.4 The bars

FULL and MID: the file bar is 24 rows and the mode bar 22 (were 16 and 16);
COMPACT keeps 12, the kernel face and no pictures. A bar is its `CLGRAY`
ground with a white top rule and a dark-grey bottom rule; groups are parted by
an etched pair of lines (dark grey, white). A control is one of four looks:

| look | used for | drawn as | calls |
|---|---|---|---|
| raised | commands, modes, the drop-downs, panel buttons | black frame; white top and left inside it, dark grey bottom and right; the inside one band | 6 |
| pressed / active | an active mode, a held button | black frame; dark grey top and left, the face colour bottom and right; the inside one band in `CYAN` with white ink (a held, inactive button: `CLGRAY`, black ink, shifted one pixel) | 6 |
| flat tool | the mode's tools | inactive: no frame, the ground's colour round one band; active: black frame round a `CYAN` band | 2 |
| field | the page and zoom fields | black frame round a white band | 2 |

On a 1bpp display grey is white: raised is the frame with a black bottom and
right inside it, and active is the band inverted. A greyed control is its
caption in `DGRAY` (on 1bpp the kernel's disabled pen on the frame and a band
ANDed with a checkerboard). The 20.6.4 cache stands: a control whose look,
caption and state did not change is no call. Metrics: a caption and picture
button is `4 + 4 + 16 + 3 + 6n + 4` wide, a picture alone 24, a caption alone
`6n + 14`; a band is the control's inside, so the frame and bevel are the
only pixels outside it.

#### 20.21.5 The canvas

The desk is `DGRAY` on colour and the 50% pattern on 1bpp, as before; the
page gets a black shadow two pixels wide on its right and bottom, offset two
pixels down and right, drawn with the desk (the four desk pieces become six
round the page and its shadow). The vertical bar is os88ui's, unchanged.

#### 20.21.6 The Pages panel

`CLGRAY` with the tab row (Pages, Outline) as raised/active tabs, the list
inside a sunken frame (dark grey top and left, white bottom and right). A
thumbnail cell is the page's white with a black frame and a one-pixel dark
grey shadow; the current page's cell has a two-pixel `CYAN` frame outside it
and its number in a `CYAN` box with white ink, every other number black on the
panel's grey. On 1bpp the current frame is black and the number inverted. The
foot is `N pages` and the put-away button (a raised button with the left
triangle).

#### 20.21.7 Notes & edits

The title `Notes & edits` in font 2, the close box a raised button with the
cross; the filter a raised drop-down (`This page`, `All pages`, `Changes`
with the down triangle). A card is white with a black frame: the kind's
picture at its left (a note, a highlight, the T of a text box, the tool's
picture for the rest), a bold title line (`Note`, the middle dot, `Page 3`)
and up to two lines of the item's text in font 1; a note's card carries
Resolve (or Reopen) as a raised button. The selected card's frame is two
pixels of `CYAN` (black on 1bpp). Text properties keeps its rows (family,
size, bold, italic, colour, align, Delete, Done) drawn in the same looks:
drop-downs raised, B and I and the three alignments as raised/active
buttons, Done raised in `CYAN` with white ink as the default.

#### 20.21.8 The status line

14 rows, `CLGRAY`, a white rule above. One band (font 0, black on grey) holds
three fields: the message on the left (and, where the first build said
`Page drawn.`, the changes: `No changes`, `N changes · Unsaved`, or the form
note of 20.18.4), `Page n of N` in the middle, and the keys on the right
(`Ctrl+S Save As | Esc Select`; `Ctrl+O Open` with no document). The first
build's adapter name and geometry are no longer on the line; Help > Document
info's `Layout:` row gives the layout and the content box it was chosen from.

#### 20.21.9 What it cost, and the two decisions it took

Measured on the build this section shipped with (image + bss, `.o88` header):

| | before (c678c46d8) | the face | |
|---|---|---|---|
| primary | 52,810 + 6,393 = 59,203 | 57,324 + 6,793 = **64,117** | +4,914, 907 under `APP_MAX_SIZE` 65,024 |
| overlay part | 48,318 | 48,490 | +172, of 49,152 |
| `PDRES.BIN` | 77,828 | 82,084 | +4,256: the kit, 4,230 bytes (3 fonts x 100 glyphs, 28 icons) |
| `PAGEDESK.O88` on disk | 218,295 | 225,711 | +7,416 |
| the INDEX claim | <= 8 KiB + a cluster, at the first open | <= 12.5 KiB + a cluster + the 2,304-byte band, **at launch** | |

**Decision 1: the primary passes D24's 59,392.** D24's figure was a planning
cap that left room for the polish waves, and the primary was 189 bytes under it
when the face began; there is no way to compose a control in one band without
the composer (`pdkit.inc`, 899 bytes of code), the band painter
(`pdu_ctl_paint` 1,692 bytes and `pdu_bandcap` 542, both C) and the panels'
new looks.
The hard limit is the loader's 65,024 for one claim (SPEC.md 20.1) and the
face leaves 907 bytes of it. A later change that needs primary bytes takes
them from here first: `pdu_ctl_paint` is the largest C function the face
added and the first candidate for assembly.

**Decision 2: the INDEX claim is taken at launch.** The bars are drawn before
any document is open, so the kit has to be in memory before the first paint.
The claim count does not change (it is the INDEX claim, D3), and a claim the
heap refuses refuses the launch with `No memory for PageDesk`, before a window
exists; a read that fails keeps the claim for the band and draws the kernel's
8x8 face without pictures, which is a complete face.

**The UI stack.** The Notes list's card button is the deepest drawing chain
in the primary (`cc_onkey` through `ovl_nt_row` and `ovl_nt_box` to the
composer). `ovl_nt_row`, `ovl_nt_box`, `pdu_ctl_paint` and `pdu_bandcap` keep
their locals static and `pdu_bandcap` takes its thirteen arguments as the
`pdu_bc_*` statics, so the chain is no deeper than the first build's: the
pdstk gate's worst UI chain is 364 against 366 before (D22's 384). The band
is put down through the C SDK's `os88_gfx_blit1_seg` rather than a direct
`OSAPI_GFX_BLIT1` in `pdkit.inc`: that is how every other drawing call in the
primary is made. **The gate does not charge those calls**: `tools/os88pdstk.py`
reads the primary's source without expanding `CC_IMAGE_END`, the macro that
includes `apps/cc/os88thunk.asm`, so a thunk's kernel call is "no body found"
and costs 0, where a direct one costs KAPI's 156. The figures above are
therefore frames only, for the first build's draws and the face's alike; a
gate that charged them would need a KAPI measured for a drawing call (156 is
`READ_SEQ`'s, with an `int 13h` and the IRQ0 progress widget under it) and is
left for its own change.


### 20.22 Any PDF on the machine (`anypdf`, the any-PDF round)

The owner's goal (2026-11-09): *"Make PageDesk able to read any PDF inside the
operating system without the python tool."* Until this round, everything
outside 1.3's profile was refused, drawn as **Incomplete rendering**, or left
to `tools/os88pdprep.py` (20.14) on a modern host. The story list is
`docs/plans/PAGEDESK-ANY-PLAN.md` (AP-01 to AP-20). This section is the
contract those stories build against. Each agent writes detail only under its
own 20.22.N. 1.3's profile table is rewritten from 20.22's results in the
round's final pass, not before.

**Owner decisions** (the plan's section 2 records the alternatives):

| # | decision |
|---|---|
| O1 | Everything: xref and object streams, broken-xref repair, every filter, encryption, JPEG/CCITT/JBIG2, JPX at a reduced resolution level, every colour space, shading and patterns, soft masks and transparency, embedded TrueType/CFF/Type1/Type3/CID fonts drawn from their outlines |
| O2 | New code goes in lazy modules sharing ONE slot, swapped as pages need them, plus a cache of the module images in XMS or EMS when the machine has either |
| O3 | Encryption: the empty user password silently, otherwise a password prompt; RC4-40/128, AES-128, AES-256 (R6); a save appends an incremental update encrypted with the file's own key |
| O4 | Transparency is blended TRUE, through an 8-bit grey band, then quantised or dithered to the adapter |
| O5 | The permission bits `/P` are ignored: every action is allowed once the file is open; a save keeps the source's `/P` and `/Encrypt` |
| O6 | AES-256 (R6) on an 8088 asks first: *"This file's encryption takes about N minutes on this machine. Open anyway?"*; other CPUs derive with progress and Cancel |
| O7 | The 360KB set becomes TWO floppies; `PAGEDESK.O88` carries every module on every geometry |

**Lead decisions** (binding on every agent):

* **L1 - code goes where it RUNS.** Anything per byte or per object is
  resident: PDENG grows to the Makefile's 57,344 bytes (image + bss) and
  PDREND to the part limit 65,024. Anything at open only is PDXK. Anything at
  interpretation is PDXG or PDXF, and its RESULT is kept in a core cache
  (`PDMEM_XC`, the synthetic strikes in the glyph cache), so a later pass
  needs no module. The only module that runs while bands are drawn is an image
  decoder (PDXI, PDXJ). 8-bit compositing, shading spans and the quantiser
  are core.
* **L2 - the module ladder.** A verb that needs an absent occupant answers
  `PDE_NEEDMOD` (CF = 0, DX = `PDX_*`) at a point it can resume from. Inside
  PDREND, an operator whose work needs an absent module and whose result is
  not cached is recorded as pending and skipped; the pass runs on. At the end
  of the interpretation pass, before any band is drawn, a pending module is
  answered with `PDE_NEEDMOD`. The primary loads it, says so with
  `PDV_REND_SLOT`, and steps again; the pass re-runs from the start of the
  band group (it already rescans, D4) with the module resident and the
  earlier modules' results cached. While bands are drawn, the first band
  whose image record needs a codec that is not resident answers `PDE_NEEDMOD`
  before that band is put on the screen. A page view loads each module at
  most once while interpreting. A module that cannot be loaded (`PDE_NOSLOT`,
  a read error) sets `PDF_INC_MODULE`, and the page is drawn without that
  feature. `pdxb_rstatus.mods` and `.swaps` report what a page cost.
* **L3 - the slot** is one region holding the occupant's image + bss and its
  workspace (`slot_seg/slot_kb`, given at `PDV_X_INIT`), sized per occupant
  from 20.22.2's table and claimed by the primary when first needed. Modules
  never claim (3.4). The cache of unpacked occupant images in XMS or EMS is
  the primary's. Preferences has an Auto/Off row for it, and the disk is the
  fallback.
* **L4 - memory.** At most 8 claims at every peak on kern_big (`MEM_OWNER_MAX`).
  A file in 1.3's old profile keeps working on a 640 KiB XT, with at least
  64 KiB unclaimed after PD-29's session on every adapter. Everything new is
  claimed only when a document or page needs it, and on refusal degrades to a
  sentence, never to a crash.
* **L5 - honesty.** A feature this round supports never sets an Incomplete bit
  for a page that MuPDF draws. A reduced-resolution decode is NOT Incomplete,
  and neither is a dither. Everything that is still skipped keeps its
  `PDF_INC_*` bit and its first name.
* **L6 - budgets** (image + bss, against the round's start). The plan's
  section 4 has the table per agent. A budget is a cap: an agent that needs
  more writes `BLOCKED` with the figure; it does not take a neighbour's.
* **L7 - checks.** Every component compiles on the host and is held against
  an independent reader (qpdf, MuPDF, Poppler, libjpeg's `djpeg`, OpenJPEG's
  `opj_decompress`). Fixtures are DETERMINISTIC and their generators live in
  `tools/os88pdfx_<key>.py`. A fixture whose generator needs a tool this
  host lacks is committed with its provenance under `tests/pdfany/<key>/`.
  Every new chain passes `pdstk` (deep 1,776, UI 384) and `pdkgate`.

#### 20.22.1 The ABI this round adds (the lead's, `pdabi.h`)

* Codes: `PDE_PASSWORD` 23, `PDE_NOSLOT` 24, and two statuses that are NOT
  failures, `PDE_NEEDMOD` 66 and `PDE_NEEDMEM` 67 (CX = `PDMEM_*`, DX = KiB:
  the primary claims the region, puts it in `pdxb_init`, re-INITs and calls
  the same verb again). `PDE_XREFSTM` now means a damaged cross-reference
  stream. `PDE_ENCRYPT` now means a handler PageDesk does not implement.
* Incomplete bits `PDF_INC_MODULE`, `PDF_INC_APPROX`.
* `PDM_ABI` 2. Occupant magics `'PK' 'PG' 'PF' 'PI' 'PJ'`, ids `PDX_KEY`
  1 to `PDX_JPX` 5, and regions `PDMEM_XIDX`, `PDMEM_OSW`, `PDMEM_XC`,
  `PDMEM_X8`. `PDMEM_SLOT` 0 is not a region: an occupant verb answers
  `PDE_NEEDMEM` with CX = 0 and DX = the KiB its slot WORKSPACE should have
  for this one stream. The primary regrows claim S to at most the occupant's
  `want`, only while PD-29's 32 KiB stays free beside it, re-INITs the
  occupant with the new `slot_kb` and calls the verb again. A refusal
  re-INITs unchanged, and the occupant degrades (a coarser scale, or the
  image's grey box with `PDF_INC_APPROX`). An occupant's (`want`, `min`) row
  (20.22.2.3) is what it needs to LOAD; `min` decodes its common cases.
* Verbs: `PDV_ENG_CRYPTINFO` 0x13, `PDV_ENG_SETKEY` 0x14, `PDV_ENG_CRYPT`
  0x15, `PDV_ENG_SLOT` 0x16, `PDV_REND_SLOT` 0x48, and the occupants' range
  0xC0-0xFF: `PDV_X_INIT` 0xC0, `PDV_KEY_AUTH` 0xC1, `PDV_KEY_COST` 0xC2,
  `PDV_IMG_OPEN/ROW/CLOSE` 0xE8-0xEA. Reserved ranges: eng 0x17-0x1F, crypt
  0x20-0x27 and 0xC3-0xC7, rcore 0x49-0x4F, wrt 0x85-0x8F, gfx and blend
  0xC8-0xD7, fonts 0xD8-0xE7, the image agents 0xEB-0xEF.
* Structs grown at their END (an old caller's zeroed field keeps the old
  behaviour): `pdxb_init` (slot, xidx, osw, xc, x8 regions, cpu, PDREND's
  door), `pdxb_strm` (`accept`/`codec`), `pdxb_rawcopy` (`mode`:
  `PDRC_PLAIN`), `pdxb_trailer` (`id0_hi`, `encrypt_num/gen`, `tflags`),
  `pdxb_rstatus` (`mods`, `swaps`). Open flags `PDO_ENCRYPTED`,
  `PDO_XREFSTM`, `PDO_OBJSTM`, `PDO_REPAIRED`. New structs `pdxb_crypt`,
  `pdxb_cryptop` and `pdxb_img`.
* An agent's own structs go ONLY in its marked block at the end of
  `pdabi.h`, and a change to a lead struct is a `BLOCKED` line naming it.

#### 20.22.2 The slot, the ladder and the claims (`plat`)

The slot, the claims that hold it, the ladder that fills it, the cache behind
it and the disks that carry its occupants. The code is the primary's:
`apps/pagedesk/pduslot.inc` (the loader, the cache, the regions, the ladder,
in assembly: 20.22.2.9), `apps/pagedesk/pduslot.c` (what an owner edits: the
occupant table and each one's workspace, and ui's stand-ins), the job call
sites in `pdujob.c`, `pdufind.c`, `pdusel.c` and `pduside.c`, and
`pdumod.c` (PDWRT through the slot).

**20.22.2.1 Where the slot lives: a claim of its own, which PDWRT shares.**
The scout's three places, priced in claims (`MEM_OWNER_MAX` = 8 on kern_big,
kern_small and kern_emu; 12 on kern_mega) and in bytes held:

| place | claims at a save's peak | held while a document is open |
|---|---|---|
| B's tail (the Flate slots' shape) | 8, unchanged | +64 KiB for the document's life, whether or not a page ever needs a module. L4 refuses it: a 1.3-profile file would pay for code it never runs |
| its own claim, taken on the first `PDE_NEEDMOD` | **10**: carve, XB, INDEX, A, B, X (the regions), S, overlay, PDWRT, PDWRT's buffers; freeing S first still leaves 9 | only what a page used |
| **its own claim, and PDWRT is one of its occupants** | **8**: PDWRT's image and its buffers become ONE claim | only what a page used; a save swaps the decoder out |

So the slot is **claim S**, taken at the first module a document needs, and
PDWRT is a sixth occupant (internal id `PDU_OCC_WRT` 6, never in `pdxb_*`):
its row becomes `OP_LAZY | OP_SELF | OP_COMP` and its buffers
(`wbuf_seg`, 18 KiB a save, an Extract or Insert up to 40) are its workspace
in S. `op_fetch` no longer fetches PDWRT, so `w_partfetch`
(`apps/weave/wfetch.inc`) leaves the primary. Jobs are one at a time (20.6.3),
so a save never overlaps a render: the decoder in S is simply swapped out.

The four PDENG and PDREND regions (`PDMEM_XIDX`, `PDMEM_OSW`, `PDMEM_XC`,
`PDMEM_X8`) are **claim X**, one claim cut by `os88_seg_sub` like A and B,
taken at the first `PDE_NEEDMEM` and grown by `OSAPI_MEM_REGROW` (20.22.2.5).

**20.22.2.2 The claims at every peak** (kern_big; the old table is 20.6.2's):

| peak | claims | count |
|---|---|---|
| launch | carve (primary + PDENG + PDREND), exchange block, INDEX + kit + band | 3 |
| a document open | + A, B | 5 |
| ...that asked for a region | + X | 6 |
| ...whose page needed a module | + S (one occupant) | 7 |
| open of an encrypted file at its password prompt | carve, XB, INDEX, A, B, X (XIDX/OSW), S (PDXK), overlay (the prompt) | **8** |
| render with a panel, popover or inspector up | carve, XB, INDEX, A, B, X, S (a decoder), overlay | **8** |
| save, extract or insert with its dialog up | carve, XB, INDEX, A, B, X, S (PDWRT + its buffers), overlay | **8** |
| File > New, no document | carve, XB, INDEX, S (PDWRT + buffers), overlay | 5 |

The transient claims stay what they were and each already degrades when
refused as a ninth: Copy's (`pdusel.c`, falls back to the THUMB row), the
recovery journal's 8 KiB write (`pdovlrecov.c`, retried later) and its 16 KiB
read at launch (before any document set). S and X are given back at document
close; S also after every save, extract or insert (as PDWRT was).

**20.22.2.3 The occupants and their slot sizes.** An occupant is a module
(3.1's 8-byte header, its door at `PDM_ENTRY`, one DS = CS segment), its part
`OP_LAZY | OP_SELF | OP_COMP` and image + bss at most 65,024. The slot an
occupant gets is its image + bss, rounded up to a paragraph, followed by its
WORKSPACE (`slot_seg`/`slot_kb` at `PDV_X_INIT`; for PDWRT `wbuf_seg`/
`wbuf_kb` at `PDV_WRT_INIT`), at most 64 KiB. **Each owner declares its
workspace by editing ITS row below and the matching row of `pdu_occws[]` in
`pduslot.c`** (one number each; the rows are separated so the edits merge):

| id | occupant | part row | magic | cap (image + bss) | workspace KiB | owner |
|---|---|---|---|---|---|---|
| `PDX_KEY` 1 | PDXK | 5 | `'PK'` | 32,768 | 0 (placeholder) | crypt |
| `PDX_GFX` 2 | PDXG | 6 | `'PG'` | 61,440 | 0 (placeholder) | gfx |
| `PDX_FNT` 3 | PDXF | 7 | `'PF'` | 61,440 | 0 (placeholder) | fonts |
| `PDX_IMG` 4 | PDXI | 8 | `'PI'` | 65,024 | 0 (placeholder) | dct, bilevel |
| `PDX_JPX` 5 | PDXJ | 9 | `'PJ'` | 65,024 | 0 (placeholder) | jpx |
| (6, internal) | PDWRT | 2 | `'PW'` | 53,248 | `pdu_wkb`: 18, or up to 40 for an Extract/Insert | wrt |

The overlay row moves from 5 to 10 and stays last (`cc_ovidx`, crt0's
`CC_OVL_ROW` rule). Until an occupant lands its row carries a STAND-IN
(`apps/pagedesk/stubs/pdstub.asm -DSTUB_XK|XG|XF|XI|XJ`), named by the
Makefile's `PD_XK_MOD` ... `PD_XJ_MOD`, which default to it: a header, a door
that answers `PDV_X_INIT` and refuses every other verb with CF = 1,
`PDE_NOTFOUND`, and 1 KiB of bss so the row packs.

**The stack gate reads an occupant by a naming rule** (`tools/os88pdstk.py`'s
`OCCUPANTS`): its top source is `apps/pagedesk/pdx?.asm`, its door
`pdx?_entry` and its dispatcher `_pdx?_verb` (`pdxk`, `pdxg`, `pdxf`, `pdxi`,
`pdxj`); PDREND's far call into the slot names `slot` in its operand
(`call far [_pdr_slot_off]`); every far call an occupant makes is taken to be
PDENG's door. The primary's `pdu_mcall` reaches every occupant's door. An
occupant whose source is not in the tree (the stand-in) is left out, and
`t_pdkgate` reads every built `PDX?.MOD`, real and stand-in.

The slot claim S is sized for the occupant it is about to hold:
`max(roundup16(image + bss) + workspace, R + roundup_cluster(slack +
packed))`, the second term being the room the in-place expansion needs
(20.22.2.4). It is resized with `OSAPI_MEM_REGROW` (a shrink stays put; a grow
takes only the difference when the paragraphs above are free), and refused
with `PDE_NOSLOT` when the heap cannot give it.

**20.22.2.4 The loader** (`pdu_slot_load(occ)`, the UI task, never in a
callback's chain: only from the wake, 20.22.2.6):

1. Already resident: nothing.
2. PDENG and PDREND are told the slot is EMPTY (`PDV_ENG_SLOT`,
   `PDV_REND_SLOT`, BX = 0) and the slot vector (`pdu_vec[2]`, which was
   PDWRT's) is unbound, before a byte of S changes.
3. S is claimed or regrown to the size above.
4. From the cache when it holds this occupant (20.22.2.7): the unpacked image
   + bss copied into S's base. Otherwise from the disk: stand in the launch
   folder (`pdu_home`, 3.1's bracket), read the row's PACKED bytes with
   `OSAPI_FILE_READ_AT` at R = roundup512(len - packed) paragraphs up S, the
   file offset aligned down to a cluster and the capacity up to one (SPEC.md
   18.4.4), in reads of at most 32 KiB (a multiple of every cluster), each
   into its own `os88_seg_sub` window; then `OSAPI_DECOMP` from that window
   onto S:0 (the stream is above its output, so the writer never overtakes
   the reader: SPEC.md 20.13.7). A row that is stored (a `PKGZ=` A/B build)
   is read at S:0 and moved down by its slack. Then back to the document's
   folder.
5. The header is checked as 3.1 checks the eager modules: the occupant's
   magic, `PDM_ABI`, image >= 9 and image + bss == the row's `len`. A failure
   is `PDE_LIMIT`, and the slot is left empty.
6. The cache takes a copy NOW, before the first verb, so it is pristine.
7. The door is bound: `pdu_vec[2]` = `PDM_ENTRY` : `OS88_CODESEL(S)`. **The
   image starts at S's own base**, so on kern_mega its door is the claim's
   code twin, exactly as PDWRT's `op_fetch` claim was: no `OSAPI_MEM_SUBSEG`
   code twin and no sub-selector is ever far-called. The workspace above it
   is a data window, `os88_seg_sub(S, image + bss in paragraphs, workspace)`.
8. `PDV_X_INIT` (PDWRT: `PDV_WRT_INIT`) with `pdxb_init` complete: the
   document set, the regions, `slot_seg`/`slot_kb` = the workspace,
   `eng_off`/`eng_seg` and `rend_off`/`rend_seg` (PDENG's and PDREND's doors,
   code selectors), `cpu` = `OSAPI_CPU_INFO`'s class.
9. PDENG and PDREND are told: `PDV_ENG_SLOT` / `PDV_REND_SLOT` with BX = the
   `PDX_*` id (0 for PDWRT, which neither calls) and CX:DX = `PDM_ENTRY` : the
   code selector.

**A load that fails** (no RAM: `PDE_NOSLOT`; a read error; a bad header)
leaves S empty and tells PDREND **BX = 0x80 | id**, CX = DX = 0: that module
is unavailable for the rest of this page view, PDREND sets
`PDF_INC_MODULE` and names the module in `first`, and the pass runs on
without it. PDENG's only module is PDXK, and its failure fails the open with
the load's sentence.

**Who calls an occupant.** The primary (`PDV_X_INIT`, `PDV_KEY_*`), PDREND
(through the door `PDV_REND_SLOT` gave it), and nobody else. An occupant may
call PDENG (`eng_off`/`eng_seg`, to read a stream), never PDREND (it is
always under a PDREND verb or the primary's, and PDREND is not re-entrant)
and never the primary. PDENG does not call occupants: the vector
`PDV_ENG_SLOT` gives it is for a later round, and PDENG -> PDXK -> PDENG would
be a cycle `pdstk` refuses. The deep chain PDREND (its ~300 to the call) +
occupant + PDENG (424) sits inside 1,776, which leaves an occupant about
1,000 bytes of frames of its own (`tools/os88pdstk.py` holds the sum).

**20.22.2.5 The regions (claim X, `PDE_NEEDMEM`).** CX = the `PDMEM_*`
region, DX = the KiB wanted (1..64). The primary:

* grants a region on its first ask, appended to X in the order asked: X is
  claimed at the first, and regrown (`OSAPI_MEM_REGROW`) at each later one, only when
  the largest free run still leaves PD-29's 32 KiB (2,048 paragraphs) beside
  it;
* grows a region already held only when it is X's LAST region (in place);
  any other second ask is refused;
* re-hands EVERY region at every INIT (`pdu_minit` copies one `pdxb_init`),
  recomputing each segment after a regrow, which may have MOVED X (its bytes
  come with it), and re-INITs PDENG, then PDREND. The occupant is not
  re-INITed: it holds no region;
* refuses by re-INITing anyway: a region never held has its field 0, and a
  held region asked to grow keeps its segment and KiB unchanged (a refusal is
  not an error).

THE MODULES' SIDE OF IT, binding on eng and rcore: **an INIT while a document
is open only re-hands segments**: document state, and every byte of a region
already held, are kept. A module that asked for a region and finds its field
0 (or, asking to grow one, its KiB unchanged) after the re-INIT was REFUSED: it takes its degraded path and does not ask
for that region again until its next `PDV_ENG_OPEN` (PDENG) or
`PDV_REND_BEGIN` / first `TEXTMAP` / first `THUMB` (PDREND). Ask once, for the
most the document will use: PDENG asks for `PDMEM_OSW` at OPEN once it knows
the file has object streams, not at the first object it meets in one.

**20.22.2.6 The ladder in the primary.** Only these verbs answer
`PDE_NEEDMOD` or `PDE_NEEDMEM` (CF = 0, so neither is ever mistaken for a
failure by a caller that tests `pdu_mcf` first):

| verb | may answer | the primary, in that slice | then |
|---|---|---|---|
| `PDV_ENG_OPEN` (J_OPEN) | `NEEDMEM` XIDX, OSW; `NEEDMOD` `PDX_KEY` | grants (20.22.2.5); or loads PDXK and enters the open's AUTH sub-state: ui's `pdu_auth_step()` once a slice until it stops answering `PDE_BUSY` | OPEN again at the next slice; an auth failure fails the open with its `PDE_*` (`PDE_PASSWORD`, `PDE_CANCEL`) |
| `PDV_REND_STEP` (J_RENDER) | `NEEDMEM` XC, X8; `NEEDMOD` GFX, FNT, IMG, JPX | grants; or loads | the same verb at the next slice |
| `PDV_REND_TEXTMAP` (J_TEXT, J_FIND), `PDV_REND_THUMB` (J_THUMB) | as STEP | as STEP | as STEP |

Every call site of those verbs handles them the same way through ONE function,
`unsigned pdu_ladck(int mod, unsigned r)` (pduslot.inc), called with the
verb's answer: it reads the answer's CX and DX (banked by `pdu_mcall` in
`pdu_mcx`/`pdu_mdx`), does the work, and answers `0xFFFF` (call the verb
again next slice), the answer itself when it was no ladder status, or a
`PDE_*` with `pdu_mcf` set that the job then fails with. It runs from the job
slices alone, so a disk read is never under a click's chain (D22's 384).

**THE EXCHANGE BLOCK AFTER A LADDER ANSWER IS THE PRIMARY'S.** The grant or
the load re-INITs with `pdxb_init`, which is written over the XB from offset
0. So a verb that answers `PDE_NEEDMOD` or `PDE_NEEDMEM` keeps everything it
needs of its own struct in its own state, and finds the XB again as the
primary leaves it at the next call: OPEN, TEXTMAP and THUMB have their struct
copied out before every slice (pdujob.c, pdufind.c, pdusel.c, pduside.c's
rule), and STEP reads none. A loaded PDXK leaves the slot as soon as the open
completes; a parked Replace (PDWRT resident, 20.11) refuses every other load
with `PDE_NOSLOT` until `pdu_wrt_out` lets it go.

The caps: a page view (counted from the start of every job, `pdu_lad_begin`
in `pdu_job_start`: a render, an open, a text, find or thumbnail job) loads at
most **`PDU_SWAPMAX` = 6** modules; past
that a `NEEDMOD` is answered with the 0x80 refusal. rcore's side is L2's: each
module once while interpreting, results cached, so 6 is room for a page whose
bands alternate two image codecs a couple of times. At most **4** grants per
verb sequence; a fifth `NEEDMEM` is refused. A job that sees a ladder status
past either cap more than 16 times fails with `PDE_LIMIT` rather than loop.

**ui's two functions** (`pduauth.c`; ui `#define`s `PDU_AUTH_UI` at the top
of `pagedesk.c`'s slot prototype block, so the stand-ins in `pduslot.c` drop
out and the prototypes there bind to ui's): `void pdu_auth_reset(void)` at every open's start, and
`unsigned pdu_auth_step(void)` = `PDE_BUSY` (call again next slice), 0 (the
key is set with `PDV_ENG_SETKEY`; resume OPEN) or a `PDE_*`. PDXK is resident
when it is first called. The stand-in answers `PDE_PASSWORD`, so until ui
lands an encrypted file fails its open with that sentence.

**20.22.2.7 The cache of unpacked images (O2).** The primary's alone; the
modules never see it. It is ON when ui's preference says Auto
(`int pdp_xcache(void)`: 1 Auto, 0 Off; ui `#define`s `PDP_XCACHE_UI` in the
same block so the stand-in, which answers 1, drops out) and the machine has a store:

* **XMS** first: `OSAPI_XMEM_CAPS` answers at least the total + 64 KiB, and ONE
  `OSAPI_XMEM_ALLOC` block (the table is 8 entries machine-wide, SPEC.md
  41.5) holds every occupant at a fixed offset: the rows' `len`s, each
  rounded to 1 KiB, in id order with PDWRT last. Copies are
  `OSAPI_XMEM_COPY` of at most 32 KiB, even lengths. Every kernel answers the
  question: 0 KB on an 8088 and on kern_small.
* **EMS** otherwise, on a real-mode kernel (`CPU_F_PM` clear: kern_mega's
  pool is XMS anyway) with EMS.DRV attached: ONE `EMSV_ALLOC` handle of the
  same layout in 16 KiB pages; a copy takes quarter 0 (`EMSV_FRAME` AL = 1),
  `EMSV_MAP`s each page and moves 16 KiB through the frame, then gives the
  quarter back (`EMSV_UNFRAME`), so the Video Player's four-quarter hold is
  never blocked for longer than one copy. `EMSE_BUSY` is a miss, not an
  error.
* Taken at the instance's first module load (a document that never needs a
  module never takes it), given back at quit (the kernel also frees both at
  the instance's death); one copy OUT per occupant, right after its disk load
  and header check; a copy IN on every later load. A copy that fails drops the
  cache for the session (the disk is the fallback, always). Off frees it at
  the next load.
* Its price: about 570 bytes of primary (`sl_copen`, `sl_cio`, `pdu_cx`,
  `pdu_cache_drop` in `pduslot.inc`), and no claim and no store on a machine
  without one. Its worth: VIDEO-XMS-PLAN's 0.345 ms a KB on a real 286 makes a
  64 KiB swap about 23 ms against 1.2-3 s off a floppy (scout-platform 1).

**20.22.2.8 The disks (O7).** Every geometry carries the one `PAGEDESK.O88`
with all eleven rows. The 360KB set becomes two floppies:
`pagedesk360.img` (the program, `README.TXT`, `PDFONTS.TXT`, and the two
samples the empty view and PD-29's XT proof open, `HELLO.PDF` and
`XTGUIDE.PDF`) and **`pagedesk360b.img`** (every sample, and `README.TXT`).
THE ARITHMETIC THAT BINDS: a 360KB disk is 354 clusters of 1,024 bytes, and
one file cannot span two floppies, so `PAGEDESK.O88` itself must stay under
about 350,000 bytes. It is 225,778 at the round's start; the occupants add
their PACKED bytes (stand-ins about 100 each). Should the five real
occupants pass ~120 KB packed, the 360KB answer is to ship PDRES (82,084
bytes, stored) beside the program as `PDRES.BIN`: the INDEX loader and
PDREND both reach it through `pdxb_fontbank.pkgname` and `part_off`, so the
move is primary-side only. That is the lead's call when the figures exist.
**The 720KB disk is the next to bind**: it carries every sample with the
program and has 100 of 713 clusters free with the stand-ins (613 in use), so
occupants past about 100 KB packed overflow it too, and its answer is the
same pair (the samples to a second disk) or the same `PDRES.BIN`. The 1.44MB
and 1.2MB disks have 1,631 and 1,155 clusters free.

**20.22.2.9 Room in the primary.** The `.o88` header's image + bss (the
cap is 65,024):

| step | image + bss | change |
|---|---|---|
| the round's start | 64,147 | |
| `pdu_ctl_paint` (C 1,692) and `pdu_bandcap` (542) in assembly (`pdui.inc`); `pdu_held` deleted (dead) | 63,313 | **-834** (the freeing owed: 800) |
| the slot, the cache, the regions and the ladder (`pduslot.inc`, about 2,050 bytes; in C it measured 2,800), less PDWRT's old `w_partfetch` and `pdu_wrt_in_now`; and, to make the room, `pdu_doc_claim` (C 982 -> 509), `pdu_doc_free` (184 -> about 70), `pdu_minit` (133 -> about 60) and PDRES's directory walk (`pdu_bank_dir`, 166 against the C's ~440) in assembly | 64,185 | **+872** (plat's allowance: 900) |

839 bytes stay under the cap, ui's +250 among them.

**A developer's look** (`PDU_LADTEST`, `#define`d in `pagedesk.c` AND
`%define`d in `pagedesk.asm`; nothing shipped compiles it): every open loads
every occupant 1..6 into the slot and out again, then toasts `dN cN xK rE
fF` - loads off the disk, copies back from the cache, the cache's kind (1
XMS, 2 EMS, 65535 none), the last answer and the largest free KiB. On QEMU
(VGA, XMEM.DRV attached) HELLO.PDF then XTGUIDE.PDF in one instance read `d6
c0 x1 r0 f118` and `d6 c6 x1 r0 f118`: six disk loads, then six copies back
from XMS, and the same largest free run across a close and an open.

#### 20.22.3 Structure: cross-reference streams, object streams, repair, LZW, ASCII85 (`eng`)

AP-01, AP-02 and AP-03, and the engine seams the other agents build on. All
of it is PDENG (L1: it runs per object or per byte). **A file whose every
revision is a classic table, with no `/XRefStm`, no object stream and no
damage, takes exactly the code path of 20.2.3** (the subsection records and
their checkpoints, the resolved-offset cache, no new claim): the new
mechanisms below are entered only when a revision needs them. A classic file
never pays the index's 4 bytes an object, however sparse it is.

**20.22.3.1 The flat index (`PDMEM_XIDX`).** A chain with a cross-reference
STREAM in it (any revision), a hybrid section whose `/XRefStm` is read, or a
repaired file, is resolved through a flat index instead of the subsection
records: 4 bytes an object number, entry `n` at `xidx_seg + n / 4 :
(n % 4) * 4` (so it may span several segments), little-endian:

| entry | meaning |
|---|---|
| `0x00000000` | absent: no revision mentions it (a null) |
| `0xFFFFFFFF` | free (a null) |
| bit 31 clear | in use at that file offset (an offset is under 2 GiB, 20.2.2) |
| bit 31 set | in object stream `hi & 0x7FFF` (at most 32,766), member `lo` (at most 65,534) |

It holds `max(/Size)` entries over the chain (a repair: the highest object
number met, plus one), at most 65,536 - object numbers stay 16 bits (D8, an
object above 65,535 is `PDE_LIMIT` as before). There is no lower engine
ceiling: the index is `ceil(4 x entries / 1024)` KiB, at most 256, asked for
with `PDE_NEEDMEM` (CX = `PDMEM_XIDX`, DX = KiB) during `OPEN` when the claim
the primary gave is smaller, and whether the machine has it is the primary's
claim. Refused, the open is `PDE_NOMEM`, and 20.22.6's sentence for it names
the object count. Only `OPEN` ever answers `PDE_NEEDMEM`; after `OPEN` the
index and the window below never grow.

Revisions are folded newest first and an entry is written only while it is
still `0`, so the newest revision that MENTIONS an object wins, a free entry
counting as a mention (7.5.6). A classic revision in such a chain is folded by
reading its 20-byte rows in order (checked as 20.2.3 checks them; a bad row is
`PDE_XREF`). A hybrid section (7.5.8.4) is folded in three passes: the table's
in-use rows, then its `/XRefStm`'s rows, then the table's free rows, so a
compressed object that a hybrid writer lists as free in the table is found in
the stream and a genuinely free one stays free; one the table leaves out of
every subsection is found in the stream either way. (qpdf 12 lets a free row
win over the stream, so the fixtures are of the second shape: there is no
reference reader to hold the first to.) An `/XRefStm` at or past the end of
the file is ignored, as before; one inside the file that is not a
cross-reference stream is `PDE_XREFSTM`.

**20.22.3.2 Cross-reference streams.** At a revision's offset `n g obj` with
`/Type /XRef` is read as a stream (7.5.8): its dictionary IS that revision's
trailer (`/Size`, `/Root`, `/Info`, `/Prev`, `/Encrypt`, `/ID`, the same
newest-wins rule as 20.2.3), `/W [w0 w1 w2]` with each width 0 to 4, `/Index`
(pairs `first count`, default `[0 /Size]`, at most 4,096 numbers), and the
rows: type `w0` bytes (0 width means type 1), field 2 `w1` bytes, field 3 `w2`
bytes, big-endian. Type 0 is free, type 1 is an offset (field 3 the
generation, which, as for a classic entry, is not kept: the object header's is
matched, 20.2.8), type 2 is (object stream, member index), any other type is
a null. An offset under 9 or at or past the end of the file, a width over 4,
an `/Index` that is not pairs of non-negative integers, or rows that end
before `/Index` does, is `PDE_XREFSTM` (now: a DAMAGED cross-reference
stream), which 20.22.3.5 repairs. An object stream number over 32,766 or a
member index of 65,535 is `PDE_LIMIT` (the index cannot name it) and is not
repaired. Rows for objects at or past the index's size are ignored. The rows
are decoded through the ordinary filter chain (Flate with its PNG/TIFF
predictor - qpdf writes `/Predictor 12` - and every other filter of
20.22.3.6); a cross-reference stream is never decrypted (7.6.2). Its Flate
stage takes Flate slot 0 when the machine has the Flate tier (no stream is
open during `OPEN`) and the object-stream window's slot when it has not
(`PDE_NEEDMEM` for `PDMEM_OSW` first). The fold runs in `OPEN` slices of 512
rows (`PDE_BUSY`, `state` 5). `PDO_XREFSTM` is set when a stream was read,
`PDO_OBJSTM` when an entry names an object stream; `TRAILER` reports
`tflags` `PDTR_XREFSTM` when the NEWEST section is a stream, and `startxref`
is that section's offset whatever its kind. `DICT_GET` 0/0 reads the newest
trailer dictionary or cross-reference stream dictionary.

**20.22.3.3 Object streams (`PDMEM_OSW`).** A member is read IN PLACE from
the object stream's decoded bytes, through the one funnel every read goes
through (the file layer's copy, 6.1): **a position names its container.** A
position with bit 31 clear is a file offset; one with bit 31 set is `hi =
0x8000 | object stream`, `lo` = offset in that stream's decoded bytes. Every
save-and-seek-back in the parser (a reference followed in the middle of a
dictionary, the dictionary and array cursors, a page's inherited attributes)
therefore keeps working across a hop into or out of an object stream with no
change to the parser. Resolving member `k` of stream `s` reads the stream's
header pairs (`num off`, 7.5.7): pair `k` must name the object (else every
pair is searched for it; not found is a null), the position is `/First + off`,
the generation is 0 (a reference with any other is a null), and there is no
`n g obj` or `endobj`. A resolved position is kept in the resolved-offset
cache (20.2.2) as `hi | lo`, so the pairs are read once an object.

The decoded bytes live in the OBJECT-STREAM WINDOW: one region of 43 KiB,
exactly the shape of a Flate slot (20.13: a 32 KiB ring at offset 0, the rows,
the tables and the saved state), asked for with `PDE_NEEDMEM` (CX =
`PDMEM_OSW`, DX = 43) at the end of the fold when any entry names an object
stream, or when the repair scan meets one. It is a THIRD Flate slot, private
to the window, so an object stream is decoded while a page's content and a
form hold slots 0 and 1, and an XT without the Flate tier still reads
compressed objects. The ring holds the last 32,512 decoded bytes of the stream
in it; a read ahead of what is decoded decodes on, a read behind it decodes
again from the start (the swap rule: one stream is resident; asking for
another, or for bytes before the ring, re-decodes from that stream's start up
to the bytes asked for). A stream's decoded offsets are 16 bits: bytes past
65,535 cannot be named and a member there is `PDE_LIMIT`. The stream's
geometry (data offset, length, Flate or not, `/First`, `/N`) is read when a
member is first resolved and kept in 8 records (round robin), so a re-decode
in the middle of a token needs no parse; a position in a stream whose record
has been reused is `PDE_LIMIT` (it needs nine object streams entered between
saving a position and returning to it). An object stream's `/Filter` is
`/FlateDecode` (a name, or an array of that one name) or nothing - the ring
is then Flate's own history - and a `/DecodeParms` with a `/Predictor` above
1, or any other filter or chain, is `PDE_FILTER`. (Every writer we know
compresses object streams with Flate alone; a chain would cost the parse of a
second filter list, 20.22.3.7.) An object stream inside an object stream, or
one that is not `/Type /ObjStm`, is `PDE_XREF`. Decryption of an
object stream is crypt's raw layer (20.22.5): the stream context carries the
object number and generation (`sctx.onum/ogen`) for the key, and members are
never decrypted again.

`OBJ_FIND` of a member answers its position (bit 31 set) as `offset` and the
bytes of its VALUE as `length` (`kind` as usual; a member is never a stream).
`RAW_COPY` with `mode = PDRC_EXACT` copies those value bytes; with
`PDRC_PLAIN` it copies `n 0 obj\n`, the value, `\nendobj\n` (`done` and
`got` address that synthesised text, the same bytes every call). `PDRC_PLAIN`
of an object that is not a member is the file's bytes exactly (on a file with
no `/Encrypt`), and on an encrypted file it is crypt's hook
`cr_raw_plain()` in `pdeverb.c` (20.22.5 fills it: strings and stream
decrypted, `/Length` made direct).

**20.22.3.4 What `OPEN` does now.** The states (`pdxb_open.state`): 2 one
revision a slice, as before, now either kind; 5 the fold (`PDE_NEEDMEM` for
the index first; 512 rows a slice); 6 the window check (`PDE_NEEDMEM` for the
window); 7 encryption: when any revision's trailer has `/Encrypt` the engine
records it (a reference, or the position just inside a direct dictionary;
`TRAILER`'s `encrypt_num/gen` and `PDTR_ENCDIRECT`) and calls crypt's hook
`cr_open()`, which answers `PDE_PASSWORD` until 20.22.5 lands (so an encrypted
file is `PDE_PASSWORD`, no longer `PDE_ENCRYPT`, and the trailer is never a
refusal by itself); 3 and 4 the catalog and the page tree as before; 8 to 10
the repair. `/ID[0]` is kept whole up to 32 bytes (`id0`, `id0_hi`, `id0_len`
its true length). The 1 KiB the engine searches for `startxref` is read into
the stream contexts, which are free during `OPEN`'s first state, and
`FINGERPRINT` reads its two kilobytes 128 bytes at a time: the 1 KiB tail
buffer is gone.

`PDE_NEEDMEM` is the ladder's (20.22.1): the primary claims the region, puts
it in `pdxb_init` and calls `INIT` again, then `OPEN` with the same `state`.
**A re-`INIT` whose cache, arena, tables and Flate claims are the ones it
already has keeps the document and the open state** and only takes the new
`xidx_*`/`osw_*` (this amends 20.2.2's "a re-INIT closes any document", which
still holds for any other re-INIT).

**20.22.3.5 Repair (AP-02).** When `OPEN` fails in states 0 to 7, 3 or 4 with
`PDE_XREF`, `PDE_TRUNC`, `PDE_XREFSTM`, or `PDE_SYNTAX` for a missing root
(`err_detail` 4), the engine throws the cross-reference away and scans the
file instead (`PDO_REPAIRED`); a header failure (`err_detail` 1 or 2), an I/O
error, a limit, memory and encryption are not repaired. The scan is a token
walk of the whole file from offset 0 in `OPEN` slices of 4 KiB, skipping every
stream's data (to its `endstream`): `n g obj` at a token boundary is an
object at the offset of `n` (a later one overwrites an earlier one: the
incremental-update rule); an object stream's header pairs are read and each
member written as `(stream, index)` at that point of the walk, so the order
of the file decides between a member and a direct object of the same number;
`trailer <<...>>` and a `/Type /XRef` dictionary are trailers (the LAST one
in the file wins); a `/Type /Catalog` object is remembered, the root when no
trailer names one. Pass 1 (state 8) finds the highest object number, so the
index can be asked for; pass 2 (state 10) writes it. Objects freed by an
update cannot be seen by a scan and stay in use (MuPDF and qpdf do the same).
After a repair `TRAILER` reports `startxref` 0 (there is no cross-reference
for an incremental update's `/Prev` to name: 20.22.4 saves such a file whole)
and `DICT_GET` 0/0 reads the last trailer the scan met, `PDE_NOTFOUND` when it
met none. A failure after the repair is reported as it is. A file damaged AFTER `OPEN`
(an entry found wrong when an object is first read) is `PDE_XREF` at that
verb, as before.

**20.22.3.6 Filters (AP-03) and the codec pass-through.** Kinds now:
`ASCIIHexDecode` `AHx`, `RunLengthDecode` `RL`, `FlateDecode` `Fl`,
`LZWDecode` `LZW`, `ASCII85Decode` `A85`, at most four a chain, `PDFL_*` bits
as `pdabi.h`.

* ASCII85 (7.4.3): a push stage like ASCIIHex. White space is skipped, `z`
  is four zeros between groups only, `~` ends the data, a last group of 2 to
  4 characters gives 1 to 3 bytes, a character outside `!`..`u`, a group over
  2^32 - 1, or a last group of one character is `PDE_SYNTAX` at the read.
* LZW (7.4.4): a slot stage like Flate - the ONE slot stage a chain is Flate
  or LZW (both: `PDE_FILTER`), it needs the Flate tier (none: `PDE_FLATE`, as
  Flate), and it reuses Flate's predictor and delivery: `/DecodeParms`
  `/Predictor` (PNG 10-15, TIFF 2), `/Colors`, `/BitsPerComponent`,
  `/Columns` and `/EarlyChange` (0 or 1, default 1). Codes are 9 to 12 bits,
  most significant bit first; 256 clears, 257 ends; a code above the next
  table entry, or one not yet defined, is `PDE_SYNTAX`; data that ends
  without 257 ends there (as qpdf). The table (4,096 entries of a prefix code
  and a last byte, 16 KiB) and the string, built down from the top of a stack
  above it, are in the slot's window area: no length table.
* The codecs: `DCTDecode` `DCT`, `CCITTFaxDecode` `CCF`, `JBIG2Decode`,
  `JPXDecode`. With `pdxb_strm.accept` 0 they are `PDE_FILTER` at
  `STRM_OPEN`, as before. With `PDSA_CODEC` a codec that is the LAST stage of
  the chain is not decoded: the chain stops before it, `pdxb_strm.codec`
  names it (`PDCD_*`) and `STRM_READ` delivers the CODED bytes; a codec
  anywhere else, or two, is `PDE_FILTER`. The codec's parameters are the
  caller's to read with `DICT_GET` (`DecodeParms`, `[n]` when `/DecodeParms`
  is an array, and `JBIG2Globals`, a reference to a stream the caller opens
  first with its own `STRM_OPEN`).
* `Crypt` stays `PDE_FILTER` until 20.22.5 handles it.

**20.22.3.7 Costs.** PDENG image + bss, measured (`make build/PDENG.MOD`):
47,152 at the round's start (36,822 + 10,330); **46,512 + 9,810 = 56,322**
with 20.22.3.1-8, +9,170 of the 9,800 the 23:20 ledger gives eng (56,952
inside PDENG's 65,024, the part limit). The first build was 58,926; what
brought it back is a checkpoint record with no count of what it stands for
(the walk is bounded by the table instead: 2,048 bytes of bss where the
round's start had 2,560), the tail buffer gone (20.22.3.4: -1,024) and code
shared or table-driven (`kw_ix`, one stream-bounds check, Flate-only object
streams, an LZW with no length table, `pdm_u32_todec` for RAW_COPY's
numbers). A flat-index fold of classic tables was built and withdrawn: it
made a sparse classic file cost 4 bytes an object, which a large one on a
640 KiB XT cannot pay, and the checkpoints never refuse. Deep stack
(tools/os88pdstk.py): the worst verb is `OPEN` at 550 bytes from the door,
inside D23's 1,776, through an object stream's geometry read during the
fold. RAM: nothing for a classic file; `4 x /Size` bytes for a
stream-indexed or repaired one (a cross-reference-stream file of `/Size`
16,000 asks 63 KiB, which a 640 KiB XT with a document open may refuse:
`PDE_NOMEM`, and 20.22.6 says so); 43 KiB more for a file with object
streams (or, with no Flate tier, a compressed cross-reference stream).

The fixtures are `tests/pdfany/eng/` (tools/os88pdfx_eng.py, provenance in its
README): the samples as object-stream files plain and compressed, hybrids, a
cross-reference-stream update on a classic file and a classic update on a
cross-reference stream (PDWRT's shape), a stream with an indirect `/Length`, object streams longer than
the ring read backwards, three damaged files, LZW through every width and
table reset and with `/EarlyChange 0`, ASCII85 and an [ASCII85 LZW] chain.
`hosttest/pde_check.py` holds the engine to qpdf over every one, with and
without the Flate tier: pages, page objects, boxes, every content stream's
decoded bytes, and every object's `RAW_COPY` in both modes against the tool's
own reading of the decoded object stream. `make pdefuzz` mutates them with
the rest of the corpus, the claims random a case and every `PDE_NEEDMEM`
granted.

**20.22.3.8 Seams for the other agents.** Four answers that read the trailer,
the resources and the objects, which are the engine's:

* `RES_LOOKUP` of a resource written DIRECTLY in another object (an inline
  `/ExtGState << /GS1 << /CA 0.5 >> >>`) answers `objnum` 0 and its `kind` as
  before, and now also, in `pdxb_reslookup_x`'s tail (`pdabi.h`'s `[eng
  20.22.3]` block; the verb reads the `pdxb_reslookup` at the head of the
  exchange block and writes the whole extended struct): `rflags`
  `PDRL_DIRECT`, `cnum`/`cgen` the object the value is written in (the page,
  an inherited ancestor, a resource dictionary or a category dictionary,
  whichever was entered last), and `coff`, where the value's `<<` starts in
  that object's `RAW_COPY` `PDRC_EXACT` bytes - from `n g obj` for an object in
  the file, from the value for an object-stream member. PDXG reads the value
  with `RAW_COPY` from `done = coff` and parses it itself. A referenced
  resource has `rflags` 0. (Decision 23:35, Question 3; on an encrypted file
  a string inside the value is the file's bytes, and an ExtGState has none
  that matters.)
* `RAW_COPY` of `objnum` `PDRC_ENCDICT` (0xFFFE, wrt's block, 20.22.4.1) when
  `TRAILER` has `PDTR_ENCDIRECT` copies the text of the direct `/Encrypt`
  dictionary from its `<<` to its `>>`, the file's bytes in either mode;
  without a direct one it is `PDE_NOTFOUND`, and a dictionary longer than 640
  bytes is `PDE_LIMIT` (PDWRT copies it into a fixed buffer).
* `RAW_COPY` `PDRC_PLAIN` of an object is written as up to three pieces, one
  after another in `done`: the frame (`n g obj`, the dictionary, `stream`),
  the stream's data and the tail (`endstream`, `endobj`). An object-stream
  member is framed as before (20.22.3.3). A stream in the file whose
  `/Length` is a REFERENCE has that reference written as its number - the
  dictionary's bytes up to the reference, the decimal value, the bytes after
  it - so PDWRT, which copies an object into a file whose objects are
  renumbered, never carries a reference to a length object it did not copy
  (wrt's request, 20.22.4.2). The number is the one the stream was read with.
  On an encrypted file the PLAIN copy goes to the `crypt` hook
  (`cr_raw_plain`, 20.22.5), which decrypts the strings and the data; the
  EXACT copy is the file's bytes as ever.
* `OPEN` that answers `PDE_PASSWORD` (20.22.5: the file is encrypted and the
  empty password does not open it) leaves the document HALF open - the
  cross-reference, the trailer and the `/Encrypt` dictionary read, the pages
  not - and says so in `xo.state`; `CRYPT SETKEY` with the user's key and then
  `OPEN` again resumes from there, without reading the cross-reference a
  second time (wrt's request: a file that took a minute to index takes it
  once). `OPEN` with state 0, or `CLOSE`, abandons it. `DICT_GET` and
  `TRAILER` answer there (PDXK reads `/Encrypt` through them, 20.22.5); every
  other object verb answers `PDE_RANGE`, as on a closed document. `RAW_COPY`
  tests its mode's LOW byte, so crypt's `PDRC_STRCIPHER` in the high byte
  of an answer fed back is harmless.

**20.22.3.9 A Flate claim cut to one slot (plat's request, 20.22.2.4).** A
re-`INIT` with every claim the same except `flate_kb` keeps the open document
(and an `OPEN` in progress) when `flate_kb` is now one slot's worth (43 to 85
KiB) where it was two. Slot 2 must hold no live stream; the primary regrows the
Flate claim down by that slot's paragraphs afterwards. The answer is CF = 0
with AX = 1 when the cut is taken. AX = `PDE_BUSY` means a live stream sits in
slot 2 and nothing changed: the claim stays as it was, so ask again between
streams. Every other change of the Flate claim still resets the document
(20.2.2). `pde_test flshrink` (pde_check's `XTGUI_OZ.PDF` row) opens two Flate
streams, sees `PDE_BUSY`, closes the inner one, sees AX = 1 and reads the
outer one to the same bytes as an undisturbed read. Cost: 152 bytes.

#### 20.22.4 Saving from and to the new kinds of file (`wrt`)

AP-06. Owner decisions O3 (an encrypted file is saved as an incremental update
encrypted with the file's own key) and O5 (`/P` is ignored; the source's `/P`
and `/Encrypt` are kept unchanged) bind this section. Nothing here changes
14.2's phases, 20.5's verbs or the cap on what a slice may do; it changes what
the phases read and what they write.

**20.22.4.1 The update section stays classic.** Whatever the newest section of
the source is (a classic table, a cross-reference stream, a hybrid), the
update is appended as a CLASSIC `xref` section whose trailer carries
`/Prev <the source's newest startxref>` (`pdxb_trailer.startxref`, which PDENG
answers for a stream section too). Measured, qpdf 12.4.2 and MuPDF 1.28.6:
a classic update over an xref-stream file, shadowing objects that live in an
object stream with top-level objects of the same number, is accepted by both
with no warning (`qpdf --check`, `--show-xref`, `mutool show`), plain and
AES-128. An xref-stream update was equally accepted and buys nothing, so it is
not built: it would need a new object number, a self entry, a binary counted
length, a rewritten `job_verify` and a stream the engine must read back. The
trailer written is:

`/Size`, `/Root`, `/Info` as before; `/Encrypt n g R` when the source has one
(`pdxb_trailer.encrypt_num` / `encrypt_gen`); `/ID [<id0 whole, up to 32
bytes> <a new 16>]` (`id0` then `id0_hi`: the first element is an input of the
key and is written byte for byte); `/Prev`. **A DIRECT `/Encrypt` dictionary**
(`tflags` has `PDTR_ENCDIRECT`) is copied as the file has it: at job start
the job asks `PDV_ENG_RAW_COPY` for the pseudo-object `PDRC_ENCDICT`
(0xFFFE; the text `<< ... >>`, at most 640 bytes, else `PDE_ENCRYPT`), keeps it
(the source is closed before the trailer, when an Insert reads foreign files)
and writes `/Encrypt <<...>>` into the update trailer. Nothing in it is
enciphered (`/O /U /OE /UE /Perms` are the key's own data), so copying is
exact. Cost: 640 bytes of bss and about 150 of code; `pdw_encrun.py`'s
`aes128d` fixture checks it. Engine side (eng): `PDRC_ENCDICT` in
`pdabi.h`'s wrt block. indirectly. ``job_verify` is unchanged (it looks for `xref`).
`job_vfy` re-opens the temp file through PDENG, which now chains classic ->
stream sections (20.22.3); an encrypted output is opened with the held key
(20.22.4.4).

A hybrid source (a classic table with `/XRefStm`): `/Prev` names its classic
section and a reader merges the `/XRefStm` itself, so nothing is written for
it. A source rewritten through an object stream needs nothing either: an
object this job rewrites (an annotation, a page, the catalog, an `/Annots`
owner) is written top-level under the SAME number and generation, which
shadows the compressed copy.

**20.22.4.2 Every read is `PDRC_PLAIN`.** `eng_get` (the one RAW_COPY door of
the page/catalog/annotation patches AND of Extract and Insert) sets
`pdxb_rawcopy.mode = PDRC_PLAIN`; only `job_vfy`'s three-object check keeps
`PDRC_EXACT` (it wants the file's own bytes). The writer therefore never sees
ciphertext and never needs to know whether a source object came from an object
stream: PDENG hands it `N G obj <dictionary or value> [stream ... endstream]
endobj` with the stream's bytes decrypted (AMENDED by the lead's 00:00 decision:
the dictionary's STRINGS are not, see the last bullet). What the writer needs
from PLAIN, and eng/crypt implement (the contract is theirs, 20.22.3 and
20.22.5, this is what is relied on):

* the text begins `N G obj` and ends `endobj`; an object-stream member is
  wrapped in that frame; `done`/`got` address the SYNTHESISED text, the same
  bytes on every call (the writer reads a long stream in chunks);
* a stream object's dictionary carries a DIRECT integer `/Length` equal to the
  number of stream bytes that follow `stream` + one EOL (the decrypted length);
  the stream bytes are decrypted but not defiltered; the text after the data is
  `endstream`, an EOL, `endobj`;
* an object the engine would not decrypt (an `/Type /XRef` stream, a `/Crypt
  /Identity` stream, the `/Metadata` of `/EncryptMetadata false`) is returned
  as it is: the writer treats it like any other stream, see 20.22.4.3 for the
  one case this makes wrong;
* on a file with no `/Encrypt` and no object stream PLAIN is byte for byte
  EXACT, so every byte the writer produced before this round is unchanged.
* **Strings stay ciphertext** (00:00 amendment). On an encrypted source PLAIN
  decrypts only the stream data (and corrects `/Length`); the dictionary's
  strings come through as the file has them, and the RAW_COPY answer says so:
  `PDRC_STRCIPHER` (0x0100, the HIGH BYTE of `rc.mode` in the answer (crypt's pdabi.h); `pdw.h` defines it under
  `#ifndef`, crypt's name and place win). The object's number and generation
  are the request's (`eg_num`/`eg_gen`, kept as `g_snum`/`g_sgen`). An object
  that sat in an object stream (its strings were never enciphered) comes
  without the flag. With the flag set, `pb_src` collects each string (literal
  or hex, at most 256 decoded bytes, else `PDE_LIMIT`), decrypts it with the
  SOURCE's per-object key (`pdc_okey` then RC4, or AES-CBC with the first 16
  bytes as IV and the PKCS#5 pad stripped; AESV3 uses the file key itself) and
  writes it plain as a hex string (Extract, or an unencrypted target) or
  enciphered under the output's key (`sb_begin/sb_put/sb_end` in `pdwser.c`).
  The source's key is `g_sk`: CRYPTINFO after the job's open (the target, and
  Extract's source) or the key plat's pre-flight handed over (`PDV_WRT_KEYS`)
  for an Insert's foreign file; wiped with the output key at job end. A kept
  `/NM /T /CreationDate` text remembers the object it was read from. Cost 1,5xx
  bytes: see 20.22.4.7. `pdw_encrun.py`'s `ann*` fixtures (an annotated
  source, plain, AES-128, RC4-128, AES-256; Extract; Insert of an annotated
  foreign file) compare every copied annotation's strings with a plain
  control, and a build with the decrypt turned off fails 16 rows.

**20.22.4.3 Encrypted output.** The job is *encrypting* (`g_enc`) when the
target is a save of a file whose `pdxb_trailer` says it has an `/Encrypt`
(`encrypt_num` or `PDTR_ENCDIRECT`); Extract and a new document are never
encrypted (their source's key is used to READ; the output is a fresh plain
file, as before).

*The key.* Fetched once, at job start, by `PDV_ENG_CRYPTINFO` on the open,
keyed document: the engine answers `pdxb_crypt` with what `PDV_ENG_SETKEY`
set (`key`, `key_len`, `stm`, `str`, `eff`, `encmeta`, `auth`; lead decision
23:30). The writer copies them (the key does not live in PDENG across Insert,
which re-opens PDENG on other files) and zeroes the copy, the derived
schedules and the foreign keys at job end, on abort and on failure.
**PDWRT enciphers by itself**: `PDV_ENG_CRYPT` was withdrawn from PDENG, and
the per-object key (algorithm 1), RC4 and AES-CBC encrypt are crypt's shared
source (`pdcrypto.h`, `pdcmd5.inc pdcrc4.inc pdcaes.inc`, host twin
`pdctwin.c`) which `pdwrt.asm` includes. `cr_far` enciphers up to 128 bytes in
place in the writer buffer, keeping the carry, the CBC chain and the RC4
state itself between chunks. A method `PDCM_IDENTITY` for
strings or streams (V4/V5 crypt filters) means those are written plain.

*Strings.* Every string the writer emits in an object that is not in an object
stream (every object it writes) is encrypted with the per-object key of the
object being written (`pw_obj` records its number and generation): the text
strings (`/Contents`, `/T`, `/Subj`), `/NM`, `/M`, `/DA`, and every string in
a span copied out of the source (the page, catalog and annotation patches, the
kept `/NM /T /CreationDate`, Extract/Insert objects). A copied span goes
through `pb_src`, which walks it for literal and hex strings, decodes them
(the escapes of 7.3.4.2), encrypts the raw bytes in
chunks of at most 32 bytes (first chunk `phase` 1, last chunk 2) and writes
the result as a hex string `<...>`; everything else in the span is copied
verbatim. A plain job calls the same sites and writes exactly what it wrote
before (`pb_src` is `pb_n`).

*Streams.* The appearance streams are encrypted at the one byte sink,
`pw_n`: `ap_head` starts the cipher after `stream\n`, `ap_tail` finishes it
(the AES padding block) before `\nendstream`. The COUNT pass counts plain
bytes; its result is converted to the stored length before `/Length` is
written: RC4 and the identity are unchanged, AES-CBC is `(n & ~15) + 32`
(16 for the IV, then `16 * (n / 16 + 1)` with PKCS#5 padding). Insert and
Extract-into-encrypted copy a stream by its decrypted `/Length` (from PLAIN):
the dictionary's `/Length` is rewritten to the stored length, the raw `stream`
keyword and EOL are replaced by `stream\n`, the data runs through the same
cipher, and the tail is `\nendstream\nendobj\n` written by the writer. There
is no verbatim copy of ciphertext anywhere: an object that kept its number and
was not in an object stream could have been, but decrypt-then-encrypt is one
path, and is correct for the rest.

*The IV.* The machine has no random source and PDENG has none either (the
caller's `iv` is the contract). The writer builds each IV from eight words of
state: seeded at job start from the CRC of `/ID[0]` and of the save date; for
every IV the kernel tick count (`OSAPI_GET_TICKS`, a gate slot) and the
object number, the output offset and the previous ciphertext (its last 16
bytes, folded in after every cipher call) are mixed in and the words are
spread through `pdm_crc16`, as `id_make` spreads the document ID. What that
buys, and what it does not: no two IVs of one save repeat, nor of two saves of
one file (the tick and the chained ciphertext differ), and an IV is not
predictable from the file alone without the tick at which it was written. It is not
cryptographic randomness. That is enough HERE because CBC needs the IV to be
unpredictable only against an attacker who chooses the plaintext after seeing
the IV, and nothing on this machine lets an attacker choose what a user types
into a note.

*Limits stated.* (a) A copied stream whose source object the engine left
undecrypted because of `/EncryptMetadata false` or `/Crypt /Identity`, copied
into an encrypted target, is encrypted by the writer: such a stream is only
ever a `/Metadata` stream, which readers show nothing of. (b) The strings of
the `/Encrypt` dictionary itself are never encrypted and never touched.
(c) `/P` is not read.

**20.22.4.4 The output is read back.** `job_vfy` opens PDSAVE.$$$ through
PDENG. An encrypted output makes OPEN answer `PDE_PASSWORD` (the engine has no
key for a new document); the writer answers it itself, with no prompt:
`enc_setkey` calls `PDV_ENG_CRYPTINFO`, pokes the held key, `key_len`, `stm`,
`str`, `eff`, `encmeta` and `auth` into the block beside it, calls
`PDV_ENG_SETKEY` and calls OPEN again. (SETKEY is given the methods; the
engine's CRYPTINFO before a SETKEY carries none.) The three objects it reads
for the `N G obj` check use `PDRC_EXACT`.

**20.22.4.5 Insert from an encrypted foreign file.** PDWRT is a slot
occupant, so PDXK cannot load while it runs and the job never pauses for a
password (the earlier `PDE_PASSWORD` pause is withdrawn). plat's PRE-FLIGHT
(20.22.2) derives each encrypted foreign file's key before the job starts and
hands them over with `PDV_WRT_KEYS` (0x85), called after `PDV_WRT_INIT` and
before the job verb: `pdxb_wkeys` { `n`, three `pdxb_wkey` { `slot` (the
Insert slot of the file), `key_len`, `stm`, `str`, `eff`, `encmeta`, `key[32]`
} } in the `[wrt 20.22.4]` block of `pdabi.h`. The writer keeps them until
the job ends (or `PDV_WRT_INIT`/ABORT/failure, which wipe them). When a
foreign window's OPEN answers `PDE_PASSWORD`, `fw_open` finds the key of that
slot, does `enc_setkey` (as for the output) and OPENs again; a file with no
key handed over fails the job with `PDE_PASSWORD`. After the last window the
primary re-opens the SAVED file (`PDW_OUT_REOPEN`); an encrypted save keeps
the source's `/Encrypt` and `/ID[0]`, so the key it already holds opens it.

**20.22.4.6 Host stand-ins and checks.** `make pdwrt-enc` (soak row
`pdwrtenc`) drives PDWRT through `pdw_verb` over a stand-in
(`hosttest/pdw_enc.c`) for the PDENG verbs the engine agents own and that had
not landed when AP-06 was written: PDRC_PLAIN reads (served by
`hosttest/pdw_encprep.py`, which has qpdf decode the fixture), the trailer's
`encrypt_num`/`tflags`/`id0_hi`, OPEN's `PDE_PASSWORD`, CRYPTINFO and SETKEY
(which refuses a key or method that is not qpdf's, so a wrong held key fails
the verify; the harness proves that with a deliberately corrupted key).
The cipher is the writer's own, crypt's host twin, checked by qpdf. The
standing differential for every output (`hosttest/pdw_encrun.py`):
`qpdf --check` clean; `qpdf` with the password reads back the same annotations
(subtype, `/Contents`, `/T`, `/Subj`, `/DA`, `/Rect`, `/C` and the bytes of
the appearance stream) as the unencrypted control run; `mutool draw` is silent
(no `invalid string length for aes encryption`); the source is the output's
prefix; the trailer has `/Prev`, `/Encrypt` and the source's whole `/ID[0]`;
Extract from an encrypted file is a plain file whose pages equal the control's;
Insert into an encrypted file and from an AES-128 file with a user password
(its key handed over by `PDV_WRT_KEYS`, as plat's pre-flight will) equals the
control's. A 100,000-text ASan/UBSan fuzz of `pb_src` is part of the row.
Fixtures are built by the run from `samples/MARKUP.PDF` and
`samples/PAGES.PDF` with qpdf 12.4.2 (`tests/pdfany/wrt/README.TXT`).

**20.22.4.7 Cost.** PDWRT.MOD image + bss: 41,182 + 6,614 = 47,796 at the
round's start; 49,018 + 8,109 = 57,127 now (cap 61,440, lead decision 23:30):
the writer's cipher door, the IV, the string path (`pb_src`, `es_*`), the
stream sink (`cs_*`), the key fetch and re-key (`enc_*`), the Insert stream
copy (`gc_data`) and crypt's shared MD5, RC4 and AES in assembly (about 4.4 KB
of that). One gate slot is added (`GET_TICKS`, for the IV). Its workspace
stays the writer buffer of 20.22.2.3 (18 KiB, up to 40 for Extract/Insert).
`pdstk`: PDWRT's deepest chain is 558 of the 1,776 bytes of the deep stack.
`tests/suite.py` row `pdwrtenc` (soak) is the check; `pdwrt` and `pdwrtreal`
still hold the plain paths byte for byte.

#### 20.22.5 Encryption (`crypt`)

Owner decisions O3 (the empty user password silently, a prompt otherwise; RC4-40,
RC4-128, AES-128, AES-256) and O5 (`/P` is ignored: every action is allowed once
the file opens, and a save keeps the source's `/P` and `/Encrypt`) bind this
section. AP-04 is the story; AP-05 (the R6 question) takes `PDV_KEY_COST` from
here and builds its sentence in 20.22.6. The lead's decision of 23:30 (the
restructure that made PDENG's share fit) is built in. Numbers are measured:
20.22.5.1.

**What runs where (L1).**

| where | what | why there |
|---|---|---|
| PDENG (`pdecrypt.c`; `pdcmd5.inc` `pdcrc4.inc` `pdcaes.inc`) | the per-object key (MD5), RC4, AES-CBC **decryption**, of a stream's raw bytes and of a string lexed from the file; `PDV_ENG_SETKEY`; a minimal `PDV_ENG_CRYPTINFO` | a stream is read while a page is drawn, when the one slot may hold a codec |
| PDXK (`pdxk.asm`, `pdxk.c`, `pdcsum.c`, `pdcsha2.inc`, the same MD5/RC4/AES sources) | reading `/Encrypt` through PDENG's `DICT_GET`; algorithms 2, 4, 5, 6, 7 (R2 to R4) and 2.A, 2.B (R5, R6); the file key from `/UE` `/OE`; the cost estimate | at open only, and large (SHA-2, AES encryption) |
| PDWRT | encrypting what a save writes | **by the shared source**: PDWRT includes `pdcrypto.h`, `pdcmd5.inc`, `pdcrc4.inc` and `pdcaes.inc` WITHOUT `PDC_NOENC` (the encrypting half). `PDV_ENG_CRYPT` is withdrawn (decision 23:30). Per-object key: `pdc_okey(key, klen, num, gen, aes)` leaves the 16-byte MD5 in `pdc_ok`; the key is its first `min(klen + 5, 16)` bytes (AESV2 passes `aes = 1`, which appends `sAlT`; AESV3 uses the file key as it is). `pdc_aes_ks(rk, key, 4 or 8)` then `pdc_cbc_enc(rk, 10 or 14, iv, buf, nblocks)` (the caller writes the 16-byte IV first and pads to PKCS#5); `pdc_rc4_ks` / `pdc_rc4` for RC4. All take NEAR buffers; the host twin is `pdctwin.c` |

**The library** (`apps/pagedesk/pdcrypto.h` is its declaration and layouts). Every
primitive has an 8086 body (SmallerC cdecl, near pointers, `cpu 8086`) and a HOST
twin in `pdctwin.c` written from the standards with the compiler's own 32- and
64-bit integers. `tests/pdcrypt/pdc_diff.py` holds the twin and the shipping
assembly (in Unicorn) to RFC 1321, FIPS 180-4 (SHA-256 including the million-`a`
message, SHA-384, SHA-512), FIPS 197, SP 800-38A F.2 and the RC4 vectors
(RFC 6229 offset 0 among them), then to each other on random keys and lengths
(240 agreements), with a negative control (one flipped bit in AES must be
caught) and every call checked for BP, SI, DI, DS, ES, SS:SP and DF.

| primitive | assembly | body, bytes |
|---|---|---|
| MD5 block, the per-object key block | `pdcmd5.inc` | 822 with its constants |
| RC4 key schedule and stream | `pdcrc4.inc` | 171 |
| AES key expansion (128 and 256), decryption, CBC | `pdcaes.inc` | 1,249 with the S-box and its inverse (512) |
| AES encryption, CBC encryption | `pdcaes.inc` (not under `PDC_NOENC`) | 189 |
| SHA-256 and SHA-512 blocks | `pdcsha2.inc` | 3,636 with the constants (896) |

**PDXK** (`PDM_MAGIC_KEY`, the door at +8 as `pdwrt.asm`; image + bss at most
32,768, no workspace). Verbs; CF = 1 on a failure.

* `PDV_X_INIT` 0xC0 stores the XB segment, PDENG's door (`eng_off/eng_seg`) and
  `cpu`.
* `PDV_KEY_PARAMS` 0xC3: XB = `pdxb_crypt` out. The file's `/Encrypt` parameters
  (`v r length p stm str eff encmeta o_len u_len o u oe ue perms id0 id0_len`),
  no password tried. Read with PDENG: `PDV_ENG_CRYPTINFO` gives the whole
  `/ID[0]`, `PDV_ENG_TRAILER` says where the dictionary is (`encrypt_num/gen`,
  or `PDTR_ENCDIRECT` in `tflags`: eng's fields, 20.22.3.4); then one
  `PDV_ENG_DICT_GET` a key: `/Filter` must be `/Standard`,
  `/V` in {1, 2, 4, 5}, `/R` in 2 to 6, `/Length`, `/P`, `/O` `/U`
  (the first 48 bytes), `/OE` `/UE` `/Perms`, `/EncryptMetadata`, and for V4
  and V5 `/StmF` `/StrF` and `/CF/<name>/CFM` (`/V2` RC4, `/AESV2`, `/AESV3`,
  `/None`, `/Identity`) and `/CF/<name>/Length`. Anything else is
  `PDE_ENCRYPT`. Limits of reading through `DICT_GET`: a **direct** `/Encrypt`
  with V4 or V5 is `PDE_ENCRYPT` (the path to `CFM` is four keys; nothing
  writes one), and `/P` is the 16.16 integer `DICT_GET` returns, so a `/P`
  outside plus or minus 32,767 is `PDE_ENCRYPT` on R2 to R4 (where it enters the
  key) and ignored on R5 and R6. `eff` is `stm` (embedded files follow the
  stream filter).
* `PDV_KEY_AUTH` 0xC1: XB = `pdxb_crypt` in/out. `state` 0 starts: when `v` is 0
  the parameters are read as above (the caller need fill only `pw_len` and
  `pw`). Then the USER password, then the OWNER password. R2 to R4 are one call
  (about a second on an 8088). R5 and R6 are resumable: `AX = PDE_BUSY` (CF = 0)
  with `progress` 0 to 1000 of the run in hand (`state` is nonzero while busy);
  the caller calls again, and cancels by not calling. One call is a slice of
  about 40 work units (an AES block is 1, a SHA-256 block 4, a SHA-512 block 14:
  about 0.6 s on an 8088, 20 ms on a 386). The module must stay loaded between
  the calls (the primary does not swap the slot during a job); a call whose
  `state` is nonzero after a reload restarts from the beginning. Success:
  `AX = PDE_OK`, `key` and `key_len` (5 to 16, or 32), `auth` = `PDCA_USER` or
  `PDCA_OWNER`, and the password copy is wiped. A wrong password: CF = 1,
  `PDE_PASSWORD`. **The silent attempt (`pw_len` 0) on R6 tries the user key
  only**: the owner key costs another 1.7 runs (20.22.5.1) and an empty owner
  password with a non-empty user password is not a thing any writer makes.
* `PDV_KEY_COST` 0xC2: XB = `pdxb_crypt` in (an empty struct is read from the
  file first and returned filled, so the caller learns `r`, the methods and
  `length` from this one call), `pw_len`. `AX` = the estimated seconds of **one
  run of algorithm 2.B** on this CPU class (`pdxb_init.cpu`: 8086 as measured,
  286 a sixth, 386 a 24th, later a 60th); 0 for R2 to R5 (about a second, not
  worth a question). A call whose parameters cannot be read answers CF = 1.
  The caller multiplies by the runs: a correct **user** password is 2 runs (the
  check, then the key salt), a wrong one 1 (the silent attempt) or 2.7 (a typed
  password, which tries the owner key as well).

**PDENG.**

* `PDV_ENG_CRYPTINFO` 0x13 (from the moment OPEN has read the cross-reference):
  `pdxb_crypt` out, with `state` 1 when a key is set, then what `SETKEY` set
  (`auth`, `key_len`, `key`, `stm`, `str`, `eff`, `encmeta`) and the whole
  `/ID[0]` (`id0`, `id0_len` at most 32) always; `v`, `r` and the strings stay
  0 (PDXK reads those). The plat pre-flight for Insert reads a foreign file's
  key and methods here.
* `PDV_ENG_SETKEY` 0x14: XB = `pdxb_crypt` in (`key`, `key_len`, `stm`, `str`,
  `eff`, `encmeta`, `auth`). The XB's copy of the key is zeroed. `OPEN` then
  resumes from its state 7 and `PDO_ENCRYPTED` is set. `OPEN` answers
  `PDE_PASSWORD` (CF = 1) at state 7 without discarding the document, and
  **`DICT_GET` and `TRAILER` are allowed in that pause** so PDXK can read the
  dictionary.
  Closing, re-INIT or a new OPEN forgets the key.
* `PDV_ENG_CRYPT` is **withdrawn**.

Where decryption sits (and nowhere else):

| place | what is decrypted |
|---|---|
| `pdestrm.c` `raw_get`, `st_decode`'s `nflt == 0` branch, `v_strm_read`'s `nflt == 0` branch (each one `if (c->cmode)` call of `cr_read`) | the raw bytes of a stream, at the lowest layer, so every filter, Flate included, sits above plaintext. An AES stream is its 16-byte IV and blocks; the padding is stripped from the last block, which the stream's `/Length` identifies, so nothing is held back; a length that is not a multiple of 16 loses the tail, a padding byte outside 1 to 16 is left alone, a stream under 32 bytes is empty |
| `pdelex.c` `ob_value`'s `PDT_STR` case (`cr_string`) | a string lexed from the FILE in object mode: its first 254 bytes (the rest of a longer string stays ciphertext; the true length is adjusted). Never the `/Encrypt` dictionary's own strings (`cr_curn` equals `tr_enc_n`), the trailer's (`cr_curn` 0), or a member of an object stream (eng sets `cr_curn` to 0 while the lexer is in one: the object stream is what was encrypted) |
| `st_push` (`cr_ctx_open`) | the caller leaves the stream's object in `cr_sn/cr_sg` (0 = not encrypted: an xref stream, or a caller that did not say; `v_strm_open` sets them, and eng sets them for an object stream); the key is derived there |

A stream is left as it is when it is an xref stream (`/Type /XRef`), a
`/Metadata` stream with `/EncryptMetadata` false, or its chain names `/Filter
/Crypt` with `/DecodeParms << /Name /Identity >>` (or no name); `/Crypt` is a
chain member that costs no stage. A stream declared `declen` is unknown (~0)
when it is AES. Limits: at most `PDE_RC4N` = 3 RC4 streams open at once
(`PDE_LIMIT` otherwise: their state is 258 bytes each); strings and streams
under different methods work (`/StmF` and `/StrF`).

**`RAW_COPY` with `PDRC_PLAIN` on an encrypted document** (the lead's decision of
00:00, option 2; eng's hook `cr_raw_plain(rw)` for an object in no object
stream, called after `ob_extent`; any answer but `PDE_NOTFOUND` is the verb's
own). The **dictionary's strings stay ciphertext**: the answer sets
`PDRC_STRCIPHER` (0x0100) in the HIGH byte of `pdxb_rawcopy.mode`, meaning "the
strings of object `objnum`, `gen` are encrypted under that object's key with
the document's string method (`pdxb_crypt.str` of CRYPTINFO)": the caller
decrypts them with the shared source (`pdc_okey`, then `pdc_cbc_dec` after
`pdc_aes_ks`, or `pdc_rc4`), PDWRT as it tokenises. The flag is not set for the
`/Encrypt` dictionary's own object; it is set for every other non-member object
of an encrypted document (a member of an object stream has plaintext strings and
no flag). **eng's dispatcher must compare `(mode & 0xFF) == PDRC_PLAIN`**, because
a caller feeds the struct back with the flag in it. A STREAM whose data the
document encrypts comes out as: the file's bytes of the object up to `/Length`'s
key, ` <plain length>` (a direct integer, whether `/Length` was one or a
reference), the file's bytes to the end of `stream` and its EOL, the DECRYPTED
data (still in its filters), then `\nendstream\nendobj\n`. The plain length is
`/Length` for RC4 and, for AES, `/Length` less the 16-byte IV, the bytes past the
last whole block, and the last block's padding (read from the last two cipher
blocks before the first byte is delivered). Anything else (a dictionary or
array, an xref stream, a `/Crypt /Identity` stream, an unencrypted `/Metadata`)
is the file's bytes: `PDE_NOTFOUND` to eng's own copy. `done` and `got` address
the synthesised text, the same bytes on every call at any chunking; the DATA is
sequential (the first byte of it, `done` = the header's length, restarts the
cipher; a `done` that skips back into the data is `PDE_RANGE`), through a ctx
borrowed above `nctx`, which a `STRM_OPEN` in between invalidates. The header
must be under 64 KiB. `tests/pdcrypt/pdc_plain.py` holds it to `qpdf
--decrypt` for the 17 fixtures at chunks of 1, 37 and 4,096 bytes.

**Sizes** (image + bss, bytes). PDENG 43,964 + 11,827 = 55,791, which is +8,639
over the round's start (47,152; the brief said +3,200 and the lead's 23:30
decision +6,500): the assembly 2,242, the C (`cr_read` `cr_ctx_open` `cr_string`
`cr_sched` `cr_skipchk` `SETKEY` `CRYPTINFO`) and the hooks in eng's files about
3,000, PLAIN 1,734, bss 1,590 (three RC4 states 774, the AES schedule 240, the
`sctx` fields 160, the key 32, PLAIN's 76). PLAIN was estimated at 250 bytes; the
32-bit offset arithmetic through `pdm_u32_*` is what makes it 1.7 KB. PDXK 14,958 + 4,391 = 19,349 of 32,768.

**Stacks.** PDENG's chain through `cr_read` is no deeper than `v_open`'s (436
bytes, `os88pdstk`). PDXK measured by `tests/pdcrypt/pdk_stack.py`: 226 (RC4),
256 (AES-128), 264 (R6, three slices, the nested `DICT_GET` and SHA-512)
bytes below the door's entry, of 1,776. Far edge to name in `os88pdstk.py`:
`_pdk_engcall` to PDENG's `pde_entry`.

**Checks.** `make pdcrypt` (host and Unicorn; not in `all`): `pdc_diff.py`
(the library), `pdc_e2e.py` (the real PDENG and PDXK on the host against `qpdf`:
the file key equals `qpdf --show-encryption-key` for 17 fixtures
(`tests/pdfany/crypt/`, `tools/os88pdfx_crypt.py`), for the owner password too,
a wrong password and the empty one on a file that has a password are
`PDE_PASSWORD`, and every stream's CRC, every string, annotation and outline
title equals the same driver's over `qpdf --decrypt`), `pdc_plain.py` (RAW_COPY PLAIN against qpdf), `pdc_target.py` (the
**compiled** `PDENG.MOD` and `PDXK.MOD` in Unicorn, kernel stub and all,
produce the same lines as the host, PLAIN copies and a full R6 run included), `pdk_stack.py`, and
`pdc_fuzz.py` (ASan/UBSan over mutated copies of every fixture: bit flips, runs
of 00 and FF, a lying `/Length`, a cut tail, ciphertext cut to 0 to 40 bytes, a
corrupted `/O`; about 22,000 runs over three seeds, no crash, no hang, no report).

#### 20.22.5.1 R6 cost, measured

Algorithm 2.B: `K = SHA-256(pw || salt || udata)`, then at least 64 rounds, each
`K1 = (pw || K || udata) x 64` encrypted with AES-128-CBC under `K[0:16]`, `IV =
K[16:32]`, and `K = SHA-256, SHA-384 or SHA-512(E)` by the sum of `E`'s first 16
bytes mod 3, until `E`'s last byte is no more than the round number minus 32.
Counted by running it (`pdk_cycles.py`, the host twin standing in for the data),
priced from the shipping assembly run in Unicorn with the 8088 rule
`max(4.34 x instruction bytes, clocks)` of `tests/pdmath/pdcycles.py`, and
checked against the real `PDXK.MOD` (three of its own slices, instruction by
instruction: the glue and the hash finalisation add **6.2 %** to the operations
priced alone, and that factor is in every figure below).

| operation on an 8088 | clocks | ms |
|---|---|---|
| AES-128 CBC encrypt, one block | 61,100 | 12.8 |
| AES-128 key schedule | 23,200 | 4.9 |
| SHA-256 block (64 bytes) | 204,400 | 42.8 |
| SHA-512 block (128 bytes) | 790,100 | 165.6 |
| MD5 block | 98,200 | 20.6 |
| AES-128 / AES-256 decrypt, one block | 74,900 / 105,200 | 15.7 / 22.0 |
| RC4, per byte | 203 | 0.043 |

One run of 2.B, mean over 60 random salts (rounds are 69 to 71 on average, 64 at
least):

| password | rounds | AES blocks | SHA-256 blocks | SHA-384/512 blocks | seconds (min to max) |
|---|---|---|---|---|---|
| empty | 69.8 | 13,350 | 1,142 | 1,156 | **437** (355 to 557) |
| 6 characters | 69.9 | 15,021 | 1,307 | 1,282 | 490 (425 to 623) |
| 12 characters | 69.0 | 16,445 | 1,449 | 1,389 | 534 (455 to 652) |
| 24 characters | 71.2 | 20,452 | 1,733 | 1,750 | 665 (560 to 821) |

That is `9.1 x (password + 48)` seconds, the formula in `PDV_KEY_COST`
(`pdk_cycles.py --stats`). A run is a **seventh of an hour for the empty
password**; the attempts are runs: a correct user password is **two** (the check
and the key salt: 14.6 minutes for the empty one, 13 to 15 on a file's own
salts), a wrong password typed in is 2.7 (the user key, then the owner key, whose
`udata` is `/U`'s 48 bytes, makes its run 1.7 times as long: about 22 to 24
minutes),
and the silent empty attempt on a file that has a user password is 1 (7.3
minutes, to learn that a prompt is needed). The question O6 asks on an 8088 is
therefore "about 15 minutes" for a file that opens, and the prompt that follows a
failed silent attempt another 15. On a 286 (a sixth) it is 2.5 and on a 386 well
under a minute. The other algorithms, whole `KEY_AUTH` calls on an 8088 (the parse through
`DICT_GET` included): R2 0.17 s, R3 and R4 1.8 s (50 MD5 passes and 20 RC4
passes), a wrong password on R3 5.1 s (the owner key as well).

**Stream decryption speed on an 8088** (the cost of a page that is encrypted,
not of opening it): RC4 23 KB/s; AES-128 CBC **1.0 KB/s** (AES-256 0.7): a
30 KB content stream is half a minute. Candidates, in the order of their gain
(estimates only, none built): a table-driven AES decrypt (the inverse tables
are 1 KB at least and PDENG's share has no room: 2 to 3 times); fusing the
permutation and the key addition in `aes_decb` (a quarter); for R6, a
table-driven AES-128 encrypt in PDXK (4 KB of bss, built at the first call; AES
is 39 % of a run, SHA-512 44 %, SHA-256 11 %: roughly 437 to 350 seconds) and a
fused 64-bit rotation in `pdcsha2.inc` (SHA-512's share by about a third).

#### 20.22.6 The password prompt, the R6 question, errors and preferences (`ui`)

Stories AP-04 (the user half) and AP-05. Files: `pdu.h`'s prefs block (the
state and the calls below, which have to precede `pducmd.c`'s popover shut
path), `pduauth.c` (the note that says so), `pdovlauth.c` (the overlay: paint,
keys and the one entry `ovl_au_do`), and `pdovlpref.c`/`pducmd.c`'s few arms.
`PD_AUTHDEMO` (a define, default off) adds ^Q/^T/^R/^U in `os88_onkey` that
call `pdu_au_ask(0)`, `ask(1)`, `askr6(1500)` and `busy`+`prog(450)`, for a
look at the prompt before an encrypted file can be opened.

**One popover, four modes.** The prompt is the Preferences popover's second
face (`P_PREFS` with `pdu_au_mode` nonzero), not a popover id of its own: that
shares every dispatch arm in `pducmd.c`, which the primary's budget could not
pay for twice. It is a framed white popover of four text rows, 36 columns,
centred in the canvas (no source control). While it is up the keys are its own
(`ovl_pf_key` hands over), and a click inside is ignored. A click outside, or
opening any other popover, **cancels** it (answer -1). Esc is the open job's
cancel when a job is running (`pdu_jcancel`, the order of PD-03): **the open
job's cancel path must call `pdu_au_close()`**, which also zeroes the buffer;
with no job running, Esc closes the popover and the answer is -1.

| mode | rows | keys |
|---|---|---|
| `AU_PW` asking | `Password: ***_` / blank / blank / `Enter: open  Esc: cancel` | printable ASCII appends (at most 127), Backspace deletes, Enter answers (an empty entry is ignored: the empty password was already tried), Esc cancels |
| `AU_PWBAD` | the same with `Wrong password.` in row 2 | the same; typing leaves the line until Enter |
| `AU_R6` the question | `This file's encryption takes` / `about N minutes on this machine.` / `Open anyway?` / `Enter: open  Esc: cancel` | Enter or Y answers Yes, Esc or N answers No |
| `AU_CHECK` working | `Checking...  NN%` / blank / blank / `Esc: cancel` | Esc cancels |

The wording of the question is the owner's (O6), verbatim apart from N, which
is whole minutes, rounded up, at least 1, from `PDV_KEY_COST`'s seconds
(`pdu_au_askr6` does the conversion). A password is at most **127 bytes** when
typed (R6's limit): the buffer is `pdu_au_pw[128]`, resident bss (an overlay's
data stays resident, so only code can move), 128 bytes of the primary. Stars are
drawn for the last 22 characters at most. The buffer is zeroed when the popover
closes by any route, when a new ask begins and when the answer is taken
(`pdu_au_close`).

**The calls plat's ladder makes** (primary, `static`, one translation unit;
the open job owns the sequence, this file owns the popover and the buffer):

| call | meaning |
|---|---|
| `int pdu_au_ask(bad)` | the empty password failed (`bad` 0) or the typed one did (`bad` 1): show the prompt, buffer cleared, answer cleared. Returns 0 if the popover could not be shown (the overlay was refused): the caller then fails the open with `PDE_PASSWORD`, whose sentence says what is needed |
| `int pdu_au_askr6(unsigned secs)` | the question, `secs` being `PDV_KEY_COST`'s answer. Returns 0 as above (the caller then does NOT derive on an 8086) |
| `int pdu_au_busy(void)` | show `AU_CHECK` with no question: the derivation on a faster CPU runs with progress and Cancel |
| `void pdu_au_prog(int permille)` | the work's progress (`pdxb_crypt.progress`); repainted at the wake |
| `int pdu_au_poll(void)` | 0 waiting, 1 answered (Enter or Yes; the popover is now in `AU_CHECK`), -1 cancelled (Esc, N, or the popover was closed some other way; the popover is gone). Read it each slice while the job waits; a -1 means the caller cancels the open exactly as an Esc on the progress line does |
| `pdu_au_pw`, `pdu_au_len` | the typed password and its byte count, valid when `pdu_au_poll()` is 1 after `pdu_au_ask`. The caller copies it into `pdxb_crypt.pw/pw_len` and zeroes its own copy afterwards |
| `void pdu_au_close(void)` | the work is over (success, or the caller gives up): zero the buffer, drop the state, and close the popover at the next wake. A wrong password does not call this: it calls `pdu_au_ask(1)` and the popover stays |

The R6 sequence on `CPU_8086` is therefore: `pdu_au_askr6(secs)`, poll until
1 or -1, then derive with `pdu_au_prog` updates, and `pdu_au_close()` at the
end; on any other CPU the caller shows `pdu_au_busy()` instead. The question
is asked once per open, before the first R6 derivation (the empty password's).

The calls are macros over ONE overlay entry (`ovl_au_do`, so one thunk in the
primary); the popover's paint and keys are Preferences' own entries, which hand
over while the mode is set. A refused overlay fetch makes ask, askr6 and busy
answer 0 and shows nothing.

**Errors.** `pdu_errtext` has one plain sentence for every code the round
adds or re-means: `PDE_PASSWORD` "A password is needed.", `PDE_NOSLOT` the
`PDE_NOMEM` sentence ("Not enough memory. Close a window and try again."), `PDE_ENCRYPT`
"Its encryption handler is not supported." and `PDE_XREFSTM` "Its
cross-reference stream is damaged." `PDE_NEEDMOD`/`PDE_NEEDMEM` are statuses
the ladder consumes and never reach a sentence. The primary names no
`PDF_INC_*` reason of its own: the module that sets the bit supplies the name
in `pdxb_rstatus.first` (so `PDF_INC_MODULE` reads as the module's note, "module
not loaded"), and Document info says only "incomplete rendering".

**Preferences.** `PAGEDESK.CFG` is format **version 2**, eleven bytes: the
version-1 ten, then byte 10, the **module cache** (0 Auto, 1 Off: the XMS/EMS
cache of module images, O2). A version-1 file (ten bytes, version byte 1)
still loads, and byte 10 keeps its default, Auto. A file of any other version
or length is ignored as a whole, as before, and a save always writes version 2.
The dialog gains a sixth row, `Module cache  Auto/Off`; its note and key lines
merge into one (`Arrows Esc  View, caches: next open`) and the popover widens
to 36 columns, so it stays **eight rows** and the COMPACT 640x200 canvas
(92-104 px) still holds it. The setting is read at the open, before the claims
(like Flate). plat reads it through `pdu_pref_modcache()`, a macro over
`pdp_cfg[10]`: 0 Auto, 1 Off.

**Help.** The profile card (Help > The XT profile) is rewritten to the
round's profile (the module that skips a feature names it in the status line); the final pass of the round checks it against 1.3's table.

#### 20.22.7 PDREND's core: the ladder, the cache, images, Type3 (`rcore`)

This section is the contract of `apps/pagedesk/pdrxc.c` (PDREND's side of the
slot and the extension cache, #included first by `pdrend.c` so every
`pdr*.c` file and every agent's glue can call it), the ladder in `pdrv.c`, the
codec image path (`pdri.c` `do_image`, `pdrv.c` `draw_image`), sample depths
through `pdrprim.inc`'s smode 4, and the two pieces of PDREND that moved into
PDXG to make room: `pdxdash.c` and `pdxedit.c`.

**20.22.7.1 The slot from PDREND's side.** `PDV_REND_SLOT` (0x48): BX = the
`PDX_*` now in the slot, CX:DX its door (offset:segment), BX = 0 or DX = 0 =
none. The primary sends it after every load or eviction, BEFORE the next
step. A change of occupant counts one in `pdxb_rstatus.swaps`; an occupant that
leaves while one of its images is open leaves that image's ONE engine stream
on top of PDENG's stack, and PDREND closes it itself (the occupant cannot any
more). The pinned helpers:

| helper | meaning |
|---|---|
| `x_have(id)` | 1 when `id` is the occupant now |
| `x_need(id)` | this pass needed `id` and skipped the work: recorded as pending; when `id` was refused for the page view it notes `PDF_INC_MODULE` instead |
| `x_gone(id)` | `id` was refused for this page view: draw the degraded form |
| `x_call(verb, bx, cx, dx)` | far call to the occupant's door (`_pdr_xfar`: AL = verb, BX CX DX, DS saved round it), AX back; CF in `x_cf`, DX in `x_dx`. `PDE_NOSLOT` with `x_cf` = 1 when the slot is empty. Each call sets the occupant's bit in `pdxb_rstatus.mods` |
| `x_mem(region, kb)` | ask the primary for a region (`PDMEM_*`); 0 = refused for the page view |
| `x_rmark(id, y0, y1)` | a display-list record on rows y0..y1 needs `id` while bands are drawn (a codec image): the page's bands look at their records |

**20.22.7.2 The ladder (L2).** One answer at a time, and only between passes:

* At the end of an interpretation pass PDREND answers, in this order: a
  wanted region (`PDE_NEEDMEM`, CX = `PDMEM_*`, DX = KiB), else the
  highest-numbered pending module (`PDE_NEEDMOD`, CF = 0, DX = `PDX_*`;
  fonts before graphics, so PDXG - whose dash and edit-mark work is never
  cached - is the one left resident). The step after the answer runs the
  pass again from its start (`pass_begin`: the band group rescans, D4).
* What the primary did NOT provide is refused for the page view: a module
  not in the slot after the answer, a region whose segment is still 0 after
  the re-INIT. `PDF_INC_MODULE` names a refused module; the page is drawn
  without the work.
* While bands are drawn, before band n is composed, its `DLR_IMAGE` records
  say which decoders it needs (`x_rmark` lets a page with no codec image skip
  the look). When the occupant is one of them the band is drawn with it;
  otherwise the lower id is asked for with `PDE_NEEDMOD` and the band is
  drawn after the answer. The display list is NOT rebuilt. ONE SLOT: a band
  whose images need both PDXI and PDXJ is drawn with one of them, and the
  other's images in that band are not drawn and set `PDF_INC_MODULE` - the
  open limit of this section (a later round can draw the second decoder's
  images onto the composed band when no later record shares their rows).
* A page view makes at most `PDR_XASKS` = 8 answers; the next want is
  refused. A page view starts at `PDV_REND_BEGIN` and lasts to the next.
* The answer to `PDE_NEEDMEM` is a re-INIT with the same claims and a region
  added: an INIT whose first 24 words (`pdxb_init` up to `slot_seg`) are the
  current ones while a region is asked for adopts the regions only and keeps
  the page view, the document's fonts and the cache. Any other INIT is a new
  document (D2).
* The text map (`PDV_REND_TEXTMAP`) runs the same ladder: after an answer
  the walk restarts and the glyphs the primary already has are skipped.
* Codes PDREND's door answers with CF = 0: `PDE_BUSY` 65, `PDE_NEEDMOD` 66,
  `PDE_NEEDMEM` 67; CX and DX come from `pdr_out_cx` / `pdr_out_dx`.

**20.22.7.3 The extension cache (`PDMEM_XC`).** Results computed by an
occupant during interpretation are kept here so a later pass needs no module
(L1). Entries are packed from offset 0: an 8-byte header (`kind` byte,
`flags` byte, `len` word = the bytes after the header, `k0`, `k1`) then the
entry's bytes, even-rounded. An entry is named by (`kind`, `k0`, `k1`): `XCK_CS`
1, `XCK_FN` 2, `XCK_SHADE` 3, `XCK_PAT` 4, `XCK_LUT` 5 (gfx), `XCK_FONT` 6,
`XCK_CMAP` 7 (fonts), `XCK_GS` 8 (blend). `xc_find(kind, k0, k1)` and
`xc_alloc(kind, k0, k1, bytes)` answer the offset in `g_xcseg` of the entry's
BYTES, never 0 (the header is in front); 0 = absent / no room. The scan is
`pdr_xcfind` in `pdrprim.inc`.

* No region yet: `xc_alloc` asks for one (`x_mem(PDMEM_XC, PDR_XCKB)`, 16
  KiB; any grant of 1 to 63 KiB is used) and answers 0; the caller degrades
  for this pass as if its module were absent, and the pass is run again with
  the region.
* Full: `xc_alloc` answers 0 and marks the pass. At its end the cache is
  emptied and the pass run once more (once a page view); full again,
  `PDF_INC_LIMIT` ("extension cache").
* AN OFFSET IS GOOD UNTIL THE END OF THE PASS. Keep keys across passes, never
  offsets. A new region, or a new document, is an empty cache.

**20.22.7.4 Images through a codec.** `do_image` takes an XObject whose LAST
filter is `/DCTDecode`, `/CCITTFaxDecode`, `/JBIG2Decode` or `/JPXDecode`
(`PDCD_*`) as a codec image: the record carries the codec, and `x_rmark`
asks for `PDX_JPX` (JPX) or `PDX_IMG` (the rest) before its first band. A
JPX image may omit `/BitsPerComponent` and `/ColorSpace` (the codec's own).
In `draw_image` the occupant decodes it: `PDV_IMG_OPEN` with `pdxb_img`
(objnum/gen, codec, W H, bpc, ncomp, `want_w`/`want_h` = the device pixels
covered, flags = the record's stencil and /Decode bits as `PDIMG_MASK` /
`PDIMG_INVERT`); `PDV_IMG_ROW` BX:CX = the image row buffer (the glyph
claim's offset 0, 1 KiB), DX = the row. Rows are asked for ascending; a row
above the last asked re-opens. The module opens its OWN stream and holds at
most that one between rows; it applies `/Decode`. Everything `IMG_OPEN`
answers is checked as hostile: `out_w` 1..4,096, `out_h` 1..32,767,
`out_mode` a `PDIM_*`, `out_ncomp` 1..4, `rowbytes` at least what the mode
reads and at most 1,024; anything else, or CF = 1 on any verb, is
`PDF_INC_IMAGE` ("image decoder") and the image is not drawn. An open image
stays open from band to band and is closed by `PDV_IMG_CLOSE`, by another
image, and by the pass's end. A codec image whose decoder is not the occupant
when its band is drawn (refused, or the band's other decoder, 20.22.7.2):
`PDF_INC_MODULE`. Inline images through a codec are not drawn
(`PDF_INC_FILTER`, as before).

**20.22.7.5 Sample depths and colour (smode 4).** Images of 1, 2, 4, 8 and 16
bits a component in DeviceGray, DeviceRGB and DeviceCMYK are drawn, and in
any other space through gfx's LUT: the hook `XG_IMAGE_LUT(obj, gen)` answers
the `XC` offset of an `XCK_LUT` entry (`ncomp` at +0, `flags` at +2 - bit 0:
the grey is 255 minus the sum, CMYK-shaped - and `ncomp` tables of 256 bytes
at +16, `/Decode` folded in), or 0 having asked for PDXG or noted
`PDF_INC_COLOUR`. `pdr_imgrow`'s smode 4 unpacks a sample of any of those
depths (16 bits: the high byte), maps it through the table of its component
(or the device weights 77, 150, 29 for RGB and an equal share for CMYK when
there is no LUT), sums saturating, and inverts for a CMYK-shaped space.
`pdr_ip` grows by `nc`, `bpc`, `lseg`, `loff`, `lflags` (42 bytes). A raw row
over 1,024 bytes is still `PDF_INC_LIMIT`.

**20.22.7.6 The image record** (`DLR_IMAGE`, `DLI_BODY` = 28 bytes): xl yt
xr yb (qpx), W H, obj gen, bpc ncomp flags codec (bytes 16-19), the
`/SMask` or `/Mask` stream's obj gen (20, 22: blend's 20.22.10.3), the LUT's
table offset in the XC (24) and its flags (26). Flags: 1 stencil, 2 `/Decode`
inverts, 4 mirrored, 8 a LUT, 16 inline (the rows follow the body), 32 an
`/SMask`, 64 a `/Mask` stream. Blend's hook `XB_IMAGE_MASK(obj, gen, kind)`
(kind 32, 64, or 128 for a colour-key array) is told of each; without its
8-bit path the image is drawn opaque with `PDF_INC_TRANSP`.

**20.22.7.7 What moved to PDXG.** Two pieces of PDREND that run only while a
page is interpreted moved into PDXG as `#include`d source, each behind a
generator verb that hands back its work in the XB, so the result is drawn by
PDREND's own path and paint to the same pixel as before:

* `pdxdash.c`, `PDV_GFX_DASH` 0xCE (`struct pdxb_dash`): the dash walker of
  20.12.4, one piece of at most `PDXD_NPT` = 12 points a call; AX 1 a piece,
  2 too fine (stroke solid), 0 done. The core puts the subpath's vertices in
  the XB at 160 (x) and 560 (y).
* `pdxedit.c`, `PDV_GFX_EDIT` 0xCF (`struct pdxb_emark`): the edit store's
  marks of 13.3 / 17.4 (note, highlight, area, ink, signature, line,
  rectangle, ellipse) for one record as ops in page space at XB offset
  `PDXE_OPS` = 64, each batch ending at its first PAINT because a dash uses
  the XB. A record is replayed and the ops the core already has skipped;
  ink resumes from the block's `st[]`. The Text tool's box stays in PDREND:
  its line breaking needs the core's font widths.

Without PDXG (refused): a dashed line is stroked solid and the page notes
`PDF_INC_MODULE`; edit marks other than Text boxes are not drawn and the
page notes `PDF_INC_MODULE`. A page with PageDesk marks therefore loads PDXG
(once per document on a machine without a module cache: the slot keeps its
occupant between pages).

Type3 fonts are the fonts agent's (20.22.8), drawn by this interpreter.

**20.22.7.8 Checks.** `hosttest/pdr_xmod.c` stands in for the occupants on
the host: PDXG's dash and edit verbs through the real `pdxdash.c` and
`pdxedit.c`, and a fake image decoder that delivers a known ramp whatever the
bytes (`PDR_XFAKE` grey / n8 / bits; `PDR_XHOSTILE=<seed>` answers hostile
sizes and failures). `pdr_test` plays the primary's part of the ladder
(`PDR_NOSLOT=<ids|all>` refuses modules, `PDR_NOXC` the region,
`PDR_XCKB` sizes it). `tools/os88pdfx_rcore.py` writes the fixtures (dash,
codec ramp, depths and their 8-bit grey equivalent, masks);
`hosttest/pdr_xchk.py` (`make pdrend-xchk`) holds them to MuPDF and to each
other: a codec page draws to the pixel as the plain image does, `depth.pdf`
as `depthg.pdf` does, and `depthg.pdf` and `dash.pdf` as mutool does.

#### 20.22.8 Fonts from their programs (`fonts`)

*Reserved; written by its agent before its code.*

#### 20.22.9 Colour spaces, functions, shading, patterns (`gfx`)

PDXG (`PDX_GFX`, magic `PDM_MAGIC_GFX`, `PDXG.MOD`, image + bss at most
65,024: lead decision 23:18) is the slot occupant that does the work of `cs CS sc SC scn SCN sh` and
of images in non-device colour spaces **at interpretation time** (L1). It
compiles a colour space, a function or a shading and hands the RESULT to the
core, which keeps it in the extension cache (`XCK_CS`, `XCK_SHADE`, `XCK_LUT`,
`XCK_PAT`); a later pass or page needs no module. The module never claims
memory (3.4), never holds a result between verbs except in its own workspace,
and every verb is one call with no C recursion (20.15's stack bound).

**What it reads.** The module reads PDF syntax itself, through PDENG's
`PDV_ENG_RAW_COPY` in `PDRC_PLAIN` mode (so an object-stream member or an
encrypted string arrives plain; one window of 1,024 bytes a dictionary).
The core names a RESOURCE, not an object: `PDGXI_RES` with the Form's object
number (0 = the page, whose index is `bpc`) and the text `Category\0Name`; the
module walks Resources/Category/Name through `/Parent` (8 levels), finding the
key by a scan that parses nothing but the value it wants (a page dictionary can
be larger than the node tables). It also accepts a plain object number or direct
syntax (`tseg:toff`, `tlen` <= 1,024) for the host harness and for a function. Stream data (a type 0 or 4 function, a mesh shading) is read through
`PDV_ENG_STRM_OPEN/READ/CLOSE`. A tiny non-recursive parser (`pdxgp.c`: an
explicit stack, a bump arena of 12 KiB in the workspace, names, numbers,
arrays, dictionaries, references; at most 64 items a container, depth 8)
turns it into a flat node table. References are chased by re-fetching, at most
8 hops a value.

**Numbers.** 16.16 `pd_fx` throughout; no `long`, no float. A function is
evaluated in `pd_fx`; domains, ranges, encodes and decodes clip as the PDF
reference says. The luminance the core stores is `(77r + 150g + 29b) >> 8`
(pdri.c `col_set`), so a colour computed here is the core's own after one
call.

**Colour spaces** (`PDV_GFX_CS`). Device spaces never reach the module.
Handled: `Indexed` (base any of the below, hival <= 255, table string or
stream), `ICCBased` (by `/N`: 1, 3 or 4 components are `DeviceGray`,
`DeviceRGB`, `DeviceCMYK`; `/Alternate` and the profile are never read - an
approximation, not an Incomplete bit), `CalGray`, `CalRGB` (white point,
gamma and matrix applied, then XYZ D50 to sRGB), `Lab` (white point, range),
`Separation` (`/All` and `/None` included: None paints nothing), `DeviceN`
(up to 8 colorants, tint transform any function type), and `Pattern` (with
or without an underlying space). The compiled space is `struct pdgx_cs`
(`pdabi.h`'s gfx block): a 16-byte header and, for a 1-component space, a
256-entry RGB table indexed by `round(c * 255)` (Indexed: by the index
itself). So the core's `sc` for any 1-component space is one table lookup. A
space with more than one component is `PDGX_F_RGBLIKE` (CalRGB and ICC N=3:
the core takes the operands as device RGB), `PDGX_F_CMYKLIKE` (ICC N=4) or
`PDGX_F_MODULE` (Lab, DeviceN with n >= 2): `PDV_GFX_COLOR` converts one
operand set while the module is resident, and the core caches the answer as an
`XCK_LUT` entry keyed by (the space's cache offset, a hash of the operands).
The header keeps the space's source (object number, or up to 480 bytes of
syntax) so the module can recompile it for `PDV_GFX_COLOR` without the core
holding a second copy.

**Image LUTs** (`PDV_GFX_LUT`, `XCK_LUT`). For an image whose space is not a
device space, the module builds `struct pdgx_lut`: `ncomp` (1..4) tables of 256
bytes. The grey of a pixel is the SATURATING SUM of `table[i][sample[i]]` over
the components, where a sample is the raw byte (after rcore unpacks 2, 4 and
16-bit samples to 8 bits). A 1-component space (Indexed, Separation, CalGray,
ICC N=1, DeviceN n=1) is exact. A 3-component space is the luminance weights
on each channel after the space's conversion (exact for RGB-like spaces,
a linearisation about mid-grey for Lab). A 4-component space is
`255 - (cx + my + yz + k)`-shaped per channel (the core's DeviceCMYK weights).
`/Decode` is folded into the tables, so rcore applies none.

**Functions** (`pdxgf.c`). Types 0 (sampled, 1 to 4 inputs, `/BitsPerSample` 1 to 32 - linear
interpolation in 1 and 2 inputs, nearest sample in 3 and 4; a table over 3,072
bytes is refused and the colour stays black with PDF_INC_COLOUR), 2 (exponential), 3 (stitching - its sub-function chain is
walked in a loop, not by recursion; depth at most 8) and 4 (PostScript
calculator: tokens compiled to bytes, an explicit operand stack of 100 values
and an explicit if/ifelse stack of 16 frames; every operator the reference
lists). A function is evaluated with `fn_eval(in[], out[])` and any failure
(a bad stream, a stack under- or overflow) makes the colour black and sets no
Incomplete bit.

**Shading** (`PDV_GFX_SHADE`) is drawn as FLAT POLYGONS (lead decision 23:52, option 1): the module answers
records and the core appends each as an ordinary `FILL` through the existing filler, so PDREND carries no shading
rasteriser. The request is `pdxb_gfx` with the resource (`PDGXI_RES | PDGXI_CAT_SH`, the Form or page, the name at
`tseg:toff`; or `objnum:gen` of a shading object), the CTM (`ctm`: user space to device quarter-pixels), the device
clip (`clip[4]`, pixels, inclusive), `steps` (bands across a gradient's range; 0 = 32), and the result buffer
(`rseg:roff`, `rcap`; the core uses 760 bytes of the exchange block). The answer is `ntri` records in `rlen` bytes
(`pdabi.h`, `PDGX_REC_*`): `r g b flags npts n1` then `npts` points of two `pdi` device quarter-pixels,
already through the CTM and clamped to +-16,000, filled non-zero. `n1` is the length of a first contour when a
second (the hole of a ring, wound the other way) follows. Flag bit 0 marks a CLIP record: the shading's `/BBox`,
sent first; the core clips to it and fills nothing. The core wraps the whole shading in `q`/`Q`.

* **Type 2** (axial): bands perpendicular to the axis, `steps` across the visible part of `[0, 1]` (cut to the
  cover rectangle), each the colour at its middle; `/Extend` adds one band to the clip edge. The geometry is done in
  the user space of the shading against the user-space rectangle that covers the clip (the inverse of the CTM on
  the clip's corners, two pixels out), so rotation and skew need no special case.
* **Type 3** (radial): when one circle lies inside the other (the usual case), exact RINGS between the circles
  of consecutive `s` (a polygon of M sides for each, M = 16, 24, 32 or 48 by the size on the device, its radius
  scaled so the polygon has the circle's AREA) and an extension as a disc or a ring out to a circle that covers the
  rectangle; otherwise (a cone) discs in increasing `s`, the later over the earlier, the ends extended in eight
  closer-and-closer discs until the circles leave the rectangle. Identical circles paint nothing.
* **Type 1** (function-based): an 8 x 8 grid of the domain, one flat quad a cell with the mean of its corners'
  colours.
* **Types 4 and 5** (triangle meshes): each triangle is cut into k x k flat sub-triangles, k = 1 + (the largest
  colour change along an edge) / 28, at most 5, and no finer than one a step of 8 device pixels; adjacent
  triangles of different k meet in a T-junction (a hairline at worst).
* **Types 6 and 7** (patches): the surface is evaluated on an n x n lattice, n = 2, 4 or 8 by the colour change
  across the patch, one flat quad a cell (separable Bernstein evaluation, a row of the lattice at a time).

The verb is RESUMABLE: when the buffer is full it answers `PDE_BUSY` with `state` = the units done (a unit is a band,
a ring, a row of grid cells, a triangle, a patch cell); the core calls again and the module re-parses, skipping those
units without evaluating a function for them. At most 6,000 units a shading; the rest is dropped and `kind` bit 0
is set (the core notes `PDF_INC_LIMIT`). `/Background` and `/AntiAlias` are ignored (`sh` ignores Background by
the reference); a shading pattern's `/ExtGState` is not applied.

**Tiling and shading patterns** (`PDV_GFX_PAT`). `scn /Name` in a `Pattern` space asks step 1 (the name as
`PDGXI_RES | PDGXI_CAT_PAT`): the module writes `struct pdgx_pat` (56 bytes: type, `PaintType`, the stream or
shading object, `/Matrix`, `/BBox`, `/XStep`, `/YStep`) as an `XCK_PAT` entry, and the gstate's slot names it. A fill
with such a gstate (`f F f* B B* b b*`, not text and not a stroke) asks step 2 (`PDGXI_TILES`; the entry at
`cseg:coff`, `ctm` = the page's default matrix, `clip`): the answer is `ctm` = the pattern matrix x the default
matrix (a tiling pattern's: that of its first tile), and for a tiling pattern `ncomp` x `kind` tiles (0 = too
many: more than `PDGX_TILEMAX` = 40 in a fill), `comp[0..3]` the device step of a tile along i and along j, and
the first tile's i, j. The core
* **shading pattern**: clips to the path, sets the CTM and paints the shading (above) with `objnum:gen` of the
  entry; a direct `/Shading` dictionary is refused with `PDF_INC_SHADE`;
* **tiling pattern**: pushes a form frame on the pattern's stream, with its own resources, clipped to the path
  and, per tile, to the `/BBox`; the frame ends once a tile and `frame_end` (the hook `XG_TILE_NEXT`) restarts it
  for the next, adding the step to the matrix, until the counts are done, when the saved state comes back. Inside a
  tile there is no pattern; a coloured pattern starts black, an uncoloured one keeps the colour `scn` gave
  (components first, taken as device Gray, RGB or CMYK). More than 40 tiles: the path is filled flat, mid-grey for a
  coloured pattern, and `PDF_INC_APPROX` is set. A pattern stroke, a pattern in text and a pattern inside a tile
  are not drawn as patterns. A fill with `B` strokes BEFORE the pattern (the frame runs after the operator).

**Verbs** (0xC8-0xCF; every one answers only when PDXG is the occupant):

| verb | name | XB / registers |
|---|---|---|
| 0xC8 | `PDV_GFX_CS` | `pdxb_gfx` in/out: compile; result in `rseg:roff` (`PDGX_CSBYTES` = 1,264) |
| 0xC9 | `PDV_GFX_COLOR` | `pdxb_gfx`: `cseg:coff` + `ncomp` operands in `comp[]` (0: the initial colour) -> `rgb[3]`, `rgb[3]` = 1 when nothing is painted |
| 0xCA | `PDV_GFX_LUT` | `pdxb_gfx`: image LUT in `rseg:roff` (`PDGX_LUTBYTES` = 1,028) |
| 0xCB | `PDV_GFX_SHADE` | `pdxb_gfx`: flat polygon records (above); resumable |
| 0xCC | `PDV_GFX_PAT` | `pdxb_gfx`: step 1 the pattern's parameters, step 2 its matrix and tiles |
| 0xCD | `PDV_GFX_FN` | `pdxb_gfx`: evaluate ONE function (`fn` text, `comp[]` in, `rgb`-free out `val[]`); for the host harness |
| 0xCE | `PDV_GFX_DASH` | rcore's `pdxdash.c` dash walker, `#include`d |
| 0xCF | `PDV_GFX_EDIT` | rcore's `pdxedit.c` edit-store marks, `#include`d |

Blend's ExtGState (`pdxgtr.c`, 0xD0-0xD7) and the two above are `#include`d by `pdxg.c` and dispatched by the
`PDX_OTHER_VERB(verb)` hook of its door (`PDX_LAST_VERB` is the highest verb it accepts); the colour code answers
`PDE_RANGE` for any other.

**Workspace** (for 20.22.2's table): `want` 13 KiB, `min` 13 KiB (12,302 bytes: the parse nodes, kids and
strings 4.8 KiB, sample data 3 KiB, calculator byte code 1 KiB, the function table 2.3 KiB and the Indexed table
1 KiB, laid out in `pdxgp.c`/`pdxgf.c`/`pdxgc.c` as `GW_*`; `GX_WS_KB` is the figure and `PDV_X_INIT` answers
`PDE_NOMEM` below it). The rest of the module's data is bss: 11.7 KB (the verbs' 1.3 KiB overlay, two 1 KiB
object windows, the calculator stack, the lattice rows). The module's stack is the private 2 KiB deep stack
(20.15), measured at 204 bytes for a colour and well under 1 KiB for a shading.

**Limits** (each is a table row, never a crash): 8 nested containers, 64 items a container, 8 reference hops,
8 colorants, 4 inputs to a sampled function, a sample table of at most 3,072 bytes, 24 vertices a lattice row,
4 colour values a mesh vertex, 9 compiled functions a request, 6,000 units a shading, 40 tiles a pattern fill.

**Size** (as built, 2026-11-10): see the report; PDXG image + bss against its 65,024 cap, and the core's share
(`pdrxg.c`) against its allowance, are measured there each time they change.

#### 20.22.10 Transparency through an 8-bit band (`blend`)

*Reserved; written by its agent before its code.*

#### 20.22.11 PDXI and JPEG (`dct`)

**PDXI** (`PDX_IMG`, magic `PDM_MAGIC_IMG`, `apps/pagedesk/pdxi.asm` + `pdxi.c`)
is the image-decoder occupant of the slot. It holds three codecs behind one
protocol: DCT (this section, `pdxidct.c`), CCITT (20.22.12, `pdxiccit.c`) and
JBIG2 (20.22.12, `pdxijb2.c`). Image + bss is at most 65,024 bytes, of which DCT
is at most 24,576 and bilevel at most 36,864 (measured: shell 3,158, DCT 22,528, bilevel 36,081 with both
of its codecs: 61,767 in all). A JBIG2 decode needs `g_wbytes` of 64,000, so its minimum slot is 64 KiB. The door is at `PDM_ENTRY` like
`pdwrt.asm`'s: AL = verb, BX/CX/DX its arguments, AX the answer, CF = 1 for a
failure. It answers `PDV_X_INIT` and `PDV_IMG_OPEN/ROW/CLOSE` only; any other
verb is `PDE_RANGE`. It reaches PDENG through the far vector `pdxb_init`
carried (`eng_off/eng_seg`), and the kernel through `pdkgate.inc` (it makes no
kernel call today).

**The IMG protocol as PDXI implements it.**

* `PDV_X_INIT` (BX = XB): reads `pdxb_init`, keeps `xb_seg`, the PDENG vector
  and `slot_seg/slot_kb`, drops any open image and any warm state (below).
  Workspace = `min(slot_kb * 1024, 65,520)` bytes at `slot_seg:0`. A DCT image
  needs the whole of it only when it is progressive or multi-scan; a baseline
  image needs one MCU row of output planes (at most 20 KiB).
* `PDV_IMG_OPEN` (XB = `pdxb_img`). `codec` selects the arm (`PDCD_DCT` here);
  an unknown or absent codec is `PDE_FILTER`. The module copies the request out
  of the XB, because `STRM_OPEN` needs the XB for `pdxb_strm`, opens the coded
  bytes (`objnum/gen` through `PDV_ENG_STRM_OPEN` with `accept = PDSA_CODEC`,
  whose `codec` must come back equal to the request's, else `PDE_FILTER`; or
  the inline `iseg:ioff`, `ilen` bytes), parses the markers, chooses the scale
  and fills `out_w out_h out_mode out_ncomp rowbytes scale` in the XB. A
  progressive (or multi-scan) image is entropy-decoded completely in OPEN, so
  OPEN is where its time goes; a baseline image decodes lazily in ROW.
* `PDV_IMG_ROW` (BX = dest seg, CX = dest off, DX = row): writes `rowbytes`
  bytes. A baseline image answers rows in ascending order only (a lower row is
  `PDE_RANGE`: the caller OPENs again) and skips forward by entropy-decoding the
  MCU rows between without transforming them. A progressive image answers any
  row, because its coefficients are all in the workspace. A row at or past
  `out_h` is `PDE_RANGE`.
* `PDV_IMG_CLOSE` closes the coded stream. A fully decoded progressive image
  stays in the workspace as **warm state**: an OPEN with the same
  `objnum gen w h want_w want_h flags` and the same inline address as the warm
  state, with no `PDV_X_INIT` between, reuses the coefficients and costs
  nothing, so a page drawn in bands does not re-decode a progressive image per
  band. Anything else evicts it.

**The reduction rule.** For `s` = 3, 2, 1, 0 (1/8, 1/4, 1/2, 1/1) the reduced
size is `ceil(w / 2^s)` by `ceil(h / 2^s)`. The module takes the largest `s` for
which both are at least `want_w` and `want_h` (`want = 0` means 1/1). It then
takes the smallest `s` at least that one for which (a) `rowbytes <= 1,024` and (b)
the workspace holds the image's planes and coefficients. So a result smaller than
`want` happens only when it must: 3 or 4 interleaved samples a pixel, or a
progressive image too big for the workspace. A source whose own reduced size
cannot fit even at 1/8 (more than about 8,000 pixels across, or a progressive
image whose DC coefficients at 1/8 need more than the workspace, about 32,000
blocks of one component) is refused with `PDE_LIMIT` before any row is read;
PDREND draws it as an image it cannot show. Reduction is **not** Incomplete
(L5).
At 1/8 only the DC coefficient is used, with no transform: a progressive image
at 1/8 skips every AC scan without decoding it. At 1/4 and 1/2 the module uses
libjpeg's reduced transforms (2x2 and 4x4 outputs from the low-order
coefficients), so a progressive image keeps 4 or 16 coefficients a block, plus a
64-bit "nonzero" mask a block that the refinement scans' run lengths need. When the
masks do not fit but the masked-free blocks do, the decoder takes the reduction without
them and SKIPS the AC refinement scans (a kept coefficient has its first-scan precision, at
most one quantiser step off, in the few coefficients kept): a 640x480 progressive
photograph is then 160x120 at 1/4, not 80x60 at 1/8. This is a quality loss within L5's
reduction, not an Incomplete bit.

**What is accepted.** SOF0, SOF1 and SOF2 (baseline, extended sequential,
progressive), Huffman coded, 8-bit precision, one to four components, any
sampling factors from 1 to 4 whose ratios to the largest are whole numbers,
restart markers (`DRI`, `RSTn`), 8- and 16-bit quantisation tables, any
number of scans. Anything else (arithmetic, lossless, hierarchical, 12-bit,
fractional sampling ratios, a missing table) is `PDE_FILTER`. Damaged entropy
data is not an error: the decoder feeds zero bits after the marker it meets,
resynchronises at the next `RSTn`, and finishes with whatever it has, as
libjpeg does. A component count other than 1, 3 or 4 is `PDE_FILTER`.

**The output.** The colour transform follows libjpeg: three components are
YCbCr (a JFIF marker, or Adobe `transform = 1`, or no marker and the ids are not
`R G B`) or RGB (Adobe `transform = 0`, or ids `R G B`); four components are CMYK
(no Adobe marker, or `transform = 0`) or YCCK (`transform = 2`).

* **Grey** (`PDIM_GREY8`, `rowbytes = out_w`) is the default: one component
  gives its samples; YCbCr gives Y alone, **with no chroma transformed**; RGB
  gives `(77 R + 150 G + 29 B) >> 8`; CMYK and YCCK give the luma of the
  `(255 - C)(255 - K)/255` form, for `PDIMG_INVERT` set (Adobe's inverted
  samples) or not. For one-component and RGB images `PDIMG_INVERT` also inverts
  the grey.
* **Native** (`PDIM_N8`, `rowbytes = out_w * out_ncomp`) when the caller sets
  `PDIMG_NATIVE` (0x0004, defined in the img block of `pdabi.h`): the image's
  `/ColorSpace` is not a device space, so the core maps the samples through its
  colour-space LUTs. `out_ncomp` is 1 (grey), 3 (RGB, after the YCbCr
  transform) or 4 (CMYK, after the YCCK transform, in the polarity the file
  stores: Adobe files are inverted and the core applies `/Decode`). Chroma is
  replicated (a box), and the RGB conversion is libjpeg's to within one level.
* `PDIM_BITS1` is not produced.

**The workspace** (declared here, for 20.22.2's table): **64 KiB**
(`slot_kb = 64`), the most an occupant may have; the top 1,024 bytes are the
landing zone the engine's `STRM_READ` writes the coded bytes to (it writes far),
and `pdi_src_get` copies them to a near buffer. The module's near memory is its
image + bss.

| use | bytes |
|---|---|
| near: Huffman tables (2 DC + 2 AC, 838 each), quantisation (4 x 128), entropy buffer 1,420, raw buffer 512, row buffers 2,112, block and transform work arrays about 1,200 | 8,826 (the module's whole bss is 8,980) |
| far, baseline (one scan holding every component): the output planes of ONE MCU row, `padded width * rows` for each component used | at most 1,056 x 32 (a 1/1 decode of a 4:1:1-or-wider image); 17 KiB for 4:2:0 |
| far, progressive or multi-scan: per used component `blocks * stride` plus the planes of one MCU row; stride = 128 (1/1), 40 (1/2), 16 (1/4), 2 (1/8) bytes, or 32 (1/2) and 8 (1/4) without the mask | up to 62 KiB |

Huffman table ids 2 and 3 (a file with four tables of a class) are `PDE_FILTER`;
cjpeg, libjpeg's progressive scripts, Photoshop and the camera makers all use 0
and 1. An unused component (grey output of a YCbCr image) has no storage at all.
**Workspace range (want, min).** `want` 64 KiB; the module works in whatever `slot_kb` it is given and
coarsens instead of refusing. Measured on a 2,400 x 1,800 colour 4:2:0 photograph drawn at 600 x 450
(`PDI_KB` in the host harness; the landing zone takes the first 1 KiB of any grant):

| image | slot_kb | what it decodes |
|---|---|---|
| baseline | 4 | 1/4, 600 x 450, as asked (one MCU row of planes) |
| baseline | 3 | 1/8, 300 x 225 (the 1/4 plane row no longer fits) |
| progressive, grey out | 48 to 64 | 1/8 with one block in two each way: 150 x 113 |
| progressive, grey out | 12 to 32 | one block in four each way: 75 x 57 |
| progressive, grey out | under 12 | `PDE_LIMIT` |

So the DCT minimum is **4 KiB** for a baseline photograph and **12 KiB** for a progressive one. A
progressive image is coefficients: 2,400 x 1,800 has 67,500 luma blocks, 2 bytes each at 1/8 is 132 KiB,
which no slot holds, so at 1/8 the decoder KEEPS ONE BLOCK IN 2 (then 1 in 4) each way, parsing and
discarding the rest (the DC predictor still moves), and the rows it delivers are the kept blocks'
DC values: exactly djpeg's 1/8 picture sampled every 2nd or 4th pixel (max difference 0). Only a grey
single-plane image takes this path; a progressive colour image in `PDIM_N8`, or whose grey needs all
three planes, over the slot at 1/8 answers `PDE_LIMIT` and PDREND draws the grey box with
`PDF_INC_APPROX`. A width over 8,100 is also `PDE_LIMIT`.

**Fidelity.** Against `djpeg -dct int` (libjpeg-turbo 3.2.0, `tools/os88pdfx_dct.py`
over 30 fixtures, 228 comparisons):

* every **grey** decode of a baseline image is bit-exact (max 0) at 1/1, 1/2, 1/4 and
  1/8, because the transforms are `jidctint`, `jidctred`'s 4x4 and 2x2, and the
  1x1 is the same dequantise and shift;
* a **progressive or multi-scan** image is bit-exact at 1/1 and 1/8; at 1/2 and
  1/4 the coefficients outside the low 4x4 (resp. 2x2) are not kept, whereas
  libjpeg's reduced transforms read all but one column, so the result is an
  approximation: mean absolute difference below 3.0 levels, maximum 36, on fixtures
  made to be hard (hard edges, noise);
* **PDIM_N8** is libjpeg's to within one level at 1/1 (the YCbCr conversion is 16-bit
  fixed point); at a reduction chroma is the luma's reduction replicated, where
  libjpeg decodes chroma at a larger reduction, so only its luma is held to
  libjpeg (mean below 12 on subsampled images, below 3 otherwise);
* CMYK and YCCK (Adobe) match libjpeg-turbo's own CMYK-to-RGB to within 3.

**The assembly** is `pdxidct.inc`: `pdi_hsym`, `pdi_bits`, `pdi_block`, `pdi_idct`.
`hosttest/pdi_asm.py` runs it under Unicorn's real-mode x86 against the C twins
in `pdxitwin.c` (300 transforms, 300 blocks over random canonical Huffman tables
and the real tables of a cjpeg file, 64 bit reads): 0 mismatches. The twins are
the oracle that the fast paths (a block with no AC term is one flat value; a row
whose other seven inputs are 0 is flat) are held to.

**Cost** (`hosttest/pdi_cycles.py`: instruction counts under Unicorn, priced by
the 8088 timing tables with the 4.34-clocks-a-byte fetch floor; a model, not a
measurement on iron):

| | 8088 clocks | at 4.77 MHz |
|---|---|---|
| IDCT 8x8, a real photographic block | 120,000 | 25 ms |
| IDCT 4x4 (1/2) | 57,000 | 12 ms |
| IDCT 2x2 (1/4) | 27,000 | 5.6 ms |
| any IDCT of a block with no AC term | 1,800 to 2,800 | 0.4 to 0.6 ms |
| 1/8: DC only, no transform | not priced: a far poke and a clamp, small beside the 4 ms of entropy decoding | |
| baseline block, Huffman and dezigzag, 12 coefficients | 19,000 (2,500 to 41,000) | 4 ms |

A 4:2:0 MCU row of 640 pixels (40 MCUs, 6 blocks each, grey out, so 4 transforms an
MCU) costs about 5.0 s at 1/1, 2.8 s at 1/2, 1.9 s at 1/4 and 1.0 s at 1/8,
nearly all of it the entropy decoder at the reductions: a 640x480 photograph at 1/2 is
30 rows, about 87 s. The module does not hide this; it is the price of reading
every block, and the reason the reduction rule exists. The transform is one
`imul` per multiply (about 150 clocks), 12 a 1-D pass, 16 passes.

**Robustness.** `hosttest/pdi_fuzz.c` (ASan + UBSan) mutates the 30 fixtures
(bit flips, forged markers and lengths, rewritten header bytes, truncation) and
drives X_INIT, OPEN, every ROW and CLOSE, with a random workspace size, random
want and flags, the coded bytes in short chunks, and guard bands around the
workspace and the destination row. It found one bug (the Huffman lookup filled
before an over-subscribed length was refused) and then ran 30,000 mutations clean.
The bit reader reads only inside a zero-padded buffer; the entropy buffer is
refilled before every block.

**Chains** (`os88pdstk`, PDXI added as a segment with its far edge
`_pdi_engcall -> PDENG`): the deepest chain is 566 bytes of the 1,776, and 478 of
those are PDENG's own below `pde_entry`; PDXI's frames above it are 88 bytes. The
module makes no kernel call of its own (the stream is read through PDENG).

#### 20.22.12 CCITT and JBIG2 (`bilevel`)

*Reserved; written by its agent before its code.*

#### 20.22.13 JPEG 2000 (`jpx`)

PDXJ (`PDM_MAGIC_JPX`, slot id `PDX_JPX`) decodes a `/JPXDecode` image at a
REDUCED RESOLUTION LEVEL (owner decision O1): the screen never needs more
device pixels than the image covers, so the highest wavelet levels are
discarded and only the code-blocks of the resolutions that are kept are
decoded. The files are `apps/pagedesk/pdxj.asm` (the door at `PDM_ENTRY`,
`pdwrt.asm`'s shape, and the assembly inner loops), `pdxj.c` (the verbs; it
`#include`s `pdxjhdr.c` markers and geometry, `pdxjpkt.c` tier-2, `pdxjt1.c`
tier-1, `pdxjdwt.c` the transform driver), `pdxjtw.c` (the host twins of the
assembly) and `tools/os88pdfx_jpx.py` (fixtures, `tests/pdfany/jpx/`).

**Verbs** (`pdabi.h`): `PDV_X_INIT` (`slot_seg/slot_kb` are the workspace,
`eng_off/eng_seg` PDENG's door), `PDV_IMG_OPEN`, `PDV_IMG_ROW` and
`PDV_IMG_CLOSE`, the protocol 20.22.11 pins for PDXI. `0xEE-0xEF` are spare.
`IMG_OPEN` reads the headers, decides the reduction, **and nothing is decoded
until the first `IMG_ROW`**, which decodes the first tile row (every tile that
touches it). `out_w`/`out_h` are final at `OPEN`. A lower row than the last
asked is `PDE_RANGE`: the caller re-OPENs, as 20.22.11 says. `IMG_CLOSE` closes
the PDENG stream and forgets everything; an `OPEN` while one is open closes it
first. The module keeps no state between a CLOSE and the next OPEN.

*The coded bytes* come from PDENG (`PDV_ENG_STRM_OPEN` with `accept =
PDSA_CODEC`; the answer must say `codec == PDCD_JPX`, else `PDE_FILTER`) read
FORWARD ONLY with `PDV_ENG_STRM_READ` into a 512-byte buffer in the module's own
DS, or, for an inline image (`objnum == 0`), from the far buffer
`iseg:ioff`, `ilen` bytes. Both are one function pair (`src_open`, `src_get`),
so the decoder never knows which. A restart is a second `STRM_OPEN`.

**Containers.** A JP2 file (signature box `0000000C 6A502020`) is walked box by
box; `jp2c` starts the codestream, `jp2h` is read for `ihdr`/`cdef` only, and
every other box is skipped by length (a box length of 0 = to the end; 1 = the
64-bit form, whose high 32 bits must be 0). A bare codestream starts `FF4F`.
A `pclr` (palette) or `cmap` box is REFUSED: the colour the PDF asks for is
ahead of the palette, and mapping through it is the colour module's job
(20.22.9), not this one's.

##### 20.22.13.1 What is decoded, and what is refused

A refusal answers `IMG_OPEN` with `PDE_FILTER` (CF = 1) and writes a reason
code, `PDJ_R_*` below, to `pdxb_img.out_w` (every other out field is
unspecified). PDREND draws no image and notes `PDF_INC_FILTER`, which the user
reads as *Incomplete rendering: image* (the same line a DCT image that PDXI
refuses gets). Nothing is silent.

| supported | |
|---|---|
| containers | bare codestream; JP2 with `jp2c` anywhere after the signature |
| markers | SOC SIZ COD COC QCD QCC SOT SOD EOC; TLM PLM PLT CRG COM are skipped |
| geometry | origin 0 (`XOsiz = YOsiz = XTOsiz = YTOsiz = 0`); each of Xsiz, Ysiz, XTsiz, YTsiz at most 32767; one or many tiles in raster order, each tile's tile-parts contiguous; `Csiz` at most 16 |
| components | `XRsiz = YRsiz = 1` for every component; depth 1 to 12 bits, signed or unsigned |
| decomposition | NL 0 to 12; with more than one tile, XTsiz and YTsiz multiples of 2^NL (so every tile origin is even at every level and one parity rule serves) |
| code-blocks | 4x4 to 64x64 (`xcb + ycb <= 12`), styles RESET, VSC, SEGSYM; PTERM is ignored (it only constrains the encoder) |
| precincts | the default (2^15) and any user list; at most 768 precincts across the components of a tile |
| layers | 1 to 255 |
| progressions | LRCP RLCP RPCL PCRL CPRL; SOP and EPH markers present or not |
| wavelets | 9/7 irreversible and 5/3 reversible |
| quantisation | none, scalar derived, scalar expounded; guard bits 0 to 7 |
| transform | the multiple component transform, RCT or ICT, is honoured by NOT undoing it (see 20.22.13.6) |

| refused (`PDJ_R_*`) | why |
|---|---|
| 1 `NOTJPX` | neither signature found |
| 2 `TRUNC` | the main header is cut off, or SIZ is not first |
| 3 `GEOM` | a nonzero origin; an axis over 32767; tile size not a multiple of 2^NL with several tiles; a subsampled component; Csiz over 16 |
| 4 `DEPTH` | precision over 12 bits, or a QCD that leaves a band with no magnitude bit (`guard + eps < 1`). A code-block whose bit-planes (`Mb - zero bit-planes`) exceed 15 decodes as zero, quietly, like an arena overflow |
| 5 `STYLE` | code-block style BYPASS (lazy) or TERMALL, which put several codeword segments in a code-block and so lengths in the packet header this module does not parse |
| 6 `ROI` | an RGN marker |
| 7 `PACKED` | PPM or PPT packed packet headers, or a POC progression change |
| 8 `PERTILE` | a COD, COC, QCD or QCC in a tile-part header, or a COC or QCC in the main header that differs from COD/QCD for ANY component (the geometry of a skipped component still decides where the next packet starts) |
| 9 `PALETTE` | a `pclr` or `cmap` box |
| 10 `PART2` | Rsiz bit 15 (Part-2 extensions) |
| 11 `ORDER` | tiles out of raster order, or a tile's tile-parts interleaved with another's |
| 12 `LIMIT` | more than 768 precincts in a tile; more than 255 layers; a precinct with more than 384 kept code-blocks in a packet at every reduction up to the limit; a code-block segment over 65535 bytes |
| 13 `MEM` | not even the lowest resolution fits the workspace (20.22.13.3); answered `PDE_LIMIT`, the others `PDE_FILTER` |
| 14 `SOURCE` | the stream could not be opened, or ends before the first tile |
| 15 `NOALPHA` | `PDJ_ALPHA` asked and the file has no alpha channel (20.22.13.7) |

A stream that is CUT OFF after the first tile's first packet is NOT an error:
the packets that are present are decoded and the rest are zero (a truncated
file shows what it has). Every count, length and index read from the file is
bounded before use; the parsers have an ASan/UBSan fuzz target (20.22.13.9).

##### 20.22.13.2 The reduction rule

Let `NL` be the file's decomposition levels, `W x H` the image (Xsiz, Ysiz),
`want_w x want_h` the device pixels the image covers (0 = the image's own
size). **`scale = r` is the smallest `r` in `0..NL` that satisfies BOTH:**

1. *quality*: `r >= rq`, where `rq` is the largest `r <= NL` with
   `ceil(W/2^r) >= want_w` and `ceil(H/2^r) >= want_h` (0 when even `r = 0`
   is short);
2. *machine*: the reduced image is at most 1024 pixels wide and tall, its
   row (`out_w`, times `out_ncomp` for PDIM_N8) is at most 1024 bytes, and the
   whole plan fits the workspace (20.22.13.3).

`out_w = ceil(W/2^r)`, `out_h = ceil(H/2^r)`. If no `r <= NL` satisfies 2 the
answer is `PDJ_R_MEM`. A reduced decode is NOT an incomplete page (L5); the
caller scales it to the device rectangle as it scales any image, with the
nearest-pixel row rule of 20.22.7, so a 4x reduction shows blocks four device
pixels wide.

##### 20.22.13.3 Memory: the workspace and the module's bss

The module's image + bss is at most **65,024**. Its workspace is `slot_kb`
KiB, **60 KiB** (the figure for 20.22.2's table), addressed with 16-bit offsets
inside the one segment `slot_seg`. Nothing the module reads in a loop lives in
the workspace except through the far movers or the assembly routines, which take
a segment.

| bss (always) | bytes |
|---|---|
| tier-1 flags, 66 x 66 words (the horizontal transform's interleave buffer, 2,048, overlays it) | 8,712 |
| scratch, 64 x 64 words: the tier-1 magnitudes; the packet's code-block list (3 x 384 words) and the horizontal transform's E and O rows (2 x 514 words) overlay it | 8,192 |
| one code-block's coded bytes (a longer one is cut: fewer passes; `JX_CBDATA` 4,096) | 4,098 |
| tier-1 context tables and the MQ state | about 1,100 |
| row map of the tile-component, 1024 words | 2,048 |
| precinct table, 768 entries x 4 (state offset, record base) | 3,072 |
| source buffer; depth-below-8 sample tables (4 x 128) | 512; 512 |
| scalars, geometry, the tile and component records | about 1,400 |

The tier-1 arrays are the decoder's inner loop, so they stay in the module's own
segment and are reached with near pointers; moving them into the workspace would
make every flag and magnitude access a far call (about three times the 74 s of
20.22.13.10) and would not shrink the slot, which is image + bss + workspace
whichever side holds them. The saving is taken by overlay instead: three arrays
that are never live at once share the scratch array (6,148 bytes) and one
the tier-1 flags, and the code-block buffer is halved (4,096).

*Measured:* **image 22,514 + bss 29,607 = 52,121 bytes** against the cap of 65,024
(was 62,495), `make build/PDXJ.MOD`.

**Workspace range (want, min) = (60 KiB, 12 KiB).** `PDV_X_INIT` answers `PDE_NOMEM`
below 12 KiB. `plan(r)` already walks `r` upward until the regions fit, so a smaller
workspace costs resolution, not correctness: a 1,600 x 1,200 JPEG 2000 (grey, and
three-component at the same size, 38-48 KB coded) decodes at **r5 (50 x 38) from 12
KiB**, **r4 (100 x 75) from about 24 KiB** and what the want asked for from 60 KiB
(measured with `PDXJ_KB`). Below what an image needs at its coarsest level `open`
answers `PDE_LIMIT` (refusal 13 `MEM`), and the renderer draws the grey box with
APPROX. **`PDE_NEEDMEM`.** The level `open` would choose is the first `r` from the reduction rule
for which `plan(r)` fits at all (workspace unlimited, at most 63 KiB). When that needs
more KiB than `slot_kb`, `open` closes the source and answers `PDE_NEEDMEM`, CF = 0,
`CX = PDMEM_SLOT` (0), `DX` = the KiB it needs (the arena at its 4,096-byte floor). The
primary regrows the slot, re-INITs and calls `IMG_OPEN` again. If it could not, the
re-INIT carries the same `slot_kb`, and the second call does not ask again: it takes
the coarsest-fitting level as before, or `PDE_LIMIT` when none fits (the grey box). The
ask is made once per image (`IMG_CLOSE` or any final answer clears it). Measured
(`PDXJ_GROW=1 pdxj_test`): 1,600 x 1,200 grey asks 22 KiB at 12 or 16 KiB and decodes
r5 if refused, r4 once grown; at 24 KiB or more it never asks.

The slot a 640 KiB XT must find is therefore 52.1 + 12 = **about 64 KiB**
at the least and 112 KiB at the want.

| workspace, in this order (`plan(r)` computes the sizes) | bytes |
|---|---|
| persistent packet state: one byte of Lblock and inclusion per code-block, and a 2-byte node for each tag-tree node of two trees; only when there is more than one layer. With one layer a packet builds its trees afresh in a temporary at the start of this region, sized for the biggest precinct-band | about 7 B a code-block |
| code-block records of the KEPT decoded components: head, tail, passes, zero bit-planes | 6 B a code-block |
| the coefficient plane of one tile-component at the kept resolution: `2 * tw * th` | 2 B a coefficient |
| the output strip(s): present when there are several tiles across, or more than one decoded component: one byte a pixel, `out_w x tile-row height`, per decoded component | 1 B a pixel |
| the ARENA: the coded bytes of the kept code-blocks of the tile, in segments of `next:2 len:2 data` | the rest, at least 4,096 |

`plan(r)` is called for `r = rq, rq+1, ...` with the geometry of tile (0,0), the
largest, and accepts the first `r` for which every region is non-empty and the
arena is at least 4,096 bytes. **A tile whose kept coded bytes exceed the arena
loses the code-blocks that arrive after it is full** (they decode as zero: the
tile is blurrier, never absent); lower resolutions come first in every
resolution-major order and early layers first in LRCP, so what is lost is the
detail the reduction was about to discard anyway. This is a graceful, not a
reported, degradation (L5: it is the same class as a reduced decode).

*What 60 KiB buys, honestly.* The plane is 2 bytes a coefficient, so a one-component
image with no strip and a 4 KiB arena reduces to about 28,000 pixels (170 x 165):
a scanned page covering a 640 x 480 window arrives at roughly one fifth of its
device width. That is the price of the 64 KiB workspace ceiling (L3) and it is
the lever to pull if a better picture is wanted: a second 64 KiB region for the
plane lifts it four-fold.

##### 20.22.13.4 The decode

Per tile, in the order the file gives them:

1. **Tile-part headers** (SOT, then markers to SOD). Psot = 0 means to EOC.
   The tile-parts of a tile are read as one byte stream (`td_byte`), so
   packets are parsed without regard to where a tile-part ends.
2. **Tier-2.** The packets are generated in the COD's progression order and
   each is parsed by `packet(comp, res, precinct, layer)`: SOP skipped, the
   header bits read with the 0xFF bit-stuffing rule, tag trees walked with an
   explicit 16-entry stack (no recursion), the number-of-passes code, Lblock,
   the length of each code-block's single codeword segment, EPH skipped. The
   body is then, per included code-block, either **copied into the arena**
   (a kept component at a kept resolution) or **skipped** by length. The
   header of a skipped packet is still parsed in full, because its length is
   known only from it and the tag-tree and Lblock state it leaves is needed by
   its precinct's next layer.
3. **Tier-1** for each kept code-block: its segments are joined in
   `cbdata`, then `P = Mb - zbp` bit-planes are decoded pass by pass (cleanup,
   then significance, refinement, cleanup, ...), `Mb = guard + eps_b - 1`, with
   the MQ decoder in assembly (`xj_mqdec`). A pass beyond the data decodes
   from the 0xFF fill the standard defines. Magnitudes keep one fractional
   bit (`v = 2B + 2^bp`: the reconstruction midpoint), so a lossless decode is
   `v >> 1`, exact.
4. **Dequantisation** into the 16-bit plane in fixed point. Reversible: `c =
   sign * (v >> 1)`, F = 0 fractional bits. Irreversible: `c = sign * v * m
   / 2^sh` with `m = 2048 + mu` and `sh = 12 + eps - Rb - F`, `Rb = depth + gain`
   (gain 0 for LL, 1 for HL and LH, 2 for HH), `F = clamp(11 - depth, 0, 3)`,
   saturated to the int16 range (`xj_deq`).
5. **The inverse wavelet** for `NL - r` levels, in place (Mallat layout), the
   horizontal pass first and then the vertical, as F.3.2 orders them. 9/7 in
   Q14 fixed point, the six lifting steps of F.3.8.2 in the order the standard
   gives (`K`, `1/K`, then delta, gamma, beta, alpha), each a 16x16 -> 32
   multiply and a rounded shift by 14 (`xj_lift`, `xj_scale`); 5/3 in integers
   (`xj_lift53`). Symmetric extension is done by CHOOSING the neighbour rows or
   padding the row arrays, never by a branch in the loop. The vertical pass
   works on whole rows through a row map, so the final interleave of rows is a
   permutation of map entries, not a copy (the map, `rowmap`, is why the plane
   height is limited to 1024).
6. **Components to grey** (20.22.13.6) and **to bytes**: `xj_grey`.

The inverse transforms use the level shift the standard does: the unsigned
DC offset `2^(depth-1)` is added at the end, in the grey conversion.

##### 20.22.13.5 Fixed-point arithmetic

No `float` and no `long` exist on the machine and none is used. The 9/7
constants in Q14 are `alpha = -25987`, `beta = -868`, `gamma = 14466`, `delta =
7266`, `K = 20155`, `1/K = 13318` (each `round(x * 2^14)`). A lifting step is

    dst[i] -= ((a[i] + b[i]) * k + 8192) >> 14        (k signed, sum in int16)

the sum fitting int16 because F <= 3 keeps every transient at most 2^13 for
the depths allowed. The differential harness measures the whole pipeline
against `opj_decompress -r` (20.22.13.8).

##### 20.22.13.6 Components to grey

* One component (or `PDIMG_ALPHA`): that component, `PDIM_GREY8`.
* Three or more components with the multiple component transform on (COD's
  SGcod byte 3 = 1): component 0 ONLY is decoded, because after the forward
  RCT or ICT component 0 IS the luminance Y (RCT: floor((R + 2G + B)/4); ICT:
  0.299 R + 0.587 G + 0.114 B). Components 1 and 2 cost their packet headers
  and nothing else. The result is `PDIM_GREY8`. (A colour JPX therefore comes out
  in grey, as every other RGB image does on these adapters, at a third of the
  work.)
* Two or more components with the transform off: `PDIM_N8`, `out_ncomp = n =
  min(pdxb_img.ncomp or Csiz, Csiz, 4)` components interleaved, each a byte in
  its own sample range scaled to 0..255, for the colour module to map (20.22.9).
  The row bytes are `n * out_w`, so `out_w <= 1024 / n` enters the reduction.
* The depth is scaled to 8 bits by a right shift (d > 8) or by the multiplier
  255 / (2^d - 1) (d < 8, so a 1-bit component is 0 or 255). `/Decode` is the
  caller's (PDIMG_INVERT is honoured here: v -> 255 - v on grey).

##### 20.22.13.7 `/SMaskInData` and the alpha channel

When the PDF asks for the codestream's own alpha, PDREND opens the same object
with `flags |= PDJ_ALPHA` (0x0100, this module's, `pdabi.h`'s block `[img]`)
and receives that channel alone as `PDIM_GREY8`, `PDIMG_MASK` semantics: 255 =
opaque. The channel is the one the `cdef` box marks Typ 1 (opacity) or 2
(premultiplied); with no `cdef`, component `Csiz - 1` when `Csiz` is 2 or 4.
With neither, `OPEN` answers `PDE_FILTER` with `PDJ_R_NOALPHA` (15). The colour
open (no `PDJ_ALPHA`) decodes component 0 as above whatever the file carries.

##### 20.22.13.8 Checks

`tools/os88pdfx_jpx.py` writes, deterministically, with `opj_compress`
(OpenJPEG 2.5.4, recorded in `tests/pdfany/jpx/README`): 45 fixtures - lossless
and lossy; 1, 2, 3 and 4 components; several tile sizes; all five progression
orders; 1 to 7 resolutions; code-block sizes 16, 32 and 64; layers; user
precincts; SOP and EPH; 12-bit samples; every supported code-block style; an
alpha channel; and the refusals (BYPASS, TERMALL, ROI, a tile size that is not a
multiple of 2^NL). Four checks, all host-side:

1. `hosttest/pdxj_cmp.py` runs the host build of the module (`pdxj_test`) at
   every reduction each file allows and compares with `opj_decompress -r <n>` as
   the maximum and mean absolute difference: 278 cases. Budgets: `max = 0` for
   lossless 5/3 without a transform; `max <= 3`, `mean <= 0.5` for 9/7;
   a file with the multiple component transform is compared on its Y (the
   reference is Y computed from the RGB `opj_decompress` clamped, so the budget
   is `max <= 12`, `mean <= 0.1` lossless and `mean <= 0.7` lossy); an alpha
   channel is compared exactly. Measured: every lossless non-MCT case is exact;
   9/7 cases are within max 1 (mean 0.04 to 0.23).
2. `hosttest/pdxj_asm.py` holds `pdxjasm.inc` to its twin `pdxjtw.c` bit for bit
   in Unicorn - MQ decoder through random streams and contexts, every lifting
   routine near and far, interleave, dequantise, grey - including the 16-bit wrap
   of intermediate sums. A mutation of either side fails it.
3. `hosttest/pdxj_uc.py` runs the REAL `PDXJ.MOD` (compiled by cc8086, SS != DS,
   far workspace, the door and its register contract) in Unicorn on every fixture
   at two reductions and compares every row with the host build: 86 cases, 0
   differences.
4. `hosttest/pdxj_fuzz.c` (ASan + UBSan; mutated fixtures - flipped, overwritten
   and inserted bytes, the header more often, truncation, a random workspace
   size - with guard bytes either side of the workspace and an alarm for hangs):
   34,000+ inputs, no report. A second build lowers `JX_ARENA_MIN` to reach the
   arena-full path.

##### 20.22.13.9 The stack, the gate and the cost

The module's door runs on the private deep stack (20.15); the budget is 1,776
bytes per verb, including the PDENG call that `src_get` makes. `tools/os88pdstk.py` (run with PDXJ added as a segment; the edges `PDXJ
_pdxj_engcall [_pdxj_engvec] -> PDENG pde_entry` and `PRIMARY _pdu_mcall [si] ->
PDXJ pdxj_entry` are the two this module adds) reports the worst chain through
`v_row`: **700 bytes**, of which the module's own frames to its `STRM_READ` call
are 276 and PDENG's decoder under it 424; `v_open` 546, `v_close` 478, `v_init`
46. The decode chain itself (`v_row > decode_trow > decode_tile > decode_comp >
t1_block > t1_cup > xj_mqdec`) is shorter. No recursion (a tag tree walks an
explicit 16-entry stack). The only kernel calls would be through `pdkgate.inc`;
none are made - the module reaches PDENG through its door, and its largest frame
is 74 bytes (cc8086's gate is 96).

*Cost on a 4.77 MHz 8088: 20.22.13.10.*

##### 20.22.13.10 Measured cost

Taken ONCE with `hosttest/pdxj_cost.py`: the real `PDXJ.MOD` in Unicorn with a
per-instruction hook, every executed instruction bucketed by the function that owns
it and priced with a coarse 8088 table (fetch floor 4 clocks a byte, register op 3,
memory operand 14-25, push 15, pop 12, call 23, mul/imul 130, shift by CL 8 + 4 a
bit). **An estimate good to about +-30%**; MartyPC and the 5150 are the arbiters.
Instruction counts are exact. Fixture: 160x120, one tile, 5 resolutions.

| decode | instructions | est. clocks | 4.77 MHz |
|---|---|---|---|
| lossless 5/3, full size (r0) | 21.6 M (g_ll, 160x120, measured) | ~352 M | ~74 s |
| lossless 5/3, r2 (40x30) | 1.55 M | ~25 M | 5.3 s |
| lossy 9/7, full size (r0) | 8.3 M | ~148 M | 31 s |
| lossy 9/7, r2 (40x30) | 1.12 M | ~19 M | 3.9 s |
| lossless, 157x113 r0 (noisy) | 19.8 M | ~323 M | 68 s |

*Where it goes.* Tier-1 is 70-95% of a lossless decode and 62% of a lossy one
(the transform is 31% of lossy full size and under 4% once the top levels are
dropped): **dropping levels removes tier-1 work with them, which is why the
reduction rule pays**. Tier-2 (packets) is under 3% anywhere; dequantise 2.5-4%;
the grey conversion about 1%.

* **Per MQ decision: 31 to 37 instructions, about 440 to 550 clocks** (the
  decoder 31-37, byte-in amortised). This is the unit of tier-1.
* **Per code-block pass** (significance, refinement or cleanup, MQ included)
  costs by the samples it visits and the decisions it takes: measured averages
  141 to 1,190 k clocks a pass on 64x64 and smaller blocks, i.e. roughly
  **35 to 290 clocks a sample a pass**; a noisy lossless block costs about 8x a
  smooth lossy one because it codes every sample in every plane.
* **Per wavelet level** (grey, 9/7, per stage, clocks): 0.66 M, 2.4 M, 8.8 M, 33.9 M
  for stages of 10x8, 20x15, 40x30, 80x60 = 80, 300, 1,200, 4,800 samples, i.e.
  about **7,000 clocks a sample of the stage's area** (four lifting steps per
  direction, two directions, a multiply per step); 5/3: 0.2 M, 0.6 M, 2.1 M,
  7.4 M for the same stages, **about 1,500 clocks a sample**, a quarter of 9/7.
  Each stage costs four times the one before it.

The consequence for the product: a scan that reduces to about 100x75 decodes in
tens of seconds on an XT, a 2x larger result takes four times the transform and
about three times the tier-1 time. PDREND runs it once per image per page view and
the result is cached by the caller (20.22.7). A faster machine scales as its
clock; nothing here is memory-bound.
