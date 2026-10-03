; =============================================================================
; os8088 - apps/pixel/pxmaster.inc
;
; PiXEL's IMAGE MODEL (SPEC.md 106.8): the master, its palette and its mode,
; the scale memory decides, the claims that hold them, and the ONE row
; emitter - box filter, Floyd-Steinberg into the cube, the write - which is
; the only code in PiXEL that writes the master. tools/pixelsim.py's `emit`
; and `Quant` are this file in Python, and tests/pxdecode.py holds the two to
; the byte.
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
RF_IDX      equ 0                   ; what a decoder hands the emitter
RF_GREY     equ 1
RF_RGB      equ 2
PK_NONE     equ 0                   ; the Info panel's Packing
PK_RLE      equ 1
PK_RLE8     equ 2
PK_RLE4     equ 3
PK_BITF     equ 4

PX_DIMMAX   equ 8192                ; refused above, before any arithmetic
PX_QWMAX    equ 6144                ; an RGB source wider than this at 1/1
                                    ; takes 1/2: its row, its three error
                                    ; rows and the quantiser's tables then
                                    ; share one segment (3w + 6(w+2) + 5 KB)
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
; size; CF = 1 when the scale is not usable (a side would be 0, or an RGB
; row too wide for one segment at 1/1). Preserves all but AX
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
    jz .no
    or cl, cl                       ; 1/1 truecolour: the row and its error row
    jnz .ok                         ; must share a segment (PX_QWMAX)
    cmp byte [px_cur + PXR_RF], RF_RGB
    jne .ok
    cmp ax, PX_QWMAX
    ja .no
