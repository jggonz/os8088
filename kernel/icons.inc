; =============================================================================
; os8088 - icons.inc (SPEC.md 25)
;
; 1-bit icons with a mask, classic Mac style, drawn exactly like the mouse
; cursor (mouse.inc cur_draw/cur_pass): a white underlay pass using the mask
; rows as Bit Mask bits with Set/Reset = white, then a black pass from the
; data rows - generalized from the cursor's fixed 1-word x 16-row cell to
; wwords words per row and height rows.
;
; In-memory record:
;   db wwords        ; row width in words (1 = 16px, 2 = 32px)
;   db height        ; rows
;   ; height x wwords mask words, then height x wwords data words
;   ; bit 15 = leftmost pixel of the word's 16px span; words left -> right
;
; Each 16-bit source word lands at bit offset (x & 7) inside a 24-bit window
; spanning three VRAM bytes (same shifted-window trick as font.inc, widened
; by one byte): B0:B1 = row >> CL, B2 = low bits << (8-CL). Adjacent words
; of one row overlap on the shared middle byte with disjoint masks, so two
; masked writes compose correctly. A read of each target byte loads the
; latches before the masked write, so unmasked pixels survive.
;
; Clipping: rows are clipped to the screen (0..479) - icons drawn inside
; windows can hang off the bottom edge; byte columns past the right screen
; edge (>= ROW_BYTES) are skipped defensively. Leaves the GC in the default
; state (SPEC 1 rule 7). Caller holds the gfx lock.
; =============================================================================

; -----------------------------------------------------------------------------
; icon_draw - draw a full icon record (header + mask + data)
;
; in:       CX = x, DX = y (top-left, screen px), SI -> record;
;           caller holds the gfx lock
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
icon_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov al, [si]                ; header: wwords, height
    mov ah, 0
    mov [ico_ww], ax
    mov al, [si+1]              ; AH still 0
    mov [ico_h], ax
    add si, 2                   ; SI -> body (mask rows first)
    GFXDENTERCD                 ; whole-shape, like a glyph (SPEC.md 39.14.2)
    call ico_core
    GFXDLEAVE
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; icon_draw16 - draw a header-less 16x16 icon body (16 mask words + 16 data
;               words) - the harvested-icon / embedded-icon layout
;               (SPEC 19 / 20.2).
;
; in:       CX = x, DX = y, SI -> body; caller holds the gfx lock
; out:      nothing (all registers preserved)
; -----------------------------------------------------------------------------
icon_draw16:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov word [ico_ww], 1
    mov word [ico_h], 16
    GFXDENTERCD                 ; whole-shape, like a glyph (SPEC.md 39.14.2)
    call ico_core
    GFXDLEAVE
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ico_core - shared body: clip, set up the GC, run the white (mask) pass and
;            the black (data) pass, restore GC defaults.
;
; in:       CX = x, DX = y, SI -> body, [ico_ww]/[ico_h] set
; clobbers: everything but DS (wrappers restore); caller holds the gfx lock
; -----------------------------------------------------------------------------
ico_core:
    call fnt_unlazy             ; whole-shape clipped like a glyph (7.1.4)
    mov [ico_x], cx
    mov [ico_y], dx

    cmp word [wm_clip_n], 0     ; whole-icon against a background task's clip
    je .noclip                  ; region, the font_char rule (SPEC.md 11.3):
    mov ax, cx                  ; an icon is one shape or none
    mov bx, dx
    mov cx, [ico_ww]            ; CX = 16 * wwords = the body's pixel width
    shl cx, 1                   ; (a "word" of an icon row is 16 pixels, not
    shl cx, 1                   ; 8 - four shifts, not three)
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx
    mov dx, [ico_h]
    add dx, bx
    dec dx
    call wm_clip_test
    mov cx, [ico_x]
    mov dx, [ico_y]
    jc .none
