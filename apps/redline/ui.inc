; REDLINE dashboard. All pixels are native OS primitives, all timings remain
; in benchlib. Geometry and button hit boxes share the live content origin.

ru_indices:
    mov word [ru_sum], 0
    mov word [ru_sum+2], 0
    mov word [ru_sum+4], 0
    mov byte [ru_valid], 1
    xor di, di
.row:
    push di
    mov ax, [rl_results+di]
    or ax, [rl_results+di+2]
    jz .zero
    call rl_reference
    jc .zeroval
    mov cx, 1000
    call bl_mul48
    mov bx, [rl_results+di]
    mov cx, [rl_results+di+2]
    call rl_div48by32
    jmp .store
.zero:
    cmp di, 24
    jae .zeroval
    mov byte [ru_valid], 0
.zeroval:
    xor ax, ax
    xor dx, dx
.store:
    pop di
    mov [ru_scores+di], ax
    mov [ru_scores+di+2], dx
    cmp di, 24
    jae .next
    add [ru_sum], ax
    adc [ru_sum+2], dx
    adc word [ru_sum+4], 0
.next:
    add di, 4
    cmp di, RL_ROWS*4
    jb .row
    mov ax, [ru_sum]
    mov [bl_m], ax
    mov ax, [ru_sum+2]
    mov [bl_m+2], ax
    mov ax, [ru_sum+4]
    mov [bl_m+4], ax
    mov cx, 6
    call bl_div48
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    mov [ru_overall], ax
    mov [ru_overall+2], dx
    ; Each bucket - CPU, RAM, graphics - carries its own mean index.
    xor bx, bx
.group:
    mov cl, [ru_gcount+bx]
    xor ch, ch
    push bx
    mov bl, [ru_gfirst+bx]
    call ru_gmean
    pop bx
    mov di, bx
    shl di, 1
    shl di, 1
    mov [ru_gscores+di], ax
    mov [ru_gscores+di+2], dx
    inc bx
    cmp bx, 3
    jb .group
    call ru_scale_compute
    ret

; BL = first workload, CX = count -> DX:AX = their mean index. A bucket with
; any unresolved or unreferenced row has no mean (0, shown as n/a) rather
; than a mean of the rows that happened to resolve.
ru_gmean:
    push bx
    push cx
    push si
    xor bh, bh
    mov si, bx
    shl si, 1
    shl si, 1
    mov word [bl_m], 0
    mov word [bl_m+2], 0
    mov word [bl_m+4], 0
    push cx
.add:
    mov ax, [ru_scores+si]
    mov dx, [ru_scores+si+2]
    mov bx, ax
    or bx, dx
    jz .none
    add [bl_m], ax
    adc [bl_m+2], dx
    adc word [bl_m+4], 0
    add si, 4
    loop .add
    pop cx
    call bl_div48
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    jmp .out
.none:
    pop cx
    xor ax, ax
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    ret

; One common scale: round the highest score up to whole x, then add 5x.
ru_scale_compute:
    mov word [ru_scale], 0
    mov word [ru_scale+2], 0
    xor di, di
.row:
    mov dx, [ru_scores+di+2]
    mov ax, [ru_scores+di]
    cmp dx, [ru_scale+2]
    jb .next
    ja .store
    cmp ax, [ru_scale]
    jbe .next
.store:
    mov [ru_scale], ax
    mov [ru_scale+2], dx
.next:
    add di, 4
    cmp di, RL_ROWS*4
    jb .row
    mov ax, [ru_scale]
    mov dx, [ru_scale+2]
    mov cx, 1000
    call bl_div32                 ; BX = fractional remainder
    or bx, bx
    jz .margin
    add ax, 1
    adc dx, 0
.margin:
    add ax, 5
    adc dx, 0
    call bl_mul48
    cmp word [bl_m+4], 0
    jne .saturate
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    mov [ru_scale], ax
    mov [ru_scale+2], dx
    ret
.saturate:
    mov word [ru_scale], 0xFFFF
    mov word [ru_scale+2], 0xFFFF
    ret

ru_clock:
    mov word [ru_clocklabel], ru_mhzest
    mov word [ru_clockvalue], 0
    mov word [ru_clockvalue+2], 0
    mov ax, [rl_nominal]
    or ax, ax
    jz .tsc
    mov cx, 100
    mul cx
    mov word [ru_clocklabel], ru_mhznom
    jmp .store
.tsc:
    mov ax, [rl_tscmhz]
    mov dx, [rl_tscmhz+2]
    mov cx, ax
    or cx, dx
    jz .book
    mov word [ru_clocklabel], ru_mhztsc
    jmp .store
.book:
    cmp byte [rl_ran], 0
    je .out
    xor bx, bx
    cmp byte [rl_early], 1
    jbe .estimate
    mov bx, 2
    cmp word [rl_cpuname], rl_cpu286
    je .estimate
    mov bx, 4
    cmp word [rl_cpuname], rl_cpu386
    jne .out
.estimate:
    mov ax, [rl_mulbook+bx]
    mov di, 8
    call rl_estimate
.store:
    mov [ru_clockvalue], ax
    mov [ru_clockvalue+2], dx
.out:
    ret

ru_geometry:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, [rl_win]
    call OSAPI_WM_GEOM
    mov [ru_w], cx
    mov [ru_h], dx
    call OSAPI_WM_CONTENT
    mov [ru_x], ax
    mov [ru_y], dx
    mov ax, [ru_w]
    mov cx, 5
    mul cx
    mov cx, 8
    div cx
    mov [ru_split], ax
    mov ax, [ru_h]
    sub ax, 24
    mov [ru_foot], ax
    mov byte [ru_small], 0
    mov word [ru_lower], 136
    mov word [ru_offset], 20
    cmp word [ru_h], 280
    jae .normal
    mov byte [ru_small], 1
    mov word [ru_lower], 76
    mov word [ru_offset], 14
.normal:
    cmp byte [ru_view], 2
    jne .view
    mov word [ru_lower], 24
.view:
    mov ax, [ru_foot]
    sub ax, [ru_lower]
    sub ax, [ru_offset]
    sub ax, 4
    xor dx, dx
    mov cx, RU_LINES
    div cx
    cmp ax, 9                    ; 8px text/bars plus one clear scanline
    jae .spacing
    mov ax, 9
.spacing:
    cmp ax, 18
    jbe .pitch
    mov ax, 18
