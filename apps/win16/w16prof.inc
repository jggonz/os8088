; =============================================================================
; os8088 - apps/win16/w16prof.inc
;
; PROFILES (SPEC.md 124.14): WIN.INI and a program's own .INI, after Wine's
; profile.c. The six KERNEL calls read and change a copy of the file held in
; a claim of its own; the changed files are written when the program ends
; (w16_freeprog) or asks for it (the NULL, NULL, NULL write), each one
; OSAPI_FILE_WRITE, in SYSTEM\APPDATA\WIN16 on the volume WIN16.O88 came from.
;
; A held file is plain text in its own segment, [w16pf_end] bytes of it, and
; every lookup walks its lines: a profile is a few hundred bytes and is read
; at a program's start and written at its end, so there is no index to keep.
; =============================================================================

W16PF_N     equ 4               ; files held at once
WPF_NAME    equ 0               ; 13: the 8.3 name, NUL-ended (0 = a free row)
WPF_FL      equ 13              ; WPFF_*
WPF_SEG     equ 14              ; the claim
WPF_LEN     equ 16              ; bytes held
WPF_CAP     equ 18              ; bytes the claim holds
WPF_LRU     equ 20              ; the use stamp
WPF_LOG     equ 22              ; the writes made to it since it was read:
WPF_LLEN    equ 24              ;   a claim (0 none) and the bytes in it
W16PF_SZ    equ 26
WPFF_DIRTY  equ 1               ; changed since it was read or written
WPFF_MEM    equ 2               ; no folder: kept in memory only
WPFF_KEEP   equ 4               ; too large, or unreadable: never written
WPFF_LOGX   equ 8               ; its log ran out: written as held, unmerged
W16PF_LOGKB equ 1               ; a log's claim (SPEC.md 124.18.2)
W16PF_MAX   equ 32000           ; the largest file held (SPEC.md 124.14)
W16PF_STR   equ 256             ; a section, key or value: 255 and a NUL

w16pf_s_win: db 'WIN.INI', 0
w16pf_s_dir: db 'WIN16', 0

; -----------------------------------------------------------------------------
; The six calls. Each copies its strings in, finds its file and works on the
; held copy; the frame is w16_args' ES:BX, Pascal order (the last argument
; at +0).
; -----------------------------------------------------------------------------

; GetProfileInt (KERNEL.57) - +0 nDefault, +2 lpKey, +6 lpApp
w16k_getprofint:
    call w16_args
    call w16pf_win
    jmp short w16pf_getint

; GetPrivateProfileInt (KERNEL.127) - +0 lpFile, +4 nDefault, +6 lpKey,
; +10 lpApp
w16k_getpprofint:
    call w16_args
    call w16pf_priv                 ; BX += 4
    ; fall into w16pf_getint

; w16pf_getint - ES:BX = (nDefault, lpKey, lpApp), the file chosen (CF=1:
; none). AX = the value, or nDefault
w16pf_getint:
    mov ax, [es:bx]
    mov [w16pf_def], ax
    jc .def
    push es
    push bx
    les ax, [es:bx + 6]
    mov dx, es
    mov di, w16pf_app
    call w16pf_cpy
    pop bx
    pop es
    jc .def
    les ax, [es:bx + 2]
    mov dx, es
    mov di, w16pf_key
    call w16pf_cpy
    jc .def
    call w16pf_open                 ; ES = the held file
    jc .def
    call w16pf_sect
    jc .def
    call w16pf_findkey
    jc .def
    mov si, [w16pf_vs]
    cmp si, [w16pf_ve]
    je .def                         ; an empty value: the default
    xor dx, dx                      ; DX = the number: its leading digits,
.dig:                               ;   unsigned - Windows 3.0 reads "-42" as 0
    cmp si, [w16pf_ve]
    jae .sign
    mov al, [es:si]
    sub al, '0'
    cmp al, 9
    ja .sign
    cbw
    push ax
    mov ax, 10
    mul dx
    pop dx
    add dx, ax
    inc si
    jmp short .dig
.sign:
    mov ax, dx
    ret
.def:
    mov ax, [w16pf_def]
    ret

; GetProfileString (KERNEL.58) - +0 nSize, +2 lpBuf, +6 lpDefault, +10 lpKey,
; +14 lpApp
w16k_getprofstr:
    call w16_args
    call w16pf_win
    jmp short w16pf_getstr

; GetPrivateProfileString (KERNEL.128) - +0 lpFile, then as above
w16k_getpprofstr:
    call w16_args
    call w16pf_priv
    ; fall into w16pf_getstr

; w16pf_getstr - ES:BX = GetProfileString's frame, the file chosen (CF=1:
; none). AX = the characters written
w16pf_getstr:
    mov [w16pf_fr], bx
    mov [w16pf_frs], es
    jc .def
    mov ax, [es:bx]                 ; the buffer: for w16pf_put
    mov [w16pf_dn], ax
    mov ax, [es:bx + 2]
    mov [w16pf_dst], ax
    mov ax, [es:bx + 4]
    mov [w16pf_dst + 2], ax
    push es
    push bx
    les ax, [es:bx + 14]
    mov dx, es
    mov di, w16pf_app
    call w16pf_cpy                  ; CF=1: no section, the section list
    pop bx
    pop es
    jc .sects
    les ax, [es:bx + 10]
    mov dx, es
    mov di, w16pf_key
    call w16pf_cpy
    jc .keys
    call w16pf_open
    jc .def
    call w16pf_sect
    jc .def
    call w16pf_findkey
    jc .def
    call w16pf_pbeg
    call w16pf_unq                  ; "q" reads as q, as on Windows 3.0
    mov si, [w16pf_vs]              ; the value, cut to nSize - 1
