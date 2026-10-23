; =============================================================================
; os8088 - apps/xtmp3/xtmp3.asm
;
; XTMP3 (SPEC.md 121): an MP3 player that plays every file at the best quality
; this machine and its audio device can sustain, and says which quality that
; is - behind the face of an 80s stereo component, the owner's two mockups in
; docs/xtmp3/design/.
;
; THIS WAVE IS THE FACE (S4, docs/plans/XTMP3-PLAN.md): the window, the four
; layouts in colour and in black and white, the menus, the keys, the queue,
; File > Open and the .MP3 association - working, over a DEMO engine
; (x3demo.inc) that plays a pretend track so the clock, the bar and the
; needles move. The decoders and the real engine arrive behind the same
; boundary: x3_st, the one record the face reads (x3face.inc), and the
; x3_e_* procs the face calls (x3demo.inc says what each one is for).
;
; THE FILES:
;   xtmp3.asm    this: the entry, the commands and their one predicate, the
;                queue, the menus, the keys and the callbacks
;   x3draw.inc   the drawing layer - roles, themes, the band, the canvas
;   x3face.inc   the state record, the four layouts, every element
;   x3demo.inc   the demo engine (S4 only: S8's x3eng.inc replaces it)
;   build/x3art.inc  the pictures and fonts, made on the host by
;                tools/os88x3art.py
;
; REGISTER CONTRACTS are the callbacks' (SPEC.md 111.6): W_PAINT and W_ONKEY
; give SI back; W_ONTIMER and the W_ONCLICK family may clobber AX, BX, CX, DX,
; DI and ES. Internal procs say what they clobber.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'XTMP3', x3_entry, 3
                                    ; flags: bit 0 the icon, bit 1 the
                                    ; association block (SPEC.md 54.6)
    OS88_ICON16                     ; a little stereo deck: an LCD, a knob
    ; --- mask ---
    dw 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE
    ; --- data ---
    dw 0x7FFE, 0x8001, 0xBFFD, 0xA005, 0xAEE5, 0xA005, 0xBFFD, 0x8001
    dw 0x8F01, 0x90BD, 0xA241, 0xA27D, 0x9081, 0x8F7D, 0x8001, 0x7FFE
    OS88_ICON16_END

    OS88_ASSOC16                    ; SPEC.md 121.1: an MP3 opens here
    db 1
    OS88_ASSOC_EXT 'MP3'
    OS88_ASSOC16_END

X3_TICK     equ 5                   ; ticks between passes: the demo's clock,
                                    ; and the needles' 3.6 a second

; --- the commands: the buttons first, in their record's order ------------------
B_REW       equ 0                   ; back 10 s
B_PLAY      equ 1
B_PAUSE     equ 2
B_STOP      equ 3
B_FF        equ 4                   ; forward 10 s
B_OPEN      equ 5
X3_NB       equ 6
C_PREV      equ 6                   ; ...and the rest, which are menu items and
C_NEXT      equ 7                   ; keys only
C_DECODE    equ 8
C_DFILES    equ 9
C_ADD       equ 10
C_REMOVE    equ 11
C_CLEAR     equ 12
C_CANCEL    equ 13
C_THEME     equ 14
C_LAYOUT    equ 15
C_QUAL      equ 16
C_METERS    equ 17
C_STATUS    equ 18
C_UP        equ 19
C_DOWN      equ 20
C_ENTER     equ 21
C_VOLUP     equ 22
C_VOLDN     equ 23
C_PP        equ 24                  ; play / pause: whichever is live
X3_PARK     equ 0x7FF0              ; a button this layout lacks: off every screen

X3_SEEK     equ 10                  ; seconds Rewind and Fast Forward move
X3_BACK     equ 3                   ; seconds in past which Previous restarts

; --- the queue ---------------------------------------------------------------------
X3_QMAX     equ 32
QE_NAME     equ 0                   ; 13: the 8.3 name
QE_VOL      equ 13                  ; byte: its volume
QE_DIR      equ 14                  ; word: its folder's cluster
QE_DUR      equ 16                  ; word: seconds, FFFFh unknown
QE_FLAGS    equ 18                  ; byte: X3SF_EST for an estimated duration
QE_TITLE    equ 19                  ; 41: the title shown (the file's name until
QE_SIZE     equ 60                  ;     the parser, S5, reads its tags)

; --- the theme roles (x3draw.inc): one byte each in x3_tcol / x3_tbw --------------
R_GND       equ 0                   ; the panel - every pixel the face does not
R_EDGE      equ 1                   ; otherwise own; rules
R_HI        equ 2                   ; a bevel's light and shade
R_SH        equ 3
R_LABEL     equ 4                   ; panel lettering, the logo, screws, grilles
R_LCD       equ 5                   ; the LCD's glass...
R_LCDRIM    equ 6
R_LCDTTL    equ 7                   ; ...its title line...
R_LCDTIM    equ 8                   ; ...the seven segments...
R_LCDTOT    equ 9                   ; ...the thin total...
R_LCDFMT    equ 10                  ; ...MONO / STEREO...
R_LCDRATE   equ 11                  ; ...the rate
R_PBFRM     equ 12                  ; the progress bar's rule, done, to go
R_PBFG      equ 13
R_PBBG      equ 14
R_KEY       equ 15                  ; a key: face, ink, greyed ink
R_KEYINK    equ 16
R_KEYDIS    equ 17
R_KEYON     equ 18                  ; ...latched: face and ink
R_KEYONI    equ 19
R_BEZEL     equ 20                  ; the dials' bezels
R_QBOX      equ 21                  ; the queue's rows, the selection
R_QTXT      equ 22
R_QSEL      equ 23
R_QSELT     equ 24
R_TROUGH    equ 25                  ; its scroll bar
R_THUMB     equ 26
R_STAT      equ 27                  ; the status line
R_STATTXT   equ 28
X3_NROLES   equ 29

X3_WORKSZ   equ 2720                ; the band / canvas: the knob's 80 x 68 at
                                    ; 4bpp, the largest thing composed
X3F_NKEYS   equ 24                  ; element keys: FULL's 17, with room
X3F_FACESZ  equ (X3ART_DIALF_W / 2) * X3ART_DIALF_H  ; a colour dial face

; =============================================================================
; ENTRY
; =============================================================================

; -----------------------------------------------------------------------------
; x3_entry - the package entry point (SPEC.md 20.2)
; out: BX = the window, CF clear (set = abort)
; The loader shows the window; nothing here may draw. A document we were
; opened on joins the queue at the first tick.
; -----------------------------------------------------------------------------
x3_entry:
    call OSAPI_ARG_FILE             ; an .MP3? CF = 1: launched empty
    jc .plain
    mov di, x3_argname              ; SI -> its name in KERNEL_SEG: copied out
    mov cx, 13                      ; before anything else is called
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    mov [x3_argdir], dx
    mov [x3_argvol], bl
    mov byte [x3_argwant], 1
.plain:
    call OSAPI_VIDEO                ; colour by default where the primary has
    mov byte [x3_theme], 1          ; more than one bit (SPEC.md 121.8)
    cmp dh, 1
    jbe .bw
    mov byte [x3_theme], 0
.bw:
    sub cx, MBAR_H                  ; its desktop band, for the size table
    mov [x3_pband], cx
    mov byte [x3_meters], 1
    mov byte [x3_qsel], 0xFF
    mov byte [x3_qlsel], 0xFE
    mov word [x3_lp], x3_lc1        ; a layout to stand on before the paint
    mov word [x3d_th], x3_tbw
    call x3_e_init                  ; the engine's state, before any paint
    mov si, x3_tpl
    call OSAPI_WM_CREATE            ; BX = the window
    jc x3_ret
    OS88_REGION_MOVABLE_NOKEEP x3_ret ; every byte that outlives a pass is a
                                    ; static and moves with us
    mov [x3_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on the 8-pixel grid
    mov al, 1
    call OSAPI_WM_OWNBG             ; every pixel is the paint's
    mov si, x3_about
    call OSAPI_ABOUT_SET
    mov si, x3_menus
    call OSAPI_MENU_SET
    mov ax, x3_wheel
    call OSAPI_WM_ONWHEEL
    mov ax, x3_tick
    call OSAPI_WM_ONTIMER
    mov ax, 1
    call OSAPI_WM_TIMER             ; the first pass, next tick
    call x3_prefset
    mov si, x3_pref
    call OSAPI_WM_PREFER            ; the size, per adapter - and applied
    xchg ax, bx                     ; AX = the window
    mov bx, x3_btrec
    mov si, x3_onup
    mov di, x3_ondrag
    mov dx, x3_onclick
    call os88ui_btninit             ; keeps every register
    xchg ax, bx
    clc
x3_ret:
    ret

; -----------------------------------------------------------------------------
; x3_prefset - the OSAPI_WM_PREFER table for what the user wants: FULL on a
;              VGA (MID where the primary's band is EGA's 350 lines), MID on a
;              Hercules, COMPACT on the CGA - and the compact one everywhere
;              once asked for. clobbers AX
; -----------------------------------------------------------------------------
x3_prefset:
    mov word [x3_pref], X3_FW + 2
    mov word [x3_pref+4], X3_FW + 2
    mov word [x3_pref+8], X3_CW + 2
    mov word [x3_pref+10], X3_C1H + TITLE_H + 1
    cmp byte [x3_want], 0
    jne .cmp
    mov ax, X3_FH + TITLE_H + 1
    cmp word [x3_pband], X3_FH + TITLE_H + 1
    jae .vga
    mov ax, X3_MH + TITLE_H + 1
.vga:
    mov [x3_pref+2], ax
    mov word [x3_pref+6], X3_MH + TITLE_H + 1
    ret
.cmp:
    mov word [x3_pref], X3_CW + 2
    mov word [x3_pref+4], X3_CW + 2
    mov word [x3_pref+2], X3_C2H + TITLE_H + 1
    mov word [x3_pref+6], X3_C2H + TITLE_H + 1
    ret

; x3_canbig - CF = 0 when the window's display holds MID at least. keeps all
x3_canbig:
    cmp word [x3_dband], X3_MH + TITLE_H + 1
    jb .no
    cmp word [x3_dwid], X3_FW + 2
    jb .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_relayout - between the compact player and the full one (Options and C):
;               the size table for the new choice, and the window resized to
;               it on the display it is on
; in:  BX = the window. clobbers AX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
x3_relayout:
    mov si, [x3_lp]
    cmp byte [si+L_CMP], 0
    mov byte [x3_want], 1
    je .size
    call x3_canbig                  ; compact now: the full one, if it fits
    jc .out
    mov byte [x3_want], 0
.size:
    call x3_prefset
    mov cx, X3_FW + 2
    mov dx, X3_FH + TITLE_H + 1
    cmp byte [x3_want], 0
    jne .small
    cmp word [x3_dband], dx
    jae .rs
    mov dx, X3_MH + TITLE_H + 1
    jmp short .rs
.small:
    mov cx, X3_CW + 2
    mov dx, X3_C2H + TITLE_H + 1
    cmp word [x3_dband], dx
    jae .rs
    mov dx, X3_C1H + TITLE_H + 1
.rs:
    call OSAPI_WM_RESIZE            ; and the whole window repainted
.out:
    ret

; =============================================================================
; THE COMMANDS
; =============================================================================

; x3_playing - ZF = 1 when a track is under way: playing (either way),
; buffering or paused. keeps everything
x3_playing:
    push ax
    mov al, [x3_st+X3S_STATE]
    cmp al, X3ST_LIVE
    je .out
    cmp al, X3ST_FILE
    je .out
    cmp al, X3ST_BUFFER
    je .out
    cmp al, X3ST_PAUSED
.out:
    pop ax
    ret

; x3_busy - ZF = 1 while the engine owns the machine: checking, choosing a
; drive, decoding to disk. keeps everything
x3_busy:
    push ax
    mov al, [x3_st+X3S_STATE]
    cmp al, X3ST_CHECK
    je .out
    cmp al, X3ST_CHOOSE
    je .out
    cmp al, X3ST_DECODE
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_bok - would this command do anything? (SPEC.md 47 rule 2: THE ONE
;          predicate - the buttons' greying, the menus' and every key's)
; in:  AL = a command (B_* or C_*)
; out: CF = 0 live, CF = 1 refused; every register preserved
;
; Decode First and Decoded Files are refused until their engine exists: the
; output-drive dialog and the conversion are S9 (SPEC.md 121.7), and a menu
; item that does nothing is a lie about the program. Cancel is live only
; while a conversion runs, which until S9 is never.
; -----------------------------------------------------------------------------
x3_bok:
    push bx
    mov bl, al
    xor bh, bh
    cmp bl, C_PP
    ja .no
    shl bx, 1
    jmp [x3_boktab+bx]
.yes:
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret
.track:                             ; Rewind, Fast Forward, Pause: a track
    call x3_playing                 ; under way
    je .yes
    jmp short .no
.play:
    cmp byte [x3_qn], 0
    je .no
    call x3_busy
    je .no
    jmp short .yes
.stop:
    call x3_playing
    je .yes
    cmp byte [x3_st+X3S_STATE], X3ST_CHECK
    je .yes
    jmp short .no
.queue:                             ; anything that wants an entry
    cmp byte [x3_qn], 0
    je .no
    jmp short .yes
.cancel:
    cmp byte [x3_st+X3S_STATE], X3ST_DECODE
    je .yes
    jmp short .no
.theme:
    cmp byte [x3d_bpp1], 0          ; a 1bpp display has one theme
    jne .no
    jmp short .yes
.layout:
    push si
    mov si, [x3_lp]
    cmp byte [si+L_CMP], 0
    pop si
    je .yes                         ; full: compact always fits
    call x3_canbig
    jc .no
    jmp short .yes
.vol:
    test byte [x3_st+X3S_FLAGS], X3SF_VOLOK
    jz .no
    jmp short .yes
.pp:
    mov al, B_PAUSE
    call x3_bok
    mov al, C_PP
    jnc .yes
    mov al, B_PLAY
    call x3_bok
    mov al, C_PP
    jnc .yes
    jmp short .no

x3_boktab:
    dw x3_bok.track, x3_bok.play, x3_bok.track, x3_bok.stop, x3_bok.track
    dw x3_bok.yes                   ; OPEN
    dw x3_bok.queue, x3_bok.queue   ; PREV NEXT
    dw x3_bok.no, x3_bok.no         ; DECODE DFILES: S9's
    dw x3_bok.yes, x3_bok.queue, x3_bok.queue   ; ADD REMOVE CLEAR
    dw x3_bok.cancel, x3_bok.theme, x3_bok.layout
    dw x3_bok.yes, x3_bok.yes, x3_bok.yes       ; QUAL METERS STATUS
    dw x3_bok.queue, x3_bok.queue, x3_bok.queue ; UP DOWN ENTER
    dw x3_bok.vol, x3_bok.vol, x3_bok.pp
%if ($ - x3_boktab) / 2 != C_PP + 1
%error "x3_boktab has a row for every command"
%endif

; -----------------------------------------------------------------------------
; x3_regrey - every button's flags from x3_bok; draw the ones that changed
;             (all when DL = 1)
; clobbers AX, BX, DI
; -----------------------------------------------------------------------------
x3_regrey:
    xor ax, ax
    mov di, x3_bflags
.one:
    call x3_bflag                   ; BX = the flags it should have
    cmp dl, 1
    je .draw
    cmp [di], bx
    je .next
.draw:
    mov [di], bx
    push ax
    push bx
    inc ax                          ; the index PLUS ONE (os88ui_btn's way)
    mov bx, x3_btrec
    call os88ui_btn                 ; keeps every register
    pop bx
    pop ax
.next:
    inc di
    inc di
    inc ax
    cmp al, X3_NB
    jb .one
    ret

; x3_bflag - AL = a button -> BX = its flags: OWN, greyed, latched. keeps the
; rest. Play is latched while a track plays, Pause while it is paused
x3_bflag:
    mov bx, OS88UI_OWN
    call x3_bok
    jnc .live
    or bx, OS88UI_DIS
.live:
    cmp al, B_PLAY
    jne .np
    cmp byte [x3_st+X3S_STATE], X3ST_LIVE
    je .lat
    cmp byte [x3_st+X3S_STATE], X3ST_FILE
    je .lat
    cmp byte [x3_st+X3S_STATE], X3ST_BUFFER
    je .lat
    ret
.np:
    cmp al, B_PAUSE
    jne .out
    cmp byte [x3_st+X3S_STATE], X3ST_PAUSED
    jne .out
.lat:
    or bx, OS88UI_LATCH
.out:
    ret

; -----------------------------------------------------------------------------
; x3_act - do what command AL says (the caller asked x3_bok: it is live)
; in:  BX = the window
; out: CF = 1 when the whole content was repainted (a theme, a layout);
;      clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
x3_act:
    mov [x3_win], bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    jmp [x3_acttab+bx]

x3_acttab:
    dw x3_a_rew, x3_a_play, x3_a_pause, x3_e_stop, x3_a_ff, x3_a_open
    dw x3_a_prev, x3_a_next, x3_a_none, x3_a_none
    dw x3_a_add, x3_a_remove, x3_a_clear
    dw x3_e_cancel, x3_a_theme, x3_a_layout
    dw x3_a_qual, x3_a_meters, x3_a_status
    dw x3_a_up, x3_a_down, x3_a_enter
    dw x3_a_volup, x3_a_voldn, x3_a_pp
%if ($ - x3_acttab) / 2 != C_PP + 1
%error "x3_acttab has a row for every command"
%endif

x3_a_none:
    clc
    ret
x3_a_rew:
    mov ax, [x3_st+X3S_ELAP]
    sub ax, X3_SEEK
    jae x3_e_seek
    xor ax, ax
    jmp x3_e_seek
x3_a_ff:
    mov ax, [x3_st+X3S_ELAP]
    add ax, X3_SEEK
    jmp x3_e_seek                   ; (the engine clamps to the track)
x3_a_pp:
    cmp byte [x3_st+X3S_STATE], X3ST_LIVE
    je x3_e_pause
    cmp byte [x3_st+X3S_STATE], X3ST_FILE
    je x3_e_pause
x3_a_play:
    cmp byte [x3_st+X3S_STATE], X3ST_PAUSED
    je x3_e_resume
    call x3_playing
    je x3_a_none                    ; already: nothing to do
    mov al, [x3_qsel]
    jmp x3_e_play
x3_a_pause:
    cmp byte [x3_st+X3S_STATE], X3ST_PAUSED
    je x3_e_resume
    jmp x3_e_pause
x3_a_enter:
    mov al, [x3_qsel]
    jmp x3_e_play
x3_a_prev:
    call x3_playing                 ; a few seconds in: the same track again
    jne .back
    cmp word [x3_st+X3S_ELAP], X3_BACK
    jbe .back
    xor ax, ax
    jmp x3_e_seek
.back:
    mov al, [x3_st+X3S_CUR]
    cmp al, 0xFF
    jne .have
    mov al, [x3_qsel]
.have:
    or al, al
    jnz .dec
    mov al, [x3_qn]                 ; the first: round to the last
.dec:
    dec al
    jmp short x3_a_goto
x3_a_next:
    mov al, [x3_st+X3S_CUR]
    cmp al, 0xFF
    jne .have
    mov al, [x3_qsel]
.have:
    inc al
    cmp al, [x3_qn]
    jb x3_a_goto
    xor al, al                      ; the last: round to the first
; x3_a_goto - entry AL becomes the selection: played if something was
; playing, else cued
x3_a_goto:
    mov [x3_qsel], al
    call x3_playing
    je x3_e_play
    jmp x3_e_cue
x3_a_up:
    mov al, [x3_qsel]
    or al, al
    jz x3_a_none
    dec al
    jmp short x3_a_sel
x3_a_down:
    mov al, [x3_qsel]
    inc al
    cmp al, [x3_qn]
    jae x3_a_none
; x3_a_sel - the selection moves to AL; with nothing playing the LCD shows it
x3_a_sel:
    mov [x3_qsel], al
    call x3_playing
    je x3_a_none
    call x3_busy
    je x3_a_none
    jmp x3_e_cue
x3_a_volup:
    mov al, [x3_st+X3S_VOL]
    cmp al, X3_VMAX
    jae x3_a_none
    inc al
    jmp short x3_a_vol
x3_a_voldn:
    mov al, [x3_st+X3S_VOL]
    or al, al
    jz x3_a_none
    dec al
x3_a_vol:
    mov [x3_st+X3S_VOL], al
    jmp x3_e_vol
x3_a_qual:
    mov al, [x3_st+X3S_QWANT]       ; AUTO, ORIGINAL ... XT SPEECH, AUTO
    inc al
    cmp al, X3Q_N
    jb .q
    xor al, al
.q:
    mov [x3_st+X3S_QWANT], al
    jmp x3_e_qual
x3_a_meters:
    xor byte [x3_meters], 1
    clc
    ret
x3_a_status:
    xor byte [x3_adv], 1
    clc
    ret
x3_a_theme:
    xor byte [x3_theme], 1
    mov bx, [x3_win]
    call OSAPI_WM_CLIP_SET          ; a theme is every pixel: the whole paint
    jc .out
    mov si, bx
    call x3f_paint
.out:
    stc
    ret
x3_a_layout:
    mov bx, [x3_win]
    call x3_relayout
    stc
    ret
x3_a_open:
    mov byte [x3_addmode], 0
    jmp short x3_dlg
x3_a_add:
    mov byte [x3_addmode], 1
; x3_dlg - the Standard File dialog (SPEC.md 38), its answer to x3_onfile
x3_dlg:
    mov al, FDLG_OPEN
    mov bx, [x3_win]
    mov di, x3_onfile
    xor si, si
    call OSAPI_FILE_DLG
    clc
    ret
x3_a_remove:
    mov al, [x3_qsel]
    call x3_qdel
; x3_a_recue - with nothing under way, the LCD shows the selection
x3_a_recue:
    call x3_playing
    je x3_a_none
    call x3_busy
    je x3_a_none
    mov al, [x3_qsel]
    cmp al, 0xFF
    je x3_a_none
    jmp x3_e_cue
x3_a_clear:
    call x3_e_qclear
    mov byte [x3_qn], 0
    mov byte [x3_qsel], 0xFF
    mov byte [x3_qtop], 0
    inc byte [x3_qgen]
    clc
    ret

; -----------------------------------------------------------------------------
; x3_cmd - command AL from a key or a menu: refused quietly when x3_bok says
;          so, else done and what changed redrawn
; in:  BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
x3_cmd:
    call x3_bok
    jc .out
    push bx
    call x3_act
    pop bx
    jc .out                         ; it repainted the whole of it
    call x3f_update
.out:
    ret

; =============================================================================
; THE QUEUE
; =============================================================================

; x3_qent - AL = an entry -> SI = its record. keeps the rest
x3_qent:
    push ax
    push dx
    xor ah, ah
    mov si, QE_SIZE
    mul si
    mov si, ax
    add si, x3_q
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; x3_qadd - a file joins the queue: its name, folder and volume, and its
;           title for now the name without the extension
; in:  SI = the NUL 8.3 name (ours), DX = its folder, BL = its volume
; out: CF = 1 the queue is full; else AL = its entry. clobbers AX, CX, SI, DI
; -----------------------------------------------------------------------------
x3_qadd:
    mov al, [x3_qn]
    cmp al, X3_QMAX
    jae .full
    push si
    call x3_qent
    mov di, si                      ; DI = the record
    pop si
    push di
    mov cx, 12                      ; the name
.n:
    lodsb
    mov [di+QE_NAME], al
    inc di
    or al, al
    loopnz .n
    mov byte [di+QE_NAME], 0
    pop di
    mov [di+QE_VOL], bl
    mov [di+QE_DIR], dx
    mov word [di+QE_DUR], 0xFFFF
    mov byte [di+QE_FLAGS], 0
    lea si, [di+QE_NAME]            ; the title: the stem
    push di
    add di, QE_TITLE
.t:
    lodsb
    or al, al
    jz .tz
    cmp al, '.'
    je .tz
    mov [di], al
    inc di
    jmp short .t
.tz:
    mov byte [di], 0
    pop di
    mov al, [x3_qn]
    inc byte [x3_qn]
    inc byte [x3_qgen]
    push ax
    call x3_e_qadd                  ; the engine's turn: a title, a duration
    pop ax
    cmp byte [x3_qsel], 0xFF
    jne .ok
    mov [x3_qsel], al
.ok:
    clc
    ret
.full:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_qdel - entry AL leaves the queue; the engine is told first (it stops a
;           track that was the one playing). clobbers AX, CX, SI, DI
; -----------------------------------------------------------------------------
x3_qdel:
    cmp al, [x3_qn]
    jae .out
    push ax
    call x3_e_qdrop
    pop ax
    call x3_qent                    ; the ones after it, down one
    mov di, si
    add si, QE_SIZE
    mov cl, [x3_qn]
    sub cl, al
    dec cl
    mov al, QE_SIZE
    mul cl
    mov cx, ax
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    dec byte [x3_qn]
    inc byte [x3_qgen]
    mov al, [x3_qsel]               ; the selection: the same place, or the
    cmp al, [x3_qn]                 ; last, or none
    jb .out
    mov al, [x3_qn]
    dec al
    mov [x3_qsel], al
.out:
    ret

; -----------------------------------------------------------------------------
; x3_qfollow - the list's top: the selection kept in view when it MOVED (a
;              scroll of the user's is left alone), and clamped
; clobbers AX
; -----------------------------------------------------------------------------
x3_qfollow:
    push si
    mov si, [x3_lp]
    mov si, [si+L_Q]
    or si, si
    jz .out                         ; compact: no list
    mov al, [x3_qsel]
    cmp al, [x3_qlsel]
    je .clamp
    mov [x3_qlsel], al
    cmp al, 0xFF
    je .clamp
    cmp al, [x3_qtop]
    jae .below
    mov [x3_qtop], al
.below:
    sub al, [si+QG_N]
    jb .clamp
    inc al
    cmp al, [x3_qtop]
    jbe .clamp
    mov [x3_qtop], al
.clamp:
    mov al, [x3_qn]                 ; the top at most entries - rows
    sub al, [si+QG_N]
    jae .max
    xor al, al
.max:
    cmp [x3_qtop], al
    jbe .out
    mov [x3_qtop], al
.out:
    pop si
    ret

; x3_qscroll - the list's top moved AX rows (signed). clobbers AX
x3_qscroll:
    add al, [x3_qtop]
    test ah, 0x80
    jz .pos
    cmp al, [x3_qtop]               ; went below 0: the first
    jbe .set
    xor al, al
    jmp short .set
.pos:
    cmp al, [x3_qtop]
    jae .set
    mov al, 0xFF                    ; wrapped past 255: the clamp's
.set:
    mov [x3_qtop], al
    jmp x3_qfollow

; -----------------------------------------------------------------------------
; x3_onfile - the Standard File dialog's answer (SPEC.md 38): an .MP3 joins
;             the queue - Open also selects it, and plays it when nothing is
; in:  ES:DI = the name (the kernel's, for this call only), SI = the window;
;      the gfx lock held. The content is ours to repaint
; -----------------------------------------------------------------------------
x3_onfile:
    push si
    mov si, di                      ; the name, out of the kernel's buffer
    mov di, x3_dlgname
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    call x3_isMP3
    jc .bad
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the volume
    mov si, x3_dlgname
    call x3_qadd
    jc .full
    cmp byte [x3_addmode], 0
    jne .paint
    mov [x3_qsel], al               ; Open: the selection, and the music if
    call x3_playing                 ; none plays
    je .paint
    call x3_busy
    je .paint
    call x3_e_play
    jmp short .paint
.full:
    mov si, x3_s_qfull
    jmp short .toast
.bad:
    mov si, x3_s_notmp3
.toast:
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
.paint:
    pop si
    mov bx, si                      ; the dialog was over us: all of it
    call OSAPI_WM_CLIP_SET
    jc .out
    call x3f_paint
.out:
    ret

; x3_isMP3 - CF = 0 when x3_dlgname ends in .MP3 (any case). clobbers AX, SI
x3_isMP3:
    mov si, x3_dlgname
.f:
    lodsb
    or al, al
    jz .no
    cmp al, '.'
    jne .f
    lodsw
    and ax, 0xDFDF
    cmp ax, 'MP'
    jne .no
    lodsb
    cmp al, '3'
    jne .no
    cmp byte [si], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

; x3_arg - the document we were opened on: queued and played (SPEC.md 54.5).
; clobbers AX, BX, CX, DX, SI, DI
x3_arg:
    mov byte [x3_argwant], 0
    mov si, x3_argname
    mov di, x3_dlgname
.c:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .c
    call x3_isMP3
    jc .out
    mov si, x3_dlgname
    mov dx, [x3_argdir]
    mov bl, [x3_argvol]
    call x3_qadd
    jc .out
    mov [x3_qsel], al
    jmp x3_e_play
.out:
    ret

; =============================================================================
; CALLBACKS
; =============================================================================

; -----------------------------------------------------------------------------
; x3_tick - W_ONTIMER: re-arm, give the engine its turn, redraw what moved
; in:  SI = the window; the UI task, gfx lock held, no clip armed
; -----------------------------------------------------------------------------
x3_tick:
    push si                         ; the window, given back (SPEC.md 111.6)
    mov bx, si
    mov ax, X3_TICK                 ; one-shot: the repeat is ours to ask for
    call OSAPI_WM_TIMER
    cmp byte [x3_argwant], 0
    je .eng
    call x3_arg                     ; a document to play, once
.eng:
    call x3_e_tick
    pop bx
    push bx
    call x3f_update
    pop si
    ret

; -----------------------------------------------------------------------------
; x3_ondrag / x3_onup - the gesture (SPEC.md 13.8): a button's is the
;                       library's and fires on the RELEASE
; in:  CX, DX = the pointer (screen), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
x3_ondrag:
    mov bx, x3_btrec
    jmp os88ui_btndrag

x3_onup:
    mov bx, x3_btrec
    call os88ui_btnup               ; AX = what FIRED, index + 1; 0 = cancelled
    dec ax
    js .out
    push si
    mov bx, si
    call x3_cmd
    pop si                          ; the window, given back (SPEC.md 111.6)
.out:
    ret

; -----------------------------------------------------------------------------
; x3_onclick - a press that was not a button's: the About card's dismissal,
;              the queue's rows, its scroll bar, the knob, the progress bar
; in:  CX, DX = the pointer (screen), SI = the window
; out: CF = 1 the press was ours
; -----------------------------------------------------------------------------
x3_onclick:
    cmp byte [x3_btrec+OS88UI_BT_N], 0
    jne .face
    call x3_dismiss
    stc
    ret
.face:
    push si
    mov bx, si
    call x3d_origin
    sub cx, [x3d_ox]
    sub dx, [x3d_oy]
    call x3_hit
    pop si
    jc .not
    push si
    mov bx, si
    call x3f_update
    pop si
    stc
    ret
.not:
    clc
    ret

; x3_hit - CX, DX = a content point: act on it. CF = 1 nothing there.
; clobbers AX, BX, CX, DX, SI, DI
x3_hit:
    mov si, [x3_lp]                 ; the progress bar (a row more each way):
    mov ax, [si+L_PB+2]             ; a seek
    dec ax
    cmp dx, ax
    jl .npb
    mov ax, [si+L_PB+6]
    inc ax
    cmp dx, ax
    jg .npb
    cmp cx, [si+L_PB]
    jl .npb
    cmp cx, [si+L_PB+4]
    jg .npb
    mov al, B_FF                    ; (live exactly when a seek is)
    call x3_bok
    jc .yes
    mov ax, cx
    sub ax, [si+L_PB]
    mov bx, [si+L_PB+4]
    sub bx, [si+L_PB]
    inc bx
    mul word [x3_st+X3S_TOTAL]
    div bx
    call x3_e_seek
    jmp .yes
.npb:
    mov di, [si+L_KNOB]             ; the knob: its left half turns it down,
    or di, di                       ; its right half up
    jz .nkn
    mov ax, cx
    sub ax, [di+KG_X]
    jl .nkn
    cmp ax, [di+KG_W]
    jge .nkn
    mov bx, dx
    sub bx, [di+KG_Y]
    jl .nkn
    cmp bx, [di+KG_H]
    jge .nkn
    mov bx, [di+KG_W]
    shr bx, 1
    cmp ax, bx
    mov al, C_VOLDN
    jb .kv
    mov al, C_VOLUP
.kv:
    call x3_bok
    jc .yes
    mov bx, [x3_win]
    call x3_act
    jmp .yes
.nkn:
    mov di, [si+L_Q]                ; the queue
    or di, di
    jz .no
    mov ax, [di+QG_SBX]             ; its scroll bar: an arrow is a row, the
    cmp cx, ax                      ; trough a page
    jl .rows
    add ax, 12
    cmp cx, ax
    jg .no
    mov ax, [di+QG_SBT1]
    sub ax, 13
    cmp dx, ax
    jl .no
    mov ax, -1
    cmp dx, [di+QG_SBT1]
    jl .scr
    mov ax, [di+QG_SBT2]
    add ax, 13
    cmp dx, ax
    jg .no
    mov ax, 1
    cmp dx, [di+QG_SBT2]
    jg .scr
    mov bx, [di+QG_SBT1]
    add bx, [di+QG_SBT2]
    shr bx, 1
    mov ax, [di+QG_N]
    cmp dx, bx
    jge .scr
    neg ax
.scr:
    call x3_qscroll
    jmp short .yes
.rows:
    mov ax, [di+QG_X]               ; a row: selected - and played when it was
    cmp cx, ax                      ; already the selection
    jl .no
    sub dx, [di+QG_Y0]
    jl .no
    mov ax, dx
    xor dx, dx
    div word [di+QG_PITCH]
    cmp ax, [di+QG_N]
    jae .no
    add al, [x3_qtop]
    cmp al, [x3_qn]
    jae .yes
    cmp al, [x3_qsel]
    je .again
    call x3_a_sel
    jmp short .yes
.again:
    call x3_busy
    je .yes
    call x3_e_play
.yes:
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; x3_wheel - the wheel: the queue where there is one, else the volume
; in:  AX = signed notches, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
x3_wheel:
    push si
    mov bx, [x3_lp]
    cmp word [bx+L_Q], 0
    je .vol
    call x3_qscroll
    jmp short .draw
.vol:
    mov bx, si
    mov cl, C_VOLDN                 ; a notch toward the user is quieter
    or ax, ax
    jns .up
    mov cl, C_VOLUP
.up:
    mov al, cl
    call x3_bok
    jc .out
    call x3_act
.draw:
    mov bx, si
    call x3f_update
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; x3_key - W_ONKEY (SPEC.md 121.9's table)
; in:  AL = ASCII, AH = scan, SI = the window. Keeps SI
; -----------------------------------------------------------------------------
x3_key:
    push si
    cmp byte [x3_btrec+OS88UI_BT_N], 0
    jne .keys
    call x3_dismiss                 ; any key takes the About card down
    jmp short .out
.keys:
    mov bx, x3_keytab
.find:
    cmp byte [bx], 0
    je .out                         ; every other key - the F keys, Alt+Enter
    cmp ah, [bx]                    ; - is not ours. A scan code, or...
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
    pop bx
    push bx
    call x3_cmd
.out:
    pop si
    ret

; =============================================================================
; THE MENUS (SPEC.md 12.2, 121.9) - About is the application menu's own
; =============================================================================

; x3_menu - a menu item: AH = the menu, AL = the item, SI = the window
x3_menu:
    push si
    mov bx, x3_mfile
    cmp ah, 1
    jb .tab
    mov bx, x3_mplay
    je .tab
    mov bx, x3_mopt
.tab:
    xlatb                           ; AL = the command
    mov bx, si
    call x3_cmd
    pop si
    ret

; -----------------------------------------------------------------------------
; x3_menulive - every item's greying and every name, from the predicate the
;               buttons use (SPEC.md 47 rule 2). The kernel reads the strings
;               when a menu opens, so writing them is enough.
; clobbers AX, BX, SI
; -----------------------------------------------------------------------------
x3_menulive:
    mov bx, x3_i_file               ; File and Playback: their commands'
    mov si, x3_mfile
.f:
    mov al, [si]
    call x3_mlive
    add bx, 2
    inc si
    cmp si, x3_mfile + X3_NFILE
    jb .f
    mov bx, x3_i_play
    mov si, x3_mplay
.p:
    mov al, [si]
    call x3_mlive
    add bx, 2
    inc si
    cmp si, x3_mplay + X3_NPLAY
    jb .p
    mov bl, [x3_st+X3S_QWANT]       ; Options: each names its state
    and bx, 7
    shl bx, 1
    mov ax, [x3_mqual+bx]
    mov [x3_i_opt], ax
    mov ax, x3_o_col
    cmp word [x3d_th], x3_tcol
    je .th
    mov ax, x3_o_bw
.th:
    mov [x3_i_opt+2], ax
    mov al, C_THEME
    mov bx, x3_i_opt+2
    call x3_mlive
    mov ax, x3_o_meton
    cmp byte [x3_meters], 0
    jne .mt
    mov ax, x3_o_metoff
.mt:
    mov [x3_i_opt+4], ax
    mov ax, x3_o_cmp
    mov bx, [x3_lp]
    cmp byte [bx+L_CMP], 0
    je .big
    mov ax, x3_o_full
.big:
    mov [x3_i_opt+6], ax
    mov al, C_LAYOUT
    mov bx, x3_i_opt+6
    call x3_mlive
    mov ax, x3_o_basic
    cmp byte [x3_adv], 0
    je .st
    mov ax, x3_o_adv
.st:
    mov [x3_i_opt+8], ax
    ret

; x3_mlive - BX = an item's slot: it points at the live string, or at the
; MENU_DIS byte laid out in front of it (the greyed form). Live unless
; command AL is refused. keeps everything
x3_mlive:
    push si
    mov si, [bx]
    cmp byte [si], MENU_DIS
    jne .live
    inc si
.live:
    call x3_bok
    jnc .set
    dec si
.set:
    mov [bx], si
    pop si
    ret

; =============================================================================
; 'About XTMP3' - the standard card (SPEC.md 12.2, 20.5.1)
; =============================================================================
; While the card is up the button record is told it has NO buttons, so a
; press anywhere in the content reaches x3_onclick and takes the card down -
; and that zero IS the card's state (Timer's idiom, SPEC.md 111.6).
x3_about:
    mov byte [x3_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, x3_ablines
    jmp os88ui_about

; x3_dismiss - put the buttons back and repaint the content. SI = the window
x3_dismiss:
    mov byte [x3_btrec+OS88UI_BT_N], X3_NB
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out
    call x3f_paint                  ; SI = the window still; keeps it
.out:
    ret

; =============================================================================
; TEXT HELPERS
; =============================================================================

; x3_put - the NUL string at SI to DI, the NUL not copied; DI past it.
; clobbers SI (keeps AX)
x3_put:
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

; x3_putn - x3_put of at most CX characters; CX counts down by those copied.
; clobbers SI (keeps AX)
x3_putn:
    push ax
.c:
    jcxz .out
    lodsb
    or al, al
    jz .out
    mov [di], al
    inc di
    dec cx
    jmp short .c
.out:
    pop ax
    ret

; x3_2dig - AL (< 100) as two digits at DI; DI past them. clobbers AX
x3_2dig:
    aam
    add ax, '00'
    xchg al, ah
    mov [di], ax
    inc di
    inc di
    ret

; x3_mmss - AX seconds as 'mm:ss ' at DI (minutes to 99); DI past it.
; clobbers AX
x3_mmss:
    push cx
    push dx
    xor dx, dx
    mov cx, 60
    div cx                          ; AX = minutes, DX = seconds
    cmp ax, 99
    jbe .m
    mov ax, 99
.m:
    call x3_2dig
    mov byte [di], ':'
    inc di
    mov ax, dx
    call x3_2dig
    mov byte [di], ' '
    inc di
    pop dx
    pop cx
    ret

; x3_fillb - CX bytes of AL at DI. keeps everything
x3_fillb:
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

; x3_hash - SI = a NUL string -> AX = a 16-bit key of it, never FFFFh: a
; text field's key is the text itself, folded. keeps the rest
x3_hash:
    push bx
    push si
    mov bx, 0x5A3C
.c:
    lodsb
    or al, al
    jz .out
    rol bx, 1
    xor bl, al
    add bh, al
    jmp short .c
.out:
    mov ax, bx
    cmp ax, 0xFFFF
    jne .k
    dec ax
.k:
    pop si
    pop bx
    ret

%include "x3draw.inc"
%include "x3face.inc"
%include "x3demo.inc"

; =============================================================================
; DATA
; =============================================================================
x3_tpl:     dw 12, 28, X3_CW + 2, X3_C1H + TITLE_H + 1
            dw x3_ttl, x3f_paint, x3_key, 0
x3_ttl      equ 16                  ; the header's name field: 'XTMP3'

x3_pref:    times 6 dw 0            ; OSAPI_WM_PREFER's table (x3_prefset)

; --- the themes: a byte a role (x3draw.inc) ------------------------------------------
x3_tcol:                            ; COLOUR - xtmp3-color.png
    db CLGRAY, CBLACK, CWHITE, CDGRAY, CBLACK       ; GND EDGE HI SH LABEL
    db CBLACK, CDGRAY, CLCYAN, CLGREEN, CWHITE      ; LCD RIM TTL TIM TOT
    db CWHITE, CLCYAN                               ; FMT RATE
    db CWHITE, D_DITHER + 0, CBLACK                 ; PBFRM PBFG PBBG
    db CLGRAY, CBLACK, CDGRAY, CBLACK, CLGREEN      ; KEY INK DIS ON ONI
    db CDGRAY                                       ; BEZEL
    db CWHITE, CBLACK, CBLUE, CWHITE                ; QBOX QTXT QSEL QSELT
    db CWHITE, P_DITHER                             ; TROUGH THUMB
    db CLGRAY, CBLACK                               ; STAT STATTXT
x3_tbw:                             ; BLACK AND WHITE - xtmp3-mono.png
    db CWHITE, CBLACK, CWHITE, CBLACK, CBLACK
    db CBLACK, CWHITE, CWHITE, CWHITE, CWHITE
    db CWHITE, CWHITE
    db CWHITE, P_DITHER, CBLACK
    db CWHITE, CBLACK, CBLACK, CBLACK, CWHITE
    db CBLACK
    db CWHITE, CBLACK, CBLACK, CWHITE
    db CWHITE, P_DITHER
    db CWHITE, CBLACK
%if (x3_tbw - x3_tcol) != X3_NROLES || ($ - x3_tbw) != X3_NROLES
%error "XTMP3's theme tables must have a byte for every role"
%endif

; --- the faces (x3draw.inc's X3FD_*) ---------------------------------------------------
x3_fcaps:   dw x3art_caps_ix, x3art_caps            ; the panel's small caps
            db 7, 6, 5, 0
x3_fcapsb:  dw x3art_caps_ix, x3art_caps            ; ...bold
            db 7, 7, 6, 1
x3_fcapst:  dw x3art_caps_ix, x3art_caps            ; ...tracked, the legend's
            db 7, 7, 5, 0
x3_flcdf:   dw x3art_lcdf_ix, x3art_lcdf            ; the LCD's title, FULL
            db 10, 9, 6, 0
x3_flcdm:   dw x3art_lcdm_ix, x3art_lcdm            ; ...MID's
            db 8, 8, 6, 0

; --- the digit sets (x3draw.inc's X3DS_*) -----------------------------------------------
%macro X3DSET 3                     ; set, SET, 1 = a thin set (has '/', '~')
x3_ds%1:    dw x3art_%1_tab, x3art_%1_col, X3ART_%2_GAP, X3ART_%2_W
            dw X3ART_%2_H, X3ART_%2_CW
  %if %3
            dw x3art_%1_sl, x3art_%1_tl, X3ART_%2_SW
  %else
            dw 0, 0, 0
  %endif
%endmacro
    X3DSET tf, TF, 0
    X3DSET tm, TM, 0
    X3DSET tc, TC, 0
    X3DSET nf, NF, 1
    X3DSET nm, NM, 1
    X3DSET nc, NC, 1

    OS88UI_BTNREC x3_btrec, x3_rects, x3_blbl, x3_bflags, X3_NB
x3_blbl:    times X3_NB dw x3f_bpaint   ; every key is XTMP3's to paint

; the keys: a scan code, or 80h + an upper-cased ASCII; then the command
x3_keytab:
    db 0x39, C_PP                   ; Space
    db 0x4B, C_PREV, 0x4D, C_NEXT   ; Left, Right: the tracks
    db 0x48, C_UP, 0x50, C_DOWN     ; Up, Down: the selection
    db 0x1C, C_ENTER                ; Enter
    db 0x01, C_CANCEL               ; Esc
    db 0x80 | 'S', B_STOP
    db 0x80 | (',' & 0xDF), B_REW, 0x80 | ('.' & 0xDF), B_FF
    db 0x80 | ('+' & 0xDF), C_VOLUP, 0x80 | ('=' & 0xDF), C_VOLUP
    db 0x80 | ('-' & 0xDF), C_VOLDN
    db 0x80 | 'O', B_OPEN, 0x80 | 'Q', C_QUAL, 0x80 | 'D', C_DECODE
    db 0x80 | 'T', C_THEME, 0x80 | 'C', C_LAYOUT, 0x80 | 'I', C_STATUS
    db 0

; --- the menus --------------------------------------------------------------------------
    OS88_MENUSET x3_menus, x3_name, x3_menu
        OS88_MENU x3_m_file, x3_i_file, X3_NFILE
        OS88_MENU x3_m_play, x3_i_play, X3_NPLAY
        OS88_MENU x3_m_opt, x3_i_opt, 5
    OS88_MENUSET_END x3_menus
x3_name:    db 'XTMP3', 0
x3_m_file:  db 'File', 0
x3_m_play:  db 'Playback', 0
x3_m_opt:   db 'Options', 0
x3_mfile:   db B_OPEN, C_ADD, C_REMOVE, C_CLEAR, C_DFILES
X3_NFILE    equ $ - x3_mfile
x3_mplay:   db C_PP, B_STOP, C_PREV, C_NEXT, B_REW, B_FF, C_DECODE
X3_NPLAY    equ $ - x3_mplay
x3_mopt:    db C_QUAL, C_THEME, C_METERS, C_LAYOUT, C_STATUS
x3_i_file:  dw x3_s_mopen, x3_s_madd, x3_s_mrem, x3_s_mclr, x3_s_mdec
x3_i_play:  dw x3_s_mpp, x3_s_mstop, x3_s_mprev, x3_s_mnext, x3_s_mrew
            dw x3_s_mff, x3_s_mdf
x3_i_opt:   dw x3_o_q0, x3_o_col, x3_o_meton, x3_o_cmp, x3_o_basic
            db MENU_DIS
x3_s_mopen: db 'Open...  O', 0
            db MENU_DIS
x3_s_madd:  db 'Add to Queue...', 0
            db MENU_DIS
x3_s_mrem:  db 'Remove from Queue', 0
            db MENU_DIS
x3_s_mclr:  db 'Clear Queue', 0
            db MENU_DIS
x3_s_mdec:  db 'Decoded Files...', 0
            db MENU_DIS
x3_s_mpp:   db 'Play/Pause  Space', 0
            db MENU_DIS
x3_s_mstop: db 'Stop  S', 0
            db MENU_DIS
x3_s_mprev: db 'Previous Track', 0
            db MENU_DIS
x3_s_mnext: db 'Next Track', 0
            db MENU_DIS
x3_s_mrew:  db 'Rewind  ,', 0
            db MENU_DIS
x3_s_mff:   db 'Fast Forward  .', 0
            db MENU_DIS
x3_s_mdf:   db 'Decode First...  D', 0
x3_mqual:   dw x3_o_q0, x3_o_q1, x3_o_q2, x3_o_q3, x3_o_q4, x3_o_q5
            dw x3_o_q6, x3_o_q7     ; Quality NAMES its choice: an app menu
                                    ; has no check mark (SPEC.md 121.9)
x3_o_q0:    db 'Quality: Auto  Q', 0
x3_o_q1:    db 'Quality: Original  Q', 0
x3_o_q2:    db 'Quality: Half  Q', 0
x3_o_q3:    db 'Quality: Quarter  Q', 0
x3_o_q4:    db 'Quality: XT Standard  Q', 0
x3_o_q5:    db 'Quality: XT Fast  Q', 0
x3_o_q6:    db 'Quality: XT Turbo  Q', 0
x3_o_q7:    db 'Quality: XT Speech  Q', 0
            db MENU_DIS
x3_o_col:   db 'Theme: Color  T', 0
            db MENU_DIS
x3_o_bw:    db 'Theme: Black and White  T', 0
x3_o_meton: db 'Meters: On', 0
x3_o_metoff: db 'Meters: Off', 0
            db MENU_DIS
x3_o_cmp:   db 'Compact Player  C', 0
            db MENU_DIS
x3_o_full:  db 'Full Player  C', 0
x3_o_basic: db 'Status: Basic  I', 0
x3_o_adv:   db 'Status: Advanced  I', 0

; --- the face's words -----------------------------------------------------------------------
x3_s_tag:     db 'DIGITAL AUDIO PLAYER', 0
x3_s_output:  db 'OUTPUT', 0
x3_s_volume:  db 'VOLUME', 0
x3_s_min:     db 'MIN', 0
x3_s_max:     db 'MAX', 0
x3_s_l:       db 'L', 0
x3_s_r:       db 'R', 0
x3_s_mono:    db 'MONO', 0
x3_s_stereo:  db 'STEREO', 0
x3_s_buffer:  db 'BUFFER', 0
x3_s_open:    db 'OPEN', 0
x3_s_khz:     db ' kHz  ', 0
x3_s_k441:    db '44.1 kHz  ', 0
x3_s_k55:     db '5.5 kHz  ', 0
x3_s_8bit:    db '8-BIT', 0
x3_s_16bit:   db '16-BIT', 0
x3_s_device:  db ' DEVICE', 0
x3_s_notrack: db 'NO TRACK - O TO OPEN', 0
x3_s_quality: db 'Quality: ', 0
x3_s_set:     db ' (set)', 0
x3_s_peak:    db 'Peak ', 0
x3_s_kunder:  db 'K  ', 0
x3_s_under:   db ' underruns  ', 0
x3_s_dot:     db '.', 0
x3_s_x:       db 'x', 0
x3_s_pct:     db '%', 0
x3_s_notmp3:  db 'XTMP3 plays .MP3 files', 0
x3_s_qfull:   db 'The queue is full', 0

x3_qnames:  dw x3_q_auto, x3_q_orig, x3_q_half, x3_q_quart, x3_q_xtstd
            dw x3_q_xtfast, x3_q_xtturbo, x3_q_xtspch
x3_q_auto:    db 'AUTO', 0
x3_q_orig:    db 'ORIGINAL', 0
x3_q_half:    db 'HALF', 0
x3_q_quart:   db 'QUARTER', 0
x3_q_xtstd:   db 'XT STANDARD', 0
x3_q_xtfast:  db 'XT FAST', 0
x3_q_xtturbo: db 'XT TURBO', 0
x3_q_xtspch:  db 'XT SPEECH', 0

x3_stnames: dw x3_t_idle, x3_t_check, x3_t_choose, x3_t_decode, x3_t_ready
            dw x3_t_live, x3_t_file, x3_t_paused, x3_t_buffer, x3_t_done
            dw x3_t_error
%if ($ - x3_stnames) / 2 != X3ST_N
%error "x3_stnames has a name for every state"
%endif
x3_t_idle:    db 'Open an MP3 file', 0
x3_t_check:   db 'Checking performance', 0
x3_t_choose:  db 'Choosing output drive', 0
x3_t_decode:  db 'Decoding to disk ', 0
x3_t_ready:   db 'Ready to play', 0
x3_t_live:    db 'Playing live', 0
x3_t_file:    db 'Playing decoded file', 0
x3_t_paused:  db 'Paused', 0
x3_t_buffer:  db 'Buffering', 0
x3_t_done:    db 'Finished', 0
x3_t_error:   db 'Error: ', 0

x3_ablines:
    dw x3_ab1, x3_ab2, x3_ab3, 0
x3_ab1:     db 'XTMP3 for os8088', 0
x3_ab2:     db 'MP3 at the best quality', 0
x3_ab3:     db 'this machine can sustain', 0

%include "x3art.inc"

%define OS88UI_ABOUT
%define OS88UI_NOGLYPH
%define OS88UI_BOWN
%include "os88ui.inc"

; --- loader-zeroed bss (SPEC.md 21 step 5), allocated in order --------------------
%assign X3B_AT 0
%macro X3B 2                        ; name, bytes
%1 equ os88_image_end + X3B_AT
%assign X3B_AT X3B_AT + (%2)
%endmacro
    X3B x3_st, X3S_SIZE             ; THE STATE RECORD (x3face.inc)
    X3B x3_win, 2                   ; our window
    X3B x3_lp, 2                    ; the layout record
    X3B x3_pband, 2                 ; the primary's desktop band
    X3B x3_dband, 2                 ; the window's display's band...
    X3B x3_dwid, 2                  ; ...and width
    X3B x3_argdir, 2                ; the document we were opened on
    X3B x3_argname, 14
    X3B x3_argvol, 1
    X3B x3_argwant, 1               ; 1 = queue and play it at the first tick
    X3B x3_dlgname, 14              ; the file dialog's answer
    X3B x3_addmode, 1               ; 1 = Add to Queue, 0 = Open
    X3B x3_theme, 1                 ; 0 colour, 1 black and white (the choice)
    X3B x3_want, 1                  ; 1 = the user asked for the compact one
    X3B x3_meters, 1                ; 1 = the needles move (Options > Meters)
    X3B x3_adv, 1                   ; 1 = the status line's advanced figures
    X3B x3_qn, 1                    ; THE QUEUE: entries...
    X3B x3_qsel, 1                  ; ...the selection (FFh none)...
    X3B x3_qtop, 1                  ; ...the list's top...
    X3B x3_qlsel, 1                 ; ...the selection it last followed...
    X3B x3_qgen, 1                  ; ...and a count bumped by every change
    X3B x3_q, X3_QMAX * QE_SIZE
    X3B x3d_ox, 2                   ; x3draw.inc: the content origin...
    X3B x3d_oy, 2
    X3B x3d_th, 2                   ; ...the theme table...
    X3B x3d_bw, 2                   ; ...the band: width, rows, stride...
    X3B x3d_bh, 2
    X3B x3d_bst, 2
    X3B x3d_cvw, 2                  ; ...the canvas: width, rows, stride, and
    X3B x3d_cvh, 2                  ; the picture row it starts at...
    X3B x3d_cvst, 2
    X3B x3d_cvy0, 2
    X3B x3d_lx2, 2                  ; ...a line's end and its deltas...
    X3B x3d_ly2, 2
    X3B x3d_ldx, 2
    X3B x3d_ldy, 2
    X3B x3d_bpp1, 1                 ; ...1 = the display is one bit...
    X3B x3d_cutf, 1                 ; ...the clip cut what was drawn...
    X3B x3d_bdim, 1                 ; ...grey the next band's ink...
    X3B x3d_cgap, 1                 ; ...extra tracking...
    X3B x3d_ctr, 1                  ; ...centre x3d_fstr's text...
    X3B x3d_pmode, 1                ; ...the plotter's target, its colour, its
    X3B x3d_pcol, 1                 ; dotting
    X3B x3d_pdim, 1
    X3B x3f_full, 1                 ; x3face.inc: 1 = this pass is a paint
    X3B x3f_fok, 1                  ; the colour dial faces built (bit 0 VU,
                                    ; bit 1 BUFFER) for the dials x3f_fdg names
    X3B x3f_dpos, 1                 ; a dial's needle, its mode, its zone
    X3B x3f_dmd, 1
    X3B x3f_dzone, 1
    X3B x3f_bst, 1                  ; a key: 0 up, 1 held, 2 latched
    X3B x3f_cw, 2                   ; the content box, this layout
    X3B x3f_ch, 2
    X3B x3f_slot, 2                 ; the element's key slot...
    X3B x3f_kpend, 2                ; ...its key, pending the draw
    X3B x3f_dlast, 4                ; each dial's last draw, in ticks
    X3B x3f_dfx, 2                  ; the dial being drawn: its x, its box
    X3B x3f_dx0, 2
    X3B x3f_dx1, 2
    X3B x3f_dy0, 2
    X3B x3f_dy1, 2
    X3B x3f_mst, 2                  ; ...its masks' stride and row pointers
    X3B x3f_kp, 2
    X3B x3f_rp, 2
    X3B x3f_kst, 2                  ; the knob's planes' stride
    X3B x3f_fdg, 2                  ; the dials the colour faces were built for
    X3B x3f_qy, 2                   ; a queue row's top
    X3B x3f_bx, 2                   ; a key: its place, picture, caption
    X3B x3f_by, 2
    X3B x3f_bpic, 2
    X3B x3f_bcap, 2
    X3B x3f_bcx, 2
    X3B x3f_dargs, 10               ; a deco op's arguments
    X3B x3f_tbuf, 64                ; a field's text
    X3B x3f_nbuf, 8                 ; a number's digits
    X3B x3_pfx_end, 0
    X3B x3_rects, X3_NB * 8         ; the keys, screen
    X3B x3_bflags, X3_NB * 2        ; ...as drawn
    X3B x3f_keys, X3F_NKEYS * 2     ; the elements' keys, as drawn
    X3_DEMOBSS                      ; x3demo.inc's own
    X3B x3f_fvu, X3F_FACESZ         ; the colour dial faces, VU and BUFFER
    X3B x3f_fbuf, X3F_FACESZ
    X3B x3_work, X3_WORKSZ          ; the band / the canvas
    OS88_BSS X3B_AT
    OS88_IMAGE_END
