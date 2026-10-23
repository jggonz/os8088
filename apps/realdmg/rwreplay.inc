; =============================================================================
; os8088 - apps/realdmg/rwreplay.inc
;
; RealDMG's REPLAYS (docs/REALDMG-SPEC.md section 10.5): a save state and the
; buttons from there. F6 (Save State > Record Replay) saves the slot's state, as F5
; does, and from then on keeps the joypad a frame at a time; F6 again writes
; the buttons as NAME.RP1 to NAME.RP9 beside the game. F7 (Save State > Play Replay)
; loads the slot's state, as F8 does, and gives the game those buttons, frame
; for frame, in place of the keyboard's - so the same play runs again as often
; as it is asked for, in the window or in full screen, which is what a field
; measurement of one game against another build needs. Included by
; realdmg.asm.
;
; THE BUTTONS CHANGE ONLY BETWEEN TWO FRAMES while one is recorded or played:
; rw_keys writes S_JOY when the run before it ended at a VBlank (rw_frame sets
; [rw_rpatb]), so a run cut short - the LCD off, a flip's short budget - is
; never a place the buttons change, and frame n gets the same buttons on
; every play whatever the machine's timing. The file is RLE: a byte of buttons
; and a byte of frames it holds for (1-255), after a 16-byte head:
;   0  'RDRP'
;   4  the format, 1
;   5  S_JOY at the start (a press is the JOYPAD interrupt's: the first
;      frame's must find what the recording's found)
;   6  the state's record summed (rw_rpsum): a replay is THAT state's, so a
;      slot saved over since is told and not played into nonsense
;   8  dword: the frames recorded
;   12 the machine summed at the end (rw_rphash): a play that comes out
;      somewhere else says so - "out of step" - which is how a machine that
;      is not deterministic would be caught
;   14 the pairs' bytes
; The worker records and plays (rw_rpkey, on rw_keys's frame); only the UI
; task claims, writes and frees (SPEC.md 20.6 rule 7) - the worker queues
; [rw_rpq] and wakes it, and in full screen rw_keys IS the UI task.
; =============================================================================

RP_HDR          equ 16              ; the head's bytes
RP_VER          equ 1
RP_KB           equ 8               ; the pairs' claim: 4,088 changes of
                                    ;   the buttons, a long play's worth
RPM_REC         equ 1               ; [rw_rpmode]: recording...
RPM_PLAY        equ 2               ;   ...playing...
RPM_RECD        equ 3               ;   ...recorded, the UI task to write it
RPM_DONE        equ 4               ;   ...played out, the UI task to say so
RPQ_WRITE       equ 1               ; [rw_rpq]: the UI task's to do
RPQ_DONE        equ 2
RPR_FULL        equ 1               ; [rw_rpres]: a recording that filled...
RPR_STEP        equ 1               ;   ...a play that ended where it should
RPR_OUT         equ 2               ;   ...or somewhere else
RPR_STOP        equ 3               ;   ...or was stopped by the user

; rw_rpkey - rw_keys's, a replay on: DL = the keys' buttons. Out DL = the
; buttons for S_JOY, or CF = 1 to leave S_JOY as it is (not between frames)
rw_rpkey:
    cmp byte [rw_rpmode], RPM_REC
    je .rec
    cmp byte [rw_rpmode], RPM_PLAY
    je .play
    clc                             ; (written or said by the UI task next:
    ret                             ;   the keys are the game's again)
.hold:
    stc
    ret
.rec:
    cmp byte [rw_rpatb], 0
    je .hold
    mov byte [rw_rpatb], 0
    cmp byte [rw_rpstq], 0          ; F6 again: it ends here, at a frame's
    jne .fin                        ;   start, where a play of it will
    push ax                         ;   end too
    push bx
    push es
    mov es, [rw_rpseg]
    mov bx, [rw_rplen]
    cmp bx, RP_HDR
    je .new
    cmp [es:bx - 2], dl             ; the same buttons: a frame more
    jne .new
    cmp byte [es:bx - 1], 255
    je .new
    inc byte [es:bx - 1]
    jmp short .cnt
.new:
    lea ax, [bx + 2]
    cmp ax, [rw_rpcap]
    ja .full
    mov [es:bx], dl
    mov byte [es:bx + 1], 1
    mov [rw_rplen], ax
.cnt:
    pop es
    pop bx
    pop ax
.one:
    add word [rw_rpfr], 1
    adc word [rw_rpfr + 2], 0
    clc
    ret
.full:
    pop es
    pop bx
    pop ax
    mov byte [rw_rpres], RPR_FULL
.fin:
    call rw_rpfin
    clc
    ret
.play:
    cmp byte [rw_rpatb], 0
    je .hold
    mov byte [rw_rpatb], 0
    cmp byte [rw_rpleft], 0
    jne .take
    push ax
    push bx
    push es
    mov bx, [rw_rppos]
    cmp bx, [rw_rplen]
    jae .end
    mov es, [rw_rpseg]
    mov ax, [es:bx]
    or ah, ah                       ; (a count of 0 is a file's damage: as 1)
    jnz .ok
    inc ah
.ok:
    mov [rw_rpcur], al
    mov [rw_rpleft], ah
    add bx, 2
    mov [rw_rppos], bx
    pop es
    pop bx
    pop ax
.take:
    dec byte [rw_rpleft]
    mov dl, [rw_rpcur]
    jmp short .one
.end:
    pop es
    pop bx
    pop ax
    call rw_rpdone
    clc
    ret

; rw_rpfin - a recording ends, between two frames: the head completed, the
; UI task to write it (any context)
rw_rpfin:
    push ax
    push es
    mov es, [rw_rpseg]
    call rw_rphash
    mov [es:12], ax
    mov ax, [rw_rpfr]
    mov [es:8], ax
    mov ax, [rw_rpfr + 2]
    mov [es:10], ax
    mov ax, [rw_rplen]
    sub ax, RP_HDR
    mov [es:14], ax
    pop es
    pop ax
    mov byte [rw_rpstq], 0
    mov byte [rw_rpmode], RPM_RECD
    or byte [rw_rpq], RPQ_WRITE
    jmp short rw_rpkick

; rw_rpdone - a play has run out, between two frames: where it came out
; against where the recording did, and the UI task to say so
rw_rpdone:
    push ax
    push es
    call rw_rphash
    mov es, [rw_rpseg]
    cmp ax, [es:12]
    mov al, RPR_STEP
    je .s
    mov al, RPR_OUT
.s:
    mov [rw_rpres], al
    pop es
    pop ax
    mov byte [rw_rpmode], RPM_DONE
    or byte [rw_rpq], RPQ_DONE
    ; (falls through)

; rw_rpkick - the UI task to serve [rw_rpq]: now, in full screen, where
; rw_keys runs on it; woken, in the window
rw_rpkick:
    call rw_mgrey
    cmp byte [rw_infsx], 0
    jne rw_rpserve
    push bx
    mov bx, [rw_win]
    call OSAPI_WM_WAKE
    pop bx
    ret

; rw_rpserve - the UI task: a recording written, a play's end said
rw_rpserve:
    test byte [rw_rpq], RPQ_WRITE
    jz .d
    call rw_rpwrite
.d:
    test byte [rw_rpq], RPQ_DONE
    jz .r
    and byte [rw_rpq], ~RPQ_DONE
    call rw_rpend
.r:
    ret

; rw_rpwrite - the UI task: the recording to NAME.RPn, and its claim given
; back
rw_rpwrite:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    and byte [rw_rpq], ~RPQ_WRITE
    call rw_rpnm
    mov dx, [rw_romdir]             ; beside the game, as its states are
    mov bl, [rw_romvol]
    call rw_fgo
    jc .fail
    mov es, [rw_rpseg]
    xor bx, bx
    mov cx, [rw_rplen]
    xor dx, dx
    mov si, rw_rpname
    call OSAPI_FILE_WRITE
    call rw_fback                   ; (CF kept)
    jc .fail
    mov si, rw_s_rpsaved
    cmp byte [rw_rpres], RPR_FULL
    jne .say
    mov si, rw_s_rpfull
    jmp short .say
.fail:
    mov si, rw_s_rpnosave
.say:
    call rw_rpfree
    mov al, RLE2_RECOFF
    call rw_logevt2
    call rw_rpsay
    cmp byte [rw_infsx], 0          ; (full screen: the disk's time is no
    je .x                           ;   time owed - F5's own way)
    call rw_strun
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_rpend - the UI task: a play's end said, and its claim given back
rw_rpend:
    push ax
    push si
    mov si, rw_s_rpstep
    cmp byte [rw_rpres], RPR_STEP
    je .s
    mov si, rw_s_rpout
    cmp byte [rw_rpres], RPR_OUT
    je .s
    mov si, rw_s_rpstop
.s:
    call rw_rpfree
    mov al, RLE2_PLAYOFF
    call rw_logevt2
    call rw_rpsay
    pop si
    pop ax
    ret

; rw_rpfree - no replay: its claim given back, the menu told (UI task)
rw_rpfree:
    push dx
    xor dx, dx
    xchg dx, [rw_rpseg]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
.z:
    mov byte [rw_rpmode], 0
    mov byte [rw_rpstq], 0
    pop dx
    jmp rw_mgrey

; rw_rpdrop - the game started again under a replay (Reset, a load): it is
; no longer that game's, and goes without a word. Any context: the claim is
; left to the UI task's next replay or to the close
rw_rpdrop:
    cmp byte [rw_rpmode], 0
    je .r
    mov byte [rw_rpmode], 0
    mov byte [rw_rpq], 0
    call rw_mgrey
.r:
    ret

; rw_rprec - F6, Save State > Record Replay: the slot's state saved and the buttons
; kept from here - or, recording, the recording ended and written. UI task
rw_rprec:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [rw_rpmode], RPM_REC
    je .stop
    cmp byte [rw_rpmode], 0
    jne .busy
    call rw_rpclaim
    jc .x
    call rw_stgrab
    jc .nogame
    call rw_stwr                    ; the slot's state: where it starts
    jc .failed
    mov es, [rw_rpseg]
    mov word [es:0], 'RD'
    mov word [es:2], 'RP'
    mov byte [es:4], RP_VER
    push ds
    mov ds, [rdg_viewseg]
    mov al, [S_JOY]
    pop ds
    mov [es:5], al
    call rw_rpsum
    mov [es:6], ax
    mov word [rw_rplen], RP_HDR
    xor ax, ax
    mov [rw_rpfr], ax
    mov [rw_rpfr + 2], ax
    mov [rw_rpres], al
    mov [rw_rpstq], al
    mov [rw_rpq], al
    mov al, [rw_slot]
    mov [rw_rpslot], al
    mov byte [rw_rpatb], 1          ; (the state is a frame's start)
    mov byte [rw_rpmode], RPM_REC
    call rw_mgrey
    mov al, RLE2_RECON
    call rw_logevt2
    mov si, rw_s_rprec
    call rw_rpsay
    call rw_strun
    jmp short .x
.failed:
    call rw_stsay                   ; (rw_stwr's: why)
    call rw_strun
.nogame:
    call rw_rpfree
    jmp short .x
.stop:
    call rw_stgrab                  ; the worker parked: between two
    jc .x                           ;   frames, as a rule, and it ends
    cmp byte [rw_rpatb], 0          ;   there - else at the next (an LCD
    je .later                       ;   off, a run cut short)
    call rw_rpfin
    call rw_rpserve                 ; (in the window: here, not woken)
    jmp short .run
.later:
    mov byte [rw_rpstq], 1
.run:
    call rw_strun
    jmp short .x
.busy:
    mov si, rw_s_rpbusy
    call rw_note
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_rpplay - F7, Save State > Play Replay: the slot's replay read, its state
; loaded and its buttons given to the game - or, playing, the play stopped.
; UI task
rw_rpplay:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [rw_rpmode], RPM_PLAY
    je .stop
    cmp byte [rw_rpmode], 0
    jne .busy
    cmp byte [rw_state], 1
    jne .x
    mov al, [rw_slot]
    mov [rw_rpslot], al
    call rw_rpclaim
    jc .x
    call rw_rpnm
    mov dx, [rw_romdir]
    mov bl, [rw_romvol]
    call rw_fgo
    jc .none
    mov es, [rw_rpseg]
    xor bx, bx
    mov cx, [rw_rpcap]
    xor dx, dx
    mov si, rw_rpname
    call OSAPI_FILE_READ            ; DX:AX = its bytes
    call rw_fback
    jnc .read
    cmp ax, FERR_NOENT
    je .none
    jmp .bad
.read:
    or dx, dx
    jnz .bad
    cmp ax, RP_HDR
    jb .bad
    cmp word [es:0], 'RD'
    jne .bad
    cmp word [es:2], 'RP'
    jne .bad
    cmp byte [es:4], RP_VER
    jne .bad
    mov bx, [es:14]
    test bl, 1
    jnz .bad
    add bx, RP_HDR
    jc .bad
    cmp bx, ax
    ja .bad
    mov [rw_rplen], bx
    call rw_stgrab
    jc .nogame
    call rw_strd                    ; the slot's state: where it starts
    jc .failed
    call rw_rpsum
    mov es, [rw_rpseg]
    cmp ax, [es:6]
    jne .other
    mov al, [es:5]                  ; the buttons the recording found
    push ds
    mov ds, [rdg_viewseg]
    mov [S_JOY], al
    pop ds
    mov word [rw_rppos], RP_HDR
    xor ax, ax
    mov [rw_rpfr], ax
    mov [rw_rpfr + 2], ax
    mov [rw_rpleft], al
    mov [rw_rpres], al
    mov [rw_rpq], al
    mov byte [rw_rpatb], 1
    mov byte [rw_rpmode], RPM_PLAY
    call rw_mgrey
    mov al, RLE2_PLAYON
    call rw_logevt2
    mov si, rw_s_rpplay
    call rw_rpsay
    call rw_strun
    jmp short .x
.other:
    mov si, rw_s_rpother            ; (the state is loaded: it is the slot's)
    jmp short .say
.failed:
    call rw_stsay                   ; (rw_strd's: why)
    call rw_strun
    jmp short .free
.nogame:
    call rw_rpfree
    jmp short .x
.none:
    mov si, rw_s_rpempty
    jmp short .sayf
.bad:
    mov si, rw_s_rpbad
.sayf:
    call rw_rpfree
    call rw_rpsay
    jmp short .x
.say:
    call rw_rpfree
    call rw_rpsay
    call rw_strun
    jmp short .x
.free:
    call rw_rpfree
    jmp short .x
.stop:
    call rw_stgrab                  ; the worker parked, out of rw_rpkey
    jc .x
    mov byte [rw_rpres], RPR_STOP
    and byte [rw_rpq], ~RPQ_DONE
    call rw_rpend
    call rw_strun
    jmp short .x
.busy:
    mov si, rw_s_rpbusy
    call rw_note
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_rpclaim - [rw_rpseg] and [rw_rpcap]: RP_KB, or 2KB on a full heap. CF
; = 1 neither, and it is said. UI task
rw_rpclaim:
    mov dx, [rw_rpseg]              ; (a claim rw_rpdrop left behind)
    or dx, dx
    jz .c
    call OSAPI_MEM_FREE
    mov word [rw_rpseg], 0
.c:
    mov ax, RP_KB
    call OSAPI_MEM_CLAIM
    jnc .got
    mov ax, 2
    call OSAPI_MEM_CLAIM
    jnc .got
    mov si, rw_s_rproom
    call rw_rpsay
    stc
    ret
.got:
    mov [rw_rpseg], dx
    mov cl, 10
    shl ax, cl
    mov [rw_rpcap], ax
    clc
    ret

; rw_rpnm - rw_rpname = rw_name with its extension made .RP1 to .RP9, for
; the replay's slot
rw_rpnm:
    mov di, rw_rpname
    mov ax, '.R'
    mov dl, 'P'
    mov dh, [rw_rpslot]
    add dh, '1'
    jmp rw_extnm

; rw_rpsay - SI = a line with '#' for the REPLAY's slot (rw_stsay's)
rw_rpsay:
    push ax
    mov al, [rw_slot]
    push ax
    mov al, [rw_rpslot]
    mov [rw_slot], al
    call rw_stsay
    pop ax
    mov [rw_slot], al
    pop ax
    ret

; rw_rpsum - AX = the state's record (RDG_STREC, the view's) summed: what a
; replay says it starts from
rw_rpsum:
    push bx
    push cx
    push si
    push ds
    mov ds, [cs:rdg_viewseg]
    mov si, RDG_STREC
    mov cx, ST_RECSZ / 2
    xor bx, bx
    cld
.l:
    lodsw
    rol bx, 1
    add bx, ax
    loop .l
    mov ax, bx
    pop ds
    pop si
    pop cx
    pop bx
    ret

; rw_rphash - AX = the machine summed: VRAM, WRAM, OAM and HRAM (not the
; cartridge's RAM window, which a load may have left in another bank's
; place, nor the I/O registers, which the core brings up to date when they
; are read and not when time passes)
rw_rphash:
    push bx
    push cx
    push dx
    push si
    push ds
    mov ds, [cs:rdg_viewseg]
    mov si, rw_rpranges
    xor bx, bx
    cld
.r:
    mov dx, [cs:si]
    mov cx, [cs:si + 2]
    jcxz .d
    add si, 4
    push si
    mov si, dx
.l:
    lodsw
    rol bx, 1
    add bx, ax
    loop .l
    pop si
    jmp short .r
.d:
    mov ax, bx
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    ret

rw_rpranges:    dw 8000h, 1000h, 0C000h, 1000h, 0FE00h, 50h, 0FF80h, 40h, 0, 0

rw_s_rprec:     db 'Recording replay #', 0
rw_s_rpsaved:   db 'Replay # saved', 0
rw_s_rpfull:    db 'Replay # full, saved', 0
rw_s_rpnosave:  db 'Replay # not saved', 0
rw_s_rpplay:    db 'Playing replay #', 0
rw_s_rpstep:    db 'Replay # ended', 0
rw_s_rpout:     db 'Replay # OUT OF STEP', 0
rw_s_rpstop:    db 'Replay # stopped', 0
rw_s_rpempty:   db 'Replay # is empty', 0
rw_s_rpbad:     db 'Replay # is damaged', 0
rw_s_rpother:   db 'Slot # was saved over', 0
rw_s_rproom:    db 'Replay: no room', 0
rw_s_rpbusy:    db 'A replay is running', 0
