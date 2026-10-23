; =============================================================================
; os8088 - tests/x3bench/x3bench.asm - THE XTMP3 DECODER BENCH (SPEC.md 121.4)
;
; NOT shipped software (tests/). A bare-metal harness, booted by x3bboot.asm
; from a raw hard-disk image that tests/x3bench/x3bench.py builds, that runs
; the SHIPPING decoder on a real x86 with nothing else in the machine:
;
;   * apps/xtmp3/x3pars.inc, %included here - the very source the package
;     assembles (shared as source, never copied);
;   * build/x3front.bin and build/x3std.bin, the package's PARTS as they are
;     built, loaded from the image into segments of their own and far-called
;     through their verb tables exactly as the engine calls them, DS = the
;     part's own segment, with a 384-byte stack in a segment of its own (SS !=
;     DS, a worker's budget: SPEC.md 121.3's 64 bytes a verb is checked).
;
; It runs every CASE of the directory: opens the fixture with x3p_open over
; simulated cluster-aligned reads, decodes it through an X3R ring that a
; producer feeds in small chunks (so X3P_MORE, the wrap and a scan across a
; refill are all exercised), and reports through QEMU's debug console (port
; E9h) as framed records: 'R' type length(dword) payload.
;
;   I  the X3I record                  G  one granule record, compacted
;   P  PCM of one granule              C  the verdict counters (X3PC)
;   Z  the backend's STATS (X3Z)       W  the stack's low-water mark
;   E  the end of a case               F  a failure, its text
;   Q  every case done (then port F4h ends QEMU: isa-debug-exit)
;
; Memory: 0060:0 the directory, 1000:0 this, 2000:0 FRONT, 3000:0 STD,
; 4000:0 the stack (SP = 180h), 5000:0 the ring, 6000:0 the file (128 KB),
; 8000:0 PCM.
; =============================================================================

    cpu 8086
    bits 16
    org 0

%include "x3gran.inc"
%include "x3ftab.inc"

SEG_DIR     equ 0x0060
SEG_FRONT   equ 0x2000
SEG_STD     equ 0x3000
SEG_STACK   equ 0x4000
STACK_TOP   equ 0x0180
SEG_RING    equ 0x5000
SEG_FILE    equ 0x6000
SEG_PCM     equ 0x8000
PORT_OUT    equ 0xE9
PORT_EXIT   equ 0xF4

; the directory (x3bench.py writes it)
D_FRONT     equ 4                   ; dw lba, dw sectors
D_STD       equ 8
D_NCASE     equ 12
D_CASES     equ 16
C_LBA       equ 0                   ; dd
C_SIZE      equ 4                   ; dd
C_DIV       equ 8                   ; db
C_MONO      equ 9                   ; db
C_MODE      equ 10                  ; db 0 front records, 1 PCM
C_RINGK     equ 11                  ; db log2 of the ring's size
C_CHUNK     equ 12                  ; dw the producer's refill
C_NAME      equ 14                  ; 12 db
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
    mov di, bss_start               ; the bss: zero
    mov cx, bss_end - bss_start
    xor al, al
    rep stosb
    mov [b_drive], bl
    ; the parts
    mov ax, SEG_DIR
    mov es, ax
    mov ax, [es:D_FRONT]
    mov cx, [es:D_FRONT+2]
    mov dx, SEG_FRONT
    call b_load
    mov ax, [es:D_STD]
    mov cx, [es:D_STD+2]
    mov [b_hasstd], cx
    jcxz .nostd
    mov dx, SEG_STD
    call b_load
.nostd:
    mov ax, SEG_FRONT
    mov dx, SEG_FRONT
    call b_wcall                    ; x3p_init
    jnc .fok
    mov si, m_finit
    call b_fail
.fok:
    ; every case
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
    push ds                         ; the case, into ours
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

m_finit:    db 'x3p_init refused the FRONT part', 0
m_sinit:    db 'the STANDARD part refused', 0
m_hang:     db 'x3p_next answered MORE with nothing left to feed', 0
m_gran:     db 'GRANULE failed', 0

; -----------------------------------------------------------------------------
; b_wcall - x3p_init with the worker's stack checked: AX, DX as x3p_init
; (a wrapper so every x3pars call can be water-marked the same way)
; -----------------------------------------------------------------------------
b_wcall:
    call x3p_init
    ret

; -----------------------------------------------------------------------------
; b_load - CX sectors from LBA AX to segment DX
; -----------------------------------------------------------------------------
b_load:
    push ax
    push bx
    push cx
    push dx
    push si
    mov [b_dap_lba], ax
    mov word [b_dap_lba+2], 0
    mov [b_dap_seg], dx
.c:
    jcxz .out
    mov ax, cx
    cmp ax, 64
    jbe .n
    mov ax, 64
.n:
    mov [b_dap_cnt], ax
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
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.err:
    mov si, m_disk
    call b_fail
m_disk:     db 'disk read failed', 0

; b_load32 - the case's file: [b_c+C_SIZE] bytes from [b_c+C_LBA] to SEG_FILE
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
.c:
    jcxz .out
    mov ax, cx
    cmp ax, 64
    jbe .n
    mov ax, 64
