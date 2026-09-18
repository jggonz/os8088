; =============================================================================
; os8088 - tests/dosxms/xmsq.asm
;
; The XMS WORKING path (SPEC.md 96.15), which needs a machine with a store
; above 1MB - so it runs under QEMU, which is entry 1 on docs/TESTING.md's
; short list. tests/dosxms.py is its twin and asserts the REFUSAL on an 8088.
;
; It does what a DOS program that uses extended memory actually does: find the
; driver through the multiplex interrupt, ask its version, allocate a block,
; move a pattern OUT, wipe the conventional copy, move it BACK, and check
; every byte. A layer that got the move block's handle/offset pairs the wrong
; way round would pass a "did it allocate" test and fail this one at the
; compare.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

BUFN      equ 512                   ; bytes moved each way

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- 1. is there a driver? ---------------------------------------------
    mov ax, 0x4300
    int 0x2F
    cmp al, 0x80
    jne .noxms
    mov ax, 0x4310                  ; ...and where is its entry point
    int 0x2F
    mov [ent], bx
    mov [ent+2], es

    mov ah, 0x00                    ; the version, which every program asks
    call far [ent]
    push ax
    mov ah, 0x09
    mov dx, msg_ver
    int 0x21
    pop ax
    call put_hex16
    call put_crlf

    mov ah, 0x08                    ; how much is free
    call far [ent]
    push dx
    mov ah, 0x09
    mov dx, msg_free
    int 0x21
    pop ax
    call put_dec16
    mov ah, 0x09
    mov dx, msg_kb
    int 0x21

    ; --- 2. a block of our own ---------------------------------------------
    mov ah, 0x09
    mov dx, 64                      ; 64 KB
    call far [ent]
    or ax, ax
    jz .noalloc
    mov [handle], dx

    ; --- 3. out, wiped, and back -------------------------------------------
    call fill
    mov word [mv+0], BUFN           ; the length, 32 bits
    mov word [mv+2], 0
    mov word [mv+4], 0              ; source handle 0 = conventional...
    mov [mv+6], word buf            ; ...so its "offset" is a FAR POINTER
    mov [mv+8], ds
    mov ax, [handle]
    mov [mv+10], ax                 ; ...and the destination is the block
    mov word [mv+12], 0
    mov word [mv+14], 0
    mov ah, 0x0B
    mov si, mv
    call far [ent]
    or ax, ax
    jz .nomove

    call wipe
    mov ax, [handle]                ; ...and back the other way
    mov [mv+4], ax
    mov word [mv+6], 0
    mov word [mv+8], 0
    mov word [mv+10], 0
    mov [mv+12], word buf
    mov [mv+14], ds
    mov ah, 0x0B
    mov si, mv
    call far [ent]
    or ax, ax
    jz .nomove

    call check
    jc .bad

    mov ah, 0x0A                    ; ...and give it back
    mov dx, [handle]
    call far [ent]
    or ax, ax
    jz .nofree

    mov ah, 0x09
    mov dx, msg_ok
    int 0x21
    jmp short .done

.noxms:   mov dx, msg_noxms
          jmp short .say
.noalloc: mov dx, msg_noalloc
          jmp short .say
.nomove:  mov dx, msg_nomove
          jmp short .say
.bad:     mov dx, msg_bad
          jmp short .say
.nofree:  mov dx, msg_nofree
.say:
    mov ah, 0x09
    int 0x21
.done:
    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C21
    int 0x21

; -----------------------------------------------------------------------------
; fill - buf[i] = i*7+3, which is not a pattern a zeroed or a copied page has
fill:
    push ax
    push cx
    push di
    mov di, buf
    xor ax, ax
    mov cx, BUFN
.n:
    mov [di], al
    add al, 7
    inc di
    loop .n
    pop di
    pop cx
    pop ax
    ret

wipe:
    push ax
    push cx
    push di
    mov di, buf
    mov cx, BUFN
    mov al, 0xAA
.n:
    mov [di], al
    inc di
    loop .n
    pop di
    pop cx
    pop ax
    ret

; check - CF=1 if any byte came back wrong
check:
    push ax
    push cx
    push si
    mov si, buf
    xor ax, ax
    mov cx, BUFN
.n:
    cmp [si], al
    jne .bad
    add al, 7
    inc si
    loop .n
    pop si
    pop cx
    pop ax
    clc
    ret
.bad:
    pop si
    pop cx
    pop ax
    stc
    ret

put_chr:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

put_crlf:
    mov al, 13
    call put_chr
    mov al, 10
    call put_chr
    ret

put_hex16:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 4
.n:
    mov ax, bx
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    mov bx, ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .e
    add al, 7
.e:
    call put_chr
    loop .n
    pop cx
    pop bx
    pop ax
    ret

put_dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    call put_chr
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
ent:      dd 0
handle:   dw 0
mv:       times 16 db 0

msg_hi:      db 13,10,'os8088 DOS xms working gate - DOSXMSQ.COM',13,10,13,10,'$'
msg_ver:     db 'XMSVER ','$'
msg_free:    db 'XMSFREE ','$'
msg_kb:      db ' KB',13,10,'$'
msg_ok:      db 'XMS ok - 512 bytes out, wiped, back, every byte',13,10,'$'
msg_noxms:   db 'FAILED - no XMS driver on a machine that has a store',13,10,'$'
msg_noalloc: db 'FAILED at allocate',13,10,'$'
msg_nomove:  db 'FAILED at move',13,10,'$'
msg_bad:     db 'FAILED - the bytes came back WRONG',13,10,'$'
msg_nofree:  db 'FAILED at free',13,10,'$'
msg_key:     db 13,10,'READY - press a key to exit with code 33',13,10,'$'

    align 16
buf:
