; Shared one-line editor, isolated from game shortcuts. SPEC.md 83, 114.
wf_chat_begin:
    mov byte [wf_chatmode], 1
    mov word [wf_buttons+OS88UI_BT_N], 0
    mov byte [wf_line+LN_FOCUS], 1
    call wf_geometry
    cmp byte [wf_colour], 0
    je .mono
    mov word [wf_buttons+OS88UI_BT_N], 5
    call wf_update              ; Send lights, the rest grey
    jmp wf_c_entry
.mono:
    cmp word [wf_cellh], 8
    je .field
    mov word [wf_buttons+OS88UI_BT_N], 5
    call wf_controls
.field:
    jmp wf_chat_draw

wf_chat_key:
    cmp al, 27
    je wf_chat_end
    cmp al, 13
    je .send
    mov si, wf_line
    call os88line_key           ; it EDITS; drawing is the caller's, and
    jc .out                     ; nothing drew the typed text before
    jmp wf_c_entry
.send:
    call wf_chat_submit
    jc .out
    mov si, wf_line
    call os88line_resync
    jmp wf_chat_end
.out:
    ret

wf_chat_submit:
    cmp byte [wf_chatpending], 0
    jne .bad
    mov cx, [wf_line+LN_LEN]
    jcxz .bad
    mov [wf_chatlength], cl
    push ds
    pop es
    mov si, wf_input
    mov di, wf_chattext
    rep movsb
    mov byte [wf_chatpending], 1
    mov byte [wf_urgent], 1
    mov si, wf_youprefix
    mov di, wf_chatline
    mov cx, 6
    rep movsb
    mov si, wf_input
    mov cl, [wf_chatlength]
    xor ch, ch
    rep movsb
    mov byte [di], 0
    call wf_chat_append
    mov byte [wf_input], 0
    mov word [wf_line+LN_LEN], 0
    mov word [wf_line+LN_CAR], 0
    mov word [wf_line+LN_VIEW], 0
    clc
    ret
.bad:
    stc
    ret

wf_chat_end:
    mov byte [wf_chatmode], 0
    mov byte [wf_line+LN_FOCUS], 0
    mov word [wf_buttons+OS88UI_BT_N], 5
    cmp byte [wf_colour], 0
    je .mono
    call wf_update
    jmp wf_c_entry
.mono:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    xor ax, ax
    mov bx, [wf_controlsy]
    mov cx, [wf_w]
    dec cx
    mov dx, [wf_h]
    dec dx
    call wf_fill
    push ds
    pop es
    mov di, wf_buttonshown
    mov cx, 10
    xor ax, ax
    rep stosw
    mov word [wf_footershown], 0
    jmp wf_update

; The entry's own focus look, after the buttons have changed.
wf_c_entry:
    mov bx, [wf_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov si, wf_line
    jmp os88line_draw
.out:
    ret

wf_chat_geometry:
    mov word [wf_line+LN_BUF], wf_input
    mov word [wf_line+LN_MAX], 33
    cmp byte [wf_colour], 0
    je .mono
    mov ax, [wf_ox]             ; 114.10: in the chat card, beside Send
    add ax, 8
    mov [wf_line+LN_X1], ax
    add ax, [wf_w]
    sub ax, 88
    mov [wf_line+LN_X2], ax
    mov ax, [wf_oy]
    add ax, [wf_h]
    sub ax, 31
    mov [wf_line+LN_Y1], ax
    add ax, 14
    mov [wf_line+LN_Y2], ax
    ret
.mono:
    mov ax, [wf_ox]
    add ax, 16
    mov [wf_line+LN_X1], ax
    mov ax, [wf_ox]
    add ax, [wf_w]
    sub ax, 17
    mov [wf_line+LN_X2], ax
    mov ax, [wf_oy]
    add ax, [wf_h]
    sub ax, 18
    cmp word [wf_cellh], 8
    jne .top
    mov ax, [wf_oy]
    add ax, [wf_controlsy]
.top:
    mov [wf_line+LN_Y1], ax
    mov ax, [wf_oy]
    add ax, [wf_h]
    sub ax, 2
    mov [wf_line+LN_Y2], ax
    ret

wf_chat_append:
    push ds
    pop es
    mov si, wf_chatlog+40
    mov di, wf_chatlog
    mov cx, 80
    rep movsb
    mov si, wf_chatline
    mov cx, 40
    rep movsb
    inc word [wf_chatcount]
    ret

wf_chat_draw:
    clc
    call OSAPI_GFX_PEN
    cmp word [wf_cellh], 8
    jne .large
    cmp byte [wf_chatmode], 0
    je .out
    mov si, wf_line
    call os88line_draw
    ret
.large:
    mov si, wf_chatlog
    mov bp, 3
    mov dx, [wf_h]
    sub dx, 52
.line:
    push si
    mov cx, 16
    call wf_paddedline
    pop si
    add si, 40
    add dx, 10
    dec bp
    jnz .line
    mov si, wf_line
    call os88line_draw
.out:
    ret

; SI=text, CX/DX relative position. A full opaque line removes a longer
; previous status or chat line without an erase-then-letter pair.
wf_paddedline:
    push si
    push cx
    push es
    push ds
    pop es
    mov di, wf_linebuf
    mov ax, '  '
    mov cx, 36
    rep stosw
    mov byte [di], 0
    pop es
    pop cx
    pop si
    mov di, wf_linebuf
    mov bx, 72
.copy:
    lodsb
    or al, al
    jz .draw
    mov [di], al
    inc di
    dec bx
    jnz .copy
.draw:
    mov si, wf_linebuf
    mov ax, (CWHITE << 8) | CBLACK
    jmp wf_text
