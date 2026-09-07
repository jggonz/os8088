#!/bin/sh
# =============================================================================
# os8088 - apps/infones/hosttest/nimemtest.sh
#
# Assemble apps/infones/hosttest/nimemtest.asm - which %includes the SHIPPING
# apps/infones/nimem.inc and niband.inc (and nicpu.inc, for the register file
# they read the machine claim's segment out of) - and boot it in raw QEMU. It
# prints one '.' per case, 'N' per negative control CAUGHT, and a summary line
# on the serial port, and exits nonzero unless every case passed and both
# controls were caught. The head of the .asm says what each case checks and
# why SS != DS is the whole point (SPEC.md 91.14.4).
#
# IT IS IN build.sh, because it is SECONDS. `make nicputest` is the one that
# is not: that gate is minutes and fetches a fixture besides.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD
nasm -f bin -w+error -I apps/infones/ \
    -o $BUILD/nimemtest.img apps/infones/hosttest/nimemtest.asm
SZ=$(wc -c < $BUILD/nimemtest.img)
[ "$SZ" -le 32768 ] || { echo "nimemtest: image is $SZ bytes, over the 32KB stage 1 reads"; exit 1; }
dd if=/dev/zero bs=512 count=$((2880 - (SZ + 511) / 512)) >> $BUILD/nimemtest.img 2>/dev/null

OUT=$BUILD/nimemtest.out
FRM=$BUILD/nirefA.frm
rm -f $OUT $FRM
# TWO SERIAL PORTS. COM1 carries the case reports, which are text, and COM2
# the 61,440 raw bytes of the composed frame - the SHIPPING ASSEMBLY's frame,
# which tools/niref.py checks against its own render of the same fixture
# (SPEC.md 91.14.4). One port would need an escape and a de-escaper for a
# picture that contains every byte value there is.
( qemu-system-i386 -drive file=$BUILD/nimemtest.img,format=raw,if=floppy \
    -boot a -display none -serial file:$OUT -serial file:$FRM -no-reboot >/dev/null 2>&1 & echo $! > $BUILD/nimemtest.pid )
# Poll for the summary rather than sleeping a fixed time: on a loaded host a
# fixed wait fails a routine that is fine, and a harness that never prints is
# cut off at 30 s (it takes well under a second).
T=0
while [ $T -lt 30 ]; do
    case "$(cat $OUT 2>/dev/null)" in
        *"nimem OK"*|*"FAILURES in nimem"*) break ;;
    esac
    sleep 1
    T=$((T + 1))
done
kill $(cat $BUILD/nimemtest.pid) 2>/dev/null || true
cat $OUT 2>/dev/null || true
case "$(cat $OUT 2>/dev/null)" in
    *"NN"*"0 failures - nimem OK"*) ;;
    *) echo "nimemtest: FAIL (see the lines above)"; exit 1 ;;
esac

# ...and the frame the shipping composer wrote, against tools/niref.py's own
# render of the same fixture. A short file means the run died part way through
# and is a failure in itself.
SZ=$(wc -c < $FRM 2>/dev/null || echo 0)
if [ "$SZ" -ne 61440 ]; then
    echo "nimemtest: the frame dump is $SZ bytes, want 61440"
    exit 1
fi
python3 tools/niref.py --synth $BUILD/nirefA.state
python3 tools/niref.py --check $BUILD/nirefA.state $FRM || exit 1
echo "nimemtest: PASS"
exit 0
