; =============================================================================
; w16edml.inc - the multi-line EDIT (SPEC.md 124.22.3), after Wine's edit.c
; in what it does. The library part's: section .w16dlg
;
; Its text is a moveable block of the program's own local heap (Notepad reads
; a file into it), NUL-terminated, CR LF line ends. There is no line table:
; a line is found by scanning from the first line shown, whose index and
; offset are kept (ME_TOP, ME_TOPO), so what is near the view is near.
; =============================================================================

section .w16dlg

ES_MULTILINE    equ 0x0004
ES_NOHIDESEL    equ 0x0100
EM_GETRECT      equ 0x0402
EM_SCROLL       equ 0x0405
EM_LINESCROLL   equ 0x0406
EM_GETLINECOUNT equ 0x040A
EM_LINEINDEX    equ 0x040B
EM_SETHANDLE    equ 0x040C
EM_GETTHUMB     equ 0x040E
EM_GETHANDLE    equ 0x040D
EM_CANUNDO      equ 0x0416
EM_UNDO         equ 0x0417
EM_FMTLINES     equ 0x0418
EM_LINEFROMCHAR equ 0x0419
EM_EMPTYUNDO    equ 0x041D
EM_GETFIRSTVIS  equ 0x041E
WM_SETREDRAW    equ 0x000B
WM_SETFONT      equ 0x0030
EN_UPDATE       equ 0x0400
EN_ERRSPACE     equ 0x0500
EN_MAXTEXT      equ 0x0501
EN_HSCROLL      equ 0x0601
EN_VSCROLL      equ 0x0602
DLGC_WANTALLKEYS equ 0x0004

; the record: EC_CARET, EC_SELA, EC_LIMIT, EC_FL and EC_DRAG as the one-line
; EDIT's, and
ME_TOP   equ W16W_CS + 4        ; the first line shown
ME_HTXT  equ W16W_CS + 10       ; the text's local handle
ME_SEG   equ W16W_CS + 12       ; ...and the heap's segment
ME_LEN   equ W16W_CS + 14       ; its length, the NUL not counted
ME_LINES equ W16W_CS + 16       ; its lines, at least one
ME_TOPO  equ W16W_EXTRA + 0     ; the first line shown's offset
ME_XOFF  equ W16W_EXTRA + 2     ; the view's left, in the text's pixels
ME_GOAL  equ W16W_EXTRA + 4     ; Up and Down's column, pixels; -1 none
ME_WIDE  equ W16W_EXTRA + 6     ; the widest line drawn
ME_UPOS  equ W16W_EXTRA + 8     ; undo: where the step was,
ME_UINS  equ W16W_EXTRA + 10    ;   what it put in (bytes),
ME_UBLK  equ W16W_EXTRA + 12    ;   what it took out (a USER heap block,
ME_UDEL  equ W16W_EXTRA + 14    ;   0 none) and how much
ME_FL2   equ W16W_EXTRA + 16    ; MEF_*
MEF_USER   equ 1                ; the text is in WIN16's USER heap
MEF_NODRAW equ 2                ; WM_SETREDRAW FALSE
MEF_UNDO   equ 4                ; a step to undo
MEF_TYPING equ 8                ; ...a run of typing, which grows
ME_LIMDEF  equ 30000            ; Windows' default limit
ME_GROW    equ 256              ; the block grows by this
ME_LM      equ 1                ; the text's left margin, pixels

; w16e_proc - the multi-line EDIT's procedure (w16d_ctlproc sends it every
; message): BX = the record -> DX:AX
w16e_proc:
    call w16e_wset
    mov cx, [w16_sm_msg]
    mov si, w16e_msgs  ; XSEC-OK: read through cs:
.find:
    mov ax, [cs:si]
    or ax, ax
    jz w16e_zero
    cmp ax, cx
    je .hit
    add si, 4
    jmp short .find
.hit:
    jmp [cs:si + 2]
w16e_zero:
    xor ax, ax
    xor dx, dx
    ret
w16e_one:
    mov ax, 1
    xor dx, dx
    ret

w16e_msgs:
    dw WM_CREATE, w16e_create
    dw WM_DESTROY, w16e_destroy
    dw WM_PAINT, w16e_paint
    dw WM_SETTEXT, w16e_settextm
    dw WM_GETTEXT, w16e_gettext
    dw WM_GETTEXTLENGTH, w16e_getlen
    dw WM_GETDLGCODE, w16e_code
    dw WM_SETFOCUS, w16e_focus
    dw WM_KILLFOCUS, w16e_unfocus
    dw WM_LBUTTONDOWN, w16e_down
    dw WM_MOUSEMOVE, w16e_move
    dw WM_LBUTTONUP, w16e_up
    dw WM_KEYDOWN, w16e_key
    dw WM_CHAR, w16e_char
    dw WM_VSCROLL, w16e_vscroll
    dw WM_HSCROLL, w16e_hscroll
    dw WM_SIZE, w16e_size
    dw WM_ENABLE, w16e_size
    dw WM_SETREDRAW, w16e_redraw
    dw WM_CUT, w16e_cut
    dw WM_COPY, w16e_copy
    dw WM_PASTE, w16e_paste
    dw WM_CLEAR, w16e_clear
    dw EM_GETSEL, w16e_getsel
    dw EM_SETSEL, w16e_setsel
    dw EM_REPLACESEL, w16e_replsel
    dw EM_GETHANDLE, w16e_gethandle
    dw EM_SETHANDLE, w16e_sethandle
    dw EM_GETLINECOUNT, w16e_linecount
    dw EM_LINEINDEX, w16e_lineindex
    dw EM_LINELENGTH, w16e_linelen
    dw EM_LINEFROMCHAR, w16e_linefrom
    dw EM_GETLINE, w16e_getline
    dw EM_LINESCROLL, w16e_linescroll
    dw EM_SCROLL, w16e_scrollmsg
    dw EM_GETRECT, w16e_getrect
    dw EM_GETMODIFY, w16d_egetmod
    dw EM_SETMODIFY, w16d_esetmod
    dw EM_LIMITTEXT, w16e_limit
    dw EM_CANUNDO, w16e_canundo
    dw EM_UNDO, w16e_undo
    dw EM_EMPTYUNDO, w16e_emptyundo
    dw EM_FMTLINES, w16e_fmtlines
    dw EM_GETFIRSTVIS, w16e_firstvis
    dw 0

; =============================================================================
; The text
; =============================================================================

; w16e_open - BX = an edit -> ES = its text's segment, DI = the text, CX =
; its length; [w16e_base] = DI. Keeps the rest.
w16e_open:
    push ax
    push si
    mov es, [bx + ME_SEG]
    mov ax, [bx + ME_HTXT]
    ICALL w16_lfind                 ; SI = the data
    mov di, si
    mov [w16e_base], di
    mov cx, [bx + ME_LEN]
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16e_grow - BX = an edit, CX = the bytes its text must hold (the NUL
; counted): the block made that large or larger, by ME_GROW steps. CF=1 the
; heap would not. Keeps every register.
; -----------------------------------------------------------------------------
w16e_grow:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov es, [bx + ME_SEG]
    mov ax, [bx + ME_HTXT]
    ICALL w16_lfind                 ; SI = the data, DI = its entry
    jc .out
    mov dx, [es:si - 2]
    and dx, 0xFFFC
    sub dx, 2                       ; DX = what it holds
    cmp cx, dx
    jbe .ok
    or di, di                       ; a fixed block cannot move
    jz .fail
    push cx
    add cx, ME_GROW - 1
    jc .exact
    and cx, ~(ME_GROW - 1)
    add sp, 2
    jmp short .new
.exact:
    pop cx
.new:
    xor al, al
    ICALL w16_lnew                  ; AX = the new data
    or ax, ax
    jz .fail
    push di
    mov di, ax                      ; what it held, across
    mov cx, dx
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    sub si, dx                      ; the old block freed, the entry moved
    xchg ax, si
    ICALL w16_lfree
    pop di
    mov [es:di], si
.ok:
    clc
    jmp short .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16e_wset - the System font's advances into [w16e_wtab] (tabs 0, which
