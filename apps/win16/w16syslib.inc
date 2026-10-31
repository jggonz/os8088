; =============================================================================
; os8088 - apps/win16/w16syslib.inc
;
; THE SYSTEM LIBRARIES WIN16 PROVIDES (SPEC.md 124.24): COMMDLG and SHELL's
; calls, in PART 9, the system-library part (124.24.1) - the library part
; (124.19) was 57,714 bytes of its 64 KB when they came. A handler here is
; W16SYS, near, ending in ret; the gate fetches the part at the first call
; and far-calls w16s_gate with [w16_sfn]. The image is ICALL from here, the
; library part SLCALL, and the image reaches this part with SCALL.
;
; GetOpenFileName and GetSaveFileName are os8088's Standard File dialog
; (SPEC.md 38): the program waits in the call while the dialog is up, and
; is answered from the dialog's completion (w16_ofnback, in the image) or,
; when the dialog goes without one, by WIN16's tick seeing it gone - a
; Cancel, which the completion is never told of (124.24.2).
; =============================================================================

W16S_GATE   equ 10                  ; w16s_gate's offset in the part

OFN_HWND    equ 4                   ; OPENFILENAME (commdlg.h, Windows 3.1)
OFN_FILE    equ 24                  ; lpstrFile
OFN_NMAXF   equ 28                  ; nMaxFile
OFN_FTITLE  equ 32                  ; lpstrFileTitle
OFN_NMAXFT  equ 36                  ; nMaxFileTitle
OFN_FOFF    equ 52                  ; nFileOffset
OFN_FEXT    equ 54                  ; nFileExtension
OFN_DEFEXT  equ 56                  ; lpstrDefExt

CDERR_DIALOGFAILURE  equ 0xFFFF
FNERR_BUFFERTOOSMALL equ 0x3003
PDERR_NODEFAULTPRN   equ 0x1008

section .w16sys follows=.w16ld vstart=0 align=1
w16s_base:
    jmp short w16s_gate             ; +0  tools/os88ovl.py: a short jump ...
    db 'W16SYSLB'                   ; +2  ...before the cut mark
; w16s_gate - the part's one way in (SPEC.md 124.24.1): the gate's dispatch
; and SCALL far-call it with [w16_sfn] naming a near routine here. CS = this
; part, DS = WIN16's image; every register and the flags pass both ways
w16s_gate:
    call [w16_sfn]
    retf
%if w16s_gate - w16s_base != W16S_GATE
  %error "w16s_gate is not at W16S_GATE"
%endif

; =============================================================================
; COMMDLG: the file dialogs (SPEC.md 124.24.2)
; =============================================================================

; GetOpenFileName (COMMDLG.1), GetSaveFileName (COMMDLG.2) - args: +0
; lpofn. os8088's chooser in the Open or Save form over WIN16's window, the
; name part of lpstrFile its default; the program waits until it answers
W16SYS w16c_getopen
    mov al, FDLG_OPEN
    jmp short w16c_ofn
W16SYS w16c_getsave
    mov al, FDLG_SAVE
w16c_ofn:
    push si
    push di
    push ax
    mov word [w16_cderr], 0
    ICALL w16_args
    les bx, [es:bx]                 ; the OPENFILENAME...
    les bx, [es:bx + OFN_FILE]      ; ...its lpstrFile
    xor si, si                      ; SI = the default name, 0 = none
    mov ax, es
    or ax, bx
    jz .open
    mov di, bx                      ; ES:DI = past the last '\' or ':'
.scan:
    mov al, [es:bx]
    or al, al
    jz .name
    inc bx
    cmp al, '\'
    je .sep
    cmp al, ':'
    jne .scan
.sep:
    mov di, bx
    jmp short .scan
.name:
    xor bx, bx                      ; BX = the characters copied
.cp:
    mov al, [es:di + bx]
    or al, al
    jz .cpend
    cmp al, '*'                     ; a pattern names no file
    je .open
    cmp al, '?'
    je .open
    cmp bx, 12
    jae .open                       ; too long for 8.3: none
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 0x20
.up:
    mov [w16_namebuf + bx], al
    inc bx
    jmp short .cp
