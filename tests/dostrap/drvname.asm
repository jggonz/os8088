; DRVNAME.COM - does a DRIVE LETTER IN A NAME reach the drive it names?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; Prince of Persia's INSTALL.EXE walks off a cliff here: it stands on C:, asks
; AH=4Eh for its source files by a name that NAMES B:, is told 18 (no more
; files), and prints "Please insert Prince of Persia Disk in drive B:".  The
; DOSTRACE ring cannot answer why, because dos_fh_name banks the name AFTER
; stripping the prefix - so the one question a trace asks ("was there a drive
; letter to lose?") is the one it has already thrown away.  This asks it from
; the other side, by running the pattern itself.
;
;   nasm -f bin -o DRVNAME.COM tests/dostrap/drvname.asm
;
; It runs UNDER A REAL DOS UNCHANGED, which is the point of everything in this
; directory: the reference answer is a machine, not a table in a document.
;
; A line reads    <pattern> AX=hhhh CF=n CUR=n FOUND=<name>
; and THREE of those five matter:
;   AX/CF   - 0012h is "no more files", which is what a search of the wrong
;             directory looks like from the program's side
;   FOUND   - WHICH file, so "it found something" and "it found the right
;             thing" are different answers.  A pattern naming another drive
;             that comes back with a name from THIS one is the defect, and it
;             reports as a SUCCESS to every other instrument
;   CUR     - the drive DOS says we are standing on AFTERWARDS.  A name that
;             names a drive must NOT move us: under DOS the letter selects
;             which drive's current directory the name is resolved against,
;             and the default drive is AH=0Eh's business alone.  Getting the
;             search right by moving is a second defect wearing the fix
;
; Run it on a machine with a floppy in A: (and, if there is one, B:) and a
; hard disk, from whichever drive you like - it prints where it started.

    org 0x100
    cpu 8086

start:
    mov ah, 0x19                    ; where we are before anything: every CUR
    int 0x21                        ; below is read against this
    mov [home], al
    mov si, s_cur
    call puts
    mov al, [home]
    call putdec
    mov si, s_crlf
    call puts

    mov si, tab
.next:
    cmp byte [si], 0
    je .done
    mov [pat], si                   ; the pattern, printed and then passed

    call puts                       ; ...and `puts` leaves SI past its NUL,
    mov [after], si                 ; which is the next row

    mov si, [pat]
    mov cx, 12
    call pad                        ; a column, so the answers line up

    mov dx, [pat]
    xor cx, cx                      ; attribute 0: ordinary files only, which
    mov ah, 0x4E                    ; is what an installer looks for
    int 0x21
    pushf
    push ax

    mov si, s_ax
    call puts
    pop ax
    call puthex
    mov si, s_cf
    call puts
    pop ax                          ; the FLAGS, pushed by pushf
    and al, 1
    call putdec

    mov si, s_cur2                  ; DID THE SEARCH MOVE US?  This is the
    call puts                       ; half a "did it find the file" test
    mov ah, 0x19                    ; misses entirely
    int 0x21
    call putdec

    mov si, s_found                 ; the DTA's own answer at +30, which says
    call puts                       ; which file rather than how many
    mov si, 0x80 + 30
    call puts

    mov si, s_crlf
    call puts
    mov si, [after]
    jmp short .next
.done:
    ; --- AND THE SAME LETTERS THROUGH THE CALLS THAT TAKE A PATH -----------
    ; SPEC.md 96.6's table says a handle call on a lettered path answers 0Fh,
    ; which is a reasonable thing to believe and is not what the machine does
    ; for AH=4Eh.  So each family is asked separately rather than assumed to
    ; share an answer.
    mov si, tab2
.n2:
    cmp byte [si], 0
    je .done2
    mov [pat], si
    call puts
    mov [after], si
    mov si, [pat]
    mov cx, 12
    call pad

    mov dx, [pat]
    mov ax, 0x3D00                  ; OPEN for reading
    int 0x21
    pushf
    push ax
    mov si, s_op
    call puts
    pop ax
    call puthex
    mov si, s_cf
    call puts
    pop ax
    and al, 1
    call putdec

    mov dx, [pat]
    mov ah, 0x3B                    ; CHDIR - a path call rather than a file
    int 0x21                        ; one, and DOS does not always agree with
    pushf                           ; itself across that line
    push ax
    mov si, s_cd
    call puts
    pop ax
    call puthex
    mov si, s_cf
    call puts
    pop ax
    and al, 1
    call putdec

    mov si, s_crlf
    call puts
    mov si, [after]
    jmp short .n2
.done2:
    ; --- AND A HANDLE, WHICH IS THE HALF A PATTERN CANNOT REACH -------------
    ; A handle here is a NAME, re-resolved at every window, so a read of a
    ; file on A: taken while standing on B: is the case that breaks without
    ; FH_VOL.  TWO of them, alternating, because one handle never makes
    ; dos_fh_take steal the window - and the steal is where a cross-volume
    ; flush happens.
    mov dx, f_a
    mov ax, 0x3D00
    int 0x21
    mov [ha], ax
    jnc .oa
    mov word [ha], 0xFFFF
.oa:
    mov dx, f_b
    mov ax, 0x3D00
    int 0x21
    mov [hb], ax
    jnc .ob
    mov word [hb], 0xFFFF
.ob:
    mov si, s_ha
    call puts
    mov ax, [ha]                    ; the OPEN's own answer first: "NONE" alone
    call puthex                     ; cannot say whether the open or the read
    mov al, '/'                     ; was what failed
    call putc
    mov bx, [ha]
    call rd16
    mov si, s_crlf
    call puts

    mov si, s_hb
    call puts
    mov ax, [hb]
    call puthex
    mov al, '/'
    call putc
    mov bx, [hb]
    call rd16
    mov si, s_crlf
    call puts

    mov si, s_ha2                   ; ...and A: AGAIN, after B: has taken the
    call puts                       ; window: this is the alternation
    mov bx, [ha]
    call rd16
    mov si, s_crlf
    call puts

    mov si, s_done
    call puts
    xor ax, ax                      ; WAIT FOR A KEY, diskcost.asm's reason:
    int 0x16                        ; under os8088 the fsx bracket ends with
    mov ax, 0x4C00                  ; the program and the desktop comes back
    int 0x21

; --- the first 8 bytes of handle BX, as hex; 'NONE' if it never opened -------
rd16:
    cmp bx, 0xFFFF
    je .none
    push bx
    mov ax, 0x4200                  ; rewind: this is called twice on one
    xor cx, cx                      ; handle and the second read must see the
    xor dx, dx                      ; same bytes as the first
    int 0x21
    pop bx
    push bx
    mov dx, buf
    mov cx, 8
    mov ah, 0x3F
    int 0x21
    pop bx
    pushf                           ; THE READ'S OWN ANSWER, because "NONE"
    push ax                         ; covers three different failures and the
    call puthex                     ; question is which
    mov al, ':'
    call putc
    pop ax
    popf
    pushf
    push ax
    mov al, 0
    adc al, 0
    call putdec
    mov al, '/'
    call putc
    pop ax
    popf
    jc .none
    mov [got], ax
    or ax, ax
    jz .none
    mov cx, ax
    mov si, buf
.d:
    lodsb
    mov ah, 0
    push cx
    push si
    call puthex2
    pop si
    pop cx
    loop .d
    ret
.none:
    mov si, s_none
    jmp puts

; --- AL as two hex digits ---------------------------------------------------
puthex2:
    push ax
    mov cl, 4
    shr al, cl
    call .nyb
    pop ax
    and al, 0x0F
.nyb:
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o:
    jmp putc

; --- pad the string at SI out to CX columns ---------------------------------
pad:
    lodsb
    or al, al
    jz .fill
    dec cx
    jmp short pad
.fill:
    jcxz .o
    mov al, ' '
    call putc
    loop .fill
.o:
    ret

; --- AX as four hex digits ---------------------------------------------------
puthex:
    push cx
    mov cx, 4
.d:
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    call putc
    pop ax
    loop .d
    pop cx
    ret

; --- AL as decimal, 0..9 is all this needs ----------------------------------
putdec:
    and al, 0x0F
    add al, '0'
    call putc
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
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

home:   db 0
pat:    dw 0
after:  dw 0

s_cur:   db 'CUR=', 0
s_ax:    db 'AX=', 0
s_cf:    db ' CF=', 0
s_cur2:  db ' CUR=', 0
s_found: db ' FOUND=', 0
ha:      dw 0
hb:      dw 0
got:     dw 0
buf:     times 16 db 0

f_a:     db 'A:AONLY.TXT', 0
f_b:     db 'B:BONLY1.TXT', 0

s_ha:    db 'HA  =', 0
s_hb:    db 'HB  =', 0
s_ha2:   db 'HA2 =', 0
s_none:  db 'NONE', 0
s_op:    db 'OPEN=', 0
s_cd:    db ' CHDIR=', 0
s_crlf:  db 13, 10, 0
s_done:  db 'DRVNAME READY', 13, 10, 0

; The patterns, in one NUL-separated run so the loop needs no table of
; pointers.  EVERY SHAPE AN INSTALLER WRITES: bare, drive-relative (no
; separator - "the current directory OF that drive"), and drive-absolute.
tab:
    db '*.*', 0
    db 'A:*.*', 0
    db 'A:\*.*', 0
    db 'B:*.*', 0
    db 'B:\*.*', 0
    db 'C:*.*', 0
    db 'C:\*.*', 0
    db 0

; The same letters through AH=3Dh and AH=3Bh.  A name that EXISTS on the
; system disk and a bare root, so "the drive is not there" is the only thing
; left to answer.
tab2:
    db 'A:\', 0
    db 'B:\', 0
    db 'C:\', 0
    db 0
