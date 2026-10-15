# Page-ahead: a cut frame built early on the hidden page, measured before building it

**This is a measurement; §6 says what was built from it.** It was
taken on 2026-10-13 on branch `video-page`, cut from `video-palette` at
`7fa87299`, in a 4-core Xeon container with ffmpeg 6.1.1. The numbers hold
for that tree's encoder and its cost model, and nobody will update them for
later trees. Nothing here has run on a real machine: every figure comes from
`tools/os88venc.py`'s own model of the player, which is the same model that
decides what a shipped file cuts. `tools/os88vidpage.py` re-derives every row
(`table` prints this file's tables from its JSON).

## The question (the owner's)

> Use page flipping in the video player so that fewer frames are cut, on
> every mode and screen that has a second page. Do not flip every frame the
> way `--flip` does. Use a new data format to build a FUTURE frame on the
> hidden page, one that would otherwise have had a large cut, out of spare
> capacity. Then flip to it at the right time, which gives a "free" uncut
> frame. That depends on spare capacity that nothing uses today, so check
> that first. Is it viable, and how much would it gain, on the attached
> videos and on a theoretical slow video that then cuts?

## The answer

**It is viable, and it gains a lot exactly where it should: a cut that
follows a calmer stretch.**
- **On the slow video with hard cuts, every cut frame disappears on all
  three paged screens.** That covers 21, 29 and 1 cut frames, down to 0, 0
  and 0.
- **On real clips that have calm between their scene changes, 22% to 93%
  of the cut frames disappear.** Bebop drops from 330 to 257 cut frames on
  the 5150's Hercules and from 17 to 3 in Mode X on the 286. Gas Gas Gas,
  in the owner's own sixteen at 640 x 400, drops from 375 to 261. TakeOnMe
  in Mode X drops from 15 to 1, and in black and white on the 5150 from 7
  to 3.
- **On a clip that is heavy throughout, it gains next to nothing.**
  TakeOnMe in sixteen colours is cut on 1,198 of 1,212 frames either way.
  Last Exile in sixteen goes from 2,362 to 2,347, and its worst second gets
  slightly WORSE (12.85% to 13.43% error as seen). That is the one row here
  where the picture is not at least as good.

The mechanism moves DECODE forward in time. Nothing else in the player can
do that:
- The disk reserve and the XMS bank move BYTES forward.
- The CPU bucket is one second deep, but it is spent a frame at a time under
  a per-frame ceiling.

A scene change that costs more than one frame's ceiling (a `peak` cut), or
more than one super-packet's 32 KB (a `cap` cut), is cut today however idle
the seconds before it were. Those two kinds are what page-ahead removes.

**What it cannot do is sustained motion.** One hidden page holds one future
frame. Once a heavy stretch begins, no frame has anything left over to build
the next one with. The cut frames that remain are those stretches.

## 1. Which screens have a second page

| format / screen | page | pages | flip mechanism | note |
|---|---|---|---|---|
| Mode X 320 x 240 (`modex`) | 19,200 plane bytes | 3 | CRTC start (`vp_show`, exists) | a third page would let a flipped play pre-build as well |
| VGA4 on its own screen: 320 x 200, 320 x 240, 640 x 350, 640 x 400 (98.1.3.2.1) | rows x 80 | 2 | CRTC start (exists) | **the owner's 640 x 360 Gas Gas Gas fits 640 x 400** |
| VGA4 / LIN80 mono on 640 x 480 (mode 12h, screen 0) | 38,400 | **1** | - | today's default; a black-and-white file has to move to 640 x 400 at 2 colours (`--palette grey2`, one store a byte) to get a page |
| Hercules 720 x 348 (`herc*`) | 32 KB at B000 / B800 | 2 | 3B8h bit 7 with 3BFh bit 1 (§53.10 `fsx_page`) | **only on a lone Hercules**: with a colour card in the machine, B800 is the CGA's (`vid_dual_ok`). The owner's 5150 is a lone Hercules |
| TEXT 80 x 25 on CGA/EGA/VGA | 4 KB | 4-8 | CRTC start | not measured here; MDA has one page |
| CGA 640 x 200, CGA4, C160, C512 | 16,000 of 16 KB | **1** | - | the CGA has no second page in any graphics or text-hack mode |
| VGA8 (mode 13h) | 64,000 | **1** | - | |

## 2. Is there spare capacity? Yes, but not in a form the player can use today

The `CPU used / headroom` column below is the decode's cycles, and the
cycles left under each frame's own PEAK, as shares of all the frames' peaks.
The `disk overflow` column is what the disk bucket (the ring, or the ring
plus the XMS bank) could not hold, so the reader idled. Both come from the
baseline encode.

- **The CPU is idle much of the time on every clip but the swamped ones.**
  Under the per-frame peak there is 26% to 98% headroom. On the 5150's
  Hercules it is 33% to 81% on the real clips, and in Mode X on the 286 it
  is 51% to 82%.
- **The disk overflows wherever there is no XMS bank, and in Mode X even
  with one.** The 5150 and Hercules case wasted 3.2 MB on Bebop and 5.9 MB
  on Gas Gas Gas, against streams of 5.8 MB and 1.8 MB. **With a 2 MB bank,
  as the owner's GASSHOTW was made, the swamped VGA4 runs overflow 0 KB**
  (TakeOnMe, the pan). The bank already spends the disk's spare, so there
  the pre-build's bytes come out of the bank. §3 says how that is kept safe.
- **What cuts the frames is not the average.** Of the cut frames that
  page-ahead could reach, most are `peak` cuts (5150, Hercules: 96 of 96 on
  Gas Gas Gas) or `cap` cuts (286, Mode X: 15 of 15 on TakeOnMe). Both are
  one frame wanting more than one frame's share. The `disk` cuts are
  sustained stretches, which page-ahead cannot reach.

## 3. How it was measured

`os88vidpage.py` runs the shipped encoder twice under a probe.
1. **The baseline** is the encode as it is, with every frame's buckets,
   ceiling, refill and overflow logged, and the targets kept.
2. **Page-ahead** is the same encode with the same targets (checked frame by
   frame). After each frame's own record, a second encoder plays the hidden
   page and spends what is left on the next FLIP TARGET.
   - **A flip target is the first frame of each cut run of the baseline.**
     That is the frame a scene change cuts, the expensive start of a run.
   - **At the target frame**, the two pages swap if the hidden one is closer
     to the target in wrong pixels. The frame's own record is then drawn on
     the new visible page. If it is not closer, the target passes unflipped.
   - **The hidden page's CPU** is what the frame's own ceiling leaves, as the
     schedule set it, and never more than the peak. Owed time (98.2.1.1)
     belongs to the frame and not to the page. A frame that was itself cut
     leaves nothing. A first version took up to the peak in frames whose
     schedule had already gone short after an owed-time frame, and made Gas
     Gas Gas on Hercules WORSE (error 0.374% to 0.429%). That is the trap.
   - **The hidden page's bytes** are only what no later frame needs. This is
     forecast on the baseline's own records and refills, from the level this
     frame leaves, for up to 30 s. A withdrawal of X lowers every later level
     by X until an overflow absorbs it, so X may be no more than any later
     frame's slack plus the overflow before that frame. The first frame ahead
     that the baseline cut for the disk bounds it.
   - **On a disk that slows under load** (every profile here has a measured
     curve), the hidden page's cycles cost the reader its refill. That loss
     comes out of the same safe bytes. Without this, the disk-starved
     Hercules TakeOnMe came out worse. With it, that run is identical to its
     baseline.
   - **A pre-build piece is charged as a record of its own**, with its
     header and its fixed decode cost. A real format would make it a
     sub-record of the frame's packet, so this is conservative. **It has to
     fit in the frame's super-packet** beside the frame's own record.
3. **Not modelled**:
   - the flip itself, which is one CRTC pair or one 3B8h write;
   - a retrace wait at the flip (§6);
   - keyframes inside a pre-build window (§6);
   - the DAC at a scene palette (§6).

   **The unflipped frames tear exactly as an unflipped play does today**,
   because the frames between flips are drawn in place. Only the flipped
   frame is clean.

The three machines and screens:
- **`herc`**: `--preset herc-full --profile 5150-st225` (720 x 348 box, the
  owner's 5150 with its ST-225).
- **`modex`**: `--preset modex --profile 286-fast --bank auto`.
- **`vga4`**: GASSHOTW.V88's own stored options, loaded with `--from-v88`
  (`vga4-full`, `286-fast`, colour16 with a palette for each scene, a 2 MB
  bank prefilled, 309 KB of memory), but with `--screen 640x400` so that a
  second page exists.

The synthetic clips are eight stills from the four sources, 3 s each with a
hard cut between them, at 30 fps:
- `still` holds each picture with a 48-pixel box moving across it;
- `pan` gives each a slow zoom and pan, which changes every pixel every
  frame.

## 4. Results

| run | frames | cut runs | CPU used / headroom (of peak) | disk overflow | cut: base -> page | seen %: base -> page | worst s %: base -> page | broken s: base -> page | flips |
|---|---|---|---|---|---|---|---|---|---|
| herc_still | 719 | 6 | 7% / 94% | 2299 KB of 172 | 21 -> **0** | 0.256 -> **0.000** | 2.05 -> 0.00 | 0.5 -> 0.0 | 6 of 6 |
| herc_pan | 719 | 6 | 27% / 74% | 1535 KB of 889 | 23 -> **0** | 0.258 -> **0.000** | 2.17 -> 0.00 | 0.5 -> 0.0 | 6 of 6 |
| herc_TakeOnMe | 972 | 75 | 67% / 33% | 0 KB of 4042 | 887 -> **887** | 2.385 -> **2.386** | 12.36 -> 12.36 | 22.7 -> 22.7 | 0 of 74 |
| hercsm_TakeOnMe | 969 | 5 | 36% / 64% | 1900 KB of 2186 | 7 -> **3** | 0.019 -> **0.003** | 0.70 -> 0.06 | 0.2 -> 0.0 | 3 of 5 |
| herc_GasGas | 2217 | 25 | 20% / 81% | 5870 KB of 1817 | 96 -> **67** | 0.374 -> **0.226** | 7.31 -> 3.82 | 2.5 -> 2.0 | 22 of 25 |
| herc_Bebop | 2159 | 42 | 45% / 56% | 3215 KB of 5792 | 330 -> **257** | 0.471 -> **0.299** | 7.26 -> 4.94 | 8.1 -> 5.6 | 33 of 42 |
| herc_LastExile | 2489 | 81 | 61% / 41% | 1845 KB of 8123 | 1016 -> **966** | 1.663 -> **1.498** | 10.09 -> 10.09 | 25.6 -> 24.2 | 30 of 81 |
| modex_still | 600 | 2 | 2% / 98% | 10910 KB of 271 | 1 -> **0** | 0.002 -> **0.000** | 0.04 -> 0.00 | 0.0 -> 0.0 | 2 of 2 |
| modex_pan | 600 | 2 | 15% / 85% | 8469 KB of 2376 | 2 -> **0** | 0.002 -> **0.000** | 0.04 -> 0.00 | 0.0 -> 0.0 | 2 of 2 |
| modex_TakeOnMe | 969 | 14 | 37% / 63% | 5779 KB of 9764 | 15 -> **1** | 0.046 -> **0.013** | 0.55 -> 0.51 | 0.3 -> 0.0 | 13 of 13 |
| modex_GasGas | 1850 | 2 | 18% / 82% | 24212 KB of 8179 | 1 -> **0** | 0.000 -> **0.000** | 0.03 -> 0.00 | 0.0 -> 0.0 | 2 of 2 |
| modex_Bebop | 2159 | 15 | 24% / 76% | 25074 KB of 13393 | 17 -> **3** | 0.038 -> **0.007** | 1.64 -> 0.49 | 0.6 -> 0.1 | 14 of 15 |
| modex_LastExile | 2486 | 48 | 49% / 51% | 4673 KB of 31825 | 136 -> **129** | 0.102 -> **0.098** | 2.62 -> 2.19 | 1.5 -> 1.3 | 15 of 48 |
| vga4_still | 719 | 7 | 6% / 95% | 10553 KB of 682 | 29 -> **0** | 0.199 -> **0.000** | 1.28 -> 0.00 | 0.6 -> 0.0 | 7 of 7 |
| vga4_pan | 720 | 20 | 75% / 26% | 0 KB of 9103 | 278 -> **278** | 0.314 -> **0.314** | 1.69 -> 1.69 | 0.5 -> 0.5 | 0 of 19 |
| vga4_TakeOnMe | 1212 | 11 | 73% / 28% | 0 KB of 15782 | 1198 -> **1198** | 1.894 -> **1.894** | 8.55 -> 8.55 | 14.5 -> 14.5 | 0 of 10 |
| bw_TakeOnMe | 968 | 1 | 18% / 82% | 13392 KB of 4517 | 0 -> **0** | 0.000 -> **0.000** | 0.00 -> 0.00 | 0.0 -> 0.0 | 0 of 0 |
| vga4_GasGas | 2217 | 86 | 44% / 59% | 9516 KB of 17543 | 375 -> **261** | 0.464 -> **0.387** | 6.35 -> 5.71 | 3.7 -> 2.8 | 56 of 86 |
| vga4_Bebop | 2699 | 118 | 67% / 36% | 1465 KB of 26455 | 1410 -> **1408** | 1.226 -> **1.130** | 12.14 -> 6.93 | 20.6 -> 20.7 | 10 of 118 |
| vga4_LastExile | 3105 | 74 | 66% / 37% | 2898 KB of 31353 | 2362 -> **2347** | 3.297 -> **3.256** | 12.85 -> 13.43 | 38.3 -> 38.2 | 10 of 74 |

Two runs are not §3's machines but the owner's usual way of making
TakeOnMe, in black and white: `bw_TakeOnMe` is two colours (`--palette
grey2`) on the 286's 640 x 400, and `hercsm_TakeOnMe` is the `herc` preset
(400 x 200) on the 5150. The sixteen-colour `vga4_TakeOnMe` stays in as
the swamped case.

**Per cut run.** A run is "gone" when no frame of it is cut any more,
"shorter" when fewer are, and "first frame whole" counts runs whose scene
change is now drawn whole. "New cut frames" are frames the baseline did not
cut. Each is right after a flip, where the next frame no longer gets the
owed time it borrowed in the baseline.

| run | cut runs | gone | shorter | same | longer | first frame whole | new cut frames |
|---|---|---|---|---|---|---|---|
| herc_still | 6 | 6 | 0 | 0 | 0 | 6 | 0 |
| herc_pan | 6 | 6 | 0 | 0 | 0 | 6 | 0 |
| herc_TakeOnMe | 75 | 0 | 0 | 75 | 0 | 0 | 0 |
| hercsm_TakeOnMe | 5 | 3 | 0 | 2 | 0 | 3 | 0 |
| herc_GasGas | 25 | 10 | 13 | 2 | 0 | 21 | 6 |
| herc_Bebop | 42 | 23 | 11 | 8 | 0 | 33 | 13 |
| herc_LastExile | 81 | 8 | 19 | 54 | 0 | 23 | 4 |
| modex_still | 2 | 2 | 0 | 0 | 0 | 2 | 0 |
| modex_pan | 2 | 2 | 0 | 0 | 0 | 2 | 0 |
| modex_TakeOnMe | 14 | 13 | 0 | 1 | 0 | 13 | 0 |
| modex_GasGas | 2 | 1 | 1 | 0 | 0 | 2 | 0 |
| modex_Bebop | 15 | 13 | 1 | 1 | 0 | 14 | 0 |
| modex_LastExile | 48 | 8 | 3 | 37 | 0 | 11 | 1 |
| vga4_still | 7 | 7 | 0 | 0 | 0 | 7 | 0 |
| vga4_pan | 20 | 0 | 0 | 20 | 0 | 0 | 0 |
| vga4_TakeOnMe | 11 | 0 | 0 | 11 | 0 | 0 | 0 |
| bw_TakeOnMe | 1 | 0 | 0 | 1 | 0 | 0 | 0 |
| vga4_GasGas | 86 | 33 | 21 | 32 | 0 | 53 | 12 |
| vga4_Bebop | 118 | 0 | 11 | 107 | 0 | 5 | 14 |
| vga4_LastExile | 74 | 1 | 13 | 60 | 0 | 7 | 6 |

## 5. What it gains, and why it stops where it does

- **A slow video that then cuts is the case the idea is for, and it is won
  outright.** Every scene change is pre-built over the 3 s of stills before
  it, and every cut frame disappears on every screen: Hercules 21 to 0, VGA4
  29 to 0, Mode X 1 to 0. It costs 3% to 5% of the peak's cycles. In the
  panning version the same is true on Hercules and Mode X. On VGA4 at 640 x
  400 the zoom alone uses 75% of the peak and 26% headroom remains, and no
  pre-build finishes. That is the first limit: there has to be SPARE, not
  just a calm-looking picture.
- **On real clips, the gain is the scene changes that follow calm.** On Gas
  Gas Gas in the owner's own sixteen colours, 53 of its 86 cut runs now
  start with the frame whole. That is the defect docs/reports/VIDEO-SCENES-2026-10-13.md
  measured as "0 of 21 drawn whole on any arm, and on lossless as well". It
  says a whole 640 x 360 four-plane picture is several 32 KB records, so a
  change is drawn across three or more frames in the NEW scene's colours.
  Page-ahead is the first mechanism that draws such a change as one frame:
  it is built across as many records as it needs, where nobody sees it.
- **What is left is sustained motion.** TakeOnMe in sixteen colours, the VGA4
  pan, and most of Last Exile on Hercules are cut frame after frame. In those
  stretches no frame leaves anything over, so the hidden page is never
  built. That is the second limit, and it is structural: one hidden page
  holds one frame. A third page (Mode X) would let two be built, but there
  is still no spare to build them from.
- **It never made a run longer.** A disk-starved or CPU-swamped clip comes
  out identical to its baseline or close to it, because the forecast keeps
  the pre-build out of any byte or cycle a later frame needs. The exception
  is Last Exile in sixteen colours, whose worst second is 0.6 points worse:
  a flip there leaves the frames after it less owed time than the baseline
  borrowed. The few new cut frames are each in the frame right after a
  flip.

## 6. What was built from it (the same day)

The owner asked for it to be built once the first rows were in. It is
built for the two screens whose player already flips, and SPEC.md is the
contract:
- **The format**, SPEC.md 98.1.3.7, uses flag 4096 PAGE and rendition byte
  37 = 3. In a record's row words, y0 bit 15 is FLIP and y1 bit 15 means
  "a hidden record follows the lists". The hidden record sits **inside the
  frame record's `len`, before its audio**, so every super-packet walk,
  `len` hop and audio read is unchanged. A key carries the hidden page's
  difference from it in the same way, which answers the one real
  complication: a seek into a pre-build window.
- **The encoder**, SPEC.md 98.2.1.8, is `os88venc --page-ahead`. It runs a
  plan pass and then the real pass, with exactly §3's policy, as the
  `PageAhead` class. On Gas Gas Gas 40-60 s, in the owner's own sixteen at
  640 x 400 on `286-fast`, 28 of 52 cut scene changes are built ahead, and
  8 of the 13 palette scene changes are drawn whole.
- **The player**, SPEC.md 98.3.8.3, is `[vp_flip]` = 2 with `vp_pgdec`,
  `vp_pghid`, `vp_pgoff`/`vp_pgon` and `vp_vwait`, plus `vp_decboth`'s
  hidden key. It costs **+343 bytes of `video.o88`** (49,028 to 49,371)
  and no kernel byte. Its retrace wait is the one `--flip` could not
  afford on every frame; here it is paid once a scene change, on the frame
  that was built ahead.
- **The gate** is `tests/vidpage.py` (rows `vidpage` and `vidpagemx`). It
  writes a clip whose every flip and hidden record is placed by hand, and
  checks the glass at every hold against that canvas on MartyPC's VGA XT,
  from the start and from a key inside a window: exact on both screens.
  Three deliberate breaks each turn it red: the flip's `vp_show` skipped,
  `vp_pghid` a bare `ret`, and a key's hidden record left out.
- **Then Hercules page 2, the preload and auto** (the owner's second
  round, SPEC.md 98.1.3.7, 98.2.1.8.1 and 98.3.8.3). The player decodes the
  hidden page at B800 and shows it through 3B8h bit 7, and refuses in its
  own words when a colour card sits beside the Hercules (the second page
  shares the colour card's addresses, so `fsx_page` will not hand it out).
  Key 0 carries the first scene change whole, so a play from the start
  begins with a page already built. The player is now **+533 bytes**
  (49,028 to 49,561), still no kernel byte. `--page-ahead` defaults to
  `auto`: on Mode X and the 16-colour screens of their own the encoder
  makes both files and keeps the page-ahead one only when it cuts no more
  frames and shows no second worse; a Hercules stays plain unless asked,
  because it plays only where no colour card is fitted. `tests/vidpage.py`
  gained `vidpageherc` and `vidpagehercno`, and MartyPC needed patch 12
  to show a lone Hercules card's page 1 at all.
- **Then the prune, build by build** (SPEC.md 98.2.1.8.2, `--page-prune`).
  A build whose own stretch comes out worse than the plain encode is left
  out in a third pass. Run on the shipped encoder (not the simulator) for
  the two 16-colour clips, with `always`:

  | clip | plain | page-ahead | pruned | shipped |
  |---|---|---|---|---|
  | Gas Gas Gas: seen / worst s / broken s | 0.349% / 4.41% / 3.17 | 0.281% / 3.76% / 2.50 | 0.267% / 3.76% / 2.47 (8 of 83 left out) | pruned |
  | Last Exile 0-103.7 s: seen / worst s / broken s | 2.94% / 12.4% / 38.3 | 2.92% / 12.0% / 37.7 | 2.94% / 12.4% / - (3 of 8 left out) | page-ahead |

  On the shipped encoder neither clip is worse with page-ahead than plain,
  so the default `rescue` runs no third pass on either. The simulator had
  Last Exile's worst second worse, and the real encoder does not. Last
  Exile's prune also shows the stretch test misjudging: the three builds it
  named were the ones that bettered the worst second. The comparison after
  the third pass refused it. Gas Gas Gas took 951 s in two passes and 1351
  s in three, on a box running two encodes at once.

## 7. What it means

It pays on three screens. Two of them are built (§6), and the third is
the next step:
- **VGA4 on 640 x 350 or 640 x 400**: whole scene changes in the clip's own
  colours.
- **Mode X**: its `cap` cuts disappear.
- **Hercules on a lone-Hercules 5150**: Bebop's broken seconds fall from 8.1
  to 5.6, and Gas Gas Gas's worst second halves.

**Expect nothing from it on the CGA, which has no second page**, nor on a
clip that is heavy all the way through.