.noclip:

    mov ax, [ico_ww]            ; source/VRAM stride bookkeeping
    shl ax, 1
    mov [ico_rb], ax            ; bytes per icon row (= 2 * wwords)
    mov bx, [vid_stride]
    sub bx, ax
    mov [ico_rowadv], bx        ; DI advance after a row's words (the planar
                                ; pass only - the banked one goes through
                                ; gfx_nextrow instead)

    mov [ico_maskp], si         ; mask rows first, data rows after
    mov ax, [ico_ww]
    mul word [ico_h]            ; AX = words per table (DX clobbered)
    shl ax, 1
    add ax, si
    mov [ico_datap], ax

    ; --- vertical clip to 0..[vid_chm1] (y is signed here) --------------------
    mov bx, [ico_h]             ; BX = rows to draw
    mov ax, [ico_y]
    or  ax, ax
    jns .top_ok
    neg ax                      ; AX = rows clipped off the top
    cmp ax, bx
    jae .none                   ; fully above the screen
    sub bx, ax
    mul word [ico_rb]           ; AX = bytes to skip in both tables
    add [ico_maskp], ax
    add [ico_datap], ax
    mov word [ico_y], 0
.top_ok:
    mov ax, [ico_y]
    cmp ax, [vid_ch]
    jae .none                   ; fully below the screen
    add ax, bx
    cmp ax, [vid_ch]
    jbe .rows_ok
    mov bx, [vid_ch]
    sub bx, [ico_y]             ; hang off the bottom edge: draw what fits
.rows_ok:
    mov [ico_rows], bx

    ; --- horizontal setup: byte column, shift pair, VRAM offset --------------
    ; A base column >= the row stride (x past the right edge, or negative x
    ; seen as huge unsigned) makes ico_pass skip every byte write.
    mov bx, [ico_x]
    mov ax, bx
    mov cl, 3
    shr ax, cl
    mov [ico_col0], ax          ; base byte column (x / 8)
    mov di, ax
    mov ax, [ico_y]             ; AX = the row's base offset (DX clobbered;
    call gfx_rowbase            ; CX is loaded below, BX is preserved)
    add di, ax                  ; DI = rowbase(y) + x/8
    mov cl, bl
    and cl, 7                   ; CL = shift (x & 7)
    mov ch, 8
    sub ch, cl                  ; CH = 8 - shift

    cmp byte [vid_mono], 0      ; 1bpp? (SPEC.md 39.5) - the software
    jne ico_core_bb             ; twin returns straight to the wrapper

    mov ax, VGA_SEG
    mov es, ax

    mov dx, 0x3CE               ; GC1: enable Set/Reset on all planes
    mov ax, 0x0F01
    out dx, ax
    mov ah, CWHITE              ; GC0: Set/Reset color = white
    mov al, 0
    out dx, ax
    mov al, 8                   ; leave the index at GC8 (Bit Mask)
    out dx, al
    mov si, [ico_maskp]
    push di
    call ico_pass               ; white underlay from the mask rows
    pop di

    mov dx, 0x3CE
    mov ah, CBLACK              ; GC0: Set/Reset color = black
    mov al, 0
    out dx, ax
    mov al, 8
    out dx, al
    mov si, [ico_datap]
    call ico_pass               ; black pixels on top

    mov dx, 0x3CE               ; back to defaults: Set/Reset off, mask FF
    mov ax, 0x0001
    out dx, ax
    mov ax, 0xFF08
    out dx, ax
.none:
    ret

; -----------------------------------------------------------------------------
; ico_pass - one masked pass over [ico_rows] rows of [ico_ww] words each.
;            Each word lands at bit offset CL in a 3-byte window at DI+2w;
;            zero mask bytes and byte columns past the row end are skipped.
;
; in:       SI -> word rows, DI = VRAM offset of the row's first byte,
;           CL = shift, CH = 8-shift, ES = VGA_SEG,
;           GC: Set/Reset enabled + color set, index register = 8
; outs:     none; preserves CX, DI; clobbers ax, bx, dx, si, bp
; -----------------------------------------------------------------------------
ico_pass:
    push di
    mov bp, [ico_rows]
    or  bp, bp
    jz .done                    ; degenerate record / fully clipped
    mov dx, 0x3CF               ; GC data port; index already 8 (Bit Mask)
.row:
    mov ax, [ico_col0]
    mov [ico_col], ax           ; running byte column (right-edge clip)
    mov ax, [ico_ww]
    mov [ico_wleft], ax
