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
# ...and the same three machines with a sound card in them (SPEC.md 51.4).
# QEMU's -device adlib/sb16 is the only other way to give the driver
# something to attach to, and it is not a real card: these are.
VMXTSND := $(CURDIR)/vm/xt-sound
VM286SND := $(CURDIR)/vm/286-sound
VM386SND := $(CURDIR)/vm/386-sound

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
# walking it (SPEC.md 37.90). Same reason as VIDEO=: QEMU has an MC146818 and
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
        286 386sx 386 xt-sound 286-sound 386-sound check-images bench clean

# `all` deliberately does NOT build anything under tests/ (see the bench block
# below). The testing apps are on-demand only: `make bench`.
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

# The system disk is a FAT12 volume with the kernel in its RESERVED AREA
# (SPEC.md 19.3). The boot sector still reads LBA 1..K raw - reserved sectors
# belong to the boot loader by definition - and everything after them is an
# ordinary file system, so drive A: mounts, browses and WRITES like the apps
# disk. That is what gives drivers a place to live and settings a place to be
# kept (SPEC.md 51).
#
# DRIVERS is the list, one .drv per line, root-level: the kernel resolves them
# by name in the volume's current directory and the file manager shows them.
DRIVERS := $(BUILD)/sound.drv

$(BUILD)/sound.bin: drivers/sound/sound.asm drivers/sound/sb.inc \
                    drivers/os88drv.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/sound/ -I drivers/ -I apps/ \
		-o $@ drivers/sound/sound.asm
	@echo "sound:  $(call FILESIZE,$@) bytes"

$(BUILD)/sound.drv: $(BUILD)/sound.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/sound.bin -o $@

$(IMG): $(BUILD)/boot.bin $(BUILD)/kernel.bin $(DRIVERS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/boot.bin --kernel $(BUILD)/kernel.bin $(DRIVERS)

$(IMG360): $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin $(DRIVERS)

# FMTEST: the AdLib gate package (SPEC.md 34.2/51.4). NEVER on the shipped
# apps disks - their directory order is pinned (SPEC.md 24) - so it rides its
# own scratch image, the filetest precedent:
#   make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
$(BUILD)/fmtest.bin: tests/fmtest/fmtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/fmtest/fmtest.asm
	@echo "fmtest: $(call FILESIZE,$@) bytes"

$(BUILD)/fmtest.o88: $(BUILD)/fmtest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/fmtest.bin -o $@

$(BUILD)/fmtest.img: $(BUILD)/fmtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/fmtest.o88

# SBTEST: the Sound Blaster gate package (SPEC.md 34.5/34.6). Like fmtest it
# is never on the shipped apps disks and rides its own scratch image:
#   make test-snd SB16=1 TESTAPPS=build/sbtest.img
$(BUILD)/sbtest.bin: tests/sbtest/sbtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/sbtest/sbtest.asm
	@echo "sbtest: $(call FILESIZE,$@) bytes"

$(BUILD)/sbtest.o88: $(BUILD)/sbtest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/sbtest.bin -o $@

$(BUILD)/sbtest.img: $(BUILD)/sbtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/sbtest.o88

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

# Recorder (SPEC.md 35): the sound layer's recording and streaming client.
# SND_CAP_PCM_IN and PCM_BG streams live behind SOUND.DRV (SPEC.md 51.4).
# It needs no card to be USEFUL -
# DEMO stages a built-in sweep and PLAY falls back to speaker clips - so it
# ships on every disk and greys REC on a machine with no Sound Blaster.
$(BUILD)/recorder.bin: apps/recorder/recorder.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/recorder/recorder.asm
	@echo "recorder: $(call FILESIZE,$@) bytes"


$(BUILD)/recorder.o88: $(BUILD)/recorder.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/recorder.bin -o $@

# Tracker (SPEC.md 45): a four-channel ProTracker MOD player. Its mixer is
# a worker task
# feeding a RING-mode stream (SPEC.md 34.5), which is the only thing in the
# tree that uses ring mode at all, and the module blob is a heap claim read
# with OSAPI_FILE_READ, whose destination advances by SEGMENT (SPEC.md
# 18.4.1) - which is the only reason a 116KB module fits in one call. Three
# sources, one binary.
$(BUILD)/tracker.bin: apps/tracker/tracker.asm apps/tracker/trkplay.inc \
                      apps/tracker/trkui.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/tracker/ -o $@ apps/tracker/tracker.asm
	@echo "tracker: $(call FILESIZE,$@) bytes"


$(BUILD)/tracker.o88: $(BUILD)/tracker.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/tracker.bin -o $@

# ArtfulType, the eleventh shipped package (SPEC.md 46): a port of
# ActionRetro's ArtfulType, the distraction-free Markdown writer for classic
# 68k Macs, onto the fullscreen surface (SPEC.md 11.2). Windowed it is the
# splash card; a button takes the whole screen, where it draws its own
# Macintosh menu bar (inverted in Writer mode), styles markdown live from
# its own ROM-font glyph renderer (bold overstrike / italic shear / scaled
# headings / underlined links / gray code cells), and does word wrap, drag
# selection, snapshot undo in a heap claim (SPEC.md 50.3), and Open/Save
# through the Standard File dialog. One line = one OSAPI_GFX_BLIT4 is the
# whole performance story; the caret blink is its worker task.
$(BUILD)/artful.bin: apps/artful/artful.asm apps/artful/atdoc.inc \
		apps/artful/atrend.inc apps/artful/atui.inc apps/artful/atedit.inc \
		apps/artful/atcmd.inc apps/artful/atfile.inc apps/artful/atimg.inc \
		apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/artful/ -o $@ apps/artful/artful.asm
	@echo "artful: $(call FILESIZE,$@) bytes"

$(BUILD)/artful.o88: $(BUILD)/artful.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/artful.bin -o $@

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

# Missile Command, the twelfth shipped package (SPEC.md 48): a port of Atari's
# 1980 arcade game from the 6502 sources (W3MAIN/W3DSUP/W3COMN). Like Arkanoid
# the game loop is a WORKER TASK (SPEC.md 20.6), but the aiming is the mouse
# rather than the keyboard, and it runs windowed OR on the fullscreen surface
# (SPEC.md 11.2). The wave table, the smart-bomb schedule, the scoring, the
# explosion radius ramp and the city/base coordinates are the arcade's own
# numbers; the palette cycles per wave the way SETCOL does, drawn only from
# colours that survive SPEC.md 39.4's reduction to three inks. No heap claim:
# every array is sized by the arcade's object counts and fits the package bss.
$(BUILD)/missile.bin: apps/missile/missile.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/missile/missile.asm
	@echo "missile: $(call FILESIZE,$@) bytes"

$(BUILD)/missile.o88: $(BUILD)/missile.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/missile.bin -o $@

# TameGram, the thirteenth shipped package (SPEC.md 49): a four-direction,
# dual-faction containment matrix contributed by Jason Page (store.amfile.org),
# credited under its own name in the bar (OSAPI_ABOUT_SET, SPEC.md 12.2). Like
# Arkanoid and Missile Command the game loop is a WORKER TASK (SPEC.md 20.6),
# but unlike them the worker's UPDATE runs under the gfx lock as well as its
# drawing: the piece geometry and the drawing share their scratch words, and
# every UI callback already holds that lock. The cell size is derived from the
# LIVE content box on every frame rather than from the screen height, so the
# matrix fits CGA's 136-row desktop band; the two faction colours straddle
# SPEC.md 39.4's white and dither classes so they survive 1bpp. No heap claim:
# the 32x32 board is 1KB of package bss.
$(BUILD)/tamegram.bin: apps/tamegram/tamegram.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/tamegram/tamegram.asm
	@echo "tamegram: $(call FILESIZE,$@) bytes"

$(BUILD)/tamegram.o88: $(BUILD)/tamegram.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/tamegram.bin -o $@

# FILETEST, the file-API gate package (SPEC.md 18.4/18.4.1): drives the file
# slots end to end (write, read-back, replace, rename, delete, dfree and the
# refusals) with both shapes of buffer - a heap claim past the 64KB horizon
# and this package's own bss. Never on the shipped apps disks - their
# directory order is pinned - it gets its own scratch image, mounted with:
#   make test TESTAPPS=build/filetest.img
# then, after QMP quit, checked from the host with:
#   python3 tools/os88disk.py --verify build/filetest.img
$(BUILD)/filetest.bin: tests/filetest/filetest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/filetest/filetest.asm
	@echo "filetest: $(call FILESIZE,$@) bytes"


$(BUILD)/filetest.o88: $(BUILD)/filetest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/filetest.bin -o $@

# BIG.DAT: 96KB, well past the 64KB horizon the file API used to stop at, so
# filetest's big-file checks have something to read - and, once read, to write
# straight back out again. Byte i is (i >> 9) - one distinct value per
# 512-byte sector - so a buffer that failed to advance by SEGMENT reads a
# different byte rather than a plausible one. Generated, never committed:
# 96KB of git churn per rebuild for a fixture is not worth it, and it rides
# the filetest image only (never the shipped apps disks).
$(BUILD)/big.dat: Makefile | $(BUILD)
	python3 -c "import sys; n=96*1024; sys.stdout.buffer.write(bytes((i>>9)&0xFF for i in range(n)))" > $@

$(BUILD)/filetest.img: $(BUILD)/filetest.o88 $(BUILD)/big.dat tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/filetest.o88 $(BUILD)/big.dat

# The same package on a legally fragmented volume: --scramble interleaves the
# chains, so the write path's allocator and the free/replace paths meet holes
# rather than a clean run of clusters. BIG.DAT rides this image too - checks
# 2..5 need it, and a 96KB chain walked across holes is the strongest version
# of what --scramble exists to test.
$(BUILD)/filetest-frag.img: $(BUILD)/filetest.o88 $(BUILD)/big.dat tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 --scramble $(BUILD)/filetest.o88 $(BUILD)/mines.o88 $(BUILD)/piano.o88 $(BUILD)/big.dat

# --- the benchmark disk, from tests/ (ON DEMAND: `make bench`) ---------------
#
# These are the only packages in the tree built from OUTSIDE apps/, and the
# folder is the point: tests/ holds testing apps, `all` never builds them, and
# no artifact of theirs is tracked. That keeps a normal build - and every
# shipped image - free of them, and it keeps `make check-images` honest, which
# reads its list from `git ls-files build`: an untracked bench.img is invisible
# to it, where a tracked one would have to be built by `all` or read as ORPHAN.
# The DEVELOPMENT of these apps happens on the `testing` branch; what lands
# here is a finished harness, so experimental never carries the midway
# artifacts of writing one.
#
# FONTBENCH prices the PRIMITIVE (SPEC.md 6.1.1): the same ten-character run
# drawn four ways - the hand-written gfx_fill + font_str pair and one
# font_run, each at a byte-aligned x and again at x+5.
#
# TYPEBENCH prices the KEYSTROKE (SPEC.md 11.94): 40 random characters typed
# into a 40-cell line with the whole line redrawn after each one, which is
# what np_redraw does to its dirty band. It is snappable itself and says in
# its header whether the snap took.
#
# BOTH ride one disk, built in both geometries, because they answer the same
# question at two scales and you want them side by side:
#
#   make bench                                             # build the disks
#   make test                            TESTAPPS=build/bench.img   # 1.44M, QEMU
#   make test VIDEO=cga                  TESTAPPS=build/bench.img
#   make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
#
# `make test TESTAPPS=build/bench.img` builds the disk on demand by itself -
# TESTAPPS is a prerequisite of the test targets - so `make bench` is for
# building it without booting (e.g. to write build/bench360.img to a floppy).
#
# build/bench360.img is the same disk at 9 spt / 40 cylinders - what an XT
# BIOS can actually read, so it is the one to write to a real 5.25" floppy or
# hand to 86Box. THAT is where these numbers are worth taking: on a 4.77MHz
# 8088 the PIT is a wall clock and the microsecond column means microseconds.
#
# Under QEMU it does not. QEMU runs the guest at host speed, so add
# `-icount shift=3,sleep=off` and the PIT counts guest INSTRUCTIONS instead -
# reproducible and machine-independent, but not time, and it understates the
# mono win because what alignment removes is disproportionately memory
# traffic (SPEC.md 6.1.1).
BENCHPKGS := $(BUILD)/fontbnch.o88 $(BUILD)/typebnch.o88

bench: $(BUILD)/bench.img $(BUILD)/bench360.img

$(BUILD)/fontbnch.bin: tests/fontbench/fontbench.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/fontbench/fontbench.asm
	@echo "fontbnch: $(call FILESIZE,$@) bytes"

$(BUILD)/fontbnch.o88: $(BUILD)/fontbnch.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/fontbnch.bin -o $@

$(BUILD)/typebnch.bin: tests/typebench/typebench.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/typebench/typebench.asm
	@echo "typebnch: $(call FILESIZE,$@) bytes"

$(BUILD)/typebnch.o88: $(BUILD)/typebnch.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/typebnch.bin -o $@

$(BUILD)/bench.img: $(BENCHPKGS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BENCHPKGS)

$(BUILD)/bench360.img: $(BENCHPKGS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BENCHPKGS)

# There WAS a third image here - the same package on a FAT16 volume, built on
# the 2.88MB test geometry, which exercised the one part of the write path
# FAT12 cannot. It went with DSK_FAT_SECS: at 10 sectors the mount's rule 10
# rejects every FAT16 volume there can be (a FAT is only FAT16 with >= 4,085
# clusters, i.e. >= 16 FAT sectors), so the image would build and refuse to
# mount. dsk_next_clus / dskw_setfat keep their FAT16 halves, unreachable.

# The software floppies (drive B:) hold packages, not boot code - os88fs only.
# The volume is FOLDERED (SPEC.md 19.2): the root holds APPS and GAMES and
# nothing else, so a package is two double-clicks away rather than one.
#
# The order of these lists DOES NOT MATTER and nothing may be built on it.
# It used to: the listing was directory order, so the order a package was
# named here was the row it appeared on, new packages had to append at the
# end of their folder, and the scripted tests clicked by that index. The
# mount sorts by name now (SPEC.md 19.4), so a volume lists alphabetically
# whoever wrote it and whatever order its entries are stored in - which is
# also the only answer that survives a host OS writing to the disk. What is
# left here is which packages ship and which folder each lands in.
APPS_TOOLS := $(BUILD)/artful.o88 $(BUILD)/fractal.o88 $(BUILD)/hello.o88 \
              $(BUILD)/notepad.o88 $(BUILD)/paint.o88 $(BUILD)/piano.o88 \
              $(BUILD)/recorder.o88 $(BUILD)/tracker.o88
APPS_GAMES := $(BUILD)/arkanoid.o88 $(BUILD)/mines.o88 $(BUILD)/missile.o88 \
              $(BUILD)/solitair.o88 $(BUILD)/tamegram.o88

# Data that ships beside the programs that read it (SPEC.md 24): os88disk.py
# treats anything not ending .o88 as a plain file. Tracker with no module to
# open is a player with nothing to play, and this is the one it was written
# against - so it travels with it rather than being something you have to
# find. 116KB, which the 360KB disk can still hold alongside every package.
APPS_DATA := apps/tracker/beverly.mod
APPS := $(APPS_TOOLS) $(APPS_GAMES) $(APPS_DATA)

# ...and the same list with the folder each package lands in. os88disk.py
# reads a "DIR:" prefix per package, so the grouping lives here rather than
# in the tool.
APPSARGS := $(addprefix APPS:,$(APPS_TOOLS)) \
            $(addprefix GAMES:,$(APPS_GAMES)) \
            $(addprefix APPS:,$(APPS_DATA))

$(APPSIMG): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(APPSARGS)

$(APPSIMG360): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(APPSARGS)

# The GUI reads a Microsoft serial mouse on COM1; QEMU emulates one natively.
MOUSE := -chardev msmouse,id=m0 -serial chardev:m0

run: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1 $(DEVCARD)

# A maxed-out 640KB machine. QEMU/SeaBIOS cannot boot with less than 1MB
# of guest RAM (SeaBIOS wedges during POST at -m 512k and -m 640k alike),
# but conventional memory tops out at 640K regardless of installed RAM, so
# -m 1M makes int 12h report 640K - same as a fully populated XT.
run-640: $(IMG) $(APPSIMG)
	$(QEMU) -m 1M -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1 $(DEVCARD)

debug: $(IMG) $(APPSIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) -s -S \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1 $(DEVCARD)

# Headless boot with a QMP socket, for scripted screendumps and input:
#   make test
#   python3 tools/qmp.py build/qmp.sock 'screendump build/shot.ppm'
#   python3 tools/qmp.py build/qmp.sock 'quit'
# ADLIB=1 / SB16=1 put an emulated card in the machine, for `test` as well as
# `test-snd`: without one the sound DRIVER (SPEC.md 51.4) probes, finds
# nothing and reports DRVE_HW, which is the correct answer and not the one
# you want to be testing against. `make test ADLIB=1` is how the OPL2 path is
# exercised at all - QEMU's -device adlib is an OPL2 at 388h.
ifneq ($(ADLIB),)
ADLIBDEV = -device adlib,audiodev=snd
endif
ifneq ($(SB16),)
SBDEV = -device sb16,audiodev=snd
endif
# ...and both need an audiodev to hang off, which `test` otherwise has none
# of. `none` is a real backend and costs nothing headless.
ifneq ($(ADLIBDEV)$(SBDEV),)
CARDAUDIO = -audiodev none,id=snd
endif

# The plain dev-loop targets (`run`, `run-640`, `debug`, `test`) carry the
# OPL2 by DEFAULT. The sound driver is WANTED out of the box (SPEC.md 51.4),
# and on a machine with no card the boot reports "No hardware found" by
# opening the Control Panel on its Drivers page (SPEC.md 51.3) - the right
# answer on real cardless hardware, pure noise at every boot of the dev
# loop. NOCARD=1 boots the cardless machine deliberately, to see exactly
# that path; an explicit ADLIB=1/SB16=1 supplies its own card, so the
# default stands down rather than double-mapping port 388h. `test-snd` is
# NOT in the list: its wav capture asserts on PC-speaker output, and a
# present card would route the very tones it measures away to FM.
ifeq ($(NOCARD)$(ADLIB)$(SB16),)
DEVCARD = -audiodev none,id=devsnd -device adlib,audiodev=devsnd
endif

# TESTAPPS swaps the B: disk for a scratch image - the filetest/fmtest/sbtest
# gates and the bench disk. It MUST be defined above the first target that
# names it: prerequisites are expanded when the rule is READ, so a definition
# below `test:` leaves that prerequisite empty. It sat below for a long time
# and `test` therefore hard-coded $(APPSIMG) - so `make test TESTAPPS=...`
# silently booted the SHIPPED apps disk, which reads as the scratch image
# having failed to build rather than never having been mounted.
TESTAPPS ?= $(APPSIMG)

test: $(IMG) $(TESTAPPS)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(TESTAPPS),format=raw,if=floppy,index=1 \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid \
		$(CARDAUDIO) $(ADLIBDEV) $(SBDEV) $(DEVCARD)

# `make test` plus audio capture (SPEC.md 34): the PC speaker renders into
# build/snd.wav, finalized when QMP `quit` stops QEMU. Verify with
# tools/sndcheck.py (RMS + dominant-frequency assertions). TESTAPPS (defined
# above `test`) swaps the B: disk for a scratch image.
test-snd: $(IMG) $(TESTAPPS)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(TESTAPPS),format=raw,if=floppy,index=1 \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid \
		-audiodev wav,id=snd,path=build/snd.wav -machine pcspk-audiodev=snd \
		$(ADLIBDEV) $(SBDEV)

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

# The three sound machines: an XT with a Sound Blaster 2.0 (so the OPL2 is
# the FM tier and the DSP the stream tier on the CPU this OS is FOR), and the
# 286/386 with an SB16. `make test ADLIB=1` / `SB16=1` gives the driver
# something to attach to under QEMU; these give it a card on a machine whose
# bus and clock are period-correct, which is the only place a stream's pacing
# means anything (SPEC.md 34.5/51.4).
xt-sound: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT_B) $(VMXTSND)/86box.cfg
	$(BOX) -P $(VMXTSND) -N

286-sound: $(IMG) $(APPSIMG)
	@$(UNPROTECT_B) $(VM286SND)/86box.cfg
	$(BOX) -P $(VM286SND) -N

386-sound: $(IMG) $(APPSIMG)
	@$(UNPROTECT_B) $(VM386SND)/86box.cfg
	$(BOX) -P $(VM386SND) -N

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
