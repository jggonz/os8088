# Page-ahead on every page a screen has: measured before building it

**This is a measurement. docs/plans/VIDEO-PAGES-PLAN.md is what it argues
for.** It was taken on 2026-10-15 on branch `video-pages-2`, cut from
`elendilon` at `47578d5`, in a 4-core Xeon container with ffmpeg 6.1.1. The
numbers hold for that tree's encoder and its cost model, and nobody will
update them for later trees. **Nothing here has run on a real machine, and no
real clip is in it**: every figure comes from `tools/os88venc.py`'s own model
of the player - the model that decides what a shipped file cuts - over
synthetic clips made from the photographs PiXEL ships. `tools/os88vidpages.py`
re-derives every row, and `os88vidpages.py clip NAME` re-makes every clip.

## The question (the owner's)

> Mode X can fit three pages, and 320 x 200 Mode X four. Page-ahead
> (SPEC.md 98.1.3.7) uses two. Would the rest gain anything? The cases:
> page-ahead and double buffering at once; more than one expensive frame
> preloaded at the start; two transitions back to back after a stretch
> with room to build both; and in 320 x 200 sixteen colours, seven pages
> ahead - enough for a short burst of action. Either the video has room
> to fill the other pages or it does not use them. Do not judge it by the
> videos made so far: they were made to what works, and a calm video with
> several sudden transitions does not work today.

## The answer

**Yes, and on exactly the videos the owner described.** On one hidden page,
a second scene change that lands before the first one's page can be rebuilt
is cut. With more pages it is built in the calm before both.

| case | screen, machine | cut frames, one page -> more | broken s |
|---|---|---|---|
| two cuts 2 frames apart | Mode X, 5150 | 39 -> **6** (3 pages ahead) | 1.2 -> 0.2 |
| three cuts 5 frames apart | Mode X, 5150 | 50 -> **6** (3) | 1.5 -> 0.2 |
| three cuts 5 frames apart | 16 colours 320 x 200, 5150 | 78 -> **11** (5) | 2.4 -> 0.4 |
| the opening (a cut every 4 frames) | Mode X, slow 286 | 23 -> **9** (3) | 0.8 -> 0.2 |
| the opening | 16 colours 320 x 200, 5150 | 47 -> **11** (7) | 1.5 -> 0.3 |
| 10 frames of action after a calm | 16 colours 320 x 200, slow 286 | 53 -> **27** (7) | 1.6 -> 0.8 |
| tear-free AND built ahead | Mode X, 486, 3 pages | 15 -> **2** (`still`) | 0.3 -> 0.0 |
| tear-free AND built ahead | Mode X, fast 286, **4 pages** | 16 -> **2** (`still`) | 0.6 -> 0.1 |

- **No run cut more frames with more pages.** Nothing is built on page k
  that page k-1 needed, because pages are filled nearest target first out
  of the same safe bytes and cycles as today's one. Two of twenty rows have
  a worst second up to 0.4 points worse than with fewer pages, and fewer
  frames cut.
- **A burst gets one whole frame per page**, built in the calm before it.
  On seven pages that is 28 flips over four bursts, 7 a burst. Nothing
  rebuilds a page during the burst, because no frame of it leaves
  anything.
- **The cost is not in the stream. It is in the KEYS.** A key carries every
  page that is mid-build, and each one is up to a picture's worth: 40 KB on
  Mode X and ~27 KB on the 320 x 200 sixteen. A key on seven pages carries
  ~190 KB beside its own ~52 KB, which is the seek's read. VIDEO-PAGES-2026-10-15 §6.
- **Flip and build ahead together** needs a whole-canvas latch copy after
  each built flip, and that copy is the entire question. It is 102% of a
  25 fps period on the fast 286, 66% on the 486 and 302% on the 5150 (VIDEO-PAGES-2026-10-15 §5).
  On three pages it fits only a 486. **On four pages (Mode X 320 x 200) the
  copy is done in the calm, and the fast 286 does it** - which is the
  strongest single argument for building that screen.

## 1. What each screen has

A VGA has 64 KB a plane. A page is a screen's bytes in one plane.

