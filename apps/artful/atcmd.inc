; =============================================================================
; os8088 - apps/artful/atcmd.inc
;
; ArtfulType's command layer (SPEC.md 46.6): at_docmd routes every command -
; menu picks and ^key shortcuts land here identically, the notepad two-doors
; rule - into the ported markdown.c operations. All of them edit the ONE
; canonical buffer: WrapSelection (toggle ** * ` ~~ around the selection),
; ToggleHeading (the # prefix, replacing any other level in place - the
; Writer-mode semantics), DoLink ([text]( with the caret parked inside the
; parens - in Writer mode the caret parks the paragraph raw, so the URL is
; typed into visible syntax and styles itself on the way out), and
; ClearMarkdownInSelection (None), which deletes exactly the characters the
; styled parser would hide - one definition of "markup", shared with the
; renderer, so the two can never disagree.
; =============================================================================

AT_CMD_NEW    equ 1
AT_CMD_OPEN   equ 2
AT_CMD_SAVE   equ 3
AT_CMD_SAVEAS equ 4
AT_CMD_QUIT   equ 5
AT_CMD_UNDO   equ 6
AT_CMD_REDO   equ 7
AT_CMD_CUT    equ 8
AT_CMD_COPY   equ 9
AT_CMD_PASTE  equ 10
AT_CMD_SELALL equ 11
AT_CMD_BOLD   equ 12
AT_CMD_ITALIC equ 13
AT_CMD_CODE   equ 14
AT_CMD_STRIKE equ 15
AT_CMD_H1     equ 16
AT_CMD_H2     equ 17
AT_CMD_H3     equ 18
AT_CMD_LINK   equ 19
AT_CMD_NONE   equ 20
AT_CMD_VIEWMD equ 21
AT_CMD_VIEWWR equ 22
AT_CMD_ZIN    equ 23
AT_CMD_ZOUT   equ 24
AT_CMD_ZDEF   equ 25
AT_CMD_ABOUT  equ 26
AT_CMD_MAX    equ 26

; -----------------------------------------------------------------------------
; at_docmd - run command AL
; in:  AL = AT_CMD_*; gfx lock held (a key handler or the menu tracker)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_docmd:
    push ax
    push bx
    xor bh, bh
    mov bl, al
    or bl, bl
    jz .out
    cmp bl, AT_CMD_MAX
    ja .out
    dec bx
    shl bx, 1
    call [bx+at_cmdtab]
.out:
    pop bx
    pop ax
    ret

at_cmdtab:
    dw at_cmd_new,    at_cmd_open,   at_cmd_save,   at_cmd_saveas
    dw at_cmd_quit,   at_undo,       at_redo,       at_cut
    dw at_copy,       at_paste,      at_selall,     at_cmd_bold
    dw at_cmd_italic, at_cmd_code,   at_cmd_strike, at_cmd_h1
    dw at_cmd_h2,     at_cmd_h3,     at_cmd_link,   at_cmd_none
    dw at_cmd_viewmd, at_cmd_viewwr, at_cmd_zin,    at_cmd_zout
    dw at_cmd_zdef,   at_cmd_about

; =============================================================================
; The Style menu
; =============================================================================

at_cmd_bold:
    push si
    push cx
    mov si, at_d_bold
    mov cx, 2
    call at_wrapsel
    pop cx
    pop si
    ret
at_cmd_italic:
    push si
    push cx
    mov si, at_d_star
    mov cx, 1
    call at_wrapsel
    pop cx
    pop si
    ret
at_cmd_code:
    push si
    push cx
    mov si, at_d_tick
    mov cx, 1
    call at_wrapsel
    pop cx
    pop si
    ret
at_cmd_strike:
    push si
    push cx
    mov si, at_d_strk
    mov cx, 2
    call at_wrapsel
    pop cx
    pop si
    ret

; -----------------------------------------------------------------------------
; at_wrapsel - WrapSelection's port: toggle a delimiter pair around the
;              selection (or an empty pair at the caret)
; in:  SI = the delimiter literal, CX = its length
; -----------------------------------------------------------------------------
at_wrapsel:
    push ax
    push bx
    push cx
    push dx
    call at_undo_push
    mov byte [at_typrun], 0
    call at_selget                  ; AX = lo, DX = hi
    mov [at_wlo], ax
    mov [at_whi], dx
    mov [at_wplen], cx
    ; outer-wrapped? markers just outside the selection
    sub ax, cx
    jc .notouter
    call at_match
    jc .notouter
    mov ax, [at_whi]
    call at_match
    jc .notouter
    ; strip both: the far one first, so offsets hold still
    mov ax, [at_whi]
    mov dx, ax
    add dx, cx
    call at_delrange
    mov ax, [at_wlo]
    sub ax, cx
    mov dx, [at_wlo]
    call at_delrange
    ; select the un-wrapped text
    mov ax, [at_wlo]
    sub ax, cx
    mov [at_sela], ax
    mov dx, [at_whi]
    sub dx, cx
    mov [at_selb], dx
    mov [at_caret], dx
    mov dx, cx
    add dx, cx
    neg dx
    jmp .apply
.notouter:
    ; inner-wrapped? markers inside the selection's own edges
    mov ax, [at_whi]
    sub ax, [at_wlo]
    mov dx, cx
    add dx, cx
    cmp ax, dx
    jb .insert
    mov ax, [at_wlo]
    call at_match
    jc .insert
    mov ax, [at_whi]
    sub ax, cx
    call at_match
    jc .insert
    mov ax, [at_whi]
    sub ax, cx
    mov dx, [at_whi]
    call at_delrange
    mov ax, [at_wlo]
    mov dx, ax
    add dx, cx
    call at_delrange
    mov ax, [at_wlo]
    mov [at_sela], ax
    mov dx, [at_whi]
    sub dx, cx
    sub dx, cx
    mov [at_selb], dx
    mov [at_caret], dx
    mov dx, cx
    add dx, cx
    neg dx
    jmp short .apply
.insert:
    ; wrap: both markers or neither - at_insn checks only ITS length, and
    ; a suffix that fits can leave no room for the prefix, stranding one
    ; marker in the buffer with the layout table never re-applied
    mov ax, [at_ge]
    sub ax, [at_gs]                 ; AX = the gap = free bytes
    mov dx, cx
    add dx, cx
    cmp ax, dx
    jb .full
    ; suffix first (offsets), then prefix
    mov ax, [at_whi]
    call at_gapto
    push cx
    call at_insn                    ; SI = literal, CX = len
    pop cx
    jc .full
    mov ax, [at_wlo]
    call at_gapto
    push cx
    call at_insn
    pop cx
    jc .full                        ; (unreachable now: 2*CX was checked)
    mov ax, [at_wlo]
    add ax, cx
    mov [at_sela], ax
    mov dx, [at_whi]
    add dx, cx
    mov [at_selb], dx
    mov [at_caret], dx
    mov dx, cx
    add dx, cx
.apply:
    mov ax, [at_wlo]
    sub ax, [at_wplen]              ; the outer strip starts before lo
    jnc .pos
    xor ax, ax
.pos:
    call at_apply_edit
.full:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Headings: replace whatever level the logical line has with the asked-for
; one; asking for the level it already has toggles it off (markdown.c's
; ToggleHeadingHidden semantics on canonical text).
; =============================================================================

at_cmd_h1:
    push cx
    mov cx, 1
    call at_heading
    pop cx
    ret
at_cmd_h2:
    push cx
    mov cx, 2
    call at_heading
    pop cx
    ret
at_cmd_h3:
    push cx
    mov cx, 3
    call at_heading
    pop cx
    ret

at_heading:
    push ax
    push bx
    push cx
    push dx
    push si
    call at_undo_push
    mov byte [at_typrun], 0
    mov [at_wplen], cx              ; the asked-for level
    ; the logical line's start
    mov ax, [at_caret]
    call at_line_of
    call at_lhome
    mov si, bx
    shl si, 1
    mov ax, [si+at_lstart]
    mov [at_wlo], ax
    ; the existing prefix's level (0..3)
    mov bx, ax
    xor cx, cx
.count:
    push cx
    call at_dlen
    mov dx, ax
    pop cx
    cmp bx, dx
    jae .counted
    call at_getb
    cmp al, '#'
    jne .counted
    inc cx
    inc bx
    cmp cx, 3
    jbe .count
.counted:
    cmp cx, 3
    ja .none                        ; four or more hashes: not a heading
    jcxz .none
    cmp bx, dx
    jae .none
    call at_getb                    ; the byte after the hashes
    cmp al, ' '
    je .have
.none:
    xor cx, cx
.have:
    mov [at_whi], cx                ; CX = the existing level
    ; delete the existing prefix (level+1 bytes), if any
    xor dx, dx                      ; DX = running delta
    jcxz .nodel
    mov ax, [at_wlo]
    mov si, [at_caret]              ; latch: at_delrange parks the caret at
                                    ; the deletion start, but mid-line it
                                    ; should ride the edit, not teleport
    push dx
    mov dx, ax
    add dx, cx
    inc dx
    call at_delrange
    sub si, cx                      ; slide back over the removed strip...
    dec si
    cmp si, ax
    jge .ckeep                      ; ...clamped to the line start when the
    mov si, ax                      ; caret sat inside the prefix itself
.ckeep:
    mov [at_caret], si
    pop dx
    sub dx, cx
    dec dx
.nodel:
    ; toggling the same level off ends here; otherwise insert the new one
    mov cx, [at_whi]
    cmp cx, [at_wplen]
    je .done
    mov ax, [at_wlo]
    call at_gapto
    mov cx, [at_wplen]
    push si
.ins:
    jcxz .sp
    mov al, '#'
    call at_ins
    jc .abort
    inc dx
    loop .ins
.sp:
    mov al, ' '
    call at_ins
    jc .abort
    inc dx
.abort:
    pop si
.done:
    ; the caret rode the edits via at_delrange/at_ins; clamp selection off
    mov ax, [at_caret]
    mov [at_sela], ax
    mov [at_selb], ax
    mov ax, [at_wlo]
    call at_apply_edit
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Link: [selection]( with the caret inside the parens - DoLink's port.
; =============================================================================
at_cmd_link:
    push ax
    push bx
    push cx
    push dx
    push si
    call at_undo_push
    mov byte [at_typrun], 0
    call at_selget
    mov [at_wlo], ax
    mov [at_whi], dx
    ; "](" + ")" after the selection, "[" before it - far end first.
    ; All four bytes or none: a tail that fits into a gap of exactly 3
    ; would strand "]()" when the "[" is refused (the at_wrapsel rule)
    mov ax, [at_ge]
    sub ax, [at_gs]
    cmp ax, 4
    jb .out
    mov ax, dx
    call at_gapto
    mov si, at_d_ltail              ; "]()"
    mov cx, 3
    call at_insn
    jc .out
    mov ax, [at_wlo]
    call at_gapto
    mov al, '['
    call at_ins
    jc .out
    ; caret between the parens: hi + 1('[') + 2("](")
    mov ax, [at_whi]
    add ax, 3
    mov [at_caret], ax
    mov [at_sela], ax
    mov [at_selb], ax
    mov ax, [at_wlo]
    mov dx, 4
    call at_apply_edit
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; None: strip every character the styled parser would hide, inside the
; selection - heading prefixes, span delimiters, link syntax. The hidden
; set is collected per visual line through at_parse itself (forced styled
; via [at_pforce]), then deleted back-to-front so the collected offsets
; stay true while they are consumed.
; =============================================================================
at_cmd_none:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call at_selget
    cmp ax, dx
    je .out
    call at_undo_push
    mov byte [at_typrun], 0
    mov [at_wlo], ax
    mov [at_whi], dx
    ; collect hidden positions into at_strip4 (idle scratch, word list)
    push ds
    pop es
    mov di, at_strip4
    mov word [at_wn], 0
    mov byte [at_pforce], 1
    mov ax, [at_wlo]
    call at_line_of                 ; BX = first line
.line:
    cmp bx, [at_nlines]
    jae .collected
    call at_lrange                  ; AX = start
    cmp ax, [at_whi]
    jae .collected
    call at_parse
    xor si, si
.chr:
    cmp si, [at_pcnt]
    jae .nextl
    cmp byte [si+at_vis], 0
    je .keep
    mov ax, [at_pstart]
    add ax, si
    cmp ax, [at_wlo]
    jb .keep
    cmp ax, [at_whi]
    jae .keep
    cmp word [at_wn], (AT_S4ST*AT_SROWS)/2 - 4
    jae .keep                       ; the list is huge; a clamp is a clamp
    stosw
    inc word [at_wn]
.keep:
    inc si
    jmp short .chr
.nextl:
    inc bx
    jmp short .line
.collected:
    mov byte [at_pforce], 0
    ; delete from the back
    mov cx, [at_wn]
    jcxz .repaint
.del:
    sub di, 2
    mov ax, [di]
    mov dx, ax
    inc dx
    call at_delrange
    loop .del
.repaint:
    mov ax, [at_wlo]
    mov [at_caret], ax
    mov [at_sela], ax
    mov [at_selb], ax
    ; scattered edits: relayout whole, repaint whole - None is rare
    call at_layout
    call at_cline_span
    mov byte [at_dirty], 1
    call at_maxtop
    mov bx, ax
    mov ax, [at_top]
    cmp ax, bx
    jbe .t
    mov [at_top], bx
.t:
    call at_caret_off
    call at_draw_text
    call at_sbar
    call at_seecaret
    call at_caret_on
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The View menu
; =============================================================================

at_cmd_viewmd:
    push ax
    xor al, al
    call at_setview
    pop ax
    ret
at_cmd_viewwr:
    push ax
    mov al, 1
    call at_setview
    pop ax
    ret

; at_setview - AL = 1 Writer / 0 Markdown (SetViewMode's port; no buffer
; sync exists to do - the mode is a rendering property)
at_setview:
    push ax
    push bx
    cmp al, [at_writer]
    je .out
    mov [at_writer], al
    mov byte [at_typrun], 0
    call at_refit
    call at_bar_draw                ; the inverted bar follows the mode
.out:
    pop bx
    pop ax
    ret

at_cmd_zin:
    push ax
    mov al, [at_zoom]
    cmp al, AT_ZMAX
    jae .out
    inc al
    mov [at_zoom], al
    call at_refit
.out:
    pop ax
    ret
at_cmd_zout:
    push ax
    mov al, [at_zoom]
    or al, al
    jz .out
    dec al
    mov [at_zoom], al
    call at_refit
.out:
    pop ax
    ret
at_cmd_zdef:
    push ax
    cmp byte [at_zoom], 0
    je .out
    mov byte [at_zoom], 0
    call at_refit
.out:
    pop ax
    ret

; at_refit - relayout + full repaint after a mode/zoom change
at_refit:
    push ax
    push bx
    call at_layout
    call at_cline_span
    call at_maxtop
    mov bx, ax
    mov ax, [at_top]
    cmp ax, bx
    jbe .t
    mov [at_top], bx
.t:
    call at_caret_off
    call at_draw_text
    call at_sbar
    call at_seecaret
    call at_caret_on
    pop bx
    pop ax
    ret

at_cmd_about:
    push ax
    push si
    mov al, AT_MD_ABOUT
    xor si, si
    call at_alert
    pop si
    pop ax
    ret

; the delimiter literals
at_d_bold:  db '**'
at_d_star:  db '*'
at_d_tick:  db '`'
at_d_strk:  db '~~'
at_d_ltail: db ']()'
