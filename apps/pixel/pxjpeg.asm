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
JP_FW       equ PXK_PRIV + 3        ; word: the frame HEAD read - its width,
JP_FH       equ PXK_PRIV + 5        ; word: height,
JP_FNF      equ PXK_PRIV + 7        ; byte: components,
JP_FPROG    equ PXK_PRIV + 8        ; byte: progressive,
JP_FHV      equ PXK_PRIV + 9        ; 3 bytes: each one's H << 4 | V - which
                                    ; DECODE's frame must be (the scratch is
                                    ; sized by it: wave-4 review F1)
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
    mov ax, [cs:pj_fr + JF_W]       ; the frame, banked for DECODE
    mov [di + JP_FW], ax
    mov ax, [cs:pj_fr + JF_H]
    mov [di + JP_FH], ax
    mov al, [cs:pj_fr + JF_NF]
    mov [di + JP_FNF], al
    mov al, [cs:pj_fprog]
    mov [di + JP_FPROG], al
    push bx
    push si
    xor bx, bx
    xor si, si
    mov ch, [cs:pj_fr + JF_NF]      ; (its components only)
.hv:
    mov al, [cs:pj_fr + JF_C + bx + JC_H]
    mov cl, 4
    shl al, cl
    or al, [cs:pj_fr + JF_C + bx + JC_V]
    push di
    add di, si
    mov [di + JP_FHV], al
    pop di
    inc si
    add bx, JC_SZ
    dec ch
    jnz .hv
    pop si
    pop bx
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
JS_D        equ 0x0840              ; 64 words: dequantised, natural order
JS_WS       equ 0x08C0              ; 64 words: the IDCT's columns
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
JA_ADV      equ 768                 ; (r + 1) x 2 when the value is made,
                                    ; else JA_SPEC: the code alone, EOB, ZRL
                                    ; or a code longer than eight bits
JA_RS       equ 1024                ; the symbol
JA_SPEC     equ 0xC0                ; (past any k x 2 a whole entry makes)
JS_CLEAN    equ 0x3100              ; the de-stuffed data...
JS_CLEANSZ  equ 2048
JS_GUARD    equ 320                 ; ...and zeros past its end
JS_COL      equ 0x3A40              ; the colour tables, words by Cb or Cr,
JS_RCR      equ JS_COL              ; each R, G or B one a clamp table's
JS_BCB      equ JS_COL + 512        ; place (JS_CLAMP + 384 + the delta):
JS_GBL      equ JS_COL + 1024       ; Cr->R, Cb->B; and G's two 32-bit
JS_GBH      equ JS_COL + 1536       ; terms by halves, Cb's low and high
JS_GRL      equ JS_COL + 2048       ; words, Cr's low and high (the place
JS_GRH      equ JS_COL + 2560       ; on the high), added per chroma pixel
JS_CLAMP    equ 0x4640              ; 1,024: clamp(i - 384), the colour's
JS_KNW      equ 0x4A40              ; 64 words: zigzag k -> natural x 2
                                    ; when the scale keeps it, else FFFFh
JS_VAR      equ 0x4B00              ; the multiply pages at 1/1 and 1/2,
                                    ; (pj_vartab), then the row, the planes,
                                    ; the block
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
    mov ax, [cs:pj_fr + JF_W]       ; 0 wide or high at the scale: not
    shr ax, cl                      ; usable (wave-4 review F3)
    or ax, ax                       ; (a shift by 0 sets no flag)
    jz .no0
    mov ax, [cs:pj_fr + JF_H]
    shr ax, cl
    or ax, ax
    jz .no0
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
.no0:
    mov ax, 0xFFFF
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
    mov si, cx                      ; (CL = the scale)
    and si, 3
    shl si, 1
    mov si, [cs:pj_vartab + si]     ; past the scale's multiply pages
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
JV_DONEH    equ 0x42                ; word: JV_DONE's high word...
JV_UNITH    equ 0x44                ; ...JV_UNIT's (wave-4 review F5: a
                                    ; picture of 65,536 MCUs or more)
JV_CPOS     equ 0x46                ; word: the bit position's byte
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
JV_DONE     equ 0x68                ; word: MCUs (units) done in the scan
JV_TOTALH   equ 0x66                ; word: JV_TOTAL's high word
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
JV_BIH      equ 0x84                ; word: ...its high word (review F6: a
                                    ; component of 65,536 blocks at 1/8)
JV_RCNT     equ 0xA0                ; word: units in this restart interval
JV_J        equ 0xA2                ; word: the band's row
JV_KCAP     equ 0xA4                ; word: the last zigzag k the scale keeps
JV_BPE      equ 0xA6                ; word: the MCU program's end (pj_bprog)
JV_BPI      equ 0xA8                ; word: its block being decoded
JV_BOFF     equ 0xAA                ; byte: the bit position's bits taken
                                    ; of JV_CPOS's byte (a baseline scan)
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
    mov byte [cs:pj_gb], 0          ; (a refusal's give-back: this decode's)
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
    call pj_mtabs
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
    mov al, [cs:pj_mk]
    call pj_samef                   ; the frame HEAD sized the scratch by
    jnc .fsame
    jmp pj_data
.fsame:
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

; pj_samef - AL = DECODE's frame's marker, its fields at JV_FR: CF = 1
; unless it is the frame HEAD read and banked (JP_F*) - a file changed
; between the two reads would lay the planes out past the scratch the
; resident claimed by HEAD's (wave-4 review F1). Preserves all but AX
pj_samef:
    push bx
    push cx
    push si
    push es
    mov es, [JV_PKG]
    mov si, [JV_CTX]
    xor ah, ah                      ; the kind: progressive or not
    cmp al, 0xC2
    jne .k
    inc ah
.k:
    cmp ah, [es:si + JP_FPROG]
    jne .no
    mov ax, [JV_FR + JF_W]
    cmp ax, [es:si + JP_FW]
    jne .no
    mov ax, [JV_FR + JF_H]
    cmp ax, [es:si + JP_FH]
    jne .no
    mov al, [JV_FR + JF_NF]
    cmp al, [es:si + JP_FNF]
    jne .no
    xor bx, bx
    mov ch, [JV_FR + JF_NF]         ; (its components only)
