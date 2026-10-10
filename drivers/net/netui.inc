; =============================================================================
; netui.inc - NET.DRV's Control Panel page (SPEC.md 31.9)
;
; The page exists exactly while this driver is attached, which falls out of
; DSV_CPNAME being published rather than being arranged: the kernel builds
; cp_items from the five static rows plus one per loaded driver that publishes
; a name, and clearing the publication at detach is what makes the row go.
;
; WHAT IT SAYS IS THE DIAGNOSIS, and that is the whole design. docs/plans/completed/NET-PLAN.md
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
; net_pgent - THE PAGE VERBS' FRAME, written once
; in:  called as the FIRST instruction of a DSV_CP* verb, so its return address
;      is that verb's body; DI = pane left, DX = pane top (SPEC.md 31.1)
; out: the body has run; every register as the kernel had it
;
; All four page verbs save the same seven registers and bank the same pane
; origin before they do anything, and each used to spell that out - 23 bytes
; a verb. So the BODIES here spend any register they like, and a body may
; tail-jump into another verb (net_cp_up's repaint is `jmp net_cp_paint`):
; that verb's own pgent nests inside this one and returns to it.
; -----------------------------------------------------------------------------
net_pgent:
    ; STKBALANCE-OK: it takes its own return address as the body to CALL, so the verb's `ret` comes back here and this `ret` goes to the verb's caller
    pop word [net_pgbody]       ; ...taken at once, so a nested verb may
    push ax                     ; overwrite it: this one has already been
    push bx                     ; called by the time that can happen
    push cx
    push dx
    push si
    push di
    push bp
    mov [net_px], di
    mov [net_py], dx
    call [net_pgbody]
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
net_cp_paint:
    call net_pgent

    ; --- the heading, the same string the list row carries --------------------
    mov si, net_cpname
    mov bx, NP_HY
    call net_lab

    ; --- row 1: which port ---------------------------------------------------
    ; It always found one, or there would be no driver to draw this (above).
    mov si, s_port
    mov bx, NP_Y0               ; ...and BX stays the row: net_txt reads it
    call net_lab                ; and nothing below it writes it
    mov ax, [lp_base]
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
    call net_val
    jmp short .row3
.nolink:
    cmp byte [net_lost_f], 0    ; not merely absent - it DIED, and where
    je  .st
    mov si, s_slost             ; ...and WHICH COMMAND died on it. It used to
    call net_val                ; be `Lost at LBA <n>`, which is block mode's
    mov al, [net_lastcmd]       ; language - a redirector has no LBA, so the
    or  al, al                  ; number was whatever DX happened to hold -
    jz  .btn                    ; AND it was drawn at NP_VX+48, INSIDE an
    mov [net_cbuf], al          ; 11-character string, so it overprinted the
    mov si, net_cbuf            ; word it was meant to follow. The field photo
    mov cx, NP_VX + 64          ; reads `Lost a2 LBA`. net_cbuf+1 is its NUL,
    call net_txt                ; and nothing ever writes it

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
    call net_val

    ; --- the button ----------------------------------------------------------
.btn:
    jmp net_draw_btn

; -----------------------------------------------------------------------------
; DSV_CPCLICK - DI = pane left, DX = pane top, CX/BX = the click
; -----------------------------------------------------------------------------
net_cp_click:
    call net_pgent

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
    jmp net_setdown
.out:
    ret

; -----------------------------------------------------------------------------
; net_lab / net_val / net_txt - a string at a row
;   net_lab: SI = string, BX = y offset            -> the label column
;   net_val: SI = string, BX = y offset            -> the value column
;   net_txt: SI = string, BX = y offset, CX = x offset
; clobber: AX, CX, DX - every caller is a page verb, whose frame is net_pgent's
; -----------------------------------------------------------------------------
net_lab:
    mov cx, NP_LX
    jmp short net_txt
net_val:
    mov cx, NP_VX
net_txt:
    add cx, [net_px]
    mov dx, [net_py]
    add dx, bx
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    ret

; -----------------------------------------------------------------------------
; net_hex - AX as four hex digits in the value column. BX = y offset.
; -----------------------------------------------------------------------------
net_hex:
    mov si, net_buf
    mov ch, 4
.d:
    mov cl, 4
    rol ax, cl                  ; the top nibble down; four of these leave AX
    push ax                     ; exactly as it arrived
    and al, 0x0F
    add al, 0x90                ; 0..9 -> '0'..'9' and 10..15 -> 'A'..'F':
    daa                         ; the decimal-adjust digit trick, which is
    adc al, 0x40                ; the compare-and-add written as arithmetic
    daa
    mov [si], al
    inc si
    pop ax
    dec ch
    jnz .d
    mov byte [si], 0
    mov si, net_buf
    jmp short net_val

; -----------------------------------------------------------------------------
; net_brect - the button's rect in SCREEN coordinates, from the pane-relative
;             constants above and [net_px]/[net_py]
; out: BX -> {x1,y1,x2,y2} inclusive, os88ui_btn's own convention; AX spent
; -----------------------------------------------------------------------------
net_brect:
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
    ret

; -----------------------------------------------------------------------------
; net_draw_btn - Connect / Disconnect, through the SHARED CONTROL
;
; SPEC.md 20.5.1's os88ui_btn rather than a frame and a font_str of our own:
; a feature added to the button - a pressed state, a focus ring, a different
; disabled treatment - reaches this page with the rest of the system, and
; SPEC.md 47's greying is taken once in one place instead of being remembered
; here. It also centres the label in both axes.
;
; OS88UI_FILL, AND IT IS NOT OPTIONAL SINCE 13.8.4. It used to be left off,
; correctly, because the pane had been whited on every path that reached this
; - and a release path reaches it over a PRESSED button, whose interior is
; black. Without the fill the "upright" redraw leaves that black interior and
; letters a black caption onto it: measured, 147 lit pixels while held and
; **0** after sliding off. The rule generalises to every control that gains a
; down state: OS88UI_FILL asks "can this be drawn a second time without the
; ground being repainted first?", not "is my background white".
;
; IT NEVER GREYS - see the header. Both states are actions the user may take
; and neither has a predicate that could refuse one, which is SPEC.md 47
; rule 3: the only test of "is there a partner" is asking, so Connect asks and
; the Link row answers.
; -----------------------------------------------------------------------------

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; A Control Panel page draws its buttons one at a time, so the record is
; pointed at whichever rect the caller staged. The GESTURE here is the
; panel's own cp_ctl/cp_pgprobe probe (SPEC.md 13.8.4), which is already
; release-fired; what changed is that the DRAW goes through the control.
nu_btlbl: dw 0
nu_btflg: dw 0
    OS88UI_BTNREC nu_btrec, 0, nu_btlbl, nu_btflg, 1

net_draw_btn:                   ; clobbers AX, BX, SI, DI (a page verb's)
    call net_brect              ; BX = the rect
    mov si, s_conn
    cmp byte [net_state], NS_LINKED
    jne .lab
    mov si, s_disc
.lab:
    mov di, OS88UI_FILL         ; live, no default ring - see the header on
    cmp byte [net_down], 0      ; why the fill is not optional - except
    je .up                      ; PRESSED, which every path that draws this
    mov di, OS88UI_DOWN         ; button asks about, so the glass follows
.up:                            ; [net_down] on a full pane repaint as much as
                                ; on an edge (SPEC.md 13.8)
    mov [nu_btlbl], si          ; THE ONE CONTROL (SPEC.md 20.5.1.3): the
    mov [nu_btflg], di          ; record takes this button's label, flags and
    mov [nu_btrec+OS88UI_BT_RECTS], bx  ; rect, and the picture is identical
    mov bx, nu_btrec
    mov al, 1
    call os88ui_btn             ; CALLED and not jumped to, though a jump
    ret                         ; would be a byte less: t_btnrules counts
                                ; button call sites by their `call`, and a
                                ; tail jump is how a caller hides from it

; -----------------------------------------------------------------------------
; net_bhit - is the pane-relative point in the button?
; in:  CX = pane-relative x, BX = pane-relative y
; out: CF = 0 inside, CF = 1 outside; AX, BX, CX and DX spent
;
; net_brect answers in SCREEN coordinates because that is what os88ui_btn
; draws in, and the page ABI hands a click PANE-relative (SPEC.md 31.1), so
; the point is lifted rather than the rect lowered - one conversion, at the
; asking end, where a second copy of the rect would be a second description.
; -----------------------------------------------------------------------------
net_bhit:
    add cx, [net_px]            ; both lifted to the screen
    mov dx, [net_py]
    add dx, bx
    call net_brect              ; BX -> the rect; CX/DX are not touched
    jmp os88ui_bhit             ; CF = 0 inside

; -----------------------------------------------------------------------------
; net_setdown - AL = 1 if the button is drawn PRESSED, 0 if upright
; in:  AL as above; the gfx lock is held and [net_px]/[net_py] are current
;
; Draws only on a CHANGE, so a pointer moving inside the button it pressed
; costs one compare and no pixels - which is what makes an edge arriving per
; mouse packet affordable (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
net_setdown:
    cmp al, [net_down]
    je .out
    mov [net_down], al
    jmp short net_draw_btn
.out:
    ret

; -----------------------------------------------------------------------------
; DSV_CPUP - the release (SPEC.md 13.8.4). Same arguments as DSV_CPCLICK.
; -----------------------------------------------------------------------------
net_cp_up:
    call net_pgent
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    call net_bhit               ; still on it?
    jc .up                      ; no: thought better of, and nothing fires
    mov byte [net_down], 0      ; NOT net_setdown: the repaint below draws the
                                ; whole pane, this button upright with it, and
                                ; drawing it upright first is PERFORMANCE.md's
                                ; double-draw with the button as the flash
    mov bx, net_connect         ; THE STATE DECIDES BEFORE THE WAIT, as the
    cmp byte [net_state], NS_LINKED ; button the user pressed was drawn from it
    jne .claim
    mov bx, net_drop
.claim:
    call net_fclaim             ; THE WIRE, for the whole handshake: a
    jc  .redraw                 ; package's worker may be inside a socket
    call bx                     ; verb on it (netsock.inc), and the panel is
    mov byte [net_busy], 0      ; neither of the two gates. Busy: the page
                                ; redraws unchanged and the user clicks again
.redraw:
    mov al, CWHITE              ; white the pane, then paint it - the kernel
    call OSAPI_SET_COLOR        ; does not repaint after a click returns
    mov ax, [net_px]            ; (SPEC.md 31.9)
    mov di, ax
    mov bx, [net_py]
    mov dx, bx
    mov cx, ax
    add cx, NP_W
    push dx
    add dx, NP_H
    call OSAPI_GFX_FILL
    pop dx
    jmp net_cp_paint            ; its own frame nests inside ours
.up:
    xor al, al
    jmp short net_setdown
.out:
    ret

; -----------------------------------------------------------------------------
; DSV_CPDRAG - the tracking edge (SPEC.md 13.8.1/13.8.4)
; -----------------------------------------------------------------------------
net_cp_drag:
    call net_pgent
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    call net_bhit
    sbb al, al                  ; CF = 1, off it: nothing is down (0) ...
    inc al                      ; ...and CF = 0, on it: down (1)
    jmp short net_setdown       ; ...which draws only if that CHANGED
.out:
    ret

; --- the page's strings ------------------------------------------------------
s_port:     db 'Port', 0
s_state:    db 'Link', 0
net_label   equ s_state         ; ...and the VOLUME's name is the same word,
                                ; so it is the same bytes (net.asm)
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
net_buf:    db 0,0,0,0,0      ; net_hex's four digits and a NUL
net_rc:     dw 0,0,0,0       ; the button's rect, built by net_brect
net_down:   db 0             ; 1 = it is drawn PRESSED (SPEC.md 13.8.4).
                             ; net_setdown is its one writer and net_draw_btn
                             ; is its one reader, so a repaint mid-press shows
                             ; the same thing an edge does
net_pgbody: dw 0             ; net_pgent's: the verb body it is running
