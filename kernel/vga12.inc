; =============================================================================
; os8088 - vga12.inc
;
; VGA mode 12h (640x480x16, planar) primitives, region save/restore, and the
; global drawing mutex. Everything here programs the Graphics Controller
; (index 0x3CE / data 0x3CF) and Sequencer (0x3C4/0x3C5) directly.
;
; Conventions (SPEC.md 1, 5, 7):
;   - Solid fills use Set/Reset (GC0 = color, GC1 = 0Fh) with the Bit Mask
;     (GC8) for partial edge bytes; a read of the target byte loads the
;     latches before every masked write.
;   - XOR ops use ALU function = XOR (GC3 = 18h) with Set/Reset color 0Fh,
;     and EVERY target byte is read immediately before it is written -
;     including full interior bytes - because the ALU XORs against the latch
;     contents. rep stosb is only used for the interior of replace-mode
;     fills, where the latches are irrelevant.
;   - Every routine clips to 0..639 / 0..479 and leaves the GC/Sequencer in
;     the default state on return: Set/Reset enable = 0, Bit Mask = FF,
;     function = replace, rotate 0, Map Mask = 0Fh, Read Map = 0, mode 0.
;   - Drawing routines run with the gfx lock held (SPEC.md 1.6), so the
;     module-private scratch variables below need no interrupt guard: the
;     mouse ISR never draws while the lock is set.
; =============================================================================

VGA_GC      equ 0x3CE           ; Graphics Controller index port (data +1)
VGA_SEQ     equ 0x3C4           ; Sequencer index port (data +1)

gfx_color       db CBLACK       ; current drawing color for every solid op
gfx_lock_flag   db 0            ; drawing mutex (SPEC.md 7); 0 = free.
                                ; Initialized data, NOT .bss: nothing zeroes
                                ; .bss at boot, and this byte must start clear.

; -----------------------------------------------------------------------------
; vga_mode12 - enter mode 12h via the BIOS
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
vga_mode12:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, 0x0012
    int 0x10
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
; vga_text - back to 80x25 text via the BIOS (used by CMD_REBOOT)
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
vga_text:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, 0x0003
    int 0x10
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
; vga_set_color - internal: arm Set/Reset with [gfx_color] on all planes
; in:       -
; out:      GC0 = [gfx_color], GC1 = 0Fh
; clobbers: flags only (all registers preserved)
; -----------------------------------------------------------------------------
vga_set_color:
    push ax
    push dx
    mov dx, VGA_GC
    mov ah, [gfx_color]
    xor al, al                  ; GC0: Set/Reset value
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable Set/Reset on all planes
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_set_xor - internal: arm Set/Reset 0Fh with ALU function = XOR
; in:       -
; out:      GC0 = 0Fh, GC1 = 0Fh, GC3 = 18h
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_set_xor:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0F00              ; GC0: Set/Reset value = 0Fh
    out dx, ax
    mov ax, 0x0F01              ; GC1: enable on all planes
    out dx, ax
    mov ax, 0x1803              ; GC3: rotate 0, function = XOR
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_gc_reset - internal: restore the GC default state (SPEC.md 1.7)
; in:       -
; out:      GC1 = 0, GC3 = 0, GC8 = FF
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_gc_reset:
    push ax
    push dx
    mov dx, VGA_GC
    mov ax, 0x0001              ; GC1: Set/Reset enable off
    out dx, ax
    mov ax, 0x0003              ; GC3: rotate 0, function = replace
    out dx, ax
    mov ax, 0xFF08              ; GC8: Bit Mask = all bits
    out dx, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; vga_rect_setup - internal: clip a rect and precompute its geometry
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      CF = 1 if the clipped rect is empty (nothing else valid);
;           else CF = 0 and vga_y1/vga_rows/vga_off/vga_span/vga_lmsk/
;           vga_rmsk hold clipped top row, row count, VRAM offset of the
;           top-left byte, right byte - left byte, and the edge bit masks
;           (combined into vga_lmsk when the rect spans a single byte).
; clobbers: AX, BX, CX, DX, flags
; -----------------------------------------------------------------------------
vga_rect_setup:
    or bx, bx                   ; y1 < 0 -> 0
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, SCREEN_H            ; y2 > 479 -> 479
    jl .y2ok
    mov dx, SCREEN_H-1
.y2ok:
    cmp dx, bx
    jl .empty
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, SCREEN_W            ; x2 > 639 -> 639
    jl .x2ok
    mov cx, SCREEN_W-1
