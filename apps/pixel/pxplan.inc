; =============================================================================
; os8088 - apps/pixel/pxplan.inc
;
; THE MIXING PLANS (SPEC.md 106.11), SHARED SOURCE of every decoder part
; (SPEC.md 106.20): a part %includes this file, and the resident package's
; px_tables asks the part of the shown picture's kind for the 256 plans of
; its palette through PXV_PLANS. Until wave 4 this was the resident's own:
; the CUBE's and GREY's plans shipped, a PAL palette's searched. Both moved
; here - 4.6 KB of the package's room - and the resident BANKS the shown
; picture's plans across an open (px_pplan), so a cancel's restore needs no
; part to put them back.
;
; THE INCLUDER IS LINKED against the package (build/pxlink.inc): [px_pal],
; [px_plan], [px_cur + ...], [px_abort], [px_onworker] and [px_win] are
; the package's own, through DS as the vector is entered. This file's
; tables are the part's, through CS (apps/pixel/pxpart.inc's rule 1), and
; its scratch the part's too, written through ES = [cs:PXP_SELF] (rule 6).
; tools/pixelsim.py's plan_fast is the search, check for check.
; =============================================================================

; pl_plans - PXV_PLANS's body: DS = the package. [px_plan] := the 4bpp plans
; of [px_pal] for the picture's mode - the CUBE's and GREY's copied, a PAL
; palette's searched entry by entry ([px_cur + PXR_NPAL] of them, black
; after), with the cancel and the liveness every sixteen on the worker.
; out CF = 0, or CF = 1 AX = PXD_ABORT. Preserves all but AX
pl_plans:
    cmp cl, 1                       ; CL = 1: the Histogram's statistics
    jne .c                          ; instead (pl_stats, SPEC.md 106.25),
    jmp pl_stats                    ; CL = 2 its counts (pl_hcount)
.c:
    cmp cl, 2
    jne .pl
    jmp pl_hcount
.pl:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES:DI = the package's [px_plan]
    mov di, px_plan
    cld
    mov al, [px_cur + PXR_PMODE]
    mov si, px_plcube
    cmp al, PM_CUBE
    je .ship
    mov si, px_plgrey
    cmp al, PM_GREY
    jne .pal
.ship:
    push ds                         ; the shipped plans: this part's, CS
    mov ds, [cs:PXP_SELF]
    mov cx, 768 / 2
    rep movsw
    pop ds
    clc
    jmp short .out
.pal:
    mov si, px_pal
    xor bx, bx
.p:
    cmp bx, [px_cur + PXR_NPAL]
    jb .search
    xor ax, ax                      ; past the file's palette: black
    xor dl, dl
    jmp short .st
.search:
    call pl_plan1
.st:
    mov [es:di], al
    mov [es:di + 1], ah
    mov [es:di + 2], dl
    add si, 3
    add di, 3
    test bl, 15                     ; liveness and the cancel, every 16
    jnz .pn
    call pl_poll
    jc .ab
.pn:
    inc bx
    cmp bx, 256
    jb .p
    clc
    jmp short .out
.ab:
    mov ax, PXD_ABORT
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pl_poll - on the worker ([px_onworker]): the cancel, and OSAPI_TASK_ALIVE
; (set by px_wjob since 106.23). On the UI task: nothing. CF = 1 the user
; cancelled.
; Preserves all
pl_poll:
    cmp byte [px_onworker], 0
    je .ok
    cmp byte [px_abort], 0
    jne .ab
    push ax
    push bx
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE
    pop bx
    pop ax
.ok:
    clc
    ret
.ab:
    stc
    ret

%include "pxplan1.inc"             ; pl_plan1, its tables and scratch


; =============================================================================
; pl_stats - THE HISTOGRAM'S STATISTICS (SPEC.md 106.12), the resident's
; until wave 8 gave its room to the animation (106.25): [px_hist]'s counts
; in the shown master's tail folded through the palette into 256 bins of
; channel [px_hchan] (0 luma, 1 R, 2 G, 3 B), and Mean, Std Dev, Min, Max
; from them. UI task, DS = the package. Preserves all but AX; CF = 0
; =============================================================================
pl_stats:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call pl_htail                   ; ES = the picture's tail
    mov di, PXH_BINS
    mov cx, 512
    xor ax, ax
    cld
    rep stosw
    mov si, px_pal
    xor bx, bx                      ; BX = the index x 4
.f:
    mov ax, [es:PXH_HIST + bx]
    or ax, [es:PXH_HIST + bx + 2]
    jz .fn
    mov al, [px_hchan]
    or al, al
    jnz .chan
    call pl_luma
    jmp short .v
