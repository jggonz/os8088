; =============================================================================
; w16combo.inc - the COMBOBOX control and owner-drawn items (SPEC.md
; 124.22.14), after Wine's combo.c in what it does and Windows 3.0's in how
; it looks. The library part's: section .w16dlg
;
; A combo box is a field and a LISTBOX, as Windows' was: the list is a real
; W16Q_LIST window holding the items, so every LB_ path is the list box's
; own, and CB_ messages are passed on to it. A drop-down's list is a child of
; the combo box's top-level window, hidden until it drops; a CBS_SIMPLE one's
; is the combo box's own child, always shown. A CBS_DROPDOWN and a CBS_SIMPLE
; one have an EDIT for a field, the combo box's child.
;
; OWNER-DRAWN ITEMS are drawn by the control's parent, to which Windows SENT
; WM_DRAWITEM from inside the control's paint. WIN16's controls answer at
; once and cannot wait on the program, so a paint QUEUES what is owed
; (w16_odput) and the message is sent where the program next asks for one -
; GetMessage, PeekMessage and the modal loop (w16_odgo) - each a real send
; with a DC on the control, before the next message is handed over.
; =============================================================================

section .w16dlg

CBS_TYPE        equ 0x0003      ; a combo box's style: its kind,
CBS_SIMPLE      equ 0x0001
CBS_DROPDOWN    equ 0x0002
CBS_DROPDOWNLIST equ 0x0003
CBS_OWNERDRAWFIXED equ 0x0010   ; ...its parent draws the items,
CBS_AUTOHSCROLL equ 0x0040
CBS_SORT        equ 0x0100      ; ...in order,
CBS_HASSTRINGS  equ 0x0200      ; ...with strings all the same

CB_GETEDITSEL   equ 0x0400
CB_LIMITTEXT    equ 0x0401
CB_SETEDITSEL   equ 0x0402
CB_ADDSTRING    equ 0x0403
CB_DELETESTRING equ 0x0404
CB_DIR          equ 0x0405
CB_GETCOUNT     equ 0x0406
CB_GETCURSEL    equ 0x0407
CB_GETLBTEXT    equ 0x0408
CB_GETLBTEXTLEN equ 0x0409
CB_INSERTSTRING equ 0x040A
CB_RESETCONTENT equ 0x040B
CB_FINDSTRING   equ 0x040C
CB_SELECTSTRING equ 0x040D
CB_SETCURSEL    equ 0x040E
CB_SHOWDROPDOWN equ 0x040F
CB_GETITEMDATA  equ 0x0410
CB_SETITEMDATA  equ 0x0411
CB_GETDROPPEDCONTROLRECT equ 0x0412
CB_SETITEMHEIGHT equ 0x0413
CB_GETITEMHEIGHT equ 0x0414
CB_SETEXTENDEDUI equ 0x0415
CB_GETEXTENDEDUI equ 0x0416
CB_GETDROPPEDSTATE equ 0x0417
CB_FINDSTRINGEXACT equ 0x0418

CBN_SELCHANGE   equ 1
CBN_SETFOCUS    equ 3
CBN_KILLFOCUS   equ 4
CBN_EDITCHANGE  equ 5
CBN_EDITUPDATE  equ 6
CBN_DROPDOWN    equ 7
CBN_CLOSEUP     equ 8
CBN_SELENDOK    equ 9
CBN_SELENDCANCEL equ 10

EN_SETFOCUS_    equ 0x0100      ; what its edit tells it
EN_KILLFOCUS_   equ 0x0200
EN_CHANGE_      equ 0x0300

LB_DIR_         equ 0x040E

WM_DRAWITEM     equ 0x002B
WM_MEASUREITEM  equ 0x002C
ODT_LISTBOX     equ 2
ODT_COMBOBOX    equ 3
ODA_DRAWENTIRE  equ 1
ODA_SELECT      equ 2
ODS_SELECTED    equ 0x0001
ODS_DISABLED    equ 0x0004
ODS_FOCUS       equ 0x0010
DIS_SIZE        equ 26              ; DRAWITEMSTRUCT
MIS_SIZE        equ 14              ; MEASUREITEMSTRUCT

; --- a combo box's own state, in its W16W_CS ----------------------------------
CC_LIST     equ W16W_CS + 0     ; its list's HWND
CC_EDIT     equ W16W_CS + 2     ; ...its edit's, 0 none (CBS_DROPDOWNLIST)
CC_FL       equ W16W_CS + 4     ; ...CCF_*
CC_FH       equ W16W_CS + 6     ; ...the field's height inside its frame
CC_DROPH    equ W16W_CS + 8     ; ...the list's when it drops, frame and all
CCF_DROP    equ 1               ; the list is down
CCF_FOCUS   equ 2               ; a drop-down list's field has the focus
CCF_MIF     equ 4               ; WM_MEASUREITEM owed for the field,
CCF_MIL     equ 8               ; ...and for the list's items
W16C_GAP    equ 8               ; a drop-down's edit to its button

; =============================================================================
; The procedure
; =============================================================================

; w16c_proc - a combo box's: BX = it, [w16_sm_*] the message -> DX:AX.
; Keeps BX.
w16c_proc:
    mov cx, [w16_sm_msg]
    mov si, w16c_tab    ; XSEC-OK: read through cs:
.find:
    mov ax, [cs:si]
    or ax, ax
    jz .fwd
    cmp ax, cx
    je .hit
    add si, 4
    jmp short .find
.hit:
    jmp [cs:si + 2]
.fwd:
    mov si, w16c_ftab   ; XSEC-OK: read through cs:
.ff:                                ; the CB_ messages that are the list's
    mov ax, [cs:si]
    or ax, ax
    jz w16c_zero
    cmp ax, cx
    je .fhit
    add si, 6
    jmp short .ff
.fhit:
    mov ax, [cs:si + 2]             ; its LB_ name, and whether the field
    mov [w16_sm_msg], ax            ;   shows something else after it
    push word [cs:si + 4]
    push bx
    call w16c_list                  ; BX = the list
    jc .gone
    call w16d_list
    pop bx
    pop cx
    jcxz .out
    push ax
    push dx
    call w16c_field
    pop dx
    pop ax
.out:
    ret
.gone:
    pop bx
    pop cx
    jmp w16c_err

w16c_zero:
    xor ax, ax
    xor dx, dx
    ret

w16c_err:
    mov ax, -1
    mov dx, ax
    ret

w16c_tab:
    dw WM_CREATE, w16c_create
    dw WM_DESTROY, w16c_destroy
    dw WM_PAINT, w16c_paint
    dw WM_GETDLGCODE, w16c_code
    dw WM_SETFOCUS, w16c_focus
    dw WM_KILLFOCUS, w16c_unfocus
    dw WM_LBUTTONDOWN, w16c_down
    dw WM_KEYDOWN, w16c_key
    dw WM_CHAR, w16c_char
    dw WM_COMMAND, w16c_cmd
    dw WM_ENABLE, w16c_enable
    dw WM_SETTEXT, w16c_settext
    dw WM_GETTEXT, w16c_gettext
    dw WM_GETTEXTLENGTH, w16c_gettext
    dw CB_SHOWDROPDOWN, w16c_show
    dw CB_GETDROPPEDSTATE, w16c_dropped
    dw CB_GETDROPPEDCONTROLRECT, w16c_droprect
    dw CB_SETITEMHEIGHT, w16c_setih
    dw CB_GETITEMHEIGHT, w16c_getih
    dw CB_LIMITTEXT, w16c_toedit
    dw CB_GETEDITSEL, w16c_toedit
    dw CB_SETEDITSEL, w16c_toedit
    dw CB_DIR, w16c_err
    dw CB_SETEXTENDEDUI, w16c_zero
    dw CB_GETEXTENDEDUI, w16c_zero
    dw 0

