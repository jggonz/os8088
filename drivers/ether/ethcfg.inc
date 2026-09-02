; =============================================================================
; ethcfg.inc - setting the address by hand (SPEC.md 72.7)
;
; DHCP is still how a machine gets an address and nothing here changes that:
; `Automatic` is the default, it is what a fresh machine boots into, and every
; number on the Control Panel page comes off the wire. This is the other case,
; and it is the FIELD's rather than the container's - a LAN with no DHCP
; server, or a crossover cable to one other PC, where the alternative was
; rebuilding the driver with `make ETHIP=...` and carrying a floppy across the
; room (docs/FIELD-MACHINES.md's seven-step path, which this whole subsystem
; exists to shorten).
;
; --- WHY IT IS A WINDOW AND NOT THE PAGE ------------------------------------
; **NOT because a page cannot be typed into - it can.** DSV_CPKEY is a cell of
; the service table (SPEC.md 31.9.2), cp_onkey forwards to it and RAMDISK.DRV
; already publishes one. The reason is the LAYOUT: four editable dotted quads,
; two mode radios and two buttons do not fit the 221x132 pane a page is given,
; and the keyboard is kern_big's alone (SPEC.md 62.9.15) while this driver
; loads on both kernels.
;
; A driver's own WINDOW has W_ONKEY like any other window and costs the kernel
; nothing, which is HDD.DRV's disk tool exactly (SPEC.md 52.2). So the page
; gets a `Set Up` button and this is what it opens.
;
; --- IT IS UNBOUND, AND THAT IS THE FILE DIALOG'S RULE ----------------------
; No instance record, so no dock tile, no Task Manager row and no callback
; billing - and its close box reduces to `wm_hide` (SPEC.md 38). What that
; costs is one obligation the kernel cannot discharge: **the window's
; dispatcher is OURS**, so a driver that detaches with it on screen leaves a
; record pointing at freed memory. `eth_cfg_reap` is the answer and both
; DSV_CPCLOSE and DRVV_DETACH call it.
;
; --- APPLYING IS IMMEDIATE; REMEMBERING WAITS FOR THE PANEL -----------------
; SPEC.md 31.8: no setting writes to a disk on a click. `Ok` sets the live
; addresses on the spot - the point of the window is to be able to try one -
; and marks `[ec_dirty]`; DSV_CPCLOSE is what spends it. That is
; `cp_flush_close`'s own shape, one driver over.
;
; The file is `ETHER.CFG` in the root of the volume the machine BOOTED from
; (OSAPI_VOL_SYS), twenty bytes.
;
; **THE REASON IT IS A FILE CHANGED UNDER IT AND THE ANSWER DID NOT, WHICH IS
; WORTH SAYING RATHER THAN QUIETLY LEAVING.** It was written when SPEC.md
; 51.9's settings blob was ONE 34 bytes for the whole machine with the
; hard-disk driver holding it - a second writer would have destroyed a mounted
; partition's record, and the machine this project is calibrated against has an
; ST-225 in it. SPEC.md 51.9.1 landed on the integration branch in the same
; round and made the blob PER ROW, so that objection is gone.
;
; What is left is 51.9's own trade, in the direction it does not usually fall.
; A blob is `.bss` in the KERNEL, reserved on every machine whether or not the
; driver is ever loaded - DRV_BLOB_SZ's comment says so - and eighteen more
; bytes of it would be charged to the 128KB machine for a card it has not got.
; A file is charged to the machines that use one. This driver is opt-in, its
; DHCP spin is up to six seconds, and one twenty-byte read is nothing beside
; it. SPEC.md 72.8 records what taking the blob instead would cost, because
; the two are close enough that the next person should be told rather than
; left to re-derive it.
; =============================================================================

EC_MAGIC    equ 'EN'            ; the file: magic, version, mode, four addresses
EC_VER      equ 2               ; ...and, at version 2, the socket buffer rung
EC_F_MAGIC  equ 0               ; the user picked (SPEC.md 72.13.2)
EC_F_VER    equ 2
EC_F_MODE   equ 3
EC_F_IP     equ 4
EC_F_BUF    equ 20              ; KB per receive ring: 0 = auto, 1, 4 or 8
EC_F_SIZE   equ 21
EC_F_SIZE1  equ 20              ; **VERSION 1 IS STILL READ**, because the file
                                ; it is in carries the manual ADDRESS of a
                                ; machine on a LAN with no DHCP server - the
                                ; one case this whole window exists for - and
                                ; refusing it would lose that to gain a
                                ; setting the same file is about to be
                                ; rewritten with. SPEC.md 51.5 rule 2 says an
                                ; unknown version leaves the defaults; a
                                ; version that is a PREFIX of this one is not
                                ; unknown, it is short

