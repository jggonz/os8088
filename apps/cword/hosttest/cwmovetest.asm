; =============================================================================
; os8088 - apps/cword/hosttest/cwmovetest.asm
;
; A BOOT SECTOR THAT TESTS cw_memmove ON A REAL x86, because nothing else can.
;
; apps/cword/cwmove.inc is the one hand-written routine in the package
; (SPEC.md 73.11: a C package composes, the byte loop is assembly). The host
; harness cannot exercise it - it substitutes the C library's memmove() - and
; the compiler gate cannot either, because it never sees hand-written
; assembly. So the routine's first execution would otherwise be on the user's
; machine, moving the user's document.
;
; This runs it under QEMU, from a floppy boot sector, with the ONE condition
; that makes this operating system different from every other target the
; routine could be tested on:
;
;     SS != DS
;
; That is CLAUDE.md's hard rule and SPEC.md 1's: a task's stack lives in
; LOW_SEG and its data in the package's own segment. A byte mover that reads
; its arguments off the stack and its data out of DS has to keep those two
; straight, and a test that ran with SS == DS - which is what every ordinary
; environment gives you - would pass whether it did or not. Here SS is 0x1000,
; DS is 0x2000 and ES is 0x3000 standing in for KERNEL_SEG, and all three are
; checked afterwards.
;
; WHAT EACH CASE CHECKS, and it is deliberately not "the copy went the right
; way round" - that would be the routine's own logic restated:
;
;   1. every byte of the destination equals the byte the SOURCE held BEFORE
;      the move, taken from a pristine copy made while the two did not
;      overlap. That is the definition of a memmove and it catches a
;      direction error in either direction, including the ascending-copy bug
;      that made this routine necessary;
;   2. every byte OUTSIDE the destination is untouched;
;   3. ES, DS, SI, DI, BP and SP come back exactly as they went in, and DF is
;      clear - the register discipline the callback ABI needs (CLAUDE.md), and
;      the reason the routine loads ES itself instead of trusting one.
;
; The cases cover both overlap directions at every offset that matters, the
; single-byte move an insertion actually makes, a zero-length move, and a
; 4,000-byte move - the whole document buffer, which is the worst case the
; editor can ask for.
;
; RUN IT:  apps/cword/hosttest/cwmovetest.sh
;          (nasm, then qemu-system-i386 with the results on the serial port)
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000              ; SS - "LOW_SEG"
SEG_DATA    equ 0x2000              ; DS - the package's own segment
SEG_KERNEL  equ 0x3000              ; ES - "KERNEL_SEG", must come back intact

BUF         equ 0x0000              ; the buffer under test, in DS
PRISTINE    equ 0x2000              ; a copy of it before the move, in DS
BUFLEN      equ 4096

; -----------------------------------------------------------------------------
; STAGE 1 - the 512 bytes the BIOS actually loads.
;
; The test is bigger than a boot sector, so this reads the rest of the image
; off the floppy and jumps into it. It is the same shape as the loader in
; boot/boot.asm and the one the C toolchain's differential harness uses, and
; it is here rather than in a script because a boot sector is the only way to
; get an 8086 to execute a routine with SS != DS and nothing else running.
; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld

    mov cx, 8                       ; 8 sectors after this one is plenty
    mov ax, 2                       ; LBA 2 = head 0, track 0, sector 2
    mov bx, 0x7E00
.rd:
    push cx
    push ax
    mov cl, al                      ; 18 sectors a track, so LBA 2..9 are all
    mov ch, 0                       ; on track 0 head 0 - no division needed
    mov dh, 0
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    pop ax
    inc ax
    add bx, 512
    pop cx
    loop .rd
    jmp 0x0000:body
.err:
    mov al, 'D'                     ; the disk said no: nothing else can run
    call putc
    jmp halt

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2 - the test itself
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    mov ax, SEG_DATA
    mov ds, ax
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld

    xor bp, bp                      ; BP = failures

    ; --- the case table walk -------------------------------------------------
    mov si, cases
.next:
    mov ax, [cs:si]                 ; dst
    cmp ax, 0xFFFF
    je .done
    call runcase
    add si, 6
    jmp .next

.done:
    mov al, 'T'
    call putc
    mov ax, bp
    call putnum
    or bp, bp
    jnz .bad
    mov si, msg_ok
    call puts
    jmp halt
.bad:
    mov si, msg_bad
    call puts
    jmp halt

; -----------------------------------------------------------------------------
; runcase - one (dst, src, n) triple at CS:SI
; -----------------------------------------------------------------------------
runcase:
    push si

    ; fill the buffer with a pattern no memmove could produce by accident
    mov cx, BUFLEN
    mov di, BUF
    mov al, 1
.fill:
    mov [di], al
    add al, 7
    xor al, 0x5A
    inc di
    loop .fill

    ; ...and take a pristine copy, forward, into a region that cannot overlap
    mov cx, BUFLEN
    mov di, BUF
    mov bx, PRISTINE
