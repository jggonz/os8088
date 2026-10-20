; VECS.COM - THE VECTORS A REAL DOS OWNS (SPEC.md 96.5.2).  MIT with the tree.
;
; ONE BINARY, TWO DOSES - dosref.asm's shape and docs/DOS-DEBUGGING.md's
; method.  "DOS installs the whole 20h..2Fh block" is a sentence out of a
; reference book; the same program printing the same five lines under IBM DOS
; 3.30 and under the box is a measurement, and the finding is the DIFF.
;
; It exists because the box shipped with fourteen of DOS's own vectors at
; 0000:0000, which is not "unimplemented" - an `int` through a null vector
; EXECUTES THE VECTOR TABLE, and a program cannot test for it beforehand
; because the probe IS the call.  BOLOBALL asked `int 2Ah AH=00h` (is a
; network redirector loaded?) three instructions after a version check we
; answered correctly, and ran off into the IVT with SP walking down two bytes
; a lap (SPEC.md 96.5.2.1).
;
; THE FIVE LINES
;
;   NUL=...   every vector of DOS's own block still 0000:0000.  NONE is the
;             pass, and it is what IBM DOS 3.30 prints.
;   2A=..     `int 2Ah AH=00h` and then KEEP GOING.  Printing this line at all
;             is the finding; AH back is the answer (0 = no network) and also
;             says the handler is an iret rather than something that scribbles.
;   29=[x]    `int 29h AL='*'` - fast console output, which DOS's own CON
;             driver writes through.  An `iret` there is SILENCE rather than a
;             crash, which is the harder bug, so the character has to appear.
;   25=...    `int 25h` - absolute disk read, drive A, sector 0.  It SUCCEEDS
;             under a real DOS and is refused here, so CF and AX are not
;             comparable and are printed rather than judged.  **SPD IS**: these
;             two calls are the only ones of the era that do not `iret`, DOS
;             leaving the FLAGS the INT pushed on the stack for the caller to
;             pop, so the caller's `add sp,2` balances only if the handler
;             returned with a `retf`.  SPD=0000 on both machines or the
;             handler is wrong (SPEC.md 96.5.2.2).
;   EXIT=..   it leaves through `INT 21h AH=00h` with a NON-ZERO AL, because
;             AH=00h's exit code is ZERO and AL is an argument to no part of
;             it.  The host reads the code the box recorded; 96.5.2.3 is the
;             field's `Exit code 002` coming from exactly this.
;
; It waits for a key before exiting - not a courtesy: the box's fsx bracket
; ends when the program does and the desktop comes straight back.
    org 0x100
    cpu 8086

EXITAL  equ 0x42                    ; a loud AL for the exit: the code must be 0

start:
    mov si, s_head
    call puts

; --- 1. which of DOS's own block are still NULL ------------------------------
    mov si, s_nul
    call puts
    mov word [nnul], 0
    mov si, tab
    xor ax, ax
    mov es, ax
.vn:
    mov al, [si]
    inc si
    or al, al
    jz .vd
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov cx, [es:bx]
    or cx, [es:bx+2]
    jnz .vn                         ; installed - say nothing
    inc word [nnul]
    call hex2                       ; AL is still the vector number
    mov al, ' '
    call putc
    jmp short .vn
.vd:
    cmp word [nnul], 0
    jne .vnl
    mov si, s_none
    call puts
.vnl:
    call eol

; --- 2. INT 2Ah, and then KEEP GOING -----------------------------------------
    mov si, s_2a
    call puts
    xor ah, ah                      ; AH=00h: is a network redirector loaded?
    mov al, 0x5A
    int 0x2A
    mov [bank], ax                  ; ...and being HERE is the finding
    mov al, [bank+1]
    call hex2
    call eol

; --- 3. INT 29h - fast console output ----------------------------------------
    mov si, s_29
    call puts
    mov al, '*'
    int 0x29
    mov al, ']'
    call putc
    call eol

; --- 4. INT 25h - and the stack it leaves behind ------------------------------
    mov si, s_25
    call puts
    mov [sp0], sp
    mov al, 0                       ; drive A
    mov bx, buf                     ; DS:BX = the buffer
    mov cx, 1                       ; one sector
    mov dx, 0                       ; ...the first
    int 0x25
    pushf                           ; **CF FIRST, BECAUSE `add sp,2` SETS IT**
    pop cx                          ; - the obvious order reads the ADD's own
    mov [flg], cx                   ; carry and calls every DOS a success
    mov [bank], ax                  ; (`mov` touches no flag, so these are safe
    add sp, 2                       ; between).  DOS LEAVES THE FLAGS ON THE
    mov [sp1], sp                   ; STACK: this is the BALANCE, not the value
    mov al, 'C'
    call putc
    mov al, 'F'
    call putc
    mov al, [flg]
    and al, 1
    add al, '0'
    call putc
    mov si, s_ax
    call puts
    mov ax, [bank]
    call hex4
    mov si, s_spd
    call puts
    mov ax, [sp0]
    sub ax, [sp1]
    call hex4
    call eol

; --- 5. a key, then out through AH=00h ---------------------------------------
    mov si, s_key
    call puts
.k:
    mov ah, 0x08
    int 0x21
    mov ax, EXITAL                  ; AH=00h, AL LOUD: the code must be 000
    int 0x21

; --- the vectors a real IBM DOS 3.30 fills ------------------------------------
; Measured off the machine, not listed out of a book.  1Fh and everything from
; 40h up ARE zero under DOS, so they are not here: an unused vector being zero
; is DOS's own rule and this box keeps it.
tab:
    db 0x20, 0x21, 0x22, 0x23, 0x24 ; the ones that were always installed
    db 0x25, 0x26, 0x27, 0x28, 0x29
    db 0x2A, 0x2B, 0x2C, 0x2D, 0x2E
    db 0x2F
    db 0x32
    db 0x33
    db 0x34, 0x35, 0x36, 0x37, 0x38
    db 0x39, 0x3A, 0x3B, 0x3C, 0x3D
    db 0x3E
    db 0

s_head: db 'VECS 1', 13, 10, 0
s_nul:  db 'NUL=', 0
s_none: db 'NONE', 0
s_2a:   db '2A=', 0
s_29:   db '29=[', 0
s_25:   db '25=', 0
s_ax:   db ' AX=', 0
s_spd:  db ' SPD=', 0
s_key:  db 'KEY', 13, 10, 0

; --- output -------------------------------------------------------------------
putc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
puts:
    push ax
.l:
    mov al, [si]
    inc si
    or al, al
    jz .o
    call putc
    jmp short .l
.o:
    pop ax
    ret
eol:
    mov al, 13
    call putc
    mov al, 10
    jmp short putc
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
    jmp short putc

; --- bss ----------------------------------------------------------------------
; A .COM owns every byte after its image, so nothing here is EMITTED: a bigger
; file costs more to load, and this program is measured by what it prints.
nnul:   equ $
bank:   equ $ + 2
sp0:    equ $ + 4
sp1:    equ $ + 6
flg:    equ $ + 8
buf:    equ $ + 16
