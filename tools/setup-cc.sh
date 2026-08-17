#!/usr/bin/env bash
#
# setup-cc.sh - fetch and build the C compiler the C toolchain uses.
#
# One thing is downloaded and one thing is built, both into build/cc/, which
# is gitignored: **no compiler binary and no compiler source is ever committed
# to this tree.** What is committed is the pinned commit hash below and
# tools/cc8086.py, which together make the toolchain reproducible without
# vendoring 15,500 lines of somebody else's C.
#
#   SmallerC   github.com/alexfru/SmallerC, pinned to the commit in PIN.
#              2-clause BSD, so redistributable with attribution - and this
#              script prints the licence path rather than assuming anybody
#              went looking. It emits NASM syntax, which is the whole reason
#              it was chosen: the output drops straight into this repo's
#              `nasm -f bin` pipeline and Apple's Mach-O-only toolchain stays
#              out of it (CLAUDE.md, "Commands").
#
# SmallerC targets an 80386 even in its 16-bit mode, so its output is NOT
# runnable here as it stands - about 1% of the instructions it emits are
# 80186 or 80386 forms. tools/cc8086.py lowers those to the 8086 and refuses
# the constructs that have no 8086 answer. This script finishes by running a
# canary C file all the way through BOTH, and assembling the result under
# `cpu 8086`, because "the files are present" is not the same claim as "the
# toolchain works".
#
# Usage:
#   tools/setup-cc.sh                 fetch, build, verify
#   tools/setup-cc.sh --check         report what is there, change nothing
#   tools/setup-cc.sh --force         discard build/cc and start over
#   tools/setup-cc.sh --print-path    print the compiler's bin directory
#   tools/setup-cc.sh --no-verify     skip the closing canary build
#
# Idempotent: run it as often as you like. It re-fetches only when build/cc
# is missing or is not at PIN, and rebuilds only when a binary is missing or
# older than its source.
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The pinned commit. Bumping this is a decision, not a maintenance chore:
# tools/cc8086.py's census of what the compiler emits - the seven non-8086
# forms and the register-liveness argument behind the `push imm` rewrite - was
# measured against THIS commit. A newer compiler may emit an eighth form, and
# the closing sweep in cc8086.py will refuse it by name rather than let it
# through, but somebody has to go and add the lowering.
PIN="1865d79ce7a5ad3f8a9515a571437cee084b8b1d"
URL="https://github.com/alexfru/SmallerC.git"

CC_DIR="$REPO/build/cc"
SRC="$CC_DIR/SmallerC"
# smlrl is the linker and n2f a format converter; neither is on this repo's
# path, because a package is `nasm -f bin` and there is no linker (CLAUDE.md).
# smlrpp is, though: it is the preprocessor, so without it `#include` fails.
BINS=(smlrc smlrcc smlrpp)

FORCE=0
CHECK=0
VERIFY=1

if [ -t 1 ]; then
	B=$'\033[1m'; G=$'\033[32m'; Y=$'\033[33m'; R=$'\033[31m'; Z=$'\033[0m'
else
	B=''; G=''; Y=''; R=''; Z=''
fi

step() { printf '\n%s==> %s%s\n' "$B" "$*" "$Z"; }
ok()   { printf '    %s.%s %s\n' "$G" "$Z" "$*"; }
warn() { printf '    %s!%s %s\n' "$Y" "$Z" "$*"; }
die()  { printf '\n%serror:%s %s\n' "$R" "$Z" "$*" >&2; exit 1; }
run()  { printf '    %s+%s %s\n' "$G" "$Z" "$*"; "$@"; }

usage() { sed -n '3,37p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
	case "$1" in
		--force)      FORCE=1 ;;
		--check)      CHECK=1 ;;
		--no-verify)  VERIFY=0 ;;
		--print-path) echo "$SRC"; exit 0 ;;
		--help|-h)    usage ;;
		*)            die "unknown option: $1 (try --help)" ;;
	esac
	shift
done

have_bins() {
	local b
	for b in "${BINS[@]}"; do
		[ -x "$SRC/$b" ] || return 1
	done
	return 0
}

at_pin() {
	[ -d "$SRC/.git" ] || return 1
	[ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo none)" = "$PIN" ]
}

# --------------------------------------------------------------------------
# --check: say what is there and stop
# --------------------------------------------------------------------------

if [ "$CHECK" = 1 ]; then
	step "Checking build/cc"
	if [ ! -d "$SRC" ]; then
		warn "not fetched: $SRC does not exist"
		echo; echo "    run: tools/setup-cc.sh"
		exit 1
	fi
	if at_pin; then
		ok "source at the pinned commit ${PIN:0:12}"
	else
		warn "source is at $(git -C "$SRC" rev-parse --short HEAD 2>/dev/null \
			|| echo 'an unknown commit'), not ${PIN:0:12}"
	fi
	if have_bins; then
		ok "binaries built: ${BINS[*]}"
	else
		warn "binaries missing or not executable"
	fi
	at_pin && have_bins || exit 1
	exit 0
