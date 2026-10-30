#!/usr/bin/env bash
#
# setup-qemu9.sh - get a QEMU 9 onto this box, for the `qemu9` soak capability.
#
# QEMU 8.x ABORTS the whole emulator when the guest takes a hardware interrupt
# with SS:SP in MMIO-backed memory - VGA's A000-BFFF in a text mode is one -
# because 8.2's accel/tcg/cputlb.c locks the BQL unconditionally in
# do_st_mmio_leN, and a hardware interrupt is delivered with the BQL already
# held: `qemu_mutex_lock_iothread_impl: assertion failed:
# (!qemu_mutex_iothread_locked())`, from do_interrupt_all. 9.0 made that a
# BQL_LOCK_GUARD(), which takes the lock only when it is not held.
#
# os8088 does exactly that on purpose: the hibernate resume stub (SPEC.md
# 87.5, kernel/hbstub.inc) runs with its stack in the TEXT FRAMEBUFFER, the
# one RAM a conventional-memory image does not cover, and calls int 13h, which
# enables interrupts. On iron and on MartyPC that is RAM like any other; on a
# QEMU 8 it is the emulator dying mid-resume, which a row reports as a QMP
# socket refusing ("Connection refused") rather than as anything about the
# machine. Ubuntu 24.04 ships 8.2.2, so every container here has the bug.
#
#   tools/setup-qemu9.sh              probe, and build one if there is none
#   tools/setup-qemu9.sh --check      report what is there, change nothing
#   tools/setup-qemu9.sh --print-path print the bin directory and exit
#   tools/setup-qemu9.sh --force      discard build/qemu9 and start over
#   tools/setup-qemu9.sh --tag TAG    a tag other than the pinned one
#
# The rows launch `qemu-system-i386` by NAME, so the build is used by putting
# its bin directory first on PATH - the script prints the line. Idempotent,
# and a few milliseconds once build/qemu9 holds a build carrying the patches
# below. A stock QEMU 9 on PATH (Homebrew's is) answers only --check's and
# --print-path's `qemu9` question; install mode builds the patched QEMU that
# `qemuvt` needs whatever is on PATH, on a Mac as on Linux.
#
# THE PATCHES (tools/qemu9/*.patch) go into every build, and the pkgversion
# says which: `--version` reads "(os8088-<12 hex of their SHA-256>)", the
# same figure os88build.qemuvt() computes - so a build from before a patch
# changed is REBUILT here (the version is in its config: a full build) and
# a QEMU 9 on PATH is not
# taken for ours. i8257-icount.patch is why there are any: under -icount the
# stock 8237 moves its transfers on the main loop in HOST time, which stopped
# a Sound Blaster on a loaded box (os88build.qemuvt()). Without -icount the
# patched QEMU behaves as stock, so every other row may use it too.
#
# NOTHING IS COMMITTED. The clone and the build live in build/qemu9/, which is
# gitignored. `git clone` works through the agent proxy where download.qemu.org
# answers 403, so this clones and never fetches a tarball. Only i386-softmmu is
# built, with SLiRP when libslirp-dev is there (the network rows want it).
#
set -euo pipefail

PIN_TAG="v9.2.4"
QEMU_URL="https://gitlab.com/qemu-project/qemu.git"
MIN_MAJOR=9

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
patches="$root/tools/qemu9"
pv="os8088-$(LC_ALL=C; cat "$patches"/*.patch | sha256sum | cut -c1-12)"
dir="$root/build/qemu9"
inst="$dir/inst"
mode="install"
tag="$PIN_TAG"

while [ $# -gt 0 ]; do
    case "$1" in
        --check)      mode="check" ;;
        --print-path) mode="path" ;;
        --force)      mode="force" ;;
        --tag)        shift; tag="${1:?--tag wants a tag}" ;;
        -h|--help)    sed -n '2,32p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)            echo "setup-qemu9.sh: unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# The same question tools/os88build.py's qemu9() asks: the qemu-system-i386
# a row would run, by its own --version. A NAME IS NOT A VERSION.
major_of() {
    "$1" --version 2>/dev/null \
        | sed -n 's/^QEMU emulator version \([0-9][0-9]*\).*/\1/p' | head -1
}
is_qemu9() {
    local p="$1" v
    [ -n "$p" ] && [ -x "$p" ] || return 1
    v="$(major_of "$p")"
    [ -n "$v" ] && [ "$v" -ge "$MIN_MAJOR" ]
}

