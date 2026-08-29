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
    ; net_bhit, which asks net_brect - the rect the PAINTER draws. It used to
    ; be four constants compared here, which is one description too many even
    ; when they agree: they did not, once, and the button did nothing at all
    ; on a page that drew perfectly. See the header above NP_BX.
    call net_bhit
    jc  .out

    ; ...and the press only ARMS and DRAWS (SPEC.md 13.6/13.8.4). Connect is
    ; a handshake over a cable and Disconnect tears a mounted volume down, so
    ; neither has a safe prefix action and both want the release.
    mov ax, 1
    call os88ui_arm
    mov al, 1
    call net_setdown
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
    add cx, [net_px]
    mov dx, [net_py]
    add dx, bx
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
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
; OS88UI_FILL, AND IT IS NOT OPTIONAL SINCE 13.8.4. It used to be left off,
; correctly, because net_pane_clear had whited the pane on every path that
; reached this - and a release path reaches it over a PRESSED button, whose
; interior is black. Without the fill the "upright" redraw leaves that black
; interior and letters a black caption onto it: measured, 147 lit pixels while
; held and **0** after sliding off. The button does not come back up, it goes
; invisible, which is the failure os88ui.inc's own header describes as reading
; like SPEC.md 47 rule 1 being broken when rule 1 is fine.
;
; The rule generalises to every control that gains a down state: OS88UI_FILL
; asks "can this be drawn a second time without the ground being repainted
; first?", not "is my background white".
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
    mov di, OS88UI_FILL         ; live, no default ring - see the header on
    cmp byte [net_down], 0      ; why the fill is not optional      ; ...except PRESSED, which every path that
    je .up                      ; draws this button asks about, so the glass
    mov di, OS88UI_DOWN         ; follows [net_down] on a full pane repaint as
.up:                            ; much as on an edge (SPEC.md 13.8)
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_bhit - is the pane-relative point in the button?
; in:  CX = pane-relative x, BX = pane-relative y
; out: CF = 0 inside, CF = 1 outside; all registers preserved
;
; net_brect answers in SCREEN coordinates because that is what os88ui_btn
; draws in, and the page ABI hands a click PANE-relative (SPEC.md 31.1), so
; the point is lifted rather than the rect lowered - one conversion, at the
; asking end, where a second copy of the rect would be a second description.
; -----------------------------------------------------------------------------
net_bhit:
    push ax
    push bx
    push cx
    push dx
    mov ax, bx                  ; AX = the pane-relative y
    add cx, [net_px]            ; ...and both lifted to the screen
    add ax, [net_py]
    mov dx, ax
    call net_brect              ; BX -> the rect; CX/DX are not touched
    call os88ui_bhit            ; CF = 0 inside
    pop dx                      ; (pop leaves the flags alone)
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; net_setdown - AL = 1 if the button is drawn PRESSED, 0 if upright
; in:  AL as above; the gfx lock is held and [net_px]/[net_py] are current
; out: nothing (all registers preserved)
;
; Draws only on a CHANGE, so a pointer moving inside the button it pressed
; costs one compare and no pixels - which is what makes an edge arriving per
; mouse packet affordable (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
net_setdown:
    push ax
    cmp al, [net_down]
    je .out
    mov [net_down], al
    call net_draw_btn
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; DSV_CPUP - the release (SPEC.md 13.8.4). Same arguments as DSV_CPCLICK.
; -----------------------------------------------------------------------------
net_cp_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [net_px], di
    mov [net_py], dx
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    call net_bhit               ; still on it?
    jc .up                      ; no: thought better of, and nothing fires
    mov byte [net_down], 0      ; NOT net_setdown: the repaint below draws the
                                ; whole pane, this button upright with it, and
                                ; drawing it upright first is PERFORMANCE.md's
                                ; double-draw with the button as the flash
    cmp byte [net_state], NS_LINKED
    je  .disc
    call net_fclaim             ; THE WIRE, for the whole handshake: a
    jc  .redraw                 ; package's worker may be inside a socket
    call net_connect            ; verb on it (netsock.inc), and the panel is
    mov byte [net_busy], 0      ; neither of the two gates. Busy: the page
    jmp short .redraw           ; redraws unchanged and the user clicks again
.disc:
    call net_fclaim
    jc  .redraw
    call net_drop
    mov byte [net_busy], 0
.redraw:
    mov di, [net_px]
    mov dx, [net_py]
    call net_pane_clear
    call net_cp_paint
    jmp short .out
.up:
    xor al, al
    call net_setdown
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
; DSV_CPDRAG - the tracking edge (SPEC.md 13.8.1/13.8.4)
; -----------------------------------------------------------------------------
net_cp_drag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [net_px], di
    mov [net_py], dx
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    call net_bhit
    mov al, 1
    jnc .set
    xor al, al                  ; off it: nothing is down
.set:
    call net_setdown            ; ...which draws only if that CHANGED
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
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
net_down:   db 0             ; 1 = it is drawn PRESSED (SPEC.md 13.8.4).
                             ; net_setdown is its one writer and net_draw_btn
                             ; is its one reader, so a repaint mid-press shows
                             ; the same thing an edge does