fi

# --------------------------------------------------------------------------
# 1. the tools this needs
# --------------------------------------------------------------------------

step "Checking the host toolchain"
for t in git make nasm python3; do
	command -v "$t" >/dev/null 2>&1 \
		|| die "\`$t\` is not on PATH. On a Mac, tools/setup-macos.sh installs
    nasm and python3; git and make come with the Xcode command line tools
    (\`xcode-select --install\`)."
done
CC_HOST="${CC:-cc}"
command -v "$CC_HOST" >/dev/null 2>&1 \
	|| die "no C compiler on PATH (tried \`$CC_HOST\`; set CC= to name one).
    SmallerC is itself a C program and has to be built by a host compiler.
    On a Mac: \`xcode-select --install\`."
ok "$(git --version)"
ok "$("$CC_HOST" --version 2>/dev/null | head -1)"
ok "$(nasm -v)"

# SmallerC's GNUmakefile finds its own source directory with `readlink -f`,
# which BSD readlink only learned in macOS 12.3. Older systems fail with a
# usage message from readlink and a make error that names neither.
readlink -f / >/dev/null 2>&1 \
	|| die "\`readlink -f\` does not work here, and SmallerC's GNUmakefile
    needs it to find its own source tree. Install GNU coreutils
    (\`brew install coreutils\`) and put gnubin on PATH, or upgrade macOS."

# --------------------------------------------------------------------------
# 2. fetch
# --------------------------------------------------------------------------

if [ "$FORCE" = 1 ] && [ -d "$CC_DIR" ]; then
	step "Discarding build/cc (--force)"
	run rm -rf "$CC_DIR"
fi

step "Fetching SmallerC at ${PIN:0:12}"
if at_pin; then
	ok "already at the pinned commit; nothing to fetch"
else
	if [ -d "$SRC" ] && [ ! -d "$SRC/.git" ]; then
		die "$SRC exists but is not a git checkout. It was not put there by
    this script. Move it aside, or re-run with --force to delete it."
	fi
	run mkdir -p "$SRC"
	[ -d "$SRC/.git" ] || run git -C "$SRC" init -q
	git -C "$SRC" remote get-url origin >/dev/null 2>&1 \
		|| run git -C "$SRC" remote add origin "$URL"
	run git -C "$SRC" remote set-url origin "$URL"
	# Fetching the SHA directly keeps this to one commit's worth of download
	# and makes the pin unforgeable: a moved branch cannot satisfy it.
	if ! git -C "$SRC" fetch --depth 1 origin "$PIN" 2>&1 | sed 's/^/      /'
	then
		die "could not fetch $PIN from $URL.
    This is the one step that needs the network. If you are offline, or
    behind a proxy that blocks github.com, fetch the repository by hand into
    $SRC and check out $PIN, then re-run this script - it will see the pin
    and go straight to building."
	fi
	run git -C "$SRC" checkout -q --detach FETCH_HEAD
	at_pin || die "fetched, but HEAD is $(git -C "$SRC" rev-parse HEAD) rather
    than the pinned $PIN. Refusing to build a compiler this repo has not
    measured."
	ok "checked out $PIN"
fi
ok "licence: ${SRC#$REPO/}/license.txt (2-clause BSD)"

# --------------------------------------------------------------------------
# 3. build
# --------------------------------------------------------------------------

step "Building ${BINS[*]}"
if have_bins && [ -z "$(find "$SRC/v0100" -name '*.c' -newer "$SRC/smlrc" \
	-print -quit 2>/dev/null)" ]; then
	ok "up to date"
else
	# Only the compiler front end, the driver and the preprocessor. The
	# default target additionally builds SmallerC's DOS/Windows/Linux runtime
	# libraries, which this repo has no use for - a package links against
	# apps/os88api.inc and nothing else - and which take minutes.
	# THE FOUR LIMITS, RAISED BY A BUILD FLAG AND NOT BY A PATCH. smlrc
	# keeps its tables in fixed-size arrays and refuses when one fills:
	# `Identifier table exhausted`, and the message names no file, no line
	# and no way forward. A program of CWORD's size (SPEC.md 67.12, about
	# 5,000 lines in one translation unit, because `nasm -f bin` has no
	# notion of an external symbol) fills three of the four.
	#
	# Every one of them is #ifndef-guarded in smlrc.c, which is upstream
	# saying they are meant to be set from outside, so this needs no patch
	# and the pin stays a pin - nothing about the compiler's OUTPUT changes,
	# only how much input it will accept before it stops.
	#
	# They go through CFLAGS and not CPPFLAGS deliberately: CPPFLAGS is `+=`
	# in common.mk and carries -DPATH_PREFIX and -DHOST_MACOS, and a
	# command-line assignment would REPLACE it and quietly build a compiler
	# that cannot find its own include directory. CFLAGS is `?=`, so this
	# repeats its default and adds to it.
	#
	# Changing these numbers does not re-trigger the freshness check above,
	# which compares source mtimes: `tools/setup-cc.sh` after a `make
	# clean-cc` is how you make them take effect.
	run make -C "$SRC" -f GNUmakefile \
		CFLAGS="-pipe -Wall -O2 \
			-DMAX_IDENT_TABLE_LEN=32768 \
			-DMAX_MACRO_TABLE_LEN=16384 \
			-DSYNTAX_STACK_MAX=16384 \
			-DMAX_CASES=512" \
		"${BINS[@]}"
	have_bins || die "make finished but one of ${BINS[*]} is missing"
fi
for b in "${BINS[@]}"; do
	ok "$(cd "$SRC" && ls -l "$b" | awk '{print $NF, $5" bytes"}')"
done

# --------------------------------------------------------------------------
# 4. the canary
# --------------------------------------------------------------------------
#
# Three things can be individually fine and jointly useless here: the
# compiler can build, cc8086.py can run, and NASM can be installed, and the
# combination can still not produce an 8086 instruction stream. So the last
# thing this does is put a C file through all three.

if [ "$VERIFY" = 1 ]; then
	step "Verifying the whole chain on a canary"
	TMP="$(mktemp -d)"
	trap 'rm -rf "$TMP"' EXIT
	cat > "$TMP/canary.c" <<'EOF'
/* Every construct tools/cc8086.py has to lower, in eleven lines:
   a frame and a `leave`, a comparison the compiler answers with `setcc`,
   a constant argument it pushes as an immediate, and a multiply by a
   non-power-of-two it does with `imul r, r, imm`. */
static int table[16];
extern void sink(int a, int b);

int canary(int n)
{
	int i, hits = 0;
	for (i = 0; i < n; i++)
		hits += (table[i * 3 & 15] > i);
	sink(hits, 577);
	return hits;
}
EOF
	PATH="$SRC:$PATH" "$SRC/smlrcc" -tiny -S \
		-SI "$SRC/v0100/include" -I "$SRC/v0100/include" \
		"$TMP/canary.c" -o "$TMP/canary.asm" >/dev/null \
		|| die "SmallerC could not compile the canary. The build produced
    binaries that do not work; try --force."
	ok "smlrcc -tiny -S: $(grep -c . "$TMP/canary.asm") lines of NASM"

	grep -qE '^[[:space:]]*(leave|set[a-z]+|imul[[:space:]].*,.*,)' \
		"$TMP/canary.asm" \
		|| warn "the canary did not provoke a single non-8086 form; the
      compiler's code generator may have changed under the pin"

	python3 "$REPO/tools/cc8086.py" "$TMP/canary.asm" -o "$TMP/canary8086.asm" \
		|| die "tools/cc8086.py refused the canary. That is a real finding -
    read the message above; it names the C construct."

	# `-f bin` cannot hold an external reference, so the canary's one extern
	# is given an address before NASM sees it. `cpu 8086` is already at the
	# top of the file cc8086.py wrote, and it is what does the checking here.
	sed -e 's/^[[:space:]]*extern[[:space:]]*\(.*\)/\1 equ 0/' \
	    -e 's/^[[:space:]]*global.*//' \
	    "$TMP/canary8086.asm" > "$TMP/canary.bin.asm"
	nasm -f bin -w+error "$TMP/canary.bin.asm" -o "$TMP/canary.bin" \
		|| die "NASM refused the lowered canary under \`cpu 8086\`. This is a
    tools/cc8086.py bug, not a setup problem - please report it with
    $TMP/canary.bin.asm."
	ok "nasm -f bin -w+error under cpu 8086: $(wc -c < "$TMP/canary.bin" \
		| tr -d ' ') bytes"
fi

# --------------------------------------------------------------------------

cat <<EOF

$B==> Ready.$Z

  The compiler is at $B${SRC#$REPO/}$Z and is not tracked - build/ is
  gitignored, and nothing under it may ever be committed (CLAUDE.md).

  To compile one file by hand:

    PATH="$SRC:\$PATH" \\
    $SRC/smlrcc -tiny -S \\
        -SI $SRC/v0100/include \\
        -I  $SRC/v0100/include foo.c -o foo.raw.asm
    python3 tools/cc8086.py foo.raw.asm -o foo.asm

  ${B}The second step is not optional$Z: SmallerC emits 80186 and 80386
  instructions (\`leave\`, \`setcc\`, \`push imm\`, \`movzx\`, \`movsx\`,
  multi-bit shift-immediates and three-operand \`imul\`), and it emits code
  that takes the address of an automatic - which on this OS is a pointer
  into the wrong segment, because SS is LOW_SEG and DS is the package's.
  cc8086.py rewrites the first list and refuses the second by name.

  Run \`python3 tools/cc8086.py --help\` for the frame-size budget, and read
  the header of that file before writing C for this OS: the rules it enforces
  are short, but none of them is guessable from the C.

EOF
