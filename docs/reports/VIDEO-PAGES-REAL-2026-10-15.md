# VIDEO-PAGES-REAL - page-ahead on N pages, the owner's clips (2026-10-15)

**Measured on**: branch `video-pages-2` at `f384ba3a`, in a four-core
container, with the shipped encoder (`tools/os88venc.py`). The encoder did
not change for any mode measured here between that commit and the one that
adds this report: two-page, N-page and plain flipped encodes were checked
byte-identical against it (SPEC.md 98.2.1.8.5). **Nothing here was played on
a machine**: every figure is the encoder's model of the `286-picomem1`
profile, the owner's Packard Bell Victory 286.

The question docs/plans/VIDEO-PAGES-PLAN.md §9 left open: *do the owner's
real clips have the close scene changes the synthetic ones were made of, and
what do more pages buy them?* The owner's caveat stands. These are clips
picked because they already play well, so they are not the case the
feature is for: *a calm video with several sudden transitions does not
work today, so none has been tried.*

## How

Five clips, each encoded the way the owner encodes them, at 24 fps:

```
--profile 286-picomem1 --bank 2944 --memory 309 --fps 24
--page-ahead on --page-prune off
```

GasGas uses the owner's section, `--start 40 --end 110.7`. Each clip was
encoded five ways:

| name | what |
|---|---|
| `mx 2` | `--preset modex`, two pages - today's page-ahead |
| `mx max` | the same on all three of Mode X 320 x 240's pages |
| `v4 2` | `--preset vga4 --screen 320x200 --palette colour16`, two pages |
| `v4 max` | the same on all four of 320 x 200's pages |
| `v4n max` | `--screen 320x200n`, the NARROW screen (SPEC.md 98.1.3.2.5): eight pages |

The columns are the encoder's own report: frames cut to the budget, seconds
VISIBLY broken (2% of the frame or more off by 15% as seen), the worst
second's mean error, the scene changes built ahead and flipped to against
the targets, the KB the builds spent, the file, and its front matter (the
keys, which carry every page).

## Results

| clip | run | cut | broken s | worst s | flips | build KB | file KB | front KB |
|---|---|---:|---:|---:|---|---:|---:|---:|
| Bebop | mx 2 | 13 | 0.2 | 0.83% | 29/29 | 831 | 17,726 | 2,203 |
| | mx max | **9** | 0.1 | 0.63% | 35/44 | 1,013 | 19,027 | 3,431 |
| | v4 2 | 3 | 0.1 | 0.12% | 22/23 | 417 | 12,627 | 1,344 |
| | v4 max | **0** | 0.0 | 0.00% | 27/27 | 465 | 14,384 | 3,128 |
| | v4n max | **0** | 0.0 | 0.00% | 27/27 | 363 | 17,395 | 6,521 |
| GasGas | mx 2 | 13 | 0.2 | 0.66% | 22/22 | 491 | 11,146 | 1,725 |
| | mx max | **8** | 0.1 | 0.47% | 29/35 | 621 | 12,161 | 2,742 |
| | v4 2 | 0 | 0.0 | 0.00% | 2/2 | 15 | 7,152 | 958 |
| | v4 max | 0 | 0.0 | 0.00% | 2/2 | 0 | 7,686 | 1,507 |
| | v4n max | 0 | 0.0 | 0.00% | 2/2 | 0 | 7,535 | 1,510 |
| LastExileOP | mx 2 | 320 | 3.1 | 2.19% | 82/126 | 2,070 | 37,361 | 2,940 |
| | mx max | 298 | 3.0 | 2.16% | 110/398 | 2,821 | 39,086 | 4,430 |
| | v4 2 | 110 | 1.2 | 1.92% | 54/66 | 1,058 | 22,293 | 1,634 |
| | v4 max | 90 | 1.0 | 1.67% | 85/210 | 1,764 | 24,651 | 3,798 |
| | v4n max | **61** | **0.5** | **0.87%** | 110/209 | 2,193 | 28,643 | 8,273 |
| TakeOnMe | mx 2 | 14 | 0.1 | 0.92% | 20/27 | 770 | 12,642 | 1,677 |
| | mx max | **8** | 0.1 | 0.92% | 30/40 | 1,026 | 13,577 | 2,513 |
| | v4 2 | 10 | 0.1 | 0.92% | 111/113 | 2,181 | 12,901 | 1,061 |
| | v4 max | 8 | 0.1 | 0.92% | 114/123 | 2,720 | 14,436 | 2,200 |
| | v4n max | **5** | 0.1 | 0.87% | 113/123 | 3,238 | 16,740 | 4,637 |
| Thundercats | mx 2 | 607 | 10.3 | 6.71% | 53/214 | 1,173 | 30,251 | 2,546 |
| | mx max | 590 | 10.2 | 6.69% | 81/641 | 1,687 | 31,790 | 3,841 |
| | v4 2 | 185 | 3.1 | 3.57% | 140/168 | 2,304 | 18,350 | 1,368 |
| | v4 max | 144 | 2.2 | 3.37% | 189/352 | 3,747 | 20,366 | 2,913 |
| | v4n max | **101** | **1.2** | **2.77%** | 232/351 | 4,801 | 23,608 | 6,332 |

