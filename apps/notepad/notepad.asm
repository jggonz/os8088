; =============================================================================
; os8088 - apps/notepad/notepad.asm
;
; NOTEPAD, the third software package (SPEC.md 27) - formerly the built-in
; Note Pad app (KIND_NOTE). Moved out of the kernel to reclaim the 1,317
; bytes it cost there: 281 of code and 1,036 of .bss, nearly all of the
; latter a fixed two-instance text pool. As a package that pool disappears
; entirely - every instance is its own relocated copy with its own bss
; (SPEC.md 20.1), so the buffer below is simply per-instance, and the
; instance count is bounded by the region pool instead of a hard-coded 2.
;
; Behaviour is unchanged from the built-in (SPEC.md 14): printable 32..126
; append, backspace deletes, Enter stores a newline byte; text wraps at the
; content width with 6px margins, rows that would overflow the content
; bottom are dropped rather than scrolled, and a 1px caret follows the text
; when its own row fits.
;
; The state lookup is the one thing that got simpler. The built-in reached
; its state through inst_of_win -> I_SPTR because all instances shared one
; pool; a package addresses its own bss directly.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'NOTEPAD', np_entry

NP_CAP       equ 512            ; text buffer capacity, bytes
NP_BSS_TOTAL equ 520            ; see the bss layout after OS88_IMAGE_END
NP_MARGIN    equ 6              ; left/top text margin inside the content

; -----------------------------------------------------------------------------
; np_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
; -----------------------------------------------------------------------------
np_entry:
    push si
    mov si, np_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): np_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    pop ax                          ; so the next repaint re-wraps for free
    clc                             ; CF must still report the create result
.out:
    ret

; -----------------------------------------------------------------------------
; np_paint - W_PAINT: draw the buffer, then the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add ax, NP_MARGIN
    mov [np_tx], ax                 ; text origin x = the wrap column
    mov di, ax                      ; DI = pen x
    add dx, NP_MARGIN
    mov bp, dx                      ; BP = pen y
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, 2
    mov [np_rgt], ax                ; content right (inclusive)
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    sub ax, 2
    mov [np_bot], ax                ; content bottom (inclusive)

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov bx, [np_len]                ; BX = characters remaining
    mov si, np_buf

.chloop:
    test bx, bx
    jz .caret
    lodsb                           ; DF=0 per SPEC.md 1
    dec bx
    cmp al, 13
    jne .glyph
    mov di, [np_tx]                 ; newline: carriage return + line feed
    add bp, 8
    jmp .chloop

.glyph:
    mov cx, di                      ; wrap if the 8px cell would pass the edge
    add cx, 7
    cmp cx, [np_rgt]
    jbe .fits
    mov di, [np_tx]
    add bp, 8
.fits:
    mov cx, bp                      ; vertical clip: drop rows that overflow,
    add cx, 7                       ; but keep advancing the pen so the caret
    cmp cx, [np_bot]                ; position stays true
    ja .advance
    mov cx, di
    mov dx, bp
    call OSAPI_FONT_CHAR            ; AL still holds the character
.advance:
    add di, 8
    jmp .chloop

.caret:
    mov cx, di                      ; the caret occupies the next cell, so it
    add cx, 7                       ; wraps exactly like a character would
    cmp cx, [np_rgt]
    jbe .cfits
    mov di, [np_tx]
    add bp, 8
.cfits:
    mov cx, bp
    add cx, 7
    cmp cx, [np_bot]
    ja .done                        ; caret row does not fit: no caret
    mov ax, di                      ; 1px black caret, 8 rows tall
    mov bx, bp
    mov dx, bp
    add dx, 7
    call OSAPI_GFX_VLINE

.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_onkey - W_ONKEY: edit the buffer, then repaint our own content
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing; preserves all registers
; Unhandled keys touch nothing; a full buffer drops the keystroke silently.
; -----------------------------------------------------------------------------
np_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    cmp al, 8
    je .bksp
    cmp al, 13
    je .append
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out

.append:
    mov bx, [np_len]
    cmp bx, NP_CAP
    jae .out                        ; full: drop silently
    mov [bx+np_buf], al
    inc word [np_len]
    jmp .redraw

.bksp:
    cmp word [np_len], 0
    je .out
    dec word [np_len]

.redraw:
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                       ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                       ; DX = y2
    pop bx                          ; BX = y1
    push ax                         ; the pen is a register here, not a
    mov al, CWHITE                  ; variable - keep x1 across the call
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL             ; white-fill the content
    call np_paint                   ; SI still = window ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; Same geometry the built-in used: 260x180 outer -> 258x160 content.
np_tpl:
    dw 60, 60, 260, 180
    dw np_ttl, np_paint, np_onkey, 0

np_ttl: db 'Note Pad', 0

    OS88_BSS NP_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = a fresh empty note with the caret at the origin.
np_len      equ os88_image_end + 0     ; word: characters used
np_buf      equ os88_image_end + 2     ; NP_CAP bytes of text
np_tx       equ os88_image_end + 514   ; word: paint scratch, text origin x
np_rgt      equ os88_image_end + 516   ; word: content right, inclusive
np_bot      equ os88_image_end + 518   ; word: content bottom, inclusive
                                       ; total 520 = NP_BSS_TOTAL
