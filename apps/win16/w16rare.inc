; =============================================================================
; os8088 - apps/win16/w16rare.inc
;
; PART 11, THE RARE PART (SPEC.md 124.25.16): the home of an import few
; programs call, so that part 9 (fetched whole by 42 of the 85 programs
; measured) and the library part (819 bytes under 64 KB on the kern_mega arm)
; stop growing. Part 10's shape: a handler here is W16RARE, near, ending in
; ret; the dispatch fetches the part at the first call and far-calls w16r_gate
; with [w16_xfn]. From here the image is ICALL and the library part SLCALL.
; Arc and Pie came from part 9 (w16rgn.inc); TrackPopupMenu is new.
; =============================================================================

section .w16rare follows=.w16tsk vstart=0 align=1
%if ($ - $$) != 0
  %error "part 11 does not start with its header: include it after this file"
%endif
w16r_base:
    jmp short w16r_gate             ; +0  tools/os88ovl.py: a short jump ...
    db 'W16RAREP'                   ; +2  ...before the cut mark
w16vr_myds: dw 0                    ; +10 the arm's WIN16 data selector
; w16r_gate - the part's one way in: the dispatch far-calls it with [w16_xfn]
; naming a near routine here. CS = this part, DS = WIN16's image; every
; register and the flags pass both ways
w16r_gate:
    call [w16_xfn]
    retf
%if w16r_gate - w16r_base != W16S_GATE || w16vr_myds - w16r_base != W16S_MYDS
  %error "w16r_gate or w16vr_myds is not where W16S_GATE and W16S_MYDS say"
%endif
    W16SELF r

; -----------------------------------------------------------------------------
; Arc (GDI.23) and Pie (GDI.26) - args: +0 yEnd, +2 xEnd, +4 yStart, +6 xStart,
; +8 bottom, +10 right, +12 top, +14 left, +16 hDC. A circle of w16a_n = 64
; steps scaled to the box (a vertex is (cx2 + rx2 * cos) / 2, in half pixels
; so an even box has a centre between pixels), the two rays' exact ends and the
; vertices between them, counter-clockwise: Polyline's points for Arc and, with
; the centre first, Polygon's for Pie. The arguments are the caller's own to
; overwrite: Polyline's/Polygon's {count, points, hDC} are written over them.
; Parameters live in w16_gbuf past Polygon's crossings (192 bytes) and the
; vertices after them.
; -----------------------------------------------------------------------------
W16A_P      equ w16_gbuf + 192
w16a_cx2    equ W16A_P + 0          ; L + R - 1, T + B - 1, R - L - 1, B - T - 1
w16a_cy2    equ W16A_P + 2
w16a_rx2    equ W16A_P + 4
w16a_ry2    equ W16A_P + 6
w16a_psx    equ W16A_P + 8          ; the start and end points on the curve
w16a_psy    equ W16A_P + 10
w16a_pex    equ W16A_P + 12
w16a_pey    equ W16A_P + 14
w16a_sx     equ W16A_P + 16         ; the start ray's direction
w16a_sy     equ W16A_P + 18
w16a_ex     equ W16A_P + 20
w16a_ey     equ W16A_P + 22
w16a_is     equ W16A_P + 24         ; the start's vertex index
w16a_k      equ W16A_P + 26
w16a_cnt    equ W16A_P + 28
w16a_vx     equ W16A_P + 30
w16a_nl     equ W16A_P + 32         ; Sx^2 + Sy^2
w16a_nh     equ W16A_P + 34
w16a_pie    equ W16A_P + 36
W16A_VTX    equ w16_gbuf + 256

W16RARE w16g_arc
    mov byte [w16a_pie], 0
    jmp short w16a_do
W16RARE w16g_pie
    mov byte [w16a_pie], 1
