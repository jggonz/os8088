; Hercules card side (SPEC.md 102.7).  The CGA back end (cga.inc) is shared whole: the RAM boxes, the
; poses, the footprints, the sub-tick clock and the frame are the same code, because everything about
; the Hercules that is not a card ADDRESS is the CGA's:
;
;   * a game pixel is TWO card pixels wide (a pair: 00 black, 11 white, and 10 or 01 for the mid
;     grey), so 8 game pixels = 16 card pixels = 2 bytes - a tile, a pose and a glyph are exactly
;     as many bytes as on the CGA, and 8BBH.GFX has 8BBC.GFX's layout with pairs where the CGA has
;     inks (tools/excitebike_assets.py herc_tile);
;   * the card is 720 wide, so the row is 90 bytes = 45 tile columns: the picture is 360 game pixels
;     wide, and the sheared window's pitch is 45, not 40;
;   * four scan lines to a 6845 row (R9 = 3), so the banks are FOUR, 2000h apart:
;
;         byte address of (line l, world column x) = (l & 3) * 2000h + (90 * (l >> 2) + 2x) mod 8192
;
;     and the field (200 lines = 50 rows of every bank) is shown by programming R6 = 50 rows of the
;     87 the desktop mode displays (xh_crtc), R7 moved so the 200 lines sit in the middle of the
;     tube.  Without that the 37 rows below the field would show the sheared ring's other
;     rows - world columns 45 to 2,200 to the right, stale wherever the ring has been written.
;     A 50-row field also lengthens the blank the card writes hide in: ~98 scan lines from the
;     retrace start (~5.3 ms, ~25k clocks; a column is 19.2k and a rewrite 24k, so writes past
;     ~25k trail the beam - a field-run item) against the desktop mode's 16 and the CGA's 62.
;
; The dictionary strips are kept split by scan line MOD FOUR (the CGA's are split by parity),
; because the entering column is written a bank at a time: `movsw / add di,88`, 50 rows in
; all, the way cga.inc's chain writes 100.
;
; A VRAM word costs whatever the card's bus makes it; MartyPC's Hercules is the number this was
; measured against (PERFORMANCE.md Set 148, wave 6), and a real card's wait states are
; docs/FIELD-MACHINES.md's to measure.  The write discipline is the CGA's: touch the card as
; little as possible, in raster order, after the retrace starts.

; -----------------------------------------------------------------------------
; xh_crtc - after the mode set: 50 rows displayed and the vertical sync moved to match.
; The frame length (R4, R5) is untouched, so the field rate is the desktop mode's.
; The retrace to the first line is (R4 + 1 - R7) x 4 + R5 = (92 - 68) x 4 + 2 = 98 scan lines.
; -----------------------------------------------------------------------------
xh_crtc:
    push ax
    push dx
    mov dx, 3b4h
    mov ax, XH_R6 * 256 + 6
    out dx, ax
    mov ax, XH_R7 * 256 + 7
    out dx, ax
    pop dx
    pop ax
    ret

; a tile is 16 bytes, a word a line.  A four-bank strip lists bank 0's lines (0 and 4 of every
; tile) then bank 1's (1 and 5) ... : tile t is 4 bytes at 4t in each bank's list, %1 bytes apart.
%macro XH_TILE 1
    mov ax, [si]
    mov [es:di], ax
    mov ax, [si + 8]
    mov [es:di + 2], ax
    mov ax, [si + 2]
    mov [es:di + %1], ax
    mov ax, [si + 10]
    mov [es:di + %1 + 2], ax
    mov ax, [si + 4]
    mov [es:di + 2 * %1], ax
    mov ax, [si + 12]
    mov [es:di + 2 * %1 + 2], ax
    mov ax, [si + 6]
    mov [es:di + 3 * %1], ax
    mov ax, [si + 14]
    mov [es:di + 3 * %1 + 2], ax
%endmacro

; the dictionary claim's four-bank strips and the zero strip.  DS = the art claim, ES = the
; dictionary claim, BX = the band record, BP = the tile record, DI = 0.  DI = XC_ZERO + 16 on
; return.  A band column is 256 bytes (four banks of 64), a top column 128 (four of 32): the sizes
; of the CGA's parity strips, so cga.inc's layout constants stand.
xh_pstrips:
    mov dx, EXB_NCOLS
