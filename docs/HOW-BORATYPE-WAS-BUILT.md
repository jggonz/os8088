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

The engine lane runs the production C under target-width host types: all
65,536 tick conversions, 200,000 multiply/divide cases, all24 complete
lessons and four intentionally broken production variants. Its standalone
native fixture passed the compiler/frame gate. These are host/compile
observations, not a guest acceptance pass.

The face lane checked24 color/mono geometries, with minimum480x128 content,
normal616x410 and640x480, EGA-height and VESA-sized layouts. It checks all95
printable characters, shifted physical keys, opposite-hand Shift, I versus J,
and zero output for unchanged state. A first VGA character emitted9 opaque
bands covering17,808 pixels; that is a drawing bound, not measured XT latency.

The newly generated portrait master contains eight coherent expressions.
Art0 is63,677 bytes; the blink records share a9,542-byte second part with
music/effects. Each converter reads committed exact-pixel source, and no
image service or PNG decoder is in make. The three original loops last35.2,
49.2 and31.6 seconds, with three stingers and eleven effects.

The file transport's35 fault checks passed under sanitizers, including
truncation, corruption, stale valid read-back, missing/read-only storage and
generation wrap. The PCM bridge's actual8086 mock tests reject19 malformed
packs and verify byte-exact ring refill after relocating its source. An
actual sound card, audible result and subjective listening remain guest QA.

The first fully integrated package hit the expected segment fence:54,142
resident bytes plus12,031 BSS exceeded0xFE00 by1,149. Startup and native menu/
About bodies moved to the existing embedded C overlay; the typing and CGA
foreign-mode paths stayed resident. No OS budget was raised. The exact-size2,136-byte file buffer replaced a4KB buffer and still holds
CGA's2,000-byte cell workspace. The resulting package is53,908 resident +
10,071 BSS =63,979 bytes,1,045 below0xFE00; overlay8,313, total compressed
package80,925. All four software disks and an85KB WPK build and validate.
The disks explicitly pre-create SYSTEM/APPDATA so a direct B: launch can save.

Downloaded character clips and local test packs live outside the repository.
No redistribution permission is established, so the base package includes
original captions/music/effects and a real optional local PCM import path.
The rights/candidate log is [audio sources](boratype/audio-sources.md).
This is a specific unfinished requirement, not a claim that subtitles pass
as voice playback. No external publishing has occurred.