.cpend:
    mov byte [w16_namebuf + bx], 0
    or bx, bx
    jz .open
    mov si, w16_namebuf
.open:
    pop ax                          ; AL = the form
    ICALL w16_lock                  ; (FILE_DLG wants the lock held)
    mov bx, [w16_win]
    mov di, w16_ofnback             ; the completion: the image's
    call OSAPI_FILE_DLG
    pushf
    ICALL w16_unlock
    popf
    jc .fail                        ; one is up already
    call OSAPI_WM_TOP               ; BX = the chooser, on screen now
    mov [w16_ofnwin], bx
    mov ax, [w16_idx]               ; the program waits in the call
    mov [w16_widx], ax
    mov byte [w16_pend], 1
    ICALL w16_tarm                  ; ...and the tick looks for a Cancel
    jmp short .out
.fail:
    mov word [w16_cderr], CDERR_DIALOGFAILURE
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16c_ofndone - the chooser answered with a file (w16_ofnback, the lock
; held): AL = the form, DI = the name at KERNEL_SEG:DI, and the instance's
; volume and folder are the chooser's. The waiting call's OPENFILENAME is
; filled. out: CF=0 AX = the answer; CF=1 nothing waits on it
w16c_ofndone:
    cmp word [w16_ofnwin], 0
    jne .go
    stc
    ret
.go:
    mov word [w16_ofnwin], 0
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax                      ; BP's low byte = the form
    push ds                         ; the name, out of the kernel's buffer
    pop es
    push ds
    mov si, di
    mov ax, KERNEL_SEG
    mov ds, ax
    mov di, w16_namebuf
    mov cx, 12
    cld
.n:
    lodsb
    stosb
    or al, al
    loopnz .n
    mov byte [es:di], 0
    pop ds
    ; --- the path: X: and the folder, then the name (SPEC.md 124.24.2)
    call OSAPI_FILE_HERE            ; BL = the volume
    mov di, w16f_path
    mov al, bl
    add al, 'A'
    mov ah, ':'
    mov [di], ax
    add di, 2
    push ds
    pop es
    mov cx, 120
    call OSAPI_FILE_PATH            ; ES:DI = \DIR\DIR, CX its length
    jnc .pathok
    xor cx, cx
.pathok:
    add di, cx
    cmp cx, 0
    je .sl
    cmp byte [di - 1], '\'
    je .nosl
.sl:
    mov byte [di], '\'
    inc di
.nosl:
    mov dx, di
    sub dx, w16f_path               ; DX = nFileOffset
    mov si, w16_namebuf
    xor bx, bx                      ; BX = the extension's offset, 0 none
.nm:
    lodsb
    stosb
    or al, al
    jz .nmend
    cmp al, '.'
    jne .nm
    mov bx, di
    jmp short .nm
.nmend:
    dec di                          ; DI = the NUL
    or bx, bx
    jnz .ext
    mov ax, bp
    cmp al, FDLG_SAVE
    jne .noext
    ICALL w16_args                  ; a Save with no extension: lpstrDefExt
    les si, [es:bx]
    les si, [es:si + OFN_DEFEXT]
    mov ax, es
    or ax, si
    jz .noext
    cmp byte [es:si], 0
    je .noext
    mov byte [di], '.'              ; DS:DI is ours: '.' and up to 3
    inc di
    mov bx, di
    mov cx, 3
.de:
    mov al, [es:si]
    or al, al
    jz .deend
    cmp al, 'a'
    jb .deu
    cmp al, 'z'
    ja .deu
    sub al, 0x20
.deu:
    mov [di], al
    inc si
    inc di
    loop .de
.deend:
    mov byte [di], 0
    jmp short .ext
.noext:
    mov bx, di                      ; no extension: nFileExtension = the NUL
