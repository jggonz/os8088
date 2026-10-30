; =============================================================================
; os8088 - tests/x3xtbench/x3xtbench.asm - THE XT BACKEND'S BENCH
;                                           (SPEC.md 121.3, 121.4)
;
; NOT shipped software (tests/). tests/x3bench/x3bench.asm's mechanism with
; the XT part in the STANDARD part's place: booted by tests/x3bench/
; x3bboot.asm from a raw hard-disk image that tests/x3xtbench/x3xtbench.py
; builds, it runs the SHIPPING front end - apps/xtmp3/x3pars.inc, %included
; here, and build/x3front.bin - into the SHIPPING XT part, build/x3xt.bin,
; far-called through its header's vectors exactly as the engine calls it
; (DS = the part's segment, a 384-byte worker stack in a segment of its own,
; the low-water mark reported). Every case: the fixture opened, its frames
; staged through a small ring, each granule record through GRANULE, its PCM
; out on QEMU's debug console (port E9h) in tests/x3bench's framing:
;
;   'R' type length(dword) payload
;   P  PCM of one granule     Z  the part's STATS (X3Z)    W  stack low water
;   E  the end of a case      F  a failure, its text       Q  all done
;
; Memory: 0060:0 the directory as the boot sector reads it, moved to 0800:0
; at entry so KERNEL_SEG holds OSAPI_SEG's cell (x3rmseg.inc), 1000:0 this, 1800:0 FRONT, 1E00:0 the XT
; part (64 KB at most), 2E00:0 the stack (SP = 180h), 2E20:0 the ring (8 KB),
; 3020:0 PCM, 3080:0 the file (128 KB), 5080:0 the part's segments (CAPS'
; paragraphs, end to end).
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "x3gran.inc"
%include "x3ftab.inc"

SEG_BOOTDIR equ 0x0060              ; where x3bboot.asm reads the directory...
SEG_DIR     equ 0x0800              ; ...and where it is moved to at entry: its
                                    ; 8 KB at 0060:0 covers KERNEL_SEG's
                                    ; OSAPI_SEG cell, which the XT part's OPEN
                                    ; far-calls (x3rmseg.inc below)
%ifndef X3RM_SEGOFF
  %error "X3RM_SEGOFF is OSAPI_SEG's offset (x3xtbench.py passes it)"
%endif
SEG_FRONT   equ 0x1800
SEG_XT      equ 0x1E00              ; up to 64 KB
SEG_STACK   equ 0x2E00
STACK_TOP   equ 0x0180
SEG_RING    equ 0x2E20
RING_K      equ 13
SEG_PCM     equ 0x3020
SEG_FILE    equ 0x3080              ; 128 KB
SEG_WORK    equ 0x5080              ; XT Standard's 14,993 paragraphs end at 8B11h
PORT_OUT    equ 0xE9
PORT_EXIT   equ 0xF4

; the directory (x3xtbench.py writes it; the boot sector reads [14])
D_FRONT     equ 4                   ; dw lba, dw sectors
D_XT        equ 8
D_NCASE     equ 12
D_HARNESS   equ 14
D_CASES     equ 16
C_LBA       equ 0                   ; dd
C_SIZE      equ 4                   ; dd
C_PROF      equ 8                   ; db the XT profile
C_DIV       equ 9                   ; db its divisor
C_CHUNK     equ 10                  ; dw the producer's refill
C_NAME      equ 12                  ; 12 db
C_SZ        equ 32

entry:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, STACK_TOP
    sti
    mov ax, cs
    mov ds, ax
    mov es, ax
    cld
    mov bl, dl
    mov di, bss_start
    mov cx, bss_end - bss_start
    xor al, al
    rep stosb
    mov [b_drive], bl
    push ds                         ; the directory out of KERNEL_SEG's way,
    mov ax, SEG_BOOTDIR             ; then OSAPI_SEG answered there the way a
    mov ds, ax                      ; real-mode kernel answers it (x3rmseg.inc)
    xor si, si
    mov ax, SEG_DIR
    mov es, ax
    xor di, di
    mov cx, 16 * 512 / 2
    rep movsw
    pop ds
    mov ax, SEG_BOOTDIR
    mov es, ax
    mov si, x3rm_seg
    mov di, X3RM_SEGOFF
    mov cx, x3rm_seg_end - x3rm_seg
    rep movsb
    mov ax, SEG_DIR
    mov es, ax
    mov ax, [es:D_FRONT]
    mov cx, [es:D_FRONT+2]
    mov dx, SEG_FRONT
    call b_load
    mov ax, [es:D_XT]
    mov cx, [es:D_XT+2]
    mov dx, SEG_XT
    call b_load
    mov ax, SEG_FRONT
    mov dx, SEG_FRONT
    call x3p_init
    jnc .fok
    mov si, m_finit
    call b_fail
