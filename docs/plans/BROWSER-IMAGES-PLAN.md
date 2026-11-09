# Browser images — the 1.7 plan

Inline images in the windowed Browser: `<img>` laid out, fetched, decoded,
kept and drawn, on every adapter, with or without EMS. This replaces the
2026-10-17 audit plan (kept in git history at `ca2e1917`); its audit table
still holds and is summarised in section 1. SPEC.md 71 gets the contract
before any code (section 6).

The owner's target for the Browser is the early web as Netscape 2 or IE 3
showed it - images, colours, JavaScript 1.2, no plugins - on this OS and its
machines, the 4.77 MHz 8088 included. Images are the first step. Every
decision below is priced against the Browser's one hard limit: image + BSS
at most 65,024 bytes (SPEC.md 20.1.1), with 12,404 free at the start of 1.7
(BROWSER-1.7-ROADMAP.md).

## 1. What the OS and PiXEL already give

| Need | Already there | Where |
|---|---|---|
| Draw a colour band, clipped | `OSAPI_GFX_BLITP` (planes), `OSAPI_GFX_BLIT4` (packed), the window clip | SPEC.md 5.4 |
| Draw a mono band, clipped | `OSAPI_GFX_BLIT1` | SPEC.md 5.4.2 |
| GIF's LZW, hostile-input safe | `apps/os88lzw.inc`, shared source, input by `LZW_FILL`, strings to `LZW_RUN` | SPEC.md 106.18 |
| JPEG (baseline, scaled DCT) | PiXEL's part `apps/pixel/pxjpeg.asm`, 19,101 bytes | SPEC.md 106.19, 106.22 |
| PNG (inflate, filters, Adam7) | PiXEL's part `apps/pixel/pxpng.asm`, 12,018 bytes | SPEC.md 106.18, 106.26 |
| Truecolour to 256 entries | PiXEL's CUBE (6x7x6 + 4 greys), ordered blue-noise dither, 139 cycles a pixel on an 8088 | SPEC.md 106.8 |
| 256 entries to the screen | PiXEL's mixing PLANS `(c1, c2, t)` through an 8x8 Bayer; a luma threshold on 1bpp; the CUBE's and GREY's plans shipped as tables (`pxplans.inc`) | SPEC.md 106.11 |
| TIFF, ICO, IFF, MacPaint | PiXEL's part `apps/pixel/pxextra.asm`, 20,696 bytes | SPEC.md 106.25 |
| GIF animation rules | `gif_anim` in `tools/pixelsim.py`: delays, NETSCAPE2.0 loops, transparency, disposal 2/3, local tables, the dirty rect | SPEC.md 106.25 |
| Decode on a worker | PiXEL's pipeline: the UI task reads and claims, the worker only computes | SPEC.md 106.9, 20.6 rule 7 |
| Write/read a file by offset | `OSAPI_FILE_WRITE_AT`, `OSAPI_FILE_READ_AT`; `OSAPI_VOL_KIND` says floppy, fixed or RAM | docs/INDEX.md |
| EMS arenas that grow | the Browser's `br_ar_*` (SPEC.md 71.18), board-limited | browser.asm |

What an 8088 pays (PERFORMANCE.md, SPEC.md 106.17/106.22, measured on the
5150): a 640x480 baseline JPEG decodes in about 47 s at 1/2 scale; a
dithered full-canvas blit is about 1.1 s on VGA; a 96x72 animated GIF frame
is 3-5 drawing calls. **Decoding again on every scroll is therefore not an
option**, which is the main change from the 2026-10-17 plan.

## 2. Decisions

Each with what it was weighed against.

