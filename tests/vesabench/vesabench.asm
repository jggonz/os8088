; =============================================================================
; os8088 - tests/vesabench/vesabench.asm
;
; VESABENCH: where the time goes on a packed display (SPEC.md 120.8). Nothing
; here ships: `make vesabench` builds it onto a disk of its own, `all` does
; not.
;
; THE QUESTION is whether the BIOS window switch - `int 10h AX=4F05h`, once
; per 64 KB the drawing crosses - is where a VESA desktop spends its time, or
; whether it is the pixels. The plan's S7 says the direct window register for
; the Bochs VBE is taken if and only if the switch is the cost, so the rows
; are built to answer exactly that:
;
;   4F05h BH=1 get      one BIOS call that switches nothing (a READ of the
;                       window), which costs what a switch costs on the BIOS
;                       side - the whole int 10h round trip into the video
;                       ROM and back. A switch is this plus one port write
;   FILL 624x200        OSAPI_GFX_FILL of a window-sized rect: 249,600 bytes
;                       at 16bpp, so about four window crossings
;   FILL 624x1          one row - the per-call floor, no crossing
;   FILL 8x8            a cell
;   BLITD 624x16 x2     OSAPI_GFX_BLITD, 2 calls of a 16-row block: the
;                       same 400 rows at depth from a buffer, PiXEL's path
;   BLIT4 624x16 x2     OSAPI_GFX_BLIT4 of the same rows as packed nibbles:
;                       the sixteen-colour path every other package takes,
;                       a pixel decoded a nibble at a time
;   FONT_RUN 78 cells   one opaque text row, PERFORMANCE.md Part 2's row
;   bl_paint page       the report's own page of text rows - a window
;                       repaint - method T
;
; and two DERIVED rows: the window switches a 624x200 fill makes (from
; OSAPI_VIDEO's width and depth) and that many 4F05h calls as a share of the
; fill, in tenths of a percent. Above about a third, the BIOS is the cost and
; the register is worth its bytes; below it, it is the pixels.
;
; IT RUNS ITSELF AND SAVES ITSELF: the first paint runs every row and writes
; VESABNCH.TXT beside the package - so a machine with no automation socket
; (86Box) needs nobody to click, and the file is read back off the floppy
; image on the host. The vid* family's shape (benchlib's banner note).
;
; QEMU prices every call at host speed and is not a measurement of time; it
; is the correctness run. 86Box's 486 with a Cirrus 5428 is the machine the
; numbers are taken on (vm/486-vesa). Neither is an 8088 and none of this is
; for one.
; =============================================================================
    cpu 8086
    bits 16
%include "os88api.inc"
    OS88_HEADER 'VESABENCH', vb_entry

VB_W        equ 624                 ; the measured block: 78 cells wide...
VB_ROWS     equ 200                 ; ...and 200 rows for the fill - 23 ms on
                                    ; a 486 at 16bpp, under benchlib's suspect
                                    ; line (300 rows were 35 ms and flagged)
VB_BROWS    equ 16                  ; the blits: 2 blocks of 16 rows, because
VB_NBLK     equ 2                   ; the nibble path is 10x the fill a row and
                                    ; 5 blocks lapped the PIT (a lapped row is
                                    ; tick-timed, in whole 55 ms)
VB_BUFSZ    equ VB_W * 2 * VB_BROWS ; the depth block at 16bpp (8bpp uses half)
VB_B4SZ     equ VB_W / 2 * VB_BROWS ; ...and the 4bpp one
VB_CELLS    equ 78