.bcol:
    mov cx, 16
.btile:
    mov al, [bx]
    inc bx
    xor ah, ah
    push cx
    mov cl, 4
    shl ax, cl
    pop cx
    mov si, ax
    add si, bp
    XH_TILE 64
    add di, 4
    loop .btile
    add di, 192                     ; past the other three banks' lists
    dec dx
    jnz .bcol
    mov bx, [cs:xb_recs + EXBR_TOP * 2]
    mov dx, XB_TOPW
.tcol:
    push bx
    mov cx, 8
.ttile:
    mov al, [bx]
    add bx, XB_TOPW
    xor ah, ah
    push cx
    mov cl, 4
    shl ax, cl
    pop cx
    mov si, ax
    add si, bp
    XH_TILE 32
    add di, 4
    loop .ttile
    pop bx
    inc bx
    add di, 96
    dec dx
    jnz .tcol
    xor ax, ax                      ; the zero strip (DI = XC_ZERO)
    mov cx, 8
    cld
    rep stosw
    ret

; -----------------------------------------------------------------------------
; the chain: 50 rows of `movsw / add di,88`, entered part way for n rows.  A row is
; 4 bytes of code, so n rows enter 4n bytes before the end.
; -----------------------------------------------------------------------------
xh_chain:
%rep XH_ROWS
    movsw
    add di, 88
%endrep
xh_chain_end:
    ret

; n rows (CX, 1..50) from SI (words, contiguous, in the dictionary claim) to the ring's
; row BX of [xc_bank] at this column ([xc_x2], [xc_rw] set by the caller).  DS = the dictionary
; claim, ES = the card.  The run is split where the ring wraps (a course of at most 1,637 columns
; never reaches it: 90 x 49 + 2 x 1,682 is 7,774 of 8,192 - it is kept for the day a track is longer).
xh_run:
    add ax, [cs:xc_x2]              ; AX = 90 x BX, passed by the caller as an immediate (no mul)
    mov di, ax
    cmp bx, [cs:xc_rw]
    jb .before
    and di, 1fffh                   ; wholly past the wrap: a - 8192
    add di, [cs:xc_bank]
    jmp .one
.before:
    mov dx, bx
    add dx, cx
    cmp dx, [cs:xc_rw]
    ja .split
    add di, [cs:xc_bank]
.one:
    mov ax, cx
    shl ax, 1
    shl ax, 1
    neg ax
    add ax, xh_chain_end
    call ax
    ret
.split:
    mov dx, [cs:xc_rw]
    sub dx, bx                      ; the rows before the wrap
    sub cx, dx
    push cx
    mov cx, dx
    add di, [cs:xc_bank]
    mov ax, cx
    shl ax, 1
    shl ax, 1
    neg ax
    add ax, xh_chain_end
    call ax
    pop cx
    mov ax, 90
    mul word [cs:xc_rw]
    add ax, [cs:xc_x2]
    and ax, 1fffh
    add ax, [cs:xc_bank]
    mov di, ax
    jmp .one

; the per-column constants: x2 = 2x, rw = the first row whose address wraps.  AX = x
xh_setx:
    mov [cs:xc_x], ax
    shl ax, 1
    mov [cs:xc_x2], ax
    mov ax, 8192 + 89
    sub ax, [cs:xc_x2]
    xor dx, dx
    mov cx, 90
    div cx
    mov [cs:xc_rw], ax
    ret

; one bank of the entering column: 16 top rows, 32 band rows, 2 HUD rows (black)
%macro XH_BANK 1
    mov word [cs:xc_bank], %1 * 2000h
    mov si, [cs:xc_tb]
    add si, %1 * 32
    xor bx, bx
    mov cx, 16
    xor ax, ax
    call xh_run
    mov si, [cs:xc_bb]
    add si, %1 * 64
    mov bx, 16
    mov cx, 32
    mov ax, 90 * 16
    call xh_run
    mov si, XC_ZERO
    mov bx, 48
    mov cx, 2
    mov ax, 90 * 48
    call xh_run
%endmacro

; -----------------------------------------------------------------------------
; xh_col - the whole entering column x (AX): 200 lines, four banks, the HUD's eight lines
; black.  DS = dictionary claim, ES = the card.
; -----------------------------------------------------------------------------
xh_col:
    call xh_setx
    mov bx, [cs:xc_x]
    and bx, XB_TOPW - 1
    mov cl, 7
    shl bx, cl
    add bx, XC_TOP
    mov [cs:xc_tb], bx
    mov bx, [cs:xc_x]
    mov al, [cs:bx + xb_cid]
    xor ah, ah
    mov cl, 8
    shl ax, cl
    add ax, XC_BAND
    mov [cs:xc_bb], ax
    XH_BANK 0
    XH_BANK 1
    XH_BANK 2
    XH_BANK 3
    ret

; the HUD's image lines 1..7 (line 0 is black over black): the ring offset of the row each is
; in, and its bank.  Line 192 + j is bank j & 3 of row 48 + j / 4.
xh_htab:
    dw 48 * 90, 2000h
    dw 48 * 90, 4000h
    dw 48 * 90, 6000h
    dw 49 * 90, 0000h
    dw 49 * 90, 2000h
    dw 49 * 90, 4000h
    dw 49 * 90, 6000h

; the whole HUD image, with the k-cell wipe (cga.inc's xc_hud, one bank a line).  ES = the card,
; DS = the package
xh_hud:
    mov ax, [xb_shownS]
    add ax, XH_HUDCOL
    sub ax, [xc_k]
    shl ax, 1
    mov [xc_x2], ax                 ; the first cell's byte offset in its row
    mov si, xc_hudimg
    mov ax, XC_MAXK
    sub ax, [xc_k]
    shl ax, 1
    add si, ax                      ; skip the pad the wipe does not need
    add si, (XB_HUDLEN + XC_MAXK) * 2   ; image line 0 is black over the black the world already
    mov bx, xh_htab                 ; has there: a card word costs ~4x a RAM one, so start at line 1
    mov bp, 7
.line:
    mov di, [bx]
    add di, [xc_x2]
    and di, 1fffh
    mov dx, [bx + 2]
    add bx, 4
    push bx
    push si
    mov cx, XB_HUDLEN
    add cx, [xc_k]
    call xc_putw                    ; (bank-agnostic: DX is the bank's offset)
    pop si
    pop bx
    add si, (XB_HUDLEN + XC_MAXK) * 2
    dec bp
    jnz .line
    ret

; the flagged cells only (the window did not move).  ES = the card, DS = the package
xh_hud_cells:
    xor bp, bp                      ; the list's cursor
.cell:
    cmp bp, [xc_hn]
    jae .done
    mov bx, [ds:xc_hlist + bp]       ; (BP addresses SS)
    add bp, 2
    mov byte [xc_hflag + bx], 0
    mov ax, [xb_shownS]
    add ax, XH_HUDCOL
    add ax, bx
    shl ax, 1
    mov [xc_x2], ax
    mov si, bx
    shl si, 1
    add si, xc_hudimg + XC_MAXK * 2 + (XB_HUDLEN + XC_MAXK) * 2   ; from image line 1
    mov bx, xh_htab
    mov cx, 7
.line:
    mov di, [bx]
    add di, [xc_x2]
    and di, 1fffh
    add di, [bx + 2]
    mov ax, [si]
    mov [es:di], ax
    add si, (XB_HUDLEN + XC_MAXK) * 2
    add bx, 4
    loop .line
    jmp .cell
.done:
    mov word [xc_hn], 0
    ret

; -----------------------------------------------------------------------------
; xh_xfer - the boxes to the card, a line at a time.  ES = the card, DS = the package.
; A box that does not reach the ring's end (all of them, on a course the compiler accepts)
; takes the fast path: `rep movsw` a line and one ADD to reach the next line's cell, the
; four banks unrolled so no bank arithmetic is done in the loop; one that does takes xc_putw.
; -----------------------------------------------------------------------------
xh_xfer:
    xor bp, bp
.box:
    cmp bp, [xc_nbx]
    jae .out
    test bp, bp
    jz .first
    call xb_clk_sample              ; (a box is ~4-7k clocks: every one but the first)
.first:
    mov [xc_bi], bp
    mov si, bp
    mov cl, 4
    shl si, cl
    add si, xc_bd
    mov ax, [si + 2]                ; uy0
    mov bx, ax
    shr ax, 1
    shr ax, 1                       ; its row
    mov cx, 90
    mul cx
    mov cx, [si]                    ; ux0
    shl cx, 1
    add ax, cx
    and ax, 1fffh
    mov di, ax                      ; the first line's ring offset
    mov dx, bx
    and dx, 3
    mov cl, 13
    shl dx, cl                      ; ...and its bank
    mov cx, [si + 4]
    mov [xc_hw], cx                 ; the width in words
    mov bp, [si + 6]                ; the height in lines
    mov si, [si + 8]                ; the buffer
    ; the last row's end: ring offset + 90 * (rows - 1) + 2W must fit
    mov ax, bx
    and ax, 3
    add ax, bp
    dec ax
    shr ax, 1
    shr ax, 1                       ; rows - 1
    mov cx, 90
    push dx
    mul cx
    pop dx
    add ax, di
    mov cx, [xc_hw]
    shl cx, 1
    add ax, cx
    cmp ax, 2000h
    ja .slow
    ; fast: BX = W words, CX = 2W
    mov bx, [xc_hw]
    mov ax, 2000h
    sub ax, cx
    mov [xc_s0], ax                 ; the step after a line in banks 0..2
    mov ax, 90 - 6000h
    sub ax, cx
    mov [xc_s1], ax                 ; ...and after one in bank 3: back to bank 0, the next row
    add di, dx                      ; the first line's real address
    test dx, dx
    jz .b0
    cmp dx, 2000h
    je .b1
    cmp dx, 4000h
    je .b2
    jmp .b3
.b0:
    mov cx, bx
    rep movsw
    add di, [xc_s0]
    dec bp
    jz .boxdone
.b1:
    mov cx, bx
    rep movsw
    add di, [xc_s0]
    dec bp
    jz .boxdone
.b2:
    mov cx, bx
    rep movsw
    add di, [xc_s0]
    dec bp
    jz .boxdone
.b3:
    mov cx, bx
    rep movsw
    add di, [xc_s1]
    dec bp
    jnz .b0
.boxdone:
    mov bp, [xc_bi]
    inc bp
    jmp .box
.slow:
.line:
    push di
    push dx
    mov cx, [xc_hw]
    call xc_putw                    ; SI runs on to the next row
    pop dx
    pop di
    add dx, 2000h
    and dx, 6000h
    jnz .same
    add di, 90
    and di, 1fffh
.same:
    dec bp
    jnz .line
    jmp .boxdone
.out:
    ret

; wait for the retrace to START, and count the wait as idle (xb_spin), not as work - cga.inc's
; xc_vr on the Hercules' status port: 3BAh bit 7 is LOW while the vertical sync is active.
xh_vr:
    call xb_clk_sample
    mov ax, [xb_subs]
    mov [xb_sp0], ax
    mov ax, XB_SUB
    sub ax, [xb_pitl]
    mov [xb_sp1], ax
    mov dx, 3bah
.spin:
    in al, dx
    test al, 80h
    jz .done
    push dx
    call xb_clk_sample
    pop dx
    mov ax, [xb_subs]
    sub ax, [xb_sp0]                ; the retrace comes every ~1.08 sub-ticks (50.4 Hz against 54.6),
    cmp ax, 3                       ; so three whole sub-ticks from ENTRY always hold one
    jb .spin
    mov byte [xc_to], 1             ; (unreachable in practice) no retrace at all: commit nothing
    jmp .fin
.done:
    mov byte [xc_to], 0
.fin:
    call xb_clk_sample
    mov ax, [xb_subs]
    sub ax, [xb_sp0]
    mov bx, XB_SUB
    sub bx, [xb_pitl]
    sub bx, [xb_sp1]
    call xb_span
    add [xb_spinl], ax
    adc [xb_spinh], dx
    ret
