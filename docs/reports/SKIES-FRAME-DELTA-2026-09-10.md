# Clear Skies' frame, start of the thread to now — every scene

**A measurement, not a description.** Taken 2026-09-10 on a four-core cloud
container (Intel Xeon @ 2.80 GHz, 15 GB), `nasm` 2.16.01, MartyPC at the
pinned commit `e15cb04f`, guest `os8088_5150_herc_gla` — a cycle-accurate
4.77 MHz 8088 with a Hercules card. Every figure comes from
`tests/skiesprof.py` at tier 2, which brackets each stage at its CALL SITE
and subtracts the emulator's own cycle counter, so no arm is compared with
another to get a number. It is true of the two commits it names and of no
other tree; a later measurement is a new file.

## The two points

| | commit | what it is |
|---|---|---|
| **BASE** | `e6e1b94` | *Merge branch 'claude/martypc-go-resume-detection-lcj0jx' into elendilon*, **2026-09-08 06:13** — the elendilon commit the Clear Skies thread was cut from. `git merge-base` of the FIRST of this branch's four merges |
| **NOW** | `5a21837` | *skies: the ground's stale ink is FIXED*, **2026-09-10 09:55** |

The branch has merged into `elendilon` **four times** since (`019ab1e`,
`bb89ccb`, `9cee9df`, `1f07e7c`) and been re-cut after each, so its own log
is not the span. The span is BASE→NOW: **45 commits touching `apps/skies/`**
and **81 new `SPEC.md §88` sections**.

**The base was BUILT, not modelled.** `git worktree` at `e6e1b94`, a full
`make` in it, and the profiles run against its own floppies. Two files were
carried in from the current tree so the same instrument drives both arms:
`tests/skiesprof.py` itself (so the SCENES are identical by construction —
the profile table is one file) and `tools/os88marty.py` (the base's has no
`bp_trace`). Both are harness, neither is under test. MartyPC is shared by
symlink. **Every tier-1 and tier-2 bracket resolves at both ends** — checked
before the run — which is why tier 2 is the depth reported; tier 3 does not,
`cs_axcull` being this thread's own (§88.5.12).

## The frame

40 frames traced per run, the first dropped (the frame after the arming poke
redraws the whole panel), **median** of the remaining 39, and each arm run
**twice in independent guests**. The median is the statistic because a frame
here has a long tail — one arm's own min and max differ by 27% — and a mean
carries whichever outliers a run happened to catch.

| scene | base ms | now ms | delta | | base fps | now fps |
|---|---|---|---|---|---|---|
| `cruise` | 162.20 | 150.91 | **−11.29** | **−7.0%** | 6.17 | 6.63 |
| `bank` | 157.06 | 146.10 | **−10.96** | **−7.0%** | 6.37 | 6.84 |
| `rollsweep` | 294.44 | 240.54 | **−53.91** | **−18.3%** | 3.40 | 4.16 |
| `turnhold` | 294.17 | 256.74 | **−37.43** | **−12.7%** | 3.40 | 3.89 |
| `sparse` | 103.65 | 68.42 | **−35.23** | **−34.0%** | 9.65 | 14.61 |
| `slightbank` | 232.91 | 209.69 | **−23.22** | **−10.0%** | 4.29 | 4.77 |
| `climb` | 155.41 | 148.11 | **−7.30** | **−4.7%** | 6.43 | 6.75 |
| `descend` | 118.28 | 109.60 | **−8.68** | **−7.3%** | 8.45 | 9.12 |
| **mean of the eight** | **189.77** | **166.26** | **−23.50** | **−12.4%** | **5.27** | **6.01** |

**The BANKED scenes are where the thread went, and the table says so.** A
level cruise is −7% and a sweeping bank −18%; the worst frame in the set
(`rollsweep`, 294 ms) came down 54 ms. That is not an accident of what was
easy — §88.3.1 through §88.3.2 are all about the rolled horizon, and a bank
is the state they are about.

## Where the time went

Median ms, both runs, the same 39 frames. `RENDER` is the whole draw;
`scene` is the object pass and `skyground` the sky/ground fill; `blit` is
the shadow reaching the card; `panel` is the cockpit.

