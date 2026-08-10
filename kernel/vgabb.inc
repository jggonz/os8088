; =============================================================================
; os8088 - vgabb.inc
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
; Concurrency: every routine here runs with the gfx lock held (SPEC.md 1.6) -
; vga12's rect scratch needs no interrupt guard. The cursor does not come
; through here at all: mouse.inc goes through vga_save_vram/vga_restore_vram
; and draws straight to the framebuffer (SPEC.md 7.1).
; =============================================================================


; -----------------------------------------------------------------------------
; gfx_scroll - vertical scroll blit (SPEC.md 5.5; API slot 0x01E0)
;
; in:       AX = x1, BX = y1, CX = x2, DX = y2 (inclusive, absolute screen
;           coords), SI = dy SIGNED: positive moves the rect's content UP dy
;           rows, negative down. Caller holds the gfx lock - which is what
;           makes the blit cursor-safe: the cursor is hidden under it.
; out:      CF=0 done; the |dy| vacated rows keep UNSPECIFIED content and
;           the caller repaints them. CF=1 refused, nothing moved: x1/(x2+1)
;           not multiples of 8, empty/inverted rect, |dy| = 0 or >= height,
;           rect off the live screen, or an armed clip region that does not
;           contain the whole rect (whole-shape clipping, the font_char/
;           ico_core class - a blit cannot be cut per pixel; the caller
;           falls back to a full repaint, which clips per strip).
; clobbers: nothing (registers preserved; CF is the answer)
;
; Two backends, one contract (SPEC.md 39):
;   VGA direct   - GC write mode 1: each movsb moves 8 pixels across all
;                  four planes through the latches; mode restored after
;                  (SPEC.md 1 rule 7).
;   mono         - per-row rep movsb through gfx_rowbase, immune to the
;                  banked layouts by construction.
; -----------------------------------------------------------------------------
gfx_scroll:
    call cur_unlazy             ; off the clip list (SPEC.md 11.3): 7.1.4
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    GFXDENTER                   ; a blit cannot take a sub-rect (SPEC.md 11.3),
    GFXDORG cx, dx              ; so it goes onto the display holding its top
                                ; left - and the two edge tests below then
                                ; REFUSE a rect that reaches past that display,
                                ; which is 39.14.2's straddle rule for free
    test ax, 7                  ; byte-column granularity on every adapter
    jnz .no
    mov di, cx
    inc di
    test di, 7
    jnz .no
    cmp ax, cx
    ja .no
    cmp bx, dx
    ja .no
    cmp cx, [vid_cw]
    jae .no                     ; also catches a "negative" x1 via x1 <= x2
    cmp dx, [vid_ch]
    jae .no
    or si, si
    jz .no
    mov di, si                  ; DI = |dy|
    jns .pos
    neg di
.pos:
    push dx
    sub dx, bx
    inc dx                      ; DX = height
    cmp di, dx
    pop dx
    jae .no
    call wm_clip_test           ; whole-shape clip (SPEC.md 11.3): CF=1 =
    jc .no                      ; armed and not wholly inside one fragment
    mov [bbs_sdy], si
    push cx
    mov cl, 3
    shr ax, cl
    mov [bbs_col], ax           ; left byte column
    pop ax                      ; AX = x2
    inc ax
    mov cl, 3
    shr ax, cl
    sub ax, [bbs_col]
    mov [bbs_nb], ax            ; bytes per row
    mov ax, dx
    sub ax, bx
    inc ax
    sub ax, di
    mov [bbs_rows], ax          ; rows to copy = height - |dy|
    cmp word [bbs_sdy], 0
    jl .down
    mov [bbs_ydst], bx          ; up: dest rows y1..y2-|dy|, walked top-down
    mov word [bbs_step], 1      ; (source below destination - forward-safe)
    jmp short .disp
.down:
    mov [bbs_ydst], dx          ; down: dest rows y1+|dy|..y2, walked
    mov word [bbs_step], -1     ; bottom-up (source above destination)