.hv:
    mov al, [JV_FR + JF_C + bx + JC_H]
    mov cl, 4
    shl al, cl
    or al, [JV_FR + JF_C + bx + JC_V]
    cmp al, [es:si + JP_FHV]
    jne .no
    inc si
    add bx, JC_SZ
    dec ch
    jnz .hv
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

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
    mov [JV_TOTALH], dx
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
    mov di, ax                      ; pages: an entry no short code fills
    mov cx, 128                     ; is LEN 0 and ADV JA_SPEC (pj_blk)
    xor ax, ax
    rep stosw
    mov cx, 128
    mov ax, JA_SPEC * 0x0101
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
    mov byte [bx + JA_ADV], JA_SPEC
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
    mov di, bx
    shl di, 1
    cbw                             ; (FFh -> FFFFh, a place -> itself)
    mov [JS_KNW + di], ax
    push bx                         ; (AH again: the scale's mask)
    mov bl, [JV_SCL]
    xor bh, bh
    mov ah, [cs:pj_keep + bx]
    pop bx
    inc bx
    cmp bx, 64
    jb .k
    mov di, JS_D                    ; D: zero, kept so block to block
    xor ax, ax
    mov cx, 64
    rep stosw
    pop es
    ret

; pj_ctabs - the colour tables, jdcolor.c's exactly (SPEC.md 106.19), for x
; = i - 128: Cr->R = x + hi(26,345 x + 32,768), Cb->B = 2x + hi(-14,942 x +
; 32,768), each as a clamp table's place; Cb->G = -22,554 x and Cr->G =
; 18,734 x - 65,536 x + 32,768 whole, by halves - G's delta is their sum's
; high word, the low words' carry in it (pj_conv). Preserves all
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
    add dx, JS_CLAMP + 384
    mov [JS_RCR + si], dx
    mov ax, -14942
    imul bx
    add ax, 0x8000
    adc dx, 0
    add dx, bx
    add dx, bx
    add dx, JS_CLAMP + 384
    mov [JS_BCB + si], dx
    mov ax, -22554
    imul bx
    mov [JS_GBL + si], ax
    mov [JS_GBH + si], dx
    mov ax, 18734
    imul bx
    sub dx, bx
    add ax, 0x8000
    adc dx, 0
    add dx, JS_CLAMP + 384
    mov [JS_GRL + si], ax
    mov [JS_GRH + si], dx
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
    mov byte [JV_SCANS], 1          ; (any: 256 scans must not wrap it)
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
; then read MSB first by a BIT POSITION (SI the byte, CL its bits taken; see
; pj_blk), banked between blocks in JV_CPOS and JV_BOFF
; =============================================================================

; pj_binit - a scan's data starts: the position at an empty buffer.
; Preserves all
pj_binit:
    mov byte [JV_BOFF], 0
    mov word [JV_CPOS], JS_CLEAN
    mov word [JV_CEND], JS_CLEAN
    mov byte [JV_ENDED], 0
    mov byte [JV_MARK], 0
    mov byte [JV_OVER], 0
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
    cmp ax, 8                       ; eight bytes of zeros taken: kept
    jb .out                         ; inside the guard, remembered
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

; =============================================================================
; A BASELINE BLOCK (SPEC.md 106.19, 106.22): the DC, then the ACs - each
; kept coefficient DEQUANTISED as it is read, straight into D at its natural
; place, the rest only decoded - then the IDCT at the scale.
;
; THE READER IS A BIT POSITION, not a window: SI the clean buffer's byte
; holding the next bit, CL how many of its bits are taken (0..7). The next
; eight bits are AH after `mov ax, [si]` / `xchg al, ah` / `shl ax, cl` -
; nine or more of the sixteen are real - so nothing is ever refilled: the
; de-stuffed buffer has 300 bytes ahead of a block (pj_room) or the zeros
; past the segment's end. Taking n bits is `add cl, n`, and a byte on when
; CL reaches 8. A position between blocks is banked in JV_CPOS / JV_BOFF.
;
; A symbol's fast entry is read by BH = its AC table's LEN page: LEN (the
; bits it takes: code and magnitude when WHOLE, the code alone otherwise,
; 0 for a code longer than eight bits) and, a page up, ADV (k's step x 2
; when WHOLE, else JA_SPEC). A WHOLE entry is ONE test: k x 2 past the
; loop's end is everything else - a coefficient past the kept range, past
; 63 (`damaged`), or JA_SPEC (pj_acsp: EOB, ZRL, the code alone, the
; canonical walk). A loop per scale, so its end is an immediate:
;
;   pj_sk    every k: decode only - 1/8's whole AC, and the rest of a block
;            past the scale's last kept k. DH = 0 for `add bp, dx`
;   pj_vaN   up to the scale's last kept k: a kept coefficient x its
;            multiplier (`imul`) into D, its columns into JV_MASK
; =============================================================================
JB_PRED     equ 0                   ; word: its component's prediction
JB_DCP      equ 2                   ; byte: its DC table's symbol page
JB_ACP      equ 3                   ; byte: its AC table's LEN page
JB_DCS      equ 4                   ; word: the DC table's slow tables
JB_ACS      equ 6                   ; word: the AC table's
JB_MLT      equ 8                   ; word: the component's multipliers
JB_OFF      equ 10                  ; word: its place in its plane at mx 0
JB_STEP     equ 12                  ; word: its plane's bytes an MCU across
JB_CUR      equ 14                  ; word: its place this MCU
JB_ST       equ 16                  ; word: its plane's stride
JB_SZ       equ 18
JS_BPROG    equ 0x0140              ; 10 x JB_SZ: an MCU's blocks, in order
JB_MAX      equ 10

; PEEK8 - AH = the next eight bits at SI/CL (AL clobbered)
%macro PEEK8 0
    mov ax, [si]
    xchg al, ah
    shl ax, cl
%endmacro

; NORM - CL past 7: a byte on. (CL <= 15)
%macro NORM 0
    cmp cl, 8
    jb %%n
    inc si
    sub cl, 8
%%n:
%endmacro

; pj_bprog - the scan's MCU as a program of blocks: for each scan
; component in order, its V x H blocks. Preserves all
pj_bprog:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, JS_BPROG
    xor bx, bx                      ; BX = the scan's component, x 8
.c:
    mov al, [JV_SC + bx + JS_CI]
    call pj_cset                    ; pj_cpl, JV_ST, pj_ch, pj_cv, JV_MLT
    mov byte [cs:pj_v], 0
.v:
    mov byte [cs:pj_h], 0
.h:
    mov al, [JV_SC + bx + JS_CI]
    xor ah, ah
    shl ax, 1
    add ax, JV_PRED
    mov [di + JB_PRED], ax
    mov al, [JV_SC + bx + JS_DCP]
    mov [di + JB_DCP], al
    mov al, [JV_SC + bx + JS_ACP]
    add al, JA_LEN >> 8
    mov [di + JB_ACP], al
    mov ax, [JV_SC + bx + JS_DCS]
    mov [di + JB_DCS], ax
    mov ax, [JV_SC + bx + JS_ACS]
    mov [di + JB_ACS], ax
    mov ax, [JV_MLT]
    mov [di + JB_MLT], ax
    mov ax, [JV_ST]
    mov [di + JB_ST], ax
    mov al, [cs:pj_v]               ; v N rows down...
    mul byte [JV_FR + JL_N]
    mul word [JV_ST]
    add ax, [cs:pj_cpl]
    mov cx, ax
    mov al, [cs:pj_h]               ; ...h N across
    mul byte [JV_FR + JL_N]
    add ax, cx
    mov [di + JB_OFF], ax
    mov al, [cs:pj_ch]              ; H N an MCU
    mul byte [JV_FR + JL_N]
    mov [di + JB_STEP], ax
    add di, JB_SZ
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
    mov [JV_BPE], di
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_brow - an MCU row starts: every block's place back to MCU column 0.
; Preserves all
pj_brow:
    push ax
    push bx
    mov bx, JS_BPROG
.b:
    mov ax, [bx + JB_OFF]
    mov [bx + JB_CUR], ax
    add bx, JB_SZ
    cmp bx, [JV_BPE]
    jb .b
    pop bx
    pop ax
    ret

; pj_peek16 - AX = the next sixteen bits at SI/CL, three bytes read.
; Preserves all but AX
pj_peek16:
    push cx
    push dx
    mov ax, [si]
    xchg al, ah
    shl ax, cl
    mov dl, [si + 2]
    mov ch, cl
    mov cl, 8
    sub cl, ch
    shr dl, cl
    or al, dl
    pop dx
    pop cx
    ret

; pj_bits - AL = s, 1..15: AX = the next s bits, extended (Annex F's
; EXTEND), taken. SI/CL live (CL normalised). Preserves BX, DX, DI, BP;
; clobbers CH
pj_bits:
    push bx
    mov bl, al
    xor bh, bh                      ; BX = s
    mov ax, [si]
    xchg al, ah
    shl ax, cl                      ; 16 - CL real bits, nine or more
    add cl, bl
    mov ch, cl                      ; CH = the position's bits after it
    cmp cl, 16
    ja .x3
.x:
    mov cl, 16
    sub cl, bl
    shr ax, cl                      ; the s bits
    shl bx, 1
    test ax, [bx + JS_HALF]
    jnz .p
    sub ax, [bx + JS_EXT]
.p:
    mov bl, ch                      ; taken: CH >> 3 bytes on
    mov cl, ch
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    add si, bx
    and cl, 7
    pop bx
    ret
.x3:
    push dx                         ; more than the word held: the third
    mov dl, [si + 2]                ; byte's top bits into AX's zeros
    mov dh, ch
    sub dh, bl                      ; (DH = the CL before)
    mov cl, 8
    sub cl, dh
    shr dl, cl
    or al, dl
    pop dx
    jmp short .x

; pj_walk9 - the canonical walk (Annex C) of [JV_SLOWP]'s table from the
; ninth bit, when the next eight begin no code of eight bits or fewer: AL
; = the symbol, its bits taken. A code no table holds is refused after
; sixteen bits, as the reference refuses it. SI/CL live; preserves BX, DX,
; DI, BP
pj_walk9:
    push bx
    push dx
    push di
    push bp
    call pj_peek16
    mov dx, ax                      ; DX = the sixteen bits
    mov bp, [JV_SLOWP]
    mov bl, dh
    xor bh, bh                      ; BX = the code, eight bits
    mov dh, dl
    mov di, 18                      ; DI = the length x 2, 9 first
.l:
    shl dh, 1
    rcl bx, 1
    cmp bx, [ds:bp + di + JH_MAXC]
    jb .hit
    add di, 2
    cmp di, 34
    jb .l
    add si, 2                       ; sixteen bits taken, and refused
    jmp pj_badb
.hit:
    add bx, [ds:bp + di + JH_OFF]
    add bx, bp
    mov al, [bx + JH_VALS]
    shr di, 1                       ; the code's length, taken
    add cx, di                      ; (CH is not live here: CL <= 7 + 16)
    mov ch, cl
    shr ch, 1
    shr ch, 1
    shr ch, 1
    mov bl, ch
    xor bh, bh
    add si, bx
    and cl, 7
    pop bp
    pop di
    pop dx
    pop bx
    ret

; pj_blk - one block of the MCU program, entry BX: decoded, dequantised and
; through the IDCT into its plane, its place moved on an MCU. The position
; loaded and banked here. Clobbers all but DS, ES
pj_blk:
    mov bp, bx                      ; BP = the entry, until the AC
    mov ax, [ds:bp + JB_CUR]
    mov [JV_PL], ax
    add ax, [ds:bp + JB_STEP]
    mov [ds:bp + JB_CUR], ax
    mov ax, [ds:bp + JB_ST]
    mov [JV_ST], ax
    mov ax, [ds:bp + JB_MLT]
    cmp ax, [JV_MLT]
    je .m
    mov [JV_MLT], ax                ; the value loops' multipliers: their
    mov [cs:pj_va0.imul + 2], ax    ; `imul`s' displacements, PATCHED - a
    mov [cs:pj_va1.imul + 2], ax    ; call and a jump before any of them is
    mov [cs:pj_va2.imul + 2], ax    ; fetched (px_quant's precedent)
.m:
    mov ax, [ds:bp + JB_DCS]
    mov [JV_SLOWP], ax
    mov si, [JV_CPOS]               ; the position
    mov cl, [JV_BOFF]
    mov ax, [JV_CEND]               ; the clean data ahead (pj_room)
    sub ax, si
    jl .room
    cmp ax, JP_AHEAD
    jge .dc
.room:
    call pj_room
.dc:
    PEEK8                           ; --- the DC ---------------------------
    mov bh, [ds:bp + JB_DCP]
    mov bl, ah
    mov ch, [bx + 256]              ; its length, 0 longer than eight bits
    or ch, ch
    jz .dsl
    add cl, ch
    NORM
    mov al, [bx]                    ; AL = s
.dg:
    or al, al
    jz .d0
    call pj_bits                    ; AX = the difference
    jmp short .dd
.dsl:
    call pj_walk9
    jmp short .dg
.d0:
    xor ax, ax
.dd:
    mov di, [ds:bp + JB_PRED]
    add ax, [di]
    mov [di], ax                    ; the prediction
    mov di, [JV_MLT]
    imul word [di]
    add ax, JP_BIAS
    mov [JS_D], ax
    mov word [JV_MASK], 0x0001
    mov ax, [ds:bp + JB_ACS]        ; --- the ACs ------------------------
    mov [JV_SLOWP], ax
    mov bh, [ds:bp + JB_ACP]
    mov bp, 2 - 128                 ; BP = k x 2 - 128
    xor dx, dx
    mov al, [JV_SCL]
    cmp al, 1
    jb .v0
    je .v1
    cmp al, 2
    je .v2
    jmp pj_sk                       ; (1/8: no AC kept)
.v0:
    jmp pj_va0
.v1:
    jmp pj_va1
.v2:
    jmp pj_va2

; pj_bend - the block's ACs are read (the loops jump here): the position
; banked, the IDCT at the scale into the plane, D's written columns zeroed
pj_bend:
    mov [JV_CPOS], si
    mov [JV_BOFF], cl
    mov bl, [JV_SCL]
    xor bh, bh
    shl bx, 1
    call [cs:pj_idtab + bx]
    mov al, [JV_MASK]               ; D's columns that held anything, the
    xor dx, dx                      ; scale's rows of each
    mov bl, [JV_SCL]
    cmp bl, 1
    jb .c8
    je .c4
    cmp bl, 2
    jne .o
    mov [JS_D], dx                  ; 1/4: the 2x2
    mov [JS_D + 2], dx
    mov [JS_D + 16], dx
    mov [JS_D + 18], dx
.o:
    ret
.c4:
%assign u 0
%rep 4
    test al, 1 << u
    jz .n4%+u
%assign v 0
%rep 4
    mov [JS_D + 16 * v + 2 * u], dx
%assign v v + 1
%endrep
.n4%+u:
%assign u u + 1
%endrep
    ret
.c8:
    mov bx, JS_D
.c:
    shr al, 1
    jnc .n
%assign v 0
%rep 8
    mov [bx + 16 * v], dx
%assign v v + 1
%endrep
.n:
    inc bx
    inc bx
    or al, al
    jnz .c
    ret

; pj_acsp - the special entry: BP = k x 2 - 128 + JA_SPEC, BX = the entry
; (its LEN 0: a code longer than eight bits, the canonical walk). out CF =
; 1 the block is done (EOB, or a zero run to its end); else BP = k x 2 -
; 128 past the symbol and AX = its coefficient, 0 for ZRL. A coefficient
; past 63 is `damaged`, before its magnitude is read. SI/CL live (taken);
; preserves BX, DX
pj_acsp:
    push bx
    sub bp, JA_SPEC
    cmp byte [bx], 0
    jne .code
    call pj_walk9                   ; AL = the symbol
    jmp short .rs
.code:
    mov al, [bx + JA_RS - JA_LEN]
.rs:
    mov bl, al
    and bl, 15                      ; BL = s
    jz .zr
    shr al, 1
    shr al, 1
    shr al, 1
    and al, 0x1E                    ; r x 2
    xor ah, ah
    add bp, ax
    cmp bp, -2
    jg .bad
    mov al, bl
    call pj_bits                    ; AX = the coefficient
    inc bp
    inc bp
    pop bx
    clc
    ret
.zr:
    cmp al, 0xF0
    jne .eob
    add bp, 32                      ; ZRL: sixteen zeros
    xor ax, ax
    pop bx
    or bp, bp
    js .zn
.eob1:
    stc                             ; (to its end: done)
    ret
.zn:
    clc
    ret
.eob:
    pop bx
    stc
    ret
.bad:
    jmp pj_badb

; pj_sk - the skip loop: every symbol decoded, nothing kept, to the block's
; end. BH = the LEN page, BP = k x 2 - 128 (negative until k is 64), DH = 0
pj_sk:
.top:
    PEEK8
    mov bl, ah
    add cl, [bx]                    ; the bits it takes
    mov dl, [bx + JA_ADV - JA_LEN]  ; k's step x 2
    NORM
    add bp, dx
    js .top
    jz .done                        ; (a coefficient at 63: the end)
    cmp bp, 64
    jae .spec
    mov al, [bx + JA_RS - JA_LEN]   ; a WHOLE coefficient past 63: its
    and al, 15                      ; magnitude given back to the cut-short
    mov [cs:pj_gb], al              ; test, as the reference refuses its
    jmp pj_badb                     ; place before reading it
.spec:
    cmp byte [bx + JA_RS - JA_LEN], 0   ; EOB, its code taken: the block's
    jne .sp                         ; end, without the special path (a long
    cmp byte [bx], 0                ; code's entry is LEN 0, and its RS
    jne .done                       ; byte nothing)
.sp:
    call pj_acsp
    jc .done
    or bp, bp
    js .top
.done:
    jmp pj_bend

; VLOOP n, end - the value loop of scale n, keeping k < end / 2: a WHOLE
; entry's coefficient, when its place is kept (JS_KNW), x its multiplier
; into D, and its columns into JV_MASK. BP = k x 2 - 128
%macro VLOOP 2
pj_va%1:
.top:
    PEEK8
    mov bl, ah
    add cl, [bx]
    mov dl, [bx + JA_ADV - JA_LEN]
    NORM
    add bp, dx
    cmp bp, %2 - 128
    jg .out
    mov di, [ds:bp + JS_KNW + 126]  ; its natural place x 2, or FFFFh
    or di, di
    js .top
    mov al, [bx + JA_VLO - JA_LEN]
    mov ah, [bx + JA_VHI - JA_LEN]
.mul:
.imul:
    imul word [di + 0x7FFF]         ; PATCHED: the component's multipliers
    mov [di + JS_D], ax
    mov ax, [di + JS_NMASK]
    or [JV_MASK], ax
    xor dx, dx
    cmp bp, %2 - 128
    jl .top
%if %2 < 128
    or bp, bp                       ; the rest of the block: nothing kept
    js .sk
%endif
    jmp pj_bend
.out:
    cmp bp, 64
    jge .spec
%if %2 < 128
    or bp, bp                       ; a coefficient past the kept range
    js .sk
    jz .end
%endif
    mov al, [bx + JA_RS - JA_LEN]   ; ...past 63: its magnitude given back
    and al, 15
    mov [cs:pj_gb], al
    jmp pj_badb
.spec:
    cmp byte [bx + JA_RS - JA_LEN], 0   ; EOB (pj_sk's test)
    jne .sp
    cmp byte [bx], 0
    jne .end
.sp:
    call pj_acsp
    jc .end
    or ax, ax
    jz .z
    cmp bp, %2 - 128                ; a coefficient: kept?
    jg .past
    mov di, [ds:bp + JS_KNW + 126]
    or di, di
    jns .mul
.z:
    cmp bp, %2 - 128
    jl .top
.past:
%if %2 < 128
    or bp, bp
    js .sk
%endif
.end:
    jmp pj_bend
%if %2 < 128
.sk:
    jmp pj_sk.top
%endif
%endmacro

    VLOOP 0, 128
    VLOOP 1, 50
    VLOOP 2, 10

; pj_isoverb - pj_isover for the bit position: CF = 1 when a bit past the
; segment's end has been taken - less the [pj_gb] last bits. SI/CL live.
; Preserves all
pj_isoverb:
    push ax
    cmp byte [JV_OVER], 0
    jne .y
    cmp byte [JV_ENDED], 0
    je .n
    mov ax, si
    sub ax, [JV_CEND]
    jl .n
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add al, cl
    adc ah, 0                       ; AX = the bits taken past the end
    push bx
    mov bl, [cs:pj_gb]
    xor bh, bh
    cmp ax, bx
    pop bx
    jle .n                          ; (none, or only those given back)
.y:
    stc
    pop ax
    ret
.n:
    clc
    pop ax
    ret

; pj_badb - pj_bad for the bit position
pj_badb:
    call pj_isoverb
    jc .t
    jmp pj_data
.t:
    jmp pj_trunc

; pj_chkb - an MCU's end, the position banked: `cut short` if a bit past
; the end was taken. Preserves all
pj_chkb:
    push cx
    push si
    mov si, [JV_CPOS]
    mov cl, [JV_BOFF]
    call pj_isoverb
    pop si
    pop cx
    jc .t
    ret
.t:
    jmp pj_trunc

; =============================================================================
; THE IDCT'S MULTIPLIES BY TABLE (SPEC.md 106.22). MUL(x, K) = s16((x K)
; >> 8) is, for x = 256 xh + xl (xh signed, xl unsigned), exactly xh K +
; ((xl K) >> 8) - and each of jidctfst.c's constants is 256 m + c with c
; under 256: 362 = 256 + 106, 473 = 256 + 217, 277 = 256 + 21 and -669 =
; -768 + 99, so MUL(x, K) = m x + ((xl c) >> 8) + xh c. A constant c is
; three pages: (xl c) >> 8 a byte, then xh c's low and high bytes. Two
; lookups and an add where an 8088's `imul` was ~140 cycles, and DX is
; left alone. At 1/1 the four c (12 pages), at 1/2 the one (3), in the
; scratch's fixed part at a fixed page so the pages are immediates
; =============================================================================
JS_MPG      equ 0x4B00              ; the pages: 106, 217, 21, 99 at 1/1
JM_106      equ (JS_MPG >> 8)
JM_217      equ JM_106 + 3
JM_21       equ JM_106 + 6
JM_99       equ JM_106 + 9

; MULT page - AX = x: AX = (x c) >> 8's low word, c the page's constant.
; Clobbers BX, CL
%macro MULT 1
    mov bl, al
    mov bh, %1
    mov cl, [bx]
    mov bl, ah
    inc bh
    mov al, [bx]
    inc bh
    mov ah, [bx]
    add al, cl
    adc ah, 0
%endmacro

; pj_mtabs - the scale's multiply pages. Preserves all
pj_mtabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov cl, [JV_SCL]
    cmp cl, 2
    jae .out
    mov si, pj_mcon
    mov di, JS_MPG
    mov bp, 4                       ; constants: four at 1/1...
    or cl, cl
    jz .c
    mov bp, 1                       ; ...one at 1/2
.c:
    mov bl, [cs:si]                 ; BX = c
    xor bh, bh
    inc si
    xor dx, dx                      ; DX = xl c
    xor al, al
.lo:
    mov [di], dh
    add dx, bx
    inc di
    inc al
    jnz .lo
    mov ax, bx                      ; -128 c, for xh = 80h
    mov cl, 7
    shl ax, cl
    neg ax
    xor dx, dx                      ; DX = xh c, xh = 0, 1, ...
    xor cl, cl
.hi:
    cmp cl, 128
    jne .h
    mov dx, ax
.h:
    mov [di], dl
    mov [di + 256], dh
    add dx, bx
    inc di
    inc cl
    jnz .hi
    add di, 256
    dec bp
    jnz .c
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
pj_mcon:    db 106, 217, 21, 99

; PUTC k - [di + k] := clamp(AX >> 5), CL = 5. Clobbers AX
%macro PUTC 1
    sar ax, cl
    or ah, ah
    jz %%ok
    mov al, 0
    js %%ok
    mov al, 255
%%ok:
    mov [di + %1], al
%endmacro

; R4 s - the 4-point pass (pixelsim's red4) on [si + k s], k = 0..3: out AX
; = ea + oa, BX = ea - oa, SI = eb (preserved inputs gone), DX = ob -
; the outputs ea + oa, eb + ob, eb - ob, ea - oa are AX, SI + DX, SI - DX,
; BX. Clobbers BP, CL
%macro R4 1
    mov ax, [si + 3 * %1]           ; d3
    MULT JM_106
    add ax, [si + 1 * %1]           ; oa = d1 + MUL(d3, 106)
    mov bp, ax
    mov ax, [si + 1 * %1]           ; d1
    MULT JM_106
    sub ax, [si + 3 * %1]           ; ob = MUL(d1, 106) - d3
    mov dx, ax
    mov ax, [si]                    ; d0
    mov bx, [si + 2 * %1]           ; d2
    mov si, ax
    add ax, bx                      ; ea
    sub si, bx                      ; eb
    mov bx, ax
    add ax, bp                      ; ea + oa
    sub bx, bp                      ; ea - oa
%endmacro

; pj_id4 - 1/2: D's 4x4 through the columns that hold anything (a column
; with nothing below its DC is that DC four times, which the pass would
; give), then the rows; all rows the same when no column but the first
; holds anything
pj_id4:
    mov di, [JV_PL]
    mov ax, [JV_MASK]
    test al, 0x0E
    jnz .g
    test ah, 1
    jnz .c0
    mov ax, [JS_D]                  ; the DC alone: sixteen of one pixel
    mov cl, 5
    PUTC 0
    mov ah, al
    mov bx, [JV_ST]
%rep 4
    mov [di], ax
    mov [di + 2], ax
    add di, bx
%endrep
    ret
.c0:
    mov si, JS_D                    ; column 0 alone: each row one pixel
    R4 16
    mov cx, 5
    mov [JS_WS], ax
    mov [JS_WS + 24], bx
    mov ax, si
    add ax, dx
    mov [JS_WS + 8], ax
    sub si, dx
    mov [JS_WS + 16], si
    mov si, JS_WS
    mov dx, [JV_ST]
%rep 4
    lodsw
    PUTC 0
    mov ah, al
    mov [di], ax
    mov [di + 2], ax
    add si, 6
    add di, dx
%endrep
    ret
.g:
%assign u 0
%rep 4
    test byte [JV_MASK + 1], 1 << u
    jz .cc%+u
    mov si, JS_D + 2 * u
    R4 16
    mov [JS_WS + 2 * u], ax
    mov [JS_WS + 24 + 2 * u], bx
    mov ax, si
    add ax, dx
    mov [JS_WS + 8 + 2 * u], ax
    sub si, dx
    mov [JS_WS + 16 + 2 * u], si
    jmp short .cn%+u
.cc%+u:
    mov ax, [JS_D + 2 * u]
    mov [JS_WS + 2 * u], ax
    mov [JS_WS + 8 + 2 * u], ax
    mov [JS_WS + 16 + 2 * u], ax
    mov [JS_WS + 24 + 2 * u], ax
.cn%+u:
%assign u u + 1
%endrep
%assign y 0
%rep 4
    mov si, JS_WS + 8 * y
    R4 2
    push bx
    push si
    mov cl, 5
    PUTC 0
    pop si
    pop bx
    mov ax, si
    add ax, dx
    PUTC 1
    mov ax, si
    sub ax, dx
    PUTC 2
    mov ax, bx
    PUTC 3
    add di, [JV_ST]
%assign y y + 1
%endrep
    ret

; AAN s - jidctfst.c's 1-D pass in 16 bits (pixelsim's aan8) on [si + k
; s], k = 0..7. The even part first, t3 t2 t1 t0 pushed (t0 on top); then
; the odd part: out DX = t4, BX = t5, AX = t6, SI = t7. The outputs are
; t0 + t7, t1 + t6, t2 + t5, t3 - t4, t3 + t4, t2 - t5, t1 - t6, t0 - t7.
; Clobbers CX, BP (and SI)
%macro AAN 1
    mov ax, [si + 2 * %1]           ; --- the even part ---
    mov bx, [si + 6 * %1]
    mov dx, ax
    add dx, bx                      ; t13 = d2 + d6
    sub ax, bx
    mov bp, ax
    MULT JM_106
    add ax, bp                      ; MUL(d2 - d6, 362)
    sub ax, dx                      ; t12
    mov bp, ax
    mov ax, [si]
    mov bx, [si + 4 * %1]
    mov cx, ax
    add ax, bx                      ; t10 = d0 + d4
    sub cx, bx                      ; t11 = d0 - d4
    mov bx, ax
    add ax, dx                      ; t0 = t10 + t13
    sub bx, dx                      ; t3 = t10 - t13
    push bx
    mov bx, cx
    sub bx, bp                      ; t2 = t11 - t12
    push bx
    add cx, bp                      ; t1 = t11 + t12
    push cx
    push ax
    mov ax, [si + 1 * %1]           ; --- the odd part ---
    mov bx, [si + 7 * %1]
    mov dx, ax
    add ax, bx                      ; z11 = d1 + d7
    sub dx, bx                      ; z12 = d1 - d7
    mov bx, [si + 5 * %1]
    mov si, [si + 3 * %1]
    mov bp, bx
    add bx, si                      ; z13 = d5 + d3
    sub bp, si                      ; z10 = d5 - d3
    mov si, ax
    add ax, bx                      ; t7 = z11 + z13
    sub si, bx
    push ax
    push dx
    mov ax, si
    MULT JM_106
    add ax, si                      ; t11 = MUL(z11 - z13, 362)
    pop dx                          ; (z12)
    push ax
    mov ax, bp
    add ax, dx
    mov si, ax
    MULT JM_217
    add ax, si                      ; z5 = MUL(z10 + z12, 473)
    push ax
    mov ax, dx
    MULT JM_21
    add ax, dx                      ; MUL(z12, 277)
    pop si                          ; (z5)
    sub ax, si
    mov dx, ax                      ; t10 = MUL(z12, 277) - z5
    mov ax, bp
    MULT JM_99
    sub ax, bp
    sub ax, bp
    sub ax, bp                      ; MUL(z10, -669)
    add ax, si                      ; t12 = MUL(z10, -669) + z5
    pop bx                          ; (t11)
    pop si                          ; t7
    sub ax, si                      ; t6 = t12 - t7
    sub bx, ax                      ; t5 = t11 - t6
    add dx, bx                      ; t4 = t10 + t5
%endmacro

; AANCOL - AAN's eight outputs into [di + 16 k]: the even part's four
; popped (AAN pushed them). Clobbers AX, CX, BP
%macro AANCOL 0
    mov bp, ax
    pop ax                          ; t0
    mov cx, ax
    add ax, si
    sub cx, si
    mov [di], ax
    mov [di + 112], cx
    pop ax                          ; t1
    mov cx, ax
    add ax, bp
    sub cx, bp
    mov [di + 16], ax
    mov [di + 96], cx
    pop ax                          ; t2
    mov cx, ax
    add ax, bx
    sub cx, bx
    mov [di + 32], ax
    mov [di + 80], cx
    pop ax                          ; t3
    mov cx, ax
    add ax, dx
    sub cx, dx
    mov [di + 64], ax
    mov [di + 48], cx
%endmacro

; AANROW - AAN's eight outputs clamped into [di + k]. Clobbers AX, CX, BP
%macro AANROW 0
    mov bp, ax
    mov cl, 5
    pop ax                          ; t0
    push ax
    add ax, si
    PUTC 0
    pop ax
    sub ax, si
    PUTC 7
    pop ax                          ; t1
    push ax
    add ax, bp
    PUTC 1
    pop ax
    sub ax, bp
    PUTC 6
    pop ax                          ; t2
    push ax
    add ax, bx
    PUTC 2
    pop ax
    sub ax, bx
    PUTC 5
    pop ax                          ; t3
    push ax
    add ax, dx
    PUTC 4
    pop ax
    sub ax, dx
    PUTC 3
%endmacro

; pj_id8 - 1/1: jidctfst.c's columns then rows. A column with nothing
; below its DC is that DC eight times, and when no column but the first
; holds anything every row is its first value - the butterflies would give
; exactly those (SPEC.md 106.19)
pj_id8:
    xor ax, ax                      ; [pj_u] = u x 2
    mov [cs:pj_u], ax
.col:
    mov cx, [cs:pj_u]
    shr cl, 1
    mov al, [JV_MASK + 1]
    shr al, cl
    test al, 1
    jnz .full
    mov bx, [cs:pj_u]
    mov ax, [JS_D + bx]
%assign k 0
%rep 8
    mov [JS_WS + bx + k * 16], ax
%assign k k + 1
%endrep
    jmp .nc
.full:
    mov si, [cs:pj_u]
    lea di, [si + JS_WS]
    add si, JS_D
    AAN 16
    AANCOL
.nc:
    add word [cs:pj_u], 2
    cmp word [cs:pj_u], 16
    jae .rows
    jmp .col
.rows:
    mov di, [JV_PL]
    test byte [JV_MASK], 0xFE
    jz .dc
    mov word [cs:pj_u], JS_WS       ; [pj_u] = the row
.row:
    mov si, [cs:pj_u]
    AAN 2
    AANROW
    add di, [JV_ST]
    add word [cs:pj_u], 16
    cmp word [cs:pj_u], JS_WS + 128
    jae .done
    jmp .row
.done:
    ret
.dc:
    mov si, JS_WS
    mov dx, [JV_ST]
    mov cl, 5
.dr:
    lodsw
    PUTC 0
    mov ah, al
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    add si, 14
    add di, dx
    cmp si, JS_WS + 128
    jb .dr
    ret

; pj_id2 - 1/4: the 2x2 - d0 + d1, d0 - d1 each way
pj_id2:
    mov ax, [JS_D + 0]              ; column 0: d(0,0), d(1,0)
    mov bx, [JS_D + 16]
    mov cx, ax
    add ax, bx
    sub cx, bx
    mov si, ax                      ; WS row 0, row 1: SI BP / DX BX
    mov bp, cx
    mov ax, [JS_D + 2]              ; column 1
    mov bx, [JS_D + 18]
    mov dx, ax
    add dx, bx
    sub ax, bx
    mov bx, ax
    mov di, [JV_PL]
    mov cl, 5
    mov ax, si
    add ax, dx
    PUTC 0
    mov ax, si
    sub ax, dx
    PUTC 1
    add di, [JV_ST]
    mov ax, bp
    add ax, bx
    PUTC 0
    mov ax, bp
    sub ax, bx
    PUTC 1
    ret

; pj_id1 - 1/8: the DC
pj_id1:
    mov ax, [JS_D]
    mov di, [JV_PL]
    mov cl, 5
    PUTC 0
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
    mov [JV_DONEH], ax
    mov ax, [JV_TOTAL]
    mov [JV_UNIT], ax
    mov ax, [JV_TOTALH]
    mov [JV_UNITH], ax
    call pj_bprog
    mov word [JV_MLT], 0xFFFF       ; (the first block patches the loops)
.my:
    mov word [JV_MX], 0
    call pj_brow
.mx:
    mov bx, JS_BPROG
.b:
    mov [JV_BPI], bx
    call pj_blk
    mov bx, [JV_BPI]
    add bx, JB_SZ
    cmp bx, [JV_BPE]
    jb .b
    ; --- the MCU's end: a bit past the end, the restart interval --------
    call pj_chkb
    add word [JV_DONE], 1
    adc word [JV_DONEH], 0
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
    mov ax, [JV_DONEH]              ; (32 bits: wave-4 review F5)
    cmp ax, [JV_UNITH]
    jne .cmp
    mov ax, [JV_DONE]
    cmp ax, [JV_UNIT]
.cmp:
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

; CHROMA - the next chroma pixel, by the pointer on the stack (moved on):
; BP, DX, CX = R's, G's and B's clamp places for it (pj_ctabs). Cr's
; displacement from Cb (.crN) is PATCHED per row. Clobbers AX, BX
%macro CHROMA 1
    pop bx
    mov al, [bx]                    ; Cb
.cr%1:
    mov ah, [bx + 0x7FFF]           ; Cr (PATCHED: Cr's plane less Cb's)
    inc bx
    push bx
    mov bl, ah
    xor bh, bh
    shl bx, 1                       ; BX = Cr x 2
    mov bp, [JS_RCR + bx]
    mov cx, [JS_GRL + bx]
    mov dx, [JS_GRH + bx]
    mov bl, al
    xor bh, bh
    shl bx, 1                       ; BX = Cb x 2
    add cx, [JS_GBL + bx]
    adc dx, [JS_GBH + bx]           ; G's: the high word, the carry in it
    mov cx, [JS_BCB + bx]
%endmacro

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
    mov [cs:.cr0 + 2], ax           ; Cr's place from Cb's: PATCHED, a row
    mov [cs:.cr1 + 2], ax           ; before it is fetched
    mov [cs:.cr2 + 2], ax
    mov [cs:.cr3 + 2], ax
    add cx, si
    mov [JV_X], cx                  ; [JV_X] = the Y row's end
    push word [JV_CX]               ; the chroma pointer, on the stack
    mov al, [JV_FR + JF_HMAX]
    cmp al, 2
    je .h2
    cmp al, 1
    je .h1
    ; --- 4:1:1: four luma pixels a chroma one -------------------------------
.h4:
    mov ax, [JV_X]
    sub ax, si
    cmp ax, 4
    jb .tail
    CHROMA 0
    YCC1
    YCC1
    YCC1
    YCC1
    jmp .h4
    ; --- 4:4:x: one ---------------------------------------------------------
.h1:
    cmp si, [JV_X]
    jae .done
    CHROMA 1
    YCC1
    jmp short .h1
    ; --- 4:2:x: two ---------------------------------------------------------
.h2:
    mov ax, [JV_X]
    dec ax
.h2l:
    cmp si, ax                      ; (a pair left: SI < end - 1)
    jae .tail
    CHROMA 2
    YCC1
    YCC1
    mov ax, [JV_X]
    dec ax
    jmp short .h2l
.tail:
    cmp si, [JV_X]                  ; the row's last pixels, fewer than the
    jae .done                       ; chroma one serves
    CHROMA 3
.tl:
    YCC1
    cmp si, [JV_X]
    jb .tl
.done:
    pop ax                          ; (the chroma pointer)
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
    mov [JV_DONEH], ax
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
    mov al, [cs:pj_ch]              ; the row's first: by (mcux H), in 32
    xor ah, ah                      ; bits (wave-4 review F6)
    mul word [JV_FR + JF_MCUX]
    mul word [JV_MY]
    mov [cs:pj_rbi], ax
    mov [cs:pj_rbi + 2], dx
    mov word [JV_MX], 0
.bx:
    mov ax, [cs:pj_rbi]             ; bi = by (mcux H) + bx
    mov dx, [cs:pj_rbi + 2]
    add ax, [JV_MX]
    adc dx, 0
    mov [JV_BI], ax
    mov [JV_BIH], dx
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
    ; --- interleaved (DC scans): MCUs, by a program of the MCU's blocks -----
    mov ax, [JV_TOTAL]
    mov [JV_UNIT], ax
    mov ax, [JV_TOTALH]
    mov [JV_UNITH], ax
    call pj_pprog
    mov word [JV_MY], 0
.my:
    mov bx, JS_BPROG                ; the MCU row's first block of each
.r:
    mov ax, [bx + PB_ROW]
    mov dx, [bx + PB_ROWH]
    add ax, [bx + PB_OFF]
    adc dx, 0
    mov [bx + PB_CUR], ax
    mov [bx + PB_CURH], dx
    add bx, PB_SZ
    cmp bx, [JV_BPE]
    jb .r
    mov word [JV_MX], 0
.mx:
    mov bx, JS_BPROG
.b:
    mov ax, [bx + PB_CUR]
    mov dx, [bx + PB_CURH]
    mov [JV_BI], ax
    mov [JV_BIH], dx
    add ax, [bx + PB_COL]
    adc dx, 0
    mov [bx + PB_CUR], ax
    mov [bx + PB_CURH], dx
    push bx
    mov bx, [bx + PB_SC]
    call pj_pblock
    pop bx
    add bx, PB_SZ
    cmp bx, [JV_BPE]
    jb .b
    call pj_punit
    inc word [JV_MX]
    mov ax, [JV_MX]
    cmp ax, [JV_FR + JF_MCUX]
    jb .mx
    mov bx, JS_BPROG                ; the next MCU row
.rn:
    mov ax, [bx + PB_RSTEP]
    add [bx + PB_ROW], ax
    adc word [bx + PB_ROWH], 0
    add bx, PB_SZ
    cmp bx, [JV_BPE]
    jb .rn
    inc word [JV_MY]
    mov ax, [JV_MY]
    cmp ax, [JV_FR + JF_MCUY]
    jb .my
    ret

; pj_pprog - an interleaved progressive scan's MCU as a program of blocks:
; for each scan component, its V x H blocks - the store's block index of
; each at MCU (0, 0) (v (mcux H) + h), and the steps an MCU across (H) and
; an MCU row down (V mcux H). Preserves all
PB_SC       equ 0                   ; word: the scan's component, x 8
PB_OFF      equ 2                   ; word: v (mcux H) + h
PB_COL      equ 4                   ; word: H
PB_RSTEP    equ 6                   ; word: V (mcux H)
PB_ROW      equ 8                   ; dword: my V (mcux H)
PB_ROWH     equ 10
PB_CUR      equ 12                  ; dword: this MCU's block index
PB_CURH     equ 14
PB_SZ       equ 16
pj_pprog:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, JS_BPROG
    xor bx, bx
.c:
    mov al, [JV_SC + bx + JS_CI]
    call pj_cset
    mov al, [cs:pj_ch]
    xor ah, ah
    mul word [JV_FR + JF_MCUX]
    mov cx, ax                      ; CX = mcux H
    mov byte [cs:pj_v], 0
.v:
    mov byte [cs:pj_h], 0
.h:
    mov [di + PB_SC], bx
    mov al, [cs:pj_v]
    xor ah, ah
    mul cx
    add al, [cs:pj_h]
    adc ah, 0
    mov [di + PB_OFF], ax
    mov al, [cs:pj_ch]
    xor ah, ah
    mov [di + PB_COL], ax
    mov al, [cs:pj_cv]
    xor ah, ah
    mul cx
    mov [di + PB_RSTEP], ax
    mov word [di + PB_ROW], 0
    mov word [di + PB_ROWH], 0
    add di, PB_SZ
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
    mov [JV_BPE], di
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_punit - a unit of a progressive scan done: the check, the interval,
; and every 64 units the progress (which is also where a cancel is seen).
; Preserves all
pj_punit:
    call pj_chkb
    add word [JV_DONE], 1
    adc word [JV_DONEH], 0
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
    mov [JV_UNITH], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pj_pblock - scan component BX (x 8), block [JV_BI] of its store: the
; scan's kind of decode on it, by the bit position (pj_blk's reader). The
; position loaded and banked here
pj_pblock:
    cmp byte [JV_SS], 0             ; an AC first scan's EOB run: nothing
    je .full                        ; to read (pj_acfb's own test, before
    cmp byte [JV_AH], 0             ; anything is loaded)
    jne .full
    cmp word [JV_EOBRUN], 0
    je .full
    dec word [JV_EOBRUN]
    ret
