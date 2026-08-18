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
IMG720 := $(BUILD)/os8088-720.img
IMG360 := $(BUILD)/os8088-360.img
APPSIMG := $(BUILD)/apps.img
APPSIMG720 := $(BUILD)/apps720.img
APPSIMG360 := $(BUILD)/apps360.img
BOX   := /Applications/86Box.app/Contents/MacOS/86Box

# RESET= clears a machine's non-volatile state on the way in, and it reaches
# EVERY 86Box target at once because all eighteen of them launch through
# $(BOX) and differ only in which vm/ directory they point at:
#
#   make 386-c-word RESET=1       the CMOS  (the one you almost always want)
#   make xt RESET=flash           the flash, leaving the CMOS alone
#   make 386-word RESET=both      both
#
# 86Box's own -X does the clearing, so this is its supported mechanism rather
# than us deleting files under it: -X clears and then GOES ON TO BOOT, which
# is why RESET is a knob on the normal target and not a target of its own.
# On an AT-class machine (286 and up) a cleared CMOS means the first boot
# stops in BIOS setup wanting one - pick EXIT FOR BOOT once and 86Box writes
# vm/<machine>/nvr/ again for every later boot. The XT machines have no CMOS
# to clear and ignore this.
#
# What it does NOT clear is an ORPHANED .nvr: the file is named for the
# `machine =` key, so editing that key in a 86box.cfg strands the old file and
# -X never touches it again. `rm -rf vm/<name>/nvr` is the bigger hammer, and
# nvr/ is gitignored for every machine, so neither can reach the repo.
ifneq ($(RESET),)
 ifeq ($(RESET),1)
BOX   += -X cmos
 else
  ifeq ($(filter $(RESET),cmos flash both),)
   $(error RESET must be 1, cmos, flash or both - got '$(RESET)')
  endif
BOX   += -X $(RESET)
 endif
endif

VM    := $(CURDIR)/vm/xt
VM640 := $(CURDIR)/vm/xt640
VMCGA := $(CURDIR)/vm/xt-cga
VMHERC := $(CURDIR)/vm/xt-hercules
# The DUAL-DISPLAY machine (SPEC.md 39.12-39.19): the same XT with BOTH mono
# cards in it, each on its own monitor window.
VMMULTI := $(CURDIR)/vm/xt-multimon
VM286 := $(CURDIR)/vm/286
VM386SX := $(CURDIR)/vm/386sx
VM386DX := $(CURDIR)/vm/386dx
# ...and the same three machines with a sound card in them (SPEC.md 51.4).
# QEMU's -device adlib/sb16 is the only other way to give the driver
# something to attach to, and it is not a real card: these are.
VMXTSND := $(CURDIR)/vm/xt-sound
VM286SND := $(CURDIR)/vm/286-sound
VM386SND := $(CURDIR)/vm/386-sound
# The top of the range: a 486DX2/66 and a Pentium 133, both with an SB16.
VM486 := $(CURDIR)/vm/486
VMPENT := $(CURDIR)/vm/pentium
# The two Frotz machines (SPEC.md 61.9). Both carry a sound card, because
# @sound_effect is part of what is being tested, and both have the FULL 640KB:
# a Z-machine story is RESIDENT (SPEC.md 61.4) and 256KB does not hold the
# interesting ones. xt-z is the honest target - the machine this OS is for,
# with a 720KB 3.5" DD drive for B: because 360KB does not hold a library.
# 386-z is the comfortable one: the same code, two 1.44MB drives, and the
# machine where Anchorhead and Bronze are worth trying.
VMXTZ := $(CURDIR)/vm/xt-z
VM386Z := $(CURDIR)/vm/386-z

# The two WORD machines (SPEC.md 68.5): the same pairing as the Frotz two -
# an XT with the 720KB Word disk in B:, a 386 with the 1.44MB one - but no
# sound card on either, because Word makes no sound.
VMXTWORD := $(CURDIR)/vm/xt-word
VM386WORD := $(CURDIR)/vm/386-word

# The CWORD machine (SPEC.md 70.12): the C toolchain's demonstrator on a
# period machine. ONE machine and not two, and it is the 386 rather than the
# XT, because what is being demonstrated first is that a C package boots, runs
# and saves at all - the XT is where it then has to be MEASURED, and until
# somebody has taken that measurement an `xt-c-word` target would be a claim
# rather than a machine. It is a copy of vm/386-word with one line different
# (fdd_02_fn), which is deliberate: 86Box silently substitutes an
# unrecognised cpu_family at that family's default speed and rewrites the
# config on the way out, so a hand-written profile is a machine nobody has
# checked the clock of.
VM386CWORD := $(CURDIR)/vm/386-c-word

# The RUNCPM machine (SPEC.md 71.5): the same 386, B: = build/runcpm.img.
VM386RUNCPM := $(CURDIR)/vm/386-runcpm

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

# SCROLLROW=1 builds gfx_scroll's REFERENCE form - the row address recomputed
# from scratch for both ends of every row (SPEC.md 5.5.1). It is the A/B for
# the constant-delta rewrite, and the only way to show that the pixels did not
# move: a scroll is a copy, so "it looks right" is exactly what a wrong offset
# also looks like. Folded into VIDDEF so it shares the stamp below.
ifneq ($(SCROLLROW),)
VIDDEF += -DSCROLL_ROWBASE
endif

# SNAPAUDIT=1 histograms the x & 7 of every glyph the machine draws, into
# snap_hchar/snap_hrun (SPEC.md 11.94.1, kernel/font.inc). It answers "which
# app does not align its text" off a RUNNING machine, which is the only way to
# catch a pen computed at run time - a centred string, a right-aligned column,
# an icon-grid cell - that reading layout constants cannot. Read the counters
# with tools/os88snap.py. Folded into VIDDEF so it shares the stamp below:
# changing it rebuilds the kernel, which is what stops an instrumented kernel
# lingering in build/ and being booted by accident.
ifneq ($(SNAPAUDIT),)
VIDDEF += -DSNAPAUDIT
endif

# DISKCNT=1 compiles in the three disk counters of docs/DISK-PERF-PLAN.md 2:
# mounts, sectors transferred and int 13h data calls. They exist to answer
# "how much work is a directory change", which QEMU can measure exactly even
# though it cannot measure how long it takes (PERFORMANCE.md). Folded into
# VIDDEF so it shares the stamp below - changing it rebuilds the kernel, which
# is the only thing that stops a counted kernel from lingering in build/ and
# being booted by accident.
ifneq ($(DISKCNT),)
VIDDEF += -DDISK_COUNTERS
# ...and the same knob instruments the hard-disk INSTALLER (SPEC.md 52.10.9).
# One knob for both, because the two halves are useless apart: the kernel's
# counters stop at dsk_xfer's run loop, so on an install they are the FLOPPY
# side alone and the drive is invisible; the driver's own hook is the other
# half. A shipped HDD.DRV carries none of it.
DRVDEF += -DINSTBENCH
endif

# INSTCHUNK=1 puts the TOP of the hard-disk installer's copy-buffer ladder at
# 32KB, so KERNEL.SYS - the biggest file it moves, and a hidden+system one -
# goes down as a run of OSAPI_FILE_APPEND_SYS calls instead of a single write
# (SPEC.md 18.4.4/52.10.11). That path only happens on a machine too short of
# heap to fund 96KB, which is no machine here, so without this knob the one
# code path that carries a system file across several writes is never run.
# It touches the DRIVER only; the kernel is byte-identical either way.
ifneq ($(INSTCHUNK),)
DRVDEF += -DHIW_KMAXKB=32
endif

# FLOPPY1=1 puts the floppy transfer back to one sector per int 13h - the
# pre-SPEC.md-18.91 loop, with nothing else changed. It exists so that the
# batching can be A/B'd on real hardware without a source edit, which is the
# only place its failures have ever been visible (18.92).
# DISKAL=1 goes back to believing int 13h's AL - the sectors the BIOS says it
# transferred - instead of trusting CF=0 for the whole request (SPEC.md 18.91).
# The A/B for the 5150's 6x floppy loss: that machine moves all nine sectors
# and answers AL = 1, so the old code re-read the other eight one at a time.
ifneq ($(DISKAL),)
VIDDEF += -DDISK_TRUST_AL
BOOTDEF += -DDISK_TRUST_AL
endif

# BOOTDIAG=1 makes the boot sector print int 13h's STATUS as two hex digits
# instead of 'os8088: disk error'. The sector has four spare bytes, so this
# is a knob and not a default: the message is worth more on a machine that
# boots and the status is worth more on one that does not.
ifneq ($(BOOTDIAG),)
BOOTDEF += -DBOOT_DIAG
endif

ifneq ($(FLOPPY1),)
VIDDEF += -DFLOPPY_ONE
BOOTDEF += -DFLOPPY_ONE
endif

# DIRW1=1 never takes SPEC.md 18.95's sector cache, so every read moves exactly
# the sectors asked for again - the pre-18.95 behaviour, reached through the
# same refusal path a machine with no room takes. The A/B for the whole cache,
# and the reason it is a knob is FLOPPY1's: this is a claim about REVOLUTIONS,
# and no emulator here models one.
ifneq ($(DIRW1),)
VIDDEF += -DDIRW_ONE
endif

# INSTRO=1 leaves dskw_write_sys's replace mask refusing READ-ONLY, which is
# the pre-SPEC.md-19.6.2 behaviour and the A/B for it: a first hard-disk
# install onto a fresh format still works, and a SECOND onto the same disk
# errors naming KERNEL.SYS - the two legs the field reported. A knob because
# the failing leg needs a full install to reach and cannot be reasoned into
# existence from the passing one.
ifneq ($(INSTRO),)
VIDDEF += -DINST_RDONLY
endif

# KEEPH=0 ignores SPEC.md 11.93's WF_KEEPH, so wm_fit shortens EVERY window
# that will not fit the desktop band again - the pre-11.93 behaviour, and the
# A/B for the whole flag. A knob rather than a second kernel because what it
# proves is a CLICK: shortened, Minesweeper's bottom rows are drawn where no
# window claims them and the press reaches the dock, which is a difference no
# screenshot shows.
ifeq ($(KEEPH),0)
VIDDEF += -DNOKEEPH
endif

# HEAPCOMPACT=0 removes the heap compactor (SPEC.md 66) - the BODY, not merely
# the call, so the A/B measures the feature and not a branch around it. With it
# off, mem_claim's retry loop is the shed-and-retry it was, every claim stays
# where it was first placed, and OSAPI_MEM_MOVABLE records a handle nothing
# ever reads. This is the reference build for tests/heapfrag and for any claim
# that compaction changed a byte it should not have: the two kernels must
# produce identical contents in every surviving block, and only the ADDRESSES
# may differ.
ifeq ($(HEAPCOMPACT),0)
VIDDEF += -DNOCOMPACT
endif

# HEAPPARK=0 keeps the compactor and removes only the WORKER PARK (SPEC.md
# 66.5), so a claim owned by a package with a live worker stays pinned - which
# is what the kernel did before the handshake existed. It is a separate knob
# from HEAPCOMPACT because the two answer separate questions: HEAPCOMPACT=0
# asks whether compaction does anything at all, and this asks whether the park
# is what lets it reach the claims that actually fragment a heap. Against
# tests/heapfrag, which runs its suite with a live worker on purpose, both
# produce the same two failures - and a kernel where only the park is broken
# passes the first A/B and fails this one.
ifeq ($(HEAPPARK),0)
VIDDEF += -DNOPARK
endif

# HEAPPARKLK=0 keeps the park and removes only its GFX-LOCK half (SPEC.md
# 66.5.4), so a worker parks at OSAPI_TASK_ALIVE and nowhere else - which is
# what the kernel did before the declaration existed. It is the A/B for that
# section alone, and it is the one that matters for a DRAWING worker:
# tests/trackmove.py holds the gfx lock across the triggering claim on purpose,
# so with this set Tracker's module cannot move and check 1 must fail.
ifeq ($(HEAPPARKLK),0)
VIDDEF += -DNOPARKLK
endif

# FDDPROBE=0 never asks the FDC whether the second floppy drive is really there
# (SPEC.md 18.97), so the int 11h equipment word decides on its own again - the
# pre-18.97 behaviour, and the A/B for the whole probe. A knob for DIRW1's
# reason turned up one further: this is a claim about what a real uPD765
# reports for a drive that is NOT THERE, an emulated controller answers what
# its author believed one answers (SPEC.md 18.92, docs/FIELD-NOTES.md 5), and
# the machine that can settle it is the one whose desktop is wrong today.
ifeq ($(FDDPROBE),0)
VIDDEF += -DNO_FDDPROBE
endif

# FDDABSENT=1 forces the probe's verdict to ABSENT for unit 1 without touching
# a port, which is the only way to drive SPEC.md 18.97.2's DECISION on any
# emulator in this tree: none of them can produce a real absent verdict, so the
# retire path and the tier-1 keep path are both unreachable from a plain boot.
# MartyPC synthesizes ST3 = 79 (TRK0 SET) for a drive its own config does not
# have and QEMU's FDC answers 0x28 | (track==0 ? 0x10 : 0) off a track that is
# 0 for an absent drive - so both say PRESENT unconditionally.
#
# It stubs the FDC conversation and nothing else, deliberately: what 18.97.2
# changed is what the kernel DOES with an absent verdict, and that is what this
# makes testable. The conversation itself is still the 5150's question. Boot a
# tier-0 machine with this and drive B must go; boot a tier-1 one and it must
# stay, with `probe stop 03` and `verdict 1` in the published block.
#
# FDDABSENT=2 is the OTHER field signature (SPEC.md 18.97.2/18.97.3): the
# Packard Bell's PRESENT 1.2MB drive, whose ST3 is byte-identical to the
# 5150's absent one (21, twice) and whose ST0 is not - 21 against 71. It must
# be kept on EVERY tier, tier 0 included, because ST0 saying the recalibrate
# ended normally is positive evidence of a drive.
ifneq ($(FDDABSENT),)
VIDDEF += -DFDD_FORCE_ABSENT=$(FDDABSENT)
endif

# KERN_SMALL=1 selects the SMALL build of the kernel (docs/KERN-SPLIT-PLAN.md).
#
# The split is the one docs/KERNEL-MEMORY.md and kernel.asm have named for
# three budget moves: a 128KB machine and a 640KB machine stop wanting the same
# feature set long before they stop fitting the same image, so the answer at
# the ceiling is TWO KERNELS OFF ONE TREE rather than another raise.
#
# THE DEFAULT IS BIG. `all` ships kern_big, so the six images, `make field`,
# `make marty` and every test run the full kernel, and kern_small is the one
# you ask for. That is the right way round for the same reason the budget
# guards are: kern_big is what nearly every machine runs, and kern_small is a
# deliberate product for the 128KB floor rather than a fallback nobody chose.
#
# EXACTLY ONE OF THE TWO IS ALWAYS DEFINED, and both are POSITIVE. It would be
# shorter to define nothing for the default and write `%ifndef KERN_SMALL` for
# big-only code, and it would read as a double negative at every site - which
# on a conditional whose whole job is "which build is this" is exactly where a
# reader gets it backwards. kernel.asm asserts that precisely one arrives.
# KFZ=1 builds the kernel breadcrumb (the KFZ macro in kernel.asm): raw
# framebuffer marks through ui_task's keyboard branch, for a reported freeze
# that reaches no package. Mono adapters only, ships nowhere.
ifneq ($(KFZ),)
VIDDEF += -DKFZTRACE
endif

ifneq ($(KERN_SMALL),)
VIDDEF += -DKERN_SMALL
else
VIDDEF += -DKERN_BIG
endif

# REDRAWFULL=1 puts the menu bar, the dock and the Disk window's command
# path back on their pre-SPEC.md 12.9/30.3/22.13 paths: every bar redraw is a
# full one (fill, logo, name, every title and the clock), every changed dock
# tile is erased and rebuilt, every damage to the strip is the whole strip,
# and every fm_docmd ends in a whole-window fm_repaint. It exists to be DIFFED
# against - the incremental paths must be byte-identical to it, and "the
# picture is the same, only the number of times it was drawn changed" is the
# whole claim they make, which a screenshot of one build alone cannot check.
#
#   make && cp build/os8088-360.img /tmp/inc.img
#   make REDRAWFULL=1 && cp build/os8088-360.img /tmp/ref.img
#   ...drive the same script on each and compare the two strips' pixels.
#
# Verified that way on all three adapters (SPEC.md 12.9): 15 scripted steps
# on CGA and 10 on Hercules and VGA mode 12h, 0 differing pixels each.
# NOSPLIT=1 is SPEC.md 39.14.6's A/B: the same kernel with font_run's
# display split removed, so a straddled window's second half is drawn by the
# one-display path it used to take. The picture is the claim - a run that
# crosses a seam either lands on both cards or it does not - and only the
# pair of builds can show that, which is REDRAWFULL's own reasoning.
# IT IS IN $(VIDSTAMP) AND $(KNOBS) BELOW, and it shipped in neither: a knob
# outside the stamp does not rebuild the kernel, so `make NOSPLIT=1` after a
# plain `make` drove the SPLIT kernel twice and the A/B came back null. That
# is the trap the VIDEO= stamp exists to prevent - a new knob belongs in both
# lists on the day it is added, and a null A/B is one of the things it looks
# like (SPEC.md 39.14.6).
ifneq ($(NOSPLIT),)
VIDDEF += -DNOSPLIT
endif

# NOSUOCCL=1 is SPEC.md 11.96.15's A/B, and it exists because REDRAWFULL is
# too COARSE to be the reference for this one. REDRAWFULL turns off every
# incremental path in the machine at once, which makes its kernel 512 bytes
# smaller - one image rung - so KERNEL.SYS is a sector shorter, the volume has
# a kilobyte more free, and the Disk window's own status line reads `Free 195K`
# against `Free 194K`. That is 25 differing pixels of a digit, in a gate whose
# standard is zero, and it is not a drawing difference at all. This knob leaves
# the reduction COMPILED and skips only the call, so the two binaries are three
# bytes apart, land in the same rung, carry the same disk, and differ by
# exactly the thing under test.
ifneq ($(NOSUOCCL),)
VIDDEF += -DNOSUOCCL
endif

ifneq ($(REDRAWFULL),)
VIDDEF += -DREDRAWFULL
endif

# CURFIX=1 turns ON the two cursor-hide changes, and they are OFF BY DEFAULT.
# SPEC.md 7.1.4.2 makes cur_lazyck test the ARMED REGION rather than the
# window's frame, so a pointer parked over a window IN FRONT of an updating
# one stops blinking; 7.1.4.3 then hides it again while the hand is MOVING,
# because a lit arrow that cannot move under the gfx lock reads as a stutter.
# They are one knob because they are one behaviour and neither is worth
# testing without the other.
#
# THE DEFAULT IS OFF because the field has not settled. Both measure better
# on a cycle-accurate 5150 - the parked blink goes from 83 frames in 90 to 0,
# and the moving pointer is left exactly where it was - and the reports back
# from real iron are that it may still not be an improvement. Instruments
# here read the arrow's pixels and the gfx lock; they cannot read how motion
# looks to a person, and on that question the machine in the room wins. So
# the code stays, the default is the behaviour that shipped for years, and
# this is revisited after the next upstream squash.
#
#   make                -> the frame test, no motion gate (what ships)
#   make CURFIX=1       -> both changes in
#   make combo CURFIX=1 -> ...as a field disk, which is how they are compared
#
# It is in $(VIDSTAMP) and $(KNOBS) below, so changing it rebuilds the kernel
# (39.14.6's trap: a knob outside the stamp drives the PREVIOUS build and the
# A/B comes back null). Verified at the polarity flip: a plain `make` now
# reproduces the opt-out kernel byte for byte and `make CURFIX=1` the old
# default, so the inversion moved no code.
ifneq ($(CURFIX),)
VIDDEF += -DCURFIX
endif

# DIRTYRAM=1 fills the claim heap with 0xAA at boot, before anything can claim
# from it. It is a DIAGNOSTIC and never ships: QEMU gives the guest zeroed RAM
# where a real machine gives it whatever was there, so a routine that reads a
# claim it has not written is invisible under the emulator and is a different
# bug on every boot out in the field. With this on it is the same bug every
# time. It is in $(VIDSTAMP) and $(KNOBS) below, like every other knob.
ifneq ($(DIRTYRAM),)
VIDDEF += -DDIRTYRAM
endif

# DRAGCACHE=0 removes SPEC.md 11.96.12's drag cache, so a window dragged by its
# title bar goes back to ordering a full W_PAINT of itself at the new place.
# It is the A/B: the claim is "the same picture, not drawn", and the failure it
# can have is a restore landing a few pixels wide of where it belongs - which
# is a real picture, so no screenshot of one build can check it. Only the pair.
ifeq ($(DRAGCACHE),0)
VIDDEF += -DNODRAGCACHE
endif

# SOLNOKEEP=1 makes Solitaire's sol_keep answer 0, so every tableau column is
# erased and redrawn whole - the pre-SPEC.md 43.10 path, and a stricter
# reference than 43.7's, which kept the buried backs. It is REDRAWFULL's
# reasoning for a package: the shadow's whole claim is "the same picture, drawn
# fewer times", and the failure it can have is a card left standing where a
# card no longer is - a REAL picture, so no screenshot of one build can check
# it. Only the pair can.
#
#   make && python3 tools/solcheck.py capture /tmp/a build/soltest.img
#   make SOLNOKEEP=1 && python3 tools/solcheck.py capture /tmp/b build/soltest.img
#   python3 tools/solcheck.py diff /tmp/a /tmp/b        # 0 differing pixels
#
# IT IS IN $(SOLSTAMP) BELOW, and it has to be for the reason NOSPLIT records:
# the .bin rule depends on the SOURCE, so a knob outside a stamp leaves an
# up-to-date binary in place, drives the same build twice and returns a null
# A/B - which reads as a pass.
SOLDEF :=
ifneq ($(SOLNOKEEP),)
SOLDEF += -DSOLNOKEEP
endif
SOLSTAMP := $(BUILD)/.sol-$(if $(SOLNOKEEP),nokeep,keep)
$(shell mkdir -p $(BUILD); \
        [ -f $(SOLSTAMP) ] || { rm -f $(BUILD)/.sol-* $(BUILD)/solitair.bin; \
                                touch $(SOLSTAMP); })

# SNDSNIFF=sb adds the Sound Blaster DSP reset scan to the boot's sound probe
# (SPEC.md 51.3.1), which by default is the OPL2 timer-flag dance at 388h and
# nothing else. Every Sound Blaster ever made carries an OPL2 there, so the
# scan finds nothing the default has not already found on real hardware - and
# it costs six unknown port ranges being WRITTEN to on every boot of every
# machine, which is the one thing SPEC.md 51.3 refuses to do for the hard-disk
# driver. It is a knob because two cases want it: a card whose FM half is
# jumpered off or decoded elsewhere, and QEMU's own `-device sb16`, whose OPL
# does NOT answer the timer probe (a real one does). ~60 ms of a cardless
# boot; free on a machine that has any FM chip at all, which is tested first.
ifneq ($(SNDSNIFF),)
ifneq ($(SNDSNIFF),sb)
$(error SNDSNIFF must be: sb)
endif
VIDDEF += -DSND_SNIFF_SB
endif

# RAMKB=<n> makes the boot sector believe the machine has n KB, instead of
# asking int 12h (SPEC.md 2.7). It exists because QEMU always answers 639 -
# conventional memory is capped there whatever -m says - so the relocation
# arithmetic, the low-memory boot and the refusal below the floor are all
# unreachable here without it. `make test RAMKB=128` boots with the sector
# where a 128KB machine would put it (MIN_RAM_KB, the guard 5 case) and
# `RAMKB=64` must print RAM and stop rather than load a kernel over itself.
# The kernel still reads the REAL int 12h for its heap, so this moves the
# sector and nothing else. It costs the shipped sector nothing: with the knob
# unset the %ifdef is not assembled.
ifneq ($(RAMKB),)
BOOTDEF += -DRAM_KB=$(RAMKB)
endif

