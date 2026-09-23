; =============================================================================
; os8088 - apps/pixelstein/pxhs.inc
;
; THE TABLE AND THE CODES (SPEC.md 97.13; wave 4): six rows of a score and
; three initials in SYSTEM\APPDATA\PXSTEIN.HS, the keystrokes that earn a
; row (the ENTER state), and the four-letter floor passwords the attract
; page's C key takes instead of a save file (PIXELSTEIN-PLAN 12.5).
;
; The shape is apps/tank's tkhs.inc and apps/dotdel's ddhs.inc, and so are
; the two rules that fail QUIETLY when they are got wrong (pxset.inc repeats
; them and this file reaches the folder through pxset.inc's own px_set_enter
; / px_set_leave, the sixth copy of those eighty lines not being written):
; the file lives in SYSTEM\APPDATA on OUR OWN volume, and it is
; OSAPI_FILE_GOTO, never its quiet twin.
;
; WHO TOUCHES THE FILE: the UI task and nobody else (SPEC.md 20.6 rule 7 - a
; worker may not touch a file). It is read once in the entry proc and written
; by px_hs_key's Enter, which is reached through px_key_common from W_ONKEY
; in a window and from the bracket's own int 16h loop in a bracket - the
; bracket IS the UI task (SPEC.md 53.7). The worker only ever moves the
; state to ENTER (the OVER card's timer); the commit is the keystroke's.
; The write happens when the initials are committed, not at close (SPEC.md
; 85.9's reason: a machine switched off would drop a row the table showed).
; =============================================================================

PX_NHS     equ 6
PX_HSFSZ   equ 4 + PX_NHS * 2 + PX_NHS * 3
px_hs_file:  db 'PXSTEIN.HS', 0
px_hs_magic: db 'PX8', 1            ; so a stale or foreign file is refused

; the table a machine with no file has ever seen
px_hs_def:   dw 10000, 8000, 6000, 4000, 2000, 1000
px_hs_defn:  db 'KEP', 'GRD', 'SLV', 'GLD', 'DOR', 'PXS'

; -----------------------------------------------------------------------------
; px_hs_init - the built-in table, before any file is read. preserves all
; -----------------------------------------------------------------------------
px_hs_init:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, px_hs_def
    mov di, px_hs
    mov cx, PX_NHS * 2 + PX_NHS * 3 ; the scores and the names are adjacent in
    rep movsb                       ; both places (the %if below holds it)
    pop es
    pop di
    pop si
    pop cx
    ret
%if px_hs_defn - px_hs_def != PX_NHS * 2
%error "px_hs_defn must follow px_hs_def: px_hs_init copies them as one run"
%endif

; -----------------------------------------------------------------------------
; px_hs_load - read the table once, at entry (UI TASK). Anything wrong with
;              the file leaves the built-in rows standing: a score table is
;              not worth an error box, and a first run has no file at all
; -----------------------------------------------------------------------------
px_hs_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call px_set_enter
    jc .out
    mov si, px_hs_file
    mov bx, px_hsbuf
    mov cx, PX_HSFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    call px_set_leave               ; (flags preserved: the read's carry)
    jc .out
    or dx, dx
    jnz .out
    cmp ax, PX_HSFSZ
    jne .out
    mov si, px_hsbuf
    mov di, px_hs_magic
    mov cx, 4
.mag:
    mov al, [si]
    cmp al, [di]
    jne .out
    inc si
    inc di
    loop .mag
    push ds
    pop es
    cld
    mov di, px_hs                   ; the rows, as they were written - a
    mov cx, PX_NHS * 2 + PX_NHS * 3 ; score is any word and an initial any
    rep movsb                       ; byte the card's font can draw
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_hs_save - write the table back (UI TASK: px_hs_commit's caller)
; -----------------------------------------------------------------------------
px_hs_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, px_hs_magic
    mov di, px_hsbuf
    movsw
    movsw
    mov si, px_hs
    mov cx, PX_NHS * 2 + PX_NHS * 3
    rep movsb
    call px_set_enter
    jc .out                         ; no folder: a user's own disk, not an error
    mov si, px_hs_file
    mov bx, px_hsbuf
    mov cx, PX_HSFSZ
    xor dx, dx
    call OSAPI_FILE_WRITE
    call px_set_leave
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_hs_qual - does [px_score] earn a row? CF = 1 yes (it beats the last one)
px_hs_qual:
    push ax
    mov ax, [px_score]
    cmp ax, [px_hs + (PX_NHS - 1) * 2]
    pop ax
    ja .yes
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; px_hs_commit - the score in at its place, the rows under it pushed down,
;                and the file written. UI TASK ONLY (px_hs_key's Enter)
; -----------------------------------------------------------------------------
px_hs_commit:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, PX_NHS - 1              ; DI = the row it may take
.find:
    or di, di
    jz .place
    mov bx, di
    dec bx
    shl bx, 1
    mov ax, [px_hs + bx]
    cmp ax, [px_score]
    jae .place                      ; the row above is as good: here it goes
    add bx, 2                       ; ...else push that row down one
    mov [px_hs + bx], ax
    mov ax, di                      ; the names, three bytes a row
    mov cl, 3
    mul cl
    mov bx, ax
    mov al, [px_hsn + bx - 3]
    mov [px_hsn + bx], al
    mov al, [px_hsn + bx - 2]
    mov [px_hsn + bx + 1], al
    mov al, [px_hsn + bx - 1]
    mov [px_hsn + bx + 2], al
    dec di
    jmp short .find
.place:
    mov bx, di
    shl bx, 1
    mov ax, [px_score]
    mov [px_hs + bx], ax
    mov ax, di
    mov cl, 3
    mul cl
    mov bx, ax
    xor si, si
.nm:
    mov al, [px_ini + si]
    cmp al, ' '
    jne .n1
    mov al, '-'                     ; an initial left blank is a dash
.n1:
    mov [px_hsn + bx], al
    inc bx
    inc si
    cmp si, 3
    jb .nm
    call px_hs_save
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; px_hs_key - a key in the ENTER state (AL = ascii, AH = scan; UI TASK).
;             EVERY KEY IS THE PROMPT'S (SPEC.md 85.9's rule): Esc and F
;             would otherwise leave the bracket and lose the row. Letters,
;             Backspace, Enter; the name line alone is redrawn (px_cardl)
; -----------------------------------------------------------------------------
px_hs_key:
    cmp ah, KSC_ENTER
    je .done
    cmp al, 8
    je .back
    call px_upper                   ; AL = 'A'..'Z', or CF = 1 not a letter
    jc .out
    mov bl, [px_inip]
    cmp bl, 3
    jae .out
    xor bh, bh
    mov [px_ini + bx], al
    inc byte [px_inip]
    jmp short .line
.back:
    cmp byte [px_inip], 0
    je .out
    dec byte [px_inip]
    mov bl, [px_inip]
    xor bh, bh
    mov byte [px_ini + bx], ' '
.line:
    mov al, PXCD_LNAME + 1
    call px_card_lowe
.out:
    ret
.done:
    call px_hs_commit               ; the file, on this (the UI) task
    jmp px_to_attract

; px_upper - AL a letter -> 'A'..'Z' (CF = 0), else CF = 1
px_upper:
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .no
    sub al, 'a' - 'A'
.up:
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; px_code_key - a key while the attract page takes a CODE (UI TASK): four
;               letters are matched against the floors' passwords (pxlev.inc's
;               px_levpw) - a match starts a game on that floor, a miss says
;               BAD CODE. Esc stops taking one; every other key is the code's
; -----------------------------------------------------------------------------
px_code_key:
    cmp ah, KSC_ESC
    je .stop
    cmp al, 8
    je .back
    call px_upper
    jc .out
    mov bl, [px_codep]
    xor bh, bh
    mov [px_code + bx], al
    inc byte [px_codep]
    cmp byte [px_codep], 4
    jb .line
    ; four letters: which floor is it?
    xor bx, bx
.f:
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [px_code]
    cmp ax, [px_levpw + si]
    jne .next
    mov ax, [px_code + 2]
    cmp ax, [px_levpw + si + 2]
    je .found
.next:
    inc bx
    cmp bx, PXL_NLEV
    jb .f
    mov byte [px_codep], 0xFE       ; BAD CODE on the line, until a key
    jmp short .line
.found:
    mov byte [px_codep], 0xFF
    mov al, bl
    call px_new_game                ; ...on that floor (between frames)
    ret
.back:
    cmp byte [px_codep], 0
    je .out
    dec byte [px_codep]
    mov bl, [px_codep]
    xor bh, bh
    mov byte [px_code + bx], '_'
    jmp short .line
.stop:
    mov byte [px_codep], 0xFF
.line:
    mov al, PXCD_LCODE + 1
    call px_card_lowe
.out:
    ret

; px_code_begin - the C key on the attract page: four blanks, taking letters
px_code_begin:
    mov word [px_code], '__'
    mov word [px_code + 2], '__'
    mov byte [px_codep], 0
    mov al, PXCD_LCODE + 1
    jmp px_card_lowe
