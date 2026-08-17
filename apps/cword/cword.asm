; =============================================================================
; os8088 - apps/cword/cword.asm
;
; The assembly shim of CWORD, Microsoft Word 1.1a written in C (SPEC.md 67.12).
; It is the top-level nasm source: the .c is never assembled on its own,
; because `nasm -f bin` has no notion of an external symbol, so the compiled C,
; the runtime and the 32-byte header are ONE assembly (SPEC.md 67.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm error naming that function, and a C function
; with no %define is code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'CWORD'         ; <= 15 chars, and the stem of CWORD.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_ONMOUSEUP            ; void os88_onmouseup(int, int, void *)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_FDLG                 ; void os88_onfile(...)
%define CC_HAS_WORKER               ; void os88_worker(void *) - the close path
                                    ; (there is no self-close slot: SPEC.md
                                    ; 65.2, and apps/cword/cwcmd.c says how)
%define CC_HAS_OVL                  ; ...and half of this program is in
                                    ; CWORD.OVL (SPEC.md 67.14)

                                    ; and NOT CC_HAS_MENUS: this program's
                                    ; nine menus are drawn in its own window
                                    ; because MENU_APPMAX is five (SPEC.md
                                    ; 12.2, 65.2), so it registers no kernel
                                    ; menu set at all

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, the
                                    ; overlay runtime and the whole API bridge

%include "cword.gen.asm"            ; the compiled C, found through -I build/

; The one hand-written routine (SPEC.md 67.11's rule that the inner loop is
; assembly): a byte move that handles both directions. C cannot write one -
; an ascending loop corrupts an insertion by trailing its own output, and a
; descending loop in C costs 3-5x what `rep movsb` costs on the target. It is
; the only place in the package that loads ES, and it puts it back.
%include "cword/cwmove.inc"

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
