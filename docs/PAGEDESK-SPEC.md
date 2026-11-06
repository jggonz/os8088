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
| display list + gstate | PDREND | 20 + 2 KiB | open |
| band | PDREND | 1.25-5 KiB by adapter | open |
| glyph cache | PDREND | 12 KiB | open |
| thumbnails | primary | 4 KiB | panel shown |
| edit store + undo | primary | 16 KiB | open |
| text/search scratch | primary | 2 KiB | search |
| `PDWRT` image + bss | parts, lazy | <= 40 KiB | save/extract/new, freed after |
| writer buffers | PDWRT | 12 KiB (4 KiB output + 1,024-object offset table); 36 KiB for an extract of up to 4,096 objects | save/extract, freed after |
| overlay | parts, lazy | <= 24 KiB | dialog, freed when idle |

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
| B | display list 22 + PDREND's gstate/font-cache workspace 7 (D13: `dl_kb` = 29), band (`((w+1)/2)*16` bytes at 4bpp, `((w+7)/8)*16` at 1bpp, to a whole KiB) | 30-34 KiB |

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
claim the writer's buffers (12 KiB, D9), INIT it, step `PDV_WRT_SAVE`; on any
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
