; =============================================================================
; os8088 - apps/hello/hello.asm
;
; HELLO, the second software package (SPEC.md 27). Deliberately minimal: it
; proves the SDK surface (header macro, wm_create template, paint via the
; API table) and the no-icon fallback - no flags bit 0, so the Disk window
; shows the built-in ico_app16 for it. One window, two centred lines of
; text, no onkey, no onclick, no bss.
;
; The entry point only creates the window (wm_create is lock-free) and
; returns BX = window ptr / CF clear; the loader shows it. The paint proc
; runs with the gfx lock already held (SPEC.md 11) and fetches the content
; origin via wm_content each call (the window moves).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'HELLO', hl_entry

HL_CONT_W equ 238                   ; content width: 240 outer - 2px borders

; -----------------------------------------------------------------------------
; hl_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here.
; -----------------------------------------------------------------------------
hl_entry:
    push si
    mov si, hl_tpl
    call OSAPI_WM_CREATE             ; BX = window ptr, CF on table full
    pop si
    ret

; -----------------------------------------------------------------------------
; hl_paint - W_PAINT: two centred lines on the white content
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
hl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov bx, ax                      ; keep the content left in BX
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, hl_s_line1              ; content is 71px tall; two 8px lines
    add dx, 25                      ; 12px apart, centred as a 20px block
    call hl_line
    mov si, hl_s_line2
    add dx, 12
    call hl_line
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hl_line - draw one line centred in the content width
; in:  SI = NUL string, BX = content left, DX = y
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
hl_line:
    push ax
    push cx
    call OSAPI_FONT_WIDTH            ; AX = pixel width
    mov cx, HL_CONT_W
    sub cx, ax
    shr cx, 1
    add cx, bx
    call OSAPI_FONT_STR
    pop cx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
hl_tpl:
    dw 200, 150, 240, 90            ; x, y, w, h -> content 238 x 71
    dw hl_ttl, hl_paint, 0, 0       ; no onkey, no onclick

hl_ttl:     db 'Hello', 0
hl_s_line1: db 'Hello from a', 0
hl_s_line2: db '.o88 package!', 0

    OS88_IMAGE_END
