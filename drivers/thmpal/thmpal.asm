; =============================================================================
; os8088 - THMPAL.DRV: the Customize... window for Color's eight presets
; (SPEC.md 76.14)
;
; THIS IS AN OVERLAY, NOT A DRIVER (SPEC.md 79.2, 41.12, 52.11) - the same
; species as SAVER.DRV's settings window (drivers/saver/svcfg.inc) and
; HDD.DRV's disk tool (SPEC.md 52.2): a driver's header, org 0, a driver's
; three-byte dispatcher and a driver's one-claim load, class DRVC_OVL, which
; the kernel deliberately does not know - no row in drv_tab, no publication
; slot, no Drivers page tick, no SYSTEM.CFG bit of its own and no Control
; Panel page. OUR OWNER IS THE KERNEL, same as SAVER.DRV's: kernel/vga12.inc's
; thmv_open_x loads it and asks it to open; thmv_reap_x frees it, called once
; from kernel/ctrl.inc's cp_flush_close_x (the Control Panel's own teardown,
; the latest point this window can still be open at, since opening it is
; only reachable from the Theme page).
;
; WHY AN OVERLAY AND NOT AN ON-DEMAND MODULE (SPEC.md 2.8) - SAVER.DRV's own
; header gives the reason word for word: the eight preset names and the two
; button captions are DATA a Control Panel page cannot afford, and an
; overlay owns a segment, so every byte of it is on the floppy and none of
; it is in the kernel.
;
; OPEN creates or raises the window; BUSY queries its lifetime; CLOSE
; destroys it before the parent releases the image. This matters because the
; window's W_PAINT is a near pointer into this segment, and freeing the
; image under it is a paint into whatever the heap hands out next.
;
; LIVE PREVIEW IS thm_set / thm_set_preset (kernel/vga12.inc), called
; exactly as a Color radio click calls them - SPEC.md 76.14 says why. This
; file owns no palette data of its own: it lists eight names; list rows are the contiguous preset IDs.
;
; REGISTERS: tv_entry preserves everything but AX and the flags (sv_entry's
; own contract, SPEC.md 79.3).
; =============================================================================

%include "os88drv.inc"

TV_ABI_VER  equ 3                ; MIRRORED in kernel/vga12.inc's thmv_* - the
                                  ; private interface those routines were
                                  ; built against

    OS88_OVERLAY 'THMPAL', TV_ABI_VER, tv_entry

; --- the verbs, MIRRORED IN kernel/vga12.inc ---------------------------------
THMV_OPEN   equ 0
THMV_BUSY   equ 1
THMV_CLOSE  equ 2

TV_NPRESET  equ 8                ; selectable rows, matching kernel preset IDs
THM_COLOR   equ 2                ; MIRRORED from kernel/vga12.inc's THM_COLOR -
                                  ; this file never touches [thm_kind] itself,
                                  ; only passes this one constant to cw_thm_set
THM_WIN     equ 3
TV_STY_SOLID   equ 0
TV_STY_STRIPES equ 1
TV_STY_WIN     equ 2

