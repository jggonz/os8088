# Paint — design notes, and what the OS would need to do this properly

`apps/paint/paint.asm` is a bitmap editor written entirely against the
published package ABI (SPEC.md §20.3). **No kernel file was changed to make
it work** — not one byte of `kernel/`, and no new API slot. This document
records the two things that could not follow from the ABI alone, the four
capabilities whose absence cost the most, and the formats that were dropped
and why.

## What it is

448×280 canvas (shorter on a short screen), 4bpp packed, eight tools:
pencil, eraser, dropper, rectangle, ellipse, selection, flood fill and text.
Sixteen colours on VGA, three on a 1bpp adapter (SPEC.md §39.4's black /
dither / white classes — the only three that survive the reduction as
distinct). Selectable line width, per-tool: the pencil's 1/2/4/8 and the
eraser's 8/16/24/32, and the same selection sets the border thickness of an
unfilled rectangle or ellipse. Text is drawn into the picture from the ROM
8×8 font at 1×, 2× or 4×. One-level undo that doubles as redo, an internal
clipboard (cut/copy/paste), and BMP load/save through the Standard File
dialog (SPEC.md §38).

## The one thing it does that the ABI does not sanction

**It claims four 64KB windows of conventional memory above `BB_SEG`** —
linear 0x66000, 0x76000, 0x86000, 0x96000 — for the canvas, the undo image,
the clipboard and a scratch area. There is no other place for them: a
package's whole world is `APP_MAX_SIZE` = 19.5KB of image + bss (SPEC.md
§20.1), and the canvas alone is 62,720 bytes.

0x66000 is the first paragraph above the four back-buffer planes (SPEC.md
§2: `BB_SEG` + 4 × 0x9600 ends at 0x657FF), so the Control Panel can arm or
disarm double buffering underneath the app with no effect. Nothing else in
the tree touches memory above 0x40000.

Three consequences are handled rather than hoped about:

- **The memory only exists on a large machine.** `pt_entry` asks int 12h
  first and, below 620KB, opens a window that says "Not enough memory" and
  touches nothing. A 256KB or 512KB machine gets that notice; `make run-640`
  and any real 640KB XT get the app.
- **Two instances would share one canvas.** The claim record at
  `PT_SCSEG:0` holds a magic pair and the owner's window pointer, and
  `pt_dupchk` believes it only if that pointer still names a used window
  slot whose `W_TITLE` string is ours. A closed Paint leaves the magic
  behind — there is no close hook (see below) — and that test is what keeps
  the staleness harmless. In practice the loader refuses first: two 11KB
  regions do not fit the 19.5KB pool, so the second launch fails with "Out
  of memory" in the Disk window before the app runs at all.
- **The claim is invisible to the Task Manager**, which sums package
  regions and the kernel's own segments. Paint's 250KB does not appear in
  its RAM figure.

**What the kernel should provide instead:** a memory slot in the API table —
`alloc(paragraphs) → segment` / `free(segment)`, stamped with the calling
instance and force-freed by `inst_release` the way sound grants already are
(SPEC.md §34.6 verb 7 is the exact precedent, including the teardown fence).
The allocator itself is a handful of records; what matters is that the
*kernel* owns the map, so two packages cannot pick the same address and the
Task Manager can bill it. `docs/MEMORY-PLAN.md` Step D (packages into their
own segments) is the adjacent step and would want the same allocator.

## The three capabilities whose absence cost the most

### 1. No bitmap blit

`gfx_*` draws rectangles, lines and glyphs. There is no way to hand the
kernel a block of pixels. Every canvas repaint therefore goes through
`pt_blit`, which coalesces runs of equal pixels and emits one `gfx_hline`
per run — `repe scasb` over the packed bytes, so a flat region costs about
seven cycles a pixel and one call a run. That is fast enough (a blank
canvas is 280 calls) but it is the wrong shape for a photograph, where the
runs are one or two pixels and the call overhead dominates.

A `gfx_blit4` slot — packed 4bpp source, destination rect, adapter-aware,
back-buffer-aware — would make a full repaint 3–5× faster on VGA and would
let any future imaging app skip the coalescer entirely. It is also the one
addition that would let the canvas be larger than the screen without the
repaint becoming the bottleneck.

### 2. The file API is whole-file and caps at 64KB

`dskw_read`/`dskw_write` (SPEC.md §18.4) move an entire file through one
buffer, and `FERR_BIG` refuses anything over 64KB. That single constant is
what fixes the canvas at 448×280: a 4bpp BMP of that size is 62,838 bytes,
and the canvas is laid out *as* that BMP — a 118-byte DIB header in front
of bottom-up rows — so a save is one `OSAPI_FILE_WRITE` of the segment with
no staging copy at all. One pixel wider and the file would not fit.

