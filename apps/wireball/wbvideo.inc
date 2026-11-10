; Pixel court. Hidden VRAM pages on Mode X/Hercules, small packed spans on CGA.
wv_fontload:
    push es
    call OSAPI_FONT_GLYPHS
    mov [wv_fontfirst], al
    mov [wv_fontlast], ah
    mov [wv_fontbase], si
    mov es, dx
    mov di, wv_font
    mov bx, 32
.char:
    mov cx, 8
    cmp bl, [wv_fontfirst]
    jb .blank
    cmp bl, [wv_fontlast]
    ja .blank
    mov ax, bx
    sub al, [wv_fontfirst]
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [wv_fontbase]
    mov si, ax
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .copy
    jmp .next
.blank:
    mov byte [di], 0
    inc di
    loop .blank
.next:
    inc bx
    cmp bx, 128
    jb .char
    pop es
    ret

wv_mode:
    mov byte [wv_graphics], 0
    mov byte [wv_pages], 1
    mov byte [wv_page], 0
    mov word [wv_xunit], 1
    mov word [wv_yunit], 2
    mov word [wv_origin], 0
    mov word [wv_top], 32
    mov al, FSXM_TEXT80
    cmp byte [wv_fast], 0
    je .set
    mov bx, [wb_win]
    call OSAPI_FSX_CAPS
    test ax, 1 << FSXM_MODEX
    jz .mono
    mov al, FSXM_MODEX
    jmp .graphics
.mono:
    mov word [wv_xunit], 2
    mov word [wv_top], 24
    test ax, 1 << FSXM_HERC
    jz .cga
    mov word [wv_yunit], 4
    mov word [wv_origin], 40
    mov word [wv_top], 16
    mov al, FSXM_HERC
    jmp .graphics
.cga:
    mov al, FSXM_CGA640
.graphics:
    mov byte [wv_graphics], 1
.set:
    mov [wv_modeid], al
    push ds
    pop es
    mov di, wb_fsi
    call OSAPI_FSX_MODE
    jc .out
    cmp byte [wv_graphics], 0
    je .out
    cmp byte [wb_fsi+FSI_PAGES], 2
    jb .ok
    mov al, 1
    call OSAPI_FSX_PAGE         ; prove page 1 is permitted before addressing it
    jc .ok                    ; a dual-card Hercules must keep a single page
    mov byte [wv_pages], 2
.ok:
    clc
.out:
    ret

wv_surface:
    mov ax, [wb_fsi+FSI_SEG]
    mov es, ax
    mov word [wv_offset], 0
    cmp byte [wv_page], 0
    je .out
    mov word [wv_offset], 19200
    cmp byte [wv_modeid], FSXM_HERC
    jne .out
    mov word [wv_offset], 0x8000
.out:
    ret

wv_leave:
    cmp byte [wv_pages], 2
    jb .out
    xor al, al
    call OSAPI_FSX_PAGE             ; park page 0 before the desktop mode restore
.out:
    ret

wv_board:
    mov word [wv_hudvalid], 0
    mov di, wv_old
    mov cx, 48
.empty:
    mov byte [di], 0
    inc di
    loop .empty
    mov di, wv_hudold
    mov cx, 160
.invalid:
    mov byte [di], 0xff
    inc di
    loop .invalid
    mov byte [wv_page], 0
.page:
    call wv_surface
    mov di, [wv_offset]
    mov cx, 8192
    cmp byte [wv_modeid], FSXM_HERC
    jne .modex_check
    mov cx, 16384
.modex_check:
    cmp byte [wv_modeid], FSXM_MODEX
    jne .clear
    mov dx, 0x3c4
    mov ax, 0x0f02
    out dx, ax
    mov cx, 9600
.clear:
    xor ax, ax
    cld
    rep stosw
    mov ax, [wv_origin]
    mov [wv_rectbuf], ax
    mov ax, [wv_top]
    dec ax
    mov [wv_rectbuf+2], ax
    mov ax, 320
    mul word [wv_xunit]
    mov [wv_rectbuf+4], ax
    mov word [wv_rectbuf+6], 1
    mov si, wv_rectbuf
    mov al, 8
    call wv_rect
    mov ax, 80
    mul word [wv_yunit]
    add ax, [wv_top]
    mov [wv_rectbuf+2], ax
    mov si, wv_rectbuf
    mov al, 8
    call wv_rect
    mov ax, [wv_origin]
    mov [wv_tx], ax
    mov word [wv_ty], 188
    mov si, wb_footer
    cmp byte [wv_modeid], FSXM_CGA640
    je .footer
    mov word [wv_ty], 338
    cmp byte [wv_modeid], FSXM_HERC
    je .footer
    mov word [wv_ty], 208
    mov si, wv_help1
    call wv_text
    mov word [wv_tx], 0
    mov word [wv_ty], 220
    mov si, wv_help2
