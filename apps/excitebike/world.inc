; World: the track streams expanded into the column-id array (SPEC.md 102.3).
; A race is the runway (EXB_RUNWAY plain columns) then lap 1 then lap 2, one
; byte a column, so the terrain under a bike is two loads (cid[x], then the
; band dictionary's collision row) - no ring buffer, no per-column generator.
; The streams are (column id, run length) pairs ending in a zero run; the
; compiler has already refused a lap over 797 columns and this refuses too,
; because a byte read from a table is still checked before it is stored.

; AL = track index (0-based).  Fills xb_cid, xb_ncols, and sets xb_theme from
; the track.  CF=1 if a stream would overrun the array (nothing past it is
; written).
xb_track_load:
    SAVE
    push ds
    pop es
    mov [xb_track], al
    call xb_header                  ; BX = the track header
    mov al, [bx]
    mov [xb_theme], al
    mov di, xb_cid
    mov cx, EXB_RUNWAY
    mov al, EXBCOL_PLAIN_A
.runway:
    stosb
    xor al, EXBCOL_PLAIN_A ^ EXBCOL_PLAIN_B
    loop .runway
    mov si, [bx+8]                  ; lap 1 stream (the second pass: lap 2's, on both laps)
    cmp byte [xb_flag], 0
    je .l1
    mov si, [bx+10]
.l1:
    call xb_expand
    jc .out
    mov si, [bx+10]                 ; lap 2 stream
    call xb_expand
.out:
    pushf
    sub di, xb_cid
    mov [xb_ncols], di
    popf
    RESTORE
    ret

; DS:SI = a stream, ES:DI = the next cid slot.  CF=1 on overrun.
xb_expand:
    cld
.run:
    lodsb
    mov ah, al                      ; the column id
    lodsb
    xor cx, cx
    mov cl, al
    or cl, cl
    jz .done
    mov dx, di
    add dx, cx
    cmp dx, xb_cid + XB_CID_MAX
    ja .over
    mov al, ah
    rep stosb
    jmp .run
.done:
    clc
    ret
.over:
    stc
    ret