.fok:
    mov ax, SEG_XT                  ; the XT part's header
    mov es, ax
    cmp word [es:X3K_SIG], X3K_SIGVAL
    jne .bad
    cmp byte [es:X3K_ABI], X3G_ABI
    jne .bad
    cmp byte [es:X3K_KIND], X3K_XT
    je .case0
.bad:
    mov si, m_xabi
    call b_fail
.case0:
    mov word [b_case], 0
.case:
    mov ax, SEG_DIR
    mov es, ax
    mov ax, [b_case]
    cmp ax, [es:D_NCASE]
    jae .done
    mov cl, 5
    shl ax, cl
    add ax, D_CASES
    mov si, ax
    push ds
    pop es
    push ds
    mov ax, SEG_DIR
    mov ds, ax
    mov di, b_c
    mov cx, C_SZ
    rep movsb
    pop ds
    call b_runcase
    inc word [b_case]
    jmp short .case
.done:
    mov al, 'Q'
    xor cx, cx
    call b_rhead
    mov dx, PORT_EXIT
    mov al, 0
    out dx, al
.halt:
    hlt
    jmp short .halt

%include "x3rmseg.inc"

m_finit:    db 'x3p_init refused the FRONT part', 0
m_xabi:     db 'the XT part header is not x3gran.inc', 0
m_caps:     db 'XT CAPS refused', 0
m_open:     db 'XT OPEN refused', 0
m_hang:     db 'x3p_next answered MORE with nothing left to feed', 0
m_gran:     db 'XT GRANULE failed', 0
m_disk:     db 'disk read failed', 0

; b_load - CX sectors from LBA AX to segment DX
b_load:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [b_dap_lba], ax
    mov word [b_dap_lba+2], 0
    mov [b_dap_seg], dx
    call b_rdloop
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; b_rdloop - CX sectors from [b_dap_lba] to [b_dap_seg]:0, 64 at a time
b_rdloop:
.c: jcxz .out
    mov ax, cx
    cmp ax, 64
    jbe .n
    mov ax, 64
.n: mov [b_dap_cnt], ax
    sub cx, ax
    push ax
    push cx
    mov si, b_dap
    mov dl, [b_drive]
    mov ah, 0x42
    int 0x13
    pop cx
    pop ax
    jc .err
    add [b_dap_lba], ax
    adc word [b_dap_lba+2], 0
    mov bx, 32
    mul bx
    add [b_dap_seg], ax
    jmp short .c
.out:
    ret
.err:
    mov si, m_disk
    call b_fail

b_loadfile:
    mov ax, [b_c+C_LBA]
    mov dx, [b_c+C_LBA+2]
    mov [b_dap_lba], ax
    mov [b_dap_lba+2], dx
    mov word [b_dap_seg], SEG_FILE
    mov ax, [b_c+C_SIZE]
    mov dx, [b_c+C_SIZE+2]
    add ax, 511
    adc dx, 0
    mov cx, 512
    div cx
    mov cx, ax
    jmp b_rdloop

; b_runcase - one case: open, the XT part's CAPS and OPEN, every granule
b_runcase:
    call b_loadfile
    call b_stackfill
    mov di, b_info
    mov ax, [b_c+C_SIZE]
    mov dx, [b_c+C_SIZE+2]
    call x3p_inew
    xor ax, ax
    xor dx, dx
    mov byte [b_reads], 0
.rd:
    and ax, 0xFE00
    call b_window
    mov di, b_info
    call x3p_open
    inc byte [b_reads]
    cmp ax, X3P_DONE
    je .opened
    cmp byte [b_reads], 200
    jae .opened
    mov ax, [b_info+X3I_WANT]
    mov dx, [b_info+X3I_WANT+2]
    jmp short .rd
.opened:
    cmp byte [b_info+X3I_REF], 0
    je .go
    jmp .end
.go:
    mov ax, (1 << RING_K) - 1
    mov [b_ring+X3R_MASK], ax
    mov word [b_ring+X3R_SEG], SEG_RING
    mov word [b_ring+X3R_RD], 0
    mov word [b_ring+X3R_WR], 0
    mov byte [b_ring+X3R_EOF], 0
    mov ax, [b_info+X3I_AUDIO]
    mov dx, [b_info+X3I_AUDIO+2]
    mov [b_ring+X3R_POS], ax
    mov [b_ring+X3R_POS+2], dx
    mov [b_fpos], ax
    mov [b_fpos+2], dx
    mov di, b_info
    mov si, b_ring
    call x3p_start
    call b_xopen
