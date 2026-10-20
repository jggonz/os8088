; =============================================================================
; os8088 - tests/dosdir/dir.asm
;
; The wave-2 directory gate's DOS program (SPEC.md 96.12). It puts the four
; groups that landed with the file handles through their paces:
;
;   - AH=25h/35h, the interrupt vectors. It hooks INT 60h, reads it back, and
;     checks it got its own handler - the whole IVT is banked at bracket entry
;     and restored at the end, so a program may hook anything (SPEC.md 96.5);
;   - AH=1Ah/2Fh, the DTA, and AH=4Eh/4Fh over it. It counts `*.*`, then
;     `*.TXT`, then `?.TXT`, so a wildcard that matches everything and one
;     that matches by LENGTH both have to be right;
;   - AH=39h/3Bh/47h/3Ah - make a directory, stand in it, ask where we are,
;     come back, remove it. AH=47h is the '..' walk, and the answer proves it
;     because the name it must produce is one this program just chose;
;   - AH=19h, the current drive, which has to be B (1) and not A.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree, and
; it is under tests/ because it is not shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- 1. the current drive ----------------------------------------------
    mov ah, 0x19
    int 0x21
    add al, 'A'
    push ax
    mov ah, 0x09
    mov dx, msg_drv
    int 0x21
    pop ax
    call put_chr
    call put_crlf

    ; --- 1b. SELECTING one, which is the half that used to do nothing ------
    ; AH=0Eh answered the drive COUNT and never moved, so the standard idiom -
    ; select it, then ask AH=19h where you ended up - reported every drive as
    ; invalid (SPEC.md 96.6.1). A: is the system disk and is always there.
    mov ah, 0x0E
    xor dl, dl
    int 0x21
    mov ah, 0x19
    int 0x21
    push ax
    mov ah, 0x09
    mov dx, msg_sel
    int 0x21
    pop ax
    add al, 'A'
    call put_chr
    call put_crlf

    mov ah, 0x0E                    ; ...and BACK, so everything after this
    mov dl, 1                       ; still resolves on the gate disk
    int 0x21
    mov ah, 0x19
    int 0x21
    push ax
    mov ah, 0x09
    mov dx, msg_back
    int 0x21
    pop ax
    add al, 'A'
    call put_chr
    call put_crlf

    mov ah, 0x0E                    ; ...and a drive that is NOT there has to
    mov dl, 5                       ; leave us exactly where we were, which is
    int 0x21                        ; what makes the AH=19h above an answer
    mov ah, 0x19                    ; rather than an echo
    int 0x21
    push ax
    mov ah, 0x09
    mov dx, msg_nodrv
    int 0x21
    pop ax
    add al, 'A'
    call put_chr
    call put_crlf

    ; --- 1c. A VOLUME LABEL SEARCH must not answer with a FILE -------------
    ; AH=4Eh's CX is an attribute mask and this box used to ignore it, so a
    ; program asking "what is this disk called" was handed the first ordinary
    ; file on it. Prince of Persia asks exactly that to check it is running
    ; from its own floppy, and refused to start (SPEC.md 96.12.1).
    mov ah, 0x1A                    ; our own DTA, so the PSP's command tail
    mov dx, dta                     ; is not trampled by the search
    int 0x21
    mov ah, 0x4E
    mov cx, 0x0008                  ; ...the VOLUME LABEL and nothing else
    mov dx, pat_lbl
    int 0x21
    pushf
    mov ah, 0x09
    mov dx, msg_lbl
    int 0x21
    popf
    jc .nolabel
    mov si, dta + 30                ; ...whatever it handed back
    mov cx, 13
.lblc:
    mov al, [si]
    or al, al
    jz .lbldone
    call put_chr
    inc si
    loop .lblc
    jmp short .lbldone
.nolabel:
    mov ah, 0x09
    mov dx, msg_none
    int 0x21
.lbldone:
    call put_crlf

    ; --- 2. a vector, hooked and read back ---------------------------------
    mov ax, 0x2560
    mov dx, myvec
    int 0x21
    mov ax, 0x3560
    int 0x21                        ; ES:BX = what we just set
    mov ax, es
    mov cx, cs
    cmp ax, cx
    jne .vbad
    cmp bx, myvec
    jne .vbad
    mov ah, 0x09
    mov dx, msg_vok
    int 0x21
    jmp short .vdone
.vbad:
    mov ah, 0x09
    mov dx, msg_vbad
    int 0x21
.vdone:

    ; --- 3. find, three patterns -------------------------------------------
    mov dx, pat_all
    call count
    mov [n_all], ax
    mov dx, pat_txt
    call count
    mov [n_txt], ax
    mov dx, pat_one
    call count
    mov [n_one], ax

    mov ah, 0x09
    mov dx, msg_find
    int 0x21
    mov ax, [n_all]
    call put_dec16
    mov al, ' '
    call put_chr
    mov ax, [n_txt]
    call put_dec16
    mov al, ' '
    call put_chr
    mov ax, [n_one]
    call put_dec16
    call put_crlf

    ; --- 3a. the date and the time -----------------------------------------
    mov ah, 0x2A
    int 0x21                        ; CX = year, DH = month, DL = day, AL = dow
    push ax
    push cx
    push dx
    mov ah, 0x09
    mov dx, msg_date
    int 0x21
    pop dx
    pop cx
    push dx
    mov ax, cx
    call put_dec16
    mov al, '-'
    call put_chr
    pop dx
    push dx
    mov al, dh
    xor ah, ah
    call put_dec16
    mov al, '-'
    call put_chr
    pop dx
    mov al, dl
    xor ah, ah
    call put_dec16
    mov al, ' '
    call put_chr
    pop ax
    xor ah, ah
    call put_dec16                  ; ...and the day of the week
    call put_crlf

    mov ah, 0x2B                    ; a date that cannot be: DOS answers FFh
    mov cx, 2026
    mov dh, 13
    mov dl, 1
    int 0x21
    cmp al, 0xFF
    jne .dvbad
    mov ah, 0x09
    mov dx, msg_dvok
    int 0x21
    jmp short .dvdone
