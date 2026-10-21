#!/bin/sh
# =============================================================================
# os8088 - apps/lemmings/build.sh
#
# LEMMINGS's host checks (SPEC.md 116.12), run by `make lemmings` BEFORE anything
# is built for the 8086, and each one stops the build: a stale cell, a
# double-draw or a row that fell past the dock costs an hour in an emulator and
# a second here, and two of PERFORMANCE.md's three defects cannot be seen in an
# emulator at all.
#
#     apps/lemmings/build.sh        the checks only
#     make lemmings                 the checks, then the package
#     make lemmingsdisk             ...and the floppy, four geometries
#
# THE CHECKS
#   tools/os88lem.py --selfcheck        the converter re-reads what it wrote and
#                                       compares it with what it decoded. It
#                                       runs WITH OR WITHOUT the fetched data:
#                                       with no build/lemdata it checks the
#                                       parts that do not need it and says so
#   apps/lemmings/hosttest/lemtest.c    the whole program #included over a stub
#                                       os88.h and driven like a user, against
#                                       the COMMITTED SYNTHETIC FIXTURE - the
#                                       paged list rebuilt independently at three
#                                       window heights, the shadow audited
#                                       against a model of the glass, no cell
#                                       written twice, no row past the content
#                                       box, every character inside the kernel
#                                       face, a greyed row naming its fact, and
#                                       the cost table printed
#
# NEITHER NEEDS A NETWORK FETCH, which is what makes `make lemmings` work on a
# tree that has never run tools/getlemmings.py. Only `make lemmingsdisk` needs
# the real data (SPEC.md 116.12).
#
#   tools/lemsim.py + lemtest --mech    THE MECHANICS REPLAY (wave 3): three
#                                       fixture levels driven for up to 2,000
#                                       frames by committed command scripts,
#                                       once by the package's C and once by an
#                                       independent Python reading of the
#                                       mechanics, diffed frame by frame and
#                                       lemming by lemming, with the reference's
#                                       own trace pinned by a committed SHA-256
#
# STILL OWED (wave 2's plan, not built): a raw-x86 harness for lemmask.inc's
# probes and lemblit.inc's movers, run in QEMU with SS != DS and with negative
# controls, because that is the one place ES is loaded (apps/cword/hosttest/
# cwmovetest.asm is the shape). tests/lembench exercises the same routines on
# MartyPC and the glass checks exercise them on all three adapters.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
HOSTCC=${HOSTCC:-cc}
mkdir -p $BUILD

# (0) The header FIRST and then the converter's self-check, in that order and
#     as two runs, because --selfcheck returns before write_lemstr_h() is
#     reached and only the --fixture and full-convert arms write it. Written
#     here rather than left to the Makefile's own $(BUILD)/lemstr.h rule so that
#     the standalone route this file's header advertises - `apps/lemmings/
#     build.sh`, the checks only - works on a tree that has never run make: the
#     compile below #includes lemstr.h, and the ids the program compiles against
#     then cannot be older than the table.
python3 tools/os88lem.py --fixture $BUILD/lemfixture --header $BUILD/lemstr.h
python3 tools/os88lem.py --selfcheck