.pitch:
    mov [ru_pitch], ax
    mov ax, [ru_foot]
    sub ax, [ru_lower]
    sub ax, [ru_offset]
    sub ax, 4
    xor dx, dx
    div word [ru_pitch]
    cmp ax, RU_LINES
    jbe .rows
    mov ax, RU_LINES
.rows:
    or ax, ax
    jnz .count
    inc ax
.count:
    mov [ru_rows], ax
    ; Six 68px buttons with 4px gaps; coordinate refresh also runs on mouse
    ; gestures, so dragging a window cannot leave stale clickable rectangles.
    mov di, ru_rects
    mov ax, [ru_x]
    add ax, 8
    mov bx, [ru_y]
    add bx, [ru_foot]
    add bx, 3
    mov cx, 6
.button:
    mov [di], ax
    mov [di+2], bx
    mov dx, ax
    add dx, 67
    mov [di+4], dx
    mov dx, bx
    add dx, 17
    mov [di+6], dx
    add di, 8
    add ax, 72
    loop .button
    ; The results pane: Summary's lower-left box or Compare's whole width.
    mov ax, [ru_lower]
    mov [ru_restop], ax
    mov ax, [ru_split]
    sub ax, 8
    mov [ru_resw], ax
    cmp byte [ru_view], 2
    jne .resbox
    mov word [ru_restop], 24
    mov ax, [ru_w]
    sub ax, 8
    mov [ru_resw], ax
.resbox:
    mov bx, ru_pres
    mov ax, [ru_resw]
    add ax, [ru_x]
    mov [bx+SP_SB+4], ax
    sub ax, RU_SBW
    mov [bx+SP_SB], ax
    mov ax, [ru_restop]
    add ax, [ru_y]
    mov [bx+SP_SB+2], ax
    mov ax, [ru_foot]
    sub ax, 4
    add ax, [ru_y]
    mov [bx+SP_SB+6], ax
    call ru_spclamp
    ; System Snapshot, then Details: frames and line pitch as they always were.
    mov bx, ru_psnap
    mov word [bx+SP_Y], 44
    mov word [bx+SP_PITCH], 10
    cmp byte [ru_small], 0
    je .snap
    mov word [bx+SP_Y], 42
    mov word [bx+SP_PITCH], 8
.snap:
    mov ax, 24
    mov dx, [ru_lower]
    sub dx, 8
    call ru_fgeom
    mov bx, ru_pdet
    mov ax, [ru_lower]
    add ax, 6
    add ax, [ru_offset]
    mov [bx+SP_Y], ax
    mov word [bx+SP_PITCH], 12
    cmp byte [ru_small], 0
    je .details
    mov word [bx+SP_PITCH], 8
.details:
    mov ax, [ru_lower]
    mov dx, [ru_foot]
    sub dx, 4
    call ru_fgeom
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Relative rectangles/text share these helpers; no adapter-specific offsets.
ru_frame:
    add ax, [ru_x]
    add cx, [ru_x]
    add bx, [ru_y]
    add dx, [ru_y]
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    ret
ru_text:
    push ax
    push cx
    push dx
    add cx, [ru_x]
    add dx, [ru_y]
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

ru_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [rl_win], si
    call ru_geometry
    cmp byte [rl_busy], 0
    jne .running
    cmp byte [ru_view], 1
    jne .dashboard
    call bl_paint
    jmp .buttons
.dashboard:
    mov cx, 12
    mov dx, 8
    mov si, ru_heading
    call ru_text
    cmp byte [ru_view], 2
    je .compare
    cmp byte [rl_ran], 0
    je .headline
    call ru_facts
.headline:
    call ru_headline
    call ru_snapshot
    call ru_hardware
.compare:
    call ru_results
    jmp .buttons
.running:
    cmp byte [rl_busy], 0
    je .buttons
    call ru_running_paint
    jmp .out
.buttons:
    call ru_buttons_paint
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

ru_buttons_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 4
    mov bx, [ru_foot]
    mov cx, [ru_w]
    sub cx, 4
    mov dx, [ru_h]
    sub dx, 2
    call ru_frame
    mov word [ru_buttonflags], 0
    mov word [ru_buttonflags+2], 0
    mov word [ru_buttonflags+4], 0
    xor bx, bx
    mov bl, [ru_view]
    shl bx, 1
    mov word [ru_buttonflags+bx], OS88UI_LATCH
    mov bx, ru_buttons
    mov ax, 1
.button:
    call os88ui_btn
    inc ax
    cmp ax, 7
    jb .button
    cmp word [ru_w], 600
    jb .nohint
    mov cx, 448
    mov dx, [ru_foot]
    add dx, 8
    mov si, ru_keys
    cmp byte [bl_saved], 0
    je .status
    mov si, ru_saved
.status:
    call ru_text
.nohint:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

ru_headline:
    mov ax, 8
    mov bx, 24
    mov cx, [ru_split]
    sub cx, 8
    mov dx, [ru_lower]
    sub dx, 8
    call ru_frame
    mov cx, 16
    mov dx, 30
    mov si, ru_headtitle
    call ru_text
    mov ax, [ru_overall]
    mov dx, [ru_overall+2]
    call ru_ratio
    cmp byte [rl_ran], 0
    je .pending
    cmp byte [ru_valid], 0
    jne .value
    mov si, ru_unresolved
    jmp .message
.pending:
    mov si, ru_ready
.message:
    mov cx, 24
    mov dx, 48
    call ru_text
    ret
.value:
    mov cx, 80
    mov dx, 44
    cmp byte [ru_small], 0
    je .cpu
    mov dx, 42
.cpu:
    cmp byte [ru_small], 0
    je .big
    mov cx, 28
.big:
    call ru_big
    call ru_gpaint
    cmp byte [ru_small], 0
    jne .out
    mov cx, 24
    mov dx, 78
    mov si, ru_relative
    call ru_text
    mov cx, 24
    mov bx, 96
    mov di, [ru_split]
    sub di, 40
    mov bp, 12
    mov ax, [ru_overall]
    mov dx, [ru_overall+2]
    mov byte [ru_ink], CRED
    call ru_bar
    xor di, di
.tick:
    mov ax, [ru_split]
    sub ax, 40
    mul di
    mov cx, 4
    div cx
    add ax, 24
    add ax, [ru_x]
    mov bx, [ru_y]
    add bx, 108
    mov dx, bx
    add dx, 4
    call OSAPI_GFX_VLINE
    sub ax, [ru_x]
    mov cx, ax
    push cx
    push di
    mov ax, [ru_scale]
    mov dx, [ru_scale+2]
    mov cx, di
    call bl_mul48
    mov cx, 4
    call bl_div48
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    call ru_axis_label
.length:
    mov bx, si
    xor cx, cx
