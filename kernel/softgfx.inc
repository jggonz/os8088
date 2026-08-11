; =============================================================================
; os8088 - softgfx.inc
;
; THE 1BPP DRIVER, and the reason the kernel can drive a Hercules or a CGA
; card at all (SPEC.md 39.3). Nothing in here touches a VGA port or a latch:
; it does the ALU's work with plain CPU instructions, so pointing it at the
; framebuffer ([vid_rseg]), telling it there is one plane ([vid_planes]) and
; routing its row advances through gfx_nextrow so they follow the banked
; layout IS the Hercules/CGA renderer. No second driver exists.
;
; [vid_mono] is what routes drawing through this module, and on a 1bpp
; adapter it is set before anything draws and never changes.
;
; It was written for a RAM back buffer - four claimed planes on a VGA, with a
; dirty rect and a flush - and that is where the plane machinery below comes
; from. SPEC.md 32 records why the buffer went: it could only ever be armed
; on a VGA with 150KB of free heap, so the machine this project is calibrated
; against could never reach it, while every adapter paid the dispatch, the
; colour tracking and the dirty union on every fill and every glyph. What is
; left is the renderer, which was always the load-bearing half.
;
; RAM has no latches or Set/Reset, so these routines do the VGA ALU's work in
; software, one plane at a time: solid ops or/and the edge masks per the
; [gfx_color] plane bit (interiors are rep stosb of FF/00), XOR ops xor the
; edge masks and `not` whole interior bytes. Plane p lives at segment
; [vid_rseg] + p*[vid_rpara] with the same rows as VRAM, so vga_rect_setup's
; masks and offsets serve both worlds; every solid/XOR primitive here funnels
; through it. Pixel/hline/vline are degenerate fills - a rect setup costs one
; mul, same as the dedicated VRAM loops.
;
; No public API slot has its body here: gfx_scroll was the last and moved to
; vga12.inc with the back buffer (SPEC.md 5.5), so every sw_* entry below is
; reached through the [vid_mono] dispatch at a public entry in another file.
;
; Concurrency: every routine here runs with the gfx lock held (SPEC.md 1.6) -
; vga12's rect scratch needs no interrupt guard. The cursor does not come
; through here at all: mouse.inc goes through vga_save_vram/vga_restore_vram
; and draws straight to the framebuffer (SPEC.md 7.1).
; =============================================================================


; -----------------------------------------------------------------------------
; sw_fill / sw_fill_gray / sw_xor_fill - software twins of gfx_fill,
; gfx_fill_gray and gfx_xor_fill (same register contracts: AX = x1,
; BX = y1, CX = x2, DX = y2; clobbers flags). One shared core, sw_rect,
; parameterized by [sw_mode]:
;
;   SWM_SOLID - per plane, the [gfx_color] bit picks a 00/FF byte; edges
;               become (dest & ~mask) | (value & mask), interiors stosb.
;   SWM_GRAY  - one AA/55 pattern for all planes (color 15/0 per pixel
;               bit), toggled each row via [sw_altm] = FF; same edge math.
;   SWM_XOR   - XOR 0Fh flips every plane identically: edges xor the
;               mask, interior bytes are complemented.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; gfx_dbuf_gone - the retired double-buffer cell (slot 0x01F0, 20.8 rule 4)
; out: CF=1
; -----------------------------------------------------------------------------
gfx_dbuf_gone:
    stc
    ret

SWM_SOLID equ 0
SWM_GRAY  equ 1
SWM_XOR   equ 2

; SOLID is the one that falls through, because it is the one the machine
; spends its day on: every window background, every glyph cell's erase, every
; frame. XOR pays the jump instead - selection bands and drag outlines.
sw_xor_fill:
    mov byte [sw_mode], SWM_XOR
    jmp sw_rect

sw_fill_gray:
    mov byte [sw_mode], SWM_GRAY
    jmp sw_rect

sw_fill:
    mov byte [sw_mode], SWM_SOLID
    ; fall through to sw_rect

sw_rect:
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
    mov byte [sw_altm], 0
    cmp byte [sw_mode], SWM_GRAY ; ONLY gray starts from the dither byte. Solid
    jne .planes                  ; overwrote it per plane below (sw_ink), and
    call sw_parity               ; its own dither branch stages it there; XOR
    mov byte [sw_altm], 0xFF     ; never reads [sw_pat] at all. Staging it for
.planes:                         ; all three cost every solid fill five
                                 ; instructions it threw away
    mov dx, [vid_rseg]          ; DX = plane segment - the framebuffer, which
    mov bl, [gfx_color]         ; the framebuffer itself on mono (SPEC.md 39.3)
    mov bh, [vid_planes]        ; BH = plane counter: 4 or 1
.plane:
    mov es, dx
    cmp byte [sw_mode], SWM_SOLID
    jne .go
    call sw_ink                 ; solid: this pass's 00/FF byte
.go:
    call sw_plane_op
    add dx, [vid_rpara]
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
; sw_parity - internal: [sw_pat] = the 50% dither byte for the rect's top
;             row, which the row loops then alternate through [sw_altm].
; in:       [vga_y1] valid
; clobbers: AL, flags
; -----------------------------------------------------------------------------
sw_parity:
    mov al, 0xAA
    test byte [vga_y1], 1
    jz .set
    mov al, 0x55
.set:
    mov [sw_pat], al
    ret

; -----------------------------------------------------------------------------
; sw_ink - internal: solid mode's byte pattern for the pass about to run
;
; VGA: the plane's own bit of [gfx_color], consumed LSB-first out of BL -
; exactly what the open-coded shr/sbb did before.
;
; Mono: gfx_ink's reduction of the colour. Black and white become 00/FF; the
; middle greys become the 50% dither, which costs nothing extra because
; sw_rect has ALREADY put the row-parity AA/55 byte in [sw_pat] - arming
; [sw_altm] is all it takes to make SWM_GRAY's machinery do the work
; (SPEC.md 39.4).
;
; in:       BL = the colour's remaining bits (VGA path only)
; out:      [sw_pat], possibly [sw_altm]; BL shifted on the VGA path
; clobbers: AL, SI, flags
; -----------------------------------------------------------------------------
sw_ink:
    cmp byte [vid_mono], 0
    jne .mono
    shr bl, 1
    sbb al, al
    mov [sw_pat], al
    ret
.mono:
    mov al, [gfx_color]         ; gfx_ink's table read, inlined through SI -
    and ax, 15                  ; it indexes through BX, and BX is the plane
    mov si, ax                  ; counter and the colour bits here. SI is dead
    mov al, [si + gfx_inktab]   ; between vga_rect_setup and sw_plane_op, which
    cmp al, 1                   ; loads it itself
    je .dither
    mov [sw_pat], al
    ret
.dither:
    call sw_parity              ; the AA/55 byte, and then alternate it
    mov byte [sw_altm], 0xFF
    ret

; -----------------------------------------------------------------------------
; sw_plane_op - internal: one plane of a sw_rect op - left/right masked
;               edge columns plus the whole-byte interior.
; in:       ES = plane segment; [sw_mode]/[sw_pat]/[sw_altm]; vga_* rect
;           scratch from vga_rect_setup
; clobbers: ax, cx, si, di, bp, flags (preserves BX, DX)
;
; It banks BX and DX ONCE per plane so that sw_col - which used to save five
; registers on each of its two calls - can have the run of the register file
; and save none, and so that this routine's own interior loop can hold
; gfx_nextrow's two parameters in registers (SPEC.md 39.3). A row step was a
; near call plus two CS-overridden memory reads; it is three register
; instructions now, and a fill walks its rows THREE times.
; -----------------------------------------------------------------------------
sw_plane_op:
    push bx
    push dx
    mov ah, [vga_lmsk]          ; left edge column (whole rect if 1 byte wide)
    mov di, [vga_off]
    call sw_col
    mov bp, [vga_span]
    or bp, bp
    jz .out
    mov ah, [vga_rmsk]          ; right edge column
    mov di, [vga_off]
    add di, bp
    call sw_col
    mov bp, [vga_span]          ; sw_col owns BP too (it parks the wrap bit)
    dec bp                      ; interior bytes per row
    jz .out
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
    mov ah, [sw_pat]
    mov bx, [cs:vid_rowadd]     ; gfx_nextrow's two parameters, hoisted out of
    sub bx, bp                  ; the row loop and inlined below - the step
                                ; carrying the interior walk's own advance, so
                                ; neither loop banks DI (SPEC.md 5.7.1)
    mov dx, [cs:vid_wrapbit]
    cmp byte [sw_mode], SWM_XOR
    je .xrow
.irow:
    mov al, ah                  ; AX = pattern:pattern, so stosw is safe for
    mov cx, bp                  ; both the solid 00/FF and the AA/55 dither
    shr cx, 1
    rep stosw                   ; string ops leave the shr's CF alone
    jnc .ieven
    stosb                       ; odd interior width: one tail byte
.ieven:
    add di, bx                  ; gfx_nextrow, inlined (see the note above)
    test di, dx
    jz .inw
    add di, [cs:vid_wrapfix]
.inw:
    xor ah, [sw_altm]           ; gray alternates, solid stays
    dec si
    jnz .irow
    jmp .out
.xrow:
    mov cx, bp