vb_entry:
    push si
    call vb_teststr
    mov si, vb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [vb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; the content origin onto a multiple of 8
    clc
.out:
    pop si
    ret

; vb_teststr - 78 characters of the seed, wrapped, NUL
vb_teststr:
    push ax
    push cx
    push si
    push di
    mov di, vb_str
    mov si, vb_seed
    mov cx, VB_CELLS
.c:
    mov al, [si]
    or al, al
    jnz .have
    mov si, vb_seed
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

; --- the window's callbacks ---------------------------------------------------
vb_paint:                           ; SI = win. The FIRST paint runs the rows
    push ax                         ; and saves: nobody has to click
    push bx
    push cx
    push dx
    push si
    push di
    mov [vb_win], si
    cmp byte [vb_ran], 0
    jne .show
    mov byte [vb_ran], 1
    call vb_run
    mov si, vb_fname
    call bl_save
.show:
    call vb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    cmp bl, 's'
    je .save
    call bl_key
    jc .out
    call vb_repaint
    jmp short .out
.run:
    call vb_run
    call vb_repaint
    jmp short .out
.save:
    mov si, vb_fname
    call bl_save
    call vb_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vb_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [vb_win], si
    call vb_run
    call vb_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

vb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [vb_win]
    call OSAPI_WM_CONTENT
    mov [vb_cx], ax
    mov [vb_cy], dx
    mov bx, [vb_win]
    call OSAPI_WM_GEOM
    mov [vb_cw], cx
    mov [vb_ch], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [vb_cx]
    mov bx, [vb_cy]
    mov cx, ax
    add cx, [vb_cw]
    dec cx
    mov dx, bx
    add dx, [vb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [vb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the run ----------------------------------------------------------------
vb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [vb_win]
    call OSAPI_WM_CONTENT
    mov [vb_cx], ax
    mov [vb_cy], dx
    mov bx, [vb_win]
    call OSAPI_WM_GEOM
    mov [vb_cw], cx
    mov [vb_ch], dx
    mov ax, [vb_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [vb_bx], ax                 ; the block's x, on the byte grid
    mov ax, [vb_cy]
    add ax, 24                      ; ...and its top, under the first lines
    mov [vb_by], ax
    call OSAPI_VIDEO                ; AX = w, BX = h, DL = kind, DH = bpp
    mov [vb_vw], ax
    mov [vb_vh], bx
    mov [vb_kind], dl
    mov [vb_bpp], dh
    call vb_fillbufs
    call bl_blank
    mov si, vb_s_title
    call bl_sline
    mov si, vb_s_mode
    mov ax, [vb_vw]
    xor dx, dx
    mov cx, 6
    call bl_kv
    mov si, vb_s_modeh
    mov ax, [vb_vh]
    xor dx, dx
    call bl_kv
    mov si, vb_s_bpp
    mov al, [vb_bpp]
    xor ah, ah
    xor dx, dx
    call bl_kv
    mov si, vb_s_kind
    mov al, [vb_kind]
    xor ah, ah
    xor dx, dx
    call bl_kv
    call bl_head
    call bl_baseline
    mov si, vb_s_hdr
    call bl_sline

    mov word [bl_n], 200
    mov word [bl_body], vb_b_get
    mov si, vb_r_get
    xor al, al
    call bl_run
    mov ax, [bl_lastus]             ; the BIOS call, banked
    mov dx, [bl_lastus+2]
    mov [vb_tget], ax
    mov [vb_tget+2], dx

    mov word [bl_n], 4
    mov word [bl_body], vb_b_fill
    mov si, vb_r_fill
    xor al, al
    call bl_run
    mov ax, [bl_lastus]
    mov dx, [bl_lastus+2]
    mov [vb_tfill], ax
    mov [vb_tfill+2], dx

    mov word [bl_n], 50
    mov word [bl_body], vb_b_fill1
    mov si, vb_r_fill1
    xor al, al
    call bl_run

    mov word [bl_n], 50
    mov word [bl_body], vb_b_fill8
    mov si, vb_r_fill8
    xor al, al
    call bl_run

    mov word [bl_n], 2
    mov word [bl_body], vb_b_blitd
    mov si, vb_r_blitd
    xor al, al
    call bl_run
    mov ax, [bl_last]               ; CF of the LAST call: drawn or refused
    mov si, vb_r_blitdcf
    mov di, vb_s_drawn
    cmp byte [vb_cf], 0
    je .cfok
    mov di, vb_s_refused
.cfok:
    call bl_kvs

    mov word [bl_n], 2
    mov word [bl_body], vb_b_blit4
    mov si, vb_r_blit4
    xor al, al
    call bl_run

    mov word [bl_n], 8
    mov word [bl_body], vb_b_run
    mov si, vb_r_run
    xor al, al
    call bl_run

    mov word [bl_n], 2
    mov word [bl_body], vb_b_page
    mov si, vb_r_page
    mov al, 1                       ; method T: it is a whole page of rows
    call bl_run

    ; --- derived: the switches a 624x200 fill makes, and their share -------
    mov si, vb_s_hdr2
    call bl_sline
    mov ax, [vb_vw]                 ; bytes a row = w * bpp / 8
    mov cl, [vb_bpp]
    xor ch, ch
    mul cx
    mov cx, 8
    div cx                          ; AX = the stride (DX = 0)
    mov cx, VB_ROWS
    mul cx                          ; DX:AX = bytes the block spans
    add ax, 0xFFFF
    adc dx, 0                       ; ceil(/65536) = DX after the carry
    mov ax, dx
    xor dx, dx
    mov [vb_nsw], ax
    mov si, vb_r_nsw
    mov cx, 6
    call bl_kv
    mov ax, [vb_nsw]                ; nsw x 1000 (at most 24,000: one word)...
    mov cx, 1000
    mul cx
    mov cx, ax
    mov ax, [vb_tget]               ; ...x the BIOS call, in hundredths of a us,
    mov dx, [vb_tget+2]             ; over the fill's: tenths of a percent
    call bl_mul48                   ; (bl_mul48 is DX:AX x CX into the
                                    ; accumulator, not the accumulator x CX -
                                    ; two in a row kept only the second)
    mov cx, [vb_tfill]
    or cx, cx
    jz .nofill
    cmp word [vb_tfill+2], 0
    jne .big
    call bl_div48
    call bl_get32
    jmp short .share
.big:
    mov cx, 256                     ; a fill past 42 ms an iteration: scale
    call bl_div48                   ; both by 256 first
    call bl_get32
    mov bx, ax
    push bx
    mov ax, [vb_tfill]
    mov dx, [vb_tfill+2]
    mov cx, 256
    call bl_div32
    mov cx, ax
    pop ax
    xor dx, dx
    or cx, cx
    jz .nofill
    div cx
    xor dx, dx
.share:
    mov si, vb_r_share
    mov cx, 6
    call bl_kv
    jmp short .done
.nofill:
    mov si, vb_r_share
    mov di, vb_s_na
    call bl_kvs
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the bodies -------------------------------------------------------------
vb_b_get:                           ; int 10h AX=4F05h BH=1: read window A
    push bx
    push dx
    mov ax, 0x4F05
    mov bx, 0x0100
    int 0x10
    pop dx
    pop bx
    ret

vb_b_fill:
    mov al, CLGRAY
    call OSAPI_SET_COLOR
    mov ax, [vb_bx]
    mov bx, [vb_by]
    mov cx, ax
    add cx, VB_W - 1
    mov dx, bx
    add dx, VB_ROWS - 1
    call OSAPI_GFX_FILL
    ret

vb_b_fill1:
    mov al, CDGRAY
    call OSAPI_SET_COLOR
    mov ax, [vb_bx]
    mov bx, [vb_by]
    mov cx, ax
    add cx, VB_W - 1
    mov dx, bx
    call OSAPI_GFX_FILL
    ret

vb_b_fill8:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [vb_bx]
    mov bx, [vb_by]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
    ret

vb_b_blitd:                         ; 2 blocks of 16 rows at depth
    push bp
    mov bx, [vb_by]
    mov [vb_y], bx
    mov cx, VB_NBLK
.b:
    push cx
    mov si, vb_buf
    mov bp, VB_W * 2
    cmp byte [vb_bpp], 8
    jne .s
    mov bp, VB_W
.s:
    mov ax, [vb_bx]
    mov bx, [vb_y]
    mov cx, VB_W
    mov dx, VB_BROWS
    push ds
    pop es
    call OSAPI_GFX_BLITD
    sbb al, al
    neg al
    mov [vb_cf], al                 ; 1 = refused
    add word [vb_y], VB_BROWS
    pop cx
    loop .b
    pop bp
    ret

vb_b_blit4:                         ; the same rows as packed nibbles
    push bp
    mov bx, [vb_by]
    mov [vb_y], bx
    mov cx, VB_NBLK
.b:
    push cx
    mov si, vb_buf4
    mov bp, VB_W / 2
    mov ax, [vb_bx]
    mov bx, [vb_y]
    mov cx, VB_W
    mov dx, VB_BROWS
    push ds
    pop es
    call OSAPI_GFX_BLIT4
    add word [vb_y], VB_BROWS
    pop cx
    loop .b
    pop bp
    ret

vb_b_run:
    mov cx, [vb_bx]
    mov dx, [vb_by]
    mov si, vb_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

vb_b_page:
    mov si, [vb_win]
    call bl_paint
    ret

; vb_fillbufs - the depth block (a gradient of 565 words, or of bytes 16..255
; at 8bpp) and the 4bpp block (a nibble ramp): something a run coalescer
; cannot flatten
vb_fillbufs:
    push ax
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    mov di, vb_buf
    mov cx, VB_W * VB_BROWS
    xor bx, bx
    cld
    cmp byte [vb_bpp], 8
    je .b8
.w:
    mov ax, bx
    stosw
    add bx, 0x0841                  ; a step in every gun
    loop .w
    jmp short .four
.b8:
    mov al, 16
.b:
    stosb
    inc al
    jnz .b2
    mov al, 16
.b2:
    loop .b
.four:
    mov di, vb_buf4
    mov cx, VB_B4SZ
    mov al, 0x01
.n:
    stosb
    add al, 0x23
    loop .n
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

%define BL_INTRO_HOW 'it saves itself, VESABNCH.TXT'
%include "benchlib.inc"

vb_tpl:
    dw 7, 22, 632, 448
    dw vb_ttl, vb_paint, vb_onkey, vb_onclick

vb_ttl:     db 'VESA Bench', 0
vb_fname:   db 'VESABNCH.TXT', 0
vb_s_title: db 'VESABENCH - the packed display: is the BIOS window switch the cost?', 0
vb_s_mode:  db 'display width', 0
vb_s_modeh: db 'display height', 0
vb_s_bpp:   db 'bits per pixel', 0
vb_s_kind:  db 'adapter kind (4=VESA)', 0
vb_s_hdr:   db '-- the rows: one BIOS call, fills, the two blits, text --', 0
vb_s_hdr2:  db '-- derived: a 624x200 fill crosses this many 64KB windows --', 0
vb_seed:    db 'The quick brown fox jumps over 13 lazy dogs; Pack my box with 5 jugs. ', 0
vb_r_get:   db '4F05h BH=1 get', 0
vb_r_fill:  db 'FILL 624x200', 0
vb_r_fill1: db 'FILL 624x1', 0
vb_r_fill8: db 'FILL 8x8', 0
vb_r_blitd: db 'BLITD 624x16 x2', 0
vb_r_blitdcf: db 'BLITD says', 0
vb_r_blit4: db 'BLIT4 624x16 x2', 0
vb_r_run:   db 'FONT_RUN 78 cells', 0
vb_r_page:  db 'bl_paint page', 0
vb_r_nsw:   db 'window switches, est', 0
vb_r_share: db '4F05h/FILL share, 0.1%', 0
vb_s_drawn: db 'DRAWN (CF=0)', 0
vb_s_refused: db 'REFUSED (CF=1)', 0
vb_s_na:    db 'n/a', 0
vb_win:     dw 0
vb_cx:      dw 0
vb_cy:      dw 0
vb_cw:      dw 0
vb_ch:      dw 0
vb_bx:      dw 0
vb_by:      dw 0
vb_y:       dw 0
vb_vw:      dw 0
vb_vh:      dw 0
vb_nsw:     dw 0
vb_tget:    dw 0, 0
vb_tfill:   dw 0, 0
vb_kind:    db 0
vb_bpp:     db 0
vb_cf:      db 0
vb_ran:     db 0

VB_BSS_OWN  equ VB_BUFSZ + VB_B4SZ + VB_CELLS + 1 + 512
    align 512
    OS88_BSS VB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END
vb_buf      equ os88_image_end + 0
vb_buf4     equ os88_image_end + VB_BUFSZ
vb_str      equ os88_image_end + VB_BUFSZ + VB_B4SZ
    BL_BSS os88_image_end + ((VB_BUFSZ + VB_B4SZ + VB_CELLS + 1 + 511) / 512) * 512
