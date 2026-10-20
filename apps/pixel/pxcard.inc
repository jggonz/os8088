; =============================================================================
; os8088 - apps/pixel/pxcard.inc
;
; THE PARAMETER CARDS (SPEC.md 106.24), the resident half: Brightness/
; Contrast..., Gamma..., Posterize..., Threshold..., Resize... and Save As's
; format - a card over the window's middle with a title, one or two rows of
; a label, `-`, a value and `+`, and OK and Cancel. A card of PiXEL's own
; (the About and keyboard cards' kind, drawn last over everything, SPEC.md
; 106.2) whose controls are buttons of the window's one record (SPEC.md
; 106.3) - pressed and released by the library's gesture, painted by the
; face like every other button - and whose records, layout, drawing, keys
; and buttons are the EDIT part's (EV_U*, apps/pixel/pxedit.asm), held
; while a card is up. What stays here is [px_pcon] and the dispatch.
;
; THERE IS NO LIVE PREVIEW: a new palette's plans are a search of about
; two seconds on a 4.77 MHz 8088 (SPEC.md 106.11) and the canvas's render
; seconds more, so a value a press would be a wait a press. OK applies.
; =============================================================================

px_s_pcok:  db 'OK', 0              ; (labels of the window's record: ours)
px_s_pccan: db 'Cancel', 0
px_s_pcsave: db 'Save...', 0
px_fditems: dw px_s_wpng, px_s_wgif, px_s_wbmp, px_s_wpcx, px_s_wpix
            dw px_s_wbmp24
px_s_wpng:  db 'PNG', 0
px_s_wgif:  db 'GIF', 0
px_s_wbmp:  db 'BMP', 0
px_s_wpcx:  db 'PCX', 0
px_s_wpix:  db 'PIX', 0
px_s_wbmp24: db 'BMP 24-bit', 0

; px_ecall - CL = a verb of the EDIT part's, called (fetched if it must be).
; out its CF/AX. Preserves BX, DX, SI
px_ecall:
    push bx
    mov [px_earg], ax               ; (AL is the part's number: a UI verb's
    mov al, PXPART_EDIT             ; argument rides in [px_earg])
    mov bl, PXV_DECODE
    call px_pcall
    pop bx
    ret

; px_pcopen - AL = a card: put up with its rows at their defaults (Save As:
; the format of the picture's own extension when PiXEL writes it, else
; PNG; BMP 24-bit offered on a CUBE master only). Lock held
px_pcopen:
    push ax
    push cx
    push di
    call px_carddown                ; one card at a time
    mov [px_pcon], al
    mov cx, px_s_pcok               ; OK's words: "OK", or "Save..."
    cmp al, PC_SAVE
    jne .l
    mov cx, px_s_pcsave
    push es                         ; THE DROP-DOWN'S RECORD (bss): items,
    push ds                         ; count, pick, closed
    pop es
    mov di, px_fdrop
    xor ax, ax
    push cx
    mov cx, PE_DRSZ / 2
    cld
    rep stosw
    pop cx
    pop es
    mov word [px_fdrop + PE_DR_ITEMS], px_fditems
    mov byte [px_fdrop + PE_DR_HOT], 0xFF
    mov ax, 5
    cmp byte [px_cur + PXR_PMODE], PM_CUBE
    jne .n
    inc ax
.n:
    mov [px_fdrop + PE_DR_N], ax
    call px_fmtof
    mov [px_fdrop + PE_DR_SEL], ax
.l:
    mov [px_lab_full + 2 * PX_B_COK], cx
    mov [px_lab_half + 2 * PX_B_COK], cx
    mov cl, EV_UOPEN
    call px_ecall
    jc .no
    call px_flags
    push bx                         ; THE CARD AND NOTHING ELSE (SPEC.md
    mov bx, [px_win]                ; 106.15): laid out - the buttons under
    call OSAPI_WM_CLIP_SET          ; it put out of reach - and drawn over
    jc .nc                          ; the canvas, which is left as it is
    call px_layout
    jc .cc
    call px_cards
.cc:
    call OSAPI_WM_CLIP_CLEAR
.nc:
    pop bx
    jmp short .x
.no:
    mov byte [px_pcon], 0
.x:
    pop di
    pop cx
    pop ax
    ret

; px_pcact - AX = what the card's key or press asked for (PCA_*). Lock held
px_pcact:
    cmp ax, PCA_OK
    je px_pcok
    cmp ax, PCA_CANCEL
    je px_pcdown
    ret

; px_pcfire - AX = one of the card's buttons that fired. Lock held
px_pcfire:
    push cx
    mov cl, EV_UFIRE
    call px_ecall
    pop cx
    jc .x
    call px_pcact
.x:
    ret

; px_pckey - a key while a card is up (AL/AH): CF = 1 spent. Lock held
px_pckey:
    cmp byte [px_pcon], 0
    jne .on
    clc
    ret
.on:
    push ax
    push cx
    mov cl, EV_UKEY
    call px_ecall
    jc .x
    call px_pcact
.x:
    pop cx
    pop ax
    stc
    ret

; px_pcdown - the card taken down, unanswered. Preserves all
px_pcdown:
    push ax
    mov byte [px_pcon], 0
    call px_flags
    mov al, PX_R_ALL
    call px_regpaint
    pop ax
    ret

; px_pcok - OK: the card's operation run with the card still up (so its own
; drawing waits for the repaint that takes the card down, and the picture
; is rendered once), then the card down; Save As: the card down, then the
; Standard File dialog. Lock held
px_pcok:
    push ax
    push cx
    test word [px_bflags + 2 * PX_B_COK], OS88UI_DIS
    jnz .x
    mov cl, EV_UOK
    call px_ecall                   ; AL = the verb, or 0FFh: Save As
    jc .x
    cmp al, 0xFF
    je .save
    call px_edo
    call px_pcdown
    jmp short .x
.save:
    call px_pcdown
    call px_sdlg
.x:
    pop cx
    pop ax
    ret
