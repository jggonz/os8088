; Download Manager painting code; assembled as the Browser lazy part.
; DS remains the primary Browser segment; the imported symbols are its
; BSS and resident strings/assets, while these routines execute via CS.

br_dl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [br_dl_x], ax
    mov [br_dl_y], dx
    mov bx, si
    call OSAPI_WM_GEOM
    mov [br_dl_w], cx
    mov [br_dl_h], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_x]
    mov bx, [br_dl_y]
    mov cx, ax
    add cx, [br_dl_w]
    dec cx
    mov dx, bx
    add dx, [br_dl_h]
    dec dx
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call br_dl_icons
    call br_dl_progress
    call br_dl_details
    call br_dl_stats
    mov bx, [br_dl_win]
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The supplied BMPs draw at native resolution. The compact profile changes
; only their positions and path length so the 48px towers still fit.
br_dl_icons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ax, 1
    cmp byte [br_dl_layout], 0
    jne .scale_set
    mov ax, 2
.scale_set:
    mov [br_dl_scale], ax
    ; Draw the supplied server BMP at its native 48x48 size.
    call br_dl_groupx
    mov ax, 8
    add ax, [br_dl_gx]
    add ax, 4                       ; snap to the 8-pixel grid: kern_small's
    and ax, 0FFF8h                  ; BLIT1 refuses an off-grid origin
    mov dx, [br_dl_yicon]
    cmp word [br_dl_scale], 1
    jne .servery
    add dx, 7
.servery:
    add dx, [br_dl_y]
    mov [br_dl_ix], ax
    mov [br_dl_iy], dx
    mov ax, [br_dl_ix]
    mov bx, [br_dl_iy]
    mov cx, 48
    mov dx, 48
    mov bp, 6
    mov si, br_dl_icon_server
    call br_dl_blit1
    ; Draw the supplied PC BMP at its native 48x48 size.
    call br_dl_groupx
    mov ax, 301
    cmp word [br_dl_scale], 1
    je .pcx
    mov ax, 230
.pcx:
    add ax, [br_dl_gx]
    add ax, 4                       ; same 8-pixel snap as the server icon
    and ax, 0FFF8h
    mov [br_dl_pc], ax
    mov ax, [br_dl_yicon]
    cmp word [br_dl_scale], 1
    jne .pcgy
    add ax, 7
.pcgy:
    add ax, [br_dl_y]
    mov [br_dl_gy], ax
    mov ax, [br_dl_pc]
    mov bx, [br_dl_gy]
    mov cx, 48
    mov dx, 48
    mov bp, 6
    mov si, br_dl_icon_pc
    call br_dl_blit1
    ; Fixed dashed 6-on / 5-off path and arrowhead.
    call br_dl_groupx
    mov ax, [br_dl_y]
    mov [br_dl_gy], ax
    call br_dl_drawpath
    call br_dl_setdocpos
    call br_dl_drawdoc
    ; Captions track their icons in both compact and full-size layouts.
    mov cx, 8
    add cx, [br_dl_gx]
    mov dx, [br_dl_y]
    add dx, [br_dl_ycaption]
    mov si, br_dl_s_server
    call OSAPI_FONT_STR_XPARENT
    mov cx, 301
    cmp word [br_dl_scale], 1
    je .pc_label
    mov cx, 230
.pc_label:
    add cx, [br_dl_gx]
    mov dx, [br_dl_y]
    add dx, [br_dl_ycaption]
    mov si, br_dl_s_pc
    call OSAPI_FONT_STR_XPARENT
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Center either the 360px reference area or the compact 280px arrangement.
br_dl_groupx:
    push ax
    push bx
    mov ax, [br_dl_w]
    mov bx, 360
    cmp word [br_dl_scale], 1
    je .width
    mov bx, 280
.width:
    sub ax, bx
    shr ax, 1
    sub ax, 2                     ; the icon bounds are slightly asymmetric
    add ax, [br_dl_x]
    mov [br_dl_gx], ax
    pop bx
    pop ax
    ret

; BMP tables preserve their source polarity: set bits are white and clear
; bits black. Color adapters use white ink/black paper; 1bpp adapters ignore
; this pen and light set bits directly, so the same tables work on both.
br_dl_blit1:
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    pop es
    ret

