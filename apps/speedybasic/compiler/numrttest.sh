#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
BUILD="$ROOT/build"
HOSTBIN=${TMPDIR:-/tmp}/os88-numrttest.$$
SC="$BUILD/cc/SmallerC"
trap 'rm -f "$HOSTBIN" "$BUILD/numrttest.pid"' EXIT HUP INT TERM
mkdir -p "$BUILD"
cd "$ROOT"

${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -DSB_HOST_TEST \
    -I apps -I apps/speedybasic \
    apps/speedybasic/hosttest/sbnum_ref.c \
    apps/speedybasic/hosttest/sbmem_host.c \
    apps/speedybasic/compiler/numrt.c \
    apps/speedybasic/compiler/numrttest.c -lm -o "$HOSTBIN"
"$HOSTBIN"

test -x "$SC/smlrcc" || {
    echo "numrttest: SmallerC is required for the raw target gate" >&2
    exit 1
}
PATH="$SC:$PATH" "$SC/smlrcc" -tiny -S \
    -SI "$SC/v0100/include" -I "$SC/v0100/include" -I apps \
    apps/speedybasic/compiler/numrt_ccprobe.c \
    -o "$BUILD/numrt_ccprobe.raw.asm"
python3 tools/cc8086.py "$BUILD/numrt_ccprobe.raw.asm" \
    -o "$BUILD/numrt_ccprobe.gen.asm" --max-frame 96
nasm -f bin -w+error -I apps/ -I "$BUILD/" \
    apps/speedybasic/compiler/numrttest.asm -o "$BUILD/numrttest.img"

SIZE=$(wc -c < "$BUILD/numrttest.img" | tr -d ' ')
test "$SIZE" -le $((24 * 512)) || {
    echo "numrttest: $SIZE bytes exceeds its stage-1 load" >&2
    exit 1
}
dd if=/dev/zero bs=512 count=$((2880 - (SIZE + 511) / 512)) \
    >> "$BUILD/numrttest.img" 2>/dev/null

OUT="$BUILD/numrttest.out"
rm -f "$OUT"
( qemu-system-i386 -drive file="$BUILD/numrttest.img",format=raw,if=floppy \
    -boot a -display none -serial file:"$OUT" -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    >/dev/null 2>&1 & echo $! > "$BUILD/numrttest.pid" )
T=0
while test "$T" -lt 30; do
    case "$(cat "$OUT" 2>/dev/null)" in
        *"numrt OK"*|*"numrt FAIL"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill "$(cat "$BUILD/numrttest.pid")" 2>/dev/null || true
cat "$OUT" 2>/dev/null || true
case "$(cat "$OUT" 2>/dev/null)" in
    *"NR0 - numrt OK"*) echo "numrttest target: $SIZE bytes - PASS" ;;
    *) echo "numrttest target: FAIL" >&2; exit 1 ;;
esac
