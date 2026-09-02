; =============================================================================
; os8088 - apps/c64/c64.asm
;
; The assembly shim of C64, a Commodore 64 written in C - a reimplementation
; of VICE 3.10's `x64` (docs/C64-SPEC.md). It is the top-level nasm source:
; the .c is never assembled on its own, because `nasm -f bin` has no notion of
; an external symbol, so the compiled C, the runtime, the 32-byte header and
; the hand-written includes are ONE assembly (SPEC.md 73.1).
;
; Derived from VICE (https://vice-emu.sourceforge.io/), Copyright (C)
; 1996-2025 the VICE team, GPL-2-or-later. apps/c64/ is GPL-2-or-later; the
; rest of this tree is not. See apps/c64/COPYING for the licence text.
; Nothing of VICE's SOURCE is vendored - what is carried is behaviour, tables
; and strings, each naming the VICE file it came from (C64-SPEC §2).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'C64'           ; <= 15 chars; the Disk window's label, and
                                    ; the stem of C64.OVL (SPEC.md 73.14)

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *) - the
                                    ; level keyboard's make half (C64-SPEC 7.2)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *) - a
                                    ; click KICKS the slice driver, so a wake
                                    ; the event ring refused cannot park a
                                    ; running machine (SPEC.md 74.1)
%define CC_HAS_ABOUT                ; void os88_about(void *) - the kernel's
                                    ; name pull-down opens the same panel as
                                    ; Help > About VICE... (C64-SPEC 12)
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - THE slice
                                    ; driver's entry (SPEC.md 74.1): the one
                                    ; callback dispatched WITHOUT the gfx lock
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - the
                                    ; five menus of C64-SPEC 11.1, AM_NAME
                                    ; 'VICE'
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Smart
                                    ; attach... on .PRG (C64-SPEC 11.3)
                                    ; NO CC_HAS_WORKER. File > Exit emulator
                                    ; used to hire one - wm_destroy under the
                                    ; lock, then task_alive - and that closes
                                    ; the WINDOW without closing the APP: the
                                    ; dock tile stayed, dead, and answered no
                                    ; click. OSAPI_WM_CLOSE (SPEC.md 75.2) is
                                    ; the kernel's own close path and needs no
                                    ; task at all (c64.c's os88_onwake)
%define CC_HAS_OVL                  ; C64.OVL (SPEC.md 73.14) - ON FROM THE
                                    ; FIRST COMMIT (C64-SPEC 13.1). The
                                    ; alternative is discovering at 55,000
                                    ; that the code is not the kind that can
                                    ; move (LESSONS.md 5). The split is by
                                    ; FREQUENCY: the menu command bodies
                                    ; (c64cmd.c), the Smart-attach body
                                    ; (c64load.c) and the About panel
                                    ; (c64about.c) are out; every callback,
                                    ; the core, the composer and the flush
                                    ; are in
%define CC_ICON "c64/icon.inc"      ; the 16x16 breadbin, drawn for this port

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge

%include "c64.gen.asm"              ; the compiled C, found through -I build/

; The hand-written half (SPEC.md 73.11's rule that the inner loop is assembly):
%include "c64/c64cpu.inc"           ; the 6510 core - _c64_run(cycles). Wave 1
                                    ; is the register file, the 256-entry
                                    ; dispatch table and the entry/exit shell
%include "c64/c64mem.inc"           ; the RAM/ROM claim accessors and movers,
                                    ; and the core's scratch (C64-SPEC 3.5/3.6)
%include "c64/c64band.inc"          ; the 1bpp composers, the span compare and
                                    ; the row signature (C64-SPEC 9.5)

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end