w16c_ftab:                          ; CB_ message, the list's, 1 = the field
    dw CB_ADDSTRING, LB_ADDSTRING, 0    ;   shows what it leaves
    dw CB_INSERTSTRING, LB_INSERTSTRING, 0
    dw CB_DELETESTRING, LB_DELETESTRING, 1
    dw CB_RESETCONTENT, LB_RESETCONTENT, 1
    dw CB_GETCOUNT, LB_GETCOUNT, 0
    dw CB_GETCURSEL, LB_GETCURSEL, 0
    dw CB_SETCURSEL, LB_SETCURSEL, 1
    dw CB_GETLBTEXT, LB_GETTEXT, 0
    dw CB_GETLBTEXTLEN, LB_GETTEXTLEN, 0
    dw CB_FINDSTRING, LB_FINDSTRING, 0
    dw CB_FINDSTRINGEXACT, LB_FINDSTRING, 0
    dw CB_SELECTSTRING, LB_SELECTSTRING, 1
    dw CB_GETITEMDATA, LB_GETITEMDATA, 0
    dw CB_SETITEMDATA, LB_SETITEMDATA, 0
    dw 0

; w16c_type - BX = a combo box -> AL = its CBS_ kind. Keeps the rest.
w16c_type:
    mov al, [bx + W16W_STYLE]
    and al, CBS_TYPE
    ret

; w16c_list - BX = a combo box -> BX = its list. CF=1 it has none. Keeps
; the rest.
w16c_list:
    push ax
    mov ax, [bx + CC_LIST]
    or ax, ax
    jz .no
    ICALL w16_hwnd2rec
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_LIST
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; w16c_of - BX = a list box -> BX = the combo box it is the list of. CF=1
; none. Keeps the rest.
w16c_of:
    push ax
    mov ax, [bx + LC_COMBO]
    or ax, ax
    jz .no
    ICALL w16_hwnd2rec
    jc .no
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    jne .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; w16c_edit - BX = a combo box -> BX = its edit. CF=1 none. Keeps the rest.
w16c_edit:
    push ax
    mov ax, [bx + CC_EDIT]
    or ax, ax
    jz .no
    ICALL w16_hwnd2rec
    jc .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; w16c_tell - BX = a control, CX = a message, DX = wParam, SI:DI = lParam:
; it is told at once, [w16_sm_*] kept round it -> DX:AX. Keeps BX, SI, DI.
w16c_tell:
    push word [w16_sm_hwnd]
    push word [w16_sm_msg]
    push word [w16_sm_wp]
    push word [w16_sm_lp]
    push word [w16_sm_lp + 2]
    push ax
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    pop ax
    call w16d_cmsg
    pop word [w16_sm_lp + 2]
    pop word [w16_sm_lp]
    pop word [w16_sm_wp]
    pop word [w16_sm_msg]
    pop word [w16_sm_hwnd]
    ret

; =============================================================================
; Making and unmaking
; =============================================================================

; WM_CREATE: the field's height, the list (and an edit) made, and the combo
; box as tall as its field - the template's height was the field and the list
; dropped together
w16c_create:
    push es
    push ds
    pop es
    mov si, w16_cwkind              ; [w16_cw*] is the caller's: kept
    mov di, w16d_cwsv
    mov cx, W16_CWSZ
    cld
    rep movsb
    pop es
    xor ax, ax
    mov [bx + CC_LIST], ax
    mov [bx + CC_EDIT], ax
    mov byte [bx + CC_FL], al
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI  ; its scroll bar is its
    jz .nv                                          ;   list's
    and word [bx + W16W_STYLE + 2], ~WS_VSCROLL_HI
    mov ax, WS_VSCROLL_HI
.nv:
    mov [w16_cwstyle + 2], ax
    ICALL w16_rowsn
    mov [bx + CC_FH], ax
    add ax, 2                       ; AX = the field, its frame and all
    mov cx, [bx + W16W_CY]
    call w16c_type
    cmp al, CBS_SIMPLE
    jne .drop
    mov ax, [bx + CC_FH]            ; a simple one: the list under the field,
    add ax, 2                       ;   the combo box as tall as it asked
    mov [w16_cwy], ax
    sub cx, ax
    mov [w16_cwcy], cx
    or word [w16_cwstyle + 2], WS_VISIBLE_HI
    ICALL w16_rec2hwnd
    jmp short .list
.drop:
    mov ax, [bx + CC_FH]
    add ax, 2
    mov [bx + W16W_CY], ax
    sub cx, ax                      ; the list's own height, its top on the
    inc cx                          ;   field's bottom line
    mov [bx + CC_DROPH], cx
    mov [w16_cwcy], cx
    mov word [w16_cwy], 0
    push bx
    ICALL w16_toplevel
    ICALL w16_rec2hwnd
    pop bx
.list:
    mov [w16_cwpar], ax
    mov word [w16_cwx], 0
    mov ax, [bx + W16W_CX]
    mov [w16_cwcx], ax
    mov ax, [bx + W16W_MENU]
    mov [w16_cwid], ax
    xor ax, ax                      ; its style: the combo box's in the list
    test word [bx + W16W_STYLE], CBS_SORT   ;   box's terms
    jz .s
    or al, LBS_SORT
.s:
    test word [bx + W16W_STYLE], CBS_OWNERDRAWFIXED
    jz .o
    or al, LBS_OWNERDRAWFIXED
.o:
    test word [bx + W16W_STYLE], CBS_HASSTRINGS
    jz .h
    or al, LBS_HASSTRINGS
.h:
    or ax, LBS_NOINTEGRALHEIGHT
    mov [w16_cwstyle], ax
    or word [w16_cwstyle + 2], WS_CHILD_HI | WS_BORDER_HI
    mov byte [w16_cwkind], W16Q_LIST
    push bx
    call w16c_mkchild               ; BX = the list
    jc .fail
    ICALL w16_rec2hwnd
    pop si                          ; SI = the combo box
    push si
    mov [si + CC_LIST], ax
    push ax
    xchg bx, si
    ICALL w16_rec2hwnd
    mov [si + LC_COMBO], ax         ; the list is the combo box's
    and byte [si + W16W_F2], ~W16W2_OWEMI   ; ...and measured by it
    pop ax
    call w16c_type
    cmp al, CBS_DROPDOWNLIST
    je .noedit
    xor ax, ax                      ; the edit: the field
    mov [w16_cwx], ax
    mov [w16_cwy], ax
    mov ax, [bx + CC_FH]
    add ax, 2
    mov [w16_cwcy], ax
    call w16c_type
    cmp al, CBS_SIMPLE
    mov ax, [bx + W16W_CX]
    je .w
    sub ax, W16D_SBW + W16C_GAP
.w:
    mov [w16_cwcx], ax
    ICALL w16_rec2hwnd
    mov [w16_cwpar], ax
    mov word [w16_cwid], 1001       ; Windows' ID for it
    xor ax, ax
    test word [bx + W16W_STYLE], CBS_AUTOHSCROLL
    jz .ah
    mov ax, ES_AUTOHSCROLL
.ah:
    mov [w16_cwstyle], ax
    mov word [w16_cwstyle + 2], WS_CHILD_HI | WS_VISIBLE_HI | WS_BORDER_HI
    mov byte [w16_cwkind], W16Q_EDIT
    call w16c_mkchild               ; BX = the edit
    jc .fail
    ICALL w16_rec2hwnd
    pop bx                          ; the combo box, kept on the stack
    push bx
    mov [bx + CC_EDIT], ax
.noedit:
    pop bx
    test word [bx + W16W_STYLE], CBS_OWNERDRAWFIXED
    jz .ok
    or byte [bx + CC_FL], CCF_MIF | CCF_MIL
    or byte [bx + W16W_F2], W16W2_OWEMI
