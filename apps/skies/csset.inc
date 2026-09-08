; =============================================================================
; os8088 - apps/skies/csset.inc
;
; THE SETTINGS, KEPT (SPEC.md 88.13.9): the five bytes the Settings page edits,
; a file in SYSTEM\APPDATA, read once in the ENTRY PROC and written when the
; page is left.
;
; The shape is apps/tank's tkhs.inc (SPEC.md 85.9), which is apps/cyclone's
; (SPEC.md 67.20), and its two hard-won rules are repeated here because
; getting either wrong fails QUIETLY:
;
;  - **The file lives in SYSTEM\APPDATA on OUR OWN volume** (SPEC.md 19.9),
;    not beside the package. The file browser is the whole of how a user
;    reaches an application here, so a data file in an app folder is a
;    misclick waiting to happen.
;  - **It is OSAPI_FILE_GOTO and not its quiet twin.** GOTO_Q moves the GLOBAL
;    cwd and deliberately not the instance's, while FILE_FIND, _READ and
;    _WRITE all resolve in the INSTANCE's folder - so a quiet move is undone
;    by the very next call, and the save writes nothing at all while the load
;    appears to work.
;
; This is the FOURTH copy of the eighty lines below - tank, cyclone and ftpd
; carry the other three - and the obvious next step is one `apps/os88data.inc`
; that all four include. It is not taken here: those three work, and moving
; them is a change to three shipped packages rather than to this one.
;
; WHEN IT SAVES is the choice this file makes differently to tank's. Tank
; banks a score the moment the initials are typed, inside the fsx bracket,
; because a player expects a score to be theirs as soon as they finish typing
; it. A setting is not like that: the HOTKEYS are deliberately temporary - F1
; to F5 are for trying the next rung while flying and a machine switched off
; mid-flight should not have inherited whatever was being tried - so what is
; written is the SETTINGS PAGE's answer, at the moment the page is left, and
; the file is the considered choice rather than the last experiment.
; =============================================================================

CS_SETFSZ  equ 4 + 5                ; the magic and the five bytes
cs_set_file:  db 'CSSET.DAT', 0
cs_set_magic: db 'CS8', 1           ; so a stale or foreign file is refused
                                    ; rather than decoded
cs_sd_system: db 'SYSTEM', 0
cs_sd_appdat: db 'APPDATA', 0

; What each byte is, and the largest value it may hold. A file that matches
; the magic and holds nonsense anyway is CLAMPED and not refused: the magic is
; what tells a foreign file apart, and a rung out of range is one byte of a
; file this program wrote, so the honest answer is the nearest rung rather
; than throwing five good settings away.
cs_set_at:  dw cs_setbld, cs_setlod, cs_setsize, cs_modepref, cs_setfill
cs_set_max: db CSBL_HIGH, CSL_ULTRA, CSZ_FULL, 1, CSFL_ALL
; ...and what a 286 or better starts on with no file at all (SPEC.md 88.13.9).
; NOT cs_set_max: that is a CEILING for the clamp, and the Mode byte's ceiling
; is 1, which is CGA - a machine given the best of everything else would have
; been given the worse of two displays.
cs_set_best: db CSBL_HIGH, CSL_ULTRA, CSZ_FULL, 0, CSFL_ALL
CS_SETN     equ 5

; -----------------------------------------------------------------------------
; cs_set_enter / cs_set_leave - stand in SYSTEM\APPDATA on our own volume, and
;                               come back to where we were standing.
;                               CF=1 there is no such folder.
; -----------------------------------------------------------------------------
cs_set_enter:
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
    mov [cs_sdclus], dx
    mov [cs_sddrv], bl
    xor dx, dx                      ; the ROOT of that same volume
    call OSAPI_FILE_GOTO
    jc .back
    mov si, cs_sd_system
    call cs_set_dive
    jc .back
    mov si, cs_sd_appdat
    call cs_set_dive
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
    call cs_set_home                ; a disk without the folder is a user's own
    pop es                          ; disk, not an error: put the volume back
    pop di                          ; and refuse
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

cs_set_leave:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    call cs_set_home
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

cs_set_home:
    push ax
    push bx
    push dx
    mov dx, [cs_sdclus]
    mov bl, [cs_sddrv]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    pop ax
    ret

; cs_set_dive - step into the folder named at SI. CF=1 there is none.
cs_set_dive:
    push ax
    push cx
    push si
    push di
    xor cx, cx
.each:
    push ds                         ; ES EVERY TIME: nothing promises it
    pop es                          ; survives a far call into the kernel
    mov di, cs_sdfind
    call OSAPI_FILE_FIND
    jc .none
    cmp word [cs_sdfind + 14], OSAPI_FT_DIR
    jne .each                       ; only a FOLDER can be dived into, and
                                    ; type 1 is a PACKAGE, not "a file"
    push cx
    push si
    mov di, cs_sdfind
    call cs_set_same
    pop si
    pop cx
    jc .each
    mov dx, [cs_sdfind + 16]
    mov bl, [cs_sddrv]
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

