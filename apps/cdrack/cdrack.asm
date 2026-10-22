; =============================================================================
; os8088 - apps/cdrack/cdrack.asm
;
; CDRACK (SPEC.md 119.6): an audio CD in an ATAPI drive, played through the
; drive's own audio output, behind the face of an 80s rack CD player - a
; seven-segment LCD with the track grid, a transport with picture buttons, a
; volume slider, repeat / shuffle / program / remaining-time toggles and the
; disc's track list. The design is the owner's three mockups, kept in
; docs/cdrack/design/.
;
; THE FILES:
;   cdrack.asm   this: the entry, the door, the play logic, the commands, the
;                menus and the callbacks
;   crdraw.inc   the drawing primitives - roles, themes, the band
;   crface.inc   the three layouts and every field drawn from a cache
;   build/cdart.inc  the pictures and both fonts, made on the host by
;                tools/os88cdart.py
;
; EVERYTHING GOES THROUGH CDROM.DRV'S DOOR (drivers/cdrom/cdpkg.inc,
; SPEC.md 119.4): OSAPI_DRV_CALL with BH = DRVC_CD. The player owns no port.
; The drive plays, and READ SUB-CHANNEL - asked every CR_POLL ticks by the
; window's one-shot timer - says where it is. A moved media generation is a
; different disc, and its TOC is read again.
;
; PLAY ORDER (SPEC.md 119.6.5): with SHUFFLE and PROGRAM both off a play runs
; to the end of the disc on the drive's own, and the selection follows the
; track the drive reports. With either on, a play is ONE track, and the poll
; that sees it DONE chooses the next - the next programmed track, or a random
; one not yet played. REPEAT wraps both.
;
; REGISTER CONTRACTS are the callbacks' (SPEC.md 111.6's statement of them):
; W_PAINT and W_ONKEY give SI back; W_ONTIMER and the W_ONCLICK family may
; clobber AX, BX, CX, DX, DI and ES. Internal procs say what they clobber.
; =============================================================================

%include "os88api.inc"
%include "cdpkg.inc"

    OS88_HEADER 'CDRack', cr_entry, 3       ; flags: bit 0 the icon, bit 1
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

CR_POLL     equ 9                   ; ticks between status polls: under half a
                                    ; second, so a displayed second is at most
                                    ; one poll late
CR_BACK     equ 3                   ; seconds into a track past which Previous
                                    ; restarts it rather than going back one
CR_SKIP     equ 10                  ; seconds Rewind and Fast Forward move

; --- the buttons, in the record's order -----------------------------------------
B_PREV      equ 0
B_REW       equ 1
B_PLAY      equ 2
B_PAUSE     equ 3
B_STOP      equ 4
B_FF        equ 5
B_NEXT      equ 6
B_EJECT     equ 7
B_REPEAT    equ 8                   ; the four toggles: cr_opts, in this order
B_SHUFFLE   equ 9
B_PROGRAM   equ 10
B_TIME      equ 11
B_EXPAND    equ 12                  ; COMPACT's way back to the full player
CR_NB       equ 13
CR_PARK     equ 0x7FF0              ; a button this layout does not have: its
                                    ; rect is off every screen
CRF_ON      equ 0x0100              ; a toggle's flags: it is on (the library
                                    ; reads none of the high byte without
                                    ; OS88UI_INK, which these never carry)

