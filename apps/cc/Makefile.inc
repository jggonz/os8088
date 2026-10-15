# =============================================================================
# os8088 - apps/cc/Makefile.inc
#
# The build rules for a C package (SPEC.md 73.1, 73.10). A fragment, not a
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
#        | tools/os88disk.py                     (5) FAT12, four geometries
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

# Every C package depends on these five: the SDK header, the two runtime
# assemblies, and the SDK the runtime is written against - which is two
# files since SPEC.md 20.12.9, the parts standard's code having moved into
# apps/os88partsbody.inc so it can be emitted after the package's table.
CC_RUNTIME  := $(CC_DIR)/os88.h $(CC_DIR)/crt0.asm $(CC_DIR)/os88thunk.asm \
               apps/os88api.inc apps/os88parts.inc apps/os88partsbody.inc apps/os88rseq.inc

# CC_MAXFRAME is tools/cc8086.py's --max-frame (SPEC.md 73.8). 96 is the
# tool's own default and it is a SMELL THRESHOLD, not a budget: a 96-byte
# frame in this OS is a local buffer, a local buffer is either an `&local`
# (refused outright) or dead weight that should have been static, and on a
# worker it is a quarter of the whole 384-byte stack with the tick and
# mouse ISR frames still to land on top. Lowering it for a package that runs
# on a worker is a normal thing to do; raising it is an argument to have in
# SPEC.md 73.8.
CC_MAXFRAME ?= 96

# CCPEEP is the peephole pass (tools/ccpeep.py, SPEC.md 73.15): cc8086.py runs
# it over its own lowered output, so every C package gets it through the one
# compile line below. `make CCPEEP=0` is the A/B - the package exactly as
# cc8086 lowered it - and the stamp is what makes flipping it rebuild every
# C package rather than none (a knob with no stamp boots the previous
# configuration and reads exactly like the change doing nothing).
# tools/ccverify.py is the pass's differential check, run on demand.
CCPEEP ?= 1
CC_PEEPARG := $(if $(filter 1,$(CCPEEP)),,--no-peep)
CC_PEEPSTAMP := $(BUILD)/.ccpeep-$(if $(CC_PEEPARG),off,on)

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
	PATH="$(abspath $(CC_SC)):$$$$PATH" $(CC_SMLRCC) -tiny -S \
		-SI $(CC_SCINC) -I $(CC_SCINC) -I $(CC_DIR) \
		apps/$(2)/$(1).c -o $$@

$(BUILD)/$(1).gen.asm: $(BUILD)/$(1).raw.asm tools/cc8086.py tools/ccpeep.py $(CC_PEEPSTAMP)
	python3 tools/cc8086.py $$< -o $$@ --max-frame $(CC_MAXFRAME) $(CC_PEEPARG)

$(BUILD)/$(1).bin: apps/$(2)/$(1).asm $(BUILD)/$(1).gen.asm $(CC_RUNTIME) | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I $(BUILD)/ -o $$@ apps/$(2)/$(1).asm
	@echo "$(1): $$(call FILESIZE,$$@) bytes"