.dvbad:
    mov ah, 0x09
    mov dx, msg_dvbad
    int 0x21
.dvdone:

    mov ah, 0x2D                    ; set the clock, read it straight back
    mov ch, 13
    mov cl, 45
    mov dh, 30
    xor dl, dl
    int 0x21
    or al, al
    jnz .tvbad
    mov ah, 0x2C
    int 0x21
    push dx
    mov ah, 0x09
    mov dx, msg_time
    int 0x21
    pop dx
    push dx
    mov al, ch
    xor ah, ah
    call put_dec16
    mov al, ':'
    call put_chr
    mov al, cl
    xor ah, ah
    call put_dec16
    mov al, ':'
    call put_chr
    pop dx
    mov al, dh
    xor ah, ah
    call put_dec16
    call put_crlf
    jmp short .tvdone
.tvbad:
    mov ah, 0x09
    mov dx, msg_tvbad
    int 0x21
.tvdone:

    ; --- 4. mkdir / chdir / getcwd / chdir back / rmdir --------------------
    mov ah, 0x39
    mov dx, dname
    int 0x21
    jc .mdfail
    mov ah, 0x3B
    mov dx, dname
    int 0x21
    jc .cdfail
    mov ah, 0x09                    ; a find from IN HERE, which must see NOTHING:
    mov dx, msg_in                  ; the directory was made a moment ago, and
    int 0x21                        ; '.' and '..' are not reported to a package
    mov dx, pat_all                 ; at all (SPEC.md 96.12.2) - which is the
    call count                      ; fact that decided how AH=47h works
    call put_dec16
    call put_crlf

    mov ah, 0x47
    xor dl, dl
    mov si, cwdbuf
    int 0x21
    jc .cwfail
    mov ah, 0x09
    mov dx, msg_cwd
    int 0x21
    mov dx, cwdbuf
    call put_asciz
    call put_crlf
    mov ah, 0x3B
    mov dx, rootn
    int 0x21
    jc .cdfail
    mov ah, 0x3A
    mov dx, dname
    int 0x21
    jc .rdfail
    mov ah, 0x09
    mov dx, msg_dok
    int 0x21
    jmp short .done

.mdfail: mov dx, msg_emd
         jmp short .say
.cdfail: mov dx, msg_ecd
         jmp short .say
.cwfail: mov dx, msg_ecw
         jmp short .say
.rdfail: mov dx, msg_erd
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

myvec:
    iret

; -----------------------------------------------------------------------------
; count - how many files match the pattern at DX
count:
    push bx
    push cx
    push dx
    push si
    mov [pat], dx                   ; the pattern, banked BEFORE AH=1Ah takes
    mov ah, 0x1A                    ; DX for itself - a `pop dx` to get it back
    mov dx, dta                     ; pops whatever was pushed last, which was
    int 0x21                        ; SI
    mov word [cnt], 0
    mov dx, [pat]
    mov ah, 0x4E
    xor cx, cx
    int 0x21
    jc .out
    mov word [cnt], 1
.more:
    mov ah, 0x4F
    int 0x21
    jc .out
    inc word [cnt]
    jmp short .more
.out:
    mov ax, [cnt]
    pop si
    pop dx
    pop cx
    pop bx
    ret

put_asciz:
    push ax
    push si
    mov si, dx
.next:
    lodsb
    or al, al
    jz .done
    call put_chr
    jmp short .next
.done:
    pop si
    pop ax
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

put_dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    call put_chr
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
pat_all:  db '*.*', 0
pat_txt:  db '*.TXT', 0
pat_one:  db '?.TXT', 0
dname:    db 'SUBDIR', 0
rootn:    db '\', 0
pat:      dw 0
cnt:      dw 0
n_all:    dw 0
n_txt:    dw 0
n_one:    dw 0

msg_hi:   db 13,10,'os8088 DOS dir gate - DOSDIR.COM',13,10,13,10,'$'
msg_drv:  db 'DRIVE ','$'
msg_sel:   db 'SEL ','$'
msg_back:  db 'BACK ','$'
msg_nodrv: db 'NOSUCH ','$'
msg_lbl:   db 'LABEL ','$'
msg_none:  db '(none)','$'
pat_lbl:   db '????????.???', 0
msg_vok:  db 'VEC ok',13,10,'$'
msg_vbad: db 'VEC FAILED - the vector read back wrong',13,10,'$'
msg_find: db 'FIND ','$'
msg_in:   db 'IN ','$'
msg_date: db 'DATE ','$'
msg_time: db 'TIME ','$'
msg_dvok: db 'DVAL ok',13,10,'$'
msg_dvbad: db 'FAILED - an impossible date was accepted',13,10,'$'
msg_tvbad: db 'FAILED - AH=2Dh refused a legal time',13,10,'$'
msg_cwd:  db 'CWD ','$'
msg_dok:  db 'DIR ok',13,10,'$'
msg_emd:  db 'FAILED at mkdir',13,10,'$'
msg_ecd:  db 'FAILED at chdir',13,10,'$'
msg_ecw:  db 'FAILED at getcwd',13,10,'$'
msg_erd:  db 'FAILED at rmdir',13,10,'$'
msg_key:  db 13,10,'READY - press a key to exit with code 33',13,10,'$'

    align 16
cwdbuf:   times 68 db 0
dta:      times 64 db 0