is_ours() {
    [ -x "$1" ] && "$1" --version 2>/dev/null | head -1 | grep -qF "($pv)"
}

if [ "$mode" = "force" ]; then
    rm -rf "$dir"
    mode="install"
fi

onpath="$(command -v qemu-system-i386 2>/dev/null || true)"
if is_ours "$inst/bin/qemu-system-i386"; then
    case "$mode" in
        path)  echo "$inst/bin" ;;
        *)     echo "qemu 9 with os8088's patches ($pv): $inst/bin/qemu-system-i386" ;;
    esac
    exit 0
fi
# --check / --print-path: any QEMU 9 answers the `qemu9` question (the
# hibernate rows); only the build above answers `qemuvt` (os88build.py)
if [ "$mode" = "check" ] || [ "$mode" = "path" ]; then
    for q in "$onpath" "$inst/bin/qemu-system-i386"; do
        if is_qemu9 "$q"; then
            case "$mode" in
                path)  dirname "$q" ;;
                *)     echo "qemu 9+ at $q ($("$q" --version | head -1)), WITHOUT" \
                            "os8088's patches ($pv): no \`qemuvt\` - run this to build it" ;;
            esac
            exit 0
        fi
    done
fi
if [ "$mode" = "check" ] || [ "$mode" = "path" ]; then
    echo "no QEMU 9 on this box (PATH has ${onpath:-none}${onpath:+, $("$onpath" --version | head -1)})" \
         "- tools/setup-qemu9.sh builds one" >&2
    exit 1
fi

# --- what a QEMU build needs that a fresh container lacks ------------------
need=()
brew=()                              # the same set by Homebrew's names
command -v ninja >/dev/null || { need+=(ninja-build); brew+=(ninja); }
command -v flex >/dev/null || { need+=(flex); brew+=(flex); }
command -v bison >/dev/null || { need+=(bison); brew+=(bison); }
command -v pkg-config >/dev/null || brew+=(pkg-config)
pkg-config --exists glib-2.0 2>/dev/null || { need+=(libglib2.0-dev); brew+=(glib); }
pkg-config --exists pixman-1 2>/dev/null || { need+=(libpixman-1-dev); brew+=(pixman); }
pkg-config --exists slirp 2>/dev/null || { need+=(libslirp-dev); brew+=(libslirp); }
python3 -c 'import venv' 2>/dev/null || { need+=(python3-venv); brew+=(python); }
if [ ${#need[@]} -gt 0 ]; then
    if [ "$(id -u)" = 0 ] && command -v apt-get >/dev/null; then
        echo "setup-qemu9: installing ${need[*]}"
        apt-get install -y "${need[@]}" >/dev/null
    elif [ "$(uname -s)" = Darwin ]; then
        echo "setup-qemu9: needs ${brew[*]} - \`brew install ${brew[*]}\`" \
             "and run this again" >&2
        exit 1
    else
        echo "setup-qemu9: needs ${need[*]} - install them and run this again" >&2
        exit 1
    fi
fi

mkdir -p "$dir"
if [ ! -d "$dir/src/.git" ]; then
    git clone --depth 1 --branch "$tag" "$QEMU_URL" "$dir/src"
fi
cd "$dir/src"
for p in "$patches"/*.patch; do      # idempotent: an applied one is left be
    if git apply --reverse --check "$p" 2>/dev/null; then
        continue
    fi
    git apply "$p" || { echo "setup-qemu9: $p does not apply to $tag" >&2; exit 1; }
done
mkdir -p build
cd build
if [ ! -f build.ninja ]; then
    ../configure --prefix="$inst" --target-list=i386-softmmu \
        --disable-docs --disable-werror --disable-tools --disable-guest-agent \
        --enable-slirp --with-pkgversion="$pv"
else                                 # a build from before: the version only
    ./pyvenv/bin/meson configure -Dpkgversion="$pv" >/dev/null
fi
jobs="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null \
        || sysctl -n hw.ncpu 2>/dev/null || echo 4)"  # macOS has no nproc
ninja -j"$jobs"
ninja install >/dev/null
is_ours "$inst/bin/qemu-system-i386" || {
    echo "setup-qemu9: the build did not produce a QEMU 9 carrying $pv" >&2; exit 1; }
echo "qemu 9 built: $inst/bin/qemu-system-i386 ($("$inst/bin/qemu-system-i386" --version | head -1))"
echo "export PATH=$inst/bin:\$PATH"
