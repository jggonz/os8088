#!/bin/sh
# Build libpmhost: a PicoMEM card running its OWN firmware, for the host
# (docs/plans/completed/PICOMEM-EMU-PLAN.md).
#
# Fetches the firmware at the pin in UPSTREAM, applies patches/fw/ (our
# firmware: pm1x-pr, what firmware/picomem1/ ships) and then patches/host/
# (the few PMHOST-only edits the host build needs), and builds shim/ against
# the result into $BUILD/<board>/libpmhost-<board>.so. Nothing here needs the
# Pico SDK, an ARM toolchain or the firmware's submodules: the seam is drawn
# above everything that would.
#
#   tools/picomem/build.sh            # v1 and v2 (PicoMEM 1.x and 2.x)
#   BOARDS=v2 tools/picomem/build.sh  # one of them
#   python3 tools/picomem/pmhost.py   # the self-check, against the result
#
# Pinned for MartyPC's reason (tools/martypc/build.sh): an instrument that
# changes under you is one more variable.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
BUILD=${BUILD:-$ROOT/build/picomem}
SRC=$BUILD/src
BOARDS=${BOARDS:-v1 v2}

val() { sed -n "s/^$1=//p" "$HERE/UPSTREAM"; }
REPO=$(val repo); PIN=$(val commit)

for t in git cmake make g++ gcc; do
    command -v "$t" >/dev/null 2>&1 || { echo "build.sh: needs $t" >&2; exit 1; }
done

# --- source, at the pin --------------------------------------------------------
mkdir -p "$SRC"
[ -d "$SRC/.git" ] || git -C "$SRC" init --quiet
git -C "$SRC" remote add origin "$REPO" 2>/dev/null || git -C "$SRC" remote set-url origin "$REPO"
if ! git -C "$SRC" cat-file -e "$PIN^{commit}" 2>/dev/null; then
    echo "==> fetching $REPO @ $PIN"
    GIT_LFS_SKIP_SMUDGE=1 git -C "$SRC" fetch --quiet --depth 1 origin "$PIN"
fi

# --- our changes, in order: the firmware's, then the host build's ------------
# The tree is reset to the pin every time, so a patch edited here is a patch
# that takes effect, and an edit made in $SRC by hand is one that does not.
echo "==> applying patches"
GIT_LFS_SKIP_SMUDGE=1 git -C "$SRC" checkout --quiet --force --detach "$PIN"
git -C "$SRC" clean --quiet -fdx
for p in "$HERE"/patches/fw/*.patch "$HERE"/patches/host/*.patch; do
    [ -e "$p" ] || continue
    echo "    ${p#$HERE/patches/}"
    git -C "$SRC" apply --whitespace=nowarn "$p"
done

# --- build ---------------------------------------------------------------------
for b in $BOARDS; do
    echo "==> building libpmhost-$b"
    mkdir -p "$BUILD/$b"
    cmake -S "$HERE/shim" -B "$BUILD/$b" -DFW="$SRC/src" -DBOARD="$b" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo >"$BUILD/$b/cmake.log" 2>&1 \
        || { tail -20 "$BUILD/$b/cmake.log" >&2; exit 1; }
    make -C "$BUILD/$b" -j"$(nproc 2>/dev/null || echo 4)" >"$BUILD/$b/make.log" 2>&1 \
        || { grep -B2 -A6 -E "error|Error" "$BUILD/$b/make.log" | head -40 >&2; exit 1; }
    echo "    $BUILD/$b/libpmhost-$b.so"
done

# --- the microSD card every vm/ PicoMEM profile mounts -------------------------
# An EMPTY card and not none: with no card the firmware never counts the PC's
# floppies, and the BIOS clears them from the equipment word (the plan's 6.3).
# The NE2000 at 300h on IRQ 3, as the field cards have it - and 300h is the
# first base ETHER.DRV asks (SPEC.md 72.2).
python3 "$HERE/mksd.py" "$BUILD/sd.img" --cfg ne2000Port=0x300 --cfg ne2000IRQ=3 >/dev/null
echo "    $BUILD/sd.img (microSD: no images, NE2000 300h/IRQ 3; tools/picomem/mksd.py)"

