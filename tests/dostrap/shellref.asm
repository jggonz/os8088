; =============================================================================
; os8088 - tests/dostrap/shellref.asm
;
; WHAT A `/C` SHELL-OUT ACTUALLY ANSWERS (SPEC.md 96.30), and it runs under a
; REAL IBM DOS unchanged - which is the whole point of tests/dostrap
; (docs/DOS-DEBUGGING.md): under DOS it drives the genuine COMMAND.COM and
; under us it drives apps/dos/dosh.inc, and the two RESULT.TXT files are
; diffed. An exit code we invented is a wrong answer nobody would notice.
;
; It writes one digit per check into RESULT.TXT and leaves the volume in a
; state the host walks afterwards - because the files are the real assertion:
; a COPY that reports success and writes nothing looks perfect from in here.
;
;   0  copy SRC.TXT DST.TXT          -> 0
;   1  del DST.TXT > NUL             -> 0
;   2  ren ONE.TXT ONE.BAK           -> 0
;   3  move TWO.TXT SUB              -> 0   (the host checks the CLUSTER)
;   4  copy *.BAK SUB                -> 0
;   5  frobnicate                    -> non-zero, and NOT a crash
;   6  copy NOSUCH.TXT X.TXT         -> non-zero
;   7  copy SRC.TXT > OUT.TXT        -> non-zero  (a target that is not NUL)
;   8  copy SHBIG.DAT SUB           -> non-zero: the volume RUNS OUT part way
;      through, and the host then asserts SUB holds no SHBIG.DAT at all. A
;      destination created and then failed is worse than none, because a short
;      file looks like a whole one - so the undo is the check, and it is the
;      one thing a hand-rolled copy classically gets wrong (96.30.6)
;
; A DIGIT AND NOT A PASS/FAIL, so a wrong code is visible rather than merely
; wrong: '0'..'9' is the code, '+' is any code above nine, and '-' is a check
; that never ran.
;
; NOTHING HERE IS THIRD-PARTY. Ours, MIT with the rest of the tree.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

NCHK        equ 9

start:
    mov [parm+4], ds                ; the tail's SEGMENT, which exists only at
    mov [parm+8], ds                ; run time - and both FCB pointers, which
    mov [parm+12], ds               ; DOS dereferences whether or not it cares

    mov cx, NCHK * 2                ; every row starts as a check that did not
    mov di, result                  ; run, so one that dies half way cannot
    mov al, '-'                     ; read as one that passed
    push ds
    pop es
    cld
    rep stosb

    ; --- SHRINK FIRST, as any program that EXECs must under DOS -----------
    ; A .COM owns the whole arena until it says otherwise, so a real
    ; COMMAND.COM has nowhere to load. Ours needs no block at all (96.30) and
    ; would work without this - which is exactly why it is here: the probe has
    ; to be the same program on both machines.
    mov bx, (stackend - start + 0x100 + 15) >> 4
    mov ah, 0x4A
    push ds
    pop es
    int 0x21

    mov si, c_copy1
    xor di, di
    call one
    mov si, c_del
    mov di, 1
    call one
    mov si, c_ren
    mov di, 2
    call one
    mov si, c_move
    mov di, 3
    call one
    mov si, c_wild
    mov di, 4
    call one
    mov si, c_bad
    mov di, 5
    call one
    mov si, c_nosrc
    mov di, 6
    call one
    mov si, c_redir
    mov di, 7
    call one
    mov si, c_full
    mov di, 8
    call one

    ; --- and publish it ---------------------------------------------------
    mov ah, 0x3C
    mov cx, 0
    mov dx, rname
    int 0x21
    jc .done
    mov bx, ax
    mov ah, 0x40
    mov cx, NCHK * 2 + 2
    mov dx, result
    int 0x21
    mov ah, 0x3E
    int 0x21
.done:
    mov ah, 0x09                    ; THE MARKER THE HOST POLLS FOR: the probe
    mov dx, s_done                  ; is fullscreen and prints nothing else, so
    int 0x21                        ; without it the only end-of-run signal is
    mov ax, 0x4C00                  ; a timeout
    int 0x21

; -----------------------------------------------------------------------------
; one - run the ASCIZ command at SI as `COMMAND.COM /c <it>`, digit into DI
; -----------------------------------------------------------------------------
one:
    push di
    mov di, tail + 1                ; the tail is a COUNTED string: a length
    xor cx, cx                      ; byte, the text, then a CR that is not
    mov word [di - 1], 0            ; counted
    mov byte [di], '/'
    mov byte [di+1], 'c'
    mov byte [di+2], ' '
    add di, 3
    add cx, 3
.cp:
    mov al, [si]
    or al, al
    jz .cpend
    mov [di], al
    inc di
    inc si
    inc cx
    cmp cx, 120
    jb .cp
.cpend:
    mov byte [di], 13
    mov [tail], cl

    mov ax, 0x4B00
    mov dx, cname
    mov bx, parm
    push ds
    pop es
    int 0x21
    pop di
    jc .failed                      ; the EXEC itself was refused: there is no
                                    ; shell at all, which is a different
                                    ; finding from a command that failed
    mov ah, 0x4D                    ; ...otherwise the CHILD's own code, as TWO
    int 0x21                        ; HEX DIGITS: an exit code is a byte, and a
    push ax                         ; single digit turns every code above nine
    shr al, 1                       ; into the same '+' - which is exactly the
    shr al, 1                       ; range a diagnostic one lands in
    shr al, 1
    shr al, 1
    call .hex
    mov [result+di], al
    pop ax
    call .hex
    mov [result+di+NCHK], al
    ret
.hex:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .h9
    add al, 7
.h9:
    ret
.failed:
    mov byte [result+di], 'X'
    mov byte [result+di+NCHK], 'X'
    ret

s_done:  db 13, 10, 'SHELLREF DONE', 13, 10, '$'
cname:   db 'COMMAND.COM', 0
rname:   db 'RESULT.TXT', 0

c_copy1: db 'copy SRC.TXT DST.TXT', 0
c_del:   db 'del DST.TXT > NUL', 0
c_ren:   db 'ren ONE.TXT ONE.BAK', 0
c_move:  db 'move TWO.TXT SUB', 0
c_wild:  db 'copy *.BAK SUB', 0
c_bad:   db 'frobnicate', 0
c_nosrc: db 'copy NOSUCH.TXT X.TXT', 0
c_redir: db 'copy SRC.TXT > OUT.TXT', 0
c_full:  db 'copy SHBIG.DAT SUB', 0

parm:    dw 0                       ; inherit the environment
         dw tail, 0                 ; the command tail, segment patched
         dw fcb1, 0
         dw fcb2, 0
fcb1:    times 16 db 0
fcb2:    times 16 db 0

result:  times NCHK * 2 db '-'    ; the high nibbles, then the low ones
         db 13, 10
tail:    times 130 db 0
         times 64 db 0
stackend:
