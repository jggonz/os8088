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
    call sw_rect_pl
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
; sw_rect_pl - internal: the PLANE LOOP alone, for a rect vga_rect_setup has
;              already described. sw_rect is its only caller that clips;
;              SPEC.md 5.10's gfx_spans is the other, and it fills the same
;              scratch a row at a time from its own leaner setup.
; in:       the vga_* rect scratch and [sw_mode] are set
; out:      -
; clobbers: everything but the segment registers
; -----------------------------------------------------------------------------
sw_rect_pl:
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
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; sw_spans - the 1bpp half of SPEC.md 5.10's gfx_spans
; in:       gfx_spans' walk is armed; jumped to, not called
; out:      jumps back to gfx_spans.done
;
; **IT DOES NOT GO THROUGH sw_rect_pl, AND SPEC.md 5.10.4 IS WHY.** That is the
; body for a RECT and it costs 1,806 cycles for a single row, measured - the
; mode tests, the plane loop, sw_ink's table read and two sw_col calls that
; each reload six registers, all of which a rect pays ONCE and amortises over
; its rows. A span IS one row, so a span list paid a rect's fixed cost sixteen
; times and the primitive lost to the eight fills it replaced. PERFORMANCE.md
; Set 88's trap, one layer down: reusing the shape of the machinery is not
; reusing the optimisation.
;
; So the ink, the plane and the dither's alternation are hoisted out of the
; loop and what is left per row is the two masked edge bytes and the interior.
; The ink comes from sw_ink exactly as a fill's does, [vga_y1] being the FIRST
; row when it runs, and a middle grey then alternates through [sw_altm] per row
; - which is what sw_col does inside a rect (SPEC.md 39.4).
; -----------------------------------------------------------------------------
sw_spans:
    mov byte [sw_mode], SWM_SOLID   ; sw_ink's, and nothing else reads it here
    mov ax, [gfx_sp_y]
    mov [vga_y1], ax                ; the first row: sw_parity's parity
    mov byte [sw_altm], 0
    call sw_ink                     ; [sw_pat], and [sw_altm] if it dithers
    mov ax, [vid_rseg]              ; one plane: [vid_mono] and [vid_planes]
    mov [gfx_sp_fb], ax             ; are set together (SPEC.md 39.5)
    mov es, ax
