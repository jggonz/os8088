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
;   [dia_nx]    a byte: store n (1..35) from DIA_ONTICK and the NEXT beat is
;               n ticks out instead of 36, once - a second note of a chirp
;               (SPEC.md 107.3). The timer arm's; kern_small's worker has
;               begun its next sleep before the beat runs, and ignores it.
;
; SPEC.md 107.5 and 107.5.2 are the contract.
; =============================================================================

%ifndef OS88DIA_INC
%define OS88DIA_INC

DIA_HDR_MAGIC   equ 0x4144      ; 'DA' (SPEC.md 107.5)
DIA_HDR_VER     equ 1
DIA_HF_POS      equ 1           ; +3 bit 0: the position is valid
DIA_HF_CONT     equ 2           ; +3 bit 1: +4/+6 are the FACE's origin, not
                                ; a titled frame's (v2, SPEC.md 107.11) - only
                                ; a DIA_WIDGET build writes it
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
dia_nx      equ dia_wy + 2                                  ; the next beat,
                                ; ticks, once - 0: DIA_TICKS (wave 4)
%ifndef DIA_WIDGET
DIA_BSS_END equ DIA_BSS + DIA_HDR_SIZE + DIA_PAYLEN + OSAPI_DI_SIZE + 13 + 2 + 1 + 1 + 2 + 2 + 2 + 1 + 1 + OSAPI_FIND_SZ + 1 + 2 + 1
%else
; THE WIDGET'S OWN (SPEC.md 107.11), after v1's - so every v1 offset above
; is where it was, and tools/os88dia.py's widget_off() names these
dia_wf      equ dia_nx + 1      ; db DIA_W_*: the form, the focus, the x armed
dia_hv      equ dia_wf + 1      ; db the hover state, DIA_HV_*
dia_tagp    equ dia_hv + 1      ; db the tag's picture on the glass, DIA_P_*
dia_press   equ dia_tagp + 1    ; db the card a press went to, + 1 (0 none)
dia_li      equ dia_press + 1   ; db the layout: 0 TALL, 1 MID, 2 COMPACT
dia_ci      equ dia_li + 1      ; db the paint's card
dia_lay     equ dia_ci + 1      ; dw ...its DIA_LAYOUT entry
dia_fh      equ dia_lay + 2     ; dw the face's height as SHOWN: the whole
                                ; column on a desk window, less the tag row
                                ; and the last gap in the titled fallback
dia_fx      equ dia_fh + 2      ; dw the face's screen origin, as the last
dia_fy      equ dia_fx + 2      ; dw ...paint, press or draw found it
dia_cy      equ dia_fy + 2      ; dw the card being painted: its screen top
dia_ch      equ dia_cy + 2      ; dw ...and its height
dia_dmg     equ dia_ch + 2      ; 4 dw: the paint's damage rect, inclusive
dia_pp      equ dia_dmg + 8     ; dw the paint's walk over the layout's pairs
dia_gh      equ dia_pp + 2      ; dw a gap's rows
dia_tbeat   equ dia_gh + 2      ; dw the next BEAT, an absolute tick
dia_thov    equ dia_tbeat + 2   ; dw ...the dwell's or the linger's
dia_qx      equ dia_thov + 2    ; dw a press or release point, banked
dia_qy      equ dia_qx + 2
dia_pre     equ dia_qy + 2      ; db the hover state BEFORE a press
dia_sc      equ dia_pre + 1     ; db dia_slabs: the card
dia_ss      equ dia_sc + 1      ; dw ...the slab's first card row
dia_sr0     equ dia_ss + 2      ; dw ...the damaged rows, card-local
dia_sr1     equ dia_sr0 + 2
dia_ty      equ dia_sr1 + 2     ; dw the tag row's screen top, as drawn
DIA_BSS_END equ dia_ty + 2 - os88_image_end
%endif
%if OSAPI_DI_SIZE < DIA_SLACK
  %error "os88dia: dia_rec is the read's slack before it is the row"
%endif

; DIA_PLACE reg - AX, DX = the place a window's record means to the state
; file: its frame's x,y (v1), or its FACE's origin (DIA_WIDGET, SPEC.md
; 107.11). `reg` holds the record, ES = KERNEL_SEG; nothing else changes
%macro DIA_PLACE 1
%ifdef DIA_WIDGET
  %ifidni %1, si
    call dia_place
  %else
    push si
    mov si, %1
    call dia_place
    pop si
  %endif
%else
    mov ax, [es:%1+W_X]
    mov dx, [es:%1+W_Y]
%endif
%endmacro

%ifdef DIA_WIDGET
; --- THE WIDGET TREATMENT (SPEC.md 107.11) -----------------------------------
%ifndef GFXE_TEXT
  %error "os88dia: DIA_WIDGET composes with os88gfx.inc - define GFXE_TEXT"
%endif
%ifndef GFXE_BITS
  %error "os88dia: DIA_WIDGET composes with os88gfx.inc - define GFXE_BITS"
%endif
%ifndef DIA_TAG
  %error "os88dia: DIA_WIDGET wants DIA_TAG, the name tag (= the title)"
%endif
%ifndef DIA_LAYOUTS
  %error "os88dia: DIA_WIDGET wants DIA_LAYOUTS - three DIA_LAYOUT entries"
%endif
%ifndef DIA_PAINTCARD
  %error "os88dia: DIA_WIDGET wants DIA_PAINTCARD (dia_slabs + DIA_COMPOSE)"
%endif
%ifndef DIA_FACE_W
  %define DIA_FACE_W 176        ; 22 cells, the mockup's column
