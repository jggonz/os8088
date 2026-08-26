; =============================================================================
; os8088 - tests/muptest/muptest.asm
;
; The gate for SPEC.md 13.7: a package's mouse-up. Not shipped software - it
; exists to be driven by a harness and to make the contract's three claims
; observable from OUTSIDE the guest, which is why every answer it gives is a
; WINDOW that is either there or not. The harness reads wm_wins; nothing here
; has to be screen-scraped, and nothing here has to be believed.
;
; One window. W_ONCLICK records the press and draws nothing. W_ONMOUSEUP
; HIDES the window - so "did the release arrive" is a question the harness can
; answer with a memory read rather than a picture.
;
;   press inside, release inside     -> gone     (it arrives at all)
;   press inside, release OUTSIDE    -> gone     (13.7's second rule: a
;                                                 release outside the window
;                                                 is still delivered, or a
;                                                 package could never cancel)
;   press on the TITLE BAR, release  -> still up (13.7's first rule: no
;                                                 W_ONCLICK ran, so no
;                                                 release is owed)
;
; The third is the one worth having a package for: it cannot be tested from
; the kernel side, because the kernel has no way to know a package expected
; nothing.
;
; It also records what it was HANDED, at a fixed offset in its own bss, so a
; harness that wants the coordinates can find them - mup_log is the first
; thing in bss and the loader reports the package's segment.
;
; SINCE apps/os88ui.inc IT IS THE GATE FOR THAT TOO: the two buttons are
; drawn by os88ui_btn, hit by os88ui_bfind and armed/fired by the pair, so
; the release rules above and the shared control's arm are proved by the same
; four gestures. Hiding on a MATCHED press-and-release is what makes
; "released over the same button" a question the harness can answer with a
; memory read.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MUPTEST', mu_entry

MU_W        equ 240                 ; frame
MU_H        equ 100
MU_VW       equ 160                 ; ...and the VANISH window (SPEC.md 13.7's
MU_VH       equ 60                  ; second guard - see mu_vclick)

; -----------------------------------------------------------------------------
mu_entry:
    push ax
    push si
    ; THE VANISH WINDOW IS CREATED FIRST, and that ordering is the whole of
    ; what this cost to get right: loader.inc shows the window whose pointer
    ; THE ENTRY PROC RETURNS IN BX, so a second wm_create after the main one
    ; silently hands the loader the wrong window - the main one is created,
    ; never shown, and the package looks like it did not launch. Create the
    ; unbound one first, show it by hand, and let the main one be last.
    mov si, mu_vtpl
    call OSAPI_WM_CREATE
    jc .main                         ; a full table is not a failure here -
    mov ax, mu_vup                   ; the other four gestures still work
    call OSAPI_WM_ONMOUSEUP
    call OSAPI_GFX_LOCK              ; legal: the entry runs with the lock
    call OSAPI_WM_SHOW               ; FREE (SPEC.md 20.2), unlike a callback
    call OSAPI_GFX_UNLOCK
.main:
    mov si, mu_tpl
    call OSAPI_WM_CREATE             ; BX = window ptr, CF on table full
    jc .out
    mov [mu_win], bx                 ; ...banked: mu_vclick retitles it, being
                                     ; the only thing it can leave behind
    mov ax, mu_onup
    call OSAPI_WM_ONMOUSEUP          ; SPEC.md 13.7 - AFTER wm_create, never
                                     ; in the template (the copy is 8 words)
    clc
.out:
    pop si
    pop ax
    ret                              ; BX = the MAIN window: the loader's
                                     ; wm_show and inst_bind_win both take it

; -----------------------------------------------------------------------------
; The VANISH window - docs/MOUSEUP-PLAN.md 4.2, "the armed window may be gone"
;
; The one guard in the mouse-up work that no gesture could reach: between the
; press and the release, a package can tear its own window down, and
; ui_dispatch's .mup_pkg has to notice before it dispatches through a record
; the kernel has freed.
;
; It has to be a SECOND window. A package's own window is instance-owned and
; closing it tears the instance down - the kernel frees the record itself and
; the image goes with it - so there would be nothing left to observe from.
; A second one is UNBOUND (SPEC.md 38.1), which is exactly what
; OSAPI_WM_DESTROY is for: it frees the RECORD while the image lives on.
;
; The signal is the MAIN window's caption, because the vanish window is by
; then the thing under test and cannot report on itself:
;
;   'Pressed'      mu_vclick ran, so the press WAS dispatched and the arm
;                  WAS set - without this the test cannot tell a working
;                  guard from a gesture that never armed anything
;   'GuardFailed'  mu_vup ran: the release was dispatched through a freed
;                  record, which is the bug
;
; A pass is 'Pressed' still standing after the release.
; -----------------------------------------------------------------------------
mu_vpaint:
    push ax
    push bx                         ; W_PAINT preserves EVERYTHING (SPEC.md 11)
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, 8
    add dx, 20
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, mu_s_vanish
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mu_vclick:
    push ax
    push bx
    mov bx, [mu_win]                ; say so BEFORE vanishing: after the
    mov ax, mu_t_press              ; destroy there is no window to say it from
    call OSAPI_WM_TITLE
    mov bx, si                      ; ...and now free the record the kernel is
    call OSAPI_WM_DESTROY           ; about to arm the release against. The gfx
                                    ; lock is held, which is what it wants
    pop bx
    pop ax
    ret

mu_vup:
    push ax
    push bx
    mov bx, [mu_win]
    mov ax, mu_t_fail
    call OSAPI_WM_TITLE
    pop bx
    pop ax
    ret

mu_win:     dw 0

