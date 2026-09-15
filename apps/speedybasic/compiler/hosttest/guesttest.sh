#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../../.." && pwd)
TMPBASE=${TMPDIR:-/tmp}/os88-speedybasic-guest.$$
HOSTBIN=$TMPBASE.host
trap 'rm -f "$HOSTBIN" "$TMPBASE".*.asm' EXIT HUP INT TERM

cd "$ROOT"
make --no-print-directory build/SPEEDYCC.RT
${CC:-cc} -std=c89 -O1 -Wall -Wextra -Werror \
    -I apps/speedybasic/compiler/hosttest \
    -I apps/speedybasic/compiler \
    apps/speedybasic/compiler/hosttest/guesttest.c -o "$HOSTBIN"
"$HOSTBIN" build/SPEEDYCC.RT

# Keep all guest compiler modules within SmallerC's dialect and run the same
# 8086 instruction/frame gate used by shipping application objects.
make --no-print-directory cc-toolchain
CCROOT=build/cc/SmallerC
for module in writer aot guest; do
    PATH="$ROOT/$CCROOT:$PATH" "$CCROOT/smlrcc" -tiny -S \
        -SI "$CCROOT/v0100/include" -I "$CCROOT/v0100/include" \
        -I apps/cc -I apps/speedybasic/compiler \
        "apps/speedybasic/compiler/$module.c" -o "$TMPBASE.$module.raw.asm"
    python3 tools/cc8086.py "$TMPBASE.$module.raw.asm" \
        -o "$TMPBASE.$module.gen.asm" --max-frame 96 >/dev/null
done
echo "guest: SmallerC/8086 gates PASS"
