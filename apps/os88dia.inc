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
; A save asks first whether the disk in the drive is still HOME - the folder
; the widget was launched from still holds a file of its own name - because a
; widget outlives a disk swap (SPEC.md 107.5.1). A refusal that may clear
; (no disk, an I/O error, not home) is retried on a doubling backoff; one that
; will not (write-protected, full) makes the state read-only for the session.
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
;   %define DIA_ONTICK  proc            optional: a near proc, every tick -
;                                       the UI task with the gfx lock HELD on
;                                       both kernels, no clip region armed.
;                                       It ANSWERS: CF = 1 holds any save
;                                       owed to a later poll (a field open,
;                                       a question up, an edit this poll),
;                                       CF = 0 lets it go. A save is seconds
;                                       of disk on a 5150 (SPEC.md 107.6)
;   %define DIA_ONCLOSE proc            optional: before the row is removed
;   %define DIA_ISOPEN  'Desk Calendar is open'   optional: the toast a SECOND
;                                       launch is refused with (<= 24 chars,
;                                       SPEC.md 59.10); 'Already open' if not
;   %define DIA_DAMAGEDQ 'Replace damaged calendar?'   optional, and only with
;                                       os88ui.inc's OS88UI_ALERT included
;                                       first: asked before the first save
;                                       over a damaged file (SPEC.md 107.5.2)
;
; and gets DIA_PAY (the payload, DIA_PAYLEN bytes, zero when there was no
; valid file) and DIA_BSS_END (the next free bss offset).
;
; ITS CALLS, at the package's own points:
;
;   dia_load    the ENTRY PROC, before OSAPI_WM_CREATE: SI = the loader's
;               name (KERNEL_SEG:SI), DI = your window template. CF = 1: a
;               window of ANOTHER instance of this package is up, the user
;               has been told, and your entry proc must return CF = 1 having
;               done nothing else. CF = 0: the state file is read and the
;               template placed (anchored if it was saved on another screen).
;   dia_made    after OSAPI_WM_CREATE AND your own OSAPI_WM_KEEPH /
;               OSAPI_WM_PREFER, BX = the window: puts back a restored y the
;               create's fit pulled up off the dock, takes the place the window opened at
;               as the place on record, installs the timer - or, on
;               kern_small, the wake handler of the worker arm - and the close
;               negotiator, and arms the first tick.
;   dia_painted the LAST thing your W_PAINT does (lock held): on kern_small
;               it hires the worker on the first paint; elsewhere a test.
;   dia_touch   your payload changed: a save is owed, and the next tick
;               makes it. Every register and the flags kept.
;
; SPEC.md 107.5 and 107.5.2 are the contract.
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
DIA_S_RDONLY    equ 32          ; a save was refused for good (write-protected,
                                ; full): the state stays in memory (107.5.1)
DIA_S_WORKER    equ 64          ; no OSAPI_WM_TIMER (kern_small): the tick is
                                ; a worker's wake (SPEC.md 107.5.2)
DIA_S_HIRED     equ 128         ; ...and the worker is running
; dia_fl bits
DIA_F_ASKING    equ 1           ; the damaged-file question is on the glass
DIA_F_CLOSING   equ 2           ; dia_close's save: never ask from there
DIA_F_PLACED    equ 4           ; the template's place came from the file
DIA_F_KEPT      equ 8           ; a close was declined once (dia_close)
DIA_BKMAX       equ 16          ; a refusal that may clear waits 1, 2, 4, 8,
                                ; then 16 polls before the next try

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
dia_bk      equ dia_py + 2                                  ; the backoff, polls
dia_bn      equ dia_bk + 1                                  ; ...still to wait
dia_fnd     equ dia_bn + 1                                  ; the walk's FIND
                                ; buffer - its OWN: dia_rec is the registered
                                ; row from the first tick to the close, and
                                ; the close removes by it (SPEC.md 107.5)
dia_fl      equ dia_fnd + OSAPI_FIND_SZ                     ; DIA_F_* (wave 2)
dia_wy      equ dia_fl + 1                                  ; the restored y
DIA_BSS_END equ DIA_BSS + DIA_HDR_SIZE + DIA_PAYLEN + OSAPI_DI_SIZE + 13 + 2 + 1 + 1 + 2 + 2 + 2 + 1 + 1 + OSAPI_FIND_SZ + 1 + 2
%if OSAPI_DI_SIZE < DIA_SLACK
  %error "os88dia: dia_rec is the read's slack before it is the row"
%endif

