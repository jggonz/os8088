; =============================================================================
; os8088 - tests/weaveband/weavebandbench.asm
;
; WEAVEBANDBENCH: what does the GRID's band composer cost, and does it hold
; PERFORMANCE.md Set 68's numbers? apps/weave/wband.inc's wg_band turns a run
; of grid cells into a 1bpp band that ONE OSAPI_GFX_BLIT1 puts down
; (WEAVE-SPEC 6.9.1), and every row of WEAVE-SPEC 14's grid pricing rests on
; what a cell of that compose costs. PERFORMANCE.md rule 4 says measure before
; quoting, and this is the measurement: the SAME wband.inc the package ships
; is %included here and timed with tests/benchlib.inc, on the harness that
; priced RUNCPM's row composer (tests/rcband, PERFORMANCE.md Set 68) so that
; the two numbers can be read against each other in the same units.
;
; Nothing here ships: `make weavebandbench` builds build/weaveband.img and
; `all` does not.
;
;   make weavebandbench
;   make test TESTAPPS=build/weaveband.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
;   ...double-click Disk B, WEAVEBANDBENCH, click the window; read the counts
;   column: one -icount count is 0.359 ms of real XT (PERFORMANCE.md Part 4)
;
; THE ROWS, and why each:
;
;   FONT_RUN 79 aligned    the bar - the same 79-cell line lettered by the
;                          kernel, taken in the same run (Set 64's rule that a
;                          comparison is only a comparison inside one run)
;   WG_BAND 79 plain       the compose alone, drawing nothing: the number
;                          WEAVE-SPEC 14's grid rows are computed from
;   WG_BAND 79 inverted    the SAME compose with 6.9.1's selected cell
;                          inverted. 6.9.1 claims the inversion is free - the
;                          mask byte is 0xFF or 0x00 and the glyph is XOR-ed
;                          with it either way - and a claim about cost that
;                          nobody measured is a hope. These two rows must be
;                          within a count of each other or the claim is wrong
;   BLIT1 632x8 stride90   the emit alone, at the band's real stride
;   BAND 79 = compose+blit what a changed grid row actually pays (14's
;                          `79-cell row compose+blit`)
;   BAND 8 cells           ONE grid cell's worth: 6.9.1's column is 8 cells,
;                          so this is the floor of a one-cell edit if the
;                          runtime ever blitted a cell instead of a row
;   BAND 1 cell            ...and the composer's own floor
;   FONT_RUN 1 cell        against the run it replaced, so the crossover is
;                          visible rather than asserted
;
; AND THE IDENTITY ROWS, a correctness test in a benchmark's clothes: the
; string lettered by FONT_RUN, the same string composed by wg_band and
; blitted directly under it (the same pixels or the composer is wrong), and a
; third with cells 12..20 INVERTED - the selection span, which is the one
; thing a plain-text screendump would never exercise. A screendump is the
; assertion.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'WEAVEBANDBENCH', wb_entry

; wband.inc opens `section .bss` for its statics; a plain package keeps
; everything in .text and bases its bss on os88_image_end. So .bss is declared
; HERE, first, as align=1 following .text - tests/rcband says why at length.
section .bss align=1 follows=.text
section .text

WB_CELLS    equ 79                ; the line: 79 cells, 632 px - a framed
                                  ; 640-px window's whole content, and CGA's
                                  ; own CW (WEAVE-SPEC 7.1.1)
