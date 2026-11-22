; =============================================================================
; os8088 - cpw_drv.inc
;
; The wide Drivers page (SPEC.md 134.8): %included by cpwide.inc into
; CPWIDE.DRV's image, in `.modv`. Prefix `cpd_`. It draws NATIVELY with the
; widget kit (cpwkit.inc) and changes state only through CPE_GATE's driver ops
; (CPG_DRVON / OFF / WANT / TTL / STAT / MEM, SPEC.md 134.4), so the compact
; page and this one run ONE body for a load, an unload and a want bit.
;
; WHAT IT SHOWS (SPEC.md 134.8): category filter tabs (All, then the
; categories with a row on this kernel), a scrolling table of every drv_tab row
; - category icon, name, status badge, memory, Start at boot - and a DETAILS
; box for the selected row with Load, Unload, Settings... and Ports....
;
; ITS STATE is in the working area's page block (CPW_W_PAGE, 80 bytes) and
; NOTHING is kept between visits except the filter and the selection, which
; are bytes there. Everything is content-relative; ES = the asset claim.
;
; REDRAW ONLY WHAT CHANGED (CLAUDE.md, Performance): a row is painted opaque,
; in the spans AROUND its text, so a selection change is two rows and the
; details, a want bit one row, a load one row, the details, the status line and
; (only when its length moved) the page list.
; =============================================================================

