; =============================================================================
; os8088 - apps/pixel/pxview.inc
;
; PiXEL's RENDERER (SPEC.md 106.11): the mixing plans and the 1bpp
; thresholds per palette entry, the 64 Bayer tables they expand into, the
; view (zoom, pixel aspect, pan) and the composer that turns master rows
; into bands for the BLITP, BLIT4 or BLIT1 slots (OSAPI_GFX_*) - and,
; built on the same composer, the Navigator's banked thumbnail and the
; Histogram (SPEC.md 106.12).
;
; tools/pixelsim.py's plan_for, thresh1, View and render are this file in
; Python; tests/pxopen.py compares the canvas's framebuffer with render().
;
; THE VIEW CLAIM (one claim, movable, laid out per depth):
;   0x0000  64 Bayer tables of 256 bytes: table (py * 8 + px), so BH = the
;           cell and BL = 0 makes `es xlatb` the whole lookup
;   [px_vo_c2p]   (4bpp) 256 bytes: a LANE byte -> the packed pair BLIT4
;                 wants, for the fallback (px_rband)
;   [px_vo_row]   (4bpp) one row of lanes (px_crow)
;   [px_vo_band]  a band: 16 rows, planes row-interleaved, or 1bpp
;   [px_vo_thm]   the Navigator's thumbnail, the same layout
; =============================================================================

PX_BANDH    equ 16                  ; rows a band
PX_VOC2P    equ 0x4000              ; the 4bpp layout's unspread table
PX_THMW     equ 160                 ; the thumbnail's largest width...
PX_THMH     equ 96                  ; ...and height (no title strip over the
                                    ; Navigator: SPEC.md 106.15)
PX_ZMIN     equ 256                 ; the smallest zoom (16.16): 1/256
PX_SIDEMAX  equ 32767               ; no zoom makes a side longer than this
PX_HBW      equ 128                 ; the Histogram's band (os88gfx.inc's
PX_HBH      equ 80                  ; PX_HBW/H, defined from these)
PX_HBST     equ PX_HBW / 8

; (a VIEW's layout, VW_*, is apps/pixel/pxrec.inc's: the GIF part paints
; an animation's frame through it, SPEC.md 106.25)

; =============================================================================
; THE CONSTANTS
; =============================================================================
px_bayer:
    db  0, 32,  8, 40,  2, 34, 10, 42
    db 48, 16, 56, 24, 50, 18, 58, 26
    db 12, 44,  4, 36, 14, 46,  6, 38
    db 60, 28, 52, 20, 62, 30, 54, 22
    db  3, 35, 11, 43,  1, 33,  9, 41
    db 51, 19, 59, 27, 49, 17, 57, 25
    db 15, 47,  7, 39, 13, 45,  5, 37
    db 63, 31, 55, 23, 61, 29, 53, 21

; the zoom steps, 16.16 screen pixels a source pixel (SPEC.md 106.11)
px_zsteps:  dd 8192, 10923, 16384, 21845, 32768, 43691, 65536
            dd 131072, 196608, 262144, 393216, 524288
PX_NZ       equ 12

; pixel aspect a = an / ad, by OSAPI_WM_DISPLAY's kind (VGA, Herc, CGA, EGA)
px_aspn:    dw 1, 29, 5, 35
px_aspd:    dw 1, 45, 12, 48

%include "pxqtab.inc"               ; the ordered quantiser's classes (106.8)

; =============================================================================
; THE PLANS (SPEC.md 106.11) are the decoder parts' since wave 4: the
; shipped CUBE and GREY plans and the PAL search are apps/pixel/pxplan.inc,
; shared source of every decoder part, reached through PXV_PLANS by
; px_plans below (SPEC.md 106.20). What stays is the 1bpp threshold.
; =============================================================================

; px_t1for - the 1bpp threshold of the colour at DS:SI: AL = (64 L + 127)
; div 255, L = (Y + (Y^2 + 127) div 255) >> 1, Y the luma. Preserves all
; but AX
px_t1for:
    push bx
    push dx
    call px_luma                    ; AX = Y
    mov bx, ax
    mul ax
    add ax, 127
    adc dx, 0
    push cx
    mov cx, 255
    div cx                          ; AX = (Y^2 + 127) div 255
    add ax, bx
    shr ax, 1                       ; L
    mov dx, 64
    mul dx
    add ax, 127
    adc dx, 0
    div cx
    pop cx
    pop dx
    pop bx
    ret

; px_luma - AX = (77 R + 150 G + 29 B) >> 8 of DS:SI. Preserves all but AX
px_luma:
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

; =============================================================================
; px_tables - THE DISPLAY TABLES for [px_pal] at depth [px_tdepth] (SPEC.md
; 106.11), into the view claim. On the worker (a decode or a restore) or on
; the UI task (a window dragged to a display of another depth). The CUBE's
; and GREY's plans are shipped; a PAL palette's are searched here, which on
; an 8088 is the longest step. out CF = 1 AX = PXD_ABORT on a cancel
; =============================================================================
px_tables:
    cmp byte [px_hmode], 0          ; a HIDDEN decode (SPEC.md 106.21) paints
    je .go                          ; nothing: the tables stay the shown
    clc                             ; picture's
    ret
.go:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [px_tdepth], 1
    jne .four
    ; --- 1bpp: a threshold an entry ------------------------------------------
    mov si, px_pal
    xor bx, bx
.t1:
    call px_t1for
    mov [px_t1 + bx], al
    add si, 3
    inc bx
    cmp bx, 256
    jb .t1
    jmp .expand
.four:
    call px_plans                   ; [px_plan], kept or by the picture's
    jc .ab                          ; decoder part (SPEC.md 106.20)
    mov word [px_planp], px_plan
.expand:
    call px_texpand
    clc
    jmp short .out
.ab:
    cmp ax, PXD_ABORT               ; (a cancel; or no part to ask: the
    je .out                         ; tables wait for px_tabsync)
    mov ax, PXD_DATA
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_plans - [px_plan] := the 4bpp plans of [px_pal] (SPEC.md 106.20): as
; they are when [px_pvalid] says they are this palette's (a restore put
; them back), else made by PXV_PLANS of the picture's decoder part - on the
; worker the part its decode holds (px_wcall: no fetch), on the UI task
; fetched first if it must be. out CF = 1 AX = PXD_ABORT, or why not.
; Preserves all but AX
px_plans:
    cmp byte [px_pvalid], 0
    jne .ok
    push bx
    push di
    push cx
    mov al, [px_cur + PXR_FMT]
    call px_fpart
    mov bl, PXV_PLANS
    xor cl, cl                      ; (the plans; CL = 1 is pl_stats)
    mov di, px_k
    cmp byte [px_onworker], 0
    je .ui
    call px_wcall
    jmp short .r
.ui:
    call px_kneed                   ; one decoder part at a time
    call px_pcall
.r:
    pop cx
    pop di
    pop bx
    jc .out
    mov byte [px_pvalid], 1
.ok:
    clc
.out:
    ret

; px_texpand - the 64 tables from the plans (4bpp) or the thresholds (1bpp):
; table (py * 8 + px), entry i = c2 if bayer(py, px) < t_i else c1, as a
; LANE's half (px_crow, px_c2p): c's bits at 7, 5, 3, 1 in an even column's
; table and at 6, 4, 2, 0 in an odd one's, so a pair of lookups OR'd is a
; byte the composer's butterfly turns into planes. On 1bpp the entry is the
; cell's bit (0x80 >> px) if lit, else 0.
;
; BUILT BY THRESHOLD, NOT BY CELL: walked in Bayer order b = 0..63, entry i
; changes exactly once - from c2 (lit) to c1 (dark) at b = t_i - so one state
; row is kept, the entries whose t is b are switched (a list per t, made
; once), and the cell whose threshold is b is a COPY of the state: 64 block
; moves and 256 switches, where comparing every entry of every table was
; 16,384 of them and a second of an 8088 on every open. The scratch is the
; band's (nothing renders while the tables are not good). Preserves all
PXT_SE      equ 0                   ; the state row, an even column's lanes
PXT_SO      equ 256                 ; ...an odd column's (4bpp)
PXT_NX      equ 512                 ; 256 words: the next entry with its t
PXT_HD      equ 1024                ; 65 words: each t's first entry
PXT_INV     equ 1154                ; 64 bytes: the cell whose threshold is b
px_texpand:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    mov es, [px_vseg]
    mov bp, [px_vo_band]            ; BP = the scratch (in ES)
    cld
    lea di, [bp + PXT_HD]           ; every list empty
    mov cx, 65
    mov ax, 0xFFFF
    rep stosw
    xor bx, bx                      ; the cell of each threshold
.inv:
    mov al, [px_bayer + bx]
    xor ah, ah
    mov di, ax
    mov [es:bp + di + PXT_INV], bl
    inc bx
    cmp bx, 64
    jb .inv
    mov cx, 255                     ; --- the state at b = 0, and the lists --
.init:
    mov di, cx
    cmp byte [px_tdepth], 1
    je .i1
    mov si, cx
    shl si, 1
    add si, cx
    add si, [px_planp]              ; SI = plan i: c1, c2, t
    mov bl, [si + 1]
    xor bh, bh
    mov al, [px_spread + bx]        ; c2, every entry's state below its t
    mov [es:bp + di + PXT_SE], al
    shr al, 1
    mov [es:bp + di + PXT_SO], al
    mov al, [si + 2]
    jmp short .lk
.i1:
    mov byte [es:bp + di + PXT_SE], 0xFF
    mov al, [px_t1 + di]
