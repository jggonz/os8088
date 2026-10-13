; =============================================================================
; os88dia.inc - a DESKTOP INTEGRATED APPLICATION's shared half (SPEC.md 107)
;
; A DIA - a widget - is an ordinary package in SYSTEM\DIA\ on the boot volume
; that includes this file. It owns three things for it:
;
;   its STATE FILE  SYSTEM\APPDATA\<DIA_DATNAME> on its own volume (SPEC.md
;                   19.9): a 16-byte DIA header - where its window was, on
;                   what screen - then the application's payload (107.5)
;   its PRESENCE    one DIA ROW in the desktop configuration, the SYSTEM.CFG
;                   trailer (107.2), registered on the first timer tick and
;                   removed BY NAME when the window closes - which is what the
;                   kernel's dia_boot relaunches from at the next boot (107.4)
;   its TICK        a 36-tick timer that polls the window's place, saves once
;                   a drag has settled, and calls the application's own tick
;
; THE CALLER DEFINES, before the %include:
;
;   %define DIA_DATNAME 'DESKCAL.DAT'   the state file's 8.3 name
;   %define DIA_CAP     'Calendar'      the row's caption (<= 12 chars) - what
;                                       a downgraded row's icon says (107.4)
;   DIA_PAYLEN  equ n                   the payload's EXACT length
;   DIA_PAYVER  equ v                   ...and its version
;   DIA_ICON    equ 32                  the 64-byte icon body in the image
;                                       (+32 with OS88_F_ICON)
;   DIA_BSS     equ k                   where in the package's bss the state
;                                       below starts: a MULTIPLE OF 16, and
;                                       the package's own image must END on a
;                                       paragraph (`align 16` before
;                                       OS88_IMAGE_END) - the read lands at
;                                       offset 0 of a segment (OSAPI_FILE_READ)
;   %define DIA_ONTICK  proc            optional: a near proc, every tick
;   %define DIA_ONCLOSE proc            optional: before the row is removed
;
; and gets DIA_PAY (the payload, DIA_PAYLEN bytes, zero when there was no
; valid file) and DIA_BSS_END (the next free bss offset).
;
; ITS CALLS, at the package's own points:
;
;   dia_load   the ENTRY PROC, before OSAPI_WM_CREATE: SI = the loader's name
;              (KERNEL_SEG:SI), DI = your window template. Reads the state
;              file and places the template. CF = 0.
;   dia_made   after OSAPI_WM_CREATE, BX = the window: installs the timer
;              and the close negotiator and arms the first tick.
;
; WAVE 1 (SPEC.md 107.10) SHIPS THE SUBSET tests/diaprobe needs: the state
; file, the registration, the poll and save, the remove by name. Wave 2 adds
; the singleton, the anchoring of a position saved on another screen, the
; damaged-file question and kern_small's worker arm; until then a position
; saved on a screen of another size leaves the template where it was.
; =============================================================================

%ifndef OS88DIA_INC
%define OS88DIA_INC

DIA_HDR_MAGIC   equ 0x4144      ; 'DA' (SPEC.md 107.5)
DIA_HDR_VER     equ 1
DIA_HF_POS      equ 1           ; +3 bit 0: the position is valid
DIA_H_MAGIC     equ 0
DIA_H_VER       equ 2
DIA_H_FLAGS     equ 3
DIA_H_X         equ 4
DIA_H_Y         equ 6
DIA_H_SW        equ 8
DIA_H_SH        equ 10
DIA_H_PAYVER    equ 12
DIA_H_LEN       equ 14
DIA_HDR_SIZE    equ 16
DIA_TICKS       equ 36          ; ~2 s between polls
DIA_SLACK       equ 80          ; a compressed file is read high and expanded
                                ; down (OSAPI_FILE_READ): the bytes past the
                                ; payload it needs are dia_rec's, scratch then

; dia_st bits
DIA_S_TRIED     equ 1           ; the first tick has asked for the row
DIA_S_ROW       equ 2           ; ...and was granted it
DIA_S_NOHOME    equ 4           ; no SYSTEM\APPDATA: the state stays in memory
DIA_S_DAMAGED   equ 8           ; the file was there and was not ours
DIA_S_DIRTY     equ 16          ; owed a save

%if DIA_BSS % 16
  %error "os88dia: DIA_BSS must be a multiple of 16 - the read wants a paragraph"
%endif

