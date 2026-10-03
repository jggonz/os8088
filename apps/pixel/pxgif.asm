; =============================================================================
; os8088 - apps/pixel/pxgif.asm
;
; PiXEL's GIF DECODER, a far-called lazy PART (SPEC.md 106.5, 106.18): part 1
; of PIXEL.O88. HEAD reads the logical screen from the head on the UI task;
; DECODE runs on the worker, walks the blocks to the first image, sets the
; palette, and decodes its LZW through apps/os88lzw.inc into rows, which go
; to the package's ONE row emitter through the K_EMIT service. The rest of
; the screen is the background index. tools/pixelsim.py's gif_header and
; gif_rows are this file in Python, check for check.
;
; apps/pixel/pxpart.inc's four rules hold: its own variables through DS =
; CS (this part's own memory - DS is set to it for the whole decode, which
; is rule 1's case), the package only through the context and the services,
; never a word said, and every answer a PXD_* in CF/AX.
; =============================================================================

%include "pxpart.inc"
%include "pxrec.inc"
%include "pxlink.inc"                ; LINKED: the package's variables (106.20)
%include "os88api.inc"               ; (OSAPI_TASK_ALIVE, for the plans)

    cpu 8086
    bits 16
    org 0

    PXPART_HEAD pg_init, pg_decode, pg_info, pg_head, pg_plans

PG_DONE     equ 0x7FFF              ; LZW_RUN's "the image is whole": no PXD_*

%define LZW_BSS     pg_lzw
%define LZW_FILL    pg_lfill
%define LZW_RUN     pg_lrun
%define LZW_EBAD    PXD_DATA

; PXV_INIT - out AX = PXP_PROBE
pg_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO - nothing to say
pg_info:
    mov ax, PXE_NOTSUP
    stc
    retf

; PXV_PLANS - [px_plan] for the picture's palette (SPEC.md 106.20): the
; shared source's, apps/pixel/pxplan.inc
pg_plans:
    call pl_plans
    retf


; =============================================================================
; PXV_HEAD - the UI task: DS = the package, DI = the context. The logical
; screen IS the picture (SPEC.md 106.18), and its size is in the first 13
; bytes, so every head has it
; =============================================================================
pg_head:
    push es
    mov es, [di + PXK_HSEG]
    mov ax, PXD_HEAD
    cmp word [di + PXK_HLEN], 13
    jb .no
    mov ax, [es:6]
    mov dx, [es:8]
    mov [di + PXK_SW], ax
    mov [di + PXK_SH], dx
    call pg_dimok
    jc .dims
    mov ax, dx
    call pg_dimok
    jc .dims
    mov byte [di + PXK_RF], RF_IDX
    mov byte [di + PXK_PACK], PK_LZW
    mov word [di + PXK_DPARA], LZW_KB * 64
    mov al, [es:10]
    mov word [di + PXK_NPAL], 0
    mov byte [di + PXK_BITS], 8
    test al, 0x80
    jz .ok
    and al, 7
    inc al
    mov [di + PXK_BITS], al
    mov cl, al
    mov ax, 1
    shl ax, cl
    mov [di + PXK_NPAL], ax
.ok:
    pop es
    clc
    retf
.dims:
    mov ax, PXD_DIMS
.no:
    pop es
    stc
    retf

; pg_dimok - AX = a dimension: CF = 1 unless 1..PX_DIMMAX. Preserves all
pg_dimok:
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
; PXV_DECODE - the worker: DS = the package, DI = the context
; =============================================================================
pg_decode:
    push bp
    push ds
    mov [cs:pg_ctx], di
    mov [cs:pg_pkg], ds
    mov ax, [di + PXK_SW]
    mov [cs:pg_sw], ax
    mov ax, [di + PXK_SH]
    mov [cs:pg_sh], ax
    mov al, [di + PXK_SCL]
    mov [cs:pg_scl], al
    mov ax, [di + PXK_RSEG]
    mov [cs:pg_rseg], ax
    mov ax, [di + PXK_DSEG]
    mov [cs:pg_dseg], ax
    mov ax, [di + PXK_SPAL]
    mov [cs:pg_spal], ax
    xor bx, bx                      ; the services, as far pointers
    xor si, si
.svc:
    mov ax, [di + PXK_SVC + bx]
    mov [cs:pg_svc + si], ax
    mov [cs:pg_svc + si + 2], ds
    add si, 4
    inc bx
    inc bx
    cmp bx, 10
    jb .svc
    push cs
    pop ds                          ; DS = this part, for the whole decode
    cld
    mov [pg_sp], sp                 ; ...and the way out from any depth
    xor ax, ax
    mov [pg_iend], ax
    mov [pg_ipos], ax
    mov [pg_prog], ax
    mov [pg_trans], al
    mov [pg_blk], al
    mov [pg_blkend], al
    mov [pg_ilace], al
    call pg_body
    jmp short pg_ret

; pg_fail - AX = a PXD_*: out of the decode from wherever it is
pg_fail:
    ; STKBALANCE-OK: the decode's way out from any depth - SP is put back to
    ; pg_decode's own frame from [pg_sp], so what was pushed between there
    ; and here is abandoned, task_exit's shape; pg_ret's retf is pg_decode's
    mov sp, [cs:pg_sp]
    stc
pg_ret:
    pop ds
    pop bp
    retf

; pg_body - the decode proper. out CF = 0, or CF = 1 AX = a PXD_* (which
; pg_fail also delivers from any depth)
pg_body:
    ; --- the header and the global table --------------------------------
    mov di, pg_hdr
    mov cx, 13
.h:
    call pg_rbt
    mov [di], al
    inc di
    loop .h
    mov al, [pg_hdr + 10]
    mov byte [pg_gtab], 0
    test al, 0x80
    jz .blk
    mov byte [pg_gtab], 1
    call pg_tsize                   ; CX = entries
    mov [pg_gn], cx
    call pg_pal                     ; into [px_spal]
    ; --- the blocks before the image -----------------------------------
.blk:
    call pg_rbt
    cmp al, 0x2C
    je .img
    cmp al, 0x21
    je .ext
    mov ax, PXD_DATA                ; the trailer, or anything else
    jmp pg_fail
.ext:
    call pg_rbt
    mov [pg_lab], al
.sub:
    call pg_rbt                     ; a sub-block's length
    or al, al
    jz .blk
    mov cl, al
    xor ch, ch
    cmp byte [pg_lab], 0xF9         ; a Graphic Control Extension sets (or
    jne .skip                       ; clears) the transparent index
    cmp cx, 4
    jb .skip
    call pg_rbt
    and al, 1
    mov [pg_trans], al
    call pg_rbt
    call pg_rbt
    call pg_rbt
    mov [pg_tidx], al
    sub cx, 4
.skip:
    jcxz .sub
.sk:
    call pg_rbt
    loop .sk
    jmp short .sub
    ; --- the image descriptor --------------------------------------------
.img:
    mov di, pg_idesc
    mov cx, 9
.d:
    call pg_rbt
    mov [di], al
    inc di
    loop .d
    mov ax, PXD_DATA
    cmp word [pg_fw], 0
    je .bad
    cmp word [pg_fh], 0
    jne .size
.bad:
    jmp pg_fail
.size:
    mov al, [pg_fpk]
    test al, 0x80
    jz .glob
    call pg_tsize                   ; a local table wins
    mov [pg_gn], cx
    and al, 7
    inc al
    call pg_ctxb                    ; ES:BX = the context
    mov [es:bx + PXK_BITS], al
    call pg_pal
    jmp short .havep
.glob:
    cmp byte [pg_gtab], 0
    jne .havep
    mov ax, PXD_DATA                ; no table anywhere
    jmp pg_fail
.havep:
    call pg_ctxb
    mov ax, [pg_gn]
    mov [es:bx + PXK_NPAL], ax
    mov al, [pg_hdr + 11]           ; the background: the screen's index...
    cmp byte [pg_trans], 0
    je .bgi
    mov al, [pg_tidx]               ; ...or the transparent one, whose entry
    push di                         ; is the view's background (106.18)
    mov ah, 0
    mov di, ax
    shl di, 1
    add di, ax
    add di, [pg_spal]
    mov es, [pg_pkg]
    mov byte [es:di], PXP_BG
    mov byte [es:di + 1], PXP_BG
    mov byte [es:di + 2], PXP_BG
    pop di
    mov ah, 0                       ; a key past the table (old encoders) is
    cmp ax, [pg_gn]                 ; still a colour: NPAL covers it, or the
    jb .bgi                         ; plans draw it black at 1/1 (wave-3
    push ax                         ; review F2)
    inc ax
    call pg_ctxb
    mov [es:bx + PXK_NPAL], ax
    pop ax
.bgi:
    mov [pg_bgi], al
    mov byte [pg_par], 0
    test byte [pg_fpk], 0x40
    jz .noil
    mov byte [pg_ilace], 1
    call pg_ctxb
    mov byte [es:bx + PXK_PACK], PK_LZWI
    mov al, [pg_top]                ; THE PARITY below 1/1: the last pass's
    inc al                          ; rows, top + 1, top + 3, ...
    and al, 1
    mov [pg_par], al
    mov cl, [pg_scl]                ; ...summed 2^(s-1) a block
    or cl, cl
    jz .noil
    dec cl
    mov al, 1
    shl al, cl
    mov [es:bx + PXK_RBLK], al
.noil:
    mov bx, PXS_PAL * 4             ; the palette is known: the tables
    call pg_svcall
    jc .ab
    ; --- the rows above the image ------------------------------------------
    xor ax, ax
    mov [pg_y], ax
.above:
    mov ax, [pg_y]
    cmp ax, [pg_top]
    jae .frame
    cmp ax, [pg_sh]
    jae .frame
    call pg_bgrow
    jc .ab
    inc word [pg_y]
    jmp short .above
.ab:
    jmp pg_fail
    ; --- the image ---------------------------------------------------------
.frame:
    call pg_rbt                     ; the minimum code size
    mov [pg_min], al
    xor ax, ax
    mov [pg_fcol], ax
    mov [pg_fi], ax
    mov [pg_fy], ax
    mov [pg_pass], al
    call pg_fill                    ; the first row's ground
    mov al, [pg_min]
    mov es, [pg_dseg]
    call lzw_decode
    jnc .short                      ; ended (End code or input) too soon
    cmp ax, PG_DONE
    jne .ab
    ; --- the rows below ----------------------------------------------------
    mov ax, [pg_top]
    add ax, [pg_fh]
    jc .done
    mov [pg_y], ax
.below:
    mov ax, [pg_y]
    cmp ax, [pg_sh]
    jae .done
    call pg_bgrow
    jc .ab
    inc word [pg_y]
    jmp short .below
.done:
    clc
    ret
.short:
    mov ax, PXD_TRUNC
    jmp pg_fail

; pg_ctxb - ES:BX = the context. Preserves all else
pg_ctxb:
    mov es, [pg_pkg]
    mov bx, [pg_ctx]
    ret

; pg_tsize - AL = a packed byte: CX = 2 << (AL & 7). Preserves all else
pg_tsize:
    push ax
    and al, 7
    mov cl, al
    mov ax, 2
    shl ax, cl
    mov cx, ax
    pop ax
    ret

; pg_pal - CX entries of the stream into [px_spal], the rest of its 256
; black. Preserves all but CX
pg_pal:
    push ax
    push di
    push es
    mov es, [pg_pkg]
    mov di, [pg_spal]
    push cx
.e:
    call pg_rbt
    stosb
    call pg_rbt
    stosb
    call pg_rbt
    stosb
    loop .e
    pop cx
    mov ax, 256
    sub ax, cx
    mov cx, ax
    add cx, ax
    add cx, ax
    xor al, al
    rep stosb
    pop es
    pop di
    pop ax
    ret

; =============================================================================
; THE STREAM: K_NEXT's windows, a byte at a time
; =============================================================================

; pg_rbt - AL = the next byte; the file ending is `cut short` (pg_fail).
; Preserves all but AL
pg_rbt:
    call pg_rb
    jc .end
    ret
.end:
    mov ax, PXD_TRUNC
    jmp pg_fail

; pg_rb - AL = the next byte, CF = 1 at the end. Preserves all but AL
pg_rb:
    push si
    mov si, [pg_ipos]
    cmp si, [pg_iend]
    jb .get
    call pg_next
    jc .end
    mov si, [pg_ipos]
.get:
    push ds
    mov ds, [pg_iseg]
    lodsb
    pop ds
    mov [pg_ipos], si
    pop si
    clc
    ret
.end:
    pop si
    ret

; pg_next - K_NEXT: the next window, or CF = 1. Preserves all
pg_next:
    push bx
    push cx
    push si
    push es
    mov bx, PXS_NEXT * 4
    call pg_svcall                  ; ES:SI, CX
    jc .out
    mov [pg_iseg], es
    mov [pg_ipos], si
    add si, cx
    mov [pg_iend], si
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; pg_svcall - far-call service BX / 4 (BX = the service x 4). Whatever it
; takes and answers, it takes and answers
pg_svcall:
    call far [pg_svc + bx]
    ret

; =============================================================================
; THE LZW's two callbacks (apps/os88lzw.inc)
; =============================================================================

; pg_lfill - LZW_FILL: the sub-blocks' bytes into ES:DI, CX of room. out CX =
; the bytes put there; 0 once the block terminator or the file's end has
; been met
pg_lfill:
    push ax
    push bx
    push dx
    push si
    xor bx, bx                      ; BX = put so far
.l:
    cmp byte [pg_blkend], 0
    jne .done
    cmp byte [pg_blk], 0
    jne .copy
    call pg_rb                      ; a sub-block's length
    jc .stop
    or al, al
    jz .stop                        ; the terminator
    mov [pg_blk], al
.copy:
    mov si, [pg_ipos]
    cmp si, [pg_iend]
    jb .have
    call pg_next
    jc .stop
    mov si, [pg_ipos]
.have:
    mov ax, [pg_iend]
    sub ax, si                      ; the window's bytes...
    mov dl, [pg_blk]
    xor dh, dh
    cmp ax, dx                      ; ...the block's...
    jbe .a1
    mov ax, dx
.a1:
    mov dx, cx
    sub dx, bx                      ; ...the room's
    cmp ax, dx
    jbe .a2
    mov ax, dx
.a2:
    push cx
    mov cx, ax
    push ds
    mov ds, [pg_iseg]
    cld
    rep movsb
    pop ds
    pop cx
    mov [pg_ipos], si
    sub [pg_blk], al
    add bx, ax
    cmp bx, cx
    jb .l
    jmp short .done
.stop:
    mov byte [pg_blkend], 1
.done:
    mov cx, bx
    pop si
    pop dx
    pop bx
    pop ax
    ret

; pg_lrun - LZW_RUN: ES:SI = a string of CX indices, into the image's rows.
; A row complete goes to the emitter; the image complete answers CF = 1 AX =
; PG_DONE, and anything a service refuses is that refusal
pg_lrun:
    mov ax, [pg_fcol]               ; THE COMMON CASE, a string inside the
    mov bx, ax                      ; row and inside the screen: one copy
    add ax, cx
    jc .chunk
    cmp ax, [pg_fw]
    jae .chunk                      ; (it ends the row: the general path)
    add bx, [pg_left]
    jc .chunk
    mov dx, bx
    add dx, cx
    jc .chunk
    cmp dx, [pg_sw]
    ja .chunk
    mov [pg_fcol], ax
    mov di, bx
    push ds
    push es
    pop ds                          ; DS:SI = the table's string
    mov es, [cs:pg_rseg]            ; ES:DI = the row buffer
    rep movsb
    push ds
    pop es
    pop ds
    clc
    ret
.chunk:
    mov ax, [pg_fw]
    sub ax, [pg_fcol]               ; what is left of this image row (> 0)
    cmp ax, cx
    jbe .take
    mov ax, cx
.take:                              ; AX = this chunk
    mov bx, [pg_left]
    add bx, [pg_fcol]               ; BX = its first screen column
    jc .placed
    mov dx, [pg_sw]
    sub dx, bx                      ; ...and how much of it the screen keeps
    jbe .placed
    cmp dx, ax
    jbe .copy
    mov dx, ax
.copy:
    push cx
    push si
    push ds
    push es
    mov cx, dx
    mov di, bx
    push es
    pop ds                          ; DS:SI = the table's string
    mov es, [cs:pg_rseg]            ; ES:DI = the row buffer
    cld
    rep movsb
    pop es
    pop ds
    pop si
    pop cx
.placed:
    add si, ax
    sub cx, ax
    add [pg_fcol], ax
    mov ax, [pg_fcol]
    cmp ax, [pg_fw]
    jb .more
    call pg_frow                    ; a row is whole
    jc .out
.more:
    or cx, cx
    jnz .chunk
    clc
.out:
    ret

; pg_frow - the image row [pg_fy] is whole: to the emitter at its screen row,
; then the next row of its pass. out CF = 1 AX = PG_DONE when that was the
; last, or a service's refusal. Preserves CX, SI, ES
pg_frow:
    push cx
    push si
    push es
    mov ax, [pg_top]
    add ax, [pg_fy]
    jc .gone                        ; past the screen: decoded, not shown
    cmp ax, [pg_sh]
    jae .gone
    mov bx, 0xFFFF                  ; the rows complete from the top: as the
    cmp byte [pg_ilace], 0          ; emitter counts them, except for an
    je .emit                        ; interlaced image at 1/1
    cmp byte [pg_scl], 0
    jne .emit
    mov bx, [pg_top]                ; passes 1-3: only the image's top row
    inc bx
    cmp byte [pg_pass], 3
    jb .clamp
    mov dx, [pg_fy]                 ; pass 4: two rows at a time, up to
    add dx, 2                       ; [pg_fy] + 1 and the even one below it
    cmp dx, [pg_fh]
    jbe .p4
    mov dx, [pg_fh]
.p4:
    mov bx, [pg_top]
    add bx, dx
.clamp:
    cmp bx, [pg_sh]
    jbe .emit
    mov bx, [pg_sh]
.emit:
    call pg_emit
    jc .out
    jmp short .next
.gone:
    call pg_tick
    jc .out
.next:
    inc word [pg_fi]                ; the next row of the image, in the
    mov ax, [pg_fi]                 ; stream's order
    cmp ax, [pg_fh]
    jae .done
    mov word [pg_fcol], 0
    mov al, [pg_pass]
    mov bl, al
    xor bh, bh
    mov ax, 1                       ; not interlaced: the next row
    cmp byte [pg_ilace], 0
    je .step
    mov al, [pg_step + bx]
.step:
    add [pg_fy], ax
.chk:
    mov ax, [pg_fy]
    cmp ax, [pg_fh]
    jb .fill
    inc byte [pg_pass]              ; this pass is done: the next one's start
    mov bl, [pg_pass]               ; (a pass starting past the bottom is
    cmp bl, 4                       ; empty, and so is the next)
    jae .done
    xor bh, bh
    mov al, [pg_start + bx]
    xor ah, ah
    mov [pg_fy], ax
    jmp short .chk
.fill:
    call pg_fill
    clc
    jmp short .out
.done:
    mov ax, PG_DONE
    stc
.out:
    pop es
    pop si
    pop cx
    ret

; pg_bgrow - screen row [pg_y] outside the image: all background, to the
; emitter. CF/AX as a service's
pg_bgrow:
    mov word [pg_fcol], 0xFFFF      ; (pg_fill: the whole row, always)
    call pg_fill
    mov ax, [pg_y]
    mov bx, 0xFFFF
    ; fall into pg_emit

; pg_emit - AX = a screen row, in the row buffer: to K_EMIT with BX's rows
; complete - unless it is an interlaced picture below 1/1 and a row of the
; other parity (106.18), which is only progress. CF/AX as the service's
pg_emit:
    inc word [pg_prog]
    cmp byte [pg_ilace], 0
    je .go
    cmp byte [pg_scl], 0
    je .go
    push ax
    xor al, [pg_par]
    test al, 1
    pop ax
    jnz pg_tick.go
.go:
    mov dx, [pg_prog]
    push si
    mov si, PXS_EMIT * 4
    call far [pg_svc + si]
    pop si
    ret

; pg_tick - a row decoded and not shown: progress only
pg_tick:
    inc word [pg_prog]
.go:
    mov ax, [pg_prog]
    mov bx, PXS_TICK * 4
    jmp pg_svcall

; pg_fill - the row buffer's ground: the background index across the screen,
; unless the image covers every column (and [pg_fcol] is not FFFFh, which
; asks for the whole row). Preserves all
pg_fill:
    push ax
    push cx
    push di
    push es
    cmp word [pg_fcol], 0xFFFF
    je .all
    cmp word [pg_left], 0
    jne .all
    mov ax, [pg_fw]
    cmp ax, [pg_sw]
    jae .out
.all:
    mov es, [pg_rseg]
    xor di, di
    mov cx, [pg_sw]
    mov al, [pg_bgi]
    cld
    rep stosb
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret

%include "os88lzw.inc"

; --- the passes of an interlaced image (GIF89a, appendix E) -------------------
pg_start:   db 0, 4, 2, 1
pg_step:    db 8, 8, 4, 2

; --- this part's own memory: it is code in a claim of its own, so its state
; lives in it (rule 1), fetched fresh with the part -----------------------------
pg_ctx:     dw 0                    ; the context's offset in the package
pg_pkg:     dw 0                    ; the package's segment, this call
pg_sp:      dw 0                    ; the stack at pg_body's call
pg_svc:     times 10 dw 0           ; the five services, far
pg_sw:      dw 0                    ; the screen
pg_sh:      dw 0
pg_scl:     db 0
pg_rseg:    dw 0                    ; the row buffer
pg_dseg:    dw 0                    ; the LZW's table segment
pg_spal:    dw 0                    ; [px_spal]'s offset
pg_iseg:    dw 0                    ; the window: its segment...
pg_ipos:    dw 0                    ; ...where...
pg_iend:    dw 0                    ; ...and its end
pg_hdr:     times 13 db 0           ; GIF8?a, the screen, flags, bg, aspect
pg_gtab:    db 0                    ; there is a global table
pg_gn:      dw 0                    ; ...the table's entries
pg_lab:     db 0
pg_trans:   db 0                    ; the transparent index is in force...
pg_tidx:    db 0                    ; ...and is this one
pg_bgi:     db 0                    ; the background index
pg_idesc:                           ; the image descriptor, as read
pg_left:    dw 0
pg_top:     dw 0
pg_fw:      dw 0
pg_fh:      dw 0
pg_fpk:     db 0
pg_min:     db 0
pg_ilace:   db 0
pg_par:     db 0                    ; the parity below 1/1
pg_pass:    db 0
pg_fcol:    dw 0                    ; the image row's next column...
pg_fi:      dw 0                    ; ...rows done, in the stream's order...
pg_fy:      dw 0                    ; ...and the row being filled
pg_y:       dw 0                    ; a screen row outside the image
pg_prog:    dw 0                    ; rows done, for the progress
pg_blk:     db 0                    ; the sub-block's bytes left
pg_blkend:  db 0                    ; the terminator (or the end) is met
pg_lzw:     times LZW_BSSSZ db 0

%include "pxplan.inc"                ; the plans: shared source (106.20)
