; =============================================================================
; os8088 - apps/pixel/pxpng.asm
;
; PiXEL's PNG DECODER, a far-called lazy PART (SPEC.md 106.5, 106.18): part 2
; of PIXEL.O88. HEAD reads IHDR from the head on the UI task; DECODE runs on
; the worker: the chunks to the first IDAT (PLTE, tRNS, the rest skipped),
; zlib's header, INFLATE into a 32 KB window, scanlines extracted from it,
; the five filters undone, every colour type and depth made into the
; emitter's row format, and the rows - or, for Adam7 at 1/1, each pass's
; pixels at their columns - handed to the package through K_EMIT / K_SCAT.
; tools/pixelsim.py's png_header and png_rows are this file in Python, check
; for check, and its `inflate` is the one this is held to.
;
; REGISTERS ACROSS THE DECODE. DS:SI is the INPUT cursor in a ring window
; from first to last, and every routine here keeps it unless it reads; in
; INFLATE, ES:DI is the window's write position, DX the bit buffer (its
; low CH bits valid, LSB first), BP the bytes left before the next scanline
; extraction, and AX, BX, CL are scratch. This part's own variables are
; [cs:...] throughout (apps/pixel/pxpart.inc rule 1).
; =============================================================================

%include "pxpart.inc"

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pz_init, pz_decode, pz_info, pz_head

; --- the scratch (PXK_DSEG): the WINDOW and the Huffman tables, then the two
; scanline buffers in a segment of their own --------------------------------
; A PRIMARY entry is the next nine bits' answer: AH = the code's length
; (1..9) for a literal or a distance; 80h | length for a length symbol or the
; end of the block (AL = symbol - 256); C0h for a code longer than nine bits,
; AL then the number of its SUB-TABLE - 64 entries indexed by the six bits
; after the nine, each entry AH = the bits past nine | 80h as above; and 0
; for no code, where the canonical walk decides (SPEC.md 106.18)
PZ_LPRIM    equ 0x8000              ; 512 words: the literal/length primary
PZ_LSUB     equ 0x8400              ; 64 sub-tables of 64 words
PZ_LNSUB    equ 64
PZ_LCNT     equ 0xA400              ; 16 words: codes of each length
PZ_LSYM     equ 0xA420              ; 288 words: symbols by (length, value)
PZ_DPRIM    equ 0xA660              ; the distance code's
PZ_DSUB     equ 0xAA60              ; 16 sub-tables
PZ_DNSUB    equ 16
PZ_DCNT     equ 0xB260
PZ_DSYM     equ 0xB280              ; 32 words
PZ_LENS     equ 0xB2C0              ; 320 lengths, then the code-length
PZ_CLL      equ PZ_LENS + 320       ; code's 19
PZ_OFFS     equ 0xB420              ; 16 words: a build's offsets
PZ_WPARA    equ 0xB44               ; ...all of it, paragraphs
PZ_ROWMAX   equ 32760               ; a row's bytes: two and their guards
                                    ; fit one segment (SPEC.md 106.18)
PZ_BATCH    equ 4096                ; bytes inflated between extractions

; PXV_INIT - out AX = PXP_PROBE
pz_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO - nothing to say
pz_info:
    mov ax, PXE_NOTSUP
    stc
    retf

; =============================================================================
; PXV_HEAD - the UI task: DS = the package, DI = the context. IHDR, the first
; chunk, is in the first 33 bytes of every head
; =============================================================================
pz_head:
    push es
    mov es, [di + PXK_HSEG]
    cmp word [di + PXK_HLEN], 33
    jb .head
    cmp word [es:8], 0              ; length 13...
    jne .head
    cmp word [es:10], 0x0D00
    jne .head
    cmp word [es:12], 'IH'          ; ...and the type IHDR
    jne .head
    cmp word [es:14], 'DR'
    jne .head
    mov ax, [es:18]                 ; the width, big-endian, 1..8192
    xchg al, ah
    cmp word [es:16], 0
    jne .dims
    call pz_dimok
    jc .dims
    mov [di + PXK_SW], ax
    mov [cs:pz_w], ax
    mov ax, [es:22]
    xchg al, ah
    cmp word [es:20], 0
    jne .dims
    call pz_dimok
    jc .dims
    mov [di + PXK_SH], ax
    mov [cs:pz_h], ax
    mov al, [es:24]                 ; the depth...
    mov ah, [es:25]                 ; ...and the colour type
    mov [cs:pz_depth], al
    mov [cs:pz_ct], ah
    mov bx, pz_types
.t:
    cmp byte [cs:bx], 0xFF
    je .depth
    cmp [cs:bx], ah
    jne .tn
    cmp [cs:bx + 1], al
    je .tok
.tn:
    add bx, 4
    jmp short .t
.tok:
    mov cl, [cs:bx + 2]             ; the channels
    mov [cs:pz_ch], cl
    mov ch, [cs:bx + 3]             ; the row format
    mov [di + PXK_RF], ch
    mov ax, [es:26]                 ; compression and filter method 0...
    or ax, ax
    jnz .pack
    mov al, [es:28]                 ; ...and interlace 0 or 1
    cmp al, 1
    ja .pack
    mov [cs:pz_il], al
    mov ah, PK_DEFL
    add ah, al
    mov [di + PXK_PACK], ah
    mov al, [cs:pz_depth]           ; bits a pixel
    mul cl
    mov [cs:pz_bitsp], al
    mov [di + PXK_BITS], al
    shr al, 1                       ; the filter's byte step: max(1, bits/8)
    shr al, 1
    shr al, 1
    jnz .bpp
    inc al
.bpp:
    mov [cs:pz_bpp], al
    mov ax, [cs:pz_w]
    call pz_rowbytes                ; AX = a row's bytes, CF = too many
    jc .big
    mov [cs:pz_rowb], ax
    shl ax, 1                       ; the scratch: the window, its tables,
    add ax, 16 + 15                 ; and two rows with eight zero bytes
    rcr ax, 1                       ; before each (a carry is bit 16)
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add ax, PZ_WPARA
    mov [di + PXK_DPARA], ax
    mov word [di + PXK_NPAL], 0
    pop es
    clc
    retf
.head:
    mov ax, PXD_HEAD
    jmp short .no
.dims:
    mov ax, PXD_DIMS
    jmp short .no
.depth:
    mov ax, PXD_DEPTH
    jmp short .no
.pack:
    mov ax, PXD_PACK
    jmp short .no
.big:
    mov ax, PXD_BIG
.no:
    pop es
    stc
    retf

; pz_dimok - AX = a dimension: CF = 1 unless 1..PX_DIMMAX. Preserves all
pz_dimok:
    or ax, ax
    jz .no
    cmp ax, PX_DIMMAX
    ja .no
    clc
    ret
.no:
    stc
    ret

; pz_rowbytes - AX = pixels: AX = their bytes at [pz_bitsp] bits each,
; rounded up; CF = 1 above PZ_ROWMAX. Preserves all but AX
pz_rowbytes:
    push cx
    push dx
    mov cl, [cs:pz_bitsp]
    xor ch, ch
    mul cx                          ; DX:AX = bits (at most 2^19)
    add ax, 7
    adc dx, 0
    mov cx, 3
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    or dx, dx
    jnz .no
    cmp ax, PZ_ROWMAX
    ja .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; the colour types PNG defines: type, depth, channels, row format
pz_types:
    db 0, 1, 1, RF_GREY
    db 0, 2, 1, RF_GREY
    db 0, 4, 1, RF_GREY
    db 0, 8, 1, RF_GREY
    db 0, 16, 1, RF_GREY
    db 2, 8, 3, RF_RGB
    db 2, 16, 3, RF_RGB
    db 3, 1, 1, RF_IDX
    db 3, 2, 1, RF_IDX
    db 3, 4, 1, RF_IDX
    db 3, 8, 1, RF_IDX
    db 4, 8, 2, RF_GREY
    db 4, 16, 2, RF_GREY
    db 6, 8, 4, RF_RGB
    db 6, 16, 4, RF_RGB
    db 0xFF

; =============================================================================
; PXV_DECODE - the worker: DS = the package, DI = the context
; =============================================================================
pz_decode:
    push bp
    push ds
    mov [cs:pz_ctx], di
    mov [cs:pz_pkg], ds
    mov al, [di + PXK_SCL]
    mov [cs:pz_scl], al
    mov ax, [di + PXK_RSEG]
    mov [cs:pz_rseg], ax
    mov ax, [di + PXK_DSEG]
    mov [cs:pz_wseg], ax
    add ax, PZ_WPARA
    mov [cs:pz_xseg], ax
    mov ax, [di + PXK_SPAL]
    mov [cs:pz_spal], ax
    xor bx, bx                      ; the services, as far pointers
    xor si, si
