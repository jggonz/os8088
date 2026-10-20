; MCB.COM - CAN A BLOCK GROW BACK INTO WHAT IT GAVE UP?  MIT with the tree.
;
; ONE BINARY, TWO DOSES (dosref.asm's shape, docs/DOS-DEBUGGING.md's method).
; Every memory manager written for DOS does the same thing: take the largest
; block there is, then shrink and grow it as the program's own heap moves.
; `AH=4Ah` GROWS ONLY INTO THE BLOCK IMMEDIATELY ABOVE, which is DOS's rule -
; and each shrink cuts a NEW free tail, so after two of them the space a block
; gave up is two or three blocks rather than one.  A DOS that does not
; COALESCE them then refuses a block SMALLER than one it has already granted,
; and reports the previous high-water mark as the maximum.
;
; Measured on Commander Keen 2 under kern_dos before SPEC.md 96.9.2: granted
; 0x78C0 paragraphs (483 KB), later refused 0x6900 (420 KB), answering BX =
; 0x6180 with 221 KB free above the block in three adjacent pieces.
;
; The six steps are Keen's own shape.  Each prints `cf=N bx=XXXX`, so the two
; columns diff with nothing to interpret, and the LAST is the finding: step 6
; asks for less than step 2 was given.
    org 0x100
    cpu 8086

start:
    mov [pspseg], cs                ; a .COM's PSP is its own segment
    mov si, s_head
    call puts

    ; --- 0. how much is there? BX=FFFFh is the documented way to ask --------
    mov es, [pspseg]
    mov bx, 0xFFFF
    mov ah, 0x4A
    int 0x21                        ; CF=1 and BX = the most it could have
    mov [most], bx
    mov si, s_max
    call puts
    mov ax, [most]
    call hex4
    call eol

    ; --- the six steps ------------------------------------------------------
    mov si, tab
    mov cx, NSTEP
.step:
    push cx
    push si
    mov al, [si]                    ; the numerator, in eighths of `most`
    xor ah, ah
    mul word [most]                 ; DX:AX
    mov bx, 8
    div bx                          ; AX = most * n / 8
    mov [want], ax
    mov si, s_ask
    call puts
    mov ax, [want]
    call hex4
    mov es, [pspseg]
    mov bx, [want]
    mov ah, 0x4A
    int 0x21
    pushf
    pop ax
    mov [flg], ax
    mov [gotbx], bx
    mov si, s_cf
    call puts
    mov al, [flg]
    and al, 1
    add al, '0'
    call putc
    mov si, s_bx
    call puts
    mov ax, [gotbx]
    call hex4
    call eol
    pop si
    pop cx
    inc si
    loop .step

    mov si, s_key
    call puts
.k:
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; Keen's own shape, in EIGHTHS of the largest block there is: take the lot,
; give half back, take three quarters, give half back, take seven eighths.
; Step 6 asks for LESS than step 2 was given, so a `cf=1` on it is the defect
; with nothing to interpret.
tab:    db 8, 4, 6, 4, 7
NSTEP   equ 5

s_head: db 'MCB 1', 13, 10, 0
s_max:  db 'MAX=', 0
s_ask:  db 'ask=', 0
s_cf:   db ' cf=', 0
s_bx:   db ' bx=', 0
s_key:  db 'KEY', 13, 10, 0

putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
puts:
    push ax
.l:
    mov al, [si]
    inc si
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
hex2:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call hexd
    pop ax
    and al, 0x0F
    call hexd
    pop cx
    pop ax
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
    jmp short putc

; A .COM owns every byte after its image, so none of this is EMITTED.
pspseg: equ $
most:   equ $ + 2
want:   equ $ + 4
gotbx:  equ $ + 6
flg:    equ $ + 8