.ok:
    call w16c_cwback
    jmp w16c_zero
.fail:
    pop bx
    call w16c_cwback
    call w16c_unmake
    jmp w16c_err

; w16c_cwback - the caller's [w16_cw*] put back. Keeps every register.
w16c_cwback:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, w16d_cwsv
    mov di, w16_cwkind
    mov cx, W16_CWSZ
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    ret

; w16c_mkchild - one of WIN16's controls from [w16_cw*] (the kind, style,
; place, size, parent and ID set), made and told WM_CREATE -> BX. CF=1
; refused (and undone).
w16c_mkchild:
    mov byte [w16_cwcls], 0xFF
    ICALL w16_ctlstub
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    mov word [w16_cwhbr], COLOR_WINDOW + 1
    xor ax, ax
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], ax
    ICALL w16_mkwnd
    jc .out
    mov ax, [w16_face]              ; a combo box in the dialog face makes
    cmp ax, [w16_sface]             ;   its list and edit in it (124.22.17)
    je .sf
    or byte [bx + W16W_F2], W16W2_DFACE
.sf:
    push word [w16_sm_msg]
    push word [w16_sm_wp]
    push word [w16_sm_lp]
    push word [w16_sm_lp + 2]
    mov word [w16_sm_msg], WM_CREATE
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    ICALL w16_intproc
    pop word [w16_sm_lp + 2]
    pop word [w16_sm_lp]
    pop word [w16_sm_wp]
    pop word [w16_sm_msg]
    and ax, dx
    inc ax
    jnz .ok
    ICALL w16_wndfree
    stc
    ret
.ok:
    clc
.out:
    ret

; WM_DESTROY: the list goes with it - a drop-down's is not its child, so the
; tree does not take it
w16c_destroy:
    mov al, 1
    call w16c_close0                ; down, quietly
    call w16c_unmake
    jmp w16c_zero

; w16c_unmake - BX = a combo box: its list told WM_DESTROY and freed, if it
; is still its own. Keeps BX.
w16c_unmake:
    push bx
    ICALL w16_rec2hwnd
    mov dx, ax
    call w16c_list
    jc .out
    cmp [bx + LC_COMBO], dx
    jne .out
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .out
    mov word [bx + LC_COMBO], 0
    mov cx, WM_DESTROY
    xor dx, dx
    xor si, si
    xor di, di
    call w16c_tell
    ICALL w16_wndfree
.out:
    pop bx
    mov word [bx + CC_LIST], 0
    ret