| stage | `cruise` | `rollsweep` | `turnhold` | `sparse` | `slightbank` |
|---|---|---|---|---|---|
| **RENDER** | 149.12 → 141.26 | 281.68 → 229.87 | 281.45 → 245.97 | 94.94 → 61.59 | 220.20 → 197.51 |
| | −5.3% | **−18.4%** | −12.6% | **−35.1%** | −10.3% |
| `skyground` | 8.69 → 9.04 | 47.73 → 38.47 | 47.89 → 38.74 | 47.89 → 38.73 | 29.19 → 24.64 |
| | +4.1% | **−19.4%** | **−19.1%** | **−19.1%** | −15.6% |
| `scene` | 126.13 → 118.31 | 162.31 → 151.04 | 191.22 → 173.35 | 6.30 → 6.12 | 162.41 → 146.99 |
| | −6.2% | −6.9% | −9.3% | −2.9% | −9.5% |
| `blit` | 8.91 → 8.16 | 39.82 → 28.97 | 36.21 → 26.44 | 35.92 → 13.14 | 24.90 → 19.63 |
| | −8.5% | **−27.2%** | **−27.0%** | **−63.4%** | −21.2% |
| `panel` | 2.45 → 2.44 | 41.34 → 12.82 | 3.67 → 3.67 | 2.44 → 2.44 | 2.61 → 2.45 |
| | −0.1% | **−69.0%** | −0.1% | 0.0% | −6.3% |

Three of those rows are worth reading on their own.

- **`panel` on `rollsweep`, 41.34 → 12.82 ms.** That profile drives the bank
  two degrees a frame, so the attitude indicator's key changes EVERY frame
  and the instrument redraws every frame — the one profile the ADI's own
  modes are measured on. §88.9.2.5 is what moved it. On every other scene
  the panel is unchanged to a hundredth of a millisecond, which is the same
  fact from the other side: nothing was spent where nothing was redrawn.
- **`blit` on `sparse`, 35.92 → 13.14 ms.** `sparse` is a held 45-degree
  bank over an empty quarter of the map — ONE object near the horizon and
  nothing else — and its `scene` is 6.30 → 6.12, unchanged. So its −34% frame
  is almost entirely `skyground` + `blit`, which is §88.3.1.1's span pass:
  the rolled horizon's refill stopped claiming the whole row. **The emptiest
  scene had the most to give back**, because what it was paying for was the
  view and not the picture.
- **`skyground` is +4% on the LEVEL scenes** (`cruise`, `climb`). The band
  machinery costs a level frame a fraction of a millisecond it did not pay
  before, and buys 9 ms on a banked one. That trade is the design.