; w16e_adv answers itself), the tab stop and the row height. Keeps every
; register.
w16e_wset:
    push ax
    ICALL w16_rowsn
    mov [w16e_rowh], ax
    mov ax, [w16_face]
    cmp ax, [w16e_wface]
    je .out
    push cx
    push si
    push es
    mov [w16e_wface], ax
    mov es, ax
    xor cx, cx
.c:
    mov al, cl
    sub al, [es:W16F_FIRST]
    jb .def
    cmp al, [es:W16F_COUNT]
    jb .ok
.def:
    mov al, W16F_DEFCH
    sub al, [es:W16F_FIRST]
.ok:
    xor ah, ah
    mov si, ax
    mov al, [es:W16F_ADV + si]
    mov si, cx
    mov [w16e_wtab + si], al
    inc cx
    cmp cx, 256
    jb .c
    mov al, [es:W16F_AVG]           ; a tab stop every eight average widths
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov [w16e_tabw], ax
    pop es
    pop si
    pop cx
.out:
    pop ax
    ret

; w16e_adv - AL = a character, DX = the pen (the text's pixels) -> AX = its
; advance: a tab's is to the next stop. Keeps the rest.
w16e_adv:
    cmp al, 9
    je .tab
    push bx
    mov bl, al
    xor bh, bh
    mov al, [w16e_wtab + bx]
    xor ah, ah
    pop bx
    ret
.tab:
    push cx
    push dx
    mov ax, dx
    xor dx, dx
    mov cx, [w16e_tabw]
    div cx
    mov ax, cx
    sub ax, dx
    pop dx
    pop cx
    ret

; =============================================================================
; Lines. ES = the text's segment and [w16e_base] the text (w16e_open).
; =============================================================================

; w16e_line - BX = an edit, AX = a line's start -> CX = its length shown (the
; line end not counted), DX = the next line's start, 0FFFFh for the last.
; Keeps the rest.
w16e_line:
    push ax
    push si
    push di
    mov di, [w16e_base]
    add di, ax
    mov si, di                      ; SI = the line
    mov cx, [bx + ME_LEN]
    sub cx, ax
    mov dx, 0xFFFF
    jcxz .last
    mov al, 10
    cld
    repne scasb
    jne .last
    mov dx, di                      ; past the LF: the next line
    sub dx, [w16e_base]
    dec di                          ; the LF, and a CR before it
    cmp di, si
    jbe .len
    cmp byte [es:di - 1], 13
    jne .len
    dec di
    jmp short .len
.last:
    mov di, [w16e_base]
    add di, [bx + ME_LEN]
.len:
    mov cx, di
    sub cx, si
    pop di
    pop si
    pop ax
    ret

; w16e_lstart - BX = an edit, AX = an offset -> AX = the start of its line.
; Keeps the rest.
w16e_lstart:
    push cx
    push di
    mov cx, ax
    jcxz .out
    mov di, [w16e_base]
    add di, ax
    dec di
    mov al, 10
    std
    repne scasb
    cld
    jne .zero
    mov ax, di
    add ax, 2
    sub ax, [w16e_base]
    jmp short .out
.zero:
    xor ax, ax
.out:
    pop di
    pop cx
    ret

; w16e_nlf - BX = an edit, AX = an offset, CX = bytes -> DX = the line feeds
; among them. Keeps the rest.
w16e_nlf:
    push ax
    push cx
    push di
    xor dx, dx
    mov di, [w16e_base]
    add di, ax
    mov al, 10
    cld
.c:
    jcxz .out
    repne scasb
    jne .out
    inc dx
    jmp short .c
.out:
    pop di
    pop cx
    pop ax
    ret

; w16e_num - BX = an edit, AX = an offset -> DX = its line's index, counted
; from the first line shown. Keeps the rest.
w16e_num:
    push ax
    push cx
    mov cx, [bx + ME_TOPO]
    cmp ax, cx
    jb .back
    xchg ax, cx
    sub cx, ax                      ; [TOPO, offset)
    call w16e_nlf
    add dx, [bx + ME_TOP]
    jmp short .out
.back:
    sub cx, ax                      ; [offset, TOPO)
    call w16e_nlf
    neg dx
    add dx, [bx + ME_TOP]
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16e_at - BX = an edit, DX = a line's index -> AX = its start. CF=1 there
; is no such line: AX = the last line's start and DX its index. Keeps the
; rest.
; -----------------------------------------------------------------------------
w16e_at:
    push cx
    push si
    mov ax, [bx + ME_TOPO]
    mov si, [bx + ME_TOP]           ; SI = the line AX starts
    cmp dx, si
    jb .back
.fwd:
    cmp si, dx
    je .yes
    push dx
    call w16e_line                  ; DX = the next
    cmp dx, 0xFFFF
    je .end
    mov ax, dx
    pop dx
    inc si
    jmp short .fwd
.end:
    pop dx
    mov dx, si
    stc
    jmp short .out
.back:
    or ax, ax
    jz .yes                         ; (a broken anchor: the first line)
    dec ax                          ; the line feed before it
    call w16e_lstart
    dec si
    cmp si, dx
    ja .back
.yes:
    clc
.out:
    pop si
    pop cx
    ret

; w16e_count - BX = an edit: [ME_LINES] counted. Keeps every register.
w16e_count:
    push ax
    push cx
    push dx
    xor ax, ax
    mov cx, [bx + ME_LEN]
    call w16e_nlf
    inc dx
    mov [bx + ME_LINES], dx
    pop dx
    pop cx
    pop ax
    ret

; w16e_xat - BX = an edit, AX = a line's start, CX = characters of it -> AX =
; the pen after them, in the text's pixels. Keeps the rest.
w16e_xat:
    push cx
    push dx
    push si
    mov si, [w16e_base]
    add si, ax
    xor dx, dx
    jcxz .out
.c:
    mov al, [es:si]
    inc si
    call w16e_adv
    add dx, ax
    loop .c
.out:
    mov ax, dx
    pop si
    pop dx
    pop cx
    ret

; w16e_hitx - BX = an edit, AX = a line's start, CX = its length shown, DX =
; a pen in the text's pixels -> AX = the offset the caret goes to there: a
; character's own when the pen is short of its middle. Keeps the rest.
w16e_hitx:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                      ; DI = the offset
    mov si, [w16e_base]
    add si, ax
    mov bx, dx                      ; BX = the target
    xor dx, dx                      ; DX = the pen
    jcxz .done
.c:
    mov al, [es:si]
    call w16e_adv
    push ax
    shr ax, 1
    add ax, dx                      ; its middle
    cmp bx, ax
    pop ax
    jl .done
    add dx, ax
    inc si
    inc di
    loop .c
.done:
    mov ax, di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; The view
; =============================================================================

; w16e_vis - BX = an edit -> CX = the lines its client shows, at least one;
; DX = the client's width. Keeps the rest.
w16e_vis:
    push ax
    ICALL w16_csize                 ; CX/DX
    push cx
    mov ax, dx
    xor dx, dx
    div word [w16e_rowh]
    mov cx, ax
    or cx, cx
    jnz .n
    inc cx
.n:
    pop dx
    pop ax
    ret

; w16e_inval - BX = an edit, AX = a first line, DX = a last line (0FFFFh: to
; the bottom): their rows of the client invalid, as far as they are shown.
; Keeps every register.
w16e_inval:
    test byte [bx + ME_FL2], MEF_NODRAW
    jnz .ret
    push ax
    push cx
    push dx
    push si
    sub ax, [bx + ME_TOP]           ; the rows, from the first shown
    jge .a
    xor ax, ax
.a:
    cmp dx, 0xFFFF
    je .bot
    sub dx, [bx + ME_TOP]
    jl .out                         ; above the view
    inc dx
    cmp dx, 0x0400                  ; (far below it: the bottom)
    jae .bot
    push ax
    mov ax, dx
    mul word [w16e_rowh]
    mov si, ax                      ; SI = its bottom, exclusive
    pop ax
    jmp short .top
.bot:
    mov si, 0x7FFF
.top:
    cmp ax, 0x0400
    jae .out
    mul word [w16e_rowh]
    mov cx, ax                      ; CX = its top
    xor ax, ax
    mov dx, 0x7FFF
    mov byte [w16_erase], 0
    ICALL w16_inval
