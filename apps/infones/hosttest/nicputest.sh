#!/bin/sh
# =============================================================================
# os8088 - apps/infones/hosttest/nicputest.sh [timeout-s]
#
# THE 2A03 CORE'S GATE (SPEC.md 91.14.4) - `make nicputest`, MINUTES, and
# deliberately NOT in apps/infones/build.sh, exactly as c64cputest is not in
# C64's and rcz80test is not in RUNCPM's: it takes minutes and it FETCHES ITS
# OWN FIXTURE, and a fresh clone's `make infones` must neither stall on a
# network nor fail without one.
#
# It assembles apps/infones/hosttest/nicputest.asm - which %includes the
# SHIPPING apps/infones/nicpu.inc - boots it in raw QEMU with SS != DS, and
# reads the serial port.
#
# THE ROWS
#
#   1  nestest.nes from $C000 in automation mode, ONE INSTRUCTION AT A TIME,
#      diffed against nestest.log's 8,991 lines - PC, the opcode byte,
#      A/X/Y/P/SP and CYC - by tools/nitrace.py. The first differing line
#      names the exact instruction that is wrong.
#      NEGATIVE CONTROL: the harness is rebuilt with ADC # dispatched to the
#      ORA # handler, which must NOT match the log.
#   2  blargg's instr_test-v5 rom_singles 01-16 through the $6000 protocol:
#      run until $6000 leaves $80, check the $DE $B0 $61 signature at
#      $6001-$6003, read $6000 and print the NUL-terminated ASCII at $6004.
#      Each is MAPPER 0 at 40,976 bytes, which is the finding this port's plan
#      rests on: the whole CPU suite passes with NROM alone.
#      NEGATIVE CONTROL: one single is run again with LDA # dispatched to NOP,
#      which must fail.
#
# THE FIXTURES ARE FETCHED AND ARE NEVER COMMITTED, and they SHIP NOWHERE:
# christopherpow/nes-test-roms has no LICENSE and no permission statement in
# any readme (SPEC.md 91.14.2), so the bytes land in build/nesroms/tests/ and
# stop there - not on a floppy, not through The Wire, not in this repository.
#
# Exit 0 only when every row passed AND both negative controls failed.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
TMO=${1:-900}
DIR=$BUILD/nesroms/tests
BASE=https://raw.githubusercontent.com/christopherpow/nes-test-roms/master
mkdir -p $DIR

sha256() {
    if command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | cut -d' ' -f1
    else sha256sum "$1" | cut -d' ' -f1; fi
}

# fetch <path-under-base> <local-name> [sha256]
fetch() {
    if [ ! -f "$DIR/$2" ]; then
        echo "nicputest: fetching $2"
        curl -sSL --max-time 180 -o "$DIR/$2.part" "$BASE/$1" || {
            echo "nicputest: cannot fetch $BASE/$1"
            echo "           put the file at $DIR/$2 by hand"
            exit 1; }
        mv "$DIR/$2.part" "$DIR/$2"
    fi
    if [ -n "$3" ]; then
        GOT=$(sha256 "$DIR/$2")
        if [ "$GOT" != "$3" ]; then
            echo "nicputest: $DIR/$2 is not the pinned fixture"
            echo "           want $3"
            echo "           got  $GOT"
            exit 1
        fi
    fi
}

fetch other/nestest.nes nestest.nes \
      f67d55fd6b3cf0bad1cc85f1df0d739c65b53e79cecb7fea8f77ec0eadab0004
fetch other/nestest.log nestest.log ""

SINGLES="01-basics 02-implied 03-immediate 04-zero_page 05-zp_xy \
         06-absolute 07-abs_xy 08-ind_x 09-ind_y 10-branches 11-stack \
         12-jmp_jsr 13-rts 14-rti 15-brk 16-special"
for S in $SINGLES; do
    fetch "instr_test-v5/rom_singles/$S.nes" "$S.nes" ""
done

# --- the runner -------------------------------------------------------------
# run <image> <label> <serial-out>
run() {
    rm -f "$3"
    ( qemu-system-i386 -drive file="$1",format=raw,if=floppy \
        -boot a -display none -serial file:"$3" -no-reboot >/dev/null 2>&1 \
        & echo $! > $BUILD/nicputest.pid )
    QPID=$(cat $BUILD/nicputest.pid)
    T=0
    while kill -0 $QPID 2>/dev/null; do
        sleep 1
        T=$((T + 1))
        if [ $T -ge $TMO ]; then
            kill $QPID 2>/dev/null || true
            echo "nicputest: TIMEOUT after ${TMO}s in $2"
            exit 1
        fi
        # the guest halts when it is done; poll for the marker so a finished
        # run is not waited on for the whole timeout
        case "$(LC_ALL=C tail -c 200 "$3" 2>/dev/null | LC_ALL=C tr -d '\000' 2>/dev/null)" in
            *NICPUEND*|*"nicputest: PASS"*|*"nicputest: FAIL"*|\
            *TIMEOUT*|*SIGNATURE*) kill $QPID 2>/dev/null || true; break ;;
        esac
    done
    sleep 1
}

