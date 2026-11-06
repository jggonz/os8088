; =============================================================================
; os8088 - apps/pagedesk/stubs/pdmstub.inc   STAND-IN for pdmath.inc
;
; The THREE pdmath.h routines the primary calls in wave 1, under pdmath.h's
; names, so the package builds before the math agent's pdmath.inc lands
; (pagedesk.asm includes this under -DPD_MATH_STUB and pdmath.inc otherwise -
; the Makefile's PD_MATH_STUB flips it). It is a stand-in, not a second
; implementation: the view size here divides in two steps where pdmath's
; floors once, which can differ by a pixel, and that is acceptable for a
; picture of a stub page and for nothing else.
; =============================================================================

section .text

; pdw pd_fcopy_in(pdw seg, pdw off, void *dst, pdw n)
_pd_fcopy_in:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push ds
    mov cx, [bp+10]
    mov ax, [bp+6]
    add ax, cx
    jc .no
    push ds
    pop es
    mov di, [bp+8]
    mov si, [bp+6]
    mov ds, [bp+4]
    cld
    rep movsb
    mov ax, [bp+10]
    jmp short .out
.no:
    xor ax, ax
.out:
    pop ds
    pop es
    pop di
    pop si
    pop bp
    ret

; pdw pd_fcopy_out(const void *src, pdw seg, pdw off, pdw n)
_pd_fcopy_out:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov cx, [bp+10]
    mov ax, [bp+8]
    add ax, cx
    jc .no
    mov es, [bp+6]
    mov di, [bp+8]
    mov si, [bp+4]
    cld
    rep movsb
    mov ax, [bp+10]
    jmp short .out
.no:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; void pdm_view_size(pd_px *w, pd_px *h, const struct pd_view *v)
; W = x1-x0, H = y1-y0 (pt16), swapped for 90/270; w = W*zoom/1600,
; h = (H*zoom/1600)*asp_num/asp_den.
_pdm_view_size:
    push bp
    mov bp, sp
    push si
    mov si, [bp+8]
    mov ax, [si+4]
    sub ax, [si+0]
    mov bx, [si+6]
    sub bx, [si+2]
    cmp word [si+8], 90
    je .swap
    cmp word [si+8], 270
    jne .noswap
.swap:
    xchg ax, bx
.noswap:
    push bx
    mul word [si+10]
    mov cx, 1600
    div cx
    mov bx, [bp+4]
    mov [bx], ax
    pop ax
    mul word [si+10]
    div cx
    mul word [si+12]
    div word [si+14]
    mov bx, [bp+6]
    mov [bx], ax
    pop si
    pop bp
    ret
