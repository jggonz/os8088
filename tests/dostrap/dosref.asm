; DOSREF.COM - put the questions os8088's DOS box has to answer to a DOS, and
; print what it said. OURS, MIT with the rest of the tree.
;
; THE POINT IS THAT ONE BINARY RUNS ON BOTH. A rule read out of a reference
; book is an argument; the same program run under IBM DOS 3.30 and under the
; box, printing the same table, is a measurement - and the two columns diff
; line for line with nothing to interpret.
;
; It asks about the calls where the two have been seen to disagree, and about
; the ones where a disagreement would be silent:
;
;   4E/4F  what a search that matched NOTHING answers, three ways: a name that
;          is not there, a DIRECTORY that is not there, and a VOLUME LABEL on
;          a disk that has none - which is the one Prince of Persia asks, and
;          which DOS answers 12h and this box answered 2h
;   4E/4F  ...and what an enumeration that RAN OUT answers, which is the code
;          the three above are so easily confused with
;   35     INT 33h, the mouse presence test every program of the era makes by
;          reading the vector rather than by calling anything
;   48     the memory probe: BX=FFFF is a question, not an allocation, and the
;          answer is in BX with CF set
;   30     the version, which decides a C runtime's whole personality
;   44     the five standard handles (SPEC.md 96.22.1)
;   19/36  where we are and how much room is left
;
; Run it on the drive under test - it searches the CURRENT directory.
    org 0x100
    cpu 8086

start:
    mov ah, 0x1A                    ; our own DTA, so the PSP's 80h stays the
    mov dx, dta                     ; command tail if anything wants to read it
    int 0x21

    ; --- 1. a name that is not there, in a directory that is ----------------
    mov si, s_q1
    call puts
    xor cx, cx
    mov dx, n_nosuch
    mov ah, 0x4E
    int 0x21
    call axcf

    ; --- 2. a DIRECTORY that is not there ------------------------------------
    mov si, s_q2
    call puts
    xor cx, cx
    mov dx, n_nodir
    mov ah, 0x4E
    int 0x21
    call axcf

    ; --- 3. the VOLUME LABEL, on a disk that may have none -------------------
    mov si, s_q3
    call puts
    mov cx, 0x0008
    mov dx, n_all
    mov ah, 0x4E
    int 0x21
    call axcf

    ; --- 4. an enumeration that RUNS OUT -------------------------------------
    mov si, s_q4
    call puts
    xor cx, cx
    mov dx, n_star
    mov ah, 0x4E
    int 0x21
    jc .q4done                      ; nothing at all: that answer is the one
    xor bp, bp
.q4:
    inc bp
    mov ah, 0x4F
    int 0x21
    jnc .q4
.q4done:
    call axcf
    mov si, s_n
    call puts
    mov ax, bp
    call hex4
    call eol

    ; --- 5. INT 33h, read rather than called ---------------------------------
    mov si, s_q5
    call puts
    mov ax, 0x3533
    int 0x21
    mov ax, es
    call hex4
    mov al, ':'
    call putc
    mov ax, bx
    call hex4
    call eol

    ; --- 6. the memory probe --------------------------------------------------
    mov si, s_q6
    call puts
    mov bx, 0xFFFF
    mov ah, 0x48
    int 0x21
    mov [sav_bx], bx                ; ...the ANSWER, which is in BX and not AX
    call axcf
    mov si, s_bx
    call puts
    mov ax, [sav_bx]
    call hex4
    call eol

    ; --- 7. the version --------------------------------------------------------
    mov si, s_q7
    call puts
    mov ah, 0x30
    int 0x21
    mov [sav_ax], ax
    mov [sav_bx], bx
    mov [sav_cx], cx
    mov ax, [sav_ax]
    call hex4
    mov al, '/'
    call putc
    mov ax, [sav_bx]
    call hex4
    mov al, '/'
    call putc
    mov ax, [sav_cx]
    call hex4
    call eol

    ; --- 8. the five standard handles -----------------------------------------
    mov si, s_q8
    call puts
    xor bx, bx