.svc:
    mov ax, [di + PXK_SVC + bx]
    mov [cs:pz_svc + si], ax
    mov [cs:pz_svc + si + 2], ds
    add si, 4
    inc bx
    inc bx
    cmp bx, 10
    jb .svc
    cld
    xor ax, ax
    mov [cs:pz_wend], ax
    mov [cs:pz_dry], al
    mov [cs:pz_pad], al
    mov [cs:pz_badf], al
    mov [cs:pz_trn], ax
    mov [cs:pz_tkind], al
    mov [cs:pz_hplte], al
    mov [cs:pz_tabk], al
    mov [cs:pz_bn], al
    xor si, si                      ; DS:SI = no window yet: the first read
    mov [cs:pz_sp], sp              ; takes one
    call pz_body
    jmp short pz_ret

; pz_bad - `damaged`, with the bit count banked for pz_fail's question
pz_bad:
    mov [cs:pz_bn], ch
    mov ax, PXD_DATA
    ; fall into pz_fail

; pz_fail - AX = a PXD_*: out of the decode from any depth. A refusal made
; after the decode has USED a zero byte read past the image data's end is
; `cut short` instead, which is where a reader that stops there stops
pz_fail:
    ; STKBALANCE-OK: the decode's way out from any depth - SP is put back to
    ; pz_decode's own frame from [pz_sp], so what was pushed between there
    ; and here is abandoned, task_exit's shape; pz_ret's retf is pz_decode's
    mov sp, [cs:pz_sp]
    cmp ax, PXD_ABORT
    je .x
    call pz_padused
    jnc .x
    mov ax, PXD_TRUNC
.x:
    stc
pz_ret:
    pop ds
    pop bp
    retf

; pz_padused - CF = 1 when a pad byte has been consumed: the bits left
; ([pz_bn]) are fewer than the pad bits appended. Preserves all
pz_padused:
    push ax
    push cx
    mov al, [cs:pz_pad]
    or al, al
    jz .no
    mov cl, 3
    shl al, cl
    cmp [cs:pz_bn], al
    jb .yes
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop cx
    pop ax
    ret

; pz_done - the last row is in (from inside an extraction)
pz_done:
    ; STKBALANCE-OK: pz_fail's shape - reached from inside an extraction
    ; inside inflate, it puts SP back to pz_decode's frame from [pz_sp]
    mov sp, [cs:pz_sp]
    call pz_padused
    jc .trunc
    cmp byte [cs:pz_badf], 0        ; a filter type past 4, said only now:
    jne .data                       ; a stream error before it wins
    clc
    jmp short pz_ret
.trunc:
    mov ax, PXD_TRUNC
    stc
    jmp short pz_ret
.data:
    mov ax, PXD_DATA
    stc
    jmp short pz_ret

; =============================================================================
; THE BODY: the chunks, then inflate
; =============================================================================
pz_body:
    mov ax, 8                       ; the signature
    xor dx, dx
    call pz_skip
.chunk:
    mov di, pz_chk                  ; length and type
    mov cx, 8
.h:
    call pz_rawb
    mov [cs:di], al
    inc di
    loop .h
    mov ax, [cs:pz_chk]             ; the length, big-endian: DX:AX
    xchg al, ah
    mov dx, ax
    mov ax, [cs:pz_chk + 2]
    xchg al, ah
    test dh, 0x80
    jnz .bad
    cmp word [cs:pz_chk + 4], 'ID'
    jne .n1
    cmp word [cs:pz_chk + 6], 'AT'
    je .idat
.n1:
    cmp word [cs:pz_chk + 4], 'IE'
    jne .n2
    cmp word [cs:pz_chk + 6], 'ND'
    je .bad
.n2:
    cmp word [cs:pz_chk + 4], 'PL'
    jne .n3
    cmp word [cs:pz_chk + 6], 'TE'
    jne .n3
    cmp byte [cs:pz_ct], 3
    jne .skip
    call pz_plte
    jmp short .crc
.n3:
    cmp word [cs:pz_chk + 4], 'tR'
    jne .skip
    cmp word [cs:pz_chk + 6], 'NS'
    jne .skip
    or dx, dx
    jnz .skip
    cmp ax, 256
    ja .skip
    call pz_trns
    jmp short .crc
.skip:
    call pz_skip                    ; DX:AX bytes
.crc:
    mov ax, 4
    xor dx, dx
    call pz_skip
    jmp .chunk
.bad:
    mov ax, PXD_DATA
    jmp pz_fail
.idat:
    mov [cs:pz_left], ax            ; the image data starts here
    mov [cs:pz_left + 2], dx
    call pz_setpal                  ; palette and transparency, K_PAL
    call pz_plan                    ; the passes, the bytes they need
    ; --- inflate's registers ---------------------------------------------
    mov [cs:pz_mark], si
    call pz_limit
    xor dx, dx                      ; the bit buffer, empty
    xor ch, ch
    mov es, [cs:pz_wseg]
    xor di, di
    mov [cs:pz_rd], di
    mov [cs:pz_hav], di
    call pz_budget                  ; BP
    mov cl, 8                       ; zlib: CM 8, CINFO <= 7, the check,
    call pz_bits                    ; no dictionary
    mov bl, al
    mov cl, 8
    call pz_bits
    mov bh, al
    mov al, bl
    and al, 15
    cmp al, 8
    jne .zbad
    cmp bl, 0x7F
    ja .zbad
    test bh, 0x20
    jnz .zbad
    mov ah, bl
    mov al, bh
    push dx
    xor dx, dx
    push cx
    mov cx, 31
    div cx
    pop cx
    or dx, dx
    pop dx
    jnz .zbad
    jmp pz_inflate
.zbad:
    jmp pz_bad

; pz_plte - PLTE of DX:AX bytes into [px_spal], the rest black. 1..256
; entries or `damaged`
pz_plte:
    or dx, dx
    jnz .bad
    or ax, ax
    jz .bad
    cmp ax, 768
    ja .bad
    push ax
    xor dx, dx
    mov cx, 3
    div cx
    or dx, dx
    pop cx                          ; CX = the bytes
    jnz .bad
    mov [cs:pz_npal], ax
    mov byte [cs:pz_hplte], 1
    push es
    push di
    mov es, [cs:pz_pkg]
    mov di, [cs:pz_spal]
    push cx
.b:
    call pz_rawb
    stosb
    loop .b
    pop cx
    neg cx
    add cx, 768
    xor al, al
    rep stosb
    pop di
    pop es
    ret
.bad:
    mov ax, PXD_DATA
    jmp pz_fail

; pz_trns - tRNS of AX (<= 256) bytes into [pz_trns]
pz_trns:
    mov [cs:pz_trn], ax
    mov cx, ax
    xor di, di
    jcxz .out
.b:
    call pz_rawb
    mov [cs:pz_trnb + di], al
    inc di
    loop .b
.out:
    ret

; pz_setpal - at the first IDAT: a palette picture has one (or is damaged),
; and its tRNS alphas are composited into it; a grey or RGB tRNS of the
; right length is a key. Then K_PAL builds the tables (SPEC.md 106.18)
pz_setpal:
    push es
    push di
    mov es, [cs:pz_pkg]
    cmp byte [cs:pz_ct], 3
    jne .key
    cmp byte [cs:pz_hplte], 0
    jne .p
    pop di
    pop es
    mov ax, PXD_DATA                ; a palette picture with no palette
    jmp pz_fail
.p:
    mov bx, [cs:pz_ctx]
    mov ax, [cs:pz_npal]
    mov [es:bx + PXK_NPAL], ax
    mov cx, [cs:pz_trn]             ; min(entries, alphas)
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    jcxz .pal
    mov di, [cs:pz_spal]
    xor bx, bx
.e:
    mov ah, [cs:pz_trnb + bx]       ; this entry's alpha
    mov al, [es:di]
    call pz_blend
    stosb
    mov ah, [cs:pz_trnb + bx]
    mov al, [es:di]
    call pz_blend
    stosb
    mov ah, [cs:pz_trnb + bx]
    mov al, [es:di]
    call pz_blend
    stosb
    inc bx
    loop .e
    jmp short .pal
.key:
    cmp byte [cs:pz_ct], 0
    jne .rgb
    cmp word [cs:pz_trn], 2
    jne .pal
    mov ax, [cs:pz_trnb]
    xchg al, ah
    mov [cs:pz_tgrey], ax
    mov byte [cs:pz_tkind], 1
    jmp short .pal
.rgb:
    cmp byte [cs:pz_ct], 2
    jne .pal
    cmp word [cs:pz_trn], 6
    jne .pal
    xor bx, bx
.k:
    mov ax, [cs:pz_trnb + bx]
    xchg al, ah
    mov [cs:pz_trgb + bx], ax
    inc bx
    inc bx
    cmp bx, 6
    jb .k
    mov byte [cs:pz_tkind], 2
