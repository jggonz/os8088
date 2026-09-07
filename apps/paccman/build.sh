#!/bin/sh
# =============================================================================
# os8088 - apps/paccman/build.sh
#
# PACCMAN's host checks (SPEC.md 91), run by `make paccman` BEFORE the target
# build, and each one stops it. A missed damage mark, a double draw or a
# composer that is wrong in the fourth plane costs a second on the target and
# none of the three shows in an emulator (PERFORMANCE.md).
#
#     apps/paccman/build.sh      the checks only
#     make paccman               the checks, then the package
#     make paccmandisk           ...and the floppy in four geometries
#
# THE CHECKS
#   tools/paccman_assets.py --check   the committed apps/paccman/pmc_rom.c
#                                     against what the extractor writes now -
#                                     ONLY when a checkout of the reference is
#                                     at --ref or $PACMANC_SRC. Otherwise one
#                                     printed note naming the pin, cc-note's
#                                     shape: THE COMMITTED FILE IS THE BUILD'S
#                                     TRUTH and an ordinary build needs neither
#                                     the tool nor the reference
#   hosttest/pmcuitest.c              the whole program over a stub os88.h with
#                                     a PIXEL model of the glass, on all five
#                                     layouts and all three blit paths; after
#                                     every frame the glass must equal an
#                                     INDEPENDENT recomposition of the two RAMs
#   hosttest/pmcbandtest.sh           pmcband.inc's shipping routines on a real
#                                     x86 with SS != DS and an ES sentinel, in
#                                     raw QEMU, against the vectors pmcuitest
#                                     just wrote, with negative controls
#                                     (skipped with a note where qemu is absent)
#
# The compiler for the TARGET is not in this tree: tools/setup-cc.sh fetches
# SmallerC at its pinned commit into build/cc/ (gitignored). The checks below
# need only the host's cc, python3 and - for the composer gate - qemu.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
HOSTCC=${HOSTCC:-cc}
mkdir -p $BUILD

# --- the generated tables, if there is anything to check them against -------
# The reference is not vendored (CONTRIBUTING.md 6) and a contributor's tree
# will not have it. A build that FAILED without it would make the reference a
# build dependency, which is exactly what committing pmc_rom.c avoids.
REF="${PACMANC_SRC:-}"
if [ -n "$REF" ] && [ -e "$REF" ]; then
    python3 tools/paccman_assets.py --check --ref "$REF"
else
    echo "paccman: no reference checkout (\$PACMANC_SRC unset), so"
    echo "         apps/paccman/pmc_rom.c is taken as written. It is the"
    echo "         build's truth; to re-derive it, point \$PACMANC_SRC at a"
    echo "         checkout of github.com/floooh/pacman.c at"
    echo "         0f5ec5a384c1988d9889046d92e615219e1cf3b4."
fi