; -----------------------------------------------------------------------------
; dia_load - the entry proc's half (SPEC.md 107.5, 107.5.2)
; in:  SI = the loader's name (an offset in KERNEL_SEG), DI = the template
; out: CF = 1 another instance's window is up and its toast has been said:
;      return CF = 1 from the entry proc. CF = 0 the state file is read and
;      the template's x,y are the saved ones - anchored to the nearer edges
;      when they were saved on a screen of another size. Every register but
;      the flags kept.
; -----------------------------------------------------------------------------
dia_load:
    call dia_single             ; FIRST, before any I/O: a refused second
    jnc .one                    ; launch costs no disk call
    ret
.one:
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
    call OSAPI_FILE_HERE        ; HOME, banked ONCE: every save comes back
    mov [dia_hdx], dx           ; here, and asks whether this is still it
    mov [dia_hbl], bl           ; (SPEC.md 107.5.1)
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
    cmp ax, FERR_IO             ; makes it
    ja .bad                     ; the FILE's fault (too big, not ours)
    or byte [dia_st], DIA_S_RDONLY  ; FERR_NODISK / FERR_IO: UNREAD - nothing
    jmp short .zero             ; is known about it, so it is neither damaged
                                ; nor ever written over this session; the
                                ; next launch reads it again (SPEC.md 107.5)
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
    pop di                      ; the template
    push di
    call OSAPI_VIDEO            ; AX = width, BX = height
    mov cx, [dia_file+DIA_H_SW]
    mov dx, [di+WT_W]
    mov si, [dia_file+DIA_H_X]
    call dia_anchor             ; SI = x on this screen
    mov [di+WT_X], si
    mov ax, bx
    mov cx, [dia_file+DIA_H_SH]
    mov dx, [di+WT_H]
    mov si, [dia_file+DIA_H_Y]
    call dia_anchor
    mov [di+WT_Y], si           ; wm_create clamps it onto the glass (39.7)
    mov [dia_wy], si            ; ...and dia_made gives back what that clamp
    or byte [dia_fl], DIA_F_PLACED  ; took to keep it off the dock
    jmp short .home
.bad:
    or byte [dia_st], DIA_S_DAMAGED
.zero:
    push ds                     ; DAMAGED: run empty, and never overwrite it
    pop es                      ; silently (dia_save asks first, 107.5.2)
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

; dia_anchor - one axis of a saved place, onto this screen (SPEC.md 107.5.2)
; in:  SI = the saved coordinate, CX = the screen's extent when it was saved,
;      AX = the extent now, DX = the frame's template extent on that axis
; out: SI = the coordinate now: unchanged when the screen is the same size or
;      the frame's middle was in the near half, else the same distance from
;      the FAR edge; never negative. Every other register kept
dia_anchor:
    cmp ax, cx
    je .x                       ; the same screen: the saved place as it is
    push dx
    shr dx, 1                   ; w/2
    add dx, si                  ; the frame's middle, then
    push cx
    shr cx, 1
    cmp dx, cx                  ; ...against the old screen's
    pop cx
    pop dx
    jl .x                       ; the near half: keep the distance from 0
    sub si, cx                  ; x - oldW, then + newW: the same distance
    add si, ax                  ; from the far edge
    jns .x
    xor si, si
.x:
    ret

; dia_single - is another instance of this package up? (SPEC.md 107.5.2) Its
; window's segment carries OUR program name at +16. CF = 1 yes, and the toast
; is said; every register kept
dia_single:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    xor ax, ax
.slot:
    push ax
    call OSAPI_WM_OWNSEG        ; DX = the owning segment, or CF = 1 none
    pop ax
    jc .next
    mov cx, ds
    cmp dx, cx
    je .next                    ; ours (none yet, at the entry proc - but a
    mov es, dx                  ; caller may ask later)
    mov si, 16                  ; +16 the program's name, 16 bytes
    mov di, si
    mov cx, 8
    cld
    repe cmpsw
    je .found
.next:
    inc ax
    cmp al, MAX_WIN
    jb .slot
    clc
    jmp short .out
