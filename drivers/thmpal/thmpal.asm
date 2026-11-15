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
; Panel page. OUR OWNER IS THE CONTROL PANEL: kernel/ctrl.inc's cp_thmv_open,
; in CTRL.DRV's own image, loads it and asks it to open, and the resident
; thmv_reap_x frees it on every close of the panel (kernel/instance.inc).
;
; WHY AN OVERLAY AND NOT AN ON-DEMAND MODULE (SPEC.md 2.8) - SAVER.DRV's own
; header gives the reason word for word: the eight preset rows, their names
; and the two button captions are DATA a Control Panel page cannot afford, and
; an overlay owns a segment, so every byte of it is on the floppy and none of
; it is in the kernel.
;
; OPEN creates or raises the window; BUSY queries its lifetime; CLOSE
; destroys it before the parent releases the image. This matters because the
; window's W_PAINT is a near pointer into this segment, and freeing the
; image under it is a paint into whatever the heap hands out next.
;
; LIVE PREVIEW IS AN INSTALL AND OSAPI_THM_SET (SPEC.md 76.14.14): a change
; copies its row of thmrows.inc over the kernel's ONE Color row, writes the
; preset's number into 'TP' and the style bits into [thm_cstrp], all at
; addresses OPEN was handed, and then asks the kernel to resolve Color - which
; it promotes to Windows when the style bit says so. The kernel keeps no
; preset table; this file and SYSTEM.CFG's 'TP' record are the only holders.
;
; REGISTERS: tv_entry preserves everything but AX and the flags (sv_entry's
; own contract, SPEC.md 79.3).
; =============================================================================

%define OS88_DRV_HIGHOK          ; kern_mega may run this image above 1MB
                                ; (KERN-MEGA-PLAN item 82: no paragraph
                                ; arithmetic on its own segment, no BIOS or
                                ; DMA buffer in it)
%include "os88drv.inc"

THMV_ABI_VER  equ 4              ; MIRRORED in kernel/vga12.inc's thmv_* - the
                                  ; private interface those routines were
                                  ; built against

    OS88_OVERLAY 'THMPAL', THMV_ABI_VER, tv_entry

; --- the verbs, MIRRORED IN kernel/vga12.inc ---------------------------------
THMV_OPEN   equ 0
THMV_BUSY   equ 1
THMV_CLOSE  equ 2

%include "thmpal/thmrows.inc"   ; Color's eight rows (SPEC.md 76.14.14) -
                                  ; the table the kernel does NOT carry
TV_NPRESET  equ THM_PRESET_N     ; selectable rows: the preset IDs
THM_COLOR   equ 2                ; MIRRORED from kernel/vga12.inc's THM_COLOR -
                                  ; this file never touches [thm_kind] itself,
                                  ; only passes this one constant to OSAPI_THM_SET
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
;
; THE SHARED PROLOGUE AND LADDER, which SAVER.DRV's own door cannot take
; because its frame verb arrives on every UI pass: this one's three verbs come
; twice a panel session (driver size pass 1). os88_ent banks AX as well, which
; the contract allows, and neither it nor the pops moves a flag.
; =============================================================================
tv_entry:
    call os88_ent
    push es
    call tv_disp
    jmp os88_r_es

tv_disp:
    cmp al, THMV_BUSY           ; the three verbs in numeric order: one compare
    jb tv_do_open               ; splits OPEN (0) from BUSY (1)
    je tv_do_busy
    cmp al, THMV_CLOSE
    je tv_kill
    stc                          ; a verb from a kernel we were not built with
    ret

