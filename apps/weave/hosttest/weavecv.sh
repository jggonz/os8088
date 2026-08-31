#!/bin/sh
# =============================================================================
# os8088 - apps/weave/hosttest/weavecv.sh
#
# Generate the canvas differential corpus with tools/weavesim.py, assemble
# apps/weave/hosttest/weavecv.asm - which %includes the SHIPPING
# apps/weave/wspr.inc, apps/weave/wwork.inc and apps/weave/wsmdata.inc - and
# boot it in raw QEMU. It prints one '.' per case passed, 'X' per case failed,
# 'N' per negative control correctly caught and 'x' per control that slipped
# through, then a summary line on COM1, and exits nonzero unless every case
# agreed with the model.
#
# apps/weave/hosttest/weavevm.sh is the shape, and the poll-for-the-summary
# discipline is its: a fixed sleep fails a core that is fine on a loaded host.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
TMO=${1:-120}
mkdir -p $BUILD

# GENERATED, never committed: the expected buffers, records and band runs are
# the model's, so a change to tools/weavesim.py must move them
# (WEAVE-SPEC 12.1.3).
python3 tools/weavesim.py --emit-cvcorpus -o $BUILD/weavecvcorp.inc

nasm -f bin -w+error -I apps/weave/ -I apps/ -I $BUILD/ \
    -o $BUILD/weavecv.img apps/weave/hosttest/weavecv.asm
SZ=$(wc -c < $BUILD/weavecv.img)
[ "$SZ" -le 32768 ] || { echo "weavecv: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/weavecv.img 2>/dev/null

OUT=$BUILD/weavecv.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/weavecv.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 & echo $! > $BUILD/weavecv.pid )
T=0
while [ $T -lt "$TMO" ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"weavecv OK"*|*"FAILURES in weavecv"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill "$(cat $BUILD/weavecv.pid)" 2>/dev/null || true
cat $OUT 2>/dev/null || true
echo "weavecv: ${T}s"
case "$(cat $OUT 2>/dev/null)" in
    *"T0 failures - weavecv OK"*)
        # ...and the negative controls must have FIRED. A run with no N at all
        # is a run whose comparison was never exercised (c64cputest's rule,
        # and weavevm's).
        case "$(cat $OUT 2>/dev/null)" in
            *N*) echo "weavecv: PASS"; exit 0 ;;
            *) echo "weavecv: FAIL - no negative control fired, so the"
               echo "         comparison proves nothing"; exit 1 ;;
        esac ;;
    *) echo "weavecv: FAIL (see the lines above)"; exit 1 ;;
esac
