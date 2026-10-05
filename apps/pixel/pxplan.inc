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
; scratch and tables are the part's, through CS (apps/pixel/pxpart.inc's
; rule 1). tools/pixelsim.py's plan_fast is the search, check for check.
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
    push cs
    pop ds
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

; pl_plan1 - the plan for the colour at DS:SI (R, G, B): AL = c1, AH = c2,
; DL = t. pixelsim's plan_fast, which `--selfcheck` holds equal to plan_for
; over a grid of colours: the same sums, reached with less work, because a
; PAL palette is 256 of these on the worker before its first row
; (SPEC.md 106.11):
;   - c1, the nearest, from twelve weighted squares a target (each channel's
;     distance to the four levels the sixteen are made of) and three lookups
;     a candidate;
;   - each c2 in order of K (px_pord), whose penalty 57,800 K is a floor
;     under its error, so the first K whose penalty exceeds the best ends
;     the search - about four pairs reach a division, not fifteen;
;   - a pair's error summed G, R, B and dropped as soon as it exceeds the
;     best; a tie goes to the lower c2, plan_for's strict < in index order.
; Preserves all but AX, DX
PS_WSQ equ pl_scr + 0         ; 12 dwords: channel x 16 + level x 4
PS_WKD equ pl_scr + 48        ; 21 words: channel x 14 + (k + 3) x 2 -
                                    ; w dd k, k = -3..3
PS_D64 equ pl_scr + 90        ; 3 words: 64 dd
PS_KX equ pl_scr + 100       ; 7 words: k x 85 t, k = -3..3
PS_BE equ pl_scr + 114       ; dword: the best error
PS_E equ pl_scr + 118       ; dword: this pair's
PS_C1 equ pl_scr + 122       ; byte: c1
PS_BC equ pl_scr + 123       ; byte: the best c2
PS_BT equ pl_scr + 124       ; byte: its t
PS_CAND equ pl_scr + 125       ; byte: the best is a pair, not c1 alone
PS_J equ pl_scr + 126       ; byte: the pair being tried
PS_K equ pl_scr + 127       ; byte: its K
PS_L1 equ pl_scr + 128       ; 3 bytes: c1's levels
PS_PI equ pl_scr + 132       ; word: the pair's px_pki offset
pl_plan1:
    push bx
    push cx
    push si
    push di
    push bp
    ; --- twelve weighted squares: w (T - 85 l)^2 ------------------------------
    xor bx, bx                      ; BX = the channel
    mov di, PS_WSQ
.sq:
    mov al, [si + bx]
    xor ah, ah
    mov cx, ax                      ; CX = T - 85 l
.sq1:
    mov ax, cx
    or ax, ax
    jns .sq2
    neg ax
.sq2:
    mul al                          ; AX = (T - 85 l)^2, |.| <= 255
    xor dx, dx
    cmp bl, 2
    je .w1                          ; B: x 1
    mov dx, ax                      ; R: x 3, G: x 6
    xor bp, bp                      ; (BP borrowed: the high word)
    shl ax, 1
    rcl bp, 1
    add ax, dx
    adc bp, 0
    cmp bl, 1
    jne .w3
    shl ax, 1
    rcl bp, 1
.w3:
    mov dx, bp
.w1:
    mov [cs:di], ax
    mov [cs:di + 2], dx
    add di, 4
    sub cx, 85
    mov ax, di                      ; four a channel: (DI - PS_WSQ) & 15
    sub ax, PS_WSQ
    test al, 15
    jnz .sq1
    inc bx
    cmp bx, 3
    jb .sq
    ; --- c1: the nearest of the sixteen, three lookups each ------------------
    mov word [cs:PS_BE], 0xFFFF
    mov word [cs:PS_BE + 2], 0xFFFF
    push si
    xor si, si                      ; SI = the candidate x 3
    xor cx, cx                      ; CL = the candidate
    xor bx, bx                      ; (BH stays 0)
.c1:
    mov bl, [cs:pl_egal4 + si]         ; its levels, as dword offsets
    mov ax, [cs:PS_WSQ + bx]
    mov dx, [cs:PS_WSQ + bx + 2]
    mov bl, [cs:pl_egal4 + si + 1]
    add ax, [cs:PS_WSQ + 16 + bx]
    adc dx, [cs:PS_WSQ + 16 + bx + 2]
    mov bl, [cs:pl_egal4 + si + 2]
    add ax, [cs:PS_WSQ + 32 + bx]
    adc dx, [cs:PS_WSQ + 32 + bx + 2]
    cmp dx, [cs:PS_BE + 2]             ; strictly less: the lower index wins a tie
    ja .c1n
    jb .c1y
    cmp ax, [cs:PS_BE]
    jae .c1n
.c1y:
    mov [cs:PS_BE], ax
    mov [cs:PS_BE + 2], dx
    mov [cs:PS_C1], cl
.c1n:
    add si, 3
    inc cl
    cmp cl, 16
    jb .c1
    pop si
    ; --- E0 = 256 D(c1), the pure colour; dd, 64 dd and w dd k ----------------
    mov ax, [cs:PS_BE]
    mov dx, [cs:PS_BE + 2]
    mov dh, dl                      ; x 256
    mov dl, ah
    mov ah, al
    xor al, al
    mov [cs:PS_BE], ax
    mov [cs:PS_BE + 2], dx
    mov al, [cs:PS_C1]
    mov [cs:PS_BC], al
    mov byte [cs:PS_BT], 0
    mov byte [cs:PS_CAND], 0
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, ax                      ; BX = c1 x 3: its levels, to PS_L1
    mov ax, [cs:pl_egalev + bx]
    mov [cs:PS_L1], ax
    mov al, [cs:pl_egalev + bx + 2]
    mov [cs:PS_L1 + 2], al
    xor bx, bx                      ; BX = the channel
    mov di, PS_WKD
.dd:
    mov al, [cs:PS_L1 + bx]
    mov ah, 85
    mul ah
    mov cx, ax
    mov al, [si + bx]
    xor ah, ah
    sub ax, cx                      ; AX = dd
    push bx
    shl bx, 1
    mov cx, ax
    push cx
    mov cl, 6
    shl ax, cl
    pop cx
    mov [cs:PS_D64 + bx], ax           ; 64 dd
    pop bx
    mov ax, cx                      ; w dd
    cmp bl, 2
    je .wd
    shl ax, 1
    add ax, cx                      ; 3 dd
    cmp bl, 1
    jne .wd
    shl ax, 1                       ; 6 dd
.wd:
    mov word [cs:di + 6], 0            ; k = 0
    mov [cs:di + 8], ax                ; 1
    mov cx, ax
    add cx, ax
    mov [cs:di + 10], cx               ; 2
    add cx, ax
    mov [cs:di + 12], cx               ; 3
    neg ax
    mov [cs:di + 4], ax                ; -1
    mov cx, ax
    add cx, ax
    mov [cs:di + 2], cx                ; -2
    add cx, ax
    mov [cs:di], cx                    ; -3
    add di, 14
    inc bx
    cmp bx, 3
    jb .dd
    ; --- the pairs, by K ---------------------------------------------------------
    mov byte [cs:PS_J], 0
.pair:
    mov al, [cs:PS_C1]
    mov ah, 15
    mul ah
    add al, [cs:PS_J]
    adc ah, 0
    mov bx, ax
    mov si, ax                      ; the pair's k indices: 3 a pair
    shl si, 1
    add si, ax
    mov [cs:PS_PI], si
    mov al, [cs:px_pordk + bx]
    mov [cs:PS_K], al
    mov dl, [cs:px_pord + bx]          ; DL = c2
    mov bl, al                      ; the penalty: past the best, and every
    xor bh, bh                      ; later K is as far
    shl bx, 1
    shl bx, 1
    mov ax, [cs:px_pk578 + bx]
    mov cx, [cs:px_pk578 + bx + 2]
    cmp cx, [cs:PS_BE + 2]
    jb .p1
    ja .p0
    cmp ax, [cs:PS_BE]
    jbe .p1
.p0:
    jmp .done
.p1:
    mov [cs:PS_E], ax
    mov [cs:PS_E + 2], cx
    mov si, [cs:PS_PI]                 ; S = sum w dd k, through the pair's k
    mov bl, [cs:px_pki + si]           ; indices (px_pki: (k + 3) x 2 each)
    xor bh, bh
    mov cx, [cs:PS_WKD + bx]
    mov bl, [cs:px_pki + si + 1]
    add cx, [cs:PS_WKD + 14 + bx]
    mov bl, [cs:px_pki + si + 2]
    add cx, [cs:PS_WKD + 28 + bx]
    or cx, cx
    jg .s
    jmp .next                       ; S <= 0: away from the target
.s:
    mov ax, cx                      ; t = (64 S + den / 2) div den, den = 85 K
    xor dx, dx
    mov cx, 6
.s64:
    shl ax, 1
    rcl dx, 1
    loop .s64
    mov bl, [cs:PS_K]
    xor bh, bh
    shl bx, 1
    mov cx, [cs:px_pk85 + bx]
    mov bx, cx
    shr bx, 1
    add ax, bx
    adc dx, 0
    div cx
    cmp ax, 64
    jbe .t1
    mov ax, 64
