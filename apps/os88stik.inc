; =============================================================================
; os88stik.inc - a STICKY APP's shared half (SPEC.md 110)
;
; A Sticky App - a widget - is an ordinary package in STIKAPPS\ on the boot volume
; that includes this file. It owns three things for it:
;
;   its STATE FILE  SYSTEM\APPDATA\<STIK_DATNAME> on its own volume (SPEC.md
;                   19.9): a 16-byte Sticky App header - where its window was, on
;                   what screen - then the application's payload (110.5)
;   its PRESENCE    one Sticky App ROW in the desktop configuration, the SYSTEM.CFG
;                   trailer (110.2), registered on the first timer tick and
;                   removed BY NAME when the window closes - which is what the
;                   kernel's stik_boot relaunches from at the next boot (110.4)
;   its TICK        a 36-tick timer that polls the window's place, saves once
;                   a drag has settled, and calls the application's own tick
;
; A save asks first whether the disk in the drive is still HOME - the folder
; the widget was launched from still holds a file of its own name - because a
; widget outlives a disk swap (SPEC.md 110.5.1). A refusal that may clear
; (no disk, an I/O error, not home) is retried on a doubling backoff; one that
; will not (write-protected, full) makes the state read-only for the session.
;
; THE CALLER DEFINES, before the %include:
;
;   %define STIK_DATNAME 'DESKCAL.DAT'   the state file's 8.3 name
;   %define STIK_CAP    'Calendar'      the row's caption (<= 12 chars) - what
;                                       a downgraded row's icon says (110.4)
;   STIK_PAYLEN equ n                   the payload's EXACT length
;   STIK_PAYVER equ v                   ...and its version
;   STIK_ICON   equ 32                  the 64-byte icon body in the image
;                                       (+32 with OS88_F_ICON)
;   STIK_BSS    equ k                   where in the package's bss the state
;                                       below starts: a MULTIPLE OF 16, and
;                                       the package's own image must END on a
;                                       paragraph (`align 16` before
;                                       OS88_IMAGE_END) - the read lands at
;                                       offset 0 of a segment (OSAPI_FILE_READ)
;   %define STIK_ONTICK proc            optional: a near proc, every tick -
;                                       the UI task with the gfx lock HELD on
;                                       both kernels, no clip region armed.
;                                       It ANSWERS: CF = 1 holds any save
;                                       owed to a later poll (a field open,
;                                       a question up, an edit this poll),
;                                       CF = 0 lets it go. A save is seconds
;                                       of disk on a 5150 (SPEC.md 110.6)
;   %define STIK_ONCLOSE proc            optional: before the row is removed
;   %define STIK_ISOPEN 'Desk Calendar is open'   optional: the toast a SECOND
;                                       launch is refused with (<= 24 chars,
;                                       SPEC.md 59.10); 'Already open' if not
;   %define STIK_DAMAGEDQ 'Replace damaged calendar?'   optional, and only with
;                                       os88ui.inc's OS88UI_ALERT included
;                                       first: asked before the first save
;                                       over a damaged file (SPEC.md 110.5.2)
;
; and gets STIK_PAY (the payload, STIK_PAYLEN bytes, zero when there was no
; valid file) and STIK_BSS_END (the next free bss offset).
;
; ITS CALLS, at the package's own points:
;
;   stik_load   the ENTRY PROC, before OSAPI_WM_CREATE: SI = the loader's
;               name (KERNEL_SEG:SI), DI = your window template. CF = 1: a
;               window of ANOTHER instance of this package is up, the user
;               has been told, and your entry proc must return CF = 1 having
;               done nothing else. CF = 0: the state file is read and the
;               template placed (anchored if it was saved on another screen).
;   stik_made   after OSAPI_WM_CREATE AND your own OSAPI_WM_KEEPH /
;               OSAPI_WM_PREFER, BX = the window: puts back a restored y the
;               create's fit pulled up off the dock, takes the place the window opened at
;               as the place on record, installs the timer - or, on
;               kern_small, the wake handler of the worker arm - and the close
;               negotiator, and arms the first tick.
;   stik_painted the LAST thing your W_PAINT does (lock held): on kern_small
;               it hires the worker on the first paint; elsewhere a test.
;   stik_touch  your payload changed: a save is owed, and the next tick
;               makes it. Every register and the flags kept.
;   [stik_nx]    a byte: store n (1..35) from STIK_ONTICK and the NEXT beat is
;               n ticks out instead of 36, once - a second note of a chirp
;               (SPEC.md 110.3). The timer arm's; kern_small's worker has
;               begun its next sleep before the beat runs, and ignores it.
;
; SPEC.md 110.5 and 110.5.2 are the contract.
; =============================================================================

%ifndef OS88STIK_INC
%define OS88STIK_INC

STIK_HDR_MAGIC  equ 0x4144      ; 'DA' (SPEC.md 110.5)
STIK_HDR_VER    equ 1
STIK_HF_POS     equ 1           ; +3 bit 0: the position is valid
STIK_HF_CONT    equ 2           ; +3 bit 1: +4/+6 are the FACE's origin, not
                                ; a titled frame's (v2, SPEC.md 110.11) - only
                                ; a STIK_WIDGET build writes it
STIK_H_MAGIC    equ 0
STIK_H_VER      equ 2
STIK_H_FLAGS    equ 3
STIK_H_X        equ 4
STIK_H_Y        equ 6
STIK_H_SW       equ 8
STIK_H_SH       equ 10
STIK_H_PAYVER   equ 12
STIK_H_LEN      equ 14
STIK_HDR_SIZE   equ 16
STIK_TICKS      equ 36          ; ~2 s between polls
STIK_SLACK      equ 80          ; a compressed file is read high and expanded
                                ; down (OSAPI_FILE_READ): the bytes past the
                                ; payload it needs are stik_rec's, scratch then

; stik_st bits
STIK_S_TRIED    equ 1           ; the first tick has asked for the row
STIK_S_ROW      equ 2           ; ...and was granted it
STIK_S_NOHOME   equ 4           ; no SYSTEM\APPDATA: the state stays in memory
STIK_S_DAMAGED  equ 8           ; the file was there and was not ours
STIK_S_DIRTY    equ 16          ; owed a save
STIK_S_RDONLY   equ 32          ; a save was refused for good (write-protected,
                                ; full): the state stays in memory (110.5.1)
STIK_S_WORKER   equ 64          ; no OSAPI_WM_TIMER (kern_small): the tick is
                                ; a worker's wake (SPEC.md 110.5.2)
STIK_S_HIRED    equ 128         ; ...and the worker is running
; stik_fl bits
STIK_F_ASKING   equ 1           ; the damaged-file question is on the glass
STIK_F_CLOSING  equ 2           ; stik_close's save: never ask from there
STIK_F_PLACED   equ 4           ; the template's place came from the file
STIK_F_KEPT     equ 8           ; a close was declined once (stik_close)
STIK_F_QUIT     equ 16          ; stik_quit's OSAPI_WM_CLOSE is posted: the
                                ; close is deferred (SPEC.md 75.2), so a tick
                                ; or a wake before it does nothing
STIK_BKMAX      equ 16          ; a refusal that may clear waits 1, 2, 4, 8,
                                ; then 16 polls before the next try

%if STIK_BSS % 16
  %error "os88stik: STIK_BSS must be a multiple of 16 - the read wants a paragraph"
%endif

stik_file   equ os88_image_end + STIK_BSS                   ; header + payload
STIK_PAY    equ stik_file + STIK_HDR_SIZE
stik_rec    equ stik_file + STIK_HDR_SIZE + STIK_PAYLEN       ; 128: the row
stik_name   equ stik_rec + OSAPI_DI_SIZE                     ; 13: our 8.3 name
stik_hdx    equ stik_name + 13                               ; the banked folder
stik_hbl    equ stik_hdx + 2                                 ; ...and its drive
stik_st     equ stik_hbl + 1
stik_win    equ stik_st + 1
stik_px     equ stik_win + 2                                 ; the last poll
stik_py     equ stik_px + 2
stik_bk     equ stik_py + 2                                  ; the backoff, polls
stik_bn     equ stik_bk + 1                                  ; ...still to wait
stik_fnd    equ stik_bn + 1                                  ; the walk's FIND
                                ; buffer - its OWN: stik_rec is the registered
                                ; row from the first tick to the close, and
                                ; the close removes by it (SPEC.md 110.5)
stik_fl     equ stik_fnd + OSAPI_FIND_SZ                     ; STIK_F_* (wave 2)
stik_wy     equ stik_fl + 1                                  ; the restored y
stik_nx     equ stik_wy + 2                                  ; the next beat,
                                ; ticks, once - 0: STIK_TICKS (wave 4)
%ifndef STIK_WIDGET
STIK_BSS_END equ STIK_BSS + STIK_HDR_SIZE + STIK_PAYLEN + OSAPI_DI_SIZE + 13 + 2 + 1 + 1 + 2 + 2 + 2 + 1 + 1 + OSAPI_FIND_SZ + 1 + 2 + 1
%else
; THE WIDGET'S OWN (SPEC.md 110.11), after v1's - so every v1 offset above
; is where it was, and tools/os88stik.py's widget_off() names these
stik_wf     equ stik_nx + 1      ; db STIK_W_*: the form, the focus, the x armed
stik_hv     equ stik_wf + 1      ; db the hover state, STIK_HV_*
stik_tagp   equ stik_hv + 1      ; db the tag's picture on the glass, STIK_P_*
stik_press  equ stik_tagp + 1    ; db the card a press went to, + 1 (0 none)
stik_li     equ stik_press + 1   ; db the layout: 0 TALL, 1 MID, 2 COMPACT
stik_ci     equ stik_li + 1      ; db the paint's card
stik_lay    equ stik_ci + 1      ; dw ...its STIK_LAYOUT entry
stik_fh     equ stik_lay + 2     ; dw the face's height as SHOWN: the whole
                                ; column on a desk window, less the tag row
                                ; and the last gap in the titled fallback