.lk:
    xor ah, ah                      ; i onto t's list
    mov bx, ax
    shl bx, 1
    add bx, bp
    shl di, 1
    add di, bp
    mov ax, [es:bx + PXT_HD]
    mov [es:di + PXT_NX], ax
    mov [es:bx + PXT_HD], cx
    dec cx
    jns .init
    xor dx, dx                      ; --- DL = b ------------------------------
.b:
    mov bl, dl                      ; every entry whose t is b goes dark
    xor bh, bh
    shl bx, 1
    add bx, bp
    mov ax, [es:bx + PXT_HD]
.sw:
    cmp ax, 0xFFFF
    je .cell
    mov di, ax
    cmp byte [px_tdepth], 1
    je .sw1
    mov si, ax
    shl si, 1
    add si, ax
    add si, [px_planp]
    mov bl, [si]                    ; c1
    xor bh, bh
    mov bl, [px_spread + bx]
    mov [es:bp + di + PXT_SE], bl
    shr bl, 1
    mov [es:bp + di + PXT_SO], bl
    jmp short .swn
.sw1:
    mov byte [es:bp + di + PXT_SE], 0
.swn:
    shl di, 1
    mov ax, [es:bp + di + PXT_NX]
    jmp short .sw
.cell:
    mov bl, dl                      ; the cell whose threshold is b
    xor bh, bh
    mov di, bx
    mov al, [es:bp + di + PXT_INV]
    mov bl, al
    mov ah, al
    xor al, al
    mov di, ax                      ; DI = its table
    lea si, [bp + PXT_SE]
    and bl, 7                       ; BL = px
    push es
    pop ds                          ; (DS = the view claim for the copy)
    cmp byte [cs:px_tdepth], 1
    je .c1
    test bl, 1
    jz .ce
    add si, PXT_SO - PXT_SE
.ce:
    mov cx, 128
    rep movsw
    jmp short .cn
.c1:
    mov cl, bl                      ; the cell's bit, both bytes of a word
    mov bl, 0x80
    shr bl, cl
    mov bh, bl
    mov cx, 128
.c1w:
    lodsw
    and ax, bx
    stosw
    loop .c1w
.cn:
    push cs
    pop ds
    inc dl
    cmp dl, 64
    jb .b
    pop ds
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; a colour's lane half: c's bits 3, 2, 1, 0 at 7, 5, 3, 1 (shifted right once
; for an odd column)
px_spread:
    db 0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A
    db 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA

; =============================================================================
; THE VIEW CLAIM (UI task)
; =============================================================================

; px_viewclaim - the view claim, laid out for the window's depth now; a
; claim made for the other depth is given back first. The plane table is
; built when it is made (it never changes). CF = 1 refused. Preserves all
px_viewclaim:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [px_bpp]
    cmp word [px_vseg], 0
    je .new
    cmp al, [px_vdepth]
    je .ok
    mov dx, [px_vseg]               ; another depth's: let it go
    call OSAPI_MEM_FREE
    mov word [px_vseg], 0
    mov byte [px_tabok], 0
.new:
    mov [px_vdepth], al
    cmp al, 1
    je .one
    mov word [px_vo_c2p], PX_VOC2P  ; 4bpp: tables, the unspread table, a row
    mov word [px_vo_row], 0x4100    ; of lanes, a band, the thumbnail (the 4 KB
    mov word [px_vo_band], 0x4300   ; plane table it had is the butterfly's
    mov word [px_vo_thm], 0x5A00    ; registers now); the thumbnail is LAST in
    mov ax, (0x5A00 + PX_THMW / 2 * PX_THMH + 1023) / 1024    ; the claim: its
                                    ; area is PX_THMW / 2 x PX_THMH at 4bpp
    jmp short .claim
.one:
    mov word [px_vo_band], 0x4000   ; 1bpp: tables, a band, the thumbnail
    mov word [px_vo_thm], 0x4600    ; ...and PX_THMW / 8 x PX_THMH at 1bpp
    mov ax, (0x4600 + PX_THMW / 8 * PX_THMH + 1023) / 1024
.claim:
    call OSAPI_MEM_CLAIM
    jc .out
    mov [px_vseg], dx
    mov ax, px_mreloc
    call OSAPI_MEM_MOVABLE
    mov byte [px_thok], 0
    cmp byte [px_vdepth], 1
    je .ok
    call px_unspread
.ok:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_unspread - the 256-byte table at [px_vo_c2p] that turns a LANE byte -
; two pixels' bits interleaved, the left's at 7, 5, 3, 1 (px_texpand) - into
; the packed pair BLIT4 wants, left << 4 | right: BLITP's fallback, and rare.
; Preserves all
px_unspread:
    push ax
    push bx
    push cx
    push di
    push es
    mov es, [px_vseg]
    mov di, [px_vo_c2p]
    xor bx, bx                      ; BL = the lane byte
    cld
.b:
    xor ax, ax                      ; AH = the pair, built bit by bit
    mov al, bl
    mov cx, 4
.bit:                               ; the lane's bits from the top: the
    shl al, 1                       ; left pixel's into AH, the right's
    rcl ah, 1                       ; into BH, a bit each a round
    shl al, 1
    rcl bh, 1
    loop .bit
    mov al, ah                      ; AL = the left's four bits, BH's low
    mov cl, 4                       ; nibble the right's
    shl al, cl
    and bh, 0x0F
    or al, bh
    stosb
    xor bh, bh
    inc bl
    jnz .b
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; px_tabsync - on the UI task: the tables match the window's depth, or are
; built now (a window dragged onto a display of the other depth, or no
; decode has built them). CF = 1 there are none. Preserves all
px_tabsync:
    push ax
    cmp byte [px_busy], 0           ; the worker owns them while it works
    jne .no
    call px_viewclaim
    jc .no
    cmp byte [px_tabok], 0
    je .build
    mov al, [px_bpp]
    cmp al, [px_tdepth]
    je .yes
.build:
    mov al, [px_bpp]
    mov [px_tdepth], al
    call px_tables
    mov byte [px_tabok], 1
.yes:
    clc
    pop ax
    ret
.no:
    stc
    pop ax
    ret

; =============================================================================
; THE VIEW (SPEC.md 106.11)
; =============================================================================

; px_haspic - CF = 0 when there is a picture to show: decoded, or decoding.
; Preserves all
px_haspic:
    cmp byte [px_cur + PXR_HAVE], 0
    jne .y
    cmp byte [px_busy], 1
    je .y
    stc
    ret
.y:
    clc
    ret

; px_vmake - BX = a view, [px_vw] x [px_vh] its box, [px_vz] its zoom: the
; steps and the picture's screen size (SPEC.md 106.11). Preserves all
px_vmake:
    push ax
    push cx
    push dx
    push si
    mov si, bx
    mov ax, [px_vz]                 ; hs = 2^32 / (Z << s)
    mov dx, [px_vz + 2]
    mov cl, [px_cur + PXR_SCL]
    xor ch, ch
    jcxz .s0
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.s0:
    mov [px_dv0], ax
    mov [px_dv1], dx
    xor ax, ax
    mov [px_n0], ax
    mov [px_n1], ax
    mov word [px_n2], 1
    call px_div
    mov ax, [px_n0]
    mov dx, [px_n1]
    mov [si + VW_HS], ax
    mov [si + VW_HS + 2], dx
    mov bl, [px_rkind]              ; vs = hs x ad / an
    xor bh, bh
    shl bx, 1
    mov cx, [px_aspd + bx]
    push bx
    call px_mulx                    ; BX:DX:AX
    mov [px_n0], ax
    mov [px_n1], dx
    mov [px_n2], bx
    pop bx
    mov ax, [px_aspn + bx]
    mov [px_dv0], ax
    mov word [px_dv1], 0
    call px_div
    mov ax, [px_n0]
    mov dx, [px_n1]
    mov [si + VW_VS], ax
    mov [si + VW_VS + 2], dx
    mov ax, [px_cur + PXR_MW]       ; dw = ceil(mw << 16 / hs)
    mov bx, [si + VW_HS]
    mov cx, [si + VW_HS + 2]
    call px_vside
    mov [si + VW_DW], ax
    mov ax, [px_cur + PXR_MH]
    mov bx, [si + VW_VS]
    mov cx, [si + VW_VS + 2]
    call px_vside
    mov [si + VW_DH], ax
    mov bx, si
    pop si
    pop dx
    pop cx
    pop ax
    ret

; px_vside - AX = master pixels, CX:BX = a step: AX = ceil((AX << 16) /
; step), at most PX_SIDEMAX + 1. Preserves all but AX
px_vside:
    push dx
    mov dx, ax
    xor ax, ax
    call px_cdiv32
    or dx, dx
    jnz .big
    cmp ax, PX_SIDEMAX
    jbe .out
.big:
    mov ax, PX_SIDEMAX + 1
.out:
    pop dx
    ret

; px_fitz - [px_vz] := the largest zoom up to 1 that shows the whole picture
; in [px_vw] x [px_vh] at this display's aspect, at least PX_ZMIN. Preserves
; all
;
; IN CLOSED FORM from the steps the view takes (SPEC.md 106.23, pixelsim's
; fit_z): hs = 2^32 div (z << s) >= ceil(mw 2^16 / w) keeps the picture's
; width inside, vs = hs ad div an >= ceil(mh 2^16 / h) its height, so z =
; (2^32 div max(ceil(mw 2^16 / w), ceil(ceil(mh 2^16 / h) an / ad))) >> s.
; Until then it was min(w 2^16 / sw, h ad 2^16 / (sh an)), whose view could
; floor its steps into a picture a row longer than the canvas - one case in
; seven, the CGA's 5/12 the worst - so at Fit the master's last row never
; showed (the decoder speed pass's note)
px_fitz:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [px_cur + PXR_MW]       ; hx
    xor ax, ax
    mov bx, [px_vw]
    xor cx, cx
    call px_cdiv32
    push dx
    push ax
    mov dx, [px_cur + PXR_MH]       ; vy
    xor ax, ax
    mov bx, [px_vh]
    xor cx, cx
    call px_cdiv32
    mov bl, [px_rkind]              ; hy = ceil(vy an / ad)
    xor bh, bh
    shl bx, 1
    mov si, [px_aspd + bx]
    mov cx, [px_aspn + bx]
    push ax
    mov ax, dx
    mul cx
    mov di, ax
    pop ax
    mul cx
    add dx, di
    mov bx, si
    xor cx, cx
    call px_cdiv32
    pop bx                          ; CX:BX = hx
    pop cx
    cmp dx, cx                      ; the larger
    ja .hy
    jb .hx
    cmp ax, bx
    jae .hy
.hx:
    mov ax, bx
    mov dx, cx
.hy:
    mov [px_dv0], ax
    mov [px_dv1], dx
    or ax, dx
    jnz .dv
    inc word [px_dv0]
.dv:
    xor ax, ax
    mov [px_n0], ax
    mov [px_n1], ax
    inc ax
    mov [px_n2], ax
    call px_div                     ; 2^32 / it...
    mov ax, [px_n0]
    mov dx, [px_n1]
    mov cl, [px_cur + PXR_SCL]      ; ...>> s
    xor ch, ch
    jcxz .s0
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
.s0:
    or dx, dx                       ; never above 1
    jz .lo
    xor ax, ax
    mov dx, 1
    jmp short .st
.lo:
    cmp ax, PX_ZMIN                 ; ...nor below the least
    jae .st
    mov ax, PX_ZMIN
.st:
    mov [px_vz], ax
    mov [px_vz + 2], dx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_vcalc - THE CANVAS'S VIEW, from the layout: called by px_layout once
; the canvas is placed. Zoom (Fit or the user's), steps, the picture's size,
; the pan clamped, the picture's place. Preserves all
px_vcalc:
    push ax
    push bx
    push cx
    push dx
    call px_haspic
    jc .out
    mov ax, [px_cvx2]
    sub ax, [px_cvx1]
    inc ax
    jle .out
    mov [px_cw], ax
    mov [px_vw], ax
    mov ax, [px_midy2]
    sub ax, [px_midy1]
    inc ax
    jle .out
    mov [px_ch], ax
    mov [px_vh], ax
    call px_vkeyed                  ; nothing it depends on moved: the steps
    je .same                        ; and the size are what they were
    cmp byte [px_zfit], 0
    je .user
    call px_fitz
    jmp short .z
.user:
    mov ax, [px_z]
    mov [px_vz], ax
    mov ax, [px_z + 2]
    mov [px_vz + 2], ax
.z:
    mov ax, [px_vz]
    mov [px_zcur], ax
    mov ax, [px_vz + 2]
    mov [px_zcur + 2], ax
    mov bx, px_vcan
    call px_vmake
.same:
    call px_vclamp
.out:
    call px_zfield                  ; the status bar's zoom follows the view
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_vkeyed - ZF = 1 when the canvas's view depends on exactly what it did
; last time (the canvas's size, the zoom, the picture, its scale and the
; display's aspect); else ZF = 0 and the new key is banked. The view's
; arithmetic is six 48-round divisions, and every press and every wake
; lays out. Preserves all but the flags
px_vkeyed:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, px_vkeyn                ; the key now
    mov ax, [px_cw]
    stosw
    mov ax, [px_ch]
    stosw
    mov ax, [px_z]
    stosw
    mov ax, [px_z + 2]
    stosw
    mov ax, [px_cur + PXR_MW]
    stosw
    mov ax, [px_cur + PXR_MH]
    stosw
    mov al, [px_zfit]
    mov ah, [px_cur + PXR_SCL]
    stosw
    mov al, [px_cur + PXR_SERIAL]
    mov ah, [px_rkind]
    stosw
    mov si, px_vkeyn
    mov di, px_vkey
    mov cx, 8
    cld
    repe cmpsw
    je .out
    mov si, px_vkeyn                ; changed: bank it (ZF = 0 still)
    mov di, px_vkey
    mov cx, 8
    rep movsw
    or ax, 1                        ; (ZF = 0 for the caller)
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; px_vclamp - [px_ox] [px_oy] kept in range for the canvas, and the
; picture's place from them (SPEC.md 106.11): a side smaller than the canvas
; is centred, across on the byte grid; a larger one is clamped so the
; canvas stays covered, across in steps of 8. Preserves all
px_vclamp:
    push ax
    push bx
    mov ax, [px_cw]
    sub ax, [px_vcan + VW_DW]
    jl .wide
    shr ax, 1                       ; centred, on the grid
    and ax, 0xFFF8
    mov [px_ox], ax
    jmp short .y
.wide:
    neg ax                          ; AX = dw - cw > 0
    and ax, 0xFFF8                  ; the farthest left, floored to 8
    neg ax
    mov bx, [px_ox]
    and bx, 0xFFF8
    cmp bx, 0
    jle .w1
    xor bx, bx
.w1:
    cmp bx, ax
    jge .w2
    mov bx, ax
.w2:
    mov [px_ox], bx
.y:
    mov ax, [px_ch]
    sub ax, [px_vcan + VW_DH]
    jl .tall
    shr ax, 1
    mov [px_oy], ax
    jmp short .place
.tall:
    mov bx, [px_oy]
    cmp bx, 0
    jle .t1
    xor bx, bx
.t1:
    cmp bx, ax
    jge .t2
    mov bx, ax
.t2:
    mov [px_oy], bx
.place:
    mov ax, [px_cvx1]
    add ax, [px_ox]
    mov [px_vcan + VW_IX], ax
    mov ax, [px_midy1]
    add ax, [px_oy]
    mov [px_vcan + VW_IY], ax
    pop bx
    pop ax
    ret

; px_viewnew - a new picture opens at Fit, centred. Preserves all
px_viewnew:
    mov byte [px_vkey], 0xFF        ; (a key no layout can have)
    mov byte [px_zfit], 1
    mov word [px_ox], 0
    mov word [px_oy], 0
    mov byte [px_thok], 0
    ret

; =============================================================================
; THE COMPOSER (SPEC.md 106.11)
; =============================================================================

; px_cset - a pass over columns AX (on the byte grid) and CX pixels through
; view BX: where the DDA starts, its step, the groups of eight a row is.
; Preserves all
px_cset:
    push ax
    push bx
    push cx
    push dx
    mov [px_cbv], bx
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov [px_cgrp], cx
    sub ax, [bx + VW_IX]            ; (x0 - ix) x hs: int and fraction
    mov cx, ax
    mov ax, [bx + VW_HS]
    mov dx, [bx + VW_HS + 2]
    push bx
    call px_mulx                    ; BX:DX:AX
    pop bx
    mov [px_cbfrac], ax
    mov [px_cbint], dx
    mov ax, [bx + VW_HS]
    mov [px_chfrac], ax
    mov ax, [bx + VW_HS + 2]
    dec ax                          ; lodsb has stepped one already
    mov [px_chint], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_crow - compose screen row AX of the pass [px_cset] made into ES:DI (in
; the view claim), [px_cgrp] groups of eight: packed 4bpp (four bytes a
; group) or 1bpp (one), by [px_cdep]. DI is advanced.
; Clobbers AX, BX, CX, DX, SI, BP
px_crow:
    mov bx, [px_cbv]
    sub ax, [bx + VW_IY]            ; the picture's row on the screen
    push ax
    mov cx, ax
    mov ax, [bx + VW_VS]
    mov dx, [bx + VW_VS + 2]
    call px_mulx                    ; DX = the master row
    mov ax, dx
    mul word [px_cur + PXR_MW]
    add ax, [px_cbint]
    adc dx, 0                       ; DX:AX = the first sample's offset
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_cur + PXR_MSEG]
    mov [px_crseg], ax
    pop ax                          ; BH = the row's tables: (y & 7) x 8
    and al, 7
    mov cl, 3
    shl al, cl
    mov bh, al
    xor bl, bl
    mov dx, [px_cbfrac]
    mov bp, [px_chfrac]
    mov cx, [px_chint]
    mov ax, [px_cgrp]
    mov [px_ccnt], ax
    push ds
    cmp word [px_chfrac], 0         ; ONE master pixel a screen pixel: no
    jne .dda                        ; DDA at all (a picture at 100%)
    cmp word [px_chint], 0
    jne .dda
    mov cx, [px_cgrp]               ; ...and CX is free to count the groups
    cmp byte [px_cdep], 1
    mov ds, [px_crseg]
    je .one1
.g41:
%rep 4
    lodsb
    es xlatb
    mov ah, al
    inc bh
    lodsb
    es xlatb
    or al, ah
    stosb
    inc bh
%endrep
    sub bh, 8
    loop .g41
    pop ds
    ret
.one1:
    lodsb                           ; the first pixel's bit is the byte...
    es xlatb
    mov ah, al
%rep 6
    inc bh
    lodsb
    es xlatb
    or ah, al
%endrep
    inc bh
    lodsb
    es xlatb
    or al, ah                       ; ...and the eighth's goes straight out
    stosb
    sub bh, 7
    loop .one1
    pop ds
    ret
.dda:
    cmp byte [px_cdep], 1
    mov ds, [px_crseg]
    je .one
.g4:
%rep 4
    lodsb                           ; the left pixel of a pair...
    add dx, bp
    adc si, cx
    es xlatb                        ; ...its colour, << 4
    mov ah, al
    inc bh
    lodsb                           ; the right
    add dx, bp
    adc si, cx
    es xlatb
    or al, ah
    stosb
    inc bh
%endrep
    sub bh, 8
    dec word [cs:px_ccnt]
    jnz .g4
    pop ds
    ret
.one:
    xor ah, ah
%rep 8
    lodsb
    add dx, bp
    adc si, cx
    es xlatb                        ; the cell's bit, or 0
    or ah, al
    inc bh
%endrep
    sub bh, 8
    mov al, ah
    stosb
    dec word [cs:px_ccnt]
    jnz .one
    pop ds
    ret

; px_c2p - the LANE row at [px_vo_row] into four planes at DI (plane k at
; DI + k x BP), [px_cgrp] groups. ES = the view claim. Clobbers AX, BX, CX,
; DX, SI, DI.
;
; A group is four lanes, a lane two pixels with their bits interleaved
; (px_texpand) - [a3 b3 a2 b2 a1 b1 a0 b0] - which is a 4x8 bit transpose
; already two stages done. The last two are a BUTTERFLY in registers: swap
; nibbles between lanes 0/2 and 1/3, then 2-bit fields between 0/1 and 2/3,
; and the four bytes ARE the planes. Fifteen register operations a group,
; where a 4 KB table turned it at four lookups and eight memory words - 70
; cycles a pixel on an 8088 against about 40
px_c2p:
    push ds
    mov si, [px_vo_row]
    mov ch, [px_cgrp]               ; CH = groups (a canvas has under 256),
    mov cl, 4                       ; CL = the nibble shift
    push es
    pop ds
.g:
    lodsw
    xchg ax, dx                     ; DL, DH = lanes 0, 1
    lodsw                           ; AL, AH = lanes 2, 3
    mov bx, ax                      ; nibbles: 0 <-> 2, 1 <-> 3
    shr bx, cl
    xor bx, dx
    and bx, 0x0F0F
    xor dx, bx
    shl bx, cl
    xor ax, bx
    xchg al, dh                     ; (the next pairs side by side)
    mov bx, ax                      ; 2-bit fields: 0 <-> 1, 2 <-> 3
    shr bx, 1
    shr bx, 1
    xor bx, dx
    and bx, 0x3333
    xor dx, bx
    shl bx, 1
    shl bx, 1
    xor ax, bx                      ; AH, DH, AL, DL = planes 0, 1, 2, 3
    mov [di], ah
    mov [ds:bp + di], dh
    add di, bp
    mov [ds:bp + di], al
    add di, bp
    mov [ds:bp + di], dl
    sub di, bp
    sub di, bp
    inc di
    dec ch
    jnz .g
    pop ds
    ret

; px_rimg - RENDER the picture's part of a screen rect: columns AX..CX (AX on
; the byte grid, inside the picture), rows BX..DX, through the canvas view.
; Lock held, the clip armed. Band by band: BLITP when the probe says it may,
; else BLIT4; BLIT1 on a 1bpp display. Preserves all
px_rimg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [px_rx0], ax
    mov [px_ry0], bx
    mov [px_ryend], dx
    sub cx, ax
    inc cx
    mov [px_rw], cx
    mov bx, px_vcan
    call px_cset
    mov al, [px_bpp]
    mov [px_cdep], al
    mov byte [px_useP], 0
    cmp al, 1
    je .bands
    mov ax, [px_rx0]                ; may the planes go straight on? (the
    mov bx, [px_ry0]                ; probe: DI bit 15, nothing drawn)
    mov cx, [px_rw]
    mov dx, [px_ryend]
    sub dx, bx
    inc dx
    mov di, 0x8000 | 0x4000 | 1
    call OSAPI_GFX_BLITP
    jc .bands
    mov byte [px_useP], 1
.bands:
    mov es, [px_vseg]
    mov ax, [px_ry0]
.band:
    cmp ax, [px_ryend]
    jle .b1
    jmp .done
.b1:
    mov [px_rby], ax
    mov cx, [px_ryend]              ; n = min(16, rows left)
    sub cx, ax
    inc cx
    cmp cx, PX_BANDH
    jbe .n
    mov cx, PX_BANDH
.n:
    mov [px_rbn], cx
    call px_rband
    mov ax, [px_rby]
    add ax, [px_rbn]
    jmp short .band
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_rband - one band of [px_rbn] rows from [px_rby], composed and put up
px_rband:
    cmp byte [px_cdep], 1
    je .one
    cmp byte [px_useP], 0
    je .packed
    ; --- planes: each row packed, then turned, then one BLITP -------------
    mov di, [px_vo_band]
    mov cx, [px_rbn]
    mov ax, [px_rby]
.prow:
    push ax
    push cx
    push di
    mov di, [px_vo_row]
    call px_crow
    pop di
    push di
    mov bp, [px_cgrp]
    call px_c2p
    pop di
    mov ax, [px_cgrp]               ; the next row: four planes on
    shl ax, 1
    shl ax, 1
    add di, ax
    pop cx
    pop ax
    inc ax
    loop .prow
    mov si, [px_vo_band]
    mov di, [px_cgrp]
    mov bp, di
    shl bp, 1
    shl bp, 1
    or di, 0x4000                   ; walk an armed clip (SPEC.md 5.4.3.6)
    mov ax, [px_rx0]
    mov bx, [px_rby]
    mov cx, [px_rw]
    mov dx, [px_rbn]
    call OSAPI_GFX_BLITP
    jnc .out
.packed:                            ; --- packed rows and BLIT4 ---------------
    mov di, [px_vo_band]
    mov cx, [px_rbn]
    mov ax, [px_rby]
.krow:
    push ax
    push cx
    call px_crow
    pop cx
    pop ax
    inc ax
    loop .krow
    push ds                         ; the lanes made packed pairs, in place
    mov ax, [px_cgrp]
    shl ax, 1
    shl ax, 1
    mul word [px_rbn]
    mov cx, ax
    mov bx, [px_vo_c2p]
    mov si, [px_vo_band]
    mov di, si
    push es
    pop ds
    cld
.unsp:
    lodsb
    xlatb
    stosb
    loop .unsp
    pop ds
    mov si, [px_vo_band]
    mov bp, [px_cgrp]
    shl bp, 1
    shl bp, 1
    mov ax, [px_rx0]
    mov bx, [px_rby]
    mov cx, [px_rw]
    mov dx, [px_rbn]
    call OSAPI_GFX_BLIT4
    jmp short .out
.one:                               ; --- 1bpp rows and BLIT1 -----------------
    mov di, [px_vo_band]
    mov cx, [px_rbn]
    mov ax, [px_rby]
.orow:
    push ax
    push cx
    call px_crow
    pop cx
    pop ax
    inc ax
    loop .orow
    mov ax, (CBLACK << 8) | CWHITE  ; a set bit lit, a clear one dark
    call OSAPI_GFX_BLIT1_PEN
    mov si, [px_vo_band]
    mov bp, [px_cgrp]
    mov ax, [px_rx0]
    mov bx, [px_rby]
    mov cx, [px_rw]
    mov dx, [px_rbn]
    call OSAPI_GFX_BLIT1
.out:
    ret

; =============================================================================
; THE CANVAS WITH A PICTURE ON IT
; =============================================================================

; px_ground - fill AX/BX/CX/DX (empty is nothing) in the canvas's ground
; around a picture: dark grey on 4bpp, the 50% dither on 1bpp, where a grey
; would round to black (SPEC.md 39.4). Preserves all
px_ground:
    cmp ax, cx
    jg .no
    cmp bx, dx
    jg .no
    cmp byte [px_bpp], 1
    ja .col
    call OSAPI_GFX_FILL_GRAY
    ret
.col:
    push ax
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.no:
    ret

; px_rcanvas - the canvas's rect AX/BX/CX/DX (screen, inside the canvas)
; with the picture in it: ground where the picture is not, the picture where
; it is - only its DECODED rows while a decode runs (SPEC.md 106.9), ground
; for the rest. Every pixel once. Preserves all
px_rcanvas:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call px_mqhide                  ; the selection's outline (pxtools.inc)
    mov [px_qx1], ax
    mov [px_qy1], bx
    mov [px_qx2], cx
    mov [px_qy2], dx
    ; the picture's rect, then the part of ours it covers: si/di/..
    mov ax, [px_vcan + VW_IX]
    mov [px_ax1], ax
    add ax, [px_vcan + VW_DW]
    dec ax
    mov [px_ax2], ax
    mov ax, [px_vcan + VW_IY]
    mov [px_ay1], ax
    add ax, [px_vcan + VW_DH]
    dec ax
    mov [px_ay2], ax
    call px_rowspan                 ; the decoded rows narrow it while decoding
    mov ax, [px_ay1]                ; (THE span this render uses: px_rowsall
    mov [px_cspan1], ax             ; banks it, not a later read of a count
    mov ax, [px_ay2]                ; the worker has moved on since)
    mov [px_cspan2], ax
    mov ax, [px_qx1]                ; intersect
    cmp ax, [px_ax1]
    jle .i1
    mov [px_ax1], ax
.i1:
    mov ax, [px_qx2]
    cmp ax, [px_ax2]
    jge .i2
    mov [px_ax2], ax
.i2:
    mov ax, [px_qy1]
    cmp ax, [px_ay1]
    jle .i3
    mov [px_ay1], ax
.i3:
    mov ax, [px_qy2]
    cmp ax, [px_ay2]
    jge .i4
    mov [px_ay2], ax
.i4:
    mov ax, [px_ax1]
    cmp ax, [px_ax2]
    jg .allg
    mov ax, [px_ay1]
    cmp ax, [px_ay2]
    jg .allg
    cmp byte [px_tabok], 0
    je .allg
    ; ground: above, below, left, right of the picture's part
    mov ax, [px_qx1]
    mov bx, [px_qy1]
    mov cx, [px_qx2]
    mov dx, [px_ay1]
    dec dx
    call px_ground
    mov bx, [px_ay2]
    inc bx
    mov dx, [px_qy2]
    call px_ground
    mov bx, [px_ay1]
    mov dx, [px_ay2]
    mov cx, [px_ax1]
    and cx, 0xFFF8                  ; (the picture's part starts on the grid)
    dec cx
    call px_ground
    mov ax, [px_ax2]
    inc ax
    mov cx, [px_qx2]
    call px_ground
    mov ax, [px_ax1]
    and ax, 0xFFF8
    mov cx, [px_ax2]
    call px_rimg
    jmp short .out
.allg:
    mov ax, [px_qx1]
    mov bx, [px_qy1]
    mov cx, [px_qx2]
    mov dx, [px_qy2]
    call px_ground
.out:
    call px_mqshow                  ; ...back where the view puts it
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_rowspan - while a decode runs, narrow [px_ay1]..[px_ay2] to the screen
; rows whose master rows are complete: from the top for a top-down file,
; from the bottom for a bottom-up one. Preserves all
px_rowspan:
    cmp byte [px_busy], 1
    jne .ret
    push ax
    push bx
    push cx
    push dx
    cmp byte [px_tabok], 0
    je .none
    mov ax, [px_rdone]
    cmp byte [px_dtop], 0
    jne .top
    mov ax, [px_cur + PXR_MH]       ; bottom-up: rows mh - rdone .. mh - 1
    sub ax, [px_rdone]
    call px_m2s                     ; AX = the first screen row showing it
    add ax, [px_vcan + VW_IY]
    cmp ax, [px_ay1]
    jle .out
    mov [px_ay1], ax
    jmp short .out
.top:
    call px_m2s                     ; rows 0 .. rdone - 1
    add ax, [px_vcan + VW_IY]
    dec ax
    cmp ax, [px_ay2]
    jge .out
    mov [px_ay2], ax
    jmp short .out
.none:
    mov ax, [px_ay1]                ; no tables yet: nothing is showable
    dec ax
    mov [px_ay2], ax
.out:
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; px_m2s - AX = a master row count: AX = ceil((AX << 16) / vs), the screen
; rows above the first one that shows master row AX, at most the picture's
; height. Preserves all but AX
px_m2s:
    push bx
    push cx
    push dx
    mov bx, [px_vcan + VW_VS]
    mov cx, [px_vcan + VW_VS + 2]
    mov dx, ax
    xor ax, ax
    call px_cdiv32
    or dx, dx
    jnz .h
    cmp ax, [px_vcan + VW_DH]
    jbe .out
.h:
    mov ax, [px_vcan + VW_DH]
.out:
    pop dx
    pop cx
    pop bx
    ret

; px_paintrows - FROM THE WAKE while a decode runs: paint the screen rows
; that became complete since the last time, and only those (SPEC.md 106.9).
; Lock held, layout done, clip armed. Preserves all
px_paintrows:
    push ax
    push bx
    push cx
    push dx
    cmp byte [px_tabok], 0
    je .out
    mov ax, [px_vcan + VW_IX]       ; the canvas's columns the picture spans
    cmp ax, [px_cvx1]
    jge .x1
    mov ax, [px_cvx1]
.x1:
    mov cx, [px_vcan + VW_IX]
    add cx, [px_vcan + VW_DW]
    dec cx
    cmp cx, [px_cvx2]
    jle .x2
    mov cx, [px_cvx2]
.x2:
    mov [px_px1], ax
    mov [px_px2], cx
    mov ax, [px_vcan + VW_IY]       ; the complete span now: ay1..ay2
    mov [px_ay1], ax
    add ax, [px_vcan + VW_DH]
    dec ax
    mov [px_ay2], ax
    call px_rowspan
    cmp byte [px_dtop], 0
    je .bot
    mov bx, [px_vcan + VW_IY]       ; top-down: [iy + ypaint, ay2]
    add bx, [px_ypaint]
    mov dx, [px_ay2]
    mov ax, dx
    sub ax, [px_vcan + VW_IY]
    inc ax
    mov [px_ypaint], ax
    jmp short .go
.bot:
    mov dx, [px_vcan + VW_IY]       ; bottom-up: [ay1, bottom - ypbot]
    add dx, [px_vcan + VW_DH]
    dec dx
    sub dx, [px_ypbot]
    mov bx, [px_ay1]
    mov ax, [px_vcan + VW_IY]
    add ax, [px_vcan + VW_DH]
    sub ax, bx
    mov [px_ypbot], ax
.go:
    cmp bx, [px_midy1]              ; inside the canvas
    jge .y1
    mov bx, [px_midy1]
.y1:
    cmp dx, [px_midy2]
    jle .y2
    mov dx, [px_midy2]
.y2:
    cmp bx, dx
    jg .out
    mov ax, [px_px1]
    mov cx, [px_px2]
    cmp ax, cx
    jg .out
    and ax, 0xFFF8
    call px_rimg
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_rowsall - the painted spans := what a whole render of the canvas just
; painted (the complete rows): so a wake after a repaint or a pan paints
; only what is new from then. Preserves all
px_rowsall:
    push ax
    mov ax, [px_cspan1]             ; the span px_rcanvas rendered
    mov [px_ay1], ax
    mov ax, [px_cspan2]
    mov [px_ay2], ax
    mov ax, [px_ay2]
    cmp ax, [px_ay1]
    jl .none                        ; nothing showable yet: nothing painted
    sub ax, [px_vcan + VW_IY]
    inc ax
    mov [px_ypaint], ax
    mov ax, [px_vcan + VW_IY]
    add ax, [px_vcan + VW_DH]
    sub ax, [px_ay1]
    mov [px_ypbot], ax
    pop ax
    ret
.none:
    mov word [px_ypaint], 0
    mov word [px_ypbot], 0
    pop ax
    ret

; =============================================================================
; ZOOM AND PAN (SPEC.md 106.11)
; =============================================================================

; px_zoomto - DX:AX = a zoom (0 = Fit): applied when every side it makes
; fits PX_SIDEMAX, keeping the canvas's centre where it is; then the canvas,
; the Navigator and the status bar. Lock held. Preserves all
px_zoomto:
    push ax
    push bx
    push cx
    push dx
    push si
    call px_haspic
    jc .out
    call px_layout
    jc .out
    mov si, ax
    or si, dx
    jnz .user
    cmp byte [px_zfit], 0
    jne .out
    mov byte [px_zfit], 1
    jmp short .apply
.user:
    mov [px_vz], ax                 ; would it fit the sides?
    mov [px_vz + 2], dx
    mov bx, px_vtry
    call px_vmake
    cmp word [px_vtry + VW_DW], PX_SIDEMAX
    ja .out
    cmp word [px_vtry + VW_DH], PX_SIDEMAX
    ja .out
    ; the master point at the canvas's centre, kept there
    mov ax, [px_cw]
    shr ax, 1
    add ax, [px_cvx1]
    sub ax, [px_vcan + VW_IX]
    mov cx, [px_vtry + VW_DW]
    mul cx
    div word [px_vcan + VW_DW]      ; the same fraction of the new width
    mov bx, [px_cw]
    shr bx, 1
    sub bx, ax
    mov [px_ox], bx
    mov ax, [px_ch]
    shr ax, 1
    add ax, [px_midy1]
    sub ax, [px_vcan + VW_IY]
    mov cx, [px_vtry + VW_DH]
    mul cx
    div word [px_vcan + VW_DH]
    mov bx, [px_ch]
    shr bx, 1
    sub bx, ax
    mov [px_oy], bx
    mov ax, [px_vz]
    mov [px_z], ax
    mov ax, [px_vz + 2]
    mov [px_z + 2], ax
    mov byte [px_zfit], 0
.apply:
    call px_zfield
    mov al, PX_R_CANVAS | PX_R_NFRAME | PX_R_SDIRTY
    call px_regdraw                 ; (the panels' content is the picture's,
                                    ; not the view's: only the frame moves)
    call px_zfiner                  ; past what fast open decoded: finer
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_zfiner - after a zoom: a JPEG that FAST OPEN held to scale s, zoomed
; past 2^-s, is decoded again at the coarsest scale that covers the zoom -
; on the worker, Stop and progress as any open, the zoom and the pan kept,
; the coarse picture shown until the finer replaces it and put back by a
; cancel (SPEC.md 106.19). Lock held. Preserves all
px_zfiner:
    push ax
    push cx
    push dx
    cmp byte [px_dirty], 0          ; an edited picture is the master, not
    jne .out                        ; the file: nothing to read finer
    mov cl, [px_cur + PXR_SCL]
    or cl, cl
    jz .out
    cmp cl, [px_cur + PXR_FAST]     ; the scale was fast open's, not memory's
    jne .out
    cmp byte [px_busy], 0
    jne .out
    cmp byte [px_cur + PXR_HAVE], 0
    je .out
    cmp byte [px_zfit], 0
    jne .out
    ; the zoom past 2^-s, within fast open's own third? (Z is a source
    ; pixel's screen pixels, 16.16; DX = 3/4 Z, px_sfast's test)
    cmp word [px_z + 2], 0
    jne .finer
    mov dx, [px_z]
    mov ax, dx
    shr ax, 1
    shr ax, 1
    sub dx, ax
    mov ax, 0x8000
    dec cl
    shr ax, cl                      ; 65536 >> s
    cmp dx, ax
    jbe .out
.finer:
    mov cl, [px_cur + PXR_SCL]      ; the coarsest s' < s, 2^-s' >= 3/4 Z
.s:
    dec cl
    jz .go
    cmp word [px_z + 2], 0
    jne .s
    mov ax, 0x8000
    push cx
    dec cl
    shr ax, cl
    pop cx
    cmp dx, ax
    ja .s
.go:
    mov [px_zreq], cl
    mov byte [px_keepv], 1
    call px_revert                  ; the same file, from its own folder
    mov byte [px_zreq], 0xFF
    mov byte [px_keepv], 0
.out:
    pop dx
    pop cx
    pop ax
    ret

; px_zstep - AL = 1 in, 0 out: the next step from the zoom shown. Lock held
px_zstep:
    push ax
    push bx
    push cx
    push dx
    call px_haspic
    jc .out
    mov bx, px_zsteps
    mov cx, PX_NZ
    or al, al
    jz .outw
.in:                                ; the first step above it
    mov ax, [bx]
    mov dx, [bx + 2]
    cmp dx, [px_zcur + 2]
    ja .go
    jb .in1
    cmp ax, [px_zcur]
    ja .go
.in1:
    add bx, 4
    loop .in
    jmp short .out
.outw:                              ; the last step below it
    mov bx, px_zsteps + 4 * (PX_NZ - 1)
.o1:
    mov ax, [bx]
    mov dx, [bx + 2]
    cmp dx, [px_zcur + 2]
    jb .go
    ja .o2
    cmp ax, [px_zcur]
    jb .go
.o2:
    sub bx, 4
    loop .o1
    jmp short .out
.go:
    call px_zoomto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_panto - AX = the wanted [px_ox], BX = the wanted [px_oy]: the picture
; moves there - the pixels already on the glass by OSAPI_GFX_SCROLL (rows)
; and OSAPI_GFX_SAVE/REST (columns, by eights), only the exposed strips
; rendered; the whole canvas when a slot refuses, the move is a canvas or
; more, or a decode is painting it. Lock held, layout done. Preserves all
px_panto:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call px_haspic
    jnc .p0
    jmp .out
.p0:
    mov cx, [px_ox]                 ; where it was
    mov dx, [px_oy]
    mov [px_ox], ax
    mov [px_oy], bx
    call px_vclamp
    mov ax, [px_ox]
    sub ax, cx
    mov [px_pdx], ax                ; the move, across (a multiple of 8)...
    mov ax, [px_oy]
    sub ax, dx
    mov [px_pdy], ax                ; ...and down
    or ax, [px_pdx]
    jnz .moved
    jmp .out
.moved:
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jnc .vis
    jmp .out
.vis:
    call px_mqhide                  ; the selection's outline, off for the
    inc byte [px_mqhold]            ; move and back after it
    call px_navframe_off            ; the Navigator's frame, off for the move
    cmp byte [px_busy], 1
    je .full
    mov ax, [px_pdx]                ; a move as big as the canvas: render it
    or ax, ax
    jns .ax
    neg ax
.ax:
    cmp ax, [px_cw]
    jae .full
    mov ax, [px_pdy]
    or ax, ax
    jns .ay
    neg ax
.ay:
    cmp ax, [px_ch]
    jae .full
    ; --- the canvas's columns on the grid: x1 .. xg (xg + 1 a multiple of 8)
    mov ax, [px_cvx2]
    inc ax
    and ax, 0xFFF8
    dec ax
    mov [px_pxg], ax
    cmp ax, [px_cvx1]
    jle .full
    ; --- rows: one scroll -------------------------------------------------
    mov si, [px_pdy]
    or si, si
    jz .cols
    neg si                          ; a picture moving down scrolls DOWN
    mov ax, [px_cvx1]
    mov bx, [px_midy1]
    mov cx, [px_pxg]
    mov dx, [px_midy2]
    call OSAPI_GFX_SCROLL
    jc .full
.cols:
    mov ax, [px_pdx]
    or ax, ax
    jz .exposed
    call px_hshift
    jc .full
.exposed:
    call px_vcalc                   ; the new place, then the strips it opened
    call px_pexposed
    jmp short .done
.full:
    call px_vcalc
    mov ax, [px_cvx1]
    mov bx, [px_midy1]
    mov cx, [px_cvx2]
    mov dx, [px_midy2]
    call px_rcanvas
    call px_rowsall
.done:
    dec byte [px_mqhold]
    call px_mqshow
    call px_navframe_on
    call OSAPI_WM_CLIP_CLEAR
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_hshift - the canvas's grid columns moved [px_pdx] across, band by band:
; OSAPI_GFX_SAVE of the part that stays, OSAPI_GFX_REST of it where it goes,
; through the band buffer. CF = 1 a slot refused (the canvas is then
; rendered whole). Preserves all
px_hshift:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [px_vseg]
    mov bx, [px_midy1]
.b:
    cmp bx, [px_midy2]
    jg .ok
    mov dx, bx                      ; DX = the band's last row
    add dx, PX_BANDH - 1
    cmp dx, [px_midy2]
    jle .d
    mov dx, [px_midy2]
.d:
    mov ax, [px_cvx1]               ; the part that stays: shifted right, it
    mov cx, [px_pxg]                ; is x1 .. xg - dx; left, x1 - dx .. xg
    mov si, [px_pdx]
    or si, si
    js .left
    sub cx, si
    jmp short .save
.left:
    sub ax, si
.save:
    mov di, [px_vo_band]
    call OSAPI_GFX_SAVE
    jc .no
    add ax, si                      ; ...put back dx along
    add cx, si
    mov si, [px_vo_band]
    call OSAPI_GFX_REST
    jc .no
    mov bx, dx
    inc bx
    jmp short .b
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_pexposed - after a move, render what it uncovered: the rows the scroll
; opened, the columns the shift opened, and the sliver right of the grid
px_pexposed:
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_pdy]
    or ax, ax
    jz .cols
    mov bx, [px_midy1]
    mov dx, [px_midy2]
    or ax, ax
    js .up
    mov dx, bx                      ; moved down: the top AX rows
    add dx, ax
    dec dx
    jmp short .rr
.up:
    mov bx, dx                      ; moved up: the bottom -AX rows
    add bx, ax
    inc bx
.rr:
    mov ax, [px_cvx1]
    mov cx, [px_pxg]
    call px_rcanvas
.cols:
    mov ax, [px_pdx]
    or ax, ax
    jz .sliver
    mov bx, [px_midy1]
    mov dx, [px_midy2]
    js .lft
    mov cx, [px_cvx1]               ; moved right: the left AX columns
    add cx, ax
    dec cx
    mov ax, [px_cvx1]
    jmp short .rc
.lft:
    mov cx, [px_pxg]                ; moved left: the right -AX columns
    add ax, cx
    inc ax
.rc:
    call px_rcanvas
.sliver:
    mov ax, [px_pxg]                ; the columns past the grid, every time
    inc ax
    mov cx, [px_cvx2]
    cmp ax, cx
    jg .out
    mov bx, [px_midy1]
    mov dx, [px_midy2]
    call px_rcanvas
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_panby - AX = across, BX = down, in screen pixels. Lock held
px_panby:
    push ax
    push bx
    call px_layout
    jc .out
    add ax, [px_ox]
    add bx, [px_oy]
    call px_panto
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; THE NAVIGATOR (SPEC.md 106.12): the picture at its own Fit, rendered once
; into the view claim and blitted from there; the canvas's view an XOR frame
; =============================================================================

; px_navthumb - the Navigator's picture well AX/BX/CX/DX (its interior):
; the thumbnail placed in it (on the byte grid), rendered into the view
; claim when the bank is stale, and put up with the ground round it.
; Lock held. Preserves all
px_navthumb:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [px_nw1], ax
    mov [px_nw2], cx
    mov [px_nh1], bx
    mov [px_nh2], dx
    call px_haspic
    jc .ground
    cmp byte [px_cur + PXR_HAVE], 0 ; a decode in flight shows the well grey
    je .ground
    call px_tabsync
    jc .ground
    add ax, 7                       ; the thumbnail's box: the grid inside
    and ax, 0xFFF8
    mov [px_ntx], ax
    mov cx, [px_nw2]
    sub cx, ax
    inc cx
    jle .ground
    cmp cx, PX_THMW
    jbe .w
    mov cx, PX_THMW
.w:
    mov [px_vw], cx
    mov dx, [px_nh2]
    sub dx, bx
    inc dx
    jle .ground
    cmp dx, PX_THMH
    jbe .h
    mov dx, PX_THMH
.h:
    mov [px_vh], dx
    ; the bank: is it this picture, this box, this depth?
    cmp byte [px_thok], 0
    je .make
    mov al, [px_cur + PXR_SERIAL]
    cmp al, [px_thser]
    jne .make
    cmp cx, [px_thbw]
    jne .make
    cmp dx, [px_thbh]
    jne .make
    mov al, [px_bpp]
    cmp al, [px_thdep]
    je .have
.make:
    mov [px_thbw], cx
    mov [px_thbh], dx
    mov al, [px_cur + PXR_SERIAL]
    mov [px_thser], al
    mov al, [px_bpp]
    mov [px_thdep], al
    call px_fitz
    mov bx, px_vthm
    call px_vmake
    mov ax, [px_thbw]               ; a side the rounding made one pixel
    cmp ax, [px_vthm + VW_DW]       ; longer than the box is cut to it, or
    jae .bw                         ; the blit would take the well's frame
    mov [px_vthm + VW_DW], ax
.bw:
    mov ax, [px_thbh]
    cmp ax, [px_vthm + VW_DH]
    jae .bh
    mov [px_vthm + VW_DH], ax
.bh:
    call px_thrender
    mov byte [px_thok], 1
.have:
    call px_navput                  ; place it: centred, on the grid
    ; the ground round it, then the bank
    mov ax, [px_nw1]
    mov bx, [px_nh1]
    mov cx, [px_nw2]
    mov dx, [px_vthm + VW_IY]
    dec dx
    call px_wellfill
    mov bx, [px_vthm + VW_IY]
    add bx, [px_vthm + VW_DH]
    mov dx, [px_nh2]
    call px_wellfill
    mov bx, [px_vthm + VW_IY]
    mov dx, bx
    add dx, [px_vthm + VW_DH]
    dec dx
    mov cx, [px_vthm + VW_IX]
    dec cx
    call px_wellfill
    mov ax, [px_vthm + VW_IX]
    add ax, [px_vthm + VW_DW]
    mov cx, [px_nw2]
    call px_wellfill
    call px_thblit
    mov byte [px_nfon], 0           ; the frame is drawn fresh over it
    call px_navframe_on
    jmp short .out
.ground:
    mov ax, [px_nw1]
    mov bx, [px_nh1]
    mov cx, [px_nw2]
    mov dx, [px_nh2]
    call px_wellfill
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_navput - the thumbnail's place in the well [px_ntx] / [px_nh1..2]:
; centred across (on the grid) and down. Preserves all
px_navput:
    push ax
    mov ax, [px_thbw]
    sub ax, [px_vthm + VW_DW]
    jae .w
    xor ax, ax
.w:
    shr ax, 1
    and ax, 0xFFF8
    add ax, [px_ntx]
    mov [px_vthm + VW_IX], ax
    mov ax, [px_nh2]
    sub ax, [px_nh1]
    inc ax
    sub ax, [px_vthm + VW_DH]
    jae .h
    xor ax, ax
.h:
    shr ax, 1
    add ax, [px_nh1]
    mov [px_vthm + VW_IY], ax
    pop ax
    ret

; px_navplace - from the layout: the thumbnail's place in the well where it
; is NOW (a window that moved was not repainted, SPEC.md 11.96.12, and a
; press in the Navigator, its frame and a pan all read the place). Only a
; bank that is this well's is placed. Preserves all
px_navplace:
    push ax
    test byte [px_pvis], 1
    jz .out
    cmp word [px_pbhw], 0
    je .out
    cmp byte [px_thok], 0
    je .out
    mov ax, [px_nwell]
    add ax, 7
    and ax, 0xFFF8
    mov [px_ntx], ax
    mov ax, [px_nwell + 2]
    mov [px_nh1], ax
    mov ax, [px_nwell + 6]
    mov [px_nh2], ax
    call px_navput
.out:
    pop ax
    ret

; px_wellfill - a well's interior ground: light grey, or the dither on 1bpp.
; Empty is nothing. Preserves all
px_wellfill:
    cmp ax, cx
    jg .no
    cmp bx, dx
    jg .no
    cmp byte [px_bpp], 1
    ja .c
    call OSAPI_GFX_FILL_GRAY
    ret
.c:
    push ax
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.no:
    ret

; px_thrender - the thumbnail into the bank, through view [px_vthm] (its
; place is irrelevant: rows and columns are relative). Preserves all
px_thrender:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [px_vseg]
    mov ax, [px_vthm + VW_IX]       ; compose against a place at 0, 0
    push ax
    mov ax, [px_vthm + VW_IY]
    push ax
    xor ax, ax
    mov [px_vthm + VW_IX], ax
    mov [px_vthm + VW_IY], ax
    mov bx, px_vthm
    mov cx, [px_vthm + VW_DW]
    call px_cset
    mov al, [px_bpp]
    mov [px_cdep], al
    mov di, [px_vo_thm]
    xor ax, ax
.row:
    cmp ax, [px_vthm + VW_DH]
    jae .done
    push ax
    cmp byte [px_cdep], 1
    je .r1
    push di
    mov di, [px_vo_row]
    call px_crow
    pop di
    push di
    mov bp, [px_cgrp]
    call px_c2p
    pop di
    mov ax, [px_cgrp]
    shl ax, 1
    shl ax, 1
    add di, ax
    jmp short .rn
.r1:
    call px_crow
.rn:
    pop ax
    inc ax
    jmp short .row
.done:
    pop ax
    mov [px_vthm + VW_IY], ax
    pop ax
    mov [px_vthm + VW_IX], ax
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_thblit - the bank onto the glass at the thumbnail's place: BLITP, or
; (refused) the thumbnail composed again as packed rows through BLIT4 a
; band at a time; BLIT1 on 1bpp. Preserves all
px_thblit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [px_vseg]
    mov bx, px_vthm
    mov ax, [bx + VW_IX]
    mov cx, [bx + VW_DW]
    call px_cset
    mov si, [px_vo_thm]
    mov ax, [px_vthm + VW_IX]
    mov bx, [px_vthm + VW_IY]
    mov cx, [px_vthm + VW_DW]
    mov dx, [px_vthm + VW_DH]
    cmp byte [px_bpp], 1
    je .one
    mov di, [px_cgrp]
    mov bp, di
    shl bp, 1
    shl bp, 1
    or di, 0x4000
    call OSAPI_GFX_BLITP
    jnc .out
    mov [px_rx0], ax                ; refused: packed, through BLIT4
    mov [px_rw], cx
    mov [px_ry0], bx
    add dx, bx
    dec dx
    mov [px_ryend], dx
    mov byte [px_cdep], 4
    mov byte [px_useP], 0
    mov ax, bx
.band:
    cmp ax, [px_ryend]
    jg .out
    mov [px_rby], ax
    mov cx, [px_ryend]
    sub cx, ax
    inc cx
    cmp cx, PX_BANDH
    jbe .n
    mov cx, PX_BANDH
.n:
    mov [px_rbn], cx
    mov bx, px_vthm
    mov [px_cbv], bx
    call px_rband
    mov ax, [px_rby]
    add ax, [px_rbn]
    jmp short .band
.one:
    mov ax, (CBLACK << 8) | CWHITE
    call OSAPI_GFX_BLIT1_PEN
    mov ax, [px_vthm + VW_IX]
    mov bp, [px_cgrp]
    call OSAPI_GFX_BLIT1
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_navrect - the canvas's view as a rect in the thumbnail: AX/BX/CX/DX,
; CF = 1 when the canvas shows the whole picture (no frame). Preserves the
; rest
px_navrect:
    push si
    mov ax, [px_vcan + VW_DW]
    cmp ax, [px_cw]
    ja .some
    mov ax, [px_vcan + VW_DH]
    cmp ax, [px_ch]
    ja .some
    stc
    pop si
    ret
.some:
    mov ax, [px_cvx1]               ; the canvas's corners as picture pixels
    sub ax, [px_vcan + VW_IX]
    jge .a
    xor ax, ax
.a:
    mov si, px_vcan
    call px_s2t                     ; ...in the thumbnail
    add ax, [px_vthm + VW_IX]
    push ax
    mov ax, [px_cvx2]
    sub ax, [px_vcan + VW_IX]
    cmp ax, [px_vcan + VW_DW]
    jl .c
    mov ax, [px_vcan + VW_DW]
    dec ax
.c:
    call px_s2t
    add ax, [px_vthm + VW_IX]
    push ax
    mov ax, [px_midy1]
    sub ax, [px_vcan + VW_IY]
    jge .b
    xor ax, ax
.b:
    call px_s2tv
    add ax, [px_vthm + VW_IY]
    push ax
    mov ax, [px_midy2]
    sub ax, [px_vcan + VW_IY]
    cmp ax, [px_vcan + VW_DH]
    jl .d
    mov ax, [px_vcan + VW_DH]
    dec ax
.d:
    call px_s2tv
    add ax, [px_vthm + VW_IY]
    mov dx, ax
    pop bx
    pop cx
    pop ax
    clc
    pop si
    ret

; px_s2t / px_s2tv - AX = a canvas pixel's offset into the picture across
; (down): AX = the same point's offset into the thumbnail. Preserve the rest
px_s2t:
    push bx
    push cx
    push dx
    mov cx, ax
    mov ax, [px_vcan + VW_HS]
    mov dx, [px_vcan + VW_HS + 2]
    call px_mulx                    ; BX:DX:AX = master x, 16.16
    mov ax, dx                      ; ...x 2^16 / the thumbnail's step
    mov dx, bx
    mov bx, [px_vthm + VW_HS]
    mov cx, [px_vthm + VW_HS + 2]
    call px_div32
    pop dx
    pop cx
    pop bx
    ret
px_s2tv:
    push bx
    push cx
    push dx
    mov cx, ax
    mov ax, [px_vcan + VW_VS]
    mov dx, [px_vcan + VW_VS + 2]
    call px_mulx
    mov ax, dx
    mov dx, bx
    mov bx, [px_vthm + VW_VS]
    mov cx, [px_vthm + VW_VS + 2]
    call px_div32
    pop dx
    pop cx
    pop bx
    ret

; px_navframe_on / _off - the frame drawn, or taken back off (XOR both
; ways), when the Navigator's picture is up. Lock held. Preserve all
px_navframe_on:
    cmp byte [px_nfon], 0
    jne .ret
    cmp byte [px_thok], 0
    je .ret
    test byte [px_pvis], 1
    jz .ret
    cmp word [px_pbhw], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    call px_navrect
    jc .no
    call OSAPI_GFX_XOR_RECT
    sub ax, [px_cx0]                ; kept from the content's corner: a
    sub cx, [px_cx0]                ; window that moved took the frame with
    sub bx, [px_cy0]                ; it (SPEC.md 11.96.12)
    sub dx, [px_cy0]
    mov [px_nfr], ax
    mov [px_nfr + 2], bx
    mov [px_nfr + 4], cx
    mov [px_nfr + 6], dx
    mov byte [px_nfon], 1
.no:
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret
px_navframe_off:
    cmp byte [px_nfon], 0
    je .ret
    test byte [px_pvis], 1          ; the Navigator is not laid out (the
    jz .gone                        ; panels hidden, the compact column on
    cmp word [px_pbhw], 0           ; another panel): what was there has
    jne .xor                        ; been drawn over, frame and all, so
.gone:                              ; there is nothing to take back - an
    mov byte [px_nfon], 0           ; XOR now would land on the canvas
    ret
.xor:
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_nfr]
    mov bx, [px_nfr + 2]
    mov cx, [px_nfr + 4]
    mov dx, [px_nfr + 6]
    add ax, [px_cx0]
    add cx, [px_cx0]
    add bx, [px_cy0]
    add dx, [px_cy0]
    call OSAPI_GFX_XOR_RECT
    mov byte [px_nfon], 0
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; px_navframe_sync - the frame where the view is now: left alone when it
; is there already (no XOR at all), else off and on. Preserves all
px_navframe_sync:
    push ax
    push bx
    push cx
    push dx
    cmp byte [px_nfon], 0
    je .on
    cmp byte [px_thok], 0
    je .move
    test byte [px_pvis], 1
    jz .move
    call px_navrect
    jc .move
    sub ax, [px_cx0]
    sub cx, [px_cx0]
    sub bx, [px_cy0]
    sub dx, [px_cy0]
    cmp ax, [px_nfr]
    jne .move
    cmp bx, [px_nfr + 2]
    jne .move
    cmp cx, [px_nfr + 4]
    jne .move
    cmp dx, [px_nfr + 6]
    je .out