.n:
    mov [b_dap_cnt], ax
    sub cx, ax
    push ax
    push cx
    mov si, b_dap
    mov dl, [b_drive]
    mov ah, 0x42
    int 0x13
    pop cx
    pop ax
    jc b_load.err
    add [b_dap_lba], ax
    adc word [b_dap_lba+2], 0
    mov bx, 32
    mul bx
    add [b_dap_seg], ax
    jmp short .c
.out:
    ret

; -----------------------------------------------------------------------------
; b_runcase - one case
; -----------------------------------------------------------------------------
b_runcase:
    call b_loadfile
    call b_stackfill
    ; --- the open, over simulated reads ---
    mov di, b_info
    mov ax, [b_c+C_SIZE]
    mov dx, [b_c+C_SIZE+2]
    call x3p_inew
    xor ax, ax
    xor dx, dx
    mov byte [b_reads], 0
.rd:
    and ax, 0xFE00                  ; a 512-byte "cluster" window of 8 KB
    call b_window                   ; ES:BX, CX for [DX:AX, ...)
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
    mov al, 'I'
    mov cx, X3I_SIZE
    call b_rhead
    mov si, b_info
    push ds
    pop es
    call b_out
    cmp byte [b_info+X3I_REF], 0
    je .go
    jmp .end
.go:
    ; --- the ring and its producer ---
    mov cl, [b_c+C_RINGK]
    mov ax, 1
    shl ax, cl
    dec ax
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
    ; --- the backend ---
    cmp byte [b_c+C_MODE], 1
    jne .loop
    call b_sopen
.loop:
    call b_refill
    mov si, b_ring
    call x3p_next
    jnc .frame
    cmp ax, X3P_END
    je .eos
    cmp byte [b_ring+X3R_EOF], 0    ; MORE: there must be something to give
    je .loop
    mov si, m_hang
    call b_fail
.frame:
    mov [b_ngr], al
    mov byte [b_gr], 0
.g:
    mov al, [b_gr]
    call x3p_gran                   ; DX:AX = the record
    cmp byte [b_c+C_MODE], 1
    je .pcm
    call b_dumprec
    jmp short .gn
.pcm:
    call b_granule
.gn:
    inc byte [b_gr]
    mov al, [b_gr]
    cmp al, [b_ngr]
    jb .g
    jmp short .loop
.eos:
    mov al, 'C'                     ; the verdict counters
    mov cx, X3PC_SIZE
    call b_rhead
    push ds
    pop es
    mov di, b_cnt
    call x3p_counts
    mov si, b_cnt
    call b_out
    cmp byte [b_c+C_MODE], 1
    jne .end
    call b_sstats
.end:
    call b_stackmark
    mov al, 'E'
    xor cx, cx
    call b_rhead
    ret

; b_window - DX:AX = a 512-aligned offset: ES:BX -> the file there, CX = the
; bytes of an 8 KB window that exist (0 past the end)
b_window:
    push ax
    push dx
    mov cx, [b_c+C_SIZE]            ; left = size - off
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
.z:
    xor cx, cx
.have:
    mov bx, ax                      ; ES:BX = SEG_FILE + off / 16 : off & 15
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
    mov ax, [b_ring+X3R_MASK]       ; room = size - (WR - RD)
    inc ax
    mov cx, [b_ring+X3R_WR]
    sub cx, [b_ring+X3R_RD]
    sub ax, cx
    cmp ax, [b_c+C_CHUNK]
    jbe .r
    mov ax, [b_c+C_CHUNK]
.r:
    mov cx, [b_info+X3I_AEND]       ; and no further than the audio's end
    mov dx, [b_info+X3I_AEND+2]
    sub cx, [b_fpos]
    sbb dx, [b_fpos+2]
    or dx, dx
    jnz .n
    cmp cx, ax
    jae .n
    mov ax, cx
.n:
    mov cx, ax
    jcxz .eof
    push ds
    mov ax, [b_fpos]                ; DS:SI = the file at fpos
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

; b_dumprec - the record at DX:AX, compacted: the header, each channel's
; six bytes and its ix and q below NZ, the plan when it is per line
b_dumprec:
    mov es, dx
    mov si, ax
    mov cx, X3G_HDR + 12            ; the length first
    mov bx, X3G_CH0
    call .chl
    mov bx, X3G_CH1
    call .chl
    cmp byte [es:si+X3G_PLAN], X3G_PLAN_PL
    jne .np
    add cx, 576
.np:
    mov al, 'G'
    call b_rhead
    mov cx, X3G_HDR
    call b_out
    mov bx, X3G_CH0
    call .ch
    mov bx, X3G_CH1
    call .ch
    cmp byte [es:si+X3G_PLAN], X3G_PLAN_PL
    jne .out
    push si
    add si, X3G_PL
    mov cx, 576
    call b_out
    pop si
.out:
    ret
.chl:                               ; CX += the channel's bytes
    cmp byte [es:si+bx+X3G_C_OK], 0
    je .chl0
    mov ax, [es:si+bx+X3G_C_NZ]
    shl ax, 1
    shl ax, 1
    add cx, ax