.word:
    lodsw                       ; AX = row word, MSB = leftmost pixel
    mov bx, ax
    shr bx, cl                  ; BH:BL = B0:B1
    xchg cl, ch
    shl ax, cl                  ; AL = B2 (low bits spilling into byte 3)
    xchg cl, ch
    mov ah, al                  ; stash B2

    mov al, bh                  ; --- byte 0, column [ico_col] ---
    or  al, al
    jz .b1
    cmp word [ico_col], ROW_BYTES
    jae .b1
    out dx, al
    mov al, [es:di]             ; load latches
    mov [es:di], al
.b1:
    mov al, bl                  ; --- byte 1, column +1 ---
    or  al, al
    jz .b2
    cmp word [ico_col], ROW_BYTES-1
    jae .b2
    out dx, al
    mov al, [es:di+1]
    mov [es:di+1], al
.b2:
    mov al, ah                  ; --- byte 2, column +2 ---
    or  al, al
    jz .wnext
    cmp word [ico_col], ROW_BYTES-2
    jae .wnext
    out dx, al
    mov al, [es:di+2]
    mov [es:di+2], al
.wnext:
    add di, 2                   ; next word of this row
    add word [ico_col], 2
    dec word [ico_wleft]
    jnz .word
    add di, [ico_rowadv]        ; next scanline (80 - 2*wwords)
    dec bp
    jnz .row
.done:
    pop di
    ret

