; =============================================================================
; os8088 - apps/pagedesk/pdrx8h.inc      PDREND: the far call into PDX8
;
; %included by pdrend.asm under PDR_X8 (PAGEDESK-SPEC 20.22.10.11). PDX8 is the
; raster compositor, a part loaded into the X8 region at its base; its door is
; at x8_seg:8 and its code selector is the word plat binds at x8_seg:10.
;
;   pdr_x8bind(seg)               take the vector: 8 : [seg:10]
;   pdr_x8far(verb, bx, cx, dx)   AX back whatever the carry; CF, CX and DX of
;                                 the answer land in x8_cf, x8_ocx and x8_odx
; =============================================================================
section .bss
x8_vec:   resw 2                    ; offset, selector
_x8_cf:   resw 1
_x8_ocx:  resw 1
_x8_odx:  resw 1
section .text

_pdr_x8bind:
    push bp
    mov bp, sp
    push es
    mov es, [bp+4]
    mov ax, [es:10]
    mov [x8_vec+2], ax
    mov word [x8_vec], 8
    pop es
    pop bp
    ret

_pdr_x8far:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov bx, [bp+6]
    mov cx, [bp+8]
    mov dx, [bp+10]
    push ds
    call far [x8_vec]
    pop ds                          ; pop leaves the flags
    mov [_x8_ocx], cx
    mov [_x8_odx], dx
    sbb cx, cx                      ; CF -> 0 / FFFF
    neg cx
    mov [_x8_cf], cx
    pop bp
    ret