section .modv                   ; CPWIDE.DRV's image, like the file that includes
                                ; this one (os88ovlchk reads each file's own sections)

; --- the state, in the working area's page block (80 bytes, SPEC.md 134.5) ----
CPD_TB      equ CPW_W_PAGE              ; the table's block (CPKT_SIZE = 18)
CPD_FLT     equ CPD_TB + CPKT_SIZE      ; db the filter: 0 All, else a CWC_*
CPD_ON      equ CPD_FLT + 1             ; db 1 = this page is on the screen
CPD_SELR    equ CPD_FLT + 2             ; db the selected drv_tab row, 0FFh none
CPD_MASK    equ CPD_FLT + 3             ; db one bit per CWC_* that has a row
CPD_GT      equ CPD_FLT + 4             ; dw the DETAILS box's top
CPD_C1      equ CPD_FLT + 6             ; dw the badge column's x, row-relative
CPD_C2      equ CPD_FLT + 8             ; dw the memory column's
CPD_C3      equ CPD_FLT + 10            ; dw the Start-at-boot column's
CPD_ROWS    equ CPD_FLT + 12            ; db x DRV_MAX: the drv_tab rows listed
; (kernel.asm, after driver.inc, checks CPD_ROWS + DRV_MAX against the block)
CPW_D_ON    equ CPD_ON                  ; (cpwide.inc's name for it)

; --- the geometry, content-relative (SPEC.md 134.8) ---------------------------
CPD_XL      equ CPW_PX + 8              ; the table's, the tabs' and the box's left
CPD_TABY    equ CPW_BY + 4              ; the tab strip's top
CPD_HDRY    equ CPD_TABY + CPK_TABH + 2 ; the column heads' top
CPD_HDRH    equ 12
CPD_ROWY    equ CPD_HDRY + CPD_HDRH     ; the first row's top
CPD_RH      equ 18                      ; a row's pitch
CPD_DH      equ 112                     ; the DETAILS box's height
CPD_BTY     equ CPD_DH - 22             ; its buttons' top, box-relative
CPD_TXC     equ 52                      ; its text column, box-relative
CPD_ROWMIN  equ 465                     ; a row's width at 616x408 (no bar)

; a row's columns, row-relative: 2 icon, 20 name (22 cells), 196 badge
; (12 cells), 292 memory (8), 356 Start at boot (13). The window's width above
; 616 is shared out among the badge, memory and boot columns in whole cells.
CPD_NX      equ 20
CPD_NW      equ 22
CPD_SX      equ CPD_NX + CPD_NW * 8
CPD_MX      equ CPD_SX + 96
CPD_BXC     equ CPD_MX + 64
CPD_CKX     equ 46                      ; the check box, in the boot column

CPD_GRN     equ 10                      ; the badges' papers (EGA indices)
CPD_RED     equ 12
CPD_YEL     equ 14

; -----------------------------------------------------------------------------
; cpd_geo - the table's block and the columns, from the content's size
; in:  nothing. out: nothing; every register kept
; -----------------------------------------------------------------------------
cpd_geo:
    push ax
    push bx
    push cx
    push dx
    mov word [es:CPD_TB + CPKT_X], CPD_XL
    mov word [es:CPD_TB + CPKT_Y], CPD_ROWY
    mov word [es:CPD_TB + CPKT_RH], CPD_RH
    mov word [es:CPD_TB + CPKT_ROW], cpd_row
    mov ax, [es:CPW_W_CW]
    sub ax, CPD_XL + 8
    mov [es:CPD_TB + CPKT_W], ax
    sub ax, CPK_SBW + CPD_ROWMIN    ; the width over 616's, in 24 px steps: a
    jge .e                          ; third of it, in cells, to each column
    xor ax, ax
.e:
    xor dx, dx
    mov cx, 24
    div cx
    mov cl, 3
    shl ax, cl                      ; AX = 8 px a cell of it
    mov bx, CPD_SX
    add bx, ax
    mov [es:CPD_C1], bx
    mov bx, CPD_MX
    add bx, ax
    add bx, ax
    mov [es:CPD_C2], bx
    mov bx, CPD_BXC
    add bx, ax
    add bx, ax
    add bx, ax
    mov [es:CPD_C3], bx
    call cpw_sty
    sub ax, 4 + CPD_DH              ; the box: its bottom 3 rows above the page's
    mov [es:CPD_GT], ax
    sub ax, 3 + CPD_ROWY            ; ...and the rows that fit above it
    xor dx, dx
    mov cx, CPD_RH
    div cx
    mov [es:CPD_TB + CPKT_NV], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_cat - a drv_tab row's category. in: AL = a row. out: AL = its CWC_*
; (CWC_UNK for a file the catalog does not know); every other register kept
; -----------------------------------------------------------------------------
cpd_cat:
    push di
    call cpw_cat
    mov al, CWC_UNK
    jc .r
    mov al, [es:di + CWD_C_CAT]
.r:
    pop di
    ret

; -----------------------------------------------------------------------------
; cpd_build - the table's rows: the drv_tab rows the filter keeps, the
;             categories present, the selection's place in the list
; in:  [CPD_FLT], [CPD_SELR]. out: [CPD_ROWS], N, SEL, TOP clamped, [CPD_MASK];
;      every register kept
; A selection the filter dropped (or none yet) becomes the first row shown.
; -----------------------------------------------------------------------------
cpd_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    xor dx, dx                  ; DL = the categories present
    xor cx, cx                  ; CL = the drv_tab row, CH = rows listed
    mov bp, CPD_ROWS
    mov bx, drv_tab
    mov si, 0FFFFh              ; SI = the selected row's place
.r:
    cmp word [bx + DRVR_FILE], 0
    je .n
    mov al, cl
    call cpd_cat
    mov ah, al
    or al, al
    jz .m
    push cx
    mov cl, al
    mov al, 1
    shl al, cl
    pop cx
    or dl, al
.m:
    mov al, [es:CPD_FLT]
    or al, al
    jz .take
    cmp al, ah
    jne .n
.take:
    mov [es:bp], cl
    inc bp
    cmp cl, [es:CPD_SELR]
    jne .ns
    mov al, ch
    xor ah, ah
    mov si, ax
.ns:
    inc ch
.n:
    add bx, DRVR_SIZE
    inc cl
    cmp cl, DRV_MAX
    jb .r
    mov [es:CPD_MASK], dl
    mov al, ch
    xor ah, ah
    mov [es:CPD_TB + CPKT_N], ax
    cmp si, 0FFFFh
    jne .sel
    or ax, ax
    jz .none
    xor si, si                  ; the first row shown
    mov al, [es:CPD_ROWS]
    mov [es:CPD_SELR], al
    jmp short .sel
.none:
    mov byte [es:CPD_SELR], 0FFh
.sel:
    mov [es:CPD_TB + CPKT_SEL], si
    mov ax, [es:CPD_TB + CPKT_N]
    sub ax, [es:CPD_TB + CPKT_NV]   ; the last top there is
    jge .t
    xor ax, ax
.t:
    cmp [es:CPD_TB + CPKT_TOP], ax
    jle .o
    mov [es:CPD_TB + CPKT_TOP], ax
.o:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_paint - the whole page: ground, tabs, column heads, table, DETAILS
; in:  nothing. out: nothing
; The page area's ground goes down in ONE fill (the compact pages' own
; precedent, cp_page); every widget after it is opaque, so a later redraw of
; one of them never needs the ground again.
; -----------------------------------------------------------------------------
cpd_paint:
    mov byte [es:CPD_ON], 1
    call cpd_geo
    call cpd_build
    call cpw_sty
    mov dx, ax
    dec dx
    dec dx
    mov byte [gfx_color], CWHITE
    mov ax, CPW_PX
    mov bx, CPW_BY
    mov cx, [es:CPW_W_CW]
    dec cx
    call cpw_fill
    call cpd_tabs
    call cpd_hdr
    mov bx, CPD_TB
    call cpk_table
    mov ax, CPD_XL                  ; the DETAILS box: its band and frame
    mov bx, [es:CPD_GT]
    mov cx, [es:CPD_TB + CPKT_W]
    add cx, CPD_XL - 1
    mov dx, bx
    add dx, CPD_DH - 1
    mov si, cpd_s_det
    call cpk_group
    jmp cpd_det

; -----------------------------------------------------------------------------
; cpd_tabs - the filter strip: All, then each category a row has, the chosen
;            one barred. Each tab is cpk_tabs' with the one label (and the
;            empty string that ends it) so the strip can be any subset.
; -----------------------------------------------------------------------------
cpd_tabs:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, CPD_XL
    mov dx, CPD_TABY
    mov si, cpd_s_all
    mov al, 1
    cmp byte [es:CPD_FLT], 0
    jne .a
    xor al, al
.a:
    call cpk_tabs                   ; CX = past it
    mov bl, 1                       ; BL = the category
    mov bh, [es:CPD_MASK]
.c:
    push cx
    mov cl, bl
    mov al, 1
    shl al, cl
    pop cx
    test bh, al
    jz .nx
    mov al, 1
    cmp bl, [es:CPD_FLT]
    jne .d
    xor al, al
.d:
    push bx
    xor bh, bh
    shl bx, 1
    mov si, [cs:bx + cpd_cn - 2]
    pop bx
    call cpk_tabs
