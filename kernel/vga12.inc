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
gfx_dis         db 0            ; 1 = what is being drawn is a DISABLED
                                ; control (SPEC.md 47). It is a FLAG and not
                                ; a colour because the renderer has to do
                                ; something a colour cannot express: on a
                                ; mono adapter font_ink masks the glyph to a
                                ; checkerboard, which is the only way a
                                ; disabled STRING is distinguishable from a
                                ; live one at 1bpp. Keying that off CDGRAY
                                ; instead would catch every other use of the
                                ; colour - Minesweeper's digit 8 came out
                                ; looking like a menu item you cannot pick -
                                ; and would make "never use this colour for
                                ; anything else" a rule the tree has to keep
                                ; by hand. Set it through gfx_pen_* and it
                                ; cannot disagree with gfx_color.
                                ; Initialized data, NOT .bss (nothing zeroes
                                ; .bss), and gfx_unlock clears it for the
                                ; reason it clears the clip region: it is
                                ; valid for exactly one lock hold.
gfx_pat         dw 0            ; gfx_fill_pat: near ptr to 8 row bytes,
                                ; set by the caller before every call
gfx_lock_flag   db 0            ; drawing mutex (SPEC.md 7); 0 = free.
                                ; Initialized data, NOT .bss: nothing zeroes
                                ; .bss at boot, and this byte must start clear.

; -----------------------------------------------------------------------------
; GFXCLIP - the clip hook, at a public rect entry (SPEC.md 11.3)
;
; Placed at the PUBLIC entry, above the [bb_on] dispatch, so one hook covers
; the VRAM path, the back-buffer path, VGA and both mono adapters at once -
; on a 1bpp adapter the software renderer IS the direct path (SPEC.md 39.5),
; and a hook below the dispatch would work on VGA and silently do nothing on
; Hercules. Same reasoning that places bb_mono_chk where it is.
;
; Disarmed (the whole repaint path) it costs one compare and a taken branch;
; nothing else changes. Armed, gfx_clip_run re-enters the raw body once per
; visible fragment.
; -----------------------------------------------------------------------------
%macro GFXCLIP 1
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    je %1
    push di
    mov di, %1
    call gfx_clip_run
    pop di
    ret
%endmacro

