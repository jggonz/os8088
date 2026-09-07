; =============================================================================
; os8088 - apps/infones/infones.asm
;
; The assembly shim of INFONES, a Nintendo Entertainment System emulator
; written in C (SPEC.md 91). It is the top-level nasm source: the .c is never
; assembled on its own, because `nasm -f bin` has no notion of an external
; symbol, so the compiled C, the runtime, the 32-byte header and the
; hand-written includes are ONE assembly (SPEC.md 73.1).
;
; -----------------------------------------------------------------------------
; Derived from InfoNES (https://github.com/jay-kumogata/InfoNES) at commit
; fe3295c0a86bf5bbecc22aeab3b3af11f6f47908, Copyright (c) Jay Kumogata /
; Jay's Factory, licensed under the Apache License, Version 2.0. See
; apps/infones/LICENSE.TXT for the licence text.
;
; Section 4(b) modification notice: derived from InfoNES fe3295c0,
; restructured for 8086 real mode.
;
; Nothing of InfoNES's SOURCE is vendored (CONTRIBUTING.md 6): what is carried
; is behaviour, tables and strings, and SPEC.md 91.2 names the InfoNES file
; every user-visible surface came from. THIS FILE carries no InfoNES material
; at all - it is os8088's own package shape.
; -----------------------------------------------------------------------------
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

; THE TWO KNOBS `make nisystest` TURNS, and nothing else in this file moves.
; That target compiles the SAME apps/infones/infones.c a second time with
; -DNITEST and assembles it through THIS shim, so what the whole-emulator gate
; runs is the shipping loader, the shipping core, the shipping PPU and the
; shipping composer inside the shipping bracket (SPEC.md 91.14.4). A second
; shim would be a second program, and a gate that tests a second program tests
; nothing.
%ifndef CC_PKG_NAME
%define CC_PKG_NAME 'INFONES'       ; <= 15 chars, and the stem of INFONES.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it
%endif

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *) - the
                                    ; panel's keys; the GAME's keys are read
                                    ; inside the bracket and never arrive here
                                    ; (SPEC.md 91.6.4)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *) - the
                                    ; three FLAT menus of SPEC.md 91.8,
                                    ; AM_NAME 'InfoNES'
%define CC_HAS_ABOUT                ; void os88_about(void *) - the kernel's
                                    ; name pull-down and Help > About InfoNES
                                    ; open the same panel (SPEC.md 91.7.3)
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Open ROM
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - the ONE
                                    ; callback dispatched WITHOUT the gfx lock
                                    ; (SPEC.md 74.1), and therefore the first
                                    ; place an ovl_* may be reached: a ROM load
                                    ; is a floppy walk and it happens HERE, not
                                    ; in os88_main (SPEC.md 91.13.1)
%define CC_HAS_OVL                  ; INFONES.OVL (SPEC.md 73.14) - ON FROM THE
                                    ; FIRST COMMIT, with the split ALREADY MADE
                                    ; (C64-SPEC 13.1's rule): nirom.c, nicmd.c
                                    ; and niabout.c are out. The alternative is
                                    ; discovering at 55,000 bytes that the code
                                    ; is not the KIND that can move
                                    ;
                                    ; NO CC_HAS_WORKER. The emulated machine
                                    ; runs only inside SPEC.md 53's bracket and
                                    ; File > Exit is os88_wm_close (SPEC.md
                                    ; 75.2), so 20.6's worker rules never come
                                    ; into play - and no CC_STACK_CLASS either,
                                    ; because that byte sizes the WORKER's
                                    ; stack (SPEC.md 8.7) and there is no
                                    ; worker to size. What this package's stack
                                    ; budget IS, and where it is spent, is
                                    ; SPEC.md 91.4.4's table
%define CC_ICON  "infones/icon.inc"   ; the 16x16 controller, drawn for this
                                      ; port
%define CC_ASSOC "infones/niassoc.inc" ; ...and `.NES` claimed at BUILD time,
                                      ; so a ROM beside the program opens on
                                      ; the FIRST double-click with no prior
                                      ; run (SPEC.md 54.6). os88_assoc_set()
                                      ; is the runtime half and infones.c
                                      ; calls it too: they are not
                                      ; alternatives

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, the
                                    ; overlay runtime and the whole API bridge

%ifdef CC_GEN                       ; the compiled C, found through -I build/
%include CC_GEN                     ; ...which `make nisystest` renames, and
%else                               ; which is otherwise spelled OUT rather
%include "infones.gen.asm"          ; than through the %define: tests/unit/
%endif                              ; t_asmrules.py's `cpu 8086` gate finds a
                                    ; C package's root by the LITERAL
                                    ; `%include "<name>.gen.asm"` - that being
                                    ; how it knows tools/cc8086.py is the 8086
                                    ; constraint here rather than a `cpu`
                                    ; directive - and a root it cannot detect
                                    ; is a root it does not check, which is
                                    ; the one failure a gate may not have

; The hand-written half (SPEC.md 73.11's rule that the inner loop is
; assembly). Each is %included AFTER the compiled C, because each defines
; labels the C calls and nasm resolves them in one pass over the whole file.
%include "infones/nicpu.inc"        ; the 2A03 core (SPEC.md 91.4)
%include "infones/nimem.inc"        ; the cross-segment movers. ES IS LOADED IN
                                    ; THESE FOUR FILES AND NOWHERE ELSE, never
                                    ; in compiled C (SPEC.md 73.5); 91.13.1's
                                    ; table says which points it at what
%include "infones/niband.inc"       ; the scanline composer and the tile-cache
                                    ; decoder (SPEC.md 91.5)
%include "infones/nifsx.inc"        ; SPEC.md 53's exclusive bracket, which is
                                    ; deliberately not wrapped for C

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