A positioned read/write (`read(name, offset, len, buf)`) or a real
open/seek/close would lift both limits: bigger canvases, and formats whose
decoder wants to stream rather than see the whole file at once.

### 3. No teardown callback for a package

Closing a task-less package's window is a synchronous `wm_destroy` +
`I_STATE = 0` (SPEC.md §21/§29). The package is never told. Anything it
owns outside its own region — memory it claimed, a file it was writing,
state another instance can see — leaks or goes stale, and the only defence
is a liveness test like `pt_dupchk`'s. A `KD_QUIT`-style optional entry in
the package header, called under the lock before the region is freed, would
cost the kernel one indirect call and would make an allocator (above) safe
by construction.

## Smaller gaps, in order of how much they cost here

- **No access to the kernel's font bitmaps.** `font_char` draws to the
  screen; the text tool has to write glyph pixels into its own canvas, so
  `pt_font_init` re-fetches the ROM 8×8 font through int 10h AX=1130h and
  keeps a second 760-byte copy of what the kernel already has in `.bss`. A
  slot returning the glyph table pointer would remove both.
- **No mouse-motion events.** `W_ONCLICK` fires on the press and nothing
  else, so every drag is a poll loop that owns the gfx lock — and, because
  a package's drawing goes through the back buffer, the loop must
  *release* the lock each pass so `gfx_unlock`'s flush reaches the glass.
  That inverts `ui_drag`'s ordering (SPEC.md §13) and is worth writing down
  for the next app that tracks a drag.
- **No menu item state.** `AM_ONCMD` sets have no check mark, no disable,
  no radio group, so "Filled Shapes" and the text size are mirrored as
  clickable indicators in the app's own strip and the menu items are
  write-only twins of them.
- **No clipping to a window.** `vga_rect_setup` clips to the screen
  (SPEC.md §39.10), so every canvas coordinate is clipped by the app before
  it becomes a gfx call. That is `pt_clip`, and it is called on every dab.

## Formats

**BMP is implemented both ways.** Save writes a 16-colour 4bpp DIB with the
standard EGA palette, which round-trips through any host paint program.
Load accepts 1, 4, 8 and 24bpp uncompressed BMPs, top-down or bottom-up,
mapping any source palette (or true colour) to the nearest of the sixteen
by a weighted city-block distance. Compressed (RLE4/RLE8) BMPs are refused
with a message rather than misread.

**GIF and JPEG are not implemented.** The app recognises both by their
magic bytes and says "Only BMP is supported" instead of guessing.

- **JPEG is out of reach**, and not because of code size: a baseline
  decoder is Huffman + dequantise + IDCT + upsample + colour convert, and
  on a 4.77MHz 8088 with no hardware multiply worth the name that is tens
  of seconds for a single 448×280 frame, before the dither to sixteen
  colours. An encoder is worse. The 64KB file ceiling also means the only
  JPEGs that could be opened at all are small ones.
- **GIF is feasible and was deferred, not refused.** The LZW decoder is
  about 700 bytes of code plus a 16KB dictionary, and the scratch segment
  already has room for the dictionary; the current package is 8,899 bytes
  of image + 2,314 of bss against the 19,968 budget, so roughly 8.7KB is
  free. Read support would fit comfortably. Writing needs a ~20KB hash
  table (also affordable) and about 600 more bytes of code, and buys
  nothing over BMP for round-tripping — a non-compressing LZW stream is
  *larger* than the raw 4bpp bytes, so a real compressor is the only useful
  version. Say the word and it goes in.

## Performance notes

The two decisions that carry the app:

- **Nothing repaints more of the screen than it changed.** A brush stroke
  emits only the pixels the dab uncovered: `pt_seg` walks the line and
  draws the square brush's *leading edge* — one `gfx_fill` per step, never
  a re-dab — so a stroke costs its own area once, not width² per pixel of
  travel. At the eraser's 32px that is the difference between usable and
  unusable. A flood fill emits its spans as it finds them, so the fill IS
  the repaint. A paste writes pixels with no gfx calls at all and blits
  once at the end.
- **Undo is row-granular and lazy.** `pt_umark` copies a canvas row into
  the undo image the first time an operation touches it, so a small stroke
  pays for the handful of rows it crosses and only a whole-canvas operation
  pays 62,720 bytes. Undo and redo are the same instruction: the marked
  rows are *exchanged* with the undo image, so the two states alternate
  forever from one buffer.

Measured under QEMU with `make run-640`: a full-canvas flood fill of a
picture with obstacles completes in about four seconds of wall clock,
opening a 448×280 4bpp BMP (read, decode, blit) in about eight, and a
stroke keeps up with the 1200-baud mouse with the CPU to spare. A real
8MHz machine will be several times slower; a 4.77MHz 8088 slower again.
