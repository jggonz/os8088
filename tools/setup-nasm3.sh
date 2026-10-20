#!/usr/bin/env bash
#
# setup-nasm3.sh - get an nasm 3 onto this box, for the `nasm3` soak row.
#
# The row (tests/unit/t_nasm3.py) assembles the whole shipped set with an
# nasm 3 and SKIPS without one, because CONTRIBUTING.md's floor is 2 and
# every Linux box here answers 2.16. A skip is the box declining to answer,
# never a pass - so on a machine where the row matters, this is the one
# command to type.
#
#   tools/setup-nasm3.sh              probe, and build one if there is none
#   tools/setup-nasm3.sh --check      report what is there, change nothing
#   tools/setup-nasm3.sh --print-path print the binary's path and exit
#   tools/setup-nasm3.sh --force      discard build/nasm3 and start over
#   tools/setup-nasm3.sh --tag TAG    a tag other than the pinned one
#
# Idempotent, and a few milliseconds when a 3.x is already there. On macOS
# that is always the case - `brew install nasm` is 3.x - so this exits at the
# probe having done nothing.
#
# NOTHING IS COMMITTED. The clone and the build live in build/nasm3/, which
# is gitignored and which `make clean` spares the way it spares build/cc: it
# is a pinned upstream instrument, not an artefact of this tree.
#
# WHY THIS IS A SCRIPT AND NOT FOUR LINES IN A DOCUMENT. Those four lines
# were in CONTRIBUTING.md for a cycle and nobody got an nasm 3 out of them,
# because on Linux the route has three traps that all report as something
# else (docs/MARTYPC-DEBUG.md, "An nasm 3 in a fresh container"):
#
#   1. www.nasm.us is refused by the agent proxy, so there is no tarball and
#      no prebuilt binary - and the refusal reads like the whole network
#      being down, because probing github.com answers 403 too. It is not:
#      `git clone` works fine. This script does not probe, it clones.
#   2. the git tree ships no `configure` - it is generated, and only the
#      release tarballs (on the blocked host) carry one. An operator used to
#      tarballs runs ./configure, is told there is no such file, and concludes
#      the clone is broken. `sh autogen.sh` is the missing step, and its
#      first-run `mv: cannot stat 'autoconf/aclocal.m4'` is harmless noise.
#   3. **a /dev/null that is a regular file silently breaks ./configure**,
#      and this is the one that actually stops people, because the error
#      names config.status and nothing names /dev/null. autoconf's default
#      cache_file IS /dev/null; when that is an ordinary file the cache
#      flush writes diff output into the generated config.status, which then
#      dies on `0a1,180: command not found`. Checked below, BEFORE the clone,
#      because four minutes is a long way to carry a one-second question.
#
set -euo pipefail

PIN_TAG="nasm-3.02"
NASM_URL="https://github.com/netwide-assembler/nasm.git"
MIN_MAJOR=3

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="$root/build"
dir="$build/nasm3"
mode="install"
tag="$PIN_TAG"

while [ $# -gt 0 ]; do
    case "$1" in
        --check)      mode="check" ;;
        --print-path) mode="path" ;;
        --force)      mode="force" ;;
        --tag)        shift; tag="${1:?--tag wants a tag}" ;;
        --build-dir)  shift; build="${1:?--build-dir wants a directory}"
                      dir="$build/nasm3" ;;
        -h|--help)    sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)            echo "setup-nasm3.sh: unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# --- the probe ------------------------------------------------------------
#
# The same order os88build.nasm3() uses, so this script and the capability
# agree by construction: $OS88_NASM3, then nasm3/nasm-3 on PATH, then plain
# `nasm` if it is already a 3.x (the macOS case). A NAME IS NOT A VERSION -
# `nasm3` has been a symlink to 2.16 on at least one box - so every candidate
# is asked, and a mislabelled binary counts as absence.
is_nasm3() {
    local p="$1" v
    [ -n "$p" ] && [ -x "$p" ] || return 1
    v="$("$p" -v 2>&1 | sed -n 's/^NASM version \([0-9][0-9]*\).*/\1/p')"
    [ -n "$v" ] && [ "$v" -ge "$MIN_MAJOR" ]
}

find_nasm3() {
    local p
    if [ -n "${OS88_NASM3:-}" ]; then
        is_nasm3 "$OS88_NASM3" && { echo "$OS88_NASM3"; return 0; }
    fi
    for n in nasm3 nasm-3 nasm; do
        p="$(command -v "$n" 2>/dev/null || true)"
        is_nasm3 "$p" && { echo "$p"; return 0; }
    done
    is_nasm3 "$dir/nasm/nasm" && { echo "$dir/nasm/nasm"; return 0; }
    return 1
}

