; =============================================================================
; os8088 - apps/win16/w16dlg.inc
;
; PART 3 OF WIN16.O88: THE DIALOG MANAGER AND THE CONTROLS (SPEC.md 124.13).
; Assembled here, in WIN16's own nasm run, at `vstart=0` of a section of its
; own, and cut off the image by tools/os88ovl.py - so every byte of WIN16's
; that this code names (the bss, the tables, the strings) is at the address
; WIN16 uses, by construction, and DS is WIN16's segment throughout.
;
; THE RULES OF THIS FILE:
;   - its code runs with CS = the part's segment, so a near call reaches the
;     part and a call into the image is ICALL; the image reaches it with
;     DCALL, and a routine DCALLed ends in retf;
;   - a constant it reads through DS lives in the image: write it in
;     `section .text`, and a table it reads in its own section needs `cs:`;
;   - a continuation (SPEC.md 124.2.2) is DCONT name, ends in retf, and is
;     handed to w16_cbopen as name_c, its trampoline in the image;
;   - the kernel is never handed an address in here (SPEC.md 124.13);
;   - no routine in here is on any stack while the program runs: every DCALL
;     returns before the gate enters the program. The part stays until the
;     program ends all the same (SPEC.md 124.19.2): it is the LIBRARY part
;     now, and a handler moved here is reached through w16_lbgate.
;
; After Wine's dialog16.c, dialog.c, defdlg.c, button.c, static.c, edit.c and
; listbox.c, cut to what Windows 3.0 showed (SPEC.md 124.13's tables) and
; drawn as its screenshots measure.
; =============================================================================

; --- styles and messages (Windows 3.0's windows.h) ----------------------------
BS_PUSHBUTTON    equ 0
BS_DEFPUSHBUTTON equ 1
BS_CHECKBOX      equ 2
BS_AUTOCHECKBOX  equ 3
BS_RADIOBUTTON   equ 4
BS_3STATE        equ 5
BS_AUTO3STATE    equ 6
BS_GROUPBOX      equ 7
BS_USERBUTTON    equ 8
BS_AUTORADIOBUTTON equ 9
BS_LEFTTEXT      equ 0x20

SS_LEFT          equ 0
SS_CENTER        equ 1
SS_RIGHT         equ 2
SS_ICON          equ 3
SS_BLACKRECT     equ 4
SS_GRAYRECT      equ 5
SS_WHITERECT     equ 6
SS_BLACKFRAME    equ 7
SS_GRAYFRAME     equ 8
SS_WHITEFRAME    equ 9
SS_SIMPLE        equ 0x0B
SS_LEFTNOWORDWRAP equ 0x0C
SS_NOPREFIX      equ 0x80

ES_UPPERCASE     equ 0x0008
ES_LOWERCASE     equ 0x0010
ES_PASSWORD      equ 0x0020

LBS_NOTIFY       equ 0x0001
LBS_SORT         equ 0x0002
LBS_NOINTEGRALHEIGHT equ 0x0100

DS_ABSALIGN      equ 0x01
DS_SETFONT       equ 0x40

BM_GETCHECK      equ 0x0400
BM_SETCHECK      equ 0x0401
BM_GETSTATE      equ 0x0402
BM_SETSTATE      equ 0x0403
BM_SETSTYLE      equ 0x0404
BN_CLICKED       equ 0

EM_GETSEL        equ 0x0400
EM_SETSEL        equ 0x0401
EM_GETMODIFY     equ 0x0408
EM_SETMODIFY     equ 0x0409
EM_LINELENGTH    equ 0x0411
EM_REPLACESEL    equ 0x0412
EM_GETLINE       equ 0x0414
EM_LIMITTEXT     equ 0x0415
EN_CHANGE        equ 0x0300

LB_ADDSTRING     equ 0x0401
LB_INSERTSTRING  equ 0x0402
LB_DELETESTRING  equ 0x0403
LB_RESETCONTENT  equ 0x0405
LB_SETCURSEL     equ 0x0407
LB_GETCURSEL     equ 0x0409
LB_GETTEXT       equ 0x040A
LB_GETTEXTLEN    equ 0x040B
LB_GETCOUNT      equ 0x040C
LB_SELECTSTRING  equ 0x040D
LB_GETTOPINDEX   equ 0x040F
LB_FINDSTRING    equ 0x0410
LB_SETTOPINDEX   equ 0x0418
LB_GETITEMDATA   equ 0x041A
LB_SETITEMDATA   equ 0x041B
LBN_SELCHANGE    equ 1
LBN_DBLCLK       equ 2

DM_GETDEFID      equ 0x0400
DM_SETDEFID      equ 0x0401

DLGC_WANTARROWS  equ 0x0001
DLGC_WANTTAB     equ 0x0002
DLGC_HASSETSEL   equ 0x0008
DLGC_DEFPUSHBUTTON equ 0x0010
DLGC_UNDEFPUSHBUTTON equ 0x0020
DLGC_RADIOBUTTON equ 0x0040
DLGC_WANTCHARS   equ 0x0080
DLGC_STATIC      equ 0x0100
DLGC_BUTTON      equ 0x2000

WM_CTLCOLOR      equ 0x0019
WM_QUERYDRAGICON equ 0x0037
WM_COMPAREITEM   equ 0x0039
WM_VKEYTOASCII   equ 0x002E
WM_CHARTOITEM    equ 0x002F
WM_LBUTTONDBLCLK equ 0x0203

VK_BACK     equ 0x08
VK_TAB      equ 0x09
VK_RETURN   equ 0x0D
VK_ESCAPE   equ 0x1B
VK_SPACE    equ 0x20
VK_PRIOR    equ 0x21
VK_NEXT     equ 0x22
VK_END      equ 0x23
VK_HOME     equ 0x24
VK_LEFT     equ 0x25
VK_UP       equ 0x26
VK_RIGHT    equ 0x27
VK_DOWN     equ 0x28
VK_INSERT   equ 0x2D
VK_DELETE   equ 0x2E
WM_CUT      equ 0x0300          ; an edit and the clipboard (SPEC.md 124.16.2)
WM_COPY     equ 0x0301
WM_PASTE    equ 0x0302
WM_CLEAR    equ 0x0303

; --- a control's style word, named for its class: BS_, ES_, SS_ and LBS_ give
; the same bits different meanings, as Windows' did, so each class reads them
; through its own name (tests/unit/t_bits.py keeps a family to one meaning)
BC_STYLE    equ W16W_STYLE
EC_STYLE    equ W16W_STYLE
SC_STYLE    equ W16W_STYLE
LC_STYLE    equ W16W_STYLE

; --- a control's own state, in its W16W_CS ------------------------------------
SC_ICON     equ W16W_CS + 0     ; STATIC: an SS_ICON's icon (SPEC.md 124.18.4)
BC_STATE    equ W16W_CS + 0     ; BUTTON: the check (0-2) and BST_*
BC_TRACK    equ W16W_CS + 1     ; ...1 = the mouse is pressing it
BST_PUSHED  equ 4
BST_FOCUS   equ 8

EC_CARET    equ W16W_CS + 0     ; EDIT: the caret, as a character index
EC_SELA     equ W16W_CS + 2     ; ...the selection's other end
EC_FIRST    equ W16W_CS + 4     ; ...the first character shown
EC_LIMIT    equ W16W_CS + 6     ; ...EM_LIMITTEXT's, at most 255
EC_FL       equ W16W_CS + 8     ; ...ECF_*
EC_DRAG     equ W16W_CS + 9     ; ...1 = a press is selecting
ECF_FOCUS   equ 1
ECF_MODIFY  equ 2
EC_BUFSZ    equ 256             ; its text block, made once and never moved

LC_TAB      equ W16W_CS + 0     ; LISTBOX: its item table, a USER heap block
LC_COUNT    equ W16W_CS + 2     ; ...the items
LC_CUR      equ W16W_CS + 4     ; ...the selection, -1 none
LC_TOP      equ W16W_CS + 6     ; ...the first shown
LC_CAP      equ W16W_CS + 8     ; ...the table's room, in items
LC_FL       equ W16W_CS + 10    ; ...LCF_*
LCF_FOCUS   equ 1
LCF_TRACK   equ 2               ; a press is selecting
LI_SIZE     equ 4               ; an item: its text (a USER heap block) and
                                ;   its data word

; =============================================================================
; The image's side (`section .text`)
; =============================================================================

; w16_dthunk - AX = an export's handler in the dialog part: fetch the part
; and run it there. Its answer is the routine's; 0 when the part could not
; be read (op_fetch has said why).
w16_dthunk:
    call w16_dlgin
    jc .no
    mov [w16_pfv], ax
    call far [w16_pfv]
    ret
.no:
    xor ax, ax
    xor dx, dx
    ret

%macro DTHUNK 1                 ; %1 is an export handler; %1_d is its body
%1:                             ;   in the part
    mov ax, %1_d
    jmp w16_dthunk
%endmacro

DTHUNK w16u_dlgbox
DTHUNK w16u_dlgboxp
DTHUNK w16u_dlgboxind
DTHUNK w16u_dlgboxindp
DTHUNK w16u_credlg
DTHUNK w16u_credlgp
DTHUNK w16u_credlgind
DTHUNK w16u_credlgindp
DTHUNK w16u_enddlg
DTHUNK w16u_isdlgmsg
DTHUNK w16u_getdlgitem
DTHUNK w16u_setdlgtext
DTHUNK w16u_getdlgtext
DTHUNK w16u_setdlgint
DTHUNK w16u_getdlgint
DTHUNK w16u_checkradio
DTHUNK w16u_checkdlg
DTHUNK w16u_isdlgcheck
DTHUNK w16u_senddlgmsg
DTHUNK w16u_mapdlgrect
DTHUNK w16u_nextgroup
DTHUNK w16u_nexttab
DTHUNK w16u_defdlgproc
DTHUNK w16u_msgbox

; MakeProcInstance (KERNEL.51) - args: +0 hInstance, +2 lpProc. The gate
; enters every callee with AX = DS = DGROUP (SPEC.md 124.2.2), which is what
; a real-mode instance thunk did, so the procedure is its own instance.
w16k_mkprocinst:
    call w16_args
    mov ax, [es:bx + 2]
    mov dx, [es:bx + 4]
    ret

; FreeProcInstance (KERNEL.52) - args: +0 lpProc. Nothing was made.
w16k_freeprocinst:
    ret

; GetDialogBaseUnits (USER.243) - DX:AX = the System font's units: 8 across,
; its rows down (SPEC.md 124.13)
w16u_dlgunits:
    mov ax, 8
    call w16_rowsn
    mov dx, ax
    mov ax, 8
    ret

; w16_rowsn - AX = the System font's rows. Keeps the rest.
w16_rowsn:
    mov ax, [w16_capn]
    sub ax, 3
    ret

; CallWindowProc (USER.122) - args: +0 lParam, +4 wParam, +6 msg, +8 hWnd,
; +10 lpPrevWndFunc: a window procedure called as Windows calls one. WIN16's
; own controls' answers at once (w16_ctlrow).
w16u_callwndproc:
    call w16_args
    call w16_smargs
    mov si, [es:bx + 10]
    mov di, [es:bx + 12]
    call w16_ctlstub
    cmp ax, si
    jne .cb
    cmp dx, di
    jne .cb
    mov ax, [w16_sm_hwnd]
    call w16_hwnd2rec
    jc .cb
    jmp w16_intproc
.cb:
    push si
    mov si, w16_cont_ret
    xor ax, ax
    xor cx, cx
    call w16_cbopen
    mov ax, [w16_sm_hwnd]
    call w16_ppush
    mov ax, [w16_sm_msg]
    call w16_ppush
    mov ax, [w16_sm_wp]
    call w16_ppush
    mov ax, [w16_sm_lp + 2]
    call w16_ppush
    mov ax, [w16_sm_lp]
    call w16_ppush
    pop ax
    mov dx, di
    jmp w16_cbcall

; -----------------------------------------------------------------------------
; w16_mkick - a callback's turn of the innermost modal dialog's loop
; (SPEC.md 124.13 step 3): if the program waits in its DialogBox, run the
; loop until the queue is empty, a call into the program is entered (and has
; come back to waiting), or the dialog has ended and DialogBox answers. The
; caller has set [w16_locked]. Clobbers AX, BX, CX, DX.
; -----------------------------------------------------------------------------
w16_mkick:
    cmp byte [w16_state], W16S_RUN
    jne .out
    cmp byte [w16_inprog], 0        ; a callback inside a run (a W_PAINT that
    jne .out                        ;   a resize sent at once) waits its turn
    mov cl, [w16_inmodal]
    xor ch, ch
    jcxz .out
    dec cx
    call w16_slot2rec
    mov ax, [bx + WD_WPSP]
    cmp ax, [w16_psp_]
    jne .out                        ; it waits somewhere else just now
    mov ax, [bx + WD_WIDX]
    mov [w16_idx], ax
    mov ax, [bx + WD_WNARGB]
    mov [w16_nargb], ax
    mov byte [w16_rflags], 0
    DCALL w16d_mstep                ; CL = 0 wait, 1 answered, 2 a call
    cmp cl, 1
    je .done
    cmp cl, 2
    jne .out
    call w16_resumecb
    ret
.done:
    xor dx, dx
    call w16_resume
.out:
    ret

; w16_dlgres - DX:AX = a dialog's name or MAKEINTRESOURCE in this program ->
; ES = its template's segment (the template at ES:0), BX = the resource
; entry it is (w16_dlgrel gives it back). CF=1 there is none.
RT_DIALOG equ 5
w16_dlgres:
    push ax
    push dx
    push si
    push di
    mov di, w16_rnamb
    call w16_rkey
    mov [w16_rnk], ax
    mov word [w16_rtk], 0x8000 | RT_DIALOG
    call w16_rfind
    or ax, ax
    jz .no
    mov bx, ax                      ; BX = the entry
    call w16_rload
    or ax, ax
    jz .no
    push bx
    call w16_gfind                  ; ES = the block's header
    pop bx
    jc .no
    cmp byte [w16_gdisc], 0
    jne .no
    mov ax, es
    inc ax
    mov es, ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

; w16_dlgrel - BX = w16_dlgres's entry: the template is not in use any more.
; Keeps every register.
w16_dlgrel:
    push es
    mov es, [w16_rtab]
    cmp word [es:bx + RN_USAGE], 0
    je .out
    dec word [es:bx + RN_USAGE]
.out:
    pop es
    ret

; -----------------------------------------------------------------------------
; w16_tbox - fill the box [w16_bxl]..[w16_bxb] (screen, inclusive) in
; [w16_gcol] around the text - [w16_tn] characters of w16_txt at [w16_tx],
; [w16_ty] in [w16_tcol] on [w16_tpap] - and draw the text, both cut to the
; box: every pixel once (PERFORMANCE.md rule 2). Keeps every register.
; -----------------------------------------------------------------------------
w16_tbox:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push word [w16_cl]              ; the clip, cut to the box
    push word [w16_ct]
    push word [w16_cr]
    push word [w16_cb]
    mov ax, [w16_bxl]
    cmp ax, [w16_cl]
    jle .l
    mov [w16_cl], ax
.l:
    mov ax, [w16_bxt]
    cmp ax, [w16_ct]
    jle .t
    mov [w16_ct], ax
.t:
    mov ax, [w16_bxr]
    cmp ax, [w16_cr]
    jge .r
    mov [w16_cr], ax
.r:
    mov ax, [w16_bxb]
    cmp ax, [w16_cb]
    jge .b
    mov [w16_cb], ax
.b:
    mov si, w16_txt                 ; the text's cell: CX..DX across,
    mov cx, [w16_tn]                ;   BP..DI down
    call w16_textw
    mov cx, [w16_tx]
    mov dx, cx
    add dx, ax
    dec dx
    mov bp, [w16_ty]
    call w16_rowsn
    mov di, ax
    add di, bp
    dec di
    mov ax, [w16_bxl]               ; above it
    mov bx, [w16_bxt]
    push cx
    push dx
    mov cx, [w16_bxr]
    mov dx, bp
    dec dx
    call w16_gfill
    mov bx, di                      ; below it
    inc bx
    mov dx, [w16_bxb]
    call w16_gfill
    pop dx
    pop cx
    push cx                         ; left of it
    push dx
    mov bx, bp
    mov dx, di
    dec cx
    call w16_gfill
    pop dx
    pop cx
    mov ax, dx                      ; right of it
    inc ax
    mov cx, [w16_bxr]
    mov dx, di
    call w16_gfill
    cmp word [w16_tn], 0
    je .out
    call w16_txtdraw
.out:
    pop word [w16_cb]
    pop word [w16_cr]
    pop word [w16_ct]
    pop word [w16_cl]
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_wakeme - a wake for WIN16's window: the modal loop's next turn
; (w16d_mrun). Keeps every register.
w16_wakeme:
    push bx
    mov bx, [w16_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; the strings the part shows, in DS
w16_s_ok:       db 'OK', 0
w16_s_cancel:   db 'Cancel', 0
w16_s_abort:    db '&Abort', 0
w16_s_retry:    db '&Retry', 0
w16_s_ignore:   db '&Ignore', 0
w16_s_yes:      db '&Yes', 0
w16_s_no:       db '&No', 0
w16_s_mbclass:  db 0

section .w16dlg vstart=0 align=1
; w16_lbgate - the library part's one way in for a NEAR routine (SPEC.md
; 124.19.2): the gate's dispatch and LCALL far-call it with [w16_lfn] naming
; the routine. CS = this part, DS = WIN16's image. Every register and the flags
; pass through both ways - w16_ibgate's mirror.
w16_lbgate:
    call [w16_lfn]
    retf

; align=1 is load-bearing (apps/word/word.asm's .modc): a pad between the
; image the header records and this section's start would ride into the part
; and move every label in it.

w16d_org:

; =============================================================================
; The controls (SPEC.md 124.13's table): one procedure, by kind
; =============================================================================

; w16d_ctlproc - a control's procedure: BX = its record, [w16_sm_*] the
; message (the HWND filled in) -> DX:AX. Keeps BX, SI, DI.
w16d_ctlproc:
    push si
    push di
    xor ax, ax
    xor dx, dx
    mov cx, [w16_sm_msg]
    cmp cx, WM_GETTEXT              ; what all four take alike
    je .gettext
    cmp cx, WM_GETTEXTLENGTH
    je .gettlen
    cmp cx, WM_ENABLE
    je .inval
    mov al, [bx + W16W_KIND]
    cmp al, W16Q_BUTTON
    je .button
    cmp al, W16Q_EDIT
    je .edit
    cmp al, W16Q_STATIC
    je .static
    cmp al, W16Q_LIST
    je .list
    jmp short .out
.button:
    call w16d_button
    jmp short .out
.edit:
    call w16d_edit
    jmp short .out
.static:
    call w16d_static
    jmp short .out
.list:
    call w16d_list
    jmp short .out
.gettext:
    mov cx, [w16_sm_wp]             ; WM_GETTEXT: wParam bytes at lParam
    mov di, [w16_sm_lp]
    mov dx, [w16_sm_lp + 2]
    ICALL w16_gettext
    xor dx, dx
    jmp short .out
.gettlen:
    ICALL w16_textptr
    mov ax, cx
    xor dx, dx
    jmp short .out
.inval:
    call w16d_inval
.out:
    pop di
    pop si
    retf

; --- what every control shares ------------------------------------------------

; w16d_inval - BX = a control: all of it invalid. Keeps every register.
w16d_inval:
    push ax
    push cx
    push dx
    push si
    xor ax, ax
    xor cx, cx
    mov dx, 0x7FFF
    mov si, dx
    mov byte [w16_erase], 0
    ICALL w16_inval
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16d_notify - BX = a control, AX = a notification: WM_COMMAND to its
; parent, POSTED (SPEC.md 124.13): wParam its ID, lParam it and the code.
; Keeps every register.
w16d_notify:
    push ax
    push cx
    mov [w16_sm_lp + 2], ax
    ICALL w16_rec2hwnd
    mov [w16_sm_lp], ax
    mov ax, [bx + W16W_PARENT]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_COMMAND
    mov ax, [bx + W16W_MENU]
    mov [w16_sm_wp], ax
    xor al, al
    ICALL w16_post
    pop cx
    pop ax
    ret

; w16d_popen - BX = a control: BeginPaint's DC on it, its invalid part made
; valid. Out [w16d_l]..[w16d_b] = its client on the screen (inclusive),
; [w16d_w]/[w16d_h] its size. CF=1 nothing of it can be drawn (the DC is
; closed again). Keeps every register.
w16d_popen:
    push ax
    push cx
    push dx
    push si
    mov [w16d_rec], bx
    mov al, 1
    ICALL w16_dcopen
    and byte [bx + W16W_FLAGS], ~(W16WF_INVAL | W16WF_ERASE)
    mov ax, [w16_cl]
    cmp ax, [w16_cr]
    jg .none
    mov ax, [w16_ct]
    cmp ax, [w16_cb]
    jg .none
    ICALL w16_csize                 ; CX/DX = the client's size
    mov [w16d_w], cx
    mov [w16d_h], dx
    mov ax, [w16_ox]
    mov [w16d_l], ax
    add ax, cx
    dec ax
    mov [w16d_r], ax
    mov ax, [w16_oy]
    mov [w16d_t], ax
    add ax, dx
    dec ax
    mov [w16d_b], ax
    clc
    jmp short .out
.none:
    ICALL w16_dcclose
    stc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16d_pclose - the DC w16d_popen opened, closed
w16d_pclose:
    ICALL w16_dcclose
    ret

; w16d_syscol - AL = a COLOR_* -> AL = the colour the controls draw it in:
; GetSysColor's, except that on a 1bpp display a button's face is white and
; its shadow black, as Windows' monochrome drivers drew a button - the face's
; light grey would be the 50% dither, under a caption it makes unreadable.
; AH = 0, as GetSysColor's. Keeps all but AX.
w16d_syscol:
    cmp byte [w16_mono], 0
    je .col
    cmp al, COLOR_BTNFACE
    jne .ns
    mov ax, CWHITE
    ret
.ns:
    cmp al, COLOR_BTNSHADOW
    jne .col
    mov ax, CBLACK
    ret
.col:
    ICALL w16_syscol
    ret

; w16d_fill - AX/BX/CX/DX (screen, inclusive) in the COLOR_* SI. Keeps
; every register.
w16d_fill:
    push ax
    mov ax, si
    call w16d_syscol
    mov [w16_gcol], al
    pop ax
    ICALL w16_gfill
    ret

; w16d_colors - AL = the text's COLOR_*, AH = its paper's -> [w16_tcol],
; [w16_tpap]. Keeps every register.
w16d_colors:
    push ax
    push ax
    call w16d_syscol
    mov [w16_tcol], al
    pop ax
    mov al, ah
    call w16d_syscol
    mov [w16_tpap], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; w16d_label - BX = a control: its text into [w16d_sbuf], an `&` taken out
; and the letter after it remembered ([w16d_ul], -1 none) unless AL = 0 or
; the control is SS_NOPREFIX. Out CX = the characters. Keeps the rest.
; -----------------------------------------------------------------------------
w16d_label:
    push ax
    push dx
    push si
    push di
    push es
    mov dl, al
    cmp byte [bx + W16W_KIND], W16Q_STATIC
    jne .pre
    test byte [bx + SC_STYLE], SS_NOPREFIX
    jz .pre
    xor dl, dl
.pre:
    mov word [w16d_ul], -1
    ICALL w16_textptr               ; ES:SI, CX = its length
    mov di, w16d_sbuf
    xor ax, ax                      ; AX = the characters out
    jcxz .done
.c:
    mov dh, [es:si]
    inc si
    or dl, dl
    jz .put
    cmp dh, '&'
    jne .put
    dec cx                          ; `&&` is one `&`; `&x` underlines x
    jz .done
    mov dh, [es:si]
    inc si
    cmp dh, '&'
    je .put
    mov [w16d_ul], ax
.put:
    mov [di], dh
    inc di
    inc ax
    cmp ax, W16D_SBUF - 1
    jae .done
    loop .c
.done:
    mov byte [di], 0
    mov cx, ax
    pop es
    pop di
    pop si
    pop dx
    pop ax
    ret

; w16d_settxt - [w16d_sbuf] + AX, CX characters (at most W16_TXTMAX), as the
; text w16_txtdraw draws at [w16_tx], [w16_ty]. Keeps every register.
w16d_settxt:
    push cx
    push si
    push di
    push es
    cmp cx, W16_TXTMAX
    jbe .n
    mov cx, W16_TXTMAX
.n:
    mov [w16_tn], cx
    mov si, w16d_sbuf
    add si, ax
    mov di, w16_txt
    push ds
    pop es
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    ret

; w16d_tw - AX = the width of CX characters of [w16d_sbuf] from SI. Keeps
; the rest.
w16d_tw:
    push si
    add si, w16d_sbuf
    ICALL w16_textw
    pop si
    ret

; w16d_uline - the underline of [w16d_ul], if it is in the line of CX
; characters from [w16d_sbuf] + SI drawn at [w16_tx], [w16_ty]: one row
; under the baseline, the letter's advance wide, in [w16_tcol]. Keeps every
; register.
w16d_uline:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [w16d_ul]
    or ax, ax
    js .out
    sub ax, si
    jb .out
    cmp ax, cx
    jae .out
    mov cx, ax                      ; the pen before it
    call w16d_tw
    mov bx, ax
    add bx, [w16_tx]
    add si, cx
    mov cx, 1
    call w16d_tw                    ; ...and its advance
    mov cx, bx
    add cx, ax
    dec cx
    mov ax, bx
    call w16d_ulrow                ; BX = the row
    mov dx, bx
    mov al, [w16_tcol]
    mov [w16_gcol], al
    mov ax, [w16d_ulx]
    ICALL w16_gfill
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16d_ulrow - AX = the underline's left -> [w16d_ulx] = AX, BX = its row:
; two below the baseline on VGA and EGA (rows - 2), one on CGA
w16d_ulrow:
    mov [w16d_ulx], ax
    push ax
    ICALL w16_rowsn
    mov bx, ax
    sub bx, 2
    cmp ax, 8
    ja .r
    inc bx
.r:
    add bx, [w16_ty]
    pop ax
    ret

; w16d_focus - a dotted rectangle round AX/BX/CX/DX (screen, inclusive),
; Windows' focus rectangle: every other pixel in [w16_tcol]. Keeps every
; register.
w16d_focus:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, ax
    mov di, bx
    mov al, [w16_tcol]
    mov [w16_gcol], al
    mov ax, si                      ; the top and the bottom
.h:
    push cx
    mov cx, ax
    mov bx, di
    push dx
    mov dx, bx
    ICALL w16_gfill
    pop dx
    mov bx, dx
    push dx
    mov dx, bx
    ICALL w16_gfill
    pop dx
    pop cx
    add ax, 2
    cmp ax, cx
    jle .h
    mov bx, di                      ; the sides
    add bx, 2
.v:
    cmp bx, dx
    jge .out
    push cx
    push dx
    mov ax, si
    mov cx, si
    mov dx, bx
    ICALL w16_gfill
    pop dx
    pop cx
    push dx
    mov ax, cx
    mov dx, bx
    ICALL w16_gfill
    pop dx
    add bx, 2
    jmp short .v
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16d_grey - AL = the COLOR_* a control's text is in: COLOR_GRAYTEXT if it
; is disabled, else AL. Keeps the rest.
COLOR_WINDOWTEXT equ 8
COLOR_HIGHLIGHT  equ 13
COLOR_HIGHLIGHTTEXT equ 14
COLOR_BTNSHADOW  equ 16
COLOR_GRAYTEXT   equ 17
COLOR_BTNTEXT    equ 18
COLOR_WINDOWFRAME equ 6
COLOR_BACKGROUND equ 1
COLOR_SCROLLBAR  equ 0
w16d_grey:
    ICALL w16_disabled
    jnc .out
    mov al, COLOR_GRAYTEXT
.out:
    ret

; =============================================================================
; STATIC (after Wine's static.c)
; =============================================================================
w16d_static:
    mov cx, [w16_sm_msg]
    cmp cx, WM_PAINT
    je w16d_stpaint
    cmp cx, WM_SETTEXT
    je w16d_settext
    cmp cx, WM_GETDLGCODE
    jne .z
    mov ax, DLGC_STATIC
    ret
.z:
    xor ax, ax
    ret

; w16d_settext - WM_SETTEXT for a control that keeps its text as it is
; given: lParam the string; all of it is invalid. Answers 1.
w16d_settext:
    push si
    mov si, [w16_sm_lp]
    mov dx, [w16_sm_lp + 2]
    ICALL w16_settext
    pop si
    call w16d_inval
    mov ax, 1
    xor dx, dx
    ret

w16d_stpaint:
    call w16d_popen
    jnc .go
    ret
.go:
    mov al, [bx + W16W_STYLE]
    and al, 0x0F
    cmp al, SS_ICON
    je .icon
    cmp al, SS_BLACKRECT
    jb .text
    cmp al, SS_WHITEFRAME
    ja .text
    sub al, SS_BLACKRECT            ; the rectangles and frames
    xor ah, ah
    mov si, ax
    mov cl, 3
    div cl                          ; AL = rect 0 / frame 1, AH = which colour
    push ax
    mov al, ah
    xor ah, ah
    mov si, ax
    mov al, [cs:w16d_strect + si]
    mov si, ax                      ; SI = the COLOR_*
    pop ax
    mov ax, [w16d_l]
    mov bx, [w16d_t]
    mov cx, [w16d_r]
    mov dx, [w16d_b]
    or al, al
    jnz .frame
    call w16d_fill
    jmp .done
.frame:
    push dx                         ; a frame: its four sides
    mov dx, bx
    call w16d_fill                  ; the top
    pop dx
    push bx
    mov bx, dx
    call w16d_fill                  ; the bottom
    pop bx
    push cx
    mov cx, ax
    call w16d_fill                  ; the left
    pop cx
    mov ax, cx
    call w16d_fill                  ; the right
    jmp .done
.icon:
    mov al, COLOR_WINDOW            ; SS_ICON (SPEC.md 124.18.4): the icon,
    call w16d_syscol                ;   its see-through pixels in the
    push bx                         ;   window's colour, one pass
    push si
    mov cx, [w16d_l]
    mov dx, [w16d_t]
    mov si, [bx + SC_ICON]
    mov bl, al
    mov ax, si
    call w16_icodraw
    pop si
    pop bx
    jmp .done
.text:
    mov [w16d_sty], al
    mov al, 1
    call w16d_label                 ; [w16d_sbuf], CX characters
    mov [w16d_n], cx
    mov al, COLOR_WINDOWTEXT
    call w16d_grey
    mov ah, COLOR_WINDOW
    call w16d_colors
    mov ax, COLOR_WINDOW
    call w16d_syscol
    mov [w16_gcol], al
    mov ax, [w16d_t]
    mov [w16_ty], ax
    xor si, si                      ; SI = the next line's first character
.line:
    mov ax, [w16_ty]
    cmp ax, [w16d_b]
    jg .done
    mov cx, [w16d_n]
    sub cx, si                      ; CX = what is left
    mov dx, [w16d_w]
    cmp byte [w16d_sty], SS_SIMPLE
    jae .nowrap
    call w16d_wrap                  ; CX = this line's, AX = its next start
    jmp short .have
.nowrap:
    call w16d_eol
.have:
    push ax
    push cx
    call w16d_tw                    ; AX = its width
    mov dx, [w16d_l]
    cmp byte [w16d_sty], SS_CENTER
    jne .nc
    mov dx, [w16d_w]
    sub dx, ax
    sar dx, 1
    add dx, [w16d_l]
.nc:
    cmp byte [w16d_sty], SS_RIGHT
    jne .nr
    mov dx, [w16d_r]
    sub dx, ax
    inc dx
.nr:
    mov [w16_tx], dx
    mov ax, si
    call w16d_settxt
    mov ax, [w16d_l]
    mov [w16_bxl], ax
    mov ax, [w16d_r]
    mov [w16_bxr], ax
    mov ax, [w16_ty]
    mov [w16_bxt], ax
    push ax
    ICALL w16_rowsn
    mov dx, ax
    pop ax
    add ax, dx
    dec ax
    cmp ax, [w16d_b]
    jle .bb
    mov ax, [w16d_b]
.bb:
    mov [w16_bxb], ax
    ICALL w16_tbox
    call w16d_uline
    pop cx
    pop si                          ; the next line
    ICALL w16_rowsn
    add [w16_ty], ax
    cmp si, [w16d_n]
    jb .line
    mov ax, [w16_ty]                ; below the last line
    cmp ax, [w16d_b]
    jg .done
    mov bx, ax
    mov ax, [w16d_l]
    mov cx, [w16d_r]
    mov dx, [w16d_b]
    mov si, COLOR_WINDOW
    call w16d_fill
.done:
    call w16d_pclose
    xor ax, ax
    xor dx, dx
    ret

w16d_strect:                    ; SS_BLACK*, SS_GRAY*, SS_WHITE*: their colour
    db COLOR_WINDOWFRAME, COLOR_BACKGROUND, COLOR_WINDOW

; -----------------------------------------------------------------------------
; w16d_wrap - the line of [w16d_sbuf] from SI, CX characters left, that fits
; DX pixels: broken after the last space that fits, or where it stops
; fitting if there is none, and at a line break. Out CX = its characters,
; AX = where the next begins (past the break, or the spaces it broke at).
; Keeps the rest.
; -----------------------------------------------------------------------------
w16d_wrap:
    push bx
    push di
    push bp
    mov bp, si
    add bp, cx                      ; BP = the end of the text
    mov bx, si                      ; BX = the end so far
    xor di, di                      ; DI = past the last space that fits
.c:
    cmp bx, bp
    jae .all
    mov al, [w16d_sbuf + bx]
    cmp al, 13
    je .brk
    cmp al, 10
    je .brk
    push cx
    mov cx, bx
    sub cx, si
    inc cx
    call w16d_tw                    ; AX = the line's width with it
    pop cx
    cmp ax, dx
    ja .over
    cmp byte [w16d_sbuf + bx], ' '
    jne .nsp
    mov di, bx
    inc di
.nsp:
    inc bx
    jmp short .c
.all:
    mov cx, bx
    sub cx, si
    mov ax, bx
    jmp short .out
.brk:
    mov cx, bx                      ; a line break ends it, and CR LF is one
    sub cx, si
    mov ax, bx
    inc ax
    cmp byte [w16d_sbuf + bx], 13
    jne .out
    cmp ax, bp
    jae .out
    mov bx, ax
    cmp byte [w16d_sbuf + bx], 10
    jne .out
    inc ax
    jmp short .out
.over:
    or di, di
    jz .cut
    mov cx, di                      ; at the space, which is not drawn
    dec cx
    sub cx, si
    mov ax, di
.sp:
    cmp ax, bp                      ; ...and the spaces after it skipped
    jae .out
    mov bx, ax
    cmp byte [w16d_sbuf + bx], ' '
    jne .out
    inc ax
    jmp short .sp
.cut:
    cmp bx, si
    jne .mid
    inc bx                          ; one character too wide is a line anyway
.mid:
    mov cx, bx
    sub cx, si
    mov ax, bx
.out:
    pop bp
    pop di
    pop bx
    ret

; w16d_eol - SS_SIMPLE's line: up to a break, CX characters left from SI ->
; CX = the line's, AX = the next's start
w16d_eol:
    push bx
    mov bx, si
.c:
    mov ax, bx
    sub ax, si
    cmp ax, cx
    jae .end
    cmp byte [w16d_sbuf + bx], 13
    je .end
    cmp byte [w16d_sbuf + bx], 10
    je .end
    inc bx
    jmp short .c
.end:
    mov cx, bx
    sub cx, si
    mov ax, bx
    cmp ax, [w16d_n]
    jae .out
    inc ax
.out:
    pop bx
    ret

; =============================================================================
; BUTTON (after Wine's button.c): push, default push, check box, radio
; button, 3-state and group box, drawn as Windows 3.0 drew them on VGA
; =============================================================================

; w16d_bstyle - BX = a button -> AL = its BS_* (the low nibble). Keeps the rest.
w16d_bstyle:
    mov al, [bx + W16W_STYLE]
    and al, 0x0F
    ret

w16d_button:
    mov cx, [w16_sm_msg]
    cmp cx, WM_PAINT
    jne .np
    jmp w16d_bpaint
.np:
    cmp cx, WM_SETTEXT
    jne .nst
    jmp w16d_settext
.nst:
    cmp cx, WM_GETDLGCODE
    je .code
    cmp cx, BM_GETCHECK
    je .getcheck
    cmp cx, BM_SETCHECK
    je .setcheck
    cmp cx, BM_GETSTATE
    je .getstate
    cmp cx, BM_SETSTATE
    je .setstate
    cmp cx, BM_SETSTYLE
    je .setstyle
    cmp cx, WM_SETFOCUS
    je .setfocus
    cmp cx, WM_KILLFOCUS
    je .killfocus
    cmp cx, WM_LBUTTONDOWN
    je .down
    cmp cx, WM_MOUSEMOVE
    je .move
    cmp cx, WM_LBUTTONUP
    je .up
    cmp cx, WM_KEYDOWN
    je .key
    cmp cx, WM_KEYUP
    je .keyup
    jmp .zero
.code:
    call w16d_bstyle
    mov dx, DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON
    cmp al, BS_PUSHBUTTON
    je .c
    mov dx, DLGC_BUTTON | DLGC_DEFPUSHBUTTON
    cmp al, BS_DEFPUSHBUTTON
    je .c
    mov dx, DLGC_STATIC
    cmp al, BS_GROUPBOX
    je .c
    mov dx, DLGC_BUTTON | DLGC_RADIOBUTTON
    cmp al, BS_RADIOBUTTON
    je .c
    cmp al, BS_AUTORADIOBUTTON
    je .c
    mov dx, DLGC_BUTTON
.c:
    mov ax, dx
    xor dx, dx
    ret
.getcheck:
    mov al, [bx + BC_STATE]
    and ax, 3
    xor dx, dx
    ret
.setcheck:
    call w16d_bstyle                ; a push button and a group box have none
    cmp al, BS_DEFPUSHBUTTON
    jbe .zero
    cmp al, BS_GROUPBOX
    je .zero
    mov al, [w16_sm_wp]
    and al, 3
    mov ah, [bx + BC_STATE]
    and ah, ~3
    or al, ah
    cmp al, [bx + BC_STATE]
    je .zero
    mov [bx + BC_STATE], al
    call w16d_inval
    jmp .zero
.getstate:
    mov al, [bx + BC_STATE]
    xor ah, ah
    xor dx, dx
    ret
.setstate:
    mov al, [bx + BC_STATE]
    and al, ~BST_PUSHED
    cmp word [w16_sm_wp], 0
    je .ss
    or al, BST_PUSHED
.ss:
    cmp al, [bx + BC_STATE]
    je .zero
    mov [bx + BC_STATE], al
    call w16d_inval
    jmp .zero
.setstyle:
    mov al, [w16_sm_wp]             ; BM_SETSTYLE: the new BS_*, redrawn
    mov [bx + W16W_STYLE], al
    call w16d_inval
    jmp .zero
.setfocus:
    or byte [bx + BC_STATE], BST_FOCUS
    call w16d_inval
    jmp .zero
.killfocus:
    and byte [bx + BC_STATE], ~(BST_FOCUS | BST_PUSHED)
    mov byte [bx + BC_TRACK], 0
    call w16d_inval
    jmp .zero
.down:
    call w16d_bstyle
    cmp al, BS_GROUPBOX
    je .zero
    ICALL w16_rec2hwnd              ; the focus, and the press tracked until
    push bx                         ;   the release
    ICALL w16_setfocus
    pop bx
    ICALL w16_rec2hwnd
    mov [w16_capture], ax
    mov byte [bx + BC_TRACK], 1
    or byte [bx + BC_STATE], BST_PUSHED
    call w16d_inval
    jmp .zero
.move:
    cmp byte [bx + BC_TRACK], 0
    je .zero
    call w16d_inside                ; pushed while the pointer is on it
    mov al, [bx + BC_STATE]
    and al, ~BST_PUSHED
    jnc .mv
    or al, BST_PUSHED
.mv:
    cmp al, [bx + BC_STATE]
    je .zero
    mov [bx + BC_STATE], al
    call w16d_inval
    jmp .zero
.up:
    cmp byte [bx + BC_TRACK], 0
    je .zero
    mov byte [bx + BC_TRACK], 0
    ICALL w16_rec2hwnd
    cmp [w16_capture], ax
    jne .nocap
    mov word [w16_capture], 0
.nocap:
    test byte [bx + BC_STATE], BST_PUSHED
    jz .zero
    and byte [bx + BC_STATE], ~BST_PUSHED
    call w16d_inval
    call w16d_click                 ; a release on it clicks it
    jmp .zero
.key:
    cmp word [w16_sm_wp], VK_SPACE
    jne .zero
    or byte [bx + BC_STATE], BST_PUSHED
    call w16d_inval
    jmp .zero
.keyup:
    cmp word [w16_sm_wp], VK_SPACE
    jne .zero
    test byte [bx + BC_STATE], BST_PUSHED
    jz .zero
    and byte [bx + BC_STATE], ~BST_PUSHED
    call w16d_inval
    call w16d_click
.zero:
    xor ax, ax
    xor dx, dx
    ret

; w16d_inside - BX = a control, [w16_sm_lp] a client point: CF=1 on it.
; Keeps every register.
w16d_inside:
    push ax
    push cx
    push dx
    ICALL w16_csize
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
    pop dx
    pop cx
    pop ax
    stc
    ret
.no:
    pop dx
    pop cx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; w16d_click - BX = a button: what a click does (BM_CLICK's): an automatic
; check box or 3-state turns, an automatic radio button is checked and the
; others of its group are not, and the parent is told BN_CLICKED. Keeps
; every register.
; -----------------------------------------------------------------------------
w16d_click:
    push ax
    push cx
    call w16d_bstyle
    cmp al, BS_AUTOCHECKBOX
    je .check
    cmp al, BS_AUTO3STATE
    je .three
    cmp al, BS_AUTORADIOBUTTON
    je .radio
    jmp short .tell
.check:
    xor byte [bx + BC_STATE], 1
    and byte [bx + BC_STATE], ~2
    call w16d_inval
    jmp short .tell
.three:
    mov al, [bx + BC_STATE]
    mov ah, al
    and ah, 3
    inc ah
    cmp ah, 3
    jb .t
    xor ah, ah
.t:
    and al, ~3
    or al, ah
    mov [bx + BC_STATE], al
    call w16d_inval
    jmp short .tell
.radio:
    call w16d_radioset
.tell:
    mov ax, BN_CLICKED
    call w16d_notify
    pop cx
    pop ax
    ret

; w16d_radioset - BX = a radio button: it is checked, and every other
; automatic radio button of its group is not (Wine's BUTTON_CheckAutoRadio).
; Keeps every register.
w16d_radioset:
    push ax
    push si
    push di
    ICALL w16_rec2hwnd
    mov si, ax                      ; SI = this one
    push bx
    call w16d_grpfirst              ; BX = its group's first
.g:
    ICALL w16_rec2hwnd
    cmp ax, si
    je .me
    cmp byte [bx + W16W_KIND], W16Q_BUTTON
    jne .n
    call w16d_bstyle
    cmp al, BS_AUTORADIOBUTTON
    jne .n
    test byte [bx + BC_STATE], 3
    jz .n
    and byte [bx + BC_STATE], ~3
    call w16d_inval
    jmp short .n
.me:
    test byte [bx + BC_STATE], 1
    jnz .n
    or byte [bx + BC_STATE], 1
    call w16d_inval
.n:
    call w16d_sibnext               ; the next of the group, CF=1 its end
    jnc .g
    pop bx
    pop di
    pop si
    pop ax
    ret

; --- a button's picture --------------------------------------------------------

w16d_bpaint:
    call w16d_popen
    jnc .go
    ret
.go:
    call w16d_bstyle
    cmp al, BS_DEFPUSHBUTTON
    ja .notpush
    jmp w16d_pushpaint
.notpush:
    cmp al, BS_GROUPBOX
    jne .check
    jmp w16d_grppaint
.check:
    jmp w16d_chkpaint

; w16d_bevel - AX = 2 on a display whose font has more than 8 rows, else 1:
; Windows 3.0's push-button edges. Keeps the rest.
w16d_bevel:
    push bx
    ICALL w16_rowsn
    mov bx, ax
    mov ax, 2
    cmp bx, 8
    ja .out
    dec ax
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16d_pushpaint - a push button: a black frame with its corners off (two
; rows for the default button), white edges up and left and grey edges down
; and right with their corners cut on the diagonal (a pushed one: grey up and
; left), the face, and the caption centred on it with the focus's dotted
; rectangle round the caption (measured off Windows 3.0's VGA)
; -----------------------------------------------------------------------------
w16d_pushpaint:
    mov al, COLOR_BTNTEXT
    call w16d_grey
    mov ah, COLOR_BTNFACE
    call w16d_colors
    mov ax, [w16d_l]                ; the four corners are the dialog's
    mov bx, [w16d_t]
    mov cx, ax
    mov dx, bx
    mov si, COLOR_WINDOW
    call w16d_fill
    mov ax, [w16d_r]
    mov cx, ax
    call w16d_fill
    mov bx, [w16d_b]
    mov dx, bx
    call w16d_fill
    mov ax, [w16d_l]
    mov cx, ax
    call w16d_fill
    mov di, 1                       ; DI = the frame's rows
    mov bx, [w16d_rec]
    call w16d_bstyle
    cmp al, BS_DEFPUSHBUTTON
    jne .thin
    inc di
.thin:
    mov si, COLOR_WINDOWFRAME       ; the frame: top, bottom, left, right
    mov ax, [w16d_l]
    inc ax
    mov bx, [w16d_t]
    mov cx, [w16d_r]
    dec cx
    mov dx, bx
    add dx, di
    dec dx
    call w16d_fill
    mov dx, [w16d_b]
    mov bx, dx
    sub bx, di
    inc bx
    call w16d_fill
    mov ax, [w16d_l]
    mov bx, [w16d_t]
    inc bx
    mov cx, ax
    add cx, di
    dec cx
    mov dx, [w16d_b]
    dec dx
    call w16d_fill
    mov cx, [w16d_r]
    mov ax, cx
    sub ax, di
    inc ax
    call w16d_fill
    mov ax, [w16d_l]                ; [w16d_il]..: inside the frame
    add ax, di
    mov [w16d_il], ax
    mov ax, [w16d_t]
    add ax, di
    mov [w16d_it], ax
    mov ax, [w16d_r]
    sub ax, di
    mov [w16d_ir], ax
    mov ax, [w16d_b]
    sub ax, di
    mov [w16d_ib], ax
    mov bx, [w16d_rec]
    test byte [bx + BC_STATE], BST_PUSHED
    jz .up
    mov si, COLOR_BTNSHADOW         ; pushed: grey up and left, one row
    mov ax, [w16d_il]
    mov bx, [w16d_it]
    mov cx, [w16d_ir]
    mov dx, bx
    call w16d_fill
    inc bx
    mov cx, ax
    mov dx, [w16d_ib]
    call w16d_fill
    inc word [w16d_il]
    inc word [w16d_it]
    mov word [w16d_sh], 2           ; the caption moves down and right
    jmp .face
.up:
    mov word [w16d_sh], 0
    call w16d_bevel
    mov di, ax                      ; DI = the bevel's width
    mov si, COLOR_WINDOW            ; white: the top rows, each a pixel
    mov ax, [w16d_il]               ;   shorter at the right
    mov bx, [w16d_it]
    mov cx, [w16d_ir]
    push di
.wt:
    dec cx
    mov dx, bx
    call w16d_fill
    inc bx
    dec di
    jnz .wt
    pop di
    mov cx, ax                      ; ...the left columns, each a pixel
    add cx, di                      ;   shorter at the bottom
    dec cx
    mov dx, [w16d_ib]
    push di
.wl:
    dec dx
    push cx
    mov cx, ax
    call w16d_fill
    pop cx
    inc ax
    dec di
    jnz .wl
    pop di
    mov si, COLOR_BTNSHADOW         ; grey: the right columns, from the top
    mov cx, [w16d_ir]               ;   less a pixel each
    mov bx, [w16d_it]
    mov dx, [w16d_ib]
    push di
.gr:
    mov ax, cx
    call w16d_fill
    dec cx
    inc bx
    dec di
    jnz .gr
    pop di
    mov bx, [w16d_ib]               ; ...the bottom rows, from the left less a
    mov ax, [w16d_il]               ;   pixel each
    mov cx, [w16d_ir]
    sub cx, di
    push di
.gb:
    mov dx, bx
    call w16d_fill
    dec bx
    inc ax
    dec di
    jnz .gb
    pop di
    add [w16d_il], di               ; the face is inside the bevel
    add [w16d_it], di
    sub [w16d_ir], di
    sub [w16d_ib], di
.face:
    mov bx, [w16d_rec]
    mov al, 1
    call w16d_label                 ; CX characters in [w16d_sbuf]
    xor ax, ax
    call w16d_settxt
    xor si, si
    call w16d_tw                    ; AX = the caption's width
    mov dx, [w16d_w]
    sub dx, ax
    sar dx, 1
    add dx, [w16d_l]
    add dx, [w16d_sh]
    mov [w16_tx], dx
    push ax
    ICALL w16_rowsn
    mov dx, [w16d_h]
    sub dx, ax
    sar dx, 1
    add dx, [w16d_t]
    add dx, [w16d_sh]
    mov [w16_ty], dx
    mov ax, COLOR_BTNFACE
    call w16d_syscol
    mov [w16_gcol], al
    mov ax, [w16d_il]
    mov [w16_bxl], ax
    mov ax, [w16d_it]
    mov [w16_bxt], ax
    mov ax, [w16d_ir]
    mov [w16_bxr], ax
    mov ax, [w16d_ib]
    mov [w16_bxb], ax
    ICALL w16_tbox
    xor si, si
    call w16d_uline
    pop ax                          ; the caption's width
    mov bx, [w16d_rec]
    test byte [bx + BC_STATE], BST_FOCUS
    jz .done
    or ax, ax
    jz .done
    mov cx, ax                      ; the focus: round the caption
    mov ax, [w16_tx]
    dec ax
    add cx, ax
    inc cx
    mov bx, [w16_ty]
    push ax
    ICALL w16_rowsn
    mov dx, ax
    pop ax
    add dx, bx
    call w16d_focus
.done:
    call w16d_pclose
    xor ax, ax
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; w16d_grppaint - a group box: a frame whose top runs through the middle of
; the caption's row, and the caption on white over it, eight in
; -----------------------------------------------------------------------------
w16d_grppaint:
    mov al, COLOR_WINDOWTEXT
    call w16d_grey
    mov ah, COLOR_WINDOW
    call w16d_colors
    mov bx, [w16d_rec]
    mov al, 1
    call w16d_label
    mov [w16d_n], cx
    ICALL w16_rowsn
    mov di, ax                      ; DI = the rows
    shr ax, 1
    add ax, [w16d_t]
    mov [w16d_it], ax               ; the frame's top row
    mov si, COLOR_WINDOWFRAME
    mov ax, [w16d_l]                ; the left and the right
    mov bx, [w16d_it]
    mov cx, ax
    mov dx, [w16d_b]
    call w16d_fill
    mov ax, [w16d_r]
    mov cx, ax
    call w16d_fill
    mov ax, [w16d_l]                ; the bottom
    mov bx, [w16d_b]
    mov cx, [w16d_r]
    mov dx, bx
    call w16d_fill
    mov cx, [w16d_n]
    xor si, si
    call w16d_tw                    ; AX = the caption's width
    mov dx, ax
    mov ax, [w16d_l]                ; the top: left of the caption ...
    mov bx, [w16d_it]
    mov cx, ax
    add cx, 7
    or dx, dx
    jnz .cap
    mov cx, [w16d_r]                ; (no caption: all of it)
.cap:
    push dx
    mov dx, bx
    mov si, COLOR_WINDOWFRAME
    call w16d_fill
    pop dx
    or dx, dx
    jz .done
    mov ax, [w16d_l]                ; ...and right of it
    add ax, 8
    add ax, dx
    mov cx, [w16d_r]
    mov dx, bx
    call w16d_fill
    mov ax, [w16d_l]                ; the caption, a fifth of its rows down
    add ax, 8
    mov [w16_tx], ax
    mov ax, di
    mov cl, 5
    div cl
    xor ah, ah
    add ax, [w16d_t]
    mov [w16_ty], ax
    mov cx, [w16d_n]
    xor ax, ax
    call w16d_settxt
    ICALL w16_txtdraw
    xor si, si
    call w16d_uline
.done:
    call w16d_pclose
    xor ax, ax
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; w16d_chkpaint - a check box, a radio button or a 3-state: the box (13 by
; 13 on VGA, on the line's middle), and the caption 18 to the right of its
; left, on white; BS_LEFTTEXT puts the box at the right
; -----------------------------------------------------------------------------
w16d_chkpaint:
    mov al, COLOR_WINDOWTEXT
    call w16d_grey
    mov ah, COLOR_WINDOW
    call w16d_colors
    mov bx, [w16d_rec]
    call w16d_bstyle
    mov si, w16d_bmcheck            ; which picture: SI in the part, as an  XSEC-OK: read through cs:
    cmp al, BS_RADIOBUTTON          ;   offset of w16d_bm*
    je .radio
    cmp al, BS_AUTORADIOBUTTON
    jne .pic
.radio:
    mov si, w16d_bmradio  ; XSEC-OK: read through cs:
.pic:
    mov al, [bx + BC_STATE]
    and al, 3
    jz .shape
    add si, 13 * 2                  ; checked: the next picture
    cmp al, 2
    jne .shape
    add si, 13 * 2                  ; 3-state's grey
.shape:
    mov [w16d_pic], si
    mov ax, 13
    ICALL w16_rowsn
    cmp ax, 16                      ; the box's rows: 13 on VGA, fewer on a
    jae .bh                         ;   smaller font, never under 7
    sub ax, 3
    cmp ax, 7
    jae .bh
    mov ax, 7
.bh:
    cmp ax, 13
    jbe .bh2
    mov ax, 13
.bh2:
    mov [w16d_bh], ax
    mov dx, [w16d_h]                ; the box: on the middle row
    sub dx, ax
    sar dx, 1
    add dx, [w16d_t]
    mov [w16d_by], dx
    mov ax, [w16d_l]
    test byte [bx + BC_STYLE], BS_LEFTTEXT
    jz .bx
    mov ax, [w16d_r]
    sub ax, 12
.bx:
    mov [w16d_bxx], ax
    mov si, COLOR_WINDOW            ; the white round the box: above, below
    mov ax, [w16d_bxx]
    mov bx, [w16d_t]
    mov cx, ax
    add cx, 12
    mov dx, [w16d_by]
    dec dx
    call w16d_fill
    mov bx, [w16d_by]
    add bx, [w16d_bh]
    mov dx, [w16d_b]
    call w16d_fill
    mov si, [w16d_pic]              ; the box, from the part's picture
    call w16d_pic13
    mov bx, [w16d_rec]              ; the caption
    mov al, 1
    call w16d_label
    xor ax, ax
    call w16d_settxt
    mov ax, [w16d_l]
    add ax, 18
    mov [w16_bxl], ax
    mov dx, [w16d_r]
    mov [w16_bxr], dx
    test byte [bx + BC_STYLE], BS_LEFTTEXT
    jz .tx
    mov ax, [w16d_l]
    mov [w16_bxl], ax
    mov dx, [w16d_bxx]
    sub dx, 5
    mov [w16_bxr], dx
.tx:
    mov [w16_tx], ax
    ICALL w16_rowsn
    mov dx, [w16d_h]
    sub dx, ax
    sar dx, 1
    inc dx
    add dx, [w16d_t]
    mov [w16_ty], dx
    mov ax, [w16d_t]
    mov [w16_bxt], ax
    mov ax, [w16d_b]
    mov [w16_bxb], ax
    mov ax, COLOR_WINDOW
    call w16d_syscol
    mov [w16_gcol], al
    ICALL w16_tbox
    mov ax, [w16_bxl]               ; the gap between the box and the caption
    dec ax
    mov cx, ax
    mov ax, [w16d_bxx]
    add ax, 13
    test byte [bx + BC_STYLE], BS_LEFTTEXT
    jz .gap
    mov ax, [w16_bxr]
    inc ax
    mov cx, [w16d_bxx]
    dec cx
.gap:
    cmp ax, cx
    jg .ul
    mov bx, [w16d_t]
    mov dx, [w16d_b]
    mov si, COLOR_WINDOW
    call w16d_fill
.ul:
    mov bx, [w16d_rec]
    mov cx, [w16_tn]
    xor si, si
    call w16d_uline
    test byte [bx + BC_STATE], BST_FOCUS
    jz .done
    mov cx, [w16_tn]                ; the focus: round the caption
    jcxz .done
    xor si, si
    call w16d_tw
    mov cx, ax
    mov ax, [w16_tx]
    dec ax
    add cx, ax
    inc cx
    mov bx, [w16_ty]
    push ax
    ICALL w16_rowsn
    mov dx, ax
    pop ax
    add dx, bx
    call w16d_focus
.done:
    call w16d_pclose
    xor ax, ax
    xor dx, dx
    ret

; w16d_pic13 - the part's 13-row picture at CS:SI drawn at [w16d_bxx],
; [w16d_by], [w16d_bh] rows of it (the middle rows dropped on a smaller
; font), black on white: copied into [w16d_pbuf] first, as w16_bmp reads
; our segment. Keeps BX.
w16d_pic13:
    push bx
    mov di, w16d_pbuf
    mov cx, [w16d_bh]
    xor bx, bx                      ; BX = the source row, scaled
.r:
    push bx
    mov ax, bx
    mov dx, 13
    mul dx
    div word [w16d_bh]
    shl ax, 1
    mov bx, ax
    mov ax, [cs:si + bx]
    mov [di], ax
    pop bx
    inc di
    inc di
    inc bx
    loop .r
    mov ax, [w16d_bxx]
    mov [w16_tx], ax
    mov ax, [w16d_by]
    mov [w16_ty], ax
    mov si, w16d_pbuf
    mov cx, [w16d_bh]
    mov dx, 13
    ICALL w16_bmp
    pop bx
    ret

; the pictures, 13 rows of two bytes (the left pixel the top bit of the
; first): a check box empty, checked (the X, corner to corner) and grey;
; a radio button empty and checked (measured off Windows 3.0's VGA)
w16d_bmcheck:
    db 0xFF,0xF8, 0x80,0x08, 0x80,0x08, 0x80,0x08, 0x80,0x08, 0x80,0x08
    db 0x80,0x08, 0x80,0x08, 0x80,0x08, 0x80,0x08, 0x80,0x08, 0x80,0x08
    db 0xFF,0xF8
    db 0xFF,0xF8, 0xC0,0x18, 0xA0,0x28, 0x90,0x48, 0x88,0x88, 0x85,0x08
    db 0x82,0x08, 0x85,0x08, 0x88,0x88, 0x90,0x48, 0xA0,0x28, 0xC0,0x18
    db 0xFF,0xF8
    db 0xFF,0xF8, 0xD5,0x58, 0xAA,0xA8, 0xD5,0x58, 0xAA,0xA8, 0xD5,0x58
    db 0xAA,0xA8, 0xD5,0x58, 0xAA,0xA8, 0xD5,0x58, 0xAA,0xA8, 0xD5,0x58
    db 0xFF,0xF8
w16d_bmradio:
    db 0x07,0x00, 0x18,0xC0, 0x20,0x20, 0x40,0x10, 0x40,0x10, 0x80,0x08
    db 0x80,0x08, 0x80,0x08, 0x40,0x10, 0x40,0x10, 0x20,0x20, 0x18,0xC0
    db 0x07,0x00
    db 0x07,0x00, 0x18,0xC0, 0x20,0x20, 0x47,0x10, 0x4F,0x90, 0x9F,0xC8
    db 0x9F,0xC8, 0x9F,0xC8, 0x4F,0x90, 0x47,0x10, 0x20,0x20, 0x18,0xC0
    db 0x07,0x00

; =============================================================================
; EDIT (after Wine's edit.c): one line, at most 255 characters, in a block
; of the USER heap made once and never moved; the caret a solid bar
; =============================================================================

w16d_edit:
    mov cx, [w16_sm_msg]
    mov si, w16d_edtab  ; XSEC-OK: read through cs:
.find:
    mov ax, [cs:si]
    or ax, ax
    jz .zero
    cmp ax, cx
    je .hit
    add si, 4
    jmp short .find
.hit:
    jmp [cs:si + 2]
.zero:
    xor ax, ax
    xor dx, dx
    ret

w16d_edtab:
    dw WM_CREATE, w16d_ecreate
    dw WM_PAINT, w16d_epaint
    dw WM_SETTEXT, w16d_esettext
    dw WM_GETDLGCODE, w16d_ecode
    dw WM_SETFOCUS, w16d_efocus
    dw WM_KILLFOCUS, w16d_eunfocus
    dw WM_LBUTTONDOWN, w16d_edown
    dw WM_MOUSEMOVE, w16d_emove
    dw WM_LBUTTONUP, w16d_eup
    dw WM_KEYDOWN, w16d_ekey
    dw WM_CHAR, w16d_echar
    dw EM_GETSEL, w16d_egetsel
    dw EM_SETSEL, w16d_esetsel
    dw EM_REPLACESEL, w16d_ereplmsg
    dw EM_LIMITTEXT, w16d_elimit
    dw EM_GETMODIFY, w16d_egetmod
    dw EM_SETMODIFY, w16d_esetmod
    dw EM_LINELENGTH, w16d_elen
    dw EM_GETLINE, w16d_egetline
    dw WM_CUT, w16d_ecut
    dw WM_COPY, w16d_ecopycb
    dw WM_PASTE, w16d_epaste
    dw WM_CLEAR, w16d_eclear
    dw 0

; w16d_ebuf - BX = an edit -> ES:DI = its text, CX = its length. Keeps the
; rest.
w16d_ebuf:
    push si
    ICALL w16_textptr               ; ES:SI, CX
    mov di, si
    pop si
    ret

; WM_CREATE: the block, the text copied in
w16d_ecreate:
    call w16_mheap                 ; ES = the USER heap
    jc .fail
    mov cx, EC_BUFSZ
    xor al, al
    ICALL w16_lnew
    or ax, ax
    jz .fail
    mov di, ax
    push di
    ICALL w16_textptr               ; ES:SI, CX = the text it was made with
    cmp cx, EC_BUFSZ - 1
    jbe .c
    mov cx, EC_BUFSZ - 1
.c:
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
    pop di
    mov ax, [bx + W16W_TEXT]
    or ax, ax
    jz .none
    ICALL w16_lfree
.none:
    mov [bx + W16W_TEXT], di
    mov word [bx + EC_LIMIT], EC_BUFSZ - 1
    xor ax, ax
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    mov [bx + EC_FIRST], ax
    xor dx, dx
    ret
.fail:
    mov ax, -1
    mov dx, ax
    ret

w16d_ecode:
    mov ax, DLGC_WANTCHARS | DLGC_HASSETSEL | DLGC_WANTARROWS
    xor dx, dx
    ret

w16d_efocus:
    or byte [bx + EC_FL], ECF_FOCUS
    jmp short w16d_eredraw
w16d_eunfocus:
    and byte [bx + EC_FL], ~ECF_FOCUS
    mov byte [bx + EC_DRAG], 0
w16d_eredraw:
    call w16d_inval
w16d_ezero:
    xor ax, ax
    xor dx, dx
    ret

; WM_SETTEXT: the string, the caret at its start
w16d_esettext:
    call w16d_ebuf                  ; ES:DI = the block
    push ds
    lds si, [w16_sm_lp]
    mov cx, ds
    or cx, si
    jz .empty
    mov cx, EC_BUFSZ - 1
.c:
    lodsb
    or al, al
    jz .end
    stosb
    loop .c
.end:
.empty:
    pop ds
    mov byte [es:di], 0
    xor ax, ax
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    mov [bx + EC_FIRST], ax
    call w16d_inval
    mov ax, 1
    xor dx, dx
    ret

; w16d_esel - BX = an edit -> AX = the selection's start, DX = its end.
; Keeps the rest.
w16d_esel:
    mov ax, [bx + EC_SELA]
    mov dx, [bx + EC_CARET]
    cmp ax, dx
    jbe .out
    xchg ax, dx
.out:
    ret

w16d_egetsel:
    call w16d_esel
    ret

; EM_SETSEL: lParam's low word the start, its high word the end (-1 its end)
w16d_esetsel:
    call w16d_ebuf                  ; CX = the length
    mov ax, [w16_sm_lp]
    mov dx, [w16_sm_lp + 2]
    cmp ax, cx
    jbe .a
    mov ax, cx
.a:
    cmp dx, cx
    jbe .d
    mov dx, cx
.d:
    mov [bx + EC_SELA], ax
    mov [bx + EC_CARET], dx
    jmp w16d_eredraw

w16d_elimit:
    mov ax, [w16_sm_wp]
    or ax, ax
    jz .max
    cmp ax, EC_BUFSZ - 1
    jbe .set
.max:
    mov ax, EC_BUFSZ - 1
.set:
    mov [bx + EC_LIMIT], ax
    jmp w16d_ezero

w16d_egetmod:
    mov al, [bx + EC_FL]
    and ax, ECF_MODIFY
    shr ax, 1
    xor dx, dx
    ret

w16d_esetmod:
    and byte [bx + EC_FL], ~ECF_MODIFY
    cmp word [w16_sm_wp], 0
    je w16d_ezero
    or byte [bx + EC_FL], ECF_MODIFY
    jmp w16d_ezero

w16d_elen:
    call w16d_ebuf
    mov ax, cx
    xor dx, dx
    ret

; EM_GETLINE: the one line into lParam, whose first word is its room; no NUL
w16d_egetline:
    push ds
    call w16d_ebuf                  ; ES:DI, CX
    mov si, di
    push es
    les di, [w16_sm_lp]
    mov ax, [es:di]
    cmp cx, ax
    jbe .n
    mov cx, ax
.n:
    pop ds                          ; DS:SI = the text
    mov ax, cx
    cld
    rep movsb
    pop ds
    xor dx, dx
    ret

; EM_REPLACESEL: lParam's string in place of the selection
w16d_ereplmsg:
    push ds
    lds si, [w16_sm_lp]
    mov di, w16d_sbuf
    pop es
    push es
    xor cx, cx
    mov ax, ds
    or ax, si
    jz .end
.c:
    lodsb
    or al, al
    jz .end
    stosb
    inc cx
    cmp cx, EC_BUFSZ - 1
    jb .c
.end:
    pop ds
    mov si, w16d_sbuf
    call w16d_erepl
    jmp w16d_ezero

; -----------------------------------------------------------------------------
; w16d_erepl - BX = an edit: CX characters at DS:SI in place of its
; selection, as many as its limit takes; the caret after them, the text
; modified, EN_CHANGE posted and the edit redrawn. Keeps BX.
; -----------------------------------------------------------------------------
w16d_erepl:
    push cx
    push si
    call w16d_esel                  ; AX..DX = the selection: out first
    mov [w16d_e0], ax
    mov cx, dx
    sub cx, ax
    call w16d_edel
    call w16d_ebuf                  ; CX = the length now
    mov ax, [bx + EC_LIMIT]
    sub ax, cx                      ; AX = the room
    pop si
    pop cx
    cmp cx, ax
    jbe .fit
    mov cx, ax
.fit:
    jcxz .done
    push cx
    call w16d_ebuf                  ; ES:DI = the text, CX = its length
    mov ax, cx                      ; move the tail right by the count,
    sub ax, [w16d_e0]               ;   last byte first
    inc ax                          ; (its NUL too)
    pop dx                          ; DX = the count
    push dx
    push si
    mov si, di
    add si, cx                      ; ES:SI = the NUL
    mov di, si
    add di, dx
    mov cx, ax
    push ds
    push es
    pop ds
    std
    rep movsb
    cld
    pop ds
    pop si
    pop cx                          ; CX = the count
    push cx
    call w16d_ebuf
    pop cx
    add di, [w16d_e0]
    push cx
    cld
    rep movsb                       ; DS:SI in
    pop cx
.done:
    mov ax, [w16d_e0]
    add ax, cx
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    or byte [bx + EC_FL], ECF_MODIFY
    mov ax, EN_CHANGE
    call w16d_notify
    call w16d_inval
    ret

; w16d_edel - BX = an edit: CX characters from AX deleted. Keeps BX, AX.
w16d_edel:
    jcxz .out
    push ax
    push cx
    push si
    push di
    push cx
    call w16d_ebuf                  ; ES:DI, CX = the length
    pop dx                          ; DX = the count
    add di, ax                      ; ES:DI = the first to go
    mov si, di
    add si, dx                      ; ES:SI = the first kept after them
    sub cx, ax
    sub cx, dx
    inc cx                          ; ...to the NUL
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    pop di
    pop si
    pop cx
    pop ax
.out:
    ret

; WM_CHAR: a character in place of the selection, Backspace the selection
; or the character before the caret
w16d_echar:
    mov al, [w16_sm_wp]
    cmp al, VK_BACK
    je .back
    cmp al, ' '
    jb w16d_ezero
    test byte [bx + EC_STYLE], ES_UPPERCASE
    jz .nu
    cmp al, 'a'
    jb .nu
    cmp al, 'z'
    ja .nu
    sub al, 'a' - 'A'
.nu:
    test byte [bx + EC_STYLE], ES_LOWERCASE
    jz .nl
    cmp al, 'A'
    jb .nl
    cmp al, 'Z'
    ja .nl
    add al, 'a' - 'A'
.nl:
    mov [w16d_sbuf], al
    mov si, w16d_sbuf
    mov cx, 1
    call w16d_erepl
    jmp w16d_ezero
.back:
    call w16d_esel
    cmp ax, dx
    jne .cut
    or ax, ax
    jz w16d_ezero
    dec ax
.cut:
    mov cx, dx
    sub cx, ax
    call w16d_edel
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    or byte [bx + EC_FL], ECF_MODIFY
    mov ax, EN_CHANGE
    call w16d_notify
    jmp w16d_eredraw

; w16d_eshift - CF=1 when Shift was down for the key in [w16_sm_lp]'s scan
; code. Keeps every register.
w16d_eshift:
    push bx
    mov bl, [w16_sm_lp + 2]
    xor bh, bh
    cmp bl, W16_NSCAN
    jae .no
    test byte [w16_kmods + bx], 3
    jz .no
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; w16d_ectrl - CF=1 when Ctrl was down for the key in [w16_sm_lp]'s scan
; code. Keeps every register.
w16d_ectrl:
    push bx
    mov bl, [w16_sm_lp + 2]
    xor bh, bh
    cmp bl, W16_NSCAN
    jae .no
    test byte [w16_kmods + bx], 4
    jz .no
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; WM_COPY: the selection onto os8088's clipboard; an ES_PASSWORD edit too,
; as Windows 3.0's did (CALC 3.00 copies through one)
w16d_ecopycb:
    call w16d_esel                  ; AX..DX
    mov cx, dx
    sub cx, ax
    jcxz .out
    push es
    call w16d_ebuf                  ; ES:DI = the text
    mov si, di
    add si, ax
    call w16_clput                 ; (a single line: nothing to translate)
    pop es
.out:
    jmp w16d_ezero

; WM_CUT: copied, then cleared
w16d_ecut:
    call w16d_ecopycb
; WM_CLEAR: the selection deleted
w16d_eclear:
    call w16d_esel
    cmp ax, dx
    je .out
    mov cx, dx
    sub cx, ax
    call w16d_edel
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    or byte [bx + EC_FL], ECF_MODIFY
    mov ax, EN_CHANGE
    call w16d_notify
    jmp w16d_eredraw
.out:
    jmp w16d_ezero

; WM_PASTE: the clipboard's text, to its first line end, in place of the
; selection
w16d_epaste:
    push es
    push ds
    pop es
    mov di, w16d_sbuf
    mov cx, EC_BUFSZ - 1
    call OSAPI_CLIP_GET             ; CX = the bytes copied
    pop es
    jc .out
    mov si, w16d_sbuf
    xor dx, dx
.end:
    cmp dx, cx
    jae .got
    mov al, [si]
    cmp al, 13
    je .got
    cmp al, 10
    je .got
    or al, al
    je .got
    inc si
    inc dx
    jmp short .end
.got:
    mov cx, dx
    mov si, w16d_sbuf
    jcxz .out
    call w16d_erepl
.out:
    jmp w16d_ezero

; WM_KEYDOWN: the arrows, Home, End and Delete; Shift extends the selection
w16d_ekey:
    call w16d_ebuf                  ; CX = the length
    mov ax, [bx + EC_CARET]
    mov dx, [w16_sm_wp]
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
    jmp w16d_ezero
.ins:
    call w16d_eshift                ; Shift+Ins pastes, Ctrl+Ins copies
    jc w16d_epaste
    call w16d_ectrl
    jc w16d_ecopycb
    jmp w16d_ezero
.left:
    or ax, ax
    jz .move
    dec ax
    jmp short .move
.right:
    cmp ax, cx
    jae .move
    inc ax
    jmp short .move
.home:
    xor ax, ax
    jmp short .move
.end:
    mov ax, cx
.move:
    mov [bx + EC_CARET], ax
    call w16d_eshift
    jc .ext
    mov [bx + EC_SELA], ax
.ext:
    jmp w16d_eredraw
.del:
    call w16d_eshift                ; Shift+Del cuts
    jc w16d_ecut
    call w16d_esel
    cmp ax, dx
    jne .cut
    cmp ax, cx
    jae w16d_ezero
    inc dx
.cut:
    mov cx, dx
    sub cx, ax
    call w16d_edel
    mov [bx + EC_CARET], ax
    mov [bx + EC_SELA], ax
    or byte [bx + EC_FL], ECF_MODIFY
    mov ax, EN_CHANGE
    call w16d_notify
    jmp w16d_eredraw

; w16d_emargin - BX = an edit -> AX = where its text starts in its client:
; 4 inside a border, 1 without. Keeps the rest.
w16d_emargin:
    mov ax, 1
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .out
    mov ax, 4
.out:
    ret

; w16d_ehit - BX = an edit, AX = a client x -> AX = the character index the
; caret goes to there. Keeps the rest.
w16d_ehit:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, ax
    call w16d_emargin
    sub dx, ax                      ; DX = the x in the text
    call w16d_ecopy                 ; [w16d_sbuf], CX characters shown
    mov ax, [bx + EC_FIRST]
    mov di, ax                      ; DI = the index
    xor si, si
.c:
    or dx, dx
    jle .out
    cmp si, cx
    jae .out
    push cx
    mov cx, 1
    call w16d_tw                    ; AX = this one's advance
    pop cx
    push ax
    shr ax, 1
    cmp dx, ax                      ; past its middle: after it
    pop ax
    jl .out
    sub dx, ax
    inc si
    inc di
    jmp short .c
.out:
    mov ax, di
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16d_ecopy - BX = an edit: what it shows from [EC_FIRST] into [w16d_sbuf]
; (stars for ES_PASSWORD) -> CX = the characters. Keeps the rest.
w16d_ecopy:
    push ax
    push si
    push di
    push es
    call w16d_ebuf                  ; ES:DI, CX
    mov si, di
    add si, [bx + EC_FIRST]
    sub cx, [bx + EC_FIRST]
    jae .n
    xor cx, cx
.n:
    mov di, w16d_sbuf
    push cx
    jcxz .done
.c:
    mov al, [es:si]
    test byte [bx + EC_STYLE], ES_PASSWORD
    jz .p
    mov al, '*'
.p:
    mov [di], al
    inc si
    inc di
    loop .c
.done:
    mov byte [di], 0
    pop cx
    mov [w16d_n], cx
    pop es
    pop di
    pop si
    pop ax
    ret

w16d_edown:
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_setfocus
    pop bx
    ICALL w16_rec2hwnd
    mov [w16_capture], ax
    mov byte [bx + EC_DRAG], 1
    mov ax, [w16_sm_lp]
    call w16d_ehit
    mov [bx + EC_CARET], ax
    test byte [w16_sm_wp], 4        ; MK_SHIFT
    jnz .ext
    mov [bx + EC_SELA], ax
.ext:
    jmp w16d_eredraw

w16d_emove:
    cmp byte [bx + EC_DRAG], 0
    je .z
    mov ax, [w16_sm_lp]
    call w16d_ehit
    cmp ax, [bx + EC_CARET]
    je .z
    mov [bx + EC_CARET], ax
    jmp w16d_eredraw
.z:
    jmp w16d_ezero

w16d_eup:
    cmp byte [bx + EC_DRAG], 0
    je .z
    mov byte [bx + EC_DRAG], 0
    ICALL w16_rec2hwnd
    cmp [w16_capture], ax
    jne .z
    mov word [w16_capture], 0
.z:
    jmp w16d_ezero

; -----------------------------------------------------------------------------
; w16d_epaint - the frame (WS_BORDER), and the line on white from its first
; character shown: the selection in COLOR_HIGHLIGHT when it has the focus,
; and the caret, a bar the text's height
; -----------------------------------------------------------------------------
w16d_epaint:
    call w16d_popen
    jnc .go
    ret
.go:
    xor ax, ax                      ; [w16d_il].. = inside the frame
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .nb
    inc ax
    push ax
    mov si, COLOR_WINDOWFRAME
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
    mov ax, [w16d_r]
    mov cx, ax
    call w16d_fill
    pop ax
    mov bx, [w16d_rec]
.nb:
    mov dx, [w16d_l]
    add dx, ax
    mov [w16d_il], dx
    mov dx, [w16d_r]
    sub dx, ax
    mov [w16d_ir], dx
    mov dx, [w16d_t]
    add dx, ax
    mov [w16d_it], dx
    mov dx, [w16d_b]
    sub dx, ax
    mov [w16d_ib], dx
    call w16d_evis                  ; the caret shown: [EC_FIRST]
    ICALL w16_rowsn                 ; the line's rows: [w16_ty]..[w16d_lb]
    mov dx, [w16d_h]
    sub dx, ax
    sar dx, 1
    add dx, [w16d_t]
    mov [w16_ty], dx
    add dx, ax
    dec dx
    mov [w16d_lb], dx
    mov si, COLOR_WINDOW            ; white above it and below it
    mov ax, [w16d_il]
    mov bx, [w16d_it]
    mov cx, [w16d_ir]
    mov dx, [w16_ty]
    dec dx
    call w16d_fill
    mov bx, [w16d_lb]
    inc bx
    mov dx, [w16d_ib]
    call w16d_fill
    mov bx, [w16d_rec]
    call w16d_emargin
    add ax, [w16d_l]
    mov [w16_tx], ax
    dec ax                          ; ...and left of it
    mov cx, ax
    mov ax, [w16d_il]
    mov bx, [w16_ty]
    mov dx, [w16d_lb]
    call w16d_fill
    push word [w16_cr]              ; the line, cut at the frame
    mov ax, [w16d_ir]
    cmp ax, [w16_cr]
    jge .cr
    mov [w16_cr], ax
.cr:
    mov bx, [w16d_rec]
    call w16d_ecopy                 ; [w16d_sbuf], CX characters
    call w16d_esel                  ; AX..DX, the selection, as indices in
    sub ax, [bx + EC_FIRST]         ;   what is shown
    jae .s0
    xor ax, ax
.s0:
    sub dx, [bx + EC_FIRST]
    jae .s1
    xor dx, dx
.s1:
    test byte [bx + EC_FL], ECF_FOCUS
    jnz .sel
    mov ax, cx                      ; no focus: no selection shown
    mov dx, cx
.sel:
    cmp ax, cx
    jbe .s2
    mov ax, cx
.s2:
    cmp dx, cx
    jbe .s3
    mov dx, cx
.s3:
    mov [w16d_e0], ax
    mov [w16d_e1], dx
    mov al, COLOR_WINDOWTEXT        ; before the selection
    call w16d_grey
    mov ah, COLOR_WINDOW
    call w16d_colors
    xor si, si
    mov cx, [w16d_e0]
    call w16d_eseg
    mov ax, (COLOR_WINDOW << 8) | COLOR_HIGHLIGHTTEXT
    call w16d_colors                ; the selection
    mov ax, COLOR_HIGHLIGHT
    call w16d_syscol
    mov [w16_tpap], al
    mov si, [w16d_e0]
    mov cx, [w16d_e1]
    sub cx, si
    call w16d_eseg
    mov al, COLOR_WINDOWTEXT        ; after it
    call w16d_grey
    mov ah, COLOR_WINDOW
    call w16d_colors
    mov si, [w16d_e1]
    mov cx, [w16d_n]
    sub cx, si
    call w16d_eseg
    pop word [w16_cr]
    mov ax, [w16_tx]                ; white to the right of the text
    cmp ax, [w16d_ir]
    jg .caret
    mov bx, [w16_ty]
    mov cx, [w16d_ir]
    mov dx, [w16d_lb]
    mov si, COLOR_WINDOW
    call w16d_fill
.caret:
    mov bx, [w16d_rec]
    test byte [bx + EC_FL], ECF_FOCUS
    jz .done
    mov cx, [bx + EC_CARET]
    sub cx, [bx + EC_FIRST]
    jb .done
    xor si, si
    call w16d_tw                    ; AX = the caret's x in the text
    push ax
    call w16d_emargin
    add ax, [w16d_l]
    pop si
    add ax, si
    cmp ax, [w16d_ir]
    jg .done
    mov cx, ax
    mov bx, [w16_ty]
    mov dx, [w16d_lb]
    mov si, COLOR_WINDOWTEXT
    call w16d_fill
.done:
    call w16d_pclose
    jmp w16d_ezero

; w16d_eseg - CX characters of [w16d_sbuf] from SI drawn at [w16_tx], which
; moves past them. Keeps BX.
w16d_eseg:
    jcxz .out
    push bx
    mov ax, si
    call w16d_settxt
    push cx
    call w16d_tw
    pop cx
    push ax
    ICALL w16_txtdraw
    pop ax
    add [w16_tx], ax
    pop bx
.out:
    ret

; w16d_evis - BX = an edit: [EC_FIRST] moved so the caret is shown. Keeps
; BX.
w16d_evis:
    push ax
    push cx
    push si
    mov ax, [bx + EC_CARET]
    cmp ax, [bx + EC_FIRST]
    jae .w
    mov [bx + EC_FIRST], ax
.w:
    call w16d_ecopy                 ; CX characters from the first
    mov cx, [bx + EC_CARET]
    sub cx, [bx + EC_FIRST]
    xor si, si
    call w16d_tw                    ; AX = the caret's x in the text
    mov si, [w16d_ir]
    sub si, [w16d_il]
    sub si, 4                       ; SI = the room
    cmp ax, si
    jle .out
    inc word [bx + EC_FIRST]
    jmp short .w
.out:
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; LISTBOX (after Wine's listbox.c): one selection, its items in the USER
; heap - a table of LI_SIZE entries and a block a string - and Windows 3.0's
; scroll bar when they do not all show
; =============================================================================

W16D_SBW    equ 17              ; the scroll bar's width (SM_CXVSCROLL)

w16d_list:
    mov cx, [w16_sm_msg]
    mov si, w16d_lbtab  ; XSEC-OK: read through cs:
.find:
    mov ax, [cs:si]
    or ax, ax
    jz .zero
    cmp ax, cx
    je .hit
    add si, 4
    jmp short .find
.hit:
    jmp [cs:si + 2]
.zero:
w16d_lzero:
    xor ax, ax
    xor dx, dx
    ret

w16d_lbtab:
    dw WM_CREATE, w16d_lcreate
    dw WM_DESTROY, w16d_ldestroy
    dw WM_PAINT, w16d_lpaint
    dw WM_GETDLGCODE, w16d_lcode
    dw WM_SETFOCUS, w16d_lfocus
    dw WM_KILLFOCUS, w16d_lunfocus
    dw WM_LBUTTONDOWN, w16d_ldown
    dw WM_MOUSEMOVE, w16d_lmove
    dw WM_LBUTTONUP, w16d_lup
    dw WM_KEYDOWN, w16d_lkey
    dw WM_CHAR, w16d_lchar
    dw LB_ADDSTRING, w16d_ladd
    dw LB_INSERTSTRING, w16d_lins
    dw LB_DELETESTRING, w16d_ldel
    dw LB_RESETCONTENT, w16d_lreset
    dw LB_SETCURSEL, w16d_lsetcur
    dw LB_GETCURSEL, w16d_lgetcur
    dw LB_GETTEXT, w16d_lgettext
    dw LB_GETTEXTLEN, w16d_lgettlen
    dw LB_GETCOUNT, w16d_lgetcount
    dw LB_FINDSTRING, w16d_lfind
    dw LB_SELECTSTRING, w16d_lselect
    dw LB_GETTOPINDEX, w16d_lgettop
    dw LB_SETTOPINDEX, w16d_lsettop
    dw LB_GETITEMDATA, w16d_lgetdata
    dw LB_SETITEMDATA, w16d_lsetdata
    dw 0

w16d_lerr:
    mov ax, -1
    mov dx, ax
    ret

w16d_lcreate:
    call w16_mheap
    jc w16d_lerr
    mov cx, 16 * LI_SIZE
    xor al, al
    ICALL w16_lnew
    or ax, ax
    jz w16d_lerr
    mov [bx + LC_TAB], ax
    mov word [bx + LC_CAP], 16
    mov word [bx + LC_COUNT], 0
    mov word [bx + LC_CUR], -1
    mov word [bx + LC_TOP], 0
    jmp w16d_lzero

w16d_ldestroy:
    call w16d_lfreeall
    mov ax, [bx + LC_TAB]
    or ax, ax
    jz .z
    call w16_mheap
    jc .z
    ICALL w16_lfree
    mov word [bx + LC_TAB], 0
.z:
    jmp w16d_lzero

; w16d_lfreeall - BX = a list box: every item's string given back, none
; left. Keeps BX.
w16d_lfreeall:
    mov cx, [bx + LC_COUNT]
    jcxz .out
    call w16_mheap
    jc .out
    mov si, [bx + LC_TAB]
.i:
    mov ax, [es:si]
    or ax, ax
    jz .n
    ICALL w16_lfree
.n:
    add si, LI_SIZE
    loop .i
.out:
    mov word [bx + LC_COUNT], 0
    mov word [bx + LC_CUR], -1
    mov word [bx + LC_TOP], 0
    ret

w16d_lreset:
    call w16d_lfreeall
    call w16d_inval
    jmp w16d_lzero

w16d_lcode:
    mov ax, DLGC_WANTARROWS | DLGC_WANTCHARS
    xor dx, dx
    ret

w16d_lfocus:
    or byte [bx + LC_FL], LCF_FOCUS
    call w16d_inval
    jmp w16d_lzero
w16d_lunfocus:
    and byte [bx + LC_FL], ~(LCF_FOCUS | LCF_TRACK)
    call w16d_inval
    jmp w16d_lzero

; w16d_lent - BX = a list box, AX = an index -> ES:SI = its entry. Keeps
; the rest.
w16d_lent:
    push ax
    push dx
    call w16_mheap
    mov dx, LI_SIZE
    mul dx
    mov si, ax
    add si, [bx + LC_TAB]
    pop dx
    pop ax
    ret

; w16d_lstr - BX = a list box, AX = an index -> ES:SI = its string, CX =
; its length. Keeps the rest.
w16d_lstr:
    push ax
    push di
    call w16d_lent
    mov si, [es:si]
    mov di, si
    mov cx, 0xFFFF
    xor al, al
    cld
    repne scasb
    not cx
    dec cx
    pop di
    pop ax
    ret

w16d_lgetcount:
    mov ax, [bx + LC_COUNT]
    xor dx, dx
    ret

w16d_lgetcur:
    mov ax, [bx + LC_CUR]
    cwd
    ret

w16d_lgettop:
    mov ax, [bx + LC_TOP]
    xor dx, dx
    ret

w16d_lsettop:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae w16d_lerr
    mov [bx + LC_TOP], ax
    call w16d_inval
    jmp w16d_lzero

w16d_lgetdata:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae w16d_lerr
    call w16d_lent
    mov ax, [es:si + 2]
    xor dx, dx
    ret

w16d_lsetdata:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae w16d_lerr
    call w16d_lent
    mov dx, [w16_sm_lp]
    mov [es:si + 2], dx
    jmp w16d_lzero

w16d_lgettlen:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae w16d_lerr
    call w16d_lstr
    mov ax, cx
    xor dx, dx
    ret

; LB_GETTEXT: the item's string and its NUL into lParam; its length
w16d_lgettext:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae .err
    call w16d_lstr                  ; ES:SI = the string, CX = its length
    push cx
    push ds
    mov di, [w16_sm_lp]
    mov ax, [w16_sm_lp + 2]
    push es
    pop ds                          ; DS:SI = the string
    mov es, ax                      ; ES:DI = the buffer
    inc cx
    cld
    rep movsb
    pop ds
    pop ax
    xor dx, dx
    ret
.err:
    jmp w16d_lerr

; w16d_lnewstr - lParam's string -> AX = a copy of it in the USER heap,
; 0 none. Keeps BX.
w16d_lnewstr:
    push cx
    push si
    push di
    push ds
    lds si, [w16_sm_lp]
    xor cx, cx
    mov ax, ds
    or ax, si
    jz .len0
.l:
    cmp byte [si], 0
    je .len0
    inc si
    inc cx
    cmp cx, 255
    jb .l
.len0:
    sub si, cx
    pop ds
    push ds
    push cx
    call w16_mheap
    pop cx
    jc .no
    push cx
    inc cx
    xor al, al
    ICALL w16_lnew
    pop cx
    or ax, ax
    jz .no
    mov di, ax
    push ax
    push ds
    mov ds, [w16_sm_lp + 2]
    cld
    rep movsb
    pop ds
    mov byte [es:di], 0
    pop ax
    jmp short .out
.no:
    xor ax, ax
.out:
    pop ds
    pop di
    pop si
    pop cx
    ret

; LB_ADDSTRING: at the end, or in its place with LBS_SORT
w16d_ladd:
    mov ax, [bx + LC_COUNT]
    test byte [bx + LC_STYLE], LBS_SORT
    jz w16d_linsat
    xor ax, ax                      ; the first item after it
.s:
    cmp ax, [bx + LC_COUNT]
    jae w16d_linsat
    call w16d_lcmp                  ; CF=1 the item is after lParam's
    jc w16d_linsat
    inc ax
    jmp short .s

; LB_INSERTSTRING: at wParam, -1 the end
w16d_lins:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jbe w16d_linsat
    mov ax, [bx + LC_COUNT]
    ; fall into w16d_linsat

; w16d_linsat - lParam's string as item AX -> its index
w16d_linsat:
    mov [w16d_e0], ax
    mov ax, [bx + LC_COUNT]         ; room for one more
    cmp ax, [bx + LC_CAP]
    jb .room
    mov cx, [bx + LC_CAP]
    shl cx, 1
    push cx
    shl cx, 1
    shl cx, 1                       ; (* LI_SIZE)
    call w16_mheap
    xor al, al
    ICALL w16_lnew
    pop cx
    or ax, ax
    jz w16d_lerr
    push cx
    mov di, ax                      ; the table, copied over
    mov si, [bx + LC_TAB]
    mov cx, [bx + LC_COUNT]
    shl cx, 1
    push ds
    push es
    pop ds
    cld
    rep movsw
    pop ds
    mov ax, [bx + LC_TAB]
    ICALL w16_lfree
    sub di, [bx + LC_COUNT]
    sub di, [bx + LC_COUNT]
    sub di, [bx + LC_COUNT]
    sub di, [bx + LC_COUNT]
    mov [bx + LC_TAB], di
    pop cx
    mov [bx + LC_CAP], cx
.room:
    call w16d_lnewstr               ; AX = the string
    or ax, ax
    jz w16d_lerr
    push ax
    mov ax, [bx + LC_COUNT]         ; the entries after it, one along
    call w16d_lent                  ; ES:SI = one past the last
    mov di, si
    add di, LI_SIZE - 2
    sub si, 2
    mov cx, [bx + LC_COUNT]
    sub cx, [w16d_e0]
    shl cx, 1
    push ds
    push es
    pop ds
    std
    rep movsw
    cld
    pop ds
    mov ax, [w16d_e0]
    call w16d_lent
    pop ax
    mov [es:si], ax
    mov word [es:si + 2], 0
    inc word [bx + LC_COUNT]
    mov ax, [w16d_e0]
    cmp word [bx + LC_CUR], -1
    je .cur
    cmp [bx + LC_CUR], ax
    jb .cur
    inc word [bx + LC_CUR]
.cur:
    call w16d_inval
    mov ax, [w16d_e0]
    xor dx, dx
    ret

; LB_DELETESTRING: wParam's item; the count left
w16d_ldel:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jae w16d_lerr
    call w16d_lent
    push ax
    mov ax, [es:si]
    ICALL w16_lfree
    pop ax
    mov di, si
    add si, LI_SIZE
    mov cx, [bx + LC_COUNT]
    sub cx, ax
    dec cx
    shl cx, 1
    push ds
    push es
    pop ds
    cld
    rep movsw
    pop ds
    dec word [bx + LC_COUNT]
    mov dx, [bx + LC_CUR]
    cmp dx, -1
    je .t
    cmp dx, ax
    jb .t
    ja .after
    mov word [bx + LC_CUR], -1
    jmp short .t
.after:
    dec word [bx + LC_CUR]
.t:
    mov ax, [bx + LC_TOP]
    cmp ax, [bx + LC_COUNT]
    jb .ok
    mov word [bx + LC_TOP], 0
.ok:
    call w16d_inval
    mov ax, [bx + LC_COUNT]
    xor dx, dx
    ret

; w16d_lcmp - BX = a list box, AX = an item: CF=1 when it sorts after
; lParam's string (case folded). Keeps every register.
w16d_lcmp:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    call w16d_lstr                  ; ES:SI = the item's
    mov di, si
    lds si, [w16_sm_lp]             ; DS:SI = the new one's
.c:
    mov al, [si]
    mov ah, [es:di]
    call w16d_fold
    cmp ah, al
    ja .after
    jb .before
    or al, al
    jz .before
    inc si
    inc di
    jmp short .c
.after:
    stc
    jmp short .out
.before:
    clc
.out:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16d_fold - AL and AH in upper case. Keeps the rest.
w16d_fold:
    cmp al, 'a'
    jb .h
    cmp al, 'z'
    ja .h
    sub al, 'a' - 'A'
.h:
    cmp ah, 'a'
    jb .out
    cmp ah, 'z'
    ja .out
    sub ah, 'a' - 'A'
.out:
    ret

; w16d_lprefix - BX = a list box, AX = an item: CF=1 its string begins with
; lParam's (case folded). Keeps every register.
w16d_lprefix:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    call w16d_lstr
    mov di, si
    lds si, [w16_sm_lp]
.c:
    mov al, [si]
    or al, al
    jz .yes
    mov ah, [es:di]
    call w16d_fold
    cmp al, ah
    jne .no
    inc si
    inc di
    jmp short .c
.yes:
    stc
    jmp short .out
.no:
    clc
.out:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; LB_FINDSTRING: the first item after wParam (-1: from the start) that
; begins with lParam's string, round to it again
w16d_lfind:
    mov cx, [bx + LC_COUNT]
    jcxz .err
    mov ax, [w16_sm_wp]
.i:
    inc ax
    cmp ax, [bx + LC_COUNT]
    jb .t
    xor ax, ax
.t:
    call w16d_lprefix
    jc .hit
    loop .i
.err:
    jmp w16d_lerr
.hit:
    xor dx, dx
    ret

w16d_lselect:
    call w16d_lfind
    cmp ax, -1
    je .out
    push ax
    call w16d_lcur
    pop ax
    xor dx, dx
.out:
    ret

; LB_SETCURSEL: wParam the selection, -1 none
w16d_lsetcur:
    mov ax, [w16_sm_wp]
    cmp ax, [bx + LC_COUNT]
    jb .ok
    mov word [bx + LC_CUR], -1
    call w16d_inval
    jmp w16d_lerr
.ok:
    call w16d_lcur
    mov ax, [w16_sm_wp]
    xor dx, dx
    ret

; w16d_lcur - BX = a list box, AX = the item to select: selected, shown and
; redrawn. Keeps BX.
w16d_lcur:
    mov [bx + LC_CUR], ax
    cmp ax, [bx + LC_TOP]
    jae .below
    mov [bx + LC_TOP], ax
    jmp short .inv
.below:
    push ax
    call w16d_lvis                  ; CX = the rows it shows
    pop ax
    mov dx, [bx + LC_TOP]
    add dx, cx
    cmp ax, dx
    jb .inv
    sub ax, cx
    inc ax
    mov [bx + LC_TOP], ax
.inv:
    call w16d_inval
    ret

; w16d_lvis - BX = a list box -> CX = how many items its client shows, at
; least one. Keeps the rest.
w16d_lvis:
    push ax
    push dx
    ICALL w16_csize                 ; DX = its height
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .nb
    sub dx, 2
.nb:
    ICALL w16_rowsn
    mov cx, ax
    mov ax, dx
    or ax, ax
    jle .none
    xor dx, dx
    div cx
    mov cx, ax
    jmp short .chk
.none:
    xor cx, cx
.chk:
    or cx, cx
    jnz .out
    inc cx
.out:
    pop dx
    pop ax
    ret

; w16d_lbar - BX = a list box: CF=1 it shows its scroll bar (WS_VSCROLL and
; more items than rows). Keeps every register.
w16d_lbar:
    test word [bx + W16W_STYLE + 2], WS_VSCROLL_HI
    jz .no
    push cx
    call w16d_lvis
    cmp [bx + LC_COUNT], cx
    pop cx
    jbe .no
    stc
    ret
.no:
    clc
    ret

; w16d_lsel - BX = a list box, AX = the item now selected: if it is a
; change, selected and, with LBS_NOTIFY, LBN_SELCHANGE posted. Keeps BX.
w16d_lsel:
    cmp ax, [bx + LC_CUR]
    je .out
    call w16d_lcur
    test byte [bx + LC_STYLE], LBS_NOTIFY
    jz .out
    mov ax, LBN_SELCHANGE
    call w16d_notify
.out:
    ret

; w16d_lhit - BX = a list box, AX = a client y -> AX = the item there, not
; past the last. CF=1 there is none. Keeps the rest.
w16d_lhit:
    push cx
    push dx
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .nb
    dec ax
.nb:
    or ax, ax
    jge .p
    xor ax, ax
.p:
    push ax
    ICALL w16_rowsn
    mov cx, ax
    pop ax
    xor dx, dx
    div cx
    add ax, [bx + LC_TOP]
    cmp ax, [bx + LC_COUNT]
    jb .ok
    mov ax, [bx + LC_COUNT]
    dec ax
    js .none
.ok:
    clc
    jmp short .out
.none:
    stc
.out:
    pop dx
    pop cx
    ret

w16d_ldown:
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_setfocus
    pop bx
    call w16d_lbar                  ; on the scroll bar?
    jnc .items
    ICALL w16_csize
    mov ax, [w16_sm_lp]
    add ax, W16D_SBW
    cmp ax, cx
    jl .items
    mov ax, [w16_sm_lp + 2]         ; the arrows a line, the rest a page
    call w16d_lvis
    cmp ax, W16D_SBW
    jl .up
    sub dx, W16D_SBW
    cmp ax, dx
    jge .down
    shr dx, 1
    cmp ax, dx
    jl .pgup
    mov ax, [bx + LC_TOP]
    add ax, cx
    jmp short .top
.pgup:
    mov ax, [bx + LC_TOP]
    sub ax, cx
    jmp short .top
.up:
    mov ax, [bx + LC_TOP]
    dec ax
    jmp short .top
.down:
    mov ax, [bx + LC_TOP]
    inc ax
.top:
    or ax, ax                       ; between 0 and the count less a page
    jge .t0
    xor ax, ax
.t0:
    mov dx, [bx + LC_COUNT]
    sub dx, cx
    cmp ax, dx
    jle .t1
    mov ax, dx
.t1:
    mov [bx + LC_TOP], ax
    call w16d_inval
    jmp w16d_lzero
.items:
    mov ax, [w16_sm_lp + 2]
    call w16d_lhit
    jc .z
    push ax
    ICALL w16_ticksms               ; a second press on the same item, soon:
    pop dx                          ;   LBN_DBLCLK
    push dx
    mov cx, ax
    sub ax, [bx + W16W_CS + 12]
    mov [bx + W16W_CS + 12], cx
    cmp dx, [bx + LC_CUR]
    jne .sel
    cmp ax, 500
    ja .sel
    test byte [bx + LC_STYLE], LBS_NOTIFY
    jz .sel
    pop ax
    mov ax, LBN_DBLCLK
    call w16d_notify
    jmp short .z
.sel:
    pop ax
    or byte [bx + LC_FL], LCF_TRACK
    call w16d_lsel
    ICALL w16_rec2hwnd
    mov [w16_capture], ax
.z:
    jmp w16d_lzero

w16d_lmove:
    test byte [bx + LC_FL], LCF_TRACK
    jz .z
    mov ax, [w16_sm_lp + 2]
    call w16d_lhit
    jc .z
    call w16d_lsel
.z:
    jmp w16d_lzero

w16d_lup:
    test byte [bx + LC_FL], LCF_TRACK
    jz .z
    and byte [bx + LC_FL], ~LCF_TRACK
    ICALL w16_rec2hwnd
    cmp [w16_capture], ax
    jne .z
    mov word [w16_capture], 0
.z:
    jmp w16d_lzero

; WM_KEYDOWN: the arrows, Home, End, Page Up and Page Down move the selection
w16d_lkey:
    mov cx, [bx + LC_COUNT]
    jcxz .z
    mov ax, [bx + LC_CUR]
    mov dx, [w16_sm_wp]
    cmp dx, VK_UP
    je .up
    cmp dx, VK_DOWN
    je .down
    cmp dx, VK_HOME
    je .home
    cmp dx, VK_END
    je .end
    cmp dx, VK_PRIOR
    je .pgup
    cmp dx, VK_NEXT
    je .pgdn
.z:
    jmp w16d_lzero
.up:
    cmp ax, -1
    je .home
    or ax, ax
    jz .set
    dec ax
    jmp short .set
.down:
    inc ax
    cmp ax, cx
    jb .set
.end:
    mov ax, cx
    dec ax
    jmp short .set
.home:
    xor ax, ax
    jmp short .set
.pgup:
    push cx
    call w16d_lvis
    sub ax, cx
    pop cx
    jge .set
    xor ax, ax
    jmp short .set
.pgdn:
    push cx
    call w16d_lvis
    add ax, cx
    pop cx
    cmp ax, cx
    jb .set
    jmp short .end
.set:
    call w16d_lsel
    jmp w16d_lzero

; WM_CHAR: the next item after the selection that begins with the letter
w16d_lchar:
    mov al, [w16_sm_wp]
    mov [w16d_sbuf], al
    mov byte [w16d_sbuf + 1], 0
    push word [w16_sm_lp]
    push word [w16_sm_lp + 2]
    push word [w16_sm_wp]
    mov word [w16_sm_lp], w16d_sbuf
    mov [w16_sm_lp + 2], ds
    mov ax, [bx + LC_CUR]
    mov [w16_sm_wp], ax
    call w16d_lfind
    pop word [w16_sm_wp]
    pop word [w16_sm_lp + 2]
    pop word [w16_sm_lp]
    cmp ax, -1
    je .z
    call w16d_lsel
.z:
    jmp w16d_lzero

; -----------------------------------------------------------------------------
; w16d_lpaint - the frame, the items shown (the selection in
; COLOR_HIGHLIGHT, the focus's dotted rectangle round it when the box has
; the focus), white below them, and the scroll bar
; -----------------------------------------------------------------------------
w16d_lpaint:
    call w16d_popen
    jnc .go
    ret
.go:
    xor ax, ax
    test word [bx + W16W_STYLE + 2], WS_BORDER_HI
    jz .nb
    inc ax
    push ax
    mov si, COLOR_WINDOWFRAME
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
    mov ax, [w16d_r]
    mov cx, ax
    call w16d_fill
    pop ax
.nb:
    mov bx, [w16d_rec]
    mov dx, [w16d_l]
    add dx, ax
    mov [w16d_il], dx
    mov dx, [w16d_r]
    sub dx, ax
    mov [w16d_ir], dx
    mov dx, [w16d_t]
    add dx, ax
    mov [w16d_it], dx
    mov dx, [w16d_b]
    sub dx, ax
    mov [w16d_ib], dx
    call w16d_lbar
    jnc .nobar
    call w16d_sbar                  ; the bar, then the items left of it
    sub word [w16d_ir], W16D_SBW
.nobar:
    mov ax, [bx + LC_TOP]
    mov [w16d_e0], ax               ; [w16d_e0] = the item
    mov ax, [w16d_it]
    mov [w16_ty], ax
.item:
    mov ax, [w16_ty]
    cmp ax, [w16d_ib]
    jg .done
    mov ax, [w16d_e0]
    cmp ax, [bx + LC_COUNT]
    jae .rest
    call w16d_lstr                  ; ES:SI, CX
    cmp cx, W16_TXTMAX
    jbe .c
    mov cx, W16_TXTMAX
.c:
    mov [w16_tn], cx
    push ds
    push ds
    push es
    pop ds                          ; DS:SI = the string
    pop es                          ; ES:DI = w16_txt
    mov di, w16_txt
    cld
    rep movsb
    pop ds
    mov ax, (COLOR_WINDOW << 8) | COLOR_WINDOWTEXT
    mov dx, COLOR_WINDOW
    mov cx, [w16d_e0]
    cmp cx, [bx + LC_CUR]
    jne .col
    mov ax, (COLOR_HIGHLIGHT << 8) | COLOR_HIGHLIGHTTEXT
    mov dx, COLOR_HIGHLIGHT
.col:
    call w16d_colors
    mov ax, dx
    call w16d_syscol
    mov [w16_gcol], al
    mov ax, [w16d_il]
    mov [w16_bxl], ax
    add ax, 2
    mov [w16_tx], ax
    mov ax, [w16d_ir]
    mov [w16_bxr], ax
    mov ax, [w16_ty]
    mov [w16_bxt], ax
    push ax
    ICALL w16_rowsn
    mov dx, ax
    pop ax
    add ax, dx
    dec ax
    cmp ax, [w16d_ib]
    jle .bb
    mov ax, [w16d_ib]
.bb:
    mov [w16_bxb], ax
    ICALL w16_tbox
    mov ax, [w16d_e0]               ; the focus round the selection
    cmp ax, [bx + LC_CUR]
    jne .nf
    test byte [bx + LC_FL], LCF_FOCUS
    jz .nf
    mov al, COLOR_HIGHLIGHTTEXT
    call w16d_syscol
    mov [w16_tcol], al
    mov ax, [w16_bxl]
    mov cx, [w16_bxr]
    mov dx, [w16_bxb]
    push bx
    mov bx, [w16_bxt]
    call w16d_focus
    pop bx
.nf:
    inc word [w16d_e0]
    ICALL w16_rowsn
    add [w16_ty], ax
    jmp .item
.rest:
    mov ax, [w16d_il]               ; white below the last
    mov bx, [w16_ty]
    mov cx, [w16d_ir]
    mov dx, [w16d_ib]
    mov si, COLOR_WINDOW
    call w16d_fill
.done:
    call w16d_pclose
    jmp w16d_lzero

; -----------------------------------------------------------------------------
; w16d_sbar - BX = a list box with its bar: the bar down the right of
; [w16d_il]..[w16d_ib] - an arrow box at each end, the track, and the thumb
; where the top item is among them all. Keeps BX.
; -----------------------------------------------------------------------------
w16d_sbar:
    push bx
    mov ax, [w16d_ir]
    sub ax, W16D_SBW - 1
    mov [w16d_bxx], ax              ; the bar's left
    mov si, COLOR_WINDOWFRAME       ; its left edge
    mov bx, [w16d_it]
    mov cx, ax
    mov dx, [w16d_ib]
    call w16d_fill
    mov ax, [w16d_it]               ; the up box
    mov [w16d_by], ax
    mov si, w16d_bmup  ; XSEC-OK: w16d_sbox reads it through cs:
    call w16d_sbox
    mov ax, [w16d_ib]               ; the down box
    sub ax, W16D_SBW - 1
    mov [w16d_by], ax
    mov si, w16d_bmdown  ; XSEC-OK: w16d_sbox reads it through cs:
    call w16d_sbox
    pop bx
    push bx
    mov ax, [w16d_ib]               ; the track: [w16d_e0]..[w16d_e1]
    sub ax, W16D_SBW
    mov [w16d_e1], ax
    mov ax, [w16d_it]
    add ax, W16D_SBW
    mov [w16d_e0], ax
    call w16d_lvis                  ; the thumb's top: the track's top plus
    mov ax, [bx + LC_COUNT]         ;   its free rows x top / (count - shown)
    sub ax, cx
    mov cx, ax                      ; CX = the scroll range
    mov ax, [w16d_e1]
    sub ax, [w16d_e0]
    inc ax
    sub ax, W16D_SBW                ; AX = the rows the thumb can move
    jge .r
    xor ax, ax
.r:
    mul word [bx + LC_TOP]
    or cx, cx
    jz .t0
    div cx
.t0:
    add ax, [w16d_e0]
    mov [w16d_by], ax
    mov si, COLOR_SCROLLBAR         ; the track above it and below it
    mov ax, [w16d_bxx]
    inc ax
    mov bx, [w16d_e0]
    mov cx, [w16d_ir]
    mov dx, [w16d_by]
    dec dx
    call w16d_fill
    mov bx, [w16d_by]
    add bx, W16D_SBW
    mov dx, [w16d_e1]
    call w16d_fill
    xor si, si                      ; the thumb, a plain box
    call w16d_sbox
    pop bx
    ret

; w16d_sbox - a scroll bar box at [w16d_bxx] + 1, [w16d_by], W16D_SBW - 1
; across and W16D_SBW down: a black frame top and bottom, white, and the
; part's 7 x 4 picture at CS:SI in the middle (SI = 0: none). Keeps every
; register.
w16d_sbox:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push si
    mov si, COLOR_WINDOWFRAME
    mov ax, [w16d_bxx]
    inc ax
    mov bx, [w16d_by]
    mov cx, [w16d_ir]
    mov dx, bx
    call w16d_fill                  ; its top
    add bx, W16D_SBW - 1
    mov dx, bx
    call w16d_fill                  ; its bottom
    mov bx, [w16d_by]
    inc bx
    dec dx
    mov si, COLOR_WINDOW
    pop di
    or di, di
    jz .plain
    push dx                         ; white round the picture
    mov dx, [w16d_by]
    add dx, 6
    call w16d_fill                  ; above it
    pop dx
    push bx
    mov bx, [w16d_by]
    add bx, 11
    call w16d_fill                  ; below it
    pop bx
    add bx, 6
    push dx
    mov dx, bx
    add dx, 3
    push cx
    mov cx, ax
    add cx, 3
    call w16d_fill                  ; left of it
    pop cx
    push ax
    add ax, 11
    call w16d_fill                  ; right of it
    pop ax
    pop dx
    push ax                         ; the picture: four rows of two bytes
    mov cx, 4
    mov si, w16d_pbuf
.cp:
    mov ax, [cs:di]
    mov [si], ax
    inc di
    inc di
    inc si
    inc si
    loop .cp
    pop ax
    add ax, 4
    mov [w16_tx], ax
    mov ax, [w16d_by]
    add ax, 7
    mov [w16_ty], ax
    mov ax, (COLOR_WINDOW << 8) | COLOR_WINDOWTEXT
    call w16d_colors
    mov si, w16d_pbuf
    mov cx, 4
    mov dx, 7
    ICALL w16_bmp
    jmp short .out
.plain:
    call w16d_fill
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

w16d_bmup:   db 0x10,0, 0x38,0, 0x7C,0, 0xFE,0
w16d_bmdown: db 0xFE,0, 0x7C,0, 0x38,0, 0x10,0

; =============================================================================
; THE DIALOG MANAGER (SPEC.md 124.13): siblings, templates, the DialogBox
; family, the modal loop, IsDialogMessage, DefDlgProc and MessageBox
; =============================================================================

IDOK        equ 1
IDCANCEL    equ 2
DC_HASDEFID equ 0x534B
WDF_MSGBOX  equ 4               ; WD_FL: MessageBox's, which ends on any
                                ;   WM_COMMAND

; --- siblings, in the order they were made (W16W_SEQ, Windows' Z order) ------

; w16d_sibx - DX = a parent's HWND, SI = a W16W_SEQ, AL = 1 the child made
; next after it / 0 the one made last before it -> BX. CF=1 none. Keeps the
; rest.
w16d_sibx:
    push ax
    push cx
    push di
    mov ah, al
    xor di, di                      ; DI = the best so far
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.r:
    mov al, [bx + W16W_FLAGS]
    and al, W16WF_USED | W16WF_DYING
    cmp al, W16WF_USED
    jne .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [bx + W16W_PARENT], dx
    jne .n
    push dx
    mov dx, [bx + W16W_SEQ]
    or ah, ah
    jz .prev
    cmp dx, si
    jbe .no
    or di, di
    jz .take
    cmp dx, [di + W16W_SEQ]
    jae .no
    jmp short .take
.prev:
    cmp dx, si
    jae .no
    or di, di
    jz .take
    cmp dx, [di + W16W_SEQ]
    jbe .no
.take:
    mov di, bx
.no:
    pop dx
.n:
    add bx, W16W_SIZE
    loop .r
    mov bx, di
    or di, di
    jnz .found
    stc
    jmp short .out
.found:
    clc
.out:
    pop di
    pop cx
    pop ax
    ret

; w16d_sib - BX = a child window, AL = 1 its next sibling / 0 its previous
; -> BX. CF=1 none, BX kept. Keeps the rest.
w16d_sib:
    push dx
    push si
    push bx
    mov dx, [bx + W16W_PARENT]
    mov si, [bx + W16W_SEQ]
    call w16d_sibx
    jc .none
    add sp, 2
    jmp short .out
.none:
    pop bx
.out:
    pop si
    pop dx
    ret

; w16d_kid - AX = a window's HWND, DL = 1 its first child / 0 its last ->
; BX. CF=1 none. Keeps the rest.
w16d_kid:
    push ax
    push dx
    push si
    xor si, si
    or dl, dl
    mov dx, ax                      ; (the flags are DL's)
    mov al, 1
    jnz .go
    dec si
    xor al, al
.go:
    call w16d_sibx
    pop si
    pop dx
    pop ax
    ret

; w16d_grpfirst - BX = a control -> BX = the first of its group: back to one
; with WS_GROUP, or to the first child. Keeps the rest.
w16d_grpfirst:
    push ax
.u:
    test word [bx + W16W_STYLE + 2], WS_GROUP_HI
    jnz .out
    xor al, al
    call w16d_sib
    jnc .u
.out:
    pop ax
    clc
    ret

; w16d_sibnext - BX = a control -> BX = the next of its group. CF=1 it was
; the group's last, and BX is kept. Keeps the rest.
w16d_sibnext:
    push ax
    push bx
    mov al, 1
    call w16d_sib
    jc .end
    test word [bx + W16W_STYLE + 2], WS_GROUP_HI
    jnz .end
    add sp, 2
    pop ax
    clc
    ret
.end:
    pop bx
    pop ax
    stc
    ret

; w16d_takes - BX = a control: CF=0 it can take the focus (visible, and
; neither it nor its dialog disabled). Keeps every register.
w16d_takes:
    test byte [bx + W16W_FLAGS], W16WF_VIS
    jz .no
    ICALL w16_disabled
    ret
.no:
    stc
    ret

; w16d_tab - [w16d_hdlg] = a dialog, BX = a control of it or 0, DL = 1 the
; next WS_TABSTOP control that can take the focus / 0 the previous, round
; the end -> BX. CF=1 none (BX kept). Keeps the rest.
w16d_tab:
    push ax
    push cx
    push si
    mov si, bx
    mov cx, W16_MAXWND + 1
.step:
    or bx, bx
    jz .wrap
    mov al, dl
    call w16d_sib
    jnc .have
.wrap:
    mov ax, [w16d_hdlg]
    call w16d_kid
    jc .none
.have:
    test word [bx + W16W_STYLE + 2], WS_TABSTOP_HI
    jz .next
    call w16d_takes
    jnc .out
.next:
    cmp bx, si
    je .none
    loop .step
.none:
    mov bx, si
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; w16d_grp - BX = a control, DL = 1 the next of its group that can take the
; focus / 0 the previous, round the group -> BX. CF=1 none (BX kept). Keeps
; the rest.
w16d_grp:
    push ax
    push cx
    push si
    mov si, bx
    mov cx, W16_MAXWND + 1
.step:
    or dl, dl
    jz .prev
    call w16d_sibnext
    jnc .have
    call w16d_grpfirst              ; past the group's last: its first
    jmp short .have
.prev:
    test word [bx + W16W_STYLE + 2], WS_GROUP_HI
    jnz .last
    xor al, al
    call w16d_sib
    jnc .have
.last:
    call w16d_sibnext               ; before the group's first: its last
    jnc .last
.have:
    call w16d_takes
    jnc .out
    cmp bx, si
    je .none
    loop .step
.none:
    mov bx, si
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

; w16d_item - [w16d_hdlg] = a dialog, AX = an ID -> BX = its control of that
; ID. CF=1 none. Keeps the rest.
w16d_item:
    push cx
    push dx
    mov dx, [w16d_hdlg]
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.r:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .n
    cmp [bx + W16W_PARENT], dx
    jne .n
    cmp [bx + W16W_MENU], ax
    je .hit
.n:
    add bx, W16W_SIZE
    loop .r
    stc
    jmp short .out
.hit:
    clc
.out:
    pop dx
    pop cx
    ret

; w16d_focusin - [w16d_hdlg] = a dialog -> BX = the control of it that has
; the focus, 0 none. Keeps the rest.
w16d_focusin:
    push ax
    push cx
    mov ax, [w16_focus]
    or ax, ax
    jz .none
    ICALL w16_hwnd2rec
    jc .none
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jz .none
    mov ax, [w16d_hdlg]
    cmp [bx + W16W_PARENT], ax
    je .out
.none:
    xor bx, bx
.out:
    pop cx
    pop ax
    ret

; w16d_cmsg - BX = a control, CX = a message, DX = wParam, SI:DI = lParam:
; sent at once if it is one of WIN16's (it never calls the program) ->
; DX:AX. 0 for any other window. Keeps BX, SI, DI.
w16d_cmsg:
    ICALL w16_isctl
    jc .no
    mov [w16_sm_msg], cx
    mov [w16_sm_wp], dx
    mov [w16_sm_lp], di
    mov [w16_sm_lp + 2], si
    ICALL w16_intproc
    ret
.no:
    xor ax, ax
    xor dx, dx
    ret

; w16d_fcode - BX = a control or 0 -> AX = its WM_GETDLGCODE. Keeps BX.
w16d_fcode:
    push cx
    push dx
    push si
    push di
    xor ax, ax
    or bx, bx
    jz .out
    mov cx, WM_GETDLGCODE
    xor dx, dx
    xor si, si
    xor di, di
    call w16d_cmsg
.out:
    pop di
    pop si
    pop dx
    pop cx
    ret

; w16d_focus2 - BX = a control: it has the focus, and an EDIT's text is all
; selected, as Tab and a mnemonic leave it (SPEC.md 124.13). Keeps BX.
w16d_focus2:
    push ax
    push cx
    push dx
    push si
    push di
    push bx
    ICALL w16_rec2hwnd
    ICALL w16_setfocus
    pop bx
    cmp byte [bx + W16W_KIND], W16Q_EDIT
    jne .out
    mov cx, EM_SETSEL
    xor dx, dx
    xor di, di
    mov si, 0x7FFF
    call w16d_cmsg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; w16d_cmd - [w16d_hdlg] = a dialog, AX = an ID: WM_COMMAND posted to it,
; lParam its control of that ID (if any) and BN_CLICKED. Keeps every register.
w16d_cmd:
    push ax
    push bx
    mov [w16_sm_wp], ax
    call w16d_item
    mov ax, 0
    jc .n
    ICALL w16_rec2hwnd
.n:
    mov [w16_sm_lp], ax
    mov word [w16_sm_lp + 2], BN_CLICKED
    mov ax, [w16d_hdlg]
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_COMMAND
    xor al, al
    ICALL w16_post
    pop bx
    pop ax
    ret

; w16d_upc - AL upper case. Keeps the rest.
w16d_upc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; w16d_hasmn - BX = a control, [w16d_mch] = an upper-case character: CF=1
; its label underlines it (a button's, a static's that takes `&`). Keeps
; every register.
w16d_hasmn:
    push ax
    push cx
    push si
    push es
    mov al, [bx + W16W_KIND]
    cmp al, W16Q_BUTTON
    je .ok
    cmp al, W16Q_STATIC
    jne .no
    mov al, [bx + W16W_STYLE]
    test al, SS_NOPREFIX
    jnz .no
    and al, 0x0F
    cmp al, SS_RIGHT
    jbe .ok
    cmp al, SS_SIMPLE
    jb .no
.ok:
    ICALL w16_textptr               ; ES:SI, CX
    jcxz .no
.c:
    cmp byte [es:si], '&'
    jne .n
    dec cx
    jz .no
    inc si
    mov al, [es:si]
    cmp al, '&'
    je .n
    call w16d_upc
    cmp al, [w16d_mch]
    je .yes
.n:
    inc si
    loop .c
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop es
    pop si
    pop cx
    pop ax
    ret

; w16d_mnem - [w16d_hdlg] = a dialog, AL = a character: the control after
; the focus whose label underlines it. A button is focused and clicked; a
; static or a group box sends the focus to the next control. CF=1 found.
w16d_mnem:
    call w16d_upc
    mov [w16d_mch], al
    call w16d_focusin
    mov si, bx                      ; SI = where it starts
    mov cx, W16_MAXWND + 1
.step:
    or bx, bx
    jz .wrap
    mov al, 1
    call w16d_sib
    jnc .have
.wrap:
    mov ax, [w16d_hdlg]
    mov dl, 1
    call w16d_kid
    jc .none
.have:
    call w16d_hasmn
    jc .hit
    cmp bx, si
    je .none
    loop .step
.none:
    clc
    ret
.hit:
    cmp byte [bx + W16W_KIND], W16Q_BUTTON
    jne .label
    call w16d_bstyle
    cmp al, BS_GROUPBOX
    je .label
    call w16d_takes
    jc .eat
    call w16d_focus2
    call w16d_click
.eat:
    stc
    ret
.label:
    mov dl, 1                       ; a label: the next control takes it
    call w16d_tab
    jc .eat
    call w16d_focus2
    stc
    ret

; -----------------------------------------------------------------------------
; w16d_isdlg - BX = a dialog, [w16_msg] a message for it or its controls:
; the keys a dialog takes (SPEC.md 124.13's table). CF=1 it took it.
; Clobbers everything but BX, BP, DS.
; -----------------------------------------------------------------------------
w16d_isdlg:
    push bx
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
    call w16d_focusin
    mov [w16d_frec], bx
    call w16d_fcode
    mov [w16d_code], ax
    mov ax, [w16_msg + MSG_MSG]
    cmp ax, WM_KEYDOWN
    je .key
    cmp ax, WM_CHAR
    je .char
    cmp ax, WM_SYSCHAR
    je .sys
.no:
    pop bx
    clc
    ret
.yes:
    pop bx
    stc
    ret
.char:
    test byte [w16d_code], DLGC_WANTCHARS
    jnz .no
    mov al, [w16_msg + MSG_WP]
    call w16d_mnem
    jc .yes
    jmp short .no
.sys:
    mov al, [w16_msg + MSG_WP]
    call w16d_mnem
    jmp short .yes
.key:
    mov al, [w16_msg + MSG_WP]
    cmp al, VK_TAB
    je .tab
    cmp al, VK_RETURN
    je .enter
    cmp al, VK_ESCAPE
    je .esc
    mov dl, 0
    cmp al, VK_LEFT
    je .arrow
    cmp al, VK_UP
    je .arrow
    inc dl
    cmp al, VK_RIGHT
    je .arrow
    cmp al, VK_DOWN
    jne .no
.arrow:
    test byte [w16d_code], DLGC_WANTARROWS
    jnz .no
    mov bx, [w16d_frec]
    or bx, bx
    jz .yes
    call w16d_grp
    jc .yes
    call w16d_focus2
    cmp byte [bx + W16W_KIND], W16Q_BUTTON
    jne .yes
    call w16d_bstyle                ; an automatic radio button is checked as
    cmp al, BS_AUTORADIOBUTTON      ;   it is reached
    jne .yes
    test byte [bx + BC_STATE], 3
    jnz .yes
    call w16d_click
    jmp short .yes
.tab:
    test byte [w16d_code], DLGC_WANTTAB
    jnz .no
    mov bl, [w16_msg + MSG_LP + 2]  ; Shift, as it was when Tab was pressed
    xor bh, bh
    mov dl, 1
    test byte [w16_kmods + bx], 3
    jz .fwd
    dec dl
.fwd:
    mov bx, [w16d_frec]
    call w16d_tab
    jc .yes
    call w16d_focus2
    jmp .yes
.enter:
    test word [w16d_code], DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON
    jz .def
    mov bx, [w16d_frec]             ; a push button with the focus: it
    mov ax, [bx + W16W_MENU]
    jmp short .post
.def:
    pop bx                          ; the default push button's, else IDOK
    push bx
    mov ax, [bx + WD_DEFID]
    or ax, ax
    jnz .defid
    mov ax, IDOK
.defid:
    push bx
    call w16d_item
    jc .post0
    ICALL w16_disabled              ; ...when it may be pressed
    pop bx
    jc .yes
    jmp short .post
.post0:
    pop bx
.post:
    call w16d_cmd
    jmp .yes
.esc:
    mov ax, IDCANCEL
    jmp short .post

; -----------------------------------------------------------------------------
; w16d_mrun - the innermost modal dialog's loop as far as it goes without
; waiting (SPEC.md 124.13 step 3): CL = 0 the queue is empty, 1 the dialog
; has gone and AX is DialogBox's answer, 2 a call into the program is
; queued. Clobbers everything but BP and DS.
; -----------------------------------------------------------------------------
W16D_MTURN equ 32               ; messages a turn, then a wake: a window
                                ;   that never validates cannot wedge it
w16d_mrun:
    mov cl, [w16_inmodal]
    xor ch, ch
    jcxz .empty
    dec cx
    ICALL w16_slot2rec
    test byte [bx + WD_FL], WDF_ENDED
    jnz .end
    inc word [w16d_mcnt]
    cmp word [w16d_mcnt], W16D_MTURN
    jbe .turn
    ICALL w16_wakeme                ; the rest on the next pass
    jmp short .empty
.turn:
    push bx
    mov al, 1
    ICALL w16_nextmsg
    pop bx
    jc .empty
    cmp word [w16_msg + MSG_MSG], WM_QUIT
    jne .notquit
    mov byte [w16_quit], 1          ; WM_QUIT ends the dialog, and is left
    or byte [bx + WD_FL], WDF_ENDED ;   for the program's own loop
    jmp short w16d_mrun
.notquit:
    call w16d_isdlg
    jc w16d_mrun
    push es
    push ds
    pop es
    mov bx, w16_msg
    ICALL w16_xlatemsg
    pop es
    call w16d_dispmsg
    jnc w16d_mrun
    mov cl, 2
    ret
.empty:
    xor cl, cl
    ret
.end:
    mov ax, [bx + WD_OWNER]         ; the owner enabled, and the focus its
    or ax, ax
    jz .noown
    push bx
    push ax
    mov dx, 1
    ICALL w16_enable
    pop ax
    ICALL w16_setfocus
    pop bx
.noown:
    mov al, [bx + WD_PREV]
    mov [w16_inmodal], al
    mov dx, [bx + WD_RESULT]
    ICALL w16_rec2hwnd
    mov si, w16d_mfin_c
    ICALL w16_destroyx              ; WM_DESTROY to it and its controls
    cmp byte [w16_cbgo], 0
    jne .call
    mov cl, 1
    ret
.call:
    mov cl, 2
    ret

; w16d_dispmsg - DispatchMessage of [w16_msg] in the modal loop: WIN16's own
; control at once (CF=0), any other window called with w16d_mcont as the
; continuation (CF=1). A message for no window is dropped.
w16d_dispmsg:
    mov ax, [w16_msg + MSG_LP]
    mov [w16_sm_lp], ax
    mov ax, [w16_msg + MSG_LP + 2]
    mov [w16_sm_lp + 2], ax
    mov ax, [w16_msg + MSG_WP]
    mov [w16_sm_wp], ax
    mov ax, [w16_msg + MSG_MSG]
    mov [w16_sm_msg], ax
    mov ax, [w16_msg + MSG_HWND]
    mov [w16_sm_hwnd], ax
    ICALL w16_hwnd2rec
    jc .drop
    ICALL w16_isctl
    jnc .ctl
    mov si, w16d_mcont_c
    xor ax, ax
    ICALL w16_sendwnd
    stc
    ret
.ctl:
    ICALL w16_intproc
.drop:
    clc
    ret

; w16d_mstep - DCALLed by w16_mkick, from a callback: w16d_mrun, keeping SI,
; DI, BP and ES
w16d_mstep:
    push si
    push di
    push bp
    push es
    mov word [w16d_mcnt], 0
    call w16d_mrun
    pop es
    pop bp
    pop di
    pop si
    retf

; w16d_mcont - the modal loop's continuation, as DialogBox's handler: a
; message the program's procedure answered, so on with the loop
DCONT w16d_mcont
    call w16d_mrun
    cmp cl, 1
    je .done
    cmp cl, 2
    je .out
    mov byte [w16_pend], 1          ; the queue is empty: DialogBox waits
.out:
    retf
.done:
    xor dx, dx
    retf

; w16d_mfin - the last WM_DESTROY answered: DialogBox answers AX
DCONT w16d_mfin
    xor dx, dx
    retf

; =============================================================================
; Making a dialog
; =============================================================================

; w16d_tstr - ES:SI = a template string (`FFh` and a word, or NUL-ended) ->
; DX:AX = it (DX = 0: AX is the word), SI past it. Keeps the rest.
w16d_tstr:
    cmp byte [es:si], 0xFF
    jne .sz
    mov ax, [es:si + 1]
    xor dx, dx
    add si, 3
    ret
.sz:
    mov ax, si
    mov dx, es
.c:
    cmp byte [es:si], 0
    je .e
    inc si
    jmp short .c
.e:
    inc si
    ret

; w16d_dux / w16d_duy - AX = dialog units across / down -> AX = pixels
; (SPEC.md 124.13: 8 / 4 across, the face's rows / 8 down). Keep the rest.
w16d_dux:
    sal ax, 1
    ret
w16d_duy:
    push cx
    push dx
    push ax
    ICALL w16_rowsn
    mov cx, ax
    pop ax
    imul cx
    mov cx, 8
    idiv cx
    pop dx
    pop cx
    ret

; w16d_mkctl - one of WIN16's controls from [w16_cw*] (the kind, style,
; place, size, ID and text set; the rest is filled here) in the dialog
; [w16d_dlg]: made, disabled if its style says, told WM_CREATE, and the
; dialog's default if it is a default push button. CF=1 refused.
w16d_mkctl:
    mov byte [w16_cwcls], 0xFF
    ICALL w16_ctlstub
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    mov word [w16_cwhbr], COLOR_WINDOW + 1
    or word [w16_cwstyle + 2], WS_CHILD_HI
    mov ax, [w16d_hdlg]
    mov [w16_cwpar], ax
    ICALL w16_mkwnd                 ; BX = it
    jc .out
    test word [bx + W16W_STYLE + 2], WS_DISABLED_HI
    jz .en
    or byte [bx + W16W_FLAGS], W16WF_DIS
.en:
    mov word [w16_sm_msg], WM_CREATE
    xor ax, ax
    mov [w16_sm_wp], ax
    mov [w16_sm_lp], ax
    mov [w16_sm_lp + 2], ax
    ICALL w16_intproc
    and ax, dx
    inc ax
    jz .refused
    cmp byte [bx + W16W_KIND], W16Q_BUTTON
    jne .ok
    call w16d_bstyle
    cmp al, BS_DEFPUSHBUTTON
    jne .ok
    mov ax, [bx + W16W_MENU]
    mov bx, [w16d_dlg]
    mov [bx + WD_DEFID], ax
.ok:
    clc
.out:
    ret
.refused:
    ICALL w16_wndfree
    stc
    ret

; w16d_mkitem - the item at [w16d_tseg]:[w16d_tp] made, [w16d_tp] moved past
; it. CF=1 refused: a class WIN16 has not got (SPEC.md 124.13), or no room.
w16d_mkitem:
    push es
    mov es, [w16d_tseg]
    mov si, [w16d_tp]
    mov ax, [es:si]
    call w16d_dux
    mov [w16_cwx], ax
    mov ax, [es:si + 2]
    call w16d_duy
    mov [w16_cwy], ax
    mov ax, [es:si + 4]
    call w16d_dux
    mov [w16_cwcx], ax
    mov ax, [es:si + 6]
    call w16d_duy
    mov [w16_cwcy], ax
    mov ax, [es:si + 8]
    mov [w16_cwid], ax
    mov ax, [es:si + 10]
    mov [w16_cwstyle], ax
    mov ax, [es:si + 12]
    mov [w16_cwstyle + 2], ax
    add si, 14
    mov al, [es:si]
    test al, 0x80
    jz .named
    inc si
    sub al, 0x80 - W16Q_BUTTON      ; BUTTON EDIT STATIC LISTBOX are 80h-83h,
    cmp al, W16Q_LIST               ;   which is W16Q_BUTTON.. in order
    ja .refuse
    jmp short .kind
.named:
    mov ax, si
    mov dx, es
    call w16d_tstr
    push si
    ICALL w16_farname
    jc .refuse1
    ICALL w16_builtin               ; AL = the kind
    jc .refuse1
    pop si
    mov es, [w16d_tseg]
.kind:
    mov [w16_cwkind], al
    call w16d_tstr                  ; its text
    mov word [w16d_icon], 0
    cmp byte [w16_cwkind], W16Q_STATIC
    jne .noi
    mov cl, [w16_cwstyle]
    and cl, 0x0F
    cmp cl, SS_ICON
    jne .noi
    call w16_icload                ; SS_ICON: the text names the icon, and
    mov [w16d_icon], ax             ;   the control is the icon's size, as
    mov word [w16_cwcx], 32         ;   Windows sized it (SPEC.md 124.18.4)
    mov word [w16_cwcy], 32
    xor dx, dx
.noi:
    or dx, dx
    jnz .t
    xor ax, ax                      ; an ordinal: no text
.t:
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], dx
    mov al, [es:si]                 ; the extra bytes, skipped
    inc si
    xor ah, ah
    add si, ax
    mov [w16d_tp], si
    pop es
    cmp byte [w16_cwkind], W16Q_LIST
    jne .mk
    test word [w16_cwstyle + 2], WS_BORDER_HI
    jz .integral
    dec word [w16_cwx]              ; a list box's border lies outside the
    dec word [w16_cwy]              ;   rectangle it asked for
    add word [w16_cwcx], 2
    add word [w16_cwcy], 2
.integral:
    test word [w16_cwstyle], LBS_NOINTEGRALHEIGHT
    jnz .mk
    xor cx, cx                      ; whole items high
    test word [w16_cwstyle + 2], WS_BORDER_HI
    jz .nb
    mov cx, 2
.nb:
    ICALL w16_rowsn
    mov di, ax
    mov ax, [w16_cwcy]
    sub ax, cx
    jle .mk
    xor dx, dx
    div di
    or ax, ax
    jnz .rows
    inc ax
.rows:
    mul di
    add ax, cx
    mov [w16_cwcy], ax
.mk:
    call w16d_mkctl
    jc .mkx
    mov ax, [w16d_icon]
    or ax, ax                       ; (CF = 0)
    jz .mkx
    mov [bx + SC_ICON], ax
.mkx:
    ret
.refuse1:
    pop si
.refuse:
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; w16d_mkdlg - the dialog in the template at [w16d_tseg]:[w16d_tp], owned by
; [w16d_par], its procedure [w16d_func]: it and its controls made, hidden,
; with nothing sent but the controls' WM_CREATE. Out BX = its record. CF=1
; refused, and nothing is left of it.
; -----------------------------------------------------------------------------
w16d_mkdlg:
    push es
    mov es, [w16d_tseg]
    mov si, [w16d_tp]
    mov ax, [es:si]
    mov [w16d_dsty], ax
    mov ax, [es:si + 2]
    mov [w16d_dsty + 2], ax
    mov al, [es:si + 4]
    xor ah, ah
    mov [w16d_cnt], ax
    mov ax, [es:si + 5]
    call w16d_dux
    mov [w16_cwx], ax
    mov ax, [es:si + 7]
    call w16d_duy
    mov [w16_cwy], ax
    mov ax, [es:si + 9]
    call w16d_dux
    mov [w16_cwcx], ax
    mov ax, [es:si + 11]
    call w16d_duy
    mov [w16_cwcy], ax
    add si, 13
    call w16d_tstr                  ; the menu: a dialog that is the program's
    mov [w16d_menu], ax             ;   own window has it (w16d_ownwin)
    mov [w16d_menu + 2], dx
    call w16d_tstr                  ; the class
    mov [w16d_cls], ax
    mov [w16d_cls + 2], dx
    call w16d_tstr                  ; the caption
    or dx, dx
    jnz .cap
    xor ax, ax
.cap:
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], dx
    test byte [w16d_dsty], DS_SETFONT
    jz .nofont
    add si, 2                       ; the font: read, and not used (SPEC.md
    call w16d_tstr                  ;   124.13)
.nofont:
    mov [w16d_tp], si
    mov byte [w16_cwkind], W16Q_DIALOG
    mov byte [w16_cwcls], 0xFF
    ICALL w16_ddpstub
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    mov word [w16_cwhbr], COLOR_WINDOW + 1
    mov ax, [w16d_cls]              ; a class of the program's own, whose
    mov dx, [w16d_cls + 2]          ;   procedure calls DefDlgProc
    or dx, dx
    jz .style
    mov bx, ax
    cmp byte [es:bx], 0
    je .style
    ICALL w16_farname
    jc .fail
    ICALL w16_findcls               ; BX = the class
    jc .fail
    mov ax, bx
    sub ax, w16_cls
    mov dl, W16C_SIZE
    div dl
    mov [w16_cwcls], al
    mov ax, [bx + W16C_PROC]
    mov [w16_cwproc], ax
    mov ax, [bx + W16C_PROC + 2]
    mov [w16_cwproc + 2], ax
    mov ax, [bx + W16C_HBR]
    mov [w16_cwhbr], ax
.style:
    mov ax, [w16d_dsty]
    mov [w16_cwstyle], ax
    mov ax, [w16d_dsty + 2]
    and ax, ~WS_VISIBLE_HI          ; shown after WM_INITDIALOG
    mov [w16_cwstyle + 2], ax
    mov word [w16_cwid], 0
    mov ax, [w16d_par]
    mov [w16_cwpar], ax
    test word [w16d_dsty + 2], WS_CHILD_HI
    jnz .mk                         ; a child: in its parent's client
    xor cx, cx                      ; a top-level: from its owner's client
    xor dx, dx
    test byte [w16d_dsty], DS_ABSALIGN
    jnz .org
    mov ax, [w16d_par]
    or ax, ax
    jz .org
    ICALL w16_hwnd2rec
    jc .org
    ICALL w16_origin                ; AX/DX = its client's top left
    jc .org0
    mov cx, ax
    jmp short .org
.org0:
    xor cx, cx
    xor dx, dx
.org:
    add [w16_cwx], cx
    add [w16_cwy], dx
.mk:
    ICALL w16_mkwnd                 ; BX = it
    jc .fail
    mov [w16d_dlg], bx
    test word [w16d_dsty + 2], WS_CHILD_HI
    jnz .notown
    mov ax, [bx + W16W_OSW]
    cmp ax, [w16_win]
    jne .notown
    call w16d_ownwin                ; the program's first window: WIN16's own
.notown:
    mov ax, [w16d_dsty + 2]         ; its style as the template has it
    mov [bx + W16W_STYLE + 2], ax
    mov ax, [w16d_func]
    mov [bx + W16W_EXTRA + DWL_DLGPROC], ax
    mov ax, [w16d_func + 2]
    mov [bx + W16W_EXTRA + DWL_DLGPROC + 2], ax
    test word [w16d_dsty + 2], WS_DISABLED_HI
    jz .en
    or byte [bx + W16W_FLAGS], W16WF_DIS
.en:
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
.item:
    cmp word [w16d_cnt], 0
    je .done
    call w16d_mkitem
    jc .undo
    dec word [w16d_cnt]
    jmp short .item
.done:
    mov bx, [w16d_dlg]
    pop es
    clc
    ret
.undo:
    mov bx, [w16d_dlg]
    ICALL w16_freetree
.fail:
    pop es
    stc
    ret

; -----------------------------------------------------------------------------
; w16d_ownwin - BX = a dialog that is the program's first top-level window,
; so WIN16's own (CALC's, made by CreateDialog with no parent): its menu,
; the template's or else its class's, as CreateWindow gives one, and WIN16's
; window the size of its client, its caption and its menu bar - before
; anything can ask GetWindowRect (SPEC.md 124.13). Keeps BX, ES.
; -----------------------------------------------------------------------------
w16d_ownwin:
    push bx
    push es
    mov ax, [w16d_menu]
    mov dx, [w16d_menu + 2]
    or dx, dx
    jnz .tstr
    or ax, ax                       ; an ordinal, or none
    jnz .load
    jmp short .cls
.tstr:
    push si
    mov es, dx
    mov si, ax
    cmp byte [es:si], 0
    pop si
    jne .load
.cls:
    mov al, [bx + W16W_CLS]         ; the class's, by name
    cmp al, 0xFF
    je .size
    mov ah, W16C_SIZE
    mul ah
    mov di, ax
    add di, w16_cls
    mov ax, [di + W16C_MENU]
    mov dx, [di + W16C_MENU + 2]
    mov cx, ax
    or cx, dx
    jz .size
.load:
    call w16_mload                 ; AX = the HMENU, 0 = none
    mov [bx + W16W_MENU], ax
.size:
    xor ax, ax                      ; first without the bar, whose rows
    call .to                        ;   depend on the width it is laid in
    call w16_mbarh                 ; AX = its rows
    or ax, ax
    jz .out
    call .to
.out:
    pop es
    pop bx
    ret
; .to - AX = the menu bar's rows: WIN16's window takes the client, that bar,
; the caption and a thin frame (w16_sizeto's outer size)
.to:
    push bx
    mov cx, [bx + W16W_CX]
    add cx, 2
    mov dx, [bx + W16W_CY]
    add dx, 2
    add dx, [w16_capn]
    add dx, ax
    mov ax, [bx + W16W_STYLE + 2]
    ICALL w16_sizeto
    pop bx
    ret

; -----------------------------------------------------------------------------
; The DialogBox and CreateDialog family (SPEC.md 124.13). Each sets
; [w16d_func], [w16d_par], [w16d_param], the template and whether it is
; modal, and goes to w16d_make.
; -----------------------------------------------------------------------------

; DialogBox (USER.87) - args: +0 lpDialogFunc, +4 hWndParent,
; +6 lpTemplateName, +10 hInst
w16u_dlgbox_d:
    mov byte [w16d_modal], 1
    jmp short w16d_noparam
; CreateDialog (USER.89) - args: as DialogBox
w16u_credlg_d:
    mov byte [w16d_modal], 0
w16d_noparam:
    xor ax, ax
    mov [w16d_param], ax
    mov [w16d_param + 2], ax
    ICALL w16_args
    jmp short w16d_byname
; DialogBoxParam (USER.239) - args: +0 dwInitParam, then as DialogBox
w16u_dlgboxp_d:
    mov byte [w16d_modal], 1
    jmp short w16d_param4
; CreateDialogParam (USER.241) - args: as DialogBoxParam
w16u_credlgp_d:
    mov byte [w16d_modal], 0
w16d_param4:
    ICALL w16_args
    call w16d_getparam
w16d_byname:
    call w16d_getfp                 ; the procedure and the parent
    mov ax, [es:bx + 6]             ; the template, by name
    mov dx, [es:bx + 8]
    ICALL w16_dlgres                ; ES:0 = it, BX = its entry
    jc w16d_failed
    mov [w16d_tseg], es
    mov word [w16d_tp], 0
    mov [w16d_rent], bx
    jmp w16d_make

; DialogBoxIndirect (USER.218) - args: +0 lpDialogFunc, +4 hWndParent,
; +6 hDialogTemplate (a global handle), +8 hInst
w16u_dlgboxind_d:
    mov byte [w16d_modal], 1
    xor ax, ax
    mov [w16d_param], ax
    mov [w16d_param + 2], ax
    ICALL w16_args
    jmp short w16d_byhandle
; DialogBoxIndirectParam (USER.240) - args: +0 dwInitParam, then as
; DialogBoxIndirect
w16u_dlgboxindp_d:
    mov byte [w16d_modal], 1
    ICALL w16_args
    call w16d_getparam
w16d_byhandle:
    call w16d_getfp
    mov ax, [es:bx + 6]
    ICALL w16_gfind                 ; ES = its block's header
    jc w16d_failed
    mov ax, es
    inc ax
    mov [w16d_tseg], ax
    mov word [w16d_tp], 0
    mov word [w16d_rent], 0
    jmp short w16d_make

; CreateDialogIndirect (USER.219) - args: +0 lpDialogFunc, +4 hWndParent,
; +6 lpDialogTemplate (far), +10 hInst
w16u_credlgind_d:
    mov byte [w16d_modal], 0
    xor ax, ax
    mov [w16d_param], ax
    mov [w16d_param + 2], ax
    ICALL w16_args
    jmp short w16d_byptr
; CreateDialogIndirectParam (USER.242) - args: +0 dwInitParam, then as
; CreateDialogIndirect
w16u_credlgindp_d:
    mov byte [w16d_modal], 0
    ICALL w16_args
    call w16d_getparam
w16d_byptr:
    call w16d_getfp
    mov ax, [es:bx + 6]
    mov [w16d_tp], ax
    mov ax, [es:bx + 8]
    mov [w16d_tseg], ax
    mov word [w16d_rent], 0
    jmp short w16d_make

; w16d_getparam - ES:BX = the args: [w16d_param] from +0, and ES:BX moved
; past it
w16d_getparam:
    mov ax, [es:bx]
    mov [w16d_param], ax
    mov ax, [es:bx + 2]
    mov [w16d_param + 2], ax
    add bx, 4
    ret

; w16d_getfp - ES:BX = the args from the procedure on: [w16d_func] and
; [w16d_par]
w16d_getfp:
    mov ax, [es:bx]
    mov [w16d_func], ax
    mov ax, [es:bx + 2]
    mov [w16d_func + 2], ax
    mov ax, [es:bx + 4]
    mov [w16d_par], ax
    ret

; w16d_failed - the dialog could not be made: DialogBox answers -1 and
; CreateDialog NULL (SPEC.md 124.13)
w16d_failed:
    xor ax, ax
    xor dx, dx
    cmp byte [w16d_modal], 0
    je .out
    dec ax
.out:
    retf

; -----------------------------------------------------------------------------
; w16d_make - the template found: the dialog made, then WM_INITDIALOG sent
; with the first control that takes Tab and [w16d_param], w16d_init after
; -----------------------------------------------------------------------------
w16d_make:
    call w16d_mkdlg                 ; BX = it
    pushf
    push bx
    mov bx, [w16d_rent]
    or bx, bx
    jz .norel
    ICALL w16_dlgrel
.norel:
    pop bx
    popf
    jc w16d_failed
    cmp byte [w16d_modal], 0
    je .ml
    or byte [bx + WD_FL], WDF_MODAL
.ml:
    ; fall into w16d_initsend

; w16d_initsend - BX = a dialog just made: WM_INITDIALOG, with the first
; control that takes Tab and [w16d_param], and w16d_init after it
w16d_initsend:
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
    mov [w16_sm_hwnd], ax
    push bx
    xor bx, bx
    mov dl, 1
    call w16d_tab
    mov ax, 0
    jc .none
    ICALL w16_rec2hwnd
.none:
    pop bx
    mov [w16_sm_wp], ax
    mov word [w16_sm_msg], WM_INITDIALOG
    mov ax, [w16d_param]
    mov [w16_sm_lp], ax
    mov ax, [w16d_param + 2]
    mov [w16_sm_lp + 2], ax
    ICALL w16_rec2hwnd              ; the continuation's word: its slot
    sub ax, W16_HWND0
    shr ax, 1
    shr ax, 1
    mov si, w16d_init_c
    ICALL w16_sendwnd
    retf

; w16d_init - WM_INITDIALOG answered (AX:DX), CX = the dialog's slot: an
; answer that is not 0 puts the focus on the first control that takes Tab;
; the dialog is shown, and a modal one's loop starts (SPEC.md 124.13)
DCONT w16d_init
    push ax
    ICALL w16_slot2rec
    pop ax
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .gone
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    jne .gone
    push ax
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
    pop ax
    or ax, ax                       ; a BOOL: AX alone
    jnz .first
    test byte [bx + WD_FL], WDF_MSGBOX
    jz .focused
    mov ax, [bx + WD_DEFID]         ; a message box: its default button
    push bx
    call w16d_item
    jc .mbno
    call w16d_focus2
.mbno:
    pop bx
    jmp short .focused
.first:
    push bx
    xor bx, bx
    mov dl, 1
    call w16d_tab
    jc .nofirst
    call w16d_focus2
.nofirst:
    pop bx
.focused:
    push bx                         ; no control has it: the dialog has
    call w16d_focusin
    or bx, bx
    pop bx
    jnz .titled
    mov ax, [w16d_hdlg]
    push bx
    ICALL w16_setfocus
    pop bx
.titled:
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .show
    mov ax, [bx + W16W_STYLE + 2]   ; no caption: its owner's title
    and ax, WS_CAPTION_HI
    cmp ax, WS_CAPTION_HI
    je .show
    mov ax, [bx + W16W_PARENT]
    or ax, ax
    jz .show
    ICALL w16_ttlfrom
.show:
    test byte [bx + WD_FL], WDF_ENDED
    jnz .noshow                     ; EndDialog in WM_INITDIALOG
    test byte [bx + WD_FL], WDF_MODAL
    jnz .shown
    test word [bx + W16W_STYLE + 2], WS_VISIBLE_HI
    jz .noshow
.shown:
    ICALL w16_showwnd
.noshow:
    test byte [bx + WD_FL], WDF_MODAL
    jnz .modal
    mov ax, [w16d_hdlg]             ; CreateDialog answers it
    xor dx, dx
    retf
.modal:
    test word [bx + W16W_STYLE + 2], WS_CHILD_HI
    jnz .noown
    mov ax, [bx + W16W_PARENT]      ; the owner's top-level window disabled,
    or ax, ax                       ;   as Windows disabled it
    jz .noown
    push bx
    ICALL w16_hwnd2rec
    jc .noown1
    ICALL w16_toplevel
    test byte [bx + W16W_FLAGS], W16WF_DIS
    jnz .noown1
    ICALL w16_rec2hwnd
    mov cx, ax
    pop bx
    mov [bx + WD_OWNER], cx
    push bx
    mov ax, cx
    xor dx, dx
    ICALL w16_enable
.noown1:
    pop bx
.noown:
    mov al, [w16_inmodal]           ; the loop's state, in its record
    mov [bx + WD_PREV], al
    ICALL w16_rec2hwnd
    sub ax, W16_HWND0
    shr ax, 1
    shr ax, 1
    inc al
    mov [w16_inmodal], al
    mov ax, [w16_idx]
    mov [bx + WD_WIDX], ax
    mov ax, [w16_nargb]
    mov [bx + WD_WNARGB], ax
    mov ax, [w16_psp_]
    mov [bx + WD_WPSP], ax
    mov word [w16d_mcnt], 0
    jmp w16d_mcont
.gone:
    xor ax, ax                      ; it was destroyed in WM_INITDIALOG
    xor dx, dx
    retf

; -----------------------------------------------------------------------------
; The calls on a dialog (SPEC.md 124.13)
; -----------------------------------------------------------------------------

; EndDialog (USER.88) - args: +0 nResult, +2 hDlg. The dialog is hidden at
; once; its loop ends it at its next step.
w16u_enddlg_d:
    ICALL w16_args
    mov dx, [es:bx]
    mov ax, [es:bx + 2]
    ICALL w16_hwnd2rec
    jc w16d_zero
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    jne w16d_zero
    mov [bx + WD_RESULT], dx
    or byte [bx + WD_FL], WDF_ENDED
    ICALL w16_hidewnd
w16d_one:
    mov ax, 1
    xor dx, dx
    retf
w16d_zero:
    xor ax, ax
    xor dx, dx
    retf

; IsDialogMessage (USER.90) - args: +0 lpMsg, +4 hDlg
w16u_isdlgmsg_d:
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov [w16d_hdlg], ax
    mov si, [es:bx]
    mov es, [es:bx + 2]
    mov di, w16_msg
    mov cx, MSG_SIZE / 2
.cp:
    mov ax, [es:si]
    mov [di], ax
    inc si
    inc si
    inc di
    inc di
    loop .cp
    mov ax, [w16_msg + MSG_HWND]    ; a message for it or one of its controls
    ICALL w16_hwnd2rec
    jc w16d_zero
    mov ax, [w16d_hdlg]
    ICALL w16_inside
    jnc w16d_zero
    ICALL w16_hwnd2rec              ; BX = the dialog
    jc w16d_zero
    call w16d_isdlg
    jc w16d_one
    push ds
    pop es
    mov bx, w16_msg
    ICALL w16_xlatemsg
    mov ax, [w16_msg + MSG_LP]      ; ...dispatched, and the answer is 1
    mov [w16_sm_lp], ax
    mov ax, [w16_msg + MSG_LP + 2]
    mov [w16_sm_lp + 2], ax
    mov ax, [w16_msg + MSG_WP]
    mov [w16_sm_wp], ax
    mov ax, [w16_msg + MSG_MSG]
    mov [w16_sm_msg], ax
    mov ax, [w16_msg + MSG_HWND]
    mov [w16_sm_hwnd], ax
    ICALL w16_hwnd2rec
    jc w16d_one
    mov si, w16_cont_one
    ICALL w16_sendwnd
    retf

; w16d_dlgid - ES:BX = the args, [es:bx + DI] the dialog and [es:bx + DI - 2]
; the ID -> BX = its control. CF=1 none. Keeps ES.
w16d_dlgid:
    mov ax, [es:bx + di]
    mov [w16d_hdlg], ax
    mov ax, [es:bx + di - 2]
    push es
    call w16d_item
    pop es
    ret

; GetDlgItem (USER.91) - args: +0 nIDDlgItem, +2 hDlg
w16u_getdlgitem_d:
    ICALL w16_args
    mov di, 2
    call w16d_dlgid
    jc w16d_zero
    ICALL w16_rec2hwnd
    xor dx, dx
    retf

; w16d_send - BX = a control, [w16_sm_msg]/[w16_sm_wp]/[w16_sm_lp] set: its
; procedure, and the call answers what it answers
w16d_send:
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov si, w16_cont_ret
    ICALL w16_sendwnd
    retf

; SetDlgItemText (USER.92) - args: +0 lpString, +4 nIDDlgItem, +6 hDlg
w16u_setdlgtext_d:
    ICALL w16_args
    mov di, 6
    call w16d_dlgid
    jc w16d_zero
    mov word [w16_sm_msg], WM_SETTEXT
    mov word [w16_sm_wp], 0
w16d_lp0:
    push bx
    call w16d_argslp0
    pop bx
    jmp short w16d_send

; GetDlgItemText (USER.93) - args: +0 nMaxCount, +2 lpString, +6 nIDDlgItem,
; +8 hDlg
w16u_getdlgtext_d:
    ICALL w16_args
    mov di, 8
    call w16d_dlgid
    jc w16d_zero
    mov word [w16_sm_msg], WM_GETTEXT
    push bx
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_sm_wp], ax
    mov ax, [es:bx + 2]
    mov [w16_sm_lp], ax
    mov ax, [es:bx + 4]
    mov [w16_sm_lp + 2], ax
    pop bx
    jmp short w16d_send

; w16d_argslp0 - [w16_sm_lp] = the far pointer at the args' +0
w16d_argslp0:
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16_sm_lp], ax
    mov ax, [es:bx + 2]
    mov [w16_sm_lp + 2], ax
    ret

; SendDlgItemMessage (USER.101) - args: +0 lParam, +4 wParam, +6 msg,
; +8 nIDDlgItem, +10 hDlg
w16u_senddlgmsg_d:
    ICALL w16_args
    mov di, 10
    call w16d_dlgid
    jc w16d_zero
    push bx
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov [w16_sm_wp], ax
    mov ax, [es:bx + 6]
    mov [w16_sm_msg], ax
    pop bx
    jmp w16d_lp0

; SetDlgItemInt (USER.94) - args: +0 bSigned, +2 uValue, +4 nIDDlgItem,
; +6 hDlg
w16u_setdlgint_d:
    ICALL w16_args
    mov di, 6
    call w16d_dlgid
    jc w16d_zero
    push bx
    ICALL w16_args
    mov ax, [es:bx + 2]
    mov cx, [es:bx]
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in SI
    mov di, w16d_nbuf
    or cx, cx
    jz .u
    or ax, ax
    jns .u
    mov byte [di], '-'
    inc di
    neg ax
.u:
    mov cx, 10                      ; the digits, backwards onto the stack
    xor si, si
.d:
    xor dx, dx
    div cx
    push dx
    inc si
    or ax, ax
    jnz .d
.o:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    dec si
    jnz .o
    mov byte [di], 0
    pop bx
    mov word [w16_sm_msg], WM_SETTEXT
    mov word [w16_sm_wp], 0
    mov word [w16_sm_lp], w16d_nbuf
    mov [w16_sm_lp + 2], ds
    jmp w16d_send

; GetDlgItemInt (USER.95) - args: +0 bSigned, +2 lpTranslated, +6
; nIDDlgItem, +8 hDlg. The text is read into [w16d_nbuf] and read there.
w16u_getdlgint_d:
    ICALL w16_args
    mov di, 8
    call w16d_dlgid
    jnc .have
    mov byte [w16d_nbuf], 0         ; no control: nothing to read
    xor ax, ax
    xor dx, dx
    jmp short w16d_gdint_
.have:
    ICALL w16_rec2hwnd
    mov [w16_sm_hwnd], ax
    mov word [w16_sm_msg], WM_GETTEXT
    mov word [w16_sm_wp], W16D_NBUF
    mov word [w16_sm_lp], w16d_nbuf
    mov [w16_sm_lp + 2], ds
    mov si, w16d_gdint_c
    xor ax, ax
    ICALL w16_sendwnd
    retf

; w16d_gdint - the text in [w16d_nbuf]: GetDlgItemInt's answer, and its
; lpTranslated
DCONT w16d_gdint
w16d_gdint_:
    push bp
    mov si, w16d_nbuf
.sp:
    cmp byte [si], ' '
    jne .nsp
    inc si
    jmp short .sp
.nsp:
    xor bp, bp                      ; BP = 1 negative
    ICALL w16_args
    cmp word [es:bx], 0
    je .digits
    cmp byte [si], '-'
    jne .digits
    inc si
    inc bp
.digits:
    xor ax, ax                      ; AX = the value, CX = digits read
    xor cx, cx
.d:
    mov dl, [si]
    sub dl, '0'
    jb .end
    cmp dl, 9
    ja .end
    mov di, 10
    push dx
    mul di
    pop di
    jc .bad                         ; DX set: past 65535
    and di, 0xFF
    add ax, di
    jc .bad
    inc si
    inc cx
    jmp short .d
.end:
    cmp byte [si], 0                ; only spaces may follow
    je .ok
    cmp byte [si], ' '
    jne .bad
    inc si
    jmp short .end
.ok:
    jcxz .bad
    ICALL w16_args
    cmp word [es:bx], 0
    je .good
    cmp ax, 0x8000                  ; signed: -32768..32767
    ja .bad
    jb .sok
    or bp, bp
    jz .bad
.sok:
    or bp, bp
    jz .good
    neg ax
.good:
    mov dx, 1
    jmp short .tell
.bad:
    xor ax, ax
    xor dx, dx
.tell:
    push ax
    ICALL w16_args
    les di, [es:bx + 2]
    mov cx, es
    or cx, di
    jz .nt
    mov [es:di], dx
.nt:
    pop ax
    xor dx, dx
    pop bp
    retf

; CheckDlgButton (USER.97) - args: +0 nCheck, +2 nIDButton, +4 hDlg
w16u_checkdlg_d:
    ICALL w16_args
    mov dx, [es:bx]
    push dx
    mov di, 4
    call w16d_dlgid
    pop dx
    jc w16d_zero
    jmp short w16d_setcheck

; IsDlgButtonChecked (USER.98) - args: +0 nIDButton, +2 hDlg
w16u_isdlgcheck_d:
    ICALL w16_args
    mov di, 2
    call w16d_dlgid
    jc w16d_zero
    mov cx, BM_GETCHECK
    xor dx, dx
    xor si, si
    xor di, di
    call w16d_cmsg
    retf

; w16d_setcheck - BX = a button, DX = BM_SETCHECK's wParam
w16d_setcheck:
    mov cx, BM_SETCHECK
    xor si, si
    xor di, di
    call w16d_cmsg
    jmp w16d_zero

; CheckRadioButton (USER.96) - args: +0 nIDCheck, +2 nIDLast, +4 nIDFirst,
; +6 hDlg: each from the first to the last checked if it is nIDCheck, else
; not
w16u_checkradio_d:
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov [w16d_hdlg], ax
    mov ax, [es:bx + 4]
.id:
    ICALL w16_args
    cmp ax, [es:bx + 2]
    ja .out
    push ax
    xor dx, dx
    cmp ax, [es:bx]
    jne .set
    inc dx
.set:
    call w16d_item
    jc .n
    mov cx, BM_SETCHECK
    xor si, si
    xor di, di
    call w16d_cmsg
.n:
    pop ax
    inc ax
    jnz .id
.out:
    jmp w16d_zero

; MapDialogRect (USER.103) - args: +0 lpRect, +4 hDlg
w16u_mapdlgrect_d:
    ICALL w16_args
    les di, [es:bx]
    mov ax, [es:di]
    call w16d_dux
    mov [es:di], ax
    mov ax, [es:di + 4]
    call w16d_dux
    mov [es:di + 4], ax
    mov ax, [es:di + 2]
    call w16d_duy
    mov [es:di + 2], ax
    mov ax, [es:di + 6]
    call w16d_duy
    mov [es:di + 6], ax
    jmp w16d_zero

; GetNextDlgGroupItem (USER.227), GetNextDlgTabItem (USER.228) - args:
; +0 bPrevious, +2 hCtl, +4 hDlg
w16u_nextgroup_d:
    mov byte [w16d_tabq], 0
    jmp short w16d_next
w16u_nexttab_d:
    mov byte [w16d_tabq], 1
w16d_next:
    ICALL w16_args
    mov ax, [es:bx + 4]
    mov [w16d_hdlg], ax
    mov dl, 1
    cmp word [es:bx], 0
    je .dir
    dec dl
.dir:
    mov ax, [es:bx + 2]
    push dx
    xor cx, cx
    ICALL w16_hwnd2rec
    pop dx
    jnc .ctl
    xor bx, bx
.ctl:
    cmp byte [w16d_tabq], 0
    je .grp
    call w16d_tab
    jmp short .have
.grp:
    or bx, bx
    jz .zero
    call w16d_grp
.have:
    jc .zero
    ICALL w16_rec2hwnd
    xor dx, dx
    retf
.zero:
    jmp w16d_zero

; -----------------------------------------------------------------------------
; DefDlgProc (USER.308) - args: as DefWindowProc. A dialog window's own
; procedure (SPEC.md 124.13): the dialog procedure, then Windows' default
; when it answers 0.
; -----------------------------------------------------------------------------
w16u_defdlgproc_d:
    ICALL w16_args
    ICALL w16_smargs                ; AX = the HWND
    ICALL w16_hwnd2rec
    jc w16d_zero
    cmp byte [bx + W16W_KIND], W16Q_DIALOG
    jne .def
    mov ax, [bx + W16W_EXTRA + DWL_DLGPROC]
    mov dx, [bx + W16W_EXTRA + DWL_DLGPROC + 2]
    mov cx, ax
    or cx, dx
    jz .def
    xor cx, cx
    mov [bx + W16W_EXTRA + DWL_MSGRESULT], cx
    mov [bx + W16W_EXTRA + DWL_MSGRESULT + 2], cx
    push ax
    push dx
    mov si, w16d_ddp_c
    xor ax, ax
    ICALL w16_cbopen
    mov ax, [w16_sm_hwnd]           ; pascal: the first argument first
    ICALL w16_ppush
    mov ax, [w16_sm_msg]
    ICALL w16_ppush
    mov ax, [w16_sm_wp]
    ICALL w16_ppush
    mov ax, [w16_sm_lp + 2]
    ICALL w16_ppush
    mov ax, [w16_sm_lp]
    ICALL w16_ppush
    pop dx
    pop ax
    ICALL w16_cbcall
    retf
.def:
    jmp w16d_ddef

; w16d_ddp - the dialog procedure answered DX:AX (a BOOL): not 0 is the
; answer - the BOOL itself for the six messages whose answer it is, else
; DWL_MSGRESULT - and 0 is Windows' default
DCONT w16d_ddp
    push ax
    push dx
    ICALL w16_args
    ICALL w16_smargs
    ICALL w16_hwnd2rec
    pop dx
    pop ax
    jc .out
    or ax, ax                       ; a BOOL: AX alone (DX is whatever the
    jz w16d_ddef                    ;   procedure left in it)
    mov cx, [w16_sm_msg]
    cmp cx, WM_INITDIALOG
    je .bool
    cmp cx, WM_CTLCOLOR
    je .bool
    cmp cx, WM_COMPAREITEM
    je .bool
    cmp cx, WM_VKEYTOASCII
    je .bool
    cmp cx, WM_CHARTOITEM
    je .bool
    cmp cx, WM_QUERYDRAGICON
    je .bool
    mov ax, [bx + W16W_EXTRA + DWL_MSGRESULT]
    mov dx, [bx + W16W_EXTRA + DWL_MSGRESULT + 2]
    retf
.bool:
    xor dx, dx
.out:
    retf

; w16d_ddef - Windows' default for a dialog: BX = it, [w16_sm_*] the
; message, DefDlgProc's args the waiting call's
w16d_ddef:
    mov cx, [w16_sm_msg]
    cmp cx, WM_CLOSE
    jne .nclose
    ICALL w16_rec2hwnd              ; WM_CLOSE is Cancel
    mov [w16d_hdlg], ax
    mov ax, IDCANCEL
    call w16d_cmd
    jmp w16d_zero
.nclose:
    cmp cx, DM_GETDEFID
    jne .ngdef
    mov ax, [bx + WD_DEFID]
    xor dx, dx
    or ax, ax
    jz .ret
    mov dx, DC_HASDEFID
.ret:
    retf
.ngdef:
    cmp cx, DM_SETDEFID
    jne .nsdef
    mov ax, [w16_sm_wp]
    mov [bx + WD_DEFID], ax
    jmp w16d_one
.nsdef:
    cmp cx, WM_COMMAND
    jne .ncmd
    test byte [bx + WD_FL], WDF_MSGBOX
    jz .zero
    mov ax, [w16_sm_wp]             ; a message box ends on its button
    cmp ax, IDCANCEL
    jne .end
    push ax                         ; ...and Esc is Cancel, or OK when OK is
    ICALL w16_rec2hwnd              ;   all it has
    mov [w16d_hdlg], ax
    pop ax
    push bx
    call w16d_item
    pop bx
    jnc .end
    test byte [bx + WD_FL + 1], 1   ; WD_FL's high byte: the buttons' type
    jnz .zero
    mov ax, IDOK
.end:
    mov [bx + WD_RESULT], ax
    or byte [bx + WD_FL], WDF_ENDED
    ICALL w16_hidewnd
.zero:
    jmp w16d_zero
.ncmd:
    cmp cx, WM_INITDIALOG
    je .zero
    cmp cx, WM_GETDLGCODE
    je .zero
    ICALL w16u_defproc
    retf

; =============================================================================
; MessageBox (USER.1) - args: +0 wType, +2 lpCaption, +6 lpText, +10 hWnd.
; A dialog WIN16 builds, laid out as Windows 3.0 laid it out (SPEC.md 124.13)
; =============================================================================
W16D_MBW    equ 58              ; a button, VGA's: 58 wide, the face's rows
W16D_MBGAP  equ 12              ;   + 12 high, 12 apart
W16D_MBM    equ 16              ; the margin above the text, and right of it
W16D_MBIX   equ 18              ; the text's left, or an icon's; the text
W16D_MBTX   equ 68              ;   beside an icon starts here
W16D_MBGV   equ 22              ; the content to the buttons, and the buttons
W16D_MBBOT  equ 10              ;   to the bottom (SPEC.md 124.18.4: Windows
                                ;   3.0's VGA boxes, measured)

w16u_msgbox_d:
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16d_mbt], ax
    mov ax, [es:bx + 10]
    mov [w16d_par], ax
    push ds                         ; the text into [w16d_sbuf], measured
    push ds
    lds si, [es:bx + 6]
    pop es
    mov di, w16d_sbuf
    xor cx, cx
    mov ax, ds
    or ax, si
    jz .tend
.t:
    lodsb
    or al, al
    jz .tend
    stosb
    inc cx
    cmp cx, W16D_SBUF - 1
    jb .t
.tend:
    pop ds
    mov byte [di], 0
    mov [w16d_n], cx
    mov ax, [w16_scrw]              ; the lines, at most five eighths of the
    mov cx, 5                       ;   screen wide with the margins (Windows
    mul cx                          ;   3.0's VGA box wraps its fixture's line
    mov cl, 3                       ;   between 285 and 345 pixels)
    shr ax, cl
    sub ax, 2 * W16D_MBM
    mov dx, ax
    xor si, si
    xor ax, ax
    mov [w16d_mtw], ax
    mov [w16d_mtl], ax
.line:
    cmp si, [w16d_n]
    jae .measured
    mov cx, [w16d_n]
    sub cx, si
    push dx
    call w16d_wrap                  ; CX = its characters, AX = the next's
    push ax
    call w16d_tw
    cmp ax, [w16d_mtw]
    jbe .nw
    mov [w16d_mtw], ax
.nw:
    pop si
    pop dx
    inc word [w16d_mtl]
    jmp short .line
.measured:
    cmp word [w16d_mtl], 0
    jne .ml
    inc word [w16d_mtl]
.ml:
    mov word [w16d_icon], 0         ; the icon (SPEC.md 124.18.4): MB_ICONHAND
    mov word [w16d_tx], W16D_MBIX   ;   1 .. MB_ICONASTERISK 4 in bits 4-6,
    mov ax, [w16d_mbt]              ;   IDI_HAND .. IDI_ASTERISK
    mov cl, 4
    shr ax, cl
    and ax, 7
    jz .noicon
    cmp ax, 4
    ja .noicon
    call w16_icpart                ; WIN16's own art, read if need be
    jc .noicon
    add ax, W16_HSYSICO
    mov [w16d_icon], ax
    mov word [w16d_tx], W16D_MBTX
.noicon:
    mov bx, [w16d_mbt]              ; the buttons
    and bx, 0x0F
    cmp bx, 5
    jbe .bt
    xor bx, bx
.bt:
    mov [w16d_mbk], bx
    shl bx, 1
    shl bx, 1
    add bx, w16d_mbtab  ; XSEC-OK: read through cs:
    mov al, [cs:bx]
    xor ah, ah
    mov [w16d_mbn], ax              ; how many
    mov cx, W16D_MBW + W16D_MBGAP   ; their row's width
    mul cx
    sub ax, W16D_MBGAP
    mov [w16d_mbrw], ax
    mov cx, [w16d_mtw]              ; the client: the wider of the text and
    add cx, [w16d_tx]               ;   the buttons, with the text's left
    sub cx, W16D_MBM                ;   (past an icon) and the margins
    cmp cx, ax
    jae .w
    mov cx, ax
.w:
    add cx, 2 * W16D_MBM
    mov [w16_cwcx], cx
    mov [w16d_mcw], cx
    ICALL w16_rowsn
    mov [w16d_mbh], ax
    add word [w16d_mbh], 12
    mul word [w16d_mtl]
    mov [w16d_mth], ax
    mov word [w16d_ty], W16D_MBM    ; the text's top: centred on an icon
    cmp word [w16d_icon], 0         ;   taller than it, which sets the
    je .th                          ;   content's height then
    cmp ax, 32
    jae .th
    mov ax, 32
    sub ax, [w16d_mth]
    shr ax, 1
    add [w16d_ty], ax
    mov ax, 32
.th:
    add ax, W16D_MBM + W16D_MBGV    ; AX = the buttons' top
    mov [w16d_mby], ax
    add ax, W16D_MBBOT
    add ax, [w16d_mbh]
    mov [w16_cwcy], ax
    mov ax, [w16_scrw]              ; centred on the screen
    sub ax, [w16_cwcx]
    sar ax, 1
    mov [w16_cwx], ax
    mov ax, [w16_scrh]
    sub ax, [w16_cwcy]
    sar ax, 1
    mov [w16_cwy], ax
    mov byte [w16_cwkind], W16Q_DIALOG
    mov byte [w16_cwcls], 0xFF
    ICALL w16_ddpstub
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    mov word [w16_cwhbr], COLOR_WINDOW + 1
    mov word [w16_cwstyle], 0
    mov word [w16_cwstyle + 2], WS_POPUP_HI | WS_CAPTION_HI
    mov word [w16_cwid], 0
    mov ax, [w16d_par]
    mov [w16_cwpar], ax
    ICALL w16_args                  ; the caption, "Error" without one
    mov ax, [es:bx + 2]
    mov dx, [es:bx + 4]
    mov cx, ax
    or cx, dx
    jnz .cap
    mov ax, w16_s_error
    mov dx, ds
.cap:
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], dx
    ICALL w16_mkwnd                 ; BX = it
    jnc .made
    jmp w16d_mbfail
.made:
    mov [w16d_dlg], bx
    xor ax, ax
    mov [bx + W16W_EXTRA + DWL_DLGPROC], ax
    mov [bx + W16W_EXTRA + DWL_DLGPROC + 2], ax
    mov byte [bx + WD_FL], WDF_MODAL | WDF_MSGBOX
    cmp word [w16d_mbk], 0          ; MB_OK: Esc is OK (WD_FL's high byte)
    je .okonly
    mov byte [bx + WD_FL + 1], 1
.okonly:
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
    mov byte [w16_cwkind], W16Q_STATIC  ; the text
    mov ax, [w16d_tx]
    mov [w16_cwx], ax
    mov ax, [w16d_ty]
    mov [w16_cwy], ax
    mov ax, [w16d_mtw]
    mov [w16_cwcx], ax
    mov ax, [w16d_mth]
    mov [w16_cwcy], ax
    mov word [w16_cwid], -1
    mov word [w16_cwstyle], SS_LEFT | SS_NOPREFIX
    mov word [w16_cwstyle + 2], WS_VISIBLE_HI | WS_GROUP_HI
    mov word [w16_cwtext], w16d_sbuf
    mov [w16_cwtext + 2], ds
    call w16d_mkctl
    jc .undo
    cmp word [w16d_icon], 0         ; the icon, an SS_ICON static
    je .noic
    mov byte [w16_cwkind], W16Q_STATIC
    mov word [w16_cwx], W16D_MBIX
    mov word [w16_cwy], W16D_MBM
    mov word [w16_cwcx], 32
    mov word [w16_cwcy], 32
    mov word [w16_cwid], -1
    mov word [w16_cwstyle], SS_ICON
    mov word [w16_cwstyle + 2], WS_VISIBLE_HI
    xor ax, ax
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], ax
    call w16d_mkctl
    jc .undo
    mov ax, [w16d_icon]
    mov [bx + SC_ICON], ax
.noic:
    mov ax, [w16d_mbt]              ; the default: MB_DEFBUTTON1-3
    mov al, ah
    and ax, 0x0F
    cmp ax, [w16d_mbn]
    jb .defok
    xor ax, ax
.defok:
    mov [w16d_mbd], ax
    mov cx, [w16d_mcw]              ; the buttons' row, centred
    sub cx, [w16d_mbrw]
    sar cx, 1
    mov [w16_cwx], cx
    mov ax, [w16d_mby]
    mov [w16_cwy], ax
    xor cx, cx                      ; CX = the button
.b:
    cmp cx, [w16d_mbn]
    jae .made2
    push cx
    mov bx, [w16d_mbk]
    shl bx, 1
    shl bx, 1
    add bx, cx
    mov al, [cs:w16d_mbtab + bx + 1]
    xor ah, ah
    mov [w16_cwid], ax
    mov bx, ax
    shl bx, 1
    mov ax, [cs:w16d_mbstr + bx - 2]
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], ds
    mov byte [w16_cwkind], W16Q_BUTTON
    mov word [w16_cwcx], W16D_MBW
    mov ax, [w16d_mbh]
    mov [w16_cwcy], ax
    mov ax, BS_PUSHBUTTON
    cmp cx, [w16d_mbd]
    jne .nd
    mov ax, BS_DEFPUSHBUTTON
.nd:
    mov [w16_cwstyle], ax
    mov ax, WS_VISIBLE_HI | WS_TABSTOP_HI
    or cx, cx
    jnz .ng
    or ax, WS_GROUP_HI
.ng:
    mov [w16_cwstyle + 2], ax
    call w16d_mkctl
    pop cx
    jc .undo
    add word [w16_cwx], W16D_MBW + W16D_MBGAP
    inc cx
    jmp short .b
.made2:
    xor ax, ax
    mov [w16d_param], ax
    mov [w16d_param + 2], ax
    mov bx, [w16d_dlg]
    jmp w16d_initsend
.undo:
    mov bx, [w16d_dlg]
    ICALL w16_freetree
w16d_mbfail:
    xor ax, ax                      ; no room: 0, as Windows' answer when it
    xor dx, dx                      ;   could not make one
    retf

; each type's buttons: how many, then their IDs (MB_OK .. MB_RETRYCANCEL)
w16d_mbtab:
    db 1, 1, 0, 0                   ; OK
    db 2, 1, 2, 0                   ; OK Cancel
    db 3, 3, 4, 5                   ; Abort Retry Ignore
    db 3, 6, 7, 2                   ; Yes No Cancel
    db 2, 6, 7, 0                   ; Yes No
    db 2, 4, 2, 0                   ; Retry Cancel
; ...and each ID's caption, in DS (IDOK 1 .. IDNO 7)
w16d_mbstr:
    dw w16_s_ok, w16_s_cancel, w16_s_abort, w16_s_retry, w16_s_ignore
    dw w16_s_yes, w16_s_no

section .text
