; =============================================================================
; os8088 - apps/win16/w16sys.inc
;
; WHAT THE CONTROL PANEL CALLS (SPEC.md 124.20.5): the directories, the
; module and library calls and the system settings a Control Panel reads and
; writes. All of it is the library part's (SPEC.md 124.19.3).
; =============================================================================

section .w16dlg

; GetWindowsDirectory (KERNEL.134), GetSystemDirectory (KERNEL.135) - the
; folder WIN.INI is in (SPEC.md 124.14) as a DOS path: "A:\SYSTEM\APPDATA\
; WIN16" on WIN16's own volume. One folder answers both: WIN16 has no SYSTEM
; folder of its own. args: +0 nSize, +2 lpBuffer. Answers the length; a
; buffer too small for it answers the size it needs, NUL included, as Windows
; did, and is left alone
W16LIB w16k_windir
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    les di, [es:bx + 2]
    mov ax, w16s_dirlen + 1
    cmp cx, ax
    jb .out
    mov al, [w16_homev]
    add al, 'A'
    cld
    stosb
    mov si, w16s_dir
    mov cx, w16s_dirlen
    rep movsb                       ; the NUL with it
    mov ax, w16s_dirlen
.out:
    pop di
    pop si
    ret

; OpenFile (KERNEL.74) - every file is absent until WIN16 has files (S16):
; HFILE_ERROR, the OFSTRUCT's nErrCode 2 (file not found), its path empty.
; The Control Panel asks OF_EXIST for SETUP.INF and goes on without it.
; args: +0 wStyle, +2 lpReOpenBuff, +6 lpFileName
W16LIB w16k_openfile
    push di
    ICALL w16_args
    les di, [es:bx + 2]
    mov word [es:di], 136           ; cBytes, fFixedDisk 0
    mov word [es:di + 2], 2         ; nErrCode: not found
    mov byte [es:di + 8], 0         ; szPathName
    pop di
    mov ax, -1
    ret

; _lclose (KERNEL.81) - no file is open to close: HFILE_ERROR
W16LIB w16k_lclose
    mov ax, -1
    ret

; LoadLibrary (KERNEL.95) - no DLL loads yet (S18): 2, file not found, as a
; library that is not on the path answered. FreeLibrary (KERNEL.96) has
; nothing to free
W16LIB w16k_loadlib
    mov ax, 2
    ret
W16LIB w16k_freelib
    ret

; The network (USER.512-520): there is none. WNetGetCaps answers 0, no
; capabilities, and every other WNet call WN_NOT_SUPPORTED (1), as Windows
; answered without a network driver
W16LIB w16u_wnetcaps
    xor ax, ax
    ret
W16LIB w16u_wnetno
    mov ax, 1
    ret

; OemToAnsi (KEYBOARD.6) - args: +0 lpAnsiStr, +4 lpOemStr. WIN16 does not
; translate between the OEM and ANSI sets (SPEC.md 124.20.5), so the string
; is copied as it is; the same buffer twice is legal
W16LIB w16kb_oem2ansi
    push si
    push di
    push ds
    ICALL w16_args
    les di, [es:bx]
    lds si, [es:bx + 4]
    cld
.c:
    lodsb
    stosb
    or al, al
    jnz .c
    pop ds
    pop di
    pop si
    mov ax, -1
    ret

; EqualRect (USER.244) - args: +0 lprc2, +4 lprc1
W16LIB w16u_eqrect
    push si
    push di
    push ds
    ICALL w16_args
    lds si, [es:bx + 4]
    les di, [es:bx]
    mov cx, 4
    cld
    repe cmpsw
    pop ds
    pop di
    pop si
    mov ax, 0
    jne .out
    inc ax
.out:
    ret

section .text

w16s_dir:    db ':\SYSTEM\APPDATA\WIN16', 0
w16s_dirlen  equ $ - w16s_dir