| # | Decision | Against | Why |
|---|---|---|---|
| D1 | **Decode once, keep the image READY TO DRAW**: on VGA/EGA the four planes `OSAPI_GFX_BLITP` takes, on CGA/Hercules 1bpp for `OSAPI_GFX_BLIT1`. Scrolling is a blit per band | decode again on every repaint (the old plan); keep an 8bpp master and compose at draw time (PiXEL's way) | Re-decoding costs seconds to tens of seconds a picture on an XT. PiXEL's composer (zoom, pan) is too large to sit in the Browser's resident image, and a page never zooms. A 1bpp copy is an eighth of an 8bpp one |
| D2 | **The ready copy goes to the best tier there is**: conventional memory inside the Browser's 20% reserve, then EMS, then a cache file on a FIXED or RAM volume, then the alt box | EMS required; disk always | EMS must never be required (owner). Most early-web images are small (a 200x60 logo is 6 KB in planes), so conventional memory carries a typical page on a 640 KB machine. A hard disk returns a picture in one or two `int 13h` calls (~0.4 s each), far below a re-decode |
| D3 | **No floppy cache.** A machine whose only writable volume is a floppy keeps what fits in memory and shows the rest as alt boxes (a small GIF may be re-decoded instead, section 4.4) | a cache file on A:/B: | Slow, often write-protected, a 360 KB disk fills, and the user swaps disks while browsing |
| D4 | **JPEG and PNG reuse PiXEL's decoder PARTS, by source**: `pxjpeg.asm` and `pxpng.asm` assembled a second time as parts of BROWSER.O88, against a link file generated from `browser.bin` (`tools/pxlink.py`'s output, with `-I` putting the Browser's first), and the seven `PXS_*` services implemented by the Browser | a Browser JPEG/PNG of its own; picojpeg | 31 KB of tuned, tested, hostile-input-safe decoder for about 1.6 KB of resident BSS (`px_spal` 768, `px_plan` 768, `px_cur` 52, a few flags) and the service thunks. Nothing is copied (WEAVE-SPEC 1.2's rule). **PiXEL is not edited** and keeps building and running standalone |
| D5 | **GIF is a Browser part of its own**, on `apps/os88lzw.inc`, following `pixelsim`'s `gif_anim` rules frame for frame, static and ANIMATED | PiXEL's `pxgif.asm` linked the same way | `pxgif` is fused to PiXEL's model: 40-odd linked variables (`px_an*`, the master's windows, the view) and a UI renderer service. Carrying that state would cost more resident BSS than the whole GIF part costs on disk (estimated 3-4 KB, lazy, 0 resident). `pixelsim` is the reference implementation the tests hold it to |
| D6 | **Animated GIFs play**, the visible ones, one frame at a time on the one worker; paused when scrolled off, when another window is in front, when minimised | first frame only | Owner. PiXEL already shows the engine works on an XT. With many animations a page plays slower than the file's delays, never with the UI blocked |
| D7 | **Every format's colours go through one shared conversion**: source row -> palette index (PAL as is, truecolour through the CUBE's dither) -> the display's plans -> planes or 1bpp, in the image part | per-decoder conversion | One tested path, the plans PiXEL already ships |
| D8 | **One resource fetch at a time**, after the page, in document order, visible images first; its own generation, URL and staging; never the page's source claim | one socket per `<img>` | The single-socket discipline and SPEC.md 71's ownership rules; View Source and Save Page As keep the page source intact |
| D10 | **The older formats the web carried until about 2000 are read too.** TIFF, ICO (favicons), IFF and MacPaint through PiXEL's part `pxextra.asm`, by source as D4 (it links `px_spal`, `px_plan`, `px_cur` and the ring's six words, all of which the Browser's services hold anyway). BMP (1/4/8/16/24/32 bpp, RLE4/RLE8), XBM (the X bitmap Netscape showed inline) and PCX/TGA in a small Browser part of its own | PiXEL's `pxsimp.asm` | `pxsimp` is linked into PiXEL's resident reader: it calls the package's own routines (`px_emit`, `px_tokget`, 60-odd symbols), so a second host would carry PiXEL's reader. Uncompressed and RLE formats are a few hundred bytes each of their own; XBM is not in PiXEL at all |
| D9 | **Without memory for an image, the page still lays out**: the box at its declared or header size with the alt text and a status line. One bad image never fails a page | refuse the page | The old plan's rule, kept |

## 3. Memory

The claims the Browser holds (MEM_OWNER_MAX = 8, BROWSER-1.7-ROADMAP.md 5)
grow by these, and the claims table in SPEC.md 71.23 is updated first:

| claim | when | size |
|---|---|---|
| image store (conventional) | while a page with images is shown | ONE movable claim for every image of the page, grown by the UI task within the reserve and carved per image; while a decode runs, its scratch is carved off the store's end (PiXEL's `PXK_DSC` per scale; the LZW's 18 KB) and given back after (SPEC.md 20.12, *One claim, carved*) |
| image part (a decoder, with the host code) | while decoding or animating | lazy part, dropped after |