.xbyte:
    not byte [es:di]
    inc di
    loop .xbyte
    add di, bx
    test di, dx
    jz .xnw
    add di, [cs:vid_wrapfix]
.xnw:
    dec si
    jnz .xrow
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; sw_col - internal: one masked byte column of [vga_rows] rows at ES:DI.
;          XOR mode: dest ^= mask. Else: dest = (dest & ~mask) |
;          (pattern & mask), pattern xor-stepped by [sw_altm] per row.
; in:       AH = mask, DI = top byte, ES = plane; [sw_mode]/[sw_pat]/[sw_altm]
; clobbers: AL, BX, CX, DX, SI, DI, BP, flags (AH and ES survive)
;
; It preserves NOTHING, deliberately: sw_plane_op banks BX/DX for the whole
; plane and reloads AH, DI, SI and BP itself around every call, so the five
; push/pop pairs this used to open with were saving registers their only
; caller was about to overwrite - a fifth of the cost of drawing one pixel
; (PERFORMANCE.md Part 2). SI and BP then pay for themselves by holding
; gfx_nextrow's step and wrap bit across the row loop.
; -----------------------------------------------------------------------------
sw_col:
    mov bl, ah
    not bl                      ; BL = ~mask
    mov ch, [sw_pat]            ; CH = running pattern
    mov cl, [sw_altm]
    mov dx, [vga_rows]
    mov si, [cs:vid_rowadd]     ; gfx_nextrow, hoisted and inlined - its body
    mov bp, [cs:vid_wrapbit]    ; is three instructions and the call and ret
    cmp byte [sw_mode], SWM_XOR ; around them cost as much again
    je .xrow
.row:
    mov al, ch
    and al, ah                  ; AL = pattern & mask
    and [es:di], bl
    or  [es:di], al
    xor ch, cl
    add di, si
    test di, bp
    jz .nw
    add di, [cs:vid_wrapfix]
.nw:
    dec dx
    jnz .row
    ret
.xrow:
    xor [es:di], ah
    add di, si
    test di, bp
    jz .xnw
    add di, [cs:vid_wrapfix]
.xnw:
    dec dx
    jnz .xrow
    ret

; -----------------------------------------------------------------------------
; sw_fill_pat - software twin of gfx_fill_pat (SPEC.md 5/32): the 8-row
;               pattern at [gfx_pat], staged into vga_patbuf, written
;               identically to every plane (colors 0/15 per pixel bit).
;               sw_col's fixed-byte/xor-toggle model cannot index 8 rows,
;               so this brings its own column helper.
; in:       AX = x1, BX = y1, CX = x2, DX = y2; [gfx_pat] = pattern ptr
; out:      -
; clobbers: flags
; -----------------------------------------------------------------------------
sw_fill_pat:
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
    call vga_pat_stage          ; vga_patbuf <- the 8 bytes at [gfx_pat]
    mov dx, [vid_rseg]          ; DX = plane segment (see sw_rect)
    mov bp, [vid_planes_w]      ; BP = plane counter
.plane:
    mov es, dx
    mov di, [vga_off]
    mov cl, [vga_lmsk]          ; left edge column (whole rect if 1 byte)
    call sw_patcol
    mov si, [vga_span]
    or si, si
    jz .next
    mov di, [vga_off]
    add di, si
    mov cl, [vga_rmsk]          ; right edge column
    call sw_patcol
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
    mov cx, si                  ; tail via the shr carry, like sw_plane_op
    shr cx, 1
    push di
    rep stosw
    jnc .ieven
    stosb
.ieven:
    pop di
    call gfx_nextrow
    inc bx
    and bx, 7
    dec bp
    jnz .irow
    pop bp