.nx:
    inc bl
    cmp bl, CWC_N
    jb .c
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_tabhit - which filter is at this x?
; in:  AX = x, content-relative. out: CF = 0 and AL = the filter (0 All, or a
;      CWC_*); CF = 1 none. Every other register kept
; -----------------------------------------------------------------------------
cpd_tabhit:
    push bx
    push cx
    push dx
    push si
    sub ax, CPD_XL
    js .no
    mov dx, ax
    mov si, cpd_s_all
    xor al, al
    mov ah, [es:CPD_MASK]
.t:
    mov bx, 9                       ; cpk_tabs' widths: 4 + 4 + 1, 8 a cell
.w:
    mov cl, [cs:si]
    inc si
    or cl, cl
    jz .wd
    add bx, 8
    jmp short .w
.wd:
    cmp dx, bx
    jl .yes
    sub dx, bx
.nx:
    inc al
    cmp al, CWC_N
    jae .no
    mov cl, al
    mov ch, 1
    shl ch, cl
    test ah, ch
    jz .nx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov si, [cs:bx + cpd_cn - 2]
    jmp short .t
.yes:
    clc
    jmp short .o
.no:
    stc
.o:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cpd_hdr - the column heads: one run on a light grey band
; -----------------------------------------------------------------------------
cpd_hdr:
    mov di, CPW_W_STR
    mov ax, [es:CPD_C1]
    sub ax, CPD_SX
    mov cl, 3
    shr ax, cl
    mov dx, ax                  ; DX = the cells of air in each gap
    xor bl, bl
    mov si, cpd_h0
    mov cx, 22
    call cpd_cpyn
    call .air
    mov si, cpd_h1
    mov cx, 12
    call cpd_cpyn
    call .air
    mov si, cpd_h2
    mov cx, 8
    call cpd_cpyn
    call .air
    mov si, cpd_h3
    mov cx, 13
    call cpd_cpyn
    mov ax, [es:CPD_TB + CPKT_W]    ; ...and spaces to the table's right
    sub ax, CPD_NX
    mov cl, 3
    shr ax, cl
    cmp ax, 95
    jbe .l
    mov ax, 95
.l:
    add ax, CPW_W_STR
    call cpd_pad
    mov byte [gfx_color], CLGRAY
    mov ax, CPD_XL                  ; the band round the run
    mov bx, CPD_HDRY
    mov cx, [es:CPD_TB + CPKT_W]
    add cx, CPD_XL - 1
    mov dx, bx
    inc dx
    call cpw_fill
    add bx, CPD_HDRH - 2
    add dx, CPD_HDRH - 2
    call cpw_fill
    sub bx, CPD_HDRH - 4
    mov dx, bx
    add dx, 7
    mov cx, CPD_XL + CPD_NX - 1
    call cpw_fill
    mov cx, CPD_XL + CPD_NX
    mov dx, bx
    mov si, CPW_W_STR
    mov ax, (CLGRAY << 8) | CBLACK
    mov bl, 2
    call cpw_run                    ; CX = past it
    mov ax, cx
    mov cx, [es:CPD_TB + CPKT_W]
    add cx, CPD_XL - 1
    cmp ax, cx
    jg .r
    mov byte [gfx_color], CLGRAY
    mov bx, dx
    add dx, 7
    call cpw_fill
.r:
    ret
.air:                           ; DX spaces at ES:DI, and the next field's SI
    push cx
    mov cx, dx
    jcxz .a1
.a2:
    mov byte [es:di], ' '
    inc di
    loop .a2
.a1:
    pop cx
    ret

; cpd_cpyn - SI = a string in segment BL (0 CS, 1 DS, 2 ES): at most CX of its
; characters at ES:DI, DI advanced past them (no NUL written). Keeps the rest
; but SI
cpd_cpyn:
    push ax
    push ds
    call cpk_ds
.c:
    mov al, [si]
    or al, al
    jz .z
    mov [es:di], al
    inc si
    inc di
    loop .c
.z:
    pop ds
    pop ax
    ret

; cpd_pad - spaces at ES:DI up to the address in AX, and the NUL
cpd_pad:
.p:
    cmp di, ax
    jae .z
    mov byte [es:di], ' '
    inc di
    jmp short .p
.z:
    mov byte [es:di], 0
    ret

; -----------------------------------------------------------------------------
; The row
; -----------------------------------------------------------------------------

; cpd_g - [gfx_color] = the row's ground (CPK_S_B). Keeps all
cpd_g:
    push ax
    mov al, [es:CPK_S_B]
    mov [gfx_color], al
    pop ax
    ret

; cpd_ip - AL = the row's ink, AH = its ground. Keeps the rest
cpd_ip:
    mov ah, [es:CPK_S_B]
    mov al, CBLACK
    cmp ah, CBLUE
    jne .r
    mov al, CWHITE
.r:
    ret

; cpd_rfill - a fill in the pen, ROW-relative: AX/CX = x1/x2, BL/BH = y1/y2.
; Keeps every register
cpd_rfill:
    push ax
    push bx
    push cx
    push dx
    add ax, [es:CPK_S_X]
    add cx, [es:CPK_S_X]
    mov dl, bh
    xor dh, dh
    add dx, [es:CPK_S_Y]
    xor bh, bh
    add bx, [es:CPK_S_Y]
    call cpw_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cpd_gap - the ground over columns AX..CX (when there are any), rows 1-16 of
; the row. Keeps every register
cpd_gap:
    cmp cx, ax
    jl .r
    push bx
    mov bx, 0x1001
    call cpd_g
    call cpd_rfill
    pop bx