; -----------------------------------------------------------------------------
; mu_paint - W_PAINT: one line, so the window is visibly there
; -----------------------------------------------------------------------------
mu_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, mu_s_wait
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = this window's ground
    call OSAPI_FONT_RUN
    call mu_layout                  ; the two buttons follow the window
    mov bx, mu_rects
    mov si, mu_l_a
    xor di, di
    call os88ui_btn
    mov bx, mu_rects+8
    mov si, mu_l_b
    mov di, OS88UI_DEF              ; ...and one of them proves the ring
    call os88ui_btn
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mu_onclick - W_ONCLICK: bank the press and draw NOTHING.
; in:  CX = x, DX = y, SI = window; gfx lock held
;
; Drawing nothing is deliberate: the harness's "did anything happen" is the
; window's existence, and a repaint here would put pixels in the way of that
; being the only signal.
; -----------------------------------------------------------------------------
mu_onclick:
    push ax
    push bx
    mov [mu_log + 0], cx
    mov [mu_log + 2], dx
    inc word [mu_log + 8]           ; presses seen
    call mu_layout
    mov ax, 2
    mov bx, mu_rects
    call os88ui_bfind               ; AX = button+1, 0 = off both
    mov [mu_log + 12], ax
    call os88ui_arm                 ; a press ACTS ON NOTHING
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mu_onup - W_ONMOUSEUP (SPEC.md 13.7): bank the release and HIDE.
; in:  CX = x, DX = y, SI = window; gfx lock held, billed like W_ONCLICK
;
; CX/DX are SCREEN coordinates and may be outside the content box - that is
; the rule this package exists to prove, so they are banked raw and nothing
; here range-tests them.
;
; OSAPI_WM_HIDE wants the gfx lock held, which it is (SPEC.md 11).
; -----------------------------------------------------------------------------
mu_onup:
    push ax
    push bx
    push di
    mov [mu_log + 4], cx
    mov [mu_log + 6], dx
    inc word [mu_log + 10]          ; releases seen
    call mu_layout
    call os88ui_fire                ; AX = what the press armed, cleared
    mov [mu_log + 14], ax
    or ax, ax
    jz .out                         ; the press was on neither button
    mov di, ax
    mov ax, 2
    mov bx, mu_rects
    call os88ui_bfind               ; ...and the release?
    cmp ax, di
    jne .cancel                     ; a different button, or none: CANCELLED
    cmp di, 2                       ; button Two: put up the Standard File
    jne .hide                       ; dialog, which is the ONLY way to see
    mov bx, si                      ; fdlg_btn's own drawing - its default
    mov al, FDLG_SAVE               ; button, its greying and its 2px ring
    mov di, mu_fdone                ; (SPEC.md 38). Save mode, because that is
    xor si, si                      ; the one that starts REFUSED and so shows
    call OSAPI_FILE_DLG             ; rule 1 doing its job
    jmp short .out
.hide:
    mov bx, si                      ; matched: hide, which is the harness's
    call OSAPI_WM_HIDE              ; whole signal
    jmp short .out
.cancel:
    mov bx, si                      ; the release ARRIVED and was refused, and
    mov ax, mu_t_no                 ; those are different facts, so the caption
    xor byte [mu_tog], 1            ; says so - and it TOGGLES between two
    jz .t2                          ; strings rather than latching, because a
    mov ax, mu_t_no2                ; harness has to see EVERY refused release
.t2:                                ; and not just the first (SPEC.md 11.92)
    call OSAPI_WM_TITLE
.out:
    pop di
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mu_fdone - the dialog's completion proc. Nothing to do: the gate is what the
; dialog LOOKED like, not what it returned.
; -----------------------------------------------------------------------------
mu_fdone:
    ret

; -----------------------------------------------------------------------------
; mu_layout - the two button rects, from the live content origin
; in:  SI = window (mu_onclick/mu_onup/mu_paint all have it)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mu_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, 16
    mov [mu_rects+0], cx
    add cx, 63
    mov [mu_rects+4], cx
    mov cx, ax
    add cx, 100
    mov [mu_rects+8], cx
    add cx, 63
    mov [mu_rects+12], cx
    mov cx, dx
    add cx, 40
    mov [mu_rects+2], cx
    mov [mu_rects+10], cx
    add cx, 19
    mov [mu_rects+6], cx
    mov [mu_rects+14], cx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

mu_rects:   times 8 dw 0            ; two {x1,y1,x2,y2}, screen coords
mu_l_a:     db 'One', 0
mu_l_b:     db 'Two', 0

; -----------------------------------------------------------------------------
mu_tpl:
    dw 120, 60, MU_W, MU_H
    dw mu_title, mu_paint, 0, mu_onclick

mu_vtpl:                            ; clear of the main window AND of the
    dw 400, 60, MU_VW, MU_VH        ; drive column on a 640px screen
    dw mu_vtitle, mu_vpaint, 0, mu_vclick

mu_title:   db 'MupTest', 0
mu_t_no:    db 'Cancelled', 0
mu_t_no2:   db 'Cancelled.', 0
mu_tog:     db 0
mu_s_wait:  db 'press me', 0
mu_vtitle:  db 'Vanish', 0
mu_s_vanish: db 'press me too', 0
mu_t_press: db 'Pressed', 0
mu_t_fail:  db 'GuardFailed', 0

; --- the shared controls ------------------------------------------------------
%include "os88ui.inc"

    OS88_BSS 16
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
; mu_log is FIRST and at offset 0 on purpose: a harness that wants the
; coordinates reads the package's segment + os88_image_end, and "the first
; thing in bss" is the only address it can derive without a map.
mu_log      equ os88_image_end + 0   ; +0 press x, +2 press y,
                                     ; +4 release x, +6 release y,
                                     ; +8 presses seen, +10 releases seen,
                                     ; +12 button under the press,
                                     ; +14 what fire() answered
