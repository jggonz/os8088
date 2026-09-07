#!/bin/sh
# =============================================================================
# os8088 - apps/apple2/hosttest/a2memtest.sh
#
# Assemble apps/apple2/hosttest/a2memtest.asm - which %includes the SHIPPING
# apps/apple2/a2mem.inc and apps/apple2/a2band.inc (and a2cpu.inc, for the
# register file they read their claim segments from) - and boot it in QEMU. It
# prints one '.' per case, 'N' per negative control caught, and a summary line
# on the serial port, and exits nonzero unless every case passed. See the head
# of the .asm for what each case checks and why SS != DS is the whole point
# (docs/APPLE2-SPEC.md section 3.4).
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD
nasm -f bin -w+error -I apps/apple2/ \
    -o $BUILD/a2memtest.img apps/apple2/hosttest/a2memtest.asm
SZ=$(wc -c < $BUILD/a2memtest.img)
[ "$SZ" -le 32768 ] || { echo "a2memtest: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/a2memtest.img 2>/dev/null

OUT=$BUILD/a2memtest.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/a2memtest.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot >/dev/null 2>&1 & echo $! > $BUILD/a2memtest.pid )
# Poll for the summary line rather than sleeping a fixed time: on a loaded
# host a fixed wait would fail a routine that is fine, and a harness that
# never prints is cut off at 30 s (it takes well under a second).
T=0
while [ $T -lt 30 ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"a2mem OK"*|*"FAILURES in a2mem"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill $(cat $BUILD/a2memtest.pid) 2>/dev/null || true
cat $OUT 2>/dev/null || true
case "$(cat $OUT 2>/dev/null)" in
    *"NNNN"*"0 failures - a2mem OK"*) echo "a2memtest: PASS"; exit 0 ;;
    *) echo "a2memtest: FAIL (see the lines above)"; exit 1 ;;
esac
