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
    push ax                         ; the dialog opens where the MACHINE
    push si                         ;   stands, and a handle's read or write
    mov ah, 0x47                    ;   leaves it in that file's folder
    xor dl, dl                      ;   (SPEC.md 96.52) - Paintbrush's swap
    mov si, w16_gbuf                ;   file's. AH=47h stands it in the
    push ds                         ;   program's current folder first, which
    pop es                          ;   is where Windows' dialog opens
    ICALL w16_dcall                 ;   (SPEC.md 124.24.2)
    pop si
    pop ax
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


; =============================================================================
; SHELL: the registration database (SPEC.md 124.24.6) - REG.DAT in the
; Windows directory, a profile file with one section, [HKEY_CLASSES_ROOT],
; and a line a key: its full name, '=', its value. The library part's profile
; code holds it, merges it and writes it; this part holds the open keys
; =============================================================================

ERROR_BADKEY      equ 2
ERROR_CANTWRITE   equ 5
ERROR_OUTOFMEMORY equ 6
ERROR_INVALID_PARAMETER equ 7
REG_SZ      equ 1
W16R_N      equ 8                   ; keys open at once, besides the root
W16R_SZ     equ 128                 ; a key's row: a used byte, its full name
W16R_MAX    equ W16R_SZ - 2         ;   (126 at most) and its NUL
W16R_H0     equ 0x10                ; the first key's handle

; RegOpenKey (SHELL.1), RegCreateKey (SHELL.2) - args: +0 phkResult,
; +4 lpSubKey, +8 hKey
W16SYS w16r_create
    mov cl, 1
    jmp short w16r_opn
W16SYS w16r_open
    xor cl, cl
w16r_opn:
    push si
    push di
    push cx
    ICALL w16_args
    mov ax, [es:bx + 8]
    les si, [es:bx + 4]
    call w16r_path
    pop cx
    jc w16r_bad
    mov ax, 1                       ; the root itself: its own handle
    cmp byte [w16pf_key], 0
    je .give
    or cl, cl
    jz .find
    call w16r_make
    jc w16r_cant
    jmp short .slot
.find:
    call w16r_find
    jc w16r_bad
.slot:
    xor bx, bx                      ; a free row for it
.s:
    cmp byte [cs:w16r_keys + bx], 0
    je .take
    add bx, W16R_SZ
    cmp bx, W16R_N * W16R_SZ
    jb .s
    mov ax, ERROR_OUTOFMEMORY
    jmp short w16r_out
.take:
    mov byte [cs:w16r_keys + bx], 1
    mov si, w16pf_key
    lea di, [bx + 1]
.cp:
    mov al, [si]
    mov [cs:w16r_keys + di], al
    inc si
    inc di
    or al, al
    jnz .cp
    mov ax, bx
    mov cl, 7                       ; (W16R_SZ)
    shr ax, cl
    add ax, W16R_H0
.give:
    push ax
    ICALL w16_args
    les bx, [es:bx]
    pop ax
    mov [es:bx], ax
    mov word [es:bx + 2], 0
    ; fall into w16r_ok

; the calls' ends: AX = ERROR_SUCCESS, ERROR_CANTWRITE, ERROR_BADKEY or, at
; w16r_out, AX; DX = 0, and SI and DI as the call's start pushed them
w16r_ok:
    xor ax, ax
    jmp short w16r_out
w16r_cant:
    mov ax, ERROR_CANTWRITE
    jmp short w16r_out
w16r_bad:
    mov ax, ERROR_BADKEY
w16r_out:
    xor dx, dx
    pop di
    pop si
    ret

; RegCloseKey (SHELL.3) - args: +0 hKey
W16SYS w16r_close
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]
    cmp ax, 1
    jbe w16r_ok
    call w16r_row
    jc w16r_bad
    mov byte [cs:w16r_keys + bx], 0
    jmp short w16r_ok

; RegDeleteKey (SHELL.4) - args: +0 lpSubKey, +4 hKey. The key's line and
; every line below it
W16SYS w16r_delete
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 4]
    les si, [es:bx]
    call w16r_path
    jc w16r_bad
    cmp byte [w16pf_key], 0         ; never the root
    je w16r_bad
    mov si, w16pf_key               ; the name, kept in [w16pf_val]
    mov di, w16pf_val
    call w16r_scpy
    mov byte [cs:w16r_hit], 0
