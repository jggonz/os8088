# Borat native face and assets

`apps/boratype/bt_face.c` reads the shared engine state and returns the action
IDs in `bt_face.h`. Painting does not advance a passage, change settings,
commit results, choose a reaction or advance the animation clock. The platform
owns windows, menus, lifecycle and the foreign CGA renderer. Cold screen
bodies and layout arrangement use the existing C overlay; typing stays in the
resident segment.

The face uses the shared Breadboard band core (`apps/breadxt/draw.inc`) and
C game kit (`apps/cc/gk.inc`), following Breadboard and Byteopoly's live
geometry and independent-cache patterns. It introduces no kernel renderer,
API or memory budget.

## Layout and input feedback

All eleven screens derive their rectangles from the current client box and
display. The minimum verified client is 480×128; 304×130 is explicitly refused.
Below 280 pixels high, compact composition uses two prompt rows, short keys
and a bounded caption line. Taller compact clients also have an accepted-input
row. Medium and primary layouts add a portrait and wrapped caption beside the
prompt. At height 400 and above, the primary header and prompt use the 24-pixel
font; prompt cells are 16×28, with sufficient glyph height and input spacing.
The primary portrait caption has at least three rows at the normal 616×410
client. Counter labels use short forms so their numeric values fit.

The shared US QWERTY table covers all 95 printable characters. It maps shifted
punctuation to its physical key and the opposite-hand Shift, including the
backquote/tilde key. Public pure helpers `bt_key_base`, `bt_key_finger` and
`bt_key_shift` let the foreign CGA scene use exactly the same map. F/J have
anchor marks; I uses right middle and J uses right index. Clicking a key
requests a hint, rather than synthesizing a typing event.

Color keys have static finger zones. A double outline marks the target or
required Shift, `!` marks a mistake, `+` marks the last accepted physical key,
and an underline shows held Shift/Caps. Mono retains these shapes. Accepted
prompt characters have a mono underline; the target uses inverse fill and a
caret. Accuracy is a dash before an attempt exists; WPM is a dash before an
active second exists.

Header, footer, body, prompt/input cells, counters, hint, portrait/caption and
physical keys have separate caches. Cached scalar state gates precede glyph
measurement and keyboard scans. Prompt changes redraw the affected cells and
byte-sharing neighbours. Medium/primary key updates use the existing native
frame, opaque `font_run` and hline primitives inside reserved flat-face areas;
they preserve bevels and glyphs and exactly match the full band's appearance.
Compact keys retain band composition.

Typing paints the prompt and key feedback before deferred work. Idle paints
do one pending job: a small input-mirror span, footer strip, hint, counter, or
portrait/caption strip. The counter cursor prevents one changing value from
starving the other counters. Portrait changes coalesce to the latest expression;
there is no frame queue. Portrait jobs paint one physical row; caption jobs
paint at most four physical rows. A full W_PAINT still composes the complete scene.
Content-edge slivers outside the byte-aligned band are filled through the
native clipped fill API.

## Visual reactions and Settings

The face reads the original `bt_caption`, `bt_expression` and
`bt_reaction_history` helpers. The portrait panel wraps the complete original
joke, and Results can display recent original reactions. Music, sound effects
and these visual messages are the audio/reaction scope; sound clips and their
status/caption controls have been removed.

Settings has nine controls in three compact columns: Music, Effects, Key ticks;
Roast, Display, Test music; Fullscreen, Save settings, Back. Display remains
mouse-accessible at 480×128, including selection of CGA color fullscreen from
native CGA. A bounded status line shows the actual master/music/effects state.
Help and labels advertise M/E/K/R/D/T and F6, and 1–4/Up/Down practice selection.
Painting never changes audio state.

## Committed artwork