.ext:
    sub bx, w16f_path               ; BX = nFileExtension
    sub di, w16f_path
    inc di                          ; DI = the path's size, its NUL counted
    ; --- into the program's OPENFILENAME
    push bx
    ICALL w16_args
    les si, [es:bx]                 ; ES:SI = the OPENFILENAME
    pop bx
    mov [es:si + OFN_FOFF], dx
    mov [es:si + OFN_FEXT], bx
    cmp di, [es:si + OFN_NMAXF]
    jbe .fits
    mov word [w16_cderr], FNERR_BUFFERTOOSMALL
    xor ax, ax
    jmp short .done
.fits:
    push si
    push es
    les bx, [es:si + OFN_FILE]
    mov cx, di
    mov si, w16f_path
.pc:
    lodsb
    mov [es:bx], al
    inc bx
    loop .pc
    pop es
    pop si
    mov cx, [es:si + OFN_NMAXFT]    ; lpstrFileTitle: the name alone
    les bx, [es:si + OFN_FTITLE]
    mov ax, es
    or ax, bx
    jz .ok
    jcxz .ok
    mov si, w16f_path
    add si, dx
.tc:
    lodsb
    dec cx
    jnz .tput
    xor al, al                      ; the last byte the buffer has: a NUL
.tput:
    mov [es:bx], al
    inc bx
    or al, al
    jnz .tc
.ok:
    mov word [w16_cderr], 0
    mov ax, 1
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; GetFileTitle (COMMDLG.27) - args: +0 cbBuf, +2 lpszTitle, +6 lpszFile.
; The name part of the path into the buffer: 0, or the size it needs (its
; NUL counted) when the buffer is short
W16SYS w16c_filetitle
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    push word [es:bx + 4]           ; lpszTitle
    push word [es:bx + 2]
    les si, [es:bx + 6]             ; ES:DI = past the last '\' or ':'
    mov di, si
.scan:
    mov al, [es:si]
    inc si
    or al, al
    jz .end
    cmp al, '\'
    je .sep
    cmp al, ':'
    jne .scan
.sep:
    mov di, si
    jmp short .scan
.end:
    mov ax, si
    sub ax, di                      ; AX = the size the name needs
    pop si                          ; the buffer's offset...
    pop dx                          ;   ...and segment
    cmp ax, cx
    ja .out                         ; short: the size it needs
    push ds
    push es
    pop ds
    mov es, dx
    xchg si, di                     ; DS:SI = the name, ES:DI = the buffer
    mov cx, ax
    cld
    rep movsb
    pop ds
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; PrintDlg (COMMDLG.20) - args: +0 lppd. FALSE, PDERR_NODEFAULTPRN: a
; Windows with no printer installed (SPEC.md 124.24.3)
W16SYS w16c_printdlg
    mov word [w16_cderr], PDERR_NODEFAULTPRN
    xor ax, ax
    xor dx, dx
    ret

; FindText, ReplaceText (COMMDLG.11, 12) - args: +0 lpfr. No dialog: 0
W16SYS w16c_findtext
    mov word [w16_cderr], CDERR_DIALOGFAILURE
    xor ax, ax
    xor dx, dx
    ret

; CommDlgExtendedError (COMMDLG.26)
W16SYS w16c_exterr
    mov ax, [w16_cderr]
    xor dx, dx
    ret

; =============================================================================
; COMMDLG: ChooseFont and ChooseColor (SPEC.md 124.24.4)
; =============================================================================

CF_INITTOLOGFONTSTRUCT equ 0x40     ; CHOOSEFONT's Flags
CC_RGBINIT  equ 1                   ; CHOOSECOLOR's
CHF_LOGFONT equ 8                   ; CHOOSEFONT: lpLogFont
CHF_POINTS  equ 12                  ;   iPointSize
CHF_FLAGS   equ 14                  ;   Flags
CHF_TYPE    equ 40                  ;   nFontType
CHC_RGB     equ 8                   ; CHOOSECOLOR: rgbResult
CHC_FLAGS   equ 16                  ;   Flags
W16C_LIST   equ 0x460               ; the list's ID: Windows' lst1
W16C_M      equ 8                   ; the margin, and between the parts
W16C_BW     equ 58                  ; a button, MessageBox's width