stik_fx     equ stik_fh + 2      ; dw the face's screen origin, as the last
stik_fy     equ stik_fx + 2      ; dw ...paint, press or draw found it
stik_cy     equ stik_fy + 2      ; dw the card being painted: its screen top
stik_ch     equ stik_cy + 2      ; dw ...and its height
stik_dmg    equ stik_ch + 2      ; 4 dw: the paint's damage rect, inclusive
stik_pp     equ stik_dmg + 8     ; dw the paint's walk over the layout's pairs
stik_gh     equ stik_pp + 2      ; dw a gap's rows
stik_tbeat  equ stik_gh + 2      ; dw the next BEAT, an absolute tick
stik_thov   equ stik_tbeat + 2   ; dw ...the dwell's or the linger's
stik_qx     equ stik_thov + 2    ; dw a press or release point, banked
stik_qy     equ stik_qx + 2
stik_pre    equ stik_qy + 2      ; db the hover state BEFORE a press
stik_sc     equ stik_pre + 1     ; db stik_slabs: the card
stik_ss     equ stik_sc + 1      ; dw ...the slab's first card row
stik_sr0    equ stik_ss + 2      ; dw ...the damaged rows, card-local
stik_sr1    equ stik_sr0 + 2
stik_ty     equ stik_sr1 + 2     ; dw the tag row's screen top, as drawn
STIK_BSS_END equ stik_ty + 2 - os88_image_end
%endif
%if OSAPI_DI_SIZE < STIK_SLACK
  %error "os88stik: stik_rec is the read's slack before it is the row"
%endif

; STIK_PLACE reg - AX, DX = the place a window's record means to the state
; file: its frame's x,y (v1), or its FACE's origin (STIK_WIDGET, SPEC.md
; 110.11). `reg` holds the record, ES = KERNEL_SEG; nothing else changes
%macro STIK_PLACE 1
%ifdef STIK_WIDGET
  %ifidni %1, si
    call stik_place
  %else
    push si
    mov si, %1
    call stik_place
    pop si
  %endif
%else
    mov ax, [es:%1+W_X]
    mov dx, [es:%1+W_Y]
%endif
%endmacro

%ifdef STIK_WIDGET
; --- THE WIDGET TREATMENT (SPEC.md 110.11) -----------------------------------
%ifndef GFXE_TEXT
  %error "os88stik: STIK_WIDGET composes with os88gfx.inc - define GFXE_TEXT"
%endif
%ifndef GFXE_BITS
  %error "os88stik: STIK_WIDGET composes with os88gfx.inc - define GFXE_BITS"
%endif
%ifndef STIK_TAG
  %error "os88stik: STIK_WIDGET wants STIK_TAG, the name tag (= the title)"
%endif
%ifndef STIK_LAYOUTS
  %error "os88stik: STIK_WIDGET wants STIK_LAYOUTS - three STIK_LAYOUT entries"
%endif
%ifndef STIK_PAINTCARD
  %error "os88stik: STIK_WIDGET wants STIK_PAINTCARD (stik_slabs + STIK_COMPOSE)"
%endif
%ifndef STIK_FACE_W
  %define STIK_FACE_W 176        ; 22 cells, the mockup's column
%endif
%ifndef STIK_WAKE
  %define STIK_WAKE stik_wake
%endif
%if STIK_FACE_W % 8
  %error "os88stik: STIK_FACE_W must be a multiple of 8 (BLIT1's grid on small)"
%endif
%if STIK_FACE_W > GFXE_BAND_W
  %error "os88stik: the band is narrower than the face (GFXE_BAND_W)"
%endif
STIK_HOVER_T equ 5               ; the dock's DOCK_HOVER_T and DOCK_LEAVE_T
STIK_LEAVE_T equ 14              ; (kernel/dock.inc, SPEC.md 30.6) by value -
                                ; tests/unit/t_stik.py holds them equal
STIK_FOCUS_T equ 4               ; the focus poll, only while owning the bar
STIK_HV_HIDDEN equ 0             ; stik_hv: the handle down...
STIK_HV_DWELL equ 1             ; ...the pointer on it, the dwell running
STIK_HV_SHOWN equ 2             ; ...the handle up
STIK_HV_LINGER equ 3             ; ...up, the pointer gone, the linger running
STIK_W_DESK equ 1               ; stik_wf: a desk window (MAKE took)
STIK_W_FOCUS equ 2               ; ...owning the menu bar and the keys
STIK_W_ARMED equ 4               ; ...a press on the handle's x is armed
STIK_W_PLATE equ 8               ; ...stik_tagdraw: plate to plate, the ground
                                ; beside it is on the glass already
STIK_P_REST equ 0               ; stik_tagp: the plate,
STIK_P_FOCUS equ 1               ; ...the plate inverse,
STIK_P_HAND equ 2               ; ...the handle,
STIK_P_ARMED equ 3               ; ...the handle with its x lit,
STIK_P_NONE equ 0FFh            ; ...or not known: the next sync draws
STIKL_N     equ 0               ; a STIK_LAYOUT entry: db the cards,
STIKL_TAG   equ 1               ; db the tag row's height,
STIKL_FY    equ 2               ; dw the default face y,
STIKL_CARDS equ 4               ; then (db height, db the gap under it) x 4
STIKL_MAX   equ 4
STIKL_SZ    equ STIKL_CARDS + 2 * STIKL_MAX
STIK_XX     equ 3               ; the handle's close box: face x 3..12
STIK_XW     equ 10
%strlen STIK_TAGLEN STIK_TAG
STIK_TAGW   equ STIK_TAGLEN * 8
STIK_PW     equ STIK_TAGW + 8    ; the plate: the name and 4 px each side
STIK_PX     equ (STIK_FACE_W - STIK_PW) / 2
STIK_NX     equ (STIK_FACE_W - STIK_TAGW) / 2     ; the handle's name
STIK_GX     equ STIK_FACE_W - 18                 ; ...and its grip
%if STIK_PW > STIK_FACE_W
  %error "os88stik: STIK_TAG is wider than the face"
%endif
%if STIK_NX < STIK_XX + STIK_XW + 1 || STIK_NX + STIK_TAGW > STIK_GX - 1
  %error "os88stik: STIK_TAG crowds the handle's x or its grip"
%endif

; STIK_LAYOUT band, facey, tagh, h0,g0 [, h1,g1 [, h2,g2 [, h3,g3]]] - one
; layout: each card with the gap UNDER it; refused at assembly time when the
; face, or the titled fallback's frame, is taller than `band`, and when a gap
; BETWEEN two cards is taller than GFXE_BAND_H - the titled fallback composes
; such a gap in the band whole (stik_tgap); the last gap is the tag's and is
; never composed
%macro STIK_LAYOUT 5-11
  %if (%0 - 3) % 2
    %error "STIK_LAYOUT: a height and a gap per card"
  %endif
  %assign %%n (%0 - 3) / 2
  %assign %%band %1
  %assign %%tag %3
  %if %%tag < 10 || %%tag > GFXE_BAND_H
    %error "STIK_LAYOUT: the tag row is 10 rows or more, and no taller than the band"
  %endif
    db %%n, %3
    dw %2
  %assign %%sum %3
  %assign %%last 0
  %assign %%i 0
  %rotate 3
  %rep %%n
    db %1, %2
    %assign %%i %%i + 1
    %if %%i < %%n && %2 > GFXE_BAND_H
      %error "STIK_LAYOUT: a gap between cards is taller than the band - the titled fallback composes it there whole (stik_tgap)"
    %endif
    %assign %%sum %%sum + %1 + %2
    %assign %%last %2
    %rotate 2
  %endrep
    times 2 * (STIKL_MAX - %%n) db 0
  %if %%sum > %%band
    %error "STIK_LAYOUT: the face is taller than its band"
  %endif
  %if %%sum - %%tag - %%last + TITLE_H + 1 > %%band
    %error "STIK_LAYOUT: the titled fallback is taller than its band"
  %endif
%endmacro
%endif ; STIK_WIDGET

; -----------------------------------------------------------------------------
; stik_load - the entry proc's half (SPEC.md 110.5, 110.5.2)
; in:  SI = the loader's name (an offset in KERNEL_SEG), DI = the template
; out: CF = 1 another instance's window is up and its toast has been said:
;      return CF = 1 from the entry proc. CF = 0 the state file is read and
;      the template's x,y are the saved ones - anchored to the nearer edges
;      when they were saved on a screen of another size. Every register but
;      the flags kept.
; -----------------------------------------------------------------------------
stik_load:
    call stik_single            ; FIRST, before any I/O: a refused second
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
%ifdef STIK_WIDGET
    call stik_wsize             ; QUERY, the layout, the template's size and
                                ; its default place (SPEC.md 110.11)