w16a_do:
    push si
    push di
    push bp
    ICALL w16_args
    mov si, bx
    mov ax, [es:si + 14]            ; left, right
    mov dx, [es:si + 10]
    mov cx, dx
    add cx, ax
    dec cx
    mov [w16a_cx2], cx
    sub dx, ax
    dec dx
    jle .degen
    mov [w16a_rx2], dx
    mov ax, [es:si + 12]            ; top, bottom
    mov dx, [es:si + 8]
    mov cx, dx
    add cx, ax
    dec cx
    mov [w16a_cy2], cx
    sub dx, ax
    dec dx
    jle .degen
    mov [w16a_ry2], dx
    mov ax, [es:si + 6]             ; the start ray
    mov dx, [es:si + 4]
    call w16a_ray
    mov [w16a_psx], ax
    mov [w16a_psy], dx
    mov [w16a_sx], cx
    mov [w16a_sy], bx
    call w16a_idx
    mov [w16a_is], ax
    mov ax, [es:si + 2]             ; the end ray
    mov dx, [es:si]
    call w16a_ray
    mov [w16a_pex], ax
    mov [w16a_pey], dx
    mov [w16a_ex], cx
    mov [w16a_ey], bx
    call w16a_idx
    sub ax, [w16a_is]
    and ax, 63
    mov cx, ax                      ; CX = the vertices between the ends
    jnz .build
    mov ax, [w16a_sx]               ; the same bucket: cross(S, E) > 0 means
    imul word [w16a_ey]             ;   E is just ahead of S - no vertex -
    mov bx, ax                      ;   and anything else is round the whole
    mov cx, dx                      ;   circle
    mov ax, [w16a_sy]
    imul word [w16a_ex]
    sub bx, ax
    sbb cx, dx
    js .whole
    or cx, bx
    jnz .build0
.whole:
    mov cx, 64
    jmp short .build
.build0:
    xor cx, cx
.build:
    mov di, W16A_VTX
    xor bp, bp                      ; BP = the vertex count
    cmp byte [w16a_pie], 0
    je .first
    mov ax, [w16a_cx2]              ; Pie: the centre first
    sar ax, 1
    mov [di], ax
    mov ax, [w16a_cy2]
    sar ax, 1
    mov [di + 2], ax
    add di, 4
    inc bp
.first:
    mov ax, [w16a_psx]
    mov [di], ax
    mov ax, [w16a_psy]
    mov [di + 2], ax
    add di, 4
    inc bp
    mov word [w16a_k], 1
    jcxz .last
.vl:
    mov ax, [w16a_is]
    add ax, [w16a_k]
    and ax, 63
    call w16a_cs                    ; AX = cos, DX = sin (14 bits)
    push dx
    imul word [w16a_rx2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    add dx, [w16a_cx2]
    sar dx, 1
    mov [di], dx
    pop ax
    imul word [w16a_ry2]
    shl ax, 1
    rcl dx, 1
    shl ax, 1
    rcl dx, 1
    mov ax, [w16a_cy2]
    sub ax, dx
    sar ax, 1
    mov [di + 2], ax
    add di, 4
    inc bp
    inc word [w16a_k]
    loop .vl
.last:
    mov ax, [w16a_pex]
    mov [di], ax
    mov ax, [w16a_pey]
    mov [di + 2], ax
    inc bp
    mov ax, [es:si + 16]            ; the arguments become Polyline's
    mov [es:si + 6], ax
    mov [es:si], bp
    mov word [es:si + 2], W16A_VTX
    mov [es:si + 4], ds
    cmp byte [w16a_pie], 0
    jne .pie
    SLCALL w16g_polyline
    jmp short .out
.pie:
    SLCALL w16g_polygon
    jmp short .out
.degen:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; w16a_cs - AX = a step of 64 round the circle -> AX = cos, DX = sin, scaled
; to 16384. Keeps the rest.
w16a_cs:
    push bx
    push cx
    mov bx, ax
    and bx, 15
    shl bx, 1
    mov dx, [cs:w16a_tab + bx]      ; DX = S, the sine of the step in its
    neg bx                          ;   quadrant
    push ax
    mov ax, [cs:w16a_tab + 32 + bx] ; AX = C, its cosine
    pop bx
    mov cl, 4
    shr bx, cl
    and bl, 3                       ; BL = the quadrant
    jz .out
    cmp bl, 2
    jb .q1
    je .q2
    xchg ax, dx                     ; q3: cos = S, sin = -C
    neg dx
    jmp short .out
.q2:
    neg ax                          ; q2: cos = -C, sin = -S
    neg dx
    jmp short .out
.q1:
    xchg ax, dx                     ; q1: cos = -S, sin = C
    neg ax
.out:
    pop cx
    pop bx
    ret

; w16a_tab - sin (j * 90 / 16 degrees) * 16384, j = 0 to 16
w16a_tab:
    dw 0, 1606, 3196, 4756, 6270, 7723, 9102, 10394, 11585
    dw 12665, 13623, 14449, 15137, 15679, 16069, 16305, 16384

; w16a_fit - DX:AX fits a signed word other than -32768: ZF=1. Clobbers BP.
; -32768 is refused because w16a_idx's NEG cannot turn it over (a quarter
; arc from 180 degrees drew three quarters and a chord), and because two of
; them make w16a_ray's square 2^31, whose root's first DIV overflows
; (Arc(0, 0, 257, 257, 64, 192, ...) was a divide error)
w16a_fit:
    push dx
    push ax
    cwd
    mov bp, dx
    pop ax
    pop dx
    cmp dx, bp
    jne .r
    mov bp, ax
    neg bp                          ; OF=1 for 8000h alone, ZF=0 with it
    jo .r
    cmp ax, ax
