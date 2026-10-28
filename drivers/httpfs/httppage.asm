; =============================================================================
; os8088 - HTTPPAGE.DRV
;
; HTTPFS.DRV's OTHER half (SPEC.md 122.5): its Control Panel page. It is read
; off the boot volume the first time somebody selects `Web Drive` in the
; panel's list, and its memory goes back when the panel closes - RAMPAGE.DRV's
; shape (SPEC.md 62.9.9), so a drive that is live costs nothing for a page
; nobody is looking at.
;
;     HTTP Drive
;     Address
;     [http://10.0.0.2:8088/________]
;     [ Mount ]  [Unmount]
;     Drive E:  70 entries
;     <the last failure, in words>
;
; **IT REACHES THE DRIVE THROUGH THE PACKAGE DOOR**, OSAPI_DRV_CALL with BH =
; DRVC_HTTP, exactly as an installer does (hfpkg.inc): HFV_STATE is what every
; paint reads, HFV_MOUNT / HFV_FETCH / HFV_UNMOUNT are the two buttons, and
; HFV_URL / HFV_SAVE are the field's address and HTTPDRV.CFG. So there is no
; private service ABI between the two images - only the six verbs the
; resident's thunks send here (HPT_*, hfpkg.inc).
;
; ONE FIELD, so it always has the caret: there is nothing else on the page a
; key could be for. Enter is Mount. A page draws unclipped and must repaint
; what it changes (SPEC.md 31.9), so every handler redraws exactly the field,
; a button or the two status lines - never the whole pane on a key.
;
; MOUNT FETCHES NOW, where a drive mounted at boot fetches when it is first
; opened: a user who has just typed an address wants to know at once whether
; the server answers, and the status line is where they are looking.
;
; NOTHING REACHES A DISK ON A CLICK (SPEC.md 31.8): Mount and Unmount mark
; [hp_dirty] and the panel's close asks the resident for HFV_SAVE.
; =============================================================================

%include "os88drv.inc"
%include "netpkg.inc"
%include "hfpkg.inc"

    OS88_OVERLAY 'Web Page', HFP_ABI, hp_entry

HP_LX       equ 2
HP_HY       equ 4               ; the heading
HP_AY       equ 20              ; 'Address'
HP_FY       equ 32              ; the field...
HP_FH       equ 13              ; ...and its last row, inclusive
HP_FX2      equ 218
HP_BY       equ 54              ; the buttons
HP_BH       equ 16
HP_BW       equ 68
HP_B2X      equ 76
HP_S1Y      equ 80              ; the two status lines
HP_S2Y      equ 92
HP_HINTY    equ 108
HP_COLS     equ 27              ; a line is padded to the pane: font_run is
                                ; opaque, so a shorter string than last time
                                ; leaves no tail (SPEC.md 31.9.3)
HP_BMOUNT   equ 1               ; the press's token, os88ui_arm's
HP_BUNMNT   equ 2
HFE_BUSY    equ 11              ; the page's own: 'Fetching...' (not an error)
HFE_MAXMSG  equ 11

; The shared includes: os88ui.inc's button and os88line.inc's field.
%define OS88UI_ARM              ; os88ui_arm/fire/armed - the page drives its
%define OS88UI_NOGEST           ; own gesture, etherui.inc's shape
%define OS88UI_NOBFIND
%define OS88UI_NOGLYPH          ; no check box, no radio
%include "os88ui.inc"
%define OS88LINE_NOEDIT
%define OS88LINE_NOSET
%include "os88line.inc"

; -----------------------------------------------------------------------------
; hp_entry - the dispatcher's landing site. AL = an HPT_*, and every other
; register exactly as the kernel handed the resident's thunk; DS = ours
; -----------------------------------------------------------------------------
hp_entry:
    cmp al, HPT_MAX
    ja .bad
    cbw
    xchg ax, bp                 ; BP is ours to spend: the resident banked it
    shl bp, 1
    jmp [ds:bp+hp_vtab]
.bad:
    stc
    ret

hp_vtab:                        ; in HPT_* order, which hfpkg.inc pins
    dw hp_paint, hp_click, hp_up, hp_drag, hp_key, hp_close
%if ($ - hp_vtab) / 2 != HPT_MAX + 1
  %error "httppage: hp_vtab is indexed by the HPT_* numbers"
%endif

; --- hp_door - BL = an HFV_*: OSAPI_DRV_CALL to the resident ----------------
hp_door:
    mov bh, DRVC_HTTP
    call OSAPI_DRV_CALL         ; ES = our DS on the way in: the X stub's
    ret

; --- hp_state - HFV_STATE into the page's copy of it ------------------------
hp_state:
    push bx
    push cx
    push dx
    push si
    mov bl, HFV_STATE
    call hp_door
    mov [hp_vol], ax            ; AL = the volume, AH = HFS_*
    mov [hp_nrec], cx
    mov [hp_hst], dx
    mov [hp_err], si
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; HPT_PAINT - DI = pane left, DX = pane top
; -----------------------------------------------------------------------------
hp_paint:
    call os88_ent
    call hp_at
    mov si, hp_s_name
    mov bx, HP_HY
    call hp_lab
    mov si, hp_s_addr
    mov bx, HP_AY
    call hp_lab
%ifndef HFDIAG
    mov si, hp_s_hint
    mov bx, HP_HINTY
    call hp_lab
%endif
    call hp_state
    call hp_field
    call hp_btns
    call hp_status
    jmp os88_r_bp

; --- hp_at - DI/DX = the pane: banked, and both buttons' screen rects made
;     from it, which every later hit and draw reads until the next paint
hp_at:
    push ax
    push bx
    push dx
    mov [hp_px], di
    mov [hp_py], dx
    mov bx, hp_brect
    add dx, HP_BY
    lea ax, [di+HP_LX]
    call .one
    add ax, HP_B2X - HP_LX - HP_BW + 1
    call .one
    pop dx
    pop bx
    pop ax
    ret
.one:
    mov [bx], ax
    mov [bx+2], dx
    add ax, HP_BW - 1
    mov [bx+4], ax
    push dx
    add dx, HP_BH - 1
    mov [bx+6], dx
    pop dx
    add bx, 8
    ret

; --- hp_field - the field's rect from the pane, synced, drawn ---------------
hp_field:
    mov si, hp_lnb
    mov ax, [hp_px]
    add ax, HP_LX
    mov [si+LN_X1], ax
    add ax, HP_FX2 - HP_LX
    mov [si+LN_X2], ax
    mov ax, [hp_py]
    add ax, HP_FY
    mov [si+LN_Y1], ax
    add ax, HP_FH
    mov [si+LN_Y2], ax
    mov word [si+LN_BUF], hp_url
    mov word [si+LN_MAX], HF_URLMAX
    cmp byte [hp_have], 0
    jne .drawn
    mov di, hp_url              ; the address as the resident holds it: the
    mov bl, HFV_URL             ; settings file's, or a package's MOUNT
    call hp_door
    call os88line_resync
    inc byte [hp_have]
.drawn:
    mov byte [si+LN_FOCUS], 1
                                ; ...and falls into hp_fdraw
; --- hp_fdraw - SI = the field: drawn, and SCROLLED HOME whenever the whole
;     address fits - os88line keeps a view a deletion left behind, and an
;     address that fits should never read 'ttp://...'
hp_fdraw:
    push cx
    call os88line_cols          ; CX = the columns the box shows
    cmp [si+LN_LEN], cx
    jae .draw
    mov word [si+LN_VIEW], 0
.draw:
    pop cx
    jmp os88line_draw

; -----------------------------------------------------------------------------
; HPT_KEY - BL = ASCII, BH = scan. CF=0 taken, CF=1 not ours
; -----------------------------------------------------------------------------
hp_key:
    call os88_ent
    mov ax, bx                  ; os88line_key takes AL/AH
    mov si, hp_lnb
    call os88line_key
    jc .mine
    call hp_fdraw               ; ...and only the field it changed
    jmp short .took
.mine:
    cmp al, 13
    stc
    jne .out
    call hp_mount               ; Enter is Mount
.took:
    clc
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; HPT_CLICK - CX = pane x, BX = pane y. The field takes a click at once;
; a button only ARMS and draws pressed, and fires on the release (13.8.4)
; -----------------------------------------------------------------------------
hp_click:
    call os88_ent
    call hp_xy                  ; CX/DX = the point on the screen
    mov si, hp_lnb
    call os88line_click         ; (keeps CX and DX)
    jc .btn
    call hp_fdraw
    jmp short .out
.btn:
    call hp_bhit
    jc .out
    cmp al, HP_BUNMNT
    jne .arm
    cmp byte [hp_vol], 0xFF
    je .out                     ; greyed: the same predicate the paint reads
.arm:
    xor ah, ah
    call os88ui_arm
    call hp_setdown
.out:
    jmp os88_r_bp

; --- hp_xy - the pane at DI/DX banked (hp_at), CX/BX pane-relative -> CX/DX
;     on the screen
hp_xy:
    call hp_at
    add cx, di
    add dx, bx
    ret

; --- HPT_DRAG / HPT_UP: one body, CF says which (etherui.inc's shape) -------
hp_drag:
    CLC_OR_STC hp_up
hp_ud:
    call os88_ent               ; (keeps the flags)
    sbb bp, bp                  ; BP = -1 a release, 0 a drag
    call hp_xy
    call os88ui_armed           ; a drag peeks...
    or bp, bp
    jz .got
    call os88ui_fire            ; ...a release spends it
.got:
    or ax, ax
    jz .out
    xchg ax, bx                 ; BL = what the press armed
    call hp_bhit
    jc .off
    cmp al, bl
    jne .off
    or bp, bp
    jz .set
    mov byte [hp_down], 0
    call hp_btns                ; upright again before the work starts
    cmp al, HP_BUNMNT
    je .unmount
    call hp_mount
    jmp short .out
.unmount:
    mov bl, HFV_UNMOUNT
    call hp_door
    mov byte [hp_dirty], 1
    call hp_show
    jmp short .out
.off:
    xor al, al
.set:
    call hp_setdown
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; hp_mount - the field's address, mounted and fetched, said on the page
; -----------------------------------------------------------------------------
hp_mount:
    push ax
    push bx
    push si
    mov byte [hp_dirty], 1
    mov si, hp_url              ; ES:SI, which the X stub makes our DS
    mov bl, HFV_MOUNT
    call hp_door
    jc .show                    ; the resident kept the reason (HFV_STATE)
    call hp_state
    call hp_btns
    mov word [hp_err], HFE_BUSY ; 'Fetching...' while the wire works
    call hp_status
    mov bl, HFV_FETCH           ; ...which keeps how it went for HFV_STATE
    call hp_door
.show:
    call hp_show
    pop si
    pop bx
    pop ax
    ret

hp_show:
    call hp_state
    call hp_btns
    jmp hp_status

; --- hp_bhit - CX/DX on the screen -> CF=0, AL = HP_B*. keeps every other -
hp_bhit:
    push bx
    mov bx, hp_brect
    mov al, HP_BMOUNT
.t:
    call os88ui_bhit
    jnc .out
    add bx, 8
    inc al
    cmp al, HP_BUNMNT
    jbe .t
    stc
.out:
    pop bx
    ret

hp_setdown:
    cmp al, [hp_down]
    je .out
    mov [hp_down], al
    call hp_btns
.out:
    ret

; --- hp_btns - both, in the state they are in -------------------------------
hp_btns:
    push ax
    push bx
    mov ax, OS88UI_FILL
    cmp byte [hp_down], HP_BMOUNT
    jne .m
    or ax, OS88UI_DOWN
.m:
    mov [hp_bflg], ax
    mov ax, OS88UI_FILL
    cmp byte [hp_vol], 0xFF     ; DISABLED IS A FACT: no drive, nothing to
    jne .u                      ; unmount (SPEC.md 47 rule 3)
    or ax, OS88UI_DIS
    jmp short .uset
.u:
    cmp byte [hp_down], HP_BUNMNT
    jne .uset
    or ax, OS88UI_DOWN
.uset:
    mov [hp_bflg+2], ax
    mov bx, hp_brec
    mov word [bx+OS88UI_BT_N], 2    ; both live, always
    mov al, HP_BMOUNT
    call os88ui_btn
    mov al, HP_BUNMNT
    call os88ui_btn
    pop bx
    pop ax
    ret

hp_brec:    dw hp_brect, hp_blbl, hp_bflg, 0
            times OS88UI_BT_SIZE - 8 db 0
hp_blbl:    dw hp_s_mount, hp_s_unmnt

; -----------------------------------------------------------------------------
; hp_status - the two lines: where the drive is and what it holds, and the
;             last failure in words
; -----------------------------------------------------------------------------
hp_status:
    call os88_ent
    push es
    push ds
    pop es
    mov di, hp_sbuf             ; --- line 1 -----------------------------------
    mov si, hp_s_none
    mov al, [hp_vol]
    cmp al, 0xFF
    je .l1
    mov si, hp_s_drive          ; 'Drive '
    call hp_scpy                ; (lodsb: AL is spent, so read it again)
    mov al, [hp_vol]
    add al, 'A'
    stosb
    mov al, ':'
    stosb
    mov si, hp_s_lazy
    cmp byte [hp_st], HFS_READY
    jne .l1
    mov ax, ' '
    stosb
    stosb
    mov ax, [hp_nrec]
    dec ax                      ; the root is not an entry
    call hp_utoa
    mov si, hp_s_ents
.l1:
    call hp_scpy
    mov bx, HP_S1Y
    call hp_padrun
    mov di, hp_sbuf             ; --- line 2 -----------------------------------
    mov cx, [hp_err]
    dec cx
    cmp cx, HFE_MAXMSG - 1
    ja .l2                      ; 0 = none, and anything we have no words for
    mov si, hp_msgs             ; ...the CXth of a packed run of strings: a
    jcxz .got                   ; walk is 9 bytes where a table was 22
.skip:
    lodsb
    or al, al
    jnz .skip
    loop .skip
.got:
    call hp_scpy
    cmp word [hp_err], HFE_STATUS
    jne .l2
    mov ax, [hp_hst]
    call hp_utoa
.l2:
%ifdef HFDIAG
    call hp_diag                ; ...in line 2's place: its HFE_* is in each
%else                           ; record
    mov bx, HP_S2Y
    call hp_padrun
%endif
    jmp os88_r_es

%ifdef HFDIAG
; --- hp_diag - HFDIAG=1: the resident's read log, two lines a read, oldest
;     first, under the status lines. ES = DS. clobbers what hp_status may
hp_diag:
    push bp
    mov di, hp_dlog
    mov bl, HFV_DIAG
    call hp_door                ; CX = reads logged
    sub cx, 2                   ; the LAST TWO, oldest first: what the pane
    mov bx, HP_S2Y - 4          ; has room for under line 1
    mov bp, 2
.rec:
    mov ax, cx
    and ax, 3
    inc cx
    push cx
    mov cl, 24
    mul cl
    add ax, hp_dlog
    mov si, ax
    mov di, hp_sbuf
    cmp byte [si], 0
    je .blank
    lodsb                       ; kind
    stosb
    lodsb                       ; flags
    push ax
    lodsw                       ; handle
    call hp_hex4
    call hp_sp
    lodsw                       ; offset
    call hp_hex4
    call hp_sp
    lodsw                       ; count
    call hp_hex4
    call hp_sp
    pop ax
    call hp_hex2
    call hp_sp
    push si
    add si, 12                  ; +20: the four bytes delivered
    lodsb
    call hp_hex2
    lodsb
    call hp_hex2
    lodsb
    call hp_hex2
    lodsb
    call hp_hex2
    call hp_padrun              ; (which takes SI: the record is on the stack)
    pop si
    add bx, 9
    mov di, hp_sbuf
    lodsw                       ; status
    call hp_hex4
    call hp_sp
    lodsw                       ; body bytes not delivered
    call hp_hex4
    call hp_sp
    lodsw                       ; ticks taken
    call hp_hex4
    call hp_sp
    lodsw                       ; result
    call hp_hex4
    call hp_sp
    lodsw                       ; HFE_*
    call hp_hex2
    call hp_sp
    lodsw                       ; staged
    call hp_hex4
    jmp short .last
.blank:
    call hp_padrun
    add bx, 9
    mov di, hp_sbuf
.last:
    call hp_padrun
    add bx, 9
    pop cx
    dec bp
    jnz .rec
    pop bp
    ret

hp_sp:
    mov al, ' '
    stosb
    ret
hp_hex4:
    push ax
    mov al, ah
    call hp_hex2
    pop ax
hp_hex2:
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call .nib
    pop cx
    pop ax
.nib:
    push ax
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 7
.put:
    stosb
    pop ax
    ret
%endif

; --- hp_utoa - AX in decimal at ES:DI ---------------------------------------
hp_utoa:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    xor cx, cx
    mov bx, 10
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    stosb
    loop .out
    ret

; --- hp_scpy - the NUL string at SI to ES:DI, without its NUL ---------------
hp_scpy:
    lodsb
    or al, al
    jz .z
    stosb
    jmp short hp_scpy
.z:
    ret

; --- hp_padrun - [hp_sbuf] up to DI, padded to HP_COLS, drawn at row BX ----
hp_padrun:
    mov al, ' '
.p:
    cmp di, hp_sbuf + HP_COLS
    jae .z
    stosb
    jmp short .p
.z:
    mov byte [di], 0
    mov si, hp_sbuf
                                ; ...and falls into hp_lab
hp_lab:                         ; SI = a string, BX = its y in the pane
    push ax
    push cx
    push dx
    mov cx, [hp_px]
    add cx, HP_LX
    mov dx, [hp_py]
    add dx, bx
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; HPT_CLOSE - the panel has gone: HTTPDRV.CFG if a Mount or Unmount changed
; it. SILENT about failing, ethcfg.inc's reason: the panel is gone and what
; was asked for - a drive - already happened
; -----------------------------------------------------------------------------
hp_close:
    push ax
    push bx
    cmp byte [hp_dirty], 0
    je .out
    mov bl, HFV_SAVE
    call hp_door
.out:
    pop bx
    pop ax
    clc
    ret

hp_s_name:  db 'HTTP Drive', 0
hp_s_addr:  db 'Address', 0
hp_s_hint:  db 'e.g. http://10.0.0.2:8088/', 0
hp_s_mount: db 'Mount', 0
hp_s_unmnt: db 'Unmount', 0
hp_s_none:  db 'Not mounted', 0
hp_s_drive: db 'Drive ', 0
hp_s_lazy:  db '  not read yet', 0
hp_s_ents:  db ' entries', 0
hp_msgs:                        ; one per HFE_*, 1..HFE_MAXMSG, IN ORDER -
    db 'Bad address', 0         ; HFE_URL       the walk in hp_status counts
    db 'No network', 0          ; HFE_NONET     NULs to find one
    db 'Cannot connect', 0      ; HFE_CONNECT
    db 'Timed out', 0           ; HFE_TIMEOUT
    db 'HTTP ', 0               ; HFE_STATUS    ...and the status after it
    db 'No OS88.IDX', 0         ; HFE_INDEX
    db 'Cut short', 0           ; HFE_SHORT
    db 'No memory', 0           ; HFE_MEM
    db 'No drive letter', 0     ; HFE_VOL
    db 'Not understood', 0      ; HFE_ARG
    db 'Fetching...', 0         ; HFE_BUSY, the page's own
%if HFE_URL != 1 || HFE_ARG != 10 || HFE_BUSY != 11 || HFE_MAXMSG != 11
  %error "httppage: hp_msgs is one string per HFE_*, in order"
%endif

    OS88_ENT
    OS88_RET_LADDER es

; =============================================================================
; STATE - zero at load, and off the disk (OS88_STATE)
; =============================================================================
    OS88_STATE
hp_px:      dw 0
hp_py:      dw 0
hp_vol:     db 0                ; HFV_STATE: AL, the volume (0FFh none)...
hp_st:      db 0                ; ...AH, the HFS_*...
hp_nrec:    dw 0                ; ...CX, the records...
hp_hst:     dw 0                ; ...DX, the last HTTP status...
hp_err:     dw 0                ; ...SI, the last HFE_* (or HFE_BUSY)
hp_down:    db 0                ; the button drawn pressed, 0 = neither
hp_dirty:   db 0                ; 1 = HTTPDRV.CFG is owed at the close
hp_have:    db 0                ; 1 = hp_url holds the resident's address
hp_brect:   times 16 db 0       ; the two buttons' screen rects
hp_bflg:    dw 0, 0             ; ...and their flags
hp_lnb:     times OS88LINE_SZ db 0
hp_sbuf:    times HP_COLS + 1 db 0
hp_url:     times HF_URLMAX db 0
%ifdef HFDIAG
hp_dlog:    times 96 db 0
%endif

    OS88_DRV_END