.pal:
    pop di
    pop es
    mov bx, PXS_PAL * 4
    call far [cs:pz_svc + bx]
    jnc .ok
    jmp pz_fail
.ok:
    ret

; pz_blend - AL = a channel, AH = its alpha: AL = it over the view background
; (SPEC.md 106.18): t = a c + (255 - a) 85 + 128, (t + (t >> 8)) >> 8.
; Preserves all but AX
pz_blend:
    cmp ah, 255
    je .c
    or ah, ah
    jz .bg
    push bx
    push dx
    mov bl, ah
    mul ah                          ; AX = a c
    mov dx, ax
    mov al, 255
    sub al, bl
    mov ah, PXP_BG
    mul ah                          ; AX = (255 - a) 85
    add ax, dx
    add ax, 128
    mov dl, ah
    xor dh, dh
    add ax, dx
    mov al, ah
    pop dx
    pop bx
.c:
    ret
.bg:
    mov al, PXP_BG
    ret

; =============================================================================
; THE PASSES (Adam7, or the one pass of a plain picture)
; =============================================================================

; pz_plan - every pass's rows and bytes: [pz_remain] = all the image data's
; bytes (filter bytes included), [pz_ptot] = all the rows; the first pass
; set up. Also the parity below 1/1 (PXK_RBLK)
pz_plan:
    push es
    xor ax, ax
    mov [cs:pz_remain], ax
    mov [cs:pz_remain + 2], ax
    mov [cs:pz_ptot], ax
    mov [cs:pz_rdone], ax
    mov byte [cs:pz_pass], 7        ; a plain picture: the table's eighth row
    cmp byte [cs:pz_il], 0
    je .one
    mov byte [cs:pz_pass], 0
    mov al, [cs:pz_scl]             ; Adam7 below 1/1: its odd rows, 2^(s-1)
    or al, al                       ; a block
    jz .one
    mov cl, al
    dec cl
    mov al, 1
    shl al, cl
    mov es, [cs:pz_pkg]
    mov bx, [cs:pz_ctx]
    mov [es:bx + PXK_RBLK], al
.one:
    mov al, [cs:pz_pass]
    mov [cs:pz_p0], al
.p:
    call pz_pdims                   ; this pass's pw, ph, prb
    mov ax, [cs:pz_prb]
    inc ax
    mul word [cs:pz_ph]
    add [cs:pz_remain], ax
    adc [cs:pz_remain + 2], dx
    mov ax, [cs:pz_ph]
    add [cs:pz_ptot], ax
    cmp byte [cs:pz_pass], 7
    jae .done
    inc byte [cs:pz_pass]
    cmp byte [cs:pz_pass], 7
    jb .p
.done:
    mov al, [cs:pz_p0]              ; ...and back to the first
    mov [cs:pz_pass], al
    call pz_pstart
    pop es
    ret

; pz_pdims - pass [pz_pass]: [pz_x0] [pz_y0] [pz_dxs] [pz_dys] (shifts),
; [pz_pw] [pz_ph] (0 when empty), [pz_prb]. Preserves all
pz_pdims:
    push ax
    push bx
    push cx
    mov bl, [cs:pz_pass]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov al, [cs:pz_a7 + bx]
    xor ah, ah
    mov [cs:pz_x0], ax
    mov al, [cs:pz_a7 + bx + 1]
    mov [cs:pz_y0], ax
    mov al, [cs:pz_a7 + bx + 2]
    mov [cs:pz_dxs], al
    mov al, [cs:pz_a7 + bx + 3]
    mov [cs:pz_dys], al
    mov ax, [cs:pz_w]               ; pw = (w - x0 + dx - 1) >> dxs
    mov bx, [cs:pz_x0]
    mov cl, [cs:pz_dxs]
    call pz_pcount
    mov [cs:pz_pw], ax
    mov ax, [cs:pz_h]
    mov bx, [cs:pz_y0]
    mov cl, [cs:pz_dys]
    call pz_pcount
    mov [cs:pz_ph], ax
    cmp word [cs:pz_pw], 0          ; an empty pass has no rows at all
    jne .r
    mov word [cs:pz_ph], 0
.r:
    mov ax, [cs:pz_pw]
    call pz_rowbytes                ; never more than the full row's
    mov [cs:pz_prb], ax
    pop cx
    pop bx
    pop ax
    ret

; pz_pcount - AX = a size, BX = the pass's start, CL = its step's shift:
; AX = the pass's count. Preserves all but AX
pz_pcount:
    cmp ax, bx
    jbe .zero
    push dx
    sub ax, bx
    mov dx, 1
    shl dx, cl
    dec dx
    add ax, dx
    shr ax, cl
    pop dx
    ret
.zero:
    xor ax, ax
    ret

; pz_pstart - pass [pz_pass], or the next one that has rows: set up, its
; previous row zeroed, the filter byte wanted. CF = 1 when there is none.
; Preserves all
pz_pstart:
    push ax
    push cx
    push di
    push es
.l:
    call pz_pdims
    cmp word [cs:pz_ph], 0
    jne .go
    cmp byte [cs:pz_pass], 6
    jae .none
    inc byte [cs:pz_pass]
    jmp short .l
.go:
    mov word [cs:pz_j], 0
    mov byte [cs:pz_ftwant], 1
    mov word [cs:pz_cur], 8         ; the two rows, eight zero bytes before
    mov ax, [cs:pz_rowb]            ; each: the guards are never written
    add ax, 16
    mov [cs:pz_prev], ax
    mov es, [cs:pz_xseg]
    xor di, di
    mov cx, ax
    add cx, [cs:pz_rowb]            ; both rows and both guards
    xor al, al
    rep stosb
    clc
    jmp short .out
.none:
    stc
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

; =============================================================================
; THE INPUT: the ring's windows (K_NEXT), and the IDAT chunks in them
; =============================================================================

; pz_next - the next window into DS:SI, its end into [pz_wend]; CF = 1 at
; the end of the file. Preserves all else
pz_next:
    push ax
    push bx
    push cx
    push es
    mov bx, PXS_NEXT * 4
    call far [cs:pz_svc + bx]       ; ES:SI, CX
    jc .out
    mov ax, es
    mov ds, ax
    add cx, si
    mov [cs:pz_wend], cx
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; pz_rawb - AL = the next byte of the file; its end is `cut short`
pz_rawb:
    cmp si, [cs:pz_wend]
    jb .ok
    call pz_next
    jnc pz_rawb
    mov ax, PXD_TRUNC
    jmp pz_fail
.ok:
    lodsb
    ret

; pz_skip - past DX:AX bytes of the file; its end is `cut short`.
; Preserves all but AX, DX
pz_skip:
    push bx
.l:
    mov bx, [cs:pz_wend]
    sub bx, si                      ; this window's bytes
    or dx, dx
    jnz .over
    cmp ax, bx
    jbe .fin
.over:
    sub ax, bx
    sbb dx, 0
    mov si, [cs:pz_wend]
    call pz_next
    jnc .l
    mov ax, PXD_TRUNC
    jmp pz_fail
.fin:
    add si, ax
    pop bx
    ret

; pz_limit - [pz_inlim]: where the fast path stops - the window's end or
; the chunk's, whichever is first. Preserves all
pz_limit:
    push ax
    mov ax, [cs:pz_wend]
    sub ax, si
    cmp word [cs:pz_left + 2], 0
    jne .set
    cmp ax, [cs:pz_left]
    jbe .set
    mov ax, [cs:pz_left]
.set:
    add ax, si
    mov [cs:pz_inlim], ax
    pop ax
    ret

; pz_byte - AL = the next byte of the image data. Preserves all but AL
pz_byte:
    cmp si, [cs:pz_inlim]
    jae pz_inslow
    lodsb
    ret

; pz_inslow - the slow half of pz_byte: a window or a chunk has ended. The
; IDATs that follow one another are the data; anything else (or the file's
; end) is its end, past which the data reads as zero bytes - at most four
; (SPEC.md 106.18). Preserves all but AL, DS, SI
pz_inslow:
    cmp byte [cs:pz_dry], 0
    jne .pad
    push ax
.again:
    mov ax, si                      ; what the fast path took from the chunk
    sub ax, [cs:pz_mark]
    sub [cs:pz_left], ax
    sbb word [cs:pz_left + 2], 0
    mov [cs:pz_mark], si
    mov ax, [cs:pz_left]
    or ax, [cs:pz_left + 2]
    jnz .inchunk
    push cx                         ; the chunk is over: its CRC, unread,
    mov cx, 4                       ; then the next chunk's header
.crc:
    call pz_rawq
    jc .dryp
    loop .crc
    mov cx, 8
    push di
    mov di, pz_chk