The original generated master and exact prompt are retained as
`apps/boratype/art/master.png` and `PROMPT.md`. `tools/os88btart.py draft` is an
offline Pillow authoring command: it crops the four-by-two 1774×887 source
sheet with rounded cell boundaries, resizes with a fixed filter, quantizes to
the fixed palette and commits exact pixel rows. The runtime sources are
`sprites.txt` and `blink.txt`. Explicit mono variants use a fixed 2×2 ordered
threshold; CGA uses black/cyan/magenta/white.

`build` and `build-sound` use Python's standard library and the existing
`os88bbart`/`os88gkmus` source packers. No generation API, Pillow or image decoder
runs during `make` or on the guest.

Expression order is welcome, attentive, pleased, smug, disappointed, laughing,
shocked. The eighth source cell is the idle blink.

| IDs | Record |
| --- | --- |
| 0–6 | 96×120 color portraits |
| 7–13 | 40×48 compact color portraits |
| 14–20 | 40×48 explicit mono portraits |
| 21–27 | 32×40 CGA portraits |
| 28, 29, 30 | WIN16 dialog10, dialog8, System16 fonts |
| 256, 257 | Original music and effect banks |
| 258, 259, 260, 261 | Large, small, mono and CGA blink portraits |
| 262 | 24-pixel primary font, maximum advance 16 |

Art part 0 stays byte-identical at **63,677 bytes**. Part 1 is **12,823 bytes**
with music, effects, four blink records and the large font. Both use checked
unsigned word directory offsets. CGA sprites are ordinary packed 4bpp records:
EGA nibble values 0, 11, 5, 15 map to CGA palette-1 intense pixels 0, 1, 2, 3.
The three WIN16 fonts retain their existing Wine/LGPL-2.1-or-later provenance.
The large font deterministically resamples that committed font source.

The platform derives `bt_blink_frame` from OS ticks, two ticks out of every 91,
independently of scoring. Only attentive expression 1 can select a blink
record. The portrait signature includes the frame; the existing strip cache
limits changed output. Repaint never changes the clock or picks a random pose.

Reproduction:

```
python3 tools/os88btart.py check
python3 tools/os88btart.py build -o build/BTART.BIN
python3 tools/os88btart.py build-sound -o build/BTSOUND.BIN
```

`bt_face_move` updates the face and shared band holders when their claims move.
The C runtime relocates the common eager-parts carve; the platform re-reads
part segments, repairs both art-bank holders and refreshes the face's cached
art base. Code and artwork are not copied into private mechanisms.

## Host checks

The production-source host harness checks every screen/control bound and
clickable center, physical-key separation, all printable mappings, opposite
Shift, glyph extents, mono shapes, attentive-only blink selection, session
immutability and zero output for unchanged state. Its 24 color/mono cases
cover 624×128/160/240/280/300/420, 616×410, 640×480, 784×540, 1008×708,
480×128 and 480×150. It rejects 304×130. ASan/UBSan and the native 8086 compiler
frame gate passed during implementation. These host bounds and output-area
assertions establish geometry, not XT latency or inspected guest pixels.

## Target performance probe

`tests/boratype_performance.py` is the measurable XT row. It boots the actual
installed package through `os88ui`, derives field offsets from the production
headers and reassembles maps against the exact native app and kernel binaries.
It verifies that KERNEL.SYS and BORATYPE.O88 on the mounted disks match the
measured build, and records SHA-256 manifests for source, binaries, disks,
generated assembly, KZIP metadata and probe/helper code. Artifacts and source
must remain unchanged through the run. No guest state is written.

The default performance-only command is:

```
python3 tests/boratype_performance.py
```

The six isolated cases are accepted F, wrong Z/X/C (including the third error),
accepted Space and accepted J. Three continuous eight-key runs mix five
accepted characters and three mistakes. Each next physical key is queued at
the previous public callback return, without a per-key UI drain. This avoids
simultaneously overflowing the emulated controller's make/break queue while
still exercising uninterrupted input. Attempts, correct/error totals and
passage progress must agree exactly. Original music/effects and master sound
must be enabled; the removed PCM entry points must be absent.

