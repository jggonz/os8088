; =============================================================================
; zipcp.inc - ZIP.DRV's Control Panel page (SPEC.md 31.9, 138.6)
;
;   Zip Drive
;   Port  0378
;   Mode  VPI0 nibble
;   Disk  96 MB
;         Write-protected          (only when it is)
;   [ Eject ]                      (greyed with no disk, SPEC.md 47)
;
; THE DRIVE IS ASKED AT EVERY PAINT (zs_query: three short commands), so the
; page says what is in the drive NOW and not what the volume last learned -
; and a UNIT ATTENTION it sees on the way is not lost: zs_cmd hands it to the
; volume as ZM_CHANGED (SPEC.md 138.4).
;
; THE PANE IS 221 PX WIDE, 27 characters. The longest value line is 'VPI2
; nibble' at x = 40: 40 + 88. Every label is ONE font_run (SPEC.md 6.1), and
; the two lines that change with the disk clear only their TAIL (31.9.3).
; NET.DRV's page (drivers/net/netui.inc) is the worked example of everything
; here, the one button and its gesture included.
; =============================================================================

ZC_W        equ 221             ; the pane, as ctrl.inc lays it out
ZC_LX       equ 2               ; text inset - CP_PMX, the panel's own
ZC_HY       equ 4               ; the heading row
ZC_Y0       equ 20              ; the first value row
ZC_ROW      equ 12
ZC_VX       equ 40              ; the value column: four-letter labels
ZC_GLYPH_H  equ 8

; --- Eject, ONE definition, PANE-RELATIVE (netui.inc's NP_B*, for its bug) ---
ZC_BX       equ 2
ZC_BY       equ ZC_Y0 + 4*ZC_ROW + 6
ZC_BW       equ 64
ZC_BH       equ 16

; -----------------------------------------------------------------------------
; zc_ent - THE PAGE VERBS' FRAME (netui.inc's net_pgent)
; in:  the FIRST instruction of a DSV_CP* verb; DI = pane left, DX = pane top
; out: the body has run; every register as the kernel had it
; -----------------------------------------------------------------------------
zc_ent:
    ; STKBALANCE-OK: it takes its own return address as the body to CALL, so the verb's `ret` comes back here and this `ret` goes to the verb's caller
    pop word [zc_body]
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [zc_px], di
    mov [zc_py], dx
    call [zc_body]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; DSV_CPPAINT - DI = pane left, DX = pane top
; -----------------------------------------------------------------------------
zc_paint:
    call zc_ent
    call zs_query               ; the drive, NOW (above)
    mov si, zip_s_name          ; the heading: the list row's own string
    mov bx, ZC_HY
    call zc_lab

    mov si, zc_s_port           ; --- Port ---------------------------------
    mov bx, ZC_Y0
    call zc_lab
    mov ax, [zt_base]
    mov di, zc_buf
    mov cx, 4
.hex:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, 0x90                ; the decimal-adjust digit trick (net_hex)
    daa
    adc al, 0x40
    daa
    mov [di], al
    inc di
    pop ax
    loop .hex
    mov [di], cl                ; (CX = 0) the NUL
    call zc_valb

    mov si, zc_s_mode           ; --- Mode ---------------------------------
    mov bx, ZC_Y0 + ZC_ROW
    call zc_lab
    mov ax, 'VP'
    mov di, zc_buf
    mov [di], ax
    mov ah, [zp_proto]          ; ZIPP_VPI0 = 1 -> '0', ZIPP_VPI2 = 2 -> '2'
    shl ah, 1
    add ah, '0' - 2
    mov al, 'I'
    mov [di+2], ax
    mov si, zc_s_nib
    cmp byte [zt_mode], ZIPM_PS2
    jne .m
    mov si, zc_s_ps2
.m:
    add di, 4
.cpy:                           ; (not lodsb: DF is not ours to assume)
    mov al, [si]
    inc si
    mov [di], al
    inc di
    or al, al
    jnz .cpy
    call zc_valb

    mov si, zc_s_disk           ; --- Disk ---------------------------------
    mov bx, ZC_Y0 + 2*ZC_ROW
    call zc_lab
    mov al, [zp_st]
    mov si, zc_s_nodrv
    cmp al, ZIPS_NODRIVE
    je .dv
    mov si, zc_s_nodisk
    cmp al, ZIPS_NODISK
    je .dv
    call zc_mb                  ; SI = "<n> MB" in zc_buf
.dv:
    call zc_valt
    xor si, si                  ; --- and the write-protect line -----------
    cmp byte [zp_st], ZIPS_WP
    jne .wp
    mov si, zc_s_wp
.wp:
    mov bx, ZC_Y0 + 3*ZC_ROW
    call zc_valt
    jmp zc_btn

; -----------------------------------------------------------------------------
; zc_mb - [zp_cap] as "<n> MB" in zc_buf. out: SI = its first digit.
; clobbers: AX, CX, DX
; A megabyte here is 2,048 sectors, so a Zip 100 says 96 and a Zip 250 239.
; -----------------------------------------------------------------------------
zc_mb:
    mov ax, [zp_cap]
    mov dx, [zp_cap+2]
    mov cx, 11
.sh:
    shr dx, 1
    rcr ax, 1
    loop .sh
    mov si, zc_buf + 8          ; the digits are written backwards from here
    mov word [si], ' M'
    mov word [si+2], 'B'        ; ...'B' and the NUL
    mov cx, 10
.dig:
    xor dx, dx
    div cx
    add dl, '0'
    dec si
    mov [si], dl
    or ax, ax
    jnz .dig
    ret

