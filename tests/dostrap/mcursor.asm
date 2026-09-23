; MCURSOR.COM - does INT 33h DRAW the text cursor, and does it take it off?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.10.5.  A DOS mouse driver draws its own pointer - there is no
; compositor and no arrow the machine keeps for it - and this box answered
; 01h and 02h with a shrug. The rule is one line,
;
;       displayed = (cell AND screen_mask) XOR cursor_mask
;
; so the probe can put a KNOWN word in the cell and then say exactly what
; should be in it. It never has to see the screen; it reads the framebuffer
; back and judges the arithmetic.
;
;   A  the cell under the pointer is the INVERSE of what we wrote
;   B  ...and 02h puts our word back, byte for byte
;   C  the masks are STATE: a second 0Ah with 80FF/F000 repaints to those
;   D  the counter NESTS: hide, hide, show leaves it hidden
;   E  ...and the fourth call brings it back
;
; It runs under this box AND under a real DOS with a mouse driver loaded, so
; the answers are checkable against CTMOUSE. With no driver at all INT 33h is
; not installed, function 0 answers AX != FFFF, and it prints SKIP - which is
; the honest answer rather than a failure of ours.
    org 0x100
    cpu 8086

CELL    equ 0x0741                  ; 'A' in plain grey: chosen so that both
                                    ; mask pairs give a DIFFERENT answer and
                                    ; neither equals the cell itself
M1_S    equ 0x77FF                  ; the pair every driver powers up with
M1_C    equ 0x7700
M2_S    equ 0x80FF                  ; ...and the one Microsoft Works moves to
M2_C    equ 0xF000

start:
    mov ax, 0x0000                  ; is there a mouse at all?
    int 0x33
    cmp ax, 0xFFFF
    je .have
    mov si, s_skip
    call puts
    jmp .bye
.have:
    call seg_of                     ; ES = the text screen, BX = columns
    jnc .gotseg
    mov si, s_nosc
    call puts
    jmp .bye
.gotseg:
    mov [scrseg], es
    mov [cols], bx

    ; --- A: fill, show, and read the cell back --------------------------
    call fill                       ; EVERY cell, so wherever the pointer is
    mov ax, 0x000A                  ; the answer is the same
    xor bx, bx                      ; kind 0 = the SOFTWARE cursor
    mov cx, M1_S
    mov dx, M1_C
    int 0x33
    mov ax, 0x0001                  ; show
    int 0x33
    call cell                       ; -> AX = the word under the pointer
    mov [r_a], ax

    ; --- B: hide, and it must be exactly what we wrote -------------------
    mov ax, 0x0002
    int 0x33
    call cell
    mov [r_b], ax

    ; --- C: the masks are state, not a constant --------------------------
    call fill                       ; a fresh ground, so C cannot pass on A's
    mov ax, 0x0001
    int 0x33
    mov ax, 0x000A
    xor bx, bx
    mov cx, M2_S
    mov dx, M2_C
    int 0x33
    call cell
    mov [r_c], ax

    ; --- D and E: the counter NESTS --------------------------------------
    mov ax, 0x0002                  ; -1
    int 0x33
    mov ax, 0x0002                  ; -2
    int 0x33
    mov ax, 0x0001                  ; -1: still hidden
    int 0x33
    call cell
    mov [r_d], ax
    mov ax, 0x0001                  ; 0: back
    int 0x33
    call cell
    mov [r_e], ax
    mov ax, 0x0002                  ; ...and away, so the prints below are not
    int 0x33                        ; done over a live cursor
    call blank                      ; ...and on a clear screen
    mov ah, 0x02                    ; home the caret, which the fill did not
    mov dl, 13                      ; move
    int 0x21
    mov dl, 10
    int 0x21

    ; --- F: A FUNCTION WITH NO RETURN VALUE LEAVES AX ALONE --------------
    ; This is Microsoft Works's own test, instruction for instruction
    ; (SPEC.md 96.10.6): it sets the Y range and then stores AL as its
    ; `mouse present` flag. A box that answers 08h with a zero has told it
    ; there is no mouse at the END of an init it answered correctly - and the
    ; program then never asks for a cursor, while the event handler it already
    ; installed keeps working. Half a working mouse, which is how this
    ; survived a gate that only drove the drawing.
    mov cx, 0
    mov dx, 0x00C0                  ; Works's own arguments
    mov ax, 0x0008
    int 0x33
    mov [r_f], ax

    ; --- the verdict, which is arithmetic and not a photograph -----------
    mov si, s_a
    mov ax, [r_a]
    mov bx, (CELL & M1_S) ^ M1_C
    call judge
    mov si, s_b
    mov ax, [r_b]
    mov bx, CELL
    call judge
    mov si, s_c
    mov ax, [r_c]
    mov bx, (CELL & M2_S) ^ M2_C
    call judge
    mov si, s_d
    mov ax, [r_d]
    mov bx, CELL
    call judge
    mov si, s_e
    mov ax, [r_e]
    mov bx, (CELL & M2_S) ^ M2_C
    call judge
    mov si, s_f
    mov ax, [r_f]
    mov bx, 0x0008                  ; AX as it went in, not a zero
    call judge

    cmp byte [n_bad], 0
    jne .fail
    mov si, s_pass
    call puts
    jmp short .bye