.x2ok:
    cmp cx, ax
    jl .empty
    mov [vga_y1], bx
    sub dx, bx                  ; rows = y2 - y1 + 1
    inc dx
    mov [vga_rows], dx
    mov dl, al                  ; left mask = FF >> (x1 & 7)
    and dl, 7
    push cx
    mov cl, dl
    mov dl, 0xFF
    shr dl, cl
    mov [vga_lmsk], dl
    pop cx
    mov dl, cl                  ; right mask = FF << (7 - (x2 & 7))
    and dl, 7
    xor dl, 7
    push cx
    mov cl, dl
    mov dl, 0xFF
    shl dl, cl
    mov [vga_rmsk], dl
    mov cl, 3
    shr ax, cl                  ; ax = left byte  (x1 / 8)
    mov [vga_lcol], ax          ; kept for the bb_* dirty union (SPEC.md 32)
    pop dx                      ; dx = x2 (pushed above)
    shr dx, cl                  ; dx = right byte (x2 / 8)
    sub dx, ax
    mov [vga_span], dx          ; span = right byte - left byte
    or dx, dx
    jnz .offs
    mov dl, [vga_rmsk]          ; one-byte rect: combine the masks
    and [vga_lmsk], dl
.offs:
    push ax                     ; left byte
    mov ax, ROW_BYTES
    mul bx                      ; ax = y1 * 80
    pop dx
    add ax, dx
    mov [vga_off], ax
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; gfx_pixel - plot one pixel in [gfx_color]
; in:       CX = x, DX = y
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_pixel:
    push ax
    push bx
    mov ax, cx                  ; a pixel is a 1x1 solid rect (CX/DX already
    mov bx, dx                  ; hold x2/y2); gfx_fill clips and back-
    call gfx_fill               ; buffer-dispatches itself
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_hline - horizontal line in [gfx_color], inclusive endpoints
; in:       AX = x1, BX = x2, DX = y  (x1 <= x2)
; out:      -
; clobbers: flags
;
; A 1-row solid rect: gfx_fill clips and back-buffer-dispatches itself.
; (The former dedicated VRAM loop was retired for the SPEC.md 32 code
; budget - gfx_fill produces identical pixels for one row.)
; -----------------------------------------------------------------------------
gfx_hline:
    push bx
    push cx
    mov cx, bx                  ; rect (x1,y)-(x2,y)
    mov bx, dx
    call gfx_fill
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; vga_vline_core - internal: vertical run of masked byte writes
;
; Shared by gfx_pixel / gfx_vline / gfx_xor_rect: the caller has already
; armed GC0/GC1 (and GC3 for XOR). Reads each byte before writing, which
; both loads the latches for the masked write and feeds the XOR ALU.
; in:       AX = x, BX = y1, DX = y2
; out:      GC8 left set to the column mask (caller resets)
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_vline_core:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp ax, SCREEN_W            ; catches x < 0 too (unsigned)
    jae .done
    or bx, bx
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, SCREEN_H
    jl .y2ok
    mov dx, SCREEN_H-1
.y2ok:
    cmp dx, bx
    jl .done
    mov si, dx                  ; si = row count
    sub si, bx
    inc si
    push ax
    mov ax, ROW_BYTES
    mul bx                      ; ax = y1 * 80
    mov di, ax
    pop ax
    mov cx, ax                  ; bh = 80h >> (x & 7)
    and cl, 7
    mov bh, 0x80
    shr bh, cl
    mov cl, 3
    shr ax, cl
    add di, ax                  ; di = VRAM offset of top byte
    mov dx, VGA_GC
    mov ah, bh
    mov al, 8
    out dx, ax                  ; GC8 = column mask
    mov ax, VGA_SEG
    mov es, ax
.row:
    mov al, [es:di]             ; load latches
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .row
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
; gfx_vline - vertical line in [gfx_color], inclusive endpoints
; in:       AX = x, BX = y1, DX = y2  (y1 <= y2)
; out:      -
; clobbers: flags
;
; A 1-column solid rect via gfx_fill (which clips and back-buffer-
; dispatches itself); vga_vline_core stays for gfx_xor_rect's verticals.
; -----------------------------------------------------------------------------
gfx_vline:
    push cx
    mov cx, ax                  ; rect (x,y1)-(x,y2)
    call gfx_fill
    pop cx
    ret