# (0b) ...AND THE COMMITTED FIXTURE IS THE ONE THAT WAS JUST GENERATED. The
#      harness reads apps/lemmings/hosttest/fixture/ and the run above wrote
#      build/lemfixture/ from the same table, so nothing compared them: a
#      reworded, reordered or mid-table-inserted row in tools/os88lem.py's
#      string table regenerates build/lemstr.h and the shipped LEMSTR.LEM and
#      leaves the committed copy alone - and LEMS_* ids are POSITIONAL, so the
#      harness would assert the old wording against the new ids and pass.
#      lem_str_load()'s own "lem_strn < LEMS_COUNT" catches an APPEND and
#      nothing else. This is the one place lemtext.c's "THE TWO HALVES CANNOT
#      DRIFT" is actually asserted.
for f in $BUILD/lemfixture/*.LEM; do
    b=$(basename "$f")
    if ! cmp -s "$f" "apps/lemmings/hosttest/fixture/$b"; then
        echo "build.sh: apps/lemmings/hosttest/fixture/$b is stale."
        echo "  tools/os88lem.py now writes different bytes. The LEMS_* ids are"
        echo "  positional, so the harness would assert the old wording against"
        echo "  the new ids. Refresh it:"
        echo "    cp $BUILD/lemfixture/*.LEM apps/lemmings/hosttest/fixture/"
        exit 1
    fi
done
for f in apps/lemmings/hosttest/fixture/*.LEM; do
    b=$(basename "$f")
    if [ ! -f "$BUILD/lemfixture/$b" ]; then
        echo "build.sh: apps/lemmings/hosttest/fixture/$b is not written by"
        echo "  tools/os88lem.py --fixture any more. Delete it or restore it."
        exit 1
    fi
done

# (0c) THE ICON'S PICTURE IS ITS SOURCE (wave 6). Every data row of
#      apps/lemmings/icon.inc carries its own sixteen pixels in its comment;
#      this re-derives each dw from the picture and stops the build on the
#      first that disagrees, and checks the mask rows are the full tile - so
#      the bytes cannot drift from the drawing a reader sees.
python3 - apps/lemmings/icon.inc <<'PYEOF'
import re, sys
rows = [l for l in open(sys.argv[1]) if re.match(r"\s+dw 0x", l)]
if len(rows) != 32:
    sys.exit("build.sh: icon.inc has %d dw rows, not 32" % len(rows))
for i, l in enumerate(rows[:16]):
    if int(l.split()[1], 16) != 0xFFFF:
        sys.exit("build.sh: icon.inc mask row %d is not 0xFFFF" % i)
for i, l in enumerate(rows[16:]):
    m = re.search(r";.*?([#.]{16})\s*$", l)
    if not m:
        sys.exit("build.sh: icon.inc data row %d has no 16-pixel picture" % i)
    want = int(m.group(1).replace("#", "1").replace(".", "0"), 2)
    if int(l.split()[1], 16) != want:
        sys.exit("build.sh: icon.inc data row %d is %s but its picture says "
                 "0x%04X" % (i, l.split()[1], want))
PYEOF

# (1) The whole program against a model of the glass. apps/lemmings/hosttest is
#     AHEAD of apps/cc on the include path so its stub os88.h is the one that
#     resolves; -w because the stubs deliberately ignore arguments and the
#     checks are the point.
$HOSTCC -O1 -w -I apps/lemmings/hosttest -I apps/lemmings -I $BUILD \
    -o $BUILD/lemtest apps/lemmings/hosttest/lemtest.c
# the spray's area row reads the REAL table too when tools/getlemmings.py has
# fetched it (it is never committed; the fixture's invented one always runs)
if [ -f $BUILD/lemdata/PARTICLES.DAT ]; then
    LEMTEST_PARTICLES=$BUILD/lemdata/PARTICLES.DAT $BUILD/lemtest
else
    $BUILD/lemtest
fi

# (2) THE MECHANICS REPLAY (SPEC.md 116.12). Three fixture levels, each driven
#     for up to 2,000 frames by a committed command script (hosttest/mech/
#     levelN.txt), TWICE: by tools/lemsim.py - the mechanics read again from
#     lemmings_3ds src/lemming.c under the rulings its header names - and by
#     the package's own C through `lemtest --mech`. Every frame's counts and
#     every lemming's x, y, action, frame, flags and fuse must agree, and the
#     reference's own trace must hash to the committed value, so neither side
#     can move without the build saying which frame, which lemming and which
#     field. A slip in the C - a fall limit three high, a fuse one short - is
#     a divergence here (both were tried); the scripts reach all eighteen
#     actions, the eight skills and their refusals, a trap, water, steel, the
#     nuke, the pause and the rate's clamps. lemtest prints each level's
#     probe calls a frame, priced at tests/lembench's 178 us a call - the
#     mechanics' cost-table row (SPEC.md 116.7.3).
M=$BUILD/lemmech
mkdir -p $M
# EVERY SCRIPT IN THE FOLDER, keyed by its name: levelN.txt drives fixture
# level N and levelN<tag>.txt drives it again under another script (level2n is
# the early nuke the NUKE GLITCH needs), each with its own pinned hash.
for S in apps/lemmings/hosttest/mech/level*.txt; do
    K=$(basename "$S" .txt)
    K=${K#level}
    L=${K%%[!0-9]*}
    H=$(awk -v k="$K" '$1 == k { print $2 }' apps/lemmings/hosttest/mech/traces.sha256)
    if [ -z "$H" ]; then
        echo "build.sh: $S has no pinned hash in hosttest/mech/traces.sha256"
        exit 1
    fi
    $BUILD/lemtest --mech $L "$S" 2000 $M/c$K.trace
    python3 tools/lemsim.py --fixture apps/lemmings/hosttest/fixture \
        --level $L --script "$S" \
        --frames 2000 -o $M/sim$K.trace --sha256 "$H" --compare $M/c$K.trace
done
