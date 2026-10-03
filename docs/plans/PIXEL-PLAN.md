# PIXEL-PLAN: PiXEL, an image viewer and editor for os8088

Status: DESIGN RECORD, written before any code. Branch `app/pixel`, worktree
`/tmp/pixel`. Contract: SPEC.md §106 (written wave by wave, before each wave's
code). Authority order when two documents disagree: SPEC.md §106 > this plan >
the research notes it was built from.

The owner's brief, in one paragraph: an image viewer and editor named
**PiXEL**, laid out like the PixelStudio mock-up (toolbar, tool column, big
canvas, Navigator / Histogram / Image Info panels, a filmstrip of the folder's
pictures, a status bar). It runs on **VGA, CGA and Hercules**. It loads **JPG,
PNG, GIF, BMP** and whatever else a viewer on this machine ought to load. The
desktop has 16 colours on VGA and 2 on CGA/Hercules, so it **dithers**, and it
offers a **full-screen view with more colours** where the hardware has them.
It should look modern, and it must stay cheap to redraw on a 4.77 MHz XT.

Everything below is priced against that XT (CLAUDE.md's table: a `gfx_*` call
~756 us fixed, a glyph ~900 us, an `int 13h` ~400 ms, 18.2 Hz tick).

---------------------------------------------------------------------------

## 0. Decisions at a glance

Taken by the planner, because the brief left them open. Each is a one-place
change if the owner wants it the other way; the five the owner is most likely
to care about are marked **(owner)**.

| # | decision | why |
|---|---|---|
| 1 | **Native 8086 assembly**, package `PIXEL.O88`, source `apps/pixel/pixel.asm` (stem = folder, which `os88index.py` and `t_livefull` require) | JPEG's Huffman reader and IDCT, inflate's bit buffer and every dither inner loop are register-bound 16x16->32 work. The C toolchain has no `long` and no `&local` (§73), so every one of them would be a fight |
| 2 | **SPEC.md §106.** §104/§105 are taken in the MIDIRack worktrees; renumber at merge if those land first (`checkdocs` refuses a duplicate) | |
| 3 | **Menu bar is File / Edit / Image / Effects / View.** There is **no Help menu**: "About PiXEL" goes in the app-name cell via `OSAPI_ABOUT_SET` (§12.7), and View > Keyboard Help shows the key card. **(owner)** | `MENU_APPMAX` = 5 (`kernel/menu.inc:132`), and CLAUDE.md names an invented Help menu as the exact mistake `docs/INDEX.md` exists to stop (SHEET, CHART). The mock-up's "Window" menu is the kernel's |
| 4 | **No canvas scroll bars.** Pan with the Hand tool, the Navigator's frame, the arrow keys and PgUp/PgDn | The mock-up has none. `os88ui.inc`'s scroll bar is vertical only (§13.10), and a new shared horizontal element is a §13.14.6 project of its own |
| 5 | **The master image is 8 bits per pixel, INDEXED, with a 256-entry RGB palette.** Truecolour sources are quantised during decode onto a fixed **6x7x6 colour cube + 4 greys**; greyscale sources get **256 greys**; paletted sources keep their own palette | One byte a pixel is the only depth that fits: 640x480 is 300 KB indexed, and 900 KB as RGB. An indexed master makes every display path a **per-palette-entry lookup**, and makes most of Effects (brightness, contrast, gamma, invert, greyscale, sepia, posterize, threshold, levels) a 256-entry palette edit that is instant even on an XT. §2 has the arithmetic |
| 6 | **The windowed view uses an ORDERED (8x8 Bayer) dither. Full screen uses error diffusion (Floyd-Steinberg) by default.** Both are on View > Dither | An ordered dither is position-stable, so any damage rect, any pan strip and any zoom renders on its own and matches its neighbours. That is what lets the canvas obey "nothing repaints more than it changed". Error diffusion would have to re-render the whole canvas on every pan |
| 7 | **Decoding runs on a WORKER. The UI task is the file pump** (the Audio Player's request-byte handshake, §86.5 / §77.1). The UI stays live: progress in the status bar, Esc / the Stop button cancels | A worker may not touch files (§20.6 rule 7), claim memory or load parts. A 640x480 baseline JPEG is tens of seconds on an XT, and a frozen desktop for that long is not acceptable |
| 8 | **Heavy decoders are lazy, compressed, far-called code PARTS** (`OP_SEG|OP_COMP|OP_LAZY`, §20.12): GIF, PNG, JPEG, TIFF and the save writers each load only when needed and are dropped after. PiXEL is the first assembly package to far-call a lazy code part (§68.10 calls this "the next step"), so wave 1 proves the mechanism with a gate row before any decoder depends on it | `APP_MAX_SIZE` is 61,440 bytes of image + bss. The decoders alone are bigger than that |
| 9 | **Associations: PiXEL declares JPG, PNG, PCX, TIF, PIX** (5 is the header maximum, §54.6). **BMP and GIF stay Paint's** built-in rows. PiXEL still opens them through File > Open, Prev/Next and the filmstrip. **(owner)** | Taking BMP/GIF from Paint is a kernel `assoc.inc` change and changes what OS8088.GIF opens in. TGA, ICO, PNM, LBM and MacPaint have no owner either; they open through File > Open and the filmstrip |
| 10 | **PiXEL carries its own streaming decoders and does not grow `os88img.inc`**, and §106 records the departure from §94.1's "8-bit is refused, quantising belongs on the host" policy **for PiXEL only**. **(owner)** | §94's contract is a whole file under 64 KB in, one 4bpp segment out. PiXEL's is a stream in and rows into a master of up to ~300 KB. Different shape, not a wider one. SCRIBE and the other `os88img.inc` consumers keep §94.1 unchanged |
| 11 | **GIF's LZW is lifted from Paint into a shared include, `apps/os88lzw.inc`.** PiXEL uses it in wave 3; Paint is switched to it in wave 9 only if `tests/paintgif.py` and `paint1load.py` stay byte-identical | What two programs share they share as source (WEAVE-SPEC §1.2's rule). Paint's decoder already has the guards that matter (code validation, no loop on corrupt data) |
| 12 | **Disk placement: `apps.img` (1.44 MB), `apps120`, the everything set and the live media through one `APPS_TOOLS` word, plus `office360` (with a sample picture) and a dedicated `make pixeldisk` in all four geometries** carrying the sample gallery. Off `apps360` and `apps720` by a dated §24.6.1 filter. **(owner)** | apps360 has 25 free clusters and apps720 has 37. A ~45-60 KB package plus pictures fits neither |
| 13 | **kern_small: `SMALLOMIT` with a requirement reason.** Its arena (~28-47 KB) cannot hold a master, and it lacks `READ_SEQ`, `BLITP`, the FSX VGA modes and associations | §24.5's rule: a package that cannot reach what it needs is left off and says why |
| 14 | **Sample pictures are ORIGINAL images made for this project** (generated once, then reduced and re-encoded by a committed host script), committed under `apps/pixel/samples/` with a provenance README. JPEG/PNG/GIF samples are not CZ-wrapped by `PKGZ`, since they are already compressed. **(owner)** | The gallery is what makes the filmstrip and the full-screen modes demonstrable, and `os88sample.py`-style synthetic pictures would not show a photo off. Nothing copyrighted is committed |
| 15 | **Test fixtures:** small committed JPEGs (baseline 4:4:4/4:2:2/4:2:0/grey, restart markers, progressive), each pinned by SHA-256, made once with Pillow/cjpeg. Every other format is generated deterministically by `tools/pixcorpus.py` in pure Python | A pure-Python JPEG *encoder* to generate fixtures is more code than it is worth. PNG (zlib), GIF, BMP, PCX, TGA and TIFF writers are a few lines each |

