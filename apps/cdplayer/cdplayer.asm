; =============================================================================
; os8088 - apps/cdplayer/cdplayer.asm
;
; CD PLAYER (SPEC.md 119.6): an audio CD in an ATAPI drive, played through the
; drive's own audio output. A display - the track, the time into it, the
; track's length, and one line of state - over six buttons: previous, play,
; pause, stop, next, eject.
;
; EVERYTHING GOES THROUGH CDROM.DRV'S DOOR (drivers/cdrom/cdpkg.inc,
; SPEC.md 119.4): OSAPI_DRV_CALL with BH = DRVC_CD. The player owns no port
; and keeps no copy of the disc: the drive plays, and READ SUB-CHANNEL - asked
; every CP_POLL ticks by the window's one-shot timer - says where it is. The
; media generation in that answer is how a changed disc is noticed: a moved
; generation re-reads the TOC.
;
; ONLY WHAT CHANGED IS DRAWN (PERFORMANCE.md rule 1): each display line is a
; fixed-width string, and a pass compares it with what the window is showing
; and letters it only when it differs - one opaque run (SPEC.md 6.1), white
; on the display's black, at a content-relative x that is a multiple of 8 in
; a window that asks for WF_SNAP.
;
; THE BUTTONS ARE THE STANDARD CONTROL (apps/os88ui.inc, SPEC.md 20.5.1.3),
; greyed by ONE predicate, cp_bok, read by the painter and the release alike
; (SPEC.md 47 rule 2).
;
; OPENED FROM A TRACKnn.CDA (SPEC.md 119.3.5) it plays that track: the file's
; track word is read at the first poll, on the UI task.
;
; REGISTER CONTRACTS are the callbacks' (SPEC.md 111.6's statement of them):
; W_PAINT and W_ONKEY give SI back; W_ONTIMER and the W_ONCLICK family may
; clobber AX, BX, CX, DX, DI and ES. Internal procs say what they clobber.
; =============================================================================

%include "os88api.inc"
%include "cdpkg.inc"

    OS88_HEADER 'CD Player', cp_entry, 3    ; flags: bit 0 the icon, bit 1
                                            ; the association block
    OS88_ICON16                     ; a disc: the hole, and a glint
    ; --- mask ---
    dw 0x07E0, 0x1FF8, 0x3FFC, 0x7FFE, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE, 0x7FFE, 0x3FFC, 0x1FF8, 0x07E0
    ; --- data ---
    dw 0x07E0, 0x1818, 0x2004, 0x4602, 0x4802, 0x9001, 0x8181, 0x8241
    dw 0x8241, 0x8181, 0x8009, 0x4012, 0x4062, 0x2004, 0x1818, 0x07E0
    OS88_ICON16_END

    OS88_ASSOC16                    ; SPEC.md 119.3.5: an audio track's file
    db 1
    OS88_ASSOC_EXT 'CDA'
    OS88_ASSOC16_END

; --- geometry ------------------------------------------------------------------
; Fixed-size and never resizable, and 288x74 is inside every adapter's desktop
; band (CGA's is the tightest at 640x157), so the content box is CP_CW x CP_CH
; on all three.
CP_W        equ 288                 ; frame width / height
CP_H        equ 74
CP_CW       equ CP_W - 2            ; content: 286 x 55
CP_CH       equ CP_H - TITLE_H - 1
CP_PX       equ 8                   ; the display: a black panel...
CP_PY       equ 4
CP_PW       equ CP_CW - 2 * CP_PX
CP_PH       equ 24
CP_TX       equ 16                  ; ...its two lines, on the 8-pixel grid
CP_TY1      equ CP_PY + 4
CP_TY2      equ CP_PY + 13
CP_LEN      equ 32                  ; characters a line: 256 px, inside the panel
%if CP_TX + CP_LEN * 8 > CP_PX + CP_PW
%error "the CD Player's display lines run past its panel"
%endif
CP_NB       equ 6                   ; the buttons: count, size and pitch
CP_BW       equ 42
CP_BH       equ 15
CP_BGAP     equ 4
CP_BY       equ CP_PY + CP_PH + 6
CP_BX       equ (CP_CW - CP_NB * CP_BW - (CP_NB - 1) * CP_BGAP) / 2
%if CP_BY + CP_BH > CP_CH
%error "the CD Player's buttons fall off its content"
%endif

CP_POLL     equ 9                   ; ticks between status polls: under half a
                                    ; second, so a displayed second is at most
                                    ; one poll late
CP_BACK     equ 3                   ; seconds into a track past which Previous
                                    ; restarts it rather than going back one

CB_PREV     equ 0                   ; button indices, in drawn order
CB_PLAY     equ 1
CB_PAUSE    equ 2
CB_STOP     equ 3
CB_NEXT     equ 4
CB_EJECT    equ 5

; --- what the player knows: cp_state ------------------------------------------
CPS_NODRV   equ 0                   ; no CD-ROM driver answers the door
CPS_NODISC  equ 1                   ; the drive is empty, or its tray is open
CPS_DATA    equ 2                   ; a disc with no audio track
CPS_AUDIO   equ 3                   ; an audio disc: [cp_play] says what it does
CPS_NOAUD   equ 4                   ; the drive has no audio commands (QEMU's)
CPS_ERR     equ 5                   ; the drive did not answer

; =============================================================================
; ENTRY
; =============================================================================

; -----------------------------------------------------------------------------
; cp_entry - the package entry point (SPEC.md 20.2)
; out: BX = the window, CF clear (set = abort)
; The loader shows the window; nothing here may draw. The first poll is armed
; for the next tick, so the display is filled as soon as the window is up.
; -----------------------------------------------------------------------------
cp_entry:
    call OSAPI_ARG_FILE             ; a TRACKnn.CDA? CF = 1: launched empty
    jc .plain
    mov di, cp_argname              ; SI -> its name in KERNEL_SEG: copied out
    mov cx, 13                      ; before anything else is called
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    mov [cp_argdir], dx
    mov [cp_argvol], bl
    mov byte [cp_argwant], 1
.plain:
    mov si, cp_tpl
    call OSAPI_WM_CREATE            ; BX = the window
    jc cp_ret
    OS88_REGION_MOVABLE_NOKEEP cp_ret ; every byte that outlives a pass is a
                                    ; static and moves with us
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on the 8-pixel grid
    mov si, cp_about
    call OSAPI_ABOUT_SET
    mov ax, cp_tick
    call OSAPI_WM_ONTIMER
    mov ax, 1
    call OSAPI_WM_TIMER             ; the first poll, next tick (CF = 1 on
                                    ; kern_small, which has no driver either:
                                    ; the display then says so at the paint)
    xchg ax, bx                     ; AX = the window
    mov bx, cp_btrec
    mov si, cp_onup
    mov di, cp_ondrag
    mov dx, cp_onclick
    call os88ui_btninit             ; keeps every register
    xchg ax, bx
    clc
cp_ret:
    ret

; =============================================================================
; THE DOOR
; =============================================================================

; cp_call - OSAPI_DRV_CALL with BH = DRVC_CD and BL = the verb in CP_VERB's
; argument. A macro and not a proc: BX is the door's, AX/CX/DX/SI the verb's.
%macro CP_CALL 1
    mov bx, DRVC_CD * 256 + %1
    call OSAPI_DRV_CALL
%endmacro

; -----------------------------------------------------------------------------
; cp_disc - ask the drive what is in it, and set the state from the answer
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cp_disc:
    mov byte [cp_state], CPS_NODRV
    CP_CALL CDV_IDENT
    jc .out
    cmp ax, CD_SIG
    jne .out                        ; somebody else in the slot (SPEC.md 20.11.1)
    CP_CALL CDV_DISC
    jc .refused
    mov [cp_first], al
    mov [cp_last], ah
    mov [cp_dlen], cx
    mov [cp_gen], dh
    mov byte [cp_state], CPS_DATA
    test dl, CDK_AUDIO
    jz .out
    mov byte [cp_state], CPS_AUDIO
    mov al, [cp_sel]                ; keep the selection if this disc has it
    cmp al, [cp_first]
    jb .first
    cmp al, [cp_last]
    jbe .tlen
.first:
    mov al, [cp_first]
    call cp_audio_from              ; ...else the first AUDIO track
    mov [cp_sel], al
.tlen:
    call cp_seltlen
.out:
    ret
.refused:
    mov byte [cp_state], CPS_NODISC
    cmp ax, CDE_NODISC
    je .out
    mov byte [cp_state], CPS_ERR
    ret

; cp_seltlen - [cp_tlen] = the selected track's length in seconds. clobbers AX-DX, SI
cp_seltlen:
    mov al, [cp_sel]
    CP_CALL CDV_TRACK
    jnc .got
    xor cx, cx
.got:
    mov [cp_tlen], cx
    mov [cp_tctl], si
    ret

; -----------------------------------------------------------------------------
; cp_audio_from - AL = a track -> AL = it, or the next audio track after it,
;                 or AL past [cp_last] when there is none. clobbers BX, CX,
;                 DX, SI
; -----------------------------------------------------------------------------
cp_audio_from:
    cmp al, [cp_last]
    ja .out                         ; past the disc: none
    push ax
    CP_CALL CDV_TRACK               ; SI = the control nibble
    pop ax
    jc .next
    test si, CDT_DATA
    jz .out
.next:
    inc al
    jmp short cp_audio_from
.out:
    ret

; cp_audio_back - the same, downwards: AL below [cp_first] when there is none.
; The first track is at least 1, so a step down from it stops at the test
; and never wraps through 0
cp_audio_back:
    cmp al, [cp_first]
    jb .out
    push ax
    CP_CALL CDV_TRACK
    pop ax
    jc .prev
    test si, CDT_DATA
    jz .out
.prev:
    dec al
    jmp short cp_audio_back
.out:
    ret

; -----------------------------------------------------------------------------
; cp_poll - where the drive is: the play state, the track and the time
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; A generation that moved is a different disc (SPEC.md 119.3.4): the TOC is
; read again. A refusal is the empty drive, or the driver gone.
; -----------------------------------------------------------------------------
cp_poll:
    cmp byte [cp_state], CPS_AUDIO
    je .ask
    cmp byte [cp_state], CPS_NOAUD
    je .out                         ; nothing to ask a drive with no audio
    jmp cp_disc                     ; empty, data or gone: ask again
.ask:
    CP_CALL CDV_STATUS
    jc .refused
    mov bx, si
    cmp bl, [cp_gen]
    jne .changed
    mov [cp_play], al
    cmp al, CDS_PLAYING
    je .where
    cmp al, CDS_PAUSED
    jne .out
.where:                             ; playing or paused: the drive says which
    mov [cp_secs], cx               ; track and how far, and the selection
    cmp ah, [cp_sel]                ; follows it
    je .out
    or ah, ah
    jz .out
    mov [cp_sel], ah
    jmp cp_seltlen
.changed:
    mov byte [cp_play], CDS_IDLE
    mov word [cp_secs], 0
    jmp cp_disc
.refused:
    cmp ax, CDE_NOAUDIO
    jne .changed                    ; empty now, or gone: the disc is asked for
    mov byte [cp_state], CPS_NOAUD
.out:
    ret

; =============================================================================
; THE DISPLAY
; =============================================================================

; -----------------------------------------------------------------------------
; cp_lines - compose both display lines from the state into cp_l1 / cp_l2
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cp_lines:
    mov di, cp_l1                   ; both lines blank to their full width
    mov cx, CP_LEN * 2 + 1          ; (cp_l1, its NUL, cp_l2 - adjacent)
    mov al, ' '
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    mov byte [cp_l1+CP_LEN], 0
    mov byte [cp_l2+CP_LEN], 0
    mov bl, [cp_state]
    xor bh, bh
    shl bx, 1
    mov si, [cp_smsg+bx]            ; line 2: the state's sentence...
    mov di, cp_l2
    call cp_put
    cmp byte [cp_state], CPS_AUDIO
    jne .out
    mov bl, [cp_play]               ; ...which for an audio disc is the play's
    xor bh, bh
    shl bx, 1
    mov si, [cp_pmsg+bx]
    mov di, cp_l2
    call cp_put
    mov si, cp_s_disc               ; ...and the disc's length at its end
    mov di, cp_l2 + CP_LEN - 11
    call cp_put
    mov ax, [cp_dlen]
    call cp_mmss
    ; --- line 1: 'Track nn of nn' and 'm:ss / m:ss' --------------------------
    mov si, cp_s_track
    mov di, cp_l1
    call cp_put
    mov al, [cp_sel]
    call cp_2dig
    mov si, cp_s_of
    call cp_put
    mov al, [cp_last]
    call cp_2dig
    mov di, cp_l1 + CP_LEN - 14     ; 'mm:ss / mm:ss ', right-aligned
    mov ax, [cp_secs]
    cmp byte [cp_play], CDS_PLAYING
    je .t
    cmp byte [cp_play], CDS_PAUSED
    je .t
    xor ax, ax                      ; stopped: the track from its start
.t:
    call cp_mmss
    mov al, '/'
    mov ah, ' '
    mov [di], ax                    ; '/ '
    inc di
    inc di
    mov ax, [cp_tlen]
    call cp_mmss
.out:
    ret

; cp_put - copy a NUL string at SI to DI, NUL not copied; DI past it.
; clobbers AL, SI
cp_put:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short cp_put
.out:
    ret

; cp_2dig - AL (< 100) as two digits at DI; DI past them. clobbers AX
cp_2dig:
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    inc di
    inc di
    ret

; cp_mmss - AX seconds as 'mm:ss ' at DI (minutes to 99); DI past it.
; clobbers AX, DX
cp_mmss:
    xor dx, dx
    push cx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    pop cx
    cmp ax, 99
    jbe .m
    mov ax, 99
.m:
    call cp_2dig
    mov byte [di], ':'
    inc di
    mov ax, dx
    call cp_2dig
    mov byte [di], ' '
    inc di
    ret

; -----------------------------------------------------------------------------
; cp_show - letter the display lines that differ from what the window shows
; in:  BX = the window; gfx lock held, the clip armed (a paint's, or ours)
; out: nothing; clobbers AX, CX, DX, SI, DI
;
; A line is drawn WHOLE and only when it differs: one opaque run each, so the
; display is never blank between an erase and a lettering (SPEC.md 6.1). A
; line the clip cuts is drawn and FORGOTTEN, so the next pass draws it again.
; -----------------------------------------------------------------------------
cp_show:
    push bx
    call cp_lines
    pop bx
    mov si, cp_l1
    mov di, cp_shown1
    mov dx, CP_TY1
    call cp_line
    mov si, cp_l2
    mov di, cp_shown2
    mov dx, CP_TY2
    ; ...and falls into cp_line for the second
; cp_line - SI = a composed line, DI = what the window shows of it, DX = its
;           content-relative y, BX = the window. clobbers AX, CX, DX, SI, DI
cp_line:
    push si
    push di
    mov cx, CP_LEN
    push es
    push ds
    pop es
    cld
    repe cmpsb
    pop es
    pop di
    pop si
    je .out                         ; the window shows this already
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add dx, cx                      ; DX = the line's y
    add ax, CP_TX                   ; AX = its x
    push bx
    mov bx, dx                      ; the line's rect, inclusive
    mov cx, ax
    add cx, CP_LEN * 8 - 1
    add dx, 7
    call OSAPI_WM_CLIP_TEST         ; CF = 1: an edge cuts it. Keeps all four
    mov cx, ax                      ; CX = x, DX = y for the run - two moves,
    mov dx, bx                      ; so CF is still the test's
    pop bx
    jc .forget
    push si                         ; adopted: the window will show this
    push di
    push cx
    push es
    push ds
    pop es
    mov cx, CP_LEN
    rep movsb                       ; DS:SI (the line) -> ES:DI (shown)
    pop es
    pop cx
    pop di
    pop si
    jmp short .draw
.forget:
    mov byte [di], 0                ; a NUL never matches: drawn again next time
.draw:
    mov al, CBLACK                  ; a button drawn just before may have left
    call OSAPI_SET_COLOR            ; the pen DISABLED (SPEC.md 47 rule 1)
    mov ax, (CBLACK << 8) | CWHITE  ; AL = ink, AH = ground: white on black
    call OSAPI_FONT_RUN             ; CX = x, DX = y, SI = the run
.out:
    ret

; =============================================================================
; THE BUTTONS
; =============================================================================

; -----------------------------------------------------------------------------
; cp_bok - would this button do anything? (SPEC.md 47 rule 2: the ONE
;          predicate, shared by the greying and the release)
; in:  AL = a button index
; out: CF = 0 live, CF = 1 refused; every register preserved
; -----------------------------------------------------------------------------
cp_bok:
    push ax
    mov ah, [cp_play]
    cmp al, CB_EJECT
    je .eject
    cmp byte [cp_state], CPS_AUDIO
    jne .no                         ; no audio disc: nothing to play or skip
    cmp al, CB_PLAY
    je .play
    cmp al, CB_PAUSE
    je .going
    cmp al, CB_STOP
    je .going
.yes:                               ; Previous and Next: always, on audio
    clc
    pop ax
    ret
.play:
    cmp ah, CDS_PLAYING             ; Play: not while playing
    je .no
    jmp short .yes
.going:                             ; Pause and Stop: while playing or paused
    cmp ah, CDS_PLAYING
    je .yes
    cmp ah, CDS_PAUSED
    je .yes
    jmp short .no
.eject:
    cmp byte [cp_state], CPS_NODRV  ; Eject: whenever a drive answers
    jne .yes
.no:
    stc
    pop ax
    ret

; -----------------------------------------------------------------------------
; cp_regrey - every button's flags from cp_bok; redraw the ones that changed
;             (all of them when DL = 1, the paint's)
; in:  DL = 1 draw all, 0 only the changed. clobbers AX, BX, DI
; -----------------------------------------------------------------------------
cp_regrey:
    xor ax, ax
    mov di, cp_bflags
.one:
    call cp_bok                     ; CF = 1: refused; AX kept
    mov bx, OS88UI_FILL
    jnc .live
    inc bx                          ; ...| OS88UI_DIS
.live:
    cmp dl, 1
    je .draw
    cmp [di], bx
    je .next
.draw:
    mov [di], bx
    push ax
    push bx
    inc ax                          ; the index PLUS ONE (os88ui_btn's way)
    mov bx, cp_btrec
    call os88ui_btn                 ; keeps every register
    pop bx
    pop ax
.next:
    inc di
    inc di
    inc ax
    cmp al, CP_NB
    jb .one
    ret

; -----------------------------------------------------------------------------
; cp_act - do what button AL says (it is live: the caller asked cp_bok)
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cp_act:
    cmp al, CB_PLAY
    je .play
    cmp al, CB_PAUSE
    je .pause
    cmp al, CB_STOP
    je .stop
    cmp al, CB_EJECT
    je .eject
    cmp al, CB_NEXT
    je .next
    ; --- Previous: back to this track's start once it is under way ---------
    mov al, [cp_sel]
    call cp_going
    jne .back
    cmp word [cp_secs], CP_BACK
    ja .go                          ; a few seconds in: the same track again
.back:
    dec al
    call cp_audio_back
    cmp al, [cp_first]
    jae .go
    mov al, [cp_last]               ; ...wrapping to the last
    call cp_audio_back
    jmp short .go
.next:
    mov al, [cp_sel]
    inc al
    call cp_audio_from
    cmp al, [cp_last]
    jbe .go
    mov al, [cp_first]              ; ...wrapping to the first
    call cp_audio_from
.go:                                ; AL = the track to stand on: played at
    mov [cp_sel], al                ; once if the disc was going, selected
    mov word [cp_secs], 0           ; otherwise
    call cp_seltlen
    mov al, [cp_sel]
    call cp_going
    jne .out
.playsel:
    mov al, [cp_sel]
    xor ah, ah                      ; to the end of the disc
    xor cx, cx
    CP_CALL CDV_PLAY
    jmp short .out
.play:
    cmp byte [cp_play], CDS_PAUSED
    jne .playsel
.resume:
    CP_CALL CDV_RESUME
    jmp short .out
.pause:
    cmp byte [cp_play], CDS_PAUSED
    je .resume
    CP_CALL CDV_PAUSE
    jmp short .out
.stop:
    CP_CALL CDV_STOP
    mov word [cp_secs], 0
    jmp short .out
.eject:
    xor al, al                      ; open the tray...
    cmp byte [cp_state], CPS_NODISC
    jne .tray
    inc ax                          ; ...or close it on an empty drive
.tray:
    CP_CALL CDV_EJECT
.out:
    jmp cp_poll                     ; and say at once what the drive now does

; cp_going - ZF = 1 when the drive is playing or paused. clobbers nothing
cp_going:
    cmp byte [cp_play], CDS_PLAYING
    je .out
    cmp byte [cp_play], CDS_PAUSED
.out:
    ret

; -----------------------------------------------------------------------------
; cp_redraw - after the state moved: the display and the greying, where the
;             window can be seen
; in:  BX = the window; gfx lock held. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cp_redraw:
    cmp byte [cp_btrec+OS88UI_BT_N], 0  ; the About card is up: it is opaque
    je .out                             ; over the display, and its dismissal
                                        ; repaints the whole content
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one visible pixel
    jc .out
    call cp_show
    xor dl, dl
    call cp_regrey
.out:
    ret

; =============================================================================
; CALLBACKS
; =============================================================================

; -----------------------------------------------------------------------------
; cp_paint - W_PAINT: the panel, its lines and the six buttons; the About card
;            last
; in:  SI = the window (content white-filled, gfx lock held). Keeps SI
; -----------------------------------------------------------------------------
cp_paint:
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    push ax
    push dx
    add ax, CP_BX                   ; the buttons' rects, screen, inclusive -
    add dx, CP_BY                   ; the drawn control and the clicked one
    mov cx, dx                      ; are these same words
    add cx, CP_BH - 1
    mov di, cp_rects
.rect:
    mov [di+0], ax
    mov [di+2], dx
    add ax, CP_BW - 1
    mov [di+4], ax
    mov [di+6], cx
    add ax, CP_BGAP + 1
    add di, 8
    cmp di, cp_rects + 8 * CP_NB
    jb .rect
    pop dx
    pop ax
    push bx
    add ax, CP_PX                   ; the display's black panel
    add dx, CP_PY
    mov bx, dx
    mov cx, ax
    add cx, CP_PW - 1
    add dx, CP_PH - 1
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop bx
    mov byte [cp_shown1], 0         ; the panel has just been filled: both
    mov byte [cp_shown2], 0         ; lines are owed whole
    call cp_show
    mov dl, 1
    call cp_regrey
    pop si
    cmp byte [cp_btrec+OS88UI_BT_N], 0 ; ...and the About card LAST
    jne .out
    mov bx, si
    mov si, cp_ablines
    call os88ui_about_d             ; the paint's region is armed; it keeps
    mov si, bx                      ; every register
.out:
    ret

; -----------------------------------------------------------------------------
; cp_tick - W_ONTIMER: re-arm, ask the drive, redraw what moved
; in:  SI = the window; the UI task, gfx lock held, no clip armed
; -----------------------------------------------------------------------------
cp_tick:
    mov bx, si
    mov ax, CP_POLL                 ; one-shot: the repeat is ours to ask for
    call OSAPI_WM_TIMER
    push bx
    cmp byte [cp_argwant], 0
    je .poll
    call cp_arg                     ; a TRACKnn.CDA to play, once
    jmp short .drawn
.poll:
    call cp_poll
.drawn:
    pop bx
    jmp cp_redraw

; -----------------------------------------------------------------------------
; cp_arg - the document we were opened on: its track, played (SPEC.md
;          119.3.5). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cp_arg:
    mov byte [cp_argwant], 0
    call cp_disc                    ; the disc first: is there an audio track?
    cmp byte [cp_state], CPS_AUDIO
    jne .out
    mov dx, [cp_argdir]             ; ...then the file, from its own folder:
    mov bl, [cp_argvol]             ; GOTO_QM, the quiet stand that MOVES us -
    call OSAPI_FILE_GOTO_QM         ; GOTO_Q leaves our folder where it was, and
    jc .out                         ; a name then resolves there (SPEC.md 19.2.2)
    push es
    push ds
    pop es
    mov si, cp_argname
    mov bx, cp_cdabuf
    xor dx, dx
    mov cx, 44
    call OSAPI_FILE_READ            ; ES:BX = ours
    pop es
    jc .out
    cmp word [cp_cdabuf+8], 'CD'    ; 'CDDA' at 8: a CD audio track's file
    jne .out
    cmp word [cp_cdabuf+10], 'DA'
    jne .out
    mov al, [cp_cdabuf+22]          ; the track
    cmp al, [cp_first]
    jb .out
    cmp al, [cp_last]
    ja .out
    mov [cp_sel], al
    call cp_seltlen
    mov al, CB_PLAY
    jmp cp_act
.out:
    ret

; -----------------------------------------------------------------------------
; cp_ondrag / cp_onup - the buttons' gesture (SPEC.md 13.8): a button fires on
;                       the RELEASE
; in:  CX, DX = the pointer (screen), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
cp_ondrag:
    mov bx, cp_btrec
    jmp os88ui_btndrag

cp_onup:
    mov bx, cp_btrec
    call os88ui_btnup               ; AX = what FIRED, index + 1; 0 = cancelled
    dec ax
    js .out
    call cp_bok                     ; a greyed control explains itself (47 r6)
    jc .out
    push si
    call cp_act
    pop bx
    jmp cp_redraw
.out:
    ret

; -----------------------------------------------------------------------------
; cp_key - W_ONKEY: Space plays or pauses, the arrows step, S stops, E ejects
; in:  AL = ASCII, AH = scan, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
cp_key:
    push si
    cmp byte [cp_btrec+OS88UI_BT_N], 0
    jne .keys
    call cp_dismiss                 ; any key takes the About card down
    jmp short .out
.keys:
    mov bl, CB_PREV
    cmp ah, 0x4B                    ; Left
    je .do
    mov bl, CB_NEXT
    cmp ah, 0x4D                    ; Right
    je .do
    and al, 0xDF                    ; letters upper-cased (Space becomes 0)
    mov bl, CB_STOP
    cmp al, 'S'
    je .do
    mov bl, CB_EJECT
    cmp al, 'E'
    je .do
    or al, al
    jnz .out
    mov bl, CB_PAUSE                ; Space: pause what plays, else play
    call cp_going
    je .do
    mov bl, CB_PLAY
.do:
    mov al, bl
    call cp_bok
    jc .out
    call cp_act
    pop bx
    push bx
    call cp_redraw
.out:
    pop si
    ret

; =============================================================================
; 'About CD Player' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; While the card is up the button record is told it has NO buttons, so a
; press anywhere in the content reaches cp_onclick and takes the card down -
; and that zero IS the card's state (Timer's idiom, SPEC.md 111.6).
cp_about:
    mov byte [cp_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, cp_ablines
    jmp os88ui_about

; cp_onclick - a press that was not a button's: the card's dismissal
; out: CF = 1 the press was spent on the card
cp_onclick:
    cmp byte [cp_btrec+OS88UI_BT_N], 0
    jne .not
    call cp_dismiss
    stc
    ret
.not:
    clc
    ret

; cp_dismiss - put the buttons back and repaint the content. SI = the window
cp_dismiss:
    mov byte [cp_btrec+OS88UI_BT_N], CP_NB
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT
    mov bx, dx
    mov cx, ax
    add cx, CP_CW - 1
    add dx, CP_CH - 1
    call OSAPI_GFX_FILL
    call cp_paint                   ; SI = the window still; keeps it
.out:
    ret

; =============================================================================
; DATA
; =============================================================================
cp_tpl:     dw 176, 64, CP_W, CP_H
            dw cp_ttl, cp_paint, cp_key, 0

cp_ttl      equ 16                  ; the header's name field: 'CD Player'
cp_s_prev:  db '|<', 0
cp_s_play:  db 'Play', 0
cp_s_pause: db 'Pause', 0
cp_s_stop:  db 'Stop', 0
cp_s_next:  db '>|', 0
cp_s_eject: db 'Eject', 0
cp_btlbl:   dw cp_s_prev, cp_s_play, cp_s_pause, cp_s_stop, cp_s_next
            dw cp_s_eject

cp_s_track: db 'Track ', 0
cp_s_of:    db ' of ', 0
cp_s_disc:  db 'Disc ', 0
cp_smsg:    dw cp_s_nodrv, cp_s_nodisc, cp_s_data, cp_s_none, cp_s_noaud
            dw cp_s_err
cp_s_nodrv: db 'CD-ROM driver not loaded', 0
cp_s_nodisc: db 'No disc', 0
cp_s_data:  db 'Data disc - no audio tracks', 0
cp_s_none:  db 0
cp_s_noaud: db 'This drive plays no audio', 0
cp_s_err:   db 'The drive does not answer', 0
cp_pmsg:    dw cp_s_stopped, cp_s_playing, cp_s_paused, cp_s_stopped
            dw cp_s_perr
cp_s_stopped: db 'Stopped', 0
cp_s_playing: db 'Playing', 0
cp_s_paused: db 'Paused', 0
cp_s_perr:  db 'Stopped: a drive error', 0
%if CPS_NODRV != 0 || CPS_NODISC != 1 || CPS_DATA != 2 || CPS_AUDIO != 3 || CPS_NOAUD != 4 || CPS_ERR != 5
%error "cp_smsg is indexed by CPS_*"
%endif
%if CDS_IDLE != 0 || CDS_PLAYING != 1 || CDS_PAUSED != 2 || CDS_DONE != 3 || CDS_ERROR != 4
%error "cp_pmsg is indexed by CDS_*"
%endif

    OS88UI_BTNREC cp_btrec, cp_rects, cp_btlbl, cp_bflags, CP_NB

cp_ablines:
    dw cp_ab1, cp_ab2, cp_ab3, 0
cp_ab1:     db 'CD Player for os8088', 0
cp_ab2:     db 'plays audio CDs in the', 0
cp_ab3:     db 'CD-ROM drive (CDROM.DRV)', 0

%define OS88UI_ABOUT
%define OS88UI_NOGLYPH
%include "os88ui.inc"
%if OS88UI_DIS != 1 || (OS88UI_FILL & 1)
%error "cp_regrey greys with an inc: it needs OS88UI_DIS at bit 0 and FILL clear of it"
%endif

CPB_SIZE    equ 92 + 4 * CP_LEN + 2 + 44
    OS88_BSS CPB_SIZE
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------
cp_state    equ os88_image_end + 0  ; CPS_*
cp_play     equ os88_image_end + 1  ; CDS_*, the last status
cp_first    equ os88_image_end + 2  ; the disc's first and last track
cp_last     equ os88_image_end + 3
cp_gen      equ os88_image_end + 4  ; the media generation the TOC came from
cp_sel      equ os88_image_end + 5  ; the track shown, played next
cp_argwant  equ os88_image_end + 6  ; 1 = a .CDA to play at the first poll
cp_argvol   equ os88_image_end + 7
cp_dlen     equ os88_image_end + 8  ; word: the disc, in seconds
cp_tlen     equ os88_image_end + 10 ; word: the shown track, in seconds
cp_secs     equ os88_image_end + 12 ; word: seconds into it
cp_tctl     equ os88_image_end + 14 ; word: its control nibble
cp_argdir   equ os88_image_end + 16 ; word
cp_argname  equ os88_image_end + 18 ; 14
cp_rects    equ os88_image_end + 32 ; 6 x 4 words
cp_bflags   equ os88_image_end + 80 ; 6 words
cp_l1       equ os88_image_end + 92 ; CP_LEN + 1, then...
cp_l2       equ cp_l1 + CP_LEN + 1  ; ...CP_LEN + 1: ADJACENT (cp_lines)
cp_shown1   equ cp_l2 + CP_LEN + 1  ; CP_LEN: what the window shows
cp_shown2   equ cp_shown1 + CP_LEN
cp_cdabuf   equ cp_shown2 + CP_LEN  ; 44: a TRACKnn.CDA's bytes
%if cp_cdabuf + 44 - os88_image_end != CPB_SIZE
%error "the CD Player's bss layout and OS88_BSS disagree"
%endif
