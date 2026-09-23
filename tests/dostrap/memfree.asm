; MEMFREE.COM - what INT 21h AH=48h offers the program, in the program's own
; units.  OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the
; manual.
;
; THE NUMBER A DOS PROGRAM REFUSES ON.  Prince of Persia says `Requires 318
; KBytes … 146 KBytes is Available`, and that sentence is the only instrument
; the field has for SPEC.md 96.35's arena - but it is in the PROGRAM's terms:
; what is left after it loaded itself, reported only because that program
; happens to refuse out loud.  This asks the same question with nothing loaded
; on top of it, so the answer pairs directly with the `(Arena: NNNKB)` the
; console now prints (SPEC.md 96.33.19) and the difference between the two is
; the box's own overhead and nothing else.
;
;   nasm -f bin -o MEMFREE.COM tests/dostrap/memfree.asm
;
; **AH=48h WITH BX=FFFF IS A FAILED ALLOCATION ON PURPOSE** - every DOS
; refuses it with CF=1 and AX=8, and hands back the largest block it COULD
; have given in BX.  Asking for the impossible is how the question is spelled;
; a program that read the carry and stopped would learn nothing.
;
; It runs UNDER A REAL DOS UNCHANGED, which is the point of everything in this
; directory: the reference answer is a machine, not a table in a document.

    org 0x100
    cpu 8086

start:
    mov ah, 0x48
    mov bx, 0xFFFF
    int 0x21                        ; CF=1, AX=8, BX = the largest block
    mov [free], bx

    mov si, s_para
    call puts
    mov ax, [free]
    call putdec

    mov si, s_kb
    call puts
    mov ax, [free]
    mov cl, 6
    shr ax, cl                      ; paragraphs -> whole KB
    call putdec
    mov si, s_crlf
    call puts

    ; ...AND THE PSP's OWN BLOCK, because the two together say where the
    ; arena went: DOS gives a .COM every byte it has, so [PSP:0002] is the
    ; top of what this program was handed and the free block above is what
    ; is left of it (SPEC.md 96.11).
    mov si, s_own
    call puts
    mov ax, [0x0002]                ; the PSP's first word past the int 20h
    mov bx, cs                      ; ...as a length rather than an address.
    sub ax, bx                      ; THROUGH BX: the 8086 has no `sub ax, cs`
    mov cl, 6
    shr ax, cl
    call putdec
    mov si, s_crlf
    call puts

    mov si, s_done
    call puts
    xor ax, ax                      ; WAIT FOR A KEY, dfree.asm's reason: under
    int 0x16                        ; os8088 the fsx bracket ends with the
    mov ax, 0x4C00                  ; program and the desktop comes back
    int 0x21

; --- AX as decimal, no leading zeros ----------------------------------------
putdec:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.push:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .push
.pop:
    pop ax
    add al, '0'
    call putc
    loop .pop
    pop dx
    pop cx
    pop bx
    pop ax
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

puts:
    push ax
    push si
.c:
    mov al, [si]
    or al, al
    jz .done
    call putc
    inc si
    jmp short .c
.done:
    pop si
    pop ax
    ret

s_para:  db 'MEMFREE: largest free block ', 0
s_kb:    db ' paragraphs = ', 0
s_own:   db 'MEMFREE: this block is ', 0
s_crlf:  db 'KB', 13, 10, 0
s_done:  db 'READY - press a key', 13, 10, 0
free:    dw 0