---------------------------------------------------------------------------

## 1. Scope

### 1.1 Formats

| format | read | write | notes |
|---|---|---|---|
| **JPEG** (`.JPG`/`.JPEG`) | baseline (SOF0/SOF1), Huffman, 8-bit, grey/YCbCr, 4:4:4 / 4:2:2 / 4:2:0 / 4:1:1, restart markers, EXIF orientation; **progressive (SOF2) at up to 1/4 scale** (§3.4) | no | DCT-domain scaling 1/1, 1/2, 1/4, 1/8: 1/8 is DC-only and does no IDCT at all |
| **PNG** | all colour types, bit depths 1-16, Adam7 interlace, PLTE, tRNS (composited over the view background), gAMA ignored, CRC checked (with a "verify CRC" option off by default on an 8088) | yes (§8.4) | inflate with a 32 KB window claim |
| **GIF** | 87a/89a, interlaced, local/global palettes, transparency, **first frame; animation playback in wave 8** | yes | LZW via `os88lzw.inc` |
| **BMP** | 1/4/8/16/24/32 bpp, RLE4/RLE8, bottom-up and top-down, OS/2 v1 headers | yes (8-bit and 24-bit) | |
| **PCX** | 1-bit, 4-plane EGA, 8-bit + 256 palette, 24-bit (3 planes) | yes (8-bit) | |
| **TIFF** (`.TIF`) | baseline: uncompressed, PackBits, LZW (+ horizontal predictor), strips, 1/4/8 bit grey/palette, 24-bit RGB, both byte orders | no | wave 8 |
| **TGA** | types 1/2/3/9/10/11, 8/15/16/24/32 bpp, both origins | no | |
| **PIX** | os8088's own picture format (§94, `tools/os88pix.py`) | yes | |
| **PNM** (`.PBM/.PGM/.PPM`) | P1-P6 | no | trivial, and the host tools' lingua franca |
| **ICO/CUR** | the largest image, BMP-encoded (1/4/8/24/32) with AND mask; PNG-in-ICO through the PNG part | no | wave 8 |
| **IFF ILBM / PBM** (`.LBM`, `.IFF`) | Deluxe Paint's format: ByteRun1, 1-8 planes, CMAP, EHB | no | wave 8. The period format a PC paint user had |
| **MacPaint** (`.MAC`) | 576x720 1-bit PackBits, with or without the MacBinary header | no | wave 8. On-theme for a System 1-style OS |

Format is decided by **content sniffing first**, extension second, so a misnamed
file still opens. A file PiXEL cannot read is refused by name in the status
line and a toast ("Progressive JPEG > 1/4", "16-bit TGA palette"). It is never
approximated silently.

### 1.2 The window (the mock-up, laid out from live geometry)

```
+------------------------------------------------------------------+
| [Open][Save] | [Prev][Next] | [Zoom+][Zoom-][Fit][1:1] | [Rotate][Show] |  toolbar
+---+--------------------------------------------+-----------------+
|Hnd|                                            | Navigator    [-]|
|Zm |                                            | [thumb + frame] |
|Sel|              canvas                        |  100% [+][-][Fit]|
|Crp|          (dithered view of the master)     | Histogram    [-]|
|Eye|                                            | [graph] Lum v   |
|Rot|                                            | Mean/SD/Min/Max |
|   |                                            | Image Info   [-]|
|   |                                            | name/path/dims..|
+---+--------------------------------------------+-----------------+
| Images (13)                                                   [-] |
| [<] [thumb][thumb][thumb][thumb][thumb][thumb][thumb][thumb] [>]  |  filmstrip
+------------------------------------------------------------------+
| VACATION.JPG | 640 x 480 | JPEG | 100% | 256 colours | 87,432 bytes | Mem 200K | 6 of 13 [<][>] |
+------------------------------------------------------------------+
```

