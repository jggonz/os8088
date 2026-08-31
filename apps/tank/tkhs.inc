; =============================================================================
; os8088 - apps/tank/tkhs.inc
;
; THE HIGH SCORES (SPEC.md 85.9): six rows of a score and three initials, a
; file in SYSTEM\APPDATA, and the three keystrokes that earn a row.
;
; The shape is apps/cyclone's (SPEC.md 67.20) and so are its two hard-won
; rules, which are repeated here because getting either wrong fails QUIETLY:
;
;  - **The file lives in SYSTEM\APPDATA on OUR OWN volume** (SPEC.md 19.9),
;    not beside the game. The file browser is the whole of how a user reaches
;    an application here, so a data file in an app folder is a misclick
;    waiting to happen.
;  - **It is OSAPI_FILE_GOTO and not its quiet twin.** GOTO_Q moves the
;    GLOBAL cwd and deliberately not the instance's, while FILE_FIND, _READ
;    and _WRITE all resolve in the INSTANCE's folder - so a quiet move is
;    undone by the very next call, and the save writes nothing at all while
;    the load appears to work.
;
; And one rule of this game's own: the write happens INSIDE the fsx bracket,
; at the moment the initials are committed. That is legal - the file API is
; UI-task-context-only and a bracket IS the UI task (SPEC.md 53.7) - and it is
; a choice, not the only option. SPEC.md 75.1's wm_onclose is a real exit hook
; that may call the file slots, and saving there would cost one write a session
; instead of one a game. It loses on what the PLAYER sees: a score is banked
; when they finish typing it, and a machine switched off - or a session ended
; any way but through the close box - would drop a row the table had shown.
; =============================================================================

TK_NHS    equ 6                 ; rows in the table
TK_HSFSZ  equ 4 + TK_NHS * 4 + TK_NHS * 3

tk_hs_file:  db 'TANK.HS', 0
tk_hs_magic: db 'TK8', 1        ; so a stale or foreign file is refused rather
                                ; than decoded
tk_d_system: db 'SYSTEM', 0
tk_d_appdat: db 'APPDATA', 0
tk_s_namep:  db 'NAME ', 0
tk_s_hshead: db 'HIGH SCORES', 0

; The table a machine with no file has ever seen. Descending, because
; tk_hs_submit walks from the bottom and stops at the first row it beats.
tk_hs_def:
    dw 6000, 0, 5000, 0, 4000, 0, 3000, 0, 2000, 0, 1000, 0

; -----------------------------------------------------------------------------
; tk_hs_init - the built-in table, before any file is read
; -----------------------------------------------------------------------------
tk_hs_init:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, tk_hs_def
    mov di, tk_hs
    mov cx, TK_NHS * 2
