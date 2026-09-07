#!/bin/sh
# =============================================================================
# os8088 - apps/apple2/build.sh
#
# APPLE2's host checks (docs/APPLE2-SPEC.md section 16.6), run by `make
# apple2` BEFORE the target build, and each one stops it: a stale line, a
# double draw or a shadow that has stopped describing the glass costs a
# second on the target, and none of the three shows in an emulator
# (PERFORMANCE.md).
#
#     apps/apple2/build.sh       the checks only
#     make apple2                the checks, then the package
#     make apple2disk            ...and the four floppies
#
# **EVERY STEP FAILS RATHER THAN PASSING WHEN ITS SUBJECT IS ABSENT**
# (docs/APPLE2-PORT-PLAN.md Decision 12). That is why `a2ref.py --check` takes
# an EXPLICIT MODE LIST - wave 1 asks for `text` alone, and the wave that
# writes the lo-res and hi-res composers adds them - and why the a2_say walk
# below carries an expected MINIMUM: a corpus of zero literals would otherwise
# print green from wave 1 to the wave that adds the first message.
#
# THE CHECKS
#   tools/getapple2rom.py --check  the three fetched Apple II+ ROM images
#                                  against their SHA-256s, before anything
#                                  reads them (section 1.4). The CHECK only:
#                                  build/apple2-rom/APPLE2.ROM itself belongs
#                                  to the Makefile rule that names it
#   hosttest/a2uitest.c            the whole program over a stub os88.h with a
#                                  PIXEL model of the glass: after every step
#                                  the glass must show what the 7,680-byte
#                                  shadow says it shows, and the cost table is
#                                  printed in MILLISECONDS (section 7.9)
#   tools/a2ref.py --check         ...and the composed frame against an
#                                  INDEPENDENT compositor, bit for bit - twice,
#                                  once on each FLASH PHASE, which is the same
#                                  memory and different pixels. Then
#                                  --selftest, which injects a one-bit defect
#                                  and requires the compare to FAIL: a check
#                                  that cannot fail is not a check. And
#                                  --romshape, which asserts the PINNED ROM's
#                                  measured 128 distinct bitmaps, so the
#                                  64-glyph decision is checkable rather than
#                                  remembered
#   hosttest/a2memtest.sh          a2mem.inc AND a2band.inc's string loops on
#                                  a real x86 with SS != DS and an ES
#                                  sentinel, in raw QEMU, with FOUR negative
#                                  controls (section 3.4)
#
# NOT here, because it takes minutes: hosttest/a2cputest.sh runs the 6502 core
# against Klaus Dormann's functional test and the eleven other rows of section
# 4.4 (`make a2cputest`) - the rcz80test precedent. It arrives with the core
# it gates, in wave 2.
#
# NOT here YET: `a2ref.py --lumcheck`, which is the lo-res composer's 16-entry
# luminance ladder over all 256 ordered pairs. Its SUBJECT does not exist
# until the wave that writes that composer, and a green pass over a table that
# is not there is exactly what these harnesses are built not to print.
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

# THE ROMS, BEFORE ANYTHING READS THEM. A ROM that is not the one the port was
# written against is a machine that boots to something else, and "it booted"
# is not the same claim as "it booted the machine this document describes".
python3 tools/getapple2rom.py --check
# ...and NOT `-o build/apple2-rom/APPLE2.ROM`. That file has a make rule of
# its own and is a prerequisite of build/apple2.o88 through the package's
# parts list, so writing it from the .apple2-hostchecks stamp recipe would
# re-make a downstream prerequisite behind make's back. The artefact has ONE
# owner: the Makefile.
python3 tools/a2ref.py --romshape $BUILD/apple2-rom/APPLE2.ROM

# The UI harness includes apple2.c itself, with apps/apple2/hosttest ahead of
# apps/cc on the include path so that its stub os88.h is the one that
# resolves. -w because the stubs deliberately ignore arguments; the checks are
# the point. -DA2_HOST is what keeps the cost counters OUT of the shipping
# image: nothing in apps/apple2/*.c reads one, the Makefile's smlrcc line
# never defines it, and this harness is their only reader.
$HOSTCC -O1 -w -DA2_HOST -I apps/apple2/hosttest -I apps/apple2 \
    -o $BUILD/a2uitest apps/apple2/hosttest/a2uitest.c
$BUILD/a2uitest

# ...and the frames it composed, against the independent compositor. TWO of
# them, one per FLASH PHASE: the same memory, different pixels, which is the
# one thing on the glass the damage model cannot see (section 7.6).
python3 tools/a2ref.py --check text $BUILD/a2ref-state.bin $BUILD/a2ref-frame.bin
python3 tools/a2ref.py --check text $BUILD/a2ref2-state.bin $BUILD/a2ref2-frame.bin
python3 tools/a2ref.py --selftest text $BUILD/a2ref-state.bin $BUILD/a2ref-frame.bin

# The movers and the composer's string loops, on a real x86 with SS != DS.
apps/apple2/hosttest/a2memtest.sh

# THE MESSAGE-LENGTH GATE'S LIST, TIED TO THE SOURCES (section 9). a2uitest.c
# walks a hand-typed msgs[] array and asserts every entry fits the status
# row's cells; nothing ties that array to the code, so an a2_say() added in a
# later wave and not copied in would simply NOT BE CHECKED - a gate that
# silently narrows instead of failing. This extracts every a2_say literal out
# of apps/apple2/*.c and requires the array to contain each one, AND requires
# the corpus to reach an explicit MINIMUM, so that "no literals at all" is a
# failure rather than a pass.
python3 - <<'PY'
import re, sys, glob
A2_SAY_MIN = 5
said = set()
for f in glob.glob('apps/apple2/*.c'):
    src = open(f).read()
    for m in re.finditer(r'a2_say\(\s*"((?:[^"\\]|\\.)*)"', src):
        said.add(m.group(1))
h = open('apps/apple2/hosttest/a2uitest.c').read()
i = h.index('static const char *msgs[] = {')
listed = set(re.findall(r'"((?:[^"\\]|\\.)*)"', h[i:h.index('};', i)]))
missing = sorted(said - listed)
if missing:
    for s in missing:
        print('a2msgs: a2_say("%s") is not in a2uitest.c\'s msgs[] - the '
              'length gate never sees it' % s)
    sys.exit(1)
if len(said) < A2_SAY_MIN:
    print('a2msgs: only %d a2_say literal(s) in apps/apple2/*.c, and the gate '
          'expects at least %d - a corpus this small is a gate that is not '
          'looking' % (len(said), A2_SAY_MIN))
    sys.exit(1)
print('a2msgs: %d a2_say literal(s), every one in the length gate' % len(said))
PY
