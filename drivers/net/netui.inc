; =============================================================================
; netui.inc - NET.DRV's Control Panel page (SPEC.md 31.9)
;
; The page exists exactly while this driver is attached, which falls out of
; DSV_CPNAME being published rather than being arranged: the kernel builds
; cp_items from the five static rows plus one per loaded driver that publishes
; a name, and clearing the publication at detach is what makes the row go.
;
; WHAT IT SAYS IS THE DIAGNOSIS, and that is the whole design. docs/NET-PLAN.md
; 1.4.4: a port with nothing plugged into it and a port with a PRINTER on it
; read as the same constant status byte, and only a live partner reads as
; whatever we ask it to. So "not connected" on its own would send the user to
; look at the wrong end of the cable, and the page names the port it found
; before it says anything about the far end:
;
;   Port  0378          the base a latch probe answered at
;   Link  No partner    ...and nobody home on it
;   Link  Linked        a partner answered the magic, and here is its size
;
; **NOTHING ON THIS PAGE EVER GREYS, and that is SPEC.md 47 rule 3 rather than
; an omission.** There is no state to grey FOR. `No parallel port` was a third
; Link value here and it is unreachable by construction: DRVV_ATTACH refuses
; when no port answers (the sound driver's shape - DRVE_HW, reported on the
; Drivers page), so a machine with no port has no driver and therefore no page
; at all. And whether a PARTNER is there cannot be greyed on either, because
; rule 3's other half is that the only honest test is doing the thing: Connect
; does it and the Link row reports. A branch that can only be entered by a
; state the attach path forbids is dead code that looks tested, so it is gone.
;
; A PAGE DRAWS ITSELF AND THE KERNEL DOES NOT REPAINT AFTER A CLICK RETURNS
; (SPEC.md 31.9), so the click path repaints the pane itself - hd_page_repaint's
; shape, and for the same reason.
; =============================================================================

; THE PANE IS 221 x 132 PX - 27 CHARACTERS BY 14 ROWS - and every string here
; fits it, which is page.inc's rule and for page.inc's reason: font_str clips
; to the SCREEN and not to the pane (SPEC.md 5), so a caption one character
; too long does not stop at the edge, it paints over the panel's border and
; out the other side. The widest thing on this page is 'No partner' in the
; value column at x = 48: 48 + 80 = 128 of 221.
NP_W        equ 221             ; the pane, as ctrl.inc lays it out
NP_H        equ 131             ; ...and its LAST ROW: the click repaint clears
                                ; the whole pane, so a short one leaves a stale
                                ; strip along the bottom and nothing says so
NP_LX       equ 2               ; text inset - CP_PMX, the panel's own
NP_HY       equ 4               ; the heading row, HDP_HY's
NP_Y0       equ 20              ; the first value row
NP_ROW      equ 12
NP_VX       equ 48              ; where a value column starts

; --- the button, ONE definition, PANE-RELATIVE -------------------------------
; The draw converts these to screen coordinates for os88ui_btn and the hit
; test compares the click against them directly, so the drawn control and the
; clickable control read the same four numbers.
;
; THAT IS NOT A TIDINESS ARGUMENT, it is the bug this page shipped with. The
; hit test compared the PANE-RELATIVE click the page ABI hands it (SPEC.md
; 31.1: DI = pane left, DX = pane top, CX = pane-relative x, BX =
; pane-relative y) against a rect built from [net_px]/[net_py] - absolute
; screen coordinates. CX was 0..220 and the test wanted ~264, so the first
; `jb` took the exit every time and **the button did nothing at all**, on a
; page that drew perfectly. It reached the field as "Connect does nothing, but
; unloading and reloading the driver connects".
NP_BX       equ 2               ; the button
NP_BY       equ 66
NP_BW       equ 92
NP_BH       equ 16

NS_NOPORT   equ 0               ; no parallel port answered at all
NS_PORT     equ 1               ; a port, but nobody home on it
NS_LINKED   equ 2               ; a partner answered the magic

