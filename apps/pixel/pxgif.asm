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
%include "pxanim.inc"                ; a GIF that plays (106.25)
%include "pxed.inc"                  ; (UK_NONE)
%include "pxsvc.inc"                 ; the UI services (106.24)
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
%define LZW_RUN1    pg_lrun1
%define LZW_EBAD    PXD_DATA

; PXV_INIT - out AX = PXP_PROBE
pg_init:
    mov ax, PXP_PROBE
    clc
    retf

; PXV_INFO - CL = an animation verb (AV_*, apps/pixel/pxanim.inc): the UI
; task's half of a GIF that plays (SPEC.md 106.25). DS = the package
pg_info:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    cmp word [8], PXL_IMAGE         ; THE STAMP (106.20)
    jne .link
    cmp word [10], PXL_BSS
    jne .link
    mov ax, [px_svgp]               ; the resident's UI services (pxsvc.inc)
    mov [cs:pv_sv], ax
    mov ax, [cs:PXP_PKG]
    mov [cs:pv_sv + 2], ax
    cld
    mov bl, cl
    and bx, 3
    shl bx, 1
    call [cs:av_tab + bx]
    clc
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
av_tab:     dw av_init, av_tick, av_wake, av_toggle

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
    cmp byte [px_job], JOB_ANIM     ; A FRAME'S JOB (106.25): the services
    jne .ctx                        ; are the context's, whatever DI says
    mov di, px_k
.ctx:
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
    mov [pg_anim], al
    mov [pa_fdisp], al              ; frame 0's GCE, the loop count, the
    mov [pa_fdly], ax               ; animated flag (106.25)
    mov [pa_hasloop], al
    mov [pa_isanim], al
    mov es, [pg_pkg]
    cmp byte [es:px_job], JOB_ANIM
    jne .dec
    call pa_job
    jmp short pg_ret
.dec:
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
    mov word [pa_an0], 13
    mov word [pa_an0 + 2], 0
    mov al, [pg_hdr + 10]
    mov byte [pg_gtab], 0
    test al, 0x80
    jz .blk
    mov byte [pg_gtab], 1
    call pg_tsize                   ; CX = entries
    mov [pg_gn], cx
    call pg_pal                     ; into [px_spal]
    call pa_off                     ; (frame 0's blocks start here: a pass
    mov [pa_an0], ax                ; of the animation begins again here,
    mov [pa_an0 + 2], dx            ; 106.25)
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
    mov byte [pa_subn], 0
.sub:
    call pg_rbt                     ; a sub-block's length
    or al, al
    jz .blk
    mov cl, al
    xor ch, ch
    inc byte [pa_subn]
    cmp byte [pg_lab], 0xFF         ; NETSCAPE2.0's loop count (106.25)
    jne .gce
    call pa_netscape
    jc .tr
    jmp short .skip
.gce:
    cmp byte [pg_lab], 0xF9         ; a Graphic Control Extension sets (or
    jne .skip                       ; clears) the transparent index - and
    cmp cx, 4                       ; frame 0's disposal and delay (106.25)
    jb .skip
    call pa_gce
    jc .tr
    sub cx, 4
.skip:
    jcxz .sub
.sk:
    call pg_rbt
    loop .sk
    jmp short .sub
.tr:
    mov ax, PXD_TRUNC
    jmp pg_fail
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
    mov ax, [pg_sw]                 ; [pg_flim]: a string ending before it
    sub ax, [pg_left]               ; is inside the row and the screen -
    jbe .fl0                        ; min(fw, sw - left + 1), 0 when the
    inc ax                          ; image starts past the screen
    cmp ax, [pg_fw]
    jbe .fl1
    mov ax, [pg_fw]
.fl1:
    mov [pg_flim], ax
    jmp short .fl2
.fl0:
    mov word [pg_flim], 0