.copy:
    mov al, [di]
    mov [bx], al
    inc di
    inc bx
    loop .copy

    ; The call, cdecl: the arguments are pushed RIGHT TO LEFT and the caller
    ; cleans up. BP is banked FIRST, before the arguments - pushing it after
    ; them puts it between the return address and the first argument, and the
    ; callee then reads the saved BP as its destination pointer. That was this
    ; harness's own first bug and every case failed check 1 on it, which is a
    ; fair demonstration that the checks look at the right thing.
    push bp
    mov ax, [cs:si+4]               ; n
    push ax
    mov ax, [cs:si+2]               ; src
    add ax, BUF
    push ax
    mov ax, [cs:si]                 ; dst
    add ax, BUF
    push ax

    mov di, 0x1234                  ; witnesses for the register discipline
    mov bx, 0x5678
    mov bp, 0xABCD
    mov word [state_sp], sp

    call _cw_memmove

    mov word [state_sp2], sp
    mov [state_bp], bp
    add sp, 6                       ; cdecl: the CALLER cleans up
    pop bp

    mov [state_di], di
    mov [state_es], es
    mov [state_ds], ds
    pushf
    pop ax
    mov [state_fl], ax

    ; --- (1) the destination holds what the source held ---------------------
    mov cx, [cs:si+4]
    jcxz .outside
    mov di, [cs:si]
    add di, BUF                     ; where it went
    mov bx, [cs:si+2]
    add bx, PRISTINE                ; what was there before
.cmp1:
    mov al, [bx]
    cmp al, [di]
    jne .fail1
    inc bx
    inc di
    loop .cmp1

    ; --- (2) nothing outside the destination moved --------------------------
.outside:
    xor di, di                      ; DI = index through the buffer
.cmp2:
    mov ax, di
    sub ax, [cs:si]                 ; ax = di - dst
    jb .check                       ; below the destination: must be untouched
    cmp ax, [cs:si+4]
    jb .skip                        ; inside the destination: already checked
.check:
    mov bx, di
    add bx, PRISTINE
    mov al, [bx]
    mov bx, di
    add bx, BUF
    cmp al, [bx]
    jne .fail2
.skip:
    inc di
    cmp di, BUFLEN
    jb .cmp2

    ; --- (3) the register discipline ----------------------------------------
    cmp word [state_di], 0x1234
    jne .fail
    cmp word [state_bp], 0xABCD
    jne .fail
    mov ax, SEG_KERNEL
    cmp [state_es], ax
    jne .fail
    mov ax, SEG_DATA
    cmp [state_ds], ax
    jne .fail
    mov ax, [state_sp]
    cmp [state_sp2], ax             ; the routine left SP where it found it
    jne .fail
    test word [state_fl], 0x0400    ; DF must be clear on the way out
    jnz .fail

    mov al, '.'
    call putc
    pop si
    ret

.fail1:
    mov al, '1'
    call putc
    jmp .fail
.fail2:
    mov al, '2'
    call putc
    jmp .fail
.fail:
    mov al, 'X'
    call putc
    inc bp
    pop si
    ret

; -----------------------------------------------------------------------------
; the routine under test - THE SHIPPING TEXT, not a copy of it
; -----------------------------------------------------------------------------
%include "cwmove.inc"

; -----------------------------------------------------------------------------
; serial output (COM1, which QEMU is told to send to stdout)
; -----------------------------------------------------------------------------
putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

puts:
    push ax
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e:
    pop ax
    ret

putnum:                             ; AX in decimal, up to 4 digits
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.dv:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .dv
.pr:
    pop ax
    add al, '0'
    call putc
    loop .pr
    pop dx
    pop cx
    pop bx
    pop ax
    ret

halt:
    cli
    hlt
    jmp halt

msg_ok:  db ' failures - cw_memmove OK', 13, 10, 0
msg_bad: db ' FAILURES in cw_memmove', 13, 10, 0

; --- state, in DS ------------------------------------------------------------
state_sp:  dw 0
state_sp2: dw 0
state_bp:  dw 0
state_di:  dw 0
state_es:  dw 0
state_ds:  dw 0
state_fl:  dw 0

; --- the cases: dst, src, n --------------------------------------------------
; The two overlap directions at every distance that matters, the one-byte move
; an insertion makes, the empty move, and the whole 4,000-character document.
cases:
    dw 0,    0,    0                ; nothing at all
    dw 0,    0,    1
    dw 1,    0,    1                ; the editor's own insert: one byte up
    dw 0,    1,    1                ; ...and its delete: one byte down
    dw 1,    0,    4000             ; a full document, inserted at the front
    dw 0,    1,    4000             ; ...and deleted from the front
    dw 1,    0,    2                ; overlap by all but one
    dw 0,    1,    2
    dw 2,    0,    3
    dw 0,    2,    3
    dw 8,    0,    16               ; overlapping, aligned
    dw 0,    8,    16
    dw 7,    3,    31               ; overlapping, odd offsets both ends
    dw 3,    7,    31
    dw 100,  0,    100              ; abutting, not overlapping
    dw 0,    100,  100
    dw 500,  0,    1000             ; overlapping by half
    dw 0,    500,  1000
    dw 2048, 0,    2048             ; the top half against the bottom half
    dw 0,    2048, 2048
    dw 96,   0,    4000             ; a big move, small overlap distance
    dw 0,    96,   4000
    dw 0xFFFF, 0, 0                 ; the end

    times 4608-($-$$) db 0          ; 9 sectors in all
