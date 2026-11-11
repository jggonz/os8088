; =============================================================================
; Browser 1.7 - animated GIF (SPEC.md 71.29), part 1's. tools/pixelsim.py's
; gif_anim is the reference: a master of the logical screen, a byte an index,
; each frame drawn on it by the frame rules, and the box rows its dirty rect
; reaches made again in the copy in the store.
; =============================================================================

; bri_now - AX = the tick count. Every other register kept - ES above all:
; a kernel call does not keep it, and the records are reached through it
bri_now:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_GET_TICKS
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; bri_arp - AL = a record -> DI = it in bri_arec (ES = the part). Others kept
bri_arp:
    push ax
    push dx                         ; (mul writes it)
    xor ah, ah
    mov di, A_SIZE
    mul di
    add ax, bri_arec
    mov di, ax
    pop dx
    pop ax
    ret

; bri_skipdata - ES:SI = a frame's minimum-code byte, BP = the file's end: SI
; past its sub-blocks and their terminator. CF=1 the file ended first. AX
; clobbered
bri_skipdata:
    inc si
.b:
    cmp si, bp
    jae .end
    mov al, [es:si]
    inc si
    or al, al
    jz .ok
    xor ah, ah
    add si, ax
    jc .end
    jmp .b
.ok:
    clc
    ret
.end:
    stc
    ret

; bri_aext - ES:SI = where a run of blocks starts, BP = the file's end: the
; extensions walked to the next image descriptor (gif _gif_exts). A GCE of 4
; bytes or more sets [bri_xdisp], [bri_xdelay] and [bri_trans]; NETSCAPE2.0's
; second sub-block (3 bytes or more, starting 01) sets [bri_xloop]. CF=0 SI
; at the 2Ch; CF=1 anything else. AX, BX, CX, DX clobbered
bri_aext:
    push di
.blk:
    cmp si, bp
    jae .no
    mov al, [es:si]
    cmp al, 0x2C
    je .yes
    cmp al, 0x21
    jne .no
    inc si
    cmp si, bp
    jae .no
    mov al, [es:si]
    xor ah, ah
    mov di, ax                      ; DI = the label
    inc si
    xor bx, bx                      ; BL = the sub-block, BH = NETSCAPE2.0
.sub:
    cmp si, bp
    jae .no
    mov cl, [es:si]
    inc si
    xor ch, ch
    jcxz .blk
    mov dx, si
    add dx, cx
    jc .no
    cmp dx, bp
    ja .no
    cmp di, 0xF9
    jne .app
    cmp cl, 4
    jb .nx
    mov al, [es:si]
    mov ah, al
    shr al, 1
    shr al, 1
    and al, 7
    mov [bri_xdisp], al
    mov dx, [es:si + 1]
    mov [bri_xdelay], dx
    mov word [bri_trans], 0xFFFF
    test ah, 1
    jz .nx
    mov al, [es:si + 3]
    xor ah, ah
    mov [bri_trans], ax
    jmp .nx
.app:
    cmp di, 0xFF
    jne .nx
    or bl, bl
    jnz .k1
    cmp cl, 11
    jne .nx
    call bri_isns
    jne .nx
    mov bh, 1
    jmp .nx
.k1:
    cmp bl, 1
    jne .nx
    or bh, bh
    jz .nx
    cmp cl, 3
    jb .nx
    cmp byte [es:si], 1
    jne .nx
    mov dx, [es:si + 1]
    mov [bri_xloop], dx
.nx:
    add si, cx
    inc bl
    jmp .sub
.yes:
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; bri_isns - ZF=1 when ES:SI is NETSCAPE2.0. AL clobbered
bri_isns:
    push bx
    xor bx, bx
.c:
    mov al, [es:si + bx]
    cmp al, [cs:bri_s_ns + bx]
    jne .out
    inc bx
    cmp bx, 11
    jb .c
    cmp al, al
.out:
    pop bx
    ret
bri_s_ns: db 'NETSCAPE2.0'

; bri_xreset - a frame's GCE state as gif_anim starts it. All kept
bri_xreset:
    mov byte [bri_xdisp], 0
    mov word [bri_xdelay], 0
    mov word [bri_trans], 0xFFFF
    ret

; bri_isanim - IE_PREP, the file staged: a second image descriptor after the
; first image's sub-blocks, past extensions only, marks image [br_ik]
; animated in [bri_amask] (gif_animated). All kept
bri_isanim:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov es, dx
    mov bp, [bri_rx]
    mov si, [bri_data]
    dec si                          ; its minimum-code byte
    call bri_skipdata
    jc .out
    mov ax, [bri_trans]
    push ax
    call bri_aext
    pop ax
    mov [bri_trans], ax             ; (frame 0's key, for the plans)
    jc .out
    mov cl, [br_ik]
    call bri_abit                   ; BX = the byte, AL = the bit
    or [bri_amask + bx], al
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_abit - CL = an image -> BX = its byte in a 32-bit mask, AL = its bit.
; CL clobbered
bri_abit:
    mov bl, cl
    xor bh, bh
    shr bx, 1
    shr bx, 1
    shr bx, 1
    and cl, 7
    mov al, 1
    shl al, cl
    ret

; bri_adelay - AX = a GCE's delay in hundredths -> AX = ticks (gif_delay:
; under 2 reads as 10, rounded, at least 1). DX clobbered
bri_adelay:
    cmp ax, 2
    jae .d
    mov ax, 10
