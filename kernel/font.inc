; =============================================================================
; font.inc - 8x8 text for mode 12h (SPEC.md 6)
;
; No font bytes are hard-coded unless the BUILD asks for some: font_init asks
; the VGA BIOS for its ROM 8x8 character set (int 10h AX=1130h BH=03h) and
; copies the printable range, glyphs 32..126, into a kernel buffer -- and
; `make FONT=<name>` replaces that whole path with a typeface carried in the
; boot overlay instead (SPEC.md 6.2, ovl_font_init at the foot of this file).
;
; Drawing uses Set/Reset with the Bit
; Mask so the background stays untouched (transparent): the glyph row is
; widened to a 16-bit window and shifted right by x&7, giving the masks for
; the one or two VRAM bytes the row lands on. A read of each byte loads the
; latches, so unmasked pixels are written back unchanged.
; =============================================================================

FONT_FIRST  equ 32              ; first glyph kept from the ROM set
FONT_LAST   equ 126             ; last  glyph kept
FONT_BYTES  equ (FONT_LAST - FONT_FIRST + 1) * 8    ; 95 glyphs x 8 rows = 760

; SPEC.md 6.1.7: the widest run any adapter can show, in cells. A run stops at
; [vid_cwm8], so this is vid_w / 8 for the widest screen os8088 drives -
; Hercules at 720 - and the run-major scan can therefore never overflow the
; table. It has a fallback for it anyway, because a constant that is a bound
; on another module's geometry is one somebody will change.
FONT_RN_MAX equ 90

; SPEC.md 6.1.8: the shortest trailing span worth a `rep stosb`. The tail block
; is 17 bytes against a plain cell's 14, so it breaks even at 2 and wins from
; 3; below that the run takes the plain walk and pays nothing at all.
FONT_RN_KMIN equ 3

SNAP_ENT     equ 8              ; bytes per caller-log entry: x, y, char, ret
SNAP_NCALL   equ 24             ; SPEC.md 11.94.2: off-grid callers logged per
                                ; reset. A repaint draws hundreds of glyphs and
                                ; the first two dozen name every routine in it

; -----------------------------------------------------------------------------
; SPEC.md 11.94.1's INSTRUMENT: `make SNAPAUDIT=1` histograms the x & 7 of
; every glyph the machine draws, so "which app does not align its text" is a
; number read out of a running machine rather than a guess made by reading
; layout constants. It only means anything now that alignment is the DEFAULT
; (11.94): with every window's content origin on a multiple of 8, a non-zero
; bucket is the APP's own offset and nothing else.
;
; Two histograms because the two callers answer different questions - a
; font_run is a whole line and a font_char is one cell, so counting them
; together would let one app's 40-cell string outvote another's 40 strings.
;
; A plain build emits NOT ONE BYTE of this: the macro is empty and the
; counters are inside the same %ifdef. It preserves the flags because
; font_char's own contract does and its callers are inner loops.
; -----------------------------------------------------------------------------
%ifdef SNAPAUDIT
%macro SNAPAUDIT_TALLY 2        ; %1 = the audited window's table, %2 = the other
    pushf
    push ax
    push bx
    mov ax, [cs:snap_cur]       ; which window's callback are we inside?
    or ax, ax
    jz %%other                  ; none: kernel chrome, menus, the desktop
    mov bx, [cs:snap_win]
    or bx, bx
    jz %%mine                   ; no filter set: every callback counts
    cmp bx, ax
    jne %%other                 ; a DIFFERENT window - a repaint pass calls
%%mine:                         ; several, and only one is being asked about
    mov bx, %1
    jmp short %%have
%%other:
    mov bx, %2
%%have:
    mov ax, cx                  ; CX = the pen x, both callers
    and ax, 7
    shl ax, 1
    add bx, ax
    inc word [cs:bx]            ; cs: - .text, and CS is KERNEL_SEG here

    ; --- WHO DREW IT: the off-grid caller names itself (SPEC.md 11.94.2) -----
    ; A bucket count says an app is off-grid and not WHERE, and reading layout
    ; constants to find out is what left Piano, Arkanoid and Missile unresolved.
    ; So an off-grid glyph inside the AUDITED window records the pen and its
    ; caller's return address, and tools/os88snap.py maps that back to a symbol
    ; through os88sym. First SNAP_NCALL of them, because a repaint draws
    ; hundreds and the first few name the routine just as well.
    or ax, ax
    jz %%done                   ; bucket 0: aligned, nothing to report
    cmp bx, %1 + 16             ; the audited window's table? (its 8 words end
    jae %%done                  ; there) - the chrome's callers are not the ask
    mov bx, [cs:snap_ncall]
    cmp bx, SNAP_NCALL * SNAP_ENT
    jae %%done
    add word [cs:snap_ncall], SNAP_ENT
    mov bx, sp                  ; the macro pushed flags, ax, bx - so the frame
                                ; from SP is [+0] bx, [+2] ax, [+4] flags,
                                ; [+6] the caller's return address. ss: because
                                ; SS is not DS in this kernel
    mov ax, [ss:bx + 2]         ; the caller's AX: AL is the CHARACTER, and
    mov [cs:snap_tmpc], ax      ; reading [+0] instead gets the saved BX, which
                                ; is constant inside font_str_x's loop and
                                ; therefore looks exactly like a real answer
    mov ax, [ss:bx + 6]
    mov [cs:snap_tmpr], ax
    mov bx, [cs:snap_ncall]     ; ...and the slot this entry goes in
    sub bx, SNAP_ENT
    mov ax, cx
    mov [ss:bx + snap_calls], ax        ; pen x
    mov ax, dx
    mov [ss:bx + snap_calls + 2], ax    ; pen y
    mov ax, [cs:snap_tmpc]
    mov ah, 0
    mov [ss:bx + snap_calls + 4], ax    ; the character
    mov ax, [cs:snap_tmpr]
    mov [ss:bx + snap_calls + 6], ax    ; who called
%%done:
    pop bx
    pop ax
    popf
%endmacro
%else
%macro SNAPAUDIT_TALLY 2
%endmacro
%endif

%ifndef BAKED_FONT
; -----------------------------------------------------------------------------
; font_init - copy the ROM 8x8 font's printable range into RAM
;
; in:       nothing (must run after vid_init; uses int 10h)
; out:      nothing
; clobbers: nothing
;
; NOT ASSEMBLED under BAKED_FONT (SPEC.md 6.2), where ovl_font_init at the
; foot of this file does the same job out of the boot overlay. The two are
; alternatives rather than a fallback pair on purpose: a probe kept behind a
; baked table would be dead code on every machine that ever runs it, and a
; baked table kept behind a probe would never be the thing being tested.
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
    mov ax, LOW_SEG             ; destination: the table is in LOW_SEG now
    mov es, ax                  ; (SPEC.md 2.1), so the copy goes through SS's
    mov ds, dx                  ; segment rather than the kernel's own
    cld
    rep movsw

    mov cx, 4                   ; ...and the blank cell, which nothing else
    mov di, font_zero           ; zeroes: .lowbss is nobits and the loader
    xor ax, ax                  ; never writes it. ES is still LOW_SEG from
    rep stosw                   ; the copy above, which is what stosw needs

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
%endif

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
    SNAPAUDIT_TALLY snap_hchar, snap_kchar
    call fnt_unlazy
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    GFXDENTERCD                 ; the cell goes WHOLE onto the display holding
                                ; its top-left corner (SPEC.md 39.14.2), and
                                ; the two edge tests below are that display's

    cmp al, FONT_FIRST          ; unprintable -> space -> nothing to draw
    jb .done
    cmp al, FONT_LAST
    ja .done
    cmp cx, [vid_cwm8]          ; unsigned: negative x fails this too
    ja .done
    cmp dx, [vid_chm8]
    ja .done
    cmp word [wm_clip_n], 0     ; ...and against the clip region, which
    je .noclip                  ; answers in ROWS (SPEC.md 11.3.2): a cell an
    push ax                     ; edge crosses horizontally draws the rows on
    mov ax, cx                  ; our side of it rather than nothing at all,
    mov bx, dx                  ; which is what left a half-covered Timer
    mov cx, ax                  ; frozen at the second it was covered
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_rows
    mov cx, ax                  ; MOV and POP leave CF alone
    mov dx, bx
    pop ax
    jc .done
    jmp short .rows
.noclip:
    ; The whole cell, said in the one place the renderers below read it from -
    ; and said in ONE word store rather than two byte ones, because this is the
    ; path every unclipped glyph in the system takes and an 8088 pays 4.34
    ; clocks per instruction BYTE (PERFORMANCE.md Part 9), so 6 bytes against
    ; 12 is worth having. MEASURED on fontbench's PAIR rows it is worth less
    ; than the byte count suggests - 2.18% -> 2.04% - because what dominates is
    ; the four-instruction bias below, not these stores; the arithmetic said
    ; ~0.8% and the machine said 0.14%, which is why the figure quoted here is
    ; the machine's. The two bytes are adjacent by assertion, not by hope
    ; - see wm_clip_r0.
    mov word [wm_clip_r0], (8 << 8) | 0
.rows:

    sub al, FONT_FIRST          ; SI = glyph row data
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, font_glyphs
    mov al, [wm_clip_r0]        ; AL is spent (SI carries the glyph now), so
    mov ah, 0                   ; the row bias costs no register: start at
    add si, ax                  ; the glyph's row r0...
    add dx, ax                  ; ...and at the screen row it belongs on

    cmp byte [vid_mono], 0      ; 1bpp? the software renderer IS the direct
    je .vram                    ; path there (SPEC.md 39.5)
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

    mov ch, [wm_clip_rn]        ; row counter: the rows the region allows,
.row:                           ; 8 whenever it is not cutting this cell
    mov ah, [ss:si]             ; glyph row in the high byte of the window
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
    GFXDLEAVE
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_char_bb - the software renderer's glyph body (SPEC.md 32/39.5)
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
;   both identity, and a read-modify-write of framebuffer memory costs the
;   same on an 8088 whether or not it changes anything. MEASURED on a real
;   5150 (PERFORMANCE.md Part 9): **79.6 clocks per byte** on a Hercules and
;   81.0 on a CGA, for the whole five-instruction step. This comment used to
;   say ~30, which was low by 2.6x AND attributed to the wrong thing - only
;   about 7 of those clocks are the framebuffer, the rest is the 8088
;   executing the loop, and the same loop against plain RAM measures 72.8.
;   Most glyphs have a blank descender row and many a blank top row; a SPACE
;   is eight of them, and the Task Manager's columns are largely spaces.
; - **gfx_nextrow is inlined.** Its body is three instructions and the call
;   and ret around them cost as much again - about a third of a row's work
;   once the redundant writes above are gone. The CS overrides come with it:
;   see gfx_nextrow for why they are not optional.
; -----------------------------------------------------------------------------
font_char_bb:
    mov ax, cx
    mov di, cx
    and di, 7                   ; DI parks the shift (x & 7) for a moment
    mov cl, 3
    shr ax, cl                  ; AX = the byte column (x / 8)
    push di
    mov bx, ax                  ; BX survives gfx_rowbase; AX does not
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
    mov bh, [wm_clip_rn]        ; row counter - see font_char's .rows
    test ch, 1                  ; this plane's ink, decided ONCE rather than
    jnz .srow                   ; once per row - and so is the disabled
    cmp byte [font_dith], 0     ; dither, which is mono-only and therefore
    jne .dstart                 ; always on the clear-bits side
    jmp .crow
.srow:
    mov ah, [ss:si]             ; glyph row in the window's high byte
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
    jmp .pnext                  ; NEAR: .dstart's seed put .pnext out of a
                                ; short jump's reach from here
.crow:
    mov ah, [ss:si]
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
; The phase is the CELL's row 0, so a whole line is in step - text rows are 8px
; apart, which is even, so lines are in step with each other too. It is seeded
; off [wm_clip_r0] rather than always AA because the first row DRAWN is not
; always the cell's first (SPEC.md 11.3.2): a clip edge cutting a cell biases
; both the glyph pointer and the screen row, and an unbiased seed would flip
; the checkerboard's phase against the rows drawn either side of it.
.dstart:
    mov al, 0xAA                ; AL is free here - SI carries the glyph, and
    test byte [wm_clip_r0], 1   ; the row loop reloads AX every pass
    jz .dset
    mov al, 0x55
.dset:
    mov [font_dmask], al
.drow:
    mov ah, [ss:si]
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
    cmp byte [gfx_dis], 0       ; DISABLED IS ASKED FIRST (SPEC.md 76.6). It
    je .ink                     ; used to be asked only after `cmp al, 0xFF`
    mov byte [font_dith], 1     ; sent a WHITE pen straight to .white, which
%ifndef DISINK0                 ; was sound while ink was black by definition
    mov al, [gfx_disink]        ; (`make DISINK0=1` is the A/B: the reduction
%endif                          ; below then sends every grey to black again)
                                ; - and under the Dark theme the chrome's ink
                                ; IS white, so a greyed caption came out
                                ; SOLID and pixel-identical to a live one:
                                ; SPEC.md 47 rule 1's own failure, arriving
                                ; through the renderer rather than a caller
                                ;
                                ; **AND THE STIPPLE IS APPLIED TO THE LIVE
                                ; INK'S DIRECTION, NOT TO THE GREY** (SPEC.md
                                ; 76.6.1). The reduction below sends anything
                                ; that is not 0xFF to BLACK, and every
                                ; disabled pen in the machine is a middle grey
                                ; - so the checkerboard was always laid on
                                ; black, which is a legible stipple on white
                                ; and NOTHING AT ALL on Dark's black chrome.
                                ; [gfx_disink] is what the pen that set the
                                ; flag says a greyed glyph stipples in, and it
                                ; is that pen's own live ink: black for the
                                ; content pens, [thm_inkc] for the chrome's
.ink:
    call gfx_ink                ; CH is untouched by it - still the index
    cmp al, 0xFF                ; only pure white lights a mono pixel
    je .white
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
    retf

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
;   - `[vid_mono]` (both mono adapters) with x
;     byte-aligned: the per-plane loop below. This is the case the slow
;     machines are in - on mono `[vid_mono]` is permanently 1 (SPEC.md 39.5).
;   - anything else: one gfx_fill of the run's rect in the background colour
;     and font_str over it, which is exactly what every caller wrote by hand
;     before. No regression, no new planar VRAM code, and the answer is
;     identical either way.
; =============================================================================
; fnt_unlazy - a glyph under an ARMED clip region is confined to it by
; wm_clip_test (it draws a cell whole or not at all), so a cursor outside that
; region is provably safe. Unarmed, a glyph lands wherever it is told and the
; deferred hide is owed (SPEC.md 7.1.4).
fnt_unlazy:
    cmp word [wm_clip_n], 0
    jne .out
    CURUNLAZY
