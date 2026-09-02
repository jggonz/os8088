#!/bin/bash
# Build the FreeDOS payload for the DOS floppy: KERNEL.SYS, COMMAND.COM and a
# CHS-only FAT12 boot sector. See SPEC.md 86.6 for why each step is what it is.
#
# ON DEMAND ONLY - `make dos`, never `make all`. This fetches a ~148MB
# toolchain the first time it runs, and a default-target dependency that needs
# the network is a default target that breaks on a train.
#
# Everything is built from the sibling ../kernel and ../freecom checkouts, into
# COPIES under build/freedos/. Those trees are never modified in place: they
# are somebody else's repository and a patched working tree there is a trap for
# whoever next types `git pull` in them.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"      # the os8088 repo root
REPOS="$(cd "$HERE/.." && pwd)"               # the directory holding all four
BUILD="$HERE/build/freedos"
OUT="$HERE/build/dos"
PATCHES="$HERE/tools/freedos/patches"

# The Open Watcom V2 snapshot. This is a ROLLING tag - the same URL serves
# different bits over time - so the hash below is the build that was proven to
# work, not a security check. A mismatch is a warning and not an error: the
# newer snapshot is usually fine, and hard-failing would break the build every
# time upstream ships. If FreeDOS then misbehaves, this is the first suspect.
OW_URL='https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz'
OW_SHA='2be9994e0a9c1e691091e9badea6d512cd5c791f530e81661a12e5a2c567b526'

# The country submodule. kernel/kernel/config.c #includes ../country/kernel.tb1
# outright, so an unpopulated submodule is a compile error, not a missing
# feature. Pinned because this is a build input like any other.
COUNTRY_URL='https://github.com/FDOS/country.git'
COUNTRY_REV='7f83e041d00f78b3912c761246930f3b437440f6'

mkdir -p "$BUILD" "$OUT"

# ---- 1. Open Watcom, macOS/arm64 host binaries ------------------------------
# $WATCOM set in the environment wins, so a developer with their own install
# pays none of this.
if [ -z "${WATCOM:-}" ]; then
  if [ ! -x "$BUILD/watcom/armo64/wcc" ]; then
    TAR="$BUILD/ow-snapshot.tar.xz"
    if [ ! -f "$TAR" ]; then
      echo "fetching Open Watcom (~148MB, once)..."
      curl -fL --progress-bar -o "$TAR.part" "$OW_URL"
      mv "$TAR.part" "$TAR"
    fi
    GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
    if [ "$GOT" != "$OW_SHA" ]; then
      echo "warning: ow-snapshot.tar.xz is not the proven build" >&2
      echo "  expected $OW_SHA" >&2
      echo "  got      $GOT" >&2
      echo "  (rolling tag - carrying on. Suspect this first if DOS misbehaves.)" >&2
    fi
    # armo64 is the Apple-Silicon HOST build. Only these members are needed;
    # the whole archive is several GB unpacked.
    mkdir -p "$BUILD/watcom"
    tar -xJf "$TAR" -C "$BUILD/watcom" \
        ./armo64 ./h ./lh ./rh ./lib286 ./lib386 ./eddat
  fi
  export WATCOM="$BUILD/watcom"
fi
export PATH="$WATCOM/armo64:$PATH"

command -v wcc >/dev/null || {
  echo "error: wcc not on PATH after setup (WATCOM=$WATCOM)" >&2; exit 1; }

# ---- 2. FreeDOS kernel, 8086 ------------------------------------------------
# XCPU=86 is the whole point: the target is an 8088. XFAT=16 keeps the kernel
# small (nothing here has a FAT32 volume). XUPX= (empty) disables compression -
# upx is not installed, and 70KB uncompressed still fits the 128KB the boot
# sector can load.
KDIR="$BUILD/kernel"
if [ ! -f "$KDIR/bin/kwc8616.sys" ]; then
  rm -rf "$KDIR"
  cp -R "$REPOS/kernel" "$KDIR"
  rm -rf "$KDIR/.git"
  rmdir "$KDIR/country" 2>/dev/null || true
  if [ ! -f "$KDIR/country/kernel.tb1" ]; then
    rm -rf "$KDIR/country"
    git clone -q "$COUNTRY_URL" "$KDIR/country"
    git -C "$KDIR/country" checkout -q "$COUNTRY_REV"
  fi
  cp "$PATCHES/owosx.mak" "$KDIR/mkfiles/"
  ( cd "$KDIR" && make all COMPILER=owosx XCPU=86 XFAT=16 XUPX= )
fi

# ---- 3. FreeCOM -------------------------------------------------------------
FCDIR="$BUILD/freecom"
if [ ! -f "$FCDIR/command.com" ]; then
  rm -rf "$FCDIR"
  cp -R "$REPOS/freecom" "$FCDIR"
  rm -rf "$FCDIR/.git"
  # Drop -DGCC from the macOS host-tool rule - see tools/freedos/README.md for
  # why this is an upstream bug and why its symptom is a lie. Done with sed
  # rather than a .patch because these files are CRLF and a patch context
  # written on this side of the fence never matches. sed does not care, but it
  # also does not report a miss, so the result is asserted below.
  sed -i '' 's/-Wno-pragma-pack -DGCC -D__GETOPT_H/-Wno-pragma-pack -D__GETOPT_H/' \
      "$FCDIR/mkfiles/watcom.mak"
  if grep -q -- '-DGCC' "$FCDIR/mkfiles/watcom.mak"; then
    echo "error: -DGCC still present in freecom/mkfiles/watcom.mak after the" >&2
    echo "  edit. Upstream changed that line; re-derive it before building, or" >&2
    echo "  command.com will build, install, and fail as 'Bad or missing" >&2
    echo "  Command Interpreter'." >&2
    exit 1
  fi
  ( cd "$FCDIR" && LNG=english bash ./build.sh wc )
fi

# ---- 4. Collect, and patch the boot sector to CHS-only ----------------------
cp "$KDIR/bin/kwc8616.sys" "$OUT/KERNEL.SYS"
cp "$FCDIR/command.com"    "$OUT/COMMAND.COM"

# The FreeDOS FAT12 boot sector probes for INT 13h extensions (AH=41h) unless
# the boot unit is 0. We boot from unit 1, so on an XT that probe goes to a ROM
# that predates the call. It SHOULD return CF=1 and fall through; an XT BIOS
# under test is not the place to find out. Turning `test dl,dl` into
# `xor dl,dl` forces the following `jz` and skips the probe entirely - exactly
# what SYS /FORCE:CHS does. Safe with DL=1 because the read path reloads the
# unit from [drive] before every INT 13h.
python3 - "$KDIR/boot/fat12com.bin" "$OUT/FAT12CHS.BIN" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, 'rb').read())
if len(b) != 512:
    sys.exit(f"boot sector is {len(b)} bytes, not 512")
# Assert before patching: if upstream moves this instruction, a blind poke
# would corrupt a working sector into a plausible-looking broken one.
if b[0x17B:0x17D] != b'\x84\xD2':
    sys.exit(f"expected `test dl,dl` (84 D2) at 0x17B, found "
             f"{b[0x17B]:02X} {b[0x17C]:02X} - upstream boot sector changed, "
             f"re-derive the offset before touching this")
b[0x17B] = 0x30                      # 30 D2 = xor dl,dl
open(dst, 'wb').write(bytes(b))
print(f"boot sector: {src} -> {dst} (CHS-only)")
PY

echo
echo "FreeDOS payload ready in $OUT:"
ls -la "$OUT"
