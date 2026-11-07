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
32 subsection records; the 33rd merges adjacent pairs into CHECKPOINTS (a
record that stands for `span` consecutive subsections, found again by reading
their 20-byte-aligned headers off the file). A lookup in a thinned revision
takes the last record whose first object is at most the one asked for and
walks at most `span` headers. The whole chain may name at most `PD_MAXSUBSEC`
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
| `PDE_LIMIT` | token/name/array limits; more than 8 revisions or 1,024 pages; an object number over 65,535; a subsection past object 65,535; a revision of 33+ subsections out of order; a file of 2 GiB; 5 filters; 7 open streams; 1 MiB decoded; 4 MiB of filter work |
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
vanishes. Beziers are flattened by de Casteljau subdivision until both second
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
stencil** is solid, black below luminance 128, white above. 4bpp fills use the
index; 4bpp greys in images are dithered between the four greys.

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
| `ABORT` | `PDE_OK`; closes every stream |
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
part of a path rather than wrapping; 4bpp fills lighter than the palette's light
grey round to white (10.5); a
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
document claims (D2). A refused header toasts `<MODULE> is the wrong version.`
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

1. **Insert from another PDF** (13.8) is a greyed-in-words command: the map has
   the `PDM_FOREIGN` kind and the writer refuses it (`err_detail` 14). Design: a
   merge job that runs the Extract pipeline once per source document, numbering
   from the previous round and writing the page tree last; it needs PDENG to
   switch documents inside the job and so the 20.11.6 restructuring.
2. **The recovery journal and the `PDSAVE.$$$` offer** (14.7, 14.3) are not built.
3. Undo/redo of a page operation needs edit (20.11.1); the dirty flag is
   `pdu_changes`.
4. `pdu_side_sel()` (nav) is a commented line in `ovl_pg_tgt`: the command's page
   is the current one until nav lands.
5. Sizes at this writing: PDWRT 45,194 bytes (44.1 KiB; D18's cap is 46 KiB,
   `PDW_CAP` = 47,104, target 44: the page-op, edit and map tables went to the
   claim's tail, the Extract bit maps are sized from `/Size` inside the claim,
   the inherited-resources buffer shares the annotation text buffer and the
   verify scratch the kept-identity buffer). The primary 42,484 + 4,057 = 46,541
   against the wave-1 shell's 44,623: +1,918 against a +1.5 KiB share. What is
   left is the overlay's cost, not the commands: ~280 bytes of shims and vectors
   for the resident routines the overlay calls (shared with any later overlay
   tenant), the overlay's literals (which stay resident, 73.14), the nine-row menu
   table and the resident lookups. The overlay holds every command body: 6,171
   bytes, pages' share about +3.9 KiB.
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
two slots when the largest free run holds B, both slots and the 32 KiB reserve,
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
