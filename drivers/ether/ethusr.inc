; =============================================================================
; os8088 - drivers/ether/ethusr.inc
;
; THE CALLER'S SEGMENT, and the only two places it is addressed.
;
; Cut out of ether.asm so the DOS end of the parallel cable can host the same
; socket verbs (ethsock.inc's header). There [eth_useg] is simply DS - the
; .COM's one segment - so the same two routines serve a caller that is a
; package in another segment and a caller that is the program itself.
; =============================================================================
; -----------------------------------------------------------------------------
; usr_read / usr_write - the caller's segment, and the only two places it is
; in: usr_read  SI = their offset, DI = ours, CX = bytes
;     usr_write SI = ours, DI = their offset, CX = bytes
; -----------------------------------------------------------------------------
usr_read:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [eth_useg]
    jcxz .out
.l:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .l
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

usr_write:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [eth_useg]
    jcxz .out
.l:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .l
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