; ChooseFont (COMMDLG.15), ChooseColor (COMMDLG.5) - args: +0 the structure.
; A WIN16 dialog of the faces it has, or of the sixteen colours: the program
; waits in the call, and OK fills the structure before the dialog ends
W16SYS w16c_choosefont
    xor al, al
    jmp short w16c_choose
W16SYS w16c_choosecol
    mov al, 1
w16c_choose:
    push si
    push di
    mov [w16_chk], al
    mov word [w16_cderr], 0
    ICALL w16_args
    les bx, [es:bx]
    mov [w16_chst], bx
    mov [w16_chst + 2], es
    mov ax, [es:bx + 4]             ; hwndOwner, both structures'
    mov [w16d_par], ax
    xor dx, dx                      ; DX = the item selected to begin with
    cmp byte [w16_chk], 0
    jne .rgb
    test byte [es:bx + CHF_FLAGS], CF_INITTOLOGFONTSTRUCT
    jz .sel
    les si, [es:bx + CHF_LOGFONT]
    mov ax, es
    or ax, si
    jz .sel
    mov al, [es:si + 17]            ; lfPitchAndFamily
    add si, 18                      ; lfFaceName
    SLCALL w16g_fontmap             ; the face CreateFont would give it
    cmp ax, W16_HFDLG
    je .sel
    cmp word [w16_dface], 0         ; System: the second, after the dialog
    je .sel                         ;   face when there is one
    inc dx
    jmp short .sel
.rgb:
    test byte [es:bx + CHC_FLAGS], CC_RGBINIT
    jz .sel
    mov ax, [es:bx + CHC_RGB]
    mov dl, [es:bx + CHC_RGB + 2]
    ICALL w16_rgb2col
    xor ah, ah
    mov dx, ax
.sel:
    push dx
    ICALL w16_rowsn                 ; AX = the System font's rows
    mov [w16d_mbh], ax              ; (MessageBox's words, as scratch)
    mov si, w16c_fitems     ; XSEC-OK: the items' table, read through cs:
    cmp byte [w16_chk], 0
    je .fl
    mov si, w16c_citems     ; XSEC-OK: read through cs:
.fl:
    mov [w16d_mtl], si
    mov cl, [cs:si]                 ; CL = how many, CH = the list's width
    mov ch, [cs:si + 1]             ;   in cells
    cmp byte [w16_chk], 0
    jne .n
    cmp word [w16_dface], 0         ; no dialog face: System alone
    jne .n
    dec cl
    add word [w16d_mtl], w16c_fsys - w16c_fitems - 2 ; XSEC-OK: a length
.n:
    xor ah, ah
    mov al, cl
    mov [w16d_mbn], ax              ; [w16d_mbn] = the items
    mul word [w16d_mbh]
    add ax, 2                       ; the list: an item a row, and its border
    mov [w16d_mth], ax
    mov al, ch
    xor ah, ah
    mov cl, 3
    shl ax, cl
    mov [w16d_mtw], ax              ; [w16d_mtw] = the list's width
    add ax, 3 * W16C_M + W16C_BW
    mov [w16_cwcx], ax
    mov ax, [w16d_mbh]              ; the client's height: the list's, below
    add ax, W16C_M + 4              ;   its label, or the two buttons'
    mov [w16d_mby], ax              ; [w16d_mby] = the list's top
    add ax, [w16d_mth]
    mov cx, [w16d_mbh]
    add cx, 12
    shl cx, 1
    add cx, [w16d_mby]
    add cx, 6
    cmp ax, cx
    jae .h
    mov ax, cx
.h:
    add ax, W16C_M
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
    mov ax, w16c_sfont      ; XSEC-OK: a string the window copies, by CS
    cmp byte [w16_chk], 0
    je .cap
    mov ax, w16c_scolor     ; XSEC-OK: by CS
.cap:
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], cs
    ICALL w16_mkwnd                 ; BX = it
    jc .fail
    mov [w16d_dlg], bx
    xor ax, ax
    mov [bx + W16W_EXTRA + DWL_DLGPROC], ax
    mov [bx + W16W_EXTRA + DWL_DLGPROC + 2], ax
    mov byte [bx + WD_FL], WDF_MODAL | WDF_CHOOSE
    ICALL w16_rec2hwnd
    mov [w16d_hdlg], ax
    mov byte [w16d_dfon], 0
    mov byte [w16_cwkind], W16Q_STATIC  ; the label
    mov word [w16_cwx], W16C_M
    mov word [w16_cwy], W16C_M
    mov ax, [w16d_mtw]
    mov [w16_cwcx], ax
    mov ax, [w16d_mbh]
    mov [w16_cwcy], ax
    mov word [w16_cwid], -1
    mov word [w16_cwstyle], SS_LEFT
    mov word [w16_cwstyle + 2], WS_VISIBLE_HI | WS_GROUP_HI
    mov ax, w16c_lfont      ; XSEC-OK: by CS
    cmp byte [w16_chk], 0
    je .lab
    mov ax, w16c_lcolor     ; XSEC-OK: by CS
