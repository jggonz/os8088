#!/bin/sh
# =============================================================================
# os8088 - apps/paccman/hosttest/pmcbandtest.sh
#
# Assemble apps/paccman/hosttest/pmcbandtest.asm - which %includes the
# SHIPPING apps/paccman/pmcband.inc and the vectors pmcuitest.c just wrote -
# and boot it in QEMU. It prints one '.' per case, 'N' per negative control
# caught, and a summary line on the serial port, and exits nonzero unless
# every case passed. See the head of the .asm for what each case checks and
# why SS != DS is the whole point (SPEC.md 91, 73.5.1).
#
# It is SKIPPED with a note where qemu is not installed, the way the C
# toolchain's own guard is: a contributor without an emulator still builds.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD

if ! command -v qemu-system-i386 >/dev/null 2>&1; then
    echo "pmcbandtest: SKIP - qemu-system-i386 is not installed, so"
    echo "             pmcband.inc's SS != DS gate did not run. It is the"
    echo "             only check of the one file tools/cc8086.py never sees."
    exit 0
fi
if [ ! -f $BUILD/pmcbandvec.inc ]; then
    echo "pmcbandtest: build/pmcbandvec.inc is missing - pmcuitest writes it."
    exit 1
fi

nasm -f bin -w+error -I apps/ -I $BUILD/ \
    -o $BUILD/pmcbandtest.img apps/paccman/hosttest/pmcbandtest.asm
SZ=$(wc -c < $BUILD/pmcbandtest.img)
[ "$SZ" -le 32768 ] || { echo "pmcbandtest: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/pmcbandtest.img 2>/dev/null

OUT=$BUILD/pmcbandtest.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/pmcbandtest.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot >/dev/null 2>&1 & echo $! > $BUILD/pmcbandtest.pid )
# Poll for the summary rather than sleeping a fixed time: on a loaded host a
# fixed wait fails a routine that is fine, and a harness that never prints is
# cut off at 30 s (it takes well under a second).
T=0
while [ $T -lt 30 ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"pmcband OK"*|*"FAILURES in pmcband"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill $(cat $BUILD/pmcbandtest.pid) 2>/dev/null || true
cat $OUT 2>/dev/null || true
case "$(cat $OUT 2>/dev/null)" in
    *"pmcband: 0 failures - pmcband OK"*) echo "pmcbandtest: PASS"; exit 0 ;;
    *) echo "pmcbandtest: FAIL (see the lines above)"; exit 1 ;;
esac
