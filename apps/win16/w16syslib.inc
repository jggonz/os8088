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