.count:
    cmp byte [bx], 0
    je .label
    inc bx
    add cx, 8
    jmp .count
.label:
    pop di
    mov bx, cx
    shr bx, 1
    cmp di, 4
    jne .middle
    mov bx, cx
.middle:
    pop cx
    or di, di
    jz .text
    sub cx, bx
.text:
    mov dx, 113
    call ru_text
    inc di
    cmp di, 5
    jb .tick
.out:
    ret

; The three buckets' means beside the CPU + RAM headline, right-aligned in
; the headline box: "CPU      1.07x", "RAM", "Graphics".
ru_gpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, 44
    cmp byte [ru_small], 0
    je .first
    mov dx, 42
.first:
    xor bx, bx
.group:
    push dx
    mov di, bx
    shl di, 1
    shl di, 1
    mov ax, [ru_gscores+di]
    mov dx, [ru_gscores+di+2]
    push bx
    call ru_ratio                 ; clobbers BX, CX, DI, SI
    call ru_pad10
    pop bx
    call bl_lclr
    mov di, bx
    shl di, 1
    mov si, [ru_gshort+di]
    xor di, di
    call bl_lput
    mov si, ru_pad
    mov di, 9
    call bl_lput
    mov byte [bl_lscr+18], 0
    pop dx
    mov si, bl_lscr
    mov cx, [ru_split]
    sub cx, 156
    call ru_text
    add dx, 8
    cmp byte [ru_small], 0
    jne .next
    add dx, 2
.next:
    inc bx
    cmp bx, 3
    jb .group
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; SI = a NUL string -> SI = ru_pad, the same text space-padded to ten cells,
; so an opaque run covers whatever longer value was drawn there before.
ru_pad10:
    push ax
    push cx
    push di
    mov di, ru_pad
    mov cx, 10
.copy:
    mov al, [si]
    or al, al
    jz .fill
    mov [di], al
    inc si
    inc di
    loop .copy
    jmp .end
.fill:
    mov byte [di], ' '
    inc di
    loop .fill
.end:
    mov byte [di], 0
    mov si, ru_pad
    pop di
    pop cx
    pop ax
    ret

; DX:AX is an axis value in thousandths. Keep fractional ticks accurate.
ru_axis_label:
    mov bx, ax
    or bx, dx
    jnz .value
    mov si, ru_zerotick
    ret
.value:
    call ru_ratio
    mov bx, si
.scan:
    cmp byte [bx], 0
    je .out
    cmp byte [bx], '.'
    je .fraction
    inc bx
    jmp .scan
.fraction:
    cmp word [bx+1], '00'
    jne .out
    mov byte [bx], 'x'
    mov byte [bx+1], 0
.out:
    ret

; DX:AX index/1000 -> full-range decimal ratio, preserving the index.
ru_ratio:
    push ax
    push dx
    call bl_lclr
    mov cx, ax
    or cx, dx
    jnz .format
    mov si, ru_na
    xor di, di
    call bl_lput
    mov byte [bl_lscr+3], 0
    jmp .done
.format:
    mov cx, 1000
    call bl_div32
    push bx
    xor di, di
    mov cx, 7
    call bl_dec
    mov si, bl_lscr
.trim:
    cmp byte [si], ' '
    jne .decimal
    inc si
    jmp .trim
.decimal:
    mov byte [bl_lscr+7], '.'
    pop ax
    xor dx, dx
    mov cx, 10
    div cx
    xor dx, dx
    mov di, 8
    mov cx, 2
    call bl_dec
    cmp byte [bl_lscr+8], ' '
    jne .suffix
    mov byte [bl_lscr+8], '0'
.suffix:
    mov byte [bl_lscr+10], 'x'
    mov byte [bl_lscr+11], 0
    jmp .out
.done:
    mov si, bl_lscr
.out:
    pop dx
    pop ax
    ret

; Solid category colors on VGA; dither on monochrome adapters.
; The common scale is at least 100x; positive subpixel values get one pixel.
; DX:AX=index, CX=left, BX=top, DI=width, BP=height; relative coordinates.
ru_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [ru_barx], cx
    mov [ru_bary], bx
    mov [ru_barw], di
    mov [ru_barvalue], ax
    or [ru_barvalue], dx
    mov cx, di
    call bl_mul48
    mov bx, [ru_scale]
    mov cx, [ru_scale+2]
    call rl_div48by32
    cmp ax, di
    jb .width
    dec ax                          ; the right frame stays solid black
.width:
    or ax, ax
    jnz .filled
    cmp word [ru_barvalue], 0
    je .filled
    mov ax, 1
.filled:
    mul word [ru_anim]
    mov cx, RU_ANIM_STEPS
    div cx
    mov [ru_fillw], ax
    mov ax, [ru_barx]
    mov bx, [ru_bary]
    mov cx, ax
    add cx, di
    mov dx, bx
    add dx, bp
    call ru_frame
    mov ax, [ru_barx]
    inc ax
    add ax, [ru_x]
    mov bx, [ru_bary]
    inc bx
    add bx, [ru_y]
    mov dx, bx
    add dx, bp
    sub dx, 2
    cmp word [ru_fillw], 0
    je .rest
    mov cx, ax
    add cx, [ru_fillw]
    dec cx
    cmp byte [rl_video], VID_VGA
    jne .mono
    push ax
    mov al, [ru_ink]
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    jmp .painted
.mono:
    cmp byte [ru_hdr], 0          ; a bucket's mean is solid, its rows dithered
    je .dither
    call OSAPI_GFX_FILL           ; ru_frame left the pen black
    jmp .painted
.dither:
    mov si, ru_pattern
    call OSAPI_GFX_FILL_PAT
.painted:
    mov ax, cx
    inc ax
.rest:
    ; The unfilled interior goes white in the same pass, so a bar redrawn in
    ; place by a scroll or a rerun never keeps a longer bar's remains.
    mov cx, [ru_barx]
    add cx, [ru_barw]
    dec cx
    add cx, [ru_x]
    cmp ax, cx
    jg .out
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Big five-by-seven bitmap glyphs, rendered as 3x3 blocks using OS primitives.
; Six bounded characters; no fonts or instructions outside the 8086 tier.
ru_big:
    mov [ru_bigx], cx
    mov [ru_bigy], dx
    mov bp, si
    mov al, [ds:bp]
    or al, al
    jz .out