.again:
    call w16r_db
    jc .end
    mov si, [w16pf_body]
.k:
    SLCALL w16pf_keyline
    jc .end
    call w16r_under                 ; AX = 0 the key, 1 below it, 2 neither
    cmp ax, 1
    ja .k
    mov si, [w16pf_ks]              ; that line's key, as spelled, deleted
    mov di, w16pf_key
.c:
    cmp si, [w16pf_ke]
    jae .cz
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    jmp short .c
.cz:
    mov byte [di], 0
    mov byte [w16pf_noval], 1
    call w16r_write
    jc w16r_cant
    mov byte [cs:w16r_hit], 1
    jmp short .again
.end:
    cmp byte [cs:w16r_hit], 0
    je w16r_bad
    jmp w16r_ok

; RegSetValue (SHELL.5) - args: +0 cbData, +4 lpData, +8 dwType,
; +12 lpSubKey, +16 hKey. The key made, as RegCreateKey makes it, and its
; value set
W16SYS w16r_setval
    push si
    push di
    ICALL w16_args
    mov ax, ERROR_INVALID_PARAMETER
    cmp word [es:bx + 8], REG_SZ
    jne w16r_out
    cmp word [es:bx + 10], 0
    jne w16r_out
    mov ax, [es:bx + 16]
    les si, [es:bx + 12]
    call w16r_path
    jc w16r_bad
    cmp byte [w16pf_key], 0         ; the root has no value of its own
    je w16r_bad
    call w16r_make
    jc w16r_cant
    ICALL w16_args
    les ax, [es:bx + 4]
    mov dx, es
    mov di, w16pf_val
    SLCALL w16pf_cpy                ; (NULL: "")
    mov byte [w16pf_noval], 0
    call w16r_write
    jc w16r_cant
    jmp w16r_ok

; RegQueryValue (SHELL.6) - args: +0 lpcb, +4 lpValue, +8 lpSubKey,
; +12 hKey. The value, cut to *lpcb bytes with its NUL; *lpcb the bytes
; written with it, or with no buffer the bytes it needs
W16SYS w16r_queryval
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 12]
    les si, [es:bx + 8]
    call w16r_path
    jc w16r_bad
    xor si, si                      ; the value: the root's is empty
    xor di, di
    mov ax, ds
    cmp byte [w16pf_key], 0
    je .have
    call w16r_find
    jc w16r_bad
    mov ax, es
    mov si, [w16pf_vs]
    mov di, [w16pf_ve]
.have:
    mov [cs:w16r_vseg], ax
    mov [cs:w16r_vs], si
    mov [cs:w16r_ve], di
    ICALL w16_args
    les di, [es:bx]                 ; *lpcb: the room, its NUL among it
    mov cx, [es:di]
    ICALL w16_args
    les di, [es:bx + 4]             ; ES:DI = the buffer
    mov si, [cs:w16r_vs]
    mov dx, [cs:w16r_ve]
    push ds
    mov ds, [cs:w16r_vseg]
    cld
    xor bx, bx                      ; BX = the bytes written, its NUL aside
    mov ax, es
    or ax, di
    jz .count
    jcxz .size
    dec cx
.v:
    cmp si, dx
    jae .nul
    jcxz .nul
    lodsb
    stosb
    inc bx
    dec cx
    jmp short .v
.nul:
    mov byte [es:di], 0
    jmp short .size
.count:
    mov bx, dx
    sub bx, si
.size:
    pop ds
    inc bx                          ; with its NUL
    push bx
    ICALL w16_args
    les bx, [es:bx]
    pop ax
    mov [es:bx], ax
    mov word [es:bx + 2], 0
    jmp w16r_ok

