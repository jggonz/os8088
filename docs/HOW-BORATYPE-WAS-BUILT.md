# How Borat Teaches Typing was built

The current delivered scope is original music/effects and visual jokes, with
recorded clips removed by the owner. The chronological notes below retain
early experiments; [final acceptance](boratype/EVIDENCE.md) is the current
behavior, evidence and limitation record.

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

The first playable package and its test artifacts were built locally. After
trying it in 86Box, the owner requested the VGA redesign and a pull request.
The shipping audio consists of original music and sound effects. Borat's
reactions appear as text and animated poses.

## Measurements and observations

The engine lane runs the production C under target-width host types: all
65,536 tick conversions, 200,000 multiply/divide cases, all24 complete
lessons and four intentionally broken production variants. Its standalone
native fixture passed the compiler/frame gate. These are host/compile
observations, not a guest acceptance pass.

The face lane checked24 color/mono geometries, with minimum 480x128 content,
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
10,071 BSS =63,979 bytes,1,045 below 0xFE00; overlay8,313, total compressed
package80,925. All four software disks and an85KB WPK build and validate.
The disks explicitly pre-create SYSTEM/APPDATA so a direct B: launch can save.

Downloaded character clips and local test packs live outside the repository.
No redistribution permission is established, so the base package includes
original captions/music/effects and a real optional local PCM import path.
The source/candidate log remains in the session evidence outside the repository.
This was an unfinished requirement at that point, not a claim that subtitles
passed as voice playback. No external publishing occurred.


## The first real lesson exposed the cost the host could not see

The installed 360 KB software disk launched successfully on MartyPC's VGA XT.
The first Enter then failed the native UI-idle check after270 guest seconds.
This was a real failure, not a reason to lengthen a wait. Paused CPU samples
and captured strips showed the full lesson paint making progress and returning;
subsequent timer callbacks kept executing. The face was rescanning84 prompt
cells and60 keys every tick even when the host harness reported zero bands.
Quadratic prefix-width caption wrapping also ran for strips outside its area,
and portrait changes monopolized large bands. The repair targets that measured
work, with actual key-to-feedback cycle measurements still required.

The initial guest pictures also expose typography smaller than the supplied
concept and a duplicated saved-status line. These are observed mismatches;
merely generating a screenshot does not pass the visual comparison.


## The owner chose effects and visual messages

During the repair round the owner explicitly removed recorded clips from
scope: “Just use sound effects and visual messages, no sound clips.” The
product therefore keeps the original composed music, short speaker/FM effects
and original visual jokes, and removes the PCM transport/importer, voice
controls, local-pack loading and audio staging claims. Earlier PCM experiments
are historical diagnostics, not a dependency or an unfinished shipping story.
The BTT v1 legacy voice byte stays compatible and defaults off.

Measured native feedback after reusing clipped OS frame/hline/font_run calls
was51–79ms for six queued hardware-key cases on the VGA XT, including a third
error reaction. Complete callbacks97–147ms and first visible feedback are
different measurements; secondary mirror/hint/counter work is coalesced.
The final acceptance run must repeat the key measurements on the integrated
no-clip head and inspect every adapter.

## The final guest checks changed the implementation

The combined native tour completed beginner and shifted mixed lessons,
compared exact counters, named all four profiles and exported the actual
guest-written floppy before cold reboot. It covered the window, shrank it
to480x128 content, resumed deliberately and restored geometry. It found an
OWNBG shrink does not necessarily owe a resize callback: service-boundary
geometry comparisons now pause at the last observed active tick. Focus loss
uses that same conservative boundary, so delayed modal work cannot become a
ranked challenge expiry. Help/Settings/Records return to Pause.

Continuous hardware input exposed124–145ms feedback despite isolated keys
passing. Read-only IRQ/BIOS/pulse attribution caught a timer draw running
before a key already waiting in the BIOS. A non-consuming peek now defers
only idle drawing; the normal OS event remains the only scoring input.
Thirty keys then completed feedback within98ms, including uninterrupted
eight-key runs. Complete callbacks and CRT scanout remain separate costs.

Real empty challenges reached1093/3278/5462ticks but initially praised
no typing. They now show accuracy as a dash and neutral guidance. Five
neutral captions guarantee a choice outside the recent-four history.
A four-profile mouse tour caught stale name-edit state, and a real weak-key
practice tour caught the mislabeled digits-only group. Both were repaired.
Nineteen actual errors produced error/low/poor roasts, a ten-key comeback,
a sampled F recommendation and a working focused drill.

