; =============================================================================
; os8088 - apps/pixel/pxfull.asm
;
; PiXEL's FULL-SCREEN PART (SPEC.md 106.23): part 6 of PIXEL.O88, LINKED
; against the package (build/pxlink.inc) like the SIMPLE and FOLDER parts -
; every mode's renderer, the colours each mode shows (the median cut, the
; plan search over any colours, the CGA's palette chooser), the diffuser,
; the lettering and the input poll of the full screen. The resident fetches
; it before OSAPI_FSX_RUN, calls it from the bracket's loop and drops it on
; the way out; it keeps the hidden decode (a next picture), which is
; resident code, and the commit.
;
; One vector, DECODE, with a VERB in CL (apps/pixel/pxfs.inc):
;
;   FSV_ENTER  CH = a PXM_*: the mode set (or the same-mode Desktop's rect),
;              its colours, the view at Fit, the captions raised, a whole
;              render owed. CF = 1: the kernel refused the mode
;   FSV_SHOW   the picture [px_fsrec] is another: its colours, Fit, a render
;   FSV_POLL   the keys, the mouse, Alt+Enter; a zoom or a pan done here; a
;              slice of any render owed (until a key waits). AX = an FSA_*
;   FSV_PROG   DI = 0: the bottom row's progress line, where the worker's
;              rows put it (drawn when its percent moved); FFFFh: off
;   FSV_MSG    DI = a line in the package: the bottom band says it
;
; DS IS THE PART inside every routine below `pxf_decode` (push cs / pop ds,
; SPEC.md 106.5's rule 1: the part's OWN data), and the package is reached
; through [PXP_PKG] - stamped by the resident before every call, never kept
; past one. The master is re-read at every row, as the window's renderer
; re-reads it (106.8): a compaction may move it between two calls.
;
; tools/pixelsim.py's FsView / FsPic are this file in Python, rule for rule,
; and tests/pxfsx.py holds the two to the pixel.
; =============================================================================

%include "pxpart.inc"
%include "os88api.inc"
%include "pxrec.inc"
%include "pxfs.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pxf_init, pxf_decode, pxf_none, pxf_none, pxf_none

PXF_WMAX    equ 720                 ; the widest screen (a Hercules)
PXF_BANDN   equ 16                  ; the Desktop's rows a BLITP
PXF_PLW     equ 80                  ; a 16-colour row's bytes a plane
PXH_HIST    equ 0                   ; (pxmaster.inc's: the histogram's dword
                                    ; counts at the master's tail)

; PXV_INIT - out AX = PXP_PROBE
pxf_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO, PXV_HEAD, PXV_PLANS - not this part's
pxf_none:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_DECODE - CL = a verb (FSV_*), CH and DI its argument; DS = the package.
; out CF/AX as the verb says. Preserves all but AX
pxf_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    cmp word [8], PXL_IMAGE         ; THE STAMP (SPEC.md 106.20)
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    cld
    mov ax, [px_fsrec]              ; what every verb reads of the package,
    mov [cs:pxf_rec], ax            ; banked into the part's own
    mov al, [px_dither]
    mov [cs:pxf_dith], al
    mov al, [px_slon]
    mov [cs:pxf_slon], al
    mov al, [px_fskind]
    mov [cs:pxf_vkind], al
    push cs
    pop ds
    cmp cl, FSV_POLL                ; the frame's own verb first
    je .poll
    cmp cl, FSV_ENTER
    je .enter
    cmp cl, FSV_SHOW
    je .show
    cmp cl, FSV_PROG
    je .prog
    cmp cl, FSV_MSG
    je .msg
    mov ax, PXE_NOTSUP
    stc
    jmp short .out
.poll:
    call pxf_poll
    clc
    jmp short .out
.enter:
    call pxf_enter
    jmp short .out
.show:
    call pxf_show
    clc
    jmp short .out
.prog:
    call pxf_prog
    clc
    jmp short .out
.msg:
    call pxf_msg
    clc
    jmp short .out
.link:
    mov ax, PXD_LINK
    stc
.out:
    pop ds
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; =============================================================================
; ENTERING, ANOTHER PICTURE
; =============================================================================

; pxf_enter - CH = the mode: set it, its colours, Fit, the captions, a whole
; render owed. CF = 1 AX = 1 refused (nothing changed on the glass)
pxf_enter:
    mov [pxf_mode], ch
    mov bl, ch
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [pxf_geo + bx]
    mov [pxf_W], ax
    mov ax, [pxf_geo + bx + 2]
    mov [pxf_H], ax
    mov ax, [pxf_geo + bx + 4]
    mov [pxf_an], ax
    mov ax, [pxf_geo + bx + 6]
    mov [pxf_ad], ax
    call far OSAPI_FONT_GLYPHS      ; DX:SI the 8x8 table, AL..AH its codes
    mov [pxf_font], si
    mov [pxf_font + 2], dx
    mov [pxf_gfirst], ax
    mov byte [pxf_jon], 0
    mov byte [pxf_bup], 0
    mov byte [pxf_bdue], 0
    mov word [pxf_pct], 0xFFFF
    mov byte [pxf_p4], 0
    mov byte [pxf_bandn], 0
    cmp byte [pxf_mode], PXM_DESK
    je .desk
    mov bl, [pxf_mode]              ; THE MODE (SPEC.md 53.4): the kernel's
    xor bh, bh                      ; FSX id, the block into the part
    mov al, [pxf_fsxid + bx]
    push ds
    pop es
    mov di, pxf_fsi
    call far OSAPI_FSX_MODE
    jnc .set
    mov ax, 1
    stc
    ret
.set:
    mov ax, [pxf_fsi + FSI_SEG]
    mov [pxf_seg], ax
    cmp byte [pxf_mode], PXM_C160
    jne .seed
    call pxf_c160
    jmp short .seed
.desk:
    call far OSAPI_FSX_SURF         ; THE SAME-MODE BRACKET's rect (53.7.1)
    jnc .surf
    mov ax, 1
    stc
    ret
.surf:
    mov [pxf_sx], ax
    mov [pxf_sy], bx
    mov [pxf_W], cx
    mov [pxf_H], dx
    cmp dx, 350                     ; the EGA's 35/48; a 480-row desktop 1/1
    je .probe
    mov word [pxf_an], 1
    mov word [pxf_ad], 1
.probe:
    push di                         ; may the planes go straight on? (DI bit
    mov di, 0x8000 | PXF_PLW        ; 15 is BLITP's probe, nothing drawn)
    call far OSAPI_GFX_BLITP
    pop di
    mov al, 0
    adc al, 0
    mov [pxf_p4], al                ; 1 = refused: packed pairs and BLIT4
.seed:
    mov byte [pxf_altdn], 1         ; OS88_ALTENTER_SEED's thought: the press
    call far OSAPI_MOUSE            ; that came in is still down, and so is
    mov [pxf_btn], al               ; any button
    mov ax, [pxf_W]                 ; the keys' line: the longest that fits
    mov si, pxf_hint80
    cmp ax, 640
    jae .h
    mov si, pxf_hint40
    cmp ax, 320
    jae .h
    mov si, pxf_hint20
.h:
    mov [pxf_hint], si
    call far OSAPI_GET_TICKS
    mov [pxf_bt0], ax
    or byte [pxf_bup], 1            ; the top band up
    call pxf_show
    clc
    ret

; pxf_show - the picture [pxf_rec] shown: its colours, Fit, its name in the
; bottom band, a whole render owed
pxf_show:
    call pxf_colours
    xor ax, ax                      ; Fit
    xor dx, dx
    call pxf_zoom
    mov word [pxf_pct], 0xFFFF      ; (a next picture's line: it is here)
    call pxf_capname
    call far OSAPI_GET_TICKS
    mov [pxf_bt1], ax
    or byte [pxf_bup], 2
    call pxf_jfull
    ret

; pxf_c160 - the CGA's 160x100 in sixteen colours (Clear Skies' cs_c160_mode,
; SPEC.md 88.15.2): six 6845 writes, a black border, every cell the right
; half block on black, with the video off while it is done
pxf_c160:
    mov dx, 0x3D8                   ; 80 columns, blink OFF, video OFF
    mov al, 0x01
    out dx, al
    mov si, pxf_c160crt
    mov cx, 6
    mov dx, 0x3D4
.r:
    lodsw
    out dx, al
    inc dx
    xchg al, ah
    out dx, al
    dec dx
    loop .r
    mov dx, 0x3D9
    xor al, al
    out dx, al
    mov es, [pxf_seg]
    xor di, di
    mov ax, 0x00DE
    mov cx, 8000
    rep stosw
    mov dx, 0x3D8                   ; ...and the picture on
    mov al, 0x09
    out dx, al
    ret

; =============================================================================
; THE COLOURS A MODE SHOWS (SPEC.md 106.23; pixelsim's FsPic)
; =============================================================================

; pxf_colours - the tables for the picture and the mode, and the card's
; palette: the DAC (256 and the adaptive sixteen) or the BIOS's (320x200)
pxf_colours:
    call pxf_getpal                 ; the palette and the counts into the part
    mov al, [pxf_mode]
    cmp al, PXM_VGA13
    jbe .c256
    cmp al, PXM_CGA640
    jae .mono
    mov byte [pxf_kind], 1          ; --- a PLAN mode ---------------------------
    cmp al, PXM_VGA12
    jne .n12
    call pxf_mcut
    call pxf_cols8to6
    call pxf_vgapal
    jmp short .plans
.n12:
    cmp al, PXM_CGA320
    jne .fixed
    call pxf_mcut
    call pxf_cgapick
    call pxf_cgapal
    jmp short .plans
.fixed:                             ; C160, the Desktop: the RGBI sixteen
    mov si, pxf_cga6
    mov di, pxf_c6
    mov cx, 48
    push ds
    pop es
    rep movsb
    mov word [pxf_ncol], 16
.plans:
    call pxf_plans
    call pxf_gl
    ret
.c256:
    mov byte [pxf_kind], 0          ; --- 256: the DAC IS the palette --------
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, pxf_pal
    mov cx, 768
.d:
    lodsb
    shr al, 1
    shr al, 1
    mov [si - 1 + pxf_p6 - pxf_pal], al
    out dx, al
    loop .d
    mov si, pxf_p6                  ; ground and ink over all 256
    mov cx, 256
    call pxf_glof
    ret
.mono:
    mov byte [pxf_kind], 2          ; --- two: the window's 1bpp luma ---------
    mov si, pxf_pal
    xor bx, bx
.m:
    call pxf_l1                     ; AX = L, DL = t
    sub al, 128
    mov [pxf_Tb + bx], al
    mov [pxf_t + bx], dl
    mov byte [pxf_c1 + bx], 0
    mov byte [pxf_c2 + bx], 1
    add si, 3
    inc bx
    cmp bx, 256
    jb .m
    mov byte [pxf_gnd], 0
    mov byte [pxf_light], 1
    ret

; pxf_l1 - DS:SI = an 8-bit colour: AX = L = (Y + (Y^2 + 127) div 255) >> 1,
; Y its luma, and DL = (64 L + 127) div 255 (106.11's 1bpp threshold).
; Preserves all but AX, DX
pxf_l1:
    push bx
    push cx
    mov al, [si]
    mov bl, 77
    mul bl
    mov bx, ax
    mov al, [si + 1]
    mov cl, 150
    mul cl
    add bx, ax
    mov al, [si + 2]
    mov cl, 29
    mul cl
    add ax, bx
    mov al, ah
    xor ah, ah                      ; Y
    mov bx, ax
    mul ax
    add ax, 127
    adc dx, 0
    mov cx, 255
    div cx
    add ax, bx
    shr ax, 1                       ; L
    push ax
    mov dx, 64
    mul dx
    add ax, 127
    adc dx, 0
    div cx
    mov dl, al                      ; t
    pop ax
    pop cx
    pop bx
    ret

; pxf_getpal - [px_pal] (768 bytes) and the master's histogram counts (256
; dwords, at its tail, SPEC.md 106.20) into the part, and the picture's
; master size. Preserves BP
pxf_getpal:
    push ds
    mov ds, [cs:PXP_PKG]
    mov si, px_pal
    push cs
    pop es
    mov di, pxf_pal
    mov cx, 384
    rep movsw
    xor bx, bx                      ; ...and as three planes of 256, the
    mov si, pxf_pal                 ; cut's to read a channel by
.ch:
    mov al, [cs:si]
    mov [cs:pxf_pch + bx], al
    mov al, [cs:si + 1]
    mov [cs:pxf_pch + 256 + bx], al
    mov al, [cs:si + 2]
    mov [cs:pxf_pch + 512 + bx], al
    add si, 3
    inc bx
    cmp bx, 256
    jb .ch
    mov bx, [cs:pxf_rec]
    mov ax, [bx + PXR_MW]
    mov [cs:pxf_mw], ax
    mov cx, [bx + PXR_MH]
    mov [cs:pxf_mh], cx
    mul cx                          ; DX:AX = the pixels
    mov [cs:pxf_npx], ax
    mov [cs:pxf_npx + 2], dx
    add ax, 15                      ; the tail: their paragraphs past the base
    adc dx, 0
    mov cx, 4
.p:
    shr dx, 1
    rcr ax, 1
    loop .p
    add ax, [bx + PXR_MSEG]
    mov ds, ax
    xor si, si
    mov di, pxf_cnt
    mov cx, 512
    rep movsw
    pop ds
    ret

; pxf_gl - the plan modes' ground and ink: the darkest and the lightest of
; the [pxf_ncol] colours at pxf_c6
pxf_gl:
    mov si, pxf_c6
    mov cx, [pxf_ncol]
    ; fall through
; pxf_glof - SI = CX colours of three 6-bit bytes: [pxf_gnd] the index of
; the darkest (77 r + 150 g + 29 b), [pxf_light] of the lightest, the
; lowest index on a tie
pxf_glof:
    xor bx, bx                      ; BX = the index
    mov word [pxf_lmin], 0xFFFF
    mov word [pxf_lmax], 0
    mov byte [pxf_gnd], 0
    mov byte [pxf_light], 0
.l:
    push cx
    mov al, [si]
    mov cl, 77
    mul cl
    mov dx, ax
    mov al, [si + 1]
    mov cl, 150
    mul cl
    add dx, ax
    mov al, [si + 2]
    mov cl, 29
    mul cl
    add dx, ax
    pop cx
    cmp dx, [pxf_lmin]
    jae .n1
    mov [pxf_lmin], dx
    mov [pxf_gnd], bl
.n1:
    cmp dx, [pxf_lmax]
    jbe .n2
    mov [pxf_lmax], dx
    mov [pxf_light], bl
.n2:
    cmp bx, 0                       ; (the first entry sets both)
    jne .n3
    mov [pxf_lmax], dx
.n3:
    add si, 3
    inc bx
    loop .l
    ret

; pxf_cols8to6 - the cut's colours (pxf_cols, 8-bit) as the DAC's six bits
pxf_cols8to6:
    push ds
    pop es
    mov di, pxf_c6                  ; (past the cut's colours: black)
    mov cx, 24
    xor ax, ax
    rep stosw
    mov si, pxf_cols
    mov di, pxf_c6
    mov cx, [pxf_ncol]
    mov ax, cx
    shl cx, 1
    add cx, ax
    push ds
    pop es
.c:
    lodsb
    shr al, 1
    shr al, 1
    stosb
    loop .c
    ret

; pxf_vgapal - 640x480's sixteen: the IDENTITY attribute map and DAC 0..15
; (Gorillas' gr_vgapalette), the graphics controller left as a write wants it
pxf_vgapal:
    mov dx, 0x3DA
    in al, dx                       ; the attribute flip-flop to "index"
    mov dx, 0x3C0
    xor al, al
.a:
    out dx, al                      ; index n...
    out dx, al                      ; ...holds n
    inc al
    cmp al, 16
    jb .a
    mov al, 0x20                    ; the palette back to the screen
    out dx, al
    mov dx, 0x3C8
    xor al, al
    out dx, al
    inc dx
    mov si, pxf_c6
    mov cx, 48
.d:
    lodsb                           ; (past [pxf_ncol] the cut left zeroes:
    out dx, al                      ; black)
    loop .d
    mov dx, 0x3CE
    mov ax, 0x0001                  ; set/reset off
    out dx, ax
    mov ax, 0x0003                  ; no rotate, replace
    out dx, ax
    mov ax, 0x0005                  ; write mode 0
    out dx, ax
    mov ax, 0xFF08                  ; every bit
    out dx, ax
    ret

; =============================================================================
; THE ADAPTIVE COLOURS: a median cut over the palette, weighted
; =============================================================================

; pxf_mcut - the used entries as points weighted by their pixels (each count
; shifted right until the picture's pixels would be under 32,768, at least
; 1); the box with the largest weight x weighted range cut on that channel
; at its weighted MEAN, entries at or below it first, until there are
; sixteen or none can be cut; each box's weighted mean, rounded. Out
; pxf_cols (8-bit), pxf_wts, [pxf_ncol]; pixelsim's fs_mediancut
pxf_mcut:
    xor cx, cx                      ; --- the shift ---------------------------
    mov ax, [pxf_npx]
    mov dx, [pxf_npx + 2]
.sh:
    or dx, dx
    jnz .sh1
    cmp ax, 32767
    jbe .shd
.sh1:
    shr dx, 1
    rcr ax, 1
    inc cl
    jmp short .sh
.shd:
    xor bx, bx                      ; --- the weights, and the points --------
    mov di, pxf_perm
    xor bp, bp                      ; BP = the points
.w:
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [pxf_cnt + si]
    mov dx, [pxf_cnt + si + 2]
    mov si, ax
    or si, dx
    jz .w0
    push cx
    jcxz .w2
.w1:
    shr dx, 1
    rcr ax, 1
    loop .w1
.w2:
    pop cx
    or ax, ax
    jnz .w3
    inc ax                          ; at least 1
.w3:
    mov [di], bl
    inc di
    inc bp
.w0:
    mov si, bx
    shl si, 1
    mov [pxf_w + si], ax            ; (0 for an unused entry)
    xor ax, ax
    inc bx
    cmp bx, 256
    jb .w
    mov word [pxf_bs], 0            ; one box: every point
    mov [pxf_bn], bp
    mov word [pxf_nbox], 1
.cut:
    cmp word [pxf_nbox], 16
    jb .c0
    jmp .colours
.c0:
    mov word [pxf_bpr], 0           ; --- the box to cut: weight x range -----
    mov word [pxf_bpr + 2], 0
    mov word [pxf_bbox], 0xFFFF
    xor bx, bx                      ; BX = the box x 2
.bx:
    mov ax, bx
    shr ax, 1
    cmp ax, [pxf_nbox]
    jae .chosen
    cmp word [pxf_bn + bx], 2
    jb .bnext
    call pxf_brange                 ; AX = the weighted range, DL = channel
    or ax, ax
    jz .bnext
    push dx
    push ax
    call pxf_bweight                ; AX = the box's weight
    pop dx
    mul dx                          ; DX:AX = weight x range
    pop cx                          ; CL = the channel
    cmp dx, [pxf_bpr + 2]
    jb .bnext
    ja .better
    cmp ax, [pxf_bpr]
    jbe .bnext
.better:
    mov [pxf_bpr], ax
    mov [pxf_bpr + 2], dx
    mov [pxf_bbox], bx
    mov [pxf_bbch], cl              ; (pxf_brange spends [pxf_bch])
.bnext:
    add bx, 2
    jmp short .bx
.chosen:
    mov bx, [pxf_bbox]
    cmp bx, 0xFFFF
    jne .split
    jmp .colours                    ; nothing can be cut
.split:
    call pxf_bweight                ; --- its weighted mean on the channel --
    mov [pxf_bw], ax
    mov cl, [pxf_bbch]
    mov [pxf_bch], cl
    call pxf_bsum                   ; DX:AX = sum w c
    div word [pxf_bw]               ; AX = the mean (at most 255)
    mov [pxf_bm], al
    ; the stable partition: <= the mean first, into pxf_tmp, then back
    mov si, [pxf_bs + bx]
    add si, pxf_perm
    mov cx, [pxf_bn + bx]
    mov di, pxf_tmp
    xor dx, dx                      ; DX = the left's points
    push si
    push cx
.p1:
    call pxf_pval                   ; AL = the point's channel value
    cmp al, [pxf_bm]
    ja .p1n
    mov al, [si]
    mov [di], al
    inc di
    inc dx
.p1n:
    inc si
    loop .p1
    pop cx
    pop si
    push si
    push cx
.p2:
    call pxf_pval
    cmp al, [pxf_bm]
    jbe .p2n
    mov al, [si]
    mov [di], al
    inc di
.p2n:
    inc si
    loop .p2
    pop cx
    pop di                          ; back over the box
    mov si, pxf_tmp
    push ds
    pop es
    rep movsb
    ; the boxes past it up one, and the right half after it
    mov di, [pxf_nbox]
    shl di, 1                       ; DI = the new last box x 2
.up:
    cmp di, bx
    jbe .ins
    mov ax, [pxf_bs + di - 2]
    mov [pxf_bs + di], ax
    mov ax, [pxf_bn + di - 2]
    mov [pxf_bn + di], ax
    sub di, 2
    jmp short .up
.ins:
    mov ax, [pxf_bn + bx]
    sub ax, dx
    mov [pxf_bn + bx + 2], ax       ; the right: what is left
    mov ax, [pxf_bs + bx]
    add ax, dx
    mov [pxf_bs + bx + 2], ax
    mov [pxf_bn + bx], dx           ; the left
    inc word [pxf_nbox]
    jmp .cut
.colours:                           ; --- each box its weighted mean ---------
    mov cx, [pxf_nbox]
    mov [pxf_ncol], cx
    xor bx, bx
    mov di, pxf_cols
.col:
    call pxf_bweight
    mov [pxf_bw], ax
    mov si, bx
    mov [pxf_wts + si], ax
    push cx
    xor cl, cl
.ch:
    push cx
    call pxf_bsum                   ; DX:AX = sum w c
    mov cx, [pxf_bw]
    shr cx, 1
    add ax, cx
    adc dx, 0
    div word [pxf_bw]
    mov [di], al
    inc di
    pop cx
    inc cl
    cmp cl, 3
    jb .ch
    pop cx
    add bx, 2
    loop .col
    ret

; pxf_pval - SI = a point (its palette index at [SI]), [pxf_bch] a channel:
; AL = its value. Preserves all but AX
pxf_pval:
    push bx
    mov bl, [si]
    mov bh, [pxf_bch]               ; the channel's plane
    mov al, [pxf_pch + bx]
    pop bx
    ret

; pxf_bweight - BX = a box x 2: AX = its points' weight. Preserves all but AX
pxf_bweight:
    push cx
    push si
    push di
    mov si, [pxf_bs + bx]
    add si, pxf_perm
    mov cx, [pxf_bn + bx]
    xor ax, ax
.l:
    mov di, [si]
    and di, 0xFF
    shl di, 1
    add ax, [pxf_w + di]
    inc si
    loop .l
    pop di
    pop si
    pop cx
    ret

; pxf_bsum - BX = a box x 2, CL = a channel: DX:AX = its points' sum of
; weight x value. Preserves all but AX, DX
pxf_bsum:
    push bx
    push cx
    push si
    push di
    push bp
    mov si, [pxf_bs + bx]
    add si, pxf_perm
    mov bp, [pxf_bn + bx]
    xor ch, ch
    mov di, cx                      ; DI = the channel
    xor cx, cx                      ; CX:BX = the sum
    xor bx, bx
.l:
    push bx
    mov bl, [si]
    xor bh, bh
    mov ax, bx
    shl bx, 1
    push bx                         ; (the index x 2: its weight)
    add bx, ax
    mov al, [pxf_pal + bx + di]
    xor ah, ah
    pop bx
    mul word [pxf_w + bx]
    pop bx
    add bx, ax
    adc cx, dx
    inc si
    dec bp
    jnz .l
    mov ax, bx
    mov dx, cx
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; pxf_brange - BX = a box x 2: AX = its largest weighted range, WGT (3, 6,
; 1) x (hi - lo), and DL its channel (the first on a tie). Preserves all but
; AX, DX
pxf_brange:
    push bx
    push cx
    push si
    push di
    push bp
    mov si, [pxf_bs + bx]
    add si, pxf_perm
    mov bp, [pxf_bn + bx]
    xor di, di                      ; DI = the best range
    mov byte [pxf_brc], 0
    xor bx, bx                      ; BH = the channel's plane
.ch:
    mov dx, 0x00FF                  ; DL = lo, DH = hi
    push si
    mov cx, bp
.p:
    mov bl, [si]
    mov al, [pxf_pch + bx]
    cmp al, dl
    jae .p1
    mov dl, al
.p1:
    cmp al, dh
    jbe .p2
    mov dh, al
.p2:
    inc si
    loop .p
    pop si
    mov al, dh
    sub al, dl
    xor ah, ah
    cmp bh, 1
    jne .r3
    mov dx, ax                      ; G: x 6
    shl ax, 1
    add ax, dx
    shl ax, 1
    jmp short .rw
.r3:
    or bh, bh
    jnz .rw
    mov dx, ax                      ; R: x 3
    shl ax, 1
    add ax, dx
.rw:
    cmp ax, di
    jbe .rn
    mov di, ax
    mov [pxf_brc], bh
.rn:
    inc bh
    cmp bh, 3
    jb .ch
    mov ax, di
    mov dl, [pxf_brc]
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; THE PLANS over any colours, in six bits (pixelsim's plan6)
; =============================================================================

; pxf_plans - every USED palette entry's plan against the [pxf_ncol]
; colours at pxf_c6: pxf_c1, pxf_c2, pxf_t, and the diffuser's target T -
; 128 in pxf_Tb
pxf_plans:
    xor bx, bx
.e:
    mov si, bx
    shl si, 1
    shl si, 1
    mov ax, [pxf_cnt + si]
    or ax, [pxf_cnt + si + 2]
    jz .z
    mov si, bx                      ; the target, six bits
    shl si, 1
    add si, bx
    add si, pxf_pal
    mov al, [si]
    shr al, 1
    shr al, 1
    mov [pxf_tg], al
    mov al, [si + 1]
    shr al, 1
    shr al, 1
    mov [pxf_tg + 1], al
    mov al, [si + 2]
    shr al, 1
    shr al, 1
    mov [pxf_tg + 2], al
    mov si, pxf_c6
    mov cx, [pxf_ncol]
    call pxf_plan6                  ; AL = c1, AH = c2, DL = t
    mov [pxf_c1 + bx], al
    mov [pxf_c2 + bx], ah
    mov [pxf_t + bx], dl
    mov al, dl                      ; T = (255 t + 32) >> 6, biased by -128
    mov ah, 255
    mul ah
    add ax, 32
    mov cl, 6
    shr ax, cl
    sub al, 128
    mov [pxf_Tb + bx], al
    jmp short .n
.z:
    mov byte [pxf_c1 + bx], 0
    mov byte [pxf_c2 + bx], 0
    mov byte [pxf_t + bx], 0
    mov byte [pxf_Tb + bx], -128
.n:
    inc bx
    cmp bx, 256
    jb .e
    ret

; pxf_plan6 - the target at pxf_tg (three 6-bit bytes) against CX colours
; at SI: AL = c1, AH = c2, DL = t, and its error in [pxf_E]. c1 the nearest
; (WGT 3, 6, 1; the lower on a tie); each other c2 by the projection, t =
; round(64 S / K) clipped to 64, its error sum WGT (64 d - D t)^2 + 128 K;
; the strictly lowest (pixelsim's plan6).
;
; THE ERROR IS EXPANDED, not summed: sum w (64 d - D t)^2 = 4096 sum w d^2 -
; 128 t S + t^2 K, exactly, with S and K the sums the projection needs
; anyway - so a pair costs three byte multiplies and a division rather than
; six multiplies more, and every square is a table's (pxf_sq). Preserves all
; but AX, DX
pxf_plan6:
    push bx
    push cx
    push si
    push di
    push bp
    mov [pxf_pcs], si
    mov [pxf_pcn], cx
    xor bx, bx                      ; --- c1: the nearest ---------------------
    mov word [pxf_pd], 0xFFFF
.c1:
    mov di, si
    call pxf_wsq                    ; AX = WGT-weighted squared distance
    cmp ax, [pxf_pd]
    jae .c1n
    mov [pxf_pd], ax
    mov [pxf_pc1], bl
.c1n:
    add si, 3
    inc bx
    cmp bx, cx
    jb .c1
    mov bl, [pxf_pc1]               ; dd = T - C1; the best so far is c1
    xor bh, bh                      ; alone: 4096 sum w dd^2
    mov si, bx
    shl si, 1
    add si, bx
    add si, [pxf_pcs]
    mov [pxf_pC1], si
    mov al, [pxf_tg]
    sub al, [si]
    mov [pxf_dd], al
    mov al, [pxf_tg + 1]
    sub al, [si + 1]
    mov [pxf_dd + 1], al
    mov al, [pxf_tg + 2]
    sub al, [si + 2]
    mov [pxf_dd + 2], al
    mov ax, [pxf_pd]                ; (sum w dd^2 IS c1's distance)
    xor dx, dx
    mov cx, 12
.a:
    shl ax, 1
    rcl dx, 1
    loop .a
    mov [pxf_A], ax
    mov [pxf_A + 2], dx
    mov [pxf_bE], ax
    mov [pxf_bE + 2], dx
    mov al, [pxf_pc1]
    mov [pxf_pc2], al
    mov byte [pxf_bt], 0
    xor bx, bx                      ; --- every other c2 ----------------------
    mov si, [pxf_pcs]
.c2:
    cmp bl, [pxf_pc1]
    je .c2n
    mov di, [pxf_pC1]               ; D = C2 - C1, and K = sum w D^2
    mov al, [si]
    sub al, [di]
    mov [pxf_D], al
    mov al, [si + 1]
    sub al, [di + 1]
    mov [pxf_D + 1], al
    mov al, [si + 2]
    sub al, [di + 2]
    mov [pxf_D + 2], al
    push si
    mov si, pxf_D
    call pxf_wsq0                   ; AX = K (the D's own squares)
    pop si
    or ax, ax
    jz .c2n
    mov [pxf_K], ax
    mov dx, ax                      ; 128 K is a FLOOR under the pair's
    mov cl, 7                       ; error: at or past the best, it cannot
    shl ax, cl                      ; win (plan_fast's own pruning)
    mov cl, 9
    shr dx, cl
    cmp dx, [pxf_bE + 2]
    ja .c2n
    jb .s
    cmp ax, [pxf_bE]
    jae .c2n
.s:
    call pxf_S1                     ; DX:AX = S = sum w dd D (signed)
    or dx, dx                       ; S <= 0: no help (and S > 0 is under
    js .c2n                         ; 2^16: 39,690 at most)
    or ax, ax
    jz .c2n
    mov [pxf_Sp], ax
    ; t = min(64, (64 S + K / 2) div K): the clip first, so the div fits
    mov cx, 64
    mul cx                          ; DX:AX = 64 S
    mov cx, [pxf_K]
    shr cx, 1
    add ax, cx
    adc dx, 0
    push ax
    push dx
    mov ax, [pxf_K]                 ; 65 K
    mov cx, 65
    mul cx
    mov cx, dx
    mov bp, ax
    pop dx
    pop ax
    cmp dx, cx
    jb .div
    ja .t64
    cmp ax, bp
    jb .div
.t64:
    mov ax, 64
    jmp short .tok
.div:
    div word [pxf_K]                ; AX = t, at most 64
.tok:
    or ax, ax
    jz .c2n
    mov [pxf_pt], al
    ; E = A + t^2 K + 128 K - 128 t S (every term under 2^29, and E >= 0)
    mov cx, ax
    mul cx                          ; t^2
    mul word [pxf_K]                ; DX:AX = t^2 K
    add ax, [pxf_A]
    adc dx, [pxf_A + 2]
    mov bp, ax
    mov di, dx
    mov ax, [pxf_K]                 ; + 128 K
    mov dx, ax
    push cx
    mov cl, 7
    shl ax, cl
    mov cl, 9
    shr dx, cl
    pop cx
    add bp, ax
    adc di, dx
    mov ax, [pxf_Sp]                ; - 128 t S
    mul cx
    mov cx, 7
.ts:
    shl ax, 1
    rcl dx, 1
    loop .ts
    sub bp, ax
    sbb di, dx
    cmp di, [pxf_bE + 2]            ; strictly lower?
    ja .c2n
    jb .take
    cmp bp, [pxf_bE]
    jae .c2n
.take:
    mov [pxf_bE], bp
    mov [pxf_bE + 2], di
    mov [pxf_pc2], bl
    mov al, [pxf_pt]
    mov [pxf_bt], al
.c2n:
    add si, 3
    inc bx
    cmp bx, [pxf_pcn]
    jb .c2
    mov ax, [pxf_bE]                ; the answer
    mov [pxf_E], ax
    mov ax, [pxf_bE + 2]
    mov [pxf_E + 2], ax
    mov al, [pxf_pc1]
    mov ah, [pxf_pc2]
    mov dl, [pxf_bt]
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; pxf_wsq - DI = a colour: AX = 3 (T0-C0)^2 + 6 (T1-C1)^2 + (T2-C2)^2, the
; target at pxf_tg; pxf_wsq0 - SI = three signed differences: the same sum
; of theirs. Each square is the table's; 39,690 at most. Preserve all but AX
pxf_wsq:
    push si
    push di
    mov si, pxf_wd
    mov al, [pxf_tg]
    sub al, [di]
    mov [si], al
    mov al, [pxf_tg + 1]
    sub al, [di + 1]
    mov [si + 1], al
    mov al, [pxf_tg + 2]
    sub al, [di + 2]
    mov [si + 2], al
    call pxf_wsq0
    pop di
    pop si
    ret
pxf_wsq0:
    push bx
    push dx
    mov al, [si]                    ; R, x 3
    cbw
    mov bx, ax
    shl bx, 1
    mov dx, [pxf_sq + 126 + bx]
    mov ax, dx
    shl ax, 1
    add dx, ax
    mov al, [si + 1]                ; G, x 6
    cbw
    mov bx, ax
    shl bx, 1
    mov ax, [pxf_sq + 126 + bx]
    mov bx, ax
    shl ax, 1
    add ax, bx
    shl ax, 1
    add dx, ax
    mov al, [si + 2]                ; B
    cbw
    mov bx, ax
    shl bx, 1
    mov ax, [pxf_sq + 126 + bx]
    add ax, dx
    pop dx
    pop bx
    ret

; pxf_S1 - DX:AX = S = sum WGT dd D (signed). Preserves all but AX, DX
pxf_S1:
    push bx
    push cx
    xor bx, bx                      ; CX:BX = S
    xor cx, cx
    mov al, [pxf_dd]                ; R, x 3
    imul byte [pxf_D]
    mov dx, ax
    shl ax, 1
    add ax, dx
    cwd
    add bx, ax
    adc cx, dx
    mov al, [pxf_dd + 1]            ; G, x 6
    imul byte [pxf_D + 1]
    mov dx, ax
    shl ax, 1
    add ax, dx
    shl ax, 1
    cwd
    add bx, ax
    adc cx, dx
    mov al, [pxf_dd + 2]            ; B
    imul byte [pxf_D + 2]
    cwd
    add ax, bx
    adc dx, cx
    pop cx
    pop bx
    ret

; the squares of -63..63, a word each: pxf_sq + 126 is 0's
pxf_sq:
%assign i -63
%rep 127
    dw i * i
%assign i i + 1
%endrep

; =============================================================================
; THE CGA'S FOUR (320x200): 96 sets scored against the cut (fs_cgapick)
; =============================================================================

; pxf_cgapick - every background x {palette 1, palette 0, mode 5} x {high,
; low}, each scored as the sum over the cut's colours of its weight x (its
; best plan's error over the four >> 8); the lowest, the first on a tie.
; Out pxf_c6 (four colours), [pxf_ncol] = 4, [pxf_cset], [pxf_chi], [pxf_cbg]
pxf_cgapick:
    mov cx, [pxf_ncol]              ; the heaviest targets first (the sum is
    dec cx                          ; the same in any order; it reaches a
    jle .sorted                     ; losing candidate's best sooner)
.so:
    xor bx, bx
    xor dx, dx                      ; DX = a swap was made
.si:
    mov ax, [pxf_wts + bx]
    cmp ax, [pxf_wts + bx + 2]
    jae .sn
    xchg ax, [pxf_wts + bx + 2]
    mov [pxf_wts + bx], ax
    mov si, bx                      ; ...and their colours with them
    shr si, 1
    mov ax, si
    shl si, 1
    add si, ax
    mov ax, [pxf_cols + si]
    xchg ax, [pxf_cols + si + 3]
    mov [pxf_cols + si], ax
    mov al, [pxf_cols + si + 2]
    xchg al, [pxf_cols + si + 5]
    mov [pxf_cols + si + 2], al
    inc dx
.sn:
    add bx, 2
    mov ax, bx
    shr ax, 1
    cmp ax, cx
    jb .si
    or dx, dx
    jnz .so
.sorted:
    mov word [pxf_best + 4], 0xFFFF ; no best yet
    xor bx, bx                      ; BX = the set (0..2)
.set:
    mov byte [pxf_chi1], 1
.hi:
    mov byte [pxf_cbg1], 0
.bg:
    call pxf_cgafour                ; pxf_c4 = the four, six bits
    xor ax, ax                      ; the score, 48 bits
    mov [pxf_sc], ax
    mov [pxf_sc + 2], ax
    mov [pxf_sc + 4], ax
    xor di, di                      ; DI = the cut's colour
.tg:
    cmp di, [pxf_ncol]
    jae .scored
    mov si, di                      ; its six bits
    shl si, 1
    add si, di
    mov al, [pxf_cols + si]
    shr al, 1
    shr al, 1
    mov [pxf_tg], al
    mov al, [pxf_cols + si + 1]
    shr al, 1
    shr al, 1
    mov [pxf_tg + 1], al
    mov al, [pxf_cols + si + 2]
    shr al, 1
    shr al, 1
    mov [pxf_tg + 2], al
    push bx
    mov si, pxf_c4
    mov cx, 4
    call pxf_plan6                  ; [pxf_E]
    pop bx
    mov ax, [pxf_E]                 ; E >> 8
    mov dx, [pxf_E + 2]
    mov al, ah
    mov ah, dl
    mov dl, dh
    xor dh, dh                      ; DX:AX = E >> 8 (DX under 2^8)
    mov si, di
    shl si, 1
    mov cx, [pxf_wts + si]
    push dx
    mul cx                          ; low word x w
    add [pxf_sc], ax
    adc [pxf_sc + 2], dx
    adc word [pxf_sc + 4], 0
    pop ax
    mul cx                          ; high word x w, 16 up
    add [pxf_sc + 2], ax
    adc [pxf_sc + 4], dx
    inc di
    mov ax, [pxf_sc + 4]            ; at or past the best already: the rest
    cmp ax, [pxf_best + 4]          ; only add, so it cannot win
    jb .tg
    ja .next
    mov ax, [pxf_sc + 2]
    cmp ax, [pxf_best + 2]
    jb .tg
    ja .next
    mov ax, [pxf_sc]
    cmp ax, [pxf_best]
    jb .tg
    jmp short .next
.scored:
    mov ax, [pxf_sc + 4]            ; lower than the best? (48 bits)
    cmp ax, [pxf_best + 4]
    ja .next
    jb .take
    mov ax, [pxf_sc + 2]
    cmp ax, [pxf_best + 2]
    ja .next
    jb .take
    mov ax, [pxf_sc]
    cmp ax, [pxf_best]
    jae .next
.take:
    mov ax, [pxf_sc]
    mov [pxf_best], ax
    mov ax, [pxf_sc + 2]
    mov [pxf_best + 2], ax
    mov ax, [pxf_sc + 4]
    mov [pxf_best + 4], ax
    mov [pxf_cset], bl
    mov al, [pxf_chi1]
    mov [pxf_chi], al
    mov al, [pxf_cbg1]
    mov [pxf_cbg], al
.next:
    inc byte [pxf_cbg1]
    cmp byte [pxf_cbg1], 16
    jb .bg
    dec byte [pxf_chi1]
    jns .hi
    inc bx
    cmp bx, 3
    jb .set
    mov bl, [pxf_cset]              ; the chosen four, as the plans want them
    mov al, [pxf_chi]
    mov [pxf_chi1], al
    mov al, [pxf_cbg]
    mov [pxf_cbg1], al
    xor bh, bh
    call pxf_cgafour
    mov si, pxf_c4
    mov di, pxf_c6
    mov cx, 12
    push ds
    pop es
    rep movsb
    mov word [pxf_ncol], 4
    ret

; pxf_cgafour - BL = a set (0 palette 1, 1 palette 0, 2 mode 5), [pxf_chi1]
; high, [pxf_cbg1] the background: pxf_c4 = its four in six bits, and
; pxf_c4i their RGBI numbers. Preserves all
pxf_cgafour:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [pxf_cbg1]
    mov [pxf_c4i], al
    xor bh, bh
    mov si, bx
    shl si, 1
    add si, bx
    add si, pxf_csets
    mov cx, 3
    mov di, pxf_c4i + 1
.f:
    mov al, [si]
    cmp byte [pxf_chi1], 0
    je .lo
    add al, 8
.lo:
    mov [di], al
    inc si
    inc di
    loop .f
    xor si, si
.c:
    mov bl, [pxf_c4i + si]
    xor bh, bh
    mov di, bx
    shl di, 1
    add di, bx
    mov al, [pxf_cga6 + di]
    mov bx, si
    shl bx, 1
    add bx, si
    mov [pxf_c4 + bx], al
    mov al, [pxf_cga6 + di + 1]
    mov [pxf_c4 + bx + 1], al
    mov al, [pxf_cga6 + di + 2]
    mov [pxf_c4 + bx + 2], al
    inc si
    cmp si, 4
    jb .c
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; pxf_cgapal - the chosen set on the card THROUGH THE BIOS (Clear Skies'
; cs_cga_pal: AH=0Bh twice, never port 3D9h); mode 5's red is the CGA's
; 3D8h B/W bit, and an EGA's palette register 2 (the Video Player's
; vp_cgaset). The pixel values 0..3 are then the four of pxf_c6
pxf_cgapal:
    mov ah, 0x0B                    ; the background, and the intensity
    xor bh, bh
    mov bl, [pxf_cbg]
    cmp byte [pxf_chi], 0
    je .b
    or bl, 0x10
.b:
    int 0x10
    mov ah, 0x0B                    ; the set: palette 0 is set 1 (index 1),
    mov bh, 1                       ; palette 1 and mode 5 are palette 1
    xor bl, bl
    cmp byte [pxf_cset], 1
    je .p
    inc bl
.p:
    int 0x10
    cmp byte [pxf_cset], 2
    jne .out
    cmp byte [pxf_vkind], VID_CGA   ; MODE 5's cyan, red and white
    jne .ev
    mov dx, 0x3D8
    mov al, 0x0E                    ; 320x200, the black-and-white bit, video
    out dx, al
    ret
.ev:
    mov ax, 0x1000                  ; palette register 2: magenta -> red, in
    mov bx, 0x0402                  ; the 200-line codes, intensity 10h
    cmp byte [pxf_chi], 0
    je .ev1
    mov bh, 0x14
.ev1:
    int 0x10
.out:
    ret

; =============================================================================
; THE VIEW (pixelsim's FsView): a zoom, its steps, the picture's place
; =============================================================================

; pxf_zoom - DX:AX = a zoom (0 = Fit): the view, centred. CF = 1 a side of
; 32,768 or more (nothing changed)
pxf_zoom:
    mov cx, ax
    or cx, dx
    jnz .z
    call pxf_fit
.z:
    push word [pxf_z]
    push word [pxf_z + 2]
    mov [pxf_z], ax
    mov [pxf_z + 2], dx
    call pxf_steps
    cmp word [pxf_dw + 2], 0
    jne .big
    cmp word [pxf_dh + 2], 0
    jne .big
    cmp word [pxf_dw], 32767
    ja .big
    cmp word [pxf_dh], 32767
    ja .big
    add sp, 4
    mov word [pxf_ox], 0x8000       ; centred
    mov word [pxf_oy], 0x8000
    call pxf_place
    clc
    ret
.big:
    pop word [pxf_z + 2]
    pop word [pxf_z]
    call pxf_steps
    stc
    ret

; pxf_fit - DX:AX = Fit, the closed form: 2^32 div max(ceil(mw 2^16 / W),
; ceil(ceil(mh 2^16 / H) an / ad), 1)
pxf_fit:
    mov ax, [pxf_mw]
    mov cx, [pxf_W]
    call pxf_cdiv16                 ; DX:AX = ceil((AX << 16) / CX)
    push dx
    push ax
    mov ax, [pxf_mh]
    mov cx, [pxf_H]
    call pxf_cdiv16                 ; vy
    mov bx, dx                      ; x an (vy under 2^24, an under 64)
    mul word [pxf_an]
    push dx
    push ax
    mov ax, bx
    mul word [pxf_an]
    pop bx
    pop cx
    add ax, cx                      ; AX:BX = vy x an (the high word in AX)
    mov dx, ax
    mov ax, bx
    mov cx, [pxf_ad]
    call pxf_cdiv32x16              ; DX:AX = ceil(DX:AX / CX)
    pop bx                          ; hx
    pop cx
    cmp dx, cx                      ; the larger
    ja .hy
    jb .hx
    cmp ax, bx
    jae .hy
.hx:
    mov ax, bx
    mov dx, cx
.hy:
    or dx, dx
    jnz .div
    or ax, ax
    jnz .div
    inc ax
.div:
    mov word [pxf_n0], 0            ; 2^32 / it
    mov word [pxf_n1], 0
    mov word [pxf_n2], 1
    mov [pxf_dv0], ax
    mov [pxf_dv1], dx
    call pxf_div
    mov ax, [pxf_n0]
    mov dx, [pxf_n1]
    ret

; pxf_steps - from [pxf_z]: hs = 2^32 div z, vs = hs ad div an, dw = ceil(mw
; 2^16 / hs), dh = ceil(mh 2^16 / vs) (32-bit each)
pxf_steps:
    mov word [pxf_n0], 0
    mov word [pxf_n1], 0
    mov word [pxf_n2], 1
    mov ax, [pxf_z]
    mov [pxf_dv0], ax
    mov ax, [pxf_z + 2]
    mov [pxf_dv1], ax
    call pxf_div
    mov ax, [pxf_n0]
    mov [pxf_hs], ax
    mov dx, [pxf_n1]
    mov [pxf_hs + 2], dx
    mul word [pxf_ad]               ; hs x ad: 48 bits, then / an
    mov [pxf_n0], ax
    mov [pxf_n1], dx
    mov ax, [pxf_hs + 2]
    mul word [pxf_ad]
    add [pxf_n1], ax
    adc dx, 0
    mov [pxf_n2], dx
    mov ax, [pxf_an]
    mov [pxf_dv0], ax
    mov word [pxf_dv1], 0
    call pxf_div
    mov ax, [pxf_n0]
    mov [pxf_vs], ax
    mov ax, [pxf_n1]
    mov [pxf_vs + 2], ax
    mov ax, [pxf_mw]
    mov bx, pxf_hs
    call pxf_side
    mov [pxf_dw], ax
    mov [pxf_dw + 2], dx
    mov ax, [pxf_mh]
    mov bx, pxf_vs
    call pxf_side
    mov [pxf_dh], ax
    mov [pxf_dh + 2], dx
    ret

; pxf_side - AX = a master side, BX -> its step (a dword): DX:AX = ceil((AX
; << 16) / step)
pxf_side:
    mov word [pxf_n0], 0
    mov [pxf_n1], ax
    mov word [pxf_n2], 0
    mov ax, [bx]
    mov [pxf_dv0], ax
    mov ax, [bx + 2]
    mov [pxf_dv1], ax
    call pxf_div
    or ax, dx                       ; a remainder: one more
    mov ax, [pxf_n0]
    mov dx, [pxf_n1]
    jz .x
    add ax, 1
    adc dx, 0
.x:
    ret

; pxf_place - [pxf_ox] [pxf_oy] (8000h = centre) kept in range: a side that
; fits centred, a larger one clamped so the screen stays covered; then the
; columns the picture spans, [pxf_xa] .. [pxf_xb]
pxf_place:
    mov ax, [pxf_ox]
    mov bx, [pxf_W]
    mov cx, [pxf_dw]
    call pxf_pl1
    mov [pxf_ox], ax
    mov ax, [pxf_oy]
    mov bx, [pxf_H]
    mov cx, [pxf_dh]
    call pxf_pl1
    mov [pxf_oy], ax
    mov ax, [pxf_ox]                ; xa = max(0, ox), xb = min(W, ox + dw)
    or ax, ax
    jns .a
    xor ax, ax
.a:
    mov [pxf_xa], ax
    mov ax, [pxf_ox]
    add ax, [pxf_dw]
    cmp ax, [pxf_W]
    jle .b
    mov ax, [pxf_W]
.b:
    mov [pxf_xb], ax
    mov word [pxf_lmy], 0xFFFF      ; no row sampled
    ret

; pxf_pl1 - AX = an offset (8000h centre), BX = the screen's side, CX = the
; picture's: AX in range
pxf_pl1:
    cmp cx, bx
    jg .big
    mov ax, bx
    sub ax, cx
    shr ax, 1
    ret
.big:
    cmp ax, 0x8000
    jne .c
    mov ax, bx
    sub ax, cx
    sar ax, 1
.c:
    or ax, ax
    jle .lo
    xor ax, ax
.lo:
    mov dx, bx
    sub dx, cx                      ; W - dw < 0
    cmp ax, dx
    jge .ok
    mov ax, dx
.ok:
    ret

; pxf_cdiv16 - DX:AX = ceil((AX << 16) / CX), 32 bits. Preserves CX
pxf_cdiv16:
    push bx
    xor dx, dx                      ; the high word: AX / CX
    div cx
    mov bx, ax
    xor ax, ax                      ; the low: (rem << 16) / CX
    div cx
    or dx, dx
    mov dx, bx
    jz .x
    add ax, 1
    adc dx, 0
.x:
    pop bx
    ret

; pxf_cdiv32x16 - DX:AX = ceil(DX:AX / CX). Preserves CX
pxf_cdiv32x16:
    push bx
    push ax
    mov ax, dx
    xor dx, dx
    div cx
    mov bx, ax                      ; the high word
    pop ax
    div cx
    or dx, dx
    mov dx, bx
    jz .x
    add ax, 1
    adc dx, 0
.x:
    pop bx
    ret

; pxf_div - [pxf_n0..n2] (48 bits) / [pxf_dv0..dv1] (32, not 0): the
; quotient in [pxf_n0..n2], the remainder in DX:AX (px_div's shift and
; subtract, the part's own copy). Preserves BX, CX, SI, DI
pxf_div:
    push cx
    xor ax, ax
    xor dx, dx
    mov cx, 48
.l:
    shl word [pxf_n0], 1
    rcl word [pxf_n1], 1
    rcl word [pxf_n2], 1
    rcl ax, 1
    rcl dx, 1
    jc .sub
    cmp dx, [pxf_dv1]
    jb .no
    ja .sub
    cmp ax, [pxf_dv0]
    jb .no
.sub:
    sub ax, [pxf_dv0]
    sbb dx, [pxf_dv1]
    or word [pxf_n0], 1
.no:
    loop .l
    pop cx
    ret

; =============================================================================
; THE CAPTIONS
; =============================================================================

; pxf_capname - the bottom band's line: the picture's name, and "3 of 9"
; when it is among the folder's
pxf_capname:
    push ds
    mov ds, [cs:PXP_PKG]
    mov si, [cs:pxf_rec]            ; PXR_NAME is the record's first field
    push cs
    pop es
    mov di, pxf_cap
    mov cx, 12
.n:
    lodsb
    or al, al
    jz .nd
    stosb
    loop .n
.nd:
    mov ax, [px_ncur]
    mov bx, [px_nnames]
    pop ds
    cmp ax, bx
    jae .z
    mov byte [di], ' '
    mov byte [di + 1], ' '
    add di, 2
    inc ax
    call pxf_dec
    mov si, pxf_s_of
.of:
    lodsb
    stosb
    or al, al
    jnz .of
    dec di
    mov ax, bx
    call pxf_dec
.z:
    mov byte [di], 0
    ret

; pxf_dec - AX = a number: its decimal at ES:DI, DI past it. Preserves BX
pxf_dec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop ax
    add al, '0'
    stosb
    loop .p
    pop dx
    pop cx
    pop bx
    ret

; pxf_msg - DI = a line in the package: the bottom band says it, now, for
; two seconds (rows of the band only: no render of the picture)
pxf_msg:
    push ds
    mov ds, [cs:PXP_PKG]
    mov si, di
    push cs
    pop es
    mov di, pxf_cap
    mov cx, 90
.c:
    lodsb
    stosb
    or al, al
    jz .z
    loop .c
    mov byte [es:di], 0
.z:
    pop ds
    call far OSAPI_GET_TICKS
    mov [pxf_bt1], ax
    or byte [pxf_bup], 2
    mov ax, [pxf_H]
    mov bx, ax
    sub ax, FS_BAND
    call pxf_ovrows                 ; rows AX .. BX - 1, overlay alone
    ret

; pxf_prog - DI = FFFFh: the line comes off (the bottom band's rows owed
; again); else a next picture decodes: the bottom row is the line, its
; percent the rows the worker has emitted (px_progfield's), drawn when it
; moved
pxf_prog:
    cmp di, 0xFFFF
    jne .on
    cmp word [pxf_pct], 0xFFFF
    je .x
    mov word [pxf_pct], 0xFFFF
    or byte [pxf_bdue], 2           ; the band's rows again, when nothing else
.x:                                 ; is owed
    ret
.on:
    push ds
    mov ds, [PXP_PKG]
    mov ax, [px_srcdone]
    mov bx, [px_cur + PXR_SH]       ; (px_cur is the one decoding)
    pop ds
    mov cx, 100
    mul cx
    or bx, bx
    jz .x
    cmp dx, bx                      ; (rows past the picture's: 100)
    jae .full
    div bx
    cmp ax, 100
    jbe .p
.full:
    mov ax, 100
.p:
    cmp ax, [pxf_pct]
    je .x
    mov [pxf_pct], ax
    mov ax, [pxf_H]
    mov bx, ax
    dec ax
    call pxf_ovrows
    ret

; pxf_ovrows - rows AX .. BX - 1 drawn as overlay alone: a band's or the
; progress line, which cover each of them whole
pxf_ovrows:
.r:
    cmp ax, bx
    jae .f
    push ax
    push bx
    mov [pxf_y], ax
    call pxf_ground
    call pxf_overlay
    call pxf_out
    pop bx
    pop ax
    inc ax
    jmp short .r
.f:
    call pxf_flush
    ret

; pxf_overlay - the row [pxf_y] in pxf_R: a raised band's text over its
; ground, or the progress line; nothing elsewhere. CF = 1 it was overlaid
pxf_overlay:
    mov ax, [pxf_y]
    cmp word [pxf_pct], 0xFFFF      ; the progress line, over anything
    je .b
    mov bx, [pxf_H]
    dec bx
    cmp ax, bx
    je .line
.b:
    mov bx, [pxf_H]
    sub bx, FS_BAND
    test byte [pxf_bup], 2
    jz .t
    cmp ax, bx
    jl .t
    sub ax, bx
    mov si, pxf_cap
    jmp short .band
.line:
    call pxf_ground                 ; the line: pct of the width lit
    mov ax, [pxf_W]
    mul word [pxf_pct]
    mov cx, 100
    div cx
    mov cx, ax
    jcxz .pd
    mov di, pxf_R
    mov al, [pxf_light]
    push ds
    pop es
    rep stosb
.pd:
    stc
    ret
.t:
    test byte [pxf_bup], 1
    jz .no
    cmp ax, FS_BAND
    jge .no
    mov si, [pxf_hint]
.band:                              ; AX = the band's row, SI its line
    push ax
    push si
    call pxf_ground
    pop si
    pop ax
    dec ax                          ; rows 1..8 are the glyphs' 0..7
    js .bd
    cmp ax, 8
    jae .bd
    call pxf_letter
.bd:
    stc
    ret
.no:
    clc
    ret

; pxf_gside - pxf_R's columns either side of the picture's, [0, xa) and
; [xb, W), in the ground colour (the picture's are the row's own)
pxf_gside:
    push ds
    pop es
    mov al, [pxf_gnd]
    mov di, pxf_R
    mov cx, [pxf_xa]
    rep stosb
    mov di, [pxf_xb]
    mov cx, [pxf_W]
    sub cx, di
    jbe .x
    add di, pxf_R
    rep stosb
.x:
    ret

; pxf_ground - pxf_R, [pxf_W] of it, in the ground colour
pxf_ground:
    push ds
    pop es
    mov di, pxf_R
    mov cx, [pxf_W]
    mov al, [pxf_gnd]
    rep stosb
    ret

; pxf_letter - AX = a glyph row (0..7), SI = a line (the part's): its glyphs'
; row in the ink, from column 4 (0 on a 160-wide screen), as many cells as
; fit
pxf_letter:
    mov bx, 4
    cmp word [pxf_W], 160
    ja .x
    xor bx, bx
.x:
    mov di, bx                      ; DI = the screen column
    mov cx, [pxf_W]
    sub cx, bx
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; CX = the cells that fit
    mov dx, ax                      ; DX = the glyph row
    mov es, [pxf_font + 2]
.c:
    lodsb
    or al, al
    jz .done
    cmp al, [pxf_gfirst]
    jb .sp
    cmp al, [pxf_gfirst + 1]
    ja .sp
    sub al, [pxf_gfirst]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, dx
    add ax, [pxf_font]
    mov bx, ax
    mov ah, [es:bx]                 ; the glyph's row
    mov bx, di
    push cx
    mov cx, 8
    mov al, [pxf_light]
.b:
    shl ah, 1
    jnc .b0
    mov [pxf_R + bx], al
.b0:
    inc bx
    loop .b
    pop cx
.sp:
    add di, 8
    loop .c
.done:
    ret

; =============================================================================
; THE POLL
; =============================================================================

; pxf_poll - one pass. AX = an FSA_*
pxf_poll:
    call pxf_keys                   ; a verb, or a zoom or a pan done here
    or ax, ax
    jnz .out
    cmp byte [pxf_jon], 0
    jne .slice
    call pxf_bands                  ; a band whose time is up, owed again
    xor ax, ax
    cmp byte [pxf_jon], 0
    je .out
.slice:
    call pxf_mseg                   ; THE MASTER GIVEN BACK (a next picture
    or ax, ax                       ; decoding where it was, 106.23): the
    jz .out                         ; render waits - a slice now would sample
                                    ; segment 0 (review-w6 F1); the commit's
                                    ; FSV_SHOW renders anew
    call pxf_slice
    mov ax, FSA_BUSY
    cmp byte [pxf_jon], 0
    jne .out
    xor ax, ax
.out:
    ret

; pxf_bands - a band up two seconds comes down: its rows rendered again;
; and the progress line taken off. Nothing while the picture's master is
; given back (a next one decoding where it was)
pxf_bands:
    call pxf_mseg
    or ax, ax
    jz .out
    call far OSAPI_GET_TICKS
    mov bx, ax
    test byte [pxf_bup], 2
    jz .t
    sub ax, [pxf_bt1]
    cmp ax, FS_HOLD
    jb .t
    and byte [pxf_bup], 0xFD
    or byte [pxf_bdue], 2
.t:
    test byte [pxf_bup], 1
    jz .due
    mov ax, bx
    sub ax, [pxf_bt0]
    cmp ax, FS_HOLD
    jb .due
    and byte [pxf_bup], 0xFE
    or byte [pxf_bdue], 1
.due:
    test byte [pxf_bdue], 2         ; the bottom band, from the state kept
    jz .top
    and byte [pxf_bdue], 0xFD
    mov ax, [pxf_H]
    sub ax, FS_BAND
    mov bx, [pxf_H]
    mov dl, 2
    call pxf_job
    ret
.top:
    test byte [pxf_bdue], 1
    jz .out
    and byte [pxf_bdue], 0xFE
    xor ax, ax
    mov bx, FS_BAND
    mov dl, 1
    call pxf_job
.out:
    ret

; pxf_keys - the mouse, Alt+Enter and a key. AX = an FSA_*, or 0 with a
; zoom or a pan done here (or nothing)
pxf_keys:
    mov al, KSC_ALT                 ; ALT+ENTER (os88alt_edge's, the part's
    call far OSAPI_KEY_DOWN         ; own copy: a part cannot call the
    jnc .aup                        ; package's code)
    mov al, KSC_ENTER
    call far OSAPI_KEY_DOWN
    jnc .aup
    cmp byte [pxf_altdn], 0
    jne .kbd
    mov byte [pxf_altdn], 1
    jmp .leave
.aup:
    mov byte [pxf_altdn], 0
.kbd:
    mov ah, 1                       ; A KEY FIRST: with no mouse that has
    int 0x16                        ; spoken, OSAPI_MOUSE is the keyboard
    jnz .key                        ; mouse, and would take Space and the
    call far OSAPI_MOUSE            ; arrows for itself (SPEC.md 9.6.1)
    mov ah, [pxf_btn]               ; AL = the buttons: a PRESS is an edge
    mov [pxf_btn], al
    not ah
    and al, ah
    test al, 1
    jz .m2
    mov ax, FSA_NEXT
    jmp .verb
.m2:
    test al, 2
    jz .none
    mov ax, FSA_PREV
    jmp .verb
.none:
    xor ax, ax
    ret
.key:
    xor ah, ah
    int 0x16
    cmp ah, KSC_ESC
    je .leave
    cmp ax, KEY_ALTENTER            ; (a BIOS that does enqueue it)
    je .leave
    mov dl, al
    and dl, 0xDF
    cmp dl, 'F'
    je .leave
    cmp byte [pxf_slon], 0          ; a slideshow: any other key stops it
    je .k1
    mov ax, FSA_STOP
    ret
.k1:
    cmp al, ' '
    je .next
    cmp dl, 'N'
    je .next
    cmp al, 8
    je .prev
    cmp dl, 'P'
    je .prev
    cmp ah, 0x47                    ; Home
    jne .k2
    mov ax, FSA_FIRST
    ret
.k2:
    cmp ah, 0x4F                    ; End
    jne .k3
    mov ax, FSA_LAST
    ret
.k3:
    cmp dl, 'S'
    jne .k4
    mov ax, FSA_SLIDE
    ret
.k4:
    cmp dl, 'M'
    jne .k5
    mov ax, FSA_MODE
    ret
.k5:
    push ax
    call pxf_mseg                   ; a zoom or a pan needs the master
    or ax, ax
    pop ax
    jnz .k6
    xor ax, ax
    ret
.k6:
    cmp al, '+'
    je .zin
    cmp al, '='
    je .zin
    cmp al, '-'
    je .zout
    cmp al, '0'
    je .zfit
    cmp al, '1'
    je .z1
    cmp ah, KSC_LEFT
    je .pl
    cmp ah, KSC_RIGHT
    je .pr
    cmp ah, KSC_UP
    je .pu
    cmp ah, KSC_DOWN
    je .pd
    xor ax, ax
    ret
.next:
    mov ax, FSA_NEXT
    ret
.prev:
    mov ax, FSA_PREV
    ret
.leave:
    mov ax, FSA_LEAVE
    ret
.verb:
    cmp byte [pxf_slon], 0
    je .v
    mov ax, FSA_STOP
.v:
    ret
.zin:
    mov al, 1
    jmp short .zs
.zout:
    xor al, al
.zs:
    call pxf_zstep                  ; DX:AX = the next step, or the same
    jmp short .zto
.zfit:
    call pxf_fit
    jmp short .zto
.z1:
    xor ax, ax
    mov dx, 1
.zto:
    cmp ax, [pxf_z]                 ; the same zoom: nothing
    jne .zgo
    cmp dx, [pxf_z + 2]
    je .znone
.zgo:
    call pxf_zoom
    jc .znone
    call pxf_jfull
.znone:
    xor ax, ax
    ret
.pl:
    mov ax, [pxf_W]                 ; the view left: the picture moves right
    mov cl, 3
    shr ax, cl
    xor bx, bx
    jmp short .pan
.pr:
    mov ax, [pxf_W]
    mov cl, 3
    shr ax, cl
    neg ax
    xor bx, bx
    jmp short .pan
.pu:
    mov bx, [pxf_H]
    mov cl, 3
    shr bx, cl
    xor ax, ax
    jmp short .pan
.pd:
    mov bx, [pxf_H]
    mov cl, 3
    shr bx, cl
    neg bx
    xor ax, ax
.pan:
    call pxf_pan
    xor ax, ax
    ret

; pxf_zstep - AL = 1 in, 0 out: DX:AX = the next of 106.11's steps past
; [pxf_z], or [pxf_z] when there is none or it would make a side of 32,768
pxf_zstep:
    mov si, pxf_zsteps
    mov cx, 12
    or al, al
    jz .out
.in:
    mov ax, [si]
    mov dx, [si + 2]
    cmp dx, [pxf_z + 2]
    ja .try
    jb .in1
    cmp ax, [pxf_z]
    ja .try
.in1:
    add si, 4
    loop .in
    jmp short .same
.out:
    add si, 44                      ; from the top down
.o1:
    mov ax, [si]
    mov dx, [si + 2]
    cmp dx, [pxf_z + 2]
    jb .try
    ja .o2
    cmp ax, [pxf_z]
    jb .try
.o2:
    sub si, 4
    loop .o1
.same:
    mov ax, [pxf_z]
    mov dx, [pxf_z + 2]
    ret
.try:
    ret                             ; (pxf_zoom refuses a side too large)

; pxf_pan - AX = columns, BX = rows the picture moves: kept in range, and
; then the 256-colour modes MOVE what stays and render the strip, the
; others render whole
pxf_pan:
    mov cx, [pxf_ox]
    mov dx, [pxf_oy]
    mov [pxf_pox], cx
    mov [pxf_poy], dx
    mov si, [pxf_dw]
    cmp si, [pxf_W]
    jle .y
    add [pxf_ox], ax
.y:
    mov si, [pxf_dh]
    cmp si, [pxf_H]
    jle .pl
    add [pxf_oy], bx
.pl:
    call pxf_place
    mov ax, [pxf_ox]
    sub ax, [pxf_pox]               ; AX, BX = what it moved
    mov bx, [pxf_oy]
    sub bx, [pxf_poy]
    mov cx, ax
    or cx, bx
    jz .none
    cmp byte [pxf_kind], 0          ; a 256 mode, nothing up, nothing owed:
    jne .whole                      ; move it
    cmp byte [pxf_bup], 0
    jne .whole
    cmp word [pxf_pct], 0xFFFF
    jne .whole
    cmp byte [pxf_jon], 0
    jne .whole
    cmp byte [pxf_mode], PXM_MODEX  ; Mode X moves four columns an address
    jne .sh
    test al, 3
    jnz .whole
.sh:
    call pxf_shift
    ret
.whole:
    call pxf_jfull
.none:
    ret

; =============================================================================
; THE RENDER: a JOB of rows, done a slice at a time
; =============================================================================

; pxf_jfull - a whole render owed: every row, the diffuser from zero, the
; bottom band's state kept on the way past
pxf_jfull:
    xor ax, ax
    mov bx, [pxf_H]
    mov dl, 0
    and byte [pxf_bdue], 0          ; (it covers the bands)
    ; fall through
; pxf_job - rows AX .. BX - 1, every column; DL = 0 whole, 1 the top band,
; 2 the bottom band (from the state kept)
pxf_job:
    mov [pxf_jy], ax
    mov [pxf_jy1], bx
    mov [pxf_jkind], dl
    mov word [pxf_jx0], 0
    mov cx, [pxf_W]
    mov [pxf_jx1], cx
    mov byte [pxf_jon], 1
    mov word [pxf_lmy], 0xFFFF
    push ds
    pop es
    cmp dl, 2
    je .kept
    mov di, pxf_E0                  ; the diffuser at zero
    mov cx, PXF_WMAX + 2
    xor ax, ax
    rep stosw
    ret
.kept:
    mov si, pxf_ES                  ; ...or where the whole render was
    mov di, pxf_E0
    mov cx, PXF_WMAX + 2
    rep movsw
    ret

; pxf_slice - rows of the job until it is done or a key is waiting (the
; first row always), then whatever the Desktop's band holds put up
pxf_slice:
    mov byte [pxf_srow], 0
.r:
    cmp byte [pxf_jon], 0
    je .f
    cmp byte [pxf_srow], 0          ; a key waits: the poll's, next pass
    je .go
    mov ah, 1
    int 0x16
    jnz .f
.go:
    mov byte [pxf_srow], 1
    mov ax, [pxf_jy]
    mov [pxf_y], ax
    cmp byte [pxf_jkind], 0         ; the whole render passes the bottom
    jne .row                        ; band: its state is kept
    mov bx, [pxf_H]
    sub bx, FS_BAND
    cmp ax, bx
    jne .row
    push ds
    pop es
    mov si, pxf_E0
    mov di, pxf_ES
    mov cx, PXF_WMAX + 2
    rep movsw
.row:
    call pxf_row
    inc word [pxf_jy]
    mov ax, [pxf_jy]
    cmp ax, [pxf_jy1]
    jb .r
    mov byte [pxf_jon], 0
    inc word [pxf_nren]             ; (renders finished: a test's)
    cmp byte [pxf_jkind], 0         ; a whole render: the bands' two seconds
    jne .f                          ; count from now, when they are on the
    call far OSAPI_GET_TICKS        ; glass (the render letters them)
    test byte [pxf_bup], 1
    jz .b1
    mov [pxf_bt0], ax
.b1:
    test byte [pxf_bup], 2
    jz .f
    mov [pxf_bt1], ax
.f:
    call pxf_flush
    ret

; pxf_mseg - AX = the shown master's segment, re-read (0: given back)
pxf_mseg:
    push bx
    push ds
    mov ds, [cs:PXP_PKG]
    mov bx, [cs:pxf_rec]
    mov ax, [bx + PXR_MSEG]
    pop ds
    pop bx
    ret

; pxf_row - the row [pxf_y]: sampled, coloured, overlaid, put on the glass
pxf_row:
    mov ax, [pxf_y]
    sub ax, [pxf_oy]                ; ky
    js .gnd
    cmp ax, [pxf_dh]
    jae .gnd
    mov cx, [pxf_xb]
    sub cx, [pxf_xa]
    jle .gnd
    mov [pxf_ky], ax
    mov ax, [pxf_xa]                ; the columns sampled: the picture's, cut
    cmp ax, [pxf_jx0]               ; to a strip's
    jge .a
    mov ax, [pxf_jx0]
.a:
    mov [pxf_sa], ax
    mov ax, [pxf_xb]
    cmp ax, [pxf_jx1]
    jle .b
    mov ax, [pxf_jx1]
.b:
    mov [pxf_sb], ax
    cmp ax, [pxf_sa]
    jle .gnd
    call pxf_sample                 ; pxf_S[sa..sb)
    call pxf_gside
    mov al, [pxf_kind]
    or al, al
    jnz .dith
    push ds                         ; 256: the indices ARE the colours
    pop es
    mov si, [pxf_sa]
    mov di, si
    mov cx, [pxf_sb]
    sub cx, si
    add si, pxf_S
    add di, pxf_R
    rep movsb
    jmp short .ov
.dith:
    cmp byte [pxf_dith], 0
    je .ord
    call pxf_diffuse
    jmp short .ov
.ord:
    call pxf_order
    jmp short .ov
.gnd:
    call pxf_ground
.ov:
    call pxf_overlay
    call pxf_out
    ret

; pxf_sample - the master row of screen row ky = [pxf_ky] into pxf_S[xa..xb),
; nearest, by the DDA: x shows master column ((x - ox) hs) >> 16. A master
; row the last row sampled is not sampled again
pxf_sample:
    mov ax, [pxf_ky]                ; my = (ky vs) >> 16
    mul word [pxf_vs]
    mov bx, dx
    mov ax, [pxf_ky]
    mul word [pxf_vs + 2]
    add bx, ax                      ; BX = my
    cmp bx, [pxf_lmy]
    jne .s
    ret
.s:
    mov [pxf_lmy], bx
    mov ax, [pxf_sa]                ; acc = (sa - ox) hs
    sub ax, [pxf_ox]
    mov cx, ax
    mul word [pxf_hs]
    mov [pxf_acc], ax
    mov [pxf_acc + 2], dx
    mov ax, cx
    mul word [pxf_hs + 2]
    add [pxf_acc + 2], ax
    mov ax, bx                      ; the row: my x mw past the base
    mul word [pxf_mw]
    add ax, [pxf_acc + 2]           ; ...and its first column
    adc dx, 0
    mov si, ax
    and si, 15
    mov cx, 4
.p:
    shr dx, 1
    rcr ax, 1
    loop .p
    mov bx, ax                      ; BX = paragraphs
    call pxf_mseg
    add ax, bx
    push ds
    pop es
    mov di, [pxf_sa]
    mov cx, [pxf_sb]
    sub cx, di
    add di, pxf_S
    mov dx, [pxf_acc]               ; the fraction
    mov bp, [pxf_hs]
    mov bx, [pxf_hs + 2]
    push ds
    mov ds, ax
    cmp bx, 1                       ; exactly 1:1 - a copy
    jne .dda
    or bp, bp
    jne .dda
    rep movsb
    pop ds
    ret
.dda:
    dec bx                          ; (lodsb has stepped one)
.l:
    lodsb
    stosb
    add dx, bp
    adc si, bx
    loop .l
    pop ds
    ret

; pxf_diffuse - pxf_S[xa..xb) into pxf_R by the PLAN DIFFUSER ("Filter
; Lite"): v = T + e_in[x] + carry; c2 when v >= 128, else c1; e = v - 255 or
; v; q = e >> 2 to below-left and below, carry = e - 2q to the right.
;
; ONE ROW OF ERRORS, IN PLACE: word x + 1 holds e_in[x] until pixel x has
; read it, and is then free - so pixel x writes the NEXT row's x - 1 there,
; which is complete by then (q[x-1] from below, q[x] from below-right: BP
; carries the one before). No read-modify-write, no second row, no swap;
; x - 1 = -1 is the guard word. The two paths each run to the loop's end
; rather than meet at a jump
pxf_diffuse:
    mov si, [pxf_xa]
    mov cx, [pxf_xb]
    sub cx, si
    mov di, si
    shl di, 1
    add di, pxf_E0 + 2              ; DI -> e[xa]
    add si, pxf_S
    xor bx, bx
    xor dx, dx                      ; the carry
    xor bp, bp                      ; q of the pixel before
    cmp byte [pxf_kind], 2
    je pxf_diff1
.px:
    lodsb
    mov bl, al
    mov al, [bx + pxf_Tb]           ; T - 128
    cbw
    add ax, [di]
    add ax, dx                      ; v - 128: negative when v < 128
    js .lo
    mov dl, [bx + pxf_c2]
    mov [si + pxf_R - pxf_S - 1], dl
    sub ax, 127                     ; e = v - 255
    mov dx, ax
    sar ax, 1
    sar ax, 1                       ; q
    add bp, ax
    mov [di - 2], bp                ; e_next[x - 1] = q[x - 1] + q[x]
    mov bp, ax
    sub dx, ax
    sub dx, ax                      ; carry = e - 2q
    inc di
    inc di
    loop .px
    jmp short .end
.lo:
    mov dl, [bx + pxf_c1]
    mov [si + pxf_R - pxf_S - 1], dl
    add ax, 128                     ; e = v
    mov dx, ax
    sar ax, 1
    sar ax, 1
    add bp, ax
    mov [di - 2], bp
    mov bp, ax
    sub dx, ax
    sub dx, ax
    inc di
    inc di
    loop .px
.end:
    mov [di - 2], bp                ; e_next[xb - 1] = q[xb - 1]
    ret

; pxf_diff1 - pxf_diffuse's two-colour arm (640x200, the Hercules): the
; colour IS the decision, so there is no plan to look up - the target by
; xlat, the pixel an immediate (registers as pxf_diffuse sets them)
pxf_diff1:
    mov bx, pxf_Tb
.px:
    lodsb
    xlatb                           ; T - 128
    cbw
    add ax, [di]
    add ax, dx
    js .lo
    mov byte [si + pxf_R - pxf_S - 1], 1
    sub ax, 127
    mov dx, ax
    sar ax, 1
    sar ax, 1
    add bp, ax
    mov [di - 2], bp
    mov bp, ax
    sub dx, ax
    sub dx, ax
    inc di
    inc di
    loop .px
    jmp short .end
.lo:
    mov byte [si + pxf_R - pxf_S - 1], 0
    add ax, 128
    mov dx, ax
    sar ax, 1
    sar ax, 1
    add bp, ax
    mov [di - 2], bp
    mov bp, ax
    sub dx, ax
    sub dx, ax
    inc di
    inc di
    loop .px
.end:
    mov [di - 2], bp
    ret

; pxf_order - pxf_S[xa..xb) into pxf_R by the window's ORDERED rule on the
; screen's coordinates: c2 where bayer(y & 7, x & 7) < t
pxf_order:
    mov ax, [pxf_y]                 ; the row's eight thresholds, copied
    and ax, 7
    mov cl, 3
    shl ax, cl
    add ax, pxf_bayer
    mov si, ax
    push ds
    pop es
    mov di, pxf_brow
    mov cx, 4
    rep movsw
    mov si, [pxf_xa]
    mov cx, [pxf_xb]
    sub cx, si
    mov di, si                      ; DI = x
    add si, pxf_S
    xor bx, bx
.px:
    lodsb
    mov bl, al                      ; BX = the index
    mov al, [bx + pxf_t]
    mov bp, di
    and bp, 7
    cmp al, [ds:bp + pxf_brow]      ; t > b: c2
    ja .c2
    mov al, [bx + pxf_c1]
    jmp short .st
.c2:
    mov al, [bx + pxf_c2]
.st:
    mov [di + pxf_R], al
    inc di
    loop .px
    ret

; =============================================================================
; THE GLASS: a row of colours into the mode's memory
; =============================================================================

; pxf_out - pxf_R (row [pxf_y], columns [pxf_jx0] .. [pxf_jx1] of a strip,
; or all) into the mode
pxf_out:
    mov bl, [pxf_mode]
    xor bh, bh
    shl bx, 1
    jmp [pxf_outs + bx]

pxf_outs:
    dw pxf_oX, pxf_o13, pxf_o12, pxf_oD, pxf_o160, pxf_o320, pxf_o1, pxf_o1

; Mode X: four planes, a byte a pixel, four pixels an address
pxf_oX:
    mov es, [pxf_seg]
    mov ax, [pxf_y]
    mov dx, 80
    mul dx
    mov bx, [pxf_jx0]
    shr bx, 1
    shr bx, 1
    add ax, bx
    mov [pxf_vo], ax
    mov bx, [pxf_jx1]               ; the bytes the columns are
    sub bx, [pxf_jx0]
    shr bx, 1
    shr bx, 1
    xor cx, cx
.p:
    mov dx, 0x3C4                   ; map mask: this plane
    mov al, 2
    mov ah, 1
    shl ah, cl
    out dx, ax
    mov si, [pxf_jx0]
    add si, cx
    add si, pxf_R
    mov di, [pxf_vo]
    push cx
    mov cx, bx
.b:
    mov al, [si]
    add si, 4
    stosb
    loop .b
    pop cx
    inc cx
    cmp cx, 4
    jb .p
    mov dx, 0x3C4
    mov ax, 0x0F02                  ; every plane again
    out dx, ax
    ret

; 13h: a byte a pixel
pxf_o13:
    mov es, [pxf_seg]
    mov ax, [pxf_y]
    mov dx, 320
    mul dx
    add ax, [pxf_jx0]
    mov di, ax
    mov si, [pxf_jx0]
    mov cx, [pxf_jx1]
    sub cx, si
    add si, pxf_R
    rep movsb
    ret

; 640x480x16: the row as four planes (pxf_planes), each through its map mask
pxf_o12:
    call pxf_planes
    mov es, [pxf_seg]
    mov ax, [pxf_y]
    mov dx, 80
    mul dx
    mov bx, ax
    xor cx, cx
    mov si, pxf_pl
.p:
    mov dx, 0x3C4
    mov al, 2
    mov ah, 1
    shl ah, cl
    out dx, ax
    mov di, bx
    push cx
    mov cx, 40
    rep movsw
    pop cx
    inc cx
    cmp cx, 4
    jb .p
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    ret

; the Desktop: the row into the band buffer, put up with BLITP (or BLIT4)
; every PXF_BANDN rows, at a gap, and at the slice's end (pxf_flush)
pxf_oD:
    mov al, [pxf_bandn]
    or al, al
    jz .first
    xor ah, ah
    add ax, [pxf_bandy]
    cmp ax, [pxf_y]
    je .add
    call pxf_flush                  ; not the next row: what is held first
.first:
    mov ax, [pxf_y]
    mov [pxf_bandy], ax
.add:
    mov al, [pxf_bandn]
    xor ah, ah
    mov cx, 320
    mul cx
    add ax, pxf_band
    push ax
    cmp byte [pxf_p4], 0
    jne .pk
    call pxf_planes                 ; the four planes, then into the band
    pop di
    push ds
    pop es
    mov si, pxf_pl
    mov cx, 160
    rep movsw
    jmp short .n
.pk:
    pop di                          ; packed pairs, for BLIT4
    push ds
    pop es
    mov si, pxf_R
    mov bx, [pxf_W]
    shr bx, 1
    mov cl, 4
.pp:
    lodsw                           ; AL = the left, AH = the right
    shl al, cl
    or al, ah
    stosb
    dec bx
    jnz .pp
.n:
    inc byte [pxf_bandn]
    cmp byte [pxf_bandn], PXF_BANDN
    jb .x
    call pxf_flush
.x:
    ret

; pxf_flush - the Desktop's band put up, if it holds rows
pxf_flush:
    cmp byte [pxf_mode], PXM_DESK
    jne .x
    mov al, [pxf_bandn]
    or al, al
    jz .x
    xor ah, ah
    mov dx, ax                      ; DX = rows
    mov ax, [pxf_sx]
    mov bx, [pxf_sy]
    add bx, [pxf_bandy]
    mov cx, [pxf_W]
    push cs
    pop es
    mov si, pxf_band
    cmp byte [pxf_p4], 0
    jne .p4
    mov di, 0x4000 | PXF_PLW        ; the plane step (bit 14: walk a clip)
    mov bp, 4 * PXF_PLW             ; the row stride
    call far OSAPI_GFX_BLITP
    jmp short .z
.p4:
    mov bp, 320                     ; a packed row's stride
    call far OSAPI_GFX_BLIT4
.z:
    mov byte [pxf_bandn], 0
.x:
    ret

; pxf_planes - pxf_R (640 colours, 4 bits each) as four planes of 80 at
; pxf_pl: two pixels a LANE by the spread tables (pxview.inc's px_spread),
; four lanes a group turned by the window's BUTTERFLY (px_c2p)
pxf_planes:
    push ds
    pop es
    mov si, pxf_R
    mov di, pxf_lane
    mov cx, 320
    mov bx, pxf_spread
.l:
    lodsw                           ; AL = the even pixel, AH = the odd
    xlatb                           ; its bits at 7, 5, 3, 1
    mov dl, al
    mov al, ah
    xlatb
    shr al, 1                       ; ...the odd's at 6, 4, 2, 0
    or al, dl
    stosb
    loop .l
    mov si, pxf_lane
    mov di, pxf_pl
    mov ch, 80                      ; groups
    mov cl, 4
.g:
    lodsw
    xchg ax, dx                     ; DL, DH = lanes 0, 1
    lodsw                           ; AL, AH = lanes 2, 3
    mov bx, ax                      ; nibbles: 0 <-> 2, 1 <-> 3
    shr bx, cl
    xor bx, dx
    and bx, 0x0F0F
    xor dx, bx
    shl bx, cl
    xor ax, bx
    xchg al, dh
    mov bx, ax                      ; 2-bit fields: 0 <-> 1, 2 <-> 3
    shr bx, 1
    shr bx, 1
    xor bx, dx
    and bx, 0x3333
    xor dx, bx
    shl bx, 1
    shl bx, 1
    xor ax, bx                      ; AH, DH, AL, DL = planes 0, 1, 2, 3
    mov [di], ah
    mov [di + PXF_PLW], dh
    mov [di + 2 * PXF_PLW], al
    mov [di + 3 * PXF_PLW], dl
    inc di
    dec ch
    jnz .g
    ret

; C160: a cell's attribute is its two pixels, left the background nibble
pxf_o160:
    mov es, [pxf_seg]
    mov ax, [pxf_y]
    mov dx, 160
    mul dx
    inc ax
    mov di, ax
    mov si, pxf_R
    mov cx, 80
    mov bx, pxf_hi4
.c:
    lodsw
    xlatb                           ; the left << 4
    or al, ah
    stosb
    inc di
    loop .c
    ret

; 320x200x4: four pixels a byte, two banks
pxf_o320:
    mov es, [pxf_seg]
    call pxf_bank2
    mov si, pxf_R
    mov cx, 80
.b:
    lodsb
    mov ah, al
    lodsb
    shl ah, 1
    shl ah, 1
    or ah, al
    lodsb
    shl ah, 1
    shl ah, 1
    or ah, al
    lodsb
    shl ah, 1
    shl ah, 1
    or al, ah
    stosb
    loop .b
    ret

; 640x200 and the Hercules: eight pixels a byte, lit = 1, the banks
pxf_o1:
    mov es, [pxf_seg]
    cmp byte [pxf_mode], PXM_HERC
    je .h
    call pxf_bank2
    mov cx, 80
    jmp short .go
.h:
    mov ax, [pxf_y]                 ; (y & 3) x 2000h + (y >> 2) x 90
    mov bx, ax
    and bx, 3
    mov cl, 13
    shl bx, cl
    shr ax, 1
    shr ax, 1
    mov dx, 90
    mul dx
    add ax, bx
    mov di, ax
    mov cx, 90
.go:
    mov si, pxf_R
.b:
%rep 8
    lodsb
    shr al, 1
    rcl ah, 1
%endrep
    mov al, ah
    stosb
    loop .b
    ret

; pxf_bank2 - DI = the CGA's row [pxf_y]: (y & 1) x 2000h + (y >> 1) x 80
pxf_bank2:
    mov ax, [pxf_y]
    mov bx, ax
    and bx, 1
    mov cl, 13
    shl bx, cl
    shr ax, 1
    mov dx, 80
    mul dx
    add ax, bx
    mov di, ax
    ret

; =============================================================================
; A 256 MODE'S PAN: what stays MOVES, the strip it exposed is rendered
; =============================================================================

; pxf_shift - AX = columns, BX = rows the picture moved (one of them 0):
; Mode X through the latches (write mode 1, four pixels an address), 13h a
; byte at a time; then the job over the strip it left
pxf_shift:
    mov [pxf_mx], ax
    mov [pxf_my], bx
    cmp byte [pxf_mode], PXM_MODEX
    jne .stride
    mov dx, 0x3CE                   ; write mode 1: the latches' bytes go
    mov al, 5                       ; back where a write lands
    out dx, al
    inc dx
    in al, dx
    mov [pxf_gc5], al
    and al, 0xFC
    or al, 1
    out dx, al
    mov dx, 0x3C4
    mov ax, 0x0F02
    out dx, ax
    mov word [pxf_bpr], 80          ; bytes a row
    mov ax, [pxf_mx]                ; columns are four pixels a byte
    sar ax, 1
    sar ax, 1
    mov [pxf_mx], ax
    jmp short .move
.stride:
    mov word [pxf_bpr], 320
.move:
    push ds
    mov es, [pxf_seg]
    mov cx, [pxf_my]
    or cx, cx
    jnz .rows
    ; --- across: each row's bytes, the right way round ---------------------
    mov bx, [pxf_mx]                ; BX = bytes moved (+ right)
    mov cx, [pxf_bpr]
    mov dx, bx
    or dx, dx
    jns .a1
    neg dx
.a1:
    sub cx, dx                      ; CX = the bytes that stay
    mov [pxf_ycnt], cx
    mov dx, [pxf_H]
    xor bp, bp                      ; BP = the row's offset
    mov ds, [pxf_seg]
.ar:
    push dx
    mov cx, [cs:pxf_ycnt]
    or bx, bx
    js .left
    std                             ; moving right: from the right end
    mov si, bp
    add si, [cs:pxf_bpr]
    dec si
    mov di, si
    sub si, bx
    rep movsb
    cld
    jmp short .an
.left:
    mov di, bp                      ; moving left: from the left
    mov si, bp
    sub si, bx
    rep movsb
.an:
    add bp, [cs:pxf_bpr]
    pop dx
    dec dx
    jnz .ar
    pop ds
    jmp .strip
.rows:                              ; --- down or up: one block move ---------
    mov ax, [pxf_my]
    mov dx, ax
    or dx, dx
    jns .r1
    neg dx
.r1:
    push ax
    mov ax, [pxf_H]
    sub ax, dx
    mul word [pxf_bpr]
    mov cx, ax                      ; CX = the bytes that stay
    pop ax
    imul word [pxf_bpr]             ; AX = the offset they move by
    mov bx, ax
    test byte [pxf_my + 1], 0x80
    mov ds, [pxf_seg]
    jnz .up
    mov si, cx                      ; down: from the end
    dec si
    mov di, si
    add di, bx
    std
    rep movsb
    cld
    pop ds
    jmp short .strip
.up:
    xor di, di
    mov si, bx
    neg si
    rep movsb
    pop ds
.strip:
    cmp byte [pxf_mode], PXM_MODEX
    jne .job
    mov dx, 0x3CE                   ; write mode back as it was
    mov al, 5
    mov ah, [pxf_gc5]
    out dx, ax
.job:
    mov ax, [pxf_my]                ; the rows a vertical move exposed
    or ax, ax
    jz .cols
    js .bot
    mov bx, ax                      ; down: rows 0 .. my
    xor ax, ax
    jmp short .rj
.bot:
    mov bx, [pxf_H]                 ; up: rows H + my .. H
    add ax, bx
.rj:
    xor dl, dl
    call pxf_job
    mov byte [pxf_jkind], 3         ; (a strip: no band state of its own)
    ret
.cols:
    mov ax, [pxf_mx]                ; the columns an across move exposed
    cmp byte [pxf_mode], PXM_MODEX
    jne .c1
    shl ax, 1
    shl ax, 1
.c1:
    push ax
    xor ax, ax
    mov bx, [pxf_H]
    xor dl, dl
    call pxf_job
    mov byte [pxf_jkind], 3
    pop ax
    or ax, ax
    js .cr
    mov [pxf_jx1], ax               ; right: columns 0 .. mx
    ret
.cr:
    add ax, [pxf_W]                 ; left: W + mx .. W
    mov [pxf_jx0], ax
    ret

; =============================================================================
; THE PART'S TABLES
; =============================================================================

; PiXEL's mode -> the kernel's FSXM_* (SPEC.md 53.4); the Desktop sets none
pxf_fsxid:  db FSXM_MODEX, FSXM_VGA13, FSXM_VGA12, 0xFF, FSXM_TEXT80
            db FSXM_CGA320, FSXM_CGA640, FSXM_HERC

; W, H, an, ad by mode (pixelsim's FS_GEOM); the Desktop's are the rect's
pxf_geo:    dw 320, 240, 1, 1
            dw 320, 200, 5, 6
            dw 640, 480, 1, 1
            dw 640, 350, 35, 48
            dw 160, 100, 5, 6
            dw 320, 200, 5, 6
            dw 640, 200, 5, 12
            dw 720, 348, 29, 45

pxf_c160crt:                        ; Clear Skies' six (SPEC.md 88.15.2)
    db  4, 127
    db  5,   6
    db  6, 100
    db  7, 112
    db  9,   1
    db 10, 0x20

; the CGA's RGBI sixteen in the DAC's six bits (os8088's own, brown at 6)
pxf_cga6:
    db  0,  0,  0,   0,  0, 42,   0, 42,  0,   0, 42, 42
    db 42,  0,  0,  42,  0, 42,  42, 21,  0,  42, 42, 42
    db 21, 21, 21,  21, 21, 63,  21, 63, 21,  21, 63, 63
    db 63, 21, 21,  63, 21, 63,  63, 63, 21,  63, 63, 63

; 320x200's foreground sets, in the chooser's order: palette 1, palette 0,
; mode 5 (their low numbers; high is + 8)
pxf_csets:  db 3, 5, 7,   2, 4, 6,   3, 4, 7

; 106.11's steps, 16.16 (pixelsim's ZSTEPS)
pxf_zsteps: dd 8192, 10923, 16384, 21845, 32768, 43691, 65536
            dd 131072, 196608, 262144, 393216, 524288

; a colour's lane half: c's bits 3, 2, 1, 0 at 7, 5, 3, 1 (px_spread)
pxf_spread: db 0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A
            db 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA

pxf_hi4:    db 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70
            db 0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0

pxf_bayer:                          ; the window's 8x8 (pxview.inc's)
    db  0, 32,  8, 40,  2, 34, 10, 42
    db 48, 16, 56, 24, 50, 18, 58, 26
    db 12, 44,  4, 36, 14, 46,  6, 38
    db 60, 28, 52, 20, 62, 30, 54, 22
    db  3, 35, 11, 43,  1, 33,  9, 41
    db 51, 19, 59, 27, 49, 17, 57, 25
    db 15, 47,  7, 39, 13, 45,  5, 37
    db 63, 31, 55, 23, 61, 29, 53, 21

pxf_s_of:   db ' of ', 0
pxf_hint80: db 'F or Esc: leave   Space N P: next, back   + -: zoom'
            db '   M: mode   S: show', 0
pxf_hint40: db 'Esc: leave  N P: next  + -  M: mode', 0
pxf_hint20: db 'Esc: leave  M: mode', 0

; =============================================================================
; THE PART'S OWN STATE AND SCRATCH (its image: zeroes, which pack to nothing)
; =============================================================================
pxf_rec     dw 0                    ; the record shown (px_cur or px_prev)
pxf_dith    db 0                    ; View > Dither: 0 ordered, 1 diffusion
pxf_slon    db 0                    ; a slideshow runs
pxf_vkind   db 0                    ; the display's VID_* (fsx_caps' DL)
pxf_mode    db 0
pxf_kind    db 0                    ; 0 256, 1 plans, 2 two colours
pxf_W       dw 0
pxf_H       dw 0
pxf_an      dw 0
pxf_ad      dw 0
pxf_seg     dw 0                    ; the mode's memory
pxf_sx      dw 0                    ; the Desktop's rect
pxf_sy      dw 0
pxf_p4      db 0                    ; ...BLITP refused: packed and BLIT4
pxf_fsi     times FSI_SIZE db 0
pxf_font    dw 0, 0                 ; the 8x8 glyphs, far
pxf_gfirst  db 0, 0                 ; ...the first and last codes
pxf_mw      dw 0
pxf_mh      dw 0
pxf_npx     dw 0, 0
pxf_z       dw 0, 0
pxf_hs      dw 0, 0
pxf_vs      dw 0, 0
pxf_dw      dw 0, 0
pxf_dh      dw 0, 0
pxf_ox      dw 0
pxf_oy      dw 0
pxf_pox     dw 0
pxf_poy     dw 0
pxf_xa      dw 0
pxf_xb      dw 0
pxf_gnd     db 0
pxf_light   db 0
pxf_lmin    dw 0
pxf_lmax    dw 0
pxf_altdn   db 0
pxf_btn     db 0
pxf_bup     db 0                    ; bands up: 1 the top, 2 the bottom
pxf_bdue    db 0                    ; ...and owed their rows again
pxf_bt0     dw 0
pxf_bt1     dw 0
pxf_pct     dw 0                    ; the progress line, FFFFh none
pxf_hint    dw 0
pxf_jon     db 0                    ; A RENDER owed
pxf_nren    dw 0                    ; ...and those finished (a test's)
pxf_jkind   db 0                    ; 0 whole, 1 top, 2 bottom, 3 a strip
pxf_jy      dw 0
pxf_jy1     dw 0
pxf_jx0     dw 0
pxf_jx1     dw 0
pxf_srow    db 0
pxf_brow    times 8 db 0            ; the ordered rule's row of thresholds
pxf_y       dw 0
pxf_ky      dw 0
pxf_sa      dw 0                    ; the columns a row samples
pxf_sb      dw 0
pxf_lmy     dw 0                    ; the master row last sampled
pxf_acc     dw 0, 0
pxf_vo      dw 0
pxf_mx      dw 0
pxf_my      dw 0
pxf_ycnt    dw 0
pxf_gc5     db 0
pxf_bandn   db 0
pxf_bandy   dw 0
pxf_n0      dw 0
pxf_n1      dw 0
pxf_n2      dw 0
pxf_dv0     dw 0
pxf_dv1     dw 0
pxf_ncol    dw 0                    ; THE COLOURS a plan mode shows
pxf_nbox    dw 0
pxf_bbox    dw 0
pxf_bch     db 0
pxf_bbch    db 0
pxf_brc     db 0
pxf_bm      db 0
pxf_bw      dw 0
pxf_bpr     dw 0, 0
pxf_tg      db 0, 0, 0              ; plan6's target
pxf_dd      db 0, 0, 0
pxf_D       db 0, 0, 0
pxf_K       dw 0
pxf_A       dw 0, 0                 ; 4096 sum w dd^2: c1 alone's error
pxf_wd      db 0, 0, 0
pxf_Sp      dw 0, 0
pxf_E       dw 0, 0
pxf_bE      dw 0, 0
pxf_pt      db 0
pxf_bt      db 0
pxf_pc1     db 0
pxf_pc2     db 0
pxf_pC1     dw 0
pxf_pcs     dw 0
pxf_pcn     dw 0
pxf_pd      dw 0
pxf_cset    db 0                    ; the CGA's chosen set, high, background
pxf_chi     db 0
pxf_cbg     db 0
pxf_chi1    db 0
pxf_cbg1    db 0
pxf_c4i     db 0, 0, 0, 0
pxf_c4      times 12 db 0
pxf_sc      dw 0, 0, 0
pxf_best    dw 0, 0, 0
pxf_bs      times 17 dw 0           ; the cut's boxes: where, how many
pxf_bn      times 17 dw 0
pxf_wts     times 16 dw 0
pxf_cols    times 48 db 0
pxf_c6      times 48 db 0
pxf_cap     times 92 db 0           ; the bottom band's line
    align 2
pxf_Tb      times 256 db 0          ; per palette entry: T - 128...
pxf_t       times 256 db 0          ; ...t...
pxf_c1      times 256 db 0          ; ...and the plan's two
pxf_c2      times 256 db 0
pxf_pal     times 768 db 0          ; the picture's palette
pxf_pch     times 768 db 0          ; ...as three planes, R G B (the cut's)
pxf_p6      times 768 db 0          ; ...in six bits (a 256 mode)
pxf_cnt     times 1024 db 0         ; the histogram's counts
pxf_w       times 512 db 0          ; ...as the cut's weights
pxf_perm    times 256 db 0          ; the cut's points
pxf_tmp     times 256 db 0
pxf_S       times PXF_WMAX + 16 db 0    ; a row's master indices...
pxf_R       times PXF_WMAX + 16 db 0    ; ...its colours
pxf_lane    times 320 db 0
pxf_pl      times 4 * PXF_PLW db 0
pxf_E0      times PXF_WMAX + 2 dw 0     ; the diffuser's row, in place
pxf_ES      times PXF_WMAX + 2 dw 0
pxf_band    times PXF_BANDN * 4 * PXF_PLW db 0
