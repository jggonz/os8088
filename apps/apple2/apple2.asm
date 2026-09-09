; =============================================================================
; os8088 - apps/apple2/apple2.asm
;
; The assembly shim of APPLE2, an Apple II Plus emulator written in C
; (docs/APPLE2-SPEC.md). It is the top-level nasm source: the .c is never
; assembled on its own, because `nasm -f bin` has no notion of an external
; symbol, so the compiled C, the runtime, the 32-byte header and the
; hand-written includes are ONE assembly (SPEC.md 73.1).
;
; ----------------------------------------------------------------------------
; LICENCE AND ATTRIBUTION (APPLE2-SPEC section 1.3)
; ----------------------------------------------------------------------------
; apps/apple2/a2cpu.inc is a DERIVED COPY of apps/c64/c64cpu.inc, which is
; GPL-2-or-later by way of VICE. **apps/apple2/ is therefore GPL-2-or-later**;
; the rest of this tree is not, and apps/apple2/COPYING is the licence text
; the licence requires to accompany copies.
;
;   VICE      (https://vice-emu.sourceforge.io/)  (C) 1996-2025 the VICE team
;                                                 GPL-2-or-later - the 6502
;                                                 core, by way of apps/c64
;   AppleWin  (https://github.com/AppleWin/AppleWin)  (C) the AppleWin
;                                                 authors, GPL-2-or-later -
;                                                 every one of its source
;                                                 headers says "either
;                                                 version 2 ... or (at your
;                                                 option) any later
;                                                 version".
;                                                 everything II+-specific
;   MII       (https://github.com/buserror/mii_emu)                MIT
;   apple2emu (https://github.com/marketideas/apple2emu)           MIT
;
; Nothing of any reference tree's SOURCE is vendored (CONTRIBUTING.md 6): what
; is carried is behaviour, tables and strings, and APPLE2-SPEC section 2 names
; the file every user-visible surface came from. The Apple II+ ROM images are
; Copyright (C) Apple Computer, Inc., are FETCHED at a pin by
; tools/getapple2rom.py and are never committed (APPLE2-SPEC section 1.4).
;
; ----------------------------------------------------------------------------
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'APPLE2'        ; <= 15 chars; the Disk window's label, and
                                    ; the stem of APPLE2.OVL (SPEC.md 73.14).
                                    ; THE SHORT PRODUCT NAME IS `Apple II+`
                                    ; and the long one `Apple II Plus
                                    ; Emulator`: two strings with a stated
                                    ; rule (APPLE2-SPEC section 16.1), and
                                    ; neither of them is a package stem

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *) - the
                                    ; II+ byte map's MAKE half (APPLE2-SPEC
                                    ; section 6.1), plus the two reset chords
                                    ; Ctrl+F2 and Ctrl+F3 (section 6.3)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *) - a
                                    ; click KICKS the wake, so a wake the
                                    ; event ring refused cannot park a running
                                    ; machine (SPEC.md 74.1)
%define CC_HAS_ABOUT                ; void os88_about(void *) - the kernel's
                                    ; name pull-down opens the About panel
                                    ; (APPLE2-SPEC section 11)
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - THE slice
                                    ; driver's entry (SPEC.md 74.1): the one
                                    ; callback dispatched WITHOUT the gfx lock
%define CC_HAS_ONTIMER              ; void os88_ontimer(void *) - the flash
                                    ; phase's heartbeat (SPEC.md 13.9,
                                    ; APPLE2-SPEC section 7.6). ONE-SHOT and
                                    ; re-armed in the handler; the wake's poll
                                    ; is the SECOND PATH for a kernel that
                                    ; carries the slot and not the body
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - the
                                    ; four menus of APPLE2-SPEC section 10.1,
                                    ; AM_NAME 'Apple II+'
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Load
                                    ; Program... / Save Program...
                                    ; (APPLE2-SPEC section 12)
                                    ;
                                    ; NO CC_HAS_WORKER. A worker may not touch
                                    ; a file (SPEC.md 20.6 rule 7) and File >
                                    ; Quit goes through OSAPI_WM_CLOSE, which
                                    ; is C64-SPEC §15.2's correction already
                                    ; made rather than repeated
                                    ;
%define CC_ASSOC "apple2/a2assoc.inc"   ; **WAVE 4 TURNED IT ON** (SPEC.md
                                    ; 54.6, APPLE2-SPEC section 12): `.BAS` is
                                    ; MINE, so a tokenised Applesoft program
                                    ; beside APPLE2.O88 opens on the FIRST
                                    ; double-click of a COLD boot - no prior
                                    ; run and no search, which the SDK's
                                    ; runtime os88_assoc_set() cannot do. It
                                    ; was deliberately absent until this wave:
                                    ; declaring an extension the build cannot
                                    ; open launches the emulator and then
                                    ; refuses, which is worse than no
                                    ; association - the user has spent a
                                    ; floppy seek, a 64KB claim and a window
                                    ; to be told no. `DSK`, `DO` and `PO` land
                                    ; with the Disk II follow-up PR, for the
                                    ; same reason one wave along