; -----------------------------------------------------------------------------
; DSV_CPPAINT - DI = pane left, DX = pane top
; -----------------------------------------------------------------------------
net_cp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [net_px], di
    mov [net_py], dx

    ; --- the heading, the same string the list row carries --------------------
    mov si, net_cpname
    mov bx, NP_HY
    call net_lab

    ; --- row 1: which port ---------------------------------------------------
    ; It always found one, or there would be no driver to draw this (above).
    mov si, s_port
    mov bx, NP_Y0
    call net_lab
    mov ax, [lp_base]
    mov bx, NP_Y0
    call net_hex

    ; --- row 2: what state the link is in ------------------------------------
    mov si, s_state
    mov bx, NP_Y0 + NP_ROW
    call net_lab
    mov si, s_sport
    cmp byte [net_state], NS_LINKED
    jne .nolink
    mov si, s_slinked
.st:
    mov bx, NP_Y0 + NP_ROW
    call net_val
    jmp short .row3
.nolink:
    cmp byte [net_lost_f], 0    ; not merely absent - it DIED, and where
    je  .st
    mov si, s_slost             ; ...and WHICH COMMAND died on it. It used to
    mov bx, NP_Y0 + NP_ROW      ; be `Lost at LBA <n>`, which is block mode's
    call net_val                ; language - a redirector has no LBA, so the
    mov al, [net_lastcmd]       ; number was whatever DX happened to hold -
    or  al, al                  ; AND it was drawn at NP_VX+48, INSIDE an
    jz  .btn                    ; 11-character string, so it overprinted the
    mov [net_cbuf], al          ; word it was meant to follow. The field photo
    mov byte [net_cbuf+1], 0    ; reads `Lost a2 LBA`.
    mov si, net_cbuf
    mov bx, NP_Y0 + NP_ROW
    mov cx, NP_VX + 64
    call net_txt

    ; --- row 3: what the far side is serving, when there is one --------------
    ; It used to be `Size <sectors>`, and a redirected volume HAS NO SECTORS
    ; (SPEC.md 62.9), so that row printed a 0 that meant nothing at all. How
    ; big the far side is now belongs to FSV_DFREE and shows up where every
    ; other volume's does - the Disk window's status line (SPEC.md 22.7).
.row3:
    cmp byte [net_state], NS_LINKED
    jne .btn
    mov si, s_mode
    mov bx, NP_Y0 + 2*NP_ROW
    call net_lab
    mov si, s_files
    test byte [net_flags], 1
    jz  .swr
    mov si, s_filesro
.swr:
    mov bx, NP_Y0 + 2*NP_ROW
    mov cx, NP_VX
    call net_txt

    ; --- the button ----------------------------------------------------------
.btn:
    call net_draw_btn

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; DSV_CPCLICK - DI = pane left, DX = pane top, CX/BX = the click
; -----------------------------------------------------------------------------
net_cp_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [net_px], di
    mov [net_py], dx

    ; --- is it in the button? -----------------------------------------------
    ; CX and BX are PANE-RELATIVE (SPEC.md 31.1) and so are these four
    ; constants, which is the whole of the fix: they used to be compared
    ; against a rect built from the pane's ABSOLUTE origin, so the test could
    ; never pass and the button did nothing. See the header above NP_BX.
    cmp cx, NP_BX
    jb  .out
    cmp cx, NP_BX + NP_BW
    jae .out
    cmp bx, NP_BY
    jb  .out
    cmp bx, NP_BY + NP_BH
    jae .out

    cmp byte [net_state], NS_LINKED
    je  .disc
    call net_connect            ; may fail: the page will say so
    jmp short .redraw
.disc:
    call net_drop
.redraw:
    mov di, [net_px]
    mov dx, [net_py]
    call net_pane_clear
    call net_cp_paint
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
; net_pane_clear - white the pane. DI = left, DX = top.
; -----------------------------------------------------------------------------
net_pane_clear:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, di
    mov bx, dx
    mov cx, di
    add cx, NP_W
    add dx, NP_H
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_lab / net_val / net_txt - a string at a row
;   net_lab: SI = string, BX = y offset            -> the label column
;   net_val: SI = string, BX = y offset            -> the value column
;   net_txt: SI = string, BX = y offset, CX = x offset
; -----------------------------------------------------------------------------
net_lab:
    push cx
    mov cx, NP_LX
    call net_txt
    pop cx
    ret