.full:
    push bx
    push es
    mov si, [JV_CPOS]
    mov cl, [JV_BOFF]
    mov ax, [JV_CEND]               ; the clean data ahead (pj_room)
    sub ax, si
    jl .room
    cmp ax, JP_AHEAD
    jge .rok
.room:
    call pj_room
.rok:
    mov al, [JV_SC + bx + JS_CI]
    xor ah, ah
    shl ax, 1
    mov di, ax                      ; DI = the component x 2
    mov ax, [JV_SBASE + di]         ; ES:BP = the block's store
    mov bp, [JV_BI]
    cmp byte [JV_SCL], 3
    je .s8
    add ax, bp                      ; (1/4: under F000h blocks, a word)
    xor bp, bp
    jmp short .s
.s8:
    push cx                         ; 1/8: bi >> 3 paragraphs on, from 32
    mov cx, [JV_BIH]                ; bits (wave-4 review F6)
    mov dx, bp
    shr cx, 1
    rcr dx, 1
    shr cx, 1
    rcr dx, 1
    shr cx, 1
    rcr dx, 1
    pop cx
    and bp, 7
    shl bp, 1
    add ax, dx
.s:
    mov es, ax
    cmp byte [JV_SS], 0
    jne .ac
    ; --- DC ------------------------------------------------------------------
    cmp byte [JV_AH], 0
    jne .dcr
    mov ax, [JV_SC + bx + JS_DCS]
    mov [JV_SLOWP], ax
    mov bh, [JV_SC + bx + JS_DCP]
    PEEK8
    mov bl, ah
    mov ch, [bx + 256]
    or ch, ch
    jz .dw
    add cl, ch
    NORM
    mov al, [bx]
    jmp short .dg
