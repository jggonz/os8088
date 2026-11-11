; =============================================================================
; Browser 1.7 - PNG and JPEG (SPEC.md 71.30), part 1's: the HOST of PiXEL's
; decoder parts, apps/pixel/pxpng.asm and pxjpeg.asm assembled a second time
; as parts 2 and 3. A decoder reads its host's words through DS - part 1's
; block between bri_pxhost and bri_pxhend, which tools/brpxlink.py names for
; it - and calls the services below far, on [cs:PXP_PKG] = part 1's code.
; =============================================================================

BRK_GIF     equ 0                   ; [bri_kind]; the decoder part is the
BRK_PNG     equ 1                   ; kind + 1 (SPEC.md 71.30, 71.31)
BRK_JPEG    equ 2
BRK_EXTRA   equ 3                   ; pxextra.asm: TIFF, ICO, IFF, MacPaint
BRK_SIMP    equ 4                   ; pxsimp.asm: BMP, PCX, TGA, PNM, PIX
BRK_XBM     equ 5                   ; brxbm.asm
BRX_XBM     equ 0x40                ; [bri_xfmt]: .XBM (no PXF_*: PiXEL's
                                    ; formats do not have it)

; bri_pxsniff - the staged file's signature, in pixelsim.sniff's order, and
; else the URL's extension ([bri_xfmt]): [bri_kind] and [bri_fmt] (PXF_*).
; CF=1 none of them. AX, BX, CX, DX clobbered
bri_pxsniff:
    push es
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov es, dx
    mov cx, [bri_rx]
    cmp cx, 8
    jb .ext
    mov ax, BRK_PNG | (PXF_PNG << 8)
    cmp word [es:0], 0x5089
    jne .jpeg
    cmp word [es:2], 0x474E
    jne .ext
    cmp word [es:4], 0x0A0D
    jne .ext
    cmp word [es:6], 0x0A1A
    jne .ext
    jmp .yes
.jpeg:
    mov ax, BRK_JPEG | (PXF_JPEG << 8)
    cmp word [es:0], 0xD8FF
    jne .c32
    cmp byte [es:2], 0xFF
    jne .c32
    jmp .yes
.c32:                               ; the rest on 32 bytes or more
    cmp cx, 32
    jb .ext
    mov bx, [es:0]
    mov ax, BRK_SIMP | (PXF_BMP << 8)
    cmp bx, 'BM'
    je .yesj
    mov ax, BRK_SIMP | (PXF_PCX << 8)
    cmp bl, 0x0A
    jne .tif
    cmp byte [es:2], 1
    jne .tif
    cmp byte [cs:bri_xfmt], PXF_PCX
    je .yesj
.tif:
    mov ax, BRK_EXTRA | (PXF_TIFF << 8)
    cmp bx, 'II'
    jne .mm
    cmp word [es:2], 0x002A
    je .yesj
.mm:
    cmp bx, 'MM'
    jne .pix
    cmp word [es:2], 0x2A00
    je .yesj
.pix:
    mov ax, BRK_SIMP | (PXF_PIX << 8)
    cmp bx, 'O8'
    jne .pnm
    cmp word [es:2], 'PI'
    je .yesj
.pnm:
    mov ax, BRK_SIMP | (PXF_PNM << 8)
    cmp bl, 'P'
    jne .lbm
    cmp bh, '1'
    jb .lbm
    cmp bh, '6'
    jbe .yesj
.lbm:
    mov ax, BRK_EXTRA | (PXF_LBM << 8)
    cmp bx, 'FO'
    jne .ico
    cmp word [es:2], 'RM'
    jne .ico
    cmp word [es:8], 'IL'
    jne .pbm
    cmp word [es:10], 'BM'
    je .yesj
.pbm:
    cmp word [es:8], 'PB'
    jne .ico
    cmp word [es:10], 'M '
    je .yesj
.ico:
    mov ax, BRK_EXTRA | (PXF_ICO << 8)
    or bl, bl
    jnz .xbm
    or bh, bh                       ; (a MacBinary's byte 1 is its name's
    jnz .mac                        ; length)
    mov bx, [es:2]
    dec bx
    cmp bx, 1
    ja .mac
    mov bx, [es:4]
    dec bx
    cmp bx, 254
    ja .mac
    cmp byte [es:9], 0
    je .yesj
