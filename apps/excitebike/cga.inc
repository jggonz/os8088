; WAVE 6 (SPEC.md 102.7): this file is the RAM side of the CGA AND the Hercules engine.  Everything that is
; not a card address is shared - the boxes, the poses (compiled or interpreted), the footprints, the frame; the
; card side (an entering column, the HUD, the box writer, the retrace wait) is reached through the pointers
; xc_init sets ([xc_colp] ...): cga.inc's own for the CGA, herc.inc's for the Hercules.
; CGA back end (SPEC.md 102.1): 320 x 200 x 4, stride 80, two banks 2000h apart,
; ONE buffer.  The world is stored SHEARED as on the VGA, but the CGA's rows
; are character rows of two scan lines, so a cell is shared by (row r, column
; x) and (row r + 1, column x - 40) IN THE SAME BANK:
;
;   byte address of (line l, world column x) = (l & 1) * 2000h + (80 * (l >> 1) + 2x) mod 8192
;
; The 6845 start address is in WORDS and the card decodes 12 bits of it plus
; the scan line's low bit as A13, so the display of row r, column c is the
; cell (r, S + c) - the ring is 8,192 bytes and a 100-row window of 8,000
; leaves 192 spare, so nothing inside a window aliases.  Writing the entering
; column splits its dest at the wrap (at most once a column).
;
; A CGA VRAM WORD COSTS ~60 CLOCKS (`rep movsw` from RAM, MartyPC's 8-bit bus and
; its wait states), against ~17 for a RAM word, so the plan's cost model - which
; priced a VRAM byte at 18 clocks - was 3x light and the design follows the
; measurement: TOUCH THE CARD AS LITTLE AS POSSIBLE.
;
;   * a sprite is composed in a RAM BOX (the plan's design after all): the union of
;     the old and new footprints, filled from a line-ordered copy of the world's
;     strips, the pose AND/ORed over it in RAM (trimmed rows, ~70 bytes), and the
;     box written to the card ONCE, a line at a time.  The old sprite is erased by
;     the box covering it, so no erased state is ever visible and nothing is written
;     twice;
;   * the world's strips are also kept split by scan-line parity, because the
;     entering column is written a bank at a time (`movsw / add di,78`);
;   * everything that touches the card happens after the retrace starts, in raster
;     order, so the beam (2.4 ms from the retrace to line 0 - 38 lines of the 62-line blank - 63.5 us a line) is behind
;     the writer: the entering column, the HUD and the boxes finish ~5 ms after it
;     starts, when the beam is at about line 20 and the sprites are at line 50+;
;   * the HUD paints only its changed cells unless the window moved.

XC_BAND         equ 0                   ; parity strips: 256 bytes a column, even lines then odd
XC_TOP          equ EXB_NCOLS * 256     ; ...and 128 bytes a top column
XC_ZERO         equ XC_TOP + 64 * 128   ; 8 black words for the HUD lines
XC2_BAND        equ (XC_ZERO + 16 + 15) & ~15   ; the same strips in LINE order (16-byte tiles end to end), derived
XC2_TOP         equ XC2_BAND + EXB_NCOLS * 256
XC_DICT_END     equ XC2_TOP + 64 * 128
XC_DICT_KB      equ (XC_DICT_END + 1023) / 1024
%if XC_DICT_END > 65535
%error "CGA dictionary does not fit one 64KB window: too many band columns"
%endif
XC_MAXK         equ 2                   ; columns a frame may advance: 2 stays ahead of the beam
XC_BOXW         equ 8                   ; a box is at most 8 words x 32 lines = 512 bytes
XC_BOXH         equ 32
XC_NBOX         equ 8

; -----------------------------------------------------------------------------
; xc_init - the dictionary claim, built once from the CGA tile art
; -----------------------------------------------------------------------------
xc_init:
    SAVE
    mov ax, XC_DICT_KB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [xc_dseg], dx
    ; the card side, by adapter: the CGA's here, the Hercules' in herc.inc (SPEC.md 102.7).  Everything
    ; that is not a card address - the boxes, the poses, the footprints, the frame - is shared.
    mov word [xc_wb], 80
    mov word [xc_wc1], XB_COLS - 1
    mov word [xc_crtc], 3d4h
    mov word [xc_vseg], 0b800h
    mov word [xc_colp], xc_col
    mov word [xc_hudp], xc_hud
    mov word [xc_hudcp], xc_hud_cells
    mov word [xc_xferp], xc_xfer
    mov word [xc_vrp], xc_vr
    cmp byte [xb_herc], 0
    je .dicts
    mov word [xc_wb], 90
    mov word [xc_wc1], XH_COLS - 1
    mov word [xc_crtc], 3b4h
    mov ax, [xb_fsi + FSI_SEG]      ; (a HERCSEG= build relocates the card)
    mov [xc_vseg], ax
    mov word [xc_colp], xh_col
    mov word [xc_hudp], xh_hud
    mov word [xc_hudcp], xh_hud_cells
    mov word [xc_xferp], xh_xfer
    mov word [xc_vrp], xh_vr
.dicts:
    call xc_dicts
    RESTORE
    clc
    ret
.nomem:
    mov byte [xb_nomem], 1
.fail:
    RESTORE
    stc
    ret

; a race begins: nothing on the glass yet, so the first frame writes it all
xc_start:
    SAVE
    mov word [xc_S], 0ffffh
    mov byte [xc_hall], 1
    mov byte [xc_hany], 0
    mov word [xc_hn], 0
    mov byte [xc_to], 0
    mov byte [xc_after], 0
    xor ax, ax
    mov di, xc_fp
    mov cx, XB_MAXBIKES * XB_BIKE_SIZE / 2
    push ds
    pop es
    cld
    rep stosw                       ; no footprints
    RESTORE
    ret

; the menus: the start address home (the 6845 latches it at the next retrace)
xc_menu:
    SAVE
    mov dx, [xc_crtc]
    mov ax, 000ch
    out dx, ax
    mov ax, 000dh
    out dx, ax
    mov word [xb_shownS], 0
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    RESTORE
    ret

xc_free:
    push dx
    mov dx, [xc_dseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [xc_dseg], 0
.out:
    pop dx
    ret

; a tile is 16 bytes: a word a line.  A parity strip lists its EVEN lines' words,
; then its ODD lines': tile t puts lines 0,2,4,6 at 8t and lines 1,3,5,7 %1 on.
%macro XC_TILE 1
    mov ax, [si]
    mov [es:di], ax
    mov ax, [si + 4]
    mov [es:di + 2], ax
    mov ax, [si + 8]
    mov [es:di + 4], ax
    mov ax, [si + 12]
    mov [es:di + 6], ax
    mov ax, [si + 2]
    mov [es:di + %1], ax
    mov ax, [si + 6]
    mov [es:di + %1 + 2], ax
    mov ax, [si + 10]
    mov [es:di + %1 + 4], ax
    mov ax, [si + 14]
    mov [es:di + %1 + 6], ax
%endmacro

xc_dicts:
    SAVE
    mov es, [xc_dseg]
    push ds
    mov ds, [cs:xb_gfxseg]
    mov bx, [cs:xb_recs + EXBR_BAND * 2]
    mov bp, [cs:xb_recs + EXBR_TILES * 2]
    xor di, di
    cmp byte [cs:xb_herc], 0
    je .cgastrips
    call xh_pstrips                 ; the four-bank strips (herc.inc); DI = XC_ZERO + 16 after
    jmp .lineorder
.cgastrips:
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
    XC_TILE 128
    add di, 8
    loop .btile
    add di, 128                     ; past the odd list
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
    XC_TILE 64
    add di, 8
    loop .ttile
    pop bx
    inc bx
    add di, 64
    dec dx
    jnz .tcol
    xor ax, ax                      ; the zero strip (DI = XC_ZERO)
    mov cx, 8
    cld
    rep stosw
.lineorder:
    ; the line-ordered copy the boxes are filled from: a strip is its tiles end to end
    mov bp, [cs:xb_recs + EXBR_TILES * 2]
    mov bx, [cs:xb_recs + EXBR_BAND * 2]
    mov di, XC2_BAND
    mov dx, EXB_NCOLS * 16
.lb:
    mov al, [bx]
    inc bx
    xor ah, ah
    push cx
    mov cl, 4
    shl ax, cl
    pop cx
    mov si, ax
    add si, bp
    mov cx, 8
    rep movsw
    dec dx
    jnz .lb
    mov bx, [cs:xb_recs + EXBR_TOP * 2]
    mov dx, XB_TOPW
.lt:
    push bx
    mov ch, 8
.lr:
    mov al, [bx]
    add bx, XB_TOPW
    xor ah, ah
    push cx
    mov cl, 4
    shl ax, cl
    mov si, ax
    add si, bp
    mov cx, 8
    rep movsw
    pop cx
    dec ch
    jnz .lr
    pop bx
    inc bx
    dec dx
    jnz .lt
    pop ds
    RESTORE
    ret

; -----------------------------------------------------------------------------
; the chain: 100 rows of `movsw / add di,78`, entered part way for n rows
; -----------------------------------------------------------------------------
xc_chain:
%rep 100
    movsw
    add di, 78
%endrep
xc_chain_end:
    ret

; n rows (CX, 1..100) from SI (words, contiguous, in the dictionary claim) to the
; ring's row BX of [xc_bank] at this column ([xc_x2], [xc_rw] set by the caller).
; DS = the dictionary claim, ES = B800.  The run is split where the ring wraps.
xc_run:
    add ax, [cs:xc_x2]              ; AX = 80 x BX, passed by the caller as an immediate (no mul)
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
    add ax, xc_chain_end
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
    add ax, xc_chain_end
    call ax
    pop cx
    mov ax, 80
    mul word [cs:xc_rw]
    add ax, [cs:xc_x2]
    and ax, 1fffh
    add ax, [cs:xc_bank]
    mov di, ax
    jmp .one

; the per-column constants: x2 = 2x, rw = the first row whose address wraps
; AX = x
xc_setx:
    mov [cs:xc_x], ax
    shl ax, 1
    mov [cs:xc_x2], ax
    mov ax, 8192 + 79
    sub ax, [cs:xc_x2]
    xor dx, dx
    mov cx, 80
    div cx
    mov [cs:xc_rw], ax
    ret

; -----------------------------------------------------------------------------
; xc_col - the whole entering column x (AX): 200 lines, both banks, the HUD's
; eight lines black.  DS = dictionary claim, ES = B800.
; -----------------------------------------------------------------------------
xc_col:
    call xc_setx
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
    xor ax, ax
    mov [cs:xc_bank], ax
    mov si, [cs:xc_tb]
    xor bx, bx
    mov cx, 32
    xor ax, ax
    call xc_run                     ; top, even lines: rows 0-31
    mov si, [cs:xc_bb]
    mov bx, 32
    mov cx, 64
    mov ax, 80 * 32
    call xc_run                     ; band, even lines: rows 32-95
    mov si, XC_ZERO
    mov bx, 96
    mov cx, 4
    mov ax, 80 * 96
    call xc_run                     ; HUD, even lines: rows 96-99
    mov word [cs:xc_bank], 2000h
    mov si, [cs:xc_tb]
    add si, 64
    xor bx, bx
    mov cx, 32
    xor ax, ax
    call xc_run
    mov si, [cs:xc_bb]
    add si, 128
    mov bx, 32
    mov cx, 64
    mov ax, 80 * 32
    call xc_run
    mov si, XC_ZERO
    mov bx, 96
    mov cx, 4
    mov ax, 80 * 96
    call xc_run
    ret

; -----------------------------------------------------------------------------
; the HUD, in stream.  Its image is 8 lines of 22 words: two words of black pad
; (room for the wipe of up to XC_MAXK stale cells) then the 20 cells, glyph row 0
; on line 1.  Painted at screen columns 10..29 of lines 192..199, which are the
; cells (row 96 + line/2, world column S + 10 + i).  When the window moves by k
; columns the old copy sits k cells left of the new place, and the k cells of pad
; are what wipes them, in the same pass; when it does not, only the changed
; cells are written.
; -----------------------------------------------------------------------------
; the changed characters of xb_hudstr [xb_hudfrom, xb_hudto) into the image, and
; which cells they were
xc_hud_img:
    mov bx, [xb_hudfrom]
.ch:
    mov al, [xb_hudstr + bx]
    cmp al, [xb_hudcur + bx]
    je .next
    mov [xb_hudcur + bx], al
    mov byte [xc_hany], 1
    cmp byte [xc_hflag + bx], 0     ; the cell joins the list of cells the card is owed
    jne .listed
    mov byte [xc_hflag + bx], 1
    mov di, [xc_hn]
    mov [xc_hlist + di], bx
    add word [xc_hn], 2
.listed:
    sub al, 32
    xor ah, ah
    mov cl, 4
    shl ax, cl
    add ax, xb_font
    mov si, ax
    mov di, bx
    shl di, 1
    add di, xc_hudimg + XC_MAXK * 2 + (XB_HUDLEN + XC_MAXK) * 2   ; glyph row 0 goes on line 1
    mov cx, 7                       ; (glyph row 7 is blank in every glyph)
.g:
    movsw
    add di, 42
    loop .g
.next:
    inc bx
    cmp bx, [xb_hudto]
    jb .ch
    ret

; DI = ring offset, DX = the bank (0 or 2000h), SI = source (DS), CX = words
xc_putw:
    mov ax, cx
    shl ax, 1
    add ax, di
    cmp ax, 2000h
    ja .split
    add di, dx
    rep movsw
    ret
.split:
    mov ax, 2000h
    sub ax, di
    shr ax, 1                       ; the words before the wrap
    sub cx, ax
    push cx
    mov cx, ax
    add di, dx
    rep movsw
    pop cx
    mov di, dx
    rep movsw
    ret

; the whole HUD image, with the k-cell wipe.  ES = B800, DS = the package
xc_hud:
    mov ax, [xb_shownS]
    add ax, XB_HUDCOL
    sub ax, [xc_k]
    shl ax, 1
    add ax, 80 * 96
    and ax, 1fffh
    mov di, ax                      ; the first cell to write, in the row-96 ring offset
    mov si, xc_hudimg
    mov ax, XC_MAXK
    sub ax, [xc_k]
    shl ax, 1
    add si, ax                      ; skip the pad the wipe does not need
    add si, (XB_HUDLEN + XC_MAXK) * 2   ; image line 0 is black over the black the world
    mov dx, 2000h                   ; already has there (its columns are written black):
    mov bp, 7                       ; a card word costs ~86 clocks, so start at line 1
.line:
    push di
    push si
    push dx
    mov cx, XB_HUDLEN
    add cx, [xc_k]
    call xc_putw
    pop dx
    pop si
    pop di
    add si, (XB_HUDLEN + XC_MAXK) * 2
    xor dx, 2000h
    jnz .same
    add di, 80
    and di, 1fffh
.same:
    dec bp
    jnz .line
    ret

; the flagged cells only (the window did not move).  ES = B800, DS = the package
xc_hud_cells:
    xor bp, bp                      ; the list's cursor
.cell:
    cmp bp, [xc_hn]
    jae .done
    mov bx, [ds:xc_hlist + bp]       ; (BP addresses SS)
    add bp, 2
    mov byte [xc_hflag + bx], 0
    mov ax, [xb_shownS]
    add ax, XB_HUDCOL
    add ax, bx
    shl ax, 1
    add ax, 80 * 96
    and ax, 1fffh
    mov di, ax
    mov si, bx
    shl si, 1
    add si, xc_hudimg + XC_MAXK * 2 + (XB_HUDLEN + XC_MAXK) * 2   ; from image line 1:
    mov dx, 2000h                   ; line 0 is black over black
    mov cx, 7
.line:
    mov ax, [si]
    push di
    add di, dx
    mov [es:di], ax
    pop di
    add si, (XB_HUDLEN + XC_MAXK) * 2
    xor dx, 2000h
    jnz .same
    add di, 80
    and di, 1fffh
.same:
    loop .line
    jmp .cell
.done:
    mov word [xc_hn], 0
    ret

; -----------------------------------------------------------------------------
; the boxes.  A box is the union of a bike's old and new footprints (world word
; columns [ux0, ux0 + W), lines [uy0, uy0 + H)), W <= 8, H <= 32, row-major words.
; -----------------------------------------------------------------------------
xc_bchain:
%rep XC_BOXH
    movsw
    add di, dx
%endrep
xc_bchain_end:
    ret

; [xc_bx0], [xc_by0], [xc_bw], [xc_bh], the buffer [xc_bbuf]: fill it with the
; world.  A column is two chains (the top strip's lines, the band strip's), each
; `movsw / add di,2W-2` a line, RAM to RAM.
xc_fill:
    mov ax, 64
    sub ax, [xc_by0]
    jg .t1
    xor ax, ax
.t1:
    cmp ax, [xc_bh]
    jbe .t2
    mov ax, [xc_bh]
.t2:
    mov [xc_nt], ax                 ; lines from the top strip
    mov ax, [xc_by0]
    cmp ax, 64
    jae .b0
    mov ax, 64
.b0:
    mov [xc_b0], ax                 ; the first band line
    mov bx, [xc_by0]
    add bx, [xc_bh]
    sub bx, ax
    jg .b1
    xor bx, bx
.b1:
    mov [xc_nb], bx                 ; lines from the band strip
    mov ax, [xc_b0]
    sub ax, [xc_by0]
    mov cx, [xc_bw]
    mul cx
    mov [xc_bo], ax                 ; the band part's first box row, in words
    push ds
    push cs
    pop es
    mov ds, [cs:xc_dseg]
    mov dx, [cs:xc_bw]
    shl dx, 1
    sub dx, 2                       ; DX = 2W - 2: the next line's word after the movsw
    xor bp, bp                      ; the box column
.col:
    mov bx, [cs:xc_bx0]
    add bx, bp                      ; the world column
    cmp word [cs:xc_nt], 0
    je .band
    push bx
    and bx, XB_TOPW - 1
    mov cl, 7
    shl bx, cl
    mov si, [cs:xc_by0]
    shl si, 1
    add si, bx
    add si, XC2_TOP
    mov di, bp
    shl di, 1
    add di, [cs:xc_bbuf]
    mov cx, [cs:xc_nt]
    call xc_bcopy
    pop bx
.band:
    cmp word [cs:xc_nb], 0
    je .next
    mov al, [cs:bx + xb_cid]
    xor ah, ah
    mov cl, 8
    shl ax, cl
    add ax, XC2_BAND
    mov si, [cs:xc_b0]
    sub si, 64
    shl si, 1
    add si, ax
    mov ax, [cs:xc_bo]
    add ax, bp
    shl ax, 1
    add ax, [cs:xc_bbuf]
    mov di, ax
    mov cx, [cs:xc_nb]
    call xc_bcopy
.next:
    inc bp
    cmp bp, [cs:xc_bw]
    jb .col
    pop ds
    ret

; CX lines (1..32) from SI to DI with the per-line step DX
xc_bcopy:
    mov ax, cx
    shl ax, 1
    add ax, cx                      ; 3 bytes a line
    neg ax
    add ax, xc_bchain_end
    call ax
    ret

; draw the bike at [xc_bdw] into the box: the world's byte columns 2S + byte0 + i
; against the box's 2 * ux0; rows are trimmed (first, count) then pairs.
; DS = package on entry and exit.
xc_draw:
    mov bp, [xc_cj]                 ; the record, and where it goes: [xc_dy] its first line, [xc_db] its first
    mov al, [cs:xb_bikes + bp + 1]  ; byte from the window's left, [xc_dp] its phase (0 or 2)
    xor ah, ah
    shl ax, 1
    mov bx, [xc_dp]
    shr bx, 1
    add ax, bx
    shl ax, 1
    mov bx, ax                      ; (pose * 2 + phase / 2) * 2
    ; the box row of the sprite's line 0, and the byte offset in it
    mov ax, [xc_dy]
    sub ax, [xc_by0]
    mov cx, [xc_bw]
    shl cx, 1
    mov [xc_stride], cx             ; bytes a box row
    mul cx
    add ax, [xc_bbuf]
    mov dx, [xc_new]
    shl dx, 1
    add dx, [xc_db]
    mov cx, [xc_bx0]
    shl cx, 1
    sub dx, cx                      ; byte offset in the row
    add dx, ax                      ; DX = the row pointer, first byte
    push cs
    pop es
    mov ax, [cs:xs_ctab + bx]       ; a hot pose has its code (sprite.inc's xs_compile, SPEC.md 102.7.2):
    test ax, ax                     ; the same bytes written by straight-line code, ~2.3x faster
    jz .interp
    mov [cs:xs_cfar], ax
    mov ax, [cs:xb_sprseg]
    mov [cs:xs_cfar + 2], ax
    mov di, dx                      ; DI = the first row pointer, DX = the box's stride, ES = the box
    mov dx, [cs:xc_stride]
    call far [cs:xs_cfar]
    ret                             ; (DS was the package throughout)
.interp:
    mov ds, [cs:xb_sprseg]
    mov si, [bx]                    ; the blob
    mov bp, XB_POSE_H
.row:
    lodsw                           ; AL = first byte, AH = count
    xor cx, cx
    mov cl, ah
    jcxz .next
    mov bl, al
    xor bh, bh
    mov di, dx
    add di, bx
.byte:
    lodsw                           ; AL = AND, AH = OR
    and al, [es:di]
    or al, ah
    stosb
    loop .byte
.next:
    add dx, [cs:xc_stride]
    dec bp
    jnz .row
    push cs
    pop ds
xc_draw_end:
    ret

; a box ([xc_bx0] ...) becomes a descriptor and its buffer is filled, and drawn
; over if [xc_bdw] is not 0ffffh
xc_mkbox:
    mov bx, [xc_nbx]
    cmp bx, XC_NBOX
    jae .out                        ; (cannot happen: at most 2 a bike, 4 bikes)
    mov ax, bx
    mov cl, 9
    shl ax, cl
    add ax, xc_boxes
    mov [xc_bbuf], ax
    mov si, bx
    mov cl, 4
    shl si, cl
    add si, xc_bd
    mov ax, [xc_bx0]
    mov [si], ax
    mov ax, [xc_by0]
    mov [si + 2], ax
    mov ax, [xc_bw]
    mov [si + 4], ax
    mov ax, [xc_bh]
    mov [si + 6], ax
    mov ax, [xc_bbuf]
    mov [si + 8], ax
    inc word [xc_nbx]
    call xc_fill
    call xc_comp
.out:
    ret

; -----------------------------------------------------------------------------
; xc_comp - draw into the box every bike whose footprint meets it, in RECORD order
; (SPEC.md 102.4).  A box is composed from the world alone and written to the card
; whole, so a bike that overlaps another's box would lose the pixels the other put
; there: the box therefore carries every bike that touches it, and whichever order
; the boxes are written in, each shows the true picture of its own region.  The
; box's own bike (xc_bdw, absent for an old-footprint-only box) takes the fast
; path, the others a clipped draw: only a NEIGHBOUR can hang outside the box.
; Two riders that do not touch pay xc_geo's ~90 clocks a record, nothing else.
; -----------------------------------------------------------------------------
xc_comp:
    mov word [xc_pi], 0
.j:
    mov bx, [xc_pi]
    cmp bx, [xc_np]
    jae .out
    mov bp, [xc_plist + bx]         ; the next PRESENT record (xc_prep lists them once a frame:
    add bx, 2                       ; a rider alone pays for one iteration, not six)
    mov [xc_pi], bx
    mov [xc_cj], bp
    cmp bp, [xc_bdw]
    jne .other
    cmp byte [xc_edge], 0
    jne .ownedge
    mov ax, [xc_ny]                 ; the box's own bike: it is wholly inside, and takes the fast draw
    mov [xc_dy], ax
    mov ax, [xc_byte0]
    mov [xc_db], ax
    mov ax, [xc_phase]
    mov [xc_dp], ax
    call xc_draw
    jmp .j
.ownedge:
    mov ax, [xc_ny]                 ; ...unless it hangs off an edge of the picture: the clipping blitter
    mov [xc_gy], ax
    mov ax, [xc_byte0]
    mov [xc_gb], ax
    mov ax, [xc_phase]
    mov [xc_gp], ax
    call xc_drawc
    jmp .j
.other:
    ; A bike LATER in record order than the box's owner lies on top of it, and a box is written after
    ; the ones before it: when it has a box of its own this frame (xc_prep redraws EVERY present bike
    ; whenever the window moved), that box carries this bike and covers every pixel of it, so this
    ; box need not draw it.  (When the window did not move an unchanged bike has no box, and is drawn
    ; into the boxes that meet it, as before.)  It halves the neighbour draws of a bunch of riders.
    cmp bp, [xc_bi]
    jbe .geo
    cmp word [xc_k], 0
    jne .j
    cmp byte [xc_full], 0
    jne .j
.geo:
    call xc_geo
    jc .j
    mov ax, [xc_gx1]
    cmp ax, [xc_bx0]
    jb .j                           ; wholly left of the box
    mov ax, [xc_bx0]
    add ax, [xc_bw]
    dec ax
    cmp [xc_gx0], ax
    ja .j                           ; wholly right
    mov ax, [xc_by0]
    add ax, [xc_bh]
    cmp [xc_gy], ax
    jge .j                          ; wholly below
    mov ax, [xc_gy]
    add ax, XB_POSE_H
    cmp ax, [xc_by0]
    jle .j                          ; wholly above
    ; a neighbour that lies wholly inside the box (the box of a bike beside it, or of the two
    ; together) needs no clipping: it takes the same fast, compiled draw as the box's own bike
    cmp byte [xc_gedge], 0
    jne .clip                       ; (it hangs off an edge of the picture: clip it to the box)
    mov ax, [xc_gx0]
    cmp ax, [xc_bx0]
    jb .clip
    mov ax, [xc_bx0]
    add ax, [xc_bw]
    dec ax
    cmp [xc_gx1], ax
    ja .clip
    mov ax, [xc_gy]
    cmp ax, [xc_by0]
    jl .clip
    add ax, XB_POSE_H
    mov dx, [xc_by0]
    add dx, [xc_bh]
    cmp ax, dx
    jg .clip
    mov ax, [xc_gy]
    mov [xc_dy], ax
    mov ax, [xc_gb]
    mov [xc_db], ax
    mov ax, [xc_gp]
    mov [xc_dp], ax
    call xc_draw
    jmp .j
.clip:
    call xc_drawc
    jmp .j
.out:
    ret

; BP = a bike record -> CF=1: not on the picture (absent, wholly outside it, or reaching the
; HUD's lines); else AX = the first byte from the window's left (NEGATIVE: the bike hangs off the
; left edge), DX = the phase (0 or 2), BX = its width in bytes (6, or 7 shifted), SI = its first
; line, CX = its first WORD column and DI = its last, both CLIPPED to the window and in world
; columns from [xc_new], and [xc_pedge] = 1 when any of its bytes lies off either edge (SPEC.md
; 102.7.4: an edge rider is drawn by the clipping blitter, xc_drawc, into a box that stops at the
; picture's edge).  Only the bike's x is read as signed: it is even, and -22 is the least that
; still shows a pixel of a 24-pixel pose.
xc_place:
    mov ax, [cs:xb_bikes + bp + 2]
    and ax, 0fffeh
    mov dx, ax
    and dx, 2
    mov cl, 2
    sar ax, cl
    mov bx, 6
    test dx, dx
    jz .w
    inc bx
.w:
    mov cx, ax
    add cx, bx
    jle .no                         ; wholly left of the picture
    cmp ax, [cs:xc_wb]
    jge .no                         ; wholly right of it
    mov si, [cs:xb_bikes + bp + 4]
    cmp si, XB_WORLD_ROWS * 8 - XB_POSE_H
    ja .no
    mov byte [cs:xc_pedge], 0
    test ax, ax
    js .edge
    cmp cx, [cs:xc_wb]
    jbe .clip
.edge:
    mov byte [cs:xc_pedge], 1
.clip:
    mov di, cx                      ; one past the last byte, clipped to the picture
    cmp di, [cs:xc_wb]
    jbe .r
    mov di, [cs:xc_wb]
.r:
    dec di
    shr di, 1
    add di, [cs:xc_new]
    mov cx, ax
    test cx, cx
    jns .l
    xor cx, cx
.l:
    shr cx, 1
    add cx, [cs:xc_new]
    clc
    ret
.no:
    stc
    ret

; BP = a bike record: where it is drawn this frame -> CF=0 and [xc_gy] the first line, [xc_gb] the
; first byte from the window's left (signed), [xc_gp] the phase, [xc_gx0..xc_gx1] the word columns
; clipped to the window, [xc_gedge] whether it hangs off an edge; CF=1 = not drawn
xc_geo:
    cmp byte [cs:xb_bikes + bp], 0
    je .no
    cmp byte [xb_nspr], 0
    je .no
    call xc_place
    jc .no
    mov [xc_gy], si
    mov [xc_gb], ax
    mov [xc_gp], dx
    mov [xc_gx0], cx
    mov [xc_gx1], di
    mov al, [xc_pedge]
    mov [xc_gedge], al
    clc
    ret
.no:
    stc
    ret

; the bike at [xc_cj] drawn into the box CLIPPED to it: rows above or below the box are
; stepped over, and of each row's span only the bytes inside the box are drawn (the
; AND/OR pairs before and after are stepped over with one ADD each).  A neighbour
; usually shares a few bytes with a box, so this is ~120 clocks a row and ~40 a byte
; drawn against the ~60 a byte of the checked-every-byte first version, which - with
; three opponents bunched round the rider - was 50k clocks a box.  DS = the package on
; entry and exit.
xc_drawc:
    mov bp, [xc_cj]
    mov al, [cs:xb_bikes + bp + 1]
    xor ah, ah
    shl ax, 1
    mov bx, [xc_gp]
    shr bx, 1
    add ax, bx
    shl ax, 1
    mov bx, ax                      ; (pose * 2 + phase / 2) * 2
    mov ax, [xc_new]
    sub ax, [xc_bx0]
    shl ax, 1
    add ax, [xc_gb]
    mov [xc_dlt], ax                ; the box-relative byte of the sprite's byte 0 (signed)
    mov ax, [xc_bw]
    shl ax, 1
    mov [xc_stride], ax
    mov ax, [xc_gy]
    sub ax, [xc_by0]
    mov [xc_dr], ax                 ; the box line of the sprite's row 0 (signed)
    push cs
    pop es
    mov ds, [cs:xb_sprseg]
    mov si, [bx]
    mov bp, XB_POSE_H
.row:
    lodsw                           ; AL = first byte, AH = count
    xor cx, cx
    mov cl, ah
    jcxz .next
    mov dx, [cs:xc_dr]
    cmp dx, 0
    jl .skip
    cmp dx, [cs:xc_bh]
    jge .skip
    xor bh, bh
    mov bl, al
    add bx, [cs:xc_dlt]             ; BX = the box-relative byte of the span's first (signed)
    mov ax, bx
    neg ax
    jg .lo
    xor ax, ax                      ; AX = the span's first byte that is inside the box
.lo:
    mov dx, [cs:xc_stride]
    sub dx, bx                      ; DX = one past its last inside the box
    cmp dx, cx
    jle .hi
    mov dx, cx
.hi:
    cmp ax, dx
    jge .skip                       ; none of this span is in the box
    mov [cs:xc_te], dx
    mov [cs:xc_cnt], cx
    push ax
    push bx
    mov ax, [cs:xc_dr]
    mul word [cs:xc_stride]
    add ax, [cs:xc_bbuf]
    mov di, ax                      ; the box row
    pop bx
    pop ax
    add di, bx
    add di, ax                      ; the first byte to write
    mov bx, ax
    shl bx, 1
    add si, bx                      ; step over the pairs left of the box
    mov cx, [cs:xc_te]
    sub cx, ax                      ; the bytes inside
.byte:
    lodsw                           ; AL = AND, AH = OR
    and al, [es:di]
    or al, ah
    stosb
    loop .byte
    mov cx, [cs:xc_cnt]
    sub cx, [cs:xc_te]
    shl cx, 1
    add si, cx                      ; ...and the pairs right of it
    jmp .next
.skip:
    shl cx, 1
    add si, cx
.next:
    inc word [cs:xc_dr]
    dec bp
    jnz .row
    push cs
    pop ds
    ret

; -----------------------------------------------------------------------------
; xc_prep - the RAM-side work of the sprites, before the retrace: for each bike the
; box or boxes to write.  DS = the package.
; -----------------------------------------------------------------------------
xc_prep:
    mov word [xc_nbx], 0
    mov word [xc_snap], 0
    xor bp, bp                      ; the present records, once a frame (xc_comp's list)
    xor bx, bx
.pl:
    cmp byte [cs:xb_bikes + bp], 0
    je .pn
    mov [xc_plist + bx], bp
    add bx, 2
.pn:
    add bp, XB_BIKE_SIZE
    cmp bp, XB_MAXBIKES * XB_BIKE_SIZE
    jb .pl
    mov [xc_np], bx                 ; (bytes: two a record)
    xor bp, bp                      ; bike record offset
.bike:
    test bp, bp
    jz .first
    cmp byte [cs:xb_bikes + bp], 0
    je .first
    call xb_clk_sample              ; a bike's box is ~15-40k clocks: before every present one but the
.first:                             ; first, keep the sub-tick clock seen
    mov [xc_bi], bp
    cmp byte [cs:xb_bikes + bp], 0
    jne .try
    cmp word [cs:xc_fp + bp + 6], 0 ; not there and nothing of it on the glass: skip it
    je .nextbike
.try:
    ; Nothing moved, nothing scrolled, nothing to write: its cells are exactly what
    ; they were, so the box is skipped whole (a stationary bike under a still
    ; window costs the frame nothing).  Six bytes of the record are what draws:
    ; the flag, the pose, x and y.
    cmp word [xc_k], 0
    jne .redraw
    cmp byte [xc_full], 0
    jne .redraw
    cmp word [cs:xc_fp + bp + 6], 0
    je .redraw                      ; nothing of it on the glass yet
    mov si, xb_bikes
    add si, bp
    mov di, xc_last
    add di, bp
    mov cx, 3
    push ds
    pop es
    cld
    repe cmpsw
    je .nextbike
.redraw:
    push ds                         ; snapshot this bike's footprint and last pose (xc_render
    pop es                          ; restores them when the frame's commit is skipped)
    cld
    mov si, xc_fp
    add si, bp
    mov di, xc_fp2
    add di, bp
    mov cx, 4
    rep movsw
    mov si, xc_last
    add si, bp
    mov di, xc_last2
    add di, bp
    mov cx, 4
    rep movsw
    mov ax, 1
    mov cx, bp
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shl ax, cl
    or [xc_snap], ax
    mov si, xb_bikes                ; remember this state for the next frame's compare
    add si, bp
    mov di, xc_last
    add di, bp
    mov cx, 3
    push ds
    pop es
    cld
    rep movsw
    mov word [xc_act], 0
    cmp byte [cs:xb_bikes + bp], 0
    je .old
    cmp byte [xb_nspr], 0
    je .old
    call xc_place                   ; CF: wholly off the picture, or reaching the HUD's lines: not drawn
    jc .old
    mov [xc_ny], si
    mov [xc_byte0], ax
    mov [xc_phase], dx
    mov [xc_nx0], cx
    mov [xc_nx1], di
    mov al, [xc_pedge]
    mov [xc_edge], al
    mov word [xc_act], 1
.old:
    ; the old footprint, clipped to the window: what scrolled out is gone
    mov bx, bp                      ; a footprint is 8 bytes, like a bike record
    mov word [xc_ov], 0
    cmp word [xc_fp + bx + 6], 0
    je .decide
    cmp byte [xc_full], 0
    jne .decide
    mov ax, [xc_fp + bx]            ; its first column
    mov dx, ax
    add dx, [xc_fp + bx + 4]
    dec dx                          ; its last
    cmp ax, [xc_new]
    jae .oa
    mov ax, [xc_new]
.oa:
    mov cx, [xc_new]
    add cx, [xc_wc1]
    cmp dx, cx
    jbe .ob
    mov dx, cx
.ob:
    cmp ax, dx
    ja .decide
    mov [xc_ox0], ax
    mov [xc_ox1], dx
    mov ax, [xc_fp + bx + 2]
    mov [xc_oy], ax
    mov word [xc_ov], 1
.decide:
    ; remember the new footprint for the next frame
    mov ax, [xc_act]
    mov [xc_fp + bx + 6], ax
    or ax, ax
    jz .nonew
    mov ax, [xc_nx0]
    mov [xc_fp + bx], ax
    mov dx, [xc_nx1]
    sub dx, ax
    inc dx
    mov [xc_fp + bx + 4], dx
    mov ax, [xc_ny]
    mov [xc_fp + bx + 2], ax
.nonew:
    cmp word [xc_act], 0
    jne .hasnew
    cmp word [xc_ov], 0
    je .nextbike
    ; only the old: restore it
    mov ax, [xc_ox0]
    mov [xc_bx0], ax
    mov ax, [xc_ox1]
    sub ax, [xc_ox0]
    inc ax
    mov [xc_bw], ax
    mov ax, [xc_oy]
    mov [xc_by0], ax
    mov word [xc_bh], XB_POSE_H
    mov word [xc_bdw], 0ffffh
    call xc_mkbox
    jmp .nextbike
.hasnew:
    cmp word [xc_ov], 0
    je .newonly
    ; the union, if it fits a box; otherwise the old alone, then the new alone
    mov ax, [xc_ox0]
    cmp ax, [xc_nx0]
    jbe .u1
    mov ax, [xc_nx0]
.u1:
    mov [xc_bx0], ax
    mov dx, [xc_ox1]
    cmp dx, [xc_nx1]
    jae .u2
    mov dx, [xc_nx1]
.u2:
    sub dx, ax
    inc dx
    mov [xc_bw], dx
    mov ax, [xc_oy]
    cmp ax, [xc_ny]
    jbe .u3
    mov ax, [xc_ny]
.u3:
    mov [xc_by0], ax
    mov dx, [xc_oy]
    cmp dx, [xc_ny]
    jae .u4
    mov dx, [xc_ny]
.u4:
    add dx, XB_POSE_H
    sub dx, ax
    mov [xc_bh], dx
    cmp word [xc_bw], XC_BOXW
    ja .split
    cmp word [xc_bh], XC_BOXH
    ja .split
    mov ax, [xc_bi]
    mov [xc_bdw], ax
    call xc_mkbox
    jmp .nextbike
.split:
    mov ax, [xc_ox0]
    mov [xc_bx0], ax
    mov ax, [xc_ox1]
    sub ax, [xc_ox0]
    inc ax
    mov [xc_bw], ax
    mov ax, [xc_oy]
    mov [xc_by0], ax
    mov word [xc_bh], XB_POSE_H
    mov word [xc_bdw], 0ffffh
    call xc_mkbox
.newonly:
    mov ax, [xc_nx0]
    mov [xc_bx0], ax
    mov ax, [xc_nx1]
    sub ax, [xc_nx0]
    inc ax
    mov [xc_bw], ax
    mov ax, [xc_ny]
    mov [xc_by0], ax
    mov word [xc_bh], XB_POSE_H
    mov ax, [xc_bi]
    mov [xc_bdw], ax
    call xc_mkbox
.nextbike:
    mov bp, [xc_bi]
    add bp, XB_BIKE_SIZE
    cmp bp, XB_MAXBIKES * XB_BIKE_SIZE
    jb .bike
    ret

; write the boxes to the card, a line at a time.  ES = B800, DS = the package.
; A box that does not reach the ring's end (nearly all of them) takes the fast
; path: `rep movsw` a line and one ADD to reach the next line's cell (the bank
; alternates: +2000h - 2W, then +80 - 2W - 2000h); one that does takes xc_putw.
xc_xfer:
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
    mov cx, 80
    mul cx
    mov cx, [si]                    ; ux0
    shl cx, 1
    add ax, cx
    and ax, 1fffh
    mov di, ax                      ; the first line's ring offset
    mov dx, bx
    and dx, 1
    mov cl, 13
    shl dx, cl                      ; ...and its bank
    mov cx, [si + 4]
    mov [xc_hw], cx                 ; the width in words
    mov bp, [si + 6]                ; the height in lines
    mov si, [si + 8]                ; the buffer
    ; the last row's end: ring offset + 80 * ceil(H / 2) + 2W must fit
    mov ax, bp
    inc ax
    shr ax, 1
    mov cx, 80
    push dx
    mul cx
    pop dx
    add ax, di
    mov cx, [xc_hw]
    shl cx, 1
    add ax, cx
    cmp ax, 2000h
    ja .slow
    ; fast: BX = W words, AX = the step after an even line, CX = after an odd one
    mov bx, [xc_hw]
    mov ax, 2000h
    sub ax, cx                      ; CX is still 2W
    mov [xc_s0], ax
    mov ax, 80 - 2000h
    sub ax, cx
    mov [xc_s1], ax
    add di, dx                      ; the first line's real address
    test dx, dx
    jnz .odd                        ; starts on an odd line
.even:
    mov cx, bx
    rep movsw
    add di, [xc_s0]
    dec bp
    jz .boxdone
.odd:
    mov cx, bx
    rep movsw
    add di, [xc_s1]
    dec bp
    jnz .even
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
    xor dx, 2000h
    jnz .same
    add di, 80
    and di, 1fffh
.same:
    dec bp
    jnz .line
    jmp .boxdone
.out:
    ret

; -----------------------------------------------------------------------------
; xc_render - one frame.  RAM-side work first (nothing here touches the card),
; then the retrace, then every card write in raster order.
; -----------------------------------------------------------------------------
xc_render:
xc_t0:
    push ds
    pop es                          ; the movsw/cmpsw below need ES = DS
    mov ax, [xb_S]
    mov cx, [xc_S]
    mov word [xc_k], 0
    mov byte [xc_full], 0
    cmp cx, 0ffffh
    je .full
    cmp ax, cx
    jb .full                        ; backwards
    mov dx, ax
    sub dx, cx
    cmp dx, 32
    jae .full                       ; a jump: rewrite the window
    cmp dx, XC_MAXK
    jbe .k
    mov dx, XC_MAXK                 ; two columns a frame keep the writer ahead of the beam
    mov ax, cx
    add ax, dx
.k:
    mov [xc_k], dx
    jmp .have
.full:
    inc byte [xc_full]
    mov byte [xc_hall], 1
    mov byte [xb_bigframe], 1
.have:
    mov [xc_new], ax
    cmp byte [xb_huddirty], 0
    je .nohud
    mov bl, [xb_huddirty]
    mov byte [xb_huddirty], 0
    mov word [xb_hudfrom], 0
    mov word [xb_hudto], XB_HUDLEN
    cmp bl, 2
    je .hud
    mov word [xb_hudfrom], XB_HUDDIG0
    mov word [xb_hudto], XB_HUDDIG1
.hud:
    call xc_hud_img
.nohud:
xc_ts:
    call xc_prep
xc_t1:
    call word [xc_vrp]
    cmp byte [xc_to], 0
    je xc_t2
    cmp byte [xc_full], 0
    jne .fullland                   ; a full write is over budget by construction: it must land
    jmp .skipped
.fullland:
    mov byte [xc_to], 0
    jmp xc_t2
.skipped:
    ; the retrace never came inside this frame's budget: commit nothing (an unsynchronised
    ; start address shows a garbage column strip), give prep's state back and let the
    ; next frame's catch-up (xc_S unchanged) write it
    push ds
    pop es
    xor bp, bp                      ; give back the entries prep rewrote (the snapshot mask)
    mov dx, 1
.rb:
    test [xc_snap], dx
    jz .rbn
    mov si, xc_fp2
    add si, bp
    mov di, xc_fp
    add di, bp
    mov cx, 4
    rep movsw
    mov si, xc_last2
    add si, bp
    mov di, xc_last
    add di, bp
    mov cx, 4
    rep movsw
.rbn:
    add bp, 8
    shl dx, 1
    cmp bp, XB_MAXBIKES * XB_BIKE_SIZE
    jb .rb
    ret
xc_t2:
    mov ax, [xc_new]
    mov [xb_shownS], ax
    mov dx, [xc_crtc]               ; the start address, in words
    mov cx, ax
    mov al, 0ch
    mov ah, ch
    out dx, ax
    mov al, 0dh
    mov ah, cl
    out dx, ax
    mov es, [xc_vseg]
    push ds
    mov ds, [xc_dseg]
    cmp byte [cs:xc_full], 0
    jne .all
    mov cx, [cs:xc_new]
    sub cx, [cs:xc_k]               ; the old S
    add cx, [cs:xc_wc1]             ; ...the first entering column
    inc cx
    jmp .lim
.all:
    mov cx, [cs:xc_new]
.lim:
    mov ax, [cs:xc_new]
    add ax, [cs:xc_wc1]
    mov [cs:xc_xl], ax
.entering:
    cmp cx, [cs:xc_xl]
    ja .entered
    cmp byte [cs:xc_full], 0
    je .nosamp
    call xb_clk_sample              ; a full window write is 40 columns of ~9k
.nosamp:
    push cx
    mov ax, cx
    call word [cs:xc_colp]
    pop cx
    inc cx
    jmp .entering
.entered:
    pop ds
xc_t3:                              ; (after the POP: the stack walker starts a routine at a global label)
    ; (the old footprints are erased by the boxes that cover them)
    ; the HUD: repaint on scroll or after a full write, else only the changed cells
    mov es, [xc_vseg]
    cmp word [xc_k], 0
    jne .paint
    cmp byte [xc_hall], 0
    jne .paint
    cmp byte [xc_hany], 0
    je .sprites
    call word [xc_hudcp]            ; (it clears the flags it painted and empties the list)
    mov byte [xc_hany], 0
    jmp .sprites
.paint:
    call word [xc_hudp]
    mov byte [xc_hall], 0
    mov byte [xc_hany], 0
    mov word [xc_hn], 0
    xor ax, ax
    mov di, xc_hflag
    mov cx, XB_HUDLEN / 2
    push ds
    pop es
    cld
    rep stosw
    mov es, [xc_vseg]
.sprites:
xc_t4:
    call word [xc_xferp]
xc_t5:
    mov ax, [xc_new]
    mov [xc_S], ax
    ret

; wait for the retrace to START, and count the wait as idle (xb_spin), not as work.
; A frame that finishes its work with the beam past the retrace waits for the next one
; (at most two sub-ticks from entry, and the sim's next step count is the
; time actually taken) rather than being thrown away: a skipped frame wasted all its
; work and, when the frames are heavy (three bikes and two columns), skipped again
; and again - the picture then froze for whole seconds of catch-up.
xc_vr:
    call xb_clk_sample
    mov ax, [xb_subs]
    mov [xb_sp0], ax
    mov ax, XB_SUB
    sub ax, [xb_pitl]
    mov [xb_sp1], ax
    mov dx, 3dah
.spin:
    in al, dx
    test al, 8
    jnz .done
    push dx
    call xb_clk_sample
    pop dx
    mov ax, [xb_subs]
    sub ax, [xb_sp0]                ; the retrace comes every 0.91 of a sub-tick, so two whole
    cmp ax, 2                       ; sub-ticks from ENTRY always hold one: wait that long at most
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
