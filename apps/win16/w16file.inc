; =============================================================================
; os8088 - apps/win16/w16file.inc
;
; FILES (SPEC.md 124.21). A Windows program reaches a file through KERNEL's
; _lopen family and OpenFile, or through int 21h, which its C runtime issues
; itself. Both arrive at ONE DOS, and it is DOS.O88's: the core DOS.O88 ships
; (build/doscore.bin) and WIN16's host part (apps/win16/w16dosh.asm) in one
; claim, fetched the first time a program asks for a file (124.21.1).
;
; The image half is the claim, the forward and the call; the KERNEL calls are
; the library part's (SPEC.md 124.19.3).
; =============================================================================

OF_READWRITE    equ 0x0003          ; the access bits OpenFile hands to 3Dh
OF_PARSE        equ 0x0100
OF_DELETE       equ 0x0200
OF_CREATE       equ 0x1000
OF_EXIST        equ 0x4000
OF_REOPEN       equ 0x8000
OFS_ERR         equ 2               ; OFSTRUCT: nErrCode
OFS_PATH        equ 8               ;   ...szPathName, 128 bytes
TF_FORCEDRIVE   equ 0x80

; -----------------------------------------------------------------------------
; w16_dosup - WIN16's DOS, here (SPEC.md 124.21.1). On the UI task's stack,
; DS = ours. out: CF=0; CF=1 it could not be had (memory, or the parts).
; Keeps every register.
; -----------------------------------------------------------------------------
w16_dosup:
    cmp word [w16_dhseg], 0
    je .load
    clc
    ret
.load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, W16DH_KB
    call OSAPI_MEM_CLAIM            ; DX = the block
    jc .out
    mov [w16_dhtmp], dx
    mov es, dx                      ; zero: the core's bss and the host's
    xor di, di
    xor ax, ax
    mov cx, W16DH_KB * 512
    cld
    rep stosw
    mov al, W16P_DCORE              ; the core at CORE_ORG...
    mov di, CORE_ORG
    call w16_dhcopy
    jc .free
    mov al, W16P_DHOST              ; ...and the host above its bss
    mov di, W16DH_ORG
    call w16_dhcopy
    jc .free
    mov dx, [w16_dir]               ; the window: DOS_WKB KB, or one cluster
    mov bl, [w16_vol]               ;   of the program's volume where that
    call OSAPI_FILE_GOTO_QM         ;   is larger (SPEC.md 96.11)
    call OSAPI_FILE_DFREE           ; BX = sectors a cluster
    jnc .clus
    xor bx, bx
.clus:
    shr bx, 1                       ; KB
    mov es, [w16_dhtmp]
    mov al, [es:W16DH_WKB]          ; the DOS's own floor (SPEC.md 96.11)
    xor ah, ah
    cmp bx, ax
    jae .kb
    mov bx, ax
.kb:
    mov ax, bx
    push bx
    call OSAPI_MEM_CLAIM            ; DX = the window
    pop si
    jc .free
    mov [w16_dhwin], dx
    mov cl, 6
    shl si, cl                      ; SI = its paragraphs
    mov cx, dx
    mov ax, [w16_dhtmp]
    mov word [w16_dhent], W16DH_INIT
    mov [w16_dhent + 2], ax
    mov ax, [w16_psp]               ; AX = the PSP, BL = the volume, DX = the
    mov bl, [w16_vol]               ;   folder, CX:SI = the window
    mov dx, [w16_dir]
    call far [w16_dhent]
    mov word [w16_dhent], W16DH_I21
    jc .nowin
    mov ax, [w16_dhtmp]
    mov [w16_dhseg], ax             ; ready
    clc
    jmp short .out
.nowin:
    xor dx, dx
    xchg dx, [w16_dhwin]
    call OSAPI_MEM_FREE
