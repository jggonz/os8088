; GETPSP.COM - does AH=62h (and AH=51h, its undocumented twin) answer the
; RUNNING program's PSP?  OURS, MIT with the rest of the tree.
; docs/DOS-DEBUGGING.md is the manual, SPEC.md 96.55 the finding.
;
; IBM DOS 3.30's own XCOPY.EXE starts with `mov ah, 62h / int 21h /
; mov [psp], bx` and parses its command tail out of that segment.  This DOS
; had no handler, so the call fell to the "invalid function" arm - CF=1,
; AX=0001, and BX LEFT AS THE PROGRAM HAD IT.  DOS sets no carry for 62h and
; nobody tests one, so XCOPY read a tail out of whatever segment BX happened
; to name and said "Invalid parameter" to every command line it was given.
;
;   nasm -f bin -o GETPSP.COM tests/dostrap/getpsp.asm
;
; It runs UNDER A REAL DOS UNCHANGED.  A .COM's PSP is its own CS, so the
; probe needs no reference value: it loads BX with junk, asks, and compares.
; It then EXECs ITSELF with the tail " C" - the child must be told ITS OWN
; PSP, not its parent's - and asks again once the child has returned, when
; the answer must be the parent's again.  Seven lines:
;
;   P62 BX=hhhh PSP=hhhh OK     the parent, AH=62h
;   P51 BX=hhhh PSP=hhhh OK     the parent, AH=51h
;   C62 ... / C51 ...           the child, both calls
;   R62 BX=hhhh PSP=hhhh OK     the parent, after the child returned
;   EXEC=0000                   the EXEC's AX (0 when it ran)
;   GETPSP READY
;
; `BAD` in place of `OK` is the finding.  It waits for a key before exiting:
; under os8088 the fsx bracket ends with the program.

    org 0x100
    cpu 8086

start:
    mov sp, stack_top
    mov si, 0x81                    ; the tail: a `C` anywhere in it makes us
    xor ch, ch                      ; the child
    mov cl, [0x80]
    jcxz .parent
.tail:
    lodsb
    cmp al, 'C'
    je child
    loop .tail
.parent:
    mov dx, 'P'
    call both
    ; --- shrink, then EXEC our own path with the tail " C" ------------------
    mov bx, (stack_top - $$ + 0x100 + 15) / 16
    mov ah, 0x4A
    int 0x21
    mov [pb+4], cs                  ; the block's three segments FIRST:
    mov [pb+8], cs                  ; mypath moves DS off our own
    mov [pb+12], cs
    call mypath                     ; DS:DX = our path, out of the environment
    push cs
    pop es
    mov bx, pb
    mov ax, 0x4B00
    int 0x21
    push cs
    pop ds
    push cs
    pop es
    jc .exf
    xor ax, ax
.exf:
    mov [exec_ax], ax
    mov dx, 'R'
    mov ah, 0x62
    call one
    mov si, s_exec
    call puts
    mov ax, [exec_ax]
    call puthex4
    call crlf
    mov si, s_done
    call puts
    xor ah, ah
    int 0x16
    mov ax, 0x4C00
    int 0x21

child:
    mov dx, 'C'
    call both
    mov ax, 0x4C00
    int 0x21

; --- both: DL = the line's letter; asks AH=62h, then AH=51h -----------------
both:
    mov ah, 0x62
    call one
    mov ah, 0x51
    ; fall through
; --- one: AH = the function, DL = the letter --------------------------------
one:
    push ax
    mov al, dl
    call putc
    pop ax
    push ax
    mov al, ah
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call nib
    pop ax
    push ax
    mov al, ah
    call nib
    pop ax
    push dx
    mov bx, 0x5A5A                  ; JUNK, and not anybody's PSP: an
    int 0x21                        ; unanswered call leaves it here
    pop dx
    mov [got], bx
    mov si, s_bx
    call puts
    mov ax, [got]
    call puthex4
    mov si, s_psp
    call puts
    mov ax, cs
    call puthex4
    mov si, s_ok
    mov ax, cs
    cmp ax, [got]
    je .p
    mov si, s_bad
.p: call puts
    ret

; --- mypath: DS:DX = the program's own path, past the environment's end -----
mypath:
    mov ds, [cs:0x2C]
    xor si, si
.z: lodsb
    or al, al
    jnz .z
    lodsb
    or al, al
    jnz .z                          ; ...until the double NUL
    lodsw                           ; the string count DOS 3 puts there
    mov dx, si
    ret

puts:
    lodsb
    or al, al
    jz .r
    call putc
    jmp short puts
.r: ret
putc:
    push dx
    push ax
    mov dl, al
    mov ah, 2
    int 0x21
    pop ax
    pop dx
    ret
crlf:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
puthex4:
    push ax
    mov al, ah
    call puthex2
    pop ax
puthex2:
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call nib
    pop ax
nib:
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
    jmp short putc

s_bx:   db ' BX=', 0
s_psp:  db ' PSP=', 0
s_ok:   db ' OK', 13, 10, 0
s_bad:  db ' BAD', 13, 10, 0
s_exec: db 'EXEC=', 0
s_done: db 'GETPSP READY', 13, 10, 0
tail:   db 2, ' C', 13
fcb:    db 0, '           ', 0, 0, 0, 0
pb:     dw 0, tail, 0, fcb, 0, fcb, 0
got:    dw 0
exec_ax: dw 0
        times 256 db 0
stack_top:
