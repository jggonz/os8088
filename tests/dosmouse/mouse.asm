; =============================================================================
; os8088 - tests/dosmouse/mouse.asm
;
; The wave-2 mouse gate's DOS program (SPEC.md 96.10). A .COM that asks
; INT 33h the four questions a real DOS program asks it and prints every
; answer, so the harness can compare them against what it told the kernel's
; own pointer to do:
;
;   - AX=0   is there a mouse, and how many buttons (FFFFh / 2);
;   - AX=3   where is it and which buttons are down - the LEVEL read, which
;            is the one that has to be exact;
;   - AX=5   how many times has button 0 been pressed, and where - the EDGE
;            read, which a polled shim can only get right by accumulating on
;            every state read (SPEC.md 96.10.1);
;   - AX=6   ...and released.
;
; It STOPS AND WAITS FOR A KEY between steps, because the harness has to move
; the pointer between them and a program that polls in a loop gives it no
; window to do that in. That is also why the counts assert anything at all:
; the press and the release both happen while this program is blocked inside
; INT 21h AH=08h, so if the edges were only sampled when function 5 is CALLED
; they would both be gone by the time it asks.
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

    ; --- 1. reset ----------------------------------------------------------
    xor ax, ax
    int 0x33
    push bx
    push ax
    mov ah, 0x09
    mov dx, msg_rst
    int 0x21
    pop ax
    call put_hex16
    mov ah, 0x09
    mov dx, msg_bx
    int 0x21
    pop ax
    call put_dec16
    call put_crlf

    ; --- 2. two level reads, with the pointer moved in between -------------
    mov dx, msg_p1
    call prompt
    mov ax, 3
    int 0x33
    mov si, msg_pos1
    call put_pos

    mov dx, msg_p2
    call prompt
    mov ax, 3
    int 0x33
    mov si, msg_pos2
    call put_pos

    ; --- 3. the edges, which happened while we were blocked ----------------
    mov dx, msg_p3
    call prompt
    mov ax, 5                   ; presses of button 0 since the last ask
    xor bx, bx
    int 0x33
    push cx                     ; CX/DX = where it went down
    push dx
    push bx                     ; BX = the count
    mov ah, 0x09
    mov dx, msg_press
    int 0x21
    pop ax
    call put_dec16
    mov ah, 0x09
    mov dx, msg_x
    int 0x21
    pop ax                      ; y
    pop cx                      ; x
    push ax
    mov ax, cx
    call put_dec16
    mov ah, 0x09
    mov dx, msg_y
    int 0x21
    pop ax
    call put_dec16
    call put_crlf

    mov ax, 6                   ; ...and releases
    xor bx, bx
    int 0x33
    push bx
    mov ah, 0x09
    mov dx, msg_rel
    int 0x21
    pop ax
    call put_dec16
    call put_crlf

    ; --- 4. an unsupported function, which must LEAVE AX ALONE -------------
    ; **SPEC.md 96.10.6, and this block used to require the opposite.** It
    ; read `or ax, ax / jz .nofn` - AX had to come back ZERO - which was the
    ; box's behaviour until a zero answer cost it the whole mouse cursor:
    ; Microsoft Works ends its init `mov ax, 8 / int 33h / mov [98CAh], al`
    ; and reads that byte as `mouse present` (docs/FIELD-NOTES.md 54). INT 33h
    ; has no not-supported convention at all - no carry flag, no error code -
    ; so a function documenting no output comes back with the registers as
    ; they went in. The kernel was fixed and 96.10.6 written; THIS PROBE went
    ; on demanding the retired answer, and SPEC.md 96.10.2's own bullet still
    ; said zero, so the row cited a section that contradicted the one the code
    ; implements.
    ; **AND THE FUNCTION HAS TO BE ONE WITH NO ANSWER**, which is a second
    ; thing this block got wrong. It asked `AX=001Fh` - and 1Fh is DISABLE
    ; MOUSE DRIVER, which documents `AX = 001Fh` back and the previous handler
    ; in ES:BX, and which CuteMouse IMPLEMENTS. Put in front of IBM DOS 3.30
    ; with CuteMouse 1.9.1 this probe printed `FN CHANGED AX - the gate has
    ; FAILED` about a driver doing exactly the right thing, and it passed here
    ; only because this box falls through to `.none`. It was a gate that would
    ; go red the day 1Fh were implemented properly (docs/FIELD-NOTES.md 56).
    ;
    ; 90h is above every function any driver in this family defines - the
    ; classic set ends at 33h and the Logitech and Genius extensions in the
    ; 40s - so it is a question with no answer on BOTH machines, which is what
    ; the rule is about.
    mov ax, 0x0090              ; no driver defines this one
    int 0x33
    cmp ax, 0x0090              ; unchanged is the pass
    je .nofn
    mov ah, 0x09
    mov dx, msg_fnbad
    int 0x21
    jmp short .done
.nofn:
    mov ah, 0x09
    mov dx, msg_fnok
    int 0x21
.done:

    mov dx, msg_key
    call prompt

    mov ax, 0x4C21              ; exit 33, so the window stays and shows it
    int 0x21

; -----------------------------------------------------------------------------
; prompt - print DX$ and wait for a key
prompt:
    mov ah, 0x09
    int 0x21
    mov ah, 0x08
    int 0x21
    ret

; put_pos - SI = the "$"-terminated label; BX/CX/DX are INT 33h's answer
put_pos:
    push bx
    push cx
    push dx
    mov dx, si
    mov ah, 0x09
    int 0x21
    pop dx
    pop cx
    pop bx
    push bx
    push dx
    mov ax, cx
    call put_dec16
    mov ah, 0x09
    mov dx, msg_y
    int 0x21
    pop ax
    call put_dec16
    mov ah, 0x09
    mov dx, msg_b
    int 0x21
    pop ax
    call put_dec16
    call put_crlf
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

; put_hex16 - AX as four hex digits
put_hex16:
    push ax
    push bx
    push cx
    mov bx, ax
    mov cx, 4
.next:
    mov ax, bx
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    mov bx, ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call put_chr
    loop .next
    pop cx
    pop bx
    pop ax
    ret

; put_dec16 - AX (0..65535) as decimal, no leading zeros
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
msg_hi:    db 13,10,'os8088 DOS mouse gate - DOSMOUSE.COM',13,10,13,10,'$'
msg_rst:   db 'RESET ax=','$'
msg_bx:    db ' bx=','$'
msg_p1:    db 'P1 - press a key',13,10,'$'
msg_p2:    db 'P2 - press a key',13,10,'$'
msg_p3:    db 'P3 - press a key',13,10,'$'
msg_pos1:  db 'POS1 x=','$'
msg_pos2:  db 'POS2 x=','$'
msg_x:     db ' x=','$'
msg_y:     db ' y=','$'
msg_b:     db ' b=','$'
msg_press: db 'PRESS n=','$'
msg_rel:   db 'REL n=','$'
msg_fnok:  db 'FN90 left AX alone, as it should be',13,10,'$'
msg_fnbad: db 'FN90 CHANGED AX - the gate has FAILED',13,10,'$'
msg_key:   db 13,10,'READY - press a key to exit with code 33',13,10,'$'
