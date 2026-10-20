; PARSEFCB.COM - what AH=29h (Parse Filename into FCB) really answers.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; Prince of Persia's INSTALL.EXE calls it twice on its way to AH=4Bh, and this
; box has no handler - so it falls to the "invalid function" arm and answers
; CF=1 with AX=0001.  DOS DOES NOT USE THE CARRY FOR THIS CALL AT ALL: it
; returns AL = 0, 1 or FFh, so a program reading AL is told "1 = the name had
; wildcards in it", which is a plausible answer to a question nobody asked.
; That is SPEC.md 96.22's defect shape for the fourth time in this box, and it
; is why the first thing to establish is what the real answer looks like.
;
;   nasm -f bin -o PARSEFCB.COM tests/dostrap/parsefcb.asm
;
; It runs UNDER A REAL DOS UNCHANGED, which is the point of everything in this
; directory: the reference answer is a machine, not a table in a document.
;
; A line reads   <input> AL=hh CF=n SI+=nn FCB=<drive><name><ext>
; where the drive byte is printed as a DIGIT (0 = the current drive, 1 = A)
; and the name and extension are the 11 bytes DOS wrote, spaces and all, so a
; parse that pads wrongly shows up as a shifted column rather than as nothing.

    org 0x100
    cpu 8086

start:
    mov si, tab
.next:
    cmp byte [si], 0
    je .done
    mov [pat], si
    call puts                       ; ...and `puts` leaves SI past its NUL
    mov [after], si
    mov si, [pat]
    mov cx, 14
    call pad

    mov di, fcb                     ; the FCB, wiped first so that a field DOS
    mov cx, 16                      ; does not touch reads as 00 rather than as
    xor al, al                      ; whatever the last row left
    cld
    rep stosb

    mov si, [pat]
    mov di, fcb
    push ds
    pop es                          ; ES:DI = the FCB, DS:SI = the name
    mov ax, 0x2901                  ; bit 0: skip leading separators
    int 0x21
    pushf
    push ax
    push si

    mov si, s_al
    call puts
    pop cx                          ; SI as DOS left it
    pop ax
    push cx
    call puthex2
    mov si, s_cf
    call puts
    pop cx
    pop ax                          ; the FLAGS
    push cx
    and al, 1
    call putdec
    pop cx

    mov si, s_si                    ; HOW FAR IT GOT, which is the half a
    call puts                       ; return code cannot say
    mov ax, cx
    sub ax, [pat]
    call puthex2

    mov si, s_fcb
    call puts
    mov al, [fcb]                   ; the drive byte, as a digit
    call putdec
    mov al, '['
    call putc
    mov si, fcb + 1                 ; ...and the 11 bytes, spaces and all
    mov cx, 11
.f:
    lodsb
    call putc
    loop .f
    mov al, ']'
    call putc

    mov si, s_crlf
    call puts
    mov si, [after]
    jmp .next
.done:
    mov si, s_done
    call puts
    xor ax, ax                      ; WAIT FOR A KEY, diskcost.asm's reason:
    int 0x16                        ; under os8088 the fsx bracket ends with
    mov ax, 0x4C00                  ; the program and the desktop comes back
    int 0x21

; --- pad the string at SI out to CX columns ---------------------------------
pad:
    lodsb
    or al, al
    jz .fill
    jcxz pad                        ; A STRING LONGER THAN THE COLUMN keeps
    dec cx                          ; walking to its NUL and pads by NOTHING:
    jmp short pad                   ; `dec cx` past zero wraps to 65,535 and
                                    ; `.fill` then prints that many spaces,
                                    ; which scrolls the whole run off the
                                    ; screen and reads as the probe crashing
.fill:
    jcxz .o
    mov al, ' '
    call putc
    loop .fill
.o:
    ret

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

putdec:
    and al, 0x0F
    add al, '0'
    jmp putc

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

pat:    dw 0
after:  dw 0
fcb:    times 16 db 0

s_al:   db 'AL=', 0
s_cf:   db ' CF=', 0
s_si:   db ' SI+=', 0
s_fcb:  db ' FCB=', 0
s_crlf: db 13, 10, 0
s_done: db 'PARSEFCB READY', 13, 10, 0

; EVERY SHAPE THE CALL IS ASKED, in one NUL-separated run: a bare name, one
; with a drive, one with wildcards, an extension-less one, a drive that is not
; there, leading blanks - and then the four that decide how the handler is
; WRITTEN rather than whether it answers: MIXED CASE (which is what Prince's
; installer actually passes - "B:Prince.exe" - and an FCB has to match a
; directory entry, which is upper), a name with an ARGUMENT after it (where
; SI is left is the whole answer), and a PATH, which 29h does not understand
; at all: it parses a NAME, and what it does with the separators is the part
; nobody can guess.
tab:
    db 'PRINCE.EXE', 0
    db 'B:PRINCE.EXE', 0
    db '*.*', 0
    db 'B:*.*', 0
    db 'PRINCE', 0
    db 'Z:PRINCE.EXE', 0
    db '  B:PRINCE.EXE', 0
    db 'B:Prince.exe', 0
    db 'prince.dat', 0
    db 'PRINCE.EXE ARG', 0
    db 'A:\DIR\PRINCE.EXE', 0
    db 0
