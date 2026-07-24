# =============================================================================
# jop - build a bootable 1.44MB floppy image
#
#   make        build build/jop.img
#   make run    boot it in QEMU
#   make debug  boot it with QEMU waiting for gdb on :1234
#   make clean
# =============================================================================

NASM  := nasm
QEMU  := qemu-system-i386
BUILD := build
IMG   := $(BUILD)/jop.img
IMG360 := $(BUILD)/jop360.img
APPSIMG := $(BUILD)/apps.img
APPSIMG360 := $(BUILD)/apps360.img
BOX   := /Applications/86Box.app/Contents/MacOS/86Box
VM    := $(CURDIR)/vm/xt

KERNEL_SRC := kernel/kernel.asm
KERNEL_INC := $(wildcard kernel/*.inc)

.PHONY: all run debug test xt clean

all: $(IMG) $(IMG360) $(APPSIMG) $(APPSIMG360)

$(BUILD):
	@mkdir -p $(BUILD)

# The kernel is a flat binary loaded at 1000:0000. No linker is involved,
# which keeps Apple's Mach-O-only toolchain out of the picture entirely.
$(BUILD)/kernel.bin: $(KERNEL_SRC) $(KERNEL_INC) | $(BUILD)
	$(NASM) -f bin -w+error -I kernel/ -o $@ $(KERNEL_SRC)
	@echo "kernel: $$(stat -f%z $@) bytes"

# The boot sector needs to know how many sectors to read, so we measure the
# kernel at build time and assemble the count in. Reading exactly what exists
# means a short kernel never waits on phantom sectors.
$(BUILD)/boot.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin \
		-DKERNEL_SECTORS=$$(( ( $$(stat -f%z $(BUILD)/kernel.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm
	@test $$(stat -f%z $@) -eq 512 || { echo "boot sector is not 512 bytes"; exit 1; }

# The same kernel on a 360KB 5.25" disk: 40 cylinders, 2 heads, 9 sectors per
# track. This is what an 8086-era machine can actually read - 1.44MB drives
# postdate the 8086 by years, and an XT BIOS knows nothing about them.
$(BUILD)/boot360.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 \
		-DKERNEL_SECTORS=$$(( ( $$(stat -f%z $(BUILD)/kernel.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm
	@test $$(stat -f%z $@) -eq 512 || { echo "boot sector is not 512 bytes"; exit 1; }

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

# Minesweeper, the first loadable program: a flat binary with the .jop
# package header, validated by jopkg.py and shipped on a jopfs data floppy.
$(BUILD)/mines.bin: apps/mines/mines.asm apps/jopapi.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/mines/mines.asm
	@echo "mines:  $$(stat -f%z $@) bytes"

$(BUILD)/mines.jop: $(BUILD)/mines.bin tools/jopkg.py
	python3 tools/jopkg.py $(BUILD)/mines.bin -o $@

# HELLO, the second package: minimal, no embedded icon (proves the
# generic-icon fallback in the Disk window).
$(BUILD)/hello.bin: apps/hello/hello.asm apps/jopapi.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/hello/hello.asm
	@echo "hello:  $$(stat -f%z $@) bytes"

$(BUILD)/hello.jop: $(BUILD)/hello.bin tools/jopkg.py
	python3 tools/jopkg.py $(BUILD)/hello.bin -o $@

# The software floppies (drive B:) hold packages, not boot code - jopfs only.
# Directory order is pinned: mines first, hello second (tests rely on it).
$(APPSIMG): $(BUILD)/mines.jop $(BUILD)/hello.jop tools/jopdisk.py
	python3 tools/jopdisk.py -o $@ --size 1440 $(BUILD)/mines.jop $(BUILD)/hello.jop

$(APPSIMG360): $(BUILD)/mines.jop $(BUILD)/hello.jop tools/jopdisk.py
	python3 tools/jopdisk.py -o $@ --size 360 $(BUILD)/mines.jop $(BUILD)/hello.jop

# The GUI reads a Microsoft serial mouse on COM1; QEMU emulates one natively.
MOUSE := -chardev msmouse,id=m0 -serial chardev:m0

run: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
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

# Boot the 360KB image on emulated period hardware in 86Box.
xt: $(IMG360) $(APPSIMG360)
	$(BOX) -P $(VM) -N

clean:
	rm -rf $(BUILD)