.loop:
    call b_refill
    mov si, b_ring
    call x3p_next
    jnc .frame
    cmp ax, X3P_END
    je .eos
    cmp byte [b_ring+X3R_EOF], 0
    je .loop
    mov si, m_hang
    call b_fail
.frame:
    mov [b_ngr], al
    mov byte [b_gr], 0
.g:
    mov al, [b_gr]
    call x3p_gran                   ; DX:AX = the record
    call b_granule
    inc byte [b_gr]
    mov al, [b_gr]
    cmp al, [b_ngr]
    jb .g
    jmp short .loop
.eos:
    call b_xstats
.end:
    call b_stackmark
    mov al, 'E'
    xor cx, cx
    call b_rhead
    ret

; b_window - DX:AX = a 512-aligned offset: ES:BX -> the file there, CX = the
; bytes of an 8 KB window that exist
b_window:
    push ax
    push dx
    mov cx, [b_c+C_SIZE]
    mov bx, [b_c+C_SIZE+2]
    sub cx, ax
    sbb bx, dx
    jc .z
    or bx, bx
    jnz .full
    cmp cx, 8192
    jbe .have
.full:
    mov cx, 8192
    jmp short .have
.z: xor cx, cx
.have:
    mov bx, ax
    and bx, 15
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, SEG_FILE
    mov es, ax
    pop cx
    pop dx
    pop ax
    ret

; b_refill - the producer: up to the case's chunk, as the ring has room
b_refill:
    cmp byte [b_ring+X3R_EOF], 0
    jne .out
    mov ax, [b_ring+X3R_MASK]
    inc ax
    mov cx, [b_ring+X3R_WR]
    sub cx, [b_ring+X3R_RD]
    sub ax, cx
    cmp ax, [b_c+C_CHUNK]
    jbe .r
    mov ax, [b_c+C_CHUNK]
.r: mov cx, [b_info+X3I_AEND]
    mov dx, [b_info+X3I_AEND+2]
    sub cx, [b_fpos]
    sbb dx, [b_fpos+2]
    or dx, dx
    jnz .n
    cmp cx, ax
    jae .n
    mov ax, cx
.n: mov cx, ax
    jcxz .eof
    push ds
    mov ax, [b_fpos]
    mov dx, [b_fpos+2]
    mov si, ax
    and si, 15
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, SEG_FILE
    pop cx
    mov di, [b_ring+X3R_WR]
    mov bx, [b_ring+X3R_MASK]
    push cx
    mov dx, SEG_RING
    mov es, dx
    mov ds, ax
.cp:
    and di, bx
    movsb
    loop .cp
    pop cx
    pop ds
    add [b_ring+X3R_WR], cx
    add [b_fpos], cx
    adc word [b_fpos+2], 0
    mov ax, [b_fpos]
    cmp ax, [b_info+X3I_AEND]
    jne .out
    mov ax, [b_fpos+2]
    cmp ax, [b_info+X3I_AEND+2]
    jne .out
.eof:
    mov byte [b_ring+X3R_EOF], 1
.out:
    ret

; -----------------------------------------------------------------------------
; THE XT PART, through its verbs
; -----------------------------------------------------------------------------
; b_xcall - verb BX of the XT part, DS = its segment, the rest as given
b_xcall:
    push ds
    push bp
    mov bp, SEG_XT
    shl bx, 1
    push es
    mov es, bp
    mov bx, [es:X3K_VEC+bx]
    pop es
    push cs
    push word [b_retv]
    push bp
    push bx
    mov ds, bp
    retf
b_xret:
    pop bp
    pop ds
    ret

; b_xopen - CAPS for the case's profile, the segments end to end from
; SEG_WORK, then OPEN
b_xopen:
    mov al, [b_c+C_PROF]
    mov ah, [b_c+C_DIV]
    mov cl, 1
    push ds
    pop es
    mov di, b_caps
    mov bx, X3V_CAPS
    call b_xcall
    jnc .c
    mov si, m_caps
    call b_fail
.c: mov di, b_open
    mov al, [b_c+C_PROF]
    mov [di+X3O_PROF], al
    mov al, [b_c+C_DIV]
    mov [di+X3O_DIV], al
    mov byte [di+X3O_OCH], 1
    mov byte [di+X3O_FMT], X3_FMT_U8
    mov al, [b_info+X3I_NCH]
    mov [di+X3O_SCH], al
    mov al, [b_info+X3I_VER]
    mov [di+X3O_VER], al
    mov al, [b_info+X3I_SRI]
    mov [di+X3O_SRI], al
    mov word [di+X3O_GAIN], 0
    mov cl, [b_caps+X3C_NSEG]
    mov [di+X3O_NSEG], cl
    xor ch, ch
    mov si, b_caps+X3C_PARA
    mov bx, X3O_SEG
    mov ax, SEG_WORK
