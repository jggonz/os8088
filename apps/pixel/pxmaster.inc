; =============================================================================
; os8088 - apps/pixel/pxmaster.inc
;
; PiXEL's IMAGE MODEL (SPEC.md 106.8): the master, its palette and its mode,
; the scale memory decides, the claims that hold them, and the ONE row
; emitter - box filter, the ordered dither into the cube, the write - which
; is the only code in PiXEL that writes the master. tools/pixelsim.py's
; `emit` and `Ordered` are this file in Python, and tests/pxdecode.py holds
; the two to the byte.
;
; The emitter and everything it calls run on the WORKER (SPEC.md 106.9): no
; file slot, no claim, no drawing. They read [px_cur + PXR_MSEG] afresh at
; every row, because a compaction may have moved the master while the worker
; stood in OSAPI_TASK_ALIVE (SPEC.md 66.5).
; =============================================================================

; --- the picture RECORD (SPEC.md 106.8): everything one picture is, so a
; cancelled open puts the previous one back by copying 48 bytes -------------
PXR_NAME    equ 0                   ; 13: NAME.EXT
PXR_VOL     equ 13                  ; byte: its drive
PXR_DIR     equ 14                  ; word: its folder's cluster
PXR_FSIZE   equ 16                  ; dword: its size, expanded
PXR_SW      equ 20                  ; word: the SOURCE's width...
PXR_SH      equ 22                  ; ...and height
PXR_FMT     equ 24                  ; byte: PXF_*
PXR_BITS    equ 25                  ; byte: bits a pixel, as the file says
PXR_PROG    equ 26                  ; byte: a progressive JPEG
PXR_FHAVE   equ 27                  ; byte: a file has been examined
PXR_FCOUNT  equ 28                  ; word: pictures in its folder
PXR_FIDX    equ 30                  ; word: its place among them
PXR_MW      equ 32                  ; word: the MASTER's width...
PXR_MH      equ 34                  ; ...and height
PXR_SCL     equ 36                  ; byte: s, the master is 1/2^s
PXR_PMODE   equ 37                  ; byte: PM_*
PXR_MSEG    equ 38                  ; word: the master's claim
PXR_MKB     equ 40                  ; word: ...and its KB
PXR_PACK    equ 42                  ; byte: PK_*
PXR_HAVE    equ 43                  ; byte: a DECODED picture is in the master
PXR_NPAL    equ 44                  ; word: palette entries a PAL source has
PXR_RF      equ 46                  ; byte: the decoder's row format (RF_*)
PXR_SERIAL  equ 47                  ; byte: bumped per picture (the thumbnail's
                                    ; key)
PXR_SZ      equ 48