.dw:
    call pj_walk9
.dg:
    or al, al
    jz .d0
    call pj_bits
    jmp short .dd
.d0:
    xor ax, ax
.dd:
    add [JV_PRED + di], ax
    mov ax, [JV_PRED + di]
    mov ch, cl
    mov cl, [JV_AL]
    shl ax, cl
    mov cl, ch
    mov [es:bp], ax
    jmp short .out
.dcr:
    PEEK8                           ; one bit
    inc cl
    NORM
    test ah, 0x80
    jz .out
    mov ax, [JV_P1]
    or [es:bp], ax
    jmp short .out
.ac:
    mov ax, [JV_SC + bx + JS_ACS]
    mov [JV_SLOWP], ax
    mov bh, [JV_SC + bx + JS_ACP]
    add bh, JA_LEN >> 8             ; BH = the LEN page
    xor dx, dx
    cmp byte [JV_AH], 0
    jne .acr
    call pj_acfb
    jmp short .out
.acr:
    call pj_acrb
.out:
    mov [JV_CPOS], si
    mov [JV_BOFF], cl
    pop es
    pop bx
    ret

; pj_rbits - AL = n, 1..15: AX = the next n bits as they are, taken.
; SI/CL live. Preserves BX, DX, DI, BP; clobbers CH
pj_rbits:
    push bx
    mov bl, al
    xor bh, bh
    mov ax, [si]
    xchg al, ah
    shl ax, cl
    add cl, bl
    mov ch, cl
    cmp cl, 16
    jbe .x
    push dx                         ; the third byte's top bits
    mov dl, [si + 2]
    mov dh, ch
    sub dh, bl
    mov cl, 8
    sub cl, dh
    shr dl, cl
    or al, dl
    pop dx
