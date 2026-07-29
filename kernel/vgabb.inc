; =============================================================================
; os8088 - vgabb.inc
;
; Optional double buffering (SPEC.md 32). bb_init probes conventional RAM
; via int 12h: at >= DB_MIN_KB it arms [bb_on] and from then on every public
; drawing entry in vga12.inc (plus font_char and ico_core) renders into a
; 4-plane back buffer at BB_SEG instead of VRAM, and gfx_flush copies the
; dirty rectangle to VRAM through the Sequencer Map Mask. Below the floor
; [bb_on] stays 0 and every entry falls through to its original VRAM body.
;
; RAM has no latches or Set/Reset, so the bb_* twins do the VGA ALU's work
; in software, one plane at a time: solid ops or/and the edge masks per the
; [gfx_color] plane bit (interiors are rep stosb of FF/00), XOR ops xor the
; edge masks and `not` whole interior bytes. Plane p lives at segment
; BB_SEG + p*BB_PLANE_PARA with the same 80-byte rows as VRAM, so
; vga_rect_setup's masks and offsets serve both worlds; every solid/XOR
; primitive here funnels through it. Pixel/hline/vline are degenerate
; fills - a rect setup costs one mul, same as the dedicated VRAM loops.
;
; Concurrency: every routine here except bb_init runs with the gfx lock
; held (SPEC.md 1.6) - the dirty rect and vga12's rect scratch need no
; interrupt guard, and the mouse ISR defers the cursor while we flush.
; The cursor itself is never in the back buffer: mouse.inc goes through
; vga_save_vram/vga_restore_vram and draws straight to VRAM.
; =============================================================================

bb_on           db 0            ; double buffering live right now? Off at
                                ; boot by choice - the Control Panel's
                                ; Display page (SPEC.md 31.3) flips it via
                                ; bb_set, which may run at any time. Every
                                ; drawing entry dispatches on this byte, so
                                ; a flip takes effect from the next call.
                                ; Initialized data, NOT .bss - nothing
                                ; zeroes .bss at boot.

bb_avail        db 0            ; did the boot RAM probe clear DB_MIN_KB?
                                ; Set once by bb_init; gates bb_set, so on a
                                ; small machine [bb_on] can never become 1
                                ; and the Display page says why.

bb_mono         db 1            ; are all four back-buffer planes identical?
                                ; True from boot (bb_init zeroes them) and
                                ; for every pixel drawn in colour 0 or 15 -
                                ; which is the whole System-1 UI, since its
                                ; greys are 0/15 dither. While set, gfx_flush
                                ; copies ONE plane with Map Mask = 0Fh and the
                                ; hardware fans it out to all four: a quarter
                                ; of the VRAM writes, and no plane-sequential
                                ; colour fringing (SPEC.md 32). The first draw
                                ; in any other colour - a Minesweeper digit -
                                ; clears it for good; the planes are always
                                ; fully rendered, so the flush just falls back
                                ; to four passes with nothing to repair.
                                ; Initialized data for the same reason as
                                ; bb_on.

; -----------------------------------------------------------------------------
; bb_init - probe conventional RAM; arm and clear the back buffer at >= 512KB
;
; Called from kmain right after vga_mode12 (the mode set cleared VRAM, so
; zeroed planes start in sync) and before the first drawing call. Runs on
; task 0, so the int 12h BIOS call honors SPEC.md 7.
; in:       -
; out:      [bb_on] = 1 and planes zeroed, or [bb_on] = 0 (fallback)
; clobbers: flags
; -----------------------------------------------------------------------------
bb_init:
    push ax
    int 0x12                    ; AX = conventional memory, KB
    cmp ax, DB_MIN_KB
    jb .done                    ; too small: the Control Panel will not offer
    mov byte [bb_avail], 1      ; it, and [bb_on] can never become 1
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_sync - seed all four back-buffer planes from VRAM
;
; Read Map Select (GC4) picks the plane to read; each pass is a straight
; 0x9600-byte copy at identical offsets, so the buffer ends up an exact
; mirror of the screen. GC4 goes back to its default afterwards (SPEC.md 1
; rule 7). Internal to bb_set.
; in:       caller HOLDS the gfx lock - the cursor must be hidden, or it
;           would be captured into the buffer and smeared by the next flush
; clobbers: flags
; -----------------------------------------------------------------------------
bb_sync:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    cld
    mov ax, VGA_SEG
    mov ds, ax                  ; source = VRAM, one plane at a time
    mov bx, BB_SEG              ; BX = destination plane segment
    mov ah, 0                   ; AH = plane index