; --- the window (SPEC.md 76.14's layout) -------------------------------------
TV_FW       equ 200
TV_FH       equ 218
TV_X        equ 110
TV_Y        equ 16

TV_L0Y      equ 3                ; first row's top, content-relative
TV_LROWH    equ 11               ; row pitch for eight preset names
                                  ; the way the Theme page's own groups do
TV_LX       equ 3                ; the highlight bar's left edge
TV_LW       equ TV_FW - 2 - 2*TV_LX   ; ...and its width: content width minus
                                  ; the same margin on both sides
TV_TXDY     equ 1                ; text y inside an 11px row (8px glyph)
TV_TXX      equ TV_LX + 3        ; text x, inset from the bar's own edge

TV_SHY      equ TV_L0Y + TV_NPRESET*TV_LROWH + 8 ; clear gap after preset list
TV_SY       equ TV_SHY + 10       ; radio group, below the heading
TV_SCOUNT   equ 3
TV_SPITCH   equ 16
TV_BY       equ TV_SY + TV_SCOUNT*TV_SPITCH + 10 ; gap after Windows/95
TV_BH       equ 16
TV_BW       equ 70
TV_B0X      equ 16               ; Apply
TV_B1X      equ TV_FW - 2 - 16 - TV_BW   ; Cancel, mirrored off the right edge
TV_CAPPLY   equ 1
TV_CCANCEL  equ 2
TV_NBTN     equ 2

%if TV_BY + TV_BH + 4 > TV_FH - TITLE_H - 1
%error "thmpal: the Customize window runs past its own content box"
%endif

; os88ui.inc's standard button (os88ui_btn/arm/fire/armed), included here -
; BEFORE every proc below that names it - the same order saver.asm uses
; ahead of svcfg.inc.
%define OS88UI_ARM              ; os88ui_arm/fire/armed: the press/release...
%define OS88UI_NOGEST           ; ...and this window drives its own buttons
                                 ; through them (tv_click, tv_drag, tv_up) -
                                 ; the record-based gesture half is bytes
                                 ; nothing here calls (SPEC.md 20.5.1.3.4),
                                 ; svcfg.inc's own shape
%define OS88UI_NOBFIND          ; hit-tests its own rects: no os88ui_bfind
%define OS88UI_RAD              ; standard radio group for title-bar style
%define OS88UI_CHK              ; Classic Windows button colors
%include "os88ui.inc"

; =============================================================================
; tv_entry - the one door (sv_entry's shape, SPEC.md 79.3)
; in:  AL = THMV_*, DS = CS, ES = KERNEL_SEG, SS = LOW_SEG
; out: CF as the verb documents; every register but AX preserved
; =============================================================================
tv_entry:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call tv_disp
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

tv_disp:
    cmp al, THMV_OPEN
    je tv_do_open
    cmp al, THMV_BUSY
    je tv_do_busy
    cmp al, THMV_CLOSE
    je tv_kill
    stc                          ; a verb from a kernel we were not built with
    ret

; -----------------------------------------------------------------------------
; tv_do_busy - may our image be freed? (sv_do_busy's shape, SPEC.md 79.8)
; out: CF = 1 = no
; -----------------------------------------------------------------------------
tv_do_busy:
    cmp word [tv_win], 0
    je .free
    stc
    ret
.free:
    clc
    ret

; -----------------------------------------------------------------------------
; tv_do_open - open the window, or bring it to the front if it is already up
; in:  the gfx lock is HELD - this is a Control Panel click (SPEC.md 31.1);
;      DL = persisted title-style bits; CH/CL = the CURRENT [thm_kind]/[thm_preset] - this file has no symbol
;      for either address (kernel/vga12.inc's thmv_open_x is the one caller
;      and the one reader, SPEC.md 76.14)
; out: CF = 0 the window is up; CF = 1 it could not be created
;
; THE ORIGINAL (kind, preset) IS BANKED EVERY TIME, even if the window is
; already open - sv_do_cfg's own rule ("seeded from the live settings every
; time") applied to a re-click rather than only to a reopen. Opening this
; window only reads the current settings; it never changes the active theme.
; -----------------------------------------------------------------------------
tv_do_open:
    mov al, dl
    and al, 7
    mov [tv_ostripes], al
    mov ah, al
    and al, 1
    mov [tv_stripes], al
    mov al, ah
    shr al, 1
    mov [tv_classic], al
    mov [tv_classic_box+OS88UI_CK_ON], al
    cmp dh, 0                   ; kernel marks an already-live Windows theme
    je .colorstyle
    mov al, TV_STY_WIN
    jmp short .style_ready
.colorstyle:
    mov al, [tv_stripes]
.style_ready:
    mov [tv_live_style], al
    xor ah, ah
    mov [tv_styles+OS88UI_RD_SEL], ax
    mov al, ch
    mov [tv_okind], al
    mov al, cl
    mov [tv_opreset], al
    cmp al, TV_NPRESET
    jb .selected
    xor al, al                  ; an unknown preset displays Classic
.selected:
    mov [tv_sel], al
    mov bx, [tv_win]
    or bx, bx
    jnz .show
    mov si, tv_tpl
    call OSAPI_WM_CREATE
    jc .no
    mov [tv_win], bx
    mov ax, tv_up                ; SPEC.md 13.7/13.8.1: NOT template words, so
    call OSAPI_WM_ONMOUSEUP      ; a window installs them itself after create
    mov ax, tv_drag
    call OSAPI_WM_ONDRAG
    mov ax, tv_close
    call OSAPI_WM_ONCLOSE        ; what makes this image safe to free (header)
    mov byte [tv_down], 0
    xor ax, ax
    call os88ui_arm              ; a window reopened mid-gesture must not come
.show:                           ; up with one armed for the next release
    call OSAPI_WM_SHOW
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tv_kill - destroy the window if it exists (sc_kill's shape)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tv_kill:
    push ax
    push bx
    mov bx, [tv_win]
    or bx, bx
    jz .out
    mov word [tv_win], 0
    call OSAPI_WM_DESTROY
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tv_cancel - restore the (kind, preset) that was live when the window opened
; out: nothing (all registers preserved, beyond what the two calls document)
;
; Restoring Windows needs style bits first so it installs matching callbacks.
; Bright/Dark restore kind first so the previewed Color row is resolved once.
; -----------------------------------------------------------------------------
tv_cancel:
    push ax
    cmp byte [tv_okind], THM_WIN
    je .style_first
    cmp byte [tv_okind], THM_COLOR
    jne .kind_first
    cmp byte [tv_live_style], TV_STY_WIN
    je .style_first             ; clear the Windows preference before Color
.kind_first:
    mov al, [tv_okind]
    call OSAPI_THM_SET
    mov al, [tv_ostripes]
    call OSAPI_THM_SET_STRIPES
    jmp short .preset
.style_first:
    mov al, [tv_ostripes]
    call OSAPI_THM_SET_STRIPES
    mov al, [tv_okind]
    call OSAPI_THM_SET
.preset:
    mov al, [tv_opreset]
    call OSAPI_THM_SET_PRESET
    pop ax
    ret

; -----------------------------------------------------------------------------
; tv_close - W_ONCLOSE: DESTROYS the window itself and refuses (sc_close's
; shape, SPEC.md 79.7's own reason): a hidden window holding a W_PAINT that
; names this segment is a paint into whatever the heap hands out next once
; thmv_reap_x frees it, so wm_hide is exactly wrong here.
; -----------------------------------------------------------------------------
tv_close:
    call tv_kill
    stc
    ret

; -----------------------------------------------------------------------------
; tv_layout - DI/BP = content left/top (screen), WITHOUT disturbing CX/DX -
; OSAPI_WM_CONTENT answers in AX/DX, and a caller that still needs the
; ORIGINAL CX/DX (a click or release's coordinates) after asking for the
; content box needs them banked first. sc_layout's exact shape.
; -----------------------------------------------------------------------------
tv_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [tv_win]
    call OSAPI_WM_CONTENT
    mov di, ax
    mov bp, dx
    jmp os88_r_si

; -----------------------------------------------------------------------------
; tv_paint - W_PAINT (content already white-filled, lock held)
; -----------------------------------------------------------------------------
tv_paint:
    call os88_ent
    mov bx, [tv_win]
    call OSAPI_WM_CONTENT        ; nothing after this needs the ORIGINAL
    mov di, ax                   ; CX/DX, so the plain call is enough here
    mov bp, dx
    call tv_list_paint
    call tv_style_paint
    call tv_btns
    jmp os88_r_bp

; The title-style radio group moves with the window; fill only its hit rect.
tv_style_layout:
    push ax
    mov ax, di
    add ax, TV_LX
    mov [tv_styles+OS88UI_RD_RECT+0], ax
    add ax, TV_LW-1
    mov [tv_styles+OS88UI_RD_RECT+4], ax
    mov ax, bp
    add ax, TV_SY
    mov [tv_styles+OS88UI_RD_RECT+2], ax
    add ax, TV_SCOUNT*TV_SPITCH-1
    mov [tv_styles+OS88UI_RD_RECT+6], ax
    mov ax, di
    add ax, TV_CLASSIC_X
    mov [tv_classic_box+OS88UI_CK_RECT+0], ax
    add ax, TV_CLASSIC_W-1
    mov [tv_classic_box+OS88UI_CK_RECT+4], ax
    mov ax, bp
    add ax, TV_SY + 2*TV_SPITCH + 2
    mov [tv_classic_box+OS88UI_CK_RECT+2], ax
    add ax, 11
    mov [tv_classic_box+OS88UI_CK_RECT+6], ax
    pop ax
    ret

tv_style_paint:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov al, CBLACK
    mov ah, CWHITE
    mov si, tv_s_style
    mov cx, di
    add cx, TV_TXX
    mov dx, bp
    add dx, TV_SHY
    push di
    push bp
    call OSAPI_FONT_RUN
    pop bp
    pop di
    call tv_style_layout
    mov bx, tv_styles
    xor di, di
    call os88ui_rad
    mov bx, tv_classic_box
    mov al, [tv_styles+OS88UI_RD_SEL]
    cmp al, TV_STY_WIN
    je .classic_enabled
    mov di, OS88UI_DIS
    jmp short .classic_draw
.classic_enabled:
    xor di, di
.classic_draw:
    call os88ui_chk
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tv_list_paint - all eight rows, the selected one filled
; in:  DI = content left, BP = content top (screen); gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tv_list_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    xor cx, cx                   ; CX = row 0..7
.row:
    mov al, cl
    mov ah, TV_LROWH
    mul ah                       ; AX = row * TV_LROWH, <= 99
    add ax, TV_L0Y
    add ax, bp                   ; AX = this row's top, screen y
    mov [tv_rowy], ax

    cmp cl, [tv_sel]
    jne .plain
    mov al, CBLACK
    call OSAPI_SET_COLOR
    push cx                      ; the fill wants AX/BX/CX/DX: bank the loop
    mov ax, di                   ; counter across it
    add ax, TV_LX
    mov bx, [tv_rowy]
    mov cx, ax
    add cx, TV_LW - 1
    mov dx, bx
    add dx, TV_LROWH - 1
    call OSAPI_GFX_FILL
    pop cx
    mov al, CWHITE                ; ink, paper for the text pass below
    mov ah, CBLACK
    jmp short .text
.plain:
    mov al, CBLACK
    mov ah, CWHITE
.text:
    push ax                      ; bank ink/paper across the name lookup
    mov bl, cl
    mov bh, 0
    add bx, bx                   ; word index into tv_names
    mov si, [bx+tv_names]
    pop ax
    push cx
    mov cx, di
    add cx, TV_TXX
    mov dx, [tv_rowy]
    add dx, TV_TXDY
    call OSAPI_FONT_RUN
    pop cx

    inc cx
    cmp cx, TV_NPRESET
    jb .row

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tv_click - W_ONCLICK (absolute screen coords, lock held)
; in:  CX = x, DX = y, SI = the window
;
; A ROW ACTS ON THE PRESS, same reason SAVER's check boxes do (SPEC.md 13.6):
; it has a safe prefix action - clicking a different row again undoes it, in
; view - and Apply/Cancel do not, so a mis-aimed press on either has to be
; cancellable by sliding off.
; -----------------------------------------------------------------------------
tv_click:
    call os88_ent
    call tv_layout                ; DI/BP set; CX/DX still the click's own
    call tv_style_layout
    cmp word [tv_styles+OS88UI_RD_SEL], TV_STY_WIN
    jne .style_hit
    mov bx, tv_classic_box
    call os88ui_chkhit
    jnc .classic_toggle
.style_hit:
    mov bx, tv_styles
    call os88ui_radhit
    jc .list
    jnz .out                      ; same/disabled style: no appearance change
    mov ax, [tv_styles+OS88UI_RD_SEL]
    cmp al, TV_STY_WIN
    je .set_windows
    mov [tv_stripes], al
    cmp byte [tv_live_style], TV_STY_WIN
    jne .set_stripes              ; already in Color: one setter, one repaint
    call tv_stylebits_get         ; stage both settings while Windows
    call OSAPI_THM_SET_STRIPES    ; is visible, then resolve once on the switch
    mov al, [tv_sel]
    call OSAPI_THM_SET_PRESET
    mov al, THM_COLOR
    call OSAPI_THM_SET
    mov al, [tv_stripes]
    mov [tv_live_style], al
    jmp .out
.set_stripes:
    call tv_stylebits_get
    call OSAPI_THM_SET_STRIPES
    mov al, [tv_styles+OS88UI_RD_SEL]
    mov [tv_live_style], al
    jmp .out
.set_windows:
    call tv_stylebits_get
    call OSAPI_THM_SET_STRIPES ; bit 2 promotes Color to Windows in one resolve
    jc .windows_refused
    jmp short .windows_live
.windows_refused:
    mov al, [tv_stripes]          ; the mode setter fell back to Color
    mov [tv_live_style], al
    xor ah, ah
    mov [tv_styles+OS88UI_RD_SEL], ax
    jmp .out
.windows_live:
    mov byte [tv_live_style], TV_STY_WIN
    jmp .out
.classic_toggle:
    mov al, [tv_classic_box+OS88UI_CK_ON]
    mov [tv_classic], al
    call tv_stylebits_get
    call OSAPI_THM_SET_STRIPES
    jmp .out
.list:
    mov ax, dx
    sub ax, bp
    sub ax, TV_L0Y
    jl .btn
    cmp ax, TV_NPRESET*TV_LROWH
    jge .btn
    mov bl, TV_LROWH
    div bl                        ; AL = the row, AH = how far into it (the
                                   ; whole row selects, like the Theme page's
                                   ; own radio bands)
    cmp al, [tv_sel]
    jne .new_preset
    cmp byte [tv_live_style], TV_STY_WIN
    jne .out                      ; same row and already Color
.new_preset:
    mov [tv_sel], al
    cmp byte [tv_live_style], TV_STY_WIN
    jne .color_preset
    mov al, [tv_sel]
    call OSAPI_THM_SET_PRESET   ; stage the new row without resolving old colors
    call tv_stylebits_get        ; persist Windows, then resolve the new row once
    call OSAPI_THM_SET_STRIPES
    jnc .out
    mov al, [tv_stripes]         ; unavailable module: the kernel fell back
    mov [tv_live_style], al      ; to Color and cleared the Windows style bit
    xor ah, ah
    mov [tv_styles+OS88UI_RD_SEL], ax
    jmp short .out
.color_preset:
    mov al, [tv_sel]
    call OSAPI_THM_SET_PRESET
    ; The setter posts one complete repaint in the same UI pass. Painting
    ; all rows here duplicates that work and leaves old highlight tails.
    jmp short .out

.btn:                             ; --- a button? THE PRESS ONLY ARMS
    call tv_bhit
    or al, al
    jz .out
    xor ah, ah
    call os88ui_arm
    call tv_setdown
.out:
    jmp os88_r_bp

; Return the persisted style byte expected by the kernel API.
tv_stylebits_get:
    mov al, [tv_classic]
    shl al, 1
    or al, [tv_stripes]
    cmp byte [tv_styles+OS88UI_RD_SEL], TV_STY_WIN
    jne .out
    or al, 4
.out:
    ret

; -----------------------------------------------------------------------------
; tv_bhit - which button is the SCREEN point (CX, DX) in? AL = 0 for neither
; in:  CX = x, DX = y; DI = content left, BP = content top (tv_layout's)
; -----------------------------------------------------------------------------
tv_bhit:
    push bx
    push cx
    push dx
    push si
    mov ax, dx
    sub ax, bp
    cmp ax, TV_BY
    jl .none
    cmp ax, TV_BY + TV_BH
    jge .none
    mov si, cx
    sub si, di
    xor cx, cx
.b:
    mov bx, cx
    add bx, bx
    mov ax, [bx+tv_btnx]
    cmp si, ax
    jl .next
    add ax, TV_BW
    cmp si, ax
    jl .hit
.next:
    inc cx
    cmp cx, TV_NBTN
    jb .b
.none:
    xor al, al
    jmp short .out
.hit:
    mov ax, cx
    inc al                       ; the ids are the table's index plus one
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; tv_setdown - AL = the button drawn PRESSED (0 = neither)
; -----------------------------------------------------------------------------
tv_setdown:
    call os88_ent
    cmp al, [tv_down]
    je .out
    mov [tv_down], al
    call tv_layout
    call tv_btns
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_up - W_ONMOUSEUP (SPEC.md 13.7): Apply and Cancel fire HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = the window
; -----------------------------------------------------------------------------
tv_up:
    call os88_ent
    call os88ui_fire              ; AX = what the press armed, cleared
    or ax, ax
    jz .out
    mov [tv_armed], al
    call tv_layout                ; DI/BP set; CX/DX still the release's own
    call tv_bhit                  ; ...against a FRESH hit at the release
    mov bl, [tv_armed]
    cmp al, bl
    jne .slid
    mov byte [tv_down], 0
    cmp al, TV_CAPPLY
    je .shut                      ; already live: nothing left to commit
    call tv_cancel
.shut:
    call tv_kill
    jmp short .out
.slid:
    xor al, al                   ; slid off: thought better of, nothing fires
    call tv_setdown
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_drag - W_ONDRAG (SPEC.md 13.8.2): keep what is drawn pressed equal to
; what would fire. The same question tv_up asks, one pass early.
; -----------------------------------------------------------------------------
tv_drag:
    call os88_ent
    call os88ui_armed            ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov bl, al
    call tv_layout
    call tv_bhit
    cmp al, bl
    je .set
    xor al, al
.set:
    call tv_setdown
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_key - W_ONKEY: AL = ascii, AH = scan, SI = the window
; Escape cancels, Return applies - the same two doors the buttons are.
; -----------------------------------------------------------------------------
tv_key:
    call os88_ent
    cmp al, 27
    je .esc
    cmp al, 13
    je .ret
    jmp short .out
.ret:
    call tv_kill
    jmp short .out
.esc:
    call tv_cancel
    call tv_kill
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_btns / tv_btn1 - the two buttons, drawn to follow [tv_down]
; in:  DI = content left, BP = content top; lock held
; -----------------------------------------------------------------------------
tv_btns:
    push ax
    push bx
    push cx
    push si
    xor cx, cx
.b:
    push cx
    mov bx, cx
    add bx, bx
    mov ax, [bx+tv_btnx]
    add ax, di
    mov si, [bx+tv_btnl]
    inc cl
    call tv_btn1
    pop cx
    inc cx
    cmp cx, TV_NBTN
    jb .b
    pop si
    pop cx
    pop bx
    pop ax
    ret

; in: AX = its left edge (screen), SI = the label, CL = its control id
tv_btlbl: dw 0
tv_btflg: dw 0
tv_btrec: dw 0, tv_btlbl, tv_btflg, 1, 0, 0, 0, 0

tv_btn1:
    push ax
    push bx
    push dx
    push di
    mov [tv_r + 0], ax
    add ax, TV_BW - 1
    mov [tv_r + 4], ax
    mov ax, bp
    add ax, TV_BY
    mov [tv_r + 2], ax
    add ax, TV_BH - 1
    mov [tv_r + 6], ax
    mov bx, tv_r
    mov di, OS88UI_FILL          ; FILL because a RELEASE redraws this button
    cmp cl, [tv_down]            ; over a PRESSED one; on a full repaint the
    jne .up                      ; content has already been whited
    or di, OS88UI_DOWN
.up:
    push ax
    push bx
    mov [tv_btlbl], si
    mov [tv_btflg], di
    mov [tv_btrec + OS88UI_BT_RECTS], bx
    mov bx, tv_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
    pop di
    pop dx
    pop bx
    pop ax
    ret

tv_r:       dw 0, 0, 0, 0

; --- the shared prologue and return ladder (drivers/os88drv.inc) -----------
    OS88_ENT
    OS88_RET_LADDER bp

; --- the window --------------------------------------------------------------
tv_tpl:     dw TV_X, TV_Y, TV_FW, TV_FH, tv_s_title, tv_paint, tv_key, tv_click
tv_s_title: db 'Color Theme', 0
tv_s_apply: db 'Apply', 0
tv_s_cancel: db 'Cancel', 0
tv_s_style: db 'Title bar style', 0
tv_s_solid: db 'Solid', 0
tv_s_stripes: db 'Stripes', 0
tv_s_windows: db 'Windows', 0
tv_styles: dw 0, 0, 0, 0, tv_style_names, TV_SCOUNT, 0, TV_SPITCH, 0
tv_style_names: dw tv_s_solid, tv_s_stripes, tv_s_windows
tv_s_classic: db '95', 0
TV_CLASSIC_X equ 91
TV_CLASSIC_W equ 83
tv_classic_box: dw 0, 0, 0, 0, tv_s_classic, 0
tv_ostripes: db 0             ; persisted style byte: bit 0 stripes, bit 1 Classic buttons
tv_stripes: db 0
tv_classic: db 0
tv_live_style: db 0

tv_btnx:    dw TV_B0X, TV_B1X
tv_btnl:    dw tv_s_apply, tv_s_cancel
%if ($ - tv_btnl) / 2 != TV_NBTN
%error "thmpal: tv_btnl must have exactly TV_NBTN entries"
%endif

; --- the eight preset names, in kernel/vga12.inc's contiguous thm_tab order
; (SPEC.md 76.14.2) - this file's one piece of knowledge about the palette
; it is naming, and nothing else: the colours themselves are the kernel's.
tv_s_p0:    db 'Classic', 0
tv_s_p1:    db 'Workbench', 0
tv_s_p2:    db 'Light', 0
tv_s_p3:    db 'Forest', 0
tv_s_p4:    db 'Amethyst', 0
tv_s_p5:    db 'Amber', 0
tv_s_p6:    db 'Midnight', 0
tv_s_p7:    db 'Desert', 0

tv_names:   dw tv_s_p0, tv_s_p1, tv_s_p2, tv_s_p3
            dw tv_s_p4, tv_s_p5, tv_s_p6, tv_s_p7
%if ($ - tv_names) / 2 != TV_NPRESET
%error "thmpal: tv_names must have exactly TV_NPRESET entries"
%endif

; --- state --------------------------------------------------------------------
tv_win:     dw 0                ; the window, 0 = none. THE GATE tv_do_busy
                                 ; answers through, same as sv_win
tv_down:    db 0                ; which button is drawn pressed, 0 = none -
                                 ; TV_CAPPLY/TV_CCANCEL start at 1, sc_down's
                                 ; own convention
tv_sel:     db 0                ; the row highlighted right now
tv_okind:   db 0                ; [thm_kind] when the window opened
tv_opreset: db 0                ; [thm_preset] when the window opened
tv_armed:   db 0                ; banked across tv_layout in tv_up (SPEC.md
                                 ; 76.14): os88ui_fire's answer, which BL would
                                 ; otherwise have to survive OSAPI_WM_CONTENT
tv_rowy:    dw 0                ; tv_list_paint's own scratch: this row's top

section .text
    OS88_DRV_END
