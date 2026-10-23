; --- br_copyselection - copy the current selection to the text clipboard ----
; The page copy buffer is a short-lived claim sized to the selected source
; span. Nothing is reserved while browsing, and even a long selection costs
; memory only for this callback. The clipboard copies the bytes before return.
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
    mov ax, [br_selanch]
    mov bx, [br_selend]
    cmp ax, bx
    je .out
    ja .reverse
    jmp short .range
.reverse:
    xchg ax, bx
    inc bx
.range:
    mov [br_copystart], ax
    mov [br_copyend], bx
    sub bx, ax
    or bx, bx
    jz .out
    mov ax, bx
    cmp ax, 16384
    jae .maxcap
    shl ax, 1                       ; room for CR/LF expansion of break markers
    jmp short .cap
.maxcap:
    mov ax, 32768
.cap:
    mov [br_copycap], ax
    add ax, 1023
    mov cl, 10
    shr ax, cl                      ; claim only enough KB for this selection
    or ax, ax
    jnz .claim
    inc ax
.claim:
    cmp ax, 32                      ; OSAPI_CLIP_PUT's published ceiling
    ja .out
    call OSAPI_MEM_CLAIM
    jc .out
    mov [br_copyseg], dx
    mov es, dx
    xor di, di
    mov si, [br_copystart]
    mov bx, [br_copyend]
    mov dx, [br_docseg]
    mov es, dx
.scan:
    cmp si, bx
    jae .put
    mov al, [es:si]
    inc si
    cmp al, 0x20
    jae .char
    cmp al, D_LNK1
    jne .m1
    add si, 2                      ; two opaque bytes carry the link index
    jmp short .scan
.m1:
    cmp al, D_CMETA
    jne .m2
    inc si                          ; cell-span hint, not visible text
    jmp short .scan
.m2:
    cmp al, D_PARA
    je .newline
    cmp al, D_BR
    je .newline
    cmp al, D_ROW
    je .newline
    cmp al, D_CELL
    je .space
    jmp short .scan                 ; formatting and layout markers are hidden
.newline:
    cmp di, [br_copycap]
    jae .overflow
    mov al, 13
    call br_copyemit
    cmp di, [br_copycap]
    jae .overflow
    mov al, 10
    call br_copyemit
    jmp short .scan
.space:
    cmp di, [br_copycap]
    jae .overflow
    mov al, 9
    call br_copyemit
    jmp short .scan
.char:
    cmp al, D_NBSP
    jne .emit
    mov al, ' '
.emit:
    cmp di, [br_copycap]
    jae .overflow
    call br_copyemit
    jmp short .scan
.put:
    mov cx, di
    mov si, 0
    mov dx, [br_copyseg]
    mov es, dx
    call OSAPI_CLIP_PUT
    mov dx, [br_copyseg]
    call OSAPI_MEM_FREE
    jmp short .out
.overflow:
    mov dx, [br_copyseg]
    call OSAPI_MEM_FREE              ; leave the previous clipboard untouched
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Append AL at the transient clipboard segment while ES remains the page reader.
; The page and output are separate segments on 8086, so switch only per byte.
br_copyemit:
    push ax
    push dx
    push es
    mov dx, [br_copyseg]
    mov es, dx
    pop dx                          ; page segment, restored after the store
    mov [es:di], al
    mov es, dx
    pop dx
    pop ax
    inc di
    ret
