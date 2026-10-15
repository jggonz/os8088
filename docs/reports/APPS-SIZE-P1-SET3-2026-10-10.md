# Apps size pass 1, set 3 — measured 2026-10-10

The first dedicated size pass over the APPLICATIONS under `apps/`. Every
earlier pass was resident kernel or the system side
(docs/reports/SYSTEM-SIDE-SIZE-P1-2026-10-04.md); packages had only been
shrunk one at a time. The pass is split across three sessions by position in
`ls apps/`: this is SET 3, positions 17-24 — `fontview`, `fptest`, `fractal`,
`frotz`, `ftpd`, `gorillas`, `hello`, `imgtest`.

- **Tree**: branch `apps-size-3`, cut from `elendilon` at `0371d27d`.
  `elendilon` moved by four FTPD commits during the pass (`6fd29392`, the
  32 KB STOR stage, SPEC.md 77.50.3); that was merged in and FTPD's "before"
  is that commit's.
- **Box**: the 4-core cloud container this session ran in. Every number below
  is counted, not timed, unless it says guest seconds.
- **Method**: one agent per app, each in its own git worktree. Brief: reduce
  image + bss + runtime heap claims and the shipped file; no feature removed;
  no hot path slower; edit only the app's own directory (plus the SPEC.md
  section and tests that pin its numbers). Wins that need a shared include,
  the kernel, a driver, a tool or the Makefile were NOT taken; they are the
  lists below, for a separate round.
- **A setup trap worth writing down**: six of the eight worktrees were created
  from `origin/main` (`95f7e971`, 247 commits behind) instead of the branch
  the session was on, where `make` fails in `apps/pixel/pxfull.asm`. Each was
  moved onto `apps-size-3` before it measured anything it reports; every
  "before" below is `0371d27d`'s (FTPD's `6fd29392`'s).

## The table

`RAM` is image + bss, the region claim; `claim` is that in whole KB. `file` is
`build/<app>.o88` as built — LZ4 `'CZ'` where the target packs it; the FPTEST,
IMGTEST, FROTZ and HELLO targets do not (HELLO because packing cannot pay at
its size). `heap` is what the app claims at runtime on top of its region.

| app | image | bss | RAM | claim KB | file | heap |
|---|---|---|---|---|---|---|
| FONTVIEW | 4,857 → 4,509 | 1,984 → 2,030 | 6,841 → 6,539 (-4.4%) | 7 → 7 | 3,736 → 3,615 | face 8 KB, peak during a switch 16 → **8** KB; 12 KB cache unchanged |
| FPTEST | 7,164 → 5,813 | 219 → 169 | 7,383 → 5,982 (-19.0%) | **8 → 6** | 7,164 → 5,813 | none |
| FRACTAL | 4,061 → 3,050 | 425 → 464 | 4,486 → 3,514 (-21.7%) | **5 → 4** | 3,365 → 2,827 | 4-32 KB cache unchanged |
| FROTZ | 25,000 → 22,578 | 2,976 → 1,768 | 27,976 → 24,346 (-13.0%) | **28 → 24** | 25,000 → 22,578 | save/undo dyn+18 → dyn+17 KB each; **24 KB scrollback leak per re-opened story fixed** |
| FTPD | 16,378 → 14,784 | 12,294 → 11,840 | 28,672 → 26,624 (-7.1%) | **28 → 26** | 13,172 → 12,304 | 32 KB STOR stage unchanged |
| GORILLAS | 28,130 → 26,770 | 29,565 → 29,563 | 57,695 → 56,333 (-2.4%) | **57 → 56** | 16,154 → 15,215 | none |
| HELLO | 661 → 640 | 2 → 2 | 663 → 642 (-3.2%) | 1 → 1 | 661 → 640 | none |
| IMGTEST | 3,070 → 2,851 | 1,397 → 1,358 | 4,467 → 4,209 (-5.8%) | 5 → 5 | 3,070 → 2,851 | 128 → **65** KB |
| **total** | **89,321 → 80,995** (-9.3%) | **48,862 → 47,194** | **138,183 → 128,189** (-7.2%) | **139 → 129** | **72,322 → 65,843** (-9.0%) | IMGTEST -63 KB; FONTVIEW peak -8 KB; FROTZ -2 KB + a 24 KB leak |

Five of eight crossed at least one whole-KB claim (FPTEST and FROTZ by two
and four). FRACTAL (-21.7%) and FPTEST (-19.0%) are the only ones near the
30% the previous pass aimed at, and the reason the rest stop short is the
same one that pass found, one level out: **what is left is shared** — a
library include (`os88type.inc` is 65% of Font Viewer's image, `os88img.inc`
63% of IMGTEST's, `os88fp.inc` ~70% of FPTEST's) or a GENERATED table whose
generator is a gate (`grart.inc`, 11,169 bytes, is 42% of Gorillas). Those are
lists (b) and (c).

## Hot paths

None got slower; two got measurably faster.

- **FRACTAL**: whole frames on MartyPC (guest seconds) default 57.4 → 56.3,
  Fire 55.9 → 53.0, Dendrite 57.3 → 54.2, Burning Ship 173.7 → 171.6; the
  per-pixel tail after `frac_iter` ~225 → ~155 cycles, the emit scan ~105 →
  ~34 a pixel (run-end sentinel). `frac_iter`, `fr_q12`, `fr_inset` are byte
  for byte unchanged. 25 frame ends pixel-identical on VGA, four on CGA.
- **FROTZ**: the two PC guards on every Z-machine instruction fall through
  short (~24 clocks an instruction), a 2OP range check that could never fire
  is gone (~20 a 2OP), `zx_getvar` ~12-20 clocks a variable; 91 `call/ret`
  pairs are tail jumps. Deepest worker chain 240 → 236 bytes.
- **FTPD**: the transfer path lost work (no tick read per SEND/RECV in the
  shipping build, the dead second-buffer arm gone from the RECV loop); per-pass
  driver calls are still direct. No KB/s figure was taken.
- **FONTVIEW**: the per-keystroke sample repaint runs fewer instructions a row.
- **GORILLAS**: ~30 conditional jumps NASM had relaxed to the 5-byte
  jump-around form (12 cycles slower not-taken) are short again; blast discs
  draw a mirrored row PAIR per width calculation (~161 cycles a pair against
  210). MartyPC: intro frame VGA 71.36 → 71.24 ms, CGA 21.54 → 21.40;
  `90<Tab>150` VGA 29.23 → 29.00, Hercules 29.00 → 28.77; city build 278.4 →
  276.8. The fixed-seed scene hash is identical on all three adapters.

## Defects found

Fixed here:

- **FROTZ About** registered the string `"Frotz"` as the About handler, so
  choosing it executed five bytes of text. It now shows the card.
- **FROTZ** leaked the previous story's 24 KB scrollback on every re-open.
- **FROTZ `@verify`** read its branch byte through an ES saved across a
  possible compaction.
- **FTPD**: the `FTPDBG` routines, strings and counters shipped although
  SPEC.md 77.43 said they were gated.
- **IMGTEST** discarded `WM_CREATE`'s CF, hiding a full window table from the
  loader.
- **HELLO**: a menu pick with the About card up repainted the content twice.
- **FONTVIEW**: a failed face open showed the PREVIOUS face's glyphs under the
  new name; it shows the built-in face (SPEC.md 90.1). `tests/fontview.py`'s
  launch check could not detect a broken name match; it now can.
- **`tests/gorillasmusic.py`** failed on the base: it assembled `SOUND.DRV`
  for its symbols without the `-DPICOMEM` the Makefile builds with, so every
  driver symbol read 341 bytes out of place. With the define its assembly is
  byte-identical to `build/sound.bin`.
- **FPTEST**: SPEC.md 84.4 said a broken FPTEST "breaks the build"; `all` does
  not build it.

Found, NOT fixed:

- **FTPD**: the Sockets menu item (UI task) composes in `fd_outb2`, which the
  worker uses for replies — a click during a reply can garble it.
- **FROTZ**: the transcript buffer is claimed only when the undo claim FAILS,
  so Story > Transcript does nothing on a normal machine; v6 text is never
  drawn (`zw6_setup` has never been called); the window title follows the
  SAVE file's name after Save/Restore (zero-byte fix: point it at
  `zi_story`); `ZI_DYNMAX` assumes a 4 KB stack where it is 16 KB, so a big
  story with a deep stack can overflow the 16-bit save builder; every
  `zm_setup` refusal says "Not a Z-machine story file."; the scrollback claim
  is never declared movable. **Decision wanted**: the gated v6 text path is
  ~400-600 bytes that cannot run — delete it, or wire it up for ~+400.
