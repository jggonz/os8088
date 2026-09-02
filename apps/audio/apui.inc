; =============================================================================
; os8088 - apps/audio/apui.inc
;
; The Audio Player's window: layout computed from the LIVE content geometry
; every paint (SPEC.md 39 - three adapters, one binary), drawing kept
; deliberately plain (SPEC.md 86.7: no visualiser, no animation). Prefix apu_.
;
;   filename line
;   status line
;   MM:SS  [========------------]  MM:SS
;   [ Prev ][ Play ][Pause][ Stop ][ Next ][ Shuf ][ Rep ]
;   playlist:  > 02  SONG.WAV
; =============================================================================

    APBUF apu_t1, 8                 ; "MM:SS" scratch (elapsed)
    APBUF apu_t2, 8                 ; "MM:SS" scratch (total)
    APW   apu_ox                   ; cached content origin
    APW   apu_oy
apu_l_noname: db '(no file)', 0
apu_l_plhdr:  db 'Playlist:', 0

; =============================================================================
; apu_layout - recompute the button rects and the progress-bar rect from the
;              window's current content size.
; =============================================================================
apu_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [ap_win]
    call OSAPI_WM_GEOM             ; CX = content w, DX = content h, CF=1 hidden
    jc .keep                      ; not laid out yet: keep the last good values
    or cx, cx
    jz .keep
    mov [apu_cw], cx
    mov [apu_ch], dx
    jmp .have
.keep:
    mov cx, [apu_cw]
    or cx, cx
    jnz .have
    mov cx, 292                    ; a sane default until the real geometry lands
    mov [apu_cw], cx
    mov word [apu_ch], 210
.have:

    ; progress bar: x 46 .. w-46, y 31 .. 39
    mov word [apu_pbx1], 46
    mov ax, cx
    sub ax, 46
    mov [apu_pbx2], ax
    mov word [apu_pby1], 31
    mov word [apu_pby2], 39

    ; 7 buttons across x 4 .. w-4 on row y 46..62
    mov ax, cx
    sub ax, 8                      ; usable width
    mov bx, 7
    xor dx, dx
    div bx                         ; AX = per-button pitch
    mov [apu_bpitch], ax
    xor di, di                     ; button index
    xor si, si                     ; running x = 4
    mov si, 4
.brow:
    cmp di, 7
    jae .bdone
    mov bx, di
    mov cl, 3
    shl bx, cl                     ; * 8 bytes per rect
    add bx, apu_rects
    mov [bx+0], si                 ; x1
    mov word [bx+2], 46            ; y1
    mov ax, si
    add ax, [apu_bpitch]
    sub ax, 3                      ; small gap
    mov [bx+4], ax                 ; x2
    mov word [bx+6], 62            ; y2
    add si, [apu_bpitch]
    inc di
    jmp .brow
.bdone:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_origin - AX = content left, DX = content top (screen coords)
; preserves BX, CX, SI, DI
; =============================================================================
apu_origin:
    push ax
    push bx
    push dx
    mov bx, [ap_win]
    call OSAPI_WM_CONTENT          ; out AX = left, DX = top (CF=1 not visible)
    jc .keep
    mov [apu_ox], ax
    mov [apu_oy], dx
.keep:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_time_str - AX = seconds -> "MM:SS" NUL at DI (this segment). MM clamps
;                at 99.  preserves everything.
; =============================================================================
apu_time_str:
    push ax
    push bx
    push dx
    push di
    xor dx, dx
    mov bx, 60
    div bx                         ; AX = minutes, DX = seconds
    cmp ax, 99
    jbe .mok
    mov ax, 99
