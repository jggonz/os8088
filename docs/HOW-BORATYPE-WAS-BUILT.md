# How Borat Teaches Typing was built

## Starting point

The owner supplied a complete product brief and nine VGA concepts, including
portrait and UI animation grids. Work began from main 528648017 on a separate
feature worktree. The main checkout and existing VMs were left alone.

## Reference map

| Real reference | Carried forward |
|---|---|
| apps/breadxt/face.c, bu_layout, bu_rect, bu_paint | content-based layouts, independently invalidated sections, drawing outside computation |
| apps/breadxt/draw.inc, bx_setup/bx_rows/bx_new/bx_out | planar opaque bands and clipped commits, fonts/sprites in package parts |
| apps/byteopoly/byteoply.asm and bp_app.c | C runtime callbacks, menus/About, movable drawing claim, timer-driven audio and cleanup |
| apps/byteopoly/bp_face.c and art/ | palette typography, beveled controls, compact composition and source art pipeline |
| apps/cc/gk.h, gk.inc, gkmus.inc | shared widgets and routed speaker/FM soundtrack |
| tests/gkdemo/mkart.py, tools/os88bbart.py | dense art ID table, shared WIN16 font packers, deterministic sprites |
| apps/cc/os88.h, tests/chello/chello.c | C ABI, static out-parameters, near-model constraints |

The mockup's sentence is a mixed-letter lesson. Beginner home-row lessons
must use declared home-row keys; the drawing never supplies curriculum data.
The existing native fullscreen latch preserves ordinary events and clipping.
CGA four-color needs a separate FSX renderer because desktop drawing is
illegal after switching into a foreign mode.

## Source and authorization

The task requests local implementation and reviewable artifacts. No external
posting, PR, push, release or website publication is performed. Redistribution
rights for existing movie/character sound clips must be recorded separately
from technical playback. Optional local imports cannot be described as shipped
licensed audio.

## Measurements and observations

Pending. The delivery plan records actual evidence as it is collected.