.lab:
    mov [w16_cwtext], ax
    SLCALL w16d_mkctl
    jc .undo
    mov byte [w16_cwkind], W16Q_LIST    ; the list
    mov ax, [w16d_mby]
    mov [w16_cwy], ax
    mov ax, [w16d_mth]
    mov [w16_cwcy], ax
    mov word [w16_cwid], W16C_LIST
    mov word [w16_cwstyle], LBS_NOTIFY
    mov word [w16_cwstyle + 2], WS_VISIBLE_HI | WS_BORDER_HI | WS_TABSTOP_HI | WS_GROUP_HI
    xor ax, ax
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], ax
    SLCALL w16d_mkctl
    jc .undo
    mov si, [w16d_mtl]              ; its items
    add si, 2
    mov cx, [w16d_mbn]
.item:
    mov [w16_sm_lp], si
    mov [w16_sm_lp + 2], cs
    mov word [w16_sm_msg], LB_ADDSTRING
    mov word [w16_sm_wp], 0
    push cx
    ICALL w16_intproc
    pop cx
.skip:
    cs lodsb                        ; (the part's own: DS is the image)
    or al, al
    jnz .skip
    loop .item
    pop dx                          ; the selection
    push dx
    mov [w16_sm_wp], dx
    mov word [w16_sm_msg], LB_SETCURSEL
    ICALL w16_intproc
    mov ax, [w16d_mtw]              ; the buttons, right of the list
    add ax, 2 * W16C_M
    mov [w16_cwx], ax
    mov ax, [w16d_mby]
    mov [w16_cwy], ax
    mov word [w16_cwcx], W16C_BW
    mov ax, [w16d_mbh]
    add ax, 12
    mov [w16_cwcy], ax
    mov word [w16_cwid], IDOK
    mov word [w16_cwtext], w16_s_ok
    mov ax, BS_DEFPUSHBUTTON
    call .button
    jc .undo
    mov ax, [w16_cwcy]
    add ax, 6
    add [w16_cwy], ax
    mov word [w16_cwid], IDCANCEL
    mov word [w16_cwtext], w16_s_cancel
    mov ax, BS_PUSHBUTTON
    call .button
    jc .undo
    pop dx
    xor ax, ax
    mov [w16d_param], ax
    mov [w16d_param + 2], ax
    mov bx, [w16d_dlg]
    SLCALL w16d_modrun              ; MessageBox's modal run: the answer,
    pop di                          ;   or the call left waiting on it
    pop si
    ret
.button:
    mov [w16_cwstyle], ax
    mov word [w16_cwstyle + 2], WS_VISIBLE_HI | WS_TABSTOP_HI
    mov [w16_cwtext + 2], ds
    mov byte [w16_cwkind], W16Q_BUTTON
    SLCALL w16d_mkctl
    ret
.undo:
    mov bx, [w16d_dlg]
    ICALL w16_freetree
.fail:
    pop dx
    mov word [w16_cderr], CDERR_DIALOGFAILURE
    xor ax, ax
    xor dx, dx
    pop di
    pop si
    ret

; w16c_chdone - the dialog's OK (w16d_ddef, WDF_CHOOSE): AX = the list's
; selection, written into the waiting call's structure. AX = TRUE, the
; dialog's answer. Keeps the rest.
SCONT w16c_chdone
    push bx
    push cx
    push dx
    push di
    push es
    cmp ax, -1
    jne .some
    xor ax, ax
.some:
    les bx, [w16_chst]
    cmp byte [w16_chk], 0
    jne .colour
    push word [w16_face]            ; the face: the dialog face first, when
    mov dx, [w16_sface]             ;   there is one, then System
    mov cx, 100
    cmp word [w16_dface], 0
    je .face
    or ax, ax
    jnz .face
    mov dx, [w16_dface]
    mov cx, 80
.face:
    mov [w16_face], dx
    mov [es:bx + CHF_POINTS], cx
    mov word [es:bx + CHF_TYPE], 0x2100 ; SCREEN_FONTTYPE | BOLD_FONTTYPE
    les di, [es:bx + CHF_LOGFONT]
    mov ax, es
    or ax, di
    jz .nolf
    SLCALL w16g_lfill
.nolf:
    pop word [w16_face]
    jmp short .ok
.colour:
    mov di, ax                      ; its COLORREF, w16_pal16's
    shl di, 1
    add di, ax
    mov ax, [w16_pal16 + di]
    mov dl, [w16_pal16 + di + 2]
    xor dh, dh
    mov [es:bx + CHC_RGB], ax
    mov [es:bx + CHC_RGB + 2], dx
.ok:
    mov ax, 1
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; each list: how many items, its width in cells, then the items
w16c_fitems:
    db 2, 23
    db 'MS Sans Serif, 8 point', 0
w16c_fsys:
    db 'System, 10 point', 0
w16c_citems:
    db 16, 14
    db 'Black', 0, 'Dark Blue', 0, 'Dark Green', 0, 'Dark Cyan', 0
    db 'Dark Red', 0, 'Dark Magenta', 0, 'Brown', 0, 'Light Gray', 0
    db 'Gray', 0, 'Blue', 0, 'Green', 0, 'Cyan', 0
    db 'Red', 0, 'Magenta', 0, 'Yellow', 0, 'White', 0
w16c_sfont:   db 'Font', 0
w16c_scolor:  db 'Color', 0
w16c_lfont:   db '&Font:', 0
w16c_lcolor:  db '&Color:', 0

section .text

; w16_ofnback - OSAPI_FILE_DLG's completion (SPEC.md 38.6, 124.24.2): the
; gfx lock held, AL = the form, DI = the name in the kernel's segment. The
; OPENFILENAME filled in the part, then the waiting call answered from here,
; in the image - the program may end before w16_resume comes back, and its
; end drops the part
w16_ofnback:
    SCALL w16c_ofndone              ; CF=1: nothing waits on it
    jc w16_ofnret
; w16_ofnresume - AX = the waiting GetOpen/SaveFileName's answer, the lock held
w16_ofnresume:
    push word [w16_locked]
    mov byte [w16_locked], 1
    push ax
    call w16_reidx
    pop ax
    xor dx, dx
    call w16_resume
    call w16_after
    pop word [w16_locked]
w16_ofnret:
    ret

; w16_ofnpoll - from WIN16's tick: a chooser a call waits on that is no
; longer frontmost went without an answer - a Cancel (SPEC.md 124.24.2)
w16_ofnpoll:
    cmp word [w16_ofnwin], 0
    je w16_ofnret
    push bx
    call OSAPI_WM_TOP
    cmp bx, [w16_ofnwin]
    pop bx
    je w16_ofnret
    mov word [w16_ofnwin], 0
    mov word [w16_cderr], 0
    xor ax, ax
    jmp short w16_ofnresume