.ioc:
    push bx
    mov ax, 0x4400
    int 0x21
    jnc .iocok
    mov dx, 0xEEEE                  ; refused: say so rather than print stale DX
.iocok:
    mov ax, dx
    call hex4
    mov al, ' '
    call putc
    pop bx
    inc bx
    cmp bx, 5
    jb .ioc
    call eol

    ; --- 9. where we are, and how much room ------------------------------------
    mov si, s_q9
    call puts
    mov ah, 0x19
    int 0x21
    xor ah, ah
    call hex4
    mov si, s_bx
    call puts
    xor dx, dx
    mov ah, 0x36
    int 0x21
    mov [sav_ax], ax
    mov [sav_bx], bx
    mov [sav_cx], cx
    mov [sav_dx], dx
    mov ax, [sav_ax]
    call hex4                       ; AX = sectors per cluster
    mov al, '/'
    call putc
    mov ax, [sav_dx]
    call hex4                       ; DX = total clusters
    mov al, '/'
    call putc
    mov ax, [sav_cx]
    call hex4                       ; CX = bytes per sector
    mov al, '/'
    call putc
    mov ax, [sav_bx]
    call hex4                       ; BX = free clusters
    call eol

    ; IT WAITS, and that is not a courtesy to the reader. Under os8088 the
    ; DOS box's bracket ENDS when the program does and the desktop comes
    ; straight back, so a program that prints and exits leaves nothing on the
    ; screen to read - the answers were there for a few milliseconds. Under a
    ; real DOS the prompt would scroll them off in its own time.
    mov si, s_done
    call puts
    xor ax, ax
    int 0x16
    mov ax, 0x4C00
    int 0x21

; --- helpers -----------------------------------------------------------------
; axcf - "AX=xxxx CF=n", the shape every refusal is read in
; THROUGH STATICS AND NOT THE STACK. Everything below calls INT 21h to put a
; character out, so anything banked in a register is banked across a DOS call -
; and the one thing this program exists to find out is where the two DOSes
; treat a register differently. A static cannot be clobbered by the subject.
axcf:
    mov [sav_ax], ax
    pushf                           ; ...the CARRY, before a single `add`
    pop ax
    mov [sav_fl], ax
    mov si, s_ax
    call puts
    mov ax, [sav_ax]
    call hex4
    mov al, '/'
    call putc
    mov al, [sav_fl]
    and al, 1
    add al, '0'
    call putc
    call eol
    mov ax, [sav_ax]
    ret
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
hex2:
    push ax
    push cx
    push ax
    mov cl, 4
    shr al, cl
    call hexd
    pop ax
    and al, 0x0F
    call hexd
    pop cx
    pop ax
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe putc
    add al, 7
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp putc
puts:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret

s_done:   db 'DOSREF READY - press a key', 13, 10, 0
s_ax:     db ' AX/CF=', 0
s_bx:     db ' ', 0
s_n:      db '   entries=', 0
s_q1:     db '1 4E no-such-name  ', 0
s_q2:     db '2 4E no-such-dir   ', 0
s_q3:     db '3 4E volume label  ', 0
s_q4:     db '4 4E+4F ran out    ', 0
s_q5:     db '5 35 INT33 vector  ', 0
s_q6:     db '6 48 BX=FFFF probe ', 0
s_q7:     db '7 30 version AX    ', 0
s_q8:     db '8 44 handles 0-4   ', 0
s_q9:     db '9 19 drive / 36 spc/tot/bps/free ', 0

n_nosuch: db 'NOSUCH.XYZ', 0
n_nodir:  db 'NODIR\NOSUCH.XYZ', 0
n_all:    db '????????.???', 0
n_star:   db '*.*', 0
sav_ax:   dw 0
sav_bx:   dw 0
sav_cx:   dw 0
sav_dx:   dw 0
sav_fl:   dw 0
dta:      times 128 db 0