.r:
    ret

; cpd_rrun - a run at ROW-relative CX/DX: SI = the string in segment BL,
; AL = ink, AH = paper. Out: CX past it (cpw_run's); keeps the rest
cpd_rrun:
    add cx, [es:CPK_S_X]
    add dx, [es:CPK_S_Y]
    call cpw_run
    sub dx, [es:CPK_S_Y]
    ret

; cpd_stat - a row's badge state, drv_status's own reading of its live row:
; a reported reason outranks 'Loaded'. in: AL = a row. out: AL = 0 loaded,
; 1 off, 2 no hardware, 3 any other reason; every other register kept
cpd_stat:
    push bx
    mov bl, al
    xor bh, bh
    push cx
    mov cl, 4
    shl bx, cl
    pop cx
    mov al, [bx + drv_tab + DRVR_ERR]
    cmp al, DRVE_NERR
    jae .n
    or al, al
    jz .n
    cmp al, DRVE_HW
    mov al, 2
    je .r
    mov al, 3
    jmp short .r
.n:
    xor al, al
    cmp word [bx + drv_tab + DRVR_SEG], 0
    jne .r
    mov al, 1
.r:
    pop bx
    ret

; cpd_memstr - a row's memory figure, as the compact page prints it (SPEC.md
; 31.6.2): '~', the KB to the half, 'K', and '+' for a store it sizes
; in:  AL = a row, ES:DI = where. out: DI at the NUL; every other register kept
cpd_memstr:
    push ax
    push bx
    push si
    mov ah, CPG_DRVMEM
    call cpw_gate               ; AX = the half-KB word, DRVM_PLUS in bit 15
    mov bx, ax
    and ax, 0x7FFF
    jz .z
    mov byte [es:di], '~'
    inc di
    shr ax, 1
    sbb si, si                  ; SI = -1 for a half
    call cpw_utoa
    or si, si
    jz .k
    mov word [es:di], '.5'
    inc di
    inc di
.k:
    mov byte [es:di], 'K'
    inc di
    test bh, 0x80
    jz .z
    mov byte [es:di], '+'
    inc di
.z:
    mov byte [es:di], 0
    pop si
    pop bx
    pop ax
    ret

; cpd_blit12 - the 12x12 check box: SI = its 12 pattern words in the image,
; AX/BX = x/y content-relative. Expanded to 4bpp (set = black, clear = white)
; in the icon staging buffer and put down with ONE OSAPI_GFX_BLIT4. Keeps all
cpd_blit12:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ax
    push bx
    mov di, CPW_W_ICON
    mov bp, 12
.r:
    mov dx, [cs:si]
    inc si
    inc si
    mov cx, 6
.b:
    mov al, 0xFF
    shl dx, 1
    jnc .a
    and al, 0x0F
.a:
    shl dx, 1
    jnc .s
    and al, 0xF0
.s:
    mov [es:di], al
    inc di
    loop .b
    dec bp
    jnz .r
    pop bx
    pop ax
    add ax, [es:CPW_W_OX]
    add bx, [es:CPW_W_OY]
    mov cx, 12
    mov dx, cx
    mov bp, 6
    mov si, CPW_W_ICON
    call KERNEL_SEG:api_gfx_blit4
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_row - the table's row painter (CPKT_ROW, SPEC.md 134.5)
; in:  AX = the list's row, BL = its ground (CWHITE, CBLUE selected), CX/DX =
;      left/top, DI = the width without the bar. out: nothing; every register
;      kept
;
; The whole row, opaque, in the spans AROUND its runs: nothing is written
; twice. Row-relative: row 0 and 17 the ground; 1-16 the icon (2..17), the
; name (20), the badge (12 cells: its paper over rows 3-14, the label on 5-12),
; the memory, and the Start at boot check box (12x12 at rows 3-14).
; -----------------------------------------------------------------------------
cpd_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [es:CPK_S_X], cx
    mov [es:CPK_S_Y], dx
    dec di
    mov [es:CPK_S_R], di        ; the row's last column, row-relative
    mov si, ax
    mov al, [es:si + CPD_ROWS]
    mov [es:CPK_S_I], al        ; the drv_tab row
    xor bh, bh
    mov [es:CPK_S_B], bx        ; BL = the ground
    call cpd_g
    xor ax, ax                  ; --- rows 0 and 17, the left margin, the gap
    mov cx, [es:CPK_S_R]        ; between the icon and the name
    xor bx, bx
    call cpd_rfill
    mov bx, (CPD_RH - 1) * 0x101
    call cpd_rfill
    mov cx, 1
    mov bx, 0x1001
    call cpd_rfill
    mov ax, 18
    mov cx, 19
    call cpd_rfill
    mov al, [es:CPK_S_I]        ; --- the icon: the catalog's, or the gear
    call cpw_cat
    mov bp, di                  ; BP = the record, 0 = none
    mov al, CWI16_GEAR
    jnc .rec
    xor bp, bp
    jmp short .ic
.rec:
    mov al, [es:di + CWD_C_ICON]
    cmp al, CWI16_N
    jb .ic
    mov al, CWI16_GEAR
.ic:
    mov cx, [es:CPK_S_X]
    add cx, 2
    mov dx, [es:CPK_S_Y]
    inc dx
    xor ah, ah
    mov bl, [es:CPK_S_B]
    call cpw_icon
    mov di, CPW_W_STR           ; --- the name, 22 cells
    mov cx, CPD_NW
    or bp, bp
    jz .ttl
    lea si, [bp + CWD_C_LONG]
    mov bl, 2
    jmp short .nm