.footer:
    call wv_text
    inc byte [wv_page]
    mov al, [wv_page]
    cmp al, [wv_pages]
    jb .page
    mov byte [wv_page], 0
    ret

wv_build:
    mov di, wv_new
    mov cx, 12
    xor ax, ax
.empty:
    mov [di], ax
    add di, 2
    loop .empty
    cmp byte [wb_phase], 2
    jne .out
    mov di, wv_new
    cmp byte [wb_side], 0
    je .left
    cmp byte [wb_practice], 0
    je .right
.left:
    mov ax, 6
    mov bl, [wb_left]
    call wv_paddle
.right:
    mov di, wv_new+8
    cmp byte [wb_side], 1
    je .rpaddle
    cmp byte [wb_practice], 0
    je .ball
.rpaddle:
    mov ax, 306
    mov bl, [wb_right]
    call wv_paddle
.ball:
    mov ax, [wb_ballx]
    cmp byte [wb_side], 0
    je .local
    sub ax, 312
.local:
    cmp ax, 312
    jae .out
    add ax, 2
    mul word [wv_xunit]
    add ax, [wv_origin]
    mov bx, ax
    xor ax, ax
    mov al, [wb_fracx]
    mul word [wv_xunit]
    shr ax, 1
    shr ax, 1
    add ax, bx
    mov [wv_new+16], ax
    mov ax, [wb_bally]
    mul word [wv_yunit]
    add ax, [wv_top]
    mov bx, ax
    xor ax, ax
    mov al, [wb_fracy]
    mul word [wv_yunit]
    shr ax, 1
    shr ax, 1
    add ax, bx
    mov [wv_new+18], ax
    mov ax, 4
    mul word [wv_xunit]
    mov [wv_new+20], ax
    mov ax, 2
    mul word [wv_yunit]
    mov [wv_new+22], ax
.out:
    ret
wv_paddle:
    mul word [wv_xunit]
    add ax, [wv_origin]
    mov [di], ax
    xor bh, bh
    mov ax, bx
    mul word [wv_yunit]
    add ax, [wv_top]
    mov [di+2], ax
    mov ax, 4
    mul word [wv_xunit]
    mov [di+4], ax
    mov ax, 16
    mul word [wv_yunit]
    mov [di+6], ax
    ret

wv_draw:
    call wv_surface
    call wv_build
    cmp byte [wv_pages], 1
    jne .start
    mov al, FSXW_VSYNC
    call OSAPI_FSX_WAIT         ; short changed spans follow the CGA's blank
.start:
    mov al, [wv_page]
    xor ah, ah
    mov bx, 24
    mul bx
    add ax, wv_old
    mov [wv_oldptr], ax
    mov byte [wv_i], 0
.erase:
    mov al, [wv_i]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, [wv_oldptr]
    mov di, ax
    add di, wv_new
    mov cx, 4
.compare:
    mov ax, [si]
    cmp ax, [di]
    jne .changed
    add si, 2
    add di, 2
    loop .compare
    mov al, [wv_i]
    xor ah, ah
    mov bx, ax
    mov byte [wv_dirty+bx], 0
    jmp .next_erase
.changed:
    mov al, [wv_i]
    xor ah, ah
    mov bx, ax
    mov byte [wv_dirty+bx], 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, [wv_oldptr]
    mov di, ax
    add di, wv_new
    cmp byte [wv_i], 2
    jae .full_erase
    call wv_paddledelta
    jc .full_erase
    mov al, [wv_i]
    xor ah, ah
    mov bx, ax
    mov byte [wv_dirty+bx], 0
    mov cx, 4
.delta_copy:
    mov ax, [di]
    mov [si], ax
    add di, 2
    add si, 2
    loop .delta_copy
    jmp .next_erase
.full_erase:
    xor al, al
    call wv_rect
.next_erase:
    inc byte [wv_i]
    cmp byte [wv_i], 3
    jb .erase
    mov byte [wv_i], 0