.out:
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; w16e_invall - BX = an edit: all of its client invalid. Keeps every
; register.
w16e_invall:
    push ax
    push dx
    mov ax, [bx + ME_TOP]
    mov dx, 0xFFFF
    call w16e_inval
    pop dx
    pop ax
    ret

; w16e_sbupd - BX = an edit: its bars' ranges and positions set and, where
; they moved, their thumbs drawn (CL = 1: only if moved). Keeps every
; register.
w16e_sbupd:
    push ax
    push cx
    push dx
    push si
    test byte [bx + ME_FL2], MEF_NODRAW
    jnz .out
    call w16e_vrange                ; vertical: the first lines it can show,
    mov ax, cx                      ;   the first line shown
    mov dx, [bx + ME_TOP]
    xor cl, cl
    cmp ax, [bx + W16W_SBV + 4]
    je .v
    mov cl, 1
.v:
    mov [bx + W16W_SBV + 4], ax
    mov word [bx + W16W_SBV + 2], 0
    cmp dx, [bx + W16W_SBV]
    je .vs
    mov cl, 1
.vs:
    mov [bx + W16W_SBV], dx
    or cl, cl
    jz .h
    mov al, SB_VERT
    mov cl, 1
    call w16_sbdraw
.h:
    call w16e_hthumb                ; horizontal: 0-100
.hc:
    cmp ax, [bx + W16W_SBH]
    je .out
    mov [bx + W16W_SBH], ax
    mov al, SB_HORZ
    mov cl, 1
    call w16_sbdraw
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16e_settop - BX = an edit, DX = a line: the first shown, cut to the text;
; the lines still shown are moved (w16_scroll), not drawn again. Keeps every
; register.
; -----------------------------------------------------------------------------
w16e_settop:
    push ax
    push cx
    push dx
    push di
    push es
    call w16e_open
    or dx, dx
    jge .p
    xor dx, dx
.p:
    push cx                         ; no further than a page short of the
    push dx                         ;   end, as Wine's EM_LINESCROLL
    call w16e_vis
    mov ax, [bx + ME_LINES]
    sub ax, cx
    jge .m
    xor ax, ax
.m:
    pop dx
    pop cx
    cmp dx, ax
    jle .q
    mov dx, ax
.q:
    mov cx, [bx + ME_TOP]
    cmp dx, cx
    je .out
    call w16e_at                    ; AX = its start
    mov [bx + ME_TOPO], ax
    mov [bx + ME_TOP], dx
    test byte [bx + ME_FL2], MEF_NODRAW
    jnz .bars
    sub cx, dx                      ; the lines it moves down, cut to a
    cmp cx, 0x0400                  ;   screen's (w16_scroll invalidates
    jl .c0                          ;   all of it at that)
    mov cx, 0x0400
.c0:
    cmp cx, -0x0400
    jg .c1
    mov cx, -0x0400
.c1:
    mov ax, cx
    imul word [w16e_rowh]
    push ax
    ICALL w16_csize
    mov [w16_srr], cx
    mov [w16_srb], dx
    xor ax, ax
    mov [w16_srl], ax
    mov [w16_srt], ax
    pop dx
    mov byte [w16_erase], 0
    call w16_scroll