| screen | page, a plane | pages | ahead | note |
|---|---|---|---|---|
| Mode X 320 x 240 | 19,200 | 3 | 2 | today's Mode X |
| **Mode X 320 x 200** | 16,000 | **4** | 3 | not built (the plan's wave 3) |
| 16 colours 320 x 200 **as stored today** | 16,000 | 4 | 3 | rows are 80 bytes apart on every screen (`lin80`, 98.1.3.2.1) |
| 16 colours 320 x 200, 40-byte rows | 8,000 | **8** | 7 | needs a 40-byte row layout (the plan's wave 5) |
| 16 colours 320 x 240, as today | 19,200 | 3 | 2 | |
| 16 colours 320 x 240, 40-byte rows | 9,600 | 6 | 5 | |
| 16 colours 640 x 350 / 640 x 400 | 28,000 / 32,000 | 2 | 1 | as today |
| Hercules | 32 KB | 2 | 1 | as today, a lone card only |

**The owner's "seven page-aheads in 320 x 200 x 16" assumes rows 40 bytes
apart.** The player's sixteen-colour screens keep `lin80`'s 80-byte stride
at every width - `vp_scrset` writes the CRTC Offset as 40 words on all four -
so that screen holds 4 pages today, not 8. The seven-page rows below
measure what a 40-byte layout would buy, because the model does not care
where the pages are.

## 2. How it was measured

`tools/os88vidpages.py` runs the SHIPPED encoder and nothing else.
- **The plan pass** is os88venc's own `_paok` pass with page-ahead on. Every
  frame's target is kept.
- **Each real pass** is os88venc's `_encode` with `PageAhead` replaced by
  `PageAheadK`, which has K hidden pages. Each page is built toward a
  target of its own, and the spare goes to the nearest target first. The
  per-frame spend (cycles, safe bytes, the slow disk's refill, the
  super-packet's room) is `PageAhead.after`'s own, shared across the
  pages, and each extra page's record is charged as a record of its own.
  At a target, the page built for it is flipped to if it is nearer the
  target than the shown page. The page left behind is then free for the
  next target nobody is building.
- **The preload** builds every page's target whole at key 0, as today's
  preload builds one (98.2.1.8, keys in parts).
- **K = 1 through `PageAheadK` was checked against the shipped class** on
  the `pairs` clip at Mode X on the fast 286: identical frame for frame,
  and the same frames cut. `--check` repeats it.
- **The file a K > 1 pass writes is not a format** (it carries one page's
  hidden records) and is thrown away unverified. Only the encoder's
  figures are read.

**THE TARGETS ARE THE TRAP, and the first run fell into it.** Today a target
is each cut run's first frame. On a 5150 the frames between two close cuts
are cut too, so the run swallows the second cut and the second scene
change is never a target at all. The first fix tried, every cut frame a
target (`--targets all`), made two cuts 2 frames apart WORSE on two pages
than on one (10 -> 14 cut frames, Mode X, slow 286). Frame 77, scene A
with its box moved two pixels, was a target, so the second page was spent
building scene A again while scene B at frame 78 went unbuilt. One page
dodged it only by accident: the frame after a flip is never a target. The
targets used below are **`scenes`**: each run's first frame, plus every
cut frame whose picture differs from the frame before by more than 20% of
its pixels. The encoder needs that rule whatever is built.

**The clips** (`os88vidpages.py clip`) are 320 x 240 at 30 fps, encoded at
the presets' 25. Each is made of the eight PiXEL sample photographs. A calm
stretch is a still with a 48-pixel box moving across it, which is the
2026-10-13 report's `still`.

| clip | what |
|---|---|
| `still` | 8 stills, 3 s each, a hard cut between them (the control) |
| `pairs` | a 3 s calm, then two hard cuts 6 frames apart, four times |
| `pairs2` | the same, 2 frames apart |
| `triples` | a 3 s calm, then three hard cuts 5 frames apart, three times |
| `burst` | a 3 s calm, then 1 s in which every pixel moves (a fast pan and zoom), four times |
| `burst10` | the same, the action 10 frames |
| `opening` | a hard cut every 4 frames for the first 32, then a 5 s calm |

**The machines**: `mx` = `--preset modex`, and `v4a` = `--preset vga4
--screen 320x200 --palette colour16`, each on `5150-st225` (the owner's
5150, its VGA modelled by the profile) and on `286-slow`.

## 3. Results

Each cell is: frames cut / worst second's error as seen % / seconds visibly
broken, then the flips. **plain** is no page-ahead. **today** is the shipped
page-ahead (one page, run starts). The K columns are this report's,
`scenes` targets, K hidden pages.

| run | plain | today | K=1 | K=2 | K=3 | K=5 | K=7 |
|---|---|---|---|---|---|---|---|
| pairs2, mx 5150 | 43 / 4.70 / 1.4 | 40 / 3.72 / 1.2 | 39 / 3.48 / 1.2, 7 | 26 / 3.31 / 0.8, 12 | **6** / 3.31 / 0.2, 12 | - | - |
| pairs2, mx 286s | 21 / 1.59 / 0.6 | 18 / 1.04 / 0.5 | 10 / 0.80 / 0.2, 8 | **2** / 0.35 / 0.0, 12 | 2 / 0.35 / 0.0, 12 | - | - |
| pairs2, v4a 5150 | 72 / 6.29 / 2.2 | 64 / 5.55 / 1.9 | 65 / 5.55 / 1.9, 11 | 51 / 5.55 / 1.5, 12 | **11** / 5.55 / 0.4, 12 | 11 | 11 |
| pairs2, v4a 286s | 31 / 2.24 / 1.0 | 26 / 1.65 / 0.7 | 24 / 1.68 / 0.6, 12 | 6 / 1.08 / 0.1, 12 | **3** / 1.08 / 0.1, 12 | 3 | 3 |
| triples, mx 5150 | 62 / 7.35 / 2.2 | 50 / 5.48 / 1.7 | 50 / 4.29 / 1.5, 6 | 17 / 3.32 / 0.5, 12 | **6** / 3.32 / 0.2, 12 | - | - |
| triples, mx 286s | 25 / 0.63 / 0.4 | 2 / 0.34 / 0.0 | 2 | 2 | 2 | - | - |
| triples, v4a 5150 | 94 / 9.68 / 2.9 | 79 / 8.03 / 2.4 | 78 / 6.94 / 2.4, 7 | 57 / 5.54 / 1.7, 10 | 21 / 5.54 / 0.4, 12 | **11** / 5.54 / 0.4, 12 | 11 |
| triples, v4a 286s | 43 / 2.28 / 1.1 | 3 / 1.02 / 0.1 | 3 | 3 | 3 | 3 | 3 |
| burst10, mx 5150 | 63 / 7.48 / 2.2 | 59 / 6.22 / 2.0 | 59 / 6.26 / 2.1, 7 | 55 / 5.90 / 2.0, 11 | **51** / 5.40 / 1.7, 14 | - | - |
| burst10, mx 286s | 43 / 2.87 / 1.5 | 39 / 2.56 / 1.3 | 38 / 2.56 / 1.3, 7 | 34 / 2.37 / 1.1, 11 | **30** / 2.22 / 1.0, 14 | - | - |
| burst10, v4a 5150 | 99 / 10.69 / 3.1 | 95 / 8.79 / 3.0 | 95 / 8.16 / 3.0, 6 | 91 / 7.86 / 2.8, 10 | 87 / 8.29 / 2.7, 13 | 79 / 6.39 / 2.4, 22 | **72** / 6.97 / 2.2, 28 |
| burst10, v4a 286s | 57 / 4.25 / 1.9 | 53 / 3.74 / 1.7 | 53 / 3.76 / 1.6, 7 | 49 / 3.56 / 1.5, 11 | 45 / 3.30 / 1.4, 14 | 37 / 2.66 / 1.2, 23 | **27** / 2.36 / 0.8, 29 |
| burst, mx 5150 | 131 / 10.87 / 4.9 | 127 / 10.66 / 4.6 | 127 / 10.66 / 4.6, 4 | 122 / 10.55 / 4.5, 10 | **118** / 10.49 / 4.4, 14 | - | - |
| burst, mx 286s | 131 / 9.02 / 4.0 | 124 / 8.75 / 3.8 | 124 / 8.75 / 3.8, 4 | 121 / 7.83 / 3.8, 10 | **118** / 7.58 / 3.7, 14 | - | - |
| burst, v4a 5150 | 176 / 14.07 / 5.7 | 173 / 12.79 / 5.5 | 173 / 12.76 / 5.5, 7 | 169 / 12.84 / 5.4, 11 | 165 / 12.79 / 5.3, 15 | 157 / 12.70 / 5.0, 23 | **149** / 12.70 / 4.7, 28 |
| burst, v4a 286s | 133 / 5.65 / 3.8 | 127 / 5.55 / 3.5 | 127 / 5.55 / 3.5, 7 | 123 / 5.53 / 3.3, 11 | 119 / 5.40 / 3.3, 15 | 111 / 5.07 / 3.0, 23 | **103** / 4.74 / 2.9, 29 |
| opening, mx 5150 | 40 / 12.18 / 1.4 | 32 / 10.98 / 1.2 | 35 / 10.89 / 1.3, 3 | 30 / 8.55 / 1.0, 4 | **22** / 5.45 / 0.6, 5 | - | - |
| opening, mx 286s | 31 / 10.34 / 1.0 | 25 / 8.07 / 0.8 | 23 / 7.97 / 0.8, 4 | 16 / 4.95 / 0.5, 6 | **9** / 1.47 / 0.2, 8 | - | - |
| opening, v4a 5150 | 51 / 14.81 / 1.6 | 48 / 15.02 / 1.5 | 47 / 13.51 / 1.5, 4 | 43 / 12.79 / 1.4, 3 | 38 / 10.02 / 1.2, 7 | 28 / 5.95 / 0.8, 7 | **11** / 5.95 / 0.3, 8 |
| opening, v4a 286s | 37 / 8.24 / 1.0 | 25 / 5.39 / 0.7 | 20 / 4.00 / 0.5, 7 | 7 / 1.21 / 0.2, 8 | **3** / 1.21 / 0.1, 8 | 3 | 3 |

**The control**: `still` (one scene change per 3 s) is the same on every K -
6, 2, 12 and 4 frames cut on the four machines - because one page already
builds every change. More pages bought nothing there and cost nothing.

## 4. What it shows

- **Back-to-back transitions are the case, and they are won.** Today's one
  page leaves a second (and third) cut that comes before the page can be
  rebuilt. Three pages ahead take two cuts 2 frames apart on the 5150's
  Mode X from 40 cut frames to 6. On the slow 286 two pages are enough.
  **This is the 320 x 200 Mode X screen's fourth page at work**: Mode X
  320 x 240 has two pages ahead, and the third is what takes the 5150 from
  26 to 6 on `pairs2` and from 17 to 6 on `triples`.
- **The opening is the preload, and it is won on the 286.** Key 0 holds
  every page's target whole, so the first K scene changes need nothing from
  the stream. Mode X on the slow 286: 25 cut frames today, 9 on three pages
  ahead, and the worst second 8.07% -> 1.47%. On the 5150 the preload
  helps less (32 -> 22), because after it no frame has spare to rebuild a
  page.
- **A burst is a page a frame, no more.** On sixteen colours 320 x 200 the
  K=7 run flips 28 times over four bursts, which is 7 a burst. Of a
  10-frame burst, seven frames come up whole. On the slow 286 that takes
  `burst10` from 53 cut frames to 27. A 1-second burst (25 frames) gains
  proportionally less (127 -> 103), because the pages run out a third of
  the way in. **What bounds it is the calm before**, and the page count:
  each burst frame was ~20 KB of build on this clip (559 KB for 28 flips),
  and a page holds one frame however much spare the calm left.
- **Disk-bound machines gain least.** Where the disk binds, the calm's
  spare bytes are what an XMS bank would already bank (the 2026-10-13
  report's opening, docs/reports/VIDEO-PAGEAHEAD-2026-10-13.md).
  Page-ahead moves DECODE forward, so it pays most where the CPU is what
  cuts.

## 5. Tear-free and built ahead at once (`--flip-ahead`)

`--flip` (98.3.8) flips every frame on two pages. Building ahead at the same
time needs a third, and the trouble is the frame AFTER a built flip. The
page now shown holds frame F, and the back page the next frame is drawn on
holds F-1 or F-2. It has to be brought up to F, and after a scene change
that is every row: a whole-canvas latch copy (98.3.8.1). Its price, off
the profiles' own `lcopy_us`:

| machine | 320 x 240 copy | 320 x 200 copy | a 25 fps period |
|---|---|---|---|
| 5150 | 302% | 252% | 190,909 cycles |
| 286-slow | 101% | 84% | 572,727 |
| 286-fast | 102% | 85% | 859,091 |
| 486-dx2-66 | **66%** | **55%** | 1,909,091 |

- **On three pages (Mode X 320 x 240) the frame after the flip pays it,
  and only the 486 can.** There, with the copy charged to that frame:
  `still` 15 -> 2 cut frames, `pairs` 22 -> 2, `pairs2` 21 -> 14 (one page
  ahead) and -> 5 (a fourth page, two ahead). On the fast 286 the copy
  overruns the period, and a silent flipped play that falls behind stays
  behind (98.2.1.1.1). Run with the copy charged anyway, every frame after
  the first flip was cut (401 of 476). With flips refused when the copy
  cannot be paid (`--copy-ok 0.25`), nothing flips at all, which is the
  plain flipped file. **The owner's "486 or Pentium territory" is right for
  three pages.**
- **On FOUR pages the copy moves into the calm** (`--precopy`). Two pages
  flip, one is built ahead, and once it is whole the fourth is copied from
  it in bands out of spare cycles. The frame after the flip then draws on
  the copy and pays nothing. Fast 286, Mode X, flips refused when the copy
  is unfinished (`--copy-ok 0.25`): `still` 16 -> **2**, `pairs2` 26 -> 18,
  `burst` 116 -> 108; and `pairs` 26 -> **2** on the run without the
  refusal, where no flip came before its copy was done. **That is Mode X 320 x 200, and it is what puts
  tear-free play with whole scene changes on a 286.**

## 6. What it costs

- **Stream bytes: nothing it would not otherwise spend.** Every byte a
  page takes is one the shipped forecast says no later frame needs.
- **Keys: a picture per page in flight.** Measured at every key of the
  K-page runs (the bytes each hidden page's writes from the key's own
  picture would take, against the key alone):

  | run | key | pages carried, K=1 | K=3 | K=7 |
  |---|---|---|---|---|
  | pairs2, mx 5150 | 81 KB | 40 KB | 121 KB | - |
  | triples, v4a 5150 | 52 KB | 25 KB | 83 KB | 191 KB |
  | opening, v4a 5150 | 55 KB | 27 KB | 84 KB | 196 KB |
  | burst10, v4a 5150 | 49 KB | 26 KB | 77 KB | 168 KB |

  **Every key carries every page, whether or not a target is near**, because
  that is today's rule (98.1.3.7). Seven pages make a seek read ~250 KB
  where today's reads ~78 KB, about two seconds of an ST-225 against
  two-thirds of one. Most of
  those pages hold a picture that will never be flipped to, so the plan's
  key rule (VIDEO-PAGES-PLAN §3.4) carries only pages with a target
  ahead.
- **Encode time**: seven pages took 0% to +45% over one on the
  sixteen-colour 5150 runs (`opening` 53 s both; `burst10` 109 -> 158 s),
  because a page is built only while it has a target and spare to spend.
- **The player**: not measured. It is VIDEO-PAGES-PLAN §4's estimate, and no kernel
  byte.

## 7. Not modelled

- **The flip's retrace wait** (98.3.8.3), as in the 2026-10-13 report.
- **The full-screen text** (98.3.13) on pages it was not drawn on. That is
  player bookkeeping and not decode.
- **Keys carrying only the pages with a target ahead.** The cost above is
  the upper bound.
- **Mode X 320 x 200's smaller frame.** Every Mode X row is a 320 x 240
  canvas. 320 x 200's records are smaller and its copy is 85% of 240's, so
  its rows here are pessimistic.
- **Real clips.** None were in the container. `os88vidpages.py SRC --pages
  1,2,3 --targets scenes -- <the file's options>` is the run to take on the
  owner's own.