.hd:
    call pz_rawq
    jc .dryd
    mov [cs:di], al
    inc di
    loop .hd
    pop di
    pop cx
    cmp word [cs:pz_chk + 4], 'ID'
    jne .dry
    cmp word [cs:pz_chk + 6], 'AT'
    jne .dry
    mov ax, [cs:pz_chk]
    xchg al, ah
    test ah, 0x80
    jnz .dry
    mov [cs:pz_left + 2], ax
    mov ax, [cs:pz_chk + 2]
    xchg al, ah
    mov [cs:pz_left], ax
    mov [cs:pz_mark], si
    jmp short .again                ; (a zero-length IDAT: the next)
.inchunk:
    cmp si, [cs:pz_wend]
    jb .lim
    call pz_next
    jc .dry
    mov [cs:pz_mark], si
.lim:
    call pz_limit
    pop ax
    lodsb
    ret
.dryd:
    pop di
.dryp:
    pop cx
.dry:
    pop ax
    mov byte [cs:pz_dry], 1
    mov word [cs:pz_inlim], 0       ; every byte from now is this path's
.pad:
    inc byte [cs:pz_pad]
    cmp byte [cs:pz_pad], 4
    ja .over
    xor al, al
    ret
.over:
    mov ax, PXD_TRUNC
    jmp pz_fail

; pz_rawq - AL = the next byte of the file, CF = 1 at its end (no refusal:
; the chunk walk past the image data only ever ENDS it). Preserves all but AL
pz_rawq:
    cmp si, [cs:pz_wend]
    jb .ok
    call pz_next
    jnc pz_rawq
    ret
.ok:
    lodsb
    clc
    ret

; =============================================================================
; BITS (LSB first): DX holds CH of them
; =============================================================================

; pz_bits - CL = n (0..8): AX = the next n bits. Preserves all but AX, DX,
; CH (the buffer)
pz_bits:
    cmp ch, cl
    jae .have
    push cx
    call pz_byte
    pop cx
    push cx
    mov cl, ch
    xor ah, ah
    shl ax, cl
    or dx, ax
    pop cx
    add ch, 8
    jmp short pz_bits
.have:
    push bx
    mov bl, cl
    xor bh, bh
    shl bx, 1
    mov ax, [cs:pz_masks + bx]
    and ax, dx
    shr dx, cl
    sub ch, cl
    pop bx
    ret

%macro PZ_FILL9 0                   ; at least nine bits in DX
%%l:
    cmp ch, 9
    jae %%ok
    cmp si, [cs:pz_inlim]
    jae %%s
    lodsb
%%p:
    mov cl, ch
    xor ah, ah
    shl ax, cl
    or dx, ax
    add ch, 8
    jmp short %%l
%%s:
    call pz_inslow
    jmp short %%p
%%ok:
%endmacro

; pz_sub - AL = a sub-table's number, BX = the table's sub-tables: the nine
; bits taken, the next six index the sub-table. out AX = the symbol (256 + n
; for a length entry); no code there is `damaged`, after the fifteen bits the
; canonical walk would have read (SPEC.md 106.18). Preserves all but AX, DX,
; CH (the buffer)
pz_sub:
    mov cl, 9
    shr dx, cl
    sub ch, cl
    push ax
    call pz_fill9
    pop ax
    xor ah, ah
    mov cl, 7
    shl ax, cl                      ; 128 bytes a sub-table
    add bx, ax
    mov ax, dx
    and ax, 63
    shl ax, 1
    add bx, ax
    mov ax, [es:bx]
    or ah, ah
    jz .bad
    mov cl, ah
    and cl, 0x0F
    shr dx, cl
    sub ch, cl
    test ah, 0x80
    mov ah, 0
    jz .out
    inc ah                          ; a length entry: 256 + AL
.out:
    ret
.bad:
    mov cl, 6
    call pz_bits
    jmp pz_bad

; pz_fill9 - at least nine bits in DX (PZ_FILL9, called). Preserves all but
; AX, CL, DX, CH
pz_fill9:
    PZ_FILL9
    ret

