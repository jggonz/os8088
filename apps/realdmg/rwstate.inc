; =============================================================================
; os8088 - apps/realdmg/rwstate.inc
;
; RealDMG's SAVE STATES (docs/REALDMG-SPEC.md section 10.4): nine slots a
; game, NAME.SS1 to NAME.SS9 beside it, as its .SAV is. 1 to 9 picks the
; slot, F5 saves it and F8 loads it, in the window and in full screen alike;
; Save State > Save State and Load State are the same two. Included by realdmg.asm.
;
; The file is the machine where it already lies, written from there and read
; back there - no buffer is claimed for it:
;   0       the view's 8000-FFFF: VRAM, the RAM window, WRAM, OAM, I/O, HRAM
;   32K     S_RAMSEG's banks, 8 or 32KB (rdgsys's rdg_stram), when it has some
;   then    the 2KB record rdgsys composes at RDG_STREC (rdg_stcompose)
; Every part but the last starts at a multiple of 32KB and the RAM part is a
; multiple of 8KB, so each append starts on a cluster on any volume whose
; clusters are 8KB or less (OSAPI_FILE_APPEND's rule), and the record, last,
; may be any length. 34, 42 or 66KB.
;
; The machine must be still while it is written or read. In the window the
; worker backs off at the top of its loop while [rw_stq] is set - rw_fsenter's
; handshake (rwfsx.inc): we raise ours, then wait for its [rw_incore] to fall.
; In full screen the machine is the UI task's own, between two frames here.
; =============================================================================

; rw_stkey - rw_klat's: AH = a typed key's scancode. 1-9 the slot, F5 save,
; F8 load, F6 and F7 the slot's replay. On the UI task, in the window
; (W_ONKEY) and the bracket alike
rw_stkey:
    cmp ah, 02h                     ; '1'
    jb .r
    cmp ah, 0Ah                     ; '9'
    jbe .slot
    cmp ah, 3Fh                     ; F5
    je rw_stsave
    cmp ah, 42h                     ; F8
    je rw_stload
    cmp ah, 40h                     ; F6, F7: the slot's replay recorded
    je rw_rprec                     ;   and played (rwreplay.inc)
    cmp ah, 41h
    je rw_rpplay
.r:
    ret
.slot:
    push ax
    push si
    mov al, ah
    sub al, 2
    mov [rw_slot], al
    call rw_mgrey                   ; (Save State's mark, for 1-6)
    mov si, rw_s_stslot
    call rw_stsay
    pop si
    pop ax
    ret

; rw_stsay - SI = a line with '#' for the slot's digit: said (rw_note)
rw_stsay:
    push ax
    push di
    push es
    push ds
    pop es
    mov di, rw_stmsg
.c:
    lodsb
    cmp al, '#'
    jne .s
    mov al, [rw_slot]
    add al, '1'
.s:
    stosb
    or al, al
    jnz .c
    pop es
    pop di
    pop ax
    mov si, rw_stmsg
    jmp rw_note

; rw_ssnm - rw_ssname = rw_name with its extension made .SS1 to .SS9
rw_ssnm:
    push ax
    push cx
    push dx
    push si
    push di
    mov di, rw_ssname
    mov ax, '.S'
    mov dl, 'S'
    mov dh, [rw_slot]
    add dh, '1'
    call rw_extnm
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; rw_stgrab - the machine still for us, the speaker quiet: CF = 1 if there
; is no game running to save or load
rw_stgrab:
    cmp byte [rw_state], 1
    jne .no
    mov byte [rw_stq], 1            ; raised BEFORE [rw_incore] is read:
    cmp byte [rw_infsx], 0          ;   rwfsx.inc's order (full screen: the
    jne .ok                         ;   machine is ours already)
.w:
    cmp byte [rw_incore], 0
    je .ok
    call rw_bserve                  ; (it may be waiting on us for a bank)
    call OSAPI_TASK_YIELD
    jmp short .w
.ok:
    mov byte [rw_stq], 2            ; (the worker parked: rw_bmiss's cue)
    call rw_quiet
    clc
    ret
.no:
    stc
    ret

; rw_strun - the machine going again from here: no time owed for the disk's,
; and the next frame drawn whole
rw_strun:
    call rw_dmgfull
    call OSAPI_GET_TICKS
    mov [rw_last], ax
    mov word [rw_owed], 0
    mov word [rw_fowed], 0
    mov byte [rw_fsfrac], 0
    call rw_drawnow
    mov byte [rw_stq], 0
    ret

; rw_stsave - F5, Save State > Save State: the slot's file written
rw_stsave:
    call rw_stgrab
    jc .r
    call rw_stwr
    call rw_stsay
    call rw_strun
.r:
    ret

; rw_stwr - the machine still (rw_stgrab): the slot's file written. SI = what
; to say (rw_stsay); CF = 1 it was not. Replay's start is one (rwreplay.inc)
rw_stwr:
    push ds
    mov ds, [cs:rdg_viewseg]
    call rdg_ramsync                 ; the window back in its bank first
    call rdg_stcompose
    call rdg_stram                   ; AX = the RAM part's bytes
    pop ds
    mov [rw_stlen], ax
    call rw_ssnm
    mov dx, [rw_romdir]             ; beside the game
    mov bl, [rw_romvol]
    call rw_fgo
    jc .nogo
    push es
    mov es, [rdg_viewseg]            ; the Game Boy's 8000-FFFF
    mov bx, 8000h
    mov cx, 8000h
    xor dx, dx
    mov si, rw_ssname
    call OSAPI_FILE_WRITE
    jc .wr
    mov cx, [rw_stlen]              ; the RAM banks
    jcxz .rec
    mov es, [rw_ramseg]
    xor bx, bx
    call OSAPI_FILE_APPEND
    jc .wr
.rec:
    mov es, [rdg_viewseg]            ; the record, last
    mov bx, RDG_STREC
    mov cx, ST_RECSZ
    call OSAPI_FILE_APPEND
    jnc .wr
    call OSAPI_FILE_DELETE          ; (a half-written state is no state)
    stc
.wr:
    pop es
    call rw_fback                   ; (CF kept)
    jc .fail
    mov si, rw_s_stsaved
    ret                             ; (CF = 0)
.nogo:
    call rw_fback
.fail:
    mov si, rw_s_stnosave
    stc
    ret

; rw_stload - F8, Save State > Load State: the slot's file read back. Nothing of
; the machine is touched until the file's size and its record say it is a
; state of THIS game; past that, a read that fails resets the game rather
; than leave it half one machine and half another
rw_stload:
    cmp byte [rw_rpmode], 0         ; (a replay is one machine's from its
    je .go                          ;   start: rwreplay.inc)
    mov si, rw_s_rpbusy
    jmp rw_note
.go:
    call rw_stgrab
    jc .r
    call rw_strd
    call rw_stsay
    call rw_strun
.r:
    ret

; rw_strd - the machine still (rw_stgrab): the slot's file read back. SI =
; what to say (rw_stsay); CF = 1 nothing was loaded - or it broke part way
; and the game was reset. Replay's play is one (rwreplay.inc)
rw_strd:
    push ds
    mov ds, [cs:rdg_viewseg]
    call rdg_stram
    pop ds
    mov [rw_stlen], ax
    call rw_ssnm
    mov dx, [rw_romdir]
    mov bl, [rw_romvol]
    call rw_fgo
    jc .nogo
    push es
    push ds                         ; ITS SIZE, before a byte is read: the
    pop es                          ;   probe (no room) is answered FERR_BIG
    mov bx, rw_stbyte               ;   with the file's KB plus one, from its
    xor cx, cx                      ;   entry (or FERR_NOENT) - so the file
    xor dx, dx                      ;   is this game's size, or up to 1,023
    mov si, rw_ssname               ;   bytes over it
    call OSAPI_FILE_READ
    jnc .notours
    cmp ax, FERR_BIG
    jne .none
    mov ax, [rw_stlen]
    add ax, 8000h + ST_RECSZ        ; (at most 66KB: AX wraps, and DX is
    mov cl, 10                      ;   the KB either way)
    rcr ax, 1
    dec cl
    shr ax, cl
    inc ax                          ; AX = what this game's state answers
    cmp dx, ax
    jne .notours
    mov es, [rdg_viewseg]            ; ITS RECORD, into RDG_STREC - scratch.
    push ds                         ;   Up to 1,023 bytes past it are the
    mov ds, [rdg_viewseg]            ;   Game Boy's 8000h on, so those are
    mov si, 8000h                   ;   kept aside in the view's spare KB
    mov di, RDG_STKEEP               ;   and put back if they were written
    mov cx, 512
    cld
    rep movsw
    pop ds
    mov si, rw_ssname               ; (the copy took SI)
    mov bx, RDG_STREC
    mov cx, 8000h                   ; (a cluster multiple on any volume)
    mov ax, 8000h
    xor dx, dx
    add ax, [rw_stlen]
    adc dx, 0
    call OSAPI_FILE_READ_AT
    jc .notours
    or dx, dx
    jnz .over
    cmp ax, ST_RECSZ
    je .rec
    jb .notours
.over:
    push ds                         ; it ran on into 8000h: back as it was
    mov ds, [rdg_viewseg]
    mov si, RDG_STKEEP
    mov di, 8000h
    mov cx, 512
    rep movsw
    pop ds
    jmp short .notours
.rec:
    push ds
    mov ds, [cs:rdg_viewseg]
    call rdg_stcheck
    pop ds
    jc .notours
    mov bx, 8000h                   ; COMMITTED: the Game Boy's 8000-FFFF...
    mov cx, 8000h
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jc .broken
    cmp ax, 8000h
    jne .broken
    mov cx, [rw_stlen]              ; ...the RAM banks...
    jcxz .apply
    mov es, [rw_ramseg]
    xor bx, bx
    mov ax, 8000h
    xor dx, dx
    call OSAPI_FILE_READ_AT
    jc .broken
    cmp ax, [rw_stlen]
    jne .broken
.apply:
    push ds                         ; ...and the record's fields
    mov ds, [cs:rdg_viewseg]
    call rdg_stapply
    pop ds
    pop es
    call rw_fback
    mov si, rw_s_stloaded
    clc
    ret
.broken:
    pop es
    call rw_fback
    call rw_reset                   ; half loaded: the game from its start
    mov si, rw_s_stbroken
    stc
    ret
.notours:
    pop es
    call rw_fback
    mov si, rw_s_stother
    stc
    ret
.none:
    pop es
.nogo:
    call rw_fback
    mov si, rw_s_stempty
    stc
    ret

rw_s_stslot:    db 'Slot #', 0
rw_s_stsaved:   db 'Saved slot #', 0
rw_s_stloaded:  db 'Loaded slot #', 0
rw_s_stempty:   db 'Slot # is empty', 0
rw_s_stother:   db 'Slot # not this game', 0
rw_s_stnosave:  db 'Slot #: save failed', 0
rw_s_stbroken:  db 'Slot #: load failed', 0