.char:
    mov al, [ds:bp]
    inc bp
    cmp al, '0'
    jb .special
    cmp al, '9'
    ja .special
    sub al, '0'
    jmp .glyph
.special:
    mov ah, al
    mov al, 10
    cmp ah, '.'
    je .glyph
    mov al, 11
    cmp ah, 'x'
    je .glyph
    mov al, 12
.glyph:
    xor ah, ah
    mov cx, 7
    mul cx
    mov si, ax
    add si, ru_digits
    mov word [ru_gy], 0
.row:
    mov al, [si]
    mov [ru_bits], al
    inc si
    mov word [ru_gx], 0
.bit:
    test byte [ru_bits], 0x10
    jz .next
    push si
    push di
    push bp
    mov ax, [ru_bigx]
    add ax, [ru_gx]
    add ax, [ru_x]
    mov bx, [ru_bigy]
    add bx, [ru_gy]
    add bx, [ru_y]
    mov cx, ax
    add cx, 2
    mov dx, bx
    add dx, 2
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop bp
    pop di
    pop si
.next:
    shl byte [ru_bits], 1
    add word [ru_gx], 3
    cmp word [ru_gx], 15
    jb .bit
    add word [ru_gy], 3
    cmp word [ru_gy], 21
    jb .row
    add word [ru_bigx], 18
    cmp byte [ds:bp], 0
    jne .char
.out:
    ret

; -----------------------------------------------------------------------------
; Scroll panes. Results, System Snapshot and Details are each a list that can
; hold more lines than its frame does on a short screen - CGA's 640x200 above
; all, and Hercules' Summary too. One record per pane carries the position and
; the shared os88ui scroll-bar block (SPEC.md 13.10), filled by ru_geometry, so
; the painter and the hit test cannot disagree about where the parts are. The
; bar is drawn only when the pane overflows; a pane that fits looks as it did.
; PgUp/PgDn/Up/Down/Home/End scroll Results; the bar scrolls any of the three.
; -----------------------------------------------------------------------------

; BX = pane -> CF = 1 when it holds more lines than it shows
ru_spover:
    push ax
    mov ax, [bx+SP_VIS]
    cmp ax, [bx+SP_CNT]
    pop ax
    ret

; BX = pane -> AX = the last legal first line
ru_spmax:
    mov ax, [bx+SP_CNT]
    sub ax, [bx+SP_VIS]
    jnc .out
    xor ax, ax
.out:
    ret

; BX = pane: keep its position legal after the frame changed size
ru_spclamp:
    push ax
    call ru_spmax
    cmp [bx+SP_TOP], ax
    jbe .out
    mov [bx+SP_TOP], ax
.out:
    pop ax
    ret

; BX = pane: copy total, fit and position into the bar's block
ru_spset:
    push ax
    mov ax, [bx+SP_CNT]
    mov [bx+SP_SB+8], ax
    mov ax, [bx+SP_VIS]
    mov [bx+SP_SB+10], ax
    mov ax, [bx+SP_TOP]
    mov [bx+SP_SB+12], ax
    pop ax
    ret

; BX = pane: draw its bar if it overflows. Preserves every register.
ru_spbar:
    call ru_spover
    jnc .out
    call ru_spset
    push bx
    add bx, SP_SB
    call os88ui_sbar
    pop bx
.out:
    ret

; BX = pane, AX = the wanted first line (signed; clamped). Repaints the pane's
; lines and moves the thumb only when the position changed - no erase pass,
; because every line painter is opaque across its whole width.
ru_spto:
    push ax
    push cx
    or ax, ax
    jns .low
    xor ax, ax
.low:
    mov cx, ax
    call ru_spmax
    cmp cx, ax
    jbe .set
    mov cx, ax
.set:
    mov ax, [bx+SP_TOP]
    cmp cx, ax
    je .out
    mov [bx+SP_TOP], cx
    push ax
    push bx
    call [bx+SP_PAINT]
    pop bx
    call ru_spset
    pop ax
    push bx
    add bx, SP_SB
    call os88ui_sbmove              ; AX = the position it was at
    pop bx
.out:
    pop cx
    pop ax
    ret

; BX = pane, CX/DX = a press in screen coordinates -> CF = 1 when the press
; was the pane's bar. Arrows step a line, the track pages; the thumb takes
; no gesture here, on fdlg's precedent (os88ui_sbhit's header).
ru_sphit:
    push ax
    push cx
    call ru_spover
    jnc .no
    call ru_spset
    push bx
    add bx, SP_SB
    call os88ui_sbhit
    pop bx
    mov cx, -1
    cmp al, OS88UI_SBUP
    je .by
    mov cx, 1
    cmp al, OS88UI_SBDOWN
    je .by
    mov cx, [bx+SP_VIS]
    cmp al, OS88UI_SBPGDN
    je .by
    neg cx
    cmp al, OS88UI_SBPGUP
    je .by
    cmp al, OS88UI_SBTHUMB
    je .yes
.no:
    pop cx
    pop ax
    clc
    ret
.by:
    mov ax, [bx+SP_TOP]
    add ax, cx
    call ru_spto
.yes:
    pop cx
    pop ax
    stc
    ret

; CX/DX = a press -> CF = 1 when one of the visible panes' bars took it
ru_panehit:
    cmp byte [rl_busy], 0
    jne .no
    cmp byte [rl_ran], 0
    je .no
    cmp byte [ru_view], 1
    je .no
    push bx
    mov bx, ru_pres
    call ru_sphit
    jc .out
    cmp byte [ru_view], 0
    jne .miss
    mov bx, ru_psnap
    call ru_sphit
    jc .out
    mov bx, ru_pdet
    call ru_sphit
    jc .out
.miss:
    clc
.out:
    pop bx
    ret
.no:
    clc
    ret

; BX = a fact pane, AX = its frame's top, DX = its bottom (relative). Fills
; the bar's block and how many lines fit, then the text width: the bar takes
; two cells off it only when there is a bar.
ru_fgeom:
    push ax
    push cx
    push dx
    add ax, [ru_y]
    mov [bx+SP_SB+2], ax
    mov cx, dx
    add cx, [ru_y]
    mov [bx+SP_SB+6], cx
    mov cx, [ru_w]
    sub cx, 8
    add cx, [ru_x]
    mov [bx+SP_SB+4], cx
    sub cx, RU_SBW
    mov [bx+SP_SB], cx
    sub dx, 9                       ; a line fits when its 8px cell ends
    sub dx, [bx+SP_Y]               ; above the frame's bottom rule
    mov ax, 1
    jl .vis
    mov ax, dx
    xor dx, dx
    div word [bx+SP_PITCH]
    inc ax
