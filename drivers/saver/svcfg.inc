; =============================================================================
; svcfg.inc - the settings window (SPEC.md 79.7)
;
; Four check boxes, two numbers and two buttons, behind the Screen Saver
; button on the Control Panel's Theme page.
;
; --- WHY IT IS A WINDOW AND NOT A CONTROL PANEL PAGE ------------------------
; **Not because a page could not hold the controls** - it could, at a squeeze.
; The reason is where the STRINGS would have to live. A Control Panel page is
; drawn by the on-demand `ctrl` module (SPEC.md 2.8), and a module's data
; stays in the kernel's `.text` because that is the only segment it can reach
; through DS - so eleven labels, four of them sixteen characters, would be
; about 150 bytes of an image rung that had 20 left in it. An overlay owns a
; segment, so every one of them is on the floppy.
;
; ETHER.DRV's Setup window (SPEC.md 72.7) and HDD.DRV's disk tool (SPEC.md
; 52.2) are the precedent, and this is the same species: a window with no
; instance behind it.
;
; --- IT IS UNBOUND, AND THAT IS THE FILE DIALOG'S RULE ----------------------
; No instance record, so no dock tile, no Task Manager row and no callback
; billing - and its close box would ordinarily reduce to `wm_hide` (SPEC.md
; 38.1), which is exactly wrong here: the window's dispatcher is a near
; pointer INTO THIS IMAGE, and the image is freed the moment nothing is using
; it. A hidden window holding a W_PAINT that names a freed claim is a paint
; into whatever the heap hands out next.
;
; So `sc_close` is installed with OSAPI_WM_ONCLOSE and DESTROYS the window
; itself, then answers CF = 1 - refused - because by the time it returns there
; is nothing left to close. That is os88ui_aclose's shape exactly (SPEC.md
; 75.1/75.3) and for the identical reason.
;
; --- APPLYING IS IMMEDIATE; REMEMBERING WAITS FOR THE PANEL -----------------
; SPEC.md 31.8: no setting writes to a disk on a click. `Ok` puts the three
; bytes into the kernel's own settings block on the spot and raises SS_P_POST;
; the UI task's next pass re-derives the idle period from them and marks
; SYSTEM.CFG owed, and the CONTROL PANEL's close is what spends that - which
; is where every other setting on that page is written too.
; =============================================================================

SC_FW       equ 320             ; the frame. 320x150 at y = 24 lands at 24..173
SC_FH       equ 150             ; on CGA, whose dock starts at 176 - so wm_fit
SC_X        equ 100             ; never has to clamp it, and a CLAMPED frame
SC_Y        equ 24              ; would leave every offset below laid out for
                                ; a window that is not there (SPEC.md 39.7)

SC_GX       equ 4               ; the check boxes
SC_LX       equ 22              ; ...and their labels
SC_R0Y      equ 2
SC_ROWH     equ 16
SC_TXDY     equ 2               ; an 8px glyph centred in the 12px box
SC_HITW     equ 200             ; how far right of the glyph a click still
                                ; means the row - the label, and no further

SC_N0Y      equ 68              ; the two numbers
SC_NPIT     equ 22
SC_NHT      equ 14
SC_FX1      equ 116             ; the field, five columns wide
SC_FX2      equ 160
SC_UX       equ 166             ; ...and the unit after it

SC_BY       equ 112             ; the buttons
SC_BH       equ 16
SC_BW       equ 60
SC_B0X      equ 4               ; Test, apart from the other two on the LEFT:
                                ; it neither commits nor abandons, and putting
                                ; it in the row with Ok and Cancel invites the
                                ; hand that means Ok
SC_B1X      equ 176
SC_B2X      equ 246
SC_COK      equ 1               ; what a press ARMS and a release fires
SC_CCANCEL  equ 2
SC_CTEST    equ 3
SC_NBTN     equ 3

SC_TLEN     equ 4               ; a field's buffer: three digits and the NUL

%if SC_BY + SC_BH > SC_FH - TITLE_H - 1
%error "saver: the settings window runs past its own content box"
%endif

