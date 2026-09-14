#!/bin/sh
# Host semantics plus actual shipping sbmem.inc on an 8086 with SS != DS.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
BUILD="$ROOT/build"
HOSTBIN=${TMPDIR:-/tmp}/os88-sbmemtest.$$
HOSTCC=${CC:-cc}
trap 'rm -f "$HOSTBIN" "$BUILD/sbmemtest.pid"' EXIT HUP INT TERM
mkdir -p "$BUILD"
cd "$ROOT"

"$HOSTCC" -std=c89 -O1 -Wall -Wextra -Werror -I apps/speedybasic \
    -o "$HOSTBIN" apps/speedybasic/hosttest/sbmemtest.c
"$HOSTBIN"

nasm -f bin -w+error -I apps/ \
    -o "$BUILD/sbmemtest.img" apps/speedybasic/hosttest/sbmemtest.asm
SIZE=$(wc -c < "$BUILD/sbmemtest.img" | tr -d ' ')
test "$SIZE" -le 8192 || {
    echo "sbmemtest: $SIZE bytes exceeds the stage-1 load" >&2
    exit 1
}
dd if=/dev/zero bs=512 count=$((2880 - (SIZE + 511) / 512)) \
    >> "$BUILD/sbmemtest.img" 2>/dev/null

OUT="$BUILD/sbmemtest.out"
rm -f "$OUT"
( qemu-system-i386 -drive file="$BUILD/sbmemtest.img",format=raw,if=floppy \
    -boot a -display none -serial file:"$OUT" -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
    >/dev/null 2>&1 & echo $! > "$BUILD/sbmemtest.pid" )
T=0
while test "$T" -lt 30; do
    case "$(cat "$OUT" 2>/dev/null)" in
        *"sbmem OK"*|*"FAILURES in sbmem"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill "$(cat "$BUILD/sbmemtest.pid")" 2>/dev/null || true
cat "$OUT" 2>/dev/null || true
case "$(cat "$OUT" 2>/dev/null)" in
    *"NT0 failures - sbmem OK"*) echo "sbmem target: $SIZE bytes, PASS" ;;
    *) echo "sbmem target: FAIL" >&2; exit 1 ;;
esac
