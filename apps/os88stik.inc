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
;   STIK_PAYLEN equ n                   the payload's length in memory (a
;                                       file holds it less its zero tail,
;                                       SPEC.md 110.5.3)
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
;   %define STIK_COLOR                  optional, STIK_WIDGET's: the cards are
;                                       painted in colours other than 0 and
;                                       15, so the titled fallback's
;                                       save-under keeps all four planes
;   %define STIK_VARFACE                optional, STIK_WIDGET's: the face's
;                                       WIDTH and LAYOUT are the package's at
;                                       run time (SPEC.md 110.14.1) - STIK_FACE_W
;                                       is then the WIDEST face, [stik_fw]
;                                       the face now, and the package's
;   %define STIK_SIZEUP proc            ...sets [stik_fw] (a multiple of 8) and
;                                       may point [stik_lay] at a layout entry
;                                       of its own; called with [stik_li]
;                                       chosen and the state file READ, and
;                                       again by stik_resize. Keeps every
;                                       register. [stik_np] = 1: the tag row
;                                       at rest is the desktop, no plate.
;                                       It implies STIK_REDRAW
;   %define STIK_DESKONLY                optional, a widget's: no titled
;                                       fallback and no worker arm - the
;                                       build for kern_big, where a refused
;                                       QUERY refuses the LAUNCH instead
;                                       (SPEC.md 110.7, 110.11)
;   %define STIK_PACK   proc             optional, with STIK_UNPACK: a PACKED
;   %define STIK_UNPACK proc            state file, only the payload bytes in
;                                       use (SPEC.md 110.5.3). STIK_PACK, just
;                                       before a save: squeeze STIK_PAY in
;                                       place, CX out = the bytes to write
;                                       (1..STIK_PAYLEN). STIK_UNPACK, after a
;                                       load (CX = the file's payload length,
;                                       <= STIK_PAYLEN, the rest of STIK_PAY
;                                       zero) and after every save (CX = what
;                                       STIK_PACK answered): spread it back,
;                                       CF = 1 it is not yours - DAMAGED. Both
;                                       keep BX, DX and ES and may spend AX,
;                                       CX, SI, DI. Without them the file is
;                                       the payload less its ZERO TAIL, which
;                                       a load gives back as zeros
;   %define STIK_DMGREPLACE              optional: a DAMAGED file is
;                                       DELETED by the first save, which then
;                                       writes afresh - nothing asked. Without
;                                       it a damaged file is never written
;                                       (SPEC.md 110.5.2)
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
;
; THE SHARED LIBRARY (SPEC.md 110.15). A kern_big widget - STIK_DESKONLY -
; that adds ONE line, `%define STIK_LIB` BEFORE its OS88_HEADER, carries none
; of the code below: OS88_STIKCARD_END puts a fixed block at image offset
; OS88_STIKLIB_AT (a descriptor assembled from the defines above, a few
; constants, the far pointers and the state), this file emits a five-byte
; thunk per routine (os88stikc.inc), and stik_load binds them to the ONE copy
; of this code that rides in DESK.DRV (OSAPI_WM_DESK's LIB). The labels, the
; defines and the calls are the same either way; what moves is the state,
; from the package's bss into its block - and STIK_BSS_END says how much bss
; the state file's buffer still takes. apps/stiklib.asm assembles this file
; as that copy (STIK_LIBRARY): DS is the calling package's on every
; instruction, everything it knows of it comes from the block, and every
; callback goes through the package's trampoline (stik_cb). A package that
; walks SYSTEM\APPDATA for files of its own with stik_enter, stik_leave,
; stik_goqm and stik_seek also defines STIK_WALK, for their thunks -
; stik_enter taking DX = a folder it banked on an earlier visit (0: walk to
; SYSTEM\APPDATA) and answering AX = the witness it banks it under.
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
STIK_F_ASKING   equ 1           ; retired: was the damaged-file question
STIK_F_CLOSING  equ 2           ; retired: stik_close's save, when a save asked
STIK_F_PLACED   equ 4           ; the template's place came from the file
STIK_F_KEPT     equ 8           ; a close was declined once (stik_close)
STIK_F_QUIT     equ 16          ; stik_quit's OSAPI_WM_CLOSE is posted: the
                                ; close is deferred (SPEC.md 75.2), so a tick
                                ; or a wake before it does nothing
STIK_BKMAX      equ 16          ; a refusal that may clear waits 1, 2, 4, 8,
                                ; then 16 polls before the next try

%ifdef STIK_LIB
  %define STIK_FIXED            ; the state is in the Sticky App block
  %ifndef STIK_DESKONLY
    %error "os88stik: STIK_LIB is a STIK_DESKONLY widget's - the library is DESK.DRV's"
  %endif
  %ifndef STIK_WIDGET
    %error "os88stik: STIK_LIB is a STIK_DESKONLY widget's - the library is DESK.DRV's"
  %endif
%endif
%ifdef STIK_LIBRARY
  %define STIK_FIXED
%else
%if STIK_BSS % 16
  %error "os88stik: STIK_BSS must be a multiple of 16 - the read wants a paragraph"
%endif
%endif
%ifdef STIK_PACK
 %ifndef STIK_UNPACK
  %error "os88stik: STIK_PACK wants STIK_UNPACK - a save spreads back what it squeezed"
 %endif
%endif
%ifdef STIK_DESKONLY
 %ifndef STIK_WIDGET
  %error "os88stik: STIK_DESKONLY is a widget's - v1 is a titled window"
 %endif
%endif

%ifdef STIK_FIXED
 %ifdef STIK_LIB
stik_file   equ os88_image_end + STIK_BSS                   ; header + payload
STIK_PAY    equ stik_file + STIK_HDR_SIZE
 %endif
stik_hd     equ os88_stiklib + STKL_HD                   ; the header known
stik_rec    equ os88_stiklib + STKL_REC                  ; then the rest,
                                ; in the same order, in the block
%else
stik_file   equ os88_image_end + STIK_BSS                   ; header + payload
STIK_PAY    equ stik_file + STIK_HDR_SIZE
stik_hd     equ stik_file                                   ; ...its header
stik_rec    equ stik_file + STIK_HDR_SIZE + STIK_PAYLEN       ; 128: the row
%endif
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
%ifdef STIK_FIXED
stik_fw     equ os88_stiklib + STKL_FW  ; born the widest (STIKD_FW)
stik_np     equ stik_ty + 2
 %ifdef STIK_LIBRARY             ; (a client's are a label's: t_stik checks)
 %if stik_np != os88_stiklib + STKL_NP || stik_nx != os88_stiklib + STKL_NX
  %error "os88stik: the state and os88api.inc's STKL_* have parted"
 %endif
 %endif
 %ifdef STIK_LIB
STIK_BSS_END equ STIK_BSS + STIK_HDR_SIZE + STIK_PAYLEN + STIK_SLACK
 %endif
%elifdef STIK_VARFACE
stik_fw     equ stik_ty + 2      ; dw the face's width NOW (SPEC.md 110.14.1)
stik_np     equ stik_fw + 2      ; db 1: no plate - the tag row at rest is ground
STIK_BSS_END equ stik_np + 1 - os88_image_end
%else
STIK_BSS_END equ stik_ty + 2 - os88_image_end
%endif
%endif
%ifdef STIK_VARFACE
 %ifndef STIK_WIDGET
  %error "os88stik: STIK_VARFACE is a widget's"
 %endif
 %ifndef STIK_REDRAW
  %define STIK_REDRAW           ; stik_resize redraws a face of the same size
 %endif
%endif
%ifndef STIK_FIXED
%if OSAPI_DI_SIZE < STIK_SLACK
  %error "os88stik: stik_rec is the read's slack before it is the row"
%endif
%endif
%ifdef STIK_FIXED                ; the descriptor and the block's own words
stik_d_file     equ os88_stiklib + STKL_D_FILE
stik_d_paylen   equ os88_stiklib + STKL_D_PAYLEN
stik_d_payver   equ os88_stiklib + STKL_D_PAYVER
stik_d_flags    equ os88_stiklib + STKL_D_FLAGS
stik_d_tagw     equ os88_stiklib + STKL_D_TAGW
stik_d_right    equ os88_stiklib + STKL_D_RIGHT
stik_d_lays     equ os88_stiklib + STKL_D_LAYS
stik_d_buf      equ os88_stiklib + STKL_D_BUF
stik_d_bst      equ os88_stiklib + STKL_D_BST
stik_d_brows    equ os88_stiklib + STKL_D_BROWS
stik_d_dname    equ os88_stiklib + STKL_D_DNAME
stik_d_cap      equ os88_stiklib + STKL_D_CAP
stik_d_isopen   equ os88_stiklib + STKL_D_ISOPEN
stik_d_tag      equ os88_stiklib + STKL_D_TAG
stik_d_paintcard equ os88_stiklib + STKL_D_PAINTCARD
stik_d_compose  equ os88_stiklib + STKL_D_COMPOSE
stik_d_oncard   equ os88_stiklib + STKL_D_ONCARD
stik_d_oncardup equ os88_stiklib + STKL_D_ONCARDUP
stik_d_onfocus  equ os88_stiklib + STKL_D_ONFOCUS
stik_d_ontick   equ os88_stiklib + STKL_D_ONTICK
stik_d_onclose  equ os88_stiklib + STKL_D_ONCLOSE
stik_d_sizeup   equ os88_stiklib + STKL_D_SIZEUP
stik_d_pack     equ os88_stiklib + STKL_D_PACK
stik_d_unpack   equ os88_stiklib + STKL_D_UNPACK
stik_d_click    equ os88_stiklib + STKL_D_CLICK
stik_d_mup      equ os88_stiklib + STKL_D_MUP
stik_d_tick     equ os88_stiklib + STKL_D_TICK
stik_d_close    equ os88_stiklib + STKL_D_CLOSE
stik_d_wake     equ os88_stiklib + STKL_D_WAKE
stik_xcp        equ os88_stiklib + STKL_XCP
stik_dsys       equ os88_stiklib + STKL_DSYS
stik_dapp       equ os88_stiklib + STKL_DAPP
stik_xbits      equ os88_stiklib + STKL_XBITS
stik_grip       equ os88_stiklib + STKL_GRIP
stik_le         equ os88_stiklib + STKL_LE
stik_xt         equ os88_stiklib + STKL_XT
; STIK_LCB name - the library calls the package's proc [stik_d_name] (0: none)
%macro STIK_LCB 1
    push word [stik_d_%1]
    call stik_cb
%endmacro
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

; STIK_ADDFW1 reg / STIK_MOVFW reg - the face's width (less one: its last
; column), a constant or [stik_fw] (STIK_VARFACE)
%macro STIK_ADDFW1 1
%ifdef STIK_VARFACE
    add %1, [stik_fw]
    dec %1
%else
    add %1, STIK_FACE_W - 1
%endif
%endmacro
%macro STIK_MOVFW 1
%ifdef STIK_VARFACE
    mov %1, [stik_fw]
