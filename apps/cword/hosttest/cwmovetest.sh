#!/bin/sh
# =============================================================================
# os8088 - apps/cword/hosttest/cwmovetest.sh
#
# Assemble apps/cword/hosttest/cwmovetest.asm - which %includes the SHIPPING
# apps/cword/cwmove.inc - and boot it in QEMU. It prints one '.' per case and
# a summary line on the serial port, and exits nonzero unless every case
# passed.
#
# This is the only thing in the tree that executes cw_memmove: the host
# harness substitutes the C library's memmove(), and tools/cc8086.py never
# sees hand-written assembly. See the head of the .asm for what each case
# checks and why SS != DS is the whole point.
# =============================================================================
set -e

cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
mkdir -p $BUILD

nasm -f bin -w+error -I apps/cword/ \
    -o $BUILD/cwmovetest.img apps/cword/hosttest/cwmovetest.asm

OUT=$(qemu-system-i386 -drive file=$BUILD/cwmovetest.img,format=raw,if=floppy \
        -boot a -display none -serial stdio -no-reboot \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 2>/dev/null &
      QPID=$!
      # The test halts rather than exiting, so give it a second and take it down.
      sleep 3
      kill $QPID 2>/dev/null || true
      wait $QPID 2>/dev/null || true) || true

echo "$OUT"
case "$OUT" in
    *"T0 failures - cw_memmove OK"*) echo "cwmovetest: PASS"; exit 0 ;;
    *) echo "cwmovetest: FAIL (see the line above)"; exit 1 ;;
esac