# FONT=<name> bakes fonts/<name>.f8 into the kernel instead of copying the
# machine's ROM 8x8 set at boot (SPEC.md 6.2). OFF by default and the shipped
# images do not use it: a plain `make` still assembles the int 10h probe, so
# this changes no released byte until it is asked for.
#
# The bytes ride in the BOOT OVERLAY, which lands in the FAT window and is
# written over by the first mount - so they cost neither KERN_BUDGET nor
# KERN_CODE_MAX, and the probe not being assembled makes .text smaller. The
# whole price is one more floppy sector at boot (~65 ms on the 5150).
#
# The generated include goes in $(BUILD) and is never tracked, like every
# other artifact; -I $(BUILD)/ on the kernel rule is what finds it.
#
# $(FONTS) is the fonts/ DIRECTORY and not a list anybody maintains - drop a
# .f8 in and it is a build target on the next run (see the font-<name> block
# further down, which is generated from this).
FONTS := $(sort $(patsubst fonts/%.f8,%,$(wildcard fonts/*.f8)))
ifneq ($(FONT),)
FONTSRC := fonts/$(FONT).f8
FONTINC := $(BUILD)/font8x8.inc
VIDDEF  += -DBAKED_FONT
# Named here rather than left to make's own "No rule to make target
# fonts/x.f8", which says nothing about the knob that asked for it or about
# what else there is to ask for.
ifeq ($(wildcard $(FONTSRC)),)
$(error FONT=$(FONT): there is no $(FONTSRC). Available: $(FONTS))
endif
endif
# ...and the rule that builds it is DOWN with the kernel's, not here. A target
# defined before `all` becomes make's default goal, so a plain `make
# FONT=tallx` built the include and stopped.
# ...and a stamp so that CHANGING A KNOB rebuilds what it affects. Without it
# make sees an up-to-date kernel.bin, skips it, and boots the PREVIOUS
# adapter - which reads exactly like the probe or the renderer being broken.
# RAMKB is in the key for the same reason and is the sharper case: it touches
# neither boot.asm nor kernel.bin, so nothing at all would rebuild and the
# machine would boot the previous relocation while reporting the new one.
#
# The invalidation runs at PARSE time, not as a rule. A rule that deletes
# kernel.bin is worse than no rule at all: make has already stat'd the target
# by the time the prerequisite's recipe runs, so it can conclude "up to date"
# about a file that recipe just removed, and then build the floppy image from
# a kernel that is not there. Doing it here means the file is simply gone
# before make builds its graph.
#
# ...and $(KNOBS) is WHICH ONES WERE ASKED FOR, for the banner the kernel rule
# prints. It cannot be "is $(VIDDEF) non-empty", which is what it used to be.
# That test WORKED until the kern_small/kern_big split, which started putting
# -DKERN_BIG in VIDDEF unconditionally - exactly one of the two is always
# defined (see the block above) - and from then on the alarm fired on every
# build ever made, naming a row of blank assignments. A warning that is always
# on is a warning nobody reads, and this one exists to stop a knob kernel being
# tested for detection or cut into a release. Worth noticing as a shape: a
# change somewhere else silenced a guard by making it shout.
#
# The variant is deliberately NOT in this list, which is tools/kernsize.py's
# distinction and worth keeping the two files agreed on: KERN_BIG/KERN_SMALL
# are two SHIPPED PRODUCTS (docs/KERN-SPLIT-PLAN.md), `make small` builds one
# into a directory of its own and it forces no probe; everything else here
# produces a kernel nobody ships.
#
# EVERY OTHER KNOB IN $(VIDSTAMP) BELOW BELONGS HERE TOO. SNAPAUDIT and
# SCROLLROW were in the stamp and not in this list, so the kernel duly rebuilt
# for them and the banner said nothing about the kernel it had just built -
# each of them changes the binary (see their ifneqs at the top of this file).
# BOOTDIAG is the one asymmetry that is meant to be one: it feeds $(BOOTDEF)
# alone, and the stamp deletes boot.bin/boot360.bin whatever it says.
KNOBS := $(strip $(foreach k,VIDEO HERCSEG RTC DISKCNT DISKAL BOOTDIAG FLOPPY1 \
                             KFZ DIRW1 INSTRO KEEPH DIRTYRAM HEAPCOMPACT HEAPPARK HEAPPARKLK FDDPROBE FDDABSENT REDRAWFULL NOSPLIT NOSUOCCL SNDSNIFF RAMKB DRAGCACHE \
                             SNAPAUDIT SCROLLROW \
                             CURFIX \
                             FONT INSTCHUNK,\
                             $(if $($(k)),$(k)=$($(k)))))
VIDSTAMP := $(BUILD)/.video-$(if $(VIDEO),$(VIDEO),auto)$(if $(HERCSEG),-$(HERCSEG))$(if $(RTC),-rtc$(RTC))$(if $(DISKCNT),-dc$(DISKCNT))$(if $(FLOPPY1),-f1$(FLOPPY1))$(if $(DISKAL),-al$(DISKAL))$(if $(RAMKB),-ram$(RAMKB))$(if $(DIRW1),-d1$(DIRW1))$(if $(INSTRO),-ro$(INSTRO))$(if $(KEEPH),-kh$(KEEPH))$(if $(HEAPCOMPACT),-hc$(HEAPCOMPACT))$(if $(HEAPPARK),-hp$(HEAPPARK))$(if $(HEAPPARKLK),-hl$(HEAPPARKLK))$(if $(FDDPROBE),-fp$(FDDPROBE))$(if $(FDDABSENT),-fa$(FDDABSENT))$(if $(SNDSNIFF),-ss$(SNDSNIFF))$(if $(REDRAWFULL),-rf$(REDRAWFULL))$(if $(DRAGCACHE),-dg$(DRAGCACHE))$(if $(NOSPLIT),-ns$(NOSPLIT))$(if $(NOSUOCCL),-no$(NOSUOCCL))$(if $(CURFIX),-cf$(CURFIX))$(if $(FONT),-font$(FONT))$(if $(KERN_SMALL),-small$(KERN_SMALL))$(if $(KFZ),-kfz$(KFZ))$(if $(INSTCHUNK),-ic$(INSTCHUNK))$(if $(SNAPAUDIT),-sa$(SNAPAUDIT))$(if $(SCROLLROW),-sr$(SCROLLROW))$(if $(DIRTYRAM),-dr$(DIRTYRAM))
$(shell mkdir -p $(BUILD); \
        [ -f $(VIDSTAMP) ] || { rm -f $(BUILD)/.video-* $(BUILD)/kernel.bin \
                                      $(BUILD)/kernel-full.bin \
                                      $(BUILD)/ctrl.drv $(BUILD)/format.drv \
                                      $(BUILD)/boot.bin $(BUILD)/boot360.bin \
                                      $(BUILD)/hdd.bin $(BUILD)/hdd.drv \
                                      $(BUILD)/hddtool.bin $(BUILD)/hddtool.drv; \
                                touch $(VIDSTAMP); })
# kernel-full.bin AND the two on-demand modules are on that list, and for most
# of this Makefile's life they were not - which made the whole stamp ineffective
# for the kernel that actually SHIPS. kernel.bin is not assembled from source:
# os88mod.py splits it, ctrl.drv and format.drv out of kernel-full.bin (SPEC.md
# 2.8), and kernel-full.bin depends on the SOURCES alone. A knob changes the
# command line and no source, so deleting only kernel.bin re-ran the split on
# the PREVIOUS knob's kernel-full.bin - so `make VIDEO=cga` after a plain build
# shipped a KERNEL.SYS and two .drv files with no CGA in them, silently, which
# is the exact failure the stamp exists to prevent and reads as the feature
# under test being broken.
#
# **FOUND TWICE, INDEPENDENTLY, FROM TWO DIFFERENT SYMPTOMS**, which is worth
# keeping because neither symptom points at the stamp. One was an incremental
# plain rebuild after `make SNAPAUDIT=1` differing from a clean one by 39,504
# bytes; the other was `make FDDABSENT=1` producing a kernel with none of the
# knob's bytes in it, while the knob's A/B duly "passed" by comparing a build
# against itself.
#
# **kernsize.py cannot catch this and will actively mislead you**: it
# re-assembles the kernel ITSELF with $(VIDDEF), so it reports the sizes of a
# binary the build did not produce - a 65-byte knob was reported for a kernel
# that did not contain it. os88sym.py is the tool that does catch it, because
# it asserts byte-identity against build/kernel.bin and REFUSES rather than
# answering. The check that never lies is to look for the knob's own bytes in
# the artifact.

# --- the build number the About box shows (SPEC.md 14.2) ---------------------
#
# A GENERATED include, like $(ASSOCICO) and $(FONTINC) below, and generated the
# same way for the same reason: `-I $(BUILD)/` on the kernel rule finds it, so
# tools/kernsize.py and tools/os88sym.py - which re-assemble the kernel
# themselves and pass that same -I - keep working with no argument of their
# own. A -D in $(VIDDEF) would have been shorter and would have broken
# os88sym.py for every session that did not know to repeat it: it asserts
# byte-identity with build/kernel.bin, so a missing define reads as "the map
# describes a DIFFERENT kernel" rather than as a forgotten flag.
#
# It runs at PARSE time and not as a rule, because the thing it depends on is
# not a file: HEAD moves when you commit and no tracked file's mtime moves
# with it, so a rule would never fire and the image would carry the previous
# build's number - the VIDSTAMP trap one paragraph up, with a number instead
# of an adapter. buildnum.py rewrites the file only when the number actually
# changed, so this costs an up-to-date tree nothing; when it does change,
# kernel.bin's ordinary prerequisite does the rest and no stamp is needed.
#
# $(BUILDNUM) is the number itself for the banner on the kernel rule. It is
# 0 when the build could not determine it (no git, or a shallow clone, where
# the count is silently short - see buildnum.py), and 0 means the About box
# shows the version line exactly as it always did.
BUILDINC := $(BUILD)/buildnum.inc
BUILDNUM := $(shell python3 tools/buildnum.py -o $(BUILDINC))

# "size of this file in bytes" is spelled differently by GNU coreutils and by
# BSD/macOS stat, and this gets built on both. Try GNU first, fall back to BSD.
FILESIZE = $$(stat -c%s $(1) 2>/dev/null || stat -f%z $(1))


KERNEL_SRC := kernel/kernel.asm
# apps/os88ui.inc is a KERNEL source too - SPEC.md 20.5.1's shared control
# assembles into fdlg.inc with OS88UI_KERNEL defined. Without it here, editing
# that file leaves build/kernel.bin untouched and every image stale, which is
# indistinguishable from a change that did nothing: kernsize.py re-assembles
# and so reports the NEW sizes while the booted kernel is the old one.
KERNEL_INC := $(wildcard kernel/*.inc) apps/os88ui.inc

.PHONY: small kernsplit all run run-640 run-720 debug test test-snd xt xt-640 xt-cga \
        xt-hercules xt-multimon 286 386sx 386 xt-sound 286-sound 386-sound 486 pentium \
        bench field combo combo144 combo720 stackprobe trklog npbench clicktest marty \
        comscan lptlink calcref \
        fonts fontsheets fontlist \
        stories zdisk ztest zh zhboot zcheck zgfx zpic zscreens xt-z 386-z \
        worddisk wordcheck xt-word 386-word \
        cc-note chello covl cword cworddisk 386-c-word runcpm runcpmdisk \
        runcpm-src rcz80test rcmemtest rczex 386-runcpm \
        allapps rcbandbench \
        checkdocs clean clean-cc clean-marty distclean

# `all` deliberately does NOT build anything under tests/ (see the bench block
# below). The testing apps are on-demand only: `make bench`.
#
# It does not build anything C either, and it does not NEED the C compiler:
# SmallerC is not in this tree (SPEC.md 70.1), so a clone with nasm, python3
# and nothing else builds every floppy this project ships. `cc-note` is last
# in the list and is the whole of what the default build says about C - one
# paragraph, only when the compiler is absent, never an error.
all: checkdocs $(IMG) $(IMG720) $(IMG360) $(APPSIMG) $(APPSIMG720) $(APPSIMG360) \
     cc-note

# The documentation gate (SPEC.md is the binding contract, so a citation that
# names a heading which does not exist is a defect in it): a stale section
# reference, and an API slot number in prose that no longer names that
# routine. The second is the one that cannot be caught by reading - after a
# renumbering a stale slot is usually still a VALID slot, just a different
# call.
#
# It runs in the DEFAULT build rather than sitting behind `make checkdocs`,
# and that is the whole point of the target: nothing ran it for long enough to
# accumulate 34 findings, and a check nobody types has exactly that failure
# mode. Same shape as os88ovlchk.py on the kernel rule below - a gate whose
# value is that it cannot be skipped - and it costs ~0.7 s, reads only tracked
# text and writes nothing. It builds no artifact, so it is PHONY and every
# `make` pays it; that is deliberate, because the drift it catches arrives in
# commits that touch no source at all.
checkdocs:
	@python3 tools/checkdocs.py

$(BUILD):
	@mkdir -p $(BUILD)

# The kernel is a flat binary loaded at 1000:0000. No linker is involved,
# which keeps Apple's Mach-O-only toolchain out of the picture entirely.
# The default associations' 8x8 glyphs (SPEC.md 54.3), reduced on the HOST out
# of each package's own embedded icon so the kernel ships knowing what its own
# applications look like - a document icon then costs no disk read on the first
# boot of any machine. GENERATED, and that is the point: hand-pasted bytes go
# stale in silence when an app's icon changes, where this dependency cannot.
# The DAG stays acyclic - a package depends on apps/os88api.inc, never on
# kernel.bin.
ASSOCICO := $(BUILD)/associco.inc
$(ASSOCICO): tools/os88mini.py $(BUILD)/paint.o88 $(BUILD)/notepad.o88 \
             $(BUILD)/tracker.o88 $(BUILD)/artful.o88 | $(BUILD)
	python3 tools/os88mini.py -o $@ \
		PAINT=$(BUILD)/paint.o88 NOTEPAD=$(BUILD)/notepad.o88 \
		TRACKER=$(BUILD)/tracker.o88 ARTFUL=$(BUILD)/artful.o88

# The baked typeface (SPEC.md 6.2), when FONT= asked for one. Empty otherwise,
# so the prerequisite below simply is not there on a default build.
$(FONTINC): $(FONTSRC) tools/os88font.py | $(BUILD)
	python3 tools/os88font.py $(FONTSRC) -o $@

# The on-demand modules (SPEC.md 2.8), in the order os88mod.py numbers them -
# the index IS the kernel's MOD_* and the header carries it, so a mismatch
# here is refused at build time rather than far-called at run time. Defined
# above the rules because one of them is a target list, which make expands
# when it PARSES rather than when it runs.
# RECURSIVE, both of them, and that is the whole of how a second kernel gets
# its own modules: a rule that builds a kernel somewhere other than $(BUILD)
# sets KMODDIR for itself (see `small` and `field`), and DRIVERS - recursive
# for the same reason - then names that directory's images instead of this
# one's. A module carries the LAYOUT of the kernel it was cut out of
# (SPEC.md 2.8.2), so shipping the wrong one is refused rather than executed;
# this is what stops it happening in the first place.
KMODDIR = $(BUILD)
KMODS = $(KMODDIR)/ctrl.drv $(KMODDIR)/format.drv
KMODARGS = -m 0=$(BUILD)/ctrl.drv -m 1=$(BUILD)/format.drv

# THE KERNEL IS ASSEMBLED WHOLE AND THEN CUT UP (SPEC.md 2.8). Everything
# from .modc onward is an on-demand module: kernel code that ships as a file
# instead of inside KERNEL.SYS. It is assembled HERE, with the kernel, because
# a module is .cold code running with DS = KERNEL_SEG - so every kernel symbol
# it names has to be the address the kernel itself uses, and one assembly is
# what makes that true rather than claimed.
$(BUILD)/kernel-full.bin: $(KERNEL_SRC) $(KERNEL_INC) $(ASSOCICO) $(FONTINC) $(BUILDINC) tools/os88ovlchk.py | $(BUILD)
	@python3 tools/os88ovlchk.py
	$(NASM) -f bin -w+error -I kernel/ -I apps/ -I $(BUILD)/ $(VIDDEF) -o $@ $(KERNEL_SRC)

$(BUILD)/kernel.bin: $(BUILD)/kernel-full.bin tools/os88mod.py | $(BUILD)
	python3 tools/os88mod.py $< -k $@ $(KMODARGS) --build $(BUILDNUM)
	@echo "kernel: $(call FILESIZE,$@) bytes (image rung + boot overlay)$(if $(filter-out 0,$(BUILDNUM)), - build $(BUILDNUM), - NO build number: buildnum.py said why)"
# What that cost, per section and in 512-byte rungs, against the baseline in
# docs/KERNEL-MEMORY.md. A REPORT and never a gate: the guards inside
# kernel.asm are what refuse an overrun, and this says how close you came and
# how much of each rung's slack is left for the next feature. It costs one
# extra assembly of the kernel, which is why it is not folded into the line
# above: -w+error would turn its %warning into an error, and relaxing that
# for every build would silence a %warning somebody meant as an alarm.
#
# IT BELONGS TO THIS RULE AND NOT TO THE MODULES' BELOW. Both lines sat after
# $(KMODS) for a while, which made them the MODULE rule's recipe - and that
# rule is up to date the moment os88mod.py has written the files, so on an
# ordinary build neither the report nor the banner ran at all. A guard that
# stops being asked reads exactly like a guard that passes.
	@python3 tools/kernsize.py --build $(BUILD) $(VIDDEF) || true
# ...and this tests the KNOBS, not $(VIDDEF), and NAMES them: the banner used
# to be a row of blank assignments, which says no more than the alarm itself.
# Both halves of that were the same bug and $(KNOBS) is where it is explained.
ifneq ($(KNOBS),)
	@echo "  *** BUILT WITH A KNOB: $(KNOBS)"
	@echo "  *** It boots that way on every machine. Rebuild with a plain   ***"
	@echo "  *** \`make\` before testing detection or cutting a release.      ***"
	@echo "  *** DISKCNT=1 ALONE is expected: it is in every field kernel   ***"
	@echo "  *** (SPEC.md 18.94.1) and costs the image 0 bytes. Any OTHER   ***"
	@echo "  *** knob above is the one to be surprised by.                  ***"
endif

# The modules fall out of the rule above rather than having one of their own:
# a second recipe would run os88mod.py a second time, and GNU make would run
# it once PER TARGET for a multi-target rule, which is the classic way to get
# a file written twice and a race with -j.
$(KMODS): $(BUILD)/kernel.bin ;

# The boot sector needs to know how many sectors to read, so we measure the
# kernel at build time and assemble the count in. Reading exactly what exists
# means a short kernel never waits on phantom sectors.
$(BUILD)/boot.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/kernel.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm
	@test $(call FILESIZE,$@) -eq 512 || { echo "boot sector is not 512 bytes"; exit 1; }

# The same kernel on a 360KB 5.25" disk: 40 cylinders, 2 heads, 9 sectors per
# track. This is what an 8086-era machine can actually read - 1.44MB drives
# postdate the 8086 by years, and an XT BIOS knows nothing about them.
#
# THIS SECTOR IS THE 720KB DISK'S TOO, and that is not a shortcut. A 720KB
# 3.5" DD floppy is 80 cylinders of the SAME track shape - 9 sectors, 2 heads
# - and boot/boot.asm's whole knowledge of a geometry is SPT and HEADS: it
# derives the cylinder from the LBA and never has a count of them to be wrong
# about. What genuinely differs between the two disks is the BPB (media byte,
# total sectors, FAT size), and os88disk.py writes that over the first 62
# bytes when it builds the image. A boot720.bin would therefore be a
# byte-identical second artifact that can only ever say what this one already
# says.
$(BUILD)/boot360.bin: boot/boot.asm $(BUILD)/kernel.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 $(BOOTDEF) \
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
#
# SYSAPPS rides beside them: applications the KERNEL loads by name off A:
# rather than by double-click (SPEC.md 28). Only the Task Manager so far,
# which the chip menu opens - so it has to be on the disk in drive A:.
#
# It is on the APPS disk TOO, for the SINGLE-FLOPPY machine (SPEC.md 28.1):
# there, swapping to the apps disk makes it A:, and the chip menu would
# otherwise stop working the moment the user went to look for a program. Same
# file, different attributes - on the system disk it is read-only
# (SPEC.md 19.6), on the apps disk it is an ordinary file, because that disk
# is the user's.
#
# It lives in SYSTEM/ on both, which is what SYSAPPSARGS says and what
# ui_tm_open looks for (SPEC.md 28.3). Kernel machinery in a folder of its
# own, so the root of a disk is the user's files - and the two disks agree,
# because the chip menu cannot know which of them is in the drive.
# DEBUG.DRV is NOT here and its absence is deliberate: SPEC.md 58's monitor
# lost its drv_tab row to the RAM disk (the Drivers page fits exactly four),
# so nothing in the kernel names it and shipping the file would put a driver
# on every disk that no Control Panel row can load. Its source and its rules
# below are kept, so `make build/debug.drv` still builds it.
#
# RECURSIVE (`=`, not `:=`), because the on-demand modules appended below are
# per BUILD DIRECTORY (SPEC.md 2.8.2) and a rule that builds a kernel outside
# $(BUILD) overrides KMODDIR for itself - which a simply-expanded DRIVERS
# would have baked in at parse time.
DRIVERS = $(BUILD)/sound.drv $(BUILD)/hdd.drv $(BUILD)/net.drv
DRIVERS += $(BUILD)/ramdisk.drv
# ...and the hard-disk driver's on-demand half, which rides every disk the
# drivers do but is NOT one of them: nothing puts it in drv_tab, the Drivers
# page never lists it, and only HDD.DRV ever loads it (SPEC.md 52.11)
DRIVERS += $(BUILD)/hddtool.drv
# ...and the store above 1MB (SPEC.md 41.12), which is an overlay for the same
# reason and rides every KERN_BIG disk the same way: no drv_tab row, no
# Drivers-page tick, no SYSTEM.CFG bit. The kernel's own boot sniff decides
# whether to read it, so a machine with no memory up there never touches this
# file - and kern_small filters it back out again ($(SMALLDRIVERS) below),
# because SPEC.md 41.11 took the whole feature out of that kernel and nothing
# in it can name, read or load the file
DRIVERS += $(BUILD)/xmem.drv
# ...and the ON-DEMAND KERNEL MODULES (SPEC.md 2.8), which are neither a driver
# nor an overlay. Both of those are SELF-CONTAINED images with a dispatcher and
# an ABI; a module is this kernel's OWN CODE, cut out of the assembled binary by
# tools/os88mod.py, naming `cp_sel` and `dsk_secbuf` at the addresses this build
# put them at. They are .DRV for one reason: os88disk.py's sys_attr stamps
# anything ending DRV read-only+hidden+system by EXTENSION (SPEC.md 19.6), the
# installer's `*.DRV` copy rule picks them up, and ld_check_hdr refuses to launch
# one. Nothing lists them as drivers.
#
# LAST, because $(KMODS) is the only entry that reads $(KMODDIR), which the
# non-default image rules override per target - which is also why DRIVERS is a
# recursive `=` and not a `:=`.
DRIVERS += $(KMODS)
SYSAPPS := $(BUILD)/taskmgr.o88
SYSAPPSARGS := $(addprefix SYSTEM:,$(SYSAPPS))

# ...and MEDIA, which every disk carries: it is where a File Open or File
# Save starts (SPEC.md 38.10), so it has to exist on whatever volume the user
# is on. --folder is os88disk's way of saying "this folder, with nothing in
# it" - a folder otherwise exists only because a file named one - and it is
# still what the disks below that carry no media use.
MEDIAFOLDER := --folder MEDIA

# The logo (SPEC.md 63): 466x100 of monochrome GIF, and the one thing in
# MEDIA on the three SHIPPED system disks, which used to be empty. It is not
# decoration on a disk with room to spare - it is what makes the default
# folder open on SOMETHING. The dialog starts in MEDIA and a boot floppy had
# nothing to put there, so the first File > Open a new user ever ran showed
# them an empty list on a working machine.
#
# GENERATED, never committed: the artwork is code (tools/os88logo.py), for
# the reason fonts/*.f8 is ASCII art rather than hex - a picture's defects
# are entirely visual and a 2KB LZW blob is not reviewable. Scoped the way
# SYSDOC is, and for the same reason: `make field`'s narrow disks and the
# bench disks are clusters the benchmarks may want.
SYSLOGO := $(BUILD)/OS8088.GIF
SYSLOGOARG := MEDIA:$(SYSLOGO)

$(SYSLOGO): tools/os88logo.py | $(BUILD)
	python3 tools/os88logo.py -o $@

# SYSDOC is the manual, and it is deliberately NOT part of SYSAPPS: that list
# rides the apps disk (APPS_ROOT) and all five `make field` disks as well, and
# 16KB of prose on a 360KB benchmark disk is 16KB the benchmarks may want. It
# goes on the three SHIPPED system images and nowhere else.
#
# README.TXT: the user manual, in the root of the system disk, so the machine
# explains itself with no second disk and no host computer to read it on.
#
# Two constraints shape the source file and neither is arbitrary. Note Pad
# wraps by WORD (SPEC.md 27.11), so PROSE is written as one long line per
# paragraph and re-flows to whatever width the window is dragged to. What
# cannot re-flow is everything whose SHAPE is the meaning - the rules under a
# heading, the two-column key tables, the contents list - so those are
# hand-wrapped to 28 columns, one under the 29 that Note Pad's default window
# fits (260px frame, less the border, the 8px margin and the 14px scroll bar,
# over an 8px cell). And the whole file stays under 16KB because that is Note
# Pad's own ceiling (NP_MAXKB): a byte over and it refuses the file outright
# with 'Too big'. tools/checkreadme.py holds both, and runs before the file
# is used.
#
# CRLF is applied HERE rather than committed, so the repository copy stays a
# plain LF text file that diffs and merges normally, and the disk gets the
# DOS line endings a .TXT on a FAT floppy is expected to have (the disks are
# meant to be readable on a DOS PC - SPEC.md 19). The conversion is
# idempotent: LF is normalised out first, so re-running it never doubles a CR.
# --- the TYPEFACES (SPEC.md 6.4/19.8) ----------------------------------------
# One .F88 per family, built from the reviewable art in faces/, and carried in
# FONTS/ on the SYSTEM disk. A face is the machine's and not an application's -
# the same thing the kernel's own 8x8 cell is - so a second program wanting
# Charter finds it already there instead of carrying a copy. They are DATA: the
# mount types a directory entry as an application only when its extension is
# O88 (SPEC.md 19), so a .F88 can never be double-clicked into the loader.
#
# The list is generated from the directory, exactly as SPEC.md 6.2.1's FONT=
# targets are, so a new face is a new file and not an edit here as well.
FACESRC := $(wildcard faces/*.t88)
FACES := $(patsubst faces/%.t88,$(BUILD)/%.f88,$(FACESRC))

# ...and the LICENCE rides beside them (SPEC.md 6.4.1). Eight of the ten
# families are fitted from typefaces somebody else drew, all of them under the
# SIL Open Font License 1.1, which asks that its notice travel with anything
# derived from the font - so it travels on the disk the faces are on and not
# only in the tree. It is a .TXT: ty_scan takes .F88 and nothing else, so it
# cannot turn up in a Font menu, and the mount types it as a document, so the
# person at the machine can double-click it and read it. CRLF here for the
# same reason readme.txt gets it below.
FACELIC := $(BUILD)/license.txt
FACESARG := $(addprefix FONTS:,$(FACES)) FONTS:$(FACELIC)

$(BUILD)/%.f88: faces/%.t88 tools/os88face.py | $(BUILD)
	python3 tools/os88face.py $< -o $@

$(FACELIC): faces/LICENSES.txt | $(BUILD)
	python3 -c "import sys; d = open(sys.argv[1], 'rb').read(); \
		open(sys.argv[2], 'wb').write(d.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))" \
		$< $@

SYSDOC := $(BUILD)/readme.txt

$(BUILD)/readme.txt: readme.txt tools/checkreadme.py | $(BUILD)
	python3 tools/checkreadme.py $<
	python3 -c "import sys; d = open(sys.argv[1], 'rb').read(); \
		open(sys.argv[2], 'wb').write(d.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))" \
		$< $@

$(BUILD)/taskmgr.bin: apps/taskmgr/taskmgr.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/taskmgr/taskmgr.asm
	@echo "taskmgr: $(call FILESIZE,$@) bytes"

$(BUILD)/taskmgr.o88: $(BUILD)/taskmgr.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/taskmgr.bin -o $@

$(BUILD)/sound.bin: drivers/sound/sound.asm drivers/sound/sb.inc \
                    drivers/os88drv.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/sound/ -I drivers/ -I apps/ \
		-o $@ drivers/sound/sound.asm
	@echo "sound:  $(call FILESIZE,$@) bytes"

$(BUILD)/sound.drv: $(BUILD)/sound.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/sound.bin -o $@

# The store above 1MB (SPEC.md 41.12). An OVERLAY, not a driver: os88drv.py
# stamps it and names it 'overlay' because its class byte is DRVC_OVL, which
# the kernel deliberately does not know - so nothing can load it but xm_boot.
$(BUILD)/xmem.bin: drivers/xmem/xmem.asm drivers/os88drv.inc apps/os88api.inc \
                   | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/ -I apps/ -o $@ drivers/xmem/xmem.asm
	@echo "xmem:   $(call FILESIZE,$@) bytes"

$(BUILD)/xmem.drv: $(BUILD)/xmem.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/xmem.bin -o $@

# The MBR's 446 bytes of boot code (SPEC.md 52.10.1). Assembled on its own and
# incbin'ed by drivers/hdd/part.inc, which is why the driver gets -I $(BUILD):
# a chain-loader is too much to write as a `db` list and far too much to
# review as one, which is what the `Not bootable` stub it replaced was.
$(BUILD)/mbr.bin: boot/mbr.asm | $(BUILD)
	$(NASM) -f bin -w+error -o $@ $<
	@echo "mbr:    $(call FILESIZE,$@) bytes (of 446)"

# The hard disk's volume boot record (SPEC.md 52.10.2), likewise incbin'ed by
# the driver. NO -DKERNEL_SECTORS: unlike the floppy sectors, this one is
# built long before it knows which kernel it will boot, so the count is a word
# at a pinned offset that the installer patches when it writes the VBR.
$(BUILD)/boothd.bin: boot/boothd.asm | $(BUILD)
	$(NASM) -f bin -w+error -o $@ $<
	@echo "boothd: $(call FILESIZE,$@) bytes"

# HDDTOOL.DRV - the hard-disk driver's OTHER half (SPEC.md 52.11): the
# partitioner, the formatter and the installer, which only run while somebody
# is clicking on them and so have no business being resident. HDD.DRV reads it
# off the system volume on a Format or an Install click and frees it at detach.
#
# It is not a driver: no class the kernel knows, no drv_tab row, so it is NOT
# in DRIVERS below - but it does ride the same disks, because the .DRV suffix
# is what gives os88disk.py's sys_attr the read-only + hidden + system
# attributes every kernel-owned file wants (SPEC.md 19.6), and what makes the
# installer's "every *.DRV" copy pick it up (SPEC.md 52.10.4).
$(BUILD)/hddtool.bin: drivers/hdd/hddtool.asm apps/os88ui.inc drivers/hdd/hddabi.inc \
                  drivers/hdd/hdcom.inc drivers/hdd/hdsvc.inc drivers/hdd/hdsec.inc \
                  drivers/hdd/partw.inc drivers/hdd/fmt.inc drivers/hdd/tool.inc \
                  drivers/hdd/inst.inc drivers/os88drv.inc apps/os88api.inc \
                  $(BUILD)/mbr.bin $(BUILD)/boothd.bin | $(BUILD)
	$(NASM) -f bin -w+error $(DRVDEF) -I drivers/hdd/ -I drivers/ -I apps/ -I $(BUILD) -o $@ $<
	@echo "hddtool: $(call FILESIZE,$@) bytes"

$(BUILD)/hddtool.drv: $(BUILD)/hddtool.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/hddtool.bin -o $@

# -DHDTOOL_KB is the claim HDD.DRV makes for the tool, and it is injected the
# way boot.asm is told KERNEL_SECTORS: there is no file-size slot in the API,
# so a driver cannot ask how big a file is before it reads one. It is a
# CEILING - a bigger tool on the disk is refused by OSAPI_FILE_READ before any
# data moves, and a smaller one leaves the tail unread.
$(BUILD)/hdd.bin: drivers/hdd/hdd.asm apps/os88ui.inc drivers/hdd/hddabi.inc drivers/hdd/hdcom.inc \
                  drivers/hdd/hdtool.inc drivers/hdd/hdsec.inc \
                  drivers/hdd/page.inc drivers/hdd/cfg.inc \
                  drivers/os88drv.inc apps/os88api.inc \
                  $(BUILD)/hddtool.bin | $(BUILD)
	$(NASM) -f bin -w+error $(DRVDEF) -I drivers/hdd/ -I drivers/ -I apps/ -I $(BUILD) \
		-DHDTOOL_KB=$$(( ( $(call FILESIZE,$(BUILD)/hddtool.bin) + 1023 ) / 1024 )) \
		-o $@ $<
	@echo "hdd:    $(call FILESIZE,$@) bytes"

$(BUILD)/hdd.drv: $(BUILD)/hdd.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/hdd.bin -o $@

# DEBUG.DRV (SPEC.md 58) - the serial monitor. It SHIPS, and it costs a
# machine that never asks for it one drv_tab row and a file on the floppy:
# DRVR_WANT is 0 like every other row (SPEC.md 51.3), so nothing probes 2E8
# and nothing hooks IRQ3 until the Drivers page is ticked. That is the whole
# reason it is a driver rather than a SERDBG= kernel - a knob kernel is a
# different binary, and the machine you debugged is then not the machine that
# ships.
$(BUILD)/debug.bin: drivers/debug/debug.asm drivers/os88drv.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/debug/ -I drivers/ -I apps/ -o $@ $<
	@echo "debug:  $(call FILESIZE,$@) bytes"

$(BUILD)/debug.drv: $(BUILD)/debug.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/debug.bin -o $@

# NET.DRV - a LapLink parallel cable as a block volume (docs/NET-PLAN.md stage
# 1). Its transport is drivers/net/lplink.inc, which tests/lptlink includes
# too, so the thing PERFORMANCE.md Part 9 Set 39 measured is the thing that
# ships. OS88NET.COM is the other end of the cable: a DOS program for the FAR
# machine, which rides the apps disk in SYSTEM/DOS (APPS_DOS below) so the
# user has a copy to carry across - it used to be built here and sent, which
# only ever worked for someone with this repository.
#
# NETTURN1=1 leaves the reply deadline at TURN_RX once the link is up, which
# is the pre-SPEC.md-62.10.4.6 behaviour: 440ms for the far side to BEGIN
# answering a command. It is the A/B for the field bug where entering a
# subdirectory on a machine serving a floppy could not possibly succeed - the
# motor's spin-up alone exceeds it - and it exists as a knob because a harness
# that answers instantly measures the two builds identically. Drive it with
# tests/lptlink/partner.py's `stall` (62.10.4.6.1), which is the only thing
# here capable of being slow on purpose.
NETDEF :=
ifneq ($(NETTURN1),)
NETDEF += -DNET_TURN1
endif
NETSTAMP := $(BUILD)/.net-$(if $(NETTURN1),turn1,std)
$(shell mkdir -p $(BUILD); \
        [ -f $(NETSTAMP) ] || { rm -f $(BUILD)/.net-* $(BUILD)/net.bin \
                                      $(BUILD)/net.drv; \
                                touch $(NETSTAMP); })

$(BUILD)/net.bin: drivers/net/net.asm drivers/net/netui.inc \
                  drivers/net/lplink.inc drivers/os88drv.inc apps/os88api.inc \
                  apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error $(NETDEF) -I drivers/net/ -I drivers/ -I apps/ -o $@ $<
	@echo "net:    $(call FILESIZE,$@) bytes"

$(BUILD)/net.drv: $(BUILD)/net.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/net.bin -o $@

# RAMDISK.DRV - a DRVC_FILE volume with no hardware behind it (SPEC.md 62.9),
# and the FILE REDIRECTOR'S HARNESS: every branch site the redirector added to
# the kernel runs on a cycle-accurate 8088 in a container, which is the one
# thing block mode never had (docs/NET-PLAN.md 2.2.1). It ships because that is
# DEBUG.DRV's argument - a knob kernel is a different binary, so what you
# tested is not what ships - and it costs a machine that never ticks it one
# drv_tab row and a file on the floppy.
# RAMSEED=1 fills the RAM disk with the folders and files the redirector's
# branch sites were built against - two levels of directory, three text files
# and a copy of MINES.O88 (SPEC.md 62.9.5.1). It is OFF by default: those
# files cost the shipped floppy 1.8KB of driver image to carry a package it
# already has, and they cost the KERNEL nothing either way, a .drv being a
# heap claim. `make ramseed` is the target that builds it.
RDSEEDDEF :=
ifneq ($(RAMSEED),)
RDSEEDDEF += -DRDSEED
endif
RDSTAMP := $(BUILD)/.ramdisk-$(if $(RAMSEED),seed,bare)
$(shell mkdir -p $(BUILD); \
        [ -f $(RDSTAMP) ] || { rm -f $(BUILD)/.ramdisk-* $(BUILD)/ramdisk.bin \
                                     $(BUILD)/ramdisk.drv; \
                               touch $(RDSTAMP); })

$(BUILD)/ramdisk.bin: drivers/ramdisk/ramdisk.asm drivers/os88drv.inc \
                      apps/os88api.inc $(BUILD)/mines.o88 | $(BUILD)
	$(NASM) -f bin -w+error $(RDSEEDDEF) -I drivers/ramdisk/ -I drivers/ \
	        -I apps/ -I $(BUILD)/ -o $@ $<
	@echo "ramdisk: $(call FILESIZE,$@) bytes"

$(BUILD)/ramdisk.bin: $(RDSTAMP)

# ramseed: the populated RAM disk, for debugging the redirector's branch sites
# without a cable. Same disks, one driver rebuilt (SPEC.md 62.9.5.1).
.PHONY: ramseed
ramseed:
	$(MAKE) RAMSEED=1
	@echo "ramseed: build/ramdisk.drv carries DOCS/, DEEP/ and MINES.O88"

$(BUILD)/ramdisk.drv: $(BUILD)/ramdisk.bin tools/os88drv.py
	python3 tools/os88drv.py $(BUILD)/ramdisk.bin -o $@

$(BUILD)/os88net.com: drivers/net/os88net.asm drivers/net/lplink.inc \
                      drivers/net/lplslv.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/net/ -o $@ $<
	@echo "os88net.com: $(call FILESIZE,$@) bytes - the DOS end, for the FAR machine"

$(IMG): $(BUILD)/boot.bin $(BUILD)/kernel.bin $(DRIVERS) $(SYSAPPS) $(SYSDOC) $(SYSLOGO) $(FACES) $(FACELIC) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/boot.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(SYSDOC) $(SYSLOGOARG) $(FACESARG)

# The 720KB 3.5" DD disk (SPEC.md 19). It is the geometry the machines
# BETWEEN the two shipped ones have: an XT or AT fitted with a 3.5" DD drive,
# and - the reason it is worth a shipped image - every USB floppy drive and
# every Gotek/flash emulator made, which read 720KB and 1.44MB and nothing
# 5.25". So it is the image to write when the target machine cannot take a
# 360KB disk and cannot read a 1.44MB one either.
#
# Same boot sector as the 360KB disk (see boot360.bin above): 9 spt, 2 heads,
# 80 cylinders instead of 40, and the boot sector never counts cylinders.
$(IMG720): $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) $(SYSAPPS) $(SYSDOC) $(SYSLOGO) $(FACES) $(FACELIC) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(SYSDOC) $(SYSLOGOARG) $(FACESARG)

$(IMG360): $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) $(SYSAPPS) $(SYSDOC) $(SYSLOGO) $(FACES) $(FACELIC) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(SYSDOC) $(SYSLOGOARG) $(FACESARG)

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

# XMTEST: the extended-memory TEARDOWN gate (SPEC.md 41.5/29.4). It answers
# "when an instance holding blocks above 1MB closes, are they freed?", which
# needs a package because xm_alloc stamps a block with the CALLING INSTANCE -
# nothing outside one can make a block that belongs to a slot. It must run on
# a machine that HAS a store, so QEMU on a 386 rather than MartyPC's 8088:
#   make test TESTAPPS=build/xmtest.img
#   python3 tests/xmcheck.py build/qmp.sock
$(BUILD)/xmtest.bin: tests/xmtest/xmtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/xmtest/xmtest.asm
	@echo "xmtest: $(call FILESIZE,$@) bytes"

$(BUILD)/xmtest.o88: $(BUILD)/xmtest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/xmtest.bin -o $@

$(BUILD)/xmtest.img: $(BUILD)/xmtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/xmtest.o88

# LINETEST: the gate for SPEC.md 5.6.6, the 1bpp three-column walk. A
# deterministic fan of dilated steep lines and nothing else, so two kernels
# can be compared byte for byte over a framebuffer dump:
#   make test VIDEO=herc HERCSEG=0x7000 TESTAPPS=build/linetest.img
$(BUILD)/linetest.bin: tests/linetest/linetest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/linetest/linetest.asm
	@echo "linetest: $(call FILESIZE,$@) bytes"

$(BUILD)/linetest.o88: $(BUILD)/linetest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/linetest.bin -o $@

$(BUILD)/linetest.img: $(BUILD)/linetest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/linetest.o88

# FSXTEST: the fullscreen-exclusive gate package (SPEC.md 53.9). Like fmtest
# it is never on the shipped apps disks and rides its own scratch image:
#   make test TESTAPPS=build/fsxtest.img
$(BUILD)/fsxtest.bin: tests/fsxtest/fsxtest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/fsxtest/fsxtest.asm
	@echo "fsxtest: $(call FILESIZE,$@) bytes"

$(BUILD)/fsxtest.o88: $(BUILD)/fsxtest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/fsxtest.bin -o $@

$(BUILD)/fsxtest.img: $(BUILD)/fsxtest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/fsxtest.o88

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
$(BUILD)/notepad.bin: apps/notepad/notepad.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/notepad/notepad.asm
	@echo "notepad: $(call FILESIZE,$@) bytes"


$(BUILD)/notepad.o88: $(BUILD)/notepad.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/notepad.bin -o $@

# Calculator (SPEC.md 65): a four-function desk calculator with a foldaway
# history. It uses os88ui.inc for its twenty keys and OSAPI_WM_ONRESIZE to
# re-derive how many history rows fit whenever the kernel moves its box.
$(BUILD)/calc.bin: apps/calc/calc.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/calc/calc.asm
	@echo "calc:   $(call FILESIZE,$@) bytes"


$(BUILD)/calc.o88: $(BUILD)/calc.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/calc.bin -o $@

# ...and the REFERENCE build, which letters every history row where the
# shipped one scrolls the band (SPEC.md 65.4.1). It is the A/B for that
# claim - `make calcref` then `python3 tests/calcflick.py --ref` prices both
# on the same machine - and it goes on its own scratch image, never on a
# shipped disk.
calcref: $(BUILD)/calcref.img

$(BUILD)/calcref.bin: apps/calc/calc.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -DCALC_NOSCROLL -I apps/ -o $@ apps/calc/calc.asm
	@echo "calcref: $(call FILESIZE,$@) bytes (the no-scroll reference)"

$(BUILD)/calcref.o88: $(BUILD)/calcref.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/calcref.bin -o $@

$(BUILD)/calcref.img: $(BUILD)/calcref.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 APPS:$(BUILD)/calcref.o88

# TeXPad (SPEC.md 69): source on the left, typeset preview on the right, and
# File > Export writes PDF 1.4 or PostScript Level 1. Contributed.
#
# It ships on the ORDINARY apps disk rather than getting its own the way Word
# and Frotz did (SPEC.md 68.5/61.9), because the argument that gave them one
# does not apply: those two need a disk with DOCUMENTS on it - stories, a
# .DOC - and at 43KB Word does not leave room for the rest of the software on
# a 360KB floppy anyway. TeXPad is 20KB and its documents are two .TEX files
# of 3KB together, so it fits beside everything else with room to spare.
#
# Three sources, one binary, and each is a prerequisite: the typesetter and
# the exporter are where the page layout lives, and a stale texpad.bin reads
# exactly like the layout being wrong.
$(BUILD)/texpad.bin: apps/texpad/texpad.asm apps/texpad/tpparse.inc \
                     apps/texpad/tpexport.inc apps/os88api.inc \
                     apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ apps/texpad/texpad.asm
	@echo "texpad: $(call FILESIZE,$@) bytes"

$(BUILD)/texpad.o88: $(BUILD)/texpad.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/texpad.bin -o $@

# Piano, the fifth shipped package (SPEC.md 36): a colorful playable piano
# over the SPEC.md 34 tone tier (note viewer, replay, embedded songs).
$(BUILD)/piano.bin: apps/piano/piano.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
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
                      apps/tracker/trkui.inc apps/tracker/trktxt.inc \
                      apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/tracker/ -o $@ apps/tracker/tracker.asm
	@echo "tracker: $(call FILESIZE,$@) bytes"


$(BUILD)/tracker.o88: $(BUILD)/tracker.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/tracker.bin -o $@

# ModPlug Player, the fourteenth shipped package (SPEC.md 56): a port of
# ModPlug Player V2's LOOK AND FEEL - the skinned player window with its LCD
# panel, LED transport row and visualiser, the Setup window with its page
# list, and the PlayList editor - onto the window manager. Its replayer is an
# INDEPENDENT copy of the tree's 8086 ProTracker engine (ModPlugPlayer's own
# is libopenmpt, which no 8086 runs), extended with the four DSP stages its
# Setup pages expose. Four sources, one binary.
$(BUILD)/modplug.bin: apps/modplug/modplug.asm apps/modplug/mppmix.inc \
                      apps/modplug/mppui.inc apps/modplug/mppset.inc \
                      apps/modplug/mpplist.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/modplug/ -o $@ apps/modplug/modplug.asm
	@echo "modplug: $(call FILESIZE,$@) bytes"

$(BUILD)/modplug.o88: $(BUILD)/modplug.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/modplug.bin -o $@

# ...and the SAME SOURCE with -DMPPDEBUG, which is the tests/trklog.inc shape
# (SPEC.md 45.14): one source, every hook inside %ifdef, so the shipped
# MODPLUG.O88 carries none of it and the two cannot drift. It exists for a
# reported hard freeze on 'L' that reproduces on nobody's emulator here - see
# the MPPDBG macro in modplug.asm for what it draws and why the screen is the
# only channel a stopped machine still has.
#
# It builds a WHOLE FLOPPY PAIR rather than a package, because the reporter
# needs something to boot: build/dbg-os8088-360.img is the ordinary system
# disk and build/dbg-apps360.img is the apps disk with the instrumented
# player in place of the shipped one. Nothing here is in `all` and nothing
# ships.
modplugdbg: $(BUILD)/dbg-apps360.img

$(BUILD)/dbg/modplug.bin: apps/modplug/modplug.asm apps/modplug/mppmix.inc \
                      apps/modplug/mppui.inc apps/modplug/mppset.inc \
                      apps/modplug/mpplist.inc apps/os88api.inc | $(BUILD)
	@mkdir -p $(BUILD)/dbg
	$(NASM) -f bin -w+error -DMPPDEBUG -I apps/ -I apps/modplug/ -o $@ \
	        apps/modplug/modplug.asm
	@echo "modplug (MPPDEBUG): $(call FILESIZE,$@) bytes"

$(BUILD)/dbg/modplug.o88: $(BUILD)/dbg/modplug.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/dbg/modplug.bin -o $@

$(BUILD)/dbg-apps360.img: $(BUILD)/dbg/modplug.o88 $(APPS_TOOLS) $(APPS_GAMES) \
                          $(SYSAPPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
	    $(patsubst %,APPS:%,$(filter-out $(BUILD)/modplug.o88,$(APPS_TOOLS))) \
	    APPS:$(BUILD)/dbg/modplug.o88 \
	    $(patsubst %,GAMES:%,$(APPS_GAMES)) \
	    MEDIA:apps/tracker/beverly.mod \
	    $(patsubst %,SYSTEM:%,$(SYSAPPS))
	@echo "modplugdbg: boot build/os8088-360.img with $@ as the APPS disk"

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
		apps/os88api.inc apps/os88ui.inc | $(BUILD)
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
# offscreen canvas, one-level undo/redo, an internal clipboard and
# BMP load/save through the Standard File dialog. Needs ~620KB of conventional
# memory for its canvas (int 12h decides; a smaller machine gets a notice
# window instead), so `make run-640` is the way to exercise it.
$(BUILD)/paint.bin: apps/paint/paint.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
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
	$(NASM) -f bin -w+error $(SOLDEF) -I apps/ -o $@ apps/solitaire/solitaire.asm
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

# Cyclone 88, a Tempest 2000 clone (SPEC.md 67). The web is a polygon of rim
# vertices in a normalised space plus a depth ladder, resolved ONCE per layout
# into a vertex table, so no frame does any perspective arithmetic. It is drawn
# once and never again: level entry EXTRUDES it a few pixels of every spoke per
# frame through SPEC.md 5.6.7's resumable walk, batched into one
# OSAPI_GFX_LSTEPV a frame, and level exit replays the identical walks in the
# background colour so the erase visits exactly the pixels the draw visited.
# Every mover is a rect drawn strictly inside its lane, which is what lets an
# erase be one gfx_fill rather than a repair. No heap claim.
# CYTRACE=1 records the CALLER of every background fill landing in a watch
# rect the host writes into the app's bss - the instrument that settled which
# routine was erasing the movers, after three source-reading theories missed.
# It is not in `all` and costs the shipped build nothing.
CYCFLAGS :=
ifdef CYTRACE
CYCFLAGS += -DCYTRACE
endif
$(BUILD)/cyclone.bin: apps/cyclone/cyclone.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ $(CYCFLAGS) -o $@ apps/cyclone/cyclone.asm
	@echo "cyclone: $(call FILESIZE,$@) bytes"

$(BUILD)/cyclone.o88: $(BUILD)/cyclone.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/cyclone.bin -o $@

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
# heapfrag - the heap-compaction gate (SPEC.md 66.8). Its own scratch image
# like filetest's, and it needs no data file: the whole suite is heap.
#
#   make marty TESTAPPS=build/heapfrag.img          the one that matters
#   make marty TESTAPPS=build/heapfrag.img HEAPCOMPACT=0    the A/B
#
# With HEAPCOMPACT=0 checks 7 and 10 MUST fail and 8 and 9 must still pass:
# nothing moved, so nothing was corrupted and nothing was claimed. That pattern
# is the gate - a suite that passes against both kernels is measuring something
# other than compaction. (Check 11 passes in both, honestly: it compares moves
# against notifications and both are 0. That is why 10 exists.)
$(BUILD)/heapfrag.bin: tests/heapfrag/heapfrag.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/heapfrag/heapfrag.asm
	@echo "heapfrag: $(call FILESIZE,$@) bytes"

$(BUILD)/heapfrag.o88: $(BUILD)/heapfrag.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/heapfrag.bin -o $@

$(BUILD)/heapfrag.img: $(BUILD)/heapfrag.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/heapfrag.o88

# ...and the 360KB twin, which is the one that gets used: every 5150 machine
# config in tools/martypc has 360KB drives, and a 1.44MB image mounted in one
# does not error - the Disk window opens HIDDEN and the run reports that the
# package would not launch (SPEC.md 66.8).
# PAINT rides along, because tests/paintmove.py needs a REAL holder with a
# real derived row table on the heap while heapfrag forces a compaction: the
# canvas is the biggest claim on the machine and the one whose relocation proc
# has actual work to do (SPEC.md 66.2, apps/paint's pt_reloc).
$(BUILD)/heapfrag360.img: $(BUILD)/heapfrag.o88 $(BUILD)/paint.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/heapfrag.o88 \
		$(BUILD)/paint.o88

# ...and Tracker's own disk, for tests/trackmove.py (SPEC.md 66.5.2). It is a
# SEPARATE image and not an addition to heapfrag360: the listing is sorted by
# name (SPEC.md 19.4), so dropping BEVERLY.MOD into that one renumbers every
# row tests/heapcheck.py clicks.
#
# BEVERLY.MOD rides in the ROOT rather than MEDIA because the point is a
# double-click on the .MOD row - Tracker claims the extension (SPEC.md 54), so
# the association opens the app AND loads the module in one action, where
# driving its File menu means a Standard File dialog on a 640x200 screen.
$(BUILD)/trackmove360.img: $(BUILD)/heapfrag.o88 $(BUILD)/tracker.o88 \
                           apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/heapfrag.o88 \
		$(BUILD)/tracker.o88 apps/tracker/beverly.mod

# ...and the three editors' disk, for tests/editmove.py (SPEC.md 66.5.7). One
# image for all three because each run needs heapfrag plus exactly ONE app -
# the app has to land ABOVE heapfrag in the arena, and a second app opened
# first would sit between them. The listing is sorted by name (SPEC.md 19.4):
# ARTFUL 0, FRACTAL 1, HEAPFRAG 2, NOTEPAD 3, which is what editmove.py's
# ROW_* constants say.
$(BUILD)/editmove360.img: $(BUILD)/heapfrag.o88 $(BUILD)/notepad.o88 \
                          $(BUILD)/fractal.o88 $(BUILD)/artful.o88 \
                          tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/heapfrag.o88 \
		$(BUILD)/notepad.o88 $(BUILD)/fractal.o88 $(BUILD)/artful.o88

# ModPlug's own disk, for tests/trackmove.py --app modplug (SPEC.md 66.5.8).
# Same shape as trackmove360 and separate for the same reason: the listing is
# sorted by name (SPEC.md 19.4), so an extra package renumbers every row the
# script clicks. ModPlug does NOT own .MOD (SPEC.md 56.13 leaves that pointed
# at Tracker), so the module is opened through its own File menu rather than
# by a double-click on the row.
$(BUILD)/mppmove360.img: $(BUILD)/heapfrag.o88 $(BUILD)/modplug.o88 \
                         apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/heapfrag.o88 \
		$(BUILD)/modplug.o88 apps/tracker/beverly.mod

# ...and Frotz's, for tests/editmove.py --app frotz (SPEC.md 66.5.9).
#
# The STORY is compiled here rather than fetched: tools/getstories.py needs the
# network once and this has to run in a container that has none, and no story
# file may be committed (SPEC.md 61.9). tests/frotz/zopstest.inf is the tree's
# own Inform source and `make zpic` already sets the precedent for inform being
# an on-demand dependency. v5 because zopstest uses call_vn, which v3 has not.
#
# Frotz OWNS .Z5 (SPEC.md 54), so one double-click on the row opens the app and
# loads the story - the trackmove.py route, and the reason this is easier to
# drive than ModPlug.
$(BUILD)/zt/ZOPS.Z5: tests/frotz/zopstest.inf
	@mkdir -p $(BUILD)/zt
	inform -v5 $< $@

$(BUILD)/zmove360.img: $(BUILD)/heapfrag.o88 $(BUILD)/frotz.o88 \
                       $(BUILD)/zt/ZOPS.Z5 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/heapfrag.o88 \
		$(BUILD)/frotz.o88 $(BUILD)/zt/ZOPS.Z5

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

# muptest: the SPEC.md 13.7 gate - a package's mouse-up. Its answers are a
# WINDOW that is there or not, so a harness reads wm_wins rather than pixels.
# The case only a package can prove is the FIRST rule: a press that ran no
# W_ONCLICK owes no release, which the kernel cannot test from its own side
# because it has no way to know a package expected nothing.
#
#   make test TESTAPPS=build/muptest.img
$(BUILD)/muptest.bin: tests/muptest/muptest.asm apps/os88api.inc apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/muptest/muptest.asm
	@echo "muptest: $(call FILESIZE,$@) bytes"

$(BUILD)/muptest.o88: $(BUILD)/muptest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/muptest.bin -o $@

$(BUILD)/muptest.img: $(BUILD)/muptest.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/muptest.o88

# assoctest: the SPEC.md 54 gate. Its own scratch image, and a TEST.AST for it
# to be opened WITH - the point of the gate is what happens on a document
# double-click, so the fixture is half the test:
#   make test TESTAPPS=build/assoctest.img     then double-click TEST.AST
# Launching ASSOCTEST.O88 by hand is the control: rows 1-4 read '-'.
$(BUILD)/assoctest.bin: tests/assoctest/assoctest.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/assoctest/assoctest.asm
	@echo "assoctest: $(call FILESIZE,$@) bytes"

$(BUILD)/asstest.o88: $(BUILD)/assoctest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/assoctest.bin -o $@

$(BUILD)/test.ast: Makefile | $(BUILD)
	printf 'os8088 association gate fixture\n' > $@

$(BUILD)/assoctest.img: $(BUILD)/asstest.o88 $(BUILD)/test.ast tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/asstest.o88 $(BUILD)/test.ast

# The same package on a legally fragmented volume: --scramble interleaves the
# chains, so the write path's allocator and the free/replace paths meet holes
# rather than a clean run of clusters. BIG.DAT rides this image too - checks
# 2..5 need it, and a 96KB chain walked across holes is the strongest version
# of what --scramble exists to test.
$(BUILD)/filetest-frag.img: $(BUILD)/filetest.o88 $(BUILD)/big.dat tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 --scramble $(BUILD)/filetest.o88 $(BUILD)/mines.o88 $(BUILD)/piano.o88 $(BUILD)/big.dat

# =============================================================================
# THE C TOOLCHAIN (SPEC.md 70) - ON DEMAND: `make cc-smoke`, `make chello`,
#                                           `make cword`, `make cworddisk`
# =============================================================================
# apps/cc/Makefile.inc holds the rules that turn a .c file into an .o88: the
# two new steps (smlrcc -tiny -S, then tools/cc8086.py, which lowers the seven
# non-8086 forms SmallerC emits and REFUSES the C that is silently wrong here
# - SPEC.md 70.6, 67.10) in front of the three steps every assembly package
# already goes through unchanged. Its header asks to be included "anywhere
# after $(BUILD) and $(NASM) are defined"; here also puts it after FILESIZE,
# which its size report uses.
#
# Until this line existed nothing in the tree built any C at all, while three
# files already advertised `make cc-smoke`. That target is real from here.
#
# NOTHING IN `all` REACHES ANY OF IT, in two separate senses and both on
# purpose:
#
#  * THE DEFAULT BUILD DOES NOT NEED THE COMPILER. SmallerC is not in this
#    tree - tools/setup-cc.sh fetches it at a pinned commit into build/cc/,
#    which is gitignored, so no compiler binary and no compiler source is ever
#    committed (SPEC.md 70.1). A clone with nasm and python3 and nothing else
#    builds every floppy this project ships; `make` there prints the one
#    paragraph cc-note holds and exits 0. Every rule below reaches the
#    compiler only through the `cc-toolchain` order-only guard in
#    apps/cc/Makefile.inc, which prints the command to run rather than failing
#    from inside a recipe with "no such file".
#
#  * CWORD DOES NOT RIDE THE SHIPPED APPS DISKS. It takes Frotz's and Word's
#    precedent (SPEC.md 61, 65.5, and 67.12 names the disk and the machine):
#    its own floppy, all three geometries, built only when asked for.
include apps/cc/Makefile.inc

# The one thing the default build says about C, and it says it only when there
# is something to say. A guard nobody types is a guard that does not run
# (SPEC.md 15.1's shape, the same argument checkdocs is in `all` for) - but
# the converse also holds, so this is silent on a tree that has run
# setup-cc.sh, and it can never fail a build.
cc-note:
	@test -x $(CC_SMLRCC) || { \
	  echo "";                                                              \
	  echo "note: the C toolchain (SPEC.md 70) is not built, so the C";     \
	  echo "      targets - cc-smoke, chello, cword, cworddisk and";        \
	  echo "      386-c-word - are unavailable. Everything else, which is"; \
	  echo "      every floppy this project ships, is built above.";        \
	  echo "";                                                              \
	  echo "      To get it:  tools/setup-cc.sh";                           \
	  echo "";                                                              \
	  echo "      It fetches SmallerC at its pinned commit into build/cc/"; \
	  echo "      - the compiler is not in this tree because build/ is";    \
	  echo "      gitignored - builds three binaries, and runs a canary C"; \
	  echo "      file through the whole chain.";                           \
	  echo ""; }

# --- CHELLO, the C capability gate (ON DEMAND: `make chello`) ----------------
# tests/chello is the program that established a compiled package can hold a
# window down on all three adapters: it has been booted on VGA 640x480, on CGA
# 640x200 and off a 360KB floppy, clicked, typed at, dragged and closed. It is
# under tests/ because it is a capability gate and nothing under tests/ ships
# (CLAUDE.md), so it is on demand exactly like bench.
#
# IT IS OPEN-CODED RATHER THAN CALLED THROUGH CC_PACKAGE, and the choice is
# worth writing down because the template is right there. CC_PACKAGE is
# `$(eval $(call CC_PACKAGE,<name>,<dir under apps/>))` and it roots BOTH the
# source and the shim at apps/$(2)/ - chello is under tests/, and its nasm
# line needs a third -I as well. The two ways out are a second template in
# apps/cc/Makefile.inc taking a directory, or one open-coded rule here. This
# is the second, for two reasons: there is exactly one C package outside
# apps/ and there is meant to be exactly one, so a generalised template would
# be a parameter with a single caller; and it would put knowledge of tests/
# into the SDK's own build fragment, which is the file a C author reads to
# learn how to ship an application. If a second tests/ C package ever turns
# up, that is the moment to lift these four rules into a directory-taking
# CC_PACKAGE_AT and give both callers the same one.
$(BUILD)/chello.raw.asm: tests/chello/chello.c $(CC_RUNTIME) | $(BUILD) cc-toolchain
	PATH="$(CURDIR)/$(CC_SC):$$PATH" $(CC_SMLRCC) -tiny -S \
		-SI $(CC_SCINC) -I $(CC_SCINC) -I $(CC_DIR) \
		tests/chello/chello.c -o $@

$(BUILD)/chello.gen.asm: $(BUILD)/chello.raw.asm tools/cc8086.py
	python3 tools/cc8086.py $< -o $@ --max-frame $(CC_MAXFRAME)

$(BUILD)/chello.bin: tests/chello/chello.asm $(BUILD)/chello.gen.asm $(CC_RUNTIME) | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I $(BUILD)/ -I tests/chello/ \
		-o $@ tests/chello/chello.asm
	@echo "chello: $(call FILESIZE,$@) bytes"

$(BUILD)/chello.o88: $(BUILD)/chello.bin tools/os88pkg.py
	python3 tools/os88pkg.py $< -o $@

# Two geometries and not three, which is tests/chello/build.sh's own choice
# and the right one for a gate: 1.44MB is what QEMU gets and 360KB is what an
# 86Box XT or a real one takes, and the 720KB disk would exercise no third
# thing. A shipped image is a different obligation and gets all three.
$(BUILD)/chello.img: $(BUILD)/chello.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/chello.o88
	@python3 tools/os88disk.py --verify $@

$(BUILD)/chello360.img: $(BUILD)/chello.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/chello.o88
	@python3 tools/os88disk.py --verify $@

#   make chello                            builds both images
#   make test TESTAPPS=build/chello.img    boots with it in B:
chello: $(BUILD)/chello.img $(BUILD)/chello360.img

# --- COVL, the C OVERLAY capability gate (ON DEMAND: `make covl`) ------------
# tests/covl is what SPEC.md 70.14 rests on: a compiled package half of whose
# code is in a second segment, read off the floppy on demand and far-called
# both ways. Open-coded for the same reason chello is - it is under tests/, so
# CC_PACKAGE's apps/ rooting does not fit and its nasm line needs a third -I -
# and if a third tests/ C package ever turns up, THAT is the moment to lift
# these rules into a directory-taking CC_PACKAGE_AT rather than write them a
# third time.
#
# The disk carries two files: COVL.O88 and the module beside it. Boot it, press
# SPACE, and read the numbers - each one is a different way for the mechanism
# to be wrong (tests/covl/covl.c says which).
$(BUILD)/covl.raw.asm: tests/covl/covl.c $(CC_RUNTIME) | $(BUILD) cc-toolchain
	PATH="$(CURDIR)/$(CC_SC):$$PATH" $(CC_SMLRCC) -tiny -S \
		-SI $(CC_SCINC) -I $(CC_SCINC) -I $(CC_DIR) \
		tests/covl/covl.c -o $@

$(BUILD)/covl.gen.asm: $(BUILD)/covl.raw.asm tools/cc8086.py
	python3 tools/cc8086.py $< -o $@ --max-frame $(CC_MAXFRAME)

$(BUILD)/covl.bin: tests/covl/covl.asm $(BUILD)/covl.gen.asm $(CC_RUNTIME) | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I $(BUILD)/ -I tests/covl/ \
		-o $@ tests/covl/covl.asm
	@echo "covl: $(call FILESIZE,$@) bytes (image + module)"

$(BUILD)/covl.o88: $(BUILD)/covl.bin tools/os88pkg.py tools/os88ovl.py
	python3 tools/os88ovl.py $< -o $(BUILD)/COVL.OVL \
		--trim $(BUILD)/covl.trim.bin
	python3 tools/os88pkg.py $(BUILD)/covl.trim.bin -o $@

$(BUILD)/covl.img: $(BUILD)/covl.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/covl.o88 $(BUILD)/COVL.OVL
	@python3 tools/os88disk.py --verify $@

$(BUILD)/covl360.img: $(BUILD)/covl.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		$(BUILD)/covl.o88 $(BUILD)/COVL.OVL
	@python3 tools/os88disk.py --verify $@

#   make covl                            builds both images
#   make test TESTAPPS=build/covl.img    boots with it in B:
covl: $(BUILD)/covl.img $(BUILD)/covl360.img

# --- CWORD and its document floppy (SPEC.md 70.12) ---------------------------
# The C toolchain's demonstrator: a word processor whose UI, layout, redraw
# and RTF engine are all C, going through the same five steps ccsmoke and
# chello do. `make cword` builds the package, `make cworddisk` the floppy in
# all three geometries, and `make 386-c-word` boots a period machine with it.
#
# IT IS NOT THE WORD PORT (SPEC.md 70.12). §68's apps/word/ is hand-written
# assembly and the two share no file, no package name, no make target, no disk
# image, no VM directory and no extension. Nothing here may reach a `word`
# name, and nothing in the Word section above may reach a `cword` one.
$(eval $(call CC_PACKAGE,cword,cword,CWORD.OVL))

# THE REST OF THE TRANSLATION UNIT. `nasm -f bin` has no notion of an external
# symbol, so a C package is ONE compilation and one assembly (SPEC.md 70.1):
# cword.c #includes the RTF tables and the RTF engine, and the shim %includes
# the one hand-written routine (SPEC.md 70.11's exception, cw_memmove - the
# only place ES is loaded). CC_PACKAGE names apps/cword/cword.c and
# apps/cword/cword.asm, which is right for the general case and four files
# short here, and make cannot see through a #include. Without these two lines
# an edit to the RTF engine or to the byte mover leaves build/cword.o88
# untouched - and a stale package reads exactly like the change having done
# nothing, which is the failure the WORD.OVL rule above already paid for once.
CWORDSRC := apps/cword/cwrtfio.c apps/cword/cwrtftbl.c apps/cword/cwrtftbl.h \
            apps/cword/cwmenu.c apps/cword/cwchrome.c apps/cword/cwdrop.c \
            apps/cword/cwcmd.c apps/cword/cwovl.c
$(BUILD)/cword.raw.asm: $(CWORDSRC)
$(BUILD)/cword.bin: apps/cword/cwmove.inc

cword: $(BUILD)/cword.o88

# All three geometries, as every disk-visible image in this tree is built
# (CLAUDE.md): 1.44MB and 720KB for QEMU, 360KB for an 86Box XT or a real one.
# The C toolchain has been booted from a 360KB floppy once, on chello, and
# that is the geometry a 20KB image most wants re-checked on.
#
# --verify is a standalone structural fsck of what came out (tools/os88disk.py)
# and it is in the recipe rather than in a separate target because it costs
# milliseconds and catches the class of defect - a bad FAT chain, a directory
# entry pointing at nothing - that otherwise arrives as "Disk error" inside
# the emulator, ten minutes later, reading like a bug in the file system.
# WELCOME.RTF rides the ROOT of all three, beside CWORD.O88 - which is where
# the assembly port puts WELCOME.DOC and for a reason that is not tidiness:
# a double-click on the document launches the program through SPEC.md 54.4.2,
# and assoc_back then leaves the app's current directory on the DOCUMENT's
# (SPEC.md 54.9, 19.2.1). CWORD.OVL is resolved in that directory (SPEC.md
# 70.14), so a document in a folder of its own would open a program whose
# every menu then refused, politely and inexplicably.
$(BUILD)/WELCOME.RTF: tools/os88rtf.py tools/os88doc.py apps/cword/welcome.wtx | $(BUILD)
	python3 tools/os88rtf.py apps/cword/welcome.wtx -o $@

cworddisk: $(BUILD)/cword.img $(BUILD)/cword720.img $(BUILD)/cword360.img

CWORDDISK := $(BUILD)/cword.o88 $(BUILD)/CWORD.OVL $(BUILD)/WELCOME.RTF

$(BUILD)/cword.img: $(CWORDDISK) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(CWORDDISK) --folder DOCS
	@python3 tools/os88disk.py --verify $@

$(BUILD)/cword720.img: $(CWORDDISK) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 $(CWORDDISK) --folder DOCS
	@python3 tools/os88disk.py --verify $@

$(BUILD)/cword360.img: $(CWORDDISK) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(CWORDDISK) --folder DOCS
	@python3 tools/os88disk.py --verify $@

# --- RUNCPM, RunCPM 6.9 as a C package (SPEC.md 71) --------------------------
# The C toolchain's second application: a CP/M 2.2 emulator - a Z80 in a 64KB
# claim, BIOS/BDOS in C, drives as folders, an 80x25 terminal in a window - a
# reimplementation of Marcelo Dantas / Mockba the Borg's RunCPM (MIT). `make
# runcpm` runs the host checks (apps/runcpm/build.sh - the terminal against a
# model of the glass) and then builds the package; `make runcpmdisk` the
# floppy. Nothing here is on the shipped apps disks, and nothing in `all`
# reaches it: on demand like cword, through the same cc-toolchain guard.
#
# THE HOST CHECKS RUN FIRST AND STOP THE BUILD. They are an order-only-free
# prerequisite of the .raw.asm through the stamp below: a check that fails
# leaves no stamp, and the compile does not run.
$(eval $(call CC_PACKAGE,runcpm,runcpm,RUNCPM.OVL))

# THE REST OF THE TRANSLATION UNIT (SPEC.md 70.1): runcpm.c #includes the
# parts, and the shim %includes the three hand-written pieces and the icon. Every one is a
# written prerequisite because make cannot see through either kind of include
# - and every file the port plan names is listed from wave 1, stubs included,
# so no later wave adds a file the build does not know (docs/RUNCPM-PORT-PLAN.md).
RUNCPMSRC := apps/runcpm/rcterm.c apps/runcpm/rccpm.c apps/runcpm/rcfs.c \
             apps/runcpm/rcabout.c
RUNCPMINC := apps/runcpm/rcz80.inc apps/runcpm/rcmem.inc apps/runcpm/rcband.inc
RUNCPMHOST := apps/runcpm/build.sh apps/runcpm/hosttest/os88.h \
              apps/runcpm/hosttest/rcuitest.c apps/runcpm/hosttest/rcfstest.c \
              apps/runcpm/hosttest/rcmemtest.asm \
              apps/runcpm/hosttest/rcmemtest.sh $(RUNCPMINC)
$(BUILD)/runcpm.raw.asm: $(RUNCPMSRC) $(BUILD)/.runcpm-hostchecks
$(BUILD)/runcpm.bin: $(RUNCPMINC) apps/runcpm/icon.inc

# ($(RUNCPMINC) is in RUNCPMHOST because rcmemtest.asm %includes rcmem.inc and
# rcz80.inc: an edit to a mover must re-run the mover harness, and make
# cannot see through a %include - LESSONS.md 9.)
$(BUILD)/.runcpm-hostchecks: apps/runcpm/runcpm.c $(RUNCPMSRC) $(RUNCPMHOST) | $(BUILD)
	apps/runcpm/build.sh
	@touch $@

runcpm: $(BUILD)/runcpm.o88

# THE MASTER DISK AND THE CCP ARE FETCHED, NEVER COMMITTED (CONTRIBUTING.md 6,
# SPEC.md 71.5): tools/getruncpm.py takes RunCPM's CCP-DR.60K, LICENSE,
# 1STREAD.ME and DISK/A0.zip at the pinned commit (the same hash the banner's
# 'Built' line names), verifies every SHA-256, and unpacks the master disk
# into build/runcpm-disk/A/0 minus the three files above 65,535 bytes (which
# A/0/LEFT-OFF.TXT names). A stamp rather than a directory, as the story cache
# is: make cannot depend on eighty files, and the script is idempotent -
# nothing is downloaded twice. `make runcpm-src` alone fetches.
RUNCPMDIR := $(BUILD)/runcpm-disk
$(BUILD)/runcpm-src.stamp: tools/getruncpm.py | $(BUILD)
	python3 tools/getruncpm.py -o $(RUNCPMDIR)
	@touch $@

runcpm-src: $(BUILD)/runcpm-src.stamp

# HELLO.COM - the hand-assembled Z80 hello the wave-2 gate loads with the
# debug key (docs/RUNCPM-PORT-PLAN.md): LD C,9 / LD DE,0109h / CALL 5 / RET,
# then the string - 49 bytes: nine of Z80 and a 40-byte message; the RET
# goes to the 0000 the loader put on the stack, so it also exercises the
# warm-boot path (SPEC.md 71). Emitted here rather than assembled because
# there is no Z80 assembler in this tree and nine bytes of code are not worth
# adding one. It is a BUILD ARTIFACT of this tree, not master-disk
# content, and it ships on NO image (SPEC.md 71.5 - wave 6's curation took
# it off build/runcpm.img's root, where wave 2's gate had it: a released
# disk carries RunCPM's files and nothing invented here, in A\0 or beside
# it). It is still built, for a hand test of the loader's launch-folder
# path: put it in the root of a SCRATCH copy of an image (tools/os88disk.py)
# and Alt+L HELLO. tests/rczex.py needs no such row - RUNCPM.O88 is the
# fifth listed row of the shipping root.
$(BUILD)/HELLO.COM: | $(BUILD)
	printf '\016\011\021\011\001\315\005\000\311Hello from the Z80 - RunCPM on os8088\r\n$$' > $@

# THE THREE FLOPPIES (SPEC.md 71.5): the package in the root beside the CCP it
# loads (before any folder move: the same rule as CWORD.OVL), RunCPM's LICENSE
# and 1STREAD.ME, and drive A user 0 - the master disk as far as the geometry
# holds it, chosen at recipe time by getruncpm.py --select (the texts and
# submit files first, then the programs, then documentation, libraries and
# sources; 720KB and 1.44MB carry all of it, 360KB the programs and texts), so
# no manifest is checked in. A/0 holds 77 files on 1.44MB, past the Disk
# window's 32-entry listing cap, which is a DISPLAY cap (SPEC.md 19): the file
# API walks them all, and --deep-folders is os88disk.py's word for a folder
# that is a data store rather than a place to browse. Each image is
# --verify'd, and the verify is what catches a --select that overshot - but
# --select is told what it is choosing beside: --reserve names the root files
# (the package, an .OVL if one comes, the CCP, the texts) and prices them
# in the geometry's own clusters, so the A/0
# selection re-shapes itself as the package grows instead of the 360KB
# build stopping at 'data over capacity'. The selection is a shell
# substitution INSIDE the recipe, so a --select that fails (no A0.list, a
# bad geometry) would otherwise print nothing and the image would build with
# an empty A/0 and verify clean - which reads exactly like a working disk.
# RUNCPMIMG therefore keeps its stderr and stops the recipe when the
# selection is empty. $(3) is the geometry's extra root files, if any.
RUNCPMDISK := $(BUILD)/runcpm.o88 $(BUILD)/RUNCPM.OVL $(RUNCPMDIR)/CCP-DR.60K \
              $(RUNCPMDIR)/LICENSE $(RUNCPMDIR)/1STREAD.ME
RUNCPMDEPS := $(BUILD)/runcpm.o88 $(BUILD)/RUNCPM.OVL $(BUILD)/runcpm-src.stamp \
              tools/os88disk.py tools/getruncpm.py
# A\0 SHIPS WITH SPARE DIRECTORY SLOTS (SPEC.md 71.3): the kernel does not
# grow a directory (SPEC.md 18.5, FERR_DIRFULL), so a folder a CP/M session
# saves into - MBASIC's SAVE, TE's write, PIP's copy, SUBMIT's $$$.SUB - must
# have its room built in; os88disk.py's own sizing leaves ONE free slot after
# the master disk's 77 files, and the second save failed 'Not saved: X'
# (found on the glass in wave 4). 128 entries is the size of RUNCPM's
# directory cache, so A\0 holds 126 files, and getruncpm.py --select prices
# the same figure so the fill cannot overflow the disk.
RUNCPMSLOTS := 128
define RUNCPMIMG
sel="$$(python3 tools/getruncpm.py -o $(RUNCPMDIR) --select $(2) --dir-slots $(RUNCPMSLOTS) --reserve $(RUNCPMDISK) $(3) | sed 's,^,A/0:,')"; \
[ -n "$$sel" ] || { echo "runcpm: getruncpm.py --select $(2) chose nothing"; exit 1; }; \
python3 tools/os88disk.py -o $(1) --size $(2) --deep-folders --dir-slots A/0=$(RUNCPMSLOTS) $(RUNCPMDISK) $(3) $$sel
endef

runcpmdisk: $(BUILD)/runcpm.img $(BUILD)/runcpm720.img $(BUILD)/runcpm360.img

$(BUILD)/runcpm.img: $(RUNCPMDEPS)
	$(call RUNCPMIMG,$@,1440)
	@python3 tools/os88disk.py --verify $@

$(BUILD)/runcpm720.img: $(RUNCPMDEPS)
	$(call RUNCPMIMG,$@,720)
	@python3 tools/os88disk.py --verify $@

$(BUILD)/runcpm360.img: $(RUNCPMDEPS)
	$(call RUNCPMIMG,$@,360)
	@python3 tools/os88disk.py --verify $@

# THE CORE GATES (SPEC.md 71, docs/RUNCPM-PORT-PLAN.md wave 2). `make rczex`
# is the plan's: boot build/runcpm.img in QEMU, launch RUNCPM, load ZEXDOC
# through the debug key and read the terminal rows off screendumps until
# 'Tests complete' (tests/rczex.py, an 8x8-glyph OCR in tests/rczex_ocr.py).
# `make rcz80test` runs the SAME shipping core against the same ZEXDOC in raw
# QEMU from a boot sector - and `make rcmemtest` runs the Z80-RAM movers there with SS != DS
# and negative controls (apps/runcpm/hosttest/*.sh). None of the three is in
# `all`: the first two need the fetched master disk.
rcz80test: $(BUILD)/runcpm-src.stamp
	apps/runcpm/hosttest/rcz80test.sh

rcmemtest:
	apps/runcpm/hosttest/rcmemtest.sh

#
# MEASURED (2026-08-17, wave 2, an Apple-silicon host running QEMU's TCG):
# rcz80test 144 s alone (185 s beside another QEMU); rczex 146 s from Alt+L
# to 'Tests complete', 67 of 67 groups OK (179 s before the review's slice
# fixes) - the in-OS run costs about what the raw one does, the wake round
# trip and the terminal being what is left. Re-measured after the second
# review on a host at load ~2.3: rczex 175 / 211 / 193 s over three runs
# with rcz80test at 155 s the same hour and a control build carrying the
# previous adaptation at 178 s - the spread is the host's, not the code's,
# and the figure to quote is the quiet-host one. (The first
# in-OS runs were five times slower, and the reason is in SPEC.md 71: TCG's
# price for a per-branch write into a page that also holds translated code.)
rczex: $(BUILD)/runcpm.img
	python3 tests/rczex.py $(BUILD)/runcpm.img

# =============================================================================
# FROTZ and its story floppy (SPEC.md 61) - ON DEMAND: `make zdisk`
# =============================================================================
# Frotz does NOT ride the shipped apps disks. The 360KB one has about 100KB
# free (tools/os88disk.py --verify says so) and the interpreter alone is most
# of that, never mind a story; and a Z-machine with no story to play is a menu
# item that disappoints. So it gets its own floppy, in all three geometries,
# and `all` does not build any of them - the documented on-demand shape that
# bench, trklog and npbench already use.
#
# NO STORY FILE IS COMMITTED TO THIS REPOSITORY. Every one of them is someone
# else's work under someone else's copyright, so tools/getstories.py fetches
# them into build/stories/ against a manifest of pinned SHA-256s and the disk
# is built from there - the same decision build/big.dat made, for a stronger
# reason. The manifest is limited to what the authors released freely, which
# is why the Infocom titles are Mini-Zork I, both Samplers and Zork: The
# Undiscovered Underground rather than Zork I-III and Planetfall.
#
# Adding your own: STORIES='path/to/ZORK1.DAT path/to/HHGG.DAT' puts them in
# the disk's root beside the folders. They must already be valid 8.3 names -
# os88disk.py has no long-name handling and fails hard rather than truncating.
FROTZSRC := apps/frotz/frotz.asm apps/frotz/zbss.inc apps/frotz/zmem.inc \
            apps/frotz/ztext.inc apps/frotz/zobj.inc apps/frotz/zdict.inc \
            apps/frotz/zwin.inc apps/frotz/zwin6.inc apps/frotz/zpic.inc \
            apps/frotz/zsnd.inc apps/frotz/zio.inc apps/frotz/zexec.inc

$(BUILD)/frotz.bin: $(FROTZSRC) apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/frotz/ -o $@ apps/frotz/frotz.asm
	@echo "frotz:  $(call FILESIZE,$@) bytes"

$(BUILD)/frotz.o88: $(BUILD)/frotz.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/frotz.bin -o $@

# The story cache. A stamp file rather than a directory, because make cannot
# depend on "sixteen files in a directory" and a directory's mtime changes for
# reasons that are not a fetch. getstories.py is idempotent and verifies every
# hash on every run, so re-running it costs 0.4s and no network.
STORYDIR := $(BUILD)/stories
$(BUILD)/stories.stamp: tools/getstories.py | $(BUILD)
	python3 tools/getstories.py -o $(STORYDIR)
	@touch $@

stories: $(BUILD)/stories.stamp

# Which stories fit which floppy. These are chosen against the CLUSTER counts
# os88disk.py reports, not against the byte totals: a 360KB and a 720KB disk
# allocate in 1KB clusters and a 1.44MB one in 512B, so a 52,216-byte story
# takes 51 clusters on the first two and 102 on the third. The whole library
# is 2,713KB and no floppy holds it, so each geometry ships a subset and
# os88disk.py refuses at build time if a list stops fitting - which is the
# check, rather than a comment claiming it fits.
#
#   360KB   what a 256KB XT can also RUN: the v3 stories (SPEC.md 61.4)
#   720KB   the xt-z disk
#   1.44MB  the 386-z disk, plus a second library disk you swap in
ZS_360  := INFOCOM:$(STORYDIR)/MINIZORK.Z3 CLASSIC:$(STORYDIR)/ADVENT.Z3 \
           CLASSIC:$(STORYDIR)/ZORK285.Z5 CLASSIC:$(STORYDIR)/BALANCES.Z5
ZS_720  := INFOCOM:$(STORYDIR)/MINIZORK.Z3 INFOCOM:$(STORYDIR)/ZTUU.Z5 \
           CLASSIC:$(STORYDIR)/ADVENT.Z3 CLASSIC:$(STORYDIR)/ZORK285.Z5 \
           MODERN:$(STORYDIR)/PHOTOPIA.Z5
ZS_1440 := INFOCOM:$(STORYDIR)/MINIZORK.Z3 INFOCOM:$(STORYDIR)/SAMPLER1.Z3 \
           INFOCOM:$(STORYDIR)/SAMPLER2.Z3 INFOCOM:$(STORYDIR)/ZTUU.Z5 \
           CLASSIC:$(STORYDIR)/ADVENT.Z3 CLASSIC:$(STORYDIR)/ADVENT5.Z5 \
           CLASSIC:$(STORYDIR)/ZORK285.Z5 CLASSIC:$(STORYDIR)/BALANCES.Z5 \
           MODERN:$(STORYDIR)/PHOTOPIA.Z5 MODERN:$(STORYDIR)/905.Z5 \
           MODERN:$(STORYDIR)/BEAR.Z5
# Disk 2: the big ones, and it carries NO interpreter on purpose - it is a
# library disk you swap into B: while Frotz is already running, and the 100
# clusters a second copy would cost are 50KB of story.
ZS_DISK2 := MODERN:$(STORYDIR)/BRONZE.Z8 MODERN:$(STORYDIR)/DREAMHLD.Z8 \
            MODERN:$(STORYDIR)/LOSTPIG.Z8 CLASSIC:$(STORYDIR)/CURSES.Z5

STORIES ?=

zdisk: $(BUILD)/zork.img $(BUILD)/zork720.img $(BUILD)/zork360.img \
       $(BUILD)/zork2.img

# The catalogue is CATALOG.TXT on every disk - os88disk.py takes the 8.3 name
# from the file's BASENAME, so the four of them need four directories rather
# than four names. `make -j` safe: each rule creates only its own.
$(BUILD)/zcat/360/CATALOG.TXT: tools/getstories.py
	@mkdir -p $(dir $@)
	python3 tools/getstories.py --catalog $@ MINIZORK.Z3 ADVENT.Z3 ZORK285.Z5 BALANCES.Z5
$(BUILD)/zcat/720/CATALOG.TXT: tools/getstories.py
	@mkdir -p $(dir $@)
	python3 tools/getstories.py --catalog $@ MINIZORK.Z3 ZTUU.Z5 ADVENT.Z3 ZORK285.Z5 PHOTOPIA.Z5
$(BUILD)/zcat/1440/CATALOG.TXT: tools/getstories.py
	@mkdir -p $(dir $@)
	python3 tools/getstories.py --catalog $@ MINIZORK.Z3 SAMPLER1.Z3 SAMPLER2.Z3 ZTUU.Z5 \
		ADVENT.Z3 ADVENT5.Z5 ZORK285.Z5 BALANCES.Z5 PHOTOPIA.Z5 905.Z5 BEAR.Z5
$(BUILD)/zcat/disk2/CATALOG.TXT: tools/getstories.py
	@mkdir -p $(dir $@)
	python3 tools/getstories.py --catalog $@ BRONZE.Z8 DREAMHLD.Z8 LOSTPIG.Z8 CURSES.Z5

$(BUILD)/zork.img: $(BUILD)/frotz.o88 $(BUILD)/stories.stamp $(BUILD)/zcat/1440/CATALOG.TXT \
                   tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/frotz.o88 $(BUILD)/zcat/1440/CATALOG.TXT $(ZS_1440) $(STORIES) \
		--folder SAVES --folder ART

$(BUILD)/zork720.img: $(BUILD)/frotz.o88 $(BUILD)/stories.stamp $(BUILD)/zcat/720/CATALOG.TXT \
                      tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 \
		$(BUILD)/frotz.o88 $(BUILD)/zcat/720/CATALOG.TXT $(ZS_720) $(STORIES) \
		--folder SAVES

$(BUILD)/zork360.img: $(BUILD)/frotz.o88 $(BUILD)/stories.stamp $(BUILD)/zcat/360/CATALOG.TXT \
                      tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		$(BUILD)/frotz.o88 $(BUILD)/zcat/360/CATALOG.TXT $(ZS_360) $(STORIES) \
		--folder SAVES

# BRONZE.PIX - the one picture archive a legally shippable game provides
# (SPEC.md 61.7). Bronze arrives as a Blorb carrying both its Z-code and a
# JPEG cover; tools/getstories.py takes the ZCOD chunk and this takes the
# picture, so the v6 picture path is exercised by a real game rather than only
# by a fixture. The Blorb is getstories' cached artifact, which is why the
# stamp is the prerequisite: it is what guarantees the file is there and
# hash-verified.
$(BUILD)/BRONZE.PIX: $(BUILD)/stories.stamp tools/os88pix.py
	python3 tools/os88pix.py -o $@ --release 3 \
		--blorb $(STORYDIR)/.artifacts/Bronze.zblorb

$(BUILD)/zork2.img: $(BUILD)/stories.stamp $(BUILD)/zcat/disk2/CATALOG.TXT \
                    $(BUILD)/BRONZE.PIX tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/zcat/disk2/CATALOG.TXT $(ZS_DISK2) \
		ART:$(BUILD)/BRONZE.PIX --folder SAVES

# =============================================================================
# MICROSOFT WORD and its document floppy (SPEC.md 68) - ON DEMAND: `make worddisk`
# =============================================================================
# Word follows Frotz's precedent (SPEC.md 68.5) exactly: WORD.O88 does NOT
# ride the shipped apps disks - it gets its own floppy in all three
# geometries, each with an empty DOCS\ folder where the file dialog lands the
# user's documents, and `all` does not build any of them. The xt-word and
# 386-word machines below put this disk in B: instead of the apps disk.
# WELCOME.DOC rides the root of all three: a native .DOC (SPEC.md 68.4)
# generated DETERMINISTICALLY by tools/os88doc.py from apps/word/welcome.wtx
# - a document that exercises the formatting the same engine renders, so the
# disk demonstrates the product the moment it is double-clicked.
# Every include is a prerequisite: the format modules are where the file
# layout lives, and a stale word.bin reads exactly like the layout being wrong.
WORDSRC := apps/word/word.asm apps/word/wddoc.inc apps/word/wdrtf.inc \
           apps/word/wdutil.inc

$(BUILD)/WELCOME.DOC: tools/os88doc.py apps/word/welcome.wtx | $(BUILD)
	python3 tools/os88doc.py apps/word/welcome.wtx -o $@
	@echo "welcome: $(call FILESIZE,$@) bytes"

$(BUILD)/word.bin: $(WORDSRC) apps/os88api.inc apps/os88ui.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -I apps/word/ -o $@ apps/word/word.asm
	@echo "word:   $(call FILESIZE,$@) bytes"

# WORD.OVL is cut off the assembled image before it is packaged (SPEC.md
# 65.10): the module is assembled WITH the package so it can reach every wd_*
# through DS, and only then split out, so what ships in WORD.O88 is the
# resident half alone. The cut point is the image size the package header
# already carries, so the layout does not live in two places.
# ONE recipe makes all three, because they are one operation: a rule whose
# only prerequisite was WORD.OVL and which had NO recipe of its own left make
# free to decide word.o88 was up to date against the PREVIOUS word.trim.bin,
# and it packaged a stale image while the cut silently succeeded. That reads
# exactly like the feature under test being broken - it cost a debugging pass
# on a ruler that was already correct.
$(BUILD)/word.o88: $(BUILD)/word.bin tools/os88ovl.py tools/os88pkg.py
	python3 tools/os88ovl.py $(BUILD)/word.bin -o $(BUILD)/WORD.OVL \
		--trim $(BUILD)/word.trim.bin
	python3 tools/os88pkg.py $(BUILD)/word.trim.bin -o $@

$(BUILD)/WORD.OVL: $(BUILD)/word.o88 ;

worddisk: $(BUILD)/word.img $(BUILD)/word720.img $(BUILD)/word360.img

$(BUILD)/word.img: $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC --folder DOCS

$(BUILD)/word720.img: $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC --folder DOCS

$(BUILD)/word360.img: $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC --folder DOCS

# --- the .DOC format gate (ON DEMAND: `make wordcheck`) ----------------------
# There is no copy of Word here to open the output with, and "it round-trips
# through the app that wrote it" proves only that the app is self-consistent.
# So the format has a SECOND implementation: tools/os88doc.py writes it and
# tools/wordfmt.py reads it, sharing no code, both from the Opus headers
# (SPEC.md 68.4.2). This builds WELCOME.DOC, parses it back with the reader,
# and diffs the result against the markup it was generated from - so a wrong
# FIB offset, a wrong FKP offset scale or a wrong sprm width is a DIFF and
# not a silently prettier document.
#
# What it does NOT establish is that a running Word 1.1a accepts the file.
wordcheck: $(BUILD)/WELCOME.DOC
	@python3 tools/wordfmt.py $(BUILD)/WELCOME.DOC
	@grep -v '^;' apps/word/welcome.wtx | sed 's/^;;/;/' > $(BUILD)/word.src.wtx
	@python3 tools/wordfmt.py $(BUILD)/WELCOME.DOC --wtx > $(BUILD)/word.rt.wtx
	@diff $(BUILD)/word.src.wtx $(BUILD)/word.rt.wtx \
		&& echo "wordcheck: the .DOC round-trips through an independent reader"

# --- the Frotz gate (ON DEMAND: `make ztest`) --------------------------------
# tests/frotz/zopstest.inf is a STORY, not a package, because the thing under
# test is an interpreter: the only way to ask whether @div truncates toward
# zero is to make a Z-machine execute @div. It prints one "PASS name" or "FAIL
# name got <n> want <n>" per check, so the transcript is a RESULT rather than
# prose to eyeball, and the two interpreters are comparable by diff.
#
# The GOLD side is generated, never hand-written: dfrotz (Frotz 2.55) runs the
# same story and its transcript is what os8088's has to match. A check that
# fails on dfrotz is a bug in the .inf, which is exactly how three of them were
# found - Inform folds constant comparisons UNSIGNED, so `(-4 < 3)` compiled to
# a false the reference duly reported.
#
# Needs `inform` and `dfrotz`: `brew install inform6 frotz`. Both are host-side
# only and nothing shipped depends on them.
#
#   make ztest                                  # build stories + gold
#   make test TESTAPPS=build/ztest/ztest.img    # ...and boot it
ZTESTDIR := $(BUILD)/ztest
ZTESTVERS := 3 5 8

ztest: $(ZTESTDIR)/gold3.txt $(ZTESTDIR)/gold5.txt $(ZTESTDIR)/gold8.txt \
       $(ZTESTDIR)/ztest.img

$(ZTESTDIR)/zopstest.z%: tests/frotz/zopstest.inf
	@mkdir -p $(ZTESTDIR)
	inform -v$* $< $@

$(ZTESTDIR)/gold%.txt: $(ZTESTDIR)/zopstest.z%
	dfrotz -w 80 -h 200 -p $< | grep -E '^(PASS|FAIL|TEXT|RESULT)' > $@
	@grep -q '^RESULT pass .* fail 0$$' $@ || \
		{ echo "ztest: the REFERENCE interpreter failed a check - the bug is in tests/frotz/zopstest.inf"; \
		  grep '^FAIL' $@; false; }
	@echo "ztest: v$* gold $$(grep -c . $@) lines, $$(grep -c '^PASS' $@) passing"

$(ZTESTDIR)/ztest.img: $(BUILD)/frotz.o88 $(ZTESTDIR)/zopstest.z3 \
                       $(ZTESTDIR)/zopstest.z5 $(ZTESTDIR)/zopstest.z8 \
                       tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/frotz.o88 $(ZTESTDIR)/zopstest.z3 $(ZTESTDIR)/zopstest.z5 \
		$(ZTESTDIR)/zopstest.z8 --folder SAVES

# --- the Frotz story harness (ON DEMAND: `make zh` / `make zcheck`) ----------
# `make ztest` above asks whether the opcodes are right one opcode at a time,
# against a story written to be a test. This asks the other question - whether
# a REAL story runs - by playing one to a script and diffing the transcript
# against dfrotz. They fail differently and both are needed: zopstest.inf found
# @div's rounding, and the harness found a branch that was decoded correctly,
# executed correctly, and left the program counter in a form the next
# instruction's guard rejected (apps/frotz/zexec.inc, zx_jrel).
#
# FROTZ.O88 HERE IS A DIFFERENT BINARY, built with -DZHARNESS: the story's
# output goes out COM4 a byte at a time and its keystrokes come back the same
# way, so the host plays the story over a socket instead of a person typing at
# a window. Every line of that is inside %ifdef ZHARNESS and none of it is in
# the shipped package - `nasm -f bin` twice, once each way, and the shipped
# build's size is unchanged to the byte.
#
#   make zh                                 # build the harness interpreter
#   python3 tools/zharness.py ADVENT.Z3     # play one story, print the log
#   make zcheck                             # every story x its script, gated
ZHDIR := $(BUILD)/zh

zh: $(ZHDIR)/frotz.o88

$(ZHDIR)/frotz.bin: $(FROTZSRC) apps/frotz/zharness.inc apps/os88api.inc | $(BUILD)
	@mkdir -p $(ZHDIR)
	$(NASM) -f bin -w+error -DZHARNESS -I apps/ -I apps/frotz/ -o $@ apps/frotz/frotz.asm
	@echo "zh:     $(call FILESIZE,$@) bytes (harness build, not shipped)"

$(ZHDIR)/frotz.o88: $(ZHDIR)/frotz.bin tools/os88pkg.py
	python3 tools/os88pkg.py $< -o $@

# The B: disk the harness boots with. tools/zharness.py writes it - the story
# has to arrive as STORY.DAT whatever it is called in build/stories, which is
# a copy make cannot express as a pattern rule over eleven different names.
#
# ZHIMG names it, so `make zhboot ZHIMG=build/zh/advent.img` is the one line
# the tool runs. It is `test` with one more chardev: COM4 at 0x3E8, the one
# port SPEC.md 9.5's mouse probe and SPEC.md 58's monitor both leave alone.
ZHIMG ?= $(ZHDIR)/story.img
ZHDEV = -chardev socket,id=zh,path=$(BUILD)/zh.sock,server=on,wait=off \
        -device isa-serial,chardev=zh,iobase=0x3e8,irq=3

zhboot: $(IMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(ZHIMG),format=raw,if=floppy,index=1 \
		-display none -qmp unix:$(BUILD)/qmp.sock,server,nowait \
		-daemonize -pidfile $(BUILD)/qemu.pid $(ZHDEV)

# The gate. Every story on the 1.44MB library disk, each played to its script
# in tests/frotz/scripts, each diffed against dfrotz. Needs the stories
# (`make stories`, which fetches) and dfrotz on the host.
zcheck: zh $(BUILD)/stories.stamp
	python3 tools/zharness.py --all --compare

# --- the GRAPHICS gate (ON DEMAND: `make zgfx`) ------------------------------
# `make zcheck` above asks what the story PRINTED. This asks what the reader
# can SEE, which is a different question and the one three defects hid behind:
# a quote box drawn into the upper window and thrown away by the next
# @split_window, a picture archive nothing ever loaded, and two routines in
# apps/frotz/zpic.inc that pushed seven registers and popped six because
# nothing had ever called them.
#
# Three checks per story, and only the third needs a reference interpreter:
#
#   model vs pixels  every row the interpreter says holds text is drawn, and
#                    every row it says is blank is not. Read by UNIFORMITY, so
#                    reverse video and @set_colour do not fool it
#   across a repaint the same, on a window that has just been redrawn from the
#                    model - which is what an uncover does, and where anything
#                    on the glass the model does not hold disappears
#   opening screen   against tests/frotz/screens, taken from the real curses
#                    Frotz by tools/zref.py. Every word the reference shows
#                    must be on our screen too
#
# It is slower than zcheck by a screendump per prompt and is a separate target
# for that reason alone; it is not optional in any other sense.
zgfx: zh zpic $(BUILD)/stories.stamp
	python3 tools/zharness.py --all --graphics
	python3 tools/zharness.py $(ZPICDIR)/zpictest.z6 --graphics

# The v6 picture fixture: a story that draws, and three flat blocks to draw.
# Needs `inform` (`brew install inform6`), which is host-side only.
ZPICDIR := $(BUILD)/zpic

zpic: $(ZPICDIR)/zpictest.z6 $(ZPICDIR)/zpictest.PIX

$(ZPICDIR)/zpictest.z6: tests/frotz/zpictest.inf
	@mkdir -p $(ZPICDIR)
	inform -v6 $< $@

$(ZPICDIR)/zpictest.PIX: tools/zpicgen.py tools/os88pix.py
	@mkdir -p $(ZPICDIR)
	python3 tools/zpicgen.py -o $(ZPICDIR)

# The golden opening screens the graphics gate compares against. REGENERATING
# them needs the curses frotz and pyte (`brew install frotz`, `pip3 install
# pyte`); the gate itself needs neither, which is why they are committed.
# Re-take them when the Frotz window's size changes - tools/zref.py writes the
# geometry into each file and zharness.py refuses rather than guessing.
zscreens: $(BUILD)/stories.stamp
	python3 tools/zref.py --all -o tests/frotz/screens

# --- the tracker log disk (ON DEMAND: `make trklog`) -------------------------
# TRKLOG.O88 is apps/tracker built with -DTRKLOG, which is the ONLY difference:
# the shipped TRACKER.O88 has no log, no claims and no D/W keys, and the hooks
# that reach tests/trklog.inc are every one of them inside %ifdef TRKLOG. One
# source, two binaries, and the bench one never touches a shipped disk.
#
#   make trklog                                    # build the disks
#   make test SB16=1 TESTAPPS=build/trklog.img     # ...or build and boot
#
# The disk carries BEVERLY.MOD because a log of a player with nothing to play
# is a log of an idle machine. It must NOT be write-protected: W writes
# TRKLOG.TXT back to it, which is the point (docs/TESTING.md).
TRKLOGSRC := apps/tracker/tracker.asm apps/tracker/trkplay.inc \
             apps/tracker/trkui.inc apps/tracker/trktxt.inc tests/trklog.inc

trklog: $(BUILD)/trklog.img $(BUILD)/trklog360.img

$(BUILD)/trklog.bin: $(TRKLOGSRC) apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -DTRKLOG -I apps/ -I apps/tracker/ -I tests/ \
		-o $@ apps/tracker/tracker.asm
	@echo "trklog: $(call FILESIZE,$@) bytes"

$(BUILD)/trklog.o88: $(BUILD)/trklog.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/trklog.bin -o $@

$(BUILD)/trklog.img: $(BUILD)/trklog.o88 apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/trklog.o88 apps/tracker/beverly.mod

$(BUILD)/trklog360.img: $(BUILD)/trklog.o88 apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		$(BUILD)/trklog.o88 apps/tracker/beverly.mod

# --- the Note Pad walk bench (ON DEMAND: `make npbench`) ---------------------
# NPBENCH.O88 is apps/notepad built with -DNPBENCH, which is the only
# difference: the shipped NOTEPAD.O88 has no bench, no Ctrl-B and no buffer,
# and the hooks that reach tests/npbench.inc are inside %ifdef NPBENCH. One
# source, two binaries - the trklog arrangement above, for its reason.
#
#   make npbench                        # build the disks (BOOTABLE, one each)
#   make test HDD= FLOPPY=build/npbench.img   # ...or boot the 1.44MB one here
#
# It builds FOUR disks: this pair around README.TXT, and the nprun pair around
# a note that is one long run with no newlines in it (SPEC.md 27.4.2), which
# README.TXT cannot show - see the second block below.
#
# WHAT IT ANSWERS: SPEC.md 27.7.3's NP_HCHUNK sizes a gfx-lock hold and had
# never been measured on iron - every figure behind it was a MartyPC cycle
# count, which is the right units and the wrong machine. Boot the disk,
# double-click README.TXT in the root, press Ctrl-B, and the report REPLACES
# the note. The disk must NOT be write-protected if you then want Ctrl-S to
# keep it.
#
# The note the numbers are quoted against is README.TXT, which every system
# disk already carries - so the reference is the shipped file rather than a
# copy here that can drift from it.
NPBENCHSRC := apps/notepad/notepad.asm tests/npbench.inc

npbench: $(BUILD)/npbench.img $(BUILD)/npbench360.img \
         $(BUILD)/nprun.img $(BUILD)/nprun360.img

$(BUILD)/npbench.bin: $(NPBENCHSRC) apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -DNPBENCH -I apps/ -I tests/ \
		-o $@ apps/notepad/notepad.asm
	@echo "npbench: $(call FILESIZE,$@) bytes"

$(BUILD)/npbench.o88: $(BUILD)/npbench.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/npbench.bin -o $@

# It ships as APPS/NOTEPAD.O88 and not as NPBENCH.O88, which is the whole
# ergonomics of the thing: SPEC.md 54's association maps TXT to the stem
# NOTEPAD and hunts for NOTEPAD.O88 in the document's folder, both roots and
# each volume's APPS - so DOUBLE-CLICKING README.TXT in the root opens it in
# the BENCH build with the reference note already loaded. Named NPBENCH.O88
# the association would miss it and the operator would have to launch it and
# drive a file dialog on a machine they are standing next to with a stopwatch.
$(BUILD)/npb/notepad.o88: $(BUILD)/npbench.o88
	@mkdir -p $(BUILD)/npb
	@cp $< $@

# ONE DISK, AND IT BOOTS - `make field`'s rule, which this target got wrong
# first time round and which docs/FIELD-MACHINES.md states outright: the
# calibration machine has ONE floppy drive, so a benchmark on a second disk is
# a swap mid-session, and on that machine a swap is a walk to another room and
# back. So the bench rides the SYSTEM disk: kernel, drivers, TASKMGR, and
# README.TXT in the root beside APPS/NOTEPAD.O88, which is the note the
# numbers are quoted against (15,889 bytes) and is already there because every
# system disk carries it.
#
# Boot it, double-click README.TXT, press Ctrl-B. Nothing else is needed and
# nothing is swapped. It must NOT be write-protected: Ctrl-S is how the report
# leaves the machine.
$(BUILD)/npbench.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin $(DRIVERS) \
                      $(SYSAPPS) $(SYSDOC) $(BUILD)/npb/notepad.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/boot.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(SYSDOC) \
		APPS:$(BUILD)/npb/notepad.o88 $(MEDIAFOLDER)

$(BUILD)/npbench360.img: $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) \
                         $(SYSAPPS) $(SYSDOC) $(BUILD)/npb/notepad.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(SYSDOC) \
		APPS:$(BUILD)/npb/notepad.o88 $(MEDIAFOLDER)

# --- ...and the ONE LONG RUN disk, for SPEC.md 27.4.2 ------------------------
#
# README.TXT is prose and CANNOT show the bug docs/NOTEPAD-NOTES.md 5.6 is
# about: its longest unbroken run is 28 characters, and np_cellrun needs
# np_rcols + 2 - 31 at the default 29 columns - before it will accept. So the
# reference note is the wrong instrument here, and the report sat unmeasured
# for a round because reproducing it meant hand-building a disk.
#
# RUN.TXT is that note: 709 bytes with NO NEWLINES IN IT AT ALL, one run of
# 249 characters, a semicolon, a sentence of prose, then a long tail. All
# three of the interesting carets are on it - inside the first run, just after
# the semicolon (the field's own repro), and at the end of the tail.
#
#   make npbench
#   python3 tools/notepad/lab.py --len 709 boot --image build/nprun360.img
#   ...then Down x8, Right x18, and hold a key
#
# IT CARRIES NO README.TXT, AND THAT IS THE POINT. drive.open_readme clicks a
# FIXED ROW and the root listing is sorted by name (SPEC.md 19.4), so leaving
# the reference note off puts RUN.TXT at the same ordinal README.TXT occupies
# on the disk above - APPS, MEDIA, RUN.TXT, SYSTEM - and one set of
# coordinates drives both disks. Add README.TXT here and RUN.TXT moves down a
# row, which reads as the harness failing to open anything.
#
# The note is GENERATED rather than committed, because it is 709 bytes of 'a'
# and the toolchain is deterministic on purpose: the same command makes the
# same bytes on every machine, so there is nothing for a checked-in copy to
# drift from.
NPRUNLEN := 709
NPRUNTXT := This note is one long run with no newlines in it at all, which is \
what docs/NOTEPAD-NOTES.md 5.6 and SPEC.md 27.4.2 are about. Put the caret \
just after the semicolon above and hold a key down.

$(BUILD)/nprun/run.txt: Makefile | $(BUILD)
	@mkdir -p $(BUILD)/nprun
	@python3 -c "s='a'*249+'; '+'$(NPRUNTXT) '; \
	  assert len(s) <= $(NPRUNLEN), len(s); \
	  open('$@','w',newline='').write(s+'a'*($(NPRUNLEN)-len(s)))"
	@echo "npbench: $@ $(call FILESIZE,$@) bytes, no newlines"

$(BUILD)/nprun.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin $(DRIVERS) \
                    $(SYSAPPS) $(BUILD)/nprun/run.txt $(BUILD)/npb/notepad.o88 \
                    tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/boot.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(BUILD)/nprun/run.txt \
		APPS:$(BUILD)/npb/notepad.o88 $(MEDIAFOLDER)

$(BUILD)/nprun360.img: $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) \
                       $(SYSAPPS) $(BUILD)/nprun/run.txt $(BUILD)/npb/notepad.o88 \
                       tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(BUILD)/nprun/run.txt \
		APPS:$(BUILD)/npb/notepad.o88 $(MEDIAFOLDER)

# --- the A/V SYNC disk (ON DEMAND: `make clicktest`) -------------------------
#
# "The music is not synced to the display" cannot be judged against real
# music - notes are everywhere, so there is nothing to time the display
# against. CLICK.MOD (tests/mkclick.py) plays ONE click, on ONE channel, every
# TWO SECONDS, on rows 00/10/20/30 of a single looping pattern, so the whole
# question becomes one observation with no instruments at all:
#
#     when you HEAR the click, what row does the screen SHOW?
#
# Expected: 00, 10, 20 or 30. Anything else is the offset, read off the screen
# in rows, and a row is exactly 125 ms here (BPM 120, speed 6 - chosen so that
# 16 rows is 2.000 s and the arithmetic needs no calculator).
#
# It carries the TRKLOG build rather than the shipped one, because the two
# extra keys are exactly what a sync question wants: M stamps "I heard it
# here" into the current tick and W writes the log out (SPEC.md 45.14). The
# hooks cost a few compares until D arms them.
#
#   make clicktest                                    # build the disks
#   make test SB16=1 TESTAPPS=build/click.img         # ...or build and boot
#
# Must NOT be write-protected: W writes TRKLOG.TXT back to it.
clicktest: $(BUILD)/click.img $(BUILD)/click360.img

$(BUILD)/click.mod: tests/mkclick.py | $(BUILD)
	python3 tests/mkclick.py $@

# BEVERLY.MOD rides along, and it is not padding. CLICK.MOD is ONE 64-row
# pattern by construction, so the pattern-loop key P has nothing to loop that
# the song does not already play - it looks like a bare restart, and a
# multi-pattern module is the only thing that shows otherwise. It is also what
# a REAL scroll looks like: the pacing modes are being judged by eye, and a
# metronome at 8 rows a second is the easiest possible case.
$(BUILD)/click.img: $(BUILD)/trklog.o88 $(BUILD)/click.mod \
                    apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		$(BUILD)/trklog.o88 $(BUILD)/click.mod apps/tracker/beverly.mod

$(BUILD)/click360.img: $(BUILD)/trklog.o88 $(BUILD)/click.mod \
                       apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		$(BUILD)/trklog.o88 $(BUILD)/click.mod apps/tracker/beverly.mod

# --- the benchmark disk, from tests/ (ON DEMAND: `make bench`) ---------------
#
# These are the only packages in the tree built from OUTSIDE apps/, and the
# folder is the point: tests/ holds testing apps and `all` never builds them,
# which keeps a normal build - and every shipped image - free of them. (Their
# artifacts are untracked, but so is everything else in build/ now; that used
# to be the load-bearing half of this comment.)
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
# GFXBENCH prices the WHOLE DRAWING SURFACE on whichever adapter it boots on
# (SPEC.md 39): every gfx_* and font_* slot, most of them at two sizes so the
# per-call and per-pixel terms come apart, plus the raw RAM and framebuffer
# bandwidth underneath them. One package for Hercules AND CGA on purpose -
# both are the same 1bpp renderer over four different numbers, and two sources
# would be two chances to drift.
#
# SYSBENCH prices the MACHINE: 8086-nominal clocks against a real 8088 per
# instruction class, RAM bandwidth, the clock ladder, what the kernel's own
# interrupts cost per second of work, the API's far-call floor, and the
# floppy. BENCH.DAT and BENCHSML.DAT on the disk are what its file rows read;
# they are generated here rather than tracked, like tests/filetest's big.dat.
#
# Both of the last two write their report to a TEXT FILE on the current volume
# (SPEC.md 18.4), because 90 rows do not fit a 640x200 screen and the results
# are meant to be carried off the machine and pasted into PERFORMANCE.md. That
# means the bench floppy must NOT be write-protected when you use them.
#
# ALL FOUR ride one disk, built in both geometries, because they answer the
# same question at different scales and you want them side by side:
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
BENCHPKGS := $(BUILD)/fontbnch.o88 $(BUILD)/typebnch.o88 \
             $(BUILD)/gfxbench.o88 $(BUILD)/sysbench.o88 \
             $(BUILD)/bandbnch.o88 $(BUILD)/facetest.o88
BENCHDATA := $(BUILD)/bench.dat $(BUILD)/benchsml.dat $(BUILD)/bigfile.dat

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

# The two report-writing harnesses. They share tests/benchlib.inc, which is why
# these two rules carry -I tests/ and the two above do not.
$(BUILD)/gfxbench.bin: tests/gfxbench/gfxbench.asm tests/benchlib.inc apps/os88api.inc tools/benchlint.py | $(BUILD)
	python3 tools/benchlint.py tests/gfxbench/gfxbench.asm
	$(NASM) -f bin -w+error -I apps/ -I tests/ -o $@ tests/gfxbench/gfxbench.asm
	@echo "gfxbench: $(call FILESIZE,$@) bytes"

$(BUILD)/gfxbench.o88: $(BUILD)/gfxbench.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/gfxbench.bin -o $@

# ...and the third: the band blit against the face it replaces (SPEC.md 5.4.2).
# It is the GATE on the proportional-type work, and it prints the 78-cell
# FONT_RUN row it is competing against in the same run, on the same machine, so
# the comparison never rests on a figure quoted from another harness.
$(BUILD)/bandbnch.bin: tests/bandbench/bandbench.asm tests/benchlib.inc apps/os88api.inc tools/benchlint.py | $(BUILD)
	python3 tools/benchlint.py tests/bandbench/bandbench.asm
	$(NASM) -f bin -w+error -I apps/ -I tests/ -o $@ tests/bandbench/bandbench.asm
	@echo "bandbnch: $(call FILESIZE,$@) bytes"

$(BUILD)/bandbnch.o88: $(BUILD)/bandbnch.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/bandbnch.bin -o $@

# ...and the one that shows a FACE rather than timing one: it draws the same
# sentence through the kernel, through face 0, and through both of the
# library's compose loops, so a screendump is the whole assertion (SPEC.md 6.5).
$(BUILD)/facetest.bin: tests/facetest/facetest.asm apps/os88type.inc apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/facetest/facetest.asm
	@echo "facetest: $(call FILESIZE,$@) bytes"

$(BUILD)/facetest.o88: $(BUILD)/facetest.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/facetest.bin -o $@

$(BUILD)/sysbench.bin: tests/sysbench/sysbench.asm tests/benchlib.inc apps/os88api.inc tools/benchlint.py | $(BUILD)
	python3 tools/benchlint.py tests/sysbench/sysbench.asm
	$(NASM) -f bin -w+error -I apps/ -I tests/ -o $@ tests/sysbench/sysbench.asm
	@echo "sysbench: $(call FILESIZE,$@) bytes"

$(BUILD)/sysbench.o88: $(BUILD)/sysbench.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/sysbench.bin -o $@

# sysbench's floppy rows read these. 16KB is 32 sectors - enough that one
# int 13h per sector dominates and the number means something, short enough
# that the two reads together are seconds rather than a minute on a 4.77MHz
# machine. The one-sector file isolates what finding and opening a file costs
# with almost no data behind it.
$(BUILD)/bench.dat: | $(BUILD)
	python3 -c "import sys; sys.stdout.buffer.write(bytes((i>>9)&0xFF for i in range(16*1024)))" > $@

$(BUILD)/benchsml.dat: | $(BUILD)
	python3 -c "import sys; sys.stdout.buffer.write(b'os8088 sysbench small file\r\n' * 18)" > $@

# ...and ONE BIG CONTIGUOUS FILE, for a DOS cross-check AND for sysbench's
# cache-capacity sweep. PERFORMANCE.md Part 9 Set 13's DOS figure came from
# copying the disk's several small files, so it carried a directory write, a
# FAT write and a fresh seek per file and undercounts the read rate it was
# being used to bound. One big file is a single chain and a single open.
#
# 104KB, AND IT USED TO BE 170. The old size was "~80% of what is free on a
# 360KB field disk after everything else", which left 11 clusters for the two
# reports the disk exists to produce - and the reports are the point
# (docs/FIELD-MACHINES.md). The floor is sysbench's sweep, not this file: the
# deepest byte SB_RAH_WMAX = 12 touches on a floppy is 11 x 9216 + 1024 =
# 102,400, so 104KB covers it with slack. Raise SB_RAH_WMAX and this has to
# grow with it; the sweep says so in the report either way rather than
# reporting a cliff that is the FILE's.
$(BUILD)/bigfile.dat: | $(BUILD)
	python3 -c "import sys; sys.stdout.buffer.write(bytes((i>>9)&0xFF for i in range(104*1024)))" > $@

# ...and RUNCPM's row composer on the same harness (SPEC.md 71.2): the package's
# own apps/runcpm/rcband.inc timed against the 79-cell FONT_RUN it replaces,
# with the first version of the loop kept in the harness for the record. Its
# own disk, on demand, because it exists to answer one question once:
#   make rcbandbench
#   make test TESTAPPS=build/rcband.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
rcbandbench: $(BUILD)/rcband.img

$(BUILD)/rcbband.bin: tests/rcband/rcbandbench.asm apps/runcpm/rcband.inc tests/benchlib.inc apps/os88api.inc tools/benchlint.py | $(BUILD)
	python3 tools/benchlint.py tests/rcband/rcbandbench.asm
	$(NASM) -f bin -w+error -I apps/ -I tests/ -o $@ tests/rcband/rcbandbench.asm
	@echo "rcbband: $(call FILESIZE,$@) bytes"

$(BUILD)/rcbband.o88: $(BUILD)/rcbband.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/rcbband.bin -o $@

$(BUILD)/rcband.img: $(BUILD)/rcbband.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/rcbband.o88

$(BUILD)/bench.img: $(BENCHPKGS) $(BENCHDATA) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BENCHPKGS) $(BENCHDATA)

$(BUILD)/bench360.img: $(BENCHPKGS) $(BENCHDATA) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BENCHPKGS) $(BENCHDATA)

# --- the FIELD disks: one BOOTABLE 360KB floppy per adapter ------------------
#
# `make field` -> the NARROW disks, for the questions `make combo` cannot
# answer. It is no longer the default ask (that is combo.img, below), and
# herc.img/cga.img in particular are now the special case rather than the
# ordinary one: SPEC.md 39.11's Display page switches the adapter at RUN TIME,
# so a pinned-adapter build is only wanted when a run must fix the card at
# BOOT, or must compare against an older set that was taken that way. What is
# still only here: cga720 (a 720KB GEOMETRY, for the Toshiba T1100 Plus),
# flop1 (FLOPPY1=1) and cqdiag (BOOTDIAG=1).
#
# All of them are shaped by the machine this project is calibrated against
# (docs/FIELD-MACHINES.md, E1: an IBM PC 5150 with ONE floppy drive - the
# second bay is an ST-225 - and both a Hercules and a CGA card in it at all
# times).
#
# THE BENCHMARKS ARE ON THE BOOT DISK. With no drive B, the two-floppy shape
# `make bench` produces would mean swapping disks mid-session on the one
# machine where a disk swap is a walk to another room. These carry the
# benchmarks in the root of the SYSTEM disk instead - the TASKMGR.O88
# precedent (SPEC.md 28.3), for exactly the same reason - so booting one puts
# them one double-click away, and the reports they save land back on the disk
# they came from. os88disk marks them visible + read-only (SPEC.md 19.6), so
# they list and cannot be deleted by accident, and the disk is NOT
# write-protected because the reports are the point.
#
# ONE IMAGE PER CARD, because the probe (SPEC.md 39.1) finds the Hercules
# first and a machine that holds both can only be asked one question at a
# time. herc.img is the ordinary SHIPPED kernel - so it exercises the probe on
# the way past - and cga.img is a VIDEO=cga kernel that ignores the Hercules.
# That kernel is built in a directory of its own: a VIDEO=-forced kernel that
# reaches build/ is a machine that boots the wrong card for everyone, and that
# is a mistake that has been made.
#
# The names are short and unambiguous at a DOS prompt on purpose: DOS 3.3 has
# 8.3 names and no tab completion, and these get typed by hand into dskimage.
# NOBIG=1 leaves BIGFILE.DAT off the field disks. It is 170KB of the 354 a
# 360KB floppy holds, so a full disk has ~14KB free and SPEC.md 18.4's write
# rows step all the way down and skip - and a WRITE bench with no room to
# write in is the one thing it cannot be. Without it there is ~185KB free and
# the row gets its full 128KB.
#
# The trade is stated rather than hidden: BIGFILE.DAT is what PERFORMANCE.md
# Part 9 Set 13's DOS cross-check reads, and what SPEC.md 18.95.4's
# cache-width sweep walks, so a NOBIG disk skips that row and says so. Build
# one of each if you want both, and they have the same NAMES - so build,
# copy, rebuild.
ifneq ($(NOBIG),)
FIELDBENCH := $(BENCHPKGS) $(BUILD)/bench.dat $(BUILD)/benchsml.dat
else
FIELDBENCH := $(BENCHPKGS) $(BENCHDATA)   # bigfile.dat is in BENCHDATA now
endif
CGADIR     := $(BUILD)/cgak
F1DIR      := $(BUILD)/f1k
HERCDIR    := $(BUILD)/herck
CQDIR      := $(BUILD)/cqk

# EVERY field kernel is built DISKCNT=1, and there is no separate instrumented
# disk any more. Both halves of the reason there used to be one have expired:
#
#   "the counters are two instructions in the hot path of every transfer" -
#   measured, they are about twelve instructions per int 13h CALL (not per
#   sector) against a 238 ms sector, and the image is BYTE FOR BYTE the same
#   size either way because the growth lands inside the padding to OVL_START.
#
#   "the published word is an ABI that depends on a knob" - it was, when it
#   was a fixed word at 0060:000E. SPEC.md 57's registry is exactly the fix
#   for that: the block is found by TAG, and a reader that cannot find one
#   says so and continues. One build of sysbench already serves both kernels.
#
# The second is the one worth noticing: a later change removed the reason and
# nobody went back to re-ask the question. What it buys is a DISK SWAP - the
# 5150 has one drive (docs/FIELD-MACHINES.md), so a second disk is a swap and
# a reboot in the middle of every batch.
FIELDKNOBS := DISKCNT=1

# --- the SMALL build (docs/KERN-SPLIT-PLAN.md) -------------------------------
#
# `make small` builds kern_small and its system disks. THE DEFAULT IS BIG, so
# this is the one that is asked for - and it builds into a directory of its
# own, build/smallk/, for the reason the field kernels do: a knob-built kernel
# that reaches build/ is a kernel somebody boots by accident believing it is
# the shipped one, and that mistake has been made in this tree before (see the
# cgak note above). Nothing under build/smallk/ is what `all` ships.
#
# The APPS disks are NOT rebuilt and must not be: a package is the same bytes
# on both kernels by construction, because the two builds hold the SAME API
# table at the same offsets (docs/KERN-SPLIT-PLAN.md 3). The day that stops
# being true is the day the split acquired an ABI, which is the one thing this
# design is written to avoid.
SMALLDIR := $(BUILD)/smallk

# ...and its drivers are $(DRIVERS) LESS THE STORE ABOVE 1MB. XMEM.DRV is dead
# weight on this kernel and only on this one: xmem.inc is entirely inside
# %ifdef KERN_BIG and so is drv_load_at, its only loader, and it has no drv_tab
# row that a Drivers page could tick - so nothing on a kern_small disk can name
# it, read it or load it. Same reason DEBUG.DRV is not in $(DRIVERS) at all.
# RECURSIVE, like $(DRIVERS) itself: $(KMODS) inside it still reads the
# per-target KMODDIR.
SMALLDRIVERS = $(filter-out $(BUILD)/xmem.drv,$(DRIVERS))

small: $(BUILD)/small360.img $(BUILD)/small.img
	@python3 tools/kernsplit.py $(SMALLDIR)/kernel.bin $(BUILD)/kernel.bin

# its kernel is $(SMALLDIR)'s, so its modules are too
$(BUILD)/small360.img: KMODDIR := $(SMALLDIR)

$(BUILD)/small360.img: $(SMALLDRIVERS) $(SYSAPPS) $(SYSDOC) tools/os88disk.py
	@$(MAKE) BUILD=$(SMALLDIR) KERN_SMALL=1 $(SMALLDIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(SMALLDIR)/boot360.bin --kernel $(SMALLDIR)/kernel.bin \
		$(SMALLDRIVERS) $(SYSAPPSARGS) $(SYSDOC) $(MEDIAFOLDER)
	@echo "small: $@ - kern_small on 360KB. Its apps disk is the ordinary"
	@echo "       build/apps360.img: one package, both kernels"

# its kernel is $(SMALLDIR)'s, so its modules are too
$(BUILD)/small.img: KMODDIR := $(SMALLDIR)

$(BUILD)/small.img: $(SMALLDRIVERS) $(SYSAPPS) $(SYSDOC) tools/os88disk.py
	@$(MAKE) BUILD=$(SMALLDIR) KERN_SMALL=1 $(SMALLDIR)/boot.bin
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(SMALLDIR)/boot.bin --kernel $(SMALLDIR)/kernel.bin \
		$(SMALLDRIVERS) $(SYSAPPSARGS) $(SYSDOC) $(MEDIAFOLDER)

# ...and the size comparison on its own, for when you want the numbers without
# building two floppies for them. SMALL FIRST in the argument order, because it
# is the figure being defended (tools/kernsplit.py).
kernsplit:
	@$(MAKE) $(BUILD)/kernel.bin
	@$(MAKE) BUILD=$(SMALLDIR) KERN_SMALL=1 $(SMALLDIR)/kernel.bin
	@python3 tools/kernsplit.py $(SMALLDIR)/kernel.bin $(BUILD)/kernel.bin

# --- a build target per TYPEFACE (SPEC.md 6.2) -------------------------------
#
# `make font-tallx` is a pair of system disks in that face; `make fonts` is
# all of them; `make fontsheet-<name>` is the proof sheet, on VGA pixels and
# on the CGA's 2.4:1 ones. None of it is in `all` and none of it changes a
# shipped byte - THE DEFAULT IS STILL THE MACHINE'S OWN ROM FONT, because
# these rules pass FONT= to a sub-make and never to this one.
#
# The rules are GENERATED from $(FONTS), which is the fonts/ directory read at
# parse time, so adding a face is adding a file: `make fontlist` will list it,
# `make fonts` will build it, and nothing here has to be edited. That is the
# whole point - the knob could always name any face, but only one existed and
# only one thing could be built with it.
#
# Each face's kernel goes in a directory of ITS OWN, build/fontk-<name>/, for
# the reason `small` and the field kernels do (the cgak note above): a kernel
# built with a knob that reaches build/ is one somebody boots by accident
# believing it is the shipped one, and that mistake has been made here. The
# finished disks are named for the face and sit in build/ like the field
# disks, because a disk says which face it carries in its own file name.
#
# The APPS disks are NOT rebuilt and must not be, which is `small`'s argument
# exactly: a package reaches text through OSAPI_FONT_* and carries no glyphs
# of its own, so build/apps360.img pairs with every one of these.
FONTDIR = $(BUILD)/fontk-$(1)

define FONT_TARGETS
# its kernel is that face's
$$(BUILD)/font-$(1)-360.img: KMODDIR := $$(call FONTDIR,$(1))

$$(BUILD)/font-$(1)-360.img: fonts/$(1).f8 $$(DRIVERS) $$(SYSAPPS) $$(SYSDOC) \
                             tools/os88font.py tools/os88disk.py
	@$$(MAKE) BUILD=$$(call FONTDIR,$(1)) FONT=$(1) $$(call FONTDIR,$(1))/boot360.bin
	python3 tools/os88disk.py -o $$@ --size 360 \
		--boot $$(call FONTDIR,$(1))/boot360.bin \
		--kernel $$(call FONTDIR,$(1))/kernel.bin \
		$$(DRIVERS) $$(SYSAPPSARGS) $$(SYSDOC) $$(MEDIAFOLDER)
	@echo "font: $$@ - a 360KB system disk set in $(1)"

# its kernel is that face's
$$(BUILD)/font-$(1).img: KMODDIR := $$(call FONTDIR,$(1))

$$(BUILD)/font-$(1).img: fonts/$(1).f8 $$(DRIVERS) $$(SYSAPPS) $$(SYSDOC) \
                         tools/os88font.py tools/os88disk.py
	@$$(MAKE) BUILD=$$(call FONTDIR,$(1)) FONT=$(1) $$(call FONTDIR,$(1))/boot.bin
	python3 tools/os88disk.py -o $$@ --size 1440 \
		--boot $$(call FONTDIR,$(1))/boot.bin \
		--kernel $$(call FONTDIR,$(1))/kernel.bin \
		$$(DRIVERS) $$(SYSAPPSARGS) $$(SYSDOC) $$(MEDIAFOLDER)
	@echo "font: $$@ - the same disk on 1.44MB, for \`make run\`"

$$(BUILD)/fontsheet-$(1).png: fonts/$(1).f8 tools/os88font.py | $$(BUILD)
	python3 tools/os88font.py $$< --preview $$@ --zoom 3
$$(BUILD)/fontsheet-$(1)-cga.png: fonts/$(1).f8 tools/os88font.py | $$(BUILD)
	python3 tools/os88font.py $$< --preview $$@ --zoom 3 --cga

font-$(1): $$(BUILD)/font-$(1)-360.img $$(BUILD)/font-$(1).img
fontsheet-$(1): $$(BUILD)/fontsheet-$(1).png $$(BUILD)/fontsheet-$(1)-cga.png
.PHONY: font-$(1) fontsheet-$(1)
endef
$(foreach f,$(FONTS),$(eval $(call FONT_TARGETS,$(f))))

fonts:      $(addprefix font-,$(FONTS))
fontsheets: $(addprefix fontsheet-,$(FONTS))

# What is there to ask for, and what each one is - read out of the face's own
# first comment line, so a new .f8 describes itself here and cannot go stale.
fontlist:
	@echo "typefaces in fonts/ (SPEC.md 6.2):"
	@for f in $(FONTS); do \
		printf '  %-10s %s\n' "$$f" \
		  "$$(sed -n '1s/^# *//p' fonts/$$f.f8)"; \
	done
	@echo
	@echo "  make FONT=<name>       bake one into build/ (a KNOB build)"
	@echo "  make font-<name>       ...or into disks of its own, safely"
	@echo "  make fontsheet-<name>  proof sheet, VGA and CGA aspect"
	@echo "  make fonts             every face above"
	@echo
	@echo "  the DEFAULT is no FONT= at all: the machine's own ROM 8x8 set."

field: $(BUILD)/herc.img $(BUILD)/cga.img $(BUILD)/cga720.img $(BUILD)/flop1.img \
       $(BUILD)/cqdiag.img


# EVERY field disk rebuilds the DRIVERS under $(FIELDKNOBS) too, and that line
# is not decoration. $(DRIVERS) comes out of $(BUILD), built with whatever
# knobs the TOP-LEVEL invocation had - so `make field` used to pair a
# DISKCNT=1 kernel with whatever HDD.DRV happened to be lying there. That was
# harmless while no driver read a knob, and stopped being harmless the moment
# SPEC.md 52.10.9 put the installer's instrument behind DISKCNT: the disk
# would boot a counted kernel whose installer had no phase table, and nothing
# would say so. The rebuild is seconds and the stamp puts build/ back to the
# shipped bytes on the next knobless make.
#
# THE MODULES ARE FILTERED OUT OF IT, and they are the one entry that has to
# be. $(DRIVERS) expands here with the rule's own KMODDIR, so it names the
# FIELD kernel's modules - and this sub-make builds into the default $(BUILD),
# where no rule can make them. They are not skipped: the sub-make on the next
# line of each rule builds <dir>/kernel.bin, whose own recipe cuts <dir>'s
# ctrl.drv and format.drv out of it (SPEC.md 2.8).
FIELDDRV = @$(MAKE) $(FIELDKNOBS) $(filter-out $(KMODS),$(DRIVERS))

# its kernel is $(HERCDIR)'s, so its modules are too
$(BUILD)/herc.img: KMODDIR := $(HERCDIR)

$(BUILD)/herc.img: $(BUILD)/kernel.bin $(DRIVERS) \
                   $(SYSAPPS) $(FIELDBENCH) tools/os88disk.py
	$(FIELDDRV)
	@$(MAKE) BUILD=$(HERCDIR) $(FIELDKNOBS) $(HERCDIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(HERCDIR)/boot360.bin --kernel $(HERCDIR)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(FIELDBENCH)
	@python3 tools/fieldsize.py $(BUILD)/kernel.bin $(HERCDIR)/kernel.bin
	@echo "field: $@ - the PROBE kernel; on a machine holding both cards it"
	@echo "       finds the Hercules (SPEC.md 39.1)"

# its kernel is $(CGADIR)'s, so its modules are too
$(BUILD)/cga.img: KMODDIR := $(CGADIR)

$(BUILD)/cga.img: $(DRIVERS) $(SYSAPPS) $(FIELDBENCH) tools/os88disk.py
	$(FIELDDRV)
	@$(MAKE) BUILD=$(CGADIR) VIDEO=cga $(FIELDKNOBS) $(CGADIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(CGADIR)/boot360.bin --kernel $(CGADIR)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(FIELDBENCH)
	@echo "field: $@ - VIDEO=cga, so the Hercules is ignored and the CGA"
	@echo "       column can be taken without opening the machine"

# ...and the same disk on 720KB 3.5" DD, for a machine that cannot take a
# 360KB disk. Same kernel, same benchmarks, same everything: what changes is
# 80 cylinders instead of 40 and the FAT12 layout that follows from it (2
# sectors a cluster, 112 root entries), which os88disk.py owns. The boot
# sector is boot360.bin for both, because it is 9 spt and 2 heads on either
# and it never counts cylinders - the note above $(IMG720) is the long version.
#
# CGA only, because that is what was asked for. The Hercules twin is this
# rule with $(BUILD)/boot360.bin and $(BUILD)/kernel.bin - the probe build -
# in place of $(CGADIR)'s, and nothing else.
# its kernel is $(CGADIR)'s, so its modules are too
$(BUILD)/cga720.img: KMODDIR := $(CGADIR)

$(BUILD)/cga720.img: $(DRIVERS) $(SYSAPPS) $(FIELDBENCH) tools/os88disk.py
	$(FIELDDRV)
	@$(MAKE) BUILD=$(CGADIR) VIDEO=cga $(FIELDKNOBS) $(CGADIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 720 \
		--boot $(CGADIR)/boot360.bin --kernel $(CGADIR)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(FIELDBENCH)
	@echo "field: $@ - the CGA disk on 720KB 3.5\" DD media"

# ...and the A/B disk. FLOPPY1=1 puts dsk_xfer back to one sector per int 13h
# (SPEC.md 18.91) and the boot sector with it, which is the transfer this
# project shipped before the batching. It exists because on the IBM 5150 the
# batching measured ZERO improvement - 16KB in 7.63 s before it and 8.07 s
# after, 2,100 bytes/second and then 2,001 (docs/FIELD-NOTES.md 7) - while
# both emulators showed a large gain and neither of them models rotational
# latency, so neither can arbitrate. This disk settles it in one sysbench run:
#
#   the same 8.07 s   the multi-sector command is not reaching the hardware
#   much slower       the batching works, and Set 1's 9x model was wrong
#
# It is the PROBE kernel (so it boots either card) because the question has
# nothing to do with video, and its `boot ticks` row is a second, independent
# reading of the same thing.
# its kernel is $(F1DIR)'s, so its modules are too
$(BUILD)/flop1.img: KMODDIR := $(F1DIR)

$(BUILD)/flop1.img: $(DRIVERS) $(SYSAPPS) $(FIELDBENCH) tools/os88disk.py
	$(FIELDDRV)
	@$(MAKE) BUILD=$(F1DIR) FLOPPY1=1 $(FIELDKNOBS) $(F1DIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(F1DIR)/boot360.bin --kernel $(F1DIR)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(FIELDBENCH)
	@echo "field: $@ - FLOPPY1=1, one sector per int 13h. The A/B against"
	@echo "       herc.img for docs/FIELD-NOTES.md 7 - run SYSBENCH on both"

# There is no INSTRUMENTED disk any more: DISKCNT=1 is in $(FIELDKNOBS) and so
# in all five images above. SPEC.md 18.94's counters are therefore in whatever
# disk the operator happens to have in the drive, which is the point - the
# question they answer ("what did dsk_xfer actually issue?") is one you want
# to have asked about the run you already did, not the run you have to go back
# and do again on a different floppy.
#
# ...and the DIAGNOSTIC disk, for a machine that will not boot. BOOTDIAG=1
# trades the boot sector's 'os8088: disk error' for int 13h's STATUS as two
# hex digits, which is the whole diagnosis in one boot instead of a bisect:
# 0C is a media type the drive could not identify (a 360KB disk in a 1.2MB
# drive), 04 a sector the FDC never found (EOT / the multi-track flip), 09 a
# transfer that crossed a 64KB DMA page, 80 a drive that never answered.
# The sector has four spare bytes, which is why this is a knob.
# its kernel is $(CQDIR)'s, so its modules are too
$(BUILD)/cqdiag.img: KMODDIR := $(CQDIR)

$(BUILD)/cqdiag.img: $(DRIVERS) $(SYSAPPS) $(FIELDBENCH) tools/os88disk.py
	$(FIELDDRV)
	@$(MAKE) BUILD=$(CQDIR) BOOTDIAG=1 $(FIELDKNOBS) $(CQDIR)/boot360.bin
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(CQDIR)/boot360.bin --kernel $(CQDIR)/kernel.bin \
		$(DRIVERS) $(SYSAPPSARGS) $(FIELDBENCH)
	@echo "field: $@ - BOOTDIAG=1. A boot that fails prints int 13h's status"

# STACKPROBE measures the 256-byte task-stack margin (SPEC.md 8) from the
# inside: its worker 0xCC-fills its own slice, spins so every interrupt the
# machine takes lands there, and reports the high-water mark live. The QEMU
# probe understates a real BIOS (SeaBIOS keeps its interrupt entries on an
# internal stack; a real int 09h + the tick + the mouse nest on the task
# slice), so the 360KB image is the one that matters: boot os8088-360.img on
# the real machine, stkprobe360.img in the other drive, hold keys down and
# read the number. docs/TESTING.md has the recipe.
stackprobe: $(BUILD)/stkprobe.img $(BUILD)/stkprobe360.img

$(BUILD)/stkprobe.bin: tests/stackprobe/stackprobe.asm apps/os88api.inc | $(BUILD)
	$(NASM) -f bin -w+error -I apps/ -o $@ tests/stackprobe/stackprobe.asm
	@echo "stkprobe: $(call FILESIZE,$@) bytes"

$(BUILD)/stkprobe.o88: $(BUILD)/stkprobe.bin tools/os88pkg.py
	python3 tools/os88pkg.py $(BUILD)/stkprobe.bin -o $@

$(BUILD)/stkprobe.img: $(BUILD)/stkprobe.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(BUILD)/stkprobe.o88

$(BUILD)/stkprobe360.img: $(BUILD)/stkprobe.o88 tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(BUILD)/stkprobe.o88

# COMSCAN surveys the machine's serial ports (tests/comscan) - the field
# diagnostic for "the mouse was not detected on real hardware" (SPEC.md 9.5).
# It is NOT an os8088 package and deliberately so: the thing being diagnosed is
# the mouse, so anything that has to be reached by clicking is unreachable on
# exactly the machine that needs it. Two builds from one source:
#
#   build/comscan.com   a DOS program. `COMSCAN > COMSCAN.TXT` captures the
#                       whole report to a file, because its output goes
#                       through int 21h rather than the BIOS
#   build/comscan.img   a BOOTABLE floppy carrying the same code as its
#                       "kernel" - the shipped boot sector loads anything at
#                       KERNEL_SEG:0 that honours its three-point handoff, so
#                       this needs no DOS, no os8088 and no mouse. COMSCAN.COM
#                       rides along on the same disk for the DOS route
#
# Both geometries are built because a period portable's drive is not knowable
# from here: comscan.img is 360KB (readable in a 360K, 720K or 1.2M drive) and
# comscan144.img is 1.44MB (and is what QEMU boots easily).
comscan: $(BUILD)/comscan.img $(BUILD)/comscan144.img $(BUILD)/comscan.com
	@echo "comscan: build/comscan.img (360K, bootable), comscan144.img (1.44M),"
	@echo "         and build/comscan.com to run under DOS"

$(BUILD)/comscan.com: tests/comscan/comscan.asm | $(BUILD)
	$(NASM) -f bin -w+error -DCOMFILE -o $@ tests/comscan/comscan.asm
	@echo "comscan.com: $(call FILESIZE,$@) bytes"

$(BUILD)/comscan.bin: tests/comscan/comscan.asm | $(BUILD)
	$(NASM) -f bin -w+error -o $@ tests/comscan/comscan.asm

# Its own boot sectors, because the count of sectors to read is assembled in
# and comscan is a great deal smaller than the kernel.
$(BUILD)/csboot360.bin: boot/boot.asm $(BUILD)/comscan.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/comscan.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm

$(BUILD)/csboot144.bin: boot/boot.asm $(BUILD)/comscan.bin | $(BUILD)
	$(NASM) -f bin $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/comscan.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm

$(BUILD)/comscan.img: $(BUILD)/csboot360.bin $(BUILD)/comscan.bin \
                      $(BUILD)/comscan.com tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/csboot360.bin --kernel $(BUILD)/comscan.bin \
		$(BUILD)/comscan.com

$(BUILD)/comscan144.img: $(BUILD)/csboot144.bin $(BUILD)/comscan.bin \
                         $(BUILD)/comscan.com tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/csboot144.bin --kernel $(BUILD)/comscan.bin \
		$(BUILD)/comscan.com

# LPTLINK surveys the machine's PARALLEL ports and then measures the cable
# between two of them (tests/lptlink) - step 1 of docs/NET-PLAN.md. Same shape
# as comscan above and for the same reason: NEITHER END IS os8088, so a
# failure is a failure of the cable or the protocol and cannot be anything
# else. Run it on both machines - one Slave, one Master, SLAVE FIRST.
#
#   build/lptlink.com   a DOS program. `LPTLINK > LPTLINK.TXT` captures the
#                       report, because its output goes through int 21h
#   build/lptlink.img   a BOOTABLE 360KB floppy carrying the same code as its
#                       "kernel", so the 5150 needs no DOS and no os8088 to be
#                       one end of the link. LPTLINK.COM rides along for the
#                       DOS route
#
# `python3 tests/lptlink/linksim.py` is the host-side model of its link layer,
# and it is not optional reading before touching the handshake: three defects
# in it were found there rather than in the field, and every one of them would
# have presented as a cable fault.
lptlink: $(BUILD)/lptlink.img $(BUILD)/lptlink144.img $(BUILD)/lptlink.com $(BUILD)/os88net.com
	@python3 tests/lptlink/linksim.py
	@echo "lptlink: build/lptlink.img (360K, bootable), lptlink144.img (1.44M),"
	@echo "         and build/lptlink.com to run under DOS"

$(BUILD)/lptlink.com: tests/lptlink/lptlink.asm drivers/net/lplink.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/net/ -DCOMFILE -o $@ tests/lptlink/lptlink.asm
	@echo "lptlink.com: $(call FILESIZE,$@) bytes"

$(BUILD)/lptlink.bin: tests/lptlink/lptlink.asm drivers/net/lplink.inc | $(BUILD)
	$(NASM) -f bin -w+error -I drivers/net/ -o $@ tests/lptlink/lptlink.asm

# Its own boot sectors, because the sector count is assembled in and lptlink
# is a great deal smaller than the kernel.
$(BUILD)/llboot360.bin: boot/boot.asm $(BUILD)/lptlink.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/lptlink.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm

$(BUILD)/llboot144.bin: boot/boot.asm $(BUILD)/lptlink.bin | $(BUILD)
	$(NASM) -f bin $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/lptlink.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm

# THE DOS-LITE HARNESS (tests/dosstub): a bootable floppy that runs
# OS88NET.COM on a machine with no DOS on it.
#
# It exists because the DOS end was written, assembled, packaged and SENT TO
# THE FIELD TWICE without one instruction of it ever executing - there is no
# DOS in this container and none this tree may ship, so `make` could say the
# program was fine while DOS entered it at a byte that was not its entry.
#
#   make dosstub                     10MB image: 20,480 sectors
#   make dosstub FSIZE=64M           past os8088's cap: 65,535, and it says so
#   make dosstub FSIZE=256           under one sector: the refusal
#   make dosstub FAILOPEN=1          DOS says no: the error path
#   make dosstub ARGS='/RO /P:378'   the command tail
#
# ON DEMAND ONLY. `all` never builds tests/ and nothing under it ships.
DOSSTUB_DEF :=
ifeq ($(FSIZE),64M)
DOSSTUB_DEF += -DFSIZE_HI=0x0400 -DFSIZE_LO=0x0000
endif
ifeq ($(FSIZE),256)
DOSSTUB_DEF += -DFSIZE_HI=0x0000 -DFSIZE_LO=0x0100
endif
ifneq ($(ARGS),)
DOSSTUB_DEF += -DARGS='"$(ARGS)"'
endif
ifneq ($(FAILOPEN),)
DOSSTUB_DEF += -DFAILOPEN=1
endif
# COMFILE=<path> embeds a DIFFERENT OS88NET.COM, which is how a fix to the DOS
# side gets an A/B: build the previous commit's .com to a scratch path and run
# the same test against it. It was named in DSSTAMP below and NEVER PASSED TO
# NASM - so the knob rebuilt the stub faithfully and rebuilt it around the
# default file, and an A/B ran the same binary twice and reported both legs
# passing. That is the very trap DSSTAMP's own comment describes, one knob
# later: a stamp makes the rebuild happen and says nothing about what the
# rebuild is made of.
ifneq ($(COMFILE),)
DOSSTUB_DEF += -DCOMFILE='"$(COMFILE)"'
endif

# ...AND A STAMP FILE, for exactly VIDSTAMP's reason (see its comment above).
# None of these four knobs is a prerequisite of anything, so `make dosstub
# ARGS='/P:378 /W'` after a plain `make dosstub` saw an up-to-date .bin and
# rebuilt NOTHING - the program then ran with the PREVIOUS run's command tail,
# which reads exactly like a switch that does not work. Measured: /W was
# parsed correctly and never reached the binary at all.
DSSTAMP := $(BUILD)/.dosstub-$(if $(FSIZE),$(FSIZE),def)$(if $(ARGS),-a$(shell echo '$(ARGS)' | tr -c 'A-Za-z0-9' '_'))$(if $(FAILOPEN),-fo$(FAILOPEN))$(if $(COMFILE),-c$(notdir $(COMFILE)))

$(BUILD)/dosstub.bin: tests/dosstub/dosstub.asm $(BUILD)/os88net.com | $(BUILD)
	@[ -f $(DSSTAMP) ] || { rm -f $(BUILD)/.dosstub-*; touch $(DSSTAMP); }
	$(NASM) -f bin -w+error $(DOSSTUB_DEF) -o $@ tests/dosstub/dosstub.asm

$(BUILD)/dosstub.bin: $(DSSTAMP)
$(DSSTAMP): | $(BUILD)
	@rm -f $(BUILD)/.dosstub-*
	@touch $@

$(BUILD)/dsboot.bin: boot/boot.asm $(BUILD)/dosstub.bin | $(BUILD)
	$(NASM) -f bin -DSPT=9 -DHEADS=2 $(BOOTDEF) \
		-DKERNEL_SECTORS=$$(( ( $(call FILESIZE,$(BUILD)/dosstub.bin) + 511 ) / 512 )) \
		-o $@ boot/boot.asm

$(BUILD)/dosstub.img: $(BUILD)/dsboot.bin $(BUILD)/dosstub.bin tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/dsboot.bin --kernel $(BUILD)/dosstub.bin

.PHONY: dosstub
dosstub: $(BUILD)/dosstub.img
	@echo "dosstub: $(BUILD)/dosstub.img - boots and runs OS88NET.COM with no DOS"
	@echo "  cd $(BUILD)/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 \\"
	@echo "    ./martypc_headless --machine-config-name os8088_5150_cga_lpt \\"
	@echo "    --mount fd:0:media/floppies/dosstub.img &"
	@echo "  python3 tools/os88marty.py 127.0.0.1:9001 screen"

$(BUILD)/lptlink.img: $(BUILD)/llboot360.bin $(BUILD)/lptlink.bin \
                      $(BUILD)/lptlink.com tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/llboot360.bin --kernel $(BUILD)/lptlink.bin \
		$(BUILD)/lptlink.com

$(BUILD)/lptlink144.img: $(BUILD)/llboot144.bin $(BUILD)/lptlink.bin \
                         $(BUILD)/lptlink.com tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/llboot144.bin --kernel $(BUILD)/lptlink.bin \
		$(BUILD)/lptlink.com

# There WAS a third image here - the same package on a FAT16 volume, built on
# the 2.88MB test geometry, which exercised the one part of the write path
# FAT12 cannot. It went with DSK_FAT_SECS: at 10 sectors the mount's rule 10
# rejects every FAT16 volume there can be (a FAT is only FAT16 with >= 4,085
# clusters, i.e. >= 16 FAT sectors), so the image would build and refuse to
# mount. dsk_next_clus / dskw_setfat keep their FAT16 halves, unreachable.

# The software floppies (drive B:) hold packages, not boot code - os88fs only.
# The volume is FOLDERED (SPEC.md 19.2): the root holds APPS and GAMES, so a
# package is two double-clicks away rather than one. The one root-level file
# is TASKMGR.O88, which is there for the chip menu on a single-floppy machine
# (SPEC.md 28.1) and not to be double-clicked.
#
# The order of these lists DOES NOT MATTER and nothing may be built on it.
# It used to: the listing was directory order, so the order a package was
# named here was the row it appeared on, new packages had to append at the
# end of their folder, and the scripted tests clicked by that index. The
# mount sorts by name now (SPEC.md 19.4), so a volume lists alphabetically
# whoever wrote it and whatever order its entries are stored in - which is
# also the only answer that survives a host OS writing to the disk. What is
# left here is which packages ship and which folder each lands in.
APPS_TOOLS := $(BUILD)/artful.o88 $(BUILD)/calc.o88 $(BUILD)/fractal.o88 \
              $(BUILD)/hello.o88 $(BUILD)/modplug.o88 $(BUILD)/notepad.o88 \
              $(BUILD)/paint.o88 $(BUILD)/piano.o88 $(BUILD)/recorder.o88 \
              $(BUILD)/texpad.o88 $(BUILD)/tracker.o88
APPS_GAMES := $(BUILD)/arkanoid.o88 $(BUILD)/cyclone.o88 $(BUILD)/mines.o88 \
              $(BUILD)/missile.o88 $(BUILD)/solitair.o88 $(BUILD)/tamegram.o88

# Data that ships beside the programs that read it (SPEC.md 24): os88disk.py
# treats anything not ending .o88 as a plain file. Tracker with no module to
# open is a player with nothing to play, and this is the one it was written
# against - so it travels with it rather than being something you have to
# find. 116KB, which the 360KB disk can still hold alongside every package.
#
# It lives in MEDIA/ rather than beside the players in APPS/, because MEDIA
# is where a File Open starts (SPEC.md 38.10): the module Tracker and ModPlug
# were written against is in the folder their Open dialog already opens on,
# which is the whole point of having a default location at all.
#
# TeXPad's two documents are here for that same reason, and they are the
# reason the folder is not just the module's: PAPER.TEX is a short paper that
# exercises the dialect the typesetter implements, and GUIDE.TEX is the
# markup written up as a document TeXPad itself sets - so the manual for the
# markup is a worked example of it. Both are the kernel's default Open
# location, and both are ASSOCIATED (SPEC.md 69.6), so a double-click on
# either one opens TeXPad on it without going through APPS/ at all.
APPS_DATA := apps/tracker/beverly.mod apps/texpad/PAPER.TEX \
             apps/texpad/GUIDE.TEX

# The Task Manager, in SYSTEM/ and not in the root, because that is where
# ui_tm_open looks (SPEC.md 28.3). Not in APPS_TOOLS - it is not a program to
# go and find, it is the chip menu's, and a copy in APPS/ would be a second
# one to double-click by mistake.
APPS_SYS := $(SYSAPPS)

# OS88NET.COM, the DOS end of the parallel link (SPEC.md 62), in SYSTEM/DOS.
# It is the one thing on either floppy that does not run on os8088 at all: it
# is an MS-DOS .COM for the machine at the OTHER end of the cable, and it is
# here so that the user has it - the link is how files reach these disks in
# the first place, so "copy it off the disk that came with the OS" cannot
# depend on already having a way to move a file across.
#
# On the APPS disk rather than the system disk, so a single-floppy machine
# does not have to eject the disk it booted from to reach it; and in SYSTEM/
# rather than the root because it is machinery and not a program to go and
# find - the same argument that put TASKMGR.O88 there (SPEC.md 28.3). DOS/
# below it is what says which machine it is for: a .COM in SYSTEM/ beside a
# .O88 invites a double-click, which gives 'Bad package' (the loader refuses
# anything that is not a v3 package) and reads as a broken file rather than
# as a file for another computer.
APPS_DOS := $(BUILD)/os88net.com

APPS := $(APPS_TOOLS) $(APPS_GAMES) $(APPS_DATA) $(APPS_SYS) $(APPS_DOS)

# ...and the same list with the folder each package lands in. os88disk.py
# reads a "DIR:" prefix per package, so the grouping lives here rather than
# in the tool; no prefix means the root - and no package uses it any more, so
# the apps disk lists four folders and nothing else (ASSOC.DAT is the tool's
# own, and hidden).
#
# "SYSTEM/DOS:" is a NESTED folder: the prefix is a path now, every component
# an 8.3 stem, and naming a folder makes every folder above it - so SYSTEM/
# is made by whichever of these two arguments os88disk.py reads first. The
# kernel needs nothing for it: a subdirectory's '..' carries its parent's
# first cluster (SPEC.md 19.2), so dsk_dotdot walks back up out of a folder
# two deep exactly as it does out of one.
APPSARGS := $(addprefix APPS:,$(APPS_TOOLS)) \
            $(addprefix GAMES:,$(APPS_GAMES)) \
            $(addprefix MEDIA:,$(APPS_DATA)) \
            $(SYSAPPSARGS) \
            $(addprefix SYSTEM/DOS:,$(APPS_DOS))

$(APPSIMG): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 $(APPSARGS)

$(APPSIMG720): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 $(APPSARGS)

$(APPSIMG360): $(APPS) tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 $(APPSARGS)

# =============================================================================
# THE EVERYTHING DISK (ON DEMAND: `make allapps`) - SPEC.md 19.9
# =============================================================================
# build/apps-all.img: ONE 1.44MB floppy with every application this project
# ships on it, including the four that have their own disks and therefore
# never appear on the shipped apps disk - FROTZ (SPEC.md 61), WORD (SPEC.md
# 65), CWORD (SPEC.md 70.12) and RUNCPM (SPEC.md 71). It is a CONVENIENCE, offered beside the
# shipped images on a release page for somebody who wants one disk rather
# than four, and nothing in the tree boots it by default.
#
# It is NOT in `all`, and the reason is CWORD: a C package needs SmallerC,
# which tools/setup-cc.sh fetches and which is deliberately not in this tree
# (SPEC.md 70.1). A clone with nasm and python3 builds every SHIPPED floppy;
# this one target is the exception, so it is on demand exactly like cworddisk.
#
# WHY 1.44MB AND ONLY 1.44MB. The contents are ~1,050KB with RUNCPM's drive
# on it (~430KB before). That is not a geometry choice made to be generous - a
# 720KB or 360KB build of this list simply does not fit, and the shipped disks
# already cover those machines. So there is one size here and no --size
# variants to keep in step.
#
# THE TREE: each Word gets a FOLDER OF ITS OWN rather than a place in APPS/,
# and that is a correctness requirement and not tidiness. Both carry an
# overlay resolved in the launching instance's current directory (SPEC.md
# 65.10, 67.14, 19.2.1), and a double-click on a document leaves that
# directory on the DOCUMENT's (SPEC.md 54.9) - so package, overlay and welcome
# document have to be three files in one folder or the document opens a
# program whose every menu then refuses.
#
# FROTZ ships without a story. The stories are fetched by tools/getstories.py
# and are never committed (SPEC.md 61), so what rides here is the interpreter;
# `make zdisk` is still where a story disk comes from.
#
# RUNCPM (SPEC.md 71.5) rides the same way the Words do - a folder of its own,
# RUNCPM\, because it too has an .OVL resolved in the launching instance's
# folder, and the CCP it loads and the CP/M drive A\0 below it are found the
# same way - and, unlike FROTZ, WITH its disk: the master disk is fetched by
# tools/getruncpm.py (never committed, the same rule as the stories) and this
# target acquires the fetch as a prerequisite, which it can because it already
# needs the C toolchain. The A\0 selection is the 1.44MB one - the whole
# master disk minus the three files above 65,535 bytes, its LEFT-OFF.TXT
# saying so - chosen at recipe time exactly as build/runcpm.img's is
# (RUNCPMIMG's shell substitution and its empty-selection guard), and A\0
# is a deep folder with the same 128 directory slots. --select is told what
# it chooses beside: --reserve names EVERY FILE ON THIS DISK (ALLAPPSFILES,
# the files behind ALLAPPSARGS - not the prerequisite list, which carries
# tools and a stamp that never ride), and --folders the folder directories
# the tree above has besides RUNCPM\A\0, one cluster each at 1.44MB's 16
# entries a cluster - DERIVED from ALLAPPSARGS below (ALLAPPSDIRS: every
# DIR: prefix, each one's parent, --folder DOCS, and RUNCPM\A, the
# selection's own parent; ten today: APPS, GAMES, MEDIA, WORD, CWORD,
# RUNCPM, RUNCPM\A, SYSTEM, SYSTEM\DOS, DOCS), so the budget is derived
# here as it is for build/runcpm.img, and a folder added to the tree above
# is priced without anyone remembering a constant. One parent level is
# taken (the tree nests one deep); a DIR/SUB/SUB2: entry would need its
# grandparent added by hand.
ALLAPPSIMG := $(BUILD)/apps-all.img

ALLAPPSFILES := $(APPS) $(BUILD)/frotz.o88 \
                $(BUILD)/word.o88 $(BUILD)/WORD.OVL $(BUILD)/WELCOME.DOC \
                $(BUILD)/cword.o88 $(BUILD)/CWORD.OVL $(BUILD)/WELCOME.RTF \
                $(RUNCPMDISK)
ALLAPPS := $(ALLAPPSFILES) $(RUNCPMDEPS)

ALLAPPSARGS := $(addprefix APPS:,$(APPS_TOOLS) $(BUILD)/frotz.o88) \
               $(addprefix GAMES:,$(APPS_GAMES)) \
               $(addprefix MEDIA:,$(APPS_DATA)) \
               $(addprefix WORD:,$(BUILD)/word.o88 $(BUILD)/WORD.OVL \
                                 $(BUILD)/WELCOME.DOC) \
               $(addprefix CWORD:,$(BUILD)/cword.o88 $(BUILD)/CWORD.OVL \
                                  $(BUILD)/WELCOME.RTF) \
               $(addprefix RUNCPM:,$(RUNCPMDISK)) \
               $(SYSAPPSARGS) \
               $(addprefix SYSTEM/DOS:,$(APPS_DOS))
ALLAPPSDIRS := $(sort $(foreach a,$(ALLAPPSARGS),$(firstword $(subst :, ,$a))) \
                      DOCS RUNCPM/A)
ALLAPPSDIRS := $(sort $(ALLAPPSDIRS) \
                      $(patsubst %/,%,$(filter-out ./,$(dir $(ALLAPPSDIRS)))))
ALLAPPSFOLDERS := $(words $(ALLAPPSDIRS))

allapps: $(ALLAPPSIMG)

$(ALLAPPSIMG): $(ALLAPPS) tools/os88disk.py
	sel="$$(python3 tools/getruncpm.py -o $(RUNCPMDIR) --select 1440 --dir-slots $(RUNCPMSLOTS) --folders $(ALLAPPSFOLDERS) --reserve $(ALLAPPSFILES) | sed 's,^,RUNCPM/A/0:,')"; \
	[ -n "$$sel" ] || { echo "allapps: getruncpm.py --select 1440 chose nothing"; exit 1; }; \
	python3 tools/os88disk.py -o $@ --size 1440 --deep-folders --dir-slots RUNCPM/A/0=$(RUNCPMSLOTS) --folder DOCS $(ALLAPPSARGS) $$sel
	@python3 tools/os88disk.py --verify $@
	@echo "allapps: $@ - every app on one 1.44MB floppy; boot the system"
	@echo "         disk with it in B: (make run RUNAPPS=$@)"

# `make combo` -> build/combo.img: ONE 360KB bootable disk with the system,
# every application AND the four benchmarks on it.
#
# THIS IS THE DEFAULT DISK FOR A FIELD OR BENCH REQUEST. Build and send this
# one unless the ask is a `make field` case (a 720KB geometry, a knob kernel,
# or an adapter pinned at boot to match an older set). It used to be the
# herc.img/cga.img pair, and what changed is SPEC.md 39.11: the adapter
# stopped being a property of the BUILD, so one disk now takes a set from both
# cards - see below.
#
# The field machine has ONE floppy drive (docs/FIELD-MACHINES.md), so the
# three-disk shape `all` produces - system, apps, bench - is two disk swaps,
# and on that machine a swap is a walk to another room. This is the whole
# session on one disk.
#
# WHAT IT LEAVES OFF, because 360KB is 354 clusters and everything is 484:
#
#   MEDIA/BEVERLY.MOD  114 cl - the module Tracker and ModPlug open. It is a
#                      third of the disk on its own, and it is the one item
#                      here that is DATA rather than software: the two players
#                      still launch, they just have nothing to open. Use the
#                      shipped apps disk when the module is the point.
#   BIGFILE.DAT        104 cl - sysbench's cache-capacity sweep and the DOS
#                      read-rate cross-check. sysbench says so in the report
#                      and skips the row (SPEC.md 57.3 rule 2's shape); every
#                      other row still runs. It is on the `make field` disks,
#                      which is where that measurement belongs.
#   README.TXT         16 cl - the manual, on a disk that is for running.
#
# ONE IMAGE AND NOT ONE PER CARD, and that is SPEC.md 39.19 rather than a
# compromise: the probe still finds the Hercules first (39.1), and the Control
# Panel's Display page then switches the primary to the CGA or extends the
# desktop across both without rebuilding anything. So the operator runs
# GFXBENCH, switches the display, and runs it again - and because gfxbench
# names its report after the adapter it FOUND, both sets land on the one disk
# without colliding. SYSBENCH is run once: none of its rows is about the
# adapter. This is also the PLAINEST kernel of the lot - the shipped one, with
# no VIDEO= forced - so a field request no longer hands anybody a
# forced-adapter kernel at all, which is what put a VGA machine down the CGA
# path and cost the Packard Bell 286 its first set.
# The disk is NOT write-protected - SYSTEM.CFG is what remembers that choice.
COMBOARGS := $(DRIVERS) $(SYSAPPSARGS) \
             $(addprefix APPS:,$(APPS_TOOLS)) \
             $(addprefix GAMES:,$(APPS_GAMES)) \
             $(BENCHPKGS) $(BUILD)/bench.dat $(BUILD)/benchsml.dat

combo: $(BUILD)/combo.img

# ...and the same disk at 1.44MB, which is a DIFFERENT MACHINE and not a
# bigger version of the one above. A 1.44MB disk needs a 500 kbps controller
# and a BIOS that knows about it, so it will not boot the calibration 5150 -
# an IBM 5150/XT ROM tops out at 360KB and its 8-bit controller runs the data
# rate to match (docs/FIELD-MACHINES.md). This is the image for QEMU, for the
# AT-class 86Box profiles, and for a Gotek or USB floppy on a machine with a
# 1.44MB drive; `make combo` is still the one for real XT-class iron.
#
# It carries the three things the 360KB combo leaves off, because every reason
# given for dropping them up there is CLUSTERS and this disk has 2,847 of them
# against 354: BEVERLY.MOD (so Tracker and ModPlug have something to open),
# BIGFILE.DAT (so sysbench's cache-capacity sweep and DOS read-rate row RUN
# instead of being skipped) and README.TXT, plus the logo so File > Open lands
# somewhere that is not empty. Same plainest kernel - no VIDEO= forced - so
# one image still covers both cards through the Display page.
COMBO144ARGS := $(COMBOARGS) $(BUILD)/bigfile.dat \
                MEDIA:apps/tracker/beverly.mod $(SYSLOGOARG) $(SYSDOC)

combo144: $(BUILD)/combo144.img

$(BUILD)/combo144.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin $(DRIVERS) \
                    $(APPS_TOOLS) $(APPS_GAMES) $(SYSAPPS) \
                    $(BENCHPKGS) $(BUILD)/bench.dat $(BUILD)/benchsml.dat \
                    $(BUILD)/bigfile.dat $(SYSDOC) $(SYSLOGO) \
                    apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 1440 \
		--boot $(BUILD)/boot.bin --kernel $(BUILD)/kernel.bin \
		$(COMBO144ARGS)
	@echo "combo144: $@ - the combo disk at 1.44MB, with BEVERLY.MOD,"
	@echo "          BIGFILE.DAT and README.TXT that the 360KB one has no"
	@echo "          room for. NOT for the 5150: a 1.44MB disk needs a"
	@echo "          500 kbps controller and a BIOS that knows about it."
	@echo "          QEMU, the AT-class 86Box profiles, or a Gotek."

# ...and the same disk again at 720KB, which is the GEOMETRY between the two
# above rather than a third payload. A 720KB 3.5" DD disk is what an XT or AT
# fitted with a 3.5" drive takes, and - the reason it earns a combo of its own -
# what every USB floppy drive and every Gotek reads, neither of which will touch
# 5.25" media at all. So this is the field disk for a machine that cannot be fed
# a 360KB floppy and cannot read a 1.44MB one either.
#
# It carries COMBO144ARGS, the FULL payload, because the space is there: 720KB
# is 713 clusters of 1KB against the 360KB disk's 354, and the 360KB combo
# spends 304 of those - so BEVERLY.MOD, BIGFILE.DAT, README.TXT and the logo all
# fit with room over, and sysbench's cache sweep and DOS read-rate rows RUN here
# rather than being skipped the way they are on the 360KB disk.
#
# Same boot sector as the 360KB disk, and that is not a shortcut: 9 spt and 2
# heads are identical and the sector derives the cylinder from the LBA rather
# than counting them, so what differs is the BPB, which os88disk.py writes over
# the first 62 bytes. $(IMG720) above is the same argument for the system disk.
combo720: $(BUILD)/combo720.img

$(BUILD)/combo720.img: $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) \
                    $(APPS_TOOLS) $(APPS_GAMES) $(SYSAPPS) \
                    $(BENCHPKGS) $(BUILD)/bench.dat $(BUILD)/benchsml.dat \
                    $(BUILD)/bigfile.dat $(SYSDOC) $(SYSLOGO) \
                    apps/tracker/beverly.mod tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 720 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(COMBO144ARGS)
	@echo "combo720: $@ - the combo disk at 720KB, full 1.44MB payload."
	@echo "          The geometry a Gotek or a USB floppy reads. Boots any"
	@echo "          machine with a 3.5\" DD drive; NOT the 5150, which is"
	@echo "          5.25\" only - that is \`make combo\`."

$(BUILD)/combo.img: $(BUILD)/boot360.bin $(BUILD)/kernel.bin $(DRIVERS) \
                    $(APPS_TOOLS) $(APPS_GAMES) $(SYSAPPS) \
                    $(BENCHPKGS) $(BUILD)/bench.dat $(BUILD)/benchsml.dat \
                    tools/os88disk.py
	python3 tools/os88disk.py -o $@ --size 360 \
		--boot $(BUILD)/boot360.bin --kernel $(BUILD)/kernel.bin \
		$(COMBOARGS)
	@echo "combo: $@ - system + apps + benchmarks on ONE 360KB boot disk"
	@echo "       THE DEFAULT DISK FOR A FIELD OR BENCH REQUEST. One image for"
	@echo "       BOTH cards: Control Panel > Display switches the adapter at"
	@echo "       run time, and gfxbench names its report after the one it"
	@echo "       found - so run it, switch, run it again. sysbench once."
	@echo "       no BEVERLY.MOD, no BIGFILE.DAT, no README.TXT (see the Makefile)"

# The GUI reads a Microsoft serial mouse on COM1 or COM2 (SPEC.md 9.5); QEMU
# emulates one natively. MOUSEPORT= says where, and on WHICH IRQ:
#
#   make run                    the mouse on COM1, nothing at 2F8 - so the
#                               probe finds one port and the kernel runs the
#                               single-port path it always did
#   make test MOUSEPORT=com2    a live but SILENT UART at 3F8 and the mouse at
#                               2F8 on its textbook IRQ3. The two-port contest:
#                               a first port that must be retired rather than
#                               preferred. Leaving 3F8 unpopulated instead
#                               would test only the easy half
#   make test MOUSEPORT=com2irq4  THE COMPAQ PORTABLE III (SPEC.md 9.5.2): the
#                               mouse at 2F8 with its card driving IRQ4, which
#                               is where the base-to-IRQ convention the kernel
#                               used to rely on stops being true. Before that
#                               fix this configuration never finds the mouse at
#                               all - [mou_seen] stays 0 however far you move
#                               it - so it is the regression test for a bug
#                               that took real hardware to find
#   make test MOUSEPORT=com1irq3  the mirror image, for symmetry
#
# `-serial` cannot set an IRQ, so these go through `-device isa-serial`, which
# takes iobase= and irq= and is what makes a cross-wired card reproducible at
# all. `-serial none` suppresses the machine's default ports so the devices
# below are the only ones.
MOUSEPORT ?= com1
MOUSEQ    := -serial none -chardev null,id=mq
ifeq ($(MOUSEPORT),com2)
MOUSE := $(MOUSEQ) -device isa-serial,chardev=mq,iobase=0x3f8,irq=4 \
         -chardev msmouse,id=m0 -device isa-serial,chardev=m0,iobase=0x2f8,irq=3
else ifeq ($(MOUSEPORT),com2irq4)
MOUSE := $(MOUSEQ) -device isa-serial,chardev=mq,iobase=0x3f8,irq=4 \
         -chardev msmouse,id=m0 -device isa-serial,chardev=m0,iobase=0x2f8,irq=4
else ifeq ($(MOUSEPORT),com1irq3)
MOUSE := $(MOUSEQ) -device isa-serial,chardev=mq,iobase=0x2f8,irq=3 \
         -chardev msmouse,id=m0 -device isa-serial,chardev=m0,iobase=0x3f8,irq=3
else
MOUSE := -chardev msmouse,id=m0 -serial chardev:m0
endif

# RUNAPPS is what goes in B:, and it exists so that a disk built on demand can
# be LOOKED AT rather than only driven over QMP. `make test` has taken TESTAPPS
# since the first test disk; the interactive target hardcoded the apps floppy,
# so seeing tests/facetest or tests/bandbench meant a hand-written qemu line.
#   make bench && make run RUNAPPS=build/bench.img
#   make worddisk && make run RUNAPPS=build/word.img
RUNAPPS ?= $(APPSIMG)

run: $(IMG) $(RUNAPPS)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(RUNAPPS),format=raw,if=floppy,index=1 $(DEVCARD)

# A maxed-out 640KB machine. QEMU/SeaBIOS cannot boot with less than 1MB
# of guest RAM (SeaBIOS wedges during POST at -m 512k and -m 640k alike),
# but conventional memory tops out at 640K regardless of installed RAM, so
# -m 1M makes int 12h report 640K - same as a fully populated XT.
run-640: $(IMG) $(APPSIMG)
	$(QEMU) -m 1M -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG),format=raw,if=floppy,index=1 $(DEVCARD)

# The 720KB pair. QEMU picks a floppy's geometry from the image SIZE, and
# 737,280 bytes is a standard one (2 heads x 80 cyl x 9 spt), so this needs
# nothing beyond naming the two images - which is itself the check that
# matters: a 720KB image the BIOS reads as some other shape fails here.
run-720: $(IMG720) $(APPSIMG720)
	$(QEMU) -drive file=$(IMG720),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(APPSIMG720),format=raw,if=floppy,index=1 $(DEVCARD)

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

# DBG=1 puts the serial monitor's UART in the machine (SPEC.md 58): a second
# card at COM4 - 2E8, IRQ3, which is where 86Box and QEMU both put COM4 - with
# a UNIX socket on the host end, so tools/os88dbg.py can drive it.
#
#   make test DBG=1
#   python3 tools/os88dbg.py build/dbg.sock m 60 0 20
#
# 2E8 and NOT 2F8, because os8088 probes 3F8 and 2F8 and hooks the IRQ of
# every UART that answers (SPEC.md 9.5) - the monitor would be fighting the
# mouse for its own port. 3E8/2E8 are the two the kernel has never heard of.
# The driver refuses to attach if a card DID answer at 2F8, because that is
# the case where IRQ3 is already the kernel's; nothing here supplies one, and
# MOUSEPORT=com2 deliberately does, which is the negative test.
#
# `server=on,wait=off` so the machine boots whether or not anybody is
# listening, and the tool may attach and detach as often as it likes.
ifneq ($(DBG),)
DBGDEV = -chardev socket,id=dbg,path=$(BUILD)/dbg.sock,server=on,wait=off \
         -device isa-serial,chardev=dbg,iobase=0x2e8,irq=3
endif

# TESTAPPS swaps the B: disk for a scratch image - the filetest/fmtest/sbtest
# gates and the bench disk. It MUST be defined above the first target that
# names it: prerequisites are expanded when the rule is READ, so a definition
# below `test:` leaves that prerequisite empty. It sat below for a long time
# and `test` therefore hard-coded $(APPSIMG) - so `make test TESTAPPS=...`
# silently booted the SHIPPED apps disk, which reads as the scratch image
# having failed to build rather than never having been mounted.
TESTAPPS ?= $(APPSIMG)

# HDD=<megabytes> puts a blank raw IDE disk in the machine, for the hard-disk
# driver (SPEC.md 52). Without one its probe correctly finds nothing, which is
# the right answer and not the one you want to be testing against - the same
# reasoning as ADLIB= above. The image is created once and then kept, because
# partitioning and formatting it is the thing under test.
ifneq ($(HDD),)
HDDIMG = $(BUILD)/hdd.img
HDDDEV = -drive file=$(HDDIMG),format=raw,if=ide,index=0,media=disk
$(HDDIMG):
	dd if=/dev/zero of=$@ bs=1024 count=$$(( $(HDD) * 1024 )) 2>/dev/null
endif

test: $(IMG) $(TESTAPPS) $(HDDIMG)
	$(QEMU) -drive file=$(IMG),format=raw,if=floppy -boot a $(MOUSE) \
		-drive file=$(TESTAPPS),format=raw,if=floppy,index=1 $(HDDDEV) \
		-display none -qmp unix:build/qmp.sock,server,nowait -daemonize -pidfile build/qemu.pid \
		$(CARDAUDIO) $(ADLIBDEV) $(SBDEV) $(DEVCARD) $(DBGDEV)

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
# setting cannot silently regress.
#
# BOTH floppies now, because the BOOT floppy is a writable FAT12 volume too
# (SPEC.md 19.3) and SYSTEM.CFG lives in its root: protected, every Control
# Panel setting silently fails to survive a reboot. Its old justification -
# "sector 0 has no valid BPB so the kernel refuses to write it anyway" -
# stopped being true when the system disk became a real volume.
#
# The cost is the one QEMU already imposes: a machine that writes its settings
# changes build/os8088.img, which is untracked scratch but persists across
# boots, so `rm -f build/os8088.img build/os8088-720.img
# build/os8088-360.img && make` when a run's starting state matters.
# perl -pi behaves identically on GNU and BSD/macOS, unlike sed -i.
UNPROTECT = perl -pi -e 's{^fdd_01_fn = wp://}{fdd_01_fn = }; s{^fdd_02_fn = wp://}{fdd_02_fn = }'

# Boot the 360KB image on emulated period hardware in 86Box.
xt: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VM)/86box.cfg
	$(BOX) -P $(VM) -N

# The same XT with a full 640KB of RAM instead of 256KB.
xt-640: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VM640)/86box.cfg
	$(BOX) -P $(VM640) -N

# The two monochrome machines (SPEC.md 39), both 256KB - which is all an
# ibmxt takes anyway, and the floor os8088 targets. These are the ONLY way to
# exercise the detection probe and the Hercules renderer: QEMU has no such
# card, so `make test VIDEO=cga` covers the mono renderer but never the probe.
xt-cga: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VMCGA)/86box.cfg
	$(BOX) -P $(VMCGA) -N

xt-hercules: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VMHERC)/86box.cfg
	$(BOX) -P $(VMHERC) -N

# ...and the same XT with BOTH of them in it: SPEC.md 39.12-39.19's extended
# desktop on the machine it was written for. A CGA at B8000 and a Hercules at
# B0000, two CRTCs, two monitors, and 86Box opens a window per card ("86Box
# Monitor #2" is the Hercules). That pairing is the one thing QEMU cannot
# stage at all and the one `vid_dual_ok` accepts: [vid_avail] must be exactly
# VID_A_HERC | VID_A_CGA, so a second VGA-shaped card would not do.
#
# THE SECOND CARD IS `hercules_plus` AND NOT `hercules`, WHICH IS NOT A
# PREFERENCE - it is the difference between a machine that offers the extended
# desktop and one that silently does not. 86Box's plain `hercules` does not
# answer 32KB at B0000 while the card is still in the text mode POST left it
# in: vid_memchk writes 0x55 at B000:0000 and 0xAA at B000:1000 and reads the
# first back, and on that device the second write lands on the first. That is
# the MDA signature the probe exists to reject (SPEC.md 39.11.1 - a text-only
# 4KB card has no 720x348 mode to offer), so the kernel is right and the model
# is what differs; `hercules_plus` - a real 1986 HGC+, and period hardware for
# an ibmxt86 - keeps the two offsets apart and is found. MEASURED, both ways,
# by forcing [vid_dmode] to Extend and watching which card the desktop grows
# onto: `hercules` stays black in POST's text mode, `hercules_plus` comes up
# carrying the desktop. If the Control Panel has no Display page on some other
# 86Box video pairing, this is the first thing to suspect - the page is hidden
# when [vid_avail] has one bit (SPEC.md 31.10.1) and nothing announces why.
#
# THE CGA IS PRIMARY, which is what `gfxcard` means here - `gfxcard_2` is the
# card the BIOS does not own, and the OS duly comes up on the colour one. That
# matters because the primary is what the chrome is drawn on (SPEC.md 39.16)
# and what sits at the virtual origin. Swapping which monitor carries the menu
# bar is the Control Panel's job (Display -> a row -> Activate,
# SPEC.md 39.19.2), not this file's.
# It also matches vm/xt-multimon's opposite number under MartyPC,
# os8088_5150_both, which the tools/martypc configs put in the same order for
# a POST reason - so the two emulators disagree about nothing.
#
# THE EXTENDED DESKTOP IS OFF WHEN IT BOOTS, and that is SPEC.md 39.19.1: the
# kernel can detect a second CARD and nothing can detect a second MONITOR, so
# Single is the default and the second window stays dark until the machine is
# told. Control Panel -> Display -> Desktop: Right or Below. The setting is
# written to SYSTEM.CFG when the panel is CLOSED (SPEC.md 31.8), so close it
# with the box on the title bar and the next boot comes up extended.
#
# 640KB on an `ibmxt86` planar, where the two mono machines above are 256KB on
# an `ibmxt`: a second display costs no conventional RAM (both framebuffers
# are card memory) but the windows opened across it do, and a machine bought
# to have more desktop should be able to fill it.
xt-multimon: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VMMULTI)/86box.cfg
	$(BOX) -P $(VMMULTI) -N

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
	@$(UNPROTECT) $(VM286)/86box.cfg
	$(BOX) -P $(VM286) -N

