; =============================================================================
; os8088 - apps/win16/w16mci.inc
;
; MMSYSTEM'S MCI (SPEC.md 124.32.4): mciSendCommand and mciSendString for two
; device types, cdaudio over CDROM.DRV's package door (drivers/cdrom/
; cdpkg.inc, SPEC.md 119.4) and waveaudio over waveOut's device (w16mm.inc,
; 124.32.3), streaming its file. A string is read into the same parameter
; block a command carries, so the two are one path (w16mm_mexec). The kern_mega
; arm's alone, in the system-library part (124.24.1).
;
; The devices are four records in one fixed block of WIN16's heap, made at the
; first call and named by [w16mm_mci]; the block also holds the command being
; carried out and a string's words. ES names it through most of this file.
; =============================================================================

%include "drivers/cdrom/cdpkg.inc"        ; CDROM.DRV's door, one ABI (SPEC.md 20.11)

section .w16sys

MCI_OPEN        equ 0x0803
MCI_CLOSE       equ 0x0804
MCI_PLAY        equ 0x0806
MCI_SEEK        equ 0x0807
MCI_STOP        equ 0x0808
MCI_PAUSE       equ 0x0809
MCI_INFO        equ 0x080A
MCI_GETDEVCAPS  equ 0x080B
MCI_SET         equ 0x080D
MCI_SYSINFO     equ 0x0810
MCI_STATUS      equ 0x0814
MCI_RESUME      equ 0x0855
MM_MCINOTIFY    equ 0x03B9

MCF_NOTIFY      equ 0x0001          ; dwParam1's flags every command takes
MCF_WAIT        equ 0x0002
MCF_FROM        equ 0x0004
MCF_TO          equ 0x0008
MCF_TRACK       equ 0x0010
MCF_X1          equ 0x0100          ; ...and each command's own: OPEN_SHAREABLE,
MCF_X2          equ 0x0200          ;   SEEK_TO_START, STATUS_ITEM, INFO_PRODUCT,
MCF_X4          equ 0x0400          ;   GETDEVCAPS_ITEM, SYSINFO_QUANTITY,
MCF_X8          equ 0x0800          ;   SET_DOOR_OPEN; X2 its second, and on
MCF_X10         equ 0x1000
MCF_X20         equ 0x2000

MCN_OK          equ 1               ; MM_MCINOTIFY's wParam
MCN_SUPER       equ 2
MCN_ABORT       equ 4
MCN_FAIL        equ 8

MCT_MS          equ 0               ; time formats
MCT_MSF         equ 2
MCT_BYTES       equ 8
MCT_SAMPLES     equ 9
MCT_TMSF        equ 10

MCM_NOTREADY    equ 524             ; MCI_STATUS_MODE's answers
MCM_STOP        equ 525
MCM_PLAY        equ 526
MCM_PAUSE       equ 529
MCI_DT_CD       equ 516
MCI_DT_WAVE     equ 522

MCE_DEVID       equ 257             ; MCIERR_*
MCE_KEYWORD     equ 259
MCE_COMMAND     equ 261
MCE_HARDWARE    equ 262
MCE_DEVNAME     equ 263
MCE_NOMEM       equ 264
MCE_DEVOPEN     equ 265
MCE_OVERFLOW    equ 268
MCE_INTEGER     equ 270
MCE_MISSING     equ 273
MCE_UNSUP       equ 274
MCE_NOFILE      equ 275
MCE_NOTREADY    equ 276
MCE_ALL         equ 279
MCE_EXT         equ 281
MCE_RANGE       equ 282
MCE_ALIAS       equ 289
MCE_NODEV       equ 292
MCE_TFMT        equ 293
MCE_QUOTE       equ 294
MCE_BADFILE     equ 296
MCE_NULLPB      equ 297
MCE_NEEDFILE    equ 304
MCE_NOTINST     equ 306
MCE_WINUSE      equ 320
MCE_WUNSUIT     equ 326

; the block
MB_SIG      equ 0                   ; W16MM_MSIG
MB_POLL     equ 2                   ; ticks to the next CD poll
MB_RK       equ 3                   ; a string's answer: its kind (W16MM_RK_*)
MB_XDEV     equ 4                   ; the command: its device,
MB_XMSG     equ 6                   ;   its message,
MB_XFL      equ 8                   ;   dwParam1's low word,
MB_XPB      equ 10                  ;   dwParam2, the parameter block
MB_TOK      equ 14                  ; a string's next word, an offset here
MB_PB       equ 16                  ; a string's parameter block, 24 bytes
MB_OUT      equ 40                  ; ...what it answers into, 64
MB_CMD      equ 104                 ; ...its words, each NUL-ended
W16MM_CMDN  equ 128
MB_DEVS     equ MB_CMD + W16MM_CMDN
W16MM_MSIG  equ 0x434D
W16MM_NDEV  equ 4

; a device's record
MD_TYPE     equ 0                   ; 0 free, MDT_CD, MDT_WAVE
MD_TF       equ 1                   ; its time format
MD_FL       equ 2                   ; MDF_*
MD_ID       equ 3                   ; its device ID
MD_NWND     equ 4                   ; the window a play's notice goes to
MD_NAME     equ 6                   ; its alias or name, lower case, 24 bytes
MD_CT       equ 30                  ; cdaudio: where it stands stopped, the
MD_CTO      equ 31                  ;   track and the seconds into it; a play's
MD_CS       equ 32                  ;   end inside a track, likewise
MD_CSO      equ 34
MD_FH       equ 30                  ; waveaudio: its DOS handle,
MD_DOFF     equ 32                  ;   where its data lies in the file,
MD_DLEN     equ 36                  ;   how long it is,
MD_POS      equ 40                  ;   where it stands (a play's start while
MD_TO       equ 44                  ;   it plays) and its end, in frames,
MD_NEXT     equ 48                  ;   the next byte read, from the data's start
MD_FMT      equ 52                  ;   its fmt chunk, 16 bytes,
MD_BUF      equ 68                  ;   the buffers' block and selector,
MD_BSEL     equ 70
MD_BLEN     equ 72                  ;   each buffer's bytes,
MD_SH       equ 74                  ;   a frame's bytes as a shift,
MD_HDR      equ 76                  ;   the two WAVEHDRs
MD_ELEM     equ MD_HDR + 64         ;   and its file's name, 66 bytes
MD_SIZE     equ MD_ELEM + 66
MDT_CD      equ 1
MDT_WAVE    equ 2
MDF_PLAY    equ 1
MDF_PAUSE   equ 2
MDF_NOTE    equ 4                   ; a play's notice is owed
MDF_AT      equ 8                   ; cdaudio: the play stops at MD_CTO/MD_CSO
MDF_ELEM    equ 16                  ; waveaudio: it has a file
W16MM_NAMEN equ 24
W16MM_ELEMN equ 66
W16MM_CDPOLL equ 9                  ; ticks between CD polls, CDRack's

W16MM_RK_NONE   equ 0               ; what a string's command answers
W16MM_RK_INT    equ 1
W16MM_RK_POS    equ 2
W16MM_RK_BOOL   equ 3
W16MM_RK_MODE   equ 4
W16MM_RK_TF     equ 5
W16MM_RK_TYPE   equ 6               ; a track's: audio or other
W16MM_RK_DEV    equ 7               ; a device type's name
W16MM_RK_STR    equ 8               ; the text at MB_OUT
W16MM_RK_QTY    equ 9               ; the DWORD at MB_OUT
W16MM_RK_LEN    equ 10              ; a length: TMSF answered as MSF

; =============================================================================
; The calls
; =============================================================================

; mciSendCommand (MMSYSTEM.701) - args: +0 dwParam2, +4 dwParam1, +8 wMessage,
; +10 wDeviceID: 0, or an MCIERR_*
W16SYS w16mm_mcicmd
    push si
    push di
    push bp
    call w16mm_mblk                 ; ES = the block
    jc .out
    push es
    ICALL w16_args
    mov ax, [es:bx + 10]
    mov dx, [es:bx + 8]
    mov cx, [es:bx + 4]
    mov si, [es:bx]
    mov di, [es:bx + 2]
    pop es
    mov [es:MB_XDEV], ax
    mov [es:MB_XMSG], dx
    mov [es:MB_XFL], cx
    mov [es:MB_XPB], si
    mov [es:MB_XPB + 2], di
    call w16mm_mexec
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; mciSendString (MMSYSTEM.702) - args: +0 hwndCallback, +2 wReturnLength, +4
; lpstrReturnString, +8 lpstrCommand: 0, or an MCIERR_*
W16SYS w16mm_mcistr
    push si
    push di
    push bp
    call w16mm_mblk
    jc .out
    mov dx, es                      ; DX = the block
    ICALL w16_args
    push ds
    lds si, [es:bx + 8]
    mov es, dx
    call w16mm_mcpy                 ; its words at MB_CMD
    pop ds
    jc .out
    call w16mm_mparse               ; the command at MB_X*, its parameters
    jc .out                         ;   at MB_PB
    push es
    ICALL w16_args
    mov ax, [es:bx]
    pop es
    mov [es:MB_PB], ax              ; dwCallback
    call w16mm_mexec
    or ax, ax
    jnz .out
    push es
    ICALL w16_args
    mov cx, [es:bx + 2]
    les di, [es:bx + 4]
    mov dx, es
    pop es
    call w16mm_mans                 ; DX:DI = the program's room, CX its size
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; mciExecute (MMSYSTEM.712) - args: +0 lpstrCommand: mciSendString's, with no
; answer and no message box (124.32.4). TRUE when it was carried out
W16SYS w16mm_mciexec
    push si
    push di
    push bp
    call w16mm_mblk
    jc .no
    mov dx, es
    ICALL w16_args
    push ds
    lds si, [es:bx]
    mov es, dx
    call w16mm_mcpy
    pop ds
    jc .no
    call w16mm_mparse
    jc .no
    mov word [es:MB_PB], 0
    call w16mm_mexec
    or ax, ax
    jnz .no
    inc ax
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; mciGetDeviceID (MMSYSTEM.703) - args: +0 lpstrName: the ID of the device
; open by that alias or name, or 0
W16SYS w16mm_mciid
    push si
    push di
    push bp
    call w16mm_mblk
    jc .no
    mov dx, es
    ICALL w16_args
    push ds
    lds si, [es:bx]
    mov es, dx
    call w16mm_mcpy
    pop ds
    jc .no
    mov di, MB_CMD
    call w16mm_mfind                ; SI = its record
    jc .no
    mov al, [es:si + MD_ID]
    xor ah, ah
    jmp short .out
.no:
    xor ax, ax
.out:
    xor dx, dx
    pop bp
    pop di
    pop si
    ret

; mciSetYieldProc (MMSYSTEM.714): TRUE, and nothing kept. mciGetYieldProc
; (716): NULL
W16SYS w16mm_mciyset
    mov ax, 1
    xor dx, dx
    ret
W16SYS w16mm_mciyget
    xor ax, ax
    xor dx, dx
    ret

; mciGetErrorString (MMSYSTEM.706) - args: +0 wLength, +2 lpstrBuffer, +6
; dwError: Windows' sentence, cut to the buffer. TRUE for a code the arm
; answers; FALSE, and a general sentence, for any other
W16SYS w16mm_mcierr
    push si
    push di
    ICALL w16_args
    mov cx, [es:bx]
    mov ax, [es:bx + 6]
    mov dx, [es:bx + 8]
    les di, [es:bx + 2]
    jcxz .none
    mov si, w16mm_s_mcok            ; XSEC-OK: the part's strings, at cs:
    mov bx, 1                       ; BX = the answer
    or dx, dx
    jnz .gen
    or ax, ax
    jz .cp
    sub ax, 256                     ; MCIERR_BASE
    cmp ax, 255
    ja .gen
    mov dl, al                      ; DL = the code, less the base
    mov si, w16mm_merrs             ; XSEC-OK: the part's table, at cs:
.f:
    cs lodsb
    cmp al, 0xFF
    je .gen
    cmp al, dl
    je .cp
.sk:
    cs lodsb
    or al, al
    jnz .sk
    jmp short .f
.gen:
    xor bx, bx
    mov si, w16mm_s_mcix            ; XSEC-OK
.cp:
    dec cx                          ; room for the NUL
    jcxz .nul
.c:
    cs lodsb
    or al, al
    jz .nul
    stosb
    loop .c
.nul:
    mov byte [es:di], 0
    mov ax, bx
    jmp short .out
.none:
    xor ax, ax
.out:
    xor dx, dx
    pop di
    pop si
    ret

; =============================================================================
; The command (one path for both calls)
; =============================================================================

; w16mm_mexec - ES = the block, MB_X* the command: carried out, and its notice
; posted when it is done at once. AX = 0 or an MCIERR_*. Keeps ES; clobbers
; BX, CX, DX, SI, DI, BP
w16mm_mexec:
    mov ax, [es:MB_XMSG]
    cmp ax, MCI_OPEN
    jne .notopen
    call w16mm_mopen                ; AX = the error, BX = the new ID
    jmp short .now
.notopen:
    cmp ax, MCI_SYSINFO
    jne .dev
    call w16mm_msys
    mov bx, [es:MB_XDEV]
    jmp short .now