%endif
%ifndef DIA_WAKE
  %define DIA_WAKE dia_wake
%endif
%if DIA_FACE_W % 8
  %error "os88dia: DIA_FACE_W must be a multiple of 8 (BLIT1's grid on small)"
%endif
%if DIA_FACE_W > GFXE_BAND_W
  %error "os88dia: the band is narrower than the face (GFXE_BAND_W)"
%endif
DIA_HOVER_T equ 5               ; the dock's DOCK_HOVER_T and DOCK_LEAVE_T
DIA_LEAVE_T equ 14              ; (kernel/dock.inc, SPEC.md 30.6) by value -
                                ; tests/unit/t_dia.py holds them equal
DIA_FOCUS_T equ 4               ; the focus poll, only while owning the bar
DIA_HV_HIDDEN equ 0             ; dia_hv: the handle down...
DIA_HV_DWELL  equ 1             ; ...the pointer on it, the dwell running
DIA_HV_SHOWN  equ 2             ; ...the handle up
DIA_HV_LINGER equ 3             ; ...up, the pointer gone, the linger running
DIA_W_DESK  equ 1               ; dia_wf: a desk window (MAKE took)
DIA_W_FOCUS equ 2               ; ...owning the menu bar and the keys
DIA_W_ARMED equ 4               ; ...a press on the handle's x is armed
DIA_P_REST  equ 0               ; dia_tagp: the plate,
DIA_P_FOCUS equ 1               ; ...the plate inverse,
DIA_P_HAND  equ 2               ; ...the handle,
DIA_P_ARMED equ 3               ; ...the handle with its x lit,
DIA_P_NONE  equ 0FFh            ; ...or not known: the next sync draws
DIAL_N      equ 0               ; a DIA_LAYOUT entry: db the cards,
DIAL_TAG    equ 1               ; db the tag row's height,
DIAL_FY     equ 2               ; dw the default face y,
DIAL_CARDS  equ 4               ; then (db height, db the gap under it) x 4
DIAL_MAX    equ 4
DIAL_SZ     equ DIAL_CARDS + 2 * DIAL_MAX
DIA_XX      equ 3               ; the handle's close box: face x 3..12
DIA_XW      equ 10
%strlen DIA_TAGLEN DIA_TAG
DIA_TAGW    equ DIA_TAGLEN * 8
DIA_PW      equ DIA_TAGW + 8    ; the plate: the name and 4 px each side
DIA_PX      equ (DIA_FACE_W - DIA_PW) / 2
DIA_NX      equ (DIA_FACE_W - DIA_TAGW) / 2     ; the handle's name
DIA_GX      equ DIA_FACE_W - 18                 ; ...and its grip
%if DIA_PW > DIA_FACE_W
  %error "os88dia: DIA_TAG is wider than the face"
%endif
%if DIA_NX < DIA_XX + DIA_XW + 1 || DIA_NX + DIA_TAGW > DIA_GX - 1
  %error "os88dia: DIA_TAG crowds the handle's x or its grip"
%endif

; DIA_LAYOUT band, facey, tagh, h0,g0 [, h1,g1 [, h2,g2 [, h3,g3]]] - one
; layout: each card with the gap UNDER it; refused at assembly time when the
; face, or the titled fallback's frame, is taller than `band`
%macro DIA_LAYOUT 5-11
  %if (%0 - 3) % 2
    %error "DIA_LAYOUT: a height and a gap per card"
  %endif
  %assign %%n (%0 - 3) / 2
  %assign %%band %1
  %assign %%tag %3
  %if %%tag < 10 || %%tag > GFXE_BAND_H
    %error "DIA_LAYOUT: the tag row is 10 rows or more, and no taller than the band"
  %endif
    db %%n, %3
    dw %2
  %assign %%sum %3
  %assign %%last 0
  %rotate 3
  %rep %%n
    db %1, %2
    %assign %%sum %%sum + %1 + %2
    %assign %%last %2
    %rotate 2
  %endrep
    times 2 * (DIAL_MAX - %%n) db 0
  %if %%sum > %%band
    %error "DIA_LAYOUT: the face is taller than its band"
  %endif
  %if %%sum - %%tag - %%last + TITLE_H + 1 > %%band
    %error "DIA_LAYOUT: the titled fallback is taller than its band"
  %endif
%endmacro
%endif ; DIA_WIDGET

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
%ifdef DIA_WIDGET
    call dia_wsize              ; QUERY, the layout, the template's size and
                                ; its default place (SPEC.md 107.11)
%endif
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
%ifdef DIA_WIDGET
    call dia_wplace             ; the FACE's place, anchored, into the
                                ; template's frame - SI = its y
%else
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
%endif
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
%ifdef DIA_WIDGET
    call dia_wmade              ; MAKE, or the titled fallback's promises; the
                                ; prefilters and the wake (SPEC.md 107.11)
%endif
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
    DIA_PLACE bx                ; THE PLACE ON RECORD is where it opened: the
                                ; poll owes a save only for another one, so a
%ifdef DIA_WIDGET
    or byte [dia_file+DIA_H_FLAGS], DIA_HF_CONT ; ...a FACE's from now on: a
                                ; v1 file is converted by its next save, once
%endif
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
%ifndef DIA_WIDGET              ; (a widget's is below: the hover edge's too)
dia_wake:
    call OSAPI_GFX_LOCK
    call dia_beat
    call OSAPI_GFX_UNLOCK
    ret
%endif