.x:
    mov cl, 16
    sub cl, bl
    shr ax, cl
    mov bl, ch
    mov cl, ch
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    add si, bx
    and cl, 7
    pop bx
    ret

; pj_kset - DL = k, AX = a value: into the store's word for k when 1/4
; keeps it (k = 1, 2, 4), and k's bit of the nonzero history set. ES =
; the block's paragraph. Preserves all
pj_kset:
    push bx
    push ax
    cmp dl, 4
    ja .h
    mov bx, dx                      ; (DH = 0)
    mov bl, [cs:pj_kidx + bx]
    cmp bl, 0xFF
    je .h
    shl bl, 1
    mov [es:bx], ax
.h:
    mov bl, dl
    and bl, 7
    xor bh, bh
    mov al, [cs:pj_bitm + bx]
    mov bl, dl
    shr bl, 1
    shr bl, 1
    shr bl, 1
    or [es:bx + 8], al
    pop ax
    pop bx
    ret

; pj_acfb - an AC first scan's block: EOB runs, Ss..Se, values << Al. DL =
; k (DH = 0), BH = the LEN page. SI/CL live
pj_acfb:
    cmp word [JV_EOBRUN], 0
    je .go
    dec word [JV_EOBRUN]
    ret
.go:
    mov dl, [JV_SS]