; Relative line helper uses [br_dl_gx],[br_dl_gy] as its origin.
br_dl_hrel:
    push ax
    push bx
    push dx
    push si
    add ax, [br_dl_gx]
    add bx, [br_dl_gx]
    add dx, [br_dl_gy]
    call OSAPI_GFX_HLINE
    pop si
    pop dx
    pop bx
    pop ax
    ret

; SI -> y,x1,x2 word triples, terminated by FFFFh (used by the far arrow tip).
br_dl_drawhrowsw:
    push ax
    push bx
    push cx
    push dx
    push si
.loop:
    ; These tables live beside the painter in its lazy code segment. DS must
    ; remain the Browser's primary segment for BSS and strings.
    mov ax, [cs:si]
    add si, 2
    cmp ax, 0FFFFh
    je .out
    mov [br_dl_curvey], ax
    mov ax, [cs:si]
    add si, 2
    mov [br_dl_curvex1], ax
    mov ax, [cs:si]
    add si, 2
    mov [br_dl_curvex2], ax
    mov ax, [br_dl_curvex1]
    mov bx, [br_dl_curvex2]
    mov dx, [br_dl_curvey]
    call br_dl_hrel
    jmp .loop
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Draw a six-pixel dash every eleven pixels and a solid triangular arrowhead.
br_dl_drawpath:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, 65
    cmp word [br_dl_scale], 1
    je .dash
    mov ax, 56
.dash:
    cmp ax, [br_dl_ypath]
    jae .arrow
    mov dx, 43
    cmp word [br_dl_scale], 1
    je .liney
    mov dx, 24
.liney:
    mov bx, ax
    add bx, 5
    call br_dl_hrel
    add ax, 11
    jmp .dash
.arrow:
    mov si, br_dl_arrow_rows
    cmp word [br_dl_scale], 1
    je .drawarrow
    mov si, br_dl_arrow_rows_compact
.drawarrow:
    call br_dl_drawhrowsw
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The frame table selects eight fixed positions from the original transfer path.
br_dl_setdocpos:
    push ax
    push bx
    push cx
    mov al, [br_dl_anim]
    and al, 7
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, br_dl_doc_xframes
    cmp word [br_dl_scale], 1
    je .frame_table
    mov si, br_dl_doc_xframes_compact
.frame_table:
    mov ax, [cs:si+bx]
    add ax, [br_dl_gx]
    ; BLIT1's byte-grid head is exact, but the small-kernel fallback refuses
    ; an off-grid origin. The animation is deliberately snapped to the same
    ; 8-pixel grid so this one bitmap works on every display/kernel build.
    add ax, 4
    and ax, 0FFF8h
    mov [br_dl_mx], ax
    mov ax, 17
    cmp word [br_dl_scale], 1
    je .docy
    mov ax, 10
.docy:
    add ax, [br_dl_y]
    mov [br_dl_my], ax
    mov ax, 24
    mov bx, 28
    mov [br_dl_filew], ax
    mov [br_dl_fileh], bx
    pop cx
    pop bx
    pop ax
    ret

; Draw the one supplied folded-sheet bitmap at any of the eight positions.
br_dl_drawdoc:
    push bp
    mov ax, [br_dl_mx]
    mov bx, [br_dl_my]
    mov cx, 24
    mov dx, 28
    mov bp, 3
    mov si, br_dl_icon_document
    call br_dl_blit1
    pop bp
    ret

; Restore old document footprint, then redraw only the moving document and
; the fixed path beneath it. WM's active clip handles obscuring and occlusion.
br_dl_redraw_doc:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_mx]
    mov bx, [br_dl_my]
    mov cx, ax
    add cx, [br_dl_filew]
    dec cx
    mov dx, bx
    add dx, [br_dl_fileh]
    dec dx
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call br_dl_groupx
    mov ax, [br_dl_y]
    mov [br_dl_gy], ax
    call br_dl_restorepath
    call br_dl_setdocpos
    call br_dl_drawdoc
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Restore only the dashed line under the old opaque document rectangle.
; The arrow and all line sections outside that 24x28 footprint stay untouched.
br_dl_restorepath:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [br_dl_mx]
    sub ax, [br_dl_gx]
    mov [br_dl_curvex1], ax
    add ax, [br_dl_filew]
    dec ax
    mov [br_dl_curvex2], ax
    mov ax, 65
    cmp word [br_dl_scale], 1
    je .startdash
    mov ax, 56
