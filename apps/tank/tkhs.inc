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
;  - **The folder is reached through apps/os88data.inc**, which walks with
;    OSAPI_FILE_GOTO_QM - the quiet stand that MOVES THE INSTANCE, so the
;    file cell after it resolves there - banks APPDATA's cluster, and
;    rewrites a file of unchanged length with one sector. This file used to
;    walk with OSAPI_FILE_GOTO, a remount for DISPLAY at every step, and a
;    save cost about six seconds of floppy (docs/plans/NAV-COST-PLAN.md).
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

; THE TABLE IS THE FILE. tk_hsbuf is the four-byte magic and tk_hs and tk_hsn
; follow it in bss - asserted where they are declared - so the load reads
; straight into the live table and the save writes it as it stands, with no
; staging copy of either; tk_hs_init stamps the magic with the defaults, so
; the table in memory is a valid file from the entry proc on.
TK_HSMAG0 equ 'TK'              ; 'TK8', 1: so a stale or foreign file is
TK_HSMAG1 equ '8' | (1 << 8)    ; refused rather than decoded

tk_hs_file:  db 'TANK.HS', 0
tk_hs_ip:    db 0               ; os88data.inc's in-place stamp for TANK.HS
tk_s_namep:  db 'NAME ', 0
tk_s_hshead: db 'HIGH SCORES', 0

; The table a machine with no file has ever seen, magic first. Descending,
; because tk_hs_submit walks from the bottom and stops at the first row it
; beats.
tk_hs_def:
    dw TK_HSMAG0, TK_HSMAG1
    dw 6000, 0, 5000, 0, 4000, 0, 3000, 0, 2000, 0, 1000, 0

; -----------------------------------------------------------------------------
; tk_hs_init - the built-in table, before any file is read (or over a bad one)
; -----------------------------------------------------------------------------
tk_hs_init:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, tk_hs_def
    mov di, tk_hsbuf
    mov cx, 2 + TK_NHS * 2
    rep movsw                       ; the magic and the six scores...
    mov al, '-'                     ; ...and no initials, which tk_hsn follows
    mov cx, TK_NHS * 3
    rep stosb
    pop es
    pop di
    pop si
    pop cx
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
    mov word [tk_ient], 1           ; [tk_ient] = 1 and [tk_ipos] = 0, adjacent
    mov word [tk_ibuf], '  '        ; ...and three blanks and a NUL
    mov word [tk_ibuf+2], ' '
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
    mov ax, [tk_ibuf]
    mov [di], ax
    mov al, [tk_ibuf+2]
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

%include "os88data.inc"

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
    mov si, tk_hs_file
    mov bx, tk_hsbuf                ; STRAIGHT INTO THE TABLE: a file that is
    mov cx, TK_HSFSZ                ; not ours is caught below and the defaults
    xor dx, dx                      ; written back over whatever it left
    call od_read                    ; ...and we stand where we started again,
    jc .bad                         ; whatever the read answered
    or dx, dx
    jnz .bad
    cmp ax, TK_HSFSZ
    jne .bad
    cmp word [tk_hsbuf], TK_HSMAG0
    jne .bad
    cmp word [tk_hsbuf+2], TK_HSMAG1
    je .done
.bad:
    call tk_hs_init
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
    mov si, tk_hs_file
    mov bx, tk_hsbuf
    mov cx, TK_HSFSZ
    mov di, tk_hs_ip
    call od_write                   ; one sector, after the session's first
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