.v:
    cmp si, [w16pf_ve]
    jae .vend
    mov al, [es:si]
    call w16pf_put1
    jc .vend
    inc si
    jmp short .v
.vend:
    jmp w16pf_pone
.def:
    mov bx, [w16pf_fr]
    mov es, [w16pf_frs]
    jmp w16_profdef                 ; the default (w16api.inc)
.keys:
    call w16pf_open
    jc .empty
    call w16pf_sect
    jc .empty
    call w16pf_pbeg
    mov si, [w16pf_body]
.k:
    call w16pf_keyline              ; CF=1: the section's end
    jc .kend
    push si
    mov si, [w16pf_ks]
.kc:
    cmp si, [w16pf_ke]
    jae .kz
    mov al, [es:si]
    call w16pf_putm
    inc si
    jmp short .kc
.kz:
    xor al, al
    call w16pf_putm
    pop si
    jmp short .k
.kend:
    jmp w16pf_pmany
.sects:
    call w16pf_open
    jc .empty
    call w16pf_pbeg
    xor si, si
.s:
    cmp si, [w16pf_end]
    jae .send
    call w16pf_hdr                  ; CF=0: a header, [w16pf_ks]..[w16pf_ke]
    jc .s                           ;   its name; SI = the next line
    push si
    mov si, [w16pf_ks]
.sc:
    cmp si, [w16pf_ke]
    jae .sz
    mov al, [es:si]
    call w16pf_putm
    inc si
    jmp short .sc
.sz:
    xor al, al
    call w16pf_putm
    pop si
    jmp short .s
.send:
    jmp w16pf_pmany
.empty:
    call w16pf_pbeg                 ; nothing to list: "" and its second NUL
    jmp w16pf_pmany

; WriteProfileString (KERNEL.59) - +0 lpString, +4 lpKey, +8 lpApp
w16k_writeprof:
    call w16_args
    call w16pf_win
    jmp short w16pf_write

; WritePrivateProfileString (KERNEL.129) - +0 lpFile, then as above
w16k_writepprof:
    call w16_args
    call w16pf_priv
    ; fall into w16pf_write

; w16pf_write - ES:BX = (lpString, lpKey, lpApp), the file chosen (CF=1:
; none). AX = TRUE or FALSE
w16pf_write:
    mov [w16pf_fr], bx
    mov [w16pf_frs], es
    pushf
    mov ax, [es:bx]                 ; all three NULL: Windows 3.1's flush
    or ax, [es:bx + 2]
    or ax, [es:bx + 4]
    or ax, [es:bx + 6]
    or ax, [es:bx + 8]
    or ax, [es:bx + 10]
    jnz .one
    popf
    call w16pf_flushall
    mov ax, 1
    ret
.one:
    popf
    jc .no
    les ax, [es:bx + 8]
    mov dx, es
    mov di, w16pf_app
    call w16pf_cpy
    jc .no                          ; no section: FALSE
    les bx, [w16pf_fr]
    les ax, [es:bx + 4]
    mov dx, es
    mov di, w16pf_key
    call w16pf_cpy
    mov byte [w16pf_nokey], 0
    adc byte [w16pf_nokey], 0
    les bx, [w16pf_fr]
    les ax, [es:bx]
    mov dx, es
    mov di, w16pf_val
    call w16pf_cpy
    mov byte [w16pf_noval], 0
    adc byte [w16pf_noval], 0
    call w16pf_wcore
    or ax, ax
    jz .out
    call w16pf_log                  ; kept for the merge (SPEC.md 124.18.2)
.out:
    ret
.no:
    xor ax, ax
    ret

; w16pf_wcore - the write itself: [w16pf_nm], [w16pf_app], [w16pf_key],
; [w16pf_val], [w16pf_nokey] and [w16pf_noval] as w16pf_write leaves them.
; AX = TRUE or FALSE. The merge plays a log back through here
w16pf_wcore:
    call w16pf_open
    jc .no
    call w16pf_sect
    jc .newsect
    cmp byte [w16pf_nokey], 0
    jne .delsect
    call w16pf_findkey
    jc .newkey
    cmp byte [w16pf_noval], 0
    jne .delkey
    mov di, w16pf_ins               ; the key keeps its spelling: '=' and the
    mov byte [di], '='              ;   value replace the rest of its line
    inc di                          ;   (DS: ES is the held file)
    mov si, w16pf_val
    call w16pf_cat
    mov ax, [w16pf_kend]
    mov cx, [w16pf_lend]
    sub cx, ax
    jmp short .splice
.delkey:
    mov ax, [w16pf_kln]
    mov cx, [w16pf_lnx]
    sub cx, ax
    mov di, w16pf_ins
    jmp short .splice
.delsect:
    mov ax, [w16pf_hdrp]
    mov cx, [w16pf_send]
    sub cx, ax
    mov di, w16pf_ins
    jmp short .splice