.found:
    push ds
    pop es
    mov si, dia_isopen
    xor cx, cx
    call OSAPI_TOAST            ; the entry proc's own toast: the loader then
    stc                         ; says nothing over it (SPEC.md 20.12.4)
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dia_made - after OSAPI_WM_CREATE: BX = the window. Installs the tick and the
; close negotiator; every register kept, the flags not - clear CF yourself
; before the entry proc returns
; -----------------------------------------------------------------------------
dia_made:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov [dia_win], bx
    mov ax, KERNEL_SEG          ; the record (SPEC.md 20.1)
    mov es, ax
    test byte [dia_fl], DIA_F_PLACED
    jz .rec
    mov ax, [dia_wy]            ; THE RESTORED y. wm_create's fit keeps every
    cmp ax, [es:bx+W_Y]         ; frame that fits above the dock, but a frame
    jle .rec                    ; DRAGGED half over it is left there on
    push ax                     ; purpose (wm_dock_snap) - and that is the
    call OSAPI_WM_DISPLAY       ; place the user chose. Give back what the fit
    mov cx, bx                  ; took, down to the lowest y whose frame and
    mov bx, [dia_win]           ; shadow are on this display - wm_fit's own
    pop ax                      ; floor for a KEEPH window, ph - h - 1 - and
    sub cx, [es:bx+W_H]         ; never past it (SPEC.md 107.5.2). The window
    dec cx                      ; is not shown yet; x is clamped the same
    cmp ax, cx                  ; either way and is not touched
    jle .put
    mov ax, cx
.put:
    cmp ax, [es:bx+W_Y]
    jle .rec
    mov [es:bx+W_Y], ax
.rec:
    mov ax, [es:bx+W_X]         ; THE PLACE ON RECORD is where it opened: the
    mov dx, [es:bx+W_Y]         ; poll owes a save only for another one, so a
    mov [dia_px], ax            ; launch alone writes nothing (107.5.2). The
    mov [dia_py], dx            ; header's screen goes with it, or a later
    mov [dia_file+DIA_H_X], ax  ; save would pair this place with the old
    mov [dia_file+DIA_H_Y], dx  ; screen; DIA_HF_POS is left as the file had
    call OSAPI_VIDEO            ; it, so a close still writes a missing file
    mov [dia_file+DIA_H_SW], ax
    mov [dia_file+DIA_H_SH], bx
    mov bx, [dia_win]
    mov ax, dia_tick
    call OSAPI_WM_ONTIMER
    mov ax, dia_close
    call OSAPI_WM_ONCLOSE
    mov ax, 1                   ; the first tick: the window is up by then
    call OSAPI_WM_TIMER         ; CF = 1: kern_small, which has no timer -
    jnc .t                      ; the WORKER ARM (SPEC.md 107.5.2): the
    or byte [dia_st], DIA_S_WORKER  ; first paint hires it, and its wake is
    mov ax, dia_wake            ; the tick
    call OSAPI_WM_ONWAKE
.t:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_painted - the end of the package's W_PAINT (lock held). On the worker
; arm, hire the worker once: OSAPI_TASK_SPAWN wants the lock held and the
; instance published, which the entry proc has neither of (apps/taskmgr's
; rule). A refusal (the task table full) is retried by the next paint.
; Every register and the flags kept
dia_painted:
    pushf
    test byte [dia_st], DIA_S_WORKER
    jz .r
    test byte [dia_st], DIA_S_HIRED
    jnz .r
    push ax
    push bx
    mov ax, dia_worker
    mov bx, [dia_win]
    call OSAPI_TASK_SPAWN
    jc .no
    or byte [dia_st], DIA_S_HIRED
    OS88_WORKER_RESTARTABLE dia_worker  ; a pass of the loop is all a restart
.no:                            ; costs: it parks in OSAPI_TASK_ALIVE only
    pop bx
    pop ax
.r:
    popf
    ret

; dia_touch - the payload changed: owe a save, which the next tick makes.
; Every register and the flags kept
dia_touch:
    pushf
    or byte [dia_st], DIA_S_DIRTY
    popf
    ret

; dia_worker - kern_small's tick: sleep, ask to live, kick the UI task. It
; touches no file and no pixel (SPEC.md 20.6 rule 7); dia_wake does both
dia_worker:
    mov ax, DIA_TICKS
    call OSAPI_TASK_SLEEP
    mov bx, [dia_win]
    call OSAPI_TASK_ALIVE       ; never comes back once the window is closing
    call OSAPI_WM_WAKE          ; one queued per window: a busy UI task loses
    jmp short dia_worker        ; nothing by it

; dia_wake - the worker's kick (SI = the window, the UI task, NO lock): take
; the lock so the beat - and DIA_ONTICK - sees kern_big's environment exactly
dia_wake:
    call OSAPI_GFX_LOCK
    call dia_beat
    call OSAPI_GFX_UNLOCK
    ret

; -----------------------------------------------------------------------------
; dia_tick - the timer (SI = the window; the UI task, the gfx lock held)
; -----------------------------------------------------------------------------
dia_tick:
    call dia_beat
    push ax
    push bx
    mov bx, [dia_win]
    mov ax, DIA_TICKS
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret

