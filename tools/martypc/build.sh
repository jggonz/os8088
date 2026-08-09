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

# The IBM BIOS is IBM's. It is not in this tree and cannot be - CONTRIBUTING.md
# puts the whole tree under one MIT file, and IBM has never licensed this ROM
# for redistribution. Supply your own copy; the five os8088_5150_* machines that
# ask for rom_set = "ibm5150_82_v4" need it, and the three glabios_* ones do not.
IBM_ROM=BIOS_IBM5150_27OCT82_1501476_U33.BIN
IBM_MD5=f453eb2df6daf21ec644d33663d85434
if [ -d "$HERE/roms" ] && ls "$HERE"/roms/*.BIN >/dev/null 2>&1; then
    cp "$HERE"/roms/*.BIN "$RUN/media/roms/"
else
    cat >&2 <<EOF
build.sh: note - no BIOS ROM in $HERE/roms/
    The GLaBIOS machines (os8088_5150_fast, os8088_5150_vga, os8088_5150_hd)
    will run. The period-accurate ones (rom_set = "ibm5150_82_v4") will not
    start until you drop in your own dump of the 27 OCT 82 IBM 5150 BIOS:

        $HERE/roms/$IBM_ROM
        8192 bytes, md5 $IBM_MD5

EOF
fi

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
