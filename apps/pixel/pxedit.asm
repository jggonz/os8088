; =============================================================================
; os8088 - apps/pixel/pxedit.asm
;
; PiXEL's EDIT PART (SPEC.md 106.24): part 7 of PIXEL.O88, LINKED against
; the package (build/pxlink.inc, SPEC.md 106.20). It is the arithmetic of
; Image and Effects, and nothing else - it never draws, never touches a file
; and never claims (SPEC.md 20.6 rule 7): the resident fetches it, claims
; what an operation needs, and words every answer.
;
; One vector does the work, DECODE, with a VERB in CL (apps/pixel/pxed.inc):
;
;   EV_INVERT .. EV_LEVELS   a PALETTE operation, on the UI task: [px_pal]'s
;                            256 entries edited in place. No pixel is read
;                            but the histogram's counts (Auto Levels)
;   EV_ROTCW .. EV_PIXEL     a PIXEL operation, on the WORKER: the shown
;                            master into the destination - [px_edseg], or
;                            the master itself when that is 0 - through the
;                            work claim [px_ewseg]; [px_erow] the rows done,
;                            the cancel ([px_abort]) read every row
;   op | EV_WORK             on the UI task, before anything is claimed: the
;                            destination's size into [px_edw] [px_edh] and
;                            AX = the work claim's KB (0 = none)
;
; tools/pixelsim.py's pal_* and op_* are this file in Python, rule for rule,
; and tests/pxedit.py holds the two to the byte. The cube's ordered dither is
; the emitter's (SPEC.md 106.8) - the same 48 pages, its classes and
; thresholds out of pxqtab.inc - at the DESTINATION's own place.
;
; THE MASTER MAY MOVE (SPEC.md 66) while the worker stands in
; OSAPI_TASK_ALIVE, so no pointer into it is held across pe_tick: every row
; re-reads [px_cur + PXR_MSEG]. The destination and the work claim are pinned
; while an operation runs.
; =============================================================================

%include "pxpart.inc"
%include "os88api.inc"
%include "pxrec.inc"
%include "pxed.inc"
%include "pxsvc.inc"
%include "pxlink.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pe_init, pe_decode, pe_none, pe_none, pe_none

QT_SIZE     equ 48 * 256            ; the quantiser's pages (pxmaster.inc's)

pe_init:
    mov ax, PXP_PROBE
    clc
    retf

pe_none:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_DECODE - CL = a verb, DS = the package. Preserves all but AX
pe_decode:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    cmp word [8], PXL_IMAGE         ; THE STAMP (SPEC.md 106.20)
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    cld
    push ax
    mov ax, [px_svgp]               ; the resident's UI services (pxsvc.inc)
    mov [cs:pv_sv], ax
    mov ax, [cs:PXP_PKG]
    mov [cs:pv_sv + 2], ax
    pop ax                          ; (a UI verb's AX is its argument)
    test cl, 0x40
    jz .nui
    call pv_ui                      ; the UI task's half
    jmp short .out
.nui:
    test cl, EV_WORK
    jnz .work
    cmp cl, EV_NPAL
    jb .pal
    cmp cl, EV_N
    jae .bad
    call pe_pixop
    jmp short .out
.pal:
    call pe_palop
    jmp short .out
.work:
    and cl, 0x7F
    cmp cl, EV_N
    jae .bad
    call pe_layout
    jmp short .out
.bad:
    mov ax, PXE_NOTSUP
    stc
    jmp short .out
.link:
    mov ax, PXD_LINK
    stc
.out:
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; =============================================================================
; THE PALETTE OPERATIONS (UI task): each entry of [px_pal] through one
; routine; the ones that need a table build it first
; =============================================================================
pe_palop:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov ax, [cs:pe_pset + bx]
    or ax, ax
    jz .noset
    call ax                         ; the operation's tables
.noset:
    mov ax, [cs:pe_pent + bx]
    mov [cs:pe_fn], ax
    mov si, px_pal
    mov cx, 256
.l:
    call word [cs:pe_fn]
    add si, 3
    loop .l
    xor ax, ax
    clc
    ret

pe_pset:    dw 0, 0, 0, pe_sbc, pe_sgam, pe_spost, 0, pe_slev
pe_pent:    dw pe_einv, pe_egrey, pe_esepia, pe_ebc, pe_elut, pe_elut
            dw pe_ethr, pe_elut

; pe_luma - AL = (77 R + 150 G + 29 B) >> 8 of DS:SI. Clobbers AX, DX
pe_luma:
    mov al, [si]
    mov ah, 77
    mul ah
    mov dx, ax
    mov al, [si + 1]
    mov ah, 150
    mul ah
    add dx, ax
    mov al, [si + 2]
    mov ah, 29
    mul ah
    add ax, dx
    mov al, ah
    ret

; --- each entry: DS:SI = R, G, B. Preserve all but AX, DX -----------------------
pe_einv:
    not byte [si]
    not byte [si + 1]
    not byte [si + 2]
    ret

pe_egrey:
    call pe_luma
    mov [si], al
    mov [si + 1], al
    mov [si + 2], al
    ret

pe_ethr:
    call pe_luma
    cmp al, [px_ep]                 ; at or past the level: white
    mov al, 0
    jb .s
    mov al, 255
.s:
    mov [si], al
    mov [si + 1], al
    mov [si + 2], al
    ret

; Sepia: the classic matrix in 128ths (pixelsim's SEPIA), each sum >> 7 and
; held at 255
pe_esepia:
    push bx
    push cx
    push di
    mov di, pe_sepk
    xor bx, bx
.k:
    xor dx, dx
    mov al, [cs:di]
    mul byte [si]
    add dx, ax
    mov al, [cs:di + 1]
    mul byte [si + 1]
    add dx, ax
    mov al, [cs:di + 2]
    mul byte [si + 2]
    add ax, dx
    mov cl, 7
    shr ax, cl
    cmp ax, 255
    jbe .c
    mov al, 255
.c:
    mov [cs:pe_t3 + bx], al
    add di, 3
    inc bx
    cmp bx, 3
    jb .k
    mov ax, [cs:pe_t3]
    mov [si], ax
    mov al, [cs:pe_t3 + 2]
    mov [si + 2], al
    pop di
    pop cx
    pop bx
    ret
pe_sepk:    db 50, 98, 24, 45, 88, 21, 35, 68, 17

; Brightness and contrast: ((v - 128) f >> 8) + 128 + o, held to 0..255
pe_ebc:
    push bx
    xor bx, bx
.c:
    mov al, [si + bx]
    xor ah, ah
    sub ax, 128
    imul word [cs:pe_f]             ; DX:AX, signed
    mov al, ah                      ; >> 8, a floor: the two's complement
    mov ah, dl                      ; pair's middle word (|result| < 2^15)
    add ax, 128
    add ax, [cs:pe_o]
    jns .p
    xor ax, ax
.p:
    cmp ax, 255
    jbe .s
    mov ax, 255
.s:
    mov [si + bx], al
    inc bx
    cmp bx, 3
    jb .c
    pop bx
    ret

; pe_sbc - f = 25600 div (100 - c) for c >= 0, (256 (100 + c)) div 100 for c
; < 0; o = (|b| x 51) div 20, signed like b
pe_sbc:
    push bx
    push cx
    push dx
    mov ax, [px_ep + 2]             ; c
    or ax, ax
    js .neg
    mov cx, 100
    sub cx, ax
    mov ax, 25600
    xor dx, dx
    div cx
    jmp short .f
.neg:
    add ax, 100                     ; 100 + c, >= 10
    mov cx, 256
    mul cx
    mov cx, 100
    div cx
.f:
    mov [cs:pe_f], ax
    mov ax, [px_ep]                 ; b
    mov bx, ax
    or ax, ax
    jns .bp
    neg ax
.bp:
    mov cx, 51
    mul cx
    mov cx, 20
    div cx
    or bx, bx
    jns .o
    neg ax
.o:
    mov [cs:pe_o], ax
    pop dx
    pop cx
    pop bx
    ret

; the three lookup tables' entry: R, G and B each through its own
pe_elut:
    push bx
    mov bx, pe_lut
    mov al, [si]
    cs xlatb
    mov [si], al
    add bx, 256
    mov al, [si + 1]
    cs xlatb
    mov [si + 1], al
    add bx, 256
    mov al, [si + 2]
    cs xlatb
    mov [si + 2], al
    pop bx
    ret

; pe_sgam - the curve [px_ep]'s 33 words interpolated into the R table, and
; it copied to G and B: v' = min(255, c[i] + ((c[i+1] - c[i]) (v & 7) + 4)
; >> 3), i = v >> 3 (pixelsim's gamma_lut)
pe_sgam:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [px_ep]
    cmp ax, PE_NGAMMA
    jb .g
    mov ax, PE_GAMMA1
.g:
    mov cx, 66
    mul cx
    add ax, pe_gam
    mov si, ax                      ; SI = the curve (CS)
    xor di, di                      ; DI = v
.v:
    mov bx, di
    shr bx, 1
    shr bx, 1
    and bx, 0xFFFE                  ; (v >> 3) x 2
    add bx, si
    mov ax, [cs:bx + 2]
    sub ax, [cs:bx]
    mov cx, di
    and cx, 7
    mul cx
    add ax, 4
    mov cl, 3
    shr ax, cl
    add ax, [cs:bx]
    cmp ax, 255
    jbe .s
    mov ax, 255
.s:
    mov [cs:pe_lut + di], al
    inc di
    cmp di, 256
    jb .v
    call pe_lut3
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pe_lut3 - the R table copied to G and B. Preserves all but AX
pe_lut3:
    push cx
    push si
    push di
    push ds
    push es
    push cs
    pop ds
    push cs
    pop es
    mov si, pe_lut
    mov di, pe_lut + 256
    mov cx, 256
    rep movsw                       ; (256 words: two tables)
    pop es
    pop ds
    pop di
    pop si
    pop cx
    ret

; pe_spost - n = [px_ep] levels: q = (v (n - 1) + 127) div 255, v' = (q 255
; + (n - 1) div 2) div (n - 1)
pe_spost:
    push bx
    push cx
    push dx
    mov cx, [px_ep]
    cmp cx, 2
    jae .n
    mov cx, 2
.n:
    cmp cx, 16
    jbe .n1
    mov cx, 16
.n1:
    dec cx                          ; CX = n - 1
    xor bx, bx
.v:
    mov ax, bx
    mul cx
    add ax, 127
    adc dx, 0
    push cx
    mov cx, 255
    div cx                          ; AX = q
    mul cx                          ; q x 255
    pop cx
    mov dx, cx
    shr dx, 1
    add ax, dx
    xor dx, dx
    div cx
    mov [cs:pe_lut + bx], al
    inc bx
    cmp bx, 256
    jb .v
    call pe_lut3
    pop dx
    pop cx
    pop bx
    ret

; pe_slev - AUTO LEVELS: each channel's 1% and 99% points over the picture's
; pixels, from the shown master's histogram counts (its tail, SPEC.md
; 106.20), and its table stretching them to 0 and 255
pe_slev:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call pe_tail                    ; ES = the counts
    xor di, di                      ; DI = the channel
.ch:
    push di
    mov si, pe_bins                 ; the channel's 256 bins, cleared
    xor ax, ax
    mov cx, 512
.z:
    mov [cs:si], ax
    add si, 2
    loop .z
    xor bx, bx                      ; BX = the entry: its count into the bin
    mov si, px_pal                  ; of its channel's value
    add si, di
.e:
    mov al, [si]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    push si
    mov si, ax
    mov ax, bx
    shl ax, 1
    shl ax, 1
    xchg ax, bx
    mov dx, [es:bx]
    add [cs:pe_bins + si], dx
    mov dx, [es:bx + 2]
    adc [cs:pe_bins + si + 2], dx
    xchg ax, bx
    pop si
    add si, 3
    inc bx
    cmp bx, 256
    jb .e
    ; N, then N div 100 (32-bit), lo_n and hi_n = N - lo_n
    xor ax, ax
    xor dx, dx
    xor bx, bx