.move:
    call px_navframe_off
.on:
    call px_navframe_on
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_navpan - CX, DX = a point in the Navigator's picture: the canvas
; centred on it. Lock held, layout done
px_navpan:
    push ax
    push bx
    push cx
    push dx
    cmp byte [px_thok], 0
    je .out
    mov ax, cx                      ; the thumbnail point -> the canvas's
    sub ax, [px_vthm + VW_IX]       ; offset that puts it at the centre
    jl .out
    mov bx, dx
    sub bx, [px_vthm + VW_IY]
    jl .out
    mul word [px_vcan + VW_DW]
    div word [px_vthm + VW_DW]      ; the canvas column of the point
    mov cx, [px_cw]
    shr cx, 1
    sub cx, ax
    mov ax, bx
    mul word [px_vcan + VW_DH]
    div word [px_vthm + VW_DH]
    mov bx, [px_ch]
    shr bx, 1
    sub bx, ax
    mov ax, cx
    call px_panto
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE HISTOGRAM (SPEC.md 106.12)
; =============================================================================

; px_hstats - [px_bins] folded from [px_hist] through the palette for
; channel [px_hchan] (0 luma, 1 R, 2 G, 3 B), and Mean, Std Dev, Min, Max
; from them into [px_hmean] [px_hsd] [px_hmin] [px_hmax] (SPEC.md 106.12).
; Preserves all
px_hstats:
    push ax                         ; the shown picture's decoder part folds
    push bx                         ; them (pxplan.inc's pl_stats, through
    push cx                         ; PXV_PLANS with CL = 1: SPEC.md 106.25 -
    push di                         ; the resident's room), fetched if it
    mov al, [px_cur + PXR_FMT]      ; must be, as px_plans' is
    call px_fpart
    call px_kneed
    mov bl, PXV_PLANS
    mov cl, 1
    mov di, px_k
    call px_pcall
    pop di
    pop cx
    pop bx
    pop ax
    ret

; px_hgraph - the graph in the well AX/BX/CX/DX (its interior): one column a
; pixel, each the sum of its share of the 256 bins, scaled to the tallest,
; composed into ONE 1bpp band (os88gfx.inc) and put up with one BLIT1 (SPEC.md
; 106.12). Lock held. Preserves all
px_hgraph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [px_hok], 0            ; nothing counted yet: the well's grey,
    jne .aln                        ; as the Navigator's is
    call px_wellfill
    jmp .out
.aln:
    push ax                         ; BLIT1 wants x on the byte grid: the
    add ax, 7                       ; columns before it are paper
    and ax, 0xFFF8
    pop si
    cmp si, ax
    je .al
    push cx
    push ax
    mov cx, ax
    dec cx
    mov ax, si
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop ax
    pop cx
.al:
    mov [px_gx1], ax
    mov [px_gy1], bx
    sub cx, ax
    inc cx
    jle .bare
    cmp cx, PX_HBW
    jbe .w
    mov cx, PX_HBW
.w:
    mov [px_gw], cx
    sub dx, bx
    inc dx
    jle .bare
    cmp dx, PX_HBH
    jbe .h
    mov dx, PX_HBH
.h:
    mov [px_gh], dx
    mov [gfxe_bx0], ax              ; the band: the well's interior...
    mov [gfxe_by0], bx
    mov [gfxe_bw], cx
    mov [gfxe_bh], dx
    mov ax, 0xFFFF                  ; ...on lit paper
    call gfxe_bclear
    cmp byte [px_hok], 0
    je .put
    ; each column's sum: bins [c x 256 / gw, (c + 1) x 256 / gw)
    mov word [px_gmax], 0
    mov word [px_gmax + 2], 0
    xor si, si                      ; SI = the column
.c1:
    call px_gcol                    ; DX:AX = its sum
    mov di, si
    shl di, 1
    shl di, 1
    mov [px_gsum + di], ax
    mov [px_gsum + di + 2], dx
    cmp dx, [px_gmax + 2]
    jb .c1n
    ja .c1m
    cmp ax, [px_gmax]
    jbe .c1n
.c1m:
    mov [px_gmax], ax
    mov [px_gmax + 2], dx
.c1n:
    inc si
    cmp si, [px_gw]
    jb .c1
    mov ax, [px_gmax]
    or ax, [px_gmax + 2]
    jz .put
    xor si, si
.c2:
    mov di, si                      ; the bar: sum x gh / max, rounded up
    shl di, 1
    shl di, 1
    mov ax, [px_gsum + di]
    mov dx, [px_gsum + di + 2]
    mov cx, [px_gh]
    call px_mulx
    mov [px_n0], ax
    mov [px_n1], dx
    mov [px_n2], bx
    mov ax, [px_gmax]
    mov [px_dv0], ax
    mov ax, [px_gmax + 2]
    mov [px_dv1], ax
    call px_div
    mov cx, [px_n0]
    or ax, dx
    jz .ex
    inc cx
.ex:
    cmp cx, [px_gh]
    jbe .bar
    mov cx, [px_gh]
.bar:
    jcxz .c2n                       ; ink the column's bottom CX rows
    mov bx, [px_gh]
    sub bx, cx                      ; BX = the bar's top row
    mov ax, si
    mov dl, 0x80
    push cx
    mov cl, al
    and cl, 7
    shr dl, cl
    not dl                          ; DL = the AND mask
    pop cx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov di, ax                      ; DI = the byte in a row
    mov ax, bx
    mov bl, PX_HBST
    mul bl
    add di, ax
    add di, px_hband
.ink:
    and [di], dl
    add di, PX_HBST
    loop .ink
.c2n:
    inc si
    cmp si, [px_gw]
    jb .c2
.put:
    mov ax, (CBLACK << 8) | CWHITE  ; paper lit (white), ink dark (black)
    cmp byte [px_col], 0            ; the colour face draws the channel in its
    je .pen                         ; own colour (SPEC.md 106.16)
    mov bl, [px_hchan]
    xor bh, bh
    mov ah, [px_hink + bx]
.pen:
    call OSAPI_GFX_BLIT1_PEN
    call gfxe_bput
    jmp short .out
.bare:
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

px_hink:    db CDGRAY, CRED, CGREEN, CBLUE  ; Luminosity, Red, Green, Blue

; px_gcol - SI = a column: DX:AX = the sum of its bins. Preserves all else
px_gcol:
    push bx
    push cx
    push di
    push es
    call px_htail                   ; the bins, in the picture's tail
    mov ax, si                      ; first = c x 256 / gw
    mov cl, 8
    shl ax, cl
    xor dx, dx
    div word [px_gw]
    mov bx, ax
    mov ax, si
    inc ax
    shl ax, cl
    xor dx, dx
    div word [px_gw]
    mov cx, ax                      ; last + 1
    xor ax, ax
    xor dx, dx
.l:
    cmp bx, cx
    jae .out
    mov di, bx
    shl di, 1
    shl di, 1
    add ax, [es:PXH_BINS + di]
    adc dx, [es:PXH_BINS + di + 2]
    inc bx
    jmp short .l
.out:
    pop es
    pop di
    pop cx
    pop bx
    ret