; pz_walk - BX = a code's count array (its symbols 32 bytes on): the
; CANONICAL WALK, a bit at a time from the first (puff's loop) - the codes
; longer than the primary table's nine bits, and a code no set contains
; (`damaged`). out AX = the symbol. Preserves all but AX, DX, CH
pz_walk:
    push bx
    push di
    push bp
    xor di, di                      ; DI = the code so far
    xor bp, bp                      ; BP = the first code of this length
    mov word [cs:pz_widx], 0        ; ...and its first symbol's index
    mov byte [cs:pz_wlen], 1
.l:
    mov cl, 1
    call pz_bits
    or di, ax
    mov al, [cs:pz_wlen]
    xor ah, ah
    shl ax, 1
    push bx
    add bx, ax
    mov ax, [es:bx]                 ; the codes of this length
    pop bx
    mov [cs:pz_wcnt], ax
    mov ax, di
    sub ax, bp
    cmp ax, [cs:pz_wcnt]
    jb .found
    mov ax, [cs:pz_wcnt]
    add [cs:pz_widx], ax
    add bp, ax
    shl bp, 1
    shl di, 1
    inc byte [cs:pz_wlen]
    cmp byte [cs:pz_wlen], 15
    jbe .l
    pop bp
    pop di
    pop bx
    jmp pz_bad
.found:
    add ax, [cs:pz_widx]
    shl ax, 1
    add ax, bx
    add ax, 32
    mov bx, ax
    mov ax, [es:bx]
    pop bp
    pop di
    pop bx
    ret

; =============================================================================
; INFLATE
; =============================================================================
pz_inflate:
.block:
    mov cl, 1
    call pz_bits
    mov [cs:pz_final], al
    mov cl, 2
    call pz_bits
    cmp al, 1
    je .fixed
    cmp al, 2
    je .dyn
    or al, al
    jz .stored
    jmp pz_bad                      ; block type 3
.fixed:
    cmp byte [cs:pz_tabk], 1
    je .sym
    call pz_fixed
    mov byte [cs:pz_tabk], 1
    jmp short .sym
.dyn:
    call pz_dynhdr
    mov byte [cs:pz_tabk], 2
    jmp short .sym
.stored:
    call pz_stored
    jmp .eob
    ; --- the symbol loop -------------------------------------------------
.sym:
    PZ_FILL9
    mov bx, dx
    and bh, 1
    shl bx, 1
    mov ax, [es:bx + PZ_LPRIM]
    or ah, ah
    jle .lother                     ; a literal is AH > 0: its length
    mov cl, ah
    shr dx, cl
    sub ch, cl
.lit:
    stosb
    and di, 0x7FFF
    dec bp
    jnz .sym
    call pz_extract
    jmp short .sym
.lother:
    jz .lwalk
    cmp ah, 0xC0
    jae .lsub
    mov cl, ah                      ; a length symbol, or the block's end
    and cl, 0x0F
    shr dx, cl
    sub ch, cl
    or al, al
    jz .eob
    jmp short .len
.lsub:
    mov bx, PZ_LSUB
    call pz_sub
    jmp short .lsym
.lwalk:
    mov bx, PZ_LCNT
    call pz_walk
.lsym:
    cmp ax, 256
    jb .lit
    je .eob
    sub ax, 256
.len:                               ; AL = the symbol - 256, 1..31
    cmp al, 30
    jb .l29
    jmp pz_bad                      ; 286 and 287
.l29:
    dec al
    xor ah, ah
    mov bx, ax
    mov cl, [cs:pz_lext + bx]
    shl bx, 1
    mov ax, [cs:pz_lbase + bx]
    or cl, cl
    jz .lfix
    push ax
    call pz_bits
    pop bx
    add ax, bx
.lfix:
    mov [cs:pz_mlen], ax
    PZ_FILL9                        ; --- the distance ---
    mov bx, dx
    and bh, 1
    shl bx, 1
    mov ax, [es:bx + PZ_DPRIM]
    or ah, ah
    jle .dother
    mov cl, ah
    shr dx, cl
    sub ch, cl
    xor ah, ah
    jmp short .dsym
.dother:
    jz .dwalk
    mov bx, PZ_DSUB
    call pz_sub
    jmp short .dsym
.dwalk:
    mov bx, PZ_DCNT
    call pz_walk
.dsym:
    cmp ax, 30
    jb .d29
    jmp pz_bad                      ; 30 and 31
.d29:
    mov bx, ax
    mov cl, [cs:pz_dext + bx]
    shl bx, 1
    mov ax, [cs:pz_dbase + bx]
    or cl, cl
    jz .dist
    cmp cl, 8
    jbe .d8
    push ax                         ; 9..13 bits: eight, then the rest
    mov [cs:pz_tmp], cl
    mov cl, 8
    call pz_bits
    mov bx, ax
    mov cl, [cs:pz_tmp]
    sub cl, 8
    call pz_bits
    mov ah, al
    xor al, al
    or ax, bx
    pop bx
    add ax, bx
    jmp short .dist
.d8:
    push ax
    call pz_bits
    pop bx
    add ax, bx
.dist:                              ; AX = the distance: not before the first
    cmp word [cs:pz_hav], 0x8000    ; byte the stream wrote
    jae .copy
    mov bx, di
    sub bx, [cs:pz_rd]
    and bx, 0x7FFF
    add bx, [cs:pz_hav]
    cmp ax, bx
    jbe .copy
    jmp pz_bad
.copy:
    call pz_copy
    mov ax, [cs:pz_mlen]
    sub bp, ax
    ja .sym2
    call pz_extract
.sym2:
    jmp .sym
.eob:
    cmp byte [cs:pz_final], 0
    jne .short
    jmp .block
.short:
    mov [cs:pz_bn], ch              ; the stream is over and the rows are not
    mov ax, PXD_TRUNC
    jmp pz_fail

; pz_copy - AX = a distance, [pz_mlen] bytes: the window copies itself.
; Preserves all but AX
pz_copy:
    push cx
    push si
    push ds
    mov si, di
    sub si, ax
    and si, 0x7FFF
    mov cx, [cs:pz_mlen]
    push es
    pop ds
    mov ax, si                      ; neither end wraps: one movsb run (a
    add ax, cx                      ; byte at a time, so an overlap repeats
    cmp ax, 0x8000                  ; the string, as deflate means it)
    ja .slow
    mov ax, di
    add ax, cx
    cmp ax, 0x8000
    ja .slow
    rep movsb
    and di, 0x7FFF
    jmp short .out
.slow:
    lodsb
    stosb
    and si, 0x7FFF
    and di, 0x7FFF
    loop .slow
.out:
    pop ds
    pop si
    pop cx
    ret

; pz_stored - a stored block, after its three bits: to the byte, LEN, NLEN,
; and LEN bytes into the window
pz_stored:
    mov cl, ch
    and cl, 7
    call pz_bits                    ; to the byte
    call pz_get16
    mov [cs:pz_slen], ax
    call pz_get16
    not ax
    cmp ax, [cs:pz_slen]
    je .copy
    jmp pz_bad
.copy:
    cmp word [cs:pz_slen], 0
    je .out
    mov cl, 8
    call pz_bits
    stosb
    and di, 0x7FFF
    dec word [cs:pz_slen]
    dec bp
    jnz .copy
    call pz_extract
    jmp short .copy
.out:
    ret

; pz_get16 - AX = sixteen bits
pz_get16:
    mov cl, 8
    call pz_bits
    push ax
    mov cl, 8
    call pz_bits
    mov ah, al
    pop bx
    mov al, bl
    ret

; pz_fixed - the fixed code's two tables (RFC 1951 3.2.6): 288 literal/
; length codes, and 32 distance codes of five bits, 30 and 31 refused when
; used. Preserves all but AX, BX
pz_fixed:
    push cx
    push di
    mov di, PZ_LENS
    mov al, 8
    mov cx, 144
    rep stosb
    mov al, 9
    mov cx, 112
    rep stosb
    mov al, 7
    mov cx, 24
    rep stosb
    mov al, 8
    mov cx, 8
    rep stosb
    mov al, 5
    mov cx, 32
    rep stosb
    pop di
    pop cx
    mov [cs:pz_bn], ch
    mov word [cs:pz_bl], PZ_LENS
    mov word [cs:pz_bnum], 288
    call pz_buildl
    mov word [cs:pz_bl], PZ_LENS + 288
    mov word [cs:pz_bnum], 32
    call pz_buildd
    ret

; pz_dynhdr - a dynamic block's header: HLIT, HDIST, HCLEN, the code-length
; code, the lengths, and the two tables
pz_dynhdr:
    mov cl, 5
    call pz_bits
    add ax, 257
    mov [cs:pz_nlen], ax
    mov cl, 5
    call pz_bits
    inc ax
    mov [cs:pz_ndist], ax
    mov cl, 4
    call pz_bits
    add ax, 4
    mov [cs:pz_ncode], ax
    cmp word [cs:pz_nlen], 286
    ja .bad
    cmp word [cs:pz_ndist], 30
    ja .bad
    push di                         ; the code-length code's lengths, in
    mov di, PZ_CLL                  ; their order
    push cx
    mov cx, 19
    xor al, al
    rep stosb
    pop cx
    xor bx, bx
.cl:
    push bx
    mov cl, 3
    call pz_bits
    pop bx
    push bx
    mov bl, [cs:pz_clord + bx]
    xor bh, bh
    mov [es:PZ_CLL + bx], al
    pop bx
    inc bx
    cmp bx, [cs:pz_ncode]
    jb .cl
    pop di
    mov [cs:pz_bn], ch
    mov word [cs:pz_bl], PZ_CLL     ; ...built COMPLETE into the distance
    mov word [cs:pz_bnum], 19       ; code's arrays
    mov byte [cs:pz_bcmp], 1
    call pz_buildd
    mov byte [cs:pz_bcmp], 0
    xor bx, bx                      ; BX = lengths read
.len:
    mov ax, [cs:pz_nlen]
    add ax, [cs:pz_ndist]
    cmp bx, ax
    jae .read
    push bx
    PZ_FILL9
    mov bx, dx
    and bh, 1
    shl bx, 1
    mov ax, [es:bx + PZ_DPRIM]
    or ah, ah
    jle .clslow                     ; (seven bits at most: never a sub-table)
    mov cl, ah
    shr dx, cl
    sub ch, cl
    xor ah, ah
    jmp short .clsym
.clslow:
    mov bx, PZ_DCNT
    call pz_walk
.clsym:
    pop bx
    cmp al, 16
    jae .rep
    mov [es:PZ_LENS + bx], al
    inc bx
    jmp short .len
.rep:
    jne .z
    or bx, bx                       ; 16: the previous length, 3..6 times
    jz .bad
    push bx
    mov cl, 2
    call pz_bits
    pop bx
    add ax, 3
    push ax
    mov al, [es:PZ_LENS + bx - 1]
    mov [cs:pz_rv], al
    pop ax
    jmp short .fill
.z:
    mov byte [cs:pz_rv], 0
    push bx
    cmp al, 17
    jne .z18
    mov cl, 3                       ; 17: zeros, 3..10
    call pz_bits
    add ax, 3
    jmp short .z1
.z18:
    mov cl, 7                       ; 18: zeros, 11..138
    call pz_bits
    add ax, 11
.z1:
    pop bx
.fill:                              ; AX repeats of [pz_rv], within bounds
    push ax
    add ax, bx
    mov [cs:pz_rt], ax
    mov ax, [cs:pz_nlen]
    add ax, [cs:pz_ndist]
    cmp [cs:pz_rt], ax
    pop ax
    ja .bad
    push cx
    mov cx, ax
    mov al, [cs:pz_rv]
.f:
    mov [es:PZ_LENS + bx], al
    inc bx
    loop .f
    pop cx
    jmp .len
.bad:
    jmp pz_bad
.read:
    mov bx, [cs:pz_nlen]            ; the end-of-block code must exist
    cmp byte [es:PZ_LENS + 256], 0
    je .bad
    mov [cs:pz_bn], ch
    mov word [cs:pz_bl], PZ_LENS
    mov [cs:pz_bnum], bx
    call pz_buildl
    add bx, PZ_LENS
    mov [cs:pz_bl], bx
    mov ax, [cs:pz_ndist]
    mov [cs:pz_bnum], ax
    call pz_buildd
    ret

; pz_buildl / pz_buildd - the literal/length or the distance tables from
; [pz_bnum] lengths at ES:[pz_bl]. `damaged` (pz_fail) when the set is
; over-subscribed, or incomplete where that is not allowed. Preserve all
pz_buildl:
    push ax
    mov word [cs:pz_bprim], PZ_LPRIM
    mov word [cs:pz_bcnt], PZ_LCNT
    mov word [cs:pz_bsub], PZ_LSUB
    mov byte [cs:pz_bsubn], PZ_LNSUB
    mov byte [cs:pz_blit], 0x80
    jmp short pz_build
pz_buildd:
    push ax
    mov word [cs:pz_bprim], PZ_DPRIM
    mov word [cs:pz_bcnt], PZ_DCNT
    mov word [cs:pz_bsub], PZ_DSUB
    mov byte [cs:pz_bsubn], PZ_DNSUB
    mov byte [cs:pz_blit], 0
pz_build:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    pop ds                          ; DS = ES = the scratch's window segment
    mov di, [cs:pz_bcnt]            ; --- how many codes of each length
    xor ax, ax
    mov cx, 16
    rep stosw
    mov si, [cs:pz_bl]
    mov cx, [cs:pz_bnum]
.c:
    lodsb
    xor ah, ah
    mov bx, ax
    shl bx, 1
    add bx, [cs:pz_bcnt]
    inc word [bx]
    loop .c
    mov bx, [cs:pz_bcnt]            ; --- over-subscribed, or incomplete?
    mov ax, 1                       ; AX = the codes left at this length
    xor dx, dx                      ; DX = the codes used
    mov cx, 15
.k:
    add bx, 2
    shl ax, 1
    sub ax, [bx]
    jb .bad
    add dx, [bx]
    loop .k
    or ax, ax
    jz .ok
    cmp byte [cs:pz_bcmp], 0        ; the code-length code is complete
    jne .bad
    or dx, dx                       ; ...the others may be empty, or one
    jz .ok                          ; code of one bit
    cmp dx, 1
    jne .bad
    mov bx, [cs:pz_bcnt]
    cmp word [bx + 2], 1
    je .ok
.bad:
    pop ds
    mov ax, PXD_DATA
    jmp pz_fail
.ok:
    mov bx, [cs:pz_bcnt]            ; --- offsets: where each length's
    mov word [PZ_OFFS + 2], 0       ; symbols start
    mov cx, 14
    mov si, 2
.o:
    mov ax, [PZ_OFFS + si]
    add ax, [bx + si]
    mov [PZ_OFFS + si + 2], ax
    add si, 2
    loop .o
    mov si, [cs:pz_bl]              ; --- the symbols, sorted by length
    xor dx, dx                      ; DX = the symbol
.s:
    cmp dx, [cs:pz_bnum]
    jae .prim
    lodsb
    or al, al
    jz .sn
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov di, [PZ_OFFS + bx]
    inc word [PZ_OFFS + bx]
    shl di, 1
    add di, [cs:pz_bcnt]
    mov [di + 32], dx
.sn:
    inc dx
    jmp short .s
.prim:
    mov di, [cs:pz_bprim]           ; --- the primary table: nine bits
    xor ax, ax
    mov cx, 512
    rep stosw
    xor si, si                      ; SI = the symbol index (x 2)
    xor dx, dx                      ; DX = the code
    mov byte [cs:pz_blen], 1
.len:
    mov bl, [cs:pz_blen]
    xor bh, bh
    shl bx, 1
    add bx, [cs:pz_bcnt]
    mov cx, [bx]                    ; CX = the codes of this length
    jcxz .nextl
.code:
    push cx
    mov bx, si                      ; the symbol
    add bx, [cs:pz_bcnt]
    mov ax, [bx + 32]
    mov ah, [cs:pz_blen]            ; the entry: the length...
    cmp word [bx + 32], 256
    jb .e
    or ah, [cs:pz_blit]             ; ...and a literal/length table's flag
.e:
    mov bp, ax                      ; BP = the entry
    mov cl, [cs:pz_blen]            ; the code, bit-reversed: the stream's
    mov ax, dx                      ; order
    xor bx, bx
.rv:
    shr ax, 1
    rcl bx, 1
    dec cl
    jnz .rv
    mov cl, [cs:pz_blen]
    mov ax, 1
    shl ax, cl                      ; AX = the step: 1 << length
    shl ax, 1
    shl bx, 1
    add bx, [cs:pz_bprim]
    mov cx, [cs:pz_bprim]
    add cx, 1024                    ; CX = the table's end
.f:
    mov [bx], bp
    add bx, ax
    cmp bx, cx
    jb .f
    inc dx
    inc si
    inc si
    pop cx
    loop .code
.nextl:
    shl dx, 1
    inc byte [cs:pz_blen]
    cmp byte [cs:pz_blen], 9
    jbe .len
    ; --- codes of ten bits and more: a sub-table each nine-bit prefix ------
    mov byte [cs:pz_bnsub], 0
.len2:
    mov bl, [cs:pz_blen]
    xor bh, bh
    shl bx, 1
    add bx, [cs:pz_bcnt]
    mov cx, [bx]
    or cx, cx
    jnz .code2
    jmp .next2
.code2:
    push cx
    mov bx, si                      ; the entry: the bits past nine...
    add bx, [cs:pz_bcnt]
    mov ax, [bx + 32]
    mov ah, [cs:pz_blen]
    sub ah, 9
    cmp word [bx + 32], 256
    jb .e2
    or ah, [cs:pz_blit]             ; ...and a length symbol's flag
.e2:
    mov bp, ax
    mov cl, [cs:pz_blen]            ; the prefix: the code's first nine bits,
    sub cl, 9                       ; in the stream's order
    mov ax, dx
    shr ax, cl
    xor bx, bx
    mov cl, 9
.r9:
    shr ax, 1
    rcl bx, 1
    dec cl
    jnz .r9
    shl bx, 1
    add bx, [cs:pz_bprim]
    mov ax, [bx]
    cmp ah, 0xC0
    je .have
    or ax, ax
    jnz .skip
    mov al, [cs:pz_bnsub]           ; a new sub-table - or, when they are all
    cmp al, [cs:pz_bsubn]           ; spent, this prefix is the walk's
    jae .skip
    inc byte [cs:pz_bnsub]
    mov ah, 0xC0
    mov [bx], ax
    push ax
    push di
    xor ah, ah
    mov cl, 7
    shl ax, cl
    add ax, [cs:pz_bsub]
    mov di, ax
    mov cx, 64
    xor ax, ax
    rep stosw
    pop di
    pop ax
.have:
    xor ah, ah                      ; this code's sub-table
    mov cl, 7
    shl ax, cl
    add ax, [cs:pz_bsub]
    mov [cs:pz_bsb], ax
    mov cl, [cs:pz_blen]            ; its bits past nine, reversed
    sub cl, 9
    mov ax, 1
    shl ax, cl
    dec ax
    and ax, dx
    xor bx, bx
    mov ch, cl
.rv2:
    shr ax, 1
    rcl bx, 1
    dec ch
    jnz .rv2
    mov ax, 2                       ; the step, in bytes: 2 << (len - 9)
    shl ax, cl
    shl bx, 1
    add bx, [cs:pz_bsb]
    mov cx, [cs:pz_bsb]
    add cx, 128
.f2:
    mov [bx], bp
    add bx, ax
    cmp bx, cx
    jb .f2
.skip:
    inc dx
    inc si
    inc si
    pop cx
    dec cx
    jz .next2
    jmp .code2
.next2:
    shl dx, 1
    inc byte [cs:pz_blen]
    cmp byte [cs:pz_blen], 15
    jbe .len2
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; EXTRACTION: the window's new bytes into scanlines (SPEC.md 106.18)
; =============================================================================

; pz_budget - BP = the bytes before the next extraction: a batch, or what
; the rows still want if that is fewer. Preserves all but BP
pz_budget:
    mov bp, PZ_BATCH
    cmp word [cs:pz_remain + 2], 0
    jne .out
    cmp bp, [cs:pz_remain]
    jbe .out
    mov bp, [cs:pz_remain]
.out:
    ret

; pz_extract - from inflate: every byte written since the last extraction
; goes into the scanline being gathered, a whole scanline is decoded and
; emitted, and the last one ends the decode (pz_done). Preserves all but BP
pz_extract:
    mov [cs:pz_bn], ch
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    mov ax, di                      ; what is new
    sub ax, [cs:pz_rd]
    and ax, 0x7FFF
    mov [cs:pz_pend], ax
    cmp word [cs:pz_hav], 0x8000    ; ...and the window's fill
    jae .l
    add [cs:pz_hav], ax
    cmp word [cs:pz_hav], 0x8000
    jbe .l
    mov word [cs:pz_hav], 0x8000
.l:
    mov ax, [cs:pz_pend]
    or ax, ax
    jz .fin
    cmp byte [cs:pz_ftwant], 0
    je .data
    mov bx, [cs:pz_rd]              ; a scanline's filter byte
    mov al, [es:bx]
    mov [cs:pz_ft], al
    inc bx
    and bx, 0x7FFF
    mov [cs:pz_rd], bx
    dec word [cs:pz_pend]
    mov byte [cs:pz_ftwant], 0
    mov word [cs:pz_fill], 0
    sub word [cs:pz_remain], 1
    sbb word [cs:pz_remain + 2], 0
    jmp short .l
.data:
    mov cx, [cs:pz_prb]             ; this chunk: the new bytes, what the
    sub cx, [cs:pz_fill]            ; row lacks, and the window's run
    cmp ax, cx
    jbe .t1
    mov ax, cx
.t1:
    mov cx, 0x8000
    sub cx, [cs:pz_rd]
    cmp ax, cx
    jbe .t2
    mov ax, cx
.t2:
    mov cx, ax
    push es
    push di
    mov si, [cs:pz_rd]
    mov di, [cs:pz_cur]
    add di, [cs:pz_fill]
    push es
    pop ds
    mov es, [cs:pz_xseg]
    rep movsb
    pop di
    pop es
    and si, 0x7FFF
    mov [cs:pz_rd], si
    add [cs:pz_fill], ax
    sub [cs:pz_pend], ax
    sub [cs:pz_remain], ax
    sbb word [cs:pz_remain + 2], 0
    mov ax, [cs:pz_fill]
    cmp ax, [cs:pz_prb]
    jb .l
    push es                         ; a whole scanline
    push di
    call pz_row
    pop di
    pop es
    mov byte [cs:pz_ftwant], 1
    jmp .l
.fin:
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    call pz_budget
    ret

; pz_row - the scanline at [pz_cur] in the row segment: unfiltered, made
; into the emitter's format, emitted, and the next row (or pass, or the
; end) set up. Clobbers AX, BX, CX, DX, SI, DI, ES, DS, BP
pz_row:
    mov ds, [cs:pz_xseg]
    push ds
    pop es
    call pz_unfilter
    call pz_out                     ; to the emitter (or progress only)
    mov ax, [cs:pz_cur]             ; this row is the next one's previous
    xchg ax, [cs:pz_prev]
    mov [cs:pz_cur], ax
    inc word [cs:pz_rdone]
    inc word [cs:pz_j]
    mov ax, [cs:pz_j]
    cmp ax, [cs:pz_ph]
    jb .ret
    cmp byte [cs:pz_pass], 6        ; the pass is done: the next with rows
    jae .end
    inc byte [cs:pz_pass]
    call pz_pstart
    jnc .ret