; -----------------------------------------------------------------------------
; ico_core_bb - the software renderer's icon body (SPEC.md 32/39.5): the white underlay
;               ORs the shifted mask-row bits into all four planes, the
;               black pass AND-NOTs the data-row bits out of them - color
;               15 under the mask, 0 under the data, like the VRAM passes.
; in:       DI = row offset, CL/CH = shift pair, ico_* vars set by ico_core
; out:      returns to icon_draw/icon_draw16's epilogue (ico_core contract)
; clobbers: everything but DS (the wrappers restore)
; -----------------------------------------------------------------------------
ico_core_bb:
    mov ax, [ico_col0]
    cmp ax, [vid_stride]        ; x < 0 (huge unsigned) or entirely off the
    jae .none                   ; right edge: the passes would skip every
                                ; byte - skip them and the dirty union too
    mov bx, [ico_ww]            ; dirty: columns col0..col0+2*ww (the last
    shl bx, 1                   ; word's 3-byte window), clamped to the row
    add bx, ax
    cmp bx, [vid_stride]
    jb .rok
    mov bx, [vid_strm1]
.rok:
    mov al, [vid_strm1]         ; the three right-edge limits ico_pass_bb
    mov [ico_lim1], al          ; needs as BYTES - see the compares there
    dec al
    mov [ico_lim2], al
    mov al, [vid_stride]
    mov [ico_lim0], al

    mov byte [ico_bbop], 0      ; white underlay: OR the mask rows in
    mov si, [ico_maskp]
    call ico_pass_bb
    mov byte [ico_bbop], 1      ; black pixels: AND-NOT the data rows out
    mov si, [ico_datap]
    call ico_pass_bb
.none:
    ret

; -----------------------------------------------------------------------------
; ico_pass_bb - one masked pass over every renderer plane: the same
;               3-byte shifted window and edge clipping as ico_pass, with
;               [ico_bbop] picking OR (0) or AND-NOT (1) per byte.
; in:       SI -> word rows, DI = row offset, CL = shift, CH = 8-shift,
;           [ico_bbop] = 0/1, ico_* vars set
; out:      none; preserves CX, DI; clobbers ax, bx, dx, si, bp, es
; -----------------------------------------------------------------------------
ico_pass_bb:
    push di
    cmp word [ico_rows], 0
    je .done                    ; degenerate record / fully clipped
    mov [ico_bbsrc], si         ; per-plane restart points
    mov [ico_bbdi], di
    mov dx, [vid_rseg]          ; DX = plane segment - the framebuffer on
    mov al, [vid_planes]        ; mono (SPEC.md 39.3)
    mov [ico_bbpl], al
.plane:
    mov es, dx
    mov si, [ico_bbsrc]
    mov di, [ico_bbdi]
    mov bp, [ico_rows]
.row:
    mov al, [ico_col0]          ; running byte column (right-edge clip); a
    mov [ico_bcol], al          ; byte - ico_core_bb proved it is below the
                                ; stride, which is 90 at most
    mov ax, [ico_ww]
    mov [ico_wleft], ax
.word:
    lodsw                       ; AX = row word, MSB = leftmost pixel
    mov bx, ax
    shr bx, cl                  ; BH:BL = B0:B1
    xchg cl, ch
    shl ax, cl                  ; AL = B2 (low bits spilling into byte 3)
    xchg cl, ch
    mov ah, al                  ; stash B2

    mov al, bh                  ; --- byte 0, column [ico_col] ---
    or  al, al
    jz .b1
    mov bh, [ico_lim0]          ; BH is dead from here - .b1 wants only BL
    cmp [ico_bcol], bh
    jae .b1
    call ico_bbop_byte
.b1:
    mov al, bl                  ; --- byte 1, column +1 ---
    or  al, al
    jz .b2
    mov bl, [ico_lim1]          ; BL is dead from here - .b2 reads AH
    cmp [ico_bcol], bl
    jae .b2
    inc di
    call ico_bbop_byte
    dec di
.b2:
    mov al, ah                  ; --- byte 2, column +2 ---
    or  al, al
    jz .wnext
    mov bl, [ico_lim2]          ; BX is entirely dead from here
    cmp [ico_bcol], bl
    jae .wnext
    inc di
    inc di
    call ico_bbop_byte
    dec di
    dec di
.wnext:
    add di, 2                   ; next word of this row
    add byte [ico_bcol], 2
    dec word [ico_wleft]
    jnz .word
    sub di, [ico_rb]            ; back to the row's first byte, then down one
    call gfx_nextrow            ; scan line the adapter's way (SPEC.md 39.3)
    dec bp
    jnz .row
    add dx, [vid_rpara]
    dec byte [ico_bbpl]
    jnz .plane
.done:
    pop di
    ret

; -----------------------------------------------------------------------------
; ico_bbop_byte - internal: apply [ico_bbop] to [es:di] with mask AL
;                 (0 = or in, 1 = and-not out); AH and all else preserved
; -----------------------------------------------------------------------------
ico_bbop_byte:
    cmp byte [ico_bbop], 0
    jne .clr
    or [es:di], al
    ret
.clr:
    not al
    and [es:di], al
    ret

; -----------------------------------------------------------------------------
; ico_disk32 - 32x32 Mac-style 3.5" floppy. Black data = 1px body outline,
;              shutter with cutout window, label rectangle; mask = the solid
;              disk silhouette so the desktop dither never bleeds through.
;
; data (black pixels), # = set:            rows
;   ..############################..        0      top edge
;   ..#......#............#......#..        1      body + shutter sides
;   ..#......#..####......#......#..        2..8    + cutout window (filled)
;   ..#......#............#......#..        9
;   ..#......##############......#..       10      shutter bottom
;   ..#..........................#..       11..14
;   ..#..######################..#..       15      label top
;   ..#..#....................#..#..       16..30  label sides
;   ..############################..       31      bottom edge
;
; mask (white underlay), every row:
;   ..############################..        0..31   solid silhouette
; -----------------------------------------------------------------------------
ico_disk32:
    db 2, 32                    ; wwords, height

    ; --- mask: 32 rows x 2 words, solid silhouette x=2..29 -------------------
    times 32 dw 0x3FFF, 0xFFFC

    ; --- data: 32 rows x 2 words ---------------------------------------------
    dw 0x3FFF, 0xFFFC           ;  0  ..############################..
    dw 0x2040, 0x0204           ;  1  ..#......#............#......#..
    dw 0x204F, 0x0204           ;  2  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  3  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  4  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  5  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  6  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  7  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  8  ..#......#..####......#......#..
    dw 0x2040, 0x0204           ;  9  ..#......#............#......#..
    dw 0x207F, 0xFE04           ; 10  ..#......##############......#..
    dw 0x2000, 0x0004           ; 11  ..#..........................#..
    dw 0x2000, 0x0004           ; 12  ..#..........................#..
    dw 0x2000, 0x0004           ; 13  ..#..........................#..
    dw 0x2000, 0x0004           ; 14  ..#..........................#..
    dw 0x27FF, 0xFFE4           ; 15  ..#..######################..#..
    dw 0x2400, 0x0024           ; 16  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 17  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 18  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 19  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 20  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 21  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 22  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 23  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 24  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 25  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 26  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 27  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 28  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 29  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 30  ..#..#....................#..#..
    dw 0x3FFF, 0xFFFC           ; 31  ..############################..

; -----------------------------------------------------------------------------
; ico_disk14 - the same diskette, 32 wide by 14 ROWS (SPEC.md 26.4)
;
; The CGA's pixels are 2.4:1 TALL - 640x200 on a 4:3 tube - so ico_disk32
; above renders 32 wide by about 77 high there: a stretched column, not a
; diskette. 14 rows is 33.6 real units against 32, which is square to within
; 5% and is also what a 3.5" diskette actually is (90 x 94mm).
;
; It is drawn and not squashed. Halving ico_disk32 by OR-ing row pairs was
; the cheap option and it thickens every 1px feature into 2 - the shutter
; window, the label box and the two vertical rules are all single lines, so
; what comes out is a blob with the diskette's outline. Selecting alternate
; rows loses them instead. The shutter keeps 3 rows of window and the label
; box 5 of interior, which is the proportion the 32-row icon has.
; -----------------------------------------------------------------------------
ico_disk14:
    db 2, 14                    ; wwords, height

    ; --- mask: solid silhouette x=2..29, as the tall one ---------------------
    times 14 dw 0x3FFF, 0xFFFC

    ; --- data ----------------------------------------------------------------
    dw 0x3FFF, 0xFFFC           ;  0  ..############################..
    dw 0x2040, 0x0204           ;  1  ..#......#............#......#..
    dw 0x204F, 0x0204           ;  2  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  3  ..#......#..####......#......#..
    dw 0x204F, 0x0204           ;  4  ..#......#..####......#......#..
    dw 0x207F, 0xFE04           ;  5  ..#......##############......#..
    dw 0x2000, 0x0004           ;  6  ..#..........................#..
    dw 0x27FF, 0xFFE4           ;  7  ..#..######################..#..
    dw 0x2400, 0x0024           ;  8  ..#..#....................#..#..
    dw 0x2400, 0x0024           ;  9  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 10  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 11  ..#..#....................#..#..
    dw 0x2400, 0x0024           ; 12  ..#..#....................#..#..
    dw 0x3FFF, 0xFFFC           ; 13  ..############################..

; -----------------------------------------------------------------------------
; ico_hdd14 - the hard disk at the same 32 x 14 (SPEC.md 26.4)
;
; ico_hdd32's three stacked sections - platter, bays, feet - in the rows
; there are for them: one platter with its hub, one row of three bays, and
; the two feet that make it sit on a desk rather than float.
; -----------------------------------------------------------------------------
ico_hdd14:
    db 2, 14                    ; wwords, height

    ; --- mask: the case is solid, the feet are not ---------------------------
    dw 0x0000, 0x0000           ;  0
    times 12 dw 0x3FFF, 0xFFFC  ;  1..12  the case
    dw 0x0C00, 0x0030           ; 13      the feet

    ; --- data ----------------------------------------------------------------
    dw 0x0000, 0x0000           ;  0  ................................
    dw 0x3FFF, 0xFFFC           ;  1  ..############################..
    dw 0x2000, 0x0004           ;  2  ..#..........................#..
    dw 0x23FF, 0xFFC4           ;  3  ..#...####################...#..
    dw 0x2400, 0x0024           ;  4  ..#..#....................#..#..
    dw 0x241F, 0xF824           ;  5  ..#..#.....##########.....#..#..
    dw 0x2400, 0x0024           ;  6  ..#..#....................#..#..
    dw 0x23FF, 0xFFC4           ;  7  ..#...####################...#..
    dw 0x3FFF, 0xFFFC           ;  8  ..############################..
    dw 0x2000, 0x0004           ;  9  ..#..........................#..
    dw 0x27E7, 0xE7E4           ; 10  ..#..######..######..######..#..
    dw 0x27E7, 0xE7E4           ; 11  ..#..######..######..######..#..
    dw 0x3FFF, 0xFFFC           ; 12  ..############################..
    dw 0x0C00, 0x0030           ; 13  ....##....................##....

; -----------------------------------------------------------------------------
; ico_app16 - 16x16 generic application: a diamond outline with a filled
;             square at the center; mask = the solid diamond silhouette.
;
; data (black pixels):                mask (white underlay):
;   .......##.......                    .......##.......
;   ......#..#......                    ......####......
;   .....#....#.....                    .....######.....
;   ....#......#....                    ....########....
;   ...#........#...                    ...##########...
;   ..#..........#..                    ..############..
;   .#....####....#.                    .##############.
;   #.....####.....#                    ################
;   #.....####.....#                    ################
;   .#....####....#.                    .##############.
;   ..#..........#..                    ..############..
;   ...#........#...                    ...##########...
;   ....#......#....                    ....########....
;   .....#....#.....                    .....######.....
;   ......#..#......                    ......####......
;   .......##.......                    .......##.......
; -----------------------------------------------------------------------------
ico_app16:
    db 1, 16                    ; wwords, height

    ; --- mask: solid diamond --------------------------------------------------
    dw 0x0180                   ;  0  .......##.......
    dw 0x03C0                   ;  1  ......####......
    dw 0x07E0                   ;  2  .....######.....
    dw 0x0FF0                   ;  3  ....########....
    dw 0x1FF8                   ;  4  ...##########...
    dw 0x3FFC                   ;  5  ..############..
    dw 0x7FFE                   ;  6  .##############.
    dw 0xFFFF                   ;  7  ################
    dw 0xFFFF                   ;  8  ################
    dw 0x7FFE                   ;  9  .##############.
    dw 0x3FFC                   ; 10  ..############..
    dw 0x1FF8                   ; 11  ...##########...
    dw 0x0FF0                   ; 12  ....########....
    dw 0x07E0                   ; 13  .....######.....
    dw 0x03C0                   ; 14  ......####......
    dw 0x0180                   ; 15  .......##.......

    ; --- data: diamond outline + center square --------------------------------
    dw 0x0180                   ;  0  .......##.......
    dw 0x0240                   ;  1  ......#..#......
    dw 0x0420                   ;  2  .....#....#.....
    dw 0x0810                   ;  3  ....#......#....
    dw 0x1008                   ;  4  ...#........#...
    dw 0x2004                   ;  5  ..#..........#..
    dw 0x43C2                   ;  6  .#....####....#.
    dw 0x83C1                   ;  7  #.....####.....#
    dw 0x83C1                   ;  8  #.....####.....#
    dw 0x43C2                   ;  9  .#....####....#.
    dw 0x2004                   ; 10  ..#..........#..
    dw 0x1008                   ; 11  ...#........#...
    dw 0x0810                   ; 12  ....#......#....
    dw 0x0420                   ; 13  .....#....#.....
    dw 0x0240                   ; 14  ......#..#......
    dw 0x0180                   ; 15  .......##.......

; -----------------------------------------------------------------------------
; ico_hdd32 - 32x32 hard disk: the drive body, a platter face and two feet.
;             What a driver-backed volume gets on the desktop (SPEC.md 26),
;             so a hard disk never reads as a floppy that will not eject.
;
; data (black pixels), # = set:            rows
;   ..............................         0
;   ..############################..       1      case top
;   ..#..........................#..       2
;   ..#...####################...#..       3      platter top
;   ..#..#....................#..#..       4
;   ..#..#...##############...#..#..       5      spindle ring top
;   ..#..#..#..............#..#..#..       6..11  the platter's face
;   ..#..#...##############...#..#..      12
;   ..#..#....................#..#..      13
;   ..#...####################...#..      14
;   ..#..........................#..      15
;   ..############################..      16     case rule
;   ..#..........................#..      17
;   ..#..######..######..######..#..      18     the three drive bays
;   ..#..######..######..######..#..      19..21
;   ..#..........................#..      22
;   ..############################..      23     case bottom
;   .....##................##.......      24..26 feet
;
; mask (white underlay), rows 1..23 solid, the feet's rows narrower - so the
; desktop dither never shows through the body and the feet still read.
; -----------------------------------------------------------------------------
ico_hdd32:
    db 2, 32                    ; wwords, height

    ; --- mask: the case is a solid silhouette, the feet are not -------------
    dw 0x0000, 0x0000
    times 23 dw 0x3FFF, 0xFFFC  ;  1..23  the case
    dw 0x0C00, 0x0030           ; 24     ....##....................##....
    dw 0x0C00, 0x0030           ; 25
    times 6 dw 0x0000, 0x0000   ; 26..31

    ; --- data ----------------------------------------------------------------
    dw 0x0000, 0x0000           ;  0  ................................
    dw 0x3FFF, 0xFFFC           ;  1  ..############################..
    dw 0x2000, 0x0004           ;  2  ..#..........................#..
    dw 0x23FF, 0xFFC4           ;  3  ..#...####################...#..
    dw 0x2400, 0x0024           ;  4  ..#..#....................#..#..
    dw 0x247F, 0xFE24           ;  5  ..#..#...##############...#..#..
    dw 0x2480, 0x0124           ;  6  ..#..#..#..............#..#..#..
    dw 0x2480, 0x0124           ;  7  ..#..#..#..............#..#..#..
    dw 0x2487, 0x8124           ;  8  ..#..#..#....####......#..#..#..
    dw 0x2487, 0x8124           ;  9  ..#..#..#....####......#..#..#..
    dw 0x2480, 0x0124           ; 10  ..#..#..#..............#..#..#..
    dw 0x2480, 0x0124           ; 11  ..#..#..#..............#..#..#..
    dw 0x247F, 0xFE24           ; 12  ..#..#...##############...#..#..
    dw 0x2400, 0x0024           ; 13  ..#..#....................#..#..
    dw 0x23FF, 0xFFC4           ; 14  ..#...####################...#..
    dw 0x2000, 0x0004           ; 15  ..#..........................#..
    dw 0x3FFF, 0xFFFC           ; 16  ..############################..
    dw 0x2000, 0x0004           ; 17  ..#..........................#..
    dw 0x27E7, 0xE7E4           ; 18  ..#..######..######..######..#..
    dw 0x2424, 0x2424           ; 19  ..#..#....#..#....#..#....#..#..
    dw 0x2424, 0x2424           ; 20  ..#..#....#..#....#..#....#..#..
    dw 0x27E7, 0xE7E4           ; 21  ..#..######..######..######..#..
    dw 0x2000, 0x0004           ; 22  ..#..........................#..
    dw 0x3FFF, 0xFFFC           ; 23  ..############################..
    dw 0x0C00, 0x0030           ; 24  ....##....................##....
    dw 0x0C00, 0x0030           ; 25  ....##....................##....
    times 6 dw 0x0000, 0x0000   ; 26..31


section .bss
ico_ww      resw 1              ; current record: words per row
ico_h       resw 1              ; current record: total rows
ico_rows    resw 1              ; rows after vertical clip
ico_rb      resw 1              ; bytes per icon row (2 * wwords)
ico_rowadv  resw 1              ; DI advance after a row (80 - 2*wwords)
ico_maskp   resw 1              ; -> first (unclipped) mask row
ico_datap   resw 1              ; -> first (unclipped) data row
ico_col0    resw 1              ; base byte column (x / 8)
ico_col     resw 1              ; running byte column inside ico_pass
ico_bcol    resb 1              ; ...and inside ico_pass_bb, where it must be
ico_lim0    resb 1              ; a byte; with the three right-edge limits it
ico_lim1    resb 1              ; is compared against (stride, stride-1,
ico_lim2    resb 1              ; stride-2), which must be bytes for the same
                                ; reason
ico_wleft   resw 1              ; words left in the current row
ico_x       resw 1              ; requested x
ico_y       resw 1              ; requested y, clamped to 0 after top clip
ico_bbsrc   resw 1              ; bb pass: row table, reloaded per plane
ico_bbdi    resw 1              ; bb pass: row offset, reloaded per plane
ico_bbop    resb 1              ; bb pass: 0 = or (white), 1 = and-not (black)
ico_bbpl    resb 1              ; bb pass: plane countdown
section .text