%endif
    push di
    push ds                     ; the NAME, out of KERNEL_SEG: copy it, the
    push ds                     ; loader reuses the buffer (SPEC.md 20.2)
    mov ax, KERNEL_SEG
    mov ds, ax
    pop es
    mov di, stik_name
    mov cx, 12
.nm:
    lodsb
    stosb
    or al, al
    loopnz .nm
    mov byte [es:di], 0
    pop ds
    call OSAPI_FILE_HERE        ; HOME, banked ONCE: every save comes back
    mov [stik_hdx], dx           ; here, and asks whether this is still it
    mov [stik_hbl], bl           ; (SPEC.md 110.5.1)
    call stik_enter             ; standing in SYSTEM\APPDATA, or CF = 1
    jc .none
    push ds
    pop es
    mov bx, stik_file
    test bl, 15
    jnz .home                   ; a misplaced STIK_BSS: no home, said nowhere
    mov si, stik_dname
    mov cx, STIK_HDR_SIZE + STIK_PAYLEN + STIK_SLACK
    xor dx, dx
    call OSAPI_FILE_READ        ; DX:AX = its size
    jnc .got
    cmp ax, FERR_NOENT
    je .home                    ; MISSING: the defaults, and the first save
    cmp ax, FERR_IO             ; makes it
    ja .bad                     ; the FILE's fault (too big, not ours)
    or byte [stik_st], STIK_S_RDONLY ; FERR_NODISK / FERR_IO: UNREAD - nothing
    jmp short .zero             ; is known about it, so it is neither damaged
                                ; nor ever written over this session; the
                                ; next launch reads it again (SPEC.md 110.5)
.got:
    or dx, dx
    jnz .bad
    cmp ax, STIK_HDR_SIZE + STIK_PAYLEN
    jne .bad                    ; the EXACT size, or it is not ours
    cmp word [stik_file+STIK_H_MAGIC], STIK_HDR_MAGIC
    jne .bad
    cmp byte [stik_file+STIK_H_VER], STIK_HDR_VER
    jne .bad
    cmp byte [stik_file+STIK_H_PAYVER], STIK_PAYVER
    jne .bad
    cmp word [stik_file+STIK_H_LEN], STIK_PAYLEN
    jne .bad
    test byte [stik_file+STIK_H_FLAGS], STIK_HF_POS
    jz .home
    pop di                      ; the template
    push di
    call OSAPI_VIDEO            ; AX = width, BX = height
%ifdef STIK_WIDGET
    call stik_wplace            ; the FACE's place, anchored, into the
                                ; template's frame - SI = its y
%else
    mov cx, [stik_file+STIK_H_SW]
    mov dx, [di+WT_W]
    mov si, [stik_file+STIK_H_X]
    call stik_anchor            ; SI = x on this screen
    mov [di+WT_X], si
    mov ax, bx
    mov cx, [stik_file+STIK_H_SH]
    mov dx, [di+WT_H]
    mov si, [stik_file+STIK_H_Y]
    call stik_anchor
    mov [di+WT_Y], si           ; wm_create clamps it onto the glass (39.7)
%endif
    mov [stik_wy], si            ; ...and stik_made gives back what that clamp
    or byte [stik_fl], STIK_F_PLACED ; took to keep it off the dock
    jmp short .home
.bad:
    or byte [stik_st], STIK_S_DAMAGED
.zero:
    push ds                     ; DAMAGED: run empty, and never overwrite it
    pop es                      ; silently (stik_save asks first, 110.5.2)
    mov di, stik_file
    mov cx, STIK_HDR_SIZE + STIK_PAYLEN
    xor al, al
    cld
    rep stosb
.home:
    call stik_leave
    jmp short .out
.none:
    or byte [stik_st], STIK_S_NOHOME
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

; stik_anchor - one axis of a saved place, onto this screen (SPEC.md 110.5.2)
; in:  SI = the saved coordinate, CX = the screen's extent when it was saved,
;      AX = the extent now, DX = the frame's template extent on that axis
; out: SI = the coordinate now: unchanged when the screen is the same size or
;      the frame's middle was in the near half, else the same distance from
;      the FAR edge; never negative. Every other register kept
stik_anchor:
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

; stik_single - is another instance of this package up? (SPEC.md 110.5.2) Its
; window's segment carries OUR program name at +16. CF = 1 yes, and the toast
; is said; every register kept
stik_single:
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
    mov si, stik_isopen
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
; stik_made - after OSAPI_WM_CREATE: BX = the window. Installs the tick and the
; close negotiator; every register kept, the flags not - clear CF yourself
; before the entry proc returns
; -----------------------------------------------------------------------------
stik_made:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov [stik_win], bx
    mov ax, KERNEL_SEG          ; the record (SPEC.md 20.1)
    mov es, ax
%ifdef STIK_WIDGET
    call stik_wmade             ; MAKE, or the titled fallback's promises; the
                                ; prefilters and the wake (SPEC.md 110.11)
%endif
    test byte [stik_fl], STIK_F_PLACED
    jz .rec
    mov ax, [stik_wy]            ; THE RESTORED y. wm_create's fit keeps every
    cmp ax, [es:bx+W_Y]         ; frame that fits above the dock, but a frame
    jle .rec                    ; DRAGGED half over it is left there on
    push ax                     ; purpose (wm_dock_snap) - and that is the
    call OSAPI_WM_DISPLAY       ; place the user chose. Give back what the fit
    mov cx, bx                  ; took, down to the lowest y whose frame and
    mov bx, [stik_win]           ; shadow are on this display - wm_fit's own
    pop ax                      ; floor for a KEEPH window, ph - h - 1 - and
    sub cx, [es:bx+W_H]         ; never past it (SPEC.md 110.5.2). The window
    dec cx                      ; is not shown yet; x is clamped the same
    cmp ax, cx                  ; either way and is not touched
    jle .put
    mov ax, cx
.put:
    cmp ax, [es:bx+W_Y]
    jle .rec
    mov [es:bx+W_Y], ax
.rec:
    STIK_PLACE bx                ; THE PLACE ON RECORD is where it opened: the
                                ; poll owes a save only for another one, so a
%ifdef STIK_WIDGET
    or byte [stik_file+STIK_H_FLAGS], STIK_HF_CONT ; ...a FACE's from now on: a
                                ; v1 file is converted by its next save, once
%endif
    mov [stik_px], ax            ; launch alone writes nothing (110.5.2). The
    mov [stik_py], dx            ; header's screen goes with it, or a later
    mov [stik_file+STIK_H_X], ax  ; save would pair this place with the old
    mov [stik_file+STIK_H_Y], dx  ; screen; STIK_HF_POS is left as the file had
    call OSAPI_VIDEO            ; it, so a close still writes a missing file
    mov [stik_file+STIK_H_SW], ax
    mov [stik_file+STIK_H_SH], bx
    mov bx, [stik_win]
    mov ax, stik_tick
    call OSAPI_WM_ONTIMER
    mov ax, stik_close
    call OSAPI_WM_ONCLOSE
    mov ax, 1                   ; the first tick: the window is up by then
    call OSAPI_WM_TIMER         ; CF = 1: kern_small, which has no timer -
    jnc .t                      ; the WORKER ARM (SPEC.md 110.5.2): the
    or byte [stik_st], STIK_S_WORKER ; first paint hires it, and its wake is
%ifndef STIK_WIDGET             ; the tick - a widget's W_ONWAKE is STIK_WAKE
    mov ax, stik_wake           ; already (stik_wmade), which reaches stik_wake,
    call OSAPI_WM_ONWAKE        ; and stik_wake reads STIK_S_WORKER (SPEC.md
%endif                          ; 110.11)
.t:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_painted - the end of the package's W_PAINT (lock held). On the worker
; arm, hire the worker once: OSAPI_TASK_SPAWN wants the lock held and the
; instance published, which the entry proc has neither of (apps/taskmgr's
; rule). A refusal (the task table full) is retried by the next paint.
; Every register and the flags kept
stik_painted:
    pushf
    test byte [stik_st], STIK_S_WORKER
    jz .r
    test byte [stik_st], STIK_S_HIRED
    jnz .r
    push ax
    push bx
    mov ax, stik_worker
    mov bx, [stik_win]
    call OSAPI_TASK_SPAWN
    jc .no
    or byte [stik_st], STIK_S_HIRED
    OS88_WORKER_RESTARTABLE stik_worker ; a pass of the loop is all a restart
.no:                            ; costs: it parks in OSAPI_TASK_ALIVE only
    pop bx
    pop ax
.r:
    popf
    ret

; stik_touch - the payload changed: owe a save, which the next tick makes.
; Every register and the flags kept
stik_touch:
    pushf
    or byte [stik_st], STIK_S_DIRTY
    popf
    ret

; stik_worker - kern_small's tick: sleep, ask to live, kick the UI task. It
; touches no file and no pixel (SPEC.md 20.6 rule 7); stik_wake does both
stik_worker:
    mov ax, STIK_TICKS
    call OSAPI_TASK_SLEEP
    mov bx, [stik_win]
    call OSAPI_TASK_ALIVE       ; never comes back once the window is closing
    call OSAPI_WM_WAKE          ; one queued per window: a busy UI task loses
    jmp short stik_worker       ; nothing by it

; stik_wake - the worker's kick (SI = the window, the UI task, NO lock): take
; the lock so the beat - and STIK_ONTICK - sees kern_big's environment exactly
%ifndef STIK_WIDGET             ; (a widget's is below: the hover edge's too)
stik_wake:
    call OSAPI_GFX_LOCK
    call stik_beat
    call OSAPI_GFX_UNLOCK
    ret
