; =============================================================================
; os8088 - apps/mousechk/rows.inc - the row model and its builder API
;                                   (SPEC.md 126.2)
;
; A rebuild makes the WHOLE list again, from the sections' own xx_rows procs,
; into mc_buf: a packed run of records, one per row,
;
;       +0 kind   MC_K_HEAD 0 (a section heading) / MC_K_ROW 1 / MC_K_LINE 2
;       +1 lamp   a heading's lamp (LP_*); 0 in the others
;       +2 text   NUL-terminated, at most MC_RW characters
;
; and mc_roff[i] is record i's offset. Nothing here draws: ui.inc compares each
; visible row's HASH against the hash it last drew at that screen line and
; draws only the lines that differ (SPEC.md 126.2.3). The sections never see a
; screen, a pixel or a segment: they call these procs with SI in their own
; segment and ES = KERNEL_SEG, and every proc keeps every register but its
; documented outputs.
;
; A full buffer is not an error: rows past MC_MAXR (or past the buffer) are
; written into mc_scr and dropped, and the last row kept is replaced with
; "(list full)" at the end of the rebuild (mc_finish), so the loss is said.
; =============================================================================

MC_RW       equ 60              ; characters in a row's text; helpers truncate
MC_VCOL     equ 22              ; the column mc_row's value starts at
MC_MAXR     equ 128             ; rows
MC_BUFSZ    equ 5120            ; bytes of records

LP_INFO     equ 0               ; blue arrow     information
LP_OK       equ 1               ; green tick     as it should be
LP_WARN     equ 2               ; brown checks   works, but look
LP_BAD      equ 3               ; red solid      a problem
LP_NA       equ 4               ; grey hollow    does not apply here

MC_K_HEAD   equ 0
MC_K_ROW    equ 1
MC_K_LINE   equ 2

; -----------------------------------------------------------------------------
; mc_reset - begin a rebuild: the list is empty
; -----------------------------------------------------------------------------
mc_reset:
    mov word [mc_nrows], 0
    mov word [mc_bp], mc_buf
    mov word [mc_rp], mc_scr + 2
    mov word [mc_hd], 0
    mov byte [mc_rc], 0
    mov byte [mc_real], 0
    mov byte [mc_ovf], 0
    ret

; -----------------------------------------------------------------------------
; mc_finish - end a rebuild: say so if rows were dropped
; -----------------------------------------------------------------------------
mc_finish:
    cmp byte [mc_ovf], 0
    je .out
    push ax
    push bx
    push si
    push di
    mov bx, [mc_nrows]
    or bx, bx
    jz .done
    dec bx
    shl bx, 1
    mov di, [mc_roff+bx]
    mov byte [di], MC_K_LINE
    mov byte [di+1], 0
    add di, 2
    mov si, mc_s_full
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
.done:
    pop di
    pop si
    pop bx
    pop ax
.out:
    ret

mc_s_full:  db '  (list full - more rows not shown)', 0

; -----------------------------------------------------------------------------
; mc_newrow - open a record: AL = kind, AH = lamp. [mc_rp] = its text, empty.
; -----------------------------------------------------------------------------
mc_newrow:
    push ax
    push bx
    push dx
    push di
    mov dx, [mc_bp]
    cmp byte [mc_real], 0
    je .have
    mov dx, [mc_rp]                 ; the last real row's NUL, and one past it
    inc dx
    mov [mc_bp], dx
.have:
    mov bx, [mc_nrows]
    cmp bx, MC_MAXR
    jae .full
    cmp dx, mc_buf + MC_BUFSZ - (MC_RW + 4)
    ja .full
    mov di, dx
    shl bx, 1
    mov [mc_roff+bx], di
    inc word [mc_nrows]
    mov byte [mc_real], 1
    jmp short .set
.full:
    mov byte [mc_ovf], 1
    mov byte [mc_real], 0
    mov di, mc_scr
.set:
    mov [di], al
    mov [di+1], ah
    add di, 2
    mov byte [di], 0
    mov [mc_rp], di
    mov byte [mc_rc], 0
    pop di
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_head - in SI = NUL heading, AL = lamp. Starts a SECTION and its heading.
; -----------------------------------------------------------------------------
mc_head:
    push ax
    mov ah, al
    mov al, MC_K_HEAD
    call mc_newrow
    mov ax, [mc_rp]
    dec ax                          ; the record's lamp byte: text - 1
    mov [mc_hd], ax
    pop ax
    jmp mc_put