; -----------------------------------------------------------------------------
; dia_tick - the timer (SI = the window; the UI task, the gfx lock held)
; -----------------------------------------------------------------------------
%ifndef DIA_WIDGET              ; (a widget's multiplexes, below)
dia_tick:
    call dia_beat
    push ax
    push bx
    mov bx, [dia_win]
    xor ax, ax
    xchg al, [dia_nx]           ; a sooner beat the application asked for,
    or al, al                   ; once (SPEC.md 107.5)
    jnz .t
    mov al, DIA_TICKS
.t:
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret
%endif

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
%ifdef DIA_WIDGET
    call dia_refit              ; a drop that left the tag off the glass
%endif                          ; (SPEC.md 11.103.6's bound)
    DIA_PLACE si
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
%ifdef DIA_WIDGET
    or byte [dia_file+DIA_H_FLAGS], DIA_HF_POS | DIA_HF_CONT
%else
    or byte [dia_file+DIA_H_FLAGS], DIA_HF_POS
%endif
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
    DIA_PLACE si
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
    call OSAPI_WM_CLIP_CLEAR    ; DIA_ONTICK may have left a clip armed this
                                ; beat, and the alert's create recomposes
                                ; the bar through it (SPEC.md 107.9)
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

%ifdef DIA_WIDGET
; =============================================================================
; THE WIDGET TREATMENT (SPEC.md 107.11) - the face, its tag and its gestures.
; No pixel of it is the kernel's: the kernel's part is SPEC.md 11.103's layer
; =============================================================================

; dia_wsize - dia_load's first step (DI = the template): QUERY, the layout by
; the screen's HEIGHT, the template's size, and - with DIA_RIGHT - its default
; place. Every register kept
dia_wsize:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, 0
    call OSAPI_WM_DESK          ; CF = 0: this kernel has the desk layer
    jc .q
    or byte [dia_wf], DIA_W_DESK
.q:
    call OSAPI_VIDEO            ; AX = width, BX = height
    push ax
    xor cx, cx                  ; TALL at 400 rows or more, MID from 300 -
    cmp bx, 400                 ; Hercules' 348 AND EGA's 350, which the
    jae .l                      ; adapter's kind would have called VGA -
    inc cx                      ; COMPACT below (SPEC.md 107.11)
    cmp bx, 300
    jae .l
    inc cx
.l:
    mov [dia_li], cl
    mov ax, DIAL_SZ
    mul cx
    add ax, DIA_LAYOUTS
    mov si, ax
    mov [dia_lay], si
    call dia_fsum               ; DX = the whole column, CX = tag + last gap
    test byte [dia_wf], DIA_W_DESK
    jnz .d
    sub dx, cx                  ; the titled fallback: no tag, no last gap
    mov [dia_fh], dx
    add dx, TITLE_H + 1
    jmp short .h
.d:
    mov [dia_fh], dx
.h:
    mov [di+WT_H], dx
    mov word [di+WT_W], DIA_FACE_W + 2
    pop ax
%ifdef DIA_RIGHT
    sub ax, DIA_RIGHT + 1       ; the default place: the face DIA_RIGHT in
    mov [di+WT_X], ax           ; from the right edge, at the layout's y -
    mov ax, [si+DIAL_FY]        ; a state file's place overrides it
    test byte [dia_wf], DIA_W_DESK
    jnz .y
    sub ax, TITLE_H
.y:
    mov [di+WT_Y], ax
%endif
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_fsum - SI = a layout entry: DX = its whole face height, CX = its tag row
; plus its last gap. Keeps the rest
dia_fsum:
    push ax
    push bx
    xor dx, dx
    xor ax, ax
    mov cl, [si+DIAL_N]
    xor ch, ch
    lea bx, [si+DIAL_CARDS]
.s:
    mov al, [bx]
    add dx, ax
    mov al, [bx+1]
    add dx, ax
    add bx, 2
    loop .s
    mov cl, [si+DIAL_TAG]       ; AL = the last gap
    add dx, cx
    add cx, ax
    pop bx
    pop ax
    ret

; dia_wplace - dia_load's restored place (AX, BX = the screen, DI = the
; template): the FACE's origin out of the header - a v1 file's titled frame
; converted - anchored with the face's size, into the template's FRAME. Out
; SI = the frame's y; AX..DX spent
dia_wplace:
    mov si, [dia_file+DIA_H_X]
    mov dx, [dia_file+DIA_H_Y]
    test byte [dia_file+DIA_H_FLAGS], DIA_HF_CONT
    jnz .f
    inc si                      ; v1: a titled frame, so the face is a
    add dx, TITLE_H             ; border in and a title bar down
.f:
    push dx
    mov cx, [dia_file+DIA_H_SW]
    mov dx, DIA_FACE_W
    call dia_anchor
    dec si                      ; the face is one column in from the frame,
    mov [di+WT_X], si           ; in both forms
    pop si
    mov ax, bx
    mov cx, [dia_file+DIA_H_SH]
    mov dx, [dia_fh]
    call dia_anchor
    test byte [dia_wf], DIA_W_DESK
    jnz .d
    sub si, TITLE_H             ; wm_create clamps it onto the glass
.d:
    mov [di+WT_Y], si
    ret

; dia_wmade - dia_made's first step (BX = the window, ES = KERNEL_SEG): MAKE,
; or the titled fallback's two promises; the prefilters and the wake. Every
; register kept
dia_wmade:
    push ax
    test byte [dia_wf], DIA_W_DESK
    jz .t
    mov al, 1
    call OSAPI_WM_DESK          ; MAKE: we are in the entry proc, the window
    jnc .cb                     ; hidden and nobody's yet (SPEC.md 11.103.9)
    and byte [dia_wf], ~DIA_W_DESK & 0FFh   ; refused: painted as titled
.t:
    mov al, 1
    call OSAPI_WM_OWNBG         ; every pixel is ours (OSAPI_WM_DAMAGE wants it)
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP   ; ...and two colours, as v1
    call OSAPI_WM_SAVEU
.cb:
    mov ax, dia_click
    call OSAPI_WM_ONCLICK
    mov ax, dia_mup
    call OSAPI_WM_ONMOUSEUP
    mov ax, DIA_WAKE            ; the hover edge's wake (kern_small's worker
    call OSAPI_WM_ONWAKE        ; arm puts its own over it)
    mov byte [dia_tagp], DIA_P_NONE
    call OSAPI_GET_TICKS
    mov [dia_tbeat], ax         ; the first beat is due at once
    pop ax
    ret