.startdash:
    mov [br_dl_curvey], ax        ; current dash's local x
.dash:
    mov ax, [br_dl_curvey]
    cmp ax, [br_dl_ypath]
    jae .out
    mov bx, ax
    add bx, 5
    cmp ax, [br_dl_curvex1]
    jae .leftdone
    mov ax, [br_dl_curvex1]
.leftdone:
    cmp bx, [br_dl_curvex2]
    jbe .rightdone
    mov bx, [br_dl_curvex2]
.rightdone:
    cmp ax, bx
    ja .next
    add ax, [br_dl_gx]
    add bx, [br_dl_gx]
    mov dx, 43
    cmp word [br_dl_scale], 1
    je .liney
    mov dx, 24
.liney:
    add dx, [br_dl_gy]
    call OSAPI_GFX_HLINE
.next:
    mov ax, [br_dl_curvey]
    add ax, 11
    mov [br_dl_curvey], ax
    jmp .dash
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The arrow remains line geometry; the three pictured assets are generated
; from the supplied BMPs into compact static BLIT1 bands during the build.
br_dl_arrow_rows:
    dw 38,285,285, 39,286,287, 40,288,289, 41,290,291, 42,292,293
    dw 43,294,294, 44,292,293, 45,290,291, 46,288,289, 47,286,287
    dw 48,285,285, 0FFFFh
br_dl_arrow_rows_compact:
    dw 19,213,213, 20,214,215, 21,216,217, 22,218,219
    dw 23,220,221, 24,222,222, 25,220,221, 26,218,219
    dw 27,216,217, 28,214,215, 29,213,213, 0FFFFh
br_dl_doc_xframes:
    dw 67,94,121,148,175,202,229,256
br_dl_doc_xframes_compact:
    dw 56,75,94,113,132,151,170,189


br_dl_progress:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [br_dl_x]
    ; Keep the track visibly inset from the window's left frame. On compact
    ; outer geometries the content origin is close to that frame, so the old
    ; 12-pixel inset let the black fill read as if it started at the border.
    add ax, 24
    mov [br_dl_barx], ax
    mov bx, [br_dl_y]
    add bx, [br_dl_yprogress]
    mov [br_dl_bary], bx
    mov cx, [br_dl_x]
    add cx, [br_dl_w]
    ; Reserve only the label's real width at the right. The old 82-pixel
    ; inset left a conspicuous blank tail after the percent value.
    sub cx, 48
    mov [br_dl_barx2], cx
    mov dx, bx
    add dx, [br_dl_barh]
    call OSAPI_GFX_FRAME
    mov ax, [br_dl_barx]
    inc ax
    mov bx, [br_dl_bary]
    inc bx
    mov cx, [br_dl_barx2]
    dec cx
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_barx]
    inc ax
    call OSAPI_GFX_FILL
    cmp byte [br_dl_have_len], 0
    je .indeterminate
    call br_dl_percent
    mov [br_dl_pct], ax
    mov ax, [br_dl_barx2]
    sub ax, [br_dl_barx]
    sub ax, 2
    mov bx, [br_dl_pct]
    mul bx
    mov bx, 100
    div bx
    mov [br_dl_barfill], ax
    or ax, ax
    jz .label
    mov cx, [br_dl_barx]
    add cx, ax                    ; right edge = left inset + fill width - 1
    push cx
    mov ax, [br_dl_barx]
    inc ax
    mov bx, [br_dl_bary]
    inc bx
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop cx
    mov ax, [br_dl_barx]
    inc ax
    call OSAPI_GFX_FILL
    jmp .label
.indeterminate:
    mov word [br_dl_barfill], 0
    ; A short dither segment communicates activity without implying a total.
    mov ax, [br_dl_barx]
    add ax, 8
    mov cx, ax
    add cx, 40
    mov bx, [br_dl_bary]
    inc bx
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    push cx
    mov ax, [br_dl_barx]
    add ax, 8
    call OSAPI_GFX_FILL_GRAY
    pop cx