.row:
    call gfx_sp_next                ; AH/AL = the masks, BX = span, DI = the byte
    jc .next
    mov dl, [sw_pat]
    mov dh, dl                      ; left edge (the whole row if 1 byte wide)
    and dh, ah
    not ah
    and [es:di], ah
    or  [es:di], dh
    or bx, bx
    jz .next
    add di, bx                      ; right edge
    mov dh, dl
    and dh, al
    not al
    and [es:di], al
    or  [es:di], dh
    dec bx                          ; interior bytes
    jz .next
    sub di, bx                      ; DI is on the right edge: step back over
    mov cx, bx                      ; the interior rather than reload the left
    mov al, dl                      ; byte, which is no longer in memory to
    rep stosb                       ; reload (SPEC.md 5.7.1's shape)
.next:
    mov al, [sw_altm]               ; gray alternates down the rows, solid
    xor [sw_pat], al                ; leaves [sw_altm] zero and this alone
    dec word [gfx_sp_n]
    jnz .row
    jmp gfx_spans.done
%endif

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

; =============================================================================
; THE BLIT DECODER (SPEC.md 5.4.1) - a row of packed 4bpp straight into 1bpp
;
; gfx_blit4 used to emit one gfx_hline per coalesced RUN, and SPEC.md 5.4.1's
; first form wrote the run itself instead - which took ~756us of ARRIVING off
; each one and left ~371us of per-run work. This takes the runs out altogether:
; a source BYTE is two pixels and maps, through 39.4's reduction, to a two-bit
; destination pattern that depends only on its value and one parity bit, so a
; 256-byte table answers it outright and the loop is a read, an xlat and a
; shift per two pixels. It costs the same whatever is in the row.
;
; THERE IS NO HYBRID and docs/LAST-DROP.md 3 is why. The run path is cheaper
; only below 1.84 runs a row (PERFORMANCE.md Set 43's two-term fit), nothing
; anyone waits on draws flat art through this - Paint's canvas, Solitaire's
; lattice card backs, ArtfulType's line of TEXT per keystroke - and keeping
; both paths costs 336 bytes and a permanent second opinion about the pixels.
;
; THREE SEGMENTS AT ONCE is what shapes the loop: the source is the CALLER's
; (ES on entry), the framebuffer is [vid_rseg], and the table is the kernel's.
; So DS becomes the source (lodsb), ES the framebuffer, and the table is read
; `ss xlat` - the tables having moved to .lowbss when SPEC.md 5.4.1.3 spent
; the segment (39.3.1's reason), where they were `cs xlat` in .bss. It is the
; same one prefix byte and the same clocks either way, because SS IS LOW_SEG
; for all kernel code (SPEC.md 1) exactly as CS is KERNEL_SEG. Nothing may
; read a [gfx_blit_*] word inside the loop.
; =============================================================================

; sw_pbit - internal: the 1bpp bit for colour SI at parity DH (SPEC.md 39.4)
; out: AL = 0 or 1; clobbers AL and flags
sw_pbit:
    mov al, [si + gfx_inktab]
    cmp al, 1
    je .dither
    and al, 1                   ; gfx_inktab is 00 / 01 / FF, so the low bit
    ret                         ; IS the answer for the two solid classes
.dither:
    mov al, dh                  ; ...and a middle grey alternates with (x+y) -
    xor al, 1                   ; INVERTED, because sw_parity's 0xAA has its
    ret                         ; bit SET at x=0 of an even row, and bit 7 is
                                ; the leftmost pixel

; -----------------------------------------------------------------------------
; sw_pairbuild - fill the two pair tables from gfx_inktab (SPEC.md 5.4.1)
; out: gfx_pairtab0/1 built, [gfx_pairbuilt] = 1; every register preserved
;
; Built at run time rather than assembled, so gfx_inktab stays the ONE place
; the reduction is written down. Once per boot, ~5,000 clocks, behind a flag
; in .text because -f bin zeroes no .bss.
;
; tab0 is for a pair whose FIRST pixel is at even (x+y), tab1 for odd; the two
; pixels of a pair are always opposite, so one bit selects the table and the
; loop then costs nothing per pixel for the dither at all.
; -----------------------------------------------------------------------------
sw_pairbuild:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor bx, bx
.byte:
    mov ax, bx                  ; AL = the byte; SI = its high nibble...
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov si, ax
    mov ax, bx                  ; ...and DI its low one
    and al, 15
    xor ah, ah
    mov di, ax
    xor dh, dh                  ; --- tab0: hi at parity 0, lo at parity 1
    call sw_pbit
    shl al, 1
    mov dl, al
    push si
    mov si, di
    mov dh, 1
    call sw_pbit
    pop si
    or dl, al
    mov [ss:bx + gfx_pairtab0], dl
    mov dh, 1                   ; --- tab1: the other way round
    call sw_pbit
    shl al, 1
    mov dl, al
    push si
    mov si, di
    xor dh, dh
    call sw_pbit
    pop si
    or dl, al
    mov [ss:bx + gfx_pairtab1], dl
    inc bx
    cmp bx, 256
    jb .byte
    mov byte [gfx_pairbuilt], 1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sw_blit_row - ONE ROW of packed 4bpp into the 1bpp framebuffer (SPEC.md 5.4.1)
; in:       ES:SI = the row's first source byte (pixel 0 is its HIGH nibble),
;           [gfx_blit_x] = pixel 0's destination x, [gfx_blit_w] = pixels,
;           [gfx_blit_ro] = the row's base byte, [gfx_blit_tb] = this row's
;           pair table. gfx_blit4 has already proved the row is ON SCREEN in x.
; out:      nothing; every register preserved, ES and DS included
;
; The accumulator is 16-bit and carries `AH` bits, seeded with the `x & 7` bits
; of the first destination byte that belong to the SCREEN - so the first store
; needs no mask, and only the last one does. That seeding is why this handles
; an odd x with no special case: the completed byte is `DX >> (AH-8)` and
; `AH-8` is 0 or 1 by construction, fixed for the whole row.
;
; It does NOT clip in x, and that is the one thing gfx_blit4 owes it: a left
; clip would advance the source by a pixel count that may be ODD, which shifts
; the nibble phase of every pair after it. Refusing an off-screen block costs
; nothing real - Paint, ArtfulType and Solitaire all clip their own blits - and
; buys the loop its simplicity.
;
; The general loop costs 19 bytes a PAIR and eight of them are the accounting -
; `add ah,2 / cmp ah,8 / jb`, asked per pair to find a boundary that arrives
; every fourth one. Once the FIRST destination byte has been stored the phase
; is fixed for the rest of the row - nothing pending, or exactly one bit
; pending, for ever - so four pairs are always the same shape and SPEC.md
; 5.4.1.2 gives each phase a body of its own, past the `ret` below: .abyte (37
; bytes per eight pixels) and .obyte (52, the carry epilogue being the whole
; difference).
;
; NEITHER TESTS THE PHASE, because each is entered from the store that proves
; its own precondition: an odd x can never reach the whole-byte store (AH steps
; by 2 from an odd seed, so it hits 9 and carries, for ever) and an even one
; can never reach the carry. Both are gated - tools/ptcheck.py's PTNUDGE is an
; odd sideways drag, and without it a run proves half the routine.
; -----------------------------------------------------------------------------
sw_blit_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, [gfx_blit_x]
    mov di, [gfx_blit_ro]
    mov bx, ax
    and bx, 7                   ; BL = the bits of byte 0 we do NOT own
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax                  ; DI = the first destination byte
    mov cx, [gfx_blit_w]
    mov bp, cx
    and bp, 1                   ; BP = 1 if there is an odd trailing pixel
    shr cx, 1
    push cx                     ; ...whole pairs, banked over the shift below
    mov dx, [gfx_blit_tb]
    mov ax, [vid_rseg]
    push dx                     ; ...and the table, over the segment swap
    mov dx, es                  ; DX = the CALLER's source segment
    mov es, ax                  ; ES = the framebuffer
    mov ds, dx                  ; DS = the source: NO kernel reads past here
    xor dx, dx
    mov ah, bl                  ; AH = bits already accounted for
    or bl, bl
    jz .primed
    mov al, [es:di]             ; the screen's top `bl` bits, right-aligned, so
    mov cl, 8                   ; that the first store owes no mask
    sub cl, bl
    shr al, cl
    mov dl, al
.primed:
    pop bx                      ; BX = the pair table (ss xlat's base)
    pop cx                      ; CX = whole pairs
    jcxz .nomore
.pair:
    lodsb                       ; DS:SI - the source byte, two pixels
    ss xlat                     ; ...and its two destination bits
    shl dx, 1
    shl dx, 1
    or dl, al
    add ah, 2
    cmp ah, 8
    jb .next
    sub ah, 8
    jnz .carry
    mov [es:di], dl             ; the common case: eight bits, none over
    inc di
    xor dx, dx
    dec cx                      ; ...and the pair just consumed
    jz .nomore
    jmp .esplit                 ; SPEC.md 5.4.1.2: DI is on a byte with nothing
                                ; pending, so four pairs are one whole byte
.carry:
    mov al, dl                  ; AH = 1: one bit belongs to the NEXT byte, so
    and al, 1                   ; bank it, shift the completed byte down and
    shr dx, 1                   ; put it back
    mov [es:di], dl
    inc di
    xor dx, dx
    mov dl, al
    dec cx
    jz .nomore
    jmp .osplit                 ; ...and 5.4.1.2's other phase: four pairs are
                                ; nine bits, a byte and the next pending one
.nomore:
    jmp short .odd              ; the bodies sit past the `ret`, out of `loop`'s
.next:                          ; reach, so every exit trampolines through here
    loop .pair
.odd:
    or bp, bp                   ; a trailing pixel with no partner
    jz .tail
    lodsb
    ss xlat
    shr al, 1                   ; the HIGH nibble's bit, out of the pair
    shl dx, 1
    or dl, al
    inc ah
    cmp ah, 8
    jb .tail
    xor ah, ah
    mov [es:di], dl
    inc di
    xor dx, dx
.tail:
    or ah, ah                   ; AH bits left over: the TOP AH bits of the
    jz .done                    ; last byte, and the screen keeps the rest
    mov cl, 8
    sub cl, ah
    mov al, dl
    shl al, cl
    mov ah, 0xFF
    shl ah, cl                  ; AH = the mask of the bits we own
    mov ch, ah
    not ch
    and ch, [es:di]
    or ch, al
    mov [es:di], ch
.done:
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

; --- SPEC.md 5.4.1.2's two aligned bodies. They live PAST the `ret` because
; `loop .pair` is a short jump and cannot reach over them, and each is entered
; from the store that proves its own precondition - so neither tests the phase.
.esplit:
    mov ah, cl                  ; AH = the pairs the body cannot take...
    and ah, 3
    shr cx, 1
    shr cx, 1                   ; ...and CX = the whole destination bytes
    jcxz .erest
.abyte:
    lodsb
    ss xlat
    mov dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    lodsb
    ss xlat
    shl dl, 1
    shl dl, 1
    or dl, al
    mov al, dl
    stosb                       ; ES:DI, and DI walks itself
    loop .abyte
.erest:
    mov cl, ah                  ; the leftovers go back to the general loop,
    xor ch, ch                  ; accumulator empty and DI on a byte
    xor ax, ax
    xor dx, dx
    jcxz .backodd
    jmp .pair
.backodd:
    jmp .odd
.osplit:
    mov al, cl                  ; AH is 1 here and has to STAY 1, so this one's
    and al, 3                   ; leftover count rides the stack rather than AH
    push ax
    shr cx, 1
    shr cx, 1
    jcxz .orest
.obyte:
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    lodsb
    ss xlat
    shl dx, 1
    shl dx, 1
    or dl, al
    mov al, dl                  ; nine bits: the top eight are the byte and the
    and al, 1                   ; ninth is the next one's pending bit
    shr dx, 1
    mov [es:di], dl
    inc di
    xor dx, dx
    mov dl, al
    loop .obyte
.orest:
    pop ax                      ; AL = the leftover pairs, AH = 1 again
    mov cl, al
    xor ch, ch
    jcxz .backodd
    jmp .pair

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
