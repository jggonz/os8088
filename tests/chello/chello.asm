; =============================================================================
; os8088 - tests/chello/chello.asm
;
; The assembly shim of the C toolchain's capability gate (SPEC.md 73), and the
; second worked example of the two files a C package is made of - the first
; being apps/cc/ccsmoke.asm, which this is deliberately identical in shape to.
;
; It is the top-level nasm source: the .c is never assembled on its own,
; because `nasm -f bin` has no notion of an external symbol, so the compiled C,
; the runtime and the header are one assembly (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the 32-byte header has to be at file offset 0),
; and close the image.
;
; The two halves cannot drift silently: a %define with no C function behind it
; is an nasm error naming that function, and a C function with no %define is
; code the kernel never calls.
; =============================================================================

%define CC_PKG_NAME 'CHELLO'        ; <= 15 chars (the field is 16, NUL-padded)

%define CC_HAS_ONCLICK              ; void os88_onclick(int x, int y, void *win)
%define CC_HAS_ONKEY                ; void os88_onkey(int ascii, int scan, void *win)
                                    ; and NOT: CC_HAS_MENUS, CC_HAS_ABOUT,
                                    ; CC_HAS_ONMOUSEUP, CC_HAS_ONRESIZE,
                                    ; CC_HAS_FDLG, CC_HAS_WORKER - this program
                                    ; has none of those, so neither their
                                    ; trampolines nor the thunks that install
                                    ; them are assembled at all

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; entry and callback trampolines, and the
                                    ; whole API bridge (it includes
                                    ; os88thunk.asm)

%include "chello.gen.asm"           ; the compiled C: smlrcc -S, then
                                    ; tools/cc8086.py, found through -I build/

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end - the two
                                    ; forward references the header made