net_val:
    push cx
    mov cx, NP_VX
    call net_txt
    pop cx
    ret

net_txt:
    push ax
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add cx, [net_px]
    mov dx, [net_py]
    add dx, bx
    call OSAPI_FONT_STR
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_hex - AX as four hex digits in the value column. BX = y offset.
; -----------------------------------------------------------------------------
net_hex:
    push ax
    push bx
    push cx
    push si
    push bx                     ; the row, wanted again after the arithmetic
    mov si, net_buf
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl                  ; the top nibble down; four of these leave AX
    pop cx                      ; exactly as it arrived
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    mov [si], al
    inc si
    pop ax
    loop .d
    mov byte [si], 0
    pop bx
    mov si, net_buf
    call net_val
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_dec - AX as decimal in the value column. BX = y offset.
; -----------------------------------------------------------------------------
net_dec:
    push cx
    mov cx, NP_VX
    call net_declba
    pop cx
    ret

; -----------------------------------------------------------------------------
; net_declba - AX as decimal at an arbitrary column. BX = y, CX = x offset.
; -----------------------------------------------------------------------------
net_declba:
    push ax
    push bx
    push cx
    push dx
    push si
    push bx                     ; the row, wanted again after the arithmetic
    push cx                     ; ...and the column
    mov si, net_buf + 8
    mov byte [si], 0
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    inc cx
    or  ax, ax
    jnz .d
    pop cx
    pop bx
    call net_txt
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_brect - the button's rect in SCREEN coordinates, from the pane-relative
;             constants above and [net_px]/[net_py]
; out: BX -> {x1,y1,x2,y2} inclusive, os88ui_btn's own convention
; -----------------------------------------------------------------------------
net_brect:
    push ax
    mov ax, [net_px]
    add ax, NP_BX
    mov [net_rc+0], ax
    add ax, NP_BW - 1
    mov [net_rc+4], ax
    mov ax, [net_py]
    add ax, NP_BY
    mov [net_rc+2], ax
    add ax, NP_BH - 1
    mov [net_rc+6], ax
    mov bx, net_rc
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_draw_btn - Connect / Disconnect, through the SHARED CONTROL
;
; SPEC.md 20.5.1's os88ui_btn rather than a frame and a font_str of our own:
; a feature added to the button - a pressed state, a focus ring, a different
; disabled treatment - reaches this page with the rest of the system, and
; SPEC.md 47's greying is taken once in one place instead of being remembered
; here. It also centres the label in both axes, which is why the +8/+5 this
; routine used to carry are gone.
;
; NO OS88UI_FILL: net_pane_clear has already whited the pane on every path
; that reaches this, so filling again would be PERFORMANCE.md's double-draw
; with the frame as the second layer.
;
; IT NEVER GREYS - see the header. Both states are actions the user may take
; and neither has a predicate that could refuse one, which is SPEC.md 47
; rule 3: the only test of "is there a partner" is asking, so Connect asks and
; the Link row answers.
; -----------------------------------------------------------------------------
net_draw_btn:
    push ax
    push bx
    push si
    push di
    call net_brect              ; BX = the rect
    mov si, s_conn
    cmp byte [net_state], NS_LINKED
    jne .lab
    mov si, s_disc
.lab:
    xor di, di                  ; no flags: live, no fill, no default ring
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

; --- the page's strings ------------------------------------------------------
s_port:     db 'Port', 0
s_state:    db 'Link', 0
s_mode:     db 'Mode', 0     ; ...FOUR characters, like 'Port' and 'Link':
                                ; net_lab and net_txt are two fixed columns
                                ; and a longer label runs into the value
s_sport:    db 'No partner', 0
s_slinked:  db 'Linked', 0
s_slost:    db 'Lost on', 0
s_files:    db 'Files', 0
s_filesro:  db 'Files, read only', 0
s_conn:     db 'Connect', 0
s_disc:     db 'Disconnect', 0
net_cbuf:   db 0,0            ; the dead command's letter, as a string
net_buf:    db 0,0,0,0,0,0,0,0,0
net_rc:     dw 0,0,0,0       ; the button's rect, built by net_brect