- **SPEC.md 12.2 vs `kernel/ui.inc`**: the spec lets a click handler clobber
  SI, but `ui.inc` stores SI into `[ui_armw]` right after the handler returns.
  Either save SI in the dispatch (~2 resident bytes) or tighten the spec.
- **IMGTEST has no suite row** — no emulator row boots it, so the decoders
  have no soak gate. FPTEST and FROTZ have none either (FROTZ's gates are
  `make zcheck`/`zgfx`, which need stories this container could not fetch).
- **`tests/trackmove.py`** (~line 267) re-assembles `SOUND.DRV` the same way
  and reads `sbl_poolseg` from it — very likely the same defect; outside this
  set, not checked.
- **`tools/incsize.py`** writes `/tmp/attrib.lst` and `/tmp/attrib.bin` at
  fixed paths, so two parallel users clobber each other.
- `tests/editmove.py --app fractal` reads "repaint identical DIFFERENT" on the
  base as on the branch; not a registered variant, not diagnosed.

## Lists

### (a) Kernel

| win | resident cost | buys |
|---|---|---|
| A worker-safe claim, or a UI-task request type, so FTPD can claim its 32 KB STOR stage per STOR instead of for the window's life (SPEC.md 20.6 rule 7) | small | 32 KB of heap whenever FTPD is open and idle |
| `OSAPI_FMT_DEC` decimal formatter (Fractal and Calc carry the same loop; many others their own) | ~30 + an 8-byte cell | ~25-30 a package |
| Text at content-relative coordinates (window + x/y) | small | ~8 in Fractal; uncounted elsewhere; Font Viewer wrote its own `fv_abs` |
| Save SI in the click dispatch (above) | ~2 | fixes a latent defect |

### (b) Shared includes

- **`apps/os88type.inc`** (Font Viewer, Word, Scribe, CWORD, facetest):
  - size the face claim from the FILE — every face claims 8 KB and the shipped
    faces unpack to 1,118-1,688 bytes: **~6 KB of heap per open face**, up to
    three in Word/CWORD. Keep the 512-byte alignment.
  - size the 12 KB pre-shift cache from the face height (9 KB for 12-row
    faces, 6 KB for 8): 1-6 KB of heap per includer.
  - `%ifndef TY_STRIDE`: Font Viewer composes 480 px against a 720 px band, 480
    bss bytes; each includer could set its own widest line.
  - an opt-out for `ty_hit`/`ty_width`/`ty_widthn`/`ty_put`/`ty_advof`/
    `ty_famn`/`ty_getasc`/`ty_getlead`: 183 in Font Viewer. With TY_STRIDE it
    reaches a 6 KB claim.
  - an exact `TY_BSS_SIZE` with an assertion instead of `+48` slack.