# $(4) is the PARTS (SPEC.md 20.12), one file per OS88_PART row that has file
# bytes, in table order - appended by os88pkg.py after the image, and after
# the overlay has been cut off it, so the three mechanisms stack in the one
# order they can: nasm emits image+module, os88ovl.py takes the module away,
# os88pkg.py puts the parts on the end.
#
# CCPARTZ=lz4|lzb picks the format an OP_COMP row uses (SPEC.md 20.12.7).
# WHICH rows is the package's own decision - the flag is on the row, in its
# source - so this only says how, and a package with no such row is unaffected
# by it entirely.
#
# PKGZ REACHES A C PACKAGE TOO (SPEC.md 20.13.5): $(PKGZARG) is the top-level
# Makefile's `--compress-if=lz4`, the soft form, so the package's RESIDENT
# image is packed when that pays and is written plain with a line saying why
# when it does not - a package with PARTS (C64, APPLE2) and one too small to
# save a sector (ccsmoke) are the plain ones. $(PKGZSTAMP) is the stamp every
# other package rule carries, so flipping PKGZ rebuilds these too. Both expand
# to nothing in a Makefile that does not define them, which is the plain
# package this rule always built.
#
# AND THE OVERLAY SHIPS PACKED, from $(CC_OVLZ) (SPEC.md 73.14.1):
# $(BUILD)/<NAME>.OVL stays exactly what os88ovl.py cut - the plain module the
# symbol readers and tests/unit/t_ctoolchain.py read - and CC_OVLZ_RULE below
# writes the copy a DISK carries, $(CC_OVLZ)/<NAME>.OVL, as a 'CZ' file.
# cc_ovneed needs nothing for it: OSAPI_FILE_READ is transparent (SPEC.md
# 20.14) and crt0 sizes the claim from the IMAGE's own cc_modc_end, which is
# the UNPACKED size. A disk recipe names $(CC_OVLZ)/<NAME>.OVL, never
# $(BUILD)/<NAME>.OVL - tests/unit/t_pkgzrules.py fails the build otherwise.
CC_PARTZ := --part-compress $(if $(CCPARTZ),$(CCPARTZ),lz4)
$(BUILD)/$(1).o88: $(BUILD)/$(1).bin tools/os88pkg.py $(if $(3),tools/os88ovl.py) $(4) $(PKGZSTAMP)
	$(if $(3),python3 tools/os88ovl.py $$< -o $(BUILD)/$(3) --trim $(BUILD)/$(1).trim.bin$(CC_NL)	python3 tools/os88pkg.py $(PKGZARG) $(BUILD)/$(1).trim.bin -o $$@ $(CC_PARTZ) $(addprefix --part ,$(4)),python3 tools/os88pkg.py $(PKGZARG) $$< -o $$@ $(CC_PARTZ) $(addprefix --part ,$(4)))

# The overlay is also a direct prerequisite of the all-apps/live images.
# Declare its producer for fresh builds; recover it if only it was deleted.
$(if $(3),$(BUILD)/$(3): $(BUILD)/$(1).o88$(CC_NL)	@test -f $$@ || python3 tools/os88ovl.py $(BUILD)/$(1).bin -o $$@ --trim $(BUILD)/$(1).trim.bin)

$(if $(3),$(call CC_OVLZ_RULE,$(3)))

endef

# -----------------------------------------------------------------------------
# CC_OVLZ_RULE - the copy of an overlay a DISK carries, packed (SPEC.md 73.14.1)
#   $(eval $(call CC_OVLZ_RULE,<NAME>.OVL))
# CC_PACKAGE calls it for its own third argument; a package whose rules are
# open-coded in the top-level Makefile (LOOM, COVL) calls it by hand, after
# giving $(BUILD)/<NAME>.OVL a rule of its own (the empty `;` one is enough).
#
# `PKGZ=` writes a plain copy, which is the A/B the rest of the tree's PKGZ
# gives, and os88lz.py --wrap itself writes the file UNCHANGED when packing it
# would make it bigger, so no overlay can grow by this.
#
# ONE RISK, AND WHY IT IS ACCEPTED. A 'CZ' file's unpacked size lives in its
# DIRECTORY ENTRY (SPEC.md 20.14.1), and a host copy can drop it. That used to
# hand cc_ovneed the packed bytes, which its stamp compare refuses as `Old
# <NAME>.OVL` - clean, and a regression for an overlay copied off a PC. It
# cannot happen now: the read SNIFFS a hintless file's own header (SPEC.md
# 20.14.6) and expands it anyway, on both kernels and on a redirected volume,
# and the claim it reads into is sized from the image and not from FIND.
# -----------------------------------------------------------------------------
CC_OVLZ := $(BUILD)/ovlz
define CC_OVLZ_RULE
$(CC_OVLZ)/$(1): $(BUILD)/$(1) tools/os88lz.py $(PKGZSTAMP)
	@mkdir -p $$(@D)
	$(if $(PKGZ),python3 tools/os88lz.py --wrap $$@ --fmt $(PKGZ) $$<,cp $$< $$@)
endef

# -----------------------------------------------------------------------------
# Bootstrap once per make invocation, before any C compilation (including -j).
# The setup script is idempotent and verifies the pinned compiler with a canary.
# Ordinary assembly-only builds never depend on this target.
# -----------------------------------------------------------------------------
$(CC_PEEPSTAMP): | $(BUILD)
	@rm -f $(BUILD)/.ccpeep-on $(BUILD)/.ccpeep-off
	@touch $@

.PHONY: cc-toolchain
cc-toolchain:
	@if ! test -x "$(CC_SC)/smlrc" || ! test -x "$(CC_SMLRCC)" || ! test -x "$(CC_SC)/smlrpp"; then \
	  tools/setup-cc.sh --build-dir "$(BUILD)"; \
	fi

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