dia_file    equ os88_image_end + DIA_BSS                    ; header + payload
DIA_PAY     equ dia_file + DIA_HDR_SIZE
dia_rec     equ dia_file + DIA_HDR_SIZE + DIA_PAYLEN        ; 128: the row
dia_name    equ dia_rec + OSAPI_DI_SIZE                     ; 13: our 8.3 name
dia_hdx     equ dia_name + 13                               ; the banked folder
dia_hbl     equ dia_hdx + 2                                 ; ...and its drive
dia_st      equ dia_hbl + 1
dia_win     equ dia_st + 1
dia_px      equ dia_win + 2                                 ; the last poll
dia_py      equ dia_px + 2
DIA_BSS_END equ DIA_BSS + DIA_HDR_SIZE + DIA_PAYLEN + OSAPI_DI_SIZE + 13 + 2 + 1 + 1 + 2 + 2 + 2
%if OSAPI_DI_SIZE < DIA_SLACK || OSAPI_FIND_SZ > OSAPI_DI_SIZE
  %error "os88dia: dia_rec is the read's slack and the walk's find buffer"
%endif

; -----------------------------------------------------------------------------
; dia_load - the entry proc's half (SPEC.md 107.5)
; in:  SI = the loader's name (an offset in KERNEL_SEG), DI = the template
; out: CF = 0; the template's x,y the saved ones when the file was valid and
;      saved on a screen of this size. Every register but the flags kept.
; -----------------------------------------------------------------------------
dia_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push di
    push ds                     ; the NAME, out of KERNEL_SEG: copy it, the
    push ds                     ; loader reuses the buffer (SPEC.md 20.2)
    mov ax, KERNEL_SEG
    mov ds, ax
    pop es
    mov di, dia_name
    mov cx, 12
.nm:
    lodsb
    stosb
    or al, al
    loopnz .nm
    mov byte [es:di], 0
    pop ds
    call dia_enter              ; standing in SYSTEM\APPDATA, or CF = 1
    jc .none
    push ds
    pop es
    mov bx, dia_file
    test bl, 15
    jnz .home                   ; a misplaced DIA_BSS: no home, said nowhere
    mov si, dia_dname
    mov cx, DIA_HDR_SIZE + DIA_PAYLEN + DIA_SLACK
    xor dx, dx
    call OSAPI_FILE_READ        ; DX:AX = its size
    jnc .got
    cmp ax, FERR_NOENT
    je .home                    ; MISSING: the defaults, and the first save
    jmp short .bad              ; makes it
.got:
    or dx, dx
    jnz .bad
    cmp ax, DIA_HDR_SIZE + DIA_PAYLEN
    jne .bad                    ; the EXACT size, or it is not ours
    cmp word [dia_file+DIA_H_MAGIC], DIA_HDR_MAGIC
    jne .bad
    cmp byte [dia_file+DIA_H_VER], DIA_HDR_VER
    jne .bad
    cmp byte [dia_file+DIA_H_PAYVER], DIA_PAYVER
    jne .bad
    cmp word [dia_file+DIA_H_LEN], DIA_PAYLEN
    jne .bad
    test byte [dia_file+DIA_H_FLAGS], DIA_HF_POS
    jz .home
    call OSAPI_VIDEO            ; AX = width, BX = height
    cmp ax, [dia_file+DIA_H_SW]
    jne .home                   ; another screen: the template stands (wave 2
    cmp bx, [dia_file+DIA_H_SH] ; anchors it instead)
    jne .home
    pop di                      ; the template
    push di
    mov ax, [dia_file+DIA_H_X]
    mov [di+WT_X], ax
    mov ax, [dia_file+DIA_H_Y]
    mov [di+WT_Y], ax           ; wm_create clamps it onto the glass (39.7)
    jmp short .home
.bad:
    or byte [dia_st], DIA_S_DAMAGED
    push ds                     ; DAMAGED: run empty, and never overwrite it
    pop es                      ; silently (no save until wave 2 asks)
    mov di, dia_file
    mov cx, DIA_HDR_SIZE + DIA_PAYLEN
    xor al, al
    cld
    rep stosb
.home:
    call dia_leave
    jmp short .out
.none:
    or byte [dia_st], DIA_S_NOHOME
.out:
    pop di
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dia_made - after OSAPI_WM_CREATE: BX = the window. Installs the tick and the
; close negotiator; every register kept, the flags not - clear CF yourself
; before the entry proc returns
; -----------------------------------------------------------------------------
dia_made:
    push ax
    mov [dia_win], bx
    mov ax, dia_tick
    call OSAPI_WM_ONTIMER
    mov ax, dia_close
    call OSAPI_WM_ONCLOSE
    mov ax, 1                   ; the first tick: the window is up by then
    call OSAPI_WM_TIMER         ; CF = 1 on kern_small - wave 2's worker arm
    pop ax
    ret

