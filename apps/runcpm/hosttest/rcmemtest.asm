; =============================================================================
; os8088 - apps/runcpm/hosttest/rcmemtest.asm
;
; A BOOT SECTOR THAT TESTS apps/runcpm/rcmem.inc ON A REAL x86 - the Z80-RAM
; accessors and block movers, the one place in RUNCPM where ES is loaded and
; a string instruction runs (SPEC.md 70.6). The host harness cannot reach
; them (it substitutes C), tools/cc8086.py never sees hand-written assembly,
; and a mover that got a segment wrong would write into the kernel and not
; fault (LESSONS.md 4). So, as apps/cword/hosttest/cwmovetest.asm does for
; cw_memmove, this runs the SHIPPING text (%included, not copied) under the
; one condition that makes this OS unlike every other target: SS != DS. Here
; CS = DS = 0 stand in for the package (rcmem.inc reads rc_z.seg through DS,
; the core through CS, and in a package the two are equal), SS = 0x1000 is
; the task stack, ES = 0x3000 stands in for KERNEL_SEG and must come back
; intact, 0x4000 is the Z80's claim and 0x5000 a second claim (a file, the
; CCP image) for the claim-to-claim movers.
;
; WHAT EACH CASE CHECKS, and none of it is the routine's own logic restated:
;
;   1. bytes written through rc_wr / rc_wr16 read back through rc_rd /
;      rc_rd16 AND through a direct far read of the claim - little-endian, at
;      0xFFFF wrapping to 0x0000 as the Z80's address space does;
;   2. every mover leaves the destination equal to the source, byte for byte,
;      including a 24-byte block laid across the 64KB wrap (0xFFF4..0x000B),
;      and touches NOTHING outside it (a guard byte each side is checked);
;   3. after every call ES, DS, BP and SP are what they were and DF is clear -
;      the register discipline the callback ABI needs (SPEC.md 70.3: SI and DI
;      are the caller's to lose, and the movers do use them);
;   4. NEGATIVE CONTROLS: two deliberately broken routines - one that leaves
;      ES = the claim, one that returns with DF set - are run through the same
;      checks, which must FAIL them; a harness that cannot see a broken
;      routine has proved nothing.
;
; RUN IT:  apps/runcpm/hosttest/rcmemtest.sh
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000
SEG_Z80     equ 0x4000
SEG_FILE    equ 0x5000
IMG_SECTORS equ 64

%macro PUSHI 1
    mov ax, %1
    push ax
%endmacro

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1
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
    ; read sectors 1..IMG_SECTORS-1 with a CHS walk (18 sectors a track, 2
    ; heads; the script pads the image to 1.44MB so QEMU agrees)
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
.err:
    mov al, 'D'
    call putc
    jmp halt
lba: dw 0

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld
    xor bp, bp                      ; failures
    mov word [_rc_z+RZ_SEG], SEG_Z80

    mov si, msg_hello
    call puts

    ; --- (1) the accessors ---------------------------------------------------
    ; rc_wr(0x1234, 0xA5); rc_wr(0xFFFF, 0x5A); rc_wr(0, 0xC3)
    mov word [pushes], 2
    PUSHI 0xA5
    PUSHI 0x1234
    call disc_call
    dw _rc_wr
    PUSHI 0x5A
    PUSHI 0xFFFF
    call disc_call
    dw _rc_wr
    PUSHI 0xC3
    PUSHI 0
    call disc_call
    dw _rc_wr
    ; direct far reads
    push es
    mov ax, SEG_Z80
    mov es, ax
    cmp byte [es:0x1234], 0xA5
    jne .f1
    cmp byte [es:0xFFFF], 0x5A
    jne .f1
    cmp byte [es:0], 0xC3
    jne .f1
    pop es
    ; rc_rd back
    mov word [pushes], 1
    PUSHI 0x1234
    call disc_call
    dw _rc_rd
    cmp ax, 0x00A5                  ; AH must be 0
    jne .f1b
    ; rc_rd16(0xFFFF) = C3 5A (low byte at FFFF, high at 0000)
    PUSHI 0xFFFF
    call disc_call
    dw _rc_rd16
    cmp ax, 0xC35A
    jne .f1b
    ; rc_wr16(0xFFFF, 0x1122): FFFF = 22, 0000 = 11
    mov word [pushes], 2
    PUSHI 0x1122
    PUSHI 0xFFFF
    call disc_call
    dw _rc_wr16
    mov word [pushes], 1
    PUSHI 0xFFFF
    call disc_call
    dw _rc_rd
    cmp ax, 0x22
    jne .f1b
    PUSHI 0
    call disc_call
    dw _rc_rd
    cmp ax, 0x11
    jne .f1b
    mov al, '.'
    call putc
    jmp .movers
.f1:
    pop es
.f1b:
    mov al, '1'
    call putc
    inc bp

    ; --- (2) the movers ------------------------------------------------------
.movers:
    ; the source pattern in DS, 24 bytes at src, with guards around dst
    mov cx, 24
    mov bx, src
    mov al, 0x11
.fill:
    mov [bx], al
    add al, 0x37
    inc bx
    loop .fill

    ; rc_zcopy_in(0xFFF4, src, 24): across the 64KB wrap
    call zclear
    mov word [pushes], 3
    PUSHI 24
    PUSHI src
    PUSHI 0xFFF4
    call disc_call
    dw _rc_zcopy_in
    mov ax, 0xFFF4
    call zcheck                     ; the claim holds the pattern at FFF4..000B
    jc .f2
    ; rc_zcopy_out(dst, 0xFFF4, 24)
    call dclear
    PUSHI 24
    PUSHI 0xFFF4
    PUSHI dst
    call disc_call
    dw _rc_zcopy_out
    call dcheck                     ; dst holds the pattern, guards intact
    jc .f2
    ; rc_zzcopy_out(SEG_FILE, 0x0100, 0xFFF4, 24) then
    ; rc_zzcopy_in(0x2000, SEG_FILE, 0x0100, 24)
    mov word [pushes], 4
    PUSHI 24
    PUSHI 0xFFF4
    PUSHI 0x0100
    PUSHI SEG_FILE
    call disc_call
    dw _rc_zzcopy_out
    call zclear                     ; the claim emptied...
    PUSHI 24
    PUSHI 0x0100
    PUSHI SEG_FILE
    PUSHI 0x2000
    call disc_call
    dw _rc_zzcopy_in
    mov ax, 0x2000
    call zcheck                     ; ...and refilled at 2000 from the file
    jc .f2
    ; rc_zfill(0x2000, 0xEE, 24) then read the edges
    mov word [pushes], 3
    PUSHI 24
    PUSHI 0xEE
    PUSHI 0x2000
    call disc_call
    dw _rc_zfill
    push es
    mov ax, SEG_Z80
    mov es, ax
    cmp byte [es:0x2000], 0xEE
    jne .f2e
    cmp byte [es:0x2017], 0xEE
    jne .f2e
    cmp byte [es:0x2018], 0        ; one past: untouched (zclear left 0)
    jne .f2e
    cmp byte [es:0x1FFF], 0
    jne .f2e
    pop es
    ; rc_sfill(SEG_FILE, 0x0100, 0x5A, 24): the file claim, not the Z80's
    mov word [pushes], 4
    PUSHI 24
    PUSHI 0x5A
    PUSHI 0x0100
    PUSHI SEG_FILE
    call disc_call
    dw _rc_sfill
    push es
    mov ax, SEG_FILE
    mov es, ax
    cmp byte [es:0x0100], 0x5A
    jne .f2e
    cmp byte [es:0x0117], 0x5A
    jne .f2e
    cmp byte [es:0x0118], 0x5A     ; one past: untouched (QEMU's RAM is 0)
    je .f2e
    mov ax, SEG_Z80
    mov es, ax
    cmp byte [es:0x0100], 0x5A     ; and NOT the Z80's 0100
    je .f2e
    pop es
    ; n = 0: nothing moves
    call zclear
    mov word [pushes], 3
    PUSHI 0
    PUSHI src
    PUSHI 0x3000
    call disc_call
    dw _rc_zcopy_in
    push es
    mov ax, SEG_Z80
    mov es, ax
    cmp byte [es:0x3000], 0
    jne .f2e
    pop es
    mov al, '.'
    call putc
    jmp .neg
.f2e:
    pop es
.f2:
    mov al, '2'
    call putc
    inc bp

    ; --- (4) the negative controls: the discipline check must catch these ----
.neg:
    mov word [pushes], 1
    PUSHI 0
    mov word [disc_expect_bad], 1
    call disc_call
    dw bad_es
    PUSHI 0
    call disc_call
    dw bad_df
    mov word [disc_expect_bad], 0

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
; disc_call - call the routine whose address follows the call, with the
; arguments already pushed ([pushes] words), and check the discipline after:
; ES, DS, BP, SP back, DF clear. The caller's arguments are popped here and
; the routine's AX handed back. Prints '.' on a pass, 'X' on a failure (or, when a negative control is
; expected to fail, 'N' when it does and 'x' when it slipped through).
; -----------------------------------------------------------------------------
disc_call:
    pop bx                          ; the return address: the routine's word
    mov ax, [bx]
    mov [disc_target], ax
    add bx, 2
    mov [disc_ret], bx
    mov [disc_sp], sp
    call [disc_target]
    mov [disc_ax], ax               ; the answer, handed back at .out
    mov [disc_sp2], sp
    mov [disc_bp], bp
    mov [disc_es], es
    mov [disc_ds], ds
    pushf
    pop bx
    mov [disc_fl], bx
    cld
    mov bx, SEG_KERNEL
    mov es, bx
    ; the arguments off
    mov bx, [pushes]
    shl bx, 1
    add sp, bx
    ; the checks
    mov bx, 0                       ; bx = bad
    mov ax, bp
    cmp [disc_bp], ax
    jne .b
    cmp word [disc_es], SEG_KERNEL
    jne .b
    cmp word [disc_ds], 0
    jne .b
    mov ax, [disc_sp]
    cmp [disc_sp2], ax
    jne .b
    test word [disc_fl], 0x0400
    jz .ok
.b: inc bx
.ok:
    cmp word [disc_expect_bad], 0
    jne .neg
    or bx, bx
    jz .pass
    mov al, 'X'
    call putc
    inc bp
    jmp .out
.pass:
    mov al, '.'
    call putc
    jmp .out
.neg:
    or bx, bx
    jz .slipped
    mov al, 'N'
    call putc
    jmp .out
.slipped:
    mov al, 'x'
    call putc
    inc bp
.out:
    mov ax, [disc_ax]
    jmp [disc_ret]

; the negative controls: one leaves ES on the claim, one returns with DF set
bad_es:
    push bp
    mov bp, sp
    mov es, [_rc_z+RZ_SEG]
    pop bp
    ret
bad_df:
    push bp
    mov bp, sp
    std
    pop bp
    ret

; zclear - the claim's first 64 and last 64 bytes and 0x2000.. to zero
zclear:
    push es
    push cx
    push bx
    mov ax, SEG_Z80
    mov es, ax
    xor bx, bx
    mov cx, 64
.a: mov byte [es:bx], 0
    inc bx
    loop .a
    mov bx, 0xFFC0
    mov cx, 64
.b: mov byte [es:bx], 0
    inc bx
    loop .b
    mov bx, 0x1FF0
    mov cx, 64
.c: mov byte [es:bx], 0
    inc bx
    loop .c
    pop bx
    pop cx
    pop es
    ret

; zcheck - the claim at AX.. holds the 24-byte pattern and the byte before
; and after are 0. CF = 1 on a mismatch.
zcheck:
    push es
    push cx
    push bx
    push dx
    mov dx, ax
    mov ax, SEG_Z80
    mov es, ax
    mov bx, dx
    dec bx
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    add bx, 24
    cmp byte [es:bx], 0
    jne .bad
    mov bx, dx
    mov cx, 24
    mov al, 0x11
.l: cmp [es:bx], al
    jne .bad
    add al, 0x37
    inc bx
    loop .l
    clc
    jmp .out
.bad:
    stc
.out:
    pop dx
    pop bx
    pop cx
    pop es
    ret

; dclear / dcheck - the DS-side destination and its guards
dclear:
    push cx
    push bx
    mov bx, dst - 4
    mov cx, 32
.l: mov byte [bx], 0xCC
    inc bx
    loop .l
    pop bx
    pop cx
    ret
dcheck:
    push cx
    push bx
    mov bx, dst
    mov cx, 24
    mov al, 0x11
.l: cmp [bx], al
    jne .bad
    add al, 0x37
    inc bx
    loop .l
    cmp byte [dst - 1], 0xCC
    jne .bad
    cmp byte [dst + 24], 0xCC
    jne .bad
    clc
    jmp .out
.bad:
    stc
.out:
    pop bx
    pop cx
    ret

; -----------------------------------------------------------------------------
; the routines under test - THE SHIPPING TEXT (rcz80.inc for _rc_z, which
; rcmem.inc reads its segment from)
; -----------------------------------------------------------------------------
%include "rcz80.inc"
%include "rcmem.inc"

section .text
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
.l: mov al, [cs:si]
    inc si
    or al, al
    jz .e
    call putc
    jmp .l
.e: pop ax
    ret
putnum:
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

msg_hello: db 'rcmemtest: rcmem.inc on a real x86, SS != DS', 13, 10, 0
msg_ok:    db ' failures - rcmem OK', 13, 10, 0
msg_bad:   db ' FAILURES in rcmem', 13, 10, 0

pushes:          dw 0
disc_expect_bad: dw 0
disc_target: dw 0
disc_ret:    dw 0
disc_sp:     dw 0
disc_sp2:    dw 0
disc_bp:     dw 0
disc_ax:     dw 0
disc_es:     dw 0
disc_ds:     dw 0
disc_fl:     dw 0
src:         times 24 db 0
             times 4 db 0xCC
dst:         times 24 db 0
             times 4 db 0xCC