.ttl:
    mov al, [es:CPK_S_I]
    mov ah, CPG_DRVTTL
    call cpw_gate               ; SI = cp_sbuf
    mov bl, 1
.nm:
    call cpd_cpyn
    mov ax, CPW_W_STR + CPD_NW
    call cpd_pad
    call cpd_g
    mov ax, CPD_NX
    mov cx, CPD_SX - 1
    mov bx, 0x0401
    call cpd_rfill
    mov bx, 0x100D
    call cpd_rfill
    call cpd_ip
    mov cx, CPD_NX
    mov dx, 5
    mov si, CPW_W_STR
    mov bl, 2
    call cpd_rrun
    call cpd_g                  ; --- the badge
    mov ax, CPD_SX              ; (the air to it, when the window is wide)
    mov cx, [es:CPD_C1]
    dec cx
    call cpd_gap
    mov ax, [es:CPD_C1]
    mov cx, ax
    add cx, 95
    mov bx, 0x0201
    call cpd_rfill
    mov bx, 0x100F
    call cpd_rfill
    mov al, [es:CPK_S_I]
    call cpd_stat
    xor ah, ah
    mov si, ax
    mov dl, [cs:si + cpd_pc]    ; DL = its paper
    mov [gfx_color], dl
    mov ax, [es:CPD_C1]
    mov bx, 0x0403
    call cpd_rfill
    mov bx, 0x0E0D
    call cpd_rfill
    mov ax, si
    mov dh, 13
    mul dh                      ; AX = the label's offset (DL, the paper, kept)
    mov si, ax
    add si, cpd_pl
    mov ah, dl
    mov al, CBLACK
    mov cx, [es:CPD_C1]
    mov dx, 5
    xor bl, bl
    call cpd_rrun
    call cpd_g                  ; --- the memory
    mov ax, [es:CPD_C1]
    add ax, 96
    mov cx, [es:CPD_C2]
    dec cx
    call cpd_gap
    mov ax, [es:CPD_C2]
    mov cx, ax
    add cx, 63
    mov bx, 0x0401
    call cpd_rfill
    mov bx, 0x100D
    call cpd_rfill
    mov di, CPW_W_STR
    mov byte [es:di], ' '
    inc di
    mov al, [es:CPK_S_I]
    call cpd_memstr
    mov ax, CPW_W_STR + 8
    call cpd_pad
    call cpd_ip
    mov cx, [es:CPD_C2]
    mov dx, 5
    mov si, CPW_W_STR
    mov bl, 2
    call cpd_rrun
    call cpd_g                  ; --- the check box and the air round it
    mov ax, [es:CPD_C2]
    add ax, 64
    mov cx, [es:CPD_C3]
    add cx, CPD_CKX - 1
    call cpd_gap
    mov ax, [es:CPD_C3]
    add ax, CPD_CKX
    mov cx, ax
    add cx, 11
    mov bx, 0x0201
    call cpd_rfill
    mov bx, 0x100F
    call cpd_rfill
    mov bl, [es:CPK_S_I]        ; the box: checked when the row is WANTED
    xor bh, bh
    mov cl, 4
    shl bx, cl
    mov si, cpd_ck0
    cmp byte [bx + drv_tab + DRVR_WANT], 0
    je .ck
    mov si, cpd_ck1
.ck:
    add ax, [es:CPK_S_X]
    mov bx, [es:CPK_S_Y]
    add bx, 3
    call cpd_blit12
    mov ax, [es:CPD_C3]
    add ax, CPD_CKX + 12
    mov cx, [es:CPK_S_R]
    call cpd_gap
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_row1 - repaint one list row if it is showing
; in:  AX = the list's row (0FFFFh none). out: nothing; every register kept
; -----------------------------------------------------------------------------
cpd_row1:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cx, ax
    sub cx, [es:CPD_TB + CPKT_TOP]
    jl .o
    cmp cx, [es:CPD_TB + CPKT_NV]
    jge .o
    mov bl, CWHITE
    cmp ax, [es:CPD_TB + CPKT_SEL]
    jne .g
    mov bl, CBLUE
.g:
    push ax
    mov ax, cx
    mov dx, CPD_RH
    mul dx
    add ax, CPD_ROWY
    mov dx, ax
    pop ax
    mov cx, CPD_XL
    mov di, [es:CPD_TB + CPKT_W]
    sub di, CPK_SBW
    call cpd_row
.o:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpd_setsel - select list row AX, redrawing what changed: the row losing the
;              bar and the row gaining it (or the whole table, when the
;              selection scrolls it into view), and the DETAILS
; in:  AX = a list row, in range. out: nothing
; -----------------------------------------------------------------------------
cpd_setsel:
    mov si, [es:CPD_TB + CPKT_SEL]
    cmp ax, si
    je .r
    mov [es:CPD_TB + CPKT_SEL], ax
    mov bx, ax
    mov cl, [es:bx + CPD_ROWS]
    mov [es:CPD_SELR], cl
    mov cx, [es:CPD_TB + CPKT_TOP]
    cmp ax, cx
    jl .up
    add cx, [es:CPD_TB + CPKT_NV]
    cmp ax, cx
    jl .vis
    mov cx, ax                  ; scrolled: the table's last row is this one
    sub cx, [es:CPD_TB + CPKT_NV]
    inc cx
    jmp short .scr