.vis:
    cmp ax, [bx+SP_CNT]
    jbe .fit
    mov ax, [bx+SP_CNT]
.fit:
    mov [bx+SP_VIS], ax
    mov ax, [ru_w]
    sub ax, [ru_split]
    sub ax, 16
    call ru_spover
    jnc .cols
    sub ax, 16
.cols:
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [bx+SP_COLS], ax
    call ru_spclamp
    pop dx
    pop cx
    pop ax
    ret

; CX/DX relative, SI label, DI value: ten label cells, then the value, the
; line space-padded to [ru_kvcols] cells so it is opaque across its pane.
; Long brand strings are cut; the complete ones stay in Detailed and the file.
ru_kvs:
    push ax
    push bx
    push si
    push di
    call bl_lclr
    push di
    xor di, di
    call bl_lput
    pop si
    mov di, 10
    call bl_lput
    mov bx, [ru_kvcols]
    cmp bx, BL_MAXLINE
    jbe .cut
    mov bx, BL_MAXLINE
.cut:
    mov byte [bl_lscr+bx], 0
    mov si, bl_lscr
    call ru_text
    pop di
    pop si
    pop bx
    pop ax
    ret

; BX = a fact pane: paint its visible lines out of ru_ftab
ru_factpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [ru_cur], bx
    mov ax, [bx+SP_COLS]
    mov [ru_kvcols], ax
    mov dx, [bx+SP_Y]
    xor cx, cx
.line:
    mov bx, [ru_cur]
    cmp cx, [bx+SP_VIS]
    jae .out
    mov ax, [bx+SP_TOP]
    add ax, cx
    cmp ax, [bx+SP_CNT]
    jae .out
    add ax, [bx+SP_FIRST]
    shl ax, 1
    shl ax, 1
    mov si, ax
    mov di, [ru_ftab+si+2]
    mov si, [ru_ftab+si]
    push cx
    mov cx, [ru_split]
    add cx, 8
    call ru_kvs
    pop cx
    add dx, [bx+SP_PITCH]
    inc cx
    jmp .line
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Every fact either fact pane shows, as (label, value string) pairs: the
; Snapshot's eight, then the Details' twelve. Rebuilt on each Summary paint.
ru_facts:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ru_fcopy's movsb is within the package
    cld
    mov di, ru_ftab
    mov si, ru_cpu
    mov ax, [rl_cpuname]
    call ru_fstr
    call ru_fclock
    mov si, ru_bioskb
    mov ax, [rl_convkb]
    call ru_fnum
    mov si, ru_freekb
    mov ax, [rl_free]
    call ru_fnum
    cmp byte [rl_mapok], 0
    jne .usable
    mov si, ru_extkb
    cmp byte [rl_extok], 0
    je .unknown
    mov ax, [rl_extkb]
    call ru_fnum
    jmp .video
.usable:
    mov si, ru_usablekb
    ; A huge E820 figure keeps its complete 32-bit value in Detailed.
    cmp word [rl_ramkb+2], 0
    je .number
    mov ax, ru_seedetailed
    call ru_fstr
    jmp .video
.number:
    mov ax, [rl_ramkb]
    call ru_fnum
    jmp .video
.unknown:
    mov ax, ru_na
    call ru_fstr
.video:
    mov si, ru_adapter
    mov bl, [rl_video]
    xor bh, bh
    shl bx, 1
    mov ax, [ru_adapters+bx]
    call ru_fstr
    mov si, ru_screen
    mov ax, [rl_vw]
    call ru_fnum
    mov si, ru_screenh
    mov ax, [rl_vh]
    call ru_fnum
    mov si, ru_bios
    mov ax, rl_biosdate
    call ru_fstr
    mov si, ru_model
    push es
    mov ax, 0xF000
    mov es, ax
    xor ax, ax
    mov al, [es:0xFFFE]
    pop es
    call ru_fnum
    xor bx, bx
.port:
    mov si, [ru_portlabels+bx]
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:bx]
    pop es
    call ru_fnum
    add bx, 2
    cmp bx, 14
    jb .port
    mov si, ru_bpp
    mov al, [rl_video+1]
    xor ah, ah
    call ru_fnum
    mov si, ru_xfreekb
    mov ax, [rl_xfree]
    call ru_fnum
    mov si, ru_largestkb
    mov ax, [rl_largest]
    call ru_fnum
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DI = the next ru_ftab slot. ru_fstr: SI label, AX value string. ru_fnum: SI
; label, AX a number, right-aligned in eight cells. ru_fcopy: SI label, the
; value already in bl_lscr. All three advance DI by one slot.
ru_fstr:
    mov [di], si
    mov [di+2], ax
    add di, 4
    ret
ru_fnum:
    push cx
    push dx
    call bl_lclr
    push di
    xor dx, dx
    mov cx, 8
    xor di, di
    call bl_dec
    pop di
    mov byte [bl_lscr+8], 0
    pop dx
    pop cx
ru_fcopy:
    push ax
    push bx
    push cx
    push si
    mov ax, di
    sub ax, ru_ftab
    mov bx, ax
    shl ax, 1
    add ax, bx                      ; slot * 12: this slot's own buffer
    add ax, ru_fbuf
    push di
    mov di, ax
    mov si, bl_lscr
    mov cx, 10
    rep movsb
    pop di
    pop si
    pop cx
    pop bx
    call ru_fstr
    pop ax
    ret

; The clock fact: SI = its label, NNNNNN.NN or n/a.
ru_fclock:
    mov si, [ru_clocklabel]
    mov ax, [ru_clockvalue]
    mov dx, [ru_clockvalue+2]
    mov cx, ax
    or cx, dx
    jnz .value
    mov ax, ru_na
    jmp ru_fstr
.value:
    mov cx, 100
    call bl_div32                   ; DX:AX = whole MHz, BX = hundredths
    push bx
    call bl_lclr
    push di
    xor di, di
    mov cx, 6
    call bl_dec
    mov byte [bl_lscr+6], '.'
    pop di
    pop ax
    push di
    xor dx, dx
    mov di, 7
    mov cx, 2
    call bl_dec
    pop di
    cmp byte [bl_lscr+7], ' '
    jne .decimal
    mov byte [bl_lscr+7], '0'
.decimal:
    mov byte [bl_lscr+9], 0
    jmp ru_fcopy