PM_PAL      equ 0                   ; SPEC.md 106.8's three modes
PM_CUBE     equ 1
PM_GREY     equ 2
; (RF_* and PK_* are apps/pixel/pxpart.inc's: a part hands them over too)

PX_RESKB    equ 8                   ; what the master leaves of the largest run

; =============================================================================
; px_mreloc - the master's relocation proc (SPEC.md 66): BX = where a claim
; WAS, DX = where it is. Either master - the picture shown, or the one a
; cancel would put back - is ours to re-base; nothing else derives from it.
; Called inside the allocator: it may not claim, draw or yield
; =============================================================================
px_mreloc:
    cmp [px_cur + PXR_MSEG], bx
    jne .p
    mov [px_cur + PXR_MSEG], dx
.p:
    cmp [px_prev + PXR_MSEG], bx
    jne .v
    mov [px_prev + PXR_MSEG], dx
.v:
    cmp [px_vseg], bx               ; the view claim (pxview.inc) moves too
    jne .out
    mov [px_vseg], dx
.out:
    ret

; =============================================================================
; THE ARITHMETIC every setup path shares. None of it is hot.
; =============================================================================

; px_mulx - DX:AX x CX -> BX:DX:AX (48 bits). Preserves CX, SI, DI
px_mulx:
    push si
    mov si, dx
    mul cx                          ; DX:AX = low word x CX
    push ax
    mov bx, dx
    mov ax, si
    mul cx                          ; DX:AX = high word x CX
    add ax, bx
    adc dx, 0
    mov bx, dx
    mov dx, ax
    pop ax
    pop si
    ret

; px_div - [px_n0..px_n2] (48 bits) / [px_dv0..px_dv1] (32, not 0): the
; quotient in [px_n0..px_n2], the remainder in DX:AX. Shift and subtract, so
; it is 48 rounds and exact whatever the sizes. Preserves BX, CX, SI, DI
px_div:
    push cx
    xor ax, ax
    xor dx, dx
    mov cx, 48
.l:
    shl word [px_n0], 1
    rcl word [px_n1], 1
    rcl word [px_n2], 1
    rcl ax, 1
    rcl dx, 1
    jc .sub                         ; the remainder passed 32 bits
    cmp dx, [px_dv1]
    jb .no
    ja .sub
    cmp ax, [px_dv0]
    jb .no
.sub:
    sub ax, [px_dv0]
    sbb dx, [px_dv1]
    or word [px_n0], 1
.no:
    loop .l
    pop cx
    ret

; px_div32 - DX:AX / CX:BX -> DX:AX (floor). Preserves BX, CX, SI, DI
px_div32:
    mov [px_n0], ax
    mov [px_n1], dx
    mov word [px_n2], 0
    mov [px_dv0], bx
    mov [px_dv1], cx
    call px_div
    mov ax, [px_n0]
    mov dx, [px_n1]
    ret

; px_cdiv32 - DX:AX / CX:BX rounded UP. Preserves BX, CX, SI, DI
px_cdiv32:
    mov [px_n0], ax
    mov [px_n1], dx
    mov word [px_n2], 0
    mov [px_dv0], bx
    mov [px_dv1], cx
    call px_div
    or ax, dx                       ; a remainder: one more
    mov ax, [px_n0]
    mov dx, [px_n1]
    jz .out
    add ax, 1
    adc dx, 0
.out:
    ret

; =============================================================================
; THE PALETTE A MODE HAS (SPEC.md 106.8). px_setpal - [px_pal] for the
; current picture: the source's for PAL (from [px_spal]), the cube, or the
; greys. Preserves all
; =============================================================================
px_setpal:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov al, [px_cur + PXR_PMODE]
    cmp al, PM_PAL
    jne .cube
    mov si, px_spal
    mov di, px_pal
    mov cx, 768 / 2
    cld
    rep movsw
    jmp .out
.cube:
    cmp al, PM_CUBE
    jne .grey
    mov di, px_pal                  ; (51r, R7[g], 51b), r major
    xor bx, bx                      ; BL = r, BH = g
.cr:
    xor bh, bh
.cg:
    xor cl, cl                      ; CL = b
.cb:
    mov al, 51
    mul bl
    stosb
    push bx
    mov bl, bh
    xor bh, bh
    mov al, [px_r7 + bx]
    pop bx
    stosb
    mov al, 51
    mul cl
    stosb
    inc cl
    cmp cl, 6
    jb .cb
    inc bh
    cmp bh, 7
    jb .cg
    inc bl
    cmp bl, 6
    jb .cr
    mov al, 51                      ; ...and the four greys
.cgr:
    stosb
    stosb
    stosb
    add al, 51
    cmp al, 51 * 5
    jb .cgr
    xor bx, bx                      ; the NEUTRAL codes are those greys too
    mov al, 51                      ; (SPEC.md 106.8)
.cn:
    mov di, [px_neut + bx]
    add di, px_pal
    stosb
    stosb
    stosb
    add al, 51
    inc bx
    inc bx
    cmp bx, 8
    jb .cn
    jmp short .out
.grey:
    mov di, px_pal
    xor al, al
.g:
    stosb
    stosb
    stosb
    inc al
    jnz .g
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE SCALE AND THE CLAIMS (SPEC.md 106.8)
; =============================================================================

; px_mkb - for scale CL: AX = the master's KB at it, [px_tmw]/[px_tmh] its
; size; CF = 1 when the scale is not usable (a side would be 0). Preserves
; all but AX
px_mkb:
    push bx
    push cx
    push dx
    mov ax, [px_cur + PXR_SW]
    shr ax, cl
    mov [px_tmw], ax
    mov bx, [px_cur + PXR_SH]
    shr bx, cl
    mov [px_tmh], bx
    or ax, ax
    jz .no
    or bx, bx
    jz .no                          ; (any width's row shares one segment with
    mul bx                          ; the quantiser's 12 KB: 3 x 8,192 + 12 KB)                          ; DX:AX = the bytes
    add ax, 1023
    adc dx, 0
    mov cx, 10                      ; ...in KB, up
.kb:
    shr dx, 1
    rcr ax, 1
    loop .kb
    or dx, dx
    jnz .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; px_wkb - the WORK claim's KB for scale CL at [px_tmw] (SPEC.md 106.9): the
; quantiser's pages, the row buffer, the accumulators below 1/1, a PCX line,
; a KB of the decoder's tables. Sets the sub-buffers' offsets - [px_wo_acc]
; [px_wo_lin] [px_wo_lut] in paragraphs from the row - and [px_tquant].
; AX = KB; CF = 1 too large. Preserves all but AX
px_wkb:
    push bx
    push cx
    push dx
    mov [px_tscl], cl
    mov ax, [px_cur + PXR_SW]       ; THE ROW: max(3w + 4, the raw row)
    mov bx, ax
    shl ax, 1
    add ax, bx
    add ax, 4
    cmp ax, [px_drawsz]
    jae .r1
    mov ax, [px_drawsz]
.r1:
    mov [px_wrowsz], ax
    mov byte [px_tquant], 0         ; the cube is the target: truecolour, or
    mov bl, [px_cur + PXR_RF]       ; indices below 1/1
    cmp bl, RF_RGB
    je .q
    cmp bl, RF_IDX
    jne .noq
    cmp byte [px_tscl], 0
    je .noq
.q:
    mov byte [px_tquant], 1
.noq:
    add ax, 15
    jc .big
    mov cl, 4
    shr ax, cl
    mov dx, ax                      ; DX = paragraphs so far
    mov [px_wo_acc], dx
    cmp byte [px_tscl], 0           ; THE ACCUMULATORS: mw words a channel
    je .noacc
    mov ax, [px_tmw]
    shl ax, 1
    cmp byte [px_cur + PXR_RF], RF_GREY
    je .acc1
    mov bx, ax
    shl ax, 1
    add ax, bx
.acc1:
    add ax, 15
    mov cl, 4
    shr ax, cl
    add dx, ax
.noacc:
    mov [px_wo_lin], dx             ; A PCX LINE (0 for anything else)
    mov ax, [px_dlinsz]
    add ax, 15
    rcr ax, 1                       ; (a carry out of the add is bit 16)
    mov cl, 3
    shr ax, cl
    add dx, ax
    mov [px_wo_lut], dx             ; THE TABLES: a KB...
    add dx, 1024 / 16 + 1
    mov [px_wo_dec], dx             ; ...a decoder part's scratch (106.18)...
    add dx, [px_ddpara]
    jc .big
    add dx, QT_SIZE / 16            ; ...and the quantiser's pages below the
    jc .big                         ; row
    mov ax, dx
    add ax, 63
    mov cl, 6
    shr ax, cl                      ; paragraphs -> KB, up
    clc
    jmp short .out
.big:
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; px_claims - the master, the ring and the work claim for the picture
; [px_cur] describes (SPEC.md 106.8): the finest scale memory affords, the
; previous master freed first when that buys a finer one. Sets PXR_SCL,
; PXR_MW, PXR_MH, PXR_MSEG, PXR_MKB, PXR_PMODE, [px_rseg0] and [px_wseg].
; out CF = 0, or CF = 1 AX = PXD_MEM with nothing claimed
px_claims:
    push bx
    push cx
    push dx
    push si
    call OSAPI_MEM_AVAIL            ; AX = largest run, BX = total, KB
    call px_capavl
    call px_pickscale               ; CL = the scale, CF = none fits
    jc .free1
    cmp byte [px_prev + PXR_HAVE], 0
    je .go
    mov [px_kscl], cl               ; would freeing the old master buy more?
    mov ax, [px_prev + PXR_MKB]
    add [px_avl], ax
    add [px_avt], ax
    call px_pickscale
    jc .keep
    cmp cl, [px_kscl]
    jb .free
.keep:
    mov cl, [px_kscl]
    jmp short .go
.free1:
    cmp byte [px_prev + PXR_HAVE], 0  ; nothing fits with it held: let it go
    je .nomem                         ; and ask again
.free:
    call px_freeprev
    call OSAPI_MEM_AVAIL
    call px_capavl
    call px_pickscale
    jc .nomem
.go:
    mov [px_cur + PXR_SCL], cl
    call px_mkb                     ; AX = master KB, px_tmw/tmh
    mov [px_cur + PXR_MKB], ax
    mov bx, [px_tmw]
    mov [px_cur + PXR_MW], bx
    mov bx, [px_tmh]
    mov [px_cur + PXR_MH], bx
    call OSAPI_MEM_CLAIM            ; the master first: the largest
    jc .nomem
    mov [px_cur + PXR_MSEG], dx
    mov ax, px_mreloc
    call OSAPI_MEM_MOVABLE
    call px_wkb                     ; the work claim: the quantiser's tables
    call OSAPI_MEM_CLAIM            ; at its base, the row's segment after
    jc .nomem2                      ; them
    mov [px_wbase], dx
    add dx, QT_SIZE / 16
    mov [px_wseg], dx
    cmp byte [px_rflat], 0          ; the ring, unless the file is in memory
    jne .mode
    call px_ringclaim
    jc .nomem3
.mode:
    mov al, PM_PAL                  ; the mode the scale leaves
    mov ah, [px_cur + PXR_RF]
    cmp ah, RF_RGB
    je .cube
    cmp ah, RF_GREY
    jne .idx
    mov al, PM_GREY
    jmp short .setm
.idx:
    cmp byte [px_cur + PXR_SCL], 0
    je .setm
.cube:
    mov al, PM_CUBE
.setm:
    mov [px_cur + PXR_PMODE], al
    clc
    jmp short .out
.nomem3:
    mov dx, [px_wbase]
    call OSAPI_MEM_FREE
.nomem2:
    mov dx, [px_cur + PXR_MSEG]
    call OSAPI_MEM_FREE
    mov word [px_cur + PXR_MSEG], 0
.nomem:
    mov ax, PXD_MEM
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_capavl - AX/BX = OSAPI_MEM_AVAIL's answers into [px_avl] [px_avt], the
; largest run cut to [px_mcap] when a test has set one (tests/pxdecode.py's
; way of making a small picture take a smaller scale). Preserves all
px_capavl:
    mov [px_avl], ax
    mov [px_avt], bx
    cmp word [px_mcap], 0
    je .out
    push ax
    mov ax, [px_mcap]
    cmp ax, [px_avl]
    jae .k
    mov [px_avl], ax
.k:
    pop ax
.out:
    ret

; px_pickscale - out CL = the finest scale whose master fits [px_avl] less
; the reserve and whose master + work + ring fit [px_avt]; CF = 1 none
px_pickscale:
    push ax
    push bx
    xor cl, cl
.s:
    call px_mkb
    jc .next
    mov bx, ax                      ; BX = master KB
    add ax, PX_RESKB
    jc .next
    cmp ax, [px_avl]
    ja .next
    call px_wkb
    jc .next
    add bx, ax
    jc .next
    add bx, PX_RESKB + PX_RINGKB + 1
    jc .next
    cmp bx, [px_avt]
    ja .next
    clc
    jmp short .out
.next:
    inc cl
    cmp cl, 4
    jb .s
    stc
.out:
    pop bx
    pop ax
    ret

; px_freeprev - give the previous picture's master back: a cancel will then
; have nothing to restore (SPEC.md 106.8). Preserves all
px_freeprev:
    push dx
    cmp byte [px_prev + PXR_HAVE], 0
    je .out
    mov dx, [px_prev + PXR_MSEG]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
.z:
    mov word [px_prev + PXR_MSEG], 0
    mov byte [px_prev + PXR_HAVE], 0
.out:
    pop dx
    ret

; =============================================================================
; THE ROW EMITTER (SPEC.md 106.8) - worker side
;
; px_emit - the source row in the work claim's row buffer, in [px_cur +
; PXR_RF]'s format, is source row AX (top-down). Below 1/1 it is summed into
; the accumulators and a block of 2^s rows becomes one master row; at 1/1 it
; IS one. Counts progress and wakes the UI (pxpump.inc).
; out CF = 1 AX = PXD_ABORT when the user has cancelled; else CF = 0.
; Clobbers AX, BX, CX, DX, SI, DI (ES and DS kept)
; =============================================================================
px_emit:
    push es
    inc word [px_srcdone]
    mov bx, ax                      ; BX = y
    mov cl, [px_cur + PXR_SCL]
    mov ax, [px_cur + PXR_MH]
    shl ax, cl
    cmp bx, ax
    jae .done                       ; past the last whole block: dropped
    or cl, cl
    jnz .scaled
    mov ax, bx                      ; --- 1/1: this row IS master row y
    mov [px_qrow], ax               ; (the dither's phase)
    call px_mrow                    ; ES:DI = it
    call px_emitrow
    jmp short .wrote
.scaled:
    cmp byte [px_cur + PXR_RF], RF_IDX
    jne .acc
    call px_idx2rgb                 ; an index is not a colour: through the
.acc:                               ; palette first
    cmp byte [px_rcnt], 0
    jne .sum
    call px_accclear
.sum:
    call px_accrow
    inc byte [px_rcnt]
    mov al, [px_k + PXK_RBLK]       ; the rows a block sums: 2^s, or fewer
    or al, al                       ; for an interlaced picture (106.18)
    jnz .rb
    mov al, 1
    shl al, cl
.rb:
    cmp [px_rcnt], al
    jb .done
    mov byte [px_rcnt], 0
    call px_accavg                  ; the averages, back into the row buffer
    mov ax, bx
    shr ax, cl                      ; the block's master row
    mov [px_qrow], ax
    call px_mrow
    call px_emitrow
.wrote:
    inc word [px_rdone]
.done:
    pop es
    jmp px_progress                 ; pxpump.inc: abort, liveness, the wake

; px_mrow - AX = a master row: ES:DI = its first byte. Preserves all else
px_mrow:
    push ax
    push dx
    mul word [px_cur + PXR_MW]      ; DX:AX = its offset
    mov di, ax
    and di, 15
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    pop cx
    add ax, [px_cur + PXR_MSEG]     ; re-read: the master may have moved
    mov es, ax
    pop dx
    pop ax
    ret

; px_emitrow - the row buffer's [px_cur + PXR_MW] pixels into ES:DI: copied
; (an index or a grey), or quantised into the cube. Preserves ES
px_emitrow:
    push ds
    cmp byte [px_tquant], 0
    jne .q
    mov cx, [px_cur + PXR_MW]
    mov ds, [px_wseg]
    xor si, si
    cld
    rep movsb
    pop ds
    ret
.q:
    pop ds
    jmp px_quant

; px_idx2rgb - expand the row buffer's indices into R,G,B triples through
; [px_spal], from the right end leftwards (each lands at or right of where
; it was read). Preserves BX, CX
px_idx2rgb:
    push bx
    push cx
    push es
    mov es, [px_wseg]
    mov cx, [px_cur + PXR_SW]
    mov si, cx
    dec si                          ; SI = the last index
    mov di, cx
    shl di, 1
    add di, cx
    dec di                          ; DI = the last triple's B
    std
.l:
    mov bl, [es:si]
    xor bh, bh
    mov ax, bx
    shl bx, 1
    add bx, ax                      ; BX = 3 x index
    mov al, [px_spal + bx + 2]
    stosb
    mov al, [px_spal + bx + 1]
    stosb
    mov al, [px_spal + bx]
    stosb
    dec si
    loop .l
    cld
    pop es
    pop cx
    pop bx
    ret

; px_accclear - zero the accumulators: mw words a channel. Preserves BX, CX
px_accclear:
    push cx
    push es
    call px_accseg                  ; ES = the accumulators, CX = their words
    xor di, di
    xor ax, ax
    cld
    rep stosw
    pop es
    pop cx
    ret

; px_accseg - ES = the accumulators, CX = their words, DX = channels (1 or 3)
px_accseg:
    mov ax, [px_wseg]
    add ax, [px_wo_acc]
    mov es, ax
    mov dx, 3
    cmp byte [px_cur + PXR_RF], RF_GREY
    jne .3
    mov dx, 1
.3:
    mov ax, [px_cur + PXR_MW]
    push dx
    mul dx
    pop dx
    mov cx, ax
    ret

; px_accrow - add the row buffer into the accumulators: per master pixel
; and channel, the sum of its 2^s source pixels. Preserves BX, CX
px_accrow:
    push bx
    push cx
    push bp
    push ds
    push es
    mov bp, 1
    shl bp, cl                      ; BP = n, the block's width
    call px_accseg                  ; ES:0 = acc, DX = channels
    mov ds, [px_wseg]               ; DS:0 = the row
    xor si, si
    xor di, di
    mov bx, [cs:px_cur + PXR_MW]    ; BX = master pixels
    cmp dx, 1
    je .grey
.rgb:                               ; three channels: the block's n triples
    push bx
    xor bx, bx                      ; BX = r, sums kept in BX/CX/DX
    xor cx, cx
    xor dx, dx
    push bp
.r3:
    lodsb
    xor ah, ah
    add bx, ax
    lodsb
    add cx, ax
    lodsb
    add dx, ax
    dec bp
    jnz .r3
    pop bp
    add [es:di], bx
    add [es:di + 2], cx
    add [es:di + 4], dx
    add di, 6
    pop bx
    dec bx
    jnz .rgb
    jmp short .out
.grey:
    xor dx, dx
    mov cx, bp
.g1:
    lodsb
    xor ah, ah
    add dx, ax
    loop .g1
    add [es:di], dx
    add di, 2
    dec bx
    jnz .grey
.out:
    pop es
    pop ds
    pop bp
    pop cx
    pop bx
    ret

; px_accavg - the accumulators' averages (sum >> 2s) as bytes into the row
; buffer's first mw x channels. Preserves BX, CX
px_accavg:
    push bx
    push cx
    push ds
    push es
    mov bl, cl
    shl bl, 1                       ; BL = 2s, or 2s - 1 when a block is
    cmp byte [px_k + PXK_RBLK], 0   ; half its rows (an interlaced picture,
    je .sh                          ; SPEC.md 106.18)
    dec bl
.sh:
    call px_accseg                  ; ES = acc, CX = words
    push es
    pop ds                          ; DS:SI = acc
    mov es, [cs:px_wseg]            ; ES:DI = the row
    xor si, si
    xor di, di
    mov dx, cx
    mov cl, bl
    cld
.l:
    lodsw
    shr ax, cl
    stosb
    dec dx
    jnz .l
    pop es
    pop ds
    pop cx
    pop bx
    ret

; =============================================================================
; px_quant - THE CUBE BY ORDERED DITHER (SPEC.md 106.8): the row buffer's mw
; triples into ES:DI, mw indices, for master row [px_qrow].
;
; A 16x16 BLUE-NOISE matrix cut to sixteen classes, ONE threshold a pixel
; shared by its three channels, so a channel's part of the index - 42 qr,
; 6 qg or qb, q = (n v + T16[class]) div 255 - is one `xlatb` into a page of
; 256 built per decode, and a pixel is three lookups and two adds. The 48
; pages (sixteen classes, three channels in the ROW's byte order, [px_bgr])
; sit at the work claim's base, under the row, so DS:BX is a page and DS:SI
; the row and `xlatb` needs no prefix. tools/pixelsim.py's `Ordered` is this
; loop in Python, and SPEC.md 106.8 is why it replaced Floyd-Steinberg: the
; same look through the window's dither, at about a tenth of the 1,361
; cycles a pixel the three error passes cost.
;
; THE UNROLLED LOOP IS PATCHED, sixteen `mov bh, imm8` immediates a row: a
; column's class is its matrix row's, and nothing cheaper than an immediate
; can put an arbitrary page in BH (an `inc bh` only walks pages in order).
; The patching runs here, a routine call and hundreds of instructions before
; the loop is fetched, so no stale byte can sit in the 4-byte prefetch queue
; (apps/tracker/trkplay.inc's mixer is the precedent). The loop is ENTERED
; part-way, Duff's way, at slot (16 - mw % 16) % 16, with the immediates
; rotated to match, so no pixel tests a count. Preserves ES
; =============================================================================
QT_SIZE     equ 48 * 256            ; the pages, then the row

%macro PXQ_SLOT 1
.qs%1:
    mov bh, 0                       ; PATCHED: this column's class x 3
    lodsb                           ; the row's first channel...
    xlatb
    mov ah, al
    inc bh
    lodsb                           ; ...G...
    xlatb
    add ah, al
    inc bh
    lodsb                           ; ...and the third
    xlatb
    add al, ah                      ; the cube's index
    stosb
%endmacro

px_quant:
    ; STKBALANCE-OK: the `jmp dx` below is Duff's entry into the unrolled
    ; loop (a slot of this routine's own, from px_qslots), not a tail call:
    ; every slot runs on to the same `.done` that pops the two words pushed
    ; here. No static walk can follow a computed jump into the middle of a
    ; routine, so the balance is this routine's to state.
    push bp
    push ds
    mov ax, [px_cur + PXR_MW]
    mov bp, ax
    add bp, 15
    mov cl, 4
    shr bp, cl                      ; BP = groups of sixteen, the first partial
    neg ax
    and ax, 15                      ; AX = the slot pixel 0 enters at
    push ax
    mov si, [px_qrow]               ; --- the row's sixteen pages, rotated ----
    and si, 15
    shl si, cl
    add si, px_qpage                ; SI = this matrix row's page bytes
    mov dx, si
    xor bx, bx                      ; BX = slot x 2
.pat:
    mov si, bx                      ; slot i takes column (i - entry) & 15
    shr si, 1
    sub si, ax
    and si, 15
    add si, dx
    mov cl, [si]
    mov si, [px_qslots + bx]
    mov [si + 1], cl                ; the `mov bh, imm8`'s immediate
    inc bx
    inc bx
    cmp bx, 32
    jb .pat
    pop bx                          ; the entry slot
    shl bx, 1
    mov dx, [px_qslots + bx]
    mov cx, bp
    mov si, QT_SIZE
    mov ds, [px_wbase]
    xor bx, bx                      ; BL stays 0: BX is a page
    cld
    jmp dx
.top:
%assign pqs 0
%rep 16
    PXQ_SLOT pqs
%assign pqs pqs + 1
%endrep
    dec cx                          ; (sixteen slots are 304 bytes: too far
    jz .done                        ; back for `loop`)
    jmp .top
.done:
    pop ds
    pop bp
    ret

px_qslots:
%assign pqs 0
%rep 16
    dw px_quant.qs %+ pqs
%assign pqs pqs + 1
%endrep

; px_qprep - before a picture's first row: the quantiser's 48 pages, when the
; emitter quantises (SPEC.md 106.8). Preserves all
px_qprep:
    cmp byte [px_tquant], 0
    je .out
    call px_qtabs
.out:
    ret

; px_qtabs - the 48 pages at the work claim's base: page (class x 3 + c) is
; the part of the index that the row's byte c contributes at that class's
; threshold - 42 q, 6 q or q, by [px_bgr]'s order. Each page is a staircase
; of at most seven runs, so it is filled a run at a time: level q starts at
; v = ceil((255 q - T) / n). Once a decode. Preserves all
px_qtabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [px_wbase]
    xor di, di
    cld
    xor si, si                      ; SI = the class
.k:
    xor bp, bp                      ; BP = c, the byte in the triple
.c:
    mov bx, 0x0606                  ; BL = n, BH = the part's multiplier:
    cmp bp, 1                       ; G (byte 1) is seven levels and 6 q,
    je .k1
    mov bx, 0x0105                  ; and R / B six levels and 42 q / q, R
    mov al, [px_bgr]                ; at byte 0 unless the row is B, G, R
    xor ah, ah
    shl ax, 1                       ; AX = 2 when B, G, R; 0 when R, G, B
    cmp bp, ax
    jne .k1
    mov bh, 42
.k1:
    mov al, [px_qt16 + si]          ; T
    call px_qpage1
    inc bp
    cmp bp, 3
    jb .c
    inc si
    cmp si, 16
    jb .k
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_qpage1 - one page at ES:DI (DI advanced by 256): AL = T, BL = n, BH =
; the multiplier. Clobbers AX, CX, DX
px_qpage1:
    push si
    push bp
    mov ah, 0
    mov bp, ax                      ; BP = T
    mov si, di                      ; SI = the page's first byte
    xor dx, dx                      ; DL = q, DH = q x the multiplier
.q:
    inc dl                          ; where does level q (DL) start?
    cmp dl, bl
    ja .last
    mov al, 255
    mul dl                          ; AX = 255 q
    sub ax, bp                      ; ...- T (> 0: T <= 247)
    push dx
    mov cl, bl
    xor ch, ch
    add ax, cx
    dec ax
    xor dx, dx
    div cx                          ; AX = ceil((255 q - T) / n) <= 255
    pop dx
    add ax, si                      ; ...as an offset
    mov cx, ax
    sub cx, di                      ; the run of level q - 1
    mov al, dh
    rep stosb
    add dh, bh
    jmp short .q
.last:
    mov cx, si
    add cx, 256
    sub cx, di                      ; the rest of the page is level n
    mov al, dh
    rep stosb
    pop bp
    pop si
    ret

; px_qinit - once at launch (pixel.asm): nothing the ordered quantiser needs
; (its pages are built per decode, its classes ship in pxplans.inc). Kept as
; the launch hook the image core has. Preserves all
px_qinit:
    ret

; --- the cube's tables (SPEC.md 106.8) ------------------------------------------
px_r7:      db 0, 43, 85, 128, 170, 213, 255
px_neut:    dw 49 * 3, 98 * 3, 153 * 3, 202 * 3    ; the NEUTRAL codes' entries
