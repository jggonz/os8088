#!/bin/sh
# =============================================================================
# os8088 - apps/weave/hosttest/weavevm.sh
#
# Generate the WVM differential corpus with tools/weavesim.py, assemble
# apps/weave/hosttest/weavevm.asm - which %includes the SHIPPING
# apps/weave/wvm.inc and apps/weave/wfx.inc - and boot it in raw QEMU. It
# prints one '.' per case passed, 'X' per case failed, 'N' per negative control correctly caught and
# 'x' per control that slipped through, then a summary line on COM1, and exits
# nonzero unless every case agreed with the model. See the head of the .asm
# for what each row checks and why SS != DS is the whole point.
#
# apps/c64/hosttest/c64memtest.sh is the shape; the poll-for-the-summary
# discipline is its, and its comment says why a fixed sleep is wrong.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
TMO=${1:-120}
mkdir -p $BUILD

# Both corpora are GENERATED, never committed: the expected states and values
# are the model's, so a change to tools/weavesim.py must move them
# (WEAVE-SPEC 12.1.1, 12.1.2).
python3 tools/weavesim.py --emit-optab > $BUILD/wvmtab.inc
python3 tools/weavesim.py --emit-vmcorpus tests/weave/vmcorpus -o $BUILD/weavevmcorp.inc
python3 tools/weavesim.py --emit-fxcorpus tests/weave/fxcorpus -o $BUILD/weavefxcorp.inc

nasm -f bin -w+error -I apps/weave/ -I $BUILD/ \
    -o $BUILD/weavevm.img apps/weave/hosttest/weavevm.asm
SZ=$(wc -c < $BUILD/weavevm.img)
[ "$SZ" -le 32768 ] || { echo "weavevm: image is $SZ bytes, over the 32KB the loader reads"; exit 1; }
# pad to a full 1.44MB: QEMU takes the floppy's GEOMETRY from the image size,
# and the boot sector's CHS walk assumes 18 sectors a track and two heads
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/weavevm.img 2>/dev/null

OUT=$BUILD/weavevm.out
rm -f $OUT
( qemu-system-i386 -drive file=$BUILD/weavevm.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -no-reboot \
    -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 & echo $! > $BUILD/weavevm.pid )
# Poll for the summary line rather than sleeping a fixed time: on a loaded
# host a fixed wait would fail a core that is fine, and a harness that never
# prints is cut off by the timeout (it takes a second or two).
T=0
while [ $T -lt "$TMO" ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"weavevm OK"*|*"FAILURES in weavevm"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill "$(cat $BUILD/weavevm.pid)" 2>/dev/null || true
cat $OUT 2>/dev/null || true
echo "weavevm: ${T}s"
case "$(cat $OUT 2>/dev/null)" in
    *"T0 failures - weavevm OK"*)
        # ...and the negative controls must have FIRED. A run with no N at all
        # is a run whose comparison was never exercised (c64cputest's rule).
        case "$(cat $OUT 2>/dev/null)" in
            *N*) echo "weavevm: PASS"; exit 0 ;;
            *) echo "weavevm: FAIL - no negative control fired, so the"
               echo "         comparison proves nothing"; exit 1 ;;
        esac ;;
    *) echo "weavevm: FAIL (see the lines above)"; exit 1 ;;
esac
