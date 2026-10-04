; =============================================================================
; os8088 - apps/pixel/pxsave.inc
;
; SAVE AS, UNSAVED EDITS AND COPY (SPEC.md 106.24) - the resident half; the
; names, the job's start, the pump, the end and Copy's rect are the WRITE
; part's UI verbs (WV_U*, apps/pixel/pxwrite.asm), the encoding its worker
; half.
;
; SAVE AS is a card (the format: a drop-down - BMP 24-bit is offered on a
; CUBE master only), then the Standard File dialog (SPEC.md 38) for where
; and under what name - the name's own extension wins when it is one PiXEL
; writes, else the card's is put on it - then "Replace NAME?" (os88ui's
; alert, SPEC.md 75.3) when the name is taken. The WORKER encodes into the
; ring's slots and the UI task writes each full one from W_ONWAKE into a
; TEMPORARY file in the target folder; only when the last is down is the
; target deleted and the temporary renamed to it - so a full disk, a
; write-protected one or Esc leaves the old file as it was and no half file
; under the user's name. The worker may not touch a file (SPEC.md 20.6 rule
; 7), which is why the work is split there and nowhere else.
;
; UNSAVED EDITS are asked about (OS88UI_ASAVE: Save / Discard / Cancel)
; wherever the picture would be LEFT - px_navto (Prev, Next, Home, End, a
; card of the strip), File > Open, the slideshow, and the window's close -
; through px_gate, which banks what was asked for and does it once the
; answer allows. Save saves over the file when PiXEL writes its format and
; the master is the whole picture (not a scale of it), else asks where.
; Full screen does not ask: its Next and Prev say "Save the edit first"
; (pxfull.inc), since its glass has no room for a card.
;
; COPY puts an 8-bit BMP of the selection - or of the picture - on the
; system clipboard (SPEC.md 55): the WRITE part's BMP writer into one slot,
; and OSAPI_CLIP_PUT. The clipboard is CLIP_MAXKB, so a larger one is
; refused with its size.
; =============================================================================

PXL_NAV     equ 1                   ; px_gate's banked actions
PXL_OPEN    equ 2
PXL_SLIDE   equ 3
PXL_CLOSE   equ 4

px_s_pixbig: db 'PIX holds 64K at most', 0
px_s_qmark: db '?', 0
px_s_chg:   db 'Save changes to ', 0
px_s_sfirst: db 'Save the edit first', 0

; px_wcallu - CL = a verb of the WRITE part's, called (fetched if it must
; be). out its CF/AX. Preserves BX, DX, SI
px_wcallu:
    push bx
    mov [px_earg], ax
    mov al, PXPART_WRITE
    mov bl, PXV_DECODE
    call px_pcall
    pop bx
    ret

; px_saveas - File > Save As..., Ctrl+S, the toolbar's Save. Lock held
px_saveas:
    push ax
    push si
    mov si, px_s_nopico
    cmp byte [px_cur + PXR_HAVE], 0
    je .say
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    mov al, PC_SAVE
    call px_pcopen
    jmp short .x
.say:
    call px_toast
.x:
    pop si
    pop ax
    ret

; px_fmtof - AX = the WF_* of the shown picture's own format, PNG when PiXEL
; does not write it. Preserves all but AX
px_fmtof:
    push bx
    mov al, [px_cur + PXR_FMT]
    mov bx, px_wfmts
.l:
    cmp al, [bx]
    je .f
    inc bx
    cmp bx, px_wfmts + WF_PIX + 1
    jb .l
    mov bx, px_wfmts                ; (PNG)
.f:
    sub bx, px_wfmts
    mov ax, bx
    pop bx
    ret
px_wfmts:   db PXF_PNG, PXF_GIF, PXF_BMP, PXF_PCX, PXF_PIX