; -----------------------------------------------------------------------------
; gfx_fill - solid rectangle in [gfx_color], inclusive corners
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_fill:
    call bb_mono_chk            ; colour tracking runs in BOTH modes: double
                                ; buffering can be switched on later, and the
                                ; buffer it seeds from VRAM is only plane-
                                ; identical if nothing but 0/15 was ever
                                ; drawn (SPEC.md 32)
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_set_color
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column (whole rect if 1 byte wide)
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
.lcol:
    mov al, [es:di]             ; load latches
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov bx, [vga_span]
    or bx, bx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, bx
    mov si, [vga_rows]
.rcol:
    mov al, [es:di]
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec bx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov cx, bx
    push di
    rep stosb
    pop di
    add di, ROW_BYTES
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
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
; gfx_frame - 1px outline rectangle in [gfx_color]
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_frame:
    push bx
    push dx
    push si
    push di
    mov si, bx                  ; si = y1
    mov di, dx                  ; di = y2
    mov bx, cx                  ; hline wants BX = x2
    mov dx, si
    call gfx_hline              ; top
    mov dx, di
    call gfx_hline              ; bottom
    mov bx, si                  ; vline wants BX = y1, DX = y2
    mov dx, di
    call gfx_vline              ; left  (replace mode: corner overlap is fine)
    push ax
    mov ax, cx
    call gfx_vline              ; right
    pop ax
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_fill_gray - 50% dither: (x+y) even = white, odd = black (desktop)
;
; Ignores [gfx_color]. Bytes start on even x, so the pattern byte depends
; only on row parity: 0AAh on even rows, 55h on odd. With Set/Reset off and
; Map Mask 0Fh the CPU byte lands in all four planes, giving color 15/0
; per bit directly.
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_fill_gray:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill_gray
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    mov bl, 0xAA                ; pattern for the top clipped row
    test byte [vga_y1], 1
    jz .parity
    mov bl, 0x55
.parity:
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
    mov bh, bl
.lcol:
    mov al, [es:di]             ; load latches for the masked write
    mov [es:di], bh
    not bh                      ; AA <-> 55 each row
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov cx, [vga_span]
    or cx, cx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, cx
    mov si, [vga_rows]
    mov bh, bl
.rcol:
    mov al, [es:di]
    mov [es:di], bh
    not bh
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec cx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08              ; full bytes: latches irrelevant
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov al, bl
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, ROW_BYTES
    not bl
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
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
; vga_xor_hline - internal: horizontal XOR line
;
; Caller has armed the XOR state (vga_set_xor). Every byte - edges AND
; interior - is read immediately before the write so the ALU XORs against
; fresh latches. No rep stosb here (SPEC.md 5).
; in:       AX = x1, BX = x2, DX = y  (x1 <= x2)
; out:      GC8 left set (caller resets)
; clobbers: flags only
; -----------------------------------------------------------------------------
vga_xor_hline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp dx, SCREEN_H
    jae .done
    or ax, ax
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp bx, SCREEN_W
    jl .x2ok
    mov bx, SCREEN_W-1
.x2ok:
    cmp bx, ax
    jl .done
    push ax
    mov ax, ROW_BYTES
    mul dx                      ; ax = y * 80
    mov di, ax
    pop ax
    mov si, ax                  ; si = left byte
    mov cl, 3
    shr si, cl
    mov dx, bx                  ; dx = right byte
    shr dx, cl
    sub dx, si
    add di, si
    mov si, dx                  ; si = span
    mov cl, al                  ; ch = left mask
    and cl, 7
    mov ch, 0xFF
    shr ch, cl
    mov cl, bl                  ; bh = right mask
    and cl, 7
    xor cl, 7
    mov bh, 0xFF
    shl bh, cl
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    or si, si
    jnz .edge
    and ch, bh                  ; one byte: combine the masks
.edge:
    mov ah, ch
    mov al, 8
    out dx, ax
    mov al, [es:di]             ; read-before-write: loads the latches
    stosb
    or si, si
    jz .done
    dec si
    jz .right
    mov ax, 0xFF08              ; interior: full mask, still byte-by-byte
    out dx, ax
    mov cx, si
.interior:
    mov al, [es:di]
    stosb
    loop .interior