.label:
    ; The first paint may have shown '--' while HTTP headers were pending.
    ; Clear this bounded label cell before writing a known percentage so the
    ; shorter text cannot leave the old dashes or percent sign behind.
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_barx2]
    inc ax
    mov bx, [br_dl_bary]
    inc bx
    mov cx, [br_dl_barx2]
    add cx, 32
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [br_dl_barx2]
    add cx, 8
    mov dx, [br_dl_bary]
    mov ax, [br_dl_barh]
    sub ax, 8
    shr ax, 1
    add dx, ax
    cmp byte [br_dl_have_len], 0
    je .unknown
    mov ax, [br_dl_pct]
    call br_dl_percent_label
    jmp .out
.unknown:
    mov si, br_dl_s_no
    mov cx, [br_dl_barx2]
    add cx, 8
    mov dx, [br_dl_bary]
    mov bx, [br_dl_barh]
    sub bx, 8
    shr bx, 1
    add dx, bx
    call OSAPI_FONT_STR_XPARENT
.out:
    mov al, [br_dl_have_len]
    mov [br_dl_barstate], al
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The bar frame and white interior are drawn by br_dl_progress on a real
; repaint. During transfer, only add newly earned black pixels; never clear
; and repaint the whole bar on each timer tick.
br_dl_progress_live:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [br_dl_have_len], 0
    jne .known
    cmp byte [br_dl_barstate], 0
    je .out
    ; A redirect or changed response can return to unknown length. Rebuild the
    ; bar once to remove the old percentage/fill, then keep it stable.
    call br_dl_progress
    jmp .out
.known:
    cmp byte [br_dl_barstate], 0
    jne .incremental
    ; Headers arrive after the window's first paint. Replace the indeterminate
    ; dither with a real, clean bar exactly once when Content-Length appears.
    call br_dl_progress
    jmp .out
.incremental:
    call br_dl_percent
    mov [br_dl_pct], ax
    mov ax, [br_dl_barx2]
    sub ax, [br_dl_barx]
    sub ax, 2
    mov bx, [br_dl_pct]
    mul bx
    mov bx, 100
    div bx
    mov [br_dl_barnew], ax
    cmp ax, [br_dl_barfill]
    jbe .label
    ; SET_COLOR takes AL, so select the ink before loading the rectangle.
    ; Doing it after AX held x1 replaced its low byte with CBLACK and painted
    ; the incremental strip at an unrelated screen x (left of the track).
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [br_dl_barx]
    add ax, [br_dl_barfill]
    inc ax
    mov bx, [br_dl_bary]
    inc bx
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    mov cx, [br_dl_barx]
    add cx, [br_dl_barnew]
    call OSAPI_GFX_FILL
    mov ax, [br_dl_barnew]
    mov [br_dl_barfill], ax
.label:
    mov cx, [br_dl_barx2]
    add cx, 8
    mov dx, [br_dl_bary]
    mov ax, [br_dl_barh]
    sub ax, 8
    shr ax, 1
    add dx, ax
    mov ax, [br_dl_pct]
    call br_dl_percent_label
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_details:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [br_dl_y]
    add ax, [br_dl_ydetail]
    mov [br_dl_rowy], ax
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_rowy]
    mov si, br_dl_s_file
    call OSAPI_FONT_STR_XPARENT
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_rowy]
    mov si, br_dl_name
    call br_dl_drawcut

    mov ax, [br_dl_rowy]
    add ax, [br_dl_detailstep]
    mov [br_dl_rowy], ax
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_rowy]
    mov si, br_dl_s_source
    call OSAPI_FONT_STR_XPARENT
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_rowy]
    mov si, br_dl_url
    call br_dl_drawcut

    mov ax, [br_dl_rowy]
    add ax, [br_dl_detailstep]
    mov [br_dl_rowy], ax
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_rowy]
    mov si, br_dl_s_dest
    call OSAPI_FONT_STR_XPARENT
    mov cx, [br_dl_x]
    add cx, 116
    mov dx, [br_dl_rowy]
    mov si, br_dl_dest
    call br_dl_drawcut

    mov ax, [br_dl_rowy]
    add ax, [br_dl_detailstep]
    mov [br_dl_rowy], ax
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_rowy]
    mov si, br_dl_s_status
    call OSAPI_FONT_STR_XPARENT
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_rowy]
    mov si, [br_dl_msg]
    or si, si
    jnz .statustext
    mov si, br_dl_s_connect
.statustext:
    call br_dl_drawcut
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_stats:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [br_dl_liveflag], 0
    jne .bytesvalue
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_y]
    add dx, [br_dl_ystats]
    mov si, br_dl_s_bytes
    call OSAPI_FONT_STR_XPARENT