%else
    mov %1, STIK_FACE_W
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
%ifndef STIK_LIBRARY             ; (the library knows the package by its
                                ; descriptor, and none of these is its)
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
%endif ; STIK_LIBRARY
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
%ifndef STIK_LIBRARY
%strlen STIK_TAGLEN STIK_TAG
STIK_TAGW   equ STIK_TAGLEN * 8
STIK_PW     equ STIK_TAGW + 8    ; the plate: the name and 4 px each side
STIK_PX     equ (STIK_FACE_W - STIK_PW) / 2
STIK_NX     equ (STIK_FACE_W - STIK_TAGW) / 2     ; the handle's name
STIK_GX     equ STIK_FACE_W - 18                 ; ...and its grip
%if STIK_PW > STIK_FACE_W
  %error "os88stik: STIK_TAG is wider than the face"
%endif
%ifndef STIK_VARFACE
%if STIK_NX < STIK_XX + STIK_XW + 1 || STIK_NX + STIK_TAGW > STIK_GX - 1
  %error "os88stik: STIK_TAG crowds the handle's x or its grip"
%endif
%else
%ifndef STIK_SIZEUP
  %error "os88stik: STIK_VARFACE wants STIK_SIZEUP"
%endif
STIK_NAMEMIN equ 2 * (STIK_XX + STIK_XW + 1) + STIK_TAGW  ; the handle letters
                                ; its name only on a face at least this wide
%endif
%endif ; STIK_LIBRARY

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

%ifdef STIK_LIB
%include "os88stikc.inc"        ; the CLIENT: thunks, its strings, the bind
%else ; the code - inline, or apps/stiklib.asm's one copy

; -----------------------------------------------------------------------------
; stik_load - the entry proc's half (SPEC.md 110.5, 110.5.2)
; in:  SI = the loader's name (an offset in KERNEL_SEG), DI = the template
; out: CF = 1 another instance's window is up and its toast has been said:
;      return CF = 1 from the entry proc. CF = 0 the state file is read and
;      the template's x,y are the saved ones - anchored to the nearer edges
;      when they were saved on a screen of another size. Every register but
;      the flags kept.
;
; THE INCLUDE'S OWN ROUTINES SAVE NOTHING THEY ARE NOT ASKED TO (SPEC.md 13):
; a window callback may clobber AX..DI and ES, giving back only W_PAINT's and
; W_ONCLICK's SI, so the helpers below say what they spend and the callbacks
; save only that. What the PACKAGE calls - stik_load, stik_made, stik_touch,
; stik_painted, stik_org, stik_place, stik_ctop, stik_slabs, stik_redraw,
; stik_redrawr, stik_blit - keeps the contract its header states.
; -----------------------------------------------------------------------------
%ifdef STIK_WIDGET
; STIK_COLH - the column of the layout at SI: DX = the FRAME's height, and
; [stik_fh] the face's height as SHOWN - every
; card, the gap under it and the tag row on a desk window; less the tag row
; and the last gap, under a title bar, in the titled fallback. AX, BX spent.
; Inline in stik_load's sizing, and a routine (stik_colh) where a VARFACE's
; stik_resize asks it again
%macro STIK_COLH 0
    mov al, [si+STIKL_N]         ; DX = the whole column: every card and the
    call stik_ctop              ; gap under it, then the tag row (CX)
    mov dx, bx