.free:
    mov dx, [w16_dhtmp]
    call OSAPI_MEM_FREE
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_dhcopy - part AL into [w16_dhtmp]:DI, fetched, copied and dropped, as
; DOS.O88's loader puts its core in place (SPEC.md 96.44.5.3). CF=1 not had.
w16_dhcopy:
    push di                         ; (op_fetch spends DI)
    push ax
    call w16_pfetch                 ; AX = where it is
    pop bx                          ; BL = the part
    pop di
    jc .no
    push ax
    mov al, bl
    call op_row                     ; SI -> its row: the unpacked length
    mov cx, [si + OP_R_LEN]
    inc cx
    shr cx, 1
    pop ax
    push ds
    mov es, [w16_dhtmp]
    mov ds, ax
    xor si, si
    cld
    rep movsw
    pop ds
    mov al, bl
    call op_drop
    clc
.no:
    ret

; -----------------------------------------------------------------------------
; w16_dosend - the program has ended: every file it left open closed, which
; writes what the window holds, and the DOS given back. Keeps all.
; -----------------------------------------------------------------------------
w16_dosend:
    cmp word [w16_dhseg], 0
    je .ret
    push dx
    push es
    mov es, [w16_dhseg]
    mov [es:W16DH_HSS], ss
    mov word [w16_dhent], W16DH_DONE
    call far [w16_dhent]
    mov word [w16_dhent], W16DH_I21
    xor dx, dx
    xchg dx, [w16_dhwin]
    call OSAPI_MEM_FREE
    xor dx, dx
    xchg dx, [w16_dhseg]
    call OSAPI_MEM_FREE
    pop es
    pop dx
.ret:
    ret

; -----------------------------------------------------------------------------
; w16_dcall - an int 21h call to WIN16's DOS from a handler (SPEC.md 124.21.3)
; in:  AX, BX, CX, DX, SI, DI as the call takes them, ES = the DS it takes;
;      on the UI task's stack, DS = ours
; out: as the call answers, CF included; DS and ES kept. CF=1 AX=8 when the
;      DOS could not be had
; -----------------------------------------------------------------------------
w16_dcall:
    call w16_dosup
    jnc .go
    mov ax, 8                       ; not enough memory
    ret
