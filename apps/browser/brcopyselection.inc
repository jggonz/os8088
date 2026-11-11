; --- br_copyselection - copy the current selection to the text clipboard ----
; The page copy buffer is carved for this callback in the URL store, sized to
; the selected span (SPEC.md 71.23): nothing is reserved while browsing, a
; long selection costs memory only for this callback, and it takes no slot of
; MEM_OWNER_MAX. The clipboard copies the bytes before return.
br_copyselection:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [br_selkind], 1
    je .page
    cmp byte [br_selkind], 2
    jne .out
    mov ax, [br_baranch]
    mov bx, [br_barend]
    cmp ax, bx
    ja .barreverse
    jmp short .barordered
.barreverse:
    xchg ax, bx
    inc bx                          ; the anchor cell is part of a reverse drag
.barordered:
    cmp ax, bx
    je .out
    mov cx, bx
    sub cx, ax
    mov si, br_ubuf
    add si, ax
    push ds
    pop es
    call OSAPI_CLIP_PUT
    jmp .out
.page:
    ; [br_spanlo, br_spanhi) is the selection br_build draws (br_selspan), in
    ; dwords: past the 64 KiB seam, in EMS and under kern_mega alike. The
    ; stream is read through br_docmap a 16 KiB page at a time - where this
    ; refused EMS and read [br_docseg], a selector on kern_mega (SPEC.md
    ; 71.19.1)
    mov ax, [br_spanhi]
    mov dx, [br_spanhi+2]
    sub ax, [br_spanlo]
    sbb dx, [br_spanlo+2]
    jnz .maxcap                     ; 64 KiB or more
    or ax, ax
    jz .out
    cmp ax, 16384
    jae .maxcap
    shl ax, 1                       ; room for CR/LF expansion of break markers
    jmp short .cap
.maxcap:
    mov ax, 32768                   ; OSAPI_CLIP_PUT's published ceiling
.cap:
    ; The text is carved in the URL store after a transfer's buffer, if one is
    ; carved (SPEC.md 71.23): no claim of its own, and the store's 64 KiB is
    ; the bound - with a 32 KiB cluster buffer below it, a long selection is
    ; cut to what is left, and refused past it as a full one always was
    mov bx, BR_URLDL
    add bx, [br_dl_bufsz]
    mov cx, 0FFF0h
    sub cx, bx                      ; what the store can still reach
    cmp ax, cx
    jbe .room
    mov ax, cx
.room:
    mov cx, ax
    add cx, [br_dl_bufsz]
    xchg ax, cx                     ; AX = everything carved, CX = the text
    mov byte [br_dl_ugrow], 1       ; the grow may MOVE the store: a worker
.wbusy:                             ; copying into a transfer's buffer there
    cmp byte [br_dl_wbusy], 0       ; finishes first (br_dlx_put)
    je .grow
    call OSAPI_TASK_YIELD
    jmp short .wbusy
.grow:
    call far [br_dl_r_urlgrow]
    mov byte [br_dl_ugrow], 0       ; (mov keeps CF)
    jc .out
    add cx, bx
    mov [br_copycap], cx            ; DI's bound, an offset in the store
    mov [br_copybase], bx
    mov di, bx
    mov ax, [br_spanlo]             ; DX:AX the cursor, ES:SI the mapped byte
    mov dx, [br_spanlo+2]           ; and CX what the mapping still reaches;
    xor cx, cx                      ; DI the output, BH a marker's payload left
    xor bh, bh
.scan:
    cmp dx, [br_spanhi+2]
    jb .in
    ja .put
    cmp ax, [br_spanhi]
    jae .put
.in:
    or cx, cx
    jnz .read
    push di
    call far [br_dl_r_docmap]       ; ES:DI = the byte, AX..DX kept
    mov si, di
    pop di
    mov cx, ax
    and cx, 3FFFh                   ; to the end of its 16 KiB page, which
    neg cx                          ; every arm of br_docmap reaches
    add cx, 4000h
.read:
    mov bl, [es:si]
    inc si
    dec cx
    add ax, 1
    adc dx, 0
    or bh, bh
    jz .marker
    dec bh                          ; a marker's payload, not text
    jmp .scan
.marker:
    cmp bl, 0x20
    jae .char
    mov bh, 3                       ; D_LNK1: three index nibbles
    cmp bl, D_LNK1
    je .scan
    mov bh, 1                       ; D_CMETA: the cell-span hint
    cmp bl, D_CMETA
    je .scan
    cmp bl, D_HEAD                  ; D_HEAD: the level, 1..6, which would
    je .scan                        ; read as D_PARA or D_BR
    xor bh, bh
    cmp bl, D_PARA
    je .newline
    cmp bl, D_BR
    je .newline
    cmp bl, D_ROW
    je .newline
    cmp bl, D_CELL
    jne .scan                       ; formatting and layout markers are hidden
    mov bl, 9
    jmp short .emit
.newline:
    mov bl, 13
    call br_copyemit
    jc .overflow
    mov bl, 10
    jmp short .emit
.char:
    cmp bl, D_NBSP
    jne .emit
    mov bl, ' '
.emit:
    call br_copyemit
    jc .overflow
    jmp .scan
.put:
    mov si, [br_copybase]
    mov cx, di
    sub cx, si
    mov es, [br_urlseg]
    xor ax, ax                      ; the store MOVES and the clipboard claims
    call far [br_dl_r_pagepin]      ; its own copy, so ES:SI is pinned for the
    call OSAPI_CLIP_PUT             ; call (SPEC.md 66.3 rule 5)
    call far [br_dl_r_pageunpin]
.overflow:                          ; a full buffer leaves the previous
                                    ; clipboard untouched
    mov ax, [br_dl_bufsz]           ; the text goes back to the store; a
    call far [br_dl_r_urlgrow]      ; transfer's buffer below it stays
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Append BL at DI in the URL store's carved text while ES stays the page's
; mapping: they are separate segments, so switch only per byte. CF=1 when the
; text is full (nothing written). Every register but DI kept
br_copyemit:
    cmp di, [br_copycap]
    jae .full
    push es
    mov es, [br_urlseg]
    mov [es:di], bl
    pop es
    inc di
    clc
    ret
.full:
    stc
    ret
