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
at most 65,024 bytes (SPEC.md 20.1.1), with 11,374 free at the start of 1.7
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
| D2 | **The ready copy goes to the best tier there is**: conventional memory inside the Browser's 20% reserve, then EMS, then a cache file (D3), then the alt box | EMS required; disk always | EMS must never be required (owner). Most early-web images are small (a 200x60 logo is 6 KB in planes), so conventional memory carries a typical page on a 640 KB machine. A hard disk returns a picture in one or two `int 13h` calls (~0.4 s each), far below a re-decode |
| D3 | **A cache file per page view, in `SYSTEM\APPDATA\BRCACHE\` on the Browser's volume, floppy included** (owner, 2026-11-10; it replaced "no floppy cache"). On a FIXED or RAM volume it also keeps the last pages' copies, the ones in memory and EMS too, so Back and History redraw without a fetch or a decode, up to a count of pages and then oldest first, like a security camera's loop. On a floppy it holds the current page only, after a free-space check and with the disk's identity checked before every read | no floppy cache (the first plan); one `IMGCACHE.TMP` on a fixed volume only | A JPEG is about 47 s to decode on a 5150 and a 32 KB copy is 1-3 s off a floppy, so for JPEG and PNG the floppy wins; GIF alone made the first plan's case. Write protection, a full disk and a swapped disk are each checked rather than avoided |
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

The disk cache (D3), UI task only (SPEC.md 20.6 rule 7):

- **Where.** `SYSTEM\APPDATA\BRCACHE\` on the Browser's own volume, beside
  `BROWSE.DAT` (`apps/os88data.inc`'s walk, `OSAPI_FILE_MKDIR` once).
  `OSAPI_VOL_KIND` says floppy, fixed or RAM.
- **Names are 8.3** (FAT12/16 has no long names): `Bkhhhhhh.BRC`, k the
  instance (several Browsers may be open) and hhhhhh the page view's
  number. The file is written cluster-aligned with `OSAPI_FILE_WRITE_AT`
  and read with `OSAPI_FILE_READ_AT`. Its header names the page's URL, and
  each copy's entry its `src`, COLS and ROWS, so a revisit takes a copy only
  where all three still match.
- **Fixed or RAM: a loop of pages.** Every ready copy of the page is written
  as it becomes ready, whichever tier holds it, so the file is the page's
  whole set. The last `BR_CACHEPAGES` views (8, the History menu's length)
  are kept, within a byte cap and a free-space margin; past either, the
  oldest view's file is deleted first. Back, Fwd and History look a URL up
  in the loop before fetching any image.
- **Floppy: the current page only.** Written only for copies that fit
  neither the reserve nor EMS, after `OSAPI_FILE_DFREE` (no disk I/O) says
  the copy and a margin fit. A refused write (write-protected, full) turns
  the cache off for the page, with no message. The volume's serial and label
  are kept and checked before every read, since `DFREE` cannot see a swapped
  disk: a different disk makes the copy none rather than read the wrong
  file or write to it. The view's file is deleted at navigation.
- **Is the copy still the server's? (owner, 2026-11-10)** Each copy's
  entry also keeps the image's `Last-Modified`, `ETag` and
  `Content-Length`. Back, Fwd and History trust the file for the session,
  as Netscape's default *Verify documents: Once per session* did, and the
  session ends at close, when the files go. A Reload, or the same URL
  opened again from the bar, asks again with `If-Modified-Since` /
  `If-None-Match`: `304 Not Modified` keeps the copy with no body and no
  decode, `200` replaces it, and an image whose server sent neither header
  is fetched and decoded again. `Expires` and `max-age` are not read.
- **Cleanup.** At close each instance deletes its own files. At start, a
  Browser that finds no other instance running empties the folder, which
  clears what a crash or a power-off left behind.

## 4. Deliveries

One reviewable commit each, on a branch of its own after the 1.7 preparation
PR. Rows are named per delivery; the soak union and `test-full` run once at
the end (CLAUDE.md, Testing).

### 4.1 Image boxes and alt text - done

Done (SPEC.md 71.25): the record, the unbreakable unit, the continuation
rows and the frames, on VGA and CGA, scroll by blit, a click in a linked
box's top row follows the link. 1,803 bytes of image + BSS (43,958 +
11,505 = 55,463; 9,561 free). Row: `brimages` (`tests/brtest.py --page
IMAGES.HTM`, the fixture `tests/htm/images.htm`).


`<img>` in the parser and layout: `width`/`height` attributes, `alt`, `align`
left/right/middle at the level early pages use, `border`, an image inside a
link is clickable over its whole box. With no attributes, a box of a fixed
placeholder size until 4.2 reads headers. Drawing: a frame and the alt text.

Verification: a fixture page through `tools/htmsim.py` (its reference layout
grows the same rule) and the brtest/brlink rows; the line records stay valid.

### 4.2 Static GIF

Conventional tier done (SPEC.md 71.26): the fetch after the page, the
header's size and a re-layout, part 1 with the GIF reader on `os88lzw.inc`
and PiXEL's plan search assembled from `apps/pixel/pxplan1.inc`, the ready
copy (four planes or 1bpp) and its blit on paint and scroll, the stop on a
navigation. 1,996 bytes of image + BSS (45,211 + 12,248 = 57,459; 7,565
free); part 1 is 7,712 bytes in the file. Rows: `brgif`, `brgifcga`,
`brgifreload`.

EMS tier done (SPEC.md 71.26, The EMS tier): when the store cannot grow, its
ready copies move to an EMS arena of §71.18's shape and the store keeps only
the scratch; the paint maps a slice's two pages into the frame. 54 bytes
(57,513, 7,511 free). Row: `brgifems` (86Box, a 384 KB XT with a card and a
board: ten of twelve copies in EMS, every EMS slice byte-identical through
the frame).

The resource fetch (D8), header-first sizing (a box re-laid when its header
arrives), the GIF part, the shared colour conversion (D7), the conventional
and EMS tiers of the store (D2), the blit on paint and scroll.

Verification: `tests/brcapacity.py --serve` serves the corpus; a row compares
the ready copy's bytes against `pixelsim` plus the plans, on VGA, EGA, CGA
and Hercules; malformed, cut short, interlaced, 1x1, local tables,
transparency; navigation during a fetch; low memory leaves alt boxes and
another app launchable.

### 4.3 The disk tier

4.3a done (SPEC.md 71.27): the disk as the store's last tier, floppy
included - the copies the reserve and EMS refuse appended to
`SYSTEM\APPDATA\BRCACHE\Bssssvvv.BRC`, read back through an 8 KB paint
buffer, the file deleted with its page and at an accepted close, a lone
start sweeping the folder. 433 bytes (57,946, 7,078 free). Row
`brgifdisk`. Next, 4.3b: the loop of the last pages on a fixed or RAM
volume and the revalidation.

4.3b done (SPEC.md 71.28): on a fixed or RAM volume every copy is kept in
the page's file behind a 32-slot index, the last 8 pages in a loop, Back /
Fwd / History draw them with no request, and Reload or a typed address asks
with If-Modified-Since / If-None-Match, a 304 keeping the copy. The 4.3a
spill writes as many whole copies as the disk holds and grows the store
for its paint buffer when the copies are fewer than 8 KB. 369 bytes
(58,315, 6,709 free). Row `brkeep`.

The cache files of section 3 (D2, D3): a file per page view in
`SYSTEM\APPDATA\BRCACHE\`; on a fixed or RAM volume the loop of the last
pages, every copy written, Back/Fwd/History reading it before any fetch,
a Reload or a typed URL asking again with `If-Modified-Since` /
`If-None-Match`;
on a floppy the current page's overflow only, with the free-space check, the
write-protect refusal and the disk's identity checked before every read;
the cleanup at close and at a lone start.

Verification: QEMU with `TESTHD=` (a fixed disk): a revisit through Back
draws every copy from the file with no image request reaching the server,
and the ninth page deletes the first page's file; on the floppy, a page that
outgrows memory reads its overflow back, a write-protected disk leaves alt
boxes and no error, and a swapped disk makes the copy none; the folder empty
after close. MartyPC's XT-IDE for timing in the final pass.

### 4.4 Animated GIF

Done (SPEC.md 71.29): after the page's images, the animated ones still in
the store are fetched again and played on the worker, a master per image
behind the copies, each frame held to `gif_anim` - every disposal, the
nearest-entry mapping of a frame's table, the loop count - and paused while
nothing draws them. 101 bytes (58,416, 6,608 free). Row `brganim`. Not
done: an animation in EMS or the file (it stays on frame 0); the dirty
slices alone (the image is drawn whole); re-decoding a small GIF on
exposure instead of keeping it.

4.4b (SPEC.md 71.29, Set up as it arrives / Held in the store): the
animation is set up from the fetch that drew frame 0 - the staged file is
moved past where the copy goes, so the decode writes the copy in place and
the file stays as the block's start - so an animated GIF is fetched once,
not twice. The spills take only the copies no animation holds and close the
store up round the rest (`bri_compact`); an animation is let go only when
nothing else is left to move. No resident byte; part 1 +641 (25,738).

The frame engine (D5, D6) on the worker, its 8bpp logical screen in the
store's tiers, the dirty rect converted and blitted, pause and resume rules.
A small GIF that cannot keep its ready copy may be re-decoded on exposure
instead of showing alt (its cost is measured first).

Verification: frame by frame against `gif_anim` (as PiXEL's `pxanim` rows
do); several animations on one page; scroll away and back; MartyPC frame
rate in the final pass.

### 4.5 JPEG and PNG

Done (SPEC.md 71.30). PiXEL's `pxpng.asm` and `pxjpeg.asm` are parts 2 and 3
of BROWSER.O88, by source, and **their host is part 1** rather than the
resident Browser (D4 had the Browser's bss hold the 1.6 KB of names): the
names a decoder reads are a block of part 1's own data, linked by
`tools/brpxlink.py` from part 1's map, and the services are part 1's. The
resident Browser grew 81 bytes (the two part rows and the fetch and drop
shims); part 1 holds the host. PiXEL's parts are byte-identical; the one
edit to its source is `PXP_BG` made `%ifndef`, so the Browser's assembly
composites over white. The JPEG's scale is the coarsest the IDCT offers whose
master still covers the box. Row `brpx`: 13 pictures, every copy byte for
byte its reference. The 25 KB on the disk moved REDLINE to more720.img and
the Browser off apps720.img and apps360.img (the owner's decision,
2026-11-11).

`pxjpeg.asm` and `pxpng.asm` as Browser parts (D4), the link file, the
services, the scale chosen to the box (JPEG's DCT scaling lands near the
page's size for free).

Verification: PiXEL's committed fixtures; the parts' bytes identical to
PiXEL's own build where the link file does not reach; PiXEL's own rows still
pass unchanged.

### 4.6 The older formats

Done (SPEC.md 71.31). Not as D10 drew it: `pxsimp.asm` is reused rather
than written again, because since PiXEL's 106.25 it names only its host's
VARIABLES - part 1's host block grew by them - so parts 4 and 5 are PiXEL's
EXTRAS and SIMPLE FORMATS, by source, and part 6 is the Browser's own XBM
(`apps/browser/brxbm.asm`, 628 bytes; `pixelsim` grew `xbm_header` and
`xbm_rows`). The signature decides and, without one, the URL's extension
(TGA, PCX, a plain MacPaint, XBM); the media type is still not read. A PNG
inside an ICO keeps its alt box. Row `brold`: 19 pictures, every copy byte
for byte its reference.

D10: `pxextra.asm` as a Browser part (TIFF, ICO, IFF, MacPaint), and the
Browser's own simple part (BMP, XBM, PCX, TGA). The media type guides, the
signature decides (XBM has none: `#define name_width` is its text).

Verification: PiXEL's generated corpus (`tools/pixcorpus.py`) for every
format PiXEL reads, its bytes against `pixelsim`; XBM fixtures of its own,
`pixelsim` grown a reader for it.

### 4.7 Save Image As and image maps

Done (SPEC.md 71.32): Save Image As from the context menu through the
Download Manager, client-side maps (`usemap`, `<map>`, `<area>`: rect,
circle, poly, default) and server-side `ismap`. Row `brmap`. Colours and
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
