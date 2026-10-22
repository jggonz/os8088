; =============================================================================
; os8088 - apps/install/install.asm
;
; INSTALL (SPEC.md 117): puts os8088 on a hard disk, or upgrades the copy that
; is there, a page at a time - the hard-disk driver switched on, the drives
; found, a partition picked, its files kept or erased, the system disk and
; then the apps disk copied, the machine restarted.
;
; IT HAS NO INSTALL ENGINE OF ITS OWN. Everything below is pages and words;
; every step that touches a disk is the Control Panel's own code, reached
; through two doors:
;
;   OSAPI_DRV_USE (SPEC.md 117.3)  the Drivers page's tick - CTRL.DRV's own
;                                  cdl_load - which brings HDD.DRV up, and
;                                  at the end gives its tool back
;   HDD.DRV's HDV_TOOL (117.4)     HDDTOOL.DRV's Install window, driven from
;                                  here: its device table, its State column,
;                                  its hd_inst_step
;
; So a fix to the installer is a fix to both faces, and an install made from
; either is the same bytes on the disk.
;
; ONE WINDOW, PAGES IN IT. in_build sets what a page shows - a heading, up to
; IN_NL lines, a radio group, three buttons - and in_paint draws it. A page
; change repaints the content once (it is a different picture); a change of
; selection redraws the hint line and the buttons and nothing else. Every
; line is one opaque OSAPI_FONT_RUN (SPEC.md 6.1); the dynamic ones are padded
; to the band, so a partial redraw needs no erase.
;
; THE CHOICES ARE ONE RADIO GROUP (os88ui.inc's, SPEC.md 13.17): the drives,
; the partitions, and keep-or-erase are each a group of one-of-many rows, and
; a row that cannot be chosen is greyed by the group's own bit with the reason
; in its label or under it (SPEC.md 47 rule 5).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Install', in_entry, 1, OS88_HIGH | OS88_HIGHMEM
    OS88_ICON16                     ; an arrow onto a hard disk
    ; --- mask ---
    dw 0x03C0, 0x03C0, 0x03C0, 0x03C0
    dw 0x1FF8, 0x0FF0, 0x07E0, 0x03C0
    dw 0x0180, 0x0000, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0x0000
    ; --- data ---
    dw 0x03C0, 0x03C0, 0x03C0, 0x03C0
    dw 0x1FF8, 0x0FF0, 0x07E0, 0x03C0
    dw 0x0180, 0x0000, 0xFFFF, 0x8001
    dw 0x8019, 0x8001, 0xFFFF, 0x0000
    OS88_ICON16_END

; --- geometry (SPEC.md 117.2) -------------------------------------------------
; CGA's desktop band is the tightest at 640 x 157, so the window is 155 tall
; and its content 136: wm_fit cannot shrink it on any adapter, and these are
; constants rather than another read of OSAPI_VIDEO.
IN_W        equ 346
IN_H        equ 155
IN_CW       equ IN_W - 2            ; 344
IN_CH       equ IN_H - TITLE_H - 1  ; 136
IN_LX       equ 8                   ; text's left edge: on the 8px grid, with
                                    ; WF_SNAP, so every run is single-store
IN_HY       equ 4                   ; the heading
IN_RY       equ 14                  ; the rule under it
IN_L0       equ 20                  ; the first body line...
IN_LP       equ 11                  ; ...and the pitch
IN_NL       equ 8                   ; lines 20..97
IN_CELLS    equ 41                  ; a body line's band, in cells
IN_BY       equ 114                 ; the buttons
IN_BH       equ 16
IN_BW       equ 72                  ; 'Continue' is 64px and needs a margin
IN_BX0      equ 104                 ; Back, Next, Cancel - right-aligned with an
IN_BGAP     equ 8                   ; 8px gap, the last ending at 335
IN_NB       equ 3
IN_RPITCH   equ 12                  ; a radio row: four of them end above the
                                    ; hint line on every page that has both
IN_LOGN     equ 9                   ; THE LOG (SPEC.md 117.4.1): nine lines,
IN_LOGC     equ 41                  ; 41 cells, each one padded opaque run
IN_LOGW     equ IN_LOGC + 1
IN_LOGY     equ 20                  ; the first line's top...
IN_LOGP     equ 10                  ; ...and the pitch, the 2px gaps never drawn
IN_FX0      equ 3                   ; the frame round it, 4px clear each side
IN_FY0      equ IN_LOGY - 2
IN_FX1      equ IN_CW - 4
IN_FY1      equ IN_LOGY + IN_LOGN * IN_LOGP + 1
%if IN_L0 + IN_NL * IN_LP > IN_BY
%error "INSTALL's body lines run into its buttons"
%endif
%if IN_FY1 >= IN_BY - 1
%error "INSTALL's log runs into its buttons"
%endif
%if IN_LX + IN_LOGC * 8 > IN_FX1
%error "INSTALL's log is wider than its frame"
%endif
%if HDI_LINESZ - 1 > IN_LOGC
%error "an engine line is wider than INSTALL's log"
%endif
%if IN_BX0 + IN_NB * IN_BW + (IN_NB - 1) * IN_BGAP > IN_CW - 8
%error "INSTALL's buttons do not fit its content"
%endif

IN_BBACK    equ 0                   ; the three buttons
IN_BNEXT    equ 1
IN_BCANCEL  equ 2

; --- the pages ----------------------------------------------------------------
P_WELCOME   equ 0
P_DRIVE     equ 1
P_SLOT      equ 2
P_MODE      equ 3
P_CONFIRM   equ 4
P_APPS      equ 5
P_DONE      equ 6
P_STOP      equ 7
P_BUSY      equ 8                   ; drawn before a copy, never clicked on

; =============================================================================
; in_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = ours, ES = KERNEL_SEG; gfx lock NOT held
; out: BX = window ptr, CF clear
; =============================================================================
in_entry:
    mov si, in_tpl
    call OSAPI_WM_CREATE
    jc in_ret
    OS88_REGION_MOVABLE_NOKEEP in_ret   ; every byte that outlives a pass is
                                    ; a static and moves with us
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on the 8px grid
    mov si, in_about
    call OSAPI_ABOUT_SET
    mov ax, in_onclose              ; every way of closing comes through here,
    call OSAPI_WM_ONCLOSE           ; which is where the tool is given back
    push bx                         ; (the window: VOL_SYS answers in BL, and
    call OSAPI_VOL_SYS              ; in_build spends BX)
    cmp bl, 2                       ; BL = the volume we booted from
    cmc
    sbb al, al
    neg al                          ; 1 = a hard disk: this is an UPGRADE
    mov [in_sysboot], al
    mov byte [in_page], P_WELCOME
    call in_build
    pop ax                          ; AX = the window
    mov bx, in_btrec
    mov si, in_onup
    mov di, in_ondrag
    mov dx, in_onclick              ; a press that was not a button's: the
    call os88ui_btninit             ; radio rows, or the About card
    xchg ax, bx
    mov ax, in_press                ; ...and OURS goes in front of the
    call OSAPI_WM_ONCLICK           ; library's: see in_press
    clc
in_ret:
    ret

; -----------------------------------------------------------------------------
; in_press - W_ONCLICK: the rects again, from the LIVE content box, and then
; the library's press (os88ui_btnclick, which chains to in_onclick).
; in:  CX/DX = the point (SCREEN), SI = the window; gfx lock held
;
; **A DRAGGED WINDOW IS NOT REPAINTED** (SPEC.md 11.96.12.0): the drag cache
; replays its pixels at the new place and W_PAINT never runs, so the rects
; in_layout wrote at the last paint still name where the window WAS. Reported
; as "Finished - click Next, and Next does nothing" - the window had been
; moved since the page was drawn, and minimizing and restoring it was the
; repaint that put the rects back. The library now moves a record's rects
; with a dragged window by itself (SPEC.md 20.5.1.3.5), so for the BUTTONS
; this is belt and braces; it stays for the RADIO rows, whose rect
; (in_rad's OS88UI_RD_RECT) is ours and which the library cannot see.
; -----------------------------------------------------------------------------
in_press:
    push cx
    push dx
    mov bx, si
    call in_layout                  ; clobbers AX, CX, DX, DI
    pop dx
    pop cx
    jmp os88ui_btnclick

; =============================================================================
; DRAWING
; =============================================================================

; -----------------------------------------------------------------------------
; in_paint - W_PAINT: the page as in_build left it, the About card last
; in:  SI = the window; content white-filled, gfx lock held
; out: SI kept; AX, BX, CX, DX, DI clobbered
; -----------------------------------------------------------------------------
in_paint:
    push si
    mov bx, si
    call in_layout                  ; the rects, from the content origin
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, IN_LX
    push dx
    add dx, IN_HY
    mov si, [in_head]
    call in_ink
    call in_run
    pop dx
    push dx                         ; --- the rule under the heading ---------
    mov bx, dx
    add bx, IN_RY
    mov dx, bx
    mov ax, cx
    push cx
    add cx, IN_CW - 2 * IN_LX - 1
    call OSAPI_GFX_FILL             ; the pen is black: in_ink set it
    pop cx
    pop dx
    add dx, IN_L0                   ; --- the body ----------------------------
    mov si, in_lines
    mov di, IN_NL
.line:
    push si
    mov si, [si]
    or si, si
    jz .blank
    call in_run
.blank:
    pop si
    inc si
    inc si
    add dx, IN_LP
    dec di
    jnz .line
    cmp byte [in_page], P_BUSY          ; --- the log -------------------------
    jne .rad
    call in_logbox
.rad:
    cmp word [in_rad+OS88UI_RD_N], 0    ; --- the choices ---------------------
    je .btns
    mov bx, in_rad
    xor di, di
    call os88ui_rad
.btns:
    call in_btns                    ; --- the buttons -------------------------
    pop si
    cmp byte [in_abon], 0           ; ...and the About card LAST
    je .out
    mov bx, si
    mov si, in_ablines
    call os88ui_about_d
    mov si, bx
.out:
    ret

; in_ink - the pen live and black (a greyed control may have left it
; disabled, SPEC.md 47 rule 1). Keeps every register
in_ink:
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    ret

; in_run - SI = a NUL string at CX/DX, black on white, one opaque run
in_run:
    push ax
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop ax
    ret

; -----------------------------------------------------------------------------
; in_layout - the buttons' and the radio group's SCREEN rects, from the content
; origin, every paint AND every press (in_press: a dragged window is replayed,
; not painted)
; in:  BX = the window. out: nothing; clobbers AX, CX, DX, DI
; -----------------------------------------------------------------------------
in_layout:
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    push ax
    push dx
    add ax, IN_BX0
    add dx, IN_BY
    mov cx, dx
    add cx, IN_BH - 1
    mov di, in_rects
.rect:
    mov [di+0], ax
    mov [di+2], dx
    add ax, IN_BW - 1
    mov [di+4], ax
    mov [di+6], cx
    add ax, IN_BGAP + 1
    add di, 8
    cmp di, in_rects + 8 * IN_NB
    jb .rect
    pop dx
    pop ax
    add ax, IN_LX                   ; the radio group: its rows from in_rady,
    mov [in_rad+OS88UI_RD_RECT+0], ax   ; the band to the right margin
    add ax, IN_CW - 2 * IN_LX - 1
    mov [in_rad+OS88UI_RD_RECT+4], ax
    add dx, [in_rady]
    mov [in_rad+OS88UI_RD_RECT+2], dx
    mov ax, [in_rad+OS88UI_RD_N]
    mov cx, IN_RPITCH
    push dx
    mul cx
    pop dx
    add dx, ax
    dec dx
    mov [in_rad+OS88UI_RD_RECT+6], dx
    ret

; -----------------------------------------------------------------------------
; in_btns - the three buttons, greying from in_bok (SPEC.md 47 rule 2: the
; one predicate the release refuses on too). Clobbers AX, BX, DI
; -----------------------------------------------------------------------------
in_btns:
    mov di, in_bflags
    xor ax, ax
.flag:
    mov bx, OS88UI_FILL
    call in_bok
    jnc .live
    or bx, OS88UI_DIS
    jmp short .put
.live:
    cmp al, IN_BNEXT                ; the live Next is the default: Enter
    jne .put
    or bx, OS88UI_DEF
.put:
    mov [di], bx
    inc di
    inc di
    inc ax
    cmp al, IN_NB
    jb .flag
    mov bx, in_btrec
    mov al, 1
.btn:
    call os88ui_btn
    inc ax
    cmp al, IN_NB + 1
    jb .btn
    ret

; -----------------------------------------------------------------------------
; in_bok - would this button do anything? CF = 1 no. AX kept
; in:  AL = IN_B*
; -----------------------------------------------------------------------------
in_bok:
    push ax                         ; bit n of in_bdis greys button n, and
    push cx                         ; in_build is its only writer
    mov cl, al
    inc cx
    mov al, [in_bdis]
    shr al, cl                      ; bit n is the last one shifted out: CF
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; in_redraw - the whole content again, from a handler (not W_PAINT): arm the
; clip, erase, paint. A page change is a different picture, so this is the
; one place a fill precedes the text - exactly what a W_PAINT is.
; in:  SI = the window. clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
in_redraw:
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; CF = 1: nothing of it is visible
    jc .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, IN_CW - 1
    add dx, IN_CH - 1
    call OSAPI_GFX_FILL
    call in_paint
.out:
    ret

; -----------------------------------------------------------------------------
; in_hint - redraw the HINT LINE and the buttons alone, after a selection
; moved: the only two things a selection changes (PERFORMANCE.md rule 1). The
; hint is padded to the band by in_build, so it is its own erase.
; in:  SI = the window, the clip armed. clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
in_hint:
    mov bx, si
    call in_layout
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, IN_LX
    mov ax, [in_hline]              ; which body line is the hint
    mov bl, IN_LP
    mul bl
    add ax, IN_L0
    add dx, ax
    mov bx, [in_hline]
    shl bx, 1
    push si
    mov si, [in_lines+bx]
    or si, si
    jz .nohint
    call in_ink
    call in_run
.nohint:
    pop si
    jmp in_btns

; =============================================================================
; THE PAGES
; =============================================================================

; -----------------------------------------------------------------------------
; in_build - what [in_page] shows: heading, lines, choices, button labels and
; greying. Pure: no drawing and no disk. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
in_build:
    mov di, in_lines                ; nothing, to start
    xor ax, ax
    mov cx, IN_NL
.clr:
    mov [di], ax
    inc di
    inc di
    loop .clr
    mov [in_rad+OS88UI_RD_N], ax
    mov [in_rad+OS88UI_RD_DIS], ax
    mov word [in_hline], IN_NL - 1
    mov word [in_blbl+0], in_s_back
    mov word [in_blbl+2], in_s_next
    mov word [in_blbl+4], in_s_cancel
    mov byte [in_bdis], 0
    mov bl, [in_page]
    xor bh, bh
    shl bx, 1
    jmp [in_ptab+bx]

in_ptab:
    dw in_b_welcome, in_b_drive, in_b_slot, in_b_mode
    dw in_b_confirm, in_b_apps, in_b_done, in_b_stop, in_b_busy

; in_text - copy a page's fixed lines into in_lines from SI (a 0-ended table of
; [line index, string] byte/word pairs). clobbers AX, BX, SI
in_text:
    lodsb
    cmp al, 0xFF
    je .out
    xor ah, ah
    mov bx, ax
    shl bx, 1
    lodsw
    mov [in_lines+bx], ax
    jmp short in_text
.out:
    ret

in_b_welcome:
    mov si, in_t_welcome
    mov word [in_head], in_h_welcome
    cmp byte [in_sysboot], 0
    je .fl
    mov si, in_t_upgrade
    mov word [in_head], in_h_upgrade
.fl:
    or byte [in_bdis], 1 << IN_BBACK
    jmp in_text

; --- the drive: the driver's state, then the devices ------------------------
in_b_drive:
    mov word [in_head], in_h_drive
    mov al, [in_drvst]              ; line 0: the driver
    mov si, in_s_drvgo
    cmp al, 3
    jne .st
    mov [in_lines+0], si            ; switching on: that, and nothing to click
    mov byte [in_bdis], 7
    ret
.st:
    mov si, in_s_drvon
    cmp al, 1
    jb .l0
    mov si, in_s_drvnow
    je .l0
    mov si, in_s_drvbad
.l0:
    mov [in_lines+0], si
    cmp al, 2
    jne .devs
    mov al, [in_drverr]             ; ...and why not, under it
    call in_drvwhy
    mov [in_lines+2], si
    mov word [in_lines+6], in_s_drvcp
    or byte [in_bdis], 1 << IN_BNEXT
    ret
.devs:
    mov si, in_s_nodev
    mov cl, [in_ndev]
    or cl, cl
    jnz .some
    mov [in_lines+2], si
    mov word [in_lines+4], in_s_nodev2
    or byte [in_bdis], 1 << IN_BNEXT
    ret
.some:
    mov si, in_s_onedev
    cmp cl, 1
    je .cnt
    mov si, in_s_devs
.cnt:
    mov [in_lines+2], si
    mov word [in_rady], IN_L0 + 2 * IN_LP
    xor ch, ch
    mov [in_rad+OS88UI_RD_N], cx
    xor bx, bx                      ; BX = the device, its label built and its
.dev:                               ; row greyed when its geometry is unknown
    call in_devlabel
    inc bx
    cmp bx, cx
    jb .dev
    mov ax, [in_dev]
    mov [in_rad+OS88UI_RD_SEL], ax
    mov word [in_hline], 7
    mov word [in_lines+14], in_s_blank
    mov bx, ax
    call in_devok
    jnc .ok
    mov word [in_lines+14], in_s_nogeom
    or byte [in_bdis], 1 << IN_BNEXT
.ok:
    ret

; --- the partition --------------------------------------------------------
in_b_slot:
    mov word [in_head], in_h_slot
    mov word [in_lines+0], in_lb0       ; 'Disk 1, 31 MB. Pick a partition:'
    mov word [in_rady], IN_L0 + IN_LP + 2
    mov word [in_rad+OS88UI_RD_N], 4
    xor bx, bx
.slot:
    call in_slotlabel               ; and greys an unusable one
    inc bx
    cmp bx, 4
    jb .slot
    mov ax, [in_slot]
    mov [in_rad+OS88UI_RD_SEL], ax
    mov word [in_hline], 6
    call in_slothint                ; SI = the hint, CF = 1 Next is greyed
    jnc .ok
    or byte [in_bdis], 1 << IN_BNEXT
.ok:
    mov [in_lines+12], si
    ret

; --- keep or erase --------------------------------------------------------
in_b_mode:
    mov word [in_head], in_h_mode
    mov word [in_lines+0], in_s_mode0
    mov word [in_rady], IN_L0 + IN_LP + 2
    mov word [in_rad+OS88UI_RD_N], 2
    mov word [in_items+0], in_s_keep
    mov word [in_items+2], in_s_erase
    call in_modedis                 ; AX = the greyed bits
    mov [in_rad+OS88UI_RD_DIS], ax
    mov al, [in_wipe]
    xor ah, ah
    mov [in_rad+OS88UI_RD_SEL], ax
    mov word [in_hline], 6
    call in_modehint                ; lines 5, 6 and 7
    ret

; --- confirm --------------------------------------------------------------
in_b_confirm:
    mov word [in_head], in_h_confirm
    mov si, in_t_confirm
    call in_text
    mov word [in_lines+2], in_lb1   ; built: '  disk 1, slot 1 (31 MB, C:),'
    mov si, in_s_cfkeep
    cmp byte [in_wipe], 0
    je .k
    mov si, in_s_cferase
.k:
    mov [in_lines+4], si
    mov word [in_blbl+2], in_s_install
    ret

; --- the copy, about to start (or the apps phase) -------------------------
in_b_busy:
    mov ax, [in_lhead]              ; the verb has answered: how it ended, and
    cmp byte [in_logend], 0         ; Next goes on to the page it named - the
    je .run                         ; end of the log stays until it is read
    mov [in_head], ax
    mov byte [in_bdis], (1 << IN_BBACK) | (1 << IN_BCANCEL)
    ret
.run:
    mov word [in_head], in_h_busy
    cmp byte [in_inapps], 0
    je .sys
    mov word [in_head], in_h_busy1
.sys:
    mov byte [in_bdis], 7           ; all three greyed: the machine is busy
    ret

; --- the apps disk --------------------------------------------------------
in_b_apps:
    mov word [in_head], in_h_apps
    mov si, in_t_apps
    call in_text
    mov word [in_blbl+2], in_s_cont
    mov word [in_blbl+4], in_s_skip
    or byte [in_bdis], 1 << IN_BBACK
    ret

; --- installed ------------------------------------------------------------
in_b_done:
    mov word [in_head], in_h_done
    mov si, in_t_done
    call in_text
    cmp byte [in_skip], 0
    je .all
    mov word [in_lines+12], in_s_skipped
.all:
    mov word [in_blbl+2], in_s_restart
    mov word [in_blbl+4], in_s_close
    or byte [in_bdis], 1 << IN_BBACK
    ret

; --- stopped --------------------------------------------------------------
in_b_stop:
    mov word [in_head], in_h_stop
    mov word [in_lines+0], in_s_stop0
    mov word [in_lines+2], in_cap0      ; '  ' + the engine's own caption
    mov si, in_s_stopsys
    cmp byte [in_inapps], 0
    je .sys
    mov si, in_s_stopapp
.sys:
    mov [in_lines+6], si
    cmp si, in_s_stopapp
    jne .one
    mov word [in_lines+8], in_s_stop2
.one:
    mov word [in_lines+12], in_s_stopback
    mov word [in_blbl+4], in_s_close
    or byte [in_bdis], 1 << IN_BNEXT
    ret

; =============================================================================
; THE LABELS AND HINTS
; =============================================================================

; in_drvwhy - AL = a DRVE_* (or 0) -> SI = its words
in_drvwhy:
    cmp al, IN_NWHY
    jb .k
    mov al, 0
.k:
    xor ah, ah
    mov si, ax
    shl si, 1
    mov si, [in_whytab+si]
    ret

; in_devok - BX = a device: CF = 1 its geometry is unknown (or no such device)
in_devok:
    cmp bl, [in_ndev]
    cmc
    jc .out
    push bx
    shl bx, 1
    shl bx, 1
    shl bx, 1                       ; * HDIV_SIZE
    cmp byte [in_devs+bx+HDIV_GEOM], 1
    pop bx
.out:
    ret

; -----------------------------------------------------------------------------
; in_devlabel - BX = a device: its radio row ('Disk 1  31 MB  BIOS drive 80h')
; into in_ib + BX * IN_IBSZ, and its grey bit when unusable. Keeps BX, CX
; -----------------------------------------------------------------------------
in_devlabel:
    push cx
    mov di, bx
    mov ax, IN_IBSZ
    mul di
    add ax, in_ib
    mov di, ax
    push bx
    shl bx, 1
    mov [in_items+bx], di
    pop bx
    mov si, in_s_disk
    call in_cat
    lea ax, [bx+1]
    call dec_u16
    mov si, in_s_sp2
    call in_cat
    push bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    lea si, [in_devs+bx]
    pop bx
    cmp byte [si+HDIV_GEOM], 1
    jne .unk
    mov ax, [si+HDIV_SECS]
    mov dx, [si+HDIV_SECS+2]
    call in_size                    ; '31 MB'
    mov si, in_s_sp2
    call in_cat
    push bx
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov al, [in_devs+bx+HDIV_KIND]
    mov ah, [in_devs+bx+HDIV_BIOS]
    mov cl, [in_devs+bx+HDIV_UNIT]
    pop bx
    cmp al, 2                       ; the IDE task file...
    jne .bios
    mov si, in_s_idec               ; ...'IDE, ' and then the ROM's name for
    call in_cat                     ; it if the BIOS knows it too (52.1.1)
    or ah, ah
    jnz .bios
    mov si, in_s_ide                ; 'IDE unit 0' when it does not
    call in_cat
    mov al, cl
    xor ah, ah
    call dec_u16
    jmp short .end
.bios:
    mov si, in_s_bios
    call in_cat
    mov al, ah
    call in_hex2                    ; '80h'
    mov byte [di], 'h'
    inc di
    jmp short .end
.unk:
    mov si, in_s_unk                ; 'size unknown', greyed
    call in_cat
    mov ax, 1
    mov cl, bl
    shl ax, cl
    or [in_rad+OS88UI_RD_DIS], ax
.end:
    mov byte [di], 0
    pop cx
    ret

; -----------------------------------------------------------------------------
; in_slotlabel - BX = a slot: 'Slot 1  31 MB  C: FAT16' (the State column is
; the Install window's own words, HDIS_TEXT) into its item buffer; greyed
; unless an install may take it (HDS_OK, or HDS_LIVE kept). Keeps BX
; -----------------------------------------------------------------------------
in_slotlabel:
    mov ax, IN_IBSZ
    mul bx
    add ax, in_ib
    mov di, ax
    push bx
    shl bx, 1
    mov [in_items+bx], di
    pop bx
    mov si, in_s_slotn
    call in_cat
    lea ax, [bx+1]
    call dec_u16
    mov si, in_s_sp2
    call in_cat
    call in_srec                    ; SI = the slot's HDIS_* record
    mov ax, [si+HDIS_SECS]
    mov dx, [si+HDIS_SECS+2]
    mov cx, ax
    or cx, dx
    jz .nosize
    push si
    call in_size
    pop si
    jmp short .st
.nosize:
    mov byte [di], '-'
    inc di
.st:
    push si
    mov si, in_s_sp2
    call in_cat
    pop si
    push si
    add si, HDIS_TEXT
    call in_cat
    pop si
    mov byte [di], 0
    call in_slotuse                 ; CF = 1: not a target
    jnc .ok
    mov ax, 1
    mov cl, bl
    shl ax, cl
    or [in_rad+OS88UI_RD_DIS], ax
.ok:
    ret

; in_srec - SI = slot BX's HDIS_* record. Keeps every other register
in_srec:
    push ax
    mov ax, HDIS_SIZE
    mul bl
    add ax, in_slots
    mov si, ax
    pop ax
    ret

; in_slotuse - BX = a slot: CF = 0 an install may take it. Keeps all
in_slotuse:
    push si
    call in_srec
    cmp byte [si+HDIS_STATE], HDS_OK
    je .yes
    cmp byte [si+HDIS_STATE], HDS_LIVE  ; the booted-from volume: KEPT, never
    je .yes                             ; erased (SPEC.md 52.10.15)
    pop si
    stc
    ret
.yes:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; in_slothint - the selected slot's hint into in_lb2 (padded), SI = it;
; CF = 1 Next is greyed
; -----------------------------------------------------------------------------
in_slothint:
    mov bx, [in_slot]
    mov si, in_s_hbad
    call in_slotuse
    jc .put
    mov si, in_s_hos                ; os8088 is on it already
    call in_hasos
    jnz .putok
    push si
    call in_srec
    mov al, [si+HDIS_VOL]
    mov ah, [si+HDIS_TYPE]
    pop si
    mov si, in_s_hfiles             ; a volume with files on it
    or al, al
    jnz .putok
    mov si, in_s_hfree              ; free space: the install makes the entry
    or ah, ah
    jz .putok
    mov si, in_s_hfmt               ; a FAT entry with nothing in it
.putok:
    clc
.put:
    pushf
    mov di, in_lb2
    call in_cat
    call in_pad
    mov si, in_lb2
    popf
    ret

; in_hasos - ZF = 0 slot BX carries KERNEL.SYS (in_osmask bit BX). Keeps all
in_hasos:
    push ax
    push cx
    mov al, 1
    mov cl, bl
    shl al, cl
    test [in_osmask], al
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; in_modedis - AX = the keep/erase group's grey bits for the selected slot
; (SPEC.md 117.5): nothing to keep greys Keep; the booted-from volume greys
; Erase. Keeps the rest
; -----------------------------------------------------------------------------
in_modedis:
    push bx
    push si
    mov bx, [in_slot]
    call in_srec
    xor ax, ax
    cmp byte [si+HDIS_VOL], 0
    jne .kept
    or al, 1                        ; bit 0: Keep
.kept:
    cmp byte [si+HDIS_STATE], HDS_LIVE
    jne .out
    or al, 2                        ; bit 1: Erase
.out:
    pop si
    pop bx
    ret

; in_modehint - lines 5..7 for the keep/erase choice in [in_wipe]
in_modehint:
    mov si, in_s_mkeep1
    mov di, in_s_mkeep2
    cmp byte [in_wipe], 0
    je .k
    mov si, in_s_merase1
    mov di, in_s_merase2
.k:
    mov [in_lines+10], si
    mov [in_lines+12], di           ; (the hint line, 6: padded strings both)
    call in_modedis
    mov si, in_s_blank
    test al, 1
    jz .nolive
    mov si, in_s_mnokeep
.nolive:
    test al, 2
    jz .p
    mov si, in_s_mlive
.p:
    mov [in_lines+14], si
    ret

; =============================================================================
; THE ACTIONS
; =============================================================================

; -----------------------------------------------------------------------------
; in_onup - W_ONMOUSEUP: a button fires here (SPEC.md 13.7)
; in:  CX/DX = the point (SCREEN), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
in_onup:
    mov bx, in_btrec
    call os88ui_btnup               ; AX = what FIRED, + 1; 0 = cancelled
    dec ax
    js in_ret2
    ; ...and on into in_fire

; in_fire - AL = an IN_B*, SI = the window: do what that button says
in_fire:
    call in_bok
    jc in_ret2                      ; greyed: it explains itself (47 rule 6)
    cmp al, IN_BBACK
    je in_back
    cmp al, IN_BNEXT
    je in_next
    ; --- Cancel, Skip or Close ---
    cmp byte [in_page], P_APPS
    je in_skipapps
    call in_release
    mov bx, si
    call OSAPI_WM_CLOSE             ; DEFERRED, and the negotiator is not asked
in_ret2:                            ; again: the release above is what it would
    ret                             ; have done

; in_go - [in_page] = AL: build it and draw it. SI = the window
in_go:
    mov [in_page], al
    push si
    call in_build
    pop si
    jmp in_redraw

in_back:
    mov al, [in_page]
    cmp al, P_STOP
    jne .prev
    mov byte [in_inapps], 0
    jmp in_toslot                   ; Back from a stop: the partitions, fresh
.prev:
    dec ax
    jmp short in_go

in_next:
    mov al, [in_page]
    cmp al, P_WELCOME
    je in_todrive
    cmp al, P_DRIVE
    je in_toslot
    cmp al, P_SLOT
    je in_tomode
    cmp al, P_MODE
    je .confirm
    cmp al, P_CONFIRM
    je in_install
    cmp al, P_APPS
    je in_apps
    cmp al, P_DONE
    je in_restart
    cmp al, P_BUSY
    je .ended                       ; live only once the verb has answered
    ret
.ended:
    mov byte [in_logend], 0
    mov al, [in_lnext]
    jmp short in_go
.confirm:
    call in_cfline
    mov al, P_CONFIRM
    jmp short in_go

; -----------------------------------------------------------------------------
; in_todrive - switch the driver on and ask for the drives (SPEC.md 117.1)
; -----------------------------------------------------------------------------
in_todrive:
    mov byte [in_drvst], 0
    mov al, DRVC_DISK
    call OSAPI_DRV_CLASSK           ; CF = 0: something of the class is up
    jnc .devs
    mov byte [in_drvst], 3          ; say what is happening before the read:
    mov al, P_DRIVE                 ; it is a floppy load of two files
    call in_go
    mov ax, (DRVU_UP << 8) | DRVC_DISK
    call OSAPI_DRV_USE              ; the Drivers page's tick (117.3)
    mov byte [in_drvst], 1
    jnc .devs
    mov byte [in_drvst], 2
    mov [in_drverr], al
    jmp short .show
.devs:
    mov byte [in_ndev], 0
    mov byte [in_dev], 0
    mov di, in_devs
    mov cl, HDI_DEVS
    call in_tool
    jnc .got
    mov byte [in_drvst], 2          ; the tool would not come in
    mov byte [in_drverr], IN_WHYTOOL
    jmp short .show
.got:
    mov [in_ndev], al
    xor bx, bx                      ; the first usable drive is the pick
.pick:
    cmp bl, [in_ndev]
    jae .show
    call in_devok
    jnc .pk
    inc bx
    jmp short .pick
.pk:
    mov [in_dev], bl
.show:
    mov al, P_DRIVE
    jmp in_go

; -----------------------------------------------------------------------------
; in_toslot - the selected drive's four slots, and which carry os8088
; -----------------------------------------------------------------------------
in_toslot:
    call in_scan
    jc .refused
    mov al, P_SLOT
    jmp in_go
.refused:                           ; the tool would not come in, or the
    push di                         ; Control Panel's Install window is open
    mov byte [in_page], P_DRIVE     ; and owns the engine's state: say so on
    push si                         ; the drive page's hint line
    call in_build
    pop si
    pop di
    mov [in_lines+14], di
    jmp in_redraw

; in_scan - HDI_SLOTS for [in_dev], then the os8088 probe and the default
; selection. CF = 1 refused, DI = the words for why
in_scan:
    push si
    mov di, in_slots
    mov dl, [in_dev]
    mov cl, HDI_SLOTS
    call in_tool
    mov di, in_s_notool2
    jc .bad
    mov di, in_s_cpopen
    cmp al, 0xFD
    je .bad
    mov [in_eslot], al              ; the engine's own pick, 0FFh none
    call in_probe                   ; in_osmask: which carry KERNEL.SYS
    ; --- the default: os8088 found on a usable slot, else the engine's ---
    xor bx, bx
.os:
    call in_hasos
    jz .next
    call in_slotuse
    jnc .pick
.next:
    inc bx
    cmp bx, 4
    jb .os
    mov bl, [in_eslot]
    cmp bl, 4
    jb .pick
    xor bx, bx
.pick:
    mov [in_slot], bx
    call in_slotline                ; line 0: 'Disk 1, 31 MB. Pick a partition'
    pop si
    clc
    ret
.bad:
    pop si
    stc
    ret

; in_slotline - in_lb0 = 'Disk n (size). Pick a partition:'
in_slotline:
    mov di, in_lb0
    mov si, in_s_disk
    call in_cat
    mov ax, [in_dev]
    inc ax
    call dec_u16
    mov si, in_s_open
    call in_cat
    mov bx, [in_dev]
    shl bx, 1
    shl bx, 1
    shl bx, 1
    mov ax, [in_devs+bx+HDIV_SECS]
    mov dx, [in_devs+bx+HDIV_SECS+2]
    call in_size
    mov si, in_s_pick
    call in_cat
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; in_probe - in_osmask bit n = slot n's volume carries KERNEL.SYS in its root
; (SPEC.md 117.5). A size probe: a read with no room answers FERR_BIG off the
; directory entry, with no data I/O. Our own folder is banked and put back.
; -----------------------------------------------------------------------------
in_probe:
    mov byte [in_osmask], 0
    call OSAPI_FILE_HERE            ; DX = our folder, BL = its drive
    mov [in_hdx], dx
    mov [in_hbl], bl
    xor cx, cx                      ; CL = the slot
.slot:
    push cx
    mov bx, cx
    call in_srec
    mov al, [si+HDIS_LET]
    or al, al
    jz .next                        ; not on the desktop: nothing to ask
    sub al, 'A'
    mov bl, al
    xor dx, dx
    call OSAPI_FILE_GOTO_QM         ; its root, quietly - and OUR folder with
    jc .next                        ; it, which is where the read below looks
                                    ; (SPEC.md 19.2.1: GOTO_Q alone is undone
                                    ; by the very next file cell)
    push es
    push ds
    pop es
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, in_s_kernel
    call OSAPI_FILE_READ            ; nothing fits: the size probe
    pop es
    jnc .has                        ; an empty KERNEL.SYS is still one
    cmp ax, FERR_NOENT
    je .next
    cmp ax, FERR_NODISK
    je .next
    cmp ax, FERR_IO
    je .next
.has:
    pop cx
    push cx
    mov al, 1
    shl al, cl
    or [in_osmask], al
.next:
    pop cx
    inc cx
    cmp cx, 4
    jb .slot
    mov dx, [in_hdx]                ; ...and back where we were
    mov bl, [in_hbl]
    call OSAPI_FILE_GOTO_QM
    ret

; --- the mode page: the default follows the slot -------------------------
in_tomode:
    call in_modedis
    mov byte [in_wipe], 0           ; keep, over a volume (52.10.15's default)
    test al, 1
    jz .m
    mov byte [in_wipe], 1           ; nothing to keep: erase it is
.m:
    mov al, P_MODE
    jmp in_go

; in_cfline - in_lb1 = '  disk 1, slot 2 (31 MB, C:),'. Keeps SI (the
; window, which in_go is about to want)
in_cfline:
    push si
    mov di, in_lb1
    mov si, in_s_cfdisk
    call in_cat
    mov ax, [in_dev]
    inc ax
    call dec_u16
    mov si, in_s_cfslot
    call in_cat
    mov ax, [in_slot]
    inc ax
    call dec_u16
    mov si, in_s_open
    call in_cat
    mov bx, [in_slot]
    call in_srec
    mov ax, [si+HDIS_SECS]
    mov dx, [si+HDIS_SECS+2]
    push si
    call in_size
    pop si
    mov al, [si+HDIS_LET]
    or al, al
    jz .nol
    mov si, in_s_comma
    call in_cat
    mov [di], al
    mov byte [di+1], ':'
    inc di
    inc di
.nol:
    mov byte [di], ')'
    mov byte [di+1], ','
    mov byte [di+2], 0
    pop si
    ret

; -----------------------------------------------------------------------------
; in_install - Confirm's Install: the busy page, then HDI_GO (SPEC.md 117.4)
; -----------------------------------------------------------------------------
in_install:
    mov byte [in_inapps], 0
    mov byte [in_skip], 0
    mov byte [in_logn], 0           ; a fresh run, a fresh log
    call in_busy
    mov si, in_s_loff               ; ...whose first line is ours
    call in_onlog
    mov si, [in_win]
    mov dl, [in_dev]
    mov dh, [in_slot]
    mov ch, [in_wipe]
    mov cl, HDI_GO
    jmp short in_stage

in_apps:
    mov byte [in_inapps], 1
    call in_busy                    ; the log goes on where it stopped
    mov cl, HDI_APPS
                                    ; ...and on into in_stage

; in_stage - CL = HDI_GO or HDI_APPS (DL/DH/CH per the verb): run it and go
; to the page its answer names. SI = the window
in_stage:
    push si
    mov di, in_cap
    mov word [di+HDIB_LOG], in_onlog
    call in_tool
    pop si
    jnc .ran
    mov di, in_cap                  ; the tool could not be reached
    push si
    mov si, in_s_notool
    call in_cat
    mov byte [di], 0
    pop si
    xor al, al
.ran:
    mov word [in_lhead], in_h_bdone
    mov bl, P_DONE
    mov di, in_s_ldone
    cmp al, HDG_DONE
    je .end
    mov bl, P_APPS
    mov di, in_s_lapps
    cmp al, HDG_APPS
    je .end
    call in_capline                 ; HDG_FAIL: the engine's own words
    mov word [in_lhead], in_h_bstop
    mov bl, P_STOP
    mov di, in_cap0                 ; '  ' + the caption, to the log as well
.end:
    mov [in_lnext], bl              ; the busy page again, with its ending:
    mov byte [in_logend], 1         ; the last line, the heading, the buttons
    push si
    mov si, di
    call in_onlog
    pop si
    jmp in_ended

; in_busy - draw the busy page NOW, before the call that freezes the machine
in_busy:
    mov [in_win], si                ; the log draws from inside the verb, which
    mov al, P_BUSY                  ; hands it no window (SPEC.md 117.4.1)
    jmp in_go

; -----------------------------------------------------------------------------
; THE LOG (SPEC.md 117.4.1). The engine calls in_onlog once a step, from inside
; HDI_GO / HDI_APPS: DS = ours, ES = KERNEL_SEG, SI = the line, the gfx lock
; held - and it is between two halves of a transfer, so this draws and touches
; nothing else. The lines are kept, padded to the band, so a W_PAINT redraws
; them and every line is its own erase.
; -----------------------------------------------------------------------------

; in_onlog - SI = a line (ours): keep it, then draw it - one run, or one
; scroll and one run once the box is full. Clobbers everything but SI
in_onlog:
    mov al, [in_logn]
    push ax
    call in_logadd
    mov bx, [in_win]
    call OSAPI_WM_CLIP_SET          ; the engine's own drawing (the bar) may
    pop ax                          ; have moved the clip: ours, every line
    jc .out
    call in_ink
    cmp al, IN_LOGN
    jb .one                         ; there was room: the line's own row
    call in_logscroll
    jnc .one
    jmp in_loglines                 ; refused (the window is covered): all nine
.one:
    mov al, [in_logn]
    dec ax
    jmp in_logline
.out:
    ret

; in_logadd - SI = a line: the next row of in_log, padded; the oldest goes
; once there are nine. Keeps SI
in_logadd:
    push si
    cmp byte [in_logn], IN_LOGN
    jb .room
    mov si, in_log + IN_LOGW        ; one row up: ES is the kernel's here, so
    mov di, in_log                  ; no movsb
    mov cx, (IN_LOGN - 1) * IN_LOGW
.up:
    lodsb
    mov [di], al
    inc di
    loop .up
    dec byte [in_logn]
.room:
    mov al, [in_logn]
    mov ah, IN_LOGW
    mul ah
    add ax, in_log
    mov di, ax
    pop si
    push si
    mov cx, IN_LOGC
.cp:
    lodsb
    or al, al
    jz .pad
    mov [di], al
    inc di
    loop .cp
    jmp short .end
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
.end:
    mov byte [di], 0
    inc byte [in_logn]
    pop si
    ret

; in_logbox - W_PAINT's half: the frame and every line kept
in_logbox:
    mov bx, [in_win]
    call in_ink
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    push si
    mov si, ax
    mov bx, dx
    add ax, IN_FX0
    add bx, IN_FY0
    mov cx, si
    add cx, IN_FX1
    add dx, IN_FY1
    call OSAPI_GFX_FRAME
    pop si
    ; ...and on into in_loglines

; in_loglines - every line kept, at its row. Clobbers AX, CX, DX, DI
in_loglines:
    xor ax, ax
.line:
    cmp al, [in_logn]
    jae .out
    call in_logline
    inc ax
    jmp short .line
.out:
    ret

; in_logline - row AL of the log, one opaque run. Keeps AX and SI
in_logline:
    push ax
    push si
    push bx
    mov bx, [in_win]
    push ax
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, IN_LX
    pop ax
    push ax
    mov ah, IN_LOGP
    mul ah
    add dx, ax
    add dx, IN_LOGY
    pop ax
    mov ah, IN_LOGW
    mul ah
    add ax, in_log
    mov si, ax
    call in_run
    pop bx
    pop si
    pop ax
    ret

; in_logscroll - the box's text up one row (OSAPI_GFX_SCROLL): CF = 1 refused
in_logscroll:
    push si
    mov bx, [in_win]
    call OSAPI_WM_CONTENT
    add ax, IN_LX                   ; byte columns at both ends: the content
    mov cx, ax                      ; origin is on the 8px grid (WF_SNAP)
    add cx, IN_LOGC * 8 - 1
    add dx, IN_LOGY
    mov bx, dx
    add dx, IN_LOGN * IN_LOGP - 1
    mov si, IN_LOGP
    call OSAPI_GFX_SCROLL
    pop si
    ret

; in_ended - the verb has answered: the heading and the buttons, which are all
; that changed on this page (PERFORMANCE.md rule 1). SI = the window
in_ended:
    push si
    call in_build
    pop si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call in_ink
    call in_layout
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, IN_LX
    add dx, IN_HY
    push si
    mov si, [in_head]               ; padded to one length: its own erase
    call in_run
    pop si
    jmp in_btns
.out:
    ret

; in_capline - in_cap0 = '  ' + in_cap, the stop page's second line
in_capline:
    push si
    mov di, in_cap0
    mov si, in_s_sp2
    call in_cat
    mov si, in_cap
    call in_cat
    mov byte [di], 0
    pop si
    ret

in_skipapps:
    call in_quit
    mov byte [in_skip], 1
    mov al, P_DONE
    jmp in_go

in_restart:
    call in_release
    mov al, 1                       ; NOT the flushing kind: the page has just
    call OSAPI_REBOOT               ; told the user to take the floppy out
    ret                             ; (SPEC.md 52.10.6.1) - and it is POSTED

; -----------------------------------------------------------------------------
; in_quit / in_release - abandon a run waiting on its apps disk, then give the
; tool back (DRVU_DONE). Both keep every register
; -----------------------------------------------------------------------------
in_quit:
    push ax
    push cx
    mov cl, HDI_QUIT
    call in_tool                    ; CF = 1: no driver - nothing to abandon
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; in_tool - one HDDTOOL.DRV verb (SPEC.md 117.4): CL = an HDI_*, the rest per
; the verb, the buffer at DS:DI. THROUGH OSAPI_DRV_USE's DRVU_CALL and never
; the plain door: the engine walks the machine's files, and asked as us it
; would resolve every name in OUR folder (SPEC.md 51.2.3, 117.3).
; out: CF and AX as the tool answers; keeps every other register
; -----------------------------------------------------------------------------
in_tool:
    push bx
    push es
    push ds
    pop es
    mov bx, (DRVC_DISK << 8) | HDV_TOOL
    mov ah, DRVU_CALL
    call OSAPI_DRV_USE
    pop es
    pop bx
    ret

in_release:
    call in_quit
    push ax
    mov ax, (DRVU_DONE << 8) | DRVC_DISK
    call OSAPI_DRV_USE
    pop ax
    ret

; in_onclose - OSAPI_WM_ONCLOSE: every way out of the window comes here
; out: CF = 0, the close goes ahead
in_onclose:
    call in_release
    clc
    ret

; =============================================================================
; INPUT
; =============================================================================

; in_ondrag - W_ONDRAG: the pressed button follows the pointer
in_ondrag:
    mov bx, in_btrec
    jmp os88ui_btndrag

; -----------------------------------------------------------------------------
; in_onclick - the library's chain for a press that was not a button's: the
; About card's dismissal, else the radio rows
; in:  CX/DX = the point (SCREEN), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
in_onclick:
    cmp byte [in_abon], 0
    je .rad
    mov byte [in_abon], 0           ; the card comes down: one repaint
    jmp in_redraw
.rad:
    cmp word [in_rad+OS88UI_RD_N], 0
    je .out
    mov bx, si
    call OSAPI_WM_CLIP_SET          ; a click dispatch arms no region
    jc .out
    mov bx, in_rad
    call os88ui_radhit              ; CF = 0 ours; ZF = 1 the pick MOVED
    jc .out
    jnz .out
    jmp in_picked
.out:
    ret

; -----------------------------------------------------------------------------
; in_picked - the radio pick moved to [in_rad+RD_SEL]: adopt it, rebuild the
; hint and the greying, redraw those two. SI = the window, clip armed
; -----------------------------------------------------------------------------
in_picked:
    mov ax, [in_rad+OS88UI_RD_SEL]
    mov bl, [in_page]
    cmp bl, P_DRIVE
    jne .slot
    mov [in_dev], al
    jmp short .re
.slot:
    cmp bl, P_SLOT
    jne .mode
    mov [in_slot], ax
    jmp short .re
.mode:
    mov [in_wipe], al
.re:
    push si
    call in_build
    pop si
    cmp byte [in_page], P_MODE      ; the mode page's two explanation lines
    jne .h                          ; change with the pick as well
    mov bx, si
    push si
    call OSAPI_WM_CONTENT
    mov cx, ax
    add cx, IN_LX
    add dx, IN_L0 + 5 * IN_LP
    mov si, [in_lines+10]
    call in_ink
    call in_run
    pop si
.h:
    jmp in_hint

; -----------------------------------------------------------------------------
; in_onkey - W_ONKEY: Enter is the default button, Escape is Cancel, the
; arrows move the pick (SPEC.md 117.2). AL = ASCII, AH = scan, SI = window
; -----------------------------------------------------------------------------
in_onkey:
    cmp byte [in_abon], 0
    je .keys
    mov byte [in_abon], 0           ; any key takes the About card down
    jmp in_redraw
.keys:
    push ax
    mov bx, si
    call OSAPI_WM_CLIP_SET
    pop ax
    jc .out
    cmp al, 13
    je .enter
    cmp al, 27
    je .esc
    cmp ah, 0x48                    ; up
    je .up
    cmp ah, 0x50                    ; down
    je .down
.out:
    ret
.enter:
    mov al, IN_BNEXT
    jmp in_fire
.esc:
    mov al, IN_BCANCEL
    jmp in_fire
.up:
    mov dx, -1
    jmp short .move
.down:
    mov dx, 1
.move:
    mov cx, [in_rad+OS88UI_RD_N]
    jcxz .out
    mov ax, [in_rad+OS88UI_RD_SEL]
.step:                              ; the next row that is not greyed, or none
    add ax, dx
    js .out
    cmp ax, cx
    jae .out
    push cx
    mov cl, al
    mov bx, 1
    shl bx, cl
    pop cx
    test [in_rad+OS88UI_RD_DIS], bx
    jnz .step
    mov [in_rad+OS88UI_RD_SEL], ax
    mov bx, si
    push si
    call in_layout
    mov bx, in_rad
    xor di, di
    call os88ui_rad                 ; the group as it now stands
    pop si
    jmp in_picked

; =============================================================================
; 'About Install' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
in_about:
    mov byte [in_abon], 1
    mov bx, si
    mov si, in_ablines
    jmp os88ui_about

; =============================================================================
; TEXT HELPERS
; =============================================================================

; in_cat - append the NUL string at SI to DI (DI left on the end, no NUL
; written). Keeps everything but DI
in_cat:
    push ax
    push si
.c:
    lodsb
    or al, al
    jz .e
    mov [di], al
    inc di
    jmp short .c
.e:
    pop si
    pop ax
    ret

; in_pad - the string at in_lb2 (ending at DI) padded with spaces to
; IN_CELLS and terminated: an opaque run that is its own erase
in_pad:
    mov ax, in_lb2 + IN_CELLS
.p:
    cmp di, ax
    jae .e
    mov byte [di], ' '
    inc di
    jmp short .p
.e:
    mov byte [di], 0
    ret

; in_size - DX:AX sectors as '31 MB' (or 'nnn KB' under a megabyte) at DI
in_size:
    push ax
    push cx
    push dx
    push si
    or dx, dx
    jnz .mb
    cmp ax, 2048
    jae .mb
    shr ax, 1                       ; KB
    call dec_u16
    mov si, in_s_kb
    jmp short .u
.mb:
    mov cx, 11                      ; / 2048
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    call dec_u16
    mov si, in_s_mb
.u:
    call in_cat
    pop si
    pop dx
    pop cx
    pop ax
    ret

; in_hex2 - AL as two hex digits at DI
in_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .dig
    mov al, ah
    and al, 15
    call .dig
    pop cx
    pop ax
    ret
.dig:
    add al, '0'
    cmp al, '9'
    jbe .w
    add al, 'A' - '9' - 1
.w:
    mov [di], al
    inc di
    ret

; =============================================================================
; DATA
; =============================================================================
in_tpl:     dw 148, 24, IN_W, IN_H
            dw in_ttl, in_paint, in_onkey, 0

in_ttl:     db 'Install os8088', 0

; --- headings ---
in_h_welcome: db 'Install os8088 on a hard disk', 0
in_h_upgrade: db 'Upgrade os8088 on this hard disk', 0
in_h_drive:   db 'Step 1 of 4:  the hard disk', 0
in_h_slot:    db 'Step 2 of 4:  where to put it', 0
in_h_mode:    db 'Step 3 of 4:  keep or erase', 0
in_h_confirm: db 'Step 4 of 4:  ready to install', 0
in_h_busy:    db 'Installing os8088 on the hard disk', 0 ; ONE LENGTH, all four:
in_h_busy1:   db 'Installing the apps disk          ', 0 ; in_ended redraws the
in_h_bdone:   db 'Finished - click Next             ', 0 ; heading over the
in_h_bstop:   db 'It stopped - click Next           ', 0 ; last with no erase
in_h_apps:    db 'Nearly done:  the apps disk', 0
in_h_done:    db 'os8088 is installed', 0
in_h_stop:    db 'The install stopped', 0

; --- fixed lines: [line, string] pairs, 0FFh ends ---
in_t_welcome:
    db 0
    dw in_w0
    db 1
    dw in_w1
    db 3
    dw in_w3
    db 4
    dw in_w4
    db 5
    dw in_w5
    db 7
    dw in_w7
    db 0xFF
in_t_upgrade:
    db 0
    dw in_u0
    db 1
    dw in_u1
    db 2
    dw in_u2
    db 3
    dw in_u3
    db 4
    dw in_u4
    db 6
    dw in_u6
    db 7
    dw in_u7
    db 0xFF
in_t_confirm:
    db 0
    dw in_c0
    db 3
    dw in_c3
    db 5
    dw in_c5
    db 7
    dw in_c7
    db 0xFF
in_t_apps:
    db 0
    dw in_a0
    db 1
    dw in_a1
    db 3
    dw in_a3
    db 4
    dw in_a4
    db 6
    dw in_a6
    db 0xFF
in_t_done:
    db 0
    dw in_d0
    db 2
    dw in_d2
    db 3
    dw in_d3
    db 4
    dw in_d4
    db 0xFF

in_w0:  db 'This puts os8088 on your hard disk, so', 0
in_w1:  db 'the machine starts without a floppy.', 0
in_w3:  db 'You need the system disk - it is in now -', 0
in_w4:  db 'and the apps disk. On an XT the copy', 0
in_w5:  db 'takes several minutes.', 0
in_w7:  db 'Click Next to look for hard disks.', 0
in_u0:  db 'This machine started from its hard disk,', 0
in_u1:  db 'and INSTALL can bring it up to this', 0
in_u2:  db 'release. Put the NEW system disk in a', 0
in_u3:  db 'floppy drive, and the apps disk when it', 0
in_u4:  db 'is asked for.', 0
in_u6:  db 'Your own files and settings are kept.', 0
in_u7:  db 'Click Next to look at the hard disk.', 0
in_c0:  db 'INSTALL is ready. It will use', 0
in_c3:  db '  and copy both floppies onto it.', 0
in_c5:  db 'The machine is busy while it copies.', 0
in_c7:  db 'Click Install to start.', 0
in_a0:  db 'The system is on the hard disk, and the', 0
in_a1:  db 'machine will start from it.', 0
in_a3:  db 'Now take the system disk out of drive A:', 0
in_a4:  db 'put the APPS disk in, and click Continue.', 0
in_a6:  db 'Skip leaves them off the hard disk.', 0
in_d0:  db 'os8088 is installed on the hard disk.', 0
in_d2:  db 'Take the floppy out of the drive, then', 0
in_d3:  db 'click Restart: the machine will start', 0
in_d4:  db 'from the hard disk.', 0

in_s_loff:    db 'Do not switch the machine off.', 0
in_s_ldone:   db 'Finished.', 0
in_s_lapps:   db 'Next: the apps disk.', 0
in_s_cfkeep:  db '  keeping the files on it,', 0
in_s_cferase: db '  ERASING everything on it,', 0
in_s_skipped: db 'The apps disk was not copied.', 0
in_s_stop0:   db 'The install stopped:', 0
in_s_stopsys: db 'The hard disk was not made bootable.', 0
in_s_stopapp: db 'The system part is on and it starts; not', 0
in_s_stop2:   db 'everything else was copied.', 0
in_s_stopback: db 'Back goes to the partition list again.', 0
in_s_notool:  db 'HDDTOOL.DRV could not be read', 0
in_s_notool2: db 'HDDTOOL.DRV could not be read.          ', 0

; --- the drive page ---
in_s_drvon:   db 'The hard disk driver is on.', 0
in_s_drvnow:  db 'The hard disk driver is switched on now.', 0
in_s_drvgo:   db 'Switching on the hard disk driver...', 0
in_s_drvbad:  db 'The hard disk driver did not start:', 0
in_s_drvcp:   db 'Control Panel > Drivers says more.', 0
in_s_nodev:   db 'No hard disk was found.', 0
in_s_nodev2:  db 'Is the controller fitted, and its ROM on?', 0
in_s_onedev:  db 'Found one hard disk:', 0
in_s_devs:    db 'Found these hard disks - pick one:', 0
in_s_nogeom:  db 'Size unknown: set it in Control Panel.', 0
in_s_cpopen:  db 'Close the Control Panel Install window. ', 0
in_s_blank:   db '                                         ', 0
in_s_disk:    db 'Disk ', 0
in_s_slotn:   db 'Slot ', 0
in_s_sp2:     db '  ', 0
in_s_bios:    db 'BIOS drive ', 0
in_s_ide:     db 'unit ', 0
in_s_idec:    db 'IDE, ', 0
in_s_unk:     db 'size unknown', 0
in_s_kb:      db ' KB', 0
in_s_mb:      db ' MB', 0
in_s_open:    db ' (', 0
in_s_pick:    db '). Pick a partition:', 0
in_s_comma:   db ', ', 0
in_s_cfdisk:  db '  disk ', 0
in_s_cfslot:  db ', slot ', 0
in_s_kernel:  db 'KERNEL.SYS', 0

; --- the slot hints (padded at build time) ---
in_s_hbad:    db 'That partition cannot be used.', 0
in_s_hos:     db 'os8088 is on it already: Upgrade it.', 0
in_s_hfiles:  db 'It has files: next, keep or erase them.', 0
in_s_hfree:   db 'Free space: INSTALL makes a partition.', 0
in_s_hfmt:    db 'Not formatted: INSTALL formats it.', 0

; --- the mode page ---
in_s_mode0:   db 'What happens to the files on it?', 0
in_s_keep:    db 'Keep them, and upgrade os8088', 0
in_s_erase:   db 'Erase it and install fresh', 0
in_s_mkeep1:  db 'The system and programs are replaced;   ', 0
in_s_mkeep2:  db 'your own files and settings stay.       ', 0
in_s_merase1: db 'EVERYTHING on the partition is deleted. ', 0
in_s_merase2: db '                                         ', 0
in_s_mnokeep: db '(There is nothing on it to keep.)       ', 0
in_s_mlive:   db '(It is running now: it cannot be erased)', 0

; --- buttons ---
in_s_back:    db 'Back', 0
in_s_next:    db 'Next', 0
in_s_cancel:  db 'Cancel', 0
in_s_install: db 'Install', 0
in_s_cont:    db 'Continue', 0
in_s_skip:    db 'Skip', 0
in_s_restart: db 'Restart', 0
in_s_close:   db 'Close', 0
in_blbl:      dw in_s_back, in_s_next, in_s_cancel

; --- why a driver did not start: DRVE_* (kernel/driver.inc), and two of ours -
in_whytab:
    dw in_e0, in_e1, in_e2, in_e3, in_e4, in_e5, in_e6, in_e7, in_e8, in_etool
IN_NWHY     equ ($ - in_whytab) / 2
IN_WHYTOOL  equ IN_NWHY - 1
in_e0:  db '  this system has no drivers', 0
in_e1:  db '  no system disk in the drive', 0
in_e2:  db '  HDD.DRV is not on the system disk', 0
in_e3:  db '  HDD.DRV on this disk is damaged', 0
in_e4:  db '  not enough memory for it', 0
in_e5:  db '  no hard disk controller answered', 0
in_e6:  db '  another driver has the disk', 0
in_e7:  db '  it was started twice (a bug)', 0
in_e8:  db '  it found nothing to mount', 0
in_etool: db '  HDDTOOL.DRV could not be read', 0

; --- the controls' records ---
    OS88UI_BTNREC in_btrec, in_rects, in_blbl, in_bflags, IN_NB
in_rad:     dw 0, 0, 0, 0           ; OS88UI_RD_RECT, filled by in_layout
            dw in_items             ; OS88UI_RD_ITEMS
            dw 0                    ; OS88UI_RD_N
            dw 0                    ; OS88UI_RD_SEL
            dw IN_RPITCH            ; OS88UI_RD_PITCH
            dw 0                    ; OS88UI_RD_DIS
in_items:   dw 0, 0, 0, 0
in_lines:   times IN_NL dw 0
in_head:    dw 0
in_hline:   dw 0
in_rady:    dw 0
in_dev:     dw 0                    ; the selected device (a word: RD_SEL's)
in_slot:    dw 0                    ; ...and slot

in_ablines:
    dw in_ab1, in_ab2, in_ab3, 0
in_ab1:     db 'Install for os8088', 0
in_ab2:     db 'the Control Panel installer, as a wizard', 0
in_ab3:     db 'Contributed by Jorge Gonzalez', 0

; --- the shared code ----------------------------------------------------------
%define DEC_U16
%include "os88dec.inc"
%define OS88UI_ABOUT
%define OS88UI_RAD
%define OS88UI_NOGCHECK
%include "os88ui.inc"

IN_IBSZ     equ 44
    OS88_BSS IN_BSS
    OS88_IMAGE_END

; --- loader-zeroed bss ----------------------------------------------------------
in_page     equ os88_image_end + 0
in_sysboot  equ os88_image_end + 1
in_drvst    equ os88_image_end + 2  ; 0 was on, 1 switched on, 2 failed
in_drverr   equ os88_image_end + 3
in_ndev     equ os88_image_end + 4
in_wipe     equ os88_image_end + 5
in_osmask   equ os88_image_end + 6
in_skip     equ os88_image_end + 7
in_inapps   equ os88_image_end + 8
in_eslot    equ os88_image_end + 9
in_abon     equ os88_image_end + 10
in_bdis     equ os88_image_end + 11
in_hbl      equ os88_image_end + 12
in_hdx      equ os88_image_end + 14
in_devs     equ os88_image_end + 16             ; 4 * HDIV_SIZE
in_slots    equ in_devs + 4 * HDIV_SIZE         ; 4 * HDIS_SIZE
in_cap      equ in_slots + 4 * HDIS_SIZE        ; HDIB_SIZE: the HDI_GO block
in_cap0     equ in_cap + HDIB_SIZE              ; 2 + HDI_CAPSZ
in_ib       equ in_cap0 + 2 + HDI_CAPSZ         ; 4 * IN_IBSZ
in_lb0      equ in_ib + 4 * IN_IBSZ             ; 48
in_lb1      equ in_lb0 + 48                     ; 48
in_lb2      equ in_lb1 + 48                     ; 48
in_rects    equ in_lb2 + 48                     ; 3 * 8
in_bflags   equ in_rects + 8 * IN_NB            ; 3 * 2
in_win      equ in_bflags + 2 * IN_NB           ; the window, for the log
in_lhead    equ in_win + 2                      ; the busy page's ending...
in_lnext    equ in_lhead + 2                    ; ...and the page Next goes to
in_logend   equ in_lnext + 1                    ; 1 = the verb has answered
in_logn     equ in_logend + 1                   ; lines kept, 0..IN_LOGN
in_log      equ in_logn + 1                     ; IN_LOGN * IN_LOGW
IN_BSS      equ in_log + IN_LOGN * IN_LOGW - os88_image_end