.go:
    push ds
    push es
    push ax
    mov es, [w16_dhseg]
    mov [es:W16DH_HSS], ss          ; the stack it runs on is this one
    mov ax, [w16_hsp]
    mov [es:W16DH_HSP], ax
    pop ax
    pop es
    push es
    push es
    pop ds                          ; DS = the call's
    pushf                           ; ...and an int's frame
    call far [cs:w16_dhent]
    mov [cs:w16_dces], es           ; the ES it answers (2Fh's), banked
    pop es
    pop ds
    ret

; -----------------------------------------------------------------------------
; w16_i21dos - w16_i21's forward (SPEC.md 124.21.2): on the PROGRAM's stack,
; IF = 0, the int's frame on top and every register the program's. The DOS
; is fetched first, on the UI task's stack, if the program has none yet.
; -----------------------------------------------------------------------------
w16_i21dos:
    ; STKBALANCE-OK: leaves by the core's iret, or by .nodos's
    cmp word [cs:w16_dhseg], 0
    jne .go
    mov [cs:w16_i21ss], ss          ; to the UI task's stack for the fetch
    mov [cs:w16_i21sp], sp
    mov ss, [cs:w16_hss]
    mov sp, [cs:w16_hsp]
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds
    push word [w16_hflags]          ; interrupts as the host has them: a disk
    popf                            ;   is read
    cld
    call w16_dosup
    cli
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov ss, [cs:w16_i21ss]
    mov sp, [cs:w16_i21sp]
    cmp word [cs:w16_dhseg], 0
    je .nodos
.go:
    push es
    push ax
    mov es, [cs:w16_dhseg]
    mov ax, [cs:w16_hss]
    mov [es:W16DH_HSS], ax
    mov ax, [cs:w16_hsp]
    mov [es:W16DH_HSP], ax
    pop ax
    pop es
    jmp far [cs:w16_dhent]          ; the core's gate, which irets
.nodos:
    push bp
    mov bp, sp
    mov ax, 8                       ; not enough memory
    or word [bp + W16_IRET_FL], 1
    pop bp
    iret

; the int 21h functions w16_i21 hands to the DOS (SPEC.md 124.21.2)
w16_i21fwd:
    db 0x0D, 0x0E, 0x19, 0x1A, 0x29, 0x2F, 0x36, 0x39, 0x3A, 0x3B, 0x3C
    db 0x3D, 0x3F, 0x41, 0x42, 0x43, 0x47, 0x4E, 0x4F, 0x56, 0x68, 0x6A
W16_NI21FWD equ $ - w16_i21fwd

; =============================================================================
; KERNEL's file calls (SPEC.md 124.21.3): the library part's
; =============================================================================

section .w16dlg

; _lopen (KERNEL.85) - args: +0 wMode, +2 lpPathName
W16LIB w16k_lopen
    ICALL w16_args
    mov al, [es:bx]
    mov ah, 0x3D
    jmp short w16f_named

; _lcreat (KERNEL.83) - args: +0 wAttr, +2 lpPathName
W16LIB w16k_lcreat
    ICALL w16_args
    mov cx, [es:bx]
    mov ah, 0x3C
w16f_named:                         ; AH (and AL, CX) set, ES:BX -> the args:
    mov dx, [es:bx + 2]             ;   DS:DX = the name at +2
    mov es, [es:bx + 4]
    ICALL w16_dcall
w16f_ax:                            ; AX, or HFILE_ERROR
    jnc .out
    mov ax, -1
.out:
    ret

; _lread (KERNEL.82), _lwrite (KERNEL.86) - args: +0 wBytes, +2 lpBuffer,
; +6 hFile. The count, or -1
W16LIB w16k_lread
    mov ah, 0x3F
    jmp short w16f_rw
W16LIB w16k_lwrite
    mov ah, 0x40
w16f_rw:
    push ax
    ICALL w16_args
    pop ax
    mov cx, [es:bx]
    mov dx, [es:bx + 2]
    push word [es:bx + 6]
    mov es, [es:bx + 4]
    pop bx
    ICALL w16_dcall
    jmp short w16f_ax

; _llseek (KERNEL.84) - args: +0 nOrigin, +2 lOffset, +6 hFile. DX:AX = the
; new position, or -1
W16LIB w16k_llseek
    ICALL w16_args
    mov al, [es:bx]
    mov ah, 0x42
    mov dx, [es:bx + 2]
    mov cx, [es:bx + 4]
    mov bx, [es:bx + 6]
    push ds
    pop es
    ICALL w16_dcall
    jnc .out
    mov ax, -1
    cwd
.out:
    ret

; _lclose (KERNEL.81) - args: +0 hFile. 0, or -1
W16LIB w16k_lclose
    ICALL w16_args
    mov bx, [es:bx]
    mov ah, 0x3E
    push ds
    pop es
    ICALL w16_dcall
    mov ax, 0
    jnc .out
    dec ax
.out:
    ret

; -----------------------------------------------------------------------------
; OpenFile (KERNEL.74) - SPEC.md 124.21.3, Wine's OpenFile16 in what it does
; args: +0 wStyle, +2 lpReOpenBuff, +6 lpFileName. The handle; 1 for
; OF_EXIST and OF_DELETE; 0 for OF_PARSE; -1 with the OFSTRUCT's nErrCode.
; OF_PROMPT puts up nothing: the call fails as it would after Cancel.
; -----------------------------------------------------------------------------
W16LIB w16k_openfile
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx]
    mov [w16f_style], ax
    les di, [es:bx + 2]             ; the OFSTRUCT
    mov [w16f_ofs], di
    mov [w16f_ofs + 2], es
    mov word [es:di], 136           ; cBytes, and fFixedDisk 0
    mov word [es:di + OFS_ERR], 0
    test ax, OF_REOPEN              ; the name: the path it was opened by, or
    jz .arg                         ;   the argument
    lea si, [di + OFS_PATH]
    mov ax, es
    jmp short .cp
.arg:
    ICALL w16_args
    mov si, [es:bx + 6]
    mov ax, [es:bx + 8]
.cp:
    mov di, w16f_name
    mov cx, 127
    call w16f_copy                  ; AX:SI -> ours
    mov si, w16f_name
    call w16f_full                  ; -> w16f_path
    jc .err
    mov ax, [w16f_style]
    test ax, OF_PARSE
    jz .use
    call w16f_putpath
    xor ax, ax
    jmp short .out