.next:
    add dx, [vid_rpara]
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
; sw_patcol - internal: one masked byte column of [vga_rows] rows at ES:DI,
;             row byte from vga_patbuf by (y & 7):
;             dest = dest ^ ((dest ^ pat) & mask)
; in:       CL = mask, DI = top byte, ES = plane; vga_* rect scratch
; clobbers: AX, flags (preserves BX, SI, DI and CX)
; -----------------------------------------------------------------------------
sw_patcol:
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
    call gfx_nextrow
    dec si
    jnz .row
    pop si
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; sw_xor_rect - software twin of gfx_xor_rect: 1px outline, XOR 0Fh.
;               Same edge decomposition as the VRAM version - each pixel
;               XORed exactly once, degenerate rects collapse - built from
;               four sw_xor_fill strips.
; in:       AX = x1, BX = y1, CX = x2, DX = y2  (x1<=x2, y1<=y2)
; clobbers: flags
; -----------------------------------------------------------------------------
sw_xor_rect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = y1
    mov di, dx                  ; DI = y2
    mov dx, si
    call sw_xor_fill            ; top row (x1,y1)-(x2,y1)
    cmp di, si
    je .out                     ; y1 = y2: one line, done
    mov bx, di
    mov dx, di
    call sw_xor_fill            ; bottom row (x1,y2)-(x2,y2)
    mov bx, si                  ; verticals span y1+1 .. y2-1
    inc bx
    mov dx, di
    dec dx
    cmp dx, bx
    jl .out                     ; only two rows: no vertical part
    push cx
    mov cx, ax
    call sw_xor_fill            ; left column (x1,y1+1)-(x1,y2-1)
    pop cx
    cmp cx, ax
    je .out                     ; x1 = x2: one column, done
    push ax
    mov ax, cx
    call sw_xor_fill            ; right column
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
; sw_save / sw_restore - software twins of gfx_save / gfx_restore, same
; rounding and buffer layout (SPEC.md 5) - a buffer saved from the back
; buffer restores to it byte-identically. One shared body, sw_xfer, with
; [sw_dir] picking the direction; restore additionally dirties the rect.
;   sw_save:    in AX/BX/CX/DX = rect, ES:DI = buffer; DI advances
;   sw_restore: in AX/BX/CX/DX = rect, ES:SI = buffer; SI advances
; ES preserved; clobbers flags.
; -----------------------------------------------------------------------------
sw_save:
    mov byte [sw_dir], 0
    jmp sw_xfer

sw_restore:
    mov byte [sw_dir], 1
    ; fall through to sw_xfer

sw_xfer:
    push ax
    push bx
    push cx
    push dx
    push bp
    call vga_rect_setup
    jc .done
    cmp byte [sw_dir], 0
    je .geom
    add si, [gfx_sub_st]        ; the SUB-RECT restore (SPEC.md 5.8), 0 at rest.
                                ; OUT HERE, once - .rst below runs per plane,
                                ; and DS is still KERNEL_SEG until it does
.geom:
    mov bp, [vga_span]
    inc bp                      ; BP = bytes per row
    mov bx, [vid_rseg]
.plane:
    mov ax, [vga_off]           ; this plane's walking offset
    mov dx, [vga_rows]
    push ds
    push es
    cmp byte [sw_dir], 0
    jne .rst
    push si                     ; save: plane -> buffer; caller SI untouched
    mov si, ax
    mov ds, bx                  ; movsb source = this plane
.srow:
    mov cx, bp
    rep movsb                   ; buffer DI advances contiguously
    sub si, bp
    add si, [cs:vid_rowadd]     ; gfx_nextrow INLINED, on SI: the framebuffer
    test si, [cs:vid_wrapbit]   ; pointer is SI here, so through the call it
    jz .snw                     ; cost two xchg's as well as the call and the
    add si, [cs:vid_wrapfix]    ; ret - on a loop whose body is a rep movsb of
.snw:                           ; two or three bytes (PERFORMANCE.md Part 9
    dec dx                      ; Set 3: inline it in every row loop)
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
    add si, [cs:gfx_sub_rs]     ; CS: IS MANDATORY - DS is the BUFFER in here,
                                ; which is why the row walk below reads
                                ; [cs:vid_rowadd] too
    sub di, bp
    add di, [cs:vid_rowadd]     ; ...and here on DI, which is what gfx_nextrow
    test di, [cs:vid_wrapbit]   ; takes, so this one saves only the call and
    jz .rnw                     ; the ret
    add di, [cs:vid_wrapfix]
.rnw:
    dec dx
    jnz .rrow
    pop di
    add si, [cs:gfx_sub_ps]     ; ...past the rows this rect does not cover. DS
                                ; is the buffer until .pnext's `pop ds`, and
                                ; the SAVE path jumps straight to .pnext, so
                                ; this is the restore's alone
.pnext:
    pop es
    pop ds
    add bx, [vid_rpara]         ; nonzero even at one plane, or this loop
    cmp bx, [vid_rend]          ; would never terminate
    jb .plane
.done:
    cmp byte [sw_dir], 0        ; a sub-rect is ONE restore (SPEC.md 5.8), and
    je .nosub                   ; clearing it HERE rather than at the call site
    call gfx_sub_off            ; is what makes a forgetful caller inherit
.nosub:                         ; "whole", the safe value. Restore only, or a
                                ; gfx_save between the arm and the restore
                                ; would disarm it - and wm_su_edge is exactly
                                ; that, twice
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

sw_dir          resb 1          ; xfer: 0 = save, 1 = restore
sw_mode         resb 1          ; rect core: SWM_SOLID / SWM_GRAY / SWM_XOR
sw_pat          resb 1          ; rect core: plane value / dither pattern
sw_altm         resb 1          ; rect core: per-row pattern toggle (0 or FF)

section .text
