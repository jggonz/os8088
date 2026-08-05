; =============================================================================
; font.inc - 8x8 text for mode 12h (SPEC.md 6)
;
; No font bytes are hard-coded: font_init asks the VGA BIOS for its ROM 8x8
; character set (int 10h AX=1130h BH=03h) and copies the printable range,
; glyphs 32..126, into a kernel buffer. Drawing uses Set/Reset with the Bit
; Mask so the background stays untouched (transparent): the glyph row is
; widened to a 16-bit window and shifted right by x&7, giving the masks for
; the one or two VRAM bytes the row lands on. A read of each byte loads the
; latches, so unmasked pixels are written back unchanged.
; =============================================================================

FONT_FIRST  equ 32              ; first glyph kept from the ROM set
FONT_LAST   equ 126             ; last  glyph kept
FONT_BYTES  equ (FONT_LAST - FONT_FIRST + 1) * 8    ; 95 glyphs x 8 rows = 760

; -----------------------------------------------------------------------------
; font_init - copy the ROM 8x8 font's printable range into RAM
;
; in:       nothing (must run after vid_init; uses int 10h)
; out:      nothing
; clobbers: nothing
; -----------------------------------------------------------------------------
font_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    xor ax, ax                  ; zero the answer first: on a pre-EGA BIOS
    mov es, ax                  ; AH=11h is not implemented and returns with
    xor bp, bp                  ; ES:BP untouched
    mov ax, 0x1130              ; get font information
    mov bh, 0x03                ; pointer wanted: ROM 8x8 character set
    int 0x10                    ; -> ES:BP = font table
    mov ax, es
    or ax, bp
    jnz .have
    mov ax, 0xF000              ; the IBM PC/XT ROM's own 8x8 set, which is
    mov es, ax                  ; where a CGA or Hercules machine gets it
    mov bp, 0xFA6E
.have:

    mov si, bp
    add si, FONT_FIRST * 8      ; skip the control-character glyphs
    mov di, font_glyphs
    mov cx, FONT_BYTES / 2
    mov dx, es                  ; ROM segment becomes the source
    mov ax, KERNEL_SEG
    mov es, ax                  ; destination: our .bss buffer
    mov ds, dx
    cld
    rep movsw

    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_char - draw one 8x8 glyph, transparent background
