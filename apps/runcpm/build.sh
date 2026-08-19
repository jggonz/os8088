#!/bin/sh
# =============================================================================
# os8088 - apps/runcpm/build.sh
#
# RUNCPM's host checks (SPEC.md 74), run by `make runcpm` BEFORE the target
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
#   apps/runcpm/hosttest/rcmemtest.sh the Z80-RAM movers on a real x86 with
#                                     SS != DS, in raw QEMU, with negative
#                                     controls (a few seconds)
#   apps/runcpm/hosttest/rcfstest.c   the disk layer (rcfs.c, SPEC.md 74.3)
#                                     with the BDOS dispatcher over a fake
#                                     folder tree with contents: names, the
#                                     directory-entry synthesis and its
#                                     extent chain, the record math, the
#                                     open-file table, $$$.SUB, the capture
#                                     files, the errors, USER n, BDOS 249
#
# NOT here, because it takes minutes: apps/runcpm/hosttest/rcz80test.sh runs
# the Z80 core against ZEXDOC in raw QEMU (`make rcz80test`); tests/rczex.py
# is the same oracle inside os8088 (`make rczex`).
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

# The disk layer, through rc_bdos, against a fake folder tree with contents.
$HOSTCC -O1 -w -I apps/runcpm/hosttest -I apps/runcpm \
    -o $BUILD/rcfstest apps/runcpm/hosttest/rcfstest.c
$BUILD/rcfstest

# The movers, on a real x86 with SS != DS (a boot sector in raw QEMU).
apps/runcpm/hosttest/rcmemtest.sh