All18 initial final rows passed. Added fullscreen-playing captures verified
the expanded EGA/VESA/Mega keyboard, with two early emulator hangs passing
on isolated retry. The full repository gate caught missing HIGH/HIGHMEM
declarations. The audited one-byte class change now permits high code and
band; a full actual 286 lesson/profile/fullscreen/cold-reboot tour passed
with both above 1 MB. The test helper also learned to check KIND_FILES before
interpreting I_SPTR, rather than mistaking a high selector for file state.
The copied emulator runtime was rebuilt from the current pinned sources.

The source-derived app-owned peak is163.5 KiB on the observed XT volume,
164 KiB with the Mega high-region rounding. Region, parts and band all moved
through the public compaction fixture in real mode. Sound-route captures
proved transport and cleanup; the tool could not listen, so audition remains
unverified. Full PLAY/Resume paints can interrupt speaker sequencing for
about7.3seconds. The committed gallery and comparison notes show the native
result and its differences from the supplied visual target.

The rebuilt canonical MartyPC exposed a final timing defect: one complete
caption line cost about121ms while every measured key remained under100ms.
Read-only pending-state attribution isolated it from counters, mirror and
portrait work. Four physical scanlines per idle pass reduced the final
220-call maximum to99.062ms. Thirty hardware-enqueued keys peaked at97.978ms;
1,030 sampled UI-stack stops used at most 156 of 512 bytes with the guard intact.
The first repaired run passed those assertions but raced a leftover probe
breakpoint before capture. The test now leaves its last callback stopped,
clears the probe and confirms resume. Its repeat passed end to end.

## A second pass with the player's design sheets

The first playable build made the slowdown easy to reproduce. On the
4.77 MHz VGA XT, three wrong keys triggered a new expression. Read-only
samples taken every twelfth of a guest second showed that only 50 of 120
portrait rows had reached the new pose before the eight-second reaction
expired. The timing gate had bounded the cost of an individual update,
but had not measured how long a player waited for a complete expression.

The supplied sheets became package assets. The title is the original
432 × 24 gold silhouette. The keyboard supplies bitmap capitals and digits,
and the records page supplies the earned achievement icons. Lessons,
records, results and sound settings each have their own scenic illustration.
A normal build packs committed native-size masks without opening any PNG.
Scoring, selections, captions and hit rectangles are still live UI.

The animation banks contain preconverted VGA and monochrome strips. Matching
strip signatures let static hair, shoulders and scenery stay on screen while
the eyes or mouth move. The revisions to the timing probe examine signatures
only after a completed display write. A guest can now fail the animation
check even when its logical frame counter continues to advance.

A 25 MHz 386 running 86Box exposed a fault that the host geometry checks
couldn't see. The first 208-pixel lesson illustration overran a packed
sprite row intended for 120 pixels. The compositor subsequently read the
overwritten bytes as its plane count. The finished scene format skips that
scratch row entirely. The packer emits four bitplanes, and the native
renderer copies just the intersection with the active window band.
On the same emulated 386, opening Lessons from Home took 439 ms to show
the complete selector body, down from 639 ms with the enlarged nibble
buffer. The comparison sampled the guest display every 25 ms.

Guest captures also informed the composition. A center crop lost the
Settings tutor's mouth. The final card keeps the complete drawing and
speech balloon. The small Records plaques now have separate 16-pixel
locked and earned badges, and the white spacebar spans most of the home
keyboard. The full QWERTY symbol set and all seven achievement states
remain selectable or visible on the appropriate screens.

In the final build, the first complete pointing pose appeared 301 ms after
the third mistake. All six appeared within 1,573 ms. The motion probe
compared each 96 × 120 display capture with its source pose. Thirty hardware
keystrokes took at most 59.7 ms to reach the first completed prompt update.
The longest of 220 periodic callbacks took 71.2 ms, and stack sampling
reached 170 of 512 bytes with the guard intact. These are emulated XT
measurements.

Keeping every pose resident at Home prevented a second instance from opening
on a 640 KB machine. The menu now retains the small directory and menu art.
Start fetches the three large pose banks before starting the scored clock.
Returning to the menu frees 173,568 bytes. A second instance opened read-only
beside the first, and an explicit resume after visiting Help restored the
large frames. The original outlined speech balloon is drawn with the scene.
While typing, four-row updates write only its white text interior.

The [guest gallery](boratype/EVIDENCE.md#vga-redesign) shows the selector,
typing feedback, a running challenge, results, achievements and settings.
