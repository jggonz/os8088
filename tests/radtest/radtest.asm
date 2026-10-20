; =============================================================================
; os8088 - tests/radtest/radtest.asm
;
; THE RADIO GROUP'S GATE (SPEC.md 13.17): a window with two groups in it, one
; of them with a greyed row, and nothing else.
;
; WHAT IT IS FOR is the three things a driving row cannot see any other way:
;
;   - THE PIXELS. The control's whole reason is a shape that is not a square
;     (docs/plans/completed/CTRL-GLYPH-PLAN.md), and a shape is not assertable from a
;     count. The row reads the framebuffer and checks the ring's four corners
;     are CLEAR while its edges are set - which is the difference between this
;     control and os88ui_chk's, stated as pixels.
;   - THAT A PRESS MOVES THE PICK AND REPAINTS TWO ROWS AND NOT THE GROUP.
;     [rt_paints] counts whole-group paints, so a press that repainted the
;     group would raise it and a press that repainted two rows does not.
;   - THAT A GREYED ROW AND THE ALREADY-PICKED ROW ARE BOTH SWALLOWED. Both
;     are CF = 0 / ZF = 0, which is the answer a caller acts on, and neither
;     may move the pick.
;
; It exercises NO kernel path of its own: every drawing call in it belongs to
; apps/os88ui.inc, which is the point - a gate for a library, not for a
; program.
;
; Prefix rt_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'RADTEST', rt_entry

RT_X       equ 8                    ; the groups' left, inside the content
RT_Y       equ 6
RT_W       equ 150                  ; ...and how far the labels' ground runs
RT_PITCH   equ 16                   ; group A's row pitch
RT_PITCH2  equ 20                   ; ...and group B's, DIFFERENT on purpose:
                                    ; the pitch is in the record because the
                                    ; Control Panel's pages disagree about it,
                                    ; so a gate with one pitch would not have
                                    ; tested the field at all
RT_GAP     equ 12                   ; between the two groups
RT_BSS     equ 16

; -----------------------------------------------------------------------------
rt_entry:
    push si
    mov si, rt_tpl
    call OSAPI_WM_CREATE
    mov [rt_win], bx
    pop si
    ret

; -----------------------------------------------------------------------------
; rt_place - both records' rects, from the window's content (it moves)
; in:  SI = the window
; -----------------------------------------------------------------------------
rt_place:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, RT_X
    add dx, RT_Y
    mov [rt_a+OS88UI_RD_RECT+0], ax
    mov [rt_a+OS88UI_RD_RECT+2], dx
    mov cx, ax
    add cx, RT_W
    mov [rt_a+OS88UI_RD_RECT+4], cx
    mov cx, dx
    add cx, RT_PITCH * 3 - 1
    mov [rt_a+OS88UI_RD_RECT+6], cx

    add cx, RT_GAP
    mov [rt_b+OS88UI_RD_RECT+0], ax
    mov [rt_b+OS88UI_RD_RECT+2], cx
    mov dx, cx
    mov cx, ax
    add cx, RT_W
    mov [rt_b+OS88UI_RD_RECT+4], cx
    mov cx, dx
    add cx, RT_PITCH2 * 2 - 1
    mov [rt_b+OS88UI_RD_RECT+6], cx

    add cx, RT_GAP              ; ...and the check box under them both
    mov [rt_c+0], ax
    mov [rt_c+2], cx
    mov dx, cx
    mov cx, ax
    add cx, RT_W
    mov [rt_c+4], cx
    mov cx, dx
    add cx, RT_PITCH - 1
    mov [rt_c+6], cx
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_paint - W_PAINT
;
; [rt_paints] is what makes the two-row claim testable: a press that repainted
; the GROUP would come back through here.
; -----------------------------------------------------------------------------
rt_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    inc word [rt_paints]
    call rt_place
    mov bx, rt_a
    xor di, di
    call os88ui_rad
    mov bx, rt_b
    xor di, di
    call os88ui_rad
    mov bx, rt_c
    xor di, di
    call os88ui_chk
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_onclick - W_ONCLICK: CX/DX = the point
;
; The three answers are counted separately, because they are what a caller
; branches on and a row that only counted "hits" could not tell a pick that
; moved from one that was swallowed.
; -----------------------------------------------------------------------------
rt_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, rt_a
    call os88ui_radhit
    jnc .ours
    mov bx, rt_b
    call os88ui_radhit
    jnc .ours
    mov bx, rt_c                ; ...and the check box, whose hit half answers
    call os88ui_chkhit          ; CF only: it toggled, or it was not ours
    jc .out
    inc word [rt_ctog]
    jmp short .out
.ours:
    jnz .swallowed
    inc word [rt_moved]
    jmp short .out
.swallowed:
    inc word [rt_swall]
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rt_onkey - W_ONKEY: any key redraws group A IN PLACE
;
; It exists for the harness rather than for a user: a driving row wants the
; drawing calls one full `os88ui_rad` makes, and the only other way to get a
; repaint is to move the window, which drags the WM's own painting into the
; capture. A caller redrawing a control in place is a supported thing to do
; (the gfx lock is held here exactly as it is in W_PAINT), so this asks for
; nothing the library does not already promise.
;
; [rt_paints] is NOT touched: it counts W_PAINT, and a row asserting "the group
; was not repainted" after a click must not be perturbed by this existing.
; -----------------------------------------------------------------------------
rt_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, rt_a
    xor di, di
    call os88ui_rad
    mov bx, rt_c                ; ...and the CHECK BOX, which is what makes the
    xor di, di                  ; harness able to see rule 1 on it: chkhit
    call os88ui_chk             ; redraws the MARK, so a toggle never reaches
    pop di                      ; os88ui_chk's own ground handling at all
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rt_tpl:
    dw 120, 30, 180, 150        ; TALL ENOUGH FOR ALL THREE. Group A is
                                ; 3 x RT_PITCH, group B 2 x RT_PITCH2 and the
                                ; check box one more row, with RT_GAP between
                                ; each - 134 of content. At 108 the check box
                                ; was drawn BELOW the window and every press on
                                ; it missed, which reads as the control being
                                ; broken rather than as the test's own layout
    dw rt_ttl, rt_paint, rt_onkey, rt_onclick

rt_ttl:  db 'Radio', 0

; --- group A: three live rows -------------------------------------------------
rt_a:
    dw 0, 0, 0, 0               ; OS88UI_RD_RECT, filled by rt_place
    dw rt_a_items               ; OS88UI_RD_ITEMS
    dw 3                        ; OS88UI_RD_N
    dw 0                        ; OS88UI_RD_SEL
    dw RT_PITCH                 ; OS88UI_RD_PITCH
    dw 0                        ; OS88UI_RD_DIS
rt_a_items:
    dw rt_a0, rt_a1, rt_a2
rt_a0:   db 'Bright', 0
rt_a1:   db 'Dark', 0
rt_a2:   db 'Colour', 0

; --- group B: TWO rows, the second greyed, and a WIDER pitch ------------------
; Row 1 greyed is the case SPEC.md 47 rule 2 is about and the one a press must
; swallow; the pitch differs from A's so the gate reads a record field rather
; than a constant it could have got right by luck.
rt_b:
    dw 0, 0, 0, 0
    dw rt_b_items
    dw 2
    dw 0
    dw RT_PITCH2
    dw 2                        ; OS88UI_RD_DIS: bit 1 - row 1 is greyed
rt_b_items:
    dw rt_b0, rt_b1
rt_b0:   db 'On', 0
rt_b1:   db 'Off', 0

; --- the CHECK BOX (SPEC.md 13.15), which had no gate in the tree at all -----
; Here rather than in a package of its own because it is the radio's neighbour
; in one file and shares its shapes since 13.15.1: a change to os88ui_gsqm
; moves BOTH controls, so one gate should see both.
rt_c:
    dw 0, 0, 0, 0               ; OS88UI_CK_RECT, filled by rt_place
    dw rt_c0                    ; OS88UI_CK_LABEL
    db 0                        ; OS88UI_CK_ON
    db 0                        ; (pad to OS88UI_CK_SIZE)
rt_c0:   db 'Sound', 0


; --- THE LIBRARY GOES LAST, which is skies' own placement (apps/skies:2955) ---
; os88ui.inc EMITS CODE, and OS88_HEADER has to be the image's first bytes - so
; an include in front of it puts a `mov` where the name should be and os88pkg
; refuses the package with "name contains non-printable byte". Nothing about
; the control; everything about where a package's header lives.
%define OS88UI_RAD                  ; ...the whole subject
%define OS88UI_CHK                  ; ...and its neighbour, which had NO gate
%include "os88ui.inc"

    OS88_BSS RT_BSS
    OS88_IMAGE_END

rt_win     equ os88_image_end + 0    ; word: our window
rt_paints  equ os88_image_end + 2    ; word: whole-group paints
rt_moved   equ os88_image_end + 4    ; word: presses that MOVED the pick
rt_swall   equ os88_image_end + 6    ; word: ...and presses swallowed
rt_ctog    equ os88_image_end + 8    ; word: check-box toggles