.dev:
    mov bx, [es:MB_XDEV]
    cmp bx, 0xFFFF
    jne .one
    mov ax, MCE_ALL
    cmp word [es:MB_XMSG], MCI_CLOSE
    jne .ret
    mov si, MB_DEVS                 ; close all
    mov cx, W16MM_NDEV
.all:
    cmp byte [es:si + MD_TYPE], 0
    je .alln
    push cx
    call w16mm_mclose
    pop cx
.alln:
    add si, MD_SIZE
    loop .all
    xor ax, ax
    jmp short .now
.one:
    call w16mm_mrec                 ; SI = its record
    mov ax, MCE_DEVID
    jc .ret
    mov ax, [es:MB_XMSG]
    mov di, w16mm_mtab              ; XSEC-OK: the part's table, at cs:
.t:
    mov dx, [cs:di]                 ; XSEC-OK
    cmp dx, 0xFFFF
    je .unsup
    cmp dx, ax
    je .go
    add di, 4
    jmp short .t
.unsup:
    mov ax, MCE_UNSUP
    jmp short .ret
.go:
    call [cs:di + 2]                ; XSEC-OK: AX = the error
    mov bx, [es:MB_XDEV]
    cmp word [es:MB_XMSG], MCI_PLAY
    je .ret                         ; a play's notice is its own (w16mm_mnote)
.now:
    or ax, ax
    jnz .ret
    test byte [es:MB_XFL], MCF_NOTIFY
    jz .ret
    push ax
    push bx
    mov cx, bx                      ; CX = the device's ID
    xor bx, bx
    call w16mm_mpget                ; AX = dwCallback's window
    mov bx, ax
    mov ax, MCN_OK
    jc .np
    call w16mm_mpost
.np:
    pop bx
    pop ax
.ret:
    ret

w16mm_mtab:
    dw MCI_CLOSE, w16mm_mclose
    dw MCI_PLAY, w16mm_mplay
    dw MCI_SEEK, w16mm_mseek
    dw MCI_STOP, w16mm_mstop
    dw MCI_PAUSE, w16mm_mpause
    dw MCI_RESUME, w16mm_mresume
    dw MCI_STATUS, w16mm_mstatus
    dw MCI_SET, w16mm_mset
    dw MCI_INFO, w16mm_minfo
    dw MCI_GETDEVCAPS, w16mm_mcaps
    dw 0xFFFF

; --- MCI_OPEN ----------------------------------------------------------------
; w16mm_mopen - ES = the block: a record taken and opened. AX = 0 and BX =
; its ID (written to wDeviceID), or AX = why not
w16mm_mopen:
    mov si, MB_DEVS                 ; a free record
    mov cx, W16MM_NDEV
.f:
    cmp byte [es:si + MD_TYPE], 0
    je .got
    add si, MD_SIZE
    loop .f
    mov ax, MCE_NOMEM
    ret
.got:
    mov di, si                      ; zeroed, its ID kept
    push di
    mov cx, MD_SIZE
    xor al, al
    cld
    rep stosb
    pop di
    mov ax, si
    sub ax, MB_DEVS - MD_SIZE
    xor dx, dx
    mov cx, MD_SIZE
    div cx
    mov [es:si + MD_ID], al
    ; --- its type
    mov bx, [es:MB_XFL]
    test bh, MCF_X10 >> 8           ; MCI_OPEN_TYPE_ID
    jz .tname
    push bx
    mov bx, 8
    call w16mm_mpget
    pop bx
    jc .nullpb
    mov cl, MDT_CD
    cmp ax, MCI_DT_CD
    je .type
    mov cl, MDT_WAVE
    cmp ax, MCI_DT_WAVE
    je .type
    jmp .badname
.tname:
    test bh, MCF_X20 >> 8           ; MCI_OPEN_TYPE
    jz .text
    push bx
    mov bx, 8
    call w16mm_mpget                ; DX:AX = the name
    pop bx
    jc .nullpb
    call w16mm_mtype                ; CL = the type
    jc .badname
    jmp short .type
.text:
    test bh, MCF_X2 >> 8            ; MCI_OPEN_ELEMENT: by its extension
    jz .noname
    push bx
    mov bx, 12
    call w16mm_mpget
    pop bx
    jc .nullpb
    call w16mm_mext                 ; CL = the type
    jc .badext
.type:
    mov [es:si + MD_TYPE], cl
    ; --- its name: the alias, else the element, else the type's
    test bh, MCF_X4 >> 8            ; MCI_OPEN_ALIAS
    jz .nalias
    push bx
    mov bx, 16
    call w16mm_mpget
    pop bx
    jc .nullpb
    jmp short .name
.nalias:
    test bh, MCF_X2 >> 8
    jz .tn
    push bx
    mov bx, 12
    call w16mm_mpget
    pop bx
    jmp short .name
.tn:
    mov ax, w16mm_s_cd              ; XSEC-OK: the part's names, at cs:
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .tn1
    mov ax, w16mm_s_wave            ; XSEC-OK
.tn1:
    mov dx, cs
.name:
    lea di, [si + MD_NAME]
    mov cx, W16MM_NAMEN
    call w16mm_mput                 ; copied, lower case
    push si
    mov di, si
    add di, MD_NAME
    call w16mm_mfind2               ; SI = a record by that name, but this one
    pop si
    jc .unique
    mov ax, MCE_ALIAS
    test bh, MCF_X4 >> 8
    jnz .refuse
    mov ax, MCE_DEVOPEN
    jmp short .refuse
.unique:
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    call w16mm_cdopen
    jmp short .opened
.wave:
    call w16mm_wvopen
.opened:
    or ax, ax
    jnz .refuse
    mov al, [es:si + MD_ID]
    xor ah, ah
    mov bx, ax                      ; BX = the ID, into wDeviceID
    push bx
    mov bx, 4
    xor dx, dx
    call w16mm_mpput
    pop bx
    xor ax, ax
    ret
.nullpb:
    mov ax, MCE_NULLPB
    jmp short .refuse
.noname:
    mov ax, MCE_NODEV
    jmp short .refuse
.badext:
    mov ax, MCE_EXT
    jmp short .refuse
.badname:
    mov ax, MCE_DEVNAME
.refuse:
    mov byte [es:si + MD_TYPE], 0
    ret

; w16mm_mtype - DX:AX = a type's name -> CL = MDT_*, CF=0; CF=1 neither.
; Keeps the rest
w16mm_mtype:
    push si
    push di
    push ds
    mov ds, dx
    mov si, ax
    mov di, w16mm_s_cd              ; XSEC-OK: the part's names, at cs:
    mov cl, MDT_CD
    call w16mm_mcmpi
    je .ok
    mov si, ax
    mov di, w16mm_s_wave            ; XSEC-OK
    mov cl, MDT_WAVE
    call w16mm_mcmpi
    je .ok
    stc
    jmp short .out
.ok:
    clc
.out:
    pop ds
    pop di
    pop si
    ret

; w16mm_mext - DX:AX = a file's name -> CL = MDT_WAVE, CF=0, when it ends in
; .WAV; CF=1 otherwise. Keeps the rest
w16mm_mext:
    push ax
    push bx
    push si
    push ds
    mov ds, dx
    mov si, ax
    mov bx, ax                      ; BX = its start
.end:
    lodsb
    or al, al
    jnz .end
    sub si, 5                       ; SI = its last four, ".wav"
    jc .no
    cmp si, bx
    jb .no
    mov ax, [si]
    or ah, 0x20
    cmp ax, '.w'
    jne .no
    mov ax, [si + 2]
    or ax, 0x2020
    cmp ax, 'av'
    jne .no
    mov cl, MDT_WAVE
    clc
    jmp short .out
.no:
    stc
.out:
    pop ds
    pop si
    pop bx
    pop ax
    ret

; w16mm_mcmpi - DS:SI = a name, CS:DI = one in lower case: ZF when they are
; the same, case aside. Clobbers SI, DI
w16mm_mcmpi:
    push ax
.c:
    lodsb
    cmp al, 'A'
    jb .l
    cmp al, 'Z'
    ja .l
    or al, 0x20
.l:
    cmp al, [cs:di]                 ; XSEC-OK: the part's names, at cs:
    jne .out
    inc di
    or al, al
    jnz .c
.out:
    pop ax
    ret

; w16mm_mput - DX:AX = a name, ES:DI = CX bytes of room: copied, lower case,
; NUL-ended, cut to the room. Keeps all
w16mm_mput:
    push ax
    push cx
    push si
    push di
    push ds
    mov ds, dx
    mov si, ax
    dec cx
.c:
    lodsb
    or al, al
    jz .nul
    cmp al, 'A'
    jb .s
    cmp al, 'Z'
    ja .s
    or al, 0x20
.s:
    stosb
    loop .c
.nul:
    mov byte [es:di], 0
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16mm_mclose - ES:SI = a device: its play stopped (a notice owed is
; aborted), its file closed and its buffers given back, the record free. AX = 0
w16mm_mclose:
    call w16mm_mstop
    cmp byte [es:si + MD_TYPE], MDT_WAVE
    jne .free
    test byte [es:si + MD_FL], MDF_ELEM
    jz .buf
    mov bx, [es:si + MD_FH]
    mov ah, 0x3E
    ICALL w16_dcall
.buf:
    mov ax, [es:si + MD_BUF]
    or ax, ax
    jz .free
    ICALL w16_gfreeh
.free:
    mov byte [es:si + MD_TYPE], 0
    xor ax, ax
    ret

; --- MCI_STOP, MCI_PAUSE, MCI_RESUME, MCI_SEEK ---------------------------------
; w16mm_mstop - ES:SI = a device: stopped where it stands, a notice owed
; aborted. AX = 0. Clobbers BX, CX, DX
w16mm_mstop:
    mov al, MCN_ABORT
    call w16mm_mnote
    test byte [es:si + MD_FL], MDF_PLAY
    jz .ok
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    call w16mm_cdhere               ; MD_CT/MD_CS = where it is
    mov bl, CDV_STOP
    call w16mm_cd
    jmp short .flags
.wave:
    call w16mm_wvhalt
.flags:
    and byte [es:si + MD_FL], ~(MDF_PLAY | MDF_PAUSE | MDF_AT) & 0xFF
.ok:
    xor ax, ax
    ret

; w16mm_mpause - ES:SI = a device: paused, a notice owed aborted. AX = 0
w16mm_mpause:
    mov al, MCN_ABORT
    call w16mm_mnote
    xor ax, ax
    test byte [es:si + MD_FL], MDF_PLAY
    jz .ret
    test byte [es:si + MD_FL], MDF_PAUSE
    jnz .ret
    or byte [es:si + MD_FL], MDF_PAUSE
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    mov bl, CDV_PAUSE
    call w16mm_cd
    jmp short .ok
.wave:
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .w1
    or byte [es:WO_FL], WOF_PAUSE
    call w16mm_wfeed
.w1:
    pop es
.ok:
    xor ax, ax
.ret:
    ret

; w16mm_mresume - ES:SI = a device: a pause ended. AX = 0
w16mm_mresume:
    xor ax, ax
    test byte [es:si + MD_FL], MDF_PAUSE
    jz .ret
    and byte [es:si + MD_FL], ~MDF_PAUSE & 0xFF
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    mov bl, CDV_RESUME
    call w16mm_cd
    jmp short .ok
.wave:
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .w1
    and byte [es:WO_FL], ~WOF_PAUSE & 0xFF
    call w16mm_wfeed
    ICALL w16_tarm
.w1:
    pop es
.ok:
    xor ax, ax
.ret:
    ret

; w16mm_mseek - ES:SI = a device: stopped, and where its next play starts
; moved: MCI_TO, MCI_SEEK_TO_START or _END. AX = 0 or why not
w16mm_mseek:
    call w16mm_mstop
    mov bx, [es:MB_XFL]
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    call w16mm_cddisc               ; AL = the first track, AH the last
    jc .cderr
    test bh, MCF_X1 >> 8            ; to the start
    jz .cend
    mov [es:si + MD_CT], al
    mov word [es:si + MD_CS], 0
    jmp short .ok
.cend:
    test bh, MCF_X2 >> 8            ; to the end: the last track's length
    jz .cto
    mov al, ah
    mov [es:si + MD_CT], al
    push si
    call w16mm_cdtrk                ; CX = its seconds
    pop si
    mov [es:si + MD_CS], cx
    jmp short .ok
.cto:
    test bl, MCF_TO
    jz .miss
    mov bx, 4
    call w16mm_mpget
    jc .null
    call w16mm_cdin                 ; BL = the track, CX the seconds
    jc .range
    mov [es:si + MD_CT], bl
    mov [es:si + MD_CS], cx
    jmp short .ok
.wave:
    test byte [es:si + MD_FL], MDF_ELEM
    jz .nofile
    xor ax, ax
    xor dx, dx
    test bh, MCF_X1 >> 8
    jnz .wput
    call w16mm_wvlen                ; DX:AX = its length, frames
    test bh, MCF_X2 >> 8
    jnz .wput
    test bl, MCF_TO
    jz .miss
    mov bx, 4
    call w16mm_mpget
    jc .null
    call w16mm_wvin                 ; DX:AX = frames
    call w16mm_wvcap                ; CF=1 past the end
    jc .range
.wput:
    mov [es:si + MD_POS], ax
    mov [es:si + MD_POS + 2], dx
.ok:
    xor ax, ax
    ret
.nofile:
    mov ax, MCE_NEEDFILE
    ret
.miss:
    mov ax, MCE_MISSING
    ret