; px_sdlg - the Standard File dialog, Save, the name the picture's with the
; chosen format's extension. Lock held
px_sdlg:
    push ax
    push bx
    push cx
    push si
    push di
    mov cl, WV_UNAME
    call px_wcallu
    jc .x
    mov al, 1                       ; Save
    mov bx, [px_win]
    mov si, px_sname
    mov di, px_sdone
    call OSAPI_FILE_DLG
.x:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; px_sdone - the dialog's completion (SPEC.md 38.6): ES:DI = the name typed
; (the kernel's, valid for this call). The instance stands in the folder it
; goes to. Lock held
px_sdone:
    push si
    mov si, di
    mov di, px_sname
    mov cx, 12
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [px_sname + 12], 0
    pop si
    cmp byte [px_sname], 0
    je .x
    mov cl, WV_UDONE                ; its extension, its folder: taken?
    call px_wcallu
    jnc px_sgo
    cmp byte [px_pfail], 0
    jne .x
    mov al, OS88UI_AYESNO           ; "Replace NAME?"
    mov di, px_sreplace
    jmp px_ask
.x:
    ret

; px_sreplace - "Replace NAME?" answered: Yes (0) saves, anything else
; drops it (and a question banked behind it)
px_sreplace:
    or al, al
    jz px_sgo
    mov byte [px_lvpend], 0
    ret

; px_sgo - THE SAVE: [px_sname] in [px_sdir], format [px_wfmt]. The WRITE
; part asked whether the master can be written so; the parts a save does
; not need given back (a save's claims: the face, the master, the view, the
; store, undo's master, the part, the ring - SPEC.md 50.2's eight at most);
; the job started. Lock held
px_sgo:
    push ax
    push cx
    push si
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    mov byte [px_wclip], 0
    mov word [px_wsel], 0           ; the whole picture
    mov cl, [px_wfmt]
    or cl, WF_SIZE
    call px_wcallu
    jnc .ok
    cmp byte [px_pfail], 0
    jne .x
    mov si, px_s_pixbig
    jmp short .say
.ok:
    call px_wgo
    jnc .x
    mov si, px_s_r7
.say:
    call px_toast
.x:
    pop si
    pop cx
    pop ax
    ret

; px_wgo - the decoder and EDIT parts given back, the WRITE part's job
; started. CF = 1 no memory for it (the part dropped then). Preserves all
px_wgo:
    push ax
    push cx
    xor al, al                      ; (no decoder part: a save needs none)
    call px_kneed
    mov al, PXPART_EDIT
    call px_pdrop
    mov cl, WV_USTART
    call px_wcallu
    jnc .x
    mov al, PXPART_WRITE
    call px_pdrop
    stc
.x:
    pop cx
    pop ax
    ret

; px_wfree - the ring (or the copy's slot) given back. Preserves all
px_wfree:
    push dx
    mov dx, [px_ringraw]
    or dx, dx
    jz .x
    call OSAPI_MEM_FREE
    mov word [px_ringraw], 0
.x:
    pop dx
    ret

; px_wpump - W_ONWAKE, no lock: the full slots written (the WRITE part's
; WV_UPUMP; it is held while a save runs). Preserves all
px_wpump:
    push ax
    push cx
    mov cl, WV_UPUMP
    call px_wcallu
    pop cx
    pop ax
    ret

; px_sfin - the worker has answered a save or a copy (lock held): the WRITE
; part's end, then the part given back
px_sfin:
    push ax
    push cx
    mov byte [px_busy], 0
    mov cl, WV_UFIN
    call px_wcallu
    mov al, PXPART_WRITE
    call px_pdrop
    pop cx
    pop ax
    ret

; px_copy - Edit > Copy (Ctrl+C): the selection, or the picture, as an
; 8-bit BMP on the system clipboard. Lock held
px_copy:
    push ax
    push cx
    push si
    mov si, px_s_nopico
    cmp byte [px_cur + PXR_HAVE], 0
    je .say
    mov si, px_s_busy
    cmp byte [px_busy], 0
    jne .say
    mov cl, WV_UCOPY                ; the rect, and its size (refused: said)
    call px_wcallu
    jc .x
    call px_wgo
    jnc .x
    mov si, px_s_r7
.say:
    call px_toast
.x:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; UNSAVED EDITS: "Save changes to NAME?" (OS88UI_ASAVE, SPEC.md 75.3)
; =============================================================================

; px_ask - AL = the button set, DI = the completion: os88ui's alert over our
; window with [px_aq] - a buffer of its own, because the alert reads its
; words at every paint - and ES = KERNEL_SEG, which os88ui_ask reads our
; window's caption through. Preserves all
px_ask:
    push ax
    push bx
    push si
    push es
    mov bx, KERNEL_SEG
    mov es, bx
    mov bx, [px_win]
    mov si, px_aq
    call os88ui_ask
    pop es
    pop si
    pop bx
    pop ax
    ret

; px_gate - AL = what the user asked for (PXL_*; px_navto's name banked as
; [px_lvarg]): CF = 0 go on - nothing is unsaved; CF = 1 the question is up,
; and the action is done (px_lvdo) when its answer lets it. Lock held.
; Preserves all
px_gate:
    cmp byte [px_dirty], 0
    jne .ask
    clc
    ret
.ask:
    push ax
    push si
    push di
    mov [px_lvpend], al
    mov si, px_s_chg
    mov di, px_aq
    call px_strcpy
    mov si, px_cur + PXR_NAME
    call px_strcat
    mov si, px_s_qmark
    call px_strcat
    mov al, OS88UI_ASAVE
    mov di, px_asked
    call px_ask
    pop di
    pop si
    pop ax
    stc
    ret

; px_asked - the answer: Save (0), Discard (1), Cancel or dismissed. Lock
; held
px_asked:
    or al, al
    jz .save
    cmp al, 1
    je .disc
    mov byte [px_lvpend], 0
    ret
.disc:
    mov byte [px_dirty], 0
    jmp px_lvdo
.save:
    ; over the file itself when PiXEL writes its format and the master is
    ; the whole picture; else where, through the card (the action waits for
    ; the save's end either way)
    cmp byte [px_cur + PXR_SCL], 0
    jne .as
    mov al, [px_cur + PXR_FMT]
    cmp al, PXF_PNG
    je .own
    cmp al, PXF_GIF
    je .own
    cmp al, PXF_BMP
    je .own
    cmp al, PXF_PCX
    je .own
    cmp al, PXF_PIX
    jne .as
.own:
    call px_fmtof
    mov [px_wfmt], al
    mov si, px_cur + PXR_NAME
    mov di, px_sname
    call px_strcpy
    mov ax, [px_cur + PXR_DIR]
    mov [px_sdir], ax
    mov al, [px_cur + PXR_VOL]
    mov [px_svol], al
    jmp px_sgo
.as:
    jmp px_saveas

; px_lvdo - the action a "Save changes?" banked, now the picture may be
; left. Lock held
px_lvdo:
    push ax
    mov al, [px_lvpend]
    mov byte [px_lvpend], 0
    cmp al, PXL_NAV
    jne .o
    mov ax, [px_lvarg]
    call px_navto
    jmp short .x
.o:
    cmp al, PXL_OPEN
    jne .s
    call px_cmd_open
    jmp short .x
.s:
    cmp al, PXL_SLIDE
    jne .c
    call px_sltoggle
    jmp short .x
.c:
    cmp al, PXL_CLOSE
    jne .x
    push bx
    mov bx, [px_win]
    call OSAPI_WM_CLOSE             ; deferred (SPEC.md 75.2)
    pop bx
.x:
    pop ax
    ret

; px_onclose - the CLOSE NEGOTIATOR (SPEC.md 75.1): SI = the window, the
; lock held. A save still running refuses; unsaved edits ask
px_onclose:
    cmp byte [px_busy], PXB_SAVE
    jne .q
    push si
    mov si, px_s_busy
    call px_toast
    pop si
    stc
    ret
.q:
    push ax
    mov al, PXL_CLOSE
    call px_gate
    pop ax
    ret