ru_snapshot:
    mov ax, [ru_split]
    mov bx, 24
    mov cx, [ru_w]
    sub cx, 8
    mov dx, [ru_lower]
    sub dx, 8
    call ru_frame
    mov cx, [ru_split]
    add cx, 8
    mov dx, 30
    mov si, ru_system
    call ru_text
    cmp byte [rl_ran], 0
    jne .facts
    mov si, ru_query
    add dx, 14
    call ru_text
    ret
.facts:
    mov bx, ru_psnap
    call ru_factpaint
    call ru_spbar
    ret

ru_hardware:
    mov ax, [ru_split]
    mov bx, [ru_lower]
    mov cx, [ru_w]
    sub cx, 8
    mov dx, [ru_foot]
    sub dx, 4
    call ru_frame
    mov cx, [ru_split]
    add cx, 8
    mov dx, [ru_lower]
    add dx, 6
    mov si, ru_details
    call ru_text
    cmp byte [rl_ran], 0
    je .out
    mov bx, ru_pdet
    call ru_factpaint
    call ru_spbar
.out:
    ret

; Results are 28 lines: each bucket's header carries its mean, its workloads
; follow it indented. ru_geometry has already placed the box (ru_restop,
; ru_resw) and its bar.
ru_results:
    mov ax, 8
    mov bx, [ru_restop]
    mov cx, [ru_resw]
    mov dx, [ru_foot]
    sub dx, 4
    call ru_frame
    mov cx, 16
    mov dx, [ru_restop]
    add dx, 6
    cmp byte [ru_small], 0
    je .title
    sub dx, 2                       ; a clear row above the first line
.title:
    mov si, ru_resulttitle
    call ru_text
    cmp byte [rl_ran], 0
    jne .start
    mov cx, 16
    add dx, 14
    mov si, ru_ready
    call ru_text
    ret
.start:
    call ru_rowspaint
    mov bx, ru_pres
    call ru_spbar
    ret

; The results pane's painter: its visible lines from [ru_page] down.
ru_rowspaint:
    push ax
    push bx
    push cx
    mov ax, [ru_restop]
    add ax, [ru_offset]
    mov [ru_resulty], ax
    mov bx, [ru_page]
    mov cx, [ru_rows]
.line:
    jcxz .out
    cmp bx, RU_LINES
    jae .out
    call ru_drawline
    mov ax, [ru_pitch]
    add [ru_resulty], ax
    inc bx
    dec cx
    jmp .line
.out:
    pop cx
    pop bx
    pop ax
    ret

; BX = a display line -> SI = its label, DX:AX = its index, [ru_ink] its
; bucket's colour and [ru_hdr] = 1 for a bucket's header.
ru_lineinfo:
    push bx
    push di
    mov bl, [ru_lines+bx]
    xor bh, bh
    mov byte [ru_hdr], 0
    test bl, 0x80
    jz .row
    and bl, 3
    mov byte [ru_hdr], 1
    mov di, bx
    shl di, 1
    mov si, [ru_glabels+di]
    cmp bx, 2
    jne .score
    cmp word [rl_gfxbase], 0
    jne .score
    mov si, ru_gnoref
.score:
    shl di, 1
    mov ax, [ru_gscores+di]
    mov dx, [ru_gscores+di+2]
    mov bl, [ru_gfirst+bx]
    jmp .ink
.row:
    mov di, bx
    shl di, 1
    mov si, [ru_labels+di]
    shl di, 1
    mov ax, [ru_scores+di]
    mov dx, [ru_scores+di+2]
.ink:
    mov byte [ru_ink], CBLUE
    cmp bx, 4
    jb .out
    mov byte [ru_ink], CGREEN
    cmp bx, 6
    jb .out
    mov byte [ru_ink], CRED
.out:
    pop di
    pop bx
    ret

; BX = a display line at [ru_resulty]: label, bar and ratio, each opaque
; across its whole cell span, so a scroll repaints it in place.
ru_drawline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call ru_lineinfo
    push ax
    push dx
    call bl_lclr
    xor di, di
    cmp byte [ru_hdr], 0
    jne .label
    inc di                          ; workloads sit one cell in
.label:
    call bl_lput
    mov byte [bl_lscr+17], 0        ; seventeen cells: x 16..151, to the bar
    mov si, bl_lscr
    mov cx, 16
    mov dx, [ru_resulty]
    call ru_text
    pop dx
    pop ax
    call ru_ratio
    call ru_pad10
    push ax
    push dx
    mov cx, [ru_resw]
    sub cx, 96
    mov dx, [ru_resulty]
    call ru_text
    pop dx
    pop ax
    mov cx, 152
    mov bx, [ru_resulty]
    mov di, [ru_resw]
    sub di, 256
    mov bp, 8
    call ru_bar
    mov byte [ru_hdr], 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
 ; Completion animation runs on the UI timer, outside every timed span.
ru_start_animation:
    mov word [ru_anim], 0
    mov bx, [rl_win]
    mov ax, 1
    call OSAPI_WM_TIMER
    jnc .out
    mov word [ru_anim], RU_ANIM_STEPS ; kern_small has no UI timers
.out:
    ret
ru_timer:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [rl_busy], 0
    jne .out
    cmp word [ru_anim], RU_ANIM_STEPS
    jae .out
    inc word [ru_anim]
    call ru_geometry
    cmp byte [ru_view], 1
    je .schedule
    cmp byte [ru_view], 2
    je .results
    cmp byte [ru_small], 0
    jne .results
    mov cx, 24
    mov bx, 96
    mov di, [ru_split]
    sub di, 40
    mov bp, 12
    mov ax, [ru_overall]
    mov dx, [ru_overall+2]
    mov byte [ru_ink], CRED
    call ru_bar
.results:
    call ru_rowspaint             ; the frame, title and bar are already up
.schedule:
    cmp word [ru_anim], RU_ANIM_STEPS
    jae .out
    mov bx, [rl_win]
    mov ax, 1
    call OSAPI_WM_TIMER
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

; Progress is drawn between rows, never inside a timing bracket.
ru_progress_update:
    call ru_geometry
    mov ax, [ru_x]
    add ax, 304
    mov bx, [ru_y]
    add bx, 18
    mov cx, [ru_x]
    add cx, [ru_w]
    sub cx, 8
    mov dx, [ru_y]
    add dx, 124
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    call ru_running_paint
    ret
