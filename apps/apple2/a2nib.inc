; =============================================================================
; os8088 - apps/apple2/a2nib.inc    the 6-and-2 nibbliser - A STUB
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
; apps/apple2/ is GPL-2-or-later (apps/apple2/COPYING).
;
; ----------------------------------------------------------------------------
; THIS FILE IS EMPTY ON PURPOSE, AND IT EXISTS ON PURPOSE
; ----------------------------------------------------------------------------
; Read-only Disk II is a FOLLOW-UP PULL REQUEST (APPLE2-SPEC section 14,
; docs/APPLE2-PORT-PLAN.md Decision 13). What lands here when it comes is
; pinned in section 14 so that the follow-up is a transcription and not a
; re-design: 48 self-sync $FF; `D5 AA 96` with 4-and-4 volume, track, sector
; and checksum; `DE AA EB`; 6 sync; `D5 AA AD`; Code62's 343 bytes with its
; running-XOR checksum through `ms_DiskByte[0x40]`; `DE AA EB`; 27 sync; and
; SkewTrack's `(track * 768) % nibbles` rotation.
;
; IT WILL BE ASSEMBLY AND RESIDENT (Decision 18), not C and not an `ovl_`: it
; is a per-nibble loop reached from inside an emulated slice through the
; controller's latch read, and an overlay call there would be a 400 ms floppy
; seek with the whole emulated machine stopped behind it.
;
; The file is here from wave 1 because it is a WRITTEN PREREQUISITE in the
; Makefile and a `%include` in apps/apple2/apple2.asm: make cannot see through
; either kind of include, and a file a later wave adds is a build the tree
; does not know about (LESSONS.md 9). An empty include is one line of nasm
; work; a missing one is a stale APPLE2.O88 that reads exactly like a change
; that did nothing.
; =============================================================================
