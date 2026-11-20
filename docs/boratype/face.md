# Borat native face and assets

The production face is `apps/boratype/bt_face.c`, included in the package's
single translation unit. It reads the shared state and returns action IDs;
painting never advances a passage, changes settings, commits results or
chooses reactions. `ovl_tf_drawbody` is the cold body for all non-play screens.

The drawing references inspected were `apps/breadxt/face.c` (`bu_layout`,
section rectangles, strip composition), `apps/breadxt/draw.inc` (opaque
planar/mono bands and packed sprite/font records), and
`apps/byteopoly/bp_face.c` (live geometry, independent signatures, shared game
kit). The face uses those actual shared sources, plus `apps/cc/gk.h` and
`gk.inc`; it does not copy their renderer. Native window chrome and menus
belong to the platform. All rectangles derive from the current window content
and display. The window should request content alignment as those apps do.

## Layout and drawing

Header/navigation, footer, non-play body, portrait, caption, target hint,
individual prompt/input cells, five counters and sixty physical keyboard
entries have separate signatures/caches. Two zero-width keyboard entries are
reserved; 58 visible physical keys include the complete printable US QWERTY
set plus Backspace, Tab, Caps, Enter, opposite-hand Shift, Space, Ctrl and Alt.
The printable map is 47 ordinary keys plus Space. The I key uses right middle,
J right index, and F/J have their tactile anchor marks. Shifted punctuation
identifies its physical base key and opposite-hand Shift. Clicking any key
returns a hint action, never a printable input event.

Color keys use finger zones, a double outline for target/required Shift,
a `!` for mistakes, `+` for the previous accepted physical key and an
underline for held Shift/Caps. Monochrome preserves these shapes explicitly.
Accepted prompt text is underlined on mono; the target has inverse fill and a
caret. Numbers show a dash before an attempt/active second exists. The engine
owns all numbers; rendering does no statistics or random selection.

The minimum verified client is 480x128. A 624x128 client has a special
short composition with two prompt rows and one-line caption; accepted input
is represented in the prompt itself there. Taller compact clients add a
separate bounded accepted-input row. Compact target/finger instructions live
in the title line. Short lesson grids use four columns, taller compact grids
three, normal grids two. EGA/tall/VESA use the extra height for larger prompt
cells and taller keys; the keyboard spans the live available width.
The foreign CGA 320x200 renderer belongs to the platform, sharing the engine
and the CGA expression records below.

Incremental output rounds only the changed cell's rect to byte boundaries
and recomposes neighbours sharing those bytes. It does not call the full
body painter for a typing update. The physical target/shift/wrong/correct IDs
are resolved once per paint, rather than scanning QWERTY for every key.

## Art part 0

`tools/os88btart.py build -o PATH` reads only committed `sprites.txt` and
WIN16's committed `dlg10`, `dlg08` and `sys16` fonts. It needs Python's standard
library and imports the existing `os88bbart` font/sprite packers as source.
No image service, PNG decoder or Pillow is needed by `build`.

The original master and exact image generation prompt are retained in
`apps/boratype/art/master.png` and `PROMPT.md`. `draft` is a development-only
Pillow command; it derives the four-by-two source cells using rounded edges
(the source is 1774x887), resizes with a fixed filter, quantizes to EGA, and
writes exact pixel rows as the authoritative runtime source. Dark navy source
background is held at palette index1. Other color pixels use fixed-palette
Floyd-Steinberg quantization. The explicit mono uses a fixed 2x2 ordered
threshold; CGA uses black/cyan/magenta/white. No fetched reference image is
redistributed. The generated art is original source; no movie frame or audio
clip is included by this lane.

Expression order: welcome, attentive, pleased, smug, disappointed, laughing,
shocked. The eighth blink cell is retained only in the source master.

| IDs | Records |
| --- | --- |
| 0–6 | 96x120 color portrait |
| 7–13 | 40x48 compact color portrait |
| 14–20 | 40x48 explicit black/white portrait |
| 21–27 | 32x40 CGA portrait |
| 28 | WIN16 dialog10 font |
| 29 | WIN16 dialog8 font |
| 30 | WIN16 System16 font |

Each sprite is an ordinary packed4bpp record. CGA nibbles are EGA0,11,5,15,
corresponding to CGA palette1 intense pixel values0,1,2,3. A BA directory uses
unsigned word offsets. Every dimension, character, palette and directory
record is validated. The complete part is **63,677 bytes**, below 65,535.

Reproduction commands:

```
python3 tools/os88btart.py draft apps/boratype/art/master.png
python3 tools/os88btart.py check
python3 tools/os88btart.py build -o build/BTART.BIN
python3 tools/os88btart.py preview -o build/boratype-art-preview.png
```

Build prerequisites: `tools/os88btart.py`, `tools/os88bbart.py`, `art.h`,
`art/sprites.txt`, `apps/win16/fonts/dlg10.txt`, `dlg08.txt`, `sys16.txt`.
The package depends on `bt_face.c`, `bt_face.h`, and `apps/cc/gk.h`, with the
existing shared band core and game kit included from the package shim.
`bt_face_move` updates its own art and band holders, including `bx_bseg`;
the next paint refreshes the shared art base. The platform updates art part1
through `bx_setart2` when that separate claim moves.

The three WIN16 font strikes are Wine font art, LGPL-2.1-or-later, with author,
source revision and license provenance in their existing source headers.
They are reused as source. The art converter preserves their records.

## Actual host checks and cost

`cc -std=c99 -Wall -Wextra -Iapps/cc apps/boratype/hosttest/face_test.c -o
build/btfacehost && build/btfacehost` runs against the production face and
asserts all screen/control bounds, actionable centers, keyboard separation,
all95 printable characters, opposite Shift, I/J finger assignment and no
unchanged-state band output. Cases624x128,624x160,624x240,624x280,624x300,624x420,616x410,
640x480,784x540,1008x708,480x128 and480x150 run on both color and mono.
A304x130 client is explicitly rejected. The production window negotiator must
enforce the minimum. ASan/UBSan pass; a deliberate opposite-Shift reversal
is rejected by the production-map assertions. After the first printable
character, 624x420 produces9 bands covering17,808 pixels; unchanged state
produces0 bands. These are bounded drawing measurements, **not XT latency**.
The host harness models the band's contract and glyph extents; it does not
substitute for inspected guest pixels.

SmallerC plus `cc8086.py --max-frame96` passes with a26-byte maximum frame. Full paints fill any sub-byte content-edge slivers
with the native clipped fill service, so OWNBG windows never retain stale
pixels there; typing updates continue through the band core.
A face-only assembled size probe with external references resolved to zero
measured `.text`9266, `.data`450, `.rodata`2913, `.modc`4913 and `.bss`975
bytes. This excludes the shared drawing core/game kit, runtime and integration
thunks; the final package build is the authority for combined cost.

The new host harness should be registered as `boratypeface` with its measured
runtime. Parent integration owns that registration, installed guest rows and
the final deduplicated union. Appropriate existing mechanism rows are
`breadxtuihost` and `gkdemo` only if the shared core/kit itself is changed;
this lane changes neither. Actual guest screenshots, aspect inspection,
clip/uncover/focus/fullscreen behavior and XT timing remain integration work.
