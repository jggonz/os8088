; =============================================================================
; os8088 - tests/rcband/rcbandbench.asm
;
; RCBANDBENCH: what does RUNCPM's row composer cost? apps/runcpm/rcband.inc's
; rc_band turns a run of terminal cells into a 1bpp band that ONE
; OSAPI_GFX_BLIT1 puts down (SPEC.md 71.2), and every number in that section's
; cost model rests on what a cell of that compose costs. PERFORMANCE.md rule 4
; says measure before quoting, and this is the measurement: the SAME
; rcband.inc the package ships is %included here and timed with
; tests/benchlib.inc, on the same harness that priced the proportional band
; (tests/bandbench, PERFORMANCE.md Set 64). Nothing here ships: `make
; rcbandbench` builds build/rcband.img and `all` does not.
;
;   make rcbandbench
;   make test TESTAPPS=build/rcband.img QEMU="qemu-system-i386 -icount shift=3,sleep=off"
;   ...double-click Disk B, RCBANDBENCH, click the window; read the counts
;   column: one -icount count is 0.359 ms of real XT (PERFORMANCE.md Part 4)
;
; THE ROWS, and why each:
;
;   FONT_RUN 79 aligned      the bar - the same 79-cell line lettered by the
;                            kernel, taken in the same run (Set 64's rule)
;   RC_BAND 79 (shipping)    the compose alone, drawing nothing: the number
;                            SPEC.md 71.2's model quotes per cell
;   RC_BAND 79 (v1 loop)     the FIRST version of that loop - `loop` over
;                            eight rows with the stride added from memory and
;                            the attribute bit recomputed with two `shr ..,cl`
;                            a cell - kept here verbatim so the record shows
;                            what the rewrite was worth. It is not in the
;                            package.
;   BLIT1 632x8 stride 80    the emit alone, the band's real stride
;   BAND 79 = compose+blit   what a changed row actually pays
;   BAND 1 cell              a cursor cell: the floor of the cheapest row draw
;   FONT_RUN 1 cell          ...against the run it replaced (SPEC.md 71.2:
;                            the band is the first choice at EVERY n)
;   BAND 11 cells            the same-row cursor span, against two 1-cell
;                            bands - the crossover 71.2 states
;
; AND THE IDENTITY ROWS, a correctness test in a benchmark's clothes: the
; string lettered by FONT_RUN, the same string composed by rc_band and blitted
; directly under it (the same pixels or the composer is wrong), and a third
; line with every eighth cell reverse-video so the attribute cursor's byte
; wrap - the one thing a plain-text screendump would never exercise - is on
; the glass. A screendump is the assertion.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'RCBANDBENCH', rb_entry

; rcband.inc opens `section .bss` for its statics; a plain package keeps
; everything in .text and bases its bss on os88_image_end. So .bss is declared
; HERE, first, as align=1 following .text: it then starts exactly at
; os88_image_end and rcband's seven bytes and this harness's own bss share it.
; A cross-section subtraction cannot be assembled (crt0.asm says why), so
; OS88_BSS is a stated constant and rb_entry checks it at run time.
section .bss align=1 follows=.text
section .text

RB_CELLS    equ 79                ; the line: 79 cells, 632 px - a framed
                                  ; 640-px window's whole content (71.2)
RB_W        equ RB_CELLS * 8
RB_STRIDE   equ 80                ; rc_band's stride: RC_COLS, always
RB_N        equ 8                 ; iterations a row (bandbench's)
RB_SPAN     equ 11                ; the same-row cursor span of SPEC.md 71.2

; -----------------------------------------------------------------------------
; rb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
rb_entry:
    push si
    mov ax, rb_bss_end              ; the layout the header promised: every
    cmp ax, os88_image_end + RB_BSS_TOTAL   ; bss byte inside what the loader
    ja .refuse                      ; zeroes and the region holds
    call rb_teststr
    call rb_bank                    ; rc_band's glyph statics, once, outside
                                    ; the timed rows
    call rb_hint
    mov si, rb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [rb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; rb_bank - one rc_band of one cell, so OSAPI_FONT_GLYPHS is asked before any
; row is timed (rc_band asks on its first call and banks the answer)
rb_bank:
    push ax
    mov ax, 1
    push ax                         ; n
    xor ax, ax
    push ax                         ; first
    mov ax, rb_attr0
    push ax                         ; attr
    mov ax, rb_str
    push ax                         ; chars
    mov ax, rb_band
    push ax                         ; dst
    call _rc_band
    add sp, 10
    pop ax
    ret

; -----------------------------------------------------------------------------
; rb_teststr - RB_CELLS characters of ordinary prose (bandbench's seed), and
;              the attribute rows: all plain, and every eighth cell reversed
; -----------------------------------------------------------------------------
rb_teststr:
    push ax
    push cx
    push si
    push di
    mov di, rb_str
    mov cx, RB_CELLS
    mov si, rb_seed
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, rb_seed
    mov al, [si]
.have:
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [di], 0
    mov di, rb_attr0
    mov cx, 10
.a:
    mov byte [di], 0
    mov byte [di+10], 0x81          ; cells 0 and 7 of every eight: the bit
    inc di                          ; that starts a byte and the one that
    loop .a                         ; wraps it
    pop di
    pop si
    pop cx
    pop ax
    ret

rb_hint:
    push si
    call bl_blank
    mov si, rb_s_title
    call bl_sline
    call bl_head
    mov si, rb_s_hint
    call bl_sline
    pop si
    ret

rb_paint:
    call bl_paint
    ret

rb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [rb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call rb_run
    call rb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [rb_win], si
    call rb_run
    call rb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [rb_win]
    call OSAPI_WM_CONTENT
    mov [rb_cx], ax
    mov [rb_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [rb_cx]
    mov bx, [rb_cy]
    mov cx, ax
    add cx, [rb_cw]
    dec cx
    mov dx, bx
    add dx, [rb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [rb_win]
    call bl_paint
    call rb_ident
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the measured bodies - each draws at ([rb_bx], [rb_by]), the last content
; rows, so nothing scribbles over a result line
; =============================================================================

; --- the bar: 79 cells of 8x8, byte-aligned ----------------------------------
rb_b_run79:
    mov cx, [rb_bx]
    mov dx, [rb_by]
    mov si, rb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; --- rc_band(rb_band, rb_str, rb_attr0, 0, n): the shipping compose ----------
; in: [rb_ncell] = n
rb_b_band:
    push word [rb_ncell]
    xor ax, ax
    push ax
    mov ax, rb_attr0
    push ax
    mov ax, rb_str
    push ax
    mov ax, rb_band
    push ax
    call _rc_band
    add sp, 10
    ret

; --- the same, through the v1 loop kept below --------------------------------
rb_b_bandv1:
    push word [rb_ncell]
    xor ax, ax
    push ax
    mov ax, rb_attr0
    push ax
    mov ax, rb_str
    push ax
    mov ax, rb_band
    push ax
    call rb_band_v1
    add sp, 10
    ret

; --- BLIT1: the emit alone, [rb_ncell]*8 x 8 at stride 80 --------------------
rb_b_blit:
    mov si, rb_band
    mov bp, RB_STRIDE
    mov ax, [rb_bx]
    mov bx, [rb_by]
    mov cx, [rb_ncell]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    ret

; --- BAND: compose then emit -------------------------------------------------
rb_b_cband:
    call rb_b_band
    call rb_b_blit
    ret

; --- FONT_RUN of one cell ----------------------------------------------------
rb_b_run1:
    mov cx, [rb_bx]
    mov dx, [rb_by]
    mov si, rb_one
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; rb_band_v1 - THE FIRST VERSION of rc_band, verbatim from the wave-1 build
; before review (the same cdecl, the same statics), so the record carries what
; the rewrite was worth. Stride n, `loop` over the eight rows, the attribute
; bit recomputed per cell. NOT in the package.
; -----------------------------------------------------------------------------
rb_band_v1:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [rc_gseg]
    mov di, [bp+4]
    mov si, [bp+6]
    mov bx, [bp+10]
    mov cx, [bp+12]
    jcxz .out
.cell:
    push cx
    mov ax, bx
    mov cl, 3
    shr ax, cl
    push si
    mov si, [bp+8]
    add si, ax
    mov ah, [si]
    pop si
    mov cx, bx
    and cl, 7
    mov al, 0x80
    shr al, cl
    and al, ah
    mov ah, 0xFF
    jz .plain
    mov ah, 0
.plain:
    mov al, [si]
    inc si
    inc bx
    sub al, [rc_gfirst]
    jb .blank
    cmp al, [rc_gspan]
    ja .blank
    push ax
    mov ah, 0
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [rc_goff]
    mov dx, ax
    pop ax
    push di
    push bx
    mov bx, dx
    mov cx, 8
.row:
    mov al, [es:bx]
    xor al, ah
    mov [di], al
    inc bx
    add di, [bp+12]
    loop .row
    pop bx
    pop di
    jmp short .next
.blank:
    push di
    mov cx, 8
.brow:
    mov [di], ah
    add di, [bp+12]
    loop .brow
    pop di
.next:
    inc di
    pop cx
    loop .cell
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; rb_ident - the correctness rows: FONT_RUN, the same line through rc_band +
;            BLIT1 under it, and the every-eighth-cell-reversed line under that
; in: gfx lock held
; -----------------------------------------------------------------------------
rb_ident:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov cx, [rb_ix]
    mov dx, [rb_iy]
    mov si, rb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN

    mov word [rb_ncell], RB_CELLS
    call rb_b_band
    mov si, rb_band
    mov bp, RB_STRIDE
    mov ax, [rb_ix]
    mov bx, [rb_iy]
    add bx, 10
    mov cx, RB_W
    mov dx, 8
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    mov byte [rb_cf], 0
    jnc .ok
    mov byte [rb_cf], 1
.ok:
    ; the reversed line: attr = rb_attr0 + 10 (0x81 a byte), first = 0
    mov ax, RB_CELLS
    push ax
    xor ax, ax
    push ax
    mov ax, rb_attr0 + 10
    push ax
    mov ax, rb_str
    push ax
    mov ax, rb_band
    push ax
    call _rc_band
    add sp, 10
    mov si, rb_band
    mov bp, RB_STRIDE
    mov ax, [rb_ix]
    mov bx, [rb_iy]
    add bx, 20
    mov cx, RB_W
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

; -----------------------------------------------------------------------------
; rb_run - every row, in order, into the report
; -----------------------------------------------------------------------------
rb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [rb_win]
    call OSAPI_WM_CONTENT
    mov [rb_cx], ax
    mov [rb_cy], dx
    mov bx, [rb_win]
    call OSAPI_WM_GEOM
    mov [rb_cw], cx
    mov [rb_ch], dx
    mov ax, [rb_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [rb_bx], ax
    mov [rb_ix], ax
    mov ax, [rb_cy]
    add ax, [rb_ch]
    sub ax, 13
    mov [rb_by], ax
    sub ax, 32
    mov [rb_iy], ax

    call bl_blank
    mov si, rb_s_title
    call bl_sline
    call bl_head
    call bl_baseline

    mov si, rb_s_hdr
    call bl_sline

    mov word [bl_n], RB_N
    mov word [rb_ncell], RB_CELLS

    mov word [bl_body], rb_b_run79
    mov si, rb_r_run79
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [rb_tbar], ax
    mov [rb_tbar+2], dx

    mov word [bl_body], rb_b_band
    mov si, rb_r_band79
    xor al, al
    call bl_run

    mov word [bl_body], rb_b_bandv1
    mov si, rb_r_bandv1
    xor al, al
    call bl_run

    mov word [bl_body], rb_b_blit
    mov si, rb_r_blit79
    xor al, al
    call bl_run

    mov word [bl_body], rb_b_cband
    mov si, rb_r_cband79
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [rb_tband], ax
    mov [rb_tband+2], dx

    mov word [rb_ncell], 1
    mov word [bl_body], rb_b_band
    mov si, rb_r_band1
    xor al, al
    call bl_run

    mov word [bl_body], rb_b_cband
    mov si, rb_r_cband1
    xor al, al
    call bl_run

    mov word [bl_body], rb_b_run1
    mov si, rb_r_run1
    xor al, al
    call bl_run

    mov word [rb_ncell], RB_SPAN
    mov word [bl_body], rb_b_cband
    mov si, rb_r_cband11
    xor al, al
    call bl_run
    mov word [rb_ncell], RB_CELLS

    ; --- the derived row: the lettered line against the composed one --------
    call bl_blank
    mov si, rb_s_ratio
    call bl_sline
    mov ax, [rb_tbar]
    mov dx, [rb_tbar+2]
    mov bx, [rb_tband]
    mov cx, [rb_tband+2]
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
    mov si, rb_r_x100
    xor di, di
    call bl_lput
    mov di, BL_C_N
    mov cx, 9
    call bl_dec
    call bl_lcommit
.noratio:

    call rb_ident
    call bl_lclr
    mov si, rb_r_ident
    xor di, di
    call bl_lput
    mov al, [rb_cf]
    or al, al
    mov si, rb_s_drawn
    jz .cf0
    mov si, rb_s_refused
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

%include "runcpm/rcband.inc"        ; THE THING MEASURED - the package's own
%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

rb_tpl:
    dw 7, 22, 632, 448
    dw rb_ttl, rb_paint, rb_onkey, rb_onclick

rb_ttl:     db 'RC Band Bench', 0

rb_s_title: db 'RCBANDBENCH - the RUNCPM row composer (apps/runcpm/rcband.inc)', 0
rb_s_hint:  db 'Click the window, or press R, to run.', 0
rb_s_hdr:   db '-- the line: 79 cells of 8x8, 632 px --', 0
rb_s_ratio: db '-- the lettered line against the composed one --', 0

rb_seed:    db 'The quick brown fox jumps over 13 lazy dogs; Pack my box with 5 jugs. ', 0
rb_one:     db 'X', 0

rb_r_run79: db 'FONT_RUN 79 aligned', 0
rb_r_band79: db 'RC_BAND 79 shipping', 0
rb_r_bandv1: db 'RC_BAND 79 v1 loop', 0
rb_r_blit79: db 'BLIT1 632x8 stride80', 0
rb_r_cband79: db 'BAND 79 compose+blit', 0
rb_r_band1: db 'RC_BAND 1 cell', 0
rb_r_cband1: db 'BAND 1 compose+blit', 0
rb_r_run1:  db 'FONT_RUN 1 cell', 0
rb_r_cband11: db 'BAND 11 compose+blit', 0
rb_r_x100:  db 'FONT_RUN/BAND x100', 0
rb_r_ident: db 'identity blit says', 0
rb_s_drawn: db 'DRAWN (CF=0)', 0
rb_s_refused: db 'REFUSED (CF=1)', 0

rb_win:     dw 0
rb_cx:      dw 0
rb_cy:      dw 0
rb_cw:      dw 0
rb_ch:      dw 0
rb_bx:      dw 0
rb_by:      dw 0
rb_ix:      dw 0
rb_iy:      dw 0
rb_ncell:   dw RB_CELLS
rb_tbar:    dw 0, 0
rb_tband:   dw 0, 0
rb_cf:      db 0

RB_BSS_OWN  equ RB_STRIDE * 8 + RB_CELLS + 1 + 20
RB_BSS_TOTAL equ RB_BSS_OWN + BL_BSS_SIZE + 16    ; + rcband.inc's statics
                                                  ; (7 bytes), with slack;
                                                  ; rb_entry checks the sum
    OS88_BSS RB_BSS_TOTAL
    OS88_IMAGE_END

section .bss
rb_band:    resb RB_STRIDE * 8      ; the 80x8 band, as rc_bandbuf is
rb_str:     resb RB_CELLS + 1
rb_attr0:   resb 20                 ; 10 attribute bytes plain, 10 reversed
rb_bl:      resb BL_BSS_SIZE
rb_bss_end:
section .text

    BL_BSS rb_bl
