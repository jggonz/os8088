#!/bin/sh
# =============================================================================
# os8088 - apps/infones/hosttest/nisystest.sh
#
# THE WHOLE-EMULATOR ROM GATE (SPEC.md 91.14.4) - `make nisystest`.
#
# A SKELETON, AND WAVE 2 FILLS IT, for the reason its .asm's header gives at
# length: this gate runs the PACKAGE - the compiled C included - through
# blargg's $6000 protocol, and there is no PPU and no frame loop to run until
# wave 2 writes them. It exists from wave 1 so that the Makefile's target and
# its prerequisite list are written once.
#
# It is NOT in apps/infones/build.sh: it boots the OS under QEMU and reads the
# result back over QMP, which is minutes rather than seconds.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."

echo "nisystest: wave 2's gate - the PPU and the frame loop it runs do not"
echo "           exist yet, and apps/infones/hosttest/nisystest.asm says so."
echo "           The CPU's own gate is \`make nicputest\`, which is real and"
echo "           runs nestest.log's 8,991 lines today."
exit 1