if [ "$mode" = "force" ]; then
    rm -rf "$dir"
    mode="install"
fi

if have="$(find_nasm3)"; then
    case "$mode" in
        path)  echo "$have"; exit 0 ;;
        *)     echo "nasm 3 present: $have ($("$have" -v))"
               case "$have" in
                   "$dir"/*) echo "export OS88_NASM3=$have" ;;
               esac
               exit 0 ;;
    esac
fi

if [ "$mode" = "check" ]; then
    echo "no nasm 3 on this box - tools/setup-nasm3.sh builds one" >&2
    exit 1
fi
if [ "$mode" = "path" ]; then
    exit 1
fi

# --- trap 3, asked in one second rather than four minutes ------------------
#
# A /dev/null that is not a character device breaks autoconf, and the error
# it produces names config.status. Say so here instead. It is a container
# defect rather than anything this tree did, and the repair is one command -
# but it needs a privilege this script will not assume, so it is REPORTED.
if [ ! -c /dev/null ]; then
    cat >&2 <<'NULLMSG'
setup-nasm3.sh: /dev/null on this box is NOT a character device.

  $ stat -c %F /dev/null
NULLMSG
    echo "  $(stat -c %F /dev/null 2>&1)" >&2
    cat >&2 <<'NULLMSG'

autoconf's default cache_file IS /dev/null, so with an ordinary file there
the cache flush writes diff output into the config.status it is generating,
and ./configure dies with

  ./config.status: line NNN: 0a1,180: command not found

which names neither /dev/null nor the real fault. Nothing about nasm is
wrong and re-cloning will not help. Repair the node first, as root:

  mknod /dev/null.new c 1 3 && chmod 666 /dev/null.new \
      && mv -f /dev/null.new /dev/null

then run this script again. (Everything that redirects to a regular-file
/dev/null is also quietly appending to it, so this is worth fixing whatever
you think of nasm.)
NULLMSG
    exit 1
fi

# --- prerequisites --------------------------------------------------------
missing=""
for t in git gcc make autoconf automake aclocal autoheader perl; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done
if [ -n "$missing" ]; then
    echo "setup-nasm3.sh: missing:$missing" >&2
    echo "  apt-get install -y --no-install-recommends build-essential \\" >&2
    echo "          autoconf automake perl git" >&2
    exit 1
fi

# --- clone, generate, build ----------------------------------------------
#
# DO NOT PROBE FIRST. github.com answers 403 to the API and to the release
# pages through the agent proxy, which reads like a locked-down network and
# is not one - git's smart-HTTP endpoints are reachable and a clone works.
mkdir -p "$dir"
src="$dir/nasm"
if [ ! -d "$src/.git" ]; then
    echo "setup-nasm3.sh: cloning $tag..."
    rm -rf "$src"
    git clone --depth 1 -b "$tag" "$NASM_URL" "$src"
fi

cd "$src"
if [ ! -f configure ]; then
    echo "setup-nasm3.sh: generating configure (sh autogen.sh)..."
    # Its 'mv: cannot stat autoconf/aclocal.m4' on a fresh clone is a
    # first-run artefact and the script goes on to exit 0. Check for the
    # FILE rather than reading the log.
    sh autogen.sh >"$dir/autogen.log" 2>&1 || true
    if [ ! -f configure ]; then
        echo "setup-nasm3.sh: autogen.sh produced no configure; see $dir/autogen.log" >&2
        exit 1
    fi
fi

if [ ! -f Makefile ]; then
    echo "setup-nasm3.sh: ./configure..."
    ./configure >"$dir/configure.log" 2>&1 || {
        echo "setup-nasm3.sh: configure failed; see $dir/configure.log" >&2
        tail -5 "$dir/configure.log" >&2
        exit 1
    }
fi

echo "setup-nasm3.sh: make..."
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
     >"$dir/make.log" 2>&1 || {
    echo "setup-nasm3.sh: make failed; see $dir/make.log" >&2
    tail -5 "$dir/make.log" >&2
    exit 1
}

# --- verify, because "it built" is not "it is a 3" ------------------------
if ! is_nasm3 "$src/nasm"; then
    echo "setup-nasm3.sh: built a binary that is not an nasm 3:" >&2
    "$src/nasm" -v >&2 || true
    exit 1
fi

echo
echo "nasm 3 built: $src/nasm ($("$src/nasm" -v))"
echo
echo "  export OS88_NASM3=$src/nasm"
echo
echo "then:  python3 tools/os88soak.py check | grep nasm3"
echo "       python3 tools/os88test.py soak -k nasm3 --user-asked"