.disp:
    cmp byte [vid_planes], 1
    je .mono
    mov dx, 0x03CE              ; VRAM, via the latches
    mov ax, 0x0105              ; GC5: write mode 1 - a movsb read fills the
    out dx, ax                  ; latches, the write stores all four planes
    mov dx, VGA_SEG
    call bbs_lincopy
    mov dx, 0x03CE
    mov ax, 0x0005              ; write mode back to 0 (SPEC.md 1 rule 7)
    out dx, ax
    jmp short .ok
.mono:
    call bbs_bankcopy           ; the framebuffer IS [vid_rseg] (SPEC.md 39)
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    GFXDLEAVE                   ; ...and it banks CF, which is this routine's
    pop di                      ; whole answer
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; bbs_lincopy - scroll one LINEAR segment (the VGA stride): a buffer plane,
; or VRAM itself in write mode 1
; in:       DX = target segment; the bbs_* geometry
; clobbers: AX/BX/CX/DX/SI/DI, flags (gfx_scroll banked the caller's)
; -----------------------------------------------------------------------------
bbs_lincopy:
    push ds
    mov es, dx
    mov ax, [bbs_rows]
    mov [bbs_left], ax
    mov bx, [bbs_ydst]
.r:
    mov ax, bx
    add ax, [bbs_sdy]
    mul word [vid_stride]       ; AX = src row * stride (DX:AX, DX = 0)
    add ax, [bbs_col]
    mov si, ax
    mov ax, bx
    mul word [vid_stride]
    add ax, [bbs_col]
    mov di, ax
    mov cx, [bbs_nb]
    push es
    pop ds                      ; DS = ES = the target for the movs
    cld
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    add bx, [bbs_step]
    dec word [bbs_left]
    jnz .r
    pop ds
    ret

; -----------------------------------------------------------------------------
; bbs_bankcopy - the mono twin: per-row copies through gfx_rowbase, which
; absorbs the Hercules/CGA bank interleave (SPEC.md 39.2)
; in:       the bbs_* geometry; target = [vid_rseg]
; clobbers: AX/BX/CX/DX/SI/DI, flags
; -----------------------------------------------------------------------------
bbs_bankcopy:
    push ds
    mov ax, [vid_rseg]
    mov es, ax
    mov ax, [bbs_rows]
    mov [bbs_left], ax
    mov bx, [bbs_ydst]
.r:
    mov ax, bx
    add ax, [bbs_sdy]
    call gfx_rowbase            ; AX = the src row's start (CS-relative
    add ax, [bbs_col]           ; parameter reads - DS-safe, SPEC.md 39.2)
    mov si, ax
    mov ax, bx
    call gfx_rowbase
    add ax, [bbs_col]
    mov di, ax
    mov cx, [bbs_nb]
    push es
    pop ds
    cld
    rep movsb
    mov ax, KERNEL_SEG
    mov ds, ax
    add bx, [bbs_step]
    dec word [bbs_left]
    jnz .r
    pop ds
    ret

; -----------------------------------------------------------------------------
; bb_fill / bb_fill_gray / bb_xor_fill - software twins of gfx_fill,
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
; -----------------------------------------------------------------------------
; gfx_dbuf_gone - the retired double-buffer cell (slot 0x01F0, 20.8 rule 4)
; out: CF=1
; -----------------------------------------------------------------------------
gfx_dbuf_gone:
    stc
    ret

BBM_SOLID equ 0
BBM_GRAY  equ 1
BBM_XOR   equ 2

; SOLID is the one that falls through, because it is the one the machine
; spends its day on: every window background, every glyph cell's erase, every
; frame. XOR pays the jump instead - selection bands and drag outlines.
bb_xor_fill:
    mov byte [bb_mode], BBM_XOR
    jmp bb_rect

bb_fill_gray:
    mov byte [bb_mode], BBM_GRAY
    jmp bb_rect