.ok:
    mul bx                          ; DX:AX = the bytes
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
; row buffer (with the error row behind it when the cube is the target),
; the accumulators below 1/1, a PCX line, the tables. Sets the sub-buffers'
; offsets - [px_woerr] in bytes, [px_wo_acc] [px_wo_lin] [px_wo_lut] in
; paragraphs - and [px_tquant]. AX = KB; CF = 1 the row and its error row
; would not share one segment. Preserves all but AX
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
    add ax, 15                      ; ...so the error rows follow, three of
    jc .big                         ; mw + 2 words, and then the tables
    and ax, 0xFFF0
    mov [px_woerr], ax
    mov bx, [px_tmw]
    add bx, 2
    mov dx, bx
    shl bx, 1
    add bx, dx
    shl bx, 1                       ; 6 (mw + 2)
    add ax, bx
    jc .big
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
    mov [px_wo_lut], dx             ; THE TABLES: a KB, and the quantiser's
    add dx, 1024 / 16 + 1 + QT_SIZE / 16
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
    mov al, 1
    shl al, cl
    cmp [px_rcnt], al
    jb .done
    mov byte [px_rcnt], 0
    call px_accavg                  ; the averages, back into the row buffer
    mov ax, bx
    shr ax, cl                      ; the block's master row
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
    shl bl, 1                       ; BL = 2s
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
; px_quant - FLOYD-STEINBERG INTO THE CUBE, one row of error (SPEC.md 106.8):
; the row buffer's mw triples into ES:DI, mw indices.
;
; IT IS THREE PASSES, ONE A CHANNEL, and that is the whole of its speed: one
; channel's error is a function of that channel alone, so a pass keeps its
; whole state in registers - the pending sums p0 and p1 in DX and BP, the
; carry folded straight into the slot it is owed to - and every per-value
; quantity (the level, its share of the index, e, 3e, 5e, 7e) is ONE table
; lookup by 2v. The one coupling, a NEUTRAL's grey, needs all three levels,
; so the passes run B, R, G: B leaves its level in its byte, R leaves 42 qr +
; qb in its byte, and G finds the cube index there, recognises the four
; neutral indices (49, 98, 153, 202 - the only codes of (k, GNEUT[k], k))
; and only then takes its own error against 51k instead of its level's. That
; is pixelsim's Quant exactly: the same sums in the same order, the same
; floor shift, and G's error the one a neutral changes. The G bytes then
; hold the indices and one more pass copies them out.
;
; THE WORK SEGMENT ([px_wseg]) holds the row, the three channels' error rows
; at [px_woerr] ((mw + 2) words each: slot x + 1 is what pixel x of THIS row
; is owed, slot x is overwritten with what pixel x - 1 of the NEXT is), and
; the two kinds of table at the CLAIM's base, [px_wbase]:0, below the row
; (px_qtabs), so a lookup is [2v]. [px_bgr] = 1: the row is
; B, G, R (a BMP's own order, read without a swap). Preserves ES
; =============================================================================
QT_T1       equ 0                   ; per kind (6 levels, then 7): word tables
QT_T3       equ 512                 ; by 2v - e, 3e, 5e, 7e - and the parts
QT_T5       equ 1024
QT_T7       equ 1536
QT_PART     equ 2048                ; 6-level: lo 42q (R's), hi q (B's);
                                    ; 7-level: lo 6q (G's)
QT_KIND     equ 2560                ; bytes from one kind's tables to the next
QT_SIZE     equ 2 * QT_KIND

; PXQ_PASS chan, kind, parthi, other - one channel's pass. chan = the
; channel's byte in a triple, kind = 0 (six levels) or 1 (seven), parthi =
; 0 low byte of the part / 1 high byte, other = the offset (from this
; channel's byte) of the byte to add to the part, or none
%macro PXQ_PASS 4
    mov si, QT_SIZE + %1            ; SI = the channel's first byte
    mov di, [cs:px_woerr]           ; DI = its error row's slot 0
    add di, [cs:px_qerrc]
    add di, QT_SIZE
    mov cx, [cs:px_cur + PXR_MW]
    xor dx, dx                      ; p0
    xor bp, bp                      ; p1
    jmp short %%px
%%lo:
    xor ax, ax
    jmp short %%in
%%hi:
    mov ax, 255
    jmp short %%in
%%px:
    mov ax, [di + 2]                ; owed to this pixel, the carry in it
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add al, [si]                    ; v = c + (owed >> 4)
    adc ah, 0
    js %%lo
    or ah, ah
    jnz %%hi
%%in:
    mov bx, ax
    shl bx, 1                       ; BX = 2v: the tables are at DS:0
    mov al, [bx + %2 * QT_KIND + QT_PART + %3] ; this channel's part...
  %ifnidn %4, none
    add al, [si + %4]               ; ...and what the earlier pass left
  %endif
    mov [si], al
    mov ax, [bx + %2 * QT_KIND + QT_T7]
    add [di + 4], ax                ; 7e: right, into the next pixel's slot
    mov ax, [bx + %2 * QT_KIND + QT_T3]
    add ax, bp
    mov [di], ax                    ; slot x := p1 + 3e
    mov bp, [bx + %2 * QT_KIND + QT_T5]
    add bp, dx                      ; p1 := p0 + 5e
    mov dx, [bx + %2 * QT_KIND + QT_T1] ; p0 := e
    add si, 3
    inc di
    inc di
    dec cx
    jnz %%px
    mov [di], bp                    ; slot mw := the last pixel's p1
%endmacro

; PXQ_GPASS chan, rofs - G's pass: as PXQ_PASS with seven levels, the R
; byte's 42 qr + qb at rofs added in to make the index - and a NEUTRAL index
; (49, 98, 153, 202) put right: grey 251 + k, and G's own error taken
; against 51k
%macro PXQ_GPASS 2
    mov si, QT_SIZE + %1
    mov di, [cs:px_woerr]
    add di, [cs:px_qerrc]
    add di, QT_SIZE
    mov cx, [cs:px_cur + PXR_MW]
    xor dx, dx
    xor bp, bp
    jmp short %%px
%%lo:
    xor ax, ax
    jmp short %%in
%%hi:
    mov ax, 255
    jmp short %%in
%%px:
    mov ax, [di + 2]
    sar ax, 1
    sar ax, 1
    sar ax, 1
    sar ax, 1
    add al, [si]
    adc ah, 0
    js %%lo
    or ah, ah
    jnz %%hi
%%in:
    mov bx, ax
    shl bx, 1
    mov [cs:px_qvg], ax             ; (v, should it be a neutral)
    mov al, [bx + QT_KIND + QT_PART]
    add al, [si + %2]               ; the cube's index
    cmp al, 49
    jb %%cube
    je %%n1
    cmp al, 98
    je %%n2
    cmp al, 153
    je %%n3
    cmp al, 202
    je %%n4
%%cube:
    mov [si], al
    mov ax, [bx + QT_KIND + QT_T7]
    add [di + 4], ax
    mov ax, [bx + QT_KIND + QT_T3]
    add ax, bp
    mov [di], ax
    mov bp, [bx + QT_KIND + QT_T5]
    add bp, dx
    mov dx, [bx + QT_KIND + QT_T1]
%%next:
    add si, 3
    inc di
    inc di
    dec cx
    jz %%end
    jmp %%px
%%n1:
    mov ah, 51
    mov al, 252
    jmp short %%neut
%%n2:
    mov ah, 102
    mov al, 253
    jmp short %%neut
%%n3:
    mov ah, 153
    mov al, 254
    jmp short %%neut
%%n4:
    mov ah, 204
    mov al, 255
%%neut:                             ; G's error against the grey: arithmetic,
    mov [si], al                    ; not the tables (four codes in 252)
    mov bl, ah
    mov ax, [cs:px_qvg]             ; AX = v
    xor bh, bh
    sub ax, bx                      ; e
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    sub bx, ax                      ; 7e
    add [di + 4], bx
    mov bx, ax
    shl bx, 1
    add bx, ax                      ; 3e
    add bx, bp
    mov [di], bx
    mov bp, ax
    shl bp, 1
    shl bp, 1
    add bp, ax                      ; 5e
    add bp, dx
    mov dx, ax
    jmp short %%next
%%end:
    mov [di], bp
%endmacro

px_quant:
    push bp
    push ds
    push di                         ; the master row
    mov ax, [px_cur + PXR_MW]       ; each channel's error row: mw + 2 words
    add ax, 2
    shl ax, 1
    mov [px_qerrsz], ax
    mov ds, [px_wbase]              ; the tables at 0, the row at QT_SIZE
    cmp byte [cs:px_bgr], 0
    jne .bgr
    ; --- R, G, B: B is +2, R is +0, G is +1 -----------------------------------
    mov ax, [cs:px_qerrsz]          ; B's error row is the third
    shl ax, 1
    mov [cs:px_qerrc], ax
    PXQ_PASS 2, 0, 1, none          ; B: its level
    mov word [cs:px_qerrc], 0
    PXQ_PASS 0, 0, 0, 2             ; R: 42 qr + qb
    mov ax, [cs:px_qerrsz]
    mov [cs:px_qerrc], ax
    PXQ_GPASS 1, -1                 ; G: the index
    jmp .copy
.bgr:
    ; --- B, G, R: B is +0, R is +2 -------------------------------------------
    mov ax, [cs:px_qerrsz]
    shl ax, 1
    mov [cs:px_qerrc], ax
    PXQ_PASS 0, 0, 1, none
    mov word [cs:px_qerrc], 0
    PXQ_PASS 2, 0, 0, -2
    mov ax, [cs:px_qerrsz]
    mov [cs:px_qerrc], ax
    PXQ_GPASS 1, 1
.copy:
    pop di                          ; the G bytes are the indices: out
    mov si, QT_SIZE + 1
    mov cx, [cs:px_cur + PXR_MW]
    cld
.c:
    movsb
    inc si
    inc si
    loop .c
    pop ds
    pop bp
    ret

; px_qtabs - the quantiser's tables in the work segment (SPEC.md 106.8): for
; the six levels and the seven, by 2v, e = v - the level's value and 3e, 5e,
; 7e, and the part of the index (42q and q; 6q). Once a decode, before its
; first row. Preserves all
px_qtabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [px_wbase]              ; at the work claim's base
    xor di, di
    xor si, si                      ; SI = v
.v6:
    mov bl, [px_q6 + si]            ; the level
    xor bh, bh
    mov al, [px_r6 + bx]            ; e = v - R6[q]
    xor ah, ah
    mov dx, si
    sub dx, ax
    call px_qterms                  ; e, 3e, 5e, 7e at ES:DI(+512k)
    mov al, bl                      ; the parts: 42q low, q high
    mov cl, 42
    mul cl
    mov ah, bl
    mov [es:di + QT_PART], ax
    inc di
    inc di
    inc si
    cmp si, 256
    jb .v6
    mov di, QT_KIND
    xor si, si
.v7:
    mov bl, [px_q7 + si]
    xor bh, bh
    mov al, [px_r7 + bx]
    xor ah, ah
    mov dx, si
    sub dx, ax
    call px_qterms
    mov al, bl                      ; 6q
    mov cl, 6
    mul cl
    mov [es:di + QT_PART], ax
    inc di
    inc di
    inc si
    cmp si, 256
    jb .v7
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_qterms - DX = e: e, 3e, 5e, 7e into ES:DI's four word tables. Preserves
; all
px_qterms:
    push ax
    mov [es:di + QT_T1], dx
    mov ax, dx
    shl ax, 1
    add ax, dx
    mov [es:di + QT_T3], ax
    add ax, dx
    add ax, dx
    mov [es:di + QT_T5], ax
    add ax, dx
    add ax, dx
    mov [es:di + QT_T7], ax
    pop ax
    ret

; px_qinit - the quantiser's level tables, once at launch: q6[v] = (5v + 127)
; div 255 and q7[v] = (6v + 127) div 255 (SPEC.md 106.8). Preserves all
px_qinit:
    push ax
    push bx
    push cx
    push dx
    xor bx, bx
.l:
    mov ax, bx
    mov cx, 5
    mul cx
    add ax, 127
    mov cx, 255
    div cx
    mov [px_q6 + bx], al
    mov ax, bx
    mov cx, 6
    mul cx
    add ax, 127
    mov cx, 255
    div cx
    mov [px_q7 + bx], al
    inc bx
    cmp bx, 256
    jb .l
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_errclear - the error row to zero, before a picture's first row
px_errclear:
    push ax
    push cx
    push di
    push es
    cmp byte [px_tquant], 0
    je .out
    mov es, [px_wseg]
    mov di, [px_woerr]
    mov cx, [px_cur + PXR_MW]
    add cx, 2
    mov ax, cx
    shl cx, 1
    add cx, ax                      ; 3 (mw + 2) words
    xor ax, ax
    cld
    rep stosw
    call px_qtabs                   ; ...and its tables
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

; --- the cube's tables (SPEC.md 106.8) ------------------------------------------
px_r6:      db 0, 51, 102, 153, 204, 255
px_r7:      db 0, 43, 85, 128, 170, 213, 255
px_gneut:   db 0, 1, 2, 4, 5, 6
