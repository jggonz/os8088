; DOSTRAP.COM - log every INT 21h under a REAL DOS, in os8088's own trace
; format (SPEC.md 96.22.1). OURS, MIT with the rest of the tree.
;
; WHY THIS EXISTS. A DOS program that behaves differently under this box and
; under real DOS is not debuggable from one side: our trace says what we were
; asked and what we answered, and both look right. What is missing is what
; DOS answers to the SAME questions, and the only place that exists is on a
; machine running DOS. So this hooks INT 21h there and writes a TRACE.LOG in
; the identical layout, and the two files diff line for line.
;
;   DOSTRAP            install, then run the program under test
;   DOSTRAP /D         write TRACE.LOG from the ring and stay installed
;
; It logs AX BX CX DX on the way in and AX/CF on the way out, 16 bytes an
; entry, and filters the console writers exactly as the box does - a
; hundred-character message is a hundred entries of noise standing where the
; calls that caused it should be.
    org 0x100
    cpu 8086

ENTSZ   equ 32                      ; bytes an entry - the same layout the box's
                                    ; own ring uses (apps/dos/dos.asm's
                                    ; DOS_TRACE_SZ), because the whole point of
                                    ; this program is that one reader decodes
                                    ; both
SEQ33   equ 96                      ; ...and the first 96 calls IN ORDER, which
                                    ; is what a histogram cannot say: that a
                                    ; program asked for four functions and then
                                    ; STOPPED (SPEC.md 96.10.3.2)
NENT33  equ 32                      ; INT 33h functions counted, and the size of
SZ33    equ 8                       ; a slot: count, then BX/CX/DX at the last
                                    ; call. THE SAME SHAPE AS THE BOX'S
                                    ; (SPEC.md 96.10.3.1), so one host-side
                                    ; reader decodes both sides of a diff
NENT    equ 512                     ; entries, 16KB. IT KEEPS THE FIRST ONES AND
                                    ; STOPS: this exists to find where two runs
                                    ; DIVERGE, and divergence is early - a ring
                                    ; that wrapped would throw away the only
                                    ; part that matters and keep the steady
                                    ; state, which is the same on both sides

start:
    mov si, 0x81                    ; the command tail
.sw:
    lodsb
    cmp al, 13
    je .install
    cmp al, '/'
    jne .sw
    lodsb
    or al, 0x20
    cmp al, 'd'
    jne .sw
    jmp dump

.install:
    mov ax, 0x3521                  ; the old handler, kept for the chain
    int 0x21
    mov [old21], bx
    mov [old21+2], es
    mov ax, es
    or ax, bx
    jz .nodos

    mov dx, new21
    mov ax, 0x2521
    int 0x21

    ; --- ...AND INT 33h, WHICH IS THE OTHER HALF OF A COMPARISON (96.10.3.1)
    ; The box records which mouse functions a program asks for and with what;
    ; without the same record from a REAL driver there is nothing to align it
    ; against, and a difference in the program's own behaviour reads as a
    ; defect in the box underneath. Load the driver BEFORE this TSR and the
    ; chain is program -> here -> the real driver.
    ;
    ; A VECTOR OF ZERO IS NOT AN ERROR HERE: a machine with no mouse driver is
    ; one this records nothing on and traces perfectly well otherwise, so the
    ; hook goes in either way and `new33` refuses to chain into nothing.
    mov ax, 0x3533
    int 0x21
    mov [old33], bx
    mov [old33+2], es
    mov dx, new33
    mov ax, 0x2533
    int 0x21

    mov si, s_on
    call puts
    mov dx, resident_end            ; ...and stay, keeping the ring
    add dx, 15
    mov cl, 4
    shr dx, cl
    mov ax, 0x3100
    int 0x21
.nodos:
    mov si, s_nodos
    call puts
    mov ax, 0x4C01
    int 0x21

; --- the MOUSE hook ---------------------------------------------------------
; One saturating byte per function and the arguments of its last call, which is
; exactly the box's own shape (SPEC.md 96.10.3.1) so the two decode with one
; reader. It records BEFORE chaining, because what is wanted is what the
; PROGRAM asked - a driver that rewrites a register on the way out is telling
; us something else.
new33:
    push ax
    push bx
    push si
    push ds
    push cs
    pop ds
    mov si, ax
    cmp si, NENT33                  ; anything above lands in the top bucket,
    jb .in                          ; which is a reading and not a wild store
    mov si, NENT33 - 1
.in:
    shl si, 1                       ; ...times SZ33, which is 8: three shifts
    shl si, 1                       ; of ONE, the only shift an 8086 has
    shl si, 1
    add si, m33
    cmp byte [si], 0xFF             ; saturating, so a poll loop cannot wrap
    je .arg                         ; the count round to zero and read as
    inc byte [si]                   ; "never called"