bb_fill:
    mov byte [bb_mode], BBM_SOLID
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
    mov byte [bb_altm], 0
    cmp byte [bb_mode], BBM_GRAY ; ONLY gray starts from the dither byte. Solid
    jne .planes                  ; overwrote it per plane below (bb_ink), and
    call bb_parity               ; its own dither branch stages it there; XOR
    mov byte [bb_altm], 0xFF     ; never reads [bb_pat] at all. Staging it for
.planes:                         ; all three cost every solid fill five
                                 ; instructions it threw away
    mov dx, [vid_rseg]          ; DX = plane segment - the framebuffer, which
    mov bl, [gfx_color]         ; the framebuffer itself on mono (SPEC.md 39.3)
    mov bh, [vid_planes]        ; BH = plane counter: 4 or 1
.plane:
    mov es, dx
    cmp byte [bb_mode], BBM_SOLID
    jne .go
    call bb_ink                 ; solid: this pass's 00/FF byte
.go:
    call bb_plane_op
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
; bb_parity - internal: [bb_pat] = the 50% dither byte for the rect's top
;             row, which the row loops then alternate through [bb_altm].
; in:       [vga_y1] valid
; clobbers: AL, flags
; -----------------------------------------------------------------------------
bb_parity:
    mov al, 0xAA
    test byte [vga_y1], 1
    jz .set
    mov al, 0x55
.set:
    mov [bb_pat], al
    ret

; -----------------------------------------------------------------------------
; bb_ink - internal: solid mode's byte pattern for the pass about to run
;
; VGA: the plane's own bit of [gfx_color], consumed LSB-first out of BL -
; exactly what the open-coded shr/sbb did before.
;
; Mono: gfx_ink's reduction of the colour. Black and white become 00/FF; the
; middle greys become the 50% dither, which costs nothing extra because
; bb_rect has ALREADY put the row-parity AA/55 byte in [bb_pat] - arming
; [bb_altm] is all it takes to make BBM_GRAY's machinery do the work
; (SPEC.md 39.4).
;
; in:       BL = the colour's remaining bits (VGA path only)
; out:      [bb_pat], possibly [bb_altm]; BL shifted on the VGA path
; clobbers: AL, SI, flags
; -----------------------------------------------------------------------------
bb_ink:
    cmp byte [vid_mono], 0
    jne .mono
    shr bl, 1
    sbb al, al
    mov [bb_pat], al
    ret
.mono:
    mov al, [gfx_color]         ; gfx_ink's table read, inlined through SI -
    and ax, 15                  ; it indexes through BX, and BX is the plane
    mov si, ax                  ; counter and the colour bits here. SI is dead
    mov al, [si + gfx_inktab]   ; between vga_rect_setup and bb_plane_op, which
    cmp al, 1                   ; loads it itself
    je .dither
    mov [bb_pat], al
    ret
.dither:
    call bb_parity              ; the AA/55 byte, and then alternate it
    mov byte [bb_altm], 0xFF
    ret

; -----------------------------------------------------------------------------
; bb_plane_op - internal: one plane of a bb_rect op - left/right masked
;               edge columns plus the whole-byte interior.
; in:       ES = plane segment; [bb_mode]/[bb_pat]/[bb_altm]; vga_* rect
;           scratch from vga_rect_setup
; clobbers: ax, cx, si, di, bp, flags (preserves BX, DX)
;
; It banks BX and DX ONCE per plane so that bb_col - which used to save five
; registers on each of its two calls - can have the run of the register file
; and save none, and so that this routine's own interior loop can hold
; gfx_nextrow's two parameters in registers (SPEC.md 39.3). A row step was a
; near call plus two CS-overridden memory reads; it is three register
; instructions now, and a fill walks its rows THREE times.
; -----------------------------------------------------------------------------
bb_plane_op:
    push bx
    push dx
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
    mov bp, [vga_span]          ; bb_col owns BP too (it parks the wrap bit)
    dec bp                      ; interior bytes per row
    jz .out
    mov di, [vga_off]
    inc di
    mov si, [vga_rows]
    mov ah, [bb_pat]
    mov bx, [cs:vid_rowadd]     ; gfx_nextrow's two parameters, hoisted out of
    mov dx, [cs:vid_wrapbit]    ; the row loop and inlined below
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
    add di, bx                  ; gfx_nextrow, inlined (see the note above)
    test di, dx
    jz .inw
    add di, [cs:vid_wrapfix]
