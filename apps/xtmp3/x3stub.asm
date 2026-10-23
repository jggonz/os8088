; =============================================================================
; os8088 - apps/xtmp3/x3stub.asm - THE STUB BACKEND (test-only; SPEC.md 121.3)
;
; An OP_SEG part speaking x3gran.inc's backend ABI - CAPS, OPEN, RESET,
; GRANULE, STATS, CLOSE - so the engine's live path, its calibration and
; ladder, the conversion and the cache can all run and be tested before the
; real STANDARD and XT backends exist (docs/plans/XTMP3-PLAN.md phase 2, the
; engine track). It is assembled only into an X3STUB=1 build; no shipped disk
; carries it.
;
; WHAT GRANULE MAKES: 576/divisor frames of a deterministic tone - a 441 Hz
; square at the OUTPUT rate, +-12,288 (S16) or 80h +- 30h (U8), in the asked
; channels - so a finished file can be checked on the host for its length and
; its content.
;
; WHAT IT COSTS: the test hook's speed for the rung it was opened for
; (x3stub.inc: X3STUB.CFG, poked into this part's data by the primary at each
; fetch). A speed S (x 100) makes each granule take its own audio duration
; x 100 / S of WALL time, spun on the 8253's channel 0 and the BIOS tick - so
; the ladder's transitions are deterministic on any host, QEMU included: a
; rung configured at 0.80x calibrates under 1.25 there whatever the host's
; speed. S = 0 costs nothing. From granule N on, rung R may run at a second
; speed (the mid-track slowdown).
;
; It never calls the kernel and never masks interrupts (the PIT is latched,
; read, and the BIOS tick read again until they agree). DS is this part's own
; on every verb (x3gran.inc section 2).
; =============================================================================
    cpu 8086
    org 0

%include "x3gran.inc"
%include "x3stub.inc"

    db 'X3', X3G_ABI, X3SB_KIND, X3V_COUNT, 0
    dw sb_caps, sb_open, sb_reset, sb_gran, sb_stats, sb_close
    times X3SB_RUNG - ($ - $$) db 0
    db 0, 0                         ; X3SB_RUNG (and a pad)
    times X3SB_CFGSZ db 0           ; X3SB_CFG
sb_open_:   db 0                    ; OPEN has run
sb_div:     db 1
sb_och:     db 1
sb_fmt:     db 0
sb_phase:   dw 0                    ; the tone's phase, 16.16 of a period...
sb_step:    dw 0                    ; ...and its step a frame
sb_grans:   dw 0, 0                 ; granules decoded (STATS)
sb_wait:    dw 0, 0                 ; a granule's cost, in PIT counts
sb_t0:      dw 0, 0
sb_dseg:    dw 0                    ; GRANULE's destination segment

; -----------------------------------------------------------------------------
; CAPS - AL = profile, AH = divisor, CL = output channels, ES:DI -> the record
; -----------------------------------------------------------------------------
sb_caps:
    cmp al, 3
    ja .prof
    cmp ah, 1
    je .d
    cmp ah, 2
    je .d
    cmp ah, 4
    jne .div
.d:
    cmp cl, 1
    jb .chan
    cmp cl, 2
    ja .chan
    push di
    push cx
    mov cx, X3C_SIZE
    xor al, al
    cld
    rep stosb
    pop cx
    pop di
    mov byte [es:di+X3C_OFFER], 0x0F
    mov byte [es:di+X3C_NSEG], 1    ; one work segment: the engine's claim
    mov word [es:di+X3C_PARA], 64   ; path is exercised (1 KB)
    mov word [es:di+X3C_TKB], 0
    mov word [es:di+X3C_PARTKB], 1
    clc
    retf
.prof:
    mov ax, X3E_PROFILE
    stc
    retf
.div:
    mov ax, X3E_DIV
    stc
    retf
.chan:
    mov ax, X3E_CHAN
    stc
    retf

; -----------------------------------------------------------------------------
; OPEN - ES:DI -> the open record
; -----------------------------------------------------------------------------
sb_open:
    cmp byte [es:di+X3O_NSEG], 1
    jb .mem
    cmp word [es:di+X3O_SEG], 0
    je .mem
    mov al, [es:di+X3O_DIV]
    mov [sb_div], al
    mov al, [es:di+X3O_OCH]
    mov [sb_och], al
    mov al, [es:di+X3O_FMT]
    cmp al, X3_FMT_U8
    ja .fmt
    mov [sb_fmt], al
    mov byte [sb_open_], 1
    xor ax, ax
    mov [sb_grans], ax
    mov [sb_grans+2], ax
    call sb_rst
    clc
    retf
.mem:
    mov ax, X3E_MEM
    stc
    retf
.fmt:
    mov ax, X3E_FMT
    stc
    retf

sb_reset:
    call sb_rst
    clc
    retf

sb_rst:
    mov word [sb_phase], 0
    mov word [sb_step], 0
    ret

sb_close:
    mov byte [sb_open_], 0
    clc
    retf

; STATS - ES:DI -> X3Z_SIZE bytes: the granule count, nothing clipped
sb_stats:
    push cx
    push di
    mov cx, X3Z_SIZE
    xor al, al
    cld
    rep stosb
    pop di
    pop cx
    mov ax, [sb_grans]
    mov [es:di+X3Z_GRAN], ax
    mov ax, [sb_grans+2]
    mov [es:di+X3Z_GRAN+2], ax
    clc
    retf