; -----------------------------------------------------------------------------
; mc_lamp - in AL = lamp. Raises the current section's lamp when AL is worse
;           (BAD > WARN > OK > INFO > NA)
; -----------------------------------------------------------------------------
mc_lamp:
    push ax
    push bx
    push si
    mov si, [mc_hd]
    or si, si
    jz .out
    mov bl, al
    xor bh, bh
    mov ah, [mc_lrank+bx]           ; the new lamp's rank
    mov bl, [si]
    mov bl, [mc_lrank+bx]           ; ...and the heading's
    cmp ah, bl
    jbe .out
    mov [si], al
.out:
    pop si
    pop bx
    pop ax
    ret

mc_lrank:   db 1, 2, 3, 4, 0        ; INFO OK WARN BAD NA

; -----------------------------------------------------------------------------
; mc_row - in SI = NUL label. A new row: the label at column 0, the cursor at
;          MC_VCOL (a label of MC_VCOL or more is cut to MC_VCOL - 1)
; -----------------------------------------------------------------------------
mc_row:
    push ax
    push si
    mov al, MC_K_ROW
    xor ah, ah
    call mc_newrow
.l:
    cmp byte [mc_rc], MC_VCOL - 1
    jae .pad
    mov al, [si]
    or al, al
    jz .pad
    inc si
    call mc_putc
    jmp short .l
.pad:
    cmp byte [mc_rc], MC_VCOL
    jae .out
    mov al, ' '
    call mc_putc
    jmp short .pad
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_line - in SI = NUL text. A new row whose text begins at column 2: an
;           indented free line (a verdict, a trace step, an explanation)
; -----------------------------------------------------------------------------
mc_line:
    push ax
    mov al, MC_K_LINE
    xor ah, ah
    call mc_newrow
    mov al, ' '
    call mc_putc
    call mc_putc
    pop ax
    ; falls into mc_put

; -----------------------------------------------------------------------------
; mc_put - in SI = NUL text. Appends at the cursor
; -----------------------------------------------------------------------------
mc_put:
    push ax
    push si
.l:
    mov al, [si]
    or al, al
    jz .out
    inc si
    call mc_putc
    jmp short .l
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_putc - in AL = a character. Appends; past MC_RW it is dropped
; -----------------------------------------------------------------------------
mc_putc:
    push bx
    cmp byte [mc_rc], MC_RW
    jae .out
    mov bx, [mc_rp]
    mov [bx], al
    inc bx
    mov byte [bx], 0
    mov [mc_rp], bx
    inc byte [mc_rc]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; mc_hex8 / mc_hex16 - in AL / AX. Append 2 / 4 hex digits
; -----------------------------------------------------------------------------
mc_hex16:
    push ax
    xchg al, ah
    call mc_hex8
    pop ax
    ; falls into mc_hex8
mc_hex8:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call mc_nib
    mov al, ah
    call mc_nib
    pop cx
    pop ax
    ret

mc_nib:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 'A' - '9' - 1
.d:
    jmp short mc_putc

; -----------------------------------------------------------------------------
; mc_dec - in AX = unsigned. Appends it in decimal
; -----------------------------------------------------------------------------
mc_dec:
    push ax
    push si
    push di
    mov di, mc_tmp
    call dec_u16
    mov si, mc_tmp
    call mc_put
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_bits - in AL = a byte, SI = eight NUL-free characters, one per bit from
;           bit 7 to bit 0. Appends the byte as the letter where the bit is
;           set and '.' where it is clear
; -----------------------------------------------------------------------------
mc_bits:
    push ax
    push cx
    push dx
    push si
    mov dl, al
    mov cx, 8
.l:
    mov al, [si]
    inc si
    shl dl, 1
    jc .set
    mov al, '.'
.set:
    call mc_putc
    loop .l
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mc_hash - in BX = a row index. Out AX = its hash: never 0 (invalid) and
;           never 1 (the blank line a row index past the end hashes to).
;           A row's hash covers its kind, lamp and text, and the shell redraws
;           a screen line when it differs from the hash last drawn there
; -----------------------------------------------------------------------------
mc_hash:
    push bx
    push cx
    push dx
    push si
    cmp bx, [mc_nrows]
    jae .blank
    shl bx, 1
    mov si, [mc_roff+bx]
    mov ax, 0x1357
    mov cx, 2                       ; kind and lamp
.k:
    mov dl, [si]
    inc si
    rol ax, 1
    xor al, dl
    add ah, dl
    loop .k
.t:
    mov dl, [si]
    inc si
    or dl, dl
    jz .done
    rol ax, 1
    xor al, dl
    add ah, dl
    jmp short .t
.done:
    cmp ax, 2
    jae .out
    add ax, 2
    jmp short .out
.blank:
    mov ax, 1
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret
