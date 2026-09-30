; VGA 0Dh back end (SPEC.md 102.1): 320 x 200 x 16 planar, 8 pixels a byte a
; plane, stride 40.  THE WHOLE DESIGN IS ONE IDEA - the world is stored SHEARED.
; The byte of (pixel line l, world column x) lives at page + 40 * l + x, so a
; page holds a 40 x 192 window at start address page + S and moving the window
; one column needs ONE new byte per line (the entering column), landing in the
; cell the line below just scrolled out of.  No ring, no mirror, no rebase.
;
; The two dictionaries (the band's 128-line column strips and the top picture's
; 64-line strips) live in OFF-SCREEN VRAM, so an entering column is a latch copy:
; write mode 1, `movsb` from the strip, `add di,39` - one read and one write
; moves 8 pixels of all four planes (~31 clocks a line).
;
; THREE pages, not the plan's two, and the reason is a proof rather than a
; taste (SPEC.md 102.1): a start-address write takes effect at the next
; vertical retrace, up to 14 ms later, and the page it replaces stays on the
; glass until then.  Two pages would need a retrace wait to know it is gone;
; a wait burns the frame (a spin, and up to 14 of a sub-tick's 18 ms).  With
; three, the page a frame writes is never the one just replaced and never the
; one just requested, whatever the retrace phase, at the price of one more
; page of columns to bring up to date (~1.5x the column work, ~2.4k clocks).
;
; The HUD row is line compare: the split at scan line 384 restarts the address
; at 0, where 8 lines of HUD sit for every page.
;
; Cycle notes (8088): OUT DX,AX 16; MOVSB 18; ADD DI,imm8 13 (fetch bound);
; CALL AX 16; XCHG AL,[ES:BX+DI] 26; a run of the chain costs ~155 clocks
; of setup and 31 a line.

%macro XV_GC 2                  ; GC index, value: 3CEh <- index | value << 8
    mov dx, 3ceh
    mov ax, (%2 << 8) | %1
    out dx, ax
%endmacro

; -----------------------------------------------------------------------------
; xv_init - the CRTC's split, the dictionaries into off-screen VRAM, page state
; -----------------------------------------------------------------------------
xv_init:
    SAVE
    call xv_dicts
    mov bx, [xb_recs + EXBR_PAL * 2]
    push ds
    mov ds, [xb_gfxseg]
    mov si, bx
    add si, 240                     ; after the five themes: the four pose ink slots
    lodsb
    mov [cs:xv_ink], al
    lodsb
    mov [cs:xv_ink + 1], al
    lodsb
    mov [cs:xv_ink + 2], al
    lodsb
    mov [cs:xv_ink + 3], al
    pop ds
    RESTORE
    ret

; -----------------------------------------------------------------------------
; xv_start - a race begins: the CRTC's split, the page state, the HUD's row black.
; The dictionaries (xv_init) are off-screen and outlive the menus, which draw at
; address 0 and never touch them.
; -----------------------------------------------------------------------------
xv_start:
    SAVE
    ; Line compare is 9 bits over three registers, and CRTC 0-7 are write
    ; protected by bit 7 of register 11h, which the BIOS mode set leaves as it
    ; likes: clear it, then 18h = low 8, 07h bit 4 = bit 8, 09h bit 6 = bit 9.
    mov dx, 3d4h
    mov al, 11h
    out dx, al
    inc dx
    in al, dx
    and al, 7fh
    out dx, al
    dec dx
    mov ax, ((XV_LC & 0ffh) << 8) | 18h
    out dx, ax
    mov al, 07h
    out dx, al
    inc dx
    in al, dx
    and al, 0efh
    test byte [cs:xv_lcbits], 1
    jz .b8
    or al, 10h
.b8:
    out dx, al
    dec dx
    mov al, 09h
    out dx, al
    inc dx
    in al, dx
    and al, 0bfh
    test byte [cs:xv_lcbits], 2
    jz .b9
    or al, 40h
.b9:
    out dx, al
    dec dx
    mov ax, 000ch                   ; the start address: home
    out dx, ax
    mov ax, 000dh
    out dx, ax
    mov es, [xb_fsi + FSI_SEG]
    mov dx, 3c4h
    mov ax, 0f02h
    out dx, ax
    XV_GC 5, 0
    XV_GC 8, 0ffh
    xor di, di
    xor ax, ax
    mov cx, XV_PAGE0 / 2
    cld
    rep stosw                       ; the HUD row (and whatever a menu left in it) black
    mov byte [es:XV_BLACK], 0       ; the black byte line 192 is copied from
    mov word [xv_pgS], 0ffffh
    mov word [xv_pgS + 2], 0ffffh
    mov word [xv_pgS + 4], 0ffffh
    mov byte [xv_fpn], 0
    mov byte [xv_fpn + 1], 0
    mov byte [xv_fpn + 2], 0
    mov byte [xv_pg], 2             ; the first frame takes page 0
    mov byte [xv_hi], 0             ; the start address is 0
    RESTORE
    ret

; -----------------------------------------------------------------------------
; xv_menu - the card back to a plain 320 x 200 screen at address 0 for the menus:
; no split, the start address home, write mode 0, every plane, the bit mask open.
; The new start address is latched at the next retrace, so it waits one frame
; before the caller clears the screen.
; -----------------------------------------------------------------------------
xv_menu:
    SAVE
    mov dx, 3d4h
    mov al, 11h
    out dx, al
    inc dx
    in al, dx
    and al, 7fh
    out dx, al
    dec dx
    mov ax, 0ff18h                  ; line compare 3FFh: past the last line
    out dx, ax
    mov al, 07h
    out dx, al
    inc dx
    in al, dx
    or al, 10h
    out dx, al
    dec dx
    mov al, 09h
    out dx, al
    inc dx
    in al, dx
    or al, 40h
    out dx, al
    dec dx
    mov ax, 000ch
    out dx, ax
    mov ax, 000dh
    out dx, ax
    mov byte [xv_hi], 0
    mov dx, 3c4h
    mov ax, 0f02h
    out dx, ax
    XV_GC 1, 0                      ; set/reset off
    XV_GC 3, 0                      ; no rotate, no logic
    XV_GC 5, 0                      ; write mode 0
    XV_GC 8, 0ffh
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT             ; the retrace that latches the start address
    mov al, FSXW_FRAME
    call OSAPI_FSX_WAIT
    RESTORE
    ret

; the line compare's bits above the low 8: XV_LC = 384 = 1_1000_0000
xv_lcbits: db ((XV_LC >> 8) & 1) | (((XV_LC >> 9) & 1) << 1)

; -----------------------------------------------------------------------------
; xv_dicts - tile art -> the two dictionaries, one plane at a time (the map
; mask is set once a plane): 976 band tiles + 512 top tiles of 8 bytes, each
; ~195 clocks, 4 planes = ~1.2M clocks (0.25 s), once, in the loading screen.
; -----------------------------------------------------------------------------
xv_dicts:
    SAVE
    mov es, [xb_fsi + FSI_SEG]
    mov byte [cs:xv_mm], 1
    mov word [cs:xv_po], 0
.plane:
    mov dx, 3c4h
    mov ah, [cs:xv_mm]
    mov al, 2
    out dx, ax
    push ds
    mov ds, [cs:xb_gfxseg]
    mov bx, [cs:xb_recs + EXBR_BAND * 2]
    mov bp, [cs:xb_recs + EXBR_TILES * 2]
    add bp, [cs:xv_po]              ; this plane's 8 rows inside a 32-byte tile
    mov di, XV_BAND
    mov dx, EXB_NCOLS * 16
.bt:
    mov al, [bx]
    inc bx
    xor ah, ah
    mov cl, 5
    shl ax, cl
    mov si, ax
    add si, bp
%rep 8
    movsb
%endrep
    dec dx
    jnz .bt
    mov bx, [cs:xb_recs + EXBR_TOP * 2]
    mov di, XV_TOP
    xor dx, dx
.tc:
    push bx
    mov ch, 8
.tr:
    mov al, [bx]
    add bx, XB_TOPW
    xor ah, ah
    mov cl, 5
    shl ax, cl
    mov si, ax
    add si, bp
%rep 8
    movsb
%endrep
    dec ch
    jnz .tr
    pop bx
    inc bx
    inc dx
    cmp dx, XB_TOPW
    jb .tc
    pop ds
    shl byte [cs:xv_mm], 1
    add word [cs:xv_po], 8
    cmp byte [cs:xv_mm], 16
    jb .plane
    mov dx, 3c4h
    mov ax, 0f02h
    out dx, ax
    RESTORE
    ret

; -----------------------------------------------------------------------------
; the chain: 128 lines of `movsb / add di,39`, entered part way so that exactly
; n lines run.  Four bytes a line (1 + 3), so entry = end - 4n.
; -----------------------------------------------------------------------------
xv_chain:
%rep 128
    movsb
    add di, 39
%endrep
xv_chain_end:
    ret

; BX -> a run list (in CS), DX = strip base, BP = the region's line-0 dest.
; A run is (word dest offset = 40 * first line, byte first line, byte count);
; a dest offset of 0FFFFh ends the list.  DS = ES = the frame buffer.
xv_runs:
.next:
    mov di, [cs:bx]
    cmp di, 0ffffh
    je .done
    mov ax, [cs:bx + 2]             ; AL = first line, AH = count
    add bx, 4
    add di, bp
    mov si, dx
    xor ch, ch
    mov cl, al
    add si, cx
    mov al, ah
    xor ah, ah
    shl ax, 1
    shl ax, 1
    neg ax
    add ax, xv_chain_end
    call ax
    jmp .next
.done:
    ret

; SI = source, DI = dest, CX = lines (1..128); DS = ES = the frame buffer
xv_copy:
    mov ax, cx
    shl ax, 1
    shl ax, 1
    neg ax
    add ax, xv_chain_end
    call ax
    ret

; -----------------------------------------------------------------------------
; xv_col - column x (AX) into the page at [xv_pbase].  DS = ES = frame buffer.
; [xv_skip] = 1: the cell being overwritten still holds column x-40's next line
; (the invariant that makes the plain-pair lists legal), so only the lines that
; DIFFER need writing.
; -----------------------------------------------------------------------------
xv_col:
    mov [cs:xv_x], ax
    mov di, [cs:xv_pbase]
    add di, ax
    mov bp, di                      ; the page's line 0 of this column
    mov bx, ax
    and bx, XB_TOPW - 1
    mov dx, bx
    mov cl, 6
    shl dx, cl
    add dx, XV_TOP                  ; the top strip
    mov ax, exb_full_top
    cmp byte [cs:xv_skip], 0
    je .toprun
    shl bx, 1
    mov ax, [cs:bx + exb_skip_top]
.toprun:
    mov bx, ax
    call xv_runs
    ; the band: line 64 on
    mov bx, [cs:xv_x]
    mov al, [cs:bx + xb_cid]
    xor ah, ah
    mov di, ax                      ; (the column id, kept for the skip pair below)
    mov cl, 7
    shl ax, cl
    add ax, XV_BAND
    mov dx, ax                      ; the band strip
    add bp, 40 * 64                 ; line 64 of the column (BP is still line 0: xv_runs keeps it)
    mov ax, exb_full_band
    cmp byte [cs:xv_skip], 0
    je .bandrun
    mov ax, di
    cmp al, 2
    ja .bandfull
    mov ah, [cs:bx + xb_cid - 40]   ; the column that left this cell's row
    cmp ah, 2
    ja .bandfull
    ; pair index = 3 * entering + leaving
    mov bl, al
    shl al, 1
    add al, bl
    add al, ah
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov ax, [cs:bx + exb_skip_band]
    jmp .bandrun
.bandfull:
    mov ax, exb_full_band
.bandrun:
    mov bx, ax
    call xv_runs
    ; Line 192 of the page is not in the window, but the card shows it for one
    ; scan line when the split starts a line after the compare value (QEMU's VGA,
    ; and probably the real one: MartyPC starts the split ON it).  Keep it black:
    ; the cell is (line 192, x) = (line 191, x + 40), rewritten black by every
    ; entering column, and it is a cell of a column that has left when x + 40 enters.
    mov si, XV_BLACK
    lea di, [bp + 40 * 128]         ; BP = line 64: line 192 of the column
    movsb
    ret

; -----------------------------------------------------------------------------
; xv_sync - bring the page at [xv_pbase] (index [xv_pg]) up to [xb_S]: the
; columns that entered since it was last current, or the whole 40-column window
; when it is new or the jump is a screen or more (no skip: its cells are
; anybody's).  Write mode 1: the latches are the copy.
; -----------------------------------------------------------------------------
xv_sync:
    push ds
    XV_GC 5, 1
    mov ax, [cs:xb_fsi + FSI_SEG]
    mov ds, ax
    mov es, ax
    mov ax, [cs:xb_S]
    xor bh, bh
    mov bl, [cs:xv_pg]
    shl bx, 1
    mov cx, [cs:xv_pgS + bx]
    mov [cs:xv_pgS + bx], ax
    mov byte [cs:xv_skip], 1
    cmp cx, 0ffffh
    je .full
    cmp ax, cx
    je .out
    jb .full
    mov dx, ax
    sub dx, cx
    cmp dx, 40
    jae .full
    add cx, 40                      ; the columns cx+40 .. S+39 entered
    jmp .go
.full:
    mov byte [cs:xv_skip], 0
    mov byte [cs:xb_bigframe], 1
    mov cx, ax
.go:
    add ax, 39
    mov [cs:xv_xlast], ax
.col:
    mov ax, cx
    push cx
    call xv_col
    cmp byte [cs:xv_skip], 0
    jne .nosamp
    call xb_clk_sample              ; a full window write (no skip lists: a jump, the first frames) is
.nosamp:                            ; 40 columns of ~6k: keep the sub-tick clock seen.  The steady state's
    pop cx                          ; few columns (skip lists) are far under a sub-tick and pay nothing
    inc cx
    cmp cx, [cs:xv_xlast]
    jbe .col
.out:
    pop ds
    ret

; -----------------------------------------------------------------------------
; xv_erase - put back what this page's sprites covered: for each footprint the
; world under it (top strip lines, band strip lines) latch-copied over it.
; A footprint is (word first column, byte first line, byte columns).  The line
; arithmetic is done ONCE a footprint; a column then costs a shift or two and the
; copies (~150 clocks + 37 a line).
; -----------------------------------------------------------------------------
xv_erase:
    push ds
    mov bl, [cs:xv_pg]
    xor bh, bh
    mov cl, [cs:xv_fpn + bx]
    or cl, cl
    jz .out
    mov byte [cs:xv_fpn + bx], 0
    mov [cs:xv_left], cl
    XV_GC 5, 1
    mov ax, [cs:xb_fsi + FSI_SEG]
    mov ds, ax
    mov es, ax
    mov si, bx
    mov cl, 5                       ; 32 bytes a page: eight footprints of four bytes
    shl si, cl
    add si, xv_fp
    mov [cs:xv_fpp], si
.fp:
    test byte [cs:xv_left], 1       ; every second footprint (~10k clocks): a page with one pays nothing
    jnz .nosamp
    call xb_clk_sample
.nosamp:
    mov si, [cs:xv_fpp]
    mov ax, [cs:si]
    mov [cs:xv_x], ax
    xor ah, ah
    mov al, [cs:si + 2]
    mov [cs:xv_y], ax
    mov al, [cs:si + 3]
    mov [cs:xv_nw], al
    add word [cs:xv_fpp], 4
    ; the lines of the top strip: min(24, 64 - y) from line y; of the band:
    ; from max(y, 64) to y + 24
    mov ax, [cs:xv_y]
    xor cx, cx
    cmp ax, 64
    jae .notop
    mov cx, 64
    sub cx, ax
    cmp cx, XB_POSE_H
    jbe .notop
    mov cx, XB_POSE_H
.notop:
    mov [cs:xv_nt], cx              ; 0 when the footprint starts in the band
    mov ax, [cs:xv_y]
    mov bx, ax
    add bx, XB_POSE_H               ; the end line
    mov dx, 64
    cmp ax, dx
    jae .b0
    mov ax, dx
.b0:                                ; AX = the first band line
    sub bx, ax
    ja .nb
    xor bx, bx
.nb:
    mov [cs:xv_nb], bx
    mov [cs:xv_b0], ax
    ; destination bases (line 0 of the column x = 0) and source offsets
    mov ax, [cs:xv_y]
    mov cx, 40
    mul cx
    add ax, [cs:xv_pbase]
    mov [cs:xv_dt], ax
    mov ax, [cs:xv_b0]
    mov cx, 40
    mul cx
    add ax, [cs:xv_pbase]
    mov [cs:xv_db], ax
.column:
    mov bx, [cs:xv_x]
    cmp word [cs:xv_nt], 0
    je .band
    mov ax, bx
    and al, XB_TOPW - 1
    mov ah, al                      ; (x mod 64) << 6 = (x mod 64) << 8 >> 2
    xor al, al
    shr ax, 1
    shr ax, 1
    add ax, XV_TOP
    add ax, [cs:xv_y]
    mov si, ax
    mov di, [cs:xv_dt]
    add di, bx
    mov cx, [cs:xv_nt]
    call xv_copy
.band:
    cmp word [cs:xv_nb], 0
    je .next
    mov bx, [cs:xv_x]
    mov ah, [cs:bx + xb_cid]        ; cid << 7 = cid << 8 >> 1
    xor al, al
    shr ax, 1
    add ax, XV_BAND - 64
    add ax, [cs:xv_b0]
    mov si, ax
    mov di, [cs:xv_db]
    add di, bx
    mov cx, [cs:xv_nb]
    call xv_copy
.next:
    inc word [cs:xv_x]
    dec byte [cs:xv_nw]
    jnz .column
    dec byte [cs:xv_left]
    jnz .fp
.out:
    pop ds
    ret

; -----------------------------------------------------------------------------
; xv_sprites - draw the bikes on this page and record each footprint.  Write
; mode 2 (the byte written is the colour, the bit mask register picks the
; layer's pixels of it): ONE OUT, then the XCHG's read loads the latches and
; its write puts the colour under the mask - ~85 clocks a (byte, layer) pair,
; ~96 pairs a bike.  WRITE MODE 3 (set/reset colour, byte = mask: no OUT a
; record, ~73) WAS THE PLAN AND IS NOT USED: MartyPC's VGA implements modes 0-2
; and treats 3 as `Invalid`, so the whole pixel gate would fail on the one
; emulator that counts cycles; mode 2 is standard on every VGA.
; -----------------------------------------------------------------------------
xv_sprites:
    push ds
    xor bp, bp                      ; bike index * 8
    cmp byte [cs:xb_nspr], 0
    je .out
    XV_GC 5, 2
.bike:
    cmp byte [cs:xb_bikes + bp], 0
    je .next
    test bp, bp
    jz .draw
    call xb_clk_sample              ; a bike is ~19k clocks: before every one but the first, keep the
.draw:                              ; sub-tick clock seen (a lone rider pays nothing)
    call xv_bike
.next:
    add bp, XB_BIKE_SIZE
    cmp bp, XB_MAXBIKES * XB_BIKE_SIZE
    jb .bike
    XV_GC 5, 0
    XV_GC 8, 0ffh                   ; the bit mask back: the HUD's plain writes use it
.out:
    pop ds
    ret

; BP = the bike's record offset (preserved)
xv_bike:
    push bp
    mov al, [cs:xb_bikes + bp + 6]  ; the rider's colour is a per-bike variable
    mov [cs:xv_ink + 1], al
    mov ax, [cs:xb_bikes + bp + 2]  ; x pixels; even phases only
    and ax, 0fffeh
    mov dx, ax
    and dx, 7                       ; DX = the phase 0,2,4,6
    mov cl, 3
    sar ax, cl                      ; AX = the screen column (negative: hanging off the left edge)
    mov bx, 3                       ; bytes wide: 24 pixels, 32 when shifted
    test dx, dx
    jz .w
    inc bx
.w:
    mov cx, ax
    add cx, bx                      ; one past its last column
    jle .cull                       ; wholly left of the page
    cmp ax, 40
    jge .cull                       ; wholly right of it
    cmp word [cs:xb_bikes + bp + 4], XB_WORLD_ROWS * 8 - XB_POSE_H
    ja .cull                        ; nor one that would reach the HUD's lines
    ; the part of it on the page (SPEC.md 102.7.4): the footprint is the columns [max(ax,0), min(cx,40))
    mov [cs:xv_c0], ax
    mov byte [cs:xv_edge], 0
    test ax, ax
    js .edge
    cmp cx, 40
    jbe .inside
.edge:
    mov byte [cs:xv_edge], 1
.inside:
    mov bx, cx                      ; BX = one past the last column, clipped
    cmp bx, 40
    jbe .hi
    mov bx, 40
.hi:
    mov cx, ax                      ; CX = the first column, clipped
    test cx, cx
    jns .lo
    xor cx, cx
.lo:
    sub bx, cx                      ; BL = the footprint's width
    add ax, [cs:xb_S]               ; the world column of the sprite's byte 0 (the page column)
    mov [cs:xv_x0], ax
    add cx, [cs:xb_S]
    mov [cs:xv_x], cx               ; ...and of the footprint's first
    mov [cs:xv_nw], bl
    mov ax, [cs:xb_bikes + bp + 4]
    mov [cs:xv_y], ax
    mov al, [cs:xb_bikes + bp + 1]  ; the (pose, phase) blob
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shr dx, 1
    add ax, dx
    shl ax, 1
    mov bx, ax                      ; index * 2 into the sprite claim's table
    call xv_fp_add
    mov ax, [cs:xv_y]
    mov cx, 40
    mul cx
    add ax, [cs:xv_pbase]
    add ax, [cs:xv_x0]              ; (the sprite's byte 0: off the page's edge for an edge rider, which
    mov di, ax                      ; never writes there: xv_bike's clipping loop skips those records)
    mov ax, [cs:xb_fsi + FSI_SEG]
    mov es, ax
    cmp byte [cs:xv_edge], 0
    jne .interp                     ; (an edge rider takes the clipping loop below)
    mov ax, [cs:xs_ctab + bx]       ; a hot pose has its code (sprite.inc's xs_compile_v): the same writes
    test ax, ax                     ; as the record loop below, ~45 clocks a (byte, layer) pair against ~85
    jz .interp
    mov [cs:xs_cfar], ax
    mov ax, [cs:xb_sprseg]
    mov [cs:xs_cfar + 2], ax
    mov dx, 3ceh
    mov al, 8                       ; GC index 8 = bit mask; the code writes its data port a record at a time
    out dx, al
    inc dx
    mov bl, [cs:xv_ink + 1]         ; BL = the rider's colour (layer 1); the other layers' are in the code
    call far [cs:xs_cfar]
    jmp .cull
.interp:
    mov ds, [cs:xb_sprseg]
    mov si, [bx]                    ; the blob
    mov byte [cs:xv_lay], 0
.layer:
    lodsw                           ; the layer's record count
    or ax, ax
    jz .empty
    mov cx, ax
    mov bl, [cs:xv_lay]
    xor bh, bh
    mov bl, [cs:xv_ink + bx]
    xor bh, bh
    mov bp, bx                      ; BP = the layer's colour
    mov dx, 3ceh
    mov al, 8                       ; GC index 8 = bit mask; the data port then takes
    out dx, al                      ; one byte a record
    inc dx
    cmp byte [cs:xv_edge], 0
    jne .recc
.rec:
    lodsw                           ; the byte's offset from the box's first byte
    mov bx, ax
    lodsb                           ; the layer's mask there
    out dx, al                      ; bit mask = the layer's pixels in this byte
    mov ax, bp
    xchg al, [es:bx + di]           ; the read loads the latches, the write puts the
    loop .rec                       ; colour under the mask (write mode 2)
    jmp .empty
.recc:                              ; ...an edge rider: the same, a record at a time skipped when its byte
    lodsw                           ; is not on the page.  An offset is 40 x row + byte and 40 is a multiple
    mov bx, ax                      ; of four, so the byte (0..3) is offset & 3
    lodsb
    mov [cs:xv_msk], al
    mov ax, bx
    and ax, 3
    add ax, [cs:xv_c0]
    cmp ax, 40
    jae .skip                       ; (a negative column is a large unsigned one)
    mov al, [cs:xv_msk]
    out dx, al
    mov ax, bp
    xchg al, [es:bx + di]
.skip:
    loop .recc
.empty:
    inc byte [cs:xv_lay]
    cmp byte [cs:xv_lay], 4
    jb .layer
    push cs
    pop ds
.cull:
    pop bp
    ret

; append (column, line, bytes) to this page's footprint list
xv_fp_add:
    push bx
    mov bl, [cs:xv_pg]
    xor bh, bh
    mov cl, [cs:xv_fpn + bx]
    xor ch, ch
    inc byte [cs:xv_fpn + bx]
    mov di, bx
    push cx
    mov cl, 5
    shl di, cl
    pop cx
    shl cx, 1
    shl cx, 1
    add di, cx
    add di, xv_fp
    mov ax, [cs:xv_x]
    mov [cs:di], ax
    mov al, [cs:xv_y]
    mov [cs:di + 2], al
    mov al, [cs:xv_nw]
    mov [cs:di + 3], al
    pop bx
    ret

; -----------------------------------------------------------------------------
; xv_hud - the cells of the 20-character HUD string that changed since they
; were last drawn, into the fixed row at address 0 (the split shows it for every
; page).  Plain write mode 0, all four planes: ink 15 on 0.
; -----------------------------------------------------------------------------
xv_hud:
    mov al, [cs:xb_huddirty]
    or al, al
    je .out
    mov byte [cs:xb_huddirty], 0
    push es
    mov cx, XB_HUDLEN
    xor bx, bx
    cmp al, 2
    je .go
    mov cx, XB_HUDDIG1              ; a clock tick changes the digits only
    mov bx, XB_HUDDIG0
.go:
    mov dx, cx                      ; DX = the last cell + 1 (xb_glyph leaves it alone)
    mov ax, [cs:xb_fsi + FSI_SEG]
    mov es, ax
.ch:
    mov al, [xb_hudstr + bx]
    cmp al, [xb_hudcur + bx]
    je .next
    mov [xb_hudcur + bx], al
    sub al, 32
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add ax, xb_font
    mov si, ax
    lea di, [bx + XV_HUD + 40 + XB_HUDCOL] ; glyph row 0 is on the HUD's second line
    call xb_glyph                   ; 8 rows of `movsb / add di,39`
.next:
    inc bx
    cmp bx, dx
    jb .ch
    pop es
.out:
    ret

; -----------------------------------------------------------------------------
; xv_flip - the page just built becomes the one the next retrace shows.  One
; register pair, no wait (see the three-page argument at the top).  The two
; OUTs are 32 clocks apart, ~6 microseconds against a 14 ms frame: a latch
; falling between them would show one frame from the wrong page, which is the
; cost of not spinning for the retrace.
; -----------------------------------------------------------------------------
xv_flip:
    mov ax, [xv_pbase]
    add ax, [xb_S]
    mov [xb_shown], ax
    mov cx, ax
    mov dx, 3d4h
    cmp ch, [xv_hi]
    je .lo                          ; the high byte is unchanged: one OUT, nothing to tear
    mov [xv_hi], ch
    mov al, 0ch
    mov ah, ch
    out dx, ax
.lo:
    mov al, 0dh
    mov ah, cl
    out dx, ax
    mov al, [xv_pg]
    mov [xb_shownpg], al
    mov ax, [xb_S]
    mov [xb_shownS], ax
    ret

; the frame: next page, erase, columns, sprites, HUD, flip
xv_render:
    mov al, [xv_pg]
    inc al
    cmp al, 3
    jb .pg
    xor al, al
.pg:
    mov [xv_pg], al
    xor ah, ah
    mov bx, XV_PAGE_SZ
    mul bx
    add ax, XV_PAGE0
    mov [xv_pbase], ax
; (the labels xv_t0..xv_t4 are the measurement points of tests/excitebike_perf.py:
; a breakpoint costs the guest no cycle, and the gaps are erase, columns,
; sprites, HUD and flip)
xv_t0:
    call xv_erase
xv_t1:
    call xv_sync
xv_t2:
    call xv_sprites
xv_t3:
    XV_GC 5, 0
    call xv_hud
xv_t4:
xb_present:
    call xv_flip
    ret