.inw:
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
; bb_col - internal: one masked byte column of [vga_rows] rows at ES:DI.
;          XOR mode: dest ^= mask. Else: dest = (dest & ~mask) |
;          (pattern & mask), pattern xor-stepped by [bb_altm] per row.
; in:       AH = mask, DI = top byte, ES = plane; [bb_mode]/[bb_pat]/[bb_altm]
; clobbers: AL, BX, CX, DX, SI, DI, BP, flags (AH and ES survive)
;
; It preserves NOTHING, deliberately: bb_plane_op banks BX/DX for the whole
; plane and reloads AH, DI, SI and BP itself around every call, so the five
; push/pop pairs this used to open with were saving registers their only
; caller was about to overwrite - a fifth of the cost of drawing one pixel
; (PERFORMANCE.md Part 2). SI and BP then pay for themselves by holding
; gfx_nextrow's step and wrap bit across the row loop.
; -----------------------------------------------------------------------------
bb_col:
    mov bl, ah
    not bl                      ; BL = ~mask
    mov ch, [bb_pat]            ; CH = running pattern
    mov cl, [bb_altm]
    mov dx, [vga_rows]
    mov si, [cs:vid_rowadd]     ; gfx_nextrow, hoisted and inlined - its body
    mov bp, [cs:vid_wrapbit]    ; is three instructions and the call and ret
    cmp byte [bb_mode], BBM_XOR ; around them cost as much again
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
; bb_fill_pat - software twin of gfx_fill_pat (SPEC.md 5/32): the 8-row
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
    call vga_pat_stage          ; vga_patbuf <- the 8 bytes at [gfx_pat]
    mov dx, [vid_rseg]          ; DX = plane segment (see bb_rect)
    mov bp, [vid_planes_w]      ; BP = plane counter
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
    call gfx_nextrow
    dec si
    jnz .row
    pop si
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; bb_xor_rect - software twin of gfx_xor_rect: 1px outline, XOR 0Fh.
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
; bb_save / bb_restore - software twins of gfx_save / gfx_restore, same
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
.geom:
    mov bp, [vga_span]
    inc bp                      ; BP = bytes per row
    mov bx, [vid_rseg]
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
    sub di, bp
    add di, [cs:vid_rowadd]     ; ...and here on DI, which is what gfx_nextrow
    test di, [cs:vid_wrapbit]   ; takes, so this one saves only the call and
    jz .rnw                     ; the ret
    add di, [cs:vid_wrapfix]
.rnw:
    dec dx
    jnz .rrow
    pop di
.pnext:
    pop es
    pop ds
    add bx, [vid_rpara]         ; nonzero even at one plane, or this loop
    cmp bx, [vid_rend]          ; would never terminate
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

bb_dir          resb 1          ; xfer: 0 = save, 1 = restore
bb_mode         resb 1          ; rect core: BBM_SOLID / BBM_GRAY / BBM_XOR
bb_pat          resb 1          ; rect core: plane value / dither pattern
bb_altm         resb 1          ; rect core: per-row pattern toggle (0 or FF)

bbs_col         resw 1          ; gfx_scroll (SPEC.md 5.5): left byte column
bbs_nb          resw 1          ;   bytes per row
bbs_sdy         resw 1          ;   signed dy (positive = content up)
bbs_ydst        resw 1          ;   first destination row
bbs_step        resw 1          ;   +1 (up: top-down) / -1 (down: bottom-up)
bbs_rows        resw 1          ;   rows to copy = height - |dy|
bbs_left        resw 1          ;   per-pass row countdown

section .text
