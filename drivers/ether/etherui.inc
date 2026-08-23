; =============================================================================
; etherui.inc - ETHER.DRV's Control Panel page (SPEC.md 31.9, 72.6)
;
; The page exists exactly while this driver is attached, because it exists by
; publishing DSV_CPNAME and detach clears the publication. It is the FIFTH
; driver row, which is why SPEC.md 31.1.1's scrolling list had to land first:
; four rows is what the pane held, and this is the driver that needed the
; fifth.
;
; WHAT IT SAYS IS THE DIAGNOSIS, netui.inc's rule. A card that is present and
; has no address is the common failure and it is a completely different
; problem from a card that is absent - so the page names the base it found and
; the MAC it read out of the PROM before it says a word about addresses, and a
; machine whose DHCP server never answered reads `Addr  none` with a card
; plainly there above it.
;
; NOTHING HERE GREYS. Renew is honest in every state: with an address it
; re-acquires, without one it is the only thing to do, and SPEC.md 47 rule 3's
; "the only honest test is doing the thing" is exactly a DHCP exchange.
; =============================================================================

EU_LX       equ 2               ; the pane's own inset - CP_PMX
EU_HY       equ 4               ; the heading
EU_Y0       equ 20              ; the first value row
EU_ROW      equ 12
EU_VX       equ 56              ; the value column. It is 56 and not 48
                                ; because `Router` is six characters and a
                                ; glyph cell is eight pixels wide, so at 48 the
                                ; label and its value share a column and the
                                ; row reads `Router10.0.2.2`. The widest value
                                ; is a dotted quad, 15 characters = 120px, so
                                ; 56 + 120 = 176 of the pane's 221
EU_BX       equ 2               ; the button
EU_BY       equ 108             ; ...and the button sits BELOW the counters
                                ; row at EU_Y0 + 6*EU_ROW = 92, whose glyphs
                                ; run to 99
EU_BW       equ 68
EU_B2X      equ 76             ; ...and the second button beside it
EU_B3X      equ 150            ; ...and the third, which ends at 217 of the
                               ; pane's 221 - the row is full and a fourth
                               ; would have to go somewhere else