.mok:
    ; minutes -> two digits
    push dx
    xor dx, dx
    mov bx, 10
    div bx                         ; AX = tens, DX = ones
    add al, '0'
    mov [di], al
    mov ax, dx
    add al, '0'
    mov [di+1], al
    mov byte [di+2], ':'
    pop dx                         ; seconds
    mov ax, dx
    xor dx, dx
    mov bx, 10
    div bx
    add al, '0'
    mov [di+3], al
    mov ax, dx
    add al, '0'
    mov [di+4], al
    mov byte [di+5], 0
    pop di
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; apu_progress -> AX = filled pixels of the progress bar (0 .. bar width)
; =============================================================================
apu_progress:
    push bx
    push cx
    push dx
    mov cx, [apu_pbx2]
    sub cx, [apu_pbx1]            ; CX = bar width in px
    dec cx
    or cx, cx
    jle .zero
    mov ax, [ap_total_pcm]
    mov dx, [ap_total_pcm+2]
    mov bx, ax
    or bx, dx
    jz .zero                      ; unknown length
    ; step = total / barw   (>= 1 for any real track)
    div cx                        ; AX = total / barw  (DX:AX / cx)
    mov bx, ax
    or bx, bx
    jz .full
    mov ax, [ap_played]
    mov dx, [ap_played+2]
    cmp dx, bx
    jae .full
    div bx                        ; AX = played / step  ~= filled px
    cmp ax, cx
    jbe .done
.full:
    mov ax, cx
    jmp .done
.zero:
    xor ax, ax
.done:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; apu_draw - full content repaint (kernel has white-filled the content first)
; =============================================================================
apu_draw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_layout
    call apu_origin

    ; --- filename line -------------------------------------------------
    call apl_cur_name             ; SI = &name, CF = 1 empty
    jnc .havename
    mov si, apu_l_noname
.havename:
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 4
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; --- status line -------------------------------------------------
    mov si, [ap_msg]
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 16
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; --- time + bar -------------------------------------------------
    call apu_draw_time_full

    ; --- transport buttons ---------------------------------------
    call apu_draw_buttons

    ; --- playlist ------------------------------------------------
    call apu_draw_list

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_time_full - the "MM:SS [bar] MM:SS" row, all of it -------------
apu_draw_time_full:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [ap_elapsed_s]
    mov di, apu_t1
    call apu_time_str
    mov ax, [ap_total_s]
    mov di, apu_t2
    call apu_time_str

    mov si, apu_t1
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 31
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    mov si, apu_t2
    mov cx, [apu_ox]
    add cx, [apu_cw]
    sub cx, 40
    mov dx, [apu_oy]
    add dx, 31
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    ; bar frame
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    mov cx, [apu_ox]
    add cx, [apu_pbx2]
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    call OSAPI_GFX_FRAME

    call apu_draw_bar_fill

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_bar_fill - clear the bar interior, then fill the played part ---
apu_draw_bar_fill:
    push ax
    push bx
    push cx
    push dx
    ; interior white
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    inc ax
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    inc bx
    mov cx, [apu_ox]
    add cx, [apu_pbx2]
    dec cx
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    dec dx
    push ax
    push cx
    call apu_penw     ; helper below
    call OSAPI_GFX_FILL
    pop cx
    pop ax
    ; filled part
    call apu_progress             ; AX = filled px
    or ax, ax
    jz .done
    push ax
    mov ax, [apu_ox]
    add ax, [apu_pbx1]
    inc ax                        ; x1
    pop cx
    add cx, ax                    ; x2 = x1 + filled
    mov bx, [apu_oy]
    add bx, [apu_pby1]
    inc bx
    mov dx, [apu_oy]
    add dx, [apu_pby2]
    dec dx
    call apu_penb
    call OSAPI_GFX_FILL
.done:
    call apu_penb
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_penw:
    push ax
    mov al, 15
    call OSAPI_SET_COLOR
    pop ax
    ret
apu_penb:
    push ax
    xor al, al
    call OSAPI_SET_COLOR
    pop ax
    ret