.out:
    ret

font_run:
    GFXAUDIT_HOOK
    push es                     ; the deferred hide is spent in font_run_x and
    push ds                     ; NOT here, which is SPEC.md 6.1.9: this entry
    pop es                      ; is the kernel's, and the API slot 0x0258 is
    call font_run_x             ; an X STUB straight to font_run_x - so a hide
    pop es                      ; owed here was owed by kernel callers only
    ret

; SPEC.md 6.1.7: one cell of a row pass, and the row advance after it. Macros
; because the padded and unpadded row loops differ only in the tail, and two
; copies of a five-instruction body that must agree is how they stop agreeing.
%macro FONT_RN_PX 0
    lodsw                       ; AX = this cell's glyph base (DS:SI, the table)
    xchg ax, bx                 ; BX = it, in one byte
    add bx, bp                  ; ...+ the row this pass is on
    mov al, [ss:bx]             ; the glyph row byte (font_glyphs is LOW_SEG)
    and al, dl
    xor al, dh
    stosb
%endmacro

; SPEC.md 6.1.12: the end of one row of a RUN - the row advance, and the
; checkerboard's per-row swap. [font_rn_dt] is 0 for every run that is not
; disabled text on a 1bpp adapter, so the XOR is unconditional and free of a
; branch. NOT folded into FONT_RN_NEXTROW: font_rn_edge's column passes use
; DL as a BIT MASK and share that macro.
%macro FONT_RN_ROWEND 0
    FONT_RN_NEXTROW
    xor dl, [font_rn_dt]
%endmacro

%macro FONT_RN_NEXTROW 0
    add di, [cs:vid_rowadd]     ; gfx_nextrow, ONCE a row
    test di, [cs:vid_wrapbit]
    jz %%nw
    add di, [cs:vid_wrapfix]
%%nw:
%endmacro

font_run_x:
    SNAPAUDIT_TALLY snap_hrun, snap_krun
    call fnt_unlazy             ; SPEC.md 6.1.9: HERE, not at font_run above,
                                ; because api_font_run is an X STUB to this
                                ; label - so every package's opaque run drew
                                ; over the mouse pointer with gfx_lock's
                                ; promised hide unspent, and gfx_unlock then
                                ; took cur_lazyend's "the arrow never left the
                                ; screen" path and did not put it back
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; SPEC.md 6.1.7's row pass holds the glyph row
                                ; in BP, and 7.1.4.1 is what a routine that
                                ; forgets to save it costs: a background task
                                ; keeps BP for its whole life
%ifdef KERN_BIG
    ; --- SPEC.md 39.14.6: A RUN IS NOT A SHAPE. font_char is whole-cell
    ; because a cell is 8x8 and goes wholly onto one display; a run is up to a
    ; window wide, and treating it the same way cuts a line of text dead at
    ; the seam. A straddled Note Pad showed exactly that in the field: typing
    ; landed on one card only, and because SPEC.md 27.2 makes the run's own
    ; padding the ERASE, backspace did not erase on the other.
    ;
    ; So a run that does not fit one display goes PER CELL, untranslated -
    ; each font_char then picks its own display, which is the machinery
    ; SPEC.md 6.1.2 already built for a clip edge crossing a run. It skips the
    ; enter entirely, so it must skip the leave as well: .cells_nx has its own
    ; tail rather than a flag for .out to test.
%ifndef NOSPLIT                 ; `make NOSPLIT=1` is the A/B: the same session
                                ; through the kernel that has no split, which
                                ; is the only way to show that the pixels the
                                ; second card gets are THIS change's doing
    cmp byte [vid_ndisp], 1
    jbe .nosplit
    cmp byte [gfx_dnest], 0
    jne .nosplit                ; an outer hook already chose a display
    push ax
    push bx
    push cx
    push dx
    call font_width_x           ; AX = 8 * length (CX/DX/SI preserved)
    or ax, ax
    jz .spanok                  ; empty: nothing to cross
    add ax, cx
    dec ax
    xchg ax, cx                 ; AX = x1, CX = x2
    mov bx, dx                  ; BX = y1
    add dx, 7                   ; DX = y2
    call vid_span_one
    jnc .spanok
    pop dx
    pop cx
    pop bx
    pop ax
    mov [font_rn_fg], al
    mov [font_rn_bg], ah
    jmp .cells_nx
.spanok:
    pop dx
    pop cx
    pop bx
    pop ax
.nosplit:
%endif
%endif
    GFXDENTERCD                 ; ...and a run with it - its .slow path is
                                ; gfx_fill + font_str, which the nesting count
                                ; keeps from translating a second time

    mov [font_rn_fg], al
    mov [font_rn_bg], ah
    mov byte [font_rn_dt], 0    ; SPEC.md 6.1.12: no checkerboard unless the
                                ; mono prologue below finds one. Set HERE and
                                ; not there, so the planar prologue and every
                                ; fallback get it without knowing it exists
%ifdef KERN_BIG
    mov byte [font_rn_pl], 0    ; SPEC.md 6.1.10: which prologue set the masks
%endif
%ifdef KERN_BIG
    mov ax, cx                  ; SPEC.md 6.1.11: the pen's phase, and the
    and al, 7                   ; whole of what UNALIGNED costs from here -
    mov [font_rn_k7], al        ; the prologue, the glyph table and the span
%ifdef NOUNAL                   ; are the run's either way
    jnz .slow                   ; `make NOUNAL=1` is the A/B: the same session
%endif                          ; through the kernel that has no 6.1.11
%else
    test cx, 7                  ; kern_small keeps the pair for an unaligned
    jnz .slow                   ; run, exactly as it keeps it for a PLANAR one
                                ; (6.1.10): the 128-256KB machine's budget is
                                ; the tighter of the two by design, and 6.1.11
                                ; is +262 bytes of .text
%endif
    cmp byte [vid_mono], 0
%ifdef KERN_BIG
    je .planar                  ; SPEC.md 6.1.10: mode 12h has a single-store
%else                           ; prologue of its own. kern_small does not
    je .slow                    ; carry it - gfx_blit1's precedent (5.4.2)
%endif
    ; SPEC.md 6.1.12: a DISABLED control's glyph is checkerboarded, and this
    ; path used to hand it to .slow for that reason. It does not any more -
    ; the mask FOLDS INTO the run's own, below, and costs the row loop one
    ; memory XOR a row rather than one of anything a cell.
    cld

    ; --- what belongs to the RUN, done once (SPEC.md 6.1.6) ------------------
    ; A run is ONE y, ONE pair of colours and a column that advances by one
    ; byte a cell. font_run_cell used to derive all of it per cell - calling
    ; gfx_rowbase, whose 16-bit MUL an 8088 charges ~120 clocks for, and
    ; font_ink TWICE - for answers that cannot change until the run ends.
    cmp dx, [vid_chm8]          ; the run's y is every cell's y, so the
    ja .out                     ; vertical edge test is the run's too
    cmp cx, [vid_cwm8]          ; ...but x is per cell, and DI only tracks it
    jbe .xok                    ; by `inc di`. That lockstep is exact for a run
    jmp .cells                  ; that STARTS on screen - x then cannot reach