.use:
    test ax, OF_CREATE
    jz .find
    mov ah, 0x3C
    xor cx, cx
    call w16f_onpath
    jc .err
    jmp short .opened
.find:
    mov ax, 0x4300                  ; is it where it was named?
    call w16f_onpath
    jnc .found
    call w16f_bare                  ; a bare name is looked for in the
    jnz .err                        ;   Windows directory next (AX = 2 still)
    mov di, w16f_path
    call w16f_windir
    mov si, w16f_name
    call w16f_cat
    mov ax, 0x4300
    call w16f_onpath
    jc .err
.found:
    mov ax, [w16f_style]
    test ax, OF_DELETE
    jz .open
    mov ah, 0x41
    call w16f_onpath
    jc .err
    call w16f_putpath
    mov ax, 1
    jmp short .out
.open:
    and al, OF_READWRITE            ; the share bits are not DOS 2's
    mov ah, 0x3D
    call w16f_onpath
    jc .err
.opened:
    call w16f_putpath
    test word [w16f_style], OF_EXIST
    jz .out
    mov bx, ax
    mov ah, 0x3E
    push ds
    pop es
    ICALL w16_dcall
    mov ax, 1
.out:
    pop di
    pop si
    ret
.err:
    les di, [w16f_ofs]
    mov [es:di + OFS_ERR], ax
    mov ax, -1
    jmp short .out

; w16f_onpath - AH (AL, CX) on w16f_path: DS:DX = it. As w16_dcall answers
w16f_onpath:
    push es
    push ds
    pop es
    mov dx, w16f_path
    ICALL w16_dcall
    pop es
    ret

; w16f_putpath - w16f_path into the OFSTRUCT's szPathName. Keeps AX
w16f_putpath:
    push ax
    push si
    push di
    push es
    les di, [w16f_ofs]
    add di, OFS_PATH
    mov si, w16f_path
    mov cx, 127
    cld
.c:
    lodsb
    stosb
    or al, al
    loopnz .c
    mov byte [es:di], 0
    pop es
    pop di
    pop si
    pop ax
    ret

; w16f_copy - AX:SI, a NUL-ended string, into DS:DI, at most CX bytes and
; ended. Clobbers AX, CX, SI, DI
w16f_copy:
    push es
    push ds
    pop es
    push ds
    mov ds, ax
    cld
.c:
    lodsb
    stosb
    or al, al
    loopnz .c
    pop ds
    mov byte [di], 0
    pop es
    ret

; w16f_bare - ZF=1 when w16f_name names no drive or folder. Keeps all but F
w16f_bare:
    push ax
    push si
    mov si, w16f_name
    cld
.c:
    lodsb
    cmp al, ':'
    je .no
    cmp al, '\'
    je .no
    cmp al, '/'
    je .no
    or al, al
    jnz .c
    jmp short .out                  ; ZF=1
.no:
    or al, al                       ; ZF=0 (AL is not 0)
.out:
    pop si
    pop ax
    ret

; w16f_windir - the Windows directory (SPEC.md 124.20.5) at DI, then '\';
; DI past it. Clobbers AX, CX, SI
w16f_windir:
    push es
    push ds
    pop es
    mov al, [w16_homev]
    add al, 'A'
    cld
    stosb
    mov si, w16s_dir
    mov cx, w16s_dirlen - 1
    rep movsb
    mov al, '\'
    stosb
    pop es
    ret

; w16f_cat - DS:SI, NUL-ended, at DI (in DS), upper-cased, '/' made '\'.
; Clobbers AX, SI, DI
w16f_cat:
    cld
.c:
    lodsb
    cmp al, 'a'
    jb .s
    cmp al, 'z'
    ja .s
    sub al, 'a' - 'A'
.s:
    cmp al, '/'
    jne .p
    mov al, '\'
.p:
    mov [di], al
    inc di
    or al, al
    jnz .c
    ret