.up:
    mov cx, ax
.scr:
    mov [es:CPD_TB + CPKT_TOP], cx
    mov bx, CPD_TB
    call cpk_table
    jmp short .det
.vis:
    call cpd_row1               ; the new one...
    mov ax, si
    call cpd_row1               ; ...and the one losing the bar
.det:
    jmp cpd_det
.r:
    ret

; -----------------------------------------------------------------------------
; cpd_det - the DETAILS body for the selected row: its 32x32 icon, long name,
;           file, description, status sentence, memory and the four buttons.
;           Every line is one opaque padded run (cpk_text), the buttons erase
;           their own interior, so nothing is cleared first
; in:  nothing. out: nothing
; -----------------------------------------------------------------------------
cpd_det:
    xor bp, bp                  ; BP = the catalog record, 0 = none
    mov al, [es:CPD_SELR]
    cmp al, 0FFh
    je .ic
    call cpw_cat
    jc .ic
    mov bp, di
.ic:
    mov al, CWI32_GEAR          ; --- the icon: the category's header icon
    or bp, bp
    jz .i2
    mov al, [es:bp + CWD_C_CAT]
    cmp al, CWC_N
    jb .i1
    xor al, al
.i1:
    xor ah, ah
    mov si, ax
    mov al, [cs:si + cpd_c32]
.i2:
    mov ah, 1
    mov bl, CWHITE
    mov cx, CPD_XL + 8
    mov dx, [es:CPD_GT]
    add dx, CPK_BANDH + 4
    call cpw_icon
    mov si, cpd_s_sp            ; --- the long name
    xor bl, bl
    cmp byte [es:CPD_SELR], 0FFh
    je .nm
    or bp, bp
    jz .tt
    lea si, [bp + CWD_C_LONG]
    mov bl, 2
    jmp short .nm
.tt:
    mov al, [es:CPD_SELR]
    mov ah, CPG_DRVTTL
    call cpw_gate               ; SI = cp_sbuf
    mov bl, 1
.nm:
    mov dx, CPK_BANDH + 4
    mov ax, (CWHITE << 8) | CBLACK
    call cpd_dl
    mov si, cpd_s_sp            ; --- the file
    xor bl, bl
    mov al, [es:CPD_SELR]
    cmp al, 0FFh
    je .fl
    xor ah, ah
    mov si, ax
    mov cl, 4
    shl si, cl
    mov si, [si + drv_tab + DRVR_FILE]
    mov bl, 1
.fl:
    mov dx, CPK_BANDH + 15
    mov ax, (CWHITE << 8) | CDGRAY
    call cpd_dl
    mov bx, CWD_C_D1            ; --- the description, two lines
    mov dx, CPK_BANDH + 29
    call .desc
    mov bx, CWD_C_D2
    mov dx, CPK_BANDH + 39
    call .desc
    mov si, cpd_s_sp            ; --- the status sentence
    xor bl, bl
    mov al, [es:CPD_SELR]
    cmp al, 0FFh
    je .st
    mov ah, CPG_DRVSTAT
    call cpw_gate               ; SI = cp_sbuf
    mov bl, 1
.st:
    mov dx, CPK_BANDH + 53
    mov ax, (CWHITE << 8) | CBLACK
    call cpd_dl
    mov di, CPW_W_ICON + 384    ; --- the memory, composed in the icon buffer
    xor bl, bl                  ; (cpk_text's own line is CPW_W_STR)
    mov si, cpd_s_mem
    mov cx, 8
    call cpd_cpyn
    mov al, [es:CPD_SELR]
    cmp al, 0FFh
    je .mm
    call cpd_memstr
.mm:
    mov byte [es:di], 0
    mov si, CPW_W_ICON + 384
    mov bl, 2
    mov dx, CPK_BANDH + 64
    mov ax, (CWHITE << 8) | CBLACK
    call cpd_dl
    xor al, al                  ; --- the buttons
.b:
    xor ah, ah
    call cpd_btn
    inc al
    cmp al, 4
    jb .b
    ret
.desc:                          ; BX = CWD_C_D1 / D2, DX = the line's top
    mov si, cpd_s_sp
    xor cl, cl
    or bp, bp
    jz .dk
    add bx, bp
    mov ax, [es:bx]
    or ax, ax
    jz .dk
    add ax, CPW_WSZ
    mov si, ax
    mov cl, 2
.dk:
    mov bl, cl
    mov ax, (CWHITE << 8) | CBLACK
    ; falls into cpd_dl

; cpd_dl - one DETAILS text line: DX = its top, box-relative; SI = the string
; in segment BL; AX = ink / paper. One opaque padded run (cpk_text). Keeps the
; rest
cpd_dl:
    push cx
    push di
    add dx, [es:CPD_GT]
    mov cx, CPD_XL + CPD_TXC
    call cpd_wc
    call cpk_text
    pop di
    pop cx
    ret

; cpd_wc - DI = the DETAILS text's width in cells (the box's width less the
; text column and a margin), at most 95. Keeps the rest
cpd_wc:
    mov di, [es:CPD_TB + CPKT_W]
    sub di, CPD_TXC + 8
    push cx
    mov cl, 3
    shr di, cl
    pop cx
    cmp di, 95
    jbe .r
    mov di, 95
.r:
    ret

