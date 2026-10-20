# Apps size pass 2, set C — measured 2026-10-16

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file.

Set C of the second apps size pass (docs/plans/HANDOFF-APPS-SIZE-P2.md), the
games on the 360KB apps disk: **tracker, wireball, tank, cyclone, missile,
arkanoid, solitaire, tamegram**. The pass measures a package by what it
costs a DISK - the shipped, LZ4-packed file and the clusters it takes on
each 360KB disk it rides - and keeps image + bss from growing.

- **Tree**: cut from the pass branch `claude/apps-size-2-disk-reclaim-6rn5hh`
  at `c93b6f7c` (set C's claim), branch
  `claude/handoff-apps-size-p2-claim-run-duwn99`; "before" is `c93b6f7c`
  built in this container (`os88diskuse.py --save` at the cut point, before
  the first agent started), "after" is the merge of all eight agents'
  branches. Five agents' worktrees were cut from `a123653b`, an ancestor of
  `c93b6f7c` that differs from it only in documentation and
  `tools/os88diskuse.py`, so package bytes are the same either side; their
  own before/after figures are within a build number of these.
- **Box**: the 4-core cloud container this session ran in. Every byte and
  cluster figure is counted; the cycle figures are MartyPC's guest cycles.
- **Method**: handoff 4 - one agent per package in its own worktree, host-side
  checks only (no soak row by any route), one design-deciding cycle count
  allowed, and the union of the agents' named rows run ONCE on the merged tree.
  The session paused for a capacity window mid-pass; every agent stopped at a
  clean commit and resumed from it.

## The table

File and cluster figures are `tools/os88diskuse.py --diff` against the
cut-point snapshot, read off the built images; image + bss is the header
(`tools/os88pkgsize.py`).

| package | file before | after | bytes | clusters freed (360KB) | image + bss before | after | runtime claims |
|---|---:|---:|---:|---|---:|---:|---|
| wireball | 14,315 | 13,496 | **-819** | 0 (14 cl, 184 B short of 13) | 39,176 | 39,203 (**+27**) | none. The 1bpp splash is rebuilt from the colour art on the window's first MONOCHROME paint instead of shipped: ~195 ms full-height / ~97 ms compact (estimated from 8088 instruction timings, not measured), colour displays never pay it - **over handoff 4.3's 100 ms line and APPROVED by the owner**, with the +27 B of image + bss it brings (2026-10-16). Every court, rate-hook, frame-parser and main-loop routine disassembles identical at the same address parity |
| missile | 10,689 | 10,164 | **-525** | **1** on each of apps360, games360, small360, smallapps360 | 15,893 | 14,877 (-1,016) | none. ICBM and ABM tables are one object table and the trail loops walk it once: **-0.70% cycles over 400 frames** (MartyPC 5150 Hercules: 63,034,335 -> 62,595,337), the same end state `C3B7` and the same screen hash |
| cyclone | 11,410 | 11,089 | **-321** | **1** on each of apps360, games360 (it rides no small disk) | 16,159 | 15,724 (-435) | none. Seven of eight webs ship their symmetric half and a layout-time pass rebuilds the vertex list (host-checked vertex for vertex); every gameplay routine instruction-identical (routine disassembly diff) |
| arkanoid | 5,285 | 5,031 | **-254** | **1** on each of apps360, games360, small360, smallapps360 | 6,509 | 6,169 (-340) | none. Tuning tables as signed bytes widened at launch (~1 ms); single-caller inlines and dropped register saves on the frame path - instructions removed only, no cycle count taken |
| tank | 13,559 | 13,326 | **-233** | 0 (14 cl, 14 B over 13) | 22,593 | 22,381 (-212) | the 32 KB shadow claim unchanged. The entry proc is an overlay under `tk_spjunk` (a span set only ever written) and `tk_rowoff`; ridge, sine, vertices and GAME OVER ship as seeds expanded at launch (~20 ms, estimated). Tables checked byte-identical by running `tk_mktabs` out of the image under a host 8086 emulator; one pad byte restores the hot routines' code parity |
| tank (APP_SMALL) | 14,109 | 13,879 | -230 | 0 (14 cl) | 23,921 | 23,709 (-212) | the 18/17/16 KB ladder unchanged |
| tracker | 26,498 | 26,268 | **-230** | 0 (26 cl, ~670 B short of 25) | 42,962 | 42,603 (-359) | module blob, ring and Scope claim unchanged. Body tiles of the face and the PlayList editor grouped (`tools/trkface.py` emits and checks the new form; tile sets identical, fill order changed, 21 fewer `SET_COLOR`); replayer, mixers, speaker shaper and per-frame painters instruction-identical |
| solitaire | 4,259 | 4,127 | **-132** | 0 (5 cl, 31 B over 4) | 5,735 | 5,628 (-107) | none. Measured on MartyPC (package's own `tools/solperf.py`): every drop 104-328 cycles lower, every stock click 39-216 lower, New Game/Restart 1.1-2.2k lower net of tick alignment, on CGA, Hercules, VGA and CGA small. **One defect fixed** (below) |
| solitaire (APP_SMALL) | 4,067 | 3,938 | -129 | 0 (4 cl) | 5,532 | 5,433 (-99) | `t_appsmall` passes (small saves 3.5% against its 3% floor) |
| tamegram | 3,175 | 3,079 | **-96** | 0 (4 cl, 7 B over 3) | 3,937 | 3,818 (-119) | the 4 KB region unchanged. Short branches, HUD columns in pixels (ten `mul`s off a whole repaint), dead clamps; every core routine instruction-identical. The 7 bytes are left to the other session's LZB move, at the owner's word: the optional 4-byte `tg_onkey` cut was NOT taken because it costs ~450 cycles on an unmapped key |
| **total (shipped arms)** | **89,190** | **86,580** | **-2,610** | **12** (3 each on apps360, games360, small360, smallapps360) | **152,964** | **150,403** | **-2,561** of image + bss; no heap claim grew |

**The disks** (free clusters of 354, cut point -> now): `apps360.img` 1 -> **4**,
`games360.img` 80 -> **83**, `small360.img` 55 -> **58**, `smallapps360.img`
154 -> **157**. The system, media, office and network disks are untouched (no
set C package rides them). Solitaire, Tank, TameGram and Wireball also ride
the 720KB, 1.44MB and 1.2MB apps disks and take the same bytes off each.

**Four packages are within a few bytes of a cluster** and are the cheapest
room left in the set: TameGram 7 B (LZB is expected to cover it), Tank 14 B,
Solitaire 31 B, Wireball 184 B. Per handoff 4.2 no change was shaped to land
on a boundary.

## Hot paths

- **Measured**: Missile's one per-frame commit (-0.70%, same game and screen);
  Solitaire's hot-routine commits (every drop and click faster on four
  machines). Both were the design-deciding count handoff 4.3 rule 6 allows,
  taken once by the agent.
- **Shown instruction-identical** (listing or disassembly diff per routine):
  Tracker's replayer, mixers, shaper and painters; Wireball's court, rate
  hook, frame parser and main loop; Cyclone's gameplay; Tank's frame code
  outside the overlay; TameGram's core walk/paint/settle routines.
- **Argued from the code, not measured**: Arkanoid's frame path and the
  remaining TameGram, Cyclone and Tank hot-path edits, each of which only
  removes instructions or replaces one with a cheaper one.
- **Launch cost moved to entry**: Tank ~20 ms, Solitaire ~4-5 ms, Arkanoid
  ~1 ms (estimates); Wireball's ~195/97 ms is on the first monochrome paint,
  not at launch, and is the one figure over the 100 ms line (approved).

## User-visible changes

None but the defect below: every picture is pixel-identical by construction
or by check. Tracker's face paints the same tiles in a different order.

## Defects

- **Fixed - Solitaire `sol_colfan`** (since the first version): a `mul`
  overwrote DX before the face-down count was read from it, so the fit test
  counted face-down cards as zero height. A column overflowing by less than
  its face-down height was not tightened and its last card was cut off: on CGA
  6 face-down + 9 face-up cards lost 14 of the last card's 28 rows. It now
  fans at 10 rows a card with every rank showing. Shown by arithmetic from
  live CGA values; the agent's look-boot did not reach that state.
- **Fixed - Missile `mc_destroy`** claimed to preserve every register and
  changed CX; its only caller saved CX, so nothing visible changed.
- **Fixed - Cyclone** `cy_hs_submit`'s header comment (the row is returned
  in `[cy_hsidx]`, not AL).
