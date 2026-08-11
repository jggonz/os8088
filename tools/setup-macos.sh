#!/usr/bin/env bash
#
# setup-macos.sh - install everything `make`, `make run` and `make xt` need
#                  on a Mac, Apple Silicon or Intel.
#
# Four things get installed and one gets DOWNLOADED, which is the whole
# reason this script exists:
#
#   nasm        the assembler (see the version gate below - the 3.0 floor
#               this script shipped with does NOT apply to this branch, and
#               the comment there says why)
#   qemu        qemu-system-i386, for `make run` / `make test`
#   python3     the host-side tooling in tools/ - stdlib only, no pip
#   86Box       optional, for `make xt` and the nine other period machines
#   86Box ROMs  NOT shipped with 86Box, from github.com/86Box/roms. Without
#               them 86Box starts and every machine in vm/ fails to boot.
#
# Nothing here is installed for the Frotz gates, and two of them want more:
# `make ztest` and `make zcheck` need `inform` and `dfrotz` (`brew install
# inform6 frotz`), and `make zscreens` - which RE-TAKES the golden screens the
# graphics gate compares against, and is the one tool in tools/ that is not
# stdlib-only - additionally needs `pip3 install pyte`. None of them is on the
# path of `make`, `make run` or any gate that runs by default: the goldens are
# committed precisely so that `make zgfx` stays stdlib + qemu (SPEC.md 61.14).
#
# Usage:
#   tools/setup-macos.sh                 install everything, ask before big steps
#   tools/setup-macos.sh --yes           ...without asking
#   tools/setup-macos.sh --dry-run       print what it would do, change nothing
#   tools/setup-macos.sh --no-86box      just the build/QEMU toolchain
#   tools/setup-macos.sh --roms-only     just (re)install the 86Box ROM set
#   tools/setup-macos.sh --force-roms    re-download ROMs even if present
#   tools/setup-macos.sh --no-build      skip the closing `make` smoke test
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The Makefile hard-codes this path (BOX := ...), so the cask's install
# location is not a preference - a 86Box anywhere else will not be found.
BOX_APP="/Applications/86Box.app"
BOX_BIN="$BOX_APP/Contents/MacOS/86Box"

# Where 86Box 6.x looks for ROMs on macOS. The pre-Qt builds used the
# ~/Library/Application Support/86Box/roms spelling, so both are set up:
# the real tree here, a symlink there.
ROM_DIR="$HOME/Library/Application Support/net.86box.86Box/roms"
ROM_DIR_LEGACY="$HOME/Library/Application Support/86Box/roms"

# The minimum nasm this tree assembles under (CONTRIBUTING.md).
#
# THIS IS 2 HERE AND 3 ON main, and the difference is real rather than a
# merge accident. The 3.0 floor exists for one construct - `call far SEG:OFF`
# as an IMMEDIATE, which every nasm 2.11 through 2.16.03 rejects with
# "mismatch in operand sizes" - and this branch has none: SPEC.md 33 retired
# far code (kernel/farcall.inc with it) and SPEC.md 28 moved the Task Manager
# out to apps/taskmgr, and those two files were where the form lived. Every
# far call left is memory-indirect (`call far [bx+DRVR_DISP]`), which 2.x has
# always taken. Verified rather than assumed: nasm 2.16.01 builds this tree
# with zero warnings and reproduces all 41 artifacts the branch used to ship
# byte for byte. Older 2.x is untested, so the gate still exists - and if the
# immediate form ever comes back, this goes to 3 and the message below stops
# being a footnote.
NASM_MIN_MAJOR=2

# The ROM subdirectories vm/*/86box.cfg actually names. Checked after the
# download, because a truncated tarball extracts happily and then every
# `make xt` dies at the BIOS with nothing on screen to explain it.
REQUIRED_ROMS=(
	machines/ibmxt          # make xt, xt-cga, xt-hercules, xt-sound
	machines/ibmxt86        # make xt-640 (1986 board: takes 640KB)
	machines/ami286         # make 286, 286-sound
	machines/shuttle386sx   # make 386sx
	machines/micronics386   # make 386, 386-sound
	video/oti               # the OTI-067 VGA on eight of the ten machines
)