; -----------------------------------------------------------------------------
; dia_tick - the timer (SI = the window; the UI task, the gfx lock held)
; -----------------------------------------------------------------------------
dia_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    test byte [dia_st], DIA_S_TRIED
    jnz .poll
    or byte [dia_st], DIA_S_TRIED
    test byte [dia_st], DIA_S_NOHOME | DIA_S_DAMAGED
    jnz .poll                   ; no state to come back to: no row either
    call dia_register
.poll:
    mov ax, KERNEL_SEG          ; ES = KERNEL_SEG on entry to a callback -
    mov es, ax                  ; and the registration's ES = DS since
    mov si, [dia_win]
    mov ax, [es:si+W_X]
    mov dx, [es:si+W_Y]
    cmp ax, [dia_px]
    jne .moved                  ; still moving, or just started
    cmp dx, [dia_py]
    jne .moved
    test byte [dia_file+DIA_H_FLAGS], DIA_HF_POS
    jz .new                     ; settled, and never saved: save it
    cmp ax, [dia_file+DIA_H_X]
    jne .new
    cmp dx, [dia_file+DIA_H_Y]
    je .app                     ; settled where it was saved: nothing owed
.new:
    call dia_pos                ; two polls agree: the drag is over
.moved:
    mov [dia_px], ax
    mov [dia_py], dx
.app:
%ifdef DIA_ONTICK
    call DIA_ONTICK
%endif
    test byte [dia_st], DIA_S_DIRTY
    jz .arm
    call dia_save