## What it shows

1. **More pages never made a clip worse, and in sixteen colours every
   page helped.** Wherever there is anything left to cut, each step from
   two pages to four to eight cuts fewer frames: LastExileOP 110 -> 90 ->
   61, Thundercats 185 -> 144 -> 101, TakeOnMe 10 -> 8 -> 5, Bebop 3 -> 0.
   The two busy clips also halve their seconds visibly broken at eight
   pages (LastExileOP 1.2 -> 0.5, Thundercats 3.1 -> 1.2).
2. **Mode X's third page is worth having on the calmer clips, and nothing
   on the busy ones.** Bebop 13 -> 9, GasGas 13 -> 8, TakeOnMe 14 -> 8.
   LastExileOP and Thundercats in Mode X are cut by the PER-FRAME CEILING
   frame after frame (320 and 578 of their cuts). That is the CPU's
   failure, not a scene change's, and no page in the calm can pay a
   frame that has no calm before it. Their target lists grow with pages
   (126 -> 398, 214 -> 641) because on N pages every cut frame that
   changes 20% of its pixels is a target of its own (SPEC.md 98.2.1.8.4),
   and on these two clips most cut frames do.
3. **The real clips DO have close scene changes.** On every clip but GasGas
   the targets on N pages outnumber two pages' targets by more than the
   flips do. Those extra targets are scene changes inside a cut run, which
   two pages cannot even aim at. TakeOnMe in sixteen colours already
   builds and flips to 111 of its 113 targets on two pages: a pencil-sketch
   video is a run of small scene changes.
4. **The cost is the keys, not the stream.** The stream grows only by what
   the builds spend, and every byte of that is one the forecast says no
   later frame needs (98.2.1.8). The front matter grows because a key
   carries every page in flight, so at eight pages it is four to five
   times two pages' (Bebop 1,344 -> 6,521 KB, LastExileOP 1,634 -> 8,273),
   and the file is 16-38% bigger. It is read only at a seek, so it costs
   disk space, and it costs no play anything. `--page-keys started`
   (98.2.1.8.4) is the option that trades some of it back, and it was not
   measured here.
5. **GasGas's section has nothing to build.** Two targets in sixteen
   colours, both built on two pages and both preloaded by key 0 on four.
   It is the "plays already" case, as the owner said it would be.

## Not measured

- **A play on the machine.** These are the encoder's model of the Victory.
  The player's N-page path is gated on MartyPC (`vidpage*` rows), but
  nothing has timed one of these files on the owner's machine.
- **The flipped-and-built-ahead mode** (SPEC.md 98.1.3.7.2) on these clips.
  It landed after this run, and the owner encodes unflipped.
- **The clip the feature is for**: a calm video with several sudden
  transitions. The synthetic `still`, `pairs` and `burst` clips of
  docs/reports/VIDEO-PAGES-2026-10-15.md stand in for it.
