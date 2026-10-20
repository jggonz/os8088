#!/bin/sh
# =============================================================================
# os8088 - apps/apple2/hosttest/a2cputest.sh [timeout-s]
#
# THE 6502 CORE'S GATE (docs/APPLE2-SPEC.md section 4.4) - `make a2cputest`,
# minutes, deliberately NOT in apps/apple2/build.sh, exactly as c64cputest and
# rcz80test are not in theirs. It assembles apps/apple2/hosttest/a2cputest.asm
# - which %includes the SHIPPING apps/apple2/a2cpu.inc - boots it in raw QEMU
# with SS != DS and reads the serial port.
#
# THE TWELVE ROWS, and the negative control each one carries.
#
#   1  Klaus Dormann's 6502 functional test, 64KB, fetched at a pinned
#      SHA-256 and NEVER COMMITTED, run to its success trap at $3469.
#      Control: ADC # dispatched to ORA #, which must NOT reach it.
#   2  decimal ADC/SBC, all 262,144    control: SED dispatched to CLD
#   3  reads across every boundary     control: the io stub answers a constant
#   4  writes, and the drop above C0FF control: STA abs dispatched to STA zp
#   5  fetches across every boundary   control: one region, no boundary pair
#   6  ES and DS come back             control: the reload is NOPped out of the
#                                               shipping text at runtime
#   7  the scratch is out of reach     control: the same stale bias, which is
#                                               exactly the core that WOULD
#                                               execute the scratch page
#   8  soft-switch READS call out too  control: the stub answers a constant
#   9  the illegal opcodes             control: LAX abs -> NOP, ARR # -> AND #
#  10  cycle totals and the penalties  control: one opcode costs one less
#  11  the four unstable stores        control: SHA abs,Y -> STA abs,Y
#  12  the interrupt stack and RTI     control: BRK -> NOP
#
# ROW 2 IS NOT DORMANN'S DECIMAL TEST, and the reason is a fact about the world
# rather than a scope cut: that test is published as SOURCE only - the
# project's bin_files/ carries the functional test's binary and the 65C02 one
# and nothing else. tools/c64dec.py is what replaces it: an independent
# implementation, in Python, from the documented NMOS rules, over the same
# 262,144 cases, handing this harness four checksums. IT IS THE SAME ORACLE
# THE C64 CORE RUNS, which is APPLE2-SPEC section 4.1's whole mitigation for a
# derived copy.
#
# Exit 0 only when every row passed AND every negative control failed.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
TMO=${1:-1800}
DORM_URL=https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin
DORM_SHA=fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
DORM=$BUILD/a2tests/6502_functional_test.bin
DORMOK=0x3469                       # the `jmp *` the test ends on (its .lst)

mkdir -p $BUILD/a2tests

sha256() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1; fi
}

# --- the fixture: fetched, pinned, never committed -------------------------
# ...and SHARED WITH THE C64's gate when that one has already fetched it, which
# is the same file at the same SHA-256 and not a second copy to keep in step.
if [ ! -f "$DORM" ] && [ -f "$BUILD/c64tests/6502_functional_test.bin" ]; then
    cp "$BUILD/c64tests/6502_functional_test.bin" "$DORM"
fi
if [ ! -f "$DORM" ]; then
    echo "a2cputest: fetching Klaus Dormann's functional test"
    curl -sSL --max-time 120 -o "$DORM.part" "$DORM_URL" || {
        echo "a2cputest: cannot fetch $DORM_URL"
        echo "           put the 65,536-byte binary at $DORM by hand"
        exit 1; }
    mv "$DORM.part" "$DORM"
fi
GOT=$(sha256 "$DORM")
if [ "$GOT" != "$DORM_SHA" ]; then
    echo "a2cputest: $DORM is not the pinned fixture"
    echo "           want $DORM_SHA"
    echo "           got  $GOT"
    exit 1
fi

DEC=$(python3 tools/c64dec.py)
echo "a2cputest: the decimal reference is $DEC"

run() {                             # run <image> <label> -> the guest's exit
    OUT=$BUILD/a2cputest-$2.out
    rm -f "$OUT"
    ( qemu-system-i386 -drive file="$1",format=raw,if=floppy \
        -boot a -display none -serial file:"$OUT" -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 \
        & echo $! > $BUILD/a2cputest.pid )
    QPID=$(cat $BUILD/a2cputest.pid)
    T=0
    while kill -0 $QPID 2>/dev/null; do
        sleep 1
        T=$((T + 1))
        if [ $T -ge $TMO ]; then
            kill $QPID 2>/dev/null || true
            echo "a2cputest: TIMEOUT after ${TMO}s in $2"
            cat "$OUT"
            exit 1
        fi
    done
    cat "$OUT"
}

pad() {                             # pad <image> <sectors>
    SZ=$(wc -c < "$1")
    NEED=$(( $2 * 512 ))
    [ "$SZ" -le "$NEED" ] || { echo "a2cputest: $1 is $SZ bytes, over $NEED"; exit 1; }
    dd if=/dev/zero bs=1 count=$((NEED - SZ)) >> "$1" 2>/dev/null
}

# =============================================================================
# THE ROWS (seconds)
# =============================================================================
IMG=$BUILD/a2cputest-rows.img
# shellcheck disable=SC2086
nasm -f bin -w+error -I apps/apple2/ $DEC -o $IMG apps/apple2/hosttest/a2cputest.asm
pad $IMG 2880
run $IMG rows > $BUILD/a2cputest-rows.log || true
cat $BUILD/a2cputest-rows.log
grep -q "a2cputest: PASS" $BUILD/a2cputest-rows.log || {
    echo "a2cputest: the rows FAILED"; exit 1; }

# =============================================================================
# ROW 1 - DORMANN, AND ITS NEGATIVE CONTROL (minutes)
# =============================================================================
for MODE in real neg; do
    IMG=$BUILD/a2cputest-dorm-$MODE.img
    if [ "$MODE" = neg ]; then NEGF="-DNEG"; else NEGF=""; fi
    # shellcheck disable=SC2086
    nasm -f bin -w+error -I apps/apple2/ -DDORMANN -DDORMOK=$DORMOK $NEGF $DEC \
        -o $IMG apps/apple2/hosttest/a2cputest.asm
    pad $IMG 64                     # the harness occupies sectors 0..63
    cat "$DORM" >> $IMG             # ...and the 64KB fixture follows at 64
    pad $IMG 2880
    run $IMG "dorm-$MODE" > $BUILD/a2cputest-dorm-$MODE.log || true
    cat $BUILD/a2cputest-dorm-$MODE.log
    if [ "$MODE" = real ]; then
        grep -q "a2cputest: PASS" $BUILD/a2cputest-dorm-$MODE.log || {
            echo "a2cputest: Dormann's functional test FAILED"; exit 1; }
    else
        grep -q "control fails: ok" $BUILD/a2cputest-dorm-$MODE.log || {
            echo "a2cputest: the Dormann NEGATIVE CONTROL passed - the test"
            echo "           was not being run at all"; exit 1; }
    fi
done

echo "a2cputest: PASS - twelve rows, and every negative control failed"
exit 0