.newkey:
    cmp byte [w16pf_nokey], 0       ; nothing to delete: done
    jne .yes
    cmp byte [w16pf_noval], 0
    jne .yes
    mov di, w16pf_ins               ; after the section's last line that is
    mov ax, [w16pf_last]            ;   not blank
    call w16pf_nl                   ; ...on a line of its own
    call w16pf_keyval
    mov ax, [w16pf_last]
    xor cx, cx
    jmp short .splice
.newsect:
    cmp byte [w16pf_nokey], 0
    jne .yes
    cmp byte [w16pf_noval], 0
    jne .yes
    mov di, w16pf_ins
    mov ax, [w16pf_end]
    call w16pf_nl
    mov si, w16pf_s_crlfb           ; a blank line, then the header
    call w16pf_cat
    mov si, w16pf_app
    call w16pf_cat
    mov si, w16pf_s_bcrlf
    call w16pf_cat
    call w16pf_keyval
    mov ax, [w16pf_end]
    xor cx, cx
.splice:
    mov dx, di
    sub dx, w16pf_ins               ; DX = the bytes to insert
    mov si, w16pf_ins
    call w16pf_splice
    jc .no
.yes:
    mov ax, 1
    ret
.no:
    xor ax, ax
    ret

w16pf_s_crlfb:  db 13, 10, '[', 0
w16pf_s_bcrlf:  db ']', 13, 10, 0

; w16pf_keyval - DS:DI = where to compose: key '=' value CR LF
w16pf_keyval:
    mov si, w16pf_key
    call w16pf_cat
    mov byte [di], '='
    inc di
    mov si, w16pf_val
    call w16pf_cat
    mov word [di], 0x0A0D
    add di, 2
    ret