WB_W        equ WB_CELLS * 8
WB_N        equ 8                 ; iterations a row (bandbench's)
WB_COLW     equ 8                 ; WEAVE-SPEC 6.9.1's data column
WB_INV0     equ 12                ; ...and one selected cell's span
WB_INV1     equ 20

; -----------------------------------------------------------------------------
; wb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
wb_entry:
    push si
    mov ax, wb_bss_end              ; the layout the header promised: every
    cmp ax, os88_image_end + WB_BSS_TOTAL   ; bss byte inside what the loader
    ja .refuse                      ; zeroes and the region holds
    call wb_teststr
    call wb_bank                    ; wg_band's glyph statics, once, outside
    call wb_hint                    ; the timed rows
    mov si, wb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [wb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; wb_bank - one wg_band of one cell, so OSAPI_FONT_GLYPHS is asked before any
; row is timed (wg_band asks on its first call and banks the answer)
wb_bank:
    push ax
    xor ax, ax
    push ax                         ; inv1
    push ax                         ; inv0
    mov ax, 1
    push ax                         ; n
    mov ax, wb_str
    push ax                         ; chars
    mov ax, wb_band
    push ax                         ; dst
    call _wg_band
    add sp, 10
    pop ax
    ret

; -----------------------------------------------------------------------------
; wb_teststr - WB_CELLS characters shaped like a GRID ROW rather than prose:
; the row-number gutter, then cells of digits and short labels, because that
; is what the composer actually sees (WEAVE-SPEC 6.9.1) and a bench fed prose
; measures a different mix of glyphs.
; -----------------------------------------------------------------------------
wb_teststr:
    push ax
    push cx
    push si
    push di
    mov di, wb_str
    mov cx, WB_CELLS
    mov si, wb_seed
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, wb_seed
    mov al, [si]
.have:
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

wb_hint:
    push si
    call bl_blank
    mov si, wb_s_title
    call bl_sline
    call bl_head
    mov si, wb_s_hint
    call bl_sline
    pop si
    ret

wb_paint:
    call bl_paint
    ret

wb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [wb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call wb_run
    call wb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [wb_win], si
    call wb_run
    call wb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [wb_win]
    call OSAPI_WM_CONTENT
    mov [wb_cx], ax
    mov [wb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [wb_cx]
    mov bx, [wb_cy]
    mov cx, ax
    add cx, [wb_cw]
    dec cx
    mov dx, bx
    add dx, [wb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [wb_win]
    call bl_paint
    call wb_ident
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE TIMED BODIES
; =============================================================================

; --- the bar: 79 cells of 8x8, byte-aligned ----------------------------------
wb_b_run79:
    mov cx, [wb_bx]
    mov dx, [wb_by]
    mov si, wb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- wg_band(wb_band, wb_str, n, inv0, inv1): the shipping compose -----------
; in: [wb_ncell] = n, [wb_i0]/[wb_i1] = the inverted span
wb_b_band:
    push word [wb_i1]
    push word [wb_i0]
    push word [wb_ncell]
    mov ax, wb_str
    push ax
    mov ax, wb_band
    push ax
    call _wg_band
    add sp, 10
    ret

; --- BLIT1: the emit alone, [wb_ncell]*8 x 8 at stride WG_STRIDE -------------
wb_b_blit:
    mov si, wb_band
    mov bp, WG_STRIDE
    mov ax, [wb_bx]
    mov bx, [wb_by]
    mov cx, [wb_ncell]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    ret

; --- BAND: compose then emit -------------------------------------------------
wb_b_cband:
    call wb_b_band
    call wb_b_blit
    ret

; --- FONT_RUN of one cell ----------------------------------------------------
wb_b_run1:
    mov cx, [wb_bx]
    mov dx, [wb_by]
    mov si, wb_one
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; =============================================================================
; wb_run - the suite
; =============================================================================
wb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [wb_win]
    call OSAPI_WM_CONTENT
    mov [wb_cx], ax
    mov [wb_cy], dx
    mov bx, [wb_win]
    call OSAPI_WM_GEOM
    mov [wb_cw], cx
    mov [wb_ch], dx
    mov ax, [wb_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [wb_bx], ax
    mov [wb_ix], ax
    mov ax, [wb_cy]
    add ax, [wb_ch]
    sub ax, 13
    mov [wb_by], ax
    sub ax, 32
    mov [wb_iy], ax

    call bl_blank
    mov si, wb_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    mov si, wb_s_hdr
    call bl_sline

    mov word [bl_n], WB_N
    mov word [wb_ncell], WB_CELLS
    mov word [wb_i0], 0
    mov word [wb_i1], 0

    mov word [bl_body], wb_b_run79
    mov si, wb_r_run79
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [wb_tbar], ax
    mov [wb_tbar+2], dx

    mov word [bl_body], wb_b_band
    mov si, wb_r_band79
    xor al, al
    call bl_run

    mov word [wb_i0], WB_INV0       ; 6.9.1's selected cell, and the claim
    mov word [wb_i1], WB_INV1       ; that inverting it costs nothing
    mov word [bl_body], wb_b_band
    mov si, wb_r_bandinv
    xor al, al
    call bl_run
    mov word [wb_i0], 0
    mov word [wb_i1], 0

    mov word [bl_body], wb_b_blit
    mov si, wb_r_blit79
    xor al, al
    call bl_run

    mov word [bl_body], wb_b_cband
    mov si, wb_r_cband79
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [wb_tband], ax
    mov [wb_tband+2], dx

    mov word [wb_ncell], WB_COLW
    mov word [bl_body], wb_b_cband
    mov si, wb_r_cband8
    xor al, al
    call bl_run

    mov word [wb_ncell], 1
    mov word [bl_body], wb_b_cband
    mov si, wb_r_cband1
    xor al, al
    call bl_run

    mov word [bl_body], wb_b_run1
    mov si, wb_r_run1
    xor al, al
    call bl_run
    mov word [wb_ncell], WB_CELLS

    ; --- the derived row: the lettered line against the composed one --------
    call bl_blank
    mov si, wb_s_ratio
    call bl_sline
    mov ax, [wb_tbar]
    mov dx, [wb_tbar+2]
    mov bx, [wb_tband]
    mov cx, [wb_tband+2]
.fit:
    or cx, cx
    jz .fitted
    shr dx, 1
    rcr ax, 1
    shr cx, 1
    rcr bx, 1
    jmp short .fit
.fitted:
    mov cx, bx
    or cx, cx
    jz .noratio
    call bl_ratio
    call bl_lclr
    mov si, wb_r_x100
    xor di, di
    call bl_lput
    mov di, BL_C_N
    mov cx, 9
    call bl_dec
    call bl_lcommit
.noratio:

    call wb_ident
    call bl_lclr
    mov si, wb_r_ident
    xor di, di
    call bl_lput
    mov al, [wb_cf]
    or al, al
    mov si, wb_s_drawn
    jz .cf0
    mov si, wb_s_refused
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

; -----------------------------------------------------------------------------
; wb_ident - THE PICTURE THAT IS THE ASSERTION.  Three lines: the string
; lettered by the kernel, the same string composed and blitted under it, and
; the same again with 6.9.1's selection span inverted. The first two must be
; pixel-identical; the third must differ in exactly those eight cells.
; -----------------------------------------------------------------------------
wb_ident:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov cx, [wb_ix]
    mov dx, [wb_iy]
    mov si, wb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov word [wb_ncell], WB_CELLS
    mov word [wb_i0], 0
    mov word [wb_i1], 0
    call wb_b_band
    mov si, wb_band
    mov bp, WG_STRIDE
    mov ax, [wb_ix]
    mov bx, [wb_iy]
    add bx, 10
    mov cx, WB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    mov byte [wb_cf], 0
    jnc .ok
    mov byte [wb_cf], 1
.ok:
    mov word [wb_i0], WB_INV0
    mov word [wb_i1], WB_INV1
    call wb_b_band
    mov word [wb_i0], 0
    mov word [wb_i1], 0
    mov si, wb_band
    mov bp, WG_STRIDE
    mov ax, [wb_ix]
    mov bx, [wb_iy]
    add bx, 20
    mov cx, WB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "weave/wband.inc"          ; THE THING MEASURED - the package's own
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

wb_tpl:
    dw 7, 22, 632, 448
    dw wb_ttl, wb_paint, wb_onkey, wb_onclick

wb_ttl:     db 'Weave Band Bench', 0

wb_s_title: db 'WEAVEBANDBENCH - the grid band composer (apps/weave/wband.inc)', 0
wb_s_hint:  db 'Click the window, or press R, to run.', 0
wb_s_hdr:   db '-- one grid row: 79 cells of 8x8, 632 px --', 0
wb_s_ratio: db '-- the lettered row against the composed one --', 0

wb_seed:    db '  1 Item       3.5    #DIV0     22.75      -7   Total    14.5 ', 0
wb_one:     db '7', 0

wb_r_run79:  db 'FONT_RUN 79 aligned', 0
wb_r_band79: db 'WG_BAND 79 plain', 0
wb_r_bandinv: db 'WG_BAND 79 inverted', 0
wb_r_blit79: db 'BLIT1 632x8 stride90', 0
wb_r_cband79: db 'BAND 79 compose+blit', 0
wb_r_cband8: db 'BAND 8 = one column', 0
wb_r_cband1: db 'BAND 1 cell', 0
wb_r_run1:   db 'FONT_RUN 1 cell', 0
wb_r_x100:   db 'FONT_RUN/BAND x100', 0
wb_r_ident:  db 'identity blit says', 0
wb_s_drawn:  db 'DRAWN (CF=0)', 0
wb_s_refused: db 'REFUSED (CF=1)', 0

wb_win:     dw 0
wb_cx:      dw 0
wb_cy:      dw 0
wb_cw:      dw 0
wb_ch:      dw 0
wb_bx:      dw 0
wb_by:      dw 0
wb_ix:      dw 0
wb_iy:      dw 0
wb_ncell:   dw WB_CELLS
wb_i0:      dw 0
wb_i1:      dw 0
wb_tbar:    dw 0, 0
wb_tband:   dw 0, 0
wb_cf:      db 0

WB_BSS_OWN  equ WG_STRIDE * 8 + WB_CELLS + 1
WB_BSS_TOTAL equ WB_BSS_OWN + BL_BSS_SIZE + 16    ; + wband.inc's statics
                                                  ; (7 bytes), with slack;
                                                  ; wb_entry checks the sum
    OS88_BSS WB_BSS_TOTAL
    OS88_IMAGE_END

section .bss
wb_band:    resb WG_STRIDE * 8      ; the band, as the runtime's own buffer is
wb_str:     resb WB_CELLS + 1
wb_bl:      resb BL_BSS_SIZE
wb_bss_end:
section .text

    BL_BSS wb_bl