.bytesvalue:
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_y]
    add dx, [br_dl_ystats]
    mov ax, [br_dl_recvbytes]
    mov dx, [br_dl_recvbytes+2]
    call br_dl_fmt_u32
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_y]
    add dx, [br_dl_ystats]
    mov si, br_dl_numtxt
    mov byte [br_dl_livewidth], 12
    call br_dl_drawnum
    cmp byte [br_dl_have_len], 0
    je .unknown
    cmp byte [br_dl_liveflag], 0
    jne .unknown
    mov cx, [br_dl_x]
    add cx, 180
    mov dx, [br_dl_y]
    add dx, [br_dl_ystats]
    mov al, '/'
    call OSAPI_FONT_CHAR_XPARENT
    mov ax, [br_dl_total]
    mov dx, [br_dl_total+2]
    call br_dl_fmt_u32
    mov cx, [br_dl_x]
    add cx, 192
    mov dx, [br_dl_y]
    add dx, [br_dl_ystats]
    mov si, br_dl_numtxt
    mov byte [br_dl_livewidth], 12
    call br_dl_drawnum
.unknown:
    cmp byte [br_dl_liveflag], 0
    jne .speedvalue
    mov cx, [br_dl_x]
    add cx, 12
    mov dx, [br_dl_y]
    add dx, [br_dl_yspeed]
    mov si, br_dl_s_speed
    call OSAPI_FONT_STR_XPARENT
.speedvalue:
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_y]
    add dx, [br_dl_yspeed]
    mov ax, [br_dl_speed]
    or ax, ax
    jz .calc
    call br_dl_fmt_word
    push es
    push ds
    pop es
    mov si, br_dl_numtxt
    mov di, br_dl_disptxt
.speedcopy:
    lodsb
    stosb
    or al, al
    jnz .speedcopy
    dec di
    mov si, br_dl_s_speedunit
.speedappend:
    lodsb
    stosb
    or al, al
    jnz .speedappend
    pop es
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_y]
    add dx, [br_dl_yspeed]
    mov si, br_dl_disptxt
    mov byte [br_dl_livewidth], 8
    call br_dl_drawnum
    jmp .eta
.calc:
    cmp byte [br_dl_state], DL_DONE
    jae .nospeed
    mov si, br_dl_s_calc
    mov byte [br_dl_livewidth], 8
    call br_dl_drawrun
    jmp .eta
.nospeed:
    mov si, br_dl_s_no
    cmp byte [br_dl_liveflag], 0
    jne .nospeed_live
    call OSAPI_FONT_STR_XPARENT
    jmp .eta
.nospeed_live:
    mov byte [br_dl_livewidth], 8
    call br_dl_drawrun
.eta:
    cmp byte [br_dl_liveflag], 0
    jne .etavaluepos
    mov cx, [br_dl_x]
    add cx, 160
    mov dx, [br_dl_y]
    add dx, [br_dl_yspeed]
    mov si, br_dl_s_eta
    call OSAPI_FONT_STR_XPARENT
.etavaluepos:
    mov cx, [br_dl_x]
    add cx, 200
    mov dx, [br_dl_y]
    add dx, [br_dl_yspeed]
    call br_dl_setetawidth
    call br_dl_eta
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_eta:
    mov [br_dl_etax], cx
    mov [br_dl_etay], dx
    cmp byte [br_dl_have_len], 0
    je .none
    mov ax, [br_dl_speed]
    or ax, ax
    jz .calc
    mov ax, [br_dl_total]
    mov dx, [br_dl_total+2]
    sub ax, [br_dl_recvbytes]
    sbb dx, [br_dl_recvbytes+2]
    jc .none
    mov cx, 10
.shift:
    shr dx, 1
    rcr ax, 1
    loop .shift
    or dx, dx
    jnz .large
    xor dx, dx
    div word [br_dl_speed]
    call br_dl_fmt_word
    push es
    push ds
    pop es
    mov si, br_dl_numtxt
    mov di, br_dl_disptxt
.etacopy:
    lodsb
    stosb
    or al, al
    jnz .etacopy
    dec di
    mov si, br_dl_s_etaunit
