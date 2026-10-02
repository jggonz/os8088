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
    mov ax, [rl_baseline+di]
    mov dx, [rl_baseline+di+2]
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
    cmp di, 48
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
    mov cx, 12
    div cx
    cmp ax, 10
    jae .spacing
    mov ax, 10
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
    cmp ax, 12
    jbe .rows
    mov ax, 12
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
    call ru_headline
    call ru_snapshot
    call ru_hardware
.compare:
    call ru_results
.buttons:
    call ru_buttons_paint
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
    mov cx, 448
    mov dx, [ru_foot]
    add dx, 8
    mov si, ru_keys
    cmp byte [bl_saved], 0
    je .status
    mov si, ru_saved
.status:
    call ru_text
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
    or di, di
    jz .label
    sub cx, 8
    cmp di, 4
    jne .label
    sub cx, 8
.label:
    mov bx, di
    shl bx, 1
    mov si, [ru_ticklabels+bx]
    mov dx, 113
    call ru_text
    inc di
    cmp di, 5
    jb .tick
.out:
    ret

; DX:AX index/1000 -> bounded printable xx.xx x. Preserve the original index.
ru_ratio:
    push ax
    push dx
    call bl_lclr
    cmp dx, 1
    ja .overflow
    cmp dx, 0
    jne .format
    cmp ax, 0
    jne .format
    mov si, ru_na
    mov di, 0
    call bl_lput
    jmp .done
.format:
    mov cx, 1000
    call bl_div32
    cmp ax, 99
    ja .overflow
    push bx
    xor dx, dx
    mov di, 0
    mov cx, 2
    call bl_dec
    mov byte [bl_lscr+2], '.'
    pop ax
    xor dx, dx
    mov cx, 10
    div cx
    mov di, 3
    mov cx, 2
    call bl_dec
    cmp byte [bl_lscr+3], ' '
    jne .suffix
    mov byte [bl_lscr+3], '0'
.suffix:
    mov byte [bl_lscr+5], 'x'
    jmp .done
.overflow:
    mov si, ru_overflow
    xor di, di
    call bl_lput
.done:
    mov byte [bl_lscr+6], 0
    mov si, bl_lscr
    pop dx
    pop ax
    ret

; One-store dithered/white bar segments. 4x fills the bar, values stay numeric.
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
    or dx, dx
    jnz .cap
    cmp ax, 4000
    jbe .scale
.cap:
    mov ax, 4000
.scale:
    mul di
    mov cx, 4000
    div cx
    cmp ax, di
    jb .width
    dec ax                          ; the right frame pixel stays solid black
.width:
    mov [ru_fillw], ax
    mov ax, [ru_barx]
    mov bx, [ru_bary]
    mov cx, ax
    add cx, di
    mov dx, bx
    add dx, bp
    call ru_frame
    cmp word [ru_fillw], 0
    je .out
    mov ax, [ru_barx]
    inc ax
    add ax, [ru_x]
    mov cx, ax
    add cx, [ru_fillw]
    dec cx
    mov bx, [ru_bary]
    inc bx
    add bx, [ru_y]
    mov dx, bx
    add dx, bp
    sub dx, 2
    mov si, ru_pattern
    call OSAPI_GFX_FILL_PAT
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
    mov di, 6
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
    dec di
    jnz .char
    ret

; Small right-hand facts: ten label cells plus a bounded printable value.
; CX/DX relative, SI label, DI string. The snapshot truncates long brands;
; the original complete strings remain in Detailed and the saved report.
ru_kvs:
    push cx
    push dx
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    pop si
    mov di, 10
    call bl_lput
    mov bx, [ru_w]
    sub bx, [ru_split]
    sub bx, 16
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov byte [bl_lscr+bx], 0          ; keep opaque text inside its panel
    mov si, bl_lscr
    pop dx
    pop cx
    call ru_text
    ret
ru_kvn:
    push cx
    push dx
    push si
    call bl_lclr
    xor dx, dx
    mov cx, 8
    xor di, di
    call bl_dec
    mov byte [bl_lscr+8], 0
    mov si, bl_lscr
    mov di, ru_value
    mov cx, 9
    cld
    rep movsb                       ; ES = DS supplied by dashboard facts
    mov di, ru_value
    pop si
    pop dx
    pop cx
    jmp ru_kvs

ru_snapshot:
    push ds
    pop es
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
    mov cx, [ru_split]
    add cx, 8
    mov dx, 44
    cmp byte [ru_small], 0
    je .cpupos
    mov dx, 42