%endif

; -----------------------------------------------------------------------------
; stik_tick - the timer (SI = the window; the UI task, the gfx lock held)
; -----------------------------------------------------------------------------
%ifndef STIK_WIDGET             ; (a widget's multiplexes, below)
stik_tick:
    call stik_beat
    push ax
    push bx
    mov bx, [stik_win]
    xor ax, ax
    xchg al, [stik_nx]           ; a sooner beat the application asked for,
    or al, al                   ; once (SPEC.md 110.5)
    jnz .t
    mov al, STIK_TICKS
.t:
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret
%endif

; stik_beat - one tick's work, on either arm: the UI task, the lock held
stik_beat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    test byte [stik_st], STIK_S_TRIED | STIK_S_WORKER
    jnz .poll                   ; (kern_small's OSAPI_DESK_ITEM is the
    or byte [stik_st], STIK_S_TRIED  ; refusal: its path walk would buy nothing)
    call stik_register          ; EVERY launch, whatever the state file said:
.poll:                          ; the row is the program's (SPEC.md 110.5)
    mov ax, KERNEL_SEG          ; ES = KERNEL_SEG on entry to a callback -
    mov es, ax                  ; and the registration's ES = DS since
    mov si, [stik_win]
%ifdef STIK_WIDGET
    call stik_refit             ; a drop that left the tag off the glass
%endif                          ; (SPEC.md 11.103.6's bound)
    STIK_PLACE si
    cmp ax, [stik_px]
    jne .moved                  ; still moving, or just started
    cmp dx, [stik_py]
    jne .moved
    cmp ax, [stik_file+STIK_H_X]  ; two polls agree: against the place on
    jne .new                    ; record - where it was saved, or where it
    cmp dx, [stik_file+STIK_H_Y]  ; opened (stik_made) - and only another place
    je .app                     ; owes a save: a launch alone writes nothing
.new:
    call stik_pos               ; two polls agree: the drag is over
.moved:
    mov [stik_px], ax
    mov [stik_py], dx
.app:
%ifdef STIK_ONTICK
    call STIK_ONTICK            ; CF = 1: the package is mid-edit - any save
    jc .arm                     ; owed waits for a later poll (SPEC.md 110.5)
%endif
    test byte [stik_st], STIK_S_DIRTY
    jz .arm
    cmp byte [stik_bn], 0
    je .save
    dec byte [stik_bn]           ; backing off a refusal that may clear
    jmp short .arm
.save:
    call stik_save
.arm:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_pos - AX, DX = the frame's place now: into the header with the screen
; it is on, and owed a save. Keeps every register
stik_pos:
    push ax
    push bx
    push cx
    push dx
    mov [stik_file+STIK_H_X], ax
    mov [stik_file+STIK_H_Y], dx
    call OSAPI_VIDEO
    mov [stik_file+STIK_H_SW], ax
    mov [stik_file+STIK_H_SH], bx
%ifdef STIK_WIDGET
    or byte [stik_file+STIK_H_FLAGS], STIK_HF_POS | STIK_HF_CONT
%else
    or byte [stik_file+STIK_H_FLAGS], STIK_HF_POS
%endif
    or byte [stik_st], STIK_S_DIRTY
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_register - the Sticky App row (SPEC.md 110.2): our volume, KIND 1, the mark,
; the path we were launched from and our name. A refusal - no system disk in
; A:, a full table, kern_small - is CTRL.DRV's to say (or nobody's): the
; widget runs on, unremembered
stik_register:
    push ds
    pop es
    cld
    mov di, stik_rec
    mov al, [stik_hbl]
    stosb                       ; +0 the volume, OSAPI_FILE_HERE's BL
    mov ax, 0xFF | (1 << 8)
    stosw                       ; +1 no cell, +2 a package
    mov cx, OSAPI_DI_CAP - OSAPI_DI_STIK ; +3 on: the path is written ONE
    call OSAPI_FILE_PATH        ; EARLY, so its leading `\` lands on +3 -
    jc .no                      ; sc_m_new's own idiom - and the mark goes
    mov byte [stik_rec+OSAPI_DI_STIK], OSAPI_STIK_MARK    ; over it. CX = its
                                ; length; FERR_BIG = longer than a row keeps
    add di, cx                  ; DI = its NUL
    cmp cx, 1
    je .root                    ; `\` alone is the root: the name, bare
    mov al, '\'
    stosb
    inc cx
.root:
    mov si, stik_name
.cp:
    lodsb
    stosb
    inc cx
    or al, al
    jnz .cp
    cmp cx, OSAPI_DI_CAP - OSAPI_DI_STIK ; the path's 45 bytes, its NUL in
    ja .no                      ; them: a longer one is not remembered
    mov di, stik_rec + OSAPI_DI_CAP
    mov si, stik_cap
    mov cx, OSAPI_DI_BODY - OSAPI_DI_CAP
    rep movsb                   ; the caption (and the header the kernel
    mov si, STIK_ICON           ; stamps), then the picture
    mov cx, 32
    rep movsw
    mov si, stik_rec
    mov al, 1
    call OSAPI_DESK_ITEM
    jc .no
    or byte [stik_st], STIK_S_ROW
.no:
    ret

; -----------------------------------------------------------------------------
; stik_close - the close negotiator (SI = the window; UI task, lock held). The
; application's own close, the row away BY NAME, the last place saved; and the
; close goes ahead (CF = 0). Restart and power-off do not come here (SPEC.md
; 110.4): the row stays, which is what restore wants. A remove REFUSED - no
; system disk in A: to read CTRL.DRV from, whose toast asks for it - declines
; the FIRST close (CF = 1), so the user can put it back and close again; the
; next one goes ahead whatever the remove says, so the window can never be
; made unclosable (SPEC.md 75.1). STIK_ONCLOSE can therefore run twice
; -----------------------------------------------------------------------------
stik_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
%ifdef STIK_ONCLOSE
    call STIK_ONCLOSE
%endif
    test byte [stik_st], STIK_S_ROW
    jz .pos
    push ds
    pop es
    mov si, stik_rec
    mov ax, 0xFF00              ; AL = 0 remove, AH = 0xFF: by NAME
    call OSAPI_DESK_ITEM
    jnc .gone
    test byte [stik_fl], STIK_F_KEPT
    jnz .gone                   ; refused twice: close anyway, the row stays
    or byte [stik_fl], STIK_F_KEPT
    stc                         ; refused once: stay up, save nothing - the
    jmp short .ret              ; disk is not home either
.gone:
    mov ax, KERNEL_SEG          ; the window record again
    mov es, ax
.pos:
    mov si, [stik_win]
    STIK_PLACE si
    test byte [stik_file+STIK_H_FLAGS], STIK_HF_POS
    jz .p
    cmp ax, [stik_file+STIK_H_X]
    jne .p
    cmp dx, [stik_file+STIK_H_Y]
    je .s
.p:
    call stik_pos
.s:
    test byte [stik_st], STIK_S_DIRTY
    jz .out
    or byte [stik_fl], STIK_F_CLOSING ; never ask from here: the alert would
    call stik_save              ; die with this package (SPEC.md 110.5.2)
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

; stik_save - the state file, header and payload in one write (SPEC.md
; 110.5.1). Not with no home, not once a refusal was for good, and not over a
; DAMAGED file until the user has said Yes to STIK_DAMAGEDQ (110.5.2). A
; refusal that may clear keeps it owed and backs the next try off; one that
; will not makes the state read-only for the session. Keeps every register
stik_save:
    test byte [stik_st], STIK_S_NOHOME | STIK_S_RDONLY
    jnz .r
    test byte [stik_st], STIK_S_DAMAGED
    jz .go
%ifdef STIK_DAMAGEDQ
    test byte [stik_fl], STIK_F_ASKING | STIK_F_CLOSING
    jnz .r                      ; asked already, or closing: not now
    push ax
    push bx
    push si
    push di
    push es
    call OSAPI_WM_CLIP_CLEAR    ; STIK_ONTICK may have left a clip armed this
                                ; beat, and the alert's create recomposes
                                ; the bar through it (SPEC.md 110.9)
    mov ax, KERNEL_SEG          ; os88ui_ask reads our window's title out of
    mov es, ax                  ; the record, in KERNEL_SEG
    mov al, OS88UI_AYESNO
    mov bx, [stik_win]
    mov si, stik_damq
    mov di, stik_dans
    call os88ui_ask             ; CF = 1: another alert of ours is up - ask
    jc .busy                    ; at a later tick
    or byte [stik_fl], STIK_F_ASKING
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
    call stik_enter             ; CF = 1: not home now - the disk is out, or
    jc .soon                    ; another one is in
    mov word [stik_file+STIK_H_MAGIC], STIK_HDR_MAGIC
    mov byte [stik_file+STIK_H_VER], STIK_HDR_VER
    mov byte [stik_file+STIK_H_PAYVER], STIK_PAYVER
    mov byte [stik_file+STIK_H_PAYVER+1], 0
    mov word [stik_file+STIK_H_LEN], STIK_PAYLEN
    push ds
    pop es
    mov bx, stik_file
    mov si, stik_dname
    mov cx, STIK_HDR_SIZE + STIK_PAYLEN
    xor dx, dx
    call OSAPI_FILE_WRITE
    call stik_leave             ; AX and the flags kept
    jnc .ok
    cmp ax, FERR_IO
    jbe .soon                   ; FERR_NODISK, FERR_IO: may clear
    or byte [stik_st], STIK_S_RDONLY ; write-protected, full, a full folder,
.ok:                            ; a protected name: will not, this session
    and byte [stik_st], ~STIK_S_DIRTY & 0xFF
    mov byte [stik_bk], 0
    jmp short .x
.soon:
    mov al, [stik_bk]            ; 1, 2, 4, 8, then 16 polls (2 s to 32 s)
    shl al, 1
    jnz .k
    inc ax
.k:
    cmp al, STIK_BKMAX
    jbe .s
    mov al, STIK_BKMAX
.s:
    mov [stik_bk], al
    mov [stik_bn], al
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef STIK_DAMAGEDQ
; stik_dans - the damaged-file question's answer (AL = 0 Yes, 1 No, or
; OS88UI_ACANCEL; the lock held). Yes: the file is ours to replace, and the
; next tick writes it whole. Anything else: read-only for the session
stik_dans:
    and byte [stik_fl], ~STIK_F_ASKING & 0xFF
    or al, al
    jnz .no
    and byte [stik_st], ~STIK_S_DAMAGED & 0xFF
    or byte [stik_st], STIK_S_DIRTY
    ret
.no:
    or byte [stik_st], STIK_S_RDONLY
    and byte [stik_st], ~STIK_S_DIRTY & 0xFF
    ret
%endif

; stik_enter - walk to SYSTEM\APPDATA on HOME's volume, having asked whether
; the disk in the drive is still home (SPEC.md 110.5.1). ONE full
; OSAPI_FILE_GOTO first - the disk may have been swapped, and only a full
; mount re-reads its FAT (18.8.2 cannot tell two os8088 disks apart) - then
; the quiet stand that moves the instance, a word inside the volume just
; mounted. out CF = 1 not there, and the instance is home again
stik_enter:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bl, [stik_hbl]
    xor dx, dx
    call OSAPI_FILE_GOTO        ; the root, mounted afresh
    jc .fail                    ; ...and not marked: still home, nothing to
    mov dx, [stik_hdx]           ; undo
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov si, stik_name
    call stik_seek              ; IS THIS HOME? our own file in our folder
    jc .no
    cmp ax, OSAPI_FT_DIR
    je .no
    mov bl, [stik_hbl]
    xor dx, dx
    call OSAPI_FILE_GOTO_QM     ; the root again, a word
    jc .no
    mov si, stik_dsys
    call stik_dive
    jc .no
    mov si, stik_dapp
    call stik_dive
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
    call stik_leave
.fail:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; stik_leave - home, quietly; every register and the flags kept
stik_leave:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [stik_hdx]
    mov bl, [stik_hbl]
    call OSAPI_FILE_GOTO_QM     ; CX, SI and DI are a file driver's (os88api)
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; stik_dive - into the folder named at SI, in the current one; CF = 1 none
stik_dive:
    push ax
    push bx
    push dx
    call stik_seek              ; DX = its first cluster
    jc .x
    cmp ax, OSAPI_FT_DIR
    stc
    jne .x                      ; a FILE of that name: not a folder
    mov bl, [stik_hbl]
    call OSAPI_FILE_GOTO_QM
.x:
    pop dx
    pop bx
    pop ax
    ret

; stik_seek - the entry named SI in the current folder, into stik_fnd. out CF = 0
; AX = its type (+14), DX = its first cluster (+16); CF = 1 there is none
stik_seek:
    push cx
    push di
    push es
    push ds
    pop es
    xor cx, cx
.each:
    mov di, stik_fnd
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
    mov ax, [stik_fnd + 14]
    mov dx, [stik_fnd + 16]
    clc
.out:
    pop es
    pop di
    pop cx
    ret

%ifdef STIK_WIDGET
; =============================================================================
; THE WIDGET TREATMENT (SPEC.md 110.11) - the face, its tag and its gestures.
; No pixel of it is the kernel's: the kernel's part is SPEC.md 11.103's layer
; =============================================================================

; stik_wsize - stik_load's first step (DI = the template): QUERY, the layout by
; the screen's HEIGHT, the template's size, and - with STIK_RIGHT - its default
; place. Every register kept
stik_wsize:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, 0
    call OSAPI_WM_DESK          ; CF = 0: this kernel has the desk layer
    jc .q
    or byte [stik_wf], STIK_W_DESK
.q:
    call OSAPI_VIDEO            ; AX = width, BX = height
    push ax
    xor cx, cx                  ; TALL at 400 rows or more, MID from 300 -
    cmp bx, 400                 ; Hercules' 348 AND EGA's 350, which the
    jae .l                      ; adapter's kind would have called VGA -
    inc cx                      ; COMPACT below (SPEC.md 110.11)
    cmp bx, 300
    jae .l
    inc cx
.l:
    mov [stik_li], cl
    mov ax, STIKL_SZ
    mul cx
    add ax, STIK_LAYOUTS
    mov si, ax
    mov [stik_lay], si
    call stik_fsum              ; DX = the whole column, CX = tag + last gap
    test byte [stik_wf], STIK_W_DESK
    jnz .d
    sub dx, cx                  ; the titled fallback: no tag, no last gap
    mov [stik_fh], dx
    add dx, TITLE_H + 1
    jmp short .h
.d:
    mov [stik_fh], dx
.h:
    mov [di+WT_H], dx
    mov word [di+WT_W], STIK_FACE_W + 2
    pop ax
%ifdef STIK_RIGHT
    sub ax, STIK_RIGHT + 1       ; the default place: the face STIK_RIGHT in
    mov [di+WT_X], ax           ; from the right edge, at the layout's y -
    mov ax, [si+STIKL_FY]        ; a state file's place overrides it
    test byte [stik_wf], STIK_W_DESK
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

; stik_fsum - SI = a layout entry: DX = its whole face height, CX = its tag row
; plus its last gap. Keeps the rest
stik_fsum:
    push ax
    push bx
    xor dx, dx
    xor ax, ax
    mov cl, [si+STIKL_N]
    xor ch, ch
    lea bx, [si+STIKL_CARDS]
.s:
    mov al, [bx]
    add dx, ax
    mov al, [bx+1]
    add dx, ax
    add bx, 2
    loop .s
    mov cl, [si+STIKL_TAG]       ; AL = the last gap
    add dx, cx
    add cx, ax
    pop bx
    pop ax
    ret

; stik_wplace - stik_load's restored place (AX, BX = the screen, DI = the
; template): the FACE's origin out of the header - a v1 file's titled frame
; converted - anchored with the face's size, into the template's FRAME. Out
; SI = the frame's y; AX..DX spent
stik_wplace:
    mov si, [stik_file+STIK_H_X]
    mov dx, [stik_file+STIK_H_Y]
    test byte [stik_file+STIK_H_FLAGS], STIK_HF_CONT
    jnz .f
    inc si                      ; v1: a titled frame, so the face is a
    add dx, TITLE_H             ; border in and a title bar down
.f:
    push dx
    mov cx, [stik_file+STIK_H_SW]
    mov dx, STIK_FACE_W
    call stik_anchor
    dec si                      ; the face is one column in from the frame,
    mov [di+WT_X], si           ; in both forms
    pop si
    mov ax, bx
    mov cx, [stik_file+STIK_H_SH]
    mov dx, [stik_fh]
    call stik_anchor
    test byte [stik_wf], STIK_W_DESK
    jnz .d
    sub si, TITLE_H             ; wm_create clamps it onto the glass
.d:
    mov [di+WT_Y], si
    ret

; stik_wmade - stik_made's first step (BX = the window, ES = KERNEL_SEG): MAKE,
; or the titled fallback's two promises; the prefilters and the wake. Every
; register kept
stik_wmade:
    push ax
    test byte [stik_wf], STIK_W_DESK
    jz .t
    mov al, 1
    call OSAPI_WM_DESK          ; MAKE: we are in the entry proc, the window
    jnc .cb                     ; hidden and nobody's yet (SPEC.md 11.103.9)
    and byte [stik_wf], ~STIK_W_DESK & 0FFh   ; refused: painted as titled
.t:
    mov al, 1
    call OSAPI_WM_OWNBG         ; every pixel is ours (OSAPI_WM_DAMAGE wants it)
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP   ; ...and two colours, as v1
    call OSAPI_WM_SAVEU
.cb:
    mov ax, stik_click
    call OSAPI_WM_ONCLICK
    mov ax, stik_mup
    call OSAPI_WM_ONMOUSEUP
    mov ax, STIK_WAKE           ; the hover edge's wake - and kern_small's
    call OSAPI_WM_ONWAKE        ; worker kick, which stik_wake tells apart
    mov byte [stik_tagp], STIK_P_NONE
    call OSAPI_GET_TICKS
    mov [stik_tbeat], ax         ; the first beat is due at once
    pop ax
    ret

; stik_place - SI = our window (ES = KERNEL_SEG): AX, DX = the FACE's origin.
; Nothing else changes
stik_place:
    mov ax, [es:si+W_X]
    inc ax
    mov dx, [es:si+W_Y]
    test byte [stik_wf], STIK_W_DESK
    jnz .r
    add dx, TITLE_H
.r:
    ret

; stik_org - stik_place, kept in [stik_fx]/[stik_fy] for the painter
stik_org:
    call stik_place
    mov [stik_fx], ax
    mov [stik_fy], dx
    ret

; stik_refit - the beat's (SI = the window, ES = KERNEL_SEG, lock held): a
; desk window dropped with its bottom past its display - ui_drag's bound is
; the title bar's (SPEC.md 11.103.6) - is put back on it before the place is
; saved. There is no move cell, so it is hidden, moved and shown: two
; repaints, once, after a drop nobody should make. A face taller than the
; display it was dropped on (a shorter SECOND display: the layout is the
; primary's) is lifted to the menu bar once and then left - the move is made
; only when it is UP. Every register kept
stik_refit:
    test byte [stik_wf], STIK_W_DESK
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
    mov bx, MBAR_H              ; a face taller than this display (the
.y:                             ; layout is the PRIMARY's) goes up to the bar
    cmp bx, [es:si+W_Y]         ; ...and ONLY up, once: at the bar already, it
    jge .x                      ; is left where it is, never re-shown a beat
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
; stik_paint - W_PAINT (SI = the window, lock held, the kernel's clip armed):
; only what intersects OSAPI_WM_DAMAGE, every pixel once - the ground columns
; and the gaps by OSAPI_GFX_DESK, each card by STIK_PAINTCARD, the tag from
; the state - then stik_painted
; -----------------------------------------------------------------------------
stik_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_DAMAGE        ; CF = 1: the whole of it, AX..DX either way
    jmp short stik_pbody

; stik_redrawr - stik_redraw over the damage AX, BX - CX, DX alone (screen,
; inclusive): what a card over the face uncovered, its slabs and nothing
; else (SPEC.md 110.11). Every register kept
stik_redrawr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push bx
    mov bx, si
    call OSAPI_WM_CLIP_SET
    pop bx
    jnc stik_pbody
    jmp stik_pdone

; stik_redraw - the whole face again OUTSIDE a paint (SI = the window, lock
; held): OSAPI_WM_CLIP_SET, then stik_paint's body over all of it. Nothing
; when none of the window is on the glass. Every register kept
stik_redraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jnc .all
    jmp stik_pdone
.all:
    mov ax, 8000h               ; the damage is everything
    mov bx, ax
    mov cx, 7FFFh
    mov dx, cx
stik_pbody:
    mov word [gfxe_fseg], 0     ; the glyph table is asked for again: a
                                ; resume may have MOVED it (SPEC.md 5.12.8)
    mov [stik_dmg], ax
    mov [stik_dmg+2], bx
    mov [stik_dmg+4], cx
    mov [stik_dmg+6], dx
    call stik_org               ; AX, DX = the face
    test byte [stik_wf], STIK_W_DESK
    jz .cards
    mov bx, dx                  ; the two ground columns, the column's height
    add dx, [stik_fh]
    dec dx
    dec ax
    mov cx, ax
    call stik_desk              ; at x - 1...
    add ax, STIK_FACE_W + 1
    mov cx, ax
    call stik_desk              ; ...and x + STIK_FACE_W
.cards:
    mov di, [stik_fy]            ; DI = the walk's screen y
    mov si, [stik_lay]
    lea ax, [si+STIKL_CARDS]
    mov [stik_pp], ax
    mov byte [stik_ci], 0
.card:
    mov bx, [stik_pp]
    xor ax, ax
    mov al, [bx]
    mov [stik_ch], ax
    mov [stik_cy], di
    mov ax, [stik_fx]
    mov bx, di
    mov cx, ax
    add cx, STIK_FACE_W - 1
    mov dx, di
    add dx, [stik_ch]
    dec dx
    call stik_cut               ; CF = 1: the damage misses it
    jc .gap
    sub bx, di                  ; card-local rows
    sub dx, di
    mov al, [stik_ci]
    push di
    call STIK_PAINTCARD
    pop di
.gap:
    add di, [stik_ch]
    mov bx, [stik_pp]
    xor ax, ax
    mov al, [bx+1]
    or ax, ax
    jz .next
    mov [stik_gh], ax
    mov bx, [stik_lay]
    mov cl, [stik_ci]
    inc cl
    cmp cl, [bx+STIKL_N]
    jb .g                       ; the last gap is the tag's - the titled
    test byte [stik_wf], STIK_W_DESK ; fallback has neither
    jz .next
.g:
    mov ax, [stik_fx]
    mov bx, di
    mov cx, ax
    add cx, STIK_FACE_W - 1
    mov dx, di
    add dx, [stik_gh]
    dec dx
    test byte [stik_wf], STIK_W_DESK
    jz .tg
    call stik_desk              ; the desktop, in the gap
    jmp short .gn
.tg:
    call stik_tgap              ; white, with a rule through its middle
.gn:
    add di, [stik_gh]
.next:
    add word [stik_pp], 2
    inc byte [stik_ci]
    mov bx, [stik_lay]
    mov al, [stik_ci]
    cmp al, [bx+STIKL_N]
    jb .card
    test byte [stik_wf], STIK_W_DESK
    jz .done
    xor ax, ax                  ; the tag row: drawn whole if the damage
    mov al, [bx+STIKL_TAG]       ; touches it
    mov bx, di
    mov dx, di
    add dx, ax
    dec dx
    mov ax, [stik_fx]
    mov cx, ax
    add cx, STIK_FACE_W - 1
    call stik_cut
    jc .done
    mov byte [stik_tagp], STIK_P_NONE ; drawn whole, the ground beside it too
    call stik_tagdraw
.done:
stik_pdone:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jmp stik_painted            ; tail: kern_small hires its worker there

; stik_cut - AX..DX (inclusive) intersected with the paint's damage: CF = 1
; empty, else AX..DX the intersection
stik_cut:
    cmp ax, [stik_dmg]
    jge .a
    mov ax, [stik_dmg]
.a:
    cmp bx, [stik_dmg+2]
    jge .b
    mov bx, [stik_dmg+2]
.b:
    cmp cx, [stik_dmg+4]
    jle .c
    mov cx, [stik_dmg+4]
.c:
    cmp dx, [stik_dmg+6]
    jle .d
    mov dx, [stik_dmg+6]
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

; stik_desk - the desktop's own ground over AX..DX, cut to the damage. Every
; register kept
stik_desk:
    push ax
    push bx
    push cx
    push dx
    call stik_cut
    jc .x
    call OSAPI_GFX_DESK
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_tgap - the titled fallback's gap (AX..DX = it, DI = its top, [stik_gh]
; its rows): white with a 1 px rule through its middle, composed and put down
; in ONE BLIT1 - never a fill and a line over it. Every register kept
stik_tgap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call stik_cut
    jc .x
    push bx
    push dx
    push di                     ; the composer spends DI
    mov ax, [stik_gh]
    mov [gfxe_bh], ax
    mov ax, 0FFFFh
    call gfxe_bclear            ; the gap's rows, white...
    mov byte [gfxe_tsty], 0
    xor ax, ax
    mov bx, [stik_gh]
    shr bx, 1
    mov cx, STIK_FACE_W
    mov dx, 1
    call gfxe_bfill             ; ...and the rule, black
    pop di
    pop dx
    pop bx
    mov si, bx
    sub si, di                  ; the band row the damage starts at
    sub dx, bx
    inc dx                      ; its rows
    mov ax, [stik_fx]
    mov cx, STIK_FACE_W
    call stik_blit
.x:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_blit - the band onto the glass: AX = x, BX = y, CX = width, DX = rows,
; SI = the band row to start from. One OSAPI_GFX_BLIT1; every register kept
stik_blit:
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
; stik_slabs - a STIK_PAINTCARD that composes: AL = the card, BX..DX = its
; damaged rows (card-local); [stik_fx], [stik_cy], [stik_ch] as stik_paint left
; them. For each GFXE_BAND_H-row slab those rows touch: the band papered
; white, STIK_COMPOSE (AL = the card, BX = the slab's first card row, CX = its
; rows - compose at y = card row - BX), and the damaged rows of it blitted:
; ONE OSAPI_GFX_BLIT1 a slab. Every register kept
; -----------------------------------------------------------------------------
stik_slabs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [stik_sc], al
    mov [stik_sr0], bx
    mov [stik_sr1], dx
    mov ax, bx
    xor dx, dx
    mov cx, GFXE_BAND_H
    div cx
    mul cx
    mov [stik_ss], ax            ; the first slab touched
.slab:
    mov cx, [stik_ch]
    sub cx, [stik_ss]
    cmp cx, GFXE_BAND_H
    jbe .n
    mov cx, GFXE_BAND_H
.n:
    mov [gfxe_bh], cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; white paper: a card is never themed
%ifdef STIK_COMPOSE
    mov al, [stik_sc]
    mov bx, [stik_ss]
    mov cx, [gfxe_bh]
    call STIK_COMPOSE
%endif
    mov bx, [stik_sr0]           ; the rows of this slab that are damaged
    cmp bx, [stik_ss]
    jge .b0
    mov bx, [stik_ss]
.b0:
    mov dx, [stik_ss]
    add dx, [gfxe_bh]
    dec dx
    cmp dx, [stik_sr1]
    jle .b1
    mov dx, [stik_sr1]
.b1:
    mov si, bx
    sub si, [stik_ss]
    sub dx, bx
    inc dx
    add bx, [stik_cy]
    mov ax, [stik_fx]
    mov cx, STIK_FACE_W
    call stik_blit
    mov ax, [stik_ss]
    add ax, GFXE_BAND_H
    mov [stik_ss], ax
    cmp ax, [stik_sr1]
    jle .slab
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_want - AL = the tag picture the state asks for. Nothing else changes
stik_want:
    mov al, [stik_hv]
    cmp al, STIK_HV_SHOWN
    jb .plate
    mov al, STIK_P_HAND
    test byte [stik_wf], STIK_W_ARMED
    jz .r
    inc ax                      ; STIK_P_ARMED
    ret
.plate:
    mov al, STIK_P_REST
    test byte [stik_wf], STIK_W_FOCUS
    jz .r
    inc ax                      ; STIK_P_FOCUS
.r:
    ret

; -----------------------------------------------------------------------------
; stik_tagdraw - the tag row as the state asks ([stik_fx]/[stik_fy] the face,
; lock held, clipped): REST and FOCUS are ground | plate | ground - two
; OSAPI_GFX_DESK and one BLIT1, or the BLIT1 ALONE when [stik_tagp] says the
; glass holds the other plate (a focus edge: stik_paint sets STIK_P_NONE first)
; - and the handle ONE face-wide BLIT1. Every register kept
; -----------------------------------------------------------------------------
stik_tagdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call stik_want
    and byte [stik_wf], ~STIK_W_PLATE & 0FFh
    cmp al, STIK_P_HAND         ; REST <-> FOCUS differs in the plate alone:
    jae .pn                     ; the ground beside it is left as it is
    cmp byte [stik_tagp], STIK_P_HAND
    jae .pn
    or byte [stik_wf], STIK_W_PLATE
.pn:
    mov [stik_tagp], al
    mov bx, [stik_lay]
    xor cx, cx
    mov cl, [bx+STIKL_TAG]
    mov [gfxe_bh], cx
    mov dx, [stik_fy]
    add dx, [stik_fh]
    sub dx, cx
    mov [stik_ty], dx            ; the tag row's screen top
    cmp al, STIK_P_HAND
    jae .hand
    push ax                     ; --- the PLATE: white, or inverse focused
    xor bx, bx
    cmp al, STIK_P_FOCUS
    je .p
    dec bx
.p:
    mov ax, bx
    call gfxe_bclear
    pop ax
    mov bl, 0
    cmp al, STIK_P_FOCUS
    jne .s
    mov bl, GFXE_INV
.s:
    mov [gfxe_tsty], bl
    mov si, stik_tags
    mov cx, 4
    mov dx, [gfxe_bh]
    sub dx, 8
    shr dx, 1
    call gfxe_text
%if STIK_PX > 0
    test byte [stik_wf], STIK_W_PLATE
    jnz .pl
    mov ax, [stik_fx]            ; the ground left of it...
    mov cx, ax
    add cx, STIK_PX - 1
    call stik_tagrows
    call OSAPI_GFX_DESK
.pl:
%endif
    mov ax, [stik_fx]            ; ...the plate...
    add ax, STIK_PX
    mov cx, STIK_PW
    jmp short .put
.hand:
    push ax                     ; --- the HANDLE: a black bar
    xor ax, ax
    call gfxe_bclear
    pop ax
    push ax
    cmp al, STIK_P_ARMED
    je .lit
    mov byte [gfxe_tsty], GFXE_INV  ; the close box white...
    mov ax, STIK_XX
    call stik_boxy
    mov dx, cx
    call gfxe_bfill
    mov byte [gfxe_tsty], 0     ; ...with a black x
    jmp short .xm
.lit:
    mov byte [gfxe_tsty], GFXE_INV  ; armed: the box black, its x white
.xm:
    call stik_boxy
    sub cx, 6
    shr cx, 1                   ; the x's inset: centred in the box
    mov dx, bx
    add dx, cx
    add cx, STIK_XX
    mov si, stik_xbits
    call gfxe_bits
    pop ax
    mov byte [gfxe_tsty], GFXE_INV  ; the name in white, centred
    mov si, stik_tags
    mov cx, STIK_NX
    mov dx, [gfxe_bh]
    sub dx, 8
    shr dx, 1
    call gfxe_text
    mov si, stik_grip           ; ...and the grip
    mov cx, STIK_GX
    mov dx, [gfxe_bh]
    sub dx, 7
    shr dx, 1
    call gfxe_bits
    mov ax, [stik_fx]
    mov cx, STIK_FACE_W
.put:
    mov bx, [stik_ty]
    mov dx, [gfxe_bh]
    xor si, si
    call stik_blit
    cmp byte [stik_tagp], STIK_P_HAND
    jae .out
    test byte [stik_wf], STIK_W_PLATE
    jnz .out
%if STIK_PX + STIK_PW < STIK_FACE_W
    mov ax, [stik_fx]            ; ...and the ground right of it
    add ax, STIK_PX + STIK_PW
    mov cx, [stik_fx]
    add cx, STIK_FACE_W - 1
    call stik_tagrows
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

; stik_tagrows - BX, DX = the tag row's screen rows
stik_tagrows:
    mov bx, [stik_ty]
    mov dx, bx
    add dx, [gfxe_bh]
    dec dx
    ret

; stik_boxy - the close box under a tag of [gfxe_bh] rows: CX = its size,
; STIK_XW - or 8 under a tag shorter than 12 rows, so a black row stays above
; it on MID / COMPACT's 11 (v4 review) - and BX = its first band row,
; (tag rows - size) / 2. The rest kept
stik_boxy:
    mov bx, [gfxe_bh]
    mov cx, STIK_XW
    cmp bx, 12
    jae .s
    mov cx, 8
.s:
    sub bx, cx
    shr bx, 1
    ret

; stik_tagsync - the tag to what the state asks, drawn only on a change of the
; picture, under OSAPI_WM_CLIP_SET (lock held). Every register kept
stik_tagsync:
    test byte [stik_wf], STIK_W_DESK
    jz .r
    push ax
    push bx
    push dx
    push si
    push es
    call stik_want
    cmp al, [stik_tagp]
    je .x
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    test byte [es:si+W_FLAGS], 2
    jz .x                       ; not on the glass: the next paint has it
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .x                       ; none of it showing
    mov word [gfxe_fseg], 0     ; ...as stik_paint: the table may have moved
    call stik_org
    call stik_tagdraw
.x:
    pop es
    pop si
    pop dx
    pop bx
    pop ax
.r:
    ret

; stik_hit - CX, DX = a screen point, [stik_fx]/[stik_fy] the face. CF = 0 a
; card: AL = it, CX, DX card-relative. CF = 1 the frame - a ground column, a
; gap or the tag row - and AH = 1 when the point is on the handle's x
stik_hit:
    push bx
    push si
    push bp
    sub cx, [stik_fx]
    sub dx, [stik_fy]
    mov si, [stik_lay]
    lea bx, [si+STIKL_CARDS]
    xor ax, ax
    cmp cx, STIK_FACE_W
    jae .no                     ; a ground column (unsigned: -1 too)
.c:
    cmp al, [si+STIKL_N]
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
    cmp cx, STIK_XX
    jl .no
    cmp cx, STIK_XX + STIK_XW
    jge .no
    xor ax, ax
    mov al, [si+STIKL_TAG]
    sub ax, STIK_XW
    shr ax, 1
    cmp dx, ax
    jl .no
    add ax, STIK_XW
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

; stik_ctop - AL = a card: BX = its top, face-relative. Keeps the rest
stik_ctop:
    push ax
    push cx
    push si
    mov si, [stik_lay]
    lea si, [si+STIKL_CARDS]
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
; stik_click - W_ONCLICK (SI = the window, CX, DX the press, lock held). A card
; is the package's (STIK_ONCARD); on a desk window anything else is the
; frame's: the x - once the handle was SHOWING before this press - arms, and
; the rest posts OSAPI_WM_DESK DRAG (SPEC.md 11.103.6). A press reveals the
; handle at once, and hands the widget the bar (ui.inc's menu_activate did).
; -----------------------------------------------------------------------------
stik_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov [stik_qx], cx
    mov [stik_qy], dx
    mov al, [stik_hv]
    mov [stik_pre], al
    call stik_org
    mov cx, [stik_qx]
    mov dx, [stik_qy]
    call stik_hit
    jc .frame
    inc ax
    mov [stik_press], al
    dec ax
%ifdef STIK_ONCARD
    push si
    call STIK_ONCARD
    pop si
%endif
    jmp short .after
.frame:
    test byte [stik_wf], STIK_W_DESK
    jz .focus                   ; the titled frame is the kernel's
    cmp byte [stik_pre], STIK_HV_SHOWN
    jb .drag                    ; the x only once it was SHOWING
    or ah, ah
    jz .drag
    or byte [stik_wf], STIK_W_ARMED
    jmp short .after
.drag:
    mov bx, si
    mov al, 2
    call OSAPI_WM_DESK          ; the kernel's drag, once we return
.after:
    test byte [stik_wf], STIK_W_DESK
    jz .focus
    mov byte [stik_hv], STIK_HV_SHOWN ; a press reveals at once
.focus:
    call stik_focus
    call stik_tagsync
    call stik_arm
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; stik_mup - W_ONMOUSEUP (SI = the window, CX, DX the release, lock held): an
; armed x acts only if the release is ON it - a slide off cancels (SPEC.md
; 13.5) - and a card's release is STIK_ONCARDUP's, card-relative
stik_mup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    test byte [stik_wf], STIK_W_ARMED
    jz .card
    and byte [stik_wf], ~STIK_W_ARMED & 0FFh
    mov [stik_qx], cx
    mov [stik_qy], dx
    call stik_org
    mov cx, [stik_qx]
    mov dx, [stik_qy]
    call stik_hit
    jnc .stay
    or ah, ah
    jz .stay
    call stik_quit              ; CF = 0: going - nothing drawn after this
    jnc .out
.stay:
    call stik_tagsync           ; the x unlit, or the plate if it was left
    jmp short .out
.card:
    mov al, [stik_press]
    or al, al
    jz .out
    mov byte [stik_press], 0
%ifdef STIK_ONCARDUP
    dec ax
    mov [stik_sc], al
    mov [stik_qx], cx
    mov [stik_qy], dx
    call stik_org
    mov al, [stik_sc]
    call stik_ctop
    mov cx, [stik_qx]
    sub cx, [stik_fx]
    mov dx, [stik_qy]
    sub dx, [stik_fy]
    sub dx, bx
    mov al, [stik_sc]
    push si
    call STIK_ONCARDUP
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

; stik_quit - the widget's close (SI = the window, lock held): stik_close's
; negotiator, then OSAPI_WM_CLOSE. CF = 0 the close is under way - draw
; nothing more and return; CF = 1 it was declined (the disk, SPEC.md 110.5)
stik_quit:
    call stik_close
    jc .r
    or byte [stik_fl], STIK_F_QUIT   ; STIK_ONCLOSE has run: no beat after it,
    push bx                         ; and no tag drawn (stik_tick, stik_wake)
    mov bx, si
    call OSAPI_WM_CLOSE
    pop bx
    clc
.r:
    ret

; -----------------------------------------------------------------------------
; stik_wake - W_ONWAKE (SI = the window, the UI task, NO lock). The worker arm's
; kick is v1's beat; otherwise it is the hover edge (SPEC.md 11.103.7), and
; LEVEL-triggered: WF_HOVER is read, the state moves, a deadline is armed -
; and nothing is drawn, there being no lock
; -----------------------------------------------------------------------------
stik_wake:
    test byte [stik_fl], STIK_F_QUIT
    jnz .q                      ; closing: nothing more
    test byte [stik_st], STIK_S_WORKER
    jz .hov
    call OSAPI_GFX_LOCK
    call stik_beat
    call OSAPI_GFX_UNLOCK
    ret
.hov:
    push ax
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov al, [stik_hv]
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .off
    cmp al, STIK_HV_HIDDEN
    jne .on2
    mov byte [stik_hv], STIK_HV_DWELL
    mov ax, STIK_HOVER_T
    jmp short .dl
.on2:
    cmp al, STIK_HV_LINGER
    jne .x
    mov byte [stik_hv], STIK_HV_SHOWN ; back before the linger ran out
    jmp short .x
.off:
    cmp al, STIK_HV_DWELL
    jne .off2
    mov byte [stik_hv], STIK_HV_HIDDEN   ; gone before the dwell ran out
    jmp short .x
.off2:
    cmp al, STIK_HV_SHOWN
    jne .x
    mov byte [stik_hv], STIK_HV_LINGER
    mov ax, STIK_LEAVE_T
.dl:
    call stik_hset
    call stik_arm
.x:
    pop es
    pop ax
.q:
    ret

; stik_tick - the timer (SI = the window, lock held), which serves the beat,
; the dwell or linger and the focus poll at once: each keeps an ABSOLUTE
; deadline and stik_arm takes the soonest, so a hover deadline never delays a
; beat and a beat that comes early does nothing early
stik_tick:
    test byte [stik_fl], STIK_F_QUIT
    jz .go
    ret                         ; closing: no beat after STIK_ONCLOSE, no tag
.go:
    push ax
    push bx
    call OSAPI_GET_TICKS
    mov bx, ax
    sub bx, [stik_tbeat]
    js .nb                      ; the beat is not due
    push ax
    call stik_beat
    pop ax
    xor bx, bx
    xchg bl, [stik_nx]           ; a sooner beat the application asked for,
    or bl, bl                   ; once (SPEC.md 110.5)
    jnz .t
    mov bl, STIK_TICKS
.t:
    add ax, bx
    mov [stik_tbeat], ax
.nb:
    call stik_hvtick
    call stik_focus
    call stik_tagsync
    call stik_arm
    pop bx
    pop ax
    ret

; stik_hvtick - a dwell or a linger that has run out (lock held)
stik_hvtick:
    push ax
    push cx
    push dx
    push si
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    mov al, [stik_hv]
    cmp al, STIK_HV_DWELL
    je .dw
    cmp al, STIK_HV_LINGER
    jne .x
    call stik_hdue
    jc .x
    test byte [stik_wf], STIK_W_ARMED
    jnz .wait                   ; the x is held: its release decides
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
    mov byte [stik_hv], STIK_HV_SHOWN
    jmp short .x
.wait:
    mov ax, STIK_LEAVE_T
    call stik_hset
    jmp short .x
.dw:
    call stik_hdue
    jc .x
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
    call OSAPI_MOUSE            ; AL = the buttons
    test al, 3
    jz .show
    mov ax, STIK_HOVER_T        ; a button held: never revealed (SPEC.md
    call stik_hset              ; 110.11) - ask again a dwell later
    jmp short .x
.show:
    mov byte [stik_hv], STIK_HV_SHOWN
    jmp short .x
.hide:
    mov byte [stik_hv], STIK_HV_HIDDEN
.x:
    pop es
    pop si
    pop dx
    pop cx
    pop ax
    ret

; stik_hdue - CF = 1 the dwell or linger is still ahead. AX spent
stik_hdue:
    call OSAPI_GET_TICKS
    sub ax, [stik_thov]
    js .no
    clc
    ret
.no:
    stc
    ret

; stik_hset - AX = ticks from now: the dwell's or linger's deadline. AX spent
stik_hset:
    push bx
    mov bx, ax
    call OSAPI_GET_TICKS
    add ax, bx
    mov [stik_thov], ax
    pop bx
    ret

; stik_focus - does the widget own the menu bar (OSAPI_MENU_OWNER, an inline
; cell)? On a change, STIK_ONFOCUS (AL = 1 gained, 0 lost; lock held). A desk
; window's only: the titled fallback is an ordinary window. Every register
; kept
stik_focus:
    test byte [stik_wf], STIK_W_DESK
    jz .r
    push ax
    push bx
    call OSAPI_MENU_OWNER
    cmp bx, [stik_win]
    je .own
    test byte [stik_wf], STIK_W_FOCUS
    jz .x
    and byte [stik_wf], ~STIK_W_FOCUS & 0FFh
    mov al, 0
    jmp short .say
.own:
    test byte [stik_wf], STIK_W_FOCUS
    jnz .x
    or byte [stik_wf], STIK_W_FOCUS
    mov al, 1
.say:
%ifdef STIK_ONFOCUS
    push si
    mov si, [stik_win]
    call STIK_ONFOCUS
    pop si
%endif
.x:
    pop bx
    pop ax
.r:
    ret

; stik_arm - the one timer, at the soonest deadline: the beat's, the dwell's
; or linger's, and - only while the widget owns the bar - the focus poll's
; STIK_FOCUS_T. Every register kept
stik_arm:
    push ax
    push bx
    push cx
    call OSAPI_GET_TICKS
    mov cx, ax
    mov ax, [stik_tbeat]
    sub ax, cx
    cmp ax, 1
    jge .b
    mov ax, 1
.b:
    mov bl, [stik_hv]
    cmp bl, STIK_HV_DWELL
    je .h
    cmp bl, STIK_HV_LINGER
    jne .f
.h:
    mov bx, [stik_thov]
    sub bx, cx
    cmp bx, 1
    jge .h1
    mov bx, 1
.h1:
    cmp bx, ax
    jge .f
    mov ax, bx
.f:
    test byte [stik_wf], STIK_W_FOCUS
    jz .set
    cmp ax, STIK_FOCUS_T
    jle .set
    mov ax, STIK_FOCUS_T
.set:
    mov bx, [stik_win]
    call OSAPI_WM_TIMER         ; CF = 1 on kern_small: there is no timer, and
    pop cx                      ; the worker's beat is the whole of it
    pop bx
    pop ax
    ret

; the name, and the handle's two bit records (GFXE_BITS: db rows, then words)
stik_tags   db STIK_TAG, 0
stik_xbits  db 6                ; the x, 6 x 6, two in from the box's corner
            dw 08400h, 04800h, 03000h, 03000h, 04800h, 08400h
stik_grip   db 7                ; the grip: three dotted lines, 13 wide
            dw 0AAA8h, 0, 0, 0AAA8h, 0, 0, 0AAA8h
%endif ; STIK_WIDGET

stik_dname  db STIK_DATNAME, 0
stik_dsys   db 'SYSTEM', 0
stik_dapp   db 'APPDATA', 0
%ifdef STIK_ISOPEN
stik_isopen db STIK_ISOPEN, 0
%else
stik_isopen db 'Already open', 0
%endif
%ifdef STIK_DAMAGEDQ
stik_damq   db STIK_DAMAGEDQ, 0
%endif
%strlen STIK_CAPLEN STIK_CAP
%if STIK_CAPLEN > 12             ; the row's caption is 13 bytes, its NUL in
  %error "os88stik: STIK_CAP is at most 12 characters"  ; them: +62 and +63 are
%endif                          ; the kernel's picture header (SPEC.md 26.8.1)
stik_cap    db STIK_CAP, 0
            times OSAPI_DI_BODY - OSAPI_DI_CAP - ($ - stik_cap) db 0

%endif ; OS88STIK_INC
