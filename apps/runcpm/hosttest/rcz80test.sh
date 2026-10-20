#!/bin/sh
# =============================================================================
# os8088 - apps/runcpm/hosttest/rcz80test.sh [PROGRAM.COM] [timeout-s]
#
# Assemble apps/runcpm/hosttest/rcz80test.asm - which %includes the SHIPPING
# apps/runcpm/rcz80.inc and rcmem.inc - with PROGRAM.COM appended (default:
# ZEXDOC.COM out of the fetched master disk, tools/getruncpm.py), boot it in
# raw QEMU and print what the Z80 program wrote to BDOS 2/9 on the serial
# port. Exit 0 when the program ended by BOOT/WBOOT/BDOS 0 and no line said
# ERROR, 1 otherwise. This is the fast gate on the core; tests/rczex.py is
# the same oracle run inside os8088 (docs/plans/completed/RUNCPM-PORT-PLAN.md wave 2).
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
PROG=${1:-$BUILD/runcpm-disk/A/0/ZEXDOC.COM}
TMO=${2:-1800}
mkdir -p $BUILD
[ -f "$PROG" ] || { echo "rcz80test: no $PROG (make runcpm-src fetches the master disk)"; exit 1; }

nasm -f bin -w+error -I apps/runcpm/ -DZPROG="\"$PROG\"" \
    -o $BUILD/rcz80test.img apps/runcpm/hosttest/rcz80test.asm
SZ=$(wc -c < $BUILD/rcz80test.img)
[ "$SZ" -le 32768 ] || { echo "rcz80test: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
# pad to a full 1.44MB: QEMU takes the floppy's GEOMETRY from the image size,
# and the boot sector's CHS walk assumes 18 sectors a track and two heads
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/rcz80test.img 2>/dev/null

OUT=$BUILD/rcz80test.out
rm -f $OUT
# the guest exits QEMU itself through isa-debug-exit: code 1 = ended, 3 =
# HALT, 5 = discipline; a run that never ends is cut off by the timeout
( qemu-system-i386 -drive file=$BUILD/rcz80test.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 & echo $! > $BUILD/rcz80test.pid )
QPID=$(cat $BUILD/rcz80test.pid)
T=0
while kill -0 $QPID 2>/dev/null; do
    sleep 1
    T=$((T + 1))
    if [ $T -ge $TMO ]; then kill $QPID 2>/dev/null || true; echo "rcz80test: TIMEOUT after ${TMO}s"; cat $OUT; exit 1; fi
done
cat $OUT
echo "rcz80test: ${T}s"
if grep -q "program ended" $OUT && ! grep -q "ERROR" $OUT && ! grep -q "FAIL" $OUT; then
    echo "rcz80test: PASS"; exit 0
fi
echo "rcz80test: FAIL"; exit 1
