#!/bin/sh
# =============================================================================
# os8088 - apps/loom/hosttest/lmpack.sh
#
# THE FAST HALF of WEAVE-SPEC 11.1's byte-identity gate: build LOOM's shipping
# compilers with the host's cc (apps/loom/hosttest/lmhost.c stands the two
# claims up as arrays), pack every demo and every template, and diff each
# result against `python3 tools/weavesim.py --pack` BYTE FOR BYTE. Then run
# tests/weave/packerr/ and compare the two packers' refusal SENTENCES, which
# WEAVE-SPEC 10.5 pins as tightly as it pins the bytes.
#
# IT IS THE DEV LOOP AND NOT THE GATE. `int` is 32 bits here and 16 bits on
# the machine, so this proves the logic and `weavepack` (WEAVE-SPEC 12.3)
# proves the arithmetic - see the head of lmhost.c. A wave closes on the
# second one.
#
# apps/weave/hosttest/weavevm.sh is the shape; the first-difference report is
# this file's own, because "the bundles differ" is not a finding.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."       # the repo root, whatever the caller's cwd

BUILD=build
CC=${CC:-cc}
mkdir -p $BUILD

python3 tools/weavesim.py --emit-foldtab-c > $BUILD/lmfoldc.h

$CC -std=c89 -O1 -Wall -Wno-unused-function -I $BUILD -o $BUILD/lmhost \
    apps/loom/hosttest/lmhost.c

PASS=0
FAIL=0

# --- the bundles: byte for byte ----------------------------------------------
for WML in apps/weave/demos/form.wml apps/weave/demos/sheet.wml \
           apps/weave/demos/pong.wml \
           apps/loom/templates/*/MAIN.WML; do
    [ -f "$WML" ] || continue
    NAME=$(basename "$(dirname "$WML")")/$(basename "$WML")
    python3 tools/weavesim.py --pack "$WML" -o $BUILD/lm_ref.wab
    if ! $BUILD/lmhost "$WML" $BUILD/lm_got.wab; then
        echo "X $NAME: LOOM refused a project weavesim packed"
        FAIL=$((FAIL + 1))
        continue
    fi
    if cmp -s $BUILD/lm_ref.wab $BUILD/lm_got.wab; then
        echo ". $NAME: $(wc -c < $BUILD/lm_ref.wab | tr -d ' ') bytes, identical"
        PASS=$((PASS + 1))
    else
        echo "X $NAME: the two packers disagree"
        python3 tools/lmdiff.py $BUILD/lm_ref.wab $BUILD/lm_got.wab || true
        FAIL=$((FAIL + 1))
    fi
done

# --- the refusals: sentence for sentence -------------------------------------
# One case per rule (WEAVE-SPEC 10.5). weavesim prints `<file>:<line>: <msg>`
# to stderr and exits non-zero; LOOM's harness prints lm_errtext() and does the
# same. The whole line must match, the file name included - which is why the
# corpus files are named in the 8.3 spelling the machine would use.
for DIR in tests/weave/packerr/*/; do
    [ -d "$DIR" ] || continue
    NAME=$(basename "$DIR")
    REF=$(python3 tools/weavesim.py --pack "$DIR/MAIN.WML" -o /dev/null 2>&1 >/dev/null || true)
    GOT=$($BUILD/lmhost "$DIR/MAIN.WML" /dev/null 2>&1 >/dev/null || true)
    REF=$(printf '%s' "$REF" | sed -n 's/^weavesim: //p; t; p' | tail -1)
    GOT=$(printf '%s' "$GOT" | tail -1)
    if [ -z "$REF" ]; then
        echo "X $NAME: weavesim PACKED a project the corpus says must refuse"
        FAIL=$((FAIL + 1))
    elif [ "$REF" = "$GOT" ]; then
        echo ". $NAME: $REF"
        PASS=$((PASS + 1))
    else
        echo "X $NAME: the sentences differ"
        echo "    weavesim: $REF"
        echo "    LOOM:     $GOT"
        FAIL=$((FAIL + 1))
    fi
done

echo "lmpack: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
[ "$PASS" -gt 0 ] || { echo "lmpack: nothing was compared"; exit 1; }
echo "lmpack: PASS"