; --- the window ---------------------------------------------------------------
; 216 x 141 INCLUDING the frame, which is what a template's w/h are, AT y = 30 -
; and both numbers are CGA's rather than VGA's. That adapter is 200 rows with
; the dock at 176, so the window has 176 - MBAR_H = 163 to live in and lands at
; 30..170; wm_fit does not have to touch it. **A template that fits VGA and not
; CGA is CLAMPED rather than refused** (SPEC.md 39.7), and a clamp shrinks the
; frame while every offset below is still laid out for the size that was asked
; for - so the fields would be drawn outside the window that is meant to hold
; them, on the one adapter this is hardest to look at.
EC_FW       equ 216
EC_FH       equ 141
EC_RY       equ 2               ; the mode row: two radios side by side
EC_R2X      equ 108
EC_F0Y      equ 20              ; the first field...
EC_FPIT     equ 20              ; ...and the pitch between them
EC_FHT      equ 14              ; a field's height
EC_LX       equ 2               ; the label column
EC_FX1      equ 72              ; the field's left and right, content-relative
EC_FX2      equ 204
EC_MSGY     equ 96
EC_BY       equ 106
EC_BH       equ 16
EC_BW       equ 60
EC_B1X      equ 72              ; Ok
EC_B2X      equ 140             ; Cancel
EC_COK      equ 1               ; ...and their control ids (SPEC.md 13.8.4):
EC_CCANCEL  equ 2               ; what a press ARMS and a release fires
EC_NF       equ 4               ; Address, Subnet, Router, Name

ec_tpl:     dw 60, 30, EC_FW, EC_FH, ec_s_title, ec_paint, ec_key, ec_click
ec_s_title: db 'Ethernet Setup', 0

