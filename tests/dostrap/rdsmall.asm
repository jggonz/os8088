; =============================================================================
; os8088 - tests/dostrap/rdsmall.asm
;
; RDSMALL.COM - A SMALL READ, AND THE BYTES IT ACTUALLY GIVES BACK
;
;   nasm -f bin -w+error -o RDSMALL.COM tests/dostrap/rdsmall.asm
;   RDSMALL                 PRINCE.DAT, the name built in below
;   RDSMALL FILENAME.EXT    ...or one you type
;
; `RDSUM.COM` beside this one reads a whole file in 512-byte chunks and its
; checksum is exact under both arms - so the file layer DELIVERS.  What it
; cannot see is the read Prince of Persia actually makes: **six bytes**, at
; offset 0, immediately after the open.  Prince takes two words out of those
; six - an offset and a length - and seeks with them; given `DC 0A 00 00 F2
; 01` it seeks to 0x0ADC and reads 0x01F2, which is exactly the tail of the
; file.  Under `kern_dos` it seeked to 0x1733 and read 0x1728, neither of
; which is anywhere in the header, and then said *"Unable to find necessary
; files."*
;
; So this asks the smallest possible question in the sharpest possible way.
; Four reads, each printed as the bytes it returned and the AX it answered:
;
;   1  six bytes at offset 0, the call Prince makes;
;   2  six more, straight after - does the window CONTINUE correctly;
;   3  seek back to 0 and six again - does a re-read of the same span agree;
;   4  a 512-byte read from 0, of which the first six are printed - the size
;      RDSUM uses, so a disagreement between 4 and 1 is about the SIZE of the
;      read and not about the file.
;
; **AND EVERY READ IS INTO A POISONED BUFFER.**  The buffer is filled with EEh
; before the call and the SPAN it came back written is printed beside the
; count DOS reported.  A read that writes MORE than it was asked for is
; invisible in every other measurement - the answer is right, the bytes are
; right, and what is wrecked is the caller's next variable - and it is exactly
; the shape that leaves a program opening all of its files and then saying it
; cannot find them.
;
; It runs unchanged under a real DOS (docs/DOS-DEBUGGING.md's rule), so every
; line is a comparison rather than an assertion about ourselves.
; OURS, MIT with the rest of the tree.
; =============================================================================
    cpu 8086
    bits 16
    org 0x100

EXITC   equ 0x2C

start:
    mov [e_ss], ss                  ; **WHAT THE ARM HANDED US**, banked before
    mov [e_sp], sp                  ; anything of ours moves it: a program's
    mov [e_ds], ds                  ; own pointers are stack-relative, so an
    mov [e_cs], cs                  ; SP that differs between two arms shifts
    mov ax, [0x0002]                ; every buffer it computes
    mov [e_top], ax                 ; ...and PSP:0002 is the top of its block
    cmp byte [0x80], 0              ; no tail: the default below, so a double
    je .have                        ; click still names a file
    mov si, 0x81
.skip:
    lodsb
    cmp al, ' '
    je .skip
    cmp al, 9
    je .skip
    dec si
    mov di, name
.cp:
    lodsb
    cmp al, 13
    je .cpend
    cmp al, ' '
    je .cpend
    or al, al
    je .cpend
    mov [di], al
    inc di
    jmp short .cp
.cpend:
    mov byte [di], 0
.have:
    mov si, s_entry
    call puts
    mov ax, [e_cs]
    call hexw
    mov al, ':'
    call putc
    mov ax, [e_ds]
    call hexw
    mov si, s_ssp
    call puts
    mov ax, [e_ss]
    call hexw
    mov al, ':'
    call putc
    mov ax, [e_sp]
    call hexw
    mov si, s_top
    call puts
    mov ax, [e_top]
    call hexw
    call eol

    mov si, s_file
    call puts
    mov si, name
    call puts
    call eol

    mov dx, name
    xor al, al
    mov ah, 0x3D
    int 0x21
    jnc .open
    mov si, s_noopen
    call puts
    jmp .done
.open:
    mov [fh], ax

    mov si, s_r1
    call puts
    mov cx, 6
    call rd6

    mov si, s_r2
    call puts
    mov cx, 6
    call rd6

    mov si, s_r3
    call puts
    xor cx, cx                      ; seek to 0 from the start
    xor dx, dx
    mov bx, [fh]
    mov ax, 0x4200
    int 0x21
    mov cx, 6
    call rd6

    mov si, s_r4
    call puts
    mov cx, 6                       ; ...after another rewind, but 512 wide
    xor cx, cx
    xor dx, dx
    mov bx, [fh]
    mov ax, 0x4200
    int 0x21
    mov cx, 512
    call rd6

    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
.done:
    mov si, s_rdy
    call puts
    xor ah, ah
    int 0x16
    mov ax, 0x4C00 + EXITC
    int 0x21

; --- rd6 - read CX bytes into buf, print AX and the first six --------------
rd6:
    mov word [got], 0
    push cx                         ; **POISON FIRST** - 64 bytes of EEh, so
    push di                         ; the span the read really touched can be
    mov di, buf                     ; measured against the count it reported
    mov cx, 64
    mov al, 0xEE
    cld
    push es
    push ds
    pop es
    rep stosb
    pop es
    pop di
    pop cx
    mov dx, buf
    mov bx, [fh]
    mov ah, 0x3F
    int 0x21
    jnc .ok
    mov si, s_rerr
    call puts
    call hexw
    call eol
    ret
.ok:
    mov [got], ax
    mov si, s_ax
    call puts
    call hexw                       ; AX = bytes delivered
    mov si, s_by
    call puts
    ; --- the SPAN, which is the question this probe exists for --------------
    mov si, s_span
    call puts
    mov si, buf + 63                ; walk back from the end of the poison
    mov cx, 64
.sp:
    cmp byte [si], 0xEE
    jne .spdone
    dec si
    loop .sp
.spdone:
    mov ax, cx                      ; CX = how many bytes are NOT EEh...
    call hexw                       ; ...counting from the buffer's start
    mov si, s_by
    call puts

    mov si, buf
    mov cx, 6
.b:
    lodsb
    push ax
    mov al, [si-1]
    push ax
    mov al, [si-1]
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    call hexd
    mov al, ' '
    call putc
    pop ax
    loop .b
    call eol
    ret

; --- hexw - AX as four hex digits ------------------------------------------
hexw:
    push ax
    push ax
    mov al, ah
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    mov al, ah
    call hexd
    pop ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    pop ax
    call hexd
    ret

hexd:
    and al, 0x0F
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
    lodsb
    or al, al
    jz .o
    call putc
    jmp short puts
.o:
    ret

s_file:   db 'os8088 small-read gate - ', 0
s_r1:     db '1 six at 0   ', 0
s_r2:     db '2 six more   ', 0
s_r3:     db '3 rewind+six ', 0
s_r4:     db '4 rewind+512 ', 0
s_ax:     db 'AX=', 0
s_span:   db ' span=', 0
s_by:     db '  ', 0
s_noopen: db 'OPEN FAILED', 13, 10, 0
s_rerr:   db 'READ FAILED AX=', 0
s_rdy:    db 'READY - press a key to exit with code 44', 13, 10, 0
s_entry:  db 'ENTRY cs:ds=', 0
s_ssp:    db '  ss:sp=', 0
s_top:    db '  psp[2]=', 0
e_ss:     dw 0
e_sp:     dw 0
e_ds:     dw 0
e_cs:     dw 0
e_top:    dw 0
fh:       dw 0
got:      dw 0
name:     db 'PRINCE.DAT', 0
          times 8 db 0
buf:      times 512 db 0

; --- BALLAST (SPEC.md 96.11) -------------------------------------------------
; **THE PROGRAM'S OWN LOAD IS A READ THROUGH THE SAME WINDOW**, and what it
; leaves behind is state the first read after it inherits.  Prince of Persia is
; 126,304 bytes and this probe was 1,238, so the two were not asking the same
; question at all.  `-DBALLAST=n` pads the image to n bytes; the bytes are
; never executed and never read.
%ifdef BALLAST
    times BALLAST - ($ - $$) db 0
%endif
