#!/bin/sh
#
# setup-linux.sh - install everything this tree needs on a Linux box, and
#                  above all in the FRESH UBUNTU CONTAINER an agent session
#                  gets. `tools/setup-macos.sh` is the Mac half; this is the
#                  one a container wants, and `make deps` runs whichever fits.
#
# WHY THIS EXISTS. The deps were documented - CLAUDE.md's Commands section
# names `libudev-dev` and `pkg-config`, and docs/MARTYPC-DEBUG.md has sixty
# lines on the apt traps - and agent after agent still typed `make marty`
# first, waited several minutes, and only then read any of it. Documentation
# lost to a forcing function it did not have:
#
#   * the cost lands MINUTES LATE. `make marty` clones, patches and compiles
#     before cargo ever reaches `serialport`, so the missing 200KB header is
#     reported after the expensive part, by which point the reader is
#     debugging a build rather than provisioning a box.
#   * the knowledge was a PARENTHESIS in a sentence about cargo, and sixty
#     lines in a file nobody opens until something breaks. Neither is a
#     command anybody can type.
#   * there was NO ONE COMMAND to type. The recipe is two apt invocations
#     with OPPOSITE cures (below), which is a thing to get right rather than
#     a thing to run.
#
# So the fix is not more prose. It is this script, `tools/martypc/build.sh`'s
# one-second preflight that runs it, and `make deps`. Read the script, not a
# document, if you want to know what a box needs.
#
# IT PROBES RATHER THAN TRANSCRIBING, which is the other half of the lesson.
# docs/MARTYPC-DEBUG.md pins qemu to a base noble version because the
# `-updates` .deb used to 404; on the archive of 2026-09-08 both fetch fine
# and the plain install takes 8 seconds. A script that hard-coded the pin
# would have been wrong the day the archive caught up, and wrong in the
# direction that installs an OLDER emulator for no reason. Every cure here is
# therefore a FALLBACK behind the plain attempt, so the tree tracks the
# archive and still knows what to do when the archive misbehaves.
#
# Usage:
#   tools/setup-linux.sh            install what is missing, no-op what is not
#   tools/setup-linux.sh --check    report only; exit 1 if anything is missing
#   tools/setup-linux.sh --quiet    only speak when something is wrong or done
#   tools/setup-linux.sh --with-cc  also fetch/build SmallerC (the C targets)
#
# It is IDEMPOTENT and CHEAP when satisfied: the fast path is five `command
# -v` calls and one pkg-config, and it touches apt only for what is absent.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/.." && pwd)

CHECK=0; QUIET=0; WITH_CC=0
for a in "$@"; do
    case "$a" in
        --check)    CHECK=1 ;;
        --quiet|-q) QUIET=1 ;;
        --with-cc)  WITH_CC=1 ;;
        -h|--help)  sed -n '2,45p' "$0"; exit 0 ;;
        *) echo "setup-linux.sh: unknown option $a" >&2; exit 2 ;;
    esac
done