.chan:
    xor ah, ah
    dec ax
    push si
    add si, ax
    mov al, [si]
    pop si
    xor ah, ah
.v:
    mov di, ax
    shl di, 1
    shl di, 1
    mov ax, [es:PXH_HIST + bx]
    add [es:PXH_BINS + di], ax
    mov ax, [es:PXH_HIST + bx + 2]
    adc [es:PXH_BINS + di + 2], ax
.fn:
    add si, 3
    add bx, 4
    cmp bx, 1024
    jb .f
    ; N and S1 (32 bits each: N is at most a master's bytes)
    xor ax, ax
    mov [px_hn], ax
    mov [px_hn + 2], ax
    mov [px_hs1], ax
    mov [px_hs1 + 2], ax
    mov word [px_hmin], 0xFFFF
    mov word [px_hmax], 0
    xor si, si                      ; SI = v
.s1:
    mov di, si
    shl di, 1
    shl di, 1
    mov ax, [es:PXH_BINS + di]
    mov dx, [es:PXH_BINS + di + 2]
    mov cx, ax
    or cx, dx
    jz .s1n
    cmp word [px_hmin], 0xFFFF
    jne .mx
    mov [px_hmin], si
.mx:
    mov [px_hmax], si
    add [px_hn], ax
    adc [px_hn + 2], dx
    mov cx, si
    call pl_mulx                    ; v x n
    add [px_hs1], ax
    adc [px_hs1 + 2], dx
.s1n:
    inc si
    cmp si, 256
    jb .s1
    mov ax, [px_hn]
    or ax, [px_hn + 2]
    jnz .some
    mov word [px_hmin], 0
    xor ax, ax
    mov [px_hmean], ax
    mov [px_hsd], ax
    jmp .out
.some:
    mov ax, [px_hs1]                ; mean = S1 / N
    mov [px_n0], ax
    mov ax, [px_hs1 + 2]
    mov [px_n1], ax
    mov word [px_n2], 0
    mov ax, [px_hn]
    mov [px_dv0], ax
    mov ax, [px_hn + 2]
    mov [px_dv1], ax
    call pl_div
    mov ax, [px_n0]
    mov [px_hmean], ax
    xor ax, ax                      ; S2 = sum n (v - mean)^2, 48 bits
    mov [px_n0], ax
    mov [px_n1], ax
    mov [px_n2], ax
    xor si, si
.s2:
    mov di, si
    shl di, 1
    shl di, 1
    mov ax, [es:PXH_BINS + di]
    mov dx, [es:PXH_BINS + di + 2]
    mov cx, ax
    or cx, dx
    jz .s2n
    mov cx, si
    sub cx, [px_hmean]
    jns .p
    neg cx
.p:
    push ax
    mov ax, cx
    mul ax
    mov cx, ax                      ; CX = (v - mean)^2, <= 65025
    pop ax
    call pl_mulx                    ; BX:DX:AX = n x it
    add [px_n0], ax
    adc [px_n1], dx
    adc [px_n2], bx
.s2n:
    inc si
    cmp si, 256
    jb .s2
    mov ax, [px_hn]
    mov [px_dv0], ax
    mov ax, [px_hn + 2]
    mov [px_dv1], ax
    call pl_div                     ; the variance
    mov ax, [px_n0]
    call pl_isqrt
    mov [px_hsd], ax
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; pl_htail - ES = the shown master's tail (pxmaster.inc's px_htail).
; Preserves all but ES
pl_htail:
    push ax
    push cx
    push dx
    mov ax, [px_cur + PXR_MW]
    mul word [px_cur + PXR_MH]
    add ax, 15
    adc dx, 0
    mov cx, 4