386sx: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VM386SX)/86box.cfg
	$(BOX) -P $(VM386SX) -N

386: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VM386DX)/86box.cfg
	$(BOX) -P $(VM386DX) -N

# The three sound machines: an XT with a Sound Blaster 2.0 (so the OPL2 is
# the FM tier and the DSP the stream tier on the CPU this OS is FOR), and the
# 286/386 with an SB16. `make test ADLIB=1` / `SB16=1` gives the driver
# something to attach to under QEMU; these give it a card on a machine whose
# bus and clock are period-correct, which is the only place a stream's pacing
# means anything (SPEC.md 34.5/51.4).
xt-sound: $(IMG360) $(APPSIMG360)
	@$(UNPROTECT) $(VMXTSND)/86box.cfg
	$(BOX) -P $(VMXTSND) -N

286-sound: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VM286SND)/86box.cfg
	$(BOX) -P $(VM286SND) -N

386-sound: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VM386SND)/86box.cfg
	$(BOX) -P $(VM386SND) -N

# The two FROTZ machines (SPEC.md 61.9), both with the Z-machine story floppy
# in B: instead of the apps disk.
#
#   xt-z   An IBM XT at 4.77MHz with a Sound Blaster 2.0 and the FULL 640KB,
#          booting the 360KB system floppy with a 720KB 3.5" DD story disk in
#          B:. The 640KB is not a luxury: a story is RESIDENT (SPEC.md 61.4),
#          and after the 92KB kernel that leaves about 549KB, which is what
#          makes everything on the disk playable. The 3.5" DD drive is not an
#          anachronism either - DOS 3.2 supported one on an XT, and 360KB does
#          not hold a library.
#
#   386-z  The comfortable target the same code also has to be right on: a
#          386 with an SB16 and TWO 1.44MB drives, so B: is the full library
#          disk and `build/zork2.img` is the one you swap in for Anchorhead
#          and Bronze. AT-class, so the first launch stops at the BIOS setup
#          screen wanting a CMOS - pick EXIT FOR BOOT once and 86Box writes
#          vm/386-z/nvr/ for every later boot.
#
# Both call $(UNPROTECT) for the reason every other 86Box target does: 86Box
# rewrites its own config on exit and has twice re-added the wp:// prefix,
# which makes every guest write fail as FERR_WPROT - and here that would be
# every save game, reading as a Frotz bug rather than an emulator setting.
xt-z: $(IMG360) $(BUILD)/zork720.img
	@$(UNPROTECT) $(VMXTZ)/86box.cfg
	$(BOX) -P $(VMXTZ) -N