ru_running_paint:
    mov cx, 16
    mov dx, 8
    mov si, ru_runningtitle
    call ru_text
    call bl_lclr
    mov si, ru_runlabel
    xor di, di
    call bl_lput
    mov ax, [rl_pass]
    inc ax
    xor dx, dx
    mov cx, 1
    mov di, 4
    call bl_dec
    mov si, ru_runsuffix
    mov di, 5
    call bl_lput
    mov byte [bl_lscr+30], 0
    mov si, bl_lscr
    mov cx, 316
    mov dx, 24
    call ru_text
    mov si, [rl_active_label]
    mov cx, 316
    mov dx, 40
    call ru_text
    cmp word [rl_labwin], 0
    jne .labshown
    mov ax, 12
    mov bx, 56
    mov cx, 276
    mov dx, 124
    call ru_frame
.labshown:
    mov cx, 316
    mov dx, 64
    mov si, ru_livepreview
    call ru_text
    mov cx, 316
    mov dx, 80
    mov si, ru_progresshint
    call ru_text
    mov ax, [rl_pass]
    mov cx, RL_ROWS
    mul cx
    add ax, [rl_active_row]
    inc ax
    xor dx, dx
    mov cx, 1000
    call bl_mul48
    mov cx, RL_ROWS*RL_RUNS
    call bl_div48
    mov ax, [bl_m]
    mov dx, [bl_m+2]
    mov word [ru_scale], 1000
    mov word [ru_scale+2], 0
    mov word [ru_anim], RU_ANIM_STEPS
    mov byte [ru_ink], CBLUE
    mov cx, 316
    mov bx, 104
    mov di, [ru_w]
    sub di, 332
    mov bp, 12
    call ru_bar
    ret
ru_runningtitle: db 'Benchmark in progress - 3 runs averaged', 0
ru_runlabel: db 'Run ', 0
ru_runsuffix: db '/3  // fixed workloads', 0
ru_livepreview: db 'Live graphics preview', 0
ru_progresshint: db 'Please keep mouse still', 0

ru_navigation:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [rl_win], si
    cmp al, 9
    je .tab
    cmp ah, 0x3B
    je .help
    cmp byte [ru_view], 1
    jne .page
    call bl_key
    jc .out
    call rl_paint                 ; opaque report rows replace old rows
    jmp .out
.tab:
    inc byte [ru_view]
    cmp byte [ru_view], 3
    jb .draw
    mov byte [ru_view], 0
    jmp .draw
.help:
    mov byte [ru_view], 1
    mov word [bl_top], 0
    jmp .draw
.page:
    ; Summary and Compare scroll the results pane in place: the lines and the
    ; thumb are redrawn, never the window.
    cmp byte [rl_ran], 0
    je .out
    call ru_geometry
    mov bx, ru_pres
    mov cx, ax                    ; CL = ascii, CH = scan code
    mov ax, [ru_page]
    cmp ch, 0x51
    je .pgdn
    cmp cl, ' '
    je .pgdn
    cmp ch, 0x49
    je .pgup
    cmp ch, 0x50
    je .down
    cmp ch, 0x48
    je .up
    cmp ch, 0x4F
    je .end
    cmp ch, 0x47
    jne .out
    xor ax, ax
    jmp .go
.pgdn:
    add ax, [ru_rows]
    jmp .go
.pgup:
    sub ax, [ru_rows]
    jmp .go
.down:
    inc ax
    jmp .go
.up:
    dec ax
    jmp .go
.end:
    mov ax, RU_LINES              ; ru_spto clamps to the last full page
.go:
    call ru_spto
    jmp .out
.draw:
    call rl_repaint
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

ru_press:
    call ru_geometry
    call ru_panehit                 ; a pane's scroll bar, or else...
    jc .done
    jmp os88ui_btnclick             ; SDK owns every other press
.done:
    ret
ru_drag:
    push bx
    call ru_geometry
    mov bx, ru_buttons
    call os88ui_btndrag
    pop bx
    ret
ru_mouseup:
    push ax
    push bx
    call ru_geometry
    mov bx, ru_buttons
    call os88ui_btnup
    or ax, ax
    jz .out
    mov bx, ax
    dec bx
    mov al, [ru_actions+bx]
    xor ah, ah
    call rl_key
.out:
    pop bx
    pop ax
    ret

SP_TOP   equ 0                   ; scroll pane record: first visible line
SP_VIS   equ 2                   ; lines the frame holds
SP_CNT   equ 4                   ; lines in all
SP_PAINT equ 6                   ; repaints the visible lines, opaque
SP_SB    equ 8                   ; os88ui bar block: x1 y1 x2 y2 total fit pos
SP_FIRST equ 22                  ; fact panes: first ru_ftab slot
SP_Y     equ 24                  ; fact panes: first line's relative y
SP_PITCH equ 26
SP_COLS  equ 28                  ; fact panes: text cells a line
RU_SBW   equ 14                  ; the bar's width, as every caller draws it
RU_LINES equ RL_ROWS + 3         ; workloads plus three bucket headers
RU_FACTS equ 20                  ; Snapshot's eight, Details' twelve
ru_pres:                         ; Results
ru_page: dw 0
ru_rows: dw 0
    dw RU_LINES, ru_rowspaint
    times 7 dw 0
ru_psnap:                        ; System Snapshot
    dw 0, 0, 8, ru_factpaint
    times 7 dw 0
    dw 0, 0, 0, 0
ru_pdet:                         ; Details
    dw 0, 0, 12, ru_factpaint
    times 7 dw 0
    dw 8, 0, 0, 0
ru_cur: dw 0
ru_kvcols: dw 0
ru_hdr: db 0
ru_pad: times 11 db 0
ru_ftab: times RU_FACTS*2 dw 0
ru_fbuf: times RU_FACTS*12 db 0
; Display lines: 0x80+n is bucket n's header, else a workload row.
ru_lines:
    db 0x80, 0, 1, 2, 3
    db 0x81, 4, 5
    db 0x82
%assign ru_i 6
%rep RL_ROWS - 6
    db ru_i