.d:
    push bx
    mov dx, 182
    mul dx
    add ax, 500
    adc dx, 0
    mov bx, 1000
    div bx
    pop bx
    or ax, ax
    jnz .ret
    inc ax
.ret:
    ret

; bri_aplan - IE_PREP: CF=0 when image [br_ik], found animated, is to play
; (SPEC.md 71.29, Set up as it arrives): a free record in [bri_acur] with its
; box and its master's paragraphs, the screen within the limits, [bri_ahas3]
; and [bri_apar] its block's, and [bri_apend] 1. CF=1 it stays still. AX,
; BX, CX, DX, SI, DI clobbered
bri_aplan:
    mov byte [bri_apend], 0
    mov cl, [br_ik]
    call bri_abit
    test [bri_amask + bx], al
    jz .no
    cmp word [bri_gw], BRI_ROWMAX
    ja .no
    mov ax, [bri_gw]
    mul word [bri_gh]
    or dx, dx
    jnz .no
    cmp ax, BR_IRMAX
    ja .no
    call bri_paras
    mov si, ax                      ; SI = the master's paragraphs
    push es
    mov es, [cs:PXP_SELF]           ; a free record
    xor al, al
.r:
    call bri_arp
    cmp byte [es:di + A_ON], 0
    je .free
    inc al
    cmp al, BRI_AMAX
    jb .r
    pop es
    jmp short .no
.free:
    mov [bri_acur], al
    mov ax, [bri_tw]
    mov [es:di + A_TW], ax
    mov ax, [bri_th]
    mov [es:di + A_TH], ax
    mov al, [bri_bord]
    mov [es:di + A_BORD], al
    mov [es:di + A_MST], si         ; (the master's paragraphs, for IE_ASET)
    pop es
    call bri_ascan                  ; [bri_ahas3]
    mov ax, [bri_rx]
    call bri_paras                  ; the file's
    add ax, si                      ; the master's
    cmp byte [bri_ahas3], 0
    je .ns
    add ax, si                      ; the saved rect's
.ns:
    add ax, 1024 / 16               ; the plans'
    mov [bri_apar], ax
    mov byte [bri_apend], 1
    clc
    ret
.no:
    stc
    ret

; bri_paras - AX bytes -> AX paragraphs, rounded up: a COUNT, never a
; segment (every segment here is OSAPI_SEG's, br_imgsub). CL clobbered
bri_paras:
    add ax, 15
    mov cl, 4
    shr ax, cl
    ret

; bri_ascan - [bri_ahas3] = 1 when any GCE in the staged file has disposal 3.
; All kept
bri_ascan:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    mov byte [bri_ahas3], 0
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov es, dx
    mov bp, [bri_rx]
    mov ax, [bri_trans]
    push ax
    call bri_hstart                 ; SI = the blocks before frame 0
.f:
    call bri_xreset
    call bri_aext
    jc .done
    cmp byte [bri_xdisp], 3
    jne .d
    mov byte [bri_ahas3], 1
    jmp .done
.d:
    call bri_skipimg
    jc .done
    jmp .f
.done:
    pop ax
    mov [bri_trans], ax
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_hstart - ES = a GIF: SI = past its header and global table. AX, CX
; clobbered
bri_hstart:
    mov si, 13
    mov al, [es:10]
    test al, 0x80
    jz .r
    call bri_palsz
    add si, ax
.r:
    ret

; bri_skipimg - ES:SI = an image descriptor, BP = the end: SI past its local
; table and its data. CF=1 the file ended first. AX, CX clobbered
bri_skipimg:
    mov ax, si
    add ax, 10
    jc .end
    cmp ax, bp
    ja .end
    mov al, [es:si + 9]
    add si, 10
    test al, 0x80
    jz .data
    call bri_palsz
    add si, ax
    jc .end
.data:
    jmp bri_skipdata
.end:
    stc
    ret

; IE_ASET (worker) - after IE_SHOW, the file still staged behind the copy:
; the plans, the master as gif_restart makes it, the record, and the block
; joins the copies (SPEC.md 71.29)
bri_aset:
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]
    mov [bri_stgs], dx              ; the file, as staged
    call bri_plans                  ; frame 0's palette, its key white
    jc .stale
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov bx, [br_iready]
    mov [es:di + A_BLK], bx
    mov ax, [bri_rx]
    mov [es:di + A_FLEN], ax
    add ax, 15
    mov cl, 4
    shr ax, cl
    add bx, ax                      ; the master after the file
    mov si, [es:di + A_MST]         ; (its paragraphs, from IE_PREP)
    mov [es:di + A_MST], bx
    add bx, si
    mov word [es:di + A_SAV], 0
    cmp byte [bri_ahas3], 0
    je .ns
    mov [es:di + A_SAV], bx
    add bx, si
.ns:
    mov [es:di + A_PLN], bx         ; the plans after them
    mov ax, [bri_gw]
    mov [es:di + A_GW], ax
    mov ax, [bri_gh]
    mov [es:di + A_GH], ax
    mov al, [bri_bgi]
    mov [es:di + A_B], al
    mov al, [br_ik]
    mov [es:di + A_K], al
    mov ax, [bri_pal]
    mov [es:di + A_P0PAL], ax
    mov ax, [bri_npal]
    mov [es:di + A_P0N], ax
    mov ax, [bri_data]
    mov [es:di + A_I0], ax
    mov ax, [bri_fl]
    mov [es:di + A_F0L], ax
    mov ax, [bri_ft]
    mov [es:di + A_F0T], ax
    mov ax, [bri_fw]
    mov [es:di + A_F0W], ax
    mov ax, [bri_fh]
    mov [es:di + A_F0H], ax
    mov al, [bri_ilace]
    mov [es:di + A_F0IL], al
    mov al, [bri_min]
    mov [es:di + A_F0MIN], al
    mov ax, [bri_trans]
    mov [es:di + A_T0], ax
    push es                         ; the plans into the block
    mov dx, [br_istseg]
    mov ax, [es:di + A_PLN]
    call far [br_im_r_imgsub]
    mov es, dx
    push ds
    mov ds, [cs:PXP_SELF]
    mov si, bri_ptab
    xor di, di
    mov cx, (256 * 3 + 256) / 2     ; bri_ptab and bri_t1, in a row
    cld
    rep movsw
    pop ds
    pop es
    mov al, [bri_acur]
    call bri_arp
    push es                         ; the file's global table, and the
    mov es, [bri_stgs]              ; header's loop and frame 0's GCE
    mov bp, [bri_rx]
    xor ax, ax
    xor cx, cx
    mov bl, [es:10]
    test bl, 0x80
    jz .ng
    mov al, bl
    call bri_palsz
    mov ax, 13
.ng:
    mov dx, cx
    pop es
    mov [es:di + A_GPAL], ax
    mov [es:di + A_GN], dx
    push es
    mov es, [bri_stgs]
    call bri_xreset
    mov word [bri_xloop], 0FFFFh
    call bri_hstart
    call bri_aext                   ; to frame 0's descriptor
    mov bl, 1                       ; frame 0 on the global table?
    test byte [es:si + 9], 0x80
    jz .g0
    xor bl, bl
.g0:
    mov si, [bri_data]
    dec si
    push bx
    call bri_skipdata
    mov bx, si
    jnc .p1
    mov bx, 0FFFFh
.p1:
    pop ax
    pop es
    push ax
    mov [es:di + A_P1], bx
    mov [es:di + A_POS], bx
    pop ax                          ; (A_G0, kept across the walk)
    mov [es:di + A_G0], al
    mov ax, [bri_xloop]
    mov [es:di + A_LOOP], ax
    mov [es:di + A_LEFT], ax
    mov al, [bri_xdisp]
    mov [es:di + A_DISP0], al
    mov ax, [bri_xdelay]
    call bri_adelay
    mov [es:di + A_D0], ax
    mov ax, [es:di + A_T0]          ; frame 0's own key, which the walk
    mov [bri_trans], ax             ; above may have read again
    mov ax, [bri_apar]
    add [br_iready], ax             ; the block joins the copies: the LZW
    call bri_aload                  ; table is past it. The segments, plans
    call bri_arestart               ; the master: B, then frame 0
    jc .stale
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    call bri_now
    add ax, [es:di + A_D0]
    mov [es:di + A_DUE], ax
    mov byte [es:di + A_ON], 1
    inc byte [bri_anum]
    mov byte [bri_apend], 0
    mov al, IE_NEXT
    jmp bri_go
.stale:
    ret

; bri_aload - record [bri_acur]: its segments, its screen, B, frame 0's
; palette, its plans into bri_ptab and bri_t1, and its image's copy and
; box for bri_convrow. All kept
bri_aload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov si, di                      ; ES:SI = the record
    mov dx, [br_istseg]
    mov ax, [es:si + A_BLK]
    call far [br_im_r_imgsub]
    mov [bri_stgs], dx
    mov ax, [es:si + A_FLEN]
    mov [bri_rx], ax
    mov dx, [br_istseg]
    mov ax, [es:si + A_MST]
    call far [br_im_r_imgsub]
    mov [bri_amseg], dx
    mov dx, [br_istseg]
    mov ax, [es:si + A_SAV]
    call far [br_im_r_imgsub]
    mov [bri_asseg], dx
    mov dx, [br_istseg]
    mov ax, [br_iready]
    call far [br_im_r_imgsub]       ; the LZW table past the copies
    mov [bri_lzws], dx
    mov ax, [es:si + A_GW]
    mov [bri_gw], ax
    mov ax, [es:si + A_GH]
    mov [bri_gh], ax
    mov al, [es:si + A_B]
    mov [bri_ab], al
    mov ax, [es:si + A_P0PAL]
    mov [bri_apal], ax
    mov ax, [es:si + A_P0N]
    mov [bri_apn], ax
    mov ax, [es:si + A_T0]
    mov [bri_at0], ax
    mov ax, [es:si + A_TW]
    mov [bri_tw], ax
    mov ax, [es:si + A_TH]
    mov [bri_th], ax
    mov al, [es:si + A_BORD]
    mov [bri_bord], al
    mov bl, [es:si + A_K]
    xor bh, bh
    call bri_ent
    mov dx, [br_istseg]
    mov ax, [bx + IT_PARA]
    call far [br_im_r_imgsub]
    mov [bri_news], dx              ; the copy, in the store
    mov al, [bx + IT_COLS]
    xor ah, ah
    mov [bri_bc], ax
    mov al, [bx + IT_ROWS]
    mov [bri_br], al
    mov cx, [bri_bc]
    mov byte [bri_mono], 1
    cmp byte [br_bpp], 1
    je .m
    mov byte [bri_mono], 0
    shl cx, 1
    shl cx, 1
.m:
    mov [bri_stride], cx
    mov dx, [br_istseg]             ; the plans, out of the block
    mov ax, [es:si + A_PLN]
    call far [br_im_r_imgsub]
    push ds
    mov ds, dx
    xor si, si
    mov di, bri_ptab
    mov cx, (256 * 3 + 256) / 2
    cld
    rep movsw
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_arestart - a pass begins (gif_restart): the master to B, frame 0 drawn
; by the frame rules, the whole screen dirty, the walk after frame 0. The
; record loaded. CF=1 stopped
bri_arestart:
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    xor ax, ax
    mov [bri_ax1], ax
    mov [bri_ay1], ax
    mov [bri_dx1], ax
    mov [bri_dy1], ax
    mov ax, [bri_gw]
    dec ax
    mov [bri_ax2], ax
    mov [bri_dx2], ax
    mov ax, [bri_gh]
    dec ax
    mov [bri_ay2], ax
    mov [bri_dy2], ax
    xor al, al
    call bri_arect                  ; all of it B
    mov ax, [es:di + A_F0L]
    mov [bri_fl], ax
    mov ax, [es:di + A_F0T]
    mov [bri_ft], ax
    mov ax, [es:di + A_F0W]
    mov [bri_fw], ax
    mov ax, [es:di + A_F0H]
    mov [bri_fh], ax
    mov al, [es:di + A_F0IL]
    mov [bri_ilace], al
    mov al, [es:di + A_F0MIN]
    mov [bri_min], al
    mov ax, [es:di + A_T0]
    mov [bri_trans], ax
    mov ax, [es:di + A_I0]
    mov [bri_fpos], ax
    mov byte [bri_aident], 1        ; frame 0's indices are the master's
    call bri_adraw
    jc .out
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov ax, [es:di + A_P1]
    mov [es:di + A_POS], ax
    mov al, [es:di + A_DISP0]       ; frame 0's disposal 3 restores B
    cmp al, 3                       ; (gif_anim's prev_save None)
    jne .d0
    mov al, 2
.d0:
    mov [es:di + A_PDISP], al
    mov ax, [bri_fl]
    mov bx, [bri_ft]
    mov cx, [bri_fw]
    mov dx, [bri_fh]
    call bri_aclip
    mov [es:di + A_PX1], ax
    mov [es:di + A_PY1], bx
    mov [es:di + A_PX2], cx
    mov [es:di + A_PY2], dx
    mov ax, [es:di + A_LOOP]
    mov [es:di + A_LEFT], ax
    clc
.out:
    ret

; bri_aclip - a frame's rect AX left, BX top, CX w, DX h -> AX..DX = x1, y1,
; x2, y2 clipped to the screen, inclusive; AX = FFFFh when none (_gif_rect)
bri_aclip:
    or cx, cx
    jz .none
    or dx, dx
    jz .none
    cmp ax, [bri_gw]
    jae .none
    cmp bx, [bri_gh]
    jae .none
    add cx, ax
    jc .cw
    cmp cx, [bri_gw]
    jbe .x
.cw:
    mov cx, [bri_gw]
.x:
    dec cx
    add dx, bx
    jc .ch
    cmp dx, [bri_gh]
    jbe .y
.ch:
    mov dx, [bri_gh]
.y:
    dec dx
    ret
.none:
    mov ax, 0xFFFF
    ret

; bri_arect - AL = 0 the rect [bri_ax1..bri_ay2] of the master to B, 1 the
; master's into the saved rect, 2 the saved rect's back into the master
; (the same places in both). All kept
bri_arect:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    mov dl, al
    mov bx, [bri_ay1]
.row:
    cmp bx, [bri_ay2]
    ja .out
    push dx
    mov ax, bx
    mul word [bri_gw]
    pop dx
    add ax, [bri_ax1]
    mov di, ax
    mov si, ax
    mov cx, [bri_ax2]
    sub cx, [bri_ax1]
    inc cx
    cld
    or dl, dl
    jnz .cp
    mov es, [bri_amseg]
    mov al, [bri_ab]
    rep stosb
    jmp .nr
.cp:
    push ds
    mov ax, [bri_amseg]
    mov es, [bri_asseg]
    cmp dl, 1
    je .sv
    mov es, ax                      ; restore: the saved rect to the master
    mov ax, [bri_asseg]
.sv:
    mov ds, ax
    rep movsb
    pop ds
.nr:
    inc bx
    jmp .row
.out:
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_adraw - the frame in [bri_fl..bri_fh] from [bri_fpos] onto the master:
; each whole row at its final y, clipped, [bri_trans] left as it was, the
; rest mapped (or itself, [bri_aident]). CF=1 stopped; else [bri_ret] =
; BRI_DONE when every row was made
bri_adraw:
    xor ax, ax
    mov [bri_blk], al
    mov [bri_bend], al
    mov [bri_fcol], ax
    mov [bri_frow], ax
    mov [bri_fy], ax
    mov [bri_pass], al
    mov word [bri_ret], 0
    cmp byte [bri_aident], 0
    jne .go
    push es                         ; every index not yet looked up
    mov es, [cs:PXP_SELF]
    mov di, bri_amp
    mov cx, 256
    mov ax, 0FFFFh
    cld
    rep stosw
    pop es
.go:
    mov byte [bri_amode], 1
    mov al, [bri_min]
    mov es, [bri_lzws]
    call lzw_decode
    push ds
    pop es
    mov byte [bri_amode], 0
    jnc .part                       ; the codes ended first
    cmp ax, BRI_STALE
    je .stale
    mov [bri_ret], ax
    clc
    ret
.part:
    mov word [bri_ret], 0
    clc
    ret
.stale:
    stc
    ret

; bri_amrow - bri_rowdone in the master's mode: frame row [bri_fy], whole in
; bri_rowbuf, onto the master at its final y. AX, BX, CX, SI, DI, ES
; clobbered
bri_amrow:
    mov ax, [bri_ft]
    add ax, [bri_fy]
    jc .out
    cmp ax, [bri_gh]
    jae .out
    mul word [bri_gw]
    add ax, [bri_fl]
    mov di, ax
    mov cx, [bri_gw]
    sub cx, [bri_fl]
    jbe .out
    cmp cx, [bri_fw]
    jbe .c
    mov cx, [bri_fw]
.c:
    mov es, [bri_amseg]
    xor si, si
.px:
    mov bl, [cs:bri_rowbuf + si]
    xor bh, bh
    cmp bx, [bri_trans]
    je .skip
    mov al, bl
    cmp byte [bri_aident], 0
    jne .put
    call bri_amap
.put:
    mov [es:di], al
.skip:
    inc si
    inc di
    loop .px
.out:
    ret

; bri_amap - BX = a frame's index -> AL = frame 0's nearest entry, looked up
; once a frame (bri_amp). All but AX kept
bri_amap:
    push bx
    push es
    shl bx, 1
    mov ax, [cs:bri_amp + bx]
    cmp ax, 0FFFFh
    jne .have
    shr bx, 1
    call bri_anear
    xor ah, ah
    shl bx, 1
    mov es, [cs:PXP_SELF]
    mov [es:bri_amp + bx], ax
.have:
    pop es
    pop bx
    ret

; bri_anear - BX = an index of the frame's table -> AL = the entry of frame
; 0's palette nearest it, 3 dr² + 6 dg² + db², the first of equals
; (_gif_nearest); past the table the colour is black. Frame 0's palette is
; decode's: its key is the page's white, and a key past the table is one
; more entry (pixelsim's p.npal). All but AX kept
bri_anear:
    push bx
    push cx
    push dx
    push si
    push es
    mov es, [bri_stgs]
    xor ax, ax
    mov [bri_cr], al
    mov [bri_cg], ax                ; (and bri_cb)
    cmp bx, [bri_atabn]
    jae .blk
    mov si, bx
    shl si, 1
    add si, bx
    add si, [bri_atab]
    mov al, [es:si]
    mov [bri_cr], al
    mov al, [es:si + 1]
    mov [bri_cg], al
    mov al, [es:si + 2]
    mov [bri_cb], al
.blk:
    mov word [bri_best], 0FFFFh
    mov word [bri_best + 2], 0FFFFh
    mov byte [bri_bi], 0
    mov si, [bri_apal]
    xor bx, bx                      ; BX = the entry
.e:
    mov ax, [bri_apn]
    cmp ax, [bri_at0]               ; (FFFFh: no key, never past)
    ja .n
    cmp word [bri_at0], 0FFFFh
    je .n
    mov ax, [bri_at0]
    inc ax                          ; a key past the table counts
.n:
    cmp bx, ax
    jae .done
    push bx
    call .rgb                       ; DL, DH, CL = the entry's colour
    mov al, [bri_cr]
    mov ah, dl
    push dx
    call .sq                        ; AX = dr²
    pop dx
    push cx
    mov bx, ax
    xor cx, cx
    add bx, ax
    adc cx, 0
    add bx, ax
    adc cx, 0                       ; CX:BX = 3 dr²
    mov al, [bri_cg]
    mov ah, dh
    call .sq
    mov dx, 6
.g:
    add bx, ax
    adc cx, 0
    dec dx
    jnz .g                          ; + 6 dg²
    pop dx                          ; (DL = the entry's blue)
    mov al, [bri_cb]
    mov ah, dl
    call .sq
    add bx, ax
    adc cx, 0                       ; + db²
    cmp cx, [bri_best + 2]
    jb .better
    ja .worse
    cmp bx, [bri_best]
    jae .worse
.better:
    mov [bri_best], bx
    mov [bri_best + 2], cx
    pop bx
    mov [bri_bi], bl
    jmp .ne
.worse:
    pop bx
.ne:
    add si, 3
    inc bx
    jmp .e
.done:
    mov al, [bri_bi]
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret
; BX = an entry of frame 0's palette, ES:SI its bytes -> DL, DH, CL = its
; red, green, blue: white for the key, black past the table. Others kept
.rgb:
    cmp bx, [bri_at0]
    jne .nk
    mov dx, 0FFFFh
    mov cl, 0FFh
    ret
.nk:
    cmp bx, [bri_apn]
    jb .tab
    xor dx, dx
    xor cl, cl
    ret
.tab:
    mov dl, [es:si]
    mov dh, [es:si + 1]
    mov cl, [es:si + 2]
    ret
; AL, AH two channels -> AX = their difference squared
.sq:
    cmp al, ah
    jae .s
    xchg al, ah
.s:
    sub al, ah
    mul al
    ret

; bri_aconv - the box rows the dirty rect [bri_dx1..bri_dy2] reaches made
; again from the master: each one's logical row (ty H / th) into bri_rowbuf,
; then bri_trowfill and bri_convrow across the whole screen. CF=1 a stop
; came first (bri_gchk, a row at a time, so br_imghalt waits for one row and
; not the box; a refused close may keep a copy part made). Others kept
bri_aconv:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [bri_dx1], 0FFFFh
    je .out
    xor ax, ax
    mov [bri_fl], ax                ; the column map: the whole screen
    mov ax, [bri_gw]
    mov [bri_fw], ax
    call bri_colmap
    mov byte [bri_allbg], 0
    mov word [bri_lsy], 0FFFFh
    mov ax, [bri_dy2]
    inc ax
    call bri_tyof
    mov [bri_ty1], ax
    mov ax, [bri_dy1]
    call bri_tyof
    mov bx, ax                      ; BX = the box row
.r:
    call bri_gchk
    jc .out                         ; stopped: CF=1
    cmp bx, [bri_ty1]
    jae .out                        ; (CF=0, as at the je above)
    mov ax, bx
    mul word [bri_gh]
    div word [bri_th]               ; AX = its logical row
    cmp ax, [bri_lsy]
    je .conv
    mov [bri_lsy], ax
    mul word [bri_gw]
    mov si, ax
    push ds
    mov es, [cs:PXP_SELF]
    mov di, bri_rowbuf
    mov cx, [bri_gw]
    mov ds, [bri_amseg]
    cld
    rep movsb
    pop ds
    call bri_trowfill
.conv:
    call bri_convrow
    inc bx
    jmp .r
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; bri_aunion - the dirty rect := itself and AX..DX (x1, y1, x2, y2; AX = FFFFh
; none). All kept
bri_aunion:
    cmp ax, 0FFFFh
    je .ret
    cmp word [bri_dx1], 0FFFFh
    jne .u
    mov [bri_dx1], ax
    mov [bri_dy1], bx
    mov [bri_dx2], cx
    mov [bri_dy2], dx
.ret:
    ret
.u:
    cmp ax, [bri_dx1]
    jae .a
    mov [bri_dx1], ax
.a:
    cmp bx, [bri_dy1]
    jae .b
    mov [bri_dy1], bx
.b:
    cmp cx, [bri_dx2]
    jbe .c
    mov [bri_dx2], cx
.c:
    cmp dx, [bri_dy2]
    jbe .d
    mov [bri_dy2], dx
.d:
    ret

; IE_ANIM (worker) - the first animation whose frame is due, and that is not
; paused, makes it (bri_aframe), counting round from the one after the last
; served, so each due one has its turn on a machine slower than its delay;
; a paused one plays again once a paint has drawn it ([br_iseen]). None left
; playing or paused hands over to IE_AEND
bri_astep:
    call bri_gchk
    jc .ret
    mov es, [cs:PXP_SELF]
    call bri_now
    mov dx, ax                      ; DX = now
    xor si, si                      ; SI = those playing or paused
    mov al, [bri_acur]
    mov ah, BRI_AMAX                ; AH = the records left to look at
.r:
    inc al
    cmp al, BRI_AMAX
    jb .in
    xor al, al
.in:
    call bri_arp
    test byte [es:di + A_ON], 1     ; 1 plays, 3 paused (0 free, 2 stopped)
    jz .n
    inc si
    cmp byte [es:di + A_ON], 3
    jne .play
    mov cl, [es:di + A_K]
    push ax
    call bri_abit
    test [br_iseen + bx], al
    pop ax
    jz .n
    mov byte [es:di + A_ON], 1      ; drawn again: it plays from now
    mov [es:di + A_DUE], dx
.play:
    cmp byte [es:di + A_ON], 1
    jne .n
    mov cx, dx
    sub cx, [es:di + A_DUE]
    js .n                           ; not yet
    mov [bri_acur], al
    mov cl, [es:di + A_K]
    mov [br_ik], cl
    call bri_aload
    call bri_aframe
    jc .ret                         ; stopped, or nothing to show
    mov al, IE_AFRAME
    call bri_go
    jc .ret
    jmp bri_kick
.n:
    dec ah
    jnz .r
    or si, si
    jnz .ret
    mov al, IE_AEND                 ; every one still: the UI lets them go
    call bri_go
    jc .ret
    jmp bri_kick
.ret:
    ret

; bri_aframe - record [bri_acur], loaded: its next frame onto the master and
; into the copy (SPEC.md 71.29, A frame). CF=1 nothing to draw: the
; animation stopped, or the engine did
bri_aframe:
    mov word [bri_dx1], 0FFFFh
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov si, [es:di + A_POS]
    cmp si, 0FFFFh
    jne .walk
    jmp .pass
.walk:
    push es
    mov es, [bri_stgs]
    mov bp, [bri_rx]
.again:
    call bri_xreset
    call bri_aext
    jc .passp
    mov ax, si                      ; the descriptor
    add ax, 10
    jc .passp
    cmp ax, bp
    ja .passp
    mov ax, [es:si + 1]
    mov [bri_fl], ax
    mov ax, [es:si + 3]
    mov [bri_ft], ax
    mov ax, [es:si + 5]
    mov [bri_fw], ax
    mov ax, [es:si + 7]
    mov [bri_fh], ax
    mov dl, [es:si + 9]
    add si, 10
    mov al, dl
    and al, 0x40
    mov [bri_ilace], al
    test dl, 0x80
    jz .notab
    mov al, dl
    call bri_palsz                  ; CX entries, AX bytes
    mov [bri_atab], si
    mov [bri_atabn], cx
    mov byte [bri_aident], 0
    add si, ax
    jc .passp
    cmp si, bp
    ja .passp
    jmp .tab
.notab:
    pop es                          ; the record's view of the tables
    push es
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov ax, [es:di + A_GPAL]
    mov cx, [es:di + A_GN]
    mov bl, [es:di + A_G0]
    pop es
    push es
    mov es, [bri_stgs]
    mov [bri_atab], ax
    mov [bri_atabn], cx
    mov [bri_aident], bl
    or ax, ax
    jnz .tab
    cmp word [bri_fw], 0            ; no table at all: the pass ends - but
    je .tab                         ; a frame of no size is skipped first
    cmp word [bri_fh], 0
    jne .passp
.tab:
    cmp word [bri_fw], 0
    je .skip
    cmp word [bri_fh], 0
    jne .sized
.skip:
    call bri_skipdata               ; skipped: its data, and on
    jc .passp
    jmp .again
.sized:
    cmp word [bri_fw], BRI_ROWMAX
    ja .passp
    cmp si, bp
    jae .passp
    mov al, [es:si]
    cmp al, 2
    jb .passp
    cmp al, 8
    ja .passp
    mov [bri_min], al
    lea ax, [si + 1]
    mov [bri_fpos], ax
    call bri_skipdata
    mov [bri_anew], si
    jnc .data
    mov word [bri_anew], 0FFFFh
.data:
    pop es
    mov al, [bri_acur]
    call bri_arp                    ; ES:DI = the record again
    cmp byte [es:di + A_PDISP], 2   ; the last frame's disposal
    jb .nodisp
    cmp byte [es:di + A_PDISP], 3
    ja .nodisp
    cmp word [es:di + A_PX1], 0FFFFh
    je .nodisp
    mov ax, [es:di + A_PX1]
    mov [bri_ax1], ax
    mov ax, [es:di + A_PY1]
    mov [bri_ay1], ax
    mov ax, [es:di + A_PX2]
    mov [bri_ax2], ax
    mov ax, [es:di + A_PY2]
    mov [bri_ay2], ax
    xor al, al                      ; 2: B
    cmp byte [es:di + A_PDISP], 3
    jne .dfill
    cmp word [es:di + A_SAV], 0
    je .dfill
    mov al, 2                       ; 3: what was there
.dfill:
    call bri_arect
    mov ax, [bri_ax1]
    mov bx, [bri_ay1]
    mov cx, [bri_ax2]
    mov dx, [bri_ay2]
    call bri_aunion
.nodisp:
    mov ax, [bri_fl]                ; this frame's rect
    mov bx, [bri_ft]
    mov cx, [bri_fw]
    mov dx, [bri_fh]
    call bri_aclip
    mov [bri_ax1], ax
    mov [bri_ay1], bx
    mov [bri_ax2], cx
    mov [bri_ay2], dx
    cmp ax, 0FFFFh
    je .nosave
    cmp byte [bri_xdisp], 3
    jne .nosave
    cmp word [es:di + A_SAV], 0
    je .nosave
    mov al, 1                       ; its own disposal 3: keep what it covers
    call bri_arect
.nosave:
    call bri_adraw
    jc .stopped
    mov ax, [bri_ax1]
    mov bx, [bri_ay1]
    mov cx, [bri_ax2]
    mov dx, [bri_ay2]
    call bri_aunion
    call bri_aconv
    jc .stopped
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov ax, [bri_ax1]
    mov [es:di + A_PX1], ax
    mov ax, [bri_ay1]
    mov [es:di + A_PY1], ax
    mov ax, [bri_ax2]
    mov [es:di + A_PX2], ax
    mov ax, [bri_ay2]
    mov [es:di + A_PY2], ax
    mov al, [bri_xdisp]
    mov [es:di + A_PDISP], al
    mov ax, [bri_anew]
    cmp word [bri_ret], BRI_DONE
    je .whole
    mov ax, 0FFFFh                  ; part-drawn: it ends the pass
.whole:
    mov [es:di + A_POS], ax
    mov ax, [bri_xdelay]
    call bri_adelay
    jmp .due
.passp:
    pop es
.pass:                              ; the pass is over: once, n more, or ever
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    mov ax, [es:di + A_LOOP]
    cmp ax, 0FFFFh
    je .stop
    or ax, ax
    jz .again0
    cmp word [es:di + A_LEFT], 0
    je .stop
    dec word [es:di + A_LEFT]
.again0:
    push word [es:di + A_LEFT]
    call bri_arestart
    pop ax
    jc .stopped
    mov [es:di + A_LEFT], ax        ; (bri_arestart refills it: kept)
    call bri_aconv
    jc .stopped
    mov ax, [es:di + A_D0]
.due:
    mov dx, ax
    call bri_now
    add ax, dx
    mov [es:di + A_DUE], ax
    clc
    ret
.stop:
    mov byte [es:di + A_ON], 2      ; on its last frame
.stopped:
    stc
    ret

; IE_AFRAME (UI) - the frame drawn: the image alone, as IE_SHOW draws it.
; Nothing drawn - scrolled off, covered, minimised - pauses it until a paint
; does draw it (SPEC.md 71.29, Pausing)
bri_uaframe:
    mov cl, [br_ik]
    call bri_abit
    not al
    and [br_iseen + bx], al
    call OSAPI_GFX_LOCK
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    mov al, [br_ik]
    inc al
    mov [br_ionly], al
    call far [br_im_r_paint_band]
    mov byte [br_ionly], 0
.unlock:
    call OSAPI_GFX_UNLOCK
    inc word [br_iframes]
    mov cl, [br_ik]
    call bri_abit
    test [br_iseen + bx], al
    jnz .drawn
    mov es, [cs:PXP_SELF]
    mov al, [bri_acur]
    call bri_arp
    cmp byte [es:di + A_ON], 1
    jne .drawn
    mov byte [es:di + A_ON], 3      ; paused
.drawn:
    mov byte [br_ie], IE_ANIM
    ret

; IE_AGO (UI) - every image is in: the LZW table past the copies for the
; frames, then IE_ANIM; refused, the last animation is let go and its block
; given back, and it is asked again (bri_ustore's order); none left, the
; pictures stay still
bri_uago:
    mov ax, [br_iready]
    add ax, BRI_LZWPAR
    jc .drop
    call bri_grow
    jnc .go
.drop:
    call bri_adrop                  ; CF=1 none left
    jc bri_uaend
    mov word [bri_rx], 0            ; no stage: compact must not move the
    mov ax, [br_ibase]              ; last file staged
    call bri_compact                ; its block given back
    jmp bri_uago
.go:
    cmp byte [bri_anum], 0          ; the last one was let go to fit it
    je bri_uaend
    mov byte [br_ie], IE_ANIM
    ret

; IE_AEND (UI), and IE_AGO refused - every animation let go: each stays on
; the frame it shows, its block given back with the LZW table, and part 1
; dropped (IE_END)
bri_uaend:
    call bri_adrop
    jnc bri_uaend
    mov word [bri_rx], 0            ; no stage (bri_aload left a block's)
    mov ax, [br_ibase]
    call bri_compact
    call bri_ushrink
    mov byte [bri_anum], 0
    mov byte [br_ie], IE_END
    ret
