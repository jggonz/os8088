; =============================================================================
; os8088 - apps/pixel/pxsimple.inc
;
; THE SIMPLE FORMATS, RESIDENT (SPEC.md 106.10): BMP, PCX, TGA, PNM and PIX.
; Each has two halves:
;
;   px_h* - THE HEADER, on the UI task, out of the HEAD ([px_hseg]:0, at most
;           [px_hlen] = 2048 bytes). Dimensions, depth, palette (into
;           [px_spal]) and where the pixels start. Nothing is claimed yet, so
;           a refusal costs nothing.
;   px_d* - THE ROWS, on the worker, out of the ring (pxpump.inc's px_rb,
;           px_rdn, px_rskip), each source row handed to px_emit
;           (pxmaster.inc) in the row buffer at [px_wseg]:0.
;
; tools/pixelsim.py's bmp_header/bmp_rows, pcx_*, tga_*, pnm_* and pix_* are
; these routines in Python, check for check and in the same order - the
; ORDER matters, because the first failed check is the number the toast says
; and tests/pxdecode.py compares numbers.
;
; Every answer is CF = 0, or CF = 1 with AX = a PXD_*. A part never speaks
; and neither does this file: px_refusal (pxpump.inc) words the number.
; =============================================================================

; (the PXD_* answers are apps/pixel/pxpart.inc's: a part gives them too)


DF_BMP      equ 0                   ; the row decoders
DF_BMPRLE   equ 1
DF_PCX      equ 2
DF_TGA      equ 3
DF_PNMA     equ 4                   ; P1-P3, text
DF_PNMR     equ 5                   ; P4-P6, binary
DF_PIX      equ 6
DF_PART     equ 7                   ; a decoder PART (SPEC.md 106.18)

; =============================================================================
; px_hdrparse - the header of [px_cur]'s file, from the head. The format is
; the sniff's (106.6). out CF = 0 and the decode set up; CF = 1 AX = PXD_*
; =============================================================================
px_hdrparse:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; [px_spal] := black, so a palette shorter
    mov di, px_spal                 ; than 256 reads black past its end
    mov cx, 768 / 2
    xor ax, ax
    cld
    rep stosw
    mov [px_drawsz], ax
    mov [px_dlinsz], ax
    mov [px_dtop], al
    mov [px_bgr], al                ; R, G, B unless a reader says not
    mov [px_cur + PXR_PACK], al
    mov [px_cur + PXR_NPAL], ax
    mov [px_ddpara], ax             ; no decoder scratch...
    mov [px_k + PXK_RBLK], al       ; ...and every row of a block summed
    mov al, [px_cur + PXR_FMT]
    call px_fpart                   ; the decoder part this format needs, and
    call px_kneed                   ; no other held (SPEC.md 106.18)
    mov es, [px_hseg]
    mov al, [px_cur + PXR_FMT]
    mov bx, px_hpart
    cmp al, PXF_GIF
    je .go
    cmp al, PXF_PNG
    je .go
    mov bx, px_hbmp
    cmp al, PXF_BMP
    je .go
    mov bx, px_hpcx
    cmp al, PXF_PCX
    je .go
    mov bx, px_htga
    cmp al, PXF_TGA
    je .go
    mov bx, px_hpnm
    cmp al, PXF_PNM
    je .go
    mov bx, px_hpix
    cmp al, PXF_PIX
    je .go
    mov ax, PXD_NOTPIC              ; nothing PiXEL names
    cmp byte [px_cur + PXR_FMT], PXF_NONE
    je .no
    mov ax, PXD_NOTYET              ; a format whose decoder is a later wave
.no:
    stc
    jmp short .out
.go:
    call bx
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_dims - AX = a width, BX = a height, both proved non-zero and at most
; PX_DIMMAX by the caller: into the record. Preserves all
px_dims:
    mov [px_cur + PXR_SW], ax
    mov [px_cur + PXR_SH], bx
    ret

; px_dimok - DX:AX = a dimension: CF = 1 unless 1..PX_DIMMAX. Preserves all
px_dimok:
    or dx, dx
    jnz .no
    or ax, ax
    jz .no
    cmp ax, PX_DIMMAX
    ja .no
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; BMP (SPEC.md 106.10)
; =============================================================================
px_hbmp:
    cmp word [px_hlen], 26
    jae .h1
    jmp .head
.h1:
    mov ax, [es:10]                 ; where the pixels start
    mov [px_doff], ax
    mov ax, [es:12]
    mov [px_doff + 2], ax
    cmp word [es:16], 0             ; the info header's size
    je .h2
    jmp .head
.h2:
    mov ax, [es:14]
    mov [px_hsz], ax
    cmp ax, 12
    jne .win
    ; --- OS/2 v1: 16-bit fields, three-byte palette entries ----------------
    mov ax, [es:18]
    mov [px_tw], ax
    mov word [px_tw + 2], 0
    mov ax, [es:20]
    mov [px_th], ax
    mov word [px_th + 2], 0
    mov ax, [es:22]
    mov [px_dnpl], ax               ; (planes)
    mov ax, [es:24]
    mov [px_dbpp], ax
    xor ax, ax
    mov [px_dcomp], ax
    mov [px_dcomp + 2], ax
    mov [px_dused], ax
    mov [px_dused + 2], ax
    mov byte [px_desz], 3
    mov word [px_dpalo], 26
    jmp .dims
.win:
    cmp ax, 40                      ; Windows: 40, 52, 56, 108, 124
    je .w1
    cmp ax, 52
    je .w1
    cmp ax, 56
    je .w1
    cmp ax, 108
    je .w1
    cmp ax, 124
    je .w1
    jmp .head
.w1:
    cmp word [px_hlen], 54
    jae .w2
    jmp .head
.w2:
    mov ax, [es:18]
    mov [px_tw], ax
    mov ax, [es:20]
    mov [px_tw + 2], ax
    mov ax, [es:22]
    mov [px_th], ax
    mov ax, [es:24]
    mov [px_th + 2], ax
    mov ax, [es:26]
    mov [px_dnpl], ax
    mov ax, [es:28]
    mov [px_dbpp], ax
    mov ax, [es:30]
    mov [px_dcomp], ax
    mov ax, [es:32]
    mov [px_dcomp + 2], ax
    mov ax, [es:46]
    mov [px_dused], ax
    mov ax, [es:48]
    mov [px_dused + 2], ax
    mov byte [px_desz], 4
    mov ax, [px_hsz]
    add ax, 14
    mov [px_dpalo], ax
.dims:
    ; --- a negative height is a top-down picture; then the sizes ----------
    mov ax, [px_th]
    mov dx, [px_th + 2]
    or dx, dx
    jns .hpos
    mov byte [px_dtop], 1
    neg dx                          ; DX:AX := -DX:AX
    neg ax
    sbb dx, 0
.hpos:
    mov bx, ax                      ; BX = the height, once proved
    call px_dimok
    jc .dimsno
    mov ax, [px_tw]
    mov dx, [px_tw + 2]
    call px_dimok
    jnc .dimsok
.dimsno:
    mov ax, PXD_DIMS
    stc
    ret
.dimsok:
    call px_dims
    cmp word [px_dnpl], 1           ; planes
    je .pl
    jmp .head
.pl:
    mov ax, [px_dbpp]               ; the depths read
    mov [px_cur + PXR_BITS], al
    cmp ax, 1
    je .bppok
    cmp ax, 4
    je .bppok
    cmp ax, 8
    je .bppok
    cmp ax, 16
    je .bppok
    cmp ax, 24
    je .bppok
    cmp ax, 32
    je .bppok
    mov ax, PXD_DEPTH
    stc
    ret
.bppok:
    ; --- the packing ---------------------------------------------------------
    mov byte [px_dmask], 0
    cmp word [px_dcomp + 2], 0
    jne .pack
    mov ax, [px_dcomp]
    or ax, ax
    jz .comp0
    cmp ax, 1
    jne .c2
    cmp word [px_dbpp], 8
    jne .pack
    cmp byte [px_dtop], 0
    jne .pack
    mov byte [px_cur + PXR_PACK], PK_RLE8
    jmp short .comp0
.c2:
    cmp ax, 2
    jne .c3
    cmp word [px_dbpp], 4
    jne .pack
    cmp byte [px_dtop], 0
    jne .pack
    mov byte [px_cur + PXR_PACK], PK_RLE4
    jmp short .comp0
.c3:
    cmp ax, 3
    je .bf
    cmp ax, 6
    jne .pack
.bf:
    cmp word [px_dbpp], 16
    je .bf1
    cmp word [px_dbpp], 32
    jne .pack
.bf1:
    mov byte [px_cur + PXR_PACK], PK_BITF
    mov byte [px_dmask], 1
    cmp word [px_hsz], 40           ; a 40-byte header keeps its masks after
    jne .comp0                      ; itself, before the palette
    cmp word [px_hlen], 66
    jae .bf2
    jmp .head
.bf2:
    mov ax, 12
    cmp word [px_dcomp], 3
    je .bf3
    mov ax, 16
.bf3:
    add [px_dpalo], ax
    jmp short .comp0
.pack:
    mov ax, PXD_PACK
    stc
    ret
.comp0:
    ; --- the palette (1/4/8 bit) or the masks (16/32) ------------------------
    mov ax, [px_dbpp]
    cmp ax, 8
    ja .rgb
    mov byte [px_cur + PXR_RF], RF_IDX
    mov cx, [px_dused]              ; entries: as many as it says, or 2^bpp
    cmp word [px_dused + 2], 0
    jne .head
    or cx, cx
    jnz .n
    mov cl, al                      ; 2^bpp
    mov ax, 1
    shl ax, cl
    mov cx, ax
.n:
    cmp cx, 256
    ja .head
    mov [px_cur + PXR_NPAL], cx
    mov al, [px_desz]               ; palofs + n x esz inside the head
    xor ah, ah
    mul cx
    add ax, [px_dpalo]
    jc .head
    cmp ax, [px_hlen]
    ja .head
    mov si, [px_dpalo]
    mov di, px_spal
    mov bl, [px_desz]
    xor bh, bh
.pe:
    mov al, [es:si + 2]             ; B, G, R(, x) -> R, G, B
    mov [di], al
    mov al, [es:si + 1]
    mov [di + 1], al
    mov al, [es:si]
    mov [di + 2], al
    add si, bx
    add di, 3
    loop .pe
    jmp short .fin
.rgb:
    mov byte [px_cur + PXR_RF], RF_RGB
    cmp byte [px_dmask], 0
    je .defm
    mov si, 54                      ; the masks, at 54 either way
    call px_masks
    jc .depthno
    jmp short .fin
.defm:
    mov si, px_m555                 ; the defaults: 5:5:5, or B, G, R, x
    cmp ax, 16
    je .dm
    mov si, px_m888
.dm:
    push es
    push ds
    pop es
    call px_masks
    pop es
.fin:
    ; --- which decoder, and the raw row it needs -------------------------------
    mov byte [px_dfmt], DF_BMPRLE
    mov al, [px_cur + PXR_PACK]
    cmp al, PK_RLE8
    je .fmt
    cmp al, PK_RLE4
    je .fmt
    mov byte [px_dfmt], DF_BMP
    cmp word [px_dbpp], 24
    jne .not24
    mov byte [px_bgr], 1            ; read in its own order, never swapped
.not24:
    mov ax, [px_cur + PXR_SW]       ; ((w x bpp + 31) div 32) x 4
    mul word [px_dbpp]
    add ax, 31
    adc dx, 0
    mov cx, 5
.st:
    shr dx, 1
    rcr ax, 1
    loop .st
    shl ax, 1
    shl ax, 1
    mov [px_dstride], ax
    mov [px_drawsz], ax
.fmt:
    clc
    ret
.depthno:
    mov ax, PXD_DEPTH
    stc
    ret
.head:
    mov ax, PXD_HEAD
    stc
    ret

; px_masks - ES:SI = three dword masks (R, G, B): each one contiguous run of
; 1 to 8 bits, else CF = 1. Banks per channel the byte the run starts in
; ([px_mbo]), the shift inside it ([px_mbs]) and its largest value
; ([px_mmx]); the worker turns those into tables. Preserves all
px_masks:
    push ax
    push bx
    push cx
    push dx
    push si
    xor bx, bx
.ch:
    mov ax, [es:si]
    mov dx, [es:si + 2]
    mov cx, ax
    or cx, dx
    jz .no                          ; a channel with no bits
    xor cl, cl                      ; CL = the shift to its first bit
.lo:
    test ax, 1
    jnz .run
    shr dx, 1
    rcr ax, 1
    inc cl
    jmp short .lo
.run:
    xor ch, ch                      ; CH = its width
.w:
    test ax, 1
    jz .end
    shr dx, 1
    rcr ax, 1
    inc ch
    jmp short .w
.end:
    or ax, dx                       ; anything left is a hole
    jnz .no
    cmp ch, 8
    ja .no
    mov al, cl
    shr al, 1
    shr al, 1
    shr al, 1
    mov [px_mbo + bx], al
    mov al, cl
    and al, 7
    mov [px_mbs + bx], al
    mov cl, ch
    mov al, 1
    shl al, cl
    dec al                          ; (1 << 8) - 1 wraps right
    mov [px_mmx + bx], al
    add si, 4
    inc bx
    cmp bx, 3
    jb .ch
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

px_m555:    dd 0x7C00, 0x03E0, 0x001F
px_m888:    dd 0xFF0000, 0x00FF00, 0x0000FF

; =============================================================================
; PCX (SPEC.md 106.10)
; =============================================================================
px_hpcx:
    cmp word [px_hlen], 128
    jae .h1
    jmp .head
.h1:
    cmp byte [es:2], 1
    je .h2
    mov ax, PXD_PACK
    stc
    ret
.h2:
    mov ax, [es:8]                  ; w = xmax - xmin + 1, 1..PX_DIMMAX
    xor dx, dx
    sub ax, [es:4]
    sbb dx, 0
    js .dims
    add ax, 1
    adc dx, 0
    call px_dimok
    jc .dims
    mov bx, ax
    mov ax, [es:10]
    xor dx, dx
    sub ax, [es:6]
    sbb dx, 0
    js .dims
    add ax, 1
    adc dx, 0
    call px_dimok
    jc .dims
    xchg ax, bx
    call px_dims
    mov al, [es:3]                  ; (bits, planes) of four kinds
    mov ah, [es:65]
    mov [px_dbpp], al
    mov [px_dnpl], ah
    mov byte [px_dbpp + 1], 0
    mov byte [px_dnpl + 1], 0
    cmp ax, 0x0101
    je .k
    cmp ax, 0x0401
    je .k
    cmp ax, 0x0108
    je .k
    cmp ax, 0x0308
    je .k
    mov ax, PXD_DEPTH
    stc
    ret
.dims:
    mov ax, PXD_DIMS
    stc
    ret
.k:
    mul ah
    mov [px_cur + PXR_BITS], al
    mov ax, [px_cur + PXR_SW]       ; bpl >= ceil(w x bits / 8), <= 16384
    mov cl, [px_dbpp]
    cmp cl, 8
    je .b8
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
.b8:
    mov bx, [es:66]
    cmp bx, ax
    jb .head
    cmp bx, 16384
    ja .head
    mov [px_dbpl], bx
    mov ax, bx
    mul word [px_dnpl]
    mov [px_dlinsz], ax
    mov byte [px_cur + PXR_PACK], PK_RLE
    mov byte [px_dfmt], DF_PCX
    mov byte [px_dtop], 1           ; PCX, PNM and PIX are top-down: the
                                    ; painter fills from the top (106.9)
    mov word [px_doff], 128
    mov word [px_doff + 2], 0
    mov ax, [px_cur + PXR_SW]
    mov [px_drawsz], ax
    mov ax, [px_dnpl]
    cmp al, 3
    je .rgb
    mov byte [px_cur + PXR_RF], RF_IDX
    cmp byte [px_dbpp], 8
    je .eight
    cmp al, 1
    jne .ega
    mov word [px_cur + PXR_NPAL], 2 ; one plane of bits: black and white
    mov word [px_spal + 3], 0xFFFF
    mov byte [px_spal + 5], 0xFF
    clc
    ret
.ega:
    mov word [px_cur + PXR_NPAL], 16
    mov si, 16                      ; the header's sixteen...
    cmp byte [es:1], 3
    jne .hp
    push es                         ; ...or os8088's, for a version-3 file
    push cs
    pop es
    mov si, px_ega
    call px_pal16
    pop es
    clc
    ret
.hp:
    call px_pal16
    clc
    ret
.eight:
    call px_pcxtail                 ; the 769-byte tail: 0x0C and 256 colours
    jc .grey
    mov word [px_cur + PXR_NPAL], 256
    clc
    ret
.grey:
    mov byte [px_cur + PXR_RF], RF_GREY
    clc
    ret
.rgb:
    mov byte [px_cur + PXR_RF], RF_RGB
    mov ax, [px_cur + PXR_SW]
    mov bx, ax
    shl ax, 1
    add ax, bx
    mov [px_drawsz], ax
    clc
    ret
.head:
    mov ax, PXD_HEAD
    stc
    ret

; px_pal16 - ES:SI = sixteen R,G,B triples into [px_spal]. Preserves all
px_pal16:
    push cx
    push si
    push di
    mov di, px_spal
    mov cx, 48
.l:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .l
    pop di
    pop si
    pop cx
    ret

; px_pcxtail - an 8-bit PCX's palette: the file's last 769 bytes, a 0x0C and
; 256 R,G,B triples, read into [px_spal] when the file is long enough to
; have one (SPEC.md 106.10). CF = 1 there is none: the picture is grey.
; In memory (a CZ file) it is read from there; else with OSAPI_FILE_READ_AT
; into the head's claim, a cluster at a time. Preserves all
px_pcxtail:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [px_cur + PXR_FSIZE]    ; the file must hold the header and it
    mov dx, [px_cur + PXR_FSIZE + 2]
    sub ax, 128 + 769
    sbb dx, 0
    jb .none
    mov ax, [px_cur + PXR_FSIZE]    ; DX:AX = where the tail starts
    mov dx, [px_cur + PXR_FSIZE + 2]
    sub ax, 769
    sbb dx, 0
    mov [px_ttail], ax
    mov [px_ttail + 2], dx
    mov word [px_tgot], 0
    cmp byte [px_rflat], 0
    jne .flat
    call px_clbytes
    mov [px_tcl], ax
.chunk:                             ; the cluster holding byte [px_ttail] +
    mov ax, [px_ttail]              ; [px_tgot]
    mov dx, [px_ttail + 2]
    add ax, [px_tgot]
    adc dx, 0
    mov cx, [px_tcl]
    div cx                          ; AX = the cluster, DX = the byte in it
    mov si, dx
    mul cx                          ; DX:AX = its offset
    push si
    mov es, [px_hseg]
    xor bx, bx
    mov si, px_cur + PXR_NAME
    call OSAPI_FILE_READ_AT         ; DX:AX = bytes delivered
    pop si
    jc .none
    or dx, dx
    jnz .some
    cmp ax, si
    jbe .none
.some:
    mov cx, [px_tcl]                ; copy what this cluster holds of it
    sub cx, si
    mov ax, 769
    sub ax, [px_tgot]
    cmp cx, ax
    jbe .cp
    mov cx, ax
.cp:
    mov di, px_tbuf
    add di, [px_tgot]
    add [px_tgot], cx
.cl:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cl
    cmp word [px_tgot], 769
    jb .chunk
    jmp short .have
.flat:                              ; in memory: segment arithmetic
    mov ax, [px_ttail]
    mov dx, [px_ttail + 2]
    mov si, ax
    and si, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    add ax, [px_flatseg]
    mov es, ax
    mov di, px_tbuf
    mov cx, 769
.fl:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .fl
.have:
    cmp byte [px_tbuf], 0x0C
    jne .none
    push ds
    pop es
    mov si, px_tbuf + 1
    mov di, px_spal
    mov cx, 768
    cld
    rep movsb
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; TGA (SPEC.md 106.10)
; =============================================================================
px_htga:
    cmp word [px_hlen], 18
    jae .h1
    jmp .head
.h1:
    mov al, [es:2]                  ; the image type: 1, 2, 3, 9, 10, 11
    mov ah, al
    and ah, 0xF7
    or ah, ah
    jz .head1
    cmp ah, 3
    ja .head1
    cmp byte [es:1], 1              ; a colour map flag of 0 or 1
    ja .head1
    mov [px_dkind], al
    mov ax, [es:12]                 ; the sizes
    xor dx, dx
    call px_dimok
    jc .dims
    mov bx, [es:14]
    xchg ax, bx
    call px_dimok
    jc .dims
    xchg ax, bx
    call px_dims
    test byte [es:17], 0x10         ; right to left: refused by name
    jz .lr
    mov ax, PXD_ORIENT
    stc
    ret
.head1:
    jmp .head
.dims:
    mov ax, PXD_DIMS
    stc
    ret
.lr:
    mov al, [es:17]                 ; the vertical origin
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 1
    mov [px_dtop], al
    xor cx, cx                      ; CX = a colour map entry's bytes
    cmp byte [es:1], 0
    je .nocm
    mov al, [es:7]
    cmp al, 15
    je .cm2
    cmp al, 16
    je .cm2
    mov cl, 3
    cmp al, 24
    je .nocm
    mov cl, 4
    cmp al, 32
    je .nocm
    jmp .head
.cm2:
    mov cl, 2
.nocm:
    mov [px_desz], cl
    mov al, [es:16]
    mov [px_cur + PXR_BITS], al
    mov ah, [px_dkind]
    and ah, 7
    cmp ah, 1
    jne .t2
    ; --- colour-mapped: 8-bit indices into a map inside the head -------------
    cmp byte [es:1], 0
    je .head
    cmp al, 8
    jne .depth
    mov ax, [es:3]                  ; first + length <= 256
    add ax, [es:5]
    jc .head
    cmp ax, 256
    ja .head
    mov al, [es:0]                  ; the map at 18 + the ID's length
    xor ah, ah
    add ax, 18
    mov si, ax
    mov ax, [es:5]
    mul cx
    add ax, si
    jc .head
    cmp ax, [px_hlen]
    ja .head
    mov byte [px_cur + PXR_RF], RF_IDX
    mov bx, [es:3]                  ; entry first + j
    mov ax, bx
    add ax, [es:5]
    or ax, ax
    jnz .np
    inc ax                          ; (a map of none still has black)
.np:
    mov [px_cur + PXR_NPAL], ax
    mov dx, [es:5]
    mov di, bx
    shl di, 1
    add di, bx
    add di, px_spal
.ce:
    or dx, dx
    jz .t9
    call px_tgacol                  ; ES:SI, CX bytes -> R, G, B at [DI]
    add di, 3
    dec dx
    jmp short .ce
.t2:
    cmp ah, 2
    jne .t3
    mov byte [px_cur + PXR_RF], RF_RGB
    cmp al, 15
    je .t9
    cmp al, 16
    je .t9
    cmp al, 24
    je .t9
    cmp al, 32
    je .t9
    jmp short .depth
.t3:
    mov byte [px_cur + PXR_RF], RF_GREY
    cmp al, 8
    jne .depth
.t9:
    mov al, [px_cur + PXR_BITS]     ; a pixel's bytes
    add al, 7
    shr al, 1
    shr al, 1
    shr al, 1
    mov [px_dpsz], al
    mov byte [px_cur + PXR_PACK], PK_NONE
    test byte [px_dkind], 8
    jz .raw0
    mov byte [px_cur + PXR_PACK], PK_RLE
    jmp short .raw
.raw0:
    cmp byte [px_dpsz], 3           ; raw 24: B, G, R as it lies
    jne .raw
    mov byte [px_bgr], 1
.raw:
    mov al, [es:0]                  ; the pixels: 18 + ID + the map
    xor ah, ah
    add ax, 18
    mov bx, ax
    mov al, [px_desz]
    mul word [es:5]
    add ax, bx
    adc dx, 0
    mov [px_doff], ax
    mov [px_doff + 2], dx
    mov byte [px_dfmt], DF_TGA
    mov ax, [px_cur + PXR_SW]       ; the raw row: w pixels
    mov bl, [px_dpsz]
    xor bh, bh
    mul bx
    mov bx, [px_cur + PXR_SW]       ; ...or the converted one, whichever is
    shl bx, 1                       ; more
    add bx, [px_cur + PXR_SW]
    cmp ax, bx
    jae .rs
    mov ax, bx
.rs:
    mov [px_drawsz], ax
    clc
    ret
.depth:
    mov ax, PXD_DEPTH
    stc
    ret
.head:
    mov ax, PXD_HEAD
    stc
    ret

; px_tgacol - one TGA colour, CX bytes at ES:SI (SI advanced), as R, G, B at
; DS:DI. 2 bytes: 5:5:5; 3 or 4: B, G, R(, A). Preserves all but SI
px_tgacol:
    push ax
    push bx
    cmp cx, 2
    jne .bgr
    mov ax, [es:si]
    add si, 2
    mov bx, ax                      ; R = bits 10-14
    push cx
    mov cl, 10
    shr bx, cl
    pop cx
    and bx, 31
    mov bl, [px_lut5 + bx]
    mov [di], bl
    mov bx, ax
    push cx
    mov cl, 5
    shr bx, cl
    pop cx
    and bx, 31
    mov bl, [px_lut5 + bx]
    mov [di + 1], bl
    mov bx, ax
    and bx, 31
    mov bl, [px_lut5 + bx]
    mov [di + 2], bl
    jmp short .out
.bgr:
    mov al, [es:si + 2]
    mov [di], al
    mov al, [es:si + 1]
    mov [di + 1], al
    mov al, [es:si]
    mov [di + 2], al
    add si, cx
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; PNM (SPEC.md 106.10): the tokens, from the head (header) or the stream
; (P1-P3's pixels). [px_tokget] is the byte source: AL = the byte, CF = 1 at
; the end; [px_tokeof] the refusal an end between tokens is
; =============================================================================

; px_tkhead - the next head byte: CF = 1 past the head. Clobbers nothing else
px_tkhead:
    push si
    mov si, [px_tkpos]
    cmp si, [px_hlen]
    jae .end
    push es
    mov es, [px_hseg]
    mov al, [es:si]
    pop es
    inc word [px_tkpos]
    pop si
    clc
    ret
.end:
    pop si
    stc
    ret

; px_tkws - skip whitespace and #-comments: AL = the first other byte; CF = 1
; AX = [px_tokeof] when the source ends first. Preserves all but AX
px_tkws:
.l:
    call [px_tokget]
    jc .eof
    cmp al, '#'
    jne .w
.c:
    call [px_tokget]                ; a comment runs to the end of its line
    jc .eof
    cmp al, 10
    je .l
    cmp al, 13
    je .l
    jmp short .c
.w:
    call px_isws
    je .l
    clc
    ret
.eof:
    mov ax, [px_tokeof]
    stc
    ret

; px_isws - ZF = 1 when AL is space, tab, LF, CR, VT or FF. Preserves all
px_isws:
    cmp al, ' '
    je .y
    cmp al, 9
    jb .n
    cmp al, 13
    jbe .y
.n:
    cmp al, 0xFF                    ; (clears ZF: AL is not 0xFF here as a
    ret                             ; whitespace answer matters, and if it is
.y:                                 ; 0xFF it is not whitespace either)
    cmp al, al
    ret

; px_tknum - a decimal token, at most 65,535: AX = its value, DL = the byte
; that ended it, DH = 1 when the source ended it instead. CF = 1 AX = BX
; (the caller's refusal) on a non-digit start or an overflow, or AX =
; [px_tokeof] when the source ended before it. Preserves all but AX, DX
px_tknum:
    push cx
    push si
    call px_tkws
    jc .out
    cmp al, '0'
    jb .bad
    cmp al, '9'
    ja .bad
    xor si, si                      ; SI = the value
.d:
    sub al, '0'
    xor ah, ah
    mov cx, ax
    mov ax, si
    push dx
    mov dx, 10
    mul dx
    or dx, dx
    pop dx
    jnz .bad
    add ax, cx
    jc .bad
    mov si, ax
    call [px_tokget]
    jc .end
    cmp al, '0'
    jb .stop
    cmp al, '9'
    ja .stop
    jmp short .d
.end:
    mov dh, 1
    mov ax, si
    clc
    jmp short .out
.stop:
    mov dl, al
    mov dh, 0
    mov ax, si
    clc
    jmp short .out
.bad:
    mov ax, bx
    stc
.out:
    pop si
    pop cx
    ret

px_hpnm:
    mov word [px_tkpos], 2
    mov word [px_tokget], px_tkhead
    mov word [px_tokeof], PXD_HEAD
    mov al, [es:1]
    sub al, '0'
    mov [px_dkind], al
    mov bx, PXD_DIMS
    call px_tknum                   ; the width
    jc .ret
    mov cx, ax
    call px_tknum                   ; the height
    jc .ret
    or cx, cx
    jz .dims
    or ax, ax
    jz .dims
    cmp cx, PX_DIMMAX
    ja .dims
    cmp ax, PX_DIMMAX
    ja .dims
    mov bx, ax
    mov ax, cx
    call px_dims
    mov word [px_dmaxv], 1
    cmp byte [px_dkind], 1
    je .nomax
    cmp byte [px_dkind], 4
    je .nomax
    mov bx, PXD_HEAD
    call px_tknum                   ; the largest value
    jc .ret
    or ax, ax
    jz .head
    mov [px_dmaxv], ax
.nomax:
    or dh, dh                       ; exactly one whitespace byte, then the
    jnz .head                       ; pixels
    mov al, dl
    call px_isws
    jne .head
    mov ax, [px_tkpos]
    mov [px_doff], ax
    mov word [px_doff + 2], 0
    mov byte [px_dtop], 1
    mov al, [px_dkind]
    mov byte [px_dfmt], DF_PNMA
    cmp al, 4
    jb .a
    mov byte [px_dfmt], DF_PNMR
.a:
    cmp al, 1                       ; the kinds: bitmap (white, black), grey,
    je .bits                        ; colour
    cmp al, 4
    je .bits
    mov byte [px_cur + PXR_RF], RF_GREY
    mov ah, 8
    cmp al, 2
    je .g
    cmp al, 5
    je .g
    mov byte [px_cur + PXR_RF], RF_RGB
    mov ah, 24
.g:
    cmp word [px_dmaxv], 256
    jb .b8
    shl ah, 1
.b8:
    mov [px_cur + PXR_BITS], ah
    jmp short .rs
.bits:
    mov byte [px_cur + PXR_RF], RF_IDX
    mov byte [px_cur + PXR_BITS], 1
    mov word [px_cur + PXR_NPAL], 2
    mov word [px_spal], 0xFFFF      ; 0 white, 1 black
    mov byte [px_spal + 2], 0xFF
.rs:
    mov ax, [px_cur + PXR_SW]       ; the raw row, at most 6w (two bytes a
    mov bx, ax                      ; sample of three); the converted, 3w
    shl ax, 1
    add ax, bx
    shl ax, 1
    mov [px_drawsz], ax
    clc
.ret:
    ret
.dims:
    mov ax, PXD_DIMS
    stc
    ret
.head:
    mov ax, PXD_HEAD
    stc
    ret

; =============================================================================
; PIX (SPEC.md 61.7): the first picture of the archive
; =============================================================================
px_hpix:
    cmp word [px_hlen], 32
    jb .head
    cmp byte [es:5], 1
    jne .head
    cmp word [es:6], 0
    je .head
    cmp word [es:10], 16
    jne .head
    mov ax, [es:18]
    xor dx, dx
    call px_dimok
    jc .dims
    mov bx, [es:20]
    xchg ax, bx
    call px_dimok
    jc .dims
    xchg ax, bx
    call px_dims
    inc ax                          ; stride >= (w + 1) / 2
    shr ax, 1
    mov cx, [es:22]
    cmp cx, ax
    jb .head
    mov [px_dstride], cx
    mov [px_drawsz], cx
    mov ax, [es:24]
    mov [px_doff], ax
    mov ax, [es:26]
    mov [px_doff + 2], ax
    mov byte [px_cur + PXR_RF], RF_IDX
    mov byte [px_cur + PXR_BITS], 4
    mov word [px_cur + PXR_NPAL], 16
    mov byte [px_dfmt], DF_PIX
    mov byte [px_dtop], 1
    push es
    push cs
    pop es
    mov si, px_ega
    call px_pal16
    pop es
    clc
    ret
.dims:
    mov ax, PXD_DIMS
    stc
    ret
.head:
    mov ax, PXD_HEAD
    stc
    ret

; =============================================================================
; THE ROWS, ON THE WORKER
; =============================================================================

; px_decode - the decoder [px_dfmt] names, from the top of the stream.
; out CF = 0, or CF = 1 AX = a PXD_* (pxpump.inc's px_wjob says what it was)
px_decode:
    mov ax, [px_doff]
    mov dx, [px_doff + 2]
    call px_rskip                   ; to the pixels
    jnc .go
    mov ax, PXD_TRUNC
    ret
.go:
    mov bl, [px_dfmt]
    xor bh, bh
    shl bx, 1
    jmp [px_dectab + bx]

px_dectab:  dw px_dbmp, px_dbmprle, px_dpcx, px_dtga, px_dpnma, px_dpnmr
            dw px_dpix, px_dpart

; =============================================================================
; A DECODER PART (SPEC.md 106.18): its HEAD on the UI task, its DECODE here
; =============================================================================

; px_hpart - the header, by the part [px_kheld] names: the head through the
; context, the answers into the record. CF = 1 AX = a PXD_*, or PXD_QUIET
; when the part could not be fetched (op_fetch has said why)
px_hpart:
    mov ax, [px_hseg]
    mov [px_k + PXK_HSEG], ax
    mov ax, [px_hlen]
    mov [px_k + PXK_HLEN], ax
    mov al, [px_kheld]
    mov bl, PXV_HEAD
    mov di, px_k
    call px_pcall
    jnc .ok
    cmp byte [px_pfail], 0          ; the part's own answer: a PXD_*
    je .no
    cmp ax, PXE_PART
    je .quiet
    mov si, px_s_badpart            ; fetched, and not a part of this ABI
    call px_toast
.quiet:
    mov ax, PXD_QUIET
.no:
    stc
    ret
.ok:
    mov ax, [px_k + PXK_SW]
    mov bx, [px_k + PXK_SH]
    call px_dims
    mov al, [px_k + PXK_RF]
    mov [px_cur + PXR_RF], al
    mov al, [px_k + PXK_BITS]
    mov [px_cur + PXR_BITS], al
    mov al, [px_k + PXK_PACK]
    mov [px_cur + PXR_PACK], al
    mov ax, [px_k + PXK_NPAL]
    mov [px_cur + PXR_NPAL], ax
    mov ax, [px_k + PXK_DPARA]
    mov [px_ddpara], ax
    mov byte [px_dfmt], DF_PART
    mov byte [px_dtop], 1           ; (an interlaced picture says which of its
    xor ax, ax                      ; rows are complete itself)
    mov [px_doff], ax
    mov [px_doff + 2], ax
    clc
    ret

; px_dpart - the worker: the context's decode half, the part's DECODE, and
; what the stream told it back into the record. out CF/AX as any decoder's
px_dpart:
    mov al, [px_cur + PXR_SCL]
    mov [px_k + PXK_SCL], al
    mov ax, [px_cur + PXR_MW]
    mov [px_k + PXK_MW], ax
    mov ax, [px_cur + PXR_MH]
    mov [px_k + PXK_MH], ax
    mov ax, [px_wseg]
    mov [px_k + PXK_RSEG], ax
    add ax, [px_wo_dec]
    mov [px_k + PXK_DSEG], ax
    mov word [px_k + PXK_SPAL], px_spal
    mov byte [px_k + PXK_RBLK], 0
    push ds
    pop es
    mov si, px_kst
    mov di, px_k + PXK_SVC
    mov cx, 5
    cld
    rep movsw
    mov al, [px_kheld]
    mov bl, PXV_DECODE
    mov di, px_k
    call px_wcall
    pushf
    push ax
    mov al, [px_k + PXK_PACK]
    mov [px_cur + PXR_PACK], al
    mov al, [px_k + PXK_BITS]
    mov [px_cur + PXR_BITS], al
    mov ax, [px_k + PXK_NPAL]
    mov [px_cur + PXR_NPAL], ax
    pop ax
    popf
    ret

; px_rowy - AX = i, the i-th row the file holds: AX = its y, top-down
; ([px_dtop]) or bottom-up. Preserves all but AX
px_rowy:
    cmp byte [px_dtop], 0
    jne .out
    neg ax
    add ax, [px_cur + PXR_SH]
    dec ax
.out:
    ret

; px_rawrow - the next CX bytes of the stream into the row buffer at 0. CF = 1
; AX = PXD_TRUNC at the end. Preserves all but AX
px_rawrow:
    push di
    push es
    mov es, [px_wseg]
    xor di, di
    call px_rdn
    pop es
    pop di
    mov ax, PXD_TRUNC
    ret

; px_unpack - the row buffer's first bytes are [px_cur + PXR_SW] pixels of
; BL bits (1, 4 or 8) packed high bit first: expanded in place to a byte
; each, from the right end leftwards. Preserves all
px_unpack:
    cmp bl, 8
    je .ret
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    mov cx, [px_cur + PXR_SW]
    mov di, cx
    dec di                          ; DI = the last pixel
    mov ds, [px_wseg]
    cmp bl, 4
    jne .one
.n4:
    mov si, di
    shr si, 1
    mov al, [si]
    test di, 1
    jnz .lo
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
.lo:
    and al, 15
    mov [di], al
    dec di
    loop .n4
    jmp short .out
.one:
    mov si, di
    shr si, 1
    shr si, 1
    shr si, 1
    mov al, [si]
    mov dx, di
    and dx, 7
    push cx
    mov cl, 7
    sub cl, dl
    shr al, cl
    pop cx
    and al, 1
    mov [di], al
    dec di
    loop .one
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; --- BMP, uncompressed ------------------------------------------------------------
px_dbmp:
    cmp word [px_dbpp], 16          ; the masks' tables, once
    jb .rows
    call px_masklut
.rows:
    xor bp, bp                      ; BP = i
.row:
    cmp bp, [px_cur + PXR_SH]
    jb .r1
    clc
    ret
.r1:
    mov cx, [px_dstride]
    call px_rawrow
    jc .ret
    mov bx, [px_dbpp]
    cmp bl, 8
    ja .deep
    call px_unpack
    jmp short .emit
.deep:
    cmp bl, 24                      ; 24: as it is - B, G, R, which the
    je .emit                        ; quantiser reads in that order ([px_bgr])
.bf:
    call px_bfrow                   ; 16 or 32 bits through the masks
.emit:
    mov ax, bp
    call px_rowy
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp short .row
.ret:
    ret

; px_swaprb - the row buffer's w triples B, G, R -> R, G, B. Preserves all
px_swaprb:
    push ax
    push cx
    push si
    push ds
    mov ds, [px_wseg]
    mov cx, [cs:px_cur + PXR_SW]
    xor si, si
.l:
    mov al, [si]
    xchg al, [si + 2]
    mov [si], al
    add si, 3
    loop .l
    pop ds
    pop si
    pop cx
    pop ax
    ret

; px_masklut - each channel's table in the work claim's tables: v (0..max)
; -> (v x 255 + max / 2) div max (SPEC.md 106.10). Preserves all
px_masklut:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call px_lutseg                  ; ES:0 = the tables, 256 a channel
    xor di, di
    xor bx, bx
.ch:
    mov cl, [px_mmx + bx]
    xor ch, ch                      ; CX = max
    xor si, si
.v:
    mov ax, si
    mov dx, 255
    mul dx
    push cx
    shr cx, 1
    add ax, cx
    adc dx, 0
    pop cx
    div cx
    mov [es:di], al
    inc di
    inc si
    cmp si, cx
    jbe .v
    mov di, bx                      ; the next channel's 256
    inc di
    push cx
    mov cl, 8
    shl di, cl
    pop cx
    inc bx
    cmp bx, 3
    jb .ch
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_lutseg - ES = the work claim's tables. Preserves all but ES
px_lutseg:
    push ax
    mov ax, [px_wseg]
    add ax, [px_wo_lut]
    mov es, ax
    pop ax
    ret

; px_bfrow - the row buffer's w pixels of 16 or 32 bits -> R, G, B triples
; through the masks: per channel, the word at the byte the run starts in,
; shifted, masked, looked up. 16 bits widen (right to left), 32 narrow (left
; to right), so neither overwrites a pixel not yet read. Preserves all
px_bfrow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    call px_lutseg
    mov cx, [px_cur + PXR_SW]
    mov bp, [px_dbpp]
    shr bp, 1
    shr bp, 1
    shr bp, 1                       ; BP = bytes a pixel, 2 or 4
    xor si, si                      ; SI = the pixel, DI = its triple
    xor di, di
    mov dx, 3                       ; DX = the triple's step, BP the pixel's
    cmp bp, 2
    jne .go
    mov si, cx                      ; widening: from the last one back
    dec si
    shl si, 1
    mov di, cx
    dec di
    mov ax, di
    shl di, 1
    add di, ax
    mov dx, -3
    mov bp, -2
.go:
    push dx
    mov dx, [px_wseg]
.px:
    push cx
    xor bx, bx
.ch:
    push si
    mov al, [px_mbo + bx]
    xor ah, ah
    add si, ax
    push ds
    mov ds, dx
    mov ax, [si]                    ; the word holding the run
    pop ds
    mov cl, [px_mbs + bx]
    shr ax, cl
    and al, [px_mmx + bx]
    push bx
    mov cl, 8
    shl bx, cl
    add bl, al                      ; this channel's table, the value
    mov al, [es:bx]
    pop bx
    mov [px_rtri + bx], al          ; all three read before any is written:
    pop si                          ; the triple overlaps its own pixel
    inc bx
    cmp bx, 3
    jb .ch
    push ds
    mov ax, [px_rtri]
    mov bl, [px_rtri + 2]
    mov ds, dx
    mov [di], ax
    mov [di + 2], bl
    pop ds
    pop cx
    add si, bp
    pop ax
    push ax
    add di, ax
    loop .px
    pop dx
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

; --- BMP, RLE8 and RLE4 (bottom-up only) -------------------------------------------
; The stream's instructions build the current row; an end of line, a delta
; downwards or the end of the bitmap emits it (and any rows it passes, at
; index 0). A pixel past the row's end is dropped; the picture's last row
; ends the decode whatever follows (pixelsim's bmp_rle)
px_dbmprle:
    mov word [px_ri], 0             ; [px_ri] = rows emitted, [px_rx] = x
    call px_rlenew
.op:
    mov ax, [px_ri]
    cmp ax, [px_cur + PXR_SH]
    jb .more
    clc
    ret
.more:
    call px_rb
    jc .trunc
    mov ah, al                      ; AH = a
    call px_rb
    jc .trunc                       ; AL = b
    or ah, ah
    jz .esc
    ; --- a run: a copies of b (RLE4: its two nibbles in turn) ---------------
    mov cl, ah
    xor ch, ch
    xor dx, dx                      ; DX = the nibble's parity
.run:
    mov bl, al
    cmp word [px_dbpp], 4
    jne .r8
    mov bl, al
    test dl, 1
    jnz .rlo
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
.rlo:
    and bl, 15
.r8:
    call px_rleput
    inc dx
    loop .run
    jmp short .op
.esc:
    or al, al
    jnz .e1
    call px_rleflush                ; 0, 0: end of line
    jc .ret
    jmp short .op
.e1:
    cmp al, 1
    jne .e2
.eob:                               ; 0, 1: the end - the rows left at 0
    mov ax, [px_ri]
    cmp ax, [px_cur + PXR_SH]
    jae .done
    call px_rleflush
    jc .ret
    jmp short .eob
.done:
    clc
    ret
.e2:
    cmp al, 2
    jne .abs
    call px_rb                      ; 0, 2, dx, dy: a delta
    jc .trunc
    xor ah, ah
    add [px_rx], ax
    call px_rb
    jc .trunc
    xor ah, ah
    mov cx, ax
.dy:
    jcxz .op1
    mov ax, [px_ri]
    cmp ax, [px_cur + PXR_SH]
    jae .op1
    push word [px_rx]               ; the column survives the rows it skips
    call px_rleflush
    pop word [px_rx]
    jc .ret
    loop .dy
.op1:
    jmp .op
.abs:                               ; 0, n: n literal pixels, word-padded
    mov cl, al
    xor ch, ch
    cmp word [px_dbpp], 4
    je .a4
    push cx
.a8:
    call px_rb
    jc .atr
    mov bl, al
    call px_rleput
    loop .a8
    pop cx
    test cl, 1
    jz .op1
    call px_rb
    jc .trunc
    jmp .op
.atr:
    pop cx
    jmp short .trunc
.a4:
    mov ax, cx                      ; (n + 1) / 2 bytes, then a pad when that
    inc ax                          ; is odd
    shr ax, 1
    push ax
    xor dx, dx
.a4b:
    test dl, 1
    jnz .a4lo
    push cx
    call px_rb
    pop cx
    jc .atr
    mov [px_rnib], al
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    jmp short .a4p
.a4lo:
    mov bl, [px_rnib]
    and bl, 15
.a4p:
    call px_rleput
    inc dx
    loop .a4b
    pop ax
    test al, 1
    jz .op1
    call px_rb
    jc .trunc
    jmp .op
.trunc:
    mov ax, PXD_TRUNC
    stc
.ret:
    ret

; px_rlenew - the row buffer's w bytes to 0 and the column to 0. Preserves all
px_rlenew:
    push ax
    push cx
    push di
    push es
    mov es, [px_wseg]
    xor di, di
    mov cx, [px_cur + PXR_SW]
    xor al, al
    cld
    rep stosb
    mov word [px_rx], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; px_rleput - BL at the column, if it is inside the row; the column moves
; on either way. Preserves all
px_rleput:
    push di
    push es
    mov di, [px_rx]
    inc word [px_rx]
    cmp di, [px_cur + PXR_SW]
    jae .out
    mov es, [px_wseg]
    mov [es:di], bl
.out:
    pop es
    pop di
    ret

; px_rleflush - emit the row (bottom-up) and start the next. CF = 1 AX on a
; cancel. Preserves all but AX
px_rleflush:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [px_ri]
    call px_rowy
    call px_emit
    jc .out
    inc word [px_ri]
    call px_rlenew
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- PCX ------------------------------------------------------------------------------
; One continuous stream of runs, a scanline at a time into the line buffer:
; a run may carry over from one line to the next, and from one plane to the
; next inside a line (pixelsim's pcx_rows)
px_dpcx:
    mov word [px_rcnt2], 0          ; the run in hand: its count, its value
    xor bp, bp                      ; BP = y
.line:
    cmp bp, [px_cur + PXR_SH]
    jb .l1
    clc
    ret
.l1:
    mov ax, [px_wseg]
    add ax, [px_wo_lin]
    mov es, ax
    xor di, di
    mov cx, [px_dlinsz]
    call px_pcxfill                 ; the line, from the runs
    jc .trunc
    call px_pcxconv                 ; the line -> the row buffer
    mov ax, bp
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp short .line
.trunc:
    mov ax, PXD_TRUNC
    stc
.ret:
    ret

; px_pcxfill - CX bytes of the run stream into ES:DI. THE RING'S WINDOW IS
; WALKED IN REGISTERS - DS:SI on the window, DX its end - and px_rnext is
; called only when it runs out, because a call a byte (px_rb) was a fifth
; of an 8-bit PCX's whole open on an 8088; and a run is put down with one
; rep stosb. The run in hand ([px_rcnt2], [px_rval]) carries over from one
; line to the next. CF = 1 at the end of the stream
px_pcxfill:
    push ax
    push bx
    push dx
    push si
    push bp
    mov bl, [px_rcnt2]
    mov bh, [px_rval]
    cld
.reload:
    mov si, [px_rpos]
    mov dx, [px_rend]
    push ds
    mov ds, [px_rsegc]
.b:
    jcxz .done
    or bl, bl
    jnz .put
.lit:                               ; LITERALS, a byte at a time and nothing
    cmp si, dx                      ; else: a photograph's stream is mostly
    jae .refill                     ; these, and the run path below costs
    lodsb                           ; four times as much a byte
    cmp al, 0xC0
    jae .run
    stosb
    loop .lit
    jmp short .done
.run:
    and al, 0x3F
    mov bl, al
    cmp si, dx
    jae .refillv
.gv:
    lodsb
    mov bh, al
    jmp short .b
.put:                               ; min(the run, the line's rest), at once
    mov al, bh
    mov bp, cx
    mov cl, bl
    xor ch, ch
    cmp cx, bp
    jbe .n
    mov cx, bp
.n:
    sub bp, cx
    sub bl, cl
    rep stosb
    mov cx, bp
    jmp short .b
.done:
    pop ds
    mov [px_rpos], si
    mov [px_rcnt2], bl
    mov [px_rval], bh
    clc
    jmp short .out
.refill:                            ; spent before a run's first byte
    pop ds
    call px_rnext
    jc .out
    jmp short .reload
.refillv:                           ; ...or between a run's count and value
    pop ds
    call px_rnext
    jc .out
    mov si, [px_rpos]
    mov dx, [px_rend]
    push ds
    mov ds, [px_rsegc]
    jmp short .gv
.out:
    pop bp
    pop si
    pop dx
    pop bx
    pop ax
    ret

; px_pcxconv - the line buffer's planes -> the row buffer's pixels: bits,
; four planes of bits, bytes, or three planes of bytes. Preserves all
px_pcxconv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov es, [px_wseg]               ; ES:DI = the row
    mov ax, [px_wseg]
    add ax, [px_wo_lin]
    mov dx, [px_dbpl]               ; DX = a plane's bytes
    mov cx, [px_cur + PXR_SW]
    mov bl, [px_dbpp]
    mov bh, [px_dnpl]
    mov ds, ax                      ; DS:SI = the line
    xor si, si
    xor di, di
    cld
    cmp bx, 0x0108
    jne .rgb
    rep movsb                       ; 8 x 1: bytes
    jmp .out
.rgb:
    cmp bx, 0x0308
    jne .bits
.r3:                                ; 8 x 3: R, G, B from three planes
    mov al, [si]
    stosb
    mov bx, si
    add bx, dx
    mov al, [bx]
    stosb
    add bx, dx
    mov al, [bx]
    stosb
    inc si
    loop .r3
    jmp short .out
.bits:
    xor bp, bp                      ; BP = x
.bx:
    push cx
    mov bx, bp
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov cx, bp
    and cl, 7
    mov ch, 0x80
    shr ch, cl                      ; CH = the pixel's bit
    xor al, al
    cmp byte [cs:px_dnpl], 1
    je .p1
    mov cl, 0                       ; four planes: bit p from plane p
    push bx
.pl:
    test [bx], ch
    jz .pz
    mov ah, 1
    shl ah, cl
    or al, ah
.pz:
    add bx, dx
    inc cl
    cmp cl, 4
    jb .pl
    pop bx
    jmp short .pp
.p1:
    test [bx], ch
    jz .pp
    mov al, 1
.pp:
    stosb
    inc bp
    pop cx
    loop .bx
.out:
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

; --- TGA ------------------------------------------------------------------------------
px_dtga:
    mov byte [px_rcnt2], 0
    xor bp, bp                      ; BP = i
.row:
    cmp bp, [px_cur + PXR_SH]
    jb .r1
    clc
    ret
.r1:
    test byte [px_dkind], 8
    jnz .rle
    mov ax, [px_cur + PXR_SW]       ; raw: the row at once, then converted
    mov cl, [px_dpsz]
    xor ch, ch
    mul cx
    mov cx, ax
    call px_rawrow
    jc .ret
    call px_tgaconv
    jmp short .emit
.rle:
    xor di, di                      ; DI = x
.px:
    cmp di, [px_cur + PXR_SW]
    jae .emit
    cmp byte [px_rcnt2], 0
    jne .have
    call px_rb
    jc .trunc
    mov ah, al
    and al, 0x7F
    inc al
    mov [px_rcnt2], al
    mov [px_rraw], ah
    test ah, 0x80
    jz .have                        ; raw: each pixel comes as it is used
    call px_tgapix                  ; a run: its one value, now
    jc .trunc
.have:
    test byte [px_rraw], 0x80
    jnz .val
    call px_tgapix
    jc .trunc
.val:
    call px_tgaput                  ; [px_rpix] at x, converted
    dec byte [px_rcnt2]
    inc di
    jmp short .px
.emit:
    mov ax, bp
    call px_rowy
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp short .row
.trunc:
    mov ax, PXD_TRUNC
    stc
.ret:
    ret

; px_tgapix - the next [px_dpsz] bytes of the stream into [px_rpix]. CF = 1
; at the end. Preserves all but AX
px_tgapix:
    push cx
    push di
    mov cl, [px_dpsz]
    xor ch, ch
    mov di, px_rpix
.l:
    call px_rb
    jc .out
    mov [di], al
    inc di
    loop .l
    clc
.out:
    pop di
    pop cx
    ret

; px_tgaput - [px_rpix] as pixel DI of the row buffer, in the row's format.
; Preserves all
px_tgaput:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cmp byte [px_cur + PXR_RF], RF_RGB
    je .rgb
    mov al, [px_rpix]
    mov es, [px_wseg]
    mov [es:di], al
    jmp short .out
.rgb:
    mov ax, di
    shl di, 1
    add di, ax                      ; DI = 3x
    mov si, px_rpix
    mov cl, [px_dpsz]
    xor ch, ch
    push di
    mov di, px_rtri
    call px_tgacol                  ; ES:SI -> R, G, B at DS:DI
    pop di
    mov es, [px_wseg]
    mov al, [px_rtri]
    mov [es:di], al
    mov al, [px_rtri + 1]
    mov [es:di + 1], al
    mov al, [px_rtri + 2]
    mov [es:di + 2], al
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; px_tgaconv - a raw TGA row in the row buffer -> the row's format: 8 bits as
; they are, 24 swapped, 32 narrowed (left to right), 15/16 widened (right to
; left). Preserves all
px_tgaconv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov al, [px_dpsz]
    cmp al, 1
    je .out
    cmp al, 3                       ; 24: B, G, R as it is ([px_bgr])
    je .out
.n3:
    mov cx, [px_cur + PXR_SW]
    mov es, [px_wseg]
    cmp al, 4
    jne .two
    xor si, si                      ; B, G, R, A -> R, G, B
    xor di, di
.l4:
    mov al, [es:si + 2]
    mov ah, [es:si + 1]
    mov bl, [es:si]
    mov [es:di], al
    mov [es:di + 1], ah
    mov [es:di + 2], bl
    add si, 4
    add di, 3
    loop .l4
    jmp short .out
.two:
    mov si, cx                      ; 5:5:5, from the last one back
    dec si
    shl si, 1
    mov di, cx
    dec di
    mov ax, di
    shl di, 1
    add di, ax
.l2:
    push cx
    mov ax, [es:si]
    mov bx, ax
    mov cl, 10
    shr bx, cl
    and bx, 31
    mov dl, [px_lut5 + bx]
    mov [es:di], dl
    mov bx, ax
    mov cl, 5
    shr bx, cl
    and bx, 31
    mov dl, [px_lut5 + bx]
    mov [es:di + 1], dl
    mov bx, ax
    and bx, 31
    mov dl, [px_lut5 + bx]
    mov [es:di + 2], dl
    sub si, 2
    sub di, 3
    pop cx
    loop .l2
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

px_lut5:    db 0, 8, 16, 25, 33, 41, 49, 58, 66, 74, 82, 90, 99, 107, 115, 123
            db 132, 140, 148, 156, 165, 173, 181, 189, 197, 206, 214, 222
            db 230, 239, 247, 255

; --- PNM ------------------------------------------------------------------------------

; px_pnmlut - the work claim's first table: v (0..255) -> (min(v, max) x 255
; + max / 2) div max, for a largest value under 256. Preserves all
px_pnmlut:
    push ax
    push bx
    push dx
    push di
    push es
    call px_lutseg
    xor di, di
.l:
    mov ax, di
    call px_pnmscale
    mov [es:di], al
    inc di
    cmp di, 256
    jb .l
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    ret

; px_pnmscale - AX = a sample: AL = it scaled to 0..255 by the largest value
; (clamped to it first). Preserves all but AX
px_pnmscale:
    push bx
    push dx
    mov bx, [px_dmaxv]
    cmp ax, bx
    jbe .in
    mov ax, bx
.in:
    mov dx, 255
    mul dx
    push bx
    shr bx, 1
    add ax, bx
    adc dx, 0
    pop bx
    div bx
    pop dx
    pop bx
    ret

; px_rbe - px_rb for the text tokens: CF = 1 at the end. (px_tokget's shape)
px_rbe:
    jmp px_rb

; P1-P3: text samples
px_dpnma:
    mov word [px_tokget], px_rbe
    mov word [px_tokeof], PXD_TRUNC
    call px_pnmlut
    xor bp, bp                      ; BP = y
.row:
    cmp bp, [px_cur + PXR_SH]
    jb .r1
    clc
    ret
.r1:
    mov cx, [px_cur + PXR_SW]       ; samples in the row
    cmp byte [px_dkind], 3
    jne .n
    mov ax, cx
    shl cx, 1
    add cx, ax
.n:
    xor di, di
.s:
    cmp byte [px_dkind], 1
    jne .num
    call px_tkws                    ; P1: one '0' or '1', separators optional
    jc .ret
    sub al, '0'
    cmp al, 1
    ja .data
    jmp short .put
.num:
    push dx
    mov bx, PXD_DATA
    call px_tknum
    pop dx
    jc .ret
    cmp word [px_dmaxv], 256
    jb .lut
    call px_pnmscale
    jmp short .put
.lut:
    cmp ax, 255
    jbe .l1
    mov ax, 255                     ; (above 255 is above the largest too)
.l1:
    push es
    push bx
    call px_lutseg
    mov bx, ax
    mov al, [es:bx]
    pop bx
    pop es
.put:
    push es
    mov es, [px_wseg]
    mov [es:di], al
    pop es
    inc di
    loop .s
    mov ax, bp
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp short .row
.data:
    mov ax, PXD_DATA
    stc
.ret:
    ret

; P4-P6: binary samples
px_dpnmr:
    call px_pnmlut
    xor bp, bp
.row:
    cmp bp, [px_cur + PXR_SH]
    jb .r1
    clc
    ret
.r1:
    mov cx, [px_cur + PXR_SW]
    cmp byte [px_dkind], 4
    jne .gr
    add cx, 7                       ; P4: bits, a row padded to a byte
    shr cx, 1
    shr cx, 1
    shr cx, 1
    call px_rawrow
    jc .ret
    mov bl, 1
    call px_unpack
    jmp short .emit
.gr:
    cmp byte [px_dkind], 6          ; samples: w, or 3w
    jne .g1
    mov ax, cx
    shl cx, 1
    add cx, ax
.g1:
    mov [px_rsamp], cx
    cmp word [px_dmaxv], 256
    jae .wide
    call px_rawrow                  ; one byte a sample: in place, by table
    jc .ret
    push ds
    push es
    call px_lutseg
    mov ds, [px_wseg]
    xor si, si
    cld
.t:
    mov bl, [si]
    xor bh, bh
    mov al, [es:bx]
    mov [si], al
    inc si
    loop .t
    pop es
    pop ds
    jmp short .emit
.wide:
    xor di, di                      ; two bytes a sample, big-endian
.w:
    call px_rb
    jc .trunc
    mov ah, al
    call px_rb
    jc .trunc
    call px_pnmscale
    push es
    mov es, [px_wseg]
    mov [es:di], al
    pop es
    inc di
    loop .w
.emit:
    mov ax, bp
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp .row
.trunc:
    mov ax, PXD_TRUNC
    stc
.ret:
    ret

; --- PIX ------------------------------------------------------------------------------
px_dpix:
    xor bp, bp
.row:
    cmp bp, [px_cur + PXR_SH]
    jb .r1
    clc
    ret
.r1:
    mov cx, [px_dstride]
    call px_rawrow
    jc .ret
    mov bl, 4
    call px_unpack
    mov ax, bp
    push bp
    call px_emit
    pop bp
    jc .ret
    inc bp
    jmp short .row
.ret:
    ret