; -----------------------------------------------------------------------------
; gfx_clip_run - internal: run a rect primitive once per visible fragment
; in:       AX = x1, BX = y1, CX = x2, DX = y2, DI = near ptr to the raw body
;           (the entry just below that primitive's GFXCLIP); clip is armed
; out:      -
; clobbers: flags
;
; The loop's state lives in module scratch rather than registers because the
; raw bodies preserve everything and there is nothing left to spare anyway.
; That is safe for the same reason vga12's rect scratch is: this only ever
; runs with the gfx lock held, and no clipped primitive calls another.
; -----------------------------------------------------------------------------
gfx_clip_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [gfx_cl_x1], ax
    mov [gfx_cl_y1], bx
    mov [gfx_cl_x2], cx
    mov [gfx_cl_y2], dx
    mov [gfx_cl_fn], di
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    mov ax, [si+WCR_X1]         ; AX/CX = the fragment's x span, clipped to
    cmp ax, [gfx_cl_x1]         ; the primitive's
    jge .x1
    mov ax, [gfx_cl_x1]
.x1:
    mov cx, [si+WCR_X2]
    cmp cx, [gfx_cl_x2]
    jle .x2
    mov cx, [gfx_cl_x2]
.x2:
    cmp ax, cx
    jg .next
    mov bx, [si+WCR_Y1]
    cmp bx, [gfx_cl_y1]
    jge .y1
    mov bx, [gfx_cl_y1]
.y1:
    mov dx, [si+WCR_Y2]
    cmp dx, [gfx_cl_y2]
    jle .y2
    mov dx, [gfx_cl_y2]
.y2:
    cmp bx, dx
    jg .next
    call word [gfx_cl_fn]
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
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
    cmp dx, [vid_h]             ; y2 past the last row -> the last row
    jl .y2ok
    mov dx, [vid_hm1]
.y2ok:
    cmp dx, bx
    jl .empty
    or ax, ax                   ; x1 < 0 -> 0
    jns .x1ok
    xor ax, ax
.x1ok:
    cmp cx, [vid_w]             ; x2 past the last column -> the last column
    jl .x2ok
    mov cx, [vid_wm1]
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
    mov ax, bx                  ; this one call re-bases every rect primitive
    call gfx_rowbase            ; in BOTH renderers - on VGA it is still just
    pop dx                      ; y1 * 80 (SPEC.md 39.3)
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
; gfx_blit4 - draw a block of packed 4bpp pixels (SPEC.md 5.4/42)
;
; in:       ES:SI = source, packed 4 bits per pixel, 2 pixels per byte, the
;                   HIGH nibble leftmost - the layout apps/paint's canvas
;                   already uses, and the one a 16-colour BMP row uses
;           BP    = source stride in bytes (one row to the next)
;           AX    = destination x, BX = destination y (screen coords)
;           CX    = width in pixels (> 0), DX = height in rows (> 0)
; out:      nothing; every register preserved. [gfx_color] is left holding
;           the last colour drawn.
; clobbers: nothing else (flags)
;
; What this is, and what it deliberately is not. It coalesces runs of equal
; pixels and emits one gfx_fill per run - the same thing apps/paint's pt_blit
; did in the package, and therefore the same pixels - but from INSIDE the
; kernel. That is the whole win and it is a large one: since packages own a
; segment (SPEC.md 20.1) every gfx call from one is a FAR call, so a canvas
; repaint that cost N far calls plus N run scans now costs ONE call and a
; near loop. It also means the primitive works on all three adapters, in
; front of and behind the back buffer, and inside a clip region, without one
; line of adapter-specific code - because gfx_fill already does.
;
; **The run scan compares BYTES, not pixels, and that is the whole speed of
; it.** A run of colour c is a run of bytes equal to c|c<<4, so `repe scasb`
; walks it two pixels at a time at about 15 clocks a byte - roughly seven and
; a half clocks a pixel. The odd ends are handled by hand: the first pixel
; when the run starts on a low nibble, the last when it ends on a high one.
;
; The first version of this routine decoded every pixel individually instead
; - read the byte, test the parity, `shr al, cl` by four, compare, branch -
; and that is 75 to 90 clocks a pixel on an 8086, because a 4-bit shift by CL
; alone is 24 of them. It kept the SHAPE of the optimisation (one call per
; run) and threw away the optimisation, so a 448x280 repaint went from about
; a quarter of a second on a 4.77MHz 8088 to over two, and the far calls it
; saved were noise against that. QEMU cannot show this - it does not model
; 8086 timing at all - which is exactly why the cycle counts are written down
; here rather than measured.
;
; A plane-parallel VGA fast path (build a byte-per-8-pixels mask per plane
; and write it through the Map Mask) would beat this on detailed pictures and
; lose to it on flat ones, and it would need its own back-buffer and 1bpp
; twins. It is a worthwhile future optimisation and it changes no caller;
; docs/PAINT-NOTES.md records why it was not the first one.
;
; Colour reduction on a 1bpp adapter happens in gfx_fill, per run (SPEC.md
; 39.4), so a 16-colour picture reads as black/dither/white there exactly as
; the rest of the UI does.
; -----------------------------------------------------------------------------
gfx_blit4:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or cx, cx
    jz .out
    or dx, dx
    jz .out
    mov [gfx_blit_w], cx        ; lock-held scratch, like vga12's rect block:
    mov [gfx_blit_x], ax        ; every drawing entry runs under the gfx lock
    mov [gfx_blit_h], dx        ; (SPEC.md 1 rule 6), so one copy serves
    mov [gfx_blit_sv], si
.row:
    mov si, [gfx_blit_sv]       ; ES:SI = this row's first byte
    xor dx, dx                  ; DX = the pixel the next run starts at
.run:
    ; --- the run's colour, and the byte a matching PAIR would hold ---------
    mov di, dx
    shr di, 1
    add di, si
    mov al, [es:di]
    test dl, 1
    jnz .lo                     ; an even pixel is the HIGH nibble
    mov cl, 4
    shr al, cl
.lo:
    and al, 0Fh
    mov [gfx_blit_c], al
    mov ah, al
    mov cl, 4
    shl ah, cl
    or ah, al
    mov [gfx_blit_pb], ah

    ; --- walk it two pixels at a time -------------------------------------
    mov cx, dx
    test cl, 1
    jz .aligned
    inc cx                      ; an odd start: the pair scan begins at the
.aligned:                       ; next pixel, and this one is already in
    mov [gfx_blit_e], cx
    mov ax, [gfx_blit_w]
    sub ax, cx
    jbe .tail                   ; nothing left to scan in pairs
    shr ax, 1                   ; whole pairs remaining in the row
    jz .tail
    mov di, cx
    shr di, 1
    add di, si                  ; ES:DI = the first pair byte
    mov [gfx_blit_s0], di       ; SI belongs to the row, so the scan's origin
    mov cx, ax                  ; is parked in memory
    mov al, [gfx_blit_pb]
    cld
    repe scasb
    mov ax, di                  ; `mov` leaves scasb's ZF alone, and the
    jne .mism                   ; branch has to read it before anything else
    sub ax, [gfx_blit_s0]       ; does: every byte matched
    jmp short .have
.mism:
    dec ax                      ; the mismatch sits at DI-1, so it is not
    sub ax, [gfx_blit_s0]       ; part of the run
.have:
    add ax, ax                  ; bytes matched -> pixels
    add [gfx_blit_e], ax
.tail:
    mov cx, [gfx_blit_e]        ; the odd final nibble, by hand
    cmp cx, [gfx_blit_w]
    jae .emit
    mov di, cx
    shr di, 1
    add di, si
    mov al, [es:di]
    test cl, 1
    jnz .tlo
    mov cl, 4
    shr al, cl
.tlo:
    and al, 0Fh
    cmp al, [gfx_blit_c]
    jne .emit
    inc word [gfx_blit_e]       ; it matches too
.emit:
    mov di, [gfx_blit_x]
    add di, dx                  ; DI = the run's first x, on screen
    mov ah, [gfx_blit_c]
    mov dx, [gfx_blit_e]        ; DX = one past its last pixel, row-relative
    call gfx_blit_run           ; (which is where the next run starts)
    cmp dx, [gfx_blit_w]
    jb .run

    mov ax, [gfx_blit_sv]       ; next source row
    add ax, bp
    mov [gfx_blit_sv], ax
    inc bx                      ; next destination row
    dec word [gfx_blit_h]
    jnz .row
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gfx_blit_run - emit one coalesced run (module-internal to gfx_blit4)
; in:  DI = run start x, DX = one past the run's last pixel (row-relative),
;      AH = colour, BX = y, [gfx_blit_x] = the block's left edge
; out: nothing (AX/BX/CX/DX/SI/DI/BP preserved)
; -----------------------------------------------------------------------------
gfx_blit_run:
    push ax
    push bx
    push cx
    push dx
    mov al, ah
    mov [gfx_color], al
    mov ax, di                  ; x1 = the run's start
    mov cx, [gfx_blit_x]
    add cx, dx
    dec cx                      ; x2 = the last pixel drawn
    cmp ax, cx
    ja .out                     ; empty run (cannot happen; cheap to guard)
    mov dx, bx                  ; gfx_hline wants y in DX
    mov bx, cx
    call gfx_hline              ; clips, dispatches and reduces for us
.out:
    pop dx
    pop cx
    pop bx
    pop ax
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
    GFXCLIP gfx_fill_raw
gfx_fill_raw:
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
    GFXCLIP gfx_fill_gray_raw
gfx_fill_gray_raw:
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
; gfx_fill_pat - 8x8 dither pattern fill, screen-aligned (SPEC.md 5)
;
; Row y takes the byte [gfx_pat][y & 7]; bit set = white (15), clear =
; black (0), bit 7 the leftmost pixel of each screen byte. Ignores
; [gfx_color] and, like gfx_fill_gray, writes only colors 0/15, so it
; never retires [bb_mono] (SPEC.md 32). Same masked-edge scheme as
; gfx_fill_gray, with the row byte fetched from the table instead of the
; AA/55 toggle. The 8 pattern bytes are staged into vga_patbuf first:
; [bp+bx] is not an 8086 addressing mode, but [vga_patbuf+bx] is (and the
; bb twin shares the staging).
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
;
; Clipped like the other rect fills, and it re-enters per fragment for free
; because the pattern is SCREEN-aligned: the row byte is [vga_y1] & 7 off a
; table staged from [gfx_pat] on every call, so a fragment starting at any y
; picks the same byte the whole rect would have. It was the one rect
; primitive without the hook, and the Task Manager's memory map is drawn
; almost entirely out of it - the claim bands, the buffer texture and each
; package's region pattern - so a map redrawn behind another window painted
; its full width straight across whatever was on top. The kernel's own band
; stopped at the right place, because gfx_fill_gray had the hook, which is
; what made it look like the map was drawing the wrong SHAPE rather than in
; the wrong PLACE.
; -----------------------------------------------------------------------------
gfx_fill_pat:
    GFXCLIP gfx_fill_pat_raw
gfx_fill_pat_raw:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_fill_pat
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call vga_rect_setup
    jc .done
    call vga_pat_stage          ; vga_patbuf <- the 8 bytes at [gfx_pat]
    mov ax, VGA_SEG
    mov es, ax
    mov dx, VGA_GC
    mov ah, [vga_lmsk]          ; left edge column
    mov al, 8
    out dx, ax
    mov di, [vga_off]
    mov si, [vga_rows]
    mov bx, [vga_y1]
    and bx, 7
.lcol:
    mov ah, [vga_patbuf+bx]     ; row byte first: the masked write must
    mov al, [es:di]             ; follow the latch load immediately
    mov [es:di], ah
    inc bx
    and bx, 7
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
    mov bx, [vga_y1]
    and bx, 7
.rcol:
    mov ah, [vga_patbuf+bx]
    mov al, [es:di]
    mov [es:di], ah
    inc bx
    and bx, 7
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
    mov bx, [vga_y1]
    and bx, 7
.irow:
    mov al, [vga_patbuf+bx]
    push cx
    push di
    rep stosb
    pop di
    pop cx
    add di, ROW_BYTES
    inc bx
    and bx, 7
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
; vga_pat_stage - internal: copy the 8 bytes at [gfx_pat] into vga_patbuf
;                 so the fill loops can index them ([bx+disp] addressing)
; in:       [gfx_pat]
; out:      vga_patbuf filled
; clobbers: SI, DI, ES, flags
; -----------------------------------------------------------------------------
vga_pat_stage:
    push ds                     ; DF=0 per SPEC.md 1
    pop es
    mov si, [gfx_pat]
    mov di, vga_patbuf
    movsw
    movsw
    movsw
    movsw
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
    cmp word [wm_clip_n], 0     ; clipped background draw? (SPEC.md 11.3)
    jne gfx_xor_rect_clip       ; an OUTLINE is not the intersection of its
                                ; bounding rect with anything - clipping it
                                ; that way would draw edges through the
                                ; middle of it, so it decomposes first
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_rect
vga_xor_rect_vram:              ; VRAM body under its own name: transient
                                ; overlays (the drag outline) call it direct
                                ; so they never enter the back buffer
    cmp byte [vid_mono], 0      ; ...but "direct" means the framebuffer, and
    jne bb_xor_rect             ; on a 1bpp adapter the software renderer IS
                                ; the direct path (SPEC.md 39.5)
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
; gfx_xor_rect_clip - internal: the outline as four filled strips
; in:       AX = x1, BX = y1, CX = x2, DX = y2; the clip is armed
; out:      -
; clobbers: flags
;
; The same decomposition bb_xor_rect uses, with the same degenerate cases -
; each pixel is touched exactly once, so the result is still self-inverting.
; Every strip goes through the PUBLIC gfx_xor_fill, which is where it meets
; the clip region (SPEC.md 11.3).
; -----------------------------------------------------------------------------
gfx_xor_rect_clip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = y1, DI = y2
    mov di, dx
    mov dx, si
    call gfx_xor_fill           ; top row
    cmp di, si
    je .out                     ; y1 = y2: one line, done
    mov bx, di
    mov dx, di
    call gfx_xor_fill           ; bottom row
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .out                     ; only two rows: no vertical part
    push cx
    mov cx, ax
    call gfx_xor_fill           ; left column
    pop cx
    cmp cx, ax
    je .out                     ; x1 = x2: one column, done
    mov ax, cx
    call gfx_xor_fill           ; right column
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    GFXCLIP gfx_xor_fill_raw
gfx_xor_fill_raw:
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    jne bb_xor_fill
vga_xor_fill_vram:              ; VRAM body under its own name (see
                                ; vga_xor_rect_vram) - the menu highlights
    cmp byte [vid_mono], 0
    jne bb_xor_fill
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
    cmp byte [vid_mono], 0      ; the cursor's save-under, from inside IRQ4:
    jne bb_save                 ; on mono the framebuffer IS the renderer's
                                ; target, so one "plane" of bb_xfer is it
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
    cmp byte [vid_mono], 0      ; see vga_save_vram
    jne bb_restore
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
; gfx_pen_live / gfx_pen_dis / gfx_pen_cf - the enabled and disabled pens
;
; in:       gfx_pen_cf only: CF = 0 live, CF = 1 disabled - which is the shape
;           EVERY greying predicate in the tree already answers in, so a call
;           site is `call <ok-test>` then `call gfx_pen_cf` and nothing else
; out:      nothing
; clobbers: nothing (flags)
;
; SPEC.md 47 makes a disabled control two things at once - the CDGRAY pen and
; the [gfx_dis] flag - and these exist so no call site can set one without the
; other. That is not hypothetical tidiness: the bug this whole section came
; out of was a row whose LABEL was greyed and whose GLYPH was not, and "two
; halves of one control disagreeing" is the failure mode the flag would
; otherwise invite a second time.
;
; gfx_pen_live returns the pen to CBLACK, which is what every site in the tree
; wants back. A site that needs some other colour afterwards sets it itself -
; but it must still come through here, or the flag stays armed and the next
; string on this lock hold draws dithered.
; -----------------------------------------------------------------------------
gfx_pen_cf:
    jc gfx_pen_dis
gfx_pen_live:
    mov byte [gfx_color], CBLACK
    mov byte [gfx_dis], 0
    ret
gfx_pen_dis:
    mov byte [gfx_color], CDGRAY
    mov byte [gfx_dis], 1
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
;
; The clip region dies here too (SPEC.md 11.3 rule 1). Making it die with
; the lock hold is what stops a leaked region from silently truncating the
; next painter - and it is what keeps the drag outline and the menu
; highlights unclipped without any of them knowing the region exists.
; in:       -
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
gfx_unlock:
    mov word [wm_clip_n], 0
    mov byte [gfx_dis], 0       ; the disabled pen dies with the lock, for the
                                ; clip region's reason (SPEC.md 11.3/47): a
                                ; site that forgets gfx_pen_live cannot leak
                                ; dithered text into whatever draws next
    call gfx_flush
    call cursor_show
    mov byte [gfx_lock_flag], 0
    ret

; -----------------------------------------------------------------------------
section .bss

gfx_blit_x:  resw 1             ; gfx_blit4's row cursor (SPEC.md 5.4):
gfx_blit_w:  resw 1             ; destination left, width, rows left and the
gfx_blit_h:  resw 1             ; current source row's base. Lock-held
gfx_blit_sv: resw 1             ; scratch, like the rect block below.
gfx_blit_e:  resw 1             ; ...and the run scanner's state: one past the
gfx_blit_s0: resw 1             ; run's last pixel, where `repe scasb` began,
gfx_blit_c:  resb 1             ; the run's colour, and the byte a matching
gfx_blit_pb: resb 1             ; PAIR of that colour holds (c | c<<4)

vga_plane       resb 1          ; save/restore plane counter / mask bit
vga_lmsk        resb 1          ; rect: left edge bit mask
vga_rmsk        resb 1          ; rect: right edge bit mask
vga_y1          resw 1          ; rect: clipped top row (dither parity)
vga_rows        resw 1          ; rect: clipped row count
vga_span        resw 1          ; rect: right byte - left byte
vga_off         resw 1          ; rect: VRAM offset of top-left byte
vga_lcol        resw 1          ; rect: left byte column (bb dirty union)
vga_patbuf      resb 8          ; gfx_fill_pat: staged pattern rows
gfx_cl_x1       resw 1          ; gfx_clip_run: the primitive's own rect,
gfx_cl_y1       resw 1          ; held while the raw body runs per fragment
gfx_cl_x2       resw 1
gfx_cl_y2       resw 1
gfx_cl_fn       resw 1          ; ...and the raw body's near pointer

section .text