386-z: $(IMG) $(BUILD)/zork.img $(BUILD)/zork2.img
	@$(UNPROTECT) $(VM386Z)/86box.cfg
	$(BOX) -P $(VM386Z) -N

# The two WORD machines (SPEC.md 68.5), both with the Word document floppy in
# B: instead of the apps disk - Frotz's precedent, for Frotz's reason: an app
# whose documents live on its own disk is best launched from that disk.
#
#   xt-word   An IBM XT at 4.77MHz with the FULL 640KB - the document, CHP,
#             save-staging and undo claims are what the memory is for
#             (SPEC.md 68.5) - booting the 360KB system floppy with the
#             720KB Word disk in B: (the 3.5" DD drive xt-z already
#             established as period-plausible). NO sound card: Word makes no
#             sound, so the plain-machine precedent applies rather than the
#             sound-machine one.
#
#   386-word  The comfortable target the same code also has to be right on:
#             a 386DX/25 with TWO 1.44MB drives, B: = build/word.img.
#             AT-class, so the first launch stops at the BIOS setup wanting
#             a CMOS - pick EXIT FOR BOOT once and 86Box writes
#             vm/386-word/nvr/ for every later boot.
#
# Both call $(UNPROTECT) for the standing reason: 86Box re-adds wp:// to its
# floppy paths on exit, which turns every guest write into FERR_WPROT - and
# here that is every document save, reading as a Word bug rather than an
# emulator setting.
xt-word: $(IMG360) $(BUILD)/word720.img
	@$(UNPROTECT) $(VMXTWORD)/86box.cfg
	$(BOX) -P $(VMXTWORD) -N

