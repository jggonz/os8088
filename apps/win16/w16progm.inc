; =============================================================================
; os8088 - apps/win16/w16progm.inc
;
; PROGRAM MANAGER'S DDE SERVER (SPEC.md 124.33.2, docs/plans/WIN16-ANY-PLAN.md
; S24): what a setup program asks Program Manager for when it finishes -
; [CreateGroup(...)][AddItem(...)] over DDE - answered inside the instance.
; A group is a folder in the Windows directory's GROUPS, an item a link in it
; (SPEC.md 26.10.1), and a new group's folder a desktop shortcut (26.9.6).
;
; The conversation reaches here through S21 batch 5's routing: the broadcast
; of WM_DDE_INITIATE ends in w16pg_init (w16t_bsend), and a post to the
; server's window is run here in the call, not queued (w16t_postmsg). All of
; it is PART 9's: no image byte on either arm.
; =============================================================================

section .w16sys

WM_DDE_INITIATE  equ 0x03E0
WM_DDE_TERMINATE equ 0x03E1
WM_DDE_ADVISE    equ 0x03E2
WM_DDE_ACK       equ 0x03E4
WM_DDE_DATA      equ 0x03E5
WM_DDE_POKE      equ 0x03E7
WM_DDE_EXECUTE   equ 0x03E8
W16PG_FACK       equ 0x8000         ; DDEACK's fAck