.s: mov [di+bx], ax
    add ax, [si]
    inc si
    inc si
    inc bx
    inc bx
    loop .s
    push ds
    pop es
    mov bx, X3V_OPEN
    call b_xcall
    jnc .o
    mov si, m_open
    call b_fail
.o: ret

; b_granule - the record at DX:AX through GRANULE, its PCM out as 'P'
b_granule:
    mov es, dx
    mov si, ax
    mov dx, SEG_PCM
    xor di, di
    mov bx, X3V_GRANULE
    call b_xcall
    jnc .ok
    mov si, m_gran
    call b_fail
.ok:
    push cx                         ; CX = samples, one byte each (U8 mono)
    mov al, 'P'
    call b_rhead
    pop cx
    mov ax, SEG_PCM
    mov es, ax
    xor si, si
    call b_out
    ret

b_xstats:
    push ds
    pop es
    mov di, b_stats
    mov bx, X3V_STATS
    call b_xcall
    mov al, 'Z'
    mov cx, X3Z_SIZE
    call b_rhead
    push ds
    pop es
    mov si, b_stats
    call b_out
    ret

; -----------------------------------------------------------------------------
; THE STACK: A5h at a case's start, its low-water mark reported
; -----------------------------------------------------------------------------
b_stackfill:
    push es
    push di
    push cx
    mov ax, SEG_STACK
    mov es, ax
    xor di, di
    mov cx, sp
    sub cx, 16
    mov al, 0xA5
    rep stosb
    pop cx
    pop di
    pop es
    ret

b_stackmark:
    push ds
    mov ax, SEG_STACK
    mov ds, ax
    xor si, si
.s: cmp byte [si], 0xA5
    jne .f
    inc si
    jmp short .s
.f: pop ds
    mov ax, STACK_TOP
    sub ax, si
    mov [b_wm], ax
    mov al, 'W'
    mov cx, 2
    call b_rhead
    push ds
    pop es
    mov si, b_wm
    mov cx, 2
    call b_out
    ret

; -----------------------------------------------------------------------------
; OUTPUT: b_rhead AL = type, CX = length; b_out ES:SI, CX bytes
; -----------------------------------------------------------------------------
b_rhead:
    push ax
    push dx
    mov dx, PORT_OUT
    mov ah, al
    mov al, 'R'
    out dx, al
    mov al, ah
    out dx, al
    mov al, cl
    out dx, al
    mov al, ch
    out dx, al
    xor al, al
    out dx, al
    out dx, al
    pop dx
    pop ax
    ret

b_out:
    push ax
    push cx
    push dx
    push si
    mov dx, PORT_OUT
    jcxz .z
.c: mov al, [es:si]
    inc si
    out dx, al
    loop .c
.z: pop si
    pop dx
    pop cx
    pop ax
    ret

b_fail:                             ; SI = the text: say it, stop
    push ds
    pop es
    mov di, si
    xor cx, cx
.l: cmp byte [di], 0
    je .h
    inc di
    inc cx
    jmp short .l
.h: mov al, 'F'
    call b_rhead
    call b_out
    mov dx, PORT_EXIT
    mov al, 1
    out dx, al
.x: hlt
    jmp short .x

; =============================================================================
; THE SHIPPING SOURCE
; =============================================================================
%include "x3pars.inc"

; =============================================================================
; DATA AND BSS
; =============================================================================
b_dap:      db 16, 0
b_dap_cnt:  dw 0
b_dap_off:  dw 0
b_dap_seg:  dw 0
b_dap_lba:  dw 0, 0, 0, 0
b_retv:     dw b_xret

    align 2
bss_start:
%macro X3B 2
    %1: times %2 db 0
%endmacro
    X3P_BSS
b_drive:    db 0
b_case:     dw 0
b_reads:    db 0
b_ngr:      db 0
b_gr:       db 0
    align 2
b_wm:       dw 0
b_fpos:     dd 0
b_c:        times C_SZ db 0
b_info:     times X3I_SIZE db 0
b_ring:     times X3R_SIZE db 0
b_caps:     times X3C_SIZE db 0
b_open:     times X3O_SIZE db 0
b_stats:    times X3Z_SIZE db 0
bss_end:
