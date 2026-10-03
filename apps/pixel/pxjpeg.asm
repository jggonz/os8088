; =============================================================================
; os8088 - apps/pixel/pxjpeg.asm
;
; PiXEL's JPEG DECODER, a far-called lazy PART (SPEC.md 106.5, 106.19): part
; 3 of PIXEL.O88. Baseline and progressive Huffman, 8-bit, grey or three
; components, decoded STRAIGHT TO A SCALE - the IDCT itself is reduced: 8x8
; at 1/1, 4x4 at 1/2, 2x2 at 1/4, the DC alone at 1/8 - and written into the
; master UPRIGHT by its EXIF orientation. tools/pixelsim.py's JPEG section
; is this file in Python, check for check and in the same order, and every
; operation here is its 16-bit two's complement arithmetic.
;
;   HEAD     the UI task: the marker walk to the frame header, JFIF, Adobe
;            and the first EXIF, a NEW head asked for (PXD_MORE) when a
;            marker is not whole in this one; the sizes, and the decoder
;            scratch each scale needs (PXK_DSC)
;   DECODE   the worker: the walk again on the stream, the tables, the
;            scans - MCU rows of blocks through the IDCT into the component
;            planes, the planes through the colour tables into rows, the
;            rows to the emitter (K_EMIT) or, for orientations 5-8, blocks of
;            master columns (K_COLS)
;
; apps/pixel/pxpart.inc's rules hold. HEAD keeps its few words in this part
; (rule 1, pxgif.asm's way); DECODE keeps EVERYTHING in its scratch - the
; work claim's last region (SPEC.md 106.18) - with DS = ES = that segment
; for the whole decode, so the hot loops need no segment prefix at all.
; The package is reached only through the context and the services.
; =============================================================================

%include "pxpart.inc"
%include "pxrec.inc"
%include "pxlink.inc"                ; LINKED: the package's variables (106.20)
%include "os88api.inc"               ; (OSAPI_TASK_ALIVE, for the plans)

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pj_init, pj_decode, pj_info, pj_head, pj_plans

; PXV_INIT - out AX = PXP_PROBE
pj_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO - nothing to say
pj_info:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_PLANS - [px_plan] for the picture's palette (SPEC.md 106.20): the
; shared source's, apps/pixel/pxplan.inc
pj_plans:
    call pl_plans
    retf


; --- the PRIVATE block of the context: HEAD's facts, for DECODE ---------------
JP_ORIENT   equ PXK_PRIV + 0        ; byte: EXIF orientation 1..8
JP_PF       equ PXK_PRIV + 1        ; byte: JPF_* below
JP_ADOBE    equ PXK_PRIV + 2        ; byte: Adobe's transform
JPF_EXIF    equ 1                   ; an EXIF APP1 has been read
JPF_JFIF    equ 2
JPF_ADOBE   equ 4
JPF_RGB     equ 8                   ; three components that are R, G, B

JP_HEADMAX  equ 2048                ; a head's bytes (pixelsim's HEAD_MAX)
JP_HEADS    equ 16                  ; heads a walk may read (SPEC.md 106.19)
JP_DIMMAX   equ PX_DIMMAX

; =============================================================================
; PXV_HEAD - the UI task: DS = the package, DI = the context. The head is
; [PXK_HSEG]:0, [PXK_HLEN] bytes, the walk starting at [PXK_HPOS]. out CF =
; 0 the picture's facts; CF = 1 AX = a PXD_*, or PXD_MORE with [PXK_HNEXT]
; =============================================================================
pj_head:
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov [cs:pj_hsp], sp
    mov es, [di + PXK_HSEG]
    mov ax, [di + PXK_HLEN]
    mov [cs:pj_hlen], ax
    mov si, [di + PXK_HPOS]
    mov ax, si
    or ax, [di + PXK_HBASE]
    or ax, [di + PXK_HBASE + 2]
    jnz .walk
    ; --- the first head: SOI, and the private facts started afresh -------
    xor ax, ax
    mov [di + JP_ORIENT], al
    mov [di + JP_PF], al
    mov [di + JP_ADOBE], al
    mov byte [di + PXK_HCNT], 1
    mov ax, PXD_HEAD
    cmp word [cs:pj_hlen], 4
    jb .fail
    cmp word [es:0], 0xD8FF
    jne .fail
    mov si, 2
.walk:
    ; --- SI = the next marker. Is it whole in this head? -----------------
    mov bx, 4                       ; BX = the bytes it needs here
    mov ax, si
    add ax, 4
    jc .more
    cmp ax, [cs:pj_hlen]
    ja .more
    cmp byte [es:si], 0xFF
    jne .need
    mov al, [es:si + 1]
    cmp al, 0xC0
    jb .need
    cmp al, 0xC2
    ja .need
    mov ax, [es:si + 2]             ; a frame header: the whole of it
    xchg al, ah
    cmp ax, JP_HEADMAX - 2
    jbe .flen
    mov ax, PXD_HEAD
    jmp .fail
.flen:
    add ax, 2
    cmp ax, bx
    jbe .need
    mov bx, ax
.need:
    mov ax, si
    add ax, bx
    jc .more
    cmp ax, [cs:pj_hlen]
    jbe .whole
.more:
    ; --- not whole: a NEW head at this marker, if the FILE holds it -------
    mov ax, [di + PXK_HBASE]        ; DX:AX = the marker's file offset
    mov dx, [di + PXK_HBASE + 2]
    add ax, si
    adc dx, 0
    mov [di + PXK_HNEXT], ax
    mov [di + PXK_HNEXT + 2], dx
    add ax, bx                      ; ...and where its bytes end
    adc dx, 0
    cmp dx, [di + PXK_FSZ + 2]
    jb .infile
    ja .short
    cmp ax, [di + PXK_FSZ]
    jbe .infile
.short:
    mov ax, PXD_TRUNC
    jmp .fail
.infile:
    inc byte [di + PXK_HCNT]
    mov ax, PXD_HEAD
    cmp byte [di + PXK_HCNT], JP_HEADS
    ja .fail
    mov ax, PXD_MORE
    jmp .fail
.whole:
    ; --- the marker --------------------------------------------------------
    mov ax, PXD_HEAD
    cmp byte [es:si], 0xFF
    jne .fail
    mov al, [es:si + 1]
    cmp al, 0xFF                    ; fill
    jne .nf
    inc si
    jmp .walk
.nf:
    cmp al, 0x01                    ; TEM and RSTn stand alone
    je .two
    cmp al, 0xD0
    jb .seg
    cmp al, 0xD7
    jbe .two
    cmp al, 0xD9                    ; SOI, EOI before a frame
    ja .seg
    mov ax, PXD_HEAD
    jmp .fail
.two:
    add si, 2
    jmp .walk
.seg:
    mov cx, [es:si + 2]
    xchg cl, ch                     ; CX = the length
    cmp cx, 2
    jae .lenok
    mov ax, PXD_HEAD
    jmp .fail
.lenok:
    cmp al, 0xC0
    jb .app
    cmp al, 0xC2
    ja .othsof
    jmp pj_sofhead                  ; the frame header: the walk ends there
.othsof:
    cmp al, 0xCF
    ja .app
    cmp al, 0xC4                    ; DHT, JPG, DAC are not frames
    je .skip
    cmp al, 0xC8
    je .skip
    cmp al, 0xCC
    je .skip
    mov ax, PXD_PACK                ; lossless, hierarchical, arithmetic
    jmp .fail
.app:
    ; --- APP0 JFIF, APP14 Adobe, the first APP1 EXIF: from what the head
    ; holds of the segment. BP = those bytes, at ES:SI + 4 ------------------
    mov bp, si
    add bp, cx
    jc .hold                        ; (the segment runs past 64K: the head
    add bp, 2                       ; holds what it holds)
    jc .hold
    cmp bp, [cs:pj_hlen]
    jbe .held
.hold:
    mov bp, [cs:pj_hlen]
.held:
    sub bp, si
    sub bp, 4                       ; >= 0: SI + 4 <= the head's length
    cmp al, 0xE0
    jne .a14
    cmp cx, 2 + 14
    jb .skip
    cmp bp, 5
    jb .skip
    cmp word [es:si + 4], 'JF'
    jne .skip
    cmp word [es:si + 6], 'IF'
    jne .skip
    cmp byte [es:si + 8], 0
    jne .skip
    or byte [di + JP_PF], JPF_JFIF
    jmp short .skip
.a14:
    cmp al, 0xEE
    jne .a1
    cmp cx, 2 + 12
    jb .skip
    cmp bp, 12
    jb .skip
    cmp word [es:si + 4], 'Ad'
    jne .skip
    cmp word [es:si + 6], 'ob'
    jne .skip
    cmp byte [es:si + 8], 'e'
    jne .skip
    or byte [di + JP_PF], JPF_ADOBE
    mov al, [es:si + 15]
    mov [di + JP_ADOBE], al
    jmp short .skip
.a1:
    cmp al, 0xE1
    jne .skip
    test byte [di + JP_PF], JPF_EXIF
    jnz .skip
    cmp bp, 6
    jb .skip
    cmp word [es:si + 4], 'Ex'
    jne .skip
    cmp word [es:si + 6], 'if'
    jne .skip
    cmp word [es:si + 8], 0
    jne .skip
    or byte [di + JP_PF], JPF_EXIF
    call pj_exif                    ; AL = the orientation or 0
    mov [di + JP_ORIENT], al
.skip:
    mov ax, si                      ; DX:AX = the next marker, in this head
    xor dx, dx
    add ax, cx
    adc dx, 0
    add ax, 2
    adc dx, 0
    or dx, dx
    jnz .far
    mov si, ax
    jmp .walk
.far:
    ; past 64K of this head, which no head holds: a NEW head there, if the
    ; file holds its four bytes
    add ax, [di + PXK_HBASE]
    adc dx, [di + PXK_HBASE + 2]
    mov [di + PXK_HNEXT], ax
    mov [di + PXK_HNEXT + 2], dx
    add ax, 4
    adc dx, 0
    cmp dx, [di + PXK_FSZ + 2]
    jb .in2
    ja .sh2
    cmp ax, [di + PXK_FSZ]
    jbe .in2
.sh2:
    mov ax, PXD_TRUNC
    jmp short .fail
.in2:
    jmp .infile
.fail:
    mov sp, [cs:pj_hsp]
    stc
    jmp short pj_hret
pj_hok:
    mov sp, [cs:pj_hsp]
    clc
pj_hret:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    retf

; pj_exif - ES:SI = an APP1 marker whose first six bytes are Exif\0\0, BP
; = the bytes the head holds after its length: AL = the orientation (tag
; 274, a SHORT of count 1, 1..8) or 0. Anything that does not fit is 0:
; EXIF that lies is ignored (SPEC.md 106.19). Preserves all but AX
pj_exif:
    push bx
    push cx
    push dx
    push si
    push bp
    xor ax, ax
    cmp bp, 14
    jb .out
    add si, 10                      ; SI = the TIFF header
    sub bp, 6                       ; BP = its bytes
    mov byte [cs:pj_mm], 0
    cmp word [es:si], 'II'
    jne .mm
    cmp word [es:si + 2], 0x002A
    jne .no
    jmp short .ord
.mm:
    cmp word [es:si], 'MM'
    jne .no
    cmp word [es:si + 2], 0x2A00
    jne .no
    mov byte [cs:pj_mm], 1
.ord:
    mov bx, 4
    call pj_g32                     ; DX:AX = IFD0's offset
    or dx, dx
    jnz .no
    mov bx, ax
    add ax, 2
    jc .no
    cmp ax, bp
    ja .no
    call pj_g16                     ; AX = its entries
    mov cx, ax
    add bx, 2                       ; BX = the first
.e:
    jcxz .no
    mov ax, bx
    add ax, 12
    jc .no
    cmp ax, bp
    ja .no
    call pj_g16
    cmp ax, 0x0112
    je .tag
    add bx, 12
    dec cx
    jmp short .e
.tag:
    add bx, 2
    call pj_g16                     ; the type: SHORT
    cmp ax, 3
    jne .no
    add bx, 2
    call pj_g32                     ; the count: 1
    or dx, dx
    jnz .no
    cmp ax, 1
    jne .no
    add bx, 4
    call pj_g16                     ; the value
    or ah, ah
    jnz .no
    cmp al, 1
    jb .no
    cmp al, 8
    jbe .out
.no:
    xor ax, ax
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pj_g16 / pj_g32 - ES:SI + BX: a TIFF word / dword in its byte order.
; AX, or DX:AX. Preserve all else
pj_g16:
    mov ax, [es:si + bx]
    cmp byte [cs:pj_mm], 0
    je .o
    xchg al, ah
.o:
    ret
pj_g32:
    cmp byte [cs:pj_mm], 0
    jne .mm
    mov ax, [es:si + bx]
    mov dx, [es:si + bx + 2]
    ret
.mm:
    mov dx, [es:si + bx]
    xchg dl, dh
    mov ax, [es:si + bx + 2]
    xchg al, ah
    ret

; =============================================================================
; pj_sofhead - ES:SI = a frame header (SOF0/1/2) whole in the head, CX its
; length, AL its code; DI = the context. The checks of SPEC.md 106.19 in
; order, then the picture's facts and the scratch a scale. Leaves by pj_hok
; or HEAD's .fail
; =============================================================================
pj_sofhead:
    mov byte [cs:pj_fprog], 0
    cmp al, 0xC2
    jne .np
    mov byte [cs:pj_fprog], 1
.np:
    push di
    mov di, pj_fr                   ; the frame's fields, in this part
    push ds
    push cs
    pop ds
    mov dx, PXD_HEAD                ; a refusal "bad header" is HEAD's
    add si, 4
    sub cx, 2
    call pj_sof                     ; AX = a PXD_*, CF
    pop ds
    pop di
    jnc .ok
    jmp pj_head.fail
.ok:
    ; --- the colour space: libjpeg's rule ----------------------------------
    cmp byte [cs:pj_fr + JF_NF], 3
    jne .cs
    test byte [di + JP_PF], JPF_JFIF
    jnz .cs
    test byte [di + JP_PF], JPF_ADOBE
    jz .ids
    cmp byte [di + JP_ADOBE], 0
    jne .cs
    jmp short .rgb
.ids:
    cmp byte [cs:pj_fr + JF_ID], 82
    jne .cs
    cmp byte [cs:pj_fr + JF_ID + JC_SZ], 71
    jne .cs
    cmp byte [cs:pj_fr + JF_ID + 2 * JC_SZ], 66
    jne .cs
.rgb:
    or byte [di + JP_PF], JPF_RGB
.cs:
    ; --- the picture: its size upright, its rows, its scratch --------------
    mov al, [di + JP_ORIENT]
    or al, al
    jnz .o1
    mov al, 1
    mov [di + JP_ORIENT], al
.o1:
    mov bx, [cs:pj_fr + JF_W]
    mov dx, [cs:pj_fr + JF_H]
    cmp al, 5
    jb .o2
    xchg bx, dx
.o2:
    mov [di + PXK_SW], bx
    mov [di + PXK_SH], dx
    mov byte [di + PXK_RF], RF_GREY
    mov byte [di + PXK_BITS], 8
    cmp byte [cs:pj_fr + JF_NF], 1
    je .g
    mov byte [di + PXK_RF], RF_RGB
    mov byte [di + PXK_BITS], 24
.g:
    mov word [di + PXK_NPAL], 0
    mov word [di + PXK_DPARA], 0
    mov bl, PXKF_DCT | PXKF_DSC
    mov byte [di + PXK_PACK], PK_JPEG
    cmp byte [cs:pj_fprog], 0
    je .bl
    mov byte [di + PXK_PACK], PK_JPEGP
    or bl, PXKF_PROG
.bl:
    cmp al, 3
    je .up
    cmp al, 4
    jne .late
.up:
    or bl, PXKF_UP
.late:
    cmp al, 5
    jb .fl
    or bl, PXKF_LATE
.fl:
    mov [di + PXK_FLAGS], bl
    ; --- the scratch a scale (SPEC.md 106.19) ------------------------------
    xor cl, cl
.sc:
    call pj_dsc                     ; AX = paragraphs, or FFFFh
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov [di + PXK_DSC + bx], ax
    inc cl
    cmp cl, 4
    jb .sc
    jmp pj_hok

; --- the FRAME (pj_sof's answers), HEAD's copy in this part -------------------
JF_H        equ 0                   ; word: height
JF_W        equ 2                   ; word: width
JF_NF       equ 4                   ; byte: components
JF_HMAX     equ 5                   ; byte: Y's H (1 for a grey picture)
JF_VMAX     equ 6                   ; byte
JF_MCUX     equ 8                   ; word: MCUs across...
JF_MCUY     equ 10                  ; word: ...and down
JF_HV       equ 12                  ; word: the sum of H x V
JF_C        equ 14                  ; the components, JC_SZ each
JC_ID       equ 0                   ; byte: id
JC_H        equ 1                   ; byte: H
JC_V        equ 2                   ; byte: V
JC_TQ       equ 3                   ; byte: its quantisation table
JC_SZ       equ 4
JF_ID       equ JF_C + JC_ID
JF_OR       equ 26                  ; byte: the orientation (pj_layout's)
JL_N        equ 27                  ; byte: a block's side at the scale
JL_RPX      equ 28                  ; word: the Y plane's pixels across
JL_ROW      equ 30                  ; word: the converted row
JL_PL       equ 32                  ; 3 words: each component's plane
JL_ST       equ 38                  ; 3 words: ...its stride
JL_BLK      equ 44                  ; word: the transposed block (5-8)
JL_SB       equ 46                  ; 3 words: each component's store, in
                                    ; paragraphs from the store's start
JF_SZ       equ 52

; pj_sof - DS:DI = a frame to fill, ES:SI = a frame header's bytes after its
; length, CX = how many, DX = the PXD_* that "bad header" is here (HEAD's
; PXD_HEAD, DECODE's PXD_DATA). The checks in SPEC.md 106.19's order.
; out CF = 0, or CF = 1 AX = a PXD_*. Preserves all but AX
pj_sof:
    push bx
    push cx
    push si
    mov ax, dx
    cmp cx, 6
    jb .no
    mov ax, PXD_DEPTH
    cmp byte [es:si], 8
    jne .no
    mov ax, [es:si + 1]
    xchg al, ah
    mov [di + JF_H], ax
    mov bx, [es:si + 3]
    xchg bl, bh
    mov [di + JF_W], bx
    mov ax, PXD_DIMS
    cmp word [di + JF_H], 0
    je .no
    cmp word [di + JF_H], JP_DIMMAX
    ja .no
    or bx, bx
    jz .no
    cmp bx, JP_DIMMAX
    ja .no
    mov al, [es:si + 5]
    mov [di + JF_NF], al
    cmp al, 1
    je .nf
    cmp al, 3
    je .nf
    mov ax, PXD_DEPTH
    jmp .no
.nf:
    mov ah, 3                       ; the length: 6 + 3 Nf
    mul ah
    add ax, 6
    cmp ax, cx
    mov ax, dx
    jne .no
    ; --- each component -----------------------------------------------------
    xor bx, bx                      ; BX = the component's record offset
    add si, 6
.c:
    mov al, [es:si]
    mov [di + JF_C + bx + JC_ID], al
    mov al, [es:si + 1]
    mov ah, al
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                       ; AH = H
    and al, 15                      ; AL = V
    mov [di + JF_C + bx + JC_H], ah
    mov [di + JF_C + bx + JC_V], al
    cmp ah, 1
    jb .bad
    cmp ah, 4
    ja .bad
    cmp al, 1
    jb .bad
    cmp al, 4
    ja .bad
    mov al, [es:si + 2]
    mov [di + JF_C + bx + JC_TQ], al
    cmp al, 3
    ja .bad
    push bx                         ; the id not seen before
    mov al, [di + JF_C + bx + JC_ID]
.d:
    or bx, bx
    jz .dok
    sub bx, JC_SZ
    cmp al, [di + JF_C + bx + JC_ID]
    jne .d
    pop bx
    jmp .bad
.dok:
    pop bx
    add si, 3
    add bx, JC_SZ
    mov al, [di + JF_NF]
    mov ah, JC_SZ
    mul ah
    cmp bx, ax
    jb .c
    ; --- the sampling ------------------------------------------------------
    cmp byte [di + JF_NF], 1
    jne .three
    mov byte [di + JF_C + JC_H], 1  ; one component: an MCU is a block
    mov byte [di + JF_C + JC_V], 1
    jmp short .samp
.three:
    mov ax, PXD_PACK
    cmp word [di + JF_C + JC_SZ + JC_H], 0x0101   ; Cb and Cr 1 x 1
    jne .no
    cmp word [di + JF_C + 2 * JC_SZ + JC_H], 0x0101
    jne .no
    mov bx, [di + JF_C + JC_H]      ; BL = H, BH = V
    cmp bx, 0x0101
    je .samp
    cmp bx, 0x0201                  ; 1 x 2 (4:4:0)
    je .samp
    cmp bx, 0x0102                  ; 2 x 1 (4:2:2)
    je .samp
    cmp bx, 0x0202                  ; 2 x 2 (4:2:0)
    je .samp
    cmp bx, 0x0104                  ; 4 x 1 (4:1:1)
    jne .no
.samp:
    mov al, [di + JF_C + JC_H]
    mov [di + JF_HMAX], al
    mov al, [di + JF_C + JC_V]
    mov [di + JF_VMAX], al
    mov al, [di + JF_HMAX]          ; MCUs: ceil(W / 8 Hmax), ceil(H / 8 Vmax)
    mov ah, 8
    mul ah
    mov bx, ax
    mov ax, [di + JF_W]
    add ax, bx
    dec ax
    xor dx, dx
    div bx
    mov [di + JF_MCUX], ax
    mov al, [di + JF_VMAX]
    mov ah, 8
    mul ah
    mov bx, ax
    mov ax, [di + JF_H]
    add ax, bx
    dec ax
    xor dx, dx
    div bx
    mov [di + JF_MCUY], ax
    mov al, [di + JF_C + JC_H]      ; the sum of H x V
    mul byte [di + JF_C + JC_V]
    cmp byte [di + JF_NF], 1
    je .hv
    add ax, 2
.hv:
    mov [di + JF_HV], ax
    clc
    jmp short .out
.bad:
    mov ax, dx
.no:
    stc
.out:
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; THE SCRATCH (SPEC.md 106.19): one segment's FIXED part - the state, the
; tables, the de-stuffed buffer, the colour tables - then a scale's row,
; component planes and transposed block, then (progressive) the store in
; paragraphs. Offsets from the scratch's base, DS = ES = it in DECODE
; =============================================================================
JS_ZZ       equ 0x0200              ; 64 bytes: zigzag k -> natural x 2
JS_KN       equ 0x0260              ; 64 bytes: zigzag k -> natural x 2 when
                                    ; the scale keeps it, else FFh
JS_EXT      equ 0x02F0              ; 17 words: (1 << s) - 1
JS_HALF     equ 0x0320              ; 17 words: 1 << (s - 1)
JS_QT       equ 0x0400              ; 4 x 64 words: quantisers, natural
JS_MULT     equ 0x0600              ; 3 x 64 words: a component's multipliers
JS_BLKZ     equ 0x0780              ; 64 words, ZIGZAG order, + 32 of guard
JS_D        equ 0x0840              ; 64 words: dequantised, natural order
JS_WS       equ 0x08C0              ; 64 words: the IDCT's columns
JS_WL       equ 0x0940              ; 64 words: the D offsets written
JS_TMP      equ 0x09C0              ; 64 bytes: the IDCT's temporaries
JS_NMASK    equ 0x0A00              ; 64 words: natural n's column bit, and
                                    ; again in the high byte when n has v > 0
JS_SLOW     equ 0x0A80              ; 8 tables x JH_SLOWSZ: the canonical
JH_MAXC     equ 0                   ; 17 words: last code + 1, 0 none
JH_OFF      equ 34                  ; 17 words: vals index - code
JH_VALS     equ 68                  ; 256 bytes
JH_SLOWSZ   equ 324
JS_HDC      equ 0x1500              ; 4 DC tables x 2 pages: symbol, length
JS_HAC      equ 0x1D00              ; 4 AC tables x 5 pages (below)
JA_VLO      equ 0                   ; an AC table's pages: the value made...
JA_VHI      equ 256
JA_LEN      equ 512                 ; ...the bits to take, 0 = the slow walk
JA_ADV      equ 768                 ; (r + 1) x 2 when the value is made
JA_RS       equ 1024                ; the symbol
JS_CLEAN    equ 0x3100              ; the de-stuffed data...
JS_CLEANSZ  equ 2048
JS_GUARD    equ 320                 ; ...and zeros past its end
JS_COL      equ 0x3A40              ; Cr->R, Cb->B words; Cb->G, Cr->G dwords
JS_RCR      equ JS_COL
JS_BCB      equ JS_COL + 512
JS_GCB      equ JS_COL + 1024
JS_GCR      equ JS_COL + 2048
JS_CLAMP    equ 0x4640              ; 1,024: clamp(i - 384), the colour's
JS_VAR      equ 0x4A40              ; the row, the planes, the block
JS_FIXED    equ JS_VAR

; pj_dsc - CL = a scale, the frame at CS:pj_fr, [cs:pj_fprog], DI = the
; context (its orientation): AX = the scratch's paragraphs at the scale,
; FFFFh where it is not usable. The layout pj_layout makes. Preserves all
; but AX
pj_dsc:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov [cs:pj_dscl], cl
    mov al, [di + JP_ORIENT]
    mov [cs:pj_fr + JF_OR], al
    push cs
    pop ds
    mov di, pj_fr
    mov al, cl
    call pj_layout                  ; AX = the segment's bytes, CF too many
    jc .no
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov si, ax                      ; SI = its paragraphs
    cmp byte [pj_fprog], 0
    je .yes
    cmp byte [pj_dscl], 2
    jb .no
    call pj_stsize                  ; AX = the store's paragraphs, CF
                                    ; (pj_layout left JL_N: 2 or 1)
    jc .no
    add si, ax
    jc .no
    cmp si, 0xF000
    ja .no
.yes:
    mov ax, si
    jmp short .out
.no:
    mov ax, 0xFFFF
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pj_layout - DS:DI = a frame (its JF_OR set), AL = the scale: the
; variable part of the scratch - the converted row (the Y plane's pixels, 3
; bytes each), each component's plane (mcux H N by V N), and the transposed
; block for orientations 5-8 (the row's pixels by Vmax N, 3 bytes each) -
; into JL_*. out AX = the segment's bytes, CF = 1 past 0FFF0h. Preserves
; all but AX
pj_layout:
    push bx
    push cx
    push dx
    push si
    push bp
    mov cl, al
    mov bl, 8
    shr bl, cl
    mov [di + JL_N], bl             ; N
    mov al, [di + JF_HMAX]
    mul bl
    mul word [di + JF_MCUX]
    or dx, dx
    jnz .no
    mov [di + JL_RPX], ax           ; the row's pixels
    mov si, JS_VAR
    mov [di + JL_ROW], si
    mov cx, 3
    mul cx
    or dx, dx
    jnz .no
    add si, ax
    jc .no
    xor bp, bp                      ; BP = the component, x 2
.pl:
    mov [ds:di + JL_PL + bp], si
    mov bx, bp
    shl bx, 1                       ; BX = its record, x 4
    mov al, [di + JF_C + bx + JC_H]
    mul byte [di + JL_N]
    mul word [di + JF_MCUX]
    or dx, dx
    jnz .no
    mov [ds:di + JL_ST + bp], ax       ; the stride
    mov cx, ax
    mov al, [di + JF_C + bx + JC_V]
    mul byte [di + JL_N]            ; the rows
    mul cx
    or dx, dx
    jnz .no
    add si, ax
    jc .no
    add bp, 2
    mov al, [di + JF_NF]
    xor ah, ah
    shl ax, 1
    cmp bp, ax
    jb .pl
    mov [di + JL_BLK], si
    cmp byte [di + JF_OR], 5
    jb .done
    mov al, [di + JF_VMAX]
    mul byte [di + JL_N]
    mul word [di + JL_RPX]
    or dx, dx
    jnz .no
    mov cx, 3
    mul cx
    or dx, dx
    jnz .no
    add si, ax
    jc .no
.done:
    mov ax, si
    cmp ax, 0xFFF0
    ja .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; pj_stsize - DS:DI = a progressive frame, [pj_dscl] (HEAD) or AL... the
; scale in CL: AX = the STORE's paragraphs - a paragraph a block at 1/4, a
; word a block (eight to a paragraph, each component's own) at 1/8 - CF =
; 1 past 0F000h. Fills JL_SB with each component's paragraph offset.
; Preserves all but AX
pj_stsize:
    push bx
    push cx
    push dx
    push si
    push bp
    xor si, si                      ; SI = paragraphs so far
    xor bp, bp
.c:
    mov [ds:di + JL_SB + bp], si
    mov bx, bp
    shl bx, 1
    mov al, [di + JF_C + bx + JC_H]
    xor ah, ah
    mul word [di + JF_MCUX]
    mov cx, ax
    mov al, [di + JF_C + bx + JC_V]
    xor ah, ah
    mul word [di + JF_MCUY]
    mul cx                          ; DX:AX = its blocks
    cmp byte [di + JL_N], 1
    jne .p16
    add ax, 7
    adc dx, 0
    mov cx, 3
.d8:
    shr dx, 1
    rcr ax, 1
    loop .d8
.p16:
    or dx, dx
    jnz .no
    add si, ax
    jc .no
    add bp, 2
    mov al, [di + JF_NF]
    xor ah, ah
    shl ax, 1
    cmp bp, ax
    jb .c
    cmp si, 0xF000
    ja .no
    mov ax, si
    clc
    jmp short .out
.no:
    stc
.out:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; DECODE'S STATE, in the scratch at 0 (DS = ES = the scratch throughout)
; =============================================================================
JV_PKG      equ 0x00                ; word: the package's segment
JV_CTX      equ 0x02                ; word: the context's offset in it
JV_SVC      equ 0x04                ; PXS_N far pointers: the services
JV_SCL      equ 0x1C                ; byte: the scale
JV_OR       equ 0x1D                ; byte: the orientation
JV_PF       equ 0x1E                ; byte: JPF_*
JV_PROG     equ 0x1F                ; byte: the frame is progressive
JV_MW       equ 0x20                ; word: the master...
JV_MH       equ 0x22
JV_RSEG     equ 0x24                ; word: the row buffer
JV_SH       equ 0x26                ; word: the picture's height, upright
JV_WS       equ 0x28                ; word: the stored picture at the scale,
JV_HS       equ 0x2A                ; its whole pixels across and down
JV_FSZ      equ 0x2C                ; dword: the file's size
JV_CONS     equ 0x30                ; dword: the bytes K_NEXT has handed over
JV_ISEG     equ 0x34                ; the window: segment, where, end
JV_IPOS     equ 0x36
JV_IEND     equ 0x38
JV_CEND     equ 0x3A                ; word: the clean data's end
JV_ENDED    equ 0x3C                ; byte: the segment's end was met...
JV_MARK     equ 0x3D                ; byte: ...at this marker, 0 the file's
JV_OVER     equ 0x3E                ; byte: a fill bit has been taken
JV_RI       equ 0x40                ; word: the restart interval
JV_WIN      equ 0x42                ; word: the bit window, banked
JV_CNT      equ 0x44                ; byte: its bits
JV_CPOS     equ 0x46                ; word: the clean data's next byte
JV_HAVEF    equ 0x48                ; byte: a frame has been read
JV_SCANS    equ 0x49                ; byte: scans read
JV_LATCH    equ 0x4A                ; byte: a bit a component latched
JV_QHAVE    equ 0x4B                ; byte: a bit a quantiser defined
JV_HHAVE    equ 0x4C                ; byte: DC tables 0-3, AC 4-7 defined
JV_NS       equ 0x4D                ; byte: the scan's components
JV_SC       equ 0xE0                ; 3 x 8: the scan's components: its
JS_CI       equ 0                   ; index in the frame, its DC table's
JS_DCP      equ 1                   ; page, its AC table's page, and their
JS_ACP      equ 2                   ; slow tables
JS_DCS      equ 4
JS_ACS      equ 6
JV_SS       equ 0x5A                ; bytes: Ss, Se, Ah, Al
JV_SE       equ 0x5B
JV_AH       equ 0x5C
JV_AL       equ 0x5D
JV_PRED     equ 0x5E                ; 3 words: the DC predictions
JV_EOBRUN   equ 0x64                ; word
JV_KMAX     equ 0x66                ; word: past the block's last coefficient
JV_DONE     equ 0x68                ; word: MCUs (units) done this interval
JV_RSTN     equ 0x6A                ; word: restart markers met
JV_MY       equ 0x6C                ; word: the MCU row
JV_MX       equ 0x6E                ; word
JV_Y0       equ 0x70                ; word: the band's first row (scaled)
JV_K        equ 0x72                ; word: the band's rows
JV_SBASE    equ 0x74                ; 3 words: each component's store segment
JV_SLOWP    equ 0x7A                ; word: the slow tables being walked
JV_MASK     equ 0x7C                ; word: columns with any / with an AC
JV_MLT      equ 0x7E                ; word: the component's multipliers
JV_PL       equ 0x80                ; word: the block's place in its plane
JV_ST       equ 0x82                ; word: ...that plane's stride
JV_WLN      equ 0x84                ; word: D offsets written, x 2
JV_P1       equ 0x86                ; word: 1 << Al
JV_M1       equ 0x88                ; word: -1 << Al
JV_BW       equ 0x8A                ; words: a non-interleaved scan's grid
JV_BH       equ 0x8C
JV_UNIT     equ 0x8E                ; word: units in the scan
JV_TOTAL    equ 0x90                ; word: MCUs in the picture
JV_CX       equ 0x92                ; word: the chroma's column pointer
JV_CH       equ 0x94                ; byte: luma pixels a chroma one serves
JV_HN       equ 0x95                ; byte
JV_X        equ 0x96                ; word: pixels left in the row
JV_CDLT     equ 0x98                ; word: Cr's plane less Cb's
JV_COMP     equ 0x9A                ; word: the component (x 2)
JV_BI       equ 0x9C                ; word: the block's index in its store
JV_RCNT     equ 0xA0                ; word: units in this restart interval
JV_J        equ 0xA2                ; word: the band's row
JV_KCAP     equ 0xA4                ; word: the last zigzag k the scale keeps
JV_FR       equ 0x100               ; JF_SZ: the frame, and its layout

JP_SEGMAX   equ JS_CLEANSZ + JS_GUARD   ; a table segment's bytes at most
JP_BIAS     equ (128 << 5) + 16     ; the level shift and the rounding

; =============================================================================
; PXV_DECODE - the worker: DS = the package, DI = the context
; =============================================================================
pj_decode:
    push bp
    push ds
    push es
    cld
    mov es, [di + PXK_DSEG]         ; ES = the scratch
    mov [cs:pj_dseg], es
    mov [es:JV_PKG], ds
    mov [es:JV_CTX], di
    xor bx, bx
    xor si, si
.svc:
    mov ax, [di + PXK_SVC + bx]
    mov [es:JV_SVC + si], ax
    mov [es:JV_SVC + si + 2], ds
    add si, 4
    add bx, 2
    cmp bx, 2 * PXS_N
    jb .svc
    mov al, [di + PXK_SCL]
    mov [es:JV_SCL], al
    mov al, [di + JP_ORIENT]
    mov [es:JV_OR], al
    mov al, [di + JP_PF]
    mov [es:JV_PF], al
    mov al, [di + PXK_FLAGS]
    and al, PXKF_PROG
    mov [es:JV_PROG], al
    mov ax, [di + PXK_MW]
    mov [es:JV_MW], ax
    mov ax, [di + PXK_MH]
    mov [es:JV_MH], ax
    mov ax, [di + PXK_RSEG]
    mov [es:JV_RSEG], ax
    mov ax, [di + PXK_SH]
    mov [es:JV_SH], ax
    mov ax, [di + PXK_FSZ]
    mov [es:JV_FSZ], ax
    mov ax, [di + PXK_FSZ + 2]
    mov [es:JV_FSZ + 2], ax
    push es
    pop ds                          ; DS = ES = the scratch, for good
    mov [cs:pj_dsp], sp             ; the way out from any depth
    call pj_body
    clc
    jmp short pj_dret

; pj_dfail - AX = a PXD_*: out of the decode from wherever it is
pj_dfail:
    ; STKBALANCE-OK: the decode's way out from any depth - SP is put back to
    ; pj_decode's own frame from [pj_dsp], so what was pushed between there
    ; and here is abandoned, pxgif.asm's pg_fail; pj_dret's retf is
    ; pj_decode's
    mov sp, [cs:pj_dsp]
    stc
pj_dret:
    pop es
    pop ds
    pop bp
    retf

; pj_svc - far-call service BX (the service x 4). Whatever it takes and
; answers, it takes and answers
pj_svc:
    call far [JV_SVC + bx]
    ret

; =============================================================================
; pj_body - the decode: the tables made, the stream walked
; =============================================================================
pj_body:
    mov ax, PXD_BIG                 ; a progressive picture below 1/4: the
    cmp byte [JV_PROG], 0           ; resident never asks (SPEC.md 106.19)
    je .s
    cmp byte [JV_SCL], 2
    jae .s
    jmp pj_dfail
.s:
    call pj_tinit
    xor ax, ax
    mov [JV_IPOS], ax
    mov [JV_IEND], ax
    mov [JV_CONS], ax
    mov [JV_CONS + 2], ax
    mov [JV_HAVEF], al
    mov [JV_SCANS], al
    mov [JV_LATCH], al
    mov [JV_QHAVE], al
    mov [JV_HHAVE], al
    mov [JV_RI], ax
    call pj_rbt                     ; the SOI, which HEAD saw
    call pj_rbt
.m:
    call pj_rbt                     ; a marker: FF, fill, its code
    cmp al, 0xFF
    je .m2
    jmp pj_data
.m2:
    call pj_rbt
    cmp al, 0xFF
    je .m2
.have:
    cmp al, 0xD9                    ; EOI
    jne .ne
    cmp byte [JV_HAVEF], 0
    je .eoibad
    cmp byte [JV_PROG], 0
    je .eoibad
    cmp byte [JV_SCANS], 0
    je .eoibad
    jmp pj_output                   ; the store through the IDCT, and done
.eoibad:
    jmp pj_data
.ne:
    cmp al, 0xD8
    je .eoibad
    cmp al, 0xDA
    jne .nsos
    cmp byte [JV_HAVEF], 0
    je .eoibad
.nsos:
    cmp al, 0x01                    ; TEM and RSTn stand alone
    je .m
    cmp al, 0xD0
    jb .seg
    cmp al, 0xD7
    jbe .m
.seg:
    mov [cs:pj_mk], al
    call pj_seglen                  ; CX = the segment's bytes
    mov al, [cs:pj_mk]
    call pj_istab                   ; a table, a frame, a scan: read whole
    jc .skip
    call pj_segread                 ; ...into the clean buffer (cut short at
    mov al, [cs:pj_mk]              ; the file's end); one too long for it
    cmp cx, JP_SEGMAX               ; is damaged once it is read
    jbe .disp
    jmp pj_data
.skip:
    call pj_segskip
    jmp .m
.disp:
    mov si, JS_CLEAN                ; SI = the segment, CX its bytes
    cmp al, 0xDB
    jne .d1
    call pj_dqt
    jmp .m
.d1:
    cmp al, 0xC4
    jne .d2
    call pj_dht
    jmp .m
.d2:
    cmp al, 0xDD
    jne .d3
    cmp cx, 2
    je .dri
    jmp pj_data
.dri:
    mov ax, [si]
    xchg al, ah
    mov [JV_RI], ax
    jmp .m
.d3:
    cmp al, 0xDA
    je .sos
    ; --- a frame header (pj_istab has said it is one) ----------------------
    cmp byte [JV_HAVEF], 0
    jne .fbad
    cmp al, 0xC2
    ja .fbad
    mov di, JV_FR
    mov dx, PXD_DATA
    call pj_sof
    jnc .fok
    jmp pj_dfail
.fbad:
    jmp pj_data
.fok:
    mov byte [JV_HAVEF], 1
    mov al, [JV_OR]
    mov [JV_FR + JF_OR], al
    mov al, [JV_SCL]
    call pj_layout                  ; the planes, the row, the block
    jnc .lok
    mov ax, PXD_BIG
    jmp pj_dfail
.lok:
    mov [cs:pj_vend], ax
    call pj_fsizes
    jmp .m
.sos:
    call pj_sos                     ; the scan's checks, then its data
    cmp byte [JV_PROG], 0
    jne .next
    ret                             ; a baseline picture is whole
.next:
    mov al, [JV_MARK]               ; the marker that ended the scan
    jmp .have

; pj_istab - AL = a marker: CF = 0 when it is one whose segment is read
; whole and parsed (DQT, DHT, DRI, SOS, a frame header), CF = 1 skipped.
; Preserves all
pj_istab:
    cmp al, 0xDB
    je .y
    cmp al, 0xC4
    je .y
    cmp al, 0xDD
    je .y
    cmp al, 0xDA
    je .y
    cmp al, 0xC0
    jb .n
    cmp al, 0xCF
    ja .n
    cmp al, 0xC8
    je .n
    cmp al, 0xCC
    je .n
.y:
    clc
    ret
.n:
    stc
    ret

; pj_fsizes - after the frame: the stored picture's whole pixels at the
; scale, the store's segments, the MCUs. Preserves all
pj_fsizes:
    push ax
    push bx
    push cx
    push dx
    mov cl, [JV_SCL]
    mov ax, [JV_FR + JF_W]
    shr ax, cl
    mov [JV_WS], ax
    mov ax, [JV_FR + JF_H]
    shr ax, cl
    mov [JV_HS], ax
    mov ax, [JV_FR + JF_MCUX]
    mul word [JV_FR + JF_MCUY]
    mov [JV_TOTAL], ax
    cmp byte [JV_FR + JF_NF], 3     ; the colour tables, for YCbCr
    jne .nc
    test byte [JV_PF], JPF_RGB
    jnz .nc
    call pj_ctabs
.nc:
    cmp byte [JV_PROG], 0
    je .out
    mov di, JV_FR
    call pj_stsize                  ; JL_SB: each component's paragraphs
    mov ax, [cs:pj_vend]            ; the store starts past the segment's
    add ax, 15                      ; variable part
    mov cl, 4
    shr ax, cl
    add ax, [cs:pj_dseg]
    xor bx, bx
.sb:
    mov dx, ax
    add dx, [JV_FR + JL_SB + bx]
    mov [JV_SBASE + bx], dx
    add bx, 2
    cmp bx, 6
    jb .sb
    call pj_stzero
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_stzero - the store, every paragraph of it, zeroed. Preserves all
pj_stzero:
    push ax
    push bx
    push cx
    push di
    push es
    mov di, JV_FR
    call pj_stsize                  ; AX = its paragraphs
    mov bx, ax
    mov ax, [JV_SBASE]
    mov es, ax
    xor ax, ax
.z:
    or bx, bx
    jz .out
    mov cx, bx
    cmp cx, 0x0800                  ; 32 KB at a time
    jbe .c
    mov cx, 0x0800
.c:
    sub bx, cx
    push cx
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; words
    xor di, di
    rep stosw
    pop cx
    mov di, es
    add di, cx
    mov es, di
    jmp short .z
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE STREAM, a byte at a time (the marker segments) and in windows
; =============================================================================

; pj_rb - AL = the next byte, CF = 1 at the file's end. Preserves all but AL
pj_rb:
    push si
    mov si, [JV_IPOS]
    cmp si, [JV_IEND]
    jb .get
    call pj_next
    jc .end
    mov si, [JV_IPOS]
.get:
    push es
    mov es, [JV_ISEG]
    mov al, [es:si]
    pop es
    inc si
    mov [JV_IPOS], si
    clc
.end:
    pop si
    ret

; pj_rbt - AL = the next byte; the file ending is `cut short`
pj_rbt:
    call pj_rb
    jc pj_trunc
    ret

pj_trunc:
    mov ax, PXD_TRUNC
    jmp pj_dfail

pj_data:
    mov ax, PXD_DATA
    jmp pj_dfail

; pj_next - K_NEXT: the next window, or CF = 1 (the file's end, a cancel or
; a read error - the resident says which). Preserves all
pj_next:
    push ax
    push bx
    push cx
    push si
    push es
    mov bx, PXS_NEXT * 4
    call pj_svc                     ; ES:SI, CX
    jc .out
    mov [JV_ISEG], es
    mov [JV_IPOS], si
    add si, cx
    mov [JV_IEND], si
    add [JV_CONS], cx
    adc word [JV_CONS + 2], 0
    clc
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; pj_seglen - a segment's length: CX = its bytes after the length. Less
; than 2 is `damaged`. Preserves all but CX
pj_seglen:
    push ax
    call pj_rbt
    mov ch, al
    call pj_rbt
    mov cl, al
    cmp cx, 2
    jb .bad
    sub cx, 2
    pop ax
    ret
.bad:
    jmp pj_data

; pj_segread - the next CX bytes into the clean buffer, as many as fit;
; the rest read and dropped (the segment is damaged then, and the caller
; says so - after reading it whole, as the reference does). Preserves all
pj_segread:
    push ax
    push cx
    push di
    mov di, JS_CLEAN
.r:
    jcxz .out
    call pj_rbt
    cmp di, JS_CLEAN + JP_SEGMAX
    jae .d
    mov [di], al
    inc di
.d:
    dec cx
    jmp short .r
.out:
    pop di
    pop cx
    pop ax
    ret

; pj_segskip - the next CX bytes, unread: a window at a time. Preserves all
pj_segskip:
    push ax
    push cx
.w:
    jcxz .out
    mov ax, [JV_IEND]
    sub ax, [JV_IPOS]
    jnz .h
    call pj_next
    jc pj_trunc
    jmp short .w
.h:
    cmp ax, cx
    jbe .t
    mov ax, cx
.t:
    add [JV_IPOS], ax
    sub cx, ax
    jmp short .w
.out:
    pop cx
    pop ax
    ret

; =============================================================================
; THE TABLES: DQT and DHT, parsed from the segment at DS:SI, CX bytes
; =============================================================================

; pj_dqt - each table: precision 0 or 1, number 0..3, 64 values none 0,
; inside the segment; into JS_QT in NATURAL order
pj_dqt:
    mov dx, si
    add dx, cx                      ; DX = the segment's end
.t:
    cmp si, dx
    jb .one
    ret
.one:
    lodsb
    mov ah, al
    and al, 15                      ; AL = the table
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                       ; AH = the precision
    cmp ah, 1
    ja .bad
    cmp al, 3
    ja .bad
    mov [cs:pj_qp], ah
    mov [cs:pj_th], al
    mov bl, al
    xor bh, bh
    mov cl, 7
    shl bx, cl
    add bx, JS_QT                   ; BX = the table (128 bytes)
    mov cx, 64
    cmp ah, 0
    je .n8
    add cx, 64
.n8:
    mov di, si
    add di, cx
    cmp di, dx
    ja .bad
    xor di, di                      ; DI = k
.k:
    xor ax, ax
    lodsb
    cmp byte [cs:pj_qp], 0
    je .b
    mov ah, al
    lodsb
.b:
    or ax, ax
    jz .bad
    push bx
    mov cl, [JS_ZZ + di]            ; the natural place, x 2
    xor ch, ch
    add bx, cx
    mov [bx], ax
    pop bx
    inc di
    cmp di, 64
    jb .k
    mov cl, [cs:pj_th]              ; this table is defined
    mov al, 1
    shl al, cl
    or [JV_QHAVE], al
    jmp short .t
.bad:
    jmp pj_data

; pj_dht - each table: class 0 or 1, number 0..3, sixteen counts, at most
; 256 symbols, inside the segment; its codes checked and its tables built
pj_dht:
    mov dx, si
    add dx, cx                      ; DX = the segment's end
.t:
    cmp si, dx
    jb .one
    ret
.one:
    lodsb
    mov ah, al
    and al, 15                      ; AL = the number
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                       ; AH = the class
    cmp ah, 1
    ja .bad
    cmp al, 3
    ja .bad
    mov di, si
    add di, 16
    cmp di, dx
    ja .bad
    mov [cs:pj_tc], ah
    mov [cs:pj_th], al
    call pj_hsum                    ; AX = the counts' sum
    cmp ax, 256
    ja .bad
    add di, ax
    cmp di, dx
    ja .bad
    push dx
    call pj_hbuild                  ; SI = the counts, then the symbols
    pop dx
    mov si, di                      ; past them
    jmp short .t
.bad:
    jmp pj_data

; pj_hsum - DS:SI = sixteen counts: AX = their sum. Preserves all else
pj_hsum:
    push bx
    push cx
    xor ax, ax
    xor bx, bx
    mov cx, 16
.s:
    add al, [si + bx]
    adc ah, 0
    inc bx
    loop .s
    pop cx
    pop bx
    ret

; pj_hbuild - DS:SI = a table's sixteen counts and then its symbols,
; [pj_tc]/[pj_th] its class and number. The canonical codes checked as
; jdhuff.c checks them (a length's codes running past it, the all-ones code
; included, is `damaged`; a DC symbol above 15 too), the slow tables, and
; the 256-entry FAST table on the next eight bits. Preserves all
pj_hbuild:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov al, [cs:pj_tc]
    shl al, 1
    shl al, 1
    add al, [cs:pj_th]
    xor ah, ah
    mov bx, JH_SLOWSZ
    mul bx
    add ax, JS_SLOW
    mov bp, ax                      ; BP = the slow tables (DS-relative)
    xor ax, ax                      ; AX = the code
    xor dx, dx                      ; DX = the symbols so far
    mov di, 1                       ; DI = the length
.l:
    mov bx, di
    shl bx, 1
    add bx, bp                      ; BX = this length's words
    push dx
    sub dx, ax
    mov [bx + JH_OFF], dx           ; vals index = OFF + code
    pop dx
    mov bx, si
    mov cl, [bx + di - 1]
    xor ch, ch
    add ax, cx
    jc .bad
    add dx, cx
    cmp di, 16
    je .m
    push cx
    mov cx, di
    mov bx, 1
    shl bx, cl                      ; 1 << l
    pop cx
    cmp ax, bx
    jae .bad
.m:
    mov bx, di
    shl bx, 1
    add bx, bp
    mov word [bx + JH_MAXC], 0
    jcxz .z
    mov [bx + JH_MAXC], ax          ; the last code + 1
.z:
    shl ax, 1
    inc di
    cmp di, 17
    jb .l
    ; --- the symbols: a DC one above 15 is damaged ----------------------
    mov cx, dx                      ; CX = how many
    push si
    add si, 16
    lea di, [ds:bp + JH_VALS]
    jcxz .vd
.v:
    lodsb
    cmp byte [cs:pj_tc], 0
    jne .vs
    cmp al, 15
    ja .badv
.vs:
    mov [di], al
    inc di
    loop .v
.vd:
    pop si
    ; --- the FAST table: clear its length page, then every code of eight
    ; bits or fewer fills the entries it prefixes ------------------------
    mov al, [cs:pj_th]
    xor ah, ah
    cmp byte [cs:pj_tc], 0
    jne .acp
    mov bx, 512
    mul bx
    add ax, JS_HDC
    mov [cs:pj_tpg], ax
    add ax, 256                     ; the DC table's length page
    jmp short .clr
.acp:
    mov bx, 1280
    mul bx
    add ax, JS_HAC
    mov [cs:pj_tpg], ax
    add ax, JA_LEN                  ; the AC table's length and advance
    mov di, ax                      ; pages, both: an entry no short code
    mov cx, 256                     ; fills is LEN 0 and ADV 0 (pj_acs)
    xor ax, ax
    rep stosw
    jmp short .fill
.clr:
    mov di, ax
    mov cx, 128
    xor ax, ax
    rep stosw
.fill:
    xor ax, ax                      ; AX = the code
    lea bx, [ds:bp + JH_VALS]       ; BX = the next symbol
    mov dl, 1                       ; DL = L
.fl:
    mov cl, [si]                    ; this length's count
    inc si
    xor ch, ch
    jcxz .fn
.fs:
    push cx
    mov dh, [bx]                    ; DH = the symbol
    inc bx
    call pj_hfill                   ; AX = code, DL = L, DH = the symbol
    pop cx
    inc ax
    loop .fs
.fn:
    shl ax, 1
    inc dl
    cmp dl, 8
    jbe .fl
    mov cl, [cs:pj_th]              ; this table is defined
    cmp byte [cs:pj_tc], 0
    je .hd
    add cl, 4
.hd:
    mov al, 1
    shl al, cl
    or [JV_HHAVE], al
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.badv:
    pop si
.bad:
    jmp pj_data

; pj_hfill - the fast entries of one code: AX = the code, DL = its length
; L <= 8, DH = its symbol, [pj_tpg] the table's first page, [pj_tc] its
; class. Preserves all
pj_hfill:
    push ax
    push bx
    push cx
    push si
    push di
    mov cl, 8
    sub cl, dl                      ; CL = 8 - L
    mov si, 1
    shl si, cl                      ; SI = the entries it prefixes
    shl ax, cl                      ; AX = the first of them
    mov bx, [cs:pj_tpg]
    add bx, ax                      ; BX = its entry
    cmp byte [cs:pj_tc], 0
    jne .ac
.dc:
    mov [bx], dh
    mov [bx + 256], dl
    inc bx
    dec si
    jnz .dc
    jmp .out
.ac:
    ; for each index: a magnitude whose bits fit too makes the entry WHOLE
    mov [cs:pj_hi], al              ; (the index's low byte = the entry's)
.e:
    mov [bx + JA_RS], dh
    mov al, dh
    and al, 15                      ; s
    jz .codeonly
    mov ah, al
    add ah, dl                      ; L + s
    cmp ah, 8
    ja .codeonly
    ; m = (index >> (8 - L - s)) & ((1 << s) - 1), extended
    mov [bx + JA_LEN], ah
    mov cl, 8
    sub cl, ah
    mov al, [cs:pj_hi]
    xor ah, ah
    shr ax, cl
    mov cl, dh
    and cl, 15                      ; CL = s
    mov di, 1
    shl di, cl
    dec di                          ; DI = (1 << s) - 1
    and ax, di
    mov cx, di
    inc cx
    shr cx, 1                       ; CX = 1 << (s - 1)
    test ax, cx
    jnz .pos
    sub ax, di                      ; negative: m - (2^s - 1)
.pos:
    mov [bx + JA_VLO], al
    mov [bx + JA_VHI], ah
    mov al, dh
    mov cl, 4
    shr al, cl                      ; r
    inc al
    shl al, 1                       ; (r + 1) x 2
    mov [bx + JA_ADV], al
    jmp short .nx
.codeonly:
    mov [bx + JA_LEN], dl
    mov byte [bx + JA_ADV], 0
    mov byte [bx + JA_VLO], 0
    mov byte [bx + JA_VHI], 0
.nx:
    inc bx
    inc byte [cs:pj_hi]
    dec si
    jnz .e
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; pj_tinit - the tables every decode starts with: zigzag, the masks, the
; extension words, the colour clamp, the kept positions at this scale,
; D zeroed
; =============================================================================
pj_tinit:
    push es
    push ds
    pop es
    xor bx, bx                      ; zigzag k -> natural x 2
.z:
    mov al, [cs:pj_zz + bx]
    shl al, 1
    mov [JS_ZZ + bx], al
    inc bx
    cmp bx, 64
    jb .z
    xor bx, bx                      ; (1 << s) - 1 and 1 << (s - 1)
    mov ax, 1
.e:
    mov dx, ax
    dec dx
    mov [JS_EXT + bx], dx
    shr dx, 1
    inc dx
    or bx, bx
    jnz .h
    xor dx, dx
.h:
    mov [JS_HALF + bx], dx
    shl ax, 1
    add bx, 2
    cmp bx, 34
    jb .e
    mov word [JS_EXT + 32], 0xFFFF
    xor bx, bx                      ; the column masks, natural n
.n:
    mov cx, bx
    and cl, 7
    mov al, 1
    shl al, cl
    xor ah, ah
    cmp bx, 8
    jb .n0
    mov ah, al
.n0:
    mov di, bx
    shl di, 1
    mov [JS_NMASK + di], ax
    inc bx
    cmp bx, 64
    jb .n
    mov di, JS_CLAMP                ; the colour's clamp: i - 384 in 0..255
    xor ax, ax
    mov cx, 384
    rep stosb
.c1:
    stosb
    inc al
    jnz .c1
    mov al, 255
    mov cx, 1024 - 384 - 256
    rep stosb
    mov bl, [JV_SCL]                ; the kept positions at this scale:
    xor bh, bh                      ; JS_KN[k] = the natural place x 2, or
    mov ah, [cs:pj_keep + bx]       ; FFh; [JV_KCAP] the last kept k
    mov word [JV_KCAP], 0
    xor bx, bx
.k:
    mov al, [JS_ZZ + bx]
    shr al, 1
    test al, ah
    mov al, 0xFF
    jnz .kn
    mov al, [JS_ZZ + bx]
    mov [JV_KCAP], bx
.kn:
    mov [JS_KN + bx], al
    inc bx
    cmp bx, 64
    jb .k
    mov di, JS_D                    ; D: zero, kept so block to block
    xor ax, ax
    mov cx, 64
    rep stosw
    mov di, JS_BLKZ
    mov cx, 96
    rep stosw
    pop es
    ret

; pj_ctabs - the colour tables, jdcolor.c's exactly (SPEC.md 106.19), for x
; = i - 128: Cr->R = x + hi(26,345 x + 32,768), Cb->B = 2x + hi(-14,942 x +
; 32,768), Cb->G = -22,554 x, Cr->G = 18,734 x - 65,536 x + 32,768 (the
; last two whole, added and their high word taken per pixel pair).
; Preserves all
pj_ctabs:
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = i x 2
.i:
    mov bx, si
    shr bx, 1
    sub bx, 128                     ; BX = x
    mov ax, 26345
    imul bx
    add ax, 0x8000
    adc dx, 0
    add dx, bx
    mov [JS_RCR + si], dx
    mov ax, -14942
    imul bx
    add ax, 0x8000
    adc dx, 0
    add dx, bx
    add dx, bx
    mov [JS_BCB + si], dx
    push si
    shl si, 1                       ; dwords
    mov ax, -22554
    imul bx
    mov [JS_GCB + si], ax
    mov [JS_GCB + si + 2], dx
    mov ax, 18734
    imul bx
    sub dx, bx
    add ax, 0x8000
    adc dx, 0
    mov [JS_GCR + si], ax
    mov [JS_GCR + si + 2], dx
    pop si
    add si, 2
    cmp si, 512
    jb .i
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE SCAN HEADER (SOS), the segment at DS:SI, CX bytes - SPEC.md 106.19's
; checks in the reference's order - then the scan's data
; =============================================================================
pj_sos:
    or cx, cx
    jz .bad
    mov al, [si]                    ; Ns
    or al, al
    jz .bad
    cmp al, [JV_FR + JF_NF]
    ja .bad
    mov [JV_NS], al
    xor ah, ah
    shl ax, 1
    add ax, 4
    cmp ax, cx
    jne .bad
    ; --- each component: framed, once, tables 0..3 ---------------------
    xor bx, bx                      ; BX = the scan's component, x 8
    inc si
.c:
    mov al, [si]                    ; its id
    xor di, di                      ; DI = the frame's component, x 4
    xor dl, dl                      ; DL = its index
.f:
    cmp al, [JV_FR + JF_C + di + JC_ID]
    je .found
    add di, JC_SZ
    inc dl
    cmp dl, [JV_FR + JF_NF]
    jb .f
    jmp .bad
.found:
    push bx                         ; not already in the scan
.u:
    or bx, bx
    jz .uok
    sub bx, 8
    cmp dl, [JV_SC + bx + JS_CI]
    jne .u
    pop bx
    jmp .bad
.uok:
    pop bx
    mov [JV_SC + bx + JS_CI], dl
    mov al, [si + 1]
    mov ah, al
    and al, 15                      ; Ta
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                       ; Td
    cmp ah, 3
    ja .bad
    cmp al, 3
    ja .bad
    mov [JV_SC + bx + JS_DCP], ah   ; (the numbers, for now)
    mov [JV_SC + bx + JS_ACP], al
    add si, 2
    add bx, 8
    mov al, [JV_NS]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp bx, ax
    jb .c
    ; --- Ss, Se, Ah, Al ------------------------------------------------
    mov al, [si]
    mov [JV_SS], al
    mov al, [si + 1]
    mov [JV_SE], al
    mov al, [si + 2]
    mov ah, al
    and al, 15
    mov [JV_AL], al
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1
    mov [JV_AH], ah
    cmp byte [JV_PROG], 0
    je .base
    cmp byte [JV_SS], 0
    jne .acs
    cmp byte [JV_SE], 0
    jne .bad
    jmp short .ahl
.acs:
    mov al, [JV_SS]
    cmp al, [JV_SE]
    ja .bad
    cmp byte [JV_SE], 63
    ja .bad
    cmp byte [JV_NS], 1
    jne .bad
.ahl:
    mov al, [JV_AH]
    or al, al
    jz .al
    dec al
    cmp al, [JV_AL]
    jne .bad
.al:
    cmp byte [JV_AL], 13
    ja .bad
    jmp short .tabs
.bad:
    jmp pj_data
.base:
    mov al, [JV_NS]
    cmp al, [JV_FR + JF_NF]
    je .tabs
    mov ax, PXD_PACK                ; a baseline picture in more scans
    jmp pj_dfail
    ; --- the tables: latched, present --------------------------------------
.tabs:
    xor bx, bx
.t:
    mov dl, [JV_SC + bx + JS_CI]
    mov cl, dl
    mov al, 1
    shl al, cl
    test [JV_LATCH], al
    jnz .lat
    or [JV_LATCH], al
    call pj_mults                   ; DL = the component; CF its table is
    jc .bad                         ; not defined
.lat:
    mov al, 1                       ; its DC table: a baseline scan, and a
    mov cl, [JV_SC + bx + JS_DCP]   ; progressive DC first scan
    shl al, cl
    cmp byte [JV_PROG], 0
    je .ndc
    cmp byte [JV_SS], 0
    jne .nodc
    cmp byte [JV_AH], 0
    jne .nodc
.ndc:
    test [JV_HHAVE], al
    jz .bad
.nodc:
    mov al, 16                      ; its AC table: a baseline scan, and a
    mov cl, [JV_SC + bx + JS_ACP]   ; progressive AC one
    shl al, cl
    cmp byte [JV_PROG], 0
    je .nac
    cmp byte [JV_SS], 0
    je .noac
.nac:
    test [JV_HHAVE], al
    jz .bad
.noac:
    ; the numbers -> the pages and the slow tables
    mov al, [JV_SC + bx + JS_DCP]
    xor ah, ah
    push ax
    mov cx, JH_SLOWSZ
    mul cx
    add ax, JS_SLOW
    mov [JV_SC + bx + JS_DCS], ax
    pop ax
    shl al, 1
    add al, JS_HDC >> 8
    mov [JV_SC + bx + JS_DCP], al
    mov al, [JV_SC + bx + JS_ACP]
    xor ah, ah
    push ax
    add ax, 4
    mov cx, JH_SLOWSZ
    mul cx
    add ax, JS_SLOW
    mov [JV_SC + bx + JS_ACS], ax
    pop ax
    mov ah, al
    shl al, 1
    shl al, 1
    add al, ah                      ; x 5
    add al, JS_HAC >> 8
    mov [JV_SC + bx + JS_ACP], al
    add bx, 8
    mov al, [JV_NS]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp bx, ax
    jae .tdone
    jmp .t
.tdone:
    inc byte [JV_SCANS]
    call pj_binit                   ; the data
    cmp byte [JV_PROG], 0
    jne .prog
    jmp pj_baseline
.prog:
    cmp byte [JV_SS], 0
    je .read
    cmp byte [JV_SCL], 3            ; 1/8 keeps no AC: the scan skipped to
    jne .read                       ; its next marker that is not RSTn
    mov al, 1
    call pj_tomark
    jc .eof
    ret
.read:
    call pj_progscan
    mov al, 1
    call pj_tomark
    jc .eof
    ret
.eof:
    jmp pj_trunc

; pj_mults - DL = a component: its multipliers at the scale, from its
; quantiser as it is now (latched: SPEC.md 106.19). CF = 1 the quantiser
; is not defined. Preserves all
pj_mults:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bl, dl
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov cl, [JV_FR + JF_C + bx + JC_TQ]
    mov al, 1
    shl al, cl
    test [JV_QHAVE], al
    jnz .q
    stc
    jmp short .out
.q:
    mov bl, cl                      ; SI = its quantiser
    xor bh, bh
    mov cl, 7
    shl bx, cl
    lea si, [bx + JS_QT]
    mov al, dl                      ; DI = the component's multipliers
    xor ah, ah
    shl ax, cl
    add ax, JS_MULT
    mov di, ax
    mov bl, [JV_SCL]
    xor bh, bh
    shl bx, 1
    mov bp, [cs:pj_ptab + bx]       ; BP = the scale's prescale (in CS)
    mov cl, [JV_SCL]
    mov ch, 8
    shr ch, cl                      ; CH = N
    xor dh, dh                      ; DH = v
.v:
    xor dl, dl                      ; DL = u
.u:
    mov al, dh                      ; natural x 2: (8 v + u) x 2
    mov ah, 8
    mul ah
    add al, dl
    xor ah, ah
    shl ax, 1
    mov bx, ax
    push dx
    mov ax, [si + bx]               ; q
    push bx
    mov bx, [cs:bp]                 ; P, in order
    add bp, 2
    mul bx                          ; DX:AX = q P
    add ax, 2048
    adc dx, 0
    mov cx, 12
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    or dx, dx
    jnz .cl
    cmp ax, 32767
    jbe .st
.cl:
    mov ax, 32767
.st:
    pop bx
    mov [di + bx], ax
    pop dx
    mov cl, [JV_SCL]
    mov ch, 8
    shr ch, cl
    inc dl
    cmp dl, ch
    jb .u
    inc dh
    cmp dh, ch
    jb .v
    clc
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ENTROPY-CODED DATA (SPEC.md 106.19): DE-STUFFED into the clean buffer,
; then read MSB first through a 16-bit window. In the hot loops DX is the
; window (its next bit is bit 15), CH how many of its bits are real, SI the
; clean buffer's next byte; between blocks they are banked in JV_WIN,
; JV_CNT and JV_CPOS
; =============================================================================

; FILL - one byte into the window: CH < 8 before, 8..15 after. AX, CL
%macro FILL 0
    lodsb
    mov cl, 8
    sub cl, ch
    xor ah, ah
    shl ax, cl
    or dx, ax
    add ch, 8
%endmacro

; pj_binit - a scan's data starts: an empty window and an empty buffer.
; Preserves all
pj_binit:
    mov word [JV_WIN], 0
    mov byte [JV_CNT], 0
    mov word [JV_CPOS], JS_CLEAN
    mov word [JV_CEND], JS_CLEAN
    mov byte [JV_ENDED], 0
    mov byte [JV_MARK], 0
    mov byte [JV_OVER], 0
    ret

; pj_load / pj_bank - the window into DX, CH, SI and back. Preserve all else
pj_load:
    mov dx, [JV_WIN]
    mov ch, [JV_CNT]
    mov si, [JV_CPOS]
    ret
pj_bank:
    mov [JV_WIN], dx
    mov [JV_CNT], ch
    mov [JV_CPOS], si
    ret

; pj_room - before a block: at least JP_AHEAD bytes of clean data ahead of
; SI, or the segment's end met - and then SI kept inside the zeros past
; it, a bit taken from them remembered (JV_OVER). In/out SI. Preserves all
; else
JP_AHEAD    equ 300                 ; > a block's worst, 64 x (16 + 15) bits
pj_room:
    push ax
    mov ax, [JV_CEND]
    sub ax, si
    jl .low
    cmp ax, JP_AHEAD
    jge .out
.low:
    cmp byte [JV_ENDED], 0
    jne .ended
    call pj_destuff
    jmp short .out
.ended:
    mov ax, si
    sub ax, [JV_CEND]
    jbe .out
    cmp ax, 8                       ; eight bytes of zeros loaded is more
    jb .out                         ; than the window holds: some were taken
    mov byte [JV_OVER], 1
    mov si, [JV_CEND]
    add si, 8
.out:
    pop ax
    ret

; pj_destuff - the clean data left (SI .. CEND) moved to the buffer's start,
; then more de-stuffed after it from the windows until the buffer is full
; or the segment ends (FF and a code that is not 0 or FF; the file's end),
; and zeros after the end. out SI = the buffer's start. Preserves all else
pj_destuff:
    push ax
    push bx
    push cx
    push di
    mov cx, [JV_CEND]
    sub cx, si
    jae .mv
    xor cx, cx
.mv:
    mov di, JS_CLEAN
    rep movsb
.f:
    mov ax, JS_CLEAN + JS_CLEANSZ
    sub ax, di                      ; AX = room
    jbe .full
    mov cx, [JV_IEND]
    sub cx, [JV_IPOS]
    jnz .have
    call pj_next
    jnc .f
    mov byte [JV_ENDED], 1          ; the file's end
    mov byte [JV_MARK], 0
    jmp short .end
.have:
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    push di                         ; FF in the window's next CX bytes?
    mov bx, cx
    mov di, [JV_IPOS]
    mov es, [JV_ISEG]
    mov al, 0xFF
    repne scasb
    push ds
    pop es
    pop di
    jne .nf
    sub bx, cx                      ; the bytes before it
    dec bx
    mov cx, bx
    call pj_wcopy
    inc word [JV_IPOS]              ; the FF
.ff:
    call pj_rb                      ; what follows it
    jc .eof
    cmp al, 0xFF                    ; fill
    je .ff
    or al, al
    jnz .mark
    mov al, 0xFF                    ; FF 00: a data FF
    stosb
    jmp short .f
.nf:
    mov cx, bx
    call pj_wcopy
    jmp short .f
.eof:
    mov byte [JV_ENDED], 1
    mov byte [JV_MARK], 0
    jmp short .end
.mark:
    mov [JV_MARK], al
    mov byte [JV_ENDED], 1
.end:
    mov [JV_CEND], di
    mov cx, JS_GUARD / 2
    xor ax, ax
    rep stosw
    jmp short .out
.full:
    mov [JV_CEND], di
.out:
    mov si, JS_CLEAN
    pop di
    pop cx
    pop bx
    pop ax
    ret

; pj_wcopy - CX bytes from the window to ES:DI. Preserves all but CX, DI
pj_wcopy:
    push si
    push ds
    mov si, [JV_IPOS]
    mov ds, [JV_ISEG]
    rep movsb
    pop ds
    mov [JV_IPOS], si
    pop si
    ret

; pj_tomark - the rest of the segment discarded, unread, to the marker
; that ends it; with AL = 1 an RSTn is passed over too. out JV_MARK and
; AL: the marker, or 0 at the file's end (CF = 1). Preserves all else
pj_tomark:
    push bx
    push cx
    push di
    mov [cs:pj_skr], al
.again:
    cmp byte [JV_ENDED], 0
    jne .have
.w:
    mov cx, [JV_IEND]               ; FF in the window?
    sub cx, [JV_IPOS]
    jnz .scan
    call pj_next
    jnc .w
    mov byte [JV_MARK], 0
    mov byte [JV_ENDED], 1
    jmp short .have
.scan:
    mov di, [JV_IPOS]
    mov es, [JV_ISEG]
    mov al, 0xFF
    repne scasb
    push ds
    pop es
    mov [JV_IPOS], di
    jne .w
.ff:
    call pj_rb
    jc .eof
    cmp al, 0xFF
    je .ff
    or al, al
    jz .w
    mov [JV_MARK], al
    mov byte [JV_ENDED], 1
    jmp short .have
.eof:
    mov byte [JV_MARK], 0
    mov byte [JV_ENDED], 1
.have:
    mov al, [JV_MARK]
    or al, al
    jz .none
    cmp byte [cs:pj_skr], 0
    je .ok
    cmp al, 0xD0
    jb .ok
    cmp al, 0xD7
    ja .ok
    mov byte [JV_ENDED], 0          ; an RSTn inside a skipped scan
    jmp short .again
.ok:
    clc
    jmp short .out
.none:
    stc
.out:
    pop di
    pop cx
    pop bx
    ret

; pj_isover - with DX/CH/SI live: CF = 1 when a bit past the segment's end
; has been taken - less the [pj_gb] last bits, which a WHOLE fast entry
; took before the reference would have read them. Preserves all
pj_isover:
    push ax
    cmp byte [JV_OVER], 0
    jne .y
    mov ax, si
    sub ax, [JV_CEND]
    jbe .n
    cmp byte [JV_ENDED], 0
    je .n
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; the bits loaded past the end...
    push cx
    add ch, [cs:pj_gb]
    cmp al, ch                      ; ...more than are still in the window
    pop cx
    ja .y
.n:
    clc
    pop ax
    ret
.y:
    stc
    pop ax
    ret

; pj_badgb - pj_bad, the magnitude bits of the WHOLE entry at BX given
; back first: the reference refuses a coefficient's place before it reads
; them
pj_badgb:
    mov al, [bx + JA_RS]
    and al, 15
    mov [cs:pj_gb], al
    ; fall into pj_bad

; pj_bad - a refusal inside the data: `cut short` when it read past the
; end, else `damaged` (SPEC.md 106.19). DX/CH/SI live
pj_bad:
    call pj_isover
    jc .t
    jmp pj_data
.t:
    jmp pj_trunc

; pj_chk - an MCU's end: `cut short` if a bit past the end was taken. With
; the window banked. Preserves all
pj_chk:
    push dx
    push cx
    push si
    call pj_load
    call pj_isover
    pop si
    pop cx
    pop dx
    jc .t
    ret
.t:
    jmp pj_trunc

; pj_getb - AL = n, 0..16: AX = the next n bits. DX/CH/SI live; preserves
; BX, DI; clobbers CL
pj_getb:
    or al, al
    jnz .go
    xor ax, ax
    ret
.go:
    push bx
    mov bl, al
.again:
    cmp ch, bl
    jae .take
    cmp ch, 8
    ja .split
    FILL
    jmp short .again
.take:
    mov cl, 16
    sub cl, bl
    mov ax, dx
    shr ax, cl
    mov cl, bl
    shl dx, cl
    sub ch, bl
    pop bx
    ret
.split:
    ; 9 <= CH < n <= 16: the CH bits there, then the rest after two fills
    mov bh, bl
    sub bh, ch                      ; BH = the rest
    mov cl, 16
    sub cl, ch
    mov ax, dx
    shr ax, cl                      ; the first CH bits
    push ax
    xor dx, dx
    xor ch, ch
    FILL
    FILL
    mov cl, 16
    sub cl, bh
    mov ax, dx
    shr ax, cl                      ; the last BH bits
    mov cl, bh
    shl dx, cl
    sub ch, bh                      ; (CL = BH, the rest's bits)
    pop bx                          ; BX = the first part...
    shl bx, cl                      ; ...shifted on past the rest
    or ax, bx
    pop bx
    ret

; pj_ext - AX = s bits as read, BL = s: AX = the value they code (Annex
; F's EXTEND). Preserves all but AX
pj_ext:
    push bx
    xor bh, bh
    shl bx, 1
    test ax, [JS_HALF + bx]
    jnz .p
    sub ax, [JS_EXT + bx]
.p:
    pop bx
    ret

; pj_hslow - the CANONICAL WALK (Annex C's maxcode), a bit at a time from
; the first: AL = the symbol of [JV_SLOWP]'s table. A code no table holds
; is refused after sixteen bits, as the reference refuses it. DX/CH/SI
; live; preserves BX, DI
pj_hslow:
    push bx
    push di
    push bp
    mov bp, [JV_SLOWP]
    xor bx, bx                      ; BX = the code
    mov di, 2                       ; DI = the length x 2
.l:
    or ch, ch
    jnz .b
    FILL
.b:
    shl dx, 1
    rcl bx, 1
    dec ch
    cmp bx, [ds:bp + di + JH_MAXC]
    jb .hit
    add di, 2
    cmp di, 34
    jb .l
    jmp pj_bad
.hit:
    add bx, [ds:bp + di + JH_OFF]
    add bx, bp
    mov al, [bx + JH_VALS]
    pop bp
    pop di
    pop bx
    ret

; pj_hdc - BH = a DC table's page, [JV_SLOWP] its slow tables: AL = the
; symbol (the magnitude's bits). DX/CH/SI live; clobbers BL, CL
pj_hdc:
    cmp ch, 8
    jae .p
    FILL
.p:
    mov bl, dh
    mov cl, [bx + 256]
    or cl, cl
    jz .slow
    shl dx, cl
    sub ch, cl
    mov al, [bx]
    ret
.slow:
    jmp pj_hslow

; pj_dcdiff - BH = the DC table's page: AX = the DC difference (decoded,
; its bits read and extended). DX/CH/SI live; preserves BH, DI
pj_dcdiff:
    call pj_hdc
    push bx
    mov bl, al
    call pj_getb
    call pj_ext
    pop bx
    ret

; =============================================================================
; A BLOCK'S COEFFICIENTS, ZIGZAG ORDER, into JS_BLKZ (SPEC.md 106.19). The
; AC loop's fast entry is WHOLE when a code and its magnitude fit eight
; bits: its bits taken, k advanced, the value stored, with no test of the
; symbol; otherwise the symbol's run and size are read as Annex F reads
; them. DI = the next coefficient's place: a coefficient past the 63rd is
; `damaged`, a zero run past it ends the block
; =============================================================================

; pj_acs - BH = the AC table's page, DI = &BLKZ[k]: the block's ACs from k.
; DX/CH/SI live. out [JV_KMAX] = past the last place written
pj_acs:
.ac:
    cmp ch, 8
    jae .pk
    FILL
.pk:
    mov bl, dh                      ; (an entry with no code of eight bits
    mov cl, [bx + JA_LEN]           ; or fewer is LEN 0, ADV 0: the shift
    shl dx, cl                      ; and the subtraction do nothing, and
    sub ch, cl                      ; .code sends it to the walk)
    mov al, [bx + JA_ADV]
    or al, al
    jz .code
    cbw
    add di, ax
    mov al, [bx + JA_VLO]
    mov ah, [bx + JA_VHI]
    mov [di - 2], ax
    cmp di, JS_BLKZ + 128
    jb .ac
    je .done
    jmp pj_badgb                    ; k past 63, from a WHOLE entry
.badk:
    jmp pj_bad                      ; k past 63
.code:
    or cl, cl
    jz .slow
    mov al, [bx + JA_RS]
    jmp short .rs
.slow:
    call pj_hslow
.rs:
    push bx
    mov bl, al
    and bl, 15                      ; BL = s
    jz .zr
    mov bh, al
    mov cl, 4
    shr bh, cl                      ; BH = r
    mov al, bh
    cbw
    shl ax, 1
    add di, ax                      ; k += r
    cmp di, JS_BLKZ + 126
    ja .badk2
    mov al, bl
    call pj_getb
    call pj_ext
    stosw
    pop bx
    cmp di, JS_BLKZ + 128
    jb .ac
    jmp short .done
.badk2:
    pop bx
    jmp short .badk
.zr:
    pop bx
    cmp al, 0xF0                    ; ZRL: sixteen zeros
    jne .done                       ; EOB
    add di, 32
    cmp di, JS_BLKZ + 128
    jb .ac
.done:
    mov [JV_KMAX], di
    ret

; pj_bdec - one block of scan component BX (x 8): the DC (prediction added)
; and the ACs into JS_BLKZ. The window loaded and banked here
pj_bdec:
    push bx
    push di
    call pj_load
    call pj_room                    ; the clean data ahead
    mov ax, [JV_SC + bx + JS_DCS]
    mov [JV_SLOWP], ax
    mov al, [JV_SC + bx + JS_CI]
    xor ah, ah
    shl ax, 1
    mov di, ax                      ; DI = the component x 2
    mov bh, [JV_SC + bx + JS_DCP]
    call pj_dcdiff
    add [JV_PRED + di], ax
    mov ax, [JV_PRED + di]
    mov [JS_BLKZ], ax
    pop di
    pop bx
    push bx
    push di
    mov ax, [JV_SC + bx + JS_ACS]
    mov [JV_SLOWP], ax
    mov bh, [JV_SC + bx + JS_ACP]
    mov di, JS_BLKZ + 2
    call pj_acs
    call pj_bank
    pop di
    pop bx
    ret

; =============================================================================
; DEQUANTISE AND THE IDCT, at the scale (SPEC.md 106.19), into the plane
; at [JV_PL], stride [JV_ST]; [JV_MLT] the component's multipliers
; =============================================================================
pj_bput:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, [JS_BLKZ]               ; the DC, always, the bias on it
    mov bx, [JV_MLT]
    imul word [bx]
    add ax, JP_BIAS
    mov [JS_D], ax
    mov word [JS_WL], 0
    mov word [JV_WLN], 2
    mov word [JV_MASK], 0x0001
    mov cx, [JV_KMAX]               ; the ACs written, up to the last the
    cmp cx, JS_BLKZ + 128           ; scale keeps (JS_KN)
    jbe .kw
    mov cx, JS_BLKZ + 128
.kw:
    sub cx, JS_BLKZ + 2
    jbe .kd
    shr cx, 1
    cmp cx, [JV_KCAP]
    jbe .kc0
    mov cx, [JV_KCAP]
.kc0:
    jcxz .kd
    mov si, JS_BLKZ + 2
.k:
    lodsw
    or ax, ax
    jnz .nz
    loop .k
    jmp short .kd
.nz:
    mov bx, si                      ; k = (SI - BLKZ - 2) / 2: its natural
    sub bx, JS_BLKZ + 2             ; place x 2, or FFh when the scale does
    shr bx, 1                       ; not keep it
    mov bl, [JS_KN + bx]
    cmp bl, 0xFF
    je .kn
    xor bh, bh
    push cx
    mov di, [JV_MLT]
    imul word [di + bx]
    pop cx
    mov [JS_D + bx], ax
    mov di, [JV_WLN]
    mov [JS_WL + di], bx
    add word [JV_WLN], 2
    mov ax, [JS_NMASK + bx]
    or [JV_MASK], ax
.kn:
    loop .k
.kd:
    mov cx, [JV_KMAX]               ; the block's coefficients cleared
    cmp cx, JS_BLKZ + 128
    jbe .kc
    mov cx, JS_BLKZ + 128
.kc:
    sub cx, JS_BLKZ
    shr cx, 1
    mov di, JS_BLKZ
    xor ax, ax
    rep stosw
    mov bl, [JV_SCL]
    xor bh, bh
    shl bx, 1
    call [cs:pj_idtab + bx]
    mov cx, [JV_WLN]                ; D's written words cleared
    xor bx, bx
    xor ax, ax
.wl:
    mov di, [JS_WL + bx]
    mov [JS_D + di], ax
    add bx, 2
    cmp bx, cx
    jb .wl
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; MUL - AX = s16((AX x K) >> 8) for the immediate K, by BX (clobbered) and
; DX: `imul`, and the product's middle word
%macro MULK 1
    or ax, ax                       ; (0 x K is 0: no `imul` for it)
    jz %%z
    mov bx, %1
    imul bx
    mov al, ah
    mov ah, dl
%%z:
%endmacro

; AAN %1 %2 %3 - jidctfst.c's 1-D pass in 16 bits (pixelsim's aan8): the
; eight inputs at [si + k x %1], the eight outputs by OUT8 %2 %3 k, reg.
; Temporaries in JS_TMP. Clobbers AX, BX, CX, DX, SI
%macro AAN 3
    mov ax, [si + 0 * %1]           ; --- the even part ---
    mov bx, [si + 4 * %1]
    mov cx, ax
    add ax, bx                      ; t10 = d0 + d4
    sub cx, bx                      ; t11 = d0 - d4
    mov [JS_TMP + 0], ax
    mov [JS_TMP + 2], cx
    mov ax, [si + 2 * %1]
    mov bx, [si + 6 * %1]
    mov cx, ax
    add cx, bx                      ; t13 = d2 + d6
    sub ax, bx
    MULK 362                        ; MUL(d2 - d6, 1.414)
    sub ax, cx                      ; t12
    mov bx, [JS_TMP + 0]
    mov dx, bx
    add bx, cx                      ; t0 = t10 + t13
    sub dx, cx                      ; t3 = t10 - t13
    mov [JS_TMP + 4], bx
    mov [JS_TMP + 10], dx
    mov bx, [JS_TMP + 2]
    mov dx, bx
    add bx, ax                      ; t1 = t11 + t12
    sub dx, ax                      ; t2 = t11 - t12
    mov [JS_TMP + 6], bx
    mov [JS_TMP + 8], dx
    mov ax, [si + 5 * %1]           ; --- the odd part ---
    mov bx, [si + 3 * %1]
    mov cx, ax
    add cx, bx                      ; z13 = d5 + d3
    sub ax, bx                      ; z10 = d5 - d3
    mov [JS_TMP + 12], ax
    mov ax, [si + 1 * %1]
    mov bx, [si + 7 * %1]
    mov dx, ax
    add ax, bx                      ; z11 = d1 + d7
    sub dx, bx                      ; z12 = d1 - d7
    mov [JS_TMP + 14], dx
    mov bx, ax
    add bx, cx                      ; t7 = z11 + z13
    mov [JS_TMP + 16], bx
    sub ax, cx
    MULK 362                        ; t11 = MUL(z11 - z13, 1.414)
    mov [JS_TMP + 18], ax
    mov ax, [JS_TMP + 12]
    add ax, [JS_TMP + 14]
    MULK 473                        ; z5 = MUL(z10 + z12, 1.848)
    mov cx, ax
    mov ax, [JS_TMP + 14]
    MULK 277
    sub ax, cx                      ; t10 = MUL(z12, 1.082) - z5
    mov [JS_TMP + 20], ax
    mov ax, [JS_TMP + 12]
    MULK -669
    add ax, cx                      ; t12 = MUL(z10, -2.613) + z5
    sub ax, [JS_TMP + 16]
    mov cx, ax                      ; t6 = t12 - t7
    mov bx, [JS_TMP + 18]
    sub bx, cx                      ; t5 = t11 - t6
    mov ax, [JS_TMP + 20]
    add ax, bx                      ; t4 = t10 + t5
    mov dx, [JS_TMP + 10]           ; --- out: AX t4, BX t5, CX t6 ---
    mov si, dx
    add si, ax
    sub dx, ax
    OUT8 %2, %3, 4, si              ; t3 + t4
    OUT8 %2, %3, 3, dx              ; t3 - t4
    mov dx, [JS_TMP + 8]
    mov si, dx
    add si, bx
    sub dx, bx
    OUT8 %2, %3, 2, si              ; t2 + t5
    OUT8 %2, %3, 5, dx              ; t2 - t5
    mov dx, [JS_TMP + 6]
    mov si, dx
    add si, cx
    sub dx, cx
    OUT8 %2, %3, 1, si              ; t1 + t6
    OUT8 %2, %3, 6, dx              ; t1 - t6
    mov dx, [JS_TMP + 4]
    mov ax, [JS_TMP + 16]
    mov si, dx
    add si, ax
    sub dx, ax
    OUT8 %2, %3, 0, si              ; t0 + t7
    OUT8 %2, %3, 7, dx              ; t0 - t7
%endmacro

; OUT8 base stride k reg - [base + k x stride] := reg
%macro OUT8 4
    mov [%1 + %3 * %2], %4
%endmacro

; CLAMP5 - AX = a sum: AL = (AX >> 5) clamped to 0..255. Clobbers CL
%macro CLAMP5 0
    mov cl, 5
    sar ax, cl
    or ah, ah
    jz %%ok
    mov al, 0
    js %%ok
    mov al, 255
%%ok:
%endmacro

; PUT5 reg k - the sum in reg >> 5, clamped, to the plane at [di + k]; CL
; must be 5. Clobbers AX
%macro PUT5 2
%ifnidni %1, ax
    mov ax, %1
%endif
    sar ax, cl
    or ah, ah
    jz %%ok
    mov al, 0
    js %%ok
    mov al, 255
%%ok:
    mov [di + %2], al
%endmacro

; pj_id8 - 1/1: jidctfst.c's columns then rows. A column with no AC is its
; DC copied, and when no column but the first has any coefficient every row
; is its first value - the butterflies would give exactly those (SPEC.md
; 106.19)
pj_id8:
    xor bp, bp                      ; BP = u x 2
.col:
    mov cx, bp
    shr cl, 1
    mov al, [JV_MASK + 1]           ; the columns with an AC
    shr al, cl
    test al, 1
    jnz .full
    mov ax, [ds:JS_D + bp]
    mov di, JS_WS
    add di, bp
%assign k 0
%rep 8
    mov [di + k * 16], ax
%assign k k + 1
%endrep
    jmp .nc
.full:
    lea si, [bp + JS_D]
    lea di, [bp + JS_WS]
    AAN 16, di, 16
.nc:
    add bp, 2
    cmp bp, 16
    jb .col
    mov di, [JV_PL]
    test byte [JV_MASK], 0xFE
    jz .dc
    xor bp, bp                      ; BP = y x 16
.row:
    lea si, [bp + JS_WS]
    AAN 2, JS_TMP + 32, 2
    mov cl, 5                       ; the eight, clamped into the plane
%assign k 0
%rep 8
    PUT5 [JS_TMP + 32 + k * 2], k
%assign k k + 1
%endrep
    add di, [JV_ST]
    add bp, 16
    cmp bp, 128
    jb .row
    ret
.dc:
    xor bp, bp
.dr:
    mov ax, [ds:JS_WS + bp]
    CLAMP5
    mov ah, al
%assign k 0
%rep 4
    mov [di + k * 2], ax
%assign k k + 1
%endrep
    add di, [JV_ST]
    add bp, 16
    cmp bp, 128
    jb .dr
    ret

; RED4 %1 %2 %3 - the 4-point pass (1/2; pixelsim's red4): inputs at [si +
; k x %1], outputs by OUT8 %2 %3 k, reg. Clobbers AX, BX, CX, DX
%macro RED4 3
    mov ax, [si]
    mov cx, [si + 2 * %1]
    mov dx, ax
    add ax, cx                      ; ea = d0 + d2
    sub dx, cx                      ; eb = d0 - d2
    mov [JS_TMP + 0], ax
    mov [JS_TMP + 2], dx
    mov ax, [si + 3 * %1]
    MULK 106
    add ax, [si + 1 * %1]           ; oa = d1 + MUL(d3, tan pi/8)
    mov [JS_TMP + 4], ax
    mov ax, [si + 1 * %1]
    MULK 106
    sub ax, [si + 3 * %1]           ; ob = MUL(d1, tan pi/8) - d3
    mov cx, ax
    mov ax, [JS_TMP + 0]
    mov bx, [JS_TMP + 4]
    mov dx, ax
    add ax, bx
    sub dx, bx
    OUT8 %2, %3, 0, ax              ; ea + oa
    OUT8 %2, %3, 3, dx              ; ea - oa
    mov ax, [JS_TMP + 2]
    mov dx, ax
    add ax, cx
    sub dx, cx
    OUT8 %2, %3, 1, ax              ; eb + ob
    OUT8 %2, %3, 2, dx              ; eb - ob
%endmacro

; pj_id4 - 1/2: the 4x4, columns then rows, the same shortcuts
pj_id4:
    xor bp, bp
.col:
    mov cx, bp
    shr cl, 1
    mov al, [JV_MASK + 1]
    shr al, cl
    test al, 1
    jnz .full
    mov ax, [ds:JS_D + bp]
%assign k 0
%rep 4
    mov [ds:JS_WS + bp + k * 16], ax
%assign k k + 1
%endrep
    jmp short .nc
.full:
    lea si, [bp + JS_D]
    lea di, [bp + JS_WS]
    RED4 16, di, 16
.nc:
    add bp, 2
    cmp bp, 8
    jb .col
    mov di, [JV_PL]
    test byte [JV_MASK], 0x0E
    jz .dc
    xor bp, bp
.row:
    lea si, [bp + JS_WS]
    mov ax, [si]                    ; the 4-point pass, its four outputs
    mov cx, [si + 4]                ; put straight into the plane
    mov dx, ax
    add ax, cx                      ; ea
    sub dx, cx                      ; eb
    mov [JS_TMP + 0], ax
    mov [JS_TMP + 2], dx
    mov ax, [si + 6]
    MULK 106
    add ax, [si + 2]                ; oa
    mov [JS_TMP + 4], ax
    mov ax, [si + 2]
    MULK 106
    sub ax, [si + 6]                ; ob
    mov [JS_TMP + 6], ax
    mov cl, 5
    mov ax, [JS_TMP + 0]
    add ax, [JS_TMP + 4]
    PUT5 ax, 0                      ; ea + oa
    mov ax, [JS_TMP + 0]
    sub ax, [JS_TMP + 4]
    PUT5 ax, 3                      ; ea - oa
    mov ax, [JS_TMP + 2]
    add ax, [JS_TMP + 6]
    PUT5 ax, 1                      ; eb + ob
    mov ax, [JS_TMP + 2]
    sub ax, [JS_TMP + 6]
    PUT5 ax, 2                      ; eb - ob
    add di, [JV_ST]
    add bp, 16
    cmp bp, 64
    jb .row
    ret
.dc:
    xor bp, bp
.dr:
    mov ax, [ds:JS_WS + bp]
    CLAMP5
    mov ah, al
    mov [di], ax
    mov [di + 2], ax
    add di, [JV_ST]
    add bp, 16
    cmp bp, 64
    jb .dr
    ret

; pj_id2 - 1/4: the 2x2 - d0 + d1, d0 - d1 each way
pj_id2:
    mov ax, [JS_D + 0]              ; column 0: d(0,0), d(1,0)
    mov bx, [JS_D + 16]
    mov cx, ax
    add ax, bx
    sub cx, bx
    mov [JS_WS + 0], ax             ; WS row 0, row 1
    mov [JS_WS + 16], cx
    mov ax, [JS_D + 2]              ; column 1
    mov bx, [JS_D + 18]
    mov cx, ax
    add ax, bx
    sub cx, bx
    mov [JS_WS + 2], ax
    mov [JS_WS + 18], cx
    mov di, [JV_PL]
    xor bp, bp
.r:
    mov ax, [ds:JS_WS + bp]
    mov dx, [ds:JS_WS + bp + 2]
    push ax
    add ax, dx
    CLAMP5
    mov [di], al
    pop ax
    sub ax, dx
    CLAMP5
    mov [di + 1], al
    add di, [JV_ST]
    add bp, 16
    cmp bp, 32
    jb .r
    ret

; pj_id1 - 1/8: the DC
pj_id1:
    mov ax, [JS_D]
    CLAMP5
    mov di, [JV_PL]
    mov [di], al
    ret

; =============================================================================
; A BASELINE SCAN: MCU rows of blocks into the planes, each row then out
; =============================================================================
pj_baseline:
    xor ax, ax
    mov [JV_PRED], ax
    mov [JV_PRED + 2], ax
    mov [JV_PRED + 4], ax
    mov [JV_DONE], ax
    mov [JV_RSTN], ax
    mov [JV_RCNT], ax
    mov [JV_MY], ax
    mov ax, [JV_TOTAL]
    mov [JV_UNIT], ax
.my:
    mov word [JV_MX], 0
.mx:
    xor bx, bx                      ; BX = the scan's component, x 8
.c:
    mov al, [JV_SC + bx + JS_CI]
    call pj_cset                    ; its multipliers, plane, stride; AL
    mov byte [cs:pj_v], 0           ; = its H, AH its V
.v:
    mov byte [cs:pj_h], 0
.h:
    mov al, [JV_SC + bx + JS_CI]
    call pj_bplace                  ; [JV_PL] for (mx H + h, v)
    call pj_bdec
    call pj_bput
    inc byte [cs:pj_h]
    mov al, [cs:pj_h]
    cmp al, [cs:pj_ch]
    jb .h
    inc byte [cs:pj_v]
    mov al, [cs:pj_v]
    cmp al, [cs:pj_cv]
    jb .v
    add bx, 8
    mov al, [JV_NS]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp bx, ax
    jb .c
    ; --- the MCU's end: a bit past the end, the restart interval --------
    call pj_chk
    inc word [JV_DONE]
    call pj_rstnext
    inc word [JV_MX]
    mov ax, [JV_MX]
    cmp ax, [JV_FR + JF_MCUX]
    jb .mx
    call pj_band
    inc word [JV_MY]
    mov ax, [JV_MY]
    cmp ax, [JV_FR + JF_MCUY]
    jb .my
    ret

; pj_rstnext - a unit (an MCU, or a block of a non-interleaved scan) is
; done: at the interval's end, unless it was the scan's last, the RSTm
; that must follow (m = the intervals so far mod 8), and everything
; restarted. Preserves all
pj_rstnext:
    cmp word [JV_RI], 0
    je .out
    push ax
    inc word [JV_RCNT]
    mov ax, [JV_RCNT]
    cmp ax, [JV_RI]
    pop ax
    jb .out
    mov word [JV_RCNT], 0
    push ax
    mov ax, [JV_DONE]
    cmp ax, [JV_UNIT]
    pop ax
    jae .out
    push ax
    xor al, al
    call pj_tomark
    jc .t
    mov ah, [JV_RSTN]
    and ah, 7
    add ah, 0xD0
    cmp al, ah
    jne .d
    inc word [JV_RSTN]
    call pj_binit
    xor ax, ax
    mov [JV_PRED], ax
    mov [JV_PRED + 2], ax
    mov [JV_PRED + 4], ax
    mov [JV_EOBRUN], ax
    pop ax
.out:
    ret
.t:
    jmp pj_trunc
.d:
    jmp pj_data

; pj_cset - AL = a frame component: its multipliers ([JV_MLT]), plane and
; stride ([cs:pj_cpl], [JV_ST]), H and V ([cs:pj_ch], [cs:pj_cv]).
; Preserves all
pj_cset:
    push ax
    push bx
    xor ah, ah
    mov bx, ax
    push cx
    mov cl, 7
    shl ax, cl
    pop cx
    add ax, JS_MULT
    mov [JV_MLT], ax
    shl bx, 1
    mov ax, [JV_FR + JL_PL + bx]
    mov [cs:pj_cpl], ax
    mov ax, [JV_FR + JL_ST + bx]
    mov [JV_ST], ax
    shl bx, 1
    mov al, [JV_FR + JF_C + bx + JC_H]
    mov [cs:pj_ch], al
    mov al, [JV_FR + JF_C + bx + JC_V]
    mov [cs:pj_cv], al
    pop bx
    pop ax
    ret

; pj_bplace - the block (mx H + h, v) of the component pj_cset set: its
; place in the plane, [JV_PL]. Preserves all
pj_bplace:
    push ax
    push dx
    mov al, [cs:pj_v]               ; v N rows down
    mul byte [JV_FR + JL_N]
    mul word [JV_ST]
    add ax, [cs:pj_cpl]
    push ax
    mov al, [cs:pj_ch]              ; (mx H + h) N across
    xor ah, ah
    mul word [JV_MX]
    add al, [cs:pj_h]
    adc ah, 0
    mov dl, [JV_FR + JL_N]
    xor dh, dh
    mul dx
    pop dx
    add ax, dx
    mov [JV_PL], ax
    pop dx
    pop ax
    ret

; =============================================================================
; A BAND: MCU row [JV_MY]'s rows of the stored picture, at the scale, out -
; to the emitter a master row each (orientations 1-4: 3 and 4 bottom-up, 2
; and 3 mirrored), or a block of master COLUMNS to K_COLS (5-8)
; =============================================================================
pj_band:
    mov al, [JV_FR + JF_VMAX]
    mul byte [JV_FR + JL_N]
    mov cx, ax                      ; CX = the band's rows
    mul word [JV_MY]
    cmp ax, [JV_HS]
    jb .in
    ret
.in:
    mov [JV_Y0], ax
    mov dx, [JV_HS]
    sub dx, ax
    cmp cx, dx
    jbe .k
    mov cx, dx
.k:
    mov [JV_K], cx
    mov word [JV_J], 0
.r:
    mov es, [JV_RSEG]               ; where the row is made: the emitter's
    xor di, di                      ; row buffer for 1 and 4, else ours
    mov al, [JV_OR]
    cmp al, 1
    je .cv
    cmp al, 4
    je .cv
    push ds
    pop es
    mov di, [JV_FR + JL_ROW]
.cv:
    mov bx, [JV_J]
    call pj_conv
    push ds
    pop es
    mov al, [JV_OR]
    cmp al, 5
    jae .scat
    cmp al, 2
    je .rev
    cmp al, 3
    jne .em
.rev:
    call pj_rev
.em:
    mov ax, [JV_Y0]
    add ax, [JV_J]                  ; AX = y, the stored row
    push ax
    inc ax
    call pj_prog                    ; DX = the progress
    pop ax
    cmp byte [JV_OR], 3             ; 3, 4: the master's row from the bottom
    jb .top
    neg ax
    add ax, [JV_HS]
    dec ax
.top:
    mov bx, 0xFFFF                  ; (the emitter counts the rows complete)
    call far [JV_SVC + PXS_EMIT * 4]
    jc .ab
    jmp short .nx
.scat:
    call pj_scat
.nx:
    inc word [JV_J]
    mov ax, [JV_J]
    cmp ax, [JV_K]
    jb .r
    cmp byte [JV_OR], 5
    jb .out
    ; --- 5-8: the band is a block of the master's columns ---------------
    mov cx, [JV_Y0]                 ; CX = its first column
    cmp byte [JV_OR], 6
    je .rc
    cmp byte [JV_OR], 7
    jne .c0
.rc:
    mov cx, [JV_HS]
    sub cx, [JV_Y0]
    sub cx, [JV_K]
.c0:
    mov di, 0xFFFF                  ; rows complete: none until the last
    mov ax, [JV_Y0]
    add ax, [JV_K]
    cmp ax, [JV_HS]
    jb .nl
    mov di, [JV_WS]
.nl:
    xor ax, ax                      ; master row 0...
    mov bx, [JV_WS]                 ; ...every one of them
    mov dx, [JV_K]                  ; DX pixels a row
    mov si, [JV_FR + JL_BLK]        ; ES:SI = the block
    call far [JV_SVC + PXS_COLS * 4]
    jc .ab
    mov ax, [JV_Y0]
    add ax, [JV_K]
    call pj_prog
    mov ax, dx
    call far [JV_SVC + PXS_TICK * 4]
    jc .ab
.out:
    ret
.ab:
    jmp pj_dfail

; pj_prog - AX = the stored rows done: DX = the progress in the picture's
; upright rows (SPEC.md 106.18's K_EMIT DX); a progressive picture's output
; pass is the second half. Preserves all but DX
pj_prog:
    push ax
    push cx
    mul word [JV_SH]
    div word [JV_HS]
    cmp byte [JV_PROG], 0
    je .o
    shr ax, 1
    mov cx, [JV_SH]
    shr cx, 1
    add ax, cx
.o:
    mov dx, ax
    pop cx
    pop ax
    ret

; YCC1 - one luma pixel at DS:SI through the clamp bases BP (R), DX (G), CX
; (B) to R, G, B at ES:DI. Clobbers AX, BX
%macro YCC1 0
    lodsb
    mov ah, al
    mov bx, bp
    xlatb
    stosb
    mov al, ah
    mov bx, dx
    xlatb
    stosb
    mov al, ah
    mov bx, cx
    xlatb
    stosb
%endmacro

; pj_conv - BX = the band's row j: the row of pixels at ES:DI - grey copied,
; R G B copied, YCbCr through jdcolor.c's tables with each chroma pixel
; REPLICATED over Hmax luma ones (and Vmax rows). Preserves all
pj_conv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, bx                      ; the Y row
    mul word [JV_FR + JL_ST]
    add ax, [JV_FR + JL_PL]
    mov si, ax
    mov cx, [JV_WS]
    cmp byte [JV_FR + JF_NF], 1
    jne .col
    rep movsb
    jmp .out
.col:
    mov ax, bx                      ; the chroma row: j / Vmax
    cmp byte [JV_FR + JF_VMAX], 2
    jne .cr
    shr ax, 1
.cr:
    mul word [JV_FR + JL_ST + 2]
    add ax, [JV_FR + JL_PL + 2]
    mov [JV_CX], ax
    mov ax, [JV_FR + JL_PL + 4]
    sub ax, [JV_FR + JL_PL + 2]
    mov [JV_CDLT], ax
    mov [JV_X], cx
    test byte [JV_PF], JPF_RGB
    jnz .rgb
.c:
    mov bx, [JV_CX]                 ; a chroma pixel: Cb, Cr
    mov al, [bx]
    add bx, [JV_CDLT]
    mov ah, [bx]
    inc word [JV_CX]
    mov bl, ah                      ; BP = R's clamp base
    xor bh, bh
    shl bx, 1
    mov bp, [JS_RCR + bx]
    add bp, JS_CLAMP + 384
    shl bx, 1
    mov cx, [JS_GCR + bx]
    mov dx, [JS_GCR + bx + 2]
    mov bl, al
    xor bh, bh
    shl bx, 1
    push word [JS_BCB + bx]
    shl bx, 1
    add cx, [JS_GCB + bx]
    adc dx, [JS_GCB + bx + 2]
    add dx, JS_CLAMP + 384          ; DX = G's
    pop cx
    add cx, JS_CLAMP + 384          ; CX = B's
    mov al, [JV_FR + JF_HMAX]
    xor ah, ah
    cmp ax, [JV_X]
    jbe .n
    mov ax, [JV_X]
.n:
    sub [JV_X], ax
    cmp al, 2                       ; the common cases unrolled: two luma
    je .p2                          ; pixels a chroma one (4:2:x), one
    cmp al, 1                       ; (4:4:x)
    je .p1
    mov [JV_HN], al
.p:
    YCC1
    dec byte [JV_HN]
    jnz .p
    jmp short .nc
.p2:
    YCC1
.p1:
    YCC1
.nc:
    cmp word [JV_X], 0
    je .ycdone
    jmp .c
.ycdone:
    jmp .out
.rgb:
    mov bx, [JV_CX]                 ; R G B as they stand: G and B are the
    mov dl, [bx]                    ; "chroma" planes
    add bx, [JV_CDLT]
    mov dh, [bx]
    inc word [JV_CX]
    mov al, [JV_FR + JF_HMAX]
    xor ah, ah
    cmp ax, [JV_X]
    jbe .rn
    mov ax, [JV_X]
.rn:
    sub [JV_X], ax
    mov cx, ax
.rp:
    movsb
    mov al, dl
    stosb
    mov al, dh
    stosb
    loop .rp
    cmp word [JV_X], 0
    jne .rgb
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_nch - AX = bytes a pixel: 1 grey, 3 colour. Preserves all else
pj_nch:
    mov ax, 1
    cmp byte [JV_FR + JF_NF], 1
    je .o
    mov al, 3
.o:
    ret

; pj_rev - the converted row (ours) into the row buffer MIRRORED. Preserves
; all
pj_rev:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [JV_RSEG]
    mov si, [JV_FR + JL_ROW]
    call pj_nch
    mov bx, ax                      ; BX = bytes a pixel
    mov cx, [JV_WS]
    dec cx
    mul cx
    mov di, ax                      ; DI = the last pixel's place
    inc cx
.p:
    push cx
    mov cx, bx
    rep movsb
    pop cx
    sub di, bx
    sub di, bx
    loop .p
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; pj_scat - band row [JV_J], converted (ours), into the transposed block:
; stored pixel x goes to block row r (x for 5 and 6, WS - 1 - x for 7 and
; 8) at place j (j for 5 and 8, k - 1 - j for 6 and 7). Preserves all
pj_scat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call pj_nch
    mov bx, ax                      ; BX = bytes a pixel
    mul word [JV_K]
    mov dx, ax                      ; DX = a block row's bytes
    mov ax, [JV_J]
    mov cl, [JV_OR]
    cmp cl, 5
    je .j
    cmp cl, 8
    je .j
    mov ax, [JV_K]
    dec ax
    sub ax, [JV_J]
.j:
    push dx
    mul bx
    pop dx
    mov di, [JV_FR + JL_BLK]
    add di, ax                      ; DI = row 0's place
    cmp cl, 7
    jb .fw
    push dx                         ; 7, 8: from the last block row back
    mov ax, [JV_WS]
    dec ax
    mul dx
    pop dx
    add di, ax
    neg dx
.fw:
    sub dx, bx                      ; (each copy moves DI on by BX already)
    mov si, [JV_FR + JL_ROW]
    mov cx, [JV_WS]
.p:
    push cx
    mov cx, bx
    rep movsb
    pop cx
    add di, dx
    loop .p
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; PROGRESSIVE (SPEC.md 106.19): every scan into the STORE - a paragraph a
; block at 1/4, the four coefficients zigzag 0, 1, 2, 4 and at 8..15 the
; positions that have ever been nonzero; a word a block at 1/8, the DC -
; and after the EOI the store through the reduced IDCT
; =============================================================================
pj_progscan:
    mov cl, [JV_AL]
    mov ax, 1
    shl ax, cl
    mov [JV_P1], ax
    mov ax, -1
    shl ax, cl
    mov [JV_M1], ax
    xor ax, ax
    mov [JV_EOBRUN], ax
    mov [JV_PRED], ax
    mov [JV_PRED + 2], ax
    mov [JV_PRED + 4], ax
    mov [JV_DONE], ax
    mov [JV_RSTN], ax
    mov [JV_RCNT], ax
    cmp byte [JV_NS], 1
    jne .il
    ; --- one component: its own block grid, its blocks in raster order --
    xor bx, bx
    mov al, [JV_SC + JS_CI]
    call pj_cset
    call pj_grid                    ; JV_BW, JV_BH, JV_UNIT
    mov word [JV_MY], 0
.by:
    mov word [JV_MX], 0
.bx:
    mov al, [cs:pj_ch]              ; bi = by (mcux H) + bx
    xor ah, ah
    mul word [JV_FR + JF_MCUX]
    mul word [JV_MY]
    add ax, [JV_MX]
    mov [JV_BI], ax
    xor bx, bx
    call pj_pblock
    call pj_punit
    inc word [JV_MX]
    mov ax, [JV_MX]
    cmp ax, [JV_BW]
    jb .bx
    inc word [JV_MY]
    mov ax, [JV_MY]
    cmp ax, [JV_BH]
    jb .by
    ret
.il:
    ; --- interleaved (DC scans): MCUs ---------------------------------------
    mov ax, [JV_TOTAL]
    mov [JV_UNIT], ax
    mov word [JV_MY], 0
.my:
    mov word [JV_MX], 0
.mx:
    xor bx, bx
.c:
    mov al, [JV_SC + bx + JS_CI]
    call pj_cset
    mov byte [cs:pj_v], 0
.v:
    mov byte [cs:pj_h], 0
.h:
    mov al, [cs:pj_cv]              ; bi = (my V + v) (mcux H) + mx H + h
    xor ah, ah
    mul word [JV_MY]
    add al, [cs:pj_v]
    adc ah, 0
    push ax
    mov al, [cs:pj_ch]
    xor ah, ah
    mul word [JV_FR + JF_MCUX]
    mov cx, ax
    pop ax
    mul cx
    push ax
    mov al, [cs:pj_ch]
    xor ah, ah
    mul word [JV_MX]
    add al, [cs:pj_h]
    adc ah, 0
    pop cx
    add ax, cx
    mov [JV_BI], ax
    call pj_pblock
    inc byte [cs:pj_h]
    mov al, [cs:pj_h]
    cmp al, [cs:pj_ch]
    jb .h
    inc byte [cs:pj_v]
    mov al, [cs:pj_v]
    cmp al, [cs:pj_cv]
    jb .v
    add bx, 8
    mov al, [JV_NS]
    xor ah, ah
    mov cl, 3
    shl ax, cl
    cmp bx, ax
    jb .c
    call pj_punit
    inc word [JV_MX]
    mov ax, [JV_MX]
    cmp ax, [JV_FR + JF_MCUX]
    jb .mx
    inc word [JV_MY]
    mov ax, [JV_MY]
    cmp ax, [JV_FR + JF_MCUY]
    jb .my
    ret

; pj_punit - a unit of a progressive scan done: the check, the interval,
; and every 64 units the progress (which is also where a cancel is seen).
; Preserves all
pj_punit:
    call pj_chk
    inc word [JV_DONE]
    call pj_rstnext
    test byte [JV_DONE], 63
    jnz .o
    push ax
    push dx
    call pj_scanprog
    mov ax, dx
    call far [JV_SVC + PXS_TICK * 4]
    pop dx
    jc .ab
    pop ax
.o:
    ret
.ab:
    jmp pj_dfail

; pj_scanprog - DX = the progress while the scans are read: the bytes
; handed over, as a share of the file, of the first half. Preserves all
; but DX
pj_scanprog:
    push ax
    push bx
    push cx
    mov ax, [JV_CONS]
    mov dx, [JV_CONS + 2]
    mov bx, [JV_FSZ]
    mov cx, [JV_FSZ + 2]
.s:
    or cx, cx                       ; both halved until the size is a word
    jz .w
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .s
.w:
    or bx, bx
    jz .z
    cmp dx, bx                      ; (what was handed over can pass the
    jae .z                          ; size by a window: the end)
    div bx                          ; AX = the share, of 65,536
    mov cx, [JV_SH]
    shr cx, 1
    mul cx
    jmp short .o
.z:
    mov dx, [JV_SH]
    shr dx, 1
.o:
    pop cx
    pop bx
    pop ax
    ret

; pj_grid - a non-interleaved scan's component (pj_cset's): its block
; grid, ceil(ceil(W H / Hmax) / 8) by ceil(ceil(H V / Vmax) / 8), and the
; units. Preserves all
pj_grid:
    push ax
    push bx
    push cx
    push dx
    mov al, [cs:pj_ch]
    xor ah, ah
    mul word [JV_FR + JF_W]
    mov bl, [JV_FR + JF_HMAX]
    xor bh, bh
    add ax, bx
    dec ax
    adc dx, 0
    div bx
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [JV_BW], ax
    mov al, [cs:pj_cv]
    xor ah, ah
    mul word [JV_FR + JF_H]
    mov bl, [JV_FR + JF_VMAX]
    xor bh, bh
    add ax, bx
    dec ax
    adc dx, 0
    div bx
    add ax, 7
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [JV_BH], ax
    mul word [JV_BW]
    mov [JV_UNIT], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_pblock - scan component BX (x 8), block [JV_BI] of its store: the
; scan's kind of decode on it. The window loaded and banked here
pj_pblock:
    push bx
    push es
    call pj_load
    call pj_room
    mov al, [JV_SC + bx + JS_CI]
    xor ah, ah
    shl ax, 1
    mov di, ax                      ; DI = the component x 2
    mov ax, [JV_SBASE + di]         ; ES:[pj_soff] = the block's store
    mov bp, [JV_BI]
    cmp byte [JV_SCL], 3
    je .s8
    add ax, bp
    mov word [cs:pj_soff], 0
    jmp short .s
.s8:
    push bp
    and bp, 7
    shl bp, 1
    mov [cs:pj_soff], bp
    pop bp
    push cx
    mov cl, 3
    shr bp, cl
    pop cx
    add ax, bp
.s:
    mov es, ax
    cmp byte [JV_SS], 0
    jne .ac
    ; --- DC ------------------------------------------------------------------
    mov bp, [cs:pj_soff]
    cmp byte [JV_AH], 0
    jne .dcr
    mov ax, [JV_SC + bx + JS_DCS]
    mov [JV_SLOWP], ax
    mov bh, [JV_SC + bx + JS_DCP]
    call pj_dcdiff
    add [JV_PRED + di], ax
    mov ax, [JV_PRED + di]
    push cx
    mov cl, [JV_AL]
    shl ax, cl
    pop cx
    mov [es:bp], ax
    jmp short .out
.dcr:
    call pj_bit
    jnc .out
    mov ax, [JV_P1]
    or [es:bp], ax
    jmp short .out
.ac:
    mov ax, [JV_SC + bx + JS_ACS]
    mov [JV_SLOWP], ax
    mov bh, [JV_SC + bx + JS_ACP]
    cmp byte [JV_AH], 0
    jne .acr
    call pj_acfirst
    jmp short .out
.acr:
    call pj_acref
.out:
    call pj_bank
    pop es
    pop bx
    ret

; pj_bit - one bit, into CF. DX/CH/SI live; clobbers AX, CL
pj_bit:
    or ch, ch
    jnz .b
    FILL
.b:
    shl dx, 1
    dec ch
    ret

; pj_sym - BH = an AC table's page: one symbol by the fast table, the way
; pj_acs reads it. out AL = the run r; AH = s, or 0FFh when the entry was
; WHOLE (its value then in BP, r in AL); for s = 0 AL is the symbol's r.
; DX/CH/SI live; clobbers BL, CL
pj_sym:
    cmp ch, 8
    jae .p
    FILL
.p:
    mov bl, dh
    mov cl, [bx + JA_LEN]
    or cl, cl
    jz .slow
    shl dx, cl
    sub ch, cl
    mov al, [bx + JA_ADV]
    or al, al
    jz .code
    shr al, 1
    dec al                          ; r
    mov cl, [bx + JA_VLO]
    mov ah, [bx + JA_VHI]
    push ax
    mov al, cl
    mov bp, ax
    pop ax
    mov ah, 0xFF
    ret
.slow:
    call pj_hslow
    jmp short .rs
.code:
    mov al, [bx + JA_RS]
.rs:
    mov ah, al
    and ah, 15                      ; s
    mov cl, 4
    shr al, cl                      ; r
    ret

; pj_nzbit - AL = k: BL... ES:[8 + (k >> 3)] and CL = the bit's mask in
; AH. out ZF = 0 when k has been nonzero. Preserves all but AH, CL
pj_nzt:
    push bx
    mov bl, al
    mov cl, al
    and cl, 7
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov ah, 1
    shl ah, cl
    test [es:bx + 8], ah
    pop bx
    ret
pj_nzs:
    push bx
    mov bl, al
    mov cl, al
    and cl, 7
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov ah, 1
    shl ah, cl
    or [es:bx + 8], ah
    pop bx
    ret

; pj_kput - AL = k, BP = a value: into the store when k is one of the four
; it keeps. Preserves all
pj_kput:
    push bx
    mov bl, al
    xor bh, bh
    mov bl, [cs:pj_kidx + bx]
    cmp bl, 0xFF
    je .o
    shl bl, 1
    mov [es:bx], bp
.o:
    pop bx
    ret

; pj_kref - AL = k, an old nonzero coefficient's correction bit was 1: its
; value, when kept, made one step larger in magnitude where that bit is
; still clear (jdphuff.c). Preserves all
pj_kref:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    mov bl, [cs:pj_kidx + bx]
    cmp bl, 0xFF
    je .o
    shl bl, 1
    mov ax, [es:bx]
    test ax, [JV_P1]
    jnz .o
    or ax, ax
    js .neg
    add ax, [JV_P1]
    jmp short .st
.neg:
    add ax, [JV_M1]
.st:
    mov [es:bx], ax
.o:
    pop bx
    pop ax
    ret

; pj_acfirst - an AC first scan's block: EOB runs, Ss..Se, values << Al
pj_acfirst:
    cmp word [JV_EOBRUN], 0
    je .go
    dec word [JV_EOBRUN]
    ret
.go:
    mov al, [JV_SS]
    mov [cs:pj_k], al
.l:
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .done
    call pj_sym
    cmp ah, 0xFF
    je .whole
    or ah, ah
    jz .z
    add [cs:pj_k], al               ; k += r: past Se is damaged, before
    mov cl, [cs:pj_k]               ; the magnitude is read
    cmp cl, [JV_SE]
    ja .bad
    mov al, ah
    push bx
    mov bl, al
    call pj_getb
    call pj_ext
    pop bx
    mov bp, ax
    jmp short .put
.whole:
    add [cs:pj_k], al
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .badgb
.put:
    mov ax, bp
    mov cl, [JV_AL]
    shl ax, cl
    mov bp, ax
    mov al, [cs:pj_k]
    call pj_kput
    call pj_nzs
    inc byte [cs:pj_k]
    jmp short .l
.z:
    cmp al, 15                      ; ZRL
    jne .eob
    add byte [cs:pj_k], 16
    jmp short .l
.eob:
    mov cl, al                      ; EOBRUN = 2^r + r bits - 1
    mov bp, 1
    shl bp, cl
    push bx
    call pj_getb
    pop bx
    add ax, bp
    dec ax
    mov [JV_EOBRUN], ax
.done:
    ret
.badgb:
    jmp pj_badgb
.bad:
    jmp pj_bad

; pj_acref - an AC refinement scan's block (jdphuff.c's
; decode_mcu_AC_refine), on the nonzero history
pj_acref:
    mov al, [JV_SS]
    mov [cs:pj_k], al
    cmp word [JV_EOBRUN], 0
    jne .eob
.l:
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .done
    call pj_sym
    mov word [cs:pj_nv], 0
    cmp ah, 0xFF
    je .whole
    or ah, ah
    jz .z
    cmp ah, 1                       ; a new coefficient's size is always 1
    jne .bad
    mov [cs:pj_r], al
    push ax
    call pj_bit
    pop ax
    mov bp, [JV_P1]
    jc .nv
    mov bp, [JV_M1]
    jmp short .nv
.whole:
    mov [cs:pj_r], al
    cmp bp, 1                       ; WHOLE: s was 1 when the value is +-1
    je .wp
    cmp bp, -1
    jne .badgb
    mov bp, [JV_M1]
    jmp short .nv
.wp:
    mov bp, [JV_P1]
.nv:
    mov [cs:pj_nv], bp
    jmp short .adv
.z:
    mov [cs:pj_r], al
    cmp al, 15
    je .adv                         ; ZRL: sixteen zeros, no new one
    mov cl, al                      ; EOBRUN = 2^r + r bits; the rest of
    mov bp, 1                       ; the block is the EOB's
    shl bp, cl
    push bx
    call pj_getb
    pop bx
    add ax, bp
    mov [JV_EOBRUN], ax
    jmp short .eob
.adv:
    ; past the nonzero ones (their correction bits) and r zero ones
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .place
    call pj_nzt
    jz .zero
    call pj_bit
    jnc .nx
    mov al, [cs:pj_k]
    call pj_kref
    jmp short .nx
.zero:
    cmp byte [cs:pj_r], 0
    je .place
    dec byte [cs:pj_r]
.nx:
    inc byte [cs:pj_k]
    jmp short .adv
.place:
    mov bp, [cs:pj_nv]
    or bp, bp
    jz .pn
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .bad
    call pj_kput
    call pj_nzs
.pn:
    inc byte [cs:pj_k]
    jmp .l
.eob:
    ; the EOB run's block: correction bits for every nonzero one left
    mov al, [cs:pj_k]
    cmp al, [JV_SE]
    ja .ed
    call pj_nzt
    jz .en
    call pj_bit
    jnc .en
    mov al, [cs:pj_k]
    call pj_kref
.en:
    inc byte [cs:pj_k]
    jmp short .eob
.ed:
    dec word [JV_EOBRUN]
.done:
    ret
.badgb:
    jmp pj_badgb
.bad:
    jmp pj_bad

; =============================================================================
; pj_output - after a progressive picture's EOI: the store, MCU row by MCU
; row, through the reduced IDCT into the planes, and each row out
; =============================================================================
pj_output:
    mov word [JV_MY], 0
.my:
    xor ax, ax                      ; AX = the frame's component
.c:
    mov [JV_COMP], ax
    call pj_cset
    mov byte [cs:pj_v], 0
.v:
    mov word [JV_MX], 0             ; JV_MX here = the block across, bx
.b:
    mov al, [cs:pj_cv]              ; bi = (my V + v) (mcux H) + bx
    xor ah, ah
    mul word [JV_MY]
    add al, [cs:pj_v]
    adc ah, 0
    push ax
    mov al, [cs:pj_ch]
    xor ah, ah
    mul word [JV_FR + JF_MCUX]
    mov [cs:pj_bpl], ax
    pop cx
    mul cx
    add ax, [JV_MX]
    mov bp, ax                      ; BP = bi
    mov bx, [JV_COMP]
    shl bx, 1
    mov ax, [JV_SBASE + bx]
    cmp byte [JV_SCL], 3
    je .o8
    add ax, bp
    mov es, ax
    mov ax, [es:0]
    mov [JS_BLKZ + 0], ax
    mov ax, [es:2]
    mov [JS_BLKZ + 2], ax
    mov ax, [es:4]
    mov [JS_BLKZ + 4], ax
    mov ax, [es:6]
    mov [JS_BLKZ + 8], ax
    mov word [JV_KMAX], JS_BLKZ + 10
    jmp short .put
.o8:
    mov dx, bp
    mov cl, 3
    shr dx, cl
    add ax, dx
    mov es, ax
    and bp, 7
    shl bp, 1
    mov ax, [es:bp]
    mov [JS_BLKZ], ax
    mov word [JV_KMAX], JS_BLKZ + 2
.put:
    push ds
    pop es
    mov al, [cs:pj_ch]              ; the block's place: bx N across, v N
    mov [cs:pj_h], al               ; down (pj_bplace's with mx 0, h = bx)
    mov ax, [JV_MX]
    mul byte [JV_FR + JL_N]
    push ax
    mov al, [cs:pj_v]
    mul byte [JV_FR + JL_N]
    mul word [JV_ST]
    pop dx
    add ax, dx
    add ax, [cs:pj_cpl]
    mov [JV_PL], ax
    call pj_bput
    inc word [JV_MX]
    mov ax, [JV_MX]
    cmp ax, [cs:pj_bpl]
    jb .b
    inc byte [cs:pj_v]
    mov al, [cs:pj_v]
    cmp al, [cs:pj_cv]
    jb .v
    mov ax, [JV_COMP]
    inc ax
    cmp al, [JV_FR + JF_NF]
    jb .c
    call pj_band
    inc word [JV_MY]
    mov ax, [JV_MY]
    cmp ax, [JV_FR + JF_MCUY]
    jb .my
    ret

; =============================================================================
; THIS PART'S OWN MEMORY AND CONSTANTS (rule 1: through CS)
; =============================================================================
pj_zz:      db 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5
            db 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28
            db 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51
            db 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
pj_keep:    db 0x00, 0x24, 0x36, 0x3F   ; a natural index i is kept: i & K = 0
pj_kidx:    db 0, 1, 2, 0xFF, 3         ; zigzag k -> the store's word, 1/4
            times 59 db 0xFF
pj_idtab:   dw pj_id8, pj_id4, pj_id2, pj_id1
pj_ptab:    dw pj_p0, pj_p1, pj_p2, pj_p3
; the prescales (SPEC.md 106.19), 2^17 p(u) p(v), v-major
pj_p0:      dw 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520
            dw 22725, 31521, 29692, 26722, 22725, 17855, 12299, 6270
            dw 21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906
            dw 19266, 26722, 25172, 22654, 19266, 15137, 10426, 5315
            dw 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520
            dw 12873, 17855, 16819, 15137, 12873, 10114, 6967, 3552
            dw 8867, 12299, 11585, 10426, 8867, 6967, 4799, 2446
            dw 4520, 6270, 5906, 5315, 4520, 3552, 2446, 1247
pj_p1:      dw 16384, 20995, 15137, 17799, 20995, 26905, 19397, 22809
            dw 15137, 19397, 13985, 16444, 17799, 22809, 16444, 19336
pj_p2:      dw 16384, 14846, 14846, 13452
pj_p3:      dw 16384

pj_hsp:     dw 0                    ; HEAD: the stack at entry
pj_hlen:    dw 0                    ; HEAD: the head's bytes
pj_fprog:   db 0                    ; HEAD: the frame is progressive
pj_mm:      db 0                    ; HEAD: EXIF is big-endian
pj_dscl:    db 0                    ; HEAD: the scale being priced
pj_fr:      times JF_SZ db 0        ; HEAD: the frame
pj_dsp:     dw 0                    ; DECODE: the stack at pj_body's call
pj_dseg:    dw 0                    ; DECODE: the scratch
pj_mk:      db 0                    ; the marker being read
pj_qp:      db 0                    ; DQT: the precision
pj_tc:      db 0                    ; DHT: the class...
pj_th:      db 0                    ; ...and number
pj_tpg:     dw 0                    ; ...its fast table
pj_hi:      db 0                    ; ...the entry being filled
pj_skr:     db 0                    ; pj_tomark: pass an RSTn over
pj_cpl:     dw 0                    ; pj_cset: the component's plane
pj_ch:      db 0                    ; ...its H...
pj_cv:      db 0                    ; ...and V
pj_h:       db 0                    ; the block within the MCU
pj_v:       db 0
pj_soff:    dw 0                    ; the block's offset in its store
pj_k:       db 0                    ; a progressive scan's k
pj_r:       db 0                    ; ...its zero run
pj_nv:      dw 0                    ; ...its new coefficient
pj_bpl:     dw 0                    ; the output pass's blocks a row
pj_vend:    dw 0                    ; the scratch's variable part's end
pj_gb:      db 0                    ; bits a refusal gives back

%include "pxplan.inc"                ; the plans: shared source (106.20)
