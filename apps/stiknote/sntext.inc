; =============================================================================
; apps/stiknote/sntext.inc - the note model (SPEC.md 110.13)
;
; THE POOL is SN_POOL bytes of text, note after note in list order, each
; note's length in SNP_LENS - no gaps, so a note's place is the sum of the
; lengths before it and an insert moves the pool's tail up one byte.
;
; A NOTE is lines separated by LF (10). Line 0 is the TITLE. A body line
; whose FIRST byte is SN_BOX0 or SN_BOX1 is a check box - open or ticked -
; and the marker is never shown as a character: the caret never stands on it
; (sn_norm), Backspace after it takes the box away, and a merge of two lines
; takes the second one's marker with the LF.
;
; THE WALK (sn_body, sn_vline, sn_vadv) cuts the body into VISUAL lines for a
; width in cells - [it_w] for a text paragraph, [it_wb] for a box's, which is
; indented. A line that does not fit breaks after its last space (the space
; is the line's end and is not drawn), or hard at the width when it has
; none. Nothing is cached: a note is at most SN_POOL bytes, and a walk of the
; whole of it is milliseconds on the target, against a line table that every
; insert would have to patch.
; =============================================================================

SN_POOLB    equ STIK_PAY + SNP_POOL

; sn_nptr - AL = a note: SI = its first byte, CX = its length. The rest kept
sn_nptr:
    push ax
    push bx
    mov si, SN_POOLB
    mov bx, STIK_PAY + SNP_LENS
    mov cl, al
    xor ch, ch
    jcxz .got
.l:
    add si, [bx]
    add bx, 2
    loop .l
.got:
    mov cx, [bx]
    pop bx
    pop ax
    ret

; sn_cur - the CURRENT note: SI = its first byte, CX = its length, DI = past
; its last. With no notes, an empty one at the pool's start. The rest kept
sn_cur:
    push ax
    mov al, [STIK_PAY + SNP_CUR]
    call sn_nptr
    mov di, si
    add di, cx
    pop ax
    ret

; sn_curlen - BX = the current note's length word's address. The rest kept
sn_curlen:
    push ax
    mov al, [STIK_PAY + SNP_CUR]
    xor ah, ah
    shl ax, 1
    add ax, STIK_PAY + SNP_LENS
    mov bx, ax
    pop ax
    ret

; sn_used - AX = the pool's bytes in use. The rest kept
sn_used:
    push bx
    push cx
    xor ax, ax
    mov bx, STIK_PAY + SNP_LENS
    mov cx, SN_MAXN
.l:
    add ax, [bx]
    add bx, 2
    loop .l
    pop cx
    pop bx
    ret

; sn_ztail - the pool past the bytes in use ZEROED: os88stik.inc writes the
; payload less its zero tail (SPEC.md 110.5.3), so a pool whose free end is
; clean is a file as long as its notes. Every register kept
sn_ztail:
    push ax
    push cx
    push di
    push es
    call sn_used
    mov di, SN_POOLB
    add di, ax
    mov cx, SN_POOL
    sub cx, ax
    push ds
    pop es
    xor al, al
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; sn_ins - insert AL at offset BX of the current note: the pool's tail up one.
; CF = 1 the pool is full and nothing changed (the toast is said). Every
; register kept
sn_ins:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ax
    call sn_used                ; AX = the bytes in use
    cmp ax, SN_POOL
    jb .room
    pop ax
    push si
    mov si, sn_s_pool
    call sn_toast
    pop si
    stc
    jmp short .x
.room:
    add ax, SN_POOLB            ; AX = past the last byte in use
    push ax
    call sn_cur                 ; SI = the note
    add si, bx                  ; SI = the place
    pop di
    mov cx, di
    sub cx, si                  ; the bytes from the place on
    push ds
    pop es
    mov si, di
    dec si
    std
    rep movsb                   ; up one, from the top down: DI ends on the
    cld                         ; place
    pop ax
    mov [di], al
    call sn_curlen
    inc word [bx]
    clc
.x:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sn_del - delete CX bytes at offset BX of the current note (CX > 0, inside
; it): the pool's tail down. Every register kept
sn_del:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    jcxz .x
    push cx
    call sn_used
    add ax, SN_POOLB            ; AX = past the last byte in use
    push ax
    call sn_cur
    add si, bx
    mov di, si                  ; DI = the place
    pop ax
    pop cx
    add si, cx                  ; SI = past what goes
    push cx
    mov cx, ax
    sub cx, si                  ; the bytes after it
    push ds
    pop es
    cld
    rep movsb
    pop cx
    call sn_curlen
    sub [bx], cx
    call sn_ztail               ; the bytes the move left behind, zero
.x:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sn_tend - AX = the offset of the current note's title's end: its first LF,
; or its length. The rest kept
sn_tend:
    push cx
    push si
    push di
    call sn_cur
    mov ax, si
.l:
    cmp si, di
    jae .x
    cmp byte [si], 10
    je .x
    inc si
    jmp short .l
.x:
    sub si, ax
    mov ax, si
    pop di
    pop si
    pop cx
    ret

; sn_ismark - CF = 0 (ZF meaningless) when the byte at SI (a pointer inside
; the current note, DI its end) is a box MARKER: a SN_BOX0 / SN_BOX1 at the
; start of a body line. Every register kept
sn_ismark:
    push ax
    push bx
    cmp si, di
    jae .no
    mov al, [si]
    or al, al
    jz .no
    cmp al, SN_BOX1
    ja .no
    push cx
    push si
    push di
    call sn_cur                 ; SI = the note's start (CX spent: kept -
    mov bx, si                  ; sne_kdel counts its delete in it)
    pop di
    pop si
    pop cx
    cmp si, bx
    je .no                      ; the title's first byte is never a box
    cmp byte [si - 1], 10
    jne .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret

; sn_norm - [sne_car] off a marker: a caret ON one goes to just past it
sn_norm:
    push si
    push di
    push cx
    call sn_cur
    add si, [sne_car]
    call sn_ismark
    jc .x
    inc word [sne_car]
.x:
    pop cx
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; THE WALK
; -----------------------------------------------------------------------------

; sn_body - start the walk over the current note's body: [it_e] its end, and
; the first visual line laid out. CF = 1 the note has no body (no LF). AX
; spent
sn_body:
    push cx
    push si
    push di
    call sn_cur
    mov [it_e], di
.l:
    cmp si, di
    jae .none
    lodsb
    cmp al, 10
    jne .l
    call sn_vpara               ; SI = the body's first byte
    call sn_vline
    clc
    jmp short .x
.none:
    stc
.x:
    pop di
    pop si
    pop cx
    ret

; sn_vpara - SI = a paragraph's first byte: its marker into [it_k] and past
; it, [it_p] its first line's start, [it_f] = 1. AL spent
sn_vpara:
    mov byte [it_k], 0
    mov byte [it_f], 1
    cmp si, [it_e]
    jae .x
    mov al, [si]
    or al, al
    jz .x
    cmp al, SN_BOX1
    ja .x
    mov [it_k], al
    inc si
.x:
    mov [it_p], si
    ret

; sn_vline - lay out the visual line at [it_p]: [it_le] past its content,
; [it_n] the next line's start, [it_lf] / [it_end] how it ended. Every
; register kept
sn_vline:
    push ax
    push bx
    push cx
    push si
    mov si, [it_p]
    mov cl, [it_w]
    cmp byte [it_k], 0
    je .w
    mov cl, [it_wb]
.w:
    xor ch, ch
    mov byte [it_lf], 0
    mov byte [it_end], 0
    xor bx, bx                  ; the last space seen, 0 none
.s:
    cmp si, [it_e]
    jae .eon
    mov al, [si]
    cmp al, 10
    je .lf
    jcxz .full
    cmp al, ' '
    jne .ns
    mov bx, si
.ns:
    inc si
    dec cx
    jmp short .s
.full:                          ; SI = the first byte that does not fit
    cmp al, ' '
    je .sp
    or bx, bx
    jz .hard
    mov si, bx                  ; back to the last space
.sp:
    mov [it_le], si
    inc si
    mov [it_n], si
    jmp short .x
.hard:
    cmp si, [it_p]
    jne .h
    inc si                      ; a width of 0 would never move on
.h:
    mov [it_le], si
    mov [it_n], si
    jmp short .x
.lf:
    mov [it_le], si
    inc si
    mov [it_n], si
    mov byte [it_lf], 1
    jmp short .x
.eon:
    mov [it_le], si
    mov [it_n], si
    mov byte [it_end], 1
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sn_vadv - on to the next visual line, laid out. CF = 1 there is none. Every
; register kept
sn_vadv:
    cmp byte [it_end], 0
    jne .none
    push ax
    push si
    mov si, [it_n]
    cmp byte [it_lf], 0
    je .cont
    call sn_vpara
    jmp short .l
.cont:
    mov [it_p], si
    mov byte [it_f], 0
.l:
    call sn_vline
    pop si
    pop ax
    clc
    ret
.none:
    stc
    ret

; sn_vcount - AX = the current note's body lines (0: no body). The walk is
; left on the last one
sn_vcount:
    xor ax, ax
    push ax
    call sn_body
    pop ax
    jc .r
.l:
    inc ax
    call sn_vadv
    jnc .l
.r:
    ret

; sn_vseek - the walk onto body line AX (0-based). CF = 1 there is no such
; line (the walk is then on the last). Every register kept
sn_vseek:
    push ax
    push cx
    mov cx, ax
    call sn_body
    jc .x
    jcxz .x
.l:
    call sn_vadv
    jc .x
    loop .l
    clc
.x:
    pop cx
    pop ax
    ret

; sn_vfind - the body line holding pointer SI (inside the current note, past
; its title): AX = its index, the walk on it. A line holds the pointers from
; its start to before its next one's - or, at the note's end, the end too
sn_vfind:
    push si
    xor ax, ax
    push ax
    call sn_body
    pop ax
    jc .x
.l:
    cmp si, [it_n]
    jb .x                       ; before the next line: this one
    cmp byte [it_end], 0
    jne .x
    inc ax
    call sn_vadv
    jnc .l
.x:
    pop si
    ret