386-word: $(IMG) $(BUILD)/word.img
	@$(UNPROTECT) $(VM386WORD)/86box.cfg
	$(BOX) -P $(VM386WORD) -N

# The CWORD machine (SPEC.md 70.12) - the C toolchain's demonstrator on a
# period machine, with build/cword.img in B: instead of the apps disk.
#
#   386-c-word  A 386DX/25 with TWO 1.44MB drives, B: = build/cword.img.
#               AT-class, so the first launch stops at the BIOS setup wanting
#               a CMOS - pick EXIT FOR BOOT once and 86Box writes
#               vm/386-c-word/nvr/ for every later boot.
#
# vm/386-c-word/86box.cfg is vm/386-word/86box.cfg with the B: image and the
# uuid changed and NOTHING else, which is the whole reason it exists as a copy
# of a machine that has been booted rather than as a profile written from the
# documentation: 86Box does not reject an unrecognised cpu_family, it
# substitutes that family's default speed and rewrites the config on exit, so
# a typo there is a machine running at a clock nobody chose and no error
# anywhere. $(UNPROTECT) for the standing reason - 86Box re-adds wp:// to its
# floppy paths on the way out, which turns every guest write into FERR_WPROT,
# and here that is every document save.
#
# ONE machine and not two. The XT is where a C package has to be MEASURED
# rather than merely run (PERFORMANCE.md: 756us a drawing call, ~900us a glyph
# cell, and C is 2-4x hand assembly), and an `xt-c-word` target before anybody
# has taken that measurement would be a claim rather than a machine.
386-c-word: $(IMG) $(BUILD)/cword.img
	@$(UNPROTECT) $(VM386CWORD)/86box.cfg
	$(BOX) -P $(VM386CWORD) -N