.l:
    cmp dl, [JV_SE]
    ja .done
    PEEK8
    mov bl, ah
    mov al, [bx + JA_ADV - JA_LEN]
    cmp al, JA_SPEC
    je .sp
    add cl, [bx]                    ; WHOLE: its bits taken
    NORM
    shr al, 1
    dec al
    add dl, al                      ; k += r: past Se is damaged, the
    cmp dl, [JV_SE]                 ; magnitude given back
    ja .badgb
    mov al, [bx + JA_VLO - JA_LEN]
    mov ah, [bx + JA_VHI - JA_LEN]
    jmp short .put
.sp:
    cmp byte [bx], 0
    jne .code
    call pj_walk9
    jmp short .rs
.code:
    add cl, [bx]
    NORM
    mov al, [bx + JA_RS - JA_LEN]
.rs:
    mov ah, al
    and ah, 15                      ; s
    jz .z
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    add dl, al                      ; k += r, before the magnitude
    cmp dl, [JV_SE]
    ja .bad
    mov al, ah
    call pj_bits
.put:
    mov ch, cl
    mov cl, [JV_AL]
    shl ax, cl
    mov cl, ch
    call pj_kset
    inc dl
    jmp short .l
.z:
    cmp al, 0xF0
    jne .eob
    add dl, 16                      ; ZRL
    jmp .l