.s:
    mov ax, [si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .s
    mov di, tk_hsn
    mov cx, TK_NHS
.n:
    mov word [di], '--'
    mov byte [di+2], '-'
    add di, 3
    loop .n
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_hs_submit - offer [tk_score] to the table
; out: [tk_hsidx] = the row it took as a BYTE OFFSET x4, or TK_NHS * 4 for
;      none - which is the sentinel BOTH readers test, and writing the
;      unscaled TK_NHS here instead opened the initials prompt for every game
;      that missed the table (it is below 24, so the jae never fired) and then
;      stamped the letters at tk_hsn + 4, off the end of row 1
; -----------------------------------------------------------------------------
tk_hs_submit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [tk_hsidx], TK_NHS * 4
    mov bx, (TK_NHS - 1) * 4        ; the LAST row: if the score cannot beat
    mov ax, [tk_score+2]            ; that one it is not a high score at all
    cmp ax, [tk_hs + bx + 2]
    ja .in
    jb .out
    mov ax, [tk_score]
    cmp ax, [tk_hs + bx]
    jbe .out
.in:
    ; find the row: walk up while the one above is smaller
    mov si, (TK_NHS - 1) * 4
.up:
    or si, si
    jz .got
    mov bx, si
    sub bx, 4
    mov ax, [tk_score+2]
    cmp ax, [tk_hs + bx + 2]
    ja .shift
    jb .got
    mov ax, [tk_score]
    cmp ax, [tk_hs + bx]
    jbe .got
.shift:
    mov ax, [tk_hs + bx]            ; push that row down one
    mov [tk_hs + si], ax
    mov ax, [tk_hs + bx + 2]
    mov [tk_hs + si + 2], ax
    push si                         ; ...and its initials with it
    mov ax, si
    shr ax, 1
    mov cx, si                      ; si/4*3, as si/2 + si/4 - and the /4 is
    shr cx, 1                       ; two shifts, because an 8086 has no
    shr cx, 1                       ; shift-by-immediate (CLAUDE.md 1)
    add ax, cx
    mov di, ax
    sub ax, 3
    mov si, ax
    call tk_hs_movename
    pop si
    sub si, 4
    jmp short .up
.got:
    mov ax, [tk_score]
    mov [tk_hs + si], ax
    mov ax, [tk_score+2]
    mov [tk_hs + si + 2], ax
    mov [tk_hsidx], si              ; the row, as a BYTE OFFSET x4
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_hs_movename - three initials from row SI to row DI (both x3 offsets)
tk_hs_movename:
    push ax
    push si
    push di
    add si, tk_hsn
    add di, tk_hsn
    mov ax, [si]
    mov [di], ax
    mov al, [si+2]
    mov [di+2], al
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; tk_ient_begin - the score made the table: take three initials for it
; -----------------------------------------------------------------------------
tk_ient_begin:
    mov byte [tk_ient], 1
    mov byte [tk_ipos], 0
    mov word [tk_ibuf], '  '
    mov byte [tk_ibuf+2], ' '
    mov byte [tk_ibuf+3], 0
    ret

; -----------------------------------------------------------------------------
; tk_ient_key - AL = ascii while the prompt is up
; out: CF=1 the key was ours and is spent
; -----------------------------------------------------------------------------
tk_ient_key:
    cmp byte [tk_ient], 0
    je .no
    push ax
    push bx
    cmp al, 13
    je .commit
    cmp al, 8
    je .back
    cmp al, ' '
    jb .eat                         ; a control key that is not ours: SWALLOW
    cmp al, 'z'                     ; it, or Esc leaves the bracket in the
    ja .eat                         ; middle of entering a name
    cmp al, 'a'
    jb .up
    sub al, 0x20
.up:
    cmp byte [tk_ipos], 3
    jae .eat
    mov bl, [tk_ipos]
    xor bh, bh
    mov [tk_ibuf + bx], al
    inc byte [tk_ipos]
    jmp short .eat
.back:
    cmp byte [tk_ipos], 0
    je .eat
    dec byte [tk_ipos]
    mov bl, [tk_ipos]
    xor bh, bh
    mov byte [tk_ibuf + bx], ' '
    jmp short .eat
.commit:
    call tk_ient_commit
.eat:
    pop bx
    pop ax
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; tk_ient_commit - stamp the initials into the row and WRITE THE FILE
; -----------------------------------------------------------------------------
tk_ient_commit:
    push ax
    push bx
    push si
    push di
    mov bx, [tk_hsidx]
    cmp bx, TK_NHS * 4
    jae .done
    mov ax, bx                      ; row x4 -> row x3
    shr ax, 1
    mov si, bx
    shr si, 1
    shr si, 1
    add ax, si
    mov di, tk_hsn
    add di, ax
    mov si, tk_ibuf
    mov al, [si]
    mov [di], al
    mov al, [si+1]
    mov [di+1], al
    mov al, [si+2]
    mov [di+2], al
.done:
    mov byte [tk_ient], 0
    call tk_hs_save
    pop di
    pop si
    pop bx
    pop ax
    ret

; =============================================================================
; SYSTEM\APPDATA, and the file
; =============================================================================

; tk_data_enter - bank where we are and go to SYSTEM\APPDATA on this volume.
; out: CF=1 we did not get there and NOTHING was banked or moved.
tk_data_enter:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call OSAPI_FILE_HERE            ; DX = our cwd cluster, BL = our drive
    mov [tk_dbclus], dx
    mov [tk_dbdrv], bl
    xor dx, dx                      ; the ROOT of that same volume
    call OSAPI_FILE_GOTO
    jc .back
    mov si, tk_d_system
    call tk_data_dive
    jc .back
    mov si, tk_d_appdat
    call tk_data_dive
    jc .back
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.back:
    call tk_data_home               ; a disk without the folder is a user's own
    pop es                          ; disk, not an error: put the volume back
    pop di                          ; and refuse
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

tk_data_leave:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    call tk_data_home
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

tk_data_home:
    push ax
    push bx
    push dx
    mov dx, [tk_dbclus]
    mov bl, [tk_dbdrv]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

; tk_data_dive - step into the folder named at SI. CF=1 there is none.
tk_data_dive:
    push ax
    push cx
    push si
    push di
    xor cx, cx
.each:
    push ds                         ; ES EVERY TIME: nothing promises it
    pop es                          ; survives a far call into the kernel
    mov di, tk_dfind
    call OSAPI_FILE_FIND
    jc .none
    cmp word [tk_dfind + 14], OSAPI_FT_DIR
    jne .each                       ; only a FOLDER can be dived into, and
                                    ; type 1 is a PACKAGE, not "a file"
    push cx
    push si
    mov di, tk_dfind
    call tk_data_same
    pop si
    pop cx
    jc .each
    mov dx, [tk_dfind + 16]
    mov bl, [tk_dbdrv]
    call OSAPI_FILE_GOTO
    jc .none
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.none:
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; tk_data_same - is the NUL name at SI the one at DI? CF=0 yes.
tk_data_same:
    push ax
.n:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .n
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; tk_hs_load - read the table once, from the first paint. Anything wrong with
; the file leaves the built-in rows standing: a score table is not worth an
; error box, and the first run has no file at all.  UI TASK ONLY.
; -----------------------------------------------------------------------------
tk_hs_load:
    cmp byte [tk_hsload], 0
    jne .out
    mov byte [tk_hsload], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    call tk_data_enter
    jc .done
    mov si, tk_hs_file
    mov bx, tk_hsbuf
    mov cx, TK_HSFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    call tk_data_leave              ; ...and we stand where we started again,
    jc .done                        ; whatever the read answered
    or dx, dx
    jnz .done
    cmp ax, TK_HSFSZ
    jne .done
    mov si, tk_hsbuf
    mov di, tk_hs_magic
    mov cx, 4
.mag:
    mov al, [si]
    cmp al, [di]
    jne .done
    inc si
    inc di
    loop .mag
    mov si, tk_hsbuf + 4
    mov di, tk_hs
    mov cx, TK_NHS * 4
    call tk_bcopy
    mov di, tk_hsn
    mov cx, TK_NHS * 3
    call tk_bcopy
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; tk_hs_save - write it back.  UI TASK ONLY, which inside a bracket is where
; we are (SPEC.md 53.7).
; -----------------------------------------------------------------------------
tk_hs_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, tk_hs_magic
    mov di, tk_hsbuf
    mov cx, 4
    call tk_bcopy
    mov si, tk_hs
    mov cx, TK_NHS * 4
    call tk_bcopy
    mov si, tk_hsn
    mov cx, TK_NHS * 3
    call tk_bcopy
    call tk_data_enter
    jc .gone
    mov si, tk_hs_file
    mov bx, tk_hsbuf
    mov cx, TK_HSFSZ
    xor dx, dx
    call OSAPI_FILE_WRITE
    call tk_data_leave
.gone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; tk_bcopy - CX bytes DS:SI -> DS:DI, advancing both
tk_bcopy:
    push ax
.each:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .each
    pop ax
    ret