.etaappend:
    lodsb
    stosb
    or al, al
    jnz .etaappend
    pop es
    mov cx, [br_dl_etax]
    mov dx, [br_dl_etay]
    mov si, br_dl_disptxt
    mov byte [br_dl_livewidth], 10
    call br_dl_drawnum
    jmp .out
.large:
.none:
    mov cx, [br_dl_etax]
    mov dx, [br_dl_etay]
    mov si, br_dl_s_no
    cmp byte [br_dl_liveflag], 0
    jne .none_live
    call OSAPI_FONT_STR_XPARENT
    jmp .out
.none_live:
    mov byte [br_dl_livewidth], 10
    call br_dl_drawrun
    jmp .out
.calc:
    mov cx, [br_dl_etax]
    mov dx, [br_dl_etay]
    mov si, br_dl_s_calc
    cmp byte [br_dl_liveflag], 0
    jne .calc_live
    call br_dl_drawrun
    jmp .out
.calc_live:
    call br_dl_drawrun
 .out:
    ret

; The compact label/value pair must stay clear of the right-aligned button.
; At the narrowest supported window the value gets seven cells; wider windows
; retain ten. The largest real ETA string is seven characters ("999 sec").
br_dl_setetawidth:
    push ax
    push cx                         ; CL is the shift count; preserve the X coordinate
    mov ax, [br_dl_w]
    sub ax, 264                    ; value starts at x+200, reserve 64px button
    mov cl, 3
    shr ax, cl
    cmp ax, 10
    jbe .cap
    mov ax, 10
.cap:
    cmp ax, 7
    jae .store
    mov ax, 7
.store:
    mov [br_dl_livewidth], al
    pop cx
    pop ax
    ret

br_dl_drawnum:
    cmp byte [br_dl_liveflag], 0
    jne br_dl_drawrun
    push es
    push ds
    pop es
    call OSAPI_FONT_STR_XPARENT
    pop es
    ret

; Replace a dynamic fixed-width field in one opaque FONT_RUN. Padding is part
; of the run, so shorter values erase old trailing glyphs without a blank
; flash. BX is kept for the caller; the field width is a small static bound.
br_dl_drawrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    push cx
    push dx
    mov di, br_dl_runbuf
    mov bl, [br_dl_livewidth]
.copy:
    or bl, bl
    jz .term
    lodsb
    or al, al
    jz .pad
    stosb
    dec bl
    jmp short .copy
.pad:
    mov al, ' '
.spaces:
    or bl, bl
    jz .term
    stosb
    dec bl
    jmp short .spaces
.term:
    xor al, al
    stosb
    pop dx
    pop cx
    mov si, br_dl_runbuf
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Redraw only the small percentage cell, including its paper, so transitions
; from '--' and changes in digit count cannot leave stale glyphs behind.
br_dl_percent_label:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_barx2]
    inc ax
    mov bx, [br_dl_bary]
    inc bx
    mov cx, [br_dl_barx2]
    add cx, 32
    mov dx, [br_dl_bary]
    add dx, [br_dl_barh]
    dec dx
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [br_dl_barx2]
    add cx, 8
    mov dx, [br_dl_bary]
    mov bx, [br_dl_barh]
    sub bx, 8
    shr bx, 1
    add dx, bx
    mov ax, [br_dl_pct]
    call br_dl_fmt_word
    mov di, br_dl_numtxt
.percent_scan:
    cmp byte [di], 0
    je .percent
    inc di
    jmp short .percent_scan
.percent:
    mov al, '%'
    mov [di], al
    inc di
    mov byte [di], 0
    mov cx, [br_dl_barx2]
    add cx, 8
    mov dx, [br_dl_bary]
    mov bx, [br_dl_barh]
    sub bx, 8
    shr bx, 1
    add dx, bx
    mov si, br_dl_numtxt
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_drawcut:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, br_dl_disptxt
    mov bx, [br_dl_w]
    sub bx, 130
    shr bx, 1
    shr bx, 1
    shr bx, 1
    cmp bx, 58
    jbe .copy
    mov bx, 58
.copy:
    lodsb
    or al, al
    jz .term
    stosb
    dec bx
    jnz .copy
    cmp byte [si], 0
    je .term
    sub di, 3
    mov byte [di], '.'
    mov byte [di+1], '.'
    mov byte [di+2], '.'
    add di, 3