%ifdef STIK_VARFACE
    xor cx, cx                  ; (stik_load's own CX is the layout, 0..2)
%endif
    mov cl, [si+STIKL_TAG]
    add dx, cx
%ifndef STIK_DESKONLY
    mov bl, [si+STIKL_N]         ; ...and CX the tag row plus the last gap
    xor bh, bh
    shl bx, 1
    mov al, [bx+si+STIKL_CARDS-1]
    xor ah, ah
    add cx, ax
    test byte [stik_wf], STIK_W_DESK
    jnz %%d
    sub dx, cx                  ; the titled fallback: no tag, no last gap
    mov [stik_fh], dx
    add dx, TITLE_H + 1
    jmp short %%h
%%d:
%endif
    mov [stik_fh], dx
%%h:
%endmacro

; STIK_WSIZE - the WIDGET's size and default place (SPEC.md 110.11), AX, BX =
; the screen and DI = the template: the layout by the screen's HEIGHT, the
; template's size, and - with STIK_RIGHT - its place. A state file's place
; overrides it (STIK_WPLACE). Spends AX..DX and SI
%macro STIK_WSIZE 0
    push ax
    xor cx, cx                  ; TALL at 400 rows or more, MID from 300 -
%ifdef STIK_LIBRARY
    mov si, [stik_d_lays]
%else
    mov si, STIK_LAYOUTS        ; Hercules' 348 AND EGA's 350, which the
%endif
    cmp bx, 400                 ; adapter's kind would have called VGA -
    jae %%l                      ; COMPACT below (SPEC.md 110.11)
    inc cx
    add si, STIKL_SZ
    cmp bx, 300
    jae %%l
    inc cx
    add si, STIKL_SZ
%%l:
    mov [stik_li], cl
    mov [stik_lay], si
%ifdef STIK_VARFACE
%ifdef STIK_LIBRARY
    STIK_LCB sizeup             ; (none: [stik_fw] stays the descriptor's)
%else
    call STIK_SIZEUP            ; [stik_fw], and perhaps a layout of its own
%endif
    call stik_colh              ; ...shared with stik_resize: CX the width
    mov [di+WT_W], cx
%else
    STIK_COLH
    mov word [di+WT_W], STIK_FACE_W + 2
%endif
    mov [di+WT_H], dx
    pop ax
%ifdef STIK_LIBRARY
  %define STIK_RIGHT
%endif
%ifdef STIK_RIGHT
%ifdef STIK_LIBRARY
    sub ax, [stik_d_right]
    dec ax
%else
    sub ax, STIK_RIGHT + 1       ; the default place: the face STIK_RIGHT in
%endif
    mov [di+WT_X], ax           ; from the right edge, at the layout's y -
    mov ax, [si+STIKL_FY]        ; a state file's place overrides it
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jnz %%y
    sub ax, TITLE_H
%%y:
%endif
    mov [di+WT_Y], ax
%endif
%endmacro

; STIK_WPLACE - the restored place (AX, BX = the screen, DI = the template,
; [stik_fh] known): SI out = its y. Spends AX..DX
%macro STIK_WPLACE 0
    ; --- stik_wplace, written in place: the restored place
    mov si, [stik_hd+STIK_H_X] ; the FACE's origin out of the header - a v1
    mov dx, [stik_hd+STIK_H_Y] ; file's titled frame converted - anchored
    test byte [stik_hd+STIK_H_FLAGS], STIK_HF_CONT ; with the face's size,
    jnz %%f                      ; into the template's FRAME
    inc si                      ; v1: a titled frame, so the face is a
    add dx, TITLE_H             ; border in and a title bar down
%%f:
    push dx
    mov cx, [stik_hd+STIK_H_SW]
    STIK_MOVFW dx
    call stik_anchor
    dec si                      ; the face is one column in from the frame,
    mov [di+WT_X], si           ; in both forms
    pop si
    mov ax, bx
    mov cx, [stik_hd+STIK_H_SH]
    mov dx, [stik_fh]
    call stik_anchor
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jnz %%dy
    sub si, TITLE_H             ; wm_create clamps it onto the glass
%%dy:
%endif
    mov [di+WT_Y], si
%endmacro

%endif

stik_load:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    push di                     ; the template, for the place (.got)
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
%ifndef STIK_DESKONLY
    call stik_single            ; before any I/O - QUERY loads DESK.DRV: a
    jc .ret                     ; refused second launch costs no disk call
%endif
%ifdef STIK_WIDGET
    ; --- stik_wsize, written in place (it had this one caller): the
    ; WIDGET's size and default place (SPEC.md 110.11): QUERY, the
    ; layout by the screen's HEIGHT, the template's size, and - with
    ; STIK_RIGHT - its place. A state file's place overrides it (.got).
    ; STIK_DESKONLY's has no I/O - its QUERY is stik_single's - so it comes
    ; first there, and a refusal discards it
    pop di
    push di
%ifdef STIK_DESKONLY
    or byte [stik_wf], STIK_W_DESK  ; stik_single's QUERY takes, or the
                                    ; launch is refused
%else
    mov al, 0
    call OSAPI_WM_DESK          ; CF = 0: this kernel has the desk layer
    jc .q
    or byte [stik_wf], STIK_W_DESK
.q:
%endif
%ifndef STIK_VARFACE
    call OSAPI_VIDEO            ; AX = width, BX = height
    STIK_WSIZE
%endif                          ; (STIK_VARFACE: after the read, at .out -
%endif ; STIK_WIDGET            ; the size is the payload's)
%ifdef STIK_DESKONLY
    call stik_single            ; before any I/O: a refused second launch
    jc .ret                     ; costs no disk call
%endif
    call OSAPI_FILE_HERE        ; HOME, banked ONCE: every save comes back
    mov [stik_hdx], dx           ; here, and asks whether this is still it
    mov [stik_hbl], bl           ; (SPEC.md 110.5.1)
    call stik_enter0            ; standing in SYSTEM\APPDATA, or CF = 1
    jc .none
    call stik_io
    test bl, 15
    jnz .home                   ; a misplaced STIK_BSS: no home, said nowhere
%ifdef STIK_LIBRARY
    mov cx, [stik_d_paylen]
    add cx, STIK_HDR_SIZE + STIK_SLACK
%else
    mov cx, STIK_HDR_SIZE + STIK_PAYLEN + STIK_SLACK
%endif
    call OSAPI_FILE_READ        ; DX:AX = its size
    jnc .got
    cmp ax, FERR_NOENT
    je .home                    ; MISSING: the defaults, and the first save
    cmp ax, FERR_IO             ; makes it - and above FERR_IO it is the
    mov al, STIK_S_RDONLY       ; FILE's fault (too big, not ours). FERR_NODISK
    jbe .rz                     ; / FERR_IO: UNREAD - nothing is known about
                                ; it, so it is neither damaged nor ever
                                ; written over this session; the next launch
                                ; reads it again (SPEC.md 110.5)
.bad:
    mov al, STIK_S_DAMAGED
.rz:
    or [stik_st], al            ; DAMAGED: run empty - the first save
                                ; replaces it (STIK_DMGREPLACE) or never
                                ; (110.5.2). ES = DS still: stik_io's, which
                                ; the read and STIK_UNPACK keep
%ifdef STIK_LIBRARY
    mov di, bx                  ; (BX = stik_file: stik_io's, kept)
    mov cx, [stik_d_paylen]
    add cx, STIK_HDR_SIZE
%else
    mov di, stik_file
    mov cx, STIK_HDR_SIZE + STIK_PAYLEN
%endif
    xor al, al
    cld
    rep stosb
.home:
    call stik_leave
    jmp short .out
.none:
    or byte [stik_st], STIK_S_NOHOME
.out:
%ifdef STIK_VARFACE
    pop di                      ; the template
    push di
    call OSAPI_VIDEO            ; the payload is read: the layout, the
    STIK_WSIZE                  ; package's size, the template - and the
    test byte [stik_fl], STIK_F_PLACED ; saved place, anchored with the
    jz .np                      ; face's size as it is now (SPEC.md 110.14.1)
    call OSAPI_VIDEO
    STIK_WPLACE
    mov [stik_wy], si
.np:
%endif
    clc
.ret:
    pop di                      ; (the template; POP writes no flags)
    jmp stik_pop_8
.got:
    or dx, dx
    jnz .bad
    sub ax, STIK_HDR_SIZE       ; AS LONG AS ITS CONTENT (SPEC.md 110.5.3):
%ifdef STIK_LIBRARY
    jb .bad                     ; 0..STIK_PAYLEN payload bytes - 1.. PACKED -
    jnz .gn                     ; and the header's own length says how many
    test byte [stik_d_flags], STKF_PACK
    jnz .bad
.gn:
    cmp ax, [stik_d_paylen]
%else
%ifdef STIK_PACK                ; 0..STIK_PAYLEN payload bytes - 1.. PACKED -
    jbe .bad                    ; and the header's own length says how many
%else
    jb .bad
%endif
    cmp ax, STIK_PAYLEN
%endif
    ja .bad
    cmp [bx+STIK_H_LEN], ax     ; (BX = stik_file still: stik_io's, which
    jne .bad                    ; the read keeps - and so does STIK_UNPACK)
    cmp word [bx+STIK_H_MAGIC], STIK_HDR_MAGIC
    jne .bad
    cmp byte [bx+STIK_H_VER], STIK_HDR_VER
    jne .bad
%ifdef STIK_LIBRARY
    mov cl, [stik_d_payver]
    cmp [bx+STIK_H_PAYVER], cl
    jne .bad
    test byte [stik_d_flags], STKF_PACK
    jz .tail
    xchg ax, cx                 ; CX = the payload's length, the rest of it
    STIK_LCB unpack             ; zero (bss): spread it, or CF = 1 not ours
    jc .bad
    jmp short .hd
.tail:
    mov di, bx                  ; the tail the save left off comes back zero
    add di, STIK_HDR_SIZE
    add di, ax
    mov cx, [stik_d_paylen]
    sub cx, ax
    xor al, al
    cld
    rep stosb
.hd:
    mov si, bx                  ; the header, as known from here on (the
    mov di, stik_hd             ; save writes it back: stik_hdo)
    mov cx, STIK_HDR_SIZE / 2
    cld
    rep movsw
%else
    cmp byte [bx+STIK_H_PAYVER], STIK_PAYVER
    jne .bad
%endif
%ifdef STIK_LIBRARY
%elifdef STIK_PACK
    xchg ax, cx                 ; CX = the payload's length, the rest of it
    call STIK_UNPACK            ; zero (bss): spread it, or CF = 1 not ours
    jc .bad
%else
    mov di, STIK_PAY            ; the tail the save left off comes back zero
    add di, ax                  ; - the read may have put STIK_SLACK's bytes
                                ; there (ES = DS: stik_io's)
    mov cx, STIK_PAYLEN
    sub cx, ax
    xor al, al
    cld
    rep stosb
%endif
    test byte [bx+STIK_H_FLAGS], STIK_HF_POS
    jz .home
%ifdef STIK_VARFACE
    or byte [stik_fl], STIK_F_PLACED ; placed once the size is known (.out)
    jmp .home
%else
    pop di                      ; the template
    push di
    call OSAPI_VIDEO            ; AX = width, BX = height
%ifdef STIK_WIDGET
    STIK_WPLACE
%else
    mov cx, [stik_hd+STIK_H_SW]
    mov dx, [di+WT_W]
    mov si, [stik_hd+STIK_H_X]
    call stik_anchor            ; SI = x on this screen
    mov [di+WT_X], si
    mov ax, bx
    mov cx, [stik_hd+STIK_H_SH]
    mov dx, [di+WT_H]
    mov si, [stik_hd+STIK_H_Y]
    call stik_anchor
    mov [di+WT_Y], si           ; wm_create clamps it onto the glass (39.7)
%endif
    mov [stik_wy], si            ; ...and stik_made gives back what that clamp
    or byte [stik_fl], STIK_F_PLACED ; took to keep it off the dock
    jmp .home
%endif

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
; is said. Spends AX, CX, DX, SI, DI and ES (stik_load's, which keeps them)
stik_single:
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
    jb .slot                    ; (not taken: CF = 0, none found)
%ifdef STIK_LIBRARY
    ret                         ; (the package's bind asked QUERY first)
%elifdef STIK_DESKONLY
    mov al, 0                   ; QUERY (stik_load's, asked here so that a
    call OSAPI_WM_DESK          ; refusal costs no disk call): no desk layer
    jnc .r                      ; - DESK.DRV missing, or no heap for it - is
    mov si, stik_nodesk         ; no launch, said (SPEC.md 110.7)
    jmp short .say
%else
    ret
%endif
.found:
%ifdef STIK_LIBRARY
    mov si, [stik_d_isopen]
%else
    mov si, stik_isopen
.say:
%endif
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST            ; the entry proc's own toast: the loader then
    stc                         ; says nothing over it (SPEC.md 20.12.4)
%ifndef STIK_LIBRARY
.r:
%endif
    ret

; -----------------------------------------------------------------------------
; stik_made - after OSAPI_WM_CREATE: BX = the window. Installs the tick and the
; close negotiator; every register kept, the flags not - clear CF yourself
; before the entry proc returns
; -----------------------------------------------------------------------------
stik_made:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    mov [stik_win], bx
    call stik_kwin              ; the record (SPEC.md 20.1)
%ifdef STIK_WIDGET
    ; --- stik_wmade, written in place (it had this one caller): MAKE, or
    ; the titled fallback's two promises; the prefilters and the wake
    ; (SPEC.md 110.11)
%ifdef STIK_DESKONLY
    mov al, 1
    call OSAPI_WM_DESK          ; MAKE: we are in the entry proc, the window
                                ; hidden and nobody's yet (SPEC.md 11.103.9) -
                                ; its refusals are all a caller's mistakes
%else
    test byte [stik_wf], STIK_W_DESK
    jz .t
    mov al, 1
    call OSAPI_WM_DESK          ; MAKE: we are in the entry proc, the window
    jnc .cb                     ; hidden and nobody's yet (SPEC.md 11.103.9)
    and byte [stik_wf], ~STIK_W_DESK & 0FFh   ; refused: painted as titled
.t:
    mov al, 1
    call OSAPI_WM_OWNBG         ; every pixel is ours (OSAPI_WM_DAMAGE wants it)
%ifdef STIK_COLOR
    mov al, OSAPI_SAVEU_ON      ; ...in the colours it is drawn in: a widget
%else                           ; whose cards are not two-colour (STIK_COLOR,
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP   ; Sticky Note's papers) keeps
%endif                          ; four planes; the rest two, as v1
    call OSAPI_WM_SAVEU
.cb:
%endif
%ifdef STIK_LIBRARY              ; (the package's thunks: a callback is a
    mov ax, [stik_d_click]      ; near proc in ITS segment, SPEC.md 20.2)
    call OSAPI_WM_ONCLICK
    mov ax, [stik_d_mup]
    call OSAPI_WM_ONMOUSEUP
    mov ax, [stik_d_wake]
%else
    mov ax, stik_click
    call OSAPI_WM_ONCLICK
    mov ax, stik_mup
    call OSAPI_WM_ONMOUSEUP
    mov ax, STIK_WAKE           ; the hover edge's wake - and kern_small's
%endif
    call OSAPI_WM_ONWAKE        ; worker kick, which stik_wake tells apart
    mov byte [stik_tagp], STIK_P_NONE
    call OSAPI_GET_TICKS
    mov [stik_tbeat], ax         ; the first beat is due at once
%endif
    test byte [stik_fl], STIK_F_PLACED
    jz .rec
    mov ax, [stik_wy]            ; THE RESTORED y. wm_create's fit keeps every
    cmp ax, [es:bx+W_Y]         ; frame that fits above the dock, but a frame
    jle .rec                    ; DRAGGED half over it is left there on
    push ax                     ; purpose (wm_dock_snap) - and that is the
    push bx                     ; place the user chose. Give back what the fit
    call OSAPI_WM_DISPLAY       ; took, down to the lowest y whose frame and
    mov cx, bx                  ; shadow are on this display - wm_fit's own
    pop bx                      ; floor for a KEEPH window, ph - h - 1 - and
    pop ax
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
%ifdef STIK_WIDGET
    mov si, bx                  ; THE PLACE ON RECORD is where it opened: the
    call stik_place             ; poll owes a save only for another one, so a
    or byte [stik_hd+STIK_H_FLAGS], STIK_HF_CONT ; ...a FACE's from now on: a
                                ; v1 file is converted by its next save, once
%else
    STIK_PLACE bx
%endif
    mov [stik_px], ax            ; launch alone writes nothing (110.5.2). The
    mov [stik_py], dx            ; header's screen goes with it, or a later
    call stik_hdr               ; save would pair this place with the old
                                ; screen; STIK_HF_POS is left as the file had
                                ; it, so a close still writes a missing file
%ifdef STIK_LIBRARY
    mov ax, [stik_d_tick]
    call OSAPI_WM_ONTIMER
    mov ax, [stik_d_close]
%else
    mov ax, stik_tick
    call OSAPI_WM_ONTIMER
    mov ax, stik_close
%endif
    call OSAPI_WM_ONCLOSE
    mov ax, 1                   ; the first tick: the window is up by then
    call OSAPI_WM_TIMER         ; CF = 1: kern_small, which has no timer -
%ifndef STIK_DESKONLY           ; the WORKER ARM (SPEC.md 110.5.2): the
    jnc .tm                     ; first paint hires it, and its wake is
    or byte [stik_st], STIK_S_WORKER ; the tick - a widget's W_ONWAKE is
%ifndef STIK_WIDGET             ; STIK_WAKE already (above), which
    mov ax, stik_wake           ; reaches stik_wake, and stik_wake reads
    call OSAPI_WM_ONWAKE        ; STIK_S_WORKER (SPEC.md 110.11)
%endif
.tm:
%endif
    jmp stik_pop_8

%ifndef STIK_DESKONLY
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

%endif

; stik_touch - the payload changed: owe a save, which the next tick makes.
; Every register and the flags kept
stik_touch:
    pushf
    or byte [stik_st], STIK_S_DIRTY
    popf
    ret

; stik_push / stik_pop_8 - the include's ONE register bank (os88api.inc's
; OS88_PUSHER and OS88_POPLADDER), for a routine that keeps every register:
; `call stik_push` first, `jmp stik_pop_8` last - the ladder's RET is its.
; Neither writes a flag, so a routine's CF answer survives the ladder. The
; UI task's stack is the kernel's (SPEC.md 8.6): 18 bytes there, none on a
; worker's
OS88_PUSHER stik_push, es, ax, bx, cx, dx, si, di, bp
OS88_POPLADDER stik_pop, es, ax, bx, cx, dx, si, di, bp

%ifndef STIK_DESKONLY
; stik_worker - kern_small's tick: sleep, ask to live, kick the UI task. It
; touches no file and no pixel (SPEC.md 20.6 rule 7); stik_wake does both
stik_worker:
    mov ax, STIK_TICKS
    call OSAPI_TASK_SLEEP
    mov bx, [stik_win]
    call OSAPI_TASK_ALIVE       ; never comes back once the window is closing
    call OSAPI_WM_WAKE          ; one queued per window: a busy UI task loses
    jmp short stik_worker       ; nothing by it

%endif

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
; stik_tick - the timer (SI = the window; the UI task, the gfx lock held).
; A window callback: spends AX..DI and ES (SPEC.md 13)
; -----------------------------------------------------------------------------
%ifndef STIK_WIDGET             ; (a widget's multiplexes, below)
stik_tick:
    call stik_beat
    mov bx, [stik_win]
    xor ax, ax
    xchg al, [stik_nx]           ; a sooner beat the application asked for,
    or al, al                   ; once (SPEC.md 110.5)
    jnz .t
    mov al, STIK_TICKS
.t:
    call OSAPI_WM_TIMER
    ret
%endif

; stik_beat - one tick's work, on either arm: the UI task, the lock held.
; Spends AX..DI and ES - its callers are window callbacks
stik_beat:
%ifdef STIK_DESKONLY
    test byte [stik_st], STIK_S_TRIED
%else
    test byte [stik_st], STIK_S_TRIED | STIK_S_WORKER
%endif
    jnz .poll                   ; (kern_small's OSAPI_DESK_ITEM is the
    or byte [stik_st], STIK_S_TRIED  ; refusal: its path walk would buy nothing)
    ; --- THE REGISTRATION (it was stik_register): EVERY launch, whatever
    ; the state file said - the row is the program's (SPEC.md 110.5). The
    ; Sticky App row (SPEC.md 110.2): our volume, KIND 1, the mark, the path
    ; we were launched from and our name. A refusal - no system disk in A:,
    ; a full table, kern_small - is CTRL.DRV's to say (or nobody's): the
    ; widget runs on, unremembered
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
    jc .poll                    ; sc_m_new's own idiom - and the mark goes
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
    ja .poll                    ; them: a longer one is not remembered
    mov di, stik_rec + OSAPI_DI_CAP
%ifdef STIK_LIBRARY
    mov si, [stik_d_cap]
%else
    mov si, stik_cap
%endif
    mov cx, OSAPI_DI_BODY - OSAPI_DI_CAP
    rep movsb                   ; the caption (and the header the kernel
    mov si, STIK_ICON           ; stamps), then the picture
    mov cx, 32
    rep movsw
    mov si, stik_rec
    mov al, 1
    call OSAPI_DESK_ITEM
    jc .poll
    or byte [stik_st], STIK_S_ROW
.poll:
    call stik_kwin              ; ES = KERNEL_SEG on entry to a callback -
                                ; and the registration's ES = DS since
%ifdef STIK_WIDGET
    call stik_refit             ; a drop that left the tag off the glass
%endif                          ; (SPEC.md 11.103.6's bound)
    STIK_PLACE si
    cmp ax, [stik_px]
    jne .moved                  ; still moving, or just started
    cmp dx, [stik_py]
    jne .moved
    call stik_repos             ; two polls agree: the drag is over, and
                                ; against the place on record - where it was
                                ; saved, or where it opened (stik_made) -
                                ; only another place owes a save: a launch
                                ; alone writes nothing. (Into .moved after:
                                ; the stores write what is there already)
.moved:
    mov [stik_px], ax
    mov [stik_py], dx
.app:
%ifdef STIK_LIBRARY
    STIK_LCB ontick             ; CF = 1: the package is mid-edit - any save
    jc .arm                     ; owed waits for a later poll (SPEC.md 110.5)
%elifdef STIK_ONTICK
    call STIK_ONTICK            ; CF = 1: the package is mid-edit - any save
    jc .arm                     ; owed waits for a later poll (SPEC.md 110.5)
%endif
    test byte [stik_st], STIK_S_DIRTY
    jz .arm
    cmp byte [stik_bn], 0
    je stik_save
    dec byte [stik_bn]           ; backing off a refusal that may clear
.arm:
    ret

; stik_save - the state file, header and payload in one write (SPEC.md
; 110.5.1). Not with no home, not once a refusal was for good. A DAMAGED file
; is deleted first and then written afresh - or, with no STIK_DMGREPLACE,
; never written at all (110.5.2). A refusal that may clear keeps
; it owed and backs the next try off; one that will not makes the state
; read-only for the session. Spends AX..DI and ES (its callers are window
; callbacks); out CF = 0 always, which stik_close's answer is
stik_save:
    test byte [stik_st], STIK_S_NOHOME | STIK_S_RDONLY
    jnz .x
%ifdef STIK_LIBRARY
    test byte [stik_st], STIK_S_DAMAGED
    jz .e
    test byte [stik_d_flags], STKF_DMGREPLACE
    jz .x                       ; (CF = 0: the TEST's)
.e:
%elifndef STIK_DMGREPLACE
    test byte [stik_st], STIK_S_DAMAGED
    jnz .x
%endif
    call stik_enterf            ; CF = 1: not home now - the disk is out, or
    jc .soon                    ; another one is in
%ifdef STIK_LIBRARY
  %define STIK_DMGREPLACE_
%elifdef STIK_DMGREPLACE
  %define STIK_DMGREPLACE_
%endif
%ifdef STIK_DMGREPLACE_
    test byte [stik_st], STIK_S_DAMAGED
    jz .w
%ifdef STIK_LIBRARY
    mov si, [stik_d_dname]
%else
    mov si, stik_dname          ; the damaged file goes FIRST: a replace
%endif
    call OSAPI_FILE_DELETE      ; claims the new chain before it frees the
    and byte [stik_st], ~STIK_S_DAMAGED & 0xFF  ; old (18.4), and a full
.w:                             ; system disk has no room for both
%endif
    mov word [stik_hd+STIK_H_MAGIC], STIK_HDR_MAGIC
    mov byte [stik_hd+STIK_H_VER], STIK_HDR_VER
%ifdef STIK_LIBRARY
    mov al, [stik_d_payver]
    xor ah, ah                  ; (+13 = 0)
    mov [stik_hd+STIK_H_PAYVER], ax
    test byte [stik_d_flags], STKF_PACK
    jz .np
    STIK_LCB pack               ; CX = the payload's bytes in use, squeezed
    mov [stik_hd+STIK_H_LEN], cx
    call stik_hdo
    call stik_io
    add cx, STIK_HDR_SIZE
    call OSAPI_FILE_WRITE
    pushf
    push ax
    mov cx, [stik_hd+STIK_H_LEN]
    STIK_LCB unpack             ; spread back - it cannot refuse what
    pop ax                      ; STIK_PACK made
    popf
    jmp short .wr
.np:
    push ds                     ; AS LONG AS ITS CONTENT (SPEC.md 110.5.3)
    pop es
    mov cx, [stik_d_paylen]
    mov di, [stik_d_file]
    add di, cx
    add di, STIK_HDR_SIZE - 1
    xor al, al
    std
    repe scasb
    cld
    je .zl                      ; all zero: CX = 0
    inc cx                      ; stopped ON a non-zero byte: it is kept
.zl:
    mov [stik_hd+STIK_H_LEN], cx
    call stik_hdo
    call stik_io
    add cx, STIK_HDR_SIZE
    call OSAPI_FILE_WRITE
.wr:
%else
%if STIK_PAYVER > 255 || STIK_PAYVER < 0
  %error "os88stik: STIK_PAYVER is a byte, and +13 is its zero"
%endif
    mov word [stik_hd+STIK_H_PAYVER], STIK_PAYVER  ; +13 = 0
%endif
%ifdef STIK_LIBRARY
%elifdef STIK_PACK
    call STIK_PACK              ; CX = the payload's bytes in use, squeezed
    mov [stik_hd+STIK_H_LEN], cx
    call stik_io
    add cx, STIK_HDR_SIZE
    call OSAPI_FILE_WRITE
    pushf
    push ax
    mov cx, [stik_hd+STIK_H_LEN]
    call STIK_UNPACK            ; spread back - it cannot refuse what
    pop ax                      ; STIK_PACK made
    popf
%else
    push ds                     ; AS LONG AS ITS CONTENT (SPEC.md 110.5.3):
    pop es                      ; the payload less its ZERO TAIL - which a
    mov di, STIK_PAY + STIK_PAYLEN - 1  ; load gives back as zeros, so the
    mov cx, STIK_PAYLEN         ; payload in memory is the same either way
    xor al, al                  ; and a notes file holding one note is one
    std                         ; cluster and not five
    repe scasb
    cld
    je .zt                      ; all zero: CX = 0
    inc cx                      ; stopped ON a non-zero byte: it is kept
.zt:
    mov [stik_hd+STIK_H_LEN], cx
    call stik_io
    add cx, STIK_HDR_SIZE
    call OSAPI_FILE_WRITE
%endif
    call stik_leave             ; AX and the flags kept
    jnc .ok
    cmp ax, FERR_IO
    jbe .soon                   ; FERR_NODISK, FERR_IO: may clear
    or byte [stik_st], STIK_S_RDONLY ; write-protected, full, a full folder,
.ok:                            ; a protected name: will not, this session
    and byte [stik_st], ~STIK_S_DIRTY & 0xFF
    mov byte [stik_bk], 0
.x:
    ret                         ; (CF = 0: the AND's, or the TEST's)
.soon:
%if STIK_BKMAX != 16
  %error "os88stik: stik_save's backoff doubles to a power of two, 16"
%endif
    mov al, [stik_bk]            ; 1, 2, 4, 8, then 16 polls (2 s to 32 s)
    test al, STIK_BKMAX         ; ...16 stays 16 (and CF = 0)
    jnz .s
    shl al, 1                   ; (CF = 0: AL was 8 at most)
    jnz .s
    inc ax
.s:
    mov [stik_bk], al
    mov [stik_bn], al
    ret

%ifdef STIK_LIBRARY
; stik_hdo - the header as known, into the state file's buffer before a
; write (stik_load took it out after the read). ES = DS out; CX kept, SI and
; DI spent
stik_hdo:
    push cx
    push ds
    pop es
    mov si, stik_hd
    mov di, [stik_d_file]
    mov cx, STIK_HDR_SIZE / 2
    cld
    rep movsw
    pop cx
    ret
%endif

; stik_io - the state file's registers: ES:BX = stik_file, SI = its name,
; DX = 0 (the count's high word)
stik_io:
    push ds
    pop es
%ifdef STIK_LIBRARY
    mov bx, [stik_d_file]
    mov si, [stik_d_dname]
%else
    mov bx, stik_file
    mov si, stik_dname
%endif
    xor dx, dx
    ret

; stik_kwin - ES = KERNEL_SEG and SI = our window: its record. AX spent
stik_kwin:
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    ret

; stik_crepos - stik_close's: no place on the record at all (STIK_HF_POS
; clear, so a close writes a missing file), or stik_repos's answer
stik_crepos:
    test byte [stik_hd+STIK_H_FLAGS], STIK_HF_POS
    jz stik_pos
; stik_repos - AX, DX = the place now: another place than the one on record
; is owed a save (stik_pos). Keeps AX, BX and DX
stik_repos:
    cmp ax, [stik_hd+STIK_H_X]
    jne stik_pos
    cmp dx, [stik_hd+STIK_H_Y]
    jne stik_pos
    ret

; stik_pos - AX, DX = the frame's place now: into the header with the screen
; it is on, and owed a save. Keeps AX, BX and DX; spends CX
stik_pos:
%ifdef STIK_WIDGET
    or byte [stik_hd+STIK_H_FLAGS], STIK_HF_POS | STIK_HF_CONT
%else
    or byte [stik_hd+STIK_H_FLAGS], STIK_HF_POS
%endif
    or byte [stik_st], STIK_S_DIRTY
; stik_hdr - AX, DX = a place: into the header, with the screen it is on.
; Keeps AX, BX and DX; spends CX
stik_hdr:
    mov [stik_hd+STIK_H_X], ax
    mov [stik_hd+STIK_H_Y], dx
    push ax
    push bx
    push dx
    call OSAPI_VIDEO
    mov [stik_hd+STIK_H_SW], ax
    mov [stik_hd+STIK_H_SH], bx
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; stik_close - the close negotiator (SI = the window; UI task, lock held). The
; application's own close, the row away BY NAME, the last place saved; and the
; close goes ahead (CF = 0). Restart and power-off do not come here (SPEC.md
; 110.4): the row stays, which is what restore wants. A remove REFUSED - no
; system disk in A: to read CTRL.DRV from, whose toast asks for it - declines
; the FIRST close (CF = 1), so the user can put it back and close again; the
; next one goes ahead whatever the remove says, so the window can never be
; made unclosable (SPEC.md 75.1). STIK_ONCLOSE can therefore run twice.
; A window callback: spends AX..DI and ES
; -----------------------------------------------------------------------------
stik_close:
%ifdef STIK_LIBRARY
    STIK_LCB onclose
%elifdef STIK_ONCLOSE
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
.r:                             ; disk is not home either
    ret
.gone:
.pos:
    call stik_kwin              ; the window record again
    STIK_PLACE si
    call stik_crepos            ; no place on record, or another one: saved
    test byte [stik_st], STIK_S_DIRTY   ; (CF = 0)
    jz .r
    jmp stik_save               ; ...which answers CF = 0 too

; stik_enterf - stik_enter0 behind ONE FULL MOUNT: the SAVE's (SPEC.md
; 110.5.1). The save is the one write a Sticky App makes with nobody looking -
; a beat after a drag, perhaps hours after the user swapped the disk - and a
; quiet stand trusts a boot sector's signature, which two disks FORMATTED here
; share (dskw_fmt_t2 stamps one serial: 18.8.2's accepted residual). A full
; mount reads this disk's own FAT and drops 18.95's cache, so the witness and
; the walk after it read the platter and not the last disk's sectors. Every
; stand after it is a word. CF = 1 not mounted (nothing marked: still home)
stik_enterf:
    mov bl, [stik_hbl]
    xor dx, dx
    call OSAPI_FILE_GOTO        ; the root, mounted afresh
    jnc stik_enter0
    ret
; stik_enter0 - stik_enter with DX = 0: SYSTEM\APPDATA, by a walk. What
; this file's own two visits make - the load, and a save, which is a CREATE
; or a REPLACE and so goes where a walk says, never where a cluster is
; remembered (apps/os88data.inc's rule)
stik_enter0:
    xor dx, dx
; stik_enter - IS THE DISK IN THE DRIVE STILL HOME? Then stand in a folder of
; it (SPEC.md 110.5.1). in DX = a folder of HOME's volume the caller banked
; on an earlier visit, or 0 for SYSTEM\APPDATA by a walk. out CF = 0 standing
; there, DX kept, AX = the first cluster of our own file in home - the
; witness's answer, which a caller banking a folder keeps beside it and
; compares, so the bank is THIS disk's and not every disk that passes;
; CF = 1 not there, and the instance is home again. Spends BX, CX, SI, DI, ES.
;
; The first stand is OSAPI_FILE_GOTO_QM to home, and that is the whole
; question of WHICH DISK, answered by the kernel: a word while the spindle
; is still turning, when no hand can have swapped the medium (18.9.1); else
; a QUIET mount (18.9.1.1), which reads this disk's boot sector, signs it
; (dsk_bpb_sig) and keeps the banked FAT window and 18.95's cache only when
; the signature is the one they were banked under - and os88disk derives
; every volume's serial from its content, so two different disks sign
; differently (18.8.2). A swapped disk is therefore mounted as ITSELF, its
; own FAT read, before a byte of it is believed. It was a FULL mount on
; every visit, which read the root for a listing and dropped 18.95's cache
; that the walk below then read back a sector at a time; only the save
; (stik_enterf) still makes one, for the one case a signature cannot tell.
;
; Then the witness, unchanged: our own file in the banked folder (FIND).
; Every stand after it is a word - the volume was just stood on, the motor
; is turning (18.9.1) and this is one callback with the lock held (18.9.3's
; argument) - so a banked folder is ONE word and the walk is one per
; component, each found BY NAME in its parent
stik_enter:
    push dx                     ; the folder asked for
    mov dx, [stik_hdx]
    call stik_goqm              ; HOME: a word, or a quiet mount. A refusal
    jc .x                       ; marks nothing: still home, nothing to undo
    mov si, stik_name
    call stik_seek              ; IS THIS HOME? our own file in our folder
    jc .no
    cmp al, OSAPI_FT_DIR        ; (a type is 0..3: AH is 0)
    je .no
    pop ax
    push ax
    xchg ax, dx                 ; DX = the folder asked for, AX = the witness
    push ax
    call stik_goqm              ; it - or, DX = 0, the root - a word
    jc .n2
    or dx, dx
    jnz .ok                     ; a banked folder: there (CF = 0, the OR's)
    mov si, stik_dsys
    call stik_dive
    jc .n2
    mov si, stik_dapp
    call stik_dive
    jc .n2
.ok:
    pop ax                      ; (POP writes no flag: CF = 0 still)
    pop dx
    ret
.n2:
    pop ax
.no:
    call stik_leave
    stc
.x:
    pop dx
    ret

; stik_leave - home, quietly. AX and the flags kept; spends BX..DI
stik_leave:
    pushf
    push ax
    mov dx, [stik_hdx]
    call stik_goqm              ; CX, SI and DI are a file driver's (os88api)
    pop ax
    popf
    ret

; stik_dive - into the folder named at SI, in the current one; CF = 1 none.
; Spends AX..DI and ES
stik_dive:
    call stik_seek              ; DX = its first cluster
    jc stik_goqm.x
    cmp ax, OSAPI_FT_DIR
    stc
    jne stik_goqm.x             ; a FILE of that name: not a folder
; stik_goqm - OSAPI_FILE_GOTO_QM on HOME's volume: DX = the folder. Its spend
stik_goqm:
    mov bl, [stik_hbl]
    call OSAPI_FILE_GOTO_QM
.x:
    ret

; stik_seek - the entry named SI in the current folder, into stik_fnd. out CF = 0
; AX = its type (+14), DX = its first cluster (+16); CF = 1 there is none.
; SI kept; spends CX, DI and ES (= DS)
stik_seek:
    push ds
    pop es
    cld
    xor cx, cx
.each:
    mov di, stik_fnd
    call OSAPI_FILE_FIND
    jc .out
    push si
.c:
    lodsb
    scasb
    jne .ne
    or al, al
    jnz .c                      ; (the NUL matched: CF = 0, ZF = 1)
.ne:
    pop si
    jne .each
    mov ax, [stik_fnd + 14]
    mov dx, [stik_fnd + 16]
.out:
    ret

%ifdef STIK_WIDGET
; =============================================================================
; THE WIDGET TREATMENT (SPEC.md 110.11) - the face, its tag and its gestures.
; No pixel of it is the kernel's: the kernel's part is SPEC.md 11.103's layer.
; stik_load sizes and places the template, stik_made MAKEs and installs the
; prefilters; what follows is the rest
; =============================================================================
%if STIK_HV_HIDDEN != 0 || STIK_HV_DWELL != 1 || STIK_HV_SHOWN != 2 || STIK_HV_LINGER != 3
  %error "os88stik: the hover states are 0..3, the two with a deadline ODD"
%endif
%if STIK_P_REST != 0 || STIK_P_FOCUS != 1 || GFXE_INV != 1 || STIK_W_FOCUS != 2
  %error "os88stik: stik_tagdraw and stik_focus lean on these values"
%endif

; stik_place - SI = our window (ES = KERNEL_SEG): AX, DX = the FACE's origin.
; Nothing else changes
stik_place:
    mov ax, [es:si+W_X]
    inc ax
    mov dx, [es:si+W_Y]
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jnz .r
    add dx, TITLE_H
.r:
%endif
    ret

; stik_forg - a paint's start: the glyph table is asked for again - a resume
; may have MOVED it (SPEC.md 5.12.8) - then stik_org
stik_forg:
    mov word [gfxe_fseg], 0
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
; only when it is UP. SI and ES kept; spends AX..DX
stik_refit:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .r
%endif
    test byte [es:si+W_FLAGS], 2
    jz .r
    push si
    mov bx, si
    call OSAPI_WM_DISPLAY       ; BX = the display's height (AX CX SI DX)
    pop si
    mov ax, [es:si+W_Y]
    add ax, [es:si+W_H]
    cmp ax, bx
    jle .r                      ; on the glass
    sub bx, [es:si+W_H]
    cmp bx, MBAR_H
    jge .y
    mov bx, MBAR_H              ; a face taller than this display (the
.y:                             ; layout is the PRIMARY's) goes up to the bar
    cmp bx, [es:si+W_Y]         ; ...and ONLY up, once: at the bar already, it
    jge .r                      ; is left where it is, never re-shown a beat
    push bx
    mov bx, si
    call OSAPI_WM_HIDE
    pop ax
    mov [es:si+W_Y], ax
    call OSAPI_WM_SHOW
.r:
    ret

; -----------------------------------------------------------------------------
; stik_paint - W_PAINT (SI = the window, lock held, the kernel's clip armed):
; only what intersects OSAPI_WM_DAMAGE, every pixel once - the ground columns
; and the gaps by OSAPI_GFX_DESK, each card by STIK_PAINTCARD, the tag from
; the state - then stik_painted. W_PAINT's contract: SI given back, AX..DI,
; BP and the flags spent (SPEC.md 11's W_PAINT)
; -----------------------------------------------------------------------------
stik_paint:
    push si
    mov bx, si
    call OSAPI_WM_DAMAGE        ; CF = 1: the whole of it, AX..DX either way
    call stik_pbody
    pop si
%ifdef STIK_DESKONLY
    ret
%else
    jmp stik_painted            ; tail: kern_small hires its worker there
%endif

; stik_redraw - the whole face again OUTSIDE a paint (SI = the window, lock
; held): OSAPI_WM_CLIP_SET, then stik_paint's body over all of it. Nothing
; when none of the window is on the glass. Every register kept.
; OPT-IN (%define STIK_REDRAW): its one caller was the calendar's About card,
; which is a toast now (SPEC.md 110.3), and nothing else calls it
%ifdef STIK_REDRAW
stik_redraw:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    mov ax, 8000h               ; the damage is everything
    mov bx, ax
    mov cx, 7FFFh
    mov dx, cx
    jmp short stik_redrawr.all
%endif

; stik_redrawr - stik_redraw over the damage AX, BX - CX, DX alone (screen,
; inclusive): what a card over the face uncovered, its slabs and nothing
; else (SPEC.md 110.11). Every register kept
stik_redrawr:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
.all:
    push bx
    mov bx, si
    call OSAPI_WM_CLIP_SET
    pop bx
    jc .x                       ; none of it showing
    call stik_pbody
.x:
%ifndef STIK_DESKONLY
    call stik_painted           ; as stik_paint's (it keeps every register)
%endif
    jmp stik_pop_8

; stik_pbody - the paint's body: AX..DX = the damage (screen, inclusive), SI =
; the window, ES = KERNEL_SEG, the lock held and clipped. Spends AX..DI, BP
; and ES
stik_pbody:
    mov [stik_dmg], ax
    mov [stik_dmg+2], bx
    mov [stik_dmg+4], cx
    mov [stik_dmg+6], dx
    call stik_forg              ; AX, DX = the face
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .cards
%endif
    mov bx, dx                  ; the two ground columns, the column's height
    add dx, [stik_fh]
    dec dx
    dec ax
    call stik_desk              ; at x - 1...
%ifdef STIK_VARFACE
    add ax, [stik_fw]
    inc ax
%else
    add ax, STIK_FACE_W + 1
%endif
    call stik_desk              ; ...and x + the face's width
.cards:
    mov di, [stik_fy]            ; DI = the walk's screen y, SI its pair
    mov si, [stik_lay]
    add si, STIKL_CARDS
    mov byte [stik_ci], 0
.card:
    xor ax, ax
    mov al, [si]
    mov [stik_ch], ax
    mov [stik_cy], di
    call stik_rowc              ; CF = 1: the damage misses it
    jc .gap
    sub bx, di                  ; card-local rows
    sub dx, di
    mov al, [stik_ci]
    push si
    push di
%ifdef STIK_LIBRARY
    STIK_LCB paintcard
%else
    call STIK_PAINTCARD
%endif
    pop di
    pop si
.gap:
    add di, [stik_ch]
    xor ax, ax
    mov al, [si+1]
    or al, al
    jz .next
    mov [stik_gh], ax
%ifndef STIK_DESKONLY
    mov bx, [stik_lay]
    mov cl, [stik_ci]
    inc cx
    cmp cl, [bx+STIKL_N]
    jb .g                       ; the last gap is the tag's - the titled
    test byte [stik_wf], STIK_W_DESK ; fallback has neither
    jz .next
.g:
%endif
    call stik_rowc
    jc .gn
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .tg
%endif
    call OSAPI_GFX_DESK         ; the desktop, in the gap
%ifndef STIK_DESKONLY
    jmp short .gn
.tg:
    call stik_tgap              ; white, with a rule through its middle
%endif
.gn:
    add di, [stik_gh]
.next:
    inc si
    inc si
    inc byte [stik_ci]
    mov bx, [stik_lay]
    mov al, [stik_ci]
    cmp al, [bx+STIKL_N]
    jb .card
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz stik_cut.r
%endif
    xor ax, ax                  ; the tag row: drawn whole if the damage
    mov al, [bx+STIKL_TAG]       ; touches it
    call stik_rowc
    jc stik_cut.r
    mov byte [stik_tagp], STIK_P_NONE ; drawn whole, the ground beside it too
    jmp stik_tagdraw

; stik_rowc - DI = a row's screen top, AX = its rows: AX..DX = the face's
; width over them, cut to the paint's damage - stik_cut's answer
stik_rowc:
    mov bx, di
    mov dx, di
    add dx, ax
    dec dx
    mov ax, [stik_fx]
    mov cx, ax
    STIK_ADDFW1 cx
; stik_cut - AX..DX (inclusive) intersected with the paint's damage: CF = 1
; empty, else AX..DX the intersection
stik_cut:
    push si
    mov si, stik_dmg
    cmp ax, [si]
    jge .a
    mov ax, [si]
.a:
    cmp bx, [si+2]
    jge .b
    mov bx, [si+2]
.b:
    cmp cx, [si+4]
    jle .c
    mov cx, [si+4]
.c:
    cmp dx, [si+6]
    jle .d
    mov dx, [si+6]
.d:
    pop si
    cmp ax, cx
    jg .e
    cmp bx, dx
    jg .e
    clc
.r:
    ret
.e:
    stc
    ret

; stik_desk - the desktop's own ground over the column x = AX, rows BX..DX,
; cut to the damage. Keeps AX, BX and DX; spends CX
stik_desk:
    mov cx, ax
    push ax
    push bx
    push dx
    call stik_cut
    jc .x
    call OSAPI_GFX_DESK
.x:
    pop dx
    pop bx
    pop ax
    ret

%ifndef STIK_DESKONLY
; stik_tgap - the titled fallback's gap (AX..DX = it, already cut to the
; damage - stik_rowc's - DI = its top, [stik_gh] its rows): white with a 1 px
; rule through its middle, composed and put down in ONE BLIT1 - never a fill
; and a line over it. SI and DI kept; spends AX..DX, BP and ES
stik_tgap:
    push si
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
    STIK_MOVFW cx
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
    STIK_MOVFW cx
    call stik_blit
    pop si
    ret

%endif
; stik_blit - the band onto the glass: AX = x, BX = y, CX = width, DX = rows,
; SI = the band row to start from. One OSAPI_GFX_BLIT1; every register kept
stik_blit:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    push ax
    push dx
%ifdef STIK_LIBRARY
    mov ax, [stik_d_bst]
    mul si
    add ax, [stik_d_buf]
%else
    mov ax, GFXE_BAND_ST
    mul si
    add ax, GFXE_BAND_BUF
%endif
    mov si, ax
    pop dx
    pop ax
    push ds
    pop es
%ifdef STIK_LIBRARY
    mov bp, [stik_d_bst]
%else
    mov bp, GFXE_BAND_ST
%endif
    call OSAPI_GFX_BLIT1
    jmp stik_pop_8

; -----------------------------------------------------------------------------
; stik_slabs - a STIK_PAINTCARD that composes: AL = the card, BX..DX = its
; damaged rows (card-local); [stik_fx], [stik_cy], [stik_ch] as stik_paint left
; them. For each GFXE_BAND_H-row slab those rows touch: the band papered
; white, STIK_COMPOSE (AL = the card, BX = the slab's first card row, CX = its
; rows - compose at y = card row - BX), and the damaged rows of it blitted:
; ONE OSAPI_GFX_BLIT1 a slab. Every register kept
; -----------------------------------------------------------------------------
stik_slabs:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    mov [stik_sc], al
    mov [stik_sr0], bx
    mov [stik_sr1], dx
%ifdef STIK_LIBRARY
  %define STIK_BH_ [stik_d_brows]
    mov ax, bx                  ; BP = the slab's first card row
    xor dx, dx
    div word STIK_BH_
    mul word STIK_BH_
    mov bp, ax
%else
  %define STIK_BH_ GFXE_BAND_H
%if (GFXE_BAND_H & (GFXE_BAND_H - 1)) == 0
    mov bp, bx                  ; BP = the slab's first card row: the first
    and bp, -GFXE_BAND_H        ; slab touched (BX >= 0)
%else
    mov ax, bx
    xor dx, dx
    mov cx, GFXE_BAND_H
    div cx
    mul cx
    mov bp, ax
%endif
%endif
.slab:
    mov cx, [stik_ch]
    sub cx, bp
    cmp cx, STIK_BH_
    jbe .n
    mov cx, STIK_BH_
.n:
    mov [gfxe_bh], cx
    mov ax, 0FFFFh
    call gfxe_bclear            ; white paper: a card is never themed
%ifdef STIK_LIBRARY
  %define STIK_COMPOSE_
%elifdef STIK_COMPOSE
  %define STIK_COMPOSE_
%endif
%ifdef STIK_COMPOSE_
    mov al, [stik_sc]
    mov bx, bp
    mov cx, [gfxe_bh]
    push bp
%ifdef STIK_LIBRARY
    STIK_LCB compose
%else
    call STIK_COMPOSE
%endif
    pop bp
%endif
    mov bx, [stik_sr0]           ; the rows of this slab that are damaged
    cmp bx, bp
    jge .b0
    mov bx, bp
.b0:
    mov dx, bp
    add dx, [gfxe_bh]
    dec dx
    cmp dx, [stik_sr1]
    jle .b1
    mov dx, [stik_sr1]
.b1:
    mov si, bx
    sub si, bp
    sub dx, bx
    inc dx
    add bx, [stik_cy]
    mov ax, [stik_fx]
    STIK_MOVFW cx
    call stik_blit
    add bp, STIK_BH_
    cmp bp, [stik_sr1]
    jle .slab
    jmp stik_pop_8

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

; stik_tagname - STIK_TAG into the band at x = CX, centred down the tag row,
; in the style [gfxe_tsty]. gfxe_text's spend
stik_tagname:
%ifdef STIK_LIBRARY
    mov si, [stik_d_tag]
%else
    mov si, stik_tags
%endif
    mov dx, [gfxe_bh]
    sub dx, 8
    shr dx, 1
    jmp gfxe_text

; -----------------------------------------------------------------------------
; stik_tagdraw - the tag row as the state asks ([stik_fx]/[stik_fy] the face,
; lock held, clipped): REST and FOCUS are ground | plate | ground - two
; OSAPI_GFX_DESK and one BLIT1, or the BLIT1 ALONE when [stik_tagp] says the
; glass holds the other plate (a focus edge: stik_paint sets STIK_P_NONE first)
; - and the handle ONE face-wide BLIT1. Spends AX..DI, BP and ES. ITS ONE RET
; IS ITS LAST BYTE, and stik_tagrows follows it: tests/desklayer.py counts the
; calls between this entry and that ret
; -----------------------------------------------------------------------------
stik_tagdraw:
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
%ifdef STIK_VARFACE
    cmp byte [stik_np], 0       ; NO PLATE (face-only, or a face narrower
    jne .bare                   ; than the plate): the row is the desktop
%ifdef STIK_LIBRARY
    mov cx, [stik_d_tagw]
    add cx, 8                   ; the plate: the name and 4 px each side
    cmp [stik_fw], cx
%else
    cmp word [stik_fw], STIK_PW
%endif
    jae .plate
.bare:
    test byte [stik_wf], STIK_W_PLATE
    jnz .out                    ; rest <-> focus: nothing to see
    xor ax, ax
    mov cx, [stik_fw]
    dec cx
    call stik_tagrows
    jmp .out
.plate:
%endif
    push ax                     ; --- the PLATE: white, or inverse focused -
    cbw                         ; REST 0 / FOCUS 1, so the paper is AX - 1
    dec ax                      ; and the name's style is AL itself
    call gfxe_bclear
    pop ax
    mov [gfxe_tsty], al
    mov cx, 4
    call stik_tagname
%ifdef STIK_VARFACE
    call stik_vpx               ; AX = the plate's x in the face
    or ax, ax
    jz .pl
    test byte [stik_wf], STIK_W_PLATE
    jnz .pl
    mov cx, ax                  ; the ground left of it...
    dec cx
    xor ax, ax
    call stik_tagrows
.pl:
    call stik_vpx
    add ax, [stik_fx]            ; ...the plate...
%else
%if STIK_PX > 0
    test byte [stik_wf], STIK_W_PLATE
    jnz .pl
    xor ax, ax                  ; the ground left of it...
    mov cx, STIK_PX - 1
    call stik_tagrows
.pl:
%endif
    mov ax, [stik_fx]            ; ...the plate...
    add ax, STIK_PX
%endif
%ifdef STIK_LIBRARY
    mov cx, [stik_d_tagw]
    add cx, 8
%else
    mov cx, STIK_PW
%endif
    jmp short .put
.hand:
    xor ax, ax                  ; --- the HANDLE: a black bar
    call gfxe_bclear
    mov byte [gfxe_tsty], GFXE_INV  ; the name in white, centred...
%ifdef STIK_VARFACE
    mov cx, [stik_fw]           ; ...on a face wide enough for it beside the
%ifdef STIK_LIBRARY
    sub cx, [stik_d_tagw]       ; x and the grip: a narrower one is a handle
    cmp cx, 2 * (STIK_XX + STIK_XW + 1)  ; of the x and the grip alone
    jl .nn
%else
    cmp cx, STIK_NAMEMIN        ; x and the grip: a narrower one is a handle
    jb .nn                      ; of the x and the grip alone
    sub cx, STIK_TAGW
%endif
    shr cx, 1
    call stik_tagname
.nn:
    mov si, stik_grip           ; ...the grip...
    mov cx, [stik_fw]
    sub cx, 18
%else
    mov cx, STIK_NX
    call stik_tagname
    mov si, stik_grip           ; ...the grip...
    mov cx, STIK_GX
%endif
    mov dx, [gfxe_bh]
    sub dx, 7
    shr dx, 1
    call gfxe_bits
    cmp byte [stik_tagp], STIK_P_ARMED  ; (none of the three overlaps the
    je .xm                      ; box, so the band is the same in any order)
    mov ax, STIK_XX             ; ...the close box white...
    call stik_boxy
    mov dx, cx
    call gfxe_bfill
    mov byte [gfxe_tsty], 0     ; ...with a black x - armed, the box black
.xm:                            ; and its x white
    call stik_boxy
    sub cx, 6
    shr cx, 1                   ; the x's inset: centred in the box
    mov dx, bx
    add dx, cx
    add cx, STIK_XX
    mov si, stik_xbits
    call gfxe_bits
    mov ax, [stik_fx]
    STIK_MOVFW cx
.put:
    mov bx, [stik_ty]
    mov dx, [gfxe_bh]
    xor si, si
    call stik_blit
    cmp byte [stik_tagp], STIK_P_HAND
    jae .out
    test byte [stik_wf], STIK_W_PLATE
    jnz .out
%ifdef STIK_VARFACE
    call stik_vpx               ; ...and the ground right of it
%ifdef STIK_LIBRARY
    add ax, [stik_d_tagw]
    add ax, 8
%else
    add ax, STIK_PW
%endif
    mov cx, [stik_fw]
    cmp ax, cx
    jae .out
    dec cx
    call stik_tagrows
%else
%if STIK_PX + STIK_PW < STIK_FACE_W
    mov ax, STIK_PX + STIK_PW    ; ...and the ground right of it
    mov cx, STIK_FACE_W - 1
    call stik_tagrows
%endif
%endif
.out:
    ret

; stik_tagrows - AX..CX = face columns: the desktop's ground there, over the
; tag row - one OSAPI_GFX_DESK. It FOLLOWS stik_tagdraw's ret, which
; tests/desklayer.py finds as this address less one
stik_tagrows:
    mov bx, [stik_fx]
    add ax, bx
    add cx, bx
    mov bx, [stik_ty]
    mov dx, bx
    add dx, [gfxe_bh]
    dec dx
    call OSAPI_GFX_DESK
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

%ifdef STIK_VARFACE
; stik_colh - STIK_COLH as a routine, over [stik_lay] (SI out), and CX = the
; frame's width: stik_load's sizing and stik_resize's. AX, BX spent
stik_colh:
    mov si, [stik_lay]
    STIK_COLH
    mov cx, [stik_fw]
    inc cx
    inc cx
    ret

; stik_vpx - AX = the plate's x in a face of [stik_fw] (no narrower than the
; plate: the caller has asked). Nothing else changes
stik_vpx:
    mov ax, [stik_fw]
%ifdef STIK_LIBRARY
    sub ax, [stik_d_tagw]
    sub ax, 8
%else
    sub ax, STIK_PW
%endif
    shr ax, 1
    ret

; -----------------------------------------------------------------------------
; stik_resize - the package's answer to STIK_SIZEUP has changed (a setting:
; SPEC.md 110.14.1). SI = the window, lock held. The layout and [stik_fw] are
; asked again; a frame of another size is HIDDEN, re-sized from its top-left
; and SHOWN - there is no move cell, and the kernel's Arrange does the same -
; kept on its display, and a titled fallback goes through OSAPI_WM_RESIZE and
; is drawn again whole (a pure shrink is no damage there). The same size is
; one stik_redraw. Every register kept
; -----------------------------------------------------------------------------
stik_resize:
    call stik_push              ; AX..DI, BP and ES banked (stik_pop_8)
    mov ax, KERNEL_SEG
    mov es, ax
    mov di, si                  ; DI = the window
%ifdef STIK_LIBRARY
    STIK_LCB sizeup
%else
    call STIK_SIZEUP
%endif
    call stik_colh              ; CX, DX = the frame
    mov byte [stik_tagp], STIK_P_NONE
    mov si, di
    cmp cx, [es:si+W_W]
    jne .chg
    cmp dx, [es:si+W_H]
    jne .chg
    call stik_redraw            ; the same frame: the face again
    jmp .x
.chg:
    mov bx, si
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jnz .desk
    call OSAPI_WM_RESIZE        ; the titled fallback
    call stik_redraw            ; ...and a shrink is no damage: draw it
    jmp .x
.desk:
%endif
    mov byte [stik_hv], STIK_HV_HIDDEN
    mov al, [es:si+W_FLAGS]
    push ax                     ; AL bit 1: it was on the glass
    test al, 2
    jz .poke
    call OSAPI_WM_HIDE
.poke:
    mov [es:si+W_W], cx
    mov [es:si+W_H], dx
    push cx
    push dx
    call OSAPI_WM_DISPLAY       ; AX = its width, BX = height (CX DX SI)
    pop dx
    pop cx
    mov si, di
    push bx
    mov bx, [es:si+W_X]          ; the RIGHT edge on the display, the face
    add bx, cx                  ; kept on the byte grid (W_X = 7 mod 8)
    cmp bx, ax
    jle .xok
    sub ax, cx
    and ax, 0FFF8h
    dec ax
    jns .xs
    mov ax, 7
.xs:
    mov [es:si+W_X], ax
.xok:
    pop bx                      ; ...and the BOTTOM, at the bar at worst
    mov ax, [es:si+W_Y]
    add ax, dx
    cmp ax, bx
    jle .yok
    sub bx, dx
    cmp bx, MBAR_H
    jge .ys
    mov bx, MBAR_H
.ys:
    mov [es:si+W_Y], bx
.yok:
    pop ax
    test al, 2
    jz .x
    mov bx, si
    call OSAPI_WM_SHOW          ; the new frame painted, by the kernel
    test byte [stik_wf], STIK_W_FOCUS
    jz .x                       ; it held the bar, and the hide gave it to
    call OSAPI_WM_FRONT         ; the front window: a desk window's FRONT
.x:                             ; takes it back, raising nothing (11.103.4)
    jmp stik_pop_8
%endif

; stik_tagsync - the tag to what the state asks, drawn only on a change of the
; picture, under OSAPI_WM_CLIP_SET (lock held). Spends AX..DI, BP and ES
stik_tagsync:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .r
%endif
    call stik_want
    cmp al, [stik_tagp]
    je .r
    call stik_kwin
    test byte [es:si+W_FLAGS], 2
    jz .r                       ; not on the glass: the next paint has it
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .r                       ; none of it showing
    call stik_forg              ; ...as stik_paint: the table may have moved
    jmp stik_tagdraw
.r:
    ret

; stik_hitp - stik_org (SI = the window, ES = KERNEL_SEG), then stik_hit
stik_hitp:
    push dx
    call stik_org
    pop dx
; stik_hit - CX, DX = a screen point, [stik_fx]/[stik_fy] the face. CF = 0 a
; card: AL = it (AH = 0), CX, DX card-relative. CF = 1 the frame - a ground
; column, a gap or the tag row - and AH = 1 when the point is on the handle's
; x. SI kept; spends BX and DI
stik_hit:
    sub cx, [stik_fx]
    sub dx, [stik_fy]
    mov di, [stik_lay]
    lea bx, [di+STIKL_CARDS]
    xor ax, ax
%ifdef STIK_VARFACE
    cmp cx, [stik_fw]
%else
    cmp cx, STIK_FACE_W
%endif
    jae .no                     ; a ground column (unsigned: -1 too)
.c:
    cmp al, [di+STIKL_N]
    jae .tag
    sub dl, [bx]                ; DX - the card's height: a 16-bit SUB in two
    sbb dh, 0                   ; halves, whose SF and OF are the word's
    jl .card
    sub dl, [bx+1]
    sbb dh, 0
    jl .no                      ; a gap
    inc bx
    inc bx
    inc ax
    jmp short .c
.tag:                           ; DX = the row in the tag row; AX = the count
    cmp cx, STIK_XX
    jl .no
    cmp cx, STIK_XX + STIK_XW
    jge .no
    mov al, [di+STIKL_TAG]
    sub ax, STIK_XW
    shr ax, 1
    cmp dx, ax
    jl .no
    add ax, STIK_XW
    cmp dx, ax
    jge .no
    mov ax, 0100h
    stc
    ret
.no:
    xor ax, ax
    stc
    ret
.card:
    add dl, [bx]                ; the card's row back
    adc dh, 0
    clc
    ret

; stik_ctop - AL = a card: BX = its top, face-relative. Keeps the rest
stik_ctop:
    push ax
    push si
    mov si, [stik_lay]
    xor bx, bx
    jmp short .t
.l:
    add bl, [si+STIKL_CARDS]     ; the card's height...
    adc bh, 0
    add bl, [si+STIKL_CARDS+1]   ; ...and the gap under it
    adc bh, 0
    inc si
    inc si
.t:
    dec al
    jns .l
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; stik_click - W_ONCLICK (SI = the window, CX, DX the press, lock held). A card
; is the package's (STIK_ONCARD); on a desk window anything else is the
; frame's: the x - once the handle was SHOWING before this press - arms, and
; the rest posts OSAPI_WM_DESK DRAG (SPEC.md 11.103.6). A press reveals the
; handle at once, and hands the widget the bar (ui.inc's menu_activate did).
; SI given back (SPEC.md 13.7); AX..DI, BP and ES spent
; -----------------------------------------------------------------------------
stik_click:
    push si
    mov al, [stik_hv]
    mov [stik_pre], al
    call stik_hitp
    jc .frame
    inc ax
    mov [stik_press], al
    dec ax
%ifdef STIK_LIBRARY
    STIK_LCB oncard             ; SI = the window: W_ONCLICK's environment
%elifdef STIK_ONCARD
    call STIK_ONCARD            ; SI = the window: W_ONCLICK's environment
%endif
    jmp short .after
.frame:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .focus                   ; the titled frame is the kernel's
%endif
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
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .focus
%endif
    mov byte [stik_hv], STIK_HV_SHOWN ; a press reveals at once
.focus:
    call stik_focus
    call stik_tagsync
    call stik_arm
    pop si
    ret

; stik_mup - W_ONMOUSEUP (SI = the window, CX, DX the release, lock held): an
; armed x acts only if the release is ON it - a slide off cancels (SPEC.md
; 13.5) - and a card's release is STIK_ONCARDUP's, card-relative. A window
; callback: AX..DI, BP and ES spent
stik_mup:
    test byte [stik_wf], STIK_W_ARMED
    jz .card
    and byte [stik_wf], ~STIK_W_ARMED & 0FFh
    call stik_hitp
    jnc .stay
    or ah, ah
    jz .stay
    call stik_quit              ; CF = 0: going - nothing drawn after this
    jnc .out
.stay:
    jmp stik_tagsync            ; the x unlit, or the plate if it was left
.card:
    mov al, [stik_press]
    or al, al
    jz .out
    mov byte [stik_press], 0
%ifdef STIK_LIBRARY
  %define STIK_ONCARDUP_
%elifdef STIK_ONCARDUP
  %define STIK_ONCARDUP_
%endif
%ifdef STIK_ONCARDUP_
    push dx
    push ax
    call stik_org               ; AX, DX = the face (CX kept)
    sub cx, ax                  ; card-relative x
    pop ax
    dec ax                      ; AL = the card
    call stik_ctop              ; BX = its top in the face
    add bx, dx
    pop dx
    sub dx, bx                  ; card-relative y
%ifdef STIK_LIBRARY
    STIK_LCB oncardup           ; SI = the window, as W_ONMOUSEUP's
%else
    call STIK_ONCARDUP          ; SI = the window, as W_ONMOUSEUP's
%endif
%endif
.out:
    ret

; stik_quit - the widget's close (SI = the window, lock held): stik_close's
; negotiator, then OSAPI_WM_CLOSE. CF = 0 the close is under way - draw
; nothing more and return; CF = 1 it was declined (the disk, SPEC.md 110.5).
; Spends AX..DI and ES, as stik_close does
stik_quit:
    call stik_close
    jc .r
    or byte [stik_fl], STIK_F_QUIT   ; STIK_ONCLOSE has run: no beat after it,
    mov bx, [stik_win]              ; and no tag drawn (stik_tick, stik_wake)
    call OSAPI_WM_CLOSE
    clc
.r:
    ret

; stik_hvtick - a dwell or a linger that has run out (lock held). Spends AX..DX,
; SI and ES
stik_hvtick:
    test byte [stik_hv], 1      ; DWELL or LINGER: a deadline is running
    jz .x
    call stik_hdue
    jc .x
    call stik_kwin
    cmp byte [stik_hv], STIK_HV_DWELL
    je .dw
    test byte [stik_wf], STIK_W_ARMED   ; the LINGER's: the x is held - its
    mov ax, STIK_LEAVE_T                ; release decides
    jnz .set
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
.show:
    mov byte [stik_hv], STIK_HV_SHOWN
.x:
    ret
.dw:
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .hide
    call OSAPI_MOUSE            ; AL = the buttons
    test al, 3
    jz .show
    mov ax, STIK_HOVER_T        ; a button held: never revealed (SPEC.md
.set:                           ; 110.11) - ask again a dwell later
    jmp stik_hset
.hide:
    mov byte [stik_hv], STIK_HV_HIDDEN
    ret

; stik_hdue - CF = 1 the dwell or linger is still ahead. AX spent
stik_hdue:
    call OSAPI_GET_TICKS
    sub ax, [stik_thov]
    rol ax, 1                   ; CF = the sign: not yet
    ret

; stik_hset - AX = ticks from now: the dwell's or linger's deadline. AX, BX
; spent
stik_hset:
    mov bx, ax
    call OSAPI_GET_TICKS
    add ax, bx
    mov [stik_thov], ax
    ret

; stik_focus - does the widget own the menu bar (OSAPI_MENU_OWNER, an inline
; cell)? On a change, STIK_ONFOCUS (AL = 1 gained, 0 lost; SI = the window;
; lock held). A desk window's only: the titled fallback is an ordinary
; window. Spends AX, BX and SI
stik_focus:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .r
%endif
    call OSAPI_MENU_OWNER       ; BX = the owner
    xor ax, ax
    cmp bx, [stik_win]
    jne .n
    mov al, STIK_W_FOCUS
.n:
    mov ah, [stik_wf]
    and ah, STIK_W_FOCUS
    cmp al, ah
    je .r                       ; no change
    xor byte [stik_wf], STIK_W_FOCUS
%ifdef STIK_LIBRARY
    shr al, 1                   ; 1 gained, 0 lost
    mov si, [stik_win]
    STIK_LCB onfocus
%elifdef STIK_ONFOCUS
    shr al, 1                   ; 1 gained, 0 lost
    mov si, [stik_win]
    call STIK_ONFOCUS
%endif
.r:
    ret

; -----------------------------------------------------------------------------
; stik_wake - W_ONWAKE (SI = the window, the UI task, NO lock). The worker arm's
; kick is v1's beat; otherwise it is the hover edge (SPEC.md 11.103.7), and
; LEVEL-triggered: WF_HOVER is read, the state moves, a deadline is armed -
; and nothing is drawn, there being no lock. A window callback: spends AX..DI
; and ES
; -----------------------------------------------------------------------------
stik_wake:
    test byte [stik_fl], STIK_F_QUIT
    jnz .x                      ; closing: nothing more
%ifndef STIK_DESKONLY
    test byte [stik_st], STIK_S_WORKER
    jz .hov
    call OSAPI_GFX_LOCK
    call stik_beat
    call OSAPI_GFX_UNLOCK
    ret
.hov:
%endif
    call stik_kwin              ; (SI = the window already: it is ours)
    mov al, [stik_hv]            ; EVERY move is the state XOR 1 - HIDDEN to
    inc ax                      ; DWELL and LINGER to SHOWN under the
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8 ; pointer, DWELL to HIDDEN and
    jz .o                       ; SHOWN to LINGER off it - and it is made
    add al, 2                   ; when bit 1 of (state + 1), or under the
.o:                             ; pointer of (state + 3), is set
    test al, 2
    jz .x
    xor byte [stik_hv], 1
    mov al, [stik_hv]
    shr al, 1                   ; CF = 1: DWELL or LINGER, which run a
    jnc .x                      ; deadline - ZF = 1 the DWELL's
    mov ax, STIK_HOVER_T
    jz .dl
    mov ax, STIK_LEAVE_T
.dl:
    call stik_hset
    jmp stik_arm
.x:
    ret

; stik_tick - the timer (SI = the window, lock held), which serves the beat,
; the dwell or linger and the focus poll at once: each keeps an ABSOLUTE
; deadline and stik_arm takes the soonest, so a hover deadline never delays a
; beat and a beat that comes early does nothing early. A window callback:
; spends AX..DI, BP and ES
stik_tick:
    test byte [stik_fl], STIK_F_QUIT
    jnz stik_wake.x             ; closing: no beat after STIK_ONCLOSE, no tag
    call OSAPI_GET_TICKS
    cmp ax, [stik_tbeat]
    js .nb                      ; the beat is not due (now - beat < 0)
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
    call stik_tagsync           ; ...and falls into stik_arm

; stik_arm - the one timer, at the soonest deadline: the beat's, the dwell's
; or linger's, and - only while the widget owns the bar - the focus poll's
; STIK_FOCUS_T. Spends AX, BX and CX
stik_arm:
    call OSAPI_GET_TICKS
    mov cx, ax
    mov ax, [stik_tbeat]
    sub ax, cx
    test byte [stik_hv], 1      ; DWELL or LINGER: its deadline too
    jz .b
    mov bx, [stik_thov]
    sub bx, cx
    cmp bx, ax
    jge .b
    mov ax, bx
.b:
    cmp ax, 1                   ; at least a tick: min(max(a, 1), max(b, 1))
    jge .f                      ; is max(min(a, b), 1)
    mov ax, 1
.f:
    test byte [stik_wf], STIK_W_FOCUS
    jz .set
    cmp ax, STIK_FOCUS_T
    jle .set
    mov ax, STIK_FOCUS_T
.set:
    mov bx, [stik_win]
    call OSAPI_WM_TIMER         ; CF = 1 on kern_small: there is no timer, and
    ret                         ; the worker's beat is the whole of it

%ifndef STIK_LIBRARY
; the name, and the handle's two bit records (GFXE_BITS: db rows, then words)
stik_tags   db STIK_TAG, 0
stik_xbits  db 6                ; the x, 6 x 6, two in from the box's corner
            dw 08400h, 04800h, 03000h, 03000h, 04800h, 08400h
stik_grip   db 7                ; the grip: three dotted lines, 13 wide
            dw 0AAA8h, 0, 0, 0AAA8h, 0, 0, 0AAA8h
%endif
%endif ; STIK_WIDGET

%ifdef STIK_LIBRARY
; -----------------------------------------------------------------------------
; stik_cb - the library's call INTO the package (SPEC.md 110.15): `push word
; [stik_d_x]` / `call stik_cb`. The word is a near proc in the package, or 0
; for none (CF = 0 back, nothing called). The package's trampoline
; (os88stikc.inc's stik_xcb) is far-called at DS - the package's segment NOW,
; never a stored one, its region being movable (SPEC.md 66.6.1.1) - and it
; `call`s [stik_xt], which it reads before anything else can write it. Every
; register and the flags are the callee's on the way back.
;
; DS is the package's DATA segment, and on kern_mega a far call through it is
; #GP that mega_gpfix finishes - one exception a callback, ~2 a second for
; each open Sticky App (KERN-MEGA-PLAN 14.46). So the segment the call goes
; to is OSAPI_MEM_CODESEL's answer for DS, asked NOW for the reason above:
; DS itself on every real-mode kernel, the code alias (or a pair's code half
; above 1MB) under protected mode. Refused - which a package's region never
; is - it is DS as before, and the fixup finishes it
; -----------------------------------------------------------------------------
stik_cb:
    push bp
    mov bp, sp
    push ax
    mov ax, [bp+4]              ; the proc
    or ax, ax
    jz .none
    mov [stik_xt], ax
    push dx
    mov dx, ds
    call OSAPI_MEM_CODESEL      ; AX = the selector DS's code is called at
    jnc .cs
    mov ax, dx
.cs:
    mov [stik_xcp+2], ax
    pop dx
    pop ax
    pop bp
    call far [stik_xcp]
    ret 2
.none:
    pop ax
    pop bp
    clc
    ret 2
%else
stik_dname  db STIK_DATNAME, 0
stik_dsys   db 'SYSTEM', 0
stik_dapp   db 'APPDATA', 0
%ifdef STIK_ISOPEN
stik_isopen db STIK_ISOPEN, 0
%else
stik_isopen db 'Already open', 0
%endif
%ifdef STIK_DESKONLY
stik_nodesk db 'No desk layer (DESK.DRV)', 0
%endif
%strlen STIK_CAPLEN STIK_CAP
%if STIK_CAPLEN > 12             ; the row's caption is 13 bytes, its NUL in
  %error "os88stik: STIK_CAP is at most 12 characters"  ; them: +62 and +63 are
%endif                          ; the kernel's picture header (SPEC.md 26.8.1)
stik_cap    db STIK_CAP, 0
            times OSAPI_DI_BODY - OSAPI_DI_CAP - ($ - stik_cap) db 0
%endif ; STIK_LIBRARY

%endif ; STIK_LIB
%endif ; OS88STIK_INC