# The RUNCPM machine (SPEC.md 71.5): vm/386-c-word with B: = build/runcpm.img
# and the uuid changed and NOTHING else, for the same reason that one is a
# copy of vm/386-word (above). The banner's 'Estimated Z80 clock speed' read
# here is the number SPEC.md 71 records for the 386; the XT figure is taken
# on vm/xt640 with fdd_02_fn hand-pointed at build/runcpm360.img for the
# session (docs/RUNCPM-PORT-PLAN.md wave 2) - no xt-runcpm target until the
# measurement says the port is usable there.
386-runcpm: $(IMG) $(BUILD)/runcpm.img
	@$(UNPROTECT) $(VM386RUNCPM)/86box.cfg
	$(BOX) -P $(VM386RUNCPM) -N

# The MARTYPC DEBUGGER (docs/MARTYPC-DEBUG.md): a remote debug server bolted
# into MartyPC's headless frontend, giving memory, registers, breakpoints,
# single-step and cycle counts on a running os8088 with NO code in the guest
# at all. It is the other half of SPEC.md 58's DEBUG.DRV and not a replacement
# for it - this one costs the guest nothing and answers on a frozen machine,
# that one is the only one that works on real iron.
#
# Pinned to one upstream commit on purpose (tools/martypc/UPSTREAM): a debugger
# that changes under you is one more variable in a session whose whole point is
# removing them. Needs cargo, and on Linux libudev-dev + pkg-config.
marty: $(IMG360)
	tools/martypc/build.sh
	@mkdir -p $(BUILD)/martypc/run/media/floppies
	@cp $(IMG360) $(BUILD)/martypc/run/media/floppies/
	@echo "marty: cd $(BUILD)/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 \\"
	@echo "         ./martypc_headless --mount fd:0:media/floppies/os8088-360.img &"
	@echo "       python3 tools/os88marty.py 127.0.0.1:9001 verify"
	@echo ""
	@echo "       machines: os8088_5150_cga (default), _herc, _cga_gla, _sb,"
	@echo "                 _sbonly, os8088_xt_vga and _xt_vga_sb;"
	@echo "                 _both / _both_gla are the TWO-CARD 5150 (a CGA and"
	@echo "                 a Hercules, docs/DUAL-DISPLAY-PLAN.md) and _herc_gla"
	@echo "                 is the single-card Hercules without the IBM ROM."
	@echo "                 python3 tests/dualcheck.py is the two-card gate"
	@echo "       _herc_gla_144 has 1.44MB DRIVES, for make combo144 - the one"
	@echo "                 machine here that can read an 18-spt disk. An"
	@echo "                 anachronism on purpose: no stock XT reads 1.44MB"
	@echo "                 (500 kbps against the 8-bit card's 250), so it"
	@echo "                 proves OUR boot sector and FAT12 code at 18 spt"
	@echo "                 and nothing about the media. No timings off it"
	@echo "       _cga_lpt has a PARALLEL PORT at 378h (SPEC.md 62's NET.DRV)"
	@echo "                 and so does _xt_hdd, which is then the one machine"
	@echo "                 with TWO driver Control Panel pages at once"
	@echo "       ..._sb has an AdLib AND a Sound Blaster, _sbonly has the DSP"
	@echo "       and NOTHING at 388h - the SPEC.md 51.3.1 pair; _xt_vga_sb is"
	@echo "       the one to run with --turbo (7.16MHz, the fastest MartyPC has;"
	@echo "       the CGA panics there, so a turbo machine is a VGA one); add"
	@echo "       MARTYPC_WAV=/tmp/cap for one wav per source (sndcheck.py reads them)"