; w16f_full - DS:SI, a name as a program gives it, into w16f_path as a full
; DOS path: its drive, or the current one; its folder from the root, or the
; current folder's. CF=1 AX = the DOS error. Clobbers AX, BX, CX, DX, SI, DI
w16f_full:
    mov di, w16f_path
    cmp byte [si + 1], ':'
    jne .cur
    mov al, [si]
    and al, 0xDF                    ; upper case
    sub al, 'A'
    inc si
    inc si
    jmp short .drv
.cur:
    push si
    mov ah, 0x19                    ; AL = the current drive
    push es
    push ds
    pop es
    ICALL w16_dcall
    pop es
    pop si
    jc .ret
.drv:
    mov dl, al
    add al, 'A'
    mov [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], '\'
    add di, 3
    cmp byte [si], '\'
    je .root
    cmp byte [si], '/'
    je .root
    push si                         ; the drive's current folder, from 47h
    inc dl                          ;   (1 = A:), with no leading '\'
    mov si, di
    mov ah, 0x47
    push es
    push ds
    pop es
    ICALL w16_dcall
    pop es
    pop si
    jc .ret
.end:
    cmp byte [di], 0
    je .sep
    inc di
    jmp short .end
.sep:
    cmp di, w16f_path + 3
    je .name                        ; the root: no second '\'
    mov byte [di], '\'
    inc di
    jmp short .name
.root:
    inc si
.name:
    call w16f_cat
    clc
.ret:
    ret

; -----------------------------------------------------------------------------
; GetTempFileName (KERNEL.97) - SPEC.md 124.21.3: X:\...\~PPPuuuu.TMP in the
; Windows directory, or at the root of the drive TF_FORCEDRIVE names. With
; uUnique 0 a number from the clock, stepped until the name is free, and the
; file CREATED. args: +0 lpTempFileName, +4 uUnique, +6 lpPrefixString,
; +10 cDriveLetter. The number
; -----------------------------------------------------------------------------
W16LIB w16k_tempname
    push si
    push di
    ICALL w16_args
    mov di, w16f_path
    mov al, [es:bx + 10]
    test al, TF_FORCEDRIVE
    jz .win
    and al, 0x7F                    ; 'C' or 0: the current drive
    jnz .letter
    push es
    push bx
    mov ah, 0x19
    push ds
    pop es
    ICALL w16_dcall
    pop bx
    pop es
    add al, 'A'