; -----------------------------------------------------------------------------
; eth_cfg_open - put the window up (the page's `Set Up` button)
; in:  the gfx lock is HELD (a Control Panel click)
;
; **THE FIELDS ARE SEEDED FROM THE LIVE ADDRESSES EVERY TIME**, not from the
; last thing typed. That is right in both modes and not a shortcut: in Manual
; the live values ARE what was typed, and in Automatic they are what DHCP got -
; which is the sensible thing to start editing from when a user opens this to
; pin down an address the server already handed out.
; -----------------------------------------------------------------------------
eth_cfg_open:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [eth_mode]
    mov [ec_mode], al
    mov si, eth_ip
    mov di, ec_t0
    call eth_ipstr
    mov si, eth_mask
    mov di, ec_t1
    call eth_ipstr
    mov si, eth_gw
    mov di, ec_t2
    call eth_ipstr
    mov si, eth_dns
    mov di, ec_t3
    call eth_ipstr
    mov word [ec_msg], ec_s_ready

    xor cx, cx                  ; the four blocks: the buffer, its size, the
.fld:                           ; text length, and the caret at the end
    call ec_blk
    mov di, [ec_bufs+bx]
    mov [si+LN_BUF], di
    mov word [si+LN_MAX], EC_TLEN
    mov byte [si+LN_FOCUS], 0
    mov word [si+LN_VIEW], 0
    push cx
    call os88line_set           ; DI = the string; the caret lands at the end
    pop cx
    inc cx
    cmp cx, EC_NF
    jb .fld
    mov si, ec_f0
    mov byte [si+LN_FOCUS], 1   ; the first field has it, so a user who opens
                                ; this and types is typing into the address

    mov bx, [ec_win]
    or bx, bx
    jnz .show
    mov si, ec_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [ec_win], bx
    mov ax, ec_up               ; SPEC.md 13.7/13.8.1: NOT template words, so a
    call OSAPI_WM_ONMOUSEUP     ; window installs them itself after create
    mov ax, ec_drag
    call OSAPI_WM_ONDRAG
    mov byte [ec_down], 0       ; a window reopened mid-gesture must not come
    xor ax, ax                  ; up with one armed for the next release to
    call os88ui_arm             ; fire
.show:
    call OSAPI_WM_SHOW
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_blk - CX = 0..EC_NF-1 -> SI = that field's line block, BX = CX * 2
; -----------------------------------------------------------------------------
ec_blk:
    mov bx, cx
    shl bx, 1
    mov si, [ec_blks+bx]
    ret

; -----------------------------------------------------------------------------
; ec_layout - the four field rects, in SCREEN coordinates
; in:  [ec_win] is live
; out: DI = content left, BP = content top
;
; **RECOMPUTED AT EVERY PAINT, CLICK AND KEY**, because a window moves: a rect
; banked once is a control that is drawn in one place and clickable in another
; the moment the user drags it, which is SPEC.md 22's fm_hit discipline said
; about a window that is not fixed.
; -----------------------------------------------------------------------------
ec_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [ec_win]
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov di, ax
    mov bp, dx
    mov dx, bp                  ; ...and DX walks down the column, which is why
    add dx, EC_F0Y              ; there is no multiply here: an 8086 `mul`
    xor cx, cx                  ; writes DX, and DX is the thing being kept
.f:
    call ec_blk
    mov ax, di
    add ax, EC_FX1
    mov [si+LN_X1], ax
    mov ax, di
    add ax, EC_FX2
    mov [si+LN_X2], ax
    mov [si+LN_Y1], dx
    mov ax, dx
    add ax, EC_FHT - 1
    mov [si+LN_Y2], ax
    add dx, EC_FPIT
    inc cx
    cmp cx, EC_NF
    jb .f
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_paint - W_PAINT (content already white-filled, lock held)
; in:  SI = the window
; -----------------------------------------------------------------------------
ec_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call ec_layout              ; DI = content left, BP = content top

    ; --- the mode, two radios --------------------------------------------
    mov al, OS88UI_GRADIO
    cmp byte [ec_mode], ECM_DHCP
    jne .r1
    or  al, OS88UI_GON
.r1:
    xor ah, ah
    mov cx, di
    add cx, EC_LX
    mov dx, bp
    add dx, EC_RY
    call os88ui_glyph
    mov si, ec_s_auto
    mov cx, di
    add cx, EC_LX + OS88UI_GW + 4
    mov dx, bp
    add dx, EC_RY + 2
    call ec_txt

    mov al, OS88UI_GRADIO
    cmp byte [ec_mode], ECM_MAN
    jne .r2
    or  al, OS88UI_GON
.r2:
    xor ah, ah
    mov cx, di
    add cx, EC_R2X
    mov dx, bp
    add dx, EC_RY
    call os88ui_glyph
    mov si, ec_s_man
    mov cx, di
    add cx, EC_R2X + OS88UI_GW + 4
    mov dx, bp
    add dx, EC_RY + 2
    call ec_txt

    ; --- the four fields, each with its label ----------------------------
    xor cx, cx
.f:
    push cx
    call ec_blk                 ; SI = the block, BX = the index * 2
    mov dx, [si+LN_Y1]          ; the label sits on the field's own top edge,
    add dx, 3                   ; so the two cannot drift apart
    mov si, [ec_labels+bx]
    mov cx, di
    add cx, EC_LX
    call ec_txt
    pop cx
    push cx
    call ec_blk
    call os88line_draw
    pop cx
    inc cx
    cmp cx, EC_NF
    jb .f

    ; --- the message line, then the two buttons --------------------------
    mov si, [ec_msg]
    mov cx, di
    add cx, EC_LX
    mov dx, bp
    add dx, EC_MSGY
    call ec_txt

    mov ax, di
    add ax, EC_B1X
    mov bx, bp
    add bx, EC_BY
    mov si, ec_s_ok
    mov cl, EC_COK
    call ec_btn
    mov ax, di
    add ax, EC_B2X
    mov bx, bp
    add bx, EC_BY
    mov si, ec_s_cancel
    mov cl, EC_CCANCEL
    call ec_btn

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- ec_txt - SI = the string at (CX, DX), black ----------------------------
ec_txt:
    push ax
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    pop ax
    ret

; --- ec_btn - one button whose top-left is (AX, BX), label SI ---------------
; in:  AX = x, BX = y, SI = the label, CL = the control's id
ec_btn:
    push ax
    push bx
    push si
    push di
    mov [ec_rect], ax
    add ax, EC_BW - 1
    mov [ec_rect+4], ax
    mov [ec_rect+2], bx
    add bx, EC_BH - 1
    mov [ec_rect+6], bx
    mov bx, ec_rect
    mov di, OS88UI_FILL         ; FILL because a RELEASE redraws this button
                                ; over a PRESSED one; on a full repaint the
                                ; pane has already been whited
    cmp cl, [ec_down]
    jne .up
    or di, OS88UI_DOWN          ; SPEC.md 13.8: the pressed look is DRAWN, not
.up:                            ; XOR-ed - a repaint between the two edges must
                                ; agree with the glass
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_click - W_ONCLICK (absolute screen coords, lock held)
; in:  CX = x, DX = y, SI = the window
; -----------------------------------------------------------------------------
ec_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [ec_cx], cx
    mov [ec_cy], dx
    call ec_layout              ; DI = content left, BP = content top

    ; --- a field? --------------------------------------------------------
    xor cx, cx
.f:
    push cx
    call ec_blk
    mov cx, [ec_cx]
    mov dx, [ec_cy]
    call os88line_click
    pop cx
    jc .nextf
    call ec_focus               ; CX = the one that was hit
    call ec_repaint
    jmp .out
.nextf:
    inc cx
    cmp cx, EC_NF
    jb .f

    ; --- a radio? --------------------------------------------------------
    mov ax, [ec_cy]
    sub ax, bp
    sub ax, EC_RY
    cmp ax, OS88UI_GW
    jae .btn
    mov ax, [ec_cx]
    sub ax, di
    cmp ax, EC_LX + 64
    jae .man
    mov byte [ec_mode], ECM_DHCP
    call ec_repaint
    jmp short .out
.man:
    cmp ax, EC_R2X
    jb  .out
    cmp ax, EC_R2X + 64
    jae .out
    mov byte [ec_mode], ECM_MAN
    call ec_repaint
    jmp short .out

    ; --- a button? -------------------------------------------------------
.btn:
    mov ax, [ec_cy]
    sub ax, bp
    cmp ax, EC_BY
    jb  .out
    cmp ax, EC_BY + EC_BH
    jae .out
    mov ax, [ec_cx]
    sub ax, di
    cmp ax, EC_B1X
    jb  .out
    cmp ax, EC_B1X + EC_BW
    jb  .ok
    cmp ax, EC_B2X
    jb  .out
    cmp ax, EC_B2X + EC_BW
    jae .out
    mov al, EC_CCANCEL
    jmp short .arm
.ok:
    mov al, EC_COK
.arm:                           ; **THE PRESS ONLY ARMS** (SPEC.md 13.8.3/
    xor ah, ah                  ; 13.8.4). Both of these want the release: Ok
    call os88ui_arm             ; commits an address and Cancel throws typing
    call ec_setdown             ; away, and neither is a 13.6 control with a
    jmp short .out              ; safe prefix action - so a mis-aimed press is
                                ; cancelled by sliding off, which it could not
                                ; be while it acted on the way down
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_setdown - AL = the button drawn PRESSED (0 = neither)
; Draws only on a CHANGE, so a pointer moving inside the button it pressed
; costs one compare and no pixels (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
ec_setdown:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp al, [ec_down]
    je .out
    mov [ec_down], al
    call ec_layout              ; DI = content left, BP = content top
    mov ax, di
    add ax, EC_B1X
    mov bx, bp
    add bx, EC_BY
    mov si, ec_s_ok
    mov cl, EC_COK
    call ec_btn
    mov ax, di
    add ax, EC_B2X
    mov bx, bp
    add bx, EC_BY
    mov si, ec_s_cancel
    mov cl, EC_CCANCEL
    call ec_btn
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_bhit - which button is the SCREEN point (CX,DX) in? AL = 0 for neither
; in:  CX = x, DX = y; DI = content left and BP = content top, from ec_layout
; -----------------------------------------------------------------------------
ec_bhit:
    push bx
    push cx
    push dx
    mov ax, dx
    sub ax, bp
    cmp ax, EC_BY
    jb  .none
    cmp ax, EC_BY + EC_BH
    jae .none
    mov ax, cx
    sub ax, di
    cmp ax, EC_B1X
    jb  .none
    cmp ax, EC_B1X + EC_BW
    jb  .ok
    cmp ax, EC_B2X
    jb  .none
    cmp ax, EC_B2X + EC_BW
    jae .none
    mov al, EC_CCANCEL
    jmp short .out
.ok:
    mov al, EC_COK
    jmp short .out
.none:
    xor al, al
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ec_up - W_ONMOUSEUP (SPEC.md 13.7): the two buttons fire HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = the window
; -----------------------------------------------------------------------------
ec_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov bl, al
    call ec_layout
    call ec_bhit                ; ...against a FRESH hit at the release point
    cmp al, bl
    jne .cancel
    mov byte [ec_down], 0       ; NOT ec_setdown: both paths below either
                                ; repaint the window or destroy it, and drawing
                                ; this button upright first is
                                ; PERFORMANCE.md's double-draw
    cmp al, EC_COK
    je .ok
    call ec_close
    jmp short .out
.ok:
    call ec_apply
    jmp short .out
.cancel:
    xor al, al                  ; slid off: thought better of, nothing fires
    call ec_setdown
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_drag - W_ONDRAG (SPEC.md 13.8.2): keep what is drawn pressed equal to
; what would fire. Same question ec_up asks, one pass early.
; -----------------------------------------------------------------------------
ec_drag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov bl, al
    call ec_layout
    call ec_bhit
    cmp al, bl
    je .set
    xor al, al
.set:
    call ec_setdown             ; ...which draws only if that CHANGED
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- ec_focus - CX = the field that gets it, the others lose it -------------
ec_focus:
    push ax
    push bx
    push cx
    push si
    push cx
    xor cx, cx
.l:
    call ec_blk
    mov byte [si+LN_FOCUS], 0
    inc cx
    cmp cx, EC_NF
    jb .l
    pop cx
    call ec_blk
    mov byte [si+LN_FOCUS], 1
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- ec_focused - out CX = which field has it (0 if none does) --------------
ec_focused:
    push ax
    push bx
    push si
    xor cx, cx
.l:
    call ec_blk
    cmp byte [si+LN_FOCUS], 0
    jne .got
    inc cx
    cmp cx, EC_NF
    jb .l
    xor cx, cx
.got:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_key - W_ONKEY: AL = ascii, AH = scan, SI = the window
;
; The field takes what it knows; Tab, Return and Escape come back here, which
; is os88line.inc's whole contract - only the caller knows whether Return means
; Ok (SPEC.md 20.5.1's shape, one level up).
; -----------------------------------------------------------------------------
ec_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call ec_layout
    cmp al, 13
    je  .ret
    cmp al, 27
    je  .esc
    cmp al, 9
    je  .tab
    call ec_focused
    call ec_blk
    call os88line_key
    jc  .out                    ; not the field's: nothing here wants it
    call os88line_draw          ; ...and only the field it changed is redrawn
    jmp short .out
.tab:
    call ec_focused
    inc cx
    cmp cx, EC_NF
    jb  .setf
    xor cx, cx
.setf:
    call ec_focus
    call ec_repaint
    jmp short .out
.ret:
    call ec_apply
    jmp short .out
.esc:
    call ec_close
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_repaint - the window's content again (the kernel does not repaint after a
;              callback returns, SPEC.md 31.9's rule one window over)
; -----------------------------------------------------------------------------
ec_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [ec_win]
    or  bx, bx
    jz  .out
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov si, ax
    mov di, dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, si
    mov bx, di
    mov cx, si
    add cx, EC_FW - 3           ; the content is (w-2) x (h-TITLE_H-1), so its
    mov dx, di                  ; last column and row are these
    add dx, EC_FH - TITLE_H - 2
    call OSAPI_GFX_FILL
    mov si, [ec_win]
    call ec_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_close - take the window down. The close box does exactly this by itself
;            (an unbound window's box is wm_hide, SPEC.md 38), so this is what
;            Cancel and Escape do and nothing more.
; -----------------------------------------------------------------------------
ec_close:
    push bx
    mov bx, [ec_win]
    or  bx, bx
    jz  .out
    call OSAPI_WM_HIDE
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; eth_cfg_reap - the window goes, record and all
;
; DSV_CPCLOSE and DRVV_DETACH both call it, and the second is why it exists:
; the record's dispatcher is OURS, so a window left on screen by a detaching
; driver is a far pointer into a heap claim the kernel is about to hand to
; somebody else. Hiding is not enough - the slot stays used with our segment
; in its W_SEG (OSAPI_WM_DESTROY's own note).
; -----------------------------------------------------------------------------
eth_cfg_reap:
    push ax
    push bx
    mov bx, [ec_win]
    or  bx, bx
    jz  .out
    call OSAPI_WM_HIDE
    call OSAPI_WM_DESTROY
    mov word [ec_win], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_apply - Ok: parse what was typed and make it live
;
; **THE ADDRESS AND THE MASK ARE REQUIRED AND THE OTHER TWO ARE NOT.** A
; machine on a LAN segment with no router and no name server is an ordinary
; thing - it can still reach everything on its own subnet by address, which is
; exactly the crossover-cable case this window exists for - so an empty Router
; or Name is stored as 0.0.0.0 and ip_route simply never leaves the subnet.
; A malformed one is refused rather than ignored, because a typo that silently
; became 0.0.0.0 would look like the feature not working.
; -----------------------------------------------------------------------------
ec_apply:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [ec_mode], ECM_MAN
    jne .auto

    mov si, ec_t0
    mov di, ec_ip
    call ip_parse
    jc  .badip
    mov si, ec_t1
    mov di, ec_mask
    call ip_parse
    jc  .badmask
    mov si, ec_t2
    mov di, ec_gw
    call ec_optional
    jc  .badgw
    mov si, ec_t3
    mov di, ec_dns
    call ec_optional
    jc  .baddns

    mov si, ec_ip               ; ...and only now does anything change
    mov di, eth_ip
    mov cx, 16
    call mem_copy
    mov byte [eth_mode], ECM_MAN
    mov byte [dhcp_st], DH_BOUND
    mov word [dhcp_tmo], 0
    call arp_flush              ; the router may be a different box now, and a
                                ; cached MAC for it is a black hole
    jmp short .live
.auto:
    mov byte [eth_mode], ECM_DHCP
    mov di, eth_ip              ; the old address goes with the mode: a machine
    mov cx, 16                  ; that is asking has no address yet, and a page
    call mem_zero               ; still showing the last one is a page that
                                ; says it succeeded
    call arp_flush              ; ...and BEFORE the exchange, not after: the
                                ; server is reached through the gateway entry
                                ; this is throwing away
    call eth_dhcp_wait          ; and WAIT for an address: no package is
                                ; polling here, so nothing else would ever
                                ; drain the reply (SPEC.md 72.2.1)
.live:
    mov byte [ec_dirty], 1      ; ...remembered when the PANEL closes (31.8)
    call ec_close
    jmp short .out
.badip:
    mov word [ec_msg], ec_s_badip
    jmp short .say
.badmask:
    mov word [ec_msg], ec_s_badmask
    jmp short .say
.badgw:
    mov word [ec_msg], ec_s_badgw
    jmp short .say
.baddns:
    mov word [ec_msg], ec_s_baddns
.say:
    call ec_repaint             ; the window STAYS UP: a refusal the user
                                ; cannot see is a window that did nothing
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- ec_optional - an empty field is 0.0.0.0; anything else must parse ------
ec_optional:
    cmp byte [si], 0
    jne .parse
    push cx
    mov cx, 4
    call mem_zero
    pop cx
    clc
    ret
.parse:
    call ip_parse
    ret

; --- arp_flush - forget every hardware address ------------------------------
arp_flush:
    push cx
    push di
    mov di, arp_used
    mov cx, ARP_N
    call mem_zero
    pop di
    pop cx
    ret

; =============================================================================
; THE FILE
; =============================================================================

; -----------------------------------------------------------------------------
; ec_vol_bank / ec_vol_back - stand on the system volume's root and come back
; out: bank: CF=1 = we could not get there and nothing should be attempted
;
; The kernel's own drv_vol_bank/drv_vol_back (SPEC.md 51.5.2) in a package's
; vocabulary, and for its reason: a file operation resolves in the CURRENT
; directory, so going somewhere to read a settings file and not coming back
; silently moves whatever the user was working on.
; -----------------------------------------------------------------------------
ec_vol_bank:
    push ax
    call OSAPI_FILE_HERE        ; DX = our cluster, BL = our drive
    mov [ec_bdrv], bl
    mov [ec_bclus], dx
    call OSAPI_VOL_SYS          ; BL = the volume we BOOTED from
    xor dx, dx                  ; ...and its ROOT
    call OSAPI_FILE_GOTO
    pop ax
    ret

ec_vol_back:
    push ax
    push bx
    push dx
    pushf                       ; the CALLER's result is in CF and a failed
    mov bl, [ec_bdrv]           ; restore must not overwrite it - drv_vol_back's
    mov dx, [ec_bclus]          ; own rule (SPEC.md 51.5.2)
    call OSAPI_FILE_GOTO
    popf
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_cfg_load - read ETHER.CFG, if there is one (DRVV_READY)
; out: CF=0 and the mode and addresses are set; CF=1 = there is no file, or it
;      is not one of ours, and the caller does DHCP
; -----------------------------------------------------------------------------
eth_cfg_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call ec_vol_bank
    jc  .back                   ; the GOTO failed - and dsk_chdir's failure path
                                ; can move the globals it would be recovered
                                ; from (SPEC.md 38.10), so come back anyway
    push ds
    pop es
    mov si, ec_s_file
    mov bx, ec_fbuf
    mov cx, EC_F_SIZE           ; a version 1 file is SHORTER and the read
    xor dx, dx                  ; answers with what it found, which is what
    call OSAPI_FILE_READ        ; tells the two apart below
    push ax
    call ec_vol_back
    pop ax
    jc  .no
    cmp word [ec_fbuf+EC_F_MAGIC], EC_MAGIC
    jne .no
    cmp byte [ec_fbuf+EC_F_VER], EC_VER
    jne .v1
    cmp ax, EC_F_SIZE
    jne .no
    call ec_buf_take            ; the rung FIRST and outside every exit below:
    jmp short .mode             ; it is wanted whether the address is manual,
.v1:                            ; automatic or absent, and .no is three of
    cmp byte [ec_fbuf+EC_F_VER], 1  ; those four paths
    jne .no
    cmp ax, EC_F_SIZE1
    jne .no
.mode:
    mov al, [ec_fbuf+EC_F_MODE]
    cmp al, ECM_MAN
    jne .no                     ; stored as Automatic: nothing to restore, and
                                ; DHCP is what a fresh machine does anyway
    mov [eth_mode], al
    mov si, ec_fbuf + EC_F_IP
    mov di, eth_ip
    mov cx, 16
    call mem_copy
    mov si, eth_ip              ; ...and a stored 0.0.0.0 is not an address
    call ip_zero
    jz  .drop
    clc
    jmp short .out
.drop:
    mov byte [eth_mode], ECM_DHCP
    jmp short .no
.back:
    call ec_vol_back
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_cfg_save - write it, if anything changed (DSV_CPCLOSE)
;
; It is SILENT about failing, and that is deliberate: the panel has just been
; destroyed, there is nowhere left to say it, and what was actually asked for -
; an address to test with - already happened when Ok was pressed. A machine
; with the wrong disk in A: loses the memory of the setting and not the
; setting.
; -----------------------------------------------------------------------------
eth_cfg_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [ec_dirty], 0
    je  .out
    mov byte [ec_dirty], 0
    mov word [ec_fbuf+EC_F_MAGIC], EC_MAGIC
    mov byte [ec_fbuf+EC_F_VER], EC_VER
    mov al, [eth_mode]
    mov [ec_fbuf+EC_F_MODE], al
    mov si, eth_ip
    mov di, ec_fbuf + EC_F_IP
    mov cx, 16
    call mem_copy
    call ec_buf_put
    call ec_vol_bank
    jc  .back
    push ds
    pop es
    mov si, ec_s_file
    mov bx, ec_fbuf
    mov cx, EC_F_SIZE
    xor dx, dx
    call OSAPI_FILE_WRITE
.back:
    call ec_vol_back
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ec_buf_take / ec_buf_put - the stored rung <-> [sk_want]
;
; **STORED IN KB AND NOT IN BYTES**, so the file says 8 where the driver says
; 8192. That is not tidiness: the byte is one byte, and a future rung that is
; not a power of two in bytes is still a whole number of KB. An unrecognised
; value is AUTO rather than a refusal - the same rule the version gets, one
; field down.
; -----------------------------------------------------------------------------
ec_buf_take:
    push ax
    push cx
    mov al, [ec_fbuf+EC_F_BUF]
    xor ah, ah
    cmp al, SK_RXCAP0 / 1024
    je  .ok
    cmp al, SK_RXMID / 1024
    je  .ok
    cmp al, SK_RXMAX / 1024
    je  .ok
    xor ax, ax                  ; anything else, 0 included, is auto
.ok:
    mov cl, 10
    shl ax, cl
    mov [sk_want], ax
    pop cx
    pop ax
    ret

ec_buf_put:
    push ax
    push cx
    mov ax, [sk_want]
    mov cl, 10
    shr ax, cl
    mov [ec_fbuf+EC_F_BUF], al
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_buf_apply - the rung the file asked for, once the file has been read
;
; DRVV_READY and not DRVV_ATTACH, and the ordering is the whole point:
; sk_claim runs at ATTACH because a card with no rings cannot be brought up at
; all, and ETHER.CFG cannot be read that early - the volume the driver booted
; from is not mounted yet. So the ladder's automatic answer is taken first and
; the user's stored choice replaces it here, before any package has had the
; chance to open a socket.
; -----------------------------------------------------------------------------
eth_buf_apply:
    push ax
    mov ax, [sk_want]
    or  ax, ax
    jz  .out                    ; auto: the ladder's answer stands
    cmp ax, [sk_rxcap]
    je  .out                    ; ...and so does an answer that already agrees
    call sk_reclaim
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_cp_close - DSV_CPCLOSE: the panel has gone
; -----------------------------------------------------------------------------
eth_cp_close:
    call eth_cfg_reap
    call eth_cfg_save
    ret

; --- strings and state --------------------------------------------------------
ec_s_file:   db 'ETHER.CFG', 0
ec_s_auto:   db 'Automatic', 0
ec_s_man:    db 'Manual', 0
ec_s_ok:     db 'Ok', 0
ec_s_cancel: db 'Cancel', 0
ec_s_ready:  db 'Needs Address and Subnet', 0
                                ; **24 CHARACTERS**, and every string on this
                                ; window is measured: font_str clips to the
                                ; SCREEN and not to the window (SPEC.md 5), so
                                ; a message one character too long paints over
                                ; the frame and out the other side. The content
                                ; is 214px from EC_LX = 2, which is 26 cells
ec_s_badip:  db 'Address?', 0
ec_s_badmask: db 'Subnet?', 0
ec_s_badgw:  db 'Router?', 0
ec_s_baddns: db 'Name server?', 0
ec_s_l0:     db 'Address', 0
ec_s_l1:     db 'Subnet', 0
ec_s_l2:     db 'Router', 0
ec_s_l3:     db 'Name', 0

ec_labels:   dw ec_s_l0, ec_s_l1, ec_s_l2, ec_s_l3
ec_blks:     dw ec_f0, ec_f1, ec_f2, ec_f3
ec_bufs:     dw ec_t0, ec_t1, ec_t2, ec_t3

EC_TLEN     equ 16              ; 15 characters and the NUL

ec_win:     dw 0
ec_mode:    db ECM_DHCP         ; what the WINDOW is showing...
ec_dirty:   db 0
ec_msg:     dw ec_s_ready
ec_cx:      dw 0
ec_cy:      dw 0
ec_down:    db 0             ; which button is drawn PRESSED (SPEC.md 13.8.4)
ec_rect:    dw 0, 0, 0, 0
ec_bdrv:    db 0
ec_bclus:   dw 0
ec_ip:      times 4 db 0        ; the parsed edit, staged so that a bad field
ec_mask:    times 4 db 0        ; further down cannot leave half of it applied
ec_gw:      times 4 db 0
ec_dns:     times 4 db 0
ec_t0:      times EC_TLEN db 0
ec_t1:      times EC_TLEN db 0
ec_t2:      times EC_TLEN db 0
ec_t3:      times EC_TLEN db 0
ec_f0:      times OS88LINE_SZ db 0
ec_f1:      times OS88LINE_SZ db 0
ec_f2:      times OS88LINE_SZ db 0
ec_f3:      times OS88LINE_SZ db 0
ec_fbuf:    times EC_F_SIZE db 0
