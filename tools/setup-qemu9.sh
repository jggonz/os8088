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
# and a few milliseconds when the qemu-system-i386 on PATH is already 9+
# (Homebrew's is).
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

if [ "$mode" = "force" ]; then
    rm -rf "$dir"
    mode="install"
fi

onpath="$(command -v qemu-system-i386 2>/dev/null || true)"
if is_qemu9 "$onpath"; then
    case "$mode" in
        path)  dirname "$onpath" ;;
        *)     echo "qemu 9+ on PATH: $onpath ($("$onpath" --version | head -1))" ;;
    esac
    exit 0
fi
if is_qemu9 "$inst/bin/qemu-system-i386"; then
    case "$mode" in
        path)  echo "$inst/bin" ;;
        *)     echo "qemu 9+ built: $inst/bin/qemu-system-i386, NOT first on PATH:"
               echo "export PATH=$inst/bin:\$PATH" ;;
    esac
    exit 0
fi
if [ "$mode" = "check" ] || [ "$mode" = "path" ]; then
    echo "no QEMU 9 on this box (PATH has ${onpath:-none}${onpath:+, $("$onpath" --version | head -1)})" \
         "- tools/setup-qemu9.sh builds one" >&2
    exit 1
fi

# --- what a QEMU build needs that a fresh container lacks ------------------
need=()
command -v ninja >/dev/null || need+=(ninja-build)
command -v flex >/dev/null || need+=(flex)
command -v bison >/dev/null || need+=(bison)
pkg-config --exists glib-2.0 2>/dev/null || need+=(libglib2.0-dev)
pkg-config --exists pixman-1 2>/dev/null || need+=(libpixman-1-dev)
pkg-config --exists slirp 2>/dev/null || need+=(libslirp-dev)
python3 -c 'import venv' 2>/dev/null || need+=(python3-venv)
if [ ${#need[@]} -gt 0 ]; then
    if [ "$(id -u)" = 0 ] && command -v apt-get >/dev/null; then
        echo "setup-qemu9: installing ${need[*]}"
        apt-get install -y "${need[@]}" >/dev/null
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
mkdir -p build
cd build
if [ ! -f build.ninja ]; then
    ../configure --prefix="$inst" --target-list=i386-softmmu \
        --disable-docs --disable-werror --disable-tools --disable-guest-agent \
        --enable-slirp
fi
ninja -j"$(nproc)"
ninja install >/dev/null
is_qemu9 "$inst/bin/qemu-system-i386" || {
    echo "setup-qemu9: the build did not produce a QEMU 9" >&2; exit 1; }
echo "qemu 9 built: $inst/bin/qemu-system-i386 ($("$inst/bin/qemu-system-i386" --version | head -1))"
echo "export PATH=$inst/bin:\$PATH"