.n:
    add ax, [cs:pe_bins + bx]
    adc dx, [cs:pe_bins + bx + 2]
    add bx, 4
    cmp bx, 1024
    jb .n
    push ax
    push dx
    mov cx, 100                     ; (DX:AX) div 100, a word at a time
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx
    mov si, ax                      ; SI = the quotient's high word
    mov ax, bx
    div cx                          ; AX = its low word
    mov [cs:pe_lon], ax
    mov [cs:pe_lon + 2], si
    pop dx
    pop bx                          ; DX:BX = N
    sub bx, ax
    sbb dx, si
    mov [cs:pe_hin], bx
    mov [cs:pe_hin + 2], dx
    ; lo: the first v whose running sum passes lo_n
    xor ax, ax
    xor dx, dx
    xor bx, bx
.lo:
    add ax, [cs:pe_bins + bx]
    adc dx, [cs:pe_bins + bx + 2]
    cmp dx, [cs:pe_lon + 2]
    ja .lof
    jb .lon
    cmp ax, [cs:pe_lon]
    ja .lof
.lon:
    add bx, 4
    cmp bx, 1024
    jb .lo
    xor bx, bx
.lof:
    shr bx, 1
    shr bx, 1
    mov [cs:pe_lo], bl
    ; hi: the first v whose running sum reaches hi_n
    xor ax, ax
    xor dx, dx
    xor bx, bx
.hi:
    add ax, [cs:pe_bins + bx]
    adc dx, [cs:pe_bins + bx + 2]
    cmp dx, [cs:pe_hin + 2]
    ja .hif
    jb .hin
    cmp ax, [cs:pe_hin]
    jae .hif
.hin:
    add bx, 4
    cmp bx, 1024
    jb .hi
    mov bx, 255 * 4
.hif:
    shr bx, 1
    shr bx, 1
    mov [cs:pe_hi], bl
    ; the channel's table
    pop di
    push di
    mov si, di
    mov cl, 8
    shl si, cl                      ; SI = the channel's table
    add si, pe_lut
    xor bx, bx                      ; BL = v
.t:
    mov al, bl
    mov ch, [cs:pe_lo]
    mov cl, [cs:pe_hi]
    cmp cl, ch
    jbe .st                         ; hi <= lo: v as it is
    xor al, al
    cmp bl, ch
    jbe .st
    mov al, 255
    cmp bl, cl
    jae .st
    mov al, bl                      ; (v - lo) 255 div (hi - lo)
    sub al, ch
    mov ah, 255
    mul ah
    sub cl, ch
    div cl
.st:
    mov [cs:si + bx], al
    inc bl
    jnz .t
    pop di
    inc di
    cmp di, 3
    jae .done
    jmp .ch
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pe_tail - ES = the shown master's histogram tail: its segment and its
; pixels' paragraphs (the resident's px_htail). Preserves all but ES
pe_tail:
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_cur + PXR_MW]
    mov bx, [px_cur + PXR_MH]
    mov cx, [px_cur + PXR_MSEG]
    call pe_tailof
    mov es, ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pe_tailof - AX x BX pixels at segment CX: AX = their tail's segment.
; Preserves all but AX, DX
pe_tailof:
    mul bx
    add ax, 15
    adc dx, 0
    push cx
    mov cx, 4
.p:
    shr dx, 1
    rcr ax, 1
    loop .p
    pop cx
    add ax, cx
    ret

; =============================================================================
; THE PIXEL OPERATIONS (the worker)
; =============================================================================

; pe_layout - for verb CL: the destination's size into [px_edw] [px_edh],
; the work claim's regions into the part's own words, and AX = its KB (0 =
; none). CF = 1 the regions do not fit one segment. Preserves all but AX
pe_layout:
    push bx
    push cx
    push dx
    push si
    push di
    mov [cs:pe_op], cl
    mov si, [px_cur + PXR_MW]       ; SI = sw
    mov [cs:pe_sw], si
    mov di, [px_cur + PXR_MH]       ; DI = sh
    mov [px_edw], si                ; the same size, most of them
    mov [px_edh], di
    xor ax, ax                      ; AX = the work's bytes
    cmp cl, EV_ROT180
    jae .r1
    mov [px_edw], di                ; a quarter turn: the sides swap
    mov [px_edh], si
    jmp .done
.r1:
    cmp cl, EV_FLIPV
    jb .done                        ; 180, across: in place, nothing more
    jne .crop
    mov ax, si                      ; down: a row's copy
    jmp .done
.crop:
    cmp cl, EV_CROP
    jne .res
    mov ax, [px_ep + 4]
    sub ax, [px_ep]
    inc ax
    mov [px_edw], ax
    mov ax, [px_ep + 6]
    sub ax, [px_ep + 2]
    inc ax
    mov [px_edh], ax
    xor ax, ax
    jmp .done
.res:
    mov word [cs:pe_o_row], QT_SIZE ; the RGB row out, after the pages
    cmp cl, EV_RESIZE
    jne .conv
    mov ax, si                      ; dw = max(1, sw num div den)
    mul word [px_ep]
    div word [px_ep + 2]
    or ax, ax
    jnz .w1
    inc ax
.w1:
    mov [px_edw], ax
    mov ax, di
    mul word [px_ep]
    div word [px_ep + 2]
    or ax, ax
    jnz .h1
    inc ax
.h1:
    mov [px_edh], ax
    mov ax, [px_edw]                ; the out row: 3 dw
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, QT_SIZE
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_s0], ax            ; a source row's RGB: 3 sw
    mov bx, ax
    mov ax, si
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov bx, [px_ep]
    cmp bx, [px_ep + 2]
    jae .up
    mov [cs:pe_o_xb], ax            ; DOWN: the bounds, dw + 1 words...
    mov bx, ax
    mov ax, [px_edw]
    inc ax
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_acc], ax           ; ...and the sums, 3 dw words
    mov bx, ax
    mov ax, [px_edw]
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    jmp .tot
.up:
    mov [cs:pe_o_s1], ax            ; UP: a second source row...
    mov bx, ax
    mov ax, si
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_xb], ax            ; ...the columns' places, 4 dw bytes...
    mov bx, ax
    mov ax, [px_edw]
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_h0], ax            ; ...and the two interpolated rows, 3 dw
    mov bx, ax                      ; words each
    mov ax, [px_edw]
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov dx, ax
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_h1], ax
    add ax, dx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    jmp .tot
.conv:
    cmp cl, EV_PIXEL
    je .pix
    mov ax, si                      ; three rows of 3 (sw + 2), and the out
    add ax, 2                       ; row
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_rsz], ax
    mov bx, si
    xchg ax, bx
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, QT_SIZE
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_s0], ax
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_s1], ax
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_s2], ax
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    jmp short .tot
.pix:
    mov ax, si                      ; a source row's RGB...
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov bx, ax
    add ax, QT_SIZE
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_s0], ax
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov [cs:pe_o_acc], ax           ; ...and the blocks' sums, 3 words each
    mov bx, ax
    mov ax, si
    add ax, PE_PIXN - 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    mov cl, 3
    shr ax, cl
    call pe_x3
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    shl ax, 1
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
    add ax, bx
    jc .big                         ; (a sum past 64 KB: review-w7 F7)
.tot:
    jc .big
    add ax, 15                      ; the palette, last, on a paragraph
    jc .big
    and ax, 0xFFF0
    mov [cs:pe_o_pal], ax
    add ax, 768
    jc .big
.done:
    push ax
    mov ax, [px_edw]
    mov [cs:pe_dw], ax
    cmp ax, PX_DIMMAX               ; (a destination past 8,192 is refused
    ja .big0                        ; like a source, SPEC.md 106.8)
    cmp word [px_edh], PX_DIMMAX
    ja .big0
    pop ax
    add ax, 1023
    jc .big1
.kb:
    mov cl, 10
    shr ax, cl
    clc
    jmp short .out
.big1:
    mov ax, 64                      ; (a run up to 65,535 bytes: 64 KB)
    clc
    jmp short .out
.big0:
    pop ax
.big:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pe_x3 - AX = 3 AX (CF on overflow). Preserves all else
pe_x3:
    push dx
    mov dx, ax
    shl ax, 1
    jc .o
    add ax, dx
.o:
    pop dx
    ret

; pe_pixop - the operation CL, on the worker. out CF = 0 AX = PXD_OK, or
; CF = 1 AX = PXD_ABORT (cancelled)
pe_pixop:
    call pe_layout                  ; (the same regions EV_WORK priced)
    mov word [px_erow], 0
    mov byte [px_egrey], 0
    mov bl, [cs:pe_op]
    cmp bl, EV_RESIZE
    jb .geo
    call pe_prep                    ; the palette into the work claim; the
.geo:                               ; pages, or GREY
    mov bl, [cs:pe_op]
    sub bl, EV_ROTCW
    xor bh, bh
    shl bx, 1
    call [cs:pe_ops + bx]
    jc .out
    mov bl, [cs:pe_op]              ; THE HISTOGRAM: a permutation keeps its
    cmp bl, EV_CROP                 ; counts (a quarter turn copies them to
    jae .count                      ; its new master); the rest count them
    cmp bl, EV_ROT180
    jae .ok
    call pe_hcopy
    jmp short .ok
.count:
    call pe_hcount
.ok:
    xor ax, ax
    clc
.out:
    ret

pe_ops:     dw pe_rotcw, pe_rotccw, pe_rot180, pe_fliph, pe_flipv, pe_crop
            dw pe_resize, pe_conv, pe_conv, pe_conv, pe_conv, pe_pixel

; --- the rows ---------------------------------------------------------------------

; pe_rowp - AX = a row, BX = a master's segment, CX = its width: DX:SI = the
; row's first byte (SI < 16). Preserves all else
pe_rowp:
    push ax
    push cx
    mul cx
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, bx
    mov dx, ax
    pop cx
    pop ax
    ret

; pe_srow - AX = a row of the SHOWN master: DX:SI. Preserves all else
pe_srow:
    push bx
    push cx
    mov bx, [px_cur + PXR_MSEG]     ; re-read: it may have moved
    mov cx, [px_cur + PXR_MW]
    call pe_rowp
    pop cx
    pop bx
    ret

; pe_drow - AX = a row of the DESTINATION: ES:DI. Preserves all else
pe_drow:
    push bx
    push cx
    push dx
    push si
    mov bx, [px_edseg]
    or bx, bx
    jnz .d
    mov bx, [px_cur + PXR_MSEG]     ; in place
.d:
    mov cx, [px_edw]
    call pe_rowp
    mov es, dx
    mov di, si
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pe_tick - a row done: [px_erow] + 1, liveness, a wake every 3 ticks, and
; the cancel. out CF = 1 AX = PXD_ABORT. Preserves all but AX
pe_tick:
    inc word [px_erow]
    push bx
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE           ; (where a compaction may move the master)
    call OSAPI_GET_TICKS
    mov bx, ax
    sub ax, [px_etick]
    cmp ax, 3
    jb .w
    mov [px_etick], bx
    mov bx, [px_win]
    call OSAPI_WM_WAKE
.w:
    pop bx
    cmp byte [px_abort], 0
    jne .ab
    clc
    ret
.ab:
    mov ax, PXD_ABORT
    stc
    ret

; pe_tickq - the same for an operation done IN PLACE with no copy behind it
; (a flip, the half turn): it is not cancelled half-way. Preserves all
pe_tickq:
    push ax
    push word [px_abort]
    mov byte [px_abort], 0
    call pe_tick
    pop ax
    mov [px_abort], al
    pop ax
    clc
    ret

; --- the geometry: indices moved, the palette untouched ----------------------------

; A QUARTER TURN: destination row y is a COLUMN of the source walked from the
; bottom (clockwise: D[y][x] = S[sh-1-x][y]) or the top (anticlockwise:
; D[y][x] = S[x][sw-1-y]); a step of a source row is sw = 16 q + r bytes,
; taken on a (segment, offset < 16) pair with no division in the loop
pe_rotcw:
    mov byte [cs:pe_dir], 0
    jmp short pe_rot90
pe_rotccw:
    mov byte [cs:pe_dir], 1
pe_rot90:
    xor bp, bp                      ; BP = the destination row
.row:
    cmp bp, [px_edh]
    jb .go
    clc
    ret
