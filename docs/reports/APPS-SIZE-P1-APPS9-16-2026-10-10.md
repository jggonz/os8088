# Apps size pass 1, apps 9-16 — measured 2026-10-10

The first dedicated size pass on the APPLICATION packages. Many kernel passes
and one system-side pass (docs/reports/SYSTEM-SIDE-SIZE-P1-2026-10-04.md)
preceded it; apps had only ever been trimmed one at a time. The pass was
split across three sessions by the raw `ls apps/` order of the directories;
this report is the second eight: `cc`, `chart`, `cword`, `cyclone`, `dos`,
`dotdel`, `drmario`, `excitebike`. `cc` is the C SDK and not an app — what it
saves, it saves in every C package.

- **Tree**: branch `apps-size-2`, cut from `elendilon` at `0371d27`. "Before"
  is `0371d27` built in the main checkout; "after" is the branch with all
  eight items merged (`346b9051` and the commits after it).
- **Box**: the 4-core cloud container this session ran in. Every byte figure
  is counted, not timed. The timings quoted are MartyPC's 4.77 MHz 8088.
- **Method**: one agent per item, each in its own git worktree, brief: cut
  memory (image + bss + run-time claims) and disk, no feature removed, no hot
  path slower, no edit to shared includes or the kernel (other sessions were
  editing those in parallel) — wins there are LISTED below instead. The
  session hit a capacity limit and every agent was given a 30-minute wrap-up,
  so several stopped with verified partial results and say what is left.

## The table

`RAM` is image + bss, the one heap claim the loader takes per instance
(SPEC.md 20.1); `claim` is that in whole KB. `file` is the bytes on the
floppy (LZ4 unless noted).

| item | RAM before | RAM after | % | claim KB | file |
|---|---:|---:|---:|---|---|
| CHART.O88 | 13,852 | 12,081 | -12.8% | **14 → 12** | 10,049 → 9,713 |
| CWORD.O88 (with the SDK pass) | 61,047 | 59,350 | -2.8% | **60 → 58** | 36,514 → **20,351** |
| CWORD.OVL (claim at run time) | 18,565 | 18,565 | 0 | 19 → 19 | 18,565 → **8,851** |
| CYCLONE.O88 | 17,981 | 16,153 | -10.2% | **18 → 16** | 11,921 → 11,400 |
| DOS.O88 (plain, apps disks) | 50,116 | 49,596 | -1.0% | 49 → 49 | 31,992 → 31,552 |
| DOS.O88 (parted, system disk) | — | — | — | — | 48,718 → 48,203 |
| kern_dos image (DOS.O88 part 2) | 34,731 | 33,659 | -3.1% | **`KD_IMG_KB` 35 → 33** | — |
| DOTDEL.O88 | 30,892 | 25,417 | -17.7% | **31 → 25** | 16,557 → 16,313 |
| DRMARCO.O88 | 56,228 | 37,720 | **-32.9%** | **55 → 37** | 25,478 → 21,322 |
| EXCBIKE.O88 | 53,510 | 40,940 | **-23.5%** | **53 → 40** | 24,815 → 24,300 |
| Excitebike's art + README | — | — | — | — | 64,554 raw → 28,855 |
| **seven packages** | **283,626** | **241,257** | **-14.9%** | **280 → 237** | **157,326 → 134,951** |

The seven-package row is the seven `.O88`s above (plain DOS, not the parted
one); it excludes the overlay and data files, whose disk savings are on top.

### What is not in the header

- **Chart** held a fixed 32KB staging claim for the instance's life; a file
  read now takes a claim sized to the file (SALES.SLK: 2KB) and frees it in
  the same callback, and Export writes the canvas in place. Heap held while
  open: **51KB → 19KB**.