.letter:
    and al, 0xDF
    mov [di], al
    mov word [di + 1], ':' + ('\' << 8)
    add di, 3
    jmp short .pre
.win:
    push es
    push bx
    call w16f_windir
    pop bx
    pop es
.pre:
    mov byte [di], '~'
    inc di
    mov ax, ds                      ; the prefix: its first three characters
    push ds
    push es
    lds si, [es:bx + 6]
    mov es, ax                      ; ES = ours, for the stores
    mov cx, 3
    cld
.p:
    lodsb
    or al, al
    jz .pend
    cmp al, 'a'
    jb .pu
    cmp al, 'z'
    ja .pu
    sub al, 'a' - 'A'
.pu:
    stosb
    loop .p
.pend:
    pop es
    pop ds
    mov [w16f_num], di              ; where the four digits go
    mov ax, [es:bx + 4]
    or ax, ax
    jnz .given
    push es
    xor ax, ax                      ; a number from the clock: the BIOS's tick
    mov es, ax
    mov ax, [es:0x046C]
    pop es
    or ax, ax
    jnz .try
    inc ax
.try:
    mov [w16f_unq], ax
    call w16f_hex4
    push es
    push bx
    mov ax, 0x4300                  ; free?
    call w16f_onpath
    jc .free
    pop bx
    pop es
    mov ax, [w16f_unq]
    inc ax
    jnz .try
    inc ax
    jmp short .try
.free:
    mov ah, 0x3C                    ; made, so a second call cannot answer it
    xor cx, cx
    call w16f_onpath
    jc .made
    mov bx, ax
    mov ah, 0x3E
    push ds
    pop es
    ICALL w16_dcall
.made:
    pop bx
    pop es
    mov ax, [w16f_unq]
    jmp short .copy
.given:
    mov [w16f_unq], ax
    call w16f_hex4
.copy:
    push ax
    les di, [es:bx]                 ; the answer's buffer
    mov si, w16f_path
    cld
.c:
    lodsb
    stosb
    or al, al
    jnz .c
    pop ax
    pop di
    pop si
    ret

; w16f_hex4 - [w16f_unq] as four hex digits and ".TMP" at [w16f_num]. Keeps
; all but flags
w16f_hex4:
    push ax
    push bx
    push cx
    push di
    mov di, [w16f_num]
    mov bx, [w16f_unq]
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol bx, cl
    pop cx
    mov al, bl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put
    add al, 'A' - '0' - 10
.put:
    mov [di], al
    inc di
    loop .d
    mov word [di], '.T'
    mov word [di + 2], 'MP'
    mov byte [di + 4], 0
    pop di
    pop cx
    pop bx
    pop ax
    ret

; AnsiToOem (KEYBOARD.5) is OemToAnsi's copy (SPEC.md 124.21.3).
; AnsiToOemBuff (KEYBOARD.134), OemToAnsiBuff (KEYBOARD.135) - args: +0
; nLength, +2 lpDest, +6 lpSrc: the bytes copied as they are
W16LIB w16kb_xlatbuf
    push si
    push di
    push ds
    ICALL w16_args
    mov cx, [es:bx]
    lds si, [es:bx + 6]
    les di, [es:bx + 2]
    cld
    rep movsb
    pop ds
    pop di
    pop si
    ret


; -----------------------------------------------------------------------------
; DlgDirList (USER.100) - SPEC.md 124.21.3: the dialog's list box filled with
; the files the spec names, then "[dir]" and "[-a-]" as wFiletype asks, its
; static control given the current folder, both in lower case as Windows 3.0
; showed them. A folder in the spec is moved to first, and the spec is left
; holding its file part. args: +0 wFiletype, +2 nIDStaticPath, +4
; nIDListBox, +6 lpPathSpec, +10 hDlg. TRUE, or FALSE for a folder or drive
; that is not there
; -----------------------------------------------------------------------------
DDL_DIRECTORY   equ 0x0010
DDL_DRIVES      equ 0x4000
DDL_EXCLUSIVE   equ 0x8000
DDL_FILEATTR    equ 0x0027              ; read-only, hidden, system, archive

W16LIB w16u_dlgdirlist
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 10]
    mov [w16d_hdlg], ax
    mov ax, [es:bx]
    mov [w16f_ddl], ax
    mov si, [es:bx + 6]
    mov ax, [es:bx + 8]
    mov di, w16f_name
    mov cx, 127
    call w16f_copy
    cmp byte [w16f_name], 0
    jne .spec
    mov word [w16f_name], '*.'
    mov word [w16f_name + 2], '*'
.spec:
    mov si, w16f_name               ; SI -> the file part: past the last ':'
    mov di, si                      ;   or '\'
.split:
    lodsb
    or al, al
    jz .split2
    cmp al, ':'
    je .sep
    cmp al, '\'
    je .sep
    cmp al, '/'
    jne .split
.sep:
    mov di, si
    jmp short .split
.split2:
    mov [w16f_num], di              ; (the file part, kept)
    cmp di, w16f_name
    je .here
    cmp byte [w16f_name + 1], ':'   ; a drive: there first
    jne .dir
    mov dl, [w16f_name]
    and dl, 0xDF
    sub dl, 'A'
    mov ah, 0x0E
    call w16f_call
    mov ah, 0x19                    ; ...and it must be the one now
    call w16f_call
    cmp al, dl
    jne .fail
.dir:
    mov si, w16f_name               ; the folder part, if more than a drive
    cmp byte [si + 1], ':'
    jne .dir1
    inc si
    inc si
.dir1:
    mov di, [w16f_num]
    cmp di, si
    je .here                        ; "A:" alone
    mov al, [di - 1]                ; the separator ends it, except the root's
    mov byte [di - 1], 0
    push ax
    push di
    cmp byte [si], 0
    jne .cd
    mov word [si], '\'              ; "\" itself: the root