The strict **100 ms** deadline measures enqueue through the real device to
completion of the first applicable output: the prompt's band blit for an
accepted key, or the opaque `!` badge run for a mistake. It measures framebuffer
completion; CRT scanout is not included. The report separately records public
`os88_onkey` entry-to-return time, queue wait, engine/face time and eventual UI
quiescence. IRQ1 scancode, BIOS-pending and periodic entry/return markers make
queue delays attributable. The negative arm (`--negative`) requires a mistake
badge for a correct F and must fail because that output was never emitted.

The first immutable acceptance baseline was source 743da2621:

| Artifact | SHA-256 |
| --- | --- |
| Package | `946461362cc2597703aa19904574634065fe560817a40dd2d27f128ee360c360` |
| Native BIN | `b79d0d896a978826698ffa95ae27d56b91c90129f00573c43d05e1e7c327d2ff` |
| Kernel BIN | `9a88e1725f4378664e1852d662276221bbe928280ac34863ba49765518081b1f` |

That baseline **fails the strict deadline under continuous input**. A complete
300 ms diagnostic trace preserved all 30 key outcomes and measured rapid
feedback up to **144.502 ms**, with complete callbacks up to **135.061 ms**.
The relaxed diagnostic ceiling is not an acceptance pass. Its 220 periodic
calls peaked at **72.245 ms**, with quiet calls at most **46.895 ms**. A separate
attribution trace caught a real key in the BIOS before a new periodic call;
that call spent **70.685 ms** before the OS polled the key, followed by prompt/
badge work. This evidence motivates the platform's pending-input repair.

Continuous eight-key consumption took 1.35–1.38 seconds per run; eventual UI
quiescence took 3.83–24.21 seconds and is recorded separately. Full paints and
this quiescence are not represented as sub-100 ms callbacks. The original
isolated-only probe passed, but did not prove uninterrupted-input deadlines.

The shipping UI stack's `5A57` canary remained intact over **794 samples**;
maximum observed use was **156 of 512 bytes**. Task 0 has no shipping sentinel
fill, so this is a sampled lower bound on maximum use, **not global high-water**.
The probe does not write a fill pattern or invent an estimate.

`--relocation-only` optionally reuses CHELLO and HEAPFRAG's public compaction
scenario; `--fixture-build PATH` selects those existing fixture packages.
The scenario must observe actual movement and repaired holders, preserve part
hashes and score, then draw and type after movement. Relocation is separate
from the default timing run. Parent integration owns suite registration and
final adapter/resource acceptance; this lane changes no shared kit or kernel.

### Pending-input repair acceptance

The repaired product at f931dc93e passed the strict default row in **64.792
host seconds**. The recorded source revision is 906751645, which adds test-only
changes to that product; the native map and complete source/artifact manifest
were verified against the frozen build. Artifacts:

| Artifact | SHA-256 |
| --- | --- |
| Package | `c25070dbda8eff4840b481b2e15113cb1307197ff6f4fc6c3c437c3100a154bf` |
| Native BIN | `ea90efeef9c1c50f16e461850cb71e3ba0085108aee6bacd524867b12af2753d` |
| Kernel BIN | `99151eeaf586c823f803cd9746e33ba5ffdcdae4679f8657e9c29ac6e09d69b9` |
| System disk | `2e05975e9ad6e7a34587f1546faf03dc04880fdb2fa33ef56c8c2d8d7ee822fb` |
| Software disk | `ec09f504c9d33c366252b27f13a277727014bbbd788838d5a103756ad737e28b` |
| Probe source | `d74f9502f02aedfd2ebbdd383d932049bd678ded8ad0375f07fb84f54ee7e867` |

