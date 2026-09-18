; =============================================================================
; os8088 - tests/dostrap/kdhello.asm
;
; KDHELLO.COM - the DOS program docs/plans/KERN-DOS-PLAN.md wave 4 runs, and
; the smallest one that can tell a working kern_dos from a plausible one.
;
;   nasm -f bin -w+error -o KDHELLO.COM tests/dostrap/kdhello.asm
;
; It prints four things through INT 21h and nothing through the BIOS, which
; is the point: every line on the screen is evidence that a DOS call was
; SERVICED, by the DOS core running over a back end that is not the kernel's.
;
;   1  a banner, through AH=09h - the string services work at all;
;   2  the DOS version, through AH=30h - a dispatch that RETURNS a value
;      rather than one that only has to not crash;
;   3  the top of its own memory block, out of PSP:0002 - the arena, which is
;      the number this whole plan exists to move, read where a real DOS puts
;      it rather than where we say it is;
;   4  the first bytes of a FILE it opens and reads itself (AH=3Dh/3Fh/3Eh) -
;      the file layer, end to end, through the doors kerndos/kdback.inc
;      implements rather than the ones apps/dos/dos.asm ships.
;
; Then AH=4Ch with a known exit code.  It runs unchanged under a real DOS
; (docs/DOS-DEBUGGING.md's rule), which is what makes every line above a
; comparison rather than an assertion about ourselves.
;
; NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the rest of the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

EXITC   equ 0x2A                 ; the exit code, so the harness can tell a
                                 ; program that finished from one that fell
                                 ; over into a zero

start:
    mov dx, s_banner
    mov ah, 0x09
    int 0x21

    ; --- 2: the version -----------------------------------------------------
    mov dx, s_ver
    mov ah, 0x09
    int 0x21
    mov ah, 0x30
    int 0x21                     ; AL = major, AH = minor
    push ax
    xor ah, ah
    call dec
    mov dl, '.'
    mov ah, 0x02
    int 0x21
    pop ax
    mov al, ah
    xor ah, ah
    call dec
    call crlf

    ; --- 3: the arena, out of the PSP ---------------------------------------
    mov dx, s_mem
    mov ah, 0x09
    int 0x21
    mov ax, [0x0002]             ; PSP:0002 = the first paragraph NOT ours
    mov bx, cs
    sub ax, bx                   ; ...minus where we start = our own size
    mov cl, 6
    shr ax, cl                   ; paragraphs -> KB
    call dec
    mov dx, s_kb
    mov ah, 0x09
    int 0x21

    ; --- 4: a file, end to end ----------------------------------------------
    mov dx, s_file
    mov ah, 0x09
    int 0x21
    mov dx, fname
    mov al, 0                    ; read-only
    mov ah, 0x3D
    int 0x21
    jc .nofile
    mov bx, ax                   ; the handle
    mov dx, buf
    mov cx, 16
    mov ah, 0x3F
    int 0x21
    jc .noread
    mov cx, ax                   ; ...however many came back
    mov si, buf
.pc:
    jcxz .shut
    lodsb
    mov dl, al
    mov ah, 0x02
    int 0x21
    dec cx
    jmp short .pc
.shut:
    mov ah, 0x3E
    int 0x21
    call crlf
    jmp short .done
.nofile:
    mov dx, s_nofile
    mov ah, 0x09
    int 0x21
    jmp short .done
.noread:
    mov dx, s_noread
    mov ah, 0x09
    int 0x21
.done:
    mov dx, s_ok
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C00 | EXITC
    int 0x21

; --- AX in decimal, through AH=02h and nothing else -------------------------
dec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop dx
    add dl, '0'
    mov ah, 0x02
    int 0x21
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

crlf:
    push ax
    push dx
    mov dl, 13
    mov ah, 0x02
    int 0x21
    mov dl, 10
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

s_banner: db 'KDHELLO under kern_dos', 13, 10, '$'
s_ver:    db 'DOS version ', '$'
s_mem:    db 'PSP says ', '$'
s_kb:     db ' KB', 13, 10, '$'
s_file:   db 'file says: ', '$'
s_nofile: db '(open failed)', 13, 10, '$'
s_noread: db '(read failed)', 13, 10, '$'
s_ok:     db 'KDHELLO done', 13, 10, '$'
fname:    db 'KDDATA.TXT', 0
buf:      times 32 db 0   ; `resb` in a .COM's .text is a zeroing warning,
                          ; and a .COM carries its own image anyway