; RegEnumKey (SHELL.7) - args: +0 cchName, +4 lpName, +8 dwIndex, +12 hKey.
; The dwIndex'th key directly below hKey, in the file's order
W16SYS w16r_enum
    push si
    push di
    ICALL w16_args
    cmp word [es:bx + 10], 0
    jne .bad
    mov ax, [es:bx + 8]
    mov [cs:w16r_idx], ax
    mov ax, [es:bx + 12]
    xor si, si                      ; (no subkey: the key itself)
    mov es, si
    call w16r_path
    jc .bad
    mov si, w16pf_key
    mov di, w16pf_val
    call w16r_scpy
    call w16r_db
    jc .bad
    mov si, [w16pf_body]
.k:
    SLCALL w16pf_keyline
    jc .bad
    call w16r_under                 ; 1: below it, the rest at [w16r_tail]
    cmp ax, 1
    jne .k
    mov bx, [cs:w16r_tail]          ; directly below: no '\' in the rest
.d:
    cmp bx, [w16pf_ke]
    jae .direct
    cmp byte [es:bx], '\'
    je .k
    inc bx
    jmp short .d
.direct:
    cmp word [cs:w16r_idx], 0
    je .this
    dec word [cs:w16r_idx]
    jmp short .k
.this:
    mov [cs:w16r_vseg], es
    ICALL w16_args
    mov cx, [es:bx]                 ; the room, its NUL among it
    les di, [es:bx + 4]
    mov si, [cs:w16r_tail]
    mov dx, [w16pf_ke]
    push ds
    mov ds, [cs:w16r_vseg]
    cld
    jcxz .z
    dec cx
.c:
    cmp si, dx
    jae .n
    jcxz .n
    lodsb
    stosb
    dec cx
    jmp short .c
.n:
    mov byte [es:di], 0
.z:
    pop ds
    jmp w16r_ok
.bad:
    jmp w16r_bad

; w16r_path - AX = an HKEY, ES:SI = a subkey (NULL or "" for the key
; itself): its full name below the root into [w16pf_key]. CF=1 not an open
; key, or a name longer than W16R_MAX. Keeps every register
w16r_path:
    push ax
    push bx
    push si
    push di
    mov di, w16pf_key
    cmp ax, 1                       ; 0 and HKEY_CLASSES_ROOT: the root
    jbe .sub
    call w16r_row
    jc .out
    inc bx
.base:
    mov al, [cs:w16r_keys + bx]
    or al, al
    jz .sub
    mov [di], al
    inc di
    inc bx
    jmp short .base
.sub:
    mov ax, es
    or ax, si
    jz .done
    cmp byte [es:si], '\'
    jne .s1
    inc si
.s1:
    cmp byte [es:si], 0
    je .done
    cmp di, w16pf_key
    je .cp
    mov al, '\'
.put:
    cmp di, w16pf_key + W16R_MAX
    jae .long
    mov [di], al
    inc di
.cp:
    mov al, [es:si]
    inc si
    or al, al
    jnz .put
.done:
    mov byte [di], 0
    clc
    jmp short .out
.long:
    stc
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; w16r_row - AX = a key's handle: BX = its row. CF=1 not an open one.
; Keeps the rest
w16r_row:
    push ax
    push cx
    sub ax, W16R_H0
    jb .no
    cmp ax, W16R_N
    jae .no
    mov cl, 7                       ; (W16R_SZ)
    shl ax, cl
    mov bx, ax
    cmp byte [cs:w16r_keys + bx], 0
    je .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop ax
    ret

; w16r_names - REG.DAT and its section, for the profile code
w16r_names:
    push ax
    push cx
    push si
    push di
    mov di, w16pf_nm                ; the name, zero-filled
    mov cx, 13
    xor al, al