say()  { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
warn() { printf '%s\n' "$*" >&2; }

if [ "$(uname -s)" = "Darwin" ]; then
    warn "setup-linux.sh: this is a Mac - run tools/setup-macos.sh instead"
    warn "                (it installs the Mac set, but NOT Rust, so"
    warn "                 \`make marty\` there still wants cargo by hand)"
    exit 2
fi

# --- what the tree needs, and who needs it ----------------------------------
#
# ONE ROW PER THING, and the third column is what breaks without it - because
# the reader of a failed check is deciding whether to care, and "nasm" does
# not tell them and "nothing under build/ can be made" does.
#
# `cargo` is in the table and NOT in the apt list on purpose: Rust comes from
# rustup rather than the archive, installing it is a 200MB decision belonging
# to whoever is at the keyboard, and every agent container here already has
# one. So it is REPORTED and never installed.

have_libudev() { pkg-config --exists libudev 2>/dev/null; }
have_pil() { python3 -c 'from PIL import Image' >/dev/null 2>&1; }

missing_list() {
    command -v nasm            >/dev/null 2>&1 || echo nasm
    command -v pkg-config      >/dev/null 2>&1 || echo pkg-config
    have_libudev                               || echo libudev-dev
    command -v qemu-system-i386 >/dev/null 2>&1 \
        || command -v qemu-system-x86_64 >/dev/null 2>&1 || echo qemu-system-x86
    # The Z-machine compiler. `build/zt/ZOPS.Z5` is built with it, and it is
    # a prerequisite of `zmove360.img` and `editmove360.img` one edge up - so
    # without it those rows have no disk and `os88soak.py check` reports the
    # gap. It is small and it is packaged, which is why it is installed here
    # rather than merely reported: the soak preflight once took a five-hour
    # run down to 37 minutes with 0 of 267 rows reported over exactly this.
    command -v inform6 >/dev/null 2>&1 \
        || command -v inform >/dev/null 2>&1 || echo inform6-compiler
    # Pillow. `tests/pxsshots.py` writes its photographs through it, and
    # tools/os88pix.py falls back to it for a picture that is not a PNG. The
    # row died on an ImportError in a soak rather than skipping, because
    # nothing named the module - so it is a probed capability now (`pil`,
    # tools/os88test.py) and installed here for the same reason as inform6.
    # `PIL.Image` and not `PIL`: the package imports on an interpreter its C
    # extension was not built for, and only the extension says so.
    have_pil || echo python3-pil
}

report() {
    printf '  %-18s %s\n' "$1" "$2"
}

status() {
    say "os8088 host dependencies:"
    command -v nasm >/dev/null 2>&1 \
        && report nasm "$(nasm -v 2>/dev/null | head -1)" \
        || report nasm "MISSING - every build; nothing under build/ can be made"
    command -v cargo >/dev/null 2>&1 \
        && report cargo "$(cargo --version 2>/dev/null)" \
        || report cargo "MISSING - \`make marty\`. NOT installed here: it is
                     rustup's, not the archive's. See https://rustup.rs"
    have_libudev \
        && report libudev "$(pkg-config --modversion libudev 2>/dev/null)" \
        || report libudev "MISSING - \`make marty\` fails MINUTES IN, inside
                     cargo, on the serialport crate's build script"
    command -v qemu-system-i386 >/dev/null 2>&1 \
        && report qemu "$(qemu-system-i386 --version 2>/dev/null | head -1)" \
        || report qemu "MISSING - docs/TESTING.md's seven QEMU-only cases:
                     the 286/386 rows, the PS/2 mouse, the RTC write half"
    command -v python3 >/dev/null 2>&1 \
        && report python3 "$(python3 -V 2>&1)" \
        || report python3 "MISSING - every tool in tools/"
    if command -v inform6 >/dev/null 2>&1; then
        report inform6 "$(inform6 -h 2>&1 \
            | sed -n 's/.*Inform \([0-9][0-9.]*\).*/\1/p' | head -1)"
    elif command -v inform >/dev/null 2>&1; then
        report inform6 "present (inform)"
    else
        report inform6 "MISSING - build/zt/ZOPS.Z5, so the zmove and
                     editmove rows have no disk to boot"
    fi
    have_pil \
        && report pillow "$(python3 -c 'import PIL; print(PIL.__version__)' 2>/dev/null)" \
        || report pillow "MISSING - the pxsshots row SKIPs (its photographs
                     are written through it)"
    # nasm 3 is a PROBED CAPABILITY and not a dependency - nothing in any
    # tier needs one to build, and the `nasm3` row SKIPs without it. So it is
    # reported and never installed, and no distribution packages one anyway:
    # tools/setup-nasm3.sh is the command, and it is instant when there is
    # already a 3.x (which on macOS there always is).
    if command -v nasm >/dev/null 2>&1 \
       && nasm -v 2>/dev/null | grep -q 'NASM version [3-9]'; then
        report nasm3 "$(nasm -v 2>/dev/null | head -1) (plain nasm is 3.x)"
    elif [ -n "${OS88_NASM3:-}" ] || [ -x "$HERE/../build/nasm3/nasm/nasm" ]; then
        report nasm3 "present"
    else
        report nasm3 "absent - the \`nasm3\` soak row SKIPs. Not a build
                     dependency; tools/setup-nasm3.sh builds one"
    fi
}

MISSING=$(missing_list || true)

if [ "$CHECK" = 1 ]; then
    status
    [ -z "$MISSING" ] && { say ""; say "All present."; exit 0; }
    warn ""
    warn "MISSING: $(echo $MISSING)"
    warn "Fix it in one command:  tools/setup-linux.sh"
    exit 1
fi

if [ -z "$MISSING" ]; then
    status
    say ""
    say "All present - nothing to do."
    [ "$WITH_CC" = 1 ] && exec "$HERE/setup-cc.sh"
    exit 0
fi

# --- apt, and the two traps -------------------------------------------------
if ! command -v apt-get >/dev/null 2>&1; then
    warn "setup-linux.sh: no apt-get here, so install these by hand:"
    warn "    $(echo $MISSING)"
    warn "  (they are: the assembler; pkg-config + the udev headers the"
    warn "   serialport crate wants; and qemu's i386 system emulator)"
    exit 1
fi

SUDO=
if [ "$(id -u)" != 0 ]; then
    command -v sudo >/dev/null 2>&1 || {
        warn "setup-linux.sh: not root and no sudo - install by hand:"
        warn "    apt-get install -y --no-install-recommends $(echo $MISSING)"
        exit 1
    }
    SUDO=sudo
fi