- **Toolbar:** ten 16x16 icon buttons with captions (`OS88UI_BIMG`, §13.8.9),
  grouped by separators as in the mock-up. "1:1" is a text button. On CGA the
  captions go and the icons stay (the row is 20 px rather than 30).
- **Tool column:** Hand, Zoom, Marquee, Crop, Eyedropper, Rotate.
  `OS88UI_LATCH` marks the active tool. Each glyph is one `OSAPI_ICON_DRAW`,
  and on a 1bpp screen the whole column is one band (Paint's §42.26 /
  §42.26.1 lesson).
- **Right panels:** Navigator, Histogram, Image Info. Each has a [-] collapse
  box, so a collapsed panel is its title strip only, and View > Panels toggles
  them.
- **Filmstrip:** "Images (N)" with < > paging and the current picture framed.
- **Status bar:** one `OSAPI_FONT_RUN` per field, each repainted only when its
  value changes. State goes here; verdicts go to toasts (§59.5).
- **Layout tiers from the live window size** (`W_W`/`W_H` on every paint,
  §11.100 `OSAPI_WM_PREFER` per adapter):
  - VGA 640x480 and Hercules 720x348: the full layout.
  - CGA 640x200: toolbar without captions, panels collapsed into a single
    switchable column, filmstrip hidden by default (View > Filmstrip shows it).
  - Any window too small for a tier drops panels in the order Info, Histogram,
    Navigator, then the filmstrip.
- Look at every layout tier on a 1bpp adapter before calling it done (§39.4,
  §47: grey rounds to black there).

### 1.3 Menus (11 items a menu, 24 glyphs wide)

- **File:** Open..., Save As..., Revert, -, Previous Image, Next Image,
  Slideshow, -, Image Info...
- **Edit:** Undo <op>, -, Copy, -, Select All, Deselect, Crop to Selection
- **Image:** Rotate 90 CW, Rotate 90 CCW, Rotate 180, Flip Horizontal,
  Flip Vertical, -, Resize..., Auto Levels, Brightness/Contrast..., Greyscale,
  Invert
- **Effects:** Blur, Sharpen, Edge Detect, Emboss, Pixelate, -, Sepia,
  Posterize..., Threshold..., Gamma...
- **View:** Zoom In, Zoom Out, Fit, Actual Size, -, Full Screen, Dither:
  Ordered/Diffusion, Panels..., Filmstrip, Keyboard Help

Greyed items carry their reason in the status line (§47: grey a fact, never a
guess). Examples: "Undo needs 300K; 120K free", "Full Screen 256: VGA only".

---------------------------------------------------------------------------

## 2. The image model

### 2.1 Master, palette, mode

```
master   : W x H bytes, one heap claim (claims may exceed 64 KB; rows are
           reached by segment arithmetic: seg = base + (y*W)>>4, Paint's way)
palette  : 256 x RGB888 (768 bytes, resident bss)
pmode    : PAL (source palette, n <= 256 entries)
           CUBE (6x7x6 = 252 colours + 4 greys, for truecolour sources)
           GREY (256 greys)
```

- **Truecolour to CUBE** during decode: per pixel `r6 = r*6>>8`, `g7 = g*7>>8`,
  `b6 = b*6>>8` and `idx = (r6*7+g7)*6+b6`, through three 256-byte tables. One
  row of Floyd-Steinberg error (3 x (W+2) words) gives a smooth master. It is
  on by default and off with Settings > Fast decode.
- **Why the cube, and not an adaptive palette:** a truecolour source cannot be
  stored to make a second pass, and neighbourhood effects (§7.3) need a
  colour-to-index map. In a cube that map is arithmetic. For an arbitrary
  256-colour palette it is a 32 KB inverse-map table, which takes minutes to
  build on an XT. The cube's quantisation step is far smaller than the
  16-colour desktop's, so it never limits what the window shows. Full screen
  256 shows the cube's 256 colours directly.
- **GREY** is exact for greyscale JPEG/PNG/PGM/TIFF and is what the 1bpp
  adapters effectively see.
- **Memory budget:** `OSAPI_MEM_AVAIL` decides the scale.
  - The master is the largest of 1/1, 1/2, 1/4, 1/8 of the source that fits
    `largest run - reserve`. The reserve covers the dither tables, the display
    band, the thumbnail store and the decoder's working claim.
  - JPEG scales in the DCT domain. Every other format box-filters rows during
    decode (a 1/2 scale keeps two source rows of accumulators).
  - The Info panel says so: "1600 x 1200 (shown at 1/4)".
  - Typical free heap: ~360 KB largest run on a 640 KB machine, so 640x480 is
    1/1. On a 256 KB XT it is ~140 KB, so 640x480 is 1/2.
- **Claims:** at most 8 per owner (`MEM_OWNER_MAX`), counting the code region.
  Budget: region, master, decoder work, display band, thumbnails, undo (when
  affordable), lazy part = 7.
  - The master claim is **movable** (§66) with a relocation proc that re-bases
    `px_mseg`, and pinned across any file call (§66.5.7.1).

### 2.2 Display lookup tables (rebuilt when the palette changes: 256 entries, ~10 ms)

- **VGA 16 (desktop palette, the EGA 16 of `os88api.inc:157`):** per master
  index, a **mixing plan** `(c1, c2, t)`, where c1 and c2 are the two desktop
  colours whose blend best reproduces the entry (Yliluoma-style, searched over
  the 16x16 pairs once per entry) and t is the mix ratio in 64ths. Pixel
  `(x, y)` takes c2 if `bayer8[y&7][x&7] < t`, else c1. Inner loop: `lodsb`,
  one `xlat` to the plan, one compare against a pre-rotated threshold row. The
  target is ~25 clocks a pixel.