; dia_beat - one tick's work, on either arm: the UI task, the lock held
dia_beat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    test byte [dia_st], DIA_S_TRIED | DIA_S_WORKER
    jnz .poll                   ; (kern_small's OSAPI_DESK_ITEM is the
    or byte [dia_st], DIA_S_TRIED   ; refusal: its path walk would buy nothing)
    call dia_register           ; EVERY launch, whatever the state file said:
.poll:                          ; the row is the program's (SPEC.md 107.5)
    mov ax, KERNEL_SEG          ; ES = KERNEL_SEG on entry to a callback -
    mov es, ax                  ; and the registration's ES = DS since
    mov si, [dia_win]
    mov ax, [es:si+W_X]
    mov dx, [es:si+W_Y]
    cmp ax, [dia_px]
    jne .moved                  ; still moving, or just started
    cmp dx, [dia_py]
    jne .moved
    cmp ax, [dia_file+DIA_H_X]  ; two polls agree: against the place on
    jne .new                    ; record - where it was saved, or where it
    cmp dx, [dia_file+DIA_H_Y]  ; opened (dia_made) - and only another place
    je .app                     ; owes a save: a launch alone writes nothing
.new:
    call dia_pos                ; two polls agree: the drag is over
.moved:
    mov [dia_px], ax
    mov [dia_py], dx
.app:
%ifdef DIA_ONTICK
    call DIA_ONTICK             ; CF = 1: the package is mid-edit - any save
    jc .arm                     ; owed waits for a later poll (SPEC.md 107.5)
%endif
    test byte [dia_st], DIA_S_DIRTY
    jz .arm
    cmp byte [dia_bn], 0
    je .save
    dec byte [dia_bn]           ; backing off a refusal that may clear
    jmp short .arm
.save:
    call dia_save
.arm:
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
; 107.4): the row stays, which is what restore wants. A remove REFUSED - no
; system disk in A: to read CTRL.DRV from, whose toast asks for it - declines
; the FIRST close (CF = 1), so the user can put it back and close again; the
; next one goes ahead whatever the remove says, so the window can never be
; made unclosable (SPEC.md 75.1). DIA_ONCLOSE can therefore run twice
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
    jnc .gone
    test byte [dia_fl], DIA_F_KEPT
    jnz .gone                   ; refused twice: close anyway, the row stays
    or byte [dia_fl], DIA_F_KEPT
    stc                         ; refused once: stay up, save nothing - the
    jmp short .ret              ; disk is not home either
.gone:
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
    or byte [dia_fl], DIA_F_CLOSING ; never ask from here: the alert would
    call dia_save               ; die with this package (SPEC.md 107.5.2)
.out:
    clc
.ret:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_save - the state file, header and payload in one write (SPEC.md
; 107.5.1). Not with no home, not once a refusal was for good, and not over a
; DAMAGED file until the user has said Yes to DIA_DAMAGEDQ (107.5.2). A
; refusal that may clear keeps it owed and backs the next try off; one that
; will not makes the state read-only for the session. Keeps every register
dia_save:
    test byte [dia_st], DIA_S_NOHOME | DIA_S_RDONLY
    jnz .r
    test byte [dia_st], DIA_S_DAMAGED
    jz .go
%ifdef DIA_DAMAGEDQ
    test byte [dia_fl], DIA_F_ASKING | DIA_F_CLOSING
    jnz .r                      ; asked already, or closing: not now
    push ax
    push bx
    push si
    push di
    push es
    mov ax, KERNEL_SEG          ; os88ui_ask reads our window's title out of
    mov es, ax                  ; the record, in KERNEL_SEG
    mov al, OS88UI_AYESNO
    mov bx, [dia_win]
    mov si, dia_damq
    mov di, dia_dans
    call os88ui_ask             ; CF = 1: another alert of ours is up - ask
    jc .busy                    ; at a later tick
    or byte [dia_fl], DIA_F_ASKING
.busy:
    pop es
    pop di
    pop si
    pop bx
    pop ax
%endif
.r:
    ret
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call dia_enter              ; CF = 1: not home now - the disk is out, or
    jc .soon                    ; another one is in
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
    call dia_leave              ; AX and the flags kept
    jnc .ok
    cmp ax, FERR_IO
    jbe .soon                   ; FERR_NODISK, FERR_IO: may clear
    or byte [dia_st], DIA_S_RDONLY  ; write-protected, full, a full folder,