.bars:
    call w16e_sbupd
    mov ax, EN_VSCROLL              ; the parent follows it (Notepad's bar)
    call w16d_notify
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; w16e_setx - BX = an edit, AX = the view's left (not below 0): all drawn
; again when it moved. Keeps every register.
w16e_setx:
    push ax
    or ax, ax
    jge .p
    xor ax, ax
.p:
    cmp ax, [bx + ME_XOFF]
    je .out
    mov [bx + ME_XOFF], ax
    call w16e_invall
    call w16e_sbupd
    mov ax, EN_HSCROLL
    call w16d_notify
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16e_show - BX = an edit: the view moved so the caret is in it - a line at
; a time vertically, a quarter of the width across. Keeps every register.
; -----------------------------------------------------------------------------
w16e_show:
    push ax
    push cx
    push dx
    push si
    push es
    push di
    call w16e_open
    call w16e_vis                   ; CX = the lines shown
    mov ax, [bx + EC_CARET]
    call w16e_num                   ; DX = its line
    cmp dx, [bx + ME_TOP]
    jge .below
    call w16e_settop
    jmp short .across
.below:
    mov ax, [bx + ME_TOP]
    add ax, cx
    cmp dx, ax
    jl .across
    sub dx, cx
    inc dx
    call w16e_settop
.across:
    call w16e_open                  ; (the view moved: the text did not)
    mov ax, [bx + EC_CARET]
    call w16e_lstart
    mov cx, [bx + EC_CARET]
    sub cx, ax
    call w16e_xat                   ; AX = the caret's pen
    push ax
    call w16e_vis                   ; DX = the width
    pop ax
    sub dx, ME_LM + 1
    mov si, dx
    shr si, 1
    shr si, 1                       ; SI = a quarter of it
    cmp ax, [bx + ME_XOFF]
    jge .right
    sub ax, si
    call w16e_setx
    jmp short .out
.right:
    mov cx, [bx + ME_XOFF]
    add cx, dx
    cmp ax, cx
    jl .out
    sub ax, dx
    add ax, si
    call w16e_setx
.out:
    pop di
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16e_sel - BX = an edit -> AX = the selection's start, DX = its end.
; Keeps the rest.
w16e_sel:
    mov ax, [bx + EC_SELA]
    mov dx, [bx + EC_CARET]
    cmp ax, dx
    jbe .out
    xchg ax, dx
.out:
    ret

; -----------------------------------------------------------------------------
; w16e_moveto - BX = an edit, AX = where the caret goes, [w16e_ext] = 1 to
; carry the selection there: the lines the old selection and the new one
; cover drawn again, and the view moved to the caret. Keeps every register.
; -----------------------------------------------------------------------------
w16e_moveto:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call w16e_open
    mov si, ax                      ; SI = the new caret
    call w16e_sel
    cmp ax, si                      ; the lines between the lowest end and
    jbe .a                          ;   the highest
    mov ax, si
.a:
    cmp dx, si
    jae .d
    mov dx, si
.d:
    mov di, dx
    call w16e_num
    mov cx, dx                      ; CX = the first line
    mov ax, di
    call w16e_num                   ; DX = the last
    mov ax, cx
    mov [bx + EC_CARET], si
    cmp byte [w16e_ext], 0
    jne .ext
    mov [bx + EC_SELA], si
.ext:
    call w16e_inval
    call w16e_show
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; WM_PAINT: a line at a time, each run of it in one w16_tbox
; =============================================================================

w16e_paint:
    call w16d_popen
    jnc .go
    jmp w16e_zero
.go:
    call w16e_open
    mov ax, [w16_prc + 2]           ; the first line the update reaches
    xor dx, dx
    div word [w16e_rowh]
    push ax
    mul word [w16e_rowh]
    add ax, [w16d_t]
    mov [w16e_y], ax                ; ...its top on the screen
    pop dx
    add dx, [bx + ME_TOP]
    push dx
    call w16e_sel                   ; the selection shown, when focused
    test byte [bx + EC_FL], ECF_FOCUS
    jnz .s
    test word [bx + W16W_STYLE], ES_NOHIDESEL
    jnz .s
    xor ax, ax
    xor dx, dx
.s:
    mov [w16e_s0], ax
    mov [w16e_s1], dx
    pop dx
    call w16e_at                    ; AX = its start
    jc .gone                        ; past the last line: white
.line:
    mov cx, [w16e_y]
    cmp cx, [w16_cb]
    jg .done
    call w16e_line                  ; CX = its length, DX = the next
    push dx
    call w16e_pline
    pop dx
    mov cx, [w16e_rowh]
    add [w16e_y], cx
    cmp dx, 0xFFFF
    je .rest
    mov ax, dx
    jmp short .line
.gone:
.rest:
    mov ax, [w16d_l]                ; white below the last line
    mov bx, [w16e_y]
    mov cx, [w16d_r]
    mov dx, [w16d_b]
    mov si, COLOR_WINDOW
    cmp bx, dx
    jg .done
    call w16d_fill
.done:
    mov bx, [w16d_rec]
    call w16d_pclose
    jmp w16e_zero

; -----------------------------------------------------------------------------
; w16e_pline - BX = an edit, AX = a line's start, CX = its length shown,
; [w16e_y] its top: drawn - each run of text between tabs and the selection's
; edges in one w16_tbox, its box reaching the next run, a tab a box of its
; own, the last box to the client's right - and the caret on it. ES = the
; text's segment. Keeps BX.
; -----------------------------------------------------------------------------
w16e_pline:
    push bx
    mov [w16e_ls], ax
    add cx, ax
    mov [w16e_le], cx               ; the end shown
    mov ax, [bx + ME_XOFF]
    mov [w16e_xo], ax
    mov word [w16e_x], 0            ; the pen, the text's pixels
    mov ax, [w16d_l]
    mov [w16e_bx0], ax              ; the next box's left, on the screen
    mov si, [w16e_base]
    add si, [w16e_ls]               ; ES:SI = the character
    mov di, [w16e_ls]               ; DI = its offset
    mov ax, [w16e_y]
    mov [w16_ty], ax
    mov [w16_bxt], ax
    add ax, [w16e_rowh]
    dec ax
    mov [w16_bxb], ax
.run:
    cmp di, [w16e_le]
    jae .end
    call w16e_insel
    mov [w16e_rsel], al
    mov ax, [w16e_x]                ; the run's pen on the screen
    sub ax, [w16e_xo]
    add ax, [w16d_l]
    add ax, ME_LM
    mov [w16_tx], ax
    xor cx, cx                      ; CX = the run's characters
    cmp byte [es:si], 9
    jne .text
    mov al, 9                       ; a tab: a box to the next stop
    mov dx, [w16e_x]
    call w16e_adv
    add [w16e_x], ax
    inc si
    inc di
    jmp short .box
.text:
    mov bx, w16_txt                 ; BX = where it is copied
.tc:
    mov al, [es:si]
    cmp al, 9
    je .box
    push ax
    call w16e_insel
    cmp al, [w16e_rsel]
    pop ax
    jne .box
    mov [bx], al
    inc bx
    mov dx, [w16e_x]
    call w16e_adv
    add [w16e_x], ax
    inc si
    inc di
    inc cx
    cmp di, [w16e_le]
    jae .box
    cmp cx, W16_TXTMAX
    jb .tc
.box:
    mov [w16_tn], cx
    mov ax, [w16e_x]                ; the box: to the pen after the run, or
    sub ax, [w16e_xo]               ;   the right when it ends the line
    add ax, [w16d_l]
    add ax, ME_LM - 1
    cmp di, [w16e_le]
    jb .mid
    mov ax, [w16d_r]
.mid:
    cmp ax, [w16e_bx0]
    jl .run                         ; all of it left of the view
    mov [w16_bxr], ax
    mov dx, [w16e_bx0]
    mov [w16_bxl], dx
    inc ax
    mov [w16e_bx0], ax
    call w16e_colors
    ICALL w16_tbox
    mov ax, [w16e_bx0]
    cmp ax, [w16d_r]
    jle .run
    jmp short .caret                ; past the right: the rest is not shown
.end:
    mov ax, [w16e_bx0]              ; an empty line, or one that ended in a
    cmp ax, [w16d_r]                ;   box short of the right: white
    jg .caret
    mov bx, [w16e_y]
    mov cx, [w16d_r]
    mov dx, bx
    add dx, [w16e_rowh]
    dec dx
    mov si, COLOR_WINDOW
    call w16d_fill
    mov bx, [w16d_rec]
.caret:
    mov bx, [w16d_rec]              ; the caret: focused, no selection, on
    test byte [bx + EC_FL], ECF_FOCUS   ; this line
    jz .out
    mov ax, [bx + EC_CARET]
    cmp ax, [bx + EC_SELA]
    jne .out
    cmp ax, [w16e_ls]
    jb .out
    cmp ax, [w16e_le]
    ja .out
    sub ax, [w16e_ls]
    mov cx, ax
    mov ax, [w16e_ls]
    call w16e_xat
    sub ax, [bx + ME_XOFF]
    jl .out
    add ax, [w16d_l]
    add ax, ME_LM
    cmp ax, [w16d_r]
    jg .out
    mov cx, ax
    mov bx, [w16e_y]
    mov dx, bx
    add dx, [w16e_rowh]
    dec dx
    mov si, COLOR_WINDOWTEXT
    call w16d_fill
.out:
    mov bx, [w16d_rec]              ; the widest line drawn
    mov ax, [w16e_x]
    cmp ax, [bx + ME_WIDE]
    jbe .w
    mov [bx + ME_WIDE], ax
.w:
    pop bx
    ret

; w16e_insel - DI = an offset -> AL = 1 when it is selected and shown so.
; Keeps the rest.
w16e_insel:
    xor al, al
    cmp di, [w16e_s0]
    jb .out
    cmp di, [w16e_s1]
    jae .out
    inc al
.out:
    ret

; w16e_colors - [w16e_rsel]'s text, paper and box colours. Keeps every
; register.
w16e_colors:
    push ax
    mov ax, (COLOR_WINDOW << 8) | COLOR_WINDOWTEXT
    mov dl, COLOR_WINDOW
    cmp byte [w16e_rsel], 0
    je .c
    mov ax, (COLOR_HIGHLIGHT << 8) | COLOR_HIGHLIGHTTEXT
    mov dl, COLOR_HIGHLIGHT
.c:
    call w16d_colors
    mov al, dl
    call w16d_syscol
    mov [w16_gcol], al
    pop ax
    ret

; =============================================================================
; Changing the text
; =============================================================================

; -----------------------------------------------------------------------------
; w16e_repl - BX = an edit: CX bytes at the far [w16e_src] in place of its
; selection, as many as its limit takes; the caret after them, the step kept
; for EM_UNDO (unless [w16e_noundo]), EN_CHANGE posted, the lines it touched
; drawn again and the view moved to the caret. [w16e_typing] = 1 a typed
; character, which joins the run of typing before it. CF=1 nothing changed:
; the limit (EN_MAXTEXT) or the heap (EN_ERRSPACE). Keeps BX.
; -----------------------------------------------------------------------------
w16e_repl:
    call w16e_open
    mov cx, [w16e_cnt]
    call w16e_sel
    mov [w16e_a], ax
    mov [w16e_b], dx
    sub dx, ax                      ; DX = what goes
    mov ax, [bx + EC_LIMIT]         ; the room the limit leaves, unsigned:
    sub ax, [bx + ME_LEN]           ;   EM_LIMITTEXT's limit runs to 65535
    jae .r
    xor ax, ax                      ; (already past it: what goes, only)
.r:
    add ax, dx
    jnc .r2
    mov ax, 0xFFFF
.r2:
    cmp cx, ax
    jbe .fits
    mov cx, ax
    or cx, dx
    jnz .cut
    mov ax, EN_MAXTEXT              ; nothing would change
    call w16d_notify
    stc
    ret
.cut:
    mov cx, ax
    mov ax, EN_MAXTEXT
    call w16d_notify
.fits:
    mov [w16e_cnt], cx
    mov ax, [bx + ME_LEN]           ; the new length, and room for it
    sub ax, dx
    add ax, cx
    mov [w16e_nlen], ax
    inc ax
    push cx
    mov cx, ax
    call w16e_grow
    pop cx
    jnc .room
    mov ax, EN_ERRSPACE
    call w16d_notify
    stc
    ret
.room:
    call w16e_open                  ; (the block may have moved)
    call w16e_undorec               ; the step, before it is made
    mov ax, [w16e_a]                ; the line the change starts on, while
    call w16e_lstart                ;   the text above it is as it was
    mov [w16e_als], ax
    call w16e_num
    mov [w16e_aln], dx
    mov ax, [w16e_a]                ; the line feeds that go and that come
    mov cx, [w16e_b]
    sub cx, ax
    call w16e_nlf
    mov [w16e_dlf], dx
    ; --- the tail moved to its place: from b to a + count, its NUL too ---
    mov cx, [bx + ME_LEN]
    sub cx, [w16e_b]
    inc cx
    mov si, [w16e_base]
    add si, [w16e_b]
    mov di, [w16e_base]
    add di, [w16e_a]
    add di, [w16e_cnt]
    push ds
    push es
    pop ds
    cmp di, si
    jbe .fwd
    add si, cx                      ; to the right: last byte first
    dec si
    add di, cx
    dec di
    std
    rep movsb
    cld
    jmp short .moved
.fwd:
    cld
    rep movsb
.moved:
    pop ds
    ; --- the new bytes in, counting their line feeds ---
    mov cx, [w16e_cnt]
    mov di, [w16e_base]
    add di, [w16e_a]
    xor dx, dx
    jcxz .in
    push ds
    lds si, [w16e_src]
    cld
.cp:
    lodsb
    cmp al, 10
    jne .cp1
    inc dx
.cp1:
    stosb
    loop .cp
    pop ds
.in:
    mov [w16e_ilf], dx
    mov ax, [w16e_nlen]
    mov [bx + ME_LEN], ax
    mov ax, [bx + ME_LINES]
    add ax, dx
    sub ax, [w16e_dlf]
    mov [bx + ME_LINES], ax
    mov ax, [w16e_a]
    add ax, [w16e_cnt]
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    mov word [bx + ME_GOAL], -1
    or byte [bx + EC_FL], ECF_MODIFY
    mov ax, [w16e_a]                ; above the view: the view starts at the
    cmp ax, [bx + ME_TOPO]          ;   changed line, whose index is known
    jae .anch
    mov ax, [w16e_als]
    mov [bx + ME_TOPO], ax
    mov ax, [w16e_aln]
    mov [bx + ME_TOP], ax
    call w16e_invall
    jmp short .drawn
.anch:
    mov ax, [w16e_aln]              ; a line feed in or out: from the line
    mov dx, ax                      ;   down; else the line alone
    cmp word [w16e_dlf], 0
    jne .down
    cmp word [w16e_ilf], 0
    je .one
.down:
    mov dx, 0xFFFF
.one:
    call w16e_inval
.drawn:
    mov ax, EN_UPDATE
    call w16d_notify
    mov ax, EN_CHANGE
    call w16d_notify
    call w16e_sbupd
    call w16e_show
    clc
    ret

; w16e_put - BX = an edit, CX bytes at DS:SI: w16e_repl's. Keeps BX.
w16e_put:
    mov [w16e_src], si
    mov [w16e_src + 2], ds
    mov [w16e_cnt], cx
    jmp w16e_repl

; -----------------------------------------------------------------------------
; w16e_undorec - BX = an edit, [w16e_a]..[w16e_b] about to become [w16e_cnt]
; bytes: the step kept, unless [w16e_noundo]. A typed character right after
; a run of typing joins it. What goes is copied into the USER heap; with no
; room for it there is no step. ES = the text's segment (w16e_open). Keeps
; BX.
; -----------------------------------------------------------------------------
w16e_undorec:
    cmp byte [w16e_noundo], 0
    jne .ret
    mov ax, [w16e_b]
    sub ax, [w16e_a]
    jnz .new
    cmp byte [w16e_typing], 0
    je .new
    test byte [bx + ME_FL2], MEF_TYPING
    jz .new
    mov ax, [bx + ME_UPOS]
    add ax, [bx + ME_UINS]
    cmp ax, [w16e_a]
    jne .new
    mov ax, [w16e_cnt]              ; the run grows
    add [bx + ME_UINS], ax
.ret:
    ret
.new:
    call w16e_undofree
    and byte [bx + ME_FL2], ~(MEF_UNDO | MEF_TYPING)
    mov ax, [w16e_a]
    mov [bx + ME_UPOS], ax
    mov ax, [w16e_cnt]
    mov [bx + ME_UINS], ax
    mov cx, [w16e_b]
    sub cx, [w16e_a]
    mov [bx + ME_UDEL], cx
    jcxz .kept
    push es                         ; the text's segment
    call w16_mheap                  ; ES = the USER heap
    jc .none
    xor al, al
    ICALL w16_lnew                  ; AX = a block of CX bytes
    or ax, ax
    jz .none
    mov [bx + ME_UBLK], ax
    mov di, ax                      ; ES:DI = it
    pop ax                          ; AX = the text's segment
    push ds
    push ax
    mov si, [w16e_base]
    add si, [w16e_a]
    mov ds, ax                      ; DS:SI = what goes
    cld
    rep movsb
    pop es
    pop ds
    jmp short .kept
.none:
    pop es
    ret
.kept:
    or byte [bx + ME_FL2], MEF_UNDO
    cmp byte [w16e_typing], 0
    je .ret2
    or byte [bx + ME_FL2], MEF_TYPING
.ret2:
    ret

; w16e_undofree - BX = an edit: the step's block given back. Keeps every
; register.
w16e_undofree:
    push ax
    push es
    xor ax, ax
    xchg ax, [bx + ME_UBLK]
    or ax, ax
    jz .out
    call w16_mheap
    jc .out
    ICALL w16_lfree
.out:
    pop es
    pop ax
    ret

; =============================================================================
; The messages
; =============================================================================

; WM_CREATE: the block, in the program's local heap (WIN16's USER heap when
; it has none), and the text it was made with in it
w16e_create:
    mov word [bx + EC_LIMIT], ME_LIMDEF
    mov word [bx + ME_GOAL], -1
    mov es, [w16_dgroup]
    push bx
    ICALL w16_lheap
    pop bx
    jnc .local
    call w16_mheap
    jc .fail
    or byte [bx + ME_FL2], MEF_USER
.local:
    mov [bx + ME_SEG], es
    push es
    ICALL w16_textptr               ; ES:SI, CX = the text it was made with
    mov ax, es
    pop es
    push ax
    push si
    push cx
    inc cx
    add cx, ME_GROW - 1
    and cx, ~(ME_GROW - 1)
    xor al, al
    ICALL w16_lnew                  ; AX = the data
    or ax, ax
    jz .nomem
    mov di, ax
    mov cx, 2                       ; ...and its moveable entry, the handle
    xor al, al
    ICALL w16_lnew
    or ax, ax
    jz .noent
    mov si, ax
    mov [es:si], di
    or byte [es:si - 2], LB_ENTRY
    mov [bx + ME_HTXT], si
    pop cx
    pop si
    pop ax
    mov [bx + ME_LEN], cx
    push ds
    mov ds, ax
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
    call w16e_open
    call w16e_count
    xor ax, ax
    mov [bx + ME_TOP], ax
    mov [bx + ME_TOPO], ax
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    mov ax, [bx + ME_LINES]         ; its bars' ranges (SPEC.md 124.22.3)
    dec ax
    mov [bx + W16W_SBV + 4], ax
    jmp w16e_zero
.noent:
    mov ax, di
    ICALL w16_lfree
.nomem:
    add sp, 6
.fail:
    mov ax, -1
    mov dx, ax
    ret

; WM_DESTROY: the block given back, and the undo step's
w16e_destroy:
    call w16e_undofree
    mov es, [bx + ME_SEG]
    xor ax, ax
    xchg ax, [bx + ME_HTXT]
    or ax, ax
    jz .z
    ICALL w16_lfind                 ; SI = the data, DI = the entry
    jc .z
    mov ax, si
    ICALL w16_lfree
    or di, di
    jz .z
    mov ax, di
    ICALL w16_lfree
.z:
    jmp w16e_zero

; WM_SETTEXT: the string in place of the whole text, the caret at its start
w16e_settextm:
    les di, [w16_sm_lp]
    xor cx, cx
    mov ax, es
    or ax, di
    jz .len
    dec cx
    xor al, al
    cld
    repne scasb
    not cx
    dec cx                          ; CX = the string's length
.len:
    mov ax, [w16_sm_lp]
    mov [w16e_src], ax
    mov ax, [w16_sm_lp + 2]
    mov [w16e_src + 2], ax
    mov [w16e_cnt], cx
    mov word [bx + EC_SELA], 0
    mov ax, [bx + ME_LEN]
    mov [bx + EC_CARET], ax
    mov byte [w16e_noundo], 1
    mov byte [w16e_typing], 0
    call w16e_repl
    mov byte [w16e_noundo], 0
    call w16e_reset
    jmp w16e_one