.p:
    shr dx, 1
    rcr ax, 1
    loop .p
    add ax, 7                       ; ...up to a multiple of 8 (the layout
    PX_ADC                          ; KERN-MEGA-PLAN 14.40 made) - a SEG_SUB
    and al, 0xF8                    ; there (pxrec.inc's PX_TAILAT)
    PX_TAILAT [px_cur + PXR_MSEG]
    mov es, dx
    pop dx
    pop cx
    pop ax
    ret
    nop                             ; PARITY PAD, never executed (MC7's
                                    ; odd count above: the routines below
                                    ; keep their measured even shift)

; pl_luma - AX = (77 R + 150 G + 29 B) >> 8 of DS:SI. Preserves all but AX
pl_luma:
    push bx
    push dx
    mov al, [si]
    mov bl, 77
    mul bl
    mov bx, ax
    mov al, [si + 1]
    mov dl, 150
    mul dl
    add bx, ax
    mov al, [si + 2]
    mov dl, 29
    mul dl
    add ax, bx
    mov al, ah
    xor ah, ah
    pop dx
    pop bx
    ret

; pl_mulx - DX:AX x CX: BX:DX:AX (pxmaster.inc's px_mulx)
pl_mulx:
    push si
    mov si, dx
    mul cx
    push ax
    mov bx, dx
    mov ax, si
    mul cx
    add ax, bx
    adc dx, 0
    mov bx, dx
    mov dx, ax
    pop ax
    pop si
    ret

; pl_div - [px_n2:n1:n0] / [px_dv1:dv0], 48 by 32 bits: the quotient in
; place (pxmaster.inc's px_div). Preserves all but AX, DX
pl_div:
    push cx
    xor ax, ax
    xor dx, dx
    mov cx, 48
.l:
    shl word [px_n0], 1
    rcl word [px_n1], 1
    rcl word [px_n2], 1
    rcl ax, 1
    rcl dx, 1
    jc .sub
    cmp dx, [px_dv1]
    jb .no
    ja .sub
    cmp ax, [px_dv0]
    jb .no
.sub:
    sub ax, [px_dv0]
    sbb dx, [px_dv1]
    or word [px_n0], 1
.no:
    loop .l
    pop cx
    ret

; pl_isqrt - AX = floor(sqrt(AX)), by the bit method. Preserves all but AX
pl_isqrt:
    push bx
    push cx
    push dx
    mov cx, ax
    xor ax, ax
    mov bx, 0x4000
.l:
    or bx, bx
    jz .out
    mov dx, ax
    add dx, bx
    cmp cx, dx
    jb .no
    sub cx, dx
    shr ax, 1
    add ax, bx
    jmp short .n
.no:
    shr ax, 1
.n:
    shr bx, 1
    shr bx, 1
    jmp short .l
.out:
    pop dx
    pop cx
    pop bx
    ret


; =============================================================================
; pl_hcount - ON THE WORKER, after a decode: the master's indices counted
; into its tail's 256 dwords - the resident's px_hcount until wave 8
; (106.25). A pixel is ONE `inc byte`: three byte planes of 256 at PXH_BINS
; (low, middle, high; a low byte that wraps carries on - a master is under
; 2^24 pixels), the dwords put together at the end. The liveness and the
; cancel every 8 rows. DS = the package. CF = 1 AX = PXD_ABORT; else CF = 0
; =============================================================================
PL_MID      equ 256
PL_HI       equ 512
pl_hcount:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call pl_htail                   ; the planes, in the master's tail
    mov di, PXH_BINS
    mov cx, 768 / 2
    xor ax, ax
    cld
    rep stosw
    xor dx, dx                      ; DX = the row
.row:
    cmp dx, [px_cur + PXR_MH]
    jae .sum
    test dl, 7
    jnz .r
    cmp byte [px_abort], 0
    jne .ab
    push bx
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE           ; (where a real-mode move happens...)
    pop bx
    call pl_htail                   ; ...so the tail is asked again here
.r:
    push dx
    mov ax, dx                      ; the row: (y w) of the master, through
    mul word [px_cur + PXR_MW]      ; its 32KB windows (pxrec.inc, MC7) -
    mov di, ax                      ; current for the job (px_wjob, or the
    and di, 0x7FFF                  ; UI's px_pcall), a real-mode move
    shl ax, 1                       ; re-basing them (px_mreloc)
    rcl dx, 1
    mov si, dx
    shl si, 1
    mov si, [px_mwc + MWT_WIN + si] ; SI:DI = the row
    pop dx
    mov cx, [px_cur + PXR_MW]
    push ds
    mov ds, si
    mov si, di                      ; DS:SI the row, BH = 0 throughout
    xor bx, bx
    cld
.px:
    lodsb
    mov bl, al
    inc byte [es:PXH_BINS + bx]
    jz .carry
.n:
    loop .px
    pop ds
    inc dx
    jmp short .row
.carry:
    inc byte [es:PXH_BINS + PL_MID + bx]
    jnz .n
    inc byte [es:PXH_BINS + PL_HI + bx]
    jmp short .n
.sum:
    push ds
    call pl_htail
    push es
    pop ds                          ; DS = ES = the tail
    xor bx, bx                      ; the dwords: lo + 256 mid + 65536 hi
    mov di, PXH_HIST
.s:
    mov al, [PXH_BINS + bx]
    mov ah, [PXH_BINS + PL_MID + bx]
    stosw
    mov al, [PXH_BINS + PL_HI + bx]
    xor ah, ah
    stosw
    inc bx
    cmp bx, 256
    jb .s
    pop ds
    clc
    jmp short .out
.ab:
    mov ax, PXD_ABORT
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