.r:
    ret

; w16a_ray - AX, DX = a point -> AX, DX = where the ray from the box's centre
; through it meets the curve; CX, BX = the ray's direction in the circle's
; frame, scaled to fit a word. Clobbers BP, DI. Keeps SI.
w16a_ray:
    shl ax, 1
    sub ax, [w16a_cx2]              ; AX = 2 x - cx2
    shl dx, 1
    sub dx, [w16a_cy2]              ; DX = 2 y - cy2 (down)
    mov cx, dx
    imul word [w16a_ry2]            ; DX:AX = Sx = dx * ry2
    mov bx, dx
    xchg cx, ax                     ; BX:CX = Sx, AX = dy
    neg ax
    imul word [w16a_rx2]            ; DX:AX = Sy = -dy * rx2
.red:
    call w16a_fit
    jne .sh
    xchg ax, cx
    xchg dx, bx
    call w16a_fit
    xchg ax, cx
    xchg dx, bx
    je .fits
.sh:
    sar dx, 1
    rcr ax, 1
    sar bx, 1
    rcr cx, 1
    jmp short .red
.fits:
    mov bx, ax                      ; BX = Sy, CX = Sx
    mov ax, cx
    or ax, bx
    jnz .nz
    mov cx, 1
.nz:
    mov ax, cx
    imul cx
    push dx
    push ax
    mov ax, bx
    imul bx
    pop bp
    add ax, bp
    pop bp
    adc dx, bp
    mov [w16a_nl], ax               ; the square of its length
    mov [w16a_nh], dx
    push cx
    push bx
    mov ax, cx                      ; DI = the larger of |Sx|, |Sy|, which
    cwd                             ;   Newton's square root starts from
    xor ax, dx
    sub ax, dx
    mov di, ax
    mov ax, bx
    cwd
    xor ax, dx
    sub ax, dx
    cmp ax, di
    jbe .nw0
    mov di, ax
.nw0:
    mov cx, 4
.nw:
    mov dx, [w16a_nh]
    mov ax, [w16a_nl]
    div di
    add ax, di
    rcr ax, 1
    mov di, ax                      ; DI = |S|, within a part in 2^20
    loop .nw
    pop bx
    pop cx
    mov ax, cx                      ; x = (cx2 + rx2 * Sx / |S|) / 2
    cwd
    xor ax, dx
    sub ax, dx
    mul word [w16a_rx2]
    div di
    or cx, cx
    jns .px
    neg ax
.px:
    add ax, [w16a_cx2]
    sar ax, 1
    push ax
    mov ax, bx                      ; y = (cy2 - ry2 * Sy / |S|) / 2
    cwd
    xor ax, dx
    sub ax, dx
    mul word [w16a_ry2]
    div di
    or bx, bx
    jns .py
    neg ax
.py:
    mov dx, [w16a_cy2]
    sub dx, ax
    sar dx, 1
    pop ax
    ret

