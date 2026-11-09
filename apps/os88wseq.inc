; =============================================================================
; os8088 - apps/os88wseq.inc - an APPEND OF ANY SIZE, over WRITE_SEQ's stream
;                              (SPEC.md 18.4.9.4)
;
; OSAPI_FILE_APPEND and OSAPI_FILE_WRITE_SEQ both append only at a CLUSTER
; boundary: every chunk but the last must be a whole number of clusters, and
; a file whose size is not one is refused FERR_NAME (SPEC.md 18.4.4,
; 18.4.9). That is the walk's rule and the right one for the kernel - but a
; program writing a report, a log or an export has text of whatever length
; it has, and every such program was going to buffer it the same way. This
; is that buffering, written once: the read side's os88rseq.inc, pointed the
; other way.
;
;     mov si, my_name                ; open: the file is created EMPTY
;     mov di, my_stream              ; (WSEQ_SIZE bytes of yours)
;     mov bx, my_buf                 ; a buffer of yours, in DS: at least a
;     mov cx, MY_BUFSZ               ; cluster - 4 KB covers every volume
;     call os88_wseq_open            ; os8088 formats
;     ...
;     mov di, my_stream
;     les si, [where]                ; ES:SI = ANY bytes, CX = ANY count
;     mov cx, [count]
;     call os88_wseq_put             ; as many times as you like
;     ...
;     mov di, my_stream
;     call os88_wseq_close           ; the tail, any size
;
; Every call answers CF=0, or CF=1 with AX = FERR_*; a failed stream stays
; failed (every later call answers the same) and what reached the disk
; stays there.
;
; THE BUFFER IS FLUSHED IN WHOLE CLUSTERS. The open asks OSAPI_VOL_STAT for
; the cluster of the volume the instance stands on - which is where the file
; is - once: that is a FAT walk, ~105 ms on a 20 MB disk on a 4.77 MHz 8088
; (SPEC.md 18.4.5), paid per STREAM and not per put. A full buffer writes the
; largest whole number of clusters it holds and keeps the rest, so a buffer
; that is itself a cluster multiple never moves a byte. A buffer smaller
; than one cluster is refused at the open, FERR_NAME.
;
; ON kern_small THE OPEN IS REFUSED, and no chunk is reached: OSAPI_VOL_STAT
; is a bare `stc` there (kernel.asm's osapi_vol_stat), so os88_wseq_open
; answers CF=1 with AX = 0, which is no FERR_* code. The APPEND arm in
; os88_wseq_out was written for that kernel's FERR_NAME WRITE_SEQ - the same
; rule, the same chunks, a lookup and a walk a call - and on kern_big it only
; repeats WRITE_SEQ's refusal of a misaligned chunk. The stream is PLAIN (not
; WSEQF_HELD): every flush is committed, so nothing is held across the
; caller's callbacks.
;
; UI TASK ONLY - every call is a file slot (SPEC.md 20.6 rule 7).
; =============================================================================
%ifndef OS88WSEQ_INC
%define OS88WSEQ_INC

WS_NAME     equ 0                   ; dw the NUL 8.3 name (yours, kept)
WS_BUF      equ 2                   ; dw the buffer, in DS
WS_CAP      equ 4                   ; dw its size
WS_FILL     equ 6                   ; dw bytes in it
WS_TOK      equ 8                   ; dw WRITE_SEQ's token
WS_MASK     equ 10                  ; dw the cluster's bytes - 1
WS_ERR      equ 12                  ; dw 0, or the FERR_* that ended it
WSEQ_SIZE   equ 14

; -----------------------------------------------------------------------------
; os88_wseq_open - SI -> a NUL 8.3 name, DS:DI -> the stream, BX = the
;                  buffer (DS), CX = its size: the file created EMPTY in the
;                  current directory (replacing one of that name)
; out: CF=0; CF=1 AX = FERR_*
; preserves: everything but AX
; -----------------------------------------------------------------------------
os88_wseq_open:
    push bx
    push cx
    push dx
    push es
    mov [di+WS_NAME], si
    mov [di+WS_BUF], bx
    mov [di+WS_CAP], cx
    xor ax, ax
    mov [di+WS_FILL], ax
    mov [di+WS_TOK], ax
    mov [di+WS_ERR], ax
    push bx
    push cx
    push di
    call OSAPI_VOL_STAT             ; AX = sectors a cluster, CX = bytes one
    pop di
    jc .stat
    mul cx                          ; AX = the cluster's bytes (a power of two)
    pop cx
    pop bx
    mov dx, FERR_NAME
    cmp cx, ax                      ; a buffer under one cluster could never
    jb .fail                        ; flush a whole one
    dec ax
    mov [di+WS_MASK], ax
    push ds
    pop es
    xor cx, cx                      ; DX:CX = 0: the file exists, and EMPTY -
    xor dx, dx                      ; size 0 is a cluster multiple
    call OSAPI_FILE_WRITE
    jnc .out
    xchg dx, ax
    jmp short .fail
