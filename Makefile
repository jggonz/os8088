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

# "size of this file in bytes" is spelled differently by GNU coreutils and by
# BSD/macOS stat, and this gets built on both. Try GNU first, fall back to BSD.
FILESIZE = $$(stat -c%s $(1) 2>/dev/null || stat -f%z $(1))

KERNEL_SRC := kernel/kernel.asm
KERNEL_INC := $(wildcard kernel/*.inc)

.PHONY: all run run-640 debug test test-snd xt xt-640 clean

all: $(IMG) $(IMG360) $(APPSIMG) $(APPSIMG360)

$(BUILD):
	@mkdir -p $(BUILD)

# The kernel is a flat binary loaded at 1000:0000. No linker is involved,
# which keeps Apple's Mach-O-only toolchain out of the picture entirely.
$(BUILD)/kernel.bin: $(KERNEL_SRC) $(KERNEL_INC) | $(BUILD)
	$(NASM) -f bin -w+error -I kernel/ -o $@ $(KERNEL_SRC)
	@echo "kernel: $(call FILESIZE,$@) bytes"

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
# package header. Each package is assembled TWICE - at the 0xB000 link base
# and at org 0xB800 - and os88pkg.py diffs the pair into the v2 relocation
# table (SPEC.md 24), so the kernel can load any number of instances at
# per-instance bases.
$(BUILD)/mines.bin: apps/mines/mines.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/mines/mines.asm
	@echo "mines:  $(call FILESIZE,$@) bytes"

$(BUILD)/mines.alt.bin: apps/mines/mines.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/mines/mines.asm

$(BUILD)/mines.o88: $(BUILD)/mines.bin $(BUILD)/mines.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/mines.bin --alt $(BUILD)/mines.alt.bin -o $@

# HELLO, the second package: minimal, no embedded icon (proves the
# generic-icon fallback in the Disk window).
$(BUILD)/hello.bin: apps/hello/hello.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/hello/hello.asm
	@echo "hello:  $(call FILESIZE,$@) bytes"

$(BUILD)/hello.alt.bin: apps/hello/hello.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/hello/hello.asm

$(BUILD)/hello.o88: $(BUILD)/hello.bin $(BUILD)/hello.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/hello.bin --alt $(BUILD)/hello.alt.bin -o $@

# Note Pad, formerly the built-in KIND_NOTE app (SPEC.md 27).
$(BUILD)/notepad.bin: apps/notepad/notepad.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/notepad/notepad.asm
	@echo "notepad: $(call FILESIZE,$@) bytes"

$(BUILD)/notepad.alt.bin: apps/notepad/notepad.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/notepad/notepad.asm

$(BUILD)/notepad.o88: $(BUILD)/notepad.bin $(BUILD)/notepad.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/notepad.bin --alt $(BUILD)/notepad.alt.bin -o $@

# Recorder, the fourth shipped package (SPEC.md 35): sound wave recorder and
# player over the SPEC.md 34 sound layer (grants, streams, PCM_EXCL fallback).
$(BUILD)/recorder.bin: apps/recorder/recorder.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/recorder/recorder.asm
	@echo "recorder: $(call FILESIZE,$@) bytes"

$(BUILD)/recorder.alt.bin: apps/recorder/recorder.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/recorder/recorder.asm

$(BUILD)/recorder.o88: $(BUILD)/recorder.bin $(BUILD)/recorder.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/recorder.bin --alt $(BUILD)/recorder.alt.bin -o $@

# Piano, the fifth shipped package (SPEC.md 36): a colorful playable piano
# over the SPEC.md 34 tone tier (note viewer, replay, embedded songs).
$(BUILD)/piano.bin: apps/piano/piano.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/piano/piano.asm
	@echo "piano:  $(call FILESIZE,$@) bytes"

$(BUILD)/piano.alt.bin: apps/piano/piano.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/piano/piano.asm

$(BUILD)/piano.o88: $(BUILD)/piano.bin $(BUILD)/piano.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/piano.bin --alt $(BUILD)/piano.alt.bin -o $@

# FMTEST, the sound Phase 3 gate package (docs/SOUND-PLAN.md): drives the FM
# slot 0x0084 end to end (patch-load, chord, all-off, tone expiry, teardown).
# Never on the shipped apps disks - their directory order is pinned - it gets
# its own scratch image, mounted with:
#   make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
$(BUILD)/fmtest.bin: apps/fmtest/fmtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/fmtest/fmtest.asm
	@echo "fmtest: $(call FILESIZE,$@) bytes"

$(BUILD)/fmtest.alt.bin: apps/fmtest/fmtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/fmtest/fmtest.asm

$(BUILD)/fmtest.o88: $(BUILD)/fmtest.bin $(BUILD)/fmtest.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/fmtest.bin --alt $(BUILD)/fmtest.alt.bin -o $@

$(BUILD)/fmtest.img: $(BUILD)/fmtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/fmtest.o88

# SBTEST, the sound Phase 4 gate package (docs/SOUND-PLAN.md): drives the
# stream + staging slot 0x0088 end to end (grant, stage, open, status,
# underrun, close, teardown). Like fmtest it is never on the shipped apps
# disks - it gets its own scratch image, mounted with:
#   make test-snd SB16=1 TESTAPPS=build/sbtest.img
$(BUILD)/sbtest.bin: apps/sbtest/sbtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/sbtest/sbtest.asm
	@echo "sbtest: $(call FILESIZE,$@) bytes"

$(BUILD)/sbtest.alt.bin: apps/sbtest/sbtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -DOS88_ORG=0xB800 -o $@ apps/sbtest/sbtest.asm

$(BUILD)/sbtest.o88: $(BUILD)/sbtest.bin $(BUILD)/sbtest.alt.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/sbtest.bin --alt $(BUILD)/sbtest.alt.bin -o $@

$(BUILD)/sbtest.img: $(BUILD)/sbtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/sbtest.o88

# The software floppies (drive B:) hold packages, not boot code - os88fs only.
# Directory order is pinned: mines first, hello second (tests rely on it);
# notepad is appended third, recorder fourth and piano fifth so earlier
# indices hold.
$(APPSIMG): $(BUILD)/mines.o88 $(BUILD)/hello.o88 $(BUILD)/notepad.o88 $(BUILD)/recorder.o88 $(BUILD)/piano.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/mines.o88 $(BUILD)/hello.o88 $(BUILD)/notepad.o88 $(BUILD)/recorder.o88 $(BUILD)/piano.o88

$(APPSIMG360): $(BUILD)/mines.o88 $(BUILD)/hello.o88 $(BUILD)/notepad.o88 $(BUILD)/recorder.o88 $(BUILD)/piano.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/mines.o88 $(BUILD)/hello.o88 $(BUILD)/notepad.o88 $(BUILD)/recorder.o88 $(BUILD)/piano.o88

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

# `make test` plus audio capture (SPEC.md 34 / docs/SOUND-PLAN.md): the PC
# speaker renders into build/snd.wav, finalized when QMP `quit` stops QEMU.
# Verify with tools/sndcheck.py (RMS + dominant-frequency assertions).
# `make test-snd ADLIB=1` adds an emulated AdLib (OPL2 at 388h) on the same
# wav audiodev, so sndcheck hears FM output too (Phase 3); without it the
# boot has no OPL2 and the probe must report absent. TESTAPPS swaps the B:
# disk for a scratch image (the fmtest gate above).
ifneq ($(ADLIB),)
ADLIBDEV = -device adlib,audiodev=snd
endif
# `make test-snd SB16=1` adds an emulated Sound Blaster 16 (iobase 0x220,
# IRQ 5, DMA 1; DSP reports 4.x, so QEMU only ever exercises the auto-init
# strategy - SPEC.md 34.5; DSP < 2.00 is 86Box/real-hardware work).
ifneq ($(SB16),)
SBDEV = -device sb16,audiodev=snd
endif
TESTAPPS ?= $(APPSIMG)
test-snd: $(IMG) $(TESTAPPS)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(TESTAPPS),format=raw,if=floppy,index=1 \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid \
		-audiodev wav,id=snd,path=build/snd.wav -machine pcspk-audiodev=snd $(ADLIBDEV) $(SBDEV)

# Boot the 360KB image on emulated period hardware in 86Box.
xt: $(IMG360) $(APPSIMG360)
	$(BOX) -P $(VM) -N

# The same XT with a full 640KB of RAM instead of 256KB.
xt-640: $(IMG360) $(APPSIMG360)
	$(BOX) -P $(VM640) -N

clean:
	rm -rf $(BUILD)
