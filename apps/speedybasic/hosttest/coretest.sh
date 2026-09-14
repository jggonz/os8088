#!/bin/sh
# Run every Speedy BASIC host/native core gate from one suite entry point.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
OUT=${TMPDIR:-/tmp}/os88-speedybasic-coretest.$$
HOSTCC=${CC:-cc}
trap 'rm -f "$OUT"' EXIT HUP INT TERM

cd "$ROOT"
"$HOSTCC" -std=c99 -O1 -Wall -Wextra -Werror \
    -I apps/speedybasic -o "$OUT" apps/speedybasic/hosttest/coretest.c
"$OUT" apps/speedybasic/demos/MANIFEST.TXT apps/speedybasic/demos
apps/speedybasic/hosttest/sbmemtest.sh
apps/speedybasic/hosttest/sbnumtest.sh