.stat:
    pop cx
    pop bx
    xchg dx, ax
.fail:
    mov [di+WS_ERR], dx
    xchg ax, dx
    stc
.out:
    pop es
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88_wseq_put - DS:DI -> the stream, ES:SI -> the bytes, CX = how many
;                 (0 is legal and writes nothing)
; out: CF=0; CF=1 AX = FERR_*
; preserves: everything but AX
; -----------------------------------------------------------------------------
os88_wseq_put:
    push bx
    push cx
    push dx
    push si
    push bp
    mov bp, di
.more:
    mov ax, [ds:bp+WS_ERR]
    or ax, ax
    jnz .err
    jcxz .done
    mov dx, [ds:bp+WS_CAP]
    sub dx, [ds:bp+WS_FILL]         ; DX = room
    cmp dx, cx
    jbe .n
    mov dx, cx
.n:
    sub cx, dx                      ; CX = what is left after this copy
    push cx
    push di
    mov cx, dx
    mov di, [ds:bp+WS_BUF]
    add di, [ds:bp+WS_FILL]
    add [ds:bp+WS_FILL], dx
.c:
    mov al, [es:si]                 ; ES:SI into DS:DI
    mov [di], al
    inc si
    inc di
    loop .c
    pop di
    pop cx
    mov ax, [ds:bp+WS_FILL]
    cmp ax, [ds:bp+WS_CAP]
    jb .more                        ; (CX = 0 then: done)
    not ax                          ; FULL: its whole clusters out
    or ax, [ds:bp+WS_MASK]
    not ax                          ; AX = fill rounded DOWN to a cluster
    call os88_wseq_out
    jmp short .more
.err:
    stc
    jmp short .out
.done:
    clc
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; os88_wseq_close - DS:DI -> the stream: what is buffered goes out, ANY size
;                   (the last chunk is the one allowed to be short)
; out: CF=0; CF=1 AX = FERR_*
; preserves: everything but AX
; -----------------------------------------------------------------------------
os88_wseq_close:
    push bp
    mov bp, di
    mov ax, [ds:bp+WS_FILL]
    or ax, ax
    jz .end
    call os88_wseq_out
.end:
    mov ax, [ds:bp+WS_ERR]
    cmp ax, 1                       ; CF = (AX = 0): flip it
    cmc
    pop bp
    ret

; os88_wseq_out - (internal) BP -> the stream, AX = the bytes to write from
; the front of its buffer: written, and what is past them moved down to the
; front. An error is banked in WS_ERR. Preserves all but AX and flags
os88_wseq_out:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov cx, ax                      ; CX = the chunk
    mov si, [ds:bp+WS_NAME]
    mov bx, [ds:bp+WS_BUF]
    mov di, [ds:bp+WS_TOK]
    xor al, al                      ; plain: every chunk committed
    push cx
    call OSAPI_FILE_WRITE_SEQ       ; DI = the next token
    pop cx
    jnc .ok
    cmp ax, FERR_NAME               ; the small kernel's answer, or a
    jne .bad                        ; misaligned chunk APPEND refuses too
    call OSAPI_FILE_APPEND
    jc .bad
    xor di, di                      ; no token on that path
.ok:
    mov [ds:bp+WS_TOK], di
    mov si, [ds:bp+WS_BUF]          ; the rest down to the front
    mov di, si
    add si, cx
    mov ax, [ds:bp+WS_FILL]
    sub ax, cx
    mov [ds:bp+WS_FILL], ax
    xchg cx, ax
    cld
    rep movsb
    jmp short .out
.bad:
    mov [ds:bp+WS_ERR], ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

%endif