- **Not fixed**: Cyclone's write-only `[cy_towave]` (4 B), left per SPEC.md
  67.25; TameGram's edge-rub (pass 1's open item, the owner's call).

## Risks the final pass was asked to watch

- Tank's overlay rests on `tk_spjunk` never being read and `tk_rowoff` being
  written whole by `tk_devrows` before any reader (`tank`, `tanksmall`).
- Solitaire's entry proc relies on the loader reloading SI/DI, and
  `sol_oncmd` on SPEC.md 12.2 letting a menu handler clobber them.
- Missile's object bss was reordered (`mcseg` forces the trail path the bench
  does not run).
- Wireball's rebuild uses `wb_ports` as scratch; in practice mode the HUD's
  change test can see the leftover bytes, which can only cause an extra HUD
  redraw (`wireballfront`, `wireballmono`).
- Tracker's tile tables changed form; nothing but `tools/trkface.py` parses
  them (`trkface`, `trkclick`).

## Coverage

The merged tree built clean (`make -j4`, fast tier 47/47; `make small
smallapps`; `make KERN_SMALL=1`), and ONE scoped run took the union of the
eight agents' named rows plus the shared rows handoff 4.4 lists - **93 rows**
(`os88test.py soak` with `--marty-jobs 4`, 1,189 s):