; -----------------------------------------------------------------------------
; zc_lab / zc_valb / zc_valt - one string at a row, ONE font_run
;   zc_lab:  SI = string, BX = y offset          -> the label column
;   zc_valb: zc_buf, BX = y offset               -> the value column
;   zc_valt: SI = string (0 = none), BX = y      -> the value column, and the
;            rest of the line cleared: its TAIL only (SPEC.md 31.9.3)
; clobber: AX, CX, DX, SI - a page verb's, whose frame is zc_ent's
; -----------------------------------------------------------------------------
zc_valb:
    mov si, zc_buf
zc_val:
    mov cx, ZC_VX
    jmp short zc_txt
zc_lab:
    mov cx, ZC_LX
zc_txt:
    add cx, [zc_px]
    mov dx, [zc_py]
    add dx, bx
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    ret

zc_valt:
    xor ax, ax                  ; no string: the whole line is tail
    or si, si
    jz .tail
    call zc_val
    call OSAPI_FONT_WIDTH       ; AX = its width
.tail:
    push bx
    add ax, ZC_VX
    add ax, [zc_px]             ; x1 = where the string ended
    mov cx, [zc_px]
    add cx, ZC_W - 1            ; x2 = the pane's right edge
    cmp ax, cx
    ja .out
    add bx, [zc_py]             ; y1
    mov dx, bx
    add dx, ZC_GLYPH_H - 1      ; y2
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; zc_brect - Eject's rect in SCREEN coordinates. out: BX -> it. clobbers AX
; -----------------------------------------------------------------------------
zc_brect:
    mov ax, [zc_px]
    add ax, ZC_BX
    mov [zc_rc+0], ax
    add ax, ZC_BW - 1
    mov [zc_rc+4], ax
    mov ax, [zc_py]
    add ax, ZC_BY
    mov [zc_rc+2], ax
    add ax, ZC_BH - 1
    mov [zc_rc+6], ax
    mov bx, zc_rc
    ret

; -----------------------------------------------------------------------------
; zc_btn - Eject, through THE ONE CONTROL (SPEC.md 20.5.1.3), greyed with no
;          disk (SPEC.md 47: the predicate is [zp_st], what the drive said)
; clobbers: AX, BX, SI, DI
; -----------------------------------------------------------------------------
    OS88UI_BTNREC zc_btrec, 0, zc_btlbl, zc_btflg, 1

zc_btn:
    call zc_brect
    mov di, OS88UI_FILL
    cmp byte [zp_st], ZIPS_READY
    jae .live
    mov di, OS88UI_FILL | OS88UI_DIS
.live:
    cmp byte [zc_down], 0
    je .up
    mov di, OS88UI_DOWN
.up:
    mov word [zc_btlbl], zc_s_eject
    mov [zc_btflg], di
    mov [zc_btrec+OS88UI_BT_RECTS], bx
    mov bx, zc_btrec
    mov al, 1
    call os88ui_btn
    ret

; zc_bhit - the pane-relative click CX/BX in Eject? out: CF = 0 inside.
; clobbers AX, BX, CX, DX
zc_bhit:
    add cx, [zc_px]
    mov dx, [zc_py]
    add dx, bx
    call zc_brect
    jmp os88ui_bhit

; zc_setdown - AL = 1 Eject drawn pressed, 0 upright; drawn only on a change
zc_setdown:
    cmp al, [zc_down]
    je .out
    mov [zc_down], al
    jmp short zc_btn
.out:
    ret

; -----------------------------------------------------------------------------
; DSV_CPCLICK - the press ARMS (SPEC.md 13.8.4), and only a live Eject
; -----------------------------------------------------------------------------
zc_click:
    call zc_ent
    cmp byte [zp_st], ZIPS_READY
    jb .out                     ; greyed: a press on it is no press
    call zc_bhit
    jc .out
    mov ax, 1
    call os88ui_arm
    mov al, 1
    jmp short zc_setdown
.out:
    ret

; -----------------------------------------------------------------------------
; DSV_CPDRAG - the tracking edge (SPEC.md 13.8.1)
; -----------------------------------------------------------------------------
zc_drag:
    call zc_ent
    call os88ui_armed
    or ax, ax
    jz .out
    call zc_bhit
    sbb al, al
    inc al                      ; on it: down (1); off it: up (0)
    jmp short zc_setdown
.out:
    ret

; -----------------------------------------------------------------------------
; DSV_CPUP - the release FIRES Eject (SPEC.md 138.7), then the page repaints
; itself, the kernel not doing so after a click (SPEC.md 31.9)
; -----------------------------------------------------------------------------
zc_up:
    call zc_ent
    call os88ui_fire
    or ax, ax
    jz .out
    call zc_bhit
    jc .off                     ; slid off: thought better of
    mov byte [zc_down], 0       ; drawn upright BY the repaint, not before it
    call zp_eject
    mov di, [zc_px]
    mov dx, [zc_py]
    jmp zc_paint                ; its own frame nests inside ours
.off:
    xor al, al
    jmp short zc_setdown
.out:
    ret

; --- the page's strings ------------------------------------------------------
zc_s_port:  db 'Port', 0
zc_s_mode:  db 'Mode', 0
zc_s_disk:  db 'Disk', 0
zc_s_nib:   db ' nibble', 0
zc_s_ps2:   db ' PS/2', 0
zc_s_nodrv: db 'No drive', 0
zc_s_nodisk: db 'No disk', 0
zc_s_wp:    db 'Write-protected', 0
zc_s_eject: db 'Eject', 0
