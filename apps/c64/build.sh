#!/bin/sh
# =============================================================================
# os8088 - apps/c64/build.sh
#
# C64's host checks (C64-SPEC §14.5), run by `make c64` BEFORE the
# target build, and each one stops it: a stale cell, a double draw or a shadow
# that has stopped describing the glass costs a second on the target, and none
# of the three shows in an emulator (PERFORMANCE.md).
#
#     apps/c64/build.sh          the checks only
#     make c64                   the checks, then the package
#     make c64disk               ...and the floppy
#
# THE CHECKS
#   tools/c64rom.py --check        the three committed Commodore ROM images
#                                  against their SHA-256s, before anything
#                                  reads them (C64-SPEC §1.3). The CHECK only:
#                                  build/c64-rom/C64.ROM itself belongs to the
#                                  Makefile rule that names it
#   hosttest/c64uitest.c           the whole program over a stub os88.h with a
#                                  PIXEL model of the glass: after every step
#                                  the glass must show what the 8,000-byte
#                                  shadow says it shows, and the cost table is
#                                  printed in MILLISECONDS (9.7)
#   hosttest/c64uitest --no-rom    ...and again, as a second process, for the
#                                  machine with no C64.ROM: os88_main decides
#                                  that surface once per launch (1.4)
#   tools/c64ref.py --check        ...and the composed frame against an
#                                  INDEPENDENT compositor, bit for bit - twice,
#                                  once with the CHARGEN ROM and once with a
#                                  RAM character set. Then --selftest, which
#                                  injects a one-bit defect and requires the
#                                  compare to FAIL: a check that cannot fail is
#                                  not a check
#   hosttest/c64memtest.sh         c64mem.inc AND c64band.inc's string loops on
#                                  a real x86 with SS != DS and an ES sentinel,
#                                  in raw QEMU, with negative controls (3.6)
#
# NOT here, because it takes minutes: hosttest/c64cputest.sh runs the 6510 core
# against Klaus Dormann's functional test and the eleven other rows of 4.6 -
# twelve rows in all (`make c64cputest`) - the rcz80test precedent.
#
# The compiler for the TARGET is not in this tree: tools/setup-cc.sh fetches
# SmallerC at its pinned commit into build/cc/ (gitignored). The checks below
# need only the host's cc, python3 and - for the mover gate - qemu.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
HOSTCC=${HOSTCC:-cc}
mkdir -p $BUILD

# The ROMs, before anything reads them. A ROM that is not the one the port was
# written against is a machine that boots to something else, and "it booted" is
# not the same claim as "it booted the machine this document describes".
python3 tools/c64rom.py --check
# ...and NOT `-o build/c64-rom/C64.ROM`. That file has a make rule of its own
# and is a prerequisite of build/c64.img through C64DISK, so writing it from
# the .c64-hostchecks stamp recipe re-made a downstream prerequisite behind
# make's back and relinked the disk on every host-check run. The artefact has
# ONE owner: the Makefile. c64uitest reads it and says what to run if it is
# not there.

# The UI harness includes c64.c itself, with apps/c64/hosttest ahead of apps/cc
# on the include path so that its stub os88.h is the one that resolves. -w
# because the stubs deliberately ignore arguments; the checks are the point.
# -DC64_HOST is what keeps the cost counters (c64scr.c's c64_n_*, c64kbd.c's
# c64_ndrop) OUT of the shipping image: nothing in apps/c64/*.c reads one, the
# Makefile's smlrcc line never defines it, and this harness is their only
# reader (§9.7). The build fails loudly on any increment left unguarded.
$HOSTCC -O1 -w -DC64_HOST -I apps/c64/hosttest -I apps/c64 \
    -o $BUILD/c64uitest apps/c64/hosttest/c64uitest.c
$BUILD/c64uitest
# ...and the ROM-LESS machine, which is a SECOND PROCESS because os88_main
# decides that surface once per launch (1.4). It is the screen a user of a
# mis-copied disk sees, and until this run existed nothing drew it.
$BUILD/c64uitest --no-rom

# ...and the frame it composed, against the independent compositor. The state
# c64uitest dumps last is the RAM-character-set one, which is the case a
# cell-identity model provably cannot check.
python3 tools/c64ref.py --check $BUILD/c64ref-state.bin $BUILD/c64ref-frame.bin
python3 tools/c64ref.py --check $BUILD/c64ref-state2.bin $BUILD/c64ref-frame2.bin
python3 tools/c64ref.py --selftest $BUILD/c64ref-state2.bin $BUILD/c64ref-frame2.bin
# ...and the luminance ladder, over all 256 ORDERED PAIRS - which is the only
# way to ask about the seven equal-luminance pairs in both directions (9.6);
# a rendered frame can only carry one background at a time.
python3 tools/c64ref.py --lumcheck $BUILD/c64ref-lum.bin

# The movers and the composer's string loops, on a real x86 with SS != DS.
apps/c64/hosttest/c64memtest.sh

# THE MESSAGE-LENGTH GATE'S LIST, TIED TO THE SOURCES (§10.1). c64uitest.c
# walks a hand-typed msgs[] array and asserts every entry fits C64_MSGCELLS;
# nothing tied that array to the code, so a c64_say() added in a later wave and
# not copied in was simply NOT CHECKED - a gate that silently narrows instead
# of failing. This extracts every c64_say literal out of apps/c64/*.c and
# requires the array to contain each one. The two permanent lines c64_status
# draws itself are not c64_say calls and stay in the array by hand.
python3 - <<'PY'
import re, sys, glob
said = set()
for f in glob.glob('apps/c64/*.c'):
    src = open(f).read()
    for m in re.finditer(r'c64_say\(\s*"((?:[^"\\]|\\.)*)"', src):
        said.add(m.group(1))
h = open('apps/c64/hosttest/c64uitest.c').read()
i = h.index('static const char *msgs[] = {')
listed = set(re.findall(r'"((?:[^"\\]|\\.)*)"', h[i:h.index('};', i)]))
missing = sorted(said - listed)
if missing:
    for s in missing:
        print('c64msgs: c64_say("%s") is not in c64uitest.c\'s msgs[] - the '
              'length gate never sees it' % s)
    sys.exit(1)
print('c64msgs: %d c64_say literal(s), every one in the length gate' % len(said))
PY