.draw:
    mov al, [wv_i]
    xor ah, ah
    mov bx, ax
    cmp byte [wv_dirty+bx], 0
    je .next_draw
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, wv_new
    mov al, 15
    call wv_rect
    mov al, [wv_i]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, ax
    add si, wv_new
    mov di, ax
    add di, [wv_oldptr]
    mov cx, 4
.copy:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .copy
.next_draw:
    inc byte [wv_i]
    cmp byte [wv_i], 3
    jb .draw
    call wv_hud
    inc word [wv_frames]
    cmp byte [wv_pages], 2
    jb .out
    mov al, [wv_page]
    call OSAPI_FSX_PAGE
    xor byte [wv_page], 1
.out:
    ret

; SI=old and DI=new paddle. Keep their overlapping rows in VRAM, writing
; only exposed/covered strips. CF=1 requests the full rectangle path.
wv_paddledelta:
    mov ax, [si]
    cmp ax, [di]
    jne .full
    mov ax, [si+4]
    cmp ax, [di+4]
    jne .full
    mov ax, [si+6]
    or ax, ax
    jz .full
    cmp ax, [di+6]
    jne .full
    mov bx, [di+2]
    sub bx, [si+2]
    jns .down
    neg bx
    cmp bx, ax
    jae .full
    mov [wv_rectbuf+6], bx
    mov bx, [di+2]
    mov [wv_stripdraw], bx
    add bx, ax
    mov [wv_rectbuf+2], bx
    jmp .strips
.down:
    cmp bx, ax
    jae .full
    mov [wv_rectbuf+6], bx
    mov bx, [si+2]
    mov [wv_rectbuf+2], bx
    add bx, ax
    mov [wv_stripdraw], bx
.strips:
    mov ax, [si]
    mov [wv_rectbuf], ax
    mov ax, [si+4]
    mov [wv_rectbuf+4], ax
    push si
    push di
    mov si, wv_rectbuf
    xor al, al
    call wv_rect
    mov ax, [wv_stripdraw]
    mov [wv_rectbuf+2], ax
    mov si, wv_rectbuf
    mov al, 15
    call wv_rect
    pop di
    pop si
    clc
    ret
.full:
    stc
    ret

; SI = physical x/y/w/h rectangle, AL = ink. Packed backends write only
; the covered byte masks; Mode X selects a plane once per sprite column.
wv_rect:
    mov [wv_ink], al
    mov ax, [si+4]
    or ax, ax
    jz .out
    mov [wv_w], ax
    mov ax, [si+6]
    or ax, ax
    jz .out
    mov [wv_h], ax
    mov ax, [si]
    mov [wv_x], ax
    mov ax, [si+2]
    mov [wv_y], ax
    cmp byte [wv_modeid], FSXM_MODEX
    je .modex
    mov ax, [wv_x]
    mov cx, ax
    and cl, 7
    mov bl, 0xff
    shr bl, cl
    mov [wv_lmask], bl
    mov cl, 3
    shr ax, cl
    mov [wv_firstbyte], ax
    mov ax, [wv_x]
    add ax, [wv_w]
    dec ax
    mov cx, ax
    and cl, 7
    mov bl, 7
    sub bl, cl
    mov cl, bl
    mov bl, 0xff
    shl bl, cl
    mov [wv_rmask], bl
    mov cl, 3
    shr ax, cl
    sub ax, [wv_firstbyte]
    inc ax
    mov [wv_nbytes], ax
    cmp ax, 2
    jbe .span
.row:
    mov ax, [wv_y]
    call wv_address
    add di, [wv_firstbyte]
    mov cx, [wv_nbytes]
.byte:
    mov al, 0xff
    cmp cx, [wv_nbytes]
    jne .last
    and al, [wv_lmask]
.last:
    cmp cx, 1
    jne .ink
    and al, [wv_rmask]
.ink:
    mov ah, [es:di]
    cmp byte [wv_ink], 0
    je .erase
    or ah, al
    jmp .store
.erase:
    not al
    and ah, al
.store:
    mov [es:di], ah
    inc di
    loop .byte
    inc word [wv_y]
    dec word [wv_h]
    jnz .row
    ret
.span:
    ; Sprites cover one/two bytes. A masked word handles both end masks at
    ; once; scanlines advance through banks with adds instead of a MUL.
    mov ax, [wv_y]
    call wv_address
    add di, [wv_firstbyte]
    mov dx, [wv_y]
    mov cx, [wv_h]
    mov al, 1
    mov word [wv_wrap], 0x4000-80
    cmp byte [wv_modeid], FSXM_HERC
    jne .spanmask
    mov al, 3
    mov word [wv_wrap], 0x8000-90