.term:
    xor al, al
    stosb
    mov si, br_dl_disptxt
    call OSAPI_FONT_STR_XPARENT
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Format a 16-bit unsigned value as decimal in br_dl_numtxt.
br_dl_fmt_word:
; Division yields least-significant digits first, so place each remainder
; backward from the terminator. The old stack/pop loop wrote the most-
; significant digit at the end, reversing values such as 100 into 001.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, br_dl_numtxt+5
    mov byte [di], 0
    mov bx, 10
.digits:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    or ax, ax
    jnz .digits
    mov si, di
    mov di, br_dl_numtxt
.copy:
    lodsb
    stosb
    or al, al
    jnz .copy
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Format DX:AX as decimal, using 32-bit divide by ten in two 16-bit steps.
br_dl_fmt_u32:
; STKBALANCE-OK: quotient remainders are pushed once per digit and the emit
; loop pops exactly CX digits before restoring the saved registers.
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov [br_dl_divlo], ax
    mov [br_dl_divhi], dx
    mov bx, 10
    xor cx, cx
.divide:
    mov ax, [br_dl_divhi]
    xor dx, dx
    div bx
    mov [br_dl_divhi], ax
    mov ax, [br_dl_divlo]
    div bx
    mov [br_dl_divlo], ax
    push dx
    inc cx
    mov ax, [br_dl_divhi]
    or ax, [br_dl_divlo]
    jnz .divide
    mov di, br_dl_numtxt
.emit:
    pop ax
    add al, '0'
    stosb
    loop .emit
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_dl_percent:
    ; Read one coherent snapshot. The download worker can be preempted between
    ; ADD and ADC when the low word wraps; reject that transient combination
    ; so the text never jumps backwards while the monotonic bar stays filled.
    push bx
    push cx
    push dx
    mov bl, [br_dl_recvseq]
    test bl, 1
    jnz .unstable
    mov ax, [br_dl_recvbytes]
    mov dx, [br_dl_recvbytes+2]
    cmp bl, [br_dl_recvseq]
    jne .unstable
    ; Scale both 32-bit counts together until total fits in one word.
    mov bx, [br_dl_total]
    mov cx, [br_dl_total+2]
.scale:
    or cx, cx
    jz .div
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp .scale
.div:
    or bx, bx
    jnz .mul
    mov bx, 1
.mul:
    mov cx, 100
    mul cx
    ; DX:AX / BX, with DX < BX because the numerator never exceeds total*100.
    div bx
    jmp short .done
.unstable:
    ; Keep the last confirmed display value until the next paint gets a
    ; coherent counter snapshot. This path is only one timer tick at rollover.
    mov ax, [br_dl_pct]
.done:
    pop dx
    pop cx
    pop bx
    ret


br_dl_paint_live:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; The frame and static labels do not change. Keep the fill monotonic and
    ; replace only the live numeric fields with opaque, padded text runs.
    call br_dl_progress_live
    call br_dl_paint_live_status
    mov byte [br_dl_liveflag], 1
    call br_dl_stats
    mov byte [br_dl_liveflag], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Status is static for most timer passes. Replace its value only when the
; worker changes the message, instead of erasing the whole row repeatedly.
br_dl_paint_live_status:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [br_dl_msg]
    or si, si
    jnz .message
    mov si, br_dl_s_connect
.message:
    cmp si, [br_dl_lastmsg]
    je .out
    mov [br_dl_lastmsg], si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [br_dl_x]
    add ax, 80
    mov bx, [br_dl_y]
    add bx, [br_dl_ydetail]
    mov cx, [br_dl_detailstep]
    shl cx, 1
    add bx, cx
    add bx, [br_dl_detailstep]
    dec bx
    mov cx, [br_dl_x]
    add cx, [br_dl_w]
    sub cx, 8
    mov dx, bx
    add dx, 10
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [br_dl_x]
    add cx, 80
    mov dx, [br_dl_y]
    add dx, [br_dl_ydetail]
    mov ax, [br_dl_detailstep]
    mov bx, ax
    shl ax, 1
    add ax, bx
    add dx, ax
    mov si, [br_dl_lastmsg]
    call br_dl_drawcut
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret


br_dl_part_paint:
    call br_dl_paint
    retf
br_dl_part_redraw_doc:
    call br_dl_redraw_doc
    retf
br_dl_part_paint_live:
    call br_dl_paint_live
    retf