.mac:
    mov ax, BRK_EXTRA | (PXF_MAC << 8)
    cmp cx, 69
    jb .xbm
    cmp word [es:65], 'PN'
    jne .xbm
    cmp word [es:67], 'TG'
    je .yesj
.xbm:
    mov ax, BRK_XBM
    xor bx, bx
.xb:
    mov dl, [es:bx]
    cmp dl, [cs:bri_s_define + bx]
    jne .ext
    inc bx
    cmp bx, 7
    jb .xb
.yesj:
    jmp short .yes
.ext:                               ; no signature: the URL's extension
    mov ah, [cs:bri_xfmt]
    mov al, BRK_SIMP
    cmp ah, PXF_TGA
    je .yes
    cmp ah, PXF_PCX
    je .yes
    mov al, BRK_EXTRA
    cmp ah, PXF_MAC
    je .yes
    mov al, BRK_XBM
    cmp ah, BRX_XBM
    je .yes
    pop es
    stc
    ret
.yes:
    mov es, [cs:PXP_SELF]
    mov [es:bri_kind], al
    mov [es:bri_fmt], ah
    pop es
    clc
    ret

bri_s_define: db '#define'

; bri_extof - [bri_xfmt]: the request's path's extension, if it is one
; only it names (TGA, PCX, MAC, PNTG, XBM). After bri_resolve. All kept
bri_extof:
    push ax
    push bx
    push si
    push es
    mov si, br_reqb + 4
    xor bx, bx                      ; BX = past the last '.' of the last name
.l:
    mov al, [si]
    cmp al, ' '
    jbe .end
    cmp al, '?'
    je .end
    inc si
    cmp al, '/'
    jne .dot
    xor bx, bx
.dot:
    cmp al, '.'
    jne .l
    mov bx, si
    jmp .l
.end:
    xor al, al
    or bx, bx
    jz .put
    mov si, bri_xtab
.t:
    mov ah, [cs:si]
    or ah, ah
    jz .put
    push bx
    push si
    inc si
.c:
    mov al, [bx]
    cmp al, 'a'
    jb .u
    sub al, 'a' - 'A'
.u:
    cmp byte [cs:si], 0
    je .cend
    cmp al, [cs:si]
    jne .cn
    inc bx
    inc si
    jmp .c
.cend:                              ; the name is whole: the path's end next
    cmp al, ' '
    jbe .hit
    cmp al, '?'
    je .hit
.cn:
    pop si
    pop bx
.sk:
    inc si
    cmp byte [cs:si], 0
    jne .sk
    inc si
    jmp .t
.hit:
    pop si
    pop bx
    mov al, [cs:si]
.put:
    mov es, [cs:PXP_SELF]
    mov [es:bri_xfmt], al
    pop es
    pop si
    pop bx
    pop ax
    ret

bri_xtab:   db PXF_TGA, 'TGA', 0, PXF_PCX, 'PCX', 0, PXF_MAC, 'MAC', 0
            db PXF_MAC, 'PNTG', 0, BRX_XBM, 'XBM', 0, 0

