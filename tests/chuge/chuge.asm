; =============================================================================
; os8088 - tests/chuge/chuge.asm
;
; CHUGE's shim (tests/chuge/chuge.c says what it tests): the runtime, the
; compiled C, and the one routine C cannot write - a line out of port E9, which
; Bochs prints and every other machine ignores.
; =============================================================================
%define CC_PKG_NAME 'CHUGE'         ; <= 15 chars
%define CC_STACK_CLASS OS88_HIGH | OS88_HIGHMEM ; its claims may be above 1MB:
                                    ; that is the subject
%include "cc/crt0.asm"
%include "chuge.gen.asm"            ; the compiled C, found through -I build/

section .text
; void chuge_e9(const char *s) - the NUL-ended line at DS:s, then a newline
_chuge_e9:
    mov bx, sp
    mov si, [ss:bx+2]
.l:
    lodsb
    or al, al
    jz .n
    out 0xE9, al
    jmp short .l
.n:
    mov al, 10
    out 0xE9, al
    ret

    CC_IMAGE_END
