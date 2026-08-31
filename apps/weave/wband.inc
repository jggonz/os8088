; =============================================================================
; os8088 - apps/weave/wband.inc     THE GRID'S BAND COMPOSER (WEAVE-SPEC 6.9.1)
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm after the compiled C. apps/runcpm/rcband.inc is the
; shape and the precedent - PERFORMANCE.md Set 68 measured that loop at 145 us
; a cell to compose and 173 with the emit, 860 us a call - and this file is
; that loop with WEAVE's own inversion rule in place of RunCPM's attribute
; cursor.
;
; -----------------------------------------------------------------------------
; WHY A COMPOSER AND NOT A FONT_RUN PER ROW
; -----------------------------------------------------------------------------
; WEAVE-SPEC 6.9's whole arithmetic: a 79-cell grid row lettered with
; OSAPI_FONT_RUN is ~60 ms on the target (79 cells x ~756 us of primitive call
; amortised over the run, PERFORMANCE.md Part 2) and composed-and-blitted is
; 14.5 ms. A 20-row page is 291 ms against 1.4 s. That is the difference
; between a spreadsheet and a slideshow, and it is the reason section 6.9
; calls the band composer the component's painter rather than an optimisation
; of one.
;
; A row is composed WHOLE - the row number, the cell text, the column
; separators and the inverted selection all land in the same eight rows of
; bytes - and ONE os88_gfx_blit1 puts it down. The inversion costs nothing:
; a cell's mask byte is 0xFF or 0x00 and the glyph is XOR-ed with it either
; way, so the selection frame and the header band are free where a second
; drawing call would have been ~756 us each.
;
; -----------------------------------------------------------------------------
; POLARITY, WHICH IS THE ONE THING TO GET RIGHT
; -----------------------------------------------------------------------------
; The framebuffer's 1 is a LIT pixel (SPEC.md 5.4.2) and the kernel's glyph
; table's 1 is INK. So a plain cell is `glyph XOR 0xFF` - paper lit, ink clear
; - and an inverted cell is the glyph as it stands. Getting this backwards
; produces a picture that looks like a photographic negative of a spreadsheet,
; which is at least obvious; getting the INVERTED half backwards produces a
; selection that is invisible on white cells and only there, which is not.
;
; ES is the one register that is not this package's (SPEC.md 20.4): it is
; loaded from what OSAPI_FONT_GLYPHS answered and PUT BACK before returning.
; No string instruction is used anywhere here.
;
; cdecl (SPEC.md 73.3): arguments at [bp+4], [bp+6], ...; BP, DS, ES, SS:SP
; and DF preserved.
; =============================================================================

section .text

; The band buffer's stride, in BYTES, and it is an assemble-time constant for
; PERFORMANCE.md Set 68's reason: the eight rows are unrolled and each store's
; displacement is folded into the instruction, so a cell is eight
; `mov al,[es:bx+k] / xor al,ah / mov [di+90*k],al` triplets with no counter
; and no add. The widest content grid the family has is Hercules' 89 cells
; (WEAVE-SPEC 7.1.1) and a window can be no wider than its adapter, so 90
; covers the worst case with a byte to spare. It is a BYTE count and not a
; pixel one, so it needs no alignment of its own - only the blit's x and w do
; (os88.h), and both of those are cell multiples by construction.
WG_STRIDE equ 90

; -----------------------------------------------------------------------------
; void wg_band(unsigned char *dst, const char *chars, int n, int inv0,
;              int inv1)
;
; Compose n cells of `chars` into an 8-row 1bpp band at dst, whose stride is
; WG_STRIDE bytes a pixel row, in the framebuffer's polarity. Cells inv0 ..
; inv1-1 are drawn INVERTED (ink and paper swapped): that is WEAVE-SPEC
; 6.9.1's selected cell, and the whole band with inv0 = 0, inv1 = n is its
; column-header row.
;
; A character outside the glyph table's range - it covers what
; OSAPI_FONT_GLYPHS says it covers, and this OS stores 32..126 - composes as a
; BLANK cell, which is what makes a NUL-terminated string safe to hand over
; padded: the C side pads with spaces and never has to know the table's span.
;
; One os88_gfx_blit1(dst, WG_STRIDE, x, y, n*8, 8) then puts the band down.
; -----------------------------------------------------------------------------
_wg_band:
    push bp
    mov bp, sp
    push si
    push di
    push es
    push bx
    cmp byte [wg_gok], 0
    jne .have
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL = first, AH = last
    mov [wg_gseg], dx
    mov [wg_goff], si
    mov [wg_gfirst], al
    sub ah, al
    mov [wg_gspan], ah              ; last - first
    mov byte [wg_gok], 1
    jmp short .have
.none:
    jmp .out                        ; n = 0: .out is past a short jump's reach
.have:
    mov es, [wg_gseg]
    mov cx, [bp+8]                  ; n
    jcxz .none
    mov di, [bp+4]                  ; dst: cell 0 of pixel row 0
    mov si, [bp+6]                  ; the characters
    xor dx, dx                      ; DX = the cell index, counted up so the
                                    ; inversion window is one compare pair
    jmp short .cell
    ; The blank cell sits ABOVE the loop so that both back-edges are short
    ; jumps: the unrolled glyph body fills most of a short jump's reach.
.blank:
%assign WGR 0
%rep 8
    mov [di + WGR * WG_STRIDE], ah
%assign WGR WGR+1
%endrep
    inc di
    inc dx
    loop .cell
    jmp short .out
.cell:
    ; --- the mask: 0xFF for a plain cell, 0x00 inside the inverted span -----
    mov ah, 0xFF
    cmp dx, [bp+10]                 ; inv0
    jl .plain
    cmp dx, [bp+12]                 ; inv1, exclusive
    jge .plain
    xor ah, ah
.plain:                             ; AH = the xor mask
    mov al, [si]
    inc si
    sub al, [wg_gfirst]             ; below the table's first code wraps to
    cmp al, [wg_gspan]              ; 0xE0.., which is above its span too:
    ja .blank                       ; one compare covers both ends
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, [wg_goff]               ; ES:BX = the glyph
%assign WGR 0
%rep 8
    mov al, [es:bx + WGR]
    xor al, ah
    mov [di + WGR * WG_STRIDE], al  ; DS:DI - the band buffer is ours
%assign WGR WGR+1
%endrep
    inc di                          ; the next cell's column
    inc dx
    loop .cell
.out:
    pop bx
    pop es                          ; ES = KERNEL_SEG again, as it arrived
    pop di
    pop si
    pop bp
    ret

section .bss
wg_gseg:    resw 1                  ; the glyph table's segment (LOW_SEG)
wg_goff:    resw 1                  ; ...and offset
wg_gfirst:  resb 1                  ; the first character code it covers
wg_gspan:   resb 1                  ; last - first
wg_gok:     resb 1                  ; the three above are filled

section .text                       ; SWITCH BACK (CLAUDE.md's section rule)
