; =============================================================================
; os8088 - cpwkit.inc
;
; The wide Control Panel's widget kit (SPEC.md 134.5): the drawing procs every
; wide page draws with, %included by cpwide.inc into CPWIDE.DRV's image, so
; the wave-2 pages that join that image (SPEC.md 134.8, 134.9, 134.11) draw
; with these and not with copies. Prefix `cpk_` for a widget, `cpw_` for the
; four primitives the chrome shares with them.
;
; THE COMMON CONTRACT, which SPEC.md 134.5 pins per proc:
; - every coordinate is CONTENT-relative (the window's content box: the
;   working area's CPW_W_OX/OY), so a page never adds an origin;
; - ES = the asset claim (cpw_ev's), DS = KERNEL_SEG, the gfx lock held;
; - every register is preserved but a documented output, and [gfx_color] is
;   left wherever the last primitive put it;
; - text is ONE opaque font_run (SPEC.md 6.1) and nothing writes a pixel
;   twice: a band with text in it is laid as the spans AROUND the text;
; - a label a widget takes is an IMAGE string (CS); text from elsewhere goes
;   through cpw_run or cpk_text, whose BL names the segment - 0 this image,
;   1 the kernel's (cp_sbuf: what CPE_GATE stages), 2 the asset claim
;   (CPWIDE.DAT's catalog, the working area).
; =============================================================================

section .modv                   ; CPWIDE.DRV's image, like the file that
                                ; includes this one (and .text again at
                                ; its foot, SPEC.md 1's section rule)

CPW_ID0     equ 0xC000          ; a wide control's arm (os88ui_arm) is this
                                ; and up; the compact pages' ids are bytes
                                ; and a driver page's are its own small ones

CPK_BANDH   equ 12              ; a group box's title band
CPK_TABH    equ 14              ; a tab strip's height
CPK_SBW     equ 12              ; a table's scroll bar (os88ui_sbar)
CPK_OK      equ 0               ; cpk_badge's three states, in CWI16_OK's
CPK_ERR     equ 1               ; order (cpwfmt.inc)
CPK_WARN    equ 2

; cpk_table's block: 16 bytes in the working area (ES), named by BX
CPKT_X      equ 0               ; dw the table's left, content-relative
CPKT_Y      equ 2               ; dw ...its top
CPKT_W      equ 4               ; dw ...its width, the scroll bar's included
CPKT_RH     equ 6               ; dw a row's pitch
CPKT_NV     equ 8               ; dw rows that fit
CPKT_N      equ 10              ; dw rows there are
CPKT_TOP    equ 12              ; dw the first row shown
CPKT_ROW    equ 14              ; dw the page's row painter, a near proc in
                                ;    this image: in AX = the row, BL = its
                                ;    ground (CWHITE, or CBLUE for the
                                ;    selection), CX/DX = its left/top, DI =
                                ;    its width (no bar); paints the whole
                                ;    row opaque, keeps every register
CPKT_SEL    equ 16              ; dw the selected row, 0FFFFh none
CPKT_SIZE   equ 18

; the kit's scratch in the working area (CPW_W_KIT, 12 bytes)
CPK_S_X     equ CPW_W_KIT + 0
CPK_S_Y     equ CPW_W_KIT + 2
CPK_S_R     equ CPW_W_KIT + 4
CPK_S_B     equ CPW_W_KIT + 6
CPK_S_I     equ CPW_W_KIT + 8

; =============================================================================
; The four primitives
; =============================================================================

; -----------------------------------------------------------------------------
; cpw_fill - a filled rect in the pen
; in:  AX/BX = left/top, CX/DX = right/bottom, INCLUSIVE, content-relative;
;      [gfx_color] = the pen
; out: nothing; every register kept
; -----------------------------------------------------------------------------
cpw_fill:
    push ax
    push bx
    push cx
    push dx
    add ax, [es:CPW_W_OX]
    add cx, [es:CPW_W_OX]
    add bx, [es:CPW_W_OY]
    add dx, [es:CPW_W_OY]
    call KERNEL_SEG:cw_gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpw_run - one opaque text run (SPEC.md 6.1)
; in:  CX/DX = x/y, content-relative; SI = a NUL string in segment BL (0 CS,
;      1 DS, 2 ES); AL = ink, AH = paper
; out: CX = the x just past the run (8 a character: the font is a fixed
;      8x8); every other register kept
;
; OSAPI_FONT_RUN's cell reads the string through the CALLER's DS (an X cell,
; SPEC.md 20.3), so a run is drawn from wherever its string is by setting DS
; for the one call - no copy into a kernel buffer, whatever its length.
; -----------------------------------------------------------------------------
cpw_run:
    push bx
    push dx
    push si
    push ds
    push cx
    or bl, bl
    jnz .n0
    push cs
    pop ds
    jmp short .go
.n0:
    cmp bl, 2
    jne .go
    push es
    pop ds
.go:
    xor bx, bx                  ; BX = its length
.m:
    cmp byte [si+bx], 0
    je .md
    inc bx
    jmp short .m
.md:
    add cx, [es:CPW_W_OX]
    add dx, [es:CPW_W_OY]
    call KERNEL_SEG:apic_font_run
    pop cx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add cx, bx
    pop ds
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cpw_runp - cpw_run, and its paper on to the content's right edge
; in:  cpw_run's. out: cpw_run's (CX past the TEXT); [gfx_color] = the paper
;
; A line that replaces a longer one erases what is left of it in the same
; pass: the run and the fill beside it are disjoint (CLAUDE.md, Performance
; rule 2). A line that must stop short of the edge is cpk_text's, with a
; width.
; -----------------------------------------------------------------------------
cpw_runp:
    call cpw_run
    push ax
    push bx
    push cx
    push dx
    mov [gfx_color], ah
    mov ax, cx
    mov bx, dx
    mov cx, [es:CPW_W_CW]
    dec cx
    add dx, 7
    cmp ax, cx
    jg .r
    call cpw_fill
.r:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpw_icon - one icon out of CPWIDE.DAT, its transparent pixels the ground
; in:  AL = the icon's index (CWI16_* or CWI32_*), AH = 0 a 16x16 / 1 a
;      32x32; BL = the ground colour; CX/DX = x/y, content-relative
; out: nothing; every register kept
;
; The file holds each icon's pixels and its mask (SPEC.md 134.6: one bit a
; pixel, 1 = opaque, a transparent pixel's nibble 0). The pixels are copied
; into the working area's staging buffer with every transparent nibble made
; the ground, and the buffer goes down in ONE OSAPI_GFX_BLIT4 - so an icon on
; a selected (blue) row and on a white one is the same 128 or 512 bytes, and
; nothing is drawn under it first.
; -----------------------------------------------------------------------------
cpw_icon:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push cx                     ; the position, for the blit
    push dx
    mov bh, bl                  ; BH = the ground, BL = the ground << 4
    mov cl, 4
    shl bl, cl
    or ah, ah
    jnz .big
    mov dx, CWD_I16_SZ
    mul dl                      ; (AH = 0: a byte multiply, 160 x index)
    add ax, [es:CPW_WSZ + CWD_H_OFF16]
    mov si, ax
    mov cx, CWD_I16_PIX / 8     ; CX = (mask word, eight bytes) groups
    mov bp, CWD_I16_PIX
    jmp short .have
.big:
    xor ah, ah
    mov dx, CWD_I32_SZ
    mul dx
    add ax, [es:CPW_WSZ + CWD_H_OFF32]
    mov si, ax
    mov cx, CWD_I32_PIX / 8
    mov bp, CWD_I32_PIX
.have:
    add si, CPW_WSZ             ; SI = its pixels in the claim
    add bp, si                  ; BP = its masks
    mov di, CPW_W_ICON
    push cx                     ; (the group count says which size)
.g:
    mov dx, [es:bp]             ; sixteen pixels' mask, bit 15 the leftmost
    inc bp
    inc bp
    mov ah, 8
.b:
    mov al, [es:si]
    inc si
    shl dx, 1
    jc .hi
    and al, 0x0F
    or al, bl
.hi:
    shl dx, 1
    jc .lo
    and al, 0xF0
    or al, bh
.lo:
    mov [es:di], al
    inc di
    dec ah
    jnz .b
    loop .g
    pop cx                      ; the group count...
    pop bx                      ; (y)
    pop ax                      ; (x)
    mov bp, CWD_I16_STR         ; ...BP = the stride, CX = DX = the size
    cmp cx, CWD_I16_PIX / 8
    mov cx, 16
    je .sz
    mov bp, CWD_I32_STR
    mov cx, 32
.sz:
    mov dx, cx
    add ax, [es:CPW_W_OX]
    add bx, [es:CPW_W_OY]
    mov si, CPW_W_ICON          ; ES:SI = the staged pixels
    call KERNEL_SEG:api_gfx_blit4
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The widgets (SPEC.md 134.5)
; =============================================================================

; -----------------------------------------------------------------------------
; cpk_group - a group box: a title band and a 1px frame round its body
; in:  AX/BX/CX/DX = the box, inclusive, content-relative; SI = its title
;      (an image string)
; out: nothing; every register kept
;
; The band is light grey with black text, CPK_BANDH rows, laid as the five
; spans round the run; the frame is three black rules (left, right, bottom)
; under it. The body is NOT filled: it is the page's ground, laid by whoever
; laid the page's.
; -----------------------------------------------------------------------------
cpk_group:
    push ax
    push bx
    push cx
    push dx
    mov [es:CPK_S_X], ax
    mov [es:CPK_S_Y], bx
    mov [es:CPK_S_R], cx
    mov [es:CPK_S_B], dx
    mov byte [gfx_color], CLGRAY
    mov dx, bx                  ; the band's two rows above the text...
    inc dx
    call cpw_fill
    add bx, CPK_BANDH - 2       ; ...and two below it
    mov dx, bx
    inc dx
    call cpw_fill
    mov bx, [es:CPK_S_Y]        ; ...the left margin
    inc bx
    inc bx
    mov cx, ax
    add cx, 3
    mov dx, bx
    add dx, 7
    call cpw_fill
    mov cx, ax                  ; ...the title
    add cx, 4
    mov dx, bx
    mov ax, (CLGRAY << 8) | CBLACK
    push bx
    xor bl, bl
    call cpw_run                ; CX = past it
    pop bx
    mov byte [gfx_color], CLGRAY    ; (a renderer may leave the pen at the
    mov ax, cx                  ; ink: VESA.DRV's does) ...and the rest of
    mov cx, [es:CPK_S_R]        ; its row
    add dx, 7
    cmp ax, cx
    jg .fr
    call cpw_fill
.fr:
    mov byte [gfx_color], CBLACK    ; the frame: left, right, bottom
    mov ax, [es:CPK_S_X]
    mov bx, [es:CPK_S_Y]
    add bx, CPK_BANDH
    mov cx, ax
    mov dx, [es:CPK_S_B]
    call cpw_fill
    mov ax, [es:CPK_S_R]
    mov cx, ax
    call cpw_fill
    mov bx, dx
    mov ax, [es:CPK_S_X]
    call cpw_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpk_button - a push button: SPEC.md 20.5.1.3's, through os88ui_kbtn
; in:  AX/BX/CX/DX = its rect, inclusive, content-relative; SI = its label
;      (an image string); DI = OS88UI_DIS / OS88UI_DEF / 0, and the pressed
;      look is the caller's to ask for by passing OS88UI_DEF's neighbour bits
;      exactly as os88ui_btn takes them
; out: nothing; every register kept
;
; The ONE button drawer (SPEC.md 13.8.3): the rect is staged in os88ui_krect,
; absolute, and the label in cp_sbuf, where os88ui_btn reads it through DS.
; A press on it is the page's to arm with an id from CPW_ID0 up.
; -----------------------------------------------------------------------------
cpk_button:
    push ax
    push bx
    push cx
    push dx
    push si
    add ax, [es:CPW_W_OX]
    add cx, [es:CPW_W_OX]
    add bx, [es:CPW_W_OY]
    add dx, [es:CPW_W_OY]
    mov [os88ui_krect], ax
    mov [os88ui_krect+2], bx
    mov [os88ui_krect+4], cx
    mov [os88ui_krect+6], dx
    call cpw_stage              ; SI = cp_sbuf
    mov bx, os88ui_krect
    CPWC os88ui_kbtn
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpk_inrect - is a point inside a rect of the image's?
; in:  SI -> 4 words in this image (CS): left, top, right, bottom, inclusive,
;      content-relative; CX/DX = the point, content-relative
; out: CF = 0 inside, CF = 1 outside; every register kept
; -----------------------------------------------------------------------------
cpk_inrect:
    cmp cx, [cs:si]
    jl .no
    cmp dx, [cs:si+2]
    jl .no
    cmp cx, [cs:si+4]
    jg .no
    cmp dx, [cs:si+6]
    jg .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cpk_tabs - a tab (or filter) strip: one tab a label, the chosen one barred
; in:  CX/DX = the strip's left/top, content-relative; SI = its labels (image
;      strings, one after another, an empty one ending them); AL = the chosen
; out: CX = the x just past the last tab; every other register kept
;
; A tab is its label with 4px of its ground either side and CPK_TABH rows:
; the chosen one white on blue (the list's selection, SPEC.md 134.3), the
; rest black on light grey, a 1px white gap between them. Each tab is the run
; and its four spans, so a tab that changes state is redrawn with nothing
; under it: cpk_tabs again, whole - six to fifteen primitives.
; -----------------------------------------------------------------------------
cpk_tabs:
    push ax
    push bx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the chosen, AL = the tab being drawn
    xor al, al
.t:
    cmp byte [cs:si], 0
    je .out
    mov di, (CLGRAY << 8) | CBLACK  ; DI = paper : ink
    cmp al, ah
    jne .c
    mov di, (CBLUE << 8) | CWHITE
.c:
    push ax
    mov [es:CPK_S_X], cx
    mov [es:CPK_S_Y], dx
    mov ax, di
    mov [gfx_color], ah
    mov ax, cx                  ; the left margin, full height
    mov bx, dx
    add cx, 3
    add dx, CPK_TABH - 1
    call cpw_fill
    mov cx, ax                  ; the label, three rows down
    add cx, 4
    mov dx, bx
    add dx, 3
    mov ax, di
    push bx
    xor bl, bl
    call cpw_run                ; CX = past it
    pop bx
    push si                     ; (SI past the label: the next one)
.sk:
    inc si
    cmp byte [cs:si-1], 0
    jne .sk
    mov [es:CPK_S_I], si
    pop si
    mov ax, di                  ; the rows above and below the label
    mov [gfx_color], ah
    mov ax, [es:CPK_S_X]
    add ax, 4
    dec cx                      ; (CX = its last column)
    mov dx, bx
    inc dx
    inc dx
    call cpw_fill
    add bx, 11
    mov dx, bx
    add dx, CPK_TABH - 12
    call cpw_fill
    mov ax, cx                  ; the right margin, full height
    inc ax
    mov bx, [es:CPK_S_Y]
    add cx, 4
    mov dx, bx
    add dx, CPK_TABH - 1
    call cpw_fill
    mov byte [gfx_color], CWHITE    ; ...and the gap after it
    inc cx
    mov ax, cx
    call cpw_fill
    inc cx                      ; CX = the next tab's left
    mov dx, bx
    mov si, [es:CPK_S_I]
    pop ax
    inc al
    jmp .t
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpk_tabhit - which tab of a strip is at this x?
; in:  SI = the strip's labels (cpk_tabs'); CX = the x minus the strip's left
; out: CF = 0 and AL = the tab; CF = 1 none (a gap counts as the tab before
;      it). Every other register kept
; -----------------------------------------------------------------------------
cpk_tabhit:
    push bx
    push cx
    push si
    xor al, al
    or cx, cx
    jl .no
.t:
    cmp byte [cs:si], 0
    je .no
    mov bx, 9                   ; 4 + 4 margins and the 1px gap, and 8 a
.w:                             ; character (cpk_tabs' own widths)
    inc si
    cmp byte [cs:si-1], 0
    je .wd
    add bx, 8
    jmp short .w
.wd:
    cmp cx, bx
    jl .yes
    sub cx, bx
    inc al
    jmp short .t
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cpk_table - a scrolling table: its visible rows, and os88ui_sbar beside them
; in:  BX = its block (CPKT_*, in the working area: ES). out: nothing; every
;      register kept
;
; The rows are the PAGE's: the block names its row painter, which paints one
; whole row opaque at the origin it is handed, so the table lays no ground
; under a row and the scroll bar is the kernel's own (SPEC.md 13.10) - the
; block is copied into os88ui_ksb, absolute, for the one call. A row past the
; last is the ground (white). cpk_tbrows is the rows alone, for a scroll.
; -----------------------------------------------------------------------------
cpk_table:
    call cpk_tbrows
    push bx
    call cpk_tbsb               ; BX = os88ui_ksb, filled
    CPWC os88ui_sbar
    pop bx
    ret

cpk_tbrows:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    mov cx, [es:bx+CPKT_X]
    mov dx, [es:bx+CPKT_Y]
    mov di, [es:bx+CPKT_W]
    sub di, CPK_SBW             ; DI = a row's width
    mov ax, [es:bx+CPKT_TOP]
    mov bp, [es:bx+CPKT_NV]
.r:
    or bp, bp
    jz .out
    cmp ax, [es:bx+CPKT_N]
    jae .blank
    push si
    push bx
    mov si, [es:bx+CPKT_ROW]
    cmp ax, [es:bx+CPKT_SEL]
    mov bl, CWHITE
    jne .g
    mov bl, CBLUE               ; the selection bar is the row's ground
.g:
    call si                     ; the page's row, whole and opaque
    pop bx
    pop si
    jmp short .next
.blank:
    push ax                     ; past the last row: the ground
    push cx
    push dx
    push si
    mov si, [es:bx+CPKT_RH]
    mov byte [gfx_color], CWHITE
    push bx
    mov ax, cx
    mov bx, dx
    add cx, di
    dec cx
    add dx, si
    dec dx
    call cpw_fill
    pop bx
    pop si
    pop dx
    pop cx
    pop ax
.next:
    add dx, [es:bx+CPKT_RH]
    inc ax
    dec bp
    jmp short .r
.out:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cpk_tbsb - a table block's scroll bar into os88ui_ksb, absolute
; in:  BX = the block. out: BX = os88ui_ksb; every other register kept
cpk_tbsb:
    push ax
    push dx
    mov ax, [es:bx+CPKT_X]
    add ax, [es:bx+CPKT_W]
    add ax, [es:CPW_W_OX]
    dec ax
    mov [os88ui_ksb+4], ax      ; x2: the table's right edge
    sub ax, CPK_SBW - 1
    mov [os88ui_ksb], ax        ; x1
    mov ax, [es:bx+CPKT_Y]
    add ax, [es:CPW_W_OY]
    mov [os88ui_ksb+2], ax      ; y1
    push ax
    mov ax, [es:bx+CPKT_RH]
    mul word [es:bx+CPKT_NV]
    pop dx
    add ax, dx
    dec ax
    mov [os88ui_ksb+6], ax      ; y2: the last visible row's last
    mov ax, [es:bx+CPKT_N]
    mov [os88ui_ksb+8], ax      ; total
    mov ax, [es:bx+CPKT_NV]
    mov [os88ui_ksb+10], ax     ; fit
    mov ax, [es:bx+CPKT_TOP]
    mov [os88ui_ksb+12], ax     ; pos
    mov bx, os88ui_ksb
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cpk_tbclick - a press in a table
; in:  BX = its block; CX/DX = the point, content-relative
; out: CF = 1 not in the table. CF = 0 and AX = the row pressed, or AX =
;      0FFFFh when the press was the scroll bar's - which has been acted on:
;      an arrow is one row, the track one page, the view redrawn by its rows
;      and os88ui_sbmove (SPEC.md 13.10.4: three calls, not the bar's sixteen)
; Every other register kept
;
; The bar acts on the PRESS, as every scroll bar in the system does; a row
; is the page's to act on (and to arm, from CPW_ID0, when it fires on the
; release).
; -----------------------------------------------------------------------------
cpk_tbclick:
    push bx
    push cx
    push dx
    push si
    mov si, bx                  ; SI = the block
    mov ax, cx
    sub ax, [es:si+CPKT_X]
    jl .no
    cmp ax, [es:si+CPKT_W]
    jge .no
    mov ax, dx
    sub ax, [es:si+CPKT_Y]
    jl .no
    push dx
    xor dx, dx
    div word [es:si+CPKT_RH]    ; AX = the visible row
    pop dx
    cmp ax, [es:si+CPKT_NV]
    jae .no
    push ax
    mov ax, cx
    sub ax, [es:si+CPKT_X]
    add ax, CPK_SBW
    cmp ax, [es:si+CPKT_W]
    pop ax
    jge .bar
    add ax, [es:si+CPKT_TOP]    ; a row
    cmp ax, [es:si+CPKT_N]
    jae .no                     ; past the last: the ground
    clc
    jmp short .out
.bar:
    call cpk_tbsb               ; BX = os88ui_ksb
    add cx, [es:CPW_W_OX]       ; os88ui_sbhit takes the point absolute
    add dx, [es:CPW_W_OY]
    CPWC os88ui_sbhit           ; AL = the part
    mov bx, si
    mov dx, [es:bx+CPKT_TOP]    ; DX = the old top
    mov cx, 1
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    mov cx, [es:bx+CPKT_NV]
    cmp al, OS88UI_SBPGUP
    je .up
    cmp al, OS88UI_SBPGDN
    jne .done                   ; the thumb, or nothing: no drag here
.dn:
    mov ax, [es:bx+CPKT_N]      ; the last top there is
    sub ax, [es:bx+CPKT_NV]
    jl .done
    add cx, dx
    cmp cx, ax
    jle .set
    mov cx, ax
    jmp short .set
.up:
    mov ax, dx
    sub ax, cx
    jge .u
    xor ax, ax
.u:
    mov cx, ax
.set:
    cmp cx, dx
    je .done
    mov [es:bx+CPKT_TOP], cx
    call cpk_tbrows
    call cpk_tbsb
    mov ax, dx                  ; os88ui_sbmove: AX = the pos it was at
    CPWC os88ui_sbmove
.done:
    mov ax, 0FFFFh
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; cpk_badge - a status badge: the 16x16 mark, and a label after it
; in:  CX/DX = its left/top, content-relative; AL = CPK_OK / CPK_ERR /
;      CPK_WARN; AH = the ground; SI = the label (an image string), or 0
; out: CX = the x past it; every other register kept
;
; The label is on the mark's middle rows (DX + 4) and its rows above and
; below are the CALLER's: a badge sits in a row that has laid them already
; or lays them round it (the table's row painters do).
; -----------------------------------------------------------------------------
cpk_badge:
    push ax
    push bx
    push dx
    mov bl, ah                  ; BL = the ground
    push ax
    add al, CWI16_OK
    xor ah, ah
    call cpw_icon
    pop ax
    add cx, 16
    or si, si
    jz .r
    mov [gfx_color], bl         ; the 4px between the mark and the label
    push ax
    push cx
    mov ax, cx
    add cx, 3
    add dx, 4
    mov bx, dx
    add dx, 7
    call cpw_fill
    sub dx, 7
    pop cx
    pop ax
    add cx, 4
    mov al, CBLACK              ; black on the ground (AH)
    xor bl, bl
    call cpw_run
.r:
    pop dx
    pop bx
    pop ax
    ret

; cpk_ds - DS = the segment BL names (0 this image, 1 the kernel's - DS as it
; is - 2 the claim). The caller pushes DS first and pops it after. Keeps all
cpk_ds:
    or bl, bl
    jnz .n
    push cs
    pop ds
    ret
.n:
    cmp bl, 2
    jne .r
    push es
    pop ds
.r:
    ret

; -----------------------------------------------------------------------------
; cpk_text - long text: a string set as lines of a given width
; in:  CX/DX = the first line's left/top, content-relative; SI = a NUL string
;      in segment BL (0 CS, 1 DS, 2 ES); DI = the width in characters (1 to
;      95); AL = ink, AH = paper
; out: DX = the top of the row under the last line (DX + 10 a line); every
;      other register kept
;
; A line breaks at the last space that fits, or hard at the width when a word
; is longer than it. Every line is padded with spaces to the full width, so
; it is ONE opaque run that replaces whatever text was there before - a
; shorter description over a longer one leaves nothing behind - and the 2px
; between lines are the caller's ground.
; -----------------------------------------------------------------------------
cpk_text:
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    mov [es:CPK_S_X], cx
    mov [es:CPK_S_R], ax
    mov bp, di                  ; BP = the width
.line:
    push ds
    call cpk_ds
    cmp byte [si], 0
    je .end
    mov di, CPW_W_STR
    xor cx, cx                  ; CX = characters in this line
    mov word [es:CPK_S_B], 0    ; the last space's index + 1 (0 = none)
.c:
    mov al, [si]
    or al, al
    jz .eos
    cmp cx, bp
    jae .full
    mov [es:di], al
    cmp al, ' '
    jne .ns
    mov ax, cx
    inc ax
    mov [es:CPK_S_B], ax
.ns:
    inc si
    inc di
    inc cx
    jmp short .c
.full:                          ; more to come: break at the last space
    mov ax, [es:CPK_S_B]
    or ax, ax
    jz .eos                     ; none: a hard break here
    sub cx, ax                  ; the characters copied past the space...
    sub si, cx                  ; ...are the next line's
    mov cx, ax
    dec cx                      ; this line ends before the space
    mov di, CPW_W_STR
    add di, cx
.eos:
    pop ds
.pad:
    cmp cx, bp
    jae .z
    mov byte [es:di], ' '
    inc di
    inc cx
    jmp short .pad
.z:
    mov byte [es:di], 0
    push si
    push bx
    mov si, CPW_W_STR
    mov bl, 2
    mov cx, [es:CPK_S_X]
    mov ax, [es:CPK_S_R]
    call cpw_run
    pop bx
    pop si
    add dx, 10
    jmp short .line
.end:
    pop ds
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
section .text