%assign ru_i ru_i + 1
%endrep
ru_gfirst: db 0, 4, 6
ru_gcount: db 4, 2, RL_ROWS - 6
ru_gscores: times 3 dd 0
ru_glabels: dw ru_gcpu, ru_gram, ru_ggfx
ru_gshort: dw ru_cpu, ru_gramshort, ru_gfxshort
ru_gcpu: db 'CPU average', 0
ru_gram: db 'RAM average', 0
ru_ggfx: db 'Graphics average', 0
ru_gnoref: db 'Graphics: no ref', 0
ru_gramshort: db 'RAM', 0
ru_gfxshort: db 'Graphics', 0
ru_view: db 0                    ; 0 Summary, 1 Detailed, 2 Compare
ru_small: db 0
ru_valid: db 0
ru_bits: db 0
ru_row: dw 0
ru_x: dw 0
ru_y: dw 0
ru_w: dw 0
ru_h: dw 0
ru_split: dw 0
ru_lower: dw 0
ru_foot: dw 0
ru_pitch: dw 0
ru_offset: dw 0
ru_port: dw 0
ru_resw: dw 0
ru_restop: dw 0
ru_resulty: dw 0
ru_barx: dw 0
ru_bary: dw 0
ru_barw: dw 0
ru_fillw: dw 0
ru_barvalue: dw 0
RU_ANIM_STEPS equ 12
ru_anim: dw RU_ANIM_STEPS
ru_ink: db CRED
ru_scale: dd 5000
ru_bigx: dw 0
ru_bigy: dw 0
ru_gx: dw 0
ru_gy: dw 0
ru_value: times 16 db 0
ru_sum: times 3 dw 0
ru_overall: dd 0
ru_clockvalue: dd 0
ru_clocklabel: dw ru_mhzest
ru_scores: times RL_ROWS dd 0
ru_rects: times 24 dw 0
ru_buttonflags: times 6 dw 0
OS88UI_BTNREC ru_buttons, ru_rects, ru_buttonlabels, ru_buttonflags, 6
ru_buttonlabels: dw ru_summary, ru_detailed, ru_compare, rl_runitem, ru_save, ru_quit
ru_actions: db 'udcrsq'
ru_summary: db 'Summary', 0
ru_detailed: db 'Detailed', 0
ru_compare: db 'Compare', 0
ru_save: db 'Save...', 0
ru_quit: db 'Quit', 0
ru_heading: db 'PC Performance Benchmark                       REDLINE 1.0', 0
ru_headtitle: db 'CPU + RAM Performance', 0
ru_relative: db '(relative to the 4.77 MHz PC)', 0
ru_system: db 'System Snapshot', 0
ru_details: db 'Details', 0
ru_resulttitle: db 'Benchmark Results (4.77 MHz PC)', 0
ru_ready: db 'Press R / Run to benchmark', 0
ru_unresolved: db 'Timing below resolution', 0
ru_query: db 'Run to query the system', 0
ru_na: db 'n/a', 0
ru_seedetailed: db 'See Detailed', 0
ru_keys: db 'PgUp/PgDn  F1 Help', 0
ru_saved: db 'REDLINE.TXT saved  ', 0
ru_cpu: db 'CPU', 0
ru_mhzest: db 'MHz est', 0
ru_mhznom: db 'MHz nom', 0
ru_zerotick: db '0x', 0
ru_mhztsc: db 'TSC MHz', 0
ru_bioskb: db 'BIOS KB', 0
ru_freekb: db 'Free KB', 0
ru_extkb: db 'Ext KB', 0
ru_usablekb: db 'Usable KB', 0
ru_adapter: db 'Video', 0
ru_screen: db 'Width px', 0
ru_screenh: db 'Height px', 0
ru_bpp: db 'Depth bpp', 0
ru_xfreekb: db 'OS ext KB', 0
ru_largestkb: db 'LargestKB', 0
ru_bios: db 'BIOS date', 0
ru_model: db 'ROM model', 0
ru_portlabels: dw ru_com1, ru_com2, ru_com3, ru_com4, ru_lpt1, ru_lpt2, ru_lpt3
ru_com1: db 'COM1 base', 0
ru_com2: db 'COM2 base', 0
ru_com3: db 'COM3 base', 0
ru_com4: db 'COM4 base', 0
ru_lpt1: db 'LPT1 base', 0
ru_lpt2: db 'LPT2 base', 0
ru_lpt3: db 'LPT3 base', 0
ru_adapters: dw ru_vga, ru_herc, ru_cga
ru_vga: db 'VGA/EGA', 0
ru_herc: db 'Hercules', 0
ru_cga: db 'CGA', 0
ru_labels: dw ru_alu, ru_rotate, ru_mul, ru_div, ru_copy, ru_fill
           dw ru_gfill, ru_line, ru_frame_label, ru_text_label, ru_mono, ru_packed
    dw ru_fill_large, ru_lines_many, ru_frames_many, ru_text_grid, ru_blit1_large, ru_blit4_large
    dw ru_wire, ru_shaded, ru_fractal, ru_patternblit, ru_scroll, ru_composition, ru_resize
ru_alu: db 'CPU - ALU', 0
ru_rotate: db 'Rotate', 0
ru_mul: db 'MUL', 0
ru_div: db 'DIV', 0
ru_copy: db 'RAM Copy', 0
ru_fill: db 'RAM Fill', 0
ru_gfill: db 'Graphics Fill', 0
ru_line: db 'Horizontal Line', 0
ru_frame_label: db 'Graphics Frame', 0
ru_text_label: db 'Opaque Text', 0
ru_mono: db 'Mono Blit', 0
ru_packed: db 'Packed Blit', 0
ru_fill_large: db '2 Canvas fill', 0
ru_lines_many: db '2 Line field', 0
ru_frames_many: db '2 Nested frames', 0
ru_text_grid: db '2 Text grid', 0
ru_blit1_large: db '2 Mono 128xH', 0
ru_blit4_large: db '2 Packed 128xH/2', 0
ru_wire: db '3 Wireframe cube', 0
ru_shaded: db '3 Shaded cube', 0
ru_fractal: db '3 Mandelbrot', 0
ru_patternblit: db '3 Patterned blit', 0
ru_scroll: db '3 Canvas scroll', 0
ru_composition: db '3 Moving windows', 0
ru_resize: db '3 Window resize', 0
ru_pattern: db 0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55
ru_digits:
    db 14,17,19,21,25,17,14         ; 0
    db 4,12,4,4,4,4,14              ; 1
    db 14,17,1,2,4,8,31             ; 2
    db 30,1,1,14,1,1,30             ; 3
    db 2,6,10,18,31,2,2             ; 4
    db 31,16,16,30,1,1,30           ; 5
    db 14,16,16,30,17,17,14         ; 6
    db 31,1,2,4,8,8,8               ; 7
    db 14,17,17,14,17,17,14          ; 8
    db 14,17,17,15,1,1,14           ; 9
    db 0,0,0,0,0,6,6                ; decimal
    db 0,17,10,4,10,17,0            ; x
    db 0,0,0,0,0,0,0                ; blank