The platform keeps scoring and music/effect service in each pulse, and skips
its redraw when a BIOS key or OS input event awaits dispatch. In the production
trace, 21 pulses with a key already pending cost **16.469–18.038 ms**, compared
with 55–72 ms before that fence.

| Physical input | First completed feedback | Complete public callback |
| --- | ---: | ---: |
| F, accepted | 70.258 ms | 104.256 ms |
| Z, wrong | 52.868 ms | 55.755 ms |
| X, wrong | 56.746 ms | 56.849 ms |
| C, third wrong | 61.391 ms | 58.630 ms |
| Space, accepted | 51.668 ms | 115.193 ms |
| J, accepted | 69.496 ms | 111.369 ms |

The 24 continuous keys ranged **44.207–97.540 ms** to first completed output;
all 30 isolated/continuous results passed the 100 ms bound. Complete callbacks
ranged **55.755–134.246 ms**. Eight-key consumption took **940.165–956.552 ms**;
eventual UI quiescence took **3.790–26.135 seconds** and remains a separate
reported limit. The 220 periodic samples peaked at **73.371 ms**; quiet work
peaked at **47.514 ms**. The master, music and effects were enabled before and
afterward, with the real sound route active; this cycle row does not claim an
auditioned recording. No PCM entry point was present.

The UI canary remained intact over **837 samples**, with the same maximum
observed **156/512 bytes**. Global stack high-water is still unmeasured. The
same-build negative arm failed after **11.936 host seconds** because accepted F
emitted no mistake badge, proving that an unrelated output cannot satisfy the
feedback observer. These are scoped target runs, not a soak or full-suite run.

### Public relocation evidence

The existing-fixture scenario passed in **31.819 host seconds** on the immutable
743 baseline. A CHELLO instance first held a ceiling region and a bottom claim.
After Boratype was initialized and paused, HEAPFRAG ran its existing suite.
Closing CHELLO opened holes on both sides; a real key in HEAPFRAG posted the
public `MEM_COMPACT` request. All four fixture region checks passed.

| Boratype role | Before segment | After segment | Actual heap claim |
| --- | --- | --- | --- |
| Resident region | `8FE0` | `9080` | 63,488 bytes |
| Art view | `798D` | `7A2D` | Common eager-parts carve |
| Sound/font view | `8919` | `89B9` | Common eager-parts carve |
| Overlay view | `8C3B` | `8CDB` | Common eager-parts carve |
| Band | `19E0` | `17E0` | 8,192 bytes |

The common carve moved `7980`→`7A20` and occupies 91,648 bytes; its three part
views are not three independent claims. Each actual claim had a nonzero
relocation procedure. Both immutable art-bank hashes and resident engine
instruction samples survived; the face, shared band and second-art holders
matched the live segments. Attempts, correct/error counts and passage position
were unchanged. Raising Boratype, opening cold Settings, returning to Pause,
resuming and typing the next correct Space succeeded after the move.

The timing row needs `marty`, `nasm`, `cc` and the built native
`boratype.bin`/`boratype.o88` plus `boratype360.img` and `os8088-360.img` with their
generated map inputs. The relocation row additionally wants the existing
`build/chello.o88` and `build/heapfrag.o88`; it constructs a private 360 KB
software disk through `os88disk.py`. Both use real UI input and read-only
inspection. Neither builds a new kernel or writes guest state. Parent integration
registers `boratypeperformance` and its relocation variant and reruns them once
against the final package after the remaining cold profile fix.

## Final integrated caption bound

The final current-Marty timing run isolated a whole-caption-line idle update
at about121ms. The face now paints at most four caption scanlines per idle
job. The host face check drains a long caption at every color/mono geometry,
bounds each job and proves session state unchanged. The strict integrated
probe then passed 30 hardware keys (97.978ms worst completed feedback) and
220 periodic calls (99.062ms worst), with both 100ms limits unchanged. See
[EVIDENCE.md](EVIDENCE.md) for artifact hashes and the full-paint limitation.