.null:
    mov ax, MCE_NULLPB
    ret
.range:
    mov ax, MCE_RANGE
    ret
.cderr:
    call w16mm_cderr
    ret

; --- MCI_PLAY ------------------------------------------------------------------
; w16mm_mplay - ES:SI = a device: a play from MCI_FROM (or where it stands) to
; MCI_TO (or the end), its notice owed with MCI_NOTIFY, waited for with
; MCI_WAIT. A notice already owed is superseded (a new one asked) or aborted.
; AX = 0 or why not
w16mm_mplay:
    mov al, MCN_SUPER
    test byte [es:MB_XFL], MCF_NOTIFY
    jnz .n
    mov al, MCN_ABORT
.n:
    call w16mm_mnote
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    call w16mm_cdplay
    jmp short .played
.wave:
    call w16mm_wvplay
.played:
    or ax, ax
    jnz .ret
    test byte [es:MB_XFL], MCF_NOTIFY
    jz .arm
    or byte [es:si + MD_FL], MDF_NOTE
    xor bx, bx
    call w16mm_mpget                ; AX = the window
    mov [es:si + MD_NWND], ax
.arm:
    mov byte [w16mm_wq], 1          ; the tick sees it end (w16mm_mtick)
    ICALL w16_tarm
    test byte [es:MB_XFL], MCF_WAIT
    jz .ok
.wait:                              ; MCI_WAIT: here until it ends
    test byte [es:si + MD_FL], MDF_PLAY
    jz .ok
    cmp byte [es:si + MD_TYPE], 0
    je .ok
    mov ax, 1
    call OSAPI_TASK_SLEEP
    call w16mm_wtick
    jmp short .wait
.ok:
    xor ax, ax
.ret:
    ret

; --- MCI_STATUS -----------------------------------------------------------------
; w16mm_mstatus - ES:SI = a device: MCI_STATUS_ITEM's answer at dwReturn. AX
; = 0 or why not
w16mm_mstatus:
    test byte [es:MB_XFL + 1], MCF_X1 >> 8
    jz .miss
    mov bx, 8
    call w16mm_mpget                ; AX = the item
    jc .null
    mov di, ax                      ; DI = the item
    xor ax, ax
    xor dx, dx
    cmp di, 6                       ; MCI_STATUS_TIME_FORMAT
    jne .nt
    mov al, [es:si + MD_TF]
    jmp short .put
.nt:
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .wave
    call w16mm_cdstat               ; DX:AX, or AX = why not and CF
    jc .ret
    jmp short .put
.wave:
    call w16mm_wvstat
    jc .ret
.put:
    mov bx, 4
    call w16mm_mpput
    xor ax, ax
.ret:
    ret
.miss:
    mov ax, MCE_MISSING
    ret
.null:
    mov ax, MCE_NULLPB
    ret

; --- MCI_SET ----------------------------------------------------------------------
; w16mm_mset - ES:SI = a device: MCI_SET_TIME_FORMAT, MCI_SET_DOOR_OPEN and
; _CLOSED; MCI_SET_AUDIO and the rest accepted. AX = 0 or why not
w16mm_mset:
    mov bx, [es:MB_XFL]
    test bh, MCF_X4 >> 8            ; MCI_SET_TIME_FORMAT
    jz .door
    push bx
    mov bx, 4
    call w16mm_mpget                ; AX = the format
    pop bx
    jc .null
    mov cx, w16mm_cdtfs             ; XSEC-OK: the part's lists, at cs:
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .tf
    mov cx, w16mm_wvtfs             ; XSEC-OK
.tf:
    push si
    mov si, cx
.tl:
    mov cl, [cs:si]                 ; XSEC-OK
    inc si
    cmp cl, 0xFF
    je .tbad
    cmp al, cl
    jne .tl
    or ah, ah
    jnz .tbad
    pop si
    mov [es:si + MD_TF], al
    jmp short .door
.tbad:
    pop si
    mov ax, MCE_TFMT
    ret
.door:
    test bh, (MCF_X1 | MCF_X2) >> 8 ; the door
    jz .ok
    mov ax, MCE_UNSUP
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .ret
    xor al, al                      ; CDV_EJECT: 0 opens, 1 closes
    test bh, MCF_X2 >> 8
    jz .ej
    inc ax
.ej:
    mov bl, CDV_EJECT
    call w16mm_cd
    jnc .ok
    call w16mm_cderr
    ret
.ok:
    xor ax, ax
.ret:
    ret
.null:
    mov ax, MCE_NULLPB
    ret

w16mm_cdtfs:  db MCT_MS, MCT_MSF, MCT_TMSF, 0xFF
w16mm_wvtfs:  db MCT_MS, MCT_BYTES, MCT_SAMPLES, 0xFF

; --- MCI_INFO, MCI_GETDEVCAPS, MCI_SYSINFO -----------------------------------------
; w16mm_minfo - ES:SI = a device: MCI_INFO_PRODUCT, or a waveaudio's
; MCI_INFO_FILE, into lpstrReturn. AX = 0 or why not
w16mm_minfo:
    mov bx, [es:MB_XFL]
    test bh, MCF_X1 >> 8            ; the product
    jz .file
    mov ax, w16mm_s_cdp             ; XSEC-OK: the part's names, at cs:
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .cs
    mov ax, w16mm_s_wvp             ; XSEC-OK
.cs:
    mov dx, cs
    jmp short w16mm_mret
.file:
    mov ax, MCE_UNSUP
    test bh, MCF_X2 >> 8
    jz .ret
    cmp byte [es:si + MD_TYPE], MDT_WAVE
    jne .ret
    lea ax, [si + MD_ELEM]
    mov dx, es
    jmp short w16mm_mret
.ret:
    ret

; w16mm_mret - DX:AX = a NUL-ended answer: copied to lpstrReturn (+4) of
; dwRetSize (+8). AX = 0, or MCIERR_PARAM_OVERFLOW when it is cut
w16mm_mret:
    push si
    push di
    push ds
    push es
    push dx
    push ax
    mov bx, 8
    call w16mm_mpget
    mov cx, ax                      ; CX = its room
    mov bx, 4
    call w16mm_mpget                ; DX:AX = where
    mov di, ax
    mov es, dx
    pop si
    pop ds
    mov ax, MCE_NULLPB
    jc .out
    or dx, dx
    jz .out
    mov ax, MCE_OVERFLOW
    jcxz .out
    dec cx
.c:
    lodsb
    or al, al
    jz .end
    jcxz .cut
    stosb
    dec cx
    jmp short .c
.cut:
    mov byte [es:di], 0
    mov ax, MCE_OVERFLOW
    jmp short .out
.end:
    stosb
    xor ax, ax
.out:
    pop es
    pop ds
    pop di
    pop si
    ret

; w16mm_mcaps - ES:SI = a device: MCI_GETDEVCAPS_ITEM's answer at dwReturn.
; AX = 0 or why not
w16mm_mcaps:
    test byte [es:MB_XFL + 1], MCF_X1 >> 8
    jz .miss
    mov bx, 8
    call w16mm_mpget                ; AX = the item, 1 to 9
    jc .null
    dec ax
    cmp ax, 9
    jae .bad
    mov bx, w16mm_cdcaps            ; XSEC-OK: the part's tables, at cs:
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .t
    mov bx, w16mm_wvcaps            ; XSEC-OK
.t:
    add bx, ax
    mov al, [cs:bx]                 ; XSEC-OK
    xor ah, ah
    xor dx, dx
    cmp al, 2                       ; 2: the device type
    jne .put
    mov ax, MCI_DT_CD
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .put
    mov ax, MCI_DT_WAVE
.put:
    mov bx, 4
    call w16mm_mpput
    xor ax, ax
    ret
.bad:
    mov ax, MCE_RANGE
    ret
.miss:
    mov ax, MCE_MISSING
    ret
.null:
    mov ax, MCE_NULLPB
    ret

; can record, has audio, has video, device type, uses files, compound, can
; eject, can play, can save
w16mm_cdcaps: db 0, 1, 0, 2, 0, 0, 1, 1, 0
w16mm_wvcaps: db 0, 1, 0, 2, 1, 1, 0, 1, 0

