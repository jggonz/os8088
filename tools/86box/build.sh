#!/bin/sh
# Build the os8088 86Box instrument (docs/86BOX.md).
#
# Clones 86Box at the PINNED commit in UPSTREAM, applies the patches beside
# this script, and builds it with upstream's AUTOMATION socket and GDB stub
# switched on - the two things that turn 86Box from "a machine somebody has to
# watch" into one a session can drive and read (tools/os88box.py). Fetches the
# pinned ROM set beside it. Everything lands in $BUILD (default build/86box/).
#
# Pinned rather than tracking main for MartyPC's reason (tools/martypc/
# build.sh): an instrument that changes under you is one more variable.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
BUILD=${BUILD:-$ROOT/build/86box}
SRC=$BUILD/src
ROMS=$BUILD/roms

val() { sed -n "s/^$1=//p" "$HERE/UPSTREAM"; }
REPO=$(val repo); PIN=$(val commit); RREPO=$(val roms); RPIN=$(val roms_commit)

# --- the preflight -----------------------------------------------------------
# 86Box wants a long list of -dev packages and cmake reports the missing ones
# one at a time, each after a full configure. Ask pkg-config / the filesystem
# for all of them first. Auto-install is gated on root + apt, exactly as
# tools/martypc/build.sh gates it: a disposable container, never a
# contributor's workstation.
PKGS="cmake ninja-build pkg-config extra-cmake-modules qtbase5-dev qtbase5-private-dev \
qttools5-dev libqt5opengl5-dev libsdl2-dev libslirp-dev libfreetype-dev libpng-dev \
libsndfile1-dev libevdev-dev libxkbcommon-x11-dev libzstd-dev zlib1g-dev \
libserialport-dev libvdeplug-dev libgl-dev libpcap-dev libfaudio-dev"
missing=""
if command -v dpkg-query >/dev/null 2>&1; then
    for p in $PKGS; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "ok installed" || missing="$missing $p"
    done
fi
if [ -n "$missing" ] && [ "${OS88_NO_PREFLIGHT:-}" != "1" ]; then
    if [ "$(id -u)" = 0 ] && command -v apt-get >/dev/null 2>&1; then
        echo "==> installing:$missing"
        DEBIAN_FRONTEND=noninteractive apt-get install -y -q $missing >/dev/null 2>&1 || {
            apt-get update -q >/dev/null 2>&1 || true
            DEBIAN_FRONTEND=noninteractive apt-get install -y -q $missing
        }
    else
        echo "build.sh: missing packages:$missing" >&2
        echo "          apt-get install -y$missing   (OS88_NO_PREFLIGHT=1 skips this check)" >&2
        exit 1
    fi
fi

# --- source and ROMs, at the pins ---------------------------------------------
# Shallow fetches of one commit each: the ROM repository is ~250MB at depth 1
# and gigabytes of history beyond it. GIT_LFS_SKIP_SMUDGE because neither
# needs LFS objects and a proxy that refuses them would fail the clone.
fetch_pin() {        # fetch_pin DIR REPO COMMIT
    mkdir -p "$1"
    [ -d "$1/.git" ] || git -C "$1" init --quiet
    git -C "$1" remote add origin "$2" 2>/dev/null || git -C "$1" remote set-url origin "$2"
    if [ "$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null || true)" != "$3" ]; then
        echo "==> fetching $2 @ $3"
        GIT_LFS_SKIP_SMUDGE=1 git -C "$1" fetch --quiet --depth 1 origin "$3"
        GIT_LFS_SKIP_SMUDGE=1 git -C "$1" checkout --quiet --force "$3"
    fi
}
fetch_pin "$SRC" "$REPO" "$PIN"
fetch_pin "$ROMS" "$RREPO" "$RPIN"

# --- our changes -------------------------------------------------------------
echo "==> applying patches"
git -C "$SRC" checkout --quiet -- .
for p in "$HERE"/patches/*.patch; do
    echo "    $(basename "$p")"
    git -C "$SRC" apply "$p"
done

# --- build -------------------------------------------------------------------
# No dynarec: the interpreter is the CPU model that faults where a 286 faults.
# MIDI, OpenAL and Discord are off because nothing here needs them and each is
# another -dev package.
echo "==> configuring"
cmake -S "$SRC" -B "$SRC/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT=ON -DSDL2=ON -DOPENAL=OFF -DRTMIDI=OFF -DFLUIDSYNTH=OFF -DMUNT=OFF \
    -DDISCORD=OFF -DNEW_DYNAREC=OFF -DDYNAREC=OFF \
    -DAUTOMATION=ON -DGDBSTUB=ON >"$BUILD/cmake.log" 2>&1 \
    || { tail -20 "$BUILD/cmake.log" >&2; exit 1; }
echo "==> building (ninja; ~5 minutes on four cores)"
ninja -C "$SRC/build" >"$BUILD/ninja.log" 2>&1 || { grep -A5 FAILED "$BUILD/ninja.log" | head -30 >&2; exit 1; }
cp "$SRC/build/src/86Box" "$BUILD/86Box"

cat <<EOF

==> done.

  binary : $BUILD/86Box
  ROMs   : $ROMS

Boot os8088 on one of the vm/ machines, headless:

  python3 $ROOT/tools/os88box.py launch 286 --a build/os8088.img
  python3 $ROOT/tools/os88box.py 286 shot out.png
EOF
