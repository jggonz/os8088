#!/bin/sh
# =============================================================================
# os8088 - apps/c64/hosttest/c64memtest.sh
#
# Assemble apps/c64/hosttest/c64memtest.asm - which %includes the SHIPPING
# apps/c64/c64mem.inc and apps/c64/c64band.inc (and c64cpu.inc, for the
# register file they read their claim segments from) - and boot it in QEMU. It
# prints one '.' per case, 'N' per negative control caught, and a summary line
# on the serial port, and exits nonzero unless every case passed. See the head
# of the .asm for what each case checks and why SS != DS is the whole point
# (C64-SPEC §3.6).
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD
nasm -f bin -w+error -I apps/c64/ \
    -o $BUILD/c64memtest.img apps/c64/hosttest/c64memtest.asm
SZ=$(wc -c < $BUILD/c64memtest.img)
[ "$SZ" -le 32768 ] || { echo "c64memtest: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/c64memtest.img 2>/dev/null

OUT=$BUILD/c64memtest.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/c64memtest.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot >/dev/null 2>&1 & echo $! > $BUILD/c64memtest.pid )
# Poll for the summary line rather than sleeping a fixed time: on a loaded
# host a fixed wait would fail a routine that is fine, and a harness that
# never prints is cut off at 30 s (it takes well under a second).
T=0
while [ $T -lt 30 ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"c64mem OK"*|*"FAILURES in c64mem"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill $(cat $BUILD/c64memtest.pid) 2>/dev/null || true
cat $OUT 2>/dev/null || true
case "$(cat $OUT 2>/dev/null)" in
    *"NNT0 failures - c64mem OK"*) echo "c64memtest: PASS"; exit 0 ;;
    *) echo "c64memtest: FAIL (see the lines above)"; exit 1 ;;
esac
