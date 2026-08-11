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
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MUPTEST', mu_entry

MU_W        equ 240                 ; frame
MU_H        equ 100

; -----------------------------------------------------------------------------
mu_entry:
    push ax
    push si
    mov si, mu_tpl
    call OSAPI_WM_CREATE             ; BX = window ptr, CF on table full
    jc .out
    mov ax, mu_onup
    call OSAPI_WM_ONMOUSEUP          ; SPEC.md 13.7 - AFTER wm_create, never
                                     ; in the template (the copy is 8 words)
    clc
.out:
    pop si
    pop ax
    ret

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
    call OSAPI_FONT_STR
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
    mov [mu_log + 0], cx
    mov [mu_log + 2], dx
    inc word [mu_log + 8]           ; presses seen
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
    mov [mu_log + 4], cx
    mov [mu_log + 6], dx
    inc word [mu_log + 10]          ; releases seen
    push bx
    mov bx, si
    call OSAPI_WM_HIDE
    pop bx
    ret

; -----------------------------------------------------------------------------
mu_tpl:
    dw 120, 60, MU_W, MU_H
    dw mu_title, mu_paint, 0, mu_onclick

mu_title:   db 'MupTest', 0
mu_s_wait:  db 'press me', 0

    OS88_BSS 12
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
; mu_log is FIRST and at offset 0 on purpose: a harness that wants the
; coordinates reads the package's segment + os88_image_end, and "the first
; thing in bss" is the only address it can derive without a map.
mu_log      equ os88_image_end + 0   ; +0 press x, +2 press y,
                                     ; +4 release x, +6 release y,
                                     ; +8 presses seen, +10 releases seen