.fail:
    mov si, s_fail
    call puts
.bye:
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; --- judge: SI = the label, AX = what was there, BX = what should be --------
judge:
    push ax
    push bx
    call puts
    pop bx
    pop ax
    push ax
    push bx
    call putw
    mov si, s_want
    call puts
    pop ax
    push ax
    call putw
    pop bx
    pop ax
    cmp ax, bx
    je .ok
    mov byte [n_bad], 1
    mov si, s_no
    jmp short .say
.ok:
    mov si, s_yes
.say:
    call puts
    mov si, s_nl
    call puts
    ret

; --- seg_of: the text screen, off the BDA the ROM keeps ---------------------
; out: ES = segment, BX = columns, CF=1 = not a text mode
seg_of:
    push ax
    push si
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x49]
    cmp al, 7
    je .mono
    cmp al, 3
    ja .no
    mov si, 0xB800
    jmp short .have
.mono:
    mov si, 0xB000
.have:
    mov bx, [es:0x4A]
    or bx, bx
    jz .no
    mov es, si
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret

; --- fill: CELL into every cell of the screen -------------------------------
; **IT USES `stosw` AND NOT THE BIOS**, which is what a DOS application does
; and is the whole point: a driver that only saw `int 10h` writes would pass
; a test written the other way and fail every real program.
fill:
    mov ax, CELL
    jmp short fillax
blank:
    mov ax, 0x0720                  ; ...and the same routine clears the ground
fillax:                             ; before the verdict is printed on it: a
    push ax                         ; screen of 'A' would bury the one thing
    push cx                         ; anybody reads
    push di
    push es
    push ax
    mov es, [scrseg]
    xor di, di
    mov ax, [cols]                  ; cols * 25, in the one multiply an 8086
    mov cl, 25                      ; has: `imul r,r,imm` is a 186
    mul cl
    mov cx, ax
    pop ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; --- cell: the word the driver should have touched --------------------------
; out: AX.  The cell is computed the way the DRIVER computes it - INT 33h's
; units are a 640x200 virtual screen whatever the text mode is, so a cell is
; 8 of them on both axes - which is what makes this a check of the drawing and
; not of the arithmetic we would otherwise be asserting against itself.
cell:
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, 0x0003
    int 0x33                        ; CX = x, DX = y
    mov si, cx
    mov ax, dx
    mov cl, 3
    shr si, cl
    shr ax, cl
    mov bx, [cols]
    mul bl
    add ax, si
    shl ax, 1
    mov si, ax
    mov es, [scrseg]
    mov ax, [es:si]
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

putw:
    push ax
    push cx
    push dx
    mov cx, 4
.digit:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 7
.out:
    mov dl, al
    push ax
    mov ah, 0x02
    int 0x21
    pop ax
    pop ax
    loop .digit
    pop dx
    pop cx
    pop ax
    ret

puts:
    push ax
    push dx
.one:
    mov dl, [si]
    or dl, dl
    jz .out
    mov ah, 0x02
    int 0x21
    inc si
    jmp short .one
.out:
    pop dx
    pop ax
    ret

s_a:     db 'A shown    ', 0       ; **EVERY LABEL ENDS IN A SPACE** and they
s_b:     db 'B hidden   ', 0       ; are all the same width: the host side
s_c:     db 'C remasked ', 0       ; splits this line on whitespace, and
s_d:     db 'D nested   ', 0       ; `remasked0741` is one token
s_e:     db 'E restored ', 0
s_f:     db 'F ax kept   ', 0
s_want:  db ' want ', 0
s_yes:   db ' ok', 0
s_no:    db ' BAD', 0
s_nl:    db 13, 10, 0
s_skip:  db 'MCURSOR SKIP - no INT 33h on this machine', 13, 10, 0
s_nosc:  db 'MCURSOR SKIP - not a text mode', 13, 10, 0
s_pass:  db 'MCURSOR PASS', 13, 10, 0
s_fail:  db 'MCURSOR FAIL', 13, 10, 0
scrseg:  dw 0
cols:    dw 0
r_a:     dw 0
r_b:     dw 0
r_c:     dw 0
r_d:     dw 0
r_e:     dw 0
r_f:     dw 0
n_bad:   db 0
