#!/bin/sh
# =============================================================================
# os8088 - apps/cword/build.sh
#
# Build CWORD end to end and put it on a floppy in all three geometries.
#
#     apps/cword/build.sh
#     make test TESTAPPS=build/cword.img
#
# It is a script rather than a make target for the same reason
# tests/chello/build.sh is: the top-level Makefile does not include
# apps/cc/Makefile.inc yet, so nothing in `make` knows how to build a C
# package. When it does, this becomes
#
#     $(eval $(call CC_PACKAGE,cword,cword))
#
# plus the disk rules below, and this file goes away. Until then the chain is
# written out here, and it is the SAME chain - the three host checks are the
# only thing in it that is cword's own.
#
# THE CHAIN
#
#   (0) apps/cword/cwrtfchk.c   the TABLES check themselves, on the host
#   (0) apps/cword/cwrtfrt.c    the RTF READER AND WRITER check themselves, by
#                               round-tripping documents
#   (0) apps/cword/hosttest/    THE REDRAW PATH checks itself, against a model
#       cwuitest.c              of the glass: every keystroke's damage compared
#                               cell for cell with what the screen ought to
#                               show, plus the cost table in calls and glyph
#                               cells
#   (0) apps/cword/hosttest/    THE ONE HAND-WRITTEN ROUTINE, cw_memmove, runs
#       cwmovetest.asm          on a real x86 in QEMU with SS != DS, which is
#                               the only condition under which its bugs would
#                               show. Skipped with a printed line if
#                               qemu-system-i386 is not installed
#                               ...all four run FIRST and all four stop the
#                               build: a stale cell found in an emulator costs
#                               an hour and costs a second here, and two of the
#                               three defects PERFORMANCE.md names cannot be
#                               seen in an emulator at all
#   (1) smlrcc -tiny -S         C -> NASM, but 80386 NASM
#   (2) tools/cc8086.py         -> strict 8086, or REFUSE (SPEC.md 70.6, 67.10)
#   (3) nasm -f bin             one assembly, no linker: the shim %includes
#                               apps/cc/crt0.asm and build/cword.gen.asm
#   (4) tools/os88ovl.py        cut CWORD.OVL off the end (SPEC.md 70.14)
#   (5) tools/os88pkg.py        validate the 32-byte header and stamp it
#   (6) tools/os88disk.py       FAT12, three geometries (CLAUDE.md), --verify
#
# Steps 3, 5 and 6 are the pipeline every assembly package already goes
# through, unchanged and unaware that the source was C; step 4 is the one an
# assembly package only takes if it has a module too (apps/word does).
#
# The compiler is not in this tree: tools/setup-cc.sh fetches SmallerC at its
# pinned commit into build/cc/, which is gitignored, so no compiler binary and
# no compiler source is ever committed.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
SC=$BUILD/cc/SmallerC
HOSTCC=${HOSTCC:-cc}

test -x "$SC/smlrcc" || {
    echo ""
    echo "The C compiler is not built. Run:"
    echo ""
    echo "    tools/setup-cc.sh"
    echo ""
    exit 1
}

mkdir -p $BUILD

# (0) The three host checks. None is in the package and none costs it a byte:
#     cwrtfchk.c defines CW_RTF_SELFCHECK, which is what compiles
#     cw_rtf_selfcheck() at all; cwrtfrt.c includes the RTF engine, which
#     calls no operating system on purpose, so that this is possible; and
#     cwuitest.c includes the whole program against a stubbed API.
$HOSTCC -O1 -Wall -I apps/cword -o $BUILD/cwrtfchk apps/cword/cwrtfchk.c
$BUILD/cwrtfchk
$HOSTCC -O1 -Wall -I apps/cword -o $BUILD/cwrtfrt apps/cword/cwrtfrt.c
$BUILD/cwrtfrt

# The UI harness includes cword.c itself, with apps/cword/hosttest ahead of
# apps/cc on the include path so that its stub os88.h is the one that resolves.
# -w because the stubs deliberately ignore arguments; the checks are the point.
$HOSTCC -O1 -w -DCW_RTF_SELFCHECK=1 -I apps/cword/hosttest -I apps/cword \
    -o $BUILD/cwuitest apps/cword/hosttest/cwuitest.c
$BUILD/cwuitest

# The byte mover, on a real x86. It is a boot sector because SS != DS is the
# condition it has to be tested under and nothing else provides one.
if command -v qemu-system-i386 >/dev/null 2>&1; then
    apps/cword/hosttest/cwmovetest.sh
else
    echo "cwmovetest: SKIPPED - qemu-system-i386 is not installed"
fi

# (1) C -> NASM. -tiny is the one-segment model, -S stops at assembly, and
#     -nopp must NOT be passed: it disables the preprocessor, and #include
#     "os88.h" then fails a long way from the cause.
PATH="$PWD/$SC:$PATH" "$SC/smlrcc" -tiny -S \
    -SI "$SC/v0100/include" -I "$SC/v0100/include" -I apps/cc -I apps/cword \
    apps/cword/cword.c -o $BUILD/cword.raw.asm

# (2) The gate. It lowers the seven non-8086 forms SmallerC emits and REFUSES
#     the C that is silently wrong here - &automatic above all (SPEC.md 70.5).
#     It prints every function's frame size on every build, deliberately
#     (70.8). Never pass --quiet or --no-gate here.
python3 tools/cc8086.py $BUILD/cword.raw.asm -o $BUILD/cword.gen.asm

# (3) One nasm job: the shim, which %includes apps/cc/crt0.asm (via -I apps/)
#     and build/cword.gen.asm (via -I build/).
nasm -f bin -w+error -I apps/ -I $BUILD/ -o $BUILD/cword.bin apps/cword/cword.asm

# (4) THE OVERLAY CUT (SPEC.md 70.14). Everything from `.modc` on is code that
#     must not ship inside CWORD.O88: it becomes CWORD.OVL, a file beside it,
#     read into a heap claim the first time a dialog, a file command or one of
#     the utilities is asked for. os88ovl.py finds the boundary in the image
#     size word the header already carries, so the layout lives in one place -
#     and os88pkg.py then refuses a package whose header and file disagree,
#     which is what stops this step being skippable.
python3 tools/os88ovl.py $BUILD/cword.bin -o $BUILD/CWORD.OVL \
	--trim $BUILD/cword.trim.bin

# (5) and (6) - the ordinary package pipeline, which does not know this was C.
python3 tools/os88pkg.py $BUILD/cword.trim.bin -o $BUILD/cword.o88

# Three geometries, as every shipped image is built (CLAUDE.md): 1.44MB and
# 720KB are what QEMU gets, 360KB is what an 86Box XT or a real one takes. The
# module rides beside the package on every one of them: a disk with CWORD.O88
# and no CWORD.OVL is a program whose menus all refuse, politely (SPEC.md 47).
python3 tools/os88disk.py -o $BUILD/cword.img    --size 1440 $BUILD/cword.o88 $BUILD/CWORD.OVL
python3 tools/os88disk.py -o $BUILD/cword720.img --size 720  $BUILD/cword.o88 $BUILD/CWORD.OVL
python3 tools/os88disk.py -o $BUILD/cword360.img --size 360  $BUILD/cword.o88 $BUILD/CWORD.OVL
python3 tools/os88disk.py --verify $BUILD/cword.img
python3 tools/os88disk.py --verify $BUILD/cword720.img
python3 tools/os88disk.py --verify $BUILD/cword360.img

ls -l $BUILD/cword.bin $BUILD/cword.o88 $BUILD/cword.img $BUILD/cword360.img