; -----------------------------------------------------------------------------
; GRANULE - ES:SI -> the record, DX:DI -> the PCM. out: CX = frames, AX = 0
; -----------------------------------------------------------------------------
sb_gran:
    cmp byte [sb_open_], 0
    jne .go
    mov ax, X3E_NOTOPEN
    stc
    retf
.go:
    push bx
    push dx
    push si
    push di
    push bp
    push es
    mov [sb_dseg], dx               ; the destination's segment
    call sb_now                     ; the cost's clock starts
    mov [sb_t0], ax
    mov [sb_t0+2], dx
    mov bx, [es:si+X3G_RATE]        ; the source's rate
    mov cl, [sb_div]                ; the output rate, for the tone's step
    mov ax, bx
    cmp cl, 2
    jb .r
    shr ax, 1
    cmp cl, 4
    jb .r
    shr ax, 1
.r:
    mov bp, ax                      ; BP = the output rate
    mov dx, 441                     ; step = 441 x 65536 / rate
    xor ax, ax
    div bp
    mov [sb_step], ax
    call sb_cost                    ; [sb_wait], from BX = the source's rate
    add word [sb_grans], 1
    adc word [sb_grans+2], 0
    mov cx, 576                     ; frames: 576 / divisor
    mov al, [sb_div]
    cmp al, 2
    jb .f
    shr cx, 1
    cmp al, 4
    jb .f
    shr cx, 1
.f:
    push cx
    mov es, [sb_dseg]
    cld
.fr:
    mov ax, [sb_phase]
    add ax, [sb_step]
    mov [sb_phase], ax
    test ah, 0x80
    mov ax, 12288
    jz .hi
    neg ax
.hi:
    cmp byte [sb_fmt], X3_FMT_U8
    jne .w
    mov al, ah                      ; the high byte, XOR 80h (x3gran's U8)
    xor al, 0x80
    stosb
    cmp byte [sb_och], 2
    jne .n
    stosb
    jmp short .n
.w:
    stosw
    cmp byte [sb_och], 2
    jne .n
    stosw
.n:
    loop .fr
    call sb_spin                    ; THE COST: the rest of the granule's wait
    pop cx                          ; CX = frames
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop bx
    xor ax, ax                      ; nothing clipped
    clc
    retf

; sb_cost - [sb_wait] = the granule's audio duration (576 source samples at
; BX Hz, in PIT counts) x 100 / the rung's speed; 0 when the speed is 0
sb_cost:
    push ax
    push bx
    push cx
    push dx
    mov word [sb_wait], 0
    mov word [sb_wait+2], 0
    mov al, [X3SB_RUNG]
    and ax, 7
    mov si, ax
    shl si, 1
    mov cx, [X3SB_CFG + X3SB_SPEED + si]
    cmp al, [X3SB_CFG + X3SB_SLR]   ; the mid-track slowdown
    jne .s
    mov ax, [X3SB_CFG + X3SB_SLAFT]
    or ax, ax
    jz .s
    cmp [sb_grans+2], word 0
    jne .slow
    cmp [sb_grans], ax
    jb .s
.slow:
    mov cx, [X3SB_CFG + X3SB_SLSPD]
.s:
    jcxz .out
    mov si, cx                      ; SI = the speed
    mov ax, 576 * 100 / 2           ; 576 x 1,193,182 / rate x 100 / speed, as
    mov cx, ax                      ; (28,800 x 1,193,182 / rate) x 2 / speed
    mov ax, 0x34DE
    mov dx, 0x0012
    call sb_mdiv                    ; DX:AX = 28,800 x 1,193,182 / rate
    mov cx, 2
    mov bx, si
    call sb_mdiv
    mov [sb_wait], ax
    mov [sb_wait+2], dx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sb_mdiv - DX:AX x CX / BX -> DX:AX (saturating). keeps the rest
sb_mdiv:
    push si
    push di
    mov si, dx
    mul cx
    mov di, ax
    mov ax, si
    mov si, dx
    mul cx
    add ax, si
    adc dx, 0
    cmp dx, bx
    jae .sat
    div bx
    mov si, ax
    mov ax, di
    div bx
    mov dx, si
    jmp short .o
.sat:
    mov ax, 0xFFFF
    mov dx, ax
.o:
    pop di
    pop si
    ret

; sb_spin - until [sb_wait] PIT counts have passed since [sb_t0]
sb_spin:
    push ax
    push dx
    mov ax, [sb_wait]
    or ax, [sb_wait+2]
    jz .out
.l:
    call sb_now
    sub ax, [sb_t0]
    sbb dx, [sb_t0+2]
    cmp dx, [sb_wait+2]
    jb .l
    ja .out
    cmp ax, [sb_wait]
    jb .l
.out:
    pop dx
    pop ax
    ret

; sb_now - DX:AX = the BIOS tick (high) and the 8253's channel 0 counting up
; (low): one count 838 ns, one tick 65,536 counts. No cli: the latch holds
; the count until it is read, and the tick is read again until it agrees
sb_now:
    push es
    mov ax, 0x40
    mov es, ax
.a:
    mov dx, [es:0x6C]
    mov al, 0
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    neg ax
    cmp dx, [es:0x6C]
    jne .a
    pop es
    ret