# The far end of the range, both carrying an SB16 on the ISA bus:
#
#   486      AMI 486 (SiS 471) board, 486DX2 @ 66MHz (2 x 33), 8MB
#   pentium  ASUS P/I-P55TP4XE (430FX), Pentium P54C @ 133MHz (2 x 66), 16MB
#
# 8086 real-mode code runs verbatim on both, so what these are FOR is the
# other end of the timing range: everything sized while looking at a 4.77MHz
# 8088 (typematic deadlines, the tracker's ring refill, Arkanoid's frame
# pacing) also has to behave on a machine two orders of magnitude faster,
# and that is not something QEMU's untimed execution can answer either.
#
# The CPU names are 86Box's own and were checked by launching it on a
# throwaway config and reading the file back after exit (which is how the
# tree checks any candidate machine): `pentium` is NOT a family - 86Box
# silently falls back to `pentium_p54c` at its default 75MHz, so a config
# saying `pentium` boots a P75 while claiming a P133. `i486dx2` is real.
#
# Both are AT-class, so the first launch of each stops at the BIOS setup
# screen wanting a CMOS - same one-time cost per VM directory as the 286.
486: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VM486)/86box.cfg
	$(BOX) -P $(VM486) -N

pentium: $(IMG) $(APPSIMG)
	@$(UNPROTECT) $(VMPENT)/86box.cfg
	$(BOX) -P $(VMPENT) -N