.spanmask:
    mov bl, [wv_lmask]
    mov bh, [wv_rmask]
    cmp word [wv_nbytes], 2
    je .wordspan
    and bl, bh
    cmp byte [wv_ink], 0
    jne .bytepaint
    not bl
.byteerase:
    and [es:di], bl
    call .spanline
    loop .byteerase
    ret
.bytepaint:
    or [es:di], bl
    call .spanline
    loop .bytepaint
    ret
.wordspan:
    cmp byte [wv_ink], 0
    jne .wordpaint
    not bx
.worderase:
    and [es:di], bx
    call .spanline
    loop .worderase
    ret
.wordpaint:
    or [es:di], bx
    call .spanline
    loop .wordpaint
    ret
.spanline:
    add di, 0x2000
    inc dx
    test dl, al
    jnz .spanned
    sub di, [wv_wrap]
.spanned:
    ret
.modex:
    mov ax, [wv_x]
    mov [wv_cx], ax
.column:
    mov ax, [wv_cx]
    mov cx, ax
    and cl, 3
    ; Up to four neighboring pixels share one VRAM byte in different planes.
    ; Uniform rectangle ink can write all of those planes at once.
    mov ax, 4
    sub al, cl
    cmp ax, [wv_w]
    jbe .group
    mov ax, [wv_w]
.group:
    mov [wv_nbytes], ax
    mov bx, ax
    mov ah, [wv_planemasks+bx]
    shl ah, cl
    mov al, 2
    mov dx, 0x3c4
    out dx, ax
    mov ax, [wv_y]
    mov dx, 80
    mul dx
    mov di, [wv_cx]
    shr di, 1
    shr di, 1
    add di, ax
    add di, [wv_offset]
    mov cx, [wv_h]
    mov al, [wv_ink]
.pixel:
    mov [es:di], al
    add di, 80
    loop .pixel
    mov ax, [wv_nbytes]
    add [wv_cx], ax
    sub [wv_w], ax
    jnz .column
.out:
    ret

; AX = physical scanline; DI = its banked row offset. ES stays video RAM.
wv_address:
    mov bx, ax
    and bx, 1
    shr ax, 1
    cmp byte [wv_modeid], FSXM_HERC
    jne .cga
    mov bx, [wv_y]
    and bx, 3
    shr ax, 1
    mov dx, 90
    jmp .bank
.cga:
    mov dx, 80
.bank:
    ror bx, 1
    ror bx, 1
    ror bx, 1
    mul dx
    add ax, bx
    add ax, [wv_offset]
    mov di, ax
    ret

wv_hud:
    ; Comparing five state words is far cheaper on an 8088 than rebuilding
    ; and comparing 80 text cells on every CRT frame.
    mov ax, [wb_phase]          ; phase and side are adjacent
    mov [wv_hudkey], ax
    mov ax, [wb_flags]          ; flags and serve countdown are adjacent
    or ah, ah
    jz .readybit
    mov ah, 1
.readybit:
    mov [wv_hudkey+2], ax
    mov ax, [wb_scorel]
    mov [wv_hudkey+4], ax
    mov ax, [wb_port]
    mov [wv_hudkey+6], ax
    mov ax, [wb_ports+WP_BASE]
    or ax, [wb_ports+WP_SIZE+WP_BASE]
    mov [wv_hudkey+8], ax
    mov al, [wv_page]
    xor ah, ah
    mov bx, ax
    mov byte [wv_hudchanged], 0
    cmp byte [wv_hudvalid+bx], 0
    jne .keypage
    mov byte [wv_hudchanged], 1
    mov byte [wv_hudvalid+bx], 1
.keypage:
    mov bx, 10
    mul bx
    add ax, wv_hudstate
    mov di, ax
    mov si, wv_hudkey
    mov cx, 5
.key:
    mov ax, [si]
    cmp ax, [di]
    je .samekey
    mov byte [wv_hudchanged], 1
    mov [di], ax
.samekey:
    add si, 2
    add di, 2
    loop .key
    cmp byte [wv_hudchanged], 0
    jne .update
    ret
.update:
    call wb_hudmake
    mov al, [wv_page]
    xor ah, ah
    mov bx, 80
    mul bx
    add ax, wv_hudold
    mov [wv_hudptr], ax
    mov byte [wv_hi], 0