.arm:
    mov bx, [dia_win]
    mov ax, DIA_TICKS
    call OSAPI_WM_TIMER
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_pos - AX, DX = the frame's place now: into the header with the screen
; it is on, and owed a save. Keeps every register
dia_pos:
    push ax
    push bx
    push cx
    push dx
    mov [dia_file+DIA_H_X], ax
    mov [dia_file+DIA_H_Y], dx
    call OSAPI_VIDEO
    mov [dia_file+DIA_H_SW], ax
    mov [dia_file+DIA_H_SH], bx
    or byte [dia_file+DIA_H_FLAGS], DIA_HF_POS
    or byte [dia_st], DIA_S_DIRTY
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_register - the DIA row (SPEC.md 107.2): our volume, KIND 1, the mark,
; the path we were launched from and our name. A refusal - no system disk in
; A:, a full table, kern_small - is CTRL.DRV's to say (or nobody's): the
; widget runs on, unremembered
dia_register:
    push ds
    pop es
    cld
    mov di, dia_rec
    mov al, [dia_hbl]
    stosb                       ; +0 the volume, OSAPI_FILE_HERE's BL
    mov ax, 0xFF | (1 << 8)
    stosw                       ; +1 no cell, +2 a package
    mov cx, OSAPI_DI_CAP - OSAPI_DI_DIA ; +3 on: the path is written ONE
    call OSAPI_FILE_PATH        ; EARLY, so its leading `\` lands on +3 -
    jc .no                      ; sc_m_new's own idiom - and the mark goes
    mov byte [dia_rec+OSAPI_DI_DIA], OSAPI_DIA_MARK     ; over it. CX = its
                                ; length; FERR_BIG = longer than a row keeps
    add di, cx                  ; DI = its NUL
    cmp cx, 1
    je .root                    ; `\` alone is the root: the name, bare
    mov al, '\'
    stosb
    inc cx
.root:
    mov si, dia_name
.cp:
    lodsb
    stosb
    inc cx
    or al, al
    jnz .cp
    cmp cx, OSAPI_DI_CAP - OSAPI_DI_DIA ; the path's 45 bytes, its NUL in
    ja .no                      ; them: a longer one is not remembered
    mov di, dia_rec + OSAPI_DI_CAP
    mov si, dia_cap
    mov cx, OSAPI_DI_BODY - OSAPI_DI_CAP
    rep movsb                   ; the caption (and the header the kernel
    mov si, DIA_ICON            ; stamps), then the picture
    mov cx, 32
    rep movsw
    mov si, dia_rec
    mov al, 1
    call OSAPI_DESK_ITEM
    jc .no
    or byte [dia_st], DIA_S_ROW
.no:
    ret

; -----------------------------------------------------------------------------
; dia_close - the close negotiator (SI = the window; UI task, lock held). The
; application's own close, the row away BY NAME, the last place saved; and the
; close goes ahead (CF = 0). Restart and power-off do not come here (SPEC.md
; 107.4): the row stays, which is what restore wants
; -----------------------------------------------------------------------------
dia_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
%ifdef DIA_ONCLOSE
    call DIA_ONCLOSE
%endif
    test byte [dia_st], DIA_S_ROW
    jz .pos
    push ds
    pop es
    mov si, dia_rec
    mov ax, 0xFF00              ; AL = 0 remove, AH = 0xFF: by NAME
    call OSAPI_DESK_ITEM
    mov ax, KERNEL_SEG          ; the window record again
    mov es, ax
.pos:
    mov si, [dia_win]
    mov ax, [es:si+W_X]
    mov dx, [es:si+W_Y]
    test byte [dia_file+DIA_H_FLAGS], DIA_HF_POS
    jz .p
    cmp ax, [dia_file+DIA_H_X]
    jne .p
    cmp dx, [dia_file+DIA_H_Y]
    je .s
.p:
    call dia_pos
.s:
    test byte [dia_st], DIA_S_DIRTY
    jz .out
    call dia_save
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; dia_save - the state file, header and payload in one write. A refusal (the
; disk swapped, full) keeps it owed for the next tick, silently. Not over a
; DAMAGED file, not with no home. Keeps every register
dia_save:
    test byte [dia_st], DIA_S_NOHOME | DIA_S_DAMAGED
    jnz .r
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call dia_enter
    jc .x
    mov word [dia_file+DIA_H_MAGIC], DIA_HDR_MAGIC
    mov byte [dia_file+DIA_H_VER], DIA_HDR_VER
    mov byte [dia_file+DIA_H_PAYVER], DIA_PAYVER
    mov byte [dia_file+DIA_H_PAYVER+1], 0
    mov word [dia_file+DIA_H_LEN], DIA_PAYLEN
    push ds
    pop es
    mov bx, dia_file
    mov si, dia_dname
    mov cx, DIA_HDR_SIZE + DIA_PAYLEN
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .l
    and byte [dia_st], ~DIA_S_DIRTY & 0xFF
.l:
    call dia_leave
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; dia_enter - bank where we stand and walk to SYSTEM\APPDATA on OUR volume
; (SPEC.md 19.9: bank, go, act, go back - OSAPI_FILE_GOTO, never the quiet
; twin). out CF = 1 there is none, and we are back where we were
dia_enter:
    push ax
    push bx
    push dx
    push si
    call OSAPI_FILE_HERE        ; DX = our folder, BL = our volume
    mov [dia_hdx], dx
    mov [dia_hbl], bl
    xor dx, dx
    call OSAPI_FILE_GOTO        ; that volume's root
    jc .no
    mov si, dia_dsys
    call dia_dive
    jc .no
    mov si, dia_dapp
    call dia_dive
    jc .no
    pop si
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    call dia_leave
    pop si
    pop dx
    pop bx
    pop ax
    stc
    ret

; dia_leave - back to the banked folder; every register and the flags kept
dia_leave:
    pushf
    push bx
    push dx
    mov dx, [dia_hdx]
    mov bl, [dia_hbl]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    popf
    ret

; dia_dive - into the folder named at SI, in the current one; CF = 1 none.
; dia_rec is the find buffer (it is built only at the first tick)
dia_dive:
    push ax
    push bx
    push cx
    push dx
    push di
    xor cx, cx
.each:
    push ds
    pop es
    mov di, dia_rec
    call OSAPI_FILE_FIND
    jc .none
    cmp word [dia_rec + 14], OSAPI_FT_DIR
    jne .each
    push si
    mov di, dia_rec
.c:
    mov al, [si]
    cmp al, [di]
    jne .ne
    inc si
    inc di
    or al, al
    jnz .c
.ne:
    pop si
    jne .each
    mov dx, [dia_rec + 16]      ; its first cluster
    mov bl, [dia_hbl]
    call OSAPI_FILE_GOTO
    jc .none
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.none:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

dia_dname   db DIA_DATNAME, 0
dia_dsys    db 'SYSTEM', 0
dia_dapp    db 'APPDATA', 0
dia_cap     db DIA_CAP, 0
            times OSAPI_DI_BODY - OSAPI_DI_CAP - ($ - dia_cap) db 0
%if ($ - dia_cap) != OSAPI_DI_BODY - OSAPI_DI_CAP
  %error "os88dia: DIA_CAP is at most 12 characters"
%endif

%endif ; OS88DIA_INC