.cd:
    mov dx, si
    mov ah, 0x3B
    call w16f_callds
    pop di
    pop ax
    mov [di - 1], al
    jc .fail
.here:
    ICALL w16_args                  ; the spec keeps its file part only
    les di, [es:bx + 6]
    mov si, [w16f_num]
    cld
.back:
    lodsb
    stosb
    or al, al
    jnz .back
    mov ax, [w16f_num]              ; ...and the search is made with it
    mov [w16f_spec], ax
    ICALL w16_args
    mov ax, [es:bx + 4]
    or ax, ax
    jz .static
    call w16d_item                  ; BX = the list box
    jc .static
    mov ax, LB_RESETCONTENT
    call w16f_lbsend
    call w16f_dtaon
    test word [w16f_ddl], DDL_EXCLUSIVE
    jnz .dirs
    mov cx, [w16f_ddl]              ; the files, by the spec
    and cx, DDL_FILEATTR
    mov dx, [w16f_spec]
    call w16f_each
.dirs:
    test word [w16f_ddl], DDL_DIRECTORY
    jz .drives
    mov word [w16f_item], '*.'      ; ...the folders, all of them
    mov word [w16f_item + 2], '*'
    mov cx, DDL_DIRECTORY
    mov dx, w16f_item
    call w16f_each
.drives:
    call w16f_dtaoff
    test word [w16f_ddl], DDL_DRIVES
    jz .static
    mov ah, 0x19
    call w16f_call
    mov dl, al                      ; 0Eh to where we are: AL = how many
    mov ah, 0x0E
    call w16f_call
    mov cl, al
    xor ch, ch
    mov dl, 'a'
.drv:
    mov word [w16f_item], '[-'
    mov [w16f_item + 2], dl
    mov word [w16f_item + 3], '-]'
    mov byte [w16f_item + 5], 0
    mov ax, LB_ADDSTRING
    call w16f_lbsend
    inc dl
    loop .drv
.static:
    ICALL w16_args
    mov ax, [es:bx + 2]
    or ax, ax
    jz .ok
    call w16d_item                  ; BX = the static control
    jc .ok
    mov ah, 0x19                    ; "a:\folder", from 19h and 47h
    call w16f_call
    mov dl, al
    add al, 'a'
    mov [w16f_item], al
    mov word [w16f_item + 1], ':\'
    inc dl
    mov si, w16f_item + 3
    mov ah, 0x47
    call w16f_call
    mov si, w16f_item
    call w16f_lower
    mov word [w16_sm_msg], WM_SETTEXT
    mov word [w16_sm_wp], 0
    call w16f_ctl
.ok:
    mov ax, 1
    jmp short .out
.fail:
    xor ax, ax
.out:
    pop di
    pop si
    ret

; w16f_each - every entry 4Eh/4Fh find for DS:DX with attributes CX into the
; list box BX; with DDL_DIRECTORY in CX only the folders, as "[name]", and
; never "." itself. The DTA is ours (w16f_dtaon)
w16f_each:
    push cx
    mov ah, 0x4E
    call w16f_callds
    pop cx
.next:
    jc .done
    mov si, w16f_dta + 30           ; the name
    mov di, w16f_item
    test cl, DDL_DIRECTORY
    jz .file
    test byte [w16f_dta + 21], DDL_DIRECTORY
    jz .skip
    cmp word [si], '.'              ; "." and nothing after it
    je .skip
    mov byte [di], '['
    inc di
.file:
    test cl, DDL_DIRECTORY
    jnz .cp
    test byte [w16f_dta + 21], DDL_DIRECTORY
    jnz .skip                       ; a folder in the files' pass
.cp:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .cp
    test cl, DDL_DIRECTORY
    jz .add
    mov word [di - 1], ']'
.add:
    mov si, w16f_item
    call w16f_lower
    mov ax, LB_ADDSTRING
    call w16f_lbsend
.skip:
    push cx
    mov ah, 0x4F
    call w16f_call
    pop cx
    jmp short .next
.done:
    ret