.xok:                           ; 0x10000 in 80 cells - and it breaks on the
                                ; one run that can wrap: a NEGATIVE x, which
                                ; this unsigned test catches. Two cells later x
                                ; is 0 and DI is 0x2000 past where it belongs,
                                ; so that run goes per cell (SPEC.md 6.1.2's
                                ; path), which recomputes both and is the
                                ; documented same-pixels fallback
    push cx                     ; font_ink returns in CH and gfx_rowbase
    push dx                     ; clobbers CL and DX - between them that is
                                ; both of the coordinates
    mov bh, [gfx_dis]           ; BH = is this DISABLED text (SPEC.md 47)?
    mov byte [gfx_dis], 0       ; NEITHER font_ink call may act on it: the
                                ; BACKGROUND is never the disabled pen, and
                                ; with the flag live both calls answer with
                                ; [gfx_disink] and the run comes out solid
    mov ch, [font_rn_fg]
    or bh, bh
    jz .fgink
    mov ch, [gfx_disink]        ; SPEC.md 76.6.1: the stipple is laid on the
.fgink:                         ; LIVE ink's direction, which is what that pen
    call font_ink               ; records - black for the content pens,
    mov bl, ch                  ; [thm_inkc] for the chrome's
    mov ch, [font_rn_bg]
    call font_ink
    mov [gfx_dis], bh           ; ...and the flag back, for font_char below
    mov [font_rn_bm], ch        ; the byte a CLEAR glyph bit composes to...
    xor bl, ch                  ; ...and ink XOR background (SPEC.md 6.1.5)
    or bh, bh                   ; live text: DL is the same mask for all eight
    jz .nodith                  ; rows and [font_rn_dt] is already 0
    mov [font_rn_dt], bl        ; disabled: the two dither masks PARTITION the
    mov bh, 0xAA                ; run's mask, so XORing DL with the whole of it
                                ; swaps one for the other - one memory XOR a
                                ; ROW, and nothing at all per cell.
                                ;
                                ; 0xAA UNCONDITIONALLY, because font_char's
                                ; phase is GLYPH-relative and not screen-
                                ; relative: its seed is [wm_clip_r0] & 1, which
                                ; is 0 for an unclipped cell whatever y it is
                                ; on. Seeding off y's parity instead inverts
                                ; the checkerboard on half the screen rows,
                                ; which a pixel diff catches and an eye does
                                ; not
%ifdef KERN_BIG
    test byte [font_rn_k7], 1   ; ...but HORIZONTALLY the phase is screen-
    jz .dseed                   ; absolute: font_char masks the SHIFTED bytes
    mov bh, 0x55                ; and 6.1.11's row walk masks the cell before
%endif                          ; shifting it, so an odd skew takes the other
.dseed:                         ; mask to land in the same columns
    and bl, bh
.nodith:
    mov [font_rn_xm], bl        ; DL's value for the FIRST row
    mov ax, dx                  ; DX is still y - font_ink does not touch it
    call gfx_rowbase            ; AX = rowbase(y), once for the whole run
    pop dx
    pop cx
    mov di, cx
    shr di, 1                   ; three single shifts rather than one by CL:
    shr di, 1                   ; 6 clocks against 20, and CL stays the x it
    shr di, 1                   ; still needs to be
    add di, ax                  ; DI = the run's first framebuffer byte

.p1pre:
    ; --- PASS 1: which cells get drawn, and where their glyphs live ----------
    ; SPEC.md 6.1.7. The string is walked ONCE into a table of glyph pointers,
    ; which is what frees ES - it holds the caller's string right up to here -
    ; so the drawing pass can point it at the framebuffer for the whole run.
    push si                     ; both wanted again if this falls back to the
    push cx                     ; per-cell path below
    mov bx, font_rn_tab
    xor bp, bp                  ; BP = cells that will actually be drawn
.p1:
    mov al, [es:si]
    or al, al
    jz .p1end
    cmp cx, [vid_cwm8]          ; the first cell off the right edge ends the
    ja .p1end                   ; run - x only grows, so the drawn cells are
    cmp bp, FONT_RN_MAX         ; a PREFIX, which is what lets stosb walk them
    jae .p1full
    inc si
    add cx, 8
    cmp al, FONT_FIRST
    jb .p1blank
    cmp al, FONT_LAST
    ja .p1blank
    sub al, FONT_FIRST
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, font_glyphs
    jmp short .p1put
.p1blank:
    mov ax, font_zero           ; an unprintable cell is still PAINTED
.p1put:
    mov [bx], ax
    add bx, 2
    inc bp
    jmp short .p1
.p1full:                        ; unreachable while the table covers the widest
    pop cx                      ; screen, and a fallback rather than a truncation
    pop si                      ; if that ever stops being true
%ifdef KERN_BIG
    mov al, [font_rn_pl]        ; ...and planar OR unaligned, to the scell
    or al, [font_rn_k7]         ; path (6.1.10, 6.1.11)
    jne .cells
%endif
    jmp .cell                   ; NOT `jmp short`: it reaches by 12 bytes today
                                ; and this is the cold path of the pair
.p1end:
    or bp, bp
    jz .p1none
    mov [font_rn_n], bp
    mov [font_rn_n2], bp        ; the default is NO span: every cell drawn
    mov word [font_rn_k], 0     ; singly, and the plain row walk below
    cmp bp, 2
    jb .p1none                  ; one cell cannot repeat anything
    mov ax, [bx-2]              ; the last cell's glyph against the one before
    cmp ax, [bx-4]              ; it. On a run with no span THIS is the whole
    jne .p1none                 ; test - two words and a branch, not a walk,
                                ; which is what took the unpadded rows from
                                ; +1.50% back to the noise
    ; --- the TRAILING SPAN, found once (SPEC.md 6.1.8) ----------------------
    ; This system pads deliberately - 27.2 makes a Note Pad row's padding its
    ; ERASE, 12.9 composes the bar out to the clock, 28's columns are largely
    ; spaces - so the last cells of a run are usually the SAME cell repeated,
    ; and repeated cells compose to one byte. That is a `rep stosb`. The scan
    ; is here, once, rather than a compare per cell per row; SI and CX are
    ; free because the values they held are on the stack.
    mov si, bx                  ; AX already holds the repeated glyph
    sub si, 4                   ; ...and the one before it
    mov cx, 1                   ; the last cell always repeats itself
.p1trl:
    cmp cx, bp
    jae .p1trle
    cmp [si], ax
    jne .p1trle
    dec si
    dec si
    inc cx
    jmp short .p1trl
.p1trle:
    mov [font_rn_k], cx         ; cells the tail writes...
    mov ax, bp
    sub ax, cx
    mov [font_rn_n2], ax        ; ...and the ones drawn singly before it
