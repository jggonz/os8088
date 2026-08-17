; =============================================================================
; os8088 - apps/cc/ccsmoke.asm
;
; The assembly shim of the C toolchain's smoke test (SPEC.md 70), and the
; template every C package copies. It is the top-level nasm source: the .c
; file is never assembled on its own, because `nasm -f bin` has no notion of
; an external symbol and the C, the runtime and the header therefore have to
; be one assembly (SPEC.md 70.1).
;
; A shim does exactly four things and nothing else belongs in it:
;
;   1. names the package, which is what the Disk window labels the icon with
;      and what the instance table shows;
;   2. declares which callbacks the C actually defines, one %define each. A
;      %define with no C function behind it is an nasm external-reference
;      error naming that function; a C function with no %define is code the
;      kernel never calls. The two halves cannot drift silently;
;   3. includes the runtime and then the compiled C, in that order - the
;      header has to be at file offset 0;
;   4. closes the image.
;
; Anything else - the window, the layout, the data, the logic - is in the C.
; =============================================================================

%define CC_PKG_NAME 'CCSMOKE'       ; <= 15 chars (the field is 16, NUL-padded)

%define CC_HAS_ONKEY                ; void os88_onkey(int ascii, int scan, void *win)
%define CC_HAS_ONCLICK              ; void os88_onclick(int x, int y, void *win)
%define CC_HAS_MENUS                ; void os88_oncmd(int item, int menu, void *win)
%define CC_HAS_ABOUT                ; void os88_about(void *win)
                                    ; and NOT: CC_HAS_ONMOUSEUP, CC_HAS_ONRESIZE,
                                    ; CC_HAS_FDLG, CC_HAS_WORKER - this program
                                    ; has none of those, so neither their
                                    ; trampolines nor the thunks that install
                                    ; them are assembled at all

%include "cc/crt0.asm"              ; sections, the 32-byte header, the entry
                                    ; and callback trampolines, and the whole
                                    ; API bridge (it includes os88thunk.asm)

%include "ccsmoke.gen.asm"          ; the compiled C: smlrcc -S then
                                    ; tools/cc8086.py, found through -I build/

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end - the two
                                    ; forward references the header made