.eob:
    shr al, 1                       ; EOBRUN = 2^r + r bits - 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov ch, cl
    mov cl, al
    mov bx, 1
    shl bx, cl
    mov cl, ch
    or al, al
    jz .e0
    call pj_rbits
    add bx, ax
.e0:
    dec bx
    mov [JV_EOBRUN], bx
.done:
    ret
.badgb:
    mov al, [bx + JA_RS - JA_LEN]
    and al, 15
    mov [cs:pj_gb], al
.bad:
    jmp pj_badb

; REF1 - DL = k, a history position: its correction bit read; a 1 makes a
; kept coefficient one step larger in magnitude where that bit is still
; clear (jdphuff.c). Clobbers AX, BX
%macro REF1 0
    PEEK8
    inc cl
    NORM
    test ah, 0x80
    jz %%n
    cmp dl, 4
    ja %%n
    mov bx, dx
    mov bl, [cs:pj_kidx + bx]
    cmp bl, 0xFF
    je %%n
    shl bl, 1
    mov ax, [es:bx]
    test ax, [JV_P1]
    jnz %%n
    or ax, ax
    js %%m
    add ax, [JV_P1]
    jmp short %%s
%%m:
    add ax, [JV_M1]
%%s:
    mov [es:bx], ax
%%n:
%endmacro

; HIST - DL = k: ZF = 0 when k has been nonzero. Clobbers AX, BX
%macro HIST 0
    mov bl, dl
    and bl, 7
    xor bh, bh
    mov al, [cs:pj_bitm + bx]
    mov bl, dl
    shr bl, 1
    shr bl, 1
    shr bl, 1
    test [es:bx + 8], al
%endmacro

; BULK - from k (DL) a whole byte of positions past the kept ones, when
; k >= 8 starts one and Se is past it: AL = its history byte, AH its
; zero positions; ZF = 1 when it cannot be taken whole. Clobbers BX
%macro BULK 1
    test dl, 7
    jnz %1
    cmp dl, 8
    jb %1
    mov al, dl
    add al, 7
    cmp al, [JV_SE]
    ja %1
    mov bl, dl
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor bh, bh
    mov al, [es:bx + 8]             ; the history byte
    mov bx, pj_popc
    cs xlatb                        ; AL = its positions with history
    mov ah, 8
    sub ah, al                      ; AH = its zero ones
%endmacro

; NORMW - CL any number of bits past SI: whole bytes on. Clobbers AX
%macro NORMW 0
    mov al, cl
    shr al, 1
    shr al, 1
    shr al, 1
    xor ah, ah
    add si, ax
    and cl, 7
%endmacro

; pj_acrb - an AC refinement scan's block (jdphuff.c's
; decode_mcu_AC_refine, on the history). DL = k (DH = 0), BH = the LEN
; page. SI/CL live. The correction bits of the history positions past the
; kept ones are counted a byte at a time (pj_popc) and taken whole: their
; values change nothing 1/4 keeps
pj_acrb:
    mov dl, [JV_SS]
    cmp word [JV_EOBRUN], 0
    je .l
    jmp .eob
.l:
    cmp dl, [JV_SE]
    jbe .sym
    ret
.sym:
    PEEK8
    mov bl, ah
    mov al, [bx + JA_ADV - JA_LEN]
    cmp al, JA_SPEC
    je .sp
    add cl, [bx]                    ; WHOLE: a new coefficient's size is
    NORM                            ; always 1, its sign bit in the entry
    mov ah, [bx + JA_RS - JA_LEN]   ; (else `damaged` once its code is
    and ah, 15                      ; read: the rest given back)
    cmp ah, 1
    jne .badgb
    shr al, 1
    dec al
    mov ch, al                      ; CH = r
    mov di, [JV_P1]
    cmp byte [bx + JA_VLO - JA_LEN], 1
    je .walk
    mov di, [JV_M1]
    jmp short .walk
