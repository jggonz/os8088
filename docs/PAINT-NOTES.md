# Paint — design notes, and what the OS would need to do this properly

`apps/paint/paint.asm` is a bitmap editor, contributed as a fork of os8088
by [Elendilon](https://github.com/Elendilon) and ported to the v3 package
ABI. This document records what it needed from the kernel, what it got, the
capabilities whose absence still cost the most, and how the picture formats
it reads and writes were arrived at.

**The two liberties this file was written to record are gone.** As written
against v2 it claimed conventional memory above `BB_SEG` by picking an
address, and it wrote `W_W`/`W_H` in the kernel's window record by hand.
Neither is possible in v3 — a package's DS cannot reach the record, and the
arena at 0x65800 is where every package segment now comes from, so an
address Paint picked would be an address the loader had granted somebody
else. What the notes asked for is what the port added:

- **`alloc(paragraphs) → segment` / `free(segment)`, stamped with the
  calling instance and force-freed at teardown** — `OSAPI_MEM_ALLOC` /
  `OSAPI_MEM_FREE`, SPEC.md §2.6/§20.7, built on `kernel/cmem.inc`. The
  §34.6 sound-grant precedent named here is exactly the shape it took,
  teardown fence included.
- **A `largest_free` query** — `OSAPI_MEM_AVAIL`, which answers the largest
  contiguous run and the total separately, because an app must size itself
  from the first and not the second.
- **A `wm_resize` that clamps to the screen** — `OSAPI_WM_RESIZE`, SPEC.md
  §11.1, which retires the record write and is also how a grow-box drag is
  refused.

Two of the three reciprocal concerns in the old text answered themselves.
The Task Manager **does** bill the grant now (`cm_inst_mem`), so Paint's
quarter megabyte is no longer invisible. And `bb_set` never has to refuse:
`ARENA_SEG` is one paragraph above the back buffer's pinned extent by
construction, so the two can never want the same paragraph and Paint no
longer reads `DB_MIN_KB` to guess whether the kernel is going to.

What is still missing is the **teardown callback** (below): a task-less
package is never told its window is closing. The allocator is safe without
one only because the *kernel* force-frees; an app that owned anything the
kernel does not know about still has no way to clean it up.

## What it is

A canvas of any size the screen and memory allow — 448×280 by default, resized
by dragging the window's grow box or by typing into the `W`/`H` fields under the
tools, or set by opening a picture of its own dimensions — 4bpp packed, with
eight tools: pencil, eraser, dropper, rectangle, ellipse, selection, flood fill
and text.
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

The app's **name in the menu bar is itself a menu**, and `About Paint` lives
in it — the Macintosh place for it. That needed no kernel change: SPEC.md
§12.2 draws `AM_NAME` and then the app's cells to its right, so a set whose
name is the empty string and whose first menu is titled "Paint" puts a
pull-down exactly where the label would have been. The About window is a
second, instance-less window of the file dialog's species (§38): no dock
tile, no Task Manager row, close reduces to `wm_hide`, and Paint's teardown
sweeps it by segment stamp. Its command is dispatched ahead of the mode
test, so a machine too small to fund a canvas can still read it.

Shrinking the window never silently eats the picture: the rows or columns
about to go are checked for ink first, per axis, and a dirty axis keeps its
size while the other one still moves. When that happens the frame is written
back to fit the canvas and the status toast says why.

## What the memory looks like now

One `OSAPI_MEM_ALLOC` grant, sliced into canvas, undo image, clipboard and a
12KB scratch area. `pt_geom` asks `OSAPI_MEM_AVAIL` for the largest free run,
divides it in three tiers — clipboard first to go, then undo — and asks for
exactly what it decided. Nothing is a fixed address and no kernel constant is
read to derive one.

It deliberately leaves `PT_RESERVEP` (64KB, one whole package region) behind
when doing so still funds the top tier. Without that, a canvas plus an equal
undo image eats a 233KB arena whole and no other package can load while Paint
is open — a true answer to a question nobody asked. With it, the default
448×280 canvas still fits and Minesweeper still launches alongside.

Two instances get two grants, so the v2 claim record and its liveness test
(`pt_dupchk`, which existed because a fixed address had to be arbitrated and
a closed Paint left a stale magic word behind) are gone outright. A closed
Paint's grant is force-freed by the kernel, which is the only thing that
could have freed it: nothing tells a task-less package it is closing.

**Two bugs this shook out of the kernel, worth knowing.** `ld_alloc` reserves
a package's region at SPEC.md §21 step 5, but the instance record is not
published until step 10, after the entry proc returns. Paint was the first
package to call `OSAPI_MEM_ALLOC` from its entry — and the allocator, whose
only evidence is `I_STATE`, handed it `ARENA_SEG`: the segment it was
executing in. It filled its canvas with white and the machine wedged
mid-repaint on the first `0xFF` opcode, gfx lock held. `cm_hold`/`cm_unhold`
(SPEC.md §2.6) is the reservation for that window.

And the canvas is the first disk buffer in the tree that is not 512-aligned
by accident. `dsk_xfer` does one sector per int 13h call, which the spec took
to mean "DMA alignment never matters" — but the 8237's page register does not
increment, so a single 512-byte transfer that straddles a 64KB physical
boundary is refused with AH=09h. The undo image sits `pt_smaxp` paragraphs
above the grant base, `pt_smaxp` is an arbitrary paragraph count, and opening
any file long enough to reach the next page boundary answered "Disk error".
`dsk_xfer` now stages such a sector through `dsk_dmabuf` in `LOW_SEG`
(SPEC.md §18.1). Worth knowing because it is the caller's *address*, not its
size or its content, that decides — so it reproduces on exactly one file in
several and looks like flaky hardware.

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
owns outside its own region — a file it was writing, state another instance
can see — leaks or goes stale, and there is no defence but a liveness test.
Arena memory is the one case that is now safe *without* a callback, and only
because the kernel force-frees the grant itself (SPEC.md §2.6); the app never
learns it happened. A `KD_QUIT`-style optional entry in the package header,
called under the lock before the region is freed, would cost the kernel one
indirect call and would generalise that safety to everything else.

## Smaller gaps, in order of how much they cost here

- **No access to the kernel's font bitmaps.** `font_char` draws to the
  screen; the text tool has to write glyph pixels into its own canvas, so
  `pt_font_init` re-probes the ROM 8×8 font through int 10h AX=1130h (with the
  kernel's own F000:FA6E fallback) and keeps the pointer. Glyphs are then read
  eight bytes at a time straight out of ROM — a second copy in bss was 760
  bytes, 3.8% of everything this package is allowed to be, and buying it back
  was the single largest saving available. The eight-byte staging buffer is
  there because ES belongs to the canvas the moment `pt_rect` starts drawing,
  so the glyph has to leave ROM before that and not during. A slot returning
  the kernel's glyph table pointer would remove the probe and the fallback
  both.
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
  a different geometry at its next paint (`OSAPI_WM_GEOM`, SPEC.md §11.1).
  That works, but the size is adopted *during* the paint — after the kernel
  has already drawn the title bar — which is why the canvas dimensions are
  printed in the tool palette rather than the title, where they would always
  be one repaint stale, at the cost of a second full repaint to fix. A
  callback that ran *before* the paint, and whose refusal `ui_grow` honoured,
  would let the crop guard reject a drag outright instead of resizing back
  with `OSAPI_WM_RESIZE` and putting the notice up a paint later. The slot
  made the correction legal; it did not make it unnecessary.
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
  goes in the undo image. Both are slices of the one arena grant. Both are already invalidated by the operation, and two
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

(The package is 14,112 bytes of image + 3,582 of bss today. Since v3 the
budget is the 65,520-byte `PKG_MAX_PARA` cap on one segment, not the 19,968
of the retired kernel-segment pool, so size is no longer the binding
constraint it was when this file was written.)

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

**One place decides a size.** The grow box, the Apply button and Enter in a size
field all land in `pt_setsize`, which clamps to the screen, then to memory, then
to what the picture will stand to lose. Typing 900 into a field is therefore
indistinguishable from dragging too far, down to the toast it produces — and the
crop guard could not have been bolted onto one entry point and forgotten on the
other.

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
times slower; a 4.77MHz 8088 slower again. The AT-class 86Box targets
(`make 286`, `make 386sx`, `make 386` — SPEC.md §16) are the other end of that
range and the honest place to judge whether a full repaint is tolerable.

**One thing to know before running this on a 286 or a 386.** The tree is 8086
code and those machines execute it verbatim, with one divergence: an 8086 uses a
shift count in full, while a 286 and later mask it to five bits, so `shl reg, cl`
with CL ≥ 32 does something different. Every one of Paint's 38 variable shifts
was audited against that; the largest possible count is 24 (`pt_bmp_pal`'s
`1 << biBitCount`), so none of them can diverge. Worth re-checking if a shift
count ever becomes a computed value rather than a small constant or a validated
field.