.arg:
    mov [si+2], bx
    mov [si+4], cx
    mov [si+6], dx
    mov si, [m33n]                  ; ...AND THE ORDER (SPEC.md 96.10.3.2), the
    cmp si, SEQ33                   ; same 96 bytes the box keeps, so one
    jae .done                       ; reader decodes both
    mov [si+m33seq], al
    inc si
    mov [m33n], si
.done:
    pop ds
    pop si
    pop bx
    pop ax
    cmp word [cs:old33+2], 0        ; NO DRIVER: answer AX=0, INT 33h's own
    jne .chain                      ; "not supported", rather than jumping
    xor ax, ax                      ; through a zeroed vector into the IVT
    iret
.chain:
    jmp far [cs:old33]

; --- the hook ---------------------------------------------------------------
new21:
    ; ARM ON THE EXEC, which is what makes the ring the PROGRAM's and not the
    ; shell's. COMMAND.COM spends hundreds of calls on its own prompt, so a
    ; ring that starts at install time is full of somebody else's work before
    ; the subject has been loaded. The first AH=4Bh after installation IS the
    ; subject being loaded.
    cmp byte [cs:armed], 0
    jne .live
    cmp ah, 0x4B
    jne .chain
    mov byte [cs:armed], 1
    jmp .chain
.live:
    cmp ah, 0x02
    je .chain
    cmp ah, 0x09
    je .chain
    cmp ah, 0x06
    je .chain

    cmp word [cs:total], NENT
    jae .chain                      ; full: the first NENT are the answer
    push bp
    push si
    push ax
    mov si, [cs:wr]
    add si, ring
    mov [cs:here], si
    pop ax
    push ax
    mov [cs:si], ax
    mov [cs:si+2], bx
    mov [cs:si+4], cx
    mov [cs:si+6], dx
    mov word [cs:si+8], 0xFFFF
    mov word [cs:si+10], 0xFFFF
    mov word [cs:si+12], 0xFFFF
    mov word [cs:si+14], 0xFFFF

    ; --- WHO CALLED. The `int 21h` pushed FLAGS, CS and IP, and this handler
    ; has since pushed BP, SI and AX - so the caller's frame is six bytes up
    ; from where BP now points. CS is recorded raw and the reader subtracts the
    ; PSP, so an offset compares across two machines that loaded the program at
    ; different addresses.
    mov bp, sp
    mov ax, [bp+6]                  ; +0 AX, +2 SI, +4 BP, then IP
    mov [cs:si+16], ax
    mov ax, [bp+8]
    mov [cs:si+18], ax              ; ...and CS
    mov ax, ds
    mov [cs:si+20], ax              ; DS, SI, DI, BP: the caller's, live -
    mov ax, [bp+2]                  ; nothing above has changed them except
    mov [cs:si+22], ax              ; the three this pops back
    mov [cs:si+24], di
    mov ax, [bp+4]
    mov [cs:si+26], ax
    mov ax, ss
    mov [cs:si+28], ax
    lea ax, [bp+12]                 ; ...at the SP the `int` was taken on
    mov [cs:si+30], ax

    add word [cs:wr], ENTSZ
    inc word [cs:total]
    pop ax
    pop si
    pop bp

    pushf
    call far [cs:old21]
    pushf                           ; ...the ANSWER's flags, banked across the
    push si                         ; logging below
    push ax
    mov si, [cs:here]
    mov [cs:si+8], ax
    mov [cs:si+12], bx              ; ES:BX IS THE ANSWER to AH=35h, 48h and
    mov ax, es                      ; 2Fh, and AX is not - a ring that logs
    mov [cs:si+14], ax              ; only AX is silent about all three
    mov word [cs:si+10], 0
    pushf                           ; ...still the far call's: every store
    pop ax                          ; above is a MOV, which sets no flag
    test al, 1                      ; CF is bit 0 of the flags word
    jz .noc
    mov word [cs:si+10], 1
.noc:
    pop ax
    pop si
    popf
    retf 2                          ; ...discarding the caller's pushed flags

.chain:
    jmp far [cs:old21]

; --- the dumper -------------------------------------------------------------
dump:
    mov ah, 0x3C                    ; create TRACE.LOG, replacing any
    xor cx, cx
    mov dx, s_file
    int 0x21
    jc .noopen
    mov [fh], ax

    mov di, line
    mov si, s_hdr
    call cat
    mov ax, [total]
    call hex4
    mov al, '/'
    stosb
    mov ax, [wr]
    call hex4
    call eol
    call flush

    mov cx, [total]
    cmp cx, NENT
    jbe .short
    mov cx, NENT