- **kern_dos** reserves `KD_IMG_KB`, so a DOS program under it gets **2KB more
  conventional memory** (it also gives back SPEC.md 96.11.7's borrowed 1KB).
- **Dot Delirium's** board-picture claim now shrinks back when fullscreen
  ends (it only ever grew): 11KB → 4KB on the 128KB CGA machine, measured;
  19KB → 8KB on Hercules by arithmetic.
- **Excitebike's** race claims: sprites VGA 44 → 41KB, CGA/Hercules 28 → 21KB
  (`OSAPI_MEM_REGROW` once the poses are expanded); the art claim is freed
  after the back end is built instead of held all race; the test harness's
  4,352 bytes moved out of bss into a claim made only when a harness asks.
  The whole game on the 360KB disk: 94 → 56 clusters.

### The C SDK, per consumer

`cc8086.py` now emits a `%define CC_U_<name>` for every extern the C uses and
`os88thunk.asm` assembles only those thunks (every package carried all ~130);
the thunks and trampolines were tightened (SPEC.md 73.9.1); and `CC_PACKAGE`
now packs the image like every other package (SPEC.md 20.13.5).

| package | RAM before | RAM after | file before | file after |
|---|---:|---:|---:|---:|
| ccsmoke | 3,850 | 1,532 | 3,756 | 1,470 |
| chello | 4,702 | 2,528 | 4,542 | 2,400 |
| covl | 4,452 | 2,140 | 4,270 | 1,988 |
| cword | 61,047 | 59,350 | 36,514 | 20,351 |
| weave | 60,910 | 59,106 | 51,700 | 30,041 |
| loom | 60,774 | 58,958 | 54,562 | 52,760 |
| paccman | 47,402 | 45,356 | 42,172 | 19,427 |
| c64 | 53,450 | 51,600 | 57,810 | 56,274 |
| runcpm | 51,461 | 49,787 | 39,772 | 21,523 |
| apple2 | 51,838 | 50,106 | 52,736 | 50,688 |

chello, covl and loom have open-coded Makefile rules and are not packed yet;
c64 and apple2 carry parts and stay plain. `LOOM.WPV` 16,260 → 15,908.

### The resident kernel

No kernel file was edited; `kern_big`, `kern_small` and `kern_emu` are
unchanged. (`kerndos/kdlayout.inc` moved, but kern_dos is a part of DOS.O88,
not a resident kernel.)

## What each item did

- **CHART**: the sized transient read claim and in-place export above;
  series one sorted in place, the rows and both series' doubles moved into the
  transient claim (bss -1,301); shared dialog/read paths (image -470).
  SPEC.md 82.14.
- **CWORD**: ships the package and the overlay packed (the overlay as a `'CZ'`
  file that `cc_ovneed`'s transparent read expands); `apps/cword/cwpeep.py`, a
  liveness-based peephole pass over cc8086's output worth **-8,589 bytes of
  code** (image 36,514 → 31,974, overlay claim 19 → 15KB), is committed **OFF**
  (`CWPEEP ?= --off`; `make cword CWPEEP=` turns it on) — its first combined
  build miscompiled (two rewrites on one branch in one sweep), the fix passed a
  short typing session, and the full differential session did not finish.
- **CYCLONE**: the high-score buffer's 511 bytes of alignment slack reclaimed
  (the table sits on the boundary, the rest of bss fills the gap in front of
  it, `CY_PAD`/`CY_FIT`, assembly-time asserted; SPEC.md 77.31.1); 10-byte
  object blocks; one shared text line; mover loops step DI instead of two MULs
  per mover (faster); one web-segment layer; stack chain 80 → 70.
- **DOS**: window-only routines gated out of the INT 21h core kern_dos also
  carries (-596 core); INT 21h dispatch is a word table instead of 48
  compare/branch pairs (-164; faster for everything but 4Ch/00h/30h/01h/07h);
  near doors for expanded error branches; a hook table. `CORE_MAX` 16,384 →
  15,312. SPEC.md 96.7.3, 96.44.15.
- **DOTDEL**: layouts packed at assembly time by a `DDROW` macro (mirror,
  2 bits a tile, shared rows once; 2,604 → 284, with assembly-time refusals of
  a bad layout); the route-home walk's queue is a 64-entry ring and its map two
  tiles a byte (-2,042 bss, faster); drawing scratch overlaid; the board claim
  given back. SPEC.md 93.2, 93.8.5.
- **DRMARCO**: playfield screens coded against the row above (-9,041),
  animation patches without per-record headers (-3,660), tiles store one of
  each identical VGA plane pair and write both at once (-3,128, faster), music
  stores a duration only when it changes (-1,773). VGA move render 4.54 →
  2.09 ms, worst animation frame 27.5 → 19.8 ms; the one slower thing is the
  CGA background decode at mode entry, 93 → 100 ms, paid back by the font
  build in the same entry (65 → 13 ms). SPEC.md 100.
- **EXCITEBIKE**: the claims above; course streams recoded (6,002 → 2,942
  bytes, `EXBTRACK.DAT` unchanged); splash rows, loader scratch and the loading
  glyph share the CGA box buffer; the 2bpp loading glyphs derived from the
  1bpp ones; the six art files and README LZ-wrapped. SPEC.md 102.9.

## Defects found

- **CHART, fixed** (`a147ff9e`): Export copied the Save dialog's name over the
  charted file's name, so a later Data > Column re-read `CHART.BMP` as SYLK
  ("No data in that column.", empty canvas, BMP's name as the title).
  Reproduces on `0371d27`.
- **CYCLONE, fixed in the refactor**: `cy_web_spoke` incremented `[cy_wn]` a
  second time, so every web repair stepped three walks with nothing to draw.
  No visible effect.
- **Found, not fixed**: `cy_pn_dismiss` says it preserves everything and does
  not; `apps/dos/doscents.inc` names a `tests/unit/t_doscore.py` that does not
  exist; `kerndos/kdmouse.inc`'s comment still says `KD_IMG_KB` 35;
  `dos_k_copy` is a door nothing calls (kept: the slot numbers are an ABI);
  `drmarioaudio`'s AdLib arm fails at `sbl_up` **on `0371d27` as on the
  branch**; DrMarco's README timing table is stale.
- **Harness trap**: the dotdel rows read package offsets from
  `build/dotdel.bin`, and a stale one reports `content 0x0`, which reads as a
  broken package.

## Missing kernel or library components

All three sets' lists are compiled, de-duplicated and ranked in
docs/plans/HANDOFF-APPS-SHARED.md; this is set 2's own.

Collected for separate selection. "Resident" means a kernel byte.

| component | where | buys | cost |
|---|---|---|---|
| **cc8086 peephole** — the `setcc`/`test`/`jcc` collapse, or the whole of `apps/cword/cwpeep.py` once verified, plus a size-aware jump retargeter | `tools/cc8086.py` | ~9 bytes a site: loom 375 sites (~3.4KB), weave 313 (~2.8KB), cword 220 (~2KB), apple2/c64/runcpm/paccman 1.3-1.7KB each; cwpeep measured -8,589 in cword (~14% of compiled code) | none resident; needs a differential verifier first |
| **Pack the C `.OVL` files** as `'CZ'` (cword now does) | `apps/cc/Makefile.inc` | LOOM.OVL 43.7KB → ~22KB on disk, WEAVE.OVL 21KB, etc. | none; risk: a host copy that drops the directory hint refuses with "Old X.OVL" |
| **Pack loom, chello, covl** (open-coded rules) | `Makefile` | LOOM.O88 52,760 → 28,534 on disk | none |
| **512-aligned scratch from the OS, or FILE_READ/WRITE bouncing sub-sector buffers on driver volumes too** (only BIOS volumes bounce, SPEC.md 18.91.4) | kernel | every small save file drops its alignment slack | resident, unpriced |
| **`OS88_BSS_ALIGNED`** — Cyclone's gap-filling aligned-bss layout as a macro | `apps/os88api.inc` | up to 511 bytes in any package with an int-13h buffer in its own segment (FTPD's `fd_stage`) | none |
| **A shared save-all/restore-all ladder** for apps | `apps/os88*.inc` | ~250-400 bytes in DOS's host code, ~10 bytes a callback site in Dot Delirium; every large package | none |
| **`os88data.inc` read-into-place helper** ("read into the live table, else restore defaults") | `apps/os88data.inc` | the staging buffer every includer keeps (Cyclone did it by hand) | none |
| **A transparent `OSAPI_FILE_READ` into a buffer not at offset 0** | kernel | Excitebike claims 1KB only to read `EXBTRACK.DAT` and copy it | resident, small |
| **Document the zero-capacity `OSAPI_FILE_READ` probe** as the way to size a buffer, and shrink-to-fit via `OSAPI_MEM_REGROW` for surface-sized claims | SDK docs | Chart and Dot Delirium both used these; other apps hold fixed or grown-only claims | none |

## Shared-include wins seen, not taken

- **`apps/os88fp.inc` has no opt-out gates**: ~960 bytes of it is unreachable
  from Chart and assembled anyway — `fp_ftoa` 454, the `10e5` divides 109,
  `fp_sqrt` 140, `fp_i2a`/`fp_i2b` 118, `fp_floor` 39, sub helpers ~80.
  `FP_NO_*` gates would take ~900 bytes off Chart and likely Sheet.
- **`apps/os88chart.inc`**: `ch_bmp_write` (92) is now unused by Chart — a
  `CH_NO_BMPWRITE` gate, or have it flip in place as `ct_flip` does so no
  caller needs a 19KB scratch segment.
- **`apps/os88con.inc`**: `con_glyf` is a 2,048-byte bss copy of the CP437
  face built at launch from `os88cp437.inc` (1,280 of image); reading the
  packed form, or the ROM font where the glyphs agree, is ~2KB of RAM in DOS
  and Telnet. `con_band` (640) might share the carrier's scratch.
- **`apps/os88data.inc`**: `od_find` is 30 bytes of zeros in the image; it can
  be bss. The include is ~370 bytes of every includer.
- **`apps/loom/lmpvmod.asm`** includes `os88thunk.asm` ungated; included after
  the C with `CC_THUNK_GATED`, `LOOM.WPV` would carry only its own thunks.
- `os88ui_about_d` (178) has no gate; it is needed for the About card wherever
  one is shown, so not a win.

## Left on the table (built or designed, not shipped)

- **Excitebike's delta-coded splash**: VGA splash 12,457 → 6,553, ~6KB off the
  claim held while the window is open. Reverted: a help-page reveal step
  measured 68 ms against `excitebikefront`'s 55 ms gate, cause not found.
- **DrMarco's front art**: row-above coding would halve the VGA file but slow
  the window painter's random row access; a same-row previous-plane variant is
  ~22.8KB from 26,947 (~4KB of claim) and changes the DMF1 contract.
- **Dot Delirium**: the high-score table read in place (-153 image, -42 bss)
  and a sprite-cutter rewrite (est. -300 to -400) — both drafted, neither
  verified in time.
- **Cyclone**: a dictionary coder for ~980 bytes of panel text (~120 net).
- **DOS**: the host's push/pop ladders (~250-400), an INT 33h table (~30).

## Decision for the owner

**DrMarco and kern_small.** It was left off the small disks because 56,333
bytes did not fit the 53,760-byte arena. It is 37,720 now, plus a 6KB CGA
front-art claim, against ~36KB recorded as fitting there. The omit reason in
`tests/unit/t_smallreq.py` was updated; the omit list was not. Measure it on
`os8088_5150_cga_128k` before re-admitting it.

## Coverage

Each item ran its own rows in its worktree; the merged tree then ran `make`
(fast tier 61/61) and `make small`, and these on MartyPC/QEMU:

- **CHART**: no row opens Chart (no association). Its agent's own MartyPC
  differential (ten files, every gallery type, Data > Column) matched base
  61/63 steps, the two being the Export fix; the BMP is byte-identical. CGA
  only; the no-memory toast forced by breakpoint.
- **CWORD**: `ovlhigh` (off the packed disk, re-run on the merged tree after
  the SDK reconciliation) ok; a typing session matched the plain build.
- **CYCLONE**: `cycplay` (again on the merged tree), `cycweb`, `cycpu`, `cycfire`, `gfxewalk`, `gfxlk` ok;
  web geometry dumped identical to base on Hercules and VGA. **Not verified:
  the high-score load/save path**, which the pass changed (the format did
  not).
- **DOS**: 32 rows ok (dosregs … dosdir, kdos, kerndos, kdpart, kdreturn …);
  ~45 dos/kd rows not run (network, dosshell, dosxms…); knob arms assemble.
- **DOTDEL**: `ddmaze`, `ddcorner`, `dotdelwin`, `dotdelmd` ok in the
  worktree; on the merged tree `dotdelpen` (which had failed one leg under a
  nine-emulator load in the worktree), `ddsmall` (the 128KB floor machine,
  with the new claim-return check) and `dotdel` — all ok.
- **DRMARCO**: `drmarcoart`, `drmariofront` ok; `drmario` ok in the worktree
  and again on the merged tree; `drmarioaudio` fails as on base.
- **EXCITEBIKE**: in the worktree `front`, `lowmem`, `smallclaim`, `perf`,
  `perfherc`, `audio`, `assets`, `selfcheck`, `sim`, `geomhost`, `videocga`,
  `custom`, `boot720`; on the merged tree `ref`, `flow`, `ai`, `videoherc`,
  `lap`, `selfb`, `g1`, `boot360`, `load`, `video` — **10/10 ok**. Not run:
  `audiofm`, `audiocap`, `audiolap`, `boot1440`, the remaining `*herc` rows.
- **C SDK**: `lmpack`, `paccman`, `ovlhigh`, eleven `weave*` rows, `c64part`,
  `apple2part`, `cmemmove` ok; COVL, CCSMOKE and CHELLO driven by hand. Not
  run: `weavepack`, `ctoolchain`, RunCPM's emulator rows.

Speed claims marked as instruction arithmetic in the sections above were not
timed; the DrMarco and Excitebike frame figures were.
