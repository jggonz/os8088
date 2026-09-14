#!/bin/sh
# Host oracle + SmallerC ABI compile + raw-QEMU execution of sbnum.inc.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
BUILD="$ROOT/build"
HOSTBIN=${TMPDIR:-/tmp}/os88-sbnumtest.$$
HOSTCC=${CC:-cc}
trap 'rm -f "$HOSTBIN" "$BUILD/sbnumtest.pid"' EXIT HUP INT TERM
mkdir -p "$BUILD"
cd "$ROOT"

"$HOSTCC" -std=c99 -O2 -Wall -Wextra -Werror -I apps/speedybasic \
    apps/speedybasic/hosttest/sbnum_ref.c \
    apps/speedybasic/hosttest/sbnumtest.c -lm -o "$HOSTBIN"
"$HOSTBIN" --emit "$BUILD/sbnum_cases.inc"

# Compile and assemble a C consumer when the pinned SmallerC instrument is
# present.  The raw machine test below remains runnable with just NASM/QEMU.
SC="$BUILD/cc/SmallerC"
if test -x "$SC/smlrcc"; then
    PATH="$SC:$PATH" "$SC/smlrcc" -tiny -S \
        -SI "$SC/v0100/include" -I "$SC/v0100/include" \
        -I apps/speedybasic \
        apps/speedybasic/hosttest/sbnum_ccprobe.c \
        -o "$BUILD/sbnum_ccprobe.raw.asm"
    python3 tools/cc8086.py "$BUILD/sbnum_ccprobe.raw.asm" \
        -o "$BUILD/sbnum_ccprobe.gen.asm"
    nasm -f bin -w+error -I apps/ -I "$BUILD/" \
        apps/speedybasic/hosttest/sbnum_cclink.asm \
        -o "$BUILD/sbnum_ccprobe.bin"
    echo "sbnum SmallerC ABI: $(wc -c < "$BUILD/sbnum_ccprobe.bin" | tr -d ' ') bytes - PASS"
fi

nasm -f bin -w+error -I apps/ -I "$BUILD/" \
    apps/speedybasic/hosttest/sbnumtest.asm -o "$BUILD/sbnumtest.img"
SIZE=$(wc -c < "$BUILD/sbnumtest.img" | tr -d ' ')
test "$SIZE" -le 16384 || {
    echo "sbnumtest: $SIZE bytes exceeds the stage-1 load" >&2
    exit 1
}
dd if=/dev/zero bs=512 count=$((2880 - (SIZE + 511) / 512)) \
    >> "$BUILD/sbnumtest.img" 2>/dev/null

OUT="$BUILD/sbnumtest.out"
rm -f "$OUT"
( qemu-system-i386 -drive file="$BUILD/sbnumtest.img",format=raw,if=floppy \
    -boot a -display none -serial file:"$OUT" -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    >/dev/null 2>&1 & echo $! > "$BUILD/sbnumtest.pid" )
T=0
while test "$T" -lt 30; do
    case "$(cat "$OUT" 2>/dev/null)" in
        *"sbnum OK"*|*"FAILURES in sbnum"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill "$(cat "$BUILD/sbnumtest.pid")" 2>/dev/null || true
cat "$OUT" 2>/dev/null || true
case "$(cat "$OUT" 2>/dev/null)" in
    *N*"T0 failures - sbnum OK"*) echo "sbnum target: $SIZE bytes, PASS" ;;
    *) echo "sbnum target: FAIL" >&2; exit 1 ;;
esac