; -----------------------------------------------------------------------------
; The buttons: Load, Unload, Settings..., Ports... (SPEC.md 134.8). Each is
; (x from the box's left, width, label); a press arms CPW_ID0 + its number and
; acts on the release, over it (SPEC.md 13.8.3)
; -----------------------------------------------------------------------------

; cpd_page - the page a loaded row's driver publishes (SPEC.md 31.9)
; in:  AL = a drv_tab row. out: CF = 0 and AL = that page's record; CF = 1
;      none. Every other register kept
cpd_page:
    push bx
    push cx
    mov bl, al
    xor bh, bh
    mov cl, 4
    shl bx, cl
    mov ch, [bx + drv_tab + DRVR_CLASS]
    xor cl, cl                  ; CL = the page's ordinal
.k:
    mov al, cl
    CPWC drv_cp_class_x         ; AL = its class, CF = 1 past the last
    jc .no
    cmp al, ch
    je .hit
    inc cl
    jmp short .k
.hit:
    mov al, cl
    add al, CP_ITEMS
    clc
    jmp short .o
.no:
    stc
.o:
    pop cx
    pop bx
    ret

; cpd_bflags - DI = OS88UI_DIS, or 0 when button AL is live: Load on a row
; that is down, Unload on one that is up or wanted (the compact page's "stop
; asking", SPEC.md 31.6), Settings... on a loaded driver that publishes a page,
; Ports... always. Keeps the rest
cpd_bflags:
    push ax
    push bx
    push cx
    mov di, OS88UI_DIS
    cmp al, 3
    je .en
    mov ah, [es:CPD_SELR]
    cmp ah, 0FFh
    je .r
    mov bl, ah
    xor bh, bh
    mov cl, 4
    shl bx, cl
    add bx, drv_tab
    cmp al, 1
    ja .set
    jb .load
    cmp word [bx + DRVR_SEG], 0
    jne .en
    cmp byte [bx + DRVR_WANT], 0
    jne .en
    jmp short .r
.load:
    cmp word [bx + DRVR_SEG], 0
    je .en
    jmp short .r
.set:
    cmp word [bx + DRVR_SEG], 0
    je .r
    mov al, ah
    call cpd_page
    jc .r
.en:
    xor di, di
.r:
    pop cx
    pop bx
    pop ax
    ret

; cpd_btn - draw button AL, AH = 1 pressed. Keeps every register
cpd_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, ah
    call cpd_bflags
    or di, di
    jnz .d
    or dl, dl
    jz .d
    mov di, OS88UI_DOWN
.d:
    mov bl, al
    xor bh, bh
    mov si, bx
    shl si, 1
    add si, bx
    shl si, 1                   ; SI x 6
    add si, cpd_bt
    mov ax, [cs:si]
    add ax, CPD_XL
    mov cx, ax
    add cx, [cs:si + 2]
    dec cx
    mov bx, [es:CPD_GT]
    add bx, CPD_BTY
    mov dx, bx
    add dx, 17
    mov si, [cs:si + 4]
    call cpk_button
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cpd_bhit - the button at a point. in: AX/BX = x/y, content-relative. out:
; CF = 0 and AL = the button; CF = 1 none (BX spent)
cpd_bhit:
    push cx
    push si
    mov cx, [es:CPD_GT]
    add cx, CPD_BTY
    sub bx, cx
    cmp bx, 18
    jae .no
    sub ax, CPD_XL
    mov si, cpd_bt
    xor cl, cl
.l:
    push ax
    sub ax, [cs:si]
    cmp ax, [cs:si + 2]
    pop ax
    jb .hit
    add si, 6
    inc cl
    cmp cl, 4
    jb .l
.no:
    stc
    jmp short .o
.hit:
    mov al, cl
    clc
.o:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; cpd_click - a press in the page area while this page shows
; in:  AX/BX = the point, content-relative. out: nothing
; A tab filters; the bar scrolls (cpk_tbclick acts on it); a row presses its
; check box (the want bit alone) or selects; a live button arms and draws down.
; -----------------------------------------------------------------------------
cpd_click:
    mov cx, ax
    mov dx, bx
    cmp dx, CPD_TABY
    jl .r
    cmp dx, CPD_TABY + CPK_TABH - 1
    jle .tabs
    cmp dx, CPD_ROWY
    jl .r
    cmp dx, [es:CPD_GT]
    jl .tbl
    call cpd_bhit
    jc .r
    xor ah, ah
    push ax
    call cpd_bflags
    pop ax
    or di, di
    jnz .r                      ; a greyed button: nothing arms
    mov si, ax
    add ax, CPW_ID0
    CPWC os88ui_arm
    mov ax, si
    mov ah, 1
    jmp cpd_btn                 ; ...and drawn down
.tabs:
    call cpd_tabhit
    jc .r
    cmp al, [es:CPD_FLT]
    je .r
    mov [es:CPD_FLT], al
    mov word [es:CPD_TB + CPKT_TOP], 0
    call cpd_build
    call cpd_tabs
    mov bx, CPD_TB
    call cpk_table
    jmp cpd_det
.tbl:
    mov bx, CPD_TB
    call cpk_tbclick
    jc .r
    cmp ax, 0FFFFh
    je .r                       ; the bar: acted on
    mov si, ax                  ; SI = the list row
    mov ax, cx
    sub ax, CPD_XL
    sub ax, [es:CPD_C3]
    cmp ax, 104                 ; the Start at boot column (unsigned: left of
    jae .sel                    ; it wraps)
    mov bx, si
    mov al, [es:bx + CPD_ROWS]
    mov bl, al
    xor bh, bh
    mov cl, 4
    shl bx, cl
    mov cl, [bx + drv_tab + DRVR_WANT]
    xor cl, 1
    mov ah, CPG_DRVWANT         ; the want bit ALONE
    call cpw_gate
    mov ax, si
    jmp cpd_row1
.sel:
    mov ax, si
    jmp cpd_setsel
.r:
    ret

; -----------------------------------------------------------------------------
; cpd_rel - the release of a wide control: AX = the id the press armed (spent
; by os88ui_fire), CX/DX = the SCREEN point. The button is drawn up FIRST; it
; acts only when the release is over it
; -----------------------------------------------------------------------------
cpd_rel:
    sub ax, CPW_ID0
    cmp ax, 4
    jae .r
    push ax
    xor ah, ah
    call cpd_btn
    mov ax, cx
    sub ax, [es:CPW_W_OX]
    mov bx, dx
    sub bx, [es:CPW_W_OY]
    call cpd_bhit
    pop bx
    jc .r
    cmp al, bl
    jne .r
    mov al, [es:CPD_SELR]
    cmp bl, 1
    jb .load
    je .unl
    cmp bl, 3
    je .ports
    call cpd_page               ; Settings...: that driver's page
    jc .r
    jmp cpw_select
.ports:                         ; Ports...: the compact view, hosted
    mov byte [cp_dview], 1
    mov byte [es:CPD_ON], 0
    jmp cpw_page
.load:
    mov ah, CPG_DRVON
    jmp short .go
.unl:
    mov ah, CPG_DRVOFF
.go:
    call cpw_gate
    mov al, [es:CPW_W_NL]       ; --- a driver coming up or going may have
    push ax                     ; brought or taken its page: the list, when
    call cpw_lbuild             ; its length moved
    pop ax
    cmp al, [es:CPW_W_NL]
    je .ns
    call cpw_list
.ns:
    xor al, al
    call cpw_status             ; the count, if it moved
    mov ax, [es:CPD_TB + CPKT_SEL]
    call cpd_row1
    jmp cpd_det
.r:
    ret

; -----------------------------------------------------------------------------
; cpd_key - Up / Down move the selection. AX = the key word. out: nothing
; -----------------------------------------------------------------------------
cpd_key:
    mov bx, [es:CPD_TB + CPKT_SEL]
    cmp bx, 0FFFFh
    je .r
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    jne .r
    inc bx
    cmp bx, [es:CPD_TB + CPKT_N]
    jae .r
    jmp short .go
.up:
    or bx, bx
    jz .r
    dec bx
.go:
    mov ax, bx
    jmp cpd_setsel
.r:
    ret

; --- the image's strings ------------------------------------------------------
cpd_s_det:  db 'DETAILS', 0
cpd_s_sp:   db ' ', 0
cpd_s_mem:  db 'Memory: ', 0
cpd_s_all:  db 'All', 0, 0
cpd_cn:     dw cpd_n1, cpd_n2, cpd_n3, cpd_n4, cpd_n5, cpd_n6, cpd_n7   ; CWC_DISP..SYS
cpd_n1:     db 'Display', 0, 0
cpd_n2:     db 'Sound', 0, 0
cpd_n3:     db 'Storage', 0, 0
cpd_n4:     db 'Network', 0, 0
cpd_n5:     db 'Input', 0, 0
cpd_n6:     db 'Memory', 0, 0
cpd_n7:     db 'System', 0, 0
; the column heads: exactly 22, 12, 8 and 13 characters
cpd_h0:     db 'Name                  ', 0
cpd_h1:     db '   Status   ', 0
cpd_h2:     db ' Memory ', 0
cpd_h3:     db 'Start at boot', 0
; the badges, 12 characters each, and their papers (loaded, off, no hardware,
; any other refusal)
cpd_pl:     db '   Loaded   ', 0
            db '    Off     ', 0
            db 'No hardware ', 0
            db '   Error    ', 0
cpd_pc:     db CPD_GRN, CLGRAY, CPD_RED, CPD_YEL
; a category's 32x32 header icon, by CWC_*
cpd_c32:    db CWI32_GEAR, CWI32_DISP, CWI32_SOUND, CWI32_HDD, CWI32_NET
            db CWI32_GEAR, CWI32_GEAR, CWI32_GEAR
; the buttons: x from the box's left, width, label
cpd_bt:     dw CPD_TXC,       56, cpd_b0
            dw CPD_TXC + 64,  64, cpd_b1
            dw CPD_TXC + 136, 104, cpd_b2
            dw CPD_TXC + 248, 80, cpd_b3
cpd_b0:     db 'Load', 0
cpd_b1:     db 'Unload', 0
cpd_b2:     db 'Settings...', 0
cpd_b3:     db 'Ports...', 0
; the check box: 12 rows of 12 bits, bit 15 leftmost
cpd_ck0:
    dw 0xFFF0, 0x8010, 0x8010, 0x8010, 0x8010, 0x8010
    dw 0x8010, 0x8010, 0x8010, 0x8010, 0x8010, 0xFFF0
cpd_ck1:
    dw 0xFFF0, 0x8010, 0x80D0, 0x8190, 0xA310, 0xB610
    dw 0x9E10, 0x8C10, 0x8810, 0x8010, 0x8010, 0xFFF0