The limit is a COUNT, not a size: MEM_OWNER_MAX is eight claims an owner
(SPEC.md 50.2), and a claim is rounded to 512 bytes, so a claim costs at
most a few hundred bytes more than it holds. EMS adds handles, not claims
(`br_ar_*`, eight on the machine), and the disk cache adds a file. At the
worst moment the Browser holds source, document, line table, links, URL
store, image store and image part: **seven**, so part 0 (Download Manager,
cold UI) still fits and neither waits for the other.

The disk cache: one file, `SYSTEM\APPDATA\BROWSER\IMGCACHE.TMP` on the
first fixed or RAM volume, written cluster-aligned with `OSAPI_FILE_WRITE_AT`,
emptied at navigation and at close. UI task only (SPEC.md 20.6 rule 7).

## 4. Deliveries

One reviewable commit each, on a branch of its own after the 1.7 preparation
PR. Rows are named per delivery; the soak union and `test-full` run once at
the end (CLAUDE.md, Testing).

### 4.1 Image boxes and alt text

`<img>` in the parser and layout: `width`/`height` attributes, `alt`, `align`
left/right/middle at the level early pages use, `border`, an image inside a
link is clickable over its whole box. With no attributes, a box of a fixed
placeholder size until 4.2 reads headers. Drawing: a frame and the alt text.

Verification: a fixture page through `tools/htmsim.py` (its reference layout
grows the same rule) and the brtest/brlink rows; the line records stay valid.

### 4.2 Static GIF

The resource fetch (D8), header-first sizing (a box re-laid when its header
arrives), the GIF part, the shared colour conversion (D7), the conventional
and EMS tiers of the store (D2), the blit on paint and scroll.

Verification: `tests/brcapacity.py --serve` serves the corpus; a row compares
the ready copy's bytes against `pixelsim` plus the plans, on VGA, EGA, CGA
and Hercules; malformed, cut short, interlaced, 1x1, local tables,
transparency; navigation during a fetch; low memory leaves alt boxes and
another app launchable.

### 4.3 The disk tier

The cache file on a fixed or RAM volume (D2, D3); eviction when conventional
memory is wanted back; the floppy-only machine keeps what fits.

Verification: QEMU with `TESTHD=` (a fixed disk) and without; MartyPC's
XT-IDE for timing in the final pass.

### 4.4 Animated GIF

The frame engine (D5, D6) on the worker, its 8bpp logical screen in the
store's tiers, the dirty rect converted and blitted, pause and resume rules.
A small GIF that cannot keep its ready copy may be re-decoded on exposure
instead of showing alt (its cost is measured first).

Verification: frame by frame against `gif_anim` (as PiXEL's `pxanim` rows
do); several animations on one page; scroll away and back; MartyPC frame
rate in the final pass.

### 4.5 JPEG and PNG

`pxjpeg.asm` and `pxpng.asm` as Browser parts (D4), the link file, the
services, the scale chosen to the box (JPEG's DCT scaling lands near the
page's size for free).

Verification: PiXEL's committed fixtures; the parts' bytes identical to
PiXEL's own build where the link file does not reach; PiXEL's own rows still
pass unchanged.

### 4.6 The older formats

D10: `pxextra.asm` as a Browser part (TIFF, ICO, IFF, MacPaint), and the
Browser's own simple part (BMP, XBM, PCX, TGA). The media type guides, the
signature decides (XBM has none: `#define name_width` is its text).

Verification: PiXEL's generated corpus (`tools/pixcorpus.py`) for every
format PiXEL reads, its bytes against `pixelsim`; XBM fixtures of its own,
`pixelsim` grown a reader for it.

### 4.7 Later

Save Image As through the Download Manager; image maps; colours and
backgrounds (`bgcolor`, `<font color>`) are their own plan.

## 5. Measurements to take

Resident bytes before and after each delivery (`tools/os88pkgsize.py`,
the roadmap's size table); conventional, EMS and disk bytes per image; first
pixels and whole-image time on MartyPC's 5150 for one GIF, one JPEG and one
animated GIF; drawing calls per scroll step.

## 6. Before code

1. SPEC.md 71: the `<img>` layout rules, the resource fetch's ownership, the
   store's tiers, the image part's interface, the claims table.
2. The image store's carving: how a decode's scratch comes off its end
   and goes back while images are being added (section 3).
3. The link-file generator for a second host (`tools/pxlink.py`), and a
   build check that the Browser's JPEG/PNG parts and PiXEL's come from one
   source.