.p1none:
    pop cx
    pop si
    or bp, bp
    jz .out                     ; nothing on screen: no rows to write
    cmp word [wm_clip_n], 0
    je .rm
    ; A region is armed. stosb walks CONSECUTIVE bytes, so it is only correct
    ; if every cell is drawn - one rejected in the middle breaks the run into
    ; pieces. So the whole run has to sit inside one fragment; a cut one goes
    ; per cell, which is where a per-cell clip test lives anyway (SPEC.md 6.1.2).
    push cx
    push dx
    push si
    push di
    mov ax, cx                  ; AX = x1
    mov bx, dx                  ; BX = y1
    mov cx, bp
    shl cx, 1
    shl cx, 1
    shl cx, 1
    add cx, ax
    dec cx                      ; CX = x2 = x + 8n - 1
    mov dx, bx
    add dx, 7                   ; DX = y2
    call wm_clip_test
    pop di
    pop si
    pop dx
    pop cx
%ifdef KERN_BIG
    jnc .rm
    mov al, [font_rn_pl]        ; SPEC.md 6.1.10/6.1.11: font_run_cell is the
    or al, [font_rn_k7]         ; MONO, byte-ALIGNED cell and there is no
    jne .cells                  ; planar or off-grid spelling of it - a cut run
    jmp .cell                   ; that is either goes to font_run_scell, which
%else                           ; is gfx_fill + font_char and blind to both
    jc .cell                    ; the adapter and the phase
%endif

    ; --- PASS 2: one ROW of the whole run at a time --------------------------
    ; The cells of a run are CONSECUTIVE BYTES within a row, so a row is a
    ; stosb walk and gfx_nextrow is paid once a row instead of once a row per
    ; cell - 12 of the old 27-byte row body, eight times over, per cell.
.rm:
    mov dl, [font_rn_xm]        ; DL = ink XOR background, DH = background:
    mov dh, [font_rn_bm]        ; the run's masks (SPEC.md 6.1.5) for all 8 rows
    mov es, [vid_tseg]          ; ES:DI is the framebuffer from here on - the
    xor bp, bp                  ; string has been read and will not be touched
%ifdef KERN_BIG
    cmp byte [font_rn_k7], 0
    jne .rmu                    ; SPEC.md 6.1.11: off the byte grid, so a cell
                                ; does not own a byte - but the RUN still owns
                                ; all but two of them
%endif
    cmp word [font_rn_k], FONT_RN_KMIN
    jb .rmp                     ; no span worth a `rep`: take the plain walk,
                                ; and take it at the RUN level. The span is a
                                ; property of the run, so a per-ROW test would
                                ; ask the same question eight times and charge
                                ; every unpadded run for the answer - measured
                                ; at +3.06% before this branch existed
.rmrow:
    mov si, font_rn_tab
    mov cx, [font_rn_n2]        ; the cells before the trailing span
    push di                     ; this row's first byte
    jcxz .rmtail                ; ...of which there may be none at all
.rmpx:
    FONT_RN_PX
    loop .rmpx
.rmtail:
    mov cx, [font_rn_k]         ; the repeated tail: one compose, then the
    lodsw                       ; string instruction writes every byte of it.
    xchg ax, bx                 ; SI already points at the first tail entry,
    add bx, bp                  ; and they all carry the same glyph
    mov al, [ss:bx]
    and al, dl
    xor al, dh
    rep stosb
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmrow
    jmp .out
.rmp:                           ; ...and the same rows with no tail at all
    mov si, font_rn_tab
    mov cx, [font_rn_n]
    push di
.rmppx:
    FONT_RN_PX
    loop .rmppx
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmp
    jmp .out

%ifdef KERN_BIG
; --- SPEC.md 6.1.11: the run owns all but TWO of its bytes -------------------
; 6.1.4 argued that unaligned cannot be made fast, and it is right about a CELL
; and wrong about a RUN. Its figure - "4 accesses per cell row" - counts the
; neighbours whose bits must be preserved. In a run, for every cell but the
; last, that neighbour is THE NEXT CELL OF THE SAME RUN: bits the run owns and
; is about to write anyway. A run of n cells covers n+1 framebuffer bytes, of
; which n-1 are wholly inside it, so a row is (n-1) plain stores and TWO merges
; - n+3 accesses against 4n, and 104 against 320 for the ten cells 6.1.4 prices.
;
; The shift is one instruction a cell and no table. With k = x & 7 and c[] the
; composed cells, output byte j is (c[j-1] << (8-k)) | (c[j] >> k), which is
; exactly what `mov ah,c[j-1]` / `mov al,c[j]` / `shr ax,cl` leaves in AL. An
; 8088 charges 8+4k for that shift, so 12-36 clocks against an aligned cell
; row's ~40 - the whole reason this is worth doing rather than the pair.
;
; THE TWO EDGE BYTES ARE COLUMN-MAJOR, NOT PER ROW. Their masks are constant
; for the whole run, so doing them per row would set the VGA Bit Mask sixteen
; times and read-modify-write in the middle of a `stosb` walk. Instead each
; row STASHES its two edge bytes, and two column passes at the end write them
; with the mask set ONCE. gfx_fill_gray's .lcol / .irow / .rcol, in the same
; order and for the same reason.
;
; CX carries the SHIFT in CL and the previous composed byte in CH, so there is
; no register left for a cell counter - the walk ends on SI reaching the glyph
; table's end instead. That is why [font_rn_send] exists.
%macro FONT_RN_UPX 0
    lodsw                       ; AX = this cell's glyph base (DS:SI, the table)
    add ax, bp                  ; ...+ the row this pass is on
    xchg ax, bx                 ; BX = it, and the old BX is spent
    mov al, [ss:bx]             ; the glyph row byte (font_glyphs is LOW_SEG)
    and al, dl
    xor al, dh                  ; AL = this cell, composed
    mov ah, ch                  ; AH = the cell BEFORE it...
    mov ch, al                  ; ...and this one becomes that for the next
    shr ax, cl                  ; AL = the framebuffer byte the two straddle
%endmacro

.rmu:
    mov ax, [font_rn_n]         ; where the cell walk ends (see above)
    shl ax, 1
    add ax, font_rn_tab
    mov [font_rn_send], ax
    mov cl, [font_rn_k7]
    push di                     ; the run's first byte, for the column passes
.rmurow:
    mov si, font_rn_tab
    xor ch, ch                  ; no cell before the first: its bits fall off
    push di                     ; the top of the shift and are masked away
    FONT_RN_UPX                 ; ...so this is the LEFT edge byte
    mov bx, bp
    mov [bx + font_rn_le], al
    inc di                      ; it is written by the column pass, not here
    cmp si, [font_rn_send]
    jae .rmuend                 ; a one-cell run: no interior at all
.rmumid:
    FONT_RN_UPX
    stosb                       ; the interior: bytes the run owns whole
    cmp si, [font_rn_send]
    jb .rmumid
.rmuend:
    mov ah, ch                  ; the RIGHT edge: the last cell's tail, with
    xor al, al                  ; nothing after it
    shr ax, cl
    mov bx, bp
    mov [bx + font_rn_re], al
    pop di
    FONT_RN_ROWEND
    inc bp
    cmp bp, 8
    jb .rmurow

    ; --- the two edge columns, each with its mask set ONCE ------------------
    pop di                      ; back to the run's first byte
    mov al, 0xFF
    shr al, cl                  ; AL = the LEFT byte's mask: our (8-k) low bits
    mov dl, al
    not al
    mov dh, al                  ; DH = the RIGHT byte's: its k high bits
    push di
    mov si, font_rn_le
    call fnt_rn_edge
    pop di
    add di, [font_rn_n]         ; ...and the right byte sits n bytes along
    mov dl, dh
    mov si, font_rn_re
    call fnt_rn_edge
    jmp .out