; w16f_dtaon / w16f_dtaoff - the DOS's DTA is ours for a search, and the
; program's again after it (2Fh, 1Ah)
w16f_dtaon:
    push bx
    push es
    push ds
    pop es
    mov ah, 0x2F
    ICALL w16_dcall                 ; ES:BX, ES banked by w16_dcall
    pop es
    mov [w16f_odta], bx
    mov ax, [w16_dces]
    mov [w16f_odta + 2], ax
    pop bx
    mov dx, w16f_dta
    mov ah, 0x1A
    jmp short w16f_callds
w16f_dtaoff:
    push es
    mov dx, [w16f_odta]
    mov es, [w16f_odta + 2]
    mov ah, 0x1A
    ICALL w16_dcall
    pop es
    ret

; w16f_call / w16f_callds - AH to the DOS with DS = ours as the call's DS.
; BX kept (it is the list box, to the callers)
w16f_call:
w16f_callds:
    push bx
    push es
    push ds
    pop es
    ICALL w16_dcall
    pop es
    pop bx
    ret

; w16f_lbsend - AX = a list-box message, lParam = DS:w16f_item, wParam 0,
; to the control BX: its own procedure, now. AX = its answer
w16f_lbsend:
    mov [w16_sm_msg], ax
    mov word [w16_sm_wp], 0
w16f_ctl:                           ; [w16_sm_msg]/[w16_sm_wp] set: lParam is
    mov word [w16_sm_lp], w16f_item ;   DS:w16f_item
    mov [w16_sm_lp + 2], ds
w16f_ctlx:                          ; all of [w16_sm_*] set but the HWND
    push cx
    push dx
    push si
    push di
    push bx
    ICALL w16_rec2hwnd
    pop bx
    mov [w16_sm_hwnd], ax
    ; STKBALANCE-OK: push cs and a near call are a far call, and
    ; w16d_ctlproc ends in retf (it is the dialog part's far procedure)
    push cs
    call w16d_ctlproc
    pop di
    pop si
    pop dx
    pop cx
    ret

; w16f_lower - DS:SI, NUL-ended, in lower case. Keeps all but flags
w16f_lower:
    push ax
    push si
.c:
    mov al, [si]
    or al, al
    jz .out
    cmp al, 'A'
    jb .n
    cmp al, 'Z'
    ja .n
    add byte [si], 'a' - 'A'
.n:
    inc si
    jmp short .c
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; DlgDirSelect (USER.99) - the list box's selection into lpString: a folder
; "[name]" as "name\", a drive "[-a-]" as "a:", both answering TRUE; a file
; as its name, with a '.' when it has none, answering FALSE. args: +0
; nIDListBox, +2 lpString, +6 hDlg
; -----------------------------------------------------------------------------
W16LIB w16u_dlgdirsel
    push si
    push di
    ICALL w16_args
    mov ax, [es:bx + 6]
    mov [w16d_hdlg], ax
    mov ax, [es:bx]
    call w16d_item
    jc .no
    mov ax, LB_GETCURSEL
    call w16f_lbsend
    cmp ax, -1
    je .no
    mov [w16_sm_wp], ax
    mov word [w16_sm_msg], LB_GETTEXT
    call w16f_ctl
    ICALL w16_args
    les di, [es:bx + 2]
    mov si, w16f_item
    cld
    xor dx, dx                      ; DX = the answer
    cmp byte [si], '['
    jne .file
    inc si
    inc dx
    cmp byte [si], '-'
    jne .dir
    mov al, [si + 1]                ; "[-a-]"
    stosb
    mov al, ':'
    stosb
    jmp short .end
.dir:
    lodsb
    cmp al, ']'
    je .dirend
    or al, al
    jz .dirend
    stosb
    jmp short .dir
.dirend:
    mov al, '\'
    stosb
    jmp short .end
.file:
    xor cx, cx                      ; CX = 1 once a '.' is seen
.f:
    lodsb
    or al, al
    jz .fend
    cmp al, '.'
    jne .fput
    inc cx
.fput:
    stosb
    jmp short .f
.fend:
    jcxz .dot
    jmp short .end
.dot:
    mov al, '.'
    stosb
.end:
    xor al, al
    stosb
    mov ax, dx
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    ret

section .text
