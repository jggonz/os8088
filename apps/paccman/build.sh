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
# THE THREE MEASURED TERMS, in microseconds on a 4.77 MHz 8088. Leave them
# EMPTY and the harness prices its rows from zeros and says so on its last
# line, which is what a tree that has never run the bench should see; an
# environment variable of the same name overrides one for an experiment.
# From `make pmcbandbench` on QEMU under `-icount shift=3`, converted at
# PERFORMANCE.md Part 4's one count = 0.359 ms of real XT (SPEC.md 91):
#   TILE step 1   2.000 counts ->  0.72 ms      PACK_PL 8 rows 116.375 -> 41.78 ms
#   TILE step 2   1.250 counts ->  0.45 ms      PACK_1  8 rows  50.500 -> 18.13 ms
PMC_T_TILE=${PMC_T_TILE:-718}       # one 8x8 tile composed, us
PMC_T_PACKPL=${PMC_T_PACKPL:-5222}  # one packed row -> four planes, at 28 cols
PMC_T_PACK1=${PMC_T_PACK1:-2266}    # one packed row -> 1bpp, at 28 cols
PMC_TERMS=""
if [ -n "$PMC_T_TILE" ] && [ -n "$PMC_T_PACKPL" ] && [ -n "$PMC_T_PACK1" ]; then
    PMC_TERMS="-DPMC_T_TILE=$PMC_T_TILE -DPMC_T_PACKPL=$PMC_T_PACKPL -DPMC_T_PACK1=$PMC_T_PACK1"
fi
$HOSTCC -O1 -w -DPMC_HOST $PMC_TERMS \
    -I apps/paccman/hosttest -I apps/paccman \
    -o $BUILD/pmcuitest apps/paccman/hosttest/pmcuitest.c
$BUILD/pmcuitest

# --- the composer, on a real x86 with SS != DS ------------------------------
apps/paccman/hosttest/pmcbandtest.sh