`trk*` (19: trkface, trkscrl, trkclick, trkspk, trkspkrate, trkspklevel,
trkspkcovox, trkload, trkscrub, trkspkref, trkspkdrop, trkspkturbo,
trkspkend, trkbigmod, trkcompact, trkrate, trktxsurf, trklcd, trackmove),
`wireball*` (5), cycweb, cycplay, cycpu, cycfire, cychs, gfxewalk, gfxlk,
tank, tankaim, tankspawn, tanksmall, ddsmall, mcperf, mcseg, dispmcfs,
curshape, dispcorner, shedrelist, win1bpp, arkpuwipe, arkpuwipesmall, tgplay,
tgui, dispapp, dispapps, dispmodex, dispsize, dispfsx, altenter, appdata,
regapp, regwork, appsmall, smallreq, movable, `lz*` but `lzdrv*` (16),
`wseq*` (8), modstr, `assoc*` (8), instassoc.

**91 passed, 2 failed: `tankaim` and `tanksmall`, both Tank's and both this
pass's**, and both fixed by the Tank agent and re-run green with `tank`,
`appsmall` and `smallreq` on the rebuilt tree (5/5):

- **`tanksmall`** - on the 128KB machine the small arm's entry proc returned
  CF=1 and the package refused ITSELF (`LD_EABORT`, no toast). Tank had
  switched to `OS88_REGION_MOVABLE_NOKEEP`, which leaves the slot's CF in
  the flags, and on kern_small `OSAPI_MEM_MOVABLE` answers `stc` because
  nothing moves there (SPEC.md 66.0); nothing later in `tk_entry` set CF
  again on that kernel. kern_big's slot succeeds, which is why `tank` stayed
  green. A/B'd against the base on the same machine with only `TANK.O88`
  swapped. Fix: the keeping form (+7 file B shipped, +6 small, image + bss
  unchanged - it sits in the entry overlay). **The trap is the SDK's, not
  Tank's**: any package whose entry proc ends on `_NOKEEP` without setting
  CF again refuses itself on kern_small and on nothing else, and no row
  that boots kern_big can see it. The other carriers (TameGram, Font
  Viewer, Audio, Calc, Redline) were checked: each sets the flags again
  after it (TameGram's is an explicit `clc`). Worth a line in the SDK note
  for `OS88_REGION_MOVABLE_NOKEEP`.
- **`tankaim`** parsed `tksin.inc` as a plain table, which the sine's new
  seed form broke. It now sums the second differences exactly as
  `tk_mktabs` does, the 256-entry table it builds is identical to the base
  file's, it also checks the quarter wave ends at 16,384, and it was broken
  on purpose (one difference changed: red, "ends at 16322") before being
  kept.

`make test-full` once afterwards: **54/54**.

Not covered: `lzdrv` and `lzdrv-nohint` (their image cannot be built, see
Process notes); `tankperf` is not a registered row. Missile's agent saw one
`mcperf`-harness run end on a different screen hash and its unchanged re-run
match the base - `mcperf` passed here, but its screen hash may not be fully
repeatable.

## Wins no set could take (shared includes, kernel, tools)

| win | where | bytes | carriers | found by |
|---|---|---|---|---|
| `os88spk` + `os88spkfx` are ~1,750 packed bytes of Tracker's file - the largest unique block in it outside the replayer | `apps/os88spk.inc`, `apps/os88spkfx.inc` | unpriced; any trim pays in every carrier | Tracker, Audio, Video | tracker |
| `os88ui_btn` ~423 packed B in Tracker; `os88ui_about_d` ~124 packed B (175 B image) in every About carrier, the largest shared routine in Tank's image | `apps/os88ui.inc` | unpriced | every About/button carrier | tracker, tank |
| one `je .out` in `os88ui_about_d` assembles long (5 B); restructured to reach a short branch | `apps/os88ui.inc` | ~3 B | every About carrier | tamegram. NB HANDOFF-APPS-SHARED 0.1 records "`os88ui_about_d`'s exit is short" as BUILT, so this is a SECOND long branch or a regression - re-check before taking |
| ~52 five-byte far `call OSAPI_*` sites; a near thunk would cost hot-path cycles, so only a cold-path form could pay | `apps/os88api.inc` | unpriced | every package | solitaire |
| `od_*` (`os88data.inc`) is ~300 packed B of Cyclone; `gfxe_*` (`os88gfx.inc`) not examined | shared | unexamined | every carrier | cyclone |
| content-origin helpers (B.3) and a grid hit-test helper (B.7) still apply | shared | pass 1's estimates | pass 1's carriers | solitaire |
| LZB packs ~10 points tighter than LZ4 | tools (`PKGZ`) | ~10% of every file | every package | tracker; the owner's decision, already in hand for TameGram in another session |

**Stale, not passed on**: Tracker's agent listed "`os88spkfx.inc`'s
2,816-byte level family in the package segment (pass 1's B.2)". It was
BUILT in shared round 1 (HANDOFF-APPS-SHARED 0.1, SPEC.md 34.11.9): the family
lives in the speaker ring's claim and is generated, read `es xlatb`.

## Not taken, and why

- Tracker: the period table's octaves built at init (-17 file B, +27 B RAM -
  rule 3); a batch of `trkspk.inc` golfs (image -17, file +3). The finding
  generalises: de-duplicating code buys almost nothing packed, because LZ4
  already matches it; only unique bytes pay, and most of what is left unique
  in Tracker is on hot paths.
- Missile: `mc_line`'s Mode X Bresenham (~60 B), hot and not benched.
- Arkanoid: `OSAPI_ICON_DRAW` for the capsule fallback (draws nothing when a
  clip edge cuts the shape); one helper for the three origin wrappers (more
  code than it saves).
- Tank: factoring the pop ladders (grows the packed file); `tk_lgpts` (too
  little).
- TameGram: `tg_draw_active` before `tg_frame` (image -1, file +2).

## Process notes

- `make KERN_SMALL=1` builds INTO `build/` and replaces the shipped
  `os8088-360.img`; an `os88diskuse --diff` run straight after it reports the
  small kernel as a 53-cluster win on the system disk. A plain `make` puts it
  back. Take the diff after the last default build.
- `build/lzdrv360.img` cannot be built at this cut point (`packages need 363
  clusters; disk holds 354`): the system disk with the compressed driver set
  had already outgrown 360KB before set C (no set C package rides it), so
  `lzdrv` and `lzdrv-nohint` were excluded from the run and are not covered
  here. It is set A's disk to relieve.
