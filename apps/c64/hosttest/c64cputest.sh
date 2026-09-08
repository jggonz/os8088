#!/bin/sh
# =============================================================================
# os8088 - apps/c64/hosttest/c64cputest.sh [timeout-s]
#
# THE 6510 CORE'S GATE (docs/C64-SPEC.md §4.6) - `make c64cputest`, minutes,
# deliberately NOT in apps/c64/build.sh, exactly as rcz80test is not in
# RUNCPM's. It assembles apps/c64/hosttest/c64cputest.asm - which %includes the
# SHIPPING apps/c64/c64cpu.inc and c64mem.inc - boots it in raw QEMU with
# SS != DS and reads the serial port.
#
# THE TWELVE ROWS, and the negative control each one carries. Decision 22 in
# docs/plans/completed/C64-PORT-PLAN.md says nine; the tenth is the split below (decimal
# ADC/SBC came out of Dormann's row and became a row of its own) and rows 11
# and 12 are the fix pass's.
#
#   1  Klaus Dormann's 6502 functional test, 64KB, fetched at a pinned
#      SHA-256 and NEVER COMMITTED, run to its success trap at $3469.
#      Control: ADC # dispatched to ORA #, which must NOT reach it.
#   2  the seven bank maps            control: the map row is shifted
#   3  $00/$01 are not RAM            control: the re-bank does not take effect
#   4  a fetch ACROSS a boundary      control: the bank above it is made RAM
#   5  real I/O stub returns          control: the stub answers a constant
#   6  ES and DS come back            control: the reload is NOPped out of the
#                                              shipping text at runtime
#   7  IRQ and NMI entry              control: nothing is pending
#   8  the illegal opcodes            control: LAX abs dispatched to NOP
#   9  cycle totals and the penalties control: one opcode costs one less
#  10  decimal ADC/SBC, all 262,144   control: SED dispatched to CLD
#  11  the four unstable stores       control: SHA abs,Y dispatched to STA abs,Y
#  12  the interrupt stack and RTI    control: BRK dispatched to NOP
#
# ROW 10 IS NOT DORMANN'S DECIMAL TEST, and the reason is a fact about the
# world rather than a scope cut: that test is published as SOURCE only - the
# project's bin_files/ carries the functional test's binary and the 65C02
# one and nothing else. tools/c64dec.py is what replaces it: an independent
# implementation, in Python, from the documented NMOS rules, over the same
# 262,144 cases, handing this harness four checksums.
#
# Exit 0 only when every row passed AND every negative control failed.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
TMO=${1:-1800}
DORM_URL=https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin
DORM_SHA=fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd
DORM=$BUILD/c64tests/6502_functional_test.bin
DORMOK=0x3469                       # the `jmp *` the test ends on (its .lst)

mkdir -p $BUILD/c64tests

sha256() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1; fi
}

# --- the fixture: fetched, pinned, never committed -------------------------
if [ ! -f "$DORM" ]; then
    echo "c64cputest: fetching Klaus Dormann's functional test"
    curl -sSL --max-time 120 -o "$DORM.part" "$DORM_URL" || {
        echo "c64cputest: cannot fetch $DORM_URL"
        echo "            put the 65,536-byte binary at $DORM by hand"
        exit 1; }
    mv "$DORM.part" "$DORM"
fi
GOT=$(sha256 "$DORM")
if [ "$GOT" != "$DORM_SHA" ]; then
    echo "c64cputest: $DORM is not the pinned fixture"
    echo "            want $DORM_SHA"
    echo "            got  $GOT"
    exit 1
fi

DEC=$(python3 tools/c64dec.py)
echo "c64cputest: the decimal reference is $DEC"

run() {                             # run <image> <label> -> the guest's exit
    OUT=$BUILD/c64cputest-$2.out
    rm -f "$OUT"
    ( qemu-system-i386 -drive file="$1",format=raw,if=floppy \
        -boot a -display none -serial file:"$OUT" -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 >/dev/null 2>&1 \
        & echo $! > $BUILD/c64cputest.pid )
    QPID=$(cat $BUILD/c64cputest.pid)
    T=0
    while kill -0 $QPID 2>/dev/null; do
        sleep 1
        T=$((T + 1))
        if [ $T -ge $TMO ]; then
            kill $QPID 2>/dev/null || true
            echo "c64cputest: TIMEOUT after ${TMO}s in $2"
            cat "$OUT"
            exit 1
        fi
    done
    cat "$OUT"
}

pad() {                             # pad <image> <sectors>
    SZ=$(wc -c < "$1")
    NEED=$(( $2 * 512 ))
    [ "$SZ" -le "$NEED" ] || { echo "c64cputest: $1 is $SZ bytes, over $NEED"; exit 1; }
    dd if=/dev/zero bs=1 count=$((NEED - SZ)) >> "$1" 2>/dev/null
}

# =============================================================================
# THE ROWS (seconds)
# =============================================================================
IMG=$BUILD/c64cputest-rows.img
# shellcheck disable=SC2086
nasm -f bin -w+error -I apps/c64/ $DEC -o $IMG apps/c64/hosttest/c64cputest.asm
pad $IMG 2880
run $IMG rows > $BUILD/c64cputest-rows.log || true
cat $BUILD/c64cputest-rows.log
grep -q "c64cputest: PASS" $BUILD/c64cputest-rows.log || {
    echo "c64cputest: the rows FAILED"; exit 1; }

# =============================================================================
# ROW 1 - DORMANN, AND ITS NEGATIVE CONTROL (minutes)
# =============================================================================
for MODE in real neg; do
    IMG=$BUILD/c64cputest-dorm-$MODE.img
    if [ "$MODE" = neg ]; then NEGF="-DNEG"; else NEGF=""; fi
    # shellcheck disable=SC2086
    nasm -f bin -w+error -I apps/c64/ -DDORMANN -DDORMOK=$DORMOK $NEGF $DEC \
        -o $IMG apps/c64/hosttest/c64cputest.asm
    pad $IMG 64                     # the harness occupies sectors 0..63
    cat "$DORM" >> $IMG             # ...and the 64KB fixture follows at 64
    pad $IMG 2880
    run $IMG "dorm-$MODE" > $BUILD/c64cputest-dorm-$MODE.log || true
    cat $BUILD/c64cputest-dorm-$MODE.log
    if [ "$MODE" = real ]; then
        grep -q "c64cputest: PASS" $BUILD/c64cputest-dorm-$MODE.log || {
            echo "c64cputest: Dormann's functional test FAILED"; exit 1; }
    else
        grep -q "control fails: ok" $BUILD/c64cputest-dorm-$MODE.log || {
            echo "c64cputest: the Dormann NEGATIVE CONTROL passed - the test"
            echo "            was not being run at all"; exit 1; }
    fi
done

echo "c64cputest: PASS - twelve rows, and every negative control failed"
exit 0