# NOTHING IN build/ IS TRACKED, and that is a decision rather than an accident.
#
# For most of this tree's life the opposite held: build/ was gitignored but 41
# artifacts inside it - the kernel, both boot sectors, all three bootable
# floppies, all three software floppies, both drivers and every package's
# .bin/.o88 - were force-added and shipped, so a clone could boot without a
# toolchain. Nothing made them follow a source change, so they went stale in
# silence, and `check-images` lived here to catch that by building everything a
# second time and comparing byte for byte. It caught real staleness (two
# "Rebuild the shipped images" commits, and a merge that shipped a Paint two
# fixes out of date), which is the point: the cache had a correctness
# obligation, and the obligation was not free.
#
# A binary an artifact of THIS tree does not need to be committed:
#
#  - the toolchain is deterministic on purpose (tools/os88disk.py pins the
#    volume serial and every FAT timestamp), so `make` reproduces any of them
#    byte for byte - a committed copy carried no information a rebuild lacks;
#  - the images are published where a version can be attached to them: a GitHub
#    release, and os8088.com. .claude/skills/release-os8088 builds them fresh
#    from a clean checkout, so the release path never read the tracked copies;
#  - anyone running `make run` already has QEMU, and nasm is the easier of the
#    two to install.
#
# Three ongoing traps died with it, and they are the reason not to reintroduce
# any of this: STALE/ORPHAN/SCRATCH as a class (build/ was force-added
# wholesale more than once, which swept in a VIDEO= stamp twice and once took
# kernel.bin OUT of the repo); binary merge conflicts; and the sharpest one -
# QEMU mounts build/apps.img and build/os8088.img WRITABLE and the OS writes to
# them, so any test that saved a file or touched a Control Panel setting
# dirtied a shipped artifact and needed the image deleted and rebuilt before
# committing. Those images are now scratch, and a test may dirty them freely.
#
# The determinism is still load-bearing - it is what lets anyone rebuild a
# released image and get the same bytes - it just no longer has a make target
# guarding it.

# `clean` SPARES build/martypc, and that is deliberate. MartyPC is an
# INSTRUMENT, not an output of this source tree: it is pinned to an upstream
# commit (tools/martypc/UPSTREAM), nothing in this repo changes what it
# builds, and rebuilding it is a several-minute cargo build. A `clean` that
# threw it away made the DEFAULT test target expensive to get back, which is
# the wrong incentive when CLAUDE.md's rule is "build it at the START of a
# session". `clean-marty` is the escape hatch, and it is what re-pinning
# wants.
#
# It spares build/cc for the same reason and by the same test: SmallerC is an
# INSTRUMENT, not an output of this source tree. It is pinned to an upstream
# commit (tools/setup-cc.sh's PIN), nothing in this repo changes what it
# builds, and it is a fetch over the network - so a `clean` that threw it away
# would make the C targets need the network to come back, which is the wrong
# incentive for a check that is meant to be cheap to re-run. `clean-cc` is the
# escape hatch, and re-pinning does not need it: setup-cc.sh compares HEAD
# against PIN on every run and re-fetches when they differ.
clean:
	find $(BUILD) -mindepth 1 -maxdepth 1 ! -name martypc ! -name cc \
		-exec rm -rf {} + 2>/dev/null || true

clean-marty:
	rm -rf $(BUILD)/martypc

clean-cc:
	rm -rf $(BUILD)/cc

distclean: clean clean-marty clean-cc
