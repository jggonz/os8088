; =============================================================================
; hfdiag.inc - HFDIAG=1's read log: a DEVELOPER build only, never shipped
;
; The last four FSV_READ/FSV_READAT calls, one 24-byte record each, written
; at hf_rdout - the exit both share - and handed to the page by HFV_DIAG,
; which draws them where the status lines were. For a server we cannot reach
; from here: one photograph of the page says what each read asked, what the
; reply said and what was delivered.
; =============================================================================

HFD_N       equ 4
HFD_REC     equ 24

; --- hf_dgin - AX = the handle, DX:BX = the buffer. keeps every register ---
hf_dgin:
    mov [hf_dgh], ax
    mov [hf_dgs], dx
    mov [hf_dgo], bx
    mov word [hf_err], 0
    mov word [hf_status], 0
    mov byte [hf_dgr], 0
    push ax
    call OSAPI_GET_TICKS
    mov [hf_dgt], ax
    pop ax
    ret

; --- hf_dgout - the record. keeps every register and the flags ------------
hf_dgout:
    pushf
    push ax
    push bx
    push si
    push di
    push es
    push ds
    pop es
    mov di, [hf_dgcnt]
    inc word [hf_dgcnt]
    and di, HFD_N - 1
    mov bl, HFD_REC
    xchg ax, di                 ; AX = the slot, DI = the result
    mul bl
    xchg ax, di                 ; DI = the slot's offset, AX = the result
    add di, hf_dglog
    mov si, sp
    mov bl, [ss:si+10]          ; the pushed flags' low byte: CF is bit 0
    and bl, 1
    test byte [hf_kept], 0xFF
    jz .k1
    or bl, 2
.k1:
    test byte [hf_keep], 0xFF
    jz .k2
    or bl, 4
.k2:
    test byte [hf_hasl], 0xFF
    jz .k3
    or bl, 8
.k3:
    test byte [hf_dgr], 0xFF
    jz .k5
    or bl, 32                   ; a kept connection died and was retried
.k5:
    mov bh, bl
    mov bl, [hf_dgk]
    push ax
    mov ax, bx
    stosw                       ; +0 kind, +1 flags
    mov ax, [hf_dgh]
    stosw                       ; +2 the handle
    mov ax, [hf_rlo]
    stosw                       ; +4 the offset asked
    mov ax, [hf_rn]
    stosw                       ; +6 the count asked
    mov ax, [hf_status]
    stosw                       ; +8 the reply's status
    mov ax, [hf_left]
    stosw                       ; +10 body bytes NOT delivered
    call OSAPI_GET_TICKS
    sub ax, [hf_dgt]
    stosw                       ; +12 ticks the read took
    pop ax
    stosw                       ; +14 the result AX
    mov ax, [hf_err]
    stosw                       ; +16 the HFE_*
    mov ax, [hf_rlen]
    stosw                       ; +18 bytes staged by the last receive
    push ds
    mov si, [hf_dgo]
    mov ds, [hf_dgs]
    movsw                       ; +20 the first four bytes delivered
    movsw
    pop ds
    pop es
    pop di
    pop si
    pop bx
    pop ax
    popf
    ret

; --- HFV_DIAG - ES:DI = HFD_N * HFD_REC bytes -> CX = reads logged --------
hf_pdiag:
    push si
    push di
    mov si, hf_dglog
    mov cx, HFD_N * HFD_REC / 2
    rep movsw
    pop di
    pop si
    mov cx, [hf_dgcnt]
    clc
    ret

hf_dgk:     db 0
hf_dgr:     db 0
hf_dgt:     dw 0
hf_dgh:     dw 0
hf_dgs:     dw 0
hf_dgo:     dw 0
hf_dgcnt:   dw 0
hf_dglog:   times HFD_N * HFD_REC db 0