At object level the two that moved most are `faces` (−12% to −15% on the
banked scenes — §88.5.12's eye-cull and the vertex work) and the mark, which
moved **sideways**: `markrows` −20% to −30% against `edges` +28% on
`rollsweep` and `turnhold`. That is §88.3.2.3's stepped mark paying its own
cost inside `cs_seg` instead of inside `cs_markrows`, and it independently
reproduces §88.3.2.3.7 — a trade, not a win, and a net LOSS at 45 degrees
(`turnhold`: `markrows` −2.98, `edges` +4.37) against a net gain at 12
(`slightbank`: −1.41 and −1.46).

## Four controls, because a cross-build comparison has four ways to lie

**1. Reproducibility within an arm.** Each arm ran twice in independent
guests. Six of eight scenes repeat to **0.03 ms or better** (`now`/`cruise`
150.91 and 150.91; `now`/`sparse` 68.42 and 68.42). Two do not: `rollsweep`
(295.28 / 293.60 base, 239.00 / 242.07 now) and `descend` base (120.28 /
116.29). Those two carry a spread of a few ms and their deltas should be
read to the nearest few ms, not to the hundredth.

**2. THE SCENES DRIFT, because a faster build flies less far.** The world
steps on IRQ0 ticks and a frame that costs fewer cycles spans fewer ticks, so
over 40 frames the faster arm has covered less ground. It is visible:
`sparse` ends on heading 243 in the base arm and 238 in the current one,
`climb` at 36 m against 33. Two scenes (`turnhold`, `slightbank`) report an
identical flight to the printed precision; six differ slightly.

**3. So the sampling window was moved, as the control for it.** The same arm,
the same flight, the 40 frames taken 48 frames later (`--warm 6` → `12`):

| scene | base w6 / w12 | now w6 / w12 | worst within-arm shift | between-arm delta |
|---|---|---|---|---|
| `cruise` | 162.20 / 161.46 | 150.91 / 149.17 | 1.74 | −11.29 |
| `bank` | 157.06 / 151.21 | 146.10 / 137.05 | **9.06** | −10.96 |
| `rollsweep` | 294.44 / 292.68 | 240.54 / 238.46 | 2.07 | −53.91 |
| `turnhold` | 294.17 / 295.98 | 256.74 / 258.39 | 1.80 | −37.43 |
| `sparse` | 103.65 / 103.88 | 68.42 / 67.82 | 0.60 | −35.23 |
| `slightbank` | 232.91 / 233.70 | 209.69 / 209.34 | 0.79 | −23.22 |
| `climb` | 155.41 / 153.45 | 148.11 / 147.38 | 1.96 | −7.30 |
| `descend` | 118.28 / 116.24 | 109.60 / 104.01 | **5.59** | −8.68 |

**Six of eight clear their own sampling sensitivity by more than 3x**, so on
those the delta is the code and not where the 40 frames were taken. **`bank`
and `descend` do not** — both are profiles whose state is CHANGING through
the run (a released bank decaying, an altitude falling), so where you sample
genuinely changes the scene. Their sign and rough size hold in both windows
(`bank` reads −10.96 at w6 and −14.16 at w12; `descend` −8.68 and −12.23), so
read those two as *about −7% to −11%* rather than as a figure.

**4. The KERNEL contributes nothing measurable.** The two trees carry
different kernels (540 lines changed under `kernel/` over the span), but
Skies runs inside an fsx bracket and owns every pixel (§53.7), so the only
route from the kernel into the frame is interrupt cost — which lands in the
profile's `unaccounted` row, the frame loop's own time outside every bracket.
That row reads **0.07 / 0.07, 0.08 / 0.07, 0.06 / 0.05, 0.08 / 0.08 ms**
(base / now) on `cruise`, `turnhold`, `sparse` and `slightbank`. Below
0.03 ms of a 68–294 ms frame, on both arms, on every scene: nil.

## Size — and the package changed SHAPE, so read the right row

`SKIES.O88` is no longer one image. §20.12's parts landed mid-thread
(`ec7f9f0`, *the worlds become lazy parts, read into a bss overlay*), so the
file is now a 2,000-byte LOADER plus eleven parts: the program, the art, and
the nine worlds, each compressed and read on demand.

| | base | now | delta |
|---|---|---|---|
| the disk file `SKIES.O88` | 36,658 | 44,229 | **+7,571** |
| the PROGRAM's image (code + data) | 46,445 | 31,679 | **−14,766 (−31.8%)** |
| its bss | 13,599 | 20,097 | +6,498 |
| **its region claim (image + bss)** | **60,044** | **51,776** | **−8,268 (−13.8%)** |
| the loader's own resident cost | — | 2,000 + 90 | — |

**The row that matters to a 4.77 MHz machine is the claim, and it fell 13.8%
while the frame got 12.4% faster and the program gained features.** The file
on the floppy grew because nine worlds that used to be assembled into one
lz-compressed image now ship as ten separately-compressed parts plus a
loader — a worse ratio on the disk, bought deliberately, for a program that
reads one world and not nine.

## What the span carried, because it was not all optimisation

Of the 45 commits touching `apps/skies/`, roughly a third are FEATURES the
base did not have, and they are in the "now" arm's cost: the box impostor
banking with the world, the windshield, the runway centreline on a long final
and its stripes, the watchdog strip, F6's four ADI modes and the Fast mode, a
wing paying for its lift, the airbrake and its line, and the A5 turned to
face the city. **The frame got 12.4% faster carrying those**, which is the
honest way to read the headline: it is not a like-for-like renderer
comparison, it is the program a player runs.

Four commits in the span are REFUSALS rather than builds — the angular skip,
the narrow fill, cull-before-project, and both vertex-pipeline candidates —
and three are defect fixes found by instruments built on the way
(docs/FIELD-NOTES.md 40 and 41).

## What this report does NOT say

- **Nothing here is a field measurement.** MartyPC agrees with the 5150 to
  0–4% on 45 of 47 `gfxbench` rows, and that is the whole of the warrant. The
  ratios are safer than the absolute milliseconds.
- **It is Hercules only.** CGA and Mode X have different byte-per-pixel
  arithmetic and a different blit; §88.3.1's band and §88.4.6's row bodies are
  1bpp-shaped, so the CGA and Mode X deltas are unknown and may differ.
- **It is one view size** — MODERATE, the Hercules default, 400x112 in a
  640x200 box. `CSZ_FULL` is the default on CGA and Mode X and is not
  measured here.
- **The eight profiles are not the population.** They are the scenes the
  thread was steered by, which makes them the right table for "what did we
  do" and the wrong one for "what will a player see".

## How to take it again

```sh
git worktree add <dir> <base-commit> && (cd <dir> && make)
ln -s $PWD/build/martypc <dir>/build/martypc     # the emulator is shared
cp tests/skiesprof.py tools/os88marty.py <dir>/  # the instrument is shared
python3 tests/skiesprof.py --profile <p> --frames 40 --tier 2 --csv <f>
```

...once per arm per profile, twice each, and take the median of the CSV's
`total_ms` with the first row dropped. Then move `--warm` and check that the
delta survives it.
