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
# THE FAST CORE'S LEG (C64-SPEC section 4.7): APPLE2's a2cputest one machine
# along - the same rows bar row 4 (a fetch falling through a region boundary,
# the fast core's stated deviation) and row 6 (the accurate core's fetch ES),
# and Dormann. C64CPU_ONLY=fast runs this leg alone; =plain the others alone.
# =============================================================================
FASTBIN=$BUILD/c64/c64fast.bin
fast_leg() {
    [ -f "$FASTBIN" ] || { echo "c64cputest: $FASTBIN is missing - make c64"; exit 1; }
    [ "$(wc -c < "$FASTBIN")" -eq 65024 ] || { echo "c64cputest: $FASTBIN is not 65,024 bytes"; exit 1; }
    for MODE in rows real neg; do
        IMG=$BUILD/c64cputest-fast-$MODE.img
        case $MODE in
            rows) FL="-DFAST" ;;
            real) FL="-DFAST -DDORMANN -DDORMOK=$DORMOK" ;;
            neg)  FL="-DFAST -DDORMANN -DDORMOK=$DORMOK -DNEG" ;;
        esac
        # shellcheck disable=SC2086
        nasm -f bin -w+error -I apps/c64/ $FL $DEC -o $IMG apps/c64/hosttest/c64cputest.asm
        pad $IMG 64
        cat "$DORM" >> $IMG
        cat "$FASTBIN" >> $IMG
        pad $IMG 2880
        run $IMG "fast-$MODE" > $BUILD/c64cputest-fast-$MODE.log || true
        cat $BUILD/c64cputest-fast-$MODE.log
        case $MODE in
            rows|real) grep -q "c64cputest: PASS" $BUILD/c64cputest-fast-$MODE.log || {
                echo "c64cputest: the FAST CORE's $MODE leg FAILED"; exit 1; } ;;
            neg) grep -q "control fails: ok" $BUILD/c64cputest-fast-$MODE.log || {
                echo "c64cputest: the fast core's Dormann NEGATIVE CONTROL passed"; exit 1; } ;;
        esac
    done
}
# =============================================================================
# THE TRANSLATED LEGS (C64-SPEC section 4.8): APPLE2's a2cputest one machine
# along - Dormann's code translated ahead of time by apps/m6502/m65aot.py, in
# four parts, each run with a core built for its part's range (m65gen.py
# --aot-range) and the rest on the core (C64AOT_RANGES overrides). Seeds are tools/py6502.py's
# trace of the same test. The control breaks every translated ADC and CMP
# in the translation, and must fail. C64CPU_ONLY=aot runs this leg alone.
# =============================================================================
aot_leg() {
    mkdir -p $BUILD/c64
    python3 tools/py6502.py --dormann "$DORM" --trace-out $BUILD/c64/dorm.seeds >/dev/null
    for R in ${C64AOT_RANGES:-0400:1400 1400:2000 2000:2A00 2A00:4000}; do
        N=${R%%:*}
        python3 apps/m6502/m65aot.py --machine c64 --rom "$DORM" --range $R \
            --seeds $BUILD/c64/dorm.seeds \
            -o $BUILD/c64/dormaot_$N.asm --table $BUILD/c64/dormaotpc_$N.bin
        python3 apps/m6502/m65gen.py --machine c64 --aot-range $R -o $BUILD/c64/c64fast_dt$N.asm
        nasm -f bin -w+error -o $BUILD/c64/c64fast_dt$N.bin $BUILD/c64/c64fast_dt$N.asm
        CTL=0x$(awk '$3 == "ctl_list" {print $1}' $BUILD/c64/dormaot_$N.map)
        for MODE in real neg; do
            IMG=$BUILD/c64cputest-aot$N-$MODE.img
            FL="-DFAST -DAOT -DCTL_LIST=$CTL -DDORMANN -DDORMOK=$DORMOK"
            [ $MODE = neg ] && FL="$FL -DNEG"
            # shellcheck disable=SC2086
            nasm -f bin -w+error -I apps/c64/ $FL $DEC -o $IMG apps/c64/hosttest/c64cputest.asm
            pad $IMG 64
            cat "$DORM" >> $IMG
            cat $BUILD/c64/c64fast_dt$N.bin >> $IMG
            cat $BUILD/c64/dormaot_$N.bin >> $IMG
            pad $IMG $((64 + 128 + 127 + 127))
            cat $BUILD/c64/dormaotpc_$N.bin >> $IMG
            pad $IMG 2880
            run $IMG "aot$N-$MODE" > $BUILD/c64cputest-aot$N-$MODE.log || true
            cat $BUILD/c64cputest-aot$N-$MODE.log
            if [ $MODE = real ]; then
                grep -q "c64cputest: PASS" $BUILD/c64cputest-aot$N-$MODE.log || {
                    echo "c64cputest: Dormann through the translation of $R FAILED"; exit 1; }
            else
                grep -q "control fails: ok" $BUILD/c64cputest-aot$N-$MODE.log || {
                    echo "c64cputest: the translated leg's control passed ($R)"; exit 1; }
            fi
        done
    done
}
if [ "${C64CPU_ONLY:-}" = aot ]; then
    aot_leg
    echo "c64cputest: PASS - the translated legs"
    exit 0
fi
if [ "${C64CPU_ONLY:-}" = fast ]; then
    fast_leg
    echo "c64cputest: PASS - the fast core's leg"
    exit 0
fi

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

if [ "${C64CPU_ONLY:-}" = plain ]; then
    echo "c64cputest: PASS - thirteen rows, and every negative control failed (the accurate core alone)"
else
    fast_leg
    echo "c64cputest: PASS - thirteen rows, and every negative control failed, on both cores"
fi
exit 0