; --- what the player knows: cr_state ----------------------------------------------
CPS_NODRV   equ 0                   ; no CD-ROM driver answers the door
CPS_NODISC  equ 1                   ; the drive is empty, or its tray is open
CPS_DATA    equ 2                   ; a disc with no audio track
CPS_AUDIO   equ 3                   ; an audio disc: [cr_play] says what it does
CPS_NOAUD   equ 4                   ; the drive has no audio commands (QEMU's)
CPS_ERR     equ 5                   ; the drive did not answer

; --- the theme roles (crdraw.inc): one byte each in cr_tcol / cr_tbw -------------
R_BG        equ 0                   ; the window's ground
R_PANEL     equ 1                   ; a panel's inside
R_EDGE      equ 2                   ; every panel's rule
R_LOGO      equ 3
R_LABEL     equ 4                   ; captions printed on a panel
R_LCD       equ 5                   ; the LCD's glass...
R_LCDEDGE   equ 6
R_LCDFG     equ 7                   ; ...its lit segments
R_LCDLBL    equ 8                   ; ...its printed labels
R_LCDDIM    equ 9                   ; ...its rules, and a data track's number
R_LCDSEL    equ 10                  ; the grid's selected cell
R_LCDPRG    equ 11                  ; a programmed track's number
R_BTN       equ 12                  ; a button: face, rule, picture
R_BTNEDGE   equ 13
R_BTNTXT    equ 14
R_BTNDIS    equ 15                  ; ...greyed (the colour face's)
R_BTNON     equ 16                  ; ...pressed or latched
R_BTNONE    equ 17
R_BTNONT    equ 18
R_SLOT      equ 19                  ; the tray's slot and its label
R_SLOTLBL   equ 20
R_SLOTTXT   equ 21
R_SLFILL    equ 22                  ; a slider's (or a bar's) done part
R_SLREST    equ 23                  ; ...and the rest
R_THUMB     equ 24
R_LIST      equ 25                  ; the track list's rows
R_LISTTXT   equ 26
R_SEL       equ 27                  ; ...the selected row
R_SELTXT    equ 28
R_TROUGH    equ 29                  ; its scroll bar's trough
R_STAT      equ 30                  ; the status bar
R_STATTXT   equ 31
R_HDR       equ 32                  ; the logo's panel
R_TEXT      equ 33                  ; a title on a panel
R_PROGBG    equ 34                  ; the LCD's progress bar: to go...
R_PROGFG    equ 35                  ; ...and done
CR_NROLES   equ 36

CR_BANDSZ   equ 1024                ; the composing band (crdraw.inc)
CR_NKEYS    equ 40                  ; element keys: FULL's 32, with room

; =============================================================================
; ENTRY
; =============================================================================

; -----------------------------------------------------------------------------
; cr_entry - the package entry point (SPEC.md 20.2)
; out: BX = the window, CF clear (set = abort)
; The loader shows the window; nothing here may draw. The first poll is armed
; for the next tick, so the display is filled as soon as the window is up.
; -----------------------------------------------------------------------------
cr_entry:
    call OSAPI_ARG_FILE             ; a TRACKnn.CDA? CF = 1: launched empty
    jc .plain
    mov di, cr_argname              ; SI -> its name in KERNEL_SEG: copied out
    mov cx, 13                      ; before anything else is called
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    mov [cr_argdir], dx
    mov [cr_argvol], bl
    mov byte [cr_argwant], 1
.plain:
    call OSAPI_VIDEO                ; the primary: colour by default where it
    mov byte [cr_theme], 1          ; has more than one bit (SPEC.md 119.6.2)
    cmp dh, 1
    jbe .bw
    mov byte [cr_theme], 0
.bw:
    sub cx, MBAR_H                  ; its desktop band, for the size table
    mov [cr_pband], cx
    call OSAPI_GET_TICKS
    or ax, 1
    mov [cr_seed], ax
    mov word [cr_lp], cr_lc1        ; a layout to stand on before the paint
    mov word [cr_th], cr_tbw
    mov si, cr_tpl
    call OSAPI_WM_CREATE            ; BX = the window
    jc cr_ret
    OS88_REGION_MOVABLE_NOKEEP cr_ret ; every byte that outlives a pass is a
                                    ; static and moves with us
    mov [cr_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on the 8-pixel grid
    mov al, 1
    call OSAPI_WM_OWNBG             ; every pixel is the paint's (the face is
                                    ; dark: no white flash before it)
    mov si, cr_about
    call OSAPI_ABOUT_SET
    mov si, cr_menus
    call OSAPI_MENU_SET
    mov ax, cr_wheel
    call OSAPI_WM_ONWHEEL
    mov ax, cr_tick
    call OSAPI_WM_ONTIMER
    mov ax, 1
    call OSAPI_WM_TIMER             ; the first poll, next tick (CF = 1 on
                                    ; kern_small, which has no driver either:
                                    ; the display then says so at the paint)
    call cr_prefset
    mov si, cr_pref
    call OSAPI_WM_PREFER            ; the size, per adapter - and applied
    xchg ax, bx                     ; AX = the window
    mov bx, cr_btrec
    mov si, cr_onup
    mov di, cr_ondrag
    mov dx, cr_onclick
    call os88ui_btninit             ; keeps every register
    xchg ax, bx
    clc
cr_ret:
    ret

; -----------------------------------------------------------------------------
; cr_prefset - the OSAPI_WM_PREFER table for what the user wants: the big
;              player on VGA (or MID where the primary's band is EGA's 350
;              lines) and on Hercules, the compact one on the CGA - and the
;              compact one everywhere once asked for. clobbers AX
; -----------------------------------------------------------------------------
cr_prefset:
    mov word [cr_pref], CF_W + 2
    mov word [cr_pref+4], CF_W + 2
    mov word [cr_pref+8], CC_W + 2
    mov word [cr_pref+10], C1_H + TITLE_H + 1
    cmp byte [cr_want], 0
    jne .cmp
    mov ax, CF_H + TITLE_H + 1
    cmp word [cr_pband], CF_H + TITLE_H + 1
    jae .vga
    mov ax, CM_H + TITLE_H + 1
.vga:
    mov [cr_pref+2], ax
    mov word [cr_pref+6], CM_H + TITLE_H + 1
    ret
.cmp:
    mov word [cr_pref], CC_W + 2
    mov word [cr_pref+4], CC_W + 2
    mov word [cr_pref+2], C2_H + TITLE_H + 1
    mov word [cr_pref+6], C2_H + TITLE_H + 1
    ret

; =============================================================================
; THE DOOR
; =============================================================================

; CR_CALL - OSAPI_DRV_CALL with BH = DRVC_CD and BL = the verb. A macro and
; not a proc: BX is the door's, AX/CX/DX/SI the verb's.
%macro CR_CALL 1
    mov bx, DRVC_CD * 256 + %1
    call OSAPI_DRV_CALL
%endmacro

; -----------------------------------------------------------------------------
; cr_disc - ask the drive what is in it, and set the state from the answer;
;           an audio disc's TOC is cached - every track's length and whether
;           it is audio - so the list and the play order never ask again
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_disc:
    mov byte [cr_state], CPS_NODRV
    CR_CALL CDV_IDENT
    jc .out
    cmp ax, CD_SIG
    jne .out                        ; somebody else in the slot (SPEC.md 20.11.1)
    xor di, di                      ; the volume's letter, for the status bar
    CR_CALL CDV_DRIVE
    jc .nolet
    mov byte [cr_letter], 0
    cmp dl, 26
    jae .nolet
    add dl, 'A'
    mov [cr_letter], dl
.nolet:
    CR_CALL CDV_DISC
    jc .refused
    cmp dh, [cr_gen]                ; a different disc: its program and its
    je .same                        ; shuffle's memory go with the old one
    mov di, cr_prog
    call cr_bclr
    mov di, cr_played
    call cr_bclr
.same:
    mov [cr_first], al
    mov [cr_last], ah
    mov [cr_dlen], cx
    mov [cr_gen], dh
    mov byte [cr_state], CPS_DATA
    test dl, CDK_AUDIO
    jz .out
    mov byte [cr_state], CPS_AUDIO
    mov di, cr_isaud                ; the TOC, cached
    call cr_bclr
    mov al, [cr_first]
.toc:
    push ax
    CR_CALL CDV_TRACK               ; CX = its length, SI = its control nibble
    pop ax
    jnc .tk
    xor cx, cx
    mov si, CDT_DATA
.tk:
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov [cr_tlens+bx], cx
    test si, CDT_DATA
    jnz .data
    mov si, cr_isaud
    call cr_bset
.data:
    inc al
    cmp al, [cr_last]
    jbe .toc
    mov al, [cr_sel]                ; keep the selection if this disc has it
    call cr_isaudt
    jc .tlen
    mov al, [cr_first]
    call cr_audfrom                 ; ...else the first AUDIO track
    mov [cr_sel], al
.tlen:
    call cr_seltlen
    mov byte [cr_volok], 0          ; the drive's volume, if it has one
    xor al, al
    CR_CALL CDV_VOLUME
    jc .out
    mov al, cl                      ; 0..255 -> 0..100
    mov ah, 100
    mul ah
    mov cl, 255
    div cl
    mov [cr_vol], al
    mov byte [cr_volok], 1
.out:
    ret
.refused:
    mov byte [cr_state], CPS_NODISC
    cmp ax, CDE_NODISC
    je .out
    mov byte [cr_state], CPS_ERR
    ret

; cr_seltlen - [cr_tlen] = the selected track's length. clobbers BX
cr_seltlen:
    mov bl, [cr_sel]
    xor bh, bh
    shl bx, 1
    mov bx, [cr_tlens+bx]
    mov [cr_tlen], bx
    ret

; -----------------------------------------------------------------------------
; cr_poll - where the drive is: the play state, the track and the time; and
;           the next track of a one-track play that has ended
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
;
; A generation that moved is a different disc (SPEC.md 119.3.4): the TOC is
; read again. A refusal is the empty drive, or the driver gone.
; -----------------------------------------------------------------------------
cr_poll:
    cmp byte [cr_state], CPS_AUDIO
    je .ask
    cmp byte [cr_state], CPS_NOAUD
    jne .again                      ; empty, data or gone: ask again
    CR_CALL CDV_DISC                ; a drive with no audio commands: only a
    jc .again                       ; DIFFERENT disc (or none) is news, so the
    cmp dh, [cr_gen]                ; state stays put until the generation
    je .out                         ; moves - re-asking every poll would flip
.again:                             ; the display between two states
    jmp cr_disc
.ask:
    CR_CALL CDV_STATUS
    jc .refused
    mov bx, si
    cmp bl, [cr_gen]
    jne .changed
    mov [cr_play], al
    cmp al, CDS_PLAYING
    je .where
    cmp al, CDS_PAUSED
    je .where
    cmp al, CDS_DONE
    je .done
    cmp al, CDS_ERROR
    jne .out
    mov byte [cr_ours], 0          ; an error ends what we started
    ret
.done:
    cmp byte [cr_ours], 0          ; a play of OURS ran out: the next one
    je .out
    jmp cr_advance
.where:                             ; playing or paused: the drive says which
    mov [cr_secs], cx               ; track and how far, and the selection
    cmp ah, [cr_sel]                ; follows it
    je .out
    or ah, ah
    jz .out
    mov [cr_sel], ah
    jmp cr_seltlen
.changed:
    mov byte [cr_play], CDS_IDLE
    mov byte [cr_ours], 0
    mov word [cr_secs], 0
    jmp cr_disc
.refused:
    cmp ax, CDE_NOAUDIO
    jne .changed                    ; empty now, or gone: the disc is asked for
    mov byte [cr_state], CPS_NOAUD
.out:
    ret

; =============================================================================
; THE PLAY ORDER (SPEC.md 119.6.5)
; =============================================================================

; cr_bit - AL = a track, SI = a 13-byte set -> CF = 1 when it is in it.
; keeps everything
cr_bit:
    push ax
    push bx
    push cx
    call cr_bpos                    ; BX = its byte, AL = its mask
    test [si+bx], al
    jz .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop cx
    pop bx
    pop ax
    ret

; cr_bset / cr_bflip - put AL into set SI / turn it over. keeps everything
cr_bset:
    push ax
    push bx
    push cx
    call cr_bpos
    or [si+bx], al
    pop cx
    pop bx
    pop ax
    ret

cr_bflip:
    push ax
    push bx
    push cx
    call cr_bpos
    xor [si+bx], al
    pop cx
    pop bx
    pop ax
    ret

; cr_bpos - AL = a track -> BX = its byte in a set, AL = its bit. clobbers CL
cr_bpos:
    mov bl, al
    xor bh, bh
    mov cl, 3
    shr bx, cl
    mov cl, al
    and cl, 7
    mov al, 1
    shl al, cl
    ret

; cr_bclr - DI = a 13-byte set, emptied. keeps everything
cr_bclr:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    xor al, al
    mov cx, CR_SETSZ
    cld
    rep stosb
    pop es
    pop di
    pop cx
    pop ax
    ret

; cr_isaudt - CF = 1 when AL is an audio track of this disc. keeps everything
cr_isaudt:
    cmp al, [cr_first]
    jb .no
    cmp al, [cr_last]
    ja .no
    push si
    mov si, cr_isaud
    call cr_bit
    pop si
    ret
.no:
    clc
    ret

; cr_progon - ZF = 0 when PROGRAM is on AND something is programmed: the
; program decides the order. keeps everything
cr_progon:
    push ax
    push cx
    push si
    cmp byte [cr_progm], 0
    je .out
    mov si, cr_prog
    mov cx, CR_SETSZ
    xor al, al
.or:
    or al, [si]
    inc si
    loop .or
    or al, al
.out:
    pop si
    pop cx
    pop ax
    ret

; cr_elig - CF = 1 when AL may be played next: an audio track, and a
; programmed one when the program decides. keeps everything
cr_elig:
    call cr_isaudt
    jnc .out
    call cr_progon
    jz .yes
    push si
    mov si, cr_prog
    call cr_bit
    pop si
    ret
.yes:
    stc
.out:
    ret

; cr_audfrom - AL = a track -> AL = it or the next eligible one after it,
; or past [cr_last] when there is none.  cr_audback - the same, downwards,
; below [cr_first] for none (which is at least 1, so never wraps through 0).
; keep everything else
cr_audfrom:
    cmp al, [cr_last]
    ja .out
    call cr_elig
    jc .out
    inc al
    jmp short cr_audfrom
.out:
    ret

cr_audback:
    cmp al, [cr_first]
    jb .out
    call cr_elig
    jc .out
    dec al
    jmp short cr_audback
.out:
    ret

; cr_seq - ZF = 0 when a play is one track at a time (shuffle, or a program)
cr_seq:
    cmp byte [cr_shuf], 0
    jne .out
    call cr_progon
.out:
    ret

; -----------------------------------------------------------------------------
; cr_nextt - the track after AL in the play order
; in:  AL = the track, BL = 1 to wrap whatever REPEAT says (the Next button)
; out: AL = the next, 0 = none. clobbers AH, BX, CX, DX, SI
; -----------------------------------------------------------------------------
cr_nextt:
    or bl, [cr_repeat]
    cmp byte [cr_shuf], 0
    jne .shuffle
    inc al
    call cr_audfrom
    cmp al, [cr_last]
    jbe .out
    or bl, bl
    jz .none
    mov al, [cr_first]
    call cr_audfrom
    cmp al, [cr_last]
    jbe .out
.none:
    xor al, al
.out:
    ret
.shuffle:
    mov si, cr_played               ; this one is played...
    call cr_bset
    call cr_rpick                   ; ...so one that is not
    or al, al
    jnz .out
    or bl, bl
    jz .out                         ; all played, and no repeat: the end
    mov di, cr_played               ; round again
    call cr_bclr
    call cr_rpick
    ret

; cr_rpick - AL = a random eligible track not in cr_played, 0 = none.
; clobbers AH, CX, DX, SI
cr_rpick:
    xor cx, cx                      ; how many there are
    mov al, [cr_first]
.count:
    call cr_unplayed
    jnc .c1
    inc cx
.c1:
    inc al
    cmp al, [cr_last]
    jbe .count
    xor al, al
    jcxz .out
    push cx
    mov ax, [cr_seed]               ; one 16-bit LCG step (Numerical Recipes'
    mov dx, 25173                   ; constants): plenty for "which song next"
    mul dx
    add ax, 13849
    mov [cr_seed], ax
    pop cx
    xor dx, dx
    div cx                          ; DX = the pick, 0..CX-1
    mov al, [cr_first]
.find:
    call cr_unplayed
    jnc .f1
    or dx, dx
    jz .out
    dec dx
.f1:
    inc al
    jmp short .find
.out:
    ret

; cr_unplayed - CF = 1 when AL is eligible and not yet played. keeps all
cr_unplayed:
    call cr_elig
    jnc .out
    push si
    mov si, cr_played
    call cr_bit
    pop si
    cmc
.out:
    ret

; -----------------------------------------------------------------------------
; cr_playt - play track AL from CX seconds in: to the disc's end, or the one
;            track when the order is ours. clobbers AX, BX, CX, DX, SI
; -----------------------------------------------------------------------------
cr_playt:
    mov [cr_sel], al
    mov [cr_secs], cx
    push ax
    call cr_seltlen
    pop ax
    mov byte [cr_ours], 1
    mov byte [cr_play], CDS_PLAYING ; what the next poll will confirm
    xor ah, ah                      ; to the end of the disc...
    call cr_seq
    jz .go
    mov ah, al                      ; ...or this track alone
.go:
    CR_CALL CDV_PLAY
    ret

; cr_advance - a one-track play ended: the next, or the stop at the end
cr_advance:
    mov al, [cr_sel]
    xor bl, bl
    call cr_nextt
    or al, al
    jz .end
    xor cx, cx
    jmp cr_playt
.end:
    mov byte [cr_ours], 0
    mov byte [cr_play], CDS_IDLE
    mov word [cr_secs], 0
    mov al, [cr_first]              ; back to the start of the order
    call cr_audfrom
    cmp al, [cr_last]
    ja .out
    mov [cr_sel], al
    call cr_seltlen
.out:
    ret

; cr_going - ZF = 1 when the drive is playing or paused. clobbers nothing
cr_going:
    cmp byte [cr_play], CDS_PLAYING
    je .out
    cmp byte [cr_play], CDS_PAUSED
.out:
    ret

; =============================================================================
; THE COMMANDS
; =============================================================================

; -----------------------------------------------------------------------------
; cr_bok - would this button do anything? (SPEC.md 47 rule 2: the ONE
;          predicate, shared by the greying, the menus and the release)
; in:  AL = a button index
; out: CF = 0 live, CF = 1 refused; every register preserved
; -----------------------------------------------------------------------------
cr_bok:
    cmp al, B_REPEAT
    jb .deck
    cmp al, B_EXPAND
    je cr_canbig
    clc                             ; the four toggles: always
    ret
.deck:
    cmp al, B_EJECT
    je .eject
    cmp byte [cr_state], CPS_AUDIO
    jne .no                         ; no audio disc: nothing to play or skip
    cmp al, B_PREV
    je .yes
    cmp al, B_NEXT
    je .yes
    cmp al, B_PLAY
    je .yes
    call cr_going                   ; Rewind, Pause, Stop, Fast Forward: while
    je .yes                         ; something plays or is paused
.no:
    stc
    ret
.yes:
    clc
    ret
.eject:
    cmp byte [cr_state], CPS_NODRV  ; Eject: whenever a drive answers
    je .no
    clc
    ret

; cr_canbig - CF = 0 when the display holds the big player (MID at least).
; keeps everything
cr_canbig:
    cmp word [cr_dband], CM_H + TITLE_H + 1
    jb .no
    cmp word [cr_dwid], CF_W + 2
    jb .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cr_regrey - every button's flags from cr_bok; draw the ones that changed
;             (all when DL = 1)
; clobbers AX, BX, DI
; -----------------------------------------------------------------------------
cr_regrey:
    xor ax, ax
    mov di, cr_bflags
.one:
    call cr_bflag                   ; BX = the flags it should have
    cmp dl, 1
    je .draw
    cmp [di], bx
    je .next
.draw:
    mov [di], bx
    push ax
    push bx
    inc ax                          ; the index PLUS ONE (os88ui_btn's way)
    mov bx, cr_btrec
    call os88ui_btn                 ; keeps every register
    pop bx
    pop ax
.next:
    inc di
    inc di
    inc ax
    cmp al, CR_NB
    jb .one
    ret

; cr_bflag - AL = a button -> BX = its flags: OWN, greyed, latched, on.
; keeps everything else
cr_bflag:
    mov bx, OS88UI_OWN
    call cr_bok
    jnc .live
    or bx, OS88UI_DIS
.live:
    cmp al, B_PLAY
    jne .np
    cmp byte [cr_play], CDS_PLAYING
    jne .out
    call cr_isaudio
    jne .out
    or bx, OS88UI_LATCH
    ret
.np:
    cmp al, B_PAUSE
    jne .tg
    cmp byte [cr_play], CDS_PAUSED
    jne .out
    call cr_isaudio
    jne .out
    or bx, OS88UI_LATCH
    ret
.tg:
    cmp al, B_REPEAT
    jb .out
    cmp al, B_TIME
    ja .out
    push si
    mov si, ax
    and si, 0xFF
    cmp byte [cr_opts + si - B_REPEAT], 0
    pop si
    je .out
    or bx, CRF_ON
.out:
    ret

; -----------------------------------------------------------------------------
; cr_act - do what button AL says (it is live: the caller asked cr_bok)
; in:  BX = the window
; out: nothing; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_act:
    cmp al, B_REPEAT
    jb .deck
    cmp al, B_EXPAND
    je .expand
    mov bl, al                      ; a toggle
    xor bh, bh
    xor byte [cr_opts + bx - B_REPEAT], 1
    cmp al, B_SHUFFLE
    jne .tout
    mov di, cr_played               ; a shuffle starts with nothing played
    call cr_bclr
.tout:
    ret
.expand:
    jmp cr_relayout
.deck:
    cmp al, B_PLAY
    je .play
    cmp al, B_PAUSE
    je .pause
    cmp al, B_STOP
    je .stop
    cmp al, B_EJECT
    je .eject
    cmp al, B_NEXT
    je .next
    cmp al, B_REW
    je .rew
    cmp al, B_FF
    je .ff
    ; --- Previous: back to this track's start once it is under way ---------
    mov al, [cr_sel]
    call cr_going
    jne .back
    cmp word [cr_secs], CR_BACK
    ja .go                          ; a few seconds in: the same track again
.back:
    cmp byte [cr_shuf], 0           ; a shuffle has no "before"
    jne .go
    dec al
    call cr_audback
    cmp al, [cr_first]
    jae .go
    mov al, [cr_last]               ; ...wrapping to the last
    call cr_audback
    cmp al, [cr_first]
    jae .go
    mov al, [cr_sel]
    jmp short .go
.next:
    mov al, [cr_sel]
    mov bl, 1
    call cr_nextt
    or al, al
    jnz .go
    mov al, [cr_sel]
.go:                                ; AL = the track to stand on: played at
    call cr_going                   ; once if the disc was going, selected
    je .playat0                     ; otherwise
    mov [cr_sel], al
    mov word [cr_secs], 0
    call cr_seltlen
    jmp .out
.playat0:
    xor cx, cx
    call cr_playt
    jmp .out
.rew:
    mov cx, [cr_secs]
    sub cx, CR_SKIP
    jae .seek
    xor cx, cx
    jmp short .seek
.ff:
    mov cx, [cr_secs]
    add cx, CR_SKIP
    mov ax, [cr_tlen]
    dec ax
    js .seek0
    cmp cx, ax
    jb .seek
    mov cx, ax
    jmp short .seek
.seek0:
    xor cx, cx
.seek:
    mov al, [cr_sel]
    call cr_playt
    jmp .out
.play:
    cmp byte [cr_play], CDS_PAUSED
    je .resume
    cmp byte [cr_play], CDS_PLAYING
    je .out                         ; already: nothing to do
    mov al, [cr_sel]                ; a fresh start, in the play order
    cmp byte [cr_shuf], 0
    je .start
    mov di, cr_played
    call cr_bclr
    call cr_rpick
    or al, al
    jz .out
    jmp short .startp
.start:
    call cr_elig
    jc .startp
    mov al, [cr_first]              ; the selection is not in the program:
    call cr_audfrom                 ; the program's first
    cmp al, [cr_last]
    ja .out
.startp:
    xor cx, cx
    call cr_playt
    jmp short .out
.resume:
    CR_CALL CDV_RESUME
    jmp short .out
.pause:
    cmp byte [cr_play], CDS_PAUSED
    je .resume
    CR_CALL CDV_PAUSE
    jmp short .out
.stop:
    CR_CALL CDV_STOP
    mov byte [cr_ours], 0
    mov word [cr_secs], 0
    jmp short .out
.eject:
    mov byte [cr_ours], 0
    xor al, al                      ; open the tray...
    cmp byte [cr_state], CPS_NODISC
    jne .tray
    inc ax                          ; ...or close it on an empty drive
.tray:
    CR_CALL CDV_EJECT
.out:
    jmp cr_poll                     ; and say at once what the drive now does

; -----------------------------------------------------------------------------
; cr_pick - the user chose track AL in the grid or the list: with PROGRAM on
;           it goes into (or out of) the program, else it plays.
; clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_pick:
    call cr_isaudt
    jnc .out                        ; a data track, or none: nothing
    cmp byte [cr_progm], 0
    je .play
    mov si, cr_prog
    jmp cr_bflip
.play:
    cmp byte [cr_state], CPS_AUDIO
    je .go
    mov [cr_sel], al                ; a drive that plays nothing: chosen only
    jmp cr_seltlen
.go:
    xor cx, cx
    call cr_playt
    jmp cr_poll
.out:
    ret

; -----------------------------------------------------------------------------
; cr_relayout - between the compact player and the big one (Options and the
;               Expand button): the size table for the new choice, and the
;               window resized to it on the display it is on
; in:  BX = the window. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_relayout:
    mov si, [cr_lp]
    cmp byte [si+L_CMP], 0
    mov byte [cr_want], 1
    je .size
    call cr_canbig                  ; compact now: the big one, if it fits
    jc .out
    mov byte [cr_want], 0
.size:
    call cr_prefset
    mov cx, CF_W + 2
    mov dx, CF_H + TITLE_H + 1
    cmp byte [cr_want], 0
    jne .small
    cmp word [cr_dband], dx
    jae .rs
    mov dx, CM_H + TITLE_H + 1
    jmp short .rs
.small:
    mov cx, CC_W + 2
    mov dx, C2_H + TITLE_H + 1
    cmp word [cr_dband], dx
    jae .rs
    mov dx, C1_H + TITLE_H + 1
.rs:
    call OSAPI_WM_RESIZE            ; and the whole window repainted
.out:
    ret

; -----------------------------------------------------------------------------
; cr_lfollow - the list's top: the selection kept in view when it MOVED (a
;              scroll of the user's is left alone), and clamped to the disc
; clobbers AX
; -----------------------------------------------------------------------------
cr_lfollow:
    call cr_isaudio
    jne .out
    mov al, [cr_sel]
    cmp al, [cr_lsel]
    je .clamp
    mov [cr_lsel], al
    cmp al, [cr_ltop]
    jae .below
    mov [cr_ltop], al
.below:
    sub al, CF_LN - 1
    jb .clamp
    cmp al, [cr_ltop]
    jbe .clamp
    mov [cr_ltop], al
.clamp:
    mov al, [cr_last]               ; the top at most last - 5...
    sub al, CF_LN - 1
    jb .first
    cmp al, [cr_first]
    jae .max
.first:
    mov al, [cr_first]
.max:
    cmp [cr_ltop], al
    jbe .min
    mov [cr_ltop], al
.min:
    mov al, [cr_first]              ; ...and at least the first
    cmp [cr_ltop], al
    jae .out
    mov [cr_ltop], al
.out:
    ret

; cr_scroll - the list's top moved AX rows (signed). clobbers AX
cr_scroll:
    add al, [cr_ltop]
    test ah, 0x80
    jz .pos
    cmp al, [cr_ltop]               ; went below 0: the first
    jbe .set
    xor al, al
    jmp short .set
.pos:
    cmp al, [cr_ltop]
    jae .set
    mov al, 0xFF                    ; wrapped past 255: the clamp's
.set:
    mov [cr_ltop], al
    jmp cr_lfollow

; =============================================================================
; DRAWING ENTRY POINTS
; =============================================================================

; -----------------------------------------------------------------------------
; cr_redraw - after the state moved: what changed, where the window can be seen
; in:  BX = the window; gfx lock held. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_redraw:
    cmp byte [cr_btrec+OS88UI_BT_N], 0  ; the About card is up: it is opaque
    je .out                             ; over the face, and its dismissal
                                        ; repaints the whole content
    call OSAPI_WM_CLIP_SET          ; CF = 1: not one visible pixel
    jc .out
    call cr_origin
    xor dl, dl
    call cr_update
.out:
    ret

; -----------------------------------------------------------------------------
; cr_paint - W_PAINT: the layout's static parts, every element and every
;            button; the About card last
; in:  SI = the window (gfx lock held; NOT white-filled - OSAPI_WM_OWNBG).
;      Keeps SI
; -----------------------------------------------------------------------------
cr_paint:
    push si
    mov bx, si
    call cr_setup
    push bx
    call OSAPI_WM_DISPLAY           ; the band, for cr_canbig
    sub cx, si
    mov [cr_dband], cx
    mov [cr_dwid], ax
    pop bx
    mov si, [cr_lp]
    call [si+L_DECO]                ; keeps BX
    call cr_forget
    mov dl, 1
    call cr_update
    pop si
    cmp byte [cr_btrec+OS88UI_BT_N], 0 ; ...and the About card LAST
    jne .out
    mov bx, si
    mov si, cr_ablines
    call os88ui_about_d             ; the paint's region is armed; it keeps
    mov si, bx                      ; every register
.out:
    ret

; =============================================================================
; CALLBACKS
; =============================================================================

; -----------------------------------------------------------------------------
; cr_tick - W_ONTIMER: re-arm, ask the drive, redraw what moved
; in:  SI = the window; the UI task, gfx lock held, no clip armed
; -----------------------------------------------------------------------------
cr_tick:
    push si                         ; the window, given back (SPEC.md 111.6)
    mov bx, si
    mov ax, CR_POLL                 ; one-shot: the repeat is ours to ask for
    call OSAPI_WM_TIMER
    cmp byte [cr_argwant], 0
    je .poll
    call cr_arg                     ; a TRACKnn.CDA to play, once
    jmp short .drawn
.poll:
    call cr_poll
.drawn:
    pop bx
    push bx
    call cr_redraw
    pop si
    ret

; -----------------------------------------------------------------------------
; cr_arg - the document we were opened on: its track, played (SPEC.md
;          119.3.5). clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
cr_arg:
    mov byte [cr_argwant], 0
    call cr_disc                    ; the disc first: is there an audio track?
    cmp byte [cr_state], CPS_AUDIO
    jne .out
    mov dx, [cr_argdir]             ; ...then the file, from its own folder:
    mov bl, [cr_argvol]             ; GOTO_QM, the quiet stand that MOVES us -
    call OSAPI_FILE_GOTO_QM         ; GOTO_Q leaves our folder where it was, and
    jc .out                         ; a name then resolves there (SPEC.md 19.2.2)
    push es
    push ds
    pop es
    mov si, cr_argname
    mov bx, cr_cdabuf
    xor dx, dx
    mov cx, 44
    call OSAPI_FILE_READ            ; ES:BX = ours
    pop es
    jc .out
    cmp word [cr_cdabuf+8], 'CD'    ; 'CDDA' at 8: a CD audio track's file
    jne .out
    cmp word [cr_cdabuf+10], 'DA'
    jne .out
    mov al, [cr_cdabuf+22]          ; the track
    call cr_isaudt
    jnc .out
    xor cx, cx
    call cr_playt
    jmp cr_poll
.out:
    ret

; -----------------------------------------------------------------------------
; cr_ondrag / cr_onup - the gesture (SPEC.md 13.8): the volume slider's drag
;                       is ours, a button's is the library's and fires on the
;                       RELEASE
; in:  CX, DX = the pointer (screen), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
cr_ondrag:
    cmp byte [cr_drag], 0
    je .btn
    push si
    mov bx, si
    call cr_origin
    sub cx, [cr_ox]
    call cr_volat                   ; [cr_vol] from x
    call cr_redraw
    pop si
    ret
.btn:
    mov bx, cr_btrec
    jmp os88ui_btndrag

cr_onup:
    cmp byte [cr_drag], 0
    je .btn
    mov byte [cr_drag], 0           ; the slider let go: the drive is told
    push si
    call cr_volset
    pop bx
    push bx
    call cr_redraw
    pop si
    ret
.btn:
    mov bx, cr_btrec
    call os88ui_btnup               ; AX = what FIRED, index + 1; 0 = cancelled
    dec ax
    js .out
    call cr_bok                     ; a greyed control explains itself (47 r6)
    jc .out
    push si
    mov bx, si
    call cr_act
    pop bx
    push bx
    call cr_redraw
    pop si                          ; the window, given back (SPEC.md 111.6)
.out:
    ret

; cr_volat - CX = a content x -> [cr_vol], the slider's value there. clobbers
; AX, CX, DX, SI
cr_volat:
    mov si, [cr_lp]
    sub cx, [si+L_SL]
    sub cx, CR_THUMB / 2
    jns .pos
    xor cx, cx
.pos:
    mov ax, 100
    mul cx
    mov cx, [si+L_SL+4]
    sub cx, [si+L_SL]
    sub cx, CR_THUMB - 1
    div cx
    cmp ax, 100
    jbe .ok
    mov ax, 100
.ok:
    mov [cr_vol], al
    ret

; cr_volset - tell the drive [cr_vol], both channels. clobbers AX, BX, CX, DX
cr_volset:
    mov al, [cr_vol]
    mov ah, 255
    mul ah
    mov cl, 100
    div cl
    mov cl, al
    mov ch, al
    mov al, 1
    CR_CALL CDV_VOLUME
    ret

; -----------------------------------------------------------------------------
; cr_onclick - a press that was not a button's: the About card's dismissal,
;              the grid, the list, its scroll bar, the slider, the progress bar
; in:  CX, DX = the pointer (screen), SI = the window
; out: CF = 1 the press was ours
; -----------------------------------------------------------------------------
cr_onclick:
    cmp byte [cr_btrec+OS88UI_BT_N], 0
    jne .face
    call cr_dismiss
    stc
    ret
.face:
    push si
    mov bx, si
    call cr_origin
    sub cx, [cr_ox]
    sub dx, [cr_oy]
    call cr_hit
    pop si
    jc .not
    push si
    mov bx, si
    call cr_redraw
    pop si
    stc
    ret
.not:
    clc
    ret

; cr_hit - CX, DX = a content point: act on it. CF = 1 nothing there.
; clobbers AX, BX, CX, DX, SI, DI
cr_hit:
    mov si, [cr_lp]                 ; the slider (a row more each way)
    mov ax, [si+L_SL+2]
    sub ax, 2
    cmp dx, ax
    jl .nsl
    mov ax, [si+L_SL+6]
    add ax, 2
    cmp dx, ax
    jg .nsl
    cmp cx, [si+L_SL]
    jl .nsl
    cmp cx, [si+L_SL+4]
    jg .nsl
    cmp byte [cr_volok], 0
    je .yes
    mov byte [cr_drag], 1
    call cr_volat
    jmp .yes
.nsl:
    lea si, [si+L_PB]               ; the progress bar: a seek
    call cr_in
    jc .npb
    call cr_going
    jne .yes
    mov ax, cx
    sub ax, [si]
    mov bx, [si+4]
    sub bx, [si]
    inc bx
    mul word [cr_tlen]
    div bx
    mov cx, ax
    mov al, [cr_sel]
    call cr_playt
    jmp .yes
.npb:
    mov si, [cr_lp]
    cmp byte [si+L_CMP], 0
    jne .no                         ; compact: nothing else
    mov si, cr_rgrid                ; the grid
    call cr_in
    jc .ngrid
    mov ax, cx
    sub ax, CF_GX
    mov bl, CF_GCW
    div bl
    xor ah, ah
    mov cx, ax
    cmp dx, CF_GY + CF_GCH
    jl .row1
    add cx, 6
.row1:
    call cr_cellt
    or al, al
    jz .yes
    call cr_pick
    jmp short .yes
.ngrid:
    cmp word [cr_lp], cr_lfull
    jne .no
    mov si, cr_rrows                ; the list's rows
    call cr_in
    jc .nrow
    mov ax, dx
    sub ax, CF_LY0
    mov bl, CF_LROW
    div bl
    add al, [cr_ltop]
    cmp al, [cr_last]
    ja .yes
    call cr_pick
    jmp short .yes
.nrow:
    mov si, cr_rsbar                ; its scroll bar: an arrow is a row, the
    call cr_in                      ; trough a page
    jc .no
    mov ax, -1
    cmp dx, CF_SBT1
    jl .scr
    mov ax, 1
    cmp dx, CF_SBT2
    jg .scr
    mov ax, -CF_LN
    cmp dx, (CF_SBT1 + CF_SBT2) / 2
    jl .scr
    mov ax, CF_LN
.scr:
    call cr_scroll
.yes:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cr_wheel - the wheel: the list on the full player, the volume elsewhere
; in:  AX = signed notches, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
cr_wheel:
    push si
    cmp word [cr_lp], cr_lfull
    jne .vol
    mov cx, WHEEL_STEPS
    imul cx
    call cr_scroll
    jmp short .draw
.vol:
    neg ax                          ; a notch toward the user is quieter
    call cr_volstep
    jc .out
.draw:
    mov bx, si
    call cr_redraw
.out:
    pop si
    ret

; cr_volstep - the volume AX (signed) twentieths up, told to the drive.
; CF = 1: the drive has no volume to set. clobbers AX, BX, CX, DX
cr_volstep:
    cmp byte [cr_volok], 0
    je .no
    mov cx, 5
    imul cx
    mov cl, [cr_vol]
    xor ch, ch
    add ax, cx
    jns .lo
    xor ax, ax
.lo:
    cmp ax, 100
    jbe .hi
    mov ax, 100
.hi:
    mov [cr_vol], al
    call cr_volset
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cr_key - W_ONKEY
;   Space play / pause     S stop        E eject        Left/Up previous
;   Right/Down next        , rewind      . fast forward + - volume
;   R repeat   H shuffle   P program     T time         C compact / full
; in:  AL = ASCII, AH = scan, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
cr_key:
    push si
    cmp byte [cr_btrec+OS88UI_BT_N], 0
    jne .keys
    call cr_dismiss                 ; any key takes the About card down
    jmp .out
.keys:
    mov bx, cr_keytab
.find:
    cmp byte [bx], 0
    je .other
    cmp ah, [bx]                    ; a scan code, or...
    je .hit
    cmp byte [bx], 0x80
    jb .next
    mov dl, al                      ; ...an ASCII one, any case
    and dl, 0xDF
    mov dh, [bx]
    and dh, 0x7F
    cmp dl, dh
    je .hit
.next:
    add bx, 2
    jmp short .find
.hit:
    mov al, [bx+1]
    cmp al, 0xFF
    jne .do
    mov al, B_PAUSE                 ; Space: pause what plays, else play
    cmp byte [cr_play], CDS_PLAYING
    je .do
    mov al, B_PLAY
.do:
    call cr_bok
    jc .out
    pop bx
    push bx
    call cr_act
    jmp short .draw
.other:
    cmp al, '+'                     ; the volume, a tenth at a time
    je .louder
    cmp al, '='
    je .louder
    cmp al, '-'
    jne .layout
    mov ax, -2
    jmp short .vol
.louder:
    mov ax, 2
.vol:
    call cr_volstep
    jc .out
    jmp short .draw
.layout:
    and al, 0xDF
    cmp al, 'C'
    jne .out                        ; every other key - the F keys, Alt+Enter
    call cr_canbig                  ; - is not ours
    jc .out
    pop bx
    push bx
    call cr_relayout
    jmp short .out
.draw:
    pop bx
    push bx
    call cr_redraw
.out:
    pop si
    ret

; =============================================================================
; THE MENUS (SPEC.md 12.2) - About is the application menu's own
; =============================================================================

; -----------------------------------------------------------------------------
; cr_menu - a menu item: AH = the menu, AL = the item, SI = the window
; -----------------------------------------------------------------------------
cr_menu:
    push si
    call cr_menu1
    pop si
    ret

cr_menu1:
    mov bx, si
    cmp ah, 1
    je .playback
    ja .options
    or al, al                       ; Disc
    jnz .close
    mov al, B_EJECT
    jmp short .btn
.close:
    cmp byte [cr_state], CPS_NODRV
    je .out
    push bx
    mov al, 1
    CR_CALL CDV_EJECT
    call cr_poll
    pop bx
    jmp short .draw
.playback:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [cr_pbtab+bx]
    pop bx
    cmp al, 0xFF
    jne .btn
    mov al, B_PAUSE                 ; Play/Pause
    cmp byte [cr_play], CDS_PLAYING
    je .btn
    mov al, B_PLAY
.btn:
    call cr_bok
    jc .out
    push bx
    call cr_act
    pop bx
    jmp short .draw
.options:
    cmp al, 4
    jb .toggle
    je .lay
    cmp byte [cr_bpp1], 0           ; 5: the theme, which a 1bpp display has
    jne .out                        ; only one of
    xor byte [cr_theme], 1
    call OSAPI_WM_CLIP_SET          ; a theme is every pixel: the whole paint
    jc .out
    mov si, bx
    jmp cr_paint
.lay:
    call cr_canbig
    jc .out
    jmp cr_relayout
.toggle:
    add al, B_REPEAT
    jmp short .btn
.draw:
    call cr_redraw
.out:
    ret

; -----------------------------------------------------------------------------
; cr_menulive - every item's greying and every check, from the same
;               predicates the buttons use (SPEC.md 47 rule 2). The kernel
;               reads the strings when a menu opens, so writing them is enough.
; clobbers AX, BX, SI
; -----------------------------------------------------------------------------
cr_menulive:
    mov al, B_EJECT                 ; Disc: both items want a drive
    mov bx, cr_i_disc
    call cr_mlive
    add bx, 2
    call cr_mlive
    mov bx, cr_i_play               ; Playback: its buttons'
    xor si, si
.pb:
    mov al, [cr_pbtab+si]
    cmp al, 0xFF
    jne .pb1
    mov al, B_PLAY
.pb1:
    call cr_mlive
    add bx, 2
    inc si
    cmp si, CR_NPB
    jb .pb
    xor bx, bx                      ; Options: each toggle names its state
.opt:
    mov al, [cr_opts+bx]
    xor ah, ah
    mov si, bx
    shl si, 1
    add si, ax
    shl si, 1
    mov ax, [cr_mopt+si]
    mov si, bx
    shl si, 1
    mov [cr_i_opt+si], ax
    inc bx
    cmp bx, 4
    jb .opt
    mov ax, cr_o_cmp                ; the other player, greyed where the big
    mov bx, [cr_lp]                 ; one does not fit
    cmp byte [bx+L_CMP], 0
    je .big
    mov ax, cr_o_full
.big:
    mov [cr_i_opt+8], ax
    mov al, B_EXPAND
    mov bx, cr_i_opt + 8
    call cr_mlive
    mov ax, cr_o_col                ; the theme, greyed on a 1bpp display
    cmp word [cr_th], cr_tcol
    je .th
    mov ax, cr_o_bw
.th:
    cmp byte [cr_bpp1], 0
    je .thl
    dec ax
.thl:
    mov [cr_i_opt+10], ax
    ret

; cr_mlive - BX = an item's slot: it points at the live string, or at the
; MENU_DIS byte laid out in front of it (the greyed form). Live unless button
; AL is refused. keeps everything
cr_mlive:
    push si
    mov si, [bx]
    cmp byte [si], MENU_DIS
    jne .live
    inc si
.live:
    call cr_bok
    jnc .set
    dec si
.set:
    mov [bx], si
    pop si
    ret

; =============================================================================
; 'About CDRack' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; While the card is up the button record is told it has NO buttons, so a
; press anywhere in the content reaches cr_onclick and takes the card down -
; and that zero IS the card's state (Timer's idiom, SPEC.md 111.6).
cr_about:
    mov byte [cr_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, cr_ablines
    jmp os88ui_about

; cr_dismiss - put the buttons back and repaint the content. SI = the window
cr_dismiss:
    mov byte [cr_btrec+OS88UI_BT_N], CR_NB
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call cr_paint                   ; SI = the window still; keeps it
.out:
    ret

; =============================================================================
; TEXT HELPERS
; =============================================================================

; cr_put - copy a NUL string at SI to DI, NUL not copied; DI past it.
; clobbers SI (keeps AX)
cr_put:
    push ax
.c:
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    jmp short .c
.out:
    pop ax
    ret

; cr_2dig - AL (< 100) as two digits at DI; DI past them. clobbers AX
cr_2dig:
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    inc di
    inc di
    ret

; cr_num - AL (< 100) as one digit or two at DI; DI past them. clobbers AX
cr_num:
    cmp al, 10
    jae cr_2dig
    add al, '0'
    mov [di], al
    inc di
    ret

; cr_mmss - AX seconds as 'mm:ss ' at DI (minutes to 99); DI past it.
; clobbers AX
cr_mmss:
    push cx
    push dx
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    cmp ax, 99
    jbe .m
    mov ax, 99
.m:
    call cr_2dig
    mov byte [di], ':'
    inc di
    mov ax, dx
    call cr_2dig
    mov byte [di], ' '
    inc di
    pop dx
    pop cx
    ret

; cr_fillb - CX bytes of AL at DI. keeps everything
cr_fillb:
    push cx
    push di
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    pop di
    pop cx
    ret

%include "crdraw.inc"
%include "crface.inc"

; =============================================================================
; DATA
; =============================================================================
cr_tpl:     dw 12, 28, CC_W + 2, C1_H + TITLE_H + 1
            dw cr_ttl, cr_paint, cr_key, 0
cr_ttl      equ 16                  ; the header's name field: 'CDRack'

cr_pref:    times 6 dw 0            ; OSAPI_WM_PREFER's table (cr_prefset)

; --- the themes: a byte a role (crdraw.inc) -----------------------------------------
cr_tcol:                            ; COLOUR - the dark deck of the mockup
    db CDGRAY, CBLACK, CLGRAY, CLGRAY, CLGRAY       ; BG PANEL EDGE LOGO LABEL
    db CBLACK, CDGRAY, CLCYAN, CCYAN, CCYAN         ; LCD ... LCDDIM
    db CLCYAN, CYELLOW                              ; LCDSEL LCDPRG
    db CDGRAY, CLGRAY, CWHITE, CLGRAY               ; BTN EDGE TXT DIS
    db CCYAN, CLCYAN, CWHITE                        ; BTNON ONE ONT
    db CDGRAY, CDGRAY, CLGRAY                       ; SLOT SLOTLBL SLOTTXT
    db CCYAN, CDGRAY, CLGRAY                        ; SLFILL SLREST THUMB
    db CBLACK, CLGRAY, CCYAN, CWHITE, CDGRAY        ; LIST LISTTXT SEL SELTXT
                                                    ; TROUGH
    db CBLACK, CLGRAY, CBLACK, CWHITE               ; STAT STATTXT HDR TEXT
    db CDGRAY, CCYAN                                ; PROGBG PROGFG
cr_tbw:                             ; BLACK AND WHITE - every display's
    db CWHITE, CWHITE, CBLACK, CBLACK, CBLACK
    db CBLACK, CBLACK, CWHITE, CWHITE, CWHITE
    db CWHITE, CWHITE
    db CWHITE, CBLACK, CBLACK, CBLACK
    db CBLACK, CBLACK, CWHITE
    db P_DITHER, CWHITE, CBLACK
    db P_VSTR, P_DITHER, CWHITE
    db CWHITE, CBLACK, CBLACK, CWHITE, P_DITHER
    db CWHITE, CBLACK, CWHITE, CBLACK
    db CBLACK, P_VSTR
%if (cr_tbw - cr_tcol) != CR_NROLES || ($ - cr_tbw) != CR_NROLES
%error "CDRack's theme tables must have a byte for every role"
%endif

; --- the digit sets -------------------------------------------------------------------
    CRDSET dl, DL, 4
    CRDSET cb, CB, 3
    CRDSET cs, CS, 2
    CRDSET cb2, CB2, 3
    CRDSET cs2, CS2, 2

; --- the layouts (crface.inc's L_*) ---------------------------------------------------
cr_lfull:
    dw CF_W, CF_H, cr_d_full, cr_efull, cr_bfull, cr_ifull, cr_cfull
    dw cdart_check, cdart_uncheck, cr_ds_dl
    dw 226, 68, 296, 68, 228, 106
    dw 273, 127, 534, 130, R_PROGFG, R_PROGBG
    dw 421, 165, 594, 174
    dw 336
    times 4 dw 0
    times 2 dw 0
    times 3 dw 0
    times 3 dw 0
    times 2 dw 0
    times 3 dw 0
    times 2 dw 0
    dw 0
cr_lmid:
    dw CF_W, CM_H, cr_d_mid, cr_emid, cr_bfull, cr_ifull, cr_cfull
    dw cdart_check, cdart_uncheck, cr_ds_dl
    dw 226, 68, 296, 68, 228, 106
    dw 273, 127, 534, 130, R_PROGFG, R_PROGBG
    dw 421, 165, 594, 174
    dw 216
    times 4 dw 0
    times 2 dw 0
    times 3 dw 0
    times 3 dw 0
    times 2 dw 0
    times 3 dw 0
    times 2 dw 0
    dw 0
cr_lc1:                             ; COMPACT on the CGA: 7-row pictures
    dw CC_W, C1_H, cr_d_cmp, cr_ecmp, cr_bc1, cr_ic1, cr_cc1
    dw cdart_h_check, cdart_h_uncheck, cr_ds_cb
    dw 8, 13, 70, 13, 182, 26
    dw 3, 41, 436, 46, R_SLFILL, R_SLREST
    dw 29, 73, 189, 81
    dw 0
    dw 2, 2, 437, 37                ; the LCD
    dw 16, 4                        ; 'TRACK'
    dw 62, 5, 34                    ; the divider
    dw 182, 6, cr_ds_cs             ; '/ 04:18'
    dw 380, 26                      ; '03 OF 12'
    dw 8, 74, cdart_h_speaker       ; the speaker
    dw 72, 83                       ; the bottom row
    dw 1
cr_lc2:                             ; COMPACT, rows doubled
    dw CC_W, C2_H, cr_d_cmp, cr_ecmp, cr_bc2, cr_ic2, cr_cc1
    dw cdart_check, cdart_uncheck, cr_ds_cb2
    dw 8, 22, 70, 22, 192, 54
    dw 3, 81, 436, 92, R_SLFILL, R_SLREST
    dw 29, 147, 189, 160
    dw 0
    dw 2, 4, 437, 75
    dw 16, 10
    dw 62, 9, 70
    dw 192, 14, cr_ds_cs2
    dw 380, 54
    dw 8, 147, cdart_speaker
    dw 142, 165
    dw 1
%if (cr_lmid - cr_lfull) != L_CMP + 2 || (cr_lc1 - cr_lmid) != L_CMP + 2
%error "a CDRack layout record is L_CMP + 2 bytes"
%endif
%if (cr_lc2 - cr_lc1) != L_CMP + 2
%error "a CDRack layout record is L_CMP + 2 bytes"
%endif

; the elements, (proc, parameter) - crface.inc
cr_efull:
    dw cr_e_trk, 0, cr_e_tlbl, 0, cr_e_time, 0
%assign i 0
%rep 12
    dw cr_e_cell, i
%assign i i+1
%endrep
    dw cr_e_stat, 0, cr_e_stereo, 0, cr_e_ptxt, 0, cr_e_pbar, 0
    dw cr_e_ptxt, 1, cr_e_vol, 0, cr_e_slider, 0, cr_e_tray, 0
    dw cr_e_linfo, 0
%assign i 0
%rep CF_LN
    dw cr_e_row, i
%assign i i+1
%endrep
    dw cr_e_sbar, 0, cr_e_sbtxt, 0
    dw 0
cr_emid:
    dw cr_e_trk, 0, cr_e_tlbl, 0, cr_e_time, 0
%assign i 0
%rep 12
    dw cr_e_cell, i
%assign i i+1
%endrep
    dw cr_e_stat, 0, cr_e_stereo, 0, cr_e_ptxt, 0, cr_e_pbar, 0
    dw cr_e_ptxt, 1, cr_e_vol, 0, cr_e_slider, 0, cr_e_tray, 0
    dw cr_e_sbtxt, 0
    dw 0
cr_ecmp:
    dw cr_e_trk, 0, cr_e_time, 0, cr_e_ctot, 0, cr_e_stat, 0
    dw cr_e_cof, 0, cr_e_pbar, 0, cr_e_slider, 0
    dw 0
%if (cr_emid - cr_efull) / 4 - 1 > CR_NKEYS
%error "CR_NKEYS is short of the full player's elements"
%endif

; the button rects, content-relative and inclusive, B_* order
%macro CRBTN 4
    dw %1, %2, %1 + %3 - 1, %2 + %4 - 1
%endmacro
%macro CRPARK 0
    dw CR_PARK, CR_PARK, CR_PARK, CR_PARK
%endmacro
cr_bfull:
%assign i 0
%rep 7
    CRBTN 12 + i * 45, 161, 42, 46
%assign i i+1
%endrep
    CRBTN 148, 100, 52, 40          ; Eject, in the tray
    CRBTN 340, 184, 62, 20          ; the four toggles
    CRBTN 406, 184, 62, 20
    CRBTN 472, 184, 62, 20
    CRBTN 538, 184, 58, 20
    CRPARK                          ; no Expand: this IS the full player
cr_bc1:
%assign i 0
%rep 8
    CRBTN 6 + i * 54, 51, 50, 18
%assign i i+1
%endrep
    CRBTN 204, 72, 96, 12           ; Repeat
    CRPARK
    CRPARK
    CRPARK
    CRBTN 314, 72, 120, 12          ; Expand
cr_bc2:
%assign i 0
%rep 8
    CRBTN 6 + i * 54, 100, 50, 36
%assign i i+1
%endrep
    CRBTN 204, 143, 96, 22
    CRPARK
    CRPARK
    CRPARK
    CRBTN 314, 143, 120, 22

; the pictures and captions, B_* order (a toggle's picture is its box)
cr_ifull:
    dw cdart_i_prev, cdart_i_rew, cdart_i_play, cdart_i_pause
    dw cdart_i_stop, cdart_i_ff, cdart_i_next, cdart_i_eject
    dw 0, 0, 0, 0, cdart_expand
cr_ic2:
    dw cdart_i_prev, cdart_i_rew, cdart_i_play, cdart_i_pause
    dw cdart_i_stop, cdart_i_ff, cdart_i_next, cdart_i_eject
    dw 0, 0, 0, 0, cdart_expand
cr_ic1:
    dw cdart_h_prev, cdart_h_rew, cdart_h_play, cdart_h_pause
    dw cdart_h_stop, cdart_h_ff, cdart_h_next, cdart_h_eject
    dw 0, 0, 0, 0, cdart_h_expand
cr_cfull:
    dw cr_s_bprev, cr_s_brew, cr_s_bplay, cr_s_bpause, cr_s_bstop
    dw cr_s_bff, cr_s_bnext, cr_s_beject
    dw cr_s_brep, cr_s_bshuf, cr_s_bprog, cr_s_btime, cr_s_bexp
cr_cc1:
    dw 0, 0, 0, 0, 0, 0, 0, 0
    dw cr_s_brep, cr_s_bshuf, cr_s_bprog, cr_s_btime, cr_s_bexp

    OS88UI_BTNREC cr_btrec, cr_rects, cr_blbl, cr_bflags, CR_NB
cr_blbl:    times CR_NB dw cr_bpaint    ; every button is CDRack's to paint

; hit rects (content) for the full player's grid, list rows and scroll bar
cr_rgrid:   dw CF_GX, CF_GY, CF_GX + 6 * CF_GCW - 1, CF_GY + 2 * CF_GCH - 1
cr_rrows:   dw CF_LX1, CF_LY0, CF_SBX - 1, CF_LY0 + CF_LN * CF_LROW - 1
cr_rsbar:   dw CF_SBX, CF_LY0, CF_SBX + 11, CF_LY0 + CF_LN * CF_LROW - 1

; the keys: a scan code, or 80h + an upper-case ASCII; then the button, FF =
; Space's play-or-pause
cr_keytab:
    db 0x39, 0xFF                   ; Space
    db 0x4B, B_PREV, 0x48, B_PREV   ; Left, Up
    db 0x4D, B_NEXT, 0x50, B_NEXT   ; Right, Down
    db 0x80 | 'S', B_STOP, 0x80 | 'E', B_EJECT
    db 0x80 | (',' & 0xDF), B_REW, 0x80 | ('.' & 0xDF), B_FF
    db 0x80 | 'R', B_REPEAT, 0x80 | 'H', B_SHUFFLE
    db 0x80 | 'P', B_PROGRAM, 0x80 | 'T', B_TIME
    db 0

; --- the menus -------------------------------------------------------------------------
    OS88_MENUSET cr_menus, cr_name, cr_menu
        OS88_MENU cr_m_disc, cr_i_disc, 2
        OS88_MENU cr_m_play, cr_i_play, 6
        OS88_MENU cr_m_opt, cr_i_opt, 6
    OS88_MENUSET_END cr_menus
cr_name:    db 'CDRack', 0
cr_m_disc:  db 'Disc', 0
cr_m_play:  db 'Playback', 0
cr_m_opt:   db 'Options', 0
cr_i_disc:  dw cr_s_ej, cr_s_close
cr_i_play:  dw cr_s_pp, cr_s_st, cr_s_pv, cr_s_nx, cr_s_rw, cr_s_fw
CR_NPB      equ ($ - cr_i_play) / 2
%if CR_NPB != 6
%error "the Playback menu's count is written as 6 in cr_menus"
%endif
cr_i_opt:   dw cr_o_rep0, cr_o_shf0, cr_o_prg0, cr_o_tim0, cr_o_cmp, cr_o_col
cr_pbtab:   db 0xFF, B_STOP, B_PREV, B_NEXT, B_REW, B_FF  ; Playback's buttons
            db MENU_DIS
cr_s_ej:    db 'Eject  E', 0
            db MENU_DIS
cr_s_close: db 'Close Tray', 0
            db MENU_DIS
cr_s_pp:    db 'Play/Pause  Space', 0
            db MENU_DIS
cr_s_st:    db 'Stop  S', 0
            db MENU_DIS
cr_s_pv:    db 'Previous Track', 0
            db MENU_DIS
cr_s_nx:    db 'Next Track', 0
            db MENU_DIS
cr_s_rw:    db 'Rewind  ,', 0
            db MENU_DIS
cr_s_fw:    db 'Fast Forward  .', 0
cr_mopt:    dw cr_o_rep0, cr_o_rep1     ; the toggles' items, off and on: an
            dw cr_o_shf0, cr_o_shf1     ; app menu has no check mark, so each
            dw cr_o_prg0, cr_o_prg1     ; names its state (SPEC.md 119.6.6)
            dw cr_o_tim0, cr_o_tim1
cr_o_rep0:  db 'Repeat: Off  R', 0
cr_o_rep1:  db 'Repeat: On  R', 0
cr_o_shf0:  db 'Shuffle: Off  H', 0
cr_o_shf1:  db 'Shuffle: On  H', 0
cr_o_prg0:  db 'Program: Off  P', 0
cr_o_prg1:  db 'Program: On  P', 0
cr_o_tim0:  db 'Time: Elapsed  T', 0
cr_o_tim1:  db 'Time: Remaining  T', 0
            db MENU_DIS
cr_o_cmp:   db 'Compact Player  C', 0
            db MENU_DIS
cr_o_full:  db 'Full Player  C', 0
            db MENU_DIS
cr_o_col:   db 'Theme: Color', 0
            db MENU_DIS
cr_o_bw:    db 'Theme: Black and White', 0

; --- strings ---------------------------------------------------------------------------
cr_s_tag:     db 'COMPACT DISC PLAYER', 0
cr_s_track:   db 'TRACK', 0
cr_s_elapsed: db 'ELAPSED', 0
cr_s_remain:  db 'REMAIN', 0
cr_s_stereo:  db 'STEREO', 0
cr_s_khz:     db '44.1 KHZ', 0
cr_s_slash:   db '/', 0
cr_s_of:      db ' OF ', 0
cr_s_ltitle:  db 'DISC TRACKS', 0
cr_s_htrack:  db 'TRACK', 0
cr_s_htitle:  db 'TITLE', 0
cr_s_htime:   db 'TIME', 0
cr_s_up:      db 0x1E, 0
cr_s_down:    db 0x1F, 0
cr_s_os:      db 'os8088', 0
cr_s_tracks:  db ' tracks  ', 0
cr_s_ltrk:    db 'Track ', 0
cr_s_ldata:   db 'Data track ', 0
cr_s_drive:   db 'Drive ', 0
cr_s_cdrom:   db 'CD-ROM:', 0
cr_s_bprev:   db 'PREV', 0
cr_s_brew:    db 'REW', 0
cr_s_bplay:   db 'PLAY', 0
cr_s_bpause:  db 'PAUSE', 0
cr_s_bstop:   db 'STOP', 0
cr_s_bff:     db 'FF', 0
cr_s_bnext:   db 'NEXT', 0
cr_s_beject:  db 'EJECT', 0
cr_s_brep:    db 'REPEAT', 0
cr_s_bshuf:   db 'SHUFFLE', 0
cr_s_bprog:   db 'PROGRAM', 0
cr_s_btime:   db 'TIME', 0
cr_s_bexp:    db 'EXPAND', 0

; the LCD's words, by state (CPS_*) and by play (CDS_*)
cr_lsmsg:   dw cr_l_nodrv, cr_l_nodisc, cr_l_data, cr_l_stop, cr_l_noaud
            dw cr_l_err
cr_lpmsg:   dw cr_l_stop, cr_l_play, cr_l_pause, cr_l_stop, cr_l_err
cr_l_nodrv:  db 'NO DRIVE', 0
cr_l_nodisc: db 'NO DISC', 0
cr_l_data:   db 'DATA DISC', 0
cr_l_noaud:  db 'NO AUDIO', 0
cr_l_err:    db 'DRIVE ERROR', 0
cr_l_stop:   db 'STOPPED', 0
cr_l_play:   db 'PLAYING', 0
cr_l_pause:  db 'PAUSED', 0
; the tray's label, by state
cr_ltray:   dw cr_l_nodrv, cr_l_nodisc, cr_t_data, cr_t_audio, cr_t_audio
            dw cr_l_err
cr_t_data:   db 'DATA CD', 0
cr_t_audio:  db 'AUDIO CD', 0
; the status bar's sentence, by state and by play
cr_bsmsg:   dw cr_b_nodrv, cr_b_nodisc, cr_b_data, cr_b_ready, cr_b_noaud
            dw cr_b_err
cr_bpmsg:   dw cr_b_ready, cr_b_play, cr_b_pause, cr_b_ready, cr_b_perr
cr_b_nodrv:  db 'CD-ROM driver not loaded', 0
cr_b_nodisc: db 'No disc', 0
cr_b_data:   db 'Data disc - no audio tracks', 0
cr_b_ready:  db 'Audio CD ready', 0
cr_b_noaud:  db 'This drive plays no audio', 0
cr_b_err:    db 'The drive does not answer', 0
cr_b_play:   db 'Playing track ', 0
cr_b_pause:  db 'Paused at track ', 0
cr_b_perr:   db 'Drive error', 0
%if CPS_NODRV != 0 || CPS_NODISC != 1 || CPS_DATA != 2 || CPS_AUDIO != 3 || CPS_NOAUD != 4 || CPS_ERR != 5
%error "the CPS_* tables are indexed by state"
%endif
%if CDS_IDLE != 0 || CDS_PLAYING != 1 || CDS_PAUSED != 2 || CDS_DONE != 3 || CDS_ERROR != 4
%error "the CDS_* tables are indexed by play"
%endif

cr_ablines:
    dw cr_ab1, cr_ab2, cr_ab3, 0
cr_ab1:     db 'CDRack for os8088', 0
cr_ab2:     db 'plays audio CDs in the', 0
cr_ab3:     db 'CD-ROM drive (CDROM.DRV)', 0

%include "cdart.inc"

%define OS88UI_ABOUT
%define OS88UI_NOGLYPH
%define OS88UI_BOWN
%include "os88ui.inc"

CR_SETSZ    equ 13                  ; a set of tracks: 104 bits, 0..99 used

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------------
CRB_SIZE    equ 158 + CR_NB * 10 + CR_NKEYS * 2 + 200 + 72 + 44 + CR_BANDSZ
    OS88_BSS CRB_SIZE
    OS88_IMAGE_END

%define CRB(off) (os88_image_end + (off))
cr_state    equ CRB(0)              ; CPS_*
cr_play     equ CRB(1)              ; CDS_*, the last status
cr_first    equ CRB(2)              ; the disc's first and last track
cr_last     equ CRB(3)
cr_gen      equ CRB(4)              ; the media generation the TOC came from
cr_sel      equ CRB(5)              ; the track shown, played next
cr_argwant  equ CRB(6)              ; 1 = a .CDA to play at the first poll
cr_argvol   equ CRB(7)
cr_dlen     equ CRB(8)              ; word: the disc, in seconds
cr_tlen     equ CRB(10)             ; word: the shown track, in seconds
cr_secs     equ CRB(12)             ; word: seconds into it
cr_argdir   equ CRB(14)             ; word
cr_argname  equ CRB(16)             ; 14
cr_ours     equ CRB(30)             ; 1 = a play of ours is under way
cr_opts     equ CRB(31)             ; the four toggles, B_REPEAT's order:
cr_repeat   equ cr_opts + 0
cr_shuf     equ cr_opts + 1
cr_progm    equ cr_opts + 2
cr_time     equ cr_opts + 3
cr_vol      equ CRB(35)             ; 0..100
cr_volok    equ CRB(36)             ; 1 = the drive took CDV_VOLUME
cr_letter   equ CRB(37)             ; the CD's volume letter, 0 = none
cr_ltop     equ CRB(38)             ; the list's top track
cr_lsel     equ CRB(39)             ; the selection the list last followed
cr_drag     equ CRB(40)             ; 1 = the volume slider is held
cr_want     equ CRB(41)             ; 1 = the user asked for the compact one
cr_theme    equ CRB(42)             ; 0 colour, 1 black and white (the choice)
cr_bpp1     equ CRB(43)             ; 1 = the window's display is one bit
cr_cutf     equ CRB(44)             ; crdraw.inc: the clip cut what was drawn
cr_bdim     equ CRB(45)             ; crdraw.inc: grey the next band's ink
cr_cgap     equ CRB(46)             ; crdraw.inc: the small caps' tracking
cr_ctr      equ CRB(47)             ; crdraw.inc: centre cr_fcaps' text
cr_mark     equ CRB(48)             ; crface.inc: the play mark goes with it
cr_seed     equ CRB(50)             ; word: the shuffle's generator
cr_win      equ CRB(52)             ; word: our window
cr_pband    equ CRB(54)             ; word: the primary's desktop band
cr_dband    equ CRB(56)             ; word: the window's display's band...
cr_dwid     equ CRB(58)             ; word: ...and width
cr_ox       equ CRB(60)             ; word: the content origin, this pass
cr_oy       equ CRB(62)
cr_cw       equ CRB(64)             ; word: the content box, this layout
cr_ch       equ CRB(66)
cr_lp       equ CRB(68)             ; word: the layout record
cr_th       equ CRB(70)             ; word: the theme table
cr_slot     equ CRB(72)             ; word: the element's key slot
cr_kpend    equ CRB(74)             ; word: its key, pending the draw
cr_sby      equ CRB(76)             ; word: the status bar's top
cr_bw       equ CRB(78)             ; word: the band - its width...
cr_bh       equ CRB(80)             ; ...rows...
cr_bst      equ CRB(82)             ; ...and stride
cr_bbw      equ CRB(84)             ; word: a button's inside
cr_bbh      equ CRB(86)
cr_bpic_    equ CRB(88)             ; word: its picture...
cr_bcap     equ CRB(90)             ; ...its caption...
cr_bink     equ CRB(92)             ; ...and its roles, ink | paper << 8
cr_prog     equ CRB(94)             ; CR_SETSZ: the program
cr_isaud    equ CRB(107)            ; CR_SETSZ: the disc's audio tracks
cr_played   equ CRB(120)            ; CR_SETSZ: the shuffle's played ones
cr_tbuf     equ CRB(134)            ; 24: a short field's text
cr_rects    equ CRB(158)            ; CR_NB x 4 words: the buttons, screen
cr_bflags   equ cr_rects + CR_NB * 8    ; CR_NB words: as drawn
cr_keys     equ cr_bflags + CR_NB * 2   ; CR_NKEYS words: as drawn
cr_tlens    equ cr_keys + CR_NKEYS * 2  ; 100 words: each track's length
cr_lbuf     equ cr_tlens + 200      ; 72: a list row's or the status bar's text
cr_cdabuf   equ cr_lbuf + 72        ; 44: a TRACKnn.CDA's bytes
cr_band     equ cr_cdabuf + 44      ; CR_BANDSZ: the composing band
%if cr_band + CR_BANDSZ - os88_image_end != CRB_SIZE
%error "CDRack's bss layout and OS88_BSS disagree"
%endif
