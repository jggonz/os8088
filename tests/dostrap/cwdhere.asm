; =============================================================================
; os8088 - tests/dostrap/cwdhere.asm
;
; CWDHERE.COM - WHERE DOES A LAUNCHED PROGRAM STAND?
;
;   nasm -f bin -w+error -o CWDHERE.COM tests/dostrap/cwdhere.asm
;
; A program launched out of a subdirectory stands IN that subdirectory, on
; every DOS there has ever been, and everything it opens by a bare name is
; resolved there (SPEC.md 96.6.1).  Prince of Persia is the field report this
; exists for: launched from `B:\PRINCE\` under the whole-machine arm it loaded
; and then asked for its disk, because `PRINCE.DAT` beside it was being looked
; for at the volume root.
;
; It prints three things and nothing through the BIOS:
;
;   1  AH=19h, the current DRIVE - the half the field report said was right;
;   2  AH=47h, the current DIRECTORY on it - the half it said was wrong.  DOS
;      answers WITHOUT a leading backslash and with none for a root, so a root
;      prints as `\` here and a subdirectory as `\PRINCE`;
;   3  AH=3Dh on a file that exists ONLY in the subdirectory, by a BARE name -
;      because a CWD that merely READS right and does not RESOLVE is the
;      failure that matters, and an open is the thing every program does;
;   4  THE PROGRAM'S OWN PATH out of the environment's tail, which DOS 3+ puts
;      after the terminating NUL and a count word (SPEC.md 96.19.3).  It is
;      the fourth because it is the one that caught Prince: the CWD can be
;      perfect and a program that builds its data path off ITS OWN PATH still
;      looks in the wrong folder - `dos_envpath`'s own comment says so about
;      the DRIVE half of the same string.
;
; It runs unchanged under a real DOS, which is docs/DOS-DEBUGGING.md's rule
; and what makes every line a comparison rather than an assertion about
; ourselves.  NOTHING HERE IS THIRD-PARTY: it is ours, MIT with the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

EXITC   equ 0x2B                 ; ...so a run that finished is not a run that
                                 ; fell over into a zero

start:
    mov dx, s_banner
    mov ah, 0x09
    int 0x21

    ; --- 1: the drive -------------------------------------------------------
    mov dx, s_drv
    mov ah, 0x09
    int 0x21
    mov ah, 0x19                 ; AL = 0 for A:
    int 0x21
    add al, 'A'
    mov dl, al
    mov ah, 0x02
    int 0x21
    mov dl, ':'
    mov ah, 0x02
    int 0x21
    call crlf

    ; --- 2: the directory ---------------------------------------------------
    mov dx, s_dir
    mov ah, 0x09
    int 0x21
    mov byte [cwd], 0            ; so a call that writes NOTHING is not read as
                                 ; a root - the two are different answers
    mov si, cwd
    xor dl, dl                   ; the CURRENT drive
    mov ah, 0x47
    int 0x21
    jc .direrr
    mov dl, '\'                  ; DOS answers without the leading one
    mov ah, 0x02
    int 0x21
    mov dx, cwd
    call say                     ; ...and with nothing at all for a root
    call crlf
    jmp short .open
.direrr:
    mov dx, s_direrr
    mov ah, 0x09
    int 0x21

    ; --- 3: ...and does a BARE NAME resolve there ---------------------------
.open:
    mov dx, s_open
    mov ah, 0x09
    int 0x21
    mov dx, f_here
    xor al, al                   ; read only
    mov ah, 0x3D
    int 0x21
    jc .noopen
    mov bx, ax
    mov ah, 0x3E                 ; ...and give the handle straight back: the
    int 0x21                     ; question is whether it RESOLVED
    mov dx, s_yes
    jmp short .said
.noopen:
    mov dx, s_no
.said:
    mov ah, 0x09
    int 0x21

    ; --- 4: ...and the path the environment says we came from ---------------
    mov dx, s_mine
    mov ah, 0x09
    int 0x21
    mov ax, [0x2C]
    or ax, ax
    jz .noenv
    mov es, ax
    xor di, di
.var:                            ; over the SET: each row is NUL-terminated
    cmp byte [es:di], 0          ; and a BARE NUL ends the lot, so the test
    je .atcount                  ; comes BEFORE the skip
.skip:
    mov al, [es:di]
    inc di
    or al, al
    jnz .skip
    jmp short .var
.atcount:
    inc di                       ; past the set's own NUL...
    add di, 2                    ; ...and the count word
.pc:
    mov al, [es:di]
    or al, al
    jz .pdone
    mov dl, al
    mov ah, 0x02
    int 0x21
    inc di
    jmp short .pc
.pdone:
    call crlf
    jmp short .fin
.noenv:
    mov dx, s_noenv
    mov ah, 0x09
    int 0x21
.fin:

    mov dx, s_ready
    mov ah, 0x09
    int 0x21
    xor ah, ah                   ; wait for a key, so a human and a harness
    int 0x16                     ; both get to read the screen
    mov ax, 0x4C00 + EXITC
    int 0x21

; --- say - the ASCIZ string at DX, through AH=02h ----------------------------
say:
    push si
    mov si, dx
.c:
    mov dl, [si]
    or dl, dl
    jz .done
    mov ah, 0x02
    int 0x21
    inc si
    jmp short .c
.done:
    pop si
    ret

crlf:
    mov dx, s_crlf
    mov ah, 0x09
    int 0x21
    ret

s_banner:  db 'os8088 CWD gate - CWDHERE.COM', 13, 10, '$'
s_drv:     db 'DRIVE   $'
s_dir:     db 'DIR     $'
s_direrr:  db '(AH=47h refused)', 13, 10, '$'
s_open:    db 'BARE    $'
s_yes:     db 'opened HERE.TXT beside me', 13, 10, '$'
s_no:      db 'COULD NOT OPEN HERE.TXT - the CWD is not where I was '
           db 'launched', 13, 10, '$'
s_mine:    db 'MYPATH  $'
s_noenv:   db '(no environment)', 13, 10, '$'
s_ready:   db 'READY - press a key to exit with code 43', 13, 10, '$'
s_crlf:    db 13, 10, '$'
f_here:    db 'HERE.TXT', 0

cwd:                             ; AH=47h's 64-byte answer, ASCIZ