; w16e_reset - BX = an edit whose text is new: no step to undo, not
; modified, the caret and the view at the start, all drawn again. Keeps
; every register.
w16e_reset:
    push ax
    call w16e_undofree
    and byte [bx + ME_FL2], ~(MEF_UNDO | MEF_TYPING)
    and byte [bx + EC_FL], ~ECF_MODIFY
    xor ax, ax
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    mov [bx + ME_TOP], ax
    mov [bx + ME_TOPO], ax
    mov [bx + ME_XOFF], ax
    mov [bx + ME_WIDE], ax
    mov word [bx + ME_GOAL], -1
    call w16e_invall
    call w16e_sbupd
    pop ax
    ret

; WM_GETTEXT: wParam bytes at lParam, the NUL among them
w16e_gettext:
    call w16e_open                  ; ES:DI, CX
    mov ax, [w16_sm_wp]
    or ax, ax
    jz .z
    dec ax
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    mov si, di
    push ds
    push es
    les di, [w16_sm_lp]
    pop ds
    mov ax, cx
    cld
    rep movsb
    mov byte [es:di], 0
    pop ds
    xor dx, dx
    ret
.z:
    jmp w16e_zero

w16e_getlen:
    mov ax, [bx + ME_LEN]
    xor dx, dx
    ret

w16e_code:
    mov ax, DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS | DLGC_WANTALLKEYS
    xor dx, dx
    ret

; w16e_selinval - BX = an edit: the lines its selection covers drawn again.
; Keeps every register.
w16e_selinval:
    push ax
    push cx
    push dx
    push di
    push es
    call w16e_open
    call w16e_sel
    push dx
    call w16e_num
    mov cx, dx
    pop ax
    call w16e_num
    mov ax, cx
    call w16e_inval
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

w16e_focus:
    or byte [bx + EC_FL], ECF_FOCUS
    call w16e_selinval
    mov ax, EN_SETFOCUS
    call w16d_notify
    jmp w16e_zero
w16e_unfocus:
    and byte [bx + EC_FL], ~ECF_FOCUS
    mov byte [bx + EC_DRAG], 0
    call w16e_selinval
    mov ax, EN_KILLFOCUS
    call w16d_notify
    jmp w16e_zero

; --- the mouse -----------------------------------------------------------------

; w16e_hit - BX = an edit, [w16_sm_lp] a client point -> AX = the offset
; the caret goes to there. Keeps the rest.
w16e_hit:
    push cx
    push dx
    push di
    push es
    call w16e_open
    mov ax, [w16_sm_lp + 2]
    mov dx, [bx + ME_TOP]
    or ax, ax
    jl .above
    xor dx, dx
    div word [w16e_rowh]
    add ax, [bx + ME_TOP]
    mov dx, ax
    jmp short .line
.above:
    dec dx                          ; above the view: the line before it
    jge .line
    xor dx, dx