.end:
    jmp pz_done
.ret:
    ret

; pz_unfilter - DS = ES = the row segment: [pz_cur]'s [pz_prb] bytes, with
; [pz_prev] above them. A type past 4 is noted (pz_done says it) and the
; row taken as it is
pz_unfilter:
    mov al, [cs:pz_ft]
    mov bp, [cs:pz_prb]
    mov di, [cs:pz_cur]
    mov si, [cs:pz_prev]
    mov bl, [cs:pz_bpp]
    xor bh, bh
    neg bx                          ; BX = -bpp: the left neighbour
    cmp al, 1
    je .sub
    cmp al, 2
    je .up
    cmp al, 3
    je .avg
    cmp al, 4
    je .paeth
    or al, al
    jz .out
    mov byte [cs:pz_badf], 1
.out:
    ret
.sub:
    mov al, [di + bx]
    add [di], al
    inc di
    dec bp
    jnz .sub
    ret
.up:
    lodsb
    add [di], al
    inc di
    dec bp
    jnz .up
    ret
.avg:
    mov al, [di + bx]               ; (a + b) >> 1, the ninth bit in CF
    add al, [si]
    rcr al, 1
    add [di], al
    inc si
    inc di
    dec bp
    jnz .avg
    ret
.paeth:                             ; a = [di+bx], b = [si], c = [si+bx]
    mov dl, [si + bx]               ; c
    mov al, [di + bx]               ; a
    mov cl, [si]                    ; b
    mov ch, al
    sub cl, dl                      ; b - c: its sign in DH...
    sbb dh, dh
    sub ch, dl                      ; ...a - c: its sign in DL
    sbb dl, dl
    xor cl, dh
    sub cl, dh                      ; CL = pa = |b - c|
    xor ch, dl
    sub ch, dl                      ; CH = pb = |a - c|
    cmp dh, dl
    jne .pmix
    cmp cl, ch                      ; c outside a..b: pc = pa + pb is the
    jbe .pa                         ; largest, so a when pa <= pb, else b
    mov al, [si]
    jmp short .pa
