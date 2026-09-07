#!/bin/sh
# =============================================================================
# os8088 - apps/lemmings/build.sh
#
# LEMMINGS's host checks (SPEC.md 92.12), run by `make lemmings` BEFORE anything
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
# the real data (SPEC.md 92.12).
#
# WAVE 2 ADDS a third: the raw-x86 harness for lemmask.inc's probes and
# lemblit.inc's movers, run in QEMU with SS != DS and with negative controls,
# because that is the one place ES is loaded (apps/cword/hosttest/cwmovetest.asm
# is the shape).
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

# (1) The whole program against a model of the glass. apps/lemmings/hosttest is
#     AHEAD of apps/cc on the include path so its stub os88.h is the one that
#     resolves; -w because the stubs deliberately ignore arguments and the
#     checks are the point.
$HOSTCC -O1 -w -I apps/lemmings/hosttest -I apps/lemmings -I $BUILD \
    -o $BUILD/lemtest apps/lemmings/hosttest/lemtest.c
$BUILD/lemtest
