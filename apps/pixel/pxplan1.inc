; =============================================================================
; os8088 - apps/pixel/pxplan1.inc
;
; ONE PLAN (SPEC.md 106.11): the search for one colour's mixing plan, its
; tables and its scratch, out of pxplan.inc so that a second host - the
; Browser's image part (SPEC.md 71.26) - assembles the same source. The
; includer defines PXP_SELF: a word, read through CS, holding the segment
; this file's scratch (pl_scr) is written through.
; =============================================================================

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
    push es
    mov es, [cs:PXP_SELF]           ; ES = this part's own data: the PS_*
                                    ; scratch below is written through it,
                                    ; never through CS (pxpart.inc rule 6)
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
    mov [es:di], ax
    mov [es:di + 2], dx
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
    mov word [es:PS_BE], 0xFFFF
    mov word [es:PS_BE + 2], 0xFFFF
    push si
    xor si, si                      ; SI = the candidate x 3
    xor cx, cx                      ; CL = the candidate
    xor bx, bx                      ; (BH stays 0)
.c1:
    mov bl, [cs:pl_egal4 + si]         ; its levels, as dword offsets
    mov ax, [es:PS_WSQ + bx]
    mov dx, [es:PS_WSQ + bx + 2]
    mov bl, [cs:pl_egal4 + si + 1]
    add ax, [es:PS_WSQ + 16 + bx]
    adc dx, [es:PS_WSQ + 16 + bx + 2]
    mov bl, [cs:pl_egal4 + si + 2]
    add ax, [es:PS_WSQ + 32 + bx]
    adc dx, [es:PS_WSQ + 32 + bx + 2]
    cmp dx, [es:PS_BE + 2]             ; strictly less: the lower index wins a tie
    ja .c1n
    jb .c1y
    cmp ax, [es:PS_BE]
    jae .c1n
.c1y:
    mov [es:PS_BE], ax
    mov [es:PS_BE + 2], dx
    mov [es:PS_C1], cl
.c1n:
    add si, 3
    inc cl
    cmp cl, 16
    jb .c1
    pop si
    ; --- E0 = 256 D(c1), the pure colour; dd, 64 dd and w dd k ----------------
    mov ax, [es:PS_BE]
    mov dx, [es:PS_BE + 2]
    mov dh, dl                      ; x 256
    mov dl, ah
    mov ah, al
    xor al, al
    mov [es:PS_BE], ax
    mov [es:PS_BE + 2], dx
    mov al, [es:PS_C1]
    mov [es:PS_BC], al
    mov byte [es:PS_BT], 0
    mov byte [es:PS_CAND], 0
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, ax                      ; BX = c1 x 3: its levels, to PS_L1
    mov ax, [cs:pl_egalev + bx]
    mov [es:PS_L1], ax
    mov al, [cs:pl_egalev + bx + 2]
    mov [es:PS_L1 + 2], al
    xor bx, bx                      ; BX = the channel
    mov di, PS_WKD
.dd:
    mov al, [es:PS_L1 + bx]
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
    mov [es:PS_D64 + bx], ax           ; 64 dd
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
    mov word [es:di + 6], 0            ; k = 0
    mov [es:di + 8], ax                ; 1
    mov cx, ax
    add cx, ax
    mov [es:di + 10], cx               ; 2
    add cx, ax
    mov [es:di + 12], cx               ; 3
    neg ax
    mov [es:di + 4], ax                ; -1
    mov cx, ax
    add cx, ax
    mov [es:di + 2], cx                ; -2
    add cx, ax
    mov [es:di], cx                    ; -3
    add di, 14
    inc bx
    cmp bx, 3
    jb .dd
    ; --- the pairs, by K ---------------------------------------------------------
    mov byte [es:PS_J], 0