- **1bpp (CGA 640x200, Hercules, the second card of a multi-monitor desktop):**
  per index, luma `Y = (77R + 150G + 29B) >> 8` after the display's gamma
  table. Pixel lit if `bayer8 < Y`. ~15 clocks a pixel.
- Which table is used comes from **`OSAPI_WM_DISPLAY`'s DH (bits per pixel of
  the display the window is on)**, re-read in `OSAPI_WM_ONRESIZE`, never from
  `OSAPI_VIDEO`. This is so a window dragged across a VGA+Hercules desktop
  dithers for the display it is on (§39.18).

### 2.3 Pixel aspect

The canvas maps image pixels to screen pixels with an **aspect factor**:

| screen | pixel aspect (w/h) |
|---|---|
| VGA 640x480 | 1.0 |
| CGA 640x200 | 0.4167 |
| Hercules 720x348 | ~0.645 |
| EGA 640x350 | 0.729 |

A 640x480 photo therefore looks right on every adapter, rather than tall and
thin on CGA. "1:1" means one image pixel to one screen pixel *horizontally*,
with rows scaled. The full-screen modes carry their own factor (§5).

### 2.4 Zoom and pan

- **Zoom steps:** 1/8, 1/6, 1/4, 1/3, 1/2, 2/3, 1, 2, 3, 4, 6, 8 (shown as
  12%..800%), plus Fit.
- **Sampling:** a 16.16 DDA per row and per column, nearest-neighbour from the
  master. Dithering always happens at **screen** resolution, so a zoomed-in
  picture never shows a magnified dither pattern.
- **Panning by k pixels** (Hand drag, arrow keys, the Navigator frame):
  `OSAPI_GFX_SCROLL` moves the canvas, then only the exposed strip is rendered.
  A drag coalesces to one scroll + strip per tick.

---------------------------------------------------------------------------

## 3. Decoders

### 3.1 Pipeline

```
UI task (file pump)                  worker (decoder)
  OSAPI_FILE_READ_SEQ 8 KB chunks       reads bytes from the ring through a
  into a 2 x 8 KB ring  <-- req byte    refill call: "need more" = request byte
  ONWAKE: read next chunk,              set LAST, OSAPI_WM_WAKE, sleep until cleared
  clear req byte LAST                   emits ROWS -> scaler -> quantiser -> master
  repaints progress (status bar)        updates [px_rows_done]; never draws
```

- The ring buffer lives in a claim made by the UI task. Base 512-aligned, with
  no 64 KB straddle (`OSAPI_MEM_CLAIM_DMA`), because it is a disk buffer.
- **CZ-wrapped files** (`PKGZ` may compress a data file on a shipped disk;
  `READ_SEQ` returns the raw bytes) are detected by the `'CZ'` magic and
  expanded through `OSAPI_FILE_READ`, which unpacks.
- **Progress:** rows done / rows total, drawn as one opaque run in the status
  bar's progress field. It changes at most once a tick.
- **Cancel:** Esc or the Stop button sets `[px_abort]`, and the decoder checks
  it at every refill and every 8 rows. Cancelling a reload keeps the previous
  picture, because the old master is freed only after the new one decodes.
  When memory forces it to be freed first, the status line says so.
- **Progressive display:** the canvas paints decoded rows in bands as they
  arrive (every ~16 rows). On an XT the picture appears top-down rather than
  after a long blank wait.