.ok:                            ; a protected name: will not, this session
    and byte [dia_st], ~DIA_S_DIRTY & 0xFF
    mov byte [dia_bk], 0
    jmp short .x
.soon:
    mov al, [dia_bk]            ; 1, 2, 4, 8, then 16 polls (2 s to 32 s)
    shl al, 1
    jnz .k
    inc ax
.k:
    cmp al, DIA_BKMAX
    jbe .s
    mov al, DIA_BKMAX
.s:
    mov [dia_bk], al
    mov [dia_bn], al
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef DIA_DAMAGEDQ
; dia_dans - the damaged-file question's answer (AL = 0 Yes, 1 No, or
; OS88UI_ACANCEL; the lock held). Yes: the file is ours to replace, and the
; next tick writes it whole. Anything else: read-only for the session
dia_dans:
    and byte [dia_fl], ~DIA_F_ASKING & 0xFF
    or al, al
    jnz .no
    and byte [dia_st], ~DIA_S_DAMAGED & 0xFF
    or byte [dia_st], DIA_S_DIRTY
    ret
.no:
    or byte [dia_st], DIA_S_RDONLY
    and byte [dia_st], ~DIA_S_DIRTY & 0xFF
    ret
%endif

; dia_enter - walk to SYSTEM\APPDATA on HOME's volume, having asked whether
; the disk in the drive is still home (SPEC.md 107.5.1). ONE full
; OSAPI_FILE_GOTO first - the disk may have been swapped, and only a full
; mount re-reads its FAT (18.8.2 cannot tell two os8088 disks apart) - then
; the quiet stand that moves the instance, a word inside the volume just
; mounted. out CF = 1 not there, and the instance is home again
dia_enter:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [dia_hbl]
    xor dx, dx
    call OSAPI_FILE_GOTO        ; the root, mounted afresh
    jc .fail                    ; ...and not marked: still home, nothing to
    mov dx, [dia_hdx]           ; undo
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov si, dia_name
    call dia_seek               ; IS THIS HOME? our own file in our folder
    jc .no
    cmp ax, OSAPI_FT_DIR
    je .no
    mov bl, [dia_hbl]
    xor dx, dx
    call OSAPI_FILE_GOTO_QM     ; the root again, a word
    jc .no
    mov si, dia_dsys
    call dia_dive
    jc .no
    mov si, dia_dapp
    call dia_dive
    jc .no
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    call dia_leave
.fail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; dia_leave - home, quietly; every register and the flags kept
dia_leave:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [dia_hdx]
    mov bl, [dia_hbl]
    call OSAPI_FILE_GOTO_QM     ; CX, SI and DI are a file driver's (os88api)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; dia_dive - into the folder named at SI, in the current one; CF = 1 none
dia_dive:
    push ax
    push bx
    push dx
    call dia_seek               ; DX = its first cluster
    jc .x
    cmp ax, OSAPI_FT_DIR
    stc
    jne .x                      ; a FILE of that name: not a folder
    mov bl, [dia_hbl]
    call OSAPI_FILE_GOTO_QM
.x:
    pop dx
    pop bx
    pop ax
    ret

; dia_seek - the entry named SI in the current folder, into dia_fnd. out CF = 0
; AX = its type (+14), DX = its first cluster (+16); CF = 1 there is none
dia_seek:
    push cx
    push di
    push es
    push ds
    pop es
    xor cx, cx
.each:
    mov di, dia_fnd
    call OSAPI_FILE_FIND
    jc .out
    push si
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
    mov ax, [dia_fnd + 14]
    mov dx, [dia_fnd + 16]
    clc
.out:
    pop es
    pop di
    pop cx
    ret

dia_dname   db DIA_DATNAME, 0
dia_dsys    db 'SYSTEM', 0
dia_dapp    db 'APPDATA', 0
%ifdef DIA_ISOPEN
dia_isopen  db DIA_ISOPEN, 0
%else
dia_isopen  db 'Already open', 0
%endif
%ifdef DIA_DAMAGEDQ
dia_damq    db DIA_DAMAGEDQ, 0
%endif
%strlen DIA_CAPLEN DIA_CAP
%if DIA_CAPLEN > 12             ; the row's caption is 13 bytes, its NUL in
  %error "os88dia: DIA_CAP is at most 12 characters"  ; them: +62 and +63 are
%endif                          ; the kernel's picture header (SPEC.md 26.8.1)
dia_cap     db DIA_CAP, 0
            times OSAPI_DI_BODY - OSAPI_DI_CAP - ($ - dia_cap) db 0

%endif ; OS88DIA_INC
