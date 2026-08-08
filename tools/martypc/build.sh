#!/bin/sh
# Build the os8088 MartyPC debugger (docs/MARTYPC-DEBUG.md).
#
# Clones MartyPC at the PINNED commit in UPSTREAM, applies the patches beside
# this script, drops in the machine configs and the BIOS, and builds the
# headless frontend. Everything lands in $BUILD (default build/martypc/), which
# is gitignored like the rest of build/.
#
# It is pinned rather than tracking main ON PURPOSE. This is a static
# instrument: a debugger that changes under you is one more variable in a
# session whose whole point is removing them, and a number taken through one
# build has to be comparable with a number taken through the next. Re-pinning
# is a deliberate act - edit UPSTREAM, re-run, fix whatever the patches no
# longer apply to.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
BUILD=${BUILD:-$ROOT/build/martypc}
SRC=$BUILD/src
RUN=$BUILD/run

REPO=$(sed -n 's/^repo=//p' "$HERE/UPSTREAM")
PIN=$(sed -n 's/^commit=//p' "$HERE/UPSTREAM")

command -v cargo >/dev/null || { echo "build.sh: cargo not found - install Rust" >&2; exit 1; }

# --- source, at the pin ------------------------------------------------------
if [ ! -d "$SRC/.git" ]; then
    echo "==> cloning $REPO"
    mkdir -p "$BUILD"
    git clone "$REPO" "$SRC"
fi
echo "==> checking out $PIN"
git -C "$SRC" fetch --quiet origin "$PIN" 2>/dev/null || git -C "$SRC" fetch --quiet origin
git -C "$SRC" checkout --quiet --force "$PIN"
git -C "$SRC" clean -qfd

# --- our changes -------------------------------------------------------------
echo "==> applying patches"
cp "$HERE/debug_server.rs" "$SRC/crates/binaries/martypc_headless/src/debug_server.rs"
for p in "$HERE"/patches/*.patch; do
    echo "    $(basename "$p")"
    git -C "$SRC" apply "$p"
done

# --- the run tree ------------------------------------------------------------
# MartyPC resolves everything relative to its working directory, so the run
# tree is a copy of upstream's install/ with our machine configs and the BIOS
# added. It is rebuilt every time: it holds no state worth keeping, and a
# stale config here is a machine that is not the one you think it is.
echo "==> staging the run tree"
rm -rf "$RUN"
cp -r "$SRC/install" "$RUN"
mkdir -p "$RUN/media/roms" "$RUN/media/floppies"
cat "$HERE/configs/os8088_machines.toml" >> "$RUN/configs/machines/ibm5150.toml"
cp "$HERE"/roms/*.BIN "$RUN/media/roms/" 2>/dev/null || true

# The stock martypc.toml automounts a VHD into a machine that has no hard disk
# controller, which is a startup error on every one of our configs.
python3 - "$RUN/martypc.toml" <<'PY'
import re, sys
p = sys.argv[1]
t = open(p).read()
t = re.sub(r'\[\[emulator\.media\.vhd\]\][^\[]*', '# (removed by os8088 build.sh: our machines have no HDC)\n\n', t)
t = re.sub(r'^config_name = .*$', 'config_name = "os8088_5150_cga"', t, count=1, flags=re.M)
open(p, "w").write(t)
PY

# --- build -------------------------------------------------------------------
echo "==> building martypc_headless (release)"
( cd "$SRC" && cargo build -p martypc_headless --release )
cp "$SRC/target/release/martypc_headless" "$RUN/martypc_headless"

cat <<EOF

==> done.

  run tree : $RUN
  binary   : $RUN/martypc_headless

Boot os8088 on it:

  cd $RUN && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \\
      --mount fd:0:media/floppies/os8088-360.img &

  python3 $ROOT/tools/os88marty.py 127.0.0.1:9001 status
EOF
