; =============================================================================
; os8088 - apps/freedos/freedos.asm
;
; FREEDOS (SPEC.md 86.2). The package that hands the machine to FreeDOS.
;
; It carries none of FreeDOS and could not: APP_MAX_SIZE is 60KB and
; KERNEL.SYS plus COMMAND.COM are about 158KB between them, which is why
; FreeDOS gets a floppy of its own (SPEC.md 86.1). This is a window, a warning
; and one call.
;
; THE WARNING IS THE POINT OF IT. Starting FreeDOS ends the os8088 session -
; every window, the clipboard and anything unsaved go with it, and the way back
; is a reboot (SPEC.md 86.5). That is not a thing to spring on someone who
; double-clicked an icon to find out what it was. So the window says so in
; plain words and waits for a second, deliberate click; the close box is the
; cancel, which is why there is no Cancel button competing with it.
;
; The Start handler calls OSAPI_BOOT_UNIT and then RETURNS NORMALLY. The slot
; posts the handover rather than performing it, because this callback runs on
; the UI task with the gfx lock held and the teardown path takes that lock -
; the same reason OSAPI_REBOOT is a post. ui_task spends it a moment later with
; nothing held.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'FREEDOS', fd_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A 5.25" floppy: outer shell, the shutter slot at the top and the label panel
; at the bottom. The disk is the honest picture of this package - FreeDOS is
; not something os8088 runs, it is a disk the machine boots instead.
;
; The mask is the solid shell rectangle, so the outline always sits on a clean
; white underlay whatever the desktop pattern behind it (SPEC.md 39.4: on a
; 1bpp adapter grey rounds to black, so a silhouette is the only thing that
; reads).
    OS88_ICON16
    ; mask: the shell, filled
    dw 0000000000000000b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0000000000000000b
    dw 0000000000000000b
    ; data: the outline
    dw 0000000000000000b
    dw 0011111111111100b
    dw 0010000000000100b
    dw 0010011111100100b        ; shutter
    dw 0010010000100100b
    dw 0010010000100100b
    dw 0010011111100100b
    dw 0010000000000100b
    dw 0010000000000100b
    dw 0010111111110100b        ; label panel
    dw 0010100000010100b
    dw 0010100000010100b
    dw 0010100000010100b
    dw 0011111111111100b
    dw 0000000000000000b
    dw 0000000000000000b
    OS88_ICON16_END

FD_W       equ 272              ; outer window
FD_H       equ 104
FD_CONT_W  equ FD_W - 2         ; 270: two 1px borders
FD_CONT_H  equ FD_H - TITLE_H - 1

FD_TEXT_X  equ 8                ; the warning block, left-aligned: a centred
FD_TEXT_Y  equ 8                ; paragraph is harder to read and this one has
FD_LINE_H  equ 12               ; to be read

FD_BTN_W   equ 120
FD_BTN_H   equ 18
FD_BTN_X   equ (FD_CONT_W - FD_BTN_W) / 2
FD_BTN_Y   equ 56

; -----------------------------------------------------------------------------
; fd_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader shows the window; we must not show, draw or spawn here.
; -----------------------------------------------------------------------------
fd_entry:
    push si
    mov si, fd_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    ret

; -----------------------------------------------------------------------------
; fd_paint - W_PAINT: the warning, then the button
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
;
; The content origin is fetched every call because the window moves, and the
; window is not resizable, so FD_CONT_W/H are the whole geometry.
; -----------------------------------------------------------------------------
fd_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [fd_ox], ax
    mov [fd_oy], dx

    mov si, fd_lines
    mov di, FD_TEXT_Y
.line:
    mov ax, [si]                    ; whole-word package address (SPEC.md 20.2)
    or ax, ax
    jz .button
    push si
    mov cx, [fd_ox]
    add cx, FD_TEXT_X
    mov dx, [fd_oy]
    add dx, di
    mov si, ax
    mov ax, (CWHITE << 8) | CBLACK  ; AL = ink, AH = the content's own ground:
    call OSAPI_FONT_RUN             ; W_PAINT arrives white-filled, so the cells
                                    ; are lettered in ONE pass (SPEC.md 6.1)
    pop si
    add si, 2
    add di, FD_LINE_H
    jmp short .line

.button:
    call fd_btn

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fd_btn - the Start button: a frame and a centred caption
; in:  [fd_ox]/[fd_oy] current; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
fd_btn:
    push ax
    push bx
    push cx
    push dx
    push si

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [fd_ox]
    add ax, FD_BTN_X
    mov bx, [fd_oy]
    add bx, FD_BTN_Y
    mov cx, ax
    add cx, FD_BTN_W-1
    mov dx, bx
    add dx, FD_BTN_H-1
    call OSAPI_GFX_FRAME

    mov si, fd_s_start
    call OSAPI_FONT_WIDTH           ; AX = label width, px
    mov cx, FD_BTN_W
    sub cx, ax
    shr cx, 1
    add cx, [fd_ox]
    add cx, FD_BTN_X                ; centred in the button
    mov dx, [fd_oy]
    add dx, FD_BTN_Y + (FD_BTN_H - 8) / 2
    mov ax, (CWHITE << 8) | CBLACK  ; the frame is a line, not a fill, so the
    call OSAPI_FONT_RUN             ; caption's ground is the content's white

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fd_onclick - W_ONCLICK: the second, deliberate click
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing
;
; Anywhere outside the button does nothing at all - no beep, no acknowledgement.
; This is the one control in the system where a click that ALMOST hit should
; cost nothing, so a near miss is silence rather than a machine that reboots.
;
; OSAPI_BOOT_UNIT returns (SPEC.md 86.2), and this proc returns after it, and
; the desktop carries on for another pass or two before ui_task spends the
; post. That is the contract, not a race: everything the teardown needs is done
; on the UI task with nothing held.
; -----------------------------------------------------------------------------
fd_onclick:
    push ax
    push bx
    push cx
    push dx
    push si

    mov bx, si                      ; the window, for wm_content
    push cx                         ; the click, across the call
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov si, dx                      ; park the TOP: the pop below needs DX, and
                                    ; the window ptr is already on the stack
    pop dx                          ; click y
    pop cx                          ; click x
    sub cx, ax                      ; -> content-relative
    sub dx, si

    cmp cx, FD_BTN_X
    jl .out
    cmp cx, FD_BTN_X + FD_BTN_W
    jge .out
    cmp dx, FD_BTN_Y
    jl .out
    cmp dx, FD_BTN_Y + FD_BTN_H
    jge .out

    mov al, 1                       ; the second floppy (SPEC.md 86.4)
    call OSAPI_BOOT_UNIT
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
fd_tpl:
    dw 180, 130, FD_W, FD_H         ; x, y, w, h
    dw fd_ttl, fd_paint, 0, fd_onclick

fd_ttl:     db 'FreeDOS', 0

; The warning, one word-pointer per line, 0 ends it. Kept to 32 columns so it
; fits the content at 8px per glyph on every adapter.
fd_lines:
    dw fd_s_l1, fd_s_l2, fd_s_l3, 0

fd_s_l1:    db 'FreeDOS takes over the machine.', 0
fd_s_l2:    db 'os8088 closes; unsaved work is', 0
fd_s_l3:    db 'lost. Type OS8088 to come back.', 0
fd_s_start: db 'Start FreeDOS', 0

    OS88_BSS 4
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
fd_ox       equ os88_image_end + 0   ; word: content origin, refreshed by every
fd_oy       equ os88_image_end + 2   ; paint - the window moves