.right:
    mov ah, bh
    mov al, 8
    out dx, ax
    mov al, [es:di]
    stosb
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
; gfx_xor_rect - 1px outline rectangle, XOR 0Fh (drag outline)
;
; Self-inverting: drawing it twice restores the screen. Each edge pixel is
; touched exactly once - the verticals skip the corner rows, and degenerate
; rects (x1=x2 and/or y1=y2) collapse to a single line/pixel rather than
; XORing anything twice.
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_rect:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_rect
vga_xor_rect_vram:              ; VRAM body under its own name: transient
                                ; overlays (the drag outline) call it direct
                                ; so they never enter the back buffer
    push bx
    push dx
    push si
    push di
    call vga_set_xor
    mov si, bx                  ; si = y1
    mov di, dx                  ; di = y2
    mov bx, cx                  ; xor_hline wants BX = x2
    mov dx, si
    call vga_xor_hline          ; top
    cmp di, si
    je .reset                   ; y1 = y2: one line, done
    mov dx, di
    call vga_xor_hline          ; bottom
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .reset                   ; only two rows: no vertical part
    call vga_vline_core         ; left (AX = x1; reads before writes)
    cmp cx, ax
    je .reset                   ; x1 = x2: one column, done
    push ax
    mov ax, cx
    call vga_vline_core         ; right
    pop ax
.reset:
    call vga_gc_reset
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_xor_fill - filled rectangle, XOR 0Fh (menu highlight)
;
; Every byte read-before-write; no rep stosb anywhere (SPEC.md 5).
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_xor_fill:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_fill
vga_xor_fill_vram:              ; VRAM body under its own name (see
                                ; vga_xor_rect_vram) - the menu highlights
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_set_xor
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
.lcol:
    mov al, [es:di]             ; read-before-write
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .lcol
    mov bx, [vga_span]
    or bx, bx
    jz .reset
    mov ah, [vga_rmsk]          ; right edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    add di, bx
    mov si, [vga_rows]
.rcol:
    mov al, [es:di]
    mov [es:di], al
    add di, ROW_BYTES
    dec si
    jnz .rcol
    dec bx                      ; interior bytes per row
    jz .reset
    mov ax, 0xFF08
    out dx, ax
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
.irow:
    mov cx, bx
    push di
.ibyte:
    mov al, [es:di]             ; every interior byte too
    stosb
    loop .ibyte
    pop di
    add di, ROW_BYTES
    dec si
    jnz .irow
.reset:
    call vga_gc_reset
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
; gfx_save - copy a screen region to a buffer, plane by plane
;
; x1 is rounded down and x2 up to byte boundaries internally. Buffer layout:
; all plane-0 rows, then plane 1, plane 2, plane 3 (rows top to bottom,
; ((x2/8)-(x1/8)+1) bytes each). Uses Read Map Select (GC4) per plane.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:DI = buffer
; out:      DI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to bb_save under double buffering (SPEC.md 32). vga_save_vram
; is the VRAM body under its own name: the mouse cursor's save-under calls
; it directly - the cursor lives in VRAM, never in the back buffer.
; -----------------------------------------------------------------------------
gfx_save:
    cmp byte [bb_on], 0
    jne bb_save
vga_save_vram:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    call vga_rect_setup
    jc .done
    mov byte [vga_plane], 0
.plane:
    mov dx, VGA_GC
    mov ah, [vga_plane]
    mov al, 4                   ; GC4: Read Map Select
    out dx, ax
    mov si, [vga_off]
    mov dx, [vga_rows]
    mov bp, [vga_span]
    inc bp                      ; bp = bytes per row
    push ds
    mov ax, VGA_SEG
    mov ds, ax                  ; movsb source = VRAM
.row:
    mov cx, bp
    rep movsb                   ; buffer DI advances contiguously
    add si, ROW_BYTES
    sub si, bp
    dec dx
    jnz .row
    pop ds
    inc byte [vga_plane]
    cmp byte [vga_plane], 4
    jb .plane
    mov dx, VGA_GC
    mov ax, 0x0004              ; GC4 back to plane 0 (default)
    out dx, ax
.done:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_restore - write a gfx_save buffer back to the screen
;
; Same rounding and buffer layout as gfx_save. Writes through the Sequencer
; Map Mask one plane at a time with Set/Reset off and Bit Mask FF, so the
; CPU bytes land verbatim and the latches are irrelevant.
; in:       AX = x1, BX = y1, CX = x2, DX = y2, ES:SI = buffer
; out:      SI advanced past the data (unchanged if the clipped rect is
;           empty); ES preserved
; clobbers: flags
;
; Dispatches to bb_restore under double buffering (SPEC.md 32);
; vga_restore_vram is the VRAM body for the cursor path (see gfx_save).
; -----------------------------------------------------------------------------
gfx_restore:
    cmp byte [bb_on], 0
    jne bb_restore