.z:
    mov [di], al
    inc di
    loop .z
    mov si, w16r_s_file     ; XSEC-OK: read through cs:
    mov di, w16pf_nm
    call w16r_ccpy
    mov si, w16r_s_sect     ; XSEC-OK: read through cs:
    mov di, w16pf_app
    call w16r_ccpy
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16r_ccpy - CS:SI to DS:DI, its NUL too. Clobbers AL, SI, DI
w16r_ccpy:
    mov al, [cs:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz w16r_ccpy
    ret

; w16r_scpy - DS:SI to DS:DI, its NUL too. Clobbers AL, SI, DI
w16r_scpy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz w16r_scpy
    ret

; w16r_db - REG.DAT held, its section found: ES = the held file. CF=1 there
; is no memory for it, or no key yet
w16r_db:
    call w16r_names
    SLCALL w16pf_open
    jc .out
    SLCALL w16pf_sect
.out:
    ret

; w16r_find - [w16pf_key]'s line: CF=0 found, the profile code's answers
; for it and ES the held file; CF=1 none
w16r_find:
    call w16r_db
    jc .out
    SLCALL w16pf_findkey
.out:
    ret

; w16r_make - the key in [w16pf_key], and each above it, given a line with an
; empty value where it has none. CF=1 one could not be written
w16r_make:
    push si
    mov si, w16pf_key
.c:
    mov al, [si]
    or al, al
    jz .last
    cmp al, '\'
    jne .n
    mov byte [si], 0                ; the name so far
    call .one
    mov byte [si], '\'
    jc .out
.n:
    inc si
    jmp short .c
.last:
    call .one
.out:
    pop si
    ret
.one:
    push si
    call w16r_find
    jnc .has
    mov byte [w16pf_val], 0
    mov byte [w16pf_noval], 0
    call w16r_write
    pop si
    ret
.has:
    pop si
    clc
    ret

; w16r_write - [w16pf_key] written with [w16pf_val], or deleted when
; [w16pf_noval]: the profile code's write, then its merge log. CF=1 not
; written
w16r_write:
    call w16r_names
    mov byte [w16pf_nokey], 0
    SLCALL w16pf_wcore
    or ax, ax
    jz .no
    SLCALL w16pf_log
    clc
    ret
.no:
    stc
    ret

; w16r_under - ES:[w16pf_ks]..[w16pf_ke] a key, DS:w16pf_val a name: AX = 0
; the key is that name, 1 it is below it ([cs:w16r_tail] where the rest
; starts), 2 neither. The root ("") has every key below it. Keeps the rest
w16r_under:
    push bx
    push si
    mov bx, [w16pf_ks]
    mov si, w16pf_val
    cmp byte [si], 0
    je .below
.c:
    mov al, [si]
    or al, al
    jz .end
    cmp bx, [w16pf_ke]
    jae .no
    mov ah, [es:bx]
    call w16r_fold
    cmp al, ah
    jne .no
    inc si
    inc bx
    jmp short .c
.end:
    cmp bx, [w16pf_ke]
    je .same
    cmp byte [es:bx], '\'
    jne .no
    inc bx
.below:
    mov [cs:w16r_tail], bx
    mov ax, 1
    jmp short .out
.same:
    xor ax, ax
    jmp short .out
.no:
    mov ax, 2
.out:
    pop si
    pop bx
    ret

; w16r_fold - AL and AH in upper case
w16r_fold:
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

w16r_s_file:  db 'REG.DAT', 0
w16r_s_sect:  db 'HKEY_CLASSES_ROOT', 0

; =============================================================================
; SHELL: ShellExecute and FindExecutable (SPEC.md 124.24.10). A document is
; opened the way a double-click opens it: OSAPI_PKG_START's document arm
; with the name left empty, so os8088's association (SPEC.md 54) picks the
; program - an .EXE's is DOS.O88, which hands an NE to a WIN16 of its own.
; The slot wants the gfx lock free, which a call from the program seldom
; has, so the launch is POSTED: the locator in WIN16's image, a wake, and
; w16_wake's first act (DOS.O88's shape, SPEC.md 96.33.17)
; =============================================================================

SE_ERR_FNF      equ 2
SE_ERR_PNF      equ 3
SE_ERR_OOM      equ 8
SE_ERR_NOASSOC  equ 31
W16C_SEOK       equ 33              ; above 32: done (not an instance handle)
W16C_LDBAD      equ 2               ; LD_EBAD: nothing on the machine opens it

; ShellExecute (SHELL.20) - args: +0 nShowCmd, +2 lpDirectory,
; +6 lpParameters, +10 lpFile, +14 lpOperation, +18 hwnd. "open" (or NULL)
; only; lpParameters and nShowCmd are not used
W16SYS w16c_shexec
    push si
    push di
    ICALL w16_args
    les si, [es:bx + 14]
    mov ax, es
    or ax, si
    jz .open
    mov di, w16c_s_open             ; XSEC-OK: read through cs:
    call w16c_cseq
    mov ax, SE_ERR_NOASSOC
    jne .out