; w16pf_nl - AX = where text will be inserted, DI = where to compose: a CR LF
; first when the text before AX is a line with no ending (the file's last)
w16pf_nl:
    or ax, ax
    jz .out
    mov si, ax
    cmp byte [es:si - 1], 10
    je .out
    mov word [di], 0x0A0D
    add di, 2
.out:
    ret

; w16pf_cat - DS:SI = a NUL-ended string, appended at DS:DI (DI moves).
; Keeps ES
w16pf_cat:
    push es
    push ds
    pop es
.c:
    lodsb
    or al, al
    jz .out
    stosb
    jmp short .c
.out:
    pop es
    ret

; -----------------------------------------------------------------------------
; Choosing the file
; -----------------------------------------------------------------------------

; w16pf_win - WIN.INI's name into [w16pf_nm]. CF=0, ES:BX kept
w16pf_win:
    push es
    push bx
    mov ax, w16pf_s_win
    mov dx, ds
    call w16pf_name
    pop bx
    pop es
    ret

; w16pf_priv - ES:BX = a frame with lpFile at +0: its name into [w16pf_nm],
; and BX += 4. CF=1: not a name WIN16 can keep (SPEC.md 124.14)
w16pf_priv:
    push es
    push bx
    les ax, [es:bx]
    mov dx, es
    call w16pf_name
    pop bx
    pop es
    lea bx, [bx + 4]                ; (lea: CF is the name's answer)
    ret

; w16pf_name - DX:AX = a file name: its last component, upper case, into
; [w16pf_nm]. CF=1: NULL, or not a legal 8.3 name
w16pf_name:
    mov di, w16pf_val               ; (a scratch string here)
    call w16pf_cpy
    jc .out
    mov si, w16pf_val               ; SI = after the last '\' or ':'
    mov bx, si
.last:
    lodsb
    or al, al
    jz .got
    cmp al, '\'
    je .cut
    cmp al, ':'
    je .cut
    cmp al, '/'
    jne .last
.cut:
    mov bx, si
    jmp short .last
.got:
    mov di, w16pf_nm                ; zeroed whole: rows compare all 13 bytes
    mov cx, 13
.z:
    mov byte [di], 0
    inc di
    loop .z
    mov si, bx
    mov di, w16pf_nm
    xor cx, cx                      ; CL = the base's length, CH = 1 after '.'
    xor dx, dx                      ; DL = the extension's
.ch:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    je .dot
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 'a' - 'A'
.up:
    push di
    push cx
    mov di, w16pf_bad
    mov cx, w16pf_badn
    push es
    push ds
    pop es
    repne scasb
    pop es
    pop cx
    pop di
    je .bad
    cmp al, ' '
    jbe .bad
    or ch, ch
    jnz .ext
    inc cl
    cmp cl, 8
    ja .bad
    jmp short .st
.ext:
    inc dl
    cmp dl, 3
    ja .bad
.st:
    mov [di], al
    inc di
    jmp short .ch
.dot:
    or ch, ch
    jnz .bad                        ; a second '.'
    or cl, cl
    jz .bad                         ; no base
    inc ch
    mov [di], al
    inc di
    jmp short .ch
.end:
    or cl, cl
    jz .bad
    or ch, ch
    jz .ok
    or dl, dl
    jz .bad                         ; 'NAME.' with nothing after
.ok:
    mov byte [di], 0
    clc
    ret
.bad:
    stc
.out:
    ret

w16pf_bad:  db '"*+,/:;<=>?[\]|'
w16pf_badn  equ $ - w16pf_bad

; w16pf_cpy - DX:AX = a far string (0:0 is NULL): copied to DS:DI, cut to
; 255 characters. CF=1 it was NULL (DI gets ""). Keeps BX, ES
w16pf_cpy:
    push si
    push cx
    mov cx, ax
    or cx, dx
    jnz .go
    mov byte [di], 0
    stc
    jmp short .out
.go:
    mov [w16pf_cs], di
    push ds
    push es
    mov si, ax
    push ds
    pop es
    mov ds, dx
    mov cx, W16PF_STR - 1
.c:
    lodsb
    or al, al
    jz .z
    stosb
    loop .c
.z:
    xor al, al
    stosb
    pop es
    pop ds
    call w16pf_strim                ; blanks at either end are not part of it
    clc
.out:
    pop cx
    pop si
    ret

; w16pf_strim - the NUL-ended string that ends at DS:DI - 1 (w16pf_cpy's
; copy, whose start is [w16pf_cs]): blanks off both ends, in place
w16pf_strim:
    push ax
    push si
    push di
    dec di                          ; DI = its NUL
.r:
    cmp di, [w16pf_cs]
    jbe .l
    mov al, [di - 1]
    cmp al, ' '
    je .rc
    cmp al, 9
    jne .l
.rc:
    dec di
    mov byte [di], 0
    jmp short .r
.l:
    mov si, [w16pf_cs]              ; leading blanks: the rest moved down
.lb:
    mov al, [si]
    cmp al, ' '
    je .li
    cmp al, 9
    jne .ld
.li:
    inc si
    jmp short .lb
.ld:
    mov di, [w16pf_cs]
    cmp si, di
    je .out
.lm:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .lm
.out:
    pop di
    pop si
    pop ax
    ret

; w16pf_unq - a value in double quotes, [w16pf_vs]..[w16pf_ve], loses them
w16pf_unq:
    mov si, [w16pf_vs]
    mov di, [w16pf_ve]
    mov ax, di
    sub ax, si
    cmp ax, 2
    jb .out
    cmp byte [es:si], '"'
    jne .out
    cmp byte [es:di - 1], '"'
    jne .out
    inc word [w16pf_vs]
    dec word [w16pf_ve]
.out:
    ret

; -----------------------------------------------------------------------------
; The held files
; -----------------------------------------------------------------------------

; w16pf_open - the file named in [w16pf_nm], held: ES = its claim,
; [w16pf_row] = its row, [w16pf_end] = its length. CF=1: no memory for it
w16pf_open:
    inc byte [w16pf_clock]
    mov bx, w16pf_tab               ; held already?
    mov cx, W16PF_N
.f:
    cmp byte [bx + WPF_NAME], 0
    je .fn
    mov si, w16pf_nm
    lea di, [bx + WPF_NAME]
    push es
    push ds
    pop es
    push cx
    mov cx, 13
    repe cmpsb
    pop cx
    pop es
    je .have
.fn:
    add bx, W16PF_SZ
    loop .f
    mov bx, w16pf_tab               ; a free row, or the least recently used
    mov dx, bx
    mov cx, W16PF_N
    mov al, 0xFF
.r:
    cmp byte [bx + WPF_NAME], 0
    je .take
    mov ah, [w16pf_clock]
    sub ah, [bx + WPF_LRU]          ; the age, modulo 256
    not ah
    cmp ah, al
    jae .rn
    mov al, ah
    mov dx, bx
.rn:
    add bx, W16PF_SZ
    loop .r
    mov bx, dx
    or byte [bx + WPF_FL], WPFF_LOGX    ; not merged: the merge's replay
    call w16pf_drop                 ;   would spend the call in hand's
                                    ;   section, key and value. Written if it
                                    ;   changed, and let go
.take:
    mov [w16pf_row], bx
    mov ax, 2                       ; 2KB, until the file says otherwise
    call w16pf_claim
    jc .out
    mov si, w16pf_nm
    lea di, [bx + WPF_NAME]
    push es
    push ds
    pop es
    mov cx, 13
    rep movsb
    pop es
    mov byte [bx + WPF_FL], 0
    xor ax, ax
    mov [bx + WPF_LEN], ax
    mov [bx + WPF_LOG], ax
    mov [bx + WPF_LLEN], ax
    call w16pf_in                   ; SYSTEM\APPDATA\WIN16
    jnc .read
    or byte [bx + WPF_FL], WPFF_MEM ; no folder: memory only
    jmp short .have
.read:
    call w16pf_rd
    jnc .rdone
    cmp ax, FERR_BIG                ; DX = the KB it needs
    jne .rfail
    cmp dx, (W16PF_MAX / 1024)
    ja .keep
    push dx
    mov dx, [bx + WPF_SEG]
    call OSAPI_MEM_FREE
    pop ax
    add ax, 2
    call w16pf_claim
    jc .gone
    call w16pf_rd
    jnc .rdone
.rfail:
    cmp ax, FERR_NOENT              ; none yet: a new, empty file
    je .back
.keep:
    or byte [bx + WPF_FL], WPFF_KEEP    ; unreadable or too large: as empty,
    jmp short .back                 ;   and never written over
.rdone:
    or dx, dx
    jnz .keep
    cmp ax, W16PF_MAX
    ja .keep
    mov [bx + WPF_LEN], ax
.back:
    call w16pf_out
.have:
    mov [w16pf_row], bx
    mov al, [w16pf_clock]
    mov [bx + WPF_LRU], al
    mov es, [bx + WPF_SEG]
    mov ax, [bx + WPF_LEN]
    mov [w16pf_end], ax
    clc
.out:
    ret
.gone:
    call w16pf_out                  ; the claim went: the row is free again
    mov byte [bx + WPF_NAME], 0
    stc
    ret

; w16pf_claim - BX = a row, AX = KB: its claim. CF=1 refused (the row free)
w16pf_claim:
    push ax
    call OSAPI_MEM_CLAIM
    pop ax
    jc .no
    mov [bx + WPF_SEG], dx
    mov cl, 10
    shl ax, cl
    jnz .cap
    dec ax                          ; 64KB: never asked for, but not 0
.cap:
    mov [bx + WPF_CAP], ax
    clc
    ret
.no:
    mov byte [bx + WPF_NAME], 0
    ret

; w16pf_rd - BX = a row: OSAPI_FILE_READ into its claim, as OSAPI_FILE_READ
; answers. Keeps BX
w16pf_rd:
    push bx
    lea si, [bx + WPF_NAME]
    mov cx, [bx + WPF_CAP]
    mov es, [bx + WPF_SEG]
    xor bx, bx
    xor dx, dx
    call OSAPI_FILE_READ
    pop bx
    ret

; w16pf_in / w16pf_out - stand in SYSTEM\APPDATA\WIN16 on WIN16's volume,
; and back. CF=1 from w16pf_in: there is no such folder (and nothing moved).
; Keep BX
w16pf_in:
    push bx
    call od_enter
    jc .out
    push si
    mov si, w16pf_s_dir
    call od_dive
    pop si
    jnc .out
    call od_back
    stc
.out:
    pop bx
    ret
w16pf_out:
    jmp od_back

; w16pf_flush - BX = a held row: written if it changed, merged first with
; what is on the disk now (w16pf_merge). Keeps BX
w16pf_flush:
    test byte [bx + WPF_FL], WPFF_DIRTY
    jz .out
    test byte [bx + WPF_FL], WPFF_MEM | WPFF_KEEP
    jnz .out
    call w16pf_merge
    call w16pf_in
    jc .out
    push bx
    lea si, [bx + WPF_NAME]
    mov cx, [bx + WPF_LEN]
    mov es, [bx + WPF_SEG]
    xor bx, bx
    xor dx, dx
    call OSAPI_FILE_WRITE
    pop bx
    call w16pf_out
    jc .out                         ; refused: kept, and tried again later
    and byte [bx + WPF_FL], ~WPFF_DIRTY
.out:
    ret

; w16pf_drop - BX = a held row: flushed, its claims freed, the row free
w16pf_drop:
    call w16pf_flush
    mov dx, [bx + WPF_SEG]
    call OSAPI_MEM_FREE
    call w16pf_logfree
    mov byte [bx + WPF_NAME], 0
    ret

; w16pf_logfree - BX = a row: its log let go. Keeps BX
w16pf_logfree:
    mov dx, [bx + WPF_LOG]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    xor dx, dx
    mov [bx + WPF_LOG], dx
    mov [bx + WPF_LLEN], dx
.out:
    ret

; -----------------------------------------------------------------------------
; THE MERGE (SPEC.md 124.18.2). Each WIN16 holds its own copy of a file, so
; two programs that write WIN.INI would each write back what they read, and
; the second to end would undo the first. So every write is also kept in the
; row's log - a flags byte (bit 0 no key, bit 1 no value), then the section,
; key and value, each NUL-ended - and a flush first reads the file again from
; the disk and plays the log onto that. The last write of a key wins,
; whichever program made it. A log that would pass W16PF_LOGKB stops (and
; WPFF_LOGX writes the file as held): a program that writes more than that
; loses the merge for that file alone, and so does a fifth file's eviction
; (w16pf_open), which happens in the middle of another call.
; -----------------------------------------------------------------------------

; w16pf_log - [w16pf_row]'s log gains the write just made. Keeps AX
w16pf_log:
    push ax
    mov bx, [w16pf_row]
    test byte [bx + WPF_FL], WPFF_LOGX
    jnz .out
    mov ax, 1                       ; the entry's size: the flags byte ...
    mov si, w16pf_app
    call w16pf_slen
    mov si, w16pf_key
    call w16pf_slen
    mov si, w16pf_val
    call w16pf_slen
    mov cx, ax
    add ax, [bx + WPF_LLEN]
    cmp ax, W16PF_LOGKB * 1024
    ja .over
    mov dx, [bx + WPF_LOG]
    or dx, dx
    jnz .have
    mov ax, W16PF_LOGKB
    call OSAPI_MEM_CLAIM
    jc .over
    mov [bx + WPF_LOG], dx
.have:
    mov es, dx
    mov di, [bx + WPF_LLEN]
    add [bx + WPF_LLEN], cx
    mov al, [w16pf_noval]
    shl al, 1
    or al, [w16pf_nokey]
    stosb
    mov si, w16pf_app
    call w16pf_scpy
    mov si, w16pf_key
    call w16pf_scpy
    mov si, w16pf_val
    call w16pf_scpy
    jmp short .out
.over:
    or byte [bx + WPF_FL], WPFF_LOGX
.out:
    pop ax
    ret

; w16pf_slen - AX += the NUL-ended string at DS:SI's length, its NUL counted
w16pf_slen:
    inc ax
.l:
    cmp byte [si], 0
    je .z
    inc si
    inc ax
    jmp short .l
.z:
    ret

; w16pf_scpy - DS:SI -> ES:DI, its NUL too; DI past it
w16pf_scpy:
    lodsb
    stosb
    or al, al
    jnz w16pf_scpy
    ret

; w16pf_merge - BX = a changed row with a log: the file read again from the
; disk into a claim of its own, the log played onto it, and that held in
; place of the row's copy. Anything that goes wrong leaves the row as it was,
; to be written as held. Keeps BX
w16pf_merge:
    cmp word [bx + WPF_LLEN], 0
    je .out
    test byte [bx + WPF_FL], WPFF_LOGX
    jnz .out
    call w16pf_in
    jc .out
    mov ax, [bx + WPF_CAP]          ; a claim the copy's size, grown once if
    mov cl, 10                      ;   the file on the disk is larger now
    shr ax, cl
.try:
    mov [w16pf_kb], ax
    call OSAPI_MEM_CLAIM
    jc .back
    push bx
    lea si, [bx + WPF_NAME]
    mov cx, [w16pf_kb]
    mov bx, cx
    mov cl, 10
    shl bx, cl
    mov cx, bx
    jnz .cap
    dec cx
.cap:
    mov es, dx
    xor bx, bx
    push dx
    xor dx, dx
    call OSAPI_FILE_READ            ; DX:AX = its length
    pop di                          ; DI = the claim
    pop bx
    jnc .read
    push ax
    push dx
    mov dx, di
    call OSAPI_MEM_FREE
    pop dx
    pop ax
    cmp ax, FERR_BIG                ; DX = the KB it needs
    jne .gone
    cmp word [w16pf_kb], W16PF_MAX / 1024 + 2
    jae .back
    cmp dx, W16PF_MAX / 1024
    ja .back
    mov ax, dx
    add ax, 2
    jmp short .try
.gone:
    cmp ax, FERR_NOENT              ; deleted since: an empty file, merged
    jne .back
    mov ax, 1
    call OSAPI_MEM_CLAIM
    jc .back
    mov di, dx
    mov word [w16pf_kb], 1
    xor ax, ax
    xor dx, dx
.read:
    or dx, dx
    jnz .drop
    cmp ax, W16PF_MAX
    ja .drop
    push ax
    mov dx, [bx + WPF_SEG]          ; the fresh copy is the row's now
    call OSAPI_MEM_FREE
    mov [bx + WPF_SEG], di
    pop word [bx + WPF_LEN]
    mov ax, [w16pf_kb]
    mov cl, 10
    shl ax, cl
    jnz .cap2
    dec ax
.cap2:
    mov [bx + WPF_CAP], ax
    call w16pf_out
    call w16pf_replay
    jmp short .out
.drop:
    mov dx, di
    call OSAPI_MEM_FREE
.back:
    call w16pf_out
.out:
    ret

; w16pf_replay - BX = a row: its log played onto its copy, then emptied.
; Keeps BX
w16pf_replay:
    push bx
    mov si, bx                      ; [w16pf_nm] = its name: w16pf_open finds
    mov di, w16pf_nm                ;   the row by it
    push ds
    pop es
    mov cx, 13
    rep movsb
    pop bx
    push bx
    xor si, si                      ; SI = the next entry
.e:
    pop bx
    push bx
    cmp si, [bx + WPF_LLEN]
    jae .done
    push ds
    mov ds, [bx + WPF_LOG]          ; (DS: the log, until the copies are made)
    lodsb
    pop ds
    mov ah, al
    and al, 1
    mov [w16pf_nokey], al
    shr ah, 1
    mov [w16pf_noval], ah
    mov es, [bx + WPF_LOG]
    mov di, w16pf_app
    call w16pf_lcpy
    mov di, w16pf_key
    call w16pf_lcpy
    mov di, w16pf_val
    call w16pf_lcpy
    push si
    call w16pf_wcore
    pop si
    jmp short .e
.done:
    pop bx
    mov word [bx + WPF_LLEN], 0     ; it is in the copy now
    ret

; w16pf_lcpy - ES:SI (the log) -> DS:DI, its NUL too; SI past it
w16pf_lcpy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz w16pf_lcpy
    ret

; w16pf_flushall - every changed file written, all still held
w16pf_flushall:
    mov bx, w16pf_tab
    mov cx, W16PF_N
.f:
    cmp byte [bx + WPF_NAME], 0
    je .n
    push cx
    call w16pf_flush
    pop cx
.n:
    add bx, W16PF_SZ
    loop .f
    ret

; w16pf_endprog - the program has ended (w16_freeprog): every file written
; and let go
w16pf_endprog:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bx, w16pf_tab
    mov cx, W16PF_N
.f:
    cmp byte [bx + WPF_NAME], 0
    je .n
    push cx
    call w16pf_drop
    pop cx
.n:
    add bx, W16PF_SZ
    loop .f
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16pf_splice - in the held file (ES, [w16pf_row]): CX bytes at AX are
; replaced by DX bytes from DS:SI. CF=1: it would not fit (nothing changed)
w16pf_splice:
    mov bx, [w16pf_row]
    test byte [bx + WPF_FL], WPFF_KEEP
    jnz .no
    mov [w16pf_sp], ax
    mov [w16pf_sd], cx
    mov [w16pf_si], dx
    mov ax, [w16pf_end]             ; AX = the new length
    sub ax, cx
    add ax, dx
    cmp ax, W16PF_MAX
    ja .no
    cmp ax, [bx + WPF_CAP]
    jbe .fits
    push ax                         ; a claim with 2KB to spare, and the
    push si                         ;   text moved into it
    add ax, 2048 + 1023
    mov cl, 10
    shr ax, cl
    mov [w16pf_kb], ax
    call OSAPI_MEM_CLAIM
    jc .full
    push ds
    mov es, dx
    mov cx, [w16pf_end]
    mov ds, [bx + WPF_SEG]
    xor si, si
    xor di, di
    rep movsb
    pop ds
    push dx
    mov dx, [bx + WPF_SEG]
    call OSAPI_MEM_FREE
    pop dx
    mov [bx + WPF_SEG], dx
    mov ax, [w16pf_kb]
    mov cl, 10
    shl ax, cl
    mov [bx + WPF_CAP], ax
    pop si
    pop ax
.fits:
    mov es, [bx + WPF_SEG]
    push ax
    push si
    mov si, [w16pf_sp]              ; the tail: from sp + sd to the end
    add si, [w16pf_sd]
    mov cx, [w16pf_end]
    sub cx, si
    mov di, [w16pf_sp]
    add di, [w16pf_si]
    cmp di, si
    je .moved
    jb .down
    add si, cx                      ; up: from the end, backwards
    dec si
    add di, cx
    dec di
    push ds
    push es
    pop ds
    std
    rep movsb
    cld
    pop ds
    jmp short .moved
.down:
    push ds
    push es
    pop ds
    rep movsb
    pop ds
.moved:
    pop si                          ; the new text
    mov di, [w16pf_sp]
    mov cx, [w16pf_si]
    rep movsb
    pop ax
    mov [w16pf_end], ax
    mov [bx + WPF_LEN], ax
    or byte [bx + WPF_FL], WPFF_DIRTY
    clc
    ret
.full:
    pop si
    pop ax
    mov es, [bx + WPF_SEG]
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; Reading the text: the held file is ES:0 to [w16pf_end]
; -----------------------------------------------------------------------------

; w16pf_eol - SI = a line's start: DI = the end of its text (at CR, LF or the
; file's end), SI = the next line's start
w16pf_eol:
    mov di, si
.l:
    cmp di, [w16pf_end]
    jae .e
    mov al, [es:di]
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    inc di
    jmp short .l
.cr:
    lea si, [di + 1]
    cmp si, [w16pf_end]
    jae .out
    cmp byte [es:si], 10
    jne .out
    inc si
.out:
    ret
.lf:
    lea si, [di + 1]
    ret
.e:
    mov si, di
    ret

; w16pf_trim - BX..DI is a span of the held text: blanks off both ends
w16pf_trim:
.l:
    cmp bx, di
    jae .r
    mov al, [es:bx]
    cmp al, ' '
    je .li
    cmp al, 9
    jne .r
.li:
    inc bx
    jmp short .l
.r:
    cmp di, bx
    jbe .out
    mov al, [es:di - 1]
    cmp al, ' '
    je .ri
    cmp al, 9
    jne .out
.ri:
    dec di
    jmp short .r
.out:
    ret

; w16pf_hdr - SI = a line: CF=0 it is a [section] header, its name
; [w16pf_ks]..[w16pf_ke]; CF=1 it is not. SI = the next line either way
w16pf_hdr:
    mov bx, si
    call w16pf_eol
    call w16pf_trim
    cmp bx, di
    jae .no
    cmp byte [es:bx], '['
    jne .no
    inc bx
    push bx                         ; the name ends at ']' (or the line's end)
.b:
    cmp bx, di
    jae .be
    cmp byte [es:bx], ']'
    je .be
    inc bx
    jmp short .b
.be:
    mov di, bx
    pop bx
    call w16pf_trim
    mov [w16pf_ks], bx
    mov [w16pf_ke], di
    clc
    ret
.no:
    stc
    ret

; w16pf_eq - [w16pf_ks]..[w16pf_ke] against the NUL-ended DS:DI, case
; folded. ZF=1 equal
w16pf_eq:
    push si
    mov si, [w16pf_ks]
.c:
    cmp si, [w16pf_ke]
    jae .end
    mov al, [es:si]
    call w16pf_fold
    mov ah, al
    mov al, [di]
    call w16pf_fold
    cmp al, ah
    jne .out
    inc si
    inc di
    jmp short .c
.end:
    cmp byte [di], 0
.out:
    pop si
    ret

w16pf_fold:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; w16pf_sect - find [w16pf_app]'s section. CF=0: [w16pf_hdrp] its header
; line, [w16pf_body] the line after, [w16pf_send] the next header line (or
; the end), [w16pf_last] where a new key goes. CF=1: none
w16pf_sect:
    xor si, si
.s:
    cmp si, [w16pf_end]
    jae .no
    mov [w16pf_hdrp], si
    call w16pf_hdr
    jc .s
    mov di, w16pf_app
    call w16pf_eq
    jne .s
    mov [w16pf_body], si
    mov [w16pf_last], si
.b:
    cmp si, [w16pf_end]             ; to the next header, noting the last
    jae .bend                       ;   line with text
    mov [w16pf_send], si
    push si
    call w16pf_hdr
    pop dx
    jnc .found                      ; (CF=0: a header - [w16pf_send] is it)
    mov si, dx
    mov bx, si
    call w16pf_eol
    call w16pf_trim
    cmp bx, di
    je .b
    mov [w16pf_last], si
    jmp short .b
.bend:
    mov [w16pf_send], si
.found:
    clc
    ret
.no:
    stc
    ret

; w16pf_keyline - SI = a line in the section: the next key line from there.
; CF=0: [w16pf_kln] its start, [w16pf_ks]..[w16pf_ke] the key,
; [w16pf_kend] the key's end, [w16pf_vs]..[w16pf_ve] the value, [w16pf_lend]
; the text's end, SI = [w16pf_lnx] the next line. CF=1: the section's end
w16pf_keyline:
.l:
    cmp si, [w16pf_send]
    jae .no
    mov [w16pf_kln], si
    mov bx, si
    call w16pf_eol
    mov [w16pf_lnx], si
    mov [w16pf_lend], di
    call w16pf_trim
    cmp bx, di
    je .l                           ; blank
    cmp byte [es:bx], ';'
    je .l                           ; a comment
    mov [w16pf_ks], bx
    mov dx, di                      ; the text's trimmed end
    push bx
.e:
    cmp bx, dx
    jae .noeq
    cmp byte [es:bx], '='
    je .eq
    inc bx
    jmp short .e
.noeq:
    mov di, dx                      ; no '=': all key, an empty value
    pop bx
    call w16pf_trim
    mov [w16pf_ke], di
    mov [w16pf_kend], di
    mov [w16pf_vs], dx
    mov [w16pf_ve], dx
    clc
    ret
.eq:
    mov di, bx                      ; the key: to the '='
    lea ax, [bx + 1]
    pop bx
    push ax
    call w16pf_trim
    mov [w16pf_ke], di
    mov [w16pf_kend], di
    pop bx                          ; the value: after it
    mov di, dx
    call w16pf_trim
    mov [w16pf_vs], bx
    mov [w16pf_ve], di
    clc
    ret
.no:
    stc
    ret

; w16pf_findkey - [w16pf_key] in the section found. CF=0: w16pf_keyline's
; answers for it; CF=1: not there
w16pf_findkey:
    mov si, [w16pf_body]
.k:
    call w16pf_keyline
    jc .out
    mov di, w16pf_key
    call w16pf_eq
    jne .k
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; Answering into the program's buffer: [w16pf_dst] (far), [w16pf_dn] bytes
; -----------------------------------------------------------------------------

; w16pf_pbeg - nothing written yet
w16pf_pbeg:
    mov word [w16pf_cnt], 0
    mov byte [w16pf_cut], 0
    ret

; w16pf_put1 - AL to the buffer, one string: CF=1 when nSize - 1 are written
w16pf_put1:
    mov dx, [w16pf_dn]
    dec dx
    jmp short w16pf_putl

; w16pf_putm - AL to the buffer, a list: at most nSize - 2, then cut
w16pf_putm:
    cmp byte [w16pf_cut], 0
    jne .out
    mov dx, [w16pf_dn]
    sub dx, 2
    call w16pf_putl
    jnc .out
    mov byte [w16pf_cut], 1
.out:
    ret

; w16pf_putl - AL to the buffer if [w16pf_cnt] < DX (signed). CF=1 it was not
w16pf_putl:
    cmp [w16pf_cnt], dx
    jge .full
    push es
    push di
    les di, [w16pf_dst]
    add di, [w16pf_cnt]
    mov [es:di], al
    pop di
    pop es
    inc word [w16pf_cnt]
    clc
    ret
.full:
    stc
    ret

; w16pf_pone - end one string: its NUL. AX = its length
w16pf_pone:
    mov ax, [w16pf_cnt]
    cmp word [w16pf_dn], 0
    je .out
    push es
    push di
    les di, [w16pf_dst]
    add di, ax
    mov byte [es:di], 0
    pop di
    pop es
.out:
    ret

; w16pf_pmany - end a list: its second NUL (cut: nSize - 2 and two NULs).
; AX = its length
w16pf_pmany:
    mov ax, [w16pf_dn]
    cmp ax, 2
    jb .tiny
    cmp byte [w16pf_cut], 0
    jne .cut
    mov ax, [w16pf_cnt]
    les di, [w16pf_dst]
    add di, ax
    mov byte [es:di], 0
    or ax, ax                       ; an empty list is "" "": two NULs
    jnz .out
    mov byte [es:di + 1], 0
.out:
    ret
.cut:
    sub ax, 2
    les di, [w16pf_dst]
    add di, ax
    mov word [es:di], 0
    ret
.tiny:
    or ax, ax
    jz .out
    les di, [w16pf_dst]
    mov byte [es:di], 0
    xor ax, ax
    ret
