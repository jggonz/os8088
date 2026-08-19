# =============================================================================
# os8088 - apps/cc/Makefile.inc
#
# The build rules for a C package (SPEC.md 73.1, 67.10). A fragment, not a
# Makefile: the top-level Makefile owns the tree's targets, and this is the
# one line it needs to gain C -
#
#     include apps/cc/Makefile.inc
#
# placed anywhere after $(BUILD) and $(NASM) are defined (it uses both, and
# nothing else of the top-level file's). It adds no default work: `make` with
# this included builds exactly what it built before, because nothing in `all`
# depends on anything here. `make cc-smoke` is the one entry point.
#
# -----------------------------------------------------------------------------
# THE CHAIN, AND WHY IT IS FOUR STEPS AND NOT ONE
# -----------------------------------------------------------------------------
#
#   apps/<pkg>/<pkg>.c        the program
#        | smlrcc -tiny -S                      (1) C -> NASM, but 80386 NASM
#   build/<pkg>.raw.asm
#        | tools/cc8086.py                      (2) -> 8086, or REFUSE
#   build/<pkg>.gen.asm
#        | nasm -f bin, from apps/<pkg>/<pkg>.asm which %includes
#        |   apps/cc/crt0.asm  (header, sections, trampolines, os88thunk.asm)
#        |   and then this file                 (3) one assembly, no linker
#   build/<pkg>.bin
#        | tools/os88pkg.py                     (4) validate and stamp
#   build/<pkg>.o88
#        | tools/os88disk.py                        FAT12, three geometries
#   build/<pkg>.img
#
# Steps 3, 4 and 5 are the pipeline every assembly package already goes
# through, unchanged and unaware that the source was C - which is the test of
# whether this was done right. Only the first two are new:
#
#  (1) SmallerC EMITS NASM, which is why it was chosen: every other 16-bit C
#      compiler emits an object file, an object file needs a 16-bit linker,
#      and a linker is exactly what CLAUDE.md's "no linker - everything is
#      `nasm -f bin` flat binaries" exists to keep out. The three flags are
#      load-bearing: -tiny is the one-segment model, -S stops at assembly, and
#      -nopp must NOT be passed (it disables the preprocessor, and the failure
#      is a parse error a long way from the cause).
#
#  (2) SmallerC targets an 80386 even in 16-bit mode: about 1% of the
#      instructions it emits are 80186 or 80386 forms, and some of the C it
#      accepts is silently wrong on this OS. tools/cc8086.py lowers the first
#      and refuses the second, and IT IS NOT OPTIONAL - see the --no-gate note
#      at the foot of this file.
#
# ONE TRANSLATION UNIT, ALWAYS. `nasm -f bin` has no notion of an external
# symbol, so a C package cannot be several .c files linked together. What it
# can be is one .c file plus the hand-written runtime %included into the same
# nasm job, which is the arrangement the callback trampolines need anyway.
#
# -----------------------------------------------------------------------------
# THE COMPILER IS NOT IN THIS TREE
# -----------------------------------------------------------------------------
# tools/setup-cc.sh fetches SmallerC at its pinned commit into build/cc/ and
# builds it there. build/ is gitignored, so no compiler binary and no compiler
# source is ever committed: what is committed is the pinned hash and
# tools/cc8086.py. Every rule below depends on $(CC_SMLRCC) and the guard rule
# prints the one command to run if it is missing, rather than failing with
# "no such file".
# =============================================================================

CC_DIR      := apps/cc
CC_SC       := $(BUILD)/cc/SmallerC
CC_SMLRCC   := $(CC_SC)/smlrcc
CC_SCINC    := $(CC_SC)/v0100/include

# Every C package depends on these four: the SDK header, the two runtime
# assemblies, and the SDK the runtime is written against.
CC_RUNTIME  := $(CC_DIR)/os88.h $(CC_DIR)/crt0.asm $(CC_DIR)/os88thunk.asm \
               apps/os88api.inc

# CC_MAXFRAME is tools/cc8086.py's --max-frame (SPEC.md 73.8). 96 is the
# tool's own default and it is a SMELL THRESHOLD, not a budget: a 96-byte
# frame in this OS is a local buffer, a local buffer is either an `&local`
# (refused outright) or dead weight that should have been static, and on a
# worker it is over a third of the whole 256-byte stack with the tick and
# mouse ISR frames still to land on top. Lowering it for a package that runs
# on a worker is a normal thing to do; raising it is an argument to have in
# SPEC.md 73.8.
CC_MAXFRAME ?= 96

# One newline, so that the $(if) in the packaging rule below can expand to two
# recipe lines rather than one. GNU make has no other spelling of it.
define CC_NL


endef