.open:
    mov ax, SE_ERR_OOM              ; one launch waits at a time
    cmp byte [w16_shq], 0
    jne .out
    mov ax, 10
    mov bx, 2
    call w16c_path                  ; CF=1 AX = SE_ERR_FNF or SE_ERR_PNF
    jc .out
    call w16c_locate
    jc .out
    mov byte [w16_shq], 1
    mov bx, [w16_win]
    call OSAPI_WM_WAKE
    mov ax, W16C_SEOK
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16c_shrun - from w16_wake, the gfx lock free: the posted ShellExecute
; started. A document nothing on the machine opens says so in a toast
w16c_shrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [w16_shq], 0
    mov si, w16s_dir + w16s_dirlen - 1  ; "": the document names its program
    xor cx, cx
    xor dx, dx
    push ds
    pop es
    mov di, w16_shdoc
    call OSAPI_PKG_START
    jnc .out                        ; (any other refusal, the user was told)
    cmp al, W16C_LDBAD
    jne .out
    mov si, w16c_s_noprog           ; XSEC-OK: read through cs:
    mov di, w16c_toast              ; XSEC-OK: the part's buffer
.p:
    mov al, [cs:si]
    or al, al
    jz .n
    mov [cs:di], al
    inc si
    inc di
    jmp short .p
.n:
    mov si, w16_shdoc
.c:
    mov al, [si]
    mov [cs:di], al
    inc si
    inc di
    or al, al
    jnz .c
    push cs
    pop es
    mov si, w16c_toast              ; XSEC-OK: ES = CS
    xor cx, cx
    call OSAPI_TOAST
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; FindExecutable (SHELL.21) - args: +0 lpResult, +4 lpDirectory, +8 lpFile.
; A program (.EXE .COM .BAT .PIF) is its own; os8088's associations cannot
; name a Windows program for anything else
W16SYS w16c_findexe
    push si
    push di
    mov ax, 8
    mov bx, 4
    call w16c_path
    jc .empty
    mov si, w16v_path               ; XSEC-OK: read through cs:
    xor bx, bx                      ; BX = past the last name's '.', 0 none
.n:
    mov al, [cs:si]
    or al, al
    jz .e
    inc si
    cmp al, '.'
    je .dot
    cmp al, '\'
    je .sep
    cmp al, ':'
    jne .n
.sep:
    xor bx, bx
    jmp short .n
.dot:
    mov bx, si
    jmp short .n
.e:
    mov ax, SE_ERR_NOASSOC
    or bx, bx
    jz .empty
    mov di, w16c_exts               ; XSEC-OK: read through cs:
.x:
    cmp byte [cs:di], 0
    jne .try
    mov ax, SE_ERR_NOASSOC          ; (AL was the compare's)
    jmp short .empty
.try:
    push bx
    mov cx, 3
.xc:
    mov al, [cs:bx]
    call w16v_up
    cmp al, [cs:di]
    jne .xn
    inc bx
    inc di
    loop .xc
    cmp byte [cs:bx], 0
    pop bx
    je .prog
    jmp short .x
.xn:
    add di, cx
    pop bx
    jmp short .x
.prog:
    ICALL w16_args
    les di, [es:bx]
    mov si, w16v_path               ; XSEC-OK: read through cs:
.cp:
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    jnz .cp
    mov ax, W16C_SEOK
    jmp short .out
.empty:
    push ax
    ICALL w16_args
    les di, [es:bx]
    mov ax, es
    or ax, di
    jz .z
    mov byte [es:di], 0
.z:
    pop ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; w16c_path - AX = the call's word holding a file name, BX = its folder's
; (NULL or "" for the current one, and not used for a name with a drive or
; from the root): the path in w16v_path, and the file there. CF=1 AX =
; SE_ERR_FNF or SE_ERR_PNF. Keeps the rest
w16c_path:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, ax
    mov di, bx
    ICALL w16_args
    mov cx, [es:bx + si]            ; DX:CX = the name
    mov dx, [es:bx + si + 2]
    mov si, [es:bx + di]            ; BX:SI = the folder
    mov bx, [es:bx + di + 2]
    mov es, dx
    mov di, cx
    mov ax, es
    or ax, di
    jz .fnf
    cmp byte [es:di + 1], ':'
    je .whole
    cmp byte [es:di], '\'
    jne .join
.whole:
    xor bx, bx
    xor si, si
.join:
    mov di, w16v_path               ; XSEC-OK: the part's buffer
    call w16v_join
    jc .fnf
    mov dx, w16v_path               ; XSEC-OK: the part's buffer, ES = CS
    push cs
    pop es
    mov ax, 0x3D00
    ICALL w16_dcall
    jc .no
    mov bx, ax
    mov ah, 0x3E
    ICALL w16_dcall
    clc
    jmp short .out
.no:
    cmp ax, 3                       ; DOS's "path not found"
    je .pnf
.fnf:
    mov ax, SE_ERR_FNF
    stc
    jmp short .out
.pnf:
    mov ax, SE_ERR_PNF
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16c_locate - w16v_path, a file that is there: its name in capitals, its
; folder's cluster and its volume into w16_shdoc, OSAPI_PKG_START's locator.
; The folder is asked of the instance (OSAPI_FILE_HERE) with WIN16's DOS
; standing in it, then the DOS is put back. CF=1 AX = SE_ERR_FNF or
; SE_ERR_PNF
w16c_locate:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, w16v_path               ; XSEC-OK: read through cs:
    mov bx, si                      ; BX = the name's start
.n:
    mov al, [cs:si]
    or al, al
    jz .e
    inc si
    cmp al, '\'
    je .s
    cmp al, ':'
    jne .n
.s:
    mov bx, si
    jmp short .n
.e:
    mov ax, si
    sub ax, bx
    jz .fnf
    cmp ax, 12
    ja .fnf
    mov si, bx
    mov di, w16_shdoc
.c:
    mov al, [cs:si]
    call w16v_up
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    cmp bx, w16v_path               ; XSEC-OK: a bound. No folder: the one
    je .here                        ;   the DOS is standing in
    mov ah, 0x19                    ; the DOS's drive, and its folder there
    ICALL w16_dcall
    mov [cs:w16c_cdrv], al
    mov [cs:w16c_tdrv], al
    cmp byte [cs:w16v_path + 1], ':'
    jne .save
    mov al, [cs:w16v_path]
    call w16v_up
    sub al, 'A'
    mov [cs:w16c_tdrv], al
.save:
    mov byte [cs:w16v_path2], '\'
    mov si, w16v_path2 + 1          ; XSEC-OK: the part's buffer, ES = CS
    xor dl, dl
    call w16c_cwd
    jc .pnf
    mov al, [cs:w16c_tdrv]          ; another drive's folder moves too
    cmp al, [cs:w16c_cdrv]
    je .go
    mov ah, ':'
    add al, 'A'
    mov [cs:w16c_ocwd], ax
    mov byte [cs:w16c_ocwd + 2], '\'
    mov si, w16c_ocwd + 3           ; XSEC-OK: the part's buffer, ES = CS
    mov dl, [cs:w16c_tdrv]
    inc dl
    call w16c_cwd
    jc .pnf
.go:
    call w16c_seld                  ; the document's drive, if another
    mov si, bx                      ; the folder: up to the name, keeping a
    dec si                          ;   root's '\' ("\NAME", "X:\NAME")
    cmp byte [cs:si], ':'
    je .moved                       ; "X:NAME": where that drive stands (CF=0)
    cmp si, w16v_path               ; XSEC-OK: a bound
    je .root
    cmp byte [cs:si - 1], ':'
    jne .cut
.root:
    inc si
.cut:
    mov al, [cs:si]
    mov byte [cs:si], 0
    mov dx, w16v_path               ; XSEC-OK: the part's buffer, ES = CS
    call w16c_cd
    mov [cs:si], al
.moved:
    jc .undo
    call OSAPI_FILE_HERE
    mov [w16_shdoc + 13], dx
    mov [w16_shdoc + 15], bl
    mov al, [cs:w16c_tdrv]          ; ...and the DOS put back
    cmp al, [cs:w16c_cdrv]
    je .back
    mov dx, w16c_ocwd               ; XSEC-OK: the part's buffer, ES = CS
    call w16c_cd
    call w16c_selc
.back:
    mov dx, w16v_path2              ; XSEC-OK: the part's buffer, ES = CS
    call w16c_cd
    clc
    jmp short .out
.undo:
    call w16c_selc                  ; a folder that is not there: only the
    jmp short .pnf                  ;   drive had moved
.here:
    call OSAPI_FILE_HERE
    mov [w16_shdoc + 13], dx
    mov [w16_shdoc + 15], bl
    clc
    jmp short .out
.fnf:
    mov ax, SE_ERR_FNF
    stc
    jmp short .out
.pnf:
    mov ax, SE_ERR_PNF
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16c_cwd - DL = a drive (0 its own): its folder, DOS's AH=47h, at CS:SI.
; CF=1 it has none. Keeps all but AX
w16c_cwd:
    push es
    push cs
    pop es
    mov ah, 0x47
    ICALL w16_dcall
    pop es
    ret

; w16c_cd - CS:DX = a folder: WIN16's DOS there (AH=3Bh). CF=1 it is not.
; Keeps all
w16c_cd:
    push ax
    push es
    push cs
    pop es
    mov ah, 0x3B
    ICALL w16_dcall
    pop es
    pop ax
    ret

; w16c_seld / w16c_selc - WIN16's DOS on the document's drive, and back on
; its own (AH=0Eh), when the two differ. A chdir on another drive moves that
; drive's folder and not the DOS, and OSAPI_FILE_HERE asks where the DOS is.
; Keeps all
w16c_seld:
    push dx
    mov dl, [cs:w16c_tdrv]
    jmp short w16c_sel
w16c_selc:
    push dx
    mov dl, [cs:w16c_cdrv]
w16c_sel:
    pushf
    push ax
    mov al, [cs:w16c_tdrv]
    cmp al, [cs:w16c_cdrv]
    je .out
    mov ah, 0x0E
    ICALL w16_dcall
.out:
    pop ax
    popf
    pop dx
    ret

; w16c_cseq - ES:SI and CS:DI, two strings: ZF=1 the same, case ignored.
; Keeps all
w16c_cseq:
    push ax
    push si
    push di
.c:
    mov al, [es:si]
    call w16v_up
    mov ah, al
    mov al, [cs:di]
    call w16v_up
    cmp al, ah
    jne .out
    inc si
    inc di
    or al, al
    jnz .c
.out:
    pop di
    pop si
    pop ax
    ret

w16c_s_open:    db 'open', 0
w16c_s_noprog:  db 'No program: ', 0
w16c_exts:      db 'EXECOMBATPIF', 0
w16c_cdrv:      db 0                ; the DOS's drive, and the document's
w16c_tdrv:      db 0
w16c_ocwd:      times 68 db 0       ; the other drive's folder, "X:\..."
w16c_toast:     times 26 db 0

; =============================================================================
; OLECLI and OLESVR (SPEC.md 124.24.7): Windows 3.1 with no OLE server
; registered and no object on the clipboard. Eight answers; w16api.inc's rows
; say which call gets which
; =============================================================================

OLE_ERROR_FORMAT equ 12
OLE_ERROR_OBJECT equ 13
OLE_ERROR_CLASS  equ 20

; OLE_OK, or a 0 answer
W16SYS w16o_ok
    xor ax, ax
    xor dx, dx
    ret

; nothing on the clipboard to paste as an object
W16SYS w16o_fmt
    mov ax, OLE_ERROR_FORMAT
    jmp short w16o_ret

; no server registered for the class: an object is never made
W16SYS w16o_class
    mov ax, OLE_ERROR_CLASS
    jmp short w16o_ret

; there is no object
W16SYS w16o_obj
    mov ax, OLE_ERROR_OBJECT
w16o_ret:
    xor dx, dx
    ret

; OLE_OK, and a handle, 1, where the far pointer at +4 (OleRegisterServer)
; or +0 (OleRegisterClientDoc, OleRegisterServerDoc) says
W16SYS w16o_hnd4
    mov ax, 4
    mov cx, 1
    jmp short w16o_put
W16SYS w16o_hnd0
    xor ax, ax
    mov cx, 1
    jmp short w16o_put

; OLE_OK, and NULL where the far pointer at +0 says (OleEnumObjects: the
; document holds no object)
W16SYS w16o_nul0
    xor ax, ax
    xor cx, cx
w16o_put:
    push si
    push ax
    ICALL w16_args
    pop si
    les bx, [es:bx + si]
    mov ax, es
    or ax, bx
    jz .none
    mov [es:bx], cx
    mov word [es:bx + 2], 0
.none:
    pop si
    jmp short w16o_ok

; OleUnblockServer: OLE_OK, and *lpfRequest FALSE - nothing is waiting
W16SYS w16o_bool0
    ICALL w16_args
    les bx, [es:bx]
    mov ax, es
    or ax, bx
    jz w16o_ok
    mov word [es:bx], 0
    jmp short w16o_ok

; =============================================================================
; The system's parameters (SPEC.md 124.26.9)
; =============================================================================

; SystemParametersInfo (USER.483) - args: +0 fuWinIni, +2 lpvParam,
; +6 uParam, +8 uAction. ControlPanelInfo (USER.273) - args: +0 lpBuffer,
; +4 wData, +6 nInfo: Windows 3.0's undocumented name for the same actions.
; A query in w16u_spitab writes Windows' default into the buffer; every other
; action answers TRUE and changes nothing, because the settings are os8088's
; own Control Panel's
W16SYS w16u_cpinfo
    push si
    push di
    ICALL w16_args
    mov si, [es:bx + 6]
    les di, [es:bx]
    jmp short w16u_spigo
W16SYS w16u_spi
    push si
    push di
    ICALL w16_args
    mov si, [es:bx + 8]
    les di, [es:bx + 2]
w16u_spigo:
    mov bx, w16u_spitab             ; XSEC-OK: read through CS below
.f:
    xor ax, ax
    mov al, [cs:bx]                 ; XSEC-OK: this part's own table
    or al, al
    jz .yes
    mov cl, [cs:bx + 1]             ; XSEC-OK: (the same)
    xor ch, ch
    inc bx
    inc bx
    cmp ax, si
    je .hit
    add bx, cx
    add bx, cx
    jmp short .f
.hit:
    mov ax, es                      ; no buffer: 13 and 24 setting, not asking
    or ax, di
    jz .yes
    cld
.c:
    mov ax, [cs:bx]                 ; XSEC-OK: (the same)
    stosw
    inc bx
    inc bx
    loop .c
.yes:
    mov ax, 1
    pop di
    pop si
    ret

; a query: its action, its words, Windows 3.1's defaults on VGA
w16u_spitab:
    db 1, 1                         ; SPI_GETBEEP
    dw 1
    db 3, 3                         ; SPI_GETMOUSE: the two thresholds and
    dw 6, 10, 1                     ;   the acceleration
    db 5, 1                         ; SPI_GETBORDER
    dw 3
    db 10, 1                        ; SPI_GETKEYBOARDSPEED
    dw 31
    db 13, 1                        ; SPI_ICONHORIZONTALSPACING
    dw 75
    db 14, 1                        ; SPI_GETSCREENSAVETIMEOUT
    dw 120
    db 16, 1                        ; SPI_GETSCREENSAVEACTIVE
    dw 0
    db 18, 1                        ; SPI_GETGRIDGRANULARITY
    dw 0
    db 22, 1                        ; SPI_GETKEYBOARDDELAY
    dw 1
    db 24, 1                        ; SPI_ICONVERTICALSPACING
    dw 72
    db 25, 1                        ; SPI_GETICONTITLEWRAP
    dw 1
    db 27, 1                        ; SPI_GETMENUDROPALIGNMENT
    dw 0
    db 32, 1                        ; SPI_GETFASTTASKSWITCH
    dw 1
    db 0

; the registration database's words, in the part: they go with the program
w16r_hit:   db 0
w16r_idx:   dw 0
w16r_tail:  dw 0
w16r_vseg:  dw 0
w16r_vs:    dw 0
w16r_ve:    dw 0
w16r_keys:  times W16R_N * W16R_SZ db 0     ; the open keys' rows

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
