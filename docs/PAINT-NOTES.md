# Paint — design notes, and what the OS would need to do this properly

`apps/paint/paint.asm` is a bitmap editor written entirely against the
published package ABI (SPEC.md §20.3). **No kernel file was changed to make
it work** — not one byte of `kernel/`, and no new API slot. This document
records the two liberties it takes that the ABI does not sanction, the
capabilities whose absence cost the most, and how the picture formats it reads
and writes were arrived at.

## What it is

A canvas of any size the screen and memory allow — 448×280 by default,
resized by dragging the window's grow box, or set by opening a picture of its
own dimensions — 4bpp packed, with eight tools: pencil, eraser, dropper,
rectangle, ellipse, selection, flood fill and text.
Sixteen colours on VGA, three on a 1bpp adapter (SPEC.md §39.4's black /
dither / white classes — the only three that survive the reduction as
distinct). Selectable line width, per-tool: the pencil's 1/2/4/8 and the
eraser's 8/16/24/32, and the same selection sets the border thickness of an
unfilled rectangle or ellipse. Text is drawn into the picture from the ROM
8×8 font at 1×, 2× or 4×. One-level undo that doubles as redo, an internal
clipboard (cut/copy/paste), and BMP **and GIF** load/save through the Standard
File dialog (SPEC.md §38). Ctrl+Z, Ctrl+C, Ctrl+X, Ctrl+V and Delete reach the
same routines the Edit menu does — they are the control codes int 16h already
hands W_ONKEY, so no scan-code decoding is involved.

Shrinking the window never silently eats the picture: the rows or columns
about to go are checked for ink first, per axis, and a dirty axis keeps its
size while the other one still moves. When that happens the frame is written
back to fit the canvas and the status toast says why.

## The two liberties it takes

**1. It claims conventional memory above `BB_SEG`** — from linear 0x66000 up —
for the canvas, an equally-sized undo image, the clipboard and a 12KB scratch
area. There is no other place for them: a package's whole world is
`APP_MAX_SIZE` = 19.5KB of image + bss (SPEC.md §20.1), and even a modest
canvas is 60KB. Nothing is a fixed size any more: `pt_geom` divides what int
12h reports, so the same binary runs a 101KB canvas on a 640KB machine, a
39KB one at 512KB, and refuses below about 499KB.

0x66000 is the first paragraph above the four back-buffer planes (SPEC.md §2:
`BB_SEG` + 4 × 0x9600 ends at 0x657FF), so the Control Panel can arm or disarm
double buffering underneath the app with no effect. Nothing else in the tree
touches memory above 0x40000.

Three consequences are handled rather than hoped about:

- **The memory may not be there.** `pt_entry` asks int 12h first and, when the
  answer cannot fund a minimum canvas, opens a window that says "Not enough
  memory" and touches nothing. A 256KB or 384KB machine gets that notice;
  `make run-640`, a 512KB machine and any real 640KB XT get the app, with the
  canvas ceiling scaled to what they have.
- **Two instances would share one canvas.** The claim record at the scratch
  base holds a magic pair and the owner's window pointer, and `pt_dupchk`
  believes it only if that pointer still names a used window slot whose title
  starts "Paint". A closed Paint leaves the magic behind — there is no close
  hook (see below) — and that test is what keeps the staleness harmless. In
  practice the loader refuses first: two 12KB regions do not fit the 19.5KB
  pool, so the second launch fails with "Out of memory" in the Disk window
  before the app runs at all.
- **The claim is invisible to the Task Manager**, which sums package regions
  and the kernel's own segments. Paint's quarter-megabyte does not appear in
  its RAM figure.

**2. It writes W_W/W_H in the window record.** Opening a picture makes the
window match the picture, and there is no `wm_resize` slot to ask for that —
`ui_grow` and `wm_fullscreen` are the only things that resize a window, and
both are kernel-internal. So Paint stores the new frame in the record and calls
`OSAPI_WM_FRONT` for the repaint, which *is* sanctioned. The record's geometry
is documented as no longer set-once (SPEC.md §11), so this is a small liberty
rather than a violation — but it is a liberty, and a one-line
`wm_resize(BX, w, h)` that clamped to the screen and repainted would retire it.

The same write is what lets a resize be *refused*: when shrinking the window
would crop artwork, `pt_wfix` puts the frame back to what the canvas needs
(clamping x/y on screen and above the dock the way `ui_drag` does) and the
toast explains why. There is no sanctioned way to say no to `ui_grow` — it has
already rewritten the record and repainted by the time the app sees anything —
so the app corrects it afterwards and pays one `OSAPI_WM_FRONT` to put the
corrected frame up. A resize callback that could return "refused" would be the
clean version, and would cost nothing; see the smaller gaps below.

**What the kernel should provide for the memory:** a slot in the API table —
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
buffer with a 16-bit count, and `FERR_BIG` refuses anything over 64KB. Since
the canvas became resizable this is the limit users meet first, and it cuts
twice:

- **A picture larger than 64KB cannot be read at all.** Not "read partially" —
  there is no positioned read, so a 155KB BMP is simply refused. The request
  that motivated the resizable canvas ("load as much of an oversized bitmap as
  will fit") is therefore honoured for *dimensions* — a 700×440 file opens
  cropped to what the screen and memory allow — but cannot be honoured for
  *file size*.
- **A canvas whose BMP would pass 64KB cannot be saved.** Paint edits a
  594×342 picture (102KB) perfectly well; writing it needs a call the API does
  not have, so it reports "Too big to save (64KB limit)" rather than writing a
  truncated file.

The canvas is still laid out *as* the file — a 118-byte DIB header in front of
bottom-up rows — so a save that does fit is one `OSAPI_FILE_WRITE` of the
canvas base with no staging copy at all.

A positioned read/write (`read(name, offset, len, buf)`) or a real
open/seek/close would lift both limits, and would also let a decoder stream
instead of demanding the whole file in RAM at once.

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
- **No clipping to a window** for foreground drawing. SPEC.md §11.3's clip
  region is for background painters (its rule 3), and Paint has no worker
  task, so every canvas coordinate is still clipped by the app before it
  becomes a gfx call — `pt_clip`, on every dab. The region did improve one
  thing for free: a background painter *underneath* Paint now draws its own
  visible part instead of skipping the frame, which makes the drag loops'
  released-lock windows better behaved than when this was written.
- **No resize callback.** A resizable window learns it was resized by finding
  a different W_W/W_H at its next paint (SPEC.md §11.1). That works, but the
  size is adopted *during* the paint — after the kernel has already drawn the
  title bar — which is why the canvas dimensions are printed in the tool
  palette rather than the title, where they would always be one repaint
  stale, at the cost of a second full repaint to fix. A callback that ran
  *before* the paint, and whose refusal `ui_grow` honoured, would also let the
  crop guard reject a drag without the app having to rewrite the record behind
  the kernel's back and put the notice up a paint later.
- **No way to hand the menu bar back, and no modal gate.** `menu_activate` is
  kernel-internal; the only slot that reaches it is `OSAPI_WM_FRONT`, which
  repaints everything. And `fdlg_grab`, which swallows every press outside the
  file dialog's rect (SPEC.md §38), has no equivalent for a window a package
  creates. Between them those two gaps are why the crop refusal ended up as a
  toast rather than a dialog: a package's own modal-ish window would have cost a
  repaint to raise, another to dismiss, a window slot, a click, and would still
  have left the bar reading "Locator" afterwards. The kernel's own dialog has
  neither problem — `fdlg_close` calls `menu_activate` + `menu_draw_bar`
  directly. A `menu_activate(BX)` slot and a package-visible modal gate would
  make an app-authored dialog affordable; until then a status line is the
  cheaper and better-behaved answer, which is worth knowing before writing one.

## Formats

**BMP is implemented both ways.** Save writes a 16-colour 4bpp DIB with the
standard EGA palette, at whatever size the canvas is, which round-trips
through any host paint program — verified byte-exact against a Pillow reader.
Load accepts 1, 4, 8 and 24bpp uncompressed BMPs, top-down or bottom-up,
adopts the file's dimensions as far as the screen and memory allow, crops the
rest, and maps any source palette (or true colour) to the nearest of the
sixteen by a weighted city-block distance. A cropped load blocks File > Save,
so one click cannot overwrite the original with less than it held. Compressed
(RLE4/RLE8) BMPs are refused with a message rather than misread.

**GIF is implemented both ways.** Reading takes 2..8-bit minimum code sizes,
global or local colour tables, interlaced or sequential, and skips extension
blocks; writing emits GIF87a with our sixteen colours as the global table and a
4-bit minimum code size. Verified pixel-exact in both directions against a host
decoder — including files a host tool wrote interlaced, a 256-colour gradient
mapped down to sixteen, an oversized picture cropped on load, and pictures Paint
wrote and read back itself.

Two things about it are worth knowing before touching it:

- **The dictionary is 16KB, so it lives in borrowed memory.** The clipboard's
  reserved floor holds the read tables exactly (prefix, suffix, output stack)
  and the smaller write tables with room over; the file being read, or built,
  goes in the undo image. Both are already invalidated by the operation, and two
  build-time assertions fail the build if that floor stops being big enough.
- **Writing and reading are not mirror images**, and the asymmetry is real
  rather than a bug: a writer defines a new string as it emits the code before
  it, a reader cannot until it has seen the code after it, so the reader's table
  runs one entry behind and the two code-width rules are off by one on purpose.
  It is written up in full at `pt_gadd`. Pure noise is the input that exercises
  it, because it is the only kind that fills the table fast enough to force a
  Clear code mid-stream — flat drawings never get there, which is exactly why
  that bug survived the first three test pictures.

**JPEG is still out of reach**, and not because of code size: a baseline
decoder is Huffman + dequantise + IDCT + upsample + colour convert, and on a
4.77MHz 8088 with no hardware multiply worth the name that is tens of seconds
for a single 448×280 frame, before the dither to sixteen colours. An encoder is
worse. The 64KB file ceiling also means the only JPEGs that could be opened at
all are small ones. The app recognises it by magic and says so rather than
guessing.

(The package is 12,631 bytes of image + 4,293 of bss against the 19,968
budget today, so about 3KB is still free.)

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

A third decision arrived with the resizable canvas: **the size readout is
content, not chrome.** In the title bar it cost a second full repaint per
resize, because the kernel draws the title before calling W_PAINT — and a full
repaint is the most expensive thing this app does. Two `font_str` calls in the
palette column cost nothing and are never stale.

What GIF costs in time, and what it buys: LZW is per-pixel work in both
directions, so a 448×280 picture is about 125,000 trips through a dictionary
walk — a few seconds either way under QEMU, comparable to the BMP path, which
spends its time on the floppy instead. In return, the 448×280 test picture is
62,838 bytes as a BMP and 1,285 as a GIF; since the file API refuses anything
over 64KB (above), GIF is the only format in which a large canvas can be saved
at all.

Measured under QEMU with `make run-640`: a full-canvas flood fill of a picture
with obstacles completes in about four seconds of wall clock, opening a 448×280
4bpp BMP (read, decode, blit) in about eight, and a stroke keeps up with the
1200-baud mouse with the CPU to spare. A real 8MHz machine will be several
times slower; a 4.77MHz 8088 slower again.