.cpupos:
    mov si, ru_cpu
    mov di, [rl_cpuname]
    call ru_kvs
    mov si, [ru_clocklabel]
    mov ax, [ru_clockvalue]
    mov dx, [ru_clockvalue+2]
    mov cx, 100
    call bl_div32
    push bx
    call bl_lclr
    xor di, di
    mov cx, 6
    call bl_dec
    mov byte [bl_lscr+6], '.'
    pop ax
    xor dx, dx
    mov di, 7
    mov cx, 2
    call bl_dec
    cmp byte [bl_lscr+7], ' '
    jne .decimal
    mov byte [bl_lscr+7], '0'
.decimal:
    mov byte [bl_lscr+9], 0
    mov di, bl_lscr
    mov si, [ru_clocklabel]
    mov cx, [ru_split]
    add cx, 8
    mov dx, 54
    cmp byte [ru_small], 0
    je .clockpos
    mov dx, 50
.clockpos:
    mov ax, [ru_clockvalue]
    or ax, [ru_clockvalue+2]
    jne .clock
    mov di, ru_na
.clock:
    ; ru_kvs clears bl_lscr; keep the clock text in a separate small scratch.
    push si
    mov si, di
    mov di, ru_value
    mov cx, 10
    rep movsb
    pop si
    mov di, ru_value
    mov cx, [ru_split]
    add cx, 8
    call ru_kvs
    cmp byte [ru_small], 0
    je .full
    add dx, 8
    mov si, ru_bioskb
    mov ax, [rl_convkb]
    call ru_kvn
    ret
.full:
    add dx, 10
    mov si, ru_bioskb
    mov ax, [rl_convkb]
    call ru_kvn
    add dx, 10
    mov si, ru_freekb
    mov ax, [rl_free]
    call ru_kvn
    add dx, 10
    mov si, ru_extkb
    cmp byte [rl_mapok], 0
    jne .usable
    cmp byte [rl_extok], 0
    je .unknown
    mov ax, [rl_extkb]
    call ru_kvn
    jmp .video
.usable:
    mov si, ru_usablekb
    mov ax, [rl_ramkb]
    ; A huge E820 figure keeps its complete 32-bit value in Detailed.
    cmp word [rl_ramkb+2], 0
    je .number
    mov di, ru_seedetailed
    call ru_kvs
    jmp .video
.number:
    call ru_kvn
    jmp .video
.unknown:
    mov di, ru_na
    call ru_kvs
.video:
    add dx, 10
    mov si, ru_adapter
    mov al, [rl_video]
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov di, [ru_adapters+bx]
    call ru_kvs
    add dx, 10
    mov si, ru_screen
    mov ax, [rl_vw]
    call ru_kvn
    add dx, 10
    mov si, ru_screenh
    mov ax, [rl_vh]
    call ru_kvn
    ret

ru_hardware:
    push ds
    pop es
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
    jne .facts
    ret
.facts:
    add dx, [ru_offset]
    cmp byte [ru_small], 0
    je .date
    mov si, ru_adapter
    mov al, [rl_video]
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov di, [ru_adapters+bx]
    call ru_kvs
    ret
.date:
    mov si, ru_bios
    mov di, rl_biosdate
    call ru_kvs
    cmp byte [ru_small], 0
    je .full
    ret
.full:
    add dx, 12
    mov si, ru_model
    push es
    mov ax, 0xF000
    mov es, ax
    xor ax, ax
    mov al, [es:0xFFFE]
    pop es
    call ru_kvn
    mov word [ru_port], 0
.port:
    add dx, 12
    mov ax, dx
    add ax, 9
    cmp ax, [ru_foot]
    jae .out
    mov bx, [ru_port]
    mov si, [ru_portlabels+bx]
    push es
    mov ax, 0x40
    mov es, ax
    mov ax, [es:bx]
    pop es
    call ru_kvn
    add word [ru_port], 2
    cmp word [ru_port], 14
    jb .port
    add dx, 12
    mov si, ru_bpp
    mov al, [rl_video+1]
    xor ah, ah
    call ru_kvn
    add dx, 12
    mov si, ru_xfreekb
    mov ax, [rl_xfree]
    call ru_kvn
    add dx, 12
    mov ax, dx
    add ax, 9
    cmp ax, [ru_foot]
    jae .out
    mov si, ru_largestkb
    mov ax, [rl_largest]
    call ru_kvn
.out:
    ret

ru_results:
    mov ax, 8
    mov bx, [ru_lower]
    mov cx, [ru_split]
    sub cx, 8
    mov dx, [ru_foot]
    sub dx, 4
    cmp byte [ru_view], 2
    jne .box
    mov bx, 24
    mov cx, [ru_w]
    sub cx, 8