.fl2:
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
    call pa_peek                    ; is there more? (never a refusal)
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
; PG_DONE, and anything a service refuses is that refusal. Keeps DX and BP
; (os88lzw.inc's contract)
pg_lrun:
    mov ax, [pg_fcol]               ; THE COMMON CASE, a string inside the
    mov di, ax                      ; row and inside the screen ([pg_flim]):
    add ax, cx                      ; one copy
    jc .gen
    cmp ax, [pg_flim]
    jae .gen
    mov [pg_fcol], ax
    add di, [pg_left]
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
.gen:
    push dx
    push bp
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
    pop bp
    pop dx
    ret

; pg_lrun1 - LZW_RUN1: the one-character string AL, LZW_RUN's way. Keeps
; SI as well (os88lzw.inc's contract)
pg_lrun1:
    mov di, [pg_fcol]
    mov bx, di
    inc bx
    cmp bx, [pg_flim]
    jae .gen
    mov [pg_fcol], bx
    add di, [pg_left]
    push es
    mov es, [cs:pg_rseg]
    stosb
    pop es
    clc
    ret
.gen:
    push si                         ; (LZW_RUN1 keeps SI: the free code)
    mov di, LZW_STK - 1             ; the general path, from the stack
    mov [es:di], al
    mov si, di
    mov cx, 1
    call pg_lrun.gen
    pop si
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
    cmp byte [pg_anim], 0           ; AN ANIMATION'S FRAME (106.25): the row
    je .vis                         ; onto the master, its transparent pixels
    call pa_comp                    ; left
    jmp short .gone
.vis:
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
    cmp byte [pg_anim], 0           ; (an animation's frame has no ground: its
    jne pg_fret                     ; columns are composited, 106.25)
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
pg_fret:
    ret


; =============================================================================
; A GIF THAT PLAYS (SPEC.md 106.25). The first decode reads frame 0 as it
; always did and then looks on, never refusing: is there a second image
; (pa_peek)? The ANIMATION's job (JOB_ANIM) then draws the frames after it
; onto the 1/1 master a frame at a time - the previous frame's disposal, its
; pixels through a colour map, its transparent ones left - and waits between
; them for the UI's word; tools/pixelsim.py's gif_anim is it in Python
; =============================================================================

; pa_off - DX:AX = the file offset of the stream's next byte: the window's
; start ([px_rbase]) and where in it ([pg_ipos]). DS = this part
pa_off:
    push es
    mov es, [pg_pkg]
    mov ax, [es:px_rbase]
    mov dx, [es:px_rbase + 2]
    add ax, [pg_ipos]
    adc dx, 0
    pop es
    ret

; pa_gce - a Graphic Control Extension's four bytes: transparency, the
; disposal, the delay, the transparent index. CF = 1 the file ended
pa_gce:
    call pg_rb
    jc .x
    mov ah, al
    and al, 1
    mov [pg_trans], al
    jz .nt
    push es                         ; a transparent index: more than a
    mov es, [pg_pkg]                ; master holds (review-w8 N1)
    mov byte [es:px_cur + PXR_KEEP], 1
    pop es
.nt:
    mov al, ah
    shr al, 1
    shr al, 1
    and al, 7
    mov [pa_fdisp], al
    call pg_rb
    jc .x
    mov [pa_fdly], al
    call pg_rb
    jc .x
    mov [pa_fdly + 1], al
    call pg_rb
    jc .x
    mov [pg_tidx], al
.x:
    ret

; pa_netscape - an application extension's sub-block [pa_subn], CX bytes
; long: the first exactly 'NETSCAPE2.0', the second 3 bytes or more of 01,
; lo, hi - the loop count (the last such before frame 0 wins). CX left at
; what is still to skip. CF = 1 the file ended
pa_netscape:
    cmp byte [pa_subn], 1
    jne .two
    mov byte [pa_ns], 0
    cmp cx, 11
    jne .ok
    mov byte [pa_ns], 1
    mov si, pa_s_ns
.c:
    call pg_rb
    jc .x
    cmp al, [si]
    je .m
    mov byte [pa_ns], 0
.m:
    inc si
    loop .c
.ok:
    clc
.x:
    ret
.two:
    cmp byte [pa_subn], 2
    jne .ok
    cmp byte [pa_ns], 0
    je .ok
    cmp cx, 3
    jb .ok
    call pg_rb
    jc .x
    mov bl, al
    call pg_rb
    jc .x
    mov [pa_tl], al
    call pg_rb
    jc .x
    mov [pa_tl + 1], al
    sub cx, 3
    cmp bl, 1
    jne .ok
    mov ax, [pa_tl]
    mov [pa_loop], ax
    mov byte [pa_hasloop], 1
    jmp short .ok

; pa_subs - frame's remaining sub-blocks walked to its terminator: the rest
; of the one under way ([pg_blk], unless the terminator was met), then each
; by its length. CF = 1 the file ended inside them
pa_subs:
    cmp byte [pg_blkend], 0
    jne .ok
    mov cl, [pg_blk]
    xor ch, ch
.sk:
    jcxz .len
    call pg_rb
    jc .x
    loop .sk
.len:
    call pg_rb
    jc .x
    mov cl, al
    or al, al
    jnz .sk
.ok:
    clc
.x:
    ret

; pa_peek - after frame 0's last row: animated when a SECOND image follows,
; extensions only between - frame 0's sub-blocks to their terminator, then
; the blocks by their lengths; anything else (the trailer, the file's end, a
; byte that is no block) is a still picture. Never a refusal
pa_peek:
    mov byte [pa_isanim], 0
    call pa_subs
    jc .no
    call pa_off                     ; the next frame's blocks start here
    mov [pa_pos1], ax
    mov [pa_pos1 + 2], dx
.b:
    call pg_rb
    jc .no
    cmp al, 0x2C
    je .yes
    cmp al, 0x21
    jne .no
    call pg_rb                      ; (its label)
    jc .no
    mov byte [pg_blkend], 0
    mov byte [pg_blk], 0
    call pa_subs
    jc .no
    jmp short .b
.yes:
    mov byte [pa_isanim], 1
.no:
    ret

; pa_crect - the frame's rect, clipped to the screen, into [pa_cr]: x1, y1,
; x2, y2 inclusive, x1 FFFFh when none of it is on the screen. CS's words
; throughout (the UI task calls it too). Preserves all but AX, BX, CX, DX
pa_crect:
    mov word [cs:pa_cr], 0xFFFF
    mov ax, [cs:pg_left]
    cmp ax, [cs:pg_sw]
    jae .x
    mov bx, [cs:pg_top]
    cmp bx, [cs:pg_sh]
    jae .x
    mov cx, ax
    add cx, [cs:pg_fw]
    jc .cw
    cmp cx, [cs:pg_sw]
    jbe .c1
.cw:
    mov cx, [cs:pg_sw]
.c1:
    dec cx
    mov dx, bx
    add dx, [cs:pg_fh]
    jc .ch
    cmp dx, [cs:pg_sh]
    jbe .c2
.ch:
    mov dx, [cs:pg_sh]
.c2:
    dec dx
    mov [cs:pa_cr], ax
    mov [cs:pa_cr + 2], bx
    mov [cs:pa_cr + 4], cx
    mov [cs:pa_cr + 6], dx
.x:
    ret

; pa_ticks - AX = a delay in hundredths: AX = ticks of 18.2 a second - less
; than 2 is 10, browsers' rule - at least 1. Preserves all but AX
pa_ticks:
    push cx
    push dx
    cmp ax, 2
    jae .k
    mov ax, 10
.k:
    mov dx, 182
    mul dx
    add ax, 500
    adc dx, 0
    mov cx, 1000
    div cx
    or ax, ax
    jnz .x
    inc ax
.x:
    pop dx
    pop cx
    ret

; --- THE WORKER'S JOB: a frame at a time, from [px_anpos] -------------------
pa_job:
    mov ax, [es:px_cur + PXR_MW]    ; the screen IS the 1/1 master
    mov [pg_sw], ax
    mov ax, [es:px_cur + PXR_MH]
    mov [pg_sh], ax
    mov ax, [es:px_wbase]           ; the work claim: the LZW's tables, then
    mov [pg_dseg], ax               ; the row
    add ax, LZW_KB * 64
    mov [pg_rseg], ax
    mov byte [pg_scl], 0
    mov byte [pg_anim], 1
    test byte [es:px_anflg], ANF_GREAD
    jz .frame
    ; --- THE GLOBAL TABLE, once (frame 0 had its own): the header, then the
    ; table into the animation's claim, then on to the frame it is at
    mov di, pg_hdr
    mov cx, 13
.h:
    call pg_rb
    jc .endp
    mov [di], al
    inc di
    loop .h
    mov al, [pg_hdr + 10]
    call pg_tsize                   ; CX = entries
    mov ax, cx
    shl cx, 1
    add cx, ax
    mov es, [es:px_anseg]
    xor di, di
.g:
    call pg_rb
    jc .endp
    stosb
    loop .g
    mov es, [pg_pkg]
    or byte [es:px_anflg], ANF_GHAVE
    and byte [es:px_anflg], ~ANF_GREAD
.sk:
    call pa_off
    cmp dx, [es:px_anpos + 2]
    jb .s1
    ja .frame
    cmp ax, [es:px_anpos]
    jae .frame
.s1:
    call pg_rb
    jc .endp
    jmp short .sk
.frame:
    call pa_walk                    ; CF = 1: the pass ends
    jc .endp
    call pa_begin                   ; CF = 1: no room for its backup
    jc .ret
    call pa_draw                    ; CF = 1: stopped
    jc .ret
    call pa_rec
    call pa_wait                    ; shown; then the UI's word, or a stop
    jc .ret
    mov es, [pg_pkg]
    test byte [es:px_anflg], ANF_END
    jz .frame
.endp:
    mov es, [pg_pkg]                ; THE STREAM ENDED - or was STOPPED: a
    cmp byte [es:px_abort], 0       ; read refused by the cancel is no end
    jne .ab                         ; of the pass (review-w8 A3)
    xor ax, ax                      ; THE PASS HAS ENDED
    clc
.ret:
    ret
.ab:
    mov ax, PXD_ABORT
    stc
    ret

; pa_walk - the blocks to the next frame (SPEC.md 106.25's order): its GCE
; (reset per frame), its descriptor and local table; a frame of no width or
; height skipped; no table at all, or a minimum code size outside 2..8, and
; the trailer, the file's end or any other byte, END THE PASS (CF = 1)
pa_walk:
    xor ax, ax
    mov [pa_fdisp], al
    mov [pa_fdly], ax
    mov [pg_trans], al
.blk:
    call pg_rb
    jc .end
    cmp al, 0x2C
    je .img
    cmp al, 0x21
    jne .end
    call pg_rb
    jc .end
    mov [pg_lab], al
.sub:
    call pg_rb
    jc .end
    or al, al
    jz .blk
    mov cl, al
    xor ch, ch
    cmp byte [pg_lab], 0xF9
    jne .skip
    cmp cx, 4
    jb .skip
    call pa_gce
    jc .end
    sub cx, 4
.skip:
    jcxz .sub
.sk:
    call pg_rb
    jc .end
    loop .sk
    jmp short .sub
.img:
    mov di, pg_idesc
    mov cx, 9
.d:
    call pg_rb
    jc .end
    mov [di], al
    inc di
    loop .d
    mov byte [pa_haslt], 0
    test byte [pg_fpk], 0x80
    jz .nol
    mov al, [pg_fpk]                ; its local table, padded black
    call pg_tsize
    mov ax, cx
    shl cx, 1
    add cx, ax
    push cx
    push ds
    pop es
    mov di, pa_ltab
    push cx
    mov cx, 768 / 2
    xor ax, ax
    rep stosw
    pop cx
    mov di, pa_ltab
.lt:
    call pg_rb
    jc .end2
    stosb
    loop .lt
    pop cx
    mov byte [pa_haslt], 1
.nol:
    cmp word [pg_fw], 0
    je .zero
    cmp word [pg_fh], 0
    je .zero
    cmp byte [pa_haslt], 0          ; a table: its own, or the global
    jne .t
    mov es, [pg_pkg]
    test byte [es:px_anflg], ANF_GLOB
    jz .end
.t:
    call pg_rb                      ; the minimum code size, 2..8
    jc .end
    mov [pg_min], al
    sub al, 2
    cmp al, 6
    ja .end
    clc
    ret
.zero:
    call pg_rb                      ; SKIPPED: its code size and sub-blocks
    jc .end                         ; by their lengths, nothing read in
    mov byte [pg_blkend], 0         ; them; its GCE forgotten
    mov byte [pg_blk], 0
    call pa_subs
    jc .end
    jmp pa_walk
.end2:
    pop cx
.end:
    stc
    ret

; pa_begin - A FRAME BEGINS: the previous frame's disposal (unless a stopped
; job applied it already), what changes on the glass, this frame's backup
; when its own disposal is 3, its colour map. CF = 1 AX = PXD_MEM, the
; backup's room asked for ([px_anneed])
pa_begin:
    call pa_crect
    mov es, [pg_pkg]
    test byte [es:px_anflg], ANF_DISPD
    jnz .dd
    call pa_dispose
    or byte [es:px_anflg], ANF_DISPD
.dd:
    mov si, pa_cr                   ; the glass changes where it is drawn
    call pa_union
    cmp byte [pa_fdisp], 3          ; ITS OWN DISPOSAL 3: the rect kept
    jne .map                        ; (frame 0's is a fill of the
    cmp word [es:px_anfr], 0        ; background: before it the screen WAS
    je .map                         ; the background, 106.25)
    cmp word [pa_cr], 0xFFFF
    je .map
    test byte [es:px_anflg], ANF_BKD
    jnz .map
    mov si, pa_cr
    mov di, pa_t
    mov cx, 4
.cp:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .cp
    call pa_size                    ; DX:AX = its bytes
    or dx, dx
    jnz .huge
    cmp ax, 0xFFFF - AN_GTAB
    ja .huge
    add ax, AN_GTAB + 1023          ; the claim it needs, KB - the sum in
    mov al, ah                      ; 17 bits: a 320 x 200 frame is 64,000
    mov ah, 0                       ; and its sum carries (review-w8 A1: it
    adc ah, ah                      ; read as 0 KB, and the backup ran ~63
    shr ax, 1                       ; KB past a 1 KB claim)
    shr ax, 1
    cmp ax, [es:px_ankb]
    jbe .room
    mov [es:px_anneed], ax
    mov ax, PXD_MEM
    stc
    ret
.huge:
    mov word [es:px_anneed], 0xFFFF ; (more than a segment: not played)
    mov ax, PXD_MEM
    stc
    ret
.room:
    mov byte [pa_op], 1             ; the rect into the backup
    call pa_rows
    mov es, [pg_pkg]
    or byte [es:px_anflg], ANF_BKD
.map:
    jmp pa_mkmap                    ; (CF = 1 AX = PXD_ABORT: stopped)

; pa_size - [pa_t]'s bytes: DX:AX. Preserves all else
pa_size:
    push cx
    mov ax, [pa_t + 4]
    sub ax, [pa_t]
    inc ax
    mov cx, [pa_t + 6]
    sub cx, [pa_t + 2]
    inc cx
    mul cx
    pop cx
    ret

; pa_dispose - the previous frame's disposal on its rect ([px_andisp],
; [px_andr]): 2 the background index, 3 the backup put back; the rect then
; changes on the glass
pa_dispose:
    mov al, [es:px_andisp]
    cmp al, 2
    je .go
    cmp al, 3
    jne .x
.go:
    cmp word [es:px_andr], 0xFFFF
    je .x
    mov si, px_andr
    mov di, pa_t
    mov cx, 4
.cp:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .cp
    mov si, pa_t
    call pa_union
    mov byte [pa_op], 0             ; 2: filled
    cmp byte [es:px_andisp], 2
    je .r
    mov byte [pa_op], 2             ; 3: put back
.r:
    call pa_rows
    mov es, [pg_pkg]
.x:
    ret

; pa_rows - [pa_t]'s rows of the master: [pa_op] 0 filled with the
; background index, 1 copied into the backup, 2 copied back from it (the
; backup at AN_GTAB in the animation's claim, a row after a row). DS = CS.
; Clobbers AX, BX, CX, DX, SI, DI, ES
pa_rows:
    mov bx, [pa_t + 2]
    mov word [pa_bk], AN_GTAB
.r:
    cmp bx, [pa_t + 6]
    ja .x
    mov ax, bx                      ; the master row's x1: (y w + x1)
    mul word [pg_sw]
    add ax, [pa_t]
    adc dx, 0
    mov di, ax
    and di, 15
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    mov es, [pg_pkg]
    add ax, [es:px_cur + PXR_MSEG]  ; (re-read: it moves only at a park)
    mov cx, [pa_t + 4]
    sub cx, [pa_t]
    inc cx
    cmp byte [pa_op], 1
    je .save
    ja .back
    mov dl, [es:px_anbg]            ; 0: the background
    mov es, ax
    mov al, dl
    rep stosb
    jmp short .n
.save:
    push ds                         ; 1: master -> backup
    mov si, di
    mov di, [cs:pa_bk]
    add [cs:pa_bk], cx
    mov es, [es:px_anseg]
    mov ds, ax
    rep movsb
    pop ds
    jmp short .n
.back:
    push ds                         ; 2: backup -> master
    mov si, [cs:pa_bk]
    add [cs:pa_bk], cx
    mov ds, [es:px_anseg]
    mov es, ax
    rep movsb
    pop ds
.n:
    inc bx
    jmp short .r
.x:
    ret

; pa_union - the rect at CS:SI (x1 FFFFh: none) into what changes on the
; glass, [px_anrect]. Preserves all but AX
pa_union:
    push es
    mov es, [cs:pg_pkg]
    cmp word [cs:si], 0xFFFF
    je .x
    cmp word [es:px_anrect], 0xFFFF
    jne .u
    mov ax, [cs:si]
    mov [es:px_anrect], ax
    mov ax, [cs:si + 2]
    mov [es:px_anrect + 2], ax
    mov ax, [cs:si + 4]
    mov [es:px_anrect + 4], ax
    mov ax, [cs:si + 6]
    mov [es:px_anrect + 6], ax
    jmp short .x
.u:
    mov ax, [cs:si]
    cmp ax, [es:px_anrect]
    jae .a
    mov [es:px_anrect], ax
.a:
    mov ax, [cs:si + 2]
    cmp ax, [es:px_anrect + 2]
    jae .b
    mov [es:px_anrect + 2], ax
.b:
    mov ax, [cs:si + 4]
    cmp ax, [es:px_anrect + 4]
    jbe .c
    mov [es:px_anrect + 4], ax
.c:
    mov ax, [cs:si + 6]
    cmp ax, [es:px_anrect + 6]
    jbe .x
    mov [es:px_anrect + 6], ax
.x:
    pop es
    ret

; pa_mkmap - [pa_map]: the frame's indices onto the master's palette -
; itself for frame 0, and for a frame of the global table when frame 0 was
; too; else each entry of its table (padded black) the nearest of the
; palette's first NPAL by 3 dR^2 + 6 dG^2 + dB^2, the lower on a tie. CF = 1
; AX = PXD_ABORT: stopped, the map not made.
;
; That search is 256 x 256 distances, and an XT paid ~5.8 s a frame for it
; (review-w8 A4) - every frame, since a frame's table is often its own copy
; of the same colours, or the global one after a frame 0 with its own. So:
; the map is KEPT with the table, the palette's size and the picture's
; serial it was made for, and a frame whose three match reuses it; an entry
; that repeats the one before it (the black padding) takes its map; a
; candidate is dropped the moment its partial sum passes the best; and the
; cancel is polled an entry at a time
pa_mkmap:
    mov es, [pg_pkg]
    cmp word [es:px_anfr], 0
    je .id
    cmp byte [pa_haslt], 0
    jne .near
    test byte [es:px_anflg], ANF_LOCAL0
    jnz .glob
.id:
    xor bx, bx
.i:
    mov [pa_map + bx], bl
    inc bl
    jnz .i
    clc
    ret
.glob:
    push ds                         ; the global table, from the claim
    push ds
    pop es
    mov ax, [cs:pg_pkg]
    mov ds, ax
    mov ds, [px_anseg]
    xor si, si
    mov di, pa_ltab
    mov cx, 768 / 2
    rep movsw
    pop ds
.near:
    mov es, [pg_pkg]
    mov ax, [es:px_cur + PXR_NPAL]
    or ax, ax
    jnz .np
    mov ax, 256
.np:
    cmp ax, 256
    jbe .np2
    mov ax, 256
.np2:
    mov [pa_np], ax
    mov dl, [es:px_cur + PXR_SERIAL]
    cmp byte [pa_mok], 0            ; THE MAP KEPT: the same table, palette
    je .new                         ; size and picture
    cmp dl, [pa_mser]
    jne .new
    cmp ax, [pa_mnp]
    jne .new
    push ds
    pop es
    mov si, pa_ltab
    mov di, pa_mtab
    mov cx, 768 / 2
    repe cmpsw
    jne .new
    clc
    ret
.new:
    mov byte [pa_mok], 0
    mov [pa_mser], dl
    mov [pa_mnp], ax
    cmp byte [pa_sqok], 0           ; the squares, once
    jne .sq
    xor bx, bx
.q:
    mov al, bl
    mul al
    shl bx, 1
    mov [pa_sq + bx], ax
    shr bx, 1
    inc bl
    jnz .q
    mov byte [pa_sqok], 1
.sq:
    mov es, [pg_pkg]
    mov si, pa_ltab
    xor di, di                      ; DI = the entry
.e:
    cmp byte [es:px_abort], 0       ; (the cancel, an entry at a time)
    je .e1
    mov ax, PXD_ABORT
    stc
    ret
.e1:
    or di, di                       ; the same colour as the entry before:
    jz .e2                          ; the same map (the black padding)
    mov ax, [si]
    cmp ax, [si - 3]
    jne .e2
    mov al, [si + 2]
    cmp al, [si - 1]
    jne .e2
    mov al, [pa_map + di - 1]
    jmp .done
.e2:
    mov word [pa_best], 0xFFFF
    mov word [pa_best + 2], 0xFFFF
    mov byte [pa_bi], 0
    mov bp, px_pal                  ; ES:BP = the palette's entry
    xor cx, cx
.p:
    mov al, [si]                    ; 3 dR^2
    sub al, [es:bp]
    jnc .r
    neg al
.r:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [pa_sq + bx]
    xor dx, dx
    mov bx, ax
    add ax, bx
    adc dx, 0
    add ax, bx
    adc dx, 0
    cmp dx, [pa_best + 2]           ; (past the best already: dropped)
    ja .n
    jb .r2
    cmp ax, [pa_best]
    jae .n
.r2:
    mov [pa_sum], ax
    mov [pa_sum + 2], dx
    mov al, [si + 1]                ; 6 dG^2
    sub al, [es:bp + 1]
    jnc .g
    neg al
.g:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [pa_sq + bx]
    xor dx, dx
    shl ax, 1                       ; x 2...
    rcl dx, 1
    mov bx, ax
    mov ax, dx                      ; (keep 2 g^2 in AX:BX... thrice)
    add [pa_sum], bx
    adc [pa_sum + 2], ax
    add [pa_sum], bx
    adc [pa_sum + 2], ax
    add [pa_sum], bx
    adc [pa_sum + 2], ax
    mov al, [si + 2]                ; dB^2
    sub al, [es:bp + 2]
    jnc .b
    neg al
.b:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov ax, [pa_sq + bx]
    add [pa_sum], ax
    adc word [pa_sum + 2], 0
    mov ax, [pa_sum]                ; strictly less: the lower wins a tie
    mov dx, [pa_sum + 2]
    cmp dx, [pa_best + 2]
    ja .n
    jb .take
    cmp ax, [pa_best]
    jae .n
.take:
    mov [pa_best], ax
    mov [pa_best + 2], dx
    mov [pa_bi], cl
    or ax, dx                       ; (exact: nothing nearer)
    jz .pd
.n:
    add bp, 3
    inc cx
    cmp cx, [pa_np]
    jb .p
.pd:
    mov al, [pa_bi]
.done:
    mov [pa_map + di], al
    add si, 3
    inc di
    cmp di, 256
    jae .kept
    jmp .e
.kept:
    push ds                         ; made: kept with its table
    pop es
    mov si, pa_ltab
    mov di, pa_mtab
    mov cx, 768 / 2
    rep movsw
    mov byte [pa_mok], 1
    clc
    ret

; pa_draw - the frame's rows: pg_body's decode of an image, in animation
; mode - a whole row composited onto the master as it completes (pg_frow,
; pa_comp). [pa_whole] 1 the frame was whole, 0 it ended part-drawn (the
; stream ended, or a code was damaged: the pass ends after it). CF = 1 AX =
; PXD_ABORT: stopped
pa_draw:
    mov ax, [pg_sw]                 ; pg_body's [pg_flim]
    sub ax, [pg_left]
    jbe .f0
    inc ax
    cmp ax, [pg_fw]
    jbe .f1
    mov ax, [pg_fw]
.f1:
    mov [pg_flim], ax
    jmp short .f2
.f0:
    mov word [pg_flim], 0
.f2:
    xor ax, ax
    mov [pg_fcol], ax
    mov [pg_fi], ax
    mov [pg_fy], ax
    mov [pg_pass], al
    mov [pg_blk], al
    mov [pg_blkend], al
    mov [pg_ilace], al
    test byte [pg_fpk], 0x40
    jz .ni
    mov byte [pg_ilace], 1
.ni:
    mov byte [pa_whole], 0
    mov al, [pg_min]
    mov es, [pg_dseg]
    call lzw_decode
    jnc .sub                        ; the stream ended first
    cmp ax, PXD_ABORT
    je .ab
    cmp ax, PG_DONE
    jne .sub                        ; damaged: part-drawn
    mov byte [pa_whole], 1
.sub:
    call pa_subs                    ; on to its terminator (the file may end)
    mov es, [pg_pkg]                ; ...unless it was the CANCEL that ended
    cmp byte [es:px_abort], 0       ; the stream: the frame begins again
    jne .ab0                        ; later, from its start (review-w8 A3)
    clc
    ret
.ab0:
    mov ax, PXD_ABORT
.ab:
    stc
    ret

; pa_comp - screen row AX of the frame is whole in the row buffer: its
; columns on the screen onto the master row, a transparent index left, every
; other through [pa_map]. Preserves all
pa_comp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov bx, [pg_left]
    mov cx, bx                      ; CX = the end: min(left + w, screen)
    add cx, [pg_fw]
    jc .cw
    cmp cx, [pg_sw]
    jbe .c1
.cw:
    mov cx, [pg_sw]
.c1:
    sub cx, bx
    jbe .x
    mul word [pg_sw]                ; the master row's left: y w + left
    add ax, bx
    adc dx, 0
    mov di, ax
    and di, 15
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    pop cx
    mov es, [pg_pkg]
    add ax, [es:px_cur + PXR_MSEG]
    mov es, ax                      ; ES:DI = the master
    mov si, bx                      ; DS:SI = the row buffer's columns
    mov ah, [pg_trans]
    mov dl, [pg_tidx]
    mov bx, pa_map
    mov ds, [cs:pg_rseg]
    cld
.p:
    lodsb
    or ah, ah
    jz .w
    cmp al, dl
    jne .w
    inc di                          ; transparent: left as it is
    loop .p
    jmp short .x
.w:
    cs xlatb
    stosb
    loop .p
.x:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; pa_rec - the frame is drawn: what its successor needs - its disposal (frame
; 0's 3 a fill of the background), its rect, its delay - where the next one
; starts, and whether this one ended the pass
pa_rec:
    mov es, [pg_pkg]
    mov al, [pa_fdisp]
    cmp word [es:px_anfr], 0
    jne .d
    cmp al, 3
    jne .d
    mov al, 2
.d:
    mov [es:px_andisp], al
    mov si, pa_cr
    mov di, px_andr
    mov cx, 4
.cp:
    mov ax, [si]
    mov [es:di], ax
    add si, 2
    add di, 2
    loop .cp
    mov ax, [pa_fdly]
    call pa_ticks
    mov [es:px_andly], ax
    call pa_off
    mov [es:px_anpos], ax
    mov [es:px_anpos + 2], dx
    inc word [es:px_anfr]
    and byte [es:px_anflg], ~(ANF_DISPD | ANF_BKD)
    cmp byte [pa_whole], 0
    jne .x
    or byte [es:px_anflg], ANF_END
.x:
    ret

; pa_wait - the frame is ready: the UI is woken to paint it, and the worker
; sleeps a tick at a time until it says go - or stop. CF = 1 AX = PXD_ABORT
pa_wait:
    push ds
    mov ds, [cs:pg_pkg]
    mov byte [px_anrun], 2
    mov bx, [px_win]
    call OSAPI_WM_WAKE
.w:
    cmp byte [px_abort], 0
    jne .ab
    cmp byte [px_ango], 0
    jne .go
    mov ax, 1
    call OSAPI_TASK_SLEEP
    mov bx, [px_win]
    call OSAPI_TASK_ALIVE
    jmp short .w
.go:
    mov byte [px_ango], 0
    mov byte [px_anrun], 1
    pop ds
    clc
    ret
.ab:
    pop ds
    mov ax, PXD_ABORT
    stc
    ret

; =============================================================================
; THE UI TASK's HALF (PXV_INFO's verbs): DS = the package. It runs beside a
; worker waiting inside this same part between frames, so it touches none of
; the worker's own words - only the package's and its own (av_*)
; =============================================================================

; AV_INIT - a picture of ours was decoded and is shown: animated, at 1/1 and
; read from the disk (an in-memory file has no stream to come back to), it
; plays - frame 0 is on the glass, frame 1 next
av_init:
    cmp byte [cs:pa_isanim], 0
    je .x
    mov byte [px_cur + PXR_KEEP], 1 ; (its frames: a Save over it is a Save
                                    ; As, review-w8 N1 - played or not)
    cmp byte [px_anoff], 0          ; (a test's: frame 0 stays)
    jne .x
    cmp byte [px_cur + PXR_SCL], 0
    jne .x
    cmp byte [px_rflat], 0
    jne .x
    mov byte [px_anim], 1
    mov byte [px_anon], 1
    mov byte [px_anuser], 0
    mov ax, [cs:pa_pos1]
    mov [px_anpos], ax
    mov ax, [cs:pa_pos1 + 2]
    mov [px_anpos + 2], ax
    mov ax, [cs:pa_an0]
    mov [px_an0], ax
    mov ax, [cs:pa_an0 + 2]
    mov [px_an0 + 2], ax
    mov word [px_anfr], 1
    mov al, [cs:pa_fdisp]           ; frame 0's disposal (3: the background)
    cmp al, 3
    jne .d
    mov al, 2
.d:
    mov [px_andisp], al
    call pa_crect                   ; ...and its rect
    mov ax, [cs:pa_cr]
    mov [px_andr], ax
    mov ax, [cs:pa_cr + 2]
    mov [px_andr + 2], ax
    mov ax, [cs:pa_cr + 4]
    mov [px_andr + 4], ax
    mov ax, [cs:pa_cr + 6]
    mov [px_andr + 6], ax
    mov ax, [cs:pa_fdly]
    call pa_ticks
    mov [px_andly], ax
    call OSAPI_GET_TICKS
    add ax, [px_andly]
    mov [px_andue], ax
    mov byte [px_anrun], 3          ; (frame 0: shown, its delay running)
    xor ax, ax                      ; the passes after this one: none without
    cmp byte [cs:pa_hasloop], 0     ; NETSCAPE2.0, forever (FFFFh) for 0,
    je .l                           ; else its count
    mov ax, [cs:pa_loop]
    or ax, ax
    jnz .l
    dec ax
.l:
    mov [px_anloop], ax
    mov [px_anl0], ax
    mov al, [cs:pg_bgi]
    mov [px_anbg], al
    xor al, al
    test byte [cs:pg_fpk], 0x80
    jz .g
    or al, ANF_LOCAL0
.g:
    cmp byte [cs:pg_gtab], 0
    je .f
    or al, ANF_GLOB
    test al, ANF_LOCAL0             ; frame 0 had its own: the global is read
    jz .f                           ; into the claim by the first job
    or al, ANF_GREAD
.f:
    mov [px_anflg], al
    mov word [px_anrect], 0xFFFF
    PSV SV_FLAGS
.x:
    ret

; AV_TICK - the timer, the window in front and the animation playing: an
; edit has made the picture a still (for good); else what is owed the glass
; painted, a job started when none runs, and the next frame let go when the
; one shown is due
av_tick:
    cmp byte [px_dirty], 0
    jne .still
    cmp byte [px_ukind], UK_NONE
    jne .still
    cmp byte [px_anon], 0
    je .x
    cmp byte [px_slon], 0           ; A SLIDESHOW: the slide is a still - the
    je .ns                          ; frame job holds the worker, and the
    jmp av_halt                     ; next slide's decode needs it
.ns:
    cmp byte [px_busy], 0
    jne .x
    cmp byte [px_hmode], 0
    jne .x
    cmp byte [px_anrun], 1          ; what is owed the glass - but never
    je .np                          ; while a frame is being made: its rect
    call av_paint                   ; is unioned before its rows are in, and
.np:                                ; painting it then spends it on the old
    cmp byte [px_anjob], 0          ; pixels (the wave-8 shots caught it)
    jne .run
    cmp byte [px_job], 0           ; (JOB_NONE)
    jne .x
    jmp av_start
.run:
    cmp byte [px_anrun], 3          ; shown, and its delay done?
    jne .x
    call OSAPI_GET_TICKS
    sub ax, [px_andue]
    js .x
    mov byte [px_ango], 1
.x:
    ret
.still:
    call av_halt                    ; (never freed under a running job:
    jmp av_free                     ; review-w8 A2)

; av_halt - the frame job, if one runs, stopped and its claims back
; (px_hstop's shape: the cancel, the answer, AV_WAKE). Preserves all
av_halt:
    cmp byte [px_anjob], 0
    je .x
    push ax
    mov byte [px_abort], 1
.w:
    cmp byte [px_job], 0            ; (JOB_NONE)
    je .d
    call OSAPI_TASK_YIELD
    jmp short .w
.d:
    pop ax
    jmp av_wake
.x:
    ret

; av_free - not an animation any more: its claim back, every byte cleared.
; Preserves all
av_free:
    push ax
    push dx
    mov dx, [px_anseg]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
.z:
    xor ax, ax
    mov [px_anseg], ax
    mov [px_anim], al
    mov [px_anon], al
    mov [px_anuser], al
    PSV SV_FLAGS
    pop dx
    pop ax
    ret

; av_start - the job, from [px_anpos] (or the file's start, for the global
; table): the animation's claim made the first time (1 KB: the global table
; and a small backup), the ring and the work claim (the LZW's tables, a row)
av_start:
    cmp word [px_anseg], 0
    jne .have
    mov ax, 1
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [px_anseg], dx
    mov word [px_ankb], 1
    push es
    mov es, dx
    xor di, di
    mov cx, AN_GTAB / 2
    xor ax, ax
    rep stosw
    pop es
.have:
    PSV SV_RINGCL
    jc .nomem
    mov ax, [px_cur + PXR_MW]       ; LZW_KB and a row, KB
    add ax, 1023
    mov al, ah
    xor ah, ah
    shr ax, 1
    shr ax, 1
    add ax, LZW_KB
    call OSAPI_MEM_CLAIM
    jc .nring
    mov [px_wbase], dx
    xor ax, ax                      ; THE STREAM: from the frame, or from the
    mov dx, ax                      ; start for the global table
    test byte [px_anflg], ANF_GREAD
    jnz .sb
    mov ax, [px_anpos]
    mov dx, [px_anpos + 2]
.sb:
    mov [px_sbase], ax
    mov [px_sbase + 2], dx
    PSV SV_PUMPINIT
    PSV SV_SPAWN
    jc .nwork
    xor ax, ax
    mov [px_abort], al
    mov [px_ango], al
    mov byte [px_anrun], 1
    mov byte [px_anjob], 1
    mov byte [px_job], JOB_ANIM     ; LAST: the worker starts on this byte
    ret
.nwork:
    mov dx, [px_wbase]
    call OSAPI_MEM_FREE
    mov word [px_wbase], 0
.nring:
    PSV SV_WFREE
.nomem:
    mov byte [px_anon], 0           ; not played: said, once
    mov si, av_s_mem
    call av_say
    PSV SV_FLAGS
    ret

; av_say - CS:SI said in a toast. Preserves all
av_say:
    push si
    push di
    mov di, px_cline
.c:
    mov al, [cs:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    mov si, px_cline
    PSV SV_TOAST
    pop di
    pop si
    ret

; AV_WAKE - the worker's wake (the ring already pumped, the lock held): a
; frame ready is painted and its delay begins; a job that has ended gives its
; claims back and is read - the pass over (another, or the last frame stays),
; the backup's room asked for, or stopped
av_wake:
    cmp byte [px_anjob], 0
    je .x
    cmp byte [px_job], 0           ; (JOB_NONE)
    jne .rdy
    PSV SV_WFREE                    ; THE JOB HAS ENDED
    mov dx, [px_wbase]
    or dx, dx
    jz .w0
    call OSAPI_MEM_FREE
.w0:
    xor ax, ax
    mov [px_wbase], ax
    mov [px_wseg], ax
    mov [px_anjob], al
    mov [px_anrun], al
    mov [px_ango], al
    mov al, [px_wres]
    or al, al
    jz .pass
    cmp al, PXD_ABORT
    je .x
    cmp al, PXD_MEM
    je .grow
.off:
    mov byte [px_anon], 0           ; anything else: it stops where it is
    jmp .fl
.pass:
    mov ax, [px_anloop]             ; THE PASS IS OVER: another, or the last
    or ax, ax                       ; frame stays
    jz .off
    cmp ax, 0xFFFF
    je .again
    dec word [px_anloop]
.again:
    call av_restart
    jmp .x
.grow:
    mov ax, [px_anneed]             ; THE BACKUP'S ROOM: a claim that big, the
    cmp ax, 0xFFFF                  ; global table copied over
    je .nm
    call OSAPI_MEM_CLAIM
    jc .nm
    push ds
    push es
    mov es, dx
    mov ax, [px_anseg]
    mov ds, ax
    xor si, si
    xor di, di
    mov cx, AN_GTAB / 2
    rep movsw
    pop es
    pop ds
    push dx
    mov dx, [px_anseg]
    call OSAPI_MEM_FREE
    pop dx
    mov [px_anseg], dx
    mov ax, [px_anneed]
    mov [px_ankb], ax
    jmp short .x
.nm:
    mov si, av_s_mem
    call av_say
    jmp short .off
.fl:
    PSV SV_FLAGS
.x:
    ret
.rdy:
    cmp byte [px_anrun], 2          ; A FRAME READY: on the glass, its delay
    jne .x                          ; from now
    call av_paint
    call OSAPI_GET_TICKS
    add ax, [px_andly]
    mov [px_andue], ax
    mov byte [px_anrun], 3
    ret

; av_restart - a pass from frame 0 again: the whole screen disposed to the
; background first. Preserves all
av_restart:
    push ax
    mov byte [px_andisp], 2
    xor ax, ax
    mov [px_andr], ax
    mov [px_andr + 2], ax
    mov ax, [px_cur + PXR_MW]
    dec ax
    mov [px_andr + 4], ax
    mov ax, [px_cur + PXR_MH]
    dec ax
    mov [px_andr + 6], ax
    mov ax, [px_an0]
    mov [px_anpos], ax
    mov ax, [px_an0 + 2]
    mov [px_anpos + 2], ax
    mov word [px_anfr], 0
    and byte [px_anflg], ~(ANF_DISPD | ANF_BKD | ANF_END)
    pop ax
    ret

; AV_TOGGLE - File > Stop / Play Animation, or A: a playing one stops (its
; job was stopped by the command's px_hstop), a stopped one goes on, and one
; whose passes have all played starts from frame 0
av_toggle:
    cmp byte [px_anim], 0
    je .x
    cmp byte [px_anuser], 0
    jne .go
    cmp byte [px_anon], 0
    jne .stop
    call av_restart                 ; over: from the start
    mov ax, [px_anl0]
    mov [px_anloop], ax
.go:
    mov byte [px_anuser], 0
    mov byte [px_anon], 1
    jmp short .fl
.stop:
    mov byte [px_anuser], 1
    mov byte [px_anon], 0
    call av_paint                   ; (what a stopped frame changed)
.fl:
    PSV SV_FLAGS
.x:
    ret

; av_paint - what changed on the glass ([px_anrect], master pixels) mapped
; through the canvas's view and rendered there - not while a card is up.
; Lock held
av_paint:
    cmp word [px_anrect], 0xFFFF
    je .x
    cmp byte [px_abon], 0
    jne .x
    cmp byte [px_helpon], 0
    jne .x
    cmp byte [px_infoon], 0
    jne .x
    cmp byte [px_pcon], 0
    jne .x
    PSV SV_LAYOUT
    jc .x
    mov bx, [px_win]
    call OSAPI_WM_CLIP_SET
    jc .x
    PSV SV_MQHIDE
    mov si, px_vcan + VW_HS         ; across: the first screen column of x1,
    mov cx, [px_vcan + VW_DW]       ; the last of x2
    mov ax, [px_anrect]
    call av_m2s
    add ax, [px_vcan + VW_IX]
    mov [cs:av_r], ax
    mov ax, [px_anrect + 4]
    inc ax
    call av_m2s
    add ax, [px_vcan + VW_IX]
    dec ax
    mov [cs:av_r + 4], ax
    mov si, px_vcan + VW_VS         ; down
    mov cx, [px_vcan + VW_DH]
    mov ax, [px_anrect + 2]
    call av_m2s
    add ax, [px_vcan + VW_IY]
    mov [cs:av_r + 2], ax
    mov ax, [px_anrect + 6]
    inc ax
    call av_m2s
    add ax, [px_vcan + VW_IY]
    dec ax
    mov [cs:av_r + 6], ax
    mov ax, [cs:av_r]               ; inside the canvas
    cmp ax, [px_cvx1]
    jge .a
    mov ax, [px_cvx1]
.a:
    mov cx, [cs:av_r + 4]
    cmp cx, [px_cvx2]
    jle .c
    mov cx, [px_cvx2]
.c:
    mov bx, [cs:av_r + 2]
    cmp bx, [px_midy1]
    jge .b
    mov bx, [px_midy1]
.b:
    mov dx, [cs:av_r + 6]
    cmp dx, [px_midy2]
    jle .d
    mov dx, [px_midy2]
.d:
    cmp ax, cx
    jg .ns
    cmp bx, dx
    jg .ns
    and ax, 0xFFF8                  ; (the picture's left is on the grid)
    PSV SV_RIMG
.ns:
    PSV SV_MQSHOW
    mov bx, [px_win]
    call OSAPI_WM_CLIP_CLEAR
    mov word [px_anrect], 0xFFFF
.x:
    ret

; av_m2s - AX = master pixels, DS:SI = a step (16.16, master pixels a screen
; pixel): AX = ceil((AX << 16) / step), the screen pixels before the first
; that shows master pixel AX - at most CX. Preserves all but AX
av_m2s:
    push bx
    push cx
    push dx
    push di
    push bp
    push si
    mov dx, ax                      ; DX:AX = AX << 16
    xor ax, ax
    mov bx, [si]                    ; CX:BX = the step
    mov cx, [si + 2]
    xor si, si                      ; DI:SI = the remainder
    xor di, di
    mov bp, 32
.l:
    shl ax, 1
    rcl dx, 1
    rcl si, 1
    rcl di, 1
    cmp di, cx
    jb .n
    ja .s
    cmp si, bx
    jb .n
.s:
    sub si, bx
    sbb di, cx
    or al, 1
.n:
    dec bp
    jnz .l
    or si, di                       ; (up)
    jz .c
    add ax, 1
    adc dx, 0
.c:
    pop si
    pop bp
    pop di
    or dx, dx                       ; at most CX
    pop dx
    pop cx
    jnz .cap
    cmp ax, cx
    jbe .o
.cap:
    mov ax, cx
.o:
    pop bx
    ret

av_s_mem:   db 'Animation: not enough memory', 0
pv_sv:      dw 0, 0                 ; the resident's service gate (pxsvc.inc)
pa_s_ns:    db 'NETSCAPE2.0'

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
pg_flim:    dw 0                    ; ...a string ending before which needs
                                    ; no row end and no clipping (pg_lrun)
pg_fi:      dw 0                    ; ...rows done, in the stream's order...
pg_fy:      dw 0                    ; ...and the row being filled
pg_y:       dw 0                    ; a screen row outside the image
pg_prog:    dw 0                    ; rows done, for the progress
pg_anim:    db 0                    ; the decode is an animation's frame
pg_blk:     db 0                    ; the sub-block's bytes left
pg_blkend:  db 0                    ; the terminator (or the end) is met
pa_fdisp:   db 0                    ; THE ANIMATION (106.25): a frame's GCE -
pa_fdly:    dw 0                    ; its disposal and delay
pa_subn:    db 0                    ; ...an extension's sub-block's number
pa_ns:      db 0                    ; ...it is NETSCAPE2.0
pa_tl:      dw 0
pa_loop:    dw 0                    ; ...its loop count, found
pa_hasloop: db 0
pa_isanim:  db 0                    ; frame 0's decode: a second image follows
pa_pos1:    dd 0                    ; ...where its blocks start
pa_an0:     dd 0                    ; ...and where frame 0's do
pa_cr:      dw 0, 0, 0, 0           ; a frame's rect on the screen
pa_t:       dw 0, 0, 0, 0           ; ...a rect being disposed or kept
pa_op:      db 0                    ; ...pa_rows' doing
pa_bk:      dw 0                    ; ...its place in the backup
pa_haslt:   db 0                    ; the frame has its own table
pa_whole:   db 0                    ; ...it was drawn whole
pa_np:      dw 0                    ; the palette's entries, for the map
pa_best:    dd 0
pa_bi:      db 0
pa_sum:     dd 0
pa_sqok:    db 0                    ; [pa_sq] made
av_r:       dw 0, 0, 0, 0           ; AV's: a rect on the glass
pa_map:     times 256 db 0          ; a frame's indices onto the palette
pa_mok:     db 0                    ; ...KEPT: made for this table [pa_mtab],
pa_mser:    db 0                    ; this picture and this palette size
pa_mnp:     dw 0
pa_mtab:    times 768 db 0
pa_sq:      times 256 dw 0          ; d^2
pa_ltab:    times 768 db 0          ; a frame's table, padded black
pg_lzw:     times LZW_BSSSZ db 0

%include "pxplan.inc"                ; the plans: shared source (106.20)
