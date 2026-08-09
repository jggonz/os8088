# Open Watcom hosted on macOS - dropped into the kernel copy's mkfiles/ by
# tools/build-freedos.sh and selected with COMPILER=owosx (SPEC.md 59.6).
#
# Everything about the 16-bit code generation is owlinux's; the two things
# that differ are the host, and they differ for the same reason: Open Watcom
# ships a Darwin *host* build (binaries that RUN on macOS/arm64) but has no
# Darwin *code generator*, so anything that has to run on this machine has to
# be built by the machine's own compiler.
include "../mkfiles/owlinux.mak"

# LOAD-BEARING, AND SILENT WHEN WRONG. mkfiles/generic.mak passes only
# -D$(COMPILER) to nasm, while kernel/segs.inc and kernel/asmsupt.asm test
# `%ifdef owlinux` to select the WATCOM segment layout. A new profile name
# arrives without that define, the assembler quietly takes the non-Watcom
# TGROUP/IGROUP path, and the kernel still LINKS - it is simply the wrong
# kernel. Renaming this profile means keeping this line.
NASMFLAGS=$(NASMFLAGS) -Dowlinux

# The host-side helpers (utils/exeflat.exe, utils/patchobj.com) run HERE, so
# clang builds them. gnu89 because the sources predate C99 declarations.
CLDEF=1
CLT=cc -o $@ -w -std=gnu89
CLC=$(CLT)