%endif

.cell:                          ; the per-cell path: a clip-cut run, and the
    mov al, [es:si]             ; overflow that cannot happen
    or al, al
    jz .out
    inc si
    call font_run_cell          ; CX/DX/DI = this cell; preserves all three
    add cx, 8
    inc di                      ; an aligned cell is one byte wide, so the
    jmp short .cell             ; next one is the next byte along

%ifdef KERN_BIG
; --- SPEC.md 6.1.10: mode 12h's single-store prologue -------------------------
; The mono fast path exists because a 1bpp cell OWNS its framebuffer byte, so
; the background and the glyph arrive in one store. On VGA a byte is eight
; pixels of FOUR planes, and font_char reaches them by selecting Set/Reset per
; cell and masking per ROW - two `out`s a cell and, per row, another `out` and
; one or two LATCHED read-modify-writes. But that is a property of the CELL
; loop and not of the adapter: a plane is one bit of the colour INDEX, and a
; run has ONE pair of colours, so which planes vary and which are constant is
; a question the run answers once.
;
;   A = fg & ~bg   the planes that want the glyph byte
;   B = ~fg & bg   ...and the planes that want its complement
;   C = ~(fg ^ bg) the planes both colours agree on - a CONSTANT byte, which
;                  is exactly what Set/Reset is for
;
; With GC1 (Enable Set/Reset) = C and GC0 = fg, one CPU byte written at Bit
; Mask FF gives C its constant and hands A|B the data. **The Bit Mask being FF
; is what removes the read**: every bit comes from Set/Reset or from the CPU,
; so no latch has to be loaded first. The `out`s go with it - they are the
; run's now, not the cell's.
;
; One pass therefore serves any pair where A or B is EMPTY, which is to say
; where one colour's plane bits are a SUBSET of the other's: black on white,
; white on black, black on any colour, any colour on white, white on any
; colour. That is every pen this system sets bar one - the Color theme's
; disabled chrome, CDGRAY on CLGRAY (8 against 7), which shares no plane in
; either direction and takes .slow. 6.1.10 has the census.
;
; The composed byte is the MONO PATH'S OWN - (glyph & xm) ^ bm, with xm = FF
; and bm = 00 to write the glyph or FF to write its complement - so the row
; pass, the glyph table and 6.1.8's trailing span below are shared code and
; not a second renderer. That is the whole reason this is a prologue and not
; a routine.
.planar:
    cmp dx, [vid_chm8]          ; the run's y, once, exactly as on the mono
    ja .out                     ; path (SPEC.md 6.1.6)
    cmp cx, [vid_cwm8]          ; ...and a negative x goes per cell, same rule
    ja .cells
    push cx                     ; the grouping spends AX and BX only, but
    push dx                     ; gfx_rowbase below spends CX and DX
    mov al, [font_rn_fg]
    mov ah, [font_rn_bg]
    and ax, 0x0F0F              ; a colour is four planes and nothing else
    mov bl, al
    xor bl, ah                  ; BL = the planes the two colours differ on
    mov bh, al
    and bh, bl                  ; BH = A, the planes that want the glyph
    and ah, bl                  ; AH = B, the planes that want its complement
    or bh, bh
    jz .plcpl                   ; A empty: every varying plane wants ~glyph
    or ah, ah
    jnz .plno                   ; both non-empty: two passes, not this one
    mov byte [font_rn_bm], 0x00 ; B empty: the CPU byte IS the glyph
    jmp short .plset
.plcpl:
    mov byte [font_rn_bm], 0xFF
.plset:
    mov byte [font_rn_xm], 0xFF ; the compose is a pass-through either way
    mov byte [font_rn_pl], 1
    not bl
    and bl, 0x0F                ; BL = C, the planes Set/Reset answers for
    mov ah, al                  ; ...with fg's bits, and on those planes fg
    mov al, 0                   ; and bg agree, so either colour would do
    mov dx, VGA_GC
    out dx, ax                  ; GC0 Set/Reset = the constant planes' colour
    mov ah, bl
    mov al, 1
    out dx, ax                  ; GC1 Enable Set/Reset = C
    cld
    pop dx
    pop cx
    push cx
    push dx
    mov ax, dx
    call gfx_rowbase            ; AX = rowbase(y), once for the whole run
    pop dx
    pop cx
    mov di, cx
    shr di, 1                   ; three single shifts, not one by CL: 6 clocks
    shr di, 1                   ; against 20, and the mono prologue's reason
    shr di, 1
    add di, ax                  ; DI = the run's first framebuffer byte
    jmp .p1pre
.plno:                          ; the pair shares no plane in either direction
    pop dx                      ; and needs two passes: .slow draws it
    pop cx
%endif

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
%ifdef KERN_BIG
    cmp byte [font_rn_pl], 0    ; SPEC.md 6.1.10: the planar prologue enabled
    je .outd                    ; Set/Reset on the planes both colours agree
    push ax                     ; on, and vga12.inc's standing contract is that
    push dx                     ; every routine leaves Enable Set/Reset = 0.
    mov dx, VGA_GC              ; GC0 is NOT part of that resting state - the
    mov ax, 0x0001              ; Bit Mask and the Map Mask are, and neither
    out dx, ax                  ; was touched - so only GC1 is put back
    pop dx
    pop ax
.outd:
%endif
    GFXDLEAVE
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%ifdef KERN_BIG
.cells_nx:                      ; SPEC.md 39.14.6: per cell and UNTRANSLATED,
    mov al, [es:si]             ; so each font_char resolves its own display -
    or al, al                   ; and no GFXDLEAVE, because no enter was taken
    jz .out_nx
    inc si
    call font_run_scell
    add cx, 8
    jmp short .cells_nx
.out_nx:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; fnt_rn_edge - internal: one of an unaligned run's two edge COLUMNS
;               (SPEC.md 6.1.11)
;
; in:       DI = the column's first framebuffer byte, DL = the mask (a 1 bit is
;           the RUN's), SI -> the 8 bytes that row pass stashed, ES = the
;           framebuffer, [font_rn_pl] = which renderer
; out:      nothing; clobbers AX, BX, SI, DI, BP, flags
;
; A column and not a row because the mask is the RUN's - constant for all eight
; rows - so a VGA Bit Mask is set ONCE here instead of sixteen times inside the
; interior's stosb walk. That is the whole reason the row pass stashes rather
; than merging as it goes.
;
; On VGA the Bit Mask IS the merge: the bits outside it come from the latches,
; which is what the read is for. On a 1bpp adapter there are no latches and the
; merge is done in the register.
; -----------------------------------------------------------------------------
fnt_rn_edge:
    push cx
    push dx
%ifdef KERN_BIG
    cmp byte [font_rn_pl], 0
    je .mono
    push ax
    mov dh, dl                  ; GC8 Bit Mask = the run's bits, once
    mov al, 8
    mov ah, dh
    mov dx, VGA_GC
    out dx, ax
    pop ax
    xor bp, bp