# --- the UI harness ---------------------------------------------------------
# apps/paccman/hosttest ahead of apps/cc on the include path, so its stub
# os88.h is the one that resolves. -w because the stubs deliberately ignore
# arguments; the checks are the point.
#
# THE THREE COMPOSER TERMS ARE MEASURED AND THEY LIVE HERE. They come from
# `make pmcbandbench` under `qemu-system-i386 -icount shift=3`, converted at
# PERFORMANCE.md Part 4's one count = 0.359 ms of real XT (SPEC.md 91), and
# from nowhere else; this is the one place they are written down for the
# harness. A build with them absent prices a frame from zeros and SAYS SO on
# its last line rather than printing a plausible number.
#
# THE SEVEN MEASURED TERMS, in microseconds on a 4.77 MHz 8088. Leave one
# EMPTY and the harness prices its rows from zeros for that term and says so
# on its last line, which is what a tree that has never run the bench should
# see; an environment variable of the same name overrides one for an
# experiment.
#
# The first five are `make pmcbandbench` on QEMU under `-icount shift=3`,
# converted at PERFORMANCE.md Part 4's one count = 0.359 ms of real XT
# (SPEC.md 91's table is this run):
#   TILE step 1        1.949 counts ->  0.70 ms   PACK_PL 8 rows 116.250
#   TILE step 2 sample 1.164 counts ->  0.42 ms   PACK_1  8 rows  50.375
#   TILE step 2 MERGED 1.520 counts ->  0.55 ms   SPRITE 16x8 even  19.375
#   BLITP 20.500  BLIT4 134.625  BLIT1 3.375      SPRITE odd+flipx  20.375
#                                                 ...even MERGED    23.125
#                                                 ...odd+flipx M    24.375
# ...and READ THE SECOND RUN. The first run of a session prices BLIT4 about
# 10% high (148.0 counts against 134.75); runs 2 and 3 agree to the count on
# every row, and 134.75 is also what wave 1 read.
#
# THE THREE TILE ROWS ARE MEASURED AT N = 256, NOT AT THE BENCH'S PB_N = 8,
# and the terms above are the re-take. One tile is about two PIT counts, so at
# eight iterations a per-tile figure lands on a 0.125-count grid - which is
# the size of the whole difference the CGA row merge makes. Read that way the
# merge cost 1.250 - 1.125 = 0.125 counts a tile while the `BAND cga` A/B over
# the same 28 tiles said 10.5 counts, three times as much; at N = 256 the two
# agree (0.371 x 28 = 10.4 against 9.875 - 10.25 over three runs), and the
# bench prints that reconciliation itself. The merge is +32% ON A TILE and
# +16% on the CGA band it is paid for on - the band figure, which is the one
# SPEC.md 91's keep/revert gate was taken on, did not move.
#
# PMC_T_SPRROW is the MEAN of the bench's two SAMPLED sprite rows over their 8
# rows ((19.375 + 20.375) / 2 / 8 x 359 us): the even-nibble case and the
# odd-nibble-plus-flipx one differ by 5%, and Pac-Man spends about half his
# frames at each, so a frame priced from either alone would be wrong by half
# that in a stated direction. PMC_T_SPRROWM is the same mean over the two
# MERGED rows ((23.125 + 24.375) / 2 / 8 x 359 us) - the CGA layout's own
# price, +20% on a row, and pmc_draw_band counts which rows were which.
#
# PMC_T_LOGIC is NOT the bench's: it is a C function and the bench is a
# standalone assembly package that cannot call one. It is tests/paccman.py's
# cycle bracket around pmc_game_tick's entry and its own return address on
# MartyPC, the MINIMUM of eleven samples (the larger ones carry whatever
# interrupt landed inside the call, and IRQ0 is charged to the machine rather
# than to the function). Six runs across the three profiles read 85,374 /
# 86,742 / 89,990 / 90,496 / 91,432 / 91,576 cycles - 17.9 to 19.2 ms of
# 4.77 MHz 8088, the spread being what a differently-placed round does to the
# same function - and 18,600 is their middle.
PMC_T_TILE=${PMC_T_TILE:-700}       # one 8x8 tile composed, us
PMC_T_TILE2=${PMC_T_TILE2:-418}     # ...at rowstep 2 (CGA), the plain SAMPLE
PMC_T_TILE2M=${PMC_T_TILE2M:-546}   # ...and at rowstep 2 with the row MERGE,
                                    # which is what SHIPS: pmc_draw_band passes
                                    # pmc_zmask always (SPEC.md 91)
PMC_T_PACKPL=${PMC_T_PACKPL:-5217}  # one packed row -> four planes, at 28 cols
PMC_T_PACK1=${PMC_T_PACK1:-2261}    # one packed row -> 1bpp, at 28 cols
PMC_T_SPRROW=${PMC_T_SPRROW:-892}   # one 16-pixel sprite ROW merged into a band
PMC_T_SPRROWM=${PMC_T_SPRROWM:-1066} # ...and the same row with the CGA ROW
                                    # MERGE on, which is what the short display
                                    # ships (SPEC.md 91). Leave it EMPTY and
                                    # the harness prices every CGA sprite row
                                    # at PMC_T_SPRROW and says so on its last
                                    # line
PMC_T_LOGIC=${PMC_T_LOGIC:-18600}   # one game_tick() with five actors
PMC_TERMS=""
if [ -n "$PMC_T_TILE" ] && [ -n "$PMC_T_PACKPL" ] && [ -n "$PMC_T_PACK1" ]; then
    PMC_TERMS="-DPMC_T_TILE=$PMC_T_TILE -DPMC_T_PACKPL=$PMC_T_PACKPL -DPMC_T_PACK1=$PMC_T_PACK1"
fi
if [ -n "$PMC_T_TILE2" ] && [ -n "$PMC_T_TILE2M" ]; then
    PMC_TERMS="$PMC_TERMS -DPMC_T_TILE2=$PMC_T_TILE2 -DPMC_T_TILE2M=$PMC_T_TILE2M"
fi
[ -n "$PMC_T_SPRROW" ] && PMC_TERMS="$PMC_TERMS -DPMC_T_SPRROW=$PMC_T_SPRROW"
[ -n "$PMC_T_SPRROWM" ] && PMC_TERMS="$PMC_TERMS -DPMC_T_SPRROWM=$PMC_T_SPRROWM"
[ -n "$PMC_T_LOGIC" ] && PMC_TERMS="$PMC_TERMS -DPMC_T_LOGIC=$PMC_T_LOGIC"
$HOSTCC -O1 -w -DPMC_HOST $PMC_TERMS \
    -I apps/paccman/hosttest -I apps/paccman \
    -o $BUILD/pmcuitest apps/paccman/hosttest/pmcuitest.c
$BUILD/pmcuitest

# --- the composer, on a real x86 with SS != DS ------------------------------
apps/paccman/hosttest/pmcbandtest.sh