; dia_place - SI = our window (ES = KERNEL_SEG): AX, DX = the FACE's origin.
; Nothing else changes
dia_place:
    mov ax, [es:si+W_X]
    inc ax
    mov dx, [es:si+W_Y]
    test byte [dia_wf], DIA_W_DESK
    jnz .r
    add dx, TITLE_H
.r:
    ret

; dia_org - dia_place, kept in [dia_fx]/[dia_fy] for the painter
dia_org:
    call dia_place
    mov [dia_fx], ax
    mov [dia_fy], dx
    ret

; dia_refit - the beat's (SI = the window, ES = KERNEL_SEG, lock held): a
; desk window dropped with its bottom past its display - ui_drag's bound is
; the title bar's (SPEC.md 11.103.6) - is put back on it before the place is
; saved. There is no move cell, so it is hidden, moved and shown: two
; repaints, once, after a drop nobody should make. Every register kept
dia_refit:
    test byte [dia_wf], DIA_W_DESK
    jz .r
    test byte [es:si+W_FLAGS], 2
    jz .r
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_DISPLAY       ; BX = the display's height (AX CX SI DX)
    pop si
    mov ax, [es:si+W_Y]
    add ax, [es:si+W_H]
    cmp ax, bx
    jle .x                      ; on the glass
    sub bx, [es:si+W_H]
    cmp bx, MBAR_H
    jge .y
    mov bx, MBAR_H
.y:
    push bx
    mov bx, si
    call OSAPI_WM_HIDE
    pop ax
    mov [es:si+W_Y], ax
    call OSAPI_WM_SHOW
