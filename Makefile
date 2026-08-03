# =============================================================================
# os8088 - build a bootable 1.44MB floppy image
#
#   make        build build/os8088.img
#   make run    boot it in QEMU
#   make debug  boot it with QEMU waiting for gdb on :1234
#   make clean
# =============================================================================

NASM  := nasm
QEMU  := qemu-system-i386
BUILD := build
IMG   := $(BUILD)/os8088.img
IMG360 := $(BUILD)/os8088-360.img
APPSIMG := $(BUILD)/apps.img
APPSIMG360 := $(BUILD)/apps360.img
BOX   := /Applications/86Box.app/Contents/MacOS/86Box
VM    := $(CURDIR)/vm/xt
VM640 := $(CURDIR)/vm/xt640
VMCGA := $(CURDIR)/vm/xt-cga
VMHERC := $(CURDIR)/vm/xt-hercules
VM286 := $(CURDIR)/vm/286
VM386SX := $(CURDIR)/vm/386sx
VM386DX := $(CURDIR)/vm/386dx

# VIDEO=cga|herc|vga forces the adapter instead of probing for it (SPEC.md
# 39.1). The shipped images are always built without it, so they auto-detect;
# this exists because QEMU emulates no CGA and no Hercules card, and forcing
# the CGA path onto a VGA - whose int 10h mode 6 IS a CGA framebuffer, same
# segment, same two banks, same stride - is the only way to drive the mono
# renderer under the QMP harness.
VIDFORCE_vga  := 1
VIDFORCE_herc := 2
VIDFORCE_cga  := 3
ifneq ($(VIDEO),)
VIDDEF := -DVID_FORCE=$(VIDFORCE_$(VIDEO))
endif
# HERCSEG=0x7000 relocates the Hercules framebuffer into spare RAM so the
# renderer can be read back and checked without a Hercules card - B0000 is
# unmapped under QEMU and swallows every write (SPEC.md 39.9).
ifneq ($(HERCSEG),)
VIDDEF += -DVID_HERC_SEG=$(HERCSEG)
endif
# RTC=none|at|ns|rp|bios forces one rung of the clock ladder instead of
# walking it (SPEC.md 37.1). Same reason as VIDEO=: QEMU has an MC146818 and
# nothing else, so rung 1 always wins there and the other three would never
# be reached under the QMP harness. `none` exercises the fallback date.
RTCFORCE_none := 5
RTCFORCE_at   := 1
RTCFORCE_ns   := 2
RTCFORCE_rp   := 3
RTCFORCE_bios := 4
ifneq ($(RTC),)
ifeq ($(RTCFORCE_$(RTC)),)
$(error RTC must be one of: none at ns rp bios)
endif
VIDDEF += -DCLK_FORCE=$(RTCFORCE_$(RTC))
endif
# ...and a stamp so that CHANGING VIDEO rebuilds the kernel. Without it make
# sees an up-to-date kernel.bin, skips it, and boots the PREVIOUS adapter -
# which reads exactly like the probe or the renderer being broken.
#
# The invalidation runs at PARSE time, not as a rule. A rule that deletes
# kernel.bin is worse than no rule at all: make has already stat'd the target
# by the time the prerequisite's recipe runs, so it can conclude "up to date"
# about a file that recipe just removed, and then build the floppy image from
# a kernel that is not there. Doing it here means the file is simply gone
# before make builds its graph.
VIDSTAMP := $(BUILD)/.video-$(if $(VIDEO),$(VIDEO),auto)$(if $(HERCSEG),-$(HERCSEG))$(if $(RTC),-rtc$(RTC))
$(shell mkdir -p $(BUILD); \
        [ -f $(VIDSTAMP) ] || { rm -f $(BUILD)/.video-* $(BUILD)/kernel.bin; \
                                touch $(VIDSTAMP); })

# "size of this file in bytes" is spelled differently by GNU coreutils and by
# BSD/macOS stat, and this gets built on both. Try GNU first, fall back to BSD.
FILESIZE = $$(stat -c%s $(1) 2>/dev/null || stat -f%z $(1))

KERNEL_SRC := kernel/kernel.asm
KERNEL_INC := $(wildcard kernel/*.inc)

.PHONY: all run run-640 debug test test-snd xt xt-640 xt-cga xt-hercules \
        286 386sx 386 check-images clean

all: $(IMG) $(IMG360) $(APPSIMG) $(APPSIMG360)

$(BUILD):
	@mkdir -p $(BUILD)

# The kernel is a flat binary loaded at 1000:0000. No linker is involved,
# which keeps Apple's Mach-O-only toolchain out of the picture entirely.
$(BUILD)/kernel.bin: $(KERNEL_SRC) $(KERNEL_INC) | $(BUILD)
	$(NASM) -f bin -w+error -I kernel/ $(VIDDEF) -o $@ $(KERNEL_SRC)
	@echo "kernel: $(call FILESIZE,$@) bytes"
ifneq ($(VIDDEF),)
	@echo "  *** VIDEO=$(VIDEO) RTC=$(RTC): this kernel has a probe FORCED. ***"
	@echo "  *** build/ is git-tracked - rebuild with plain \`make\` before  ***"
	@echo "  *** committing, or every machine boots that way.               ***"
endif

# The boot sector needs to know how many sectors to read, so we measure the
# kernel at build time and assemble the count in. Reading exactly what exists
# means a short kernel never waits on phantom sectors.
$(BUILD)/boot.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/kernel.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm
	@test $(call FILESIZE,$@) -eq 512 || { echo "boot sector is not 512 bytes"; exit 1; }

# The same kernel on a 360KB 5.25" disk: 40 cylinders, 2 heads, 9 sectors per
# track. This is what an 8086-era machine can actually read - 1.44MB drives
# postdate the 8086 by years, and an XT BIOS knows nothing about them.
$(BUILD)/boot360.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/kernel.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm
	@test $(call FILESIZE,$@) -eq 512 || { echo "boot sector is not 512 bytes"; exit 1; }

$(IMG): $(BUILD)/boot.bin $(BUILD)/kernel.bin
	@dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	@dd if=$(BUILD)/boot.bin of=$@ conv=notrunc status=none
	@dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	@echo "image:  $@ (1.44MB, 18 spt)"

$(IMG360): $(BUILD)/boot360.bin $(BUILD)/kernel.bin
	@dd if=/dev/zero of=$@ bs=512 count=720 status=none
	@dd if=$(BUILD)/boot360.bin of=$@ conv=notrunc status=none
	@dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	@echo "image:  $@ (360KB, 9 spt)"

# Minesweeper, the first loadable program: a flat binary with the .o88
# package header. ONE assembly per package since SPEC.md 20.1 - a package
# links at org 0 and owns a segment, so it is position-independent and there
# is no relocation table to build (os88pkg.py validates and stamps).
$(BUILD)/mines.bin: apps/mines/mines.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/mines/mines.asm
	@echo "mines:  $(call FILESIZE,$@) bytes"


$(BUILD)/mines.o88: $(BUILD)/mines.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/mines.bin -o $@

# HELLO, the second package: minimal, no embedded icon (proves the
# generic-icon fallback in the Disk window).
$(BUILD)/hello.bin: apps/hello/hello.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/hello/hello.asm
	@echo "hello:  $(call FILESIZE,$@) bytes"


$(BUILD)/hello.o88: $(BUILD)/hello.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/hello.bin -o $@

# Note Pad, formerly the built-in KIND_NOTE app (SPEC.md 27).
$(BUILD)/notepad.bin: apps/notepad/notepad.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/notepad/notepad.asm
	@echo "notepad: $(call FILESIZE,$@) bytes"


$(BUILD)/notepad.o88: $(BUILD)/notepad.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/notepad.bin -o $@

# Piano, the fifth shipped package (SPEC.md 36): a colorful playable piano
# over the SPEC.md 34 tone tier (note viewer, replay, embedded songs).
$(BUILD)/piano.bin: apps/piano/piano.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/piano/piano.asm
	@echo "piano:  $(call FILESIZE,$@) bytes"


$(BUILD)/piano.o88: $(BUILD)/piano.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/piano.bin -o $@

# Fractal, the sixth shipped package: five escape-time fractals in Q4.12
# fixed point, rendered by a background WORKER TASK (SPEC.md 20.6) while the
# GUI stays live. The first client of OSAPI_TASK_SPAWN / OSAPI_TASK_ALIVE.
$(BUILD)/fractal.bin: apps/fractal/fractal.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/fractal/fractal.asm
	@echo "fractal: $(call FILESIZE,$@) bytes"


$(BUILD)/fractal.o88: $(BUILD)/fractal.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/fractal.bin -o $@

# Paint, the seventh shipped package: a bitmap editor - eight tools, a 4bpp
# offscreen canvas above BB_SEG, one-level undo/redo, an internal clipboard and
# BMP load/save through the Standard File dialog. Needs ~620KB of conventional
# memory for its canvas (int 12h decides; a smaller machine gets a notice
# window instead), so `make run-640` is the way to exercise it.
$(BUILD)/paint.bin: apps/paint/paint.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/paint/paint.asm
	@echo "paint:  $(call FILESIZE,$@) bytes"


$(BUILD)/paint.o88: $(BUILD)/paint.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/paint.bin -o $@

# Solitaire, the eighth shipped package (SPEC.md 43): Klondike, with the drag
# done as an XOR outline the way the window manager drags a window - nothing
# is repainted until the button comes up, so a hand of seven cards costs four
# thin XOR strips a tick. Card backs are rendered once into a packed 4bpp
# image and blitted with gfx_blit4; faces are drawn from the kernel font plus
# 1-bit suit masks, hollow for the red suits on a 1bpp adapter.
# The package file is SOLITAIR.O88, not SOLITAIRE.O88: the data disk is
# FAT12 (SPEC.md 19) and an 8.3 stem is eight characters, so the name is
# truncated the way DOS would truncate it. The name INSIDE the header - what
# the Task Manager and the dock show - is still 'SOLITAIRE'; that field is 16
# bytes (SPEC.md 20.2) and has nothing to do with the file name.
$(BUILD)/solitair.bin: apps/solitaire/solitaire.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/solitaire/solitaire.asm
	@echo "solitaire: $(call FILESIZE,$@) bytes"


$(BUILD)/solitair.o88: $(BUILD)/solitair.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/solitair.bin -o $@

# Arkanoid, the ninth shipped package (SPEC.md 44): a brick-breaker whose game
# loop is a WORKER TASK (SPEC.md 20.6) rather than a callback, because a ball
# has to keep moving between keystrokes. Arrow keys steer on a deadline (int
# 16h has no key-up, so a held key is inferred from typematic repeat), the
# capsules are caught with the paddle, and the PC speaker (SPEC.md 34) is
# driven FROM the worker - which snd_req_inst attributes correctly by falling
# back to the running task's instance. 'ARKANOID' is exactly eight characters,
# so unlike SOLITAIR.O88 the file name needs no truncating.
$(BUILD)/arkanoid.bin: apps/arkanoid/arkanoid.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/arkanoid/arkanoid.asm
	@echo "arkanoid: $(call FILESIZE,$@) bytes"


$(BUILD)/arkanoid.o88: $(BUILD)/arkanoid.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/arkanoid.bin -o $@

# FILETEST, the file-API gate package (SPEC.md 18.4): drives the file slots
# 0x0098..0x00A8 end to end (write, read-back, replace, rename, delete,
# dfree, and the refusals). Never on the shipped apps disks - their
# directory order is pinned - it gets its own scratch image, mounted with:
#   make test TESTAPPS=build/filetest.img
# then, after QMP quit, checked from the host with:
#   python3 tools/os88disk.py --verify build/filetest.img
$(BUILD)/filetest.bin: apps/filetest/filetest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/filetest/filetest.asm
	@echo "filetest: $(call FILESIZE,$@) bytes"


$(BUILD)/filetest.o88: $(BUILD)/filetest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/filetest.bin -o $@

$(BUILD)/filetest.img: $(BUILD)/filetest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/filetest.o88

# The same package on a legally fragmented volume: --scramble interleaves the
# chains, so the write path's allocator and the free/replace paths meet holes
# rather than a clean run of clusters.
$(BUILD)/filetest-frag.img: $(BUILD)/filetest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 --scramble $(BUILD)/filetest.o88 $(BUILD)/mines.o88 $(BUILD)/piano.o88

# ...and on a FAT16 volume (2.88MB test geometry), which differs only in the
# FAT entry encoding - the one part of the write path FAT12 cannot exercise.
$(BUILD)/filetest-fat16.img: $(BUILD)/filetest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 2880 $(BUILD)/filetest.o88

# The software floppies (drive B:) hold packages, not boot code - os88fs only.
# Directory order is pinned: mines first, hello second (tests rely on it);
# notepad third, piano fourth, fractal fifth, paint sixth and solitaire
# seventh, so earlier indices hold. New packages ALWAYS append at the end -
# the scripted tests click the Disk window by row index. (Recorder was the
# fourth entry until the sound cards were removed - SPEC.md 34 - so
# everything after it moved down one row.)
APPS := $(BUILD)/mines.o88 $(BUILD)/hello.o88 $(BUILD)/notepad.o88 \
        $(BUILD)/piano.o88 $(BUILD)/fractal.o88 $(BUILD)/paint.o88 \
        $(BUILD)/solitair.o88 $(BUILD)/arkanoid.o88

$(APPSIMG): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(APPS)

$(APPSIMG360): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(APPS)

# The GUI reads a Microsoft serial mouse on COM1; QEMU emulates one natively.
MOUSE := -chardev msmouse,id=m0 -serial chardev:m0

run: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1

# A maxed-out 640KB machine. QEMU/SeaBIOS cannot boot with less than 1MB
# of guest RAM (SeaBIOS wedges during POST at -m 512k and -m 640k alike),
# but conventional memory tops out at 640K regardless of installed RAM, so
# -m 1M makes int 12h report 640K - same as a fully populated XT.
run-640: $(IMG) $(APPSIMG)
	$(QEMU) -m 1M -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1

debug: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) -s -S \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1

# Headless boot with a QMP socket, for scripted screendumps and input:
#   make test
#   python3 tools/qmp.py build/qmp.sock 'screendump build/shot.ppm'
#   python3 tools/qmp.py build/qmp.sock 'quit'
test: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1 \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid

# `make test` plus audio capture (SPEC.md 34): the PC speaker renders into
# build/snd.wav, finalized when QMP `quit` stops QEMU. Verify with
# tools/sndcheck.py (RMS + dominant-frequency assertions). TESTAPPS swaps
# the B: disk for a scratch image (the filetest gate above).
TESTAPPS ?= $(APPSIMG)
test-snd: $(IMG) $(TESTAPPS)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(TESTAPPS),format=raw,if=floppy,index=1 \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid \
		-audiodev wav,id=snd,path=build/snd.wav -machine pcspk-audiodev=snd

# 86Box rewrites its own config file on exit, and twice now it has put the
# wp:// (write-protect) prefix back on the DATA floppy - which makes every
# SPEC.md 18.4 write fail as FERR_WPROT and reads, from inside the OS, as a
# filesystem bug rather than an emulator setting. Strip it at launch so the
# setting cannot silently regress. The BOOT floppy keeps its wp:// on
# purpose: its sector 0 has no valid BPB, so the kernel refuses to write it
# anyway, and the prefix is a second lock on the disk carrying the loader.
# perl -pi behaves identically on GNU and BSD/macOS, unlike sed -i.
UNPROTECT_B = perl -pi -e 's{^fdd_02_fn = wp://}{fdd_02_fn = }'

# Boot the 360KB image on emulated period hardware in 86Box.
xt: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT_B) $(VM)/86box.cfg
	$(BOX) -P $(VM) -N

# The same XT with a full 640KB of RAM instead of 256KB.
xt-640: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT_B) $(VM640)/86box.cfg
	$(BOX) -P $(VM640) -N

# The two monochrome machines (SPEC.md 39), both 256KB - which is all an
# ibmxt takes anyway, and the floor os8088 targets. These are the ONLY way to
# exercise the detection probe and the Hercules renderer: QEMU has no such
# card, so `make test VIDEO=cga` covers the mono renderer but never the probe.
xt-cga: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT_B) $(VMCGA)/86box.cfg
	$(BOX) -P $(VMCGA) -N

xt-hercules: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT_B) $(VMHERC)/86box.cfg
	$(BOX) -P $(VMHERC) -N

# The other end of the range: an AT-class machine, VGA, more RAM than the OS
# can reach. os8088 is 8086 code in real mode, so a 286/386 runs it verbatim -
# these targets exist to prove exactly that, and to see the same 640KB ceiling
# on a machine that has megabytes behind it (int 12h still answers 640).
#
#   286    AMI 286 clone board, 286 @ 12.5MHz, 1MB
#   386sx  Shuttle HOT-304, 386SX @ 16MHz, 2MB
#   386    Micronics 386 board, 386DX @ 25MHz, 2MB
#
# All three carry an OTI-067 VGA, a serial Microsoft mouse on COM1 and 1.44MB
# drives (so they boot the same images QEMU does), and all three are
# interactive: 86Box has no automation socket.
#
# The 286 is deliberately NOT `ibmat`: 86Box caps the real 5170 planar at
# 512KB and clamps mem_size down to it SILENTLY, the same trap `vm/xt640`
# hit with `ibmxt`. A clone AT board takes the full megabyte.
#
# Unlike the XT, an AT-class machine has a CMOS, and on the very first launch
# it is empty: the BIOS stops at its setup screen (the AMI board offers
# "EXIT FOR BOOT / RUN CMOS SETUP"). Pick EXIT FOR BOOT once - 86Box saves
# the CMOS to vm/<machine>/nvr/ (gitignored) and every later boot goes
# straight to the desktop.
286: $(IMG) $(APPSIMG)
	@$(UNPROTECT_B) $(VM286)/86box.cfg
	$(BOX) -P $(VM286) -N

386sx: $(IMG) $(APPSIMG)
	@$(UNPROTECT_B) $(VM386SX)/86box.cfg
	$(BOX) -P $(VM386SX) -N

386: $(IMG) $(APPSIMG)
	@$(UNPROTECT_B) $(VM386DX)/86box.cfg
	$(BOX) -P $(VM386DX) -N

# check-images - are the git-tracked binaries in build/ what the sources
# actually produce?
#
# build/ is gitignored, but a handful of artifacts inside it are force-added
# and shipped: the kernel, the two boot sectors, the two bootable floppies and
# the two software floppies. Nothing makes them follow a source change, so
# they go stale in silence - edit a package, skip the rebuild, and the tree
# still builds, still boots, and still looks right while carrying a floppy
# image that no longer holds what the source says it does. That is not
# hypothetical: two "Rebuild the shipped images" commits exist because someone
# caught it by hand, and a merge shipped a Paint two fixes out of date until
# the merge rebuilt it.
#
# This is the mechanical version of catching it. Every shipped artifact is
# built a SECOND time into a scratch directory and compared byte for byte.
# That is only meaningful because the toolchain is deterministic on purpose -
# tools/os88disk.py pins the volume serial and every FAT timestamp for exactly
# this reason - so a difference is always staleness and never noise.
#
# Three things are deliberate:
#
#  - **The tracked set comes from git, not from a list here.** A list would
#    drift from what is actually tracked, and the drift would be invisible.
#  - **A tracked file the build does NOT produce is reported too**, and so is
#    a tracked VIDEO=/RTC= stamp. Both are the other half of the same problem:
#    build/ has been force-added wholesale more than once, which swept in a
#    stamp twice and, on the occasion the parse-time hook had just deleted it,
#    took kernel.bin OUT of the repo. The stamp needs naming specially because
#    it would otherwise pass - the scratch build makes one too, and two empty
#    files compare equal.
#  - **The scratch build is knob-free.** The shipped images must be built with
#    no VIDEO=/HERCSEG=/RTC= forcing - the kernel recipe already says so in a
#    comment it prints at you - so building the comparison without them turns
#    that comment into a check: a forced kernel that reached the tree reads as
#    stale, which is exactly what it is.
#
# It is not part of `all`: it costs a second full build, and it is a
# pre-commit gate rather than something every build should pay for.
CHECKDIR := $(BUILD)/.check

check-images:
	@rm -rf $(CHECKDIR)
	@$(MAKE) BUILD=$(CHECKDIR) VIDEO= HERCSEG= RTC= all >/dev/null
	@stale=0; bogus=0; n=0; \
	for t in $$(git ls-files $(BUILD) 2>/dev/null); do \
	    n=$$((n+1)); \
	    b=$$(basename $$t); \
	    case $$b in .video-*) \
	        echo "  SCRATCH $$t - a build stamp, not a shipped artifact"; \
	        bogus=1; continue;; \
	    esac; \
	    if [ ! -f $(CHECKDIR)/$$b ]; then \
	        echo "  ORPHAN  $$t - tracked, but no build rule produces it"; \
	        bogus=1; \
	    elif cmp -s $$t $(CHECKDIR)/$$b; then \
	        :; \
	    else \
	        echo "  STALE   $$t - does not match what the sources build"; \
	        stale=1; \
	    fi; \
	done; \
	rm -rf $(CHECKDIR); \
	if [ $$n -eq 0 ]; then \
	    echo "check-images: nothing tracked in $(BUILD)/ - is this a git checkout?"; \
	    exit 1; \
	fi; \
	if [ $$stale -ne 0 ]; then \
	    echo "check-images: STALE above - run \`make\`, then commit $(BUILD)/"; \
	fi; \
	if [ $$bogus -ne 0 ]; then \
	    echo "check-images: SCRATCH/ORPHAN above - untrack it: git rm --cached <path>"; \
	fi; \
	if [ $$stale -ne 0 ] || [ $$bogus -ne 0 ]; then exit 1; fi; \
	echo "check-images: $$n tracked artifact(s) match the sources"

clean:
	rm -rf $(BUILD)
