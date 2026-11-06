; os8088 - apps/pagedesk/hosttest/pde_kstub.asm
; Three kernel cells, as much of OSAPI as PDENG calls, at their real offsets in
; KERNEL_SEG, serving a PDF that the harness has loaded into memory:
;   OSAPI_FILE_DFREE     0x0118   BX = sectors per cluster
;   OSAPI_FILE_FIND      0x0283   one file, DOC.PDF
;   OSAPI_FILE_READ_SEQ  0x0455   READ_SEQ with its contract ENFORCED: a call
;                                  whose capacity or offset is not a cluster
;                                  multiple prints "!CONTRACT" and stops, where
;                                  the kernel would answer FERR_NAME
; pde_boot.py patches the data words at DATA (size, cluster, PDF segment).
cpu 8086
bits 16
org 0
%define DATA 0x500
times 0x118 db 0
    jmp near s_dfree
times 0x283 - ($ - $$) db 0
    jmp near s_find
times 0x455 - ($ - $$) db 0
    jmp near s_rdseq
times DATA - ($ - $$) db 0
k_size_lo: dw 0
k_size_hi: dw 0
k_cluster: dw 1
k_reads:   dw 0
k_pdfseg:  dw 0
k_ferr:    dw 0

s_dfree:
    xor ax, ax
    xor dx, dx
    mov bx, [cs:k_cluster]
    clc
    retf

s_find:                              ; CX = ordinal, ES:DI = 24-byte buffer
    test cx, cx
    jz .one
    mov ax, 4                        ; FERR_NOENT
    stc
    retf
.one:
    push di
    push cx
    xor ax, ax
    mov cx, 12
    cld
    rep stosw                        ; (a trailing rep stosw on ES:DI: the buffer is the caller's)
    pop cx
    pop di
    mov word [es:di], 'DO'
    mov word [es:di+2], 'C.'
    mov word [es:di+4], 'PD'
    mov word [es:di+6], 'F'
    mov ax, [cs:k_size_lo]
    mov [es:di+18], ax
    mov ax, [cs:k_size_hi]
    mov [es:di+20], ax
    mov cx, 1
    clc
    retf

s_rdseq:                             ; DX:BX buffer, CX cap, ES:DI cursor
    push bp
    push si
    push di
    push ds
    push es
    mov [cs:t_dseg], dx
    mov [cs:t_doff], bx
    mov [cs:t_cap], cx
    mov [cs:t_cures], es
    mov [cs:t_cudi], di
    inc word [cs:k_reads]
    ; the contract: capacity and offset are cluster multiples (and not empty)
    mov ax, [cs:k_cluster]
    mov cl, 9
    shl ax, cl                       ; cluster bytes
    dec ax                           ; AX = mask
    mov bx, [cs:t_cap]
    test bx, ax
    jnz .contract
    mov bx, [es:di+12]
    test bx, ax
    jnz .contract
    cmp word [cs:t_cap], 0
    je .contract
    mov si, [es:di+12]
    mov bp, [es:di+14]               ; BP:SI = the offset
    mov ax, [cs:k_size_lo]
    mov dx, [cs:k_size_hi]
    sub ax, si
    sbb dx, bp                       ; DX:AX = size - offset
    jb .eof
    mov bx, ax
    or bx, dx
    jz .eof
    mov cx, [cs:t_cap]
    test dx, dx
    jnz .n
    cmp ax, cx
    jae .n
    mov cx, ax
.n:
    mov [cs:t_n], cx
    mov ax, si
    mov cl, 4
    shr ax, cl                       ; offset >> 4
    mov dx, bp
    mov cl, 12
    shl dx, cl                       ; (offset hi) << 12
    add ax, dx
    add ax, [cs:k_pdfseg]            ; the source segment
    and si, 15
    mov ds, ax                       ; DS:SI = the source
    mov es, [cs:t_dseg]
    mov di, [cs:t_doff]              ; ES:DI = the destination
    mov cx, [cs:t_n]
    cld
    rep movsb
    mov es, [cs:t_cures]
    mov di, [cs:t_cudi]
    mov ax, [cs:t_n]
    add [es:di+12], ax
    adc word [es:di+14], 0
    mov ax, [cs:t_n]
    xor dx, dx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    clc
    retf
.eof:
    xor ax, ax
    xor dx, dx
    pop es
    pop ds
    pop di
    pop si
    pop bp
    clc
    retf
.contract:
    mov si, .msg
.pm:
    mov al, [cs:si]
    test al, al
    jz .hang
    mov dx, 0x3F8
    out dx, al
    inc si
    jmp .pm
.hang:
    cli
    hlt
    jmp .hang
.msg: db 10, "!CONTRACT", 10, 0

t_dseg: dw 0
t_doff: dw 0
t_cap:  dw 0
t_cures: dw 0
t_cudi: dw 0
t_n:    dw 0