# TRAP 1: THE SANDBOX. apt drops to the unprivileged `_apt` user to fetch and
# verify, and in a container whose filesystem that user cannot traverse the
# verify fails claiming gpgv is missing while `gpgv --version` answers
# perfectly well. The refresh then touches nothing and the install 404s
# exactly as it does with no refresh at all. `APT::Sandbox::User=root` is the
# whole cure and costs nothing when the sandbox was fine, so it is
# unconditional rather than a fallback.
APT="$SUDO apt-get -o APT::Sandbox::User=root"

# TRAP 2: THE STALE INDEX. A container's shipped index names package versions
# that have been superseded and dropped from the pool, so installing straight
# off 404s. A refresh is the whole fix for that direction.
#
# `W: Some index files failed to download` is NOT a reliable tell of trap 1,
# and this is worth knowing before diagnosing one: a blocked third-party PPA
# prints the identical warning (two deadsnakes/ondrej PPAs 403 through the
# agent proxy on this very container) while the main archive fetched all
# 10.9MB perfectly. Judge the refresh by whether the INSTALL then works.
say "==> refreshing the package index"
$APT update >/dev/null 2>&1 || warn "    (apt-get update complained; carrying on)"

apt_install() {
    $APT install -y --no-install-recommends "$@" >/dev/null 2>&1
}

for pkg in $MISSING; do
    case "$pkg" in
    qemu-system-x86)
        # THE OPPOSITE CURE, kept as a FALLBACK rather than a rule. There was
        # a window in which the index named a `-updates` qemu whose .deb 404'd
        # on archive.ubuntu.com and then timed out against security.ubuntu.com,
        # so a plain install burned several minutes and failed, and the cure
        # was to pin all three packages to the BASE noble version (`-t noble`
        # is NOT enough - it still resolves to -updates). That window has
        # closed: on 2026-09-08 both .debs answer 200 and the plain install is
        # 8 seconds. So try plain, and pin only if plain fails.
        say "==> installing qemu-system-x86"
        if apt_install qemu-system-x86; then
            :
        else
            V=$(apt-cache madison qemu-system-x86 2>/dev/null \
                 | awk -F'|' '/noble\/main/ {gsub(/ /,"",$2); print $2; exit}')
            [ -n "${V:-}" ] || V='1:8.2.2+ds-0ubuntu1'
            warn "    plain install failed; pinning the BASE noble version $V"
            apt_install "qemu-system-x86=$V" "qemu-system-common=$V" \
                        "qemu-system-data=$V" || {
                warn "    qemu could not be installed. It is NOT needed for"
                warn "    \`make\`, \`make marty\` or any MartyPC row - only for"
                warn "    docs/TESTING.md's seven QEMU-only cases. Carrying on."
            }
        fi
        ;;
    libudev-dev)
        # ...and this is the direction the refresh above fixes: libudev-dev
        # wants the NEWER version a refreshed index names. Applying qemu's
        # cure here reinstates the 404 you are escaping, which is why the two
        # are separate cases and not one loop.
        say "==> installing libudev-dev pkg-config  (the serialport crate's)"
        apt_install libudev-dev pkg-config || {
            warn "setup-linux.sh: libudev-dev would not install."
            warn "  \`make marty\` WILL fail several minutes in, inside cargo,"
            warn "  on serialport's build script. Everything else still works."
        }
        ;;
    pkg-config) : ;;   # installed alongside libudev-dev above
    python3-pil)
        # The archive's Pillow is built for the ARCHIVE'S python3, and the
        # `python3` on PATH need not be that one: this container runs a
        # /usr/local 3.11 against a distro 3.12, and apt's copy then imports
        # as a package whose `_imaging` extension will not load. So apt first,
        # and pip for the interpreter the tools actually run under if that
        # did not produce a working import. `--ignore-installed` because that
        # interpreter also has the archive's dist-packages on its path, so a
        # plain pip calls the broken copy "already satisfied" and does nothing.
        say "==> installing Pillow  (pxsshots' photographs)"
        apt_install python3-pil || :
        have_pil || python3 -m pip install -q --ignore-installed pillow \
            >/dev/null 2>&1 || :
        have_pil || warn "setup-linux.sh: Pillow would not install; only the pxsshots row needs it"
        ;;
    *)
        say "==> installing $pkg"
        apt_install "$pkg" || { warn "setup-linux.sh: $pkg would not install"; exit 1; }
        ;;
    esac
done

say ""
status

STILL=$(missing_list || true)
if [ -n "$STILL" ]; then
    warn ""
    warn "STILL MISSING: $(echo $STILL)"
    [ "$STILL" = "qemu-system-x86" ] && {
        warn "(qemu alone: \`make\`, \`make marty\` and every MartyPC row are fine.)"
        exit 0
    }
    exit 1
fi

say ""
say "Ready. \`make\` builds the floppies; \`make marty\` builds the debugger."
[ "$WITH_CC" = 1 ] && exec "$HERE/setup-cc.sh"
exit 0