; w16mm_msys - ES = the block, MCI_SYSINFO: MCI_SYSINFO_QUANTITY (a DWORD at
; lpstrReturn) or MCI_SYSINFO_NAME (dwNumber's name), of the types installed
; (cdaudio with CDROM.DRV, waveaudio always) or, with MCI_SYSINFO_OPEN, of the
; devices open; wDeviceType (+16) narrows either to one type. AX = 0 or why not
w16mm_msys:
    mov bx, 16
    call w16mm_mpget
    jc .null
    mov bp, ax                      ; BP = the type asked for, 0 any
    mov bx, 12
    call w16mm_mpget
    mov di, ax                      ; DI = dwNumber
    xor cx, cx                      ; CX = how many so far
    test byte [es:MB_XFL + 1], MCF_X2 >> 8 ; MCI_SYSINFO_OPEN
    jnz .open
    cmp bp, MCI_DT_WAVE
    je .wv
    push bx
    mov bl, CDV_IDENT
    call w16mm_cd
    pop bx
    jc .wv
    mov ax, w16mm_s_cd              ; XSEC-OK: the part's names, at cs:
    mov dx, cs
    call .one
    jz .name
.wv:
    cmp bp, MCI_DT_CD
    je .count
    mov ax, w16mm_s_wave            ; XSEC-OK
    mov dx, cs
    call .one
    jz .name
    jmp short .count
.open:
    mov si, MB_DEVS
    push cx
    mov cx, W16MM_NDEV
.o:
    cmp byte [es:si + MD_TYPE], 0
    je .on
    mov ax, MCI_DT_CD
    cmp byte [es:si + MD_TYPE], MDT_CD
    je .ot
    mov ax, MCI_DT_WAVE
.ot:
    or bp, bp
    jz .oy
    cmp ax, bp
    jne .on
.oy:
    pop ax
    inc ax
    push ax
    test byte [es:MB_XFL + 1], MCF_X4 >> 8
    jz .on
    cmp ax, di
    jne .on
    pop ax
    lea ax, [si + MD_NAME]
    mov dx, es
    jmp w16mm_mret
.on:
    add si, MD_SIZE
    loop .o
    pop cx
.count:
    test byte [es:MB_XFL + 1], MCF_X1 >> 8 ; MCI_SYSINFO_QUANTITY
    jz .range
    mov ax, cx
    xor dx, dx
    push ds
    push di
    push ax
    mov bx, 4
    call w16mm_mpget
    jc .qnull
    or dx, dx
    jz .qnull
    mov ds, dx
    mov di, ax
    pop ax
    mov [di], ax
    mov word [di + 2], 0
    pop di
    pop ds
    xor ax, ax
    ret
.qnull:
    pop ax
    pop di
    pop ds
.null:
    mov ax, MCE_NULLPB
    ret
.range:
    mov ax, MCE_RANGE
    ret
.name:
    jmp w16mm_mret
; .one - DX:AX = a name, counted: ZF and the name kept when it is the one
; MCI_SYSINFO_NAME asks for
.one:
    inc cx
    test byte [es:MB_XFL + 1], MCF_X4 >> 8 ; MCI_SYSINFO_NAME
    jz .onz
    cmp cx, di
    ret
.onz:
    or cx, cx                       ; (never zero here: NZ)
    ret

; =============================================================================
; The tick, and the notices
; =============================================================================

; w16mm_mtick - WIN16's tick (w16mm_wtick): each playing device seen to its
; end - a waveaudio's buffers read again and written, a cdaudio polled every
; W16MM_CDPOLL ticks. AL = 1 while one still plays, else 0. Keeps the rest
w16mm_mtick:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor bp, bp                      ; BP = whether one still plays
    cmp word [w16mm_mci], 0
    je .out
    W16SEG es, [w16mm_mci]
    dec byte [es:MB_POLL]
    jns .p
    mov byte [es:MB_POLL], W16MM_CDPOLL - 1
.p:
    mov si, MB_DEVS
    mov cx, W16MM_NDEV
.d:
    test byte [es:si + MD_FL], MDF_PLAY
    jz .n
    push cx
    cmp byte [es:si + MD_TYPE], MDT_CD
    jne .w
    cmp byte [es:MB_POLL], W16MM_CDPOLL - 1
    jne .cdy
    call w16mm_cdtick
    jmp short .y
.cdy:
    inc bp
    jmp short .y
.w:
    call w16mm_wvtick
.y:
    pop cx
    test byte [es:si + MD_FL], MDF_PLAY
    jz .n
    inc bp
.n:
    add si, MD_SIZE
    loop .d
.out:
    mov ax, bp
    or ax, ax
    jz .r
    mov al, 1
.r:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; w16mm_mnote - AL = MCI_NOTIFY_*, ES:SI = a device: the notice its play owes
; posted, and owed no longer. Keeps all
w16mm_mnote:
    test byte [es:si + MD_FL], MDF_NOTE
    jz .ret
    and byte [es:si + MD_FL], ~MDF_NOTE & 0xFF
    push ax
    push bx
    push cx
    xor ah, ah
    mov bx, [es:si + MD_NWND]
    mov cl, [es:si + MD_ID]
    xor ch, ch
    call w16mm_mpost
    pop cx
    pop bx
    pop ax
.ret:
    ret

; w16mm_mpost - AX = MM_MCINOTIFY's wParam, BX = the window, CX = the device:
; posted. Keeps all
w16mm_mpost:
    or bx, bx
    jz .ret
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov [w16_sm_wp], ax
    mov word [w16_sm_msg], MM_MCINOTIFY
    mov [w16_sm_hwnd], bx
    mov [w16_sm_lp], cx
    mov word [w16_sm_lp + 2], 0
    xor al, al                      ; at the back
    ICALL w16_post
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; =============================================================================
; The block and its records
; =============================================================================

; w16mm_mblk - ES = the block, made the first time: CF=0; CF=1 AX =
; MCIERR_OUT_OF_MEMORY. Keeps the rest
w16mm_mblk:
    cmp word [w16mm_mci], 0
    jne .have
    push bx
    push cx
    push dx
    mov ax, MB_DEVS + W16MM_NDEV * MD_SIZE
    xor dx, dx
    mov cl, GMEM_ZEROINIT           ; fixed: its handle is its place
    ICALL w16_gnew                  ; AX = the handle
    pop dx
    pop cx
    pop bx
    or ax, ax
    jz .no
    mov [w16mm_mci], ax
    W16SEG es, ax
    mov word [es:MB_SIG], W16MM_MSIG
    clc
    ret
.have:
    W16SEG es, [w16mm_mci]
    clc
    ret
.no:
    mov ax, MCE_NOMEM
    stc
    ret

; w16mm_mrec - BX = a device ID -> SI = its record, CF=0; CF=1 none open.
; Keeps the rest
w16mm_mrec:
    or bx, bx
    jz .no
    cmp bx, W16MM_NDEV
    ja .no
    push ax
    push dx
    mov ax, bx
    dec ax
    mov si, MD_SIZE
    mul si
    add ax, MB_DEVS
    mov si, ax
    pop dx
    pop ax
    cmp byte [es:si + MD_TYPE], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; w16mm_mfind - ES:DI = a name in lower case -> SI = the device open by it,
; CF=0; CF=1 none. w16mm_mfind2: and not the record at SI. Keeps the rest
w16mm_mfind:
    xor si, si
w16mm_mfind2:
    push ax
    push bx
    push cx
    mov bx, si                      ; BX = the record left out
    mov si, MB_DEVS
    mov cx, W16MM_NDEV
.d:
    cmp si, bx
    je .n
    cmp byte [es:si + MD_TYPE], 0
    je .n
    push si
    push di
    add si, MD_NAME
.c:
    mov al, [es:si]
    cmp al, [es:di]
    jne .ne
    inc si
    inc di
    or al, al
    jnz .c
    pop di
    pop si
    clc
    jmp short .out
.ne:
    pop di
    pop si
.n:
    add si, MD_SIZE
    loop .d
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; w16mm_mpget - BX = an offset in the parameter block -> DX:AX = the DWORD
; there, CF=0; CF=1 there is no block. Keeps the rest
w16mm_mpget:
    push di
    push ds
    lds di, [es:MB_XPB]
    mov ax, ds
    or ax, di
    jz .no
    mov ax, [di + bx]
    mov dx, [di + bx + 2]
    clc
    jmp short .out
.no:
    xor dx, dx
    stc
.out:
    pop ds
    pop di
    ret

; w16mm_mpput - BX = an offset in the parameter block, DX:AX = a DWORD:
; written there when there is a block. Keeps all
w16mm_mpput:
    push di
    push ds
    push cx
    lds di, [es:MB_XPB]
    mov cx, ds
    or cx, di
    jz .out
    mov [di + bx], ax
    mov [di + bx + 2], dx
.out:
    pop cx
    pop ds
    pop di
    ret

; =============================================================================
; cdaudio (CDROM.DRV's door, SPEC.md 119.4). A position is (BL = a track, CX
; = seconds into it); the door counts no finer than a second
; =============================================================================

; w16mm_cd - BL = a CDV_* verb, the rest as it takes them: OSAPI_DRV_CALL.
; Answers as the verb does in AX, CX and DX; CF=1 AX = a CDE_* (0: no
; CDROM.DRV). Keeps BX, SI, DI, BP, DS, ES. w16mm_cdx: SI answered too
w16mm_cd:
    push si
    call w16mm_cdx
    pop si
    ret
w16mm_cdx:
    push bx
    push es
    mov bh, DRVC_CD
    call OSAPI_DRV_CALL
    pop es
    pop bx
    ret

; w16mm_cderr - AX = a CDE_* -> AX = its MCIERR_*. Keeps the rest
w16mm_cderr:
    cmp ax, CDE_NODISC
    je .nr
    cmp ax, CDE_NOTREADY
    je .nr
    cmp ax, CDE_ARG
    je .rg
    mov ax, MCE_HARDWARE
    ret
.nr:
    mov ax, MCE_NOTREADY
    ret
.rg:
    mov ax, MCE_RANGE
    ret

; w16mm_cdopen - ES:SI = a new cdaudio device: CDROM.DRV asked whether it is
; there, the format MSF, where it stands the disc's first track. AX = 0, or
; MCIERR_DEVICE_NOT_INSTALLED
w16mm_cdopen:
    mov bl, CDV_IDENT
    call w16mm_cd
    jc .no
    cmp ax, CD_SIG
    jne .no
    mov byte [es:si + MD_TF], MCT_MSF
    mov byte [es:si + MD_CT], 1
    call w16mm_cddisc
    jc .ok
    mov [es:si + MD_CT], al
.ok:
    xor ax, ax
    ret
.no:
    mov ax, MCE_NOTINST
    ret

; w16mm_cddisc - CDV_DISC: AL = the first track, AH = the last, CX = the
; disc's seconds, CF=0; CF=1 AX = a CDE_*. Keeps the rest
w16mm_cddisc:
    push bx
    push dx
    push si
    mov bl, CDV_DISC
    call w16mm_cd
    pop si
    pop dx
    pop bx
    ret

; w16mm_cdtrk - AL = a track -> DX:AX = its start LBA, CX = its seconds, CF=0;
; CF=1 not on the disc. Keeps the rest
w16mm_cdtrk:
    push bx
    mov bl, CDV_TRACK
    call w16mm_cd
    pop bx
    ret

; w16mm_cdhere - ES:SI = a playing cdaudio: MD_CT/MD_CS = where the disc is.
; Keeps all
w16mm_cdhere:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, CDV_STATUS
    call w16mm_cd                   ; AH = the track, CX the seconds
    pop si
    jc .out
    or ah, ah
    jz .out
    mov [es:si + MD_CT], ah
    mov [es:si + MD_CS], cx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16mm_cdout - BL = a track, CX = seconds into it, ES:SI = a cdaudio -> DX:AX
; = that position in its time format. Keeps the rest
w16mm_cdout:
    push bx
    push cx
    push di
    cmp byte [es:si + MD_TF], MCT_TMSF
    jne .abs
    mov ax, cx
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    mov ah, al
    mov al, bl
    xor dh, dh
    jmp short .out
.abs:
    push cx
    push si
    mov al, bl
    call w16mm_cdtrk                ; DX:AX = its LBA
    pop si
    pop cx
    jnc .lba
    xor ax, ax
    xor dx, dx
.lba:
    add ax, 150                     ; MSF 00:02:00 is LBA 0
    adc dx, 0
    mov di, ax
    mov bx, dx
    mov ax, 75
    mul cx
    add ax, di
    adc dx, bx                      ; DX:AX = frames
    cmp byte [es:si + MD_TF], MCT_MS
    je .ms
    mov cx, 4500
    div cx                          ; AX = minutes, DX = frames left
    mov bl, al
    mov ax, dx
    mov cl, 75
    div cl                          ; AL = seconds, AH = frames
    mov dl, ah
    xor dh, dh
    mov ah, al
    mov al, bl
    jmp short .out
.ms:
    mov cx, 40                      ; 1000 / 75 = 40 / 3
    call w16mm_mmul
    mov cx, 3
    call w16mm_mdiv
.out:
    pop di
    pop cx
    pop bx
    ret

; w16mm_cdin - DX:AX = a position in a cdaudio's time format, ES:SI = it ->
; BL = the track, CX = seconds into it, CF=0; CF=1 not on the disc. Keeps
; the rest
w16mm_cdin:
    push ax
    push dx
    push di
    push bp
    cmp byte [es:si + MD_TF], MCT_TMSF
    jne .abs
    mov bl, al
    mov al, ah
    mov ah, 60
    mul ah
    xor dh, dh
    add ax, dx
    mov cx, ax
    push si
    mov al, bl
    push cx
    call w16mm_cdtrk                ; on the disc?
    pop cx
    pop si
    jmp short .out
.abs:
    cmp byte [es:si + MD_TF], MCT_MS
    je .ms
    mov bl, dl                      ; MSF: (m * 60 + s) * 75 + f
    mov cl, ah
    mov ah, 60
    mul ah
    xor ch, ch
    add ax, cx
    mov cx, 75
    mul cx
    add al, bl
    adc ah, 0
    adc dx, 0
    jmp short .lba
.ms:
    mov cx, 3
    call w16mm_mmul
    mov cx, 40
    call w16mm_mdiv
.lba:
    sub ax, 150
    sbb dx, 0
    jnc .pos
    xor ax, ax
    xor dx, dx
.pos:
    mov di, ax
    mov bp, dx                      ; BP:DI = the LBA
    call w16mm_cddisc               ; AL = the first, AH = the last
    jc .out
    mov bl, ah
    mov bh, al
.t:
    push si
    mov al, bl
    call w16mm_cdtrk                ; DX:AX = its start
    pop si
    jc .prev
    cmp dx, bp
    jb .hit
    ja .prev
    cmp ax, di
    jbe .hit
.prev:
    cmp bl, bh
    je .first
    dec bl
    jmp short .t
.first:
    xor ax, ax                      ; before the first: its start
    xor dx, dx
    mov di, ax
    mov bp, dx
.hit:
    sub di, ax
    sbb bp, dx                      ; BP:DI = frames into it
    mov ax, di
    mov dx, bp
    mov cx, 75
    push bx
    call w16mm_mdiv
    pop bx
    mov cx, ax                      ; (a track is under 65,536 seconds)
    clc
.out:
    pop bp
    pop di
    pop dx
    pop ax
    ret

; w16mm_cdplay - ES:SI = a cdaudio: CDV_PLAY from MCI_FROM (or where it
; stands, or CDV_RESUME when it is paused and no MCI_TO is asked) to MCI_TO,
; whose second inside a track the tick stops at. AX = 0 or why not
w16mm_cdplay:
    mov bp, [es:MB_XFL]
    test bp, MCF_FROM
    jnz .from
    test byte [es:si + MD_FL], MDF_PAUSE
    jz .here
    test bp, MCF_TO
    jnz .here
    and byte [es:si + MD_FL], ~MDF_PAUSE & 0xFF
    mov bl, CDV_RESUME
    call w16mm_cd
    jc .err
    xor ax, ax
    ret
.here:
    test byte [es:si + MD_FL], MDF_PLAY
    jz .stood
    call w16mm_cdhere
.stood:
    mov bl, [es:si + MD_CT]
    mov cx, [es:si + MD_CS]
    jmp short .to
.from:
    mov bx, 4
    call w16mm_mpget
    jc .null
    call w16mm_cdin                 ; BL = the track, CX the seconds
    jc .range
.to:
    push bx
    push cx
    and byte [es:si + MD_FL], ~(MDF_AT | MDF_PAUSE) & 0xFF
    xor di, di                      ; DI = the last track, 0 the disc's
    test bp, MCF_TO
    jz .go
    mov bx, 8
    call w16mm_mpget
    jc .null2
    call w16mm_cdin
    jc .range2
    mov di, bx
    and di, 0x00FF
    jcxz .atstart                   ; at a track's start: the one before
    mov [es:si + MD_CTO], bl
    mov [es:si + MD_CSO], cx
    or byte [es:si + MD_FL], MDF_AT
    jmp short .go
.atstart:
    dec di
    jz .range2
.go:
    pop cx
    pop bx
    mov ax, di
    mov ah, al
    mov al, bl
    cmp ah, 0
    je .play
    cmp ah, al
    jb .range
.play:
    push bx
    mov bl, CDV_PLAY
    call w16mm_cd
    pop bx
    jc .err
    mov [es:si + MD_CT], bl
    or byte [es:si + MD_FL], MDF_PLAY
    xor ax, ax
    ret
.null2:
    pop cx
    pop bx
.null:
    mov ax, MCE_NULLPB
    ret
.range2:
    pop cx
    pop bx
.range:
    mov ax, MCE_RANGE
    ret
.err:
    call w16mm_cderr
    ret

; w16mm_cdtick - ES:SI = a playing cdaudio, its poll due: a play that has
; ended, or reached its MCI_TO, is over and its notice posted. Keeps CX, SI, ES
w16mm_cdtick:
    push cx
    push si
    mov bl, CDV_STATUS
    call w16mm_cd                   ; AL = the state, AH the track, CX secs
    pop si
    jc .end
    cmp al, CDS_PAUSED
    je .out
    cmp al, CDS_PLAYING
    jne .over
    test byte [es:si + MD_FL], MDF_AT
    jz .out
    cmp ah, [es:si + MD_CTO]
    jb .out
    ja .stop
    cmp cx, [es:si + MD_CSO]
    jb .out
.stop:
    push si
    mov bl, CDV_STOP
    call w16mm_cd
    pop si
    mov ah, [es:si + MD_CTO]
    mov cx, [es:si + MD_CSO]
.over:
    or ah, ah
    jz .end
    mov [es:si + MD_CT], ah
    mov [es:si + MD_CS], cx
.end:
    and byte [es:si + MD_FL], ~(MDF_PLAY | MDF_PAUSE | MDF_AT) & 0xFF
    mov al, MCN_OK
    call w16mm_mnote
.out:
    pop cx
    ret

; w16mm_cdstat - ES:SI = a cdaudio, DI = an MCI_STATUS item -> DX:AX, CF=0;
; CF=1 AX = why not
w16mm_cdstat:
    cmp di, 4                       ; MCI_STATUS_MODE
    jne .nm
    push si
    mov bl, CDV_STATUS
    call w16mm_cd
    pop si
    jnc .ms
    mov dx, MCM_STOP                ; a drive with no audio commands (QEMU's)
    cmp ax, CDE_NOAUDIO             ;   plays nothing: stopped; any other
    je .mode                        ;   refusal is not ready
    mov dx, MCM_NOTREADY
    jmp short .mode
.ms:
    mov dx, MCM_PLAY
    cmp al, CDS_PLAYING
    je .mode
    mov dx, MCM_PAUSE
    cmp al, CDS_PAUSED
    je .mode
    mov dx, MCM_STOP
.mode:
    mov ax, dx
    jmp .w0
.nm:
    cmp di, 5                       ; MEDIA_PRESENT
    je .disc
    cmp di, 7                       ; READY
    jne .nd
.disc:
    call w16mm_cddisc
    mov ax, 0
    jc .w0
    inc ax
    jmp .w0
.nd:
    call w16mm_cddisc               ; the rest need the disc
    jc .err
    cmp di, 3                       ; NUMBER_OF_TRACKS
    jne .nn
    sub ah, al
    mov al, ah
    xor ah, ah
    inc ax
    jmp .w0
.nn:
    mov bx, ax                      ; BL = the first, BH = the last
    test byte [es:MB_XFL], MCF_TRACK
    jz .nt
    push bx
    mov bx, 12
    call w16mm_mpget
    pop bx
    jc .null
    or ah, ah
    jnz .range
    cmp al, bl
    jb .range
    cmp al, bh
    ja .range
    mov bl, al                      ; BL = the track asked for
    cmp di, 1                       ; LENGTH, of a track
    jne .tp
    call w16mm_cdtrk                ; CX = its seconds
    jc .err
    jmp short .len
.tp:
    cmp di, 2                       ; POSITION: where it starts
    jne .ty
    xor cx, cx
    jmp short .out
.ty:
    cmp di, 0x4001                  ; MCI_CDA_STATUS_TYPE_TRACK
    jne .range
    push si
    mov al, bl
    mov bl, CDV_TRACK
    call w16mm_cdx                  ; SI = its control nibble
    jc .tyerr
    test si, CDT_DATA
    pop si
    mov ax, 1088                    ; MCI_CDA_TRACK_AUDIO
    jz .w0
    inc ax                          ; ...OTHER
    jmp short .w0
.tyerr:
    pop si
    jmp short .err
.nt:
    cmp di, 1                       ; LENGTH, of the disc
    jne .np
.len:
    mov ax, cx
    xor dx, dx
    cmp byte [es:si + MD_TF], MCT_MS
    jne .lmsf
    mov cx, 1000
    call w16mm_mmul
    clc
    ret
.lmsf:
    mov cx, 60                      ; MSF, in TMSF too, as Windows answers
    div cx
    mov ah, dl
    xor dx, dx
    clc
    ret
.np:
    cmp di, 2                       ; POSITION, or with MCI_STATUS_START the
    jne .ct                         ;   disc's start
    test byte [es:MB_XFL + 1], MCF_X2 >> 8
    jz .pos
    xor cx, cx
    jmp short .out
.pos:
    test byte [es:si + MD_FL], MDF_PLAY
    jz .stood
    call w16mm_cdhere
.stood:
    mov bl, [es:si + MD_CT]
    mov cx, [es:si + MD_CS]
.out:
    call w16mm_cdout
    clc
    ret
.ct:
    cmp di, 8                       ; CURRENT_TRACK
    jne .range
    test byte [es:si + MD_FL], MDF_PLAY
    jz .cs
    call w16mm_cdhere
.cs:
    mov al, [es:si + MD_CT]
    xor ah, ah
.w0:
    xor dx, dx
    clc
    ret
.err:
    call w16mm_cderr
    stc
    ret
.null:
    mov ax, MCE_NULLPB
    stc
    ret
.range:
    mov ax, MCE_RANGE
    stc
    ret

; =============================================================================
; waveaudio (waveOut's device, 124.32.3). A position is in frames
; =============================================================================

; w16mm_wvopen - ES:SI = a new waveaudio: its element, when MCI_OPEN_ELEMENT
; names one, opened and its RIFF read for the fmt chunk and where the data
; lies. AX = 0 or why not
w16mm_wvopen:
    xor ax, ax
    test byte [es:MB_XFL + 1], MCF_X2 >> 8
    jz .ret                         ; no element: opened, nothing to play
    mov bx, 12
    call w16mm_mpget                ; DX:AX = its name
    lea di, [si + MD_ELEM]
    mov cx, W16MM_ELEMN
    call w16mm_mput
    push es
    mov es, dx
    mov dx, ax
    mov ax, 0x3D00
    ICALL w16_dcall                 ; AX = the handle
    pop es
    jnc .opened
    mov ax, MCE_NOFILE
.ret:
    ret
.opened:
    mov [es:si + MD_FH], ax
    or byte [es:si + MD_FL], MDF_ELEM
    mov cx, 12                      ; "RIFF" size "WAVE"
    call w16mm_wvrd
    jc .bad
    cmp word [es:MB_OUT], W16MM_RIFF
    jne .bad
    cmp word [es:MB_OUT + 8], 'WA'
    jne .bad
    xor bp, bp                      ; BP: 1 the fmt chunk read, 2 the data's
.ck:
    mov cx, 8                       ; a chunk's id and size
    call w16mm_wvrd
    jc .bad
    mov ax, [es:MB_OUT + 4]
    mov dx, [es:MB_OUT + 6]
    add ax, 1                       ; its pad byte
    adc dx, 0
    and al, 0xFE
    cmp word [es:MB_OUT], 'fm'
    jne .data
    cmp ax, 16
    jb .bad
    push ax
    push dx
    mov cx, 16
    call w16mm_wvrd
    pop dx
    pop ax
    jc .bad
    push si
    push di
    lea di, [si + MD_FMT]
    mov si, MB_OUT
    mov cx, 8
    push ds
    push es
    pop ds
    cld
    rep movsw
    pop ds
    pop di
    pop si
    sub ax, 16
    sbb dx, 0
    or bp, 1
    jmp short .skip
.data:
    cmp word [es:MB_OUT], 'da'
    jne .skip
    push ax
    push dx
    mov ax, 0x4201                  ; where it is: its data's start
    mov bx, [es:si + MD_FH]
    xor cx, cx
    xor dx, dx
    ICALL w16_dcall
    mov [es:si + MD_DOFF], ax
    mov [es:si + MD_DOFF + 2], dx
    mov ax, [es:MB_OUT + 4]
    mov [es:si + MD_DLEN], ax
    mov ax, [es:MB_OUT + 6]
    mov [es:si + MD_DLEN + 2], ax
    pop dx
    pop ax
    or bp, 2
.skip:
    cmp bp, 3
    je .found
    mov cx, dx
    mov dx, ax
    mov ax, 0x4201
    mov bx, [es:si + MD_FH]
    ICALL w16_dcall
    jc .bad
    jmp .ck
.found:
    cmp word [es:si + MD_FMT], 1    ; WAVE_FORMAT_PCM
    jne .bad
    mov ax, [es:si + MD_FMT + 12]   ; nBlockAlign: 1, 2 or 4
    xor cl, cl
    cmp ax, 1
    je .sh
    inc cx
    cmp ax, 2
    je .sh
    inc cx
    cmp ax, 4
    jne .bad
.sh:
    mov [es:si + MD_SH], cl
    cmp word [es:si + MD_FMT + 6], 0 ; a rate under 65,536
    jne .bad
    cmp word [es:si + MD_FMT + 4], 0
    je .bad
    xor ax, ax
    ret
.bad:
    mov bx, [es:si + MD_FH]
    mov ah, 0x3E
    ICALL w16_dcall
    and byte [es:si + MD_FL], ~MDF_ELEM & 0xFF
    mov ax, MCE_BADFILE
    ret

; w16mm_wvrd - ES:SI = a waveaudio, CX = bytes: read from its file into
; MB_OUT. CF=1 fewer came. Keeps all but AX
w16mm_wvrd:
    push bx
    push dx
    push cx
    mov bx, [es:si + MD_FH]
    mov dx, MB_OUT
    mov ah, 0x3F
    ICALL w16_dcall
    pop cx
    jc .out
    cmp ax, cx
    jb .out                         ; (CF set)
    clc
.out:
    pop dx
    pop bx
    ret

; w16mm_wvlen - ES:SI = a waveaudio -> DX:AX = its length, frames. Keeps
; the rest
w16mm_wvlen:
    mov ax, [es:si + MD_DLEN]
    mov dx, [es:si + MD_DLEN + 2]
    push cx
    mov cl, [es:si + MD_SH]
    call w16mm_wfr
    pop cx
    ret

; w16mm_wvcap - DX:AX = frames, ES:SI = a waveaudio: CF=1 past its end.
; Keeps all
w16mm_wvcap:
    push ax
    push dx
    push bx
    push cx
    mov bx, ax
    mov cx, dx
    call w16mm_wvlen
    cmp dx, cx
    jne .c
    cmp ax, bx
.c:
    pop cx
    pop bx
    pop dx
    pop ax
    ret

; w16mm_wvout - DX:AX = frames, ES:SI = a waveaudio -> DX:AX in its time
; format. Keeps the rest
w16mm_wvout:
    cmp byte [es:si + MD_TF], MCT_SAMPLES
    je .ret
    cmp byte [es:si + MD_TF], MCT_BYTES
    jne .ms
    push cx
    mov cl, [es:si + MD_SH]
    xor ch, ch
    jcxz .b
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.b:
    pop cx
    ret
.ms:
    push bx
    mov bx, [es:si + MD_FMT + 4]
    call w16mm_ms
    pop bx
.ret:
    ret

; w16mm_wvin - DX:AX in a waveaudio's time format, ES:SI = it -> DX:AX =
; frames. Keeps the rest
w16mm_wvin:
    cmp byte [es:si + MD_TF], MCT_SAMPLES
    je .ret
    push bx
    push cx
    cmp byte [es:si + MD_TF], MCT_BYTES
    jne .ms
    mov cl, [es:si + MD_SH]
    call w16mm_wfr
    jmp short .out
.ms:
    mov cx, 1000                    ; the seconds' frames, and the rest's
    call w16mm_mdiv                 ; DX:AX = seconds, BX = ms left
    mov cx, [es:si + MD_FMT + 4]
    call w16mm_mmul
    push dx
    push ax
    mov ax, bx
    mul cx
    mov cx, 1000
    div cx
    pop cx
    add ax, cx
    pop dx
    adc dx, 0
.out:
    pop cx
    pop bx
.ret:
    ret

; w16mm_wvhere - ES:SI = a waveaudio -> DX:AX = where it stands, frames: a
; play's start and what the card has played of it. Keeps the rest
w16mm_wvhere:
    mov ax, [es:si + MD_POS]
    mov dx, [es:si + MD_POS + 2]
    test byte [es:si + MD_FL], MDF_PLAY
    jz .ret
    push bx
    push cx
    push es
    mov cl, [es:si + MD_SH]
    mov bx, ax
    push dx
    mov ax, [w16mm_wo]
    call w16mm_wst
    pop dx
    jc .gone
    push dx
    mov ax, [es:WO_PLAY]
    mov dx, [es:WO_PLAY + 2]
    call w16mm_wfr                  ; DX:AX = frames played
    pop cx
    add ax, bx
    adc dx, cx
    pop es
    cmp dx, [es:si + MD_TO + 2]     ; no further than the play's end
    jb .in
    ja .end
    cmp ax, [es:si + MD_TO]
    jbe .in
.end:
    mov ax, [es:si + MD_TO]
    mov dx, [es:si + MD_TO + 2]
.in:
    pop cx
    pop bx
.ret:
    ret
.gone:
    mov ax, bx
    pop es
    pop cx
    pop bx
    ret

; w16mm_wvhalt - ES:SI = a playing waveaudio: where it stands kept, the
; device closed. Keeps all
w16mm_wvhalt:
    push ax
    push dx
    call w16mm_wvhere
    mov [es:si + MD_POS], ax
    mov [es:si + MD_POS + 2], dx
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    jc .none
    test byte [es:WO_FL], WOF_MCI
    jz .none
    call w16mm_wkill
.none:
    pop es
    pop dx
    pop ax
    ret

; w16mm_wvplay - ES:SI = a waveaudio: waveOut's device made the MCI's, its
; buffers taken, and the first two read and written, from MCI_FROM (or where
; it stands) to MCI_TO (or its end). AX = 0 or why not
w16mm_wvplay:
    mov ax, MCE_NEEDFILE
    test byte [es:si + MD_FL], MDF_ELEM
    jz .ret
    test byte [es:si + MD_FL], MDF_PLAY
    jz .stood
    call w16mm_wvhalt               ; from where it is, by default
    and byte [es:si + MD_FL], ~(MDF_PLAY | MDF_PAUSE) & 0xFF
.stood:
    mov bp, [es:MB_XFL]
    mov ax, [es:si + MD_POS]
    mov dx, [es:si + MD_POS + 2]
    test bp, MCF_FROM
    jz .f
    mov bx, 4
    call w16mm_mpget
    jc .null
    call w16mm_wvin
    call w16mm_wvcap
    jc .range
.f:
    push dx
    push ax
    call w16mm_wvlen                ; to its end, by default
    test bp, MCF_TO
    jz .t
    mov bx, 8
    call w16mm_mpget
    jc .null2
    call w16mm_wvin
    call w16mm_wvcap
    jc .range2
.t:
    pop cx
    pop bx                          ; BX:CX = the start, DX:AX = the end
    cmp dx, bx
    jne .ft
    cmp ax, cx
.ft:
    jb .range
    mov [es:si + MD_POS], cx
    mov [es:si + MD_POS + 2], bx
    mov [es:si + MD_TO], ax
    mov [es:si + MD_TO + 2], dx
    mov ax, MCE_WINUSE
    cmp word [w16mm_wo], 0
    jne .ret
    cmp word [w16mm_wi], 0          ; half-duplex (124.32.5)
    jne .ret
    push si
    push es
    call OSAPI_SND_CAPS             ; AX = the caps word
    pop es
    pop si
    push si
    push di
    mov di, si
    add di, MD_FMT
    mov si, ax
    call w16mm_wfmt                 ; DX = the rate, AL the format, AH shift
    pop di
    pop si
    mov cx, MCE_WUNSUIT
    jc .err
    call w16mm_wvbuf                ; the buffers, once
    jc .err
    push si
    push di
    push es
    xor si, si                      ; no callback
    xor di, di
    xor cx, cx
    call w16mm_wmake                ; ES = the device's state
    jc .nomk
    or byte [es:WO_FL], WOF_MCI
    pop es
    pop di
    pop si
    mov ax, [es:si + MD_POS]        ; the file at the start
    mov dx, [es:si + MD_POS + 2]
    push cx
    mov cl, [es:si + MD_SH]
    xor ch, ch
    jcxz .b
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.b:
    pop cx
    mov [es:si + MD_NEXT], ax
    mov [es:si + MD_NEXT + 2], dx
    add ax, [es:si + MD_DOFF]
    adc dx, [es:si + MD_DOFF + 2]
    mov cx, dx
    mov dx, ax
    mov ax, 0x4200
    mov bx, [es:si + MD_FH]
    ICALL w16_dcall
    or byte [es:si + MD_FL], MDF_PLAY
    and byte [es:si + MD_HDR + WH_FLAGS], ~WHDR_INQUEUE & 0xFF
    and byte [es:si + MD_HDR + 32 + WH_FLAGS], ~WHDR_INQUEUE & 0xFF
    call w16mm_wvtick               ; the first two read and written
    xor ax, ax
    ret
.nomk:
    pop es
    pop di
    pop si
    mov cx, MCE_NOMEM
.err:
    mov ax, cx
.ret:
    ret
.null2:
    pop ax
    pop dx
.null:
    mov ax, MCE_NULLPB
    ret
.range2:
    pop ax
    pop dx
.range:
    mov ax, MCE_RANGE
    ret

; w16mm_wvbuf - ES:SI = a waveaudio: its two buffers, half a second each of
; its format, 4 to 16 KB, taken once. CF=1 CX = MCIERR_OUT_OF_MEMORY. Keeps
; the rest
w16mm_wvbuf:
    cmp word [es:si + MD_BUF], 0
    jne .ok
    push ax
    push dx
    mov ax, [es:si + MD_FMT + 8]    ; nAvgBytesPerSec / 2
    mov dx, [es:si + MD_FMT + 10]
    shr dx, 1
    rcr ax, 1
    or dx, dx
    jnz .max
    cmp ax, 16384
    jbe .min
.max:
    mov ax, 16384
.min:
    cmp ax, 4096
    jae .al
    mov ax, 4096
.al:
    and al, 0xF0
    mov [es:si + MD_BLEN], ax
    shl ax, 1
    xor dx, dx
    push cx
    xor cl, cl                      ; GMEM_FIXED
    ICALL w16_gnew                  ; AX = the handle, DX = the block
    pop cx
    or ax, ax
    jz .no
    mov [es:si + MD_BUF], ax
    mov ax, dx
    ICALL w16_pblk                  ; AX = its selector
    mov [es:si + MD_BSEL], ax
    pop dx
    pop ax
.ok:
    clc
    ret
.no:
    pop dx
    pop ax
    mov cx, MCE_NOMEM
    stc
    ret

; w16mm_wvtick - ES:SI = a playing waveaudio: each buffer the card has
; finished read again and written while the play has bytes left; once none
; is queued and none is left, the play is over and its notice posted. Keeps
; SI, ES
w16mm_wvtick:
    push es
    mov ax, [w16mm_wo]
    call w16mm_wst
    pop es
    jc .over                        ; the device is gone: so is the play
    test byte [es:si + MD_FL], MDF_PAUSE
    jnz .ret
    xor bx, bx
    call w16mm_wvfill
    mov bx, 32
    call w16mm_wvfill
    test byte [es:si + MD_HDR + WH_FLAGS], WHDR_INQUEUE
    jnz .ret
    test byte [es:si + MD_HDR + 32 + WH_FLAGS], WHDR_INQUEUE
    jnz .ret
.over:
    call w16mm_wvhalt
    mov ax, [es:si + MD_TO]
    mov [es:si + MD_POS], ax
    mov ax, [es:si + MD_TO + 2]
    mov [es:si + MD_POS + 2], ax
    and byte [es:si + MD_FL], ~(MDF_PLAY | MDF_PAUSE) & 0xFF
    mov al, MCN_OK
    call w16mm_mnote
.ret:
    ret

; w16mm_wvfill - ES:SI = a playing waveaudio, BX = 0 or 32, its WAVEHDR: when
; it is not queued and the play has bytes left, its buffer read from the file
; and written. Keeps SI, BX, ES
w16mm_wvfill:
    test byte [es:si + MD_HDR + bx + WH_FLAGS], WHDR_INQUEUE
    jnz .ret
    mov ax, [es:si + MD_TO]         ; the bytes left: the end's less the next
    mov dx, [es:si + MD_TO + 2]
    mov cl, [es:si + MD_SH]
    xor ch, ch
    jcxz .b
.sh:
    shl ax, 1
    rcl dx, 1
    loop .sh
.b:
    sub ax, [es:si + MD_NEXT]
    sbb dx, [es:si + MD_NEXT + 2]
    jc .ret
    mov cx, [es:si + MD_BLEN]
    or dx, dx
    jnz .n
    cmp ax, cx
    jae .n
    mov cx, ax
.n:
    jcxz .ret
    xor dx, dx                      ; DX = the buffer's offset: 0, or BLEN
    or bx, bx
    jz .rd
    mov dx, [es:si + MD_BLEN]
.rd:
    push bx
    push dx
    mov bx, [es:si + MD_FH]
    mov ah, 0x3F
    push es
    mov es, [es:si + MD_BSEL]
    ICALL w16_dcall                 ; AX = read
    pop es
    pop dx
    pop bx
    jc .short
    or ax, ax
    jz .short
    add [es:si + MD_NEXT], ax
    adc word [es:si + MD_NEXT + 2], 0
    lea di, [si + MD_HDR + bx]
    mov [es:di + WH_DATA], dx
    push ax
    mov ax, [es:si + MD_BSEL]
    mov [es:di + WH_DATA + 2], ax
    pop ax
    mov [es:di + WH_LEN], ax
    mov word [es:di + WH_LEN + 2], 0
    mov byte [es:di + WH_FLAGS], WHDR_PREPARED
    push si
    push es
    mov cx, di
    mov dx, es
    mov ax, [w16mm_wo]
    call w16mm_wst                  ; ES = the device's state
    jc .np
    call w16mm_wpush
.np:
    pop es
    pop si
.ret:
    ret
.short:
    mov ax, [es:si + MD_TO]         ; the file ends early: so does the play
    mov dx, [es:si + MD_TO + 2]
    push cx
    mov cl, [es:si + MD_SH]
    xor ch, ch
    jcxz .sb
.ssh:
    shl ax, 1
    rcl dx, 1
    loop .ssh
.sb:
    pop cx
    mov [es:si + MD_NEXT], ax
    mov [es:si + MD_NEXT + 2], dx
    ret

; w16mm_wvstat - ES:SI = a waveaudio, DI = an MCI_STATUS item -> DX:AX, CF=0;
; CF=1 AX = why not
w16mm_wvstat:
    xor dx, dx
    cmp di, 4                       ; MODE
    jne .nm
    mov ax, MCM_STOP
    test byte [es:si + MD_FL], MDF_PLAY
    jz .ok
    mov ax, MCM_PLAY
    test byte [es:si + MD_FL], MDF_PAUSE
    jz .ok
    mov ax, MCM_PAUSE
    jmp short .ok
.nm:
    mov ax, 1
    cmp di, 5                       ; MEDIA_PRESENT, READY, the tracks and
    je .ok                          ;   the current one: 1
    cmp di, 7
    je .ok
    cmp di, 3
    je .ok
    cmp di, 8
    je .ok
    test byte [es:si + MD_FL], MDF_ELEM
    jz .nofile
    cmp di, 1                       ; LENGTH
    jne .np
    call w16mm_wvlen
    jmp short .fmt
.np:
    cmp di, 2                       ; POSITION, or the start
    jne .w
    xor ax, ax
    test byte [es:MB_XFL + 1], MCF_X2 >> 8
    jnz .fmt
    call w16mm_wvhere
.fmt:
    call w16mm_wvout
    jmp short .ok
.w:
    mov bx, di                      ; the fmt chunk's fields, 4001h-4006h
    sub bx, 0x4001
    cmp bx, 6
    jae .range
    mov bl, [cs:w16mm_fmtoff + bx]  ; XSEC-OK: the part's table, at cs:
    xor bh, bh
    add bx, si
    mov ax, [es:bx + MD_FMT]
    cmp di, 0x4003                  ; the two DWORDs
    je .dw
    cmp di, 0x4004
    jne .ok
.dw:
    mov dx, [es:bx + MD_FMT + 2]
.ok:
    clc
    ret
.nofile:
    mov ax, MCE_NEEDFILE
    stc
    ret
.range:
    mov ax, MCE_RANGE
    stc
    ret

w16mm_fmtoff: db 0, 2, 4, 8, 12, 14  ; the fmt chunk's fields, 4001h-4006h

; =============================================================================
; mciSendString's words
; =============================================================================

; w16mm_mcpy - DS:SI = the program's string, ES = the block: its words at
; MB_CMD, lower case, each NUL-ended, quotes taken off; an empty one ends
; them. CF=1 AX = MCIERR_NO_CLOSING_QUOTE
w16mm_mcpy:
    mov di, MB_CMD
    mov cx, W16MM_CMDN - 2
    cld
.sp:
    lodsb
    cmp al, ' '
    je .sp
    cmp al, 9
    je .sp
    or al, al
    jz .end
    cmp al, '"'
    je .q
.w:
    cmp al, 'A'
    jb .s
    cmp al, 'Z'
    ja .s
    or al, 0x20
.s:
    stosb
    dec cx
    jz .end
    lodsb
    cmp al, ' '
    je .ew
    cmp al, 9
    je .ew
    or al, al
    jz .eend
    jmp short .w
.q:
    lodsb
    or al, al
    jz .noq
    cmp al, '"'
    je .ew
    stosb
    dec cx
    jz .end
    jmp short .q
.ew:
    xor al, al
    stosb
    dec cx
    jnz .sp
    jmp short .end
.eend:
    xor al, al
    stosb
.end:
    xor ax, ax
    stosw
    mov word [es:MB_TOK], MB_CMD
    clc
    ret
.noq:
    mov ax, MCE_QUOTE
    stc
    ret

; w16mm_mword - ES = the block -> DI = the next word, MB_TOK past it; ZF when
; there are no more (DI on the empty one). Keeps the rest
w16mm_mword:
    push ax
    push cx
    mov di, [es:MB_TOK]
    cmp byte [es:di], 0
    je .out
    push di
    xor al, al
    mov cx, W16MM_CMDN
    cld
    repne scasb
    mov [es:MB_TOK], di
    pop di
    or al, 1                        ; NZ
.out:
    pop cx
    pop ax
    ret

; w16mm_mkey - ES = the block, CS:BX = a keyword (words, one space between):
; ZF and MB_TOK past its words when the next words are it; else NZ and
; MB_TOK where it was. Keeps the rest
w16mm_mkey:
    push ax
    push bx
    push di
    push word [es:MB_TOK]
.wd:
    call w16mm_mword
    jz .no
.c:
    mov al, [cs:bx]                 ; XSEC-OK: the part's keywords, at cs:
    inc bx
    cmp al, ' '
    jne .nsp
    xor al, al
.nsp:
    cmp al, [es:di]
    jne .no
    inc di
    or al, al
    jnz .c
    cmp byte [cs:bx - 1], ' '       ; XSEC-OK: another word follows
    je .wd
    pop ax                          ; (MB_TOK stays past it)
    xor ax, ax                      ; ZF
    jmp short .out
.no:
    pop word [es:MB_TOK]
    or sp, sp                       ; NZ
.out:
    pop di
    pop bx
    pop ax
    ret

; w16mm_mnum - ES = the block: the next word as a number -> DX:AX, CF=0. A
; word of fields with colons between packs them a byte each, the first lowest
; (MSF, TMSF). CF=1 AX = MCIERR_MISSING_PARAMETER or MCIERR_BAD_INTEGER.
; Keeps the rest
w16mm_mnum:
    push bx
    push cx
    push si
    push di
    call w16mm_mword
    mov ax, MCE_MISSING
    jz .no
    xor ax, ax
    xor dx, dx
    xor bx, bx                      ; BX = the fields packed so far, low
    xor si, si                      ;   SI their high half; CL their count
    xor cl, cl
    cmp byte [es:di], 0
    je .bad
.d:
    mov ch, [es:di]
    inc di
    or ch, ch
    jz .end
    cmp ch, ':'
    je .field
    sub ch, '0'
    jb .bad
    cmp ch, 9
    ja .bad
    push cx                         ; DX:AX = DX:AX * 10 + the digit
    mov cx, 10
    call w16mm_mmul
    pop cx
    add al, ch
    adc ah, 0
    adc dx, 0
    jmp short .d
.field:
    call .pack
    jc .bad
    xor ax, ax
    xor dx, dx
    jmp short .d
.end:
    or cl, cl
    jz .ok                          ; one plain number
    call .pack
    jc .bad
    mov ax, bx
    mov dx, si
.ok:
    clc
    jmp short .out
.bad:
    mov ax, MCE_INTEGER
.no:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    ret
; .pack - AX = a field, under 256, into byte CL of SI:BX
.pack:
    or dx, dx
    jnz .pno
    or ah, ah
    jnz .pno
    cmp cl, 4
    jae .pno
    push cx
    mov ch, cl
    shl ch, 1
    shl ch, 1
    shl ch, 1                       ; CH = 8 * the field's place
    mov cl, ch
    and cl, 0x0F
    xor ah, ah
    xor dx, dx
    cmp ch, 16
    jb .lo
    xchg ax, dx                     ; into the high word
.lo:
    shl ax, cl
    shl dx, cl
    or bx, ax
    or si, dx
    pop cx
    inc cl
    clc
    ret
.pno:
    stc
    ret

; w16mm_mparse - ES = the block, its words at MB_CMD: the verb, the device
; and the keywords read into MB_X* and MB_PB, MB_RK what it will answer. CF=1
; AX = why not
w16mm_mparse:
    mov di, MB_PB                   ; nothing asked
    mov cx, 12
    xor ax, ax
    cld
    rep stosw
    mov [es:MB_XFL], ax
    mov word [es:MB_XPB], MB_PB
    mov [es:MB_XPB + 2], es
    mov si, w16mm_verbs             ; XSEC-OK: the part's table, at cs:
.v:
    cmp byte [cs:si], 0
    je .badcmd
    mov bx, si
    call w16mm_mkey
    je .verb
.vs:
    inc si
    cmp byte [cs:si - 1], 0
    jne .vs
    add si, 5
    jmp short .v
.verb:
    inc si
    cmp byte [cs:si - 1], 0
    jne .verb
    mov ax, [cs:si]                 ; the message
    mov [es:MB_XMSG], ax
    mov al, [cs:si + 4]             ; what it answers
    mov [es:MB_RK], al
    mov bp, [cs:si + 2]             ; BP = its keywords
    call w16mm_mword                ; DI = the device
    mov ax, MCE_NODEV
    jz .no
    mov word [es:MB_XDEV], 0xFFFF
    cmp word [es:MB_XMSG], MCI_OPEN
    jne .nopen
    call w16mm_popen
    jmp short .keys
.nopen:
    cmp word [es:MB_XMSG], MCI_SYSINFO
    jne .ndev
    call w16mm_pout
    xor ax, ax
    call w16mm_pall
    je .sty
    mov ax, di
    mov dx, es
    call w16mm_mtype                ; CL = the type
    mov ax, MCE_DEVNAME
    jc .no
    mov ax, MCI_DT_CD
    cmp cl, MDT_CD
    je .sty
    mov ax, MCI_DT_WAVE
.sty:
    mov [es:MB_PB + 16], ax
    jmp short .keys
.ndev:
    call w16mm_pall
    je .keys
    call w16mm_mfind                ; SI = its record
    mov ax, MCE_DEVNAME
    jc .no
    mov al, [es:si + MD_ID]
    xor ah, ah
    mov [es:MB_XDEV], ax
    cmp word [es:MB_XMSG], MCI_INFO
    jne .keys
    call w16mm_pout
.keys:
    mov di, [es:MB_TOK]
    cmp byte [es:di], 0
    je .done
    mov si, w16mm_kcom              ; XSEC-OK: notify and wait
    call w16mm_kmatch
    jnc .keys
    cmp ax, MCE_KEYWORD
    jne .no
    mov si, bp
    call w16mm_kmatch
    jnc .keys
.no:
    stc
    ret
.done:
    clc
    ret
.badcmd:
    mov ax, MCE_COMMAND
    stc
    ret

; w16mm_pall - ES:DI = a word: ZF when it is "all". Keeps all
w16mm_pall:
    cmp word [es:di], 'al'
    jne .ret
    cmp word [es:di + 2], 'l'
.ret:
    ret

; w16mm_popen - ES = the block, DI = open's device word: a type's name
; (MCI_OPEN_TYPE), a file (MCI_OPEN_ELEMENT), or type!file. Keeps BP
w16mm_popen:
    mov si, di                      ; type!element
.b:
    mov al, [es:si]
    inc si
    or al, al
    jz .one
    cmp al, '!'
    jne .b
    mov byte [es:si - 1], 0
    mov [es:MB_PB + 8], di
    mov [es:MB_PB + 10], es
    mov [es:MB_PB + 12], si
    mov [es:MB_PB + 14], es
    or word [es:MB_XFL], MCF_X20 | MCF_X2
    ret
.one:
    mov ax, di
    mov dx, es
    call w16mm_mtype
    jc .elem
    mov [es:MB_PB + 8], di
    mov [es:MB_PB + 10], es
    or word [es:MB_XFL], MCF_X20
    ret
.elem:
    mov [es:MB_PB + 12], di
    mov [es:MB_PB + 14], es
    or word [es:MB_XFL], MCF_X2
    ret

; w16mm_pout - ES = the block: lpstrReturn and dwRetSize (+4, +8) name
; MB_OUT, for an answer the command writes. Keeps all
w16mm_pout:
    mov word [es:MB_PB + 4], MB_OUT
    mov [es:MB_PB + 6], es
    mov word [es:MB_PB + 8], 64
    ret

; w16mm_kmatch - ES = the block, CS:SI = a keyword table: the first keyword
; that the next words are taken, with its flag, its value and what it
; answers. CF=0; CF=1 AX = MCIERR_UNRECOGNIZED_KEYWORD for none, or why its
; value is not. Keeps BP
w16mm_kmatch:
.e:
    cmp byte [cs:si], 0             ; XSEC-OK: the part's table, at cs:
    je .none
    mov bx, si
    call w16mm_mkey
    je .hit
.sk:
    inc si
    cmp byte [cs:si - 1], 0         ; XSEC-OK
    jne .sk
    add si, 6
    jmp short .e
.none:
    mov ax, MCE_KEYWORD
    stc
    ret
.hit:
    inc si
    cmp byte [cs:si - 1], 0         ; XSEC-OK
    jne .hit
    mov ax, [cs:si]                 ; XSEC-OK: its flag
    or [es:MB_XFL], ax
    mov al, [cs:si + 5]             ; XSEC-OK: what it answers
    or al, al
    jz .rk
    mov [es:MB_RK], al
.rk:
    mov bl, [cs:si + 3]             ; XSEC-OK: BX = its slot
    xor bh, bh
    mov al, [cs:si + 2]             ; XSEC-OK: what it carries
    cmp al, W16MM_KA_POS
    je .num
    cmp al, W16MM_KA_VAL
    je .val
    cmp al, W16MM_KA_STR
    je .str
    cmp al, W16MM_KA_TF
    je .tf
    clc
    ret
.num:
    call w16mm_mnum
    jc .ret
    jmp short .put
.val:
    mov al, [cs:si + 4]             ; XSEC-OK: a byte, or 40h and up as the
    xor ah, ah                      ;   status items 4001h-4006h
    cmp al, 0x40
    jb .v1
    add ax, 0x4000 - 0x40 + 1
.v1:
    xor dx, dx
    jmp short .put
.str:
    call w16mm_mword
    mov ax, MCE_MISSING
    stc
    jz .ret
    mov ax, di
    mov dx, es
    jmp short .put
.tf:
    call w16mm_mword
    mov ax, MCE_MISSING
    stc
    jz .ret
    push si
    mov si, w16mm_tfn               ; XSEC-OK: the formats' names
.tn:
    mov al, [cs:si]                 ; XSEC-OK
    cmp al, 0xFF
    je .tbad
    inc si
    push di
    mov bx, si
.tc:
    mov ah, [cs:bx]                 ; XSEC-OK
    cmp ah, [es:di]
    jne .tne
    inc bx
    inc di
    or ah, ah
    jnz .tc
    pop di
    pop si
    xor ah, ah
    xor dx, dx
    mov bx, 4
    jmp short .put
.tne:
    pop di
.tsk:
    inc si
    cmp byte [cs:si - 1], 0         ; XSEC-OK
    jne .tsk
    jmp short .tn
.tbad:
    pop si
    mov ax, MCE_TFMT
    stc
    ret
.put:
    add bx, MB_PB
    mov [es:bx], ax
    mov [es:bx + 2], dx
    clc
.ret:
    ret

; w16mm_mans - ES = the block, the string's command carried out: what it
; answers (MB_RK) written to DX:DI, CX bytes, as text, cut to them. AX = 0;
; clobbers BX, CX, DX, SI, DI
w16mm_mans:
    mov ax, dx
    or ax, di
    jz .r0
    or cx, cx
    jnz .go
.r0:
    xor ax, ax
    ret
.go:
    push dx                         ; the program's room
    push di
    push cx
    mov di, MB_OUT + 32             ; the text made here, then copied
    mov byte [es:di], 0
    mov al, [es:MB_RK]
    mov bx, [es:MB_PB + 4]          ; DX:BX = the DWORD answered
    mov dx, [es:MB_PB + 6]
    cmp al, W16MM_RK_STR
    jne .n1
    mov si, MB_OUT
    jmp .copy
.n1:
    cmp al, W16MM_RK_QTY
    jne .n2
    mov bx, [es:MB_OUT]
    mov dx, [es:MB_OUT + 2]
    jmp .int
.n2:
    cmp al, W16MM_RK_NONE
    je .made
    cmp al, W16MM_RK_INT
    je .int
    cmp al, W16MM_RK_POS
    je .pos
    cmp al, W16MM_RK_LEN
    je .pos
    cmp al, W16MM_RK_BOOL
    jne .n3
    mov si, w16mm_s_false           ; XSEC-OK: the part's names, at cs:
    or bx, bx
    jz .cs
    mov si, w16mm_s_true            ; XSEC-OK
    jmp short .cs
.n3:
    cmp al, W16MM_RK_TYPE
    jne .n4
    mov si, w16mm_s_audio           ; XSEC-OK
    cmp bx, 1088
    je .cs
    mov si, w16mm_s_other           ; XSEC-OK
    jmp short .cs
.n4:
    cmp al, W16MM_RK_DEV
    jne .n5
    mov si, w16mm_s_cd              ; XSEC-OK
    cmp bx, MCI_DT_CD
    je .cs
    mov si, w16mm_s_wave            ; XSEC-OK
    jmp short .cs
.n5:
    mov si, w16mm_tfn               ; XSEC-OK: a time format by its number
    cmp al, W16MM_RK_TF
    je .nl
    mov si, w16mm_moden             ; XSEC-OK: a mode by its number - 524
    sub bx, MCM_NOTREADY
.nl:
    mov al, [cs:si]
    cmp al, 0xFF
    je .made
    inc si
    cmp al, bl
    je .cs
.nk:
    inc si
    cmp byte [cs:si - 1], 0
    jne .nk
    jmp short .nl
.cs:                                ; CS:SI's text, to ES:DI
    mov al, [cs:si]
    inc si
    stosb
    or al, al
    jnz .cs
    jmp short .made
.pos:
    push ax
    push bx
    mov bx, [es:MB_XDEV]
    call w16mm_mrec                 ; SI = the device
    mov cl, MCT_MS
    jc .p0
    mov cl, [es:si + MD_TF]
.p0:
    pop bx
    pop ax
    cmp cl, MCT_MSF
    je .three
    cmp cl, MCT_TMSF
    jne .int
    cmp al, W16MM_RK_LEN            ; a length in TMSF is MSF's
    je .three
    mov cx, 4
    jmp short .fields
.three:
    mov cx, 3
.fields:                            ; DX:BX's bytes, two digits each
    mov [es:MB_OUT + 28], bx
    mov [es:MB_OUT + 30], dx
    mov si, MB_OUT + 28
.fl:
    mov al, [es:si]
    inc si
    aam                             ; AH = tens, AL = units
    xchg al, ah
    add ax, '00'
    stosw
    dec cx
    jz .fz
    mov al, ':'
    stosb
    jmp short .fl
.fz:
    mov byte [es:di], 0
    jmp short .made
.int:
    mov ax, bx                      ; DX:AX in decimal
    call w16mm_mdec
.made:
    mov si, MB_OUT + 32
.copy:
    pop cx
    pop di
    pop dx
    push ds
    push es
    pop ds                          ; DS:SI = the answer
    mov es, dx                      ; ES:DI = the program's room
    dec cx
.c:
    lodsb
    or al, al
    jz .z
    jcxz .z
    stosb
    dec cx
    jmp short .c
.z:
    xor ax, ax                      ; (the string's answer: done)
    stosb
    pop ds
    ret

; w16mm_mdec - DX:AX, ES:DI = room: in decimal, NUL-ended. Keeps all
w16mm_mdec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in SI
    push ax
    push bx
    push cx
    push dx
    push si
    xor si, si                      ; SI = digits pushed
    mov cx, 10
.d:
    call w16mm_mdiv                 ; BX = the digit
    push bx
    inc si
    mov bx, ax
    or bx, dx
    jnz .d
.p:
    pop ax
    add al, '0'
    stosb
    dec si
    jnz .p
    mov byte [es:di], 0
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Arithmetic
; =============================================================================

; w16mm_mmul - DX:AX times CX -> DX:AX, its low 32 bits. Keeps the rest
w16mm_mmul:
    push bx
    push si
    mov si, dx
    mul cx                          ; DX:AX = the low word's
    mov bx, dx
    xchg ax, si                     ; SI = its low word, AX = the high word
    mul cx
    add ax, bx
    mov dx, ax
    mov ax, si
    pop si
    pop bx
    ret

; w16mm_mdiv - DX:AX over CX -> DX:AX the quotient, BX the remainder. Keeps
; the rest
w16mm_mdiv:
    push si
    mov si, ax
    mov ax, dx
    xor dx, dx
    div cx
    xchg ax, si                     ; SI = the high quotient
    div cx
    mov bx, dx
    mov dx, si
    pop si
    ret

; =============================================================================
; The tables
; =============================================================================

W16MM_KA_FLAG   equ 0               ; a keyword carries: nothing more,
W16MM_KA_POS    equ 1               ;   a number or a position (the next word),
W16MM_KA_VAL    equ 2               ;   a value of its own,
W16MM_KA_STR    equ 3               ;   the next word, as a string,
W16MM_KA_TF     equ 4               ;   a time format's name (the next word)

; a keyword: its words NUL-ended; dw its flag; db what it carries, its slot
; in the parameter block, its value, and what the command then answers (0:
; what the verb does)
%macro W16MMKEY 6
    db %1, 0
    dw %2
    db %3, %4, %5, %6
%endmacro

; a verb: its name NUL-ended; dw the message, its keywords; db what it
; answers
w16mm_verbs:
    db 'open', 0
    dw MCI_OPEN, w16mm_kopen
    db W16MM_RK_INT
    db 'close', 0
    dw MCI_CLOSE, w16mm_knone
    db 0
    db 'play', 0
    dw MCI_PLAY, w16mm_kplay
    db 0
    db 'stop', 0
    dw MCI_STOP, w16mm_knone
    db 0
    db 'pause', 0
    dw MCI_PAUSE, w16mm_knone
    db 0
    db 'resume', 0
    dw MCI_RESUME, w16mm_knone
    db 0
    db 'seek', 0
    dw MCI_SEEK, w16mm_kseek
    db 0
    db 'status', 0
    dw MCI_STATUS, w16mm_kstat
    db 0
    db 'set', 0
    dw MCI_SET, w16mm_kset
    db 0
    db 'info', 0
    dw MCI_INFO, w16mm_kinfo
    db W16MM_RK_STR
    db 'capability', 0
    dw MCI_GETDEVCAPS, w16mm_kcaps
    db 0
    db 'sysinfo', 0
    dw MCI_SYSINFO, w16mm_ksys
    db W16MM_RK_STR
    db 0

w16mm_kcom:
    W16MMKEY 'notify', MCF_NOTIFY, 0, 0, 0, 0
    W16MMKEY 'wait', MCF_WAIT, 0, 0, 0, 0
w16mm_knone:
    db 0
w16mm_kopen:
    W16MMKEY 'type', MCF_X20, W16MM_KA_STR, 8, 0, 0
    W16MMKEY 'alias', MCF_X4, W16MM_KA_STR, 16, 0, 0
    W16MMKEY 'shareable', MCF_X1, 0, 0, 0, 0
    db 0
w16mm_kplay:
    W16MMKEY 'from', MCF_FROM, W16MM_KA_POS, 4, 0, 0
    W16MMKEY 'to', MCF_TO, W16MM_KA_POS, 8, 0, 0
    db 0
w16mm_kseek:
    W16MMKEY 'to start', MCF_X1, 0, 0, 0, 0
    W16MMKEY 'to end', MCF_X2, 0, 0, 0, 0
    W16MMKEY 'to', MCF_TO, W16MM_KA_POS, 4, 0, 0
    db 0
w16mm_kstat:
    W16MMKEY 'length', MCF_X1, W16MM_KA_VAL, 8, 1, W16MM_RK_LEN
    W16MMKEY 'position', MCF_X1, W16MM_KA_VAL, 8, 2, W16MM_RK_POS
    W16MMKEY 'start position', MCF_X1 | MCF_X2, W16MM_KA_VAL, 8, 2, W16MM_RK_POS
    W16MMKEY 'number of tracks', MCF_X1, W16MM_KA_VAL, 8, 3, W16MM_RK_INT
    W16MMKEY 'mode', MCF_X1, W16MM_KA_VAL, 8, 4, W16MM_RK_MODE
    W16MMKEY 'media present', MCF_X1, W16MM_KA_VAL, 8, 5, W16MM_RK_BOOL
    W16MMKEY 'time format', MCF_X1, W16MM_KA_VAL, 8, 6, W16MM_RK_TF
    W16MMKEY 'ready', MCF_X1, W16MM_KA_VAL, 8, 7, W16MM_RK_BOOL
    W16MMKEY 'current track', MCF_X1, W16MM_KA_VAL, 8, 8, W16MM_RK_INT
    W16MMKEY 'type', MCF_X1, W16MM_KA_VAL, 8, 0x40, W16MM_RK_TYPE
    W16MMKEY 'format tag', MCF_X1, W16MM_KA_VAL, 8, 0x40, W16MM_RK_INT
    W16MMKEY 'channels', MCF_X1, W16MM_KA_VAL, 8, 0x41, W16MM_RK_INT
    W16MMKEY 'samplespersec', MCF_X1, W16MM_KA_VAL, 8, 0x42, W16MM_RK_INT
    W16MMKEY 'bytespersec', MCF_X1, W16MM_KA_VAL, 8, 0x43, W16MM_RK_INT
    W16MMKEY 'alignment', MCF_X1, W16MM_KA_VAL, 8, 0x44, W16MM_RK_INT
    W16MMKEY 'bitspersample', MCF_X1, W16MM_KA_VAL, 8, 0x45, W16MM_RK_INT
    W16MMKEY 'track', MCF_TRACK, W16MM_KA_POS, 12, 0, 0
    db 0
w16mm_kset:
    W16MMKEY 'time format', MCF_X4, W16MM_KA_TF, 4, 0, 0
    W16MMKEY 'door open', MCF_X1, 0, 0, 0, 0
    W16MMKEY 'door closed', MCF_X2, 0, 0, 0, 0
    W16MMKEY 'audio all', MCF_X8, W16MM_KA_VAL, 8, 0, 0
    W16MMKEY 'audio left', MCF_X8, W16MM_KA_VAL, 8, 1, 0
    W16MMKEY 'audio right', MCF_X8, W16MM_KA_VAL, 8, 2, 0
    W16MMKEY 'on', MCF_X20, 0, 0, 0, 0
    W16MMKEY 'off', 0x4000, 0, 0, 0, 0
    db 0
w16mm_kinfo:
    W16MMKEY 'product', MCF_X1, 0, 0, 0, 0
    W16MMKEY 'file', MCF_X2, 0, 0, 0, 0
    db 0
w16mm_kcaps:
    W16MMKEY 'can record', MCF_X1, W16MM_KA_VAL, 8, 1, W16MM_RK_BOOL
    W16MMKEY 'has audio', MCF_X1, W16MM_KA_VAL, 8, 2, W16MM_RK_BOOL
    W16MMKEY 'has video', MCF_X1, W16MM_KA_VAL, 8, 3, W16MM_RK_BOOL
    W16MMKEY 'device type', MCF_X1, W16MM_KA_VAL, 8, 4, W16MM_RK_DEV
    W16MMKEY 'uses files', MCF_X1, W16MM_KA_VAL, 8, 5, W16MM_RK_BOOL
    W16MMKEY 'compound device', MCF_X1, W16MM_KA_VAL, 8, 6, W16MM_RK_BOOL
    W16MMKEY 'can eject', MCF_X1, W16MM_KA_VAL, 8, 7, W16MM_RK_BOOL
    W16MMKEY 'can play', MCF_X1, W16MM_KA_VAL, 8, 8, W16MM_RK_BOOL
    W16MMKEY 'can save', MCF_X1, W16MM_KA_VAL, 8, 9, W16MM_RK_BOOL
    db 0
w16mm_ksys:
    W16MMKEY 'quantity', MCF_X1, 0, 0, 0, W16MM_RK_QTY
    W16MMKEY 'open', MCF_X2, 0, 0, 0, 0
    W16MMKEY 'name', MCF_X4, W16MM_KA_POS, 12, 0, W16MM_RK_STR
    db 0

; the time formats by number and name, and the modes by number - 524
w16mm_tfn:
    db MCT_MS, 'milliseconds', 0
    db MCT_MS, 'ms', 0
    db MCT_MSF, 'msf', 0
    db MCT_TMSF, 'tmsf', 0
    db MCT_BYTES, 'bytes', 0
    db MCT_SAMPLES, 'samples', 0
    db 0xFF
w16mm_moden:
    db 0, 'not ready', 0
    db 1, 'stopped', 0
    db 2, 'playing', 0
    db 5, 'paused', 0
    db 0xFF

w16mm_s_cd:     db 'cdaudio', 0
w16mm_s_wave:   db 'waveaudio', 0
w16mm_s_cdp:    db 'CD Audio', 0
w16mm_s_wvp:    db 'Waveform Audio', 0
w16mm_s_true:   db 'true', 0
w16mm_s_false:  db 'false', 0
w16mm_s_audio:  db 'audio', 0
w16mm_s_other:  db 'other', 0

; mciGetErrorString's sentences (Wine's winmm, LGPL-2.1-or-later): db the
; code less MCIERR_BASE, the sentence
w16mm_s_mcok:   db 'The specified command was carried out.', 0
w16mm_s_mcix:   db 'Undefined external error.', 0
w16mm_merrs:
    db 1, 'Invalid MCI device ID. Use the ID returned when opening the MCI device.', 0
    db 3, 'The driver cannot recognize the specified command parameter.', 0
    db 5, 'The driver cannot recognize the specified command.', 0
    db 6, 'There is a problem with your media device. Make sure it is working correctly or contact the device manufacturer.', 0
    db 7, 'The specified device is not open or is not recognized by MCI.', 0
    db 8, 'Not enough memory available for this task. Quit one or more applications to increase available memory, and then try again.', 0
    db 9, 'The device name is already being used as an alias by this application. Use a unique alias.', 0
    db 12, 'The output string was too large to fit in the return buffer. Increase the size of the buffer.', 0
    db 14, 'The specified integer is invalid for this command.', 0
    db 17, 'The specified command requires a parameter. Please supply one.', 0
    db 18, 'The MCI device you are using does not support the specified command.', 0
    db 19, 'Cannot find the specified file. Make sure the path and filename are correct.', 0
    db 20, 'The device driver is not ready.', 0
    db 23, "Cannot use 'all' as the device name with the specified command.", 0
    db 25, 'Cannot determine the device type from the given filename extension.', 0
    db 26, 'The specified parameter is out of range for the specified command.', 0
    db 33, 'The specified alias is already being used in this application. Use a unique alias.', 0
    db 36, 'The specified command requires an alias, file, driver, or device name. Please supply one.', 0
    db 37, 'The specified value for the time format is invalid. Refer to the MCI documentation for valid formats.', 0
    db 38, 'A closing double-quotation mark is missing from the parameter value. Please supply one.', 0
    db 40, 'The specified file cannot be played on the specified MCI device. The file may be corrupt, or not in the correct format.', 0
    db 41, 'A null parameter block was passed to MCI.', 0
    db 48, 'The filename is invalid. Make sure the filename is not longer than 8 characters, followed by a period and an extension.', 0
    db 50, 'The specified device is not installed on the system. Use the Drivers option in Control Panel to install the device.', 0
    db 64, 'All wave devices that can play files in the current format are in use. Wait until a wave device is free, and then try again.', 0
    db 70, 'There is no wave device that can play files in the current format. Use the Drivers option to install the wave device.', 0
    db 0xFF

section .text
