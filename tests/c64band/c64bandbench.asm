; =============================================================================
; os8088 - tests/c64band/c64bandbench.asm
;
; C64BANDBENCH: what does the C64's row composer cost? apps/c64/c64band.inc
; turns a run of C64 cells into a 1bpp band that ONE OSAPI_GFX_BLIT1 puts down
; (C64-SPEC §9.5), and every number in that document's cost table (9.7)
; and its fullscreen tier table (9.8) rests on what a cell of that compose
; costs. PERFORMANCE.md rule 4 says measure before quoting, and this is the
; measurement: the SAME c64band.inc the package ships is %included here and
; timed with tests/benchlib.inc, on the harness that priced RUNCPM's composer
; (tests/rcband, PERFORMANCE.md Set 65) and the proportional band before it
; (Set 64). Nothing here ships: `make c64bandbench` builds build/c64band.img
; and `all` does not.
;
;   make c64bandbench
;   make test TESTAPPS=build/c64band.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
;   ...double-click Disk B, C64BANDBENCH, click the window; read the counts
;   column: one -icount count is 0.359 ms of real XT (PERFORMANCE.md Part 4)
;
; THE ROWS, and why each:
;
;   FONT_RUN 40 aligned      the bar - forty cells lettered by the kernel,
;                            taken in the same run (Set 64's rule)
;   BAND1 40 cells           the compose alone, drawing nothing: the number
;                            C64-SPEC §9.7's model quotes per cell
;   BAND1 1 cell             ...and the floor, which is what a keystroke's
;                            echo composes once the write window has narrowed
;                            the span (9.2)
;   BLIT1 320x8 stride 40    the emit alone, the band's real stride
;   BAND 40 = compose+blit   what a changed row actually pays
;   BAND 1 = compose+blit    ...and what one changed cell pays
;   ROWSPAN 40 equal         the compare that decides NOTHING is drawn - the
;                            common case, and the one that must be cheap
;   ROWSPAN 40 differing     ...and the same compare when it has to find the
;                            first and last differing cell, forward and back
;   ROWCOPY 40               bringing the shadow up to date with what was drawn
;   ROWSIG 40                one row's source signature: the k = 1..24 shift
;                            test asks for 25 of these before it decides (9.4)
;   BAND_X2 8 rows           fullscreen's pixel doubling (9.8)
;
; AND THE IDENTITY ROWS, a correctness test in a benchmark's clothes: a line
; of C64 screen codes composed by the SHIPPING c64_band1 through the kernel's
; own 8x8 font used as a stand-in character generator, blitted straight to the
; glass, and under it the same line with the colour nibbles set so that every
; other cell inverts - so the {and, xor} mask path, the one thing a plain text
; screendump would never exercise, is on the glass. A screendump is the
; assertion.
;
; WHY THE KERNEL'S FONT AND NOT THE CHARGEN ROM: this bench is a package of
; its own and does not read C64.ROM. What is being timed is the composer's
; INNER LOOP - a table lookup, a mask, eight stores - and that costs the same
; whatever segment the glyphs live in. The CHARGEN ROM's own bytes are on the
; glass in the package, in QEMU, where they belong.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'C64BANDBENCH', cb_entry

; c64band.inc opens `section .bss` for its scratch; a plain package keeps
; everything in .text and bases its bss on os88_image_end. So .bss is declared
; HERE, first, as align=1 following .text: it then starts exactly at
; os88_image_end and c64band's scratch and this harness's own bss share it.
section .bss align=1 follows=.text
section .text

CB_CELLS    equ 40                ; the line: 40 cells, 320 px - the C64's own
CB_W        equ CB_CELLS * 8
CB_STRIDE   equ 40                ; c64_band1's stride: C64_BSTRIDE, always
CB_N        equ 8                 ; iterations a row (bandbench's)

; -----------------------------------------------------------------------------
; cb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
cb_entry:
    push si
    mov ax, cb_bss_end
    cmp ax, os88_image_end + CB_BSS_TOTAL
    ja .refuse
    call cb_setup
    call cb_hint
    mov si, cb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [cb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; cb_setup - the matrix row, the colour row, the two mask tables, and the
;            kernel's glyph table banked as the character generator
; -----------------------------------------------------------------------------
cb_setup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; the kernel's 8x8 font stands in for the character generator. It answers
    ; DX:SI = the table and AL = the first code it covers, so the matrix bytes
    ; below are biased by that first code and the composer's `code * 8 + goff`
    ; lands on a real glyph.
    call OSAPI_FONT_GLYPHS
    mov [cb_gseg], dx
    mov [cb_goff], si
    mov [cb_gfirst], al

    ; the matrix: 40 codes of ordinary prose, biased into the table's range
    mov di, cb_mat
    mov cx, CB_CELLS
    mov si, cb_seed
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, cb_seed
    mov al, [si]
.have:
    sub al, [cb_gfirst]
    mov [di], al
    inc si
    inc di
    loop .c

    ; two colour rows: one all colour 1, and one alternating 1 and 2 so the
    ; second identity line inverts every other cell
    mov di, cb_col
    mov cx, CB_CELLS
    mov al, 1
.k:
    mov [di], al
    mov byte [di+CB_CELLS], 1
    test cl, 1
    jz .k2
    mov byte [di+CB_CELLS], 2
.k2:
    inc di
    loop .k

    ; the {and, xor} pairs c64_band1 reads (C64-SPEC §9.6). Colour 1 is
    ; the glyph as it stands; colour 2 is the glyph inverted; everything else
    ; is a uniform dark cell.
    mov di, _c64_cmask
    mov cx, 32
    xor al, al
.m:
    mov [di], al
    inc di
    loop .m
    mov byte [_c64_cmask + 1*2], 0xFF
    mov byte [_c64_cmask + 1*2 + 1], 0x00
    mov byte [_c64_cmask + 2*2], 0xFF
    mov byte [_c64_cmask + 2*2 + 1], 0xFF
    mov byte [_c64_bgfill], 0

    call _c64_x2init                ; the 512-byte doubling table, once,
                                    ; outside every timed row

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_hint:
    push si
    call bl_blank
    mov si, cb_s_title
    call bl_sline
    call bl_head
    mov si, cb_s_hint
    call bl_sline
    pop si
    ret

cb_paint:
    call bl_paint
    ret

; THE CLIP IS ARMED HERE, because this is W_ONKEY and not W_PAINT. The kernel
; arms the damage clip for a paint and for nothing else (SPEC.md 11.3), and
; everything below draws: the whole report, a full fill of the content, the
; identity lines and three blits. Unclipped, a rerun of the bench paints
; straight over whatever window is covering this one. CF = 1 means not one
; pixel of our content is visible, and then the right amount to draw is none.
cb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cb_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call cb_run
    call cb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [cb_win], si
    mov bx, si
    call OSAPI_WM_CLIP_SET      ; W_ONCLICK: ours to arm (11.3), see cb_onkey
    jc .out
    call cb_run
    call cb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [cb_win]
    call OSAPI_WM_CONTENT
    mov [cb_cx], ax
    mov [cb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [cb_cx]
    mov bx, [cb_cy]
    mov cx, ax
    add cx, [cb_cw]
    dec cx
    mov dx, bx
    add dx, [cb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [cb_win]
    call bl_paint
    call cb_ident
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the measured bodies - each draws at ([cb_bx], [cb_by]), the last content
; rows, so nothing scribbles over a result line
; =============================================================================

; --- the bar: 40 cells of 8x8, byte-aligned ----------------------------------
cb_b_run40:
    mov cx, [cb_bx]
    mov dx, [cb_by]
    mov si, cb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- c64_band1(dst, 0, [cb_last], DS, cb_mat, cb_col, gseg, goff, 0, 0) ------
; The composer takes a SPAN, so the row is driven by [cb_last]: 39 for the
; whole line, 0 for one cell.
cb_b_band:
    xor ax, ax
    push ax                         ; bg
    push ax                         ; mode 0 = standard text
    push word [cb_goff]
    push word [cb_gseg]
    mov ax, cb_col
    push ax
    mov ax, cb_mat
    push ax
    mov ax, ds
    push ax                         ; mseg: this package's own segment
    push word [cb_last]
    xor ax, ax
    push ax                         ; first
    mov ax, cb_band
    push ax                         ; dst
    call _c64_band1
    add sp, 20
    ret

; --- BLIT1: the emit alone, ([cb_last]+1)*8 x 8 at stride 40 -----------------
; ES IS SAVED AND PUT BACK, and that is not bookkeeping: this runs inside a
; window callback, which the kernel enters with ES = KERNEL_SEG (SPEC.md 20.1)
; because that is where the window record lives, and a callback that returns
; with ES on its own segment has broken the contract for everything the kernel
; does next. The first draft did `push ds / pop es` at all three blit sites
; and never restored it - so the bench measured a body that omits the
; preservation the shipping code has to do, and returned wrong ES into the
; kernel while it was at it. The pair costs 4 clocks inside the timed body and
; the rows below were re-taken with it.
cb_b_blit:
    push es
    mov si, cb_band
    mov bp, CB_STRIDE
    mov ax, [cb_bx]
    mov bx, [cb_by]
    mov cx, [cb_last]
    inc cx
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    ret

cb_b_cband:
    call cb_b_band
    call cb_b_blit
    ret

; --- ROWSPAN: the compare that decides whether anything is drawn -------------
cb_b_span:
    mov ax, CB_CELLS
    push ax
    mov ax, cb_shad
    push ax
    mov ax, cb_band
    push ax
    call _c64_rowspan
    add sp, 6
    ret

; --- ROWCOPY: the shadow brought up to date ----------------------------------
cb_b_copy:
    mov ax, CB_CELLS
    push ax
    mov ax, cb_band
    push ax
    mov ax, cb_shad
    push ax
    call _c64_rowcopy
    add sp, 6
    ret

; --- ROWSIG: one row's source signature (the shift test asks for 25) ---------
cb_b_sig:
    mov ax, CB_CELLS
    push ax
    mov ax, cb_col
    push ax
    mov ax, cb_mat
    push ax
    mov ax, ds
    push ax
    call _c64_rowsig
    add sp, 8
    ret

; --- BAND_X2: fullscreen's pixel doubling, eight rows ------------------------
cb_b_x2:
    mov ax, 8
    push ax
    mov ax, cb_band
    push ax
    mov ax, cb_x2buf
    push ax
    call _c64_band_x2
    add sp, 6
    ret

; -----------------------------------------------------------------------------
; cb_ident - the identity lines: the kernel's own lettering, then the SHIPPING
; composer's band of the same text under it (the same pixels or the composer
; is wrong), then the same band with every other cell's colour set so the xor
; mask inverts it. A screendump is the assertion.
; -----------------------------------------------------------------------------
cb_ident:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov cx, [cb_ix]
    mov dx, [cb_iy]
    mov si, cb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov word [cb_last], CB_CELLS - 1
    call cb_b_band
    mov si, cb_band
    mov bp, CB_STRIDE
    mov ax, [cb_ix]
    mov bx, [cb_iy]
    add bx, 10
    mov cx, CB_W
    mov dx, 8
    push es
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    mov byte [cb_cf], 0
    jnc .ok
    mov byte [cb_cf], 1
.ok:
    ; ...and the alternating-colour row, which exercises the xor mask
    xor ax, ax
    push ax
    push ax
    push word [cb_goff]
    push word [cb_gseg]
    mov ax, cb_col + CB_CELLS
    push ax
    mov ax, cb_mat
    push ax
    mov ax, ds
    push ax
    mov ax, CB_CELLS - 1
    push ax
    xor ax, ax
    push ax
    mov ax, cb_band
    push ax
    call _c64_band1
    add sp, 20
    mov si, cb_band
    mov bp, CB_STRIDE
    mov ax, [cb_ix]
    mov bx, [cb_iy]
    add bx, 20
    mov cx, CB_W
    mov dx, 8
    push es
    push ds
    pop es
    call OSAPI_GFX_BLIT1
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
; cb_rowb - one report row whose BODY CONTAINS A BLIT1, with [bl_body] and SI
; already set. If the preflight found the kernel refusing bands, the row is
; not timed at all and says so: a number here would be the cost of a refusal
; wearing the label of a draw.
; -----------------------------------------------------------------------------
cb_rowb:
    cmp byte [cb_blitok], 0
    je cb_norow
    xor al, al
    jmp bl_run                      ; tail call: bl_run returns to our caller

cb_norow:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov si, cb_s_refused
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit
    pop di
    ret

; =============================================================================
; cb_run - the whole report
; =============================================================================
cb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [cb_win]
    call OSAPI_WM_CONTENT
    mov [cb_cx], ax
    mov [cb_cy], dx
    mov bx, [cb_win]
    call OSAPI_WM_GEOM
    mov [cb_cw], cx
    mov [cb_ch], dx
    mov ax, [cb_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [cb_bx], ax
    mov [cb_ix], ax
    mov ax, [cb_cy]
    add ax, [cb_ch]
    sub ax, 13
    mov [cb_by], ax
    sub ax, 32
    mov [cb_iy], ax

    call bl_blank
    mov si, cb_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    ; --- THE PREFLIGHT: DOES THIS KERNEL DRAW A BAND AT ALL? ---------------
    ; OSAPI_GFX_BLIT1 answers CF = 1 REFUSED with nothing drawn - a broken
    ; argument, or a kern_small kernel that carries the slot without the body.
    ; The timed bodies below ignored that answer, so on such a kernel the
    ; bench printed attractive numbers for a call that draws nothing and
    ; returns at once, and published them as the cost of drawing a band. A
    ; measurement of a refusal is not a measurement of the thing refused, so
    ; it is taken ONCE here, outside the timing, and every row whose body
    ; contains a blit prints REFUSED instead of a number.
    mov word [cb_last], CB_CELLS - 1
    call cb_b_band
    push es
    mov si, cb_band
    mov bp, CB_STRIDE
    mov ax, [cb_bx]
    mov bx, [cb_by]
    mov cx, CB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    mov byte [cb_blitok], 1
    jnc .bok
    mov byte [cb_blitok], 0
    mov si, cb_s_noblit
    call bl_sline
.bok:

    mov si, cb_s_hdr
    call bl_sline

    mov word [bl_n], CB_N
    mov word [cb_last], CB_CELLS - 1

    mov word [bl_body], cb_b_run40
    mov si, cb_r_run40
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_band
    mov si, cb_r_band40
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_blit
    mov si, cb_r_blit40
    call cb_rowb

    mov word [bl_body], cb_b_cband
    mov si, cb_r_cband40
    call cb_rowb

    mov word [cb_last], 0
    mov word [bl_body], cb_b_band
    mov si, cb_r_band1
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_cband
    mov si, cb_r_cband1
    call cb_rowb
    mov word [cb_last], CB_CELLS - 1

    ; --- the compare, the copy and the signature -------------------------
    call bl_blank
    mov si, cb_s_hdr2
    call bl_sline

    call cb_b_band                  ; band and shadow identical...
    call cb_b_copy
    mov word [bl_body], cb_b_span
    mov si, cb_r_spaneq
    xor al, al
    call bl_run

    mov byte [cb_shad + 3*CB_STRIDE + 5], 0x99      ; ...and now differing at
    mov byte [cb_shad + 6*CB_STRIDE + 30], 0x99     ; cells 5 and 30
    mov word [bl_body], cb_b_span
    mov si, cb_r_spandiff
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_copy
    mov si, cb_r_copy
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_sig
    mov si, cb_r_sig
    xor al, al
    call bl_run

    mov word [bl_body], cb_b_x2
    mov si, cb_r_x2
    xor al, al
    call bl_run

    call cb_ident
    call bl_lclr
    mov si, cb_r_ident
    xor di, di
    call bl_lput
    mov al, [cb_cf]
    or al, al
    mov si, cb_s_drawn
    jz .cf0
    mov si, cb_s_refused
.cf0:
    mov di, BL_C_N
    call bl_lput
    call bl_lcommit

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "c64/c64band.inc"          ; THE THING MEASURED - the package's own
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

cb_tpl:
    dw 7, 22, 632, 448
    dw cb_ttl, cb_paint, cb_onkey, cb_onclick

cb_ttl:     db 'C64 Band Bench', 0

cb_s_title: db 'C64BANDBENCH - the C64 row composer (apps/c64/c64band.inc)', 0
cb_s_hint:  db 'Click the window, or press R, to run.', 0
cb_s_hdr:   db '-- the line: 40 cells of 8x8, 320 px --', 0
cb_s_hdr2:  db '-- the compare, the copy and the signature --', 0
cb_s_noblit: db 'OSAPI_GFX_BLIT1 REFUSES: no band rows below', 0

cb_seed:    db 'THE QUICK BROWN FOX JUMPS OVER 13 LAZY DO', 0
cb_str:     db 'THE QUICK BROWN FOX JUMPS OVER 13 LAZY DO', 0

cb_r_run40:   db 'FONT_RUN 40 aligned', 0
cb_r_band40:  db 'BAND1 40 cells', 0
cb_r_blit40:  db 'BLIT1 320x8 stride40', 0
cb_r_cband40: db 'BAND 40 compose+blit', 0
cb_r_band1:   db 'BAND1 1 cell', 0
cb_r_cband1:  db 'BAND 1 compose+blit', 0
cb_r_spaneq:  db 'ROWSPAN 40 equal', 0
cb_r_spandiff: db 'ROWSPAN 40 differing', 0
cb_r_copy:    db 'ROWCOPY 40 cells', 0
cb_r_sig:     db 'ROWSIG 40 cells', 0
cb_r_x2:      db 'BAND_X2 8 rows', 0
cb_r_ident:   db 'identity blit says', 0
cb_s_drawn:   db 'DRAWN (CF=0)', 0
cb_s_refused: db 'REFUSED (CF=1)', 0

cb_win:     dw 0
cb_cx:      dw 0
cb_cy:      dw 0
cb_cw:      dw 0
cb_ch:      dw 0
cb_bx:      dw 0
cb_by:      dw 0
cb_ix:      dw 0
cb_iy:      dw 0
cb_last:    dw CB_CELLS - 1
cb_gseg:    dw 0
cb_goff:    dw 0
cb_gfirst:  db 0
cb_cf:      db 0
cb_blitok:  db 1            ; the preflight's answer (see cb_run)

; c64band.inc reads these two out of the C's own storage in the package (they
; are ordinary globals there, so nasm can see the label). Here they are the
; bench's, filled by cb_setup.
_c64_cmask: times 32 db 0
_c64_bgfill: db 0

; cb_band 320 + cb_shad 320 + cb_mat 40 + cb_col 80 + cb_x2buf 1280, and the
; eight bytes of slack that make the arithmetic legible
CB_BSS_OWN  equ CB_STRIDE * 8 + CB_STRIDE * 8 + CB_STRIDE + CB_STRIDE * 2 \
                + 80 * 16 + 8
CB_BSS_TOTAL equ CB_BSS_OWN + BL_BSS_SIZE + 800   ; + c64band.inc's scratch
                                                  ; (80 + 512 + 8), with slack;
                                                  ; cb_entry checks the sum and
                                                  ; REFUSES THE LAUNCH if it is
                                                  ; short - which is what the
                                                  ; first build of this bench
                                                  ; did, by 24 bytes
    OS88_BSS CB_BSS_TOTAL
    OS88_IMAGE_END

section .bss
cb_band:    resb CB_STRIDE * 8      ; the 40x8 band, as c64_bnd is
cb_shad:    resb CB_STRIDE * 8      ; ...and the frame shadow's row
cb_mat:     resb CB_STRIDE          ; the screen matrix row
cb_col:     resb CB_STRIDE * 2      ; two colour rows: plain and alternating
cb_x2buf:   resb 80 * 16            ; the pixel-doubled band
cb_bl:      resb BL_BSS_SIZE
cb_bss_end:
section .text

    BL_BSS cb_bl