; bri_pxcall - BX = a vector (PXV_*): the decoder held is stamped - its
; package, part 1's code and data, its own data - and called with DS = part
; 1's data and DI = px_k. CF and AX are its answer. AX, BX, CX, DX, SI, DI,
; BP clobbered
bri_pxcall:
    push es
    push ds
    mov es, [cs:PXP_SELF]
    mov ax, ds
    mov [es:bri_prim], ax           ; (the services' DS)
    mov ax, [cs:bri_pxds]
    mov es, ax
    mov [es:PXH_SELF], ax
    mov ax, cs
    mov [es:PXP_PKG], ax
    mov ax, [cs:PXP_SELF]
    mov [es:PXP_PKGD], ax
    shl bx, 1
    mov ax, [es:PXP_VEC + bx]
    mov es, [cs:PXP_SELF]
    mov [es:bri_pxfar], ax
    mov ax, [cs:bri_pxcs]
    mov [es:bri_pxfar + 2], ax
    mov ds, [cs:PXP_SELF]
    mov di, px_k
    call far [cs:bri_pxfar]
    pop ds
    pop es
    ret

; =============================================================================
; IE_PREP for a decoder (UI task): the part, its HEAD on the whole file, the
; box, the scale and the store (SPEC.md 71.30)
; =============================================================================
bri_pxprep:
    mov al, [cs:bri_kind]
    inc al                          ; PNG part 2 ... XBM part 6
    cmp al, [cs:bri_pxheld]
    je .have
    push ax
    mov al, [cs:bri_pxheld]         ; one decoder at a time: the other goes
    or al, al
    jz .load
    call far [br_im_r_pxdrop]
    mov es, [cs:PXP_SELF]
    mov byte [es:bri_pxheld], 0
.load:
    pop ax
    push ax
    call far [br_im_r_pxload]       ; DX = its code, AX = its data
    pop cx
    jc .nonej
    mov es, [cs:PXP_SELF]
    mov [es:bri_pxcs], dx
    mov [es:bri_pxds], ax
    mov [es:bri_pxheld], cl
.have:
    mov dx, [br_istseg]             ; the head is the file, whole
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov es, [cs:PXP_SELF]
    mov di, px_k
    xor ax, ax
    mov cx, (PXK_SZ + PXR_SZ + 1) / 2 ; the context and the record after
    cld                             ; it, in the host block
    rep stosw
    mov di, px_spal                 ; the palette black, as PiXEL's resident
    mov cx, 768 / 2                 ; leaves it: a part writes only the
    rep stosw                       ; entries it has (MacPaint: white)
    mov [es:px_drawsz], ax
    mov [es:px_bgr], al
    mov [es:px_dlinsz], ax
    mov al, [cs:bri_fmt]
    mov [es:px_cur + PXR_FMT], al
    mov [es:px_k + PXK_HSEG], dx
    mov [es:px_flatseg], dx
    mov [es:px_hseg], dx
    mov ax, [bri_rx]
    mov [es:px_k + PXK_HLEN], ax
    mov [es:px_hlen], ax
    mov [es:px_cur + PXR_FSIZE], ax ; (a PCX's tail is found from it)
    mov [es:px_k + PXK_FSZ], ax
    mov [es:px_flatlen], ax
    mov word [es:px_flatlen + 2], 0
    mov byte [es:px_rflat], 1
    mov word [es:px_hcap], 2048
    mov word [es:px_clsz], 512
    mov bx, PXV_HEAD
    call bri_pxcall
    jnc .head
.nonej:
    jmp .none
.head:
    mov es, [cs:PXP_SELF]
    mov ax, [cs:px_k + PXK_SW]
    mov [bri_gw], ax
    mov [es:bri_sw], ax
    mov ax, [cs:px_k + PXK_SH]
    mov [bri_gh], ax
    mov [es:bri_sh], ax
    mov al, [cs:px_k + PXK_RF]
    mov [es:bri_rf], al
    call bri_size
    jc .nonej
    call bri_urecord
    ; --- the scale: a PNG's 1/1; a JPEG's the coarsest offered whose master
    ; is still the box's picture each way
    xor cl, cl
    cmp byte [cs:bri_kind], BRK_JPEG
    jne .scl
    mov cl, 3
.try:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    cmp word [cs:px_k + PXK_DSC + bx], 0xFFFF
    je .tn
    call bri_mdim                   ; AX = the master's width, DX its height
    cmp ax, [bri_tw]
    jb .tn
    cmp dx, [bri_th]
    jae .scl
.tn:
    or cl, cl
    jz .fine
    dec cl
    jmp .try
.fine:                              ; none is big enough: the finest offered
    xor cl, cl
.f:
    mov bl, cl
    xor bh, bh
    shl bx, 1
    cmp word [cs:px_k + PXK_DSC + bx], 0xFFFF
    jne .scl
    inc cl
    cmp cl, 4
    jb .f
    jmp .none
.scl:
    mov es, [cs:PXP_SELF]
    mov [es:px_k + PXK_SCL], cl
    call bri_mdim
    mov [es:px_k + PXK_MW], ax
    mov [es:px_k + PXK_MH], dx
    mov [bri_gw], ax                ; the column map and bri_tyof read the
    mov [bri_gh], dx                ; MASTER from here to the decode's end
    mov [bri_fw], ax
    mov [bri_fh], dx
    xor bx, bx
    mov [bri_fl], bx
    mov [bri_ft], bx
    mov ax, [cs:px_k + PXK_DPARA]   ; the scratch: a PNG's, or a JPEG's at
    cmp byte [cs:bri_kind], BRK_JPEG ; its scale
    jne .dp
    mov bl, cl
    shl bx, 1
    mov ax, [cs:px_k + PXK_DSC + bx]
.dp:
    cmp byte [cs:bri_kind], BRK_SIMP ; pxsimp's work: a PCX line and a KB
    jne .dps                        ; of tables (SPEC.md 71.31)
    mov ax, [cs:px_dlinsz]
    add ax, 15
    rcr ax, 1
    mov cl, 3
    shr ax, cl
    add ax, 1024 / 16 + 1
.dps:
    mov [es:bri_pxdp], ax
    mov ax, [cs:px_k + PXK_MW]      ; the row buffer: the master's width, a
    cmp byte [cs:bri_rf], RF_RGB    ; pixel three bytes in truecolour
    jne .rb
    mov dx, ax
    shl ax, 1
    add ax, dx
.rb:
    cmp byte [cs:bri_kind], BRK_EXTRA ; the parts after JPEG: PiXEL's work
    jb .rbk                         ; row, 3w + 4 or the raw row
    mov ax, [cs:bri_sw]
    mov dx, ax
    shl ax, 1
    add ax, dx
    add ax, 4
    cmp ax, [cs:px_drawsz]
    jae .rbk
    mov ax, [cs:px_drawsz]
.rbk:
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov [es:bri_pxrp], ax
    mov ax, [bri_rx]                ; the store: the file, the scratch, the
    call bri_paras                  ; row buffer and the copy
    add ax, [cs:bri_pxdp]
    jc .none
    add ax, [cs:bri_pxrp]
    jc .none
    mov dx, ax                      ; DX = up to the copy
    mov ax, [bri_rbytes]
    call bri_paras
    add ax, dx
    jc .none
    call bri_ustore
    jc .none
    mov ax, [br_iready]             ; the copy past them all (the stage stays
    add ax, dx                      ; at [br_iready]: bri_ustore may have
    mov [bri_newp], ax              ; moved it)
    mov byte [br_ie], IE_DEC
    ret
.none:
    call bri_none
    call bri_ushrink
    mov byte [br_ie], IE_NEXT
    ret

; bri_mdim - CL = a scale: AX, DX = the master's width and height,
; floor(S / 2^s) each way, as pxjpeg's pj_fsizes emits it. Others kept
bri_mdim:
    mov ax, [cs:bri_sw]
    shr ax, cl
    mov dx, [cs:bri_sh]
    shr dx, cl
    ret

; =============================================================================
; IE_DEC for a decoder (worker): the segments, the column map, the ground,
; and DECODE; then bri_dec's own end (the copy down, the entry ready)
; =============================================================================
bri_pxdec:
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov [bri_stgs], dx              ; the file
    mov bx, ax
    mov ax, [bri_rx]
    call bri_paras
    add ax, bx
    mov dx, [br_istseg]
    call far [br_im_r_imgsub]
    mov es, [cs:PXP_SELF]
    mov [es:px_k + PXK_DSEG], dx    ; the scratch
    add ax, [cs:bri_pxdp]
    mov dx, [br_istseg]
    call far [br_im_r_imgsub]
    mov [es:px_k + PXK_RSEG], dx    ; the row buffer
    mov [es:bri_rseg], dx
    mov [es:px_wseg], dx            ; (pxsimp's row)
    mov dx, [cs:px_k + PXK_DSEG]
    mov [es:px_wslin], dx           ; ...its PCX line, then its tables
    mov ax, [cs:px_dlinsz]
    add ax, 15
    rcr ax, 1
    mov cl, 3
    shr ax, cl
    add dx, ax
    mov [es:px_wslut], dx
    mov ax, [bri_stgs]              ; the ring: the file, one window
    mov [es:px_rsegc], ax
    mov [es:px_flatseg], ax         ; the file where bri_ustore left it (an
    mov [es:px_hseg], ax            ; XBM reads it whole in DECODE)
    mov [es:px_k + PXK_HSEG], ax
    xor ax, ax
    mov [es:px_rpos], ax
    mov [es:px_rbase], ax
    mov [es:px_rbase + 2], ax
    mov [es:px_sbase], ax
    mov [es:px_sbase + 2], ax
    mov ax, [bri_rx]
    mov [es:px_rend], ax
    mov dx, [br_istseg]
    mov ax, [bri_newp]
    call far [br_im_r_imgsub]
    mov [bri_news], dx              ; the copy
    mov word [es:px_k + PXK_SPAL], px_spal
    mov bx, px_k + PXK_SVC          ; the services, part 1's offsets: far on
    mov si, bri_svctab              ; [cs:PXP_PKG]
    mov cx, PXS_N
.svc:
    mov ax, [cs:si]
    mov [es:bx], ax
    inc si
    inc si
    inc bx
    inc bx
    loop .svc
    xor ax, ax
    mov [es:bri_nxt], al
    mov [es:bri_planed], al
    call bri_colmap
    call bri_ground
    call bri_gchk
    jc .stale
    mov bx, PXV_DECODE
    call bri_pxcall
    push ds
    pop es
    mov dx, [cs:bri_sw]             ; the source's size again, as the record
    mov [bri_gw], dx                ; and a kept slot know it
    mov dx, [cs:bri_sh]
    mov [bri_gh], dx
    jc .no
    jmp bri_dec.done
.no:
    cmp ax, PXD_ABORT
    je .stale                       ; stopped: nothing more is ours
    jmp bri_dec.short
.stale:
    ret

bri_svctab:
    dw bri_sv_next, bri_sv_emit, bri_sv_scat, bri_sv_tick, bri_sv_pal
    dw bri_sv_cols, bri_sv_ring
%if ($ - bri_svctab) != 2 * PXS_N
%error "bri_svctab does not have PXS_N services"
%endif

; =============================================================================
; THE SERVICES: far-called by a decoder, each with its own DS (the Browser's)
; and answering CF; CF = 1 AX = PXD_ABORT once the engine has stopped
; =============================================================================

; K_NEXT - the staged file whole, then the end
bri_sv_next:
    push ds
    mov ds, [cs:bri_prim]
    cmp byte [cs:bri_nxt], 0
    jne .end
    push ax
    mov ax, 1
    push es
    mov es, [cs:PXP_SELF]
    mov [es:bri_nxt], al
    pop es
    pop ax
    mov es, [bri_stgs]
    xor si, si
    mov cx, [bri_rx]
    pop ds
    clc
    retf
.end:
    pop ds
    stc
    retf

; K_RING - not these parts' (the simple part's): the end
bri_sv_ring:
    stc
    retf

; K_TICK - nothing but the stop
bri_sv_tick:
    push ds
    mov ds, [cs:bri_prim]
    call bri_gchk
    pop ds
    jc bri_svab
    retf

; K_PAL - the plans, once
bri_sv_pal:
    push ds
    mov ds, [cs:bri_prim]
    call bri_gchk
    jc .ab
    call bri_pxplans
    jc .ab
    pop ds
    clc
    retf
.ab:
    pop ds
    jmp bri_svab

; (the stop's answer, from any service with its DS put back)
bri_svab:
    mov ax, PXD_ABORT
    stc
    retf

; K_EMIT - master row AX, in the row buffer: every box row it fills
bri_sv_emit:
    push ds
    mov ds, [cs:bri_prim]
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call bri_gchk
    jc .ab
    call bri_pxplans
    jc .ab
    push ds
    pop es
    push ax
    call bri_tyof
    mov [bri_ty], ax
    pop ax
    inc ax
    call bri_tyof
    mov [bri_ty1], ax
.ty:
    mov bx, [bri_ty]
    cmp bx, [bri_ty1]
    jae .ok
    call bri_pxtrow                 ; its indices...
    call bri_convrow                ; ...into the copy
    inc word [bri_ty]
    jmp .ty
.ok:
    clc
    jmp short .out
.ab:
    mov ax, PXD_ABORT
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; K_SCAT - an Adam7 pass's row: its pixels at columns CL + k CH of master
; row AX, each at its own column in the row buffer (PiXEL's px_ks_scat
; reads them so). Each box pixel of the row's box rows on one of them
bri_sv_scat:
    push ds
    mov ds, [cs:bri_prim]
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call bri_gchk
    jc .ab
    call bri_pxplans
    jc .ab
    mov es, [cs:PXP_SELF]
    mov [es:bri_sx0], cl
    xor bx, bx                      ; the step's shift: CH is 1, 2, 4 or 8
.sh:
    cmp ch, 1
    jbe .shd
    shr ch, 1
    inc bx
    jmp .sh
.shd:
    mov [es:bri_ssh], bl
    push ds
    pop es
    push ax
    call bri_tyof
    mov [bri_ty], ax
    pop ax
    inc ax
    call bri_tyof
    mov [bri_ty1], ax
.ty:
    mov bx, [bri_ty]
    cmp bx, [bri_ty1]
    jae .ok
    xor cx, cx                      ; CX = tx
.tx:
    cmp cx, [bri_tw]
    jae .tn
    mov si, cx
    shl si, 1
    mov ax, [cs:bri_cmap + si]      ; its column
    mov dl, [cs:bri_sx0]
    xor dh, dh
    sub ax, dx
    jb .txn
    push cx
    mov cl, [cs:bri_ssh]
    mov dx, 1
    shl dx, cl
    dec dx
    test ax, dx
    jnz .txp                        ; not this pass's column
    pop cx
    mov ax, [cs:bri_cmap + si]      ; the pixel sits at its own column in the
    call bri_pxat                   ; row buffer (px_ks_scat): AL = its index
    call bri_pplot
    jmp short .txn
.txp:
    pop cx
.txn:
    inc cx
    jmp .tx
.tn:
    inc word [bri_ty]
    jmp .ty
.ok:
    clc
    jmp short .out
.ab:
    mov ax, PXD_ABORT
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; K_COLS - a JPEG's band turned (orientations 5-8): ES:SI = BX master rows
; of DX pixels, from row AX and column CX. Each box pixel inside it
bri_sv_cols:
    push ds
    mov ds, [cs:bri_prim]
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call bri_gchk
    jc .ab
    call bri_pxplans
    jc .ab
    push es
    mov bp, es
    mov es, [cs:PXP_SELF]
    mov [es:bri_cr0], ax
    add ax, bx
    mov [es:bri_cr1], ax
    mov [es:bri_cc0], cx
    add cx, dx
    mov [es:bri_cc1], cx
    mov [es:bri_cbs], bp
    mov [es:bri_cbo], si
    mov ax, dx                      ; a block row's bytes
    cmp byte [cs:bri_rf], RF_RGB
    jne .rs
    shl ax, 1
    add ax, dx
.rs:
    mov [es:bri_cbr], ax
    pop es
    push ds
    pop es
    xor bx, bx                      ; BX = ty
.ty:
    cmp bx, [bri_th]
    jae .ok
    mov ax, bx                      ; its master row: ty H / th
    mul word [bri_gh]
    div word [bri_th]
    cmp ax, [cs:bri_cr0]
    jb .tn
    cmp ax, [cs:bri_cr1]
    jae .tn
    sub ax, [cs:bri_cr0]
    mul word [cs:bri_cbr]
    add ax, [cs:bri_cbo]
    mov di, ax                      ; DI = the block row's start
    xor cx, cx
.tx:
    cmp cx, [bri_tw]
    jae .tn
    mov si, cx
    shl si, 1
    mov ax, [cs:bri_cmap + si]
    cmp ax, [cs:bri_cc0]
    jb .txn
    cmp ax, [cs:bri_cc1]
    jae .txn
    sub ax, [cs:bri_cc0]            ; k, in the block
    call bri_pxblk                  ; AL = its index
    call bri_pplot
.txn:
    inc cx
    jmp .tx
.tn:
    inc bx
    jmp .ty
.ok:
    clc
    jmp short .out
.ab:
    mov ax, PXD_ABORT
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ds
    retf

; =============================================================================
; PIXELS TO INDICES, AND THE PLANS
; =============================================================================

; bri_pxtrow - box row BX's indices into bri_trow, from the row buffer
; through the column map. All kept
bri_pxtrow:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov es, [cs:PXP_SELF]
    mov di, bri_trow
    xor cx, cx                      ; CX = tx
.l:
    cmp cx, [bri_tw]
    jae .out
    mov si, cx
    shl si, 1
    mov ax, [cs:bri_cmap + si]
    push es
    call bri_pxat                   ; AL = the index
    pop es
    stosb
    inc cx
    jmp .l
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; bri_pxat - AX = a pixel of the row buffer, CX = tx, BX = ty: AL = its
; index. AH, DX, SI, ES clobbered
bri_pxat:
    mov es, [cs:bri_rseg]
    mov si, ax
    cmp byte [cs:bri_rf], RF_RGB
    je .rgb
    mov al, [es:si]
    ret
.rgb:
    shl si, 1
    add si, ax                      ; three bytes a pixel
    jmp short bri_cube

; bri_pxblk - AX = a pixel of the K_COLS block row at [cs:bri_cbs]:DI, CX
; = tx, BX = ty: AL = its index. AH, DX, SI, ES clobbered
bri_pxblk:
    mov es, [cs:bri_cbs]
    mov si, ax
    cmp byte [cs:bri_rf], RF_RGB
    je .rgb
    add si, di
    mov al, [es:si]
    ret
.rgb:
    shl si, 1
    add si, ax
    add si, di
    ; fall into bri_cube

; bri_cube - ES:SI = R, G, B (B, G, R: [px_bgr]); CX = tx, BX = ty: AL =
; the cube's point, each channel 0..5 by its threshold at the pixel (SPEC.md
; 71.30). AH, DX, SI clobbered
bri_cube:
    push bp
    push bx
    push cx
    mov al, [bri_bord]
    xor ah, ah
    add bx, ax
    add cx, ax
    and bx, 7
    shl bx, 1
    shl bx, 1
    shl bx, 1
    and cx, 7
    add bx, cx
    mov dh, [cs:px_bayer + bx]      ; DH = the threshold
    xor dl, dl                      ; DL = the point
    mov cx, 3
    mov bp, 1                       ; R, G, B; or B, G, R backwards
    cmp byte [cs:px_bgr], 0
    je .c
    inc si
    inc si
    mov bp, -1
.c:
    mov al, dl                      ; x 6
    shl dl, 1
    add dl, al
    shl dl, 1
    mov bl, [es:si]
    add si, bp
    xor bh, bh
    shl bx, 1
    mov ax, [cs:bri_q6 + bx]        ; AL = the level below, AH = 64 x the
    cmp ah, dh                      ; fraction
    jbe .k
    inc al
.k:
    add dl, al
    loop .c
    mov al, dl
    pop cx
    pop bx
    pop bp
    ret

; bri_pplot - AL = an index, CX = tx, BX = ty: the copy's pixel there, by
; its plan and its threshold (bri_convrow's one pixel). All kept
bri_pplot:
    push ax
    push bx
    push cx
    push dx
    push es
    xor ah, ah
    mov dl, [bri_bord]
    xor dh, dh
    add bx, dx
    add cx, dx                      ; the box's coordinates
    push bx
    push cx
    and bx, 7
    shl bx, 1
    shl bx, 1
    shl bx, 1
    and cx, 7
    add bx, cx
    mov dh, [cs:px_bayer + bx]      ; DH = the threshold
    pop cx
    mov bx, ax
    cmp byte [bri_mono], 0
    je .colour
    xor al, al
    cmp dh, [cs:bri_t1 + bx]
    jae .put
    inc al
    jmp short .put
.colour:
    shl bx, 1
    add bx, ax
    mov al, [cs:bri_ptab + bx]
    cmp dh, [cs:bri_ptab + bx + 2]
    jae .put
    mov al, [cs:bri_ptab + bx + 1]
.put:
    pop bx
    mov es, [bri_news]
    call bri_px
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_pxplans - the plans, once a decode: [px_spal]'s entries (black past
; them) for an indexed picture, the 256 greys, or the cube's 216 points.
; The greys' and the cube's are the same for every picture, so they are made
; once a part 1 load and kept ([bri_pkey]); the greys' plans are the ones
; PiXEL ships (px_plgrey). On a 1bpp adapter the thresholds alone: nothing
; there reads a plan. CF=1 stopped, the plans not made. All kept
bri_pxplans:
    cmp byte [cs:bri_planed], 0
    je .make
    clc
    ret
.make:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [cs:PXP_SELF]
    xor bp, bp                      ; BP = 1: the thresholds alone
    cmp byte [br_bpp], 1
    jne .colour
    inc bp
.colour:
    mov ah, [cs:bri_rf]             ; AH = the key, once these are made
    cmp ah, RF_IDX
    je .fresh                       ; a picture's own palette: every decode
    or bp, bp
    jz .key
    or ah, 80h
.key:
    cmp ah, [cs:bri_pkey]
    je .made                        ; the greys' or the cube's: still here
.fresh:
    mov byte [es:bri_pkey], RF_IDX  ; (none, until this finishes)
    or bp, bp
    jnz .go
    cmp ah, RF_GREY
    jne .go
    push ds                         ; the greys' plans, as PiXEL ships them
    mov ds, [cs:PXP_SELF]
    mov si, px_plgrey
    mov di, bri_ptab
    mov cx, 768 / 2
    cld
    rep movsw
    pop ds
    inc bp                          ; ...so the thresholds alone
.go:
    push ax
    mov di, bri_ptab
    xor bx, bx
.e:
    mov al, [cs:bri_rf]
    cmp al, RF_IDX
    jne .grey
    cmp bx, [cs:px_k + PXK_NPAL]
    jae .black
    mov si, bx
    shl si, 1
    add si, bx
    add si, px_spal                 ; DS:SI = its RGB, in part 1's data
    jmp short .plan
.grey:
    cmp al, RF_GREY
    jne .cubep
    mov [es:bri_rgb], bl
    mov [es:bri_rgb + 1], bl
    mov [es:bri_rgb + 2], bl
    jmp short .own
.cubep:
    cmp bx, 216
    jae .black
    mov ax, bx                      ; R = i / 36, G = i / 6 mod 6, B = i mod 6
    mov cl, 36
    div cl
    mov si, ax
    mov al, ah
    xor ah, ah
    mov cl, 6
    div cl                          ; AL = G, AH = B
    mov cx, ax
    mov ax, si
    xor ah, ah
    mov si, ax
    mov al, [cs:bri_lv6 + si]
    mov [es:bri_rgb], al
    mov al, cl
    xor ah, ah
    mov si, ax
    mov al, [cs:bri_lv6 + si]
    mov [es:bri_rgb + 1], al
    mov al, ch
    mov si, ax
    mov al, [cs:bri_lv6 + si]
    mov [es:bri_rgb + 2], al
.own:
    mov si, bri_rgb
.plan:
    push ds
    mov ds, [cs:PXP_SELF]
    push dx
    or bp, bp
    jnz .t1
    call pl_plan1                   ; AL = c1, AH = c2, DL = t
    mov [es:di], al
    mov [es:di+1], ah
    mov [es:di+2], dl
.t1:
    call px_t1for                   ; AL = t1
    mov [es:bri_t1 + bx], al
    pop dx
    pop ds
    jmp short .n
.black:
    mov word [es:di], 0
    mov byte [es:di+2], 0
    mov byte [es:bri_t1 + bx], 0
.n:
    add di, 3
    test bl, 15
    jnz .nx
    call bri_gchk                   ; the stop, every sixteen
    jc .stop
.nx:
    inc bx
    cmp bx, 256
    jb .e
    pop ax
    mov [es:bri_pkey], ah           ; (RF_IDX: none kept)
.made:
    mov byte [es:bri_planed], 1
    clc
    jmp short .out
.stop:
    pop ax
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

bri_lv6:    db 0, 51, 102, 153, 204, 255

; a channel's place in the cube: the level below, and 64 x what is past it
; (SPEC.md 71.30), made by the assembler
bri_q6:
%assign v 0
%rep 256
    db (v * 5) / 255, (((v * 5) % 255) * 64) / 255
%assign v v + 1
%endrep