EU_BRENEW   equ 1              ; what the press arms (os88ui_arm's token)
EU_BSETUP   equ 2
EU_BBUF     equ 3
EU_BH       equ 16

; -----------------------------------------------------------------------------
; DSV_CPPAINT - DI = pane left, DX = pane top
; -----------------------------------------------------------------------------
eth_cp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [eth_px], di
    mov [eth_py], dx

    mov si, eth_cpname
    mov bx, EU_HY
    call eu_lab

    ; --- the card ------------------------------------------------------------
    mov si, eu_s_card
    mov bx, EU_Y0
    call eu_lab
    mov si, eu_s_ne1
    cmp byte [eth_word], 0
    je  .kind
    mov si, eu_s_ne2
.kind:
    mov bx, EU_Y0
    call eu_val
    mov ax, [eth_base]
    mov bx, EU_Y0
    mov cx, EU_VX + 64
    call eu_hex

    ; --- and its address, which is a fact about the hardware -----------------
    mov si, eu_s_mac
    mov bx, EU_Y0 + EU_ROW
    call eu_lab
    call eu_macstr
    mov si, eu_buf
    mov bx, EU_Y0 + EU_ROW
    call eu_val

    ; --- then the three the network gave us ----------------------------------
    mov si, eu_s_addr
    mov bx, EU_Y0 + EU_ROW * 2
    call eu_lab
    mov si, eth_ip
    mov bx, EU_Y0 + EU_ROW * 2
    call eu_ip

    mov si, eu_s_gw
    mov bx, EU_Y0 + EU_ROW * 3
    call eu_lab
    mov si, eth_gw
    mov bx, EU_Y0 + EU_ROW * 3
    call eu_ip

    mov si, eu_s_dns
    mov bx, EU_Y0 + EU_ROW * 4
    call eu_lab
    mov si, eth_dns
    mov bx, EU_Y0 + EU_ROW * 4
    call eu_ip

    ; --- how it got there ----------------------------------------------------
    mov si, eu_s_dhcp
    mov bx, EU_Y0 + EU_ROW * 5
    call eu_lab
    mov si, eu_s_idle
    cmp byte [eth_mode], ECM_MAN    ; **THE MODE, NOT THE STATE.** A manual
    jne .dauto                      ; machine has dhcp_st = DH_BOUND so that
    mov si, eu_s_manual             ; NETV_STATE reports a link, and a row
    jmp short .dsay                 ; reading `Bound` there would be saying a
.dauto:                             ; server answered when none was asked
    mov al, [dhcp_st]
    cmp al, DH_BOUND
    jne .d1
    mov si, eu_s_bound
    jmp short .dsay
.d1:
    cmp al, DH_FAIL
    jne .d2
    mov si, eu_s_nosrv
    jmp short .dsay
.d2:
    cmp al, DH_IDLE
    je  .dsay
    mov si, eu_s_asking
.dsay:
    mov bx, EU_Y0 + EU_ROW * 5
    call eu_val

    ; --- and what has crossed it ---------------------------------------------
    mov si, eu_s_rx
    mov bx, EU_Y0 + EU_ROW * 6
    call eu_lab
    mov ax, [eth_nrx]
    mov bx, EU_Y0 + EU_ROW * 6
    mov cx, EU_VX
    call eu_dec
    mov si, eu_s_tx
    mov bx, EU_Y0 + EU_ROW * 6
    mov cx, EU_VX + 48
    call eu_txt
    mov ax, [eth_ntx]
    mov bx, EU_Y0 + EU_ROW * 6
    mov cx, EU_VX + 80
    call eu_dec

    ; --- the two buttons -----------------------------------------------------
    call eth_draw_btns

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; DSV_CPCLICK - CX = pane-relative x, BX = pane-relative y, DI/DX = the pane
;
; PANE-RELATIVE, and netui.inc's own bug is the reason that is written twice:
; a hit test against the ABSOLUTE rect the button was drawn from compares
; 0..220 with ~264 and takes the exit every time, on a page that draws
; perfectly.
; -----------------------------------------------------------------------------
eth_cp_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [eth_px], di
    mov [eth_py], dx
    cmp byte [eth_up], 0
    je  .out
    call eth_bhit
    jc  .out
    cmp al, EU_BRENEW           ; a greyed control refuses in ONE place, and
    jne .nbuf                   ; that place is the same predicate the greying
    cmp byte [eth_mode], ECM_MAN    ; reads (SPEC.md 47 rule 5). A refused
    je  .out                        ; click says nothing more - rule 6
    jmp short .arm
.nbuf:
    cmp al, EU_BBUF
    jne .arm
    call sk_busy                ; ...and the buffer button's predicate is
    jc  .out                    ; sk_busy, in eth_draw_btns and here alone
.arm:
    xor ah, ah                  ; ...and the press only ARMS and DRAWS: both
    push ax                     ; of these want the RELEASE (SPEC.md 13.8.4),
    call os88ui_arm             ; Renew because it is seconds of wire and Set
    pop ax                      ; Up because it opens a window
    call eth_setdown
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
; DSV_CPUP - the release (SPEC.md 13.8.4). Same arguments as DSV_CPCLICK.
; -----------------------------------------------------------------------------
eth_cp_up:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [eth_px], di
    mov [eth_py], dx
    call os88ui_fire            ; what the press armed, and it is CLEARED
    or  ax, ax
    jz  .out
    mov dh, al                  ; ...against a FRESH hit at the release point:
                                ; equal and non-zero is the whole gesture, and
                                ; anything else is a change of mind.
                                ; **DH AND NOT BL**: BX is the page ABI's
                                ; PANE-RELATIVE Y (SPEC.md 31.1) and eth_bhit
                                ; is about to read it, so banking the armed
                                ; control there asks the hit test about a y of
                                ; 2 - which is off both buttons, so every
                                ; release cancelled and `Set Up` opened
                                ; nothing on a page that drew and armed
                                ; perfectly. DX is dead from the [eth_py]
                                ; store above and eth_bhit preserves it
    call eth_bhit
    jc  .cancel
    cmp al, dh
    jne .cancel
    cmp byte [eth_up], 0
    je  .cancel
    mov byte [eth_down], 0      ; NOT eth_setdown: every arm below repaints the
                                ; whole pane with this button upright in it,
                                ; and drawing it upright FIRST is
                                ; PERFORMANCE.md's double-draw with the button
                                ; as the flash
    cmp al, EU_BSETUP
    je  .setup
    cmp al, EU_BBUF
    je  .buf
    call eth_dhcp_wait          ; Renew: and it WAITS, because nothing else
    call eth_cp_pane            ; pumps the ring (SPEC.md 72.2.1)
    jmp short .out
.buf:
    mov al, 1
    xchg al, [eth_busy]         ; the mutex every VERB takes (SPEC.md 72.2.2):
    or al, al                   ; this arm re-sizes the ring claim on the UI
    jnz .bfheld                 ; task while a worker's verb may hold the
                                ; driver - the one mutator that skipped the
                                ; lock every other mutator takes. Held: the
                                ; request is left un-stepped and the next
                                ; click asks again
    call eu_bufnext             ; the new REQUEST, and then the heap's answer
    call sk_reclaim             ; to it - the caption is redrawn from what was
    mov byte [eth_busy], 0      ; ...released BEFORE the pane paints: a paint
    mov byte [ec_dirty], 1      ; is milliseconds a verb should not wait out
.bfheld:
    call eth_cp_pane            ; so an 8K the machine cannot fund reads 4K
    jmp short .out              ; the moment it is asked for (remembered when
                                ; the panel closes, 31.8)
.setup:
    call eth_cp_pane            ; the pane FIRST, so the button it was pressed
    call eth_cfg_open           ; with comes back upright - and the Setup
    jmp short .out              ; window (SPEC.md 72.7) SECOND, because a page
                                ; draws itself unclipped (SPEC.md 31.9) and
                                ; wm_show has already drawn that window whole:
                                ; repainting the pane after it paints straight
                                ; over its corner, which is what the Install
                                ; dialog arrived doing (drivers/hdd/page.inc)
.cancel:
    xor al, al
    call eth_setdown
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
eth_cp_drag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [eth_px], di
    mov [eth_py], dx
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or  ax, ax
    jz  .out
    mov dh, al                  ; DH, for eth_cp_up's reason: BL is the low
    call eth_bhit               ; half of the pane-relative y eth_bhit reads
    jc  .off
    cmp al, dh                  ; on a DIFFERENT button is off this one
    je  .set
.off:
    xor al, al
.set:
    call eth_setdown            ; ...which draws only if that CHANGED
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
; eth_brects - both button rects, in SCREEN coordinates, from [eth_px]/[eth_py]
; out: nothing; eth_r1 and eth_r2 filled
;
; ONE description of where the buttons are, read by the painter and by the hit
; test alike. netui.inc's own header is the reason it is written that way here
; too: its two descriptions agreed until they did not, and the button then did
; nothing at all on a page that drew perfectly.
; -----------------------------------------------------------------------------
eth_brects:
    push ax
    push bx
    mov ax, [eth_px]
    add ax, EU_BX
    mov [eth_r1], ax
    add ax, EU_BW - 1
    mov [eth_r1+4], ax
    mov ax, [eth_px]
    add ax, EU_B2X
    mov [eth_r2], ax
    add ax, EU_BW - 1
    mov [eth_r2+4], ax
    mov ax, [eth_px]
    add ax, EU_B3X
    mov [eth_r3], ax
    add ax, EU_BW - 1
    mov [eth_r3+4], ax
    mov ax, [eth_py]
    add ax, EU_BY
    mov [eth_r1+2], ax
    mov [eth_r2+2], ax
    mov [eth_r3+2], ax
    add ax, EU_BH - 1
    mov [eth_r1+6], ax
    mov [eth_r2+6], ax
    mov [eth_r3+6], ax
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_bhit - which button is the pane-relative point in?
; in:  CX = pane x, BX = pane y (the page ABI's, SPEC.md 31.1)
; out: CF=0 and AL = EU_B*; CF=1 = neither
; -----------------------------------------------------------------------------
eth_bhit:
    push bx
    push cx
    push dx
    call eth_brects
    mov dx, bx                  ; **LIFTED TO THE SCREEN**, because the page
    add dx, [eth_py]            ; ABI hands a PANE-RELATIVE point and these
    add cx, [eth_px]            ; rects are where the painter drew
    mov bx, eth_r1
    call os88ui_bhit
    jc  .try2
    mov al, EU_BRENEW
    jmp short .out
.try2:
    mov bx, eth_r2
    call os88ui_bhit
    jc  .try3
    mov al, EU_BSETUP
    jmp short .out
.try3:
    mov bx, eth_r3
    call os88ui_bhit
    jc  .out
    mov al, EU_BBUF
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; eth_setdown - AL = the button drawn PRESSED (0 = neither)
;
; Draws only on a CHANGE, so a pointer moving inside the button it pressed
; costs one compare and no pixels - which is what makes an edge arriving per
; mouse packet affordable (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
eth_setdown:
    push ax
    cmp al, [eth_down]
    je  .out
    mov [eth_down], al
    call eth_draw_btns
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_draw_btns - both of them, in whatever state they are in
; in:  the gfx lock is held and [eth_px]/[eth_py] are current
; -----------------------------------------------------------------------------
eth_draw_btns:
    push ax
    push bx
    push si
    push di
    call eth_brects
    mov bx, eth_r1
    mov si, eu_s_renew
    mov di, OS88UI_FILL         ; **THE FILL IS NOT OPTIONAL HERE.** A button
                                ; that can be drawn PRESSED is a button drawn
                                ; over its own previous picture, and
                                ; os88ui_btn's own note is that a redraw
                                ; without the fill lands the new caption on
                                ; top of the old one
    cmp byte [eth_mode], ECM_MAN    ; DISABLED IS A FLAG AND NOT A COLOUR
    jne .rdown                      ; (SPEC.md 47 rule 1): on a manual machine
    or  di, OS88UI_DIS              ; there is no server being asked, which is
    jmp short .rdraw                ; a FACT rather than a guess - rule 3. And
.rdown:                             ; a greyed control is never also PRESSED
    cmp byte [eth_down], EU_BRENEW
    jne .rdraw
    or  di, OS88UI_DOWN
.rdraw:
    call os88ui_btn
    mov bx, eth_r2
    mov si, eu_s_setup
    mov di, OS88UI_FILL
    cmp byte [eth_down], EU_BSETUP
    jne .sdraw
    or  di, OS88UI_DOWN
.sdraw:
    call os88ui_btn
    mov bx, eth_r3
    call eu_bufcap              ; the caption IS the readout: whatever the
    mov di, OS88UI_FILL         ; ladder actually got, not what was asked for
    call sk_busy                ; ...and a live socket's data is IN the rings,
    jnc .bdown                  ; so resizing them is not available while there
    or  di, OS88UI_DIS          ; is one - a FACT, which is what SPEC.md 47
    jmp short .bdraw            ; rule 3 lets a control grey itself on
.bdown:
    cmp byte [eth_down], EU_BBUF
    jne .bdraw
    or  di, OS88UI_DOWN
.bdraw:
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_cp_pane - clear the pane and draw it again (hd_page_repaint's shape)
; -----------------------------------------------------------------------------
eth_cp_pane:
    push ax
    push bx
    push cx
    push dx
    push di
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [eth_px]
    mov bx, [eth_py]
    mov cx, ax
    add cx, EU_W - 1
    mov dx, bx
    add dx, EU_H
    call OSAPI_GFX_FILL
    mov di, [eth_px]
    mov dx, [eth_py]
    call eth_cp_paint
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

EU_W        equ 221             ; the pane, as ctrl.inc lays it out...
EU_H        equ 131             ; ...and its LAST ROW: a short clear leaves a
                                ; stale strip along the bottom

; -----------------------------------------------------------------------------
; the little drawing helpers - netui.inc's, one page over
; -----------------------------------------------------------------------------
eu_lab:
    push cx
    mov cx, EU_LX
    call eu_txt
    pop cx
    ret

eu_val:
    push cx
    mov cx, EU_VX
    call eu_txt
    pop cx
    ret

; in: SI = the string, BX = the y offset, CX = the x offset
eu_txt:
    push ax
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    add cx, [eth_px]
    mov dx, [eth_py]
    add dx, bx
    call OSAPI_FONT_STR
    pop dx
    pop cx
    pop ax
    ret

; --- AX as four hex digits at column CX, row BX ------------------------------
eu_hex:
    push ax
    push bx
    push cx
    push si
    push bx
    push cx
    mov si, eu_buf
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
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
    pop cx
    pop bx
    mov si, eu_buf
    call eu_txt
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- AX as decimal at column CX, row BX --------------------------------------
eu_dec:
    push ax
    push bx
    push cx
    push dx
    push si
    push bx
    push cx
    mov si, eu_buf + 8
    mov byte [si], 0
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec si
    mov [si], dl
    or  ax, ax
    jnz .d
    pop cx
    pop bx
    call eu_txt
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eu_ip - the four bytes at DS:SI as a dotted quad in the value column, row BX
;
; An address of 0.0.0.0 is drawn as `none`, because a machine with no address
; showing four zeroes reads like a machine with a very odd one.
; -----------------------------------------------------------------------------
eu_ip:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call ip_zero
    jnz .have
    mov si, eu_s_none
    call eu_val
    jmp short .out
.have:
    mov di, eu_buf
    call eth_ipstr
    mov si, eu_buf
    call eu_val
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; eth_ipstr - the four bytes at DS:SI as a NUL-terminated dotted quad at DS:DI
; out: nothing; all registers preserved. The buffer needs 16 bytes.
;
; Shared with the Setup window (ethcfg.inc), which needs the same text in an
; editable field - so the page and the field cannot disagree about what an
; address looks like.
; -----------------------------------------------------------------------------
eth_ipstr:
    push ax
    push cx
    push si
    push di
    mov cx, 4
.part:
    mov byte [eu_st], 0         ; **ONCE A DIGIT HAS BEEN WRITTEN, A ZERO IS A
                                ; DIGIT.** Suppressing on "this one is zero"
                                ; alone turns 10.0.2.105 into 10.0.2.15
    xor ah, ah
    mov al, [si]
    inc si
    push cx
    mov cl, 100
    call eu_digit
    mov cl, 10
    call eu_digit
    pop cx
    add al, '0'
    mov [di], al
    inc di
    cmp cx, 1
    je  .done
    mov byte [di], '.'
    inc di
    loop .part
.done:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- one digit of AL in units of CL, suppressed if it and everything before
;     it is zero. DI advances if anything was written.
eu_digit:
    push bx
    xor bh, bh
.l:
    cmp al, cl
    jb  .done
    sub al, cl
    inc bh
    jmp short .l
.done:
    or  bh, bh
    jnz .emit
    cmp byte [eu_st], 0
    je  .skip
.emit:
    mov byte [eu_st], 1
    add bh, '0'
    mov [di], bh
    inc di
.skip:
    pop bx
    ret

; -----------------------------------------------------------------------------
; eu_macstr - eth_mac as twelve hex digits in eu_buf
; -----------------------------------------------------------------------------
eu_macstr:
    push ax
    push cx
    push si
    push di
    mov si, eth_mac
    mov di, eu_buf
    mov cx, 6
.b:
    mov al, [si]
    inc si
    push ax
    push cx
    mov cl, 4
    shr al, cl
    pop cx
    call eu_nib
    pop ax
    and al, 0x0F
    call eu_nib
    loop .b
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

eu_nib:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    mov [di], al
    inc di
    ret

; --- strings, every one of them inside the pane ------------------------------
eu_s_card:  db 'Card', 0
eu_s_mac:   db 'Id', 0
eu_s_addr:  db 'Addr', 0
eu_s_gw:    db 'Router', 0
eu_s_dns:   db 'Name', 0
eu_s_dhcp:  db 'Dhcp', 0
eu_s_rx:    db 'Rx', 0
eu_s_tx:    db 'Tx', 0
eu_s_ne1:   db 'Ne1000', 0
eu_s_ne2:   db 'Ne2000', 0
eu_s_none:  db 'none', 0
eu_s_idle:  db 'Off', 0
eu_s_asking: db 'Asking', 0
eu_s_bound: db 'Bound', 0
eu_s_nosrv: db 'No server', 0
eu_s_renew: db 'Renew', 0
eu_s_setup: db 'Set Up', 0
eu_s_manual: db 'Manual', 0
eu_s_b1:    db 'Buf 1K', 0    ; the socket rings (SPEC.md 72.13.2), and the
eu_s_b4:    db 'Buf 4K', 0    ; caption is the LIVE size rather than the
eu_s_b8:    db 'Buf 8K', 0    ; request: a machine that could not fund what
eu_s_bnone: db 'Buf --', 0    ; was asked says so here and nowhere else

; -----------------------------------------------------------------------------
; eu_bufcap - SI = the caption for the buffer button, from the LIVE rings
; -----------------------------------------------------------------------------
eu_bufcap:
    mov si, eu_s_bnone
    cmp word [sk_seg], 0        ; no claim at all: the driver attached without
    je  .out                    ; rings, so every socket verb is refusing
    mov si, eu_s_b1
    cmp word [sk_rxcap], SK_RXCAP0
    je  .out
    mov si, eu_s_b4
    cmp word [sk_rxcap], SK_RXMID
    je  .out
    mov si, eu_s_b8
.out:
    ret

; -----------------------------------------------------------------------------
; eu_bufnext - the next request in the cycle: auto -> 1K -> 4K -> 8K -> auto
;
; It walks from what is CURRENTLY REQUESTED and not from what is held, so the
; cycle is four steps whatever the ladder did - stepping from the live size
; would make `auto` unreachable on any machine where auto lands on a rung.
; -----------------------------------------------------------------------------
eu_bufnext:
    push ax
    mov ax, [sk_want]
    cmp ax, SK_RXCAP0
    jb  .to1
    cmp ax, SK_RXMID
    jb  .to4
    cmp ax, SK_RXMAX
    jb  .to8
    xor ax, ax                  ; ...and round to AUTO, which is the default a
    jmp short .set              ; fresh machine boots with
.to1:
    mov ax, SK_RXCAP0
    jmp short .set
.to4:
    mov ax, SK_RXMID
    jmp short .set
.to8:
    mov ax, SK_RXMAX
.set:
    mov [sk_want], ax
    pop ax
    ret

eth_px:     dw 0
eth_py:     dw 0
eth_r1:     dw 0, 0, 0, 0     ; Renew's rect, Set Up's and the buffer's - one
eth_r2:     dw 0, 0, 0, 0     ; description, drawn and clicked from
eth_r3:     dw 0, 0, 0, 0
eth_down:   db 0             ; which one is drawn PRESSED (SPEC.md 13.8.4)
eu_buf:     times 20 db 0
eu_st:      db 0                ; eu_digit: has anything been written yet?
