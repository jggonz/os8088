; Raw 8086 gate for the shipping sbmem.inc.  The heap calls below hand out
; distinct real-mode segments and record every requested size.  Its REGROW
; stand-in moves and copies each live claim, proving the returned base is taken.
; This tests far loads/stores with SS != DS and BOING's 4,001-cell boundaries.
cpu 8086
bits 16
org 0x7c00

IMG_SECTORS equ 16
SEG_STACK   equ 0x1000
SEG_SENTINEL equ 0x9000

section .text
section .rodata follows=.text align=2
section .data follows=.rodata align=2
section .bss follows=.data align=2 nobits
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
    mov word [disk_lba], 1
    mov bx, 0x7e00
.read:
    mov ax, [disk_lba]
    cmp ax, IMG_SECTORS
    jae body
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    xor dl, dl
    mov ax, 0x0201
    int 0x13
    jc disk_error
    add bx, 512
    inc word [disk_lba]
    jmp .read
disk_error:
    mov al, 'D'
    call putc
    jmp halt
disk_lba: dw 0
times 510-($-$$) db 0
dw 0xaa55

%macro EXPECT_AX 1
    cmp ax, %1
    je %%ok
    inc word [failures]
%%ok:
%endmacro

%macro CALL1 2
    mov ax, %2
    push ax
    call %1
    add sp, 2
%endmacro

%macro CALL2 3
    mov ax, %3
    push ax
    mov ax, %2
    push ax
    call %1
    add sp, 4
%endmacro

%macro CALL3 4
    mov ax, %4
    push ax
    mov ax, %3
    push ax
    mov ax, %2
    push ax
    call %1
    add sp, 6
%endmacro