- **Hostile input:** every length, dimension, code and table index is
  bounds-checked against the format document. Dimensions are capped at
  8192x8192 before any arithmetic. Every row is written through one clipped
  emitter. The rule is that a corrupt file cannot write outside the master and
  cannot loop (Paint's §42.14 guards are the model).

### 3.2 The part ABI (wave 1)

- Each decoder part is `OP_SEG|OP_COMP|OP_LAZY`, assembled at org 0, and
  starts with a vector table: `init`, `decode`, `info`, in a fixed layout
  shared by every part through `apps/pixel/pxpart.inc`.
- The resident side calls `op_fetch(part)` on the UI task, finds the segment
  with `op_seg`, and `call far`s the vectors. The worker far-calls `decode`,
  which is legal because the part is just code in a segment and the fetch has
  already happened.
- Parts follow SCRIBE's module rules (§95.8):
  - own tables through `CS:`
  - package data through a stamped package segment, never `push cs / pop ds`
  - errors returned as numbers; the part never speaks, and the resident caller
    toasts
  - no ES-fenced slots
- `op_drop` happens when no decoder of that kind has been needed for one
  picture change, so Next/Next/Next through a folder of JPEGs reads the part
  once.
- Gate row (`tests/pxparts.py`, MartyPC): fetch, call, drop and re-fetch a
  stub part, with the claim count checked before and after.

### 3.3 GIF and PNG (wave 3)

- **GIF:**
  - `os88lzw.inc`, lifted from Paint (§42.14), with the dictionary in a 16 KB
    claim and run emission into the row emitter.
  - Interlace by pass, rows placed directly at their final y in the master.
  - Transparent index mapped to the view background.
- **PNG:**
  - **Inflate:** stored, fixed and dynamic blocks. Huffman decode by a 9-bit
    primary lookup table plus secondary tables (zlib's "fast" shape, sized for
    a 64 KB claim). The 32 KB window lives in the same claim as the ring.
  - **Filters:** None, Sub, Up, Average, Paeth, two scanline buffers. Paeth is
    in asm with the branch order from the spec.
  - **Adam7:** writes into the master at the pass's coordinates. The scaled
    case box-accumulates per pass into a scratch row set.
  - **Pixel conversion:** 16-bit samples use the high byte. Palette-plus-tRNS
    and alpha are composited over the view background colour (Settings:
    white / grey / checker). CRC is skipped unless verification is on.
  - **Target:** ~30-40 clocks per output byte of inflate on an 8088.
    PERFORMANCE.md's method measures it in wave 3 and SPEC §106 records the
    figure.

### 3.4 JPEG (wave 4)

- **Headers:** markers SOI, APPn (JFIF, EXIF orientation only), DQT, SOF0/1/2,
  DHT, SOS, DRI, RSTn, EOI. Anything else is skipped by its length.
- **Huffman:** 16-bit bit buffer, 9-bit lookup, then the slow path by code
  length. Byte-stuffing (`FF 00`) and marker detection happen in the refill.
- **Dequantise + IDCT:** AAN 16-bit fixed point with 8.8 constants. The 1-D
  passes use `imul` into DX:AX, keeping the high word. A **reduced IDCT**
  applies at scale 1/2 (4x4 output from the top-left 4x4 coefficients) and
  1/4 (2x2). Scale 1/8 is the DC term only. Fast paths skip zero AC rows and
  columns (typical photos are 70-85% zeros).
- **Colour:** YCbCr to RGB through four 256-entry word tables (Cr->R, Cb->G,
  Cr->G, Cb->B), then into the cube with the error row.
  - 4:2:0 / 4:2:2 chroma are upsampled by replication (fast), with an
    optional "fancy" triangle filter on a 286 or better (`OSAPI_CPU_INFO`).
  - Greyscale JPEG goes to GREY mode with no colour work at all.
- **Progressive (SOF2):** coefficients for the whole image do not fit (640x480
  is 600 KB), so progressive decodes at **1/4 scale at most**. It keeps the
  4 lowest-frequency coefficients per block per component (a 640x480 picture
  is 4800 blocks x 4 x 2 bytes x 3 components = 115 KB, or 1/8 DC-only at
  29 KB).
  - DC first/refine scans and AC scans are decoded into that store. The
    coefficients beyond the kept band are decoded and discarded.
  - Then the reduced IDCT is applied. The Info panel says "progressive, shown
    at 1/4".
  - On a machine where even that does not fit, it is DC-only at 1/8.
- **Exactness:** the IDCT and the colour conversion are specified as integer
  arithmetic in §106. `tools/pixelsim.py` implements the same integers, so the
  guest's master is compared **byte for byte** with the reference. A second,
  looser check compares against Pillow's decode (PSNR > 30 dB) when Pillow is
  installed, and skips with a reason when it is not.
- **Target on a 4.77 MHz XT** for a 640x480 4:2:0 q85 picture:
  - 1/8 (thumbnails, DC only): ~4-6 s
  - 1/2: ~20 s
  - 1/1: ~45-60 s
  These are **estimates**. Wave 4 measures them on MartyPC and records the
  real figures. The view shows rows as they arrive, so the wait is watchable.
- **Fast open option** (on by default on an 8088, off on a 286+): a JPEG
  larger than the canvas opens at the largest DCT scale that still covers the
  canvas, and "Actual Size" re-decodes at 1/1 on demand. This is the single
  biggest XT win: a 640x480 JPEG in a 430x300 canvas decodes at 1/2 in about a
  third of the time.

### 3.5 The simple formats (wave 2) and the rest (wave 8)

- BMP, PCX, TGA, PNM and PIX are resident, because each is a few hundred
  bytes.
- TIFF, ICO, ILBM and MacPaint go in one lazy "extras" part. TIFF's LZW is
  `os88lzw.inc` with the TIFF code-width rules (early change).

---------------------------------------------------------------------------

## 4. Rendering and redraw economy

- **The canvas composer:**
  - It renders a **band** of up to 16 screen rows by the canvas width into a
    claim, one band at a time.
  - VGA: planar bytes for `OSAPI_GFX_BLITP` when x is a multiple of 8 and no
    clip is armed (it refuses otherwise: read CF), and packed 4bpp for
    `OSAPI_GFX_BLIT4` as the fallback.
  - 1bpp: `OSAPI_GFX_BLIT1`.
  - The canvas origin is snapped to 8 px (`OSAPI_WM_SNAP`, §11.94) so BLITP is
    the normal path.
- **Estimated full-canvas costs on the XT** (430x300 VGA canvas): compose
  ~25 clk/px, about 0.7 s, plus BLITP about 0.3 s, so **~1 s**. Hercules
  (560x260): ~0.5 s. Wave 2 measures both, and `tests/pxpaint.py` counts the
  calls.
- **Damage only** (`OSAPI_WM_OWNBG` + `OSAPI_WM_DAMAGE`, §11.90): a panel
  collapse repaints the canvas column it uncovers, a status field repaints its
  field, and the filmstrip repaints the thumbs that moved.
- **Panels:**
  - **Navigator:** a fit-scaled thumbnail, rendered once per picture or effect,
    banked in a claim and blitted. The view frame is an XOR rect
    (`OSAPI_GFX_XOR_RECT`), so moving it never re-renders the thumbnail.
  - **Histogram:** the worker counts master indices (256 words), then the
    luminance or R/G/B bins are folded through the palette in 256 steps rather
    than W*H. The graph is composed into one 1bpp band (`os88gfx.inc`
    `GFXE_BAND`) and put up with one `OSAPI_GFX_BLIT1_PEN` + `BLIT1`, in the
    same way on every adapter. Mean, SD, min and max are 32-bit sums over the
    256 bins.
  - **Info:** labelled `FONT_RUN` lines.
- **Budget table:** wave 9 adds PiXEL's rows to PERFORMANCE.md Part 5: open,
  pan by a step, zoom step, panel collapse, status field update, filmstrip page
  and full-screen enter. Any later full repaint is a regression against a
  documented number.

---------------------------------------------------------------------------

## 5. Full screen: more colours where the card has them (wave 6)

`View > Full Screen` (F, or Alt+Enter; Esc leaves) uses the best mode for the
display the window is on, chosen by `OSAPI_FSX_CAPS` **with PiXEL's own window**
(§39.18.2) and tested by mode bit, never by adapter kind. A submenu dialog lets
the user pick another available mode. Everything happens in a §53 bracket:

| display | default mode | colours | how |
|---|---|---|---|
| VGA | **Mode X 320x240x256** (`FSXM_MODEX`) | 256, DAC = the master palette (6-bit) | square pixels, so a 4:3 photo fills the screen exactly. Ordered or diffusion is irrelevant at 256: the cube is shown as is. 13h 320x200 is offered too |
| VGA | **640x480 x 16 adaptive** (`FSXM_VGA12` + identity attribute map + 16 DAC entries) | 16 *chosen* colours | median cut over the master's palette weighted by the histogram: ≤256 weighted points, so this is milliseconds even on an XT. Then Floyd-Steinberg at full resolution. Gorillas' `gr_vgapalette` is the register recipe |
| CGA | **160x100 x 16** (the C160 text retime, Skies' `cs_c160_mode`) | 16 | on a real CGA or VGA only, never EGA (350-line text). Diffusion dithered |
| CGA | **320x200 x 4** (`FSXM_CGA320`) | 4 | the palette is CHOSEN: 16 backgrounds x {pal 0, pal 1} x {low, high} x {mode 5 cyan/red/white} are scored against the weighted palette and the best one is used. It is set through the BIOS (`AH=0Bh`, Skies' portable way), never port 3D9h |
| CGA | 640x200 mono | 2 | diffusion, full-screen aspect |
| Hercules | 720x348 mono (`FSXM_HERC`) | 2 | diffusion at full resolution, aspect corrected |
| EGA | the §11.2 latch (a full-screen window, desktop mode and palette) | 16 | EGA's FSX caps offer only CGA modes |

- **Letterboxing:** pictures are fitted inside the mode and centred.
- **Pan in full screen:** arrow keys pan when the picture is bigger than the
  screen at the current zoom.
- **Keys:** + and - zoom, Space and N/P go to the next/previous image (the
  slideshow uses the same loop), and a mouse click goes to the next image.
- **What the bracket may not do (§53.7, binding):**
  - no drawing slots after the first `fsx_mode`
  - no `int 10h` mode sets of its own
  - PiXEL draws into the FSI surface directly
- **Restore:** the bracket's exit restores the desktop and the palette.
  - The VGA 16-adaptive mode MUST call `FSX_MODE(FSXM_VGA12)` even though the
    desktop is already 12h, or nothing restores the DAC (the
    `fsx.inc:378-381` trap).
- **Text in full screen** (filename, "3 of 13", the key hint for 2 s) is
  lettered from `OSAPI_FONT_GLYPHS` (§53.1).

---------------------------------------------------------------------------

## 6. The folder: Prev/Next, filmstrip, slideshow (wave 5)

- **Folder list:** after File > Open (or a document launch) the instance's
  directory is the picture's folder (§38.10). `OSAPI_FILE_FIND` walks it once
  and keeps the names of files whose extension is one PiXEL reads. The list is
  sorted by name and holds up to 128 entries, 16 bytes each, in bss.
- **Prev/Next** open the neighbour through the same pipeline (Home/End go to
  the first and last).
- **Filmstrip thumbnails:**
  - Each thumbnail is 64x48 (CGA 64x20 aspect-scaled), stored as an **8bpp
    cube-indexed** 3 KB block, so any display dithers it with the same tables.
  - They are made by the same worker pipeline at the smallest scale that
    covers 64x48 (JPEG 1/8 DC-only), in idle time, one picture at a time,
    current-first and then outward.
  - A placeholder card shows the format name until the thumbnail is ready.
- **Thumbnail cache:** `SYSTEM/APPDATA/PIXEL.THC` (§19.9) holds up to 64
  entries, keyed by volume serial + path + size + date, LRU-replaced. It is
  written when the filmstrip goes idle, never mid-browse. A second visit to a
  folder fills the strip without decoding anything (one file read). On a
  read-only disk the strip simply is not cached, and nothing says so more than
  once.
- **Slideshow:** File > Slideshow or the toolbar button.
  - It is full screen when the display has a full-screen mode, else the
    maximised window. Interval 5 s (Settings: 2-30 s).
  - The next picture decodes into a second master **while** the current one is
    shown when memory allows. Otherwise there is a decode pause, shown as a
    progress bar along the bottom row.
  - Esc or a click stops it.
- **Document launch** (§54.5/§54.10, Paint's `pt_argload` two-phase shape):
  double-clicking `VACATION.JPG` opens PiXEL on it with the folder strip
  already pointed at its folder.

---------------------------------------------------------------------------

## 7. Editing (wave 7)

### 7.1 Tools

| tool | gesture |
|---|---|
| Hand | drag pans; double-click = Fit |
| Zoom | click zooms in at the point; Shift- or right-click zooms out; a drag rect zooms to the rect |
| Marquee | rubber-band XOR rect selection in image coordinates; handles; arrows nudge |
| Crop | marquee, then Enter or a double-click crops |
| Eyedropper | the status bar shows x,y, RGB and index under the pointer; a click pins it in the Info panel |
| Rotate | a click rotates 90 CW, Shift 90 CCW |

### 7.2 Palette operations (instant; undo is a 768-byte palette copy)

- Brightness/Contrast, Gamma, Auto Levels (from the histogram's 1%/99% points),
  Invert, Greyscale, Sepia, Posterize (n levels), Threshold.
- On a CUBE or PAL master these edit the palette and then rebuild the 256-entry
  display tables. No pixel is touched.
- **The one wrinkle:** after a palette op a CUBE palette is no longer a cube,
  so the next neighbourhood op first maps the pixels back to the cube (one
  256-entry LUT pass over the master).

### 7.3 Pixel operations (worker, progress, cancellable)

- **Geometry:**
  - Rotate 90/180 and Flip run in place where W=H or for 180/flips. Otherwise
    they go through the undo buffer, or through row-at-a-time temp-file
    rotation on a machine without room.
  - Crop and Resize (box filter down, bilinear up, in RGB through the palette,
    re-quantised to the cube).
- **3x3 convolutions** (Blur, Sharpen, Edge, Emboss) and Pixelate run in RGB
  through the palette with a 3-row window, re-quantised to the cube
  (arithmetic, §2.1) or to GREY.
- XT cost estimate for 640x480: ~25-40 s each. Wave 7 measures it and records
  it in §106.

### 7.4 Undo

- **One level.**
  - Palette ops: always (768 bytes).
  - Rotate 180 and flips: by re-applying.
  - Other pixel ops: a copy of the master in a claim, then **XMS**
    (`OSAPI_XMEM_*`) on a 286+, and otherwise the menu item is greyed with the
    fact ("Undo needs 300K; 120K free").
- **Revert** re-reads the file.

### 7.5 Save As (lazy "writers" part)

- Formats: BMP (8-bit and 24-bit), PCX (8-bit), GIF (via `os88lzw.inc`'s
  encoder, Paint's `pt_gif_out` lineage), **PNG** (zlib with fixed-Huffman
  deflate and a simple hash-chain LZ77, which is level-1-like and small, plus
  CRC and Adler) and PIX.
- JPEG and TIFF are read-only.
- The format comes from the extension typed, with a format drop-down
  (`OS88UI_DROP`) in a small options card.
- Unsaved edits go through `OS88UI_ASAVE` (Save / Discard / Cancel, §75) on
  close and on Prev/Next.
- **Copy** puts the selection (or the picture) on the system clipboard (§55)
  in the clipboard's bitmap form. **Paste** is out of scope (a viewer does not
  compose), and the plan says so in §106.

---------------------------------------------------------------------------

## 8. Testing

All rows are registered in `tests/suite.py`. MartyPC is the default emulator
(docs/WRITING-TESTS.md §9); QEMU is used only where the closed list allows.

- **Host reference — `tools/pixelsim.py`:** pure Python, no Pillow needed.
  - Every decoder's integer arithmetic, the cube quantiser and its error
    diffusion, the mixing-plan builder, the Bayer renderer for 4bpp/1bpp, the
    histogram statistics, the CGA palette chooser and the median cut.
  - `--selfcheck` runs in the build like `weavesim.py`'s.
  - It is the authority the guest is compared against byte for byte.
- **Corpus:**
  - `tools/pixcorpus.py` generates small deterministic PNG/GIF/BMP/PCX/TGA/
    PNM/TIFF/ICO/LBM/MAC fixtures covering every depth, type, interlace,
    compression and orientation in §1.1, plus a **hostile set** (truncated
    files, lying lengths, 65535x65535 headers, bad Huffman tables, LZW codes
    past the table, inflate distances past the window).
  - JPEG fixtures are committed and SHA-256-pinned (decision 15).
- **Fast tier (each well under a second; the tier is at 26.7 of 30 s
  charged):** `pixelsim --selfcheck`, a corpus-is-stale check, `t_pkgdeps`,
  `t_btnrules`, `t_movable` and the rest of the package gates PiXEL joins
  automatically.
- **Soak tier (MartyPC):**
  - `pxdecode`: a test build of PiXEL with a hook that decodes each fixture
    and compares the master + palette with `pixelsim` byte for byte. Hostile
    files must be refused with the right error and leave the claim count
    unchanged.
  - `pxparts`: lazy part fetch, call, drop and re-fetch.
  - `pxopen`: open by association (`paint1load.py`'s shape), check the bss,
    and check the canvas framebuffer against `pixelsim`'s render of the same
    rect.
  - `pxpaint`: call counts for pan / zoom / panel / status updates against the
    budget.
  - `pxfsx`: enter every full-screen mode, read the surface back, then check
    the desktop and palette are restored.
  - `pxthumb`: thumbnail cache cold/warm.
  - `pxedit`: each effect against `pixelsim`.
  - `pxsave`: the round trip save, re-read, then the host decodes it with
    stdlib (`zlib`) / Pillow if present.
  - Each emulator row runs on VGA and on one 1bpp adapter.
- **Speed:** `tests/pxbench.py` times open (per format and scale), pan, zoom
  and full-screen enter on the MartyPC 5150 by cycle counter. Wave 9 writes
  the numbers into §106 and PERFORMANCE.md.
- **On the glass:** each wave ends with the `functional-check` skill's pass
  (QEMU, QMP-driven) on VGA, CGA and Hercules, with screenshots attached to
  the PR comment for that wave.

---------------------------------------------------------------------------

## 9. Files

```
apps/pixel/pixel.asm      entry, header, icon, menus, the event procs, includes
apps/pixel/pxui.inc       layout tiers, toolbar, tool column, panels, filmstrip, status bar
apps/pixel/pxicons.inc    16x16 ICON_DRAW art (toolbar, tools, panel boxes)
apps/pixel/pxview.inc     canvas composer, zoom/pan, mixing plans, Bayer renderers
apps/pixel/pxmaster.inc   master + palette + modes, the cube, the row emitter, scaling
apps/pixel/pxpump.inc     worker, ring, the file pump, progress, cancel
apps/pixel/pxsimple.inc   BMP / PCX / TGA / PNM / PIX readers (resident)
apps/pixel/pxpart.inc     the part vector ABI (shared by every part)
apps/pixel/pxgif.asm      part: GIF (+ apps/os88lzw.inc)
apps/pixel/pxpng.asm      part: PNG + inflate
apps/pixel/pxjpeg.asm     part: JPEG
apps/pixel/pxextra.asm    part: TIFF, ICO, ILBM, MacPaint
apps/pixel/pxwrite.asm    part: the writers (BMP/PCX/GIF/PNG/PIX)
apps/pixel/pxfsx.inc      full-screen modes, palette choosers
apps/pixel/pxfolder.inc   folder list, filmstrip, thumbnail cache, slideshow
apps/pixel/pxedit.inc     tools, palette ops, pixel ops, undo
apps/pixel/samples/       the sample gallery + README (provenance)
apps/os88lzw.inc          the shared LZW (decode + encode)
tools/pixelsim.py         the reference implementation
tools/pixcorpus.py        the fixture generator
tools/pixsamples.py       re-encodes the gallery into each shipped format/size
tests/pixel/              committed JPEG fixtures (pinned)
tests/px*.py              the rows of §8
```

---------------------------------------------------------------------------

## 10. Waves

Each wave is built by a builder agent in `/tmp/pixel` and reviewed by an
independent reviewer agent (correctness, hostile-input safety, redraw cost,
lost-from-main regressions). The builder fixes the findings. The orchestrator
then runs `make`, the wave's rows and `make test-full`, and pushes **one
commit per wave** to this PR with the wave's size line (`os88pkgsize`). SPEC
§106 is extended **before** each wave's code, in the same commit.

1. **Shell and plumbing.**
   - Package skeleton: the window at every layout tier on all three adapters
     with placeholder content, the toolbar and tool icons, menus, About,
     keyboard help, status bar and empty state ("Open a picture (Ctrl+O)").
   - The part ABI and `tests/pxparts.py` with a stub part.
   - Makefile rule, `APPS_TOOLS` word, dated apps360/apps720 filters,
     `OFFICE_PKGS`, `SMALLOMIT` with its reason, `make pixeldisk` (four
     geometries), INDEX regeneration, README line.
   - The sample gallery and `tools/pixsamples.py`.
2. **Image core + simple formats.**
   - The master and palette model, the worker + file pump + progress + cancel,
     BMP/PCX/TGA/PNM/PIX.
   - Mixing plans, the 4bpp and 1bpp Bayer composers, aspect, zoom, pan with
     `GFX_SCROLL`.
   - The Navigator, Histogram and Info panels, the live status bar.
   - `tools/pixelsim.py` (simple formats + render) and `tools/pixcorpus.py`,
     rows `pxdecode` / `pxopen` / `pxpaint`.
3. **GIF + PNG.** `apps/os88lzw.inc`; the GIF part (interlace, transparency);
   the PNG part (inflate, filters, Adam7, tRNS/alpha, 16-bit); pixelsim for
   both, plus the hostile corpus.
4. **JPEG.**
   - Baseline with all subsamplings, restart markers, EXIF orientation, and
     DCT scaling 1/2, 1/4, 1/8.
   - Progressive at ≤1/4.
   - Fast open.
   - pixelsim's integer IDCT; the byte-exact row and the Pillow PSNR row.
   - The first XT timings.
5. **The folder.** Folder list, Prev/Next, the filmstrip with thumbnails and
   `PIXEL.THC`, slideshow (windowed), associations, document launch,
   `pxthumb`.
6. **Full screen.** Mode X 256 and 13h, VGA 16-adaptive (median cut + FS), CGA
   C160 / 320x200x4 (palette chooser) / 640x200, Hercules, the EGA latch, the
   full-screen slideshow, `pxfsx`.
7. **Editing.** The six tools, Image and Effects menus, undo (claim / XMS /
   greyed), Save As with the writers part, clipboard Copy, `OS88UI_ASAVE`,
   `pxedit`, `pxsave`.
8. **More formats + animation.** The extras part (TIFF, ICO/CUR, ILBM/PBM,
   MacPaint) and GIF animation playback in the window (frame disposal, delays,
   paced by `OSAPI_WM_TIMER`, stops when obscured).
9. **Performance and finish.**
   - `pxbench` on the MartyPC 5150; fix whatever the numbers indict; the
     PERFORMANCE.md Part 5 rows; §106's measured figures.
   - Switch Paint onto `os88lzw.inc` if byte-identical.
   - 86Box profiles `vm/xt-pixel`, `vm/386-pixel` (`pixeldisk` in B:).
   - The full functional check on all adapters, and `test-full` green.

---------------------------------------------------------------------------

## 11. Risks and how each is caught

| risk | caught by |
|---|---|
| A far-called lazy part is new ground | wave 1 builds the gate row before any decoder depends on it |
| Heap fragmentation after browsing many pictures | `pxthumb` and `pxopen` assert the claim count and the largest run after 20 Prev/Next cycles |
| The fast tier's 30 s budget | new fast rows are pure host checks ≤0.5 s each; anything heavier is soak |
| CGA's 156 content rows | layout tiers are asserted by `pxopen` on CGA, and wave 1's screenshots |
| Decode times on an XT worse than estimated | fast open (§3.4), progressive row display, DC-only thumbnails, and the cache; wave 4 measures before wave 5 builds on it |
| A corrupt file crashes the machine | the hostile corpus in every decoder wave; one clipped row emitter |
| Palette leaks out of full screen | `pxfsx` reads the DAC back after exit (the `fsx.inc:378` trap) |