.pmix:                              ; c between them: pc = |pa - pb|
    mov dl, cl
    sub dl, ch
    jnc .pm1
    neg dl
.pm1:
    cmp cl, ch                      ; pa <= pb and pa <= pc: a
    ja .pnota
    cmp cl, dl
    jbe .pa
.pnota:
    cmp ch, dl                      ; pb <= pc: b, else c
    ja .pc
    mov al, [si]
    jmp short .pa
.pc:
    mov al, [si + bx]
.pa:
    add [di], al
    inc si
    inc di
    dec bp
    jnz .paeth
    ret

; pz_out - the unfiltered row to the emitter: K_EMIT, K_SCAT for Adam7 at
; 1/1, or progress alone for the passes an interlaced picture below 1/1
; does not show (SPEC.md 106.18)
pz_out:
    mov ax, [cs:pz_j]               ; the row's y: y0 + j << dys
    mov cl, [cs:pz_dys]
    shl ax, cl
    add ax, [cs:pz_y0]
    mov [cs:pz_y], ax
    cmp byte [cs:pz_il], 0
    je .plain
    cmp byte [cs:pz_scl], 0
    je .scat
    cmp byte [cs:pz_pass], 6        ; below 1/1: the seventh pass only
    jae .plain
    call pz_prog
    mov ax, dx
    mov si, PXS_TICK * 4
    jmp short .call
.scat:
    call pz_conv
    xor bx, bx                      ; rows complete from the top: none until
    cmp byte [cs:pz_pass], 6        ; the seventh pass, then 2j + 3
    jb .sb
    mov bx, [cs:pz_j]
    shl bx, 1
    add bx, 3
    cmp bx, [cs:pz_h]
    jbe .sb
    mov bx, [cs:pz_h]
.sb:
    call pz_prog
    mov cl, [cs:pz_dxs]
    mov ch, 1
    shl ch, cl                      ; CH = the column step
    mov cl, [cs:pz_x0]              ; CL = the first column
    mov ax, [cs:pz_y]
    mov si, PXS_SCAT * 4
    jmp short .call
.plain:
    call pz_conv
    mov bx, 0xFFFF
    mov dx, 0xFFFF
    cmp byte [cs:pz_il], 0
    je .em
    call pz_prog
.em:
    mov ax, [cs:pz_y]
    mov si, PXS_EMIT * 4
.call:
    call far [cs:pz_svc + si]
    jc .ab
    ret
.ab:
    jmp pz_fail

; pz_prog - DX = the progress in source rows: the rows done of all the
; passes' rows, scaled to the picture's height. Preserves all but DX
pz_prog:
    push ax
    push cx
    mov ax, [cs:pz_rdone]
    inc ax
    mul word [cs:pz_h]
    mov cx, [cs:pz_ptot]
    div cx
    mov dx, ax
    pop cx
    pop ax
    ret

; =============================================================================
; PIXELS: the unfiltered scanline (DS:[pz_cur]) into the emitter's row
; buffer (SPEC.md 106.18) - pixel k at column x0 + k << dxs
; =============================================================================
pz_conv:
    push bp
    mov si, [cs:pz_cur]
    mov es, [cs:pz_rseg]
    mov cl, [cs:pz_dxs]
    mov bx, 1
    shl bx, cl                      ; BX = the step in pixels
    mov ax, [cs:pz_x0]
    mov bp, [cs:pz_pw]              ; BP = the pixels
    mov cl, [cs:pz_ct]
    cmp cl, 2                       ; three bytes a pixel for RGB and RGBA
    je .x3
    cmp cl, 6
    jne .x1
.x3:
    mov di, ax
    add di, ax
    add di, ax
    mov ax, bx
    add bx, bx
    add bx, ax
    jmp short .go
.x1:
    mov di, ax
.go:
    mov [cs:pz_step], bx
    mov al, [cs:pz_depth]
    cmp cl, 3
    je .idx
    or cl, cl
    je .grey
    cmp cl, 4
    je .ga
    cmp cl, 2
    je .rgb
    jmp .rgba
    ; --- type 3: indices --------------------------------------------------
.idx:
    cmp al, 8
    jne .bits
    cmp bx, 1
    jne .i8s
    mov cx, bp
    rep movsb
    jmp .out
.i8s:
    dec bx
.i8:
    movsb
    add di, bx
    dec bp
    jnz .i8
    jmp .out
    ; --- 1, 2 and 4 bits: indices, or greys scaled and keyed --------------
.bits:
    mov cl, al                      ; CL = the depth
    mov dl, 1
    shl dl, cl
    dec dl                          ; DL = the sample's mask
    mov dh, 0                       ; DH = samples left in the byte
    push bx
    mov bl, cl
    xor bh, bh
    mov al, [cs:pz_gscale + bx]     ; a grey's scale, by its depth
    mov [cs:pz_gs], al
    pop bx
.b:
    or dh, dh
    jnz .b1
    lodsb
    mov [cs:pz_cb], al              ; the byte the samples come out of
    mov dh, 8