;
; in:       CX = x, DX = y (absolute screen px), AL = character,
;           color from [gfx_color]; caller holds the gfx lock
; out:      nothing
; clobbers: nothing
;
; Characters outside 32..126 draw nothing (the caller's pen still advances).
; Clipping is whole-char: if any part of the 8x8 cell would leave the screen
; the glyph is skipped entirely (acceptable v1 behavior per SPEC.md 6) - and,
; while a background task's clip region is armed, if any part of the cell
; would leave THAT (SPEC.md 11.3). A glyph is one shape or none; the region
; is the only thing here that could produce half of one, and half an 8x8
; glyph is unreadable anyway.
; -----------------------------------------------------------------------------
font_char:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp al, FONT_FIRST          ; unprintable -> space -> nothing to draw
    jb .done
    cmp al, FONT_LAST
    ja .done
    cmp cx, [vid_wm8]           ; unsigned: negative x fails this too
    ja .done
    cmp dx, [vid_hm8]
    ja .done
    cmp word [wm_clip_n], 0     ; the cell whole against the clip region too
    je .noclip                  ; (SPEC.md 11.3)
    push ax
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_test
    mov cx, ax                  ; MOV and POP leave CF alone
    mov dx, bx
    pop ax
    jc .done
.noclip:

    sub al, FONT_FIRST          ; SI = glyph row data
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, font_glyphs

    call bb_mono_chk            ; both modes - see gfx_fill (SPEC.md 32).
                                ; Placed after the clip so a glyph that draws
                                ; nothing cannot retire the flag.
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    je .vram
    call font_char_bb
    jmp .done
.vram:
    mov bx, cx                  ; BX = x; free CX for the shift count
    mov ax, ROW_BYTES
    mul dx                      ; AX = y * 80 (DX clobbered, product fits)
    mov di, ax
    mov ax, bx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax                  ; DI = y * 80 + x / 8
    mov cl, bl
    and cl, 7                   ; CL = x & 7, right shift for the 16-bit window

    mov ax, VGA_SEG
    mov es, ax

    mov dx, 0x3CE               ; GC index port
    mov ah, [gfx_color]
    mov al, 0
    out dx, ax                  ; GC0 Set/Reset = color
    mov ax, 0x0F01
    out dx, ax                  ; GC1 enable Set/Reset on all planes

    mov ch, 8                   ; row counter
.row:
    mov ah, [si]                ; glyph row in the high byte of the window
    inc si
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    mov bx, ax                  ; keep both masks in BH/BL

    or bh, bh                   ; anything landing in the first byte?
    jz .no_first
    mov al, 8
    mov ah, bh
    out dx, ax                  ; GC8 Bit Mask = first-byte glyph bits
    mov al, [es:di]             ; load latches
    mov [es:di], al             ; masked pixels <- Set/Reset color
.no_first:
    or bl, bl                   ; spill into the second byte?
    jz .no_second
    mov al, 8
    mov ah, bl
    out dx, ax                  ; GC8 Bit Mask = second-byte glyph bits
    mov al, [es:di+1]           ; load latches
    mov [es:di+1], al
.no_second:
    add di, ROW_BYTES
    dec ch
    jnz .row

    mov ax, 0x0001
    out dx, ax                  ; GC1 enable Set/Reset = 0 (default, SPEC 1.7)
    mov ax, 0xFF08
    out dx, ax                  ; GC8 Bit Mask = FF (default)
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_char_bb - back-buffer glyph renderer (SPEC.md 32)
;
; The same shifted-row masks as the VRAM path, applied to all four planes
; in software: the [gfx_color] plane bit picks or (set bits) or and-not
; (clear bits). When x is byte-aligned the second-byte mask is always 0
; and that write is skipped, so nothing lands past the row's last byte.
; in:       CX = x, DX = y (cell fully on screen - font_char clipped),
;           SI -> 8 glyph row bytes
; clobbers: ax, bx, cx, dx, si, di, es (font_char's epilogue restores all)
;
; **This is the mono adapters' ONLY text renderer** (SPEC.md 39.5), so its
; row loop is the innermost loop of every string os8088 draws on a Hercules
; or CGA machine - and those are the slow machines. Three things it does that
; the straight port did not, all of them the VRAM path's own tricks arriving
; late:
;
; - **The ink test is hoisted.** [gfx_color]'s plane bit cannot change inside
;   a plane, so `test ch, 1` belongs above the eight rows rather than in them.
;   Two loops instead of one branch per row.
; - **A blank glyph row is skipped whole.** or-ing in 0 and and-ing in FF are
;   both identity, and a read-modify-write of framebuffer memory is ~30 cycles
;   on an 8088 whether or not it changes anything. Most glyphs have a blank
;   descender row and many a blank top row; a SPACE is eight of them, and the
;   Task Manager's columns are largely spaces.
; - **gfx_nextrow is inlined.** Its body is three instructions and the call
;   and ret around them cost as much again - about a third of a row's work
;   once the redundant writes above are gone. The CS overrides come with it:
;   see gfx_nextrow for why they are not optional.
; -----------------------------------------------------------------------------
font_char_bb:
    mov ax, cx                  ; dirty: columns x/8..(x+7)/8, rows y..y+7
    mov bx, cx
    add bx, 7
    mov di, cx
    and di, 7                   ; DI parks the shift (x & 7) for a moment
    mov cl, 3
    shr ax, cl
    shr bx, cl                  ; same byte column when x & 7 = 0
    push di
    push dx
    mov cx, dx
    add dx, 7
    push ax                     ; left column survives bb_dirty for the
    call bb_dirty               ; offset math below
    pop bx
    pop dx
    mov ax, dx                  ; DI = rowbase(y) + x / 8, CL = x & 7
    call gfx_rowbase            ; (CX is popped back below, DX is dead)
    add ax, bx
    mov di, ax
    pop cx                      ; CL = right shift for the 16-bit window

    mov [font_bb_si], si        ; per-plane restart points
    mov [font_bb_di], di
    mov dx, [vid_rseg]          ; DX = plane segment - the framebuffer on
    mov ch, [gfx_color]         ; mono (SPEC.md 39.3)
    call font_ink               ; CH = colour bits, shifted right per plane;
                                ; on mono, 00 or FF so `test ch,1` still works
    mov bl, [vid_planes]        ; BL = plane counter: 4 or 1
.plane:
    mov es, dx
    mov si, [font_bb_si]
    mov di, [font_bb_di]
    mov bh, 8                   ; row counter
    test ch, 1                  ; this plane's ink, decided ONCE rather than
    jnz .srow                   ; once per row - and so is the disabled
    cmp byte [font_dith], 0     ; dither, which is mono-only and therefore
    jne .dstart                 ; always on the clear-bits side
    jmp .crow
.srow:
    mov ah, [si]                ; glyph row in the window's high byte
    inc si
    or ah, ah                   ; a blank row ors in nothing, whatever the
    jz .snext                   ; shift does to it
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    or [es:di], ah
    or al, al                   ; ...and nothing spilled into the second byte
    jz .snext                   ; at all when x is byte-aligned
    or [es:di+1], al
.snext:
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined (see the note above)
    test di, [cs:vid_wrapbit]
    jz .snw
    add di, [cs:vid_wrapfix]
.snw:
    dec bh
    jnz .srow
    jmp short .pnext
.crow:
    mov ah, [si]
    inc si
    or ah, ah                   ; a blank row masks nothing out either
    jz .cnext
    xor al, al
    shr ax, cl
    not ah
    and [es:di], ah
    or al, al
    jz .cnext
    not al
    and [es:di+1], al
.cnext:
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .cnw
    add di, [cs:vid_wrapfix]
.cnw:
    dec bh
    jnz .crow
    jmp short .pnext

; --- disabled text, mono only (SPEC.md 47) -----------------------------------
; .crow with the shifted glyph masked to a checkerboard. A separate loop
; rather than an `and ah, [font_dmask]` in .crow itself: that is the innermost
; loop of every string on the slowest machines we run on, and a memory-operand
; AND per byte per row is real money there for something almost no glyph wants.
; The phase restarts at AA for every cell, so a whole line is in step - text
; rows are 8px apart, which is even, so lines are in step with each other too.
.dstart:
    mov byte [font_dmask], 0xAA
.drow:
    mov ah, [si]
    inc si
    or ah, ah                   ; a blank row masks nothing out...
    jz .dnext
    xor al, al
    shr ax, cl
    and ah, [font_dmask]        ; ...and half of what is left goes too
    and al, [font_dmask]
    or ah, ah
    jz .dsecond                 ; the mask can empty a byte the glyph filled
    not ah
    and [es:di], ah
.dsecond:
    or al, al
    jz .dnext
    not al
    and [es:di+1], al
.dnext:
    xor byte [font_dmask], 0xFF ; every row, blank ones included, or the
                                ; checkerboard turns into stripes
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .dnw
    add di, [cs:vid_wrapfix]
.dnw:
    dec bh
    jnz .drow
.pnext:
    add dx, [vid_rpara]
    shr ch, 1
    dec bl
    jnz .plane
    ret

; -----------------------------------------------------------------------------
; font_ink - internal: font_char_bb's per-plane colour register
;
; The glyph loop tests bit 0 of CH once per plane and shifts, so on VGA the
; colour index goes in untouched. On mono there is one plane and one bit to
; give it: gfx_ink's black or white. The middle greys round to black - a
; dithered 8x8 glyph costs half its strokes, and Piano's coloured letters
; want contrast, not texture.
;
; **[gfx_dis] IS THE ONE EXCEPTION** (SPEC.md 47): a disabled control's text
; is masked to a checkerboard instead - half the strokes, which is exactly
; what the 1bpp Macintosh did to a greyed-out menu item and exactly as
; readable. Without it a disabled STRING came out black, pixel-identical to a
; live one on Hercules and CGA, so a menu item that greyed out simply stopped
; saying so and MENU_DIS's whole visual signal was missing (SPEC.md 12.2).
;
; **It keys off the FLAG and not off CDGRAY**, which was the first attempt:
; the renderer cannot ask whether a string is a control, so every other use of
; that colour got caught too - Minesweeper's digit 8 (SPEC.md 23) came out
; dotted, looking like a menu item you cannot pick. A flag says what is meant
; rather than inferring it from what was picked, and gfx_pen_* keeps the two
; from disagreeing.
;
; in:       CH = [gfx_color], [gfx_dis]
; out:      CH = the colour index (VGA) or 00/FF (mono); [font_dith] set
; clobbers: flags
; -----------------------------------------------------------------------------
font_ink:
    mov byte [font_dith], 0     ; cleared on EVERY path, VGA included: the
    cmp byte [vid_mono], 0      ; glyph loop reads it per plane and .bss is
    je .out                     ; not zeroed by -f bin
    push ax
    mov al, ch
    call gfx_ink                ; CH is untouched by it - still the index
    cmp al, 0xFF                ; only pure white lights a mono pixel
    je .white
    cmp byte [gfx_dis], 0
    je .black
    mov byte [font_dith], 1     ; disabled: black ink, half the pixels
.black:
    xor al, al
.white:
    mov ch, al
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; font_str - draw a NUL-terminated string left to right
;
; in:       CX = x, DX = y, SI -> ASCIIZ string, color from [gfx_color];
;           caller holds the gfx lock
; out:      nothing
; clobbers: nothing
; -----------------------------------------------------------------------------
font_str:
    push es                     ; the kernel's own strings live in DS; the
    push ds                     ; ES-taking body below is the one that draws
    pop es                      ; (SPEC.md 20.1: a package's are in its own
    call font_str_x             ; segment, and wm/menu pass that in ES)
    pop es
    ret

; -----------------------------------------------------------------------------
; font_str_x - font_str with the string at ES:SI
;
; in:       CX = x, DX = y, ES:SI -> ASCIIZ, [gfx_color] = pen
; out:      nothing
; clobbers: nothing else (flags)
;
; Since packages left the kernel segment (SPEC.md 20.1) a drawn string can
; belong to the kernel or to any loaded package, and the caller is the only
; one who knows which. Five call sites in the tree pass a package segment -
; the window title (wm_draw_win), the app name and menu titles
; (menu_draw_bar), and the item strings (menu_drop, menu_widest); every other
; caller reaches this through font_str above with ES = DS.
; -----------------------------------------------------------------------------
font_str_x:
    push ax
    push cx
    push si
    cld
.next:
    mov al, [es:si]
    inc si
    or al, al
    jz .done
    call font_char              ; out-of-range chars draw nothing; pen still
    add cx, 8                   ; advances, so they read as spaces
    jmp .next
.done:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; font_run - draw a string as OPAQUE cells: background and glyph in one pass
;
; in:       CX = x, DX = y, SI -> ASCIIZ (font_run_x: ES:SI), AL = text
;           colour, AH = background colour; caller holds the gfx lock
; out:      nothing
; clobbers: nothing (flags)
;
; **Every text-bearing element in this system erases a rect and then letters
; it**, and each of them writes those bytes twice: once as a fill and once,
; read-modify-write, as a glyph. This does it once. On a 1bpp adapter with a
; byte-aligned x an 8x8 cell row IS one framebuffer byte, so the whole
; operation is `mov [es:di], al` per row per cell - no shift, no read, no
; second byte, and no separate fill pass at all.
;
; **It also collapses the granularity rule** (SPEC.md 11.3) for its callers.
; A fill clips per pixel and a glyph per whole cell, so an erase-then-letter
; pair can disagree and leave a cell blank; here they are one decision about
; one cell, taken once. A caller using this cannot produce that failure.
;
; Two paths, and the fast one is deliberately narrow:
;   - `[bb_on]` (both mono adapters, and VGA while double-buffered) with x
;     byte-aligned: the per-plane loop below. This is the case the slow
;     machines are in - on mono `[bb_on]` is permanently 1 (SPEC.md 39.5).
;   - anything else: one gfx_fill of the run's rect in the background colour
;     and font_str over it, which is exactly what every caller wrote by hand
;     before. No regression, no new planar VRAM code, and the answer is
;     identical either way.
; =============================================================================
font_run:
    push es
    push ds
    pop es
    call font_run_x
    pop es
    ret

font_run_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [font_rn_fg], al
    mov [font_rn_bg], ah
    test cx, 7                  ; unaligned, or a VRAM planar target: the
    jnz .slow                   ; two-pass answer, unchanged
    cmp byte [bb_on], 0
    je .slow
    cmp byte [gfx_dis], 0       ; a DISABLED control (SPEC.md 47) masks the
    jne .slow                   ; glyph to a checkerboard inside font_ink,
                                ; which the plain store below cannot express
                                ; - and greying is a rare UI state, not a hot
                                ; path, so it takes the fallback rather than
                                ; the fast path growing a second mask

    mov al, [font_rn_fg]        ; both colours are drawn, so both decide
    mov [gfx_color], al         ; whether the planes can still be treated as
    call bb_mono_chk            ; one (SPEC.md 32)
    mov al, [font_rn_bg]
    mov [gfx_color], al
    call bb_mono_chk
    cld
.cell:
    mov al, [es:si]
    or al, al
    jz .out
    inc si
    call font_run_cell          ; CX/DX = this cell; preserves both
    add cx, 8
    jmp short .cell

.slow:
    ; The fallback: what every caller wrote by hand, one gfx_fill of the run's
    ; rect and font_str over it - EXCEPT that the pair clips at two different
    ; granularities, so under an armed clip region an edge crossing the run
    ; fills the visible rows and letters none of them, and the run goes BLANK
    ; rather than stale (SPEC.md 11.3/6.1.2). That is affordable for a caller
    ; who knows to gate on it; it is not affordable for a primitive the kernel
    ; itself draws through. So the pair is used only where it cannot
    ; disagree - no region armed, or the whole run inside one fragment - and a
    ; run an edge actually cuts goes per cell.
    call font_width_x           ; AX = 8 * length (CX/DX/SI preserved)
    or ax, ax
    jz .out                     ; empty string: nothing to fill and nothing
                                ; to letter
    cmp word [wm_clip_n], 0
    je .whole                   ; disarmed: the pair cannot disagree
    push ax
    push cx
    push dx
    add ax, cx
    dec ax
    mov bx, dx
    add dx, 7
    xchg ax, cx                 ; AX,BX,CX,DX = the run's rect
    call wm_clip_test
    pop dx
    pop cx
    pop ax
    jc .cells                   ; an edge cuts it
.whole:
    push cx
    push dx
    add ax, cx
    dec ax                      ; AX = x2
    mov bx, dx                  ; BX = y1
    add dx, 7                   ; DX = y2
    xchg ax, cx                 ; AX = x1, CX = x2
    push ax
    mov al, [font_rn_bg]
    mov [gfx_color], al
    pop ax
    call gfx_fill
    pop dx
    pop cx
    mov al, [font_rn_fg]
    mov [gfx_color], al
    call font_str_x
    jmp short .out
.cells:
    mov al, [es:si]
    or al, al
    jz .out
    inc si
    call font_run_scell
    add cx, 8
    jmp short .cells
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_run_cell - internal: one opaque 8x8 cell, byte-aligned, [bb_on] path
; in:       AL = character, CX = x (multiple of 8), DX = y
; out:      nothing (all registers preserved)
;
; The plane loop is font_char_bb's with the read-modify-write replaced by a
; store: the cell owns its whole byte, so there is nothing underneath to
; preserve, and the background arrives in the same write as the glyph. Per
; plane the byte is (glyph & fg) | (~glyph & bg), each mask 00 or FF - on
; mono that reduces to the glyph or its complement.
; -----------------------------------------------------------------------------
font_run_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp cx, [vid_wm8]           ; the same whole-cell clipping font_char does,
    ja .out                     ; screen edge and clip region alike - so a run
    cmp dx, [vid_hm8]           ; clips exactly as its glyphs would, and its
    ja .out                     ; background with them
    cmp word [wm_clip_n], 0
    je .noclip
    push ax
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_test
    mov cx, ax
    mov dx, bx
    pop ax
    jc .out
.noclip:
    cmp al, FONT_FIRST
    jb .blank
    cmp al, FONT_LAST
    ja .blank
    sub al, FONT_FIRST
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, font_glyphs
    jmp short .have
.blank:
    mov si, font_rn_zero        ; an unprintable character is a blank cell -
.have:                          ; which still has to be PAINTED here

    ; The back buffer's dirty rect, which this path owes exactly as
    ; font_char_bb does (SPEC.md 32). It is easy to miss here because the
    ; target is [vid_rseg] either way and on MONO that IS the framebuffer, so
    ; the cell appears the moment it is written and nothing looks wrong. On a
    ; VGA with double buffering armed, bb_set repoints [vid_rseg] at the back
    ; buffer and gfx_unlock flushes only what bb_dirty was told about - so an
    ; unmarked cell is rendered, correctly, into memory nobody copies to VRAM.
    ; The symptom is position-dependent and reads as anything but this: the
    ; fast path needs a byte-aligned x, so the Task Manager froze solid at one
    ; window x and ran perfectly one pixel either side of it.
    ; bb_dirty is a no-op when [bb_dbl] is 0, which is every mono machine.
    push cx                     ; x and y are still wanted below
    push dx
    mov ax, cx
    mov cl, 3
    shr ax, cl                  ; AX = this cell's byte column...
    mov bx, ax                  ; ...and its right column: an aligned 8px cell
    mov cx, dx                  ; is exactly one byte wide
    add dx, 7                   ; CX/DX = the rows it covers, inclusive
    call bb_dirty               ; clobbers flags only
    pop dx
    pop cx

    mov di, cx                  ; DI = x/8 FIRST: gfx_rowbase clobbers CX
    mov cl, 3                   ; (and AX and DX), and this cell's column has
    shr di, cl                  ; to survive it
    mov ax, dx
    call gfx_rowbase            ; AX = rowbase(y)
    add di, ax
    mov [font_rn_si], si
    mov [font_rn_di], di

    mov ch, [font_rn_fg]        ; the per-plane bit sources, reduced the way
    call font_ink               ; every other glyph draw reduces them
    mov [font_rn_fs], ch
    mov ch, [font_rn_bg]
    call font_ink
    mov [font_rn_bs], ch

    mov dx, [vid_rseg]
    mov bl, [vid_planes]        ; BL = plane counter: 4 or 1
.plane:
    mov es, dx
    mov si, [font_rn_si]
    mov di, [font_rn_di]
    mov bh, 8
    shr byte [font_rn_fs], 1    ; this plane's bit -> CF -> an 00/FF mask
    sbb al, al
    mov [font_rn_fm], al
    shr byte [font_rn_bs], 1
    sbb al, al
    mov [font_rn_bm], al
.row:
    mov al, [si]                ; AL = the glyph row
    inc si
    mov ah, al
    and al, [font_rn_fm]        ; ink where the glyph is set...
    not ah
    and ah, [font_rn_bm]        ; ...background where it is not
    or al, ah
    mov [es:di], al             ; the cell owns the byte: store, never merge
    add di, [cs:vid_rowadd]     ; (gfx_nextrow inlined - see font_char_bb)
    test di, [cs:vid_wrapbit]
    jz .nw
    add di, [cs:vid_wrapfix]
.nw:
    dec bh
    jnz .row
    add dx, [vid_rpara]
    dec bl
    jnz .plane
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_run_scell - internal: one opaque cell the SLOW way, for a run an armed
;                  clip region cuts (see font_run_x's .cells)
; in:       AL = character, CX = x, DX = y
; out:      nothing (all registers preserved)
;
; Fill the cell, letter the cell - but behind ONE wm_clip_test about that
; cell, so the two halves cannot disagree about it. That is the same decision
; font_run_cell makes on the fast path; this is just the version that works
; on a planar target or at an unaligned x, where the cell does not own its
; framebuffer byte and there is nothing cleverer to do.
; -----------------------------------------------------------------------------
font_run_scell:
    mov [font_rn_ch], al
    push ax
    push bx
    push cx
    push dx
    mov ax, cx                  ; AX,BX,CX,DX = this cell's rect, inclusive
    mov bx, dx
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_test
    jc .out                     ; crossed: leave its pixels alone, BOTH halves
    push ax                     ; (the pen loads clobber AL, which is x1's low
    mov al, [font_rn_bg]        ; byte - gfx_fill still needs the rect)
    mov [gfx_color], al
    pop ax
    call gfx_fill
    push ax
    mov al, [font_rn_fg]
    mov [gfx_color], al
    pop ax
    mov cx, ax                  ; font_char takes x,y in CX,DX
    mov dx, bx
    mov al, [font_rn_ch]
    call font_char
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

font_rn_zero:   db 0,0,0,0,0,0,0,0   ; the blank cell an unprintable draws

; -----------------------------------------------------------------------------
; font_width - pixel width of a NUL-terminated string
;
; in:       SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width:
    push es
    push ds
    pop es
    call font_width_x
    pop es
    ret

; -----------------------------------------------------------------------------
; font_width_x - font_width with the string at ES:SI (SPEC.md 20.1)
; in:       ES:SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width_x:
    push si
    xor ax, ax
.next:
    cmp byte [es:si], 0
    je .done
    inc si
    add ax, 8
    jmp .next
.done:
    pop si
    ret

; -----------------------------------------------------------------------------
section .bss
font_glyphs:    resb 760        ; glyphs 32..126, 8 bytes per row (FONT_BYTES)
font_rn_fg:     resb 1          ; font_run: the run's two colours, and the
font_rn_bg:     resb 1          ; per-plane bit sources and 00/FF masks its
font_rn_fs:     resb 1          ; cell loop consumes. In .bss for the same
font_rn_bs:     resb 1          ; reason font_bb_si is: the row loop has no
font_rn_fm:     resb 1          ; register to spare, and every caller holds
font_rn_bm:     resb 1          ; the gfx lock (SPEC.md 1.6)
font_rn_ch:     resb 1          ; font_run_scell's character, parked because
                                ; AL is the rect's low byte by the time the
                                ; letter is drawn
font_rn_si:     resw 1
font_rn_di:     resw 1
font_bb_si:     resw 1          ; bb path: glyph pointer, reloaded per plane
font_bb_di:     resw 1          ; bb path: cell offset, reloaded per plane
font_dith:      resb 1          ; 1 = this glyph is disabled text on a mono
                                ; adapter (SPEC.md 47); font_ink decides it
                                ; per glyph and writes it on every path
font_dmask:     resb 1          ; ...and the row's half of the checkerboard
section .text