.chl0:
    ret
.ch:                                ; the channel at SI + BX
    push si
    add si, bx
    mov cx, 6
    call b_out
    cmp byte [es:si+X3G_C_OK], 0
    je .ch0
    mov cx, [es:si+X3G_C_NZ]
    shl cx, 1
    push si
    add si, X3G_C_IX
    call b_out
    pop si
    add si, X3G_C_Q
    call b_out
.ch0:
    pop si
    ret

; -----------------------------------------------------------------------------
; THE BACKEND: STANDARD, through its verbs
; -----------------------------------------------------------------------------
; b_scall - verb BX of the STANDARD part, DS = its segment, AX..DI as given
b_scall:
    push ds
    push bp
    mov bp, SEG_STD
    shl bx, 1
    push es
    mov es, bp
    mov bx, [es:X3K_VEC+bx]
    pop es
    push cs                         ; the return, far
    push word [b_retv]
    push bp                         ; the target: its code is the same
    push bx                         ; segment here
    mov ds, bp
    retf
b_sret:
    pop bp
    pop ds
    ret

b_sopen:
    mov ax, SEG_STD                 ; the header
    mov es, ax
    cmp word [es:X3K_SIG], X3K_SIGVAL
    jne .bad
    cmp byte [es:X3K_ABI], X3G_ABI
    jne .bad
    push ds
    pop es
    mov di, b_open
    mov byte [di+X3O_PROF], 0
    mov al, [b_c+C_DIV]
    mov [di+X3O_DIV], al
    mov al, [b_info+X3I_NCH]
    mov [di+X3O_SCH], al
    cmp byte [b_c+C_MONO], 0
    je .och
    mov al, 1
.och:
    mov [di+X3O_OCH], al
    mov [b_och], al
    mov byte [di+X3O_FMT], X3_FMT_S16
    mov al, [b_info+X3I_VER]
    mov [di+X3O_VER], al
    mov al, [b_info+X3I_SRI]
    mov [di+X3O_SRI], al
    mov word [di+X3O_GAIN], 0
    mov byte [di+X3O_NSEG], 0
    mov bx, X3V_OPEN
    call b_scall
    jc .bad
    ret
.bad:
    mov si, m_sinit
    call b_fail

; b_granule - the record at DX:AX through GRANULE, its PCM out as 'P'
b_granule:
    mov es, dx
    mov si, ax
    mov dx, SEG_PCM
    xor di, di
    mov bx, X3V_GRANULE
    call b_scall
    jnc .ok
    mov si, m_gran
    call b_fail
.ok:
    mov ax, cx                      ; frames x channels x 2
    cmp byte [b_och], 2
    jne .m
    shl ax, 1
.m:
    shl ax, 1
    mov cx, ax
    push cx
    mov al, 'P'
    push ds
    pop es
    call b_rhead
    pop cx
    mov ax, SEG_PCM
    mov es, ax
    xor si, si
    call b_out
    ret

b_sstats:
    push ds
    pop es
    mov di, b_stats
    mov bx, X3V_STATS
    call b_scall
    mov al, 'Z'
    mov cx, X3Z_SIZE
    call b_rhead
    push ds
    pop es
    mov si, b_stats
    call b_out
    ret

; -----------------------------------------------------------------------------
; THE STACK: filled with A5h at a case's start; its low-water mark reported
; -----------------------------------------------------------------------------
b_stackfill:
    push es
    push di
    push cx
    mov ax, SEG_STACK
    mov es, ax
    xor di, di
    mov cx, sp
    sub cx, 16                      ; (our own frame stays)
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
.s:
    cmp byte [si], 0xA5
    jne .f
    inc si
    jmp short .s
.f:
    pop ds
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
.c:
    mov al, [es:si]
    inc si
    out dx, al
    loop .c
.z:
    pop si
    pop dx
    pop cx
    pop ax
    ret

b_fail:                             ; SI = the text, in ours: say it, stop
    push ds
    pop es
    mov di, si
    xor cx, cx
.l:
    cmp byte [di], 0
    je .h
    inc di
    inc cx
    jmp short .l
.h:
    mov al, 'F'
    call b_rhead
    call b_out
    mov dx, PORT_EXIT
    mov al, 1
    out dx, al
.x:
    hlt
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
b_retv:     dw b_sret

    align 2
bss_start:
%macro X3B 2
    %1: times %2 db 0
%endmacro
    X3P_BSS
b_drive:    db 0
b_hasstd:   dw 0
b_case:     dw 0
b_reads:    db 0
b_ngr:      db 0
b_gr:       db 0
b_och:      db 0
    align 2
b_wm:       dw 0
b_fpos:     dd 0
b_c:        times C_SZ db 0
b_info:     times X3I_SIZE db 0
b_ring:     times X3R_SIZE db 0
b_cnt:      times X3PC_SIZE db 0
b_open:     times X3O_SIZE db 0
b_stats:    times X3Z_SIZE db 0
bss_end:
