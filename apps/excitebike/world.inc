; World: the track streams expanded into the column-id array (SPEC.md 102.3).
; A race is the runway (EXB_RUNWAY plain columns) then lap 1 then lap 2, one
; byte a column, so the terrain under a bike is two loads (cid[x], then the
; band dictionary's collision row) - no ring buffer, no per-column generator.
; A stream is runs: a column id byte (< 64) whose top two bits are the run when
; it is 1-3, else 00 and the run in the next byte; a zero run ends it.  The 00
; form is EXBTRACK.DAT's (column id, run length) pair, so the custom course's
; stream goes through the same decoder.  Ids 62 and 63 are not columns: pass 2's
; stream is an edit of pass 1's, and 63 COPIES the run from the source BP walks
; (lap 1's columns) where 62 SKIPS it - the compiler's pass2_bytes, and a custom
; stream cannot hold either (xb_custom_check refuses an id of EXB_NCOLS or more).  The compiler has already refused a lap
; over 797 columns and this refuses too, because a byte read from a table is
; still checked before it is stored.

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
    mov si, [bx+8]                  ; lap 1: pass 1's stream
    call xb_expand
    jc .out
    mov bp, xb_cid + EXB_RUNWAY     ; lap 2: pass 2, an EDIT of the columns just written
    mov si, [bx+10]
    mov bx, di                      ; (where lap 2 starts)
    call xb_expand
    jc .out
    cmp byte [xb_flag], 0           ; (CF = 0 either way)
    je .out
    ; the second pass rides pass 2 on BOTH laps: lap 2 moves down over lap 1 (DI < SI, so a
    ; forward copy) and is copied once more after itself
    mov cx, di
    sub cx, bx
    mov si, bx
    mov di, xb_cid + EXB_RUNWAY
    rep movsb
    mov cx, di
    mov si, xb_cid + EXB_RUNWAY
    sub cx, si                      ; (CF = 0: DI is past SI)
    rep movsb
.out:
    pushf
    sub di, xb_cid
    mov [xb_ncols], di
    popf
    RESTORE
    ret

; DS:SI = a stream, ES:DI = the next cid slot, BP = the copy tokens' source (ES = DS).
; CF=1 on overrun.
xb_expand:
    cld
.run:
    lodsb
    mov ah, al
    and ah, 3fh                     ; the column id
    xor cx, cx
    rol al, 1
    rol al, 1
    and al, 3                       ; a run of 1-3 in the id byte's top two bits...
    jnz .n
    lodsb                           ; ...or the next byte (0 ends the stream)
    or al, al
    jz .done
.n:
    mov cl, al
    cmp ah, XB_TOK_SKIP
    je .skip
    mov dx, di
    add dx, cx
    cmp dx, xb_cid + XB_CID_MAX
    ja .over
    cmp ah, XB_TOK_COPY
    je .copy
    mov al, ah
    rep stosb
    jmp .run
.copy:
    xchg si, bp
    rep movsb
    xchg si, bp
    jmp .run
.skip:
    add bp, cx
    jmp .run
.done:
    clc
    ret
.over:
    stc
    ret