.box:
    mov [ru_resw], cx
    mov [ru_restop], bx
    call ru_frame
    mov cx, 16
    mov dx, [ru_restop]
    add dx, 6
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
    mov ax, [ru_page]
    mov [ru_row], ax
    mov ax, [ru_foot]
    sub ax, [ru_restop]
    sub ax, [ru_offset]
    sub ax, 4
    xor dx, dx
    div word [ru_pitch]
    mov [ru_visrows], ax
    mov ax, [ru_restop]
    add ax, [ru_offset]
    mov [ru_resulty], ax
.row:
    cmp word [ru_row], 12
    jae .out
    cmp word [ru_visrows], 0
    je .out
    mov bx, [ru_row]
    shl bx, 1
    mov si, [ru_labels+bx]
    mov cx, 16
    mov dx, [ru_resulty]
    call ru_text
    cmp word [ru_row], 6
    jb .value
    cmp byte [rl_video], VID_CGA
    je .value
    mov si, ru_sameadapter
    mov cx, 160
    call ru_text
    jmp .next
.value:
    mov bx, [ru_row]
    shl bx, 1
    shl bx, 1
    mov ax, [ru_scores+bx]
    mov dx, [ru_scores+bx+2]
    call ru_ratio
    push ax
    push dx
    mov cx, [ru_resw]
    sub cx, 56
    mov dx, [ru_resulty]
    call ru_text
    pop dx
    pop ax
    mov cx, 152
    mov bx, [ru_resulty]
    mov di, [ru_resw]
    sub di, 216
    mov bp, 8
    call ru_bar
.next:
    mov ax, [ru_pitch]
    add [ru_resulty], ax
    inc word [ru_row]
    dec word [ru_visrows]
    jmp .row
.out:
    ret

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
    cmp ah, 0x51
    je .down
    cmp al, ' '
    je .down
    cmp ah, 0x49
    je .up
    cmp ah, 0x4F
    je .end
    cmp ah, 0x47
    jne .out
    mov word [ru_page], 0
    jmp .draw
.down:
    mov ax, [ru_page]
    add ax, [ru_rows]
    cmp ax, 12
    jae .out
    mov [ru_page], ax
    jmp .draw
.up:
    mov ax, [ru_page]
    sub ax, [ru_rows]
    jnc .set
    xor ax, ax
.set:
    mov [ru_page], ax
    jmp .draw
.end:
    mov ax, 12
    sub ax, [ru_rows]
    mov [ru_page], ax
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
    jmp os88ui_btnclick             ; SDK owns every press after layout refresh
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

ru_view: db 0                    ; 0 Summary, 1 Detailed, 2 Compare
ru_small: db 0
ru_valid: db 0
ru_bits: db 0
ru_page: dw 0
ru_row: dw 0
ru_x: dw 0
ru_y: dw 0
ru_w: dw 0
ru_h: dw 0
ru_split: dw 0
ru_lower: dw 0
ru_foot: dw 0
ru_rows: dw 0
ru_pitch: dw 0
ru_offset: dw 0
ru_port: dw 0
ru_resw: dw 0
ru_restop: dw 0
ru_resulty: dw 0
ru_visrows: dw 0
ru_barx: dw 0
ru_bary: dw 0
ru_barw: dw 0
ru_fillw: dw 0
ru_bigx: dw 0
ru_bigy: dw 0
ru_gx: dw 0
ru_gy: dw 0
ru_value: times 16 db 0
ru_sum: times 3 dw 0
ru_overall: dd 0
ru_clockvalue: dd 0
ru_clocklabel: dw ru_mhzest
ru_scores: times 12 dd 0
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
ru_ticklabels: dw ru_tick0, ru_tick1, ru_tick2, ru_tick3, ru_tick4
ru_tick0: db '0', 0
ru_tick1: db '1x', 0
ru_tick2: db '2x', 0
ru_tick3: db '3x', 0
ru_tick4: db '4x', 0
ru_system: db 'System Snapshot', 0
ru_details: db 'Details', 0
ru_resulttitle: db 'Benchmark Results (4.77 MHz PC)', 0
ru_ready: db 'Press R / Run to benchmark', 0
ru_unresolved: db 'Timing below resolution', 0
ru_query: db 'Run to query the system', 0
ru_na: db 'n/a', 0
ru_overflow: db '>99.9x', 0
ru_seedetailed: db 'See Detailed', 0
ru_keys: db 'PgUp/PgDn  F1 Help', 0
ru_saved: db 'REDLINE.TXT saved  ', 0
ru_sameadapter: db 'same adapter only', 0
ru_cpu: db 'CPU', 0
ru_mhzest: db 'MHz est', 0
ru_mhznom: db 'MHz nom', 0
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