.short:
    xor bx, bx                      ; ...always from the first entry
    jcxz .done
.ent:
    push cx
    mov di, line
    mov ax, [bx+ring]
    call hex4
    mov al, ' '
    stosb
    mov ax, [bx+ring+2]
    call hex4
    mov al, ' '
    stosb
    mov ax, [bx+ring+4]
    call hex4
    mov al, ' '
    stosb
    mov ax, [bx+ring+6]
    call hex4
    mov al, '>'
    stosb
    mov ax, [bx+ring+8]
    call hex4
    mov al, '/'
    stosb
    mov ax, [bx+ring+10]
    call hex4
    mov al, '/'
    stosb
    mov ax, [bx+ring+14]
    call hex4
    mov al, ':'
    stosb
    mov ax, [bx+ring+12]
    call hex4
    mov al, '@'
    stosb
    mov ax, [bx+ring+18]
    call hex4
    mov al, ':'
    stosb
    mov ax, [bx+ring+16]
    call hex4
    mov al, '/'
    stosb
    mov ax, [bx+ring+20]
    call hex4
    call eol
    call flush
    add bx, ENTSZ
    pop cx
    loop .ent
.done:
    mov bx, [fh]
    mov ah, 0x3E
    int 0x21
    mov si, s_wrote
    call puts
    mov ax, 0x4C00
    int 0x21
.noopen:
    mov si, s_nofile
    call puts
    mov ax, 0x4C01
    int 0x21

; --- helpers ----------------------------------------------------------------
cat:
    lodsb
    or al, al
    jz .o
    stosb
    jmp short cat
.o:
    ret
eol:
    mov al, 13
    stosb
    mov al, 10
    stosb
    ret
flush:
    push ax
    push bx
    push cx
    push dx
    mov cx, di
    sub cx, line
    mov dx, line
    mov bx, [fh]
    mov ah, 0x40
    int 0x21
    pop dx
    pop cx
    pop bx
    pop ax
    ret
hexd:
    add al, '0'
    cmp al, '9'
    jbe .o
    add al, 7
.o:
    ret
hex2:
    push ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call hexd
    stosb
    pop ax
    and al, 0x0F
    call hexd
    stosb
    pop ax
    ret
hex4:
    push ax
    mov al, ah
    call hex2
    pop ax
    call hex2
    ret
puts:
    lodsb
    or al, al
    jz .o
    push si
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop si
    jmp short puts
.o:
    ret

s_on:     db 'DOSTRAP installed - it arms on the next EXEC', 13, 10, 0
s_nodos:  db 'no INT 21h to hook', 13, 10, 0
s_wrote:  db 'TRACE.LOG written', 13, 10, 0
s_nofile: db 'could not create TRACE.LOG', 13, 10, 0
s_file:   db 'TRACE.LOG', 0
s_hdr:    db 'os8088 DOS INT 21h trace', 13, 10
          db 'AX BX CX DX>AXout/CF/ES:BX@CS:IP/DS ', 0

; --- THE HOST READS THIS, rather than a nasm listing (docs/DOS-DEBUGGING.md).
; Every offset below moves whenever a line above it is edited, and a host-side
; reader that carries its own copy of them decodes a plausible-looking ring out
; of the wrong addresses - which is not a crash, it is a WRONG ANSWER about
; what a real DOS did. tools/os88dosdbg.py finds this signature in the
; assembled .COM and takes the six words that follow.
;
; `org 0x100` means a label's value is already the in-memory offset, so these
; need no bias; the signature's own FILE offset is where the search lands.
          db 'DOSTRAP1'
          dw ring, total, wr, here, NENT, ENTSZ

; ...and the mouse histogram's own, as a SECOND signature rather than three
; more words on the first: a reader that predates it still finds DOSTRAP1 and
; takes six words, which is what keeps an old tool and a new TSR from decoding
; each other's bytes.
          db 'DOSTRP33'
          dw m33, NENT33, SZ33, m33seq, SEQ33

armed:    db 0                      ; 0 until the EXEC that loads the subject
old21:    dd 0
old33:    dd 0
here:     dw 0
wr:       dw 0
total:    dw 0
fh:       dw 0
line:     times 128 db 0             ; ...a whole line: the header plus its
                                    ; two numbers is 60, and an entry 41
ring:     times NENT * ENTSZ db 0
m33:      times NENT33 * SZ33 db 0
m33n:     dw 0
m33seq:   times SEQ33 db 0
resident_end:
