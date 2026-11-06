; =============================================================================
; os8088 - apps/pagedesk/pdeflate.inc                PDENG's FlateDecode, the hot loop
;
; _fl_codes: the symbol loop of a Huffman block (PAGEDESK-SPEC 20.13), in 8086
; assembly, included by pdeng.asm after the compiled C. Its C twin is the
; `default:` case and fl_match() of pdeflate.c's fl_fill(), which the host
; harness runs and which the target builds under -DFL_NOASM; hosttest/
; pde_fl_boot.py runs both on the target and compares them.
;
; It works on the loaded decoder `_fl` (struct flst, pdeflate.h) in place, and
; reads the Huffman tables straight out of the slot's segment (ES). Registers
; while it runs: DS = CS = the module, ES = the slot, DX = the bit buffer,
; CH = the bits in it, SI = the read-ahead's next byte, DI = the output
; buffer's next free byte; AX, BX, CL scratch. It returns AX = 0 when the
; output buffer is full or the block ended (fl.state says which), 1 when the
; input ended, 2 when the data is corrupt.
;
; The FL_* offsets are struct flst's and the FT_* / PDE_FLTAB constants are
; pdeflate.h's, checked by hosttest/pde_fl_check.py (offsets).
; =============================================================================
FL_SEG      equ 0
FL_STATE    equ 6
FL_FINAL    equ 8
FL_BB       equ 14
FL_BN       equ 16
FL_MLEFT    equ 20
FL_MDIST    equ 22
FL_WPOS     equ 24
FL_WHAVE    equ 26
FL_ON       equ 28
FL_II       equ 42
FL_IN       equ 44
FL_OB       equ 56
FL_IB       equ 384
FL_OBSZ     equ 256
FL_TABLUT   equ 41664               ; PDE_FLTAB + FT_LUT
FL_TABDLUT  equ 42176               ; PDE_FLTAB + FT_DLUT
FL_S_BLK    equ 1
FL_S_END    equ 4

section .bss
flc_lim:    resw 1                  ; the read-ahead's end (address)
section .text

_fl_codes:
    push si
    push di
    push es
    cld
    mov es, [_fl + FL_SEG]
    mov dx, [_fl + FL_BB]
    mov ch, [_fl + FL_BN]
    mov si, [_fl + FL_II]
    add si, _fl + FL_IB
    mov ax, [_fl + FL_IN]
    add ax, _fl + FL_IB
    mov [flc_lim], ax
    mov di, [_fl + FL_ON]
    add di, _fl + FL_OB
.top:
    cmp di, _fl + FL_OB + FL_OBSZ
    jae .full
    cmp word [_fl + FL_MLEFT], 0
    jne .match
    cmp ch, 8
    jae .have8
    call flc_n8
.have8:
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [es:bx + FL_TABLUT]
    or ax, ax
    jz .lslow
    mov cl, ah                      ; the entry's length: e >> 9
    shr cl, 1
    cmp cl, ch
    ja .eof                         ; a code longer than the bits left
    shr dx, cl
    sub ch, cl
    and ah, 1
    jnz .lenorend
    mov [di], al                    ; a literal
    inc di
    jmp .top
.lslow:
    xor bx, bx
    call flc_slow
    cmp ax, -1
    je .eof
    or ax, ax
    js .bad
    cmp ax, 256
    jae .lenorend
    mov [di], al
    inc di
    jmp .top
.lenorend:                          ; AX = 256..287
    cmp ax, 256
    je .eob
    cmp ax, 285
    ja .bad
    sub ax, 257
    mov bx, ax
    mov cl, [_fl_lext + bx]
    shl bx, 1
    mov ax, [_fl_lbase + bx]
    or cl, cl
    jz .lend
    push ax
    mov bl, cl
    xor bh, bh
    call flc_bits
    jc .eofp
    pop bx
    add ax, bx
.lend:
    mov [_fl + FL_MLEFT], ax
    cmp ch, 8
    jae .dhave8
    call flc_n8
.dhave8:
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [es:bx + FL_TABDLUT]
    or ax, ax
    jz .dslow
    mov cl, ah
    shr cl, 1
    cmp cl, ch
    ja .eof
    shr dx, cl
    sub ch, cl
    and ah, 1
.dsym:                              ; AX = the distance symbol
    cmp ax, 30
    jae .bad
    mov bx, ax
    mov cl, [_fl_dext + bx]
    shl bx, 1
    mov ax, [_fl_dbase + bx]
    or cl, cl
    jz .dend
    push ax
    mov bl, cl
    xor bh, bh
    call flc_bits
    jc .eofp
    pop bx
    add ax, bx
.dend:
    mov [_fl + FL_MDIST], ax
    jmp .top
.dslow:
    mov bx, 1
    call flc_slow
    cmp ax, -1
    je .eof
    or ax, ax
    js .bad
    jmp .dsym
.match:
    call flc_match
    jnc .top
.bad:
    mov ax, 2
    jmp .exit
.eofp:
    pop ax
.eof:
    mov ax, 1
    jmp .exit
.eob:                               ; the block's end: the next block, or the end
    mov ax, FL_S_BLK
    cmp word [_fl + FL_FINAL], 0
    je .setst
    mov ax, FL_S_END
.setst:
    mov [_fl + FL_STATE], ax
.full:
    xor ax, ax
.exit:
    mov [_fl + FL_BB], dx
    mov [_fl + FL_BN], ch
    mov byte [_fl + FL_BN + 1], 0
    sub si, _fl + FL_IB
    mov [_fl + FL_II], si
    sub di, _fl + FL_OB
    mov [_fl + FL_ON], di
    pop es
    pop di
    pop si
    ret

; flc_byte - one more input byte into the bit buffer (CH < 8 on entry).
; CF = 1 when the input has ended.
flc_byte:
    cmp si, [flc_lim]
    jb .have
    call flc_fill
    jc .end
.have:
    lodsb
    xor ah, ah
    mov cl, ch
    shl ax, cl
    or dx, ax
    add ch, 8
    clc
.end:
    ret

; flc_n8 - the bit buffer to 8 bits or more where the input allows
flc_n8:
    call flc_byte
    jc .end
    cmp ch, 8
    jb flc_n8
.end:
    ret

; flc_get - BL (1..8) bits into AX. CF = 1 when the input has ended.
flc_get:
    cmp ch, bl
    jae .ok
    call flc_byte
    jnc flc_get
    ret
.ok:
    mov cl, bl
    mov ax, 1
    shl ax, cl
    dec ax
    and ax, dx
    shr dx, cl
    sub ch, cl
    clc
    ret

; flc_bits - BL (1..13) bits into AX; CF = 1 when the input has ended
flc_bits:
    cmp bl, 8
    ja .two
    call flc_get
    ret
.two:
    push bx
    mov bl, 8
    call flc_get
    pop bx
    jc .end
    push ax
    sub bl, 8
    call flc_get
    pop bx
    jc .end
    mov ah, al
    mov al, bl
    clc
.end:
    ret

; flc_fill - the read-ahead is spent: C refills it. CF = 1 at the end of input
flc_fill:
    push bx
    push cx
    push dx
    push di
    push es
    sub si, _fl + FL_IB
    mov [_fl + FL_II], si
    call _fl_infill
    mov si, _fl + FL_IB
    mov bx, [_fl + FL_IN]
    add bx, si
    mov [flc_lim], bx
    or ax, ax
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    jnz .some
    stc
    ret
.some:
    clc
    ret

; flc_slow - BX = 0 literal/length, 1 distance: the symbol of a code longer
; than the lookup, by C's walk, which works on the saved bit state. AX = the
; symbol, -1 input ended, -2 no such code
flc_slow:
    mov [_fl + FL_BB], dx
    mov [_fl + FL_BN], ch
    mov byte [_fl + FL_BN + 1], 0
    sub si, _fl + FL_IB
    mov [_fl + FL_II], si
    push di
    push es
    push bx
    call _fl_slow
    add sp, 2
    pop es
    pop di
    mov dx, [_fl + FL_BB]
    mov ch, [_fl + FL_BN]
    mov si, [_fl + FL_II]
    add si, _fl + FL_IB
    mov bx, [_fl + FL_IN]
    add bx, _fl + FL_IB
    mov [flc_lim], bx
    ret

; flc_w2o - CX bytes from ES:SI (the slot) to DS:DI (the output buffer)
flc_w2o:
    jcxz .end
    push ds
    push es
    pop ds
    push cs
    pop es
    rep movsb
    pop ds
    mov es, [_fl + FL_SEG]
.end:
    ret

; flc_match - as much of the pending match as the output buffer has room for
; (pdeflate.c's fl_match). CF = 1: it reaches before the stream's first byte.
flc_match:
    push cx
    push dx
    push si
    mov bx, _fl + FL_OB + FL_OBSZ
    sub bx, di
    mov cx, [_fl + FL_MLEFT]
    cmp cx, bx
    jbe .n
    mov cx, bx
.n:                                 ; CX = how many bytes this call copies
    mov dx, [_fl + FL_MDIST]
    mov bx, di
    sub bx, _fl + FL_OB             ; BX = bytes already in the buffer
    cmp dx, bx
    jbe .near
    sub dx, bx                      ; DX = how far before this buffer the source starts
    cmp dx, [_fl + FL_WHAVE]
    ja .bad
    mov si, [_fl + FL_WPOS]
    sub si, dx
    and si, 0x7FFF
    cmp dx, cx
    jbe .k
    mov dx, cx
.k:                                 ; DX = bytes taken from the history
    sub cx, dx
    sub [_fl + FL_MLEFT], dx
    push cx                         ; what is left for the near copy
    mov cx, dx
    mov bx, 0x8000
    sub bx, si
    cmp bx, cx
    jb .tk
    mov bx, cx
.tk:
    sub cx, bx
    push cx                         ; the part after the ring's end
    mov cx, bx
    call flc_w2o
    pop cx
    xor si, si
    call flc_w2o
    pop cx
    mov dx, [_fl + FL_MDIST]
.near:                              ; CX bytes from DI - DX, which may overlap
    sub [_fl + FL_MLEFT], cx
    jcxz .done
    mov si, di
    sub si, dx
    push cs
    pop es
    rep movsb
    mov es, [_fl + FL_SEG]
.done:
    clc
    jmp .out
.bad:
    stc
.out:
    pop si
    pop dx
    pop cx
    ret
