; RENREF.COM - what AH=56h (Rename) really answers.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; DOS uses this call for TWO things - rename a file, and MOVE one between
; directories of a volume by re-linking its entry - and os8088's
; OSAPI_FILE_RENAME is a directory-entry rewrite in the folder you are
; standing in (SPEC.md 18.4). So the question is not "does rename work", it is
; which of these DOS answers with an error and which it does silently, and
; what the codes are.
;
;   nasm -f bin -o RENREF.COM tests/dostrap/renref.asm
;
; It runs UNDER A REAL DOS UNCHANGED, and it WRITES - so the disk it runs from
; must be writable and is left with T2.TXT on it.
;
; A line reads   <what> AX=hhhh CF=n
; and the two rows that decide the implementation are the last two: a rename
; whose target already EXISTS, and one that names a different DRIVE.

    org 0x100
    cpu 8086

start:
    mov dx, f_t1                ; the file this is all about
    call make

    mov si, s_1
    mov dx, f_t1
    mov di, f_t2
    call try                    ; 1. the ordinary rename

    mov si, s_2
    mov dx, f_t1
    mov di, f_t3
    call try                    ; 2. ...of a file that is no longer there

    mov si, s_3
    mov dx, f_t2
    mov di, f_t2
    call try                    ; 3. ...onto its own name

    ; THE LETTERS ARE BUILT AT RUN TIME, and that is not tidiness: under a
    ; real DOS this runs from A: and under os8088 the package is launched off
    ; B:, so a hard-coded "B:" means "another drive" on one side and "the one
    ; I am on" on the other - and the two columns then disagree while both
    ; are right. Asking AH=19h makes every row below mean the same thing on
    ; both machines.
    mov ah, 0x19
    int 0x21
    push ax
    add al, 'A'
    mov [f_same], al
    pop ax
    xor al, 1                   ; THE OTHER FLOPPY, and the xor goes on the
    add al, 'A'                 ; drive NUMBER and not on its letter: 'A'^1 is
    mov [f_othr], al            ; '@', which is not a drive at all - and DOS
    mov [f_othn], al            ; answers 3 for it, which reads exactly like a
    mov [f_othx], al            ; finding about cross-drive renames

    mov si, s_4
    mov dx, f_same
    mov di, f_t4
    call try                    ; 4. the OLD name names THIS drive

    mov dx, f_t4                ; ...AND A FILE FOR EACH ROW THAT NEEDS ONE.
    call make                   ; They used to take their source from the row
    mov si, s_5                 ; above, so the first failure made every row
    mov dx, f_othr              ; after it "file not found" - a measurement of
    mov di, f_t4                ; the probe rather than of DOS
    call try                    ; 5. the OLD name names ANOTHER drive

    mov dx, f_t4
    call make
    mov si, s_6
    mov dx, f_t4
    mov di, f_othn
    call try                    ; 6. ...and the NEW one does

    mov dx, f_t4
    call make
    mov si, s_7
    mov dx, f_t4
    mov di, f_sub
    call try                    ; 7. a PATH as the new name: DOS's MOVE

    ; 8. THE SAME CROSS-DRIVE RENAME, but with the source really THERE. Rows
    ; 5 and 6 name a file on the other drive that does not exist, and DOS
    ; answers 3 - while an earlier shape of row 5, whose source DID exist,
    ; answered 11h. So the question is whether DOS looks the file up before it
    ; compares the drives, and this is the row that says.
    mov dx, f_othx
    call make
    mov si, s_8
    mov dx, f_othx
    mov di, f_t9
    call try

    mov si, s_done
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

; --- make - create (or truncate) the file named at DS:DX ---------------------
make:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jc .o
    mov bx, ax
    mov ah, 0x3E
    int 0x21
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- try - AH=56h with DS:DX and ES:DI, reported -----------------------------
try:
    push dx
    push di
    call puts                   ; SI is the label, and puts leaves it past NUL
    pop di
    pop dx
    push ds
    pop es                      ; ES:DI = the new name, in our own segment
    mov ah, 0x56
    int 0x21
    pushf
    push ax
    mov si, s_ax
    call puts
    pop ax
    call puthex
    mov si, s_cf
    call puts
    pop ax
    and al, 1
    add al, '0'
    call putc
    mov si, s_crlf
    jmp puts

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

f_t1:   db 'T1.TXT', 0
f_t2:   db 'T2.TXT', 0
f_t3:   db 'T3.TXT', 0
f_t4:   db 'T4.TXT', 0
f_same: db '?:T2.TXT', 0        ; '?' is overwritten with the drive we are ON
f_othr: db '?:T4.TXT', 0        ; ...and these two with the OTHER one
f_othn: db '?:T5.TXT', 0
f_othx: db '?:T7.TXT', 0        ; ...and this one is CREATED there first
f_t9:   db 'T9.TXT', 0
f_sub:  db '\T6.TXT', 0

s_1:    db 'rename        ', 0
s_2:    db 'gone          ', 0
s_3:    db 'onto itself   ', 0
s_4:    db 'old THIS drv  ', 0
s_5:    db 'old OTHER drv ', 0
s_6:    db 'new OTHER drv ', 0
s_7:    db 'new is a path ', 0
s_8:    db 'old OTHER,real', 0
s_ax:   db 'AX=', 0
s_cf:   db ' CF=', 0
s_crlf: db 13, 10, 0
s_done: db 'RENREF READY', 13, 10, 0