body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xfff0
    xor ax, ax
    mov ds, ax
    mov ax, SEG_SENTINEL
    mov es, ax
    sti
    cld
    mov word [failures], 0
    mov word [claim_count], 0
    mov word [free_count], 0
    mov word [fail_claim], -1
    mov word [regrow_count], 0
    mov word [fail_regrow], -1
    mov bp, 0x1357
    mov [initial_sp], sp

    call _sbm_init
    EXPECT_AX 0
    mov ax, [claim_count]
    EXPECT_AX 0

    mov word [fail_claim], -1
    CALL1 _sbm_make, 0
    EXPECT_AX 0
    mov ax, [claim_count]
    EXPECT_AX 0

    ; BOING's three 4,001-cell buffers grow one contiguous low plane from
    ; 8K through 16K to 24K.  Existing values survive every move.
    CALL1 _sbm_make, 4001
    EXPECT_AX 0
    CALL2 _sbm_set, 0, -7
    CALL2 _sbm_set, 4000, 123
    CALL1 _sbm_make, 4001
    EXPECT_AX 4001
    CALL1 _sbm_get_lo, 0
    EXPECT_AX -7
    CALL1 _sbm_get_lo, 4000
    EXPECT_AX 123
    CALL1 _sbm_make, 4001
    EXPECT_AX 8002
    mov ax, [claim_count]
    EXPECT_AX 2
    mov ax, [claim_sizes]
    EXPECT_AX 8
    mov ax, [claim_sizes+2]
    EXPECT_AX 2
    mov ax, [regrow_sizes]
    EXPECT_AX 16
    mov ax, [regrow_sizes+2]
    EXPECT_AX 24
    mov ax, [regrow_count]
    EXPECT_AX 2
    mov ax, [free_count]
    EXPECT_AX 0
    call _sbm_lo_segment
    EXPECT_AX 0x5000
    mov ax, [sbm_plane_kb]
    EXPECT_AX 24

    ; Byte access through segment + (cell<<1) crosses the first 4,001-cell
    ; image boundary without a page translation or a second segment.
    mov es, [sbm_lo_seg]
    mov byte [es:8002], 0x5a
    mov byte [es:8003], 0xa5
    mov ax, SEG_SENTINEL
    mov es, ax
    CALL1 _sbm_get_lo, 4001
    EXPECT_AX 0xa55a

    ; Sign-extension values do not materialize high.  The first true pair
    ; claims only the current 24K and seeds all existing cells.
    CALL3 _sbm_set_pair, 4000, 0x8000, -1
    EXPECT_AX 0
    mov ax, [claim_count]
    EXPECT_AX 2
    CALL1 _sbm_get_hi, 0
    EXPECT_AX -1
    CALL1 _sbm_get_hi, 4000
    EXPECT_AX -1
    CALL3 _sbm_set_pair, 12002, 0x5678, 0x1234
    EXPECT_AX 0
    mov ax, [claim_count]
    EXPECT_AX 3
    mov ax, [claim_sizes+4]
    EXPECT_AX 24
    CALL1 _sbm_get_hi, 0
    EXPECT_AX -1
    CALL1 _sbm_get_hi, 4000
    EXPECT_AX -1
    CALL1 _sbm_get_hi, 4001
    EXPECT_AX -1
    CALL1 _sbm_get_hi, 12002
    EXPECT_AX 0x1234

    ; Final growth replaces and copies both live planes before publishing them.
    CALL1 _sbm_make, 4381
    EXPECT_AX 12003
    mov ax, [regrow_count]
    EXPECT_AX 4
    mov ax, [regrow_sizes+4]
    EXPECT_AX 32
    mov ax, [regrow_sizes+6]
    EXPECT_AX 32
    call _sbm_lo_segment
    EXPECT_AX 0x7000
    mov ax, [sbm_hi_seg]
    EXPECT_AX 0x8000
    mov ax, [sbm_plane_kb]
    EXPECT_AX 32
    CALL1 _sbm_get_hi, 0
    EXPECT_AX -1
    CALL1 _sbm_get_lo, 4001
    EXPECT_AX 0xa55a
    CALL1 _sbm_get_hi, 12002
    EXPECT_AX 0x1234
    CALL1 _sbm_get_lo, 16383
    EXPECT_AX 0
    CALL1 _sbm_get_hi, 16383
    EXPECT_AX 0
    CALL1 _sbm_make, 1
    EXPECT_AX -1
    mov ax, [sbm_used]
    EXPECT_AX 16384

    ; Kind remains an eager 2K bit plane with LONG defaults at both edges.
    CALL1 _sbm_kind_get, 0
    EXPECT_AX 0
    CALL1 _sbm_kind_get, 16383
    EXPECT_AX 0
    CALL2 _sbm_kind_set, 0, 1
    CALL2 _sbm_kind_set, 16383, 1
    CALL1 _sbm_kind_get, 0
    EXPECT_AX 1
    CALL1 _sbm_kind_get, 16383
    EXPECT_AX 1

    call _sbm_reset
    mov ax, [free_count]
    EXPECT_AX 3
    mov ax, [free_segs]
    EXPECT_AX 0x7000
    mov ax, [free_segs+2]
    EXPECT_AX 0x8000
    mov ax, [free_segs+4]
    EXPECT_AX 0x3000
    mov ax, [sbm_lo_seg]
    or ax, [sbm_hi_seg]
    or ax, [sbm_kind_seg]
    EXPECT_AX 0
    mov ax, [sbm_plane_kb]
    EXPECT_AX 0

    ; If high regrow fails after low moves successfully, logical capacity and
    ; used count stay unchanged.  The new low base remains valid, values in both
    ; planes survive, and retry grows only the lagging high plane.
    mov word [claim_count], 0
    mov word [free_count], 0
    mov word [fail_claim], -1
    mov word [regrow_count], 0
    mov word [fail_regrow], -1
    mov word [regrow_segs], 0x5000
    CALL1 _sbm_make, 100
    EXPECT_AX 0
    CALL3 _sbm_set_pair, 0, 0x4321, 0x1234
    EXPECT_AX 0
    mov word [fail_regrow], 1
    CALL1 _sbm_make, 2100
    EXPECT_AX -1
    mov ax, [sbm_used]
    EXPECT_AX 100
    mov ax, [sbm_plane_kb]
    EXPECT_AX 4
    mov ax, [sbm_lo_seg]
    EXPECT_AX 0x5000
    mov ax, [sbm_hi_seg]
    EXPECT_AX 0x4000
    mov ax, [free_count]
    EXPECT_AX 0
    CALL1 _sbm_get_lo, 0
    EXPECT_AX 0x4321
    CALL1 _sbm_get_hi, 0
    EXPECT_AX 0x1234
    mov word [fail_regrow], -1
    CALL1 _sbm_make, 2100
    EXPECT_AX 100
    mov ax, [sbm_used]
    EXPECT_AX 2200
    mov ax, [sbm_plane_kb]
    EXPECT_AX 8
    mov ax, [sbm_lo_seg]
    EXPECT_AX 0x5000
    mov ax, [sbm_hi_seg]
    EXPECT_AX 0x7000
    CALL1 _sbm_get_lo, 0
    EXPECT_AX 0x4321
    CALL1 _sbm_get_hi, 0
    EXPECT_AX 0x1234

    call _sbm_reset
    mov word [claim_count], 0
    mov word [free_count], 0
    mov word [fail_claim], -1
    mov word [regrow_count], 0
    mov word [fail_regrow], -1

    ; A failed first high claim is cell-transactional and observable through
    ; sbm_set_pair's int result.  A retry succeeds and strings reuse that high.
    CALL1 _sbm_make, 2
    EXPECT_AX 0
    CALL2 _sbm_set, 0, 7
    mov word [fail_claim], 4          ; byte offset of claim ordinal 2
    CALL3 _sbm_set_pair, 0, 0x5678, 0x1234
    EXPECT_AX -1
    CALL1 _sbm_get_lo, 0
    EXPECT_AX 7
    CALL1 _sbm_get_hi, 0
    EXPECT_AX 0
    mov ax, [sbm_hi_seg]
    EXPECT_AX 0
    mov word [fail_claim], -1
    CALL3 _sbm_set_pair, 0, 0x5678, 0x1234
    EXPECT_AX 0
    CALL1 _sbm_get_lo, 0
    EXPECT_AX 0x5678
    CALL1 _sbm_get_hi, 0
    EXPECT_AX 0x1234
    mov ax, 5
    push ax
    mov ax, test_string
    push ax
    mov ax, 1
    push ax
    call _sbm_str_set
    add sp, 6
    EXPECT_AX 0
    CALL1 _sbm_str_len, 1
    EXPECT_AX 5
    CALL2 _sbm_str_char, 1, 4
    EXPECT_AX 'Y'
    mov ax, 8
    push ax
    mov ax, copy_buffer
    push ax
    mov ax, 1
    push ax
    call _sbm_str_copy
    add sp, 6
    EXPECT_AX 5
    mov si, copy_buffer
    mov di, test_string
    mov cx, 6
