#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
TMPBASE=${TMPDIR:-/tmp}/os88-speedybasic-writer.$$
HOSTBIN=$TMPBASE.host
RAW=$TMPBASE.raw.asm
GEN=$TMPBASE.gen.asm
trap 'rm -f "$HOSTBIN" "$RAW" "$GEN"' EXIT HUP INT TERM

cd "$ROOT"
${CC:-cc} -std=c89 -O1 -Wall -Wextra -Werror \
    -I apps/speedybasic/compiler/hosttest \
    -I apps/speedybasic/compiler \
    apps/speedybasic/compiler/hosttest/writertest.c -o "$HOSTBIN"
"$HOSTBIN"

# The host test proves the bytes. This second pass proves the module stays in
# SmallerC's dialect and passes the same mandatory 8086 gate as a package.
make --no-print-directory cc-toolchain
CCROOT=build/cc/SmallerC
PATH="$ROOT/$CCROOT:$PATH" "$CCROOT/smlrcc" -tiny -S \
    -SI "$CCROOT/v0100/include" -I "$CCROOT/v0100/include" \
    -I apps/cc -I apps/speedybasic/compiler \
    apps/speedybasic/compiler/writer.c -o "$RAW"
python3 tools/cc8086.py "$RAW" -o "$GEN" --max-frame 96 >/dev/null
echo "writer: SmallerC/8086 gate PASS"