.b1:
    mov al, [cs:pz_cb]
    rol al, cl
    mov [cs:pz_cb], al
    and al, dl
    sub dh, cl
    cmp byte [cs:pz_ct], 3
    je .bput
    cmp byte [cs:pz_tkind], 1       ; a grey: the key, else the scale
    jne .bsc
    cmp al, [cs:pz_tgrey]
    jne .bsc
    cmp byte [cs:pz_tgrey + 1], 0
    jne .bsc
    mov al, PXP_BG
    jmp short .bput
.bsc:
    mul byte [cs:pz_gs]
.bput:
    stosb
    add di, bx
    dec di
    dec bp
    jnz .b
    jmp .out
    ; --- type 0, 8 and 16 bits --------------------------------------------
.grey:
    cmp al, 8
    jb .bits
    ja .g16
    cmp byte [cs:pz_tkind], 1
    je .g8k
    jmp .idx                        ; (8 bits, no key: a straight copy)
.g8k:
    lodsb
    cmp al, [cs:pz_tgrey]
    jne .g8p
    cmp byte [cs:pz_tgrey + 1], 0
    jne .g8p
    mov al, PXP_BG
.g8p:
    stosb
    add di, bx
    dec di
    dec bp
    jnz .g8k
    jmp .out
.g16:
    lodsw                           ; AL = the high byte, AH the low
    cmp byte [cs:pz_tkind], 1
    jne .g16p
    xchg al, ah
    cmp ax, [cs:pz_tgrey]
    xchg al, ah
    jne .g16p
    mov al, PXP_BG
.g16p:
    stosb
    add di, bx
    dec di
    dec bp
    jnz .g16
    jmp .out
    ; --- type 4: grey and alpha --------------------------------------------
.ga:
    cmp al, 8
    jne .ga16
.ga8:
    lodsw                           ; AL = grey, AH = alpha
    call pz_blend
    stosb
    add di, bx
    dec di
    dec bp
    jnz .ga8
    jmp .out
.ga16:
    lodsw
    mov dl, al                      ; the grey's high byte
    lodsw
    mov ah, al                      ; the alpha's
    mov al, dl
    call pz_blend
    stosb
    add di, bx
    dec di
    dec bp
    jnz .ga16
    jmp .out
    ; --- type 2: RGB -----------------------------------------------------------
.rgb:
    sub bx, 3                       ; BX = the step past a pixel's three
    cmp al, 8
    jne .rgb16
    cmp byte [cs:pz_tkind], 2
    je .r8k
    or bx, bx
    jnz .r8s
    mov cx, bp                      ; no key, no step: one copy
    add cx, bp
    add cx, bp
    rep movsb
    jmp .out
.r8s:
    movsb
    movsb
    movsb
    add di, bx
    dec bp
    jnz .r8s
    jmp .out
.r8k:
    xor ax, ax                      ; a key of 8-bit samples: a word whose
    mov al, [si]                    ; high byte is not 0 matches nothing
    cmp ax, [cs:pz_trgb]
    jne .r8n
    mov al, [si + 1]
    cmp ax, [cs:pz_trgb + 2]
    jne .r8n
    mov al, [si + 2]
    cmp ax, [cs:pz_trgb + 4]
    jne .r8n
    add si, 3
    mov al, PXP_BG
    stosb
    stosb
    stosb
    jmp short .r8e
.r8n:
    movsb
    movsb
    movsb
.r8e:
    add di, bx
    dec bp
    jnz .r8k
    jmp .out
.rgb16:
    cmp byte [cs:pz_tkind], 2
    jne .r16n
    mov ax, [si]
    xchg al, ah
    cmp ax, [cs:pz_trgb]
    jne .r16n
    mov ax, [si + 2]
    xchg al, ah
    cmp ax, [cs:pz_trgb + 2]
    jne .r16n
    mov ax, [si + 4]
    xchg al, ah
    cmp ax, [cs:pz_trgb + 4]
    jne .r16n
    add si, 6
    mov al, PXP_BG
    stosb
    stosb
    stosb
    jmp short .r16e
.r16n:
    lodsw
    stosb
    lodsw
    stosb
    lodsw
    stosb
.r16e:
    add di, bx
    dec bp
    jnz .rgb16
    jmp short .out
    ; --- type 6: RGBA ----------------------------------------------------------
.rgba:
    sub bx, 3
    mov dl, 1                       ; DX = a sample's bytes
    cmp al, 8
    je .ra0
    mov dl, 2
.ra0:
    xor dh, dh
.ra:
    push bx
    mov bx, dx                      ; the alpha: three samples on
    add bx, dx
    add bx, dx
    mov al, [si + bx]
    pop bx
    mov [cs:pz_al], al
    mov ah, al
    mov al, [si]
    call pz_blend
    stosb
    add si, dx
    mov ah, [cs:pz_al]
    mov al, [si]
    call pz_blend
    stosb
    add si, dx
    mov ah, [cs:pz_al]
    mov al, [si]
    call pz_blend
    stosb
    add si, dx
    add si, dx                      ; past the alpha
    add di, bx
    dec bp
    jnz .ra
.out:
    pop bp
    ret

; --- the length and distance codes' bases and extra bits (RFC 1951 3.2.5) ---
pz_lbase:   dw 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31
            dw 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
pz_lext:    db 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2
            db 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
pz_dbase:   dw 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193
            dw 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145
            dw 8193, 12289, 16385, 24577
pz_dext:    db 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6
            db 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
pz_clord:   db 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
pz_masks:   dw 0, 1, 3, 7, 15, 31, 63, 127, 255
pz_gscale:  db 0, 255, 85, 0, 17     ; a 1/2/4-bit grey's scale, by depth
; Adam7's seven passes and the plain picture's one: x0, y0, the steps' shifts
pz_a7:      db 0, 0, 3, 3
            db 4, 0, 3, 3
            db 0, 4, 2, 3
            db 2, 0, 2, 2
            db 0, 2, 1, 2
            db 1, 0, 1, 1
            db 0, 1, 0, 1
            db 0, 0, 0, 0

; --- this part's own memory (rule 1) ---------------------------------------------
pz_ctx:     dw 0
pz_pkg:     dw 0
pz_sp:      dw 0
pz_svc:     times 10 dw 0
pz_scl:     db 0
pz_rseg:    dw 0                    ; the emitter's row buffer
pz_wseg:    dw 0                    ; the window and the tables
pz_xseg:    dw 0                    ; the two scanlines
pz_spal:    dw 0
pz_w:       dw 0                    ; the header's (HEAD to DECODE: the part
pz_h:       dw 0                    ; stays fetched between the two)
pz_depth:   db 0
pz_ct:      db 0
pz_ch:      db 0
pz_il:      db 0
pz_bitsp:   db 0
pz_bpp:     db 0
pz_rowb:    dw 0
pz_wend:    dw 0                    ; the input
pz_inlim:   dw 0
pz_mark:    dw 0
pz_left:    dd 0
pz_dry:     db 0
pz_pad:     db 0
pz_bn:      db 0                    ; the bit count, banked
pz_chk:     times 8 db 0
pz_hplte:   db 0
pz_npal:    dw 0
pz_trn:     dw 0
pz_trnb:    times 256 db 0
pz_tkind:   db 0                    ; 0 none, 1 a grey key, 2 an RGB key
pz_tgrey:   dw 0
pz_trgb:    times 3 dw 0
pz_final:   db 0                    ; inflate
pz_tabk:    db 0                    ; the tables built: 1 fixed, 2 dynamic
pz_mlen:    dw 0
pz_rd:      dw 0
pz_hav:     dw 0
pz_slen:    dw 0
pz_nlen:    dw 0
pz_ndist:   dw 0
pz_ncode:   dw 0
pz_rv:      db 0
pz_rt:      dw 0
pz_widx:    dw 0
pz_wlen:    db 0
pz_wcnt:    dw 0
pz_bl:      dw 0                    ; a build's arguments
pz_bnum:    dw 0
pz_bcmp:    db 0
pz_bprim:   dw 0
pz_bcnt:    dw 0
pz_blit:    db 0
pz_blen:    db 0
pz_bsub:    dw 0
pz_bsubn:   db 0
pz_bnsub:   db 0
pz_bsb:     dw 0
pz_remain:  dd 0                    ; the rows: the bytes still wanted
pz_pend:    dw 0
pz_ptot:    dw 0
pz_rdone:   dw 0
pz_pass:    db 0
pz_p0:      db 0
pz_x0:      dw 0
pz_y0:      dw 0
pz_dxs:     db 0
pz_dys:     db 0
pz_pw:      dw 0
pz_ph:      dw 0
pz_prb:     dw 0
pz_j:       dw 0
pz_y:       dw 0
pz_cur:     dw 0
pz_prev:    dw 0
pz_fill:    dw 0
pz_ftwant:  db 0
pz_ft:      db 0
pz_badf:    db 0
pz_step:    dw 0
pz_tmp:     dw 0
pz_gs:      db 0
pz_cb:      db 0
pz_al:      db 0