; =============================================================================
; Painting: a frame, the field, the button (Windows 3.0's, measured off its
; Control Panel's Desktop dialog)
; =============================================================================

w16c_paint:
    call w16d_popen
    jnc .go
    jmp w16c_zero
.go:
    call w16c_type
    cmp al, CBS_SIMPLE
    jne .ns
    jmp .done                       ; its edit and its list are all of it
.ns:
    cmp al, CBS_DROPDOWN
    jne .list
    mov ax, [w16d_r]                ; a drop-down: the gap, then the button
    sub ax, W16D_SBW
    mov cx, ax
    sub ax, W16C_GAP - 1
    mov bx, [w16d_t]
    mov dx, [w16d_b]
    mov si, COLOR_WINDOW
    call w16d_fill
    inc cx
    mov ax, cx
    mov cx, [w16d_r]
    call w16c_button
    jmp .done
.list:
    mov si, COLOR_WINDOWFRAME       ; a drop-down list: its frame,
    mov ax, [w16d_l]
    mov bx, [w16d_t]
    mov cx, [w16d_r]
    mov dx, bx
    call w16d_fill
    mov bx, [w16d_b]
    mov dx, bx
    call w16d_fill
    mov bx, [w16d_t]
    inc bx
    dec dx
    mov cx, ax
    call w16d_fill
    mov ax, [w16d_r]                ; ...the button inside it, the frame's
    mov cx, ax                      ;   right line its own
    sub ax, W16D_SBW
    mov bx, [w16d_t]
    mov dx, [w16d_b]
    call w16c_button
    mov ax, [w16d_l]                ; ...and the field: the selection
    inc ax
    mov [w16_bxl], ax
    add ax, 2
    mov [w16_tx], ax
    mov ax, [w16d_r]
    sub ax, W16D_SBW + 1
    mov [w16_bxr], ax
    mov ax, [w16d_t]
    inc ax
    mov [w16_bxt], ax
    mov [w16_ty], ax
    mov ax, [w16d_b]
    dec ax
    mov [w16_bxb], ax
    mov bx, [w16d_rec]
    mov word [w16_tn], 0
    push bx
    call w16c_list
    mov ax, [bx + LC_CUR]
    pop bx
    jc .text
    cmp ax, -1
    je .text
    test word [bx + W16W_STYLE], CBS_OWNERDRAWFIXED
    jz .str
    mov dx, ax                      ; owner-drawn: its parent's to draw
    mov cl, ODA_DRAWENTIRE
    call w16_odput
    jmp .done
.str:
    push bx
    call w16c_list
    call w16d_lstr                  ; ES:SI, CX
    pop bx
    cmp cx, W16_TXTMAX
    jbe .c
    mov cx, W16_TXTMAX
.c:
    mov [w16_tn], cx
    push ds
    push ds
    push es
    pop ds
    pop es
    mov di, w16_txt
    cld
    rep movsb
    pop ds
.text:
    call w16c_hilite                ; CF=1 the field shows the focus
    mov ax, (COLOR_WINDOW << 8) | COLOR_WINDOWTEXT
    mov dx, COLOR_WINDOW
    jnc .col
    mov ax, (COLOR_HIGHLIGHT << 8) | COLOR_HIGHLIGHTTEXT
    mov dx, COLOR_HIGHLIGHT
.col:
    pushf
    call w16d_colors
    mov ax, dx
    call w16d_syscol
    mov [w16_gcol], al
    ICALL w16_tbox
    popf
    jnc .done
    mov al, COLOR_HIGHLIGHTTEXT     ; ...and the focus's dotted rectangle
    call w16d_syscol
    mov [w16_tcol], al
    mov ax, [w16_bxl]
    mov bx, [w16_bxt]
    mov cx, [w16_bxr]
    mov dx, [w16_bxb]
    call w16d_focus
.done:
    call w16d_pclose
    mov bx, [w16d_rec]
    jmp w16c_zero

; w16c_hilite - BX = a drop-down list: CF=1 its field is drawn as the focus -
; it has it, and the list is not down. Keeps every register.
w16c_hilite:
    test byte [bx + CC_FL], CCF_DROP
    jnz .no
    test byte [bx + CC_FL], CCF_FOCUS
    jz .no
    stc
    ret
.no:
    clc
    ret

; w16c_button - the drop button AX..CX x BX..DX (screen, inclusive), its
; black frame included: white up and left, the shadow down and right, the
; face, and the arrow over its bar. Keeps every register.
w16c_button:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov si, COLOR_WINDOWFRAME       ; the frame
    push dx
    mov dx, bx
    call w16d_fill
    pop dx
    push bx
    mov bx, dx
    call w16d_fill
    pop bx
    push cx
    mov cx, ax
    call w16d_fill
    pop cx
    push ax
    mov ax, cx
    call w16d_fill
    pop ax
    inc ax                          ; inside it
    inc bx
    dec cx
    dec dx
    push ax
    call w16d_bevel                 ; BP = the edges' width
    mov bp, ax
    pop ax
    mov si, COLOR_WINDOW            ; white along the top and down the left
    push dx
    mov dx, bx
    add dx, bp
    dec dx
    call w16d_fill
    pop dx
    push cx
    mov cx, ax
    add cx, bp
    dec cx
    call w16d_fill
    pop cx
    mov si, COLOR_BTNSHADOW         ; the shadow along the bottom and up the
    push ax                         ;   right
    push bx
    add ax, bp
    mov bx, dx
    sub bx, bp
    inc bx
    call w16d_fill
    pop bx
    pop ax
    push ax
    push bx
    mov ax, cx
    sub ax, bp
    inc ax
    add bx, bp
    call w16d_fill
    pop bx
    pop ax
    add ax, bp                      ; AX..CX x BX..DX = the face
    add bx, bp
    sub cx, bp
    sub dx, bp
    mov si, COLOR_BTNFACE
    call w16d_fill
    mov di, cx                      ; the picture in its middle
    sub di, ax
    inc di
    sub di, 7
    sar di, 1
    add di, ax
    mov [w16_tx], di
    mov di, dx
    sub di, bx
    inc di                          ; DI = the face's rows: the tall arrow
    mov si, w16c_pic8   ; XSEC-OK: read through cs:
    mov cx, 8                       ;   (a stem) where it fits
    cmp di, 10
    jae .pic
    mov si, w16c_pic6   ; XSEC-OK: read through cs:
    mov cx, 6
.pic:
    sub di, cx
    sar di, 1
    add di, bx
    mov [w16_ty], di
    push cx
    mov di, w16d_pbuf
.cp:
    mov ax, [cs:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cp
    mov ax, (COLOR_BTNFACE << 8) | COLOR_BTNTEXT
    call w16d_colors
    pop cx
    mov si, w16d_pbuf
    mov dx, 7
    ICALL w16_bmp
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16c_pic8:  db 0x38,0, 0x38,0, 0xFE,0, 0x7C,0, 0x38,0, 0x10,0, 0,0, 0xFE,0
w16c_pic6:  db 0xFE,0, 0x7C,0, 0x38,0, 0x10,0, 0,0, 0xFE,0

; =============================================================================
; The focus, the keys and the mouse
; =============================================================================

w16c_code:
    mov ax, DLGC_WANTARROWS | DLGC_WANTCHARS
    call w16c_type
    cmp al, CBS_DROPDOWNLIST
    mov ax, DLGC_WANTARROWS | DLGC_WANTCHARS
    je .d
    or ax, DLGC_HASSETSEL
.d:
    test byte [bx + CC_FL], CCF_DROP
    jz .out
    or ax, DLGC_WANTALLKEYS         ; down: Enter and Escape are its
.out:
    xor dx, dx
    ret

w16c_focus:
    mov ax, [bx + CC_EDIT]          ; one with an edit hands it on, its text
    or ax, ax                       ;   all selected
    jz .own
    push bx
    ICALL w16_setfocus
    pop bx
    push bx
    call w16c_edit
    jc .x
    mov cx, EM_SETSEL
    xor dx, dx
    xor di, di
    mov si, 0x7FFF
    call w16c_tell
.x:
    pop bx
    jmp w16c_zero
.own:
    or byte [bx + CC_FL], CCF_FOCUS
    call w16d_inval
    mov ax, CBN_SETFOCUS
    call w16d_notify
    jmp w16c_zero

w16c_unfocus:
    mov ax, [w16_sm_wp]             ; to its own edit: still in it
    cmp ax, [bx + CC_EDIT]
    je .z
    cmp word [bx + CC_EDIT], 0
    jne .z                          ; its edit's EN_KILLFOCUS says when
    and byte [bx + CC_FL], ~CCF_FOCUS
    xor al, al
    call w16c_close
    call w16d_inval
    mov ax, CBN_KILLFOCUS
    call w16d_notify
.z:
    jmp w16c_zero

; WM_COMMAND: its edit's notifications, as the combo box's own
w16c_cmd:
    mov ax, [w16_sm_lp + 2]
    cmp ax, EN_SETFOCUS_
    mov cx, CBN_SETFOCUS
    je .tell
    cmp ax, EN_CHANGE_
    mov cx, CBN_EDITCHANGE
    je .tell
    cmp ax, EN_UPDATE
    mov cx, CBN_EDITUPDATE
    je .tell
    cmp ax, EN_KILLFOCUS_
    jne .z
    mov ax, [w16_focus]             ; the focus went where? not to the
    cmp ax, [bx + CC_EDIT]          ;   combo box or its edit: down it goes
    je .z
    push ax
    ICALL w16_rec2hwnd
    pop cx
    cmp ax, cx
    je .z
    xor al, al
    call w16c_close
    mov cx, CBN_KILLFOCUS
.tell:
    mov ax, cx
    call w16d_notify
.z:
    jmp w16c_zero

; WM_LBUTTONDOWN: the focus, and the list dropped - anywhere on a drop-down
; list, on the button of a drop-down
w16c_down:
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_setfocus
    pop bx
    call w16c_type
    cmp al, CBS_SIMPLE
    je .z
    cmp al, CBS_DROPDOWNLIST
    je .drop
    ICALL w16_csize                 ; CX = its width
    mov ax, [w16_sm_lp]
    add ax, W16D_SBW
    cmp ax, cx
    jl .z
.drop:
    test byte [bx + CC_FL], CCF_DROP
    jnz .up
    call w16c_drop
    push bx                         ; this press is the list's now: dragged
    call w16c_list                  ;   into it, it chooses
    jc .x
    or byte [bx + LC_FL], LCF_TRACK | LCF_DPRESS
.x:
    pop bx
.z:
    jmp w16c_zero
.up:
    mov al, 1
    call w16c_close
    jmp w16c_zero

; WM_KEYDOWN (its own, or its edit's passed up): F4 drops and closes it,
; Enter and Escape close it when it is down, and the arrows, Home, End, Page
; Up and Page Down move the selection as the list's own keys do
w16c_key:
    mov ax, [w16_sm_wp]
    cmp ax, VK_F4
    je .toggle
    test byte [bx + CC_FL], CCF_DROP
    jz .move
    cmp ax, VK_RETURN
    mov dl, 1
    je .close
    cmp ax, VK_ESCAPE
    mov dl, 0
    je .close
.move:
    push bx
    call w16c_list
    jc .x
    call w16d_lkey
.x:
    pop bx
    jmp w16c_zero
.toggle:
    call w16c_type
    cmp al, CBS_SIMPLE
    je .z
    test byte [bx + CC_FL], CCF_DROP
    jnz .up
    call w16c_drop
.z:
    jmp w16c_zero
.up:
    mov dl, 1
.close:
    mov al, dl
    call w16c_close
    jmp w16c_zero

; WM_CHAR: a drop-down list's next item that begins with the letter
w16c_char:
    call w16c_type
    cmp al, CBS_DROPDOWNLIST
    jne .z
    push bx
    call w16c_list
    jc .x
    call w16d_lchar
.x:
    pop bx
.z:
    jmp w16c_zero

; w16c_altkey - BX = a control, [w16_sm_*] a WM_SYSKEYDOWN: Alt+Down or
; Alt+Up on a combo box (or its edit) drops or closes it. CF=1 it was that.
; Keeps BX.
w16c_altkey:
    mov ax, [w16_sm_wp]
    cmp ax, VK_DOWN
    je .arrow
    cmp ax, VK_UP
    jne .no
.arrow:
    push bx
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    je .have
    ICALL w16_parent
    jc .not
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    jne .not
.have:
    mov word [w16_sm_wp], VK_F4
    call w16c_key
    pop bx
    stc
    ret
.not:
    pop bx
.no:
    clc
    ret

; w16c_ekey - BX = an edit, [w16_sm_*] its WM_KEYDOWN: in a combo box, F4,
; the up and down keys - and Enter and Escape while it is down - are the
; combo box's. CF=1 they were. Keeps BX.
w16c_ekey:
    push bx
    ICALL w16_parent
    jc .not
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    jne .not
    mov ax, [w16_sm_wp]
    cmp ax, VK_F4
    je .combo
    cmp ax, VK_UP
    je .combo
    cmp ax, VK_DOWN
    je .combo
    test byte [bx + CC_FL], CCF_DROP
    jz .not
    cmp ax, VK_RETURN
    je .combo
    cmp ax, VK_ESCAPE
    je .combo
    cmp ax, VK_PRIOR
    je .combo
    cmp ax, VK_NEXT
    jne .not
.combo:
    call w16c_key
    pop bx
    stc
    ret
.not:
    pop bx
    clc
    ret

; w16c_ecode - an edit's WM_GETDLGCODE answer AX: a dropped combo box's
; edit wants every key. Keeps BX.
w16c_ecode:
    push bx
    ICALL w16_parent
    jc .out
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    jne .out
    test byte [bx + CC_FL], CCF_DROP
    jz .out
    or ax, DLGC_WANTALLKEYS
.out:
    pop bx
    ret

w16c_enable:
    call w16d_inval
    mov ax, [bx + CC_EDIT]
    or ax, ax
    jz .z
    push bx
    mov dx, [w16_sm_wp]
    ICALL w16_enable
    pop bx
.z:
    jmp w16c_zero

; =============================================================================
; Its text, its field and its list
; =============================================================================

; WM_SETTEXT: an edit's; a drop-down list has no text of its own (CB_ERR)
w16c_settext:
    push bx
    call w16c_edit
    jc .no
    mov cx, WM_SETTEXT
    xor dx, dx
    mov di, [w16_sm_lp]
    mov si, [w16_sm_lp + 2]
    call w16c_tell
    pop bx
    ret
.no:
    pop bx
    jmp w16c_err

; WM_GETTEXT and WM_GETTEXTLENGTH: an edit's, or the selection's string
w16c_gettext:
    push bx
    call w16c_edit
    jc .sel
    mov cx, [w16_sm_msg]
    mov dx, [w16_sm_wp]
    mov di, [w16_sm_lp]
    mov si, [w16_sm_lp + 2]
    call w16c_tell
    pop bx
    ret
.sel:
    pop bx
    push bx
    call w16c_list
    jc .none
    mov ax, [bx + LC_CUR]
    cmp ax, -1
    je .none
    cmp word [w16_sm_msg], WM_GETTEXT
    je .get
    call w16d_lstr
    mov ax, cx
    xor dx, dx
    pop bx
    ret
.get:
    mov cx, [w16_sm_wp]             ; at most wParam - 1 and a NUL
    jcxz .none
    dec cx
    push cx
    call w16d_lstr                  ; ES:SI, CX
    pop ax
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    push cx
    mov di, [w16_sm_lp]
    mov dx, [w16_sm_lp + 2]
    push ds
    push es
    pop ds                          ; DS:SI = the string
    mov es, dx                      ; ES:DI = the buffer
    cld
    rep movsb
    mov byte [es:di], 0
    pop ds
    pop ax
    xor dx, dx
    pop bx
    ret
.none:
    pop bx
    jmp w16c_zero

; w16c_field - BX = a combo box: its field shows the selection - an edit is
; given the item's string, all of it selected; a drop-down list's field is
; drawn again. Keeps every register.
w16c_field:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call w16d_inval
    mov ax, [bx + CC_EDIT]
    or ax, ax
    jz .out
    push bx
    call w16c_list
    mov ax, [bx + LC_CUR]
    jc .out0
    cmp ax, -1
    je .out0
    call w16d_lstr                  ; ES:SI = the string
    mov di, si
    mov si, es
    pop bx
    push bx
    call w16c_edit
    jc .out0
    mov cx, WM_SETTEXT
    xor dx, dx
    call w16c_tell
    mov cx, EM_SETSEL
    xor dx, dx
    xor di, di
    mov si, 0x7FFF
    call w16c_tell
.out0:
    pop bx
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16c_lsel - BX = a combo box's list, its selection just changed by the
; mouse or a key: CBN_SELCHANGE to the combo box's parent, and the field.
; Keeps BX.
w16c_lsel:
    push bx
    call w16c_of
    jc .out
    mov ax, CBN_SELCHANGE
    call w16d_notify
    call w16c_field
.out:
    pop bx
    ret

; CB_LIMITTEXT, CB_GETEDITSEL and CB_SETEDITSEL: its edit's EM_ messages
w16c_toedit:
    push bx
    call w16c_edit
    jc .no
    mov cx, [w16_sm_msg]
    sub cx, CB_LIMITTEXT
    jz .lim
    dec cx
    mov cx, EM_SETSEL
    jz .send
    mov cx, EM_GETSEL
    jmp short .send
.lim:
    mov cx, EM_LIMITTEXT
.send:
    mov dx, [w16_sm_wp]
    mov di, [w16_sm_lp]
    mov si, [w16_sm_lp + 2]
    call w16c_tell
    pop bx
    ret
.no:
    pop bx
    jmp w16c_err

; CB_SETITEMHEIGHT: wParam -1 the field's, else the list's items'
w16c_setih:
    mov ax, [w16_sm_lp]
    or ax, ax
    jz .err
    cmp ax, 255
    ja .err
    cmp word [w16_sm_wp], -1
    je .field
    push bx
    call w16c_list
    jc .x
    mov [bx + LC_ITEMH], ax
    call w16d_inval
.x:
    pop bx
    jmp w16c_zero
.field:
    call w16c_fieldh
    jmp w16c_zero
.err:
    jmp w16c_err

; w16c_fieldh - BX = a combo box, AX = its field's height inside the frame:
; the combo box (and its edit, a simple one's list) made to fit it. Keeps
; every register.
w16c_fieldh:
    push ax
    push bx
    push dx
    mov [bx + CC_FH], ax
    add ax, 2
    mov dx, ax
    call w16c_type
    cmp al, CBS_SIMPLE
    je .edit
    mov [bx + W16W_CY], dx
.edit:
    push bx
    call w16c_edit
    jc .ne
    mov [bx + W16W_CY], dx
.ne:
    pop bx
    call w16c_type
    cmp al, CBS_SIMPLE
    jne .out
    call w16c_list
    jc .out
    mov ax, [bx + W16W_Y]           ; a simple one's list moves down with it
    add [bx + W16W_CY], ax
    mov [bx + W16W_Y], dx
    sub [bx + W16W_CY], dx
.out:
    pop dx
    pop bx
    pop ax
    call w16d_inval
    ret

w16c_getih:
    cmp word [w16_sm_wp], -1
    mov ax, [bx + CC_FH]
    je .out
    push bx
    call w16c_list
    mov ax, [bx + LC_ITEMH]
    pop bx
.out:
    xor dx, dx
    ret

w16c_show:
    cmp word [w16_sm_wp], 0
    je .up
    call w16c_drop
    jmp short .out
.up:
    xor al, al
    call w16c_close
.out:
    mov ax, 1
    xor dx, dx
    ret

w16c_dropped:
    xor ax, ax
    test byte [bx + CC_FL], CCF_DROP
    jz .out
    inc ax
.out:
    xor dx, dx
    ret

; CB_GETDROPPEDCONTROLRECT: where the list drops, on the screen
w16c_droprect:
    call w16c_place                 ; AX/DX/CX/SI = it in the top-level's
    jc .out                         ;   client
    push bx
    push cx
    push si
    push ax
    push dx
    ICALL w16_toplevel
    ICALL w16_origin                ; AX/DX = its client on the screen
    mov di, ax
    mov bx, dx
    pop dx
    pop ax
    pop si
    pop cx
    add ax, di
    add dx, bx
    push es
    les di, [w16_sm_lp]
    mov [es:di], ax
    mov [es:di + 2], dx
    add ax, cx
    mov [es:di + 4], ax
    add dx, si
    mov [es:di + 6], dx
    pop es
    pop bx
.out:
    jmp w16c_zero

; =============================================================================
; Dropping and closing the list (SPEC.md 124.22.14)
; =============================================================================

; w16c_place - BX = a drop-down combo box -> AX/DX = where its list goes in
; its top-level's client, CX/SI = its width and height: under the field, a
; whole number of items, above the field when it does not fit below and does
; there, else cut to what does. CF=1 it cannot drop. Keeps BX.
w16c_place:
    push bx
    push di
    push bp
    call w16c_type
    cmp al, CBS_SIMPLE
    je .no
    ICALL w16_origin                ; AX/DX = the combo box on the screen
    jc .no
    push ax
    push dx
    push bx
    ICALL w16_toplevel
    ICALL w16_origin                ; ...and its top-level's client: SI high
    mov di, ax
    mov bp, dx
    pop bx
    pop dx
    pop ax
    jc .no
    sub ax, di                      ; AX/DX = the field in that client
    sub dx, bp
    push si                         ; the client's height
    push bx
    call w16c_list
    mov cx, [bx + LC_ITEMH]
    pop bx
    jc .no1
    mov di, [bx + CC_DROPH]         ; DI = its height: whole items, one at
    sub di, 2                       ;   least, and its frame
    push ax
    push dx
    mov ax, di
    xor dx, dx
    or ax, ax
    jle .one
    div cx
    or ax, ax
    jnz .k
.one:
    mov ax, 1
.k:
    mul cx
    add ax, 2
    mov di, ax
    pop dx
    pop ax
    pop bp                          ; BP = the client's height
    mov si, dx                      ; below: from the field's bottom line
    add si, [bx + W16W_CY]
    dec si
    push si
    add si, di
    cmp si, bp
    pop si
    jle .at
    mov cx, dx                      ; above: to its top line
    inc cx
    sub cx, di
    jl .cut
    mov si, cx
    jmp short .at
.cut:
    mov cx, bp                      ; neither: below, cut
    sub cx, si
    mov di, cx
.at:
    mov dx, si
    mov cx, [bx + W16W_CX]
    mov si, di
    clc
    jmp short .out
.no1:
    pop si
.no:
    stc
.out:
    pop bp
    pop di
    pop bx
    ret

; w16c_drop - BX = a combo box: its list down (CBN_DROPDOWN first), shown
; over everything in its window, the selection in view, and the mouse its
; own. Keeps BX.
w16c_drop:
    test byte [bx + CC_FL], CCF_DROP
    jnz .ret
    push ax
    push cx
    push dx
    push si
    push di
    call w16c_place
    jc .out
    push ax
    mov ax, CBN_DROPDOWN
    call w16d_notify
    pop ax
    or byte [bx + CC_FL], CCF_DROP
    push bx
    call w16c_list
    mov [bx + W16W_X], ax
    mov [bx + W16W_Y], dx
    mov [bx + W16W_CX], cx
    mov [bx + W16W_CY], si
    or byte [bx + W16W_FLAGS], W16WF_VIS
    inc word [w16_seq]              ; the last made: over its siblings
    mov ax, [w16_seq]
    mov [bx + W16W_SEQ], ax
    mov ax, [bx + LC_CUR]           ; the selection in view
    cmp ax, -1
    je .top
    cmp ax, [bx + LC_TOP]
    jb .settop
    call w16d_lvis
    mov dx, [bx + LC_TOP]
    add dx, cx
    cmp ax, dx
    jb .top
    sub ax, cx
    inc ax
.settop:
    mov [bx + LC_TOP], ax
.top:
    and byte [bx + LC_FL], ~(LCF_TRACK | LCF_DPRESS)
    call w16d_inval
    ICALL w16_rec2hwnd
    mov [w16_capture], ax
    mov [w16_cbdl], ax
    ICALL w16_origin                ; its rectangle on the screen, which
    mov [w16_cbdr], ax              ;   nothing under it paints into while
    mov [w16_cbdr + 2], dx          ;   it is down
    add ax, cx
    dec ax
    mov [w16_cbdr + 4], ax
    add dx, si
    dec dx
    mov [w16_cbdr + 6], dx
    pop bx
    call w16d_inval                 ; the field: not the focus while down
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
.ret:
    ret

; w16c_close - BX = a combo box, AL = 1 chosen / 0 cancelled: its list up,
; what was under it painted again, CBN_SELENDOK or CBN_SELENDCANCEL then
; CBN_CLOSEUP. w16c_close0 tells nobody. Keeps every register.
w16c_close:
    test byte [bx + CC_FL], CCF_DROP
    jz .ret
    push ax
    mov ah, CBN_SELENDCANCEL
    or al, al
    jz .n
    mov ah, CBN_SELENDOK
.n:
    mov al, ah
    xor ah, ah
    call w16d_notify
    xor al, al
    call w16c_close0
    mov ax, CBN_CLOSEUP
    call w16d_notify
    pop ax
.ret:
    ret

w16c_close0:
    test byte [bx + CC_FL], CCF_DROP
    jz .ret
    push ax
    push bx
    push cx
    push dx
    push si
    and byte [bx + CC_FL], ~CCF_DROP
    call w16d_inval
    call w16c_list
    jc .out
    and byte [bx + W16W_FLAGS], ~W16WF_VIS
    and byte [bx + LC_FL], ~(LCF_TRACK | LCF_DPRESS)
    ICALL w16_rec2hwnd
    cmp [w16_capture], ax
    jne .cap
    mov word [w16_capture], 0
.cap:
    mov word [w16_cbdl], 0
    mov ax, [bx + W16W_X]           ; what it covered (its frame is inside
    mov cx, [bx + W16W_Y]           ;   it), in its parent: invalid and erased
    mov dx, ax
    add dx, [bx + W16W_CX]
    mov si, cx
    add si, [bx + W16W_CY]
    ICALL w16_parent
    jc .out
    mov byte [w16_erase], 1
    ICALL w16_inval
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; w16c_cancel - BX = a combo box's list, pressed off it: closed up,
; cancelled. Keeps every register.
w16c_cancel:
    push ax
    push bx
    call w16c_of
    jc .out
    xor al, al
    call w16c_close
.out:
    pop bx
    pop ax
    ret

; w16c_lin - BX = a list box, [w16_sm_lp] a point in its client: CF=1 it is
; on the list. Keeps every register.
w16c_lin:
    push ax
    push cx
    push dx
    ICALL w16_csize                 ; CX/DX = its client
    mov ax, [w16_sm_lp]
    or ax, ax
    js .no
    cmp ax, cx
    jge .no
    mov ax, [w16_sm_lp + 2]
    or ax, ax
    js .no
    cmp ax, dx
    jge .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

; w16c_lup - BX = a combo box's list, a press on it let go: on an item it
; is chosen and the list goes up; off it, the press that dropped it leaves
; it down, any other closes it
w16c_lup:
    call w16c_lin
    jc .chosen
    test byte [bx + LC_FL], LCF_DPRESS
    jz .cancel
    and byte [bx + LC_FL], ~LCF_DPRESS
    jmp w16c_zero
.cancel:
    call w16c_cancel
    jmp w16c_zero
.chosen:
    and byte [bx + LC_FL], ~LCF_DPRESS
    push bx
    call w16c_of
    jc .x
    mov al, 1
    call w16c_close
.x:
    pop bx
    jmp w16c_zero

; w16c_osclose - os8088 is painting WIN16's window again (a move, a window
; over it): a dropped list goes up first, as Windows' did when anything else
; happened, and nothing under it is left owed. The library part's, LCALLed.
w16c_osclose:
    push ax
    push bx
    mov ax, [w16_cbdl]
    or ax, ax
    jz .out
    ICALL w16_hwnd2rec
    jc .clear
    call w16c_of
    jc .clear
    xor al, al
    call w16c_close
    jmp short .out
.clear:
    mov word [w16_cbdl], 0
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Owner-drawn items: what is owed, and the sends that pay it
; =============================================================================

; w16_odput - BX = an owner-drawn list box or combo box (whose item is its
; field), AX..DX = the items owed, CL = the ODA_ action: queued for its
; parent's WM_DRAWITEM, joined to what is owed already for the same window
; and action. A hidden window is owed nothing. Keeps every register.
w16_odput:
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .ret
    cmp ax, dx
    jg .ret
    push ax
    push bx
    push cx
    push si
    push di
    mov di, ax
    ICALL w16_rec2hwnd
    xchg ax, di                     ; DI = its HWND
    mov ch, [w16_inpaint]
    mov si, w16_odq
    xor bx, bx
.find:
    cmp bl, [w16_odn]
    jae .new
    cmp [si + OQ_HWND], di
    jne .next
    cmp [si + OQ_ACT], cx
    jne .next
    cmp ax, [si + OQ_FIRST]
    jge .f
    mov [si + OQ_FIRST], ax
.f:
    cmp dx, [si + OQ_LAST]
    jle .out
    mov [si + OQ_LAST], dx
    jmp short .out
.next:
    add si, OQ_SIZE
    inc bx
    jmp short .find
.new:
    cmp bl, W16_ODQN
    jae .out                        ; full: dropped
    mov [si + OQ_HWND], di
    mov [si + OQ_FIRST], ax
    mov [si + OQ_LAST], dx
    mov [si + OQ_ACT], cx
    inc byte [w16_odn]
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

; w16_odtake - entry SI (a pointer into w16_odq) out of the queue. Keeps
; every register.
w16_odtake:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, si
    add si, OQ_SIZE
    mov cx, w16_odq + W16_ODQN * OQ_SIZE
    sub cx, si
    shr cx, 1
    cld
    rep movsw
    dec byte [w16_odn]
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; w16_odgo - AL = who asks (W16OD_*), in an import that is waiting for the
; program's next message: the first owed item (inside W_PAINT, one owed by
; this pass's paint) taken out and SENT - WM_DRAWITEM to the control's parent
; with a DC open on the control, cut to the item, and w16_odback continuing.
; CF=1 nothing was sent. Reached by LCALL from the image and called by the
; modal loop.
; -----------------------------------------------------------------------------
w16_odgo:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [w16_odctx], al
.next:
    mov si, w16_odq
    xor cx, cx
.find:
    cmp cl, [w16_odn]
    jae .none
    mov al, [si + OQ_PAINT]
    cmp byte [w16_inpaint], 0       ; inside W_PAINT the damage is the clip:
    je .take                        ;   what is owed from before waits
    or al, al
    jnz .take
    add si, OQ_SIZE
    inc cx
    jmp short .find
.none:
    stc
    jmp .ret
.take:
    mov ax, [si + OQ_HWND]          ; the window, still there?
    ICALL w16_hwnd2rec
    jc .drop
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_VIS | W16WF_DYING
    cmp al, W16WF_USED | W16WF_VIS
    jne .drop
    mov ax, [si + OQ_FIRST]         ; the item, the entry moved past it
    mov [w16_odi], ax
    mov dl, [si + OQ_ACT]
    mov [w16_odact], dl
    inc word [si + OQ_FIRST]
    mov dx, [si + OQ_LAST]
    cmp dx, ax
    jg .kept
    call w16_odtake
.kept:
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    je .combo
    cmp byte [bx + W16W_KIND], W16Q_LIST
    jne .next
    call w16_odlrect                ; a list's item: CF=1 not in view
    jc .next
    jmp short .send
.combo:
    call w16_odfrect
    jc .next
.send:
    call w16_odsend
    jc .next
    clc
    jmp short .ret
.drop:
    call w16_odtake
    jmp .next
.ret:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16_odlrect - BX = a list box, [w16_odi] = an item -> its rcItem in
; [w16_odrc], its state, owner and IDs (w16_odwho). CF=1 it is not in view.
w16_odlrect:
    mov ax, [w16_odi]
    cmp ax, [bx + LC_COUNT]
    jae .no
    sub ax, [bx + LC_TOP]
    jb .no
    call w16d_lvis
    cmp ax, cx
    jae .no
    mul word [bx + LC_ITEMH]
    xor cx, cx
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .nb
    inc cx
.nb:
    add ax, cx
    mov [w16_odrc + 2], ax
    add ax, [bx + LC_ITEMH]
    mov [w16_odrc + 6], ax
    mov [w16_odrc], cx
    push cx
    ICALL w16_csize                 ; CX = its width
    mov ax, cx
    pop cx
    sub ax, cx
    call w16d_lbar
    jnc .nbar
    sub ax, W16D_SBW
.nbar:
    mov [w16_odrc + 4], ax
    xor al, al                      ; its state
    mov dx, [w16_odi]
    cmp dx, [bx + LC_CUR]
    jne .st
    or al, ODS_SELECTED
    test byte [bx + LC_FL], LCF_FOCUS
    jz .st
    or al, ODS_FOCUS
.st:
    mov [w16_odst], al
    mov ax, [w16_odi]
    call w16d_lent
    mov ax, [es:si + 2]
    mov [w16_oddata], ax
    mov ax, [es:si + 4]
    mov [w16_oddata + 2], ax
    push bx                         ; for a combo box's list, the combo box
    call w16c_of                    ;   is the control
    jnc .cb
    pop bx
    push bx
    mov byte [w16_odtype], ODT_LISTBOX
    jmp short .who
.cb:
    mov byte [w16_odtype], ODT_COMBOBOX
.who:
    call w16_odwho
    pop bx
    ret
.no:
    stc
    ret

; w16_odfrect - BX = a combo box: its field's item, the selection, in
; [w16_odi], rcItem, state, owner and IDs. CF=1 it has none.
w16_odfrect:
    push bx
    call w16c_list
    mov ax, [bx + LC_CUR]
    jc .no0
    cmp ax, -1
    je .no0
    mov [w16_odi], ax
    call w16d_lent
    mov ax, [es:si + 2]
    mov [w16_oddata], ax
    mov ax, [es:si + 4]
    mov [w16_oddata + 2], ax
    pop bx
    mov word [w16_odrc], 1
    mov word [w16_odrc + 2], 1
    ICALL w16_csize
    sub cx, W16D_SBW + 1
    mov [w16_odrc + 4], cx
    dec dx
    mov [w16_odrc + 6], dx
    xor al, al
    call w16c_hilite
    jnc .st
    mov al, ODS_SELECTED | ODS_FOCUS
.st:
    mov [w16_odst], al
    mov byte [w16_odtype], ODT_COMBOBOX
    jmp short w16_odwho
.no0:
    pop bx
    stc
    ret

; w16_odwho - BX = the control (the combo box for its list's items): its
; parent the owner, in [w16_odown], its ID and HWND, and ODS_DISABLED.
; CF=1 its parent cannot be sent to.
w16_odwho:
    ICALL w16_rec2hwnd
    mov [w16_odhw], ax
    mov ax, [bx + W16W_MENU]
    mov [w16_odid], ax
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jz .en
    or byte [w16_odst], ODS_DISABLED
.en:
    mov ax, [bx + W16W_PARENT]
    mov [w16_odown], ax
    push bx
    ICALL w16_hwnd2rec
    jc .no
    ICALL w16_isctl
    jnc .no
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; w16_odsend - BX = the window drawn in, [w16_od*] the item: the DC on it,
; cut to the item, the DRAWITEMSTRUCT on the program's stack, and the send
; to the owner, w16_odback continuing. CF=1 not sent.
w16_odsend:
    mov ax, [w16_odown]
    push bx
    ICALL w16_hwnd2rec
    pop bx
    jc .no
    xor al, al                      ; its whole client, then cut
    ICALL w16_dcopen
    test byte [bx + W16W_F2], W16W2_DFACE
    jz .fc
    mov ax, [w16_dface]             ; a DS_SETFONT dialog's control hands its
    mov [w16_face], ax              ;   owner a DC in the dialog face, as
.fc:                                ;   Windows' had its font (124.22.17)
    mov ax, [w16_odrc]
    add ax, [w16_ox]
    cmp ax, [w16_cl]
    jle .l
    mov [w16_cl], ax
.l:
    mov ax, [w16_odrc + 2]
    add ax, [w16_oy]
    cmp ax, [w16_ct]
    jle .t
    mov [w16_ct], ax
.t:
    mov ax, [w16_odrc + 4]
    add ax, [w16_ox]
    dec ax
    cmp ax, [w16_cr]
    jge .r
    mov [w16_cr], ax
.r:
    mov ax, [w16_odrc + 6]
    add ax, [w16_oy]
    dec ax
    cmp ax, [w16_cb]
    jge .b
    mov [w16_cb], ax
.b:
    mov al, [w16_odctx]
    xor ah, ah
    mov si, w16_odback_c
    mov cx, DIS_SIZE
    ICALL w16_cbopen                ; ES:DI = the DRAWITEMSTRUCT, zeroed
    xor ax, ax
    mov al, [w16_odtype]
    mov [es:di], ax                 ; CtlType
    mov ax, [w16_odid]
    mov [es:di + 2], ax             ; CtlID
    mov ax, [w16_odi]
    mov [es:di + 4], ax             ; itemID
    mov al, [w16_odact]
    xor ah, ah
    mov [es:di + 6], ax             ; itemAction
    mov al, [w16_odst]
    mov [es:di + 8], ax             ; itemState
    mov ax, [w16_odhw]
    mov [es:di + 10], ax            ; hwndItem
    mov word [es:di + 12], W16_HDC  ; hDC
    mov ax, [w16_odrc]
    mov [es:di + 14], ax            ; rcItem
    mov ax, [w16_odrc + 2]
    mov [es:di + 16], ax
    mov ax, [w16_odrc + 4]
    mov [es:di + 18], ax
    mov ax, [w16_odrc + 6]
    mov [es:di + 20], ax
    mov ax, [w16_oddata]
    mov [es:di + 22], ax            ; itemData
    mov ax, [w16_oddata + 2]
    mov [es:di + 24], ax
    mov [w16_sm_lp], di
    mov [w16_sm_lp + 2], es
    mov ax, [w16_odown]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_DRAWITEM
    mov ax, [w16_odid]
    mov [w16_sm_wp], ax
    mov ax, [w16_odown]
    ICALL w16_hwnd2rec              ; BX = the owner
    ICALL w16_sendgo
    clc
    ret
.no:
    stc
    ret

; w16_odback - WM_DRAWITEM answered, CX = who asked: the DC closed, and the
; import that asked goes on - to the next owed item or the next message
LCONT w16_odback
    push cx
    ICALL w16_dcclose
    mov cx, [w16_sface]             ; the System font's again
    mov [w16_face], cx
    pop cx
    cmp cl, W16OD_PEEK
    je .peek
    cmp cl, W16OD_MODAL
    je .modal
    ICALL w16u_getmsg
    ret
.peek:
    ICALL w16u_peekmsg
    ret
.modal:
    call w16d_mrun                  ; w16d_mcont's, near
    cmp cl, 1
    je .done
    cmp cl, 2
    je .out
    mov byte [w16_pend], 1
.out:
    ret
.done:
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; w16d_owemi - BX = a dialog just made: the first of its owner-drawn
; controls whose WM_MEASUREITEM is owed sent it - a combo box twice, its
; field (itemID -1) then its items (0), as Windows 3.1's - and w16d_mic
; continues. CF=1 none is owed. Keeps BX.
; -----------------------------------------------------------------------------
w16d_owemi:
    push bx
    ICALL w16_rec2hwnd
    mov dx, ax                      ; DX = the dialog's HWND
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_F2], W16W2_OWEMI
    jz .n
    cmp [bx + W16W_PARENT], dx
    je .send
.n:
    add bx, W16W_SIZE
    loop .w
    pop bx
    stc
    ret
.send:
    mov [w16_mirec], bx
    mov word [w16_mii], 0           ; which item, and its height now
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    je .combo
    and byte [bx + W16W_F2], ~W16W2_OWEMI
    mov ax, [bx + LC_ITEMH]
    mov cl, ODT_LISTBOX
    jmp short .go
.combo:
    mov cl, ODT_COMBOBOX
    test byte [bx + CC_FL], CCF_MIF
    jz .items
    and byte [bx + CC_FL], ~CCF_MIF
    mov word [w16_mii], -1
    mov ax, [bx + CC_FH]
    jmp short .go
.items:
    and byte [bx + CC_FL], ~CCF_MIL
    and byte [bx + W16W_F2], ~W16W2_OWEMI
    push bx
    call w16c_list
    mov ax, [bx + LC_ITEMH]
    pop bx
.go:
    push si
    push di
    push es
    push ax
    push cx
    mov ax, dx                      ; the continuation's word: the dialog's
    sub ax, W16_HWND0               ;   slot
    shr ax, 1
    shr ax, 1
    mov si, w16d_mic_c
    mov cx, MIS_SIZE
    ICALL w16_cbopen                ; ES:DI = the MEASUREITEMSTRUCT, zeroed
    pop cx
    xor ch, ch
    mov [es:di], cx                 ; CtlType
    mov ax, [bx + W16W_MENU]
    mov [es:di + 2], ax             ; CtlID
    mov [w16_sm_wp], ax
    mov ax, [w16_mii]
    mov [es:di + 4], ax             ; itemID
    mov ax, [bx + W16W_CX]
    mov [es:di + 6], ax             ; itemWidth
    pop ax
    mov [es:di + 8], ax             ; itemHeight
    mov [w16_sm_lp], di
    mov [w16_sm_lp + 2], es
    mov [w16_sm_hwnd], dx
    mov word [w16_sm_msg], WM_MEASUREITEM
    mov ax, dx
    ICALL w16_hwnd2rec              ; BX = the dialog, whose procedure it is
    ICALL w16_sendgo
    pop es
    pop di
    pop si
    pop bx
    clc
    ret

; w16d_mic - WM_MEASUREITEM answered, CX = the dialog's slot: the height
; the program wrote taken, then the next owed (w16d_initsend). The struct is
; where it was, just above the stack the continuation left.
DCONT w16d_mic
    ICALL w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .gone
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    jne .gone
    push bx
    mov es, [w16_pss]
    mov di, [w16_psp_]
    sub di, MIS_SIZE
    mov ax, [es:di + 8]             ; itemHeight
    mov bx, [w16_mirec]
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .done
    or ax, ax
    jz .done
    cmp ax, 255
    ja .done
    cmp byte [bx + W16W_KIND], W16Q_COMBO
    je .combo
    mov [bx + LC_ITEMH], ax         ; a list box's items
    jmp short .done
.combo:
    cmp word [w16_mii], -1
    jne .items
    call w16c_fieldh                ; a combo box's field
    jmp short .done
.items:
    call w16c_list
    jc .done
    mov [bx + LC_ITEMH], ax
.done:
    pop bx
    jmp w16d_initsend
.gone:
    retf

section .text
