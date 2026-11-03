; FCBRD.COM - what DOS's FCB READ calls answer (SPEC.md 96.56.2).
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; Runs under a real DOS unchanged; tests/dosfcbrd.py holds what IBM DOS 3.30
; answered, and this DOS must answer the same.  It reads FCBRD.DAT - 300
; bytes, byte i = (i * 7 + 3) & 255, written by the row - through an FCB
; with the default 128-byte record:
;
;   OPEN  AL= SIZE=   open, and the size DOS put in the FCB
;   RND n AL= SUM=    AH=21h random read of record n (0, 2 - partial - and 3)
;   SEQ   AL= SUM=    AH=14h three times from block 0, record 0
;   BLK   AL= CX= SUM=  AH=27h, four records from record 0
;   SET   RR=         AH=24h after them: block 0, record 3
;   CLOSE AL=
;
; SUM is a plain word sum of the whole 128-byte DTA, so a partial record's
; PADDING is in it: DOS zero-fills the rest of the record.  Then it waits for
; a key - under os8088 the bracket ends with the program.
    org 0x100
    cpu 8086
start:
    mov dx, dta
    mov ah, 0x1A
    int 0x21
    mov dx, fcb
    mov ah, 0x0F
    int 0x21
    mov si, s_open
    call tag
    mov si, s_size
    call puts
    mov ax, [fcb+0x12]
    call hex4
    mov ax, [fcb+0x10]
    call hex4
    call crlf

    xor bx, bx                      ; random reads of records 0, 2 and 3
.rnd:
    mov [fcb+0x21], bx
    mov word [fcb+0x23], 0
    call fill
    mov dx, fcb
    mov ah, 0x21
    int 0x21
    push ax
    mov si, s_rnd
    call puts
    mov al, bl
    add al, '0'
    call putc
    pop ax
    call tagal
    call sum
    call crlf
    inc bx
    cmp bx, 1
    jne .r1
    inc bx
.r1:
    cmp bx, 4
    jb .rnd

    mov word [fcb+0x0C], 0          ; sequential, from the top
    mov byte [fcb+0x20], 0
    mov cx, 3
.seq:
    push cx
    call fill
    mov dx, fcb
    mov ah, 0x14
    int 0x21
    mov si, s_seq
    call tag2
    call sum
    call crlf
    pop cx
    loop .seq

    mov dx, fcb                     ; AH=24h: block 0, record 2 -> 2
    mov ah, 0x24
    int 0x21
    mov si, s_set
    call puts
    mov ax, [fcb+0x21]
    call hex4
    call crlf

    mov word [fcb+0x21], 0          ; four records from 0 into the DTA's
    mov word [fcb+0x23], 0          ; first 512 bytes: three are there
    call fill
    mov dx, fcb
    mov cx, 4
    mov ah, 0x27
    int 0x21
    push cx                         ; (the printing below spends CL)
    mov si, s_blk
    call tag2
    mov si, s_cx
    call puts
    pop ax
    call hex4
    call sum
    mov si, s_rr
    call puts
    mov ax, [fcb+0x21]
    call hex4
    call crlf

    mov dx, fcb
    mov ah, 0x10
    int 0x21
    mov si, s_close
    call tag2
    call crlf
    mov si, s_done
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

fill:                               ; the DTA to 0FFh, so padding shows
    push cx
    push di
    mov di, dta
    mov cx, 512
    mov al, 0xFF
    cld
    rep stosb
    pop di
    pop cx
    ret
sum:                                ; ' SUM=' and the word sum of dta[0..127]
    push si
    mov si, s_sum
    call puts
    mov si, dta
    mov cx, 64
    xor dx, dx
.s:
    lodsw
    add dx, ax
    loop .s
    mov ax, dx
    call hex4
    pop si
    ret
tag:                                ; the label, then ' AL=' and AL
    push ax
    call puts
    pop ax
tagal:
    push ax
    mov si, s_al
    call puts
    pop ax
    jmp hex2
tag2:
    push ax
    call puts
    pop ax
    jmp short tagal
crlf:
    mov si, s_crlf
puts:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret
putc:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
hex2:
    push ax
    mov cl, 4
    shr al, cl
    call .d
    pop ax
.d:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    jmp putc

s_open:  db 'OPEN', 0
s_rnd:   db 'RND ', 0
s_seq:   db 'SEQ', 0
s_blk:   db 'BLK', 0
s_set:   db 'SET RR=', 0
s_close: db 'CLOSE', 0
s_al:    db ' AL=', 0
s_sum:   db ' SUM=', 0
s_cx:    db ' CX=', 0
s_size:  db ' SIZE=', 0
s_rr:    db ' RR=', 0
s_crlf:  db 13, 10, 0
s_done:  db 'DONE', 13, 10, 0
fcb:     db 0, 'FCBRD   DAT'
         times 37 - 12 db 0
dta:
