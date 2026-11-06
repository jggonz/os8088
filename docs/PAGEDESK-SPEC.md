# PAGEDESK-SPEC - PageDesk, a native PDF viewer and editor

PageDesk is a PDF reader and annotator for os8088 that runs natively on the
4.77 MHz 8088 XT with 640 KiB and on VGA, CGA and Hercules. This document is
its binding contract, outside SPEC.md on the C64-SPEC/WEAVE-SPEC precedent:
the profile, the module ABI, the numeric and geometry formats and the on-disk
representations are pinned here **before** product code, and a change to any
of them is a change to this file first. SPEC.md 127 is the anchor that points
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
| +8 | the door: `call far` with AL = verb, BX, CX, DX = arguments; answer AX (and DX where a verb says so), CF = 1 on failure with AX = a `PDE_*` code |

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

When the DL fills, the interpreter stops at an operator boundary, the
rasteriser draws the bands for what was interpreted, the DL is reset and the
interpreter resumes **from the stream position and graphics state it stopped
at** for the next pass, with the earlier passes' output kept on screen (each
pass paints over the same bands in content order, so the result is the
correct painter's order). Nothing is dropped.

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
lines and text are black at luminance < 50% and white otherwise. No alpha.

### 10.6 Incomplete rendering

The interpreter records every unsupported operator, resource or feature it
skipped (a bitmask plus the first name). A page with any record shows
**Incomplete rendering - <feature>** in the status line, and destructive
operations that depend on what was skipped (Delete text box over it is fine;
"cover" and extraction of that page are not) say why they are greyed.

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

### 14.3 Finalisation

`PDSAVE.$$$` -> (target exists: confirm, delete target) -> rename. The
filesystem has no atomic replace (SPEC.md 18.4), so the documented guarantee
is: a failure before the delete leaves the target untouched; a failure
between delete and rename leaves the complete new file as `PDSAVE.$$$`, which
PageDesk offers to finish renaming at the next launch. Disk full, write
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

`SYSTEM/APPDATA/PAGEDESK.JRN` (when the boot volume is writable): a header
with the source's name and fingerprint, then append-only records (one per
completed command, CRC-16 each). Recovery is offered at open only when the
named source's fingerprint matches; replay stops at the first bad record.
A read-only volume disables the journal with a status-line note.

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

### 16.2 Claims

| claim | owner | size | when |
|---|---|---|---|
| primary image + bss | loader | <= 56 KiB | launch |
| `PDENG` image + bss | parts | <= 40 KiB measured target | launch |
| `PDREND` image + bss | parts | <= 40 KiB measured target | launch |
| exchange block | primary | 1 KiB | launch |
| block cache | PDENG | 16 KiB | open |
| object arena + xref tables + page table | PDENG | 8 + 3 + 8 KiB | open |
| display list + gstate | PDREND | 20 + 2 KiB | open |
| band | PDREND | 1.25-5 KiB by adapter | open |
| glyph cache | PDREND | 12 KiB | open |
| thumbnails | primary | 4 KiB | panel shown |
| edit store + undo | primary | 16 KiB | open |
| text/search scratch | primary | 2 KiB | search |
| `PDWRT` image + bss | parts, lazy | <= 40 KiB | save/extract/new, freed after |
| writer buffers | PDWRT | 8 KiB | save, freed after |
| overlay | parts, lazy | <= 24 KiB | dialog, freed when idle |

Failure of an optional claim (thumbnails, a larger cache) degrades quality;
failure of a required one refuses the open with the KB it wanted.

## 17. User interface

### 17.1 Layouts

The layout is chosen from the live content box (`OSAPI_WM_GEOM`), never from
the adapter kind:

| layout | when | rows of chrome | panels |
|---|---|---|---|
| FULL | width >= 600 and height >= 300 | toolbar + mode/tool bar + status | Pages/Outline sidebar and Notes & edits panel, each collapsible |
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

### 20.3 The renderer (`rend`)

### 20.4 Fonts, widths and art (`fonts`)

### 20.5 The writer (`wrt`)

### 20.6 The package and its interface (`shell`)

### 20.7 Arithmetic and the assembly contract (`math`)