; --- apu_draw_dyn - the cheap update the worker runs each pass --------------
apu_draw_dyn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call apu_origin
    ; erase + redraw the elapsed field (opaque run)
    mov ax, [ap_elapsed_s]
    mov di, apu_t1
    call apu_time_str
    mov si, apu_t1
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 31
    mov al, 0                      ; ink black
    mov ah, 15                     ; paper white
    call OSAPI_FONT_RUN
    call apu_draw_bar_fill
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- apu_draw_buttons -----------------------------------------------------
apu_draw_buttons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di
.b:
    cmp di, 7
    jae .done
    ; rect -> absolute in a local 4-word buffer
    mov bx, di
    mov cl, 3
    shl bx, cl
    add bx, apu_rects
    mov ax, [bx+0]
    add ax, [apu_ox]
    mov [apu_abr+0], ax
    mov ax, [bx+2]
    add ax, [apu_oy]
    mov [apu_abr+2], ax
    mov ax, [bx+4]
    add ax, [apu_ox]
    mov [apu_abr+4], ax
    mov ax, [bx+6]
    add ax, [apu_oy]
    mov [apu_abr+6], ax
    ; label + flags
    mov si, di
    add si, si
    mov si, [apu_blabs + si]
    xor cx, cx                     ; CX = OS88UI_* flags (via DI later)
    ; Shuffle / Repeat show the pressed look when toggled on
    cmp di, AP_B_SHUF
    jne .nsh
    cmp byte [ap_shuffle], 0
    je .nsh
    mov cx, OS88UI_DOWN
.nsh:
    cmp di, AP_B_REP
    jne .ndr
    cmp byte [ap_repeat], 0
    je .ndr
    mov cx, OS88UI_DOWN
.ndr:
    push di
    mov bx, apu_abr
    mov di, cx
    call os88ui_btn                ; BX = rect, SI = label, DI = flags
    pop di
    inc di
    jmp .b
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_blabs:
    dw ap_l_prev, ap_l_play, ap_l_pause, ap_l_stop, ap_l_next, ap_l_shuf, ap_l_rep

; --- apu_draw_list - the playlist, one 8px row per entry -------------------
apu_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, apu_l_plhdr
    mov cx, [apu_ox]
    add cx, 6
    mov dx, [apu_oy]
    add dx, 68
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN

    call apl_count
    mov [apu_ln], ax
    xor di, di                     ; row index
.row:
    cmp di, [apu_ln]
    jae .done
    mov ax, di
    mov bx, 8
    mul bx
    add ax, 78
    cmp ax, [apu_ch]
    jae .done                      ; below the content
    mov dx, [apu_oy]
    add dx, ax                     ; DX = row y
    mov cx, [apu_ox]
    add cx, 8
    ; marker for the current entry
    mov si, apu_sp
    mov ax, di
    cmp al, [apl_cur]
    jne .nomark
    mov si, apu_gt
.nomark:
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN
    ; the name
    mov ax, di
    call apl_entptr               ; BX = &entry
    mov si, bx
    mov cx, [apu_ox]
    add cx, 20
    mov ax, 0x0F00                 ; FONT_RUN: ink 0 on paper 15 (SPEC.md 6.6)
    call OSAPI_FONT_RUN
    inc di
    jmp .row
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

apu_sp: db ' ', 0
apu_gt: db '>', 0

; =============================================================================
; apu_hit - a content-relative click point -> AL = AP_B_* or AP_B_NONE
; in:  CX = x (content-relative), DX = y (content-relative)
;
; The buttons tile the row gap-free: column = (x - 4) / pitch, so a click
; anywhere on the row lands on a button and there is no 3px dead lane between
; two of them (found on the glass).
; =============================================================================
apu_hit:
    push bx
    push dx
    ; y must be on the transport row (rects[0] carries y1/y2 for all seven)
    cmp dx, [apu_rects+2]
    jb .none
    cmp dx, [apu_rects+6]
    ja .none
    mov ax, cx
    cmp ax, [apu_rects+0]              ; left edge of button 0
    jb .none
    sub ax, [apu_rects+0]
    cmp word [apu_bpitch], 0
    je .none
    xor dx, dx
    div word [apu_bpitch]             ; AX = column
    cmp ax, 7
    jae .none
    pop dx
    pop bx
    ret
.none:
    mov ax, AP_B_NONE
    pop dx
    pop bx
    ret

    APBUF apu_abr, 8               ; scratch absolute button rect
    APW   apu_bpitch
    APW   apu_ln