; cs_set_same - is the NUL name at SI the one at DI? CF=0 yes.
cs_set_same:
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
; cs_set_load - read the five bytes once. Anything wrong with the file leaves
; the built-in defaults standing: a settings file is not worth an error box,
; and the first run has no file at all.  UI TASK ONLY.
;
; CALLED FROM cs_entry AND NOT FROM THE FIRST PAINT, which is where tank puts
; its own. Two reasons, and only the first is a rule: SIZE is the last default
; set and it is set from the ADAPTER, which is not known until the window
; exists - so the read has to come after that line or the card overwrites the
; player's answer. The second is an observation this file did not chase: the
; same read AT THE FIRST PAINT left the buffer untouched with the file sitting
; on the disk, and the same read a moment later filled it. tank and cyclone
; both load at their first paint, so it is worth someone's afternoon.
; -----------------------------------------------------------------------------
cs_set_load:
    cmp byte [cs_setread], 0
    jne .out
    mov byte [cs_setread], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    ; --- A 286 OR BETTER WITH NO FILE STARTS AT THE TOP (SPEC.md 88.13.9).
    ;     The shipped defaults are an 8088's - the machine this simulator is
    ;     calibrated against - and on anything faster they are a first flight
    ;     spent looking at an empty world and wondering where the buildings
    ;     are. This is BEFORE the read on purpose: a file that exists says
    ;     what the player chose and overwrites every one of these, so the
    ;     tier decides only what a machine with no file has ever been told.
    call OSAPI_CPU_INFO
    cmp al, CPU_8086
    jbe .noboost
    xor bx, bx
.boost:
    mov al, [cs_set_best + bx]
    push bx
    shl bx, 1
    mov di, [cs_set_at + bx]
    mov [di], al
    pop bx
    inc bx
    cmp bx, CS_SETN
    jb .boost
.noboost:
    call cs_set_enter
    jc .done
    mov si, cs_set_file
    mov bx, cs_setbuf
    mov cx, CS_SETFSZ
    xor dx, dx
    call OSAPI_FILE_READ
    call cs_set_leave               ; ...and we stand where we started again,
    jc .done                        ; whatever the read answered
    or dx, dx
    jnz .done
    cmp ax, CS_SETFSZ
    jne .done
    mov si, cs_setbuf
    mov di, cs_set_magic
    mov cx, 4
.mag:
    mov al, [si]
    cmp al, [di]
    jne .done
    inc si
    inc di
    loop .mag
    xor bx, bx                      ; SI is the fifth byte of the buffer
.each:
    mov al, [si]
    cmp al, [cs_set_max + bx]       ; ...clamped rather than refused
    jbe .ok
    mov al, [cs_set_max + bx]
.ok:
    push bx
    shl bx, 1
    mov di, [cs_set_at + bx]
    mov [di], al
    pop bx
    inc si
    inc bx
    cmp bx, CS_SETN
    jb .each
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
; cs_set_save - write them back, when the Settings page is left.  UI TASK ONLY,
; which is where every W_ handler runs.
; -----------------------------------------------------------------------------
cs_set_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    ; --- NOTHING CHANGED, NOTHING WRITTEN. The page is left far more often
    ;     than it is used, and a floppy create is several int 13h calls with
    ;     the gfx lock held (SPEC.md 12.8.3) - a second of frozen machine for
    ;     a file that would come out identical. cs_setbuf holds what was last
    ;     read or written, and the MAGIC being in it is what says so: on a
    ;     first run it is zeros, and five settings that happen to be zero
    ;     would otherwise read as "already saved" and never be.
    mov si, cs_setbuf
    mov di, cs_set_magic
    mov cx, 4
.same:
    mov al, [si]
    cmp al, [di]
    jne .differs
    inc si
    inc di
    loop .same
    xor bx, bx
.sameb:
    push bx
    shl bx, 1
    mov di, [cs_set_at + bx]
    pop bx
    mov al, [di]
    cmp al, [si]
    jne .differs
    inc si
    inc bx
    cmp bx, CS_SETN
    jb .sameb
    jmp .gone                       ; byte for byte what is on the disk
.differs:
    mov si, cs_set_magic
    mov di, cs_setbuf
    mov cx, 4
.mag:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .mag
    xor bx, bx
.each:
    push bx
    shl bx, 1
    mov si, [cs_set_at + bx]
    pop bx
    mov al, [si]
    mov [di], al
    inc di
    inc bx
    cmp bx, CS_SETN
    jb .each
    call cs_set_enter
    jc .gone
    mov si, cs_set_file
    mov bx, cs_setbuf
    mov cx, CS_SETFSZ
    xor dx, dx
    call OSAPI_FILE_WRITE
    call cs_set_leave
.gone:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