.line:
    call w16e_at                    ; AX = its start (or the last line's)
    call w16e_line                  ; CX
    mov dx, [w16_sm_lp]
    sub dx, ME_LM
    add dx, [bx + ME_XOFF]
    jge .x
    xor dx, dx
.x:
    call w16e_hitx
    pop es
    pop di
    pop dx
    pop cx
    ret

w16e_down:
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_setfocus
    pop bx
    ICALL w16_rec2hwnd
    mov [w16_capture], ax
    mov byte [bx + EC_DRAG], 1
    call w16e_hit
    mov byte [w16e_ext], 0
    test byte [w16_sm_wp], 4        ; MK_SHIFT
    jz .m
    mov byte [w16e_ext], 1
.m:
    mov word [bx + ME_GOAL], -1
    call w16e_moveto
    jmp w16e_zero

w16e_move:
    cmp byte [bx + EC_DRAG], 0
    je .z
    call w16e_hit
    cmp ax, [bx + EC_CARET]
    je .z
    mov byte [w16e_ext], 1
    call w16e_moveto
.z:
    jmp w16e_zero

w16e_up:
    cmp byte [bx + EC_DRAG], 0
    je .z
    mov byte [bx + EC_DRAG], 0
    ICALL w16_rec2hwnd
    cmp [w16_capture], ax
    jne .z
    mov word [w16_capture], 0
.z:
    jmp w16e_zero

; --- the keys ------------------------------------------------------------------

; WM_KEYDOWN: the caret's keys, Shift with them to select; Delete, and the
; clipboard's Shift+Delete, Ctrl+Insert and Shift+Insert
w16e_key:
    call w16e_open
    mov byte [w16e_ext], 0
    call w16d_eshift
    jnc .ns
    mov byte [w16e_ext], 1
.ns:
    mov ax, [bx + EC_CARET]
    mov dx, [w16_sm_wp]
    cmp dx, VK_UP
    je .updown
    cmp dx, VK_DOWN
    je .updown
    cmp dx, VK_PRIOR
    je .updown
    cmp dx, VK_NEXT
    je .updown
    mov word [bx + ME_GOAL], -1
    cmp dx, VK_LEFT
    je .left
    cmp dx, VK_RIGHT
    je .right
    cmp dx, VK_HOME
    je .home
    cmp dx, VK_END
    je .end
    cmp dx, VK_DELETE
    je .del
    cmp dx, VK_INSERT
    je .ins
    jmp w16e_zero
.ins:
    call w16d_eshift                ; Shift+Ins pastes, Ctrl+Ins copies
    jc w16e_paste
    call w16d_ectrl
    jc w16e_copy
    jmp w16e_zero
.del:
    call w16d_eshift                ; Shift+Del cuts
    jc w16e_cut
    call w16e_sel
    cmp ax, dx
    jne w16e_clear
    call w16e_next                  ; DX = past the character after it
    cmp dx, ax
    je .z
    mov [bx + EC_SELA], dx
    jmp w16e_clear
.z:
    jmp w16e_zero
.left:
    cmp byte [w16e_ext], 0          ; a selection, no Shift: its left end
    jne .l1
    call w16e_sel
    cmp ax, dx
    jne .go
.l1:
    mov ax, [bx + EC_CARET]
    call w16e_prev
    jmp short .go
.right:
    cmp byte [w16e_ext], 0
    jne .r1
    call w16e_sel
    cmp ax, dx
    mov ax, dx
    jne .go
.r1:
    mov ax, [bx + EC_CARET]
    call w16e_next
    mov ax, dx
    jmp short .go
.home:
    xor dx, dx
    call w16d_ectrl
    jc .to
    call w16e_lstart
    jmp short .go
.end:
    mov dx, [bx + ME_LEN]
    call w16d_ectrl
    jc .to
    call w16e_lstart
    push ax
    call w16e_line
    pop ax
    add ax, cx
    jmp short .go
.to:
    mov ax, dx
.go:
    call w16e_moveto
    jmp w16e_zero
.updown:
    push dx                         ; the goal column: kept from the last
    mov cx, [bx + ME_GOAL]          ;   Up or Down, else the caret's
    cmp cx, -1
    jne .goal
    call w16e_lstart
    mov cx, [bx + EC_CARET]
    sub cx, ax
    call w16e_xat
    mov [bx + ME_GOAL], ax
.goal:
    call w16e_vis                   ; CX = the lines shown
    dec cx
    jnz .pg
    inc cx
.pg:
    mov ax, [bx + EC_CARET]
    call w16e_num                   ; DX = its line
    pop ax                          ; AX = the key
    push cx
    cmp ax, VK_UP
    je .up1
    cmp ax, VK_DOWN
    je .dn1
    cmp ax, VK_PRIOR
    je .pu
    add dx, cx                      ; Page Down: the view with it
    pop cx
    push dx
    mov dx, [bx + ME_TOP]
    add dx, cx
    call w16e_settop
    pop dx
    jmp short .line
.pu:
    sub dx, cx
    pop cx
    push dx
    mov dx, [bx + ME_TOP]
    sub dx, cx
    call w16e_settop
    pop dx
    jmp short .line
.up1:
    pop cx
    dec dx
    jmp short .line
.dn1:
    pop cx
    inc dx
.line:
    or dx, dx
    jge .l0
    xor dx, dx
.l0:
    call w16e_open
    call w16e_at                    ; AX = its start (or the last line's)
    call w16e_line                  ; CX
    push word [bx + ME_GOAL]
    mov dx, [bx + ME_GOAL]
    call w16e_hitx
    call w16e_moveto
    pop word [bx + ME_GOAL]
    jmp w16e_zero

; w16e_prev - BX = an edit, AX = an offset -> AX = the one before it, a CR
; LF stepped over whole. Keeps the rest.
w16e_prev:
    or ax, ax
    jz .out
    dec ax
    push si
    mov si, [w16e_base]
    add si, ax
    cmp byte [es:si], 10
    jne .o
    or ax, ax
    jz .o
    cmp byte [es:si - 1], 13
    jne .o
    dec ax
.o:
    pop si
.out:
    ret

; w16e_next - BX = an edit, AX = an offset -> DX = the one after it, a CR LF
; stepped over whole. Keeps the rest.
w16e_next:
    mov dx, ax
    cmp dx, [bx + ME_LEN]
    jae .out
    push si
    mov si, [w16e_base]
    add si, dx
    inc dx
    cmp byte [es:si], 13
    jne .o
    cmp byte [es:si + 1], 10
    jne .o
    inc dx
.o:
    pop si
.out:
    ret

; WM_CHAR: a character in place of the selection; Enter a CR LF; Backspace
; the selection or the character before the caret
w16e_char:
    test word [bx + W16W_STYLE], 0x0800 ; ES_READONLY
    jnz .z
    mov al, [w16_sm_wp]
    cmp al, VK_BACK
    je .back
    cmp al, VK_RETURN
    je .cr
    cmp al, VK_TAB
    je .put
    cmp al, ' '
    jb .z
.put:
    mov [w16e_ch], al
    mov si, w16e_ch
    mov cx, 1
    mov byte [w16e_typing], 1
    call w16e_put
    mov byte [w16e_typing], 0
.z:
    jmp w16e_zero
.cr:
    mov word [w16e_ch], 0x0A0D
    mov si, w16e_ch
    mov cx, 2
    mov byte [w16e_typing], 0
    call w16e_put
    jmp w16e_zero
.back:
    call w16e_open
    call w16e_sel
    cmp ax, dx
    jne w16e_clear
    call w16e_prev
    cmp ax, dx
    je .z
    mov [bx + EC_SELA], ax
    ; (fall through)
; WM_CLEAR: the selection deleted
w16e_clear:
    call w16e_sel
    cmp ax, dx
    je .z
    xor cx, cx
    mov byte [w16e_typing], 0
    call w16e_put
.z:
    jmp w16e_zero

; --- scrolling -----------------------------------------------------------------

; w16e_vrange - BX = an edit -> CX = the first lines it can show: the lines
; less a page, not below 0. Keeps the rest.
w16e_vrange:
    push ax
    push dx
    call w16e_vis
    mov ax, [bx + ME_LINES]
    sub ax, cx
    jge .o
    xor ax, ax
.o:
    mov cx, ax
    pop dx
    pop ax
    ret

; w16e_hrange - BX = an edit -> CX = the pixels its view's left can move
; through: the widest line drawn less the width, not below 0. Keeps the
; rest.
w16e_hrange:
    push ax
    push dx
    call w16e_vis                   ; DX = the width
    mov cx, [bx + ME_WIDE]
    sub cx, dx
    jge .o
    xor cx, cx
.o:
    pop dx
    pop ax
    ret

; w16e_hthumb - BX = an edit -> AX = its view's left as 0-100 of
; w16e_hrange's. Keeps the rest.
w16e_hthumb:
    push cx
    push dx
    call w16e_hrange
    xor ax, ax
    jcxz .o
    mov ax, [bx + ME_XOFF]
    cmp ax, cx
    jae .full
    mov dx, 100
    mul dx
    div cx
    jmp short .o
.full:
    mov ax, 100
.o:
    pop dx
    pop cx
    ret

; WM_VSCROLL (and EM_SCROLL's): a line, a page, the thumb's line
w16e_vscroll:
    mov ax, [w16_sm_wp]
    cmp ax, EM_GETTHUMB             ; Windows 3.x's: the thumb, 0-100
    jne .nt
    call w16e_vrange                ; CX = the lines it scrolls through
    jcxz .z
    mov ax, [bx + ME_TOP]
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI
    jnz .own
    mov dx, 100
    mul dx
    div cx
.own:
    xor dx, dx
    ret
.nt:
    call w16e_vis                   ; CX = the lines shown
    dec cx
    jnz .pg
    inc cx
.pg:
    mov dx, [bx + ME_TOP]
    cmp ax, SB_LINEUP
    je .lu
    cmp ax, SB_LINEDOWN
    je .ld
    cmp ax, SB_PAGEUP
    je .pu
    cmp ax, SB_PAGEDOWN
    je .pd
    cmp ax, SB_THUMBTRACK
    je .th
    cmp ax, SB_THUMBPOSITION
    je .th
    cmp ax, SB_TOP
    je .top
    cmp ax, SB_BOTTOM
    jne .z
    mov dx, [bx + ME_LINES]
    jmp short .set
.top:
    xor dx, dx
    jmp short .set
.th:
    mov dx, [w16_sm_lp]             ; its own bar's line, or a share of 100
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI
    jnz .set
    call w16e_vrange
    mov ax, dx
    mul cx
    mov cx, 100
    div cx
    mov dx, ax
    jmp short .set
.lu:
    dec dx
    jmp short .set
.ld:
    inc dx
    jmp short .set
.pu:
    sub dx, cx
    jmp short .set
.pd:
    add dx, cx
.set:
    call w16e_settop
.z:
    jmp w16e_zero

; EM_SCROLL: wParam an SB_* code, as WM_VSCROLL's
w16e_scrollmsg:
    jmp w16e_vscroll

; WM_HSCROLL: eight average widths, half the width, the thumb's share
w16e_hscroll:
    cmp word [w16_sm_wp], EM_GETTHUMB
    jne .nt
    call w16e_hthumb
    xor dx, dx
    ret
.nt:
    mov ax, [bx + ME_XOFF]
    mov dx, [w16_sm_wp]
    mov cx, [w16e_tabw]
    cmp dx, SB_LINEUP
    je .l
    cmp dx, SB_LINEDOWN
    je .r
    push dx
    call w16e_vis                   ; DX = the width
    mov cx, dx
    shr cx, 1
    pop dx
    cmp dx, SB_PAGEUP
    je .l
    cmp dx, SB_PAGEDOWN
    je .r
    cmp dx, SB_THUMBTRACK
    je .th
    cmp dx, SB_THUMBPOSITION
    jne .z
.th:
    call w16e_hrange                ; the share of what it scrolls through
    mov ax, cx
    mul word [w16_sm_lp]
    mov cx, 100
    div cx
    jmp short .set
.l:
    sub ax, cx
    jmp short .set
.r:
    add ax, cx
.set:
    call w16e_setx
.z:
    jmp w16e_zero

w16e_size:
    call w16e_invall
    call w16e_sbupd
    jmp w16e_zero

; WM_SETREDRAW: wParam 0 draws nothing until a non-zero one, which draws all
w16e_redraw:
    cmp word [w16_sm_wp], 0
    jne .on
    or byte [bx + ME_FL2], MEF_NODRAW
    jmp w16e_zero
.on:
    and byte [bx + ME_FL2], ~MEF_NODRAW
    call w16e_invall
    or byte [bx + W16W_F2], W16W2_NCDIRTY
    call w16e_sbupd
    jmp w16e_zero

; --- the clipboard -------------------------------------------------------------

; WM_COPY: the selection onto os8088's clipboard, through a block of its own
; (w16_clput changes what it is handed)
w16e_copy:
    call w16e_open
    call w16e_sel
    mov cx, dx
    sub cx, ax
    jcxz .z
    push ax
    mov [w16e_cnt], cx
    mov ax, cx
    inc ax
    xor dx, dx
    xor cl, cl
    ICALL w16_gnew                  ; AX = the handle, DX = the segment
    pop si
    or ax, ax
    jz .z
    push ax
    call w16e_open                  ; ES:DI = the text
    mov cx, [w16e_cnt]
    add si, di
    push ds
    push es
    pop ds                          ; DS:SI = the selection
    mov es, dx
    xor di, di
    push cx
    cld
    rep movsb
    pop cx
    pop ds
    mov byte [es:di], 0
    xor si, si
    call w16_clput
    pop ax
    ICALL w16_gfreeh
.z:
    jmp w16e_zero

; WM_CUT: copied, then cleared
w16e_cut:
    call w16e_copy
    jmp w16e_clear

; WM_PASTE: the clipboard's text, CR LF line ends, in place of the selection
w16e_paste:
    call w16_clgetb                 ; AX = a block the clipboard owns
    or ax, ax
    jz .z
    push es
    ICALL w16_gfind                 ; ES = its header
    mov ax, es
    pop es
    jc .z
    inc ax
    mov [w16e_src + 2], ax
    mov word [w16e_src], 0
    push es
    mov es, ax
    xor di, di
    mov cx, 0xFFFF
    xor al, al
    cld
    repne scasb
    not cx
    dec cx
    pop es
    mov [w16e_cnt], cx
    mov byte [w16e_typing], 0
    call w16e_repl
.z:
    jmp w16e_zero

; --- the EM_ messages -------------------------------------------------------

w16e_getsel:
    call w16e_sel
    ret

; EM_SETSEL: lParam's low word the start, its high word the end; an end
; past the text is its end
w16e_setsel:
    call w16e_selinval
    mov ax, [w16_sm_lp]
    mov dx, [w16_sm_lp + 2]
    mov cx, [bx + ME_LEN]
    cmp ax, cx
    jbe .a
    mov ax, cx
.a:
    cmp dx, cx
    jbe .d
    mov dx, cx
.d:
    mov [bx + EC_SELA], ax
    mov [bx + EC_CARET], ax
    mov word [bx + ME_GOAL], -1
    mov ax, dx
    mov byte [w16e_ext], 1
    call w16e_moveto
    jmp w16e_zero

; EM_REPLACESEL: lParam's string in place of the selection
w16e_replsel:
    les di, [w16_sm_lp]
    xor cx, cx
    mov ax, es
    or ax, di
    jz .z
    dec cx
    xor al, al
    cld
    repne scasb
    not cx
    dec cx
    mov ax, [w16_sm_lp]
    mov [w16e_src], ax
    mov ax, [w16_sm_lp + 2]
    mov [w16e_src + 2], ax
    mov [w16e_cnt], cx
    mov byte [w16e_typing], 0
    call w16e_repl
.z:
    jmp w16e_zero

w16e_gethandle:
    xor ax, ax
    test byte [bx + ME_FL2], MEF_USER
    jnz .out
    mov ax, [bx + ME_HTXT]
.out:
    xor dx, dx
    ret

; EM_SETHANDLE: wParam a local handle of the program's, the text from now on
; (the old one is the program's to free, as Windows')
w16e_sethandle:
    test byte [bx + ME_FL2], MEF_USER
    jnz .z
    mov ax, [w16_sm_wp]
    mov es, [bx + ME_SEG]
    ICALL w16_lfind                 ; SI = its data
    jc .z
    mov [bx + ME_HTXT], ax
    mov cx, [es:si - 2]             ; its length: to the NUL, inside it
    and cx, 0xFFFC
    sub cx, 2
    mov di, si
    xor al, al
    cld
    repne scasb
    jne .full
    dec di
.full:
    sub di, si
    mov [bx + ME_LEN], di
    call w16e_open
    call w16e_count
    call w16e_reset
.z:
    jmp w16e_zero

w16e_linecount:
    mov ax, [bx + ME_LINES]
    xor dx, dx
    ret

; w16e_wpchar - BX = an edit -> AX = wParam as an offset: -1 the caret's,
; else cut to the text. ES = its segment (w16e_open). Keeps the rest.
w16e_wpchar:
    mov ax, [w16_sm_wp]
    cmp ax, -1
    jne .c
    mov ax, [bx + EC_CARET]
.c:
    cmp ax, [bx + ME_LEN]
    jbe .out
    mov ax, [bx + ME_LEN]
.out:
    ret

; EM_LINEINDEX: wParam a line (-1 the caret's) -> its start, or -1
w16e_lineindex:
    call w16e_open
    mov dx, [w16_sm_wp]
    cmp dx, -1
    jne .l
    mov ax, [bx + EC_CARET]
    call w16e_num
.l:
    call w16e_at
    jnc .out
    mov ax, -1
.out:
    xor dx, dx
    ret

; EM_LINELENGTH: wParam an offset (-1 the caret's) -> its line's length
w16e_linelen:
    call w16e_open
    call w16e_wpchar
    call w16e_lstart
    call w16e_line
    mov ax, cx
    xor dx, dx
    ret

; EM_LINEFROMCHAR: wParam an offset (-1 the selection's start) -> its line
w16e_linefrom:
    call w16e_open
    call w16e_wpchar
    cmp word [w16_sm_wp], -1
    jne .n
    call w16e_sel
.n:
    call w16e_num
    mov ax, dx
    xor dx, dx
    ret

; EM_GETLINE: wParam a line, lParam a buffer whose first word is its room ->
; the bytes copied, no NUL
w16e_getline:
    call w16e_open
    mov dx, [w16_sm_wp]
    call w16e_at
    jc .z
    call w16e_line                  ; CX
    mov si, [w16e_base]
    add si, ax
    push ds
    push es
    les di, [w16_sm_lp]
    mov ax, [es:di]
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    pop ds
    mov ax, cx
    cld
    rep movsb
    pop ds
    xor dx, dx
    ret
.z:
    jmp w16e_zero

; EM_LINESCROLL: lParam's low word lines down, its high word characters
; across
w16e_linescroll:
    mov dx, [bx + ME_TOP]
    add dx, [w16_sm_lp]
    call w16e_settop
    mov al, [w16e_tabw]
    mov cl, 3
    shr al, cl                      ; an average width
    cbw
    imul word [w16_sm_lp + 2]
    add ax, [bx + ME_XOFF]
    call w16e_setx
    jmp w16e_one

; EM_GETRECT: lParam a RECT: the client, less the margin
w16e_getrect:
    ICALL w16_csize
    les di, [w16_sm_lp]
    mov word [es:di], ME_LM
    mov word [es:di + 2], 0
    mov [es:di + 4], cx
    mov [es:di + 6], dx
    jmp w16e_zero

w16e_limit:
    mov ax, [w16_sm_wp]
    or ax, ax
    jnz .set
    mov ax, 0xFFFE
.set:
    mov [bx + EC_LIMIT], ax
    jmp w16e_zero

w16e_canundo:
    test byte [bx + ME_FL2], MEF_UNDO
    jz .z
    jmp w16e_one
.z:
    jmp w16e_zero

; EM_UNDO: the step put back - what it put in replaced by what it took out
; - and that is the step now, so a second EM_UNDO undoes the first
w16e_undo:
    test byte [bx + ME_FL2], MEF_UNDO
    jz .z
    and byte [bx + ME_FL2], ~(MEF_UNDO | MEF_TYPING)
    xor ax, ax
    xchg ax, [bx + ME_UBLK]
    mov [w16e_ublk], ax
    mov [w16e_src], ax
    push es
    call w16_mheap
    mov [w16e_src + 2], es
    pop es
    mov ax, [bx + ME_UPOS]
    mov [bx + EC_SELA], ax
    add ax, [bx + ME_UINS]
    cmp ax, [bx + ME_LEN]
    jbe .c
    mov ax, [bx + ME_LEN]
.c:
    mov [bx + EC_CARET], ax
    mov ax, [bx + ME_UDEL]
    mov [w16e_cnt], ax
    mov byte [w16e_typing], 0
    call w16e_repl
    mov ax, [w16e_ublk]
    or ax, ax
    jz .one
    push es
    call w16_mheap
    ICALL w16_lfree
    pop es
.one:
    jmp w16e_one
.z:
    jmp w16e_zero

w16e_emptyundo:
    call w16e_undofree
    and byte [bx + ME_FL2], ~(MEF_UNDO | MEF_TYPING)
    jmp w16e_zero

w16e_fmtlines:
    mov ax, [w16_sm_wp]
    xor dx, dx
    ret

w16e_firstvis:
    mov ax, [bx + ME_TOP]
    xor dx, dx
    ret

section .text