.vrow:
    mov al, [si]
    inc si
    mov ah, [es:di]             ; the latches; the masked-out bits come back
    mov [es:di], al             ; from them untouched
    FONT_RN_NEXTROW
    inc bp
    cmp bp, 8
    jb .vrow
    mov ax, 0xFF08              ; ...and the Bit Mask back to its resting FF
    mov dx, VGA_GC              ; (vga12.inc's standing contract, SPEC.md 1.7)
    out dx, ax
    jmp short .out
%endif
.mono:
    mov bl, dl
    not bl                      ; BL = the bits that are NOT ours
    xor bp, bp
.mrow:
    mov al, [si]
    inc si
    and al, dl
    mov ah, [es:di]
    and ah, bl
    or al, ah
    mov [es:di], al
    FONT_RN_NEXTROW
    inc bp
    cmp bp, 8
    jb .mrow
.out:
    pop dx
    pop cx
    ret

%endif
; -----------------------------------------------------------------------------
; font_run_cell - internal: one opaque 8x8 cell, byte-aligned, [vid_mono] path
; in:       AL = character, CX = x (multiple of 8), DX = y, DI = the cell's
;           first framebuffer byte, [font_rn_xm]/[font_rn_bm] = the run's masks
; out:      nothing (all registers preserved)
;
; The row loop is font_char_bb's with the read-modify-write replaced by a
; store: the cell owns its whole byte, so there is nothing underneath to
; preserve, and the background arrives in the same write as the glyph. The
; byte is (glyph & fg) | (~glyph & bg) computed as (glyph & (fg^bg)) ^ bg,
; both terms handed in by the caller (SPEC.md 6.1.5).
;
; THERE IS NO PLANE LOOP, and that is font_run_x's gate rather than an
; assumption here: [vid_mono] is tested three instructions above the .cell
; loop, and a 1bpp target has one plane. A planar run takes .slow (SPEC.md
; 6.1), so the 4-plane walk this used to carry could never run.
;
; What is left is exactly what differs between two cells of one word: the
; character, the x edge test, the clip test and one byte of DI. Everything a
; RUN shares - the y test, both masks, the row base - is the caller's, once
; (SPEC.md 6.1.6).
; -----------------------------------------------------------------------------
font_run_cell:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp cx, [vid_cwm8]          ; x only: the run's y was tested once, by the
    ja .out                     ; caller (SPEC.md 6.1.6)
    cmp word [wm_clip_n], 0     ; the cell against the region in ROWS, exactly
    je .noclip                  ; as its glyphs are, background with them
    push ax                     ; (SPEC.md 11.3.2)
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_rows           ; CX and DX are spent here and NOT put back:
    pop ax                      ; nothing below reads them, the masks arriving
    jc .out                     ; in CX from memory instead
    jmp short .have_rows
.noclip:
    mov byte [wm_clip_r0], 0
    mov byte [wm_clip_rn], 8
.have_rows:
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
    mov si, font_zero           ; an unprintable character is a blank cell -
.have:                          ; which still has to be PAINTED here

    mov es, [vid_rseg]          ; the framebuffer (SPEC.md 39.3); DI arrived
    mov cl, [font_rn_xm]        ; pointing at this cell's first byte
    mov ch, [font_rn_bm]        ; CL = ink XOR background, CH = background
    mov bh, [wm_clip_rn]        ; row counter - the rows the region allows
    mov al, [wm_clip_r0]        ; ...and where they start. AL is spent (SI
    mov ah, 0                   ; carries the glyph), BL is free until .row
    test al, 1                  ; SPEC.md 6.1.12: the caller seeded the mask
    jz .r0even                  ; for the run's y, and a clipped cell starts
    xor cl, [font_rn_dt]        ; r0 rows below it - odd means the other phase
.r0even:
    or al, al
    jz .row                     ; the whole cell: the common case skips this
    add si, ax                  ; the glyph's first drawable row...
    mov bl, al
.skip:
    add di, [cs:vid_rowadd]     ; ...and DI down to the screen row it goes on.
    test di, [cs:vid_wrapbit]   ; Stepped rather than multiplied because the
    jz .sknw                    ; layout is BANKED (SPEC.md 39.3) - r0 is 1..7
    add di, [cs:vid_wrapfix]    ; on a path that until now drew nothing at all
.sknw:
    dec bl
    jnz .skip
.row:
    mov al, [ss:si]             ; AL = the glyph row
    inc si
    and al, cl                  ; a set bit picks ink, a clear bit background
    xor al, ch                  ; - the same byte, three instructions fewer
    mov [es:di], al             ; the cell owns the byte: store, never merge
    xor cl, [font_rn_dt]        ; SPEC.md 6.1.12: 0 unless this is disabled
    add di, [cs:vid_rowadd]     ; (gfx_nextrow inlined - see font_char_bb)
    test di, [cs:vid_wrapbit]
    jz .nw
    add di, [cs:vid_wrapfix]
.nw:
    dec bh
    jnz .row
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
    call wm_clip_rows           ; ...in ROWS (SPEC.md 11.3.2), so a cell an
    jc .out                     ; edge crosses draws its visible band rather
                                ; than nothing; a vertical cut still refuses
    push ax                     ; bank the cell's own origin - the fill wants
    push bx                     ; the band, font_char wants the cell
    push cx
    mov ch, 0
    mov cl, [wm_clip_r0]
    add bx, cx                  ; BX = the band's first row...
    mov cl, [wm_clip_rn]
    mov dx, bx
    add dx, cx
    dec dx                      ; ...and DX its last
    pop cx
    push ax                     ; (the pen loads clobber AL, which is x1's low
    mov al, [font_rn_bg]        ; byte - gfx_fill still needs the rect)
    mov [gfx_color], al
    pop ax
    call gfx_fill               ; the BAND, not the cell: the erase has to
    pop bx                      ; cover what the glyph is about to letter and
    pop ax                      ; nothing else, or the two disagree again
    push ax
    mov al, [font_rn_fg]
    mov [gfx_color], al
    pop ax
    mov cx, ax                  ; font_char takes x,y in CX,DX
    mov dx, bx
    mov al, [font_rn_ch]
    call font_char              ; ...and asks the region the same question,
.out:                           ; so it letters exactly the rows just erased
    pop dx
    pop cx
    pop bx
    pop ax
    ret


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
section .lowbss                 ; LOW_SEG (SPEC.md 2.1): 760 bytes is the
                                ; largest single object the kernel's own 64KB
                                ; segment was carrying, against five reads -
                                ; the glyph-row loops - so it costs five ss:
                                ; prefixes and buys the whole table back for
                                ; KERN_CODE_MAX. font_init fills both from the ROM.
font_glyphs:    resb 760        ; glyphs 32..126, 8 bytes per row (FONT_BYTES)
font_zero:      resb 8          ; the blank cell an unprintable character
                                ; draws. It has to live BESIDE the table: the
                                ; row loops take either in SI and read it with
                                ; one segment, and .lowbss is not zeroed by
                                ; anything, so font_init clears it by hand

section .bss
font_rn_fg:     resb 1          ; font_run: the run's two colours as the
font_rn_bg:     resb 1          ; caller gave them, for the .slow path
font_rn_dt:     resb 1          ; SPEC.md 6.1.12: what FONT_RN_ROWEND XORs
                                ; into DL at every row - 0 for an ordinary
                                ; run, and the run's whole mask for DISABLED
                                ; text, whose two checkerboard phases the
                                ; mask partitions
font_rn_xm:     resb 1          ; ...and the run's reduced masks (SPEC.md
font_rn_bm:     resb 1          ; 6.1.5/6.1.6): ink XOR background, and
                                ; background. Derived ONCE per run and read
                                ; into CX by every cell, which is why they are
                                ; here and not in registers: CX is the x the
                                ; cell loop is walking