# build <image> <rom> <extra-nasm-flags>
build_img() {
    IMG=$1
    ROM=$2
    shift 2
    SECS=$(( ($(wc -c < "$ROM") + 511) / 512 ))
    # shellcheck disable=SC2086
    nasm -f bin -w+error -I apps/infones/ -DROMSECS=$SECS "$@" \
        -o "$IMG" apps/infones/hosttest/nicputest.asm
    SZ=$(wc -c < "$IMG")
    [ "$SZ" -le 32768 ] || { echo "nicputest: harness is $SZ bytes, over the 32KB stage 1 reads"; exit 1; }
    dd if=/dev/zero bs=1 count=$((32768 - SZ)) >> "$IMG" 2>/dev/null
    cat "$ROM" >> "$IMG"
    NOW=$(wc -c < "$IMG")
    dd if=/dev/zero bs=512 count=$(( 2880 - (NOW + 511) / 512 )) >> "$IMG" 2>/dev/null
}

FAILED=0

# =============================================================================
# ROW 1 - nestest, and its negative control
# =============================================================================
echo "nicputest: row 1 - nestest.nes against nestest.log (8,991 lines)"
build_img $BUILD/nicpu-nestest.img $DIR/nestest.nes -DNESTEST -DTRACEN=8991
run $BUILD/nicpu-nestest.img nestest $BUILD/nicpu-nestest.bin
if python3 tools/nitrace.py $BUILD/nicpu-nestest.bin $DIR/nestest.log; then
    :
else
    FAILED=1
fi

echo "nicputest: row 1's NEGATIVE CONTROL - ADC # dispatched to ORA #"
build_img $BUILD/nicpu-nesneg.img $DIR/nestest.nes -DNESTEST -DTRACEN=8991 -DNEGADC
run $BUILD/nicpu-nesneg.img nestest-neg $BUILD/nicpu-nesneg.bin
if python3 tools/nitrace.py $BUILD/nicpu-nesneg.bin $DIR/nestest.log >/dev/null 2>&1; then
    echo "nicputest: THE NEGATIVE CONTROL PASSED - the trace was not being"
    echo "           compared at all"
    FAILED=1
else
    echo "nitrace: the control fails: ok"
fi

# =============================================================================
# ROW 2 - blargg's instr_test-v5 rom_singles 01-16
# =============================================================================
echo "nicputest: row 2 - blargg instr_test-v5 rom_singles"
for S in $SINGLES; do
    build_img $BUILD/nicpu-$S.img $DIR/$S.nes
    run $BUILD/nicpu-$S.img "$S" $BUILD/nicpu-$S.out
    OUT=$(LC_ALL=C tr -d '\000' < $BUILD/nicpu-$S.out 2>/dev/null || true)
    case "$OUT" in
        *"nicputest: PASS"*) printf "  %-16s ok\n" "$S" ;;
        *) printf "  %-16s FAILED\n" "$S"
           echo "$OUT" | sed 's/^/      /'
           FAILED=1 ;;
    esac
done

echo "nicputest: row 2's NEGATIVE CONTROL - LDA # dispatched to NOP"
build_img $BUILD/nicpu-negsingle.img $DIR/01-basics.nes -DNEGLDA
run $BUILD/nicpu-negsingle.img neg-single $BUILD/nicpu-negsingle.out
OUT=$(LC_ALL=C tr -d '\000' < $BUILD/nicpu-negsingle.out 2>/dev/null || true)
case "$OUT" in
    *"nicputest: PASS"*)
        echo "nicputest: THE NEGATIVE CONTROL PASSED - 01-basics does not"
        echo "           depend on LDA #, so this row proves nothing"
        FAILED=1 ;;
    *) echo "  control fails: ok" ;;
esac

if [ $FAILED -ne 0 ]; then
    echo "nicputest: FAIL"
    exit 1
fi
echo "nicputest: PASS - nestest's 8,991 lines, sixteen blargg singles, and"
echo "           both negative controls failed as they must"
exit 0
