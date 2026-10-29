#!/usr/bin/env bash
#
# setup-watcom.sh - fetch the Open Watcom v2 C compiler WIN16 (SPEC.md 124)
# builds its Windows 3.x test programs with.
#
# One thing is downloaded, into build/watcom/, which is gitignored and which
# `make clean` SPARES (build/cc's reason: a pinned upstream instrument, and a
# fetch over the network): **no compiler binary is ever committed.** What is
# committed is the pinned release and SHA-256 below.
#
#   Open Watcom v2   github.com/open-watcom/open-watcom-v2, the snapshot
#                    tarball of the DATED release in PIN_TAG. Sybase Open
#                    Watcom Public License 1.0 (license.txt, which this script
#                    keeps and prints). The tarball carries every host's
#                    binaries; only this host's directory, the headers and the
#                    16-bit libraries are unpacked.
#
# Why Open Watcom, beside SmallerC (setup-cc.sh): it is a real 16-bit
# compiler with `long`, far pointers and structs by value, and it targets
# Windows 3.x itself (-bt=windows -l=windows), so it builds genuine Win16 NE
# programs - the fixtures WIN16's gates load, made from C rather than a
# hand-assembled header. docs/plans/WIN16-PLAN.md D10 is the decision. It is
# NOT the shipped layer's compiler: WIN16.O88 is 8086 assembly (D7).
#
# The script finishes by compiling a canary Win16 program with -0 (8086 code)
# and checking the output is an NE that imports USER, because "the files are
# present" is not the same claim as "the toolchain works".
#
# Usage:
#   tools/setup-watcom.sh                  fetch, unpack, verify
#   tools/setup-watcom.sh --build-dir DIR  use DIR/watcom instead of build/watcom
#   tools/setup-watcom.sh --check          report what is there, change nothing
#   tools/setup-watcom.sh --env            print the exports a shell needs
#
set -euo pipefail

PIN_TAG="2026-10-01-Build"
PIN_URL="https://github.com/open-watcom/open-watcom-v2/releases/download/${PIN_TAG}/ow-snapshot.tar.xz"
PIN_SHA="e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
MODE=setup
while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD="$2"; shift 2 ;;
        --check) MODE=check; shift ;;
        --env) MODE=env; shift ;;
        *) echo "setup-watcom.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done
W="$BUILD/watcom"

# The tarball's directory for this host's binaries.
case "$(uname -s)/$(uname -m)" in
    Darwin/arm64)   HOSTBIN=armo64 ;;
    Darwin/x86_64)  HOSTBIN=bino64 ;;
    Linux/x86_64)   HOSTBIN=binl64 ;;
    Linux/aarch64)  HOSTBIN=arml64 ;;
    *) echo "setup-watcom.sh: no Open Watcom host binaries for $(uname -s)/$(uname -m)" >&2; exit 1 ;;
esac

envlines() {
    echo "export WATCOM=$W"
    echo "export INCLUDE=$W/h:$W/h/win"
    echo "export PATH=$W/$HOSTBIN:\$PATH"
}

if [ "$MODE" = env ]; then envlines; exit 0; fi

stamp="$W/.pin"
if [ "$MODE" = check ]; then
    if [ -x "$W/$HOSTBIN/wcl" ] && [ "$(cat "$stamp" 2>/dev/null)" = "$PIN_SHA" ]; then
        echo "setup-watcom: $W is Open Watcom $PIN_TAG ($HOSTBIN)"
        exit 0
    fi
    echo "setup-watcom: not installed (run tools/setup-watcom.sh)"
    exit 1
fi

if [ ! -x "$W/$HOSTBIN/wcl" ] || [ "$(cat "$stamp" 2>/dev/null)" != "$PIN_SHA" ]; then
    rm -rf "$W"
    mkdir -p "$W"
    tarball="$W/ow-snapshot.tar.xz"
    echo "setup-watcom: fetching Open Watcom $PIN_TAG (~150 MB)"
    curl -sSL --fail -o "$tarball" "$PIN_URL"
    got="$(shasum -a 256 "$tarball" 2>/dev/null || sha256sum "$tarball")"
    got="${got%% *}"
    if [ "$got" != "$PIN_SHA" ]; then
        echo "setup-watcom: SHA-256 $got does not match the pin $PIN_SHA" >&2
        rm -f "$tarball"
        exit 1
    fi
    tar -xJf "$tarball" -C "$W" "./$HOSTBIN" ./h ./lib286 ./license.txt
    rm -f "$tarball"
    echo "$PIN_SHA" > "$stamp"
fi

# The canary: a Win16 program, 8086 code, linked as a Windows NE.
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/canary.c" <<'EOF'
#include <windows.h>
int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    MessageBox(0, "canary", "setup-watcom", MB_OK);
    return 0;
}
EOF
(
    eval "$(envlines)"
    cd "$tmp"
    wcl -q -0 -bt=windows -l=windows -fe=CANARY.EXE canary.c >/dev/null
)
python3 - "$tmp/CANARY.EXE" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
assert d[:2] == b"MZ", "not an MZ"
ne = struct.unpack_from("<H", d, 0x3C)[0]
assert d[ne:ne + 2] == b"NE", "no NE header"
assert d[ne + 0x36] == 2, "not a Windows NE"
modtab, imptab, cmod = (struct.unpack_from("<H", d, ne + o)[0] for o in (0x28, 0x2A, 0x1E))
mods = []
for i in range(cmod):
    off = struct.unpack_from("<H", d, ne + modtab + 2 * i)[0]
    p = ne + imptab + off
    mods.append(d[p + 1:p + 1 + d[p]].decode())
assert "USER" in mods, mods
print("setup-watcom: canary is a Win16 NE importing " + ", ".join(mods))
EOF
echo "setup-watcom: Open Watcom $PIN_TAG ready in $W ($HOSTBIN)."
echo "  licence: $W/license.txt (Sybase Open Watcom Public License 1.0)"
echo "  shell:   eval \"\$(tools/setup-watcom.sh --env)\""