.plane:
    mov al, 4                   ; GC4: Read Map Select = this plane
    mov dx, VGA_GC
    out dx, ax
    push ax                     ; the copy needs CX, so park the index
    mov es, bx
    xor si, si
    xor di, di
    mov cx, 0x9600 / 2
    rep movsw
    pop ax
    add bx, BB_PLANE_PARA
    inc ah
    cmp ah, 4
    jb .plane
    mov dx, VGA_GC
    mov ax, 0x0004              ; GC4 back to plane 0, the default
    out dx, ax
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_set - turn double buffering on or off at runtime (SPEC.md 31.3, 32)
;
; in:       AL = 0 (off) or 1 (on); caller HOLDS the gfx lock
; out:      [bb_on] updated (or left alone: turning on needs [bb_avail])
; clobbers: flags
;
; Turning ON seeds the buffer from VRAM first, so the very next flush cannot
; push stale pixels over live ones - the buffer has never been written while
; disabled, and after a disable it is arbitrarily out of date. Turning OFF
; flushes whatever is still dirty, so nothing drawn under the old mode is
; stranded in RAM. Both directions are no-ops if already in that state, which
; keeps a repeated click from re-copying 150KB.
; -----------------------------------------------------------------------------
bb_set:
    push ax
    or al, al
    je .off
    cmp byte [bb_avail], 0
    je .out                     ; not enough RAM: stays off, silently
    cmp byte [bb_on], 0
    jne .out                    ; already on
    call bb_sync
    call bb_dirty_reset
    mov byte [bb_on], 1         ; publish last: every bb_* entry keys off it
    jmp .out
.off:
    cmp byte [bb_on], 0
    je .out                     ; already off
    call gfx_flush              ; land anything still pending in VRAM
    mov byte [bb_on], 0
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_mono_chk - retire [bb_mono] if [gfx_color] is not 0 or 15
;
; Called from gfx_fill and font_char - the only two places a colour other
; than black or white can reach the screen. Gray fills dither 0/15, XOR flips
; every plane alike and icons are white-under/black-over, so all three
; preserve the invariant untouched.
;
; Deliberately on the mode-independent path, not the bb_* twins: double
; buffering can be switched on at any time (SPEC.md 31.3) and bb_sync seeds
; the planes from VRAM, which is only plane-identical if nothing but 0/15
; has ever been drawn - including while the buffer was off.
; clobbers: flags only
; -----------------------------------------------------------------------------
bb_mono_chk:
    push ax
    mov al, [gfx_color]
    and al, 0x0F
    jz .ok                      ; 0 = every plane 00
    cmp al, 0x0F
    je .ok                      ; 15 = every plane FF
    mov byte [bb_mono], 0
