; =============================================================================
; os8088 - apps/cword/cword.asm
;
; The assembly shim of CWORD, the C toolchain's first application (SPEC.md 67,
; 67.12). It is the top-level nasm source: the .c files are never assembled on
; their own, because `nasm -f bin` has no notion of an external symbol, so the
; compiled C, the runtime and the 32-byte header are one assembly (67.1).
;
; A shim does four things - name the package, declare which callbacks the C
; defines, include the runtime and then the compiled C in that order, and close
; the image - and apps/cc/ccsmoke.asm is the template it copies. This one does
; a FIFTH thing, and it is the only C package in the tree that does:
;
;   IT CARRIES ONE HAND-WRITTEN ROUTINE, cw_memmove.
;
; SPEC.md 67.11 is explicit that a C package composes and decides while
; anything that touches bytes per iteration is a hand-written proc that C calls
; once, and an editor's insert is exactly that: every keystroke moves the tail
; of two arrays by one byte. There are two reasons it cannot be C here.
;
;   1. THE RUNTIME'S os88_memcpy() CANNOT DO IT. It is an ascending byte loop
;      (apps/cc/os88thunk.asm), and an insertion copies UP - dst above src, the
;      ranges overlapping - so an ascending loop smears the first byte over the
;      whole tail. This is a memMOVE and the runtime has none.
;   2. C IS THE WRONG PLACE FOR THE LOOP. A descending byte loop written in C
;      costs 3-5x hand assembly (67.11), and this one runs on the keystroke
;      path with up to 4,000 bytes to move.
;
; It is also the only routine in the package that touches ES, and that is the
; other half of why it lives here rather than in the C: ES is KERNEL_SEG on
; entry to every callback (SPEC.md 20.4), a `rep movsb` writes through it, and
; tools/cc8086.py therefore REFUSES every string instruction in compiled C by
; name (67.5.1). Loading ES on purpose, in assembly, with a stated reason and a
; restore, is what that refusal leaves room for - it is the same idea as the
; `; cc8086:allow` escape, taken at the only place in the program that needs it.
; =============================================================================

%define CC_PKG_NAME 'CWORD'         ; <= 15 chars (the field is 16, NUL-padded)
                                    ; and deliberately NOT 'WORD', which is
                                    ; SPEC.md 65's assembly port (67.12)

%define CC_HAS_ONKEY                ; void os88_onkey(int ascii, int scan, void *win)
%define CC_HAS_ONCLICK              ; void os88_onclick(int x, int y, void *win)
%define CC_HAS_MENUS                ; void os88_oncmd(int item, int menu, void *win)
%define CC_HAS_ABOUT                ; void os88_about(void *win)
%define CC_HAS_FDLG                 ; void os88_onfile(int mode, const char *name,
                                    ;                  unsigned lo, unsigned hi,
                                    ;                  void *win)
                                    ; and NOT: CC_HAS_ONMOUSEUP, CC_HAS_ONRESIZE,
                                    ; CC_HAS_WORKER - cword has no drag, is not
                                    ; sizable, and does nothing in the
                                    ; background, so none of those trampolines
                                    ; is assembled at all

%include "cc/crt0.asm"              ; sections, the 32-byte header, the entry
                                    ; and callback trampolines, and the whole
                                    ; API bridge (it includes os88thunk.asm)

%include "cword/cwmove.inc"         ; cw_memmove. It is in its own file, and
                                    ; not written out here, so that the boot
                                    ; test that exercises it assembles the SAME
                                    ; TEXT the package does rather than a copy
                                    ; of it that can drift

%include "cword.gen.asm"            ; the compiled C: smlrcc -S, then
                                    ; tools/cc8086.py, found through -I build/

    CC_IMAGE_END                    ; cc_bss_end and cc_image_end - the two
                                    ; forward references the header made
