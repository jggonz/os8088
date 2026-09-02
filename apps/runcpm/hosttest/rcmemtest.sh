#!/bin/sh
# =============================================================================
# os8088 - apps/runcpm/hosttest/rcmemtest.sh
#
# Assemble apps/runcpm/hosttest/rcmemtest.asm - which %includes the SHIPPING
# apps/runcpm/rcmem.inc (and rcz80.inc, for the register file it reads its
# segment from) - and boot it in QEMU. It prints one '.' per case, 'N' per
# negative control caught, and a summary line on the serial port, and exits
# nonzero unless every case passed. See the head of the .asm for what each
# case checks and why SS != DS is the whole point.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD
nasm -f bin -w+error -I apps/runcpm/ \
    -o $BUILD/rcmemtest.img apps/runcpm/hosttest/rcmemtest.asm
SZ=$(wc -c < $BUILD/rcmemtest.img)
[ "$SZ" -le 32768 ] || { echo "rcmemtest: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/rcmemtest.img 2>/dev/null

OUT=$BUILD/rcmemtest.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/rcmemtest.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot >/dev/null 2>&1 & echo $! > $BUILD/rcmemtest.pid )
# poll for the summary line rather than sleeping a fixed time: on a loaded
# host (another QEMU running the in-OS gate beside this one) a fixed 4 s
# would fail a mover that is fine; a harness that never prints is cut off
# at 30 s (it takes well under a second)
T=0
while [ $T -lt 30 ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"rcmem OK"*|*"FAILURES in rcmem"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill $(cat $BUILD/rcmemtest.pid) 2>/dev/null || true
cat $OUT 2>/dev/null || true
case "$(cat $OUT 2>/dev/null)" in
    *"NNT0 failures - rcmem OK"*) echo "rcmemtest: PASS"; exit 0 ;;
    *) echo "rcmemtest: FAIL (see the lines above)"; exit 1 ;;
esac