; w16a_idx - CX, BX = a direction -> AX = the last vertex (of 64, from angle 0
; counter-clockwise) at or before it. A direction in the lower half is turned
; over and counted from vertex 32.
w16a_idx:
    xor dx, dx
    or bx, bx
    jg .up
    jl .flip
    or cx, cx
    jg .up
.flip:
    neg cx
    neg bx
    mov dx, 32
.up:
    push dx
    mov [w16a_vx], cx
    mov [w16a_k], bx                ; (the direction's y; w16a_k is free here)
    mov word [w16a_cnt], 0
    xor di, di
.l:
    mov ax, di
    call w16a_cs
    mov bp, dx
    imul word [w16a_k]              ; Ux * Vy
    mov bx, ax
    mov cx, dx
    mov ax, bp
    imul word [w16a_vx]             ; Uy * Vx
    sub bx, ax
    sbb cx, dx
    js .no
    inc word [w16a_cnt]
.no:
    inc di
    cmp di, 32
    jb .l
    mov ax, [w16a_cnt]
    dec ax
    pop dx
    add ax, dx
    ret

; -----------------------------------------------------------------------------
; TrackPopupMenu (USER.416) - args: +0 lpRect, +4 hwnd, +6 nReserved, +8 y,
; +10 x, +12 fuFlags, +14 hMenu (SPEC.md 124.25.16). §124.11's loop with no bar
; under it: level 0 is the popup itself (w16_mhbar 0, w16_mkbd 2), WM_INITMENUPOPUP
; is sent, and the program waits in the call - the loop's own w16_mhpost
; resumes it with TRUE (library part) when the menu ends. FALSE, nothing
; shown, for a handle that is no menu, a window that is none, or a loop that
; already runs. w16_mkch holds the flags here (the bar's use of it is the
; keyboard's, which this loop never has until the first key overwrites it).
; -----------------------------------------------------------------------------
W16RARE w16u_trackpop
    push si
    push di
    xor ax, ax
    cmp byte [w16_mtrk], 0
    jne .out
    ICALL w16_args
    mov si, bx
    mov ax, [es:si + 4]
    mov [w16_mhwnd], ax
    mov ax, [es:si + 14]
    mov [w16_mnmenu], ax
    mov ax, [es:si + 10]
    mov [w16_mnx], ax
    mov ax, [es:si + 8]
    mov [w16_mny], ax
    mov al, [es:si + 12]
    mov [w16_mkch], al
    mov ax, [w16_mhwnd]
    ICALL w16_hwnd2rec              ; CF=1 it is no window
    jc .no
    mov ax, [w16_mnmenu]
    SLCALL w16_mget    ; ES:SI = the menu, CF=1 it is none
    jc .no
    test byte [w16_mkch], 0x0C      ; TPM_CENTERALIGN | TPM_RIGHTALIGN: the
    jz .placed                      ;   popup's width, laid out now, moves x
    SLCALL w16_mlaypop
    mov ax, [es:si + MH_W]
    add ax, 2                       ; the frame's width
    test byte [w16_mkch], 8
    jnz .right
    shr ax, 1
.right:
    sub [w16_mnx], ax
.placed:
    mov cx, [w16_mnx]               ; the screen point -> the content's
    mov dx, [w16_mny]
    SLCALL w16_mrel
    mov [w16_mnx], cx
    mov [w16_mny], dx
    mov byte [w16_mtrk], 1          ; w16_mtbegin's state, with no bar
    mov byte [w16_mkbd], 2
    mov byte [w16_mbsel], 0xFF
    mov byte [w16_mlev], 0
    mov byte [w16_mstale], 0
    xor ax, ax
    mov [w16_mhbar], ax
    mov [w16_mnpos], ax
    mov ax, [w16_idx]               ; the frame the loop waits in
    mov [w16_midx], ax
    mov ax, [w16_nargb]
    mov [w16_mnargb], ax
    mov ax, [w16_psp_]
    mov [w16_mpsp], ax
    xor ax, ax                      ; level 0, no item selected
    SLCALL w16_msendpop
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

section .text