.sp:
    cmp byte [bx], 0
    jne .code
    call pj_walk9
    jmp short .rs
.code:
    add cl, [bx]
    NORM
    mov al, [bx + JA_RS - JA_LEN]
.rs:
    mov ah, al
    shr ah, 1
    shr ah, 1
    shr ah, 1
    shr ah, 1                       ; AH = r
    and al, 15                      ; AL = s
    jz .z
    cmp al, 1
    jne .bad
    mov ch, ah                      ; CH = r
    PEEK8                           ; the sign bit
    inc cl
    NORM
    mov di, [JV_P1]
    test ah, 0x80
    jnz .walk
    mov di, [JV_M1]
    jmp short .walk
.z:
    cmp ah, 15
    je .zrl
    mov ch, cl                      ; EOBRUN = 2^r + r bits, this block
    mov cl, ah                      ; its first
    mov di, 1
    shl di, cl
    mov cl, ch
    mov al, ah
    or al, al
    jz .ez
    call pj_rbits
    add di, ax
.ez:
    mov [JV_EOBRUN], di
    jmp .eob
.zrl:
    mov ch, 15                      ; ZRL: fifteen zeros and a sixteenth
    xor di, di
    ; --- past r zero positions, the history positions' bits read ----------
.walk:
    push bx
.w0:
    cmp dl, 8                       ; (the kept positions: one at a time)
    jae .wbytes
    cmp dl, [JV_SE]
    ja .wd
    HIST
    jz .zero0
    REF1
    inc dl
    jmp short .w0
.zero0:
    or ch, ch
    jz .wd
    dec ch
    inc dl
    jmp short .w0
.wbytes:
    mov bh, cl                      ; BH = the bit position's CL; AH the
    xor ah, ah                      ; history positions passed, their
.wby:                               ; correction bits taken at the end
    cmp dl, [JV_SE]
    ja .wfin
    mov bp, dx
    shr bp, 1
    shr bp, 1
    shr bp, 1
    mov al, [es:bp + 8]             ; AL = k's history byte, from k on
    mov cl, dl
    and cl, 7
    shr al, cl
    mov bl, 8                       ; BL = its positions, to Se at most
    sub bl, cl
    mov cl, [JV_SE]
    sub cl, dl
    inc cl
    cmp bl, cl
    jbe .wc
    mov bl, cl
.wc:
    or al, al                       ; none with history: BL zeros at once
    jnz .wb
    cmp ch, bl
    jb .wlast
    sub ch, bl
    add dl, bl
    jmp short .wby
.wlast:
    add dl, ch
    xor ch, ch
    jmp short .wfin
.wb:
    shr al, 1
    jnc .wz
    inc ah
    inc dl
    dec bl
    jnz .wb
    jmp short .wby
.wz:
    or ch, ch
    jz .wfin
    dec ch
    inc dl
    dec bl
    jnz .wb
    jmp short .wby
.wfin:
    mov cl, bh
    add cl, ah
    NORMW
.wd:
    pop bx
    or di, di                       ; the new coefficient at k
    jz .nn
    cmp dl, [JV_SE]
    ja .bad
    mov ax, di
    call pj_kset
.nn:
    inc dl
    jmp .l
    ; --- the EOB run: every history position's bit to Se ------------------
.eob:
.e0:
    cmp dl, 8
    jae .ebytes
    cmp dl, [JV_SE]
    ja .ed
    HIST
    jz .en0
    REF1
.en0:
    inc dl
    jmp short .e0
.ebytes:
    xor ah, ah                      ; AH = the bits to take
.eby:
    cmp dl, [JV_SE]
    ja .efin
    mov bp, dx
    shr bp, 1
    shr bp, 1
    shr bp, 1
    mov al, [es:bp + 8]
    mov bl, dl
    and bl, 7
    xor bh, bh
    and al, [cs:pj_lmask + bx]      ; k's byte's history from k...
    mov bl, dl
    or bl, 7
    cmp bl, [JV_SE]
    jbe .ef
    mov bl, [JV_SE]
    and bl, 7
    and al, [cs:pj_hmask + bx]      ; ...to Se
.ef:
    mov bx, pj_popc
    cs xlatb
    add ah, al
    or dl, 7
    inc dl
    jmp short .eby
.efin:
    add cl, ah
    NORMW
.ed:
    dec word [JV_EOBRUN]
    ret
.badgb:
    mov [cs:pj_gb], ah
.bad:
    jmp pj_badb

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
    mov al, [cs:pj_ch]              ; bpl = mcux H; the band's first block
    xor ah, ah                      ; my V bpl
    mul word [JV_FR + JF_MCUX]
    mov [cs:pj_bpl], ax
    mov al, [cs:pj_cv]
    xor ah, ah
    mul word [JV_MY]
    mul word [cs:pj_bpl]
    mov [JV_BI], ax
    mov [JV_BIH], dx
    mov byte [cs:pj_v], 0
.v:
    mov al, [cs:pj_v]               ; the row of blocks' place: v N rows
    mul byte [JV_FR + JL_N]
    mul word [JV_ST]
    add ax, [cs:pj_cpl]
    mov [JV_PL], ax
    mov cx, [cs:pj_bpl]
.b:
    push cx
    call pj_oblk
    add word [JV_BI], 1
    adc word [JV_BIH], 0
    mov al, [JV_FR + JL_N]
    xor ah, ah
    add [JV_PL], ax
    pop cx
    loop .b
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

; pj_oblk - block [JV_BI] of component [JV_COMP]'s store, dequantised into
; D (1/4: its four, natural 0, 1, 8 and 9; 1/8: the DC) and through the
; reduced IDCT into [JV_PL]. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
pj_oblk:
    mov bx, [JV_COMP]
    shl bx, 1
    mov ax, [JV_SBASE + bx]
    mov bp, [JV_BI]
    mov si, [JV_MLT]
    cmp byte [JV_SCL], 3
    je .o8
    add ax, bp                      ; 1/4: a paragraph a block
    mov es, ax
    mov ax, [es:0]
    imul word [si]
    add ax, JP_BIAS
    mov [JS_D], ax
%macro OKEEP 2                      ; the store's word %1 -> D's natural %2
    mov ax, [es:%1]
    or ax, ax
    jz %%z
    imul word [si + %2]
%%z:
    mov [JS_D + %2], ax
%endmacro
    OKEEP 2, 2
    OKEEP 4, 16
    OKEEP 6, 18
    push ds
    pop es
    jmp pj_id2
.o8:
    mov cx, [JV_BIH]                ; 1/8: a word a block, bi >> 3 from 32
    mov dx, bp                      ; bits
    shr cx, 1
    rcr dx, 1
    shr cx, 1
    rcr dx, 1
    shr cx, 1
    rcr dx, 1
    add ax, dx
    mov es, ax
    and bp, 7
    shl bp, 1
    mov ax, [es:bp]
    imul word [si]
    add ax, JP_BIAS
    mov [JS_D], ax
    push ds
    pop es
    jmp pj_id1

; =============================================================================
; THIS PART'S OWN MEMORY AND CONSTANTS (rule 1: through CS)
; =============================================================================
pj_zz:      db 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5
            db 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28
            db 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51
            db 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
pj_keep:    db 0x00, 0x24, 0x36, 0x3F   ; a natural index i is kept: i & K = 0
pj_kidx:    db 0, 1, 2, 0xFF, 3         ; zigzag k -> the store's word, 1/4
pj_bitm:    db 1, 2, 4, 8, 16, 32, 64, 128
pj_lmask:   db 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0, 0x80
pj_hmask:   db 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF
pj_popc:                                ; a byte's bits set
%assign i 0
%rep 256
            db ((i >> 0) & 1) + ((i >> 1) & 1) + ((i >> 2) & 1) + ((i >> 3) & 1) + ((i >> 4) & 1) + ((i >> 5) & 1) + ((i >> 6) & 1) + ((i >> 7) & 1)
%assign i i + 1
%endrep
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
pj_rbi:     dd 0                    ; a non-interleaved scan's row's first
pj_vend:    dw 0                    ; the scratch's variable part's end
pj_gb:      db 0                    ; bits a refusal gives back
pj_u:       dw 0                    ; pj_id8: the column, then the row
pj_vartab:  dw JS_VAR + 12 * 256, JS_VAR + 3 * 256, JS_VAR, JS_VAR

%include "pxplan.inc"                ; the plans: shared source (106.20)
