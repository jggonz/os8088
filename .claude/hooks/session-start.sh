#!/bin/bash
# Install the os8088 toolchain: nasm, qemu-system-i386, python3.
#
# There is nothing to build here beyond those three - no linker, no package
# manifest, no vendored deps (CLAUDE.md, "Commands"). The whole reason this
# hook exists is the qemu install, which fails in a way that costs several
# minutes to rediscover: the apt candidate is the noble-updates build, whose
# .deb 404s on archive.ubuntu.com and then times out against
# security.ubuntu.com. The fix is to pin the BASE noble version, and `-t noble`
# is not enough - it still resolves to -updates. So the version is read out of
# the noble/main line specifically.
set -euo pipefail

# Local machines already have a toolchain; this is a web-session fixup only.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

# Idempotent: the container state is cached after a successful run, so a warm
# start should cost nothing at all.
if command -v nasm >/dev/null 2>&1 && command -v qemu-system-i386 >/dev/null 2>&1; then
  echo "session-start: toolchain already present ($(nasm -v))"
  exit 0
fi

SUDO=""
[ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1 && SUDO="sudo"
export DEBIAN_FRONTEND=noninteractive

# The shipped index is stale and does not list the base version at all.
# Third-party PPAs in this image 403 through the proxy; that is unrelated to
# the packages below, so a partial failure here must not abort the hook.
echo "session-start: refreshing apt index"
$SUDO apt-get update -qq || echo "session-start: apt-get update reported errors (continuing)"

PKGS="nasm"
if ! command -v qemu-system-i386 >/dev/null 2>&1; then
  # The noble/main line is the base version; noble-updates/noble-security are
  # the ones that 404. Fall back to the version this hook was written against.
  V="$(apt-cache madison qemu-system-x86 2>/dev/null \
       | awk -F'|' '$3 ~ /noble\/main/ {gsub(/ /,"",$2); print $2; exit}')"
  V="${V:-1:8.2.2+ds-0ubuntu1}"
  echo "session-start: pinning qemu to $V"
  # All three must be pinned together, or apt pulls the -updates common/data.
  PKGS="$PKGS qemu-system-x86=$V qemu-system-common=$V qemu-system-data=$V"
fi

# --no-install-recommends skips the gstreamer/libcaca display extras, which
# 404 the same way and which a headless `-display none` run never touches.
echo "session-start: installing $PKGS"
$SUDO apt-get install -y --no-install-recommends $PKGS

nasm -v
qemu-system-i386 --version | head -1
echo "session-start: toolchain ready"
