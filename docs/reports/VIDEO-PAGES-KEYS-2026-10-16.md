# VIDEO-PAGES-KEYS - what the keys' pages cost, and the two ways down (2026-10-16)

**Measured on**: branch `video-pages-2` at `adfa0de3` (the page pool,
SPEC.md 98.1.3.8.1), in a four-core container, with the shipped encoder.
The `all / off` rows are docs/reports/VIDEO-PAGES-REAL-2026-10-15.md's own
files; two-page and N-page encodes with the pool off are byte-identical
between the two trees, and every other row was made here. **Nothing was
played on a machine** - every figure is the encoder's model of the
`286-picomem1` profile, the owner's Packard Bell Victory 286.

The owner's question (2026-10-16): *do the keys' pages overlap - is the same
page stored in several keys?* Mostly, yes - docs/plans/VIDEO-PAGES-PLAN.md
§10 counts it. This is what the two ways of not storing it again buy:
`--page-keys started` (98.2.1.8.4: a key leaves out a page whose build has
not begun) and `--key-pool on` (98.1.3.8.1: a picture several keys carry,
stored once).

## How

The owner's five clips, sixteen colours at 320 x 200, on all four pages
(`--screen 320x200`) and all eight (`--screen 320x200n`):

```
--preset vga4 --palette colour16 --profile 286-picomem1 --bank 2944
--memory 309 --fps 24 --page-ahead on --page-prune off --pages max
--page-keys all|started --key-pool off|on
```

GasGas is the owner's section, `--start 40 --end 110.7`. **front KB** is the
front matter - keys, the pool, the tables - read only at a seek; **stream
KB** is the rest, what a play reads.

## Results

| clip | pages | keys | pool | cut | broken s | front KB | stream KB | file KB |
|---|---|---|---|---:|---:|---:|---:|---:|
| Bebop | 4 | all | off | 0 | 0.0 | 3128 | 11256 | 14384 |
| Bebop | 4 | started | off | 0 | 0.0 | 2490 | 11317 | 13808 |
| Bebop | 4 | started | on | 0 | 0.0 | 849 | 11317 | 12167 |
| Bebop | 4 | all | on | 0 | 0.0 | 956 | 11256 | 12213 |
| Bebop | 8 | all | off | 0 | 0.0 | 6521 | 10873 | 17395 |
| Bebop | 8 | started | off | 0 | 0.0 | 4609 | 11030 | 15639 |
| Bebop | 8 | started | on | 0 | 0.0 | 829 | 11030 | 11860 |
| Bebop | 8 | all | on | 0 | 0.0 | 1079 | 10873 | 11953 |
| GasGas | 4 | all | off | 0 | 0.0 | 1507 | 6179 | 7686 |
| GasGas | 4 | started | off | 0 | 0.0 | 772 | 6209 | 6981 |
| GasGas | 4 | started | on | 0 | 0.0 | 376 | 6209 | 6585 |
| GasGas | 4 | all | on | 0 | 0.0 | 405 | 6179 | 6584 |
| GasGas | 8 | all | off | 0 | 0.0 | 1510 | 6024 | 7535 |
| GasGas | 8 | started | off | 0 | 0.0 | 748 | 6054 | 6802 |
| GasGas | 8 | started | on | 0 | 0.0 | 364 | 6054 | 6418 |
| GasGas | 8 | all | on | 0 | 0.0 | 394 | 6024 | 6418 |
| LastExileOP | 4 | all | off | 90 | 1.0 | 3798 | 20853 | 24651 |
| LastExileOP | 4 | started | off | 91 | 1.0 | 3232 | 20896 | 24128 |
| LastExileOP | 4 | started | on | 91 | 1.0 | 1838 | 20896 | 22734 |
| LastExileOP | 4 | all | on | 90 | 1.0 | 2045 | 20853 | 22898 |
| LastExileOP | 8 | all | off | 61 | 0.5 | 8273 | 20369 | 28643 |
| LastExileOP | 8 | started | off | 61 | 0.5 | 6607 | 20481 | 27088 |
| LastExileOP | 8 | started | on | 61 | 0.5 | 2484 | 20481 | 22966 |
| LastExileOP | 8 | all | on | 61 | 0.5 | 2796 | 20369 | 23165 |
| TakeOnMe | 4 | all | off | 8 | 0.1 | 2200 | 12236 | 14436 |
| TakeOnMe | 4 | started | off | 12 | 0.1 | 2040 | 12430 | 14470 |
| TakeOnMe | 4 | started | on | 12 | 0.1 | 1408 | 12430 | 13838 |
| TakeOnMe | 4 | all | on | 8 | 0.1 | 1567 | 12236 | 13803 |
| TakeOnMe | 8 | all | off | 5 | 0.1 | 4637 | 12102 | 16740 |
| TakeOnMe | 8 | started | off | 6 | 0.1 | 3910 | 12373 | 16283 |
| TakeOnMe | 8 | started | on | 6 | 0.1 | 1952 | 12373 | 14325 |
| TakeOnMe | 8 | all | on | 5 | 0.1 | 2679 | 12102 | 14782 |
| Thundercats | 4 | all | off | 144 | 2.2 | 2913 | 17453 | 20366 |
| Thundercats | 4 | started | off | 149 | 2.3 | 2248 | 17521 | 19770 |
| Thundercats | 4 | started | on | 149 | 2.3 | 2019 | 17521 | 19541 |
| Thundercats | 4 | all | on | 144 | 2.2 | 2609 | 17453 | 20062 |
| Thundercats | 8 | all | off | 101 | 1.2 | 6332 | 17276 | 23608 |
| Thundercats | 8 | started | off | 106 | 1.3 | 4573 | 17397 | 21970 |
| Thundercats | 8 | started | on | 106 | 1.3 | 3526 | 17397 | 20923 |
| Thundercats | 8 | all | on | 101 | 1.2 | 4887 | 17276 | 22163 |

## What it shows

1. **The pool changes the keys and nothing else.** Against `all / off`,
   `all / on` has the same frames cut, the same seconds broken and the
   stream to the kilobyte, and its front matter falls by 83% on Bebop at
   eight pages (6,521 -> 1,079 KB), 74% on GasGas, 66% on Last Exile, 42%
   on TakeOnMe and 23% on Thundercats. The whole file is 31% smaller on
   Bebop and 19% on Last Exile at eight pages.
2. **`started` costs the picture where there is anything to cut.** It
   leaves pages out of keys, so the stream rebuilds them, so the builds
   after a key spend again. TakeOnMe at four pages goes 8 -> 12 cut
   frames, Thundercats 144 -> 149 and 101 -> 106, and the stream grows 0.1
   to 2%. On the two calm clips nothing is cut either way.
3. **`started` with the pool is the smallest file and not the best one.**
   It is a few hundred KB under the pool alone on most clips - the pages it
   leaves out - and carries `started`'s cuts with it.
4. **The defaults stand**: `--page-keys all --key-pool on` - the picture
   of `all`, the keys of the pool.

## Not measured

- **A seek's time.** With the pool a seek reads the key and then a picture
  per page it names - about the reads and bytes of an eight-page key in
  parts, from more places. Nothing here timed one on the Victory.
- Mode X and flipped-and-built-ahead files, which pool the same way.