.x:
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; dia_paint - W_PAINT (SI = the window, lock held, the kernel's clip armed):
; only what intersects OSAPI_WM_DAMAGE, every pixel once - the ground columns
; and the gaps by OSAPI_GFX_DESK, each card by DIA_PAINTCARD, the tag from
; the state - then dia_painted
; -----------------------------------------------------------------------------
dia_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_DAMAGE        ; CF = 1: the whole of it, AX..DX either way
    mov [dia_dmg], ax
    mov [dia_dmg+2], bx
    mov [dia_dmg+4], cx
    mov [dia_dmg+6], dx
    call dia_org                ; AX, DX = the face
    test byte [dia_wf], DIA_W_DESK
    jz .cards
    mov bx, dx                  ; the two ground columns, the column's height
    add dx, [dia_fh]
    dec dx
    dec ax
    mov cx, ax
    call dia_desk               ; at x - 1...
    add ax, DIA_FACE_W + 1
    mov cx, ax
    call dia_desk               ; ...and x + DIA_FACE_W
.cards:
    mov di, [dia_fy]            ; DI = the walk's screen y
    mov si, [dia_lay]
    lea ax, [si+DIAL_CARDS]
    mov [dia_pp], ax
    mov byte [dia_ci], 0
.card:
    mov bx, [dia_pp]
    xor ax, ax
    mov al, [bx]
    mov [dia_ch], ax
    mov [dia_cy], di
    mov ax, [dia_fx]
    mov bx, di
    mov cx, ax
    add cx, DIA_FACE_W - 1
    mov dx, di
    add dx, [dia_ch]
    dec dx
    call dia_cut                ; CF = 1: the damage misses it
    jc .gap
    sub bx, di                  ; card-local rows
    sub dx, di
    mov al, [dia_ci]
    push di
    call DIA_PAINTCARD
    pop di
.gap:
    add di, [dia_ch]
    mov bx, [dia_pp]
    xor ax, ax
    mov al, [bx+1]
    or ax, ax
    jz .next
    mov [dia_gh], ax
    mov bx, [dia_lay]
    mov cl, [dia_ci]
    inc cl
    cmp cl, [bx+DIAL_N]
    jb .g                       ; the last gap is the tag's - the titled
    test byte [dia_wf], DIA_W_DESK  ; fallback has neither
    jz .next
.g:
    mov ax, [dia_fx]
    mov bx, di
    mov cx, ax
    add cx, DIA_FACE_W - 1
    mov dx, di
    add dx, [dia_gh]
    dec dx
    test byte [dia_wf], DIA_W_DESK
    jz .tg
    call dia_desk               ; the desktop, in the gap
    jmp short .gn
.tg:
    call dia_tgap               ; white, with a rule through its middle
.gn:
    add di, [dia_gh]
.next:
    add word [dia_pp], 2
    inc byte [dia_ci]
    mov bx, [dia_lay]
    mov al, [dia_ci]
    cmp al, [bx+DIAL_N]
    jb .card
    test byte [dia_wf], DIA_W_DESK
    jz .done
    xor ax, ax                  ; the tag row: drawn whole if the damage
    mov al, [bx+DIAL_TAG]       ; touches it
    mov bx, di
    mov dx, di
    add dx, ax
    dec dx
    mov ax, [dia_fx]
    mov cx, ax
    add cx, DIA_FACE_W - 1
    call dia_cut
    jc .done
    call dia_tagdraw
.done:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp dia_painted             ; tail: kern_small hires its worker there

; dia_cut - AX..DX (inclusive) intersected with the paint's damage: CF = 1
; empty, else AX..DX the intersection
dia_cut:
    cmp ax, [dia_dmg]
    jge .a
    mov ax, [dia_dmg]
.a:
    cmp bx, [dia_dmg+2]
    jge .b
    mov bx, [dia_dmg+2]
.b:
    cmp cx, [dia_dmg+4]
    jle .c
    mov cx, [dia_dmg+4]
.c:
    cmp dx, [dia_dmg+6]
    jle .d
    mov dx, [dia_dmg+6]
.d:
    cmp ax, cx
    jg .e
    cmp bx, dx
    jg .e
    clc
    ret
.e:
    stc
    ret

; dia_desk - the desktop's own ground over AX..DX, cut to the damage. Every
; register kept
dia_desk:
    push ax
    push bx
    push cx
    push dx
    call dia_cut
    jc .x
    call OSAPI_GFX_DESK
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_tgap - the titled fallback's gap (AX..DX = it, DI = its top, [dia_gh]
; its rows): white with a 1 px rule through its middle, composed and put down
; in ONE BLIT1 - never a fill and a line over it. Every register kept
dia_tgap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call dia_cut
    jc .x
    push bx
    push dx
    push di                     ; the composer spends DI
    mov ax, [dia_gh]
    mov [gfxe_bh], ax
    mov ax, 0FFFFh
    call gfxe_bclear            ; the gap's rows, white...
    mov byte [gfxe_tsty], 0
    xor ax, ax
    mov bx, [dia_gh]
    shr bx, 1
    mov cx, DIA_FACE_W
    mov dx, 1
    call gfxe_bfill             ; ...and the rule, black
    pop di
    pop dx
    pop bx
    mov si, bx
    sub si, di                  ; the band row the damage starts at
    sub dx, bx
    inc dx                      ; its rows
    mov ax, [dia_fx]
    mov cx, DIA_FACE_W
    call dia_blit
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_blit - the band onto the glass: AX = x, BX = y, CX = width, DX = rows,
; SI = the band row to start from. One OSAPI_GFX_BLIT1; every register kept
dia_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    push ax
    push dx
    mov ax, GFXE_BAND_ST
    mul si
    add ax, GFXE_BAND_BUF
    mov si, ax
    pop dx
    pop ax
    push ds
    pop es
    mov bp, GFXE_BAND_ST
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dia_slabs - a DIA_PAINTCARD that composes: AL = the card, BX..DX = its
; damaged rows (card-local); [dia_fx], [dia_cy], [dia_ch] as dia_paint left
; them. For each GFXE_BAND_H-row slab those rows touch: the band papered
; white, DIA_COMPOSE (AL = the card, BX = the slab's first card row, CX = its
; rows - compose at y = card row - BX), and the damaged rows of it blitted:
; ONE OSAPI_GFX_BLIT1 a slab. Every register kept
; -----------------------------------------------------------------------------
dia_slabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [dia_sc], al
    mov [dia_sr0], bx
    mov [dia_sr1], dx
    mov ax, bx
    xor dx, dx
    mov cx, GFXE_BAND_H
    div cx
    mul cx
    mov [dia_ss], ax            ; the first slab touched
.slab:
    mov cx, [dia_ch]
    sub cx, [dia_ss]
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; white paper: a card is never themed
%ifdef DIA_COMPOSE
    mov al, [dia_sc]
    mov bx, [dia_ss]
    mov cx, [gfxe_bh]
    call DIA_COMPOSE
%endif
    mov bx, [dia_sr0]           ; the rows of this slab that are damaged
    cmp bx, [dia_ss]
    jge .b0
    mov bx, [dia_ss]
.b0:
    mov dx, [dia_ss]
    add dx, [gfxe_bh]
    dec dx
    cmp dx, [dia_sr1]
    jle .b1
    mov dx, [dia_sr1]
.b1:
    mov si, bx
    sub si, [dia_ss]
    sub dx, bx
    inc dx
    add bx, [dia_cy]
    mov ax, [dia_fx]
    mov cx, DIA_FACE_W
    call dia_blit
    mov ax, [dia_ss]
    add ax, GFXE_BAND_H
    mov [dia_ss], ax
    cmp ax, [dia_sr1]
    jle .slab
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_want - AL = the tag picture the state asks for. Nothing else changes
dia_want:
    mov al, [dia_hv]
    cmp al, DIA_HV_SHOWN
    jb .plate
    mov al, DIA_P_HAND
    test byte [dia_wf], DIA_W_ARMED
    jz .r
    inc ax                      ; DIA_P_ARMED
    ret
.plate:
    mov al, DIA_P_REST
    test byte [dia_wf], DIA_W_FOCUS
    jz .r
    inc ax                      ; DIA_P_FOCUS
.r:
    ret

