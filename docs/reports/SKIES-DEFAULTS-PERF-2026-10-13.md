# Clear Skies on default settings, before and after `clearskies-misc` — measured 2026-10-13

**A MEASUREMENT** (docs/README.md): true of the two trees below and of no
other. A later measurement is a new file.

The question, from the field: *"do a full performance test — tell me how much
we moved on default settings for all three adapters on an 8088."*

- **Trees**: "base" is `f0533e1b`, the `elendilon` commit `clearskies-misc`
  was cut from, built in its own worktree; "head" is `clearskies-misc` at
  `45b0dc66` (the road ink, SPEC.md 88.13.1.5.3). Both run the SAME
  instrument, `tests/skiesperf.py` as of `45b0dc66`, copied into the base
  worktree; its haze stage is optional so the base listing has every site
  the instrument asks for.
- **Box**: the 4-core cloud container this session ran in, MartyPC at 4.77
  MHz, four runs at a time. Every figure is guest CYCLES over the 8088's
  clock, so the host's load does not enter it.
- **Machines**: `os8088_5150_herc_gla` (Hercules), `os8088_5150_cga_gla`
  (CGA) and `os8088_xt_vga` (an 8088 XT with a VGA, which flies Mode X).
- **Settings**: untouched — a fresh disk, no `CSSET.DAT`, so each tree flies
  what an 8088 starts on. That is Detail Moderate, Draw Distance Moderate,
  the adapter's own size, and the fill byte: base `3` (Terrain, Buildings),
  head `11` (Terrain, Buildings and the Texture, SPEC.md 88.13.1.5.2). The
  haze is off on an 8088 by default in head and absent in base; Ultra is
  never a default.
- **Method**: `skiesperf --scene S --frames 8 --warm 24 --nostages`, the
  world paused and the aeroplane pinned, 24 frames thrown away so the cull
  has filed the teleported world, then the mean of 8 consecutive frames with
  the tick wait patched out. On VGA `--work` as well, below. Object counts
  agree between the two trees in every row, so both arms drew the same
  world.

## Hercules and CGA: whole frame

| scene | Herc base | Herc head | | CGA base | CGA head | |
|---|---:|---:|---:|---:|---:|---:|
| `runway` | 159.28 ms | 160.31 ms | +0.6% | 184.21 ms | 185.02 ms | +0.4% |
| `climb` | 136.01 | 136.75 | +0.5% | 155.01 | 155.65 | +0.4% |
| `bank` (30°) | 182.87 | 183.20 | +0.2% | 216.02 | 216.53 | +0.2% |
| `city` | 161.23 | 161.96 | +0.5% | 209.49 | 210.55 | +0.5% |
| `ground` | 198.59 | 199.78 | +0.6% | 241.13 | 242.24 | +0.5% |
| `groundbank` (25°) | 254.41 | 255.10 | +0.3% | 302.15 | 302.85 | +0.2% |
| **sum** | **1,092.4** | **1,097.1** | **+0.4%** | **1,308.0** | **1,312.8** | **+0.4%** |

0.3 to 1.2 ms a frame. That is the per-frame bookkeeping the branch added —
`cs_grsync`'s compare, the hill shades' one `cmp` a face (SPEC.md 88.4.4.1),
the road ink's compare an edged model — and no texture cost at all: CGA's
dotted ground and Hercules' ground are patterns the row fill always looked up.

## VGA (Mode X): the frame's WORK

A Mode X frame ends in the page flip, which waits for the retrace, so a
whole-frame time comes in steps of 16.75 ms: the first pass of this
measurement read +16.75 ms in four scenes and +0.00 in two, which is a scene
crossing a retrace boundary or not and says little about the work. So the VGA
rows are `--work`: cycles from `cs_render`'s entry to `cs_r_end`'s, the frame
less that wait.

| scene | base | head | |
|---|---:|---:|---:|
| `runway` | 215.97 ms | 222.39 ms | **+6.42 ms, +3.0%** |
| `climb` | 192.06 | 197.33 | +5.27 ms, +2.7% |
| `bank` (30°) | 241.78 | 265.60 | **+23.82 ms, +9.9%** |
| `city` | 238.11 | 245.65 | +7.54 ms, +3.2% |
| `ground` | 261.58 | 270.25 | +8.67 ms, +3.3% |
| `groundbank` (25°) | 322.56 | 344.07 | **+21.51 ms, +6.7%** |

Almost all of it is the Texture's latched checker (SPEC.md 88.13.1.5.2): a
ground run on Mode X arms write mode 1, reads the latch byte and disarms, and
on an 8-bit 8088 bus that is port I/O and call overhead a row. Level, the
ground rows are whole rows and the mode stays armed across them, so it is
5–9 ms; banked, every row is split at the horizon and pays the mode change
either side of its ground run, so it is 21–24 ms. A 286 pays a fraction of
either, and the Texture box (F7) is the A/B a player has: off, an 8088 VGA
frame is the base's work plus the same 0.4% the other adapters pay.

## What it says

- **Hercules: +0.4%. CGA: +0.4%.** On default settings an 8088 cannot tell.
- **VGA: +3% level, +7–10% banked**, all of it the ground's new texture,
  which is on by default because it measured free on CGA. On an 8088 with a
  VGA — the slowest machine that can run Mode X at all — that is the one
  default this branch moved, and the obvious next lever is the banked split
  row's two mode changes, which a two-pass band (every row's sky side, then
  every row's ground side) would take from one a row to one a band.