DRY_RUN=0
ASSUME_YES=0
WANT_86BOX=1
ROMS_ONLY=0
FORCE_ROMS=0
WANT_BUILD=1

# --------------------------------------------------------------------------
# plumbing
# --------------------------------------------------------------------------

if [ -t 1 ]; then
	B=$'\033[1m'; G=$'\033[32m'; Y=$'\033[33m'; R=$'\033[31m'; Z=$'\033[0m'
else
	B=''; G=''; Y=''; R=''; Z=''
fi

step() { printf '\n%s==> %s%s\n' "$B" "$*" "$Z"; }
ok()   { printf '    %s.%s %s\n' "$G" "$Z" "$*"; }
warn() { printf '    %s!%s %s\n' "$Y" "$Z" "$*"; }
die()  { printf '\n%serror:%s %s\n' "$R" "$Z" "$*" >&2; exit 1; }

# Every state-changing command goes through this, so --dry-run is honest
# rather than approximately honest.
run() {
	if [ "$DRY_RUN" = 1 ]; then
		printf '    %s+%s %s\n' "$Y" "$Z" "$*"
		return 0
	fi
	printf '    %s+%s %s\n' "$G" "$Z" "$*"
	"$@"
}

# Ask, unless --yes or --dry-run. Answers "no" on a non-interactive stdin
# rather than blocking a CI/ssh run forever.
confirm() {
	[ "$ASSUME_YES" = 1 ] && return 0
	[ "$DRY_RUN" = 1 ] && return 0
	[ -t 0 ] || { warn "not a terminal, skipping: $1"; return 1; }
	local reply
	read -r -p "    $1 [Y/n] " reply || return 1
	case "$reply" in ''|y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

usage() { sed -n '3,26p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
	case "$1" in
		--dry-run)    DRY_RUN=1 ;;
		--yes|-y)     ASSUME_YES=1 ;;
		--no-86box)   WANT_86BOX=0 ;;
		--roms-only)  ROMS_ONLY=1 ;;
		--force-roms) FORCE_ROMS=1 ;;
		--no-build)   WANT_BUILD=0 ;;
		--help|-h)    usage ;;
		*)            die "unknown option: $1 (try --help)" ;;
	esac
	shift
done

[ "$DRY_RUN" = 1 ] && printf '%s(dry run: nothing will be installed)%s\n' "$Y" "$Z"

# --------------------------------------------------------------------------
# 0. the machine
# --------------------------------------------------------------------------

step "Checking the machine"
[ "$(uname -s)" = "Darwin" ] || die "this script is for macOS; see CONTRIBUTING.md for Linux and Windows"

ARCH="$(uname -m)"
ok "macOS $(sw_vers -productVersion) on $ARCH"
if [ "$ARCH" = "arm64" ]; then
	# Worth stating, because the instinct on an ARM Mac is to reach for
	# Rosetta and neither of these two needs it: Homebrew's qemu is a
	# native arm64 binary that EMULATES x86, and 86Box ships universal.
	ok "Apple Silicon: qemu and 86Box are both native here - no Rosetta needed"
fi

# --------------------------------------------------------------------------
# 1. Xcode Command Line Tools (make, git, perl, cc)
# --------------------------------------------------------------------------

step "Checking Xcode Command Line Tools (make, git)"
if xcode-select -p >/dev/null 2>&1; then
	ok "installed at $(xcode-select -p)"
else
	warn "not installed - make and git come from here"
	if confirm "run xcode-select --install now?"; then
		run xcode-select --install || true
		die "finish the Command Line Tools installer, then re-run this script"
	else
		die "install them with: xcode-select --install"
	fi