; -----------------------------------------------------------------------------
; dia_tagdraw - the tag row as the state asks ([dia_fx]/[dia_fy] the face,
; lock held, clipped): REST and FOCUS are ground | plate | ground - two
; OSAPI_GFX_DESK and one BLIT1 - and the handle ONE face-wide BLIT1. Every
; register kept
; -----------------------------------------------------------------------------
dia_tagdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call dia_want
    mov [dia_tagp], al
    mov bx, [dia_lay]
    xor cx, cx
    mov cl, [bx+DIAL_TAG]
    mov [gfxe_bh], cx
    mov dx, [dia_fy]
    add dx, [dia_fh]
    sub dx, cx
    mov [dia_ty], dx            ; the tag row's screen top
    cmp al, DIA_P_HAND
    jae .hand
    push ax                     ; --- the PLATE: white, or inverse focused
    xor bx, bx
    cmp al, DIA_P_FOCUS
    je .p
    dec bx
.p:
    mov ax, bx
    call gfxe_bclear
    pop ax
    mov bl, 0
    cmp al, DIA_P_FOCUS
    jne .s
    mov bl, GFXE_INV
.s:
    mov [gfxe_tsty], bl
    mov si, dia_tags
    mov cx, 4
    mov dx, [gfxe_bh]
    sub dx, 8
    shr dx, 1
    call gfxe_text
%if DIA_PX > 0
    mov ax, [dia_fx]            ; the ground left of it...
    mov cx, ax
    add cx, DIA_PX - 1
    call dia_tagrows
    call OSAPI_GFX_DESK
%endif
    mov ax, [dia_fx]            ; ...the plate...
    add ax, DIA_PX
    mov cx, DIA_PW
    jmp short .put
.hand:
    push ax                     ; --- the HANDLE: a black bar
    xor ax, ax
    call gfxe_bclear
    pop ax
    push ax
    cmp al, DIA_P_ARMED
    je .lit
    mov byte [gfxe_tsty], GFXE_INV  ; the close box white...
    mov ax, DIA_XX
    call dia_boxy
    mov cx, DIA_XW
    mov dx, DIA_XW
    call gfxe_bfill
    mov byte [gfxe_tsty], 0     ; ...with a black x
    jmp short .xm
.lit:
    mov byte [gfxe_tsty], GFXE_INV  ; armed: the box black, its x white
.xm:
    call dia_boxy
    mov dx, bx
    add dx, 2
    mov si, dia_xbits
    mov cx, DIA_XX + 2
    call gfxe_bits
    pop ax
    mov byte [gfxe_tsty], GFXE_INV  ; the name in white, centred
    mov si, dia_tags
    mov cx, DIA_NX
    mov dx, [gfxe_bh]
    sub dx, 8
    shr dx, 1
    call gfxe_text
    mov si, dia_grip            ; ...and the grip
    mov cx, DIA_GX
    mov dx, [gfxe_bh]
    sub dx, 7
    shr dx, 1
    call gfxe_bits
    mov ax, [dia_fx]
    mov cx, DIA_FACE_W
.put:
    mov bx, [dia_ty]
    mov dx, [gfxe_bh]
    xor si, si
    call dia_blit
    cmp byte [dia_tagp], DIA_P_HAND
    jae .out
%if DIA_PX + DIA_PW < DIA_FACE_W
    mov ax, [dia_fx]            ; ...and the ground right of it
    add ax, DIA_PX + DIA_PW
    mov cx, [dia_fx]
    add cx, DIA_FACE_W - 1
    call dia_tagrows
    call OSAPI_GFX_DESK
%endif
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_tagrows - BX, DX = the tag row's screen rows
dia_tagrows:
    mov bx, [dia_ty]
    mov dx, bx
    add dx, [gfxe_bh]
    dec dx
    ret

; dia_boxy - BX = the close box's first band row, (tag rows - 10) / 2
dia_boxy:
    mov bx, [gfxe_bh]
    sub bx, DIA_XW
    shr bx, 1
    ret

; dia_tagsync - the tag to what the state asks, drawn only on a change of the
; picture, under OSAPI_WM_CLIP_SET (lock held). Every register kept
dia_tagsync:
    test byte [dia_wf], DIA_W_DESK
    jz .r
    push ax
    push bx
    push dx
    push si
    push es
    call dia_want
    cmp al, [dia_tagp]
    je .x
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [dia_win]
    test byte [es:si+W_FLAGS], 2
    jz .x                       ; not on the glass: the next paint has it
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .x                       ; none of it showing
    call dia_org
    call dia_tagdraw
.x:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
.r:
    ret

; dia_hit - CX, DX = a screen point, [dia_fx]/[dia_fy] the face. CF = 0 a
; card: AL = it, CX, DX card-relative. CF = 1 the frame - a ground column, a
; gap or the tag row - and AH = 1 when the point is on the handle's x
dia_hit:
    push bx
    push si
    push bp
    sub cx, [dia_fx]
    sub dx, [dia_fy]
    mov si, [dia_lay]
    lea bx, [si+DIAL_CARDS]
    xor ax, ax
    cmp cx, DIA_FACE_W
    jae .no                     ; a ground column (unsigned: -1 too)
.c:
    cmp al, [si+DIAL_N]
    jae .tag
    push ax
    mov al, [bx]
    xor ah, ah
    mov bp, ax
    pop ax
    cmp dx, bp
    jl .card
    sub dx, bp
    push ax
    mov al, [bx+1]
    xor ah, ah
    mov bp, ax
    pop ax
    cmp dx, bp
    jl .no                      ; a gap
    sub dx, bp
    add bx, 2
    inc al
    jmp short .c