# -----------------------------------------------------------------------------
# CC_PACKAGE - the four rules for one C package.
#   $(eval $(call CC_PACKAGE,<name>,<directory under apps/>[,<NAME>.OVL]))
# expects apps/<dir>/<name>.c and apps/<dir>/<name>.asm (the shim - see the
# head of apps/cc/crt0.asm, and apps/cc/ccsmoke.asm for a filled-in one).
#
# The nasm line carries -I apps/ so `%include "cc/crt0.asm"` resolves the same
# way every other package's includes do, and -I $(BUILD)/ so the shim can name
# the generated assembly without knowing where the build tree is.
#
# THE THIRD ARGUMENT IS THE OVERLAY (SPEC.md 73.14), and it is the file name
# the module ships under - `CWORD.OVL`, matching CC_PKG_NAME, because that is
# the name crt0.asm's loader asks the file API for. Given it, one step goes in
# between nasm and os88pkg.py: tools/os88ovl.py cuts the assembled image at the
# size its own header carries, writes the tail as that file, and os88pkg.py
# stamps the trimmed head. Given nothing, the chain is exactly what it was and
# not one byte of any existing package changes - which is the property to keep,
# because every C package in the tree predates this.
#
# The cut is NOT optional for a package that has one: a build that skipped it
# would ship a .o88 whose header says one size and whose file is another, and
# os88pkg.py refuses exactly that. So the two halves cannot drift apart
# silently in this direction either.
# -----------------------------------------------------------------------------
define CC_PACKAGE

$(BUILD)/$(1).raw.asm: apps/$(2)/$(1).c $(CC_RUNTIME) | $(BUILD) cc-toolchain
	PATH="$(CURDIR)/$(CC_SC):$$$$PATH" $(CC_SMLRCC) -tiny -S \
		-SI $(CC_SCINC) -I $(CC_SCINC) -I $(CC_DIR) \
		apps/$(2)/$(1).c -o $$@

$(BUILD)/$(1).gen.asm: $(BUILD)/$(1).raw.asm tools/cc8086.py
	python3 tools/cc8086.py $$< -o $$@ --max-frame $(CC_MAXFRAME)

$(BUILD)/$(1).bin: apps/$(2)/$(1).asm $(BUILD)/$(1).gen.asm $(CC_RUNTIME) | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I $(BUILD)/ -o $$@ apps/$(2)/$(1).asm
	@echo "$(1): $$(call FILESIZE,$$@) bytes"

$(BUILD)/$(1).o88: $(BUILD)/$(1).bin tools/os88pkg.py $(if $(3),tools/os88ovl.py)
	$(if $(3),python3 tools/os88ovl.py $$< -o $(BUILD)/$(3) --trim $(BUILD)/$(1).trim.bin$(CC_NL)	python3 tools/os88pkg.py $(BUILD)/$(1).trim.bin -o $$@,python3 tools/os88pkg.py $$< -o $$@)

endef

# -----------------------------------------------------------------------------
# The guard. `make cc-smoke` on a tree that has never run tools/setup-cc.sh
# should say so in one line, not fail somewhere inside a recipe.
# -----------------------------------------------------------------------------
.PHONY: cc-toolchain
cc-toolchain:
	@test -x $(CC_SMLRCC) || { \
	  echo "";                                                             \
	  echo "The C compiler is not built. It is not in this tree - build/"; \
	  echo "is gitignored and nothing under it is ever committed. Run:";   \
	  echo "";                                                             \
	  echo "    tools/setup-cc.sh";                                        \
	  echo "";                                                             \
	  echo "It fetches SmallerC at its pinned commit, builds three";       \
	  echo "binaries, and runs a canary C file through the whole chain.";  \
	  echo ""; exit 1; }

# -----------------------------------------------------------------------------
# CCSMOKE - the C toolchain's smoke test (apps/cc/ccsmoke.c), and the worked
# example of the two files a C package is made of.
#
#     make cc-smoke                            builds build/ccsmoke.img
#     make test TESTAPPS=build/ccsmoke.img     boots with it in B:
#
# Then: double-click Disk B, double-click CCSMOKE.O88, and click in the
# window. The counter is the point - it proves the click trampoline pushed its
# arguments in the right order, which is the sort of thing that assembles
# perfectly while being wrong.
#
# It is NOT on any shipped floppy and nothing in `all` reaches it, in the same
# way and for the same reason as everything under tests/. It is here rather
# than under tests/ because it is also the SDK's example: a C author reads
# apps/cc/ccsmoke.c and apps/cc/ccsmoke.asm as the template, and a template
# that is not built is a template that rots.
# -----------------------------------------------------------------------------
$(eval $(call CC_PACKAGE,ccsmoke,cc))

$(BUILD)/ccsmoke.img: $(BUILD)/ccsmoke.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/ccsmoke.o88
	@python3 tools/os88disk.py --verify $@

.PHONY: cc-smoke
cc-smoke: $(BUILD)/ccsmoke.img

# -----------------------------------------------------------------------------
# TWO THINGS THAT MUST NOT DRIFT (SPEC.md 73.10)
#
# * NO RULE THAT PRODUCES AN .o88 MAY PASS --no-gate. That flag lowers the
#   instruction set without enforcing the &local rule, the ES-string rule, the
#   float rule or the 32-bit rule; it exists to measure a corpus that is never
#   going to run here. A build target that passes it is the bug, not the
#   package it produced. `grep -n no-gate` over the Makefiles is the check,
#   and it should stay empty.
#
# * THE FRAME REPORT IS THE POINT OF THE BUILD BEING NOISY. cc8086.py prints
#   every function's frame size on every build because the number that
#   actually matters is a SUM over a call chain, the chain runs through kernel
#   dispatches no static analysis can see, and the only defence is an author
#   who notices the figures growing (SPEC.md 73.8, and 15.1's idea that the
#   guard which works is the one that prints on every build). Do not add
#   --quiet to these rules to tidy the output.
# =============================================================================