; -----------------------------------------------------------------------------
; sv_do_cfg - open it (SPEC.md 79.7)
; in:  SI = the settings block's offset in KERNEL_SEG; ES = KERNEL_SEG;
;      THE GFX LOCK IS HELD - this is a Control Panel click (SPEC.md 31.1), so
;      we may draw and must not take the lock
; out: CF = 0 the window is up; CF = 1 it could not be created
;
; THE CONTROLS ARE SEEDED FROM THE LIVE SETTINGS EVERY TIME and not from the
; last thing typed. A window reopened after a Cancel has to show what the
; machine is actually doing, or Cancel is not a cancel.
; -----------------------------------------------------------------------------
sv_do_cfg:
    mov [sv_pub], si

    mov al, [es:si + SS_P_MODES]
    mov [sc_modes], al
    mov ax, [es:si + SS_P_SECS] ; AL = the seconds, AH = the minutes - the two
    push ax                     ; fields, adjacent in the block
    mov si, sc_f0               ; the two line blocks: buffer, capacity, and
    mov di, sc_t0               ; the caret at the end of what is in them
    call sc_fld
    pop ax
    mov al, ah
    mov si, sc_f1
    mov di, sc_t1
    call sc_fld
    mov byte [sc_f0 + LN_FOCUS], 1  ; the first field has it, so a user who
                                    ; opens this and types is typing seconds

    mov bx, [sv_win]
    or bx, bx
    jnz .show
    mov si, sc_tpl
    call OSAPI_WM_CREATE
    jc .no
    mov [sv_win], bx
    mov byte [sc_down], 0
    xor ax, ax
    call os88ui_arm             ; a window reopened mid-gesture must not come
.show:                          ; up with one armed for the next release
    call OSAPI_WM_SHOW
    CLC_OR_STC .no
    ret

; -----------------------------------------------------------------------------
; sc_fld - AL (0..255) into the buffer at DI as decimal, NUL terminated, and
; the line block at SI filled in around it: the caret lands at the end
; clobbers: AX, BX, CX, DX (all of them sc_num2str's and sc_seed's, which
; were two routines with this one caller - driver size pass 1)
;
; Written out rather than borrowed because there is no shared one: the kernel
; has clk_fld_str and the SDK has none, and three digits of unsigned decimal
; is a divide loop either way. A zero needs no case of its own: the loop runs
; once and pushes its one '0'.
; -----------------------------------------------------------------------------
sc_fld:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push di
    xor cx, cx                  ; CX = digits pushed
    mov bl, 10
.div:
    xor ah, ah
    div bl                      ; AL = the rest, AH = this digit
    push ax
    inc cx
    or al, al
    jnz .div
.pop:
    pop ax
    add ah, '0'
    mov [di], ah
    inc di
    loop .pop
    mov byte [di], 0
    pop di

    mov [si + LN_BUF], di
    mov word [si + LN_MAX], SC_TLEN
    mov word [si + LN_VIEW], 0
    mov byte [si + LN_FOCUS], 0
    jmp os88line_set            ; DI = the string; the caret lands at the end