.tag:                           ; DX = the row in the tag row
    cmp cx, DIA_XX
    jl .no
    cmp cx, DIA_XX + DIA_XW
    jge .no
    xor ax, ax
    mov al, [si+DIAL_TAG]
    sub ax, DIA_XW
    shr ax, 1
    cmp dx, ax
    jl .no
    add ax, DIA_XW
    cmp dx, ax
    jge .no
    mov ax, 0100h
    jmp short .fr
.no:
    xor ax, ax
.fr:
    stc
    jmp short .out
.card:
    clc
.out:
    pop bp
    pop si
    pop bx
    ret

; dia_ctop - AL = a card: BX = its top, face-relative. Keeps the rest
dia_ctop:
    push ax
    push cx
    push si
    mov si, [dia_lay]
    lea si, [si+DIAL_CARDS]
    xor bx, bx
    mov cl, al
    xor ch, ch
    jcxz .r
    xor ax, ax
.l:
    mov al, [si]
    add bx, ax
    mov al, [si+1]
    add bx, ax
    add si, 2
    loop .l
.r:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dia_click - W_ONCLICK (SI = the window, CX, DX the press, lock held). A card
; is the package's (DIA_ONCARD); on a desk window anything else is the
; frame's: the x - once the handle was SHOWING before this press - arms, and
; the rest posts OSAPI_WM_DESK DRAG (SPEC.md 11.103.6). A press reveals the
; handle at once, and hands the widget the bar (ui.inc's menu_activate did).
; -----------------------------------------------------------------------------
dia_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [dia_qx], cx
    mov [dia_qy], dx
    mov al, [dia_hv]
    mov [dia_pre], al
    call dia_org
    mov cx, [dia_qx]
    mov dx, [dia_qy]
    call dia_hit
    jc .frame
    inc ax
    mov [dia_press], al
    dec ax
%ifdef DIA_ONCARD
    push si
    call DIA_ONCARD
    pop si
%endif
    jmp short .after
.frame:
    test byte [dia_wf], DIA_W_DESK
    jz .focus                   ; the titled frame is the kernel's
    cmp byte [dia_pre], DIA_HV_SHOWN
    jb .drag                    ; the x only once it was SHOWING
    or ah, ah
    jz .drag
    or byte [dia_wf], DIA_W_ARMED
    jmp short .after
.drag:
    mov bx, si
    mov al, 2
    call OSAPI_WM_DESK          ; the kernel's drag, once we return
.after:
    test byte [dia_wf], DIA_W_DESK
    jz .focus
    mov byte [dia_hv], DIA_HV_SHOWN ; a press reveals at once
.focus:
    call dia_focus
    call dia_tagsync
    call dia_arm
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_mup - W_ONMOUSEUP (SI = the window, CX, DX the release, lock held): an
; armed x acts only if the release is ON it - a slide off cancels (SPEC.md
; 13.5) - and a card's release is DIA_ONCARDUP's, card-relative
dia_mup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    test byte [dia_wf], DIA_W_ARMED
    jz .card
    and byte [dia_wf], ~DIA_W_ARMED & 0FFh
    mov [dia_qx], cx
    mov [dia_qy], dx
    call dia_org
    mov cx, [dia_qx]
    mov dx, [dia_qy]
    call dia_hit
    jnc .stay
    or ah, ah
    jz .stay
    call dia_quit               ; CF = 0: going - nothing drawn after this
    jnc .out
.stay:
    call dia_tagsync            ; the x unlit, or the plate if it was left
    jmp short .out
.card:
    mov al, [dia_press]
    or al, al
    jz .out
    mov byte [dia_press], 0
%ifdef DIA_ONCARDUP
    dec ax
    mov [dia_sc], al
    mov [dia_qx], cx
    mov [dia_qy], dx
    call dia_org
    mov al, [dia_sc]
    call dia_ctop
    mov cx, [dia_qx]
    sub cx, [dia_fx]
    mov dx, [dia_qy]
    sub dx, [dia_fy]
    sub dx, bx
    mov al, [dia_sc]
    push si
    call DIA_ONCARDUP
    pop si
%endif
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dia_quit - the widget's close (SI = the window, lock held): dia_close's
; negotiator, then OSAPI_WM_CLOSE. CF = 0 the close is under way - draw
; nothing more and return; CF = 1 it was declined (the disk, SPEC.md 107.5)
dia_quit:
    call dia_close
    jc .r
    push bx
    mov bx, si
    call OSAPI_WM_CLOSE
    pop bx
    clc
.r:
    ret

; -----------------------------------------------------------------------------
; dia_wake - W_ONWAKE (SI = the window, the UI task, NO lock). The worker arm's
; kick is v1's beat; otherwise it is the hover edge (SPEC.md 11.103.7), and
; LEVEL-triggered: WF_HOVER is read, the state moves, a deadline is armed -
; and nothing is drawn, there being no lock
; -----------------------------------------------------------------------------
dia_wake:
    test byte [dia_st], DIA_S_WORKER
    jz .hov
    call OSAPI_GFX_LOCK
    call dia_beat
    call OSAPI_GFX_UNLOCK
    ret
.hov:
    push ax
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov al, [dia_hv]
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .off
    cmp al, DIA_HV_HIDDEN
    jne .on2
    mov byte [dia_hv], DIA_HV_DWELL
    mov ax, DIA_HOVER_T
    jmp short .dl
.on2:
    cmp al, DIA_HV_LINGER
    jne .x
    mov byte [dia_hv], DIA_HV_SHOWN ; back before the linger ran out
    jmp short .x
.off:
    cmp al, DIA_HV_DWELL
    jne .off2
    mov byte [dia_hv], DIA_HV_HIDDEN    ; gone before the dwell ran out
    jmp short .x
.off2:
    cmp al, DIA_HV_SHOWN
    jne .x
    mov byte [dia_hv], DIA_HV_LINGER
    mov ax, DIA_LEAVE_T
.dl:
    call dia_hset
    call dia_arm
.x:
    pop es
    pop ax
    ret

; dia_tick - the timer (SI = the window, lock held), which serves the beat,
; the dwell or linger and the focus poll at once: each keeps an ABSOLUTE
; deadline and dia_arm takes the soonest, so a hover deadline never delays a
; beat and a beat that comes early does nothing early
dia_tick:
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [dia_tbeat]
    js .nb                      ; the beat is not due
    push ax
    call dia_beat
    pop ax
    xor bx, bx
    xchg bl, [dia_nx]           ; a sooner beat the application asked for,
    or bl, bl                   ; once (SPEC.md 107.5)
    jnz .t
    mov bl, DIA_TICKS
.t:
    add ax, bx
    mov [dia_tbeat], ax
.nb:
    call dia_hvtick
    call dia_focus
    call dia_tagsync
    call dia_arm
    pop bx
    pop ax
    ret

; dia_hvtick - a dwell or a linger that has run out (lock held)
dia_hvtick:
    push ax
    push cx
    push dx
    push si
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [dia_win]
    mov al, [dia_hv]
    cmp al, DIA_HV_DWELL
    je .dw
    cmp al, DIA_HV_LINGER
    jne .x
    call dia_hdue
    jc .x
    test byte [dia_wf], DIA_W_ARMED
    jnz .wait                   ; the x is held: its release decides
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
    mov byte [dia_hv], DIA_HV_SHOWN
    jmp short .x
.wait:
    mov ax, DIA_LEAVE_T
    call dia_hset
    jmp short .x
.dw:
    call dia_hdue
    jc .x
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
    call OSAPI_MOUSE            ; AL = the buttons
    test al, 3
    jz .show
    mov ax, DIA_HOVER_T         ; a button held: never revealed (SPEC.md
    call dia_hset               ; 107.11) - ask again a dwell later
    jmp short .x
.show:
    mov byte [dia_hv], DIA_HV_SHOWN
    jmp short .x
.hide:
    mov byte [dia_hv], DIA_HV_HIDDEN
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; dia_hdue - CF = 1 the dwell or linger is still ahead. AX spent
dia_hdue:
    call OSAPI_GET_TICKS
    sub ax, [dia_thov]
    js .no
    clc
    ret
.no:
    stc
    ret

; dia_hset - AX = ticks from now: the dwell's or linger's deadline. AX spent
dia_hset:
    push bx
    mov bx, ax
    call OSAPI_GET_TICKS
    add ax, bx
    mov [dia_thov], ax
    pop bx
    ret

; dia_focus - does the widget own the menu bar (OSAPI_MENU_OWNER, an inline
; cell)? On a change, DIA_ONFOCUS (AL = 1 gained, 0 lost; lock held). A desk
; window's only: the titled fallback is an ordinary window. Every register
; kept
dia_focus:
    test byte [dia_wf], DIA_W_DESK
    jz .r
    push ax
    push bx
    call OSAPI_MENU_OWNER
    cmp bx, [dia_win]
    je .own
    test byte [dia_wf], DIA_W_FOCUS
    jz .x
    and byte [dia_wf], ~DIA_W_FOCUS & 0FFh
    mov al, 0
    jmp short .say
.own:
    test byte [dia_wf], DIA_W_FOCUS
    jnz .x
    or byte [dia_wf], DIA_W_FOCUS
    mov al, 1
.say:
%ifdef DIA_ONFOCUS
    push si
    mov si, [dia_win]
    call DIA_ONFOCUS
    pop si
%endif
.x:
    pop bx
    pop ax
.r:
    ret

; dia_arm - the one timer, at the soonest deadline: the beat's, the dwell's
; or linger's, and - only while the widget owns the bar - the focus poll's
; DIA_FOCUS_T. Every register kept
dia_arm:
    push ax
    push bx
    push cx
    call OSAPI_GET_TICKS
    mov cx, ax
    mov ax, [dia_tbeat]
    sub ax, cx
    cmp ax, 1
    jge .b
    mov ax, 1
.b:
    mov bl, [dia_hv]
    cmp bl, DIA_HV_DWELL
    je .h
    cmp bl, DIA_HV_LINGER
    jne .f
.h:
    mov bx, [dia_thov]
    sub bx, cx
    cmp bx, 1
    jge .h1
    mov bx, 1
.h1:
    cmp bx, ax
    jge .f
    mov ax, bx
.f:
    test byte [dia_wf], DIA_W_FOCUS
    jz .set
    cmp ax, DIA_FOCUS_T
    jle .set
    mov ax, DIA_FOCUS_T
.set:
    mov bx, [dia_win]
    call OSAPI_WM_TIMER         ; CF = 1 on kern_small: there is no timer, and
    pop cx                      ; the worker's beat is the whole of it
    pop bx
    pop ax
    ret

; the name, and the handle's two bit records (GFXE_BITS: db rows, then words)
dia_tags    db DIA_TAG, 0
dia_xbits   db 6                ; the x, 6 x 6, two in from the box's corner
            dw 08400h, 04800h, 03000h, 03000h, 04800h, 08400h
dia_grip    db 7                ; the grip: three dotted lines, 13 wide
            dw 0AAA8h, 0, 0, 0AAA8h, 0, 0, 0AAA8h
%endif ; DIA_WIDGET

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