vga_restore_vram:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    call vga_rect_setup
    jc .done
    mov byte [vga_plane], 1     ; plane mask bit, 1/2/4/8
.plane:
    mov dx, VGA_SEQ
    mov ah, [vga_plane]
    mov al, 2                   ; SEQ2: Map Mask = this plane only
    out dx, ax
    mov di, [vga_off]
    mov dx, [vga_rows]
    mov bp, [vga_span]
    inc bp                      ; bp = bytes per row
    push ds
    push es
    mov ax, es
    mov ds, ax                  ; movsb source = caller's buffer
    mov ax, VGA_SEG
    mov es, ax                  ; movsb dest = VRAM
.row:
    mov cx, bp
    rep movsb
    add di, ROW_BYTES
    sub di, bp
    dec dx
    jnz .row
    pop es
    pop ds
    mov al, [vga_plane]
    add al, al                  ; next plane bit
    mov [vga_plane], al
    cmp al, 0x10
    jb .plane
    mov dx, VGA_SEQ
    mov ax, 0x0F02              ; SEQ2: Map Mask back to all planes
    out dx, ax
.done:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_save_size - buffer size a gfx_save of this rect will need
;
; bytes = ((x2/8) - (x1/8) + 1) * rows * 4, after the same clipping as
; gfx_save. Uses no module scratch variables, so it is safe to call without
; holding the gfx lock.
; in:       AX = x1, BX = y1, CX = x2, DX = y2
; out:      AX = byte count (0 if the clipped rect is empty)
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_save_size:
    push bx
    push cx
    push dx
    or bx, bx
    jns .y1ok
    xor bx, bx
.y1ok:
    cmp dx, SCREEN_H
    jl .y2ok
    mov dx, SCREEN_H-1
.y2ok:
    cmp dx, bx
    jl .empty
    or ax, ax
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, SCREEN_W
    jl .x2ok
    mov cx, SCREEN_W-1
.x2ok:
    cmp cx, ax
    jl .empty
    sub dx, bx                  ; dx = rows
    inc dx
    push cx
    mov cl, 3
    shr ax, cl                  ; ax = left byte
    pop bx                      ; bx = x2
    shr bx, cl                  ; bx = right byte
    sub bx, ax
    inc bx                      ; bx = bytes per row
    mov ax, bx
    mul dx                      ; ax = bytes per plane
    add ax, ax                  ; * 4 planes
    add ax, ax
    jmp .out
.empty:
    xor ax, ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gfx_lock - acquire the drawing mutex, then hide the cursor (SPEC.md 7)
;
; Spins with task_yield while another task holds it, so background tasks
; keep running. Task-level only - never call from an ISR. Non-reentrant:
; re-acquiring from the holder deadlocks.
; in:       -
; out:      -
; clobbers: flags (IF = 1 on return)
; -----------------------------------------------------------------------------
gfx_lock:
.retry:
    cli
    cmp byte [gfx_lock_flag], 0
    je .take
    sti
    call task_yield
    jmp .retry
.take:
    mov byte [gfx_lock_flag], 1
    sti
    call cursor_hide
    ret

; -----------------------------------------------------------------------------
; gfx_unlock - flush, show the cursor, then release the mutex (SPEC.md 7)
;
; Order is binding: gfx_flush pushes the dirty back-buffer rect to VRAM
; while the cursor is still hidden (a no-op without double buffering);
; cursor_show then redraws at the latest mouse position - its save-under
; captures the freshly flushed pixels - while we still own the screen;
; only then may the ISR draw again.
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_unlock:
    call gfx_flush
    call cursor_show
    mov byte [gfx_lock_flag], 0
    ret

; -----------------------------------------------------------------------------
section .bss

vga_plane       resb 1          ; save/restore plane counter / mask bit
vga_lmsk        resb 1          ; rect: left edge bit mask
vga_rmsk        resb 1          ; rect: right edge bit mask
vga_y1          resw 1          ; rect: clipped top row (dither parity)
vga_rows        resw 1          ; rect: clipped row count
vga_span        resw 1          ; rect: right byte - left byte
vga_off         resw 1          ; rect: VRAM offset of top-left byte
vga_lcol        resw 1          ; rect: left byte column (bb dirty union)

section .text
