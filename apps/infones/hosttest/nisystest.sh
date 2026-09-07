#!/bin/sh
# =============================================================================
# os8088 - apps/infones/hosttest/nisystest.sh
#
# THE WHOLE-EMULATOR ROM GATE (SPEC.md 91.14.4) - `make nisystest [NIROM=...]`.
#
# It boots os8088 under QEMU with a floppy carrying `build/nitest.o88` - THE
# SHIPPING PACKAGE compiled a second time with -DNITEST, through the same shim
# and the same four .inc files - beside one ROM named TEST.NES. That build
# loads the ROM on its first wake, enters the bracket, runs frames back to
# back and polls blargg's $6000 channel; when the ROM answers it writes
# `NIRES.TXT` beside itself and this script reads it back OUT OF THE IMAGE.
#
# WHY IT IS A SECOND HARNESS AND NOT A ROW OF nicputest: `make nicputest` is
# NASM-ONLY. It %includes apps/infones/nicpu.inc and nothing else, because
# nippu.c, nirun.c and nimap.c are COMPILED C and cannot be in a boot-sector
# image at all - so ppu_vbl_nmi, a test of exactly the code that harness
# excludes, has nowhere to run in it.
#
# IT ALSO CARRIES SPEC.md 91.4.4's STACK HIGH-WATER. The bracket lays a
# pattern below SP before the run and reads the deepest scrub after it, on
# task 0's real 512-byte stack, under the kernel's real frames, with
# interrupts landing on top - after a run that has exercised a mapper write, a
# PPU read, an OAM DMA, a reset, a key poll and a present.
#
# NOT in apps/infones/build.sh: it boots the OS and drives it, which is
# minutes rather than seconds.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."

BUILD=build
ROM=${1:-$BUILD/nesroms/tests/01-basics.nes}
[ -f "$ROM" ] || { echo "nisystest: no such ROM: $ROM"; exit 1; }

# A SCRATCH B: IMAGE, because the package WRITES to it (LESSONS.md 10). The
# ROM is copied to TEST.NES first: os88disk.py takes the 8.3 name from the
# basename, and the NITEST build looks for exactly one name.
cp "$ROM" $BUILD/TEST.NES
python3 tools/os88disk.py -o $BUILD/nisystest.img --size 1440 \
    NITEST:$BUILD/nitest.o88 NITEST:$BUILD/NITEST.OVL NITEST:$BUILD/TEST.NES
python3 tools/os88disk.py --verify $BUILD/nisystest.img

python3 tools/qmp.py $BUILD/qmp.sock quit >/dev/null 2>&1 || true
sleep 1
rm -f $BUILD/qmp.sock $BUILD/qemu.pid
make test TESTAPPS=$BUILD/nisystest.img >/dev/null
sleep 14

python3 apps/infones/hosttest/nisysdrive.py || {
    echo "nisystest: could not drive the desktop to the package"
    python3 tools/qmp.py $BUILD/qmp.sock quit >/dev/null 2>&1 || true
    exit 1
}
python3 tools/qmp.py $BUILD/qmp.sock quit >/dev/null 2>&1 || true
sleep 1

OUT=$(python3 tools/nifatcat.py $BUILD/nisystest.img NITEST NIRES.TXT 2>/dev/null || true)
if [ -z "$OUT" ]; then
    echo "nisystest: the package wrote no NIRES.TXT - it never reached the"
    echo "           end of the run. build/port-shots/nisystest.png is where"
    echo "           it got to."
    exit 1
fi
echo "$OUT"
case "$OUT" in
    *"NITEST PASS"*) echo "nisystest: PASS ($ROM)"; exit 0 ;;
    *) echo "nisystest: FAIL ($ROM)"; exit 1 ;;
esac