.go:
    mov ax, bp
    call pe_drow                    ; ES:DI = it
    mov ax, [px_cur + PXR_MH]       ; the column's first pixel: row sh - 1
    dec ax                          ; (clockwise) or 0, column y or sw-1-y
    mov cx, bp
    cmp byte [cs:pe_dir], 0
    je .cw
    xor ax, ax
    mov cx, [px_cur + PXR_MW]
    dec cx
    sub cx, bp
.cw:
    call pe_srow                    ; DX:SI = that row
    add si, cx                      ; ...its column (one segment: < 8,208)
    mov ax, si
    mov cl, 4
    shr ax, cl
    add dx, ax
    and si, 15
    mov ax, [px_cur + PXR_MW]       ; the step: q paragraphs and r bytes
    mov bx, ax
    and bx, 15
    mov [cs:pe_r], bx
    shr ax, cl
    mov [cs:pe_q], ax
    mov cx, [px_edw]
    push ds
    cmp byte [cs:pe_dir], 0
    jne .down
.up:                                ; clockwise: up the column
    mov ds, dx
    mov al, [si]
    stosb
    sub si, [cs:pe_r]
    jns .u1
    add si, 16
    dec dx
.u1:
    sub dx, [cs:pe_q]
    loop .up
    jmp short .rd
.down:                              ; anticlockwise: down it
    mov ds, dx
    mov al, [si]
    stosb
    add si, [cs:pe_r]
    cmp si, 16
    jb .d1
    sub si, 16
    inc dx
.d1:
    add dx, [cs:pe_q]
    loop .down
.rd:
    pop ds
    inc bp
    call pe_tick
    jnc .row
    ret

; THE HALF TURN, in place: row y and row h-1-y exchanged end for end (the
; middle row of an odd height reversed alone)
pe_rot180:
    xor bp, bp
.row:
    mov ax, [px_cur + PXR_MH]
    dec ax
    sub ax, bp                      ; AX = the partner row
    cmp ax, bp
    jl .done                        ; (signed: past the middle of a picture
                                    ; one row high it is -1)
    je .mid
    call pe_srow
    mov bx, dx                      ; BX:DI = the partner, from its END
    mov di, si
    add di, [px_cur + PXR_MW]
    dec di
    mov ax, bp
    call pe_srow                    ; DX:SI = this row
    mov cx, [px_cur + PXR_MW]
    push ds
    mov es, bx
    mov ds, dx
.x:
    mov al, [si]
    mov ah, [es:di]
    mov [es:di], al
    mov [si], ah
    inc si
    dec di
    loop .x
    pop ds
    jmp short .n
.mid:
    call pe_rev
.n:
    inc bp
    call pe_tickq
    jmp short .row
.done:
    clc
    ret

; pe_rev - AX = a row of the shown master, reversed in place. Preserves all
; but AX
pe_rev:
    push cx
    push dx
    push si
    push di
    call pe_srow
    mov di, si
    add di, [px_cur + PXR_MW]
    dec di
    push ds
    mov ds, dx
.r:
    cmp si, di
    jae .e
    mov al, [si]
    mov ah, [di]
    mov [si], ah
    mov [di], al
    inc si
    dec di
    jmp short .r
.e:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    ret

; ACROSS: every row reversed in place
pe_fliph:
    xor bp, bp
.row:
    cmp bp, [px_cur + PXR_MH]
    jae .done
    mov ax, bp
    call pe_rev
    inc bp
    call pe_tickq
    jmp short .row
.done:
    clc
    ret

; DOWN: row y and row h-1-y exchanged through the work claim's copy of one
pe_flipv:
    xor bp, bp
.row:
    mov ax, [px_cur + PXR_MH]
    dec ax
    sub ax, bp
    cmp ax, bp
    jbe .done
    mov ax, bp                      ; this row -> the copy
    call pe_srow
    mov es, [px_ewseg]
    xor di, di
    call pe_mov
    mov ax, [px_cur + PXR_MH]       ; the partner -> this row
    dec ax
    sub ax, bp
    call pe_srow
    push dx
    push si
    mov ax, bp
    call pe_srow
    mov es, dx
    mov di, si
    pop si
    pop dx
    call pe_mov
    mov ax, [px_cur + PXR_MH]       ; the copy -> the partner
    dec ax
    sub ax, bp
    call pe_srow
    mov es, dx
    mov di, si
    mov dx, [px_ewseg]
    xor si, si
    call pe_mov
    inc bp
    call pe_tickq
    jmp short .row
.done:
    clc
    ret

; pe_mov - DX:SI -> ES:DI, a row of the shown master's width. Preserves all
; but ES
pe_mov:
    push cx
    push si
    push di
    push ds
    mov cx, [px_cur + PXR_MW]
    mov ds, dx
    rep movsb
    pop ds
    pop di
    pop si
    pop cx
    ret

; CROP: destination row y is source row y1 + y from column x1 - forward, so
; in place the destination never passes what it has yet to read
pe_crop:
    xor bp, bp
.row:
    cmp bp, [px_edh]
    jb .go
    clc
    ret
.go:
    mov ax, bp
    call pe_drow                    ; ES:DI
    mov ax, bp
    add ax, [px_ep + 2]
    call pe_srow
    add si, [px_ep]
    mov cx, [px_edw]
    push ds
    mov ds, dx
    rep movsb
    pop ds
    inc bp
    call pe_tick
    jnc .row
    ret

; =============================================================================
; THROUGH THE PALETTE: rows expanded into R, G, B (or the one level of a
; GREY palette), worked on, and quantised back into the cube
; =============================================================================

; pe_prep - before the first row: the palette into the work claim, GREY when
; every entry is a grey (the operations keep a grey a grey - pixelsim's
; grey_pal), else the quantiser's 48 pages at the work claim's base
pe_prep:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [px_ewseg]
    mov di, [cs:pe_o_pal]
    mov si, px_pal
    mov cx, 768 / 2
    rep movsw
    mov si, px_pal                  ; all greys?
    mov cx, 256
    mov byte [px_egrey], 1
.g:
    mov al, [si]
    cmp al, [si + 1]
    jne .col
    cmp al, [si + 2]
    jne .col
    add si, 3
    loop .g
    jmp short .out
.col:
    mov byte [px_egrey], 0
    call pe_qtabs
.out:
    mov al, 3                       ; the bytes a pixel takes in a row
    cmp byte [px_egrey], 0
    je .n
    mov al, 1
