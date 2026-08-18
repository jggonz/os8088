#!/bin/sh
# =============================================================================
# os8088 - apps/runcpm/build.sh
#
# RUNCPM's host checks (SPEC.md 71), run by `make runcpm` BEFORE the target
# build, and each one stops it: a stale cell or a double draw found here costs
# a second, and neither shows in an emulator (PERFORMANCE.md).
#
#     apps/runcpm/build.sh          the checks only
#     make runcpm                   the checks, then the package
#     make runcpmdisk               ...and the floppy, three geometries
#
# THE CHECKS
#   apps/runcpm/hosttest/rcuitest.c   the terminal against a MODEL OF THE
#                                     GLASS: the whole program included over a
#                                     stub os88.h, driven with byte streams
#                                     the way the Z80 side will, model == glass
#                                     and shadow == glass after every step,
#                                     and the cost table in calls and cells
#   (wave 2) hosttest/rcmemtest       the Z80-RAM movers on a real x86 with
#                                     SS != DS, in QEMU, negative controls
#   (wave 4) hosttest/rcfstest.c      the FCB/directory/record layer against a
#                                     fake folder tree
#
# The compiler for the TARGET is not in this tree: tools/setup-cc.sh fetches
# SmallerC at its pinned commit into build/cc/ (gitignored). The checks below
# need only the host's cc.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
HOSTCC=${HOSTCC:-cc}
mkdir -p $BUILD

# The UI harness includes runcpm.c itself, with apps/runcpm/hosttest ahead of
# apps/cc on the include path so that its stub os88.h is the one that resolves.
# -w because the stubs deliberately ignore arguments; the checks are the point.
$HOSTCC -O1 -w -I apps/runcpm/hosttest -I apps/runcpm \
    -o $BUILD/rcuitest apps/runcpm/hosttest/rcuitest.c
$BUILD/rcuitest
