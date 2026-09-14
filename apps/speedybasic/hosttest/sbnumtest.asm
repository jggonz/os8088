; Raw 8086 differential for the shipping sbnum.inc text.  The host reference
; emits the corpus, this boot image executes it with SS != DS, and every case
; also checks DS, ES, BP, SP and DF on return.
cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000
SEG_KERNEL  equ 0x3000
IMG_SECTORS equ 32
ROW         equ 28                 ; fourteen words per generated case

section .text
section .rodata follows=.text align=2
section .data follows=.rodata align=2
section .bss follows=.data align=2 nobits
section .modc follows=.data align=1 vstart=0
section .text

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov word [lba], 1
    mov bx, 0x7E00
.read:
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
    jc .diskerr
    add bx, 512
    inc word [lba]
    jmp .read
.go:
    jmp 0x0000:body
.diskerr:
    mov al, 'D'
    call putc
    jmp halt
lba: dw 0
times 510-($-$$) db 0
dw 0xAA55

body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax
    mov ax, 0x4000                 ; load the separately-addressed cold text
    mov es, ax                     ; before resident BSS overwrites its source
    mov si, resident_file_end
    xor di, di
    mov cx, SBN_TEST_MOD_SIZE
    cld
    rep movsb
    mov word [cc_ovseg], 0x4000
    mov ax, SEG_KERNEL
    mov es, ax
    sti
    cld
    xor bp, bp
    mov si, sbn_cases
.case:
    cmp si, sbn_cases_end
    jae .done
    call runcase
    call verdict
    add si, ROW
    jmp .case
.done:
    mov al, 'T'
    call putc
    mov ax, bp
    call putdec
    mov si, msg_ok
    or bp, bp
    jz .tail
    mov si, msg_bad
.tail:
    call puts
    mov al, 1
    or bp, bp
    jz .exit
    mov al, 2
.exit:
    mov dx, 0xF4
    out dx, al
halt:
    cli
    hlt
    jmp halt

; Load one generated record, dispatch, and return AX=1 only when result and
; the complete callback-facing machine state match.
runcase:
    mov ax, [cs:si+4]
    mov [_sbn_alo], ax
    mov ax, [cs:si+6]
    mov [_sbn_ahi], ax
    mov ax, [cs:si+8]
    mov [_sbn_akind], ax
    mov ax, [cs:si+10]
    mov [_sbn_blo], ax
    mov ax, [cs:si+12]
    mov [_sbn_bhi], ax
    mov ax, [cs:si+14]
    mov [_sbn_bkind], ax
    mov word [sbnt_ret], 0
    mov [sbnt_sp], sp
    cld
    push si
    mov ax, [cs:si+2]
    cmp ax, 1
    je .add
    cmp ax, 2
    je .sub
    cmp ax, 3
    je .mul
    cmp ax, 4
    je .div
    cmp ax, 5
    je .neg
    cmp ax, 6
    je .floor
    cmp ax, 7
    je .fix
    cmp ax, 8
    je .cint
    cmp ax, 9
    je .sqrt
    cmp ax, 10
    je .sin
    cmp ax, 11
    je .cos
    cmp ax, 13
    je .float
    cmp ax, 14
    je .fdiv
    call _sbn_cmp
    mov [sbnt_ret], ax
    jmp .called
.add: call _sbn_add
    jmp .called
.sub: call _sbn_sub
    jmp .called
.mul: call _sbn_mul
    jmp .called
.div: call _sbn_div
    jmp .called
.neg: call _sbn_neg
    jmp .called
.floor: call _sbn_floor
    jmp .called
.fix: call _sbn_fix
    jmp .called
.cint: call _sbn_cint
    jmp .called
.sqrt: call _sbn_sqrt
    jmp .called
.sin: call _sbn_sin
    jmp .called
.cos: call _sbn_cos
    jmp .called
.float: call _sbn_float
    jmp .called
.fdiv: call _sbn_fdiv
.called:
    pop si
    cmp sp, [sbnt_sp]
    jne .no
    or bp, bp                       ; BP is the failure counter and must survive
    js .no                          ; (also makes accidental corruption visible)
    mov ax, ds
    or ax, ax
    jnz .no
    mov ax, es
    cmp ax, SEG_KERNEL
    jne .no
    pushf
    pop ax
    test ax, 0x0400
    jnz .no
    mov ax, [_sbn_alo]
    cmp ax, [cs:si+16]
    jne .no
    mov ax, [_sbn_ahi]
    cmp ax, [cs:si+18]
    jne .no
    mov ax, [_sbn_akind]
    cmp ax, [cs:si+20]
    jne .no
    mov ax, [_sbn_err]
    cmp ax, [cs:si+22]
    jne .no
    mov ax, [sbnt_ret]
    cmp ax, [cs:si+24]
    jne .no
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

verdict:
    cmp word [cs:si+26], 0
    jne .negative
    or ax, ax
    jz .fail
    mov al, '.'
    call putc
    ret
.negative:
    or ax, ax
    jnz .fail
    mov al, 'N'
    call putc
    ret
.fail:
    mov al, 'X'
    call putc
    mov al, ' '
    call putc
    push si
    mov si, [cs:si]
    call puts
    mov al, ' '
    call putc
    pop si
    inc bp
    ret

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
.next:
    mov al, [cs:si]
    inc si
    or al, al
    jz .out
    call putc
    jmp .next
.out:
    pop ax
    ret

putdec:
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .divide
.digit:
    pop ax
    add al, '0'
    call putc
    loop .digit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

msg_ok:  db ' failures - sbnum OK',13,10,0
msg_bad: db ' FAILURES in sbnum',13,10,0

; Stand-in for crt0's load-on-demand check.  BODY copied the module already;
; the public resident stubs still take their real far-call path.
cc_ovneed:
    clc
    ret

%include "sbnum_cases.inc"
%include "speedybasic/sbnum.inc"

section .modc
sbn_test_mod_end:
SBN_TEST_MOD_SIZE equ sbn_test_mod_end
section .data
resident_file_end:
section .bss
cc_ovseg: resw 1
sbnt_ret: resw 1
sbnt_sp:  resw 1