.n:
    mov [cs:pe_nc], al
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; pe_qtabs - the 48 pages at the work claim's base: page (class x 3 + c) the
; part of the index byte c contributes at the class's threshold - 42 q, 6 q
; or q in R, G, B order (pxmaster.inc's px_qtabs, with [px_bgr] 0)
pe_qtabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov es, [px_ewseg]
    xor di, di
    xor si, si                      ; SI = the class
.k:
    xor bp, bp
.c:
    mov bx, 0x2A05                  ; R: n 5, x 42
    cmp bp, 1
    jb .k1
    mov bx, 0x0606                  ; G: n 6, x 6
    je .k1
    mov bx, 0x0105                  ; B: n 5, x 1
.k1:
    mov al, [cs:px_qt16 + si]
    call pe_qpage1
    inc bp
    cmp bp, 3
    jb .c
    inc si
    cmp si, 16
    jb .k
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pe_qpage1 - one page at ES:DI (DI on 256): AL = T, BL = n, BH = the
; multiplier: level q from v = ceil((255 q - T) / n). Clobbers AX, CX, DX
pe_qpage1:
    push si
    push bp
    mov ah, 0
    mov bp, ax
    mov si, di
    xor dx, dx
.q:
    inc dl
    cmp dl, bl
    ja .last
    mov al, 255
    mul dl
    sub ax, bp
    push dx
    mov cl, bl
    xor ch, ch
    add ax, cx
    dec ax
    xor dx, dx
    div cx
    pop dx
    add ax, si
    mov cx, ax
    sub cx, di
    mov al, dh
    rep stosb
    add dh, bh
    jmp short .q
.last:
    mov cx, si
    add cx, 256
    sub cx, di
    mov al, dh
    rep stosb
    pop bp
    pop si
    ret

; pe_out - the work claim's RGB row (or levels, GREY) into destination row AX:
; the cube by the ordered dither at that row, or the levels copied. Preserves
; all but ES
pe_out:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call pe_drow                    ; ES:DI
    mov cx, [px_edw]
    mov si, [cs:pe_o_row]
    push ds
    mov ds, [px_ewseg]
    cmp byte [cs:pe_nc], 1
    jne .q
    rep movsb                       ; GREY: the level is the index
    jmp short .x
.q:
    and ax, 15                      ; the matrix row
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov bp, ax
    xor dx, dx                      ; DX = column & 15
.p:
    mov bx, bp
    add bx, dx
    mov bl, [cs:px_qpage + bx]
    mov bh, bl
    xor bl, bl                      ; BX = the class's R page
    lodsb
    xlatb
    mov ah, al
    inc bh
    lodsb
    xlatb
    add ah, al
    inc bh
    lodsb
    xlatb
    add al, ah
    stosb
    inc dx
    and dx, 15
    loop .p
.x:
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pe_expand - source row AX into the work claim at offset BX: each pixel
; pe_nc bytes through the palette (R, G, B, or a GREY palette's one level),
; with one pixel of border either side - the edge pixel again - when DL = 1
; (the row itself then starts pe_nc bytes in). The indices are copied to the
; region's tail first and expanded forwards over themselves: a pixel's bytes
; never reach an index not yet read. Preserves all
pe_expand:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [cs:pe_xbase], bx
    mov [cs:pe_xbord], dl
    mov es, [px_ewseg]
    call pe_srow                    ; DX:SI = the indices
    mov cx, [px_cur + PXR_MW]
    mov al, [cs:pe_xbord]           ; the region: nc (w + 2 border)
    xor ah, ah
    shl ax, 1
    add ax, cx
    push dx
    mov dl, [cs:pe_nc]
    xor dh, dh
    mul dx
    pop dx
    mov di, [cs:pe_xbase]
    add di, ax
    sub di, cx                      ; ...whose last w bytes take the indices
    push di
    push ds
    mov ds, dx
    rep movsb
    pop ds
    pop si                          ; SI = the indices, in the work claim
    mov di, [cs:pe_xbase]
    cmp byte [cs:pe_xbord], 0
    je .nb
    mov al, [cs:pe_nc]
    xor ah, ah
    add di, ax
.nb:
    mov [cs:pe_xstart], di
    mov cx, [px_cur + PXR_MW]
    mov bx, [cs:pe_o_pal]
    push ds
    push es
    pop ds                          ; DS = ES = the work claim
    cmp byte [cs:pe_nc], 1
    je .grey
.rgb:
    lodsb
    xor ah, ah
    mov dx, bx
    add bx, ax
    add bx, ax
    add bx, ax
    mov ax, [bx]
    stosw
    mov al, [bx + 2]
    stosb
    mov bx, dx
    loop .rgb
    jmp short .bord
.grey:
    lodsb
    xor ah, ah
    mov dx, bx
    add bx, ax
    add bx, ax
    add bx, ax
    mov al, [bx]
    stosb
    mov bx, dx
    loop .grey
.bord:
    cmp byte [cs:pe_xbord], 0
    je .done
    mov al, [cs:pe_nc]              ; the last pixel again, after the row...
    xor ah, ah
    mov cx, ax
    mov si, di
    sub si, cx
    rep movsb
    mov cx, ax                      ; ...and the first, before it
    mov si, [cs:pe_xstart]
    mov di, [cs:pe_xbase]
    rep movsb
.done:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the 3 x 3 kernels (pixelsim's KERNELS) -----------------------------------
; Rows y-1, y and y+1 - clamped at the picture's edges - expanded with their
; borders into three slots, row r in slot r mod 3; row y+1 is expanded before
; row y is written, so an operation done in place never reads what it wrote.
; Each output byte is one channel of one pixel, its neighbours pe_nc bytes
; either side: a loop per kernel and per pe_nc, the taps as adds - no
; multiply in any of them

; pe_slotof - AX = a row: BX = its slot's offset. Preserves all else
pe_slotof:
    push ax
    push cx
    push dx
    xor dx, dx
    mov cx, 3
    div cx
    mov bx, dx
    shl bx, 1
    mov bx, [cs:pe_o_s0 + bx]
    pop dx
    pop cx
    pop ax
    ret

pe_conv:
    xor ax, ax
    mov dl, 1
    call pe_slotof
    call pe_expand
    mov word [cs:pe_y], 0
.row:
    mov ax, [cs:pe_y]
    cmp ax, [px_edh]
    jb .go
    clc
    ret
.go:
    inc ax                          ; row y + 1 into its slot, if there is one
    cmp ax, [px_cur + PXR_MH]
    jae .nx
    call pe_slotof
    mov dl, 1
    call pe_expand
    jmp short .p2
.nx:
    dec ax                          ; (clamped: the last row again)
.p2:
    call pe_slotof                  ; THE THREE ROWS: P2 ...
    mov [cs:pe_p2], bx
    mov ax, [cs:pe_y]
    call pe_slotof
    mov [cs:pe_p1], bx
    or ax, ax
    jz .p0
    dec ax
.p0:
    call pe_slotof
    mov [cs:pe_p0], bx
    mov bl, [cs:pe_op]              ; the kernel and the stride
    sub bl, EV_BLUR
    xor bh, bh
    shl bx, 1
    shl bx, 1
    cmp byte [cs:pe_nc], 1
    je .k1
    add bx, 2
.k1:
    mov ax, [cs:pe_ktab + bx]
    mov [cs:pe_fn], ax
    mov al, [cs:pe_nc]              ; the first sample of each: past the
    xor ah, ah                      ; border
    mov si, [cs:pe_p1]
    add si, ax
    mov bx, [cs:pe_p0]
    sub bx, [cs:pe_p1]              ; BX = row y-1 from row y
    mov bp, [cs:pe_p2]
    sub bp, [cs:pe_p1]              ; BP = row y+1 from row y
    mov cx, [px_cur + PXR_MW]
    cmp al, 1
    je .n1
    mov dx, cx
    shl cx, 1
    add cx, dx                      ; CX = the samples
.n1:
    mov di, [cs:pe_o_row]
    push ds
    mov es, [px_ewseg]
    mov ds, [px_ewseg]
    call word [cs:pe_fn]
    pop ds
    mov ax, [cs:pe_y]
    call pe_out
    inc word [cs:pe_y]
    call pe_tick
    jnc .row
    ret

pe_ktab:    dw pe_kblur1, pe_kblur3, pe_ksharp1, pe_ksharp3
            dw pe_kedge1, pe_kedge3, pe_kemb1, pe_kemb3

; the clamp at the end of every kernel: DX (signed) into 0..255, stored
%macro PE_CLAMP 0
    or dx, dx
    jns %%p
    xor dx, dx
%%p:
    cmp dx, 255
    jbe %%s
    mov dx, 255
%%s:
    mov al, dl
    stosb
%endmacro

; Blur: (row-1 + 2 row + row+1 of (left + 2 centre + right) + 8) >> 4
%macro PE_KBLUR 1
    xor ah, ah
%%l:
    mov al, [si + bx - %1]
    mov dx, ax
    mov al, [si + bx]
    add dx, ax
    add dx, ax
    mov al, [si + bx + %1]
    add dx, ax
    mov al, [si - %1]
    add dx, ax
    add dx, ax
    mov al, [si + %1]
    add dx, ax
    add dx, ax
    mov al, [si]
    add dx, ax
    add dx, ax
    add dx, ax
    add dx, ax
    mov al, [ds:si + bp - %1]
    add dx, ax
    mov al, [ds:si + bp]
    add dx, ax
    add dx, ax
    mov al, [ds:si + bp + %1]
    add dx, ax
    add dx, 8
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    mov al, dl
    stosb
    inc si
    loop %%l
    ret
%endmacro

; Sharpen: 5 centre - up - down - left - right
%macro PE_KSHARP 1
    xor ah, ah
%%l:
    mov al, [si]
    mov dx, ax
    shl dx, 1
    shl dx, 1
    add dx, ax
    mov al, [si + bx]
    sub dx, ax
    mov al, [ds:si + bp]
    sub dx, ax
    mov al, [si - %1]
    sub dx, ax
    mov al, [si + %1]
    sub dx, ax
    PE_CLAMP
    inc si
    loop %%l
    ret
%endmacro

; Edge: 8 centre - the eight round it
%macro PE_KEDGE 1
    xor ah, ah
%%l:
    mov al, [si]
    mov dx, ax
    shl dx, 1
    shl dx, 1
    shl dx, 1
    mov al, [si + bx - %1]
    sub dx, ax
    mov al, [si + bx]
    sub dx, ax
    mov al, [si + bx + %1]
    sub dx, ax
    mov al, [si - %1]
    sub dx, ax
    mov al, [si + %1]
    sub dx, ax
    mov al, [ds:si + bp - %1]
    sub dx, ax
    mov al, [ds:si + bp]
    sub dx, ax
    mov al, [ds:si + bp + %1]
    sub dx, ax
    PE_CLAMP
    inc si
    loop %%l
    ret
%endmacro

; Emboss: -2 -1 0 / -1 1 1 / 0 1 2
%macro PE_KEMB 1
    xor ah, ah
%%l:
    mov al, [si]
    mov dx, ax
    mov al, [si + %1]
    add dx, ax
    mov al, [ds:si + bp]
    add dx, ax
    mov al, [ds:si + bp + %1]
    add dx, ax
    add dx, ax
    mov al, [si + bx - %1]
    sub dx, ax
    sub dx, ax
    mov al, [si + bx]
    sub dx, ax
    mov al, [si - %1]
    sub dx, ax
    PE_CLAMP
    inc si
    loop %%l
    ret
%endmacro

pe_kblur1:  PE_KBLUR 1
pe_kblur3:  PE_KBLUR 3
pe_ksharp1: PE_KSHARP 1
pe_ksharp3: PE_KSHARP 3
pe_kedge1:  PE_KEDGE 1
pe_kedge3:  PE_KEDGE 3
pe_kemb1:   PE_KEMB 1
pe_kemb3:   PE_KEMB 3

; --- PIXELATE: blocks of PE_PIXN, each its pixels' average ---------------------
; Every row of a band of blocks is read before any of them is written, so it
; is safe in place
pe_pixel:
    mov word [cs:pe_y], 0           ; the band's first row
.band:
    mov ax, [cs:pe_y]
    cmp ax, [px_cur + PXR_MH]
    jb .go
    clc
    ret
.go:
    add ax, PE_PIXN                 ; ye = min(by + n, h)
    cmp ax, [px_cur + PXR_MH]
    jbe .ye
    mov ax, [px_cur + PXR_MH]
.ye:
    mov [cs:pe_ye], ax
    mov ax, [px_cur + PXR_MW]       ; the blocks across, and their sums
    add ax, PE_PIXN - 1
    mov cl, 3
    shr ax, cl
    mov [cs:pe_nb], ax
    call pe_x3
    mov cx, ax                      ; 3 words a block
    push ds
    mov es, [px_ewseg]
    mov di, [cs:pe_o_acc]
    xor ax, ax
    rep stosw
    pop ds
    mov ax, [cs:pe_y]               ; every row of the band into the sums
.sum:
    cmp ax, [cs:pe_ye]
    jae .avg
    mov bx, [cs:pe_o_s0]
    xor dl, dl
    call pe_expand
    push ax
    push ds
    mov ds, [px_ewseg]
    mov si, [cs:pe_o_s0]
    mov di, [cs:pe_o_acc]
    xor dx, dx                      ; DX = the column
.sx:
    cmp dx, [cs:pe_sw]
    jae .sxd
    mov cl, [cs:pe_nc]
    xor ch, ch
    push di
.sc:
    lodsb
    xor ah, ah
    add [di], ax
    inc di
    inc di
    loop .sc
    pop di
    inc dx
    test dl, PE_PIXN - 1
    jnz .sx
    add di, 6                       ; the next block's sums
    jmp short .sx
.sxd:
    pop ds
    pop ax
    inc ax
    jmp short .sum
.avg:
    ; each block's average: (sum + n div 2) div n, n = its rows x its columns
    push ds
    mov ds, [px_ewseg]
    mov di, [cs:pe_o_acc]
    xor bx, bx                      ; BX = the block's first column
.ab:
    cmp bx, [cs:pe_sw]
    jae .abd
    mov ax, bx                      ; its columns: min(n, w - x)
    add ax, PE_PIXN
    cmp ax, [cs:pe_sw]
    jbe .ac
    mov ax, [cs:pe_sw]
.ac:
    sub ax, bx
    mov cx, [cs:pe_ye]
    sub cx, [cs:pe_y]
    mul cx
    mov cx, ax                      ; CX = n
    push bx
    mov bx, 3
.ach:
    mov ax, cx
    shr ax, 1
    add ax, [di]
    xor dx, dx
    div cx
    mov [di], ax                    ; the average, in the sum's place
    inc di
    inc di
    dec bx
    jnz .ach
    pop bx
    add bx, PE_PIXN
    jmp short .ab
.abd:
    pop ds
    mov ax, [cs:pe_y]               ; the band's rows, written
.wr:
    cmp ax, [cs:pe_ye]
    jae .next
    push ax
    push ds
    mov es, [px_ewseg]
    mov ds, [px_ewseg]
    mov si, [cs:pe_o_acc]
    mov di, [cs:pe_o_row]
    xor dx, dx
.wx:
    cmp dx, [cs:pe_sw]
    jae .wxd
    mov cl, [cs:pe_nc]
    xor ch, ch
    push si
.wc:
    lodsw
    stosb
    loop .wc
    pop si
    inc dx
    test dl, PE_PIXN - 1
    jnz .wx
    add si, 6
    jmp short .wx
.wxd:
    pop ds
    pop ax
    call pe_out
    push ax
    call pe_tick
    pop ax
    jc .ab1
    inc ax
    jmp short .wr
.next:
    mov [cs:pe_y], ax
    jmp .band
.ab1:
    mov ax, PXD_ABORT
    stc
    ret

; --- RESIZE (pixelsim's op_resize) ---------------------------------------------
pe_resize:
    mov ax, [px_ep]
    cmp ax, [px_ep + 2]
    jb pe_rsdown
    jmp pe_rsup

; DOWN: a box of the source a pixel, (sum + n div 2) div n
pe_rsdown:
    mov es, [px_ewseg]              ; the columns' bounds: x sw div dw, x =
                                    ; 0..dw
    mov di, [cs:pe_o_xb]
    xor bx, bx
.xb:
    mov ax, bx
    mul word [px_cur + PXR_MW]
    div word [px_edw]
    stosw
    inc bx
    cmp bx, [px_edw]
    jbe .xb
    mov word [cs:pe_y], 0
.row:
    mov ax, [cs:pe_y]
    cmp ax, [px_edh]
    jb .go
    clc
    ret
.go:
    mul word [px_cur + PXR_MH]      ; y0, y1 = max(y0 + 1, (y + 1) sh div dh)
    div word [px_edh]
    mov [cs:pe_y0], ax
    mov ax, [cs:pe_y]
    inc ax
    mul word [px_cur + PXR_MH]
    div word [px_edh]
    mov dx, [cs:pe_y0]
    inc dx
    cmp ax, dx
    jae .y1
    mov ax, dx
.y1:
    mov [cs:pe_y1], ax
    mov es, [px_ewseg]              ; the sums: 3 words a column
    mov di, [cs:pe_o_acc]
    mov ax, [px_edw]
    call pe_x3
    mov cx, ax
    xor ax, ax
    rep stosw
    mov ax, [cs:pe_y0]
.sr:
    cmp ax, [cs:pe_y1]
    jae .avg
    mov bx, [cs:pe_o_s0]
    xor dl, dl
    call pe_expand
    push ax
    push ds
    mov ds, [px_ewseg]
    mov bx, [cs:pe_o_xb]            ; BX = the bounds
    mov di, [cs:pe_o_acc]           ; DI = the column's sums
    mov cx, [cs:pe_dw]
.sx:
    push cx
    mov ax, [bx]                    ; x0
    mov dx, [bx + 2]                ; x1 = max(x0 + 1, the next bound)
    mov cx, ax
    inc cx
    cmp dx, cx
    jae .x1
    mov dx, cx
.x1:
    sub dx, ax                      ; DX = the columns
    mov si, ax                      ; SI = x0 x nc + the row
    cmp byte [cs:pe_nc], 1
    je .s1
    call pe_x3
    mov si, ax
.s1:
    add si, [cs:pe_o_s0]
.sxx:
    mov cl, [cs:pe_nc]
    xor ch, ch
    push di
.sc:
    lodsb
    xor ah, ah
    add [di], ax
    inc di
    inc di
    loop .sc
    pop di
    dec dx
    jnz .sxx
    add di, 6
    add bx, 2
    pop cx
    loop .sx
    pop ds
    pop ax
    inc ax
    jmp short .sr
.avg:
    push ds                         ; n = (x1 - x0)(y1 - y0); the averages
    mov es, [px_ewseg]              ; into the out row
    mov ds, [px_ewseg]
    mov bx, [cs:pe_o_xb]
    mov si, [cs:pe_o_acc]
    mov di, [cs:pe_o_row]
    mov cx, [cs:pe_dw]
.ax:
    push cx
    mov ax, [bx]
    mov dx, [bx + 2]
    mov cx, ax
    inc cx
    cmp dx, cx
    jae .ax1
    mov dx, cx
.ax1:
    sub dx, ax
    mov ax, [cs:pe_y1]
    sub ax, [cs:pe_y0]
    mul dx
    mov cx, ax                      ; CX = n
    push bx
    mov bl, [cs:pe_nc]
.ach:
    mov ax, cx
    shr ax, 1
    add ax, [si]
    xor dx, dx
    div cx
    stosb
    inc si
    inc si
    dec bl
    jnz .ach
    pop bx
    cmp byte [cs:pe_nc], 1          ; (the sums are 3 words a column)
    jne .an
    add si, 4
.an:
    add bx, 2
    pop cx
    loop .ax
    pop ds
    mov ax, [cs:pe_y]
    call pe_out
    inc word [cs:pe_y]
    call pe_tick
    jc .r
    jmp .row
.r:
    ret

; UP: bilinear in 8.8 - each column's (xa, its neighbour, wx) once, each
; source row pair interpolated across once into two rows of words, then a
; row of the destination is those two blended down: (256 top + (bot - top)
; wy + 32768) >> 16, the difference's sign taken first so every multiply
; is unsigned
pe_rsup:
    mov ax, [px_cur + PXR_MW]       ; sx = (sw << 8) div dw
    mov dx, ax
    mov cl, 8
    shl ax, cl
    mov cl, 8
    shr dx, cl
    div word [px_edw]
    mov [cs:pe_sx], ax
    mov ax, [px_cur + PXR_MH]
    mov dx, ax
    mov cl, 8
    shl ax, cl
    shr dx, cl
    div word [px_edh]
    mov [cs:pe_sy], ax
    mov es, [px_ewseg]              ; the columns: xa (word), wx, the step
    mov di, [cs:pe_o_xb]            ; to the neighbour (0 or 1)
    xor bx, bx
.xt:
    mov ax, bx
    mov cx, [cs:pe_sx]
    mov dx, [px_cur + PXR_MW]
    call pe_place                   ; AX = i0, CL = w, CH = the step
    stosw
    mov ax, cx
    stosw
    inc bx
    cmp bx, [px_edw]
    jb .xt
    mov word [cs:pe_ya], 0xFFFF
    mov word [cs:pe_y], 0
.row:
    mov ax, [cs:pe_y]
    cmp ax, [px_edh]
    jb .go
    clc
    ret
.go:
    mov cx, [cs:pe_sy]
    mov dx, [px_cur + PXR_MH]
    call pe_place
    mov [cs:pe_wy], cl
    cmp ax, [cs:pe_ya]
    je .blend
    mov [cs:pe_ya], ax              ; a new pair of rows: interpolated across
    mov bx, [cs:pe_o_s0]
    xor dl, dl
    call pe_expand
    mov di, [cs:pe_o_h0]
    call pe_hrow
    add al, ch                      ; the second: the neighbour row
    adc ah, 0
    mov bx, [cs:pe_o_s0]
    xor dl, dl
    call pe_expand
    mov di, [cs:pe_o_h1]
    call pe_hrow
.blend:
    push ds
    mov es, [px_ewseg]
    mov ds, [px_ewseg]
    mov si, [cs:pe_o_h0]
    mov bx, [cs:pe_o_h1]
    mov di, [cs:pe_o_row]
    mov cx, [cs:pe_dw]
    cmp byte [cs:pe_nc], 1
    je .bl
    mov ax, cx
    shl cx, 1
    add cx, ax
.bl:
    push cx
    mov ax, [si]                    ; top
    mov cx, [bx]                    ; bot
    push bx
    push si
    mov si, ax                      ; SI = top
    mov bl, [cs:pe_wy]
    xor bh, bh
    cmp cx, ax
    jb .neg
    sub cx, ax                      ; (bot - top) wy, plus 256 top
    mov ax, cx
    mul bx
    add ax, 32768
    adc dx, 0
    mov cx, si
    xchg cl, ch                     ; 256 top: its low byte up, its high
    mov bl, cl                      ; byte the high word's
    and cx, 0xFF00
    xor bh, bh
    add ax, cx
    adc dx, bx
    jmp short .st
.neg:
    sub ax, cx                      ; 256 top - (top - bot) wy
    mul bx
    mov cx, si
    xchg cl, ch
    mov bl, cl
    and cx, 0xFF00
    xor bh, bh                      ; BX:CX = 256 top
    sub cx, ax
    sbb bx, dx
    add cx, 32768
    adc bx, 0
    mov dx, bx
.st:
    mov al, dl
    stosb
    pop si
    pop bx
    add si, 2
    add bx, 2
    pop cx
    loop .bl
    pop ds
    mov ax, [cs:pe_y]
    call pe_out
    inc word [cs:pe_y]
    call pe_tick
    jc .r
    jmp .row
.r:
    ret

; pe_place - AX = i, CX = the step (8.8), DX = the source's side: AX = i0,
; CL = the weight, CH = 1 when the neighbour is i0 + 1 (0 at the last
; pixel): f = (step (2 i + 1) >> 1) - 128, at least 0 (pixelsim's place).
; Preserves all else
pe_place:
    push bx
    push dx
    mov bx, dx                      ; BX = the side
    shl ax, 1
    inc ax
    mul cx                          ; DX:AX = step (2 i + 1)
    shr dx, 1
    rcr ax, 1
    sub ax, 128
    sbb dx, 0
    jns .p
    xor ax, ax
    xor dx, dx
.p:
    mov cl, al                      ; the weight
    mov al, ah                      ; i0 = f >> 8
    mov ah, dl
    dec bx
    cmp ax, bx
    jb .in
    mov ax, bx                      ; past the last: it, alone, weight 0
    xor cx, cx
    jmp short .out
.in:
    mov ch, 1
.out:
    pop dx
    pop bx
    ret

; pe_hrow - the expanded row at pe_o_s0 interpolated across into words at
; DI: for each column (xa, w, step) and channel, 256 a + (b - a) w, its sign
; first. Preserves all
pe_hrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    mov es, [px_ewseg]
    mov ds, [px_ewseg]
    mov bp, [cs:pe_o_xb]
    mov cx, [cs:pe_dw]
.x:
    push cx
    mov ax, [ds:bp]                 ; xa
    mov bl, [ds:bp + 2]             ; w
    mov bh, [ds:bp + 3]             ; the step: 0 or 1 pixel
    mov si, ax
    cmp byte [cs:pe_nc], 1
    je .s1
    call pe_x3
    mov si, ax
    mov al, bh                      ; the step in bytes
    mov ah, 3
    mul ah
    mov bh, al
.s1:
    add si, [cs:pe_o_s0]
    mov cl, [cs:pe_nc]
    xor ch, ch
.c:
    mov al, [si]                    ; a
    mov dl, bh
    xor dh, dh
    push si
    add si, dx
    mov ah, [si]                    ; b
    pop si
    mov dl, al                      ; DX = 256 a
    xor dh, dh
    xchg dl, dh
    cmp ah, al
    jb .ng
    sub ah, al
    mov al, ah
    mul bl
    add ax, dx
    jmp short .w
.ng:
    sub al, ah
    mul bl
    sub dx, ax
    mov ax, dx
.w:
    stosw
    inc si
    loop .c
    add bp, 4
    pop cx
    loop .x
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the histogram's counts in the new master's tail (SPEC.md 106.20) -----------

; pe_dtail - ES = the destination's tail. Preserves all but ES
pe_dtail:
    push ax
    push bx
    push cx
    push dx
    mov ax, [px_edw]
    mov bx, [px_edh]
    mov cx, [px_edseg]
    or cx, cx
    jnz .d
    mov cx, [px_cur + PXR_MSEG]
.d:
    call pe_tailof
    mov es, ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pe_hcopy - a quarter turn: the shown master's counts are the new one's
pe_hcopy:
    push ax
    push cx
    push si
    push di
    push ds
    call pe_tail
    push es
    call pe_dtail
    pop ds
    xor si, si
    xor di, di
    mov cx, 512
    rep movsw
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; pe_hcount - the destination's 256 counts, a dword each. Preserves all
pe_hcount:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call pe_dtail
    xor di, di
    mov cx, 512
    xor ax, ax
    rep stosw
    xor bp, bp
.row:
    cmp bp, [px_edh]
    jae .done
    push es
    mov ax, bp
    call pe_drow                    ; ES:DI = the row
    mov dx, es
    pop es
    mov si, di
    mov cx, [px_edw]
    push ds
    mov ds, dx
    xor bh, bh
.p:
    lodsb
    mov bl, al
    shl bx, 1
    shl bx, 1
    add word [es:bx], 1
    adc word [es:bx + 2], 0
    xor bh, bh
    loop .p
    pop ds
    inc bp
    jmp short .row
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "pxqtab.inc"               ; px_qpage, px_qt16: the emitter's own
%include "pxgam.inc"                ; pe_gam: the gammas' curves (generated)

; --- the part's own words (through CS) ----------------------------------------------
pe_fn:      dw 0
pe_f:       dw 0
pe_o:       dw 0
pe_t3:      db 0, 0, 0, 0
pe_lo:      db 0
pe_hi:      db 0
pe_lon:     dd 0
pe_hin:     dd 0
pe_op:      db 0
pe_nc:      db 0
pe_dir:     db 0
pe_wy:      db 0
pe_xbord:   db 0, 0
pe_q:       dw 0
pe_r:       dw 0
pe_y:       dw 0
pe_y0:      dw 0
pe_y1:      dw 0
pe_ye:      dw 0
pe_ya:      dw 0
pe_nb:      dw 0
pe_sx:      dw 0
pe_sy:      dw 0
pe_p0:      dw 0
pe_p1:      dw 0
pe_p2:      dw 0
pe_rsz:     dw 0
pe_xbase:   dw 0
pe_xstart:  dw 0
pe_o_row:   dw 0
pe_o_s0:    dw 0
pe_o_s1:    dw 0
pe_o_s2:    dw 0
pe_o_xb:    dw 0
pe_o_acc:   dw 0
pe_o_h0:    dw 0
pe_o_h1:    dw 0
pe_o_pal:   dw 0
pe_sw:      dw 0                    ; the source's width and the
pe_dw:      dw 0                    ; destination's, where DS is not ours
pe_lut:     times 768 db 0
pe_bins:    times 1024 db 0

; =============================================================================
; THE UI TASK'S HALF (SPEC.md 106.24): the parameter cards - Brightness/
; Contrast, Gamma, Posterize, Threshold, Resize and Save As's format - and
; the XMS copy undo makes for an operation done in place. The resident
; keeps [px_pcon] and dispatches; the card's records, its layout, its
; drawing, its keys and its buttons are here, reaching the resident's
; drawing and layout through pxsvc.inc's gate - so they cost the resident
; package nothing while no card is up (SPEC.md 106.20's budget).
; =============================================================================

PV_PCW      equ 240                 ; the card: its width...
PV_PCROW    equ 16                  ; ...a row's pitch
PV_PCVAL    equ 8                   ; ...a value's cells
PF_SIGNED   equ 0                   ; a row's value, shown
PF_PLAIN    equ 1
PF_GAMMA    equ 2                   ; an index into the gammas
PF_SCALE    equ 3                   ; an index into the resize steps
PCR_LAB     equ 0                   ; a row: label, min, max, step, default,
PCR_MIN     equ 2                   ; format
PCR_MAX     equ 4
PCR_STEP    equ 6
PCR_DEF     equ 8
PCR_FMT     equ 10
PCR_SZ      equ 12

; pv_ui - CL & 3Fh = a UI verb. DS = the package
pv_ui:
    mov ax, [px_earg]               ; the verb's argument (px_ecall's)
    mov bl, cl
    and bl, 0x3F
    xor bh, bh
    shl bx, 1
    jmp [cs:pv_tab + bx]
pv_tab:     dw pv_open, pv_lay, pv_draw, pv_key, pv_fire, pv_ok, pv_room
            dw pv_xout, pv_xin, pf_pal, pf_start, pf_fin, pf_undo

; per card: its title, its rows, then a row each
pv_cards:   dw pv_c_bc, pv_c_gam, pv_c_post, pv_c_thr, pv_c_res, pv_c_save
pv_c_bc:    dw pv_t_bc
            db 2
            dw pv_s_bri, -100, 100, 10, 0, PF_SIGNED
            dw pv_s_con, -90, 90, 10, 0, PF_SIGNED
pv_c_gam:   dw pv_t_gam
            db 1
            dw pv_t_gam, 0, PE_NGAMMA - 1, 1, PE_GAMMA1, PF_GAMMA
pv_c_post:  dw pv_t_post
            db 1
            dw pv_s_lev, 2, 16, 1, 4, PF_PLAIN
pv_c_thr:   dw pv_t_thr
            db 1
            dw pv_s_lvl, 8, 248, 8, 128, PF_PLAIN
pv_c_res:   dw pv_t_res
            db 1
            dw pv_s_scl, 0, 8, 1, 2, PF_SCALE
pv_c_save:  dw pv_t_save
            db 0
pv_t_bc:    db 'Brightness/Contrast', 0
pv_t_gam:   db 'Gamma', 0
pv_t_post:  db 'Posterize', 0
pv_t_thr:   db 'Threshold', 0
pv_t_res:   db 'Resize', 0
pv_t_save:  db 'Save As', 0
pv_s_bri:   db 'Brightness', 0
pv_s_con:   db 'Contrast', 0
pv_s_lev:   db 'Levels', 0
pv_s_lvl:   db 'Level', 0
pv_s_scl:   db 'Scale', 0
pv_s_fmt:   db 'Format', 0
pv_s_needs: db 'Needs ', 0
pv_s_k:     db 'K', 0
pv_s_semi:  db '; ', 0
pv_s_kfree: db 'K free', 0
pv_s_x:     db ' x ', 0
pv_s_nd:    db ' needs ', 0
pv_gam100:  dw 30, 40, 50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200
            dw 250, 300
pv_rsnum:   db 1, 1, 1, 2, 3, 3, 2, 3, 4    ; pixelsim's RESIZE_STEPS
pv_rsden:   db 4, 3, 2, 3, 4, 2, 1, 1, 1
pv_onm:     db 'Invert', 0, 'Greyscale', 0, 'Sepia', 0, 'Brightness', 0
            db 'Gamma', 0, 'Posterize', 0, 'Threshold', 0, 'Auto Levels', 0
            db 'Rotate CW', 0, 'Rotate CCW', 0, 'Rotate 180', 0
            db 'Flip Across', 0, 'Flip Down', 0, 'Crop', 0, 'Resize', 0
            db 'Blur', 0, 'Sharpen', 0, 'Edge Detect', 0, 'Emboss', 0
            db 'Pixelate', 0

; --- the part's string helpers: its own words into the package's -------------

; pv_cstr - CS:SI copied to DS:px_cline; SI = px_cline. Preserves all but SI
pv_cstr:
    push di
    mov di, px_cline
    mov byte [di], 0
    call pv_cat
    mov si, px_cline
    pop di
    ret

; pv_cat - CS:SI appended to the DS string DI is in; DI left on its NUL.
; Preserves all but DI
pv_cat:
    push ax
    push si
.e:
    cmp byte [di], 0
    je .c
    inc di
    jmp short .e
.c:
    mov al, [cs:si]
    mov [di], al
    or al, al
    jz .x
    inc si
    inc di
    jmp short .c
.x:
    pop si
    pop ax
    ret

; pv_rec - SI = the card's record (CS). Preserves all but SI
pv_rec:
    push bx
    mov bl, [px_pcon]
    xor bh, bh
    dec bx
    shl bx, 1
    mov si, [cs:pv_cards + bx]
    pop bx
    ret

; pv_row - DI = a row: SI = its record (CS). Preserves all but SI
pv_row:
    push ax
    push dx
    call pv_rec
    mov ax, PCR_SZ
    mul di
    add si, ax
    add si, 3
    pop dx
    pop ax
    ret

; pv_open - EV_UOPEN: the card's rows at their defaults
pv_open:
    call pv_rec
    mov ax, [cs:si + 3 + PCR_DEF]
    mov [px_pcv], ax
    mov ax, [cs:si + 3 + PCR_SZ + PCR_DEF]
    mov [px_pcv + 2], ax
    clc
    ret

; pv_rect - AX/BX/CX/DX = the card's rect: PV_PCW across, its rows' height
; down, centred in the content on the byte grid. Preserves all else
pv_rect:
    push si
    call pv_rec
    mov dl, [cs:si + 2]
    xor dh, dh
    cmp byte [px_pcon], PC_SAVE
    jne .r
    inc dx                          ; (the format's row)
.r:
    mov ax, PV_PCROW
    mul dx
    add ax, 52
    mov bx, [px_h]
    sub bx, ax
    sar bx, 1
    jns .y
    xor bx, bx
.y:
    add bx, [px_cy0]
    mov dx, bx
    add dx, ax
    dec dx
    mov ax, [px_w]
    sub ax, PV_PCW
    sar ax, 1
    jns .x
    xor ax, ax
.x:
    add ax, [px_cx0]
    and ax, 0xFFF8
    mov cx, ax
    add cx, PV_PCW - 1
    pop si
    ret

; pv_lay - EV_ULAY: from px_lay_buttons while a card is up - every button
; under the card hidden, the card's own laid out, Save As's drop-down placed
pv_lay:
    call pv_rect
    mov [px_pcr], ax
    mov [px_pcr + 2], bx
    mov [px_pcr + 4], cx
    mov [px_pcr + 6], dx
    xor si, si
.h:
    PSV SV_GETRECT
    cmp ax, [px_pcr + 4]
    jg .keep
    cmp cx, [px_pcr]
    jl .keep
    cmp bx, [px_pcr + 6]
    jg .keep
    cmp dx, [px_pcr + 2]
    jl .keep
    mov ax, 1
    mov bx, 1
    xor cx, cx
    xor dx, dx
    PSV SV_SETRECT
.keep:
    inc si
    cmp si, PX_B_C1M
    jb .h
    call pv_rec
    mov cl, [cs:si + 2]             ; the rows' - and +
    xor ch, ch
    mov di, PX_B_C1M
    mov bx, [px_pcr + 2]
    add bx, 22
.row:
    jcxz .btn
    push cx
    mov ax, [px_pcr]
    add ax, 112
    mov cx, ax
    add cx, 15
    mov dx, bx
    add dx, 11
    mov si, di
    PSV SV_SETRECT
    add ax, 96
    add cx, 96
    inc si
    PSV SV_SETRECT
    add di, 2
    add bx, PV_PCROW
    pop cx
    dec cx
    jmp short .row
.btn:
    mov bx, [px_pcr + 6]            ; OK and Cancel, at the bottom
    sub bx, 21
    mov dx, bx
    add dx, 14
    mov ax, [px_pcr]
    add ax, 48
    mov cx, ax
    add cx, 63
    mov si, PX_B_COK
    PSV SV_SETRECT
    add ax, 80
    add cx, 80
    inc si
    PSV SV_SETRECT
    cmp byte [px_pcon], PC_SAVE     ; Save As's drop-down, across its row
    jne .x
    mov ax, [px_pcr]
    add ax, 72
    mov cx, [px_pcr + 4]
    sub cx, 12
    mov bx, [px_pcr + 2]
    add bx, 22
    mov dx, bx
    add dx, 11
    mov [px_fdrop + PE_DR_RECT], ax
    mov [px_fdrop + PE_DR_RECT + 2], bx
    mov [px_fdrop + PE_DR_RECT + 4], cx
    mov [px_fdrop + PE_DR_RECT + 6], dx
    mov ax, [px_win]
    mov [px_fdrop + PE_DR_WIN], ax
.x:
    clc
    ret

; pv_draw - EV_UDRAW: the card whole - a frame, its body, the title, each
; row's label and value, the buttons, the drop-down - from the painter,
; last, over the regions (px_cards, the clip armed)
pv_draw:
    mov ax, [px_pcr]
    mov bx, [px_pcr + 2]
    mov cx, [px_pcr + 4]
    mov dx, [px_pcr + 6]
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    inc ax
    inc bx
    dec cx
    dec dx
    push ax
    mov al, [px_c_body]
    mov [px_pc], al
    pop ax
    PSV SV_FILL
    call pv_rec                     ; the title, centred
    push si
    mov si, [cs:si]
    call pv_cstr
    mov bx, [px_pcr + 2]
    add bx, 6
    mov dx, bx
    mov ax, [px_pcr]
    add ax, 8
    mov cx, [px_pcr + 4]
    sub cx, 8
    mov di, 1
    call pv_band
    pop si
    mov cl, [cs:si + 2]             ; the rows' labels and values
    xor ch, ch
    xor di, di
.r:
    cmp di, cx
    jae .save
    call pv_label
    call pv_val
    inc di
    jmp short .r
.save:
    cmp byte [px_pcon], PC_SAVE
    jne .btn
    xor di, di
    call pv_label
    mov bx, px_fdrop
    PSV SV_DROP
.btn:
    mov si, PX_B_C1M
.b:
    PSV SV_GETRECT
    cmp ax, cx
    jg .bn
    PSV SV_BTN
.bn:
    inc si
    cmp si, PX_NBTN
    jb .b
    cmp byte [px_pcon], PC_RES      ; Resize's second line
    jne .x
    call pv_res
.x:
    clc
    ret

; pv_band - px_tband in the card's colours: AX..CX at row DX, the package's
; SI, DI = 0 left / 1 centred. Preserves all
pv_band:
    push bx
    mov bl, [px_c_text]
    mov bh, [px_c_body]
    PSV SV_TBAND
    pop bx
    ret

; pv_rowy - DI = a row: BX = its text's top. Preserves all else
pv_rowy:
    push ax
    push dx
    mov ax, PV_PCROW
    mul di
    mov bx, [px_pcr + 2]
    add bx, 24
    add bx, ax
    pop dx
    pop ax
    ret

; pv_label - row DI's label. Preserves all
pv_label:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pv_rowy
    mov si, pv_s_fmt                ; Save As's row is the format's
    cmp byte [px_pcon], PC_SAVE
    je .t
    call pv_row
    mov si, [cs:si + PCR_LAB]
.t:
    call pv_cstr
    mov ax, [px_pcr]
    add ax, 8
    mov cx, ax
    add cx, 103
    mov dx, bx
    xor di, di
    call pv_band
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pv_val - row DI's value, centred in its cells between its - and +.
; Preserves all
pv_val:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pv_fmt
    call pv_rowy
    mov ax, [px_pcr]
    add ax, 136
    mov cx, ax
    add cx, PV_PCVAL * 8 - 1
    mov dx, bx
    mov si, px_cline
    mov di, 1
    call pv_band
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pv_fmt - row DI's value into px_cline in its row's format. Preserves all
pv_fmt:
    push ax
    push bx
    push dx
    push si
    push di
    call pv_row
    mov al, [cs:si + PCR_FMT]
    shl di, 1
    mov bx, [px_pcv + di]
    mov di, px_cline
    cmp al, PF_GAMMA
    je .gam
    cmp al, PF_SCALE
    je .scl
    mov ah, al
    mov al, '+'
    mov [di], al
    mov ax, bx                      ; an integer, its sign when signed
    cmp byte [cs:si + PCR_FMT], PF_SIGNED
    jne .u
    or ax, ax
    jz .u
    jns .p
    mov byte [di], '-'
    neg ax
.p:
    inc di
.u:
    xor dx, dx
    PSV SV_U32
    jmp short .x
.gam:
    shl bx, 1                       ; "1.40"
    mov ax, [cs:pv_gam100 + bx]
    mov bl, 100
    div bl
    push ax
    add al, '0'
    mov [di], al
    mov byte [di + 1], '.'
    pop ax
    mov al, ah
    xor ah, ah
    mov bl, 10
    div bl
    add ax, '00'
    mov [di + 2], ax
    mov byte [di + 4], 0
    jmp short .x
.scl:
    mov al, [cs:pv_rsnum + bx]      ; "2/3", or "2" when the denominator is 1
    add al, '0'
    mov [di], al
    inc di
    mov al, [cs:pv_rsden + bx]
    cmp al, 1
    je .z
    mov byte [di], '/'
    add al, '0'
    mov [di + 1], al
    inc di
    inc di
.z:
    mov byte [di], 0
.x:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; pv_res - Resize's second line: "320 x 240", or - no run for it - "Needs
; 300K; 120K free" and OK greyed for the fact. Preserves all
pv_res:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pv_resdim                  ; [px_edw] [px_edh], AX = its KB
    mov si, ax
    call OSAPI_MEM_AVAIL
    mov di, px_cline
    cmp ax, si
    jb .no
    mov ax, [px_edw]
    xor dx, dx
    PSV SV_U32
    mov si, pv_s_x
    call pv_cat
    mov ax, [px_edh]
    xor dx, dx
    PSV SV_U32N
    and word [px_bflags + 2 * PX_B_COK], ~PE_DIS
    jmp short .d
.no:
    push ax
    mov byte [di], 0
    mov si, pv_s_needs
    call pv_cat
    call pv_resdim
    xor dx, dx
    PSV SV_U32N
    mov si, pv_s_k
    call pv_cat
    mov si, pv_s_semi
    call pv_cat
    pop ax
    xor dx, dx
    PSV SV_U32N
    mov si, pv_s_kfree
    call pv_cat
    or word [px_bflags + 2 * PX_B_COK], PE_DIS
.d:
    mov di, 1
    call pv_rowy
    mov ax, [px_pcr]
    add ax, 8
    mov cx, [px_pcr + 4]
    sub cx, 8
    mov dx, bx
    mov si, px_cline
    mov di, 1
    call pv_band
    mov si, PX_B_COK                ; OK, as the fact makes it
    PSV SV_BTN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pv_resdim - Resize's chosen step: [px_ep] num, [px_ep + 2] den, the
; destination's size into [px_edw] [px_edh] (pixelsim's resize_dims), AX =
; its claim's KB. Preserves all but AX
pv_resdim:
    push bx
    push dx
    mov bx, [px_pcv]
    mov al, [cs:pv_rsnum + bx]
    xor ah, ah
    mov [px_ep], ax
    mov al, [cs:pv_rsden + bx]
    mov [px_ep + 2], ax
    mov ax, [px_cur + PXR_MW]
    call .dim
    mov [px_edw], ax
    mov ax, [px_cur + PXR_MH]
    call .dim
    mov [px_edh], ax
    call pf_edkb
    pop dx
    pop bx
    ret
.dim:
    mul word [px_ep]
    div word [px_ep + 2]
    or ax, ax
    jnz .d1
    inc ax
.d1:
    ret

; pv_step - AX = a row's - or + (its button): the value a step, held to its
; range, and redrawn. Preserves all
pv_step:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    sub ax, PX_B_C1M
    mov cl, al                      ; (bit 0: + rather than -)
    mov di, ax
    shr di, 1                       ; DI = the row
    call pv_rec
    mov al, [cs:si + 2]             ; (a row the card has)
    xor ah, ah
    cmp ax, di
    jbe .x
    call pv_row
    mov bx, di
    shl bx, 1
    mov ax, [px_pcv + bx]
    mov dx, [cs:si + PCR_STEP]
    test cl, 1
    jnz .up
    neg dx
.up:
    add ax, dx
    cmp ax, [cs:si + PCR_MIN]
    jge .lo
    mov ax, [cs:si + PCR_MIN]
.lo:
    cmp ax, [cs:si + PCR_MAX]
    jle .hi
    mov ax, [cs:si + PCR_MAX]
.hi:
    cmp ax, [px_pcv + bx]
    je .x
    mov [px_pcv + bx], ax
    push bx
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x
    PSV SV_LAYOUT
    call pv_val
    cmp byte [px_pcon], PC_RES
    jne .c
    call pv_res
.c:
    call OSAPI_WM_CLIP_CLEAR
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pv_fire - EV_UFIRE: AX = the card's button that fired. out AX = PCA_*
pv_fire:
    cmp ax, PX_B_COK
    je .ok
    cmp ax, PX_B_CCAN
    je .can
    call pv_step
    mov ax, PCA_SPENT
    clc
    ret
.ok:
    mov ax, PCA_OK
    clc
    ret
.can:
    mov ax, PCA_CANCEL
    clc
    ret

; pv_key - EV_UKEY: AX = a key: Esc cancels, Enter is OK, Left/Right the
; first row's - and +, Down/Up the second's - or Save As's format. out AX =
; PCA_*
pv_key:
    cmp ah, KSC_ESC
    je pv_fire.can
    cmp ah, KSC_ENTER
    je pv_fire.ok
    cmp byte [px_pcon], PC_SAVE
    jne .k3
    mov dx, [px_fdrop + PE_DR_SEL]
    cmp ah, KSC_DOWN
    jne .ku
    inc dx
    cmp dx, [px_fdrop + PE_DR_N]
    jb .ks
    xor dx, dx
    jmp short .ks
.ku:
    cmp ah, KSC_UP
    jne .sp
    or dx, dx
    jnz .kd
    mov dx, [px_fdrop + PE_DR_N]
.kd:
    dec dx
.ks:
    mov [px_fdrop + PE_DR_SEL], dx
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .sp
    PSV SV_LAYOUT
    mov bx, px_fdrop
    PSV SV_DROP
    call OSAPI_WM_CLIP_CLEAR
    jmp short .sp
.k3:
    mov dl, ah
    mov ax, PX_B_C1M                ; the arrows: a row's - or +
    cmp dl, KSC_LEFT
    je .st
    inc ax
    cmp dl, KSC_RIGHT
    je .st
    inc ax
    cmp dl, KSC_DOWN
    je .st
    inc ax
    cmp dl, KSC_UP
    jne .sp
.st:
    call pv_step
.sp:
    mov ax, PCA_SPENT
    clc
    ret

; pv_ok - EV_UOK: the card's values are the operation's parameters. out AL
; = its verb, or 0FFh for Save As (the format in [px_wfmt])
pv_ok:
    mov al, [px_pcon]
    cmp al, PC_SAVE
    jne .op
    mov ax, [px_fdrop + PE_DR_SEL]
    mov [px_wfmt], al
    mov al, 0xFF
    clc
    ret
.op:
    mov si, [px_pcv]
    mov [px_ep], si
    mov si, [px_pcv + 2]
    mov [px_ep + 2], si
    mov ah, EV_BRICON
    cmp al, PC_BC
    je .go
    mov ah, EV_GAMMA
    cmp al, PC_GAM
    je .go
    mov ah, EV_POSTER
    cmp al, PC_POST
    je .go
    mov ah, EV_THRESH
    cmp al, PC_THR
    je .go
    call pv_resdim                  ; Resize: num, den
    mov ah, EV_RESIZE
.go:
    mov al, ah
    clc
    ret

; pv_room - EV_UROOM: "Blur needs 302K; 120K free" for [px_eop] at the
; destination [px_edw] x [px_edh], into px_cline
pv_room:
    mov si, pv_onm                  ; the operation's name: the [px_eop]th
    mov cl, [px_eop]
.n:
    or cl, cl
    jz .got
.s:
    mov al, [cs:si]
    inc si
    or al, al
    jnz .s
    dec cl
    jmp short .n
.got:
    call pv_cstr
    mov di, px_cline
    mov si, pv_s_nd
    call pv_cat
    call pf_edkb
    xor dx, dx
    PSV SV_U32N
    mov si, pv_s_k
    call pv_cat
    mov si, pv_s_semi
    call pv_cat
    call OSAPI_MEM_AVAIL
    xor dx, dx
    PSV SV_U32N
    mov si, pv_s_kfree
    call pv_cat
    clc
    ret

; --- XMS (SPEC.md 41.8): the shown master's claim - its pixels and its
; histogram's tail - out above 1 MB before an operation done in place, and
; back when it is cancelled or undone. UI task, 32 KB a call

; pv_xbytes - DX:AX = the bytes to copy, even. Preserves all else
pv_xbytes:
    mov ax, [px_cur + PXR_MW]
    mul word [px_cur + PXR_MH]
    add ax, 2048 + 15
    adc dx, 0
    and ax, 0xFFF0
    ret

; pv_xout - EV_UXOUT: CF = 0 the copy made ([px_uxms] [px_uxlen]); CF = 1
; no store, or no room in it
pv_xout:
    call OSAPI_XMEM_CAPS            ; AX = KB the pool can hand out
    mov cx, ax
    call pv_xbytes
    mov [px_uxlen], ax
    mov [px_uxlen + 2], dx
    add ax, 1023
    adc dx, 0
    mov al, ah
    mov ah, dl
    shr ax, 1
    shr ax, 1
    cmp ax, cx
    ja .no
    mov ax, [px_uxlen]
    mov dx, [px_uxlen + 2]
    call OSAPI_XMEM_ALLOC
    jc .no
    mov [px_uxms], ax
    mov [px_uxms + 2], dx
    xor di, di
    call pv_xcopy
    jnc .x
    mov ax, [px_uxms]
    mov dx, [px_uxms + 2]
    call OSAPI_XMEM_FREE
.no:
    stc
.x:
    ret

; pv_xin - EV_UXIN: the master back from above 1 MB, the copy given back
pv_xin:
    mov di, 1
    call pv_xcopy
    mov ax, [px_uxms]
    mov dx, [px_uxms + 2]
    call OSAPI_XMEM_FREE
    clc
    ret

; pv_xcopy - DI = 0 out, 1 in: [px_uxlen] bytes. CF on a refusal
pv_xcopy:
    mov bx, [px_cur + PXR_MSEG]     ; BX = the conventional end's segment
    mov ax, [px_uxms]               ; DX:AX = the extended end
    mov dx, [px_uxms + 2]
    mov si, [px_uxlen]              ; BP:SI = the bytes left
    mov bp, [px_uxlen + 2]
.l:
    mov cx, 0x8000
    or bp, bp
    jnz .c
    cmp si, cx
    jae .c
    mov cx, si
.c:
    jcxz .done
    push si
    push es
    mov es, bx
    xor si, si
    call OSAPI_XMEM_COPY
    pop es
    pop si
    jc .err
    add ax, cx
    adc dx, 0
    add bx, 0x0800
    sub si, cx
    sbb bp, 0
    jmp short .l
.done:
    clc
.err:
    ret

pv_sv:      dw 0, 0                 ; the resident's service gate (pxsvc.inc)

; =============================================================================
; THE OPERATIONS' FLOW (UI task): what Image and Effects do between a menu
; item and the arithmetic - the claims an operation needs, the worker's job,
; its commit and UNDO. ONE LEVEL, which is also redo where it costs nothing:
;   UK_PAL     the other palette and its plans (an open's bank, px_ppal /
;              px_pplan, free between opens - and no hidden decode runs
;              while undo holds anything): an exchange
;   UK_FLIP    a flip or the half turn, done in place: done again
;   UK_MASTER  the old master kept in its claim: an exchange of the two
;   UK_XMS     the old master copied above 1 MB before an operation done in
;              place, on a 286+: copied back, and no redo
; =============================================================================
pe_ocls:    db OC_PAL, OC_PAL, OC_PAL, OC_PAL, OC_PAL, OC_PAL, OC_PAL, OC_PAL
            db OC_DEST, OC_DEST, OC_FLIP, OC_FLIP, OC_FLIP, OC_EITHER
            db OC_DEST, OC_EITHER, OC_EITHER, OC_EITHER, OC_EITHER
            db OC_EITHER
%if ($ - pe_ocls) != EV_N
  %error "pe_ocls has a class per verb"
%endif
pf_s_wide:  db 'Too wide to edit', 0
pf_s_mem:   db 'not enough memory', 0

; pf_say - CS:SI said in a toast. Preserves all
pf_say:
    push si
    call pv_cstr
    PSV SV_TOAST
    pop si
    ret

; pf_cls - AL = the class of [px_eop]. Preserves all but AL
pf_cls:
    push bx
    mov bl, [px_eop]
    xor bh, bh
    mov al, [cs:pe_ocls + bx]
    pop bx
    ret

; pf_edkb - AX = the destination master's claim in KB: its pixels and the
; histogram's tail (SPEC.md 106.20). Preserves all but AX
pf_edkb:
    push dx
    mov ax, [px_edw]
    mul word [px_edh]
    add ax, 1023
    adc dx, 0
    mov al, ah
    mov ah, dl
    shr ax, 1
    shr ax, 1
    add ax, 2
    pop dx
    ret

; pf_bank - the palette, its plans and their validity, and the record, into
; undo's half of the exchange; [px_udirty]; Redo not yet. Preserves all
pf_bank:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, px_pal
    mov di, px_ppal
    mov cx, 768
    rep movsb
    mov si, px_plan
    mov di, px_pplan
    mov cx, 768
    rep movsb
    mov al, [px_pvalid]
    mov [px_ppvalid], al
    mov si, px_cur
    mov di, px_urec
    mov cx, PXR_SZ
    rep movsb
    mov al, [px_dirty]
    mov [px_udirty], al
    mov byte [px_uredo], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; pf_pal - EV_UPAL: AX = a palette verb: what undo held let go, this palette
; banked, the operation done, the picture's mode PAL (an edited cube is not
; the cube: its shipped plans no longer describe it), shown
pf_pal:
    PSV SV_UFREE
    call pf_bank
    mov [px_uop], al
    mov byte [px_ukind], UK_PAL
    mov cl, al
    call pe_palop
    mov byte [px_cur + PXR_PMODE], PM_PAL
    mov word [px_cur + PXR_NPAL], 256
    mov byte [px_pvalid], 0
    mov byte [px_dirty], 1
    PSV SV_ESHOW
    clc
    ret

; pf_start - EV_USTART: AX = a pixel verb: what undo held let go first (one
; level, and its memory with it), the claims made, the worker started
pf_start:
    mov [px_eop], al
    mov byte [px_eredo], 0
    cmp byte [px_ukind], UK_MASTER  ; undo's MASTER (or its copy above 1 MB)
    jb pf_go                        ; goes first - its room may be what the
    PSV SV_UFREE                    ; operation needs; a palette's or a
pf_go:                              ; flip's only once it is sure to run
                                    ; (review-w7 F11: a refused one kept it)
    mov cl, [px_eop]
    call pe_layout                  ; AX = the work's KB, the destination's
    jnc .w                          ; size in [px_edw] [px_edh]
    mov si, pf_s_wide
    jmp .say
.w:
    mov [px_ewkb], ax
    xor ax, ax
    mov [px_edseg], ax
    mov [px_ewseg], ax
    call pf_cls
    cmp al, OC_FLIP
    je .work
    mov bl, al
    call pf_edkb                    ; a destination
    mov [px_edkb2], ax
    call OSAPI_MEM_CLAIM
    jc .nodest
    mov [px_edseg], dx
    jmp short .work
.nodest:
    cmp bl, OC_EITHER               ; in place, over a copy above 1 MB?
    jne .room
    call pv_xout
    jc .room
    mov byte [px_ukind], UK_XMS
    jmp short .work
.room:
    call pv_room                    ; "Blur needs 302K; 120K free"
    mov si, px_cline
    PSV SV_TOAST
    clc
    ret
.work:
    mov ax, [px_ewkb]
    or ax, ax
    jz .start
    call OSAPI_MEM_CLAIM
    jc .nowork
    mov [px_ewseg], dx
.start:
    cmp byte [px_eredo], 0          ; (undo's flip again keeps its bank)
    jne .sp
    PSV SV_UFREE                    ; (the old undo now, F11)
    call pf_bank                    ; the picture as it is: undo's
.sp:
    PSV SV_SPAWN
    jc .nowork
    mov byte [px_abort], 0
    mov word [px_erow], 0
    mov ax, [px_edh]                ; rows the progress counts: a pair a
    mov bl, [px_eop]                ; row for the two exchanges
    cmp bl, EV_ROT180
    je .half
    cmp bl, EV_FLIPV
    jne .rows
.half:
    inc ax
    shr ax, 1
.rows:
    mov [px_erows], ax
    mov byte [px_busy], PXB_EDIT
    mov byte [px_job], JOB_EDIT     ; LAST: the worker starts on this byte
    PSV SV_EPROG
    PSV SV_FLAGS
    PSV SV_UPDATE
    clc
    ret
.nowork:
    call pf_rel
    mov si, pf_s_mem
.say:
    call pf_say
    clc
    ret

; pf_rel - an operation's claims given back: the destination, the work and
; an XMS copy no commit took. Preserves all
pf_rel:
    push dx
    mov dx, [px_ewseg]
    or dx, dx
    jz .d
    call OSAPI_MEM_FREE
    mov word [px_ewseg], 0
.d:
    mov dx, [px_edseg]
    or dx, dx
    jz .x
    call OSAPI_MEM_FREE
    mov word [px_edseg], 0
.x:
    cmp byte [px_ukind], UK_XMS
    jne .o
    PSV SV_UFREE
.o:
    pop dx
    ret

; pf_fin - EV_UFIN: THE WORKER HAS ANSWERED ([px_busy] already 0): the
; destination becomes the picture and the old one undo's, or - cancelled,
; or a part of another build - everything is given back and the picture is
; as it was
pf_fin:
    mov dx, [px_ewseg]              ; the work claim is spent either way
    or dx, dx
    jz .w
    call OSAPI_MEM_FREE
    mov word [px_ewseg], 0
.w:
    cmp byte [px_wres], 0
    je .commit
    cmp byte [px_ukind], UK_XMS     ; CANCELLED: in place, the copy back
    jne .c1
    call pv_xin
.c1:
    mov byte [px_ukind], UK_NONE
    call pf_rel
    PSV SV_COMPOSE
    PSV SV_FLAGS
    mov al, 4 | 32                  ; (PX_R_CANVAS | PX_R_STATUS)
    PSV SV_REGDRAW
    clc
    ret
.commit:
    call pf_cls
    cmp byte [px_eredo], 0          ; a flip undone by doing it again: the
    je .new                         ; exchange's other half, not a new undo
    xor byte [px_uredo], 1
    mov al, [px_dirty]
    xchg al, [px_udirty]
    mov [px_dirty], al
    jmp near .shown
.new:
    mov ah, [px_eop]
    mov [px_uop], ah
    cmp al, OC_FLIP
    jne .nf
    mov byte [px_ukind], UK_FLIP
    jmp short .done
.nf:
    mov dx, [px_edseg]
    or dx, dx
    jz .inpl                        ; in place over an XMS copy: UK_XMS
    mov byte [px_ukind], UK_MASTER  ; THE OLD MASTER IS UNDO'S: px_urec,
    mov [px_cur + PXR_MSEG], dx     ; banked before the operation, names it
    mov ax, [px_edkb2]
    mov [px_cur + PXR_MKB], ax
    mov ax, [px_mrelp]              ; movable once it is the picture's
    call OSAPI_MEM_MOVABLE
    mov word [px_edseg], 0
.inpl:
    mov ax, [px_edw]
    mov [px_cur + PXR_MW], ax
    mov dx, [px_edh]
    mov [px_cur + PXR_MH], dx
    mov cl, [px_cur + PXR_DSCL]     ; the master IS the picture now: no
    shl ax, cl                      ; scale, nothing for a re-decode to read
    shl dx, cl                      ; finer (SPEC.md 106.19) - and Image
    mov [px_cur + PXR_SW], ax       ; Info's size is the FILE's scale's, so
    mov [px_cur + PXR_SH], dx       ; an 800 x 600 turned at 1/2 reads 600 x
    mov byte [px_cur + PXR_SCL], 0  ; 800, not 300 x 400 (review-w8, F8's
                                    ; residue)
    mov byte [px_cur + PXR_FAST], 0
    cmp byte [px_eop], EV_RESIZE
    jb .done
    mov al, PM_CUBE                 ; through the palette: the cube, or GREY
    cmp byte [px_egrey], 0
    je .m
    mov al, PM_GREY
.m:
    mov [px_cur + PXR_PMODE], al
    PSV SV_SETPAL
.done:
    mov byte [px_dirty], 1
.shown:
    mov al, [px_eop]                ; a new size: the view at Fit
    cmp al, EV_ROT180
    jb .vn
    cmp al, EV_CROP
    je .vn
    cmp al, EV_RESIZE
    jne .sh
.vn:
    PSV SV_VIEWNEW
    PSV SV_SELOFF                   ; (the selection was in the old one)
.sh:
    PSV SV_ESHOW
    clc
    ret

; pf_undo - EV_UUNDO: Edit > Undo (Ctrl+Z)
pf_undo:
    mov al, [px_ukind]
    cmp al, UK_PAL
    je .pal
    cmp al, UK_FLIP
    je .flip
    cmp al, UK_MASTER
    je .mas
    cmp al, UK_XMS
    jne .x
    call pv_xin                     ; THE COPY BACK: no redo after it
    mov byte [px_ukind], UK_NONE
    call pf_uswap
    call pf_uxpal
    mov al, [px_udirty]
    mov [px_dirty], al
    PSV SV_VIEWNEW
    PSV SV_SELOFF
    PSV SV_ESHOW
    jmp short .x
.pal:
    call pf_uxpal                   ; THE PALETTE EXCHANGED
    call pf_uswap
    jmp short .ex
.mas:
    call pf_uxpal                   ; THE MASTERS EXCHANGED (the record's
    call pf_uswap                   ; MSEG is in pf_uswap's half), and the
    PSV SV_VIEWNEW                  ; palettes with them
    PSV SV_SELOFF
.ex:
    xor byte [px_uredo], 1
    mov al, [px_dirty]
    xchg al, [px_udirty]
    mov [px_dirty], al
    PSV SV_ESHOW
    jmp short .x
.flip:
    mov al, [px_uop]                ; THE FLIP AGAIN, on the worker, its
    mov [px_eop], al                ; bank kept
    mov byte [px_eredo], 1
    jmp pf_go
.x:
    clc
    ret

; pf_uxpal - the palette, its plans and their validity exchanged with
; undo's. Preserves all
pf_uxpal:
    push ax
    push cx
    push si
    push di
    mov si, px_pal
    mov di, px_ppal
    mov cx, 768
    call pf_xchg
    mov si, px_plan
    mov di, px_pplan
    mov cx, 768
    call pf_xchg
    mov al, [px_pvalid]
    xchg al, [px_ppvalid]
    mov [px_pvalid], al
    pop di
    pop si
    pop cx
    pop ax
    ret

; pf_uswap - the picture's half of the record exchanged with undo's: its
; size, scale, mode, master, palette count and fast-open scale (PXR_SW..SH,
; PXR_MW..PXR_MKB, PXR_NPAL, PXR_FAST) - never its name, folder or format,
; which a save between the two may have changed. Preserves all
pf_uswap:
    push cx
    push si
    push di
    mov si, px_cur + PXR_SW
    mov di, px_urec + PXR_SW
    mov cx, 4
    call pf_xchg
    mov si, px_cur + PXR_MW
    mov di, px_urec + PXR_MW
    mov cx, PXR_PACK - PXR_MW
    call pf_xchg
    mov si, px_cur + PXR_NPAL
    mov di, px_urec + PXR_NPAL
    mov cx, 2
    call pf_xchg
    mov si, px_cur + PXR_FAST
    mov di, px_urec + PXR_FAST
    mov cx, 1
    call pf_xchg
    pop di
    pop si
    pop cx
    ret

; pf_xchg - CX bytes at SI and DI exchanged (the package's). Preserves all
pf_xchg:
    push ax
    push cx
    push si
    push di
.l:
    mov al, [si]
    xchg al, [di]
    mov [si], al
    inc si
    inc di
    loop .l
    pop di
    pop si
    pop cx
    pop ax
    ret