%define CC_HAS_OVL                  ; APPLE2.OVL (SPEC.md 73.14) - ON FROM THE
                                    ; FIRST COMMIT (APPLE2-SPEC section 15.1).
                                    ; The alternative is discovering at 55,000
                                    ; that the code is not the kind that can
                                    ; move (LESSONS.md 5). The split is by
                                    ; FREQUENCY: the menu command shells
                                    ; (a2cmd.c), Load and Save Program
                                    ; (a2prog.c), the disk dialog (a2disk.c)
                                    ; and the About panel (a2about.c) are out;
                                    ; every callback, the core, the composers,
                                    ; the flush AND THE CHARGEN DECODE are in
%define CC_ICON "apple2/icon.inc"   ; the 16x16 machine, drawn for this port -
                                    ; not Apple's rainbow mark, which is trade
                                    ; dress

%define CC_HAS_FSX                  ; THE EXCLUSIVE BRACKET (SPEC.md 53,
                                    ; APPLE2-SPEC section 13): Machine >
                                    ; Color NTSC borrows the machine and puts
                                    ; the Apple's raster in mode 13h. The four
                                    ; os88_fsx_* thunks are 92 bytes and this
                                    ; is the only package in the tree that
                                    ; wants them - CC_HAS_PARTS's own idiom,
                                    ; and for a2band.inc's A2_SHIP reason:
                                    ; nasm has no dead-code elimination, so a
                                    ; define is the only thing that keeps them
                                    ; out of CWORD, WEAVE and LOOM

%define CC_HAS_PARTS                ; THE ROM IS IN THE PACKAGE (APPLE2-SPEC
                                    ; section 1.5, SPEC.md 20.12). A sidecar
                                    ; is a file a copy can separate from the
                                    ; program it is useless without; a part
                                    ; cannot go missing

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge

; --- the parts table (SPEC.md 20.12.3) ---------------------------------------
; ONE part: Applesoft BASIC, the Autostart Monitor, the character generator
; and the Disk II P5 boot ROM, in APPLE2-SPEC section 1.4's fixed layout. It
; is an ASSET rather than a SEGMENT because nothing in it is far-called - the
; 6502 core reaches it through segment arithmetic off its base - and it is
; REQUIRED, so a machine that cannot spare its 14,848 bytes refuses the launch
; instead of starting an Apple II with no ROM in it.
;
; AND op_load RUNS BEFORE ANY C DOES, which is what lets os88_main decode the
; character generator and build the 7-bit reverse table there - the decision
; that keeps the display path off the overlay (APPLE2-SPEC section 7.3).
    CC_PARTS_BEGIN 1
      OS88_PART OP_ASSET
    CC_PARTS_END

%include "apple2.gen.asm"           ; the compiled C, found through -I build/

; The hand-written half (SPEC.md 73.11's rule that the inner loop is
; assembly). ORDER MATTERS ONLY IN THAT a2cpu.inc declares the register file
; and the scratch layout that a2mem.inc addresses through.
%include "apple2/a2cpu.inc"         ; the 6502 core - _a2_run(cycles). Wave 1
                                    ; is the register file, the scratch, the
                                    ; memory hooks and the entry/exit shell
%include "apple2/a2mem.inc"         ; the RAM/ROM claim accessors and movers
                                    ; (APPLE2-SPEC section 3.4)
%define A2_SHIP 1                   ; ...and a2band.inc's `a2_rowsig` is NOT in
                                    ; it. The shift test compares forty source
                                    ; bytes now (APPLE2-SPEC 7.7 step 2) and
                                    ; nothing calls the signature; nasm has no
                                    ; dead-code elimination, so this define is
                                    ; what keeps ~34 unreachable bytes out of
                                    ; the resident image. The two harnesses
                                    ; that %include a2band.inc themselves -
                                    ; tests/a2band/a2bandbench.asm and
                                    ; hosttest/a2memtest.asm - do not define
                                    ; it, and keep the routine as their subject
%include "apple2/a2band.inc"        ; the 1bpp composers and the span compare
                                    ; (APPLE2-SPEC 7.3)
%include "apple2/a2nib.inc"         ; the 6-and-2 encoder - a STUB until the
                                    ; Disk II follow-up PR (section 14)
%include "apple2/a2fsx.inc"         ; THE FOREIGN-MODE RASTER WRITERS (section
                                    ; 13): a2_fsx_row's three-mode scan-line
                                    ; composer over a CELL RANGE - a masked
                                    ; glyph row, MII's lo-res CLUT and its
                                    ; artifact rule flattened into a 128-entry
                                    ; table by a2_fsx_init - a2_fsx_put's span
                                    ; compare one geometry along, the polled
                                    ; int 16h the bracket's whole input path
                                    ; is, and a2_fsx_dac's palette load

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end