; -----------------------------------------------------------------------------
; sc_layout - the two field rects, in SCREEN coordinates
; in:  [sv_win] is live
; out: DI = content left, BP = content top
;
; **RECOMPUTED AT EVERY PAINT, CLICK AND KEY**, because a window moves: a rect
; banked once is a control that is drawn in one place and clickable in another
; the moment the user drags it (SPEC.md 22's fm_hit discipline).
; -----------------------------------------------------------------------------
sc_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [sv_win]
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov di, ax
    mov bp, dx
    mov si, sc_f0
    add dx, SC_N0Y
    call sc_frect
    mov si, sc_f1
    add dx, SC_NPIT             ; sc_frect keeps DX
    call sc_frect
    jmp os88_r_si

; sc_frect - one field's rect: SI = the block, DI = content left, DX = its top
sc_frect:
    push ax
    mov ax, di
    add ax, SC_FX1
    mov [si + LN_X1], ax
    mov ax, di
    add ax, SC_FX2
    mov [si + LN_X2], ax
    mov [si + LN_Y1], dx
    mov ax, dx
    add ax, SC_NHT - 1
    mov [si + LN_Y2], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_paint - W_PAINT (content already white-filled, lock held)
; in:  SI = the window
; -----------------------------------------------------------------------------
sc_paint:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    call sc_layout              ; DI = content left, BP = content top

    xor cx, cx                  ; --- the four check boxes
.row:
    push cx
    call sc_check
    pop cx
    inc cx
    cmp cx, SS_NMODE
    jb .row

    mov dx, bp                  ; --- the two numbers, each label, unit and
    add dx, SC_N0Y + 3          ; field on one row
    mov si, sc_s_every
    mov bx, sc_s_secs
    mov ax, sc_f0
    call .num
    add dx, SC_NPIT
    mov si, sc_s_after
    mov bx, sc_s_mins
    mov ax, sc_f1
    call .num

    call sc_btns                ; --- and the two buttons

    jmp os88_r_bp

.num:                           ; SI = the label, BX = the unit, AX = the field,
    mov cx, di                  ; DX = the row's text top
    add cx, SC_GX
    call sc_txt
    mov si, bx
    mov cx, di
    add cx, SC_UX
    call sc_txt
    xchg ax, si
    jmp os88line_draw

; --- sc_txt - SI = the string at (CX, DX), black ----------------------------
sc_txt:
    push ax
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the pane's own ground
    call OSAPI_FONT_RUN
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_check - one check box and its label
; in:  CX = the row, DI = content left, BP = content top; lock held
; out: nothing; clobbers AX, BX, CX, DX, SI
;
; ONE ROW, so a toggle repaints a row and never the window: os88ui_glyph is
; ~45ms on the field machine (its own header says so) and four of them plus
; two fields plus two buttons is most of a second of held gfx lock.
; -----------------------------------------------------------------------------
sc_check:
    mov bx, cx
    add bx, bx
    mov si, [bx + sc_labels]    ; (os88ui_glyph keeps every register)
    mov ax, cx
    mov bl, SC_ROWH
    mul bl
    add ax, SC_R0Y
    mov dx, bp
    add dx, ax                  ; DX = the glyph's top

    mov al, OS88UI_GCHECK
    mov bl, [sc_modes]
    shr bl, cl                  ; bit CL = mode CL, sv_pick's own mapping
    test bl, 1
    jz .off
    or al, OS88UI_GON
.off:
    xor ah, ah
    mov cx, di
    add cx, SC_GX
    call os88ui_glyph

    add dx, SC_TXDY
    mov cx, di
    add cx, SC_LX
    jmp sc_txt

; -----------------------------------------------------------------------------
; sc_btns - the buttons, drawn to follow [sc_down]
; in:  DI = content left, BP = content top; lock held
; out: nothing; clobbers AX, BX, CX, DX, SI - its two callers bank them
;
; One button at a time through the one record: its rect is sc_r, which the
; record names statically, its label and flags the two words the record
; points at (SPEC.md 20.5.1.3). It was sc_btns calling sc_btn1, its one
; caller (driver size pass 1).
; -----------------------------------------------------------------------------
sc_btns:
    xor bx, bx                  ; BX = the button's index, twice
.b:
    mov ax, [bx + sc_btnx]      ; ONE TABLE drives the painter and the hit
    add ax, di                  ; test, so a third button is a row rather than
    mov si, [bx + sc_btnl]      ; two places to add it and one to forget
    mov [sc_r + 0], ax
    add ax, SC_BW - 1
    mov [sc_r + 4], ax
    mov ax, bp
    add ax, SC_BY
    mov [sc_r + 2], ax
    add ax, SC_BH - 1
    mov [sc_r + 6], ax
    mov cx, bx                  ; the control's id is its index plus one
    shr cx, 1
    inc cx
    mov dx, OS88UI_FILL         ; FILL because a RELEASE redraws this button
    cmp cl, [sc_down]           ; over a PRESSED one; on a full repaint the
    jne .up                     ; content has already been whited
    or dx, OS88UI_DOWN          ; SPEC.md 13.8: the pressed look is DRAWN, not
.up:                            ; XOR-ed. THE ONE CONTROL (SPEC.md 20.5.1.3):
    mov [sv_btlbl], si          ; the record takes the label and the flags,
    mov [sv_btflg], dx          ; and the picture is identical
    push bx
    mov bx, sv_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    inc bx
    inc bx
    cmp bx, SC_NBTN * 2
    jb .b
    ret

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; A Control Panel page draws its buttons one at a time, so the record is
; pointed at whichever rect the caller staged. The GESTURE here is the
; panel's own cp_ctl/cp_pgprobe probe (SPEC.md 13.8.4), which is already
; release-fired; what changed is that the DRAW goes through the control. The
; rect is always sc_r, so the record names it once, here.
sv_btlbl: dw 0
sv_btflg: dw 0
sv_btrec: dw sc_r, sv_btlbl, sv_btflg, 1, 0, 0, 0, 0, 0
%if OS88UI_BT_RECTS != 0
 %error "saver: sv_btrec names sc_r in its first word, OS88UI_BT_RECTS"
%endif

; -----------------------------------------------------------------------------
; sc_bhit - which button is the SCREEN point (CX, DX) in? AL = 0 for neither
; in:  CX = x, DX = y; DI = content left and BP = content top, from sc_layout
; out: AL; clobbers AH, CX, DX, SI. BX IS KEPT: sc_up and sc_drag hold the
;      armed control in BL across it
; -----------------------------------------------------------------------------
sc_bhit:
    mov ax, dx
    sub ax, bp
    cmp ax, SC_BY
    jl .none
    cmp ax, SC_BY + SC_BH
    jge .none
    sub cx, di                  ; CX = the x, off the table's own row
    mov si, sc_btnx
    mov dl, 1                   ; the ids are the table's index plus one
.b:
    lodsw
    cmp cx, ax
    jl .next
    add ax, SC_BW
    cmp cx, ax
    jl .hit
.next:
    inc dx
    cmp dl, SC_NBTN
    jbe .b
.none:
    xor dl, dl
.hit:
    xchg ax, dx
    ret

; -----------------------------------------------------------------------------
; sc_click - W_ONCLICK (absolute screen coords, lock held)
; in:  CX = x, DX = y, SI = the window
;
; A CHECK BOX ACTS ON THE PRESS and a BUTTON on the release, which is SPEC.md
; 13.6 applied rather than an inconsistency: a toggle has a safe prefix action
; - clicking it again undoes it, in view, with nothing else changed - and Ok
; does not, so a mis-aimed press on Ok has to be cancellable by sliding off.
;
; CX AND DX STAY THE CLICK throughout: sc_layout and os88line_click keep both.
; -----------------------------------------------------------------------------
sc_click:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    call sc_layout

    mov si, sc_f0               ; --- a field?
    call os88line_click
    mov al, 0
    jnc .focus
    mov si, sc_f1
    call os88line_click
    mov al, 1
    jc .chk
.focus:
    mov cl, al
    call sc_focus               ; ...which redraws the two fields, the whole
    jmp short .out              ; of the change

.chk:                           ; --- a check box?
    mov ax, cx
    sub ax, di
    sub ax, SC_GX
    jl .btn
    cmp ax, SC_HITW
    jge .btn
    mov ax, dx
    sub ax, bp
    sub ax, SC_R0Y
    jl .btn
    mov bl, SC_ROWH
    div bl                      ; AL = the row, AH = how far into it
    cmp ah, OS88UI_GW
    jae .btn                    ; the gap BETWEEN two rows belongs to neither
    cmp al, SS_NMODE
    jae .btn
    mov cl, al
    mov bl, 1
    shl bl, cl
    xor [sc_modes], bl          ; the toggle, and then its own row and nothing
    mov ch, 0                   ; else
    call sc_check
    jmp short .out

.btn:                           ; --- a button? THE PRESS ONLY ARMS
    call sc_bhit
    or al, al
    jz .out
    xor ah, ah
    call os88ui_arm
    call sc_setdown
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; sc_focus - CL = the field that gets it (0 or 1), the other loses it; then
; the two fields, and NOTHING else
; clobbers: CL, SI
;
; A caret arriving in one field and leaving the other is the whole of what a
; click into a field or a Tab changes, and os88line_draw draws "the box, its
; text, and the caret if it has focus" - so two calls express it. Repainting
; the content instead cost the white fill, four os88ui_glyph (~45ms each, its
; own header) and 121 cells of label and button text: a third of a second of
; held gfx lock for one 1px bar, which is sc_check's argument above at the
; other end of the same window.
;
; The caller has already run sc_layout, so LN_X1..LN_Y2 are at the window's
; live position - the only state os88line_draw reads. (sc_fields, which was
; this routine's tail and had no other caller)
; -----------------------------------------------------------------------------
sc_focus:
    mov [sc_f1 + LN_FOCUS], cl
    xor cl, 1
    mov [sc_f0 + LN_FOCUS], cl
    mov si, sc_f0
    call os88line_draw
    mov si, sc_f1
    jmp os88line_draw

; -----------------------------------------------------------------------------
; sc_setdown - AL = the button drawn PRESSED (0 = neither)
; Draws only on a CHANGE, so a pointer moving inside the button it pressed
; costs one compare and no pixels (SPEC.md 13.8.1).
; -----------------------------------------------------------------------------
sc_setdown:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    cmp al, [sc_down]
    je .out
    mov [sc_down], al
    call sc_layout
    call sc_btns
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; sc_up - W_ONMOUSEUP (SPEC.md 13.7): the two buttons fire HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = the window
; -----------------------------------------------------------------------------
sc_up:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov bl, al
    call sc_layout
    call sc_bhit                ; ...against a FRESH hit at the release point
    cmp al, bl
    jne .cancel
    cmp al, SC_CTEST            ; --- Test: the window STAYS, so this one owes
    jne .close                  ; the upright redraw the other two do not -
    call sc_test                ; which is the slid-off path's own, so it
.cancel:                        ; falls into it
    xor al, al                  ; slid off: thought better of, nothing fires
    call sc_setdown
    jmp short .out
.close:
    mov byte [sc_down], 0       ; NOT sc_setdown: both paths below destroy the
    cmp al, SC_COK              ; window, and drawing this button upright first
    jne .shut                   ; is PERFORMANCE.md's double-draw
    call sc_apply
.shut:
    call sc_kill
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; sc_drag - W_ONDRAG (SPEC.md 13.8.2): keep what is drawn pressed equal to
; what would fire. The same question sc_up asks, one pass early.
; -----------------------------------------------------------------------------
sc_drag:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov bl, al
    call sc_layout
    call sc_bhit
    cmp al, bl
    je .set
    xor al, al
.set:
    call sc_setdown             ; ...which draws only if that CHANGED
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; sc_key - W_ONKEY: AL = ascii, AH = scan, SI = the window
;
; The field takes what it knows; Tab, Return and Escape come back here, which
; is os88line.inc's whole contract - only the caller knows whether Return
; means Ok. A field's LN_FOCUS byte is 0 or 1, so the second one's IS the
; index of the field that has it.
; -----------------------------------------------------------------------------
sc_key:
    call os88_ent               ; push ax, bx, cx, dx, si, di, bp (os88drv.inc)
    call sc_layout
    cmp al, 13
    je .ret
    cmp al, 27
    je .esc
    cmp al, 9
    je .tab
    mov si, sc_f0               ; the field that has the focus
    cmp byte [sc_f1 + LN_FOCUS], 0
    je .key
    mov si, sc_f1
.key:
    call os88line_key
    jc .out                     ; not the field's: nothing here wants it
    call os88line_draw          ; ...and only the field it changed is redrawn
    jmp short .out
.tab:
    mov cl, [sc_f1 + LN_FOCUS]  ; ...to the other one
    xor cl, 1
    call sc_focus
    jmp short .out
.ret:
    call sc_apply               ; Return is Ok, and then Escape's close
.esc:
    call sc_kill
.out:
    jmp os88_r_bp

; -----------------------------------------------------------------------------
; sc_apply - the controls into the kernel's settings block (SPEC.md 79.7)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI - its callers are banked
;
; NOTHING IS WRITTEN TO A DISK HERE. The three bytes go into KERNEL_SEG and
; SS_P_POST is raised; the UI task re-derives the idle period from them and
; marks SYSTEM.CFG owed, and the CONTROL PANEL's own close is what spends
; that (SPEC.md 31.8). A floppy write is two seconds with the machine frozen
; for it, and it belongs at the end of a session of trying settings out rather
; than in the middle of one.
; -----------------------------------------------------------------------------
sc_apply:
    call sc_store
    mov byte [es:di + SS_P_POST], 1
    ret

; -----------------------------------------------------------------------------
; sc_store - the controls into the kernel's block, WITHOUT the post
; in:  nothing; ES = KERNEL_SEG
; out: DI = the block; clobbers AX, BX, CX, DX, SI
;
; Split out of sc_apply for Test, which needs the settings live and must NOT
; raise SS_P_POST: blk_pass spends a post BEFORE it looks at the clock, so a
; post would re-derive [ss_idle] from [ss_mins] on the very next pass and undo
; the 1 that Test has just written there (SPEC.md 79.7). sc_str2num spends
; BX, so the block is held in DI.
; -----------------------------------------------------------------------------
sc_store:
    mov di, [sv_pub]
    mov al, [sc_modes]
    mov [es:di + SS_P_MODES], al

    mov si, sc_t0
    call sc_str2num             ; AX = what was typed, 0 if it was nonsense
    mov cx, SV_MINSEC
    mov dx, SV_MAXSEC
    call sc_clamp
    mov [es:di + SS_P_SECS], al

    mov si, sc_t1
    call sc_str2num
    xor cx, cx                  ; ZERO IS LEGAL HERE and is the whole of the
    mov dx, SS_MAXMIN           ; off switch (SPEC.md 79.4), which is why the
    call sc_clamp               ; two clamps take their floor as an argument
    mov [es:di + SS_P_MINS], al
    ret

; -----------------------------------------------------------------------------
; sc_test - show it NOW (SPEC.md 79.7)
; in:  the gfx lock is HELD (a release); ES = KERNEL_SEG
; out: nothing; clobbers AX, BX, CX, DX, SI, DI - its caller is banked
;
; **IT APPLIES FIRST**, because a Test that showed something other than what
; the controls say would be worth less than nothing - so Test commits exactly
; what Ok commits, and the only thing it does not do is close the window.
;
; Then it writes 1 into the kernel's DERIVED idle period. That word is not a
; setting: `ss_mins2idle` owns it and re-derives it from `[ss_mins]` whenever
; a post is spent, so one is worth exactly as long as it takes blk_pass to
; notice - a tick - and the post that puts the real period back is raised by
; sv_do_stop when the session ends. That is the whole mechanism, and the
; reason it needs no kernel byte at all beyond publishing the word.
;
; **IT WORKS WITH `Start after` SET TO 0**, which is worth stating because 0
; is the off switch: the period is 1 for the length of the test whatever
; [ss_mins] says, and goes back to 0 - off - the moment it ends.
;
; The window stays up and is drawn over by the session. Waking repaints the
; desktop and the window with it (SPEC.md 79.6), so the user comes back to
; exactly the controls they pressed Test from - and the image is never freed
; in between, because SSV_BUSY answers for that window.
; -----------------------------------------------------------------------------
sc_test:
    call sc_store
    mov word [es:di + SS_P_IDLE], 1
    mov byte [sc_testing], 1    ; ...and sv_do_stop is what spends it
    ret

SS_MAXMIN   equ 59              ; MIRRORED IN kernel/blank.inc, where the
                                ; reason is arithmetic: 60 minutes of ticks
                                ; does not fit the word ss_mins2idle stores
                                ; them in. Here it is so the field cannot
                                ; accept a number the kernel will quietly
                                ; clamp behind the user's back, and the two
                                ; are the same constant - which is what
                                ; tests/unit/t_mirror.py is now pointed at

; sc_clamp - AX into [CX, DX] -> AL. clobbers AX, flags
sc_clamp:
    cmp ax, cx
    jae .lo
    mov ax, cx
.lo:
    cmp ax, dx
    jbe .out
    mov ax, dx
.out:
    ret

; -----------------------------------------------------------------------------
; sc_kill - take the window down for good (module internal)
; out: nothing (all registers preserved)
;
; DESTROY AND NOT HIDE. An unowned window's close is a hide (SPEC.md 38.1),
; which leaves the record holding a W_SEG and a W_PAINT that name THIS IMAGE -
; and this image is freed the moment the kernel's next reap finds nothing
; using it (SPEC.md 79.8). SS_P_POST is what brings that reap on.
; -----------------------------------------------------------------------------
sc_kill:
    push bx
    xor bx, bx                  ; the window, and 0 in its place
    xchg bx, [sv_win]
    or bx, bx
    jz .out
    call OSAPI_WM_DESTROY
    mov bx, [sv_pub]
    or bx, bx
    jz .out
    mov byte [es:bx + SS_P_POST], 1
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; sc_close - W_ONCLOSE (SPEC.md 75.1): the close box, and the app-name menu
; in:  SI = our window; gfx lock held
; out: CF = 1 always
;
; It answers REFUSED because by the time it returns there is nothing left to
; close: sc_kill has already destroyed the window, and letting app_close_win
; go on to hide a freed record is the one thing that would not survive. That
; is os88ui_aclose's argument (SPEC.md 75.3) word for word.
;
; The close box is a CANCEL: nothing is applied. Ok and Return are the only
; two ways a setting reaches the kernel.
; -----------------------------------------------------------------------------
sc_close:
    call sc_kill
    stc
    ret

; -----------------------------------------------------------------------------
; sc_str2num - the digits at SI into AX; anything else ends it
; clobbers: AX, BX, CX, DX
;
; A digit is one whose distance from '0' is 0..9 UNSIGNED, which is both ends
; of the range in one compare.
; -----------------------------------------------------------------------------
sc_str2num:
    xor ax, ax
    xor bx, bx
    mov cx, 10
.d:
    mov bl, [si]
    sub bl, '0'
    cmp bl, 9
    ja .out
    mul cx                      ; DX:AX; three digits cannot reach DX
    add ax, bx
    inc si
    cmp ax, 999
    jbe .d
    mov ax, 999
.out:
    ret

; --- the shared prologue and return ladder (drivers/os88drv.inc) -------------
; `call os88_ent` banks ax..bp, `jmp os88_r_bp` gives them back and returns,
; `jmp os88_r_si` the same for a routine that pushed ax..si. Cold only - this
; window's callbacks - and that is all that takes them.
    OS88_ENT
    OS88_RET_LADDER bp

; --- the window ---------------------------------------------------------------
sc_tpl:    dw SC_X, SC_Y, SC_FW, SC_FH, sc_s_title, sc_cls
; its CLASS (SPEC.md 11.104): every handler, and the flags it is born with
    OS88_CLASS sc_cls, 0, PAINT, sc_paint, ONKEY, sc_key, ONCLICK, sc_click, \
               ONCLOSE, sc_close, ONMOUSEUP, sc_up, ONDRAG, sc_drag
sc_s_title: db 'Screen Saver', 0

sc_labels:  dw sc_s_m0, sc_s_m1, sc_s_m2, sc_s_m3
sc_s_m0:    db 'Spinning Cube', 0
sc_s_m1:    db 'Flying Starfield', 0
sc_s_m2:    db 'Geometric Shapes', 0
sc_s_m3:    db 'Fish and Sea Life', 0
sc_s_every: db 'Change every', 0
sc_s_secs:  db 'seconds', 0
sc_s_after: db 'Start after', 0
sc_s_mins:  db 'minutes (0 = off)', 0
sc_s_ok:    db 'Ok', 0
sc_s_cancel:db 'Cancel', 0
sc_s_test:  db 'Test', 0

; The button row: x, then label, indexed by id-1 (SC_COK, SC_CCANCEL, SC_CTEST)
sc_btnx:    dw SC_B1X, SC_B2X, SC_B0X
sc_btnl:    dw sc_s_ok, sc_s_cancel, sc_s_test

sv_win:     dw 0                ; the settings window, 0 = none. THE GATE that
                                ; SSV_BUSY answers from (SPEC.md 79.8)
sc_modes:   db SS_M_ALL         ; the check boxes, seeded at every open
sc_testing: db 0                ; a Test is running: the session's end owes a
                                ; post, which is what restores [ss_idle]
sc_down:    db 0                ; which button is drawn pressed
sc_r:       dw 0, 0, 0, 0       ; ONE button's rect at a time - os88ui_krect's
                                ; argument, one window over
sc_t0:      times SC_TLEN db 0  ; the two fields' text...
sc_t1:      times SC_TLEN db 0
sc_f0:      times OS88LINE_SZ db 0  ; ...and their line blocks
sc_f1:      times OS88LINE_SZ db 0