.string_cmp:
    mov al, [si]
    cmp al, [di]
    je .string_next
    inc word [failures]
.string_next:
    inc si
    inc di
    loop .string_cmp

    ; Every public entry must return to the package data segment and preserve
    ; the SmallerC callee state with a distinct stack segment.
    mov ax, ds
    EXPECT_AX 0
    mov ax, es
    EXPECT_AX SEG_SENTINEL
    mov ax, bp
    EXPECT_AX 0x1357
    mov ax, sp
    cmp ax, [initial_sp]
    je .sp_ok
    inc word [failures]
.sp_ok:
    pushf
    pop ax
    test ax, 0x0400
    jz .df_ok
    inc word [failures]
.df_ok:

    ; Negative control: prove the comparison path catches a wrong oracle.
    CALL1 _sbm_get_hi, 0
    cmp ax, 1
    jne .negative_caught
    inc word [failures]
    mov al, 'X'
    call putc
    jmp short .summary
.negative_caught:
    mov al, 'N'
    call putc
.summary:
    mov al, 'T'
    call putc
    mov ax, [failures]
    call putdec
    mov si, msg_ok
    test ax, ax
    jz .print
    mov si, msg_bad
.print:
    call puts
    mov al, 1
    cmp word [failures], 0
    je .exit
    mov al, 2
.exit:
    mov dx, 0xf4
    out dx, al
halt:
    cli
    hlt
    jmp halt

; Heap-call stand-ins.  Each claim returns a separate physical segment and
; records the C argument; free records its segment argument.
_os88_mem_claim:
    push bp
    mov bp, sp
    push bx
    mov bx, [claim_count]
    shl bx, 1
    mov ax, [bp+4]
    mov [claim_sizes+bx], ax
    mov ax, [claim_segs+bx]
    cmp bx, [fail_claim]
    jne .claim_ok
    xor ax, ax
.claim_ok:
    inc word [claim_count]
    pop bx
    pop bp
    ret

; Model MEM_REGROW's moved-base path: copy every live word into a distinct
; segment and return that new base.  A refused call returns zero and touches
; neither old contents nor the base known by sbmem.
_os88_mem_regrow:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov bx, [regrow_count]
    mov ax, bx
    shl bx, 1
    mov dx, [bp+6]
    mov [regrow_sizes+bx], dx
    mov dx, [regrow_segs+bx]
    inc word [regrow_count]
    cmp ax, [fail_regrow]
    je .regrow_bad
    mov ds, [bp+4]
    mov es, dx
    xor si, si
    xor di, di
    mov cx, [cs:sbm_used]
    cld
    rep movsw
    mov ax, dx
    jmp short .regrow_done
.regrow_bad:
    xor ax, ax
.regrow_done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

_os88_mem_free:
    push bp
    mov bp, sp
    push bx
    mov bx, [free_count]
    shl bx, 1
    mov ax, [bp+4]
    mov [free_segs+bx], ax
    inc word [free_count]
    xor ax, ax
    pop bx
    pop bp
    ret

putc:
    push ax
    push dx
.wait:
    mov dx, 0x3fd
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3f8
    out dx, al
    pop dx
    ret

puts:
    push ax
.next:
    mov al, [si]
    inc si
    test al, al
    jz .done
    call putc
    jmp short .next
.done:
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
    test ax, ax
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

test_string: db 'ARRAY',0
msg_ok:      db ' failures - sbmem OK',13,10,0
msg_bad:     db ' FAILURES in sbmem',13,10,0
claim_segs:  dw 0x2000,0x3000,0x4000,0x5000,0x6000,0x7000,0x8000,0xa000
             dw 0xb000,0xc000,0xd000,0xe000,0xf000,0x1800,0x2800,0x3800
regrow_segs: dw 0x4000,0x5000,0x7000,0x8000,0xa000,0xb000,0xc000,0xd000

%include "speedybasic/sbmem.inc"

section .bss
failures:    resw 1
initial_sp:  resw 1
claim_count: resw 1
free_count:  resw 1
fail_claim:  resw 1
regrow_count: resw 1
fail_regrow: resw 1
claim_sizes: resw 16
regrow_sizes: resw 16
free_segs:   resw 16
copy_buffer: resb 8