W16PG_NARG  equ 10                  ; arguments a command keeps (AddItem's)
W16PG_ARGV  equ 0                   ; W16_GBUF: the arguments' offsets, 0 none
W16PG_GRP   equ 20                  ;   ...a group's 8.3 name
W16PG_ITEM  equ 32                  ;   ...an item's link's, with .LNK
W16PG_NAME  equ 48                  ;   ...a folder name to step into
W16PG_TXT   equ 64                  ;   ...the arguments' text
W16PG_TXTE  equ 256
W16PG_LNK   equ 256                 ;   ...a link (SPEC.md 26.10.1)
W16PG_REC   equ 320                 ;   ...a desktop record (26.9.6), 128

W16PG_LKSZ  equ 50                  ; a link: 'LK', 1, the volume, the kind,
W16PG_LKPTH equ 5                   ;   and the path below the root, NUL - 44
W16PG_SC0   equ 9                   ; the shortcuts' zones on kern_big, and
W16PG_SCN   equ 15                  ;   how many (SPEC.md 26.8, SC_MAX)

; -----------------------------------------------------------------------------
; w16pg_init - the end of a WM_DDE_INITIATE broadcast (w16t_bsend): DX = the
; client's window, CX = the application's atom, BX = the topic's. Each 0 or
; PROGMAN: a server window for the client, and WM_DDE_ACK sent to it with the
; atom PROGMAN twice - a call into the program when the window is the caller's
; own. The answer is the broadcast's, 1
; -----------------------------------------------------------------------------
w16pg_init:
    push si
    push di
    push bx
    push cx
    push dx
    push cs                         ; XSEC-OK: the name is read through ES
    pop es
    mov di, w16pg_s_pm              ; XSEC-OK: read through ES = CS
    mov al, 1
    call w16i_atomat                ; AX = PROGMAN's atom
    pop dx
    pop cx
    pop bx
    or ax, ax
    jz .one
    jcxz .app
    cmp cx, ax
    jne .one
.app:
    or bx, bx
    jz .topic
    cmp bx, ax
    jne .one
.topic:
    mov di, ax
    call w16pg_server               ; AX = the server's window
    jc .one
    mov [w16_sm_wp], ax
    mov [w16_sm_hwnd], dx
    mov word [w16_sm_msg], WM_DDE_ACK
    mov [w16_sm_lp], di
    mov [w16_sm_lp + 2], di
    mov ax, dx
    ICALL w16_hwnd2rec              ; BX = the client's window
    jc .one
    mov si, w16_cont_one            ; XSEC-OK: an image continuation
    xor ax, ax
    ICALL w16_sendwnd               ; the broadcast answers 1
    jmp short .out
.one:
    mov ax, 1
    xor dx, dx
.out:
    pop di
    pop si
    ret

; w16pg_server - DX = a client's window: AX = its server window, a hidden
; child of it of kind W16Q_DDE, made now if it has none. CF=1 none could be.
; Keeps BX, CX, DX
w16pg_server:
    push bx
    push cx
    push dx
    xor cx, cx
.f:
    ICALL w16_slot2rec
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .n
    test byte [bx + W16W_FLAGS], W16WF_DYING
    jnz .n
    cmp byte [bx + W16W_KIND], W16Q_DDE
    jne .n
    cmp [bx + W16W_PARENT], dx
    jne .n
    ICALL w16_rec2hwnd
    clc
    jmp short .out
.n:
    inc cx
    cmp cx, W16_MAXWND
    jb .f
    mov byte [w16_cwkind], W16Q_DDE
    mov byte [w16_cwcls], 0
    ICALL w16_ctlstub               ; the controls' procedure: it answers 0
    mov [w16_cwproc], ax
    mov [w16_cwproc + 2], dx
    pop dx
    push dx
    mov [w16_cwpar], dx
    xor ax, ax
    mov [w16_cwhbr], ax
    mov [w16_cwstyle], ax
    mov word [w16_cwstyle + 2], WS_CHILD_HI     ; hidden, and nowhere
    mov [w16_cwx], ax
    mov [w16_cwy], ax
    mov [w16_cwcx], ax
    mov [w16_cwcy], ax
    mov [w16_cwid], ax
    mov [w16_cwtext], ax
    mov [w16_cwtext + 2], ax
    ICALL w16_mkwnd                 ; AX = its HWND; CF=1 refused
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16pg_post - PostMessage to the server's window (w16t_postmsg), [w16_sm_*]
; its arguments: run now. WM_DDE_EXECUTE runs its commands and acknowledges,
; WM_DDE_TERMINATE is answered in kind, and a request, an advise, an unadvise
; or a poke is acknowledged negatively. The answer is 1, as a post's
; -----------------------------------------------------------------------------
w16pg_post:
    push si
    push di
    push word [w16_sm_hwnd]         ; the server: wParam of the answer
    mov ax, [w16_sm_msg]
    mov dx, [w16_sm_wp]             ; the client
    cmp ax, WM_DDE_EXECUTE
    je .exec
    cmp ax, WM_DDE_TERMINATE
    je .term
    cmp ax, WM_DDE_ADVISE
    jb .done
    cmp ax, WM_DDE_POKE
    ja .done
    cmp ax, WM_DDE_ACK
    je .done
    cmp ax, WM_DDE_DATA
    je .done
    mov cx, [w16_sm_lp + 2]         ; (0, the item's atom): not served
    xor bx, bx
    jmp short .ack
.exec:
    mov ax, [w16_sm_lp + 2]         ; hCommands
    push dx
    push ax
    call w16pg_run
    pop cx                          ; (hCommands back, as the program gave it)
    pop dx
    mov bx, W16PG_FACK
    jnc .ack
    xor bx, bx
.ack:
    mov ax, WM_DDE_ACK
    jmp short .tell
.term:
    mov ax, WM_DDE_TERMINATE
    xor bx, bx
    xor cx, cx
.tell:
    pop si
    push si
    call w16pg_tell
.done:
    pop ax
    pop di
    pop si
    mov ax, 1
    xor dx, dx
    ret

; w16pg_tell - AX = a message posted to the window DX: wParam SI, lParam
; CX:BX - to that window's task's queue
w16pg_tell:
    mov [w16_sm_hwnd], dx
    mov [w16_sm_msg], ax
    mov [w16_sm_wp], si
    mov [w16_sm_lp], bx
    mov [w16_sm_lp + 2], cx
    xor al, al                      ; (a full queue: the client times out)
    cmp byte [w16_ntk], 2
    jb .q
    jmp w16t_post
.q:
    ICALL w16_postq
    ret

; -----------------------------------------------------------------------------
; w16pg_run - AX = hCommands, as the program gave it: each [Name(args)] of
; its text run in turn. CF=0 every one ran; CF=1 one did not, or the text is
; not commands, and the rest are not run
; -----------------------------------------------------------------------------
w16pg_run:
    cld
    W16PIN
    ICALL w16_gfind                 ; ES = its header
    jc .out
    cmp byte [w16_gdisc], 0         ; discarded: no text
    stc
    jne .out
    W16VOF dx, es
    inc dx                          ; DX = the text's paragraph
    xor si, si
.next:
    push dx
    W16SEG es, dx                   ; again for each: a command's own calls
    call w16pg_skip                 ;   may have moved the window it was in
    mov al, [es:si]
    or al, al
    jz .end
    cmp al, '['
    jne .bad
    inc si
    call w16pg_parse                ; AL = the command; SI past its ]
    jc .bad
    push si
    xor ah, ah
    shl ax, 1
    mov bx, ax
    call [cs:w16pg_ctab + bx]       ; XSEC-OK: part 9's own table
    pop si
    pop dx
    jnc .next
    ret
.end:
    pop dx
    clc
    ret
.bad:
    pop dx
    stc
.out:
    ret

; w16pg_skip - ES:SI past spaces, tabs and line ends
w16pg_skip:
    mov al, [es:si]
    cmp al, ' '
    je .s
    cmp al, 9
    je .s
    cmp al, 13
    je .s
    cmp al, 10
    jne .out
.s:
    inc si
    jmp short w16pg_skip
.out:
    ret

; w16pg_parse - ES:SI just past a '[': the command's name, matched without
; case against w16pg_cmds, and its arguments into W16_GBUF (W16PG_ARGV, each
; NUL-ended in W16PG_TXT; one in quotes may hold a comma; an unquoted one
; loses its spaces at both ends). out CF=0 AL = the command, SI past its ']';
; CF=1 not a command of these, or not a command's shape
w16pg_parse:
    push di
    mov di, w16_gbuf + W16PG_ARGV   ; no arguments yet
    xor ax, ax
    mov cx, W16PG_NARG
    push es
    push ds
    pop es
    cld
    rep stosw
    pop es
    call w16pg_skip
    mov di, w16_gbuf + W16PG_NAME   ; the name, upper case, to 15
    mov cx, 15
.nm:
    mov al, [es:si]
    call w16i_up
    cmp al, 'A'
    jb .nme
    cmp al, 'Z'
    ja .nme
    mov [di], al
    inc di
    inc si
    loop .nm
.nme:
    mov byte [di], 0
    mov bx, w16pg_cmds              ; XSEC-OK: read through cs:
    xor al, al
.cmd:
    cmp byte [cs:bx], 0             ; XSEC-OK: part 9's own names
    je .bad
    mov di, w16_gbuf + W16PG_NAME
.ch:
    mov ah, [cs:bx]                 ; XSEC-OK
    cmp ah, [di]
    jne .skip
    inc bx
    inc di
    or ah, ah
    jnz .ch
    jmp short .named
.skip:
    inc bx
    cmp byte [cs:bx - 1], 0         ; XSEC-OK
    jne .skip
    inc al
    jmp short .cmd
.named:
    push ax
    call w16pg_skip
    mov di, w16_gbuf + W16PG_TXT
    xor bx, bx                      ; BX = the next argument's place
    cmp byte [es:si], '('
    jne .close
    inc si
.arg:
    call w16pg_skip
    cmp bx, 2 * W16PG_NARG
    jae .one
    mov [w16_gbuf + W16PG_ARGV + bx], di
.one:
    inc bx
    inc bx
    mov al, [es:si]
    cmp al, '"'
    jne .bare
    inc si                          ; "...": to the closing quote
.q:
    mov al, [es:si]
    or al, al
    jz .done
    inc si
    cmp al, '"'
    je .qe
    call w16pg_put
    jmp short .q
.qe:
    call w16pg_skip
    jmp short .sep
.bare:
    mov cx, di                      ; CX = past its last non-space
.b:
    mov al, [es:si]
    or al, al
    jz .done
    cmp al, ','
    je .be
    cmp al, ')'
    je .be
    inc si
    call w16pg_put
    cmp al, ' '
    je .b
    mov cx, di
    jmp short .b
.be:
    mov di, cx
.sep:
    call w16pg_end
    mov al, [es:si]
    inc si
    cmp al, ','
    je .arg
    cmp al, ')'
    jne .done
    call w16pg_skip
.close:
    cmp byte [es:si], ']'
    jne .done
    inc si
    pop ax
    clc
    pop di
    ret
.done:
    pop ax
.bad:
    stc
    pop di
    ret

; w16pg_put - AL into the arguments' text at DI, while there is room;
; w16pg_end - its NUL
w16pg_put:
    cmp di, w16_gbuf + W16PG_TXTE - 1
    jae .out
    mov [di], al
    inc di
.out:
    ret
w16pg_end:
    mov byte [di], 0
    cmp di, w16_gbuf + W16PG_TXTE - 1
    jae .out
    inc di
.out:
    ret

; w16pg_arg - BX = an argument's number: SI = its text, CF=1 it is absent or
; empty
w16pg_arg:
    shl bx, 1
    mov si, [w16_gbuf + W16PG_ARGV + bx]
    shr bx, 1
    or si, si
    jz .no
    cmp byte [si], 0
    je .no
    clc
    ret
.no:
    stc
    ret

; w16pg_83 - DS:SI = a name: its letters and digits, upper case, to 8 and to
; the first dot, NUL-ended at DS:DI - or CS:BX's when it has none. out DI =
; at the NUL. Keeps SI
w16pg_83:
    push cx
    push si
    push di
    mov cx, 8
.c:
    lodsb
    or al, al
    jz .e
    cmp al, '.'
    je .e
    call w16i_up
    cmp al, '0'
    jb .c
    cmp al, '9'
    jbe .y
    cmp al, 'A'
    jb .c
    cmp al, 'Z'
    ja .c
.y:
    mov [di], al
    inc di
    loop .c
.e:
    pop ax                          ; where it began
    cmp ax, di
    jne .done
.dflt:
    mov al, [cs:bx]                 ; XSEC-OK: part 9's own name
    mov [di], al
    or al, al
    jz .z
    inc bx
    inc di
    jmp short .dflt
.done:
    mov byte [di], 0
.z:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; Folders: the Windows directory's GROUPS, and a group's folder in it
; -----------------------------------------------------------------------------

; w16pg_go - stand in SYSTEM\APPDATA\WIN16\GROUPS on WIN16's volume, GROUPS
; made first when AL = 1 and it is not there. CF=1 it cannot be, and the
; instance stands where it stood; CF=0 and the caller ends with od_back.
; Clobbers AX, BX, DX
w16pg_go:
    push si
    push ax
    ICALL od_enter
    pop ax
    jc .out
    mov bx, w16pg_s_win             ; XSEC-OK: read through cs:
    call w16pg_csn
    push ax
    xor al, al                      ; the Windows directory is never made
    call w16pg_in
    pop ax
    jc .back
    mov bx, w16pg_s_grp             ; XSEC-OK: read through cs:
    call w16pg_csn
    call w16pg_in
    jnc .out
.back:
    ICALL od_back                   ; (the flags kept: CF=1)
.out:
    pop si
    ret

; w16pg_csn - CS:BX = a name: SI = a copy of it in DS (W16PG_NAME)
w16pg_csn:
    mov si, w16_gbuf + W16PG_NAME
    push si
.c:
    mov ah, [cs:bx]                 ; XSEC-OK: part 9's own name
    mov [si], ah
    inc bx
    inc si
    or ah, ah
    jnz .c
    pop si
    ret

; w16pg_in - SI = a NUL 8.3 folder name in DS: stand in it, made first when
; AL = 1 and it is not there. CF=1 not. Clobbers AX, BX, DX
w16pg_in:
    push ax
    ICALL od_dive
    pop ax
    jnc .out
    or al, al
    stc
    jz .out
    push si
    call OSAPI_FILE_MKDIR
    pop si
    jc .out
    ICALL od_dive
.out:
    ret

; w16pg_ingrp - stand in the current group's folder (w16pg_grp), through
; w16pg_go. CF=1 there is none, the instance where it stood
w16pg_ingrp:
    mov si, w16pg_grp               ; the current group's name, to DS
    mov di, w16_gbuf + W16PG_GRP
.c:
    mov al, [cs:si]                 ; XSEC-OK: part 9's own
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .c
    cmp byte [w16_gbuf + W16PG_GRP], 0
    stc
    je .out
    xor al, al
    call w16pg_go
    jc .out
    mov si, w16_gbuf + W16PG_GRP
    xor al, al
    call w16pg_in
    jnc .out
    ICALL od_back
.out:
    ret

; w16pg_cur - the group at W16PG_GRP the current one
w16pg_cur:
    mov si, w16_gbuf + W16PG_GRP
    mov di, w16pg_grp
.c:
    mov al, [si]
    mov [cs:di], al                 ; XSEC-OK: part 9's own, written rarely
    inc si
    inc di
    or al, al
    jnz .c
    ret

; w16pg_gname - argument 0, a group's name, as its folder's: W16PG_GRP.
; CF=1 it is absent. out SI = the name as given
w16pg_gname:
    xor bx, bx
    call w16pg_arg
    jc .out
    mov di, w16_gbuf + W16PG_GRP
    mov bx, w16pg_s_dg              ; XSEC-OK: read through cs:
    call w16pg_83
    clc
.out:
    ret

; w16pg_desk - SI = a group's name as given, W16PG_GRP its folder's: its
; desktop record at W16PG_REC (SPEC.md 26.9.6) - the folder on WIN16's volume,
; the name cut to 12 for the caption, the Disk window's folder for a picture
w16pg_desk:
    push ds
    pop es
    push si
    mov di, w16_gbuf + W16PG_REC
    xor ax, ax
    mov cx, OSAPI_DI_SIZE / 2
    cld
    rep stosw
    mov di, w16_gbuf + W16PG_REC
    mov al, [w16_homev]
    mov [di + OSAPI_DI_VOL], al
    mov byte [di + OSAPI_DI_CELL], 0xFF
    mov byte [di + OSAPI_DI_KIND], 2    ; a folder
    add di, OSAPI_DI_PATH
    mov bx, w16pg_s_path            ; XSEC-OK: read through cs:
.p:
    mov al, [cs:bx]                 ; XSEC-OK: part 9's own
    or al, al
    jz .pe
    stosb
    inc bx
    jmp short .p
.pe:
    mov si, w16_gbuf + W16PG_GRP
.g:
    lodsb
    stosb
    or al, al
    jnz .g
    pop si                          ; the caption
    mov di, w16_gbuf + W16PG_REC + OSAPI_DI_CAP
    mov cx, 12
.c:
    lodsb
    or al, al
    jz .ce
    stosb
    loop .c
.ce:
    mov di, w16_gbuf + W16PG_REC + OSAPI_DI_BODY
    mov bx, w16pg_ico               ; XSEC-OK: read through cs:
    mov cx, 64
.i:
    mov al, [cs:bx]                 ; XSEC-OK: part 9's own picture
    stosb
    inc bx
    loop .i
    mov si, w16_gbuf + W16PG_REC    ; ES:SI = the record
    ret

; -----------------------------------------------------------------------------
; The commands: each CF=0 done, CF=1 failed. The arguments are W16_GBUF's
; -----------------------------------------------------------------------------

; CreateGroup(name[,path]) - its folder, and for a new one its shortcut
w16pg_create:
    call w16pg_gname
    jc .out
    push si
    call w16pg_cur
    mov al, 1
    call w16pg_go
    jc .pop
    mov si, w16_gbuf + W16PG_GRP
    call OSAPI_FILE_MKDIR
    mov cl, 1                       ; CL = 1: made now
    jnc .made
    dec cx
    cmp ax, FERR_EXIST              ; there already: the group, as it was
    je .made
    ICALL od_back
    stc
    jmp short .pop
.made:
    ICALL od_back
    pop si
    or cl, cl
    jz .ok
    call w16pg_desk
    mov al, 1
    call OSAPI_DESK_ITEM            ; refused or not, the group is there
.ok:
    clc
.out:
    ret
.pop:
    pop si
    ret

; ShowGroup(name,cmd) - the current group. Nothing is shown
w16pg_show:
    call w16pg_gname
    jc .out
    call w16pg_cur
    clc
.out:
    ret

; DeleteGroup(name) - its folder and everything in it, then its shortcut,
; tried at each zone until one is this link (SPEC.md 26.9.6)
w16pg_delgrp:
    call w16pg_gname
    jc .out
    push si
    xor al, al
    call w16pg_go
    jc .pop
    mov si, w16_gbuf + W16PG_GRP
    mov al, 1                       ; and everything under it
    call OSAPI_FILE_RMDIR
    ICALL od_back
    pop si
    jc .out
    call w16pg_desk
    mov ah, W16PG_SC0
.z:
    push ax
    push ds
    pop es
    mov si, w16_gbuf + W16PG_REC
    xor al, al
    call OSAPI_DESK_ITEM
    pop ax
    jnc .gone
    inc ah
    cmp ah, W16PG_SC0 + W16PG_SCN
    jb .z
.gone:
    mov si, w16_gbuf + W16PG_GRP    ; the current group no longer
    mov di, w16pg_grp
.c:
    mov al, [si]
    cmp al, [cs:di]                 ; XSEC-OK
    jne .ok
    inc si
    inc di
    or al, al
    jnz .c
    mov byte [cs:w16pg_grp], 0      ; XSEC-OK: part 9's own
.ok:
    clc
.out:
    ret
.pop:
    pop si
    ret

; ReplaceItem(name), DeleteItem(name) - its link out of the current group
w16pg_delitem:
    xor bx, bx
    call w16pg_arg
    jc .out
    call w16pg_iname
    call w16pg_ingrp
    jc .out
    mov si, w16_gbuf + W16PG_ITEM
    call OSAPI_FILE_DELETE          ; (none there is no failure)
    ICALL od_back
    clc
.out:
    ret

; w16pg_iname - SI = an item's name: W16PG_ITEM = its link's, NAME.LNK
w16pg_iname:
    mov di, w16_gbuf + W16PG_ITEM
    mov bx, w16pg_s_di              ; XSEC-OK: read through cs:
    call w16pg_83
    mov bx, w16pg_s_lnk             ; XSEC-OK: read through cs:
.c:
    mov al, [cs:bx]                 ; XSEC-OK: part 9's own
    mov [di], al
    inc bx
    inc di
    or al, al
    jnz .c
    ret

; AddItem(cmdline[,name[,...]]) - a link in the current group's folder, to
; the file the command line's parameter names, or else to its program
w16pg_additem:
    xor bx, bx
    call w16pg_arg
    jc .out
    mov di, si                      ; DI = the program; past it, the rest
.w:
    mov al, [di]
    or al, al
    jz .pe
    cmp al, ' '
    je .pc
    inc di
    jmp short .w
.pc:
    mov byte [di], 0                ; the program ends here
    inc di
.ps:
    cmp byte [di], ' '
    jne .pr
    inc di
    jmp short .ps
.pr:
    push di                         ; the parameter names a file: that file
    mov bx, di
.f:
    mov al, [bx]
    or al, al
    jz .fe
    cmp al, ' '
    je .ft
    inc bx
    cmp al, '\'
    je .file
    cmp al, ':'
    je .file
    jmp short .f
.ft:
    mov byte [bx], 0
.fe:
    pop di
    jmp short .pe
.file:
    pop si                          ; SI = the file
    push si
.fz:
    mov al, [bx]                    ; to its end
    or al, al
    jz .fz2
    cmp al, ' '
    je .fz1
    inc bx
    jmp short .fz
.fz1:
    mov byte [bx], 0
.fz2:
    pop si
.pe:
    push si                         ; SI = the target
    call w16pg_link                 ; the record, W16PG_LNK
    pop si
    jc .out
    mov bx, 1                       ; the link's name: the item's...
    push si
    call w16pg_arg
    pop ax
    jnc .named
    mov si, ax                      ; ...or the target's file
    mov bx, ax
.last:
    mov al, [bx]
    inc bx
    or al, al
    jz .named
    cmp al, '\'
    je .cut
    cmp al, ':'
    jne .last
.cut:
    mov si, bx
    jmp short .last
.named:
    call w16pg_iname
    call w16pg_ingrp
    jc .out
    push ds
    pop es
    mov si, w16_gbuf + W16PG_ITEM
    mov bx, w16_gbuf + W16PG_LNK
    mov cx, W16PG_LKSZ
    xor dx, dx
    call OSAPI_FILE_WRITE           ; created or replaced
    ICALL od_back
.out:
    ret

; w16pg_link - SI = a DOS path: W16PG_LNK = a link to it (SPEC.md 26.10.1),
; kind 0, its volume the drive letter's or else WIN16's own. CF=1 it is
; longer than a link holds
w16pg_link:
    mov di, w16_gbuf + W16PG_LNK
    push ds
    pop es
    push di
    xor ax, ax
    mov cx, W16PG_LKSZ / 2
    cld
    rep stosw
    pop di
    mov word [di], 'LK'
    mov byte [di + 2], 1
    mov al, [w16_homev]
    cmp byte [si + 1], ':'
    jne .vol
    mov al, [si]
    call w16i_up
    sub al, 'A'
    inc si
    inc si
.vol:
    mov [di + 3], al
.root:
    cmp byte [si], '\'
    jne .p
    inc si
    jmp short .root
.p:
    add di, W16PG_LKPTH
    mov cx, W16PG_LKSZ - W16PG_LKPTH - 1
.c:
    lodsb
    call w16i_up
    cmp al, '/'
    jne .s
    mov al, '\'
.s:
    stosb
    or al, al
    jz .ok
    loop .c
    cmp byte [si], 0                ; 44 and no more
    je .ok
    stc
    ret
.ok:
    clc
    ret

; Reload(...), ExitProgman(...) - nothing to do
w16pg_nop:
    clc
    ret

; -----------------------------------------------------------------------------
w16pg_cmds:                         ; in w16pg_ctab's order, upper case
    db 'CREATEGROUP', 0, 'SHOWGROUP', 0, 'ADDITEM', 0, 'REPLACEITEM', 0
    db 'DELETEITEM', 0, 'DELETEGROUP', 0, 'RELOAD', 0, 'EXITPROGMAN', 0, 0
w16pg_ctab:
    dw w16pg_create, w16pg_show, w16pg_additem, w16pg_delitem
    dw w16pg_delitem, w16pg_delgrp, w16pg_nop, w16pg_nop
w16pg_s_pm:     db 'PROGMAN', 0
w16pg_s_win:    db 'WIN16', 0
w16pg_s_grp:    db 'GROUPS', 0
w16pg_s_dg:     db 'GROUP', 0
w16pg_s_di:     db 'ITEM', 0
w16pg_s_lnk:    db '.LNK', 0
w16pg_s_path:   db 'SYSTEM\APPDATA\WIN16\GROUPS\', 0
w16pg_grp:      times 9 db 0        ; the current group's folder, 0 none
w16pg_ico:                          ; the Disk window's folder (SPEC.md 25):
    dw 0, 0x7E00, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE  ; the mask
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0, 0
    dw 0, 0x7E00, 0x43FE, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002  ; the ink
    dw 0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x7FFE, 0, 0

section .text
