#!/bin/sh
# =============================================================================
# os8088 - tests/chello/build.sh
#
# Build the C toolchain's capability gate (SPEC.md 73) end to end and put it on
# a floppy. It is a script rather than a make target because nothing under
# tests/ ships and the top-level Makefile does not know about C packages yet -
# apps/cc/Makefile.inc holds the reusable rules and this is the same five steps
# written out for one program that lives outside apps/.
#
#     tests/chello/build.sh
#     make test TESTAPPS=build/chello.img
#
# THE CHAIN, WHICH IS THE POINT OF THE TEST. Steps 3, 4 and 5 are the pipeline
# every assembly package already goes through, unchanged and unaware that the
# source was C - which is the test of whether the toolchain was done right:
#
#   tests/chello/chello.c
#        | smlrcc -tiny -S             (1) C -> NASM, but 80386 NASM
#   build/chello.raw.asm
#        | tools/cc8086.py             (2) -> strict 8086, or REFUSE
#   build/chello.gen.asm
#        | nasm -f bin                 (3) one assembly, no linker, from
#   build/chello.bin                       tests/chello/chello.asm
#        | tools/os88pkg.py            (4) validate the 32-byte header and stamp
#   build/chello.o88
#        | tools/os88disk.py           (5) FAT12, and --verify is a structural
#   build/chello.img                       fsck of what came out
#
# The compiler is not in this tree: tools/setup-cc.sh fetches SmallerC at its
# pinned commit into build/cc/, which is gitignored, so no compiler binary and
# no compiler source is ever committed.
# =============================================================================
set -e

cd "$(dirname "$0")/../.."          # the repo root, whatever the caller's cwd

BUILD=build
SC=$BUILD/cc/SmallerC

test -x "$SC/smlrcc" || {
    echo ""
    echo "The C compiler is not built. Run:"
    echo ""
    echo "    tools/setup-cc.sh"
    echo ""
    exit 1
}

# (1) C -> NASM. -tiny is the one-segment model, -S stops at assembly, and
#     -nopp must NOT be passed: it disables the preprocessor, and #include
#     "os88.h" then fails a long way from the cause.
PATH="$PWD/$SC:$PATH" "$SC/smlrcc" -tiny -S \
    -SI "$SC/v0100/include" -I "$SC/v0100/include" -I apps/cc \
    tests/chello/chello.c -o $BUILD/chello.raw.asm

# (2) The gate. It lowers the five non-8086 families SmallerC emits and REFUSES
#     the C that is silently wrong here - &automatic above all (SPEC.md 73.5).
#     It prints every function's frame size on every build, deliberately: the
#     number that matters is a sum over a call chain no static analysis can
#     see, and an author who notices the figures growing is the only defence
#     (73.8). Never pass --quiet or --no-gate here.
python3 tools/cc8086.py $BUILD/chello.raw.asm -o $BUILD/chello.gen.asm

# (3) One nasm job: the shim, which %includes apps/cc/crt0.asm (via -I apps/)
#     and build/chello.gen.asm (via -I build/).
nasm -f bin -w+error -I apps/ -I $BUILD/ -I tests/chello/ \
    -o $BUILD/chello.bin tests/chello/chello.asm

# (4) and (5) - the ordinary package pipeline, which does not know this was C.
python3 tools/os88pkg.py $BUILD/chello.bin -o $BUILD/chello.o88

# Two geometries, for the same reason every shipped image is built in three
# (CLAUDE.md): 1.44MB is what QEMU gets, and 360KB is what an 86Box XT or a
# real one takes. This is the only part of the chain where the difference can
# show, and a C package that only ever gets booted at 18 sectors per track has
# not been tried on the target machine's disk.
python3 tools/os88disk.py -o $BUILD/chello.img    --size 1440 $BUILD/chello.o88
python3 tools/os88disk.py -o $BUILD/chello360.img --size 360  $BUILD/chello.o88
python3 tools/os88disk.py --verify $BUILD/chello.img
python3 tools/os88disk.py --verify $BUILD/chello360.img

ls -l $BUILD/chello.bin $BUILD/chello.o88 $BUILD/chello.img $BUILD/chello360.img