%ifdef KERN_BIG
font_rn_pl:     resb 1          ; SPEC.md 6.1.10: 1 = the masks came from the
                                ; PLANAR prologue, so the per-cell escapes go
                                ; to font_run_scell rather than font_run_cell
                                ; and the Graphics Controller is owed a reset
%endif
%ifdef KERN_BIG
font_rn_k7:     resb 1          ; SPEC.md 6.1.11: the pen's phase, x & 7. 0 is
                                ; the aligned path and every other value is the
                                ; shift the row pass runs at
font_rn_send:   resw 1          ; ...and where the cell walk stops, because CX
                                ; is carrying the shift and the previous byte
                                ; and has nothing left to count with
font_rn_le:     resb 8          ; the two edge bytes each row composed, held
font_rn_re:     resb 8          ; back for the column passes. ADJACENT and in
                                ; this order is not required - fnt_rn_edge is
                                ; handed a pointer - but they are 16 bytes and
                                ; keeping them together is one cache line of
                                ; a reader's attention
%endif
font_rn_ch:     resb 1          ; font_run_scell's character, parked because
                                ; AL is the rect's low byte by the time the
                                ; letter is drawn
font_rn_n2:     resw 1          ; cells drawn singly, before the trailing span
font_rn_k:      resw 1          ; ...the span's length, at least 1
font_rn_n:      resw 1          ; SPEC.md 6.1.7: cells the run will draw, and
font_rn_tab:    resw FONT_RN_MAX ; a glyph pointer for each. 180 bytes, taken
                                ; deliberately: it is what lets the drawing
                                ; pass go ROW-major, which pays gfx_nextrow
                                ; once a row instead of once a row per cell
font_bb_si:     resw 1          ; bb path: glyph pointer, reloaded per plane
font_bb_di:     resw 1          ; bb path: cell offset, reloaded per plane
font_dith:      resb 1          ; 1 = this glyph is disabled text on a mono
                                ; adapter (SPEC.md 47); font_ink decides it
                                ; per glyph and writes it on every path
font_dmask:     resb 1          ; ...and the row's half of the checkerboard

%ifdef BAKED_FONT
; =============================================================================
; The baked typeface (SPEC.md 6.2), in the BOOT OVERLAY (SPEC.md 2.5).
;
; 760 bytes that cost neither guard anything: .ovl lands in the FAT window and
; is written over by drv_boot's first mount, which is the last thing kmain
; does - and this runs well before it, beside ovl_clk_init and drv_snd_sniff.
; So the whole price of carrying our own font is ONE floppy sector at boot.
;
; It is an overlay ENTRY rather than a table .text reaches into, and that is
; the section's own contract rather than taste: `.ovl` has a vstart of its own
; and DS is KERNEL_SEG throughout it, so overlay data is reachable only
; through CS - which is what this does, and what kernel.asm's section header
; says anything added here would have to. tools/os88ovlchk.py cannot see a
; data reference across that boundary, only a call, so the rule is worth
; keeping to rather than working around.
; =============================================================================
section .ovl
; -----------------------------------------------------------------------------
; ovl_font_init - copy the baked typeface into font_glyphs
;
; in:       nothing (CS = the blob's, DS = KERNEL_SEG, SS = LOW_SEG)
; out:      nothing
; clobbers: nothing - it is called once, from kmain, exactly where the ROM
;           probe used to be called, and owes the same contract
; -----------------------------------------------------------------------------
ovl_font_init:
    push ax
    push cx
    push si
    push di
    push ds
    push es

    push cs                     ; the glyphs are our OWN data: DS has to leave
    pop ds                      ; KERNEL_SEG to reach them, and comes back below
    mov si, ovl_font_bits
    mov ax, LOW_SEG             ; the table is in .lowbss (SPEC.md 2.1), so the
    mov es, ax                  ; copy runs segment to segment and touches
    mov di, font_glyphs         ; neither one's own
    mov cx, FONT_BYTES / 2
    cld
    rep movsw

    mov cx, 4                   ; ...and the blank cell beside it, which
    mov di, font_zero           ; nothing else zeroes: .lowbss is nobits. ES is
    xor ax, ax                  ; still LOW_SEG from the copy above, which is
    rep stosw                   ; what stosw needs

    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    retf                        ; FAR: kmain reaches this through OVLCALL

ovl_font_bits:
%include "font8x8.inc"          ; GENERATED by tools/os88font.py from the
                                ; fonts/*.f8 that FONT= named (-I build/)
%endif
section .text

%ifdef SNAPAUDIT
; SPEC.md 11.94.2's counters (see SNAPAUDIT_TALLY at the top of this file).
; In .text with real initialisers rather than .bss, because -f bin zeroes
; nothing and a host script reads these through os88sym.py by name.
;
; Four tables, not two: what a WINDOW draws inside its own callback and what
; the KERNEL draws around it are different questions with different owners, and
; mixed together the chrome's dozen title-bar glyphs are indistinguishable from
; an app's own skew. wm_pkgcall keeps the depth.
snap_hchar: times 8 dw 0        ; font_char inside a window callback
snap_hrun:  times 8 dw 0        ; font_run  inside a window callback
snap_kchar: times 8 dw 0        ; font_char outside one - chrome, menus, desktop
snap_krun:  times 8 dw 0        ; font_run  outside one
section .lowbss                 ; ...but the LOG goes to .lowbss, which costs
snap_calls: resw SNAP_NCALL*(SNAP_ENT/2)   ; the 64KB segment nothing (SPEC.md
section .text                   ; 2.1). 192 bytes here is what put `make
                                ; SNAPAUDIT=1` 15 bytes over guard 2, and guard
                                ; 2 is the one nobody can raise. It needs no
                                ; initialiser and that is not luck: snap_ncall
                                ; below stays in .text at 0, tools/os88snap.py
                                ; reads IT first and returns early when it is
                                ; 0, and never reads past it - so nothing ever
                                ; looks at a byte of this that was not written.
                                ; os88sym.linear answers LOW_SEG for it, so the
                                ; reader needs no change. 11.94.2's log:
                                ; (pen x, pen y,
                                ; character, return address) per off-grid
                                ; glyph in the audited window - what turns
                                ; "this app is off-grid" into the STRING
snap_ncall: dw 0                ; bytes used of it, so 0 means empty
snap_tmpc:  dw 0                ; the log's scratch: the caller's AX and
snap_tsave: dw 0                ; wm_draw_title's banked snap_cur: the
                                ; caption is CHROME (SPEC.md 11.94.2)
snap_tmpr:  dw 0                ; its return address, banked while BX is
                                ; needed for the slot address
snap_cur:   dw 0                ; the window whose callback is running, 0 =
                                ; none (wm_pkgcall stacks it)
snap_win:   dw 0                ; the HOST's filter: only this window's glyphs
                                ; go to snap_h*, 0 = any. Written from outside
                                ; the guest by tools/os88snap.py
%endif