; -----------------------------------------------------------------------------
; tv_do_busy - may our image be freed? (sv_do_busy's shape, SPEC.md 79.8)
; out: CF = 1 = no. `neg` sets CF exactly when the window word is not zero
; -----------------------------------------------------------------------------
tv_do_busy:
    mov ax, [tv_win]
    neg ax
    ret

; -----------------------------------------------------------------------------
; tv_do_open - open the window, or bring it to the front if it is already up
; in:  the gfx lock is HELD - this is a Control Panel click (SPEC.md 31.1);
;      ES = KERNEL_SEG; SI = the kernel's Color row, DI = drv_cfg's 'TP'
;      record (its byte 0 is the preset's number), DX = [thm_cstrp]'s
;      address, CH = [thm_kind] - this file has no symbol for any of them
;      (kernel/ctrl.inc's cp_thmv_open is the one caller, SPEC.md 76.14.14)
; out: CF = 0 the window is up; CF = 1 it could not be created
;
; THE OPENING STATE IS BANKED ON A FRESH OPEN ONLY: a second press of
; Customize while the window is up raises it, and banking again there would
; make a preview the thing Cancel goes back to. Opening changes nothing on
; screen, whatever theme is live: Customize is live wherever Color is offered
; (SPEC.md 76.14.10), so it may open over Bright or Dark - the first change
; makes the theme Color, and Cancel puts the banked kind back.
; -----------------------------------------------------------------------------
tv_do_open:
    mov bx, [tv_win]
    or bx, bx
    jnz .show
    mov [tv_row], si
    mov [tv_cfg], di
    mov [tv_cbits], dx
    mov [tv_okind], ch
    mov byte [tv_chg], 0
    mov bx, dx
    mov al, [es:bx]
    and al, 7
    mov [tv_ostripes], al
    mov ah, al
    and al, 1
    mov [tv_stripes], al
    shr ah, 1
    and ah, 1
    mov [tv_classic], ah
    mov [tv_classic_box+OS88UI_CK_ON], ah
    cmp ch, THM_WIN             ; the style radio: Windows when it is live,
    jne .style_ready            ; else Solid or Stripes off bit 0, which AL
    mov al, TV_STY_WIN          ; still is
.style_ready:
    mov [tv_live_style], al
    xor ah, ah
    mov [tv_styles+OS88UI_RD_SEL], ax
    mov al, [es:di]
    cmp al, TV_NPRESET
    jb .selected
    xor al, al                  ; an unknown preset displays Classic
.selected:
    mov [tv_sel], al
    mov [tv_opreset], al
    mov si, tv_tpl
    call OSAPI_WM_CREATE
    jc .no
    mov [tv_win], bx
    xor ax, ax
    mov [tv_down], al
    call os88ui_arm              ; a window reopened mid-gesture must not come
.show:                           ; up with one armed for the next release
    call OSAPI_WM_SHOW
    CLC_OR_STC .no               ; ...and a refusal: nothing was changed yet,
    ret                          ; so there is nothing to undo

; -----------------------------------------------------------------------------
; tv_kill - destroy the window if it exists (sc_kill's shape)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tv_kill:
    push bx
    xor bx, bx                   ; the window, and 0 in its place
    xchg bx, [tv_win]
    or bx, bx
    jz .out
    call OSAPI_WM_DESTROY
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; tv_install - AL = a preset, AH = the style bits: the preset's row over the
; kernel's Color row, its number into 'TP' and the bits into [thm_cstrp].
; NOT a repaint - the caller asks OSAPI_THM_SET for that.
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
tv_install:
    call os88_ent
    push es
    mov bx, KERNEL_SEG
    mov es, bx
    mov bx, [tv_cbits]
    mov [es:bx], ah             ; [thm_cstrp]
    mov di, [tv_cfg]
    mov [es:di], al             ; 'TP' byte 0
    mov ah, THM_PRESET_ROWB
    mul ah
    add ax, tv_rows
    xchg ax, si
    mov di, [tv_row]
    mov cx, THM_PRESET_ROWB
    cld
    rep movsb                   ; DS = ours, ES = the kernel's: no override
    jmp os88_r_es

; -----------------------------------------------------------------------------
; tv_apply - show what the window says: [tv_sel], the style radio, the 95 box.
; ONE install and ONE OSAPI_THM_SET for every change (SPEC.md 76.14.14): the
; kernel promotes Color to Windows when the style bit says so, and falls back
; to Color - clearing the bit - when WIN95.DRV cannot be read, which is the
; refusal this answers by putting the radio back and saying why at the click
; that asked for it (SPEC.md 76.14.10).
; out: nothing (all registers preserved)
;
; The style byte the kernel expects is the 95 box in bit 1, the stripes in
; bit 0 and Windows in bit 2 (tv_stylebits_get, with this one caller).
; -----------------------------------------------------------------------------
tv_apply:
    push ax
    mov ah, [tv_classic]
    shl ah, 1
    or ah, [tv_stripes]
    cmp byte [tv_styles+OS88UI_RD_SEL], TV_STY_WIN
    jne .bits
    or ah, 4
.bits:
    mov al, [tv_sel]
    call tv_install
    mov byte [tv_chg], 1
    mov al, THM_COLOR
    call OSAPI_THM_SET
    jnc .live
    mov al, [tv_stripes]        ; Windows refused: the kernel fell back to
    xor ah, ah                  ; Color and cleared the style bit
    mov [tv_styles+OS88UI_RD_SEL], ax
    call tv_nowin
.live:
    mov al, [tv_styles+OS88UI_RD_SEL]
    mov [tv_live_style], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; tv_cancel - put back what was live when the window opened
; out: nothing (all registers preserved)
;
; Nothing is re-set that did not change (SPEC.md 76.14.14): each OSAPI_THM_SET
; is a whole-screen repaint and a settings write owed. A refusal - Windows
; whose WIN95.DRV has gone since - is said out loud, because what stands
; instead (Color) is not what the user went back to.
; -----------------------------------------------------------------------------
tv_cancel:
    push ax
    mov al, 0                   ; the flag read and cleared in one
    xchg al, [tv_chg]
    or al, al
    jz .out
    mov ax, [tv_opreset]        ; AL = the preset, AH = the style bits: the
    call tv_install             ; two bytes are a pair (asserted there)
    mov al, [tv_okind]
    call OSAPI_THM_SET
    jnc .out
    call tv_nowin
.out:
    pop ax
    ret

; tv_nowin - toast that WIN95.DRV could not be read; all registers preserved
tv_nowin:
    push cx
    push si
    push es
    push ds
    pop es
    mov si, tv_s_nowin
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; tv_close - W_ONCLOSE: the close box is CANCEL (SPEC.md 76.14.14, SAVER's
; own reading of it, 79.7), and then the window DESTROYS itself and refuses
; (sc_close's shape): a hidden window holding a W_PAINT that names this
; segment is a paint into whatever the heap hands out next once
; thmv_reap_x frees it, so wm_hide is exactly wrong here.
; -----------------------------------------------------------------------------
tv_close:
    call tv_cancel
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
;
; The three painters below are this routine's alone (and tv_btns tv_setdown's
; too, which banks the same way), so none of them banks a register this one
; has already banked: each keeps DI and BP, which the next one reads.
; -----------------------------------------------------------------------------
tv_paint:
    call os88_ent
    call tv_layout
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

; tv_style_paint - the heading, the radio group and the 95 box; keeps DI, BP
; (os88ui_rad and os88ui_chk keep every register, and FONT_RUN does)
tv_style_paint:
    push di
    mov ax, (CWHITE << 8) | CBLACK
    mov si, tv_s_style
    mov cx, di
    add cx, TV_TXX
    mov dx, bp
    add dx, TV_SHY
    call OSAPI_FONT_RUN
    call tv_style_layout
    mov bx, tv_styles
    xor di, di
    call os88ui_rad
    mov bx, tv_classic_box      ; DI is still 0 - live - unless the style is
    cmp byte [tv_styles+OS88UI_RD_SEL], TV_STY_WIN  ; not Windows
    je .classic_draw
    mov di, OS88UI_DIS
.classic_draw:
    call os88ui_chk
    pop di
    ret

; -----------------------------------------------------------------------------
; tv_list_paint - all eight rows, the selected one filled
; in:  DI = content left, BP = content top (screen); gfx lock held
; out: nothing; keeps DI and BP
;
; BX walks the rows' tops a pitch at a time, so no row multiplies.
; -----------------------------------------------------------------------------
tv_list_paint:
    xor cx, cx                   ; CX = row 0..7
    mov bx, bp
    add bx, TV_L0Y               ; BX = this row's top, screen y
.row:
    mov si, cx
    add si, si                   ; word index into tv_names
    mov si, [si+tv_names]
    mov ax, (CWHITE << 8) | CBLACK  ; ink, paper for a plain row
    cmp cl, [tv_sel]
    jne .text
    call OSAPI_SET_COLOR         ; AL = CBLACK, the bar's colour
    push cx                      ; the fill wants AX/BX/CX/DX: bank the loop
    mov ax, di                   ; counter across it
    add ax, TV_LX
    mov cx, ax
    add cx, TV_LW - 1
    mov dx, bx
    add dx, TV_LROWH - 1
    call OSAPI_GFX_FILL
    pop cx
    mov ax, (CBLACK << 8) | CWHITE  ; ink, paper for the text pass below
.text:
    push cx
    mov cx, di
    add cx, TV_TXX
    lea dx, [bx + TV_TXDY]
    call OSAPI_FONT_RUN
    pop cx
    add bx, TV_LROWH
    inc cx
    cmp cx, TV_NPRESET
    jb .row
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
    mov al, [tv_styles+OS88UI_RD_SEL]
    cmp al, TV_STY_WIN
    je .apply                     ; Windows keeps the stripes choice it had
    mov [tv_stripes], al
    jmp short .apply
.classic_toggle:
    mov al, [tv_classic_box+OS88UI_CK_ON]
    mov [tv_classic], al
    jmp short .apply

.btn:                             ; --- a button? THE PRESS ONLY ARMS
    call tv_bhit
    or al, al
    jz .out
    xor ah, ah
    call os88ui_arm
    call tv_setdown
    jmp short .out

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
    je .out                       ; already selected: nothing to do
    mov [tv_sel], al
.apply:
    call tv_apply
    ; OSAPI_THM_SET posts one complete repaint in the same UI pass. Painting
    ; all rows here duplicates that work and leaves old highlight tails.
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_bhit - which button is the SCREEN point (CX, DX) in? AL = 0 for neither
; in:  CX = x, DX = y; DI = content left, BP = content top (tv_layout's)
; out: AL; clobbers AH, CX, DX, SI. BX IS KEPT: tv_up and tv_drag hold the
;      armed control in BL across it (sc_bhit's shape)
; -----------------------------------------------------------------------------
tv_bhit:
    mov ax, dx
    sub ax, bp
    cmp ax, TV_BY
    jl .none
    cmp ax, TV_BY + TV_BH
    jge .none
    sub cx, di
    mov si, tv_btnx
    mov dl, 1                    ; the ids are the table's index plus one
.b:
    lodsw
    cmp cx, ax
    jl .next
    add ax, TV_BW
    cmp cx, ax
    jl .hit
.next:
    inc dx
    cmp dl, TV_NBTN
    jbe .b
.none:
    xor dl, dl
.hit:
    xchg ax, dx
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
;
; os88ui_fire's answer is held in BL across tv_layout and tv_bhit, which both
; keep BX - sc_up's shape, where a byte of memory used to bank it.
; -----------------------------------------------------------------------------
tv_up:
    call os88_ent
    call os88ui_fire              ; AX = what the press armed, cleared
    or ax, ax
    jz .out
    mov bl, al
    call tv_layout                ; DI/BP set; CX/DX still the release's own
    call tv_bhit                  ; ...against a FRESH hit at the release
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
; Escape cancels, Return applies - the same two doors the buttons are, and
; Escape's is Return's with the cancel in front of it.
; -----------------------------------------------------------------------------
tv_key:
    call os88_ent
    cmp al, 13
    je .ret
    cmp al, 27
    jne .out
    call tv_cancel
.ret:
    call tv_kill
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; tv_btns - the two buttons, drawn to follow [tv_down]
; in:  DI = content left, BP = content top; lock held
; out: nothing; clobbers AX, BX, CX, DX, SI - its two callers bank them
;
; sc_btns' shape: one record, its rect tv_r named statically, its label and
; flags staged per button. It was tv_btns calling tv_btn1, its one caller
; (driver size pass 1).
; -----------------------------------------------------------------------------
tv_btns:
    xor bx, bx                   ; BX = the button's index, twice
.b:
    mov ax, [bx+tv_btnx]
    add ax, di
    mov si, [bx+tv_btnl]
    mov [tv_r + 0], ax
    add ax, TV_BW - 1
    mov [tv_r + 4], ax
    mov ax, bp
    add ax, TV_BY
    mov [tv_r + 2], ax
    add ax, TV_BH - 1
    mov [tv_r + 6], ax
    mov cx, bx                   ; the control's id is its index plus one
    shr cx, 1
    inc cx
    mov dx, OS88UI_FILL          ; FILL because a RELEASE redraws this button
    cmp cl, [tv_down]            ; over a PRESSED one; on a full repaint the
    jne .up                      ; content has already been whited
    or dx, OS88UI_DOWN
.up:
    mov [tv_btlbl], si
    mov [tv_btflg], dx
    push bx
    mov bx, tv_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    inc bx
    inc bx
    cmp bx, TV_NBTN * 2
    jb .b
    ret

tv_btlbl: dw 0
tv_btflg: dw 0
tv_btrec: dw tv_r, tv_btlbl, tv_btflg, 1, 0, 0, 0, 0, 0
%if OS88UI_BT_RECTS != 0
 %error "thmpal: tv_btrec names tv_r in its first word, OS88UI_BT_RECTS"
%endif

tv_r:       dw 0, 0, 0, 0

; --- the shared prologue and return ladder (drivers/os88drv.inc) -----------
    OS88_ENT
    OS88_RET_LADDER es

; --- the window --------------------------------------------------------------
tv_tpl:     dw TV_X, TV_Y, TV_FW, TV_FH, tv_s_title, tv_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS tv_cls, 0, PAINT, tv_paint, ONKEY, tv_key, ONCLICK, tv_click, \
               ONCLOSE, tv_close, ONMOUSEUP, tv_up, ONDRAG, tv_drag
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
TOASTSTR tv_s_nowin, 'Could not load WIN95.DRV'
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

tv_rows:    THM_PRESET_ROWS     ; drivers/thmpal/thmrows.inc, the boot's too
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
tv_opreset: db 0                ; 'TP' byte 0 when the window opened...
tv_ostripes: db 0               ; ...and [thm_cstrp] (bits 0-2): a PAIR, which
                                 ; tv_cancel loads as one word
%if tv_ostripes - tv_opreset != 1
 %error "thmpal: tv_cancel loads tv_opreset and tv_ostripes as one word"
%endif
tv_chg:     db 0                ; 1 = something was installed since the open
tv_row:     dw 0                ; the kernel's Color row (OPEN's SI)
tv_cfg:     dw 0                ; drv_cfg's 'TP' record (OPEN's DI)
tv_cbits:   dw 0                ; [thm_cstrp]'s address (OPEN's DX)

section .text
    OS88_DRV_END