.cell:
    mov al, [wv_hi]
    xor ah, ah
    mov bx, ax
    mov si, [wv_hudptr]
    mov al, [wb_hud+bx]
    cmp al, [si+bx]
    je .next
    mov [si+bx], al
    push ax
    mov ax, bx
    mov word [wv_ty], 2
    cmp byte [wv_modeid], FSXM_MODEX
    jne .position
    cmp ax, 40
    jb .position
    sub ax, 40
    mov word [wv_ty], 14
.position:
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, [wv_origin]
    mov [wv_tx], ax
    pop ax
    call wv_glyph
.next:
    inc byte [wv_hi]
    cmp byte [wv_hi], 80
    jb .cell
    ret
wv_text:
    mov [wv_string], si
.char:
    mov si, [wv_string]
    mov al, [si]
    or al, al
    jz .out
    inc word [wv_string]
    call wv_glyph
    add word [wv_tx], 8
    jmp .char
.out:
    ret
wv_glyph:
    sub al, 32
    cmp al, 96
    jb .supported
    xor al, al
.supported:
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    add ax, wv_font
    mov [wv_gptr], ax
    cmp byte [wv_modeid], FSXM_MODEX
    je .modex
    mov byte [wv_gy], 0
.row:
    mov al, [wv_gy]
    xor ah, ah
    add ax, [wv_ty]
    mov [wv_y], ax
    call wv_address
    mov bx, [wv_tx]
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add di, bx
    mov bl, [wv_gy]
    xor bh, bh
    add bx, [wv_gptr]
    mov al, [bx]
    mov [es:di], al
    inc byte [wv_gy]
    cmp byte [wv_gy], 8
    jb .row
    ret
.modex:
    mov byte [wv_gx], 0
.column:
    mov al, [wv_gx]
    xor ah, ah
    add ax, [wv_tx]
    mov bx, ax
    mov cx, ax
    and cl, 3
    mov ah, 1
    shl ah, cl
    mov al, 2
    mov dx, 0x3c4
    out dx, ax
    shr bx, 1
    shr bx, 1
    mov ax, [wv_ty]
    mov dx, 80
    mul dx
    add ax, bx
    add ax, [wv_offset]
    mov di, ax
    mov cl, [wv_gx]
    mov bl, 0x80
    shr bl, cl
    mov si, [wv_gptr]
    mov cx, 8
.pixel:
    xor al, al
    test [si], bl
    jz .store
    mov al, 15
.store:
    mov [es:di], al
    inc si
    add di, 80
    loop .pixel
    inc byte [wv_gx]
    cmp byte [wv_gx], 8
    jb .column
    ret

wv_help1: db 'UP/DOWN W/S: MOVE  P: PAUSE  M: SOUND',0
wv_help2: db 'N: NEW MATCH   ESC: DESKTOP',0
wv_planemasks: db 0,1,3,7,15

%macro WV_BSS 0
WB_VAR wv_graphics,1
WB_VAR wv_modeid,1
WB_VAR wv_pages,1
WB_VAR wv_page,1
WB_VAR wv_xunit,2
WB_VAR wv_yunit,2
WB_VAR wv_origin,2
WB_VAR wv_top,2
WB_VAR wv_offset,2
WB_VAR wv_frames,2
WB_VAR wv_old,48
WB_VAR wv_new,24
WB_VAR wv_dirty,3
WB_VAR wv_oldptr,2
WB_VAR wv_i,1
WB_VAR wv_rectbuf,8
WB_VAR wv_stripdraw,2
WB_VAR wv_x,2
WB_VAR wv_y,2
WB_VAR wv_w,2
WB_VAR wv_h,2
WB_VAR wv_cx,2
WB_VAR wv_ink,1
WB_VAR wv_firstbyte,2
WB_VAR wv_nbytes,2
WB_VAR wv_wrap,2
WB_VAR wv_lmask,1
WB_VAR wv_rmask,1
WB_VAR wv_hudold,160
WB_VAR wv_hudkey,10
WB_VAR wv_hudstate,20
WB_VAR wv_hudvalid,2
WB_VAR wv_hudchanged,1
WB_VAR wv_hudptr,2
WB_VAR wv_hi,1
WB_VAR wv_font,768
WB_VAR wv_fontfirst,1
WB_VAR wv_fontlast,1
WB_VAR wv_fontbase,2
WB_VAR wv_tx,2
WB_VAR wv_ty,2
WB_VAR wv_string,2
WB_VAR wv_gptr,2
WB_VAR wv_gx,1
WB_VAR wv_gy,1
%endmacro