.t1:
    or ax, ax
    jnz .t2
    jmp .next
.t2:
    push ax
    mov ah, 85                      ; X = 85 t, and k X for k = -3..3
    mul ah
    mov word [cs:PS_KX + 6], 0
    mov [cs:PS_KX + 8], ax
    mov cx, ax
    add cx, ax
    mov [cs:PS_KX + 10], cx
    add cx, ax
    mov [cs:PS_KX + 12], cx
    neg ax
    mov [cs:PS_KX + 4], ax
    mov cx, ax
    add cx, ax
    mov [cs:PS_KX + 2], cx
    add cx, ax
    mov [cs:PS_KX], cx
    pop cx                          ; CL = t
    ; --- E = the penalty + w r'^2, G, R, B, dropped once past the best ------
%macro PXP_TERM 2                   ; channel, weight
    mov si, [cs:PS_PI]
    mov bl, [cs:px_pki + si + %1]
    xor bh, bh
    mov ax, [cs:PS_D64 + 2 * %1]
    sub ax, [cs:PS_KX + bx]            ; r = 64 dd - 85 k t
    sar ax, 1
    sar ax, 1                       ; r' = r >> 2
    imul ax                         ; DX:AX = r'^2
  %if %2 == 3
    mov bx, ax
    mov di, dx
    shl ax, 1
    rcl dx, 1
    add ax, bx
    adc dx, di
  %elif %2 == 6
    mov bx, ax
    mov di, dx
    shl ax, 1
    rcl dx, 1
    add ax, bx
    adc dx, di
    shl ax, 1
    rcl dx, 1
  %endif
    add ax, [cs:PS_E]
    adc dx, [cs:PS_E + 2]
    mov [cs:PS_E], ax
    mov [cs:PS_E + 2], dx
    cmp dx, [cs:PS_BE + 2]
    jb %%in
    ja %%out
    cmp ax, [cs:PS_BE]
    jbe %%in
%%out:
    jmp .next
%%in:
%endmacro
    PXP_TERM 1, 6
    PXP_TERM 0, 3
    PXP_TERM 2, 1
    ; DX:AX = E <= the best: better, or a tie the lower c2 wins
    mov bx, [cs:PS_J]                  ; BL = the pair's place in the order
    xor bh, bh
    mov al, [cs:PS_C1]
    mov ah, 15
    mul ah
    add bx, ax
    mov bl, [cs:px_pord + bx]          ; BL = c2
    mov ax, [cs:PS_E]
    cmp dx, [cs:PS_BE + 2]
    jb .take
    cmp ax, [cs:PS_BE]
    jb .take
    cmp byte [cs:PS_CAND], 0           ; equal: only a pair ties a pair
    je .next
    cmp bl, [cs:PS_BC]
    jae .next
.take:
    mov [cs:PS_BE], ax
    mov [cs:PS_BE + 2], dx
    mov [cs:PS_BC], bl
    mov [cs:PS_BT], cl
    mov byte [cs:PS_CAND], 1
.next:
    inc byte [cs:PS_J]
    cmp byte [cs:PS_J], 15
    jae .done
    jmp .pair
.done:
    mov al, [cs:PS_C1]
    mov ah, [cs:PS_BC]
    mov dl, [cs:PS_BT]
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; the sixteen's levels (each channel is 0, 85, 170 or 255: level 0..3)
pl_egalev:
    db 0,0,0, 0,0,2, 0,2,0, 0,2,2, 2,0,0, 2,0,2, 2,1,0, 2,2,2
    db 1,1,1, 1,1,3, 1,3,1, 1,3,3, 3,1,1, 3,1,3, 3,3,1, 3,3,3
pl_egal4:                           ; ...and x 4, a dword table's offsets
    db 0,0,0, 0,0,8, 0,8,0, 0,8,8, 8,0,0, 8,0,8, 8,4,0, 8,8,8
    db 4,4,4, 4,4,12, 4,12,4, 4,12,12, 12,4,4, 12,4,12, 12,12,4, 12,12,12


; --- the search's scratch (the resident's px_pscr until wave 4), this part's
pl_scr:     times 136 db 0

%include "pxplans.inc"              ; the shipped plans, the search's tables


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
    add ax, [px_cur + PXR_MSEG]
    mov es, ax
    pop dx
    pop cx
    pop ax
    ret

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
    call OSAPI_TASK_ALIVE
    pop bx
.r:
    push dx
    mov ax, dx                      ; the row: (y w) of the master, re-read
    mul word [px_cur + PXR_MW]      ; each row (the master moves)
    mov di, ax
    and di, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_cur + PXR_MSEG]
    mov si, ax                      ; SI:DI = the row
    call pl_htail                   ; ES = the tail
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