.pair:
    mov al, [es:PS_C1]
    mov ah, 15
    mul ah
    add al, [es:PS_J]
    adc ah, 0
    mov bx, ax
    mov si, ax                      ; the pair's k indices: 3 a pair
    shl si, 1
    add si, ax
    mov [es:PS_PI], si
    mov al, [cs:px_pordk + bx]
    mov [es:PS_K], al
    mov dl, [cs:px_pord + bx]          ; DL = c2
    mov bl, al                      ; the penalty: past the best, and every
    xor bh, bh                      ; later K is as far
    shl bx, 1
    shl bx, 1
    mov ax, [cs:px_pk578 + bx]
    mov cx, [cs:px_pk578 + bx + 2]
    cmp cx, [es:PS_BE + 2]
    jb .p1
    ja .p0
    cmp ax, [es:PS_BE]
    jbe .p1
.p0:
    jmp .done
.p1:
    mov [es:PS_E], ax
    mov [es:PS_E + 2], cx
    mov si, [es:PS_PI]                 ; S = sum w dd k, through the pair's k
    mov bl, [cs:px_pki + si]           ; indices (px_pki: (k + 3) x 2 each)
    xor bh, bh
    mov cx, [es:PS_WKD + bx]
    mov bl, [cs:px_pki + si + 1]
    add cx, [es:PS_WKD + 14 + bx]
    mov bl, [cs:px_pki + si + 2]
    add cx, [es:PS_WKD + 28 + bx]
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
    mov bl, [es:PS_K]
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
    mov word [es:PS_KX + 6], 0
    mov [es:PS_KX + 8], ax
    mov cx, ax
    add cx, ax
    mov [es:PS_KX + 10], cx
    add cx, ax
    mov [es:PS_KX + 12], cx
    neg ax
    mov [es:PS_KX + 4], ax
    mov cx, ax
    add cx, ax
    mov [es:PS_KX + 2], cx
    add cx, ax
    mov [es:PS_KX], cx
    pop cx                          ; CL = t
    ; --- E = the penalty + w r'^2, G, R, B, dropped once past the best ------
%macro PXP_TERM 2                   ; channel, weight
    mov si, [es:PS_PI]
    mov bl, [cs:px_pki + si + %1]
    xor bh, bh
    mov ax, [es:PS_D64 + 2 * %1]
    sub ax, [es:PS_KX + bx]            ; r = 64 dd - 85 k t
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
    add ax, [es:PS_E]
    adc dx, [es:PS_E + 2]
    mov [es:PS_E], ax
    mov [es:PS_E + 2], dx
    cmp dx, [es:PS_BE + 2]
    jb %%in
    ja %%out
    cmp ax, [es:PS_BE]
    jbe %%in
%%out:
    jmp .next
%%in:
%endmacro
    PXP_TERM 1, 6
    PXP_TERM 0, 3
    PXP_TERM 2, 1
    ; DX:AX = E <= the best: better, or a tie the lower c2 wins
    mov bx, [es:PS_J]                  ; BL = the pair's place in the order
    xor bh, bh
    mov al, [es:PS_C1]
    mov ah, 15
    mul ah
    add bx, ax
    mov bl, [cs:px_pord + bx]          ; BL = c2
    mov ax, [es:PS_E]
    cmp dx, [es:PS_BE + 2]
    jb .take
    cmp ax, [es:PS_BE]
    jb .take
    cmp byte [es:PS_CAND], 0           ; equal: only a pair ties a pair
    je .next
    cmp bl, [es:PS_BC]
    jae .next
.take:
    mov [es:PS_BE], ax
    mov [es:PS_BE + 2], dx
    mov [es:PS_BC], bl
    mov [es:PS_BT], cl
    mov byte [es:PS_CAND], 1
.next:
    inc byte [es:PS_J]
    cmp byte [es:PS_J], 15
    jae .done
    jmp .pair
.done:
    mov al, [es:PS_C1]
    mov ah, [es:PS_BC]
    mov dl, [es:PS_BT]
    pop es
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