fi

for t in make git perl curl tar; do
	command -v "$t" >/dev/null 2>&1 || die "$t not found in PATH"
done
ok "make, git, perl, curl, tar all present"

# --------------------------------------------------------------------------
# 2. Homebrew
# --------------------------------------------------------------------------

step "Checking Homebrew"
if ! command -v brew >/dev/null 2>&1; then
	# Deliberately not run for you: installing Homebrew is a bigger
	# decision than installing an assembler, and it wants its own sudo.
	cat <<-EOF

	    Homebrew is not installed. Install it, then re-run this script:

	      /bin/bash -c "\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

	    On Apple Silicon it lands in /opt/homebrew and its installer
	    prints the two lines to add to your shell profile. Do that, open
	    a new shell, and \`brew --version\` should answer.

	EOF
	die "Homebrew required"
fi
ok "$(brew --version | head -1) at $(brew --prefix)"

# --------------------------------------------------------------------------
# 3. the build toolchain: nasm, qemu, python3
# --------------------------------------------------------------------------

brew_install() {   # brew_install <formula> <binary-to-look-for>
	if command -v "$2" >/dev/null 2>&1; then
		ok "$2 already present: $(command -v "$2")"
		return 0
	fi
	run brew install "$1"
}

if [ "$ROMS_ONLY" = 0 ]; then
	step "Installing the build toolchain (nasm, qemu, python3)"
	brew_install nasm    nasm
	brew_install qemu    qemu-system-i386
	brew_install python3 python3

	# The nasm version gate. It matters much less on this branch than on
	# main - see NASM_MIN_MAJOR above for why the floor is 2 here - but a
	# shadowed nasm is still the usual way to end up with a tool-shaped
	# failure that reads like broken source, so the version is reported
	# either way and Apple's /usr/local/bin or an old conda env is the
	# first place to look when one turns up.
	step "Checking nasm version (this tree needs ${NASM_MIN_MAJOR}.0 or newer)"
	if [ "$DRY_RUN" = 1 ] && ! command -v nasm >/dev/null 2>&1; then
		warn "nasm not installed yet; the version gate runs on the real pass"
	else
		NASM_BIN="$(command -v nasm)"
		NASM_VER="$(nasm -v 2>/dev/null | sed -n 's/^NASM version \([0-9][0-9.]*\).*/\1/p')"
		[ -n "$NASM_VER" ] || die "could not read a version out of \`nasm -v\`"
		NASM_MAJOR="${NASM_VER%%.*}"
		if [ "$NASM_MAJOR" -lt "$NASM_MIN_MAJOR" ]; then
			printf '\n'
			warn "nasm $NASM_VER at $NASM_BIN is too old."
			warn "When an assembly failure here looks like broken source,"
			warn "suspect the TOOL first - a shadowed nasm is the usual cause."
			if BREW_NASM="$(brew --prefix nasm 2>/dev/null)/bin/nasm" && [ -x "$BREW_NASM" ]; then
				warn "Homebrew's nasm is at $BREW_NASM ($("$BREW_NASM" -v | awk '{print $3}'))"
				warn "and is being shadowed. Put $(dirname "$BREW_NASM") ahead of"
				warn "$(dirname "$NASM_BIN") in PATH, or build with:"
				warn "    make NASM=$BREW_NASM"
			fi
			die "nasm ${NASM_MIN_MAJOR}.0+ required"
		fi
		ok "nasm $NASM_VER at $NASM_BIN"
	fi

	command -v python3 >/dev/null 2>&1 && ok "python3 $(python3 -V 2>&1 | awk '{print $2}') (tools/ is stdlib-only: no pip, no venv)"
	command -v qemu-system-i386 >/dev/null 2>&1 && ok "$(qemu-system-i386 --version | head -1)"
fi

# --------------------------------------------------------------------------
# 4. 86Box
# --------------------------------------------------------------------------

if [ "$WANT_86BOX" = 1 ]; then
	step "Checking 86Box (needed only for make xt / xt-640 / xt-cga / xt-hercules / 286 / 386sx / 386 / *-sound)"
	if [ -x "$BOX_BIN" ]; then
		ok "already installed: $BOX_APP"
	elif [ -d "$BOX_APP" ]; then
		warn "$BOX_APP exists but $BOX_BIN is missing - the Makefile calls that exact path"
	else
		if confirm "install 86Box with \`brew install --cask 86box\`?"; then
			run brew install --cask 86box
		else
			warn "skipped; \`make xt\` and friends will not work"
			WANT_86BOX=0
		fi
	fi

	if [ "$WANT_86BOX" = 1 ] && [ "$DRY_RUN" = 0 ] && [ ! -x "$BOX_BIN" ]; then
		warn "expected the binary at $BOX_BIN and it is not there."
		warn "The Makefile's BOX variable names that path literally; if the"
		warn "cask changed the executable name, edit BOX in the Makefile."
	fi
fi

# --------------------------------------------------------------------------
# 5. the 86Box ROM set - the step everybody misses
# --------------------------------------------------------------------------

roms_present() {
	local d
	for d in "${REQUIRED_ROMS[@]}"; do
		[ -d "$ROM_DIR/$d" ] || return 1
	done
	return 0
}

if [ "$WANT_86BOX" = 1 ] || [ "$ROMS_ONLY" = 1 ]; then
	step "Checking the 86Box ROM set"
	echo "    $ROM_DIR"

	if roms_present && [ "$FORCE_ROMS" = 0 ]; then
		ok "every machine vm/ names already has its ROMs"
	else
		# Match the ROM set to the installed 86Box: the repo tags a
		# release per version, and a ROM tree from a much older
		# 86Box can be missing a machine the new build offers.
		BOX_VER=""
		if [ -r "$BOX_APP/Contents/Info.plist" ]; then
			BOX_VER="$(defaults read "$BOX_APP/Contents/Info" CFBundleShortVersionString 2>/dev/null || true)"
		fi
		ROM_REF="refs/heads/master"; ROM_WHAT="master"
		if [ -n "$BOX_VER" ]; then
			if curl -fsSL -o /dev/null --head "https://github.com/86Box/roms/releases/tag/v$BOX_VER" 2>/dev/null; then
				ROM_REF="refs/tags/v$BOX_VER"; ROM_WHAT="v$BOX_VER (matches the installed 86Box)"
			else
				warn "no ROM tag v$BOX_VER upstream; falling back to master"
			fi
		fi
		ROM_URL="https://codeload.github.com/86Box/roms/tar.gz/$ROM_REF"
		ok "ROM set: $ROM_WHAT"

		# ~75MB. Worth asking before spending it on a metered link.
		if confirm "download the ROM set (~75MB) from github.com/86Box/roms?"; then
			if [ "$DRY_RUN" = 1 ]; then
				printf '    %s+%s curl -fL %s | tar -xz -> %s\n' "$Y" "$Z" "$ROM_URL" "$ROM_DIR"
			else
				TMP="$(mktemp -d "${TMPDIR:-/tmp}/os8088-roms.XXXXXX")"
				trap 'rm -rf "$TMP"' EXIT
				echo "    downloading $ROM_URL"
				curl -fL --progress-bar -o "$TMP/roms.tgz" "$ROM_URL" \
					|| die "ROM download failed"
				# A truncated tarball extracts partially and without
				# complaint, so test the gzip stream before trusting it.
				gzip -t "$TMP/roms.tgz" || die "downloaded ROM archive is corrupt"
				tar -xzf "$TMP/roms.tgz" -C "$TMP"
				SRC="$(find "$TMP" -maxdepth 1 -type d -name 'roms-*' | head -1)"
				[ -n "$SRC" ] || die "unexpected archive layout - no roms-* directory inside"

				mkdir -p "$ROM_DIR"
				# Copy the CONTENTS in; do not replace the directory,
				# so a hand-added ROM already in there survives.
				(cd "$SRC" && tar -cf - --exclude='.github' .) | (cd "$ROM_DIR" && tar -xf -)
				rm -rf "$TMP"; trap - EXIT
				ok "extracted into $ROM_DIR"
			fi
		else
			warn "skipped - 86Box will start, but every vm/ machine will fail to boot"
		fi

		if [ "$DRY_RUN" = 0 ] && [ -d "$ROM_DIR" ]; then
			MISSING=0
			for d in "${REQUIRED_ROMS[@]}"; do
				if [ -d "$ROM_DIR/$d" ]; then
					ok "$d"
				else
					warn "MISSING: $d"
					MISSING=1
				fi
			done
			[ "$MISSING" = 0 ] || warn "some machines in vm/ will not boot until those are present"
		fi
	fi

	# The pre-Qt macOS builds read ~/Library/Application Support/86Box/roms.
	# A symlink costs nothing and means a 86Box installed some other way
	# (the .dmg from 86box.net, say) finds the same tree.
	if [ "$DRY_RUN" = 0 ] && [ -d "$ROM_DIR" ] && [ ! -e "$ROM_DIR_LEGACY" ]; then
		mkdir -p "$(dirname "$ROM_DIR_LEGACY")"
		ln -s "$ROM_DIR" "$ROM_DIR_LEGACY" 2>/dev/null \
			&& ok "linked the legacy path $ROM_DIR_LEGACY -> the same tree" || true
	fi
fi

# --------------------------------------------------------------------------
# 6. smoke test: actually build the six floppies
# --------------------------------------------------------------------------

if [ "$ROMS_ONLY" = 0 ] && [ "$WANT_BUILD" = 1 ]; then
	step "Building (make) to prove the toolchain works"
	if [ "$DRY_RUN" = 1 ]; then
		printf '    %s+%s make -C %s\n' "$Y" "$Z" "$REPO"
	else
		( cd "$REPO" && make ) || die "\`make\` failed - see the output above"
		for img in build/os8088.img build/os8088-720.img build/os8088-360.img \
		           build/apps.img build/apps720.img build/apps360.img; do
			[ -f "$REPO/$img" ] && ok "$img ($(stat -f%z "$REPO/$img") bytes)" \
			                    || warn "$img was not produced"
		done
	fi
fi

# --------------------------------------------------------------------------
# done
# --------------------------------------------------------------------------

cat <<EOF

$B==> Ready.$Z

    make run          boot it in QEMU (opens a window, emulated serial mouse)
    make test         boot it headless, QMP socket at build/qmp.sock
    make xt           boot the 360KB image on an emulated IBM PC/XT in 86Box

  Two things about the 86Box targets that are not failures:

  - The AT-class machines ($B make 286 / 386sx / 386 $Z) have a CMOS the XT
    does not, and on a fresh vm/<machine>/nvr/ the BIOS stops at its setup
    screen wanting "EXIT FOR BOOT" picked once. That is a one-time cost per
    VM directory.

  - 86Box rewrites vm/*/86box.cfg when it exits, so the configs in this repo
    drift. The Makefile strips the wp:// write-protect prefix off BOTH
    floppies at every launch for exactly that reason - the system disk is a
    writable FAT12 volume too and SYSTEM.CFG lives in its root, so protected
    it silently loses every Control Panel setting at reboot (SPEC.md 19.3).
    \`git diff vm/\` after a session is normal.

  If macOS refuses to open 86Box the first time ("cannot be opened because
  the developer cannot be verified"), clear the quarantine flag:

    xattr -dr com.apple.quarantine $BOX_APP

  Toolchain notes live in CONTRIBUTING.md; the QEMU test harness and its
  traps are in docs/TESTING.md.

EOF