.ok:
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_dirty_reset - internal: mark the dirty rect empty (dx1 > dx2)
; clobbers: nothing
; -----------------------------------------------------------------------------
bb_dirty_reset:
    push ax
    mov ax, 0x7FFF
    mov [bb_dx1], ax
    mov [bb_dy1], ax
    xor ax, ax
    mov [bb_dx2], ax
    mov [bb_dy2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_dirty - internal: union a clipped rect into the dirty bounding box
; in:       AX = left byte column, BX = right byte column (0..79),
;           CX = top row, DX = bottom row (0..479); all inclusive and valid
; out:      -
; clobbers: flags only
; -----------------------------------------------------------------------------
bb_dirty:
    cmp ax, [bb_dx1]
    jge .l_ok
    mov [bb_dx1], ax
.l_ok:
    cmp bx, [bb_dx2]
    jle .r_ok
    mov [bb_dx2], bx
.r_ok:
    cmp cx, [bb_dy1]
    jge .t_ok
    mov [bb_dy1], cx
.t_ok:
    cmp dx, [bb_dy2]
    jle .b_ok
    mov [bb_dy2], dx
.b_ok:
    ret

; -----------------------------------------------------------------------------
; bb_dirty_rect - internal: union the rect vga_rect_setup just clipped
;                 (vga_lcol/vga_span/vga_y1/vga_rows must be valid).
;                 Callers invoke it straight after vga_rect_setup, while
;                 AX..DX are dead - so unlike bb_dirty it may clobber them.
; clobbers: ax, bx, cx, dx, flags
; -----------------------------------------------------------------------------
bb_dirty_rect:
    mov ax, [vga_lcol]
    mov bx, ax
    add bx, [vga_span]
    mov cx, [vga_y1]
    mov dx, cx
    add dx, [vga_rows]
    dec dx
    jmp bb_dirty                ; its ret returns to our caller

; -----------------------------------------------------------------------------
; gfx_flush - copy the dirty back-buffer rect to VRAM, plane by plane
;
; Caller holds the gfx lock (cursor hidden - the flush may overwrite the
; cursor cell, which must therefore not be on screen). Plain replace-mode
; writes through SEQ2 Map Mask, one plane at a time; the GC keeps its
; default state throughout. No-op when double buffering is off or the
; dirty rect is empty. Public: menu_track and ui_drag call it mid-lock;
; gfx_unlock calls it before cursor_show.
; in:       -
; out:      dirty rect reset
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_flush:
    cmp byte [bb_on], 0
    je .off
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov si, [bb_dx1]
    cmp si, [bb_dx2]
    jg .clean
    cld
    mov bp, [bb_dx2]
    sub bp, si
    inc bp                      ; BP = bytes per row
    mov ax, ROW_BYTES
    mul word [bb_dy1]           ; AX = top row * 80 (DX clobbered)
    add si, ax                  ; SI = offset of the top-left dirty byte
    mov bx, [bb_dy2]
    sub bx, [bb_dy1]
    inc bx                      ; BX = row count
    mov ax, VGA_SEG
    mov es, ax
    mov cx, BB_SEG              ; CX = current plane segment
    mov al, 1                   ; Map Mask bit: one plane per pass...
    cmp byte [bb_mono], 0
    je .setp
    mov al, 0x0F                ; ...unless every plane holds the same bytes:
                                ; enable all four and copy plane 0 once, which
                                ; the shl below turns into a single iteration
                                ; (0Fh << 1 = 1Eh already trips the 10h test).
                                ; Quarter the VRAM traffic, and all four
                                ; planes land together so no pixel is ever
                                ; briefly the wrong colour (SPEC.md 32).
.setp:
    mov [bb_plane], al
.plane:
    mov dx, VGA_SEQ
    mov ah, [bb_plane]
    mov al, 2                   ; SEQ2: Map Mask = this plane only
    out dx, ax
    push bx
    push si
    mov di, si
    mov ds, cx                  ; source = plane segment (kernel vars are
                                ; unreachable until the push ss/pop ds below)
.row:
    push cx
    mov cx, bp
    shr cx, 1
    rep movsw
    jnc .even
    movsb                       ; odd width: one tail byte
.even:
    pop cx
    add si, ROW_BYTES
    sub si, bp
    add di, ROW_BYTES
    sub di, bp
    dec bx
    jnz .row
    push cs                     ; back to kernel data. NOT `push ss / pop ds`:
    pop ds                      ; SS is LOW_SEG now (SPEC.md 2.1). CS is the
                                ; kernel segment for everything in .text, and
                                ; this costs no register.
    pop si
    pop bx
    add cx, BB_PLANE_PARA
    shl byte [bb_plane], 1
    test byte [bb_plane], 0x10
    jz .plane
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2: Map Mask back to all planes
    out dx, ax
    call bb_dirty_reset
.clean:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.off:
    ret

; -----------------------------------------------------------------------------
; bb_fill / bb_fill_gray / bb_xor_fill - back-buffer twins of gfx_fill,
; gfx_fill_gray and gfx_xor_fill (same register contracts: AX = x1,
; BX = y1, CX = x2, DX = y2; clobbers flags). One shared core, bb_rect,
; parameterized by [bb_mode]:
;
;   BBM_SOLID - per plane, the [gfx_color] bit picks a 00/FF byte; edges
;               become (dest & ~mask) | (value & mask), interiors stosb.
;   BBM_GRAY  - one AA/55 pattern for all planes (color 15/0 per pixel
;               bit), toggled each row via [bb_altm] = FF; same edge math.
;   BBM_XOR   - XOR 0Fh flips every plane identically: edges xor the
;               mask, interior bytes are complemented.
; -----------------------------------------------------------------------------
BBM_SOLID equ 0
BBM_GRAY  equ 1
BBM_XOR   equ 2

bb_fill:
    mov byte [bb_mode], BBM_SOLID
    jmp bb_rect

bb_fill_gray:
    mov byte [bb_mode], BBM_GRAY
    jmp bb_rect

bb_xor_fill:
    mov byte [bb_mode], BBM_XOR
    ; fall through to bb_rect

bb_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call vga_rect_setup
    jc .done
    call bb_dirty_rect
    mov al, 0xAA                ; gray pattern for the top clipped row
    test byte [vga_y1], 1
    jz .par
    mov al, 0x55
.par:
    mov [bb_pat], al            ; (solid overwrites this per plane below;
    mov byte [bb_altm], 0       ;  XOR ignores it)
    cmp byte [bb_mode], BBM_GRAY
    jne .planes
    mov byte [bb_altm], 0xFF    ; gray: AA <-> 55 each row
.planes:
    mov dx, BB_SEG              ; DX = plane segment
    mov bl, [gfx_color]         ; BL = color bits, consumed LSB-first
    mov bh, 4                   ; BH = plane counter
.plane:
    mov es, dx
    cmp byte [bb_mode], BBM_SOLID
    jne .go
    shr bl, 1                   ; solid: 00/FF from this plane's color bit
    sbb al, al
    mov [bb_pat], al
.go:
    call bb_plane_op
    add dx, BB_PLANE_PARA
    dec bh
    jnz .plane
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

; -----------------------------------------------------------------------------
; bb_plane_op - internal: one plane of a bb_rect op - left/right masked
;               edge columns plus the whole-byte interior.
; in:       ES = plane segment; [bb_mode]/[bb_pat]/[bb_altm]; vga_* rect
;           scratch from vga_rect_setup
; clobbers: ax, cx, si, di, bp, flags (preserves BX, DX)
; -----------------------------------------------------------------------------
bb_plane_op:
    mov ah, [vga_lmsk]          ; left edge column (whole rect if 1 byte wide)
    mov di, [vga_off]
    call bb_col
    mov bp, [vga_span]
    or bp, bp
    jz .out
    mov ah, [vga_rmsk]          ; right edge column
    mov di, [vga_off]
    add di, bp
    call bb_col
    dec bp                      ; interior bytes per row
    jz .out
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
    mov ah, [bb_pat]
    cmp byte [bb_mode], BBM_XOR
    je .xrow
.irow:
    mov al, ah                  ; AX = pattern:pattern, so stosw is safe for
    mov cx, bp                  ; both the solid 00/FF and the AA/55 dither
    shr cx, 1
    push di
    rep stosw                   ; string ops leave the shr's CF alone
    jnc .ieven
    stosb                       ; odd interior width: one tail byte
.ieven:
    pop di
    add di, ROW_BYTES
    xor ah, [bb_altm]           ; gray alternates, solid stays
    dec si
    jnz .irow
    jmp .out
.xrow:
    mov cx, bp
    push di
.xbyte:
    not byte [es:di]
    inc di
    loop .xbyte
    pop di
    add di, ROW_BYTES
    dec si
    jnz .xrow
.out:
    ret

; -----------------------------------------------------------------------------
; bb_col - internal: one masked byte column of [vga_rows] rows at ES:DI.
;          XOR mode: dest ^= mask. Else: dest = (dest & ~mask) |
;          (pattern & mask), pattern xor-stepped by [bb_altm] per row.
; in:       AH = mask, DI = top byte, ES = plane; [bb_mode]/[bb_pat]/[bb_altm]
; clobbers: flags only
; -----------------------------------------------------------------------------
bb_col:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bl, ah
    not bl                      ; BL = ~mask
    mov ch, [bb_pat]            ; CH = running pattern
    mov cl, [bb_altm]
    mov dx, [vga_rows]
    cmp byte [bb_mode], BBM_XOR
    je .xrow
.row:
    mov al, ch
    and al, ah                  ; AL = pattern & mask
    and [es:di], bl
    or  [es:di], al
    xor ch, cl
    add di, ROW_BYTES
    dec dx
    jnz .row
    jmp .out
.xrow:
    xor [es:di], ah
    add di, ROW_BYTES
    dec dx
    jnz .xrow
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_fill_pat - back-buffer twin of gfx_fill_pat (SPEC.md 5/32): the 8-row
;               pattern at [gfx_pat], staged into vga_patbuf, written
;               identically to all four planes (colors 0/15 per pixel bit,
;               [bb_mono] untouched). bb_col's fixed-byte/xor-toggle model
;               cannot index 8 rows, so this brings its own column helper.
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
bb_fill_pat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call vga_rect_setup
    jc .done
    call bb_dirty_rect
    call vga_pat_stage          ; vga_patbuf <- the 8 bytes at [gfx_pat]
    mov dx, BB_SEG              ; DX = plane segment
    mov bp, 4                   ; BP = plane counter
.plane:
    mov es, dx
    mov di, [vga_off]
    mov cl, [vga_lmsk]          ; left edge column (whole rect if 1 byte)
    call bb_patcol
    mov si, [vga_span]
    or si, si
    jz .next
    mov di, [vga_off]
    add di, si
    mov cl, [vga_rmsk]          ; right edge column
    call bb_patcol
    dec si                      ; SI = interior bytes per row
    jz .next
    mov di, [vga_off]
    inc di
    mov bx, [vga_y1]
    and bx, 7
    push bp
    mov bp, [vga_rows]
.irow:
    mov al, [vga_patbuf+bx]
    mov ah, al                  ; byte uniform across the row: stosw + odd
    mov cx, si                  ; tail via the shr carry, like bb_plane_op
    shr cx, 1
    push di
    rep stosw
    jnc .ieven
    stosb
.ieven:
    pop di
    add di, ROW_BYTES
    inc bx
    and bx, 7
    dec bp
    jnz .irow
    pop bp
.next:
    add dx, BB_PLANE_PARA
    dec bp
    jnz .plane
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

; -----------------------------------------------------------------------------
; bb_patcol - internal: one masked byte column of [vga_rows] rows at ES:DI,
;             row byte from vga_patbuf by (y & 7):
;             dest = dest ^ ((dest ^ pat) & mask)
; in:       CL = mask, DI = top byte, ES = plane; vga_* rect scratch
; clobbers: AX, flags (preserves BX, SI, DI and CX)
; -----------------------------------------------------------------------------
bb_patcol:
    push bx
    push di
    push si
    mov si, [vga_rows]
    mov bx, [vga_y1]
    and bx, 7
.row:
    mov al, [vga_patbuf+bx]
    mov ah, [es:di]
    xor al, ah
    and al, cl
    xor al, ah
    mov [es:di], al
    inc bx
    and bx, 7
    add di, ROW_BYTES
    dec si
    jnz .row
    pop si
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; bb_xor_rect - back-buffer twin of gfx_xor_rect: 1px outline, XOR 0Fh.
;               Same edge decomposition as the VRAM version - each pixel
;               XORed exactly once, degenerate rects collapse - built from
;               four bb_xor_fill strips.
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; clobbers: flags
; -----------------------------------------------------------------------------
bb_xor_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = y1
    mov di, dx                  ; DI = y2
    mov dx, si
    call bb_xor_fill            ; top row (x1,y1)-(x2,y1)
    cmp di, si
    je .out                     ; y1 = y2: one line, done
    mov bx, di
    mov dx, di
    call bb_xor_fill            ; bottom row (x1,y2)-(x2,y2)
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .out                     ; only two rows: no vertical part
    push cx
    mov cx, ax
    call bb_xor_fill            ; left column (x1,y1+1)-(x1,y2-1)
    pop cx
    cmp cx, ax
    je .out                     ; x1 = x2: one column, done
    push ax
    mov ax, cx
    call bb_xor_fill            ; right column
    pop ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bb_save / bb_restore - back-buffer twins of gfx_save / gfx_restore, same
; rounding and buffer layout (SPEC.md 5) - a buffer saved from the back
; buffer restores to it byte-identically. One shared body, bb_xfer, with
; [bb_dir] picking the direction; restore additionally dirties the rect.
;   bb_save:    in AX/BX/CX/DX = rect, ES:DI = buffer; DI advances
;   bb_restore: in AX/BX/CX/DX = rect, ES:SI = buffer; SI advances
; ES preserved; clobbers flags.
; -----------------------------------------------------------------------------
bb_save:
    mov byte [bb_dir], 0
    jmp bb_xfer

bb_restore:
    mov byte [bb_dir], 1
    ; fall through to bb_xfer

bb_xfer:
    push ax
    push bx
    push cx
    push dx
    push bp
    call vga_rect_setup
    jc .done
    cmp byte [bb_dir], 0
    je .geom
    call bb_dirty_rect          ; restore writes the planes: mark dirty
.geom:
    mov bp, [vga_span]
    inc bp                      ; BP = bytes per row
    mov bx, BB_SEG
.plane:
    mov ax, [vga_off]           ; this plane's walking offset
    mov dx, [vga_rows]
    push ds
    push es
    cmp byte [bb_dir], 0
    jne .rst
    push si                     ; save: plane -> buffer; caller SI untouched
    mov si, ax
    mov ds, bx                  ; movsb source = this plane
.srow:
    mov cx, bp
    rep movsb                   ; buffer DI advances contiguously
    add si, ROW_BYTES
    sub si, bp
    dec dx
    jnz .srow
    pop si
    jmp .pnext
.rst:
    push di                     ; restore: buffer -> plane; caller DI untouched
    mov di, ax
    mov ax, es
    mov ds, ax                  ; movsb source = caller's buffer
    mov es, bx                  ; movsb dest = this plane
.rrow:
    mov cx, bp
    rep movsb
    add di, ROW_BYTES
    sub di, bp
    dec dx
    jnz .rrow
    pop di
.pnext:
    pop es
    pop ds
    add bx, BB_PLANE_PARA
    cmp bx, BB_SEG + 4*BB_PLANE_PARA
    jb .plane
.done:
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

bb_dx1          resw 1          ; dirty rect: left byte column (x/8)
bb_dx2          resw 1          ; dirty rect: right byte column
bb_dy1          resw 1          ; dirty rect: top row
bb_dy2          resw 1          ; dirty rect: bottom row
bb_plane        resb 1          ; flush: Map Mask plane bit
bb_dir          resb 1          ; xfer: 0 = save, 1 = restore
bb_mode         resb 1          ; rect core: BBM_SOLID / BBM_GRAY / BBM_XOR
bb_pat          resb 1          ; rect core: plane value / dither pattern
bb_altm         resb 1          ; rect core: per-row pattern toggle (0 or FF)

section .text