- **`apps/os88img.inc`** (IMGTEST, SCRIBE's overlay): 1,800 of IMGTEST's
  2,851. Caller-saves for internal helpers ~70-90; `img_palmap` stepping DI by
  3 and looping the channels ~70-80; `img_load`'s redundant zeroing -15; byte
  stores for `IMG_ERR` -14; one shared byte-copy helper ~-10; `img_pix`'s
  `jc .out` sits at exactly +127 and flips short/near by layout (-3 where
  near). ~150-250 a copy.
- **`apps/os88fp.inc`** (SHEET, CHART, FPTEST): `fp_div32_10e5`,
  `fp_udiv32_10e5`, `fp_umod32_10e5`, `fp_tv_to_b` are called by nobody — 118
  bytes a copy, ~354 across three. `fp_i2a` is SHEET's alone (59). And the gate
  has no case for `fp_i2a`, `fp_a2i` or `fp_cmpab`/`fpx_cmpab`, which SHEET and
  CHART call.
- **`apps/os88ui.inc`**: the About card's `os88ui_abx/aby/abw/abh` are `dw 0`
  in the image — 8 file bytes in ~25 packages; Frotz's scroll bar is ~600
  bytes; check that `OS88UI_NOGRADIO` really removes all ~145 bytes of radio
  (FTPD sets it and still carries ring/dot code, unverified).
- **`apps/os88api.inc`**: `OS88_REGION_MOVABLE` emits a private `ret` and a
  `jmp short` over it — a form taking an existing `ret` saves 3 bytes in every
  package (~60+). Four agents here did it by hand. A shared save-all /
  restore-all ladder (the previous pass's list (b) again): Frotz, FTPD,
  Font Viewer and Fractal each wrote their own (`zf_popN`, FTPD's ladder,
  `fv_pop4/6`, `fr_pop7`).
- **`apps/os88ui.inc` `os88ui_about_d`**: its `jc .out` assembles near (147
  bytes away); moving the exit saves ~3 bytes in each of 29 packages.
- **`apps/os88data.inc`**: `od_find` carries a 24-byte inline find buffer; a
  caller-supplied one saves 24 in every includer.

### (c) Tools and Makefile

- `$(PKGZARG)` is missing from the `.o88` rules of **FROTZ** (22,578 → ~18,700
  on disk), **FPTEST** (5,813 → 5,012) and **IMGTEST** (2,851 → 2,313). FROTZ
  is the one that ships on a floppy.
- `tools/os88imgcase.py`: drop the derivable stride, byte-size picno/err, names
  inline — ~100-160 bytes of IMGTEST, generator and harness together.
- `tools/incsize.py`'s fixed `/tmp` paths (above).
- **Gorillas' generators — the largest single item in the set, ~9.5 KB of RAM
  (17% of the app), no other includer, no resident cost.** The tests run them
  in `--check` mode against the committed output, so the format is theirs:
  - `tools/gorillas_art.py` / `grart.inc` (11,169 bytes): the eight colour
    conversion tables (4,608) rebuilt for the CURRENT adapter into one
    1,536-byte bss area on a display change (~-3.0 KB, ~30 ms once a change);
    the front sprite variants derived from `gr_apeleft/right` (~-2.3 KB); the
    marquee masks and runs generated into the existing light cache
    (~-2.2 KB); the sun bit-packed (~-440); the raised-arm poses derived from
    `gr_ape` (~-320).
  - `tools/gorillas_bgm.py` / `grbgmdata.inc` (2,697): byte note indices into
    a frequency table instead of Hz words, ~-1.1 KB.
  - `tools/gorillas_music.py` / `grmusic.inc` (728): byte note + byte ticks,
    ~-350.

### (d) Decisions left to the owner

- **FROTZ's v6 text path**: ~400-600 bytes that can never run; delete, or wire
  up for ~+400 (above).
- **GORILLAS `gr_textdouble`**: a 16-entry table instead of 512 bytes saves
  ~465 of RAM and costs 20-40 cycles a glyph row of HUD and menu text (~7% on
  the input timings). Not taken.
- **FTPD's 32 KB STOR stage** for the window's life (list (a)) — WHEN it is
  held, not throughput.

## Coverage

Each agent ran its app's rows in its own worktree before and after (and drove
by hand what no row reaches). The merged tree (`e299bb3b`, every app in) was
then built (`make` green, fast tier 61/61) and re-ran, all on MartyPC unless
named:

| app | rows on the merged tree | result |
|---|---|---|
| FONTVIEW | `fontview` | ok |
| HELLO | `pkgrun`, `thewire` (QEMU), `lzfmt`, `lzcomp` | ok |
| FRACTAL | `frstepv`, `frcycle`, `frinset`, `frcyclefull`, `dispfrac`, `frpromise`, `appsmall`, `movable` | ok |
| FTPD | `ftpdflick`, `ftpdfocus`, and `tests/ftpd.py` (QEMU, unregistered: LIST, byte-exact RETR, a 20,000-byte STOR checked by an independent FAT reader, active mode, CWD/MKD/RMD/RNTO, Setup, FTPD.CFG) | ok, all assertions |
| GORILLAS | all seven `gorillas*` | ok |
| FPTEST | none registered — agent drove it on MartyPC and QEMU (x87): pixel-identical, ALL PASS, and six data breaks plus an inverted divide CF each went red on the right rows | — |
| IMGTEST | none registered — agent drove it on MartyPC: 27/27, window pixel-identical, two broken decoders went red on exactly SPEC.md 94.3's cases | — |
| FROTZ | none registered; `make zcheck`/`zgfx` need stories and the container's egress refuses `ifarchive.org` — agent ran `tools/zharness.py --compare --graphics` base against new: zopstest v3/v5/v8 and its own coverage story match dfrotz, zpictest 3/3 | **gap: zcheck/zgfx owed** |

The before/after figures in the table were re-read off the merged tree's
build and match every agent's.
